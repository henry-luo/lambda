#include "layout.hpp"
#include "view.hpp"

#include "../lib/tagged.hpp"
#include "../lambda/input/css/css_style_node.hpp"

#include <math.h>
#include <string.h>

static bool layout_css_value_has_percentage_impl(const CssValue* value, bool nonzero_only) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        return !nonzero_only || fabs(value->data.percentage.value) > 0.000001;
    }
    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++) {
            if (layout_css_value_has_percentage_impl(
                    value->data.list.values[i], nonzero_only)) return true;
        }
        return false;
    }
    if (value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function) return false;
    const CssFunction* function = value->data.function;
    if (!function->name) return false;
    // Variables and environment values may introduce a percentage after parsing.
    if (strcmp(function->name, "var") == 0 || strcmp(function->name, "env") == 0 ||
        strcmp(function->name, "attr") == 0) return true;
    for (int i = 0; i < function->arg_count; i++) {
        if (layout_css_value_has_percentage_impl(function->args[i], nonzero_only)) return true;
    }
    return false;
}

bool layout_css_value_has_percentage(const CssValue* value) {
    return layout_css_value_has_percentage_impl(value, false);
}

bool layout_css_value_has_nonzero_percentage(const CssValue* value) {
    return layout_css_value_has_percentage_impl(value, true);
}

bool layout_resolve_percentage_value(const CssValue* value, float percentage_base, float* out) {
    if (!value || !out || percentage_base < 0.0f) return false;
    if (value->type != CSS_VALUE_TYPE_PERCENTAGE) return false;
    *out = (float)(value->data.percentage.value / 100.0) * percentage_base;
    return true;
}

bool layout_resolve_deferred_percentage(float percent, float percentage_base, float* out) {
    if (!out || isnan(percent) || percentage_base <= 0.0f) return false;
    *out = percent * percentage_base / 100.0f;
    return true;
}

bool layout_apply_deferred_percentage(float percent, float percentage_base, float* target, float* resolved) {
    float value = 0.0f;
    if (!target || !layout_resolve_deferred_percentage(percent, percentage_base, &value)) return false;
    *target = value;
    if (resolved) *resolved = value;
    return true;
}

static void layout_apply_percentage_spacing_candidate(ViewBlock* block, int side,
                                                      const CssValue* value, int64_t priority,
                                                      float inline_base, bool margin, LayoutContext* lycon) {
    if (!block || !block->bound || !value) return;
    float resolved = 0.0f;
    if (!layout_resolve_percentage_value(value, inline_base, &resolved)) {
        if (!lycon || !layout_css_value_has_percentage(value)) return;
        // table padding must re-evaluate the winning math value at the final inline basis too.
        LayoutContainingBlockScope percentage_base(lycon, LAYOUT_AXIS_X, inline_base);
        resolved = resolve_length_value(lycon,
            margin ? CSS_PROPERTY_MARGIN : CSS_PROPERTY_PADDING, value);
        if (!isfinite(resolved)) return;
        if (!margin) resolved = fmaxf(0.0f, resolved);
    }

    Spacing* spacing = margin ? (Spacing*)&block->boundary_mut()->margin : &block->boundary_mut()->padding;
    if (side < 0 || side >= 4) return;
    float* values[4] = {&spacing->top, &spacing->right, &spacing->bottom, &spacing->left};
    int64_t* priorities[4] = {&spacing->top_specificity, &spacing->right_specificity,
                              &spacing->bottom_specificity, &spacing->left_specificity};
    *values[side] = resolved;
    *priorities[side] = priority;
    if (margin) {
        CssEnum* types[4] = {&block->boundary_mut()->margin.top_type,
                             &block->boundary_mut()->margin.right_type,
                             &block->boundary_mut()->margin.bottom_type,
                             &block->boundary_mut()->margin.left_type};
        *types[side] = CSS_VALUE__PERCENTAGE;
    }
}

