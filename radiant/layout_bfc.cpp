/**
 * Block Formatting Context (BFC) Implementation
 *
 * Implements CSS 2.2 Section 9.5.1 Float Positioning Rules.
 */

#include "layout_bfc.hpp"
#include "layout.hpp"
#include "../lib/log.h"
#include <cmath>
#include <cfloat>

// =====================================================
// BfcFloatBox Implementation
// =====================================================

void BfcFloatBox::init_from_element(ViewBlock* elem, float bfc_origin_x, float bfc_origin_y) {
    element = elem;
    float_side = elem->position ? elem->position->float_prop : CSS_VALUE_NONE;

    // Calculate margin box bounds relative to BFC origin
    float margin_l = elem->bound ? elem->bound->margin.left : 0;
    float margin_r = elem->bound ? elem->bound->margin.right : 0;
    float margin_t = elem->bound ? elem->bound->margin.top : 0;
    float margin_b = elem->bound ? elem->bound->margin.bottom : 0;

    // elem->x, elem->y are absolute positions
    // Convert to BFC-relative margin box
    margin_left = elem->x - margin_l - bfc_origin_x;
    margin_top = elem->y - margin_t - bfc_origin_y;
    margin_right = elem->x + elem->width + margin_r - bfc_origin_x;
    margin_bottom = elem->y + elem->height + margin_b - bfc_origin_y;

    log_debug("[BFC] FloatBox init: elem=%s, margin_box=(%.1f,%.1f)-(%.1f,%.1f), side=%d",
              elem->node_name(), margin_left, margin_top, margin_right, margin_bottom, float_side);
}

// =====================================================
// BlockFormattingContext Implementation
// =====================================================

void BlockFormattingContext::init(ViewBlock* element, Pool* mem_pool) {
    establishing_element = element;
    parent_bfc = nullptr;

    left_floats_head = left_floats_tail = nullptr;
    left_float_count = 0;

    right_floats_head = right_floats_tail = nullptr;
    right_float_count = 0;

    // Calculate origin from element's content area
    origin_x = element->x;
    origin_y = element->y;

    if (element->bound) {
        if (element->bound->border) {
            origin_x += element->bound->border->width.left;
            origin_y += element->bound->border->width.top;
        }
        origin_x += element->bound->padding.left;
        origin_y += element->bound->padding.top;
    }

    // Content area bounds
    content_left = 0;
    content_top = 0;
    content_right = element->content_width > 0 ? element->content_width : element->width;
    if (element->bound) {
        content_right -= element->bound->padding.left + element->bound->padding.right;
        if (element->bound->border) {
            content_right -= element->bound->border->width.left + element->bound->border->width.right;
        }
    }
    if (content_right < 0) content_right = element->width;

    lowest_float_bottom = 0;
    pool = mem_pool;

    log_debug("[BFC] Init: establishing=%s, origin=(%.1f,%.1f), content_right=%.1f",
              element->node_name(), origin_x, origin_y, content_right);
}

void BlockFormattingContext::reset() {
    left_floats_head = left_floats_tail = nullptr;
    left_float_count = 0;
    right_floats_head = right_floats_tail = nullptr;
    right_float_count = 0;
    lowest_float_bottom = 0;
}

BfcFloatBox* BlockFormattingContext::alloc_float_box() {
    if (pool) {
        return (BfcFloatBox*)pool_calloc(pool, sizeof(BfcFloatBox));
    }
    return (BfcFloatBox*)calloc(1, sizeof(BfcFloatBox));
}

bool BlockFormattingContext::float_intersects_y(const BfcFloatBox* box, float y_top, float y_bottom) {
    // Check if box's margin box intersects the Y range [y_top, y_bottom)
    return !(box->margin_bottom <= y_top || box->margin_top >= y_bottom);
}

