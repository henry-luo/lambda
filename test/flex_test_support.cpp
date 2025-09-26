#include "../radiant/layout.hpp"
#include "../radiant/view.hpp"
#include <cstdlib>
#include <cstring>

// Simple memory pool implementation for testing
static void* simple_pool_alloc(size_t size) {
    void* ptr = calloc(1, size);
    return ptr;
}

// Test support functions for memory pool management
void init_view_pool(LayoutContext* lycon) {
    if (!lycon) return;
    
    // For testing, we'll use a simple approach
    lycon->pool = nullptr; // Not using actual memory pool for tests
}

void cleanup_view_pool(LayoutContext* lycon) {
    if (!lycon) return;
    
    // For testing, cleanup is handled by individual free() calls
    lycon->pool = nullptr;
}

ViewBlock* alloc_view_block(LayoutContext* lycon) {
    if (!lycon) return nullptr;
    
    // Allocate ViewBlock using simple malloc for testing
    ViewBlock* block = (ViewBlock*)simple_pool_alloc(sizeof(ViewBlock));
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
    
    // Initialize new properties with defaults
    block->aspect_ratio = 0.0f;
    block->baseline_offset = 0;
    block->margin_top_auto = false;
    block->margin_right_auto = false;
    block->margin_bottom_auto = false;
    block->margin_left_auto = false;
    block->width_is_percent = false;
    block->height_is_percent = false;
    block->min_width_is_percent = false;
    block->max_width_is_percent = false;
    block->min_height_is_percent = false;
    block->max_height_is_percent = false;
    block->min_width = 0;
    block->max_width = 0;
    block->min_height = 0;
    block->max_height = 0;
    block->position = POS_STATIC;
    block->visibility = VIS_VISIBLE;
    
    return block;
}
