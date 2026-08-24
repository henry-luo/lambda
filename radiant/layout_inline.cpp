#include "layout.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/font/font.h"
#include "../lib/tagged.hpp"
#include "../lib/strbuf.h"
#include <float.h>
#include <cstring>

float layout_inline_end_edge(ViewSpan* span) {
    if (!span || !span->bound) return 0.0f;
    float edge = span->boundary()->margin.right + span->boundary()->padding.right;
    if (span->boundary()->border) {
        edge += span->boundary()->border->width.right;
    }
    return edge;
}

static bool quirks_br_after_nested_inline_text(LayoutContext* lycon,
                                               DomNode* br_node) {
    if (!lycon || !lycon->doc || !lycon->doc->view_tree || !br_node) return false;
    if (!is_quirks_mode(lycon->doc->view_tree->html_version)) return false;
    ViewBlock* block = layout_nearest_block_ancestor(
        lam::view_require_element(static_cast<View*>(br_node)));
    if (!block || !layout_quirks_block_ignores_line_height(lycon, block)) return false;
    if (lycon->line.has_direct_block_text) return false;
    ViewText* last_text = lycon->line.last_text_view;
    return last_text && last_text->parent && br_node->parent &&
        last_text->parent != br_node->parent;
}

typedef struct InlineOutOfFlowKind {
    bool floated;
    bool positioned;
} InlineOutOfFlowKind;

static InlineOutOfFlowKind inline_out_of_flow_kind(DomElement* elem) {
    InlineOutOfFlowKind kind = {};
    if (!elem) return kind;
    kind.floated = layout_position_is_floated(elem->position);
    kind.positioned = layout_position_is_abs_fixed(elem->position);
    if (kind.floated || kind.positioned || !elem->specified_style ||
        !elem->specified_style->tree) return kind;

    AvlNode* float_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_FLOAT);
    if (float_node) {
        StyleNode* style = (StyleNode*)float_node->declaration;
        const CssValue* value = style && style->winning_decl ? style->winning_decl->value : nullptr;
        kind.floated = value && value->type == CSS_VALUE_TYPE_KEYWORD &&
            (value->data.keyword == CSS_VALUE_LEFT || value->data.keyword == CSS_VALUE_RIGHT);
    }
    AvlNode* position_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_POSITION);
    if (position_node) {
        StyleNode* style = (StyleNode*)position_node->declaration;
        const CssValue* value = style && style->winning_decl ? style->winning_decl->value : nullptr;
        kind.positioned = value && value->type == CSS_VALUE_TYPE_KEYWORD &&
            (value->data.keyword == CSS_VALUE_ABSOLUTE || value->data.keyword == CSS_VALUE_FIXED);
    }
    return kind;
}

static bool inline_span_uses_vertical_axis(ViewSpan* span) {
    CssEnum writing_mode = span
        ? layout_element_css_writing_mode(span) : CSS_VALUE_HORIZONTAL_TB;
    return writing_mode == CSS_VALUE_VERTICAL_LR ||
        writing_mode == CSS_VALUE_VERTICAL_RL ||
        writing_mode == CSS_VALUE_SIDEWAYS_LR ||
        writing_mode == CSS_VALUE_SIDEWAYS_RL;
}

static bool inline_span_start_uses_bottom(ViewSpan* span, bool rtl) {
    return inline_span_uses_vertical_axis(span) && rtl;
}

static float inline_span_edge_extent(ViewSpan* span, bool rtl, bool start_edge,
                                     bool include_margin) {
    if (!span || !span->bound) return 0.0f;
    bool vertical = inline_span_uses_vertical_axis(span);
    bool use_right = start_edge == rtl;
    bool use_bottom = start_edge
        ? inline_span_start_uses_bottom(span, rtl)
        : !inline_span_start_uses_bottom(span, rtl);
    bool use_end = vertical ? use_bottom : use_right;
    BoundaryProp* boundary = span->boundary_mut();
    float edge = 0.0f;
    if (include_margin) edge += vertical
        ? (use_end ? boundary->margin.bottom : boundary->margin.top)
        : (use_end ? boundary->margin.right : boundary->margin.left);
    if (boundary->border) edge += vertical
        ? (use_end ? boundary->border->width.bottom : boundary->border->width.top)
        : (use_end ? boundary->border->width.right : boundary->border->width.left);
    edge += vertical
        ? (use_end ? boundary->padding.bottom : boundary->padding.top)
        : (use_end ? boundary->padding.right : boundary->padding.left);
    return edge;
}

static bool inline_has_axis_edge_decoration(ViewSpan* span, bool rtl, bool start_edge,
                                            bool include_margin = false) {
    if (!span || !span->bound) return false;
    return inline_span_edge_extent(span, rtl, start_edge, include_margin) > 0.0f;
}

static bool text_is_all_collapsible_space(DomText* text, ViewSpan* span);

static bool span_has_direct_visible_text(ViewSpan* span) {
    for (View* child = span ? span->first_child : nullptr; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_TEXT &&
            child->width > 0.0f && child->height > 0.0f &&
            !text_is_all_collapsible_space(
                        lam::dom_as<DOM_NODE_TEXT>(static_cast<DomNode*>(child)), span)) {
            return true;
        }
    }
    return false;
}

static bool span_has_direct_text_on_both_sides_of_block(ViewSpan* span) {
    bool saw_in_flow_block = false;
    bool has_text_before_block = false;
    bool has_text_after_block = false;
    for (View* child = span ? span->first_child : nullptr; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            bool visible = text->width > 0.0f && text->height > 0.0f &&
                !text_is_all_collapsible_space(
                    lam::dom_as<DOM_NODE_TEXT>(static_cast<DomNode*>(child)), span);
            if (visible) {
                if (saw_in_flow_block) has_text_after_block = true;
                else has_text_before_block = true;
            }
            continue;
        }
        ViewBlock* block = lam::view_as_block(child);
        if (block && !layout_block_is_out_of_flow_positioned(block)) {
            saw_in_flow_block = true;
        }
    }
    return has_text_before_block && has_text_after_block;
}

static bool text_is_all_collapsible_space(DomText* text, ViewSpan* span) {
    if (!text || !text->text || text->length == 0) return false;
    CssEnum white_space = span && span->blk ? span->block()->white_space : CSS_VALUE_NORMAL;
    bool collapse_spaces = white_space == CSS_VALUE_NORMAL ||
        white_space == CSS_VALUE_NOWRAP ||
        white_space == CSS_VALUE_PRE_LINE ||
        white_space == 0;
    if (!collapse_spaces) return false;
    for (size_t i = 0; i < text->length; i++) {
        char c = text->text[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
            return false;
        }
    }
    return true;
}

static bool view_has_non_trailing_line_content(View* view, ViewSpan* span) {
    if (!view) return false;
    bool found = false;
    auto inspect = [&](View* candidate) -> bool {
        if (found) return false;
        if (candidate->view_type == RDT_VIEW_NONE) return false;
        if (candidate->view_type == RDT_VIEW_TEXT) {
            found = (candidate->width > 0.0f || candidate->height > 0.0f) &&
                !text_is_all_collapsible_space(
                    lam::dom_as<DOM_NODE_TEXT>(static_cast<DomNode*>(candidate)), span);
        } else if (candidate->view_type != RDT_VIEW_INLINE) {
            found = candidate->width > 0.0f || candidate->height > 0.0f;
        }
        return false;
    };
    auto no_finish = [](View*) {};
    layout_walk_inline_views(view, inspect, no_finish, false);
    return found;
}

static bool has_following_content(DomNode* node, bool inline_only) {
    DomNode* current = node;
    while (current) {
        DomNode* sib = current->next_sibling;
        while (sib) {
            if (sib->is_text()) {
                if (layout_dom_text_has_non_whitespace(
                        lam::dom_as<DOM_NODE_TEXT>(sib))) {
                    return true;
                }
            } else if (sib->is_element()) {
                DomElement* elem = lam::dom_as<DOM_NODE_ELEMENT>(sib);
                DisplayValue display = resolve_display_value(elem);
                if (inline_only) return display.outer == CSS_VALUE_INLINE;
                InlineOutOfFlowKind kind = inline_out_of_flow_kind(elem);
                if (display.outer != CSS_VALUE_NONE && !kind.floated && !kind.positioned) {
                    return true;
                }
            }
            sib = sib->next_sibling;
        }

        DomNode* parent = current->parent;
        if (!parent || !parent->is_element()) break;
        DomElement* parent_elem = lam::dom_as<DOM_NODE_ELEMENT>(parent);
        if (!parent_elem || parent_elem->view_type != RDT_VIEW_INLINE) break;
        current = parent;
    }
    return false;
}

static bool view_is_collapsed_whitespace_text(View* view, ViewSpan* span) {
    if (!view || view->view_type != RDT_VIEW_TEXT) return false;
    if (view->width > 0.0f) return false;
    DomText* text = lam::dom_as<DOM_NODE_TEXT>(static_cast<DomNode*>(view));
    return text_is_all_collapsible_space(text, span);
}

static bool ruby_annotation_is_outside_base_bounds(ViewSpan* span, View* child) {
    if (!span || !child || span->display.inner != CSS_VALUE_RUBY ||
        child->view_type != RDT_VIEW_INLINE) {
        return false;
    }
    return static_cast<DomNode*>(child)->tag() == MARKUP_NAME_RT;
}

static bool ruby_annotation_node(DomNode* node) {
    return node && node->is_element() && node->tag() == MARKUP_NAME_RT;
}

static bool ruby_has_text_box_trim_ancestor(const ViewSpan* ruby, uint8_t trim) {
    for (const DomNode* node = ruby ? static_cast<const DomNode*>(ruby) : nullptr;
         node; node = node->parent) {
        if (!node->is_element()) continue;
        const DomElement* element = node->as_element();
        if (element->blk && (element->block()->text_box_trim & trim) != 0) {
            return true;
        }
    }
    return false;
}

static void stretch_simple_ruby_annotation_to_column(ViewSpan* annotation,
                                                     float column_width) {
    if (!annotation || annotation->width >= column_width) return;
    View* child = annotation->first_placed_child();
    if (child && !child->next() && child->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            rect->width = column_width;
        }
        text->width = column_width;
    }
    annotation->width = column_width;
}

static ViewText* simple_ruby_base_text(ViewSpan* ruby, ViewSpan* annotation) {
    if (!ruby || !annotation) return nullptr;
    View* base = ruby->first_placed_child();
    if (!base || base->view_type != RDT_VIEW_TEXT || base->next() != annotation ||
        annotation->next()) {
        return nullptr;
    }
    return lam::view_require<RDT_VIEW_TEXT>(base);
}

static bool ruby_has_simple_text_pair(DomNode* first_child) {
    if (!first_child || !first_child->is_text()) return false;
    DomNode* annotation = first_child->next_sibling;
    if (!ruby_annotation_node(annotation) || annotation->next_sibling) return false;
    DomElement* annotation_element = annotation->as_element();
    DomNode* annotation_text = annotation_element->first_child;
    return annotation_text && annotation_text->is_text() && !annotation_text->next_sibling;
}

static bool layout_prepare_simple_ruby_column(ViewSpan* ruby,
                                              bool has_simple_text_pair,
                                              float base_start_x,
                                              float base_width,
                                              float annotation_width,
                                              float preceding_space_width,
                                              float* annotation_x,
                                              float* inline_advance_extra) {
    if (!ruby || !has_simple_text_pair || !annotation_x || !inline_advance_extra) {
        return false;
    }
    float column_width = annotation_width;
    if (column_width <= base_width + 0.01f) return false;

    float extra_width = column_width - base_width;
    float start_overhang = min(extra_width * 0.5f, max(0.0f, preceding_space_width));
    float end_extension = extra_width - start_overhang;
    DomElementExt* ext = ruby->ensure_ext();
    if (!ext) return false;
    ext->ruby_column_anchor_x = base_start_x;
    ext->ruby_column_width = column_width;
    ext->ruby_column_inline_advance = base_width + end_extension;
    ext->ruby_column_start_overhang = start_overhang;
    ext->has_simple_ruby_column_geometry = true;
    *annotation_x = base_start_x - start_overhang;
    *inline_advance_extra = end_extension;
    return true;
}

