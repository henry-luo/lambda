#pragma once

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "lib/shell.h"

// select MVP only for JIT arms; explicit T0/AUTO arms keep their actual tier.
struct LambdaTestCompilerEnv {
    ShellEnvEntry entries[16] = {};
    bool valid = true;

    explicit LambdaTestCompilerEnv(const char* tier = nullptr,
            const ShellEnvEntry* extra = nullptr,
            const char* required_compiler = nullptr) {
        const char* selected = required_compiler ? required_compiler
            : getenv("LAMBDA_TEST_MIR_COMPILER");
        if (tier && strcmp(tier, "jit") && strcmp(tier, "auto") && strcmp(tier, "interp")) {
            ADD_FAILURE() << "invalid Lambda test tier: " << tier;
            valid = false; return;
        }
        if (selected && *selected && strcmp(selected, "production") && strcmp(selected, "mvp")) {
            ADD_FAILURE() << "LAMBDA_TEST_MIR_COMPILER must be production or mvp";
            valid = false; return;
        }
        unsigned count = 0;
        if (selected && *selected) {
            if (!tier) tier = "jit";
            entries[count++] = {"LAMBDA_EXEC_BACKEND", tier};
            entries[count++] = {"LAMBDA_MIR_COMPILER", strcmp(tier, "jit") ? "production" : selected};
        } else if (tier) entries[count++] = {"LAMBDA_EXEC_BACKEND", tier};
        for (unsigned i = 0; extra && extra[i].key; i++) {
            if (count + 1 >= sizeof(entries) / sizeof(entries[0])) {
                ADD_FAILURE() << "Lambda test environment exceeds its capacity";
                valid = false; return;
            }
            // compiler/tier policy must not be overridden by unrelated test knobs.
            if (!strcmp(extra[i].key, "LAMBDA_EXEC_BACKEND") ||
                    !strcmp(extra[i].key, "LAMBDA_MIR_COMPILER")) {
                ADD_FAILURE() << "pass Lambda compiler selection through the test policy";
                valid = false; return;
            }
            entries[count++] = extra[i];
        }
    }

    // batch execution still streams popen output; quote every environment value.
    bool command_prefix(char* buffer, size_t capacity) const {
        if (!valid || !capacity) return false;
        size_t used = 0; buffer[0] = '\0';
        for (unsigned i = 0; entries[i].key; i++) {
#ifdef _WIN32
            int size = snprintf(buffer + used, capacity - used, "set \"%s=%s\" && ",
                entries[i].key, entries[i].value);
#else
            char* quoted = shell_quote_arg(entries[i].value);
            if (!quoted) return false;
            int size = snprintf(buffer + used, capacity - used, "%s=%s ",
                entries[i].key, quoted);
            free(quoted);
#endif
            if (size < 0 || (size_t)size >= capacity - used) {
                ADD_FAILURE() << "Lambda compiler environment prefix exceeds its buffer";
                return false;
            }
            used += (size_t)size;
        }
        return true;
    }
};

// keep explicit CLI/environment tiers when applying the compiler lane to raw CLI tests.
inline ShellResult lambda_test_shell_exec(const char* executable, const char** args,
        const ShellOptions* options = nullptr, bool batch_results = false) {
    ShellOptions selected = options ? *options : ShellOptions{};
    ShellEnvEntry extra[14] = {};
    unsigned count = 0;
    const char* tier = nullptr;
    for (unsigned i = 0; selected.env && selected.env[i].key; i++) {
        if (!strcmp(selected.env[i].key, "LAMBDA_EXEC_BACKEND")) {
            tier = selected.env[i].value;
        } else {
            if (count + 1 >= sizeof(extra) / sizeof(extra[0])) {
                ADD_FAILURE() << "Lambda CLI test environment exceeds its capacity";
                ShellResult failure = {}; failure.exit_code = -1; return failure;
            }
            extra[count++] = selected.env[i];
        }
    }
    for (unsigned i = 1; args && args[i]; i++) {
        if (!strncmp(args[i], "--tier=", 7)) tier = args[i] + 7;
        else if (!strcmp(args[i], "--tier") && args[i + 1]) tier = args[++i];
    }
    LambdaTestCompilerEnv environment(tier, extra);
    if (!environment.valid) {
        ShellResult failure = {}; failure.exit_code = -1; return failure;
    }
    selected.env = environment.entries;
    ShellResult result = shell_exec(executable, args, &selected);
    // an unsupported compiler node cannot satisfy an expected language error.
    const char* diagnostic = "error[E501]: Lambda MIR MVP:";
    // batches retain each script's failure status; failing setup would skip all selected cases.
    if (!batch_results && ((result.stderr_buf && strstr(result.stderr_buf, diagnostic)) ||
            (result.stdout_buf && strstr(result.stdout_buf, diagnostic))))
        ADD_FAILURE() << "MVP admission failed before the expected language behavior\n"
            << (result.stderr_buf ? result.stderr_buf : result.stdout_buf);
    return result;
}
