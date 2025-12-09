#include "layout.hpp"
#include "layout_positioned.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/css_style.hpp"
#include <stdlib.h>

// ============================================================================
// Legacy FloatContext (used internally, will be migrated to BlockContext)
// ============================================================================

/**
 * FloatSideData - Container for floats on one side (left or right)
 * Uses a linked list to support multiple floats per side.
 */
typedef struct FloatSideData {
    FloatBox* head;             // Head of linked list
    FloatBox* tail;             // Tail for O(1) append
    int count;                  // Number of floats on this side
} FloatSideData;

/**
 * FloatContext - Legacy context for float layout within a block formatting context
 * NOTE: Being migrated to unified BlockContext in layout.hpp
 */
typedef struct FloatContext {
    FloatSideData left;         // Left floats
    FloatSideData right;        // Right floats

    // Container content area bounds (coordinates relative to container)
    float content_left;
    float content_right;
    float content_top;
    float content_bottom;

    ViewBlock* container;       // Containing block establishing this context
} FloatContext;

// Forward declarations
ViewBlock* find_containing_block(ViewBlock* element, CssEnum position_type);
float adjust_min_max_width(ViewBlock* block, float width);
float adjust_min_max_height(ViewBlock* block, float height);
float adjust_border_padding_width(ViewBlock* block, float width);
float adjust_border_padding_height(ViewBlock* block, float height);
void layout_block_inner_content(LayoutContext* lycon, ViewBlock* block);
void setup_inline(LayoutContext* lycon, ViewBlock* block);

/**
 * Apply relative positioning to an element
 * Relative positioning moves the element from its normal position without affecting other elements
 */
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block) {
    log_debug("Applying relative positioning to element");
    // calculate offset from top/right/bottom/left properties
    int offset_x = 0, offset_y = 0;

    // Get parent's text direction to determine horizontal offset precedence
    // The CSS 'direction' property determines which value wins when both left and right are specified
    TextDirection parent_direction = TD_LTR;  // default to LTR
    ViewElement* parent = block->parent_view();
    if (parent && parent->is_element()) {
        DomElement* parent_elem = (DomElement*)parent;
        if (parent_elem->specified_style) {
            // Query the computed 'direction' property from parent
            CssValue* direction_value = (CssValue*)style_tree_get_computed_value(
                parent_elem->specified_style,
                CSS_PROPERTY_DIRECTION,
                parent_elem->parent && parent_elem->parent->is_element() ?
                    ((DomElement*)parent_elem->parent)->specified_style : NULL
            );

            if (direction_value && direction_value->type == CSS_VALUE_TYPE_KEYWORD &&
                direction_value->data.keyword == CSS_VALUE_RTL) {
                parent_direction = TD_RTL;
                log_debug("Parent has direction: rtl");
            }
        }
    }

    // horizontal offset: precedence depends on containing block's direction
    // CSS spec: If both left and right are not 'auto':
    // - If direction is 'ltr', left wins and right is ignored
    // - If direction is 'rtl', right wins and left is ignored (but equal values cancel in RTL)
    bool both_horizontal = block->position->has_left && block->position->has_right;

    if (both_horizontal) {
        if (parent_direction == TD_RTL) {
            // RTL: right takes precedence, but equal values cancel out
            if (block->position->left == block->position->right) {
                // In RTL with equal left/right values, they geometrically cancel
                offset_x = 0;
                log_debug("Over-constrained relative positioning (RTL): left=%d equals right=%d, offset=0",
                         block->position->left, block->position->right);
            } else {
                // RTL with different values: right wins
                offset_x = -block->position->right;
                log_debug("Over-constrained relative positioning (RTL): right=%d wins, left=%d ignored",
                         block->position->right, block->position->left);
            }
        } else {
            // LTR: left takes precedence (always, even if equal to right)
            offset_x = block->position->left;
            log_debug("Over-constrained relative positioning (LTR): left=%d wins, right=%d ignored",
                     block->position->left, block->position->right);
        }
    } else if (block->position->has_left) {
        offset_x = block->position->left;
    } else if (block->position->has_right) {
        offset_x = -block->position->right;
    }    // vertical offset: top takes precedence over bottom
    if (block->position->has_top) {
        offset_y = block->position->top;
    } else if (block->position->has_bottom) {
        offset_y = -block->position->bottom;
    }
    log_debug("Calculated relative offset: x=%d, y=%d (parent direction=%s)",
             offset_x, offset_y, parent_direction == TD_RTL ? "RTL" : "LTR");

    // apply offset to visual position (doesn't affect layout of other elements)
    block->x += offset_x;  block->y += offset_y;
    log_debug("Applied relative positioning: offset (%d, %d), final position (%d, %d)",
              offset_x, offset_y, block->x, block->y);

    // todo: add to chain of positioned elements for z-index stacking
    // find containing block; add to its positioned children list;
}/**
 * Find the containing block for a positioned element
 * For relative/static: nearest block container ancestor
 * For absolute: nearest positioned ancestor or initial containing block
 * For fixed: viewport (initial containing block)
 */
