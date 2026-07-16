#include "layout.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/font/font.h"
#include "../lib/tagged.hpp"
#include "../lib/strbuf.h"
#include <cstring>

// Forward declarations from layout_block.cpp for pseudo-element handling
extern PseudoContentProp* alloc_pseudo_content_prop(LayoutContext* lycon, ViewBlock* block);
extern void generate_pseudo_element_content(LayoutContext* lycon, ViewBlock* block, bool is_before);
extern void insert_pseudo_into_dom(DomElement* parent, DomElement* pseudo, bool is_before);

static inline DomElement* layout_inline_as_element(DomNode* node) {
    return lam::dom_as<DOM_NODE_ELEMENT>(node);
}

static inline DomText* layout_inline_as_text(DomNode* node) {
    return lam::dom_as<DOM_NODE_TEXT>(node);
}

static inline ViewBlock* layout_inline_as_block_view(View* view) {
    if (!view) return nullptr;
    switch (view->view_type) {
        case RDT_VIEW_INLINE_BLOCK: return lam::view_as_block<RDT_VIEW_INLINE_BLOCK>(view);
        case RDT_VIEW_BLOCK: return lam::view_as_block<RDT_VIEW_BLOCK>(view);
        case RDT_VIEW_LIST_ITEM: return lam::view_as_block<RDT_VIEW_LIST_ITEM>(view);
        case RDT_VIEW_TABLE: return lam::view_as_block<RDT_VIEW_TABLE>(view);
        case RDT_VIEW_TABLE_ROW_GROUP: return lam::view_as_block<RDT_VIEW_TABLE_ROW_GROUP>(view);
        case RDT_VIEW_TABLE_ROW: return lam::view_as_block<RDT_VIEW_TABLE_ROW>(view);
        case RDT_VIEW_TABLE_CELL: return lam::view_as_block<RDT_VIEW_TABLE_CELL>(view);
        case RDT_VIEW_TABLE_COLUMN_GROUP: return lam::view_as_block<RDT_VIEW_TABLE_COLUMN_GROUP>(view);
        case RDT_VIEW_TABLE_COLUMN: return lam::view_as_block<RDT_VIEW_TABLE_COLUMN>(view);
        case RDT_VIEW_NONE:
        case RDT_VIEW_TEXT:
        case RDT_VIEW_BR:
        case RDT_VIEW_MARKER:
        case RDT_VIEW_INLINE:
            return nullptr;
    }
    return nullptr;
}

static inline ViewBlock* layout_inline_unsafe_block_api_span(ViewSpan* span) {
    return lam::unsafe_view_block_api_span(span);
}

// Check if a view child is out of normal flow (absolute, fixed, or float)
static inline bool is_out_of_flow_child(View* child) {
    return layout_view_is_out_of_flow(child);
}

static bool quirks_table_cell_br_after_nested_inline_text(LayoutContext* lycon,
                                                          DomNode* br_node) {
    if (!lycon || !lycon->doc || !lycon->doc->view_tree || !br_node) return false;
    if (!is_quirks_mode(lycon->doc->view_tree->html_version)) return false;
    ViewBlock* block = lycon->block.establishing_element;
    if (!block || block->view_type != RDT_VIEW_TABLE_CELL) return false;
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

static bool inline_has_axis_edge_decoration(ViewSpan* span, bool rtl, bool start_edge) {
    if (!span || !span->bound) return false;
    // Logical start is right only in RTL; logical end is right only in LTR.
    bool use_right = start_edge == rtl;
    float border = 0.0f;
    if (span->bound->border) {
        border = use_right ? span->bound->border->width.right : span->bound->border->width.left;
    }
    float padding = use_right ? span->bound->padding.right : span->bound->padding.left;
    return border > 0.0f || padding > 0.0f;
}

static bool text_is_all_collapsible_space(DomText* text, ViewSpan* span);

static bool span_has_direct_visible_text(ViewSpan* span) {
    for (View* child = span ? span->first_child : nullptr; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_TEXT &&
            child->width > 0.0f && child->height > 0.0f &&
            !text_is_all_collapsible_space(
                layout_inline_as_text(static_cast<DomNode*>(child)), span)) {
            return true;
        }
    }
    return false;
}

static bool text_is_all_collapsible_space(DomText* text, ViewSpan* span) {
    if (!text || !text->text || text->length == 0) return false;
    CssEnum white_space = span && span->blk ? span->blk->white_space : CSS_VALUE_NORMAL;
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
    if (!view || view->view_type == RDT_VIEW_NONE) return false;
    if (view->view_type == RDT_VIEW_TEXT) {
        return (view->width > 0.0f || view->height > 0.0f) &&
            !text_is_all_collapsible_space(
                layout_inline_as_text(static_cast<DomNode*>(view)), span);
    }
    if (view->view_type == RDT_VIEW_INLINE) {
        ViewSpan* child_span = lam::view_require<RDT_VIEW_INLINE>(view);
        for (View* child = child_span->first_child; child; child = child->next()) {
            if (view_has_non_trailing_line_content(child, span)) return true;
        }
        return false;
    }
    return view->width > 0.0f || view->height > 0.0f;
}

static bool text_has_non_whitespace_content(DomText* text) {
    if (!text || !text->text || text->length == 0) return false;
    for (size_t i = 0; i < text->length; i++) {
        char c = text->text[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
            return true;
        }
    }
    return false;
}

static bool has_following_inline_content(DomNode* node) {
    DomNode* current = node;
    while (current) {
        DomNode* sib = current->next_sibling;
        while (sib) {
            if (sib->is_text()) {
                if (text_has_non_whitespace_content(layout_inline_as_text(sib))) {
                    return true;
                }
            } else if (sib->is_element()) {
                DomElement* elem = layout_inline_as_element(sib);
                DisplayValue display = resolve_display_value(elem);
                return display.outer == CSS_VALUE_INLINE;
            }
            sib = sib->next_sibling;
        }

        DomNode* parent = current->parent;
        if (!parent || !parent->is_element()) break;
        DomElement* parent_elem = layout_inline_as_element(parent);
        if (!parent_elem || parent_elem->view_type != RDT_VIEW_INLINE) break;
        current = parent;
    }
    return false;
}

static bool has_following_in_flow_content(DomNode* node) {
    DomNode* current = node;
    while (current) {
        for (DomNode* sibling = current->next_sibling; sibling;
             sibling = sibling->next_sibling) {
            if (sibling->is_text()) {
                if (text_has_non_whitespace_content(layout_inline_as_text(sibling))) {
                    return true;
                }
                continue;
            }
            if (!sibling->is_element()) continue;

            DomElement* elem = layout_inline_as_element(sibling);
            DisplayValue display = resolve_display_value(elem);
            InlineOutOfFlowKind kind = inline_out_of_flow_kind(elem);
            if (display.outer != CSS_VALUE_NONE && !kind.floated && !kind.positioned) {
                return true;
            }
        }

        DomNode* parent = current->parent;
        if (!parent || !parent->is_element() ||
            parent->view_type != RDT_VIEW_INLINE) {
            break;
        }
        current = parent;
    }
    return false;
}

static bool view_is_collapsed_whitespace_text(View* view, ViewSpan* span) {
    if (!view || view->view_type != RDT_VIEW_TEXT) return false;
    if (view->width > 0.0f) return false;
    DomText* text = layout_inline_as_text(static_cast<DomNode*>(view));
    return text_is_all_collapsible_space(text, span);
}

static void inline_text_line_range(View* view, bool* found,
                                   int* first_line, int* last_line) {
    if (!view) return;
    if (view->view_type == RDT_VIEW_TEXT) {
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
        inline_text_line_range(child, found, first_line, last_line);
    }
}

static View* inline_span_first_line_fragment_child(ViewSpan* span) {
    if (!span) return nullptr;
    View* first = span->first_placed_child();
    while (first && (first->view_type == RDT_VIEW_NONE || is_out_of_flow_child(first))) {
        first = first->next();
    }
    return first;
}

bool inline_span_has_multiple_line_fragments(ViewSpan* span) {
    View* first = inline_span_first_line_fragment_child(span);
    if (!first) return false;

    bool found_text_line = false;
    int first_line = 0;
    int last_line = 0;
    inline_text_line_range(static_cast<View*>(span), &found_text_line,
                           &first_line, &last_line);
    // Vertical-align changes child y without changing its recorded line identity.
    if (found_text_line) return first_line != last_line;

    float first_y = first->y;
    for (View* child = first->next(); child; child = child->next()) {
        if (child->view_type == RDT_VIEW_NONE || is_out_of_flow_child(child)) continue;
        if (child->y != first_y) return true;
    }
    return false;
}

static bool span_has_inline_axis_decoration(ViewSpan* span) {
    if (!span || !span->bound) return false;
    if (span->bound->margin.left != 0.0f || span->bound->margin.right != 0.0f ||
        span->bound->padding.left != 0.0f || span->bound->padding.right != 0.0f) {
        return true;
    }
    return span->bound->border &&
        (span->bound->border->width.left != 0.0f ||
         span->bound->border->width.right != 0.0f);
}

static bool span_children_have_no_line_content(ViewSpan* span);

static bool view_has_line_content(View* view) {
    if (!view || view->view_type == RDT_VIEW_NONE || is_out_of_flow_child(view)) {
        return false;
    }
    if (view->view_type == RDT_VIEW_TEXT) {
        return view->width > 0.0f && view->height > 0.0f;
    }
    if (view->view_type == RDT_VIEW_INLINE) {
        return !span_children_have_no_line_content(lam::view_require<RDT_VIEW_INLINE>(view));
    }
    return true;
}

static bool span_children_have_no_line_content(ViewSpan* span) {
    if (!span) return false;
    if (!span->first_child) return true;
    for (View* child = span->first_child; child; child = child->next()) {
        if (view_has_line_content(child)) {
            return false;
        }
    }
    return true;
}

