/**
 * BlockContext - Unified Block Formatting Context Implementation
 *
 * This module implements the unified BlockContext API that combines:
 * - Layout state management (from BlockContext)
 * - Float tracking and positioning (from FloatContext + BlockFormattingContext)
 * - BFC hierarchy management
 *
 * CSS 2.2 Section 9.4.1 - Block formatting contexts
 * CSS 2.2 Section 9.5.1 - Float positioning rules
 */

#include "layout.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include <cmath>
#include <cfloat>

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Check if a FloatBox intersects a Y range [y_top, y_bottom)
 */
static bool float_intersects_y_range(FloatBox* box, float y_top, float y_bottom) {
    return !(box->margin_box_bottom <= y_top || box->margin_box_top >= y_bottom);
}

// ============================================================================
// BlockContext Lifecycle
// ============================================================================

void block_context_init(BlockContext* ctx, ViewBlock* element, Pool* pool) {
    // Initialize layout state
    ctx->content_width = 0;
    ctx->content_height = 0;
    ctx->advance_y = 0;
    ctx->max_width = 0;
    ctx->max_height = 0;
    ctx->line_height = 0;
    ctx->init_ascender = 0;
    ctx->init_descender = 0;
    ctx->lead_y = 0;
    ctx->text_align = CSS_VALUE_LEFT;
    ctx->given_width = -1;
    ctx->given_height = -1;

    // Initialize BFC hierarchy
    ctx->parent = nullptr;
    ctx->establishing_element = element;
    ctx->is_bfc_root = (element != nullptr);

    // BFC offset is 0 for BFC root (coordinates are relative to itself)
    ctx->bfc_offset_x = 0;
    ctx->bfc_offset_y = 0;

    // Calculate origin from element's content area
    if (element) {
        ctx->origin_x = element->x;
        ctx->origin_y = element->y;

        if (element->bound) {
            if (element->bound->border) {
                ctx->origin_x += element->bound->border->width.left;
                ctx->origin_y += element->bound->border->width.top;
            }
            ctx->origin_x += element->bound->padding.left;
            ctx->origin_y += element->bound->padding.top;
        }

        // Content area bounds for float calculations
        // content_width is already the pure content area (width - padding - border)
        // Only subtract padding/border if we're using element->width directly
        ctx->float_left_edge = 0;
        if (element->content_width > 0) {
            ctx->float_right_edge = element->content_width;
        } else {
            ctx->float_right_edge = element->width;
            if (element->bound) {
                ctx->float_right_edge -= element->bound->padding.left + element->bound->padding.right;
                if (element->bound->border) {
                    ctx->float_right_edge -= element->bound->border->width.left + element->bound->border->width.right;
                }
            }
        }
        if (ctx->float_right_edge < 0) ctx->float_right_edge = element->width;
    } else {
        ctx->origin_x = 0;
        ctx->origin_y = 0;
        ctx->float_left_edge = 0;
        ctx->float_right_edge = 0;
    }

    // Initialize float lists
    ctx->left_floats = nullptr;
    ctx->left_floats_tail = nullptr;
    ctx->right_floats = nullptr;
    ctx->right_floats_tail = nullptr;
    ctx->left_float_count = 0;
    ctx->right_float_count = 0;
    ctx->lowest_float_bottom = 0;

    ctx->pool = pool;

    log_debug("[BlockContext] Init: element=%s, origin=(%.1f,%.1f), float_right=%.1f",
              element ? element->node_name() : "null", ctx->origin_x, ctx->origin_y, ctx->float_right_edge);
}

void block_context_reset_floats(BlockContext* ctx) {
    ctx->left_floats = nullptr;
    ctx->left_floats_tail = nullptr;
    ctx->right_floats = nullptr;
    ctx->right_floats_tail = nullptr;
    ctx->left_float_count = 0;
    ctx->right_float_count = 0;
    ctx->lowest_float_bottom = 0;
}

// ============================================================================
// BFC Detection
// ============================================================================

