#include "render.hpp"
#include "render_img.hpp"
#include "render_border.hpp"
#include "render_background.hpp"
#include "render_filter.hpp"
#include "render_math.hpp"
#include "transform.hpp"
#include "layout.hpp"
#include "form_control.hpp"
#include "state_store.hpp"
#include "../lib/log.h"
#include "../lib/avl_tree.h"
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include <string.h>
#include <math.h>
#include <chrono>
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "lib/stb_image_write.h"

#define DEBUG_RENDER 0

// Rendering performance counters
static int64_t g_render_glyph_count = 0;
static int64_t g_render_draw_count = 0;
static double g_render_load_glyph_time = 0;
static double g_render_draw_glyph_time = 0;
static int64_t g_render_setup_font_count = 0;
static double g_render_setup_font_time = 0;

void reset_render_stats() {
    g_render_glyph_count = 0;
    g_render_draw_count = 0;
    g_render_load_glyph_time = 0;
    g_render_draw_glyph_time = 0;
    g_render_setup_font_count = 0;
    g_render_setup_font_time = 0;
}

void log_render_stats() {
    log_info("[TIMING] render stats: load_glyph calls=%lld (%.1fms), draw_glyph calls=%lld (%.1fms), setup_font calls=%lld (%.1fms)",
        g_render_glyph_count, g_render_load_glyph_time,
        g_render_draw_count, g_render_draw_glyph_time,
        g_render_setup_font_count, g_render_setup_font_time);
}

// Forward declaration for border-collapse support
struct CollapsedBorder {
    float width;
    CssEnum style;
    Color color;
    uint8_t priority;
};

// ============================================================================
// CSS white-space Property Helpers for Rendering
// ============================================================================

/**
 * Check if whitespace should be preserved according to white-space property.
 * Returns true for: pre, pre-wrap, break-spaces
 * Returns false for: normal, nowrap, pre-line
 */
static inline bool ws_preserve_spaces(CssEnum ws) {
    return ws == CSS_VALUE_PRE || ws == CSS_VALUE_PRE_WRAP || ws == CSS_VALUE_BREAK_SPACES;
}

// Forward declarations for functions from other modules
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
// load_html_doc is declared in view.hpp (via layout.hpp)

void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_inline_view(RenderContext* rdcon, ViewSpan* view_span);
void render_children(RenderContext* rdcon, View* view);
void render_image_content(RenderContext* rdcon, ViewBlock* view);
void scrollpane_render(Tvg_Canvas* canvas, ScrollPane* sp, Rect* block_bound,
    float content_width, float content_height, Bound* clip);
void render_form_control(RenderContext* rdcon, ViewBlock* block);  // form controls
void render_column_rules(RenderContext* rdcon, ViewBlock* block);  // multi-column rules
// render_math_view is declared in render_math.hpp

/**
 * Helper function to apply transform and push paint to canvas
 * If a transform is active in rdcon, applies it to the paint before pushing
 */
static void push_with_transform(RenderContext* rdcon, Tvg_Paint* paint) {
    if (rdcon->has_transform) {
        tvg_paint_set_transform(paint, &rdcon->transform);
    }
    tvg_canvas_push(rdcon->canvas, paint);
}

// draw a color glyph bitmap (BGRA format, used for color emoji) into the doc surface
void draw_color_glyph(RenderContext* rdcon, FT_Bitmap *bitmap, int x, int y) {
    int left = max(rdcon->block.clip.left, x);
    int right = min(rdcon->block.clip.right, x + (int)bitmap->width);
    int top = max(rdcon->block.clip.top, y);
    int bottom = min(rdcon->block.clip.bottom, y + (int)bitmap->rows);
    if (left >= right || top >= bottom) return; // glyph outside the surface
    ImageSurface* surface = rdcon->ui_context->surface;
    for (int i = top - y; i < bottom - y; i++) {
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + i) * surface->pitch;
        uint8_t* src_row = bitmap->buffer + i * bitmap->pitch;
        for (int j = left - x; j < right - x; j++) {
            if (x + j < 0 || x + j >= surface->width) continue;
            // BGRA format: Blue, Green, Red, Alpha (4 bytes per pixel)
            uint8_t* src = src_row + j * 4;
            uint8_t src_b = src[0], src_g = src[1], src_r = src[2], src_a = src[3];
            if (src_a > 0) {
                uint8_t* dst = (uint8_t*)(row_pixels + (x + j) * 4);
                if (src_a == 255) {
                    // fully opaque - just copy
                    dst[0] = src_r;  // our surface is RGBA
                    dst[1] = src_g;
                    dst[2] = src_b;
                    dst[3] = 255;
                } else {
                    // alpha blend
                    uint32_t inv_alpha = 255 - src_a;
                    dst[0] = (dst[0] * inv_alpha + src_r * src_a) / 255;
                    dst[1] = (dst[1] * inv_alpha + src_g * src_a) / 255;
                    dst[2] = (dst[2] * inv_alpha + src_b * src_a) / 255;
                    dst[3] = 255;
                }
            }
        }
    }
}

// draw a glyph bitmap into the doc surface
void draw_glyph(RenderContext* rdcon, FT_Bitmap *bitmap, int x, int y) {
    // handle color emoji bitmaps (BGRA format)
    if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA) {
        draw_color_glyph(rdcon, bitmap, x, y);
        return;
    }
    int left = max(rdcon->block.clip.left, x);
    int right = min(rdcon->block.clip.right, x + (int)bitmap->width);
    int top = max(rdcon->block.clip.top, y);
    int bottom = min(rdcon->block.clip.bottom, y + (int)bitmap->rows);
    if (left >= right || top >= bottom) {
        log_debug("glyph clipped: x=%d, y=%d, bitmap=%dx%d, clip=[%.0f,%.0f,%.0f,%.0f]",
            x, y, bitmap->width, bitmap->rows,
            rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
        return; // glyph outside the surface
    }
    log_debug("[GLYPH RENDER] drawing glyph at x=%d y=%d size=%dx%d color=#%02x%02x%02x (c=0x%08x)",
        x, y, bitmap->width, bitmap->rows, rdcon->color.r, rdcon->color.g, rdcon->color.b, rdcon->color.c);
    ImageSurface* surface = rdcon->ui_context->surface;
    for (int i = top - y; i < bottom - y; i++) {
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + i) * surface->pitch;
        for (int j = left - x; j < right - x; j++) {
            if (x + j < 0 || x + j >= surface->width) continue;
            uint32_t intensity = bitmap->buffer[i * bitmap->pitch + j];
            if (intensity > 0) {
                // blend the pixel with the background
                uint8_t* p = (uint8_t*)(row_pixels + (x + j) * 4);
                // important to use 32bit int for computation below
                uint32_t v = 255 - intensity;
                // can further optimize if background is a fixed color
                if (rdcon->color.c == 0xFF000000) { // black text color (ABGR: alpha=FF, b=00, g=00, r=00)
                    p[0] = p[0] * v / 255;
                    p[1] = p[1] * v / 255;
                    p[2] = p[2] * v / 255;
                    p[3] = 0xFF;
                }
                else { // non-black text color
                    p[0] = (p[0] * v + rdcon->color.r * intensity) / 255;
                    p[1] = (p[1] * v + rdcon->color.g * intensity) / 255;
                    p[2] = (p[2] * v + rdcon->color.b * intensity) / 255;
                    p[3] = 0xFF;  // alpha channel
                }
            }
        }
    }
}

extern CssEnum get_white_space_value(DomNode* node);