static bool span_left_float_continuation_x(ViewSpan* span, float* continuation_x) {
    if (!span || !continuation_x) return false;
    bool found_left_float = false;
    float max_right = *continuation_x;
    for (View* child = span->first_child; child; child = child->next()) {
        DomElement* child_elem = layout_inline_as_element(static_cast<DomNode*>(child));
        if (!child_elem || !child_elem->position ||
            child_elem->position->float_prop != CSS_VALUE_LEFT) {
            continue;
        }
        float dx = 0.0f;
        float dy = 0.0f;
        ViewBlock* child_block = layout_inline_as_block_view(child);
        if (child_block) {
            layout_relative_position_offset(child_block, &dx, &dy);
        }
        float margin_right = child_block && child_block->bound ?
            child_block->bound->margin.right : 0.0f;
        // css 2.1 section 9.5: a direct left float is out of flow, but it still
        // intrudes at the inline-start side of this line. The inline parent's
        // zero-width continuation is therefore after the float margin box.
        float static_right = child->x - dx + child->width + margin_right;
        if (!found_left_float || static_right > max_right) {
            max_right = static_right;
        }
        found_left_float = true;
    }
    if (found_left_float) {
        *continuation_x = max_right;
    }
    return found_left_float;
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
        log_debug("%s inline strut: ascender=%.1f, descender=%.1f, line_height=%.1f",
                  source ? source->source_loc() : "(inline)", ascender, descender,
                  lycon->block.line_height);
    }
    span->content_height = lycon->block.line_height;
}

static void span_record_ancestor_fragment(ViewSpan* span, View* fragment) {
    if (!span || !fragment) return;
    float min_x = fragment->x;
    float max_x = fragment->x + fragment->width;
    float min_y = fragment->y;
    float max_y = fragment->y + fragment->height;
    if (!span->has_ancestor_fragment_union) {
        span->has_ancestor_fragment_union = true;
        span->ancestor_fragment_min_x = min_x;
        span->ancestor_fragment_max_x = max_x;
        span->ancestor_fragment_min_y = min_y;
        span->ancestor_fragment_max_y = max_y;
    } else {
        if (min_x < span->ancestor_fragment_min_x) span->ancestor_fragment_min_x = min_x;
        if (max_x > span->ancestor_fragment_max_x) span->ancestor_fragment_max_x = max_x;
        if (min_y < span->ancestor_fragment_min_y) span->ancestor_fragment_min_y = min_y;
        if (max_y > span->ancestor_fragment_max_y) span->ancestor_fragment_max_y = max_y;
    }
}

static void span_record_split_inline_fragment(ViewSpan* span, float min_x, float max_x,
                                              float min_y, float max_y) {
    if (!span || max_x < min_x || max_y < min_y) return;
    if (!span->has_split_inline_fragment_union) {
        span->has_split_inline_fragment_union = true;
        span->split_inline_fragment_min_x = min_x;
        span->split_inline_fragment_max_x = max_x;
        span->split_inline_fragment_min_y = min_y;
        span->split_inline_fragment_max_y = max_y;
    } else {
        if (min_x < span->split_inline_fragment_min_x) span->split_inline_fragment_min_x = min_x;
        if (max_x > span->split_inline_fragment_max_x) span->split_inline_fragment_max_x = max_x;
        if (min_y < span->split_inline_fragment_min_y) span->split_inline_fragment_min_y = min_y;
        if (max_y > span->split_inline_fragment_max_y) span->split_inline_fragment_max_y = max_y;
    }
}

static void span_record_current_split_line_fragment(LayoutContext* lycon, ViewSpan* span,
                                                    float span_line_height) {
    if (!lycon || !span || lycon->line.is_line_start) return;
    FontProp* font = span->font ? span->font : lycon->font.style;
    FontHandle* font_handle = span->font ? span->font->font_handle : lycon->font.font_handle;
    if (!font || !font_handle || span_line_height <= 0.0f) return;

    float line_height = 0.0f;
    float baseline_pos = line_baseline_position(lycon, &line_height);
    float fragment_y = layout_inline_font_box_y(
        lycon, span, span_line_height,
        font->ascender, font->descender, baseline_pos, 0.0f, 0.0f);
    float fragment_height = font_get_cell_height(font_handle);
    if (fragment_height <= 0.0f) fragment_height = font->ascender + font->descender;
    if (fragment_height <= 0.0f) fragment_height = span_line_height;

    // A split inline contributes its own font box; tall atomic descendants may
    // enlarge the line box but do not enlarge the element's client rect fragment.
    span_record_split_inline_fragment(span, lycon->line.left, lycon->line.right,
                                      fragment_y, fragment_y + fragment_height);
}

static void span_vertical_decoration_edges(ViewSpan* span, float* top_edge, float* bottom_edge) {
    float border_top = 0.0f, border_bottom = 0.0f;
    float pad_top = 0.0f, pad_bottom = 0.0f;
    if (span && span->bound) {
        if (span->bound->border) {
            border_top = span->bound->border->width.top;
            border_bottom = span->bound->border->width.bottom;
        }
        pad_top = span->bound->padding.top > 0.0f ? span->bound->padding.top : 0.0f;
        pad_bottom = span->bound->padding.bottom > 0.0f ? span->bound->padding.bottom : 0.0f;
    }
    if (top_edge) *top_edge = roundf(border_top) + roundf(pad_top);
    if (bottom_edge) *bottom_edge = roundf(border_bottom) + roundf(pad_bottom);
}

static void record_block_in_inline_split_chain(ViewSpan* span) {
    if (!span || span->width < 0.0f || span->height < 0.0f) return;

    float top_edge = 0.0f, bottom_edge = 0.0f;
    span_vertical_decoration_edges(span, &top_edge, &bottom_edge);
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
    float border_top = 0.0f, border_right = 0.0f, border_bottom = 0.0f, border_left = 0.0f;
    float pad_left = 0.0f, pad_right = 0.0f, pad_top = 0.0f, pad_bottom = 0.0f;
    if (span->bound && span->bound->border) {
        border_top = span->bound->border->width.top;
        border_right = span->bound->border->width.right;
        border_bottom = span->bound->border->width.bottom;
        border_left = span->bound->border->width.left;
    }
    if (span->bound) {
        pad_left = span->bound->padding.left > 0.0f ? span->bound->padding.left : 0.0f;
        pad_right = span->bound->padding.right > 0.0f ? span->bound->padding.right : 0.0f;
        pad_top = span->bound->padding.top > 0.0f ? span->bound->padding.top : 0.0f;
        pad_bottom = span->bound->padding.bottom > 0.0f ? span->bound->padding.bottom : 0.0f;
    }

    float left_edge = border_left + pad_left;
    float right_edge = border_right + pad_right;
    float top_edge = roundf(border_top) + roundf(pad_top);
    float bottom_edge = roundf(border_bottom) + roundf(pad_bottom);
    float fragment_width =
        span->collapsed_line_fragment_max_x - span->collapsed_line_fragment_min_x;
    float fragment_height =
        span->collapsed_line_fragment_max_y - span->collapsed_line_fragment_min_y;
    if (span->content_height > fragment_height) {
        fragment_height = span->content_height;
    }

    span->x = span->collapsed_line_fragment_min_x - left_edge;
    span->y = span->collapsed_line_fragment_min_y - top_edge;
    span->width = fragment_width + left_edge + right_edge;
    span->height = fragment_height + top_edge + bottom_edge;
}

static void compute_empty_span_bounding_box(ViewSpan* span, FontHandle* fallback_fh) {
    if (span->has_collapsed_line_fragment_union) {
        compute_span_from_collapsed_line_fragment(span);
        return;
    }

    // CSS 2.1 section 9.4.2: only inline-axis decorations keep an empty span present.
    float border_left = 0.0f, border_right = 0.0f;
    float border_top = 0.0f, border_bottom = 0.0f;
    float pad_left = 0.0f, pad_right = 0.0f;
    float pad_top = 0.0f, pad_bottom = 0.0f;
    float margin_left = 0.0f, margin_right = 0.0f;
    if (span->bound) {
        if (span->bound->border) {
            border_left = span->bound->border->width.left;
            border_right = span->bound->border->width.right;
            border_top = span->bound->border->width.top;
            border_bottom = span->bound->border->width.bottom;
        }
        pad_left = max(span->bound->padding.left, 0.0f);
        pad_right = max(span->bound->padding.right, 0.0f);
        pad_top = max(span->bound->padding.top, 0.0f);
        pad_bottom = max(span->bound->padding.bottom, 0.0f);
        margin_left = span->bound->margin.left;
        margin_right = span->bound->margin.right;
    }

    float inline_size = border_left + pad_left + pad_right + border_right;
    if (inline_size > 0.0f || margin_left != 0.0f || margin_right != 0.0f) {
        FontHandle* font = span->font ? span->font->font_handle : fallback_fh;
        float font_content_height = font ? font_get_cell_height(font) : 0.0f;
        span->width = inline_size;
        span->height = roundf(font_content_height + border_top + pad_top +
                              pad_bottom + border_bottom);
    } else {
        span->width = 0.0f;
        span->height = 0.0f;
    }
}