void BlockFormattingContext::add_float(ViewBlock* element) {
    if (!element || !element->position) return;

    CssEnum side = element->position->float_prop;
    if (side != CSS_VALUE_LEFT && side != CSS_VALUE_RIGHT) return;

    BfcFloatBox* box = alloc_float_box();
    if (!box) return;

    box->init_from_element(element, origin_x, origin_y);

    // Add to appropriate list
    if (side == CSS_VALUE_LEFT) {
        if (!left_floats_head) {
            left_floats_head = left_floats_tail = box;
        } else {
            // Insert sorted by margin_top for efficient queries
            if (box->margin_top <= left_floats_head->margin_top) {
                // Insert at head (not linked list, just track head/tail)
                left_floats_tail = box;  // Actually append for now
            }
            left_floats_tail = box;
        }
        left_float_count++;
        log_debug("[BFC] Added left float: count=%d, bottom=%.1f", left_float_count, box->margin_bottom);
    } else {
        if (!right_floats_head) {
            right_floats_head = right_floats_tail = box;
        } else {
            right_floats_tail = box;
        }
        right_float_count++;
        log_debug("[BFC] Added right float: count=%d, bottom=%.1f", right_float_count, box->margin_bottom);
    }

    // Update lowest float bottom
    if (box->margin_bottom > lowest_float_bottom) {
        lowest_float_bottom = box->margin_bottom;
    }
}

BfcAvailableSpace BlockFormattingContext::space_at_y(float y, float height) const {
    BfcAvailableSpace space;
    space.left = content_left;
    space.right = content_right;

    float y_top = y;
    float y_bottom = y + height;

    // Early exit if no floats or below all floats
    if (left_float_count == 0 && right_float_count == 0) {
        return space;
    }
    if (y_top >= lowest_float_bottom) {
        return space;
    }

    // Check left floats - find rightmost intrusion
    BfcFloatBox* box = left_floats_head;
    while (box) {
        if (float_intersects_y(box, y_top, y_bottom)) {
            if (box->margin_right > space.left) {
                space.left = box->margin_right;
            }
        }
        // Simple iteration - floats stored in a flat array conceptually
        // For now we only track head (single float per side limitation)
        // TODO: Implement proper linked list for multiple floats
        break;
    }

    // Check right floats - find leftmost intrusion
    box = right_floats_head;
    while (box) {
        if (float_intersects_y(box, y_top, y_bottom)) {
            if (box->margin_left < space.right) {
                space.right = box->margin_left;
            }
        }
        break;
    }

    // Ensure valid space (right >= left)
    if (space.right < space.left) {
        space.right = space.left;
    }

    log_debug("[BFC] space_at_y(%.1f, h=%.1f): left=%.1f, right=%.1f, width=%.1f",
              y, height, space.left, space.right, space.width());

    return space;
}

float BlockFormattingContext::find_y_for_width(float required_width, float min_y) const {
    if (left_float_count == 0 && right_float_count == 0) {
        return min_y;
    }

    float y = min_y;
    int max_iterations = 100;

    while (max_iterations-- > 0) {
        BfcAvailableSpace space = space_at_y(y, 1.0f);
        if (space.width() >= required_width) {
            return y;
        }

        // Move to next float bottom
        float next_y = find_next_float_bottom(y);
        if (next_y <= y || std::isinf(next_y)) {
            break;
        }
        y = next_y;
    }

    return y;
}

float BlockFormattingContext::find_clear_y(CssEnum clear_type) const {
    float clear_y = content_top;

    if (clear_type == CSS_VALUE_LEFT || clear_type == CSS_VALUE_BOTH) {
        BfcFloatBox* box = left_floats_head;
        while (box) {
            if (box->margin_bottom > clear_y) {
                clear_y = box->margin_bottom;
            }
            break;  // Single float for now
        }
    }

    if (clear_type == CSS_VALUE_RIGHT || clear_type == CSS_VALUE_BOTH) {
        BfcFloatBox* box = right_floats_head;
        while (box) {
            if (box->margin_bottom > clear_y) {
                clear_y = box->margin_bottom;
            }
            break;
        }
    }

    log_debug("[BFC] find_clear_y(%d): %.1f", clear_type, clear_y);
    return clear_y;
}