ViewBlock* find_containing_block(ViewBlock* element, CssEnum position_type) {
    if (position_type == CSS_VALUE_FIXED) {
        // Fixed positioning uses viewport as containing block
        // For now, return the root block (will be enhanced for viewport support)
        ViewBlock* root = element;
        while (root->parent) {
            root = (ViewBlock*)root->parent;
        }
        return root;
    }

    if (position_type == CSS_VALUE_ABSOLUTE) {
        // Find nearest positioned ancestor
        ViewElement* ancestor = element->parent_view();
        while (ancestor) {
            if (ancestor->is_block()) {
                ViewBlock* ancestor_block = (ViewBlock*)ancestor;
                // Check if ancestor is positioned
                if (ancestor_block->position && ancestor_block->position->position != CSS_VALUE_STATIC) {
                    return ancestor_block;
                }
            }
            ancestor = ancestor->parent_view();
        }

        // No positioned ancestor found, use initial containing block (root)
        ViewBlock* root = element;
        while (root->parent_view()) {
            root = (ViewBlock*)root->parent_view();
        }
        return root;
    }

    // For relative positioning, use nearest block container
    ViewElement* ancestor = element->parent_view();
    while (ancestor) {
        if (ancestor->is_block()) {
            return (ViewBlock*)ancestor;
        }
        ancestor = ancestor->parent_view();
    }

    return nullptr;
}

// calculate absolute position based on containing block and offset properties
void calculate_absolute_position(LayoutContext* lycon, ViewBlock* block, ViewBlock* containing_block) {
    // 1. top, right, bottom, left resolved relative to the padding box of the containing block.
    // 2. margin values do offset the absolutely positioned box from where top/left/right/bottom place it. but 'auto' margins are treated as 0.

    // get containing block dimensions
    float cb_x = containing_block->x, cb_y = containing_block->y;
    float cb_width = containing_block->width, cb_height = containing_block->height;

    // Calculate border offset - the absolute element is positioned relative to padding box,
    // but block->x/y are stored relative to containing block's origin (border box)
    float border_offset_x = 0, border_offset_y = 0;

    // update to padding box dimensions
    if (containing_block->bound) {
        if (containing_block->bound->border) {
            border_offset_x = containing_block->bound->border->width.left;
            border_offset_y = containing_block->bound->border->width.top;
            cb_x += border_offset_x;
            cb_y += border_offset_y;
            cb_width -= (containing_block->bound->border->width.left + containing_block->bound->border->width.right);
            cb_height -= (containing_block->bound->border->width.top + containing_block->bound->border->width.bottom);
        }
    }
    log_debug("containing block padding box: (%d, %d) size (%d, %d), border_offset: (%f, %f)",
              (int)cb_x, (int)cb_y, (int)cb_width, (int)cb_height, border_offset_x, border_offset_y);

    float content_width, content_height;
    // calculate horizontal position
    log_debug("given_width=%f, given_height=%f", lycon->block.given_width, lycon->block.given_height);

    // First determine content_width: use CSS width if specified, otherwise calculate from constraints
    if (lycon->block.given_width >= 0) {
        content_width = lycon->block.given_width;
    } else if (block->position->has_left && block->position->has_right) {
        // both left and right specified - calculate width from constraints
        float left_edge = block->position->left + (block->bound ? block->bound->margin.left : 0);
        float right_edge = cb_width - block->position->right - (block->bound ? block->bound->margin.right : 0);
        content_width = max(right_edge - left_edge, 0.0f);
    } else {
        // shrink-to-fit: will be determined by content (start with 0, let content grow it)
        // For now, fall back to containing block width minus margins
        content_width = max(cb_width - (block->bound ? block->bound->margin.right + block->bound->margin.left : 0), 0.0f);
    }

    // Now determine x position (relative to padding box, then add border offset)
    if (block->position->has_left) {
        block->x = border_offset_x + block->position->left + (block->bound ? block->bound->margin.left : 0);
    } else if (block->position->has_right) {
        block->x = border_offset_x + cb_width - block->position->right - (block->bound ? block->bound->margin.right : 0) - content_width;
    } else {
        // neither left nor right specified - use static position (with margin offset)
        block->x = border_offset_x + (block->bound ? block->bound->margin.left : 0);
    }
    assert(content_width >= 0);

    // calculate vertical position - same refactoring as horizontal
    if (lycon->block.given_height >= 0) {
        content_height = lycon->block.given_height;
    } else if (block->position->has_top && block->position->has_bottom) {
        // both top and bottom specified - calculate height from constraints
        float top_edge = block->position->top + (block->bound ? block->bound->margin.top : 0);
        float bottom_edge = cb_height - block->position->bottom - (block->bound ? block->bound->margin.bottom : 0);
        content_height = max(bottom_edge - top_edge, 0.0f);
    } else {
        // shrink-to-fit: will be determined by content
        // For now, fall back to containing block height minus margins
        content_height = max(cb_height - (block->bound ? block->bound->margin.bottom + block->bound->margin.top : 0), 0.0f);
    }

    // Now determine y position (relative to padding box, then add border offset)
    if (block->position->has_top) {
        block->y = border_offset_y + block->position->top + (block->bound ? block->bound->margin.top : 0);
    } else if (block->position->has_bottom) {
        block->y = border_offset_y + cb_height - block->position->bottom - (block->bound ? block->bound->margin.bottom : 0) - content_height;
    } else {
        // neither top nor bottom specified - use static position (with margin offset)
        block->y = border_offset_y + (block->bound ? block->bound->margin.top : 0);
    }
    assert(content_height >= 0);

    if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
        content_width = adjust_min_max_width(block, content_width);
        if (block->bound) content_width = adjust_border_padding_width(block, content_width);
        content_height = adjust_min_max_height(block, content_height);
        if (block->bound) content_height = adjust_border_padding_height(block, content_height);
    } else {
        content_width = adjust_border_padding_width(block, content_width);
        if (block->bound) content_width = adjust_min_max_width(block, content_width);
        content_height = adjust_border_padding_height(block, content_height);
        if (block->bound) content_height = adjust_min_max_height(block, content_height);
    }
    lycon->block.content_width = content_width;  lycon->block.content_height = content_height;

    if (block->bound) {
        block->width = content_width + block->bound->padding.left + block->bound->padding.right +
            (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0);
        block->height = content_height + block->bound->padding.top + block->bound->padding.bottom +
            (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0);
    }
    else {
        // no change to block->x, block->y, lycon->line.advance_x, lycon->block.advance_y
        block->width = content_width;  block->height = content_height;
    }
    log_debug("calculated x,y,wd,hg, content_width, content_height: (%f, %f) size (%f, %f), content (%f, %f) within containing block (%f, %f) size (%f, %f)",
        block->x, block->y, block->width, block->height, lycon->block.content_width, lycon->block.content_height, cb_x, cb_y, cb_width, cb_height);
}

