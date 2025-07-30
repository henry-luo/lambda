#ifndef LAMBDA_WASM_H
#define LAMBDA_WASM_H

#ifdef WASM_BUILD

// Include Lambda types
#include "lambda.h"

// Forward declarations for Lambda types (from transpiler.h)
typedef struct Runtime {
    // Simple stub fields for WASM build
    int initialized;
    void* reserved;
} Runtime;
// Item is already defined in lambda.h as uint64_t
// #define ITEM_NULL is already defined in lambda.h

// WASM function exports
const char* lambda_version();
int lambda_init();
int lambda_process_string(const char* input, char* output, int max_output_len);

// Runtime functions for WASM
Runtime* lambda_runtime_new();
void lambda_runtime_free(Runtime* runtime);
Item lambda_run_code(Runtime* runtime, const char* source_code);
const char* lambda_item_to_string(Item item);

#endif // WASM_BUILD

#endif // LAMBDA_WASM_H
