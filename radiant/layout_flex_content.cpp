#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_content.hpp"

#include "../lib/log.h"

// Layout content within a flex item
void layout_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item) {
    if (!flex_item) return;

    log_debug("Layout flex item content for %p\n", flex_item);

    // Save current context
    Blockbox pa_block = lycon->block;
    Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;
    ViewGroup* pa_parent = lycon->parent;

    // Set up flex item context
    lycon->parent = (ViewGroup*)flex_item;
    lycon->prev_view = NULL;
    lycon->block.width = flex_item->width;
    lycon->block.height = flex_item->height;
    lycon->block.advance_y = 0;
    lycon->block.max_width = 0;
    lycon->line.left = 0;
    lycon->line.right = flex_item->width;
    lycon->line.vertical_align = LXB_CSS_VALUE_BASELINE;
    line_init(lycon);

    // Layout child content
    DomNode* child = flex_item->node ? flex_item->node->first_child() : NULL;
    if (child) {
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling();
        } while (child);

        // Handle last line if needed
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }

    // Calculate final content dimensions
    flex_item->content_width = lycon->block.max_width;
    flex_item->content_height = lycon->block.advance_y;

    // Restore context
    lycon->block = pa_block;
    lycon->line = pa_line;
    lycon->font = pa_font;
    lycon->parent = pa_parent;

    log_debug("Flex item content layout complete: %dx%d\n",
              flex_item->content_width, flex_item->content_height);
}

// Layout block content within a flex item
void layout_block_in_flex_item(LayoutContext* lycon, ViewBlock* block, ViewBlock* flex_item) {
    if (!block || !flex_item) return;

    log_debug("Layout block in flex item");

    // Set up containing block context for the nested block
    LayoutContext item_context = *lycon;
    item_context.block.width = flex_item->width;
    item_context.block.height = flex_item->height;

    // Layout the block normally within the flex item constraints
    DisplayValue display = {.outer = LXB_CSS_VALUE_BLOCK, .inner = LXB_CSS_VALUE_FLOW};
    layout_block(&item_context, block->node, display);

    // Handle overflow and clipping if necessary
    handle_flex_item_overflow(flex_item, block);
}

// Layout inline content within a flex item
void layout_inline_in_flex_item(LayoutContext* lycon, View* inline_view, ViewBlock* flex_item) {
    if (!inline_view || !flex_item) return;

    log_debug("Layout inline in flex item\n");

    // Create inline formatting context within flex item
    LayoutContext inline_ctx = *lycon;
    inline_ctx.block.width = flex_item->width;
    inline_ctx.block.height = flex_item->height;
    inline_ctx.line.left = 0;
    inline_ctx.line.right = flex_item->width;

    // Layout inline content with proper line breaking
    if (inline_view->type == RDT_VIEW_INLINE) {
        ViewSpan* span = (ViewSpan*)inline_view;
        layout_inline(&inline_ctx, span->node, span->display);
    } else if (inline_view->type == RDT_VIEW_TEXT) {
        layout_text(&inline_ctx, ((ViewText*)inline_view)->node);
    }

    // Update flex item dimensions based on content
    update_flex_item_from_inline_content(flex_item, &inline_ctx);
}

// Handle overflow in flex items
void handle_flex_item_overflow(ViewBlock* flex_item, ViewBlock* content_block) {
    if (!flex_item || !content_block) return;

    // Check for horizontal overflow
    if (content_block->width > flex_item->width) {
        if (!flex_item->scroller) {
            flex_item->scroller = (ScrollProp*)calloc(1, sizeof(ScrollProp));
            flex_item->scroller->overflow_x = LXB_CSS_VALUE_VISIBLE;
            flex_item->scroller->overflow_y = LXB_CSS_VALUE_VISIBLE;
        }
        flex_item->scroller->has_hz_overflow = true;

        // Apply clipping if overflow is not visible
        if (flex_item->scroller->overflow_x == LXB_CSS_VALUE_HIDDEN ||
            flex_item->scroller->overflow_x == LXB_CSS_VALUE_CLIP) {
            flex_item->scroller->has_clip = true;
            flex_item->scroller->clip.left = 0;
            flex_item->scroller->clip.right = flex_item->width;
            flex_item->scroller->clip.top = 0;
            flex_item->scroller->clip.bottom = flex_item->height;
        }
    }

    // Check for vertical overflow
    if (content_block->height > flex_item->height) {
        if (!flex_item->scroller) {
            flex_item->scroller = (ScrollProp*)calloc(1, sizeof(ScrollProp));
            flex_item->scroller->overflow_x = LXB_CSS_VALUE_VISIBLE;
            flex_item->scroller->overflow_y = LXB_CSS_VALUE_VISIBLE;
        }
        flex_item->scroller->has_vt_overflow = true;

        // Apply clipping if overflow is not visible
        if (flex_item->scroller->overflow_y == LXB_CSS_VALUE_HIDDEN ||
            flex_item->scroller->overflow_y == LXB_CSS_VALUE_CLIP) {
            flex_item->scroller->has_clip = true;
            flex_item->scroller->clip.left = 0;
            flex_item->scroller->clip.right = flex_item->width;
            flex_item->scroller->clip.top = 0;
            flex_item->scroller->clip.bottom = flex_item->height;
        }
    }
}

// Update flex item dimensions from inline content
void update_flex_item_from_inline_content(ViewBlock* flex_item, LayoutContext* inline_ctx) {
    if (!flex_item || !inline_ctx) return;

    // Update content dimensions based on inline layout
    flex_item->content_width = max(flex_item->content_width, inline_ctx->block.max_width);
    flex_item->content_height = max(flex_item->content_height, inline_ctx->block.advance_y);

    log_debug("Updated flex item from inline content: %dx%d\n",
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
        if (block->blk->min_width > 0) {
            sizes.min_content = max(sizes.min_content, block->blk->min_width);
        }
        if (block->blk->max_width > 0) {
            sizes.max_content = min(sizes.max_content, block->blk->max_width);
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

    // This is a simplified layout pass to determine intrinsic sizes
    // We don't need to do full layout, just calculate content requirements

    calculate_flex_item_intrinsic_sizes(flex_item);

    // Set preliminary dimensions based on intrinsic sizes
    // TODO: When intrinsic properties are added to ViewBlock, use:
    // if (flex_item->width <= 0) {
    //     flex_item->width = flex_item->intrinsic_max_width;
    // }

    // Current fallback using calculated sizes from the function above
    if (flex_item->width <= 0) {
        // Use a reasonable default that considers content
        flex_item->width = 200; // Default width for sizing
    }
    if (flex_item->height <= 0) {
        flex_item->height = 100; // Default height for sizing
    }
}

// Final layout of flex item contents with determined sizes
void layout_flex_item_final_content(LayoutContext* lycon, ViewBlock* flex_item) {
    if (!flex_item) return;

    log_debug("Final layout of flex item content\n");

    // Now that flex algorithm has determined final sizes, do full content layout
    layout_flex_item_content(lycon, flex_item);
}