void render_text_view(RenderContext* rdcon, ViewText* text_view) {
    log_debug("render_text_view clip:[%.0f,%.0f,%.0f,%.0f]",
        rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
    if (!rdcon->font.ft_face) {
        log_debug("font face is null");
        return;
    }
    unsigned char* str = text_view->text_data();
    TextRect* text_rect = text_view->rect;

    if (!text_rect) {
        log_debug("no text rect for text view");
        return;
    }

    // Apply text color from text_view if set (PDF text uses this for fill color)
    Color saved_color = rdcon->color;
    if (text_view->color.c != 0) {
        rdcon->color = text_view->color;
    }

    // Setup font from text_view if set (PDF text has font property directly on ViewText)
    FontBox saved_font = rdcon->font;
    if (text_view->font) {
        setup_font(rdcon->ui_context, &rdcon->font, text_view->font);
    }

    // Skip rendering if font size is 0 - text should be invisible (e.g., font-size: 0)
    if (rdcon->font.style && rdcon->font.style->font_size <= 0.0f) {
        log_debug("skipping zero font-size text render");
        return;
    }

    // Get the white-space property for this text node
    CssEnum white_space = get_white_space_value(text_view);
    bool preserve_spaces = ws_preserve_spaces(white_space);

    // Get text-transform from parent elements
    CssEnum text_transform = CSS_VALUE_NONE;
    CssEnum text_align = CSS_VALUE_LEFT;  // default to left alignment
    DomNode* parent = text_view->parent;
    while (parent) {
        if (parent->is_element()) {
            DomElement* elem = (DomElement*)parent;
            CssEnum transform = get_text_transform_from_block(elem->blk);
            if (transform != CSS_VALUE_NONE) {
                text_transform = transform;
            }
            // Get text-align property from block properties
            if (elem->blk) {
                BlockProp* blk_prop = (BlockProp*)elem->blk;
                text_align = blk_prop->text_align;
            }
            if (transform != CSS_VALUE_NONE) {
                break;
            }
        }
        parent = parent->parent;
    }

    // Check if parent inline element has a background color to render
    DomElement* parent_elem = text_view->parent ? text_view->parent->as_element() : nullptr;
    Color* bg_color = nullptr;
    if (parent_elem && parent_elem->bound && parent_elem->bound->background &&
        parent_elem->bound->background->color.a > 0) {
        bg_color = &parent_elem->bound->background->color;
    }

    while (text_rect) {
        float x = rdcon->block.x + text_rect->x, y = rdcon->block.y + text_rect->y;

        // Render background for inline element if present
        if (bg_color) {
            Rect bg_rect = {x, y, text_rect->width, text_rect->height};
            fill_surface_rect(rdcon->ui_context->surface, &bg_rect, bg_color->c, &rdcon->block.clip);
        }

        unsigned char* p = str + text_rect->start_index;  unsigned char* end = p + text_rect->length;
        log_debug("draw text:'%t', start:%d, len:%d, x:%f, y:%f, wd:%f, hg:%f, at (%f, %f), white_space:%d, preserve:%d, color:0x%08x",
            str, text_rect->start_index, text_rect->length, text_rect->x, text_rect->y, text_rect->width, text_rect->height, x, y,
            white_space, preserve_spaces, rdcon->color.c);

        // Calculate natural text width and space count for justify rendering
        float natural_width = 0.0f;
        int space_count = 0;
        unsigned char* scan = p;
        bool scan_has_space = false;

        // Scan all content including trailing spaces for width calculation
        // Trailing whitespace is intentionally included because layout has already
        // determined the correct width and positioning - we should render exactly
        // what was laid out, including spaces between inline elements
        unsigned char* content_end = end;

        while (scan < content_end) {
            if (is_space(*scan)) {
                if (preserve_spaces || !scan_has_space) {
                    scan_has_space = true;
                    natural_width += rdcon->font.style->space_width;
                    space_count++;
                }
                scan++;
            }
            else {
                scan_has_space = false;
                uint32_t scan_codepoint;
                int bytes = utf8_to_codepoint(scan, &scan_codepoint);
                if (bytes <= 0) { scan++; }
                else { scan += bytes; }

                auto t1 = std::chrono::high_resolution_clock::now();
                FT_GlyphSlot glyph = load_glyph(rdcon->ui_context, rdcon->font.ft_face, rdcon->font.style, scan_codepoint, false);
                auto t2 = std::chrono::high_resolution_clock::now();
                g_render_load_glyph_time += std::chrono::duration<double, std::milli>(t2 - t1).count();
                g_render_glyph_count++;
                if (glyph) {
                    natural_width += glyph->advance.x / 64.0;
                } else {
                    natural_width += rdcon->font.style->space_width;  // fallback width
                }
            }
        }

        // Calculate adjusted space width for justified text
        float space_width = rdcon->font.style->space_width;
        if (text_align == CSS_VALUE_JUSTIFY && space_count > 0 && natural_width > 0 && text_rect->width > natural_width) {
            // This text is explicitly justified - distribute extra space across spaces
            float extra_space = text_rect->width - natural_width;
            space_width += (extra_space / space_count);
            log_debug("apply justification: text_align=JUSTIFY, natural_width=%f, text_rect->width=%f, space_count=%d, space_width=%f -> %f",
                natural_width, text_rect->width, space_count, rdcon->font.style->space_width, space_width);
        }

        // Render the text with adjusted spacing
        bool has_space = false;  uint32_t codepoint;
        bool is_word_start = true;  // Track word boundaries for capitalize
        while (p < end) {
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
                p++;
            }
            else {
                has_space = false;
                int bytes = utf8_to_codepoint(p, &codepoint);
                if (bytes <= 0) { p++;  codepoint = 0; }
                else { p += bytes; }

                // Apply text-transform before loading glyph
                codepoint = apply_text_transform(codepoint, text_transform, is_word_start);
                is_word_start = false;

                auto t1 = std::chrono::high_resolution_clock::now();
                FT_GlyphSlot glyph = load_glyph(rdcon->ui_context, rdcon->font.ft_face, rdcon->font.style, codepoint, true);
                auto t2 = std::chrono::high_resolution_clock::now();
                g_render_load_glyph_time += std::chrono::duration<double, std::milli>(t2 - t1).count();
                g_render_glyph_count++;
                if (!glyph) {
                    // draw a square box for missing glyph
                    Rect rect = {x + 1, y, (float)(rdcon->font.style->space_width - 2), (float)(rdcon->font.ft_face->size->metrics.y_ppem / 64.0)};
                    fill_surface_rect(rdcon->ui_context->surface, &rect, 0xFF0000FF, &rdcon->block.clip);
                    x += rdcon->font.style->space_width;
                }
                else {
                    // draw the glyph to the image buffer
                    float ascend = rdcon->font.ft_face->size->metrics.ascender / 64.0; // still use orginal font ascend to align glyphs at same baseline
                    auto t3 = std::chrono::high_resolution_clock::now();
                    draw_glyph(rdcon, &glyph->bitmap, x + glyph->bitmap_left, y + ascend - glyph->bitmap_top);
                    auto t4 = std::chrono::high_resolution_clock::now();
                    g_render_draw_glyph_time += std::chrono::duration<double, std::milli>(t4 - t3).count();
                    g_render_draw_count++;
                    // advance to the next position
                    x += glyph->advance.x / 64.0;
                }
            }
        }
        // render text deco
        if (rdcon->font.style->text_deco != CSS_VALUE_NONE) {
            float thinkness = max(rdcon->font.ft_face->underline_thickness / 64.0, 1);
            Rect rect;
            // todo: underline probably shoul draw below/before the text, and leaves a gap where text has descender
            if (rdcon->font.style->text_deco == CSS_VALUE_UNDERLINE) {
                // underline drawn at baseline, with a gap of thickness
                rect.x = rdcon->block.x + text_rect->x;  rect.y = rdcon->block.y + text_rect->y +
                    (rdcon->font.ft_face->size->metrics.ascender / 64.0) + thinkness;
            }
            else if (rdcon->font.style->text_deco == CSS_VALUE_OVERLINE) {
                rect.x = rdcon->block.x + text_rect->x;  rect.y = rdcon->block.y + text_rect->y;
            }
            else if (rdcon->font.style->text_deco == CSS_VALUE_LINE_THROUGH) {
                rect.x = rdcon->block.x + text_rect->x;  rect.y = rdcon->block.y + text_rect->y + text_rect->height / 2;
            }
            rect.width = text_rect->width;  rect.height = thinkness; // corrected the variable name from h to height
            log_debug("text deco: %d, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f", rdcon->font.style->text_deco,
                rect.x, rect.y, rect.width, rect.height); // corrected w to width
            fill_surface_rect(rdcon->ui_context->surface, &rect, rdcon->color.c, &rdcon->block.clip);
        }
        text_rect = text_rect->next;
    }

    // Restore color and font (in case they were changed for PDF text)
    rdcon->font = saved_font;
    rdcon->color = saved_color;
}

// Function to convert integer to Roman numeral
/*
static void toRoman(int num, char* result, int uppercase) {
    if (num <= 0 || num >= 4000) {
        strcpy(result, "invalid");
        return;
    }
    const int values[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    const char* symbols_lower[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
    const char* symbols_upper[] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
    const char** symbols = uppercase ? symbols_upper : symbols_lower;
    result[0] = '\0';
    int i = 0;
    while (num > 0) {
        while (num >= values[i]) {
            strcat(result, symbols[i]);
            num -= values[i];
        }
        i++;
    }
}

// list bullet formatting function
void formatListNumber(StrBuf* buf, int num, CssEnum list_style) {
    if (num <= 0) { return; }
    switch (list_style) {
        case CSS_VALUE_LOWER_ROMAN:
            toRoman(num, buf, 0);
            break;
        case CSS_VALUE_UPPER_ROMAN:
            toRoman(num, buf, 1);
            break;
        case CSS_VALUE_UPPER_ALPHA:
            if (num > 26) {
                strcpy(result, "invalid");
            } else {
                result[0] = 'A' + (num - 1);
                result[1] = '\0';
            }
            break;
        case CSS_VALUE_LOWER_ALPHA:
            if (num > 26) {
                strcpy(result, "invalid");
            } else {
                result[0] = 'a' + (num - 1);
                result[1] = '\0';
            }
            break;
    }
}
*/

/**
 * Render a ViewMarker (list bullet or number) using vector graphics.
 * Bullets (disc, circle, square) are drawn as shapes with fixed width.
 * Text markers (decimal, roman, alpha) render text right-aligned within fixed width.
 */