void layout_apply_simple_ruby_column_geometry(ViewSpan* ruby) {
    if (!ruby || ruby->tag() != MARKUP_NAME_RUBY || !ruby->ext ||
        !ruby->ext->has_simple_ruby_column_geometry) {
        return;
    }
    View* annotation_view = ruby->first_placed_child();
    if (!annotation_view || annotation_view->view_type != RDT_VIEW_TEXT) return;
    annotation_view = annotation_view->next();
    if (!annotation_view || annotation_view->view_type != RDT_VIEW_INLINE ||
        annotation_view->tag() != MARKUP_NAME_RT) {
        return;
    }
    ViewSpan* annotation = lam::view_require<RDT_VIEW_INLINE>(annotation_view);
    ViewText* base = simple_ruby_base_text(ruby, annotation);
    if (!base || !base->rect || base->rect->next) return;

    DomElementExt* ext = ruby->ext;
    float column_x = ext->ruby_column_anchor_x - ext->ruby_column_start_overhang;
    base->x = column_x;
    base->width = ext->ruby_column_width;
    base->rect->x = column_x;
    base->rect->width = ext->ruby_column_width;
    ruby->x = ext->ruby_column_anchor_x;
    ruby->width = ext->ruby_column_inline_advance;
}

static void merge_ruby_annotation_line_metrics(Linebox* base_line,
                                                const Linebox* annotation_line) {
    if (!base_line || !annotation_line) return;
    base_line->max_ascender = max(
        base_line->max_ascender, annotation_line->max_ascender);
    base_line->max_descender = max(
        base_line->max_descender, annotation_line->max_descender);
    base_line->max_css_baseline_ascender = max(
        base_line->max_css_baseline_ascender,
        annotation_line->max_css_baseline_ascender);
    base_line->max_normal_line_height = max(
        base_line->max_normal_line_height, annotation_line->max_normal_line_height);
    base_line->has_different_inline_font =
        base_line->has_different_inline_font || annotation_line->has_different_inline_font;
}

static void contribute_over_ruby_annotation_line_metrics(Linebox* base_line,
                                                         const Linebox* base_line_before_annotation,
                                                         const BlockContext* base_block,
                                                         const ViewSpan* annotation) {
    if (!base_line || !base_line_before_annotation || !base_block || !annotation) return;
    if (!base_line_before_annotation->has_initial_letter) return;
    float over_shift = max(0.0f, annotation->height - base_block->lead_y);
    float required_ascender = base_line_before_annotation->max_ascender + over_shift -
        base_line_before_annotation->initial_letter_origin_advance;
    base_line->max_ascender = max(base_line->max_ascender, required_ascender);
    base_line->ruby_annotation_over_shift = max(
        base_line->ruby_annotation_over_shift, over_shift);
}

static void begin_ruby_annotation_inline_context(LayoutContext* lycon,
                                                 const Linebox* base_line) {
    if (!lycon || !base_line) return;
    line_init(lycon, base_line->left, FLT_MAX);
}

static void contribute_under_ruby_annotation_line_height(Linebox* base_line,
                                                         const BlockContext* base_block,
                                                         const ViewSpan* annotation) {
    if (!base_line || !base_block || !annotation) return;
    float annotation_bottom = annotation->y + annotation->height;
    float annotation_line_height = annotation_bottom - base_block->advance_y -
        base_block->lead_y;
    if (annotation_line_height > 0.0f) {
        base_line->ruby_annotation_min_line_height = max(
            base_line->ruby_annotation_min_line_height, annotation_line_height);
    }
}

static void inline_text_line_range(View* view, ViewSpan* whitespace_context, bool* found,
                                   int* first_line, int* last_line) {
    if (!view) return;
    if (view->view_type == RDT_VIEW_TEXT) {
        // cannot make an ancestor a multi-line inline box.
        if (view_is_collapsed_whitespace_text(view, whitespace_context)) return;
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (!*found) {
                *found = true;
                *first_line = rect->line_number;
                *last_line = rect->line_number;
            } else {
                if (rect->line_number < *first_line) *first_line = rect->line_number;
                if (rect->line_number > *last_line) *last_line = rect->line_number;
            }
        }
        return;
    }
    if (view->view_type != RDT_VIEW_INLINE) return;
    ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
    for (View* child = span->first_child; child; child = child->next()) {
        inline_text_line_range(child, span, found, first_line, last_line);
    }
}

static bool inline_view_text_differs_from_y(View* view, float y) {
    for (View* child = view; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (fabsf(rect->y - y) > 0.5f) return true;
            }
        } else if (child->view_type == RDT_VIEW_INLINE) {
            if (inline_view_text_differs_from_y(
                    lam::view_require<RDT_VIEW_INLINE>(child)->first_child, y)) {
                return true;
            }
        }
    }
    return false;
}

static bool inline_span_marker_splits_text_line(ViewSpan* span) {
    for (View* child = span ? span->first_child : nullptr; child; child = child->next()) {
        if (child->view_type != RDT_VIEW_MARKER) continue;
        if (inline_view_text_differs_from_y(child->next(), child->y)) return true;
    }
    return false;
}

static View* inline_span_first_line_fragment_child(ViewSpan* span) {
    if (!span) return nullptr;
    View* first = span->first_placed_child();
    while (first && (first->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(first))) {
        first = first->next();
    }
    return first;
}

static bool inline_block_contains_list_item(View* atomic) {
    if (!atomic || atomic->view_type != RDT_VIEW_INLINE_BLOCK) return false;
    ViewElement* atomic_element = lam::view_require_element(atomic);
    for (View* child = atomic_element->first_placed_child(); child; child = child->next()) {
        ViewBlock* block = lam::view_as_block(child);
        if (block && block->display.list_item) {
            return true;
        }
    }
    return false;
}

static bool inline_child_is_list_item(View* child) {
    if (!child || child->view_type != RDT_VIEW_INLINE_BLOCK) return false;
    ViewBlock* block = lam::view_as_block(child);
    if (block && block->display.list_item) {
        return true;
    }
    return inline_block_contains_list_item(child);
}

static bool inline_span_has_list_item_atomic_child(ViewSpan* span) {
    if (!span || span->display.outer != CSS_VALUE_INLINE) return false;
    bool found_atomic = false;
    for (View* child = span->first_child; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(child) ||
            view_is_collapsed_whitespace_text(child, span)) {
            continue;
        }
        if (child->view_type == RDT_VIEW_TEXT &&
            text_is_all_collapsible_space(
                lam::dom_as<DOM_NODE_TEXT>(static_cast<DomNode*>(child)), span)) {
            continue;
        }
        if (!inline_child_is_list_item(child)) return false;
        found_atomic = true;
    }
    return found_atomic;
}

static bool inline_span_has_atomic_children_on_multiple_lines(ViewSpan* span) {
    // CSS 2.1 §10.8.1: line identity comes from the containing block's line
    // number, not from atomic child y positions after baseline alignment.
    if (!span || span->display.outer != CSS_VALUE_INLINE) return false;
    int first_line = -1;
    bool found_atomic = false;
    for (View* child = span->first_child; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(child) ||
            view_is_collapsed_whitespace_text(child, span)) {
            continue;
        }
        if (!inline_child_is_list_item(child)) continue;
        if (!lam::view_as_block(child) || child->inline_line_number < 0) continue;
        if (!found_atomic) {
            first_line = child->inline_line_number;
            found_atomic = true;
        } else if (child->inline_line_number != first_line) {
            return true;
        }
    }
    return false;
}

static bool inline_span_has_forced_break_view(View* view) {
    if (!view) return false;
    bool found = false;
    auto inspect = [&](View* candidate) -> bool {
        if (candidate->view_type == RDT_VIEW_BR) found = true;
        return false;
    };
    auto no_finish = [](View*) {};
    layout_walk_inline_views(view, inspect, no_finish, false);
    return found;
}

static bool inline_span_has_out_of_flow_descendant(ViewSpan* span) {
    if (!span) return false;
    bool found = false;
    auto inspect = [&](View* candidate) -> bool {
        if (candidate != static_cast<View*>(span) && layout_view_is_out_of_flow(candidate)) {
            found = true;
        }
        return found;
    };
    auto no_finish = [](View*) {};
    layout_walk_inline_views(static_cast<View*>(span), inspect, no_finish, false);
    return found;
}

static bool inline_fragment_union_extends_child_bounds(ViewSpan* span) {
    if (!span || !span->has_inline_fragment_union()) return false;
    bool found_child = false;
    float min_y = 0.0f;
    float max_y = 0.0f;
    for (View* child = span->first_child; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(child) ||
            view_is_collapsed_whitespace_text(child, span)) {
            continue;
        }
        float child_min_y = child->y;
        float child_max_y = child->y + child->height;
        if (!found_child) {
            found_child = true;
            min_y = child_min_y;
            max_y = child_max_y;
        } else {
            if (child_min_y < min_y) min_y = child_min_y;
            if (child_max_y > max_y) max_y = child_max_y;
        }
    }
    return found_child &&
        (span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->min_y < min_y ||
         span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->max_y > max_y);
}

bool inline_span_has_multiple_line_fragments(ViewSpan* span) {
    View* first = inline_span_first_line_fragment_child(span);
    if (!first) return false;
    if (layout_span_children_have_no_line_content(span)) {
        // zero-width breaks and preserved newline records do not create a
        // second visible inline fragment for the containing box.
        return false;
    }

    bool found_text_line = false;
    int first_line = 0;
    int last_line = 0;
    inline_text_line_range(static_cast<View*>(span), span, &found_text_line,
                           &first_line, &last_line);
    if (inline_fragment_union_extends_child_bounds(span)) return true;
    if (found_text_line) {
        if (first_line != last_line) return true;
        if (inline_span_marker_splits_text_line(span)) return true;
        // CSS 2.1 §8.5.1: direct text on one line does not exclude atomic
        // inline-level siblings from extending the ancestor across other lines.
        bool has_flow_root_list_items =
            inline_span_has_list_item_atomic_child(span);
        bool has_atomic_line_break =
            inline_span_has_atomic_children_on_multiple_lines(span);
        bool has_forced_break = inline_span_has_forced_break_view(
            static_cast<View*>(span));
        if (!has_flow_root_list_items && !has_atomic_line_break &&
            !has_forced_break) return false;
    }

    float first_y = first->y;
    for (View* child = first->next(); child; child = child->next()) {
        if (child->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(child)) continue;
        if (child->y != first_y) {
            return true;
        }
    }
    return false;
}

static bool inline_span_flow_root_list_item_line_box_bounds(
        ViewSpan* span, FontHandle* fallback_fh, float* min_y, float* max_y) {
    if (!span || !min_y || !max_y) return false;
    // CSS Inline 3: an inline box containing only list-item atomic children
    // uses line-box bounds; their border boxes may protrude from those lines.
    if (!inline_span_has_list_item_atomic_child(span)) return false;
    FontHandle* font_handle = span->font ? span->fontp()->font_handle : fallback_fh;
    float line_box_height = font_handle ? font_get_cell_height(font_handle) : 0.0f;
    if (line_box_height <= 0.0f) {
        line_box_height = span->content_height;
    }
    if (line_box_height <= 0.0f) return false;

    bool found_atomic = false;
    bool found_line_bounds = false;
    int current_line = 0;
    float current_baseline_y = 0.0f;
    float bounds_min_y = 0.0f;
    float bounds_max_y = 0.0f;
    auto finish_line = [&]() {
        float line_min_y = current_baseline_y - line_box_height;
        if (!found_line_bounds || line_min_y < bounds_min_y) bounds_min_y = line_min_y;
        if (!found_line_bounds || current_baseline_y > bounds_max_y) bounds_max_y = current_baseline_y;
        found_line_bounds = true;
    };

    for (View* child = span->first_child; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(child)) {
            continue;
        }
        if (child->view_type == RDT_VIEW_TEXT &&
            text_is_all_collapsible_space(
                lam::dom_as<DOM_NODE_TEXT>(static_cast<DomNode*>(child)), span)) {
            continue;
        }
        float child_baseline_y = child->y + child->height;
        int child_line = child->inline_line_number;
        if (!found_atomic) {
            found_atomic = true;
            current_line = child_line;
            current_baseline_y = child_baseline_y;
            continue;
        }
        bool same_line = child_line == current_line;
        if (!same_line) {
            finish_line();
            current_line = child_line;
            current_baseline_y = child_baseline_y;
        } else {
            if (child_baseline_y > current_baseline_y) current_baseline_y = child_baseline_y;
        }
    }
    if (!found_atomic) return false;
    finish_line();
    *min_y = bounds_min_y;
    *max_y = bounds_max_y;
    return *max_y > *min_y;
}

