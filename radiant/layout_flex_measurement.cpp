#include "layout.hpp"
#include "view.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"

#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/tagged.hpp"
#include <float.h>
#include <limits.h>

// Helper: Check if whitespace should be collapsed according to white-space property
// Returns true for: normal, nowrap, pre-line
static inline bool should_collapse_whitespace(CssEnum ws) {
    return ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_NOWRAP ||
           ws == CSS_VALUE_PRE_LINE || ws == 0;  // 0 = undefined, treat as normal
}

// Helper: Normalize whitespace to a buffer
// Collapses consecutive whitespace to single space, trims leading/trailing
// Returns length of normalized text (0 if all whitespace)
static size_t normalize_whitespace_for_flex(const char* text, size_t length, char* buffer, size_t buffer_size) {
    if (!text || length == 0 || !buffer || buffer_size == 0) return 0;

    size_t out_pos = 0;
    bool in_whitespace = true;  // Start as if preceded by whitespace (trims leading)

    for (size_t i = 0; i < length && out_pos < buffer_size - 1; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f') {
            if (!in_whitespace) {
                buffer[out_pos++] = ' ';  // Collapse to single space
                in_whitespace = true;
            }
        } else {
            buffer[out_pos++] = (char)ch;
            in_whitespace = false;
        }
    }

    // Trim trailing whitespace
    while (out_pos > 0 && buffer[out_pos - 1] == ' ') {
        out_pos--;
    }

    buffer[out_pos] = '\0';
    return out_pos;
}

struct FlexMeasureTextRun {
    const char* text;
    size_t length;
};

static FlexMeasureTextRun flex_measure_prepare_text_run(DomNode* text_node, const char* text, size_t length) {
    FlexMeasureTextRun run = {text, length};
    if (!text) return run;

    CssEnum ws = get_white_space_value(text_node);
    if (!should_collapse_whitespace(ws)) return run;

    static thread_local char normalized_buffer[4096];  // LARGE_ARRAY_OK: static buffer — not on call stack.
    run.length = normalize_whitespace_for_flex(text, length, normalized_buffer, sizeof(normalized_buffer));
    run.text = normalized_buffer;
    log_debug("flex_measure_prepare_text_run: normalized intrinsic text run for ws=%d", ws);
    return run;
}

static bool css_flex_direction_keyword_is_row(CssEnum direction, bool* recognized) {
    if (recognized) *recognized = true;
    if (direction == CSS_VALUE_ROW || direction == CSS_VALUE_ROW_REVERSE) {
        return true;
    }
    if (direction == CSS_VALUE_COLUMN || direction == CSS_VALUE_COLUMN_REVERSE) {
        return false;
    }
    if (recognized) *recognized = false;
    return true;
}

static bool flex_measurement_style_declares_display(ViewElement* elem,
                                                    CssEnum display,
                                                    CssEnum inline_display) {
    if (!elem || !elem->specified_style) return false;
    CssDeclaration* display_decl = style_tree_get_declaration(
        elem->specified_style, CSS_PROPERTY_DISPLAY);
    if (!display_decl || !display_decl->value ||
        display_decl->value->type != CSS_VALUE_TYPE_KEYWORD) {
        return false;
    }
    CssEnum display_val = display_decl->value->data.keyword;
    return display_val == display || display_val == inline_display;
}

static bool flex_measurement_style_declares_flex_display(ViewElement* elem) {
    return flex_measurement_style_declares_display(
        elem, CSS_VALUE_FLEX, CSS_VALUE_INLINE_FLEX);
}

static FlexProp* flex_measurement_embedded_flex(ViewElement* elem) {
    if (!elem) return nullptr;
    if (elem->view_type != RDT_VIEW_BLOCK && elem->view_type != RDT_VIEW_INLINE_BLOCK) {
        return nullptr;
    }
    ViewBlock* block_view = lam::view_as_block(elem);
    return block_view && block_view->embed ? block_view->embed->flex : nullptr;
}

static bool flex_measurement_direction_is_row(ViewElement* elem) {
    if (!elem) return true;

    if (elem->specified_style) {
        CssDeclaration* dir_decl = style_tree_get_declaration(
            elem->specified_style, CSS_PROPERTY_FLEX_DIRECTION);
        if (dir_decl && dir_decl->value && dir_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
            bool recognized = false;
            bool is_row = css_flex_direction_keyword_is_row(
                dir_decl->value->data.keyword, &recognized);
            if (recognized) {
                return is_row;
            }
        }

        CssDeclaration* flow_decl = style_tree_get_declaration(
            elem->specified_style, CSS_PROPERTY_FLEX_FLOW);
        if (flow_decl && flow_decl->value) {
            if (flow_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                bool recognized = false;
                bool is_row = css_flex_direction_keyword_is_row(
                    flow_decl->value->data.keyword, &recognized);
                if (recognized) {
                    return is_row;
                }
            } else if (flow_decl->value->type == CSS_VALUE_TYPE_LIST) {
                for (int i = 0; i < flow_decl->value->data.list.count; i++) {
                    CssValue* value = flow_decl->value->data.list.values[i];
                    if (!value || value->type != CSS_VALUE_TYPE_KEYWORD) continue;
                    bool recognized = false;
                    bool is_row = css_flex_direction_keyword_is_row(
                        value->data.keyword, &recognized);
                    if (recognized) {
                        return is_row;
                    }
                }
            }
        }

        if (flex_measurement_style_declares_flex_display(elem)) return true;
    }

    FlexProp* flex = flex_measurement_embedded_flex(elem);
    if (flex) {
        bool recognized = false;
        bool is_row = css_flex_direction_keyword_is_row(
            (CssEnum)flex->direction, &recognized);
        if (recognized) {
            return is_row;
        }
    }

    return true;
}

static bool flex_measurement_is_flex_container(ViewElement* elem) {
    if (!elem) return false;
    if (elem->display.inner == CSS_VALUE_FLEX) return true;

    if (flex_measurement_style_declares_flex_display(elem)) return true;

    return flex_measurement_embedded_flex(elem) != nullptr;
}

static float flex_measurement_embedded_column_gap(ViewElement* elem) {
    FlexProp* flex = flex_measurement_embedded_flex(elem);
    return flex ? flex->column_gap : 0.0f;
}

static bool flex_measurement_wrap_keyword_is_wrapping(CssEnum keyword) {
    return keyword == CSS_VALUE_WRAP || keyword == CSS_VALUE_WRAP_REVERSE;
}

static bool flex_measurement_style_declares_wrap(ViewElement* elem) {
    if (!elem || !elem->specified_style) return false;
    CssDeclaration* wrap_decl = style_tree_get_declaration(
        elem->specified_style, CSS_PROPERTY_FLEX_WRAP);
    return wrap_decl && wrap_decl->value &&
        wrap_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
        flex_measurement_wrap_keyword_is_wrapping(wrap_decl->value->data.keyword);
}

static bool flex_measurement_wraps(ViewElement* elem) {
    if (!elem) return false;

    FlexProp* flex = flex_measurement_embedded_flex(elem);
    if (flex && flex_measurement_wrap_keyword_is_wrapping((CssEnum)flex->wrap)) {
        return true;
    }

    return flex_measurement_style_declares_wrap(elem);
}

static bool flex_measurement_position_declares_out_of_flow(ViewElement* elem) {
    if (!elem || !elem->specified_style || !elem->specified_style->tree) return false;

    AvlNode* pos_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_POSITION);
    if (!pos_node) return false;
    StyleNode* sn = (StyleNode*)pos_node->declaration;
    if (!sn || !sn->winning_decl || !sn->winning_decl->value ||
        sn->winning_decl->value->type != CSS_VALUE_TYPE_KEYWORD) {
        return false;
    }
    CssEnum position = sn->winning_decl->value->data.keyword;
    return position == CSS_VALUE_ABSOLUTE || position == CSS_VALUE_FIXED;
}

static bool flex_measurement_child_is_skipped_flex_item(ViewElement* child_view,
                                                        DisplayValue child_display) {
    if (layout_display_is_none(child_display)) return true;

    ViewBlock* child_block = lam::view_as_block(child_view);
    if (child_block && child_block->position && child_block->position->position &&
        (child_block->position->position == CSS_VALUE_ABSOLUTE ||
         child_block->position->position == CSS_VALUE_FIXED)) {
        return true;
    }

    return flex_measurement_position_declares_out_of_flow(child_view);
}

static void flex_accumulate_child_cross_height(bool use_max_height, float height,
                                               float* max_height, float* sum_height) {
    if (use_max_height) {
        *max_height = fmax(*max_height, height);
    } else {
        *sum_height += height;
    }
}

static void flex_accumulate_intrinsic_child_height(bool is_row_flex, float height,
                                                   float* total_height) {
    if (is_row_flex) {
        *total_height = fmax(*total_height, height);
    } else {
        *total_height += height;
    }
}

// Forward declaration for recursive function
static float measure_content_height_recursive(DomNode* node, LayoutContext* lycon, int depth);

static float get_explicit_css_length(LayoutContext* lycon, ViewElement* elem,
                                     CssPropertyId property_id) {
    if (!elem || !elem->specified_style) return -1.0f;
    CssDeclaration* decl = style_tree_get_declaration(elem->specified_style, property_id);
    if (!decl || !decl->value || decl->value->type != CSS_VALUE_TYPE_LENGTH) return -1.0f;
    float size = resolve_length_value(lycon, property_id, decl->value);
    return !isnan(size) && size > 0.0f ? size : -1.0f;
}

static float get_explicit_css_width(LayoutContext* lycon, ViewElement* elem) {
    return get_explicit_css_length(lycon, elem, CSS_PROPERTY_WIDTH);
}

static bool flex_element_has_declared_line_height(DomElement* elem) {
    if (!elem || !elem->specified_style) return false;
    return style_tree_get_declaration(elem->specified_style, CSS_PROPERTY_LINE_HEIGHT) != nullptr ||
           style_tree_get_declaration(elem->specified_style, CSS_PROPERTY_FONT) != nullptr;
}

