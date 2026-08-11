#include "layout.hpp"
#include "view.hpp"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/css_style.hpp"
#include <cfloat>
#include <cmath>

static bool view_tree_has_table_flow(View* view) {
    if (!view || !view->is_element()) return false;
    ViewElement* element = lam::view_require_element(view);
    ViewBlock* block = lam::view_as_block(view);
    if (block && block->display.inner == CSS_VALUE_TABLE) return true;
    for (View* child = element->first_child; child; child = child->next_sibling) {
        if (view_tree_has_table_flow(child)) return true;
    }
    return false;
}

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
    if (!elem) return false;
    // Check the specified (pre-blockification) display value from the style tree.
    // We cannot use elem->display.outer because it may already be blockified
    // by resolve_display_value() for abs-pos/float elements (CSS 2.1 §9.7).
    if (elem->specified_style && elem->specified_style->tree) {
        AvlNode* disp_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_DISPLAY);
        if (disp_node) {
            StyleNode* sn = (StyleNode*)disp_node->declaration;
            if (sn && sn->winning_decl && sn->winning_decl->value &&
                sn->winning_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum kw = sn->winning_decl->value->data.keyword;
                return kw == CSS_VALUE_INLINE || kw == CSS_VALUE_INLINE_BLOCK ||
                       kw == CSS_VALUE_INLINE_FLEX || kw == CSS_VALUE_INLINE_GRID ||
                       kw == CSS_VALUE_INLINE_TABLE;
            }
        }
        // No explicit display property — check if the element defaults to inline.
        // Per HTML spec, phrasing content elements (span, a, em, strong, etc.)
        // default to display:inline.
        NameId tag = elem->tag_id;
        return (tag == MARKUP_NAME_SPAN || tag == MARKUP_NAME_A ||
                tag == MARKUP_NAME_EM || tag == MARKUP_NAME_STRONG ||
                tag == MARKUP_NAME_B || tag == MARKUP_NAME_I ||
                tag == MARKUP_NAME_U || tag == MARKUP_NAME_S ||
                tag == MARKUP_NAME_SMALL || tag == MARKUP_NAME_CODE ||
                tag == MARKUP_NAME_SUB || tag == MARKUP_NAME_SUP ||
                tag == MARKUP_NAME_ABBR || tag == MARKUP_NAME_CITE ||
                tag == MARKUP_NAME_Q || tag == MARKUP_NAME_VAR ||
                tag == MARKUP_NAME_TIME || tag == MARKUP_NAME_MARK ||
                tag == MARKUP_NAME_BDO || tag == MARKUP_NAME_BDI);
    }
    return false;
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
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
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
            offset_children_recursive(lam::view_require_element(child), offset_x, offset_y);
        }
        child = child->next();
    }
}

static TextDirection get_static_position_direction(ViewElement* parent);

static float relative_inset_offset(bool has_start, float start, float start_percent,
                                   bool has_end, float end, float end_percent,
                                   float containing_size) {
    if (has_start) {
        return !isnan(start_percent) ? start_percent * containing_size / 100.0f : start;
    }
    if (has_end) {
        return !isnan(end_percent) ? -end_percent * containing_size / 100.0f : -end;
    }
    return 0.0f;
}

static float sticky_axis_offset(bool has_start, float start,
                                bool has_end, float end,
                                float viewport_start, float viewport_end,
                                float element_start, float element_end) {
    if (has_start) {
        float minimum = viewport_start + start;
        return element_start < minimum ? minimum - element_start : 0.0f;
    }
    if (has_end) {
        float maximum = viewport_end - end;
        return element_end > maximum ? maximum - element_end : 0.0f;
    }
    return 0.0f;
}

static float sticky_clamp_axis_offset(float offset, float local_start,
                                      float local_end, float containing_start,
                                      float containing_end) {
    if (offset > 0.0f && local_end + offset > containing_end) {
        return max(0.0f, containing_end - local_end);
    }
    if (offset < 0.0f && local_start + offset < containing_start) {
        return min(0.0f, containing_start - local_start);
    }
    return offset;
}

static bool float_is_left(ViewBlock* block) {
    return block && block->positionp()->float_prop == CSS_VALUE_LEFT;
}

static float float_next_boundary(BlockContext* bfc, float current_y) {
    float next_y = FLT_MAX;
    for (FloatBox* fb = bfc ? bfc->left_floats : nullptr; fb; fb = fb->next) {
        if (fb->margin_box_bottom > current_y && fb->margin_box_bottom < next_y) {
            next_y = fb->margin_box_bottom;
        }
    }
    for (FloatBox* fb = bfc ? bfc->right_floats : nullptr; fb; fb = fb->next) {
        if (fb->margin_box_bottom > current_y && fb->margin_box_bottom < next_y) {
            next_y = fb->margin_box_bottom;
        }
    }
    return next_y;
}

static float float_position_x(const FloatAvailableSpace& space, bool left,
                              float parent_x_in_bfc, float content_offset_x,
                              float parent_content_width, ViewBlock* block,
                              float margin_left, float margin_right) {
    if (left) {
        return space.has_left_float
            ? space.left - parent_x_in_bfc + margin_left
            : content_offset_x + margin_left;
    }
    return space.has_right_float
        ? space.right - parent_x_in_bfc - block->width - margin_right
        : content_offset_x + parent_content_width - block->width - margin_right;
}

float layout_relative_axis_offset(ViewBlock* block, bool horizontal, float containing_size) {
    if (!block || !block->position) return 0.0f;
    const PositionProp* position = block->positionp();
    return horizontal
        ? relative_inset_offset(position->has_left, position->left, position->left_percent,
                                position->has_right, position->right, position->right_percent,
                                containing_size)
        : relative_inset_offset(position->has_top, position->top, position->top_percent,
                                position->has_bottom, position->bottom, position->bottom_percent,
                                containing_size);
}

void layout_relative_position_offset(ViewBlock* block, float* offset_x_out, float* offset_y_out) {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    if (!block || !block->position ||
        block->positionp()->position != CSS_VALUE_RELATIVE) {
        if (offset_x_out) *offset_x_out = offset_x;
        if (offset_y_out) *offset_y_out = offset_y;
        return;
    }

    ViewElement* parent = block->parent_view();
    TextDirection parent_direction = get_static_position_direction(parent);

    // Get containing block dimensions for percentage resolution
    // CSS Position 3 §3.4: percentage top/bottom resolve against containing block height,
    // percentage left/right resolve against containing block width
    // Note: We cannot use parent->content_width/content_height here because those fields
    // are set in finalize_block_flow() AFTER all children are laid out. At this point
    // (during child layout), they are still 0. Instead, derive from the parent's
    // border-box width/height minus padding and border.
    float cb_width = 0, cb_height = 0;
    // CSS 2.1 §9.2.1.1: For block children inside inline spans (block-in-inline),
    // the containing block is the nearest block-level ancestor, not the inline span.
    ViewBlock* parent_block = layout_nearest_block_ancestor(parent);
    if (parent_block) {
        LayoutContainingBlock cb = layout_containing_block_for_view(parent_block);
        cb_width = cb.content_width;

        // CSS 2.1 §10.5: If containing block height is auto, percentage top/bottom = 0
        // Only use parent height if it has an explicit (non-auto) height
        if (cb.has_definite_height) {
            cb_height = cb.content_height;
        }
    }

    // horizontal offset: precedence depends on containing block's direction
    // CSS spec: If both left and right are not 'auto':
    // - If direction is 'ltr', left wins and right is ignored
    // - If direction is 'rtl', right wins and left is ignored (but equal values cancel in RTL)
    bool both_horizontal = block->positionp()->has_left && block->positionp()->has_right;

    if (both_horizontal) {
        if (parent_direction == TD_RTL) {
            // CSS 2.1 §9.4.3: RTL — right wins, left becomes -right
            offset_x = relative_inset_offset(false, 0.0f, NAN, true,
                                              block->positionp()->right,
                                              block->positionp()->right_percent, cb_width);
        } else {
            // LTR: left takes precedence (always, even if equal to right)
            offset_x = relative_inset_offset(true, block->positionp()->left,
                                              block->positionp()->left_percent, false,
                                              0.0f, NAN, cb_width);
        }
    } else {
        offset_x = relative_inset_offset(
            block->positionp()->has_left, block->positionp()->left,
            block->positionp()->left_percent, block->positionp()->has_right,
            block->positionp()->right, block->positionp()->right_percent, cb_width);
    }
    // vertical offset: top takes precedence over bottom
    // CSS 2.1 §10.6.5: if containing block height is auto, percentage top/bottom = 0
    offset_y = relative_inset_offset(
        block->positionp()->has_top, block->positionp()->top,
        block->positionp()->top_percent, block->positionp()->has_bottom,
        block->positionp()->bottom, block->positionp()->bottom_percent, cb_height);

    if (offset_x_out) *offset_x_out = offset_x;
    if (offset_y_out) *offset_y_out = offset_y;
}

/**
 * Apply relative positioning to an element
 * Relative positioning moves the element from its normal position without affecting other elements
 */
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return;

    // calculate offset from top/right/bottom/left properties
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    layout_relative_position_offset(block, &offset_x, &offset_y);

    // apply offset to visual position (doesn't affect layout of other elements)
    block->x += offset_x;  block->y += offset_y;

    // For inline elements (spans), children have block-relative coordinates,
    // so we must also offset all descendants to move with the inline box
    if (block->view_type == RDT_VIEW_INLINE && (offset_x != 0 || offset_y != 0)) {
        offset_children_recursive(lam::view_require_element(block), offset_x, offset_y);
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
        ViewBlock* pb = lam::view_require_block(p);
        if (pb->scroller &&
            (pb->scroll()->overflow_x != CSS_VALUE_VISIBLE ||
             pb->scroll()->overflow_y != CSS_VALUE_VISIBLE)) {
            scroll_ancestor = p;
            break;
        }
    }

    if (!scroll_ancestor) {
        return;
    }

    ViewBlock* scroller = lam::view_require_block(scroll_ancestor);

    // Scrollport: scroller's content box at (0, 0) in scroller content coordinates.
    // At scroll position 0, the visible area starts at the content box origin.
    float sp_content_h = (float)scroller->height;
    float sp_content_w = (float)scroller->width;
    BoxMetrics scroller_box = layout_box_metrics(scroller);
    sp_content_h -= scroller_box.pad_border_v;
    sp_content_w -= scroller_box.pad_border_h;
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
            ViewBlock* pb = lam::view_require_block(p);
            offset_to_scroller_y += (float)pb->y;
            offset_to_scroller_x += (float)pb->x;
            if (pb->bound) {
                offset_to_scroller_y += pb->boundary()->padding.top;
                offset_to_scroller_x += pb->boundary()->padding.left;
                if (pb->boundary()->border) {
                    offset_to_scroller_y += pb->boundary()->border->width.top;
                    offset_to_scroller_x += pb->boundary()->border->width.left;
                }
            }
        }
    }

    // element position in scroller content coordinates
    float elem_top = (float)block->y + offset_to_scroller_y;
    float elem_left = (float)block->x + offset_to_scroller_x;
    float elem_bottom = elem_top + (float)block->height;
    float elem_right = elem_left + (float)block->width;

    float offset_x = 0, offset_y = 0;

    // sticky resolves the start constraint before the end constraint on each axis.
    offset_y = sticky_axis_offset(
        block->positionp()->has_top, block->positionp()->top,
        block->positionp()->has_bottom, block->positionp()->bottom,
        sp_top, sp_bottom, elem_top, elem_bottom);
    offset_x = sticky_axis_offset(
        block->positionp()->has_left, block->positionp()->left,
        block->positionp()->has_right, block->positionp()->right,
        sp_left, sp_right, elem_left, elem_right);

    // Constrain: element must stay within its containing block (parent).
    // Use parent content coordinates (element's y=0 is parent content top).
    ViewElement* parent = block->parent_view();
    if (parent && parent->is_block() && (offset_x != 0 || offset_y != 0)) {
        ViewBlock* cb = lam::view_require_block(parent);
        // containing block content area in parent-relative coordinates: [0, content_height]
        float cb_content_top = 0;
        float cb_content_left = 0;
        float cb_content_bottom = (float)cb->height;
        float cb_content_right = (float)cb->width;
        BoxMetrics cb_box = layout_box_metrics(cb);
        cb_content_bottom -= cb_box.pad_border_v;
        cb_content_right -= cb_box.pad_border_h;

        // clamp offset so element stays in containing block (in parent coords)
        float local_top = (float)block->y;
        float local_bottom = local_top + (float)block->height;
        float local_left = (float)block->x;
        float local_right = local_left + (float)block->width;

        offset_y = sticky_clamp_axis_offset(
            offset_y, local_top, local_bottom, cb_content_top, cb_content_bottom);
        offset_x = sticky_clamp_axis_offset(
            offset_x, local_left, local_right, cb_content_left, cb_content_right);
    }

    if (offset_x != 0 || offset_y != 0) {
        block->x += offset_x;
        block->y += offset_y;

        if (block->view_type == RDT_VIEW_INLINE) {
            offset_children_recursive(lam::view_require_element(block), offset_x, offset_y);
        }
    }
}

