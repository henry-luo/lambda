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
    TextRect* rect;
    int rect_slot;
    int logical_index;
    uint32_t codepoint;
    float width;
    float visual_x;
} BidiCharFragment;

typedef struct BidiRectInfo {
    ViewText* text;
    TextRect* rect;
    int first_char;
    int char_count;
    int visible_count;
    float raw_width;
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
} BidiLineCounts;

#if RDT_HAS_FRIBIDI
static bool bidi_codepoint_triggers_reorder(uint32_t codepoint) {
    FriBidiCharType type = fribidi_get_bidi_type((FriBidiChar)codepoint);
    return type == FRIBIDI_TYPE_RTL || type == FRIBIDI_TYPE_AL ||
           type == FRIBIDI_TYPE_LRE || type == FRIBIDI_TYPE_RLE ||
           type == FRIBIDI_TYPE_LRO || type == FRIBIDI_TYPE_RLO ||
           type == FRIBIDI_TYPE_PDF || type == FRIBIDI_TYPE_LRI ||
           type == FRIBIDI_TYPE_RLI || type == FRIBIDI_TYPE_FSI ||
           type == FRIBIDI_TYPE_PDI;
}

static bool bidi_is_line_text_rect(ViewText* text, TextRect* rect, int line_number) {
    return text && rect && rect->line_number == line_number && rect->length > 0 &&
           text->text_data();
}

static void bidi_count_views(View* view, int line_number, int depth,
                             BidiLineCounts* counts) {
    for (View* current = view; current; current = current->next()) {
        if (current->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(current)) {
            continue;
        }
        if (current->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(current);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (!bidi_is_line_text_rect(text, rect, line_number)) continue;
                const unsigned char* cursor = text->text_data() + rect->start_index;
                int remaining = rect->length;
                while (remaining > 0) {
                    uint32_t codepoint = 0;
                    int consumed = utf8_decode((const char*)cursor,
                        (size_t)remaining, &codepoint);
                    if (consumed <= 0 || consumed > remaining) consumed = 1;
                    counts->chars++;
                    counts->has_bidi_trigger = counts->has_bidi_trigger ||
                        bidi_codepoint_triggers_reorder(codepoint);
                    cursor += consumed;
                    remaining -= consumed;
                }
                counts->rects++;
            }
            continue;
        }
        if (current->view_type == RDT_VIEW_INLINE) {
            counts->spans++;
            if (depth > counts->max_depth) counts->max_depth = depth;
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(current);
            bidi_count_views(span->first_child, line_number, depth + 1, counts);
            continue;
        }
        if (current->view_type != RDT_VIEW_BR) counts->has_atomic = true;
    }
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

static void bidi_fill_views(View* view, int line_number, int depth,
                            BidiCharFragment* chars, BidiRectInfo* rects,
                            BidiSpanInfo* spans, int* char_cursor,
                            int* rect_cursor, int* span_cursor) {
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
                    BidiCharFragment* fragment = &chars[(*char_cursor)++];
                    fragment->text = text;
                    fragment->rect = rect;
                    fragment->rect_slot = *rect_cursor - 1;
                    fragment->logical_index = *char_cursor - 1;
                    fragment->codepoint = codepoint;
                    fragment->width = bidi_char_raw_width(text, codepoint);
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
            bidi_fill_views(span_info->span->first_child, line_number, depth + 1,
                            chars, rects, spans, char_cursor, rect_cursor, span_cursor);
            span_info->logical_end = *char_cursor - 1;
            continue;
        }
    }
}

