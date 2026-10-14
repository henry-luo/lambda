#include "render.hpp"
#include "render_effect_raster_fallback.hpp"
#include "render_glyph_run_raster_lower.hpp"
#include "layout.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "../lib/time_util.h"
#include "../lib/font/font.h"
#include "../lib/utf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct RenderTextPaintEntry {
    lam::Up<ViewText> text;
    lam::Up<TextRect> rect;
    FontBox font;
    int start_index;
    int length;
    float x;
    float width;
    bool projected;
    bool marker;
    float marker_x, marker_y;
};

struct RenderTextPaintData {
    lam::Own<ArrayList> entries;
    FontBox font;
    Color color;
    float marker_width;
    bool rtl;
};

static void render_text_paint_collect(RenderTextPaintData* data, RenderContext* context,
                                       View* first, FontBox font) {
    for (View* view = first; view; view = view->next()) {
        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(view);
            FontBox text_font = font;
            if (text->font) setup_font(context->ui_context, &text_font, text->font);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                RenderTextPaintEntry* entry = (RenderTextPaintEntry*)mem_calloc(
                    1, sizeof(RenderTextPaintEntry), MEM_CAT_RENDER);
                entry->text = lam::up(text);
                entry->rect = lam::up(rect);
                entry->font = text_font;
                text_rect_painted_source_range(rect, &entry->start_index, &entry->length);
                entry->x = rect->x;
                entry->width = rect->width;
                arraylist_append(data->entries, entry);
                if (getenv("RADIANT_TRACE_TEXT"))
                    log_debug("text-overflow plan: line=%d x=%.4f y=%.4f width=%.4f bytes=%d source=%.*s",
                        rect->line_number, rect->x, rect->y, rect->width,
                        rect->length, rect->length, text->text_data() + rect->start_index);
            }
        } else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require_element(view);
            FontBox child_font = font;
            if (span->font) setup_font(context->ui_context, &child_font, span->font);
            render_text_paint_collect(data, context, span->first_child, child_font);
        }
        // nested blocks establish their own paint scope and line numbering.
    }
}

static int render_text_paint_visual_order(ArrayListValue left, ArrayListValue right) {
    const TextRect* a = ((RenderTextPaintEntry*)left)->rect;
    const TextRect* b = ((RenderTextPaintEntry*)right)->rect;
    if (a->line_number != b->line_number) return a->line_number < b->line_number ? -1 : 1;
    return a->x < b->x ? -1 : a->x > b->x ? 1 : 0;
}

static int render_text_paint_source_order(ArrayListValue left, ArrayListValue right) {
    uintptr_t a = (uintptr_t)((RenderTextPaintEntry*)left)->rect.get();
    uintptr_t b = (uintptr_t)((RenderTextPaintEntry*)right)->rect.get();
    return a < b ? -1 : a > b ? 1 : 0;
}

static bool render_text_source_has_space(const unsigned char* text, int byte_offset,
                                         bool preserve_spaces) {
    return !preserve_spaces && byte_offset > 0 && is_space(text[byte_offset - 1]);
}

static bool render_text_source_word_start(const unsigned char* text, int byte_offset) {
    // a paint slice can start inside a word; UTF-8 continuation bytes cannot be CSS whitespace.
    return byte_offset == 0 || is_space(text[byte_offset - 1]);
}

static void render_text_paint_fit(RenderTextPaintEntry* entry, float edge,
                                  RenderContext* context, bool first_cluster_required = false,
                                  bool retain_suffix = false) {
    int source_start, source_length;
    text_rect_painted_source_range(entry->rect, &source_start, &source_length);
    const unsigned char* start = entry->text->text_data() + source_start;
    const unsigned char* end = start + source_length;
    const unsigned char* cursor = start;
    const unsigned char* retained_start = start;
    float discarded_width = 0.0f;
    bool suffix_started = false;
    bool preserve_spaces = white_space_preserves_space_advance(get_white_space_value(entry->text));
    bool has_space = render_text_source_has_space(
        entry->text->text_data(), source_start, preserve_spaces);
    float width = 0.0f;
    float scale = ui_context_raster_scale(context->ui_context);
    FontStyleDesc descriptor = font_style_desc_from_prop(entry->font.style);
    CssEnum transform = render_text_inherited_transform(entry->text);
    bool word_start = render_text_source_word_start(
        entry->text->text_data(), source_start);
    while (cursor < end) {
        size_t bytes = 0;
        if (!layout_get_grapheme_cluster_bytes(cursor, end, &bytes)) {
            uint32_t codepoint = 0;
            int decoded = str_utf8_decode((const char*)cursor, (size_t)(end - cursor), &codepoint);
            bytes = decoded > 0 ? (size_t)decoded : 1;
        }
        if (retain_suffix && !suffix_started &&
            (entry->rect->x + width >= edge - 0.001f ||
             (first_cluster_required && cursor + bytes == end))) {
            suffix_started = true;
        }
        const unsigned char* cluster = cursor;
        float advance = 0.0f;
        while (cluster < cursor + bytes) {
            uint32_t codepoint = 0;
            if (!layout_utf8_next_codepoint(&cluster, cursor + bytes, &codepoint)) break;
            if (codepoint == 0x00AD || text_codepoint_has_zero_advance(codepoint)) continue;
            if (is_space(codepoint)) {
                if (preserve_spaces || !has_space) advance += text_space_advance(entry->font.style);
                has_space = true;
                word_start = true;
                continue;
            }
            has_space = false;
            float synthetic_advance = 0.0f;
            if (render_text_synthesized_space_advance(&entry->font, codepoint, &synthetic_advance)) {
                advance += synthetic_advance;
                continue;
            }
            uint32_t transformed[3];
            int count = apply_text_transform_full(codepoint, transform, word_start, transformed);
            word_start = is_space(codepoint);
            for (int index = 0; index < count; index++) {
                LoadedGlyph* glyph = font_load_glyph(font_box_handle(&entry->font),
                    &descriptor, transformed[index], false);
                advance += glyph ? glyph->advance_x / scale : entry->font.style->space_width;
                advance += text_character_spacing(entry->font.style, transformed[index]);
            }
        }
        if (!retain_suffix && entry->rect->x + width + advance > edge + 0.001f &&
            !(first_cluster_required && cursor == start)) break;
        width += advance;
        cursor += bytes;
        if (retain_suffix && !suffix_started) {
            // retain the original byte offset and advance so left clipping does not move surviving glyphs.
            retained_start = cursor;
            discarded_width = width;
        }
    }
    entry->start_index = source_start +
        (int)(retained_start - start); // INT_CAST_OK: retained byte offset is bounded by the source TextRect.
    entry->length = (int)(cursor - retained_start); // INT_CAST_OK: retained byte count is bounded by the source TextRect.
    entry->x = entry->rect->x + discarded_width;
    entry->width = width - discarded_width;
    entry->projected = true;
    if (getenv("RADIANT_TRACE_TEXT"))
        log_debug("text-overflow fit: x=%.4f edge=%.4f width=%.4f prefix=%d", entry->rect->x, edge, width, entry->length);
}

RenderTextPaintScope::RenderTextPaintScope(RenderContext* context, ViewBlock* block,
                                           const FontBox* font, const Color* color)
    : context(lam::up(context)), previous(lam::up(context ? context->text_paint.get() : nullptr)), data(nullptr) {
    if (!context) return;
    context->text_paint = nullptr;
    bool ellipsis = block && block->blk && block->blk->text_overflow == CSS_VALUE_ELLIPSIS &&
        block->scroller && block->scroll()->overflow_x != CSS_VALUE_VISIBLE;
    bool clamp = block && block->blk && block->blk->line_clamp > 0;
    if ((!ellipsis && !clamp) || !font || !font_box_handle(font) || !color) return;

    data = lam::own((RenderTextPaintData*)mem_calloc(1, sizeof(RenderTextPaintData), MEM_CAT_RENDER));
    data->entries = lam::own(arraylist_new(16));
    data->font = *font;
    data->color = *color;
    data->rtl = block->blk && block->block()->direction == CSS_VALUE_RTL;
    data->marker_width = layout_text_overflow_ellipsis_width(
        font_box_handle(font), font->current_font_size);
    render_text_paint_collect(data, context, block->first_child, *font);
    arraylist_sort(data->entries, render_text_paint_visual_order);
    BoxMetrics box = layout_box_metrics(block);
    float left = box.border.left + box.padding.left;
    float right = block->width - box.border.right - box.padding.right;
    if (block->scroller && block->scroll()->pane) {
        left += block->scroll()->pane->h_scroll_position;
        right += block->scroll()->pane->h_scroll_position;
    }
    if (getenv("RADIANT_TRACE_TEXT"))
        log_debug("text-overflow block: width=%.4f right=%.4f ellipsis=%d clamp=%d", block->width, right, ellipsis, clamp);
    for (int begin = 0; begin < data->entries->length;) {
        int end = begin;
        RenderTextPaintEntry* first = (RenderTextPaintEntry*)data->entries->data[begin];
        bool truncated = false;
        while (end < data->entries->length) {
            RenderTextPaintEntry* entry = (RenderTextPaintEntry*)data->entries->data[end];
            if (entry->rect->line_number != first->rect->line_number) break;
            bool discarded = end + 1 < data->entries->length ||
                (size_t)(entry->rect->start_index + entry->rect->length) < entry->text->length;
            bool end_overflow = data->rtl ? entry->rect->x < left - 0.001f
                : entry->rect->x + entry->rect->width > right + 0.001f;
            truncated |= (ellipsis && end_overflow) ||
                (clamp && entry->rect->has_trailing_ellipsis && discarded);
            // reaching the clamp count alone does not prove that any content was discarded.
            if (entry->rect->has_trailing_ellipsis) entry->projected = true;
            end++;
        }
        if (truncated) {
            RenderTextPaintEntry* visual_first = data->rtl
                ? (RenderTextPaintEntry*)data->entries->data[end - 1] : first;
            RenderTextPaintEntry* marker = visual_first;
            float marker_x = 0.0f;
            bool retained = false;
            float fit_edge = data->rtl ? left + data->marker_width : right - data->marker_width;
            for (int index = begin; index < end; index++) {
                RenderTextPaintEntry* entry = (RenderTextPaintEntry*)data->entries->data[index];
                render_text_paint_fit(entry, fit_edge, context, false, data->rtl);
                float candidate = data->rtl ? entry->x - data->marker_width : entry->x + entry->width;
                if (entry->length > 0 && (!retained ||
                        (data->rtl ? candidate < marker_x : candidate > marker_x))) {
                    marker = entry;
                    marker_x = candidate;
                    retained = true;
                }
            }
            if (!retained) {
                // CSS Overflow 3 §6.1 clips the visual first cluster instead of removing it.
                render_text_paint_fit(visual_first, fit_edge, context, true, data->rtl);
                marker_x = data->rtl ? visual_first->x - data->marker_width
                    : visual_first->x + visual_first->width;
            }
            marker->marker = data->rtl ? marker_x + data->marker_width > left : marker_x < right;
            marker->marker_x = marker_x;
            marker->marker_y = marker->rect->y +
                font_get_rendering_ascender(font_box_handle(&marker->font)) -
                font_get_rendering_ascender(font_box_handle(font));
        }
        begin = end;
    }
    arraylist_sort(data->entries, render_text_paint_source_order);
    context->text_paint = data.borrow();
}