void render_marker_view(RenderContext* rdcon, ViewSpan* marker) {
    if (!marker || !marker->is_element()) return;

    DomElement* elem = (DomElement*)marker;
    MarkerProp* marker_prop = (MarkerProp*)elem->blk;
    if (!marker_prop) {
        log_debug("[MARKER RENDER] No marker_prop found");
        return;
    }

    float x = rdcon->block.x + marker->x;
    float y = rdcon->block.y + marker->y;
    float width = marker_prop->width;
    float bullet_size = marker_prop->bullet_size;
    CssEnum marker_type = marker_prop->marker_type;

    // Get current color (inherit from parent text color)
    Color color = rdcon->color;

    log_debug("[MARKER RENDER] type=%d, x=%.1f, y=%.1f, width=%.1f, bullet_size=%.1f",
             marker_type, x, y, width, bullet_size);

    switch (marker_type) {
        case CSS_VALUE_DISC: {
            // Filled circle - center vertically in line, positioned at right side of marker box
            // Note: y_ppem is already in pixels, but ascender is in 26.6 fixed point
            float font_size = rdcon->font.ft_face ? (float)rdcon->font.ft_face->size->metrics.y_ppem : 16.0f;
            float baseline_offset = rdcon->font.ft_face ? (rdcon->font.ft_face->size->metrics.ascender / 64.0f) : 12.0f;
            // Position bullet center: x at right side of marker box (with small gap), y at middle of x-height
            float cx = x + width - bullet_size - 4.0f;  // 4px gap from right edge
            float cy = y + baseline_offset - font_size * 0.35f;  // center on x-height
            float radius = bullet_size / 2.0f;

            // Draw filled circle using ThorVG
            Tvg_Canvas* canvas = rdcon->canvas;
            Tvg_Paint* shape = tvg_shape_new();
            tvg_shape_append_circle(shape, cx, cy, radius, radius);
            tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
            tvg_canvas_push(canvas, shape);
            tvg_canvas_draw(canvas, false);
            tvg_canvas_sync(canvas);
            log_debug("[MARKER RENDER] Drew disc at (%.1f, %.1f) r=%.1f", cx, cy, radius);
            break;
        }

        case CSS_VALUE_CIRCLE: {
            // Stroked circle (outline only)
            float font_size = rdcon->font.ft_face ? (float)rdcon->font.ft_face->size->metrics.y_ppem : 16.0f;
            float baseline_offset = rdcon->font.ft_face ? (rdcon->font.ft_face->size->metrics.ascender / 64.0f) : 12.0f;
            float cx = x + width - bullet_size - 4.0f;
            float cy = y + baseline_offset - font_size * 0.35f;
            float radius = bullet_size / 2.0f;
            float stroke_width = 1.0f;

            Tvg_Canvas* canvas = rdcon->canvas;
            Tvg_Paint* shape = tvg_shape_new();
            tvg_shape_append_circle(shape, cx, cy, radius - stroke_width/2, radius - stroke_width/2);
            tvg_shape_set_stroke_color(shape, color.r, color.g, color.b, color.a);
            tvg_shape_set_stroke_width(shape, stroke_width);
            tvg_canvas_push(canvas, shape);
            tvg_canvas_draw(canvas, false);
            tvg_canvas_sync(canvas);
            log_debug("[MARKER RENDER] Drew circle outline at (%.1f, %.1f) r=%.1f", cx, cy, radius);
            break;
        }

        case CSS_VALUE_SQUARE: {
            // Filled square
            float font_size = rdcon->font.ft_face ? (float)rdcon->font.ft_face->size->metrics.y_ppem : 16.0f;
            float baseline_offset = rdcon->font.ft_face ? (rdcon->font.ft_face->size->metrics.ascender / 64.0f) : 12.0f;
            float sx = x + width - bullet_size - 4.0f;
            float sy = y + baseline_offset - font_size * 0.35f - bullet_size/2;

            Tvg_Canvas* canvas = rdcon->canvas;
            Tvg_Paint* shape = tvg_shape_new();
            tvg_shape_append_rect(shape, sx, sy, bullet_size, bullet_size, 0, 0);
            tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
            tvg_canvas_push(canvas, shape);
            tvg_canvas_draw(canvas, false);
            tvg_canvas_sync(canvas);
            log_debug("[MARKER RENDER] Drew square at (%.1f, %.1f) size=%.1f", sx, sy, bullet_size);
            break;
        }

        case CSS_VALUE_DECIMAL:
        case CSS_VALUE_LOWER_ROMAN:
        case CSS_VALUE_UPPER_ROMAN:
        case CSS_VALUE_LOWER_ALPHA:
        case CSS_VALUE_UPPER_ALPHA: {
            // Text markers - render the text_content right-aligned within marker width
            if (marker_prop->text_content && *marker_prop->text_content) {
                // TODO: Implement text rendering for numbered markers
                // For now, log that we need to render text
                log_debug("[MARKER RENDER] Text marker: '%s' (type=%d)",
                         marker_prop->text_content, marker_type);
            }
            break;
        }

        default:
            log_debug("[MARKER RENDER] Unsupported marker type: %d", marker_type);
            break;
    }
}

/**
 * Render vector path (for PDF curves and complex paths)
 * Uses ThorVG to render Bezier curves and path segments
 */
void render_vector_path(RenderContext* rdcon, ViewBlock* block) {
    VectorPathProp* vpath = block->vpath;
    if (!vpath || !vpath->segments) return;

    log_info("[VPATH] Rendering vector path for block at (%.1f, %.1f)", block->x, block->y);

    Tvg_Canvas* canvas = rdcon->canvas;
    Tvg_Paint* shape = tvg_shape_new();
    if (!shape) {
        log_error("[VPATH] Failed to create ThorVG shape");
        return;
    }

    // Build the path from segments
    float offset_x = rdcon->block.x + block->x;
    float offset_y = rdcon->block.y + block->y;

    for (VectorPathSegment* seg = vpath->segments; seg; seg = seg->next) {
        float sx = offset_x + seg->x;
        float sy = offset_y + seg->y;

        switch (seg->type) {
            case VectorPathSegment::VPATH_MOVETO:
                tvg_shape_move_to(shape, sx, sy);
                log_debug("[VPATH] moveto (%.1f, %.1f)", sx, sy);
                break;
            case VectorPathSegment::VPATH_LINETO:
                tvg_shape_line_to(shape, sx, sy);
                log_debug("[VPATH] lineto (%.1f, %.1f)", sx, sy);
                break;
            case VectorPathSegment::VPATH_CURVETO: {
                float cx1 = offset_x + seg->x1;
                float cy1 = offset_y + seg->y1;
                float cx2 = offset_x + seg->x2;
                float cy2 = offset_y + seg->y2;
                tvg_shape_cubic_to(shape, cx1, cy1, cx2, cy2, sx, sy);
                log_debug("[VPATH] curveto (%.1f,%.1f)-(%.1f,%.1f)->(%.1f,%.1f)", cx1, cy1, cx2, cy2, sx, sy);
                break;
            }
            case VectorPathSegment::VPATH_CLOSE:
                tvg_shape_close(shape);
                log_debug("[VPATH] close");
                break;
        }
    }

    // Apply stroke if present
    if (vpath->has_stroke) {
        tvg_shape_set_stroke_color(shape,
            vpath->stroke_color.r, vpath->stroke_color.g, vpath->stroke_color.b, vpath->stroke_color.a);
        tvg_shape_set_stroke_width(shape, vpath->stroke_width);

        // Apply dash pattern if present
        if (vpath->dash_pattern && vpath->dash_pattern_length > 0) {
            log_debug("[VPATH] Setting dash pattern: count=%d, values=[%.1f, %.1f]",
                     vpath->dash_pattern_length,
                     vpath->dash_pattern[0],
                     vpath->dash_pattern_length > 1 ? vpath->dash_pattern[1] : 0.0f);
            Tvg_Result result = tvg_shape_set_stroke_dash(shape, vpath->dash_pattern, vpath->dash_pattern_length, 0);
            log_debug("[VPATH] tvg_shape_set_stroke_dash returned: %d", result);
            // Set butt cap for crisp dash ends
            tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_BUTT);
        }

        log_debug("[VPATH] Stroke: RGB(%d,%d,%d) width=%.1f",
                 vpath->stroke_color.r, vpath->stroke_color.g, vpath->stroke_color.b, vpath->stroke_width);
    }

    // Apply fill if present
    if (vpath->has_fill) {
        tvg_shape_set_fill_color(shape,
            vpath->fill_color.r, vpath->fill_color.g, vpath->fill_color.b, vpath->fill_color.a);
    }

    // Push to canvas and render
    tvg_canvas_remove(canvas, NULL);  // clear any existing shapes
    tvg_canvas_push(canvas, shape);
    tvg_canvas_draw(canvas, false);
    tvg_canvas_sync(canvas);

    log_info("[VPATH] Rendered vector path successfully");
}

