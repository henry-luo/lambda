// Minimal context implementation for input tests
// This provides the necessary runtime context without JIT/MIR dependencies

#include <stdlib.h>
#include <string.h>
#include "../lambda/lambda.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/strview.h"
#include "../lib/num_stack.h"

// Thread-local context (this is what the tests need)
__thread Context* context = NULL;

// Simple context creation for tests
Context* create_test_context() {
    Context* ctx = (Context*)calloc(1, sizeof(Context));
    if (!ctx) return NULL;
    
    // Initialize minimal context for testing - similar to the original test_math.c version
    ctx->num_stack = num_stack_create(16);  // Create the num_stack that test_math.c expects
    ctx->ast_pool = NULL;  // Not needed for input parsing
    ctx->consts = NULL;  // Not needed for input parsing
    ctx->type_list = NULL;  // Not needed for input parsing
    ctx->type_info = NULL;  // Not needed for input parsing
    ctx->cwd = NULL;  // Not needed for input parsing
    ctx->result = ITEM_NULL;  // Initialize Item properly
    ctx->heap = NULL;  // Will be initialized in setup
    
    // Initialize decimal context (required for some math operations)
    mpd_defaultcontext(&ctx->decimal_ctx);
    
    return ctx;
}

void destroy_test_context(Context* ctx) {
    if (!ctx) return;
    if (ctx->num_stack) {
        num_stack_destroy((num_stack_t*)ctx->num_stack);
    }
    free(ctx);
}

// Stub implementation for string_from_strview (used by name_pool)
String* string_from_strview(VariableMemPool* pool, StrView sv) {
    // Create a simple string from string view
    String* str = (String*)pool_calloc(pool, sizeof(String) + sv.length + 1);
    if (!str) return NULL;
    
    str->len = sv.length;
    memcpy(str->chars, sv.str, sv.length);
    str->chars[sv.length] = '\0';
    
    return str;
}

// Stub implementation for load_script (used by build_ast.o for module imports)
// Input tests don't actually load scripts, so this can return NULL
void* load_script(void* runtime, const char* script_path, const char* source) {
    // For input tests, we don't need to actually load scripts
    return NULL;
}