/**
 * Find the containing block for a positioned element
 * For relative/static: nearest block container ancestor
 * For absolute: nearest positioned ancestor or initial containing block
 * For fixed: viewport (initial containing block)
 */
ViewBlock* find_initial_containing_view_block(ViewBlock* element) {
    if (!element) return nullptr;
    ViewBlock* root = element;
    for (ViewElement* ancestor = element->parent_view(); ancestor; ancestor = ancestor->parent_view()) {
        ViewBlock* ancestor_block = lam::view_as_block(ancestor);
        if (ancestor_block) root = ancestor_block;
    }
    return root;
}

ViewBlock* find_positioned_containing_block(ViewElement* view) {
    for (ViewElement* ancestor = view ? view->parent_view() : nullptr;
         ancestor;
         ancestor = ancestor->parent_view()) {
        if (ancestor->view_type == RDT_VIEW_INLINE) {
            ViewSpan* ancestor_span = lam::view_require<RDT_VIEW_INLINE>(ancestor);
            if (ancestor_span->position &&
                ancestor_span->positionp()->position != CSS_VALUE_STATIC) {
                return lam::unsafe_view_block_api_span(ancestor_span);
            }
        } else if (ancestor->is_block()) {
            ViewBlock* ancestor_block = lam::view_require_block(ancestor);
            if (ancestor_block->position &&
                ancestor_block->positionp()->position != CSS_VALUE_STATIC) {
                return ancestor_block;
            }
        }
    }
    return nullptr;
}

ViewBlock* find_containing_block(ViewBlock* element, CssEnum position_type) {
    if (position_type == CSS_VALUE_FIXED) {
        // Fixed positioning uses viewport as containing block
        // For now, return the root block (will be enhanced for viewport support)
        return find_initial_containing_view_block(element);
    }

    if (position_type == CSS_VALUE_ABSOLUTE) {
        ViewBlock* positioned_ancestor = find_positioned_containing_block(element);
        if (positioned_ancestor) return positioned_ancestor;

        // No positioned ancestor found, use initial containing block (root)
        return find_initial_containing_view_block(element);
    }

    // For relative positioning, use nearest block container
    ViewElement* ancestor = element->parent_view();
    while (ancestor) {
        if (ancestor->is_block()) {
            return lam::view_require_block(ancestor);
        }
        ancestor = ancestor->parent_view();
    }

    return nullptr;
}

static void calculate_parent_to_cb_offset(ViewBlock* block, ViewBlock* containing_block, float* out_x, float* out_y) {
    float parent_to_cb_offset_x = 0;
    float parent_to_cb_offset_y = 0;
    ViewElement* walk_start = block->parent_view();
    ViewElement* containing_element = reinterpret_cast<ViewElement*>(containing_block);

    if (walk_start == containing_element) {
        *out_x = parent_to_cb_offset_x;
        *out_y = parent_to_cb_offset_y;
        return;
    }

    while (walk_start && !walk_start->is_block() && walk_start != containing_element) {
        walk_start = walk_start->parent_view();
    }

    if (walk_start && (walk_start->is_block() || walk_start == containing_element)) {
        ViewBlock* p = walk_start->view_type == RDT_VIEW_INLINE
            ? lam::unsafe_view_block_api_span(lam::view_require<RDT_VIEW_INLINE>(walk_start))
            : lam::view_require_block(walk_start);
        while (p && p != containing_block) {
            parent_to_cb_offset_x += p->x;
            parent_to_cb_offset_y += p->y;

            if (p->position && p->positionp()->position == CSS_VALUE_FIXED) {
                break;
            }
            if (p->position && p->positionp()->position == CSS_VALUE_ABSOLUTE) {
                ViewBlock* p_cb = find_positioned_containing_block(p);
                if (p_cb) {
                    p = p_cb;
                    continue;
                }
                break;
            }

            ViewElement* gp = p->parent_view();
            while (gp && !gp->is_block()) {
                gp = gp->parent_view();
            }
            if (gp && gp->is_block()) {
                p = lam::view_require_block(gp);
            } else {
                break;
            }
        }
    }

    if (containing_block && containing_block->parent_view() == nullptr &&
        block->position &&
        (block->positionp()->position == CSS_VALUE_ABSOLUTE ||
         block->positionp()->position == CSS_VALUE_FIXED)) {
        parent_to_cb_offset_x += containing_block->x;
        parent_to_cb_offset_y += containing_block->y;
    }

    *out_x = parent_to_cb_offset_x;
    *out_y = parent_to_cb_offset_y;
}

static TextDirection get_static_position_direction(ViewElement* parent) {
    TextDirection static_direction = TD_LTR;
    if (parent && parent->is_element()) {
        DomElement* parent_elem = lam::dom_require<DOM_NODE_ELEMENT>(parent);
        if (parent_elem->blk && parent_elem->block_mut()->direction == CSS_VALUE_RTL) {
            static_direction = TD_RTL;
        } else if (parent_elem->specified_style) {
            CssValue* direction = (CssValue*)style_tree_get_computed_value(
                parent_elem->specified_style, CSS_PROPERTY_DIRECTION,
                parent_elem->parent && parent_elem->parent->is_element() ?
                    lam::dom_require<DOM_NODE_ELEMENT>(parent_elem->parent)->specified_style : NULL);
            if (direction && direction->type == CSS_VALUE_TYPE_KEYWORD &&
                direction->data.keyword == CSS_VALUE_RTL) {
                static_direction = TD_RTL;
            }
        }
    }
    return static_direction;
}

static bool static_position_parent_uses_right_block_start(ViewElement* parent) {
    for (View* ancestor = parent; ancestor && ancestor->is_element();
         ancestor = ancestor->parent) {
        ViewBlock* block = lam::view_as_block(ancestor);
        if (block) {
            WritingMode writing_mode = layout_block_writing_mode(block);
            if (writing_mode == WM_VERTICAL_RL) return true;
            if (writing_mode == WM_VERTICAL_LR || writing_mode == WM_HORIZONTAL_TB) return false;
        }

        DomElement* element = ancestor->as_element();
        CssDeclaration* declaration = element && element->specified_style
            ? style_tree_get_declaration(element->specified_style, CSS_PROPERTY_WRITING_MODE)
            : nullptr;
        if (declaration && declaration->value &&
            declaration->value->type == CSS_VALUE_TYPE_KEYWORD) {
            return layout_writing_mode_from_css(declaration->value->data.keyword) == WM_VERTICAL_RL;
        }
    }
    return false;
}

static bool positioned_element_is_replaced(ViewBlock* block) {
    if (!block) return false;
    bool is_form_control =
        block->form_control();
    return block->display.inner == RDT_DISPLAY_REPLACED ||
        block->tag() == MARKUP_NAME_IMG || block->tag() == MARKUP_NAME_IFRAME ||
        block->tag() == MARKUP_NAME_VIDEO || block->tag() == MARKUP_NAME_EMBED ||
        (block->tag() == MARKUP_NAME_OBJECT && block->get_attribute("data")) ||
        is_form_control;
}

static bool positioned_axis_is_auto(ViewBlock* block, bool horizontal) {
    if (!block || !block->is_element()) return true;

    DomElement* element = block->as_element();
    CssDeclaration* size_decl = layout_specified_physical_size_declaration(
        element, horizontal);
    // Logical size aliases do not populate a physical declaration. Select the
    // cascade-winning alias so `block-size` is not mistaken for height:auto.
    // CSS-wide keywords resolve to the property's initial automatic value;
    // treating `initial` as a definite zero bypasses the abspos inset equation.
    return !size_decl || !size_decl->value ||
        layout_css_size_is_automatic(block, horizontal);
}

static float positioned_ratio_width_from_height(ViewBlock* block, float content_height) {
    if (!block || content_height <= 0.0f ||
        !positioned_axis_is_auto(block, true) ||
        positioned_element_is_replaced(block)) {
        return -1.0f;
    }
    float ratio = layout_preferred_aspect_ratio(block);
    return ratio > 0.0f ? content_height * ratio : -1.0f;
}

static float positioned_apply_automatic_min_width(LayoutContext* lycon,
                                                  ViewBlock* block,
                                                  float width,
                                                  const char* log_context) {
    if (!lycon || !block || !block->is_element() ||
        layout_explicit_min_axis_or(block, true, -1.0f) >= 0.0f ||
        (block->display.inner != CSS_VALUE_FLOW &&
         block->display.inner != CSS_VALUE_FLOW_ROOT) ||
        !block->as_element()->first_child) {
        return width;
    }
    IntrinsicSizes intrinsic = layout_measure_intrinsic_widths(
        lycon, block->as_element(), log_context, true);
    float automatic_min_width = layout_uses_border_box(block)
        ? layout_border_size_from_content_box(block, intrinsic.min_content, true)
        : intrinsic.min_content;
    return max(width, automatic_min_width);
}

static float positioned_inset_stretch_css_axis(ViewBlock* block, float containing_size,
    float start_inset, float end_inset, float start_margin, float end_margin,
    bool horizontal, float* border_box_size_out) {
    float border_box_size = max(containing_size - start_inset - end_inset -
        start_margin - end_margin, 0.0f);
    if (border_box_size_out) *border_box_size_out = border_box_size;
    return layout_uses_border_box(block) ? border_box_size
        : layout_content_size_from_border_box(block, border_box_size, horizontal);
}

static float positioned_stretch_axis(ViewBlock* block, float containing_size,
                                     float start_inset, float end_inset,
                                     bool has_start, bool has_end,
                                     float static_start, bool horizontal,
                                     float* available_out, float* border_out) {
    float used_start = has_start ? start_inset : 0.0f;
    float used_end = has_end ? end_inset : 0.0f;
    if (!has_start && !has_end) used_start = max(static_start, 0.0f);
    float available = containing_size - used_start - used_end;
    float border_size = layout_stretch_fit_border_box_size(block, available, horizontal);
    if (available_out) *available_out = available;
    if (border_out) *border_out = border_size;
    return layout_uses_border_box(block)
        ? border_size : layout_content_size_from_border_box(block, border_size, horizontal);
}

static bool distribute_abs_auto_margins(ViewBlock* block, bool horizontal,
                                        float remaining, TextDirection direction) {
    if (!block || !block->bound) return false;
    BoundaryProp* boundary = block->boundary_mut();
    CssEnum* start_type = horizontal ? &boundary->margin.left_type : &boundary->margin.top_type;
    CssEnum* end_type = horizontal ? &boundary->margin.right_type : &boundary->margin.bottom_type;
    float* start_margin = horizontal ? &boundary->margin.left : &boundary->margin.top;
    float* end_margin = horizontal ? &boundary->margin.right : &boundary->margin.bottom;
    bool auto_start = *start_type == CSS_VALUE_AUTO;
    bool auto_end = *end_type == CSS_VALUE_AUTO;
    if (!auto_start && !auto_end) return false;

    if (auto_start && auto_end) {
        float each = remaining / 2.0f;
        if (horizontal && each < 0.0f) {
            if (direction == TD_RTL) {
                *end_margin = 0.0f;
                *start_margin = remaining;
            } else {
                *start_margin = 0.0f;
                *end_margin = remaining;
            }
        } else {
            *start_margin = each;
            *end_margin = each;
        }
    } else if (auto_start) {
        *start_margin = horizontal ? remaining - *end_margin
                                   : max(remaining - *end_margin, 0.0f);
    } else {
        *end_margin = horizontal ? remaining - *start_margin
                                 : max(remaining - *start_margin, 0.0f);
    }
    return true;
}