static void bidi_scale_rect_widths(BidiCharFragment* chars, BidiRectInfo* rects,
                                    int rect_count) {
    for (int i = 0; i < rect_count; i++) {
        BidiRectInfo* rect_info = &rects[i];
        // line_break() trims a trailing collapsible space from the advance but
        // keeps it in TextRect::length; feeding that hidden character to UAX #9
        // would incorrectly widen the DOM range across reordered fragments.
        for (int j = rect_info->char_count - 1; j >= 0; j--) {
            BidiCharFragment* fragment = &chars[rect_info->first_char + j];
            if (!bidi_is_collapsible_space(fragment->codepoint) ||
                fragment->width <= 0.0f ||
                rect_info->raw_width - fragment->width < rect_info->rect->width - 0.01f) {
                break;
            }
            rect_info->raw_width -= fragment->width;
            fragment->width = 0.0f;
            rect_info->visible_count--;
        }
        if (rect_info->raw_width > 0.0f) {
            float scale = rect_info->rect->width / rect_info->raw_width;
            for (int j = 0; j < rect_info->char_count; j++) {
                chars[rect_info->first_char + j].width *= scale;
            }
        } else if (rect_info->visible_count > 0) {
            float width = rect_info->rect->width / rect_info->visible_count;
            for (int j = 0; j < rect_info->char_count; j++) {
                if (chars[rect_info->first_char + j].width > 0.0f) {
                    chars[rect_info->first_char + j].width = width;
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
        if (logical < 0 || logical >= char_count || chars[logical].width <= 0.0f) continue;
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
        float origin = chars[i].rect->x;
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
        if (logical >= 0 && logical < char_count && chars[logical].width > 0.0f) {
            chars[logical].visual_x = cursor;
            cursor += chars[logical].width;
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
        if (chars[i].width <= 0.0f) continue;
        int slot = -1;
        for (int rect_index = 0; rect_index < rect_count; rect_index++) {
            if (rects[rect_index].rect == chars[i].rect) {
                slot = rect_index;
                break;
            }
        }
        if (slot < 0) continue;
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
    for (View* current = view; current; current = current->next()) {
        if (current->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(current);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (rect->line_number == line_number) {
                    adjust_text_bounds(text);
                    break;
                }
            }
        } else if (current->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(current);
            bidi_refresh_bounds(span->first_child, line_number, spans, span_count);
        }
    }
    (void)spans;
    (void)span_count;
}
#endif

void layout_bidi_line(LayoutContext* lycon) {
#if !RDT_HAS_FRIBIDI
    (void)lycon;
    return;
#else
    if (!lycon || !lycon->line.start_view || lycon->line.has_replaced_content) return;
    View* root = layout_inline_fragment_root(lycon->line.start_view);
    if (!root) return;

    BidiLineCounts counts = {};
    bidi_count_views(root, lycon->block.line_number, 0, &counts);
    // Ordinary LTR lines already have correct fragment geometry; UAX #9 must
    // only rewrite lines whose bidi data can change visual order.
    if (counts.chars <= 0 || counts.rects <= 0 || counts.has_atomic ||
        !counts.has_bidi_trigger) return;

    BidiCharFragment* chars = (BidiCharFragment*)scratch_calloc(
        &lycon->scratch, sizeof(BidiCharFragment) * counts.chars);
    BidiRectInfo* rects = (BidiRectInfo*)scratch_calloc(
        &lycon->scratch, sizeof(BidiRectInfo) * counts.rects);
    BidiSpanInfo* spans = (BidiSpanInfo*)scratch_calloc(
        &lycon->scratch, sizeof(BidiSpanInfo) * counts.spans);
    FriBidiChar* logical = (FriBidiChar*)scratch_alloc(
        &lycon->scratch, sizeof(FriBidiChar) * counts.chars);
    FriBidiChar* visual = (FriBidiChar*)scratch_alloc(
        &lycon->scratch, sizeof(FriBidiChar) * counts.chars);
    FriBidiStrIndex* logical_to_visual = (FriBidiStrIndex*)scratch_alloc(
        &lycon->scratch, sizeof(FriBidiStrIndex) * counts.chars);
    FriBidiStrIndex* visual_to_logical = (FriBidiStrIndex*)scratch_alloc(
        &lycon->scratch, sizeof(FriBidiStrIndex) * counts.chars);
    FriBidiLevel* levels = (FriBidiLevel*)scratch_alloc(
        &lycon->scratch, sizeof(FriBidiLevel) * counts.chars);
    if (!chars || !rects || !spans || !logical || !visual || !logical_to_visual ||
        !visual_to_logical || !levels) return;

    int char_cursor = 0;
    int rect_cursor = 0;
    int span_cursor = 0;
    bidi_fill_views(root, lycon->block.line_number, 0, chars, rects, spans,
                    &char_cursor, &rect_cursor, &span_cursor);
    if (char_cursor != counts.chars || rect_cursor != counts.rects) return;
    for (int i = 0; i < counts.chars; i++) logical[i] = chars[i].codepoint;
    bidi_scale_rect_widths(chars, rects, counts.rects);

    FriBidiParType base_direction = lycon->block.direction == CSS_VALUE_RTL
        ? FRIBIDI_PAR_RTL : FRIBIDI_PAR_LTR;
    FriBidiLevel max_level = fribidi_log2vis(
        logical, counts.chars, &base_direction, visual,
        logical_to_visual, visual_to_logical, levels);
    if (max_level == 0) return;

    bidi_update_span_visual_ranges(chars, visual_to_logical, spans, counts.spans,
                                   counts.chars);
    bidi_place_visual_line(lycon, chars, rects, counts.rects, spans, counts.spans,
                           visual_to_logical, counts.chars, counts.max_depth);
    bidi_refresh_bounds(root, lycon->block.line_number, spans, counts.spans);
    for (int depth = counts.max_depth; depth >= 0; depth--) {
        for (int span_index = 0; span_index < counts.spans; span_index++) {
            BidiSpanInfo* span_info = &spans[span_index];
            if (span_info->depth != depth || span_info->max_visual < 0) continue;
            recompute_span_bounding_box_after_line_layout(
                span_info->span, inline_span_has_multiple_line_fragments(span_info->span),
                span_info->span->font ? span_info->span->fontp()->font_handle : nullptr);
        }
    }
#endif
}
