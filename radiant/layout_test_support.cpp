#include "layout.hpp"
#include "view.hpp"
extern "C" {
#include "../lib/mem-pool/include/mem_pool.h"
}

// Test support functions for memory pool management
void init_view_pool(LayoutContext* lycon) {
    if (!lycon) return;
    
    // Initialize memory pool for view allocation
    MemPoolError err = pool_variable_init(&lycon->pool, 1024 * 1024, MEM_POOL_NO_BEST_FIT);
    if (err != MEM_POOL_ERR_OK) {
        lycon->pool = nullptr;
    }
}

void cleanup_view_pool(LayoutContext* lycon) {
    if (!lycon || !lycon->pool) return;
    
    // Cleanup memory pool
    pool_variable_destroy(lycon->pool);
    lycon->pool = nullptr;
}

ViewBlock* alloc_view_block(LayoutContext* lycon) {
    if (!lycon || !lycon->pool) return nullptr;
    
    // Allocate ViewBlock from memory pool
    ViewBlock* block = (ViewBlock*)pool_calloc(lycon->pool, sizeof(ViewBlock));
    if (!block) return nullptr;
    
    // Initialize basic fields
    block->type = RDT_VIEW_BLOCK;
    block->parent = nullptr;
    block->next = nullptr;
    block->first_child = nullptr;
    block->last_child = nullptr;
    block->next_sibling = nullptr;
    block->prev_sibling = nullptr;
    
    // Initialize flex properties with defaults
    block->flex_grow = 0.0f;
    block->flex_shrink = 1.0f;
    block->flex_basis = -1; // auto
    block->flex_basis_is_percent = false;
    block->align_self = ALIGN_AUTO;
    block->order = 0;
    
    return block;
}
