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
                                                      CssCascadeCandidate* candidate,
                                                      float inline_base, bool margin) {
    if (!block || !block->bound || !candidate || !candidate->decl || !candidate->value) return;
    float resolved = 0.0f;
    if (!layout_resolve_percentage_value(candidate->value, inline_base, &resolved)) return;

    Spacing* spacing = margin ? (Spacing*)&block->boundary_mut()->margin : &block->boundary_mut()->padding;
    if (side < 0 || side >= 4) return;
    float* values[4] = {&spacing->top, &spacing->right, &spacing->bottom, &spacing->left};
    int64_t* priorities[4] = {&spacing->top_specificity, &spacing->right_specificity,
                              &spacing->bottom_specificity, &spacing->left_specificity};
    *values[side] = resolved;
    *priorities[side] = candidate->priority;
    if (margin) {
        CssEnum* types[4] = {&block->boundary_mut()->margin.top_type,
                             &block->boundary_mut()->margin.right_type,
                             &block->boundary_mut()->margin.bottom_type,
                             &block->boundary_mut()->margin.left_type};
        *types[side] = CSS_VALUE__PERCENTAGE;
    }
}

static void layout_collect_spacing_candidates(
    ViewBlock* block,
    bool margin,
    CssCascadeCandidate* top,
    CssCascadeCandidate* right,
    CssCascadeCandidate* bottom,
    CssCascadeCandidate* left) {
    if (!block || !block->specified_style) return;

    CssCascadeCandidate* candidates[4] = {top, right, bottom, left};
    CssPropertyCode physical[4] = {
        margin ? CSS_PROPERTY_MARGIN_TOP : CSS_PROPERTY_PADDING_TOP,
        margin ? CSS_PROPERTY_MARGIN_RIGHT : CSS_PROPERTY_PADDING_RIGHT,
        margin ? CSS_PROPERTY_MARGIN_BOTTOM : CSS_PROPERTY_PADDING_BOTTOM,
        margin ? CSS_PROPERTY_MARGIN_LEFT : CSS_PROPERTY_PADDING_LEFT
    };
    CssPropertyCode shorthand = margin ? CSS_PROPERTY_MARGIN : CSS_PROPERTY_PADDING;
    CssDeclaration* decl = style_tree_get_declaration(block->specified_style, shorthand);
    if (decl && decl->value) {
        for (int side = 0; side < 4; side++) {
            css_consider_cascade_candidate(
                candidates[side], decl,
                (CssValue*)css_box_shorthand_side_value(decl->value, side));
        }
    }
    for (int side = 0; side < 4; side++) {
        decl = style_tree_get_declaration(block->specified_style, physical[side]);
        css_consider_cascade_candidate(
            candidates[side], decl, decl ? decl->value : nullptr);
    }

    CssPropertyCode inline_prop = margin ? CSS_PROPERTY_MARGIN_INLINE : CSS_PROPERTY_PADDING_INLINE;
    CssCascadeCandidate* inline_candidates[2] = {left, right};
    decl = style_tree_get_declaration(block->specified_style, inline_prop);
    if (decl && decl->value) {
        for (int side = 0; side < 2; side++) {
            css_consider_cascade_candidate(
                inline_candidates[side], decl, css_pair_side_value(decl->value, side != 0));
        }
    }

    CssPropertyCode inline_properties[2] = {
        margin ? CSS_PROPERTY_MARGIN_INLINE_START : CSS_PROPERTY_PADDING_INLINE_START,
        margin ? CSS_PROPERTY_MARGIN_INLINE_END : CSS_PROPERTY_PADDING_INLINE_END
    };
    for (int side = 0; side < 2; side++) {
        decl = style_tree_get_declaration(block->specified_style, inline_properties[side]);
        css_consider_cascade_candidate(
            inline_candidates[side], decl, decl ? decl->value : nullptr);
    }

    CssPropertyCode block_prop = margin ? CSS_PROPERTY_MARGIN_BLOCK : CSS_PROPERTY_PADDING_BLOCK;
    decl = style_tree_get_declaration(block->specified_style, block_prop);
    CssCascadeCandidate* block_candidates[2] = {top, bottom};
    if (decl && decl->value) {
        for (int side = 0; side < 2; side++) {
            css_consider_cascade_candidate(
                block_candidates[side], decl, css_pair_side_value(decl->value, side != 0));
        }
    }

    CssPropertyCode block_properties[2] = {
        margin ? CSS_PROPERTY_MARGIN_BLOCK_START : CSS_PROPERTY_PADDING_BLOCK_START,
        margin ? CSS_PROPERTY_MARGIN_BLOCK_END : CSS_PROPERTY_PADDING_BLOCK_END
    };
    for (int side = 0; side < 2; side++) {
        decl = style_tree_get_declaration(block->specified_style, block_properties[side]);
        css_consider_cascade_candidate(
            block_candidates[side], decl, decl ? decl->value : nullptr);
    }
}

