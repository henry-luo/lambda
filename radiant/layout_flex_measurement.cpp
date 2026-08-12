#include "layout.hpp"
#include "view.hpp"
#include "render.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"

#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/mem_grow.hpp"
#include "../lib/tagged.hpp"
#include <float.h>
#include <limits.h>

FlexMeasureTextRun flex_measure_prepare_text_run(DomNode* text_node, const char* text, size_t length) {
    FlexMeasureTextRun run = {text, length};
    if (!text) return run;

    CssEnum ws = get_white_space_value(text_node);
    if (!layout_white_space_collapses(ws)) return run;

    static thread_local char normalized_buffer[4096];  // LARGE_ARRAY_OK: static buffer — not on call stack.
    run.length = layout_normalize_collapsible_whitespace(
        text, length, normalized_buffer, sizeof(normalized_buffer));
    run.text = normalized_buffer;
    return run;
}

static void flex_store_intrinsic_sizes(ViewElement* item, float min_width, float max_width,
                                       float min_height, float max_height) {
    item->fi->intrinsic_width.min_content = min_width;
    item->fi->intrinsic_width.max_content = max_width;
    item->fi->has_intrinsic_width = true;
    item->fi->intrinsic_height.min_content = min_height;
    item->fi->intrinsic_height.max_content = max_height;
    item->fi->has_intrinsic_height = true;
}

static bool flex_measurement_child_is_skipped_flex_item(ViewElement* child_view,
                                                        DisplayValue child_display) {
    if (layout_display_is_none(child_display)) return true;

    ViewBlock* child_block = lam::view_as_block(child_view);
    if (child_block && layout_position_is_abs_fixed(child_block->position)) {
        return true;
    }

    return layout_element_is_abs_or_fixed(child_view->as_element());
}

static inline void flex_accumulate_height(bool use_max_height, float height,
                                          float* max_height, float* sum_height) {
    if (use_max_height) *max_height = max(*max_height, height);
    else *sum_height += height;
}

static float get_explicit_css_length(LayoutContext* lycon, ViewElement* elem,
                                     CssPropertyCode property_code) {
    if (!elem || !elem->specified_style) return -1.0f;
    CssDeclaration* decl = style_tree_get_declaration(elem->specified_style, property_code);
    if (!decl || !decl->value || decl->value->type != CSS_VALUE_TYPE_LENGTH) return -1.0f;
    float size = resolve_length_value(lycon, property_code, decl->value);
    return !isnan(size) && size > 0.0f ? size : -1.0f;
}

static bool flex_element_has_declared_line_height(DomElement* elem) {
    if (!elem || !elem->specified_style) return false;
    return style_tree_get_declaration(elem->specified_style, CSS_PROPERTY_LINE_HEIGHT) != nullptr ||
           style_tree_get_declaration(elem->specified_style, CSS_PROPERTY_FONT) != nullptr;
}

float flex_resolve_inherited_line_height(LayoutContext* lycon, DomElement* target) {
    if (!lycon || !target) return 0;
    float target_font_size = target->font && target->fontp()->font_size > 0.0f
        ? target->fontp()->font_size : lycon->font.current_font_size;

    for (DomElement* elem = target; elem; ) {
        bool has_declared_lh = flex_element_has_declared_line_height(elem);
        const CssValue* resolved_value = nullptr;
        ViewBlock* view = lam::view_as_block(elem);
        if (has_declared_lh && view && view->blk && view->block_mut()->line_height) {
            const CssValue* lh = view->block()->line_height;
            if (!(lh->type == CSS_VALUE_TYPE_KEYWORD && lh->data.keyword == CSS_VALUE_INHERIT)) {
                resolved_value = lh;
            }
        }

        if (!resolved_value && has_declared_lh && elem->specified_style) {
            CssDeclaration* decl = style_tree_get_declaration(
                elem->specified_style, CSS_PROPERTY_LINE_HEIGHT);
            if (decl && decl->value &&
                !(decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                  decl->value->data.keyword == CSS_VALUE_INHERIT)) {
                resolved_value = decl->value;
            }
        }
        if (resolved_value) {
            return layout_resolve_line_height_value(
                lycon, resolved_value, elem, target_font_size);
        }

        DomNode* parent = elem->parent;
        while (parent && !parent->is_element()) {
            parent = parent->parent;
        }
        elem = parent ? lam::dom_require<DOM_NODE_ELEMENT>(parent) : nullptr;
    }

    return 0;
}

static float get_explicit_dom_css_height(LayoutContext* lycon, DomElement* elem) {
    return elem ? get_explicit_css_length(lycon, lam::view_as_element(elem), CSS_PROPERTY_HEIGHT)
                : -1.0f;
}

// Helper to get the resolved CSS margin for a specific side. Percentage
// margins resolve against the containing block inline size, which may differ
// from any earlier style-resolution context during intrinsic flex measurement.
static float get_css_margin(LayoutContext* lycon, ViewElement* elem,
                            CssPropertyCode property_code, float inline_base) {
    return layout_resolve_intrinsic_margin_side(
        lycon, elem, property_code, inline_base);
}

static float get_child_axis_margins(LayoutContext* lycon, ViewElement* elem,
                                    bool horizontal, float inline_base = -1.0f) {
    CssPropertyCode start = horizontal ? CSS_PROPERTY_MARGIN_LEFT : CSS_PROPERTY_MARGIN_TOP;
    CssPropertyCode end = horizontal ? CSS_PROPERTY_MARGIN_RIGHT : CSS_PROPERTY_MARGIN_BOTTOM;
    return get_css_margin(lycon, elem, start, inline_base) +
           get_css_margin(lycon, elem, end, inline_base);
}

static float flex_item_content_width_for_child_percentages(LayoutContext* lycon,
                                                           ViewElement* item,
                                                           FlexContainerLayout* flex_layout) {
    ViewBlock* block = lam::view_as_block(item);
    if (!block) return -1.0f;

    float content_width = layout_block_used_content_size(block, true, true);
    if (content_width >= 0.0f) return content_width;

    if (block->content_width > 0.0f) {
        return block->content_width;
    }

    content_width = layout_block_given_content_size(block, true);
    if (content_width >= 0.0f) return content_width;

    if (!flex_layout || flex_layout->cross_axis_size <= 0.0f) return -1.0f;

    CssDeclaration* width_decl = item->specified_style
        ? style_tree_get_declaration(item->specified_style, CSS_PROPERTY_WIDTH) : nullptr;
    if (width_decl && width_decl->value &&
        layout_resolve_percentage_value(width_decl->value, flex_layout->cross_axis_size, &content_width)) {
        if (block->blk) {
            content_width = layout_css_size_to_content_box(
                block->bound, layout_box_sizing(block), content_width, true);
        }
        return fmax(content_width, 0.0f);
    }

    if (!width_decl || !width_decl->value ||
        (width_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
         width_decl->value->data.keyword == CSS_VALUE_AUTO)) {
        return layout_block_auto_content_width_from_inline_base(block, flex_layout->cross_axis_size);
    }

    content_width = layout_block_declared_content_size(lycon, block, CSS_PROPERTY_WIDTH, true);
    if (content_width >= 0.0f) return content_width;

    return -1.0f;
}

static float flex_measure_item_content_width_for_height(LayoutContext* lycon,
                                                        ViewElement* item,
                                                        FlexContainerLayout* flex_layout) {
    ViewBlock* block = lam::view_as_block(item);
    if (!block) return 10000.0f;

    float content_width = layout_block_given_content_size(block, true);
    if (content_width >= 0.0f) return content_width;

    if (flex_layout && !is_main_axis_horizontal(flex_layout) &&
        flex_layout->cross_axis_size > 0.0f) {
        float border_box_width = flex_layout->cross_axis_size;
        float inline_base = border_box_width;
        border_box_width -= get_child_axis_margins(lycon, item, true, inline_base);
        if (border_box_width < 0.0f) border_box_width = 0.0f;
        border_box_width = layout_clamp_positive_min_max_width(block, border_box_width);
        content_width = layout_css_size_to_content_box(
            block->bound, layout_box_sizing(block), border_box_width, true);
        if (content_width > 0.0f) return content_width;
    }

    content_width = layout_block_used_content_size(block, true, true);
    if (content_width >= 0.0f) return content_width;

    if (block->content_width > 0.0f) {
        return block->content_width;
    }

    if (item && item->width > 0.0f) {
        content_width = item->width;
        if (item->bound) {
            content_width -= layout_boundary_metrics(item->bound).pad_border_h;
        }
        if (content_width > 0.0f) return content_width;
    }

    return 10000.0f;
}

static bool flex_measure_item_uses_vertical_writing_mode(ViewElement* item) {
    ViewBlock* block = lam::view_as_block(item);
    WritingMode writing_mode = layout_block_writing_mode(block);
    return writing_mode == WM_VERTICAL_LR || writing_mode == WM_VERTICAL_RL;
}

// ============================================================================
// Nested flex content height measurement
// ============================================================================

static bool flex_measurement_tag_is_inline(NameId tag) {
    switch (tag) {
    case MARKUP_NAME_A: case MARKUP_NAME_SPAN: case MARKUP_NAME_EM: case MARKUP_NAME_STRONG:
    case MARKUP_NAME_B: case MARKUP_NAME_I: case MARKUP_NAME_SMALL: case MARKUP_NAME_SUB:
    case MARKUP_NAME_SUP: case MARKUP_NAME_ABBR: case MARKUP_NAME_CODE: case MARKUP_NAME_KBD:
    case MARKUP_NAME_MARK: case MARKUP_NAME_Q: case MARKUP_NAME_S: case MARKUP_NAME_SAMP:
    case MARKUP_NAME_VAR: case MARKUP_NAME_TIME: case MARKUP_NAME_U: case MARKUP_NAME_CITE:
    case MARKUP_NAME_BDI: case MARKUP_NAME_BDO: case MARKUP_NAME_BR:
        return true;
    default:
        return false;
    }
}