bool inline_span_float_continuation_x(
        ViewSpan* span, float* continuation_x, bool* has_left_float) {
    if (has_left_float) *has_left_float = false;
    if (!span || !continuation_x) return false;
    bool found_float = false;
    bool found_left_float = false;
    float max_right = *continuation_x;
    for (View* child = span->first_child; child; child = child->next()) {
        DomElement* child_elem = lam::dom_as<DOM_NODE_ELEMENT>(static_cast<DomNode*>(child));
        if (!child_elem || !child_elem->position) {
            continue;
        }
        CssEnum float_prop = child_elem->positionp()->float_prop;
        if (float_prop != CSS_VALUE_LEFT && float_prop != CSS_VALUE_RIGHT) continue;
        found_float = true;
        if (float_prop != CSS_VALUE_LEFT) continue;
        float dx = 0.0f;
        float dy = 0.0f;
        ViewBlock* child_block = lam::view_as_block(child);
        if (child_block) {
            layout_relative_position_offset(child_block, &dx, &dy);
        }
        float margin_right = child_block && child_block->bound ?
            child_block->boundary()->margin.right : 0.0f;
        float static_right = child->x - dx + child->width + margin_right;
        if (!found_left_float || static_right > max_right) {
            max_right = static_right;
        }
        found_left_float = true;
    }
    if (found_left_float) {
        *continuation_x = max_right;
    }
    if (has_left_float) *has_left_float = found_left_float;
    return found_float;
}

static void contribute_inline_strut(LayoutContext* lycon, DomNode* source, ViewSpan* span) {
    if (!lycon || !span || !lycon->font.font_handle) return;
    float ascender = 0.0f, descender = 0.0f;
    if (lycon->block.line_height_is_normal) {
        font_get_normal_lh_split(lycon->font.font_handle, &ascender, &descender);
    } else {
        font_get_content_area_split(lycon->font.font_handle, &ascender, &descender);
        float content_height = ascender + descender;
        float half_leading = (lycon->block.line_height - content_height) / 2.0f;
        ascender += half_leading;
        descender += half_leading;
    }
    if (ascender > 0.0f || descender > 0.0f) {
        float baseline_shift = vertical_align_baseline_shift(
            lycon, lycon->line.vertical_align,
            lycon->line.vertical_align_offset);
        if (baseline_shift != 0.0f) {
            ascender += baseline_shift;
            descender -= baseline_shift;
        }
        lycon->line.max_ascender = max(lycon->line.max_ascender, ascender);
        lycon->line.max_descender = max(lycon->line.max_descender, descender);
        if (lycon->block.line_height_is_normal) {
            float normal_lh = font_calc_normal_line_height(lycon->font.font_handle);
            lycon->line.max_normal_line_height = max(lycon->line.max_normal_line_height, normal_lh);
        }
    }
    span->content_height = lycon->block.line_height;
}

static void span_record_ancestor_fragment(ViewSpan* span, View* fragment) {
    if (!span || !fragment) return;
    float min_x = fragment->x;
    float max_x = fragment->x + fragment->width;
    float min_y = fragment->y;
    float max_y = fragment->y + fragment->height;
    layout_extend_fragment_union(span, FRAGMENT_UNION_ANCESTOR,
                                 min_x, max_x, min_y, max_y);
}

static void span_record_split_inline_fragment(ViewSpan* span, float min_x, float max_x,
                                              float min_y, float max_y) {
    layout_extend_fragment_union(span, FRAGMENT_UNION_SPLIT_INLINE,
                                 min_x, max_x, min_y, max_y);
}

static void span_record_current_split_line_fragment(LayoutContext* lycon, ViewSpan* span,
                                                    float span_line_height) {
    if (!lycon || !span || lycon->line.is_line_start) return;
    FontProp* font = span->font ? span->font : lycon->font.style;
    FontHandle* font_handle = span->font ? span->fontp()->font_handle : lycon->font.font_handle;
    if (!font || !font_handle || span_line_height <= 0.0f) return;

    float line_height = 0.0f;
    float baseline_pos = line_baseline_position(lycon, &line_height);
    float fragment_y = layout_inline_font_box_y(
        lycon, span, span_line_height,
        font->ascender, font->descender, baseline_pos, 0.0f, 0.0f);
    float fragment_height = font_get_cell_height(font_handle);
    if (fragment_height <= 0.0f) fragment_height = font->ascender + font->descender;
    if (fragment_height <= 0.0f) fragment_height = span_line_height;

    float fragment_min_x = lycon->line.left;
    float fragment_max_x = lycon->line.right;
    if (lycon->line.text_indent_offset != 0.0f) {
        // CSS 2.1 §16.1: text-indent belongs to the block's first line,
        // not to an inline descendant's own border-box fragment.
        if (lycon->block.direction == CSS_VALUE_RTL) {
            fragment_min_x += lycon->line.text_indent_offset;
        } else {
            fragment_max_x -= lycon->line.text_indent_offset;
        }
    }
    span_record_split_inline_fragment(span, fragment_min_x, fragment_max_x,
                                      fragment_y, fragment_y + fragment_height);
}

static void record_block_in_inline_split_chain(ViewSpan* span) {
    if (!span || span->width < 0.0f || span->height < 0.0f) return;

    LayoutInlineDecorationEdges edges = layout_inline_decoration_edges(span);
    float top_edge = roundf(edges.top);
    float bottom_edge = roundf(edges.bottom);
    float fragment_min_x = span->x;
    float fragment_max_x = span->x + span->width;
    float fragment_min_y = span->y + top_edge;
    float fragment_max_y = span->y + span->height - bottom_edge;
    if (fragment_max_y < fragment_min_y) fragment_max_y = fragment_min_y;
    DomNode* ancestor = span->parent;
    while (ancestor && ancestor->is_element()) {
        if (ancestor->view_type != RDT_VIEW_INLINE) break;
        ViewSpan* ancestor_span = lam::view_require<RDT_VIEW_INLINE>(ancestor);
        float ancestor_min_y = ancestor_span->y;
        if (fragment_min_y < ancestor_min_y) ancestor_min_y = fragment_min_y;
        span_record_split_inline_fragment(ancestor_span, fragment_min_x, fragment_max_x,
                                          ancestor_min_y, fragment_max_y);
        ancestor = ancestor->parent;
    }
}

static void compute_span_from_collapsed_line_fragment(ViewSpan* span) {
    LayoutInlineDecorationEdges edges = layout_inline_decoration_edges(span);
    float left_edge = edges.left;
    float right_edge = edges.right;
    float top_edge = roundf(edges.top);
    float bottom_edge = roundf(edges.bottom);
    float fragment_width =
        span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->max_x - span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->min_x;
    float fragment_height =
        span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->max_y - span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->min_y;
    if (span->content_height > fragment_height) {
        fragment_height = span->content_height;
    }

    span->x = span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->min_x - left_edge;
    span->y = span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->min_y - top_edge;
    span->width = fragment_width + left_edge + right_edge;
    span->height = fragment_height + top_edge + bottom_edge;
}

static void compute_empty_span_bounding_box(ViewSpan* span, FontHandle* fallback_fh) {
    if (span->has_collapsed_line_fragment_union()) {
        compute_span_from_collapsed_line_fragment(span);
        return;
    }
    // CSS 2.1 section 9.4.2: only inline-axis decorations keep an empty span present.
    float margin_left = 0.0f, margin_right = 0.0f;
    LayoutInlineDecorationEdges edges = layout_inline_decoration_edges(span);
    if (span->bound) {
        margin_left = span->boundary()->margin.left;
        margin_right = span->boundary()->margin.right;
    }

    float inline_size = edges.left + edges.right;
    if (inline_size > 0.0f || margin_left != 0.0f || margin_right != 0.0f) {
        FontHandle* font = span->font ? span->fontp()->font_handle : fallback_fh;
        float font_content_height = font ? font_get_cell_height(font) : 0.0f;
        span->width = inline_size;
        span->height = roundf(font_content_height + edges.top + edges.bottom);
    } else {
        span->width = 0.0f;
        span->height = 0.0f;
    }
}

