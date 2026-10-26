#include "layout.hpp"
#include "view.hpp"
#include "../lib/font/font.h"
#include "../lib/tagged.hpp"
#include "../lib/utf.h"

#include <float.h>
#include <limits.h>

#if __has_include(<fribidi/fribidi.h>)
#include <fribidi/fribidi.h>
#define RDT_HAS_FRIBIDI 1
#else
#define RDT_HAS_FRIBIDI 0
#endif
// This pass deliberately works on the already-built line fragments. Inline
// boxes are not reorderable DOM nodes: UAX #9 reorders their text and their
// inline-edge fragments together, which is why a text-only reorder is wrong.
typedef struct BidiCharFragment {
    ViewText* text;
    View* atomic_view;
    TextRect* rect;
    int rect_slot;
    int logical_index;
    int source_index;
    int source_length;
    uint32_t codepoint;
    float width;
    float advance_width;
    float visual_x;
    bool synthetic_control;
} BidiCharFragment;

typedef struct BidiRectInfo {
    ViewText* text;
    TextRect* rect;
    int first_char;
    int char_count;
    int visible_count;
    float raw_width;
    bool collapses_spaces;
    bool line_has_bidi_inline_content;
    ViewSpan* directional_span;
} BidiRectInfo;

typedef struct BidiSpanInfo {
    ViewSpan* span;
    int logical_start;
    int logical_end;
    int depth;
    int min_visual;
    int max_visual;
    float left_edge;
    float right_edge;
    float flow_start_edge;
} BidiSpanInfo;

typedef struct BidiLineCounts {
    int chars;
    int rects;
    int spans;
    int max_depth;
    bool has_atomic;
    bool has_bidi_trigger;
    bool has_bidi_inline_content;
    bool has_bidi_layout_feature;
    bool has_css_scope_controls;
    bool has_inline_block;
} BidiLineCounts;

typedef struct BidiScopeControls {
    uint32_t start[2];
    uint32_t end[2];
    int count;
} BidiScopeControls;

static BidiScopeControls bidi_scope_controls(CssEnum mode, CssEnum direction) {
    // css writing modes §2.2: markup contributes these zero-advance codes
    // to the UAX #9 input without changing source text or box ownership.
    const static struct {
        CssEnum mode;
        uint32_t ltr[2], rtl[2], end[2];
        int count;
    } scopes[] = {
        {CSS_VALUE_EMBED, {0x202a, 0}, {0x202b, 0}, {0x202c, 0}, 1},
        {CSS_VALUE_BIDI_OVERRIDE, {0x202d, 0}, {0x202e, 0}, {0x202c, 0}, 1},
        {CSS_VALUE_ISOLATE, {0x2066, 0}, {0x2067, 0}, {0x2069, 0}, 1},
        {CSS_VALUE_ISOLATE_OVERRIDE, {0x2068, 0x202d}, {0x2068, 0x202e}, {0x202c, 0x2069}, 2},
        {CSS_VALUE_PLAINTEXT, {0x2068, 0}, {0x2068, 0}, {0x2069, 0}, 1}
    };
    BidiScopeControls controls = {};
    for (const auto& scope : scopes) {
        if (scope.mode != mode) continue;
        const uint32_t* start = direction == CSS_VALUE_RTL ? scope.rtl : scope.ltr;
        for (int i = 0; i < scope.count; i++) {
            controls.start[i] = start[i];
            controls.end[i] = scope.end[i];
        }
        controls.count = scope.count;
        break;
    }
    return controls;
}

static bool bidi_element_has_layout_feature(DomElement* element) {
    if (!element || !element->specified_style) return false;
    CssDeclaration* unicode_bidi = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_UNICODE_BIDI);
    if (unicode_bidi && unicode_bidi->value &&
        (unicode_bidi->value->type != CSS_VALUE_TYPE_KEYWORD ||
         unicode_bidi->value->data.keyword != CSS_VALUE_NORMAL)) {
        return true;
    }
    CssDeclaration* hanging_punctuation = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_HANGING_PUNCTUATION);
    return hanging_punctuation && hanging_punctuation->value &&
        (hanging_punctuation->value->type != CSS_VALUE_TYPE_KEYWORD ||
         hanging_punctuation->value->data.keyword != CSS_VALUE_NONE);
}

static bool bidi_node_has_layout_feature(DomNode* node) {
    for (DomNode* current = node; current; current = current->parent) {
        if (current->is_element() &&
            bidi_element_has_layout_feature(current->as_element())) {
            return true;
        }
    }
    return false;
}

static bool bidi_codepoint_triggers_reorder(uint32_t codepoint) {
#if RDT_HAS_FRIBIDI
    FriBidiCharType type = fribidi_get_bidi_type((FriBidiChar)codepoint);
    return !text_codepoint_has_zero_advance(codepoint) &&
           (type == FRIBIDI_TYPE_RTL || type == FRIBIDI_TYPE_AL);
#else
    return !text_codepoint_has_zero_advance(codepoint) &&
           utf_bidi_strong_class(codepoint) == 1;
#endif
}

static bool bidi_codepoint_is_format_control(uint32_t codepoint) {
#if RDT_HAS_FRIBIDI
    FriBidiCharType type = fribidi_get_bidi_type((FriBidiChar)codepoint);
    return type == FRIBIDI_TYPE_LRE || type == FRIBIDI_TYPE_RLE ||
           type == FRIBIDI_TYPE_LRO || type == FRIBIDI_TYPE_RLO ||
           type == FRIBIDI_TYPE_PDF || type == FRIBIDI_TYPE_LRI ||
           type == FRIBIDI_TYPE_RLI || type == FRIBIDI_TYPE_FSI ||
           type == FRIBIDI_TYPE_PDI;
#else
    return codepoint == 0x202A || codepoint == 0x202B ||
           codepoint == 0x202C || codepoint == 0x202D ||
           codepoint == 0x202E || codepoint == 0x2066 ||
           codepoint == 0x2067 || codepoint == 0x2068 ||
           codepoint == 0x2069;
#endif
}

bool layout_text_run_has_uniform_rtl_order(const unsigned char* text, size_t length) {
    const unsigned char* cursor = text;
    const unsigned char* end = text + length;
    bool found = false;
    while (cursor < end) {
        uint32_t codepoint = 0;
        if (!layout_utf8_next_codepoint(&cursor, end, &codepoint) ||
            bidi_codepoint_is_format_control(codepoint) || utf_bidi_strong_class(codepoint) < 0) return false;
        if (bidi_codepoint_triggers_reorder(codepoint)) found = true;
        else if (!is_space(codepoint) && !text_codepoint_has_zero_advance(codepoint)) return false;
    }
    return found;
}

static bool bidi_is_line_text_rect(ViewText* text, TextRect* rect, int line_number) {
    return text && rect && rect->line_number == line_number && rect->length > 0 &&
           text->text_data();
}