static bool flex_break_has_block_siblings(ViewElement* item) {
    if (!item) return false;
    DomNode* previous = item->prev_sibling;
    while (previous && (!previous->is_element() ||
                        previous->as_element()->tag() == MARKUP_NAME_BR)) {
        previous = previous->prev_sibling;
    }
    DomNode* next = item->next_sibling;
    while (next && (!next->is_element() ||
                    next->as_element()->tag() == MARKUP_NAME_BR)) {
        next = next->next_sibling;
    }
    if (!previous || !next) return false;
    bool previous_inline = flex_measurement_tag_is_inline(
        previous->as_element()->tag());
    bool next_inline = flex_measurement_tag_is_inline(
        next->as_element()->tag());
    return !previous_inline && !next_inline;
}

static float flex_measure_normal_line_height_for_font(LayoutContext* lycon,
                                                      FontProp* font,
                                                      float fallback) {
    if (!lycon || !lycon->ui_context || !font || font->font_size <= 0.0f) {
        return fallback;
    }
    FontBox temp_font;
    setup_font(lycon->ui_context, &temp_font, font);
    if (temp_font.font_handle) {
        return calc_normal_line_height(temp_font.font_handle);
    }
    return fallback;
}

static float flex_measure_intrinsic_max_height(LayoutContext* lycon, DomNode* node, float width,
                                               float percentage_containing_width = -1.0f) {
    if (!lycon || !node) return 0.0f;
    ViewBlock* block = node->is_element() ? lam::view_as_block(node->as_element()) : NULL;
    if (!block) return calculate_max_content_height(lycon, node, width);

    AvailableSpace available = AvailableSpace::make_width_definite(width);
    if (percentage_containing_width > 0.0f) {
        // The intrinsic query re-resolves style, so percentages must retain the
        // flex container's definite cross-size instead of the outer block width.
        LayoutContainingBlockScope percentage_parent_scope(
            lycon, LAYOUT_AXIS_X,
            percentage_containing_width);
        return measure_intrinsic_sizes(lycon, block, available).max_content_height;
    }
    return measure_intrinsic_sizes(lycon, block, available).max_content_height;
}

static float flex_measure_nested_intrinsic_width(ViewElement* elem) {
    float width = 10000.0f;
    ViewBlock* block = lam::view_as_block(elem);
    if (block && block->blk && block->block_mut()->given_width > 0.0f) {
        width = block->block()->given_width;
    } else if (elem && elem->width > 0.0f) {
        width = elem->width;
    }
    if (elem && elem->bound && width > 0.0f && width < 10000.0f) {
        width -= layout_boundary_metrics(elem->bound).pad_border_h;
        if (width <= 0.0f) width = 10000.0f;
    }
    return width;
}

struct FlexChildExplicitSizes {
    bool has_width;
    bool has_height;
    float width;
    float height;
};

struct FlexContentSummary {
    bool has_text_content;
    bool has_element_content;
    bool has_child_with_explicit_height;
    bool has_block_element;
    bool has_inline_element;
    int element_count;
};

static FlexContentSummary flex_measure_content_summary(LayoutContext* lycon,
                                                       DomElement* elem) {
    FlexContentSummary summary = {};
    for (DomNode* content = elem ? elem->first_child : nullptr;
         content; content = content->next_sibling) {
        if (layout_text_node_has_content(content)) {
            summary.has_text_content = true;
        } else if (content->is_element()) {
            summary.has_element_content = true;
            summary.element_count++;
            if (flex_measurement_tag_is_inline(content->tag())) {
                summary.has_inline_element = true;
            } else {
                summary.has_block_element = true;
            }
            DomElement* nested = content->as_element();
            if (get_explicit_dom_css_height(lycon, nested) > 0.0f) {
                summary.has_child_with_explicit_height = true;
            }
        }
    }
    return summary;
}

static FlexHeightMeasurement flex_measure_nested_flex_height(LayoutContext* lycon,
                                                             DomElement* elem,
                                                             float text_line_height) {
    FlexContentSummary content_summary = flex_measure_content_summary(lycon, elem);
    if (!content_summary.has_text_content &&
        !content_summary.has_element_content) {
        return {0.0f, true};
    }

    ViewElement* view = lam::view_require_element(elem);
    float intrinsic_width = flex_measure_nested_intrinsic_width(view);
    // legacy DOM recursion drifted from intrinsic flex wrapping and skip rules;
    // nested flex text must measure against its constrained content width.
    float intrinsic_height = flex_measure_intrinsic_max_height(
        lycon, static_cast<DomNode*>(elem), intrinsic_width);
    if (content_summary.has_child_with_explicit_height) {
        if (intrinsic_height > 0.0f) {
            return {intrinsic_height, true};
        }
        return {0.0f, false};
    }

    if (intrinsic_height > 0.0f) {
        return {intrinsic_height, true};
    }
    if (content_summary.has_text_content) {
        // Text-only nested flex fallback is not a final explicit height; later
        // flex layout may still resolve a more exact cross size.
        return {text_line_height, false};
    }

    return {0.0f, true};
}

static FlexChildExplicitSizes flex_measure_child_explicit_sizes(LayoutContext* lycon, ViewElement* child_view) {
    bool width_is_percentage = layout_axis_size_is_percentage(
        child_view->as_element(), true) ||
        layout_axis_size_is_percentage(lam::view_as_block(child_view), true);
    FlexChildExplicitSizes sizes = {
        !width_is_percentage && child_view->blk && child_view->block_mut()->given_width >= 0.0f,
        child_view->blk && child_view->block_mut()->given_height >= 0.0f,
        child_view->blk ? child_view->block()->given_width : -1.0f,
        child_view->blk ? child_view->block()->given_height : -1.0f
    };

    if (!sizes.has_width && !width_is_percentage && lycon) {
        float dom_css_width = get_explicit_css_length(
            lycon, child_view, CSS_PROPERTY_WIDTH);
        if (dom_css_width > 0.0f) {
            sizes.has_width = true;
            sizes.width = dom_css_width;
        }
    }

    if (!sizes.has_height && lycon) {
        float dom_css_height = get_explicit_css_length(
            lycon, child_view, CSS_PROPERTY_HEIGHT);
        if (dom_css_height > 0.0f) {
            sizes.has_height = true;
            sizes.height = dom_css_height;
        }
    }

    return sizes;
}

static float flex_measure_select_max_option_text_width(LayoutContext* lycon,
                                                       ViewElement* elem) {
    float max_text_width = 0.0f;
    for (DomNode* child = elem ? elem->first_child : nullptr; child; child = child->next_sibling) {
        DomElement* group = child->as_element();
        if (group && group->tag() == MARKUP_NAME_OPTGROUP) {
            const char* lbl = group->get_attribute("label");
            if (lbl) {
                size_t ll = strlen(lbl);
                if (ll > 0) {
                    TextIntrinsicWidths tw = measure_text_intrinsic_widths(lycon, lbl, ll);
                    if (tw.max_content > max_text_width) max_text_width = tw.max_content;
                }
            }
        }
    }
    for (DomElement* option = dom_select_next_option(elem, nullptr); option;
         option = dom_select_next_option(elem, option)) {
        float option_width = measure_direct_text_children_intrinsic_width(
            lycon, option, false, CSS_VALUE_NONE);
        DomElement* parent = option->parent ? option->parent->as_element() : nullptr;
        if (parent && parent->tag() == MARKUP_NAME_OPTGROUP) {
            option_width += FormDefaults::OPTGROUP_OPTION_INDENT;
            if (option_width < FormDefaults::OPTGROUP_OPTION_MIN_WIDTH) {
                option_width = FormDefaults::OPTGROUP_OPTION_MIN_WIDTH;
            }
        }
        if (option_width > max_text_width) max_text_width = option_width;
    }
    return max_text_width;
}

static float flex_child_height_fallback_available_width(LayoutContext* lycon,
                                                        ViewElement* item,
                                                        ViewElement* child_view,
                                                        FlexContainerLayout* flex_layout,
                                                        bool is_row_flex_container,
                                                        bool resolve_percent_width) {
    float available_width = 10000.0f;
    if (is_row_flex_container || !item) return available_width;

    float parent_cw = -1.0f;
    if (layout_axis_has_given_size(item, true)) {
        parent_cw = layout_css_size_to_content_box(
            item->bound, layout_box_sizing(lam::view_as_block(item)), item->block()->given_width, true);
    } else if (flex_layout && flex_layout->cross_axis_size > 0.0f) {
        bool use_cross_axis_width = !resolve_percent_width;
        if (resolve_percent_width && item->specified_style) {
            // Nested flex fallback historically re-resolves percentage width here; keep
            // non-percentage specified widths on the legacy unconstrained path.
            CssDeclaration* w_decl = style_tree_get_declaration(
                item->specified_style, CSS_PROPERTY_WIDTH);
            bool resolved_percent_width = w_decl && w_decl->value &&
                layout_resolve_percentage_value(w_decl->value, flex_layout->cross_axis_size, &parent_cw);
            use_cross_axis_width = !resolved_percent_width && (!w_decl || !w_decl->value ||
                (w_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                 w_decl->value->data.keyword == CSS_VALUE_AUTO));
        }
        if (use_cross_axis_width) {
            parent_cw = flex_layout->cross_axis_size;
            if (item->bound) {
                parent_cw -= item->boundary()->margin.left + item->boundary()->margin.right;
            }
        }
        if (parent_cw > 0.0f && item->blk) {
            parent_cw = layout_css_size_to_content_box(
                item->bound, layout_box_sizing(lam::view_as_block(item)), parent_cw, true);
        }
    }

    if (parent_cw > 0.0f) {
        float child_margins = get_child_axis_margins(lycon, child_view, true, parent_cw);
        available_width = fmax(parent_cw - child_margins, 0.0f);
    }
    return available_width;
}

static float flex_measure_zero_child_height_fallback(DomNode* child,
                                                     LayoutContext* lycon,
                                                     ViewElement* item,
                                                     ViewElement* child_view,
                                                     FlexContainerLayout* flex_layout,
                                                     bool is_row_flex_container) {
    DisplayValue child_display = resolve_display_value((void*)child);
    if (child_display.inner == CSS_VALUE_FLEX) {
        float avail_w = flex_child_height_fallback_available_width(
            lycon, item, child_view, flex_layout, is_row_flex_container, true);
        // intrinsic sizing owns flex-container height; keeping a local recursive
        // classifier here caused stale row/column and out-of-flow policy drift.
        float child_height = flex_measure_intrinsic_max_height(lycon, child, avail_w);
        return child_height;
    }
    if (child_display.outer == CSS_VALUE_BLOCK && lycon) {
        // For regular block elements, measure content height.
        // In column flex, use the item's resolved cross-axis width
        // so text wraps correctly during measurement.
        float available_width = flex_child_height_fallback_available_width(
            lycon, item, child_view, flex_layout, is_row_flex_container, false);
        float child_height = flex_measure_intrinsic_max_height(lycon, child, available_width);
        return child_height;
    }
    return 0.0f;
}