void render_list_bullet(RenderContext* rdcon, ViewBlock* list_item) {
    // bullets are aligned to the top and right side of the list item
    float ratio = rdcon->ui_context->pixel_ratio;
    if (rdcon->list.list_style_type == CSS_VALUE_DISC) {
        Rect rect;
        rect.x = rdcon->block.x + list_item->x - 15 * ratio;
        rect.y = rdcon->block.y + list_item->y + 7 * ratio;
        rect.width = rect.height = 5 * ratio;
        fill_surface_rect(rdcon->ui_context->surface, &rect, rdcon->color.c, &rdcon->block.clip);
    }
    else if (rdcon->list.list_style_type == CSS_VALUE_DECIMAL) {
        log_debug("render list decimal");
        // StrBuf* num = strbuf_new_cap(10);
        // strbuf_append_format(num, "%d.", rdcon->list.item_index);
        // // output the number as VIEW_TEXT
        // lxb_dom_text_t lxb_node;  ViewText text;
        // // Initialize the lexbor text node structure properly
        // memset(&lxb_node, 0, sizeof(lxb_dom_text_t));
        // lxb_node.char_data.node.type = LXB_DOM_NODE_TYPE_TEXT;
        // lxb_node.char_data.data.data = (unsigned char *)num->str;
        // lxb_node.char_data.data.length = num->length;

        // // Initialize the ViewText structure
        // text.type = RDT_VIEW_TEXT;  text.next = NULL;  text.parent = NULL;
        // text.font = rdcon->font.style;
        // TextRect text_rect;
        // text.rect = &text_rect;  text_rect.next = NULL;
        // text_rect.start_index = 0;  text_rect.length = num->length;

        // // Create DomNode wrapper
        // DomNode dom_wrapper;
        // memset(&dom_wrapper, 0, sizeof(DomNode));
        // dom_wrapper.type = LEXBOR_NODE;
        // dom_wrapper.lxb_node = (lxb_dom_node_t*)&lxb_node;
        // text.node = &dom_wrapper;
        // float font_size = rdcon->font.ft_face->size->metrics.y_ppem / 64.0;
        // text.x = list_item->x - 20 * ratio;
        // text.y = list_item->y;  // align at top the list item
        // text.width = text_rect.length * font_size;  text.height = font_size;
        // render_text_view(rdcon, &text);
        // strbuf_free(num);
    }
    else {
        log_debug("unknown list style type");
    }
}

void render_litem_view(RenderContext* rdcon, ViewBlock* list_item) {
    log_debug("view list item:%s", list_item->node_name());
    rdcon->list.item_index++;
    render_block_view(rdcon, list_item);
}

void render_list_view(RenderContext* rdcon, ViewBlock* view) {
    ViewBlock* list = (ViewBlock*)view;
    log_debug("view list:%s", list->node_name());
    ListBlot pa_list = rdcon->list;
    rdcon->list.item_index = 0;  rdcon->list.list_style_type = list->blk->list_style_type;
    render_block_view(rdcon, list);
    rdcon->list = pa_list;
}

/**
 * Render column rules for multi-column containers
 * Column rules are drawn as vertical lines between columns
 */
void render_column_rules(RenderContext* rdcon, ViewBlock* block) {
    if (!block->multicol) return;

    MultiColumnProp* mc = block->multicol;

    // Only render if we have rules and multiple columns
    if (mc->computed_column_count <= 1 || mc->rule_width <= 0 ||
        mc->rule_style == CSS_VALUE_NONE) {
        return;
    }

    float column_width = mc->computed_column_width;
    float gap = mc->column_gap_is_normal ? 16.0f : mc->column_gap;

    // Calculate block position
    float block_x = rdcon->block.x + block->x;
    float block_y = rdcon->block.y + block->y;

    // Adjust for padding
    if (block->bound) {
        block_x += block->bound->padding.left;
        block_y += block->bound->padding.top;
    }

    // Rule height is the content area height (block height minus padding/border)
    // For multi-column containers, get the actual content height from block->height
    float rule_height = block->height;
    if (block->bound) {
        // Subtract padding from top (already offset block_y, so don't need to subtract again)
        // but we need to subtract total padding for height calculation
        rule_height -= block->bound->padding.top + block->bound->padding.bottom;
        if (block->bound->border) {
            rule_height -= block->bound->border->width.top + block->bound->border->width.bottom;
        }
    }

    // Ensure minimum rule height
    if (rule_height <= 0) {
        // Fall back to using the block's content height by iterating children
        View* child = (View*)block->first_child;
        float max_bottom = 0;
        while (child) {
            if (child->is_element()) {
                ViewBlock* child_block = (ViewBlock*)child;
                float child_bottom = child_block->y + child_block->height;
                if (child_bottom > max_bottom) max_bottom = child_bottom;
            }
            child = child->next();
        }
        rule_height = max_bottom;
        log_debug("[MULTICOL] Rule height computed from children: %.1f", rule_height);
    }

    log_debug("[MULTICOL] Rendering %d column rules, width=%.1f, style=%d",
              mc->computed_column_count - 1, mc->rule_width, mc->rule_style);

    // Draw rule between each pair of columns
    for (int i = 0; i < mc->computed_column_count - 1; i++) {
        float rule_x = block_x + (i + 1) * column_width + i * gap + gap / 2.0f - mc->rule_width / 2.0f;

        // Create rule shape
        Tvg_Paint* rule = tvg_shape_new();

        // Different stroke patterns for different styles
        if (mc->rule_style == CSS_VALUE_DOTTED) {
            // Dotted line: small dashes
            float dash_pattern[] = {mc->rule_width, mc->rule_width * 2};
            tvg_shape_set_stroke_dash(rule, dash_pattern, 2, 0);
        } else if (mc->rule_style == CSS_VALUE_DASHED) {
            // Dashed line: longer dashes
            float dash_pattern[] = {mc->rule_width * 3, mc->rule_width * 2};
            tvg_shape_set_stroke_dash(rule, dash_pattern, 2, 0);
        } else if (mc->rule_style == CSS_VALUE_DOUBLE) {
            // Double: two lines (render first here, second below)
            // For double, we draw two thinner lines
            float thin_width = mc->rule_width / 3.0f;
            tvg_shape_append_rect(rule, rule_x - thin_width, block_y, thin_width, rule_height, 0, 0);
            tvg_shape_append_rect(rule, rule_x + thin_width, block_y, thin_width, rule_height, 0, 0);
            tvg_shape_set_fill_color(rule, mc->rule_color.r, mc->rule_color.g,
                                     mc->rule_color.b, mc->rule_color.a);
            push_with_transform(rdcon, rule);
            continue;
        }

        // Solid, dotted, dashed: draw as vertical line
        tvg_shape_move_to(rule, rule_x, block_y);
        tvg_shape_line_to(rule, rule_x, block_y + rule_height);
        tvg_shape_set_stroke_width(rule, mc->rule_width);
        tvg_shape_set_stroke_color(rule, mc->rule_color.r, mc->rule_color.g,
                                   mc->rule_color.b, mc->rule_color.a);
        tvg_shape_set_stroke_cap(rule, TVG_STROKE_CAP_BUTT);

        push_with_transform(rdcon, rule);

        log_debug("[MULTICOL] Rule %d at x=%.1f, height=%.1f", i, rule_x, rule_height);
    }
}

// Helper function to render linear gradient
void render_bound(RenderContext* rdcon, ViewBlock* view) {
    Rect rect;
    rect.x = rdcon->block.x + view->x;  rect.y = rdcon->block.y + view->y;
    rect.width = view->width;  rect.height = view->height;

    // Render box-shadow BEFORE background (shadows go underneath the element)
    if (view->bound->box_shadow) {
        render_box_shadow(rdcon, view, rect);
    }

    // Render background (gradient or solid color) using new rendering system
    if (view->bound->background) {
        render_background(rdcon, view, rect);
    }

    // Load background image if specified
    if (view->bound->background && view->bound->background->image) {
        const char* image_url = view->bound->background->image;
        log_debug("[RENDER] background-image on %s: loading '%s' (size: %.0fx%.0f) bg_ptr=%p",
                  view->node_name(), image_url, rect.width, rect.height, view->bound->background);

        // Use proper URL resolution
        if (!rdcon->ui_context->document || !rdcon->ui_context->document->url) {
            log_error("[RENDER] background-image: missing document URL context");
        } else {
            Url* abs_url = parse_url(rdcon->ui_context->document->url, image_url);
            if (!abs_url) {
                log_error("[RENDER] background-image: failed to parse URL '%s'", image_url);
            } else {
                char* file_path = url_to_local_path(abs_url);
                if (!file_path) {
                    log_error("[RENDER] background-image: invalid local URL '%s'", image_url);
                } else {
                    // Try loading the image
                    Tvg_Paint* pic = tvg_picture_new();
                    Tvg_Result result = tvg_picture_load(pic, file_path);

                    // If loading failed and URL starts with "./", try prepending "res/"
                    // (workaround for CSS-relative URLs that need res/ subdirectory)
                    if (result != TVG_RESULT_SUCCESS && image_url[0] == '.' && image_url[1] == '/') {
                        log_debug("[RENDER] background-image: trying with res/ prefix");
                        char* res_url = (char*)malloc(strlen(image_url) + 5);
                        sprintf(res_url, "./res/%s", image_url + 2);
                        url_destroy(abs_url);
                        abs_url = parse_url(rdcon->ui_context->document->url, res_url);
                        free(res_url);
                        if (abs_url) {
                            char* new_file_path = url_to_local_path(abs_url);
                            if (new_file_path) {
                                file_path = new_file_path;
                                result = tvg_picture_load(pic, file_path);
                            }
                        }
                    }

                    if (result == TVG_RESULT_SUCCESS) {
                        log_debug("[RENDER] background-image: loaded successfully from '%s'", file_path);
                        tvg_canvas_remove(rdcon->canvas, NULL);
                        tvg_picture_set_size(pic, rect.width, rect.height);
                        tvg_paint_translate(pic, rect.x, rect.y);

                        // Apply clipping
                        Tvg_Paint* clip_rect = tvg_shape_new();
                        Bound* clip = &rdcon->block.clip;
                        tvg_shape_append_rect(clip_rect, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, 0, 0);
                        tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255);
                        tvg_paint_set_mask_method(pic, clip_rect, TVG_MASK_METHOD_ALPHA);

                        tvg_canvas_push(rdcon->canvas, pic);
                        tvg_canvas_draw(rdcon->canvas, false);
                        tvg_canvas_sync(rdcon->canvas);
                    } else {
                        log_error("[RENDER] background-image: failed to load '%s'", file_path);
                        tvg_paint_del(pic);
                    }
                }
                url_destroy(abs_url);
            }
        }
    }

    // Render borders using new rendering system
    if (view->bound->border) {
        log_debug("render border");

        // CSS 2.1 §17.6.2: Use resolved borders for border-collapse cells
        bool use_resolved = false;
        CollapsedBorder* resolved_top = nullptr;
        CollapsedBorder* resolved_right = nullptr;
        CollapsedBorder* resolved_bottom = nullptr;
        CollapsedBorder* resolved_left = nullptr;

        if (view->view_type == RDT_VIEW_TABLE_CELL) {
            ViewTableCell* cell = (ViewTableCell*)view;
            if (cell->td && cell->td->top_resolved) {
                use_resolved = true;
                resolved_top = cell->td->top_resolved;
                resolved_right = cell->td->right_resolved;
                resolved_bottom = cell->td->bottom_resolved;
                resolved_left = cell->td->left_resolved;
            }
        }

        if (use_resolved) {
            // Render collapsed borders using resolved border data (table cells)
            if (resolved_left && resolved_left->style != CSS_VALUE_NONE && resolved_left->color.a) {
                Rect border_rect = rect;
                border_rect.width = resolved_left->width;
                fill_surface_rect(rdcon->ui_context->surface, &border_rect, resolved_left->color.c, &rdcon->block.clip);
            }
            if (resolved_right && resolved_right->style != CSS_VALUE_NONE && resolved_right->color.a) {
                Rect border_rect = rect;
                border_rect.x = rect.x + rect.width - resolved_right->width;
                border_rect.width = resolved_right->width;
                fill_surface_rect(rdcon->ui_context->surface, &border_rect, resolved_right->color.c, &rdcon->block.clip);
            }
            if (resolved_top && resolved_top->style != CSS_VALUE_NONE && resolved_top->color.a) {
                Rect border_rect = rect;
                border_rect.height = resolved_top->width;
                fill_surface_rect(rdcon->ui_context->surface, &border_rect, resolved_top->color.c, &rdcon->block.clip);
            }
            if (resolved_bottom && resolved_bottom->style != CSS_VALUE_NONE && resolved_bottom->color.a) {
                Rect border_rect = rect;
                border_rect.y = rect.y + rect.height - resolved_bottom->width;
                border_rect.height = resolved_bottom->width;
                fill_surface_rect(rdcon->ui_context->surface, &border_rect, resolved_bottom->color.c, &rdcon->block.clip);
            }
        } else {
            // Use new comprehensive border rendering
            render_border(rdcon, view, rect);
        }
    }
}