static void resolve_abs_auto_margins_axis(ViewBlock* block, float containing_size,
                                          float content_size, bool horizontal,
                                          TextDirection direction) {
    if (!block || !block->position || !block->bound) return;
    PositionProp* position = block->position;
    bool has_start = horizontal ? position->has_left : position->has_top;
    bool has_end = horizontal ? position->has_right : position->has_bottom;
    if (!has_start || !has_end) return;

    float start_inset = horizontal ? position->left : position->top;
    float end_inset = horizontal ? position->right : position->bottom;
    float used_size = layout_uses_border_box(block) ? content_size
        : content_size + layout_boundary_padding_border_axis(block->bound, horizontal);
    float remaining = containing_size - start_inset - end_inset - used_size;
    distribute_abs_auto_margins(block, horizontal, remaining, direction);
}

static void positioned_finalize_auto_margins(ViewBlock* block, float containing_size,
                                             float content_size, bool horizontal,
                                             bool has_size, TextDirection direction) {
    if (!block || !block->position || !block->bound) return;
    bool has_start = horizontal ? block->positionp()->has_left : block->positionp()->has_top;
    bool has_end = horizontal ? block->positionp()->has_right : block->positionp()->has_bottom;
    if (has_size && has_start && has_end) {
        resolve_abs_auto_margins_axis(block, containing_size, content_size,
                                      horizontal, direction);
        return;
    }
    BoundaryProp* boundary = block->boundary_mut();
    CssEnum* start_type = horizontal ? &boundary->margin.left_type : &boundary->margin.top_type;
    CssEnum* end_type = horizontal ? &boundary->margin.right_type : &boundary->margin.bottom_type;
    if (*start_type == CSS_VALUE_AUTO) {
        if (horizontal) boundary->margin.left = 0.0f;
        else boundary->margin.top = 0.0f;
    }
    if (*end_type == CSS_VALUE_AUTO) {
        if (horizontal) boundary->margin.right = 0.0f;
        else boundary->margin.bottom = 0.0f;
    }
}

static void positioned_set_axis_position(ViewBlock* block, float border_offset,
                                          float containing_size, float content_size,
                                          bool horizontal) {
    if (!block || !block->position) return;
    LayoutAxis axis = horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    const PositionProp* position = block->positionp();
    bool has_start = horizontal ? position->has_left : position->has_top;
    bool has_end = horizontal ? position->has_right : position->has_bottom;
    float start = horizontal ? position->left : position->top;
    float end = horizontal ? position->right : position->bottom;
    float margin_start = layout_axis_margin_start(block->bound, axis);
    float margin_end = layout_axis_margin_end(block->bound, axis);
    float border_box_size = layout_uses_border_box(block) ? content_size
        : content_size + layout_padding_border_axis(block, horizontal);
    float value = border_offset + margin_start;
    if (has_start) value = border_offset + start + margin_start;
    else if (has_end) value = border_offset + containing_size - end - margin_end - border_box_size;
    layout_axis_set_pos(static_cast<ViewElement*>(block), axis, value);
}

static float calculate_static_line_x(BlockContext* pa_block, Linebox* pa_line,
    TextDirection static_direction, bool was_inline) {
    float line_x = was_inline ? pa_line->advance_x : pa_line->left;

    if (was_inline && line_x <= pa_line->left + 0.01f) {
        BlockContext* bfc = block_context_find_bfc(pa_block);
        if (bfc) {
            float static_y_bfc = pa_block->advance_y + pa_block->bfc_offset_y;
            float lh = pa_block->line_height > 0 ? pa_block->line_height : 16.0f;
            FloatAvailableSpace space = block_context_space_at_y(bfc, static_y_bfc, lh);

            float avail_left = space.left - pa_block->bfc_offset_x;
            float avail_right = space.right - pa_block->bfc_offset_x;
            avail_left = fmax(avail_left, pa_line->left);
            avail_right = fmin(avail_right, pa_line->right);

            CssEnum ta = pa_block->text_align;
            if (ta == CSS_VALUE_CENTER) {
                line_x = (avail_left + avail_right) / 2.0f;
            } else if ((ta == CSS_VALUE_RIGHT && static_direction == TD_LTR) ||
                       (ta == CSS_VALUE_LEFT && static_direction == TD_RTL) ||
                       (ta == CSS_VALUE_END)) {
                line_x = (static_direction == TD_LTR) ? avail_right : avail_left;
            } else {
                line_x = (static_direction == TD_LTR) ? avail_left : avail_right;
            }
        }
    }

    return line_x;
}

static float containing_block_padding_width(ViewBlock* cb, float* border_left_out) {
    float border_left = (cb->bound && cb->boundary_mut()->border) ? cb->boundary_mut()->border->width.left : 0.0f;
    float border_right = (cb->bound && cb->boundary_mut()->border) ? cb->boundary_mut()->border->width.right : 0.0f;
    if (border_left_out) *border_left_out = border_left;
    return cb->width - border_left - border_right;
}

static float positioned_final_content_axis(ViewBlock* block, float size, bool horizontal) {
    bool border_box = layout_uses_border_box(block);
    float constrained = (block->bound || border_box)
        ? layout_apply_min_max_axis(block, size, horizontal, false) : size;
    return border_box && block->bound
        ? layout_content_size_from_border_box(block, constrained, horizontal)
        : constrained;
}

static void recalculate_right_positioned_x(ViewBlock* block, ViewBlock* cb) {
    float border_left = 0.0f;
    float padding_width = containing_block_padding_width(cb, &border_left);
    float margin_right = block->bound ? block->boundary()->margin.right : 0.0f;
    block->x = border_left + padding_width - block->positionp()->right -
        margin_right - block->width;
}

