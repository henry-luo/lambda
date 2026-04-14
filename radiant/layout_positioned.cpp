#include "layout.hpp"
#include "layout_positioned.hpp"
#include "available_space.hpp"
#include "intrinsic_sizing.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/css_style.hpp"
#include <stdlib.h>
#include <cfloat>
#include <cmath>
#include <algorithm>

using std::min;
using std::max;

// Forward declarations
ViewBlock* find_containing_block(ViewBlock* element, CssEnum position_type);
// adjust_min_max_* and adjust_border_padding_* declared in layout.hpp
void layout_block_inner_content(LayoutContext* lycon, ViewBlock* block);
void setup_inline(LayoutContext* lycon, ViewBlock* block);

/**
 * CSS 2.1 §10.3.7: Check if an element's CSS-specified display was inline-level
 * (before §9.7 blockification for abspos/float).
 * Reads from the style tree rather than elem->display, which is overwritten
 * with the blockified value during layout_block().
 */
static bool was_specified_inline(DomElement* elem) {
    if (!elem || !elem->specified_style || !elem->specified_style->tree) return false;
    AvlNode* disp_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_DISPLAY);
    if (!disp_node) return false;
    StyleNode* sn = (StyleNode*)disp_node->declaration;
    if (!sn || !sn->winning_decl || !sn->winning_decl->value) return false;
    if (sn->winning_decl->value->type != CSS_VALUE_TYPE_KEYWORD) return false;
    CssEnum kw = sn->winning_decl->value->data.keyword;
    return kw == CSS_VALUE_INLINE || kw == CSS_VALUE_INLINE_BLOCK ||
           kw == CSS_VALUE_INLINE_FLEX || kw == CSS_VALUE_INLINE_GRID ||
           kw == CSS_VALUE_INLINE_TABLE;
}

/**
 * Recursively offset all child views by the given amounts
 * Used for inline relative positioning where children have block-relative coordinates
 *
 * Note: For block-level children, we offset the block itself but NOT its contents.
 * Block children break out of inline context and establish their own coordinate system,
 * so their internal content (text, nested elements) should not be affected by the
 * inline span's relative positioning offset.
 */
static void offset_children_recursive(ViewElement* elem, float offset_x, float offset_y) {
    View* child = elem->first_child;
    while (child) {
        child->x += offset_x;
        child->y += offset_y;

        // For text nodes, also offset all TextRect positions
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)child;
            TextRect* rect = text->rect;
            while (rect) {
                rect->x += offset_x;
                rect->y += offset_y;
                rect = rect->next;
            }
        }

        // Recurse into element children, BUT skip recursing into block children
        // Block children have their own coordinate system - their internal content
        // positions are relative to the block, not to the inline span
        if (child->is_element() && child->view_type != RDT_VIEW_BLOCK) {
            offset_children_recursive((ViewElement*)child, offset_x, offset_y);
        }
        child = child->next();
    }
}

/**
 * Apply relative positioning to an element
 * Relative positioning moves the element from its normal position without affecting other elements
 */
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block) {
    log_debug("Applying relative positioning to element");
    // calculate offset from top/right/bottom/left properties
    float offset_x = 0, offset_y = 0;

    // Get parent's text direction to determine horizontal offset precedence
    // The CSS 'direction' property determines which value wins when both left and right are specified
    TextDirection parent_direction = TD_LTR;  // default to LTR
    ViewElement* parent = block->parent_view();
    if (parent && parent->is_element()) {
        DomElement* parent_elem = (DomElement*)parent;
        // Use the computed direction from BlockProp if available (set during CSS resolution)
        if (parent_elem->blk && parent_elem->blk->direction == CSS_VALUE_RTL) {
            parent_direction = TD_RTL;
            log_debug("Parent has direction: rtl (from BlockProp)");
        } else if (parent_elem->specified_style) {
            // Fall back to querying specified_style for elements without BlockProp
            CssValue* direction_value = (CssValue*)style_tree_get_computed_value(
                parent_elem->specified_style,
                CSS_PROPERTY_DIRECTION,
                parent_elem->parent && parent_elem->parent->is_element() ?
                    ((DomElement*)parent_elem->parent)->specified_style : NULL
            );

            if (direction_value && direction_value->type == CSS_VALUE_TYPE_KEYWORD &&
                direction_value->data.keyword == CSS_VALUE_RTL) {
                parent_direction = TD_RTL;
                log_debug("Parent has direction: rtl (from specified_style)");
            }
        }
    }

    // Get containing block dimensions for percentage resolution
    // CSS Position 3 §3.4: percentage top/bottom resolve against containing block height,
    // percentage left/right resolve against containing block width
    // Note: We cannot use parent->content_width/content_height here because those fields
    // are set in finalize_block_flow() AFTER all children are laid out. At this point
    // (during child layout), they are still 0. Instead, derive from the parent's
    // border-box width/height minus padding and border.
    float cb_width = 0, cb_height = 0;
    ViewElement* cb_parent = parent;
    // CSS 2.1 §9.2.1.1: For block children inside inline spans (block-in-inline),
    // the containing block is the nearest block-level ancestor, not the inline span.
    while (cb_parent && !cb_parent->is_block()) {
        cb_parent = cb_parent->parent_view();
    }
    if (cb_parent && cb_parent->is_block()) {
        ViewBlock* parent_block = (ViewBlock*)cb_parent;
        // Compute content-box width from border-box width
        float pad_border_h = 0;
        if (parent_block->bound) {
            pad_border_h = parent_block->bound->padding.left + parent_block->bound->padding.right;
            if (parent_block->bound->border)
                pad_border_h += parent_block->bound->border->width.left + parent_block->bound->border->width.right;
        }
        cb_width = parent_block->width - pad_border_h;

        // CSS 2.1 §10.5: If containing block height is auto, percentage top/bottom = 0
        // Only use parent height if it has an explicit (non-auto) height
        bool parent_has_definite_height = false;
        if (parent_block->blk && parent_block->blk->given_height >= 0) {
            parent_has_definite_height = true;
        }
        if (parent_has_definite_height) {
            float pad_border_v = 0;
            if (parent_block->bound) {
                pad_border_v = parent_block->bound->padding.top + parent_block->bound->padding.bottom;
                if (parent_block->bound->border)
                    pad_border_v += parent_block->bound->border->width.top + parent_block->bound->border->width.bottom;
            }
            cb_height = parent_block->height - pad_border_v;
        }
    }

    // horizontal offset: precedence depends on containing block's direction
    // CSS spec: If both left and right are not 'auto':
    // - If direction is 'ltr', left wins and right is ignored
    // - If direction is 'rtl', right wins and left is ignored (but equal values cancel in RTL)
    bool both_horizontal = block->position->has_left && block->position->has_right;

    if (both_horizontal) {
        if (parent_direction == TD_RTL) {
            // CSS 2.1 §9.4.3: RTL — right wins, left becomes -right
            if (!isnan(block->position->right_percent)) {
                offset_x = -(block->position->right_percent * cb_width / 100.0f);
            } else {
                offset_x = -block->position->right;
            }
            log_debug("Over-constrained relative positioning (RTL): right wins, left ignored");
        } else {
            // LTR: left takes precedence (always, even if equal to right)
            if (!isnan(block->position->left_percent)) {
                offset_x = block->position->left_percent * cb_width / 100.0f;
            } else {
                offset_x = block->position->left;
            }
            log_debug("Over-constrained relative positioning (LTR): left wins, right ignored");
        }
    } else if (block->position->has_left) {
        if (!isnan(block->position->left_percent)) {
            offset_x = block->position->left_percent * cb_width / 100.0f;
        } else {
            offset_x = block->position->left;
        }
    } else if (block->position->has_right) {
        if (!isnan(block->position->right_percent)) {
            offset_x = -(block->position->right_percent * cb_width / 100.0f);
        } else {
            offset_x = -block->position->right;
        }
    }
    // vertical offset: top takes precedence over bottom
    // CSS 2.1 §10.6.5: if containing block height is auto, percentage top/bottom = 0
    if (block->position->has_top) {
        if (!isnan(block->position->top_percent)) {
            offset_y = block->position->top_percent * cb_height / 100.0f;
        } else {
            offset_y = block->position->top;
        }
    } else if (block->position->has_bottom) {
        if (!isnan(block->position->bottom_percent)) {
            offset_y = -(block->position->bottom_percent * cb_height / 100.0f);
        } else {
            offset_y = -block->position->bottom;
        }
    }
    log_debug("Calculated relative offset: x=%.1f, y=%.1f (parent direction=%s, cb=%.0fx%.0f)",
             offset_x, offset_y, parent_direction == TD_RTL ? "RTL" : "LTR", cb_width, cb_height);

    // apply offset to visual position (doesn't affect layout of other elements)
    block->x += offset_x;  block->y += offset_y;
    log_debug("Applied relative positioning: offset (%.1f, %.1f), final position (%.0f, %.0f)",
              offset_x, offset_y, block->x, block->y);

    // For inline elements (spans), children have block-relative coordinates,
    // so we must also offset all descendants to move with the inline box
    if (block->view_type == RDT_VIEW_INLINE && (offset_x != 0 || offset_y != 0)) {
        log_debug("Offsetting inline children by (%.1f, %.1f)", offset_x, offset_y);
        offset_children_recursive((ViewElement*)block, offset_x, offset_y);
    }

    // todo: add to chain of positioned elements for z-index stacking
    // find containing block; add to its positioned children list;
}

/**
 * Apply sticky positioning to an element (CSS Position 3 §3.5)
 *
 * At scroll position 0, sticky elements remain at their normal flow position
 * unless their inset constraints (top/bottom/left/right) would pull them into
 * the visible area of their nearest scroll container. The element is also
 * constrained to stay within its containing block (parent box).
 *
 * The inset values define constraints, NOT offsets:
 *   top: T → element's top edge must be >= scrollport_top + T
 *   bottom: B → element's bottom edge must be <= scrollport_bottom - B
 *   left: L → element's left edge must be >= scrollport_left + L
 *   right: R → element's right edge must be <= scrollport_right - R
 */