// Compute bounding box of a ViewSpan based on union of child views
// The bounding box includes the span's own border and padding
void compute_span_bounding_box(ViewSpan* span, bool is_multi_line, struct FontHandle* fallback_fh) {
    View* child = span->first_child;
    if (!child) {
        compute_empty_span_bounding_box(span, fallback_fh);
        return;
    }

    // Skip nil-views (RDT_VIEW_NONE) and out-of-flow children (absolute/fixed) —
    // they don't participate in normal flow layout and have positions determined
    // by their containing block, not the inline span (CSS 2.1 §9.3.1, §10.6.3)
    while (child && (child->view_type == RDT_VIEW_NONE || is_out_of_flow_child(child) ||
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

    // Initialize bounds with first non-nil child.
    // CSS 2.1 §8.3: inline-level child margins are part of the inline flow and
    // contribute to the containing inline span's fragment bounds.
    // child->x is the border-box position (margin already added to the x coordinate),
    // so the outer-left = child->x - margin.left, outer-right = child->x + width + margin.right.
    auto get_child_relative_offset = [](View* c, float* offset_x, float* offset_y) {
        if (offset_x) *offset_x = 0.0f;
        if (offset_y) *offset_y = 0.0f;
        if (!c) return;
        if (ViewBlock* vb = layout_inline_as_block_view(c)) {
            layout_relative_position_offset(vb, offset_x, offset_y);
        } else if (ViewSpan* elem = lam::view_as<RDT_VIEW_INLINE>(c)) {
            layout_relative_position_offset(layout_inline_unsafe_block_api_span(elem),
                                            offset_x, offset_y);
        }
    };

    auto get_child_static_x = [&get_child_relative_offset](View* c) -> float {
        float dx = 0.0f;
        get_child_relative_offset(c, &dx, nullptr);
        float x = c->x - dx;
        if (ViewSpan* sp = lam::view_as<RDT_VIEW_INLINE>(c)) {
            if (sp->has_ancestor_fragment_union &&
                sp->ancestor_fragment_min_x < x) {
                x = sp->ancestor_fragment_min_x;
            }
        }
        return x;
    };

    auto get_child_static_right = [&get_child_relative_offset](View* c) -> float {
        float dx = 0.0f;
        get_child_relative_offset(c, &dx, nullptr);
        float right = c->x - dx + c->width;
        if (ViewSpan* sp = lam::view_as<RDT_VIEW_INLINE>(c)) {
            if (sp->has_ancestor_fragment_union &&
                sp->ancestor_fragment_max_x > right) {
                right = sp->ancestor_fragment_max_x;
            }
        }
        return right;
    };

    auto get_child_inline_margin = [](View* c, bool inline_start) -> float {
        if (ViewBlock* block = lam::view_as_block<RDT_VIEW_INLINE_BLOCK>(c)) {
            if (block->bound) {
                return inline_start ? block->bound->margin.left
                                    : block->bound->margin.right;
            }
        } else if (ViewSpan* child_span = lam::view_as<RDT_VIEW_INLINE>(c)) {
            if (child_span->bound) {
                return inline_start ? child_span->bound->margin.left
                                    : child_span->bound->margin.right;
            }
        }
        return 0.0f;
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
    // extend from the font CONTENT AREA, not from child borders. Track two extents:
    // 1. visual_min/max_y: union of ALL children's boxes (including child inline
    //    span borders) — the full visual extent of children.
    // 2. content_min/max_y: union of NON-inline-span children (text, inline-block)
    //    — the font content area position, used for placing THIS span's border.
    // The final bbox = union of visual extent and parent's own border extent.

    // CSS 2.1 §9.4.3: Relative positioning moves a box visually but does not
    // affect the parent's bounding rect. For children with position:relative
    // (block or inline), use their static (pre-offset) position when computing
    // the parent inline span's bounding box.
    auto get_child_static_y = [&get_child_relative_offset](View* c) -> float {
        float dy = 0.0f;
        get_child_relative_offset(c, nullptr, &dy);
        float y = c->y - dy;
        if (ViewSpan* elem = lam::view_as<RDT_VIEW_INLINE>(c)) {
            if (elem->has_ancestor_fragment_union &&
                elem->ancestor_fragment_min_y < y) {
                y = elem->ancestor_fragment_min_y;
            }
        }
        return y;
    };

    auto get_child_static_bottom = [&get_child_relative_offset](View* c) -> float {
        float dy = 0.0f;
        get_child_relative_offset(c, nullptr, &dy);
        float bottom = c->y - dy + c->height;
        if (ViewSpan* elem = lam::view_as<RDT_VIEW_INLINE>(c)) {
            if (elem->has_ancestor_fragment_union &&
                elem->ancestor_fragment_max_y > bottom) {
                bottom = elem->ancestor_fragment_max_y;
            }
        }
        return bottom;
    };

    auto get_child_content_y = [&get_child_static_y](View* c) -> float {
        if (ViewSpan* cs = lam::view_as<RDT_VIEW_INLINE>(c)) {
            float bt = 0, pt = 0;
            if (cs->bound) {
                if (cs->bound->border) bt = cs->bound->border->width.top;
                pt = cs->bound->padding.top > 0 ? cs->bound->padding.top : 0;
            }
            float content_y = get_child_static_y(c) + bt + pt;
            if (cs->has_ancestor_fragment_union &&
                cs->ancestor_fragment_min_y < content_y) {
                content_y = cs->ancestor_fragment_min_y;
            }
            return content_y;
        }
        return get_child_static_y(c);
    };
    auto get_child_content_bottom = [&get_child_static_bottom](View* c) -> float {
        if (ViewSpan* cs = lam::view_as<RDT_VIEW_INLINE>(c)) {
            float bb = 0, pb = 0;
            if (cs->bound) {
                if (cs->bound->border) bb = cs->bound->border->width.bottom;
                pb = cs->bound->padding.bottom > 0 ? cs->bound->padding.bottom : 0;
            }
            float content_bottom = get_child_static_bottom(c) - bb - pb;
            if (cs->has_ancestor_fragment_union &&
                cs->ancestor_fragment_max_y > content_bottom) {
                content_bottom = cs->ancestor_fragment_max_y;
            }
            return content_bottom;
        }
        return get_child_static_bottom(c);
    };

    float min_x = get_child_outer_left(child);
    float child_sy = get_child_static_y(child);
    float visual_min_y = child_sy;
    float max_x = get_child_outer_right(child);
    float visual_max_y = get_child_static_bottom(child);
    float content_min_y = get_child_content_y(child);
    float content_max_y = get_child_content_bottom(child);

    // Iterate through remaining children to find union
    child = child->next();
    while (child) {
        // Skip nil-views and out-of-flow children (absolute/fixed positioned)
        if (child->view_type == RDT_VIEW_NONE || is_out_of_flow_child(child) ||
            view_is_collapsed_whitespace_text(child, span)) {
            if (view_is_collapsed_whitespace_text(child, span)) {
                span_record_ancestor_fragment(span, child);
            }
            child = child->next();
            continue;
        }
        float child_min_x = get_child_outer_left(child);
        float child_max_x = get_child_outer_right(child);

        // Expand bounding box to include this child
        if (child_min_x < min_x) min_x = child_min_x;
        if (child_max_x > max_x) max_x = child_max_x;

        // Visual extent (including child inline span borders)
        // For block children with position:relative, use static y
        float sy = get_child_static_y(child);
        if (sy < visual_min_y) visual_min_y = sy;
        float sb = get_child_static_bottom(child);
        if (sb > visual_max_y) visual_max_y = sb;

        // Content extent (stripped of child inline span borders)
        float cy = get_child_content_y(child);
        float cb = get_child_content_bottom(child);
        if (cy < content_min_y) content_min_y = cy;
        if (cb > content_max_y) content_max_y = cb;

        child = child->next();
    }

    // Get border and padding widths
    float border_top = 0, border_right = 0, border_bottom = 0, border_left = 0;
    float pad_left = 0, pad_right = 0, pad_top = 0, pad_bottom = 0;
    if (span->bound && span->bound->border) {
        border_top = span->bound->border->width.top;
        border_right = span->bound->border->width.right;
        border_bottom = span->bound->border->width.bottom;
        border_left = span->bound->border->width.left;
    }
    if (span->bound) {
        pad_left = span->bound->padding.left > 0 ? span->bound->padding.left : 0;
        pad_right = span->bound->padding.right > 0 ? span->bound->padding.right : 0;
        pad_top = span->bound->padding.top > 0 ? span->bound->padding.top : 0;
        pad_bottom = span->bound->padding.bottom > 0 ? span->bound->padding.bottom : 0;
    }

    // CSS 2.1 §9.4.2: If children have zero content extent AND the span has no
    // horizontal (inline-direction) decorations, the span generates no visible
    // inline box — collapse to zero. This handles nested empty spans.
    float left_edge = border_left + pad_left;
    float right_edge = border_right + pad_right;
    float inline_sum = left_edge + right_edge;
    float content_width = max_x - min_x;
    float visual_height = visual_max_y - visual_min_y;
    if (content_width == 0 && visual_height == 0 && inline_sum == 0) {
        span->width = 0;
        span->height = 0;
        return;
    }

    // CSS 2.1 §10.6.1: For inline non-replaced elements, vertical borders/padding
    // extend from the font CONTENT AREA, not from child border boxes. This means
    // nested inline spans with borders don't stack — both extend from the same
    // content area. Compute the parent's border extent from content_min/max_y
    // (the text/inline-block positions), then take the union with the visual extent
    // (which includes child inline span borders that may extend further).
    // Use roundf to avoid truncation for fractional em-based padding (e.g. 0.2em = 2.72px).
    // Truncating 2.72 → 2 loses 0.7px per side, making inline code elements too short.
    float parent_border_top_y = content_min_y - roundf(border_top) - roundf(pad_top);
    float parent_border_bottom_y = content_max_y + roundf(border_bottom) + roundf(pad_bottom);
    // Only block-in-inline fragmentation promotes recorded descendant fragments
    // into the ancestor's own split border-box union.
    float final_min_y = span->has_split_inline_fragment_union
        ? min(visual_min_y, parent_border_top_y) : parent_border_top_y;
    float final_max_y = span->has_split_inline_fragment_union
        ? max(visual_max_y, parent_border_bottom_y) : parent_border_bottom_y;
    if (span->has_split_inline_fragment_union && !span_has_direct_visible_text(span)) {
        if (span->split_inline_fragment_min_x < min_x) min_x = span->split_inline_fragment_min_x;
        if (span->split_inline_fragment_max_x > max_x) max_x = span->split_inline_fragment_max_x;
        content_width = max_x - min_x;
        final_min_y = span->split_inline_fragment_min_y - roundf(border_top) - roundf(pad_top);
        final_max_y = span->split_inline_fragment_max_y + roundf(border_bottom) + roundf(pad_bottom);

        // CSS 2.1 §9.2.1.1: split inline boxes expose the union of their own
        // anonymous fragments. Descendant inline borders do not enlarge an
        // ancestor's fragment border-box.
        span->x = min_x;
        span->y = final_min_y;
        span->width = content_width;
        span->height = final_max_y - final_min_y;
        return;
    }
    if (span->has_inline_fragment_union && span_has_direct_visible_text(span)) {
        if (span->inline_fragment_min_y < final_min_y) final_min_y = span->inline_fragment_min_y;
        if (span->inline_fragment_max_y > final_max_y) final_max_y = span->inline_fragment_max_y;
        if (span->inline_fragment_min_x < min_x) min_x = span->inline_fragment_min_x;
        if (span->inline_fragment_max_x > max_x) max_x = span->inline_fragment_max_x;
        content_width = max_x - min_x;
    }

    // CSS 2.1 §8.5.1: Inline elements' border/padding appear at the start and end
    // of the inline box. For single-line spans, border+padding expand the bounding box
    // symmetrically. For multi-line spans, left border+padding only appears on the
    // first line fragment and right on the last — the union bounding box cannot simply
    // add both edges, so we skip horizontal expansion for multi-line.
    if (is_multi_line) {
        // Multi-line: don't add horizontal border+padding to union bounding box
        span->x = min_x;
        span->y = final_min_y;
        span->width = content_width;
        span->height = final_max_y - final_min_y;
    } else {
        // Single-line: include horizontal border+padding in bounding box
        span->x = min_x - left_edge;
        span->y = final_min_y;
        span->width = content_width + left_edge + right_edge;
        span->height = final_max_y - final_min_y;
    }
}

// ============================================================================
// Math Element Handling
// ============================================================================

/**
 * Check if an element is a math element (has class "math" with "inline" or "display" subclass).
 * Returns: 0 = not math, 1 = inline math, 2 = display math
 */
static int detect_math_element(DomElement* elem) {
    if (!elem) return 0;

    // check for class="math inline" or class="math display"
    if (dom_element_has_class(elem, "math")) {
        if (dom_element_has_class(elem, "inline")) {
            return 1;  // inline math
        }
        if (dom_element_has_class(elem, "display")) {
            return 2;  // display math
        }
        // just "math" class defaults to inline
        return 1;
    }

    // check for tag <math>
    if (elem->tag() == HTM_TAG_MATH) {
        return 1;  // MathML element
    }

    return 0;
}

/**
 * Layout a math element.
 *
 * NOTE: The legacy MathLive/MathBox pipeline has been removed.
 * For now, this function is a stub that logs a warning.
 */
static void layout_math_span(LayoutContext* lycon, DomElement* elem, bool is_display) {
     log_debug("layout_math_span: MathLive/MathBox pipeline removed");
     // For now, skip math rendering.
    (void)lycon;
    (void)elem;
    (void)is_display;
}

/**
 * Handle inline elements containing block-level children per CSS 2.1 §9.2.1.1
 *
 * When a block box appears inside an inline box, the inline box is split into
 * anonymous inline boxes before and after the block:
 *
 * <span>Text 1 <div>Block</div> Text 2</span>
 *
 * Creates:
 * - Anonymous inline box: "Text 1"
 * - Block box: <div>Block</div>
 * - Anonymous inline box: "Text 2"
 *
 * The inline box's properties (font, color, etc.) apply to the anonymous boxes.
 */
void layout_inline_with_block_children(LayoutContext* lycon, DomElement* inline_elem,
                                        ViewSpan* span, DomNode* first_child,
                                        float inline_start_edge, float span_line_height) {
    log_debug("block-in-inline: splitting inline box for %s", inline_elem->source_loc());

    // Save inline formatting context state
    Linebox saved_line = lycon->line;
    FontBox saved_font = lycon->font;
    CssEnum saved_vertical_align = lycon->line.vertical_align;

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
        // both break the inline flow. Table-internal elements have been wrapped in anonymous
        // block-level tables by wrap_orphaned_table_children() when block children exist.
        // CSS 2.1 §9.5: Floats are out of flow and should not break the inline
        // flow.  They are handled by layout_flow_node → layout_block → layout_float_element.
        bool child_is_float = false;
        // CSS 2.1 §9.6.1: Absolutely positioned elements are out of flow and
        // should not break the inline flow.  They are handled by
        // layout_flow_node → layout_block → layout_abs_block, which preserves
        // the inline cursor (advance_x) as the static position (§10.3.7).
        bool child_is_abspos = false;
        if (child->is_element()) {
            DomElement* ce = layout_inline_as_element(child);
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
            // When a block appears as the first content inside an inline with
            // inline-start border/padding, the leading anonymous block box
            // contains the span's inline-start edge, creating a line box
            // of one line-height even though there's no other inline content.
            if (!had_block_child && (!in_inline_sequence || lycon->line.is_line_start) && span->bound) {
                bool is_rtl = lycon->block.direction == CSS_VALUE_RTL;
                if (inline_has_axis_edge_decoration(span, is_rtl, true)) {
                    float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 18.0f;
                    lycon->block.advance_y += line_height;
                    log_debug("%s block-in-inline: leading anonymous block strut: advance_y += %.1f (inline-start decoration)", inline_elem->source_loc(),
                              line_height);
                }
            }
            had_block_child = true;

            // Found block/table-internal child - end current inline sequence if active
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
                    log_debug("%s block-in-inline: calling line_break before block, advance_x=%.1f, max_width=%.1f", inline_elem->source_loc(),
                             lycon->line.advance_x, lycon->block.max_width);
                    line_break(lycon);
                    log_debug("%s block-in-inline: after line_break, advance_x=%.1f, max_width=%.1f", inline_elem->source_loc(),
                             lycon->line.advance_x, lycon->block.max_width);
                }
                in_inline_sequence = false;
                visible_inline_in_sequence = false;
            }

            // Layout block child (it breaks out of inline context)
            // The block will cause a line break and establish its own formatting context
            // IMPORTANT: Save/restore max_width because block layout will set it to container width,
            // overwriting the inline content width we just measured
            float saved_max_width = lycon->block.max_width;
            log_debug("%s block-in-inline: laying out block child %s", inline_elem->source_loc(), child->node_name());
            layout_block(lycon, child, child_display);
            visible_inline_after_last_block = false;
            lycon->block.max_width = saved_max_width; // Restore inline content width
            lycon->line.inline_start_edge_pending = 0.0f;
            if (lycon->line.is_line_start) {
                lycon->line.advance_x = lycon->line.left;
            }
            log_debug("%s block-in-inline: after block layout, restored max_width=%.1f", inline_elem->source_loc(), lycon->block.max_width);

            // CSS 2.1 §9.2.1.1 + §8.3.1: Parent-child margin collapse for
            // block-in-inline. When the inline wrapper's parent is a block
            // container that doesn't create a BFC and has no border/padding,
            // the block child's margins should collapse with the container.
            if (child->is_element()) {
                DomElement* child_elem = layout_inline_as_element(child);
                ViewBlock* child_block = child_elem ? layout_inline_as_block_view(child_elem) : nullptr;
                DomNode* container_node = inline_elem->parent;
                if (child_block && child_block->bound && container_node &&
                    container_node->is_element()) {
                    ViewBlock* container = layout_inline_as_block_view(container_node);
                    // Only collapse when the container doesn't establish a BFC
                    // (table cells, overflow:hidden etc. prevent margin collapse)
                    if (container && !block_context_establishes_bfc(container)) {
                        float cont_bt = container->bound && container->bound->border
                            ? container->bound->border->width.top : 0;
                        float cont_pt = container->bound ? container->bound->padding.top : 0;
                        // Also check that the inline wrapper has no border/padding barrier
                        float inline_bt = span->bound && span->bound->border
                            ? span->bound->border->width.top : 0;
                        float inline_pt = span->bound ? span->bound->padding.top : 0;

                        // Top margin collapse: first block, no border/padding barrier
                        if (!had_block_child_before && !visible_inline_before_first_block &&
                            cont_bt == 0 && cont_pt == 0 &&
                            inline_bt == 0 && inline_pt == 0 &&
                            child_block->bound->margin.top != 0) {
                            float child_mt = child_block->bound->margin.top;
                            float cont_mt = container->bound ? container->bound->margin.top : 0;
                            float collapsed = (child_mt >= 0 && cont_mt >= 0) ?
                                (child_mt > cont_mt ? child_mt : cont_mt) :
                                (child_mt < 0 && cont_mt < 0) ?
                                (child_mt < cont_mt ? child_mt : cont_mt) :
                                child_mt + cont_mt;
                            float y_delta = collapsed - cont_mt;
                            container->y += y_delta;
                            if (!container->bound) {
                                container->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                                memset(container->bound, 0, sizeof(BoundaryProp));
                            }
                            container->bound->margin.top = collapsed;
                            child_block->y -= child_mt;
                            child_block->bound->margin.top = 0;
                            lycon->block.advance_y -= child_mt;
                            log_debug("%s block-in-inline: top margin collapse: child_mt=%.1f, collapsed=%.1f, container y_delta=%.1f",
                                      inline_elem->source_loc(), child_mt, collapsed, y_delta);
                        }
                        // Bottom margin collapse: check done after loop (need to know if it's the last block)
                    }
                }
            }

            View* block_fragment = static_cast<View*>(child);
            if (block_fragment->width > 0.0f || block_fragment->height > 0.0f) {
                float relative_x = 0.0f, relative_y = 0.0f;
                if (ViewBlock* fragment_block = lam::view_as_block(block_fragment)) {
                    layout_relative_position_offset(fragment_block, &relative_x, &relative_y);
                }
                // margin collapse finalizes normal-flow coordinates before the split
                // ancestor records its fragment; relative offsets remain visual-only.
                span_record_split_inline_fragment(
                    span, lycon->line.left, lycon->line.right,
                    block_fragment->y - relative_y,
                    block_fragment->y - relative_y + block_fragment->height);
            }
            had_block_child_before = true;
            if (child->is_element()) last_block_child_elem = layout_inline_as_element(child);

        } else {
            // Inline or text content - accumulate in anonymous inline box
            if (!in_inline_sequence) {
                // Start new anonymous inline box sequence
                in_inline_sequence = true;
                visible_inline_in_sequence = false;

                // Restore inline formatting context for this anonymous box
                // This ensures the inline's font, colors, etc. apply
                // IMPORTANT: Don't restore advance_x - let it continue from current position
                // Only restore after a block break (where advance_x was already reset by line_break)
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

                log_debug("%s block-in-inline: starting anonymous inline sequence at advance_y=%f, advance_x=%f", inline_elem->source_loc(),
                         lycon->block.advance_y, lycon->line.advance_x);
            }

            log_debug("%s block-in-inline: laying out inline/text child %s at advance_y=%f", inline_elem->source_loc(),
                     child->node_name(), lycon->block.advance_y);
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
    // After all children are processed, if the last block child has a bottom
    // margin and the container has no bottom border/padding, collapse it.
    if (last_block_child_elem && !visible_inline_after_last_block &&
        !has_following_in_flow_content(inline_elem) &&
        (!in_inline_sequence || lycon->line.is_line_start)) {
        ViewBlock* last_blk = layout_inline_as_block_view(last_block_child_elem);
        DomNode* container_node = inline_elem->parent;
        if (last_blk && last_blk->bound && container_node &&
            container_node->is_element()) {
            ViewBlock* container = layout_inline_as_block_view(container_node);
            if (container && !block_context_establishes_bfc(container)) {
                float cont_bb = container->bound && container->bound->border
                    ? container->bound->border->width.bottom : 0;
                float cont_pb = container->bound ? container->bound->padding.bottom : 0;
                float inline_bb = span->bound && span->bound->border
                    ? span->bound->border->width.bottom : 0;
                float inline_pb = span->bound ? span->bound->padding.bottom : 0;
                // Check: container has auto height (no explicit height)
                bool cont_auto_height = !container->blk || container->blk->given_height < 0;
                if (cont_bb == 0 && cont_pb == 0 && inline_bb == 0 && inline_pb == 0 &&
                    cont_auto_height && last_blk->bound->margin.bottom != 0) {
                    // the split block can reach the container edge only when no later
                    // in-flow sibling makes its margin a sibling margin instead.
                    float child_mb = last_blk->bound->margin.bottom;
                    float cont_mb = container->bound ? container->bound->margin.bottom : 0;
                    float collapsed = (child_mb >= 0 && cont_mb >= 0) ?
                        (child_mb > cont_mb ? child_mb : cont_mb) :
                        (child_mb < 0 && cont_mb < 0) ?
                        (child_mb < cont_mb ? child_mb : cont_mb) :
                        child_mb + cont_mb;
                    if (!container->bound) {
                        container->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                        memset(container->bound, 0, sizeof(BoundaryProp));
                    }
                    container->bound->margin.bottom = collapsed;
                    lycon->block.advance_y -= child_mb;
                    last_blk->bound->margin.bottom = 0;
                    log_debug("%s block-in-inline: bottom margin collapse: child_mb=%.1f, collapsed=%.1f",
                              inline_elem->source_loc(), child_mb, collapsed);
                }
            }
        }
    }

    // CSS 2.1 §9.2.1.1: When an inline element with border/padding is split by
    // a block child, the trailing anonymous block contains the span's continuation
    // (with the inline-end edge: border-right/padding-right in LTR, or
    // border-left/padding-left in RTL). Even if the trailing content is only
    // whitespace (which collapses), the span's inline-end edge generates a strut
    // that creates a non-zero-height line box in the trailing anonymous block.
    // Per CSS 2.1 §9.4.2, a line box is non-zero-height when it contains an inline
    // element with non-zero inline-direction padding or border. Account for this by
    // advancing advance_y by one line-height when:
    // 1. A block child was encountered (the inline was split)
    // 2. The trailing anonymous block is empty (is_line_start still true)
    // 3. The span has non-zero inline-END decoration (border/padding on the end side)
    if (!visible_inline_after_last_block && (!in_inline_sequence || lycon->line.is_line_start) &&
        !has_following_inline_content(inline_elem)) {
        // The last content was a block child, or trailing inline content was whitespace-only.
        // Check if the span has inline-end decoration that would keep the line box open.
        bool has_inline_end_decoration = inline_has_axis_edge_decoration(
            span, lycon->block.direction == CSS_VALUE_RTL, false);
        if (has_inline_end_decoration) {
            float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 18.0f;
            lycon->block.advance_y += line_height;
            log_debug("%s block-in-inline: trailing anonymous block strut: advance_y += %.1f (inline-end decoration)", inline_elem->source_loc(),
                      line_height);
        }
    }

    // Note: Don't call line_break() here - the caller is responsible for line breaking.
    // This function may be called for nested inlines, and the outer inline may have
    // more siblings to process on the same line.
}

void layout_inline(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {
    log_debug("layout inline %s", elmt->source_loc());

    // HTML5 §4.5.27: <wbr> represents a line break opportunity.
    // It doesn't generate a box — browsers report (0,0,0,0) via getBoundingClientRect.
    // Equivalent to U+200B ZWSP per Unicode Line Breaking Algorithm.
    if (elmt->tag() == HTM_TAG_WBR) {
        ViewSpan* wbr_span = lam::view_require<RDT_VIEW_INLINE>(set_view(lycon, RDT_VIEW_INLINE, elmt));
        wbr_span->x = 0;
        wbr_span->y = 0;
        wbr_span->width = 0;
        wbr_span->height = 0;
        // Record a soft wrap opportunity at the current inline position,
        // mirroring the U+200B ZWSP handling in layout_text.cpp.
        // Use the element address as a non-null sentinel for last_space — it will
        // never fall within a text buffer range, so the wrap logic correctly
        // breaks at the element boundary (the "outside text" path in layout_text).
        lycon->line.last_space = (unsigned char*)elmt;
        lycon->line.last_space_pos = lycon->line.advance_x;
        lycon->line.last_space_kind = BRK_ZERO_WIDTH_BREAK;
        lycon->line.trailing_space_width = 0;
        lycon->line.wrap_opportunity_before_nowrap = true;
        return;
    }

    if (elmt->tag() == HTM_TAG_BR) {
        // allocate a line break view
        View* br_view = set_view(lycon, RDT_VIEW_BR, elmt);
        // Position <br> at the trimmed content edge for LTR, or at the
        // line box start for RTL. In RTL, bidi visual reordering places the
        // zero-width <br> at the inline-start edge (before LTR text runs);
        // browsers report br.x at the content area start in RTL contexts.
        if (lycon->block.direction == CSS_VALUE_RTL) {
            br_view->x = lycon->line.left;
        } else {
            br_view->x = lycon->line.advance_x - lycon->line.trailing_space_width;
        }
        br_view->width = 0;
        // The <br> element's bounding box height is the font content area (cell height),
        // not the CSS line-height. The line-height is used by line_break() to advance
        // the block cursor, but the element's own reported height matches the font metrics.
        // Chrome's reported <br> height varies by context (0 in some cases, font-height
        // in others). The exact rule is not fully understood, so we use font cell height
        // consistently.
        struct FontHandle* br_fh = lycon->font.font_handle;
        float br_font_height = br_fh ? font_get_cell_height(br_fh) : lycon->block.line_height;
        br_view->height = br_font_height;
        // CSS 2.1 §10.8.1: <br> participates in the current line before forcing
        // the break. Its zero-width inline box is baseline-aligned with earlier
        // content on the line, including replaced elements.
        float br_line_height = lycon->block.line_height > 0.0f ? lycon->block.line_height : br_font_height;
        br_view->y = lycon->block.advance_y + (br_line_height - br_font_height) / 2.0f;
        bool collapse_br_rect = quirks_table_cell_br_after_nested_inline_text(lycon, elmt);
        float collapsed_br_y = lycon->block.advance_y + lycon->line.max_ascender;
        // CSS Text 3 §7.2: text-align-last applies to lines immediately before
        // a forced line break. <br> is a forced break per CSS Text 3 §4.1.
        bool was_line_clamped = lycon->block.line_clamped;
        lycon->line.is_last_line = true;
        line_break(lycon);
        lycon->line.is_last_line = false;
        if (collapse_br_rect) {
            // BackCompat table cells expose terminal breaks after nested inline
            // text as caret-position boxes; the line still advances normally.
            br_view->y = collapsed_br_y;
            br_view->height = 0.0f;
        }
        if (!was_line_clamped && lycon->block.line_clamped && lycon->font.font_handle) {
            GlyphInfo ellipsis = font_get_glyph(lycon->font.font_handle, 0x2026); // U+2026
            br_view->width = ellipsis.id != 0 ? ellipsis.advance_x : lycon->font.current_font_size * 0.5f;
        }

        // CSS 2.1 §9.5.2: check if the <br> has a 'clear' property and apply float clearance.
        // Browsers treat <br style="clear:both"> as clearing floats at the line break point.
        if (elmt->is_element()) {
            DomElement* br_elem = layout_inline_as_element(elmt);
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
                    // Progressive clear: only clear past floats whose top edge is at or
                    // above the current advance_y. This handles the case where all floats
                    // were pre-laid in the prescan and we need to clear them incrementally
                    // as the inline flow encounters each <br> clear point.
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
                    // clear_y is in BFC coordinates; convert to local
                    float local_clear_y = clear_y - lycon->block.bfc_offset_y;
                    if (local_clear_y > lycon->block.saved_clear_y) {
                        lycon->block.saved_clear_y = local_clear_y;
                    }
                    if (local_clear_y > lycon->block.advance_y) {
                        log_debug("%s <br> clear: advance_y %.1f -> %.1f (clear_y=%.1f, bfc_offset=%.1f)", elmt->source_loc(),
                                  lycon->block.advance_y, local_clear_y, clear_y, lycon->block.bfc_offset_y);
                        lycon->block.advance_y = local_clear_y;

                        // CSS 2.1 §9.5.2: After clearing, re-adjust the line's effective
                        // bounds and advance_x for float intrusion at the new y position.
                        // line_break() already called line_reset() which set advance_x based
                        // on float space at the pre-clear y; we must update for post-clear y.
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

    // check for math elements (class="math inline" or class="math display")
    if (elmt->is_element()) {
        DomElement* elem = layout_inline_as_element(elmt);
        // debug: check what classes this element has
        bool has_math = dom_element_has_class(elem, "math");
        bool has_inline = dom_element_has_class(elem, "inline");
        log_debug("%s layout_inline: checking %s has_math=%d has_inline=%d", elmt->source_loc(),
                  elem->node_name(), has_math, has_inline);
        int math_type = detect_math_element(elem);
        if (math_type > 0) {
            bool is_display = (math_type == 2);
            log_debug("%s layout_inline: detected math element, type=%d", elmt->source_loc(), math_type);
            layout_math_span(lycon, elem, is_display);
            return;
        }
    }

    // save parent context
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // unresolved yet
    CssEnum pa_line_align = lycon->line.vertical_align;
    float pa_valign_offset = lycon->line.vertical_align_offset;
    float saved_parent_ascender = lycon->line.parent_font_ascender;
    float saved_parent_descender = lycon->line.parent_font_descender;
    float saved_parent_font_size = lycon->line.parent_font_size;
    struct FontHandle* saved_parent_font_handle = lycon->line.parent_font_handle;
    lycon->elmt = elmt;

    if (lycon->line.is_line_start) {
        // A preceding float on an otherwise empty line does not trigger
        // line_reset(); refresh before assigning this inline's line fragment.
        update_line_for_bfc_floats(lycon);
    }

    ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(set_view(lycon, RDT_VIEW_INLINE, elmt));
    span->x = lycon->line.advance_x;  span->y = lycon->block.advance_y;
    span->width = 0;  span->height = 0;
    span->display = display;

    // resolve CSS styles
    dom_node_resolve_style(elmt, lycon);

    // CSS Counter handling (CSS 2.1 Section 12.4, CSS Lists 3)
    // Push a new counter scope for this inline element so that counter-reset
    // creates a properly nested counter instance (not modifying the parent scope)
    bool pushed_counter_scope = false;
    DomElement* elmt_elem = layout_inline_as_element(elmt);
    bool is_before_pseudo = elmt_elem && elmt_elem->tag_name &&
        strcmp(elmt_elem->tag_name, "::before") == 0;
    bool is_after_pseudo = elmt_elem && elmt_elem->tag_name &&
        strcmp(elmt_elem->tag_name, "::after") == 0;
    if (lycon->counter_context) {
        counter_push_scope(lycon->counter_context);
        pushed_counter_scope = true;
    }

    // Apply counter operations for this inline element
    if (lycon->counter_context && span->blk && !is_before_pseudo) {
        // Apply counter-reset if specified
        if (span->blk->counter_reset) {
            log_debug("%s     [Inline] Applying counter-reset: %s", elmt->source_loc(), span->blk->counter_reset);
            counter_reset(lycon->counter_context, span->blk->counter_reset);
        }

        // Apply counter-increment if specified
        if (span->blk->counter_increment) {
            log_debug("%s     [Inline] Applying counter-increment: %s", elmt->source_loc(), span->blk->counter_increment);
            counter_increment(lycon->counter_context, span->blk->counter_increment);
        }

        // Apply counter-set if specified (CSS Lists 3)
        // Processed after counter-reset and counter-increment per spec
        if (span->blk->counter_set) {
            log_debug("%s     [Inline] Applying counter-set: %s", elmt->source_loc(), span->blk->counter_set);
            counter_set(lycon->counter_context, span->blk->counter_set);
        }

        if (is_after_pseudo && elmt->parent && elmt->parent->is_element()) {
            DomElement* origin = layout_inline_as_element(elmt->parent);
            const char* content = dom_element_get_pseudo_element_content_with_counters(
                origin, 2, lycon->counter_context, lycon->doc->arena);
            if (!content) content = dom_element_get_pseudo_element_content(origin, 2);
            if (content && elmt->is_element()) {
                DomElement* pseudo_elem = layout_inline_as_element(elmt);
                DomNode* first = pseudo_elem->first_child;
                if (first && first->is_text()) {
                    DomText* text_node = layout_inline_as_text(first);
                    size_t content_len = strlen(content);
                    String* text_string = (String*)arena_alloc(pseudo_elem->doc->arena,
                        sizeof(String) + content_len + 1);
                    if (text_string) {
                        text_string->len = content_len;
                        memcpy(text_string->chars, content, content_len);
                        text_string->chars[content_len] = '\0';
                        text_node->native_string = text_string;
                        text_node->text = text_string->chars;
                        text_node->length = content_len;
                        text_node->rect = nullptr;
                    }
                }
            }
        }
    }

    // Allocate pseudo-element content if ::before or ::after is present
    // Inline elements can have pseudo-elements too (e.g., <span>::before)
    if (elmt->is_element()) {
        DomElement* elem = layout_inline_as_element(elmt);
        ViewBlock* block_api_span = layout_inline_unsafe_block_api_span(span);
        elem->pseudo = alloc_pseudo_content_prop(lycon, block_api_span);

        // Generate pseudo-element content from CSS content property
        generate_pseudo_element_content(lycon, block_api_span, true);   // ::before
        generate_pseudo_element_content(lycon, block_api_span, false);  // ::after

        // Insert pseudo-elements into DOM tree for proper view tree linking
        if (elem->pseudo) {
            if (elem->pseudo->before) {
                insert_pseudo_into_dom(elem, elem->pseudo->before, true);
            }
            if (elem->pseudo->after) {
                insert_pseudo_into_dom(elem, elem->pseudo->after, false);
            }
        }
    }

    if (pa_font.style) {
        // Vertical-align keywords use the containing inline's font metrics.
        lycon->line.parent_font_ascender = pa_font.style->ascender;
        lycon->line.parent_font_descender = pa_font.style->descender;
        lycon->line.parent_font_size = pa_font.style->font_size;
        lycon->line.parent_font_handle = pa_font.font_handle;
    }
    if (span->font) {
        setup_font(lycon->ui_context, &lycon->font,  span->font);
    }
    if (span->in_line && span->in_line->vertical_align) {
        lycon->line.vertical_align = span->in_line->vertical_align;
        lycon->line.vertical_align_offset = span->in_line->vertical_align_offset;
    }

    // CSS 2.1 §10.8.1: Each inline box uses its own 'line-height' property for
    // the half-leading model, not the parent block's line-height. Save the block's
    // line-height and resolve the inline element's own line-height if specified.
    float pa_line_height = lycon->block.line_height;
    bool pa_line_height_is_normal = lycon->block.line_height_is_normal;
    // Check the element's specified_style for an explicit line-height or font shorthand
    // declaration. If neither is present, line-height is inherited and the parent's
    // resolved value is already correct in lycon->block.line_height — UNLESS the
    // inherited line-height is a <number> value and the font-size changed.
    // CSS 2.1: <number> line-heights inherit the number, not the computed length,
    // so each element must recompute: number × own font-size.
    bool has_own_line_height = false;
    if (elmt->is_element()) {
        DomElement* dom_elmt = layout_inline_as_element(elmt);
        if (dom_elmt->specified_style) {
            has_own_line_height =
                style_tree_get_declaration(dom_elmt->specified_style, CSS_PROPERTY_LINE_HEIGHT) != nullptr ||
                style_tree_get_declaration(dom_elmt->specified_style, CSS_PROPERTY_FONT) != nullptr;
        }
    }
    // Also re-resolve when font-size changed AND inherited line-height is a
    // <number> or 'normal'. CSS 2.1: number line-heights inherit the number (not
    // the computed length), so each element recomputes: number × own font-size.
    // Length/percentage line-heights inherit the computed absolute value and must
    // NOT be re-resolved against the child's font-size.
    bool font_size_changed = lycon->font.style && pa_font.style &&
        lycon->font.style->font_size != pa_font.style->font_size;
    if (has_own_line_height || font_size_changed) {
        ViewBlock* block_api_span = layout_inline_unsafe_block_api_span(span);
        if (has_own_line_height) {
            setup_line_height(lycon, block_api_span);
        } else {
            // font-size changed: only re-resolve for number/normal line-height
            CssValue inherited_lh = inherit_line_height(lycon, block_api_span);
            if (pa_line_height_is_normal ||
                inherited_lh.type == CSS_VALUE_TYPE_NUMBER ||
                (inherited_lh.type == CSS_VALUE_TYPE_KEYWORD &&
                 inherited_lh.data.keyword == CSS_VALUE_NORMAL)) {
                // A parent's resolved normal line-height is font-dependent;
                // inline font-size changes must recompute it for the new font.
                setup_line_height(lycon, block_api_span);
            }
        }
        // Track if this inline's line-height exceeds the parent block's,
        // so line_break() knows to respect the expanded line box height.
        if (lycon->block.line_height > pa_line_height) {
            lycon->line.has_expanded_inline_lh = true;
            CssEnum span_valign = span->in_line && span->in_line->vertical_align ?
                span->in_line->vertical_align : CSS_VALUE_BASELINE;
            float span_valign_offset = span->in_line ?
                span->in_line->vertical_align_offset : 0.0f;
            if (span_valign == CSS_VALUE_BASELINE && span_valign_offset == 0.0f) {
                lycon->line.max_inline_line_height = max(
                    lycon->line.max_inline_line_height, lycon->block.line_height);
            }
        }
    }
    float span_resolved_line_height = lycon->block.line_height;
    // line.max_ascender and max_descender to be changed only when there's output from the span

    // layout inline content
    DomNode *child = nullptr;
    if (elmt->is_element()) {
        child = elmt_elem ? elmt_elem->first_child : nullptr;
    }

    // CSS 2.1 §8.3: Inline elements' margin/border/padding push content inward.
    // Advance the inline cursor so child text/elements start inside the margin+border+padding.
    // This applies to both normal inline content and block-in-inline splitting.
    float inline_left_edge = 0;
    float inline_right_edge = 0;
    if (span->bound) {
        // CSS 2.1 §8.3: horizontal margins apply to inline elements
        inline_left_edge += span->bound->margin.left;
        inline_right_edge += span->bound->margin.right;
        if (span->bound->border) {
            inline_left_edge += span->bound->border->width.left;
            inline_right_edge += span->bound->border->width.right;
        }
        inline_left_edge += span->bound->padding.left;
        inline_right_edge += span->bound->padding.right;
    }
    float saved_inline_pending = lycon->line.inline_start_edge_pending;

    // CSS 2.1 §9.2.1.1 and §17.2.1: Check for block-level and table-internal children
    // We need to handle two different scenarios:
    // 1. If ONLY table-internal children (no blocks) → wrap in anonymous inline-table
    // 2. If block children exist → table-internal treated as block-breaking (block-in-inline)
    bool has_block_children = false;
    bool has_table_internal = false;
    DomNode* scan = child;
    while (scan) {
        if (scan->is_element()) {
            DisplayValue child_display = resolve_display_value(scan);
            log_debug("%s block-in-inline scan: child=%s outer=%d inner=%d", elmt->source_loc(),
                     scan->node_name(), child_display.outer, child_display.inner);
            bool child_is_table_internal =
                is_table_internal_display(child_display.inner) ||
                is_table_internal_display(child_display.outer);
            if (child_is_table_internal) {
                has_table_internal = true;
            } else if (child_display.outer == CSS_VALUE_BLOCK ||
                child_display.outer == CSS_VALUE_LIST_ITEM ||
                child_display.outer == CSS_VALUE_TABLE) {
                // CSS 2.1 §9.5, §9.6.1: Absolutely/fixed positioned and floated
                // elements are out of flow and should not trigger block-in-inline
                // splitting, even though their display is blockified per §9.7.
                DomElement* child_elem = layout_inline_as_element(scan);
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
    // wrap them in anonymous inline-table to participate in inline flow
    if (has_table_internal && !has_block_children) {
        wrap_orphaned_table_children(lycon, elmt_elem);
        child = elmt_elem ? elmt_elem->first_child : nullptr;
    }

    // If block children exist, table-internal children act as block-breaking
    if (has_block_children && has_table_internal) {
        log_debug("%s block-in-inline detected: %s has both block and table-internal children", elmt->source_loc(),
                 elmt->node_name());
    }

    if (has_block_children || (has_table_internal && has_block_children)) {
        // When block children exist alongside table-internal, handle via block-in-inline
        // splitting. The anonymous block wrappers created by splitting will later call
        // wrap_orphaned_table_children() during their own layout in layout_block.cpp.

        // Handle block-in-inline splitting
        float pre_split_advance_y = lycon->block.advance_y;
        layout_inline_with_block_children(
            lycon, elmt_elem, span, child, inline_left_edge, span_resolved_line_height);

        // Advance past the right border+padding
        lycon->line.advance_x += inline_right_edge;

        // CSS 2.1 §9.2.1.1: When an inline element contains block-level children,
        // it is split into anonymous block boxes spanning the full content width of
        // the containing block. The inline element's bounding box should encompass
        // this full extent, so getBoundingClientRect() matches the browser.
        // Compute vertical union from children and preserve the larger of the
        // child/text fragment union and the parent line width.
        compute_span_bounding_box(span, true, lycon->font.font_handle);  // get vertical bounds from children
        float containing_line_width = lycon->line.right - lycon->line.left;
        float measured_right = span->x + span->width;
        span->x = lycon->line.left;
        float measured_width = measured_right - span->x;
        if (measured_width < 0.0f) measured_width = 0.0f;
        span->width = measured_width > containing_line_width ?
            measured_width : containing_line_width;

        // CSS 2.1 §9.2.1.1: For relatively-positioned block-in-inline spans,
        // override y with the flow position. compute_span_bounding_box uses
        // children's visual positions (after their own relative positioning),
        // which would contaminate the span's base position for its own offset.
        if (span->position && span->position->position == CSS_VALUE_RELATIVE) {
            span->y = pre_split_advance_y;
        }

        // CSS 2.1 §9.2.1.1: Extend span bounding box upward to cover the leading
        // anonymous block's strut. When the span has inline-start border/padding,
        // the leading strut creates a line box before the first block child.
        // The span's bbox should extend up to include this strut area.
        {
            bool has_inline_start = inline_has_axis_edge_decoration(
                span, lycon->block.direction == CSS_VALUE_RTL, true);
            if (has_inline_start) {
                float border_top = 0, pad_top = 0;
                if (span->bound) {
                    if (span->bound->border)
                        border_top = span->bound->border->width.top;
                    if (span->bound->padding.top > 0)
                        pad_top = span->bound->padding.top;
                }
                float strut_top = pre_split_advance_y - border_top - pad_top;
                if (strut_top < span->y) {
                    float old_y = span->y;
                    span->height += (old_y - strut_top);
                    span->y = strut_top;
                    log_debug("%s block-in-inline: extended span y upward from %.0f to %.0f (leading strut)", elmt->source_loc(),
                              old_y, span->y);
                }
            }
        }

        // CSS 2.1 §9.2.1.1: Extend span bounding box to cover the trailing anonymous
        // block's strut. When the span has inline-end decoration (border/padding),
        // the trailing anonymous block contains the span's inline-end edge, which
        // generates a strut of at least one line-height even with no visible content.
        // Compute the trailing extent directly from the last block child's bottom
        // position, avoiding reliance on advance_y which may or may not include
        // the trailing strut advance (depending on nesting context).
        {
            bool has_inline_end = inline_has_axis_edge_decoration(
                span, lycon->block.direction == CSS_VALUE_RTL, false);
            if (has_inline_end) {
                // Find the bottom of the last block child within this span.
                // The trailing anonymous block starts at that position.
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
                    // The trailing strut line extends from last_block_bottom by
                    // one line-height, then border-bottom and padding-bottom
                    float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 18.0f;
                    float border_bottom = 0, pad_bottom = 0;
                    if (span->bound) {
                        if (span->bound->border)
                            border_bottom = span->bound->border->width.bottom;
                        if (span->bound->padding.bottom > 0)
                            pad_bottom = span->bound->padding.bottom;
                    }
                    float trailing_extent = last_block_bottom + line_height + border_bottom + pad_bottom;
                    float span_bottom = span->y + span->height;
                    if (trailing_extent > span_bottom) {
                        span->height = trailing_extent - span->y;
                        log_debug("%s block-in-inline: extended span height to %.0f (last_block_bottom=%.1f, line_height=%.1f, border_bottom=%.1f)",
                                  elmt->source_loc(), span->height, last_block_bottom, line_height, border_bottom);
                    }
                }
            }
        }

        record_block_in_inline_split_chain(span);

        // Apply CSS relative/sticky positioning after normal layout
        if (span->position && span->position->position == CSS_VALUE_RELATIVE) {
            log_debug("%s Applying relative positioning to inline span (with block children)", elmt->source_loc());
            layout_relative_positioned(lycon, layout_inline_unsafe_block_api_span(span));
        } else if (span->position && span->position->position == CSS_VALUE_STICKY) {
            layout_sticky_positioned(lycon, layout_inline_unsafe_block_api_span(span));
        }

        lycon->font = pa_font;
        lycon->line.vertical_align = pa_line_align;
        lycon->block.line_height = pa_line_height;
        lycon->block.line_height_is_normal = pa_line_height_is_normal;
        if (pushed_counter_scope) {
            // Propagate counter-reset counters for real elements (sibling visibility)
            // but not for pseudo-elements (their counter-reset stays scoped)
            bool is_pseudo = elmt_elem &&
                elmt_elem->tag_name &&
                elmt_elem->tag_name[0] == ':';
            counter_pop_scope_propagate(lycon->counter_context, !is_pseudo);
        }
        return;
    }

    // Normal inline-only content
    bool had_children = (child != nullptr);
    float start_advance_y = lycon->block.advance_y;
    bool has_inline_axis_decoration = span_has_inline_axis_decoration(span);
    if (has_inline_axis_decoration && !lycon->line.start_view) {
        lycon->line.start_view = static_cast<View*>(span);
    }
    if (has_inline_axis_decoration) {
        lycon->line.is_line_start = false;
    }
    // CSS 2.1 §8.3: Track pending inline left edges for line break re-application.
    // If the span's first content wraps to a new line, line_reset() will re-apply
    // the pending edges so the content starts correctly indented on the new line.
    lycon->line.inline_start_edge_pending += inline_left_edge;
    lycon->line.advance_x += inline_left_edge;
    if (child) {
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling;
        } while (child);
    }
    float collapsed_inline_fragment_x = lycon->line.advance_x;

    // Advance past the right border+padding so the next sibling starts after this inline's border
    lycon->line.advance_x += inline_right_edge;

    // CSS 2.1 §8.3: Now that this span is closing, remove its contribution from
    // the pending inline edges if it wasn't consumed by content output.
    // If output_text() was called inside this span, pending was cleared to 0.
    // If no content was output (empty span), restore saved value so the
    // parent's pending state is unaffected.
    if (lycon->line.inline_start_edge_pending > saved_inline_pending) {
        lycon->line.inline_start_edge_pending = saved_inline_pending;
    }

    // CSS 2.1 §10.8.1: For non-replaced inline elements, the inline box height
    // equals its 'line-height', computed via the half-leading model. Even empty
    // inline elements contribute their strut to the line box height calculation.
    // When children produce text output, output_text() applies the half-leading;
    // for empty inlines (no children at all), we must apply it here explicitly.
    if (!had_children) {
        if (lycon->line.is_line_start && !lycon->line.has_phantom_inline_fragment) {
            // CSS 2.1 §16.2: phantom empty inlines retain an aligned static
            // position even though §9.4.2 suppresses their line-box height.
            lycon->line.start_view = layout_inline_fragment_root(static_cast<View*>(span));
            lycon->line.has_phantom_inline_fragment = true;
        }
        contribute_inline_strut(lycon, elmt, span);
        // Mark empty inline span for height fixup in view_vertical_align.
        // compute_span_bounding_box sets height=0, but the inline box should
        // report line-height when on a line with visible content.
        // CSS 2.1 §9.4.2: An inline element with non-zero margins, borders, or
        // padding makes the line box non-empty (not subject to zero-height rule).
        // Mark the line as having content so line_break() is called at finalization.
        // CSS Inline 3 §2.1: An inline element with ANY non-zero inline-axis
        // margin, border, or padding is not invisible — it generates a real
        // (non-phantom) line box. Check individual properties, not the sum,
        // because negative values cancel in sums but each is still non-zero.
        if (has_inline_axis_decoration) {
            lycon->line.is_line_start = false;
        }
    } else if (has_inline_axis_decoration && span_children_have_no_line_content(span)) {
        // collapsed whitespace descendants do not create text rects, but the
        // decorated inline fragment still generates a real line box.
        contribute_inline_strut(lycon, elmt, span);
        lycon->line.is_line_start = false;
    }

    // CSS 2.1 §8.5.1: Detect multi-line by checking if children are on different lines.
    // Multi-line means the span's content itself spans multiple lines, requiring
    // left border+padding only on the first line fragment and right on the last.
    bool span_is_multi_line = inline_span_has_multiple_line_fragments(span);
    // Check 2: advance_y moved during children layout, meaning a line
    // break occurred while laying out this span's content (text wrapped).
    // start_advance_y was captured before children layout, so if advance_y
    // increased, a line_break() was called inside this span.
    if (!span_is_multi_line && inline_span_first_line_fragment_child(span)) {
        if (lycon->block.advance_y > start_advance_y) {
            span_is_multi_line = true;
        }
    }

    // CSS 2.1 §16.6.1: Trailing whitespace at end of a line should not expand
    // the inline element's bounding box. We trim trailing whitespace from the
    // span bounding box only when the span is the last inline content on the line
    // (i.e., followed by a block element or end of parent). When more inline
    // content follows, the trailing space is inter-element spacing and should
    // be included in the span's bounding box.
    float saved_trailing = 0;
    View* last_child_for_trim = nullptr;
    if (lycon->line.trailing_space_width > 0) {
        bool has_following_inline = has_following_inline_content(span);

        if (!has_following_inline) {
            // Span is last inline content on this line — trim trailing whitespace
            View* c = span->first_child;
            while (c) {
                if (c->view_type) last_child_for_trim = c;
                c = c->next();
            }
            if (last_child_for_trim && last_child_for_trim->view_type == RDT_VIEW_TEXT) {
                // Only trim text children — they carry trailing space in their width.
                // Inline spans handle their own trim; inline-blocks/tables don't have
                // trailing space embedded in their width.
                saved_trailing = lycon->line.trailing_space_width;
                last_child_for_trim->width -= saved_trailing;
            }
        }
    }

    // CSS 2.1 §10.6.1: Store span's resolved line-height for use by
    // view_vertical_align() to compute the inline box Y/height.
    // For inline non-replaced elements, the inline box height equals line-height,
    // not the union of children's visual extent.
    // Forced breaks inside the span reset the shared line context; preserve
    // this inline's resolved line-height for table-cell height measurement.
    span->content_height = span_resolved_line_height;
    if (had_children && has_inline_axis_decoration && span_children_have_no_line_content(span)) {
        span->has_collapsed_line_fragment_union = true;
        span->collapsed_line_fragment_min_x = collapsed_inline_fragment_x;
        span->collapsed_line_fragment_max_x = collapsed_inline_fragment_x;
        span->collapsed_line_fragment_min_y = lycon->block.advance_y;
        span->collapsed_line_fragment_max_y = lycon->block.advance_y;
    }

    // CSS 2.1 §10.8.1: vertical-align applies to the inline box generated by
    // this element. Text descendants contribute metrics through output_text(),
    // but an inline box with only atomic children still contributes its own
    // line-height to the parent line.
    if (span->in_line && span->in_line->vertical_align &&
        span->in_line->vertical_align != CSS_VALUE_BASELINE &&
        span->content_height > 0.0f) {
        float asc_contribution = 0.0f;
        float desc_contribution = 0.0f;
        CssEnum valign = span->in_line->vertical_align;
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
            float span_asc = span->font ? span->font->ascender :
                (lycon->font.style ? lycon->font.style->ascender : 0.0f);
            float span_desc = span->font ? span->font->descender :
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
                lycon, valign, span->in_line->vertical_align_offset);
            // Placement and line metrics must use the same UA-defined super/sub shift.
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
            if (span->bound->border) {
                border_left = span->bound->border->width.left;
            }
            padding_left = span->bound->padding.left > 0.0f ? span->bound->padding.left : 0.0f;
        }
        // Empty inline elements still have a border box; it starts after the
        // inline-start margin, while width excludes margins.
        span->x = collapsed_inline_fragment_x - border_left - padding_left;
    }
    if (span->width == 0.0f && span->height == 0.0f && had_children &&
        span_children_have_no_line_content(span)) {
        // css 2.1 section 9.5: keep no-line-content spans collapsed, but anchor their
        // normal position at the collapsed continuation point so later relative
        // positioning moves the same zero-sized box browsers expose.
        float continuation_x = collapsed_inline_fragment_x;
        span_left_float_continuation_x(span, &continuation_x);
        span->x = continuation_x;
        span->y = lycon->block.advance_y;
    }

    // CSS 2.1 §10.6.1: For inline non-replaced elements, the bounding box height
    // should reflect the font's content area + border + padding, not the union of
    // children's visual extent. When children contain tall replaced elements (images,
    // inline-blocks) that extend beyond the font content area, cap the span's height.
    // Use font_get_cell_height() which matches the text rect height and browser behavior.
    if (span->height > 0) {
        struct FontHandle* fh = span->font ? span->font->font_handle : lycon->font.font_handle;
        if (fh) {
            float content_area = font_get_cell_height(fh);
            float bt = 0, bb = 0, pt_val = 0, pb_val = 0;
            if (span->bound) {
                if (span->bound->border) {
                    bt = span->bound->border->width.top;
                    bb = span->bound->border->width.bottom;
                }
                pt_val = span->bound->padding.top > 0 ? span->bound->padding.top : 0;
                pb_val = span->bound->padding.bottom > 0 ? span->bound->padding.bottom : 0;
            }
            float expected_height = content_area + bt + pt_val + pb_val + bb;
            if (!span_is_multi_line && span->height > expected_height) {
                // Descendant decorations may overflow, but cannot enlarge an ancestor's inline border box.
                float span_asc = span->font ? span->font->ascender :
                    (lycon->font.style ? lycon->font.style->ascender : 0.0f);
                float span_desc = span->font ? span->font->descender :
                    (lycon->font.style ? lycon->font.style->descender : 0.0f);
                float baseline_pos = line_baseline_position(lycon, nullptr);
                span->y = layout_inline_font_box_y(
                    lycon, span, span_resolved_line_height,
                    span_asc, span_desc, baseline_pos, bt, pt_val);
                span->height = expected_height;
                log_debug("%s inline span height capped to content area: %.0f (area=%.1f)", elmt->source_loc(), expected_height, content_area);
            }
            // CSS 2.1 §10.8.1: For empty inline elements with inline decorations
            // and negative half-leading (line-height < font content area), position
            // the content area above the line box using the half-leading offset.
            if (!had_children && !lycon->block.line_height_is_normal) {
                float ascender = 0;
                if (span->font) {
                    ascender = span->font->ascender;
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
    // When an inline element has children but all content collapsed (whitespace-only),
    // the span gets 0×0 from compute_span_bounding_box. However, per CSS 2.1, the
    // inline box still contributes its line-height to the line box only when
    // inline-axis decorations keep the box present. Vertical-only decorations do
    // not make an empty inline generate a DOMRect.
    if (span->height == 0 && span->width == 0 && had_children &&
        has_inline_axis_decoration) {
        if (span_children_have_no_line_content(span)) {
            span->content_height = lycon->block.line_height;
            if (lycon->line.start_view || !lycon->line.is_line_start) {
                span->height = span->content_height;
            }
            log_debug("%s marking collapsed inline span %s for line-height fixup (lh=%.1f)", elmt->source_loc(),
                     elmt->node_name(), lycon->block.line_height);
        }
    }

    // Restore the trailing space on the child so advance_x remains correct
    // for any subsequent layout processing
    if (last_child_for_trim && saved_trailing > 0) {
        last_child_for_trim->width += saved_trailing;
    }

    // Apply CSS relative/sticky positioning after normal layout
    // CSS 2.1 §9.4.3: Relatively positioned inline elements are offset from their normal position
    if (span->position && span->position->position == CSS_VALUE_RELATIVE) {
        log_debug("%s Applying relative positioning to inline span", elmt->source_loc());
        layout_relative_positioned(lycon, layout_inline_unsafe_block_api_span(span));
    } else if (span->position && span->position->position == CSS_VALUE_STICKY) {
        layout_sticky_positioned(lycon, layout_inline_unsafe_block_api_span(span));
    }

    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
    lycon->line.vertical_align_offset = pa_valign_offset;
    lycon->line.parent_font_ascender = saved_parent_ascender;
    lycon->line.parent_font_descender = saved_parent_descender;
    lycon->line.parent_font_size = saved_parent_font_size;
    lycon->line.parent_font_handle = saved_parent_font_handle;
    lycon->block.line_height = pa_line_height;
    lycon->block.line_height_is_normal = pa_line_height_is_normal;
    if (pushed_counter_scope) {
        bool is_pseudo = elmt_elem &&
            elmt_elem->tag_name &&
            elmt_elem->tag_name[0] == ':';
        counter_pop_scope_propagate(lycon->counter_context, !is_pseudo);
    }
    log_debug("%s inline span view: %d, child %p, x:%.0f, y:%.0f, wd:%.0f, hg:%.0f", elmt->source_loc(), span->view_type,
        span->first_child, span->x, span->y, span->width, span->height);
}