// calculate absolute position based on containing block and offset properties
// Implements CSS 2.1 §10.3.7 (horizontal) and §10.6.4 (vertical) constraint equations
void calculate_absolute_position(LayoutContext* lycon, ViewBlock* block, ViewBlock* containing_block,
    BlockContext* pa_block, Linebox* pa_line) {

    // get containing block dimensions
    LayoutContainingBlock cb = layout_absolute_containing_block(lycon, containing_block);
    float cb_width = cb.padding_width;
    float cb_height = cb.padding_height;
    float border_offset_x = cb.padding_x;
    float border_offset_y = cb.padding_y;

    // CSS 2.1 Section 10.1: For absolutely positioned elements, if the containing block is
    // the initial containing block (ICB - i.e., the root element with no positioned ancestors),
    // the ICB is the viewport rectangle at (0,0) with viewport dimensions.
    // It is NOT the root element's padding box — the root element's borders must not be subtracted.
    bool is_icb = layout_is_initial_containing_block(lycon, containing_block);
    if (is_icb) {
        border_offset_x = 0.0f;
        border_offset_y = 0.0f;
    }

    // re-resolve percentage position values against the actual containing block.
    // for absolute positioned elements, percentages are relative to the padding box.
    layout_resolve_percent_offsets_for_child(block, cb, "abspos child");

    // re-resolve percentage width/height against the actual containing block
    layout_resolve_percent_size_for_child(lycon, block, cb, false, "abspos child");

    // abspos percentage margins and padding use the containing block's padding-box width.
    layout_reresolve_percentage_box(block, cb_width);

    // CSS 2.1 §10.3.8: For absolutely positioned replaced elements with
    // 'width: auto', use the intrinsic width. §10.6.5: Same for height.
    // Replaced elements include iframe (300x150), img (intrinsic from image).
    if (block->display.inner == RDT_DISPLAY_REPLACED) {
        NameId tag = block->tag();
        if (tag == MARKUP_NAME_IFRAME) {
            IntrinsicSize replaced_size = layout_measure_replaced(lycon, block, lycon->available_space);
            if (lycon->block.given_width < 0) {
                layout_store_given_axis(lycon, block,
                    replaced_size.max_width > 0.0f ? replaced_size.max_width : 300.0f,
                    true);
            }
            if (lycon->block.given_height < 0) {
                layout_store_given_axis(lycon, block,
                    replaced_size.max_height > 0.0f ? replaced_size.max_height : 150.0f,
                    false);
            }
        }
    }

    float content_width, content_height;
    // =========================================================================
    // HORIZONTAL AXIS: CSS 2.1 §10.3.7 constraint equation
    //   left + margin-left + border-left + padding-left + width +
    //   padding-right + border-right + margin-right + right = cb_width
    // =========================================================================
    // Check if width uses intrinsic sizing keywords (max-content, min-content, fit-content)
    bool is_intrinsic_width = block->blk &&
        (block->block()->given_width_type == CSS_VALUE_MAX_CONTENT ||
         block->block()->given_width_type == CSS_VALUE_MIN_CONTENT ||
         block->block()->given_width_type == CSS_VALUE_FIT_CONTENT);
    bool is_stretch_width = block->blk &&
        block->block()->given_width_type == CSS_VALUE_STRETCH;

    // CSS 2.1 §10.3.8 / §10.6.5: Absolutely positioned REPLACED elements
    // use intrinsic dimensions for auto width/height, not the constraint equation.
    // Form controls are semi-replaced in modern layout: their intrinsic sizes are
    // the auto fallback, but auto sizes with both insets stretch like non-replaced boxes.
    bool is_form_control_replaced =
        block->form_control();
    bool is_replaced = positioned_element_is_replaced(block);

    // CSS 2.1 §10.3.7: Detect containing block's direction for auto margin resolution
    TextDirection cb_direction = TD_LTR;
    if (containing_block->blk && containing_block->block_mut()->direction == CSS_VALUE_RTL) {
        cb_direction = TD_RTL;
    } else if (containing_block->specified_style) {
        CssValue* dir_val = (CssValue*)style_tree_get_computed_value(
            containing_block->specified_style, CSS_PROPERTY_DIRECTION,
            containing_block->parent && containing_block->parent->is_element() ?
                lam::dom_require<DOM_NODE_ELEMENT>(containing_block->parent)->specified_style : NULL);
        if (dir_val && dir_val->type == CSS_VALUE_TYPE_KEYWORD &&
            dir_val->data.keyword == CSS_VALUE_RTL) {
            cb_direction = TD_RTL;
        }
    }

    bool has_auto_margin_left = block->bound && block->boundary_mut()->margin.left_type == CSS_VALUE_AUTO;
    bool has_auto_margin_right = block->bound && block->boundary_mut()->margin.right_type == CSS_VALUE_AUTO;
    bool width_is_auto = positioned_axis_is_auto(block, true);
    bool stretch_form_width = is_form_control_replaced && width_is_auto &&
        block->positionp()->has_left && block->positionp()->has_right && !is_intrinsic_width;
    bool has_width = (lycon->block.given_width >= 0 && !is_intrinsic_width &&
                      !width_is_auto && !is_stretch_width);
    ViewElement* parent = block->parent_view();
    TextDirection static_direction = get_static_position_direction(parent);
    bool was_inline = false;
    was_inline = was_specified_inline(lam::dom_require<DOM_NODE_ELEMENT>(block));
    float parent_to_cb_offset_x = 0;
    float parent_to_cb_offset_y = 0;
    calculate_parent_to_cb_offset(block, containing_block, &parent_to_cb_offset_x, &parent_to_cb_offset_y);
    float static_line_x = (pa_block && pa_line)
        ? calculate_static_line_x(pa_block, pa_line, static_direction, was_inline)
        : 0.0f;
    float static_left = parent_to_cb_offset_x + static_line_x;

    float stretch_constraint_left = block->positionp()->has_left
        ? block->positionp()->left : 0.0f;
    float stretch_constraint_right = block->positionp()->has_right
        ? block->positionp()->right : 0.0f;
    if (!block->positionp()->has_left && !block->positionp()->has_right) {
        stretch_constraint_left = max(static_left - border_offset_x, 0.0f);
    }
    // stretch min/max in abspos uses the same remaining padding-box space as
    // a preferred stretch size; resolving against the full box ignores insets.
    layout_resolve_stretch_minmax_axis(block,
        cb_width - stretch_constraint_left - stretch_constraint_right,
        cb.has_definite_width, true);

    // First determine content_width: use CSS width if specified, otherwise calculate from constraints
    if (is_stretch_width) {
        content_width = positioned_stretch_axis(
            block, cb_width, block->positionp()->left, block->positionp()->right,
            block->positionp()->has_left, block->positionp()->has_right,
            static_left - border_offset_x, true, nullptr, nullptr);
        // Abspos stretch resolves after static insets are known, not as a
        // 0px specified width during the generic CSS declaration pass.
        block->ensure_block(lycon);
        layout_store_given_axis(lycon, block, content_width, true);
    } else if (has_width) {
        content_width = lycon->block.given_width;
    } else if (block->positionp()->has_left && block->positionp()->has_right &&
               !is_intrinsic_width && (!is_replaced || stretch_form_width)) {
        // CSS 2.1 §10.3.7: width is auto, both left and right specified.
        // Replaced elements use intrinsic width, except form controls stretch
        // when their CSS width is still auto.
        // Auto margins are treated as 0 when width is auto
        float margin_left = has_auto_margin_left ? 0 : (block->bound ? block->boundary()->margin.left : 0);
        float margin_right = has_auto_margin_right ? 0 : (block->bound ? block->boundary()->margin.right : 0);
        content_width = positioned_inset_stretch_css_axis(
            block, cb_width, block->positionp()->left, block->positionp()->right,
            margin_left, margin_right, true, nullptr);
        // CRITICAL: Store constraint-calculated width so finalize_block_flow knows width is fixed
        block->ensure_block(lycon);
        layout_store_given_axis(lycon, block, content_width, true);
        // When width is derived from constraints, auto margins become 0
        if (has_auto_margin_left && block->bound) block->boundary_mut()->margin.left = 0;
        if (has_auto_margin_right && block->bound) block->boundary_mut()->margin.right = 0;
    } else if (is_intrinsic_width) {
        IntrinsicSizes intrinsic = layout_measure_intrinsic_widths(
            lycon, lam::dom_require<DOM_NODE_ELEMENT>(block),
            "abspos intrinsic preferred width");
        float border_width = intrinsic.max_content;
        if (block->block()->given_width_type == CSS_VALUE_MIN_CONTENT) {
            border_width = intrinsic.min_content;
        } else if (block->block()->given_width_type == CSS_VALUE_FIT_CONTENT) {
            float margin_left = has_auto_margin_left ? 0.0f :
                (block->bound ? block->boundary()->margin.left : 0.0f);
            float margin_right = has_auto_margin_right ? 0.0f :
                (block->bound ? block->boundary()->margin.right : 0.0f);
            float inset_left = block->positionp()->has_left ? block->positionp()->left : 0.0f;
            float inset_right = block->positionp()->has_right ? block->positionp()->right : 0.0f;
            float available_width = max(cb_width - inset_left - inset_right -
                                        margin_left - margin_right, 0.0f);
            border_width = min(intrinsic.max_content,
                               max(intrinsic.min_content, available_width));
        }
        // Preferred intrinsic sizes are measured as border-box contributions;
        // convert once here so the later box-sizing adjustment sees the same
        // representation as a definite CSS width.
        content_width = layout_uses_border_box(block)
            ? border_width
            : layout_content_size_from_border_box(block, border_width, true);
    } else if (block->flex_item() && block->fi->aspect_ratio > 0 &&
               !(lycon->block.given_height >= 0) && block->blk && block->block_mut()->given_max_height > 0) {
        // CSS Sizing Level 4: abs-pos with aspect-ratio, auto width/height, and max-height
        // Derive width from max-height * aspect-ratio
        float max_h = block->block()->given_max_height;
        float content_h = layout_css_size_to_content_box(
            block->bound, layout_box_sizing(block), max_h, false);
        float derived_width = content_h * block->fi->aspect_ratio;
        if (layout_uses_border_box(block)) {
            content_width = layout_border_size_from_content_box(block, derived_width, true);
        } else {
            content_width = derived_width;
        }
        // The nested flex layout must see the ratio-transferred width as a used
        // size; otherwise it re-enters shrink-to-fit sizing and loses max-height's
        // aspect-ratio constraint before the absolute box is finalized.
        layout_store_given_axis(lycon, block, content_width, true);
    } else if (is_replaced && block->form_control() &&
               block->form->intrinsic_width > 0) {
        // CSS 2.1 §10.3.8: replaced form control with auto width → use intrinsic width
        IntrinsicSize form_size = layout_measure_form_control(lycon, block, lycon->available_space);
        content_width = form_size.max_width;
    } else {
        // CSS 2.1 §10.3.7: width is auto, at most one of left/right specified (non-replaced)
        // Use shrink-to-fit width = min(max(preferred_minimum_width, available_width), preferred_width)
        // where preferred_minimum_width = min-content, preferred_width = max-content,
        // available_width is the remaining containing-block padding-box width after
        // substituting the used static inset for auto left/right.
        float margin_left = has_auto_margin_left ? 0 : (block->bound ? block->boundary()->margin.left : 0);
        float margin_right = has_auto_margin_right ? 0 : (block->bound ? block->boundary()->margin.right : 0);
        float used_left = block->positionp()->has_left ? block->positionp()->left : 0.0f;
        float used_right = block->positionp()->has_right ? block->positionp()->right : 0.0f;
        if (!block->positionp()->has_left && !block->positionp()->has_right) {
            if (static_direction == TD_LTR) {
                used_left = static_left;
            } else {
                used_right = max(cb_width - static_left, 0.0f);
            }
        }
        float available_width = max(cb_width - used_left - used_right - margin_left - margin_right, 0.0f);
        if (block->display.inner == CSS_VALUE_TABLE && available_width > cb_width) {
            available_width = cb_width;
        }

        // Measure intrinsic widths (returns border-box sizes including element's padding+border).
        // Top/bottom insets establish the abspos block size before cyclic percentage
        // contributions are measured; expose that used content-box basis to descendants.
        IntrinsicSizes intrinsic = {0.0f, 0.0f};
        float intrinsic_height_basis = -1.0f;
        if (block->positionp()->has_top && block->positionp()->has_bottom &&
            positioned_axis_is_auto(block, false) &&
            (!is_replaced || (is_form_control_replaced && positioned_axis_is_auto(block, false)))) {
            bool intrinsic_auto_margin_top = block->bound &&
                block->boundary()->margin.top_type == CSS_VALUE_AUTO;
            bool intrinsic_auto_margin_bottom = block->bound &&
                block->boundary()->margin.bottom_type == CSS_VALUE_AUTO;
            float intrinsic_margin_top = intrinsic_auto_margin_top ? 0.0f :
                (block->bound ? block->boundary()->margin.top : 0.0f);
            float intrinsic_margin_bottom = intrinsic_auto_margin_bottom ? 0.0f :
                (block->bound ? block->boundary()->margin.bottom : 0.0f);
            intrinsic_height_basis = positioned_inset_stretch_css_axis(
                block, cb_height, block->positionp()->top, block->positionp()->bottom,
                intrinsic_margin_top, intrinsic_margin_bottom,
                false, nullptr);
        }
        // CSS Sizing 3 resolves cyclic percentages in margins and padding to
        // zero during intrinsic contributions; the abspos auto width is itself
        // the size that would otherwise provide that percentage basis.
        LayoutContainingBlockScope intrinsic_width_scope(
            lycon, LAYOUT_AXIS_X, -1.0f);
        if (intrinsic_height_basis >= 0.0f) {
            LayoutContainingBlockScope height_scope(
                lycon, LAYOUT_AXIS_Y, intrinsic_height_basis);
            intrinsic = layout_measure_intrinsic_widths(
                lycon, lam::dom_require<DOM_NODE_ELEMENT>(block), "abspos shrink-to-fit");
        } else {
            intrinsic = layout_measure_intrinsic_widths(
                lycon, lam::dom_require<DOM_NODE_ELEMENT>(block), "abspos shrink-to-fit");
        }
        float preferred_minimum = intrinsic.min_content;  // min-content width (border-box)
        float preferred = intrinsic.max_content;          // max-content width (border-box)

        // shrink-to-fit = min(max(min_content, available), max_content) — all in border-box
        // ceil to account for fractional text measurement vs integer table/block allocation
        float shrink_to_fit = ceilf(min(max(preferred_minimum, available_width), preferred));

        // The later box-sizing adjustment (line ~558) converts border-box → content-box
        // for border-box elements. So we must set content_width appropriately:
        // - border-box: content_width = border-box value (adjustment will subtract border+padding)
        // - content-box: content_width = content-box value (no adjustment)
        bool is_border_box = layout_uses_border_box(block);
        if (is_border_box) {
            content_width = max(shrink_to_fit, 0.0f);
        } else {
            content_width = layout_content_size_from_border_box(block, shrink_to_fit, true);
        }

    }

    // The absolute-position path does not enter normal block sizing, so resolve
    // intrinsic min/max keywords here before numeric clamping and inset solving.
    layout_block_resolve_intrinsic_width_constraints(lycon, block);
    content_width = layout_apply_min_max_axis(block, content_width, true, false);
    layout_block_resolve_intrinsic_height_constraints(lycon, block, content_width);

    // CSS 2.1 §10.4: Apply min-width/max-width constraints BEFORE position calculation.
    // Per spec, min-width overrides max-width when they conflict.
    // This must happen before computing x position, because right-positioned elements
    // use the element's own width to determine x (x = cb_width - right - margin - width).
    // If we clamp after position, right/bottom-positioned elements get wrong offsets.

    // CSS 2.1 §10.3.7: Solve auto margins for horizontal axis
    // When left, right, and width are all NOT auto, the equation is over-constrained.
    // Auto margins absorb the remaining space; if both are auto, they split it equally (centering).
    positioned_finalize_auto_margins(block, cb_width, content_width, true,
                                     has_width, cb_direction);

    // Now determine x position (relative to padding box, then add border offset)
    // For right-positioning, subtract the full border-box width (content + padding + border)
    // CSS width is already the border-box width when border-box sizing is active.
    // without an inset, this preliminary coordinate is replaced by static positioning later.
    positioned_set_axis_position(block, border_offset_x, cb_width, content_width, true);
    assert(content_width >= 0);

    // =========================================================================
    // VERTICAL AXIS: CSS 2.1 §10.6.4 constraint equation
    //   top + margin-top + border-top + padding-top + height +
    //   padding-bottom + border-bottom + margin-bottom + bottom = cb_height
    // =========================================================================
    bool has_auto_margin_top = block->bound && block->boundary_mut()->margin.top_type == CSS_VALUE_AUTO;
    bool has_auto_margin_bottom = block->bound && block->boundary_mut()->margin.bottom_type == CSS_VALUE_AUTO;

    bool height_is_auto = positioned_axis_is_auto(block, false);
    bool is_intrinsic_height = block->blk &&
        (block->block()->given_height_type == CSS_VALUE_MAX_CONTENT ||
         block->block()->given_height_type == CSS_VALUE_MIN_CONTENT ||
         block->block()->given_height_type == CSS_VALUE_FIT_CONTENT);
    bool is_stretch_height = block->blk &&
        block->block()->given_height_type == CSS_VALUE_STRETCH;
    bool stretch_form_height = is_form_control_replaced && height_is_auto &&
        block->positionp()->has_top && block->positionp()->has_bottom;
    bool has_height = (lycon->block.given_height >= 0 && !height_is_auto &&
                       !is_intrinsic_height && !is_stretch_height);

    float stretch_constraint_top = block->positionp()->has_top
        ? block->positionp()->top : 0.0f;
    float stretch_constraint_bottom = block->positionp()->has_bottom
        ? block->positionp()->bottom : 0.0f;
    if (!block->positionp()->has_top && !block->positionp()->has_bottom) {
        stretch_constraint_top = max(parent_to_cb_offset_y +
            (pa_block ? pa_block->advance_y : 0.0f) - border_offset_y, 0.0f);
    }
    layout_resolve_stretch_minmax_axis(block,
        cb_height - stretch_constraint_top - stretch_constraint_bottom,
        cb.has_definite_height, false);

    if (is_stretch_height) {
        content_height = positioned_stretch_axis(
            block, cb_height, block->positionp()->top, block->positionp()->bottom,
            block->positionp()->has_top, block->positionp()->has_bottom,
            parent_to_cb_offset_y + (pa_block ? pa_block->advance_y : 0.0f) - border_offset_y,
            false, nullptr, nullptr);
        // Abspos stretch resolves after static insets are known, not as a
        // 0px specified height during the generic CSS declaration pass.
        block->ensure_block(lycon);
        layout_store_given_axis(lycon, block, content_height, false);
    } else if (has_height) {
        content_height = lycon->block.given_height;
    } else if (layout_preferred_aspect_ratio(block) > 0.0f && content_width > 0.0f) {
        // CSS Sizing: a preferred aspect ratio transfers a definite width to
        // the auto height before abspos vertical constraint resolution.
        float aspect_ratio = layout_preferred_aspect_ratio(block);
        content_height = content_width / aspect_ratio;
        block->ensure_block(lycon);
        layout_store_given_axis(lycon, block, content_height, false);
        // The ratio supplies a provisional auto height; in-flow content may
        // still establish the used height during the abspos auto-size pass.
        block->blk->aspect_ratio_auto_height = block->first_child != nullptr;
    } else if (block->positionp()->has_top && block->positionp()->has_bottom &&
               !is_intrinsic_height &&
               (!is_replaced || stretch_form_height)) {
        // CSS 2.1 §10.6.4: height is auto, both top and bottom specified
        // Auto margins are treated as 0 when height is auto
        float margin_top = has_auto_margin_top ? 0 : (block->bound ? block->boundary()->margin.top : 0);
        float margin_bottom = has_auto_margin_bottom ? 0 : (block->bound ? block->boundary()->margin.bottom : 0);
        content_height = positioned_inset_stretch_css_axis(
            block, cb_height, block->positionp()->top, block->positionp()->bottom,
            margin_top, margin_bottom, false, nullptr);
        // CRITICAL: Store constraint-calculated height so finalize_block_flow knows height is fixed
        block->ensure_block(lycon);
        layout_store_given_axis(lycon, block, content_height, false);
        // When height is derived from constraints, auto margins become 0
        if (has_auto_margin_top && block->bound) block->boundary_mut()->margin.top = 0;
        if (has_auto_margin_bottom && block->bound) block->boundary_mut()->margin.bottom = 0;
    } else if (is_replaced && block->form_control() &&
               block->form->intrinsic_height > 0) {
        // CSS 2.1 §10.6.5: replaced form control with auto height → use intrinsic height
        IntrinsicSize form_size = layout_measure_form_control(lycon, block, lycon->available_space);
        content_height = form_size.max_height;
    } else {
        // shrink-to-fit: height will be determined by content after layout
        content_height = 0;
    }

    // CSS 2.1 §10.7: Apply min-height/max-height constraints BEFORE position calculation.
    // Same rationale as horizontal: bottom-positioned elements need the clamped height.
    content_height = layout_apply_min_max_axis(block, content_height, false, false);

    float preferred_aspect_ratio = layout_preferred_aspect_ratio(block);
    bool ratio_transfers_max_height = !has_width && !has_height &&
        preferred_aspect_ratio > 0.0f && !is_intrinsic_width && !is_replaced &&
        layout_explicit_max_axis_or(block, false, -1.0f) >= 0.0f;
    float ratio_width_from_height = has_height && !is_intrinsic_width
        ? positioned_ratio_width_from_height(block, content_height) : -1.0f;
    if (ratio_width_from_height >= 0.0f) {
        // CSS Sizing 4: a definite height transfers through the preferred ratio
        // before the abspos left/right constraint can stretch an auto width.
        content_width = ratio_width_from_height;
        // The automatic minimum is applied after ratio transfer; for example,
        // abspos-013's 100px child clamps a 50px transferred width back to 100px.
        content_width = layout_apply_min_max_axis(block, content_width, true, false);
        content_width = positioned_apply_automatic_min_width(
            lycon, block, content_width, "abspos aspect-ratio automatic minimum width");
        // Persist the transferred used width so post-content abspos auto sizing
        // does not replace the aspect-ratio result with a child's max-content width.
        block->ensure_block(lycon);
        layout_store_given_axis(lycon, block, content_width, true);
    } else if (ratio_transfers_max_height && content_height > 0.0f) {
        float aspect_width = content_height * preferred_aspect_ratio;
        if (aspect_width < content_width) {
            // A max-height clamp feeds back through the preferred ratio; leaving
            // the inset-stretched width would violate the paired used sizes.
            content_width = layout_apply_min_max_axis(block, aspect_width, true, false);
            content_width = positioned_apply_automatic_min_width(
                lycon, block, content_width, "abspos max-height ratio minimum width");
            block->ensure_block(lycon);
            layout_store_given_axis(lycon, block, content_width, true);
        }
    } else if (!has_width && preferred_aspect_ratio > 0.0f && content_height > 0.0f &&
               !(block->positionp()->has_left && block->positionp()->has_right && !is_intrinsic_width && !is_replaced)) {
        float aspect_width = content_height * preferred_aspect_ratio;
        if (aspect_width > content_width) {
            content_width = aspect_width;
        }
    }

    // CSS 2.1 §10.6.4: Solve auto margins for vertical axis
    // When top, bottom, and height are all NOT auto, auto margins absorb remaining space.
    positioned_finalize_auto_margins(block, cb_height, content_height, false,
                                     has_height, TD_LTR);

    // Now determine y position (relative to padding box, then add border offset)
    // CRITICAL: For bottom positioning, we need the border-box height (including padding/border)
    positioned_set_axis_position(block, border_offset_y, cb_height, content_height, false);
    assert(content_height >= 0);

    content_width = positioned_final_content_axis(block, content_width, true);
    content_height = positioned_final_content_axis(block, content_height, false);
    lycon->block.content_width = content_width;  lycon->block.content_height = content_height;

    if (block->bound) {
        BoxMetrics block_box = layout_box_metrics(block);
        block->width = content_width + block_box.pad_border_h;
        block->height = content_height + block_box.pad_border_v;
    } else {
        // no change to block->x, block->y, lycon->line.advance_x, lycon->block.advance_y
        block->width = content_width;  block->height = content_height;
    }

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
    if (!containing_block->position || !containing_block->positionp()->first_abs_child) return;

    // Compute containing block's padding box height (CSS 2.1 §10.1)
    LayoutContainingBlock cb = layout_containing_block_for_view(containing_block);
    float cb_height = cb.padding_height;
    if (cb_height <= 0) return;

    ViewBlock* child = containing_block->positionp()->first_abs_child;
    while (child) {
        // Re-resolve percentage height against the containing block's final used
        // height. The previous value may be positive but stale when the containing
        // block itself had a deferred percentage height.
        if (child->blk && !isnan(child->block()->given_height_percent)) {
            float new_given_height = child->block()->given_height_percent * cb_height / 100.0f;
            new_given_height = layout_apply_min_max_axis(child, new_given_height, false, false);
            child->blk->given_height = new_given_height;

            float content_height = new_given_height;
            bool is_border_box = layout_uses_border_box(child);
            if (is_border_box && child->bound) {
                content_height = layout_content_size_from_border_box(child, content_height, false);
            }

            if (child->bound) {
                child->height = content_height + layout_box_metrics(child).pad_border_v;
            } else {
                child->height = content_height;
            }
            float ratio_width = positioned_ratio_width_from_height(child, content_height);
            if (ratio_width >= 0.0f) {
                // Percentage-height re-resolution happens after the first abspos
                // pass; repeat the ratio transfer or the stale stretch width wins.
                ratio_width = layout_apply_min_max_axis(child, ratio_width, true, false);
                child->blk->given_width = ratio_width;
                float child_content_width = layout_uses_border_box(child) && child->bound
                    ? layout_content_size_from_border_box(child, ratio_width, true) : ratio_width;
                child->width = child->bound
                    ? child_content_width + layout_box_metrics(child).pad_border_h
                    : child_content_width;
                if (child->position->has_right && !child->position->has_left) {
                    recalculate_right_positioned_x(child, containing_block);
                }
            }
        }

        // Re-resolve percentage top
        if (child->position && child->positionp()->has_top && !isnan(child->positionp()->top_percent)) {
            float old_top = child->positionp()->top;
            child->position->top = child->position->top_percent * cb_height / 100.0f;
            if (child->positionp()->top != old_top) {
                child->y = cb.padding_y + child->positionp()->top + (child->bound ? child->boundary()->margin.top : 0);
            }
        }

        // Re-resolve percentage bottom
        if (child->position && child->positionp()->has_bottom && !isnan(child->positionp()->bottom_percent)) {
            child->position->bottom = child->position->bottom_percent * cb_height / 100.0f;
        }

        bool is_form_control = child->form_control();
        if (child->position && child->positionp()->has_top && child->positionp()->has_bottom &&
            positioned_axis_is_auto(child, false) &&
            (!positioned_element_is_replaced(child) || is_form_control)) {
            float margin_top = child->bound && child->boundary_mut()->margin.top_type != CSS_VALUE_AUTO
                ? child->boundary()->margin.top : 0.0f;
            float margin_bottom = child->bound && child->boundary_mut()->margin.bottom_type != CSS_VALUE_AUTO
                ? child->boundary()->margin.bottom : 0.0f;
            bool is_border_box = layout_uses_border_box(child);
            float css_height = positioned_inset_stretch_css_axis(
                child, cb_height, child->positionp()->top, child->positionp()->bottom,
                margin_top, margin_bottom, false, nullptr);
            css_height = layout_apply_min_max_axis(child, css_height, false, false);
            float content_height = is_border_box && child->bound
                ? layout_content_size_from_border_box(child, css_height, false) : css_height;
            float pad_border = child->bound ? layout_box_metrics(child).pad_border_v : 0.0f;
            child->height = content_height + pad_border;
            child->y = cb.padding_y + child->positionp()->top + margin_top;
            if (child->blk) child->blk->given_height = css_height;
            // an auto-height containing block defers inset stretching until its used height exists.
        }

        // If bottom is specified but not top, recompute y from bottom edge.
        // This must be unconditional: this function runs after an auto-height
        // containing block is finalized, so cb_height changed for ALL children,
        // not just those with percentage values.
        if (child->position && child->positionp()->has_bottom && !child->positionp()->has_top) {
            child->y = cb.padding_y + cb_height - child->positionp()->bottom
                - (child->bound ? child->boundary()->margin.bottom : 0) - child->height;
        }

        if (child->position && child->positionp()->first_abs_child) {
            re_resolve_abs_children_vertical(child);
        }

        child = child->position ? child->positionp()->next_abs_sibling : nullptr;
    }
}