void draw_debug_rect(Tvg_Canvas* canvas, Rect rect, Bound* clip) {
    tvg_canvas_remove(canvas, NULL);  // clear any existing shapes
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_move_to(shape, rect.x, rect.y);
    tvg_shape_line_to(shape, rect.x + rect.width, rect.y);
    tvg_shape_line_to(shape, rect.x + rect.width, rect.y + rect.height);
    tvg_shape_line_to(shape, rect.x, rect.y + rect.height);
    tvg_shape_close(shape);
    tvg_shape_set_stroke_width(shape, 2); // stroke width of 2 pixels
    tvg_shape_set_stroke_color(shape, 255, 0, 0, 100); // Red stroke color (RGBA)
    // define the dash pattern for a dotted line
    float dash_pattern[2] = {8.0f, 8.0f}; // 8 units on, 8 units off
    tvg_shape_set_stroke_dash(shape, dash_pattern, 2, 0);

    // set clipping
    Tvg_Paint* clip_rect = tvg_shape_new();
    tvg_shape_append_rect(clip_rect, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, 0, 0);
    tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255); // solid fill
    tvg_paint_set_mask_method(shape, clip_rect, TVG_MASK_METHOD_ALPHA);

    tvg_canvas_push(canvas, shape);
    tvg_canvas_draw(canvas, false);
    tvg_canvas_sync(canvas);
}

void setup_scroller(RenderContext* rdcon, ViewBlock* block) {
    if (block->scroller->has_clip) {
        log_debug("setup scroller clip: left:%f, top:%f, right:%f, bottom:%f",
            block->scroller->clip.left, block->scroller->clip.top, block->scroller->clip.right, block->scroller->clip.bottom);
        rdcon->block.clip.left = max(rdcon->block.clip.left, rdcon->block.x + block->scroller->clip.left);
        rdcon->block.clip.top = max(rdcon->block.clip.top, rdcon->block.y + block->scroller->clip.top);
        rdcon->block.clip.right = min(rdcon->block.clip.right, rdcon->block.x + block->scroller->clip.right);
        rdcon->block.clip.bottom = min(rdcon->block.clip.bottom, rdcon->block.y + block->scroller->clip.bottom);

        // Copy border-radius for rounded corner clipping when overflow:hidden
        if (block->bound && block->bound->border) {
            BorderProp* border = block->bound->border;
            if (border->radius.top_left > 0 || border->radius.top_right > 0 ||
                border->radius.bottom_left > 0 || border->radius.bottom_right > 0) {
                rdcon->block.has_clip_radius = true;
                rdcon->block.clip_radius = border->radius;
                log_debug("setup rounded clip: tl=%f, tr=%f, bl=%f, br=%f",
                    border->radius.top_left, border->radius.top_right,
                    border->radius.bottom_left, border->radius.bottom_right);
            }
        }
    }
    if (block->scroller->pane) {
        rdcon->block.x -= block->scroller->pane->h_scroll_position;
        rdcon->block.y -= block->scroller->pane->v_scroll_position;
    }
}

void render_scroller(RenderContext* rdcon, ViewBlock* block, BlockBlot* pa_block) {
    log_debug("render scrollbars");
    // need to reset block.x and y, which was changed by the scroller
    rdcon->block.x = pa_block->x + block->x;  rdcon->block.y = pa_block->y + block->y;
    if (block->scroller->has_hz_scroll || block->scroller->has_vt_scroll) {
        Rect rect = {rdcon->block.x, rdcon->block.y, block->width, block->height};
        if (block->bound && block->bound->border) {
            rect.x += block->bound->border->width.left;
            rect.y += block->bound->border->width.top;
            rect.width -= block->bound->border->width.left + block->bound->border->width.right;
            rect.height -= block->bound->border->width.top + block->bound->border->width.bottom;
        }
        if (block->scroller->pane) {
            scrollpane_render(rdcon->canvas, block->scroller->pane, &rect,
                block->content_width, block->content_height, &rdcon->block.clip);
        } else {
            log_error("scroller has no scroll pane");
        }
    }
}

