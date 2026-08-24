#include "layout.hpp"
#include "view.hpp"
#include "../lib/font/font.h"
#include "../lib/tagged.hpp"
#include "../lib/utf.h"

#include <float.h>
#include <limits.h>
#include <string.h>

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
    uint32_t codepoint;
    float width;
    float advance_width;
    float visual_x;
} BidiCharFragment;

typedef struct BidiRectInfo {
    ViewText* text;
    TextRect* rect;
    int first_char;
    int char_count;
    int visible_count;
    float raw_width;
    bool collapses_spaces;
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
} BidiSpanInfo;

typedef struct BidiLineCounts {
    int chars;
    int rects;
    int spans;
    int max_depth;
    bool has_atomic;
    bool has_bidi_trigger;
    bool has_bidi_layout_feature;
} BidiLineCounts;

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
    if (!node) return 0;
    if (node->is_text()) {
        DomText* text = node->as_text();
        if (!text->text || text->length == 0) return 0;
        const char* cursor = text->text;
        const char* end = cursor + text->length;
        while (cursor < end) {
            uint32_t codepoint = 0;
            int bytes = str_utf8_decode(cursor, (size_t)(end - cursor), &codepoint);
            if (bytes <= 0) { cursor++; continue; }
            int strong_class = utf_bidi_strong_class(codepoint);
            if (strong_class != 0) return strong_class;
            cursor += bytes;
        }
        return 0;
    }
    if (!node->is_element()) return 0;
    DomElement* element = node->as_element();
    if (element->tag_id == MARKUP_NAME_SCRIPT ||
        element->tag_id == MARKUP_NAME_STYLE ||
        (element->tag_name && strcmp(element->tag_name, "::marker") == 0)) {
        return 0;
    }
    if (skip_explicit_dir && element->get_attribute("dir")) return 0;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        int strong_class = layout_find_first_strong_direction(child, skip_explicit_dir);
        if (strong_class != 0) return strong_class;
    }
    return 0;
}

CssEnum layout_resolve_plaintext_direction(DomElement* element, CssEnum fallback) {
    int strong_class = layout_find_first_strong_direction(element, false);
    if (strong_class > 0) return CSS_VALUE_RTL;
    if (strong_class < 0) return CSS_VALUE_LTR;
    return fallback;
}

static bool bidi_is_line_break_codepoint(uint32_t codepoint) {
    // CSS Text line breaking has already split these records; feeding them to
    // UAX #9 would make a collapsed source newline act as a paragraph break.
    return codepoint == 0x000A || codepoint == 0x000D;
}

static void bidi_count_views(View* view, int line_number, int depth,
                             CssEnum direction, BidiLineCounts* counts) {
    auto visit = [&](View* current, int current_depth) -> bool {
        if (current->view_type == RDT_VIEW_NONE) return false;
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
                    if (bidi_is_line_break_codepoint(codepoint)) {
                        cursor += consumed;
                        remaining -= consumed;
                        continue;
                    }
                    counts->has_bidi_layout_feature =
                        counts->has_bidi_layout_feature ||
                        codepoint == 0x0009 || node_has_layout_feature;
                    counts->chars++;
                    counts->has_bidi_trigger = counts->has_bidi_trigger ||
                        bidi_codepoint_triggers_reorder(codepoint) ||
                        // the fragment pass protects inline-box edge geometry;
                        // controls in bare text are already represented by the
                        // normal text layout and must not reshuffle its ranges.
                        (current_depth > 0 &&
                         bidi_codepoint_is_format_control(codepoint));
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
            if (current_depth > counts->max_depth) counts->max_depth = current_depth;
            return true;
        }
        if (current->view_type == RDT_VIEW_MARKER) {
            MarkerProp* marker = (MarkerProp*)current->as_element()->blk;
            if (marker && !marker->is_outside && marker->width > 0.0f) {
                counts->chars++;
                counts->has_bidi_trigger = counts->has_bidi_trigger ||
                    bidi_codepoint_triggers_reorder(
                        bidi_marker_representative_codepoint(direction));
            }
            return false;
        }
        if (current->view_type != RDT_VIEW_BR) counts->has_atomic = true;
        return false;
    };
    auto no_leave = [](View*, int) {};
    layout_walk_view_tree(view, visit, no_leave, true, depth);
}

