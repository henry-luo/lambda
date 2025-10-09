#include "view.hpp"
#include "layout_flex.hpp"
#include "layout.hpp"
extern "C" {
#include <stdlib.h>
#include <string.h>
}

// Allocate a new ViewBlock with integrated flex support
ViewBlock* alloc_view_block(LayoutContext* lycon) {
    if (!lycon) return nullptr;

    ViewBlock* block = (ViewBlock*)calloc(1, sizeof(ViewBlock));
    if (!block) return nullptr;

    // Initialize ViewBlock fields
    block->type = RDT_VIEW_BLOCK;
    block->x = block->y = 0;
    block->width = block->height = 0;
    block->content_width = block->content_height = 0;

    // Initialize flex item properties with defaults
    block->flex_grow = 0.0f;
    block->flex_shrink = 1.0f;
    block->flex_basis = -1; // auto
    block->align_self = ALIGN_START; // Use ALIGN_START instead of ALIGN_AUTO
    block->order = 0;
    block->flex_basis_is_percent = false;

    return block;
}

// Free a ViewBlock and its flex container resources
void free_view_block(ViewBlock* block) {
    if (!block) return;

    // Cleanup flex container if it exists
    if (block->embed && block->embed->flex) {
        free(block->embed->flex);
    }
    // Free embed properties
    if (block->embed) {
        free(block->embed);
    }

    // Free boundary properties
    if (block->bound) {
        if (block->bound->border) free(block->bound->border);
        if (block->bound->background) free(block->bound->background);
        free(block->bound);
    }

    // Free block properties
    if (block->blk) {
        free(block->blk);
    }

    // Free scroller properties
    if (block->scroller) {
        if (block->scroller->pane) free(block->scroller->pane);
        free(block->scroller);
    }

    // Free font properties
    if (block->font) {
        if (block->font->family) free(block->font->family);
        free(block->font);
    }

    // Free inline properties
    if (block->in_line) {
        free(block->in_line);
    }

    free(block);
}

// Helper to set flex item properties
void set_flex_item_properties(ViewBlock* item,
                             float flex_grow,
                             float flex_shrink,
                             int flex_basis,
                             bool flex_basis_is_percent,
                             AlignType align_self,
                             int order) {
    if (!item) return;

    item->flex_grow = flex_grow;
    item->flex_shrink = flex_shrink;
    item->flex_basis = flex_basis;
    item->flex_basis_is_percent = flex_basis_is_percent;
    item->align_self = align_self;
    item->order = order;
}

// Add a child to a flex container
void add_flex_child(ViewBlock* container, ViewBlock* child) {
    if (!container || !child) return;

    child->parent = (ViewGroup*)container;

    if (!container->child) {
        container->child = (View*)child;
    } else {
        // Find the last child and append
        View* last_child = container->child;
        while (last_child->next) {
            last_child = last_child->next;
        }
        last_child->next = (View*)child;
    }

    // Mark container for reflow
    // if (container->embed && container->embed->flex_container) {
    //     container->embed->flex_container->needs_reflow = true;
    // }
}