static void layout_reresolve_percentage_spacing(ViewBlock* block, float inline_base, bool margin,
                                                LayoutContext* lycon) {
    if (!block || !block->bound || !block->specified_style || inline_base < 0.0f) return;

    LayoutCssBoxCandidates candidates = {};
    if (!lycon) candidates.collect(block->specified_style, margin);
    LayoutViewScope owner_scope(lycon);
    LayoutFontScope font_scope(lycon);
    if (lycon) {
        // final percentage updates must resolve the same owner's variables as intrinsic measurement.
        lycon->view = lam::up(static_cast<View*>(block));
        lycon->elmt = lam::up(static_cast<DomNode*>(block));
        // font-relative math uses the receiving cell's font before its content layout begins.
        if (block->font) setup_font(lycon->ui_context, &lycon->font, block->font);
    }

    for (int side = 0; side < 4; side++) {
        CssPropertyCode property = radiant_box_side_property(
            margin ? CSS_PROPERTY_MARGIN : CSS_PROPERTY_PADDING, (CssBoxSide)side);
        CssDeclaration* declaration = lycon
            ? layout_cascaded_physical_declaration(block->as_element(), property)
            : candidates.sides[side].decl.get();
        if (!declaration || !declaration->value) continue;
        CssComputedValueScratch scratch(lycon, declaration->property_code, declaration->value, true);
        const CssValue* value = lycon
            ? layout_resolve_box_component_value(lycon, block->as_element(), declaration, (CssBoxSide)side)
            : candidates.sides[side].value.get();
        layout_apply_percentage_spacing_candidate(block, side, value,
            get_cascade_priority(declaration), inline_base, margin, lycon);
    }
}

void layout_reresolve_percentage_box(ViewBlock* block, float inline_base, LayoutContext* lycon) {
    layout_reresolve_percentage_spacing(block, inline_base, false, lycon);
    layout_reresolve_percentage_spacing(block, inline_base, true, lycon);
}

void layout_reresolve_percentage_box(ViewBlock* block, float inline_base) {
    layout_reresolve_percentage_box(block, inline_base, nullptr);
}

static bool layout_element_has_percentage_spacing(ViewBlock* block) {
    if (!block || !block->specified_style) return false;

    for (int pass = 0; pass < 2; pass++) {
        bool margin = pass != 0;
        LayoutCssBoxCandidates candidates = {};
        candidates.collect(block->specified_style, margin);
        if (candidates.has_percentage()) {
            return true;
        }
    }
    return false;
}

bool layout_view_tree_has_percentage_spacing(View* root) {
    if (!root) return false;
    if (root->is_element() &&
        layout_element_has_percentage_spacing(lam::view_as_block(root))) {
        return true;
    }
    DomElement* element = root->is_element() ? root->as_element() : nullptr;
    for (DomNode* child = element ? element->first_child : nullptr;
         child; child = child->next_sibling) {
        if (layout_view_tree_has_percentage_spacing((View*)child)) return true;
    }
    return false;
}

float layout_block_used_content_size(ViewBlock* block, bool horizontal, bool require_positive) {
    if (!block) return -1.0f;
    float border_size = horizontal ? block->width : block->height;
    if (border_size < 0.0f || (require_positive && border_size <= 0.0f)) {
        return -1.0f;
    }
    return layout_content_size_from_border_box(block, border_size, horizontal);
}

float layout_block_given_content_size(ViewBlock* block, bool horizontal) {
    if (!block || !block->blk) return -1.0f;
    LayoutAxisRefs axis(block->block_mut(), horizontal);
    float css_size = *axis.given;
    if (css_size < 0.0f) return -1.0f;
    float content_size = layout_css_size_to_content_box(
        block->bound, layout_box_sizing(block), css_size, horizontal);
    return content_size >= 0.0f ? content_size : 0.0f;
}

float layout_block_declared_content_size(LayoutContext* lycon, ViewBlock* block, CssPropertyCode property, bool horizontal) {
    if (!lycon || !block || !block->specified_style) return -1.0f;
    CssDeclaration* decl = style_tree_get_declaration(block->specified_style, property);
    if (!decl || !decl->value) return -1.0f;

    float declared_size = resolve_length_value(lycon, property, decl->value);
    if (isnan(declared_size) || declared_size < 0.0f) return -1.0f;

    CssEnum box_sizing = layout_box_sizing(block);
    float content_size = layout_css_size_to_content_box(block->bound, box_sizing, declared_size, horizontal);
    return content_size >= 0.0f ? content_size : 0.0f;
}

bool layout_css_size_is_automatic(ViewBlock* block, bool horizontal) {
    DomElement* element = block ? block->as_element() : nullptr;
    CssDeclaration* declaration = layout_specified_physical_size_declaration(element, horizontal);
    if (!declaration || !declaration->value) return true;
    if (declaration->value->type != CSS_VALUE_TYPE_KEYWORD) return false;
    CssEnum value = declaration->value->data.keyword;
    return value == CSS_VALUE_AUTO || value == CSS_VALUE_INITIAL ||
        value == CSS_VALUE_UNSET || value == CSS_VALUE_REVERT;
}

