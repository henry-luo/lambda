#include "layout.hpp"
#include "layout_positioned.hpp"
#include "available_space.hpp"
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
float adjust_min_max_width(ViewBlock* block, float width);
float adjust_min_max_height(ViewBlock* block, float height);
float adjust_border_padding_width(ViewBlock* block, float width);
float adjust_border_padding_height(ViewBlock* block, float height);
void layout_block_inner_content(LayoutContext* lycon, ViewBlock* block);
void setup_inline(LayoutContext* lycon, ViewBlock* block);

/**
 * Recursively offset all child views by the given amounts
 * Used for inline relative positioning where children have block-relative coordinates
 *
 * Note: For block-level children, we offset the block itself but NOT its contents.
 * Block children break out of inline context and establish their own coordinate system,
 * so their internal content (text, nested elements) should not be affected by the
 * inline span's relative positioning offset.
 */
static void offset_children_recursive(ViewElement* elem, int offset_x, int offset_y) {
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

    // For inline elements (spans), children have block-relative coordinates,
    // so we must also offset all descendants to move with the inline box
    if (block->view_type == RDT_VIEW_INLINE && (offset_x != 0 || offset_y != 0)) {
        log_debug("Offsetting inline children by (%d, %d)", offset_x, offset_y);
        offset_children_recursive((ViewElement*)block, offset_x, offset_y);
    }

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
        log_debug("[ABS POS] re-resolved width: %.1f%% of %.1f = %.1f", block->blk->given_width_percent, cb_width, lycon->block.given_width);
    }
    if (block->blk && !isnan(block->blk->given_height_percent)) {
        lycon->block.given_height = block->blk->given_height_percent * cb_height / 100.0f;
        log_debug("[ABS POS] re-resolved height: %.1f%% of %.1f = %.1f", block->blk->given_height_percent, cb_height, lycon->block.given_height);
    }

    float content_width, content_height;
    // calculate horizontal position
    log_debug("given_width=%f, given_height=%f, width_type=%d", lycon->block.given_width, lycon->block.given_height,
              block->blk ? block->blk->given_width_type : -1);

    // Check if width uses intrinsic sizing keywords (max-content, min-content, fit-content)
    bool is_intrinsic_width = block->blk &&
        (block->blk->given_width_type == CSS_VALUE_MAX_CONTENT ||
         block->blk->given_width_type == CSS_VALUE_MIN_CONTENT ||
         block->blk->given_width_type == CSS_VALUE_FIT_CONTENT);

    // First determine content_width: use CSS width if specified, otherwise calculate from constraints
    if (lycon->block.given_width >= 0 && !is_intrinsic_width) {
        content_width = lycon->block.given_width;
    } else if (block->position->has_left && block->position->has_right && !is_intrinsic_width) {
        // both left and right specified - calculate width from constraints
        float left_edge = block->position->left + (block->bound ? block->bound->margin.left : 0);
        float right_edge = cb_width - block->position->right - (block->bound ? block->bound->margin.right : 0);
        content_width = max(right_edge - left_edge, 0.0f);
        // CRITICAL: Store constraint-calculated width so finalize_block_flow knows width is fixed
        lycon->block.given_width = content_width;
        log_debug("[ABS POS] width from constraints: left_edge=%.1f, right_edge=%.1f, content_width=%.1f (stored in given_width)",
                  left_edge, right_edge, content_width);
    } else if (is_intrinsic_width) {
        // For max-content/min-content/fit-content, use 0 as initial width
        // The actual width will be determined by content and adjusted post-layout
        // Set content_width to 0 to trigger shrink-to-fit behavior
        content_width = 0;
        log_debug("Using intrinsic sizing for absolutely positioned element: content_width=0 (shrink-to-fit)");
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
    log_debug("[ABS POS] height calc: given_height=%.1f, has_top=%d, has_bottom=%d, cb_height=%.1f",
              lycon->block.given_height, block->position->has_top, block->position->has_bottom, cb_height);
    if (lycon->block.given_height >= 0) {
        content_height = lycon->block.given_height;
        log_debug("[ABS POS] using explicit height: %.1f", content_height);
    } else if (block->position->has_top && block->position->has_bottom) {
        // both top and bottom specified - calculate height from constraints
        float top_edge = block->position->top + (block->bound ? block->bound->margin.top : 0);
        float bottom_edge = cb_height - block->position->bottom - (block->bound ? block->bound->margin.bottom : 0);
        content_height = max(bottom_edge - top_edge, 0.0f);
        // CRITICAL: Store constraint-calculated height so finalize_block_flow knows height is fixed
        // finalize_block_flow uses block->blk->given_height (not lycon->block.given_height) to
        // determine if height is explicitly set, so we must also update block->blk->given_height
        lycon->block.given_height = content_height;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->given_height = content_height;
        log_debug("[ABS POS] height from constraints: top_edge=%.1f, bottom_edge=%.1f, content_height=%.1f (stored in given_height)",
                  top_edge, bottom_edge, content_height);
    } else {
        // shrink-to-fit: height will be determined by content after layout
        // Start with 0 and let the post-layout adjustment set the final height
        content_height = 0;
        log_debug("[ABS POS] using auto height (shrink-to-fit)");
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
    }
    else {
        // no change to block->x, block->y, lycon->line.advance_x, lycon->block.advance_y
        block->width = content_width;  block->height = content_height;
    }
    log_debug("calculated x,y,wd,hg, content_width, content_height: (%f, %f) size (%f, %f), content (%f, %f) within containing block (%f, %f) size (%f, %f)",
        block->x, block->y, block->width, block->height, lycon->block.content_width, lycon->block.content_height, cb_x, cb_y, cb_width, cb_height);
}