static float bidi_span_edge_width(ViewSpan* span, bool left) {
    if (!span || !span->bound) return 0.0f;
    BoundaryProp* boundary = span->boundary_mut();
    float border = 0.0f;
    if (boundary->border) {
        border = left ? boundary->border->width.left : boundary->border->width.right;
    }
    float padding = left ? boundary->padding.left : boundary->padding.right;
    float margin = left ? boundary->margin.left : boundary->margin.right;
    return margin + padding + border;
}

static float bidi_char_raw_width(ViewText* text, uint32_t codepoint) {
    if (!text || text_codepoint_has_zero_advance(codepoint) || !text->font ||
        !text->font->font_handle) {
        return 0.0f;
    }
    float width = font_measure_char(text->font->font_handle, codepoint);
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

static void bidi_fill_views(View* view, int line_number, int depth,
                            BidiCharFragment* chars, BidiRectInfo* rects,
                            BidiSpanInfo* spans, int* char_cursor,
                            int* rect_cursor, int* span_cursor,
                            CssEnum direction) {
    for (View* current = view; current; current = current->next()) {
        if (current->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(current)) {
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
                rect_info->directional_span = bidi_text_directional_span(text, direction);

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
                    if (bidi_is_line_break_codepoint(codepoint)) {
                        cursor += consumed;
                        remaining -= consumed;
                        continue;
                    }
                    BidiCharFragment* fragment = &chars[(*char_cursor)++];
                    fragment->text = text;
                    fragment->rect = rect;
                    fragment->rect_slot = *rect_cursor - 1;
                    fragment->logical_index = *char_cursor - 1;
                    fragment->codepoint = codepoint;
                    fragment->width = bidi_char_raw_width(text, codepoint);
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
            bool isolate = layout_inline_span_isolate(span_info->span);
            if (isolate) {
                BidiCharFragment* fragment = &chars[(*char_cursor)++];
                fragment->atomic_view = static_cast<View*>(span_info->span);
                fragment->codepoint = span_info->span->blk->direction == CSS_VALUE_RTL
                    ? 0x0627 : 0x0041;
                fragment->width = span_info->span->width;
                fragment->advance_width = fragment->width;
                span_info->logical_end = *char_cursor - 1;
                continue;
            }
            bidi_fill_views(span_info->span->first_child, line_number, depth + 1,
                            chars, rects, spans, char_cursor, rect_cursor, span_cursor,
                            direction);
            span_info->logical_end = *char_cursor - 1;
            continue;
        }
        if (current->view_type == RDT_VIEW_MARKER) {
            MarkerProp* marker = (MarkerProp*)current->as_element()->blk;
            if (marker && !marker->is_outside && marker->width > 0.0f) {
                BidiCharFragment* fragment = &chars[(*char_cursor)++];
                fragment->text = nullptr;
                fragment->atomic_view = current;
                fragment->rect = nullptr;
                fragment->rect_slot = -1;
                fragment->logical_index = *char_cursor - 1;
                fragment->codepoint = bidi_marker_representative_codepoint(direction);
                fragment->width = marker->width;
                fragment->advance_width = fragment->width;
                fragment->visual_x = 0.0f;
            }
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
            if (!rect_info->collapses_spaces || !rect_info->directional_span ||
                !bidi_is_collapsible_space(fragment->codepoint) ||
                fragment->width <= 0.0f ||
                rect_info->raw_width <= 0.0f) {
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
        for (int span_index = 0; span_index < span_count; span_index++) {
            BidiSpanInfo* span = &spans[span_index];
            if (logical < span->logical_start || logical > span->logical_end) continue;
            span->min_visual = min(span->min_visual, visual);
            span->max_visual = max(span->max_visual, visual);
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
                origin -= span->left_edge;
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
                                   int char_count, int max_depth) {
    float cursor = bidi_line_origin(chars, spans, char_count, span_count);
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
                float atomic_shift = cursor - atomic_view->x;
                atomic_view->x = cursor;
                if (atomic_shift != 0.0f &&
                    atomic_view->view_type == RDT_VIEW_INLINE) {
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

    float* min_x = (float*)scratch_alloc(&lycon->scratch, sizeof(float) * rect_count);
    float* max_x = (float*)scratch_alloc(&lycon->scratch, sizeof(float) * rect_count);
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
        min_x[slot] = min(min_x[slot], chars[i].visual_x);
        max_x[slot] = max(max_x[slot], chars[i].visual_x + chars[i].width);
    }
    for (int i = 0; i < rect_count; i++) {
        if (min_x[i] == FLT_MAX) continue;
        rects[i].rect->x = min_x[i];
        rects[i].rect->width = max_x[i] - min_x[i];
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
void layout_bidi_line(LayoutContext* lycon) {
    if (!lycon || !lycon->line.start_view || lycon->line.has_replaced_content) return;
    View* root = layout_inline_fragment_root(lycon->line.start_view);
    if (!root) return;

    BidiLineCounts counts = {};
    bidi_count_views(root, lycon->block.line_number, 0,
                     lycon->block.direction, &counts);
    // Ordinary LTR lines already have correct fragment geometry; UAX #9 must
    // only rewrite lines whose bidi data can change visual order.
    // CSS Writing Modes applies bidi to the paragraph, but an ordinary line
    // with no reorder trigger is already in visual order. Keep the pass for
    // tabs, explicit bidi modes, and hanging punctuation because those
    // features still require fragment-bound updates without an RTL codepoint
    // to trigger reordering.
    if (counts.chars <= 0 || counts.rects <= 0 || counts.has_atomic ||
        (!counts.has_bidi_trigger && !counts.has_bidi_layout_feature)) return;

    BidiCharFragment* chars = (BidiCharFragment*)scratch_calloc(
        &lycon->scratch, sizeof(BidiCharFragment) * counts.chars);
    BidiRectInfo* rects = (BidiRectInfo*)scratch_calloc(
        &lycon->scratch, sizeof(BidiRectInfo) * counts.rects);
    BidiSpanInfo* spans = (BidiSpanInfo*)scratch_calloc(
        &lycon->scratch, sizeof(BidiSpanInfo) * counts.spans);
    int* visual_to_logical = (int*)scratch_alloc(
        &lycon->scratch, sizeof(int) * counts.chars);
    int* levels = (int*)scratch_alloc(
        &lycon->scratch, sizeof(int) * counts.chars);
    // A block's anonymous inline content can have no inline-span records;
    // zero-count scratch storage is valid and must not suppress bidi placement.
    if (!chars || !rects || (counts.spans > 0 && !spans) ||
        !visual_to_logical || !levels) return;

    int char_cursor = 0;
    int rect_cursor = 0;
    int span_cursor = 0;
    bidi_fill_views(root, lycon->block.line_number, 0, chars, rects, spans,
                    &char_cursor, &rect_cursor, &span_cursor,
                    lycon->block.direction);
    if (char_cursor != counts.chars || rect_cursor != counts.rects) return;
    bidi_scale_rect_widths(chars, rects, counts.rects);

    int max_level = 0;
#if RDT_HAS_FRIBIDI
    FriBidiChar* logical = (FriBidiChar*)scratch_alloc(
        &lycon->scratch, sizeof(FriBidiChar) * counts.chars);
    FriBidiChar* visual = (FriBidiChar*)scratch_alloc(
        &lycon->scratch, sizeof(FriBidiChar) * counts.chars);
    FriBidiStrIndex* logical_to_visual = (FriBidiStrIndex*)scratch_alloc(
        &lycon->scratch, sizeof(FriBidiStrIndex) * counts.chars);
    FriBidiStrIndex* fri_visual_to_logical = (FriBidiStrIndex*)scratch_alloc(
        &lycon->scratch, sizeof(FriBidiStrIndex) * counts.chars);
    FriBidiLevel* fri_levels = (FriBidiLevel*)scratch_alloc(
        &lycon->scratch, sizeof(FriBidiLevel) * counts.chars);
    if (!logical || !visual || !logical_to_visual || !fri_visual_to_logical ||
        !fri_levels) return;
    for (int i = 0; i < counts.chars; i++) logical[i] = chars[i].codepoint;

    FriBidiParType base_direction = lycon->block.direction == CSS_VALUE_RTL
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
    int current_level = lycon->block.direction == CSS_VALUE_RTL ? 1 : 0;
    max_level = current_level;
    for (int i = 0; i < counts.chars; i++) {
        uint32_t cp = chars[i].codepoint;
        levels[i] = current_level;
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
                           visual_to_logical, counts.chars, counts.max_depth);
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