static uint32_t bidi_marker_representative_codepoint(CssEnum direction) {
    // CSS Writing Modes: an isolated marker follows its inherited direction
    // for bidi ordering, even when its content mixes strong LTR and RTL text.
    return direction == CSS_VALUE_RTL ? 0x0627 : 0x0041;
}

int layout_find_first_strong_direction(DomNode* node, bool skip_explicit_dir) {
    return dom_find_strong_direction(node, skip_explicit_dir, true);
}

int layout_find_last_strong_direction(DomNode* node, bool skip_explicit_dir) {
    return dom_find_strong_direction(node, skip_explicit_dir, false);
}

typedef struct BidiParagraphSearch {
    DomText* anchor;
    size_t anchor_byte;
    size_t start_byte, end_byte;
    int first_strong;
    int isolate_depth;
    bool contains_anchor;
    bool complete;
} BidiParagraphSearch;

static void bidi_paragraph_boundary(BidiParagraphSearch* search) {
    if (search->contains_anchor) search->complete = true;
    else {
        search->first_strong = 0;
        search->isolate_depth = 0;
    }
}

static void bidi_scan_paragraph(DomNode* node, BidiParagraphSearch* search, bool isolated) {
    if (!node || search->complete) return;
    if (node->is_text()) {
        DomText* text = node->as_text();
        const unsigned char* start = text->text_data();
        if (!start) return;
        const unsigned char* cursor = start;
        const unsigned char* end = start + text->length;
        bool preserve_breaks = !layout_white_space_collapses_newlines(get_white_space_value(node));
        while (cursor < end && !search->complete) {
            const unsigned char* before = cursor;
            uint32_t codepoint = 0;
            if (!layout_utf8_next_codepoint(&cursor, end, &codepoint)) continue;
            size_t byte = (size_t)(before - start);
            if (text == search->anchor && search->anchor_byte >= byte &&
                search->anchor_byte < (size_t)(cursor - start)) search->contains_anchor = true;
            bool paragraph_break = codepoint == 0x2029 ||
                (preserve_breaks && layout_text_is_segment_break(codepoint));
            if (paragraph_break) {
                if (text == search->anchor) {
                    if (search->contains_anchor) search->end_byte = (size_t)(cursor - start);
                    else search->start_byte = (size_t)(cursor - start);
                }
                bidi_paragraph_boundary(search);
                continue;
            }
            // P2 ignores the contents of both CSS and source Unicode isolates.
            if (isolated) continue;
            if (codepoint >= 0x2066 && codepoint <= 0x2068) search->isolate_depth++;
            else if (codepoint == 0x2069 && search->isolate_depth > 0) search->isolate_depth--;
            else if (!search->isolate_depth && !search->first_strong)
                search->first_strong = utf_bidi_strong_class(codepoint);
        }
        if (text == search->anchor && search->anchor_byte == text->length)
            search->contains_anchor = true;
        return;
    }
    if (!node->is_element()) return;
    DomElement* element = node->as_element();
    DisplayValue display = resolve_display_value(element);
    if (layout_display_is_none(display) || layout_element_is_abs_or_fixed(element) ||
        layout_element_is_floated(element)) return;
    if (element->tag() == MARKUP_NAME_BR) { bidi_paragraph_boundary(search); return; }
    if (layout_intrinsic_element_is_atomic_inline(element)) return;
    if (display.outer != CSS_VALUE_INLINE && display.outer != CSS_VALUE_CONTENTS) {
        // an intervening block ends the parent's paragraph; its contents have
        // an independent formatting context and cannot supply this P2 level.
        bidi_paragraph_boundary(search);
        return;
    }
    if (display.outer != CSS_VALUE_CONTENTS) isolated |= layout_unicode_bidi_isolate(
        layout_inline_boundary_keyword(element, CSS_PROPERTY_UNICODE_BIDI,
            CSS_VALUE_NORMAL, CSS_VALUE_NORMAL));
    for (DomNode* child = element->first_child; child && !search->complete; child = child->next_sibling)
        bidi_scan_paragraph(child, search, isolated);
}

static CssEnum bidi_plaintext_paragraph_direction(DomElement* scope, DomText* anchor,
        size_t anchor_byte, LayoutPlaintextParagraphCache* cache = nullptr) {
    if (cache && cache->scope == scope && cache->text == anchor &&
        anchor_byte >= cache->start_byte && anchor_byte < cache->end_byte) return cache->direction;
    BidiParagraphSearch search = {};
    search.anchor = anchor;
    search.anchor_byte = anchor_byte;
    search.end_byte = anchor ? (size_t)anchor->length + 1 : 0;
    search.contains_anchor = anchor == nullptr;
    for (DomNode* child = scope->first_child; child && !search.complete; child = child->next_sibling)
        bidi_scan_paragraph(child, &search, false);
    CssEnum direction = search.contains_anchor && search.first_strong > 0 ? CSS_VALUE_RTL : CSS_VALUE_LTR;
    if (cache && anchor) {
        cache->scope = lam::up(scope);
        cache->text = lam::up(anchor);
        cache->start_byte = search.start_byte;
        cache->end_byte = search.end_byte;
        cache->direction = direction;
    }
    return direction;
}

CssEnum layout_resolve_plaintext_direction(DomElement* element, CssEnum fallback) {
    return element ? bidi_plaintext_paragraph_direction(element, nullptr, 0) : fallback;
}

static bool bidi_find_line_anchor(View* view, int line_number, DomText** anchor, size_t* byte) {
    for (View* current = view; current; current = current->next()) {
        if (current->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(current);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (!bidi_is_line_text_rect(text, rect, line_number)) continue;
                *anchor = text;
                *byte = (size_t)rect->start_index;
                return true;
            }
        } else if (current->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(current);
            if (bidi_find_line_anchor(span->first_child, line_number, anchor, byte)) return true;
        }
    }
    return false;
}

CssEnum layout_resolve_line_base_direction(LayoutContext* lycon) {
    if (!lycon) return CSS_VALUE_LTR;
    ViewBlock* line_block = lycon->block.flow_box;
    if (!line_block && lycon->view && lycon->view->is_block()) {
        line_block = lam::view_require_block(lycon->view);
    } else if (!line_block && lycon->view) {
        line_block = layout_nearest_block_ancestor(lycon->view->parent_view());
    }
    if (!line_block) return CSS_VALUE_LTR;
    DomText* anchor = lycon->line.last_text_view;
    size_t byte = lycon->line.last_text_rect ? (size_t)lycon->line.last_text_rect->start_index : 0;
    if (!bidi_is_line_text_rect(lycon->line.last_text_view, lycon->line.last_text_rect,
            lycon->block.line_number) &&
        !bidi_find_line_anchor(line_block->first_child, lycon->block.line_number, &anchor, &byte))
        return CSS_VALUE_LTR;
    // soft wraps reuse the same source paragraph, including strong text before
    // this line or after its current source anchor. P3 defaults neutrals to LTR.
    return bidi_plaintext_paragraph_direction(line_block->as_element(), anchor, byte,
        &lycon->block.plaintext_paragraph);
}

