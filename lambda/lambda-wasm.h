#ifndef LAMBDA_WASM_H
#define LAMBDA_WASM_H

#ifdef WASM_BUILD

// WASM-compatible stub implementations
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Simple stub for Runtime structure
typedef struct {
    void* parser;
    void* scripts;
} Runtime;

// Simple stub for Item type 
typedef void* Item;
#define ITEM_NULL ((Item)0)

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