void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, Blockbox *pa_block, Linebox *pa_line) {
    log_debug("layout_abs_block");  log_enter();
    log_debug("block init position (%s): x=%f, y=%f, pa_block.advance_y=%f", elmt->node_name(), block->x, block->y, pa_block->advance_y);

    // find containing block
    ViewBlock* cb = find_containing_block(block, block->position->position);
    if (!cb) { log_error("Missing containing block");  return; }
    log_debug("found containing block: %p, width=%d, height=%d, content_width=%d, content_height=%d",
        cb, cb->width, cb->height, cb->content_width, cb->content_height);
    // link to containing block's float context
    if (cb->position) {
        if (!cb->position->first_abs_child) {
            cb->position->last_abs_child = cb->position->first_abs_child = block;
        } else {
            cb->position->last_abs_child->position->next_abs_sibling = block;
            cb->position->last_abs_child = block;
        }
    } else {
        log_error("Containing block has no position property");
    }

    // calculate position based on offset properties and containing block
    calculate_absolute_position(lycon, block, cb);

    // setup inline context
    setup_inline(lycon, block);

    // layout block content, and determine flow width and height
    layout_block_inner_content(lycon, block);

    // no relative positioning adjustment here
    // no margin collapsing with children

    // Apply CSS float layout after positioning
    if (block->position && element_has_float(block)) {
        log_debug("Element has float property, applying float layout");
        layout_float_element(lycon, block);
    }

    // Apply CSS clear property after float layout
    if (block->position && block->position->clear != CSS_VALUE_NONE) {
        log_debug("Element has clear property, applying clear layout");
        layout_clear_element(lycon, block);
    }

    // adjust block width and height based on content
    log_debug("block position: x=%f, y=%f, width=%f, height=%f",
        block->x, block->y, block->width,  block->height);
    if (!(block->position->has_left || block->position->has_right || lycon->block.given_width >= 0)) {
        float flow_width = lycon->block.max_width;
        block->width = flow_width + (block->bound ? block->bound->padding.left + block->bound->padding.right +
            (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0) : 0);
    }
    if (!(block->position->has_top || block->position->has_bottom || lycon->block.given_height >= 0)) {
        float flow_height = lycon->block.advance_y;
        block->height = flow_height + (block->bound ? block->bound->padding.top + block->bound->padding.bottom +
            (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0) : 0);
    }
    log_debug("final block position: x=%f, y=%f, width=%f, height=%f",
        block->x, block->y, block->width,  block->height);
    log_leave();
}

/**
 * Check if an element has positioning properties that require special handling
 */
bool element_has_positioning(ViewBlock* block) {
    return block->position &&
           (block->position->position == CSS_VALUE_RELATIVE ||
            block->position->position == CSS_VALUE_ABSOLUTE ||
            block->position->position == CSS_VALUE_FIXED);
}

/**
 * Check if an element has float properties
 * Per CSS 2.1 section 9.7: float is ignored for absolutely positioned elements
 * (position: absolute or position: fixed)
 */
bool element_has_float(ViewBlock* block) {
    if (!block || !block->position) return false;
    // Float is ignored for absolutely positioned or fixed elements
    if (block->position->position == CSS_VALUE_ABSOLUTE ||
        block->position->position == CSS_VALUE_FIXED) {
        return false;
    }
    return (block->position->float_prop == CSS_VALUE_LEFT ||
            block->position->float_prop == CSS_VALUE_RIGHT);
}

// ============================================================================
// Enhanced Float Layout Implementation
// ============================================================================