bool block_context_establishes_bfc(ViewBlock* block) {
    // CSS 2.2 Section 9.4.1 - Block formatting contexts

    if (!block) return false;

    // 1. Root element (html) - no parent
    if (!block->parent) return true;

    // 2. Floats (float == left or right)
    // Note: Check for explicit float values, not just != CSS_VALUE_NONE,
    // because uninitialized float_prop is CSS_VALUE__UNDEF (0) which is != CSS_VALUE_NONE
    if (block->position &&
        (block->position->float_prop == CSS_VALUE_LEFT ||
         block->position->float_prop == CSS_VALUE_RIGHT)) {
        return true;
    }

    // 3. Absolutely positioned elements (position: absolute/fixed)
    if (block->position &&
        (block->position->position == CSS_VALUE_ABSOLUTE ||
         block->position->position == CSS_VALUE_FIXED)) {
        return true;
    }

    // 4. Inline-blocks
    if (block->display.outer == CSS_VALUE_INLINE_BLOCK) {
        return true;
    }

    // 5. Tables, table cells and captions
    // CSS 2.2 §9.4.1: "The border box of a table..."
    if (block->display.inner == CSS_VALUE_TABLE ||
        block->display.inner == CSS_VALUE_TABLE_CELL ||
        block->display.inner == CSS_VALUE_TABLE_CAPTION) {
        return true;
    }

    // 6. Overflow != visible (creates BFC)
    if (block->scroller &&
        (block->scroller->overflow_x != CSS_VALUE_VISIBLE ||
         block->scroller->overflow_y != CSS_VALUE_VISIBLE)) {
        return true;
    }

    // 7. Display: flow-root explicitly establishes BFC
    if (block->display.inner == CSS_VALUE_FLOW_ROOT) {
        return true;
    }

    // 8. Flex and Grid containers establish BFC for their children
    if (block->display.inner == CSS_VALUE_FLEX ||
        block->display.inner == CSS_VALUE_GRID) {
        return true;
    }

    return false;
}

BlockContext* block_context_find_bfc(BlockContext* ctx) {
    if (!ctx) return nullptr;

    // Walk up the parent chain to find the nearest BFC root
    BlockContext* current = ctx;
    while (current) {
        if (current->is_bfc_root) {
            return current;
        }
        current = current->parent;
    }

    // If no explicit BFC root found, return the context itself
    // (it may be the implicit root)
    return ctx;
}

void block_context_calc_bfc_offset(ViewElement* view, BlockContext* bfc, float* offset_x, float* offset_y) {
    if (!view || !bfc || !offset_x || !offset_y) {
        if (offset_x) *offset_x = 0;
        if (offset_y) *offset_y = 0;
        return;
    }

    ViewBlock* bfc_elem = bfc->establishing_element;
    float ox = 0, oy = 0;

    // Walk up from view to BFC establishing element
    // Simply accumulate x/y positions since view->x is already relative to parent's content area
    // (border and padding are NOT added because child x/y already accounts for parent's content area)
    ViewElement* walker = view;

    while (walker && walker != bfc_elem) {
        ox += walker->x;
        oy += walker->y;
        walker = walker->parent_view();
    }

    *offset_x = ox;
    *offset_y = oy;
}

// ============================================================================
// Float Allocation
// ============================================================================

FloatBox* block_context_alloc_float_box(BlockContext* ctx) {
    if (ctx->pool) {
        return (FloatBox*)pool_calloc(ctx->pool, sizeof(FloatBox));
    }
    return (FloatBox*)mem_calloc(1, sizeof(FloatBox), MEM_CAT_LAYOUT);
}

// ============================================================================
// Float Management
// ============================================================================

