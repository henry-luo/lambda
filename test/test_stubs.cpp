/**
 * @file test_stubs.cpp
 * @brief Stub implementations for test builds to avoid complex dependencies
 */


#include "../lambda/transpiler.hpp"

// Thread-local eval context for tests
__thread EvalContext* context = nullptr;

// Stub implementation of load_script for test builds
Script* load_script(Runtime* runtime, const char* script_path, const char* source, bool is_import) {
    (void)runtime;
    (void)script_path;
    (void)source;
    (void)is_import;
    return nullptr;
}

// Stub implementation of find_errors for test builds
void find_errors(TSNode node) {
    (void)node; // Suppress unused parameter warning
    // Stub implementation - do nothing for tests
}

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