void render_block_view(RenderContext* rdcon, ViewBlock* block) {
    log_debug("render block view:%s, clip:[%.0f,%.0f,%.0f,%.0f]", block->node_name(),
        rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
    log_enter();
    BlockBlot pa_block = rdcon->block;  FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;

    // Save transform state and apply element's transform
    Tvg_Matrix pa_transform = rdcon->transform;
    bool pa_has_transform = rdcon->has_transform;

    if (block->transform && block->transform->functions) {
        // Calculate transform origin
        float origin_x = block->transform->origin_x_percent
            ? (block->transform->origin_x / 100.0f) * block->width
            : block->transform->origin_x;
        float origin_y = block->transform->origin_y_percent
            ? (block->transform->origin_y / 100.0f) * block->height
            : block->transform->origin_y;

        // Origin is relative to element's position in parent
        float elem_x = pa_block.x + block->x;
        float elem_y = pa_block.y + block->y;
        origin_x += elem_x;
        origin_y += elem_y;

        // Compute new transform matrix
        Tvg_Matrix new_transform = radiant::compute_transform_matrix(
            block->transform->functions, block->width, block->height, origin_x, origin_y);

        // If parent has transform, concatenate
        if (rdcon->has_transform) {
            // Matrix multiply: new = parent * element
            Tvg_Matrix combined = {
                pa_transform.e11 * new_transform.e11 + pa_transform.e12 * new_transform.e21 + pa_transform.e13 * new_transform.e31,
                pa_transform.e11 * new_transform.e12 + pa_transform.e12 * new_transform.e22 + pa_transform.e13 * new_transform.e32,
                pa_transform.e11 * new_transform.e13 + pa_transform.e12 * new_transform.e23 + pa_transform.e13 * new_transform.e33,
                pa_transform.e21 * new_transform.e11 + pa_transform.e22 * new_transform.e21 + pa_transform.e23 * new_transform.e31,
                pa_transform.e21 * new_transform.e12 + pa_transform.e22 * new_transform.e22 + pa_transform.e23 * new_transform.e32,
                pa_transform.e21 * new_transform.e13 + pa_transform.e22 * new_transform.e23 + pa_transform.e23 * new_transform.e33,
                pa_transform.e31 * new_transform.e11 + pa_transform.e32 * new_transform.e21 + pa_transform.e33 * new_transform.e31,
                pa_transform.e31 * new_transform.e12 + pa_transform.e32 * new_transform.e22 + pa_transform.e33 * new_transform.e32,
                pa_transform.e31 * new_transform.e13 + pa_transform.e32 * new_transform.e23 + pa_transform.e33 * new_transform.e33
            };
            rdcon->transform = combined;
        } else {
            rdcon->transform = new_transform;
        }
        rdcon->has_transform = true;

        log_debug("[TRANSFORM] Element %s: transform active, origin=(%.1f,%.1f)",
            block->node_name(), origin_x, origin_y);
    }

    if (block->font) {
        auto t1 = std::chrono::high_resolution_clock::now();
        setup_font(rdcon->ui_context, &rdcon->font, block->font);
        auto t2 = std::chrono::high_resolution_clock::now();
        g_render_setup_font_time += std::chrono::duration<double, std::milli>(t2 - t1).count();
        g_render_setup_font_count++;
    }
    // render bullet after setting the font, as bullet is rendered using the same font as the list item
    if (block->view_type == RDT_VIEW_LIST_ITEM) {
        render_list_bullet(rdcon, block);
    }
    if (block->bound) {
        // CSS 2.1 Section 17.6.1: empty-cells: hide suppresses borders/backgrounds
        bool skip_bound = false;
        if (block->view_type == RDT_VIEW_TABLE_CELL) {
            ViewTableCell* cell = (ViewTableCell*)block;
            if (cell->td && cell->td->hide_empty) {
                skip_bound = true;
                log_debug("Skipping bound for empty cell (empty-cells: hide)");
            }
        }
        if (!skip_bound) {
            render_bound(rdcon, block);
        }
    }

    // Render vector path if present (for PDF curves and complex paths)
    if (block->vpath && block->vpath->segments) {
        render_vector_path(rdcon, block);
    }

    rdcon->block.x = pa_block.x + block->x;  rdcon->block.y = pa_block.y + block->y;
    if (DEBUG_RENDER) {  // debugging outline around the block margin border
        Rect rc;
        rc.x = rdcon->block.x - (block->bound ? block->bound->margin.left : 0);
        rc.y = rdcon->block.y - (block->bound ? block->bound->margin.top : 0);
        rc.width = block->width + (block->bound ? block->bound->margin.left + block->bound->margin.right : 0);
        rc.height = block->height + (block->bound ? block->bound->margin.top + block->bound->margin.bottom : 0);
        draw_debug_rect(rdcon->canvas, rc, &rdcon->block.clip);
    }

    View* view = block->first_child;
    if (view) {
        if (block->in_line && block->in_line->color.c) {
            log_debug("[RENDER COLOR] element=%s setting color: #%02x%02x%02x (was #%02x%02x%02x) color.c=0x%08x",
                      block->node_name(),
                      block->in_line->color.r, block->in_line->color.g, block->in_line->color.b,
                      rdcon->color.r, rdcon->color.g, rdcon->color.b,
                      block->in_line->color.c);
            rdcon->color = block->in_line->color;
        } else {
            log_debug("[RENDER COLOR] element=%s inheriting color #%02x%02x%02x (in_line=%p, color.c=%u)",
                      block->node_name(), rdcon->color.r, rdcon->color.g, rdcon->color.b,
                      block->in_line, block->in_line ? block->in_line->color.c : 0);
        }
        // setup clip box
        if (block->scroller) {
            setup_scroller(rdcon, block);
        }
        // render negative z-index children
        render_children(rdcon, view);
        // render positive z-index children
        if (block->position) {
            log_debug("render absolute/fixed positioned children");
            ViewBlock* child_block = block->position->first_abs_child;
            while (child_block) {
                render_block_view(rdcon, child_block);
                child_block = child_block->position->next_abs_sibling;
            }
        }
    }
    else if (block->embed && block->embed->img) {
        // render embedded image for blocks without children (e.g., SVG document root)
        log_debug("render embedded image in block without children");
        render_image_content(rdcon, block);
    }
    else {
        log_debug("view has no child");
    }

    // render scrollbars
    if (block->scroller) {
        render_scroller(rdcon, block, &pa_block);
    }

    // Render multi-column rules between columns
    if (block->multicol && block->multicol->computed_column_count > 1) {
        render_column_rules(rdcon, block);
    }

    // Apply CSS filters after all content is rendered
    // Filters are applied to the rendered pixel data in the element's region
    if (block->filter && block->filter->functions) {
        // Sync canvas to ensure all content is rendered to the surface
        tvg_canvas_draw(rdcon->canvas, false);
        tvg_canvas_sync(rdcon->canvas);

        // Calculate the element's bounding rect
        Rect filter_rect;
        filter_rect.x = pa_block.x + block->x;
        filter_rect.y = pa_block.y + block->y;
        filter_rect.width = block->width;
        filter_rect.height = block->height;

        log_debug("[FILTER] Applying filters to element %s at (%.0f,%.0f) size %.0fx%.0f",
                  block->node_name(), filter_rect.x, filter_rect.y, filter_rect.width, filter_rect.height);

        // Apply the filter chain to the rendered pixels
        apply_css_filters(rdcon->ui_context->surface, block->filter, &filter_rect, &rdcon->block.clip);
    }

    // Restore transform state
    rdcon->transform = pa_transform;
    rdcon->has_transform = pa_has_transform;

    rdcon->block = pa_block;  rdcon->font = pa_font;  rdcon->color = pa_color;
    log_leave();
}

void render_svg(ImageSurface* surface) {
    if (!surface->pic) {
        log_debug("no picture to render");  return;
    }
    // Step 1: Create an offscreen canvas to render the original Picture
    Tvg_Canvas* canvas = tvg_swcanvas_create();
    if (!canvas) return;

    uint32_t width = surface->max_render_width;
    uint32_t height = surface->max_render_width * surface->height / surface->width;
    surface->pixels = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    if (!surface->pixels) {
        tvg_canvas_destroy(canvas);
        return;
    }

    // CRITICAL: Clear the buffer to transparent before rendering SVG
    // Without this, the SVG renders on top of garbage memory data
    memset(surface->pixels, 0, width * height * sizeof(uint32_t));

    // Set the canvas target to the buffer
    if (tvg_swcanvas_set_target(canvas, (uint32_t*)surface->pixels, width, width, height,
        TVG_COLORSPACE_ABGR8888) != TVG_RESULT_SUCCESS) {
        log_debug("Failed to set canvas target");
        free(surface->pixels);  surface->pixels = NULL;
        tvg_canvas_destroy(canvas);
        return;
    }

    tvg_picture_set_size(surface->pic, width, height);
    tvg_canvas_push(canvas, surface->pic);
    tvg_canvas_update(canvas);
    tvg_canvas_draw(canvas, true);
    tvg_canvas_sync(canvas);

    // Step 4: Clean up canvas
    tvg_canvas_destroy(canvas); // this also frees pic
    surface->pic = NULL;
    surface->width = width;  surface->height = height;  surface->pitch = width * sizeof(uint32_t);
}

// load surface pixels to a picture
Tvg_Paint* load_picture(ImageSurface* surface) {
    Tvg_Paint* pic = tvg_picture_new();
    if (!pic) { return NULL; }

    // Load the raw pixel data into the new Picture
    if (tvg_picture_load_raw(pic, (uint32_t*)surface->pixels, surface->width, surface->height,
        TVG_COLORSPACE_ABGR8888, false) != TVG_RESULT_SUCCESS) {
        log_debug("Failed to load raw pixel data");
        tvg_paint_del(pic);
        return NULL;
    }
    return pic;
}

// Helper function to render just the image content (without block layout)
// Used by both render_image_view and render_block_view for embedded images
void render_image_content(RenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embed->img) return;

    log_debug("render image content");
    ImageSurface* img = view->embed->img;
    Rect rect;
    rect.x = rdcon->block.x + view->x;  rect.y = rdcon->block.y + view->y;
    rect.width = view->width;  rect.height = view->height;
    log_debug("[IMAGE RENDER] url=%s, format=%d, img_size=%dx%d, view_size=%.0fx%.0f, pos=(%.0f,%.0f), clip=(%.0f,%.0f,%.0f,%.0f)",
              img->url && img->url->href ? img->url->href->chars : "unknown",
              img->format, img->width, img->height,
              rect.width, rect.height, rect.x, rect.y,
              rdcon->block.clip.left, rdcon->block.clip.top,
              rdcon->block.clip.right, rdcon->block.clip.bottom);
    if (img->format == IMAGE_FORMAT_SVG) {
        // render the SVG image
        log_debug("render svg image at x:%f, y:%f, wd:%f, hg:%f", rect.x, rect.y, rect.width, rect.height);
        if (!img->pixels) {
            render_svg(img);
        }
        Tvg_Paint* pic = load_picture(img);
        if (pic) {
            tvg_canvas_remove(rdcon->canvas, NULL);  // clear any existing shapes
            tvg_picture_set_size(pic, rect.width, rect.height);
            tvg_paint_translate(pic, rect.x, rect.y);
            // clip the svg picture
            Tvg_Paint* clip_rect = tvg_shape_new();  Bound* clip = &rdcon->block.clip;
            tvg_shape_append_rect(clip_rect, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, 0, 0);
            tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255); // solid fill
            tvg_paint_set_mask_method(pic, clip_rect, TVG_MASK_METHOD_ALPHA);
            tvg_canvas_push(rdcon->canvas, pic);
            tvg_canvas_draw(rdcon->canvas, false);
            tvg_canvas_sync(rdcon->canvas);
        } else {
            log_debug("failed to load svg picture");
        }
    } else {
        log_debug("blit image at x:%f, y:%f, wd:%f, hg:%f", rect.x, rect.y, rect.width, rect.height);
        blit_surface_scaled(img, NULL, rdcon->ui_context->surface, &rect, &rdcon->block.clip, SCALE_MODE_LINEAR);
    }
}