static void layout_reresolve_percentage_spacing(ViewBlock* block, float inline_base, bool margin) {
    if (!block || !block->bound || !block->specified_style || inline_base < 0.0f) return;

    CssCascadeCandidate top = {};
    CssCascadeCandidate right = {};
    CssCascadeCandidate bottom = {};
    CssCascadeCandidate left = {};

    layout_collect_spacing_candidates(block, margin, &top, &right, &bottom, &left);

    layout_apply_percentage_spacing_candidate(block, 0, &top, inline_base, margin);
    layout_apply_percentage_spacing_candidate(block, 1, &right, inline_base, margin);
    layout_apply_percentage_spacing_candidate(block, 2, &bottom, inline_base, margin);
    layout_apply_percentage_spacing_candidate(block, 3, &left, inline_base, margin);
}

void layout_reresolve_percentage_box(ViewBlock* block, float inline_base) {
    layout_reresolve_percentage_spacing(block, inline_base, false);
    layout_reresolve_percentage_spacing(block, inline_base, true);
}

static bool layout_spacing_candidate_is_percentage(
    const CssCascadeCandidate& candidate) {
    return candidate.value && candidate.value->type == CSS_VALUE_TYPE_PERCENTAGE;
}

static bool layout_element_has_percentage_spacing(ViewBlock* block) {
    if (!block || !block->specified_style) return false;

    for (int pass = 0; pass < 2; pass++) {
        bool margin = pass != 0;
        CssCascadeCandidate top = {};
        CssCascadeCandidate right = {};
        CssCascadeCandidate bottom = {};
        CssCascadeCandidate left = {};
        layout_collect_spacing_candidates(
            block, margin, &top, &right, &bottom, &left);
        if (layout_spacing_candidate_is_percentage(top) ||
            layout_spacing_candidate_is_percentage(right) ||
            layout_spacing_candidate_is_percentage(bottom) ||
            layout_spacing_candidate_is_percentage(left)) {
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
    LayoutAxisConstraintRefs axis(block->block_mut(), horizontal);
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

bool layout_percentage_height_basis_is_algorithmically_definite(ViewBlock* containing_block) {
    if (!containing_block || containing_block->height <= 0.0f) return false;

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

bool layout_block_has_automatic_size(ViewBlock* block, bool horizontal) {
    if (!block || !block->blk) return true;
    LayoutAxisConstraintRefs axis(block->block_mut(), horizontal);
    float given_size = *axis.given;
    CssEnum given_size_type = *axis.given_type;
    if (given_size < 0.0f || given_size_type == CSS_VALUE_AUTO) return true;

    const CssDeclaration* specified_size = block->is_element()
        ? layout_specified_physical_size_declaration(block->as_element(), horizontal) : nullptr;
    if (specified_size && specified_size->value) {
        if (!horizontal && specified_size->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            // A percentage block size is automatic while its containing block is auto;
            // treating a provisional intrinsic height as definite feeds aspect-ratio backward.
            ViewBlock* containing_block = layout_nearest_block_ancestor(block->parent_view());
            if (!containing_block ||
                (layout_block_has_automatic_size(containing_block, false) &&
                 !layout_percentage_height_basis_is_algorithmically_definite(containing_block))) {
                return true;
            }
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
    WritingMode mode = block && block->blk ? block->block()->writing_mode : WM_HORIZONTAL_TB;
    if (mode != WM_HORIZONTAL_TB || !block) return mode;

    // Intrinsic sizing may run before the computed BlockProp exists; resolve
    // the inherited specified axis there instead of falling back to horizontal.
    DomElement* current = block->as_element();
    for (DomNode* node = current; node; node = node->parent) {
        if (!node->is_element()) continue;
        DomElement* element = node->as_element();
        CssDeclaration* declaration = element && element->specified_style
            ? style_tree_get_declaration(element->specified_style, CSS_PROPERTY_WRITING_MODE)
            : nullptr;
        if (!declaration || !declaration->value ||
            declaration->value->type != CSS_VALUE_TYPE_KEYWORD) continue;
        return layout_writing_mode_from_css(declaration->value->data.keyword);
    }
    return mode;
}

bool layout_block_inline_axis_is_vertical(ViewBlock* block) {
    WritingMode writing_mode = layout_block_writing_mode(block);
    return writing_mode == WM_VERTICAL_LR ||
        writing_mode == WM_VERTICAL_RL;
}

bool layout_block_has_size_containment_in_axis(ViewBlock* block, bool horizontal) {
    if (!block || !block->blk) return false;
    if (block->block()->contain_size || block->block()->content_visibility_hidden) return true;
    if (!block->block()->contain_inline_size) return false;

    bool inline_axis_is_vertical = layout_block_inline_axis_is_vertical(block);
    // Inline-size containment applies size containment only to the logical
    // inline axis, which is physical height in vertical writing modes.
    return horizontal != inline_axis_is_vertical;
}

float layout_block_empty_content_size_in_axis(ViewBlock* block, bool horizontal) {
    if (!block) return 0.0f;

    if (block->form && block->block()->content_visibility_hidden &&
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
        BoxMetrics box = layout_box_metrics(block);
        content_width -= box.margin_h + box.pad_border_h;
    }
    return content_width >= 0.0f ? content_width : 0.0f;
}