float BlockFormattingContext::find_next_float_bottom(float after_y) const {
    float next_y = FLT_MAX;

    BfcFloatBox* box = left_floats_head;
    while (box) {
        if (box->margin_bottom > after_y && box->margin_bottom < next_y) {
            next_y = box->margin_bottom;
        }
        break;
    }

    box = right_floats_head;
    while (box) {
        if (box->margin_bottom > after_y && box->margin_bottom < next_y) {
            next_y = box->margin_bottom;
        }
        break;
    }

    return next_y;
}

void BlockFormattingContext::position_float(ViewBlock* element, float current_line_y) {
    if (!element || !element->position) return;

    CssEnum side = element->position->float_prop;
    if (side != CSS_VALUE_LEFT && side != CSS_VALUE_RIGHT) return;

    // Get element dimensions including margins
    float margin_l = element->bound ? element->bound->margin.left : 0;
    float margin_r = element->bound ? element->bound->margin.right : 0;
    float margin_t = element->bound ? element->bound->margin.top : 0;
    float total_width = element->width + margin_l + margin_r;
    float total_height = element->height +
                         margin_t +
                         (element->bound ? element->bound->margin.bottom : 0);

    // CSS 2.2 Rules 4, 5, 6: Float top >= max of various positions
    float min_y = fmax(content_top, current_line_y);

    // Rule 6: Float's top may not be higher than any earlier float's top
    if (left_floats_head && left_floats_head->margin_top < min_y) {
        // Earlier floats exist - we can be at same level or lower
    }
    if (right_floats_head && right_floats_head->margin_top < min_y) {
        // Same for right floats
    }

    // Find Y where float fits horizontally
    float y = find_y_for_width(total_width, min_y);

    // Get available space at that Y
    BfcAvailableSpace space = space_at_y(y, total_height);

    // Position horizontally based on float side
    float x;
    if (side == CSS_VALUE_LEFT) {
        // Rule 1: Left float at left edge of available space
        x = origin_x + space.left + margin_l;
    } else {
        // Rule 2: Right float at right edge of available space
        x = origin_x + space.right - element->width - margin_r;
    }

    // Set element position
    element->x = x;
    element->y = origin_y + y + margin_t;

    log_debug("[BFC] Positioned %s float: (%.1f, %.1f) size (%.1f, %.1f)",
              side == CSS_VALUE_LEFT ? "left" : "right",
              element->x, element->y, element->width, element->height);

    // Add to float list
    add_float(element);
}

bool BlockFormattingContext::would_overlap_floats(float x, float y, float width, float height, CssEnum side) const {
    // Check if a potential float placement would overlap existing floats
    float y_top = y;
    float y_bottom = y + height;

    BfcFloatBox* box = (side == CSS_VALUE_LEFT) ? left_floats_head : right_floats_head;
    while (box) {
        if (float_intersects_y(box, y_top, y_bottom)) {
            // Check horizontal overlap
            if (side == CSS_VALUE_LEFT) {
                if (x < box->margin_right) return true;
            } else {
                if (x + width > box->margin_left) return true;
            }
        }
        break;
    }
    return false;
}

float BlockFormattingContext::to_bfc_x(float local_x, ViewBlock* block) const {
    // Convert local block coordinate to BFC coordinate
    float offset_x = 0;
    ViewElement* ancestor = block;
    while (ancestor && ancestor != establishing_element) {
        offset_x += ancestor->x;
        if (ancestor->bound) {
            if (ancestor->bound->border) {
                offset_x += ancestor->bound->border->width.left;
            }
            offset_x += ancestor->bound->padding.left;
        }
        ancestor = ancestor->parent_view();
    }
    return local_x + offset_x - origin_x + establishing_element->x;
}

float BlockFormattingContext::to_bfc_y(float local_y, ViewBlock* block) const {
    float offset_y = 0;
    ViewElement* ancestor = block;
    while (ancestor && ancestor != establishing_element) {
        offset_y += ancestor->y;
        if (ancestor->bound) {
            if (ancestor->bound->border) {
                offset_y += ancestor->bound->border->width.top;
            }
            offset_y += ancestor->bound->padding.top;
        }
        ancestor = ancestor->parent_view();
    }
    return local_y + offset_y - origin_y + establishing_element->y;
}