/**
 * Check if a float's margin box intersects with a vertical range
 */
static bool float_intersects_y_range(FloatBox* box, float y_top, float y_bottom) {
    return !(box->margin_box_bottom <= y_top || box->margin_box_top >= y_bottom);
}

/**
 * Create a new float context for a containing block
 */
FloatContext* float_context_create(ViewBlock* container) {
    if (!container) {
        log_error("float_context_create called with NULL container");
        return NULL;
    }

    FloatContext* ctx = (FloatContext*)calloc(1, sizeof(FloatContext));
    if (!ctx) {
        log_error("Failed to allocate FloatContext");
        return NULL;
    }

    // Initialize float lists
    ctx->left.head = ctx->left.tail = NULL;
    ctx->left.count = 0;
    ctx->right.head = ctx->right.tail = NULL;
    ctx->right.count = 0;

    // Calculate content area bounds
    ctx->content_left = 0;
    ctx->content_top = 0;
    ctx->content_right = container->width;
    ctx->content_bottom = container->height;

    // Adjust for border and padding
    if (container->bound) {
        if (container->bound->border) {
            ctx->content_left += container->bound->border->width.left;
            ctx->content_top += container->bound->border->width.top;
            ctx->content_right -= container->bound->border->width.right;
            ctx->content_bottom -= container->bound->border->width.bottom;
        }
        ctx->content_left += container->bound->padding.left;
        ctx->content_top += container->bound->padding.top;
        ctx->content_right -= container->bound->padding.right;
        ctx->content_bottom -= container->bound->padding.bottom;
    }

    ctx->container = container;

    log_debug("Created float context: content area (%.1f, %.1f) to (%.1f, %.1f)",
              ctx->content_left, ctx->content_top, ctx->content_right, ctx->content_bottom);

    return ctx;
}

/**
 * Destroy a float context and free all associated memory
 */
void float_context_destroy(FloatContext* ctx) {
    if (!ctx) return;

    // Free left floats
    FloatBox* box = ctx->left.head;
    while (box) {
        FloatBox* next = box->next;
        free(box);
        box = next;
    }

    // Free right floats
    box = ctx->right.head;
    while (box) {
        FloatBox* next = box->next;
        free(box);
        box = next;
    }

    free(ctx);
}

/**
 * Add a float element to the context (after it has been positioned)
 */
void float_context_add_float(FloatContext* ctx, ViewBlock* element) {
    if (!ctx || !element || !element->position) return;

    FloatBox* box = (FloatBox*)calloc(1, sizeof(FloatBox));
    if (!box) {
        log_error("Failed to allocate FloatBox");
        return;
    }

    box->element = element;
    box->x = element->x;
    box->y = element->y;
    box->width = element->width;
    box->height = element->height;
    box->float_side = element->position->float_prop;
    box->next = NULL;

    // Calculate margin box bounds (relative to container's content area)
    // Note: element->x and element->y are already relative to the container's content area
    // We just need to account for margins to get the margin box
    float margin_left = element->bound ? element->bound->margin.left : 0;
    float margin_right = element->bound ? element->bound->margin.right : 0;
    float margin_top = element->bound ? element->bound->margin.top : 0;
    float margin_bottom = element->bound ? element->bound->margin.bottom : 0;

    // element->y already includes margin.top (added in layout_block_content)
    // For margin box, we need to go back to before margin was added
    box->margin_box_left = element->x - margin_left;
    box->margin_box_top = element->y - margin_top;
    box->margin_box_right = element->x + element->width + margin_right;
    box->margin_box_bottom = element->y + element->height + margin_bottom;

    log_debug("Adding float: side=%d, margin_box=(%.1f, %.1f, %.1f, %.1f)",
              box->float_side, box->margin_box_left, box->margin_box_top,
              box->margin_box_right, box->margin_box_bottom);

    // Add to appropriate list
    FloatSideData* side = (box->float_side == CSS_VALUE_LEFT) ? &ctx->left : &ctx->right;

    if (!side->head) {
        side->head = side->tail = box;
    } else {
        side->tail->next = box;
        side->tail = box;
    }
    side->count++;

    log_debug("Float added to %s side, count now: %d",
              box->float_side == CSS_VALUE_LEFT ? "left" : "right", side->count);
}

/**
 * Get available horizontal space at a given Y coordinate
 * Returns the left and right boundaries of available space.
 */
FloatAvailableSpace float_space_at_y(FloatContext* ctx, float y, float line_height) {
    FloatAvailableSpace space;
    space.left = ctx->content_left;
    space.right = ctx->content_right;
    space.has_left_float = false;
    space.has_right_float = false;

    if (!ctx) return space;

    float y_top = y;
    float y_bottom = y + line_height;

    // Check left floats - find the rightmost intrusion
    for (FloatBox* box = ctx->left.head; box; box = box->next) {
        if (float_intersects_y_range(box, y_top, y_bottom)) {
            // Left float intrudes from the left
            if (box->margin_box_right > space.left) {
                space.left = box->margin_box_right;
                space.has_left_float = true;
            }
        }
    }

    // Check right floats - find the leftmost intrusion
    for (FloatBox* box = ctx->right.head; box; box = box->next) {
        if (float_intersects_y_range(box, y_top, y_bottom)) {
            // Right float intrudes from the right
            if (box->margin_box_left < space.right) {
                space.right = box->margin_box_left;
                space.has_right_float = true;
            }
        }
    }

    log_debug("Space at y=%.1f (height=%.1f): left=%.1f, right=%.1f, width=%.1f, left_float=%d, right_float=%d",
              y, line_height, space.left, space.right, space.right - space.left,
              space.has_left_float, space.has_right_float);

    return space;
}