static float flex_measure_row_child_height_at_estimated_share(LayoutContext* lycon,
                                                              DomNode* child,
                                                              ViewElement* item,
                                                              ViewElement* child_view,
                                                              FlexContainerLayout* flex_layout) {
    // row flex items in a column parent: compute height at the item's
    // estimated width share, not the grandparent's full cross-axis size.
    float row_width = flex_layout->cross_axis_size;
    if (item->bound) {
        row_width -= item->boundary()->margin.left + item->boundary()->margin.right;
    }
    if (layout_uses_border_box(lam::view_as_block(item))) {
        if (item->block()->given_width >= 0.0f) {
            row_width = item->block()->given_width;
        }
        row_width = layout_clamp_positive_min_max_width(
            lam::view_as_block(item), row_width);
    }
    row_width = layout_boundary_content_size_from_border_box(item->bound, row_width, true);
    if (item->blk && !layout_uses_border_box(lam::view_as_block(item))) {
        if (item->block()->given_width >= 0.0f) {
            row_width = item->block()->given_width;
        }
        row_width = layout_clamp_positive_min_max_width(
            lam::view_as_block(item), row_width);
    }
    if (row_width <= 0.0f) row_width = flex_layout->cross_axis_size;

    float gap = layout_flex_column_gap(item);
    int n_flex_children = 0;
    for (DomNode* sibling = item->first_child; sibling; sibling = sibling->next_sibling) {
        if (sibling->is_element()) n_flex_children++;
    }

    float child_share = row_width;
    if (n_flex_children > 1) {
        child_share = (row_width - gap * (n_flex_children - 1)) / n_flex_children;
    }

    float child_content_w = child_share;
    if (child_view->bound) {
        child_content_w = layout_boundary_content_size_from_border_box(
            child_view->bound, child_content_w, true);
    } else if (child_view->specified_style) {
        // unresolved child bounds still need the CSS box extra removed before
        // intrinsic height measurement, or row-share fallback double-counts it.
        child_content_w -= layout_intrinsic_padding_border_axis(
            lycon, child_view, true, 0.0f);
    }
    if (child_content_w < 0.0f) child_content_w = 0.0f;
    float child_height = flex_measure_intrinsic_max_height(lycon, child, child_content_w);
    return child_height;
}

static FlexHeightMeasurement flex_measure_direct_element_height(LayoutContext* lycon,
                                                                DomNode* sub_child,
                                                                DomElement* elem,
                                                                float text_line_height) {
    FlexHeightMeasurement result = {0.0f, false};
    if (!sub_child) return result;

    NameId tag = sub_child->tag();
    if (tag == MARKUP_NAME_H1) result.height = 32.0f;
    else if (tag == MARKUP_NAME_H2) result.height = 28.0f;
    else if (tag == MARKUP_NAME_H3) result.height = 24.0f;
    else if (tag == MARKUP_NAME_H4) result.height = 20.0f;
    else if (tag == MARKUP_NAME_H5 || tag == MARKUP_NAME_H6) result.height = 18.0f;
    else if (tag == MARKUP_NAME_P) result.height = 36.0f;  // typically 2-3 lines
    else if (tag == MARKUP_NAME_SVG) {
        const char* attr_h = elem ? elem->get_attribute("height") : nullptr;
        if (attr_h) {
            float attr_height = (float)atoi(attr_h);
            if (attr_height > 0.0f) {
                result.height = attr_height;
                result.has_explicit_height_css = true;
            }
        }
        if (!result.has_explicit_height_css) {
            float css_height = get_explicit_dom_css_height(lycon, elem);
            if (css_height > 0.0f) {
                result.height = css_height;
                result.has_explicit_height_css = true;
            }
        }
        if (!result.has_explicit_height_css) {
            result.height = 150.0f;  // HTML default SVG height
        }
    }
    else if (tag == MARKUP_NAME_IFRAME || tag == MARKUP_NAME_IMG ||
             tag == MARKUP_NAME_VIDEO || tag == MARKUP_NAME_CANVAS) {
        float css_height = get_explicit_dom_css_height(lycon, elem);
        if (css_height > 0.0f) {
            result.height = css_height;
            result.has_explicit_height_css = true;
        }
        if (!result.has_explicit_height_css) {
            if (tag == MARKUP_NAME_IFRAME) result.height = 150.0f;  // CSS default iframe height
            else if (tag == MARKUP_NAME_VIDEO) result.height = 150.0f;
            else result.height = 0.0f;  // other replaced elements need explicit size
        }
    }
    else if (tag == MARKUP_NAME_UL || tag == MARKUP_NAME_OL) {
        FlexContentSummary list_summary = flex_measure_content_summary(lycon, elem);
        bool is_list_flex_row = false;
        if (elem) {
            ViewElement* list_view = lam::view_require_element(elem);
            if (layout_is_flex_container(list_view)) {
                is_list_flex_row = layout_flex_direction_is_row(list_view);
            }
        }
        if (is_list_flex_row) {
            result.height = list_summary.element_count > 0 ? 18.0f : 0.0f;  // row: max of children
        } else {
            result.height = list_summary.element_count * 18.0f;  // column/block: sum of children
        }
    }
    else if (tag == MARKUP_NAME_DIV || tag == MARKUP_NAME_SECTION ||
             tag == MARKUP_NAME_ARTICLE || tag == MARKUP_NAME_NAV ||
             tag == MARKUP_NAME_HEADER || tag == MARKUP_NAME_FOOTER ||
             tag == MARKUP_NAME_ASIDE || tag == MARKUP_NAME_MAIN) {
        ViewElement* nested_view = lam::view_require_element(elem);
        bool is_nested_flex = layout_is_flex_container(nested_view);

        if (is_nested_flex) {
            FlexHeightMeasurement nested_height =
                flex_measure_nested_flex_height(lycon, elem, text_line_height);
            result.height = nested_height.height;
            result.has_explicit_height_css = nested_height.has_explicit_height_css;
        } else {
            FlexContentSummary content_summary = flex_measure_content_summary(lycon, elem);
            if (content_summary.has_block_element) {
                result.height = 56.0f;
            } else if (content_summary.has_inline_element ||
                       content_summary.has_text_content) {
                result.height = text_line_height;
            } else {
                result.height = 0.0f;
            }
        }
    }
    else {
        FlexContentSummary content_summary = flex_measure_content_summary(lycon, elem);
        if (content_summary.has_text_content) {
            float child_content_height = text_line_height;
            float inherited_lh = flex_resolve_inherited_line_height(lycon, elem);
            if (inherited_lh > 0.0f) {
                child_content_height = inherited_lh;
            }
            result.height = child_content_height;

            float vert_extra = layout_intrinsic_padding_border_axis(
                lycon, elem, false, lycon->block.content_width);
            result.height += vert_extra;
            if (vert_extra > 0.0f) result.has_explicit_height_css = true;
        } else {
            ViewElement* sub_view = lam::view_as_element(sub_child);
            if (sub_view && sub_view->form_control() && sub_view->form->intrinsic_height > 0.0f) {
                result.height = sub_view->form->intrinsic_height;
                result.has_explicit_height_css = true;
            }

            if (!result.has_explicit_height_css) {
                float css_height = get_explicit_dom_css_height(lycon, elem);
                if (css_height > 0.0f) {
                    result.height = css_height;
                    result.has_explicit_height_css = true;
                }
            }
            if (!result.has_explicit_height_css) {
                result.height = 20.0f;  // default element height
            }
        }
    }
    return result;
}

static void flex_accumulate_direct_element_height(NameId tag,
                                                  FlexHeightMeasurement measured,
                                                  float text_line_height,
                                                  bool is_row_flex,
                                                  float* max_child_height,
                                                  float* measured_height) {
    if (measured.height <= 0.0f) return;

    bool is_inline_child = flex_measurement_tag_is_inline(tag) && tag != MARKUP_NAME_BR;
    bool use_max_height = is_row_flex || is_inline_child;
    float margin =
        (use_max_height && (measured.height == text_line_height ||
                            measured.has_explicit_height_css || is_inline_child)) ||
        (!use_max_height && measured.has_explicit_height_css)
            ? 0.0f : 10.0f;
    flex_accumulate_height(use_max_height, measured.height + margin,
                           max_child_height, measured_height);
}

static IntrinsicSizes flex_measure_child_intrinsic_widths(LayoutContext* lycon,
                                                          ViewElement* child_view,
                                                          bool content_only) {
    LayoutFontScope font_scope(lycon);
    if (child_view->font) {
        // Intrinsic text width must be measured in the child font; parent font state
        // is restored immediately so sibling measurement keeps its inherited context.
        setup_font(lycon->ui_context, &lycon->font, child_view->font);
    }

    IntrinsicSizes child_sizes = measure_element_intrinsic_widths(
        lycon, lam::dom_require<DOM_NODE_ELEMENT>(child_view), content_only);

    return child_sizes;
}

static void flex_measure_direct_child_heights(LayoutContext* lycon, DomElement* child_elem,
                                              float text_line_height, bool is_row_flex,
                                              float* max_child_height, float* measured_height) {
    for (DomNode* sub_child = child_elem ? child_elem->first_child : nullptr;
         sub_child; sub_child = sub_child->next_sibling) {
        if (layout_text_node_has_content(sub_child)) {
            flex_accumulate_height(true, text_line_height,
                                   max_child_height, measured_height);
        } else if (sub_child->is_element()) {
            DomElement* elem = sub_child->as_element();
            NameId tag = sub_child->tag();
            FlexHeightMeasurement measured_elem =
                flex_measure_direct_element_height(lycon, sub_child, elem, text_line_height);
            flex_accumulate_direct_element_height(tag, measured_elem, text_line_height,
                                                  is_row_flex, max_child_height,
                                                  measured_height);
        }
    }
}