bool layout_percentage_height_basis_is_algorithmically_definite(ViewBlock* containing_block,
                                                                 LayoutContext* context) {
    if (!containing_block) return false;
    const BlockContext* parent_context = context ? context->block.parent.get() : nullptr;
    FlexContainerLayout* flex = context ? context->flex_container.get() : nullptr;
    if (parent_context && parent_context->flow_box == containing_block &&
        parent_context->given_height >= 0.0f &&
        containing_block->parent_item_kind() != DomElement::PARENT_ITEM_FLEX) {
        // detached/page constraints are definite even when the DOM ancestor's CSS height is auto.
        return true;
    }
    if (parent_context && parent_context->flow_box == containing_block &&
        parent_context->given_height >= 0.0f &&
        layout_used_preferred_aspect_ratio(containing_block) > 0.0f) return true;
    if (containing_block->flex_item() &&
        !layout_view_is_abs_or_fixed(containing_block) && flex && parent_context &&
        parent_context->flow_box == containing_block && parent_context->given_height >= 0.0f &&
        containing_block->parent_view() == flex->container) {
        // post-flex constraints are definite even when the item's CSS height is auto;
        // unrelated outer flex state must not define this containing block's percentages.
        bool definite = is_main_axis_horizontal(flex)
            ? flex_item_will_stretch_cross_axis(containing_block, flex)
            : flex->main_axis_available_size_is_definite;
        if (definite) return true;
    }
    // a ratio-transferred block height supplies a definite percentage basis
    // while its final auto height may still grow around its content.
    if (containing_block->blk && containing_block->block()->aspect_ratio_auto_height) return true;
    if (containing_block->height <= 0.0f) return false;
    // A table-cell used height is established by row/table sizing even when its
    // own CSS height is auto; percentage descendants use that content box.
    if (containing_block->view_type == RDT_VIEW_TABLE_CELL) return true;

    if (!containing_block->fi || !containing_block->parent ||
        !containing_block->parent->is_block()) return false;
    ViewBlock* parent_block = lam::view_as_block(containing_block->parent);
    FlexProp* parent_flex = parent_block->embed && parent_block->embedp()->flex
        ? parent_block->embedp()->flex : nullptr;
    if (!parent_flex) return false;

    bool parent_height_is_definite = !layout_block_has_automatic_size(parent_block, false);
    if (!parent_height_is_definite && parent_block->blk &&
        parent_block->block()->given_height >= 0.0f &&
        parent_block->block()->given_height_type != CSS_VALUE_AUTO) {
        parent_height_is_definite = true;
    }
    if (!parent_height_is_definite) return false;
    // Flex stretch makes the cross size definite; a flex-assigned main size is
    // likewise definite when physical height is the container's main axis.
    if (is_main_axis_horizontal(parent_flex)) {
        int align_type = (int)containing_block->fi->align_self != ALIGN_AUTO
            ? containing_block->fi->align_self : parent_flex->align_items;
        bool auto_cross_margin = containing_block->bound &&
            (containing_block->boundary()->margin.top_type == CSS_VALUE_AUTO ||
             containing_block->boundary()->margin.bottom_type == CSS_VALUE_AUTO);
        return align_type == ALIGN_STRETCH && !auto_cross_margin;
    }
    return containing_block->fi->main_size_from_flex;
}

bool layout_percentage_height_is_automatic(ViewBlock* block, LayoutContext* context) {
    if (!block) return true;
    // css 2.1 §10.5 exempts abspos percentages from the auto-height
    // ancestor rule; a resolved viewport must not revert to its ratio.
    if (layout_view_is_abs_or_fixed(block)) return false;
    if (block->block()->percentage_height_resolved_late) {
        // A positioned percentage can become definite after its containing
        // block finishes; retain that used-value definiteness for descendants.
        return false;
    }
    // A percentage block size is automatic while its containing block is auto;
    // treating a provisional intrinsic height as definite feeds aspect-ratio backward.
    ViewBlock* containing_block = layout_nearest_block_ancestor(block->parent_view());
    bool is_root_percentage_height = !containing_block && block->doc &&
        (block->doc->root == static_cast<DomNode*>(block) ||
         (block->doc->view_tree && block->doc->view_tree->root == static_cast<View*>(block)));
    if (!is_root_percentage_height &&
        (layout_block_has_automatic_size(containing_block, false) &&
         !layout_percentage_height_basis_is_algorithmically_definite(containing_block, context))) {
        return true;
    }
    return false;
}

