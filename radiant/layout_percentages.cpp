#include "layout.hpp"

#include "../lambda/input/css/css_style_node.hpp"

#include <math.h>

typedef struct LayoutPercentageSpacingCandidate {
    CssDeclaration* decl;
    CssValue* value;
    int64_t priority;
} LayoutPercentageSpacingCandidate;

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

static CssValue* layout_pair_spacing_value(const CssValue* value, bool end_side) {
    if (!value) return nullptr;
    if (value->type != CSS_VALUE_TYPE_LIST) return (CssValue*)value;
    int cnt = value->data.list.count;
    CssValue** vals = value->data.list.values;
    if (cnt <= 0 || !vals) return nullptr;
    int idx = (end_side && cnt >= 2) ? 1 : 0;
    return (idx < cnt) ? vals[idx] : nullptr;
}

static void layout_consider_percentage_spacing_candidate(LayoutPercentageSpacingCandidate* candidate,
                                                        CssDeclaration* decl, CssValue* value) {
    if (!candidate || !decl || !value) return;
    int64_t priority = get_cascade_priority(decl);
    if (!candidate->decl || priority >= candidate->priority) {
        candidate->decl = decl;
        candidate->value = value;
        candidate->priority = priority;
    }
}

static void layout_apply_percentage_spacing_candidate(ViewBlock* block, int side,
                                                      LayoutPercentageSpacingCandidate* candidate,
                                                      float inline_base, bool margin) {
    if (!block || !block->bound || !candidate || !candidate->decl || !candidate->value) return;
    float resolved = 0.0f;
    if (!layout_resolve_percentage_value(candidate->value, inline_base, &resolved)) return;

    Spacing* spacing = margin ? (Spacing*)&block->boundary_mut()->margin : &block->boundary_mut()->padding;
    switch (side) {
        case 0:
            spacing->top = resolved;
            spacing->top_specificity = candidate->priority;
            if (margin) block->boundary_mut()->margin.top_type = CSS_VALUE__PERCENTAGE;
            break;
        case 1:
            spacing->right = resolved;
            spacing->right_specificity = candidate->priority;
            if (margin) block->boundary_mut()->margin.right_type = CSS_VALUE__PERCENTAGE;
            break;
        case 2:
            spacing->bottom = resolved;
            spacing->bottom_specificity = candidate->priority;
            if (margin) block->boundary_mut()->margin.bottom_type = CSS_VALUE__PERCENTAGE;
            break;
        case 3:
            spacing->left = resolved;
            spacing->left_specificity = candidate->priority;
            if (margin) block->boundary_mut()->margin.left_type = CSS_VALUE__PERCENTAGE;
            break;
    }
}

static void layout_collect_physical_spacing_candidates(
    ViewBlock* block,
    bool margin,
    LayoutPercentageSpacingCandidate* top,
    LayoutPercentageSpacingCandidate* right,
    LayoutPercentageSpacingCandidate* bottom,
    LayoutPercentageSpacingCandidate* left) {
    if (!block || !block->specified_style) return;

    CssPropertyCode shorthand = margin ? CSS_PROPERTY_MARGIN : CSS_PROPERTY_PADDING;
    CssDeclaration* decl = style_tree_get_declaration(block->specified_style, shorthand);
    if (decl && decl->value) {
        layout_consider_percentage_spacing_candidate(top, decl, (CssValue*)css_box_shorthand_side_value(decl->value, 0));
        layout_consider_percentage_spacing_candidate(right, decl, (CssValue*)css_box_shorthand_side_value(decl->value, 1));
        layout_consider_percentage_spacing_candidate(bottom, decl, (CssValue*)css_box_shorthand_side_value(decl->value, 2));
        layout_consider_percentage_spacing_candidate(left, decl, (CssValue*)css_box_shorthand_side_value(decl->value, 3));
    }

    CssPropertyCode prop_top = margin ? CSS_PROPERTY_MARGIN_TOP : CSS_PROPERTY_PADDING_TOP;
    CssPropertyCode prop_right = margin ? CSS_PROPERTY_MARGIN_RIGHT : CSS_PROPERTY_PADDING_RIGHT;
    CssPropertyCode prop_bottom = margin ? CSS_PROPERTY_MARGIN_BOTTOM : CSS_PROPERTY_PADDING_BOTTOM;
    CssPropertyCode prop_left = margin ? CSS_PROPERTY_MARGIN_LEFT : CSS_PROPERTY_PADDING_LEFT;

    CssDeclaration* side_decl = style_tree_get_declaration(block->specified_style, prop_top);
    layout_consider_percentage_spacing_candidate(top, side_decl, side_decl ? side_decl->value : nullptr);
    side_decl = style_tree_get_declaration(block->specified_style, prop_right);
    layout_consider_percentage_spacing_candidate(right, side_decl, side_decl ? side_decl->value : nullptr);
    side_decl = style_tree_get_declaration(block->specified_style, prop_bottom);
    layout_consider_percentage_spacing_candidate(bottom, side_decl, side_decl ? side_decl->value : nullptr);
    side_decl = style_tree_get_declaration(block->specified_style, prop_left);
    layout_consider_percentage_spacing_candidate(left, side_decl, side_decl ? side_decl->value : nullptr);
}