void layout_sticky_positioned(LayoutContext* lycon, ViewBlock* block) {
    if (!block->position) return;

    // find the nearest scroll container ancestor (overflow != visible)
    ViewElement* scroll_ancestor = NULL;
    for (ViewElement* p = block->parent_view(); p; p = p->parent_view()) {
        if (!p->is_block()) continue;
        ViewBlock* pb = (ViewBlock*)p;
        if (pb->scroller &&
            (pb->scroller->overflow_x != CSS_VALUE_VISIBLE ||
             pb->scroller->overflow_y != CSS_VALUE_VISIBLE)) {
            scroll_ancestor = p;
            break;
        }
    }

    if (!scroll_ancestor) {
        log_debug("sticky: no scroll container found, no offset applied");
        return;
    }

    ViewBlock* scroller = (ViewBlock*)scroll_ancestor;

    // Scrollport: scroller's content box at (0, 0) in scroller content coordinates.
    // At scroll position 0, the visible area starts at the content box origin.
    float sp_content_h = (float)scroller->height;
    float sp_content_w = (float)scroller->width;
    if (scroller->bound) {
        sp_content_h -= scroller->bound->padding.top + scroller->bound->padding.bottom;
        sp_content_w -= scroller->bound->padding.left + scroller->bound->padding.right;
        if (scroller->bound->border) {
            sp_content_h -= scroller->bound->border->width.top + scroller->bound->border->width.bottom;
            sp_content_w -= scroller->bound->border->width.left + scroller->bound->border->width.right;
        }
    }
    float sp_top = 0, sp_left = 0;
    float sp_bottom = sp_content_h;
    float sp_right = sp_content_w;

    // Convert element position from parent-relative to scroller content coordinates.
    // Walk from element's parent up to (not including) the scroller, accumulating
    // each ancestor's border-box offset plus its padding/border to reach its content area.
    float offset_to_scroller_y = 0;
    float offset_to_scroller_x = 0;
    for (ViewElement* p = block->parent_view(); p && p != scroll_ancestor; p = p->parent_view()) {
        if (p->is_block()) {
            ViewBlock* pb = (ViewBlock*)p;
            offset_to_scroller_y += (float)pb->y;
            offset_to_scroller_x += (float)pb->x;
            if (pb->bound) {
                offset_to_scroller_y += pb->bound->padding.top;
                offset_to_scroller_x += pb->bound->padding.left;
                if (pb->bound->border) {
                    offset_to_scroller_y += pb->bound->border->width.top;
                    offset_to_scroller_x += pb->bound->border->width.left;
                }
            }
        }
    }

    // element position in scroller content coordinates
    float elem_top = (float)block->y + offset_to_scroller_y;
    float elem_left = (float)block->x + offset_to_scroller_x;
    float elem_bottom = elem_top + (float)block->height;
    float elem_right = elem_left + (float)block->width;

    log_debug("sticky: scrollport [%.1f, %.1f, %.1f, %.1f], element in scroller coords (%.1f, %.1f) size %dx%d",
              sp_left, sp_top, sp_right, sp_bottom, elem_left, elem_top, block->width, block->height);

    float offset_x = 0, offset_y = 0;

    // vertical sticky constraints
    if (block->position->has_top && block->position->has_bottom) {
        float inset = block->position->top;
        float min_top = sp_top + inset;
        if (elem_top < min_top) {
            offset_y = min_top - elem_top;
        }
    } else if (block->position->has_top) {
        float inset = block->position->top;
        float min_top = sp_top + inset;
        if (elem_top < min_top) {
            offset_y = min_top - elem_top;
        }
    } else if (block->position->has_bottom) {
        float inset = block->position->bottom;
        float max_bottom = sp_bottom - inset;
        if (elem_bottom > max_bottom) {
            offset_y = max_bottom - elem_bottom;
        }
    }

    // horizontal sticky constraints
    if (block->position->has_left && block->position->has_right) {
        float inset = block->position->left;
        float min_left = sp_left + inset;
        if (elem_left < min_left) {
            offset_x = min_left - elem_left;
        }
    } else if (block->position->has_left) {
        float inset = block->position->left;
        float min_left = sp_left + inset;
        if (elem_left < min_left) {
            offset_x = min_left - elem_left;
        }
    } else if (block->position->has_right) {
        float inset = block->position->right;
        float max_right = sp_right - inset;
        if (elem_right > max_right) {
            offset_x = max_right - elem_right;
        }
    }

    // Constrain: element must stay within its containing block (parent).
    // Use parent content coordinates (element's y=0 is parent content top).
    ViewElement* parent = block->parent_view();
    if (parent && parent->is_block() && (offset_x != 0 || offset_y != 0)) {
        ViewBlock* cb = (ViewBlock*)parent;
        // containing block content area in parent-relative coordinates: [0, content_height]
        float cb_content_top = 0;
        float cb_content_left = 0;
        float cb_content_bottom = (float)cb->height;
        float cb_content_right = (float)cb->width;
        if (cb->bound) {
            cb_content_bottom -= cb->bound->padding.top + cb->bound->padding.bottom;
            cb_content_right -= cb->bound->padding.left + cb->bound->padding.right;
            if (cb->bound->border) {
                cb_content_bottom -= cb->bound->border->width.top + cb->bound->border->width.bottom;
                cb_content_right -= cb->bound->border->width.left + cb->bound->border->width.right;
            }
        }

        // clamp offset so element stays in containing block (in parent coords)
        float local_top = (float)block->y;
        float local_bottom = local_top + (float)block->height;
        float local_left = (float)block->x;
        float local_right = local_left + (float)block->width;

        if (offset_y > 0 && (local_bottom + offset_y) > cb_content_bottom) {
            offset_y = max(0.0f, cb_content_bottom - local_bottom);
        }
        if (offset_y < 0 && (local_top + offset_y) < cb_content_top) {
            offset_y = min(0.0f, cb_content_top - local_top);
        }
        if (offset_x > 0 && (local_right + offset_x) > cb_content_right) {
            offset_x = max(0.0f, cb_content_right - local_right);
        }
        if (offset_x < 0 && (local_left + offset_x) < cb_content_left) {
            offset_x = min(0.0f, cb_content_left - local_left);
        }
    }

    if (offset_x != 0 || offset_y != 0) {
        block->x += offset_x;
        block->y += offset_y;
        log_debug("sticky: applied offset (%.1f, %.1f), new position (%.0f, %.0f)", offset_x, offset_y, block->x, block->y);

        if (block->view_type == RDT_VIEW_INLINE) {
            offset_children_recursive((ViewElement*)block, offset_x, offset_y);
        }
    } else {
        log_debug("sticky: no offset needed, element at static position");
    }
}