void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line) {
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

            // Update block dimensions
            block->width = lycon->block.given_width;
            block->height = lycon->block.given_height;
            lycon->block.content_width = lycon->block.given_width;
            lycon->block.content_height = lycon->block.given_height;

            if (img->format == IMAGE_FORMAT_SVG) {
                img->max_render_width = max(lycon->block.given_width, img->max_render_width);
            }
            log_debug("[ABS IMG] final dimensions: %.1f x %.1f", block->width, block->height);
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
    float parent_to_cb_offset_x = 0, parent_to_cb_offset_y = 0;
    ViewElement* parent = block->parent_view();
    if (parent && parent->is_block()) {
        ViewBlock* p = (ViewBlock*)parent;
        // Walk from parent to containing block, accumulating offsets
        while (p && p != cb) {
            parent_to_cb_offset_x += p->x;
            parent_to_cb_offset_y += p->y;
            log_debug("[STATIC POS] Adding parent %s offset: (%f, %f)", p->node_name(), p->x, p->y);
            ViewElement* gp = p->parent_view();
            if (gp && gp->is_block()) {
                p = (ViewBlock*)gp;
            } else {
                break;
            }
        }
        // Note: Don't add parent's padding/border here - pa_line->left already includes them
    }
    log_debug("[STATIC POS] Total parent-to-CB offset: (%f, %f)", parent_to_cb_offset_x, parent_to_cb_offset_y);

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
        float static_x = parent_to_cb_offset_x + pa_line->left;  // Line's left edge for static horizontal position
        if (block->bound && block->bound->margin.left > 0) {
            static_x += block->bound->margin.left;
        }
        log_debug("[STATIC POS] Using static X position: %.1f (pa_line->left=%.1f, offset=%.1f)",
                  static_x, pa_line->left, parent_to_cb_offset_x);
        block->x = static_x;
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
    // Only grid containers explicitly calculate width post-layout (in layout_grid_multipass.cpp)
    // Flex containers handle shrink-to-fit within their algorithm
    bool has_grid_calculated_width = is_grid_container && block->width > 0;

    // Width is auto-sized when no explicit width AND neither left+right constraints
    if (!(lycon->block.given_width >= 0 || (block->position->has_left && block->position->has_right))) {
        // Don't override grid calculated width with flow-based auto-sizing
        if (has_grid_calculated_width) {
            log_debug("auto-sizing width: SKIPPED - grid container already has calculated width %.1f",
                      block->width);
        } else {
            // Note: max_width already includes left border + left padding from setup_inline
            // So we only need to add right padding and right border
            float flow_width = lycon->block.max_width;
            float padding_right = block->bound ? block->bound->padding.right : 0;
            float border_right = (block->bound && block->bound->border) ? block->bound->border->width.right : 0;
            block->width = flow_width + padding_right + border_right;

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
    // Height is auto-sized when no explicit height AND neither top+bottom constraints
    // CRITICAL: Skip auto-sizing for flex/grid containers - they calculate their own height
    bool has_flex_calculated_height = is_flex_container && block->height > 0;
    bool has_grid_calculated_height = is_grid_container && block->height > 0;

    if (!(lycon->block.given_height >= 0 || (block->position->has_top && block->position->has_bottom))) {
        // Don't override flex/grid calculated height with flow-based auto-sizing
        if (has_flex_calculated_height || has_grid_calculated_height) {
            log_debug("auto-sizing height: SKIPPED - %s container already has calculated height %.1f",
                      is_flex_container ? "flex" : "grid", block->height);
        } else {
            float flow_height = lycon->block.advance_y;
            // Note: advance_y already includes top border + top padding from setup_inline
            // So we only need to add bottom padding and bottom border
            float padding_bottom = block->bound ? block->bound->padding.bottom : 0;
            float border_bottom = (block->bound && block->bound->border) ? block->bound->border->width.bottom : 0;
            log_debug("auto-sizing height: flow_height=%f (includes top border+padding), adding padding_bottom=%f, border_bottom=%f",
                flow_height, padding_bottom, border_bottom);
            block->height = flow_height + padding_bottom + border_bottom;
        }

        // BFC height expansion: if floats extend beyond flow content, expand height
        if (max_float_bottom > block->height) {
            log_debug("[ABS BFC] Expanding height from %.1f to %.1f to contain floats",
                      block->height, max_float_bottom);
            block->height = max_float_bottom;
        }
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
    float initial_y_local = block->y - margin_top;  // Get content box top in parent coords
    float current_y_bfc = initial_y_local + parent_y_in_bfc;

    log_debug("[FLOAT_LAYOUT] Float dimensions: width=%.1f, height=%.1f, total_width=%.1f, total_height=%.1f",
              block->width, block->height, float_total_width, float_total_height);
    log_debug("[FLOAT_LAYOUT] Initial position: local_y=%.1f, bfc_y=%.1f", initial_y_local, current_y_bfc);
    log_debug("[FLOAT_LAYOUT] BFC: left_floats=%d, right_floats=%d, right_edge=%.1f",
              bfc->left_float_count, bfc->right_float_count, bfc->float_right_edge);

    // CSS 2.2 §9.5.1 Rule 6/7/8: Find Y position where float fits horizontally
    // Start at current Y and move down until we find space
    float final_y_bfc = current_y_bfc;
    int max_iterations = 100;  // Prevent infinite loops

    // CSS 2.1 §9.5.1: Float's margin box must not exceed the containing block's content edge
    // Calculate the containing block's right edge in BFC coordinates
    float containing_block_right_bfc = parent_x_in_bfc + content_offset_x + parent_content_width;
    log_debug("[FLOAT_LAYOUT] Containing block right edge in BFC coords: %.1f", containing_block_right_bfc);

    while (max_iterations-- > 0) {
        // Query available space at this Y position
        FloatAvailableSpace space = block_context_space_at_y(bfc, final_y_bfc, float_total_height);

        // Constrain space.right by the containing block's right edge
        float effective_right = min(space.right, containing_block_right_bfc);
        float available_width = effective_right - space.left;

        log_debug("[FLOAT_LAYOUT] Checking Y=%.1f: space=(%.1f, %.1f), effective_right=%.1f, available=%.1f, needed=%.1f",
                  final_y_bfc, space.left, space.right, effective_right, available_width, float_total_width);

        // Check if float fits at this Y position
        if (available_width >= float_total_width) {
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

        // IMPORTANT: Also update the parent's advance_y so container height is calculated correctly
        // The parent's BlockContext is accessed via parent pointer
        if (lycon->block.parent) {
            lycon->block.parent->advance_y += delta;
            log_debug("Updated parent advance_y by %.1f to %.1f", delta, lycon->block.parent->advance_y);
        }

        log_debug("Moved element down by %.1f to clear floats, new y=%.1f", delta, block->y);
    }
}
