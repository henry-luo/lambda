#include "render.hpp"
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
                entry->start_index = rect->start_index;
                entry->length = rect->length;
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
    const unsigned char* start = entry->text->text_data() + entry->rect->start_index;
    const unsigned char* end = start + entry->rect->length;
    const unsigned char* cursor = start;
    const unsigned char* retained_start = start;
    float discarded_width = 0.0f;
    bool suffix_started = false;
    bool preserve_spaces = white_space_preserves_space_advance(get_white_space_value(entry->text));
    bool has_space = render_text_source_has_space(
        entry->text->text_data(), entry->rect->start_index, preserve_spaces);
    float width = 0.0f;
    float scale = ui_context_raster_scale(context->ui_context);
    FontStyleDesc descriptor = font_style_desc_from_prop(entry->font.style);
    CssEnum transform = render_text_inherited_transform(entry->text);
    bool word_start = render_text_source_word_start(
        entry->text->text_data(), entry->rect->start_index);
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
    entry->start_index = entry->rect->start_index +
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
        RenderTextPaintData* data = context->text_paint;
        if (!data) return source;
        int low = 0, high = data->entries->length;
        while (low < high) {
            int middle = low + (high - low) / 2;
            RenderTextPaintEntry* entry = (RenderTextPaintEntry*)data->entries->data[middle];
            if ((uintptr_t)entry->rect.get() < (uintptr_t)source) low = middle + 1;
            else high = middle;
        }
        if (low == data->entries->length) return source;
        RenderTextPaintEntry* entry = (RenderTextPaintEntry*)data->entries->data[low];
        if (entry->rect != source || !entry->projected) return source;
        bool combined_marker = entry->marker && entry->length &&
            !data->rtl &&
            saved_font.style == data->font.style && saved_color.c == data->color.c;
        if (entry->marker && !combined_marker) pending_marker = lam::up(entry);
        if (!entry->length) {
            if (pending_marker) return next();
            continue;
        }
        // copy only scalar paint geometry; ownership of the layout chain stays with the view.
        projection = {};
        projection.x = entry->x;
        projection.y = source->y;
        projection.width = entry->width;
        projection.height = source->height;
        projection.start_index = entry->start_index;
        projection.length = entry->length;
        projection.line_number = source->line_number;
        if (entry->length == source->length) {
            projection.has_trailing_hyphen = source->has_trailing_hyphen;
            projection.trailing_hyphenate_character = source->trailing_hyphenate_character;
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
static bool render_text_paint_blurred_shadows(RasterRenderContext* rdcon, unsigned char* str,
                                              TextRect* text_rect, TextShadow* text_shadow,
                                              CssEnum text_transform, bool preserve_spaces,
                                              float space_width, float scaled_space_width,
                                              float cjk_spacing,
                                              float x, float y);
static LoadedGlyph* render_text_load_glyph_for_paint(RasterRenderContext* rdcon,
                                                     uint32_t codepoint,
                                                     unsigned char* cursor,
                                                     unsigned char* end);
static void render_text_paint_glyph_shadows(RasterRenderContext* rdcon, LoadedGlyph* glyph,
                                            TextShadow* text_shadow, float x, float y,
                                            float ascend);
static float render_text_trailing_marks(RasterRenderContext* rdcon, TextRect* text_rect,
                                        float x, float y);
static void render_text_decorations(RasterRenderContext* rdcon, unsigned char* str,
                                    TextRect* text_rect);
static bool render_text_background_clip_linear_text(RasterRenderContext* rdcon,
                                                    DomElement* parent_elem,
                                                    TextRect* text_rect,
                                                    float text_x, float text_y,
                                                    LinearGradient** out_gradient,
                                                    Rect* out_gradient_rect);
static Color render_text_sample_linear_gradient(LinearGradient* gradient, Rect rect,
                                                float px, float py, float scale);

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
    // Transforms can expand each source byte to one four-byte codepoint; reserve
    // the computed hyphenation string separately because it is not source text.
    size_t content_size = (size_t)text_rect->length * 4 + trailing_length + 4;
    char* content = (char*)mem_alloc(content_size, MEM_CAT_RENDER);
    unsigned char* src = (unsigned char*)text + text_rect->start_index;
    unsigned char* src_end = src + text_rect->length;
    char* dst = content;

    bool preserve_spaces = white_space_preserves_space_advance(white_space);
    bool has_space = render_text_source_has_space(text, text_rect->start_index, preserve_spaces);
    bool is_word_start = render_text_source_word_start(text, text_rect->start_index);
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

static bool render_text_rect_misses_clip(RasterRenderContext* rdcon, float x, float y,
                                         float width, float height) {
    if (!rdcon || rdcon->has_transform) return false;
    return x + width < rdcon->block.clip.left ||
           x > rdcon->block.clip.right ||
           y + height < rdcon->block.clip.top ||
           y > rdcon->block.clip.bottom;
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

    FontHandle* emphasis_handle = nullptr;
    FontStyleDesc emphasis_style = {};
    if (rdcon->font.style && rdcon->font.style->text_emphasis_enabled &&
        rdcon->ui_context && rdcon->ui_context->font_ctx) {
        emphasis_style = font_style_desc_from_prop(rdcon->font.style);
        emphasis_style.size_px *= 0.5f;
        if (!emphasis_style.family)
            emphasis_style.family = font_handle_get_family_name(font_box_handle(&rdcon->font));
        emphasis_handle = font_resolve(rdcon->ui_context->font_ctx, &emphasis_style);
    }

    // Get the white-space property for this text node
    CssEnum white_space = get_white_space_value(text_view);
    bool preserve_spaces = white_space_preserves_space_advance(white_space);
    CssEnum text_justify = text_justify_computed_value(text_view->parent);

    // Get text-transform from parent elements
    CssEnum text_transform = get_text_transform_from_node(text_view->parent);
    CssEnum text_align = CSS_VALUE_LEFT;  // default to left alignment
    bool text_align_found = false;
    DomNode* parent = text_view->parent;
    while (parent) {
        if (parent->is_element()) {
            DomElement* elem = lam::dom_require_element(parent);
            // Get text-align from the nearest ancestor that has block properties.
            // text-align is CSS-inherited, so the closest block already holds the
            // resolved value; walking further would overwrite it with an outer
            // (e.g. <html>) default and lose 'justify'.
            if (!text_align_found && elem->blk) {
                BlockProp* blk_prop = (BlockProp*)elem->blk;
                text_align = blk_prop->text_align;
                text_align_found = true;
            }
        }
        parent = parent->parent;
    }

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

        if (render_text_rect_misses_clip(rdcon, x, y,
                text_rect->width * s, text_rect->height * s)) {
            continue;
        }

        render_text_inline_background(rdcon, text_view, text_rect, parent_elem, x, y);
        render_text_selection_rect(rdcon, text_view, text_rect);

        unsigned char* p = str + text_rect->start_index;  unsigned char* end = p + text_rect->length;
        if (render_text_trace_enabled()) {
        }

        // Calculate natural text width and space count for justify rendering
        // Note: space_width is in CSS pixels (scaled down for layout), need to scale up for render
        float scaled_space_width = rdcon->font.style->space_width * s;
        float computed_space_width = text_space_advance(rdcon->font.style) * s;
        float natural_width = 0.0f;
        int space_count = 0;
        unsigned char* scan = p;
        bool scan_has_space = render_text_source_has_space(str, text_rect->start_index, preserve_spaces);

        // Scan all content including trailing spaces for width calculation
        // Trailing whitespace is intentionally included because layout has already
        // determined the correct width and positioning - we should render exactly
        // what was laid out, including spaces between inline elements
        unsigned char* content_end = end;

        while (scan < content_end) {
            if (is_space(*scan)) {
                if (preserve_spaces || !scan_has_space) {
                    scan_has_space = true;
                    natural_width += computed_space_width;
                    space_count++;
                }
                scan++;
            }
            else {
                scan_has_space = false;
                uint32_t scan_codepoint;
                int bytes = str_utf8_decode((const char*)scan, (size_t)(content_end - scan), &scan_codepoint);
                if (bytes <= 0) { scan++; }
                else { scan += bytes; }

                uint64_t t1 = time_now_ns();
                FontStyleDesc _sd = font_style_desc_from_prop(rdcon->font.style);
                LoadedGlyph* glyph = font_load_glyph(font_box_handle(&rdcon->font), &_sd, scan_codepoint, false);
                uint64_t t2 = time_now_ns();
                render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_GLYPH_LOAD,
                    time_elapsed_ms_f(t1, t2));
                if (glyph) {
                    natural_width += glyph->advance_x + text_character_spacing(rdcon->font.style, scan_codepoint) * s;  // already in physical pixels
                } else {
                    natural_width += scaled_space_width;  // fallback width in physical pixels
                }
            }
        }

        // Calculate adjusted space width for justified text (in physical pixels)
        float space_width = computed_space_width;
        float justification_per_gap = 0.0f;
        int justification_gaps = count_rendered_justify_opportunities(
            text_view, text_rect, false);
        // Layout may widen a sole run even for `none`; only the paint spacing
        // must stay at its natural advance in that case.
        if (text_align == CSS_VALUE_JUSTIFY && text_justify != CSS_VALUE_NONE &&
            justification_gaps > 0 && natural_width > 0 &&
            text_rect->width * s > natural_width) {
            float extra_space = (text_rect->width * s) - natural_width;
            justification_per_gap = extra_space / justification_gaps;
            if (space_count > 0) space_width += justification_per_gap;
            if (render_text_trace_enabled()) {
            }
        }

        // Render the text with adjusted spacing
        bool has_space = render_text_source_has_space(str, text_rect->start_index, preserve_spaces);
        uint32_t codepoint;
        bool is_word_start = render_text_source_word_start(str, text_rect->start_index);
        int char_index = text_rect->start_index;  // Track character offset for selection
        uint32_t previous_codepoint = 0;

        bool shadow_needs_blur = render_text_paint_blurred_shadows(rdcon, str, text_rect,
            text_shadow, text_transform, preserve_spaces, space_width,
            scaled_space_width, text_justify == CSS_VALUE_INTER_WORD
                ? 0.0f : justification_per_gap, x, y);

        while (p < end) {
            // Check if current character is in selection range
            bool is_selected = has_selection && char_index >= sel_start && char_index < sel_end;

            // log_debug("draw character '%c'", *p);
            if (is_space(*p)) {
                if (preserve_spaces || !has_space) {  // preserve all spaces or add single whitespace
                    has_space = true;

                    // Render space by advancing x position
                    // All spaces are rendered (not just non-trailing) because layout has
                    // already determined correct positioning including inter-element whitespace
                    x += space_width;  // Use adjusted space width for justified text
                }
                // else  // skip consecutive spaces
                is_word_start = true;  // Next non-space is word start
                previous_codepoint = 0;
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
                if (codepoint == 0x00AD || text_codepoint_has_zero_advance(codepoint)) continue;

                // Apply text-transform before loading glyph
                uint32_t tt_out[3];
                int tt_count = apply_text_transform_full(codepoint, text_transform, is_word_start, tt_out);
                codepoint = tt_out[0];
                is_word_start = false;

                for (int tti = 0; tti < tt_count; tti++) {
                    uint32_t render_cp = render_text_resolve_mathlive_fallback_glyph(tt_out[tti]);
                    if (render_cp == 0) continue;
                    if (text_codepoint_has_zero_advance(render_cp)) continue;
                    if (justification_per_gap > 0.0f &&
                        text_justify != CSS_VALUE_INTER_WORD &&
                        text_justify_cjk_gap(previous_codepoint, render_cp)) {
                        x += justification_per_gap;
                    }
                    previous_codepoint = render_cp;

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
                            ascend = font_get_rendering_ascender(font_box_handle(&rdcon->font)) * rdcon->raster_scale;
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

                        // Render text-shadow glyphs BEFORE the main glyph
                        // Skip if blur pre-pass already rendered shadows
                        if (text_shadow && !shadow_needs_blur) {
                            render_text_paint_glyph_shadows(rdcon, glyph, text_shadow, x, y, ascend);
                        }

                        LinearGradient* text_gradient = nullptr;
                        Rect text_gradient_rect = {};
                        bool has_text_gradient = render_text_background_clip_linear_text(
                            rdcon, parent_elem, text_rect,
                            rdcon->block.x + text_rect->x * s, y,
                            &text_gradient, &text_gradient_rect);
                        Color glyph_saved_color = rdcon->color;
                        if (is_selected && has_selection_foreground) {
                            rdcon->color = parent_elem->inl()->selection_color;
                        } else if (has_text_gradient) {
                            float gx_center = x + glyph->bitmap.bearing_x + glyph->bitmap.width * 0.5f;
                            float gy_center = y + ascend - glyph->bitmap.bearing_y + glyph->bitmap.height * 0.5f;
                            rdcon->color = render_text_sample_linear_gradient(
                                text_gradient, text_gradient_rect, gx_center, gy_center, s);
                        }
                        draw_glyph(rdcon, &glyph->bitmap, lroundf(x + glyph->bitmap.bearing_x), lroundf(y + ascend - glyph->bitmap.bearing_y));
                        rdcon->color = glyph_saved_color;
                        float glyph_advance = glyph->advance_x;
                        if (emphasis_handle && text_emphasis_marks_codepoint(render_cp)) {
                            LoadedGlyph* mark = font_load_glyph(emphasis_handle,
                                &emphasis_style, rdcon->font.style->text_emphasis_mark, true);
                            if (mark) {
                                Color text_color = rdcon->color;
                                if (!rdcon->font.style->text_emphasis_color_current)
                                    rdcon->color = rdcon->font.style->text_emphasis_color;
                                float mark_baseline = y +
                                    (rdcon->font.style->text_emphasis_under
                                        ? rdcon->font.style->font_size * 1.35f * s : 0.0f);
                                draw_glyph(rdcon, &mark->bitmap,
                                    lroundf(x + (glyph_advance - mark->advance_x) * 0.5f +
                                            mark->bitmap.bearing_x),
                                    lroundf(mark_baseline - mark->bitmap.bearing_y));
                                rdcon->color = text_color;
                            }
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
        render_text_decorations(rdcon, str, text_rect);
    }
    }

    // Restore color and font (in case they were changed for PDF text)
    rdcon->font = saved_font;
    rdcon->color = saved_color;
    if (emphasis_handle) font_handle_release(emphasis_handle);
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

typedef struct SkipInkGap {
    float x0;
    float x1;
} SkipInkGap;

static bool render_text_paint_blurred_shadows(RasterRenderContext* rdcon, unsigned char* str,
                                              TextRect* text_rect, TextShadow* text_shadow,
                                              CssEnum text_transform, bool preserve_spaces,
                                              float space_width, float scaled_space_width,
                                              float cjk_spacing,
                                              float x, float y) {
    if (!rdcon || !str || !text_rect || !text_shadow ||
        !font_box_handle(&rdcon->font) || !rdcon->font.style) {
        return false;
    }

    float s = rdcon->raster_scale;
    float max_shadow_blur = 0;
    TextShadow* ts = text_shadow;
    while (ts) {
        if (ts->blur_radius > 0) {
            if (ts->blur_radius > max_shadow_blur) {
                max_shadow_blur = ts->blur_radius;
            }
        }
        ts = ts->next;
    }
    if (max_shadow_blur <= 0) {
        return false;
    }

    unsigned char* sp = str + text_rect->start_index;
    unsigned char* s_end = sp + text_rect->length;
    float sx_pos = x;
    bool s_has_space = render_text_source_has_space(str, text_rect->start_index, preserve_spaces);
    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);
    bool s_word_start = render_text_source_word_start(str, text_rect->start_index);
    uint32_t previous_codepoint = 0;

    while (sp < s_end) {
        if (is_space(*sp)) {
            if (preserve_spaces || !s_has_space) {
                s_has_space = true;
                sx_pos += space_width;
            }
            s_word_start = true;
            previous_codepoint = 0;
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
            if (text_codepoint_has_zero_advance(s_tt_cp)) continue;
            if (cjk_spacing > 0.0f &&
                text_justify_cjk_gap(previous_codepoint, s_tt_cp))
                sx_pos += cjk_spacing;
            previous_codepoint = s_tt_cp;

            LoadedGlyph* s_glyph = font_load_glyph(font_box_handle(&rdcon->font), &sd, s_tt_cp, true);
            if (!s_glyph) {
                sx_pos += scaled_space_width;
                continue;
            }

            float s_ascend;
            {
                uint64_t tfm1 = time_now_ns();
                s_ascend = font_get_rendering_ascender(font_box_handle(&rdcon->font)) * rdcon->raster_scale;
                uint64_t tfm2 = time_now_ns();
                render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_FONT_METRICS,
                    time_elapsed_ms_f(tfm1, tfm2));
            }

            Color saved_color = rdcon->color;
            ts = text_shadow;
            while (ts) {
                rdcon->color = ts->color;
                float gsx = sx_pos + s_glyph->bitmap.bearing_x + ts->offset_x * s;
                float gsy = y + s_ascend - s_glyph->bitmap.bearing_y + ts->offset_y * s;
                draw_glyph(rdcon, &s_glyph->bitmap, lroundf(gsx), lroundf(gsy));
                ts = ts->next;
            }
            rdcon->color = saved_color;
            sx_pos += s_glyph->advance_x + text_character_spacing(rdcon->font.style, s_tt_cp) * s;
        }
    }

    if (rdcon->ui_context->surface) {
        float blur_extend = max_shadow_blur * 2;
        float shadow_max_ox = 0;
        float shadow_max_oy = 0;
        ts = text_shadow;
        while (ts) {
            if (fabsf(ts->offset_x) > shadow_max_ox) {
                shadow_max_ox = fabsf(ts->offset_x);
            }
            if (fabsf(ts->offset_y) > shadow_max_oy) {
                shadow_max_oy = fabsf(ts->offset_y);
            }
            ts = ts->next;
        }
        int bx = (int)floorf(x - blur_extend - shadow_max_ox * s);
        int by = (int)floorf(y - blur_extend - shadow_max_oy * s);
        int bw = (int)ceilf(text_rect->width * s + blur_extend * 2 + shadow_max_ox * s * 2);
        int bh = (int)ceilf(text_rect->height * s + blur_extend * 2 + shadow_max_oy * s * 2);
        rc_box_blur_region(rdcon, bx, by, bw, bh, max_shadow_blur, 0, nullptr);
    }

    return true;
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

static void render_text_paint_glyph_shadows(RasterRenderContext* rdcon, LoadedGlyph* glyph,
                                            TextShadow* text_shadow, float x, float y,
                                            float ascend) {
    if (!rdcon || !glyph || !text_shadow) {
        return;
    }

    Color saved_shadow_color = rdcon->color;
    TextShadow* ts = text_shadow;
    while (ts) {
        rdcon->color = ts->color;
        float sx = x + glyph->bitmap.bearing_x + ts->offset_x * rdcon->raster_scale;
        float sy = y + ascend - glyph->bitmap.bearing_y + ts->offset_y * rdcon->raster_scale;
        draw_glyph(rdcon, &glyph->bitmap, lroundf(sx), lroundf(sy));
        ts = ts->next;
    }
    rdcon->color = saved_shadow_color;
}

static bool render_text_background_clip_linear_text(RasterRenderContext* rdcon,
                                                    DomElement* parent_elem,
                                                    TextRect* text_rect,
                                                    float text_x, float text_y,
                                                    LinearGradient** out_gradient,
                                                    Rect* out_gradient_rect) {
    if (out_gradient) *out_gradient = nullptr;
    if (out_gradient_rect) *out_gradient_rect = {};
    if (!rdcon || !parent_elem || !parent_elem->bound ||
        !parent_elem->boundary()->background || !text_rect ||
        !out_gradient || !out_gradient_rect) {
        return false;
    }

    BackgroundProp* bg = parent_elem->boundary()->background;
    if (bg->bg_clip != CSS_VALUE_TEXT ||
        bg->gradient_type != GRADIENT_LINEAR ||
        !bg->linear_gradient ||
        bg->linear_gradient->stop_count < 2) {
        return false;
    }

    float s = rdcon->raster_scale;
    float width = parent_elem->content_width * s;
    float height = parent_elem->content_height * s;
    if (width <= 0.0f) {
        width = text_rect->width * s;
    }
    if (height <= 0.0f) {
        height = text_rect->height * s;
    }
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }

    out_gradient_rect->x = text_x;
    out_gradient_rect->y = text_y;
    out_gradient_rect->width = width;
    out_gradient_rect->height = height;
    *out_gradient = bg->linear_gradient;
    return true;
}

static unsigned char render_text_lerp_channel(unsigned char a, unsigned char b, float t) {
    float v = (float)a + ((float)b - (float)a) * t;
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (unsigned char)lroundf(v);  // INT_CAST_OK: converting interpolated 8-bit color channel
}

static Color render_text_sample_linear_gradient(LinearGradient* gradient, Rect rect,
                                                float px, float py, float scale) {
    Color fallback = {0};
    fallback.a = 255;
    if (!gradient || gradient->stop_count <= 0 || rect.width <= 0.0f || rect.height <= 0.0f) {
        return fallback;
    }
    if (gradient->stop_count == 1) {
        return gradient->stops[0].color;
    }

    RadiantGradientLine line = radiant_linear_gradient_line(
        rect, radiant_linear_gradient_used_angle(gradient, rect));
    float vx = line.x2 - line.x1;
    float vy = line.y2 - line.y1;
    float len2 = vx * vx + vy * vy;
    lam::Temp<RdtGradientStop> stops = lam::temp_array<RdtGradientStop>(gradient->stop_count, MEM_CAT_RENDER);
    if (!stops || !render_copy_gradient_stops(gradient->stops, gradient->stop_count,
            sqrtf(len2) / scale, stops.get(), gradient->stop_count, nullptr)) return fallback;
    float pos = len2 > 1e-7f
        ? ((px - line.x1) * vx + (py - line.y1) * vy) / len2
        : 0.0f;
    pos = fminf(1.0f, fmaxf(0.0f, pos));

    int right = 0;
    while (right < gradient->stop_count &&
           stops[right].offset < pos) {
        right++;
    }
    if (right <= 0) {
        return gradient->stops[0].color;
    }
    if (right >= gradient->stop_count) {
        return gradient->stops[gradient->stop_count - 1].color;
    }

    float left_pos = stops[right - 1].offset;
    float right_pos = stops[right].offset;
    float t = right_pos > left_pos ? (pos - left_pos) / (right_pos - left_pos) : 0.0f;
    Color left = gradient->stops[right - 1].color;
    Color right_color = gradient->stops[right].color;
    Color out = {0};
    out.r = render_text_lerp_channel(left.r, right_color.r, t);
    out.g = render_text_lerp_channel(left.g, right_color.g, t);
    out.b = render_text_lerp_channel(left.b, right_color.b, t);
    out.a = render_text_lerp_channel(left.a, right_color.a, t);
    return out;
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

static int collect_skip_ink_gaps(RasterRenderContext* rdcon, unsigned char* str,
                                 TextRect* text_rect, float deco_y_top, float deco_y_bot,
                                 SkipInkGap* gaps, int max_gaps) {
    float s = rdcon->raster_scale;
    float x = rdcon->block.x + text_rect->x * s;
    float y_base = rdcon->block.y + text_rect->y * s;
    float ascend = font_get_rendering_ascender(font_box_handle(&rdcon->font)) * s;
    int gap_count = 0;
    float pad = fmaxf(2.0f, (deco_y_bot - deco_y_top));

    const unsigned char* p = str + text_rect->start_index;
    const unsigned char* end = p + text_rect->length;
    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);

    while (p < end && gap_count < max_gaps) {
        if (is_space(*p)) {
            x += rdcon->font.style->space_width * s;
            p++;
            continue;
        }

        uint32_t codepoint;
        if (!layout_utf8_next_codepoint(&p, end, &codepoint)) continue;
        if (codepoint == 0x00AD || text_codepoint_has_zero_advance(codepoint)) continue;

        LoadedGlyph* glyph = font_load_glyph(font_box_handle(&rdcon->font), &sd, codepoint, true);
        if (!glyph) {
            x += rdcon->font.style->space_width * s;
            continue;
        }

        float glyph_top = y_base + ascend - glyph->bitmap.bearing_y;
        float glyph_bot = glyph_top + glyph->bitmap.height;
        float glyph_left = x + glyph->bitmap.bearing_x;
        float glyph_right = glyph_left + glyph->bitmap.width;

        float overlap = fminf(glyph_bot, deco_y_bot) - fmaxf(glyph_top, deco_y_top);
        float deco_height = deco_y_bot - deco_y_top;
        float min_overlap = fmaxf(deco_height * 0.5f, 2.0f);
        if (overlap >= min_overlap && glyph->bitmap.width > 0) {
            int bm_row_start = (int)fmaxf(deco_y_top - glyph_top, 0.0f);
            int bm_row_end = (int)fminf(deco_y_bot - glyph_top, (float)glyph->bitmap.height);
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
                gaps[gap_count].x0 = glyph_left + ink_col_min - pad;
                gaps[gap_count].x1 = glyph_left + ink_col_max + 1.0f + pad;
            } else {
                gaps[gap_count].x0 = glyph_left - pad;
                gaps[gap_count].x1 = glyph_right + pad;
            }
            gap_count++;
        }

        x += glyph->advance_x + rdcon->font.style->letter_spacing * s;
    }
    return gap_count;
}

