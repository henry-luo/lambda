#include <stdlib.h>
#include <string.h>
#include "../lambda/lambda.h"
#include "../lib/num_stack.h"
#include "/opt/homebrew/Cellar/mpdecimal/4.0.1/include/mpdecimal.h"

// Global context variable definition for tests
__thread Context* context = NULL;

// Minimal test context creation - only what tests need
Context* create_test_context() {
    Context* ctx = (Context*)calloc(1, sizeof(Context));
    if (!ctx) return NULL;
    
    // Initialize minimal context for testing
    ctx->num_stack = num_stack_create(16);
    ctx->ast_pool = NULL;
    ctx->consts = NULL;
    ctx->type_list = NULL;
    ctx->type_info = NULL;
    ctx->cwd = NULL;
    ctx->result = ITEM_NULL;
    ctx->heap = NULL;
    
    // Initialize decimal context (required for some math operations)
    ctx->decimal_ctx = (mpd_context_t*)malloc(sizeof(mpd_context_t));
    if (ctx->decimal_ctx) {
        mpd_defaultcontext(ctx->decimal_ctx);
    }
    
    return ctx;
}

void destroy_test_context(Context* ctx) {
    if (!ctx) return;
    
    if (ctx->num_stack) {
        num_stack_destroy(ctx->num_stack);
    }
    
    if (ctx->decimal_ctx) {
        free(ctx->decimal_ctx);
    }
    
    free(ctx);
}