RenderTextPaintScope::~RenderTextPaintScope() {
    if (context) context->text_paint = previous;
    if (!data) return;
    for (int index = 0; index < data->entries->length; index++)
        mem_free(data->entries->data[index]);
    arraylist_free(data->entries);
    mem_free(data);
}

RenderTextPaintIterator::RenderTextPaintIterator(RenderContext* context, ViewText* text)
    : context(lam::up(context)), cursor(lam::up(text ? text->rect.get() : nullptr)),
      pending_marker(nullptr), projection{}, saved_font(context->font), saved_color(context->color) {}

RenderTextPaintIterator::~RenderTextPaintIterator() {
    context->font = saved_font;
    context->color = saved_color;
}

static void render_text_project_source_rect(const TextRect* source, TextRect* projection) {
    // query ranges associate the generated mark with a continuation character;
    // raster and vector paint must give that character to its actual line.
    *projection = {};
    projection->x = source->x;
    projection->y = source->y;
    projection->width = source->width;
    projection->height = source->height;
    projection->hanging_trim = source->hanging_trim;
    projection->justification_spacing = source->justification_spacing;
    text_rect_painted_source_range(source, &projection->start_index, &projection->length);
    projection->line_number = source->line_number;
    projection->has_trailing_hyphen = source->has_trailing_hyphen;
    projection->trailing_hyphenate_character = source->trailing_hyphenate_character;
    projection->has_trailing_ellipsis = source->has_trailing_ellipsis;
}

TextRect* RenderTextPaintIterator::next() {
    context->font = saved_font;
    context->color = saved_color;
    if (pending_marker) {
        RenderTextPaintEntry* entry = pending_marker;
        pending_marker = nullptr;
        projection = {};
        projection.x = entry->marker_x;
        projection.y = entry->marker_y;
        projection.width = context->text_paint->marker_width;
        projection.height = entry->rect->height;
        projection.has_trailing_ellipsis = true;
        context->font = context->text_paint->font;
        context->color = context->text_paint->color;
        if (getenv("RADIANT_TRACE_TEXT"))
            log_debug("text-overflow marker: x=%.4f y=%.4f color=%08x", projection.x, projection.y, context->color.c);
        return &projection;
    }
    while (cursor) {
        TextRect* source = cursor;
        cursor = source->next.borrow();
        auto source_rect = [&]() -> TextRect* {
            if (!source->hyphenated_leading_bytes && !source->hyphenated_trailing_bytes)
                return source;
            render_text_project_source_rect(source, &projection);
            return &projection;
        };
        RenderTextPaintData* data = context->text_paint;
        if (!data) return source_rect();
        int low = 0, high = data->entries->length;
        while (low < high) {
            int middle = low + (high - low) / 2;
            RenderTextPaintEntry* entry = (RenderTextPaintEntry*)data->entries->data[middle];
            if ((uintptr_t)entry->rect.get() < (uintptr_t)source) low = middle + 1;
            else high = middle;
        }
        if (low == data->entries->length) return source_rect();
        RenderTextPaintEntry* entry = (RenderTextPaintEntry*)data->entries->data[low];
        if (entry->rect != source || !entry->projected) return source_rect();
        bool combined_marker = entry->marker && entry->length &&
            !data->rtl &&
            saved_font.style == data->font.style && saved_color.c == data->color.c;
        if (entry->marker && !combined_marker) pending_marker = lam::up(entry);
        if (!entry->length) {
            if (pending_marker) return next();
            continue;
        }
        render_text_project_source_rect(source, &projection);
        bool complete_source = entry->start_index == projection.start_index &&
            entry->length == projection.length;
        projection.x = entry->x;
        projection.width = entry->width;
        projection.start_index = entry->start_index;
        projection.length = entry->length;
        if (!complete_source) {
            projection.has_trailing_hyphen = false;
            projection.trailing_hyphenate_character = nullptr;
        }
        projection.has_trailing_ellipsis = combined_marker;
        if (combined_marker) projection.width += data->marker_width;
        return &projection;
    }
    return nullptr;
}

static bool render_text_trace_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        // Browser-scale pages hit text and glyph loops thousands of times; keep
        // those diagnostics opt-in so normal rendering is not log-bound.
        enabled = (getenv("RADIANT_TRACE_TEXT") || getenv("RADIANT_TRACE_RENDER")) ? 1 : 0;
    }
    return enabled != 0;
}

static void render_text_inline_background(RasterRenderContext* rdcon, ViewText* text_view,
                                          TextRect* text_rect, DomElement* parent_elem,
                                          float x, float y);
static void render_text_inline_border(RasterRenderContext* rdcon, ViewText* text_view,
                                      TextRect* text_rect, DomElement* parent_elem,
                                      float x, float y, const Rect* bg_rect);
static bool render_text_paint_background_and_shadows(RasterRenderContext* rdcon, unsigned char* str,
                                              TextRect* text_rect, TextShadow* text_shadow,
                                              CssEnum text_transform, bool preserve_spaces,
                                              float space_width, float scaled_space_width,
                                              float cjk_spacing, CssEnum justify_mode,
                                              float x, float y, DomElement* parent_elem,
                                              const RenderTextEmphasis* emphasis);
static LoadedGlyph* render_text_load_glyph_for_paint(RasterRenderContext* rdcon,
                                                     uint32_t codepoint,
                                                     unsigned char* cursor,
                                                     unsigned char* end);

static float render_text_trailing_marks(RasterRenderContext* rdcon, TextRect* text_rect,
                                        float x, float y);
static void render_text_decorations(RasterRenderContext* rdcon, unsigned char* str,
                                    TextRect* text_rect, bool before_text);

CssEnum render_text_inherited_transform(ViewText* text_view) {
    if (!text_view) return CSS_VALUE_NONE;
    return get_text_transform_from_node(text_view->parent);
}

static inline uint32_t render_text_resolve_mathlive_fallback_glyph(uint32_t codepoint) {
    // MathLive's private negation slash is not present in Lambda's document
    // fonts, and Times draws its parallel relation as diagonal strokes.
    if (codepoint == 0xE020) return 0x2215;
    if (codepoint == 0x2225) return 0x2016;
    return codepoint;
}

char* render_text_create_export_segment(const unsigned char* text,
                                        const TextRect* text_rect,
                                        CssEnum text_transform,
                                        bool include_ellipsis, CssEnum white_space) {
    if (!text || !text_rect) return nullptr;

    const char* trailing_character = text_rect->trailing_hyphenate_character;
    if (text_rect->has_trailing_hyphen && !trailing_character) trailing_character = "-";
    size_t trailing_length = trailing_character ? strlen(trailing_character) : 0;
    int source_start, source_length;
    text_rect_painted_source_range(text_rect, &source_start, &source_length);
    // Transforms can expand each source byte to one four-byte codepoint; reserve
    // the computed hyphenation string separately because it is not source text.
    size_t content_size = (size_t)source_length * 4 + trailing_length + 4;
    char* content = (char*)mem_alloc(content_size, MEM_CAT_RENDER);
    unsigned char* src = (unsigned char*)text + source_start;
    unsigned char* src_end = src + source_length;
    char* dst = content;

    bool preserve_spaces = white_space_preserves_space_advance(white_space);
    bool has_space = render_text_source_has_space(text, source_start, preserve_spaces);
    bool is_word_start = render_text_source_word_start(text, source_start);
    while (src < src_end) {
        uint32_t codepoint = *src;
        int bytes = 1;
        if (codepoint >= 128) {
            bytes = str_utf8_decode((const char*)src, (size_t)(src_end - src), &codepoint);
            if (bytes <= 0) bytes = 1;
        }
        if (codepoint == 0x00AD) { src += bytes; continue; }
        if (is_space(codepoint)) {
            // the source byte range stays intact; only its painted representation collapses.
            if (preserve_spaces || !has_space) *dst++ = preserve_spaces ? *src : ' ';
            has_space = true;
            is_word_start = true;
        } else {
            has_space = false;
            uint32_t transformed[3];
            int count = apply_text_transform_full(codepoint, text_transform, is_word_start, transformed);
            is_word_start = false;
            for (int index = 0; index < count; index++) {
                uint32_t exported = render_text_resolve_mathlive_fallback_glyph(transformed[index]);
                if (exported != 0) dst += utf8_encode(exported, dst);
            }
        }
        src += bytes;
    }

    if (text_rect->has_trailing_hyphen && trailing_character) {
        memcpy(dst, trailing_character, trailing_length);
        dst += trailing_length;
    }
    if (include_ellipsis && text_rect->has_trailing_ellipsis) {
        *dst++ = (char)0xE2;
        *dst++ = (char)0x80;
        *dst++ = (char)0xA6;
    }
    *dst = '\0';
    return content;
}

static bool render_text_rect_misses_clip(RasterRenderContext* rdcon, unsigned char* text,
    TextRect* rect, const RenderTextEmphasis* emphasis, TextShadow* shadows,
    float x, float y, float width, float height);

static bool render_text_bounds_miss_clip(RasterRenderContext* rdcon, Bound bounds) {
    if (!rdcon || rdcon->has_transform) return false;
    return bounds.right < rdcon->block.clip.left || bounds.left > rdcon->block.clip.right ||
        bounds.bottom < rdcon->block.clip.top || bounds.top > rdcon->block.clip.bottom;
}