/**
 * Find Y position where the specified width is available
 * Used for placing floats that don't fit at the current Y.
 */
float float_find_y_for_width(FloatContext* ctx, float required_width, float start_y) {
    if (!ctx) return start_y;

    float y = start_y;
    int max_iterations = 1000;  // Safety limit

    while (max_iterations-- > 0) {
        FloatAvailableSpace space = float_space_at_y(ctx, y, 1.0f);
        float available = space.right - space.left;

        if (available >= required_width) {
            return y;
        }

        // Find next Y position where a float ends
        float next_y = INFINITY;

        for (FloatBox* box = ctx->left.head; box; box = box->next) {
            if (box->margin_box_bottom > y && box->margin_box_bottom < next_y) {
                next_y = box->margin_box_bottom;
            }
        }
        for (FloatBox* box = ctx->right.head; box; box = box->next) {
            if (box->margin_box_bottom > y && box->margin_box_bottom < next_y) {
                next_y = box->margin_box_bottom;
            }
        }

        if (isinf(next_y)) break;
        y = next_y;
    }

    return y;
}

/**
 * Find clear position - Y coordinate below all floats of specified type
 */
float float_find_clear_position(FloatContext* ctx, CssEnum clear_value) {
    if (!ctx) return 0;

    float clear_y = ctx->content_top;

    if (clear_value == CSS_VALUE_LEFT || clear_value == CSS_VALUE_BOTH) {
        for (FloatBox* box = ctx->left.head; box; box = box->next) {
            if (box->margin_box_bottom > clear_y) {
                clear_y = box->margin_box_bottom;
            }
        }
    }

    if (clear_value == CSS_VALUE_RIGHT || clear_value == CSS_VALUE_BOTH) {
        for (FloatBox* box = ctx->right.head; box; box = box->next) {
            if (box->margin_box_bottom > clear_y) {
                clear_y = box->margin_box_bottom;
            }
        }
    }

    log_debug("Clear position for clear=%d: %.1f", clear_value, clear_y);
    return clear_y;
}

/**
 * Position a float element within the context
 * Implements CSS 2.2 Section 9.5.1 Float Positioning Rules
 */
void float_context_position_float(FloatContext* ctx, ViewBlock* element, float current_y) {
    if (!ctx || !element || !element->position) return;

    CssEnum float_side = element->position->float_prop;

    // Get element's margin dimensions
    float margin_left = element->bound ? element->bound->margin.left : 0;
    float margin_right = element->bound ? element->bound->margin.right : 0;
    float margin_top = element->bound ? element->bound->margin.top : 0;

    float total_width = element->width + margin_left + margin_right;
    float total_height = element->height + margin_top +
                        (element->bound ? element->bound->margin.bottom : 0);

    // Rule: Float's top may not be higher than current line position
    // Also, float's top may not be higher than any earlier float's top
    float min_y = current_y;

    // Check existing floats to ensure proper stacking
    for (FloatBox* box = ctx->left.head; box; box = box->next) {
        if (box->margin_box_top > min_y) {
            // Earlier float is lower, we need to be at least as high (can be same)
        }
    }
    for (FloatBox* box = ctx->right.head; box; box = box->next) {
        if (box->margin_box_top > min_y) {
            // Earlier float is lower
        }
    }

    // Find Y position where the float fits
    float y = float_find_y_for_width(ctx, total_width, min_y);

    // Get available space at that Y
    FloatAvailableSpace space = float_space_at_y(ctx, y, total_height);

    // Position horizontally based on float side
    float x;
    float container_x = ctx->container ? ctx->container->x : 0;
    float container_y = ctx->container ? ctx->container->y : 0;

    if (float_side == CSS_VALUE_LEFT) {
        // Left float: position at left edge of available space + margin
        x = container_x + space.left + margin_left;
    } else {
        // Right float: position at right edge of available space - width - margin
        x = container_x + space.right - element->width - margin_right;
    }

    // Set element position (adding container offset and top margin)
    element->x = x;
    element->y = container_y + y + margin_top;

    log_debug("Positioned %s float at (%.1f, %.1f) size (%.1f, %.1f)",
              float_side == CSS_VALUE_LEFT ? "left" : "right",
              element->x, element->y, element->width, element->height);

    // Add to float context for future space calculations
    float_context_add_float(ctx, element);
}

// ============================================================================
// Layout Integration Functions
// ============================================================================

/**
 * Apply float layout to an element
 * Note: The block has already been laid out at its normal flow position by layout_block_content.
 * For floats, we need to:
 * 1. Reposition float:right elements to the right edge of their containing block
 * 2. Add the float to the float context for tracking
 * 3. Adjust surrounding content (handled by adjust_line_for_floats and clear)
 *
 * CSS 2.2 Section 9.5.1: Float Positioning Rules
 * - float:left positions at the left edge of available space (after earlier floats)
 * - float:right positions at the right edge of available space (before earlier floats)
 */