/**
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
// Implements CSS 2.1 §10.3.7 (horizontal) and §10.6.4 (vertical) constraint equations
void calculate_absolute_position(LayoutContext* lycon, ViewBlock* block, ViewBlock* containing_block) {

    // get containing block dimensions
    float cb_x = containing_block->x, cb_y = containing_block->y;
    float cb_width = containing_block->width, cb_height = containing_block->height;

    // CSS 2.1 Section 10.1: For absolutely positioned elements, if the containing block is
    // the initial containing block (ICB - i.e., the root element with no positioned ancestors),
    // the ICB is the viewport rectangle at (0,0) with viewport dimensions.
    // It is NOT the root element's padding box — the root element's borders must not be subtracted.
    bool is_icb = (containing_block->parent_view() == nullptr);
    if (is_icb && lycon->ui_context) {
        // ICB = viewport: origin at (0,0), size = viewport dimensions
        cb_x = 0;
        cb_y = 0;
        if (lycon->ui_context->viewport_width > 0) {
            cb_width = lycon->ui_context->viewport_width * lycon->ui_context->pixel_ratio;
        }
        if (lycon->ui_context->viewport_height > 0) {
            cb_height = lycon->ui_context->viewport_height * lycon->ui_context->pixel_ratio;
        }
        log_debug("[ABS POS] Using viewport as ICB: (0, 0) size (%.1f, %.1f)", cb_width, cb_height);
    }

    // Calculate border offset - the absolute element is positioned relative to padding box,
    // but block->x/y are stored relative to containing block's origin (border box)
    float border_offset_x = 0, border_offset_y = 0;

    // For positioned ancestors (not ICB), use the padding box dimensions
    // For ICB, skip border adjustment — the viewport has no borders
    if (!is_icb && containing_block->bound) {
        if (containing_block->bound->border) {
            border_offset_x = containing_block->bound->border->width.left;
            border_offset_y = containing_block->bound->border->width.top;
            cb_x += border_offset_x;
            cb_y += border_offset_y;
            cb_width -= (containing_block->bound->border->width.left + containing_block->bound->border->width.right);
            cb_height -= (containing_block->bound->border->width.top + containing_block->bound->border->width.bottom);
        }
    }
    log_debug("containing block padding box: (%.0f, %.0f) size (%.0f, %.0f), border_offset: (%f, %f)",
              cb_x, cb_y, cb_width, cb_height, border_offset_x, border_offset_y);

    // re-resolve percentage position values against the actual containing block
    // during CSS resolution, percentages were resolved against the wrong reference (parent at resolution time)
    // for absolute positioned elements, percentages are relative to the containing block's padding box
    if (block->position->has_left && !isnan(block->position->left_percent)) {
        block->position->left = block->position->left_percent * cb_width / 100.0f;
        log_debug("[ABS POS] re-resolved left: %.1f%% of %.1f = %.1f", block->position->left_percent, cb_width, block->position->left);
    }
    if (block->position->has_right && !isnan(block->position->right_percent)) {
        block->position->right = block->position->right_percent * cb_width / 100.0f;
        log_debug("[ABS POS] re-resolved right: %.1f%% of %.1f = %.1f", block->position->right_percent, cb_width, block->position->right);
    }
    if (block->position->has_top && !isnan(block->position->top_percent)) {
        block->position->top = block->position->top_percent * cb_height / 100.0f;
        log_debug("[ABS POS] re-resolved top: %.1f%% of %.1f = %.1f", block->position->top_percent, cb_height, block->position->top);
    }
    if (block->position->has_bottom && !isnan(block->position->bottom_percent)) {
        block->position->bottom = block->position->bottom_percent * cb_height / 100.0f;
        log_debug("[ABS POS] re-resolved bottom: %.1f%% of %.1f = %.1f", block->position->bottom_percent, cb_height, block->position->bottom);
    }

    // re-resolve percentage width/height against the actual containing block
    if (block->blk && !isnan(block->blk->given_width_percent)) {
        lycon->block.given_width = block->blk->given_width_percent * cb_width / 100.0f;
        block->blk->given_width = lycon->block.given_width;
        log_debug("[ABS POS] re-resolved width: %.1f%% of %.1f = %.1f", block->blk->given_width_percent, cb_width, lycon->block.given_width);
    }
    if (block->blk && !isnan(block->blk->given_height_percent)) {
        lycon->block.given_height = block->blk->given_height_percent * cb_height / 100.0f;
        block->blk->given_height = lycon->block.given_height;
        log_debug("[ABS POS] re-resolved height: %.1f%% of %.1f = %.1f", block->blk->given_height_percent, cb_height, lycon->block.given_height);
    }

    // CSS 2.1 §10.3.8: For absolutely positioned replaced elements with
    // 'width: auto', use the intrinsic width. §10.6.5: Same for height.
    // Replaced elements include iframe (300x150), img (intrinsic from image).
    if (block->display.inner == RDT_DISPLAY_REPLACED) {
        uintptr_t tag = block->tag();
        if (tag == HTM_TAG_IFRAME) {
            if (lycon->block.given_width < 0) {
                lycon->block.given_width = 300;
                if (block->blk) block->blk->given_width = 300;
                log_debug("[ABS POS] iframe intrinsic width: 300");
            }
            if (lycon->block.given_height < 0) {
                lycon->block.given_height = 150;
                if (block->blk) block->blk->given_height = 150;
                log_debug("[ABS POS] iframe intrinsic height: 150");
            }
        }
    }

    float content_width, content_height;
    // =========================================================================
    // HORIZONTAL AXIS: CSS 2.1 §10.3.7 constraint equation
    //   left + margin-left + border-left + padding-left + width +
    //   padding-right + border-right + margin-right + right = cb_width
    // =========================================================================
    log_debug("given_width=%f, given_height=%f, width_type=%d", lycon->block.given_width, lycon->block.given_height,
              block->blk ? block->blk->given_width_type : -1);

    // Check if width uses intrinsic sizing keywords (max-content, min-content, fit-content)
    bool is_intrinsic_width = block->blk &&
        (block->blk->given_width_type == CSS_VALUE_MAX_CONTENT ||
         block->blk->given_width_type == CSS_VALUE_MIN_CONTENT ||
         block->blk->given_width_type == CSS_VALUE_FIT_CONTENT);

    // CSS 2.1 §10.3.8 / §10.6.5: Absolutely positioned REPLACED elements
    // use intrinsic dimensions for auto width/height, not the constraint equation.
    bool is_replaced = (block->tag() == HTM_TAG_IMG || block->tag() == HTM_TAG_IFRAME ||
                        block->tag() == HTM_TAG_VIDEO || block->tag() == HTM_TAG_EMBED ||
                        (block->tag() == HTM_TAG_OBJECT && block->get_attribute("data")));

    // Gather horizontal border+padding for constraint calculations
    float h_border_padding = 0;
    if (block->bound) {
        h_border_padding += block->bound->padding.left + block->bound->padding.right;
        if (block->bound->border) {
            h_border_padding += block->bound->border->width.left + block->bound->border->width.right;
        }
    }

    // CSS 2.1 §10.3.7: Detect containing block's direction for auto margin resolution
    TextDirection cb_direction = TD_LTR;
    if (containing_block->blk && containing_block->blk->direction == CSS_VALUE_RTL) {
        cb_direction = TD_RTL;
    } else if (containing_block->specified_style) {
        CssValue* dir_val = (CssValue*)style_tree_get_computed_value(
            containing_block->specified_style, CSS_PROPERTY_DIRECTION,
            containing_block->parent && containing_block->parent->is_element() ?
                ((DomElement*)containing_block->parent)->specified_style : NULL);
        if (dir_val && dir_val->type == CSS_VALUE_TYPE_KEYWORD &&
            dir_val->data.keyword == CSS_VALUE_RTL) {
            cb_direction = TD_RTL;
        }
    }

    bool has_auto_margin_left = block->bound && block->bound->margin.left_type == CSS_VALUE_AUTO;
    bool has_auto_margin_right = block->bound && block->bound->margin.right_type == CSS_VALUE_AUTO;
    bool has_width = (lycon->block.given_width >= 0 && !is_intrinsic_width);

    // First determine content_width: use CSS width if specified, otherwise calculate from constraints
    if (has_width) {
        content_width = lycon->block.given_width;
    } else if (block->position->has_left && block->position->has_right && !is_intrinsic_width && !is_replaced) {
        // CSS 2.1 §10.3.7: width is auto, both left and right specified (non-replaced only)
        // For replaced elements, §10.3.8 says use intrinsic width (via §10.3.2)
        // Auto margins are treated as 0 when width is auto
        float margin_left = has_auto_margin_left ? 0 : (block->bound ? block->bound->margin.left : 0);
        float margin_right = has_auto_margin_right ? 0 : (block->bound ? block->bound->margin.right : 0);
        float left_edge = block->position->left + margin_left;
        float right_edge = cb_width - block->position->right - margin_right;
        float border_box_width = max(right_edge - left_edge, 0.0f);
        content_width = max(border_box_width - h_border_padding, 0.0f);
        // CRITICAL: Store constraint-calculated width so finalize_block_flow knows width is fixed
        lycon->block.given_width = content_width;
        // When width is derived from constraints, auto margins become 0
        if (has_auto_margin_left && block->bound) block->bound->margin.left = 0;
        if (has_auto_margin_right && block->bound) block->bound->margin.right = 0;
        log_debug("[ABS POS] width from constraints: left_edge=%.1f, right_edge=%.1f, border_box=%.1f, content_width=%.1f (stored in given_width)",
                  left_edge, right_edge, border_box_width, content_width);
    } else if (is_intrinsic_width) {
        content_width = 0;
        log_debug("Using intrinsic sizing for absolutely positioned element: content_width=0 (shrink-to-fit)");
    } else if (block->fi && block->fi->aspect_ratio > 0 &&
               !(lycon->block.given_height >= 0) && block->blk && block->blk->given_max_height > 0) {
        // CSS Sizing Level 4: abs-pos with aspect-ratio, auto width/height, and max-height
        // Derive width from max-height * aspect-ratio
        float max_h = block->blk->given_max_height;
        float content_h = max_h;
        bool is_border_box = block->blk->box_sizing == CSS_VALUE_BORDER_BOX;
        if (is_border_box) {
            float v_bp = 0;
            if (block->bound) {
                v_bp += block->bound->padding.top + block->bound->padding.bottom;
                if (block->bound->border)
                    v_bp += block->bound->border->width.top + block->bound->border->width.bottom;
            }
            content_h -= v_bp;
            if (content_h < 0) content_h = 0;
        }
        float derived_width = content_h * block->fi->aspect_ratio;
        if (is_border_box) {
            content_width = derived_width + h_border_padding;
        } else {
            content_width = derived_width;
        }
        log_debug("[ABS POS] width from aspect-ratio + max-height: max_h=%.1f, content_h=%.1f, ratio=%.3f, content_width=%.1f",
                  max_h, content_h, block->fi->aspect_ratio, content_width);
    } else {
        // CSS 2.1 §10.3.7: width is auto, at most one of left/right specified (non-replaced)
        // Use shrink-to-fit width = min(max(preferred_minimum_width, available_width), preferred_width)
        // where preferred_minimum_width = min-content, preferred_width = max-content,
        // available_width = cb_width - margins (border-box available space)
        float margin_left = has_auto_margin_left ? 0 : (block->bound ? block->bound->margin.left : 0);
        float margin_right = has_auto_margin_right ? 0 : (block->bound ? block->bound->margin.right : 0);
        float available_width = max(cb_width - margin_left - margin_right, 0.0f);

        // Measure intrinsic widths (returns border-box sizes including element's padding+border)
        IntrinsicSizes intrinsic = measure_element_intrinsic_widths(lycon, (DomElement*)block);
        float preferred_minimum = intrinsic.min_content;  // min-content width (border-box)
        float preferred = intrinsic.max_content;          // max-content width (border-box)

        // shrink-to-fit = min(max(min_content, available), max_content) — all in border-box
        // ceil to account for fractional text measurement vs integer table/block allocation
        float shrink_to_fit = ceilf(min(max(preferred_minimum, available_width), preferred));

        // The later box-sizing adjustment (line ~558) converts border-box → content-box
        // for border-box elements. So we must set content_width appropriately:
        // - border-box: content_width = border-box value (adjustment will subtract border+padding)
        // - content-box: content_width = content-box value (no adjustment)
        bool is_border_box = block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX;
        if (is_border_box) {
            content_width = max(shrink_to_fit, 0.0f);
        } else {
            content_width = max(shrink_to_fit - h_border_padding, 0.0f);
        }

        log_debug("[ABS POS] shrink-to-fit: min_content=%.1f, max_content=%.1f, available=%.1f, "
                  "border_box=%.1f, content_width=%.1f, is_border_box=%d",
                  preferred_minimum, preferred, available_width, shrink_to_fit, content_width, is_border_box);
    }

    // CSS 2.1 §10.4: Apply min-width/max-width constraints BEFORE position calculation.
    // Per spec, min-width overrides max-width when they conflict.
    // This must happen before computing x position, because right-positioned elements
    // use the element's own width to determine x (x = cb_width - right - margin - width).
    // If we clamp after position, right/bottom-positioned elements get wrong offsets.
    content_width = adjust_min_max_width(block, content_width);

    // CSS 2.1 §10.3.7: Solve auto margins for horizontal axis
    // When left, right, and width are all NOT auto, the equation is over-constrained.
    // Auto margins absorb the remaining space; if both are auto, they split it equally (centering).
    if (has_width && block->position->has_left && block->position->has_right) {
        // For box-sizing: border-box, content_width is already the border-box width
        float used_width = content_width + h_border_padding;
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            used_width = content_width;  // already includes padding+border
        }
        float remaining = cb_width - block->position->left - block->position->right - used_width;
        if (has_auto_margin_left && has_auto_margin_right) {
            // CSS 2.1 §10.3.7: Both margins auto → equal values, unless negative.
            // If negative: LTR → margin-left=0, solve margin-right;
            //              RTL → margin-right=0, solve margin-left.
            float each = remaining / 2.0f;
            if (each < 0) {
                if (cb_direction == TD_RTL) {
                    if (block->bound) { block->bound->margin.right = 0; block->bound->margin.left = remaining; }
                    log_debug("[ABS POS] auto margins negative (RTL): margin-left=%.1f, margin-right=0", remaining);
                } else {
                    if (block->bound) { block->bound->margin.left = 0; block->bound->margin.right = remaining; }
                    log_debug("[ABS POS] auto margins negative (LTR): margin-left=0, margin-right=%.1f", remaining);
                }
            } else {
                if (block->bound) {
                    block->bound->margin.left = each;
                    block->bound->margin.right = each;
                }
                log_debug("[ABS POS] auto margin centering horizontal: remaining=%.1f, each=%.1f", remaining, each);
            }
        } else if (has_auto_margin_left) {
            // CSS 2.1 §10.3.7: Exactly one auto value → follows from equality
            float margin_right = block->bound ? block->bound->margin.right : 0;
            float auto_margin = remaining - margin_right;
            if (block->bound) block->bound->margin.left = auto_margin;
            log_debug("[ABS POS] auto margin-left=%.1f", auto_margin);
        } else if (has_auto_margin_right) {
            // CSS 2.1 §10.3.7: Exactly one auto value → follows from equality
            float margin_left = block->bound ? block->bound->margin.left : 0;
            float auto_margin = remaining - margin_left;
            if (block->bound) block->bound->margin.right = auto_margin;
            log_debug("[ABS POS] auto margin-right=%.1f", auto_margin);
        }
    } else {
        // When not all three (left, right, width) are specified, auto margins become 0
        if (has_auto_margin_left && block->bound) block->bound->margin.left = 0;
        if (has_auto_margin_right && block->bound) block->bound->margin.right = 0;
    }

    // Now determine x position (relative to padding box, then add border offset)
    // For right-positioning, subtract the full border-box width (content + padding + border)
    // Note: with box-sizing: border-box, content_width already IS the border-box width
    // (CSS width = border-box width), so don't add h_border_padding again.
    float h_border_box_width = content_width + h_border_padding;
    if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
        h_border_box_width = content_width;  // CSS width is already the border-box width
    }
    if (block->position->has_left) {
        block->x = border_offset_x + block->position->left + (block->bound ? block->bound->margin.left : 0);
    } else if (block->position->has_right) {
        block->x = border_offset_x + cb_width - block->position->right - (block->bound ? block->bound->margin.right : 0) - h_border_box_width;
    } else {
        // neither left nor right specified - use static position (with margin offset)
        // Note: this is a preliminary value; layout_abs_block will override with
        // the correct static position (including RTL adjustment) after this call.
        block->x = border_offset_x + (block->bound ? block->bound->margin.left : 0);
    }
    assert(content_width >= 0);

    // =========================================================================
    // VERTICAL AXIS: CSS 2.1 §10.6.4 constraint equation
    //   top + margin-top + border-top + padding-top + height +
    //   padding-bottom + border-bottom + margin-bottom + bottom = cb_height
    // =========================================================================
    bool has_auto_margin_top = block->bound && block->bound->margin.top_type == CSS_VALUE_AUTO;
    bool has_auto_margin_bottom = block->bound && block->bound->margin.bottom_type == CSS_VALUE_AUTO;

    // Gather vertical border+padding for constraint calculations
    float v_border_padding = 0;
    if (block->bound) {
        v_border_padding += block->bound->padding.top + block->bound->padding.bottom;
        if (block->bound->border) {
            v_border_padding += block->bound->border->width.top + block->bound->border->width.bottom;
        }
    }

    log_debug("[ABS POS] height calc: given_height=%.1f, has_top=%d, has_bottom=%d, cb_height=%.1f",
              lycon->block.given_height, block->position->has_top, block->position->has_bottom, cb_height);
    bool has_height = (lycon->block.given_height >= 0);

    if (has_height) {
        content_height = lycon->block.given_height;
        log_debug("[ABS POS] using explicit height: %.1f", content_height);
    } else if (block->position->has_top && block->position->has_bottom && !is_replaced) {
        // CSS 2.1 §10.6.4: height is auto, both top and bottom specified
        // Auto margins are treated as 0 when height is auto
        float margin_top = has_auto_margin_top ? 0 : (block->bound ? block->bound->margin.top : 0);
        float margin_bottom = has_auto_margin_bottom ? 0 : (block->bound ? block->bound->margin.bottom : 0);
        float top_edge = block->position->top + margin_top;
        float bottom_edge = cb_height - block->position->bottom - margin_bottom;
        float border_box_height = max(bottom_edge - top_edge, 0.0f);
        content_height = max(border_box_height - v_border_padding, 0.0f);
        // CRITICAL: Store constraint-calculated height so finalize_block_flow knows height is fixed
        lycon->block.given_height = content_height;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->given_height = content_height;
        // When height is derived from constraints, auto margins become 0
        if (has_auto_margin_top && block->bound) block->bound->margin.top = 0;
        if (has_auto_margin_bottom && block->bound) block->bound->margin.bottom = 0;
        log_debug("[ABS POS] height from constraints: top_edge=%.1f, bottom_edge=%.1f, border_box=%.1f, content_height=%.1f (stored in given_height)",
                  top_edge, bottom_edge, border_box_height, content_height);
    } else {
        // shrink-to-fit: height will be determined by content after layout
        content_height = 0;
        log_debug("[ABS POS] using auto height (shrink-to-fit)");
    }

    // CSS 2.1 §10.7: Apply min-height/max-height constraints BEFORE position calculation.
    // Same rationale as horizontal: bottom-positioned elements need the clamped height.
    content_height = adjust_min_max_height(block, content_height);

    // CSS 2.1 §10.6.4: Solve auto margins for vertical axis
    // When top, bottom, and height are all NOT auto, auto margins absorb remaining space.
    if (has_height && block->position->has_top && block->position->has_bottom) {
        // For box-sizing: border-box, content_height is already the border-box height
        float used_height = content_height + v_border_padding;
        if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            used_height = content_height;  // already includes padding+border
        }
        float remaining = cb_height - block->position->top - block->position->bottom - used_height;
        if (has_auto_margin_top && has_auto_margin_bottom) {
            // Both margins auto → center vertically
            float each = max(remaining / 2.0f, 0.0f);
            if (block->bound) {
                block->bound->margin.top = each;
                block->bound->margin.bottom = each;
            }
            log_debug("[ABS POS] auto margin centering vertical: remaining=%.1f, each=%.1f", remaining, each);
        } else if (has_auto_margin_top) {
            float margin_bottom = block->bound ? block->bound->margin.bottom : 0;
            float auto_margin = max(remaining - margin_bottom, 0.0f);
            if (block->bound) block->bound->margin.top = auto_margin;
            log_debug("[ABS POS] auto margin-top=%.1f", auto_margin);
        } else if (has_auto_margin_bottom) {
            float margin_top = block->bound ? block->bound->margin.top : 0;
            float auto_margin = max(remaining - margin_top, 0.0f);
            if (block->bound) block->bound->margin.bottom = auto_margin;
            log_debug("[ABS POS] auto margin-bottom=%.1f", auto_margin);
        }
    } else {
        // When not all three (top, bottom, height) are specified, auto margins become 0
        if (has_auto_margin_top && block->bound) block->bound->margin.top = 0;
        if (has_auto_margin_bottom && block->bound) block->bound->margin.bottom = 0;
    }

    // Now determine y position (relative to padding box, then add border offset)
    // CRITICAL: For bottom positioning, we need the border-box height (including padding/border)
    bool is_border_box = block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX;
    float border_box_height = content_height;
    if (!is_border_box && block->bound) {
        border_box_height += block->bound->padding.top + block->bound->padding.bottom;
        if (block->bound->border) {
            border_box_height += block->bound->border->width.top + block->bound->border->width.bottom;
        }
    }

    if (block->position->has_top) {
        block->y = border_offset_y + block->position->top + (block->bound ? block->bound->margin.top : 0);
    } else if (block->position->has_bottom) {
        // Use border_box_height for bottom positioning to account for padding and border
        block->y = border_offset_y + cb_height - block->position->bottom - (block->bound ? block->bound->margin.bottom : 0) - border_box_height;
        log_debug("[ABS POS] bottom positioning: y=%.1f (cb_height=%.1f, bottom=%.1f, border_box_height=%.1f, is_border_box=%d)",
                  block->y, cb_height, block->position->bottom, border_box_height, is_border_box);
    } else {
        // neither top nor bottom specified - use static position (with margin offset)
        block->y = border_offset_y + (block->bound ? block->bound->margin.top : 0);
    }
    assert(content_height >= 0);

    if (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
        // for border-box, CSS width includes padding and border, so subtract them to get content width
        content_width = adjust_min_max_width(block, content_width);
        if (block->bound) content_width = adjust_border_padding_width(block, content_width);
        content_height = adjust_min_max_height(block, content_height);
        if (block->bound) content_height = adjust_border_padding_height(block, content_height);
    } else {
        // for content-box (default), CSS width IS the content width, don't subtract padding/border
        if (block->bound) content_width = adjust_min_max_width(block, content_width);
        if (block->bound) content_height = adjust_min_max_height(block, content_height);
    }
    lycon->block.content_width = content_width;  lycon->block.content_height = content_height;

    if (block->bound) {
        block->width = content_width + block->bound->padding.left + block->bound->padding.right +
            (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0);
        block->height = content_height + block->bound->padding.top + block->bound->padding.bottom +
            (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0);
    } else {
        // no change to block->x, block->y, lycon->line.advance_x, lycon->block.advance_y
        block->width = content_width;  block->height = content_height;
    }

    log_debug("calculated x,y,wd,hg, content_width, content_height: (%f, %f) size (%f, %f), content (%f, %f) within containing block (%f, %f) size (%f, %f)",
        block->x, block->y, block->width, block->height, lycon->block.content_width, lycon->block.content_height, cb_x, cb_y, cb_width, cb_height);
}

// Re-resolve percentage-based vertical dimensions for absolutely positioned children
// after the containing block's auto height has been finalized.
//
// CSS 2.1 §10.5: For absolutely positioned elements, percentage heights resolve
// against the containing block's used (final) height. When the containing block has
// height:auto, its used height isn't known until all in-flow children are laid out.
// Abs children are laid out eagerly in DOM order, so their percentage heights
// initially resolve against 0. This function corrects them after the final height is known.
void re_resolve_abs_children_vertical(ViewBlock* containing_block) {
    if (!containing_block->position || !containing_block->position->first_abs_child) return;

    // Compute containing block's padding box height (CSS 2.1 §10.1)
    float cb_height = containing_block->height;
    if (containing_block->bound && containing_block->bound->border) {
        cb_height -= (containing_block->bound->border->width.top + containing_block->bound->border->width.bottom);
    }
    if (cb_height <= 0) return;

    ViewBlock* child = containing_block->position->first_abs_child;
    while (child) {
        bool changed = false;

        // Re-resolve percentage height
        if (child->blk && !isnan(child->blk->given_height_percent) && child->blk->given_height <= 0) {
            float new_given_height = child->blk->given_height_percent * cb_height / 100.0f;
            new_given_height = adjust_min_max_height(child, new_given_height);
            child->blk->given_height = new_given_height;

            float content_height = new_given_height;
            bool is_border_box = child->blk->box_sizing == CSS_VALUE_BORDER_BOX;
            if (is_border_box && child->bound) {
                content_height = adjust_border_padding_height(child, content_height);
            }

            if (child->bound) {
                child->height = content_height + child->bound->padding.top + child->bound->padding.bottom +
                    (child->bound->border ? child->bound->border->width.top + child->bound->border->width.bottom : 0);
            } else {
                child->height = content_height;
            }
            changed = true;
            log_debug("[ABS RE-RESOLVE] height for %s: %.1f%% of %.1f = %.1f, block->height=%.1f",
                child->node_name(), child->blk->given_height_percent, cb_height, new_given_height, child->height);
        }

        // Re-resolve percentage top
        if (child->position && child->position->has_top && !isnan(child->position->top_percent)) {
            float old_top = child->position->top;
            child->position->top = child->position->top_percent * cb_height / 100.0f;
            if (child->position->top != old_top) {
                float border_offset_y = 0;
                if (containing_block->bound && containing_block->bound->border) {
                    border_offset_y = containing_block->bound->border->width.top;
                }
                child->y = border_offset_y + child->position->top + (child->bound ? child->bound->margin.top : 0);
                changed = true;
                log_debug("[ABS RE-RESOLVE] top for %s: %.1f%% of %.1f = %.1f, y=%.1f",
                    child->node_name(), child->position->top_percent, cb_height, child->position->top, child->y);
            }
        }

        // Re-resolve percentage bottom
        if (child->position && child->position->has_bottom && !isnan(child->position->bottom_percent)) {
            child->position->bottom = child->position->bottom_percent * cb_height / 100.0f;
        }

        // If bottom is specified but not top, recompute y from bottom edge.
        // This must be unconditional: this function runs after an auto-height
        // containing block is finalized, so cb_height changed for ALL children,
        // not just those with percentage values.
        if (child->position && child->position->has_bottom && !child->position->has_top) {
            float border_offset_y = 0;
            if (containing_block->bound && containing_block->bound->border) {
                border_offset_y = containing_block->bound->border->width.top;
            }
            child->y = border_offset_y + cb_height - child->position->bottom
                - (child->bound ? child->bound->margin.bottom : 0) - child->height;
            log_debug("[ABS RE-RESOLVE] bottom-positioned y for %s: y=%.1f", child->node_name(), child->y);
        }

        child = child->position ? child->position->next_abs_sibling : nullptr;
    }
}

void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line) {
    log_debug("layout_abs_block");  log_enter();
    log_debug("block init position (%s): x=%f, y=%f, pa_block.advance_y=%f", elmt->node_name(), block->x, block->y, pa_block->advance_y);

    // guard against deeply nested positioned elements (e.g., 200 nested position:fixed flex divs)
    // layout_abs_block bypasses layout_flow_node so its depth guard doesn't apply here
    static const int MAX_ABS_DEPTH = 300;
    lycon->depth++;
    if (lycon->depth >= MAX_ABS_DEPTH) {
        log_error("layout_abs_block: depth %d exceeded, skipping %s", MAX_ABS_DEPTH, elmt->source_loc());
        lycon->depth--;
        log_leave();
        return;
    }

    // find containing block
    ViewBlock* cb = find_containing_block(block, block->position->position);
    if (!cb) { log_error("Missing containing block");  lycon->depth--;  log_leave();  return; }
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
        // Ensure the newly appended block is the list tail (no dangling next pointer)
        block->position->next_abs_sibling = nullptr;
    } else {
        log_error("Containing block has no position property");
    }

    // calculate position based on offset properties and containing block
    calculate_absolute_position(lycon, block, cb);

    // Load image for IMG elements - same as layout_block does for regular flow
    uintptr_t elmt_name = block->tag();
    if (elmt_name == HTM_TAG_IMG) {
        log_debug("[ABS IMG] Loading image for absolutely positioned IMG element");
        const char *value = block->get_attribute("src");
        if (value) {
            size_t value_len = strlen(value);
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, value, value_len);
            log_debug("[ABS IMG] image src: %s", src->str);
            if (!block->embed) {
                block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
            }
            block->embed->img = load_image(lycon->ui_context, src->str);
            strbuf_free(src);
            if (!block->embed->img) {
                log_debug("[ABS IMG] Failed to load image");
            }
        }
        if (block->embed && block->embed->img) {
            ImageSurface* img = block->embed->img;
            // Image intrinsic dimensions are in CSS logical pixels
            float w = img->width;
            float h = img->height;
            log_debug("[ABS IMG] image intrinsic dims: %.1f x %.1f, given: %.1f x %.1f",
                      w, h, lycon->block.given_width, lycon->block.given_height);

            // Adjust dimensions based on CSS constraints
            if (lycon->block.given_width < 0 && lycon->block.given_height < 0) {
                // Neither width nor height specified - use intrinsic dimensions
                // But respect max-width if set
                float max_w = block->blk ? block->blk->given_max_width : -1;
                if (max_w >= 0 && w > max_w) {
                    lycon->block.given_width = max_w;
                    lycon->block.given_height = max_w * h / w;
                } else {
                    lycon->block.given_width = w;
                    lycon->block.given_height = h;
                }
            } else if (lycon->block.given_width >= 0 && lycon->block.given_height < 0) {
                // Width specified, scale height to maintain aspect ratio
                lycon->block.given_height = lycon->block.given_width * h / w;
            } else if (lycon->block.given_height >= 0 && lycon->block.given_width < 0) {
                // Height specified, scale width to maintain aspect ratio
                lycon->block.given_width = lycon->block.given_height * w / h;
            }
            // else both are specified, use them as-is

            // Update block dimensions and persist the image-derived height into
            // block->blk so the auto-sizing check later (which reads blk->given_height
            // instead of lycon->block.given_height) won't overwrite it.
            block->width = lycon->block.given_width;
            block->height = lycon->block.given_height;
            lycon->block.content_width = lycon->block.given_width;
            lycon->block.content_height = lycon->block.given_height;
            if (block->blk) {
                block->blk->given_height = lycon->block.given_height;
                block->blk->given_width = lycon->block.given_width;
            }

            if (img->format == IMAGE_FORMAT_SVG) {
                img->max_render_width = max(lycon->block.given_width, img->max_render_width);
            }
            log_debug("[ABS IMG] final dimensions: %.1f x %.1f", block->width, block->height);

            // Recalculate position for right/bottom-positioned replaced elements.
            // calculate_absolute_position computed x/y using the pre-image width/height
            // (shrink-to-fit or 0 for auto), but the IMG sizing code may have changed
            // the dimensions via aspect ratio. Re-derive x/y from the new block size.
            if (block->position->has_right && !block->position->has_left) {
                float cb_border_left = (cb->bound && cb->bound->border) ? cb->bound->border->width.left : 0;
                float cb_border_right = (cb->bound && cb->bound->border) ? cb->bound->border->width.right : 0;
                float cb_padding_width = cb->width - cb_border_left - cb_border_right;
                float margin_right = (block->bound) ? block->bound->margin.right : 0;
                block->x = cb_border_left + cb_padding_width - block->position->right - margin_right - block->width;
                log_debug("[ABS IMG] right-positioned X recalc: x=%.1f (cb_pad_w=%.1f, right=%.1f, width=%.1f)",
                          block->x, cb_padding_width, block->position->right, block->width);
            }
            if (block->position->has_bottom && !block->position->has_top) {
                float cb_border_top = (cb->bound && cb->bound->border) ? cb->bound->border->width.top : 0;
                float cb_border_bottom = (cb->bound && cb->bound->border) ? cb->bound->border->width.bottom : 0;
                float cb_padding_height = cb->height - cb_border_top - cb_border_bottom;
                float margin_bottom = (block->bound) ? block->bound->margin.bottom : 0;
                block->y = cb_border_top + cb_padding_height - block->position->bottom - margin_bottom - block->height;
                log_debug("[ABS IMG] bottom-positioned Y recalc: y=%.1f (cb_pad_h=%.1f, bottom=%.1f, height=%.1f)",
                          block->y, cb_padding_height, block->position->bottom, block->height);
            }

            // CSS 2.1 §10.6.5: Re-resolve vertical auto margins for replaced elements
            // now that we know the intrinsic height. calculate_absolute_position
            // skipped this because has_height was false (given_height=-1 for images).
            if (block->position->has_top && block->position->has_bottom && block->bound) {
                bool has_auto_mt = block->bound->margin.top_type == CSS_VALUE_AUTO;
                bool has_auto_mb = block->bound->margin.bottom_type == CSS_VALUE_AUTO;
                if (has_auto_mt || has_auto_mb) {
                    float cb_border_top = (cb->bound && cb->bound->border) ? cb->bound->border->width.top : 0;
                    float cb_border_bottom = (cb->bound && cb->bound->border) ? cb->bound->border->width.bottom : 0;
                    float cb_pad_height = cb->height - cb_border_top - cb_border_bottom;
                    float v_bp = block->bound->padding.top + block->bound->padding.bottom;
                    if (block->bound->border) {
                        v_bp += block->bound->border->width.top + block->bound->border->width.bottom;
                    }
                    float used_height = block->height + v_bp;
                    float remaining = cb_pad_height - block->position->top - block->position->bottom - used_height;
                    if (has_auto_mt && has_auto_mb) {
                        float each = remaining / 2.0f;
                        block->bound->margin.top = each;
                        block->bound->margin.bottom = each;
                        log_debug("[ABS IMG] vertical auto margin centering: remaining=%.1f, each=%.1f", remaining, each);
                    } else if (has_auto_mt) {
                        float auto_margin = remaining - block->bound->margin.bottom;
                        block->bound->margin.top = auto_margin;
                        log_debug("[ABS IMG] auto margin-top=%.1f", auto_margin);
                    } else {
                        float auto_margin = remaining - block->bound->margin.top;
                        block->bound->margin.bottom = auto_margin;
                        log_debug("[ABS IMG] auto margin-bottom=%.1f", auto_margin);
                    }
                    // Recalculate y with the resolved margins
                    block->y = cb_border_top + block->position->top + block->bound->margin.top;
                    log_debug("[ABS IMG] y after margin resolution: %.1f", block->y);
                }
            }

            // CSS 2.1 §10.3.8: Re-resolve horizontal auto margins for replaced elements
            if (block->position->has_left && block->position->has_right && block->bound) {
                bool has_auto_ml = block->bound->margin.left_type == CSS_VALUE_AUTO;
                bool has_auto_mr = block->bound->margin.right_type == CSS_VALUE_AUTO;
                if (has_auto_ml || has_auto_mr) {
                    float cb_border_left = (cb->bound && cb->bound->border) ? cb->bound->border->width.left : 0;
                    float cb_border_right = (cb->bound && cb->bound->border) ? cb->bound->border->width.right : 0;
                    float cb_pad_width = cb->width - cb_border_left - cb_border_right;
                    float h_bp = block->bound->padding.left + block->bound->padding.right;
                    if (block->bound->border) {
                        h_bp += block->bound->border->width.left + block->bound->border->width.right;
                    }
                    float used_width = block->width + h_bp;
                    float remaining = cb_pad_width - block->position->left - block->position->right - used_width;
                    // Determine containing block direction for negative margin handling
                    TextDirection cb_dir = TD_LTR;
                    if (cb->blk && cb->blk->direction == CSS_VALUE_RTL) {
                        cb_dir = TD_RTL;
                    } else if (cb->specified_style) {
                        CssValue* dir_val = (CssValue*)style_tree_get_computed_value(
                            cb->specified_style, CSS_PROPERTY_DIRECTION,
                            cb->parent && cb->parent->is_element() ?
                                ((DomElement*)cb->parent)->specified_style : NULL);
                        if (dir_val && dir_val->type == CSS_VALUE_TYPE_KEYWORD &&
                            dir_val->data.keyword == CSS_VALUE_RTL) {
                            cb_dir = TD_RTL;
                        }
                    }
                    if (has_auto_ml && has_auto_mr) {
                        // CSS 2.1 §10.3.8: Both auto → equal, unless negative
                        float each = remaining / 2.0f;
                        if (each < 0) {
                            if (cb_dir == TD_RTL) {
                                block->bound->margin.right = 0;
                                block->bound->margin.left = remaining;
                            } else {
                                block->bound->margin.left = 0;
                                block->bound->margin.right = remaining;
                            }
                            log_debug("[ABS IMG] horizontal auto margins negative (%s): ml=%.1f, mr=%.1f",
                                      cb_dir == TD_RTL ? "RTL" : "LTR",
                                      block->bound->margin.left, block->bound->margin.right);
                        } else {
                            block->bound->margin.left = each;
                            block->bound->margin.right = each;
                            log_debug("[ABS IMG] horizontal auto margin centering: remaining=%.1f, each=%.1f", remaining, each);
                        }
                    } else if (has_auto_ml) {
                        float auto_margin = remaining - block->bound->margin.right;
                        block->bound->margin.left = auto_margin;
                        log_debug("[ABS IMG] auto margin-left=%.1f", auto_margin);
                    } else {
                        float auto_margin = remaining - block->bound->margin.left;
                        block->bound->margin.right = auto_margin;
                        log_debug("[ABS IMG] auto margin-right=%.1f", auto_margin);
                    }
                    block->x = cb_border_left + block->position->left + block->bound->margin.left;
                    log_debug("[ABS IMG] x after margin resolution: %.1f", block->x);
                }
            }
        } else {
            // Failed to load image - use placeholder
            if (lycon->block.given_width <= 0) lycon->block.given_width = 40;
            if (lycon->block.given_height <= 0) lycon->block.given_height = 30;
            block->width = lycon->block.given_width;
            block->height = lycon->block.given_height;
        }
    }

    // CSS 2.2 Section 10.6.4: For absolutely positioned elements without explicit top/bottom,
    // use the "static position" - where the element would be in normal flow
    // The static position is relative to the parent element's content area, but we need
    // to express it relative to the containing block's padding box.

    // Calculate offset from containing block to parent element
    // Walk from parent up to containing block, accumulating positions
    // Note: pa_line->left and pa_block->advance_y are already relative to the parent's
    // content area (they include padding/border offsets), so we only need to add
    // the parent's position relative to the containing block.
    //
    // IMPORTANT: For positioned ancestors (absolute/fixed), their x/y coordinates are
    // relative to their own containing block (not their DOM parent). When we encounter
    // such an ancestor, we must jump to its containing block rather than continuing
    // the DOM parent chain — the intermediate non-positioned ancestors are already
    // accounted for in the positioned ancestor's coordinates.
    float parent_to_cb_offset_x = 0, parent_to_cb_offset_y = 0;
    ViewElement* parent = block->parent_view();

    // Walk past inline ancestors to find the nearest block ancestor.
    // Inline spans don't have their own block coordinate space; pa_line->advance_x
    // and pa_block->advance_y are already relative to the nearest block ancestor's
    // content area.
    ViewElement* walk_start = parent;
    while (walk_start && !walk_start->is_block()) {
        walk_start = walk_start->parent_view();
    }

    if (walk_start && walk_start->is_block()) {
        ViewBlock* p = (ViewBlock*)walk_start;
        // Walk from nearest block ancestor to containing block, accumulating offsets
        while (p && p != cb) {
            parent_to_cb_offset_x += p->x;
            parent_to_cb_offset_y += p->y;
            log_debug("[STATIC POS] Adding parent %s offset: (%f, %f)", p->node_name(), p->x, p->y);

            // Check if this ancestor is positioned — if so, its coordinates are
            // relative to its own CB, not its DOM parent. Jump to that CB.
            if (p->position && p->position->position == CSS_VALUE_FIXED) {
                // Fixed: coordinates are viewport-relative, done
                log_debug("[STATIC POS] Encountered fixed ancestor %s, stopping walk", p->node_name());
                break;
            }
            if (p->position && p->position->position == CSS_VALUE_ABSOLUTE) {
                // Absolute: coordinates are relative to p's containing block.
                // Find p's CB and jump there, skipping intermediate DOM ancestors.
                ViewElement* ancestor = p->parent_view();
                ViewBlock* p_cb = nullptr;
                while (ancestor) {
                    if (ancestor->is_block()) {
                        ViewBlock* ab = (ViewBlock*)ancestor;
                        if (ab->position && ab->position->position != CSS_VALUE_STATIC) {
                            p_cb = ab;
                            break;
                        }
                    }
                    ancestor = ancestor->parent_view();
                }
                if (p_cb) {
                    log_debug("[STATIC POS] Absolute ancestor %s: jumping to its CB %s", p->node_name(), p_cb->node_name());
                    p = p_cb;
                    continue;  // Continue walk from p's containing block
                }
                // No positioned ancestor: p's coordinates are relative to ICB (root)
                log_debug("[STATIC POS] Absolute ancestor %s: CB is ICB, stopping walk", p->node_name());
                break;
            }

            // Normal flow or relative: coordinates are relative to DOM parent, continue walk
            // Skip inline ancestors to find the next block ancestor
            ViewElement* gp = p->parent_view();
            while (gp && !gp->is_block()) {
                gp = gp->parent_view();
            }
            if (gp && gp->is_block()) {
                p = (ViewBlock*)gp;
            } else {
                break;
            }
        }
    }

    // CSS 2.1 §9.3.1: For fixed positioning, the containing block is the viewport (ICB),
    // not the root element. The walk above stops AT the root element (cb) without
    // including its position. Since the root element's x/y includes its margin offset
    // from the viewport origin, we must add cb->x/y to convert from root-border-box-
    // relative coordinates to viewport coordinates.
    if (block->position->position == CSS_VALUE_FIXED && cb) {
        parent_to_cb_offset_x += cb->x;
        parent_to_cb_offset_y += cb->y;
        log_debug("[STATIC POS] Fixed: added CB root offset (%f, %f)", cb->x, cb->y);
    }

    log_debug("[STATIC POS] Total parent-to-CB offset: (%f, %f)", parent_to_cb_offset_x, parent_to_cb_offset_y);

    // CSS 2.1 §10.3.7: Detect direction of the static-position containing block.
    // The direction determines whether the static position is for 'left' (LTR) or 'right' (RTL).
    TextDirection static_direction = TD_LTR;
    if (parent && parent->is_element()) {
        DomElement* parent_elem = (DomElement*)parent;
        if (parent_elem->blk && parent_elem->blk->direction == CSS_VALUE_RTL) {
            static_direction = TD_RTL;
        } else if (parent_elem->specified_style) {
            CssValue* dir_val = (CssValue*)style_tree_get_computed_value(
                parent_elem->specified_style, CSS_PROPERTY_DIRECTION,
                parent_elem->parent && parent_elem->parent->is_element() ?
                    ((DomElement*)parent_elem->parent)->specified_style : NULL);
            if (dir_val && dir_val->type == CSS_VALUE_TYPE_KEYWORD &&
                dir_val->data.keyword == CSS_VALUE_RTL) {
                static_direction = TD_RTL;
            }
        }
    }
    log_debug("[STATIC POS] static-position direction: %s", static_direction == TD_RTL ? "RTL" : "LTR");

    if (!block->position->has_top && !block->position->has_bottom) {
        // Calculate static position: pa_block->advance_y is relative to parent's content area
        // Add offset to convert to containing block coordinates
        float static_y = parent_to_cb_offset_y + pa_block->advance_y;
        // Add margin.top (if not already included)
        if (block->bound && block->bound->margin.top > 0) {
            static_y += block->bound->margin.top;
        }
        log_debug("[STATIC POS] Using static Y position: %.1f (pa_block->advance_y=%.1f, offset=%.1f)",
                  static_y, pa_block->advance_y, parent_to_cb_offset_y);
        block->y = static_y;
    }
    // Similarly for X when neither left nor right specified
    if (!block->position->has_left && !block->position->has_right) {
        // CSS 2.1 §10.3.7: Use the static position — where the element would
        // be in normal flow.
        // For originally-inline elements (blockified by §9.7), the static X is
        // the inline cursor (advance_x), adjusted for float avoidance and text-align.
        // For originally-block elements, the static X is the left edge of the
        // line (pa_line->left), since block elements start on a new line.
        bool was_inline = false;
        if (elmt->is_element()) {
            DomElement* elem = elmt->as_element();
            was_inline = was_specified_inline(elem);
        }
        float line_x = was_inline ? pa_line->advance_x : pa_line->left;

        // CSS 2.1 §10.3.7: For inline-level elements at line start, the static
        // position must account for float avoidance and text-align. The hypothetical
        // inline box would be placed in the available space after floats, aligned
        // according to text-align.
        if (was_inline && line_x <= pa_line->left + 0.01f) {
            BlockContext* bfc = block_context_find_bfc(pa_block);
            if (bfc) {
                float static_y_bfc = pa_block->advance_y + pa_block->bfc_offset_y;
                float lh = pa_block->line_height > 0 ? pa_block->line_height : 16.0f;
                FloatAvailableSpace space = block_context_space_at_y(bfc, static_y_bfc, lh);

                // Convert from BFC coordinates to local coordinates
                float avail_left = space.left - pa_block->bfc_offset_x;
                float avail_right = space.right - pa_block->bfc_offset_x;

                // Clamp to content area
                avail_left = fmax(avail_left, pa_line->left);
                avail_right = fmin(avail_right, pa_line->right);

                // Apply text-align to determine position within available space
                CssEnum ta = pa_block->text_align;
                if (ta == CSS_VALUE_CENTER) {
                    line_x = (avail_left + avail_right) / 2.0f;
                } else if ((ta == CSS_VALUE_RIGHT && static_direction == TD_LTR) ||
                           (ta == CSS_VALUE_LEFT && static_direction == TD_RTL) ||
                           (ta == CSS_VALUE_END)) {
                    // End-side alignment: right for LTR, left for RTL
                    line_x = (static_direction == TD_LTR) ? avail_right : avail_left;
                } else {
                    // Start-side alignment (left, start, justify, default):
                    // left for LTR, right for RTL
                    line_x = (static_direction == TD_LTR) ? avail_left : avail_right;
                }
                log_debug("[STATIC POS] Float+align adjusted line_x=%.1f (avail=[%.1f,%.1f], text-align=%d)",
                          line_x, avail_left, avail_right, (int)ta); // INT_CAST_OK: enum for log
            }
        }

        if (static_direction == TD_RTL) {
            float static_x = parent_to_cb_offset_x + line_x;
            if (block->bound && block->bound->margin.left > 0) {
                static_x += block->bound->margin.left;
            }
            block->x = static_x;
            log_debug("[STATIC POS] RTL: temporary X position: %.1f (will adjust after width known)", block->x);
        } else {
            // CSS 2.1 §10.3.7: When direction is LTR, set 'left' to the static position.
            float static_x = parent_to_cb_offset_x + line_x;
            if (block->bound && block->bound->margin.left > 0) {
                static_x += block->bound->margin.left;
            }
            log_debug("[STATIC POS] LTR: Using static X position: %.1f (line_x=%.1f, offset=%.1f, was_inline=%d)",
                      static_x, line_x, parent_to_cb_offset_x, was_inline);
            block->x = static_x;
        }
    }

    // Absolutely positioned elements establish a new BFC
    // CSS 2.2 Section 9.4.1: "Absolutely positioned elements ... establish new BFCs"
    lycon->block.is_bfc_root = true;
    lycon->block.establishing_element = block;
    block_context_reset_floats(&lycon->block);
    log_debug("[ABS BFC] Established new BFC for absolutely positioned element %s", block->node_name());

    // Check if width uses intrinsic sizing keywords (max-content, min-content, fit-content)
    bool is_intrinsic_width = block->blk &&
        (block->blk->given_width_type == CSS_VALUE_MAX_CONTENT ||
         block->blk->given_width_type == CSS_VALUE_MIN_CONTENT ||
         block->blk->given_width_type == CSS_VALUE_FIT_CONTENT);

    // Set available space for intrinsic sizing if needed
    if (is_intrinsic_width) {
        if (block->blk->given_width_type == CSS_VALUE_MAX_CONTENT) {
            lycon->available_space = AvailableSpace::make_max_content();
            log_debug("[ABS] Setting max-content intrinsic sizing mode");
        } else if (block->blk->given_width_type == CSS_VALUE_MIN_CONTENT) {
            lycon->available_space = AvailableSpace::make_min_content();
            log_debug("[ABS] Setting min-content intrinsic sizing mode");
        } else {
            // fit-content uses max-content with clamping (for now, treat as max-content)
            lycon->available_space = AvailableSpace::make_max_content();
            log_debug("[ABS] Setting fit-content (max-content) intrinsic sizing mode");
        }
    }

    // setup inline context
    setup_inline(lycon, block);

    // CSS 2.1 §10.3.7: Save the pre-layout width (border-box) computed by
    // calculate_absolute_position via shrink-to-fit. Used below to prevent
    // the post-layout auto-sizing from shrinking when children with percentage
    // widths create a circular dependency (percentage resolves against
    // shrink-to-fit width → child becomes smaller → auto-sizing shrinks container).
    float pre_layout_width = block->width;

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

    // BFC height expansion to contain floats
    // CSS 2.2 Section 10.6.7: For BFC roots (including position:absolute),
    // the heights of floating descendants are taken into account
    float max_float_bottom = 0;
    if (lycon->block.is_bfc_root || lycon->block.establishing_element == block) {
        // Find the maximum bottom of all floated children (including margins)
        for (FloatBox* fb = lycon->block.left_floats; fb; fb = fb->next) {
            log_debug("[ABS BFC] left float margin_box_bottom=%.1f", fb->margin_box_bottom);
            if (fb->margin_box_bottom > max_float_bottom) {
                max_float_bottom = fb->margin_box_bottom;
            }
        }
        for (FloatBox* fb = lycon->block.right_floats; fb; fb = fb->next) {
            log_debug("[ABS BFC] right float margin_box_bottom=%.1f", fb->margin_box_bottom);
            if (fb->margin_box_bottom > max_float_bottom) {
                max_float_bottom = fb->margin_box_bottom;
            }
        }
        // Also check lowest_float_bottom which may be set during child layout
        if (lycon->block.lowest_float_bottom > max_float_bottom) {
            max_float_bottom = lycon->block.lowest_float_bottom;
        }
        log_debug("[ABS BFC] max_float_bottom=%.1f for %s", max_float_bottom, block->node_name());
    }

    // adjust block width and height based on content
    log_debug("block position: x=%f, y=%f, width=%f, height=%f, advance_y=%f, max_width=%f, given_height=%f, has_top=%d, has_bottom=%d",
        block->x, block->y, block->width, block->height, lycon->block.advance_y, lycon->block.max_width,
        lycon->block.given_height, block->position->has_top, block->position->has_bottom);

    // CRITICAL: Check if this is a flex/grid container that already calculated its dimensions
    bool is_flex_container = (block->display.inner == CSS_VALUE_FLEX);
    bool is_grid_container = (block->display.inner == CSS_VALUE_GRID);
    // Flex/grid containers calculate their own width via shrink-to-fit in layout_flex_content/
    // layout_grid_content. Trust the flex-calculated width if the container has children or
    // border/padding (the flex algorithm handles both cases now that layout_block_content
    // dispatches to flex even for empty containers).
    bool has_flex_calculated_width = is_flex_container && block->width > 0 &&
        (block->first_child != nullptr || (block->bound && (
            block->bound->padding.left + block->bound->padding.right +
            (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0)
        ) > 0));
    bool has_grid_calculated_width = is_grid_container && block->width > 0;

    // Width is auto-sized when no explicit width AND neither left+right constraints
    if (!(lycon->block.given_width >= 0 || (block->position->has_left && block->position->has_right))) {
        // Don't override flex/grid calculated width with flow-based auto-sizing
        if (has_flex_calculated_width || has_grid_calculated_width) {
            log_debug("auto-sizing width: SKIPPED - %s container already has calculated width %.1f",
                      is_flex_container ? "flex" : "grid", block->width);
        } else {
            // Note: max_width already includes left border + left padding from setup_inline
            // So we only need to add right padding and right border
            float flow_width = lycon->block.max_width;
            float padding_left = block->bound ? block->bound->padding.left : 0;
            float border_left = (block->bound && block->bound->border) ? block->bound->border->width.left : 0;
            float padding_right = block->bound ? block->bound->padding.right : 0;
            float border_right = (block->bound && block->bound->border) ? block->bound->border->width.right : 0;

            // flow_width includes left pad+border from setup_inline
            float border_box_width = flow_width + padding_right + border_right;

            // CSS 2.1 §10.4: Apply min/max-width constraints to auto-sized width.
            // Must handle border-box vs content-box correctly:
            // - border-box: min/max are in border-box terms, compare against border-box
            // - content-box: min/max are in content terms, extract content first
            bool is_border_box = block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX;
            if (is_border_box) {
                border_box_width = adjust_min_max_width(block, border_box_width);
            } else {
                float total_pad_border = padding_left + border_left + padding_right + border_right;
                float content_width = max(border_box_width - total_pad_border, 0.0f);
                content_width = adjust_min_max_width(block, content_width);
                border_box_width = content_width + total_pad_border;
            }
            block->width = border_box_width;

            // CSS 2.1 §10.3.7: When the pre-layout width was computed via shrink-to-fit
            // and auto-sizing produces a smaller width, check whether any child has a
            // percentage width. Percentage widths resolve against the container's
            // shrink-to-fit width, producing smaller child dimensions that make
            // max_width < shrink-to-fit. The container must not shrink below the
            // shrink-to-fit width in this case (circular dependency).
            if (block->width < pre_layout_width) {
                bool has_child_pct_width = false;
                for (View* ch = block->first_child; ch; ch = ch->next_sibling) {
                    if (ch->is_element()) {
                        ViewBlock* cb = (ViewBlock*)ch;
                        if (cb->blk && !isnan(cb->blk->given_width_percent)) {
                            has_child_pct_width = true;
                            break;
                        }
                    }
                }
                if (has_child_pct_width) {
                    log_debug("[ABS POS] Flooring auto-sized width %.1f to shrink-to-fit width %.1f (child has %%width)",
                              block->width, pre_layout_width);
                    block->width = pre_layout_width;
                }
            }

            // CRITICAL FIX: Re-align text after shrink-to-fit width calculation
            // Text alignment during layout used the large initial width, so rect->x may have
            // a large offset from centering. We need to correct it now that we know final width.
            if (lycon->block.text_align == CSS_VALUE_CENTER || lycon->block.text_align == CSS_VALUE_RIGHT) {
                float final_content_width = block->width;
                if (block->bound) {
                    final_content_width -= (block->bound->padding.left + block->bound->padding.right);
                    if (block->bound->border) {
                        final_content_width -= (block->bound->border->width.left + block->bound->border->width.right);
                    }
                }

                View* child = block->first_child;
                while (child) {
                    if (child->view_type == RDT_VIEW_TEXT) {
                        ViewText* text = (ViewText*)child;
                        TextRect* rect = text->rect;
                        while (rect) {
                            float line_width = rect->width;
                            float padding_left = block->bound ? block->bound->padding.left : 0;
                            float current_offset_in_content = rect->x - padding_left;
                            float target_offset_in_content;
                            if (lycon->block.text_align == CSS_VALUE_CENTER) {
                                target_offset_in_content = (final_content_width - line_width) / 2;
                            } else { // RIGHT
                                target_offset_in_content = final_content_width - line_width;
                            }
                            float offset = target_offset_in_content - current_offset_in_content;
                            if (fabs(offset) > 0.5f) {
                                rect->x += offset;
                                text->x = rect->x;  // Also update text bounds
                                log_debug("abs shrink-to-fit text align: rect->x adjusted by %.1f to %.1f (content_width=%.1f)",
                                          offset, rect->x, final_content_width);
                            }
                            rect = rect->next;
                        }
                    }
                    child = child->next();
                }
            }
        }
    }

    // CSS 2.1 §10.3.7: For RTL direction with neither left nor right specified,
    // set 'right' to the static position, then solve for 'left'.
    // The hypothetical box's right margin edge determines the static 'right' value.
    // For inline-level elements, account for float avoidance and text-align.
    if (static_direction == TD_RTL && !block->position->has_left && !block->position->has_right) {
        bool was_inline_rtl = false;
        if (elmt->is_element()) {
            DomElement* elem = elmt->as_element();
            was_inline_rtl = was_specified_inline(elem);
        }
        // In RTL, the inline cursor starts at the right edge of the line.
        // pa_line->advance_x tracks the LEFT cursor, not the right.
        float line_right = pa_line->right;

        // For inline-level elements, apply float avoidance + text-align
        if (was_inline_rtl) {
            BlockContext* bfc = block_context_find_bfc(pa_block);
            if (bfc) {
                float static_y_bfc = pa_block->advance_y + pa_block->bfc_offset_y;
                float lh = pa_block->line_height > 0 ? pa_block->line_height : 16.0f;
                FloatAvailableSpace space = block_context_space_at_y(bfc, static_y_bfc, lh);

                float avail_left = space.left - pa_block->bfc_offset_x;
                float avail_right = space.right - pa_block->bfc_offset_x;
                avail_left = fmax(avail_left, pa_line->left);
                avail_right = fmin(avail_right, pa_line->right);

                // Apply text-align: position is where the hypothetical box's
                // right margin edge would be
                CssEnum ta = pa_block->text_align;
                if (ta == CSS_VALUE_CENTER) {
                    line_right = (avail_left + avail_right) / 2.0f;
                } else if ((ta == CSS_VALUE_LEFT) || (ta == CSS_VALUE_END)) {
                    // End for RTL = left
                    line_right = avail_left;
                } else {
                    // Start for RTL (right, start, justify, default)
                    line_right = avail_right;
                }
                log_debug("[STATIC POS] RTL float+align adjusted line_right=%.1f (avail=[%.1f,%.1f], ta=%d)",
                          line_right, avail_left, avail_right, (int)ta); // INT_CAST_OK: enum for log
            }
        }

        float margin_right = (block->bound) ? block->bound->margin.right : 0;
        block->x = parent_to_cb_offset_x + line_right - block->width - margin_right;
        log_debug("[STATIC POS] RTL adjustment: x=%.1f (parent_offset=%.1f, line_right=%.1f, block_w=%.1f, mr=%.1f)",
                  block->x, parent_to_cb_offset_x, line_right, block->width, margin_right);
    }

    // CSS 2.1 §10.3.7: When width is auto (shrink-to-fit) and 'right' is specified
    // but 'left' is auto, recalculate x after width is finalized.
    // The initial x from calculate_absolute_position used the available width,
    // not the final shrink-to-fit width.
    if (block->position->has_right && !block->position->has_left &&
        !(lycon->block.given_width >= 0 || (block->position->has_left && block->position->has_right))) {
        float cb_border_left = (cb->bound && cb->bound->border) ? cb->bound->border->width.left : 0;
        float cb_border_right = (cb->bound && cb->bound->border) ? cb->bound->border->width.right : 0;
        float cb_padding_width = cb->width - cb_border_left - cb_border_right;
        float margin_right = (block->bound) ? block->bound->margin.right : 0;
        float margin_left = (block->bound) ? block->bound->margin.left : 0;
        block->x = cb_border_left + cb_padding_width - block->position->right - margin_right - block->width;
        log_debug("[ABS POS] right-positioned shrink-to-fit X recalc: x=%.1f (cb_pad_w=%.1f, right=%d, width=%.1f)",
                  block->x, cb_padding_width, block->position->right, block->width);
    }

    // Height is auto-sized when no explicit height AND neither top+bottom constraints
    // CRITICAL: Skip auto-sizing for flex/grid containers - they calculate their own height
    bool has_flex_calculated_height = is_flex_container && block->height > 0;
    bool has_grid_calculated_height = is_grid_container && block->height > 0;
    // CSS 2.1 §10.6.5: Replaced elements (img, iframe) use intrinsic height
    // for auto height. The image-loading code above already set block->height
    // from intrinsic dimensions; don't overwrite it with flow-based auto-sizing.
    bool has_replaced_intrinsic_height = (block->display.inner == RDT_DISPLAY_REPLACED) && block->height > 0;

    // CRITICAL: Use block->blk->given_height (canonical CSS value) instead of lycon->block.given_height
    // here, because lycon->block.given_height can be corrupted by child CSS style resolution
    // inside layout_block_inner_content (children's dom_node_resolve_style writes their own
    // given_height into lycon->block). block->blk->given_height is set once from CSS parsing
    // and from calculate_absolute_position for top+bottom constraints; it is not corrupted.
    // This mirrors the same pattern used in finalize_block_flow.
    float abs_block_given_height = (block->blk && block->blk->given_height >= 0) ? block->blk->given_height : -1;
    if (!(abs_block_given_height >= 0 || (block->position->has_top && block->position->has_bottom))) {
        // Don't override flex/grid calculated height with flow-based auto-sizing
        if (has_flex_calculated_height || has_grid_calculated_height || has_replaced_intrinsic_height) {
            log_debug("auto-sizing height: SKIPPED - %s already has calculated height %.1f",
                      is_flex_container ? "flex" : (is_grid_container ? "grid" : "replaced"), block->height);
        } else {
            float flow_height = lycon->block.advance_y;
            // Note: advance_y already includes top border + top padding from setup_inline
            // So we only need to add bottom padding and bottom border
            float padding_top = block->bound ? block->bound->padding.top : 0;
            float border_top = (block->bound && block->bound->border) ? block->bound->border->width.top : 0;
            float padding_bottom = block->bound ? block->bound->padding.bottom : 0;
            float border_bottom = (block->bound && block->bound->border) ? block->bound->border->width.bottom : 0;

            // Extract content height from advance_y (which includes top border+padding)
            float content_height = flow_height - padding_top - border_top;
            if (content_height < 0) content_height = 0;

            // CSS 2.1 §10.7: Apply min-height/max-height constraints to content height
            content_height = adjust_min_max_height(block, content_height);

            // Recompute border-box height
            block->height = content_height + padding_top + border_top + padding_bottom + border_bottom;
            log_debug("auto-sizing height: flow_height=%f, content_height=%f (after min/max), adding padding=%f+%f, border=%f+%f, total=%f",
                flow_height, content_height, padding_top, padding_bottom, border_top, border_bottom, block->height);
        }

        // BFC height expansion: if floats extend beyond flow content, expand height
        if (max_float_bottom > block->height) {
            log_debug("[ABS BFC] Expanding height from %.1f to %.1f to contain floats",
                      block->height, max_float_bottom);
            block->height = max_float_bottom;
        }

        // CRITICAL: Recalculate Y position when has_bottom without has_top and height is auto
        // The initial y was calculated with content_height=0, now we have the actual height
        if (block->position->has_bottom && !block->position->has_top) {
            // Recalculate cb_height (must use same logic as calculate_absolute_position)
            float cb_height = cb->height;
            bool is_icb = (cb->parent_view() == nullptr);
            if (is_icb && lycon->ui_context && lycon->ui_context->viewport_height > 0) {
                cb_height = lycon->ui_context->viewport_height * lycon->ui_context->pixel_ratio;
            }
            // Adjust for containing block's border (padding box)
            if (cb->bound && cb->bound->border) {
                cb_height -= (cb->bound->border->width.top + cb->bound->border->width.bottom);
            }
            float border_offset_y = (cb->bound && cb->bound->border) ? cb->bound->border->width.top : 0;
            float margin_bottom = block->bound ? block->bound->margin.bottom : 0;

            float new_y = border_offset_y + cb_height - block->position->bottom - margin_bottom - block->height;
            log_debug("[ABS POS] Recalculating Y for bottom-positioned auto-height element: old_y=%.1f, new_y=%.1f (cb_height=%.1f, bottom=%.1f, height=%.1f)",
                      block->y, new_y, cb_height, block->position->bottom, block->height);
            block->y = new_y;
        }
    }
    log_debug("final block position: x=%f, y=%f, width=%f, height=%f",
        block->x, block->y, block->width,  block->height);
    lycon->depth--;
    log_leave();
}

/**
 * Check if an element has positioning properties that require special handling
 */