void block_context_add_float(BlockContext* ctx, ViewBlock* float_elem) {
    if (!float_elem || !float_elem->position) return;

    CssEnum side = float_elem->position->float_prop;
    if (side != CSS_VALUE_LEFT && side != CSS_VALUE_RIGHT) return;

    FloatBox* box = block_context_alloc_float_box(ctx);
    if (!box) return;

    box->element = float_elem;
    box->float_side = side;
    box->next = nullptr;

    // Get margins
    float margin_l = float_elem->bound ? float_elem->bound->margin.left : 0;
    float margin_r = float_elem->bound ? float_elem->bound->margin.right : 0;
    float margin_t = float_elem->bound ? float_elem->bound->margin.top : 0;
    float margin_b = float_elem->bound ? float_elem->bound->margin.bottom : 0;

    // Store border box (parent-relative coords)
    box->x = float_elem->x;
    box->y = float_elem->y;
    box->width = float_elem->width;
    box->height = float_elem->height;

    // Convert float position to BFC-relative coordinates
    // float_elem->x/y are relative to parent, need to accumulate parent positions
    float bfc_x = float_elem->x;
    float bfc_y = float_elem->y;

    // Walk up parent chain to accumulate positions relative to BFC
    // Stop at the BFC establishing element
    ViewElement* parent = float_elem->parent_view();
    while (parent && parent != ctx->establishing_element) {
        bfc_x += parent->x;
        bfc_y += parent->y;
        parent = parent->parent_view();
    }

    // Calculate margin box relative to BFC content area origin
    box->margin_box_left = bfc_x - margin_l - ctx->origin_x;
    box->margin_box_top = bfc_y - margin_t - ctx->origin_y;
    box->margin_box_right = bfc_x + float_elem->width + margin_r - ctx->origin_x;
    box->margin_box_bottom = bfc_y + float_elem->height + margin_b - ctx->origin_y;

    log_debug("[BlockContext] Add float: bfc_pos=(%.1f,%.1f), margin_box=(%.1f,%.1f,%.1f,%.1f)",
              bfc_x, bfc_y, box->margin_box_left, box->margin_box_top,
              box->margin_box_right, box->margin_box_bottom);

    // Add to appropriate list
    if (side == CSS_VALUE_LEFT) {
        if (!ctx->left_floats) {
            ctx->left_floats = ctx->left_floats_tail = box;
        } else {
            ctx->left_floats_tail->next = box;
            ctx->left_floats_tail = box;
        }
        ctx->left_float_count++;
        log_debug("[BlockContext] Added left float: count=%d, margin_bottom=%.1f",
                  ctx->left_float_count, box->margin_box_bottom);
    } else {
        if (!ctx->right_floats) {
            ctx->right_floats = ctx->right_floats_tail = box;
        } else {
            ctx->right_floats_tail->next = box;
            ctx->right_floats_tail = box;
        }
        ctx->right_float_count++;
        log_debug("[BlockContext] Added right float: count=%d, margin_bottom=%.1f",
                  ctx->right_float_count, box->margin_box_bottom);
    }

    // Update lowest float bottom
    if (box->margin_box_bottom > ctx->lowest_float_bottom) {
        ctx->lowest_float_bottom = box->margin_box_bottom;
    }
}

// ============================================================================
// Float Space Queries
// ============================================================================

FloatAvailableSpace block_context_space_at_y(BlockContext* ctx, float y, float height) {
    FloatAvailableSpace space;
    space.left = ctx->float_left_edge;
    space.right = ctx->float_right_edge;
    space.has_left_float = false;
    space.has_right_float = false;

    float y_top = y;
    float y_bottom = y + height;

    // Early exit if no floats or below all floats
    if (ctx->left_float_count == 0 && ctx->right_float_count == 0) {
        return space;
    }
    if (y_top >= ctx->lowest_float_bottom) {
        return space;
    }

    // Check left floats - find rightmost intrusion
    for (FloatBox* fb = ctx->left_floats; fb; fb = fb->next) {
        if (float_intersects_y_range(fb, y_top, y_bottom)) {
            if (fb->margin_box_right > space.left) {
                space.left = fb->margin_box_right;
                space.has_left_float = true;
            }
        }
    }

    // Check right floats - find leftmost intrusion
    for (FloatBox* fb = ctx->right_floats; fb; fb = fb->next) {
        if (float_intersects_y_range(fb, y_top, y_bottom)) {
            if (fb->margin_box_left < space.right) {
                space.right = fb->margin_box_left;
                space.has_right_float = true;
            }
        }
    }

    // Ensure valid space (right >= left)
    if (space.right < space.left) {
        space.right = space.left;
    }

    log_debug("[BlockContext] space_at_y(%.1f, h=%.1f): left=%.1f, right=%.1f, width=%.1f",
              y, height, space.left, space.right, space.right - space.left);

    return space;
}