void layout_float_element(LayoutContext* lycon, ViewBlock* block) {
    if (!element_has_float(block)) {
        return;
    }

    log_debug("Applying float layout to element (float_prop=%d)", block->position->float_prop);

    // Get the parent's BlockContext - floats are positioned relative to their BFC container
    // lycon->block.parent points to the parent's context (pa_block in layout_block)
    BlockContext* parent_ctx = lycon->block.parent;
    if (!parent_ctx) {
        log_error("No parent BlockContext for float positioning");
        return;
    }

    // Find the BFC root from the parent's context
    BlockContext* bfc = block_context_find_bfc(parent_ctx);
    if (!bfc) {
        log_debug("No BFC found, using parent context directly");
        bfc = parent_ctx;
    }

    // Get the IMMEDIATE PARENT's content area offset (border + padding)
    // The float's x,y coordinates are relative to the PARENT's border box origin,
    // but space.left/right from block_context_space_at_y are relative to content area (0 = content left edge)
    // IMPORTANT: Use immediate parent, not BFC root - the float is positioned in parent's coordinate space
    ViewElement* parent_view = block->parent_view();
    float content_offset_x = 0;
    if (parent_view && parent_view->is_block()) {
        ViewBlock* parent_block = (ViewBlock*)parent_view;
        if (parent_block->bound) {
            if (parent_block->bound->border) {
                content_offset_x += parent_block->bound->border->width.left;
            }
            content_offset_x += parent_block->bound->padding.left;
        }
    }
    log_debug("Float parent: %s, content_offset_x=%.1f",
              parent_view ? parent_view->node_name() : "null", content_offset_x);

    float margin_left = block->bound ? block->bound->margin.left : 0;
    float margin_right = block->bound ? block->bound->margin.right : 0;

    // Get the parent block's content width for right float positioning
    // Use parent_ctx->content_width which is set from lycon->block.content_width
    // BEFORE content layout begins (unlike ViewBlock.content_width which is set after)
    float parent_content_width = parent_ctx->content_width;
    log_debug("Float positioning: using parent_ctx->content_width=%.1f", parent_content_width);

    // Get the current Y position of this float
    float current_y = block->y;
    if (block->bound) {
        current_y -= block->bound->margin.top;  // Remove margin that was already applied
    }

    // Query available horizontal space at this Y position using BlockContext API
    float total_height = block->height + (block->bound ?
        block->bound->margin.top + block->bound->margin.bottom : 0);
    FloatAvailableSpace space = block_context_space_at_y(bfc, current_y, total_height);

    log_debug("Float positioning: current_y=%.1f, available space left=%.1f, right=%.1f, content_offset_x=%.1f, parent_content_width=%.1f",
              current_y, space.left, space.right, content_offset_x, parent_content_width);

    // Calculate parent's position in BFC coordinates for coordinate conversion
    // This is needed because float space queries return BFC-relative coordinates,
    // but we position floats relative to their immediate parent
    float parent_x_in_bfc = 0;
    if (parent_view) {
        // Walk up the view tree to accumulate parent positions relative to BFC
        ViewElement* v = parent_view;
        while (v && v != bfc->establishing_element) {
            parent_x_in_bfc += v->x;
            ViewElement* pv = v->parent_view();
            if (!pv) break;
            v = pv;
        }
    }
    log_debug("Float parent_x_in_bfc=%.1f", parent_x_in_bfc);

    if (block->position->float_prop == CSS_VALUE_LEFT) {
        // Left float: position at left edge of available space
        float new_x;
        if (space.has_left_float) {
            // There's a left float - convert BFC coords to parent's border box coords
            float left_in_parent = space.left - parent_x_in_bfc;
            new_x = left_in_parent + margin_left;
        } else {
            // No left float intrusion - position at content area left edge
            new_x = content_offset_x + margin_left;
        }
        log_debug("Float:left repositioning: old_x=%.1f, new_x=%.1f (has_left=%d, content_offset=%.1f)",
                  block->x, new_x, space.has_left_float, content_offset_x);
        block->x = new_x;
    }
    else if (block->position->float_prop == CSS_VALUE_RIGHT) {
        // Right float: position at right edge of available space
        float new_x;
        if (space.has_right_float) {
            // There's a right float - convert BFC coords to parent's border box coords
            float right_in_parent = space.right - parent_x_in_bfc;
            // Position float so its margin box right edge aligns with the intrusion
            new_x = right_in_parent - block->width - margin_right;
        } else {
            // No right float intrusion - position at content area right edge
            float content_right = content_offset_x + parent_content_width;
            new_x = content_right - block->width - margin_right;
        }
        log_debug("Float:right repositioning: old_x=%.1f, new_x=%.1f (has_right=%d)",
                  block->x, new_x, space.has_right_float);
        block->x = new_x;
    }

    // Note: Float is added to BlockContext by the caller (layout_block_content)
    // to ensure it's added to the parent's context, not the float's own context
    log_debug("Float element positioned at (%.1f, %.1f) size (%.1f, %.1f)",
              block->x, block->y, block->width, block->height);
}