// Content measurement for multi-pass flex layout
// This file implements the first pass of the multi-pass flex layout algorithm

// Content measurement cache is owned by ViewTree because entries point at that document's views.
static ViewTree* measurement_cache_tree_for_node(DomNode* node) {
    DomNode* current = node;
    while (current) {
        if (current->is_element()) {
            DomElement* elem = current->as_element();
            return elem && elem->doc ? elem->doc->view_tree : nullptr;
        }
        current = current->parent;
    }
    return nullptr;
}

static bool ensure_measurement_cache_capacity(ViewTree* tree, int required) {
    if (!tree) return false;
    if (required <= tree->measurement_cache_capacity) return true;

    int old_capacity = tree->measurement_cache_capacity;
    if (!lam::mem_grow_array(&tree->measurement_cache, &tree->measurement_cache_capacity, required, 1024,
                             MEM_CAT_CACHE_LAYOUT)) {
        log_error("RAD_CAP_FLEX_MEASURE_CACHE: unable to grow measurement cache from %d to %d entries",
                  old_capacity, required);
        return false;
    }
    // New slots are zeroed so a failed/incomplete entry cannot look like a stale node hit.
    if (tree->measurement_cache_capacity > old_capacity) {
        memset(tree->measurement_cache + old_capacity, 0,
               (size_t)(tree->measurement_cache_capacity - old_capacity) * sizeof(MeasurementCacheEntry));
    }
    if (old_capacity > 0) {
        log_warn("[RAD_CAP_FLEX_MEASURE_CACHE] grew measurement cache from %d to %d entries",
                 old_capacity, tree->measurement_cache_capacity);
    }
    return true;
}

void advance_measurement_cache_generation(ViewTree* tree) {
    if (!tree) return;
    tree->measurement_cache_generation++;
}

void store_in_measurement_cache(DomNode* node, float width, float height,
                               float content_width, float content_height,
                               float context_width) {
    ViewTree* tree = measurement_cache_tree_for_node(node);
    if (!ensure_measurement_cache_capacity(tree, tree ? tree->measurement_cache_count + 1 : 1)) {
        return;
    }

    int cache_count = tree->measurement_cache_count;
    tree->measurement_cache[cache_count].node = node;
    tree->measurement_cache[cache_count].measured_width = width;
    tree->measurement_cache[cache_count].measured_height = height;
    tree->measurement_cache[cache_count].content_width = content_width;
    tree->measurement_cache[cache_count].content_height = content_height;
    tree->measurement_cache[cache_count].context_width = context_width;
    tree->measurement_cache[cache_count].generation = tree->measurement_cache_generation;
    tree->measurement_cache_count++;

}

MeasurementCacheEntry* get_from_measurement_cache(DomNode* node) {
    ViewTree* tree = measurement_cache_tree_for_node(node);
    if (!tree) return nullptr;
    for (int i = 0; i < tree->measurement_cache_count; i++) {
        if (tree->measurement_cache[i].node == node) {
            // skip stale entries from a previous layout generation
            if (tree->measurement_cache[i].generation != tree->measurement_cache_generation) {
                return nullptr;
            }
            return &tree->measurement_cache[i];
        }
    }
    return nullptr;
}

void clear_measurement_cache(ViewTree* tree) {
    if (!tree) return;
    tree->measurement_cache_count = 0;
}

void destroy_measurement_cache(ViewTree* tree) {
    if (!tree) return;
    mem_free(tree->measurement_cache);
    tree->measurement_cache = nullptr;
    tree->measurement_cache_count = 0;
    tree->measurement_cache_capacity = 0;
    tree->measurement_cache_generation = 0;
}

void invalidate_measurement_cache_for_node(DomNode* node) {
    ViewTree* tree = measurement_cache_tree_for_node(node);
    if (!tree) return;
    for (int i = 0; i < tree->measurement_cache_count; i++) {
        if (tree->measurement_cache[i].node == node) {
            // Swap with last entry and decrement count to remove this entry
            tree->measurement_cache[i] = tree->measurement_cache[--tree->measurement_cache_count];
            return;
        }
    }
}

// Measure flex child content without applying final sizing
void measure_flex_child_content(LayoutContext* lycon, DomNode* child) {
    if (!child) return;

    // Check if already measured
    MeasurementCacheEntry* cached = get_from_measurement_cache(child);
    if (cached) {
        return;
    }

    // Save current layout context
    LayoutContext saved_context = *lycon;

    // Create temporary measurement context
    LayoutContext measure_context = *lycon;
    measure_context.block.content_width = -1;  // Unconstrained width for measurement
    measure_context.block.content_height = -1; // Unconstrained height for measurement
    measure_context.block.advance_y = 0;
    measure_context.block.max_width = 0;

    // Set up measurement environment
    line_init(&measure_context, 0, 10000);

    // Perform layout in measurement mode to determine intrinsic sizes
    float measured_width = 0;
    float measured_height = 0;
    float content_width = 0;
    float content_height = 0;

    if (child->is_text()) {
        // Measure text content
        int min_width = 0, max_width = 0, height = 0;
        measure_text_content_accurate(
            &measure_context, child, &min_width, &max_width, &height);
        measured_width = max_width;
        measured_height = height;
        content_width = measured_width;
        content_height = measured_height;
    } else {
        // Measure element content by performing a preliminary layout
        // Set up the measurement context with the container's width constraint
        float container_width = lycon->block.content_width;
        if (container_width <= 0) container_width = 366;  // Default fallback

        // Set up block context for measurement
        measure_context.block.content_width = container_width;
        measure_context.block.content_height = -1;  // Unconstrained height
        measure_context.block.advance_y = 0;
        measure_context.block.max_width = 0;
        measure_context.run_mode = radiant::RunMode::ComputeSize;

        // Initialize line context
        line_init(&measure_context, 0, container_width);

        // Check if this element is a row flex container
        // For row flex containers, we should use MAX of child heights, not SUM
        ViewElement* elem_view = lam::view_require_element(child);
        bool is_row_flex = false;
        if (elem_view) {
            // Check display property directly on the DOM element
            if (elem_view->display.inner == CSS_VALUE_FLEX) {
                // It's a flex container - check direction
                is_row_flex = layout_flex_direction_is_row(elem_view);
            }
        }

        // Measure child content heights by traversing the subtree
        // The child parameter is the flex item element - get its first_child
        measured_height = 0;
        float max_child_height = 0.0f;  // For row flex containers
        measured_width = 0;
        DomElement* child_elem = child->as_element();

        // Get font-size from resolved styles (after init_flex_item_view resolved CSS)
        ViewElement* view_elem = lam::view_require_element(child_elem);
        float elem_font_size = 16;  // default fallback
        if (view_elem && view_elem->font && view_elem->fontp()->font_size > 0) {
            elem_font_size = view_elem->fontp()->font_size;
        }

        // Calculate actual line height using the font's metrics (Chrome-compatible)
        // This requires setting up the font first to get accurate font metrics
        float text_line_height = elem_font_size;  // fallback
        if (view_elem) {
            text_line_height =
                flex_measure_normal_line_height_for_font(lycon, view_elem->font, text_line_height);
        }

        flex_measure_direct_child_heights(lycon, child_elem, text_line_height, is_row_flex, &max_child_height, &measured_height);

        // For row flex containers OR blocks with only inline children, use max_child_height
        // This is because inline children flow horizontally and should not stack heights
        if (max_child_height > 0 && (is_row_flex || measured_height == 0)) {
            measured_height = max_child_height;
        }

        // Set measured dimensions
        // CRITICAL FIX: For elements without explicit width, measured_width should be based
        // on content, not container. Only use container_width if the element has explicit width.
        ViewElement* elem = lam::view_require_element(child);
        bool has_explicit_width = layout_axis_has_given_size(elem, true);

        if (has_explicit_width) {
            measured_width = elem->block()->given_width;
        } else {
            // For elements without explicit width, use 0 as intrinsic width
            // The actual width will be determined by flex layout (stretch, etc.)
            measured_width = 0;
        }
        content_width = measured_width;
        content_height = measured_height;

        // Special handling for form controls - use intrinsic size as content
        if (elem && elem->form_control()) {
            ViewBlock* elem_block = lam::view_as_block(elem);
            IntrinsicSize form_size = layout_measure_form_control(lycon, elem_block,
                                                                  lycon->available_space);
            content_height = form_size.max_height;
            content_width = form_size.max_width;

            // For text-like inputs, recalculate content height using actual font
            // (CSS may override UA font-size, so intrinsic_height from UA phase may be stale)
            if (elem->form->control_type == FORM_CONTROL_TEXT &&
                elem->font && elem->fontp()->font_size > 0 && lycon->ui_context) {
                float line_h =
                    flex_measure_normal_line_height_for_font(lycon, elem->font, content_height);
                if (line_h > content_height) content_height = line_h;
            }

            // SELECT (combo box): measure max option text + arrow overhead so
            // selects sized via the flex measurement path get their proper
            // intrinsic width (calc_select_size in layout_form.cpp is only
            // reached via the layout_form_control dispatch, not flex/grid).
            if (elem->form->control_type == FORM_CONTROL_SELECT &&
                !elem->form->multiple && elem->form->select_size <= 1) {
                LayoutFontScope font_scope(lycon);
                if (elem->font && elem->fontp()->font_size > 0 && lycon->ui_context) {
                    setup_font(lycon->ui_context, &lycon->font, elem->font);
                }
                float max_text_width = flex_measure_select_max_option_text_width(lycon, elem);
                float new_w = layout_select_combo_intrinsic_width(
                    max_text_width, !elem->form->appearance_none);
                if (new_w > content_width) {
                    content_width = new_w;
                    elem->form->intrinsic_width = new_w;
                }
            }
            measured_height = content_height;
            measured_width = content_width;

            // Special handling for buttons with child content (e.g., <button>Subscribe</button>)
            // The intrinsic_width may not be set because buttons go through normal layout flow
            if (elem->form->control_type == FORM_CONTROL_BUTTON &&
                elem->form->intrinsic_width <= 0 && elem->first_child) {
                // Get text-transform from parent element chain
                CssEnum btn_text_transform = layout_inherited_text_transform(elem);

                // Measure text content of button
                // Set up button's own font for measurement (UA default 13.3333px Arial,
                // not parent's inherited font which may differ)
                LayoutFontScope font_scope(lycon);
                if (elem->font && elem->fontp()->font_size > 0 && lycon->ui_context) {
                    setup_font(lycon->ui_context, &lycon->font, elem->font);
                }
                float max_text_width = measure_direct_text_children_intrinsic_width(
                    lycon, elem, false, btn_text_transform);
                if (max_text_width > 0) {
                    // Store intrinsic size in form property for flex-basis calculation
                    elem->form->intrinsic_width = max_text_width;

                    // Content-area height: use line-height from font metrics (buttons use normal
                    // block layout, so their content height is determined by the text line-height)
                    float btn_content_height = FormDefaults::TEXT_HEIGHT
                        - 2 * (FormDefaults::BUTTON_PADDING_V + FormDefaults::BUTTON_BORDER);  // fallback: 15
                    if (elem->font && elem->fontp()->font_size > 0 && lycon->ui_context) {
                        btn_content_height =
                            flex_measure_normal_line_height_for_font(
                                lycon, elem->font, btn_content_height);
                    }
                    elem->form->intrinsic_height = btn_content_height;

                    // Update content sizes (intrinsic, without padding/border)
                    // Padding/border will be added below in the generic code
                    content_width = max_text_width;
                    measured_width = content_width;
                    content_height = btn_content_height;
                    measured_height = content_height;

                }
            }

        }

        // Add padding and border to measured height for total height
        // CSS box model: total_height = content_height + padding + border
        if (elem && elem->bound) {
            measured_height += layout_boundary_metrics(elem->bound).pad_border_v;
        }

    }

    // Store measurement results (include the container width used during measurement)
    store_in_measurement_cache(child, measured_width, measured_height,
                              content_width, content_height,
                              saved_context.block.content_width);

    // Restore original context, but preserve depth and node_count guards
    int current_depth = lycon->depth;
    int current_node_count = lycon->node_count;
    *lycon = saved_context;
    lycon->depth = current_depth;
    lycon->node_count = current_node_count;

}