static ViewBlock* bidi_static_inline_candidate(View* view, int line_number) {
    ViewBlock* block = lam::view_as_block(view);
    return block && block->position &&
        block->positionp()->static_inline_bidi_candidate &&
        block->inline_line_number == line_number ? block : nullptr;
}

static void bidi_count_views(View* view, int line_number, int depth,
                             CssEnum direction, BidiLineCounts* counts) {
    auto visit = [&](View* current, int current_depth) -> bool {
        if (current->view_type == RDT_VIEW_NONE) return false;
        if (layout_view_is_out_of_flow(current)) {
            if (bidi_static_inline_candidate(current, line_number)) {
                counts->chars++;
                counts->has_bidi_layout_feature = true;
            }
            return false;
        }
        if (current->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(current);
            bool node_has_layout_feature = bidi_node_has_layout_feature(current);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (!bidi_is_line_text_rect(text, rect, line_number)) continue;
                const unsigned char* cursor = text->text_data() + rect->start_index;
                int remaining = rect->length;
                while (remaining > 0) {
                    uint32_t codepoint = 0;
                    int consumed = utf8_decode((const char*)cursor,
                        (size_t)remaining, &codepoint);
                    if (consumed <= 0 || consumed > remaining) consumed = 1;
                    codepoint = layout_text_resolve_css_whitespace(codepoint);
                    if (layout_text_is_segment_break(codepoint)) {
                        cursor += consumed;
                        remaining -= consumed;
                        continue;
                    }
                    counts->has_bidi_layout_feature =
                        counts->has_bidi_layout_feature ||
                        codepoint == 0x0009 || node_has_layout_feature;
                    bool bidi_reorder = bidi_codepoint_triggers_reorder(codepoint);
                    bool bidi_format_control = bidi_codepoint_is_format_control(codepoint);
                    counts->chars++;
                    counts->has_bidi_trigger = counts->has_bidi_trigger ||
                        bidi_reorder || bidi_format_control;
                    // source controls can open before an inline box and close
                    // after it; their scope belongs to the whole paragraph.
                    counts->has_bidi_inline_content =
                        counts->has_bidi_inline_content ||
                        (current_depth > 0 &&
                         (bidi_reorder || bidi_format_control));
                    cursor += consumed;
                    remaining -= consumed;
                }
                counts->rects++;
            }
            return false;
        }
        if (current->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(current);
            if (span->blk &&
                (span->block()->unicode_bidi != CSS_VALUE_NORMAL ||
                 span->block()->direction != direction)) {
                counts->has_bidi_layout_feature = true;
            }
            counts->spans++;
            if (layout_inline_span_isolate(span)) {
                // CSS Writing Modes: an isolate participates in the parent
                // paragraph as one atomic inline item, not as its text runs.
                counts->chars++;
                return false;
            }
            if (span->blk) {
                int controls = bidi_scope_controls(
                    span->block()->unicode_bidi, span->block()->direction).count;
                counts->chars += 2 * controls;
                counts->has_css_scope_controls |= controls > 0;
            }
            if (current_depth > counts->max_depth) counts->max_depth = current_depth;
            return true;
        }
        if (current->view_type == RDT_VIEW_MARKER) {
            MarkerProp* marker = current->as_element()->marker_prop();
            if (marker && !marker->is_outside && marker->width > 0.0f) {
                counts->chars++;
                counts->has_bidi_trigger = counts->has_bidi_trigger ||
                    bidi_codepoint_triggers_reorder(
                        bidi_marker_representative_codepoint(direction));
            }
            return false;
        }
        if (current->view_type == RDT_VIEW_INLINE_BLOCK) {
            if (!layout_inline_span_has_content_on_line(current, line_number)) return false;
            counts->has_inline_block = true;
            counts->chars++;
            return false;
        }
        if (current->view_type != RDT_VIEW_BR) counts->has_atomic = true;
        return false;
    };
    auto no_leave = [](View*, int) {};
    // count neutral static anchors without entering their independent flow.
    layout_walk_view_tree(view, visit, no_leave, false, depth);
}

static float bidi_span_edge_width(ViewSpan* span, bool left) {
    // A display:contents span has no box, so its decorations cannot shift bidi text.
    if (!span || !span->bound || span->display.outer == CSS_VALUE_CONTENTS) return 0.0f;
    BoundaryProp* boundary = span->boundary_mut();
    float border = 0.0f;
    if (boundary->border) {
        border = left ? boundary->border->width.left : boundary->border->width.right;
    }
    float padding = left ? boundary->padding.left : boundary->padding.right;
    float margin = left ? boundary->margin.left : boundary->margin.right;
    return margin + padding + border;
}

static bool bidi_span_owns_source_edge(ViewSpan* span, int line_number, bool end) {
    View* leaf = layout_inline_span_edge_leaf(span, end);
    if (!leaf) return true;
    if (leaf->view_type != RDT_VIEW_TEXT) return leaf->inline_line_number == line_number;
    ViewText* text = lam::view_require_text(leaf);
    TextRect* edge = text->rect;
    if (!edge) return false;
    if (end) {
        while (edge->next) edge = edge->next;
        size_t remaining = (size_t)max(0, text->length - edge->start_index - edge->length);
        if (remaining && !layout_text_is_collapsible_whitespace(leaf)) {
            const unsigned char* cursor = text->text_data() + edge->start_index + edge->length;
            const unsigned char* limit = cursor + remaining;
            if (!layout_white_space_collapses(get_white_space_value(leaf))) return false;
            while (cursor < limit) if (!is_space(*cursor++)) return false;
        }
    }
    return edge->line_number == line_number;
}

static BidiCharFragment* bidi_append_atomic_fragment(
    BidiCharFragment* chars, int* cursor, View* view, uint32_t codepoint,
    float width) {
    if (!chars || !cursor) return nullptr;
    BidiCharFragment* fragment = &chars[(*cursor)++];
    fragment->text = nullptr;
    fragment->atomic_view = view;
    fragment->rect = nullptr;
    fragment->rect_slot = -1;
    fragment->logical_index = *cursor - 1;
    fragment->codepoint = codepoint;
    fragment->width = width;
    fragment->advance_width = width;
    fragment->visual_x = 0.0f;
    return fragment;
}

static void bidi_append_controls(BidiCharFragment* chars, int* cursor,
                                 const uint32_t* controls, int count) {
    for (int i = 0; i < count; i++) {
        BidiCharFragment* fragment = bidi_append_atomic_fragment(
            chars, cursor, nullptr, controls[i], 0.0f);
        if (fragment) fragment->synthetic_control = true;
    }
}