void render_image_view(RenderContext* rdcon, ViewBlock* view) {
    log_debug("render image view");
    log_enter();
    // render border and background, etc.
    render_block_view(rdcon, (ViewBlock*)view);
    // render the image content
    render_image_content(rdcon, view);
    log_debug("end of image render");
    log_leave();
}

void render_embed_doc(RenderContext* rdcon, ViewBlock* block) {
    BlockBlot pa_block = rdcon->block;
    if (block->bound) { render_bound(rdcon, block); }

    rdcon->block.x = pa_block.x + block->x;  rdcon->block.y = pa_block.y + block->y;

    // Constrain clip region to iframe content box (before scroller setup)
    // This ensures embedded documents (SVG, PDF, etc.) don't render outside iframe bounds
    float content_left = rdcon->block.x;
    float content_top = rdcon->block.y;
    float content_right = rdcon->block.x + block->width;
    float content_bottom = rdcon->block.y + block->height;

    // Adjust for borders if present
    if (block->bound && block->bound->border) {
        content_left += block->bound->border->width.left;
        content_top += block->bound->border->width.top;
        content_right -= block->bound->border->width.right;
        content_bottom -= block->bound->border->width.bottom;
    }

    // Intersect with parent clip region
    rdcon->block.clip.left = max(rdcon->block.clip.left, content_left);
    rdcon->block.clip.top = max(rdcon->block.clip.top, content_top);
    rdcon->block.clip.right = min(rdcon->block.clip.right, content_right);
    rdcon->block.clip.bottom = min(rdcon->block.clip.bottom, content_bottom);

    log_debug("iframe clip set to: left:%.0f, top:%.0f, right:%.0f, bottom:%.0f (content box)",
              rdcon->block.clip.left, rdcon->block.clip.top,
              rdcon->block.clip.right, rdcon->block.clip.bottom);

    // setup clip box for scrolling
    if (block->scroller) { setup_scroller(rdcon, block); }
    // render the embedded doc
    if (block->embed && block->embed->doc) {
        DomDocument* doc = block->embed->doc;
        // render html doc
        if (doc && doc->view_tree && doc->view_tree->root) {
            View* root_view = doc->view_tree->root;
            if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
                log_debug("render doc root view:");
                // Save parent context and reset for embedded document
                FontBox pa_font = rdcon->font;
                Color pa_color = rdcon->color;

                // Reset color to black for embedded document (don't inherit from parent doc)
                // Each document should start with default black text color
                rdcon->color.c = 0xFF000000;  // opaque black (ABGR)

                // load default font
                FontProp* default_font = doc->view_tree->html_version == HTML5 ? &rdcon->ui_context->default_font : &rdcon->ui_context->legacy_default_font;
                log_debug("render_init default font: %s, html version: %d", default_font->family, doc->view_tree->html_version);
                setup_font(rdcon->ui_context, &rdcon->font, default_font);

                render_block_view(rdcon, (ViewBlock*)root_view);

                rdcon->font = pa_font;
                rdcon->color = pa_color;
            }
            else {
                log_debug("Invalid root view");
            }
        }
    }

    // render scrollbars
    if (block->scroller) {
        render_scroller(rdcon, block, &pa_block);
    }
    rdcon->block = pa_block;
}

void render_inline_view(RenderContext* rdcon, ViewSpan* view_span) {
    FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;
    log_debug("render inline view");
    View* view = view_span->first_child;
    if (view) {
        if (view_span->font) {
            setup_font(rdcon->ui_context, &rdcon->font, view_span->font);
        }
        if (view_span->in_line && view_span->in_line->color.c) {
            log_debug("[RENDER COLOR INLINE] element=%s setting color: #%02x%02x%02x (was #%02x%02x%02x) color.c=0x%08x",
                      view_span->node_name(),
                      view_span->in_line->color.r, view_span->in_line->color.g, view_span->in_line->color.b,
                      pa_color.r, pa_color.g, pa_color.b,
                      view_span->in_line->color.c);
            rdcon->color = view_span->in_line->color;
        } else {
            log_debug("[RENDER COLOR INLINE] element=%s inheriting color #%02x%02x%02x (in_line=%p, color.c=%u)",
                      view_span->node_name(), pa_color.r, pa_color.g, pa_color.b,
                      view_span->in_line, view_span->in_line ? view_span->in_line->color.c : 0);
        }
        render_children(rdcon, view);
    }
    else {
        log_debug("view has no child");
    }
    rdcon->font = pa_font;  rdcon->color = pa_color;
}

void render_children(RenderContext* rdcon, View* view) {
    do {
        if (view->view_type == RDT_VIEW_BLOCK || view->view_type == RDT_VIEW_INLINE_BLOCK ||
            view->view_type == RDT_VIEW_TABLE || view->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
            view->view_type == RDT_VIEW_TABLE_ROW || view->view_type == RDT_VIEW_TABLE_CELL) {
            ViewBlock* block = (ViewBlock*)view;
            log_debug("[RENDER DISPATCH] view_type=%d, embed=%p, img=%p, width=%.0f, height=%.0f",
                      view->view_type, block->embed,
                      block->embed ? block->embed->img : NULL, block->width, block->height);
            if (block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form) {
                // Form control rendering (input, select, textarea, button)
                // First render the block (background, borders, children) then form-specific decoration
                log_debug("[RENDER DISPATCH] calling render_block_view for form control");
                render_block_view(rdcon, block);
                // Now render form-specific decorations (checkboxes, radio buttons, etc.)
                log_debug("[RENDER DISPATCH] calling render_form_control");
                render_form_control(rdcon, block);
            }
            else if (block->embed && block->embed->img) {
                log_debug("[RENDER DISPATCH] calling render_image_view");
                render_image_view(rdcon, block);
            }
            else if (block->embed && block->embed->doc) {
                render_embed_doc(rdcon, block);
            }
            else if (block->blk && block->blk->list_style_type) {
                render_list_view(rdcon, block);
            }
            else {
                // Skip only absolute/fixed positioned elements - they are rendered separately
                // Floats (which also have position struct) should be rendered in normal flow
                if (block->position &&
                    (block->position->position == CSS_VALUE_ABSOLUTE ||
                     block->position->position == CSS_VALUE_FIXED)) {
                    log_debug("absolute/fixed positioned block, skip in normal rendering");
                } else {
                    render_block_view(rdcon, block);
                }
            }
        }
        else if (view->view_type == RDT_VIEW_LIST_ITEM) {
            render_litem_view(rdcon, (ViewBlock*)view);
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            render_inline_view(rdcon, span);
        }
        else if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            render_text_view(rdcon, text);
        }
        else if (view->view_type == RDT_VIEW_MARKER) {
            // List marker (bullet/number) with fixed width and vector graphics
            ViewSpan* marker = (ViewSpan*)view;
            render_marker_view(rdcon, marker);
        }
        else if (view->view_type == RDT_VIEW_MATH) {
            // Math view - renders MathBox trees from DomElement's embed prop
            DomElement* elem = static_cast<DomElement*>(view);
            radiant::render_math_from_embed(rdcon, elem);
        }
        else {
            log_debug("unknown view in rendering: %d", view->view_type);
        }
        view = view->next();
    } while (view);
}

// ============================================================================
// Focus, Caret, and Selection Rendering
// ============================================================================

/**
 * Render focus outline around the currently focused element
 * Draws a 2px dotted outline outside the element's border box
 */
void render_focus_outline(RenderContext* rdcon, RadiantState* state) {
    if (!state || !state->focus || !state->focus->current) return;
    
    // Only render focus-visible (keyboard navigation)
    if (!state->focus->focus_visible) return;
    
    View* focused = state->focus->current;
    if (focused->view_type != RDT_VIEW_BLOCK) return;
    
    ViewBlock* block = (ViewBlock*)focused;
    
    // Calculate absolute position of the focused element
    float x = block->x;
    float y = block->y;
    float width = block->width;
    float height = block->height;
    
    // Walk up the tree to get absolute coordinates
    View* parent = block->parent;
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK) {
            x += ((ViewBlock*)parent)->x;
            y += ((ViewBlock*)parent)->y;
        }
        parent = parent->parent;
    }
    
    // Outline offset (outside border box)
    float outline_offset = 2.0f;
    float outline_width = 2.0f;
    
    // Create outline shape
    Tvg_Paint* shape = tvg_shape_new();
    if (!shape) return;
    
    // Draw dotted rectangle outline
    float ox = x - outline_offset;
    float oy = y - outline_offset;
    float ow = width + outline_offset * 2;
    float oh = height + outline_offset * 2;
    
    tvg_shape_append_rect(shape, ox, oy, ow, oh, 0, 0);
    
    // Focus ring color: typically blue or system accent color
    // Using a standard web focus color: #005FCC (blue)
    tvg_shape_set_stroke_color(shape, 0x00, 0x5F, 0xCC, 0xFF);
    tvg_shape_set_stroke_width(shape, outline_width);
    
    // Dotted pattern: dash length 4, gap 2
    float dash_pattern[] = {4.0f, 2.0f};
    tvg_shape_set_stroke_dash(shape, dash_pattern, 2, 0);
    
    tvg_canvas_push(rdcon->canvas, shape);
    log_debug("[FOCUS] Rendered focus outline at (%.0f,%.0f) size %.0fx%.0f", ox, oy, ow, oh);
}

