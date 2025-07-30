#include "lambda-wasm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef WASM_BUILD

// Forward declarations for the core Lambda functions we want to expose
extern char* read_text_file(const char *filename);
extern int string_buffer_append(char* dest, const char* src, size_t max_len);

// Global runtime instance for WASM
static Runtime* global_runtime = NULL;

// Simple WASM entry points
const char* lambda_version() {
    return "Lambda WASM 1.0.0";
}

int lambda_init() {
    if (global_runtime) return 1; // already initialized
    
    global_runtime = lambda_runtime_new();
    return global_runtime ? 1 : 0;
}

// Simple string processing function
int lambda_process_string(const char* input, char* output, int max_output_len) {
    if (!input || !output) return 0;
    
    // Simple echo for now - just copy input to output
    int len = strlen(input);
    if (len >= max_output_len) len = max_output_len - 1;
    
    strncpy(output, input, len);
    output[len] = '\0';
    
    return len;
}

// Runtime functions for WASM
Runtime* lambda_runtime_new() {
    Runtime* runtime = (Runtime*)malloc(sizeof(Runtime));
    if (!runtime) return NULL;
    
    memset(runtime, 0, sizeof(Runtime));
    // In a full implementation, we would initialize parser and scripts here
    return runtime;
}

void lambda_runtime_free(Runtime* runtime) {
    if (runtime) {
        // In a full implementation, we would cleanup parser and scripts here
        free(runtime);
    }
    if (global_runtime == runtime) {
        global_runtime = NULL;
    }
}

Item lambda_run_code(Runtime* runtime, const char* source_code) {
    if (!runtime || !source_code) return ITEM_NULL;
    
    // Placeholder implementation - in a full version this would:
    // 1. Parse the source code using Tree-sitter
    // 2. Build AST
    // 3. Transpile to C
    // 4. JIT compile and execute
    // For now, just return a dummy result
    
    return (Item)"result";
}

const char* lambda_item_to_string(Item item) {
    if (!item) return "null";
    
    // Placeholder - in a full version this would serialize the item
    return (const char*)item;
}

// WASI compatible main function
int main(int argc, char* argv[]) {
    return 0;
}

// Export functions for JS
__attribute__((export_name("wasm_lambda_version"))) const char* wasm_lambda_version() {
    return lambda_version();
}

__attribute__((export_name("wasm_lambda_init"))) int wasm_lambda_init() {
    return lambda_init();
}

__attribute__((export_name("wasm_lambda_process_string"))) int wasm_lambda_process_string(const char* input, char* output, int max_output_len) {
    return lambda_process_string(input, output, max_output_len);
}

__attribute__((export_name("wasm_lambda_runtime_new"))) Runtime* wasm_lambda_runtime_new() {
    return lambda_runtime_new();
}

__attribute__((export_name("wasm_lambda_runtime_free"))) void wasm_lambda_runtime_free(Runtime* runtime) {
    lambda_runtime_free(runtime);
}

__attribute__((export_name("wasm_lambda_run_code"))) Item wasm_lambda_run_code(Runtime* runtime, const char* source_code) {
    return lambda_run_code(runtime, source_code);
}

__attribute__((export_name("wasm_lambda_item_to_string"))) const char* wasm_lambda_item_to_string(Item item) {
    return lambda_item_to_string(item);
}

// Export memory management functions
__attribute__((export_name("malloc"))) void* wasm_malloc(size_t size) {
    return malloc(size);
}

__attribute__((export_name("free"))) void wasm_free(void* ptr) {
    free(ptr);
}

#endif // WASM_BUILD