static void draw_deco_with_gaps(RasterRenderContext* rdcon, Rect rect, uint32_t color,
                                SkipInkGap* gaps, int gap_count) {
    float x_start = rect.x;
    float x_end = rect.x + rect.width;
    for (int i = 0; i < gap_count; i++) {
        if (gaps[i].x0 > x_start) {
            float seg_end = fminf(gaps[i].x0, x_end);
            if (seg_end > x_start) {
                Rect seg = {x_start, rect.y, seg_end - x_start, rect.height};
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &seg, color,
                                     &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
        }
        x_start = fmaxf(x_start, gaps[i].x1);
        if (x_start >= x_end) break;
    }
    if (x_start < x_end) {
        Rect seg = {x_start, rect.y, x_end - x_start, rect.height};
        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &seg, color,
                             &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
    }
}

static void render_single_text_decoration(RasterRenderContext* rdcon, unsigned char* str,
                                          TextRect* text_rect, CssEnum line) {
    if (!rdcon || !rdcon->font.style || !text_rect) {
        return;
    }
    if (line == CSS_VALUE_NONE || line == CSS_VALUE__UNDEF ||
        line == CSS_VALUE_BLINK) {
        return;
    }

    float s = rdcon->raster_scale;
    const FontMetrics* deco_m = font_get_metrics(font_box_handle(&rdcon->font));
    float thickness = rdcon->font.style->text_deco_thickness > 0
        ? rdcon->font.style->text_deco_thickness
        : fmaxf(deco_m ? deco_m->underline_thickness : 1.0f, 1.0f);
    // fractional font underline thickness covers the next device pixel; rounding
    // down makes auto decorations thinner than the browser's rasterized stroke.
    thickness = fmaxf(ceilf(thickness), 1.0f);

    Color deco_color = rdcon->font.style->text_deco_color.a > 0
        ? rdcon->font.style->text_deco_color
        : rdcon->color;
    CssEnum deco_style = rdcon->font.style->text_deco_style;
    if (deco_style == CSS_VALUE__UNDEF || deco_style == 0) deco_style = CSS_VALUE_SOLID;

    Rect rect = {0, 0, 0, 0};
    bool draw_deco = true;
    float deco_ascend = font_get_rendering_ascender(font_box_handle(&rdcon->font)) * s;
    if (line == CSS_VALUE_UNDERLINE) {
        float underline_pos = deco_m ? deco_m->underline_position : 0;
        float offset = rdcon->font.style->text_underline_offset;
        if (rdcon->font.style->text_underline_offset_mode == 2) {
            offset *= rdcon->font.style->font_size / 100.0f;
        }
        rect.x = rdcon->block.x + text_rect->x * s;
        float baseline_y = rdcon->block.y + text_rect->y * s + deco_ascend;
        float underline_depth = -underline_pos;
        if (rdcon->font.style->text_underline_position == CSS_VALUE_UNDER && deco_m) {
            // under places the stroke below the font descent; auto/from-font use its underline metric.
            underline_depth = fmaxf(underline_depth, -deco_m->descender);
        }
        rect.y = roundf(baseline_y + (underline_depth + offset) * s);
    }
    else if (line == CSS_VALUE_OVERLINE) {
        rect.x = rdcon->block.x + text_rect->x * s;
        // overline sits on the text-over edge; starting inside the fragment pushed it below browser output.
        rect.y = floorf(rdcon->block.y + text_rect->y * s - thickness);
    }
    else if (line == CSS_VALUE_LINE_THROUGH) {
        rect.x = rdcon->block.x + text_rect->x * s;
        float strike_y;
        if (deco_m && deco_m->strikeout_position > 0) {
            // browser line-through follows the font OS/2 strikeout metric; x-height fallback was too low.
            strike_y = deco_m->strikeout_position;
        } else if (deco_m && deco_m->x_height > 0) {
            strike_y = deco_m->x_height * 0.5f;
        } else {
            strike_y = deco_ascend * 0.3f / s;
        }
        if (rdcon->font.style->text_deco_thickness <= 0 &&
            deco_m && deco_m->strikeout_size > 0)
            thickness = fmaxf(ceilf(deco_m->strikeout_size), 1.0f);
        // browser treats the strikeout metric as the stroke position; using it as the top edge paints too low.
        rect.y = floorf(rdcon->block.y + text_rect->y * s + deco_ascend - strike_y * s - thickness);
    }
    else {
        draw_deco = false;
    }
    if (!draw_deco) {
        return;
    }

    rect.width = text_rect->width * s;
    rect.height = thickness;

    if (deco_style == CSS_VALUE_DASHED || deco_style == CSS_VALUE_DOTTED ||
        deco_style == CSS_VALUE_DOUBLE || deco_style == CSS_VALUE_WAVY) {
        RdtPath* path = render_path_create_decoration(&rect, deco_style);
        if (path) {
            if (deco_style == CSS_VALUE_WAVY) rc_stroke_path(rdcon, path, deco_color,
                fmaxf(1.0f, thickness * 0.5f), RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, NULL);
            else rc_fill_path(rdcon, path, deco_color, RDT_FILL_WINDING, NULL);
            rdt_path_free(path);
        }
    } else {
        bool apply_skip_ink = rdcon->font.style->text_deco_skip_ink == CSS_VALUE_ALL
            ? line == CSS_VALUE_UNDERLINE || line == CSS_VALUE_OVERLINE
            : rdcon->font.style->text_deco_skip_ink != CSS_VALUE_NONE &&
              line == CSS_VALUE_UNDERLINE;
        if (apply_skip_ink) {
            SkipInkGap gaps[64];
            int gap_count = collect_skip_ink_gaps(rdcon, str, text_rect,
                                                  rect.y, rect.y + thickness, gaps, 64);
            draw_deco_with_gaps(rdcon, rect, deco_color.c, gaps, gap_count);
        } else {
            rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, deco_color.c,
                &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
        }
    }
}

static void render_text_decorations(RasterRenderContext* rdcon, unsigned char* str,
                                    TextRect* text_rect) {
    if (!rdcon || !rdcon->font.style) return;
    const FontProp* font = rdcon->font.style;
    render_single_text_decoration(rdcon, str, text_rect, font->text_deco);
    if (font->text_deco_extra & CSS_TEXT_DECO_UNDERLINE) {
        render_single_text_decoration(rdcon, str, text_rect, CSS_VALUE_UNDERLINE);
    }
    if (font->text_deco_extra & CSS_TEXT_DECO_OVERLINE) {
        render_single_text_decoration(rdcon, str, text_rect, CSS_VALUE_OVERLINE);
    }
    if (font->text_deco_extra & CSS_TEXT_DECO_LINE_THROUGH) {
        render_single_text_decoration(rdcon, str, text_rect, CSS_VALUE_LINE_THROUGH);
    }
}