void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line) {
    log_enter();
    // guard against deeply nested positioned elements (e.g., 200 nested position:fixed flex divs)
    // layout_abs_block bypasses layout_flow_node so its depth guard doesn't apply here
    lycon->depth++;
    if (lycon->depth >= MAX_LAYOUT_DEPTH) {
        log_error("layout_abs_block: depth %d exceeded, skipping %s", MAX_LAYOUT_DEPTH, elmt->source_loc());
        lycon->depth--;
        log_leave();
        return;
    }

    // find containing block
    ViewBlock* cb = find_containing_block(block, block->positionp()->position);
    if (!cb) { log_error("Missing containing block");  lycon->depth--;  log_leave();  return; }
    // link to containing block's float context
    if (cb->position) {
        if (!cb->positionp()->first_abs_child) {
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
    calculate_absolute_position(lycon, block, cb, pa_block, pa_line);

    // Load image for IMG elements - same as layout_block does for regular flow
    NameId elmt_name = block->tag();
    if (elmt_name == MARKUP_NAME_IMG) {
        const char *value = block->get_attribute("src");
        if (value) {
            size_t value_len = strlen(value);
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, value, value_len);
            if (!block->embed) {
                block->ensure_embed(lycon);
            }
            block->embed->img = load_image(lycon->ui_context, src->str);
            strbuf_free(src);
        }
        if (block->embed && block->embedp()->img) {
            ImageSurface* img = block->embedp()->img;
            // Image intrinsic dimensions are in CSS logical pixels
            float w = img->width;
            float h = img->height;

            // Adjust dimensions based on CSS constraints
            if (lycon->block.given_width < 0 && lycon->block.given_height < 0) {
                // Neither width nor height specified - use intrinsic dimensions
                // But respect max-width if set
                float max_w = block->blk ? block->block()->given_max_width : -1;
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

            // Recalculate position for right/bottom-positioned replaced elements.
            // calculate_absolute_position computed x/y using the pre-image width/height
            // (shrink-to-fit or 0 for auto), but the IMG sizing code may have changed
            // the dimensions via aspect ratio. Re-derive x/y from the new block size.
            if (block->positionp()->has_right && !block->positionp()->has_left) {
                recalculate_right_positioned_x(block, cb);
            }
            if (block->positionp()->has_bottom && !block->positionp()->has_top) {
                float cb_border_top = (cb->bound && cb->boundary_mut()->border) ? cb->boundary_mut()->border->width.top : 0;
                float cb_border_bottom = (cb->bound && cb->boundary_mut()->border) ? cb->boundary_mut()->border->width.bottom : 0;
                float cb_padding_height = cb->height - cb_border_top - cb_border_bottom;
                float margin_bottom = (block->bound) ? block->boundary()->margin.bottom : 0;
                block->y = cb_border_top + cb_padding_height - block->positionp()->bottom - margin_bottom - block->height;
            }

            // CSS 2.1 §10.6.5: Re-resolve vertical auto margins for replaced elements
            // now that we know the intrinsic height. calculate_absolute_position
            // skipped this because has_height was false (given_height=-1 for images).
            if (block->positionp()->has_top && block->positionp()->has_bottom && block->bound) {
                bool has_auto_mt = block->boundary()->margin.top_type == CSS_VALUE_AUTO;
                bool has_auto_mb = block->boundary()->margin.bottom_type == CSS_VALUE_AUTO;
                if (has_auto_mt || has_auto_mb) {
                    float cb_border_top = (cb->bound && cb->boundary_mut()->border) ? cb->boundary_mut()->border->width.top : 0;
                    float cb_border_bottom = (cb->bound && cb->boundary_mut()->border) ? cb->boundary_mut()->border->width.bottom : 0;
                    float cb_pad_height = cb->height - cb_border_top - cb_border_bottom;
                    float v_bp = layout_box_metrics(block).pad_border_v;
                    float used_height = block->height + v_bp;
                    float remaining = cb_pad_height - block->positionp()->top - block->positionp()->bottom - used_height;
                    distribute_abs_auto_margins(block, false, remaining, TD_LTR);
                    // Recalculate y with the resolved margins
                    block->y = cb_border_top + block->positionp()->top + block->boundary()->margin.top;
                }
            }

            // CSS 2.1 §10.3.8: Re-resolve horizontal auto margins for replaced elements
            if (block->positionp()->has_left && block->positionp()->has_right && block->bound) {
                bool has_auto_ml = block->boundary()->margin.left_type == CSS_VALUE_AUTO;
                bool has_auto_mr = block->boundary()->margin.right_type == CSS_VALUE_AUTO;
                if (has_auto_ml || has_auto_mr) {
                    float cb_border_left = 0.0f;
                    float cb_pad_width = containing_block_padding_width(cb, &cb_border_left);
                    float h_bp = layout_box_metrics(block).pad_border_h;
                    float used_width = block->width + h_bp;
                    float remaining = cb_pad_width - block->positionp()->left - block->positionp()->right - used_width;
                    // Determine containing block direction for negative margin handling
                    TextDirection cb_dir = TD_LTR;
                    if (cb->blk && cb->block_mut()->direction == CSS_VALUE_RTL) {
                        cb_dir = TD_RTL;
                    } else if (cb->specified_style) {
                        CssValue* dir_val = (CssValue*)style_tree_get_computed_value(
                            cb->specified_style, CSS_PROPERTY_DIRECTION,
                            cb->parent && cb->parent->is_element() ?
                                lam::dom_require<DOM_NODE_ELEMENT>(cb->parent)->specified_style : NULL);
                        if (dir_val && dir_val->type == CSS_VALUE_TYPE_KEYWORD &&
                            dir_val->data.keyword == CSS_VALUE_RTL) {
                            cb_dir = TD_RTL;
                        }
                    }
                    distribute_abs_auto_margins(block, true, remaining, cb_dir);
                    block->x = cb_border_left + block->positionp()->left + block->boundary()->margin.left;
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
    calculate_parent_to_cb_offset(block, cb, &parent_to_cb_offset_x, &parent_to_cb_offset_y);
    if (!block->positionp()->has_left && !block->positionp()->has_right) {
        block->position->has_static_parent_offset_x = true;
        block->position->static_parent_offset_x = parent_to_cb_offset_x;
    }
    if (!block->positionp()->has_top && !block->positionp()->has_bottom) {
        block->position->has_static_parent_offset_y = true;
        block->position->static_parent_offset_y = parent_to_cb_offset_y;
    }

    // CSS 2.1 §10.3.7: Detect direction of the static-position containing block.
    // The direction determines whether the static position is for 'left' (LTR) or 'right' (RTL).
    TextDirection static_direction = get_static_position_direction(parent);
    bool static_x_uses_right_block_start =
        static_position_parent_uses_right_block_start(parent);

    if (!block->positionp()->has_top && !block->positionp()->has_bottom) {
        // Calculate static position: pa_block->advance_y is relative to parent's content area
        // Add offset to convert to containing block coordinates
        float static_y = parent_to_cb_offset_y + pa_block->advance_y;
        // Add margin.top (if not already included)
        if (block->bound && block->boundary_mut()->margin.top > 0) {
            static_y += block->boundary()->margin.top;
        }
        block->y = static_y;
    }
    // Similarly for X when neither left nor right specified
    if (!block->positionp()->has_left && !block->positionp()->has_right) {
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
        float line_x = calculate_static_line_x(
            pa_block, pa_line, static_direction, was_inline);

        if (static_direction == TD_RTL) {
            float static_x = parent_to_cb_offset_x + line_x;
            if (block->bound && block->boundary_mut()->margin.left > 0) {
                static_x += block->boundary()->margin.left;
            }
            block->x = static_x;
        } else {
            // CSS 2.1 §10.3.7: When direction is LTR, set 'left' to the static position.
            float static_x = parent_to_cb_offset_x + line_x;
            if (block->bound && block->boundary_mut()->margin.left > 0) {
                static_x += block->boundary()->margin.left;
            }
            block->x = static_x;
        }
    }

    // Absolutely positioned elements establish a new BFC
    // CSS 2.2 Section 9.4.1: "Absolutely positioned elements ... establish new BFCs"
    lycon->block.is_bfc_root = true;
    lycon->block.establishing_element = block;
    block_context_reset_floats(&lycon->block);

    // Check if width uses intrinsic sizing keywords (max-content, min-content, fit-content)
    bool is_intrinsic_width = block->blk &&
        (block->block()->given_width_type == CSS_VALUE_MAX_CONTENT ||
         block->block()->given_width_type == CSS_VALUE_MIN_CONTENT ||
         block->block()->given_width_type == CSS_VALUE_FIT_CONTENT);
    bool is_intrinsic_height = block->blk &&
        (block->block()->given_height_type == CSS_VALUE_MAX_CONTENT ||
         block->block()->given_height_type == CSS_VALUE_MIN_CONTENT ||
         block->block()->given_height_type == CSS_VALUE_FIT_CONTENT);

    // Set available space for intrinsic sizing if needed
    if (is_intrinsic_width) {
        if (block->block()->given_width_type == CSS_VALUE_MAX_CONTENT) {
            lycon->available_space = AvailableSpace::make_max_content();
        } else if (block->block()->given_width_type == CSS_VALUE_MIN_CONTENT) {
            lycon->available_space = AvailableSpace::make_min_content();
        } else {
            // fit-content has already been clamped to its used width; wrapping
            // must use that definite content box instead of max-content mode.
            lycon->available_space.width = AvailableSize::make_definite(
                lycon->block.content_width);
        }
    } else if (is_intrinsic_height) {
        // A definite abspos width from inset constraints must replace the
        // parent measurement mode, otherwise normal text remains unwrapped.
        lycon->available_space.width = AvailableSize::make_definite(
            lycon->block.content_width);
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
        layout_float_element(lycon, block);
    }

    // Apply CSS clear property after float layout
    if (block->position && block->positionp()->clear != CSS_VALUE_NONE) {
        layout_clear_element(lycon, block);
    }

    // BFC height expansion to contain floats
    // CSS 2.2 Section 10.6.7: For BFC roots (including position:absolute),
    // the heights of floating descendants are taken into account
    float max_float_bottom = 0;
    if (lycon->block.is_bfc_root || lycon->block.establishing_element == block) {
        // Find the maximum bottom of all floated children (including margins)
        for (FloatBox* fb = lycon->block.left_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > max_float_bottom) {
                max_float_bottom = fb->margin_box_bottom;
            }
        }
        for (FloatBox* fb = lycon->block.right_floats; fb; fb = fb->next) {
            if (fb->margin_box_bottom > max_float_bottom) {
                max_float_bottom = fb->margin_box_bottom;
            }
        }
        // Also check lowest_float_bottom which may be set during child layout
        if (lycon->block.lowest_float_bottom > max_float_bottom) {
            max_float_bottom = lycon->block.lowest_float_bottom;
        }
    }

    // adjust block width and height based on content
    // CRITICAL: Check if this is a flex/grid container that already calculated its dimensions
    bool is_flex_container = (block->display.inner == CSS_VALUE_FLEX);
    bool is_grid_container = (block->display.inner == CSS_VALUE_GRID);
    // Flex/grid containers calculate their own width via shrink-to-fit in layout_flex_content/
    // layout_grid_content. Trust the flex-calculated width if the container has children or
    // border/padding (the flex algorithm handles both cases now that layout_block_content
    // dispatches to flex even for empty containers).
    bool has_flex_calculated_width = is_flex_container &&
        (block->first_child != nullptr || layout_box_metrics(block).pad_border_h > 0);
    bool has_grid_calculated_width = is_grid_container;
    bool is_table_container = block->display.inner == CSS_VALUE_TABLE;
    // Form controls (checkbox, radio, etc.) have intrinsic sizes set by layout_form_control.
    // Don't overwrite with flow-based auto-sizing (void elements have 0 flow content).
    bool has_form_intrinsic_width = block->form_control() && block->width > 0;

    // Width is auto-sized when no explicit width AND neither left+right constraints
    if (!(lycon->block.given_width >= 0 || (block->positionp()->has_left && block->positionp()->has_right))) {
        // Don't override flex/grid/form calculated width with flow-based auto-sizing
        if (!(has_flex_calculated_width || has_grid_calculated_width || has_form_intrinsic_width ||
              is_table_container)) {
            if (pre_layout_width > 0) {
            // CSS 2.1 §10.3.7: non-replaced abspos auto width is resolved by
            // shrink-to-fit before content layout. Wrapped line widths are an
            // effect of that used width, not a second sizing pass.
            bool has_child_pct_width = false;
            bool has_table_flow_child = false;
            bool has_percentage_spacing = layout_view_tree_has_percentage_spacing(block);
            for (View* ch = block->first_child; ch; ch = ch->next_sibling) {
                if (!ch->is_element()) continue;
                ViewBlock* child_block = lam::view_as_block(ch);
                if (!child_block) continue;
                if (child_block->blk && !isnan(child_block->block()->given_width_percent)) {
                    has_child_pct_width = true;
                }
                if (view_tree_has_table_flow(child_block)) {
                    has_table_flow_child = true;
                }
            }

            float final_width = pre_layout_width;
            if (!has_child_pct_width && !has_percentage_spacing &&
                lycon->block.max_width > 0.0f) {
                float padding_right = block->bound ? block->boundary()->padding.right : 0;
                float border_right = (block->bound && block->boundary_mut()->border) ? block->boundary_mut()->border->width.right : 0;
                float post_layout_flow_width = lycon->block.max_width + padding_right + border_right;
                if (post_layout_flow_width > final_width) {
                    final_width = post_layout_flow_width;
                }
                float table_flow_width = post_layout_flow_width;
                if (has_table_flow_child && table_flow_width > 0.0f && table_flow_width < final_width) {
                    final_width = table_flow_width;
                }
            }
            block->width = final_width;
        } else {
            // Note: max_width already includes left border + left padding from setup_inline
            // So we only need to add right padding and right border
            float flow_width = lycon->block.max_width;
            BoxMetrics flow_box = layout_box_metrics(block);

            // flow_width includes left pad+border from setup_inline
            float border_box_width = flow_width + flow_box.padding.right + flow_box.border.right;

            // CSS 2.1 §10.4: Apply min/max-width constraints to auto-sized width.
            // Must handle border-box vs content-box correctly:
            // - border-box: min/max are in border-box terms, compare against border-box
            // - content-box: min/max are in content terms, extract content first
            border_box_width = layout_apply_min_max_axis(
                block, border_box_width, true, true);
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
                        ViewBlock* cb = lam::view_as_block(ch);
                        if (!cb) continue;
                        if (cb->blk && !isnan(cb->block()->given_width_percent)) {
                            has_child_pct_width = true;
                            break;
                        }
                    }
                }
                if (has_child_pct_width) {
                    block->width = pre_layout_width;
                }
            }

            // CRITICAL FIX: Re-align text after shrink-to-fit width calculation
            // Text alignment during layout used the large initial width, so rect->x may have
            // a large offset from centering. We need to correct it now that we know final width.
            if (lycon->block.text_align == CSS_VALUE_CENTER || lycon->block.text_align == CSS_VALUE_RIGHT) {
                float final_content_width = block->width;
                if (block->bound) {
                    final_content_width -= layout_box_metrics(block).pad_border_h;
                }

                View* child = block->first_child;
                while (child) {
                    if (child->view_type == RDT_VIEW_TEXT) {
                        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
                        TextRect* rect = text->rect;
                        while (rect) {
                            float line_width = rect->width;
                            float padding_left = block->bound ? block->boundary()->padding.left : 0;
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
                            }
                            rect = rect->next;
                        }
                    }
                    child = child->next();
                }
            }
        }
    }
    }

    if (is_intrinsic_width && block->positionp()->has_left &&
        block->positionp()->has_right) {
        LayoutContainingBlock used_cb = layout_absolute_containing_block(lycon, cb);
        float used_content_width = layout_uses_border_box(block)
            ? block->width
            : layout_content_size_from_border_box(block, block->width, true);
        resolve_abs_auto_margins_axis(
            block, used_cb.padding_width, used_content_width,
            true, get_static_position_direction(cb));
        block->x = used_cb.padding_x + block->positionp()->left +
            (block->bound ? block->boundary()->margin.left : 0.0f);
    }

    // CSS 2.1 §10.3.7: For RTL direction with neither left nor right specified,
    // set 'right' to the static position, then solve for 'left'.
    // The hypothetical box's right margin edge determines the static 'right' value.
    // For inline-level elements, account for float avoidance and text-align.
    if ((static_direction == TD_RTL || static_x_uses_right_block_start) &&
        !block->positionp()->has_left && !block->positionp()->has_right) {
        bool was_inline_rtl = false;
        if (elmt->is_element()) {
            DomElement* elem = elmt->as_element();
            was_inline_rtl = was_specified_inline(elem);
        }
        // In RTL and vertical-rl, the static physical x edge is the line's
        // right edge; pa_line->advance_x tracks the left cursor instead.
        // This preserves vertical-rl's right-to-left block progression when
        // CSS Positioned resolves both physical horizontal insets as auto.
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
            }
        }

        float margin_right = (block->bound) ? block->boundary()->margin.right : 0;
        block->x = parent_to_cb_offset_x + line_right - block->width - margin_right;
    }

    // CSS 2.1 §10.3.7: When width is auto (shrink-to-fit) and 'right' is specified
    // but 'left' is auto, recalculate x after width is finalized.
    // The initial x from calculate_absolute_position used the available width,
    // not the final shrink-to-fit width.
    if (block->positionp()->has_right && !block->positionp()->has_left &&
        !(lycon->block.given_width >= 0 || (block->positionp()->has_left && block->positionp()->has_right))) {
        recalculate_right_positioned_x(block, cb);
    }

    // Height is auto-sized when no explicit height AND neither top+bottom constraints
    // CRITICAL: Skip auto-sizing for flex/grid containers - they calculate their own height
    bool has_flex_calculated_height = is_flex_container && block->height > 0;
    bool has_grid_calculated_height = is_grid_container && block->height > 0;
    // CSS 2.1 §10.6.5: Replaced elements (img, iframe) use intrinsic height
    // for auto height. The image-loading code above already set block->height
    // from intrinsic dimensions; don't overwrite it with flow-based auto-sizing.
    bool has_replaced_intrinsic_height = ((block->display.inner == RDT_DISPLAY_REPLACED) ||
        (block->form_control())) && block->height > 0;

    // CRITICAL: Use block->block()->given_height (canonical CSS value) instead of lycon->block.given_height
    // here, because lycon->block.given_height can be corrupted by child CSS style resolution
    // inside layout_block_inner_content (children's dom_node_resolve_style writes their own
    // given_height into lycon->block). block->block()->given_height is set once from CSS parsing
    // and from calculate_absolute_position for top+bottom constraints; it is not corrupted.
    // This mirrors the same pattern used in finalize_block_flow.
    float abs_block_given_height = layout_axis_has_given_size(block, false)
        ? layout_axis_given_size(block->block(), LAYOUT_AXIS_Y) : -1.0f;
    bool ratio_auto_height = block->blk && block->block()->aspect_ratio_auto_height;
    float ratio_auto_height_floor = 0.0f;
    if (ratio_auto_height) {
        float preferred_aspect_ratio = layout_preferred_aspect_ratio(block);
        bool ratio_uses_border_box = !layout_aspect_ratio_uses_content_box(block) &&
            layout_uses_border_box(block);
        float ratio_source_width = ratio_uses_border_box
            ? block->width : layout_content_size_from_border_box(block, block->width, true);
        float ratio_height = preferred_aspect_ratio > 0.0f
            ? ratio_source_width / preferred_aspect_ratio : 0.0f;
        ratio_auto_height_floor = ratio_uses_border_box
            ? layout_apply_min_max_axis(block, ratio_height, false, true)
            : layout_border_size_from_content_box(
                block, layout_apply_min_max_axis(block, ratio_height, false, false), false);
    }
    if (!((abs_block_given_height >= 0 && !ratio_auto_height) ||
          (block->positionp()->has_top && block->positionp()->has_bottom &&
           !is_intrinsic_height))) {
        // Don't override flex/grid calculated height with flow-based auto-sizing
        if (has_flex_calculated_height || has_grid_calculated_height || has_replaced_intrinsic_height) {
        } else {
            float flow_height = lycon->block.advance_y;
            // Note: advance_y already includes top border + top padding from setup_inline
            // So we only need to add bottom padding and bottom border
            BoxMetrics flow_box = layout_box_metrics(block);
            float trailing_height = flow_box.padding.bottom + flow_box.border.bottom;
            block->height = layout_apply_min_max_axis(
                block, flow_height + trailing_height, false, true);

            if (ratio_auto_height) {
                bool overflow_not_visible = block->scroller &&
                    (block->scroll()->overflow_x != CSS_VALUE_VISIBLE ||
                     block->scroll()->overflow_y != CSS_VALUE_VISIBLE);
                // CSS Sizing's ratio transfer is a preferred minimum for visible
                // overflow; an explicit min-height disables that content minimum.
                if (overflow_not_visible) {
                    block->height = ratio_auto_height_floor;
                } else if (layout_explicit_min_axis_or(block, false, -1.0f) >= 0.0f) {
                    float min_height = layout_explicit_min_axis_or(block, false, 0.0f);
                    float min_border_height = layout_uses_border_box(block)
                        ? min_height : layout_border_size_from_content_box(block, min_height, false);
                    block->height = max(ratio_auto_height_floor, min_border_height);
                } else {
                    block->height = max(block->height, ratio_auto_height_floor);
                }
            }

            if (is_intrinsic_height && block->positionp()->has_top &&
                block->positionp()->has_bottom) {
                float used_content_height = layout_uses_border_box(block)
                    ? block->height
                    : layout_content_size_from_border_box(block, block->height, false);
                LayoutContainingBlock used_cb = layout_absolute_containing_block(lycon, cb);
                resolve_abs_auto_margins_axis(
                    block, used_cb.padding_height, used_content_height, false, TD_LTR);
                block->y = used_cb.padding_y + block->positionp()->top +
                    (block->bound ? block->boundary()->margin.top : 0.0f);
            }
        }

        // BFC height expansion: if floats extend beyond flow content, expand height
        bool has_text_box_trim = block->blk && block->block_mut()->text_box_trim;
        if (!has_text_box_trim && max_float_bottom > block->height) {
            block->height = max_float_bottom;
        }

        // CRITICAL: Recalculate Y position when has_bottom without has_top and height is auto
        // The initial y was calculated with content_height=0, now we have the actual height
        if (block->positionp()->has_bottom && !block->positionp()->has_top) {
            LayoutContainingBlock used_cb = layout_absolute_containing_block(lycon, cb);
            float cb_height = used_cb.padding_height;
            float border_offset_y = used_cb.padding_y;
            float margin_bottom = block->bound ? block->boundary()->margin.bottom : 0;

            float new_y = border_offset_y + cb_height - block->positionp()->bottom - margin_bottom - block->height;
            block->y = new_y;
        }
    }
    lycon->depth--;
    log_leave();
}