void render_text_view(RasterRenderContext* rdcon, ViewText* text_view) {

    if (view_visibility_is_hidden(text_view)) return;

    if (!font_box_handle(&rdcon->font)) {
        return;
    }

    float s = rdcon->raster_scale;
    unsigned char* str = text_view->text_data();
    TextRect* text_rect = text_view->rect;

    if (!text_rect) {
        return;
    }

    int sel_start = 0, sel_end = 0;
    bool has_selection = render_text_selection_span(
        rdcon, text_view, &sel_start, &sel_end);

    // Apply text color from text_view if set (PDF text uses this for fill color)
    Color saved_color = rdcon->color;

    // Setup font from text_view if set (PDF text has font property directly on ViewText)
    FontBox saved_font = rdcon->font;
    if (text_view->font) {
        setup_font(rdcon->ui_context, &rdcon->font, text_view->font);
    }

    // Skip rendering if font size is 0 - text should be invisible (e.g., font-size: 0)
    if (rdcon->font.style && rdcon->font.style->font_size <= 0.0f) {
        rdcon->font = saved_font;
        rdcon->color = saved_color;
        return;
    }

    RenderTextEmphasis emphasis = {};
    if (!render_text_emphasis_resolve(rdcon->ui_context ? rdcon->ui_context->font_ctx.get() : nullptr,
            &rdcon->font, rdcon->color, &emphasis)) rdcon->paint_failed = true;

    // Get the white-space property for this text node
    CssEnum white_space = get_white_space_value(text_view);
    bool preserve_spaces = white_space_preserves_space_advance(white_space);
    CssEnum text_justify = text_justify_computed_value(text_view->parent);

    // Get text-transform from parent elements
    CssEnum text_transform = get_text_transform_from_node(text_view->parent);
    DomElement* parent_elem = text_view->parent ? text_view->parent->as_element() : nullptr;
    bool has_selection_foreground = parent_elem && parent_elem->in_line &&
        parent_elem->inl()->has_selection_color;

    // Get text-shadow from parent element's font property
    TextShadow* text_shadow = nullptr;
    if (parent_elem && parent_elem->font && parent_elem->fontp()->text_shadow) {
        text_shadow = parent_elem->fontp()->text_shadow;
    }

    {
    RenderTextPaintIterator paint_iterator(rdcon, text_view);
    while ((text_rect = paint_iterator.next())) {
        // Apply scale to convert CSS pixel positions to physical surface pixels
        float x = rdcon->block.x + text_rect->x * s, y = rdcon->block.y + text_rect->y * s;

        if (render_text_rect_misses_clip(rdcon, str, text_rect, &emphasis, text_shadow, x, y,
                text_rect->width * s, text_rect->height * s)) {
            continue;
        }

        render_text_inline_background(rdcon, text_view, text_rect, parent_elem, x, y);
        render_text_selection_rect(rdcon, text_view, text_rect);

        unsigned char* p = str + text_rect->start_index;  unsigned char* end = p + text_rect->length;
        if (render_text_trace_enabled()) {
        }

        float scaled_space_width = rdcon->font.style->space_width * s;
        RenderTextJustification justification = render_text_justification(text_rect, text_justify);
        float space_width = (text_space_advance(rdcon->font.style) + justification.word) * s;
        float justification_per_gap = justification.character * s;

        // Render the text with adjusted spacing
        bool has_space = render_text_source_has_space(str, text_rect->start_index, preserve_spaces);
        uint32_t codepoint;
        bool is_word_start = render_text_source_word_start(str, text_rect->start_index);
        int char_index = text_rect->start_index;  // Track character offset for selection
        TextJustifyContext justify_context = {};

        bool shadow_prepass = text_shadow != nullptr;
        if (shadow_prepass && !render_text_paint_background_and_shadows(rdcon, str, text_rect,
                text_shadow, text_transform, preserve_spaces, space_width,
                scaled_space_width, text_justify == CSS_VALUE_INTER_WORD ? 0.0f : justification_per_gap,
                text_justify, x, y, parent_elem, &emphasis)) {
            rdcon->paint_failed = true;
            log_error("[TEXT_SHADOW_CAPTURE] native glyph layer capture failed");
        }

        // background/shadows precede underline/overline, then glyph ink and line-through.
        render_text_decorations(rdcon, str, text_rect, true);
        while (p < end) {
            // Check if current character is in selection range
            bool is_selected = has_selection && char_index >= sel_start && char_index < sel_end;

            // log_debug("draw character '%c'", *p);
            if (is_space(*p)) {
                if (preserve_spaces || !has_space) {  // preserve all spaces or add single whitespace
                    has_space = true;
                    if (text_justify_character_gap(&justify_context, ' ', text_justify))
                        x += justification_per_gap;
                    // Render space by advancing x position
                    // All spaces are rendered (not just non-trailing) because layout has
                    // already determined correct positioning including inter-element whitespace
                    x += space_width;  // Use adjusted space width for justified text
                }
                // else  // skip consecutive spaces
                is_word_start = true;  // Next non-space is word start
                p++;
                char_index++;
            }
            else {
                has_space = false;
                int bytes = str_utf8_decode((const char*)p, (size_t)(end - p), &codepoint);
                if (bytes <= 0) { p++;  codepoint = 0;  char_index++; }
                else { p += bytes;  char_index += bytes; }

                // Variation selectors are consumed by the preceding glyph; asking
                // CoreText to paint them standalone triggers hidden UI-font lookup.
                if (codepoint == 0x00AD || text_codepoint_has_zero_advance(codepoint)) {
                    text_justify_character_gap(&justify_context, codepoint, text_justify);
                    continue;
                }

                // Apply text-transform before loading glyph
                uint32_t tt_out[3];
                int tt_count = apply_text_transform_full(codepoint, text_transform, is_word_start, tt_out);
                codepoint = tt_out[0];
                is_word_start = false;

                for (int tti = 0; tti < tt_count; tti++) {
                    uint32_t render_cp = render_text_resolve_mathlive_fallback_glyph(tt_out[tti]);
                    if (render_cp == 0) continue;
                    bool character_gap = text_justify_character_gap(&justify_context, render_cp, text_justify);
                    if (text_codepoint_has_zero_advance(render_cp)) continue;
                    if (justification_per_gap > 0.0f &&
                        text_justify != CSS_VALUE_INTER_WORD &&
                        character_gap) {
                        x += justification_per_gap;
                    }

                    float synthetic_advance = 0.0f;
                    if (render_text_synthesized_space_advance(&rdcon->font, render_cp, &synthetic_advance)) {
                        x += synthetic_advance * s;
                        continue;
                    }

                    static int glyph_debug_count = 0;
                    if (render_text_trace_enabled() && glyph_debug_count < 500) {
                        glyph_debug_count++;
                    }

                    LoadedGlyph* glyph = render_text_load_glyph_for_paint(rdcon, render_cp, p, end);
                    if (!glyph) {
                        // draw a square box for missing glyph (scaled_space_width is in physical pixels)
                        float phys_size = font_handle_get_physical_size_px(font_box_handle(&rdcon->font));
                        const FontMetrics* _m = font_get_metrics(font_box_handle(&rdcon->font));
                        float box_height = (phys_size > 0) ? phys_size : (_m ? (_m->hhea_line_height * rdcon->raster_scale / 1.2f) : 16.0f);
                        Rect rect = {x + 1, y, (float)(scaled_space_width - 2), box_height};
                        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, 0xFF0000FF, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
                        x += scaled_space_width;
                    }
                    else {
                        // draw the glyph to the image buffer — use rendering ascender for glyph bitmap placement.
                        // font_get_rendering_ascender() returns the raw platform ascent (e.g. CoreText ascent on
                        // macOS) WITHOUT half-leading.  The glyph bitmap's bearing_y is measured from this
                        // platform baseline, so we must use the same value here.  Layout uses
                        // fprop->ascender (= init_ascender, which INCLUDES half-leading) for CSS vertical-
                        // align math, but text_rect.y already incorporates lead_y so the absolute baseline
                        // y = text_rect.y + rendering_ascender == init_ascender + lead_y  is correct.
                        float ascend;
                        {
                            uint64_t tfm1 = time_now_ns();
                            ascend = render_text_baseline_y(&rdcon->font, 0.0f) * rdcon->raster_scale;
                            uint64_t tfm2 = time_now_ns();
                            render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_FONT_METRICS,
                                time_elapsed_ms_f(tfm1, tfm2));
                        }
                        // Debug: Check bitmap data for Monaco (capped to avoid log spam)
                        static int bitmap_debug_count = 0;
                        const char* _dbg_fname = font_box_handle(&rdcon->font) ? font_handle_get_family_name(font_box_handle(&rdcon->font)) : NULL;
                        if (bitmap_debug_count < 50 && _dbg_fname &&
                            strcmp(_dbg_fname, "Monaco") == 0) {
                            bitmap_debug_count++;
                        }

                        uint64_t t3 = time_now_ns();

                        if (parent_elem && !shadow_prepass) {
                            Rect border = {rdcon->block.x, rdcon->block.y,
                                parent_elem->width * s, parent_elem->height * s};
                            render_paint_text_background_glyph(rdcon, parent_elem, border, &glyph->bitmap,
                                roundf(x + glyph->bitmap.bearing_x), roundf(y + ascend - glyph->bitmap.bearing_y));
                        }
                        Color glyph_saved_color = rdcon->color;
                        if (is_selected && has_selection_foreground)
                            rdcon->color = parent_elem->inl()->selection_color;
                        draw_glyph(rdcon, &glyph->bitmap, lroundf(x + glyph->bitmap.bearing_x), lroundf(y + ascend - glyph->bitmap.bearing_y));
                        rdcon->color = glyph_saved_color;
                        float glyph_advance = glyph->advance_x;
                        if (emphasis.handle && text_emphasis_marks_codepoint(render_cp)) {
                            float mark_x, mark_y;
                            LoadedGlyph* mark = render_text_emphasis_bitmap_glyph(&emphasis,
                                render_cp, x, y + ascend, glyph_advance, s, &mark_x, &mark_y);
                            if (mark) {
                                Color text_color = rdcon->color;
                                if (!rdcon->font.style->text_emphasis_color_current)
                                    rdcon->color = emphasis.color;
                                draw_glyph(rdcon, &mark->bitmap, mark_x, mark_y);
                                rdcon->color = text_color;
                            } else rdcon->paint_failed = true;
                        }
                        uint64_t t4 = time_now_ns();
                        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_GLYPH_DRAW,
                            time_elapsed_ms_f(t3, t4));
                        // advance to the next position (include letter-spacing)
                        x += glyph_advance + text_character_spacing(rdcon->font.style, render_cp) * s;
                    }
                } // end for tti (full case mapping expansion)
            }
        }
        x = render_text_trailing_marks(rdcon, text_rect, x, y);
        render_text_decorations(rdcon, str, text_rect, false);
    }
    }

    // Restore color and font (in case they were changed for PDF text)
    rdcon->font = saved_font;
    rdcon->color = saved_color;
    render_text_emphasis_release(&emphasis);
}

