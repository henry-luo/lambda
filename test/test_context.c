// Minimal context implementation for input tests
// This provides the necessary runtime context without JIT/MIR dependencies

#include <stdlib.h>
#include <string.h>
#include <mpdecimal.h>
#include "../lambda/lambda.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/strview.h"
#include "../lib/num_stack.h"
#include "../lib/arraylist.h"

// Define minimal Heap structure for tests
typedef struct Heap {
    VariableMemPool *pool;  // memory pool for the heap
    ArrayList *entries;     // list of allocation entries
} Heap;

// Forward declarations
Context* create_test_context(void);
static void ensure_context_initialized(void);

// Thread-local context (this is what the tests need)
__thread Context* context = NULL;

// Initialize context on first use
static void ensure_context_initialized() {
    if (context == NULL) {
        context = create_test_context();
    }
}

// Simple context creation for tests (global for test_math.c)
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
    
    // Leave heap as NULL - we'll just use regular malloc/calloc
    ctx->heap = NULL;
    
    // Initialize decimal context (required for some math operations)
    ctx->decimal_ctx = (mpd_context_t*)malloc(sizeof(mpd_context_t));
    mpd_defaultcontext(ctx->decimal_ctx);
    
    return ctx;
}

void destroy_test_context(Context* ctx) {
    if (!ctx) return;
    if (ctx->num_stack) {
        num_stack_destroy((num_stack_t*)ctx->num_stack);
    }
    if (ctx->decimal_ctx) {
        free(ctx->decimal_ctx);
        ctx->decimal_ctx = NULL;
    }
    free(ctx);
}

// Stub implementation for load_script (used by build_ast.o for module imports)
// Input tests don't actually load scripts, so this can return NULL
void* load_script(void* runtime, const char* script_path, const char* source) {
    // For input tests, we don't need to actually load scripts
    return NULL;
}

// Stub implementations for lambda-data functions that input tests need
void expand_list(void* list) {
    // For input tests, we don't need dynamic list expansion
    // This is a no-op stub
}

void frame_end() {
    // For input tests, we don't need frame management
    // This is a no-op stub
}

void frame_start() {
    // For input tests, we don't need frame management
    // This is a no-op stub
}

void* heap_calloc(size_t size) {
    // Ensure context is initialized
    ensure_context_initialized();
    
    // For input tests, just use regular calloc for simplicity
    // The actual heap management causes issues
    return calloc(1, size);
}

// Heap management stubs for test_math.c
void heap_init() {
    // For input tests, we don't need heap initialization
    // This is a no-op stub
}

void heap_destroy() {
    // For input tests, we don't need heap destruction
    // This is a no-op stub
}