static void finalize_static_positioned_abs_descendant(ViewBlock* block) {
    if (!block || !block->position) return;
    if (block->positionp()->position != CSS_VALUE_ABSOLUTE &&
        block->positionp()->position != CSS_VALUE_FIXED) return;

    bool needs_offset_delta_x = block->positionp()->has_static_parent_offset_x &&
        !block->positionp()->has_left && !block->positionp()->has_right;
    bool needs_offset_delta_y = block->positionp()->has_static_parent_offset_y &&
        !block->positionp()->has_top && !block->positionp()->has_bottom;
    if (!block->positionp()->static_x_needs_parent_offset &&
        !block->positionp()->static_y_needs_parent_offset &&
        !needs_offset_delta_x && !needs_offset_delta_y) return;

    ViewBlock* cb = find_containing_block(block, block->positionp()->position);
    if (!cb) return;

    float offset_x = 0;
    float offset_y = 0;
    calculate_parent_to_cb_offset(block, cb, &offset_x, &offset_y);

    if (block->positionp()->static_x_needs_parent_offset) {
        block->x += offset_x;
        block->position->static_x_needs_parent_offset = false;
        block->position->static_parent_offset_x = offset_x;
        block->position->has_static_parent_offset_x = true;
    } else if (needs_offset_delta_x) {
        float delta_x = offset_x - block->positionp()->static_parent_offset_x;
        if (fabs(delta_x) > 0.01f) {
            block->x += delta_x;
        }
        block->position->static_parent_offset_x = offset_x;
    }
    if (block->positionp()->static_y_needs_parent_offset) {
        block->y += offset_y;
        block->position->static_y_needs_parent_offset = false;
        block->position->static_parent_offset_y = offset_y;
        block->position->has_static_parent_offset_y = true;
    } else if (needs_offset_delta_y) {
        float delta_y = offset_y - block->positionp()->static_parent_offset_y;
        if (fabs(delta_y) > 0.01f) {
            block->y += delta_y;
        }
        block->position->static_parent_offset_y = offset_y;
    }

}