void compute_span_bounding_box(ViewSpan* span, bool is_multi_line, struct FontHandle* fallback_fh) {
    if (span->has_collapsed_line_fragment_union() &&
        layout_span_children_have_no_line_content(span)) {
        // CSS 2.1 §10.8.1: zero-content inline descendants use the collapsed
        // line fragment position; treating their zero-size child as content
        // anchors the decorated span at the containing block origin.
        compute_span_from_collapsed_line_fragment(span);
        return;
    }
    View* child = span->first_child;
    if (!child) {
        compute_empty_span_bounding_box(span, fallback_fh);
        return;
    }
    // by their containing block, not the inline span (CSS 2.1 §9.3.1, §10.6.3)
    while (child && (child->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(child) ||
           (child->is_block() && layout_block_is_self_collapsing(lam::view_require_block(child))) ||
           ruby_annotation_is_outside_base_bounds(span, child) ||
           view_is_collapsed_whitespace_text(child, span))) {
        if (view_is_collapsed_whitespace_text(child, span)) {
            span_record_ancestor_fragment(span, child);
        }
        child = child->next();
    }
    if (!child) {
        compute_empty_span_bounding_box(span, fallback_fh);
        return;
    }
    // CSS 2.1 §8.3: inline-level child margins are part of the inline flow and
    auto get_child_relative_offset = [](View* c, float* offset_x, float* offset_y) {
        if (offset_x) *offset_x = 0.0f;
        if (offset_y) *offset_y = 0.0f;
        if (!c) return;
        if (ViewBlock* vb = lam::view_as_block(c)) {
            layout_relative_position_offset(vb, offset_x, offset_y);
        } else if (ViewSpan* elem = lam::view_as<RDT_VIEW_INLINE>(c)) {
            layout_relative_position_offset(lam::unsafe_view_block_api_span(elem),
                                            offset_x, offset_y);
        }
    };

    auto text_child_uses_slice_decoration = [](View* c) -> bool {
        if (!c || c->view_type != RDT_VIEW_TEXT || !c->parent || !c->parent->is_element()) {
            return false;
        }
        DomElement* parent = c->parent->as_element();
        CssDeclaration* declaration = parent->specified_style
            ? style_tree_get_declaration(parent->specified_style,
                                         CSS_PROPERTY_BOX_DECORATION_BREAK)
            : nullptr;
        return declaration && declaration->value &&
            declaration->value->type == CSS_VALUE_TYPE_KEYWORD &&
            declaration->value->data.keyword == CSS_VALUE_SLICE;
    };

    auto text_child_has_collapsed_leading_fragment = [](View* c) -> bool {
        if (!c || c->view_type != RDT_VIEW_TEXT) return false;
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(c);
        bool saw_zero_fragment = false;
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->width <= 0.0f && rect->height > 0.0f) {
                saw_zero_fragment = true;
            } else if (rect->width > 0.0f) {
                return saw_zero_fragment;
            }
        }
        return false;
    };

    auto get_child_static_x_edge = [&get_child_relative_offset,
                                    &text_child_uses_slice_decoration,
                                    &text_child_has_collapsed_leading_fragment](
        View* c, bool right_edge) -> float {
        float dx = 0.0f;
        get_child_relative_offset(c, &dx, nullptr);
        float edge = c->x - dx + (right_edge ? c->width : 0.0f);
        if (text_child_uses_slice_decoration(c) &&
            text_child_has_collapsed_leading_fragment(c)) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(c);
            float rect_edge = right_edge ? -FLT_MAX : FLT_MAX;
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (rect->width <= 0.0f) continue;
                float visible_edge = right_edge ? rect->x + rect->width : rect->x;
                if ((right_edge && visible_edge > rect_edge) ||
                    (!right_edge && visible_edge < rect_edge)) {
                    rect_edge = visible_edge;
                }
            }
            if ((right_edge && rect_edge > -FLT_MAX) ||
                (!right_edge && rect_edge < FLT_MAX)) {
                edge = rect_edge - dx;
            }
        }
        if (ViewSpan* sp = lam::view_as<RDT_VIEW_INLINE>(c)) {
            FragmentUnion* ancestor_union = sp->has_ancestor_fragment_union()
                ? sp->ensure_fragment_union(FRAGMENT_UNION_ANCESTOR) : nullptr;
            if (ancestor_union) {
                float ancestor_edge = right_edge ? ancestor_union->max_x : ancestor_union->min_x;
                if ((right_edge && ancestor_edge > edge) ||
                    (!right_edge && ancestor_edge < edge)) {
                    edge = ancestor_edge;
                }
            }
        }
        return edge;
    };
    auto get_child_static_x = [&get_child_static_x_edge](View* c) {
        return get_child_static_x_edge(c, false);
    };
    auto get_child_static_right = [&get_child_static_x_edge](View* c) {
        return get_child_static_x_edge(c, true);
    };

    auto get_child_inline_margin = [](View* c, bool inline_start) -> float {
        BoundaryProp* bound = nullptr;
        if (ViewBlock* block = lam::view_as_block<RDT_VIEW_INLINE_BLOCK>(c)) {
            bound = block->bound;
        } else if (ViewSpan* child_span = lam::view_as<RDT_VIEW_INLINE>(c)) {
            bound = child_span->bound;
        }
        return inline_start ? layout_axis_margin_start(bound, LAYOUT_AXIS_X)
                            : layout_axis_margin_end(bound, LAYOUT_AXIS_X);
    };

    auto get_child_outer_left = [&get_child_static_x,
                                 &get_child_inline_margin](View* c) -> float {
        float left = get_child_static_x(c);
        return left - get_child_inline_margin(c, true);
    };
    auto get_child_outer_right = [&get_child_static_right,
                                  &get_child_inline_margin](View* c) -> float {
        float right = get_child_static_right(c);
        return right + get_child_inline_margin(c, false);
    };
    // CSS 2.1 §10.6.1: For inline non-replaced elements, vertical borders/padding
    // CSS 2.1 §9.4.3: Relative positioning moves a box visually but does not
    auto get_child_static_y_edge = [&get_child_relative_offset](
        View* c, bool bottom_edge) -> float {
        float dy = 0.0f;
        get_child_relative_offset(c, nullptr, &dy);
        return c->y - dy + (bottom_edge ? c->height : 0.0f);
    };
    auto get_child_static_y = [&get_child_static_y_edge](View* c) {
        return get_child_static_y_edge(c, false);
    };
    auto get_child_static_bottom = [&get_child_static_y_edge](View* c) {
        return get_child_static_y_edge(c, true);
    };

    auto get_child_content_y_edge = [&get_child_static_y_edge](
        View* c, bool bottom_edge) -> float {
        if (ViewSpan* cs = lam::view_as<RDT_VIEW_INLINE>(c)) {
            float edge = get_child_static_y_edge(c, bottom_edge);
            float border = 0.0f, padding = 0.0f;
            if (cs->bound) {
                if (cs->boundary_mut()->border) {
                    border = bottom_edge ? cs->boundary()->border->width.bottom
                                         : cs->boundary()->border->width.top;
                }
                padding = bottom_edge ? cs->boundary()->padding.bottom
                                      : cs->boundary()->padding.top;
            }
            return edge + (bottom_edge ? -(border + max(padding, 0.0f))
                                       : border + max(padding, 0.0f));
        }
        return get_child_static_y_edge(c, bottom_edge);
    };
    auto get_child_content_y = [&get_child_content_y_edge](View* c) {
        return get_child_content_y_edge(c, false);
    };
    auto get_child_content_bottom = [&get_child_content_y_edge](View* c) {
        return get_child_content_y_edge(c, true);
    };
    bool has_out_of_flow_descendant = inline_span_has_out_of_flow_descendant(span);

    float min_x = get_child_outer_left(child);
    float child_sy = get_child_static_y(child);
    float visual_min_y = child_sy;
    float max_x = get_child_outer_right(child);
    float visual_max_y = get_child_static_bottom(child);
    float content_min_y = get_child_content_y(child);
    float content_max_y = get_child_content_bottom(child);

    child = child->next();
    while (child) {
        if (child->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(child) ||
            (child->is_block() && layout_block_is_self_collapsing(lam::view_require_block(child))) ||
            ruby_annotation_is_outside_base_bounds(span, child) ||
            view_is_collapsed_whitespace_text(child, span)) {
            if (view_is_collapsed_whitespace_text(child, span)) {
                span_record_ancestor_fragment(span, child);
            }
            child = child->next();
            continue;
        }
        // CSS 2.1 §9.2.1.1: a zero-width BR forces the next line but does not
        // contribute a horizontal edge to its ancestor inline box.
        if (child->view_type != RDT_VIEW_BR || child->width > 0.0f) {
            float child_min_x = get_child_outer_left(child);
            float child_max_x = get_child_outer_right(child);

            if (child_min_x < min_x) min_x = child_min_x;
            if (child_max_x > max_x) max_x = child_max_x;
        }

        if (!has_out_of_flow_descendant || child->view_type != RDT_VIEW_BR ||
            child->height > 0.0f) {
            // CSS 2.1 §9.2.1.1: a collapsed BR forces the next line but has no
            // vertical border-box extent to include in a split inline union.
            float sy = get_child_static_y(child);
            if (sy < visual_min_y) visual_min_y = sy;
            float sb = get_child_static_bottom(child);
            if (sb > visual_max_y) visual_max_y = sb;

            float cy = get_child_content_y(child);
            float cb = get_child_content_bottom(child);
            if (cy < content_min_y) content_min_y = cy;
            if (cb > content_max_y) content_max_y = cb;
        }

        child = child->next();
    }

    float atomic_line_min_y = 0.0f;
    float atomic_line_max_y = 0.0f;
    bool span_has_direct_text = span_has_direct_visible_text(span);
    bool has_atomic_line_bounds = inline_span_flow_root_list_item_line_box_bounds(
        span, fallback_fh, &atomic_line_min_y, &atomic_line_max_y);
    if (!span_has_direct_text && has_atomic_line_bounds) {
        // CSS Inline 3 §2.2, §5.3: an inline box contributes its own
        // line-height bounds; an atomic child's border box does not size it.
        visual_min_y = atomic_line_min_y;
        visual_max_y = atomic_line_max_y;
        content_min_y = atomic_line_min_y;
        content_max_y = atomic_line_max_y;
    }

    LayoutInlineDecorationEdges edges = layout_inline_decoration_edges(span);
    // CSS 2.1 §9.4.2: If children have zero content extent AND the span has no
    float left_edge = edges.left;
    float right_edge = edges.right;
    float inline_sum = left_edge + right_edge;
    bool clone_decoration_break = false;
    if (span->specified_style) {
        CssDeclaration* decoration_decl = style_tree_get_declaration(
            span->specified_style, CSS_PROPERTY_BOX_DECORATION_BREAK);
        CssEnum decoration_value = decoration_decl && decoration_decl->value &&
            decoration_decl->value->type == CSS_VALUE_TYPE_KEYWORD
            ? decoration_decl->value->data.keyword : CSS_VALUE__UNDEF;
        clone_decoration_break = decoration_value == CSS_VALUE_CLONE;
    }
    bool forced_break_decoration = inline_span_has_forced_break_view(
        static_cast<View*>(span));
    bool clone_forced_break = clone_decoration_break && forced_break_decoration;
    float content_width = max_x - min_x;
    float visual_height = visual_max_y - visual_min_y;
    if (content_width == 0 && visual_height == 0 && inline_sum == 0) {
        span->width = 0;
        span->height = 0;
        return;
    }
    // CSS 2.1 §10.6.1: For inline non-replaced elements, vertical borders/padding
    float parent_border_top_y = content_min_y - roundf(edges.top);
    float parent_border_bottom_y = content_max_y + roundf(edges.bottom);
    float final_min_y = span->has_split_inline_fragment_union()
        ? min(visual_min_y, parent_border_top_y) : parent_border_top_y;
    float final_max_y = span->has_split_inline_fragment_union()
        ? max(visual_max_y, parent_border_bottom_y) : parent_border_bottom_y;
    if (span->has_split_inline_fragment_union() && !span_has_direct_visible_text(span)) {
        if (span->ensure_fragment_union(FRAGMENT_UNION_SPLIT_INLINE)->min_x < min_x) min_x = span->ensure_fragment_union(FRAGMENT_UNION_SPLIT_INLINE)->min_x;
        if (span->ensure_fragment_union(FRAGMENT_UNION_SPLIT_INLINE)->max_x > max_x) max_x = span->ensure_fragment_union(FRAGMENT_UNION_SPLIT_INLINE)->max_x;
        content_width = max_x - min_x;
        final_min_y = span->ensure_fragment_union(FRAGMENT_UNION_SPLIT_INLINE)->min_y - roundf(edges.top);
        final_max_y = span->ensure_fragment_union(FRAGMENT_UNION_SPLIT_INLINE)->max_y + roundf(edges.bottom);
        // CSS 2.1 §9.2.1.1: split inline boxes expose the union of their own
        span->x = min_x;
        span->y = final_min_y;
        span->width = content_width;
        span->height = final_max_y - final_min_y;
        return;
    }
    if (span->has_inline_fragment_union() && span_has_direct_visible_text(span)) {
        if (span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->min_y < final_min_y) final_min_y = span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->min_y;
        if (span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->max_y > final_max_y) final_max_y = span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->max_y;
        if (span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->min_x < min_x) min_x = span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->min_x;
        if (span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->max_x > max_x) max_x = span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->max_x;
        content_width = max_x - min_x;
    }
    // CSS 2.1 §8.5.1: Inline elements' border/padding appear at the start and end
    // first line fragment and right on the last — the union bounding box cannot simply
    if (is_multi_line && !clone_forced_break) {
        span->x = min_x;
        span->y = final_min_y;
        span->width = content_width;
        span->height = final_max_y - final_min_y;
    } else {
        span->x = min_x - left_edge;
        span->y = final_min_y;
        span->width = content_width + left_edge + right_edge;
        span->height = final_max_y - final_min_y;
    }
    layout_apply_simple_ruby_column_geometry(span);
}

static bool span_has_vertical_decoration_descendant(ViewSpan* span) {
    for (View* child = span ? span->first_child : nullptr; child; child = child->next()) {
        ViewSpan* child_span = lam::view_as<RDT_VIEW_INLINE>(child);
        if (!child_span) continue;
        LayoutInlineDecorationEdges edges = layout_inline_decoration_edges(child_span);
        float top_edge = roundf(edges.top);
        float bottom_edge = roundf(edges.bottom);
        if (top_edge > 0.0f || bottom_edge > 0.0f ||
            span_has_vertical_decoration_descendant(child_span)) {
            return true;
        }
    }
    return false;
}

