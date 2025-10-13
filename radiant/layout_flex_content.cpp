#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_content.hpp"

#include "../lib/log.h"

// Enhanced flex item content layout with full HTML nested content support (Phase 3.1)
// This is the main function used by the new flex layout system
void layout_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item) {
    if (!flex_item) return;

    log_debug("Enhanced flex item content layout for %p\n", flex_item);

    // Save parent context
    LayoutContext saved_context = *lycon;

    // Set up flex item as a proper containing block
    lycon->parent = (ViewGroup*)flex_item;
    lycon->prev_view = NULL;

    // Calculate content area dimensions accounting for box model
    int content_width = flex_item->width;
    int content_height = flex_item->height;
    int content_x_offset = 0;
    int content_y_offset = 0;

    if (flex_item->bound) {
        // Account for padding and border in content area
        content_width -= (flex_item->bound->padding.left + flex_item->bound->padding.right);
        content_height -= (flex_item->bound->padding.top + flex_item->bound->padding.bottom);
        content_x_offset = flex_item->bound->padding.left;
        content_y_offset = flex_item->bound->padding.top;

        if (flex_item->bound->border) {
            content_width -= (flex_item->bound->border->width.left + flex_item->bound->border->width.right);
            content_height -= (flex_item->bound->border->width.top + flex_item->bound->border->width.bottom);
            content_x_offset += flex_item->bound->border->width.left;
            content_y_offset += flex_item->bound->border->width.top;
        }
    }

    // Set up block formatting context for nested content
    lycon->block.width = content_width;
    lycon->block.height = content_height;
    lycon->block.advance_y = content_y_offset;
    lycon->block.max_width = 0;

    // Inherit text alignment and other block properties from flex item
    if (flex_item->blk) {
        lycon->block.text_align = flex_item->blk->text_align;
        // lycon->block.line_height = flex_item->blk->line_height;
    }

    // Set up line formatting context for inline content
    lycon->line.left = content_x_offset;
    lycon->line.right = content_x_offset + content_width;
    lycon->line.vertical_align = LXB_CSS_VALUE_BASELINE;

    line_init(lycon);

    // Layout all nested content using standard flow algorithm
    // This handles: text nodes, nested blocks, inline elements, images, etc.
    if (flex_item->node && flex_item->node->first_child()) {
        DomNode* child = flex_item->node->first_child();
        do {
            // Use standard layout flow - this handles all HTML content types
            layout_flow_node(lycon, child);
            child = child->next_sibling();
        } while (child);

        // Finalize any pending line content
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }

    // Update flex item content dimensions for intrinsic sizing
    flex_item->content_width = lycon->block.max_width;
    flex_item->content_height = lycon->block.advance_y - content_y_offset;

    // Restore parent context
    *lycon = saved_context;

    log_debug("Enhanced flex item content layout complete: %dx%d\n",
              flex_item->content_width, flex_item->content_height);
}

// Calculate intrinsic sizes for flex items containing various content types
void calculate_flex_item_intrinsic_sizes(ViewBlock* flex_item) {
    if (!flex_item) return;

    log_debug("Calculate intrinsic sizes for flex item %p\n", flex_item);

    IntrinsicSizes sizes = {0};

    // Calculate based on content type
    View* child = flex_item->child;
    while (child) {
        IntrinsicSizes child_sizes = calculate_child_intrinsic_sizes(child);

        // Combine sizes based on layout direction
        if (is_block_level_child(child)) {
            sizes.min_content = fmax(sizes.min_content, child_sizes.min_content);
            sizes.max_content = fmax(sizes.max_content, child_sizes.max_content);
        } else {
            sizes.min_content += child_sizes.min_content;
            sizes.max_content += child_sizes.max_content;
        }

        child = child->next;
    }

    // Apply constraints and aspect ratio
    apply_intrinsic_size_constraints(flex_item, &sizes);

    // Store calculated sizes in a temporary way until ViewBlock is extended
    // For now, we'll use existing properties or store in comments for future use
    // TODO: Add intrinsic_min_width and intrinsic_max_width to ViewBlock structure

    log_debug("Intrinsic sizes calculated: min=%d, max=%d\n",
              sizes.min_content, sizes.max_content);

    // Store in existing width/height as fallback (can be overridden later)
    if (flex_item->width <= 0) {
        flex_item->width = sizes.max_content;
    }
    if (flex_item->height <= 0) {
        flex_item->height = 100; // Default height
    }
}

// Calculate intrinsic sizes for different child types
IntrinsicSizes calculate_child_intrinsic_sizes(View* child) {
    IntrinsicSizes sizes = {0};

    if (!child) return sizes;

    switch (child->type) {
        case RDT_VIEW_BLOCK: {
            ViewBlock* block = (ViewBlock*)child;
            sizes = calculate_block_intrinsic_sizes(block);
            break;
        }
        case RDT_VIEW_TEXT: {
            ViewText* text = (ViewText*)child;
            sizes = calculate_text_intrinsic_sizes(text);
            break;
        }
        case RDT_VIEW_INLINE: {
            sizes = calculate_inline_intrinsic_sizes(child);
            break;
        }
        case RDT_VIEW_INLINE_BLOCK: {
            ViewBlock* inline_block = (ViewBlock*)child;
            sizes = calculate_block_intrinsic_sizes(inline_block);
            break;
        }
        default:
            // Unknown type, use zero sizes
            break;
    }

    return sizes;
}