/**
 * Adjust line box boundaries based on intersecting floats
 * Uses the new float_space_at_y API for efficient queries.
 *
 * For text to flow around floats, we need to adjust line boundaries
 * when laying out content in blocks that are siblings of floats.
 *
 * Coordinate conversion:
 * - Floats are stored with coordinates relative to the BFC establishing element
 * - Line positions are relative to the current block being laid out
 * - We need to convert between these coordinate spaces
 * - Lines INSIDE a float should NOT be adjusted by the parent's float context
 */
void adjust_line_for_floats(LayoutContext* lycon) {
    // Find BFC using BlockContext API
    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (!bfc || !bfc->establishing_element) {
        log_debug("adjust_line_for_floats: early exit - no BFC or establishing_element");
        return;
    }

    // Get the current view being laid out - may not be a block
    View* current_view = lycon->view;
    if (!current_view) {
        log_debug("adjust_line_for_floats: early exit - no current_view");
        return;
    }

    // Check if current block is inside the BFC establishing element
    ViewBlock* container = bfc->establishing_element;

    // Find the view's position relative to the container
    // Walk up the parent chain to find relationship to container
    // IMPORTANT: If we encounter a floated element on the way up,
    // skip adjustment - lines inside floats don't adjust for parent's floats
    float block_offset_x = 0;
    float block_offset_y = 0;

    ViewElement* ancestor = (ViewElement*)current_view;
    bool found_container = false;
    while (ancestor) {
        if (ancestor == container) {
            found_container = true;
            break;
        }
        // Check if this ancestor is a floated block - if so, we're inside a float
        // and should NOT adjust for the parent's float context
        if (ancestor->is_block()) {
            ViewBlock* block = (ViewBlock*)ancestor;
            if (block->position && element_has_float(block)) {
                // We're inside a floated element, don't adjust lines
                log_debug("Skipping float adjustment: inside floated element %s at (%d, %d)",
                          block->node_name(), (int)block->x, (int)block->y);
                return;
            }
            block_offset_x += block->x;
            block_offset_y += block->y;
            // Add border and padding
            if (block->bound) {
                if (block->bound->border) {
                    block_offset_x += block->bound->border->width.left;
                    block_offset_y += block->bound->border->width.top;
                }
                block_offset_x += block->bound->padding.left;
                block_offset_y += block->bound->padding.top;
            }
        }
        ancestor = ancestor->parent_view();
    }

    if (!found_container) {
        // Current view is not inside the BFC establishing element
        log_debug("adjust_line_for_floats: early exit - view not inside container");
        return;
    }

    // Find the containing block for coordinate calculations
    ViewBlock* containing_block = nullptr;
    ViewElement* search = (ViewElement*)current_view;
    while (search && !containing_block) {
        if (search->is_block()) {
            containing_block = (ViewBlock*)search;
        } else {
            search = search->parent_view();
        }
    }
    if (!containing_block) {
        log_debug("adjust_line_for_floats: early exit - no containing_block");
        return;
    }

    // Convert current line Y to container-relative coordinates
    float line_top_container = block_offset_y + lycon->block.advance_y;
    float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f;

    log_debug("Adjusting line for floats: local_y=%.1f, container_y=%.1f, height=%.1f, offset=(%.1f, %.1f)",
              lycon->block.advance_y, line_top_container, line_height, block_offset_x, block_offset_y);

    // Query available space at current line position using BlockContext API
    FloatAvailableSpace space = block_context_space_at_y(bfc, line_top_container, line_height);

    // If there's no float intrusion at this Y position, skip adjustment entirely
    // This is critical for cleared elements that are positioned below floats
    if (!space.has_left_float && !space.has_right_float) {
        log_debug("No float intrusion at this Y position, skipping adjustment");
        return;
    }

    // Convert available space from container-relative to local block coordinates
    float local_left = space.left - containing_block->x;
    float local_right = space.right - containing_block->x;

    // The effective bounds should be in the same coordinate system as line.left/right
    float new_effective_left = local_left;
    float new_effective_right = local_right;

    // Clamp to the current block's line bounds
    new_effective_left = max(new_effective_left, lycon->line.left);
    new_effective_right = min(new_effective_right, lycon->line.right);

    log_debug("Float adjustment: space=(%.1f, %.1f), block_x=%.1f, local=(%.1f, %.1f), new_effective=(%.1f, %.1f)",
              space.left, space.right, containing_block->x, local_left, local_right,
              new_effective_left, new_effective_right);

    // Apply the float intrusion to effective bounds
    if (space.has_left_float && new_effective_left > lycon->line.left) {
        log_debug("Line effective_left adjusted: %.1f->%.1f (float intrusion)",
                  lycon->line.effective_left, new_effective_left);
        lycon->line.effective_left = new_effective_left;
        lycon->line.has_float_intrusion = true;
        // Also update advance_x if we're at line start
        if (lycon->line.is_line_start && lycon->line.advance_x < new_effective_left) {
            lycon->line.advance_x = new_effective_left;
        }
    }
    if (space.has_right_float && new_effective_right < lycon->line.right) {
        log_debug("Line effective_right adjusted: %.1f->%.1f (float intrusion)",
                  lycon->line.effective_right, new_effective_right);
        lycon->line.effective_right = new_effective_right;
        lycon->line.has_float_intrusion = true;
    }
}