static void render_text_inline_background(RasterRenderContext* rdcon, ViewText* text_view,
                                          TextRect* text_rect, DomElement* parent_elem,
                                          float x, float y) {
    if (!rdcon || !text_view || !text_rect || !parent_elem ||
        parent_elem->view_type != RDT_VIEW_INLINE ||
        !parent_elem->bound) {
        return;
    }

    float s = rdcon->raster_scale;
    BackgroundProp* bg = parent_elem->boundary()->background;
    Color* bg_color = bg ? &bg->color : nullptr;
    const float* padding = parent_elem->boundary()->padding.values;
    float bg_pad_top = padding[CSS_BOX_SIDE_TOP] * s;
    float bg_pad_right = padding[CSS_BOX_SIDE_RIGHT] * s;
    float bg_pad_bottom = padding[CSS_BOX_SIDE_BOTTOM] * s;
    float bg_pad_left = padding[CSS_BOX_SIDE_LEFT] * s;
    float bg_radius = parent_elem->boundary()->border ?
        parent_elem->boundary()->border->radius.top_left * s : 0;

    Rect bg_rect = {
        x - bg_pad_left,
        y - bg_pad_top,
        text_rect->width * s + bg_pad_left + bg_pad_right,
        text_rect->height * s + bg_pad_top + bg_pad_bottom
    };

    if (bg_color && bg_color->a > 0) {
        if (bg_radius > 0) {
            float r_left = bg_radius;
            float r_right = bg_radius;
            RdtPath* p = rdt_path_new();
            float fx = bg_rect.x;
            float fy = bg_rect.y;
            float fw = bg_rect.width;
            float fh = bg_rect.height;
            rdt_path_move_to(p, fx + r_left, fy);
            rdt_path_line_to(p, fx + fw - r_right, fy);
            if (r_right > 0) rdt_path_cubic_to(p, fx + fw - r_right * 0.45f, fy, fx + fw, fy + r_right * 0.45f, fx + fw, fy + r_right);
            else rdt_path_line_to(p, fx + fw, fy);
            rdt_path_line_to(p, fx + fw, fy + fh - r_right);
            if (r_right > 0) rdt_path_cubic_to(p, fx + fw, fy + fh - r_right * 0.45f, fx + fw - r_right * 0.45f, fy + fh, fx + fw - r_right, fy + fh);
            else rdt_path_line_to(p, fx + fw, fy + fh);
            rdt_path_line_to(p, fx + r_left, fy + fh);
            if (r_left > 0) rdt_path_cubic_to(p, fx + r_left * 0.45f, fy + fh, fx, fy + fh - r_left * 0.45f, fx, fy + fh - r_left);
            else rdt_path_line_to(p, fx, fy + fh);
            rdt_path_line_to(p, fx, fy + r_left);
            if (r_left > 0) rdt_path_cubic_to(p, fx, fy + r_left * 0.45f, fx + r_left * 0.45f, fy, fx + r_left, fy);
            rdt_path_close(p);
            rc_fill_path(rdcon, p, *bg_color, RDT_FILL_WINDING, NULL);
            rdt_path_free(p);
        } else {
            rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &bg_rect, bg_color->c,
                &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
        }
    }

    render_text_inline_border(rdcon, text_view, text_rect, parent_elem, x, y, &bg_rect);
}

static void render_text_inline_border(RasterRenderContext* rdcon, ViewText* text_view,
                                      TextRect* text_rect, DomElement* parent_elem,
                                      float x, float y, const Rect* bg_rect) {
    (void)text_view;
    (void)x;
    (void)y;
    if (!rdcon || !text_view || !text_rect || !parent_elem ||
        parent_elem->view_type != RDT_VIEW_INLINE ||
        !parent_elem->bound || !parent_elem->boundary()->border || !bg_rect) {
        return;
    }

    float s = rdcon->raster_scale;
    BorderProp* border = parent_elem->boundary()->border;
    Rect border_rect = *bg_rect;
    border_rect.x -= border->width.left * s;
    border_rect.y -= border->width.top * s;
    border_rect.width += (border->width.left + border->width.right) * s;
    border_rect.height += (border->width.top + border->width.bottom) * s;

    if (border_rect.width <= 0.0f || border_rect.height <= 0.0f) {
        return;
    }

    ViewBlock* fragment_view = lam::unsafe_view_block_api_span(
        lam::view_require_element(static_cast<View*>(parent_elem)));
    Corner saved_radius = border->radius;
    resolve_border_radius_percentages(&border->radius, border_rect.width / s, border_rect.height / s);
    render_border(rdcon, fragment_view, border_rect);
    border->radius = saved_radius;
}

struct TextDecorationPaintContext {
    RasterRenderContext* raster;
    PaintList* paint;
    DisplayList* display;
    const FontBox* font;
    ScratchArena* scratch;
    float origin_x, origin_y, scale, glyph_scale;
    uint8_t lines;
    Color color;
    bool failed;
    bool force_color;
};

static void record_text_decoration_lines(TextDecorationPaintContext* context, unsigned char* text, TextRect* rect);

static void decoration_fill_rect(TextDecorationPaintContext* context, Rect* rect, uint32_t color) {
    if (context->failed) return;
    if (context->raster) {
        RasterRenderContext* raster = context->raster;
        int count = raster->dl ? raster->dl->item_count() : 0;
        const RdtMatrix* transform = render_state_current_transform(raster);
        if (transform) {
            RdtPath* path = rdt_path_new();
            if (!path) { context->failed = true; return; }
            rdt_path_add_rect(path, rect->x, rect->y, rect->width, rect->height, 0, 0);
            rc_fill_path(raster, path, Color{color}, RDT_FILL_WINDING, transform);
            rdt_path_free(path);
        } else rc_fill_surface_rect(raster, raster->ui_context->surface, rect, color,
            &raster->block.clip, raster->clip_shapes, raster->clip_shape_depth);
        if (!raster->dl || raster->dl->item_count() != count + 1) context->failed = true;
    } else if (context->display) {
        int count = context->display->item_count();
        dl_fill_rect(context->display, rect->x, rect->y, rect->width, rect->height, Color{color});
        if (context->display->item_count() != count + 1) context->failed = true;
    } else {
        size_t count = context->paint->size();
        paint_fill_rect(context->paint, rect->x, rect->y, rect->width, rect->height, Color{color});
        if (context->paint->size() != count + 1) context->failed = true;
    }
}

static bool decoration_record_path(TextDecorationPaintContext* context, RdtPath* path,
        Color color, float stroke_width, bool clip) {
    if (context->failed) return false;
    if (context->raster) {
        int count = context->raster->dl ? context->raster->dl->item_count() : 0;
        const RdtMatrix* transform = render_state_current_transform(context->raster);
        if (clip) rc_push_clip(context->raster, path, transform);
        else if (stroke_width > 0) rc_stroke_path(context->raster, path, color, stroke_width,
            RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, transform);
        else rc_fill_path(context->raster, path, color, RDT_FILL_WINDING, transform);
        if (!context->raster->dl || context->raster->dl->item_count() != count + 1) context->failed = true;
        return false;
    }
    if (context->display) {
        int count = context->display->item_count();
        if (clip) dl_push_clip(context->display, path, nullptr);
        else if (stroke_width > 0) dl_stroke_path(context->display, path, color, stroke_width,
            RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0, nullptr);
        else dl_fill_path(context->display, path, color, RDT_FILL_WINDING, nullptr);
        if (context->display->item_count() != count + 1) context->failed = true;
        return false;
    }
    int index = paint_list_count(context->paint);
    PaintOp operation = clip ? PAINT_PUSH_CLIP : stroke_width > 0 ? PAINT_STROKE_PATH : PAINT_FILL_PATH;
    if (clip) paint_push_clip(context->paint, path, nullptr);
    else if (stroke_width > 0) paint_stroke_path(context->paint, path, color, stroke_width,
        RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0, nullptr);
    else paint_fill_path(context->paint, path, color, RDT_FILL_WINDING, nullptr);
    // vector commands outlive the recorder; transfer each shape before its caller releases scratch.
    // paint_push_clip already owns its clone; fill/stroke commands require transfer.
    bool recorded = paint_list_count(context->paint) == index + 1;
    bool owned = !clip && recorded &&
        paint_list_take_path_payload(context->paint, index, operation);
    if (!recorded || (!clip && !owned)) {
        context->paint->rewind((size_t)index);
        context->failed = true;
    }
    return owned;
}

typedef struct SkipInkGap {
    float x0;
    float x1;
} SkipInkGap;