bool element_has_positioning(ViewBlock* block) {
    return block->position &&
           (block->position->position == CSS_VALUE_RELATIVE ||
            block->position->position == CSS_VALUE_STICKY ||
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
// Float Layout Implementation (using unified BlockContext)
// ============================================================================

/**
 * Apply float layout to an element
 *
 * CSS 2.2 Section 9.5.1: Float Positioning Rules
 * Rule 1: Left float's left outer edge may not be to the left of the containing block's left edge
 * Rule 2: Right float's right outer edge may not be to the right of the containing block's right edge
 * Rule 3: Right float's right outer edge may not be to the right of any preceding right float's left outer edge
 * Rule 4: Float's outer top may not be higher than the top of its containing block
 * Rule 5: Float's outer top may not be higher than the outer top of any preceding float
 * Rule 6: Float's outer top may not be higher than any line-box with content preceding the float
 * Rule 7: Left float with preceding left floats: left edge must be to the right of preceding float's right edge,
 *         OR its top must be below the preceding float's bottom (SHIFT DOWN IF DOESN'T FIT)
 * Rule 8: Float must be placed as high as possible
 * Rule 9: Left floats placed as far left as possible, right floats as far right as possible
 *
 * The key implementation here is Rule 7 (and the right float equivalent): if a float doesn't fit
 * horizontally at the current Y position, it must shift down until it finds space.
 */
void layout_float_element(LayoutContext* lycon, ViewBlock* block) {
    if (!element_has_float(block)) {
        return;
    }

    log_debug("[FLOAT_LAYOUT] Applying float layout to element %s (float_prop=%d)",
              block->node_name(), block->position->float_prop);

    // Get the parent's BlockContext - floats are positioned relative to their BFC container
    BlockContext* parent_ctx = lycon->block.parent;
    if (!parent_ctx) {
        log_error("[FLOAT_LAYOUT] No parent BlockContext for float positioning");
        return;
    }

    // Find the BFC root from the parent's context
    BlockContext* bfc = block_context_find_bfc(parent_ctx);
    if (!bfc) {
        log_debug("[FLOAT_LAYOUT] No BFC found, using parent context directly");
        bfc = parent_ctx;
    }

    // Get the IMMEDIATE PARENT's content area offset (border + padding)
    ViewElement* parent_view = block->parent_view();
    float content_offset_x = 0;
    float content_offset_y = 0;
    if (parent_view && parent_view->is_block()) {
        ViewBlock* parent_block = (ViewBlock*)parent_view;
        if (parent_block->bound) {
            if (parent_block->bound->border) {
                content_offset_x += parent_block->bound->border->width.left;
                content_offset_y += parent_block->bound->border->width.top;
            }
            content_offset_x += parent_block->bound->padding.left;
            content_offset_y += parent_block->bound->padding.top;
        }
    }
    log_debug("[FLOAT_LAYOUT] Float parent: %s, content_offset=(%.1f, %.1f)",
              parent_view ? parent_view->node_name() : "null", content_offset_x, content_offset_y);

    float margin_left = block->bound ? block->bound->margin.left : 0;
    float margin_right = block->bound ? block->bound->margin.right : 0;
    float margin_top = block->bound ? block->bound->margin.top : 0;
    float margin_bottom = block->bound ? block->bound->margin.bottom : 0;

    // Get the parent block's content width for positioning
    float parent_content_width = parent_ctx->content_width;
    log_debug("[FLOAT_LAYOUT] using parent_ctx->content_width=%.1f", parent_content_width);

    // Calculate parent's position in BFC coordinates for coordinate conversion
    float parent_x_in_bfc = 0;
    float parent_y_in_bfc = 0;
    if (parent_view) {
        ViewElement* v = parent_view;
        while (v && v != bfc->establishing_element) {
            parent_x_in_bfc += v->x;
            parent_y_in_bfc += v->y;
            ViewElement* pv = v->parent_view();
            if (!pv) break;
            v = pv;
        }
    }
    log_debug("[FLOAT_LAYOUT] Float parent_in_bfc=(%.1f, %.1f)", parent_x_in_bfc, parent_y_in_bfc);

    // Calculate float dimensions including margins (margin box)
    float float_total_width = block->width + margin_left + margin_right;
    float float_total_height = block->height + margin_top + margin_bottom;

    // Get the initial Y position (from normal flow placement)
    // block->y is relative to parent's border box, includes margin.top already
    // CSS 2.1 §9.5.2: For floats with 'clear', the border edge is positioned at or below
    // the bottom outer edge of cleared floats. The margin-top may overlap with the cleared
    // float's margin-bottom. When querying available space, use the border edge (not margin
    // edge) so the overlap doesn't cause horizontal displacement.
    bool has_clear = block->position &&
        (block->position->clear == CSS_VALUE_LEFT ||
         block->position->clear == CSS_VALUE_RIGHT ||
         block->position->clear == CSS_VALUE_BOTH);
    float initial_y_local;
    if (has_clear) {
        // Use border edge: block->y is the cleared position (border edge)
        initial_y_local = block->y;
        // Adjust float_total_height to exclude margin_top since we start at border edge
        float_total_height = block->height + margin_bottom;
    } else {
        initial_y_local = block->y - margin_top;  // margin-top edge in parent coords
    }
    float current_y_bfc = initial_y_local + parent_y_in_bfc;

    log_debug("[FLOAT_LAYOUT] Float dimensions: width=%.1f, height=%.1f, total_width=%.1f, total_height=%.1f",
              block->width, block->height, float_total_width, float_total_height);
    log_debug("[FLOAT_LAYOUT] Initial position: local_y=%.1f, bfc_y=%.1f", initial_y_local, current_y_bfc);
    log_debug("[FLOAT_LAYOUT] BFC: left_floats=%d, right_floats=%d, right_edge=%.1f",
              bfc->left_float_count, bfc->right_float_count, bfc->float_right_edge);

    // CSS 2.1 §9.5.1 Rule 5: "The outer top of a floating box may not be higher than
    // the outer top of any block or floated box generated by an element earlier in the
    // source document."
    // Enforce by finding the maximum margin_box_top of all preceding floats in the BFC.
    // Since floats are added to BFC lists in source order, all existing floats precede
    // the current one.
    {
        float max_preceding_float_top = current_y_bfc;
        for (FloatBox* fb = bfc->left_floats; fb; fb = fb->next) {
            if (fb->margin_box_top > max_preceding_float_top) {
                max_preceding_float_top = fb->margin_box_top;
            }
        }
        for (FloatBox* fb = bfc->right_floats; fb; fb = fb->next) {
            if (fb->margin_box_top > max_preceding_float_top) {
                max_preceding_float_top = fb->margin_box_top;
            }
        }
        if (max_preceding_float_top > current_y_bfc) {
            log_debug("[FLOAT_LAYOUT] Rule 5: Adjusting Y from %.1f to %.1f (preceding float top)",
                      current_y_bfc, max_preceding_float_top);
            current_y_bfc = max_preceding_float_top;
        }
    }

    // CSS 2.2 §9.5.1 Rule 6/7/8: Find Y position where float fits horizontally
    // Start at current Y and move down until we find space
    float final_y_bfc = current_y_bfc;
    int max_iterations = 100;  // Prevent infinite loops

    // CSS 2.1 §9.5.1: Calculate containing block edges in BFC coordinates
    float containing_block_left_bfc = parent_x_in_bfc + content_offset_x;
    float containing_block_right_bfc = parent_x_in_bfc + content_offset_x + parent_content_width;
    float containing_block_width = parent_content_width;
    log_debug("[FLOAT_LAYOUT] Containing block in BFC coords: left=%.1f, right=%.1f, width=%.1f",
              containing_block_left_bfc, containing_block_right_bfc, containing_block_width);

    // CSS 2.1 §9.5.1 Rule 7 (paraphrased): A left float may not stick out at the right edge
    // of its containing block UNLESS it is already as far to the left as possible.
    // This means: if the float's margin box is wider than its containing block's content width,
    // the float can overflow (it's inherently too wide to fit). In that case, don't push it
    // down trying to find space — place it at its leftmost/rightmost position and let it overflow.
    bool float_wider_than_cb = (float_total_width > containing_block_width + 0.5f);
    if (float_wider_than_cb) {
        log_debug("[FLOAT_LAYOUT] Float (%.1f) is wider than containing block (%.1f), may overflow",
                  float_total_width, containing_block_width);
    }

    while (max_iterations-- > 0) {
        // Query available space at this Y position (constrained by BFC edges and other floats)
        FloatAvailableSpace space = block_context_space_at_y(bfc, final_y_bfc, float_total_height);

        // CSS 2.1 §9.5.1: Compute effective available width
        // Rule 1: float's left outer edge >= containing block's left edge
        // Rule 7: float's right outer edge <= containing block's right edge
        // The BFC space (space.left/right) reflects other float intrusions.
        // When no other float constrains a side, the containing block boundary
        // is the only constraint — not the BFC element's content width, which
        // may be narrower than the containing block when intermediate elements
        // use negative margins (e.g., Bootstrap .row { margin: 0 -15px }).
        float effective_left, effective_right;
        if (block->position->float_prop == CSS_VALUE_LEFT) {
            effective_left = space.has_left_float
                ? max(space.left, containing_block_left_bfc)
                : containing_block_left_bfc;
            if (float_wider_than_cb) {
                // Float is wider than CB — use BFC space (allow overflow to right)
                effective_right = space.right;
            } else {
                effective_right = space.has_right_float
                    ? min(space.right, containing_block_right_bfc)
                    : containing_block_right_bfc;
            }
        } else {
            effective_right = space.has_right_float
                ? min(space.right, containing_block_right_bfc)
                : containing_block_right_bfc;
            if (float_wider_than_cb) {
                // Float is wider than CB — use BFC space (allow overflow to left)
                effective_left = space.left;
            } else {
                effective_left = space.has_left_float
                    ? max(space.left, containing_block_left_bfc)
                    : containing_block_left_bfc;
            }
        }
        float available_width = effective_right - effective_left;

        log_debug("[FLOAT_LAYOUT] Checking Y=%.1f: space=(%.1f, %.1f), effective=(%.1f, %.1f), available=%.1f, needed=%.1f",
                  final_y_bfc, space.left, space.right, effective_left, effective_right, available_width, float_total_width);

        // Check if float fits at this Y position
        // Use tiny epsilon for float32 rounding in percentage-based layouts.
        // When sibling floats have percentage widths summing to 100%, both widths
        // round up slightly after double→float32 truncation, causing their sum to
        // exceed the parent width by ~0.0001px. Without this tolerance the second
        // float wraps to the next line. Browsers avoid this via fixed-point math.
        if (available_width >= float_total_width - 0.001f) {
            // Float fits here - determine X position
            if (block->position->float_prop == CSS_VALUE_LEFT) {
                // Left float: position at left edge of available space
                float new_x;
                if (space.has_left_float) {
                    float left_in_parent = space.left - parent_x_in_bfc;
                    new_x = left_in_parent + margin_left;
                } else {
                    new_x = content_offset_x + margin_left;
                }
                block->x = new_x;
                log_debug("[FLOAT_LAYOUT] Float:left positioned at x=%.1f", new_x);
            } else {
                // Right float: position at right edge of available space
                float new_x;
                if (space.has_right_float) {
                    float right_in_parent = space.right - parent_x_in_bfc;
                    new_x = right_in_parent - block->width - margin_right;
                } else {
                    float content_right = content_offset_x + parent_content_width;
                    new_x = content_right - block->width - margin_right;
                }
                block->x = new_x;
                log_debug("[FLOAT_LAYOUT] Float:right positioned at x=%.1f", new_x);
            }
            break;  // Found a valid position
        }

        // CSS 2.1 §9.5.1 Rule 7: "A left-floating box that has another left-floating box
        // to its left may not have its right outer edge to the right of its containing
        // block's right edge. (Loosely: a left float may not stick out at the right edge,
        // unless it is already as far to the left as possible.)"
        // If the float is wider than its containing block AND it's at its leftmost/rightmost
        // possible position (no same-direction float blocking it), place it here and overflow.
        if (float_wider_than_cb) {
            if (block->position->float_prop == CSS_VALUE_LEFT) {
                bool at_leftmost = !space.has_left_float || (space.left <= containing_block_left_bfc + 0.5f);
                if (at_leftmost) {
                    log_debug("[FLOAT_LAYOUT] Left float wider than CB, at leftmost, overflow at Y=%.1f", final_y_bfc);
                    float new_x = content_offset_x + margin_left;
                    block->x = new_x;
                    break;
                }
            } else {
                bool at_rightmost = !space.has_right_float || (space.right >= containing_block_right_bfc - 0.5f);
                if (at_rightmost) {
                    log_debug("[FLOAT_LAYOUT] Right float wider than CB, at rightmost, overflow at Y=%.1f", final_y_bfc);
                    float content_right = content_offset_x + parent_content_width;
                    float new_x = content_right - block->width - margin_right;
                    block->x = new_x;
                    break;
                }
            }
        }

        // Float doesn't fit - need to shift down (CSS 2.2 §9.5.1 Rule 7)
        // Find the next float boundary to try
        float next_y = FLT_MAX;

        // Check left floats for next boundary
        for (FloatBox* fb = bfc->left_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > final_y_bfc && fb->margin_box_bottom < next_y) {
                next_y = fb->margin_box_bottom;
            }
        }

        // Check right floats for next boundary
        for (FloatBox* fb = bfc->right_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > final_y_bfc && fb->margin_box_bottom < next_y) {
                next_y = fb->margin_box_bottom;
            }
        }

        if (next_y == FLT_MAX || next_y <= final_y_bfc) {
            // No more floats below - position at current Y anyway
            // (this shouldn't happen if there's enough container width)
            log_debug("[FLOAT_LAYOUT] No more float boundaries, positioning at Y=%.1f", final_y_bfc);

            // Position float at the edge even if it doesn't fit perfectly
            if (block->position->float_prop == CSS_VALUE_LEFT) {
                FloatAvailableSpace space = block_context_space_at_y(bfc, final_y_bfc, float_total_height);
                float new_x = space.has_left_float ?
                    (space.left - parent_x_in_bfc + margin_left) :
                    (content_offset_x + margin_left);
                block->x = new_x;
            } else {
                FloatAvailableSpace space = block_context_space_at_y(bfc, final_y_bfc, float_total_height);
                float new_x = space.has_right_float ?
                    (space.right - parent_x_in_bfc - block->width - margin_right) :
                    (content_offset_x + parent_content_width - block->width - margin_right);
                block->x = new_x;
            }
            break;
        }

        log_debug("[FLOAT_LAYOUT] Float doesn't fit, shifting from Y=%.1f to Y=%.1f",
                  final_y_bfc, next_y);
        final_y_bfc = next_y;
    }

    // Convert final Y position back to parent-relative coordinates and apply
    float final_y_local = final_y_bfc - parent_y_in_bfc;
    float new_y = final_y_local + margin_top;

    if (new_y != block->y) {
        log_debug("[FLOAT_LAYOUT] Float Y shifted: old=%.1f, new=%.1f (delta=%.1f)",
                  block->y, new_y, new_y - block->y);
        block->y = new_y;
    }

    log_debug("[FLOAT_LAYOUT] Float element positioned at (%.1f, %.1f) size (%.1f, %.1f)",
              block->x, block->y, block->width, block->height);

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

    // Get the current view being laid out
    View* current_view = lycon->view;
    if (!current_view) {
        log_debug("adjust_line_for_floats: early exit - no current_view");
        return;
    }

    // Check if we're inside a floated element - if so, skip adjustment
    // (lines inside floats don't adjust for parent's float context)
    ViewElement* ancestor = (ViewElement*)current_view;
    ViewBlock* container = bfc->establishing_element;
    bool found_container = false;
    while (ancestor) {
        if (ancestor == container) {
            found_container = true;
            break;
        }
        if (ancestor->is_block()) {
            ViewBlock* block = (ViewBlock*)ancestor;
            if (block->position && element_has_float(block)) {
                log_debug("Skipping float adjustment: inside floated element %s",
                          block->node_name());
                return;
            }
        }
        ancestor = ancestor->parent_view();
    }

    if (!found_container) {
        log_debug("adjust_line_for_floats: early exit - view not inside BFC");
        return;
    }

    // Use cached BFC offset from BlockContext
    float block_offset_x = lycon->block.bfc_offset_x;
    float block_offset_y = lycon->block.bfc_offset_y;

    // Convert current line Y to BFC coordinates
    float line_top_bfc = block_offset_y + lycon->block.advance_y;
    float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f;

    log_debug("Adjusting line for floats: local_y=%.1f, bfc_y=%.1f, height=%.1f, offset=(%.1f, %.1f)",
              lycon->block.advance_y, line_top_bfc, line_height, block_offset_x, block_offset_y);

    // Query available space at current line position using BlockContext API
    FloatAvailableSpace space = block_context_space_at_y(bfc, line_top_bfc, line_height);

    // If there's no float intrusion at this Y position, skip adjustment
    if (!space.has_left_float && !space.has_right_float) {
        log_debug("No float intrusion at this Y position, skipping adjustment");
        return;
    }

    // Convert available space from BFC coordinates to local block coordinates
    float local_left = space.left - block_offset_x;
    float local_right = space.right - block_offset_x;

    // Clamp to the current block's line bounds
    float new_effective_left = max(local_left, lycon->line.left);
    float new_effective_right = min(local_right, lycon->line.right);

    log_debug("Float adjustment: space=(%.1f, %.1f), local=(%.1f, %.1f), effective=(%.1f, %.1f)",
              space.left, space.right, local_left, local_right,
              new_effective_left, new_effective_right);

    // Apply the float intrusion to effective bounds
    if (space.has_left_float && new_effective_left > lycon->line.left) {
        log_debug("Line effective_left adjusted: %.1f->%.1f (float intrusion)",
                  lycon->line.effective_left, new_effective_left);
        lycon->line.effective_left = new_effective_left;
        lycon->line.has_float_intrusion = true;
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

        // CSS 2.1 §9.5.2: Update the parent's advance_y so container height reflects the
        // clearance, but ONLY for in-flow (non-float) blocks. Floats are out of normal flow
        // (CSS 2.1 §9.5) and their clearance positions them relative to other floats without
        // affecting the parent container's flow height. BFC roots handle float containment
        // separately via §10.6.7 height expansion.
        bool is_float = block->position && element_has_float(block);
        if (!is_float && lycon->block.parent) {
            lycon->block.parent->advance_y += delta;
            log_debug("Updated parent advance_y by %.1f to %.1f", delta, lycon->block.parent->advance_y);
        }

        log_debug("Moved element down by %.1f to clear floats, new y=%.1f%s", delta, block->y,
                  is_float ? " (float — parent advance_y unchanged)" : "");
    }
}