/**
 * Initialize float context for a block layout (DEPRECATED - uses BlockContext now)
 */
void init_float_context_for_block(LayoutContext* lycon, ViewBlock* block) {
    if (!lycon || !block) return;

    // BlockContext initialization is now done in block_context_init()
    // This function remains for backward compatibility but is a no-op
    log_debug("init_float_context_for_block called for block %p (using unified BlockContext)",
              (void*)block);
}

/**
 * Get the current float context from layout context (DEPRECATED)
 * Returns NULL - use block_context_find_bfc(&lycon->block) instead
 */
FloatContext* get_current_float_context(LayoutContext* lycon) {
    // Return NULL - callers should migrate to BlockContext API
    return NULL;
}

/**
 * Clean up float context when layout is complete (DEPRECATED - no-op)
 */
void cleanup_float_context(LayoutContext* lycon) {
    // No-op - BlockContext cleanup is handled differently
}

/**
 * Apply clear property to an element
 */
void layout_clear_element(LayoutContext* lycon, ViewBlock* block) {
    // Check for actual clear values: LEFT, RIGHT, or BOTH
    // Note: We can't use "!= CSS_VALUE_NONE" because uninitialized clear is 0 (CSS_VALUE__UNDEF)
    if (!block->position ||
        (block->position->clear != CSS_VALUE_LEFT &&
         block->position->clear != CSS_VALUE_RIGHT &&
         block->position->clear != CSS_VALUE_BOTH)) {
        return;
    }

    log_debug("Applying clear property (clear=%d) to element %s",
              block->position->clear, block->node_name());

    // Find BFC using the PARENT's BlockContext
    // The current lycon->block is for the element being cleared, but floats are tracked
    // in the parent's context (or BFC root)
    BlockContext* parent_ctx = lycon->block.parent;
    if (!parent_ctx) {
        log_debug("No parent BlockContext, skipping clear");
        return;
    }

    BlockContext* bfc = block_context_find_bfc(parent_ctx);
    if (!bfc) {
        log_debug("No BFC found, skipping clear");
        return;
    }

    // Find the Y position where clear can be satisfied using BlockContext API
    // clear_y is in BFC coordinates (relative to BFC establishing element's content area)
    float clear_y_bfc = block_context_clear_y(bfc, block->position->clear);

    // Convert clear_y from BFC coords to parent's coordinate system
    // block->y is relative to block's parent, not the BFC
    // Need to calculate parent's position in BFC coords
    float parent_y_in_bfc = 0;
    ViewElement* parent_view = block->parent_view();
    if (parent_view) {
        ViewElement* v = parent_view;
        while (v && v != bfc->establishing_element) {
            parent_y_in_bfc += v->y;
            ViewElement* pv = v->parent_view();
            if (!pv) break;
            v = pv;
        }
    }

    // Convert BFC-relative clear_y to parent-relative
    float clear_y = clear_y_bfc - parent_y_in_bfc;

    log_debug("Clear position: clear_y_bfc=%.1f, parent_y_in_bfc=%.1f, clear_y=%.1f, block->y=%.1f (bfc has %d left, %d right floats)",
              clear_y_bfc, parent_y_in_bfc, clear_y, block->y, bfc->left_float_count, bfc->right_float_count);

    if (clear_y > block->y) {
        float delta = clear_y - block->y;
        block->y += delta;
        lycon->block.advance_y += delta;

        // IMPORTANT: Also update the parent's advance_y so container height is calculated correctly
        // The parent's BlockContext is accessed via parent pointer
        if (lycon->block.parent) {
            lycon->block.parent->advance_y += delta;
            log_debug("Updated parent advance_y by %.1f to %.1f", delta, lycon->block.parent->advance_y);
        }

        log_debug("Moved element down by %.1f to clear floats, new y=%.1f", delta, block->y);
    }
}

// ============================================================================
// Legacy Compatibility Functions (wrappers for old API)
// ============================================================================

FloatContext* create_float_context(ViewBlock* container) {
    return float_context_create(container);
}

void add_float_to_context(FloatContext* ctx, ViewBlock* element, CssEnum float_side) {
    if (element && element->position) {
        element->position->float_prop = float_side;
    }
    float_context_add_float(ctx, element);
}

void position_float_element(FloatContext* ctx, ViewBlock* element, CssEnum float_side) {
    if (!ctx || !element) return;
    if (element->position) {
        element->position->float_prop = float_side;
    }
    float_context_position_float(ctx, element, ctx->content_top);
}

int find_clear_position(FloatContext* ctx, CssEnum clear_value) {
    return (int)float_find_clear_position(ctx, clear_value);
}

bool float_intersects_line(FloatBox* float_box, int line_top, int line_bottom) {
    if (!float_box) return false;
    return !(float_box->margin_box_bottom <= line_top || float_box->margin_box_top >= line_bottom);
}