static bool render_text_paint_background_and_shadows(RasterRenderContext* rdcon, unsigned char* str,
                                              TextRect* text_rect, TextShadow* text_shadow,
                                              CssEnum text_transform, bool preserve_spaces,
                                              float space_width, float scaled_space_width,
                                              float cjk_spacing, CssEnum justify_mode,
                                              float x, float y, DomElement* parent_elem,
                                              const RenderTextEmphasis* emphasis) {
    if (!rdcon || !str || !text_rect || !text_shadow ||
        !font_box_handle(&rdcon->font) || !rdcon->font.style) return false;
    float s = rdcon->raster_scale;
    bool background_painted = false, success = true;
    render_visit_text_shadows_reverse(text_shadow, [&](TextShadow* shadow) {
        if (!success) return;
        OffscreenRenderArenas arenas;
        if (!arenas.init("render.text.shadow", "render.text.shadow.list", "render.text.shadow.scratch")) {
            success = false; return;
        }
        DisplayList shadow_list = {}; dl_init(&shadow_list, arenas.list_arena);
        bool direct = shadow->blur_radius <= 0.0f && shadow->color.a == 255;
        Color opaque_shadow = shadow->color; opaque_shadow.a = 255;
        TextDecorationPaintContext decoration = {nullptr, nullptr, &shadow_list, &rdcon->font,
            &rdcon->scratch, rdcon->block.x + roundf(shadow->offset_x * s),
            rdcon->block.y + roundf(shadow->offset_y * s), s, 1.0f,
            CSS_TEXT_DECO_UNDERLINE | CSS_TEXT_DECO_OVERLINE,
            opaque_shadow, false, true};
        if (!direct) record_text_decoration_lines(&decoration, str, text_rect);
        float left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
        unsigned char* sp = str + text_rect->start_index;
        unsigned char* s_end = sp + text_rect->length;
        float sx_pos = x;
        bool s_has_space = render_text_source_has_space(str, text_rect->start_index, preserve_spaces);
        FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);
        bool s_word_start = render_text_source_word_start(str, text_rect->start_index);
        TextJustifyContext justify_context = {};

        while (sp < s_end) {
            if (is_space(*sp)) {
                if (preserve_spaces || !s_has_space) {
                    s_has_space = true;
                    if (text_justify_character_gap(&justify_context, ' ', justify_mode))
                        sx_pos += cjk_spacing;
                    sx_pos += space_width;
                }
                s_word_start = true;
                sp++;
                continue;
            }

            s_has_space = false;
            uint32_t s_cp;
            int s_bytes = str_utf8_decode((const char*)sp, (size_t)(s_end - sp), &s_cp);
            if (s_bytes <= 0) {
                sp++;
                continue;
            }
            sp += s_bytes;

            if (s_cp == 0x00AD) {
                continue;
            }

            uint32_t s_tt_out[3];
            int s_tt_count = apply_text_transform_full(s_cp, text_transform, s_word_start, s_tt_out);
            s_word_start = false;

            for (int sti = 0; sti < s_tt_count; sti++) {
                uint32_t s_tt_cp = s_tt_out[sti];
                if (s_tt_cp == 0) {
                    continue;
                }
                bool character_gap = text_justify_character_gap(&justify_context, s_tt_cp, justify_mode);
                if (text_codepoint_has_zero_advance(s_tt_cp)) continue;
                if (cjk_spacing > 0.0f && character_gap)
                    sx_pos += cjk_spacing;

                float synthetic_advance = 0.0f;
                if (render_text_synthesized_space_advance(&rdcon->font, s_tt_cp, &synthetic_advance)) {
                    sx_pos += synthetic_advance * s;
                    continue;
                }

                LoadedGlyph* s_glyph = font_load_glyph(font_box_handle(&rdcon->font), &sd, s_tt_cp, true);
                if (!s_glyph) {
                    sx_pos += scaled_space_width;
                    continue;
                }

                float s_ascend;
                {
                    uint64_t tfm1 = time_now_ns();
                    s_ascend = render_text_baseline_y(&rdcon->font, 0.0f) * rdcon->raster_scale;
                    uint64_t tfm2 = time_now_ns();
                    render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_FONT_METRICS,
                        time_elapsed_ms_f(tfm1, tfm2));
                }

                float glyph_x = roundf(sx_pos + s_glyph->bitmap.bearing_x);
                float glyph_y = roundf(y + s_ascend - s_glyph->bitmap.bearing_y);
                if (!background_painted && parent_elem) {
                    Rect border = {rdcon->block.x, rdcon->block.y,
                        parent_elem->width * s, parent_elem->height * s};
                    render_paint_text_background_glyph(rdcon, parent_elem, border, &s_glyph->bitmap, glyph_x, glyph_y);
                }
                float sx = glyph_x + roundf(shadow->offset_x * s);
                float sy = glyph_y + roundf(shadow->offset_y * s);
                int count = shadow_list.item_count();
                dl_draw_glyph(&shadow_list, &s_glyph->bitmap,
                    (int)sx, (int)sy, opaque_shadow, false, nullptr, nullptr, 0); // INT_CAST_OK: rounded glyph bitmap destination pixels
                if (shadow_list.item_count() != count + 1) decoration.failed = true;
                left = fminf(left, sx); top = fminf(top, sy);
                right = fmaxf(right, sx + (float)s_glyph->bitmap.width);
                bottom = fmaxf(bottom, sy + (float)s_glyph->bitmap.height);
                // loading the mark reuses LoadedGlyph storage; retain the text advance first (D4.5.1v4).
                float text_advance = s_glyph->advance_x;
                if (emphasis && emphasis->handle && text_emphasis_marks_codepoint(s_tt_cp)) {
                    float mark_x, mark_y;
                    LoadedGlyph* mark = render_text_emphasis_bitmap_glyph(emphasis, s_tt_cp,
                        sx_pos + roundf(shadow->offset_x * s),
                        y + s_ascend + roundf(shadow->offset_y * s),
                        text_advance, s, &mark_x, &mark_y);
                    if (!mark) decoration.failed = true;
                    else {
                        int mark_count = shadow_list.item_count();
                        dl_draw_glyph(&shadow_list, &mark->bitmap,
                            (int)mark_x, (int)mark_y, opaque_shadow, false, nullptr, nullptr, 0); // INT_CAST_OK: rounded emphasis bitmap destination pixels
                        if (shadow_list.item_count() != mark_count + 1) decoration.failed = true;
                    }
                }
                sx_pos += text_advance + text_character_spacing(rdcon->font.style, s_tt_cp) * s;
            }
        }

        if (direct) {
            // opaque sharp shadows need no capture; recording directly avoids intermediate AA rounding.
            // glyph gathering has already placed clipped text backgrounds below the complete shadow.
            decoration.raster = rdcon;
            decoration.display = nullptr;
            record_text_decoration_lines(&decoration, str, text_rect);
            for (int index = 0; index < shadow_list.item_count(); index++) {
                DlDrawGlyph* glyph = &shadow_list.data()[index].draw_glyph;
                Color saved_color = rdcon->color;
                rdcon->color = glyph->color;
                int count = rdcon->dl ? rdcon->dl->item_count() : 0;
                draw_glyph(rdcon, &glyph->bitmap, glyph->x, glyph->y);
                rdcon->color = saved_color;
                if (glyph->bitmap.buffer && glyph->bitmap.width > 0 && glyph->bitmap.height > 0 &&
                        (!rdcon->dl || rdcon->dl->item_count() != count + 1)) decoration.failed = true;
            }
            decoration.lines = CSS_TEXT_DECO_LINE_THROUGH;
            record_text_decoration_lines(&decoration, str, text_rect);
            success = success && !decoration.failed;
            background_painted = true;
            dl_destroy(&shadow_list); arenas.destroy();
            return;
        }
        decoration.lines = CSS_TEXT_DECO_LINE_THROUGH;
        record_text_decoration_lines(&decoration, str, text_rect);
        if (decoration.failed) success = false;
        Bound content = dl_content_bounds(&shadow_list);
        left = fminf(left, content.left); top = fminf(top, content.top);
        right = fmaxf(right, content.right); bottom = fmaxf(bottom, content.bottom);
        background_painted = true;
        if (decoration.failed || !(right > left && bottom > top) || !shadow->color.a) {
            dl_destroy(&shadow_list); arenas.destroy(); return;
        }
        float sigma = shadow->blur_radius * s * 0.5f;
        float pad = render_filter_blur_support(sigma);
        Bound bounds = {floorf(left - pad), floorf(top - pad), ceilf(right + pad), ceilf(bottom + pad)};
        FilterFunction blur = {}; blur.type = FILTER_BLUR; blur.params.blur_radius = sigma;
        FilterProp filter = {}; filter.functions = lam::own(&blur); filter.functions_borrowed = true;
        if (sigma > 0.0f) dl_apply_filter(&shadow_list, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top, &filter, &bounds);
        // only the glyph layer is filtered; the document backdrop is composited afterward.
        ImageSurface* image = render_display_list_snapshot(&shadow_list,
            mem_context_process(MEM_ROLE_RENDER), bounds, s);
        dl_destroy(&shadow_list); arenas.destroy();
        if (!image) { success = false; return; }
        image_surface_snapshot_publish(image);
        Rect destination = {bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top};
        bool complete = render_painter_blit_surface_scaled(rdcon, image, nullptr,
            rdcon->ui_context->surface, &destination, &rdcon->block.clip, SCALE_MODE_NEAREST,
            rdcon->clip_shapes, rdcon->clip_shape_depth, shadow->color.a);
        if (!complete) success = false;
        image_surface_snapshot_release(image);
    });
    return success;
}

static LoadedGlyph* render_text_load_glyph_for_paint(RasterRenderContext* rdcon, uint32_t codepoint,
                                                     unsigned char* cursor, unsigned char* end) {
    if (!rdcon || !font_box_handle(&rdcon->font) || !rdcon->font.style) {
        return nullptr;
    }

    uint64_t t1 = time_now_ns();
    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);
    bool emoji_presentation = false;
    if (cursor < end) {
        uint32_t peek_cp;
        int peek_bytes = str_utf8_decode((const char*)cursor, (size_t)(end - cursor), &peek_cp);
        if (peek_bytes > 0 && peek_cp == 0xFE0F) {
            emoji_presentation = true;
        }
    }
    if (!emoji_presentation && utf_is_emoji_presentation_default(codepoint)) {
        emoji_presentation = true;
    }

    LoadedGlyph* glyph = emoji_presentation
        ? font_load_glyph_emoji(font_box_handle(&rdcon->font), &sd, codepoint, true)
        : font_load_glyph(font_box_handle(&rdcon->font), &sd, codepoint, true);
    uint64_t t2 = time_now_ns();
    render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_GLYPH_LOAD,
        time_elapsed_ms_f(t1, t2));
    return glyph;
}

static float render_text_trailing_marks(RasterRenderContext* rdcon, TextRect* text_rect,
                                        float x, float y) {
    if (!rdcon || !font_box_handle(&rdcon->font) || !rdcon->font.style || !text_rect) {
        return x;
    }

    if (text_rect->has_trailing_hyphen) {
        const char* trailing_character = text_rect->trailing_hyphenate_character;
        if (!trailing_character) trailing_character = "-";
        FontStyleDesc sd_h = font_style_desc_from_prop(rdcon->font.style);
        const unsigned char* cursor = (const unsigned char*)trailing_character;
        const unsigned char* end = cursor + strlen(trailing_character);
        while (cursor < end) {
            uint32_t codepoint = *cursor;
            int bytes = 1;
            if (codepoint >= 128) {
                bytes = str_utf8_decode((const char*)cursor,
                    (size_t)(end - cursor), &codepoint);
                if (bytes <= 0) bytes = 1;
            }
            float synthetic_advance = 0.0f;
            if (render_text_synthesized_space_advance(&rdcon->font, codepoint, &synthetic_advance)) {
                x += synthetic_advance * rdcon->raster_scale;
                cursor += bytes;
                continue;
            }
            LoadedGlyph* h_glyph = font_load_glyph(
                font_box_handle(&rdcon->font), &sd_h, codepoint, true);
            if (h_glyph) {
                float ascend = font_get_rendering_ascender(font_box_handle(&rdcon->font)) * rdcon->raster_scale;
                draw_glyph(rdcon, &h_glyph->bitmap,
                           lroundf(x + h_glyph->bitmap.bearing_x),
                           lroundf(y + ascend - h_glyph->bitmap.bearing_y));
                x += h_glyph->advance_x + rdcon->font.style->letter_spacing * rdcon->raster_scale;
            }
            cursor += bytes;
        }
    }

    if (text_rect->has_trailing_ellipsis) {
        FontStyleDesc sd_e = font_style_desc_from_prop(rdcon->font.style);
        LoadedGlyph* e_glyph = font_load_glyph(font_box_handle(&rdcon->font), &sd_e, 0x2026, true);
        if (e_glyph) {
            float ascend = font_get_rendering_ascender(font_box_handle(&rdcon->font)) * rdcon->raster_scale;
            draw_glyph(rdcon, &e_glyph->bitmap,
                       lroundf(x + e_glyph->bitmap.bearing_x),
                       lroundf(y + ascend - e_glyph->bitmap.bearing_y));
        }
    }

    return x;
}