static float resolve_flex_inherited_line_height(LayoutContext* lycon, DomElement* target) {
    if (!lycon || !target) return 0;
    float target_font_size = target->font && target->font->font_size > 0.0f
        ? target->font->font_size : lycon->font.current_font_size;

    for (DomElement* elem = target; elem; ) {
        bool has_declared_lh = flex_element_has_declared_line_height(elem);
        ViewBlock* view = lam::view_as_block(elem);
        if (has_declared_lh && view && view->blk && view->blk->line_height) {
            const CssValue* lh = view->blk->line_height;
            if (!(lh->type == CSS_VALUE_TYPE_KEYWORD && lh->data.keyword == CSS_VALUE_INHERIT)) {
                return layout_resolve_line_height_value(
                    lycon, lh, elem, target_font_size);
            }
        }

        if (has_declared_lh && elem->specified_style) {
            CssDeclaration* decl = style_tree_get_declaration(
                elem->specified_style, CSS_PROPERTY_LINE_HEIGHT);
            if (decl && decl->value &&
                !(decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                  decl->value->data.keyword == CSS_VALUE_INHERIT)) {
                return layout_resolve_line_height_value(
                    lycon, decl->value, elem, target_font_size);
            }
        }

        DomNode* parent = elem->parent;
        while (parent && !parent->is_element()) {
            parent = parent->parent;
        }
        elem = parent ? lam::dom_require<DOM_NODE_ELEMENT>(parent) : nullptr;
    }

    return 0;
}

static float get_explicit_css_height(LayoutContext* lycon, ViewElement* elem) {
    return get_explicit_css_length(lycon, elem, CSS_PROPERTY_HEIGHT);
}

static float get_explicit_dom_css_height(LayoutContext* lycon, DomElement* elem) {
    return elem ? get_explicit_css_height(lycon, lam::view_as_element(elem)) : -1.0f;
}

static CssValue* flex_margin_side_value(const CssValue* value, CssPropertyId property_id) {
    int side = 0;
    if (property_id == CSS_PROPERTY_MARGIN_TOP) {
        side = 0;
    } else if (property_id == CSS_PROPERTY_MARGIN_RIGHT) {
        side = 1;
    } else if (property_id == CSS_PROPERTY_MARGIN_BOTTOM) {
        side = 2;
    } else {
        side = 3;
    }
    return (CssValue*)css_box_shorthand_side_value(value, side);
}

static bool resolve_flex_margin_value(LayoutContext* lycon, CssPropertyId property_id,
                                      CssValue* value, float inline_base, float* out) {
    if (!value || !out) return false;
    if (layout_resolve_percentage_value(value, inline_base, out)) return true;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) return false;
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
        *out = 0.0f;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER ||
        value->type == CSS_VALUE_TYPE_KEYWORD) {
        float val = resolve_length_value(lycon, property_id, value);
        if (!isnan(val) && val >= 0.0f) {
            *out = val;
            return true;
        }
    }
    return false;
}

// Helper to get the resolved CSS margin for a specific side. Percentage
// margins resolve against the containing block inline size, which may differ
// from any earlier style-resolution context during intrinsic flex measurement.
static float get_css_margin(LayoutContext* lycon, ViewElement* elem,
                            CssPropertyId property_id, float inline_base) {
    if (!elem) return 0.0f;

    if (elem->specified_style && lycon) {
        float val = 0.0f;
        CssDeclaration* decl = style_tree_get_declaration(elem->specified_style, property_id);
        if (decl && decl->value &&
            resolve_flex_margin_value(lycon, property_id, decl->value, inline_base, &val)) {
            return val;
        }

        CssDeclaration* short_decl = style_tree_get_declaration(elem->specified_style, CSS_PROPERTY_MARGIN);
        CssValue* side_value = short_decl ? flex_margin_side_value(short_decl->value, property_id) : nullptr;
        if (side_value &&
            resolve_flex_margin_value(lycon, property_id, side_value, inline_base, &val)) {
            return val;
        }
    }

    if (elem->bound) {
        float val = 0.0f;
        switch (property_id) {
            case CSS_PROPERTY_MARGIN_LEFT:   val = elem->bound->margin.left; break;
            case CSS_PROPERTY_MARGIN_RIGHT:  val = elem->bound->margin.right; break;
            case CSS_PROPERTY_MARGIN_TOP:    val = elem->bound->margin.top; break;
            case CSS_PROPERTY_MARGIN_BOTTOM: val = elem->bound->margin.bottom; break;
            default: break;
        }
        if (!isnan(val) && val >= 0.0f) return val;
    }

    return 0.0f;
}

// Helper to get horizontal margins (left + right) for an element
static float get_child_horizontal_margins(LayoutContext* lycon, ViewElement* elem,
                                          float inline_base = -1.0f) {
    return get_css_margin(lycon, elem, CSS_PROPERTY_MARGIN_LEFT, inline_base)
         + get_css_margin(lycon, elem, CSS_PROPERTY_MARGIN_RIGHT, inline_base);
}