void recompute_span_bounding_box_after_line_layout(
        ViewSpan* span, bool is_multi_line, struct FontHandle* fallback_fh) {
    bool collapsed_no_content = span && span->has_collapsed_line_fragment_union() &&
        layout_span_children_have_no_line_content(span);
    float collapsed_y = collapsed_no_content
        ? span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->min_y -
            roundf(layout_inline_decoration_edges(span).top)
        : 0.0f;
    float finalized_y = span->y;
    float finalized_height = span->height;
    compute_span_bounding_box(span, is_multi_line, fallback_fh);
    if (collapsed_no_content &&
        fabsf(finalized_y - collapsed_y) > max(span->content_height, 1.0f)) {
        // CSS Inline 3: a collapsed inline fragment must not retain a later
        // line's y-origin after descendant bounds are recomputed.
        span->y = collapsed_y;
    } else if (!is_multi_line && !span_has_vertical_decoration_descendant(span)) {
        // CSS 2.1 §10.6.1: descendant font content may protrude without
        span->y = finalized_y;
        span->height = finalized_height;
    }
}
// split inline flow around block-level children per CSS 2.1 §9.2.1.1.
void layout_inline_with_block_children(LayoutContext* lycon, DomElement* inline_elem,
                                        ViewSpan* span, DomNode* first_child,
                                        float inline_start_edge, float span_line_height) {

    Linebox saved_line = lycon->line;
    FontBox saved_font = lycon->font;
    CssEnum saved_vertical_align = lycon->line.vertical_align;
    float first_line_text_indent = saved_line.text_indent_offset;
    if (first_line_text_indent == 0.0f && lycon->block.line_number == 0) {
        first_line_text_indent = lycon->block.text_indent;
    }

    DomNode* child = first_child;
    bool in_inline_sequence = false;
    bool start_edge_available = inline_start_edge != 0.0f;
    bool start_edge_pending_active = false;
    float start_edge_pending_base = 0.0f;
    bool had_block_child = false;
    bool visible_inline_after_last_block = false;
    bool visible_inline_before_first_block = false;
    bool visible_inline_in_sequence = false;
    bool had_block_child_before = false;  // tracks if a block was laid out before the current one
    DomElement* last_block_child_elem = nullptr;  // last block child for bottom margin collapse

    while (child) {
        DisplayValue child_display = child->is_element() ?
            resolve_display_value(child) : DisplayValue{CSS_VALUE_INLINE, CSS_VALUE_FLOW};
        // CSS 2.1 §9.2.1.1 and §17.2.1: Block children and orphaned table-internal children
        // CSS 2.1 §9.5: Floats are out of flow and should not break the inline
        bool child_is_float = false;
        // CSS 2.1 §9.6.1: Absolutely positioned elements are out of flow and
        bool child_is_abspos = false;
        if (child->is_element()) {
            DomElement* ce = lam::dom_as<DOM_NODE_ELEMENT>(child);
            InlineOutOfFlowKind kind = inline_out_of_flow_kind(ce);
            child_is_float = kind.floated;
            child_is_abspos = kind.positioned;
        }
        bool is_block_or_table_internal = child->is_element() && !child_is_float && !child_is_abspos &&
            (child_display.outer == CSS_VALUE_BLOCK ||
             child_display.outer == CSS_VALUE_LIST_ITEM ||
             child_display.outer == CSS_VALUE_TABLE ||
             is_table_internal_display(child_display.inner) ||
             is_table_internal_display(child_display.outer));

        if (is_block_or_table_internal) {
            // CSS 2.1 §9.2.1.1: Leading anonymous block strut.
            if (!had_block_child && (!in_inline_sequence || lycon->line.is_line_start) && span->bound) {
                bool is_rtl = lycon->block.direction == CSS_VALUE_RTL;
                if (inline_has_axis_edge_decoration(span, is_rtl, true)) {
                    float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 18.0f;
                    lycon->block.advance_y += line_height;
                }
            }
            had_block_child = true;

            if (in_inline_sequence) {
                bool sequence_has_start_edge = start_edge_pending_active;
                if (start_edge_pending_active &&
                    lycon->line.inline_start_edge_pending > start_edge_pending_base) {
                    lycon->line.inline_start_edge_pending = start_edge_pending_base;
                }
                if (start_edge_pending_active && lycon->line.is_line_start) {
                    lycon->line.advance_x = saved_line.advance_x;
                }
                start_edge_pending_active = false;
                if (!lycon->line.is_line_start &&
                    (visible_inline_in_sequence || sequence_has_start_edge)) {
                    span_record_current_split_line_fragment(lycon, span, span_line_height);
                    line_break(lycon);
                }
                in_inline_sequence = false;
                visible_inline_in_sequence = false;
            }
            // IMPORTANT: Save/restore max_width because block layout will set it to container width,
            float saved_max_width = lycon->block.max_width;
            layout_block(lycon, child, child_display);
            visible_inline_after_last_block = false;
            lycon->block.max_width = saved_max_width; // Restore inline content width
            lycon->line.inline_start_edge_pending = 0.0f;
            if (lycon->line.is_line_start) {
                lycon->line.advance_x = lycon->line.left;
            }
            // CSS 2.1 §9.2.1.1 + §8.3.1: Parent-child margin collapse for
            if (child->is_element()) {
                DomElement* child_elem = lam::dom_as<DOM_NODE_ELEMENT>(child);
                ViewBlock* child_block = child_elem ? lam::view_as_block(child_elem) : nullptr;
                DomNode* container_node = inline_elem->parent;
                if (child_block && child_block->bound && container_node &&
                    container_node->is_element()) {
                    ViewBlock* container = lam::view_as_block(container_node);
                    if (container && !block_context_establishes_bfc(container)) {
                        float cont_bt = container->bound && container->boundary_mut()->border
                            ? container->boundary()->border->width.top : 0;
                        float cont_pt = container->bound ? container->boundary()->padding.top : 0;
                        float inline_bt = span->bound && span->boundary_mut()->border
                            ? span->boundary()->border->width.top : 0;
                        float inline_pt = span->bound ? span->boundary()->padding.top : 0;

                        if (!had_block_child_before && !visible_inline_before_first_block &&
                            cont_bt == 0 && cont_pt == 0 &&
                            inline_bt == 0 && inline_pt == 0 &&
                            child_block->boundary()->margin.top != 0) {
                            float child_mt = child_block->boundary()->margin.top;
                            float cont_mt = container->bound ? container->boundary()->margin.top : 0;
                            float collapsed = (child_mt >= 0 && cont_mt >= 0) ?
                                (child_mt > cont_mt ? child_mt : cont_mt) :
                                (child_mt < 0 && cont_mt < 0) ?
                                (child_mt < cont_mt ? child_mt : cont_mt) :
                                child_mt + cont_mt;
                            float y_delta = collapsed - cont_mt;
                            container->y += y_delta;
                            if (!container->bound) {
                                container->ensure_boundary(lycon);
                            }
                            container->boundary_mut()->margin.top = collapsed;
                            child_block->y -= child_mt;
                            child_block->boundary_mut()->margin.top = 0;
                            lycon->block.advance_y -= child_mt;
                        }
                    }
                }
            }

            View* block_fragment = static_cast<View*>(child);
            if (ViewBlock* fragment_block = lam::view_as_block(block_fragment); (!fragment_block || !layout_block_is_self_collapsing(fragment_block)) && (block_fragment->width > 0.0f || block_fragment->height > 0.0f)) {
                float relative_x = 0.0f, relative_y = 0.0f;
                layout_relative_position_offset(fragment_block, &relative_x, &relative_y);
                float split_left = lycon->line.left;
                float split_right = lycon->line.right;
                if (first_line_text_indent != 0.0f) {
                    // CSS 2.1 §16.1: an RTL first-line indent narrows the
                    // block-in-inline fragment from its inline-start edge.
                    if (lycon->block.direction == CSS_VALUE_RTL) {
                        split_left += first_line_text_indent;
                    } else {
                        split_right -= first_line_text_indent;
                    }
                }
                span_record_split_inline_fragment(
                    span, split_left, split_right,
                    block_fragment->y - relative_y,
                    block_fragment->y - relative_y + block_fragment->height);
            }
            had_block_child_before = true;
            if (child->is_element()) last_block_child_elem = lam::dom_as<DOM_NODE_ELEMENT>(child);

        } else {
            if (!in_inline_sequence) {
                in_inline_sequence = true;
                visible_inline_in_sequence = false;
                // IMPORTANT: Don't restore advance_x - let it continue from current position
                float current_advance_x = lycon->line.advance_x;
                lycon->line = saved_line;
                lycon->line.advance_x = current_advance_x; // Preserve current X position
                if (start_edge_available && !had_block_child) {
                    start_edge_pending_base = lycon->line.inline_start_edge_pending;
                    lycon->line.inline_start_edge_pending += inline_start_edge;
                    lycon->line.advance_x += inline_start_edge;
                    start_edge_pending_active = true;
                    start_edge_available = false;
                }
                lycon->line.is_line_start = (current_advance_x == lycon->line.left);
                lycon->font = saved_font;
                lycon->line.vertical_align = saved_vertical_align;
                update_line_for_bfc_floats(lycon);

            }

            layout_flow_node(lycon, child);
            bool child_has_line_content = view_has_non_trailing_line_content(
                static_cast<View*>(child), span);
            if (child_has_line_content) visible_inline_in_sequence = true;
            if (!had_block_child && child_has_line_content) {
                visible_inline_before_first_block = true;
            }
            if (had_block_child && child_has_line_content) {
                visible_inline_after_last_block = true;
            }
        }

        child = child->next_sibling;
    }

    if (in_inline_sequence && !lycon->line.is_line_start &&
        (visible_inline_in_sequence || start_edge_pending_active)) {
        span_record_current_split_line_fragment(lycon, span, span_line_height);
    }
    // CSS 2.1 §9.2.1.1 + §8.3.1: Bottom margin collapse for block-in-inline.
    if (last_block_child_elem && !visible_inline_after_last_block &&
        !has_following_content(inline_elem, false) &&
        (!in_inline_sequence || lycon->line.is_line_start)) {
        ViewBlock* last_blk = lam::view_as_block(last_block_child_elem);
        DomNode* container_node = inline_elem->parent;
        if (last_blk && last_blk->bound && container_node &&
            container_node->is_element()) {
            ViewBlock* container = lam::view_as_block(container_node);
            if (container && !block_context_establishes_bfc(container)) {
                float cont_bb = container->bound && container->boundary_mut()->border
                    ? container->boundary()->border->width.bottom : 0;
                float cont_pb = container->bound ? container->boundary()->padding.bottom : 0;
                float inline_bb = span->bound && span->boundary_mut()->border
                    ? span->boundary()->border->width.bottom : 0;
                float inline_pb = span->bound ? span->boundary()->padding.bottom : 0;
                bool cont_auto_height = !container->blk || container->block()->given_height < 0;
                if (cont_bb == 0 && cont_pb == 0 && inline_bb == 0 && inline_pb == 0 &&
                    cont_auto_height && last_blk->boundary_mut()->margin.bottom != 0) {
                    // the split block can reach the container edge only when no later
                    float child_mb = last_blk->boundary()->margin.bottom;
                    float cont_mb = container->bound ? container->boundary()->margin.bottom : 0;
                    float collapsed = (child_mb >= 0 && cont_mb >= 0) ?
                        (child_mb > cont_mb ? child_mb : cont_mb) :
                        (child_mb < 0 && cont_mb < 0) ?
                        (child_mb < cont_mb ? child_mb : cont_mb) :
                        child_mb + cont_mb;
                    if (!container->bound) {
                        container->ensure_boundary(lycon);
                    }
                    container->boundary_mut()->margin.bottom = collapsed;
                    lycon->block.advance_y -= child_mb;
                    last_blk->boundary_mut()->margin.bottom = 0;
                }
            }
        }
    }
    // CSS 2.1 §9.2.1.1: When an inline element with border/padding is split by
    // Per CSS 2.1 §9.4.2, a line box is non-zero-height when it contains an inline
    if (!visible_inline_after_last_block && (!in_inline_sequence || lycon->line.is_line_start) &&
        !has_following_content(inline_elem, true)) {
        bool has_inline_end_decoration = inline_has_axis_edge_decoration(
            span, lycon->block.direction == CSS_VALUE_RTL, false);
        if (has_inline_end_decoration) {
            float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 18.0f;
            lycon->block.advance_y += line_height;
        }
    }

}