static int collect_skip_ink_gaps(TextDecorationPaintContext* context, unsigned char* str,
                                 TextRect* text_rect, float deco_y_top, float deco_y_bot,
                                 float stroke_thickness, SkipInkGap* gaps) {
    float s = context->scale, bitmap_scale = context->glyph_scale;
    float x = context->origin_x + text_rect->x * s;
    float y_base = context->origin_y + text_rect->y * s;
    float ascend = font_get_rendering_ascender(font_box_handle(context->font)) * s;
    int gap_count = 0;
    float pad = fmaxf(2.0f, stroke_thickness);

    const unsigned char* p = str + text_rect->start_index;
    const unsigned char* end = p + text_rect->length;
    FontStyleDesc sd = font_style_desc_from_prop(context->font->style);

    while (p < end) {
        if (is_space(*p)) {
            x += context->font->style->space_width * s;
            p++;
            continue;
        }

        uint32_t codepoint;
        if (!layout_utf8_next_codepoint(&p, end, &codepoint)) continue;
        if (codepoint == 0x00AD || text_codepoint_has_zero_advance(codepoint)) continue;

        float synthetic_advance = 0.0f;
        if (render_text_synthesized_space_advance(context->font, codepoint, &synthetic_advance)) {
            x += synthetic_advance * s;
            continue;
        }

        LoadedGlyph* glyph = font_load_glyph(font_box_handle(context->font), &sd, codepoint, true);
        if (!glyph) {
            x += context->font->style->space_width * s;
            continue;
        }

        float glyph_top = y_base + ascend - glyph->bitmap.bearing_y * bitmap_scale;
        float glyph_bot = glyph_top + glyph->bitmap.height * bitmap_scale;
        float glyph_left = x + glyph->bitmap.bearing_x * bitmap_scale;
        float glyph_right = glyph_left + glyph->bitmap.width * bitmap_scale;

        float overlap = fminf(glyph_bot, deco_y_bot) - fmaxf(glyph_top, deco_y_top);
        float min_overlap = fmaxf(stroke_thickness * 0.5f, 2.0f);
        if (overlap >= min_overlap && glyph->bitmap.width > 0) {
            int bm_row_start = (int)fmaxf((deco_y_top - glyph_top) / bitmap_scale, 0.0f); // INT_CAST_OK: glyph bitmap row index
            int bm_row_end = (int)fminf((deco_y_bot - glyph_top) / bitmap_scale, (float)glyph->bitmap.height); // INT_CAST_OK: glyph bitmap row index
            int ink_col_min = glyph->bitmap.width;
            int ink_col_max = -1;
            uint8_t* buf = glyph->bitmap.buffer;
            int pitch = glyph->bitmap.pitch;
            const uint8_t ink_thresh = 64;
            if (buf && glyph->bitmap.pixel_mode == GLYPH_PIXEL_GRAY) {
                for (int row = bm_row_start; row < bm_row_end; row++) {
                    uint8_t* rowp = buf + row * pitch;
                    for (int col = 0; col < glyph->bitmap.width; col++) {
                        if (rowp[col] >= ink_thresh) {
                            if (col < ink_col_min) ink_col_min = col;
                            if (col > ink_col_max) ink_col_max = col;
                        }
                    }
                }
            }
            if (ink_col_max >= ink_col_min) {
                gaps[gap_count].x0 = glyph_left + ink_col_min * bitmap_scale - pad;
                gaps[gap_count].x1 = glyph_left + (ink_col_max + 1.0f) * bitmap_scale + pad;
            } else {
                gaps[gap_count].x0 = glyph_left - pad;
                gaps[gap_count].x1 = glyph_right + pad;
            }
            gap_count++;
        }

        x += glyph->advance_x * bitmap_scale + context->font->style->letter_spacing * s;
    }
    return gap_count;
}

static bool draw_deco_with_gaps(TextDecorationPaintContext* context, Rect rect, uint32_t color,
        SkipInkGap* gaps, int gap_count, RdtPath* decoration = nullptr,
        float stroke_width = 0.0f) {
    RdtPath* clip = decoration && gap_count ? rdt_path_new() : nullptr;
    if (decoration && gap_count && !clip) {
        context->failed = true;
        log_error("[TEXT_DECO_GAPS] cannot allocate decoration gap clip");
        return false;
    }
    bool owns_decoration = false;
    size_t segment_count = 0;
    auto emit_segment = [&](float left, float right) {
        Rect segment = {left, rect.y, right - left, rect.height};
        segment_count++;
        if (decoration) {
            // the interval mask restricts x; its y edges must lie outside the pattern's antialias fringe.
            if (clip) rdt_path_add_rect(clip, segment.x,
                context->raster || context->display ? segment.y : segment.y - segment.height,
                segment.width, context->raster || context->display ? segment.height : segment.height * 3.0f, 0, 0);
        } else decoration_fill_rect(context, &segment, color);
    };
    float x_start = rect.x, x_end = rect.x + rect.width;
    for (int i = 0; i < gap_count; i++) {
        float end = fminf(gaps[i].x0, x_end);
        if (end > x_start) emit_segment(x_start, end);
        x_start = fmaxf(x_start, gaps[i].x1);
        if (x_start >= x_end) break;
    }
    if (x_start < x_end) emit_segment(x_start, x_end);
    if (decoration && gap_count && segment_count == 0) {
        // no interval survives: there is no decoration geometry to submit to any sink.
        rdt_path_free(clip);
        return false;
    }
    if (decoration) {
        // clip the complete pattern so skipping ink never restarts its dash or wave phase.
        bool owns_clip = clip && decoration_record_path(context, clip, Color{}, 0, true);
        if (!context->failed)
            owns_decoration = decoration_record_path(context, decoration, Color{color}, stroke_width, false);
        if (clip && !context->failed) {
            if (context->raster) {
                int count = context->raster->dl ? context->raster->dl->item_count() : 0;
                rc_pop_clip(context->raster);
                if (!context->raster->dl || context->raster->dl->item_count() != count + 1) context->failed = true;
            } else if (context->display) {
                int count = context->display->item_count();
                dl_pop_clip(context->display);
                if (context->display->item_count() != count + 1) context->failed = true;
            }
            else {
                size_t count = context->paint->size();
                paint_pop_clip(context->paint);
                if (context->paint->size() != count + 1) context->failed = true;
            }
        }
        if (owns_clip) clip = nullptr;
    }
    rdt_path_free(clip);
    return owns_decoration;
}

static void render_single_text_decoration(TextDecorationPaintContext* context, unsigned char* str,
                                          TextRect* text_rect, CssEnum line) {
    if (!context || !context->font->style || !text_rect) {
        return;
    }
    if (line == CSS_VALUE_NONE || line == CSS_VALUE__UNDEF ||
        line == CSS_VALUE_BLINK) {
        return;
    }

    float s = context->scale;
    const FontMetrics* deco_m = font_get_metrics(font_box_handle(context->font));
    float thickness = context->font->style->text_deco_thickness > 0
        ? context->font->style->text_deco_thickness
        : fmaxf(deco_m ? deco_m->underline_thickness : 1.0f, 1.0f);
    // only raster destinations snap; vector commands retain logical CSS geometry (RSC7).
    thickness *= s;
    if (context->raster || context->display) thickness = fmaxf(ceilf(thickness), 1.0f);

    Color deco_color = context->force_color || context->font->style->text_deco_color_current
        ? context->color : context->font->style->text_deco_color;
    CssEnum deco_style = context->font->style->text_deco_style;
    if (deco_style == CSS_VALUE__UNDEF || deco_style == 0) deco_style = CSS_VALUE_SOLID;

    Rect rect = {0, 0, 0, 0};
    bool draw_deco = true;
    float deco_ascend = font_get_rendering_ascender(font_box_handle(context->font)) * s;
    if (line == CSS_VALUE_UNDERLINE) {
        float underline_pos = deco_m ? deco_m->underline_position : 0;
        float offset = context->font->style->text_underline_offset;
        if (context->font->style->text_underline_offset_mode == 2) {
            offset *= context->font->style->font_size / 100.0f;
        }
        rect.x = context->origin_x + text_rect->x * s;
        float baseline_y = context->origin_y + text_rect->y * s + deco_ascend;
        float underline_depth = -underline_pos;
        if (context->font->style->text_underline_position == CSS_VALUE_UNDER && deco_m) {
            // under places the stroke below the font descent; auto/from-font use its underline metric.
            underline_depth = fmaxf(underline_depth, -deco_m->descender);
        }
        rect.y = baseline_y + (underline_depth + offset) * s;
        if (context->raster || context->display) rect.y = roundf(rect.y);
    }
    else if (line == CSS_VALUE_OVERLINE) {
        rect.x = context->origin_x + text_rect->x * s;
        // overline sits on the text-over edge; starting inside the fragment pushed it below browser output.
        rect.y = context->origin_y + text_rect->y * s - thickness;
        if (context->raster || context->display) rect.y = floorf(rect.y);
    }
    else if (line == CSS_VALUE_LINE_THROUGH) {
        rect.x = context->origin_x + text_rect->x * s;
        float strike_y;
        if (deco_m && deco_m->strikeout_position > 0) {
            // browser line-through follows the font OS/2 strikeout metric; x-height fallback was too low.
            strike_y = deco_m->strikeout_position;
        } else if (deco_m && deco_m->x_height > 0) {
            strike_y = deco_m->x_height * 0.5f;
        } else {
            strike_y = deco_ascend * 0.3f / s;
        }
        if (context->font->style->text_deco_thickness <= 0 &&
            deco_m && deco_m->strikeout_size > 0)
            thickness = context->raster || context->display ? fmaxf(ceilf(deco_m->strikeout_size * s), 1.0f)
                : deco_m->strikeout_size * s;
        // browser treats the strikeout metric as the stroke position; using it as the top edge paints too low.
        rect.y = context->origin_y + text_rect->y * s + deco_ascend - strike_y * s - thickness;
        if (context->raster || context->display) rect.y = floorf(rect.y);
    }
    else {
        draw_deco = false;
    }
    if (!draw_deco) {
        return;
    }

    rect.width = text_rect->width * s;
    rect.height = thickness;

    bool styled = deco_style == CSS_VALUE_DASHED || deco_style == CSS_VALUE_DOTTED ||
        deco_style == CSS_VALUE_DOUBLE || deco_style == CSS_VALUE_WAVY;
    RdtPath* path = styled ? render_path_create_decoration(&rect, deco_style) : nullptr;
    if (styled && !path) {
        context->failed = true;
        log_error("[TEXT_DECO_GAPS] cannot allocate decoration pattern");
        return;
    }
    float stroke_width = deco_style == CSS_VALUE_WAVY ? fmaxf(1.0f, thickness * .5f) : 0.0f;
    Rect ink_band = rect;
    if (path) {
        float left, top, right, bottom;
        if (rdt_path_get_bounds(path, &left, &top, &right, &bottom)) {
            // gap masks select horizontal intervals; integer vertical edges retain the stroke antialias coverage.
            ink_band.y = top - stroke_width * .5f;
            float bottom_edge = bottom + stroke_width * .5f;
            if (context->raster || context->display) {
                ink_band.y = floorf(ink_band.y);
                bottom_edge = ceilf(bottom_edge);
            }
            ink_band.height = bottom_edge - ink_band.y;
        }
    }
    bool skip = context->font->style->text_deco_skip_ink == CSS_VALUE_ALL
        ? line == CSS_VALUE_UNDERLINE || line == CSS_VALUE_OVERLINE
        : context->font->style->text_deco_skip_ink != CSS_VALUE_NONE && line == CSS_VALUE_UNDERLINE;
    bool owns_path = false;
    if (skip) {
        ScratchScope scope(context->scratch);
        // at most one gap per decoded codepoint; source bytes bound the scoped storage.
        SkipInkGap* gaps = scope.array<SkipInkGap>((size_t)text_rect->length);
        if (!gaps && text_rect->length > 0) {
            context->failed = true;
            log_error("[TEXT_DECO_GAPS] cannot retain complete glyph ink gaps");
        } else {
            int count = collect_skip_ink_gaps(context, str, text_rect,
                ink_band.y, ink_band.y + ink_band.height, thickness, gaps);
            owns_path = draw_deco_with_gaps(context, ink_band, deco_color.c, gaps, count, path, stroke_width);
        }
    } else owns_path = draw_deco_with_gaps(context, ink_band, deco_color.c, nullptr, 0, path, stroke_width);
    if (!owns_path) rdt_path_free(path);

}