static float bidi_char_raw_width(LayoutContext* lycon, ViewText* text,
                                 uint32_t codepoint, bool use_fallback_metrics) {
    if (!lycon || !text || text_codepoint_has_zero_advance(codepoint) ||
        !text->font) {
        return 0.0f;
    }
    if (layout_unicode_space_is_synthesized(text->font->font_handle, codepoint)) {
        // bidi redistribution must retain the selected-face synthetic space width.
        return layout_measure_font_unicode_space_advance(text->font->font_handle,
            text->font, codepoint, ui_context_raster_scale(lycon->ui_context),
            text->font->font_size) + text->font->letter_spacing;
    }
    float width = use_fallback_metrics
        ? (codepoint == 0x0009
            ? layout_measure_space_advance(
                lycon, text->font->font_handle, text->font)
            : layout_measure_glyph_advance(
                lycon, text->font->font_handle, text->font, codepoint))
        : font_measure_char(text->font->font_handle, codepoint);
    if (!use_fallback_metrics && codepoint != 0x0009 &&
        font_get_glyph_index(text->font->font_handle, codepoint) == 0) {
        // feature-only lines reuse the established text rect to distribute
        // missing-glyph widths; they do not need a second fallback measure.
        width = 0.0f;
    }
    return width + text->font->letter_spacing;
}

static bool bidi_is_collapsible_space(uint32_t codepoint) {
    return codepoint == 0x0009 || codepoint == 0x000A || codepoint == 0x000C ||
           codepoint == 0x000D || codepoint == 0x0020;
}

static ViewSpan* bidi_text_directional_span(ViewText* text, CssEnum direction) {
    for (DomNode* current = text ? text->parent : nullptr;
         current && current->is_element(); current = current->parent) {
        DomElement* element = current->as_element();
        if (element->view_type == RDT_VIEW_INLINE && element->blk &&
            element->block()->direction != direction) {
            return lam::view_require<RDT_VIEW_INLINE>(static_cast<View*>(element));
        }
    }
    return nullptr;
}

static void bidi_fill_views(LayoutContext* lycon, View* view, int line_number, int depth,
                            BidiCharFragment* chars, BidiRectInfo* rects,
                            BidiSpanInfo* spans, int* char_cursor,
                            int* rect_cursor, int* span_cursor,
                            CssEnum direction, bool use_fallback_metrics) {
    for (View* current = view; current; current = current->next()) {
        if (current->view_type == RDT_VIEW_NONE) continue;
        if (layout_view_is_out_of_flow(current)) {
            if (ViewBlock* block = bidi_static_inline_candidate(current, line_number)) {
                // CSS Position's inline static rectangle has zero inline size;
                // its neutral bidi position must not affect line width or height.
                bidi_append_atomic_fragment(chars, char_cursor, block, 0xFFFC, 0.0f);
            }
            continue;
        }
        if (current->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(current);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (!bidi_is_line_text_rect(text, rect, line_number)) continue;
                BidiRectInfo* rect_info = &rects[(*rect_cursor)++];
                rect_info->text = text;
                rect_info->rect = rect;
                rect_info->first_char = *char_cursor;
                rect_info->char_count = 0;
                rect_info->visible_count = 0;
                rect_info->raw_width = 0.0f;
                rect_info->collapses_spaces = layout_white_space_collapses(
                    get_white_space_value(static_cast<DomNode*>(text)));
                rect_info->line_has_bidi_inline_content = false;
                rect_info->directional_span = bidi_text_directional_span(text, direction);
                bool rect_use_fallback_metrics = use_fallback_metrics &&
                    layout_text_autospace_flags(lycon, text) == 0;

                const unsigned char* cursor = text->text_data() + rect->start_index;
                int remaining = rect->length;
                while (remaining > 0) {
                    uint32_t codepoint = 0;
                    int consumed = utf8_decode((const char*)cursor,
                        (size_t)remaining, &codepoint);
                    if (consumed <= 0 || consumed > remaining) {
                        codepoint = *cursor;
                        consumed = 1;
                    }
                    codepoint = layout_text_resolve_css_whitespace(codepoint);
                    if (layout_text_is_segment_break(codepoint)) {
                        cursor += consumed;
                        remaining -= consumed;
                        continue;
                    }
                    BidiCharFragment* fragment = &chars[(*char_cursor)++];
                    fragment->text = text;
                    fragment->rect = rect;
                    fragment->rect_slot = *rect_cursor - 1;
                    fragment->logical_index = *char_cursor - 1;
                    fragment->source_index = rect->start_index + rect->length - remaining;
                    fragment->source_length = consumed;
                    fragment->codepoint = codepoint;
                    fragment->width = bidi_char_raw_width(
                        lycon, text, codepoint, rect_use_fallback_metrics);
                    fragment->advance_width = fragment->width;
                    fragment->visual_x = 0.0f;
                    rect_info->char_count++;
                    rect_info->raw_width += fragment->width;
                    if (fragment->width > 0.0f) rect_info->visible_count++;
                    cursor += consumed;
                    remaining -= consumed;
                }
            }
            continue;
        }
        if (current->view_type == RDT_VIEW_INLINE) {
            BidiSpanInfo* span_info = &spans[(*span_cursor)++];
            span_info->span = lam::view_require<RDT_VIEW_INLINE>(current);
            span_info->logical_start = *char_cursor;
            span_info->logical_end = *char_cursor - 1;
            span_info->depth = depth;
            span_info->min_visual = INT_MAX;
            span_info->max_visual = -1;
            span_info->left_edge = bidi_span_edge_width(span_info->span, true);
            span_info->right_edge = bidi_span_edge_width(span_info->span, false);
            bool rtl = layout_inherited_direction_is_rtl(span_info->span->first_placed_child());
            bool clone = inline_box_decoration_break_value(span_info->span) == CSS_VALUE_CLONE;
            bool owns_start = clone || bidi_span_owns_source_edge(span_info->span, line_number, false);
            bool owns_end = clone || bidi_span_owns_source_edge(span_info->span, line_number, true);
            // CSS Break §5.4 slices inline decorations at source boundaries, including RTL continuations.
            if (!(rtl ? owns_end : owns_start)) span_info->left_edge = 0.0f;
            if (!(rtl ? owns_start : owns_end)) span_info->right_edge = 0.0f;
            span_info->flow_start_edge = rtl ? span_info->right_edge : span_info->left_edge;
            bool isolate = layout_inline_span_isolate(span_info->span);
            if (isolate) {
                // CSS Writing Modes: an isolated inline is already represented
                // by its complete atomic border box, including inline padding.
                span_info->left_edge = 0.0f;
                span_info->right_edge = 0.0f;
                span_info->flow_start_edge = 0.0f;
                bidi_append_atomic_fragment(
                    chars, char_cursor, static_cast<View*>(span_info->span),
                    0xFFFC, span_info->span->width);
                span_info->logical_end = *char_cursor - 1;
                continue;
            }
            BidiScopeControls controls = span_info->span->blk
                ? bidi_scope_controls(span_info->span->block()->unicode_bidi,
                    span_info->span->block()->direction) : BidiScopeControls{};
            bidi_append_controls(chars, char_cursor, controls.start, controls.count);
            span_info->logical_start = *char_cursor;
            bidi_fill_views(lycon, span_info->span->first_child, line_number, depth + 1,
                            chars, rects, spans, char_cursor, rect_cursor, span_cursor,
                            direction, use_fallback_metrics);
            span_info->logical_end = *char_cursor - 1;
            bidi_append_controls(chars, char_cursor, controls.end, controls.count);
            continue;
        }
        if (current->view_type == RDT_VIEW_MARKER) {
            MarkerProp* marker = current->as_element()->marker_prop();
            if (marker && !marker->is_outside && marker->width > 0.0f) {
                bidi_append_atomic_fragment(
                    chars, char_cursor, current,
                    bidi_marker_representative_codepoint(direction), marker->width);
            }
            continue;
        }
        if (current->view_type == RDT_VIEW_INLINE_BLOCK) {
            if (!layout_inline_span_has_content_on_line(current, line_number)) continue;
            // UAX #9 treats an atomic inline box as an object, not as a letter
            // from the box's fallback font; U+FFFC gives it neutral bidi type.
            bidi_append_atomic_fragment(chars, char_cursor, current, 0xFFFC,
                                        current->width);
            continue;
        }
    }
}

