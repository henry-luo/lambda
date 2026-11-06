#pragma once

#include "test_lambda_compiler.hpp"
#include "../lambda/runtime/interp.hpp"

// native fixtures share the CLI compiler policy, including explicit T0/AUTO
// controls. Restore both selectors even when a fatal assertion exits a test.
struct LambdaTestNativeCompiler {
    LambdaTier saved_tier = lambda_tier_selected();
    char* saved_compiler = nullptr;
    bool restore_compiler = false;

    LambdaTestNativeCompiler() {
        const char* compiler = getenv("LAMBDA_MIR_COMPILER");
        if (compiler) saved_compiler = strdup(compiler);
    }

    explicit LambdaTestNativeCompiler(LambdaTier tier) : LambdaTestNativeCompiler() { select(tier); }

    // explicit overrides are for tests of isolation between the two compilers.
    void select_compiler(const char* compiler) {
        ASSERT_TRUE(compiler && (!strcmp(compiler, "mvp") || !strcmp(compiler, "production")));
        ASSERT_TRUE(shell_setenv("LAMBDA_MIR_COMPILER", compiler));
        restore_compiler = true;
    }

    void select(LambdaTier tier) {
        const char* name = tier == LAMBDA_TIER_JIT ? "jit" :
            tier == LAMBDA_TIER_INTERP ? "interp" : "auto";
        LambdaTestCompilerEnv environment(name);
        if (!environment.valid) return;
        for (unsigned i = 0; environment.entries[i].key; i++) {
            if (strcmp(environment.entries[i].key, "LAMBDA_MIR_COMPILER")) continue;
            select_compiler(environment.entries[i].value);
        }
        lambda_tier_set(tier);
    }

    // a requested runtime denial must not pass because MVP rejected the AST.
    void expect_mvp_admitted(const Script* script) const {
        const char* selected = getenv("LAMBDA_MIR_COMPILER");
        if (lambda_tier_selected() != LAMBDA_TIER_JIT || !selected || strcmp(selected, "mvp")) return;
        EXPECT_NE(script, nullptr) << "MVP did not load the requested runtime test";
        if (!script) return;
        EXPECT_NE(script->main_func, nullptr) << "MVP did not compile the requested runtime test";
        EXPECT_FALSE(script->interp_supported) << "MVP runtime test selected T0";
    }

    ~LambdaTestNativeCompiler() {
        lambda_tier_set(saved_tier);
        if (restore_compiler) {
            bool restored = saved_compiler ? shell_setenv("LAMBDA_MIR_COMPILER", saved_compiler)
                : shell_unsetenv("LAMBDA_MIR_COMPILER");
            EXPECT_TRUE(restored);
        }
        free(saved_compiler);
    }

    LambdaTestNativeCompiler(const LambdaTestNativeCompiler&) = delete;
    LambdaTestNativeCompiler& operator=(const LambdaTestNativeCompiler&) = delete;
};