static void record_text_decoration_lines(TextDecorationPaintContext* context, unsigned char* str,
                                    TextRect* text_rect) {
    if (!context || !context->font->style) return;
    const FontProp* font = context->font->style;
    static const struct { CssEnum line; uint8_t flag; } lines[] = {
        {CSS_VALUE_UNDERLINE, CSS_TEXT_DECO_UNDERLINE},
        {CSS_VALUE_OVERLINE, CSS_TEXT_DECO_OVERLINE},
        {CSS_VALUE_LINE_THROUGH, CSS_TEXT_DECO_LINE_THROUGH}
    };
    for (const auto& line : lines)
        if ((context->lines & line.flag) &&
                (font->text_deco == line.line || (font->text_deco_extra & line.flag)))
            render_single_text_decoration(context, str, text_rect, line.line);

}


static void render_text_decorations(RasterRenderContext* raster, unsigned char* text, TextRect* rect,
                                    bool before_text) {
    uint8_t lines = before_text ? CSS_TEXT_DECO_UNDERLINE | CSS_TEXT_DECO_OVERLINE
        : CSS_TEXT_DECO_LINE_THROUGH;
    TextDecorationPaintContext context = {raster, nullptr, nullptr, &raster->font, &raster->scratch,
        raster->block.x, raster->block.y, raster->raster_scale, 1.0f,
        lines, raster->color, false};
    record_text_decoration_lines(&context, text, rect);
    raster->paint_failed = raster->paint_failed || context.failed;
}

static bool render_text_rect_ink_bounds(RasterRenderContext* raster, unsigned char* text,
    TextRect* rect, const RenderTextEmphasis* emphasis, TextShadow* shadows,
    float x, float y, float width, float height, Bound* result) {
    Bound ink = {x, y, x + width, y + height};
    float scale = raster->raster_scale;
    if (emphasis && emphasis->handle) {
        LoadedGlyph* mark = font_load_glyph(emphasis->handle, &emphasis->style, emphasis->codepoint, true);
        if (!mark) return false;
        float baseline = render_text_baseline_y(&raster->font, 0.0f) * scale + y + emphasis->baseline_offset * scale;
        ink.top = fminf(ink.top, roundf(baseline - mark->bitmap.bearing_y));
        ink.bottom = fmaxf(ink.bottom, roundf(baseline - mark->bitmap.bearing_y) + mark->bitmap.height);
        // marks are centered in the text advance and may overhang the first/last glyph.
        float overhang = fmaxf(mark->advance_x, (float)mark->bitmap.width) * 0.5f + fabsf((float)mark->bitmap.bearing_x);
        ink.left -= overhang; ink.right += overhang;
    }
    DisplayList decorations;
    TextDecorationPaintContext context = {nullptr, nullptr, &decorations, &raster->font, &raster->scratch,
        raster->block.x, raster->block.y, scale, 1.0f,
        CSS_TEXT_DECO_UNDERLINE | CSS_TEXT_DECO_OVERLINE | CSS_TEXT_DECO_LINE_THROUGH,
        Color{0xff000000}, false, true};
    // use the actual shared line recorder so authored offsets and wavy/stroke extents stay in the culling bounds.
    record_text_decoration_lines(&context, text, rect);
    if (context.failed) { dl_destroy(&decorations); return false; }
    if (decorations.item_count()) {
        Bound lines = dl_content_bounds(&decorations);
        ink.left = fminf(ink.left, lines.left); ink.top = fminf(ink.top, lines.top);
        ink.right = fmaxf(ink.right, lines.right); ink.bottom = fmaxf(ink.bottom, lines.bottom);
    }
    dl_destroy(&decorations);
    Bound source = ink;
    for (TextShadow* shadow = shadows; shadow; shadow = shadow->next) {
        if (!shadow->color.a) continue;
        float pad = render_filter_blur_support(shadow->blur_radius * scale * 0.5f);
        float dx = roundf(shadow->offset_x * scale), dy = roundf(shadow->offset_y * scale);
        ink.left = fminf(ink.left, source.left + dx - pad); ink.top = fminf(ink.top, source.top + dy - pad);
        ink.right = fmaxf(ink.right, source.right + dx + pad); ink.bottom = fmaxf(ink.bottom, source.bottom + dy + pad);
    }
    *result = ink;
    return true;
}

static bool render_text_rect_misses_clip(RasterRenderContext* raster, unsigned char* text,
    TextRect* rect, const RenderTextEmphasis* emphasis, TextShadow* shadows,
    float x, float y, float width, float height) {
    Bound ink = {x, y, x + width, y + height};
    if (!render_text_bounds_miss_clip(raster, ink)) return false;
    if (!render_text_rect_ink_bounds(raster, text, rect, emphasis, shadows,
            x, y, width, height, &ink)) return false;
    return render_text_bounds_miss_clip(raster, ink);
}

struct RenderTextOverflowContext {
    RasterRenderContext* raster;
    ViewBlock* owner;
    RdtLogicalPoint origin;
    Bound border;
    bool overflow;
};

static bool render_text_overflow_visit(View* view, bool entering, void* pointer) {
    if (!entering || view->view_type != RDT_VIEW_TEXT) return true;
    RenderTextOverflowContext* context = (RenderTextOverflowContext*)pointer;
    RasterRenderContext* raster = context->raster;
    ViewText* text = lam::view_require_text(view);
    if (view_visibility_is_hidden(text) || !text->rect || !text->font) return true;
    FontProp* style = text->font;
    if (!style->text_emphasis_enabled && !style->text_shadow &&
        !style->text_deco_extra && style->text_deco == CSS_VALUE_NONE) return true;
    // layout bounds cover an intervening clip; transformed scopes need a conservative paint walk.
    for (DomNode* parent = text->parent; parent && parent != context->owner; parent = parent->parent) {
        if (!parent->is_element()) continue;
        DomElement* element = lam::dom_require_element(parent);
        if (transform_has_functions(element->transform)) {
            context->overflow = true;
            return false;
        }
        if ((element->blk && element->block()->contain_paint) ||
            (element->scroller && (element->scroll()->overflow_x != CSS_VALUE_VISIBLE ||
                element->scroll()->overflow_y != CSS_VALUE_VISIBLE))) return true;
    }
    FontBox saved_font = raster->font;
    BlockBlot saved_block = raster->block;
    setup_font(raster->ui_context, &raster->font, text->font);
    RenderTextEmphasis emphasis = {};
    bool success = render_text_emphasis_resolve(raster->ui_context->font_ctx,
        &raster->font, raster->color, &emphasis);
    float scale = raster->raster_scale;
    for (TextRect* rect = text->rect; success && rect; rect = rect->next) {
        RdtLogicalPoint point = view_geometry_text_rect_document_origin(text, rect);
        float x = context->border.left + (point.x - context->origin.x) * scale;
        float y = context->border.top + (point.y - context->origin.y) * scale;
        raster->block.x = x - rect->x * scale;
        raster->block.y = y - rect->y * scale;
        Bound ink;
        success = render_text_rect_ink_bounds(raster, text->text_data(), rect, &emphasis,
            raster->font.style->text_shadow, x, y, rect->width * scale, rect->height * scale, &ink);
        if (!success || ink.left < context->border.left || ink.top < context->border.top ||
            ink.right > context->border.right || ink.bottom > context->border.bottom) {
            context->overflow = true;
            break;
        }
    }
    if (!success) context->overflow = true;
    render_text_emphasis_release(&emphasis);
    raster->font = saved_font;
    raster->block = saved_block;
    return !context->overflow;
}

bool render_text_subtree_overflows_block(RasterRenderContext* raster, ViewBlock* block) {
    if (!raster || !raster->ui_context || !block) return false;
    float scale = raster->raster_scale > 0.0f ? raster->raster_scale : 1.0f;
    RenderTextOverflowContext context = {raster, block, view_geometry_node_document_origin(block),
        view_geometry_rect_to_bound(render_geometry_block_border_rect(&raster->block, block, scale)), false};
    // shared paint bounds include marks, authored decoration offsets and actual blur support (FT-17/RSC7).
    view_geometry_walk_tree(block, render_text_overflow_visit, &context);
    return context.overflow;
}


bool render_paint_text_decorations(PaintList* paint, const FontBox* font,
        unsigned char* text, TextRect* rect, float origin_x, float origin_y,
        Color color, bool before_text, const Color* override_color) {
    if (!paint || !font || !font->style || !text || !rect) return false;
    uint8_t lines = before_text ? CSS_TEXT_DECO_UNDERLINE | CSS_TEXT_DECO_OVERLINE
        : CSS_TEXT_DECO_LINE_THROUGH;
    if (!(font->style->text_deco_extra & lines) &&
        !(before_text ? font->style->text_deco == CSS_VALUE_UNDERLINE ||
            font->style->text_deco == CSS_VALUE_OVERLINE :
            font->style->text_deco == CSS_VALUE_LINE_THROUGH)) return true;
    FontHandle* handle = font_box_handle(font);
    if (!handle) return false;
    float requested = font_handle_get_size_px(handle);
    float physical = font_handle_get_physical_size_px(handle);
    if (!(requested > 0.0f) || !(physical > 0.0f)) return false;
    Arena* arena = mem_arena_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_RENDER,
        "render.decoration.scratch");
    if (!arena) return false;
    ScratchArena scratch = {}; scratch_init(&scratch, arena);
    size_t checkpoint = paint->size();
    TextDecorationPaintContext context = {nullptr, paint, nullptr, font, &scratch,
        origin_x, origin_y, 1.0f, requested / physical, lines, override_color ? *override_color : color, false, override_color != nullptr};
    record_text_decoration_lines(&context, text, rect);
    scratch_release(&scratch); mem_arena_destroy(arena);
    if (context.failed) paint->rewind(checkpoint);
    return !context.failed;
}