static void bidi_scale_rect_widths(BidiCharFragment* chars, BidiRectInfo* rects,
                                    int rect_count) {
    for (int i = 0; i < rect_count; i++) {
        BidiRectInfo* rect_info = &rects[i];
        bool trimmed_hidden_space = false;
        float trimmed_space_width = 0.0f;
        float original_raw_width = rect_info->raw_width;
        // line_break() trims a trailing collapsible space from the advance but
        // keeps it in TextRect::length; feeding that hidden character to UAX #9
        // would incorrectly widen the DOM range across reordered fragments.
        for (int j = rect_info->char_count - 1; j >= 0; j--) {
            BidiCharFragment* fragment = &chars[rect_info->first_char + j];
            // anonymous text needs this only when a sibling inline bidi run
            // makes the line's logical and visual ranges diverge; ordinary
            // hanging-punctuation and whitespace lines must keep their space.
            if (!rect_info->collapses_spaces ||
                !bidi_is_collapsible_space(fragment->codepoint) ||
                fragment->width <= 0.0f ||
                rect_info->raw_width <= 0.0f ||
                (!rect_info->directional_span &&
                 !rect_info->line_has_bidi_inline_content) ||
                (!rect_info->directional_span &&
                 rect_info->raw_width - fragment->width <
                     rect_info->rect->width - 0.01f)) {
                break;
            }
            rect_info->raw_width -= fragment->width;
            trimmed_space_width += fragment->width;
            fragment->width = 0.0f;
            rect_info->visible_count--;
            trimmed_hidden_space = true;
        }
        if (rect_info->raw_width > 0.0f) {
            // keep a visible glyph at its measured width after removing a
            // trailing space that remains in the DOM text range.
            float target_width = rect_info->rect->width;
            bool space_was_included_in_rect =
                original_raw_width <= target_width + 0.01f;
            if (trimmed_hidden_space && space_was_included_in_rect) {
                // CSS Writing Modes bidi box construction keeps the directional
                // inline edge while the text rect excludes its collapsed space.
                layout_extend_fragment_union(rect_info->directional_span,
                    FRAGMENT_UNION_INLINE, rect_info->rect->x,
                    rect_info->rect->x + rect_info->rect->width,
                    rect_info->rect->y,
                    rect_info->rect->y + rect_info->rect->height);
                target_width = max(target_width - trimmed_space_width, 0.0f);
            }
            float scale = target_width / rect_info->raw_width;
            for (int j = 0; j < rect_info->char_count; j++) {
                BidiCharFragment* fragment = &chars[rect_info->first_char + j];
                fragment->width *= scale;
                if (trimmed_hidden_space && !space_was_included_in_rect &&
                    bidi_is_collapsible_space(fragment->codepoint)) {
                    fragment->advance_width = 0.0f;
                } else {
                    fragment->advance_width *= scale;
                }
            }
        } else {
            // The fallback font can have no glyph advance for a bidi script,
            // while the normal text pass still measured the complete range.
            // Preserve that range by distributing its established width over
            // the renderable codepoints before visual reordering.
            rect_info->visible_count = 0;
            for (int j = 0; j < rect_info->char_count; j++) {
                uint32_t codepoint = chars[rect_info->first_char + j].codepoint;
                if (!text_codepoint_has_zero_advance(codepoint)) {
                    rect_info->visible_count++;
                }
            }
            if (rect_info->visible_count > 0) {
            float width = rect_info->rect->width / rect_info->visible_count;
            for (int j = 0; j < rect_info->char_count; j++) {
                    if (!text_codepoint_has_zero_advance(
                            chars[rect_info->first_char + j].codepoint)) {
                        chars[rect_info->first_char + j].width = width;
                        chars[rect_info->first_char + j].advance_width = width;
                }
            }
            }
        }
    }
}

// use the pass's stable index type here; FriBidi is optional and its type is unavailable
// when the platform omits the header even though these shared helpers still compile.
static void bidi_update_span_visual_ranges(BidiCharFragment* chars,
                                            const int* visual_to_logical,
                                            BidiSpanInfo* spans, int span_count,
                                            int char_count) {
    for (int visual = 0; visual < char_count; visual++) {
        int logical = visual_to_logical[visual];
        if (logical < 0 || logical >= char_count) continue;
        if (chars[logical].synthetic_control) continue;
        for (int span_index = 0; span_index < span_count; span_index++) {
            BidiSpanInfo* span = &spans[span_index];
            if (logical < span->logical_start || logical > span->logical_end) continue;
            span->min_visual = min(span->min_visual, visual);
            span->max_visual = max(span->max_visual, visual);
        }
    }
}

static void bidi_publish_paint_runs(LayoutContext* lycon, BidiCharFragment* chars,
                                    BidiRectInfo* rects, int rect_count, const int* levels) {
    if (!lycon->doc || !lycon->doc->view_tree ||
        layout_block_inline_axis_is_vertical(lycon->block.flow_box)) return;
    ViewTree* tree = lycon->doc->view_tree;
    for (int i = 0; i < rect_count; i++) {
        BidiRectInfo* info = &rects[i];
        TextRect* source = info->rect;
        tree->recycle_text_rects(source->bidi_runs);
        source->bidi_runs = nullptr;
        bool reversed = false;
        for (int j = 0; j < info->char_count; j++) {
            int index = info->first_char + j;
            reversed |= chars[index].width > 0.0f && (levels[index] & 1);
        }
        if (!reversed) continue;
        TextRect* previous = nullptr;
        for (int begin = 0; begin < info->char_count;) {
            int first = info->first_char + begin;
            if (bidi_codepoint_is_format_control(chars[first].codepoint)) { begin++; continue; }
            int level = levels[first];
            int end = begin + 1;
            while (end < info->char_count &&
                (bidi_codepoint_is_format_control(chars[info->first_char + end].codepoint) ||
                 levels[info->first_char + end] == level)) end++;
            float left = FLT_MAX, right = -FLT_MAX;
            for (int j = begin; j < end; j++) {
                BidiCharFragment* part = &chars[info->first_char + j];
                if (part->width <= 0.0f) continue;
                left = min(left, part->visual_x);
                right = max(right, part->visual_x + part->width);
            }
            if (left != FLT_MAX) {
                TextRect* run = tree->alloc_text_rect();
                if (!run) break;
                *run = *source;
                run->bidi_runs = nullptr;
                run->next = nullptr;
                // relative paint geometry survives parent shifts and retained replay;
                // the original source/query rectangle keeps its existing visual union.
                run->x = left - source->x;
                run->y = 0.0f;
                run->width = right - left;
                run->start_index = chars[first].source_index;
                BidiCharFragment* last = &chars[info->first_char + end - 1];
                run->length = last->source_index + last->source_length - run->start_index;
                run->hyphenated_leading_bytes = run->hyphenated_trailing_bytes = 0;
                run->trailing_whitespace_bytes = 0;
                run->has_trailing_hyphen = run->has_trailing_ellipsis = false;
                run->bidi_reversed = (level & 1) != 0;
                if (previous) previous->next = lam::own(run);
                else source->bidi_runs = lam::own(run);
                previous = run;
            }
            begin = end;
        }
        if (previous) {
            previous->has_trailing_hyphen = source->has_trailing_hyphen;
            previous->has_trailing_ellipsis = source->has_trailing_ellipsis;
        }
    }
}