float BlockFormattingContext::from_bfc_x(float bfc_x, ViewBlock* block) const {
    float offset_x = 0;
    ViewElement* ancestor = block;
    while (ancestor && ancestor != establishing_element) {
        offset_x += ancestor->x;
        if (ancestor->bound) {
            if (ancestor->bound->border) {
                offset_x += ancestor->bound->border->width.left;
            }
            offset_x += ancestor->bound->padding.left;
        }
        ancestor = ancestor->parent_view();
    }
    return bfc_x - offset_x + origin_x - establishing_element->x;
}

float BlockFormattingContext::from_bfc_y(float bfc_y, ViewBlock* block) const {
    float offset_y = 0;
    ViewElement* ancestor = block;
    while (ancestor && ancestor != establishing_element) {
        offset_y += ancestor->y;
        if (ancestor->bound) {
            if (ancestor->bound->border) {
                offset_y += ancestor->bound->border->width.top;
            }
            offset_y += ancestor->bound->padding.top;
        }
        ancestor = ancestor->parent_view();
    }
    return bfc_y - offset_y + origin_y - establishing_element->y;
}

// =====================================================
// Helper Functions
// =====================================================

bool element_establishes_bfc(ViewBlock* block) {
    if (!block) return false;

    // Root element (html, body)
    if (!block->parent) return true;

    // Floats
    if (block->position &&
        (block->position->float_prop == CSS_VALUE_LEFT ||
         block->position->float_prop == CSS_VALUE_RIGHT)) {
        return true;
    }

    // Absolutely positioned elements
    if (block->position &&
        (block->position->position == CSS_VALUE_ABSOLUTE ||
         block->position->position == CSS_VALUE_FIXED)) {
        return true;
    }

    // overflow != visible
    if (block->scroller &&
        (block->scroller->overflow_x != CSS_VALUE_VISIBLE ||
         block->scroller->overflow_y != CSS_VALUE_VISIBLE)) {
        return true;
    }

    // inline-block
    if (block->display.outer == CSS_VALUE_INLINE_BLOCK) {
        return true;
    }

    // display: flow-root
    if (block->display.inner == CSS_VALUE_FLOW_ROOT) {
        return true;
    }

    // Flex and grid items (not containers)
    // Table cells, table captions
    if (block->display.inner == CSS_VALUE_TABLE_CELL ||
        block->display.inner == CSS_VALUE_TABLE_CAPTION) {
        return true;
    }

    return false;
}

BlockFormattingContext* create_bfc_if_needed(ViewBlock* block, Pool* pool,
                                              BlockFormattingContext* parent_bfc) {
    if (!element_establishes_bfc(block)) {
        return nullptr;
    }

    BlockFormattingContext* bfc = (BlockFormattingContext*)pool_calloc(pool, sizeof(BlockFormattingContext));
    if (!bfc) return nullptr;

    bfc->init(block, pool);
    bfc->parent_bfc = parent_bfc;

    log_debug("[BFC] Created new BFC for %s (parent=%p)",
              block->node_name(), (void*)parent_bfc);

    return bfc;
}

// NOTE: This function is deprecated - use lycon->block directly for BlockContext
BlockFormattingContext* find_containing_bfc(LayoutContext* lycon) {
    // With unified BlockContext, the BFC is accessed via lycon->block
    // Return null since we no longer track separate BFC pointers
    (void)lycon;
    return nullptr;
}

void calculate_block_offset_in_bfc(ViewBlock* block, BlockFormattingContext* bfc,
                                    float* offset_x, float* offset_y) {
    *offset_x = 0;
    *offset_y = 0;

    if (!bfc || !block) return;

    ViewElement* ancestor = block;
    while (ancestor && ancestor != bfc->establishing_element) {
        *offset_x += ancestor->x;
        *offset_y += ancestor->y;
        if (ancestor->bound) {
            if (ancestor->bound->border) {
                *offset_x += ancestor->bound->border->width.left;
                *offset_y += ancestor->bound->border->width.top;
            }
            *offset_x += ancestor->bound->padding.left;
            *offset_y += ancestor->bound->padding.top;
        }
        ancestor = ancestor->parent_view();
    }
}