bool layout_block_has_automatic_size(ViewBlock* block, bool horizontal) {
    if (!block || !block->blk) return true;
    LayoutAxisRefs axis(block->block_mut(), horizontal);
    float given_size = *axis.given;
    CssEnum given_size_type = *axis.given_type;
    if (given_size < 0.0f || given_size_type == CSS_VALUE_AUTO) return true;

    const CssDeclaration* specified_size = block->is_element()
        ? layout_specified_physical_size_declaration(block->as_element(), horizontal) : nullptr;
    if (specified_size && specified_size->value) {
        if (!horizontal && specified_size->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            if (layout_percentage_height_is_automatic(block)) return true;
        }
        return layout_css_size_is_automatic(block, horizontal);
    }

    const char* html_size = block->get_attribute(horizontal ? "width" : "height");
    // An aspect ratio supplies a used size for an otherwise auto box; it must
    // not become an authored specified-size suggestion during flex sizing.
    return !html_size || !html_size[0];
}

bool layout_block_has_automatic_height(ViewBlock* block) {
    return layout_block_has_automatic_size(block, false);
}

static float orthogonal_fixed_size_limit(ViewBlock* block, bool horizontal, bool minimum) {
    if (!block || !block->blk || !block->is_element()) return -1.0f;
    CssDeclaration* declaration = layout_specified_physical_minmax_size_declaration(
        block->as_element(), horizontal, minimum);
    const CssValue* value = declaration ? declaration->value : nullptr;
    if (!value || layout_css_value_has_percentage(value) ||
        (value->type != CSS_VALUE_TYPE_LENGTH && value->type != CSS_VALUE_TYPE_NUMBER &&
         value->type != CSS_VALUE_TYPE_FUNCTION)) return -1.0f;
    float limit = minimum ? layout_explicit_min_axis_or(block, horizontal, -1.0f)
                          : layout_explicit_max_axis_or(block, horizontal, -1.0f);
    return limit < 0.0f ? limit : layout_content_size_if_border_box(block, limit, horizontal);
}

float layout_orthogonal_inline_basis(LayoutContext* lycon, ViewBlock* block) {
    if (!lycon || !block) return 0.0f;
    bool horizontal = !layout_block_inline_axis_is_vertical(block);
    ViewBlock* parent = layout_nearest_block_ancestor(block->parent_view());
    const BlockContext* context = lycon->block.parent;
    float given_size = context ? (horizontal ? context->given_width : context->given_height) : -1.0f;
    if (parent && context && given_size >= 0.0f &&
        (!layout_block_has_automatic_size(parent, horizontal) ||
         (!horizontal && layout_percentage_height_basis_is_algorithmically_definite(parent, lycon)))) {
        return max(horizontal ? context->content_width : context->content_height, 0.0f);
    }
    // css writing modes §7.3.1: only the containing block's fixed maximum,
    // nearest scrollport and initial containing block bound indefinite space.
    float fallback = horizontal ? lycon->width : lycon->height;
    float parent_maximum = orthogonal_fixed_size_limit(parent, horizontal, false);
    if (parent_maximum >= 0.0f) {
        fallback = min(fallback, max(parent_maximum, orthogonal_fixed_size_limit(parent, horizontal, true)));
    }
    for (ViewElement* ancestor = block->parent_view(); ancestor;
         ancestor = ancestor->parent_view()) {
        ViewBlock* scrollport = lam::view_as_block(ancestor);
        if (!scrollport || !layout_block_establishes_scroll_container(scrollport)) continue;
        float limit = !layout_block_has_automatic_size(scrollport, horizontal)
            ? layout_block_given_content_size(scrollport, horizontal) : -1.0f;
        float maximum = orthogonal_fixed_size_limit(scrollport, horizontal, false);
        if (maximum >= 0.0f) limit = limit < 0.0f ? maximum : min(limit, maximum);
        if (limit >= 0.0f) {
            fallback = min(fallback, max(limit, orthogonal_fixed_size_limit(scrollport, horizontal, true)));
        }
        break;
    }
    return max(fallback, 0.0f);
}

WritingMode layout_writing_mode_from_css(CssEnum writing_mode) {
    // Sideways modes keep vertical block geometry; their distinction is typographic.
    if (writing_mode == CSS_VALUE_VERTICAL_RL || writing_mode == CSS_VALUE_SIDEWAYS_RL) {
        return WM_VERTICAL_RL;
    }
    if (writing_mode == CSS_VALUE_VERTICAL_LR || writing_mode == CSS_VALUE_SIDEWAYS_LR) {
        return WM_VERTICAL_LR;
    }
    return WM_HORIZONTAL_TB;
}

