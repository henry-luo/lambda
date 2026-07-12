#include "layout_percentages.hpp"

#include "layout_box.hpp"
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

    Spacing* spacing = margin ? (Spacing*)&block->bound->margin : &block->bound->padding;
    switch (side) {
        case 0:
            spacing->top = resolved;
            spacing->top_specificity = candidate->priority;
            if (margin) block->bound->margin.top_type = CSS_VALUE__PERCENTAGE;
            break;
        case 1:
            spacing->right = resolved;
            spacing->right_specificity = candidate->priority;
            if (margin) block->bound->margin.right_type = CSS_VALUE__PERCENTAGE;
            break;
        case 2:
            spacing->bottom = resolved;
            spacing->bottom_specificity = candidate->priority;
            if (margin) block->bound->margin.bottom_type = CSS_VALUE__PERCENTAGE;
            break;
        case 3:
            spacing->left = resolved;
            spacing->left_specificity = candidate->priority;
            if (margin) block->bound->margin.left_type = CSS_VALUE__PERCENTAGE;
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

    CssPropertyId shorthand = margin ? CSS_PROPERTY_MARGIN : CSS_PROPERTY_PADDING;
    CssDeclaration* decl = style_tree_get_declaration(block->specified_style, shorthand);
    if (decl && decl->value) {
        layout_consider_percentage_spacing_candidate(top, decl, (CssValue*)css_box_shorthand_side_value(decl->value, 0));
        layout_consider_percentage_spacing_candidate(right, decl, (CssValue*)css_box_shorthand_side_value(decl->value, 1));
        layout_consider_percentage_spacing_candidate(bottom, decl, (CssValue*)css_box_shorthand_side_value(decl->value, 2));
        layout_consider_percentage_spacing_candidate(left, decl, (CssValue*)css_box_shorthand_side_value(decl->value, 3));
    }

    CssPropertyId prop_top = margin ? CSS_PROPERTY_MARGIN_TOP : CSS_PROPERTY_PADDING_TOP;
    CssPropertyId prop_right = margin ? CSS_PROPERTY_MARGIN_RIGHT : CSS_PROPERTY_PADDING_RIGHT;
    CssPropertyId prop_bottom = margin ? CSS_PROPERTY_MARGIN_BOTTOM : CSS_PROPERTY_PADDING_BOTTOM;
    CssPropertyId prop_left = margin ? CSS_PROPERTY_MARGIN_LEFT : CSS_PROPERTY_PADDING_LEFT;

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

    CssPropertyId inline_prop = margin ? CSS_PROPERTY_MARGIN_INLINE : CSS_PROPERTY_PADDING_INLINE;
    CssDeclaration* decl = style_tree_get_declaration(block->specified_style, inline_prop);
    if (decl && decl->value) {
        layout_consider_percentage_spacing_candidate(left, decl, layout_pair_spacing_value(decl->value, false));
        layout_consider_percentage_spacing_candidate(right, decl, layout_pair_spacing_value(decl->value, true));
    }

    CssPropertyId inline_start = margin ? CSS_PROPERTY_MARGIN_INLINE_START : CSS_PROPERTY_PADDING_INLINE_START;
    CssPropertyId inline_end = margin ? CSS_PROPERTY_MARGIN_INLINE_END : CSS_PROPERTY_PADDING_INLINE_END;
    decl = style_tree_get_declaration(block->specified_style, inline_start);
    layout_consider_percentage_spacing_candidate(left, decl, decl ? decl->value : nullptr);
    decl = style_tree_get_declaration(block->specified_style, inline_end);
    layout_consider_percentage_spacing_candidate(right, decl, decl ? decl->value : nullptr);

    CssPropertyId block_prop = margin ? CSS_PROPERTY_MARGIN_BLOCK : CSS_PROPERTY_PADDING_BLOCK;
    decl = style_tree_get_declaration(block->specified_style, block_prop);
    if (decl && decl->value) {
        layout_consider_percentage_spacing_candidate(top, decl, layout_pair_spacing_value(decl->value, false));
        layout_consider_percentage_spacing_candidate(bottom, decl, layout_pair_spacing_value(decl->value, true));
    }

    CssPropertyId block_start = margin ? CSS_PROPERTY_MARGIN_BLOCK_START : CSS_PROPERTY_PADDING_BLOCK_START;
    CssPropertyId block_end = margin ? CSS_PROPERTY_MARGIN_BLOCK_END : CSS_PROPERTY_PADDING_BLOCK_END;
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
    float css_size = horizontal ? block->blk->given_width : block->blk->given_height;
    if (css_size < 0.0f) return -1.0f;
    float content_size = layout_css_size_to_content_box(
        block->bound, layout_box_sizing(block), css_size, horizontal);
    return content_size >= 0.0f ? content_size : 0.0f;
}

float layout_block_declared_content_size(LayoutContext* lycon, ViewBlock* block, CssPropertyId property, bool horizontal) {
    if (!lycon || !block || !block->specified_style) return -1.0f;
    CssDeclaration* decl = style_tree_get_declaration(block->specified_style, property);
    if (!decl || !decl->value) return -1.0f;

    float declared_size = resolve_length_value(lycon, property, decl->value);
    if (isnan(declared_size) || declared_size < 0.0f) return -1.0f;

    CssEnum box_sizing = layout_box_sizing(block);
    float content_size = layout_css_size_to_content_box(block->bound, box_sizing, declared_size, horizontal);
    return content_size >= 0.0f ? content_size : 0.0f;
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