// Helper to get vertical margins (top + bottom) for an element
static float get_child_vertical_margins(LayoutContext* lycon, ViewElement* elem,
                                        float inline_base = -1.0f) {
    return get_css_margin(lycon, elem, CSS_PROPERTY_MARGIN_TOP, inline_base)
         + get_css_margin(lycon, elem, CSS_PROPERTY_MARGIN_BOTTOM, inline_base);
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

// ============================================================================
// Recursive DOM-based content height measurement for nested flex containers
// ============================================================================
// This function recursively traverses the DOM tree to find the content-based
// height of nested flex containers before their Views are fully initialized.
static const int MAX_MEASURE_DEPTH = 32;

static bool has_flex_item_prop(ViewElement* item) {
    return item && item->item_prop_type == DomElement::ITEM_PROP_FLEX && item->fi;
}

static float flex_measure_existing_view_height(ViewElement* elem) {
    if (!elem) return -1.0f;
    if (elem->blk && elem->blk->given_height >= 0.0f) return elem->blk->given_height;
    if (elem->height > 0.0f) return elem->height;
    if (has_flex_item_prop(elem) && elem->fi->has_intrinsic_height &&
        elem->fi->intrinsic_height.max_content > 0.0f) {
        return elem->fi->intrinsic_height.max_content;
    }
    return -1.0f;
}

static bool flex_measurement_tag_is_inline(uintptr_t tag) {
    switch (tag) {
    case HTM_TAG_A: case HTM_TAG_SPAN: case HTM_TAG_EM: case HTM_TAG_STRONG:
    case HTM_TAG_B: case HTM_TAG_I: case HTM_TAG_SMALL: case HTM_TAG_SUB:
    case HTM_TAG_SUP: case HTM_TAG_ABBR: case HTM_TAG_CODE: case HTM_TAG_KBD:
    case HTM_TAG_MARK: case HTM_TAG_Q: case HTM_TAG_S: case HTM_TAG_SAMP:
    case HTM_TAG_VAR: case HTM_TAG_TIME: case HTM_TAG_U: case HTM_TAG_CITE:
    case HTM_TAG_BDI: case HTM_TAG_BDO: case HTM_TAG_BR:
        return true;
    default:
        return false;
    }
}

static CssEnum flex_measure_resolve_text_transform(DomNode* start) {
    for (DomNode* node = start; node; node = node->parent) {
        if (!node->is_element()) continue;
        DomElement* elem = node->as_element();
        ViewBlock* view = lam::view_as_block(elem);
        if (view && view->blk && view->blk->text_transform != 0 &&
            view->blk->text_transform != CSS_VALUE_INHERIT) {
            return view->blk->text_transform;
        }
        if (!elem->specified_style) continue;
        CssDeclaration* decl = style_tree_get_declaration(
            elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
        if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
            CssEnum val = decl->value->data.keyword;
            if (val != CSS_VALUE_INHERIT && val != CSS_VALUE_NONE) {
                return val;
            }
        }
    }
    return CSS_VALUE_NONE;
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

static float flex_measure_intrinsic_max_height(LayoutContext* lycon, DomNode* node, float width) {
    if (!lycon || !node) return 0.0f;
    ViewBlock* block = node->is_element() ? lam::view_as_block(node->as_element()) : NULL;
    if (!block) return calculate_max_content_height(lycon, node, width);

    AvailableSpace available = AvailableSpace::make_width_definite(width);
    IntrinsicSizesBidirectional sizes = measure_intrinsic_sizes(lycon, block, available);
    return sizes.max_content_height;
}

static bool flex_measure_child_height_fallback(DomNode* child, LayoutContext* lycon, float* child_height) {
    if (!lycon) return false;

    ViewElement* child_view = lam::view_require_element(child);
    if (!child_view) return true;

    float existing_height = flex_measure_existing_view_height(child_view);
    if (existing_height >= 0.0f) {
        *child_height = existing_height;
        log_debug("measure_content_height_recursive: child %s existing height=%.1f",
                  child->node_name(), *child_height);
        return true;
    }

    *child_height = flex_measure_intrinsic_max_height(lycon, child, 10000.0f);
    log_debug("measure_content_height_recursive: child %s from calculate_max_content_height=%.1f",
              child->node_name(), *child_height);
    return true;
}

static float flex_measure_recursive_child_height(DomNode* child, LayoutContext* lycon, int depth) {
    float child_height = measure_content_height_recursive(child, lycon, depth + 1);
    if (child_height != 0.0f ||
        !flex_measure_child_height_fallback(child, lycon, &child_height)) {
        log_debug("measure_content_height_recursive: child %s height=%.1f",
                  child->node_name(), child_height);
    }
    return child_height;
}

struct FlexChildExplicitSizes {
    bool has_width;
    bool has_height;
    float width;
    float height;
};

struct FlexNestedContentSummary {
    bool has_text_content;
    bool has_element_content;
    bool has_child_with_explicit_height;
};

struct FlexDirectContentSummary {
    bool has_text_content;
    bool has_block_element;
    bool has_inline_element;
    int element_count;
};

struct FlexNestedHeightResult {
    float height;
    bool has_explicit_height_css;
};

struct FlexMeasuredElementHeight {
    float height;
    bool has_explicit_height_css;
};

static FlexNestedContentSummary flex_measure_nested_content_summary(LayoutContext* lycon,
                                                                    DomElement* elem) {
    FlexNestedContentSummary summary = {false, false, false};
    for (DomNode* content = elem ? elem->first_child : NULL; content; content = content->next_sibling) {
        if (layout_text_node_has_content(content)) {
            summary.has_text_content = true;
        } else if (content->is_element()) {
            summary.has_element_content = true;
            DomElement* nested = content->as_element();
            if (get_explicit_dom_css_height(lycon, nested) > 0.0f) {
                summary.has_child_with_explicit_height = true;
            }
        }
    }
    return summary;
}

static FlexDirectContentSummary flex_measure_direct_content_summary(DomElement* elem) {
    FlexDirectContentSummary summary = {false, false, false, 0};
    for (DomNode* content = elem ? elem->first_child : nullptr;
         content; content = content->next_sibling) {
        if (content->is_element()) {
            summary.element_count++;
            if (flex_measurement_tag_is_inline(content->tag())) {
                summary.has_inline_element = true;
            } else {
                summary.has_block_element = true;
            }
        } else if (layout_text_node_has_content(content)) {
            summary.has_text_content = true;
        }
    }
    return summary;
}

static FlexNestedHeightResult flex_measure_nested_flex_height(LayoutContext* lycon,
                                                              DomElement* elem,
                                                              float text_line_height) {
    FlexNestedContentSummary content_summary =
        flex_measure_nested_content_summary(lycon, elem);
    if (!content_summary.has_text_content &&
        !content_summary.has_element_content) {
        log_debug("Nested flex container: empty, height=0");
        return {0.0f, true};
    }

    float recursive_height = measure_content_height_recursive(
        static_cast<DomNode*>(elem), lycon, 0);
    if (content_summary.has_child_with_explicit_height) {
        if (recursive_height > 0.0f) {
            log_debug("Nested flex container: measured content height=%.1f", recursive_height);
            return {recursive_height, true};
        }
        log_debug("Nested flex container: measurement returned 0, using 0");
        return {0.0f, false};
    }

    if (recursive_height > 0.0f) {
        log_debug("Nested flex container with content: recursive measured height=%.1f",
                  recursive_height);
        return {recursive_height, true};
    }
    if (content_summary.has_text_content) {
        // Text-only nested flex fallback is not a final explicit height; later
        // flex layout may still resolve a more exact cross size.
        log_debug("Nested flex container with text content: using text_line_height=%.1f",
                  text_line_height);
        return {text_line_height, false};
    }

    log_debug("Nested flex container: element children but recursive=0, using 0");
    return {0.0f, true};
}

static FlexChildExplicitSizes flex_measure_child_explicit_sizes(LayoutContext* lycon, ViewElement* child_view) {
    FlexChildExplicitSizes sizes = {
        child_view->blk && child_view->blk->given_width >= 0.0f,
        child_view->blk && child_view->blk->given_height >= 0.0f,
        child_view->blk ? child_view->blk->given_width : -1.0f,
        child_view->blk ? child_view->blk->given_height : -1.0f
    };

    if (!sizes.has_width && lycon) {
        float dom_css_width = get_explicit_css_width(lycon, child_view);
        if (dom_css_width > 0.0f) {
            sizes.has_width = true;
            sizes.width = dom_css_width;
            log_debug("Got explicit CSS width from DOM: %.1f", dom_css_width);
        }
    }

    if (!sizes.has_height && lycon) {
        float dom_css_height = get_explicit_css_height(lycon, child_view);
        if (dom_css_height > 0.0f) {
            sizes.has_height = true;
            sizes.height = dom_css_height;
            log_debug("Got explicit CSS height from DOM: %.1f", dom_css_height);
        }
    }

    return sizes;
}

static float flex_measure_length_decl(LayoutContext* lycon, DomElement* elem,
                                      CssPropertyId property,
                                      CssPropertyId resolve_property) {
    if (!elem || !elem->specified_style) return 0.0f;

    CssDeclaration* decl = style_tree_get_declaration(elem->specified_style, property);
    if (!decl || !decl->value || decl->value->type != CSS_VALUE_TYPE_LENGTH) return 0.0f;
    return resolve_length_value(lycon, resolve_property, decl->value);
}

static float flex_measure_length_decl_pair(LayoutContext* lycon, DomElement* elem,
                                           CssPropertyId first_property,
                                           CssPropertyId second_property) {
    return flex_measure_length_decl(lycon, elem, first_property, first_property) +
           flex_measure_length_decl(lycon, elem, second_property, second_property);
}

static float flex_measure_shorthand_side_pair(LayoutContext* lycon, CssValue* value,
                                              CssPropertyId resolve_property,
                                              int first_side, int second_side) {
    if (!value) return 0.0f;

    float first = 0.0f;
    float second = 0.0f;
    const CssValue* first_value = css_box_shorthand_side_value(value, first_side);
    const CssValue* second_value = css_box_shorthand_side_value(value, second_side);
    if (first_value) {
        first = resolve_length_value(lycon, resolve_property, first_value);
    }
    if (second_value) {
        second = resolve_length_value(lycon, resolve_property, second_value);
    }
    return first + second;
}

static float flex_measure_vertical_padding_sum(LayoutContext* lycon, DomElement* elem) {
    if (!elem || !elem->specified_style) return 0.0f;

    float padding = flex_measure_length_decl_pair(
        lycon, elem, CSS_PROPERTY_PADDING_TOP, CSS_PROPERTY_PADDING_BOTTOM);
    if (padding != 0.0f) return padding;

    CssDeclaration* pad_sh = style_tree_get_declaration(elem->specified_style, CSS_PROPERTY_PADDING);
    return flex_measure_shorthand_side_pair(lycon, pad_sh ? pad_sh->value : nullptr,
                                            CSS_PROPERTY_PADDING, 0, 2);
}

static float flex_measure_border_all_width(LayoutContext* lycon, CssValue* value) {
    if (!value) return 0.0f;

    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int bi = 0; bi < value->data.list.count; bi++) {
            CssValue* bv = value->data.list.values[bi];
            if (bv->type == CSS_VALUE_TYPE_LENGTH || bv->type == CSS_VALUE_TYPE_NUMBER) {
                return resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, bv);
            }
        }
        return 0.0f;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        return resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, value);
    }
    return 0.0f;
}

static float flex_measure_vertical_border_sum(LayoutContext* lycon, DomElement* elem) {
    if (!elem || !elem->specified_style) return 0.0f;

    float border = flex_measure_length_decl_pair(
        lycon, elem, CSS_PROPERTY_BORDER_TOP_WIDTH, CSS_PROPERTY_BORDER_BOTTOM_WIDTH);
    if (border != 0.0f) return border;

    CssDeclaration* bw_sh = style_tree_get_declaration(elem->specified_style, CSS_PROPERTY_BORDER_WIDTH);
    border = flex_measure_shorthand_side_pair(lycon, bw_sh ? bw_sh->value : nullptr,
                                              CSS_PROPERTY_BORDER_WIDTH, 0, 2);
    if (border != 0.0f) return border;

    CssDeclaration* b_sh = style_tree_get_declaration(elem->specified_style, CSS_PROPERTY_BORDER);
    float border_width = flex_measure_border_all_width(lycon, b_sh ? b_sh->value : nullptr);
    return border_width * 2.0f;
}

static float flex_measure_horizontal_padding_sum(LayoutContext* lycon, ViewElement* elem) {
    if (!elem || !elem->specified_style) return 0.0f;

    CssDeclaration* pad_decl = style_tree_get_declaration(
        elem->specified_style, CSS_PROPERTY_PADDING);
    if (pad_decl && pad_decl->value && pad_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
        float padding = resolve_length_value(lycon, CSS_PROPERTY_PADDING, pad_decl->value);
        return padding * 2.0f;
    }

    return flex_measure_length_decl_pair(
        lycon, elem, CSS_PROPERTY_PADDING_LEFT, CSS_PROPERTY_PADDING_RIGHT);
}

static float flex_measure_horizontal_border_sum(LayoutContext* lycon, ViewElement* elem) {
    if (!elem || !elem->specified_style) return 0.0f;

    CssDeclaration* bw_decl = style_tree_get_declaration(
        elem->specified_style, CSS_PROPERTY_BORDER_WIDTH);
    if (bw_decl && bw_decl->value && bw_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
        float border = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, bw_decl->value);
        return border * 2.0f;
    }

    CssDeclaration* b_sh = style_tree_get_declaration(
        elem->specified_style, CSS_PROPERTY_BORDER);
    float border_width = flex_measure_border_all_width(lycon, b_sh ? b_sh->value : nullptr);
    return border_width * 2.0f;
}

static float flex_measure_horizontal_box_extra(LayoutContext* lycon, ViewElement* elem) {
    return flex_measure_horizontal_padding_sum(lycon, elem) +
           flex_measure_horizontal_border_sum(lycon, elem);
}

static float flex_measure_text_child_vertical_extra(LayoutContext* lycon,
                                                    DomElement* elem,
                                                    ViewElement* sub_view) {
    if (sub_view && sub_view->bound) {
        float vert_extra = layout_boundary_metrics(sub_view->bound).pad_border_v;
        if (vert_extra != 0.0f || !elem || !elem->specified_style) {
            return vert_extra;
        }
    }
    if (!elem || !elem->specified_style) return 0.0f;
    return flex_measure_vertical_padding_sum(lycon, elem) +
           flex_measure_vertical_border_sum(lycon, elem);
}