void layout_finalize_static_positioned_abs_descendants(ViewBlock* root) {
    if (!root || !root->is_element()) return;

    finalize_static_positioned_abs_descendant(root);

    ViewElement* elem = lam::view_require_element(root);
    for (View* child = elem->first_child; child; child = child->next_sibling) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (child_block) {
            layout_finalize_static_positioned_abs_descendants(child_block);
        } else if (child && child->is_element()) {
            ViewElement* child_elem = lam::view_require_element(child);
            for (View* nested = child_elem->first_child; nested; nested = nested->next_sibling) {
                ViewBlock* nested_block = lam::view_as_block(nested);
                if (nested_block) layout_finalize_static_positioned_abs_descendants(nested_block);
            }
        }
    }
}

void layout_shift_static_positioned_abs_descendants(ViewElement* root, float delta_x, float delta_y) {
    if (!root || (delta_x == 0.0f && delta_y == 0.0f)) return;

    for (View* child = root->first_child; child; child = child->next_sibling) {
        if (!child || !child->is_element()) continue;

        ViewBlock* child_block = lam::view_as_block(child);
        if (child_block) {
            bool is_abs_fixed = child_block->position &&
                (child_block->positionp()->position == CSS_VALUE_ABSOLUTE ||
                 child_block->positionp()->position == CSS_VALUE_FIXED);
            if (is_abs_fixed) {
                if (delta_x != 0.0f &&
                    !child_block->positionp()->has_left &&
                    !child_block->positionp()->has_right) {
                    child_block->x += delta_x;
                    if (child_block->positionp()->has_static_parent_offset_x) {
                        child_block->position->static_parent_offset_x += delta_x;
                    }
                }
                if (delta_y != 0.0f &&
                    !child_block->positionp()->has_top &&
                    !child_block->positionp()->has_bottom) {
                    child_block->y += delta_y;
                    if (child_block->positionp()->has_static_parent_offset_y) {
                        child_block->position->static_parent_offset_y += delta_y;
                    }
                }
                continue;
            }
        }

        layout_shift_static_positioned_abs_descendants(
            lam::view_require_element(child), delta_x, delta_y);
    }
}