static float bidi_line_origin(BidiCharFragment* chars, BidiSpanInfo* spans,
                              int char_count, int span_count) {
    for (int i = 0; i < char_count; i++) {
        if (chars[i].width <= 0.0f) continue;
        float origin = chars[i].atomic_view
            ? chars[i].atomic_view->x : chars[i].rect->x;
        for (int span_index = 0; span_index < span_count; span_index++) {
            BidiSpanInfo* span = &spans[span_index];
            if (i >= span->logical_start && i <= span->logical_end) {
                origin -= span->flow_start_edge;
            }
        }
        return origin;
    }
    return 0.0f;
}

static void bidi_place_visual_line(LayoutContext* lycon,
                                   BidiCharFragment* chars,
                                   BidiRectInfo* rects, int rect_count,
                                   BidiSpanInfo* spans, int span_count,
                                   const int* visual_to_logical,
                                   int char_count, int max_depth,
                                   const int* levels, bool use_line_flow_origin) {
    float cursor = bidi_line_origin(chars, spans, char_count, span_count);
    if (use_line_flow_origin) {
        // atomic boxes mix provisional RTL positions with logical text;
        // their paragraph starts at the float/indent-adjusted flow origin.
        cursor = lycon->line.has_float_intrusion ? lycon->line.effective_left
            : lycon->line.left + lycon->line.text_indent_advance;
    }
    for (int visual = 0; visual < char_count; visual++) {
        for (int depth = 0; depth <= max_depth; depth++) {
            for (int span_index = 0; span_index < span_count; span_index++) {
                BidiSpanInfo* span = &spans[span_index];
                if (span->depth == depth && span->min_visual == visual) {
                    cursor += span->left_edge;
                }
            }
        }

        int logical = visual_to_logical[visual];
        if (logical >= 0 && logical < char_count) {
            chars[logical].visual_x = cursor;
            if (chars[logical].atomic_view) {
                View* atomic_view = chars[logical].atomic_view;
                ViewBlock* positioned = bidi_static_inline_candidate(
                    atomic_view, lycon->block.line_number);
                float atomic_shift = positioned ? 0.0f : cursor - atomic_view->x;
                if (positioned) {
                    layout_place_bidi_static_inline(positioned, cursor, levels[logical]);
                } else atomic_view->x = cursor;
                if (atomic_shift != 0.0f && atomic_view->view_type == RDT_VIEW_INLINE) {
                    // moving an isolated inline must carry its generated text
                    // with it; otherwise only the wrapper changes position.
                    layout_shift_view_children(atomic_view, atomic_shift, 0.0f);
                }
            }
            if (chars[logical].advance_width > 0.0f) {
                cursor += chars[logical].advance_width;
            }
        }

        for (int depth = max_depth; depth >= 0; depth--) {
            for (int span_index = 0; span_index < span_count; span_index++) {
                BidiSpanInfo* span = &spans[span_index];
                if (span->depth == depth && span->max_visual == visual) {
                    cursor += span->right_edge;
                }
            }
        }
    }

    ScratchScope scope(&lycon->scratch);
    float* min_x = scope.array<float>(rect_count);
    float* max_x = scope.array<float>(rect_count);
    if (!min_x || !max_x) return;
    for (int i = 0; i < rect_count; i++) {
        min_x[i] = FLT_MAX;
        max_x[i] = -FLT_MAX;
    }
    for (int i = 0; i < char_count; i++) {
        int slot = -1;
        for (int rect_index = 0; rect_index < rect_count; rect_index++) {
            if (rects[rect_index].rect == chars[i].rect) {
                slot = rect_index;
                break;
            }
        }
        if (slot < 0) continue;
        // a format-control-only range has no visual bounds, but controls in a
        // range with glyphs delimit that range's UAX #9 visual union; ordinary
        // zero-width line breaks must not expand an RTL text range.
        if (chars[i].width <= 0.0f &&
            (!text_codepoint_has_zero_advance(chars[i].codepoint) ||
             rects[slot].visible_count == 0)) continue;
        // text ranges use shaped advances (CSSOM View §9); bitmap ink belongs to paint.
        min_x[slot] = min(min_x[slot], chars[i].visual_x);
        max_x[slot] = max(max_x[slot], chars[i].visual_x + chars[i].width);
    }
    for (int i = 0; i < rect_count; i++) {
        if (min_x[i] == FLT_MAX) continue;
        rects[i].rect->x = min_x[i];
        rects[i].rect->width = max_x[i] - min_x[i];
    }
    for (int span_index = 0; span_index < span_count; span_index++) {
        BidiSpanInfo* span_info = &spans[span_index];
        if (span_info->logical_start < 0) continue;
        if (span_info->logical_start > span_info->logical_end) {
            // a normal empty inline is transparent to UAX #9; anchor its
            // boundary at the following run's resolved inline-start edge.
            if (span_info->logical_start < char_count) {
                int logical = span_info->logical_start;
                span_info->span->x = chars[logical].visual_x +
                    ((levels[logical] & 1) ? chars[logical].advance_width : 0.0f);
            }
            continue;
        }
        bool has_visible_content = false;
        for (int logical = span_info->logical_start;
             logical <= span_info->logical_end; logical++) {
            if (chars[logical].width > 0.0f) {
                has_visible_content = true;
                break;
            }
        }
        if (has_visible_content) continue;

        // CSS Inline exposes a zero-width inline at the preceding logical
        // fragment's line position; its format-control bidi position is not a
        // painted glyph position after UAX #9 reordering.
        float anchor_x = span_info->span->x;
        bool found_anchor = false;
        if (lycon->line.inline_base_direction == CSS_VALUE_LTR) {
            for (int logical = span_info->logical_start;
                 logical <= span_info->logical_end && !found_anchor; logical++) {
                anchor_x = chars[logical].visual_x;
                found_anchor = true;
            }
        } else {
            for (int logical = span_info->logical_start - 1;
                 logical >= 0 && !found_anchor; logical--) {
                if (chars[logical].rect) {
                    anchor_x = chars[logical].rect->x;
                    found_anchor = true;
                }
            }
        }
        if (!found_anchor && lycon->line.inline_base_direction == CSS_VALUE_RTL) {
            for (int logical = span_info->logical_end + 1;
                 logical < char_count && !found_anchor; logical++) {
                if (chars[logical].rect) {
                    anchor_x = chars[logical].rect->x;
                    found_anchor = true;
                }
            }
        }
        if (found_anchor) {
            float shift = anchor_x - span_info->span->x;
            span_info->span->x = anchor_x;
            if (shift != 0.0f) {
                layout_shift_view_children(static_cast<View*>(span_info->span),
                                            shift, 0.0f);
            }
        }
    }
}