static float flex_measure_select_max_option_text_width(LayoutContext* lycon,
                                                       ViewElement* elem) {
    float max_text_width = 0.0f;
    for (DomNode* child = elem ? elem->first_child : nullptr; child; child = child->next_sibling) {
        DomElement* group = child->as_element();
        if (group && group->tag() == HTM_TAG_OPTGROUP) {
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
        if (parent && parent->tag() == HTM_TAG_OPTGROUP) {
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
    if (item->blk && item->blk->given_width >= 0.0f) {
        parent_cw = layout_css_size_to_content_box(
            item->bound, layout_box_sizing(lam::view_as_block(item)), item->blk->given_width, true);
    } else if (flex_layout && flex_layout->cross_axis_size > 0.0f) {
        if (resolve_percent_width && item->specified_style) {
            // Nested flex fallback historically re-resolves percentage width here; keep
            // non-percentage specified widths on the legacy unconstrained path.
            CssDeclaration* w_decl = style_tree_get_declaration(
                item->specified_style, CSS_PROPERTY_WIDTH);
            bool resolved_percent_width = w_decl && w_decl->value &&
                layout_resolve_percentage_value(w_decl->value, flex_layout->cross_axis_size, &parent_cw);
            if (!resolved_percent_width && (!w_decl || !w_decl->value ||
                (w_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                 w_decl->value->data.keyword == CSS_VALUE_AUTO))) {
                parent_cw = flex_layout->cross_axis_size;
                if (item->bound) {
                    parent_cw -= item->bound->margin.left + item->bound->margin.right;
                }
            }
            if (parent_cw > 0.0f && item->blk) {
                parent_cw = layout_css_size_to_content_box(
                    item->bound, layout_box_sizing(lam::view_as_block(item)), parent_cw, true);
            }
        } else if (!resolve_percent_width) {
            parent_cw = flex_layout->cross_axis_size;
            if (item->bound) {
                parent_cw -= item->bound->margin.left + item->bound->margin.right;
            }
            if (item->blk) {
                parent_cw = layout_css_size_to_content_box(
                    item->bound, layout_box_sizing(lam::view_as_block(item)), parent_cw, true);
            }
        }
    }

    if (parent_cw > 0.0f) {
        float child_margins = get_child_horizontal_margins(lycon, child_view, parent_cw);
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
    log_debug("Child height is 0, checking display - display.inner=%d, display.outer=%d",
              child_display.inner, child_display.outer);
    if (child_display.inner == CSS_VALUE_FLEX) {
        // For flex containers, use recursive DOM-based measurement
        float child_height = measure_content_height_recursive(child, lycon, 0);
        log_debug("Nested flex child height from recursive measurement: %.1f", child_height);
        // Fallback: if recursive returned 0 (e.g., text-only flex container),
        // use calculate_max_content_height with parent's cross-axis size
        if (child_height == 0.0f && lycon) {
            float avail_w = flex_child_height_fallback_available_width(
                lycon, item, child_view, flex_layout, is_row_flex_container, true);
            child_height = flex_measure_intrinsic_max_height(lycon, child, avail_w);
            log_debug("Nested flex child height fallback: avail_w=%.1f, child_height=%.1f, child_bound=%p",
                      avail_w, child_height, (void*)child_view->bound);
        }
        return child_height;
    }
    if (child_display.outer == CSS_VALUE_BLOCK && lycon) {
        // For regular block elements, measure content height.
        // In column flex, use the item's resolved cross-axis width
        // so text wraps correctly during measurement.
        float available_width = flex_child_height_fallback_available_width(
            lycon, item, child_view, flex_layout, is_row_flex_container, false);
        float child_height = flex_measure_intrinsic_max_height(lycon, child, available_width);
        log_debug("Block child height from calculate_max_content_height: avail_w=%.1f, h=%.1f",
                  available_width, child_height);
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
        row_width -= item->bound->margin.left + item->bound->margin.right;
    }
    if (layout_uses_border_box(lam::view_as_block(item))) {
        if (item->blk->given_width >= 0.0f) {
            row_width = item->blk->given_width;
        }
        row_width = layout_clamp_positive_min_max_width(
            lam::view_as_block(item), row_width);
    }
    row_width = layout_boundary_content_size_from_border_box(item->bound, row_width, true);
    if (item->blk && !layout_uses_border_box(lam::view_as_block(item))) {
        if (item->blk->given_width >= 0.0f) {
            row_width = item->blk->given_width;
        }
        row_width = layout_clamp_positive_min_max_width(
            lam::view_as_block(item), row_width);
    }
    if (row_width <= 0.0f) row_width = flex_layout->cross_axis_size;

    float gap = flex_measurement_embedded_column_gap(item);
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
        child_content_w -= flex_measure_horizontal_box_extra(lycon, child_view);
    }
    if (child_content_w < 0.0f) child_content_w = 0.0f;
    float child_height = flex_measure_intrinsic_max_height(lycon, child, child_content_w);
    log_debug("Row flex child height at estimated share: share=%.1f, content_w=%.1f, h=%.1f",
              child_share, child_content_w, child_height);
    return child_height;
}

static FlexMeasuredElementHeight flex_measure_direct_element_height(LayoutContext* lycon,
                                                                    DomNode* sub_child,
                                                                    DomElement* elem,
                                                                    float text_line_height) {
    FlexMeasuredElementHeight result = {0.0f, false};
    if (!sub_child) return result;

    uintptr_t tag = sub_child->tag();
    if (tag == HTM_TAG_H1) result.height = 32.0f;
    else if (tag == HTM_TAG_H2) result.height = 28.0f;
    else if (tag == HTM_TAG_H3) result.height = 24.0f;
    else if (tag == HTM_TAG_H4) result.height = 20.0f;
    else if (tag == HTM_TAG_H5 || tag == HTM_TAG_H6) result.height = 18.0f;
    else if (tag == HTM_TAG_P) result.height = 36.0f;  // typically 2-3 lines
    else if (tag == HTM_TAG_SVG) {
        const char* attr_h = elem ? elem->get_attribute("height") : nullptr;
        if (attr_h) {
            float attr_height = (float)atoi(attr_h);
            if (attr_height > 0.0f) {
                result.height = attr_height;
                result.has_explicit_height_css = true;
                log_debug("SVG height from attribute: %.1f", result.height);
            }
        }
        if (!result.has_explicit_height_css) {
            float css_height = get_explicit_dom_css_height(lycon, elem);
            if (css_height > 0.0f) {
                result.height = css_height;
                result.has_explicit_height_css = true;
                log_debug("SVG height from CSS: %.1f", result.height);
            }
        }
        if (!result.has_explicit_height_css) {
            result.height = 150.0f;  // HTML default SVG height
        }
    }
    else if (tag == HTM_TAG_IFRAME || tag == HTM_TAG_IMG ||
             tag == HTM_TAG_VIDEO || tag == HTM_TAG_CANVAS) {
        float css_height = get_explicit_dom_css_height(lycon, elem);
        if (css_height > 0.0f) {
            result.height = css_height;
            result.has_explicit_height_css = true;
            log_debug("Replaced element %s has explicit CSS height=%.1f",
                      sub_child->node_name(), result.height);
        }
        if (!result.has_explicit_height_css) {
            if (tag == HTM_TAG_IFRAME) result.height = 150.0f;  // CSS default iframe height
            else if (tag == HTM_TAG_VIDEO) result.height = 150.0f;
            else result.height = 0.0f;  // other replaced elements need explicit size
        }
    }
    else if (tag == HTM_TAG_UL || tag == HTM_TAG_OL) {
        FlexDirectContentSummary list_summary = flex_measure_direct_content_summary(elem);
        bool is_list_flex_row = false;
        if (elem) {
            ViewElement* list_view = lam::view_require_element(elem);
            if (flex_measurement_is_flex_container(list_view)) {
                is_list_flex_row = flex_measurement_direction_is_row(list_view);
            }
        }
        if (is_list_flex_row) {
            result.height = list_summary.element_count > 0 ? 18.0f : 0.0f;  // row: max of children
        } else {
            result.height = list_summary.element_count * 18.0f;  // column/block: sum of children
        }
    }
    else if (tag == HTM_TAG_DIV || tag == HTM_TAG_SECTION ||
             tag == HTM_TAG_ARTICLE || tag == HTM_TAG_NAV ||
             tag == HTM_TAG_HEADER || tag == HTM_TAG_FOOTER ||
             tag == HTM_TAG_ASIDE || tag == HTM_TAG_MAIN) {
        ViewElement* nested_view = lam::view_require_element(elem);
        bool is_nested_flex = flex_measurement_is_flex_container(nested_view);
        if (is_nested_flex && nested_view &&
            nested_view->display.inner != CSS_VALUE_FLEX) {
            log_debug("Nested flex container detected before resolved display");
        }

        if (is_nested_flex) {
            FlexNestedHeightResult nested_height =
                flex_measure_nested_flex_height(lycon, elem, text_line_height);
            result.height = nested_height.height;
            result.has_explicit_height_css = nested_height.has_explicit_height_css;
        } else {
            FlexDirectContentSummary content_summary =
                flex_measure_direct_content_summary(elem);
            if (content_summary.has_block_element) {
                result.height = 56.0f;
                log_debug("Non-flex div with block elements: using estimate height=56");
            } else if (content_summary.has_inline_element ||
                       content_summary.has_text_content) {
                result.height = text_line_height;
                log_debug("Non-flex div with inline/text content: using text_line_height=%.1f",
                          result.height);
            } else {
                result.height = 0.0f;
            }
        }
    }
    else {
        FlexDirectContentSummary content_summary = flex_measure_direct_content_summary(elem);
        if (content_summary.has_text_content) {
            float child_content_height = text_line_height;
            ViewElement* sub_view = lam::view_require_element(elem);
            float inherited_lh = resolve_flex_inherited_line_height(lycon, elem);
            if (inherited_lh > 0.0f) {
                child_content_height = inherited_lh;
            }
            result.height = child_content_height;

            float vert_extra = flex_measure_text_child_vertical_extra(lycon, elem, sub_view);
            result.height += vert_extra;
            if (vert_extra > 0.0f) result.has_explicit_height_css = true;
            log_debug("Element %s: content_height=%.1f, padding+border=%.1f, total=%.1f",
                      sub_child->node_name(), child_content_height, vert_extra, result.height);
        } else {
            ViewElement* sub_view = lam::view_as_element(sub_child);
            if (sub_view && sub_view->item_prop_type == DomElement::ITEM_PROP_FORM &&
                sub_view->form && sub_view->form->intrinsic_height > 0.0f) {
                result.height = sub_view->form->intrinsic_height;
                result.has_explicit_height_css = true;
                log_debug("Element %s using form intrinsic height=%.1f",
                          sub_child->node_name(), result.height);
            }

            if (!result.has_explicit_height_css) {
                log_debug("Checking explicit CSS height for %s, elem=%p, specified_style=%p",
                          sub_child->node_name(), elem, elem ? elem->specified_style : nullptr);
            }
            if (!result.has_explicit_height_css) {
                float css_height = get_explicit_dom_css_height(lycon, elem);
                if (css_height > 0.0f) {
                    result.height = css_height;
                    result.has_explicit_height_css = true;
                    log_debug("Element %s has explicit CSS height=%.1f",
                              sub_child->node_name(), result.height);
                }
            }
            if (!result.has_explicit_height_css) {
                result.height = 20.0f;  // default element height
                log_debug("Element %s using default height=20", sub_child->node_name());
            }
        }
    }
    return result;
}

static void flex_accumulate_direct_element_height(uintptr_t tag,
                                                  FlexMeasuredElementHeight measured,
                                                  float text_line_height,
                                                  bool is_row_flex,
                                                  float* max_child_height,
                                                  float* measured_height) {
    if (measured.height <= 0.0f) return;

    bool is_inline_child = flex_measurement_tag_is_inline(tag) && tag != HTM_TAG_BR;
    bool use_max_height = is_row_flex || is_inline_child;
    float margin =
        (use_max_height && (measured.height == text_line_height ||
                            measured.has_explicit_height_css || is_inline_child)) ||
        (!use_max_height && measured.has_explicit_height_css)
            ? 0.0f : 10.0f;
    flex_accumulate_child_cross_height(use_max_height, measured.height + margin,
                                       max_child_height, measured_height);
}

static IntrinsicSizes flex_measure_child_intrinsic_widths(LayoutContext* lycon,
                                                          ViewElement* child_view,
                                                          bool content_only) {
    FontBox saved_child_font = lycon->font;
    bool child_font_changed = false;
    if (child_view->font) {
        // Intrinsic text width must be measured in the child font; parent font state
        // is restored immediately so sibling measurement keeps its inherited context.
        setup_font(lycon->ui_context, &lycon->font, child_view->font);
        child_font_changed = true;
    }

    IntrinsicSizes child_sizes = measure_element_intrinsic_widths(
        lycon, lam::dom_require<DOM_NODE_ELEMENT>(child_view), content_only);

    if (child_font_changed) {
        lycon->font = saved_child_font;
    }
    return child_sizes;
}

static bool flex_measure_recursive_existing_height(LayoutContext* lycon,
                                                   ViewElement* elem,
                                                   float* measured_height) {
    if (!elem || !measured_height) return false;

    log_debug("measure_content_height_recursive: checking elem %s, blk=%p height=%.1f",
              elem->tag_name ? elem->tag_name : "(null)",
              (void*)elem->blk, elem->height);
    float existing_height = flex_measure_existing_view_height(elem);
    if (existing_height >= 0.0f) {
        log_debug("measure_content_height_recursive: elem %s has existing height=%.1f",
                  elem->tag_name ? elem->tag_name : "(null)", existing_height);
        *measured_height = existing_height;
        return true;
    }

    float explicit_height = get_explicit_css_height(lycon, elem);
    if (explicit_height > 0.0f) {
        log_debug("measure_content_height_recursive: elem %s has specified height=%.1fpx",
                  elem->tag_name ? elem->tag_name : "(null)", explicit_height);
        *measured_height = explicit_height;
        return true;
    }
    return false;
}

static float flex_measure_recursive_children_height(DomElement* dom_elem,
                                                    LayoutContext* lycon,
                                                    int depth,
                                                    bool is_row) {
    float max_child_height = 0.0f;
    float sum_child_height = 0.0f;
    for (DomNode* child = dom_elem ? dom_elem->first_child : nullptr;
         child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        float child_height = flex_measure_recursive_child_height(child, lycon, depth);
        flex_accumulate_child_cross_height(is_row, child_height,
                                           &max_child_height, &sum_child_height);
    }
    return is_row ? max_child_height : sum_child_height;
}

static float measure_content_height_recursive(DomNode* node, LayoutContext* lycon, int depth) {
    if (!node || !node->is_element()) return 0;
    if (depth > MAX_MEASURE_DEPTH) return 0;

    // Check if the View is already set with an explicit height
    ViewElement* elem = lam::view_require_element(node);
    float measured_height = 0.0f;
    if (flex_measure_recursive_existing_height(lycon, elem, &measured_height)) {
        return measured_height;
    }

    // Check if this is a flex container
    DisplayValue display = resolve_display_value((void*)node);
    if (display.inner != CSS_VALUE_FLEX) {
        // Not a flex container - no recursive measurement needed
        return 0;
    }

    // Traverse children to calculate content-based height
    // Determine flex direction from CSS properties or resolved ViewBlock
    bool is_row = true;  // CSS default is row
    if (elem) {
        is_row = flex_measurement_direction_is_row(elem);
    }

    // Only elements can have children - get as_element() to access first_child
    DomElement* dom_elem = node->as_element();
    if (!dom_elem) return 0;

    float result = flex_measure_recursive_children_height(dom_elem, lycon, depth, is_row);
    log_debug("measure_content_height_recursive: node %s = %.1f (is_row=%d)",
              node->node_name(), result, is_row);
    return result;
}

static void flex_measure_direct_child_heights(LayoutContext* lycon, DomElement* child_elem,
                                              float text_line_height, bool is_row_flex,
                                              float* max_child_height, float* measured_height) {
    for (DomNode* sub_child = child_elem ? child_elem->first_child : nullptr;
         sub_child; sub_child = sub_child->next_sibling) {
        if (layout_text_node_has_content(sub_child)) {
            flex_accumulate_child_cross_height(true, text_line_height,
                                               max_child_height, measured_height);
        } else if (sub_child->is_element()) {
            DomElement* elem = sub_child->as_element();
            uintptr_t tag = sub_child->tag();
            FlexMeasuredElementHeight measured_elem =
                flex_measure_direct_element_height(lycon, sub_child, elem, text_line_height);
            flex_accumulate_direct_element_height(tag, measured_elem, text_line_height,
                                                  is_row_flex, max_child_height,
                                                  measured_height);
        }
    }
}

// Content measurement for multi-pass flex layout
// This file implements the first pass of the multi-pass flex layout algorithm

// Global measurement cache
static MeasurementCacheEntry* measurement_cache = nullptr;
static int cache_count = 0;
static int cache_capacity = 0;
static uint32_t cache_generation = 0;  // incremented on each top-level layout

static bool ensure_measurement_cache_capacity(int required) {
    if (required <= cache_capacity) return true;

    int new_capacity = cache_capacity > 0 ? cache_capacity : 1024;
    while (new_capacity < required) {
        new_capacity *= 2;
    }
    MeasurementCacheEntry* grown = (MeasurementCacheEntry*)mem_realloc(
        measurement_cache, (size_t)new_capacity * sizeof(MeasurementCacheEntry), MEM_CAT_CACHE_LAYOUT);
    if (!grown) {
        log_error("RAD_CAP_FLEX_MEASURE_CACHE: unable to grow measurement cache from %d to %d entries",
                  cache_capacity, new_capacity);
        return false;
    }
    // New slots are zeroed so a failed/incomplete entry cannot look like a stale node hit.
    if (new_capacity > cache_capacity) {
        memset(grown + cache_capacity, 0,
               (size_t)(new_capacity - cache_capacity) * sizeof(MeasurementCacheEntry));
    }
    if (cache_capacity > 0) {
        log_warn("[RAD_CAP_FLEX_MEASURE_CACHE] grew measurement cache from %d to %d entries",
                 cache_capacity, new_capacity);
    }
    measurement_cache = grown;
    cache_capacity = new_capacity;
    return true;
}

void advance_measurement_cache_generation() {
    cache_generation++;
    log_debug("Measurement cache generation advanced to %u", cache_generation);
}

uint32_t get_measurement_cache_generation() {
    return cache_generation;
}

void store_in_measurement_cache(DomNode* node, float width, float height,
                               float content_width, float content_height,
                               float context_width) {
    if (!ensure_measurement_cache_capacity(cache_count + 1)) {
        return;
    }

    measurement_cache[cache_count].node = node;
    measurement_cache[cache_count].measured_width = width;
    measurement_cache[cache_count].measured_height = height;
    measurement_cache[cache_count].content_width = content_width;
    measurement_cache[cache_count].content_height = content_height;
    measurement_cache[cache_count].context_width = context_width;
    measurement_cache[cache_count].generation = cache_generation;
    cache_count++;

    log_debug("Cached measurement for node %p: %.1fx%.1f (content: %.1fx%.1f) gen=%u",
              node, width, height, content_width, content_height, cache_generation);
}

MeasurementCacheEntry* get_from_measurement_cache(DomNode* node) {
    for (int i = 0; i < cache_count; i++) {
        if (measurement_cache[i].node == node) {
            // skip stale entries from a previous layout generation
            if (measurement_cache[i].generation != cache_generation) {
                log_debug("Skipping stale measurement cache entry for node %p (gen %u, current %u)",
                          node, measurement_cache[i].generation, cache_generation);
                return nullptr;
            }
            return &measurement_cache[i];
        }
    }
    return nullptr;
}

void clear_measurement_cache() {
    cache_count = 0;
    log_debug("Cleared measurement cache");
}

void invalidate_measurement_cache_for_node(DomNode* node) {
    for (int i = 0; i < cache_count; i++) {
        if (measurement_cache[i].node == node) {
            // Swap with last entry and decrement count to remove this entry
            measurement_cache[i] = measurement_cache[--cache_count];
            log_debug("Invalidated measurement cache for node %p (%s)", node, node ? node->node_name() : "null");
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
        int mw_int = 0, mh_int = 0;
        measure_text_content(&measure_context, child, &mw_int, &mh_int);
        measured_width = mw_int;
        measured_height = mh_int;
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
            log_debug("measure_flex_child_content: elem_view=%p, view_type=%d, display.inner=%d (CSS_VALUE_FLEX=%d)",
                      elem_view, elem_view->view_type, elem_view->display.inner, CSS_VALUE_FLEX);
            // Check display property directly on the DOM element
            if (elem_view->display.inner == CSS_VALUE_FLEX) {
                // It's a flex container - check direction
                is_row_flex = flex_measurement_direction_is_row(elem_view);
                log_debug("Element %s is%s a row flex container",
                          child->node_name(), is_row_flex ? "" : " NOT");
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
        if (view_elem && view_elem->font && view_elem->font->font_size > 0) {
            elem_font_size = view_elem->font->font_size;
        }

        // Calculate actual line height using the font's metrics (Chrome-compatible)
        // This requires setting up the font first to get accurate font metrics
        float text_line_height = elem_font_size;  // fallback
        if (view_elem) {
            text_line_height =
                flex_measure_normal_line_height_for_font(lycon, view_elem->font, text_line_height);
        }
        log_debug("measure_flex_child_content: elem_font_size=%.1f, text_line_height=%.1f",
                  elem_font_size, text_line_height);

        flex_measure_direct_child_heights(lycon, child_elem, text_line_height, is_row_flex, &max_child_height, &measured_height);

        // For row flex containers OR blocks with only inline children, use max_child_height
        // This is because inline children flow horizontally and should not stack heights
        if (max_child_height > 0 && (is_row_flex || measured_height == 0)) {
            measured_height = max_child_height;
            log_debug("Using max child height %.1f (is_row_flex=%d)", measured_height, is_row_flex);
        }

        // Set measured dimensions
        // CRITICAL FIX: For elements without explicit width, measured_width should be based
        // on content, not container. Only use container_width if the element has explicit width.
        ViewElement* elem = lam::view_require_element(child);
        bool has_explicit_width = (elem && elem->blk && elem->blk->given_width >= 0);

        if (has_explicit_width) {
            measured_width = elem->blk->given_width;
            log_debug("Measured element %s: using explicit width %.1f", child->node_name(), measured_width);
        } else {
            // For elements without explicit width, use 0 as intrinsic width
            // The actual width will be determined by flex layout (stretch, etc.)
            measured_width = 0;
            log_debug("Measured element %s: no explicit width, using 0", child->node_name());
        }
        content_width = measured_width;
        content_height = measured_height;

        // Special handling for form controls - use intrinsic size as content
        if (elem && elem->item_prop_type == DomElement::ITEM_PROP_FORM && elem->form) {
            ViewBlock* elem_block = lam::view_as_block(elem);
            IntrinsicSize form_size = layout_measure_form_control(lycon, elem_block,
                                                                  lycon->available_space);
            content_height = form_size.max_height;
            content_width = form_size.max_width;

            // For text-like inputs, recalculate content height using actual font
            // (CSS may override UA font-size, so intrinsic_height from UA phase may be stale)
            if (elem->form->control_type == FORM_CONTROL_TEXT &&
                elem->font && elem->font->font_size > 0 && lycon->ui_context) {
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
                FontBox saved_font = lycon->font;
                if (elem->font && elem->font->font_size > 0 && lycon->ui_context) {
                    setup_font(lycon->ui_context, &lycon->font, elem->font);
                }
                float max_text_width = flex_measure_select_max_option_text_width(lycon, elem);
                lycon->font = saved_font;
                float overhead = FormDefaults::SELECT_ARROW_WIDTH + 1.0f;
                float min_select_width = FormDefaults::SELECT_HEIGHT + 3.0f;
                float calculated = max_text_width + overhead;
                float new_w = calculated > min_select_width ? calculated : min_select_width;
                if (new_w > content_width) {
                    content_width = new_w;
                    elem->form->intrinsic_width = new_w;
                    log_debug("SELECT measured intrinsic width: %.1f (max_text=%.1f)",
                              new_w, max_text_width);
                }
            }
            measured_height = content_height;
            measured_width = content_width;

            // Special handling for buttons with child content (e.g., <button>Subscribe</button>)
            // The intrinsic_width may not be set because buttons go through normal layout flow
            if (elem->form->control_type == FORM_CONTROL_BUTTON &&
                elem->form->intrinsic_width <= 0 && elem->first_child) {
                // Get text-transform from parent element chain
                CssEnum btn_text_transform = flex_measure_resolve_text_transform(elem);

                // Measure text content of button
                // Set up button's own font for measurement (UA default 13.3333px Arial,
                // not parent's inherited font which may differ)
                FontBox saved_font = lycon->font;
                if (elem->font && elem->font->font_size > 0 && lycon->ui_context) {
                    setup_font(lycon->ui_context, &lycon->font, elem->font);
                }
                float max_text_width = measure_direct_text_children_intrinsic_width(
                    lycon, elem, false, btn_text_transform);
                lycon->font = saved_font;  // restore parent font
                if (max_text_width > 0) {
                    // Store intrinsic size in form property for flex-basis calculation
                    elem->form->intrinsic_width = max_text_width;

                    // Content-area height: use line-height from font metrics (buttons use normal
                    // block layout, so their content height is determined by the text line-height)
                    float btn_content_height = FormDefaults::TEXT_HEIGHT
                        - 2 * (FormDefaults::BUTTON_PADDING_V + FormDefaults::BUTTON_BORDER);  // fallback: 15
                    if (elem->font && elem->font->font_size > 0 && lycon->ui_context) {
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

                    log_debug("Button %s: measured text content width=%.1f, intrinsic=%dx%d",
                              child->node_name(), max_text_width,
                              (int)elem->form->intrinsic_width, (int)elem->form->intrinsic_height); // INT_CAST_OK: form intrinsic size for log
                }
            }

            log_debug("Form control %s: using intrinsic size %.1fx%.1f",
                      child->node_name(), measured_width, measured_height);
        }

        // Add padding and border to measured height for total height
        // CSS box model: total_height = content_height + padding + border
        if (elem && elem->bound) {
            measured_height += layout_boundary_metrics(elem->bound).pad_border_v;
            log_debug("Added box model to height: content=%.1f, total=%.1f (padding+border)",
                      content_height, measured_height);
        }

        log_debug("Measured element %s: %.1fx%.1f (content-based estimation)",
                  child->node_name(), measured_width, measured_height);
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

    log_debug("Content measurement complete for %s", child->node_name());
}

void measure_text_content(LayoutContext* lycon, DomNode* text_node, int* width, int* height) {
    // Legacy function - redirects to accurate measurement
    int min_w, max_w, h;
    measure_text_content_accurate(lycon, text_node, &min_w, &max_w, &h);
    *width = max_w;  // Use max-content for width
    *height = h;
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

    // Measure using actual font metrics
    measure_text_run(lycon, text_data, text_length, min_width, max_width, height);

    log_debug("Measured text accurately: min=%d, max=%d, height=%d (\"%.*s\")",
              *min_width, *max_width, *height, (int)min(text_length, 20), text_data); // INT_CAST_OK: text length for log
}

// Measure a text run with actual font metrics
// REFACTORED: Now uses unified intrinsic_sizing.hpp for text measurement
void measure_text_run(LayoutContext* lycon, const char* text, size_t length,
                     int* min_width, int* max_width, int* height) {
    if (!text || length == 0) {
        *min_width = *max_width = *height = 0;
        return;
    }

    // Use unified intrinsic sizing API
    TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, length);

    *max_width = widths.max_content;
    *min_width = widths.min_content;
    *height = (lycon->font.style && lycon->font.style->font_size > 0) ?
              (int)(lycon->font.style->font_size + 0.5f) : 20; // INT_CAST_OK: font size for char width calc

    log_debug("measure_text_run (unified): text_length=%zu, min=%d, max=%d, height=%d",
              length, *min_width, *max_width, *height);
}

int estimate_text_width(LayoutContext* lycon, const unsigned char* text, size_t length) {
    // Use unified intrinsic sizing API for accurate text width
    if (lycon && text && length > 0) {
        TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, (const char*)text, length);
        return widths.max_content;
    }
    // Fallback: rough estimate when no context available
    float avg_char_width = (lycon && lycon->font.style) ? lycon->font.style->font_size * 0.6f : 10.0f;
    return (int)(length * avg_char_width); // INT_CAST_OK: estimated pixel width
}

void cleanup_temporary_view(ViewBlock* temp_view) {
    // Cleanup temporary view and its resources
    if (temp_view) {
        // Free any allocated resources
        // Note: In practice, this might be handled by the memory pool
        log_debug("Cleaned up temporary measurement view");
    }
}

bool requires_content_measurement(ViewBlock* flex_container) {
    // Determine if content measurement is needed
    // This could be based on flex properties, content types, etc.

    if (!flex_container) return false;

    // Check if any children have auto flex-basis or need intrinsic sizing
    DomNode* child = nullptr;
    if (flex_container->is_element()) {
        child = lam::dom_require<DOM_NODE_ELEMENT>(flex_container)->first_child;
    }
    while (child) {
        // If child has complex content or auto sizing, measurement is needed
        DomNode* child_first = nullptr;
        if (child->is_element()) {
            child_first = lam::dom_require_element(child)->first_child;
        }
        if (child_first || child->is_text()) {
            return true;
        }
        child = child->next_sibling;
    }

    return false;
}

void measure_all_flex_children_content(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container) return;

    log_debug("Measuring all flex children content");
    DomNode* child = flex_container->first_child;
    int child_count = 0;  const int MAX_CHILDREN = 100; // Safety limit
    while (child && child_count < MAX_CHILDREN) {
        measure_flex_child_content(lycon, child);
        child = child->next_sibling;
        child_count++;
    }
    log_debug("Content measurement complete for %d children", child_count);
}

// Lightweight View creation for flex items with measured sizes
void layout_flow_node_for_flex(LayoutContext* lycon, DomNode* node) {
    if (!node) return;
    // Skip text nodes - flex layout only processes element nodes
    if (!node->is_element()) {
        return;
    }

    // Create lightweight View for flex item element only (no child processing)
    init_flex_item_view(lycon, node);

    // Apply measured sizes if available
    MeasurementCacheEntry* cached = get_from_measurement_cache(node);

    if (cached && node->view_type == RDT_VIEW_BLOCK) {
        ViewBlock* view = lam::view_require<RDT_VIEW_BLOCK>(node);
        if (view == node) {
            log_debug("Applying cached measurements to flex item: %.1fx%.1f",
                cached->measured_width, cached->measured_height);

            // For grid containers, skip the cached height — measure_flex_child_content
            // stacks children as blocks (wrong for grid); grid height = max_row_height.
            bool node_is_grid = (view->display.inner == CSS_VALUE_GRID) ||
                flex_measurement_style_declares_display(
                    view, CSS_VALUE_GRID, CSS_VALUE_INLINE_GRID);

            // Use measured dimensions as hints (don't override explicit sizes)
            if (view->width <= 0) {
                view->width = cached->measured_width;
            }
            if (!node_is_grid && view->height <= 0) {
                view->height = cached->measured_height;
            }
            log_debug("Applied measurements: view size now %.1fx%.1f (is_grid=%d)", view->width, view->height, node_is_grid);
        } else {
        }
    } else {
    }
}

// Set up basic flex item properties without content layout
void setup_flex_item_properties(LayoutContext* lycon, ViewBlock* view, DomNode* node) {
    (void)lycon; // Suppress unused parameter warning
    if (!view || !node) return;

    // Get display properties
    view->display = resolve_display_value(node);

    // Initialize position and sizing
    view->x = 0;
    view->y = 0;

    // Note: flex-specific properties (flex_grow, flex_shrink, flex_basis) and
    // box model properties (margin, padding, border) will be resolved by the flex algorithm
    // during CSS property resolution. We don't need to initialize them here.
    log_debug("Set up basic properties for flex item: %s", node->node_name());
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
        log_debug("init_flex_item_view: skipping display:none element %s", node->node_name());
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
    if (!node->as_element()->styles_resolved) {
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

    log_debug("init_flex_item_view EXIT for %s (node=%p, created_view=%p)", node->node_name(), node, block);
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

    FontBox saved = lycon->font;
    if (item->font) setup_font(lycon->ui_context, &lycon->font, item->font);
    TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, content, strlen(content));
    float line_height = lycon->font.style && lycon->font.style->font_size > 0
        ? lycon->font.style->font_size : 16.0f;
    if (lycon->font.font_handle) line_height = calc_normal_line_height(lycon->font.font_handle);

    *width += widths.max_content;
    if (line_height > *height) *height = line_height;
    log_debug("calculate_item_intrinsic_sizes: %s content='%s' -> width=%.1f, height=%.1f",
              label, content, widths.max_content, line_height);
    lycon->font = saved;
    return true;
}

void calculate_item_intrinsic_sizes(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item) {
        log_debug("calculate_item_intrinsic_sizes: invalid item");
        return;
    }

    // Form controls use FormControlProp instead of FlexItemProp (they're in a union).
    // Form controls have their intrinsic sizes in form->intrinsic_width/height,
    // not fi->intrinsic_width/height. Skip this function for form controls.
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM) {
        log_debug("calculate_item_intrinsic_sizes: skipping form control (uses FormControlProp)");
        return;
    }

    if (!has_flex_item_prop(item)) {
        log_debug("calculate_item_intrinsic_sizes: no flex properties");
        return;
    }

    // Skip if BOTH intrinsic sizes are already calculated
    // We need both because cross-axis alignment may need the cross-axis intrinsic size
    if (item->fi->has_intrinsic_width && item->fi->has_intrinsic_height) {
        log_debug("calculate_item_intrinsic_sizes: both sizes already calculated");
        return;
    }

    log_debug("Calculating intrinsic sizes for item %p (%s)", item, item->node_name());

    // CRITICAL FIX: Set up font for the flex item BEFORE measuring text
    // This ensures text measurement uses the correct font (e.g., bold, specific size)
    LayoutContext* lycon = flex_layout ? flex_layout->lycon : nullptr;
    FontBox saved_font;  // Save current font to restore later
    bool font_changed = false;
    if (lycon && item->font) {
        saved_font = lycon->font;
        setup_font(lycon->ui_context, &lycon->font, item->font);
        font_changed = true;
    }

    // Initialize to zero
    // Use float to preserve precision from text measurement (avoids truncation)
    float min_width = 0, max_width = 0, min_height = 0, max_height = 0;

    // Check if this is a replaced element (img, video) - needs special handling
    uintptr_t elmt_name = item->tag();
    bool is_replaced = (elmt_name == HTM_TAG_IMG || elmt_name == HTM_TAG_VIDEO ||
                        elmt_name == HTM_TAG_IFRAME || elmt_name == HTM_TAG_CANVAS);

    if (is_replaced && lycon && elmt_name == HTM_TAG_IMG) {
        // Load image to get intrinsic dimensions
        log_debug("calculate_item_intrinsic_sizes: loading image for flex item %s", item->node_name());
        const char* src_value = item->get_attribute("src");
        if (src_value) {
            if (!item->embed) {
                item->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
            }
            if (!item->embed->img) {
                item->embed->img = load_image(lycon->ui_context, src_value);
            }
            if (item->embed->img) {
                ImageSurface* img = item->embed->img;
                float w = img->width * lycon->ui_context->pixel_ratio;
                float h = img->height * lycon->ui_context->pixel_ratio;

                // Check for explicit CSS dimensions
                float explicit_width = (item->blk && item->blk->given_width >= 0) ?
                    item->blk->given_width : -1;
                float explicit_height = (item->blk && item->blk->given_height >= 0) ?
                    item->blk->given_height : -1;

                // Also check max-width as constraint
                float max_width_constraint = layout_positive_max_width_or(
                    lam::view_as_block(item), -1.0f);

                if (explicit_width > 0 && explicit_height > 0) {
                    // Both dimensions specified
                    min_width = max_width = explicit_width;
                    min_height = max_height = explicit_height;
                } else if (explicit_width > 0) {
                    // Width specified, compute height from aspect ratio
                    min_width = max_width = explicit_width;
                    min_height = max_height = explicit_width * h / w;
                } else if (explicit_height > 0) {
                    // Height specified, compute width from aspect ratio
                    min_height = max_height = explicit_height;
                    min_width = max_width = explicit_height * w / h;
                } else if (max_width_constraint > 0 && max_width_constraint < w) {
                    // Max-width constrains the image
                    min_width = max_width = max_width_constraint;
                    min_height = max_height = max_width_constraint * h / w;
                } else {
                    // Use intrinsic dimensions
                    min_width = max_width = w;
                    min_height = max_height = h;
                }
                log_debug("calculate_item_intrinsic_sizes: image intrinsic size=%.1fx%.1f (source=%.1fx%.1f)",
                          min_width, min_height, w, h);
            } else {
                // Failed image data has no natural dimensions; use the same
                // missing-image indicator size as the normal <img> layout path.
                log_debug("calculate_item_intrinsic_sizes: failed to load image %s", src_value);
                min_width = max_width = 16.0f;
                min_height = max_height = 16.0f;
            }
        } else {
            // No current request: <img> has no intrinsic dimensions.
            min_width = max_width = 0.0f;
            min_height = max_height = 0.0f;
        }

        // Store computed intrinsic sizes
        item->fi->intrinsic_width.min_content = min_width;
        item->fi->intrinsic_width.max_content = max_width;
        item->fi->has_intrinsic_width = true;

        item->fi->intrinsic_height.min_content = min_height;
        item->fi->intrinsic_height.max_content = max_height;
        item->fi->has_intrinsic_height = true;

        log_debug("calculate_item_intrinsic_sizes: image final intrinsic=%.1fx%.1f", max_width, max_height);

        // Restore font before returning
        if (font_changed) {
            lycon->font = saved_font;
        }
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
                log_debug("calculate_item_intrinsic_sizes: element has pseudo-element content (before=%d, after=%d)",
                          has_before, has_after);

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
        if (has_pseudo_content) {
            min_width = max_width = pseudo_width;
            min_height = max_height = pseudo_height;
        } else {
            min_width = max_width = 0;
            min_height = max_height = 0;
        }

        log_debug("Empty element intrinsic sizes: width=%.1f, height=%.1f (pseudo_content=%d)",
                  min_width, min_height, has_pseudo_content);
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
                CssEnum text_transform = flex_measure_resolve_text_transform(item);

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
                            log_debug("calculate_item_intrinsic_sizes: %s content '%s' -> +%.1fpx width",
                                      is_before ? "::before" : "::after", pc, pw.max_content);
                        }
                    }
                }

                // Calculate height using CSS line-height if available, otherwise font metrics
                // Line-height is inherited; font-relative lengths/percentages are
                // computed on the declaring ancestor, while unitless numbers use
                // the target element's font-size.
                float resolved_line_height = resolve_flex_inherited_line_height(lycon, item);

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
        // Complex content - check measurement cache first
        // The measurement cache is populated during the first pass of multi-pass flex layout
        log_debug("calculate_item_intrinsic_sizes: checking cache for item %p", item);
        MeasurementCacheEntry* cached = get_from_measurement_cache(static_cast<DomNode*>(item));
        log_debug("calculate_item_intrinsic_sizes: cache lookup returned %p", cached);
        if (cached) {
            log_debug("calculate_item_intrinsic_sizes: cached entry - measured_width=%.1f, measured_height=%.1f",
                      cached->measured_width, cached->measured_height);
        }

        // CRITICAL FIX: For items without explicit dimensions, the cached values may be
        // based on container size, not intrinsic size. In such cases, we should NOT use
        // the cache for the axis that doesn't have an explicit size.
        bool has_explicit_height = (item->blk && item->blk->given_height >= 0);

        // Check if this item is a row flex container
        // For row flex containers, the cached height from measure_flex_child_content might be incorrect
        // because it sums child heights instead of taking the max
        bool is_flex_container = flex_measurement_is_flex_container(item);
        bool is_row_flex_container = is_flex_container ?
            flex_measurement_direction_is_row(item) : false;
        if (is_flex_container) {
            log_debug("calculate_item_intrinsic_sizes: is_flex_container=1, is_row=%d (display flex)",
                      is_row_flex_container);
        }

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
            log_debug("calculate_item_intrinsic_sizes: non-flex container, using measure_element_intrinsic_widths (content_only): min=%.1f, max=%.1f",
                      min_width, max_width);

            // For height, calculate from children or use cached value
            bool item_is_grid_container = (item->display.inner == CSS_VALUE_GRID) ||
                flex_measurement_style_declares_display(
                    item, CSS_VALUE_GRID, CSS_VALUE_INLINE_GRID);

            // Always use calculate_max_content_height for accurate height measurement.
            // The cached heights from measure_flex_child_content use inaccurate
            // hardcoded tag-based estimates (e.g. p=36px, h1=32px + artificial margins).
            {
                float available_width = 10000.0f;
                // In column flex, text wraps at the container's cross-axis width
                // (which is the horizontal axis). Use that instead of 10000px so
                // calculate_max_content_height produces realistic wrapped heights.
                if (flex_layout && !is_main_axis_horizontal(flex_layout) &&
                    flex_layout->cross_axis_size > 0) {
                    available_width = flex_layout->cross_axis_size;
                    // Subtract item's own margin from container cross-axis
                    if (item->bound)
                        available_width -= item->bound->margin.left + item->bound->margin.right;
                    // A column-flex item's auto cross size is the stretched cross
                    // size constrained by its own min/max-width.  Height
                    // measurement must use that constrained content width; otherwise
                    // children such as width:100% images are measured too wide and
                    // produce an inflated flex base size.
                    if (item->blk) {
                        if (item->blk->given_width >= 0.0f) {
                            available_width = item->blk->given_width;
                        }
                        available_width = layout_clamp_positive_min_max_width(
                            lam::view_as_block(item), available_width);
                    }
                    // Subtract item's padding+border to get content width
                    if (item->bound) {
                        available_width -= layout_boundary_metrics(item->bound).pad_border_h;
                    }
                    if (available_width <= 0) available_width = 10000.0f;
                }
                min_height = max_height = flex_measure_intrinsic_max_height(
                    lycon, static_cast<DomNode*>(item), available_width);
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
                log_debug("calculate_item_intrinsic_sizes: calculated height: %.1f avail_w=%.1f (is_grid=%d)", min_height, available_width, item_is_grid_container);
            }

            // Skip the manual child iteration since we've already calculated sizes
            goto store_results;
        }

        // CSS Flexbox §9.9.1: Detect flex-wrap to compute min-content correctly.
        // - nowrap: min-content = sum of item outer min-content sizes
        // - wrap: min-content = max of individual item outer min-content sizes
        bool is_wrapping_flex = false;
        if (is_flex_container && is_row_flex_container) {
            is_wrapping_flex = flex_measurement_wraps(item);
        }

        // First, try to calculate intrinsic sizes from children
        // This handles both width and height by traversing child elements
        // Track min and max content widths separately per CSS intrinsic sizing spec
        float min_child_width = 0.0f;  // For min-content: max of children's min-content
        float max_child_width = 0.0f;  // For max-content: max of children's max-content
        float total_child_width = 0.0f;  // For row flex containers: sum of child widths
        float max_single_child_width = 0.0f;  // For wrapping row flex: max of individual items
        float total_child_height = 0.0f;
        int child_count = 0;  // Count children for gap calculation

        {
            DomNode* c = child;

            // Set up parent context with item's height so children with percentage heights
            // and aspect-ratio can compute their intrinsic width
            BlockContext* saved_parent = nullptr;
            BlockContext temp_parent = {};
            bool need_restore_parent = false;
            if (lycon) {
                // Get item's explicit height (from CSS or resolved)
                float item_height = -1;
                if (item->blk && item->blk->given_height >= 0) {
                    item_height = item->blk->given_height;
                } else {
                    // Try to get from CSS
                    item_height = get_explicit_css_height(lycon, item);
                }
                if (item_height > 0) {
                    saved_parent = lycon->block.parent;
                    temp_parent.content_height = item_height;
                    temp_parent.given_height = item_height;
                    lycon->block.parent = &temp_parent;
                    need_restore_parent = true;
                    log_debug("calculate_item_intrinsic_sizes: set up parent context with height=%.1f", item_height);
                }
            }

            while (c) {
                if (layout_text_node_has_content(c)) {
                    const char* text = (const char*)c->text_data();
                    size_t text_len = strlen(text);
                    float text_min_width, text_max_width, text_height;
                    if (lycon) {
                        FlexMeasureTextRun run = flex_measure_prepare_text_run(c, text, text_len);

                        CssEnum text_transform = flex_measure_resolve_text_transform(item);

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
                        total_child_width += text_max_width;  // Row flex: sum for max-content
                        child_count++;  // Count text as a child for gap calculation
                    } else {
                        min_child_width = max(min_child_width, text_min_width);
                        max_child_width = max(max_child_width, text_max_width);
                    }

                    // For height, row flex takes max, column flex sums
                    flex_accumulate_intrinsic_child_height(
                        is_row_flex_container, text_height, &total_child_height);
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

                        // Get child width - explicit (from View or DOM) or intrinsic
                        if (child_explicit.has_width) {
                            // Explicit width is both min and max
                            child_min_width = child_max_width = child_explicit.width;
                        } else if (child_display.inner == CSS_VALUE_FLEX && lycon) {
                            IntrinsicSizes child_sizes = flex_measure_child_intrinsic_widths(
                                lycon, child_view, /*content_only=*/true);
                            child_min_width = child_sizes.min_content;
                            child_max_width = child_sizes.max_content;
                            log_debug("Used flex container intrinsic widths for child: min=%.1f, max=%.1f",
                                      child_min_width, child_max_width);
                        } else if (child_view->item_prop_type == DomElement::ITEM_PROP_FORM && child_view->form) {
                            child_min_width = child_view->form->intrinsic_width;
                            child_max_width = child_view->form->intrinsic_width;
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
                                lycon, child_view, /*content_only=*/false);
                            child_min_width = child_sizes.min_content;
                            child_max_width = child_sizes.max_content;
                            log_debug("Used measure_element_intrinsic_widths for child: min=%.1f, max=%.1f",
                                      child_min_width, child_max_width);
                        }

                        // Get child height - explicit (from View or DOM) or intrinsic
                        if (child_explicit.has_height) {
                            child_height = child_explicit.height;
                        } else if (is_row_flex_container && lycon && flex_layout &&
                                   !is_main_axis_horizontal(flex_layout) &&
                                   flex_layout->cross_axis_size > 0) {
                            child_height = flex_measure_row_child_height_at_estimated_share(
                                lycon, c, item, child_view, flex_layout);
                        } else if (child_view->item_prop_type == DomElement::ITEM_PROP_FORM && child_view->form) {
                            child_height = child_view->form->intrinsic_height;
                            if (child_height <= 0.0f && lycon) {
                                child_height = flex_measure_intrinsic_max_height(lycon, c, child_max_width);
                            }
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
                        float child_h_margin = get_child_horizontal_margins(
                            lycon, child_view, child_margin_inline_base);
                        float child_v_margin = get_child_vertical_margins(
                            lycon, child_view, child_margin_inline_base);

                        // For width: row flex sums widths, column flex takes max
                        // Track both min and max content widths separately
                        if (is_row_flex_container) {
                            float outer_width = child_max_width + child_h_margin;
                            total_child_width += outer_width;  // Row flex: sum for max-content
                            max_single_child_width = max(max_single_child_width, child_min_width + child_h_margin);
                        } else {
                            min_child_width = max(min_child_width, child_min_width + child_h_margin);
                            max_child_width = max(max_child_width, child_max_width + child_h_margin);
                        }
                        child_count++;

                        // For height, column flex containers sum heights, row flex takes max
                        flex_accumulate_intrinsic_child_height(
                            is_row_flex_container, child_height + child_v_margin,
                            &total_child_height);

                        log_debug("Child element: min_width=%.1f, max_width=%.1f, height=%.1f (explicit=%d/%d)",
                                  child_min_width, child_max_width, child_height,
                                  child_explicit.has_width, child_explicit.has_height);
                    }
                }
                c = c->next_sibling;
            }

            // For row flex containers, add gaps to total width
            if (is_row_flex_container && child_count > 1) {
                // Get gap from the flex container properties
                float gap = flex_measurement_embedded_column_gap(item);
                total_child_width += gap * (child_count - 1);
                log_debug("Row flex: added %d gaps of %.1f = %.1f total gap pixels",
                          child_count - 1, gap, gap * (child_count - 1));
            }

            log_debug("Traversed children: min_width=%.1f, max_width=%.1f, total_width=%.1f, total_height=%.1f, is_row_flex=%d",
                      min_child_width, max_child_width, total_child_width, total_child_height, is_row_flex_container);

            // Restore parent context
            if (need_restore_parent && lycon) {
                lycon->block.parent = saved_parent;
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
                log_debug("Using wrapping row flex widths: min=%.1f (max single), max=%.1f (sum)", min_width, max_width);
            } else {
                // Nowrap: min-content = sum of outer min-content contributions
                min_width = total_child_width;
                log_debug("Using nowrap row flex widths: min=max=%.1f (sum)", min_width);
            }
        } else if (min_child_width > 0.0f || max_child_width > 0.0f) {
            // Use properly tracked min and max content widths
            min_width = min_child_width;
            max_width = max_child_width;
            log_debug("Using calculated widths from children: min=%.1f, max=%.1f", min_width, max_width);
        } else {
            min_width = max_width = 0;
            log_debug("No width from children or cache, using 0");
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
            log_debug("Using cached height for complex content (has explicit height): height=%.1f (stripped padding)", min_height);
        } else if (total_child_height > 0.0f) {
            min_height = total_child_height;
            max_height = total_child_height;
            log_debug("Using calculated height from children: height=%.1f", min_height);
        } else if (cached && cached->measured_height > 0) {
            // Fallback to cache without explicit height requirement
            min_height = strip_padding_border(cached->measured_height);
            max_height = min_height;
            log_debug("Using cached height for complex content: height=%.1f (stripped padding)", min_height);
        } else {
            min_height = max_height = 0;
            log_debug("No height from children or cache, using 0");
        }
    }