/**
 * Render the text caret (blinking cursor) in an editable element
 */
void render_caret(RenderContext* rdcon, RadiantState* state) {
    if (!state || !state->caret || !state->caret->visible) return;
    if (!state->caret->view) return;
    
    CaretState* caret = state->caret;
    View* view = caret->view;
    
    // Calculate absolute position
    float x = caret->x;
    float y = caret->y;
    
    // Walk up the tree to get absolute coordinates
    View* parent = view;
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK) {
            x += ((ViewBlock*)parent)->x;
            y += ((ViewBlock*)parent)->y;
        }
        parent = parent->parent;
    }
    
    // Create caret line shape
    Tvg_Paint* shape = tvg_shape_new();
    if (!shape) return;
    
    // Caret is a vertical line at x, from y to y+height
    tvg_shape_move_to(shape, x, y);
    tvg_shape_line_to(shape, x, y + caret->height);
    
    // Caret color: black
    tvg_shape_set_stroke_color(shape, 0x00, 0x00, 0x00, 0xFF);
    tvg_shape_set_stroke_width(shape, 1.5f);
    
    tvg_canvas_push(rdcon->canvas, shape);
    log_debug("[CARET] Rendered caret at (%.0f,%.0f) height=%.0f", x, y, caret->height);
}

/**
 * Render text selection highlight
 * Draws semi-transparent blue rectangles behind selected text
 */
void render_selection(RenderContext* rdcon, RadiantState* state) {
    if (!state || !state->selection) return;
    if (state->selection->is_collapsed) return;  // no selection
    if (!state->selection->view) return;
    
    SelectionState* sel = state->selection;
    View* view = sel->view;
    
    // For single-line selection, draw a single rectangle
    // Multi-line selection would require multiple rectangles per line
    
    // Calculate absolute position
    float start_x = sel->start_x;
    float start_y = sel->start_y;
    float end_x = sel->end_x;
    float end_y = sel->end_y;
    
    // Walk up to get absolute coordinates
    View* parent = view;
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* block = (ViewBlock*)parent;
            start_x += block->x;
            start_y += block->y;
            end_x += block->x;
            end_y += block->y;
        }
        parent = parent->parent;
    }
    
    // Normalize coordinates (anchor can be after focus)
    float min_x = start_x < end_x ? start_x : end_x;
    float max_x = start_x > end_x ? start_x : end_x;
    float min_y = start_y < end_y ? start_y : end_y;
    
    // For now, simple single-line selection rect
    float sel_width = max_x - min_x;
    float sel_height = end_y - start_y;  // Use line height approximation
    if (sel_height <= 0) sel_height = 20;  // default line height if not set
    
    // Create selection highlight shape
    Tvg_Paint* shape = tvg_shape_new();
    if (!shape) return;
    
    tvg_shape_append_rect(shape, min_x, min_y, sel_width, sel_height, 0, 0);
    
    // Selection highlight color: semi-transparent blue (standard selection blue)
    // #0078D7 at 50% opacity
    tvg_shape_set_fill_color(shape, 0x00, 0x78, 0xD7, 0x80);
    
    tvg_canvas_push(rdcon->canvas, shape);
    log_debug("[SELECTION] Rendered selection at (%.0f,%.0f) size %.0fx%.0f", min_x, min_y, sel_width, sel_height);
}

/**
 * Render all interactive state overlays (focus, caret, selection)
 * Called after main content rendering, before canvas sync
 */
void render_ui_overlays(RenderContext* rdcon, RadiantState* state) {
    if (!state) return;
    
    // Selection is rendered first (behind text/caret)
    render_selection(rdcon, state);
    
    // Caret rendered on top of selection
    render_caret(rdcon, state);
    
    // Focus outline rendered last (outside content)
    render_focus_outline(rdcon, state);
}

void render_init(RenderContext* rdcon, UiContext* uicon, ViewTree* view_tree) {
    memset(rdcon, 0, sizeof(RenderContext));
    rdcon->ui_context = uicon;
    rdcon->canvas = tvg_swcanvas_create();
    Tvg_Result result = tvg_swcanvas_set_target(rdcon->canvas, (uint32_t*)uicon->surface->pixels, uicon->surface->width,
        uicon->surface->width, uicon->surface->height, TVG_COLORSPACE_ABGR8888);
    if (result != TVG_RESULT_SUCCESS) {
        log_error("render_init: tvg_swcanvas_set_target failed with result=%d", result);
    }

    // Initialize transform state (identity matrix, not active)
    rdcon->transform = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    rdcon->has_transform = false;

    // load default font
    FontProp* default_font = view_tree->html_version == HTML5 ? &uicon->default_font : &uicon->legacy_default_font;
    log_debug("render_init default font: %s, html version: %d", default_font->family, view_tree->html_version);
    setup_font(uicon, &rdcon->font, default_font);
    rdcon->block.clip = (Bound){0, 0, (float)uicon->surface->width, (float)uicon->surface->height};
    // initialize default text color to opaque black (ABGR format: 0xFF000000)
    rdcon->color.c = 0xFF000000;
    log_debug("render_init clip: [%.0f, %.0f, %.0f, %.0f]", rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
}

void render_clean_up(RenderContext* rdcon) {
    tvg_canvas_destroy(rdcon->canvas);
}

/**
 * Get the canvas background color per CSS 2.1 spec section 14.2:
 * If the root element (html) has no background, propagate from body.
 * Returns white (0xFFFFFFFF) as default.
 */
static uint32_t get_canvas_background(View* root_view) {
    if (!root_view || root_view->view_type != RDT_VIEW_BLOCK) {
        return 0xFFFFFFFF;  // default white
    }

    ViewBlock* html_block = (ViewBlock*)root_view;

    // Check if html element has a background color
    bool html_has_bg = html_block->bound && html_block->bound->background &&
                       html_block->bound->background->color.a > 0;

    if (html_has_bg) {
        // HTML has background, use it for canvas
        return html_block->bound->background->color.c;
    }

    // HTML has no background, check for body element
    // Per CSS spec, propagate body's background to canvas
    View* child = html_block->first_child;
    while (child) {
        if (child->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* child_block = (ViewBlock*)child;
            const char* name = child_block->node_name();
            if (name && strcasecmp(name, "body") == 0) {
                // Found body element
                if (child_block->bound && child_block->bound->background &&
                    child_block->bound->background->color.a > 0) {
                    log_debug("[RENDER] Propagating body background #%08x to canvas",
                              child_block->bound->background->color.c);
                    return child_block->bound->background->color.c;
                }
                break;
            }
        }
        child = (View*)child->next_sibling;
    }

    return 0xFFFFFFFF;  // default white
}

void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    reset_render_stats();  // reset performance counters

    RenderContext rdcon;
    log_debug("Render HTML doc");
    render_init(&rdcon, uicon, view_tree);

    // Get canvas background color (may be propagated from body per CSS spec)
    uint32_t canvas_bg = get_canvas_background(view_tree->root);
    fill_surface_rect(rdcon.ui_context->surface, NULL, canvas_bg, &rdcon.block.clip);

    auto t_init = high_resolution_clock::now();

    View* root_view = view_tree->root;
    if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
        log_debug("Render root view");
        render_block_view(&rdcon, (ViewBlock*)root_view);
        // render positioned children
        if (((ViewBlock*)root_view)->position) {
            log_debug("render absolute/fixed positioned children of root view");
            ViewBlock* child_block = ((ViewBlock*)root_view)->position->first_abs_child;
            while (child_block) {
                render_block_view(&rdcon, child_block);
                child_block = child_block->position->next_abs_sibling;
            }
        }
    }
    else {
        log_error("Invalid root view");
    }

    auto t_render = high_resolution_clock::now();
    log_info("[TIMING] render_block_view: %.1fms", duration<double, std::milli>(t_render - t_init).count());
    log_render_stats();  // log detailed render statistics

    // Render UI overlays (focus outline, caret, selection) on top of content
    if (uicon->document && uicon->document->state) {
        render_ui_overlays(&rdcon, uicon->document->state);
    }

    // all shapes should already have been drawn to the canvas
    // tvg_canvas_draw(rdcon.canvas, false); // no clearing of the buffer
    tvg_canvas_sync(rdcon.canvas);  // wait for async draw operation to complete

    auto t_sync = high_resolution_clock::now();
    log_info("[TIMING] tvg_canvas_sync: %.1fms", duration<double, std::milli>(t_sync - t_render).count());

    // save the rendered surface to image file (PNG or JPEG based on extension)
    if (output_file) {
        const char* ext = strrchr(output_file, '.');
        if (ext && (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)) {
            save_surface_to_jpeg(rdcon.ui_context->surface, output_file, 85); // Default quality 85
        } else {
            save_surface_to_png(rdcon.ui_context->surface, output_file);
        }
    }

    auto t_save = high_resolution_clock::now();
    if (output_file) {
        log_info("[TIMING] save_to_file: %.1fms", duration<double, std::milli>(t_save - t_sync).count());
    }

    render_clean_up(&rdcon);
    if (uicon->document->state) {
        uicon->document->state->is_dirty = false;
    }

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] render_html_doc total: %.1fms", duration<double, std::milli>(t_end - t_start).count());
}