/**
 * Check if an element has float properties
 * Per CSS 2.1 section 9.7: float is ignored for absolutely positioned elements
 * (position: absolute or position: fixed)
 */
bool element_has_float(ViewBlock* block) {
    if (!block || !block->position) return false;
    // Float is ignored for absolutely positioned or fixed elements
    if (block->positionp()->position == CSS_VALUE_ABSOLUTE ||
        block->positionp()->position == CSS_VALUE_FIXED) {
        return false;
    }
    return (block->positionp()->float_prop == CSS_VALUE_LEFT ||
            block->positionp()->float_prop == CSS_VALUE_RIGHT);
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

    // Get the parent's BlockContext - floats are positioned relative to their BFC container
    BlockContext* parent_ctx = lycon->block.parent;
    if (!parent_ctx) {
        log_error("[FLOAT_LAYOUT] No parent BlockContext for float positioning");
        return;
    }

    // Find the BFC root from the parent's context
    BlockContext* bfc = block_context_find_bfc(parent_ctx);
    if (!bfc) {
        bfc = parent_ctx;
    }

    // Get the containing block's content area offset (border + padding)
    ViewElement* parent_view = block->parent_view();
    float content_offset_x = 0;
    // Floats inside inline wrappers still use the nearest block ancestor as
    // their containing block; otherwise the wrapper drops border/padding and
    // makes same-line floats appear too wide to fit.
    ViewBlock* containing_block = layout_nearest_block_ancestor(parent_view);
    if (containing_block && containing_block->bound) {
        if (containing_block->boundary()->border) {
            content_offset_x += containing_block->boundary()->border->width.left;
        }
        content_offset_x += containing_block->boundary()->padding.left;
    }
    float margin_left = block->bound ? block->boundary()->margin.left : 0;
    float margin_right = block->bound ? block->boundary()->margin.right : 0;
    float margin_top = block->bound ? block->boundary()->margin.top : 0;
    float margin_bottom = block->bound ? block->boundary()->margin.bottom : 0;

    // Get the parent block's content width for positioning
    float parent_content_width = parent_ctx->content_width;
    // Calculate parent's position in BFC coordinates for coordinate conversion
    float parent_x_in_bfc = 0;
    float parent_y_in_bfc = 0;
    if (parent_view) {
        ViewElement* v = parent_view;
        while (v && v != bfc->establishing_element) {
            if (v->is_block()) {
                parent_x_in_bfc += v->x;
                parent_y_in_bfc += v->y;
            }
            ViewElement* pv = v->parent_view();
            if (!pv) break;
            v = pv;
        }
    }
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
        (block->positionp()->clear == CSS_VALUE_LEFT ||
         block->positionp()->clear == CSS_VALUE_RIGHT ||
         block->positionp()->clear == CSS_VALUE_BOTH);
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
            current_y_bfc = max_preceding_float_top;
        }
    }

    // CSS 2.2 §9.5.1 Rule 6/7/8: Find Y position where float fits horizontally
    // Start at current Y and move down until we find space
    float final_y_bfc = current_y_bfc;
    int max_iterations = 100;  // Prevent infinite loops

    bool float_is_inline_start =
        (parent_ctx->direction != CSS_VALUE_RTL &&
         block->positionp()->float_prop == CSS_VALUE_LEFT) ||
        (parent_ctx->direction == CSS_VALUE_RTL &&
         block->positionp()->float_prop == CSS_VALUE_RIGHT);
    bool initial_letter_clearance_applied = false;
    if (float_is_inline_start && parent_ctx->initial_letter_clears_later_start_floats &&
        parent_ctx->line_number > parent_ctx->initial_letter_origin_line_number) {
        float initial_margin_bottom_bfc = parent_y_in_bfc +
            parent_ctx->initial_letter_margin_box_bottom;
        if (final_y_bfc < initial_margin_bottom_bfc) {
            // An in-flow initial has no FloatBox, so CSS Inline 3 §7.9.3's
            // required clearance for later inline-start floats must be explicit.
            final_y_bfc = initial_margin_bottom_bfc;
            initial_letter_clearance_applied = true;
        }
    }
    if (block->blk) {
        block->block_mut()->initial_letter_float_clearance = initial_letter_clearance_applied;
    }

    // CSS 2.1 §9.5.1: Calculate containing block edges in BFC coordinates
    float containing_block_left_bfc = parent_x_in_bfc + content_offset_x;
    float containing_block_right_bfc = parent_x_in_bfc + content_offset_x + parent_content_width;
    float containing_block_width = parent_content_width;
    // CSS 2.1 §9.5.1 Rule 7 (paraphrased): A left float may not stick out at the right edge
    // of its containing block UNLESS it is already as far to the left as possible.
    // This means: if the float's margin box is wider than its containing block's content width,
    // the float can overflow (it's inherently too wide to fit). In that case, don't push it
    // down trying to find space — place it at its leftmost/rightmost position and let it overflow.
    bool float_wider_than_cb = (float_total_width > containing_block_width + 0.5f);
    while (max_iterations-- > 0) {
        // Query available space at this Y position (constrained by BFC edges and other floats)
        FloatAvailableSpace space = block_context_space_at_y(
            bfc, final_y_bfc, float_total_height, false, true);
        bool left_float = float_is_left(block);

        // CSS 2.1 §9.5.1: Compute effective available width
        // Rule 1: float's left outer edge >= containing block's left edge
        // Rule 7: float's right outer edge <= containing block's right edge
        // The BFC space (space.left/right) reflects other float intrusions.
        // When no other float constrains a side, the containing block boundary
        // is the only constraint — not the BFC element's content width, which
        // may be narrower than the containing block when intermediate elements
        // use negative margins (e.g., Bootstrap .row { margin: 0 -15px }).
        float effective_left, effective_right;
        if (left_float) {
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

        // Check if float fits at this Y position
        // Use tiny epsilon for float32 rounding in percentage-based layouts.
        // When sibling floats have percentage widths summing to 100%, both widths
        // round up slightly after double→float32 truncation, causing their sum to
        // exceed the parent width by ~0.0001px. Without this tolerance the second
        // float wraps to the next line. Browsers avoid this via fixed-point math.
        if (available_width >= float_total_width - 0.001f) {
            // Float fits here - determine X position
            block->x = float_position_x(space, left_float, parent_x_in_bfc,
                                        content_offset_x, parent_content_width,
                                        block, margin_left, margin_right);
            break;  // Found a valid position
        }

        // CSS 2.1 §9.5.1 Rule 7: "A left-floating box that has another left-floating box
        // to its left may not have its right outer edge to the right of its containing
        // block's right edge. (Loosely: a left float may not stick out at the right edge,
        // unless it is already as far to the left as possible.)"
        // If the float is wider than its containing block AND it's at its leftmost/rightmost
        // possible position (no same-direction float blocking it), place it here and overflow.
        if (float_wider_than_cb) {
            if (left_float) {
                bool at_leftmost = !space.has_left_float || (space.left <= containing_block_left_bfc + 0.5f);
                if (at_leftmost) {
                    // Rule 7 overflow stays on the containing-block edge; an
                    // existing float must not pull an over-wide box inward.
                    block->x = content_offset_x + margin_left;
                    break;
                }
            } else {
                bool at_rightmost = !space.has_right_float || (space.right >= containing_block_right_bfc - 0.5f);
                if (at_rightmost) {
                    block->x = content_offset_x + parent_content_width -
                        block->width - margin_right;
                    break;
                }
            }
        }

        // Float doesn't fit - need to shift down (CSS 2.2 §9.5.1 Rule 7)
        // Find the next float boundary to try
        float next_y = float_next_boundary(bfc, final_y_bfc);

        if (next_y == FLT_MAX || next_y <= final_y_bfc) {
            // No more floats below - position at current Y anyway
            // (this shouldn't happen if there's enough container width)
            // Position float at the edge even if it doesn't fit perfectly
            FloatAvailableSpace final_space = block_context_space_at_y(
                bfc, final_y_bfc, float_total_height, false, true);
            block->x = float_position_x(final_space, left_float, parent_x_in_bfc,
                                        content_offset_x, parent_content_width,
                                        block, margin_left, margin_right);
            break;
        }

        final_y_bfc = next_y;
    }
    if (max_iterations < 0) {
        log_warn("[RAD_CAP_FLOAT_PLACE] exhausted float placement search for %s at y=%.1f",
                 block->source_loc(), final_y_bfc);
    }

    // Convert final Y position back to parent-relative coordinates and apply
    float final_y_local = final_y_bfc - parent_y_in_bfc;
    bool clearance_applied = has_clear && block->bound && block->boundary_mut()->has_clearance;
    // Uncleared clear floats already include margin-top in block->y; when clearance
    // moves the float to a clear edge, the margin applies after that edge.
    float new_y = (has_clear && !clearance_applied) ? final_y_local : final_y_local + margin_top;

    if (new_y != block->y) {
        block->y = new_y;
    }

    // Note: Float is added to BlockContext by the caller (layout_block_content)
    // to ensure it's added to the parent's context, not the float's own context
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
        return;
    }

    // Get the current view being laid out
    View* current_view = lycon->view;
    if (!current_view) {
        return;
    }

    // Check if we're inside a floated element - if so, skip adjustment
    // (lines inside floats don't adjust for parent's float context)
    ViewElement* ancestor = current_view->is_element()
        ? lam::view_require_element(current_view)
        : lam::view_as_element(static_cast<View*>(current_view->parent));
    if (!ancestor) {
        return;
    }
    ViewBlock* container = bfc->establishing_element;
    bool found_container = false;
    while (ancestor) {
        if (ancestor == container) {
            found_container = true;
            break;
        }
        if (ancestor->is_block()) {
            ViewBlock* block = lam::view_require_block(ancestor);
            if (block->position && element_has_float(block)) {
                return;
            }
        }
        ancestor = ancestor->parent_view();
    }

    if (!found_container) {
        return;
    }

    // Use cached BFC offset from BlockContext
    float block_offset_x = lycon->block.bfc_offset_x;
    float block_offset_y = lycon->block.bfc_offset_y;

    // Convert current line Y to BFC coordinates
    float line_top_bfc = block_offset_y + lycon->block.advance_y;
    float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f;

    FloatAvailableSpace space = block_context_space_at_y(bfc, line_top_bfc, line_height, true);

    // If there's no float intrusion at this Y position, skip adjustment
    if (!space.has_left_float && !space.has_right_float) {
        return;
    }

    // Convert available space from BFC coordinates to local block coordinates
    float local_left = space.left - block_offset_x;
    float local_right = space.right - block_offset_x;

    // Clamp to the current block's line bounds
    float new_effective_left = max(local_left, lycon->line.left);
    float new_effective_right = min(local_right, lycon->line.right);

    // Apply the float intrusion to effective bounds
    if (space.has_left_float && new_effective_left > lycon->line.left) {
        lycon->line.effective_left = new_effective_left;
        lycon->line.has_float_intrusion = true;
        if (lycon->line.is_line_start && lycon->line.advance_x < new_effective_left) {
            lycon->line.advance_x = new_effective_left;
        }
    }
    if (space.has_right_float && new_effective_right < lycon->line.right) {
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
        (block->positionp()->clear != CSS_VALUE_LEFT &&
         block->positionp()->clear != CSS_VALUE_RIGHT &&
         block->positionp()->clear != CSS_VALUE_BOTH)) {
        return;
    }

    // Find BFC using the PARENT's BlockContext
    // The current lycon->block is for the element being cleared, but floats are tracked
    // in the parent's context (or BFC root)
    BlockContext* parent_ctx = lycon->block.parent;
    if (!parent_ctx) {
        return;
    }

    BlockContext* bfc = block_context_find_bfc(parent_ctx);
    if (!bfc) {
        return;
    }

    // Find the Y position where clear can be satisfied using BlockContext API
    // clear_y is in BFC coordinates (relative to BFC establishing element's content area)
    float clear_y_bfc = block_context_clear_y(bfc, block->positionp()->clear);

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
        if (block->bound) {
            block->bound->has_clearance = true;
        }
        if (!is_float && lycon->block.parent) {
            lycon->block.parent->advance_y += delta;
        }
    }
}