// Calculate intrinsic sizes for block elements
IntrinsicSizes calculate_block_intrinsic_sizes(ViewBlock* block) {
    IntrinsicSizes sizes = {0};

    if (!block) return sizes;

    // For blocks, min-content is the width of the longest word/element
    // max-content is the width if no wrapping occurred

    // Simple approximation - use current dimensions as baseline
    sizes.min_content = block->width / 4; // Rough estimate for min-content
    sizes.max_content = block->width;

    // Consider constraints
    if (block->blk) {
        if (block->blk->given_min_width > 0) {
            sizes.min_content = max(sizes.min_content, block->blk->given_min_width);
        }
        if (block->blk->given_max_width > 0) {
            sizes.max_content = min(sizes.max_content, block->blk->given_max_width);
        }
    }
    return sizes;
}

// Calculate intrinsic sizes for text elements
IntrinsicSizes calculate_text_intrinsic_sizes(ViewText* text) {
    IntrinsicSizes sizes = {0};

    if (!text) return sizes;

    // Enhanced text intrinsic size calculation
    // Try to use available text properties, fallback to defaults if not available
    int text_len = 10; // Default estimate
    int char_width = 8; // Default character width estimate

    // TODO: When text_data and font_size properties become available, use this enhanced logic:
    /*
    if (text->text_data && text->font_size) {
        text_len = strlen((const char*)text->text_data);
        char_width = text->font_size ? text->font_size / 2 : 8;

        // Find longest word for min-content
        const char* str = (const char*)text->text_data;
        int max_word_len = 0;
        int current_word_len = 0;

        for (int i = 0; i <= text_len; i++) {
            if (str[i] == ' ' || str[i] == '\t' || str[i] == '\n' || str[i] == '\0') {
                max_word_len = max(max_word_len, current_word_len);
                current_word_len = 0;
            } else {
                current_word_len++;
            }
        }

        sizes.min_content = max_word_len * char_width;
        sizes.max_content = text_len * char_width;
        return sizes;
    }
    */

    // Current fallback implementation
    sizes.min_content = text_len * char_width / 4; // Rough min-content estimate
    sizes.max_content = text_len * char_width;     // Max-content estimate

    return sizes;
}

// Calculate intrinsic sizes for inline elements
IntrinsicSizes calculate_inline_intrinsic_sizes(View* inline_view) {
    IntrinsicSizes sizes = {0};

    if (!inline_view) return sizes;

    // Enhanced inline element intrinsic size calculation
    // TODO: When View child property becomes available, use this enhanced logic:
    /*
    View* child = inline_view->child;
    while (child) {
        IntrinsicSizes child_sizes = calculate_child_intrinsic_sizes(child);
        sizes.min_content += child_sizes.min_content;
        sizes.max_content += child_sizes.max_content;
        child = child->next;
    }
    */

    // Current fallback implementation using available properties
    // Try to use width if available, otherwise use defaults
    int base_width = 100; // Default width

    // TODO: When View width property becomes available, uncomment:
    // if (inline_view->width > 0) {
    //     base_width = inline_view->width;
    // }

    sizes.min_content = base_width / 2;  // Reasonable min-content
    sizes.max_content = base_width;      // Max-content based on current width

    return sizes;
}

// Apply constraints to intrinsic sizes
void apply_intrinsic_size_constraints(ViewBlock* flex_item, IntrinsicSizes* sizes) {
    if (!flex_item || !sizes) return;

    // Apply min/max width constraints
    if (flex_item->min_width > 0) {
        sizes->min_content = max(sizes->min_content, flex_item->min_width);
    }
    if (flex_item->max_width > 0) {
        sizes->max_content = min(sizes->max_content, flex_item->max_width);
    }

    // Apply aspect ratio constraints
    if (flex_item->aspect_ratio > 0) {
        // Adjust sizes based on aspect ratio
        int height_constraint = flex_item->height > 0 ? flex_item->height : 100; // Default height
        int width_from_aspect = (int)(height_constraint * flex_item->aspect_ratio);

        sizes->min_content = max(sizes->min_content, width_from_aspect);
        sizes->max_content = max(sizes->max_content, width_from_aspect);
    }

    // Ensure min <= max
    if (sizes->min_content > sizes->max_content) {
        sizes->max_content = sizes->min_content;
    }
}

// Check if a child is block-level
bool is_block_level_child(View* child) {
    if (!child) return false;

    return (child->type == RDT_VIEW_BLOCK ||
            child->type == RDT_VIEW_LIST_ITEM ||
            child->type == RDT_VIEW_INLINE_BLOCK);
}

// Layout flex item content for sizing (first pass)
void layout_flex_item_content_for_sizing(LayoutContext* lycon, ViewBlock* flex_item) {
    if (!flex_item) return;

    log_debug("Layout flex item content for sizing\n");

    // Calculate intrinsic sizes for measurement phase
    calculate_flex_item_intrinsic_sizes(flex_item);
}

// Final layout of flex item contents with determined sizes
void layout_flex_item_final_content(LayoutContext* lycon, ViewBlock* flex_item) {
    if (!flex_item) return;

    log_debug("Final layout of flex item content\n");

    // Use the main enhanced content layout function
    layout_flex_item_content(lycon, flex_item);
}