WritingMode layout_block_writing_mode(ViewBlock* block) {
    if (block && block->doc && block->doc->root.get() == block &&
        dom_element_is_html_tag(block->as_element(), MARKUP_NAME_HTML)) {
        DomElement* body = dom_document_body_element(block->doc);
        // principal flow propagation changes the root's used mode, preserving
        // its computed CSS mode for inheritance and logical-property mapping.
        if (body && dom_element_is_html_tag(body, MARKUP_NAME_BODY))
            return layout_element_writing_mode(body);
    }
    WritingMode mode = block && block->blk ? block->block()->writing_mode : WM_HORIZONTAL_TB;
    if (mode != WM_HORIZONTAL_TB || !block) return mode;
    // intrinsic sizing can precede BlockProp resolution; share computed
    // inheritance with logical-property mapping instead of reparsing it here.
    return layout_element_writing_mode(block->as_element());
}

bool layout_block_inline_axis_is_vertical(ViewBlock* block) {
    WritingMode writing_mode = layout_block_writing_mode(block);
    return writing_mode == WM_VERTICAL_LR ||
        writing_mode == WM_VERTICAL_RL;
}

bool layout_block_has_size_containment_in_axis(ViewBlock* block, bool horizontal) {
    if (!block || !block->blk) return false;
    if (block->block()->contain_size || block->block()->content_visibility_skipped) return true;
    if (!block->block()->contain_inline_size) return false;

    bool inline_axis_is_vertical = layout_block_inline_axis_is_vertical(block);
    // Inline-size containment applies size containment only to the logical
    // inline axis, which is physical height in vertical writing modes.
    return horizontal != inline_axis_is_vertical;
}

float layout_block_empty_content_size_in_axis(ViewBlock* block, bool horizontal) {
    if (!block) return 0.0f;

    if (block->form && block->block()->content_visibility_skipped &&
        block->form->control_type == FORM_CONTROL_SELECT) {
        bool is_listbox = block->form->multiple || block->has_attribute("multiple") ||
            block->form->select_size > 1;
        if (horizontal) {
            if (is_listbox) return 0.0f;
            float empty_combo_border_width = FormDefaults::SELECT_HEIGHT + 3.0f;
            return empty_combo_border_width - 2.0f * FormDefaults::SELECT_BORDER;
        }
        if (!is_listbox) {
            return FormDefaults::SELECT_HEIGHT - 2.0f * FormDefaults::SELECT_BORDER;
        }
        int visible_rows = block->form->select_size > 0 ? block->form->select_size
            : (block->form->multiple ? 4 : 1);
        // Hidden selects retain their native control chrome, but option text and
        // rows are skipped; the listbox uses its anonymous empty-row metric.
        return visible_rows * FormDefaults::SELECT_EMPTY_LISTBOX_ROW_HEIGHT;
    }

    bool inline_axis_is_vertical = layout_block_inline_axis_is_vertical(block);
    if (horizontal == inline_axis_is_vertical) return 0.0f;
    // A fixed multicol track remains after containment removes descendants;
    // it is formatting structure in the logical inline axis, not content.
    return multicol_empty_intrinsic_inline_size(block);
}

float layout_block_stable_scrollbar_gutter(ViewBlock* block, bool horizontal) {
    if (!block || !block->scroller || !block->scroll()->scrollbar_gutter_stable) {
        return 0.0f;
    }

    bool inline_axis_is_vertical = layout_block_inline_axis_is_vertical(block);
    if (horizontal == inline_axis_is_vertical) return 0.0f;

    CssEnum block_axis_overflow = inline_axis_is_vertical
        ? block->scroll()->overflow_x : block->scroll()->overflow_y;
    bool block_axis_is_scrollable = block_axis_overflow == CSS_VALUE_AUTO ||
        block_axis_overflow == CSS_VALUE_SCROLL || block_axis_overflow == CSS_VALUE_HIDDEN;
    if (!block_axis_is_scrollable) return 0.0f;
    // Classic scrollbar width is a UA layout metric; stable gutters reserve it
    // even when no scrollbar is currently painted (CSS Overflow 3 §5.2).
    constexpr float CLASSIC_SCROLLBAR_GUTTER_SIZE = 15.0f;
    float gutter_count = block->scroll()->scrollbar_gutter_both_edges ? 2.0f : 1.0f;
    return CLASSIC_SCROLLBAR_GUTTER_SIZE * gutter_count;
}

float layout_block_auto_content_width_from_inline_base(ViewBlock* block, float inline_base) {
    if (!block || inline_base <= 0.0f) return -1.0f;
    float content_width = inline_base;
    if (block->bound) {
        content_width -= layout_boundary_metrics(block->bound).margin_h;
        content_width = layout_content_size_from_border_box(block, content_width, true);
    }
    return content_width >= 0.0f ? content_width : 0.0f;
}