static void bidi_refresh_bounds(View* view, int line_number,
                                BidiSpanInfo* spans, int span_count) {
    auto visit = [&](View* current, int) -> bool {
        if (current->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(current);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (rect->line_number == line_number) {
                    adjust_text_bounds(text);
                    break;
                }
            }
        }
        return current->view_type == RDT_VIEW_INLINE;
    };
    auto no_leave = [](View*, int) {};
    layout_walk_view_tree(view, visit, no_leave, false);
    (void)spans;
    (void)span_count;
}

static void bidi_position_forced_breaks(View* view, int line_number,
                                         CssEnum direction, float left,
                                         float right) {
    if (!view) return;
    for (View* current = view; current; current = current->next()) {
        if (current->view_type == RDT_VIEW_BR) {
            if (current->inline_line_number == line_number) {
                // CSS Writing Modes places a forced break at the visual end
                // of its bidi paragraph, not at the logical child-list end.
                current->x = direction == CSS_VALUE_RTL ? left : right;
            }
            continue;
        }
        if (current->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(current);
            bidi_position_forced_breaks(span->first_child, line_number,
                                         direction, left, right);
        }
    }
}

static void bidi_resolve_line(LayoutContext* lycon, View* root,
                               CssEnum line_direction, bool isolated_paragraph,
                               CssEnum root_mode = CSS_VALUE_NORMAL) {
    if (!root) return;
    BidiLineCounts counts = {};
    bidi_count_views(root, lycon->block.line_number, 0,
                     line_direction, &counts);
    bool root_override = root_mode == CSS_VALUE_BIDI_OVERRIDE ||
        root_mode == CSS_VALUE_ISOLATE_OVERRIDE;
    BidiScopeControls root_controls = bidi_scope_controls(
        root_override ? CSS_VALUE_BIDI_OVERRIDE : CSS_VALUE_NORMAL, line_direction);
    counts.chars += 2 * root_controls.count;
    counts.has_bidi_layout_feature |= root_controls.count > 0;
    counts.has_css_scope_controls |= root_controls.count > 0;
    if (counts.has_inline_block && counts.has_bidi_layout_feature &&
        !counts.has_bidi_trigger && !counts.has_css_scope_controls &&
        line_direction != CSS_VALUE_RTL) {
        // keep atomic lines without a directional run in their native geometry;
        // orthogonal descendants do not need a second UAX #9 placement pass.
        return;
    }
    // Ordinary LTR lines already have correct fragment geometry; UAX #9 must
    // only rewrite lines whose bidi data can change visual order.
    // an RTL paragraph reorders neutral spaces without a strong RTL character.
    // tabs, explicit bidi modes and hanging punctuation also need fragment updates.
    if (counts.chars <= 0 || counts.rects <= 0 || counts.has_atomic ||
        (!counts.has_bidi_trigger && !counts.has_bidi_layout_feature &&
         line_direction != CSS_VALUE_RTL)) return;

    // every bidi temporary of this line is released when the scope ends
    ScratchScope scope(&lycon->scratch);
    BidiCharFragment* chars = scope.array_zero<BidiCharFragment>(counts.chars);
    BidiRectInfo* rects = scope.array_zero<BidiRectInfo>(counts.rects);
    BidiSpanInfo* spans = scope.array_zero<BidiSpanInfo>(counts.spans);
    int* visual_to_logical = scope.array<int>(counts.chars);
    int* levels = scope.array<int>(counts.chars);
    signed char* bidi_classes = scope.array<signed char>(counts.chars);
    // A block's anonymous inline content can have no inline-span records;
    // zero-count scratch storage is valid and must not suppress bidi placement.
    if (!chars || !rects || (counts.spans > 0 && !spans) ||
        !visual_to_logical || !levels || !bidi_classes) return;

    int char_cursor = 0;
    int rect_cursor = 0;
    int span_cursor = 0;
    bidi_append_controls(chars, &char_cursor, root_controls.start, root_controls.count);
    bidi_fill_views(lycon, root, lycon->block.line_number, 0, chars, rects, spans,
                    &char_cursor, &rect_cursor, &span_cursor,
                    line_direction, counts.has_bidi_trigger);
    bidi_append_controls(chars, &char_cursor, root_controls.end, root_controls.count);
    if (char_cursor != counts.chars || rect_cursor != counts.rects) return;
    for (int i = 0; i < rect_cursor; i++) {
        rects[i].line_has_bidi_inline_content = counts.has_bidi_inline_content;
    }
    bidi_scale_rect_widths(chars, rects, counts.rects);

    int max_level = 0;
#if RDT_HAS_FRIBIDI
    FriBidiChar* logical = scope.array<FriBidiChar>(counts.chars);
    FriBidiChar* visual = scope.array<FriBidiChar>(counts.chars);
    FriBidiStrIndex* logical_to_visual = scope.array<FriBidiStrIndex>(counts.chars);
    FriBidiStrIndex* fri_visual_to_logical = scope.array<FriBidiStrIndex>(counts.chars);
    FriBidiLevel* fri_levels = scope.array<FriBidiLevel>(counts.chars);
    if (!logical || !visual || !logical_to_visual || !fri_visual_to_logical ||
        !fri_levels) return;
    for (int i = 0; i < counts.chars; i++) logical[i] = chars[i].codepoint;

    FriBidiParType base_direction = line_direction == CSS_VALUE_RTL
        ? FRIBIDI_PAR_RTL : FRIBIDI_PAR_LTR;
    FriBidiLevel fri_max_level = fribidi_log2vis(
        logical, counts.chars, &base_direction, visual,
        logical_to_visual, fri_visual_to_logical, fri_levels);
    max_level = (int)fri_max_level;
    for (int i = 0; i < counts.chars; i++) {
        visual_to_logical[i] = (int)fri_visual_to_logical[i];
        levels[i] = (int)fri_levels[i];
    }
#else
    int embedding_stack[64];
    int embedding_depth = 0;
    int current_level = line_direction == CSS_VALUE_RTL ? 1 : 0;
    max_level = current_level;
    for (int i = 0; i < counts.chars; i++) {
        uint32_t cp = chars[i].codepoint;
        bidi_classes[i] = 0;
        levels[i] = current_level;
        if (bidi_codepoint_is_format_control(cp)) bidi_classes[i] = 2;
        if (cp == 0x202A || cp == 0x202D) {
            if (embedding_depth < 64) embedding_stack[embedding_depth++] = current_level;
            current_level++;
            if ((current_level & 1) != 0) current_level++;
            if (current_level > max_level) max_level = current_level;
        } else if (cp == 0x202B || cp == 0x202E) {
            if (embedding_depth < 64) embedding_stack[embedding_depth++] = current_level;
            current_level++;
            if ((current_level & 1) != 1) current_level++;
            if (current_level > max_level) max_level = current_level;
        } else if (cp == 0x202C) {
            if (embedding_depth > 0) current_level = embedding_stack[--embedding_depth];
        } else {
            int strong_class = utf_bidi_strong_class(cp);
            // CSS Text treats a tab as a bidi segmentation point; resolving it
            // into the surrounding RTL run would reverse the tab-separated
            // fragments as one run.
            bidi_classes[i] = cp == 0x0009 ? 2 : (signed char)strong_class;
            // Implicit levels keep an LTR run in source order inside an RTL
            // paragraph; assigning every character the paragraph level would
            // reverse Latin words one codepoint at a time.
            if ((strong_class == 1 && (current_level & 1) == 0) ||
                (strong_class == -1 && (current_level & 1) != 0)) {
                levels[i] = current_level + 1;
                if (levels[i] > max_level) max_level = levels[i];
            }
        }
        visual_to_logical[i] = i;
    }
    // UAX #9 N1/N2 resolves a neutral run from the surrounding strong types
    // before L2 reorders the line. Without this, a space between two RTL
    // characters keeps the paragraph level and splits the RTL run in two.
    for (int begin = 0; begin < counts.chars;) {
        if (bidi_classes[begin] != 0) {
            begin++;
            continue;
        }
        int end = begin + 1;
        while (end < counts.chars && bidi_classes[end] == 0) end++;
        int before = begin - 1;
        while (before >= 0 && bidi_classes[before] == 0) before--;
        int after = end;
        while (after < counts.chars && bidi_classes[after] == 0) after++;
        int resolved_level = levels[begin];
        if (before >= 0 && after < counts.chars &&
                bidi_classes[before] != 0 &&
                bidi_classes[before] == bidi_classes[after]) {
            resolved_level = levels[before];
        }
        for (int index = begin; index < end; index++) {
            levels[index] = resolved_level;
        }
        begin = end;
    }
    // UAX #9 L2 applies to the complete inline line. Reversing each span in
    // isolation leaves bidi fragments from one decorated span trapped before
    // the next span, so box-decoration-break:clone cannot expose the visual
    // union that the browser lays out.
    for (int level = max_level; level >= 1; level--) {
        int visual = 0;
        while (visual < counts.chars) {
            while (visual < counts.chars &&
                   levels[visual_to_logical[visual]] < level) visual++;
            int begin = visual;
            while (visual < counts.chars &&
                   levels[visual_to_logical[visual]] >= level) visual++;
            for (int left = begin, right = visual - 1; left < right; left++, right--) {
                int logical = visual_to_logical[left];
                visual_to_logical[left] = visual_to_logical[right];
                visual_to_logical[right] = logical;
            }
        }
    }
#endif
    // Explicit directional inline boxes still delimit whitespace runs when
    // their content has no strong character to raise the paragraph level.
    if (max_level == 0 && !counts.has_bidi_layout_feature) return;

    bidi_update_span_visual_ranges(chars, visual_to_logical, spans, counts.spans,
                                   counts.chars);
    bidi_place_visual_line(lycon, chars, rects, counts.rects, spans, counts.spans,
                           visual_to_logical, counts.chars, counts.max_depth,
                           levels, counts.has_inline_block && !isolated_paragraph);
    bidi_publish_paint_runs(lycon, chars, rects, counts.rects, levels);
    float visual_left = FLT_MAX;
    float visual_right = -FLT_MAX;
    for (int i = 0; i < counts.chars; i++) {
        if (chars[i].width <= 0.0f) continue;
        visual_left = min(visual_left, chars[i].visual_x);
        visual_right = max(visual_right,
                           chars[i].visual_x + chars[i].width);
    }
    if (visual_left != FLT_MAX) {
        bidi_position_forced_breaks(
            root, lycon->block.line_number, line_direction,
            visual_left, visual_right);
    }
    bidi_refresh_bounds(root, lycon->block.line_number, spans, counts.spans);
    if (max_level > 0) {
        for (int depth = counts.max_depth; depth >= 0; depth--) {
            for (int span_index = 0; span_index < counts.spans; span_index++) {
                BidiSpanInfo* span_info = &spans[span_index];
                if (span_info->depth != depth || span_info->max_visual < 0) continue;
                recompute_span_bounding_box_after_line_layout(
                    span_info->span, inline_span_has_multiple_line_fragments(span_info->span),
                    span_info->span->font ? span_info->span->fontp()->font_handle : nullptr);
            }
        }
    }
}