void layout_inline(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {

    if (elmt->tag() == MARKUP_NAME_WBR) {
        ViewSpan* wbr_span = lam::view_require<RDT_VIEW_INLINE>(set_view(lycon, RDT_VIEW_INLINE, elmt));
        wbr_span->x = 0;
        wbr_span->y = 0;
        wbr_span->width = 0;
        wbr_span->height = 0;
        lycon->line.last_space = (unsigned char*)elmt;
        lycon->line.last_space_pos = lycon->line.advance_x;
        lycon->line.last_space_kind = BRK_ZERO_WIDTH_BREAK;
        lycon->line.trailing_space_width = 0;
        lycon->line.wrap_opportunity_before_nowrap = true;
        return;
    }

    if (elmt->tag() == MARKUP_NAME_BR) {
        View* br_view = set_view(lycon, RDT_VIEW_BR, elmt);
        if (lycon->block.direction == CSS_VALUE_RTL) {
            br_view->x = lycon->line.left;
        } else {
            br_view->x = lycon->line.advance_x - lycon->line.trailing_space_width;
        }
        br_view->width = 0;
        struct FontHandle* br_fh = lycon->font.font_handle;
        float br_font_height = br_fh ? font_get_cell_height(br_fh) : lycon->block.line_height;
        ViewBlock* br_parent = layout_nearest_block_ancestor(br_view->parent_view());
        bool vertical_parent = br_parent && layout_block_inline_axis_is_vertical(br_parent);
        br_view->height = br_font_height;
        // CSS 2.1 §10.8.1: <br> participates in the current line before forcing
        float br_line_height = lycon->block.line_height > 0.0f ? lycon->block.line_height : br_font_height;
        br_view->y = lycon->block.advance_y + (br_line_height - br_font_height) / 2.0f;
        bool was_line_clamped = lycon->block.line_clamped;
        bool line_had_replaced_content = lycon->line.has_replaced_content;
        bool collapse_br_rect = quirks_br_after_nested_inline_text(lycon, elmt);
        float collapsed_br_y = lycon->block.advance_y + lycon->line.max_ascender;
        // CSS Text 3 §7.2: text-align-last applies to lines immediately before
        // a forced line break. <br> is a forced break per CSS Text 3 §4.1.
        lycon->line.is_last_line = true;
        line_break(lycon);
        lycon->line.is_last_line = false;
        if (collapse_br_rect) {
            br_view->y = collapsed_br_y;
            br_view->height = 0.0f;
        }
        if (!vertical_parent && !was_line_clamped &&
            lycon->block.line_clamped && !line_had_replaced_content &&
            lycon->font.font_handle) {
            // ellipsis line width only when text supplies that line; an
            GlyphInfo ellipsis = font_get_glyph(lycon->font.font_handle, 0x2026);
            br_view->width = ellipsis.id != 0
                ? ellipsis.advance_x : lycon->font.current_font_size * 0.5f;
        }
        // CSS 2.1 §9.5.2: check if the <br> has a 'clear' property and apply float clearance.
        if (elmt->is_element()) {
            DomElement* br_elem = lam::dom_as<DOM_NODE_ELEMENT>(elmt);
            CssEnum clear_value = CSS_VALUE_NONE;
            if (br_elem && br_elem->specified_style && br_elem->specified_style->tree) {
                AvlNode* clear_node = avl_tree_search(br_elem->specified_style->tree, CSS_PROPERTY_CLEAR);
                if (clear_node) {
                    StyleNode* sn = (StyleNode*)clear_node->declaration;
                    if (sn && sn->winning_decl && sn->winning_decl->value &&
                        sn->winning_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                        clear_value = sn->winning_decl->value->data.keyword;
                    }
                }
            }
            if (clear_value == CSS_VALUE_LEFT || clear_value == CSS_VALUE_RIGHT ||
                clear_value == CSS_VALUE_BOTH) {
                BlockContext* bfc = block_context_find_bfc(&lycon->block);
                if (bfc) {
                    float current_bfc_y = lycon->block.advance_y + lycon->block.bfc_offset_y;
                    float clear_y = 0;
                    if (clear_value == CSS_VALUE_LEFT || clear_value == CSS_VALUE_BOTH) {
                        for (FloatBox* fb = bfc->left_floats; fb; fb = fb->next) {
                            if (fb->margin_box_top <= current_bfc_y && fb->margin_box_bottom > clear_y)
                                clear_y = fb->margin_box_bottom;
                        }
                    }
                    if (clear_value == CSS_VALUE_RIGHT || clear_value == CSS_VALUE_BOTH) {
                        for (FloatBox* fb = bfc->right_floats; fb; fb = fb->next) {
                            if (fb->margin_box_top <= current_bfc_y && fb->margin_box_bottom > clear_y)
                                clear_y = fb->margin_box_bottom;
                        }
                    }
                    float local_clear_y = clear_y - lycon->block.bfc_offset_y;
                    if (local_clear_y > lycon->block.saved_clear_y) {
                        lycon->block.saved_clear_y = local_clear_y;
                    }
                    if (local_clear_y > lycon->block.advance_y) {
                        lycon->block.advance_y = local_clear_y;
                        // CSS 2.1 §9.5.2: After clearing, re-adjust the line's effective
                        lycon->line.effective_left = lycon->line.left;
                        lycon->line.effective_right = lycon->line.right;
                        lycon->line.has_float_intrusion = false;
                        lycon->line.advance_x = lycon->line.left + lycon->line.inline_start_edge_pending;
                        adjust_line_for_floats(lycon);
                    }
                }
            }
        }
        return;
    }

    int inline_start_line_number = lycon->block.line_number;

    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // unresolved yet
    CssEnum pa_direction = lycon->block.direction;
    CssEnum pa_line_align = lycon->line.vertical_align;
    float pa_valign_offset = lycon->line.vertical_align_offset;
    float saved_parent_ascender = lycon->line.parent_font_ascender;
    float saved_parent_descender = lycon->line.parent_font_descender;
    float saved_parent_font_size = lycon->line.parent_font_size;
    struct FontHandle* saved_parent_font_handle = lycon->line.parent_font_handle;
    float pa_line_height = lycon->block.line_height;
    bool pa_line_height_is_normal = lycon->block.line_height_is_normal;
    lycon->elmt = elmt;

    DomElement* elmt_elem = elmt->is_element() ? lam::dom_as<DOM_NODE_ELEMENT>(elmt) : nullptr;
    if (elmt_elem && layout_noscript_content_suppressed(elmt_elem)) {
        // scripting-enabled noscript keeps its DOM text for script APIs, but
        // the HTML element represents nothing and must not affect line height.
        View* saved_line_start = lycon->line.start_view;
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(set_view(
            lycon, RDT_VIEW_INLINE, elmt));
        // A non-rendered HTML element has the same zero-origin geometry as
        // display:none in DOMRect APIs, even though its computed display is inline.
        span->x = 0.0f;
        span->y = 0.0f;
        span->width = 0.0f;
        span->height = 0.0f;
        span->content_height = 0.0f;
        span->display = display;
        dom_node_resolve_style(elmt, lycon);
        span->display = display;
        elmt_elem->display = display;
        lycon->line.start_view = saved_line_start;
        lycon->font = pa_font;
        lycon->block.direction = pa_direction;
        lycon->line.vertical_align = pa_line_align;
        lycon->line.vertical_align_offset = pa_valign_offset;
        lycon->line.parent_font_ascender = saved_parent_ascender;
        lycon->line.parent_font_descender = saved_parent_descender;
        lycon->line.parent_font_size = saved_parent_font_size;
        lycon->line.parent_font_handle = saved_parent_font_handle;
        lycon->block.line_height = pa_line_height;
        lycon->block.line_height_is_normal = pa_line_height_is_normal;
        return;
    }

    if (lycon->line.is_line_start) {
        // A preceding float on an otherwise empty line does not trigger
        update_line_for_bfc_floats(lycon);
    }

    ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(set_view(lycon, RDT_VIEW_INLINE, elmt));
    span->x = lycon->line.advance_x;  span->y = lycon->block.advance_y;
    span->width = 0;  span->height = 0;
    span->display = display;
    // CSS Position 3 §4.1: preserve the first inline box origin separately
    // because the visible DOM rectangle may start on a later line fragment.
    FragmentUnion* inline_cb = span->ensure_fragment_union(FRAGMENT_UNION_INLINE_CB);
    if (inline_cb) {
        float inline_cb_x = span->x;
        // CSS Text 3 §4.1: collapsible whitespace at a line's inline end does
        // not belong to the first generated inline box's containing-block edge.
        if (!lycon->line.is_line_start && lycon->line.trailing_space_width > 0.0f) {
            inline_cb_x = max(lycon->line.left,
                              inline_cb_x - lycon->line.trailing_space_width);
        }
        inline_cb->min_x = inline_cb->max_x = inline_cb_x;
        inline_cb->min_y = inline_cb->max_y = span->y;
        span->set_has_fragment_union(FRAGMENT_UNION_INLINE_CB, true);
    }

    dom_node_resolve_style(elmt, lycon);
    elmt_elem = lam::dom_as<DOM_NODE_ELEMENT>(elmt);
    if (elmt_elem && span->blk && span->block()->unicode_bidi == CSS_VALUE_PLAINTEXT) {
        // CSS Writing Modes §2.2: an inline plaintext box establishes its own
        // paragraph direction while its children are laid out.
        lycon->block.direction = layout_resolve_plaintext_direction(
            elmt_elem, pa_direction);
    }
    // CSS Counter handling (CSS 2.1 Section 12.4, CSS Lists 3)
    bool pushed_counter_scope = false;
    bool is_before_pseudo = elmt_elem && elmt_elem->tag_name &&
        strcmp(elmt_elem->tag_name, "::before") == 0;
    bool is_after_pseudo = elmt_elem && elmt_elem->tag_name &&
        strcmp(elmt_elem->tag_name, "::after") == 0;
    if (lycon->counter_context) {
        counter_push_scope(lycon->counter_context, is_before_pseudo || is_after_pseudo);
        pushed_counter_scope = true;
    }

    if (lycon->counter_context && (is_before_pseudo || is_after_pseudo)) {
        if (elmt_elem) {
            layout_update_pseudo_content_with_counters(lycon, elmt_elem);
        }
    } else if (lycon->counter_context && span->blk) {
        ViewBlock* block_api_span = lam::unsafe_view_block_api_span(span);
        // CSS Lists 3 §4.4: an inline list container still establishes the
        // implicit list-item counter scope; outer display does not remove it.
        setup_list_container_counters(lycon, block_api_span, elmt_elem);
        if (elmt_elem) {
            compute_reversed_counter_initial(lycon, elmt_elem);
        }
        if (span->block()->counter_reset) {
            counter_reset(lycon->counter_context, span->block()->counter_reset);
        }

        if (span->block()->counter_increment) {
            counter_increment(lycon->counter_context, span->block()->counter_increment);
        }

        if (span->block()->counter_set) {
            counter_set(lycon->counter_context, span->block()->counter_set);
        }

        if (display.list_item) {
            process_list_item(lycon, block_api_span, elmt, elmt_elem, display);
        }
    }

    if (elmt->is_element()) {
        ViewBlock* block_api_span = lam::unsafe_view_block_api_span(span);
        layout_materialize_pseudo_content(lycon, block_api_span, display.list_item, false);
    }

    if (pa_font.style) {
        lycon->line.parent_font_ascender = pa_font.style->ascender;
        lycon->line.parent_font_descender = pa_font.style->descender;
        lycon->line.parent_font_size = pa_font.current_font_size > 0.0f
            ? pa_font.current_font_size : pa_font.style->font_size;
        lycon->line.parent_font_handle = pa_font.font_handle;
    }
    if (span->font) {
        span->font->used_zoom = layout_effective_zoom((View*)span);
        setup_font(lycon->ui_context, &lycon->font,  span->font);
    }
    if (span->in_line && span->inl()->vertical_align) {
        lycon->line.vertical_align = span->inl()->vertical_align;
        lycon->line.vertical_align_offset = span->inl()->vertical_align_offset;
    }
    // CSS 2.1 §10.8.1: Each inline box uses its own 'line-height' property for
    InitialLetterInfo initial_letter = {};
    bool is_initial_letter = elmt->is_element() &&
        layout_get_initial_letter_info(lam::dom_as<DOM_NODE_ELEMENT>(elmt), &initial_letter);
    // CSS 2.1: <number> line-heights inherit the number, not the computed length,
    bool has_own_line_height = false;
    if (elmt->is_element()) {
        DomElement* dom_elmt = lam::dom_as<DOM_NODE_ELEMENT>(elmt);
        if (dom_elmt->specified_style) {
            has_own_line_height =
                style_tree_get_declaration(dom_elmt->specified_style, CSS_PROPERTY_LINE_HEIGHT) != nullptr ||
                style_tree_get_declaration(dom_elmt->specified_style, CSS_PROPERTY_FONT) != nullptr;
        }
    }
    // <number> or 'normal'. CSS 2.1: number line-heights inherit the number (not
    bool font_size_changed = lycon->font.style && pa_font.style &&
        lycon->font.style->font_size != pa_font.style->font_size;
    if (!is_initial_letter && (has_own_line_height || font_size_changed)) {
        ViewBlock* block_api_span = lam::unsafe_view_block_api_span(span);
        if (has_own_line_height) {
            setup_line_height(lycon, block_api_span);
        } else {
            CssValue inherited_lh = inherit_line_height(lycon, block_api_span);
            if (pa_line_height_is_normal ||
                inherited_lh.type == CSS_VALUE_TYPE_NUMBER ||
                (inherited_lh.type == CSS_VALUE_TYPE_KEYWORD &&
                 inherited_lh.data.keyword == CSS_VALUE_NORMAL)) {
                setup_line_height(lycon, block_api_span);
            }
        }
        if (lycon->block.line_height > pa_line_height) {
            lycon->line.has_expanded_inline_lh = true;
            CssEnum span_valign = span->in_line && span->inl()->vertical_align ?
                span->inl()->vertical_align : CSS_VALUE_BASELINE;
            float span_valign_offset = span->in_line ?
                span->inl()->vertical_align_offset : 0.0f;
            if (span_valign == CSS_VALUE_BASELINE && span_valign_offset == 0.0f) {
                lycon->line.max_inline_line_height = max(
                    lycon->line.max_inline_line_height, lycon->block.line_height);
            }
        }
    }
    float span_resolved_line_height = lycon->block.line_height;
    // line.max_ascender and max_descender to be changed only when there's output from the span

    DomNode *child = nullptr;
    if (elmt->is_element()) {
        child = elmt_elem ? layout_render_child_list(elmt_elem) : nullptr;
    }
    // CSS 2.1 §8.3: Inline elements' margin/border/padding push content inward.
    float inline_left_edge = 0;
    float inline_right_edge = layout_inline_end_edge(span);
    if (span->bound) {
        // CSS Writing Modes maps inline decorations to physical top/bottom
        bool rtl = lycon->block.direction == CSS_VALUE_RTL;
        inline_left_edge = inline_span_edge_extent(span, rtl, true, true);
        inline_right_edge = inline_span_edge_extent(span, rtl, false, true);
    }
    bool span_is_unbreakable = span->blk &&
        (span->block()->white_space == CSS_VALUE_NOWRAP ||
         span->block()->white_space == CSS_VALUE_PRE);
    if (span_is_unbreakable && child && !lycon->line.is_line_start &&
        line_has_prior_flow_content(&lycon->line)) {
        float line_left = lycon->line.has_float_intrusion
            ? lycon->line.effective_left : lycon->line.left;
        float line_right = lycon->line.has_float_intrusion
            ? lycon->line.effective_right : lycon->line.right;
        LayoutInlineDecorationEdges intrinsic_edges =
            layout_inline_decoration_edges(span);
        float unbreakable_content_width = calculate_max_content_width(lycon, elmt) -
            intrinsic_edges.left - intrinsic_edges.right;
        if (unbreakable_content_width < 0.0f) unbreakable_content_width = 0.0f;
        float unbreakable_width = unbreakable_content_width +
            inline_left_edge + inline_right_edge;
        if (line_right > line_left && unbreakable_content_width > 0.0f &&
            lycon->line.wrap_opportunity_before_nowrap &&
            lycon->line.advance_x + unbreakable_width > line_right) {
            // CSS Text: only break an unbreakable inline at a legal boundary;
            // zero-width inline-blocks and adjacent content must overflow.
            line_break(lycon);
            span->x = lycon->line.advance_x;
            span->y = lycon->block.advance_y;
            if (inline_cb) {
                inline_cb->min_x = inline_cb->max_x = span->x;
                inline_cb->min_y = inline_cb->max_y = span->y;
            }
        }
    }
    float saved_inline_pending = lycon->line.inline_start_edge_pending;
    // CSS 2.1 §9.2.1.1 and §17.2.1: Check for block-level and table-internal children
    bool has_block_children = false;
    bool has_table_internal = false;
    DomNode* scan = child;
    while (scan) {
        if (scan->is_element()) {
            DisplayValue child_display = resolve_display_value(scan);
            bool child_is_table_internal =
                is_table_internal_display(child_display.inner) ||
                is_table_internal_display(child_display.outer);
            if (child_is_table_internal) {
                has_table_internal = true;
            } else if (child_display.outer == CSS_VALUE_BLOCK ||
                child_display.outer == CSS_VALUE_LIST_ITEM ||
                child_display.outer == CSS_VALUE_TABLE) {
                // CSS 2.1 §9.5, §9.6.1: Absolutely/fixed positioned and floated
                DomElement* child_elem = lam::dom_as<DOM_NODE_ELEMENT>(scan);
                InlineOutOfFlowKind kind = inline_out_of_flow_kind(child_elem);
                bool child_is_out_of_flow = kind.floated || kind.positioned;
                if (!child_is_out_of_flow) {
                    has_block_children = true;
                }
            }
        }
        scan = scan->next_sibling;
    }
    // CSS 2.1 §17.2.1: When only table-internal children exist (no block children),
    if (has_table_internal && !has_block_children) {
        wrap_orphaned_table_children(lycon, elmt_elem);
        child = elmt_elem ? elmt_elem->first_child : nullptr;
    }

    if (has_block_children || (has_table_internal && has_block_children)) {

        float pre_split_advance_y = lycon->block.advance_y;
        float first_line_text_indent = lycon->line.text_indent_offset;
        if (first_line_text_indent == 0.0f && lycon->block.line_number == 0) {
            first_line_text_indent = lycon->block.text_indent;
        }
        layout_inline_with_block_children(
            lycon, elmt_elem, span, child, inline_left_edge, span_resolved_line_height);

        lycon->line.advance_x += inline_right_edge;
        // CSS 2.1 §9.2.1.1: When an inline element contains block-level children,
        compute_span_bounding_box(span, true, lycon->font.font_handle);  // get vertical bounds from children
        float containing_line_width = lycon->line.right - lycon->line.left;
        float measured_right = span->x + span->width;
        float span_line_left = lycon->line.left;
        if (lycon->block.direction == CSS_VALUE_RTL &&
            first_line_text_indent != 0.0f) {
            // CSS 2.1 §16.1: a block-in-inline fragment begins after the
            // RTL first-line indent, so its union must not include the indent.
            span_line_left += first_line_text_indent;
        }
        span->x = span_line_left;
        float measured_width = measured_right - span->x;
        if (measured_width < 0.0f) measured_width = 0.0f;
        bool has_direct_visible_text = span_has_direct_visible_text(span);
        bool direct_text_brackets_block =
            span_has_direct_text_on_both_sides_of_block(span);
        // CSS 2.1 §9.2.1.1: a split inline spans the line extent when direct
        // content brackets an in-flow block; an out-of-flow child must not
        // widen the inline containing block beyond its direct content.
        span->width = span->has_split_inline_fragment_union() &&
            (!has_direct_visible_text || direct_text_brackets_block)
            ? max(measured_width, containing_line_width) : measured_width;
        // CSS 2.1 §9.2.1.1: Extend span bounding box upward to cover the leading
        {
            bool has_inline_start = inline_has_axis_edge_decoration(
                span, lycon->block.direction == CSS_VALUE_RTL, true);
            if (has_inline_start) {
                float border_top = 0, pad_top = 0;
                if (span->bound) {
                    if (span->boundary()->border)
                        border_top = span->boundary()->border->width.top;
                    if (span->boundary()->padding.top > 0)
                        pad_top = span->boundary()->padding.top;
                }
                float strut_top = pre_split_advance_y - border_top - pad_top;
                if (strut_top < span->y) {
                    float old_y = span->y;
                    span->height += (old_y - strut_top);
                    span->y = strut_top;
                }
            }
        }
        // CSS 2.1 §9.2.1.1: Extend span bounding box to cover the trailing anonymous
        {
            bool has_inline_end = inline_has_axis_edge_decoration(
                span, lycon->block.direction == CSS_VALUE_RTL, false);
            if (has_inline_end) {
                float last_block_bottom = -1;
                View* scan_child = span->first_child;
                while (scan_child) {
                    if (lam::view_as<RDT_VIEW_BLOCK>(scan_child)) {
                        float child_bottom = scan_child->y + scan_child->height;
                        if (child_bottom > last_block_bottom)
                            last_block_bottom = child_bottom;
                    }
                    scan_child = scan_child->next();
                }

                if (last_block_bottom >= 0) {
                    float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 18.0f;
                    float border_bottom = 0, pad_bottom = 0;
                    if (span->bound) {
                        if (span->boundary()->border)
                            border_bottom = span->boundary()->border->width.bottom;
                        if (span->boundary()->padding.bottom > 0)
                            pad_bottom = span->boundary()->padding.bottom;
                    }
                    float trailing_extent = last_block_bottom + line_height + border_bottom + pad_bottom;
                    float span_bottom = span->y + span->height;
                    if (trailing_extent > span_bottom) {
                        span->height = trailing_extent - span->y;
                    }
                }
            }
        }

        record_block_in_inline_split_chain(span);

        if (span->position && span->positionp()->position == CSS_VALUE_RELATIVE) {
            layout_relative_positioned(lycon, lam::unsafe_view_block_api_span(span));
        } else if (span->position && span->positionp()->position == CSS_VALUE_STICKY) {
            layout_sticky_positioned(lycon, lam::unsafe_view_block_api_span(span));
        }

        lycon->font = pa_font;
        lycon->block.direction = pa_direction;
        lycon->line.vertical_align = pa_line_align;
        lycon->block.line_height = pa_line_height;
        lycon->block.line_height_is_normal = pa_line_height_is_normal;
        if (pushed_counter_scope) {
            counter_pop_scope_propagate(lycon->counter_context, true);
        }
        return;
    }

    bool had_children = (child != nullptr);
    bool has_inline_axis_decoration =
        inline_has_axis_edge_decoration(span, false, true, true) ||
        inline_has_axis_edge_decoration(span, false, false, true) ||
        // CSS 2.1 §8.3: negative margins cannot cancel an inline border or
        // padding when deciding whether the inline box contributes a line box.
        inline_has_axis_edge_decoration(span, false, true, false) ||
        inline_has_axis_edge_decoration(span, false, false, false);
    if (has_inline_axis_decoration && !lycon->line.start_view) {
        lycon->line.start_view = static_cast<View*>(span);
    }
    if (has_inline_axis_decoration) {
        lycon->line.is_line_start = false;
    }
    // CSS 2.1 §8.3: Track pending inline left edges for line break re-application.
    lycon->line.inline_start_edge_pending += inline_left_edge;
    lycon->line.advance_x += inline_left_edge;
    float inline_fragment_start_x = lycon->line.advance_x;
    float inline_fragment_start_y = lycon->block.advance_y;
    bool is_ruby_container = display.inner == CSS_VALUE_RUBY;
    if (is_ruby_container) {
        // annotation must not affect a later base-sized ruby.
        DomElementExt* ruby_ext = span->ensure_ext();
        if (ruby_ext) ruby_ext->has_simple_ruby_column_geometry = false;
        float available_ruby_start_overhang = lycon->line.has_space
            ? layout_measure_space_advance(lycon, lycon->font.font_handle,
                                           lycon->font.style)
            : 0.0f;
        bool has_simple_ruby_pair = ruby_has_simple_text_pair(child);
        for (DomNode* base_child = child; base_child;
             base_child = base_child->next_sibling) {
            if (!ruby_annotation_node(base_child)) {
                layout_flow_node(lycon, base_child);
            }
        }

        float base_start_x = span->x + inline_left_edge;
        float base_end_x = lycon->line.advance_x;
        compute_span_bounding_box(span, false, lycon->font.font_handle);
        float base_top_y = span->y;

        CssEnum ruby_position = span->inl()->ruby_position;
        float simple_ruby_inline_advance_extra = 0.0f;
        for (DomNode* annotation = child; annotation;
             annotation = annotation->next_sibling) {
            if (!ruby_annotation_node(annotation)) continue;

            BlockContext saved_base_block = lycon->block;
            Linebox saved_base_line = lycon->line;
            FontBox saved_base_font = lycon->font;
            DomNode* saved_base_element = lycon->elmt;

            begin_ruby_annotation_inline_context(lycon, &saved_base_line);
            layout_flow_node(lycon, annotation);
            Linebox annotation_line = lycon->line;

            ViewSpan* annotation_span = lam::view_as<RDT_VIEW_INLINE>(
                static_cast<View*>(annotation));
            if (annotation_span) {
                float column_width = base_end_x - base_start_x;
                stretch_simple_ruby_annotation_to_column(
                    annotation_span, column_width);
                float annotation_x = base_start_x +
                    (column_width - annotation_span->width) / 2.0f;
                layout_prepare_simple_ruby_column(
                    span, has_simple_ruby_pair, base_start_x, column_width,
                    annotation_span->width, available_ruby_start_overhang, &annotation_x,
                    &simple_ruby_inline_advance_extra);
                // direct <rt> declaration cannot reposition that container.
                float annotation_y = ruby_position == CSS_VALUE_UNDER
                    ? base_top_y + span->height
                    : base_top_y - annotation_span->height;
                layout_shift_view_tree(
                    static_cast<View*>(annotation_span),
                    annotation_x - annotation_span->x,
                    annotation_y - annotation_span->y);
            }
            // Annotation layout must not alter the base line's cursor or
            // persistent formatting context: it has its own inline context.
            lycon->block = saved_base_block;
            lycon->line = saved_base_line;
            lycon->font = saved_base_font;
            lycon->elmt = saved_base_element;
            lycon->line.advance_x += simple_ruby_inline_advance_extra;
            if (ruby_position == CSS_VALUE_UNDER) {
                if (!ruby_has_text_box_trim_ancestor(span, TEXT_BOX_TRIM_END)) {
                    contribute_under_ruby_annotation_line_height(
                        &lycon->line, &saved_base_block, annotation_span);
                }
            } else {
                merge_ruby_annotation_line_metrics(&lycon->line, &annotation_line);
                if (!ruby_has_text_box_trim_ancestor(span, TEXT_BOX_TRIM_START)) {
                    contribute_over_ruby_annotation_line_metrics(
                        &lycon->line, &saved_base_line, &saved_base_block,
                        annotation_span);
                }
            }
        }
    } else if (child) {
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling;
        } while (child);
    }
    float collapsed_inline_fragment_x = lycon->line.advance_x;
    // completed fragment and must not be applied to that fresh line.
    bool ended_at_new_line_start = lycon->block.line_number > inline_start_line_number &&
        lycon->line.is_line_start;
    if (!ended_at_new_line_start) {
        lycon->line.advance_x += inline_right_edge;
    }
    // CSS 2.1 §8.3: Now that this span is closing, remove its contribution from
    if (lycon->line.inline_start_edge_pending > saved_inline_pending) {
        lycon->line.inline_start_edge_pending = saved_inline_pending;
    }
    // CSS 2.1 §10.8.1: For non-replaced inline elements, the inline box height
    if (!had_children) {
        if (lycon->line.is_line_start && !lycon->line.has_phantom_inline_fragment) {
            // CSS 2.1 §16.2: phantom empty inlines retain an aligned static
            lycon->line.start_view = layout_inline_fragment_root(static_cast<View*>(span));
            lycon->line.has_phantom_inline_fragment = true;
        }
        contribute_inline_strut(lycon, elmt, span);
        // CSS 2.1 §9.4.2: An inline element with non-zero margins, borders, or
        // CSS Inline 3 §2.1: An inline element with ANY non-zero inline-axis
        if (has_inline_axis_decoration) {
            lycon->line.is_line_start = false;
        }
    } else if (has_inline_axis_decoration && layout_span_children_have_no_line_content(span)) {
        contribute_inline_strut(lycon, elmt, span);
        lycon->line.is_line_start = false;
    }
    // CSS 2.1 §9.2.1.1 and §17.2.1: table-internal children are block
    // fragments after table fix-up; preserve an inline list-item's fragment
    // geometry during the later line-height correction.
    bool preserve_inline_list_marker_fragment = span->display.list_item &&
        span->display.outer == CSS_VALUE_INLINE;
    bool has_block_fragment_child = layout_inline_span_has_in_flow_block_child(
        span, preserve_inline_list_marker_fragment);
    bool span_is_multi_line = inline_span_has_multiple_line_fragments(span) ||
        (preserve_inline_list_marker_fragment && has_block_fragment_child);
    // CSS 2.1 §16.6.1: Trailing whitespace at end of a line should not expand
    // span bounding box only when the span is the last inline content on the line
    float saved_trailing = 0;
    View* last_child_for_trim = nullptr;
    if (lycon->line.trailing_space_width > 0) {
        bool has_following_inline = has_following_content(span, true);

        if (!has_following_inline) {
            View* c = span->first_child;
            while (c) {
                if (c->view_type) last_child_for_trim = c;
                c = c->next();
            }
            if (last_child_for_trim && last_child_for_trim->view_type == RDT_VIEW_TEXT) {
                saved_trailing = lycon->line.trailing_space_width;
                last_child_for_trim->width -= saved_trailing;
            }
        }
    }
    // CSS 2.1 §10.6.1: Store span's resolved line-height for use by
    span->content_height = span_resolved_line_height;
    if (had_children && has_inline_axis_decoration && layout_span_children_have_no_line_content(span)) {
        // CSS Inline 3: a forced break inside a zero-content inline leaves its
        // start decoration on the preceding line, while following content starts
        // on the new line.
        float collapsed_fragment_x = ended_at_new_line_start
            ? inline_fragment_start_x : collapsed_inline_fragment_x;
        float collapsed_fragment_y = ended_at_new_line_start
            ? inline_fragment_start_y : lycon->block.advance_y;
        span->set_has_collapsed_line_fragment_union(true);
        span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->min_x = collapsed_fragment_x;
        span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->max_x = collapsed_fragment_x;
        span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->min_y = collapsed_fragment_y;
        span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->max_y = collapsed_fragment_y;
    }
    // CSS 2.1 §10.8.1: vertical-align applies to the inline box generated by
    if (span->in_line && span->inl()->vertical_align &&
        span->inl()->vertical_align != CSS_VALUE_BASELINE &&
        span->content_height > 0.0f) {
        float asc_contribution = 0.0f;
        float desc_contribution = 0.0f;
        CssEnum valign = span->inl()->vertical_align;
        if (valign == CSS_VALUE_MIDDLE) {
            float x_height_half = lycon->font.current_font_size * 0.25f;
            if (lycon->line.parent_font_handle) {
                float x_ratio = font_get_x_height_ratio(lycon->line.parent_font_handle);
                x_height_half = lycon->line.parent_font_size * x_ratio / 2.0f;
            }
            asc_contribution = span->content_height / 2.0f + x_height_half;
            desc_contribution = span->content_height / 2.0f - x_height_half;
        } else if (valign == CSS_VALUE_TOP) {
            lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, span->content_height);
            lycon->line.max_top_height = max(lycon->line.max_top_height, span->content_height);
        } else if (valign == CSS_VALUE_BOTTOM) {
            lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, span->content_height);
            lycon->line.max_bottom_height = max(lycon->line.max_bottom_height, span->content_height);
        } else {
            float span_asc = span->font ? span->fontp()->ascender :
                (lycon->font.style ? lycon->font.style->ascender : 0.0f);
            float span_desc = span->font ? span->fontp()->descender :
                (lycon->font.style ? lycon->font.style->descender : 0.0f);
            float content_area = span_asc + span_desc;
            float half_leading = (span->content_height - content_area) / 2.0f;
            asc_contribution = span_asc + half_leading;
            desc_contribution = span->content_height - asc_contribution;
            if (valign == CSS_VALUE_TEXT_TOP) {
                desc_contribution = span->content_height - lycon->line.parent_font_ascender;
                asc_contribution = lycon->line.parent_font_ascender;
            } else if (valign == CSS_VALUE_TEXT_BOTTOM) {
                asc_contribution = span->content_height - lycon->line.parent_font_descender;
                desc_contribution = lycon->line.parent_font_descender;
            }
            float baseline_shift = vertical_align_baseline_shift(
                lycon, valign, span->inl()->vertical_align_offset);
            asc_contribution += baseline_shift;
            desc_contribution -= baseline_shift;
        }
        if (asc_contribution > 0.0f) {
            lycon->line.max_ascender = max(lycon->line.max_ascender, asc_contribution);
        }
        if (desc_contribution > 0.0f) {
            lycon->line.max_descender = max(lycon->line.max_descender, desc_contribution);
        }
    }

    compute_span_bounding_box(span, span_is_multi_line, lycon->font.font_handle);
    if (!had_children && has_inline_axis_decoration) {
        float border_left = 0.0f;
        float padding_left = 0.0f;
        if (span->bound) {
            if (span->boundary()->border) {
                border_left = span->boundary()->border->width.left;
            }
            padding_left = span->boundary()->padding.left > 0.0f ? span->boundary()->padding.left : 0.0f;
        }
        span->x = collapsed_inline_fragment_x - border_left - padding_left;
    }
    if (span->width == 0.0f && span->height == 0.0f && had_children &&
        layout_span_children_have_no_line_content(span)) {
        float continuation_x = collapsed_inline_fragment_x;
        bool has_left_float = false;
        bool has_float = inline_span_float_continuation_x(
            span, &continuation_x, &has_left_float);
        span->x = continuation_x;
        span->y = lycon->block.advance_y;
        if (has_float && lycon->line.is_line_start &&
            !lycon->line.has_phantom_inline_fragment) {
            lycon->line.start_view = layout_inline_fragment_root(static_cast<View*>(span));
            lycon->line.has_phantom_inline_fragment = true;
        }
        if (has_float && lycon->line.is_line_start) {
            update_line_for_bfc_floats(lycon, lycon->block.line_height);
        }
    }
    // CSS 2.1 §10.6.1: For inline non-replaced elements, the bounding box height
    if (span->height > 0) {
        struct FontHandle* fh = span->font ? span->fontp()->font_handle : lycon->font.font_handle;
        if (fh) {
            float content_area = font_get_cell_height(fh);
            float bt = 0, bb = 0, pt_val = 0, pb_val = 0;
            if (span->bound) {
                if (span->boundary()->border) {
                    bt = span->boundary()->border->width.top;
                    bb = span->boundary()->border->width.bottom;
                }
                pt_val = span->boundary()->padding.top > 0 ? span->boundary()->padding.top : 0;
                pb_val = span->boundary()->padding.bottom > 0 ? span->boundary()->padding.bottom : 0;
            }
            float expected_height = content_area + bt + pt_val + pb_val + bb;
            if (!span_is_multi_line &&
                roundf(span->height) > roundf(expected_height)) {
                float span_asc = span->font ? span->fontp()->ascender :
                    (lycon->font.style ? lycon->font.style->ascender : 0.0f);
                float span_desc = span->font ? span->fontp()->descender :
                    (lycon->font.style ? lycon->font.style->descender : 0.0f);
                float baseline_pos = line_baseline_position(lycon, nullptr);
                span->y = layout_inline_font_box_y(
                    lycon, span, span_resolved_line_height,
                    span_asc, span_desc, baseline_pos, bt, pt_val);
                span->height = expected_height;
            }
            // CSS 2.1 §10.8.1: For empty inline elements with inline decorations
            if (!had_children && !lycon->block.line_height_is_normal) {
                float ascender = 0;
                if (span->font) {
                    ascender = span->fontp()->ascender;
                } else if (lycon->font.style) {
                    ascender = lycon->font.style->ascender;
                }
                if (ascender > 0 && lycon->block.line_height < content_area) {
                    span->y = lycon->block.advance_y + lycon->line.max_ascender - ascender - bt - pt_val;
                }
            }
        }
    }
    // CSS 2.1 §10.8.1: Mark collapsed-content inline spans for line-break fixup.
    // the span gets 0×0 from compute_span_bounding_box. However, per CSS 2.1, the
    // inline box still contributes its line-height to the line box only when
    if (span->height == 0 && had_children &&
        has_inline_axis_decoration) {
        if (layout_span_children_have_no_line_content(span)) {
            span->content_height = lycon->block.line_height;
            if (lycon->line.start_view || !lycon->line.is_line_start) {
                span->height = span->content_height;
            }
        }
    }
    if (last_child_for_trim && saved_trailing > 0) {
        last_child_for_trim->width += saved_trailing;
    }
    // CSS 2.1 §9.4.3: Relatively positioned inline elements are offset from their normal position
    if (span->position && span->positionp()->position == CSS_VALUE_RELATIVE) {
        layout_relative_positioned(lycon, lam::unsafe_view_block_api_span(span));
    } else if (span->position && span->positionp()->position == CSS_VALUE_STICKY) {
        layout_sticky_positioned(lycon, lam::unsafe_view_block_api_span(span));
    }

    lycon->font = pa_font;
    lycon->block.direction = pa_direction;
    lycon->line.vertical_align = pa_line_align;
    lycon->line.vertical_align_offset = pa_valign_offset;
    lycon->line.parent_font_ascender = saved_parent_ascender;
    lycon->line.parent_font_descender = saved_parent_descender;
    lycon->line.parent_font_size = saved_parent_font_size;
    lycon->line.parent_font_handle = saved_parent_font_handle;
    lycon->block.line_height = pa_line_height;
    lycon->block.line_height_is_normal = pa_line_height_is_normal;
    if (pushed_counter_scope) {
        counter_pop_scope_propagate(lycon->counter_context, true);
    }
}