// Enhanced accurate text measurement for intrinsic sizing
void measure_text_content_accurate(LayoutContext* lycon, DomNode* text_node,
                                   int* min_width, int* max_width, int* height) {
    const char* text_data = (const char*)text_node->text_data();
    size_t text_length = text_data ? strlen(text_data) : 0;

    if (!text_data || text_length == 0) {
        *min_width = *max_width = *height = 0;
        return;
    }

    TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text_data, text_length);
    *max_width = widths.max_content;
    *min_width = widths.min_content;
    *height = (lycon->font.style && lycon->font.style->font_size > 0) ?
              (int)(lycon->font.style->font_size + 0.5f) : 20; // INT_CAST_OK: font size for text height

}

// Create lightweight View for flex item element only (no child processing)
void init_flex_item_view(LayoutContext* lycon, DomNode* node) {
    if (!node || !node->is_element()) return;

    // Get display properties for the element
    DisplayValue display = resolve_display_value(node);

    // CSS Flexbox §4: display:none elements do not generate flex items and must
    // not have a View created. Without this check, a ViewBlock is allocated and
    // linked into the view tree, causing display:none children (e.g. hidden
    // dropdown menus) to be rendered.
    if (layout_display_is_none(display)) {
        return;
    }

    // CSS Display Level 3 §3: Flex items have their display blockified.
    // inline → block, inline-block → block, inline-table → table, inline-flex → flex, inline-grid → grid
    display = blockify_display(display);

    // Create ViewBlock directly (similar to layout_block but without child processing)
    ViewBlock* block = lam::view_require_block(set_view(lycon,
        display.outer == CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK :
        display.outer == CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM :
        display.inner == CSS_VALUE_TABLE ? RDT_VIEW_TABLE : RDT_VIEW_BLOCK,
        node));

    if (!block) {
        log_error("Failed to allocate View for flex item: %s", node->node_name());
        return;
    }

    block->display = display;

    // reset flex-item CSS defaults before re-resolving styles on a reused view.
    if (!node->as_element()->styles_resolved()) {
        reset_flex_item_prop_for_style(lycon, block);
    }

    // Set up basic CSS properties (minimal setup for flex items)
    dom_node_resolve_style(node, lycon);

    // CRITICAL FIX: Ensure flex item properties are allocated
    // Even if no flex CSS properties are specified, we need fi for the flex algorithm
    alloc_flex_item_prop(lycon, block);

    // Initialize dimensions (will be set by flex algorithm)
    block->width = 0;  block->height = 0;
    block->content_width = 0;  block->content_height = 0;

}

// ============================================================================
// Enhanced Intrinsic Sizing Implementation
// ============================================================================

// Calculate intrinsic sizes for a flex item
static bool flex_measure_pseudo_content(LayoutContext* lycon, DomElement* item,
                                        int pseudo_element, const char* label,
                                        float* width, float* height) {
    const char* content = dom_element_get_pseudo_element_content(item, pseudo_element);
    if (!content || !*content) return false;

    LayoutFontScope font_scope(lycon);
    if (item->font) setup_font(lycon->ui_context, &lycon->font, item->font);
    TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, content, strlen(content));
    float line_height = lycon->font.style && lycon->font.style->font_size > 0
        ? lycon->font.style->font_size : 16.0f;
    if (lycon->font.font_handle) line_height = calc_normal_line_height(lycon->font.font_handle);

    *width += widths.max_content;
    if (line_height > *height) *height = line_height;
    return true;
}