struct RenderEmphasisCaptureContext {
    PaintList* paint;
    const RenderTextEmphasis* emphasis;
    float density;
    Color color;
    bool has_ink;
};

LoadedGlyph* render_text_emphasis_bitmap_glyph(const RenderTextEmphasis* emphasis,
    uint32_t codepoint, float x, float baseline_y, float advance, float density,
    float* mark_x, float* mark_y) {
    if (!emphasis || !emphasis->handle || !text_emphasis_marks_codepoint(codepoint) ||
        !(density > 0.0f)) return nullptr;
    LoadedGlyph* mark = font_load_glyph(emphasis->handle, &emphasis->style, emphasis->codepoint, true);
    if (!mark) return nullptr;
    float pen_x, pen_baseline;
    render_text_emphasis_place(emphasis, x / density, baseline_y / density,
        advance / density, mark->advance_x / density, &pen_x, &pen_baseline);
    *mark_x = roundf(pen_x * density + mark->bitmap.bearing_x);
    *mark_y = roundf(pen_baseline * density - mark->bitmap.bearing_y);
    return mark;
}

static bool render_emphasis_capture_glyph(void* pointer, uint32_t codepoint,
    FontHandle* face, float x, float baseline_y, float advance) {
    if (!text_emphasis_marks_codepoint(codepoint)) return true;
    RenderEmphasisCaptureContext* context = (RenderEmphasisCaptureContext*)pointer;
    float density = context->density, mark_x, mark_y;
    LoadedGlyph* mark = render_text_emphasis_bitmap_glyph(context->emphasis, codepoint,
        x * density, baseline_y * density, advance * density, density, &mark_x, &mark_y);
    if (!mark) return false;
    if (!mark->bitmap.width || !mark->bitmap.height) return true;
    RdtMatrix logical = rdt_matrix_scale(1.0f / density, 1.0f / density);
    size_t count = context->paint->size();
    // consume the selected hinted bitmap synchronously; the published image owns its pixels (FT-17 / D4.1.4v5).
    paint_draw_glyph(context->paint, &mark->bitmap,
        (int)mark_x, (int)mark_y, context->color, false, nullptr, &logical, 0); // INT_CAST_OK: rounded physical glyph bitmap positions
    context->has_ink = true;
    return context->paint->size() == count + 1;
}

bool render_paint_text_emphasis(PaintList* paint, FontContext* fonts,
    const PaintGlyphRun* run, const Color* override_color) {
    if (!paint || !run) return false;
    RenderTextEmphasis emphasis = {};
    if (!render_text_emphasis_resolve(fonts, paint_glyph_run_font(run), run->color, &emphasis)) return false;
    if (!emphasis.handle) return true;
    Color color = override_color ? *override_color : emphasis.color;
    if (!color.a) { render_text_emphasis_release(&emphasis); return true; }
    float density = font_handle_get_physical_size_px(emphasis.handle) / font_handle_get_size_px(emphasis.handle);
    if (!isfinite(density) || density <= 0.0f) { render_text_emphasis_release(&emphasis); return false; }
    PaintList coverage;
    RenderEmphasisCaptureContext context = {&coverage, &emphasis, density, color, false};
    bool ok = render_visit_text_run_glyphs(run, fonts, render_emphasis_capture_glyph, &context);
    RenderEffectRasterImage image = {};
    if (ok && context.has_ink) {
        OffscreenRenderArenas arenas;
        ok = arenas.init("render.emphasis.bounds", "render.emphasis.bounds.list", "render.emphasis.bounds.scratch");
        if (ok) {
            DisplayList display = {}; dl_init(&display, arenas.list_arena);
            paint_ir_lower_raster(&coverage, &display);
            Bound bounds = dl_content_bounds(&display);
            ok = bounds.right > bounds.left && bounds.bottom > bounds.top &&
                render_effect_rasterize_paint_list(&coverage, nullptr, &bounds,
                    bounds.right - bounds.left, bounds.bottom - bounds.top,
                    false, density, &image, "[TEXT_EMPHASIS_CAPTURE]") && image.surface;
            dl_destroy(&display); arenas.destroy();
        }
    }
    render_text_emphasis_release(&emphasis);
    if (ok && !context.has_ink) return true;
    size_t checkpoint = paint->size();
    if (ok) {
        // a static accessible label survives deferred lowering after source/font retirement (D4.1.4v5).
        RenderSemanticGroup label = {}; label.title = lam::up("text emphasis marks");
        paint_begin_semantic_group(paint, &label);
        ok = paint->size() == checkpoint + 1;
        if (ok) {
            size_t count = paint->size();
            paint_draw_image_resource(paint, image.surface, image.x, image.y, image.width, image.height,
                255, run->has_transform ? &run->transform : nullptr, image_surface_destroy);
            ok = paint->size() == count + 1;
            if (ok) image.surface = nullptr;
        }
        if (ok) {
            size_t count = paint->size(); paint_end_semantic_group(paint);
            ok = paint->size() == count + 1;
        }
    }
    if (image.surface) image_surface_destroy(image.surface);
    if (!ok) paint->rewind(checkpoint);
    return ok;
}



bool render_paint_text_shadows(PaintList* paint, UiContext* ui,
        const PaintGlyphRun* run, TextShadow* shadows, const RenderTextDecorationSource* decoration) {
    if (!paint || !run || !shadows) return false;
    char decoration_names[48];
    if (!decoration || !run->font || !run->font->style ||
        !font_text_decoration_names(run->font->style, decoration_names, sizeof(decoration_names)) ||
        strcmp(decoration_names, "none") == 0) decoration = nullptr;
    size_t checkpoint = paint->size();
    bool success = true;
    render_visit_text_shadows_reverse(shadows, [&](TextShadow* shadow) {
        if (!shadow->color.a || !success) return;
        PaintGlyphRun colored = *run;
        colored.owned_text = nullptr; colored.color = shadow->color;
        colored.x += shadow->offset_x; colored.baseline_y += shadow->offset_y;
        bool bitmap_glyphs = shadow->blur_radius > 0.0f;
        const FontBox* source_font = paint_glyph_run_font(run);
        bool has_emphasis = source_font && source_font->style && source_font->style->text_emphasis_enabled;
        bool capture = bitmap_glyphs || ((decoration || has_emphasis) && shadow->color.a != 255);
        if (capture) colored.color.a = 255;
        RdtPath* path = bitmap_glyphs ? nullptr :
            render_path_create_paint_glyph_run(&colored, ui ? ui->font_ctx.get() : nullptr);
        if (!bitmap_glyphs && !path) { success = false; return; }
        auto record_layer = [&]() {
        auto geometry = [&](PaintList* list) {
            auto lines = [&](bool before) {
                return !decoration || render_paint_text_decorations(list, run->font,
                    decoration->text, decoration->rect,
                    decoration->origin_x + shadow->offset_x,
                    decoration->origin_y + shadow->offset_y,
                    colored.color, before, &colored.color);
            };
            if (!lines(true)) return false;
            if (bitmap_glyphs) {
                size_t count = list->size();
                paint_glyph_run(list, &colored);
                if (list->size() != count + 1) return false;
            } else if (!render_paint_solid_shape(list, path, colored.color, nullptr)) return false;
            if (!render_paint_text_emphasis(list, ui ? ui->font_ctx.get() : nullptr,
                    &colored, &colored.color)) return false;
            return lines(false);
        };
        if (!capture) {
            size_t checkpoint = paint->size();
            if (!geometry(paint)) { paint->rewind(checkpoint); success = false; return; }
            return;
        }
        PaintList silhouette;
        if (!geometry(&silhouette)) { success = false; return; }
        OffscreenRenderArenas bounds_arenas;
        if (!bounds_arenas.init("render.text.shadow.bounds", "render.text.shadow.bounds.list",
                "render.text.shadow.bounds.scratch")) { success = false; return; }
        DisplayList shapes = {}; dl_init(&shapes, bounds_arenas.list_arena);
        paint_ir_register_glyph_run_raster_lowerer(render_glyph_run_raster_lower);
        paint_ir_lower_raster(&silhouette, &shapes);
        // decoration extents and hinted bitmap extents both contribute to the isolated capture.
        Bound ink = dl_content_bounds(&shapes);
        dl_destroy(&shapes); bounds_arenas.destroy();
        if (!(ink.right > ink.left && ink.bottom > ink.top)) return;
        float sigma = shadow->blur_radius * 0.5f;
        float density = ui_context_raster_scale(ui);
        float pad = render_filter_blur_support(sigma * density) / density;
        Bound bounds = {ink.left - pad, ink.top - pad, ink.right + pad, ink.bottom + pad};
        FilterFunction blur = {}; blur.type = FILTER_BLUR; blur.params.blur_radius = sigma;
        FilterProp filter = {}; filter.functions = lam::own(&blur); filter.functions_borrowed = true;
        PaintEffectGroup group = {}; group.bounds = bounds;
        // merge the glyph/decoration silhouette before applying layer alpha once.
        group.opacity = shadow->color.a / 255.0f;
        if (sigma > 0) group.filter = lam::up(&filter);
        if (!bitmap_glyphs) {
            // sharp shadows retain vector outlines and isolate the combined silhouette's alpha.
            size_t checkpoint = paint->size();
            paint_begin_effect_group(paint, &group);
            if (paint->size() != checkpoint + 1) { success = false; return; }
            if (!geometry(paint)) { paint->rewind(checkpoint); success = false; return; }
            size_t count = paint->size();
            paint_end_effect_group(paint);
            if (paint->size() != count + 1) success = false;
            return;
        }
        PaintList shadow_paint;
        paint_begin_effect_group(&shadow_paint, &group);
        if (shadow_paint.size() != 1) { success = false; return; }
        if (!geometry(&shadow_paint)) { success = false; return; }
        size_t count = shadow_paint.size();
        paint_end_effect_group(&shadow_paint);
        if (shadow_paint.size() != count + 1) { success = false; return; }
        RenderEffectRasterImage image = {};
        // temporary descriptors and source/font borrows expire after this synchronous capture.
        if (!render_effect_rasterize_paint_list(&shadow_paint, nullptr, &bounds,
                bounds.right - bounds.left, bounds.bottom - bounds.top, false,
                density, &image, "[TEXT_SHADOW_CAPTURE]") || !image.surface) { success = false; return; }
        int before = paint_list_count(paint);
        paint_draw_image_resource(paint, image.surface, image.x, image.y, image.width, image.height,
            255, nullptr, image_surface_destroy);
        if (paint_list_count(paint) != before + 1) { image_surface_destroy(image.surface); success = false; }
        };
        record_layer();
        // recording clones the selected outline; every capture/error path releases its local source.
        rdt_path_free(path);
    });
    // a later layer failure must release earlier owned paths/images from this compound shadow.
    if (!success) paint->rewind(checkpoint);
    return success;
}