static void layout_collect_logical_spacing_candidates(
    ViewBlock* block,
    bool margin,
    LayoutPercentageSpacingCandidate* top,
    LayoutPercentageSpacingCandidate* right,
    LayoutPercentageSpacingCandidate* bottom,
    LayoutPercentageSpacingCandidate* left) {
    if (!block || !block->specified_style) return;

    CssPropertyCode inline_prop = margin ? CSS_PROPERTY_MARGIN_INLINE : CSS_PROPERTY_PADDING_INLINE;
    CssDeclaration* decl = style_tree_get_declaration(block->specified_style, inline_prop);
    if (decl && decl->value) {
        layout_consider_percentage_spacing_candidate(left, decl, layout_pair_spacing_value(decl->value, false));
        layout_consider_percentage_spacing_candidate(right, decl, layout_pair_spacing_value(decl->value, true));
    }

    CssPropertyCode inline_start = margin ? CSS_PROPERTY_MARGIN_INLINE_START : CSS_PROPERTY_PADDING_INLINE_START;
    CssPropertyCode inline_end = margin ? CSS_PROPERTY_MARGIN_INLINE_END : CSS_PROPERTY_PADDING_INLINE_END;
    decl = style_tree_get_declaration(block->specified_style, inline_start);
    layout_consider_percentage_spacing_candidate(left, decl, decl ? decl->value : nullptr);
    decl = style_tree_get_declaration(block->specified_style, inline_end);
    layout_consider_percentage_spacing_candidate(right, decl, decl ? decl->value : nullptr);

    CssPropertyCode block_prop = margin ? CSS_PROPERTY_MARGIN_BLOCK : CSS_PROPERTY_PADDING_BLOCK;
    decl = style_tree_get_declaration(block->specified_style, block_prop);
    if (decl && decl->value) {
        layout_consider_percentage_spacing_candidate(top, decl, layout_pair_spacing_value(decl->value, false));
        layout_consider_percentage_spacing_candidate(bottom, decl, layout_pair_spacing_value(decl->value, true));
    }

    CssPropertyCode block_start = margin ? CSS_PROPERTY_MARGIN_BLOCK_START : CSS_PROPERTY_PADDING_BLOCK_START;
    CssPropertyCode block_end = margin ? CSS_PROPERTY_MARGIN_BLOCK_END : CSS_PROPERTY_PADDING_BLOCK_END;
    decl = style_tree_get_declaration(block->specified_style, block_start);
    layout_consider_percentage_spacing_candidate(top, decl, decl ? decl->value : nullptr);
    decl = style_tree_get_declaration(block->specified_style, block_end);
    layout_consider_percentage_spacing_candidate(bottom, decl, decl ? decl->value : nullptr);
}

static void layout_reresolve_percentage_spacing(ViewBlock* block, float inline_base, bool margin) {
    if (!block || !block->bound || !block->specified_style || inline_base < 0.0f) return;

    LayoutPercentageSpacingCandidate top = {};
    LayoutPercentageSpacingCandidate right = {};
    LayoutPercentageSpacingCandidate bottom = {};
    LayoutPercentageSpacingCandidate left = {};

    layout_collect_physical_spacing_candidates(block, margin, &top, &right, &bottom, &left);
    layout_collect_logical_spacing_candidates(block, margin, &top, &right, &bottom, &left);

    layout_apply_percentage_spacing_candidate(block, 0, &top, inline_base, margin);
    layout_apply_percentage_spacing_candidate(block, 1, &right, inline_base, margin);
    layout_apply_percentage_spacing_candidate(block, 2, &bottom, inline_base, margin);
    layout_apply_percentage_spacing_candidate(block, 3, &left, inline_base, margin);
}

void layout_reresolve_percentage_box(ViewBlock* block, float inline_base) {
    layout_reresolve_percentage_spacing(block, inline_base, false);
    layout_reresolve_percentage_spacing(block, inline_base, true);
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
    float css_size = horizontal ? block->block()->given_width : block->block()->given_height;
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

bool layout_block_has_automatic_size(ViewBlock* block, bool horizontal) {
    if (!block || !block->blk) return true;
    float given_size = horizontal ? block->block()->given_width : block->block()->given_height;
    CssEnum given_size_type = horizontal
        ? block->block()->given_width_type : block->block()->given_height_type;
    if (given_size < 0.0f || given_size_type == CSS_VALUE_AUTO) return true;

    const CssDeclaration* specified_size = block->is_element()
        ? layout_specified_physical_size_declaration(block->as_element(), horizontal) : nullptr;
    if (specified_size && specified_size->value) {
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
        if (declaration->value->data.keyword == CSS_VALUE_VERTICAL_LR) return WM_VERTICAL_LR;
        if (declaration->value->data.keyword == CSS_VALUE_VERTICAL_RL) return WM_VERTICAL_RL;
        if (declaration->value->data.keyword == CSS_VALUE_HORIZONTAL_TB) return WM_HORIZONTAL_TB;
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