void calculate_item_intrinsic_sizes(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item) {
        return;
    }

    // Widget intrinsic sizes remain role data even though flex style and scratch
    // now live in the independent FlexItemProp parent-item slot.
    if (item->role_kind() == DomElement::ROLE_FORM) {
        return;
    }

    if (!has_flex_item_prop(item)) {
        return;
    }

    // Skip if BOTH intrinsic sizes are already calculated
    // We need both because cross-axis alignment may need the cross-axis intrinsic size
    if (item->fi->has_intrinsic_width && item->fi->has_intrinsic_height) {
        return;
    }


    // CRITICAL FIX: Set up font for the flex item BEFORE measuring text
    // This ensures text measurement uses the correct font (e.g., bold, specific size)
    LayoutContext* lycon = flex_layout ? flex_layout->lycon : nullptr;
    float percentage_height_basis = -1.0f;
    if (flex_layout) {
        bool row_flex = is_main_axis_horizontal(flex_layout);
        if (row_flex && flex_layout->has_definite_cross_size) {
            percentage_height_basis = flex_layout->cross_axis_size;
        } else if (!row_flex && !flex_layout->main_axis_is_indefinite) {
            percentage_height_basis = flex_layout->main_axis_size;
        }
    }
    // Intrinsic queries re-resolve styles; preserve the flex container's
    // definite cross/main height instead of the stale outer block height.
    LayoutContainingBlockScope flex_height_scope(
        lycon, LAYOUT_AXIS_Y, percentage_height_basis);
    LayoutFontScope font_scope(lycon);
    FontProp* intrinsic_font = item->font;
    if (!intrinsic_font) {
        ViewElement* parent = item->parent_view();
        intrinsic_font = parent ? parent->font : nullptr;
    }
    if (lycon && intrinsic_font) {
        setup_font(lycon->ui_context, &lycon->font, intrinsic_font);
    }

    // Initialize to zero
    // Use float to preserve precision from text measurement (avoids truncation)
    float min_width = 0, max_width = 0, min_height = 0, max_height = 0;

    // Check if this is a replaced element (img, video, or SVG) - needs special handling
    NameId elmt_name = item->tag();
    bool is_replaced = (elmt_name == MARKUP_NAME_IMG || elmt_name == MARKUP_NAME_VIDEO ||
                        elmt_name == MARKUP_NAME_IFRAME || elmt_name == MARKUP_NAME_CANVAS ||
                        elmt_name == MARKUP_NAME_SVG);

    if (is_replaced && lycon && elmt_name == MARKUP_NAME_SVG) {
        Element* native_elem = dom_element_backing(lam::dom_require_element(item));
        SvgIntrinsicSize intrinsic = calculate_svg_intrinsic_size(native_elem);
        float intrinsic_width = intrinsic.width;
        float intrinsic_height = intrinsic.height;

        if (intrinsic.has_intrinsic_width && intrinsic.has_intrinsic_height) {
            // SVG width/height attributes are natural dimensions.
        } else if (intrinsic.has_intrinsic_width && intrinsic.has_intrinsic_aspect_ratio) {
            intrinsic_height = intrinsic_width / intrinsic.aspect_ratio;
        } else if (intrinsic.has_intrinsic_height && intrinsic.has_intrinsic_aspect_ratio) {
            intrinsic_width = intrinsic_height * intrinsic.aspect_ratio;
        } else if (intrinsic.has_intrinsic_aspect_ratio) {
            bool main_is_horizontal = is_main_axis_horizontal(flex_layout);
            float available_inline = main_is_horizontal
                ? flex_layout->main_axis_size : flex_layout->cross_axis_size;
            bool available_inline_is_definite = main_is_horizontal
                ? flex_layout->main_axis_available_size_is_definite
                : flex_layout->has_definite_cross_size;
            if (available_inline_is_definite && available_inline > 0.0f) {
                float inline_border_size = layout_stretch_fit_border_box_size(
                    lam::view_as_block(item), available_inline, main_is_horizontal);
                float inline_content_size = layout_content_size_from_border_box(
                    lam::view_as_block(item), inline_border_size, main_is_horizontal);
                if (main_is_horizontal) {
                    intrinsic_width = inline_content_size;
                    intrinsic_height = inline_content_size / intrinsic.aspect_ratio;
                } else {
                    intrinsic_height = inline_content_size;
                    intrinsic_width = inline_content_size * intrinsic.aspect_ratio;
                }
            } else {
                // CSS Sizing 3 §5.1: without a definite available inline size,
                // a ratio-only replaced box uses the default object width.
                intrinsic_width = 300.0f;
                intrinsic_height = intrinsic_width / intrinsic.aspect_ratio;
            }
        }

        min_width = max_width = intrinsic_width;
        min_height = max_height = intrinsic_height;
        flex_store_intrinsic_sizes(item, min_width, max_width, min_height, max_height);
        return;
    }

    if (is_replaced && lycon && elmt_name == MARKUP_NAME_IMG) {
        // Load image to get intrinsic dimensions
        const char* src_value = item->get_attribute("src");
        if (src_value) {
            if (!item->embed) {
                item->ensure_embed(lycon);
            }
            if (!item->embedp()->img) {
                item->embed->img = load_image(lycon->ui_context, src_value);
            }
            if (item->embedp()->img) {
                ImageSurface* img = item->embedp()->img;
                float w = img->width * lycon->ui_context->pixel_ratio;
                float h = img->height * lycon->ui_context->pixel_ratio;

                // Check for explicit CSS dimensions
                float explicit_width = layout_axis_has_given_size(item, true) ?
                    item->block()->given_width : -1;
                float explicit_height = layout_axis_has_given_size(item, false) ?
                    item->block()->given_height : -1;
                float preferred_aspect_ratio = layout_used_preferred_aspect_ratio(
                    lam::view_as_block(item));
                float used_aspect_ratio = preferred_aspect_ratio > 0.0f
                    ? preferred_aspect_ratio : w / h;

                // Also check max-width as constraint
                float max_width_constraint = layout_positive_max_axis_or(
                    lam::view_as_block(item), true, -1.0f);

                if (explicit_width > 0 && explicit_height > 0) {
                    // Both dimensions specified
                    min_width = max_width = explicit_width;
                    min_height = max_height = explicit_height;
                } else if (explicit_width > 0) {
                    // A definite CSS axis transfers through the preferred ratio;
                    // using the natural ratio here inflates flex auto-minimums.
                    min_width = max_width = explicit_width;
                    min_height = max_height = explicit_width / used_aspect_ratio;
                } else if (explicit_height > 0) {
                    // Keep intrinsic contributions consistent with normal replaced sizing.
                    min_height = max_height = explicit_height;
                    min_width = max_width = explicit_height * used_aspect_ratio;
                } else if (max_width_constraint > 0 && max_width_constraint < w) {
                    // Max-width constrains the image
                    min_width = max_width = max_width_constraint;
                    min_height = max_height = max_width_constraint * h / w;
                } else {
                    // Use intrinsic dimensions
                    min_width = max_width = w;
                    min_height = max_height = h;
                }
            } else {
                // Failed image data has no natural dimensions; use the same
                // missing-image indicator size as the normal <img> layout path.
                min_width = max_width = 16.0f;
                min_height = max_height = 16.0f;
            }
        } else {
            // HTML width attributes supply a replaced element's intrinsic
            // contribution even before an image request exists; without this,
            // Flexbox's auto minimum sees 0 and shrinks the declared box away.
            const char* width_attr = item->get_attribute("width");
            CssDeclaration* css_width_decl = item->specified_style
                ? style_tree_get_declaration(item->specified_style, CSS_PROPERTY_WIDTH)
                : nullptr;
            bool has_html_pixel_width = width_attr && !css_width_decl && item->blk &&
                item->block()->given_width >= 0.0f &&
                isnan(item->block()->given_width_percent);
            min_width = max_width = has_html_pixel_width
                ? min(item->block()->given_width, MAX_LAYOUT_DIMENSION) : 0.0f;
            min_height = max_height = 0.0f;
        }

        flex_store_intrinsic_sizes(item, min_width, max_width, min_height, max_height);


        return;
    }

    if (is_replaced && elmt_name == MARKUP_NAME_CANVAS) {
        ViewBlock* block = lam::view_as_block(item);
        float natural_width = 0.0f;
        float natural_height = 0.0f;
        if (block && block->blk &&
            layout_canvas_natural_size(block, &natural_width, &natural_height) &&
            natural_width > 0.0f && natural_height > 0.0f) {
            min_width = max_width = natural_width;
            min_height = max_height = natural_height;

            bool main_is_horizontal = is_main_axis_horizontal(flex_layout);
            bool cross_is_horizontal = !main_is_horizontal;
            CssEnum cross_size_type = cross_is_horizontal
                ? block->block()->given_width_type : block->block()->given_height_type;
            if (cross_size_type == CSS_VALUE_STRETCH && flex_layout->has_definite_cross_size) {
                float stretch_border_size = layout_stretch_fit_border_box_size(
                    block, flex_layout->cross_axis_size, cross_is_horizontal);
                float stretch_content_size = layout_content_size_from_border_box(
                    block, stretch_border_size, cross_is_horizontal);
                float natural_ratio = natural_width / natural_height;
                // A definite stretch cross size participates in the canvas's
                // min-content contribution before Flexbox §4.5 combines suggestions.
                if (cross_is_horizontal) {
                    min_width = max_width = stretch_content_size;
                    min_height = max_height = stretch_content_size / natural_ratio;
                } else {
                    min_height = max_height = stretch_content_size;
                    min_width = max_width = stretch_content_size * natural_ratio;
                }
            }
        }
        flex_store_intrinsic_sizes(item, min_width, max_width, min_height, max_height);
        return;
    }

    // Note: Form controls are handled in calculate_flex_basis directly since
    // they don't have fi (FlexItemProp) allocated - form properties use a union
    // with flex item properties, so form controls store their intrinsic sizes
    // in form->intrinsic_width/height instead.

    // Check if item has children to measure
    DomNode* child = item->first_child;
    if (!child) {
        // No children - check for pseudo-element content (::before/::after)
        // This is critical for icon fonts like FontAwesome which use ::before with content
        bool has_pseudo_content = false;
        float pseudo_width = 0, pseudo_height = 0;

        if (lycon) {
            DomElement* elem = item;
            bool has_before = dom_element_has_before_content(elem);
            bool has_after = dom_element_has_after_content(elem);

            if (has_before || has_after) {

                // Get content of pseudo-elements and measure using parent's font
                // For FontAwesome icons, the icon font-family is inherited from parent
                if (has_before) {
                    has_pseudo_content |= flex_measure_pseudo_content(
                        lycon, elem, PSEUDO_ELEMENT_BEFORE, "::before",
                        &pseudo_width, &pseudo_height);
                }

                if (has_after) {
                    has_pseudo_content |= flex_measure_pseudo_content(
                        lycon, elem, PSEUDO_ELEMENT_AFTER, "::after",
                        &pseudo_width, &pseudo_height);
                }
            }
        }

        // Intrinsic content size for empty elements = content-based measurement only.
        // Explicit CSS width/height are NOT intrinsic content sizes — they are extrinsic
        // "specified sizes" and are handled separately (e.g., in calculate_flex_basis for
        // the flex base size, and in resolve_flex_item_constraints as specified_size_suggestion
        // per CSS Flexbox §4.5). Using given_width/given_height here would conflate the
        // content_size_suggestion with the specified_size_suggestion, preventing items from
        // shrinking below their explicit size even when content is empty.
        if (elmt_name == MARKUP_NAME_BR) {
            // A column flex break between block-level flex items contributes a
            // line-box main advance; inline runs keep the browser's zero-size
            // break item behavior used by fieldsets and mixed inline content.
            float break_height = 0.0f;
            if (flex_layout && !is_main_axis_horizontal(flex_layout) &&
                flex_break_has_block_siblings(item)) {
                break_height = layout_br_line_box_extent(
                    lycon, lycon ? lycon->font.font_handle : nullptr);
            }
            min_height = max_height = break_height;
        } else if (has_pseudo_content) {
            min_width = max_width = pseudo_width;
            min_height = max_height = pseudo_height;
        } else {
            min_width = max_width = 0;
            min_height = max_height = 0;
        }

    } else if (child->is_text() && !child->next_sibling) {
        // Simple text node - use unified intrinsic sizing API if available
        const char* text = (const char*)child->text_data();
        if (text) {
            size_t len = strlen(text);
            LayoutContext* lycon = flex_layout ? flex_layout->lycon : nullptr;
            if (lycon) {
                // Normalize whitespace according to the text node's CSS white-space property
                // before measuring so collapsed trailing spaces do not inflate intrinsic width.
                FlexMeasureTextRun run = flex_measure_prepare_text_run(child, text, len);

                // Look up the inherited text-transform before measuring text widths.
                CssEnum text_transform = layout_inherited_text_transform(item);

                // Use accurate backend font measurement
                TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, run.text, run.length, text_transform);
                min_width = widths.min_content;
                max_width = widths.max_content;

                // CSS Generated Content: add ::before/::after pseudo-element content widths.
                // These inline pseudo-elements participate in the element's inline formatting
                // context and contribute to its intrinsic size (CSS Sizing §5.1).
                {
                    DomElement* elem = item;
                    for (int pi = 0; pi < 2; pi++) {
                        bool is_before = (pi == 0);
                        bool has_pseudo = is_before ? dom_element_has_before_content(elem)
                                                    : dom_element_has_after_content(elem);
                        if (!has_pseudo) continue;
                        const char* pc = dom_element_get_pseudo_element_content(elem,
                            is_before ? PSEUDO_ELEMENT_BEFORE : PSEUDO_ELEMENT_AFTER);
                        if (pc && *pc) {
                            TextIntrinsicWidths pw = measure_text_intrinsic_widths(lycon, pc, strlen(pc));
                            max_width += pw.max_content;
                            if (pw.min_content > min_width) min_width = pw.min_content;
                        }
                    }
                }

                // Calculate height using CSS line-height if available, otherwise font metrics
                // Line-height is inherited; font-relative lengths/percentages are
                // computed on the declaring ancestor, while unitless numbers use
                // the target element's font-size.
                float resolved_line_height = flex_resolve_inherited_line_height(lycon, item);

                // Use resolved line-height, or fallback to font metrics
                if (resolved_line_height > 0) {
                    min_height = max_height = resolved_line_height;
                } else if (lycon->font.font_handle) {
                    min_height = max_height = calc_normal_line_height(lycon->font.font_handle);
                } else if (lycon->font.style && lycon->font.style->font_size > 0) {
                    min_height = max_height = lycon->font.style->font_size;
                } else {
                    min_height = max_height = 20.0f;
                }
                float available_width = flex_measure_item_content_width_for_height(
                    lycon, item, flex_layout);
                ViewBlock* item_block = lam::view_as_block(item);
                bool max_content_inline_size = item_block && item_block->blk &&
                    (item_block->block()->given_width_type == CSS_VALUE_MAX_CONTENT ||
                     item_block->block()->given_min_width_type == CSS_VALUE_MAX_CONTENT ||
                     item_block->block()->given_max_width_type == CSS_VALUE_MAX_CONTENT);
                if (max_content_inline_size) {
                    // Max-content sizing removes the container-width wrap
                    // constraint; its intrinsic text height must use max-content.
                    available_width = max_width;
                }
                CssEnum white_space = get_white_space_value(child);
                bool preserve_single_line_intrinsic_height =
                    (item->fi && item->fi->aspect_ratio > 0.0f) ||
                    flex_measure_item_uses_vertical_writing_mode(item);
                if (available_width > 0.0f && available_width < max_width &&
                    white_space != CSS_VALUE_NOWRAP && white_space != CSS_VALUE_PRE &&
                    !preserve_single_line_intrinsic_height) {
                    // single text children in flex items still wrap at the used
                    // content width; a one-line intrinsic height lets column flex
                    // shrink below the text's automatic minimum.
                    CssEnum font_variant = get_element_font_variant(item);
                    min_height = max_height = compute_text_height_at_width(
                        lycon, run.text, run.length, available_width, min_height,
                        text_transform, font_variant);
                }
            } else {
                // Fallback: rough estimation when no layout context
                max_width = len * 10.0f;
                float current_word = 0.0f;
                min_width = 0.0f;
                for (size_t i = 0; i < len; i++) {
                    if (is_space(text[i])) {
                        min_width = fmaxf(min_width, current_word * 10.0f);
                        current_word = 0.0f;
                    } else {
                        current_word += 1.0f;
                    }
                }
                min_width = fmaxf(min_width, current_word * 10.0f);
                min_height = max_height = 20.0f;
            }
        }
    } else {
        // CRITICAL FIX: For items without explicit dimensions, the cached values may be
        // based on container size, not intrinsic size. In such cases, we should NOT use
        // the cache for the axis that doesn't have an explicit size.
        MeasurementCacheEntry* cached = get_from_measurement_cache(static_cast<DomNode*>(item));
        bool has_explicit_height = layout_axis_has_given_size(item, false);

        // Check if this item is a row flex container
        // For row flex containers, the cached height from measure_flex_child_content might be incorrect
        // because it sums child heights instead of taking the max
        bool is_flex_container = layout_is_flex_container(item);
        bool is_row_flex_container = is_flex_container ?
            layout_flex_direction_is_row(item) : false;

        // CRITICAL FIX: For non-flex containers (regular block elements with inline content),
        // use measure_element_intrinsic_widths which correctly sums inline children's widths.
        // The manual child iteration below doesn't handle inline content properly - it takes
        // max of children's widths instead of summing for inline elements.
        // IMPORTANT: Use content_only=true to get CONTENT-BASED min-content, excluding the
        // element's own explicit CSS width. This is critical for CSS Flexbox §4.5's
        // content_size_suggestion: the intrinsic width should represent how much space the
        // content needs, NOT the specified CSS width (which is handled separately as
        // specified_size_suggestion in resolve_flex_item_constraints).
        LayoutContext* lycon = flex_layout ? flex_layout->lycon : nullptr;
        if (!is_flex_container && lycon) {
            IntrinsicSizes item_sizes = measure_element_intrinsic_widths(
                lycon, lam::dom_require<DOM_NODE_ELEMENT>(item), /*content_only=*/true);
            min_width = item_sizes.min_content;
            max_width = item_sizes.max_content;
            // measure_element_intrinsic_widths returns border-box values (includes
            // the element's own padding+border). Convert back to content-box so all
            // stored intrinsic sizes are content-box — resolve_flex_item_constraints
            // adds padding+border when converting to border-box for comparison.
            if (item->bound) {
                float hp = layout_boundary_metrics(item->bound).pad_border_h;
                min_width -= hp;
                max_width -= hp;
                if (min_width < 0) min_width = 0;
                if (max_width < 0) max_width = 0;
            }

            // Always use calculate_max_content_height for accurate height measurement.
            // The cached heights from measure_flex_child_content use inaccurate
            // hardcoded tag-based estimates (e.g. p=36px, h1=32px + artificial margins).
            {
                float available_width = 10000.0f;
                float percentage_containing_width = -1.0f;
                // A stretched column-flex item uses the container cross size. A
                // non-stretch item instead uses its max-content cross size; using
                // the container width would transfer that arbitrary size through
                // aspect-ratio into its flex base size.
                if (flex_layout && !is_main_axis_horizontal(flex_layout) &&
                    flex_layout->cross_axis_size > 0) {
                    percentage_containing_width = flex_layout->cross_axis_size;
                    if (flex_item_will_stretch_cross_axis(item, flex_layout)) {
                        available_width = flex_layout->cross_axis_size;
                        // Subtract item's own margin from container cross-axis.
                        if (item->bound) {
                            available_width -= item->boundary()->margin.left +
                                item->boundary()->margin.right;
                        }
                    } else {
                        available_width = max_width;
                    }

                    if (item->blk) {
                        if (item->block()->given_width >= 0.0f) {
                            available_width = item->block()->given_width;
                        }
                        available_width = layout_clamp_positive_min_max_width(
                            lam::view_as_block(item), available_width);
                    }
                    // Subtract item's padding+border to get content width
                    if (item->bound) {
                        available_width -= layout_boundary_metrics(item->bound).pad_border_h;
                    }
                    if (available_width < 0.0f) available_width = 0.0f;
                }
                min_height = max_height = flex_measure_intrinsic_max_height(
                    lycon, static_cast<DomNode*>(item), available_width,
                    percentage_containing_width);
                // calculate_max_content_height returns border-box values (includes the
                // element's own padding+border). Convert back to content-box so all
                // stored intrinsic sizes are content-box — resolve_flex_item_constraints
                // adds padding+border when converting to border-box for comparison.
                // (Same conversion as done for width above.)
                if (item->bound) {
                    float vp = layout_boundary_metrics(item->bound).pad_border_v;
                    min_height -= vp;
                    max_height -= vp;
                    if (min_height < 0) min_height = 0;
                    if (max_height < 0) max_height = 0;
                }
            }

            // Skip the manual child iteration since we've already calculated sizes
            goto store_results;
        }

        // CSS Flexbox §9.9.1: Detect flex-wrap to compute min-content correctly.
        // - nowrap: min-content = sum of item outer min-content sizes
        // - wrap: min-content = max of individual item outer min-content sizes
        bool is_wrapping_flex = false;
        if (is_flex_container && is_row_flex_container) {
            is_wrapping_flex = layout_flex_wraps(item);
        }

        // First, try to calculate intrinsic sizes from children
        // This handles both width and height by traversing child elements
        // Track min and max content widths separately per CSS intrinsic sizing spec
        float min_child_width = 0.0f;  // For min-content: max of children's min-content
        float max_child_width = 0.0f;  // For max-content: max of children's max-content
        float total_child_min_width = 0.0f;  // For nowrap row flex: sum of min-content contributions
        float total_child_width = 0.0f;  // For row flex containers: sum of child widths
        float max_single_child_width = 0.0f;  // For wrapping row flex: max of individual items
        float total_child_height = 0.0f;
        int child_count = 0;  // Count children for gap calculation

        {
            DomNode* c = child;

            // Set up parent context with item's height so children with percentage heights
            // and aspect-ratio can compute their intrinsic width
            float item_height = -1.0f;
            if (lycon) {
                item_height = layout_axis_has_given_size(item, false)
                    ? item->block()->given_height
                    : get_explicit_css_length(lycon, item, CSS_PROPERTY_HEIGHT);
            }
            LayoutContainingBlockScope item_height_scope(
                lycon, LAYOUT_AXIS_Y, item_height, item_height > 0.0f);

            while (c) {
                if (layout_text_node_has_content(c)) {
                    const char* text = (const char*)c->text_data();
                    size_t text_len = strlen(text);
                    float text_min_width, text_max_width, text_height;
                    if (lycon) {
                        FlexMeasureTextRun run = flex_measure_prepare_text_run(c, text, text_len);

                        CssEnum text_transform = layout_inherited_text_transform(item);

                        TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, run.text, run.length, text_transform);
                        text_min_width = widths.min_content;
                        text_max_width = widths.max_content;
                        // BUGFIX: Use line height instead of font size for text height
                        // This matches browser behavior where text takes up line-height space
                        if (lycon->font.font_handle) {
                            text_height = calc_normal_line_height(lycon->font.font_handle);
                        } else if (lycon->font.style && lycon->font.style->font_size > 0) {
                            text_height = lycon->font.style->font_size;  // Fallback to font-size
                        } else {
                            text_height = 20.0f;  // Ultimate fallback
                        }
                    } else {
                        text_max_width = text_len * 10.0f;
                        text_min_width = text_max_width;  // Fallback: same as max
                        text_height = 20.0f;
                    }

                    // CRITICAL FIX: For row flex containers, text nodes should be summed
                    // into total_child_width just like element children. Previously text
                    // was only MAX'd which caused incorrect intrinsic width calculation
                    // for inline-flex containers with both text and element children.
                    if (is_row_flex_container) {
                        total_child_min_width += text_min_width;
                        total_child_width += text_max_width;  // Row flex: sum for max-content
                        child_count++;  // Count text as a child for gap calculation
                    } else {
                        min_child_width = max(min_child_width, text_min_width);
                        max_child_width = max(max_child_width, text_max_width);
                    }

                    // For height, row flex takes max, column flex sums
                    if (is_row_flex_container) {
                        total_child_height = max(total_child_height, text_height);
                    } else {
                        total_child_height += text_height;
                    }
                } else if (c->is_element()) {
                    ViewElement* child_view = lam::view_require_element(c);
                    if (child_view) {
                        float child_margin_inline_base =
                            flex_item_content_width_for_child_percentages(lycon, item, flex_layout);

                        // CSS Flexbox §4.1: Absolutely positioned children and display:none
                        // elements are not flex items and should not contribute to intrinsic sizing.
                        DisplayValue child_display = resolve_display_value((void*)c);
                        if (flex_measurement_child_is_skipped_flex_item(child_view, child_display)) {
                            c = c->next_sibling;
                            continue;
                        }

                        LayoutContext* lycon = flex_layout ? flex_layout->lycon : nullptr;
                        FlexChildExplicitSizes child_explicit =
                            flex_measure_child_explicit_sizes(lycon, child_view);

                        float child_min_width = 0.0f;
                        float child_max_width = 0.0f;
                        float child_height = 0.0f;
                        bool child_width_is_percentage = layout_axis_size_is_percentage(
                            c->as_element(), true) ||
                            layout_axis_size_is_percentage(lam::view_as_block(child_view), true);
                        float child_width_percentage = NAN;
                        CssDeclaration* child_width_decl = c->as_element()
                            ? dom_element_get_specified_value(c->as_element(), CSS_PROPERTY_WIDTH)
                            : nullptr;
                        if (child_width_decl && child_width_decl->value &&
                            child_width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            child_width_percentage = child_width_decl->value->data.percentage.value;
                        }

                        // Get child width - explicit (from View or DOM) or intrinsic
                        if (child_explicit.has_width) {
                            // Explicit width is both min and max
                            child_min_width = child_max_width = child_explicit.width;
                        } else if (child_display.inner == CSS_VALUE_FLEX && lycon) {
                            IntrinsicSizes child_sizes = flex_measure_child_intrinsic_widths(
                                lycon, child_view, /*content_only=*/true);
                            child_min_width = child_sizes.min_content;
                            child_max_width = child_sizes.max_content;
                        } else if (child_view->form_control()) {
                            if (child_width_is_percentage && lycon) {
                                IntrinsicSizes child_sizes = measure_element_intrinsic_widths(
                                    lycon, c->as_element(), true);
                                child_min_width = child_sizes.min_content;
                                child_max_width = child_sizes.max_content;
                                if (!isnan(child_width_percentage)) {
                                    // Flex item intrinsic sizing uses the percentage
                                    // size suggestion against the natural max-content size.
                                    child_min_width = child_max_width *
                                        child_width_percentage / 100.0f;
                                }
                            } else {
                                child_min_width = child_view->form->intrinsic_width;
                                child_max_width = child_view->form->intrinsic_width;
                            }
                            if (child_max_width <= 0.0f && lycon) {
                                IntrinsicSizes child_sizes = measure_element_intrinsic_widths(
                                    lycon, lam::dom_require<DOM_NODE_ELEMENT>(child_view));
                                child_min_width = child_sizes.min_content;
                                child_max_width = child_sizes.max_content;
                            }
                        } else if (has_flex_item_prop(child_view)) {
                            // Child has fi - use cached intrinsic or calculate
                            if (!child_view->fi->has_intrinsic_width) {
                                calculate_item_intrinsic_sizes(child_view, flex_layout);
                            }
                            if (child_view->fi->has_intrinsic_width) {
                                child_min_width = child_view->fi->intrinsic_width.min_content;
                                child_max_width = child_view->fi->intrinsic_width.max_content;
                            }
                        } else if (lycon) {
                            // Child doesn't have fi yet - use measure_element_intrinsic_widths
                            // This handles the case where intrinsic sizing runs before fi is initialized
                            IntrinsicSizes child_sizes = flex_measure_child_intrinsic_widths(
                                lycon, child_view, /*content_only=*/child_width_is_percentage);
                            child_min_width = child_sizes.min_content;
                            child_max_width = child_sizes.max_content;
                            if (!isnan(child_width_percentage)) {
                                child_min_width = child_max_width *
                                    child_width_percentage / 100.0f;
                            }
                        }

                        // Get child height - explicit (from View or DOM) or intrinsic
                        if (child_explicit.has_height) {
                            child_height = child_explicit.height;
                        } else if (is_row_flex_container && lycon && flex_layout &&
                                   !is_main_axis_horizontal(flex_layout) &&
                                   flex_layout->cross_axis_size > 0) {
                            child_height = flex_measure_row_child_height_at_estimated_share(
                                lycon, c, item, child_view, flex_layout);
                        } else if (child_view->form_control()) {
                            child_height = child_view->form->intrinsic_height;
                            if (child_height <= 0.0f && lycon) {
                                child_height = flex_measure_intrinsic_max_height(lycon, c, child_max_width);
                            }
                        } else if (child_display.inner == CSS_VALUE_FLEX && lycon) {
                            float available_width = flex_child_height_fallback_available_width(
                                lycon, item, child_view, flex_layout, is_row_flex_container, true);
                            // nested flex children may be measured before their own
                            // stretched width is assigned; use the parent's constrained
                            // content width so auto-min height sees wrapped text.
                            child_height = flex_measure_intrinsic_max_height(
                                lycon, c, available_width);
                        } else if (has_flex_item_prop(child_view)) {
                            // Child has fi - use cached intrinsic or calculate recursively
                            if (!child_view->fi->has_intrinsic_height) {
                                calculate_item_intrinsic_sizes(child_view, flex_layout);
                            }
                            if (child_view->fi->has_intrinsic_height) {
                                child_height = child_view->fi->intrinsic_height.max_content;
                            }
                        }

                        // CRITICAL: If child height is still 0 without explicit height,
                        // try to measure content-based height from the DOM tree
                        if (child_height == 0.0f && !child_explicit.has_height) {
                            child_height = flex_measure_zero_child_height_fallback(
                                c, lycon, item, child_view, flex_layout, is_row_flex_container);
                        }

                        // CSS Flexbox §9.9.1: Each flex item's contribution to the container's
                        // intrinsic size is its outer size (content + padding + border + margin).
                        // Add child margins to width/height for proper intrinsic sizing.
                        float child_h_margin = get_child_axis_margins(
                            lycon, child_view, true, child_margin_inline_base);
                        float child_v_margin = get_child_axis_margins(
                            lycon, child_view, false, child_margin_inline_base);

                        // For width: row flex sums widths, column flex takes max
                        // Track both min and max content widths separately
                        if (is_row_flex_container) {
                            total_child_min_width += child_min_width + child_h_margin;
                            float outer_width = child_max_width + child_h_margin;
                            total_child_width += outer_width;  // Row flex: sum for max-content
                            max_single_child_width = max(max_single_child_width, child_min_width + child_h_margin);
                        } else {
                            min_child_width = max(min_child_width, child_min_width + child_h_margin);
                            max_child_width = max(max_child_width, child_max_width + child_h_margin);
                        }
                        child_count++;

                        // For height, column flex containers sum heights, row flex takes max
                        float outer_child_height = child_height + child_v_margin;
                        if (is_row_flex_container) {
                            total_child_height = max(total_child_height, outer_child_height);
                        } else {
                            total_child_height += outer_child_height;
                        }

                    }
                }
                c = c->next_sibling;
            }

            // For row flex containers, add gaps to total width
            if (is_row_flex_container && child_count > 1) {
                // Get gap from the flex container properties
                float gap = layout_flex_column_gap(item);
                total_child_width += gap * (child_count - 1);
            }


        }

        // Determine intrinsic width from children or cache.
        // IMPORTANT: When the element has explicit CSS width, do NOT use cached->measured_width
        // as the intrinsic width. The cache stores the layout-result width (which includes the
        // explicit CSS width), but intrinsic width should represent the CONTENT-ONLY min-content
        // for CSS Flexbox §4.5's content_size_suggestion. Using the explicit width as intrinsic
        // would make auto min-width = min(explicit, explicit) = explicit, preventing shrinking.
        if (is_row_flex_container && total_child_width > 0.0f) {
            // CSS Flexbox §9.9.1: Row flex container intrinsic sizes
            max_width = total_child_width;  // max-content = sum of outer max-content contributions
            if (is_wrapping_flex) {
                // Wrapping: min-content = largest single item outer min-content
                min_width = max_single_child_width;
            } else {
                // Nowrap: min-content = sum of outer min-content contributions
                min_width = total_child_min_width;
            }
        } else if (min_child_width > 0.0f || max_child_width > 0.0f) {
            // Use properly tracked min and max content widths
            min_width = min_child_width;
            max_width = max_child_width;
        } else {
            min_width = max_width = 0;
        }

        // Use cached height if available and item has explicit height, otherwise use calculated
        // CRITICAL: cached->measured_height is border-box (includes padding+border added by
        // measure_flex_child_content). calculate_flex_basis will add padding again, so we must
        // store the CONTENT-ONLY height here to avoid double-counting.
        auto strip_padding_border = [&](float border_box_height) -> float {
            float content_h = border_box_height;
            if (item->bound) {
                content_h -= layout_boundary_metrics(item->bound).pad_border_v;
            }
            return (content_h > 0) ? content_h : 0;
        };
        if (cached && cached->measured_height > 0 && has_explicit_height) {
            min_height = strip_padding_border(cached->measured_height);
            max_height = min_height;
        } else if (total_child_height > 0.0f) {
            min_height = total_child_height;
            max_height = total_child_height;
        } else if (cached && cached->measured_height > 0) {
            // Fallback to cache without explicit height requirement
            min_height = strip_padding_border(cached->measured_height);
            max_height = min_height;
        } else {
            min_height = max_height = 0;
        }
    }

store_results:
    // For vertical writing modes, swap intrinsic width/height because text measurement
    // always produces horizontal metrics, but in vertical-lr/rl the inline axis is vertical
    {
        ViewBlock* block_view = lam::view_as_block(item);
        WritingMode writing_mode = layout_block_writing_mode(block_view);
        if (writing_mode == WM_VERTICAL_LR || writing_mode == WM_VERTICAL_RL) {
            float tmp;
            tmp = min_width;  min_width = min_height;  min_height = tmp;
            tmp = max_width;  max_width = max_height;  max_height = tmp;
        }
    }

    // Store results
    item->fi->intrinsic_width.min_content = min_width;
    item->fi->intrinsic_width.max_content = max_width;
    item->fi->intrinsic_height.min_content = min_height;
    item->fi->intrinsic_height.max_content = max_height;
    item->fi->has_intrinsic_width = 1;
    item->fi->has_intrinsic_height = 1;


}
