/**
 * @file test_stubs.cpp
 * @brief Stub implementations for test builds to avoid complex dependencies
 */


#include "../lambda/transpiler.hpp"
#include <stdlib.h>
#include <string.h>

// Thread-local eval context for tests
__thread EvalContext* context = nullptr;

// Lambda home path stubs — runner.cpp is not linked into the shared test library,
// so provide equivalent definitions here.
const char* g_lambda_home = "./lambda";

void lambda_home_init(void) {
    const char* env = getenv("LAMBDA_HOME");
    if (env && env[0]) {
        g_lambda_home = env;
    }
}

char* lambda_home_path(const char* rel) {
    size_t home_len = strlen(g_lambda_home);
    size_t rel_len  = strlen(rel);
    char* out = (char*)malloc(home_len + 1 + rel_len + 1);
    if (!out) return NULL;
    memcpy(out, g_lambda_home, home_len);
    out[home_len] = '/';
    memcpy(out + home_len + 1, rel, rel_len + 1);
    return out;
}

// Stub implementation of load_script for test builds
Script* load_script(Runtime* runtime, const char* script_path, const char* source, bool is_import) {
    (void)runtime;
    (void)script_path;
    (void)source;
    (void)is_import;
    return nullptr;
}

// find_errors is now in lambda-error.cpp — no stub needed

// Helper functions for C code to access EvalContext members (used by path.c)
extern "C" {
Pool* eval_context_get_pool(EvalContext* ctx) {
    if (!ctx || !ctx->heap) return nullptr;
    return ctx->heap->pool;
}

NamePool* eval_context_get_name_pool(EvalContext* ctx) {
    if (!ctx) return nullptr;
    return ctx->name_pool;
}
}
