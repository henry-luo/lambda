#include "lambda-wasm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef WASM_BUILD

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
    
    // Initialize with stub values for now
    memset(runtime, 0, sizeof(Runtime));
    return runtime;
}

void lambda_runtime_free(Runtime* runtime) {
    if (runtime) {
        // Simple cleanup
        free(runtime);
    }
    if (global_runtime == runtime) {
        global_runtime = NULL;
    }
}

Item lambda_run_code(Runtime* runtime, const char* source_code) {
    if (!runtime || !source_code) return 0; // ITEM_NULL equivalent
    
    // For now, just return a dummy value
    // In a full implementation, this would use the actual Lambda runtime
    return (Item)42; // dummy result
}

const char* lambda_item_to_string(Item item) {
    if (!item) return "null";
    
    // For now, just return a simple string representation
    // In a full implementation, this would properly serialize the Lambda item
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)item);
    return buffer;
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