static void bidi_resolve_inline_isolates(LayoutContext* lycon, View* view) {
    for (View* current = view; current; current = current->next()) {
        if (current->view_type != RDT_VIEW_INLINE ||
            layout_view_is_out_of_flow(current)) continue;
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(current);
        bidi_resolve_inline_isolates(lycon, span->first_child);
        if (!layout_inline_span_isolate(span)) continue;
        // CSS Writing Modes §2.2 gives an isolate its own bidi paragraph;
        // treating it atomically outside does not reorder its split text runs.
        CssEnum parent_direction = lycon->line.inline_base_direction;
        CssEnum direction = span->block()->direction;
        if (span->block()->unicode_bidi == CSS_VALUE_PLAINTEXT) {
            DomText* anchor = nullptr;
            size_t byte = 0;
            direction = bidi_find_line_anchor(span->first_child, lycon->block.line_number, &anchor, &byte)
                ? bidi_plaintext_paragraph_direction(span->as_element(), anchor, byte) : CSS_VALUE_LTR;
        }
        lycon->line.inline_base_direction = direction;
        bidi_resolve_line(lycon, span->first_child, direction, true,
            span->block()->unicode_bidi);
        lycon->line.inline_base_direction = parent_direction;
    }
}

void layout_bidi_line(LayoutContext* lycon) {
    if (!lycon || !lycon->line.start_view) return;
    View* root = layout_inline_fragment_root(lycon->line.start_view);
    bidi_resolve_inline_isolates(lycon, root);
    ViewBlock* block = lycon->block.flow_box;
    bidi_resolve_line(lycon, root, lycon->line.inline_base_direction, false,
        block && block->blk ? block->block()->unicode_bidi : CSS_VALUE_NORMAL);
}
