#ifndef TEST_LAMBDA_TIER_HELPERS_HPP
#define TEST_LAMBDA_TIER_HELPERS_HPP

#include <gtest/gtest.h>
#include <cstring>
#include "../lib/shell.h"

// D8.1.1v15: an unsupported pinned script fails before execution, never in MIR.
inline void expect_interp_rejection(const char* executable, const char* script,
        const char* unsupported_kind, bool procedural = true) {
    const char* run_args[] = {executable, "run", script, NULL};
    const char* direct_args[] = {executable, script, NULL};
    ShellEnvEntry env[] = {
        {"LAMBDA_TIER", "interp"},
        {"LAMBDA_DISABLE_MIR_CACHE", "1"},
        {NULL, NULL}
    };
    ShellOptions options = {};
    options.env = env;
    options.timeout_ms = 60000;
    ShellResult result = shell_exec(executable,
        procedural ? run_args : direct_args, &options);
    const char* errors = result.stderr_buf ? result.stderr_buf : "";
    EXPECT_FALSE(result.timed_out) << script;
    EXPECT_NE(result.exit_code, 0) << script;
    EXPECT_EQ(result.stdout_len, 0u) << script;
    EXPECT_NE(strstr(errors, "error[E501]"), nullptr) << errors;
    EXPECT_NE(strstr(errors, unsupported_kind), nullptr) << errors;
    EXPECT_NE(strstr(errors, "MIR fallback is disabled"), nullptr) << errors;
    EXPECT_NE(strstr(errors, "interp: executed=0 fallback=0 excluded="), nullptr)
        << errors;
    EXPECT_EQ(strstr(errors, "excluded=0"), nullptr) << errors;
    shell_result_free(&result);
}

#endif