store_results:
    // For vertical writing modes, swap intrinsic width/height because text measurement
    // always produces horizontal metrics, but in vertical-lr/rl the inline axis is vertical
    {
        ViewBlock* block_view = lam::view_as_block(item);
        if (block_view && block_view->embed && block_view->embed->flex &&
            (block_view->embed->flex->writing_mode == WM_VERTICAL_LR ||
             block_view->embed->flex->writing_mode == WM_VERTICAL_RL)) {
            float tmp;
            tmp = min_width;  min_width = min_height;  min_height = tmp;
            tmp = max_width;  max_width = max_height;  max_height = tmp;
            log_debug("Intrinsic sizes swapped for vertical writing-mode: width=[%.1f, %.1f], height=[%.1f, %.1f]",
                      min_width, max_width, min_height, max_height);
        }
    }

    // Store results
    item->fi->intrinsic_width.min_content = min_width;
    item->fi->intrinsic_width.max_content = max_width;
    item->fi->intrinsic_height.min_content = min_height;
    item->fi->intrinsic_height.max_content = max_height;
    item->fi->has_intrinsic_width = 1;
    item->fi->has_intrinsic_height = 1;

    log_debug("Intrinsic sizes calculated: width=[%.1f, %.1f], height=[%.1f, %.1f]",
              min_width, max_width, min_height, max_height);

    // Restore font after measurement
    if (font_changed) {
        lycon->font = saved_font;
    }
}