float block_context_find_y_for_width(BlockContext* ctx, float required_width, float min_y) {
    if (ctx->left_float_count == 0 && ctx->right_float_count == 0) {
        return min_y;
    }

    float y = min_y;
    int max_iterations = 100;

    while (max_iterations-- > 0) {
        FloatAvailableSpace space = block_context_space_at_y(ctx, y, 1.0f);
        float available_width = space.right - space.left;
        if (available_width >= required_width) {
            return y;
        }

        // Find next float bottom to step to
        float next_y = FLT_MAX;
        for (FloatBox* fb = ctx->left_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > y && fb->margin_box_bottom < next_y) {
                next_y = fb->margin_box_bottom;
            }
        }
        for (FloatBox* fb = ctx->right_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > y && fb->margin_box_bottom < next_y) {
                next_y = fb->margin_box_bottom;
            }
        }

        if (next_y <= y || std::isinf(next_y) || next_y == FLT_MAX) {
            break;
        }
        y = next_y;
    }

    return y;
}

float block_context_clear_y(BlockContext* ctx, CssEnum clear_type) {
    float clear_y = 0;

    if (clear_type == CSS_VALUE_LEFT || clear_type == CSS_VALUE_BOTH) {
        for (FloatBox* fb = ctx->left_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > clear_y) {
                clear_y = fb->margin_box_bottom;
            }
        }
    }

    if (clear_type == CSS_VALUE_RIGHT || clear_type == CSS_VALUE_BOTH) {
        for (FloatBox* fb = ctx->right_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > clear_y) {
                clear_y = fb->margin_box_bottom;
            }
        }
    }

    log_debug("[BlockContext] clear_y(%d): %.1f", clear_type, clear_y);
    return clear_y;
}

// ============================================================================
// Float Positioning
// ============================================================================

void block_context_position_float(BlockContext* ctx, ViewBlock* element, float current_y) {
    if (!element || !element->position) return;

    CssEnum side = element->position->float_prop;
    if (side != CSS_VALUE_LEFT && side != CSS_VALUE_RIGHT) return;

    // Get element dimensions including margins
    float margin_l = element->bound ? element->bound->margin.left : 0;
    float margin_r = element->bound ? element->bound->margin.right : 0;
    float margin_t = element->bound ? element->bound->margin.top : 0;
    float total_width = element->width + margin_l + margin_r;

    // CSS 2.2 Rules 4, 5, 6: Float top >= max of various positions
    float min_y = fmax(0, current_y);

    // Find Y where float fits horizontally
    float y = block_context_find_y_for_width(ctx, total_width, min_y);

    // Get available space at that Y
    FloatAvailableSpace space = block_context_space_at_y(ctx, y, element->height);

    // Position the float
    if (side == CSS_VALUE_LEFT) {
        element->x = ctx->origin_x + space.left + margin_l;
    } else {
        element->x = ctx->origin_x + space.right - element->width - margin_r;
    }
    element->y = ctx->origin_y + y + margin_t;

    log_debug("[BlockContext] Positioned float %s: side=%d, pos=(%.1f,%.1f), size=(%.1f,%.1f)",
              element->node_name(), side, element->x, element->y, element->width, element->height);

    // Add to float tracking
    block_context_add_float(ctx, element);
}
