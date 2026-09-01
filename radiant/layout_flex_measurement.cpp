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

LayoutTextRun flex_measure_prepare_text_run(DomNode* text_node, const char* text, size_t length) {
    CssEnum ws = get_white_space_value(text_node);
    return layout_prepare_text_run(
        text, length,
        layout_white_space_collapses(ws)
            ? LAYOUT_TEXT_RUN_COLLAPSE : LAYOUT_TEXT_RUN_RAW);
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
    ViewBlock* child_block = lam::view_as_block(child_view);
    return child_block
        ? layout_block_is_skipped_container_item(child_block)
        : layout_display_is_none(child_display) ||
          layout_element_is_abs_or_fixed(child_view->as_element());
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

static float flex_font_line_height(LayoutContext* lycon, float fallback) {
    if (!lycon) return fallback;
    if (font_box_handle(&lycon->font)) return calc_normal_line_height(font_box_handle(&lycon->font));
    return lycon->font.style && lycon->font.style->font_size > 0.0f
        ? lycon->font.style->font_size : fallback;
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

static float get_child_axis_margins(LayoutContext* lycon, ViewElement* elem,
                                    bool horizontal, float inline_base = -1.0f) {
    CssPropertyCode start = horizontal ? CSS_PROPERTY_MARGIN_LEFT : CSS_PROPERTY_MARGIN_TOP;
    CssPropertyCode end = horizontal ? CSS_PROPERTY_MARGIN_RIGHT : CSS_PROPERTY_MARGIN_BOTTOM;
    return layout_resolve_intrinsic_margin_side(lycon, elem, start, inline_base) +
           layout_resolve_intrinsic_margin_side(lycon, elem, end, inline_base);
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
// Nested flex content height measurement

static bool flex_measurement_tag_is_inline(NameId tag) {
    static const NameId inline_tags[] = {
        MARKUP_NAME_A, MARKUP_NAME_SPAN, MARKUP_NAME_EM, MARKUP_NAME_STRONG,
        MARKUP_NAME_B, MARKUP_NAME_I, MARKUP_NAME_SMALL, MARKUP_NAME_SUB,
        MARKUP_NAME_SUP, MARKUP_NAME_ABBR, MARKUP_NAME_CODE, MARKUP_NAME_KBD,
        MARKUP_NAME_MARK, MARKUP_NAME_Q, MARKUP_NAME_S, MARKUP_NAME_SAMP,
        MARKUP_NAME_VAR, MARKUP_NAME_TIME, MARKUP_NAME_U, MARKUP_NAME_CITE,
        MARKUP_NAME_BDI, MARKUP_NAME_BDO, MARKUP_NAME_BR
    };
    return layout_tag_in_list(tag, inline_tags,
                              sizeof(inline_tags) / sizeof(*inline_tags));
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

float flex_measure_intrinsic_max_height(LayoutContext* lycon, DomNode* node, float width,
                                        float percentage_containing_width) {
    if (!lycon || !node) return 0.0f;
    ViewBlock* block = node->is_element() ? lam::view_as_block(node->as_element()) : NULL;
    if (!block) return calculate_max_content_height(lycon, node, width);

    AvailableSpace available = AvailableSpace::make_width_definite(width);
    // The intrinsic query re-resolves style, so percentages must retain the
    // flex container's definite cross-size instead of the outer block width.
    LayoutContainingBlockScope percentage_parent_scope(
        lycon, LAYOUT_AXIS_X, percentage_containing_width,
        percentage_containing_width > 0.0f);
    return measure_intrinsic_sizes(lycon, block, available).max_content_height;
}

struct FlexChildExplicitSizes {
    bool has_width;
    bool has_height;
    float width;
    float height;
};

struct FlexIntrinsicAccumulator {
    bool row;
    float min_width;
    float max_width;
    float total_min_width;
    float total_width;
    float max_item_min_width;
    float height;
    int count;

    explicit FlexIntrinsicAccumulator(bool row_axis)
        : row(row_axis), min_width(0.0f), max_width(0.0f),
          total_min_width(0.0f), total_width(0.0f),
          max_item_min_width(0.0f), height(0.0f), count(0) {}

    void add_width(float min_content, float max_content, float margin) {
        if (row) {
            total_min_width += min_content + margin;
            total_width += max_content + margin;
            max_item_min_width = max(max_item_min_width, min_content + margin);
            count++;
        } else {
            min_width = max(min_width, min_content + margin);
            max_width = max(max_width, max_content + margin);
        }
    }

    void add_height(float content_height, float margin) {
        float outer_height = content_height + margin;
        height = row ? max(height, outer_height) : height + outer_height;
    }
};

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
    bool nested_flex = child_display.inner == CSS_VALUE_FLEX;
    if (!nested_flex && (child_display.outer != CSS_VALUE_BLOCK || !lycon)) {
        return 0.0f;
    }
    // intrinsic sizing owns flex-container height; keeping a local recursive
    // classifier here caused stale row/column and out-of-flow policy drift.
    float available_width = flex_child_height_fallback_available_width(
        lycon, item, child_view, flex_layout, is_row_flex_container, nested_flex);
    return flex_measure_intrinsic_max_height(lycon, child, available_width);
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
    if (!lycon || !child || get_from_measurement_cache(child)) return;

    float measured_width = 0.0f;
    float measured_height = 0.0f;
    float content_width = 0.0f;
    float content_height = 0.0f;
    if (child->is_text()) {
        const char* text = (const char*)child->text_data();
        size_t length = text ? strlen(text) : 0;
        TextIntrinsicWidths sizes = measure_text_intrinsic_widths(lycon, text, length);
        measured_width = content_width = sizes.max_content;
        float line_height = lycon->font.style && lycon->font.style->font_size > 0.0f
            ? lycon->font.style->font_size : 20.0f;
        measured_height = content_height = line_height;
    } else if (child->is_element()) {
        ViewElement* item = lam::view_as_element(child);
        DomElement* element = child->as_element();
        if (!item || !element) return;

        if (item->form_control()) {
            // Form sizing must run before generic intrinsic sizing; that API owns
            // native control padding and otherwise the button min-size is counted twice.
            IntrinsicSize form_size = layout_measure_form_control(
                lycon, lam::view_as_block(item), lycon->available_space);
            content_width = form_size.max_width;
            content_height = form_size.max_height;
            measured_width = content_width;
            measured_height = layout_border_size_from_content_box(
                lam::view_as_block(item), content_height, false);
            if (item->form->control_type == FORM_CONTROL_BUTTON && item->first_child) {
                LayoutFontScope font_scope(lycon);
                if (item->font && lycon->ui_context) {
                    setup_font(lycon->ui_context, &lycon->font, item->font);
                }
                float text_width = measure_direct_text_children_intrinsic_width(
                    lycon, item, false, layout_inherited_text_transform(item));
                if (text_width > 0.0f) {
                    item->form->intrinsic_width = text_width;
                    content_width = measured_width = text_width;
                }
            }
            if (item->form->control_type == FORM_CONTROL_SELECT &&
                !item->form->multiple && item->form->select_size <= 1) {
                LayoutFontScope font_scope(lycon);
                if (item->font && lycon->ui_context) {
                    setup_font(lycon->ui_context, &lycon->font, item->font);
                }
                // Flex measurement must include optgroup labels and indentation;
                // otherwise a select can shrink below the width used by form layout.
                float max_text_width = layout_select_option_text_width(lycon, element, false);
                float new_width = layout_select_combo_intrinsic_width(
                    max_text_width, !item->form->appearance_none);
                if (new_width > content_width) {
                    content_width = new_width;
                    item->form->intrinsic_width = new_width;
                }
            }
        } else {
            // Intrinsic sizing owns wrapping, replaced defaults, and block-flow margins;
            // the former cache path duplicated those rules with tag-specific estimates.
            IntrinsicSizes sizes = measure_element_intrinsic_widths(lycon, element, true);
            content_width = layout_content_size_from_border_box(
                lam::view_as_block(item), sizes.max_content, true);
            if (layout_axis_has_given_size(item, true)) {
                measured_width = item->block()->given_width;
                content_width = layout_css_size_to_content_box(
                    item->bound, layout_box_sizing(lam::view_as_block(item)),
                    measured_width, true);
            }

            measured_height = flex_measure_intrinsic_max_height(
                lycon, child, content_width);
            content_height = layout_content_size_from_border_box(
                lam::view_as_block(item), measured_height, false);
        }
    }

    store_in_measurement_cache(child, measured_width, measured_height,
                               content_width, content_height,
                               lycon->block.content_width);

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
// Enhanced Intrinsic Sizing Implementation
// Calculate intrinsic sizes for a flex item
static bool flex_measure_pseudo_content(LayoutContext* lycon, DomElement* item,
                                        int pseudo_element,
                                        float* width, float* height,
                                        float* min_width = nullptr) {
    const char* content = dom_element_get_pseudo_element_content(item, pseudo_element);
    if (!content || !*content) return false;

    LayoutFontScope font_scope(lycon);
    if (item->font) setup_font(lycon->ui_context, &lycon->font, item->font);
    TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, content, strlen(content));
    float line_height = flex_font_line_height(lycon, 16.0f);

    *width += widths.max_content;
    if (height && line_height > *height) *height = line_height;
    if (min_width && widths.min_content > *min_width) {
        *min_width = widths.min_content;
    }
    return true;
}

static bool flex_measure_pseudo_contents(LayoutContext* lycon, DomElement* item,
                                         float* width, float* height,
                                         float* min_width = nullptr) {
    const int pseudo_elements[2] = {
        PSEUDO_ELEMENT_BEFORE, PSEUDO_ELEMENT_AFTER
    };
    bool found = false;
    for (int i = 0; i < 2; i++) {
        found |= flex_measure_pseudo_content(
            lycon, item, pseudo_elements[i], width, height, min_width);
    }
    return found;
}

void calculate_item_intrinsic_sizes(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item) {
        return;
    }
    // Widget intrinsic sizes remain role data even though flex style and scratch
    // now live in the independent FlexItemProp parent-item slot.
    if (item->form_control()) {
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
                float w = img->width;
                float h = img->height;
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
            has_pseudo_content = flex_measure_pseudo_contents(
                lycon, item, &pseudo_width, &pseudo_height);
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
                    lycon, lycon ? font_box_handle(&lycon->font) : nullptr);
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
                LayoutTextRun run = flex_measure_prepare_text_run(child, text, len);
                // Look up the inherited text-transform before measuring text widths.
                CssEnum text_transform = layout_inherited_text_transform(item);
                // Use accurate backend font measurement
                TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, run.text, run.length, text_transform);
                min_width = widths.min_content;
                max_width = widths.max_content;
                // CSS Generated Content: add ::before/::after pseudo-element content widths.
                // These inline pseudo-elements participate in the element's inline formatting
                // context and contribute to its intrinsic size (CSS Sizing §5.1).
                flex_measure_pseudo_contents(lycon, item, &max_width, nullptr,
                                             &min_width);
                // Calculate height using CSS line-height if available, otherwise font metrics
                // Line-height is inherited; font-relative lengths/percentages are
                // computed on the declaring ancestor, while unitless numbers use
                // the target element's font-size.
                float resolved_line_height = flex_resolve_inherited_line_height(lycon, item);
                // Use resolved line-height, or fallback to font metrics
                if (resolved_line_height > 0) {
                    min_height = max_height = resolved_line_height;
                } else {
                    min_height = max_height = flex_font_line_height(lycon, 20.0f);
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
            min_width = layout_content_size_from_border_box(lam::view_as_block(item), min_width, true);
            max_width = layout_content_size_from_border_box(lam::view_as_block(item), max_width, true);
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
                    available_width = layout_content_size_from_border_box(
                        lam::view_as_block(item), available_width, true);
                }
                min_height = max_height = flex_measure_intrinsic_max_height(
                    lycon, static_cast<DomNode*>(item), available_width,
                    percentage_containing_width);
                min_height = layout_content_size_from_border_box(lam::view_as_block(item), min_height, false);
                max_height = layout_content_size_from_border_box(lam::view_as_block(item), max_height, false);
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
        FlexIntrinsicAccumulator children(is_row_flex_container);

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
                        LayoutTextRun run = flex_measure_prepare_text_run(c, text, text_len);

                        CssEnum text_transform = layout_inherited_text_transform(item);

                        TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, run.text, run.length, text_transform);
                        text_min_width = widths.min_content;
                        text_max_width = widths.max_content;
                        // text height follows font metrics when no CSS line-height is set.
                        text_height = flex_font_line_height(lycon, 20.0f);
                    } else {
                        text_max_width = text_len * 10.0f;
                        text_min_width = text_max_width;  // Fallback: same as max
                        text_height = 20.0f;
                    }

                    children.add_width(text_min_width, text_max_width, 0.0f);
                    children.add_height(text_height, 0.0f);
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

                        children.add_width(child_min_width, child_max_width, child_h_margin);
                        children.add_height(child_height, child_v_margin);

                    }
                }
                c = c->next_sibling;
            }
            // For row flex containers, add gaps to total width
            if (is_row_flex_container && children.count > 1) {
                // Get gap from the flex container properties
                float gap = layout_flex_column_gap(item);
                children.total_width += gap * (children.count - 1);
            }

        }
        // Determine intrinsic width from children or cache.
        // IMPORTANT: When the element has explicit CSS width, do NOT use cached->measured_width
        // as the intrinsic width. The cache stores the layout-result width (which includes the
        // explicit CSS width), but intrinsic width should represent the CONTENT-ONLY min-content
        // for CSS Flexbox §4.5's content_size_suggestion. Using the explicit width as intrinsic
        // would make auto min-width = min(explicit, explicit) = explicit, preventing shrinking.
        if (is_row_flex_container && children.total_width > 0.0f) {
            // CSS Flexbox §9.9.1: Row flex container intrinsic sizes
            max_width = children.total_width;  // max-content = sum of outer max-content contributions
            if (is_wrapping_flex) {
                // Wrapping: min-content = largest single item outer min-content
                min_width = children.max_item_min_width;
            } else {
                // Nowrap: min-content = sum of outer min-content contributions
                min_width = children.total_min_width;
            }
        } else if (children.min_width > 0.0f || children.max_width > 0.0f) {
            // Use properly tracked min and max content widths
            min_width = children.min_width;
            max_width = children.max_width;
        } else {
            min_width = max_width = 0;
        }
        // Use cached height if available and item has explicit height, otherwise use calculated
        // CRITICAL: cached->measured_height is border-box (includes padding+border added by
        // measure_flex_child_content). calculate_flex_basis will add padding again, so we must
        // store the CONTENT-ONLY height here to avoid double-counting.
        if (cached && cached->measured_height > 0 && has_explicit_height) {
            min_height = layout_content_size_from_border_box(lam::view_as_block(item), cached->measured_height, false);
            max_height = min_height;
        } else if (children.height > 0.0f) {
            min_height = children.height;
            max_height = children.height;
        } else if (cached && cached->measured_height > 0) {
            // Fallback to cache without explicit height requirement
            min_height = layout_content_size_from_border_box(lam::view_as_block(item), cached->measured_height, false);
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

    flex_store_intrinsic_sizes(item, min_width, max_width, min_height, max_height);
}
