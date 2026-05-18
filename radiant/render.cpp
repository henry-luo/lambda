#include "render.hpp"
#include "render_img.hpp"
#include "render_border.hpp"
#include "render_background.hpp"
#include "render_effects.hpp"
#include "render_clip.hpp"
#include "render_raster.hpp"
#include "render_state.hpp"
#include "render_profiler.hpp"
#include "render_glyph.hpp"
#include "render_text.hpp"
#include "render_output.hpp"
#include "render_selection.hpp"
#include "render_columns.hpp"
#include "render_svg_inline.hpp"
#include "layout.hpp"
#include "form_control.hpp"
#include "state_store.hpp"
#include "webview.h"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lib/avl_tree.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_value.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include <string.h>
#include <math.h>
#include <chrono>
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "lib/stb_image_write.h"

// Forward declaration for inline SVG rendering (defined in render_svg_inline.cpp)
void render_inline_svg(RenderContext* rdcon, ViewBlock* view);
// Forward declaration for SVG rasterization (defined later in this file)
void render_svg(ImageSurface* surface);

#define DEBUG_RENDER 0

/**
 * Reset canvas target and draw shapes to buffer.
 * This resets ThorVG's dirty region tracking to prevent black backgrounds
 * when rendering multiple shapes to the same frame buffer.
 *
 * ThorVG's smart rendering tracks "dirty regions" and clears them before
 * each draw. When we render multiple shapes to the same buffer within one
 * frame, this causes previously drawn content to be cleared to black.
 * Resetting the target sets fulldraw=true, which bypasses dirty region clearing.
 */
// CollapsedBorder struct is now defined in view.hpp

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
void render_video_content(RenderContext* rdcon, ViewBlock* view);
void render_webview_layer_content(RenderContext* rdcon, ViewBlock* view);
void scrollpane_render(RdtVector* vec, ScrollPane* sp, Rect* block_bound,
    float content_width, float content_height, Bound* clip, float scale,
    DocState* state, View* view, bool show_hz_scroll = true, bool show_vt_scroll = true);
void render_form_control(RenderContext* rdcon, ViewBlock* block);  // form controls
void render_select_dropdown(RenderContext* rdcon, ViewBlock* select, DocState* state);  // select dropdown popup

extern CssEnum get_white_space_value(DomNode* node);

void render_text_view(RenderContext* rdcon, ViewText* text_view) {
    log_debug("render_text_view clip:[%.0f,%.0f,%.0f,%.0f]",
        rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);

    // CSS 2.1 §11.2: text inherits visibility from parent element
    if (text_view->parent && text_view->parent->is_element()) {
        DomElement* parent_elem = lam::dom_require_element(text_view->parent);
        if (parent_elem->in_line && parent_elem->in_line->visibility == VIS_HIDDEN) {
            log_debug("text hidden by parent visibility:hidden");
            return;
        }
    }

    if (!rdcon->font.font_handle) {
        log_debug("font face is null");
        return;
    }

    float s = rdcon->scale;  // scale factor for CSS -> physical pixels
    unsigned char* str = text_view->text_data();
    TextRect* text_rect = text_view->rect;

    if (!text_rect) {
        log_debug("no text rect for text view");
        return;
    }

    // Legacy glyph-by-glyph selection code remains disabled.
    int sel_start = 0, sel_end = 0;
    (void)sel_start; (void)sel_end;

    // Calculate total text length for cross-view selection check
    int total_text_length = 0;
    if (str) {
        total_text_length = strlen((const char*)str);
    }
    (void)total_text_length;

    bool has_selection = false;

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
    bool text_align_found = false;
    DomNode* parent = text_view->parent;
    while (parent) {
        if (parent->is_element()) {
            DomElement* elem = lam::dom_require_element(parent);
            CssEnum transform = get_text_transform_from_block(elem->blk);
            if (transform != CSS_VALUE_NONE) {
                text_transform = transform;
            }
            // Get text-align from the nearest ancestor that has block properties.
            // text-align is CSS-inherited, so the closest block already holds the
            // resolved value; walking further would overwrite it with an outer
            // (e.g. <html>) default and lose 'justify'.
            if (!text_align_found && elem->blk) {
                BlockProp* blk_prop = (BlockProp*)elem->blk;
                text_align = blk_prop->text_align;
                text_align_found = true;
            }
            if (transform != CSS_VALUE_NONE) {
                break;
            }
        }
        parent = parent->parent;
    }

    DomElement* parent_elem = text_view->parent ? text_view->parent->as_element() : nullptr;

    // Get text-shadow from parent element's font property
    TextShadow* text_shadow = nullptr;
    if (parent_elem && parent_elem->font && parent_elem->font->text_shadow) {
        text_shadow = parent_elem->font->text_shadow;
    }

    while (text_rect) {
        // Apply scale to convert CSS pixel positions to physical surface pixels
        float x = rdcon->block.x + text_rect->x * s, y = rdcon->block.y + text_rect->y * s;

        render_text_inline_background(rdcon, text_view, text_rect, parent_elem, x, y);

        unsigned char* p = str + text_rect->start_index;  unsigned char* end = p + text_rect->length;
        log_debug("draw text:'%t', start:%d, len:%d, x:%f, y:%f, wd:%f, hg:%f, at (%f, %f), white_space:%d, preserve:%d, color:0x%08x",
            str, text_rect->start_index, text_rect->length, text_rect->x, text_rect->y, text_rect->width, text_rect->height, x, y,
            white_space, preserve_spaces, rdcon->color.c);

        // Calculate natural text width and space count for justify rendering
        // Note: space_width is in CSS pixels (scaled down for layout), need to scale up for render
        float scaled_space_width = rdcon->font.style->space_width * s;
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
                    natural_width += scaled_space_width;
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

                auto t1 = std::chrono::high_resolution_clock::now();
                FontStyleDesc _sd = font_style_desc_from_prop(rdcon->font.style);
                LoadedGlyph* glyph = font_load_glyph(rdcon->font.font_handle, &_sd, scan_codepoint, false);
                auto t2 = std::chrono::high_resolution_clock::now();
                render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_GLYPH_LOAD,
                    std::chrono::duration<double, std::milli>(t2 - t1).count());
                if (glyph) {
                    natural_width += glyph->advance_x + rdcon->font.style->letter_spacing * s;  // already in physical pixels
                } else {
                    natural_width += scaled_space_width;  // fallback width in physical pixels
                }
            }
        }

        // Calculate adjusted space width for justified text (in physical pixels)
        float space_width = scaled_space_width;
        if (text_align == CSS_VALUE_JUSTIFY && space_count > 0 && natural_width > 0 && text_rect->width * s > natural_width) {
            // This text is explicitly justified - distribute extra space across spaces
            float extra_space = (text_rect->width * s) - natural_width;
            space_width += (extra_space / space_count);
            log_debug("apply justification: text_align=JUSTIFY, natural_width=%f, text_rect->width=%f, space_count=%d, space_width=%f -> %f",
                natural_width, text_rect->width * s, space_count, scaled_space_width, space_width);
        }

        // Render the text with adjusted spacing
        bool has_space = false;  uint32_t codepoint;
        bool is_word_start = true;  // Track word boundaries for capitalize
        int char_index = text_rect->start_index;  // Track character offset for selection

        // Selection background color - standard blue for text selection
        uint32_t sel_bg_color = 0x80FF9933;  // ABGR format: semi-transparent blue (like browser selection)

        // Debug: log inline selection position info
        if (has_selection) {
            log_debug("[SEL-INLINE] text_rect: x=%.1f y=%.1f, rdcon->block: x=%.1f y=%.1f, final pos: x=%.1f y=%.1f, font_size=%.1f, y_ppem=%d",
                text_rect->x, text_rect->y, rdcon->block.x, rdcon->block.y, x, y,
                rdcon->font.style->font_size, (int)font_handle_get_physical_size_px(rdcon->font.font_handle));
        }

        // Track cumulative position for debugging
        float debug_start_x = x;

        bool shadow_needs_blur = render_text_paint_blurred_shadows(rdcon, str, text_rect,
            text_shadow, text_transform, preserve_spaces, space_width, scaled_space_width, x, y);

        while (p < end) {
            // Check if current character is in selection range
            bool is_selected = has_selection && char_index >= sel_start && char_index < sel_end;

            // Debug first selected character
            if (is_selected && char_index == sel_start) {
                log_debug("[SEL-INLINE] First selected char at index=%d, x=%.1f y=%.1f, advance_so_far=%.1f (expected overlay start_x=%.1f * scale=%.1f = %.1f)",
                    char_index, x, y, x - debug_start_x, 0.0f, s, 0.0f);
            }

            // log_debug("draw character '%c'", *p);
            if (is_space(*p)) {
                if (preserve_spaces || !has_space) {  // preserve all spaces or add single whitespace
                    has_space = true;

                    // Draw selection background for selected space
                    if (is_selected) {
                        Rect sel_rect = {x, y, space_width, text_rect->height * s};
                        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &sel_rect, sel_bg_color, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
                    }

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
                else { p += bytes;  char_index++; }

                // skip soft hyphen (U+00AD) — invisible unless line breaks there
                if (codepoint == 0x00AD) continue;

                // Apply text-transform before loading glyph
                uint32_t tt_out[3];
                int tt_count = apply_text_transform_full(codepoint, text_transform, is_word_start, tt_out);
                codepoint = tt_out[0];
                is_word_start = false;

                for (int tti = 0; tti < tt_count; tti++) {
                uint32_t render_cp = tt_out[tti];
                if (render_cp == 0) continue;

                // Debug: Log the font face being used for this glyph
                static int glyph_debug_count = 0;
                if (glyph_debug_count < 500) {
                    log_debug("[GLYPH DEBUG] loading glyph U+%04X from font '%s' (family=%s) y_ppem=%d css_size=%.2f",
                              codepoint,
                              rdcon->font.font_handle ? font_handle_get_family_name(rdcon->font.font_handle) : "NULL",
                              rdcon->font.style ? rdcon->font.style->family : "NULL",
                              rdcon->font.font_handle ? (int)font_handle_get_physical_size_px(rdcon->font.font_handle) : -1,
                              rdcon->font.style ? rdcon->font.style->font_size : -1.0f);
                    glyph_debug_count++;
                }

                LoadedGlyph* glyph = render_text_load_glyph_for_paint(rdcon, render_cp, p, end);
                if (!glyph) {
                    // draw a square box for missing glyph (scaled_space_width is in physical pixels)
                    float phys_size = font_handle_get_physical_size_px(rdcon->font.font_handle);
                    const FontMetrics* _m = font_get_metrics(rdcon->font.font_handle);
                    float box_height = (phys_size > 0) ? phys_size : (_m ? (_m->hhea_line_height * rdcon->scale / 1.2f) : 16.0f);
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
                        auto tfm1 = std::chrono::high_resolution_clock::now();
                        ascend = font_get_rendering_ascender(rdcon->font.font_handle) * rdcon->scale;
                        auto tfm2 = std::chrono::high_resolution_clock::now();
                        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_FONT_METRICS,
                            std::chrono::duration<double, std::milli>(tfm2 - tfm1).count());
                    }
                    if (has_selection && char_index <= 15) {
                        log_debug("[SEL-ADVANCE] char_index=%d codepoint=U+%04X '%c' x=%.1f advance=%.1f",
                            char_index, codepoint, (codepoint >= 32 && codepoint < 127) ? (char)codepoint : '?',
                            x, glyph->advance_x);
                    }

                    // Draw selection background BEFORE glyph (so text appears on top)
                    if (is_selected) {
                        float glyph_width = glyph->advance_x;
                        Rect sel_rect = {x, y, glyph_width, text_rect->height * s};
                        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &sel_rect, sel_bg_color, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
                    }

                    // Debug: Check bitmap data for Monaco (capped to avoid log spam)
                    static int bitmap_debug_count = 0;
                    const char* _dbg_fname = rdcon->font.font_handle ? font_handle_get_family_name(rdcon->font.font_handle) : NULL;
                    if (bitmap_debug_count < 50 && _dbg_fname &&
                        strcmp(_dbg_fname, "Monaco") == 0) {
                        log_debug("[BITMAP DEBUG] Monaco glyph U+%04X: bitmap=%dx%d pitch=%d left=%d top=%d advance=%.1f pixel_mode=%d",
                                  codepoint, glyph->bitmap.width, glyph->bitmap.height,
                                  glyph->bitmap.pitch, glyph->bitmap.bearing_x, glyph->bitmap.bearing_y,
                                  glyph->advance_x, glyph->bitmap.pixel_mode);
                        bitmap_debug_count++;
                    }

                    auto t3 = std::chrono::high_resolution_clock::now();

                    // Render text-shadow glyphs BEFORE the main glyph
                    // Skip if blur pre-pass already rendered shadows
                    if (text_shadow && !shadow_needs_blur) {
                        render_text_paint_glyph_shadows(rdcon, glyph, text_shadow, x, y, ascend);
                    }

                    draw_glyph(rdcon, &glyph->bitmap, lroundf(x + glyph->bitmap.bearing_x), lroundf(y + ascend - glyph->bitmap.bearing_y));
                    auto t4 = std::chrono::high_resolution_clock::now();
                    render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_GLYPH_DRAW,
                        std::chrono::duration<double, std::milli>(t4 - t3).count());
                    // advance to the next position (include letter-spacing)
                    x += glyph->advance_x + rdcon->font.style->letter_spacing * s;
                }
                } // end for tti (full case mapping expansion)
            }
        }
        x = render_text_trailing_marks(rdcon, text_rect, x, y);
        render_text_decorations(rdcon, str, text_rect);
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

    DomElement* elem = lam::dom_require_element(lam::view_dom_node(marker));
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

    // CSS 2.1 §12.5: list-style-image overrides list-style-type
    if (marker_prop->image_url && strcmp(marker_prop->image_url, "none") != 0) {
        // Lazy-load the image on first render
        if (!marker_prop->loaded_image) {
            marker_prop->loaded_image = load_image(rdcon->ui_context, marker_prop->image_url);
            if (marker_prop->loaded_image && marker_prop->loaded_image->pic) {
                // SVG: set rasterization size based on intrinsic dimensions
                float iw, ih;
                rdt_picture_get_size(marker_prop->loaded_image->pic, &iw, &ih);
                if (iw > 0 && ih > 0) {
                    marker_prop->loaded_image->max_render_width = (int)(iw + 0.5f); // INT_CAST_OK: pixel rounding
                }
                render_svg(marker_prop->loaded_image);
            }
        }
        ImageSurface* img = marker_prop->loaded_image;
        if (img && img->pixels && img->width > 0 && img->height > 0) {
            // marker images are typically rendered at intrinsic size; request natural decode
            image_surface_ensure_decoded(img, img->width, img->height);
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float img_w = (float)img->width;
            float img_h = (float)img->height;
            // Position image: centered at ~1em from content edge, vertically centered in line
            float ix = x + width - font_size - img_w / 2.0f;
            float iy = y + marker->height / 2.0f - img_h / 2.0f;
            rc_draw_image(rdcon, (uint32_t*)img->pixels, img->width, img->height,
                          img->width, ix, iy, img_w, img_h, 255, nullptr);
            log_debug("[MARKER RENDER] Drew list-style-image at (%.1f, %.1f) size %.0fx%.0f",
                     ix, iy, img_w, img_h);
            return;  // image replaces the marker type
        }
        // Fallback: if image failed to load, fall through to marker_type rendering
        log_debug("[MARKER RENDER] list-style-image failed to load, falling back to marker_type");
    }

    switch (marker_type) {
        case CSS_VALUE_DISC: {
            // Filled circle - centered at ~1em from content edge, vertically centered in line
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;
            float radius = bullet_size / 2.0f;

            // Draw filled circle using RdtVector
            RdtPath* p = rdt_path_new();
            rdt_path_add_circle(p, cx, cy, radius, radius);
            rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, NULL);
            rdt_path_free(p);
            log_debug("[MARKER RENDER] Drew disc at (%.1f, %.1f) r=%.1f", cx, cy, radius);
            break;
        }

        case CSS_VALUE_CIRCLE: {
            // Stroked circle (outline only)
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;
            float radius = bullet_size / 2.0f;
            float stroke_width = 1.0f;

            RdtPath* p = rdt_path_new();
            rdt_path_add_circle(p, cx, cy, radius - stroke_width/2, radius - stroke_width/2);
            rc_stroke_path(rdcon, p, color, stroke_width, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, NULL);
            rdt_path_free(p);
            log_debug("[MARKER RENDER] Drew circle outline at (%.1f, %.1f) r=%.1f", cx, cy, radius);
            break;
        }

        case CSS_VALUE_SQUARE: {
            // Filled square - centered at ~1em from content edge, vertically centered in line
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;
            float sx = cx - bullet_size / 2.0f;
            float sy = cy - bullet_size / 2.0f;

            rc_fill_rect(rdcon, sx, sy, bullet_size, bullet_size, color);
            log_debug("[MARKER RENDER] Drew square at (%.1f, %.1f) size=%.1f", sx, sy, bullet_size);
            break;
        }

        case CSS_VALUE_DISCLOSURE_CLOSED: {
            // Right-pointing triangle ▸ for <summary> elements
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float tri_size = bullet_size * 1.6f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;

            RdtPath* p = rdt_path_new();
            // right-pointing triangle: left edge at cx, top at cy - tri_size/2, bottom at cy + tri_size/2
            rdt_path_move_to(p, cx, cy - tri_size / 2.0f);
            rdt_path_line_to(p, cx + tri_size, cy);
            rdt_path_line_to(p, cx, cy + tri_size / 2.0f);
            rdt_path_close(p);
            rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, NULL);
            rdt_path_free(p);
            log_debug("[MARKER RENDER] Drew disclosure-closed triangle at (%.1f, %.1f)", cx, cy);
            break;
        }

        case CSS_VALUE_DISCLOSURE_OPEN: {
            // Down-pointing triangle ▾ for open <details> elements
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float tri_size = bullet_size * 1.6f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;

            RdtPath* p = rdt_path_new();
            // down-pointing triangle
            rdt_path_move_to(p, cx - tri_size / 2.0f, cy - tri_size / 2.0f);
            rdt_path_line_to(p, cx + tri_size / 2.0f, cy - tri_size / 2.0f);
            rdt_path_line_to(p, cx, cy + tri_size / 2.0f);
            rdt_path_close(p);
            rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, NULL);
            rdt_path_free(p);
            log_debug("[MARKER RENDER] Drew disclosure-open triangle at (%.1f, %.1f)", cx, cy);
            break;
        }

        case CSS_VALUE_DECIMAL:
        case CSS_VALUE_DECIMAL_LEADING_ZERO:
        case CSS_VALUE_LOWER_ROMAN:
        case CSS_VALUE_UPPER_ROMAN:
        case CSS_VALUE_LOWER_ALPHA:
        case CSS_VALUE_UPPER_ALPHA:
        case CSS_VALUE_LOWER_LATIN:
        case CSS_VALUE_UPPER_LATIN:
        case CSS_VALUE_LOWER_GREEK:
        case CSS_VALUE_ARMENIAN:
        case CSS_VALUE_GEORGIAN: {
            // Text markers - render the text_content right-aligned within marker width
            if (marker_prop->text_content && *marker_prop->text_content && rdcon->font.font_handle) {
                float s = rdcon->scale;
                const FontMetrics* _mk = font_get_metrics(rdcon->font.font_handle);
                float ascend = _mk ? (_mk->hhea_ascender * s) : 12.0f;

                // First pass: measure total text width
                float total_text_width = 0.0f;
                const char* p = marker_prop->text_content;
                while (*p) {
                    uint32_t cp;
                    int bytes = str_utf8_decode(p, strlen(p), &cp);
                    if (bytes <= 0) { p++; continue; }
                    p += bytes;
                    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);
                    LoadedGlyph* glyph = font_load_glyph(rdcon->font.font_handle, &sd, cp, false);
                    total_text_width += glyph ? glyph->advance_x + rdcon->font.style->letter_spacing * s : (rdcon->font.style->space_width * s);
                }

                // Right-align text within marker box: start at (x + width - total_text_width)
                float tx = x + (width * s) - total_text_width;

                // Second pass: render glyphs
                p = marker_prop->text_content;
                while (*p) {
                    uint32_t cp;
                    int bytes = str_utf8_decode(p, strlen(p), &cp);
                    if (bytes <= 0) { p++; continue; }
                    p += bytes;

                    if (cp == ' ') {
                        tx += rdcon->font.style->space_width * s;
                        continue;
                    }

                    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);
                    LoadedGlyph* glyph = font_load_glyph(rdcon->font.font_handle, &sd, cp, true);
                    if (glyph) {
                        draw_glyph(rdcon, &glyph->bitmap, lroundf(tx + glyph->bitmap.bearing_x), lroundf(y + ascend - glyph->bitmap.bearing_y));
                        tx += glyph->advance_x + rdcon->font.style->letter_spacing * s;
                    } else {
                        tx += rdcon->font.style->space_width * s;
                    }
                }

                log_debug("[MARKER RENDER] Text marker: '%s' at x=%.1f y=%.1f w=%.1f",
                         marker_prop->text_content, x, y, width);
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
 * Uses RdtVector to render Bezier curves and path segments
 */
void render_vector_path(RenderContext* rdcon, ViewBlock* block) {
    VectorPathProp* vpath = block->vpath;
    if (!vpath || !vpath->segments) return;

    log_info("[VPATH] Rendering vector path for block at (%.1f, %.1f)", block->x, block->y);

    RdtPath* p = rdt_path_new();
    if (!p) {
        log_error("[VPATH] Failed to create RdtPath");
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
                rdt_path_move_to(p, sx, sy);
                log_debug("[VPATH] moveto (%.1f, %.1f)", sx, sy);
                break;
            case VectorPathSegment::VPATH_LINETO:
                rdt_path_line_to(p, sx, sy);
                log_debug("[VPATH] lineto (%.1f, %.1f)", sx, sy);
                break;
            case VectorPathSegment::VPATH_CURVETO: {
                float cx1 = offset_x + seg->x1;
                float cy1 = offset_y + seg->y1;
                float cx2 = offset_x + seg->x2;
                float cy2 = offset_y + seg->y2;
                rdt_path_cubic_to(p, cx1, cy1, cx2, cy2, sx, sy);
                log_debug("[VPATH] curveto (%.1f,%.1f)-(%.1f,%.1f)->(%.1f,%.1f)", cx1, cy1, cx2, cy2, sx, sy);
                break;
            }
            case VectorPathSegment::VPATH_CLOSE:
                rdt_path_close(p);
                log_debug("[VPATH] close");
                break;
        }
    }

    // Apply stroke if present
    if (vpath->has_stroke) {
        rc_stroke_path(rdcon, p, vpath->stroke_color, vpath->stroke_width,
                        RDT_CAP_BUTT, RDT_JOIN_MITER,
                        vpath->dash_pattern, vpath->dash_pattern_length > 0 ? vpath->dash_pattern_length : 0,
                        NULL);

        if (vpath->dash_pattern && vpath->dash_pattern_length > 0) {
            log_debug("[VPATH] Setting dash pattern: count=%d, values=[%.1f, %.1f]",
                     vpath->dash_pattern_length,
                     vpath->dash_pattern[0],
                     vpath->dash_pattern_length > 1 ? vpath->dash_pattern[1] : 0.0f);
        }

        log_debug("[VPATH] Stroke: RGB(%d,%d,%d) width=%.1f",
                 vpath->stroke_color.r, vpath->stroke_color.g, vpath->stroke_color.b, vpath->stroke_width);
    }

    // Apply fill if present
    if (vpath->has_fill) {
        rc_fill_path(rdcon, p, vpath->fill_color, RDT_FILL_WINDING, NULL);
    }

    rdt_path_free(p);
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
        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, rdcon->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
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
        // float font_size = rdcon->font.current_font_size;
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
    ViewBlock* list = lam::view_require_block(view);
    log_debug("view list:%s", list->node_name());
    ListBlot pa_list = rdcon->list;
    rdcon->list.item_index = 0;  rdcon->list.list_style_type = list->blk->list_style_type;
    render_block_view(rdcon, list);
    rdcon->list = pa_list;
}

// Helper function to render linear gradient
void render_bound(RenderContext* rdcon, ViewBlock* view) {
    float s = rdcon->scale;
    Rect rect;
    rect.x = rdcon->block.x + view->x * s;  rect.y = rdcon->block.y + view->y * s;
    rect.width = view->width * s;  rect.height = view->height * s;

    // Resolve percentage border-radius values against element's own dimensions (in CSS px)
    if (view->bound->border) {
        resolve_border_radius_percentages(&view->bound->border->radius, view->width, view->height);
    }

    // Render box-shadow BEFORE background (shadows go underneath the element)
    if (view->bound->box_shadow) {
        render_box_shadow(rdcon, view, rect);
    }

    // Render background (gradient, solid color, and background-image) using new rendering system
    if (view->bound->background) {
        render_background(rdcon, view, rect);
    }

    // Render inset box-shadow AFTER background (inside the element)
    if (view->bound->box_shadow) {
        render_box_shadow_inset(rdcon, view, rect);
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
            ViewTableCell* cell = lam::view_require_table_cell(view);
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
            // CSS 2.1 §17.6.2: Collapsed borders are centered on the cell edges.
            // Since cells are positioned half-border inward, we must shift borders
            // outward by half their width to center them on the cell edge.
            if (resolved_left && resolved_left->style != CSS_VALUE_NONE && resolved_left->color.a) {
                float bw = resolved_left->width * s;
                Rect border_rect = rect;
                border_rect.x = rect.x - bw / 2.0f;
                border_rect.width = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_left->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_right && resolved_right->style != CSS_VALUE_NONE && resolved_right->color.a) {
                float bw = resolved_right->width * s;
                Rect border_rect = rect;
                border_rect.x = rect.x + rect.width - bw / 2.0f;
                border_rect.width = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_right->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_top && resolved_top->style != CSS_VALUE_NONE && resolved_top->color.a) {
                float bw = resolved_top->width * s;
                Rect border_rect = rect;
                border_rect.y = rect.y - bw / 2.0f;
                border_rect.height = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_top->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_bottom && resolved_bottom->style != CSS_VALUE_NONE && resolved_bottom->color.a) {
                float bw = resolved_bottom->width * s;
                Rect border_rect = rect;
                border_rect.y = rect.y + rect.height - bw / 2.0f;
                border_rect.height = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_bottom->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
        } else {
            // Use new comprehensive border rendering
            render_border(rdcon, view, rect);
        }
    }

    // Note: outline is NOT rendered here. Per CSS spec (Appendix E),
    // outlines paint after all backgrounds/borders/shadows in the stacking context.
    // Outlines are rendered in a second pass by the parent's render loop.
}

/**
 * Render outline for a view block (called in a deferred pass after all siblings).
 */
void render_outline_deferred(RenderContext* rdcon, ViewBlock* view) {
    if (!view->bound || !view->bound->outline) return;
    float s = rdcon->scale;
    BlockBlot saved = rdcon->block;
    // Compute absolute position for this view
    rdcon->block.x = saved.x + view->x * s;
    rdcon->block.y = saved.y + view->y * s;
    Rect rect;
    rect.x = rdcon->block.x;  rect.y = rdcon->block.y;
    rect.width = view->width * s;  rect.height = view->height * s;
    if (view->bound->border) {
        resolve_border_radius_percentages(&view->bound->border->radius, view->width, view->height);
    }
    render_outline(rdcon, view, rect);
    rdcon->block = saved;
}

void draw_debug_rect(RenderContext* rdcon, Rect rect, Bound* clip) {
    RdtPath* p = rdt_path_new();
    rdt_path_move_to(p, rect.x, rect.y);
    rdt_path_line_to(p, rect.x + rect.width, rect.y);
    rdt_path_line_to(p, rect.x + rect.width, rect.y + rect.height);
    rdt_path_line_to(p, rect.x, rect.y + rect.height);
    rdt_path_close(p);

    // dash pattern for dotted line
    float dash_pattern[2] = {8.0f, 8.0f};
    Color debug_color;
    debug_color.r = 255; debug_color.g = 0; debug_color.b = 0; debug_color.a = 100;

    // clip region
    RdtPath* clip_p = rdt_path_new();
    rdt_path_add_rect(clip_p, clip->left, clip->top,
                      clip->right - clip->left, clip->bottom - clip->top, 0, 0);
    rc_push_clip(rdcon, clip_p, NULL);

    rc_stroke_path(rdcon, p, debug_color, 2.0f, RDT_CAP_BUTT, RDT_JOIN_MITER, dash_pattern, 2, NULL);

    rc_pop_clip(rdcon);
    rdt_path_free(clip_p);
    rdt_path_free(p);
}

void setup_scroller(RenderContext* rdcon, ViewBlock* block) {
    float s = rdcon->scale;
    if (block->scroller->has_clip) {
        // Inset clip by border widths for padding-box clipping (CSS spec: overflow clips to padding edge)
        float bl = 0, bt = 0, br = 0, bb = 0;
        if (block->bound && block->bound->border) {
            BorderProp* border = block->bound->border;
            bl = border->width.left;
            bt = border->width.top;
            br = border->width.right;
            bb = border->width.bottom;
        }
        log_debug("setup scroller clip: left:%f, top:%f, right:%f, bottom:%f",
            block->scroller->clip.left, block->scroller->clip.top, block->scroller->clip.right, block->scroller->clip.bottom);
        rdcon->block.clip.left = max(rdcon->block.clip.left, rdcon->block.x + (block->scroller->clip.left + bl) * s);
        rdcon->block.clip.top = max(rdcon->block.clip.top, rdcon->block.y + (block->scroller->clip.top + bt) * s);
        rdcon->block.clip.right = min(rdcon->block.clip.right, rdcon->block.x + (block->scroller->clip.right - br) * s);
        rdcon->block.clip.bottom = min(rdcon->block.clip.bottom, rdcon->block.y + (block->scroller->clip.bottom - bb) * s);

        // Copy border-radius for rounded corner clipping when overflow:hidden (scale radius)
        if (block->bound && block->bound->border) {
            BorderProp* border = block->bound->border;
            // resolve percentage border-radius if not yet resolved
            resolve_border_radius_percentages(&border->radius, block->width, block->height);
            if (corner_has_radius(&border->radius)) {
                rdcon->block.has_clip_radius = true;
                // Use inner radius (outer minus border width) for padding-box clipping
                rdcon->block.clip_radius.top_left = fmaxf(0, border->radius.top_left - bl) * s;
                rdcon->block.clip_radius.top_right = fmaxf(0, border->radius.top_right - br) * s;
                rdcon->block.clip_radius.bottom_left = fmaxf(0, border->radius.bottom_left - bl) * s;
                rdcon->block.clip_radius.bottom_right = fmaxf(0, border->radius.bottom_right - br) * s;
                rdcon->block.clip_radius.top_left_y = fmaxf(0, border->radius.top_left_y - bt) * s;
                rdcon->block.clip_radius.top_right_y = fmaxf(0, border->radius.top_right_y - bt) * s;
                rdcon->block.clip_radius.bottom_left_y = fmaxf(0, border->radius.bottom_left_y - bb) * s;
                rdcon->block.clip_radius.bottom_right_y = fmaxf(0, border->radius.bottom_right_y - bb) * s;
                constrain_corner_radii(&rdcon->block.clip_radius,
                    rdcon->block.clip.right - rdcon->block.clip.left,
                    rdcon->block.clip.bottom - rdcon->block.clip.top);
                log_debug("setup rounded clip: tl=%f, tr=%f, bl=%f, br=%f",
                    rdcon->block.clip_radius.top_left, rdcon->block.clip_radius.top_right,
                    rdcon->block.clip_radius.bottom_left, rdcon->block.clip_radius.bottom_right);
            }
        }
    }
    if (block->scroller->pane) {
        DocState* state = block->doc ? block->doc->state : NULL;
        float scroll_x = 0.0f, scroll_y = 0.0f;
        scroll_state_get_position_for_view(state, static_cast<View*>(block), block->scroller->pane,
                                           &scroll_x, &scroll_y, NULL, NULL);
        rdcon->block.x -= scroll_x * s;
        rdcon->block.y -= scroll_y * s;
    }
}

void render_scroller(RenderContext* rdcon, ViewBlock* block, BlockBlot* pa_block) {
    log_debug("render scrollbars");
    // need to reset block.x and y, which was changed by the scroller
    float s = rdcon->scale;
    rdcon->block.x = pa_block->x + block->x * s;  rdcon->block.y = pa_block->y + block->y * s;
    if (block->scroller->has_hz_scroll || block->scroller->has_vt_scroll) {
        Rect rect = {rdcon->block.x, rdcon->block.y, block->width * s, block->height * s};
        if (block->bound && block->bound->border) {
            rect.x += block->bound->border->width.left * s;
            rect.y += block->bound->border->width.top * s;
            rect.width -= (block->bound->border->width.left + block->bound->border->width.right) * s;
            rect.height -= (block->bound->border->width.top + block->bound->border->width.bottom) * s;
        }
        if (block->scroller->pane) {
            DocState* state = block->doc ? block->doc->state : NULL;
            scrollpane_render(&rdcon->vec, block->scroller->pane, &rect,
                block->content_width * s, block->content_height * s, &rdcon->block.clip, s,
                state, static_cast<View*>(block),
                block->scroller->has_hz_scroll, block->scroller->has_vt_scroll);
        } else {
            log_error("scroller has no scroll pane");
        }
    }
}

void render_block_view(RenderContext* rdcon, ViewBlock* block) {
    auto rbv_start = std::chrono::high_resolution_clock::now();
    double children_time = 0; // will accumulate time in child render calls
    render_profiler_increment(rdcon->profiler, RENDER_PROFILE_BLOCK);
    // Phase 18: Early exit if block is entirely outside the dirty union bbox.
    // Must use the union bbox (not individual rects) because the display list
    // replay clips to the union — parent backgrounds paint the whole union,
    // so all blocks within it must be re-rendered.
    if (rdcon->has_dirty_union) {
        float s = rdcon->scale > 0 ? rdcon->scale : 1.0f;
        float abs_x = rdcon->block.x / s + block->x;
        float abs_y = rdcon->block.y / s + block->y;
        float bw = block->width;
        float bh = block->height;
        // add margin to avoid clipping positioned descendants that overflow
        float margin = 50.0f;
        abs_x -= margin; abs_y -= margin; bw += margin * 2; bh += margin * 2;
        Bound* du = &rdcon->dirty_union;
        bool intersects = (abs_x < du->right && abs_x + bw > du->left &&
                           abs_y < du->bottom && abs_y + bh > du->top);
        if (!intersects) return;
    }

    log_debug("render block view:%s, clip:[%.0f,%.0f,%.0f,%.0f]", block->node_name(),
        rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
    log_enter();
    BlockBlot pa_block = rdcon->block;  FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;

    // CSS 2.1 §11.2: visibility:hidden — suppress own rendering but still render children
    // (children with visibility:visible should still appear)
    bool self_hidden = block->in_line && block->in_line->visibility == VIS_HIDDEN;

    RenderTransformScope transform_scope = render_state_push_transform(rdcon, block, &pa_block);

    if (block->font) {
        auto t1 = std::chrono::high_resolution_clock::now();
        setup_font(rdcon->ui_context, &rdcon->font, block->font);
        auto t2 = std::chrono::high_resolution_clock::now();
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_SETUP_FONT,
            std::chrono::duration<double, std::milli>(t2 - t1).count());
    }
    // render bullet after setting the font, as bullet is rendered using the same font as the list item
    // Skip legacy render_list_bullet when a ::marker pseudo-element exists,
    // since render_marker_view will handle it during child traversal.
    if (!self_hidden && block->view_type == RDT_VIEW_LIST_ITEM) {
        DomElement* li_elem = lam::dom_require_element(lam::view_dom_node(block));
        if (!li_elem->pseudo || !li_elem->pseudo->marker) {
            render_list_bullet(rdcon, block);
        }
    }
    RenderClipScope css_clip_scope = render_clip_push_css_scope(rdcon, block,
        pa_block.x, pa_block.y, rdcon->scale);

    RenderEffectGroup effect_group = render_effect_group_begin(rdcon, block, &pa_block);

    if (!self_hidden && block->bound) {
        // CSS 2.1 Section 17.6.1: empty-cells: hide suppresses borders/backgrounds
        bool skip_bound = false;
        if (block->view_type == RDT_VIEW_TABLE_CELL) {
            ViewTableCell* cell = lam::view_require_table_cell(block);
            if (cell->td && cell->td->hide_empty) {
                skip_bound = true;
                log_debug("Skipping bound for empty cell (empty-cells: hide)");
            }
        }

        if (!skip_bound) {
            auto tb1 = std::chrono::high_resolution_clock::now();
            render_bound(rdcon, block);
            auto tb2 = std::chrono::high_resolution_clock::now();
            render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_BOUND,
                std::chrono::duration<double, std::milli>(tb2 - tb1).count());
        }
    }

    // Render vector path if present (for PDF curves and complex paths)
    if (block->vpath && block->vpath->segments) {
        render_vector_path(rdcon, block);
    }

    // Propagate position with scale applied (CSS logical pixels -> physical surface pixels)
    float s = rdcon->scale;
    rdcon->block.x = pa_block.x + block->x * s;  rdcon->block.y = pa_block.y + block->y * s;
    if (DEBUG_RENDER) {  // debugging outline around the block margin border
        Rect rc;
        rc.x = rdcon->block.x - (block->bound ? block->bound->margin.left * s : 0);
        rc.y = rdcon->block.y - (block->bound ? block->bound->margin.top * s : 0);
        rc.width = block->width * s + (block->bound ? (block->bound->margin.left + block->bound->margin.right) * s : 0);
        rc.height = block->height * s + (block->bound ? (block->bound->margin.top + block->bound->margin.bottom) * s : 0);
        draw_debug_rect(rdcon, rc, &rdcon->block.clip);
    }

    View* view = block->first_child;
    auto rc_start = std::chrono::high_resolution_clock::now();
    if (view) {
        if (block->in_line && block->in_line->has_color) {
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

        RenderClipScope overflow_clip_scope = render_clip_push_overflow_scope(rdcon);

        // render negative z-index children
        render_children(rdcon, view);
        // render positive z-index children (sorted by z-index)
        if (block->position) {
            log_debug("render absolute/fixed positioned children");
            // collect positioned children into array for z-index sorting
            ViewBlock* abs_children[256];
            int abs_count = 0;
            ViewBlock* child_block = block->position->first_abs_child;
            while (child_block && abs_count < 256) {
                abs_children[abs_count++] = child_block;
                child_block = child_block->position->next_abs_sibling;
            }
            // sort by z-index (stable: preserve document order for equal z-index)
            for (int i = 1; i < abs_count; i++) {
                ViewBlock* key = abs_children[i];
                int key_z = key->position ? key->position->z_index : 0;
                int j = i - 1;
                while (j >= 0) {
                    int j_z = abs_children[j]->position ? abs_children[j]->position->z_index : 0;
                    if (j_z > key_z) {
                        abs_children[j + 1] = abs_children[j];
                        j--;
                    } else {
                        break;
                    }
                }
                abs_children[j + 1] = key;
            }
            for (int i = 0; i < abs_count; i++) {
                render_block_view(rdcon, abs_children[i]);
            }
        }

        if (overflow_clip_scope.active) {
            auto toc1 = std::chrono::high_resolution_clock::now();
            render_clip_pop_scope(rdcon, &overflow_clip_scope);
            auto toc2 = std::chrono::high_resolution_clock::now();
            render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_OVERFLOW_CLIP,
                std::chrono::duration<double, std::milli>(toc2 - toc1).count());
        }

        // Deferred outline pass: CSS spec says outlines paint after all
        // backgrounds/borders/shadows in the stacking context. Walk direct
        // children and render their outlines on top of everything.
        View* outline_view = block->first_child;
        while (outline_view) {
            if (outline_view->view_type == RDT_VIEW_BLOCK ||
                outline_view->view_type == RDT_VIEW_INLINE_BLOCK) {
                ViewBlock* outline_block = lam::view_require_block(outline_view);
                if (outline_block->bound && outline_block->bound->outline) {
                    render_outline_deferred(rdcon, outline_block);
                }
            }
            outline_view = static_cast<View*>(outline_view->next_sibling);
        }
    }
    else {
        log_debug("view has no child");
    }
    auto rc_end = std::chrono::high_resolution_clock::now();
    children_time = std::chrono::duration<double, std::milli>(rc_end - rc_start).count();

    // render scrollbars
    if (block->scroller) {
        render_scroller(rdcon, block, &pa_block);
    }

    // Render multi-column rules between columns
    if (block->multicol && block->multicol->computed_column_count > 1) {
        render_column_rules(rdcon, block);
    }

    render_effect_group_finish(&effect_group, block, &rdcon->block.clip);

    if (css_clip_scope.active) {
        auto tc1 = std::chrono::high_resolution_clock::now();
        render_clip_pop_scope(rdcon, &css_clip_scope);
        auto tc2 = std::chrono::high_resolution_clock::now();
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_CLIP,
            std::chrono::duration<double, std::milli>(tc2 - tc1).count());
    }

    render_state_pop_transform(&transform_scope);

    rdcon->block = pa_block;  rdcon->font = pa_font;  rdcon->color = pa_color;
    log_leave();

    auto rbv_end = std::chrono::high_resolution_clock::now();
    double this_total = std::chrono::duration<double, std::milli>(rbv_end - rbv_start).count();
    // Self-time = total minus children traversal time
    render_profiler_add_time(rdcon->profiler, RENDER_PROFILE_BLOCK_SELF, this_total - children_time);
}

void render_svg(ImageSurface* surface) {
    if (!surface->pic) {
        log_debug("no picture to render");  return;
    }
    // Rasterize the SVG picture into a pixel buffer using a temporary vector context
    if (surface->width <= 0 || surface->height <= 0) {
        log_debug("invalid svg image size: %dx%d", surface->width, surface->height);
        return;
    }
    int target_width = surface->max_render_width > 0 ? surface->max_render_width : surface->width;
    if (target_width <= 0) {
        log_debug("invalid svg render width: %d", target_width);
        return;
    }
    uint32_t width = (uint32_t)target_width; // INT_CAST_OK: ThorVG raster target dimensions are integer pixels
    uint32_t height = (uint32_t)ceilf((float)target_width * (float)surface->height / (float)surface->width); // INT_CAST_OK: raster target height rounded to pixels
    if (height == 0) {
        log_debug("invalid svg render height for width: %d", target_width);
        return;
    }
    surface->pixels = (uint32_t*)mem_alloc(width * height * sizeof(uint32_t), MEM_CAT_RENDER);
    if (!surface->pixels) return;

    // CRITICAL: Clear the buffer to transparent before rendering SVG
    memset(surface->pixels, 0, width * height * sizeof(uint32_t));

    // Create a temporary vector context targeting the pixel buffer
    RdtVector tmp_vec = {};
    rdt_vector_init(&tmp_vec, (uint32_t*)surface->pixels, width, height, width);

    // Save and clear the global clip stack so parent clips don't leak into this
    // isolated off-screen SVG rasterization
    int saved_clip_depth = rdt_clip_save_depth();

    // Draw SVG picture at target size (takes ownership from surface)
    RdtPicture* pic = surface->pic;
    surface->pic = NULL;  // ownership transferred
    if (pic) {
        rdt_picture_set_size(pic, (float)width, (float)height);
        rdt_picture_draw(&tmp_vec, pic, 255, nullptr);
        rdt_picture_free(pic);
    }

    rdt_clip_restore_depth(saved_clip_depth);
    rdt_vector_destroy(&tmp_vec);
    surface->width = width;  surface->height = height;  surface->pitch = width * sizeof(uint32_t);
}

// Helper function to render just the image content (without block layout)
// Used by both render_image_view and render_block_view for embedded images
void render_image_content(RenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embed->img) return;

    log_debug("render image content");
    float s = rdcon->scale;
    ImageSurface* img = view->embed->img;
    Rect rect;
    rect.x = rdcon->block.x + view->x * s;  rect.y = rdcon->block.y + view->y * s;
    rect.width = view->width * s;  rect.height = view->height * s;

    // Apply object-fit: compute actual image render rect
    CssEnum object_fit = view->embed->object_fit;
    Rect img_rect = rect;  // default: fill (stretch to container)
    // SVG images with a viewBox implicitly use preserveAspectRatio="xMidYMid meet"
    // (equivalent to object-fit: contain) unless object-fit is explicitly set.
    bool svg_default_contain = (img->format == IMAGE_FORMAT_SVG && !object_fit);
    if ((object_fit && object_fit != CSS_VALUE_FILL && img->width > 0 && img->height > 0) ||
        svg_default_contain) {
        float img_w = (float)img->width;
        float img_h = (float)img->height;
        float box_w = rect.width;
        float box_h = rect.height;
        float scale_x = box_w / img_w;
        float scale_y = box_h / img_h;
        float scale;

        if (object_fit == CSS_VALUE_CONTAIN || svg_default_contain) {
            scale = (scale_x < scale_y) ? scale_x : scale_y;
        } else if (object_fit == CSS_VALUE_COVER) {
            scale = (scale_x > scale_y) ? scale_x : scale_y;
        } else if (object_fit == CSS_VALUE_NONE) {
            scale = 1.0f * s;
        } else if (object_fit == CSS_VALUE_SCALE_DOWN) {
            // scale-down: use the smaller of none (1x) or contain
            float contain_scale = (scale_x < scale_y) ? scale_x : scale_y;
            scale = (contain_scale < s) ? contain_scale : s;
        } else {
            scale = scale_x; // fallback: fill-like
        }

        float rendered_w = img_w * scale;
        float rendered_h = img_h * scale;
        // center in the content box (default object-position: 50% 50%)
        img_rect.x = rect.x + (box_w - rendered_w) * 0.5f;
        img_rect.y = rect.y + (box_h - rendered_h) * 0.5f;
        img_rect.width = rendered_w;
        img_rect.height = rendered_h;
    }
    log_debug("[IMAGE RENDER] url=%s, format=%d, img_size=%dx%d, view_size=%.0fx%.0f, pos=(%.0f,%.0f), clip=(%.0f,%.0f,%.0f,%.0f)",
              img->url && img->url->href ? img->url->href->chars : "unknown",
              img->format, img->width, img->height,
              rect.width, rect.height, rect.x, rect.y,
              rdcon->block.clip.left, rdcon->block.clip.top,
              rdcon->block.clip.right, rdcon->block.clip.bottom);
    if (img->format == IMAGE_FORMAT_SVG) {
        log_debug("render svg image vector-first at x:%f, y:%f, wd:%f, hg:%f", img_rect.x, img_rect.y, img_rect.width, img_rect.height);
        if (img->pic) {
            RdtPicture* pic = rdt_picture_dup(img->pic);
            if (pic) {
                render_painter_draw_picture_rect(rdcon, pic, &img_rect, &rdcon->block.clip, 255);
                if (!rdcon->dl) {
                    rdt_picture_free(pic);
                }
            }
        } else if (img->pixels) {
            render_painter_draw_pixels_rect(rdcon, (uint32_t*)img->pixels, img->width, img->height,
                                            img->width, &img_rect, &rdcon->block.clip, 255);
        } else {
            log_debug("failed to render svg image: no vector picture or raster pixels");
        }
    } else {
        // ensure raster image pixels are decoded (lazy loading) at the displayed size
        image_surface_ensure_decoded(img, (int)img_rect.width, (int)img_rect.height);
        log_debug("blit image at x:%f, y:%f, wd:%f, hg:%f", img_rect.x, img_rect.y, img_rect.width, img_rect.height);
        render_painter_blit_surface_scaled(rdcon, img, NULL, rdcon->ui_context->surface,
            &img_rect, &rdcon->block.clip, SCALE_MODE_LINEAR,
            rdcon->clip_shapes, rdcon->clip_shape_depth);
    }

    // Render blue selection overlay if image is within a cross-view selection
    DocState* state = rdcon->ui_context && rdcon->ui_context->document
        ? rdcon->ui_context->document->state : NULL;
    if (render_selection_contains_view(state, static_cast<View*>(view))) {
        // Semi-transparent blue overlay (same color as text selection)
        uint32_t sel_bg_color = 0x80FF9933;  // ABGR format: semi-transparent blue
        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, sel_bg_color, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
        log_debug("[IMAGE SELECTION] Rendered blue overlay on image at (%.0f,%.0f) size %.0fx%.0f",
                  rect.x, rect.y, rect.width, rect.height);
    }
}

void render_image_view(RenderContext* rdcon, ViewBlock* view) {
    log_debug("render image view");
    log_enter();
    // render border and background, etc.
    render_block_view(rdcon, view);
    // render the image content (using parent coordinates restored by render_block_view)
    render_image_content(rdcon, view);
    log_debug("end of image render");
    log_leave();
}

// render webview in layer mode: record a DL placeholder for post-composite blit
void render_webview_layer_content(RenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embed->webview) return;
    WebViewProp* wv = view->embed->webview;
    if (wv->mode != WEBVIEW_MODE_LAYER || !wv->surface || !wv->surface->pixels) return;
    if (!wv->visible) return;

    float s = rdcon->scale;
    float dst_x = rdcon->block.x + view->x * s;
    float dst_y = rdcon->block.y + view->y * s;
    float dst_w = view->width * s;
    float dst_h = view->height * s;

    log_debug("[WEBVIEW LAYER RENDER] pos=(%.0f,%.0f) size=%.0fx%.0f surface=%dx%d",
              dst_x, dst_y, dst_w, dst_h,
              wv->surface->width, wv->surface->height);

    if (rdcon->dl) {
        dl_webview_layer_placeholder(rdcon->dl, wv->surface,
                                     dst_x, dst_y, dst_w, dst_h,
                                     &rdcon->block.clip);
    } else {
        // fallback: direct blit (single-threaded path)
        Rect rect = { dst_x, dst_y, dst_w, dst_h };
        RasterPaintContext raster = {
            rdcon->ui_context->surface,
            &rdcon->block.clip,
            rdcon->clip_shapes,
            rdcon->clip_shape_depth
        };
        raster_blit_surface_scaled(&raster, wv->surface, NULL, &rect, SCALE_MODE_LINEAR);
    }
}

// render video element: record a DL_VIDEO_PLACEHOLDER for post-composite blit
void render_video_content(RenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embed->video) return;

    float s = rdcon->scale;
    float dst_x = rdcon->block.x + view->x * s;
    float dst_y = rdcon->block.y + view->y * s;
    float dst_w = view->width * s;
    float dst_h = view->height * s;
    int object_fit_flags = (int)view->embed->object_fit;
    // pack has_controls into bit 8
    if (view->embed->has_controls) object_fit_flags |= 0x100;

    log_debug("[VIDEO RENDER] placeholder at (%.0f,%.0f) size %.0fx%.0f controls=%d",
              dst_x, dst_y, dst_w, dst_h, view->embed->has_controls);

    if (rdcon->dl) {
        dl_video_placeholder(rdcon->dl, view->embed->video,
                             dst_x, dst_y, dst_w, dst_h,
                             object_fit_flags, &rdcon->block.clip);
    }
}

void render_embed_doc(RenderContext* rdcon, ViewBlock* block) {
    BlockBlot pa_block = rdcon->block;
    if (block->bound) { render_bound(rdcon, block); }

    float s = rdcon->scale;
    rdcon->block.x = pa_block.x + block->x * s;  rdcon->block.y = pa_block.y + block->y * s;

    // Constrain clip region to iframe content box (before scroller setup)
    // This ensures embedded documents (SVG, PDF, etc.) don't render outside iframe bounds
    float content_left = rdcon->block.x;
    float content_top = rdcon->block.y;
    float content_right = rdcon->block.x + block->width * s;
    float content_bottom = rdcon->block.y + block->height * s;

    // Adjust for borders if present
    if (block->bound && block->bound->border) {
        content_left += block->bound->border->width.left * s;
        content_top += block->bound->border->width.top * s;
        content_right -= block->bound->border->width.right * s;
        content_bottom -= block->bound->border->width.bottom * s;
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

                ViewBlock* root_block = lam::view_require_block(root_view);

                // Per CSS 2.1 §14.2: the iframe's viewport is its own canvas.
                // Propagate the body background (or html background) to fill the
                // iframe content box, otherwise the body only paints its own
                // intrinsic-sized box (often smaller than the iframe viewport,
                // leaving white gaps below the body content).
                if (root_block->tag_id != HTM_TAG_SVG &&
                    !(root_block->embed && root_block->embed->img)) {
                    Color canvas_bg;
                    canvas_bg.c = 0;
                    bool html_has_bg = root_block->bound && root_block->bound->background &&
                                       root_block->bound->background->color.a > 0;
                    if (html_has_bg) {
                        canvas_bg = root_block->bound->background->color;
                    } else {
                        // walk html children for body bg
                        View* c = root_block->first_child;
                        while (c) {
                            if (c->view_type == RDT_VIEW_BLOCK) {
                                ViewBlock* cb = lam::view_require_block(c);
                                const char* nm = cb->node_name();
                                if (nm && str_ieq_const(nm, strlen(nm), "body")) {
                                    if (cb->bound && cb->bound->background &&
                                        cb->bound->background->color.a > 0) {
                                        canvas_bg = cb->bound->background->color;
                                    }
                                    break;
                                }
                            }
                            c = static_cast<View*>(c->next_sibling);
                        }
                    }
                    if (canvas_bg.a > 0) {
                        // Fill iframe content box (already computed above as content_left/top/right/bottom).
                        rc_fill_rect(rdcon,
                                     content_left, content_top,
                                     content_right - content_left,
                                     content_bottom - content_top,
                                     canvas_bg);
                    }
                }

                // Check if root element is SVG - if so, render directly without background
                if (root_block->tag_id == HTM_TAG_SVG) {
                    log_debug("render embedded SVG document (no background)");
                    render_inline_svg(rdcon, root_block);
                } else if (root_block->embed && root_block->embed->img) {
                    // Image/SVG document root — use render_image_view
                    render_image_view(rdcon, root_block);
                } else {
                    // Regular HTML document - render with background
                    render_block_view(rdcon, root_block);
                }

                rdcon->font = pa_font;
                rdcon->color = pa_color;
            }
            else {
                log_debug("Invalid root view");
            }
        }
    }

    // Render scrollbar for the iframe scroll container
    if (block->scroller) {
        render_scroller(rdcon, block, &pa_block);
    }
    rdcon->block = pa_block;
}

void render_inline_view(RenderContext* rdcon, ViewSpan* view_span) {
    render_profiler_increment(rdcon->profiler, RENDER_PROFILE_INLINE);
    FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;
    log_debug("render inline view");

    bool self_hidden = view_span->in_line && view_span->in_line->visibility == VIS_HIDDEN;

    // Render border/outline for inline elements.
    // Background is rendered per-line-fragment in render_text_view so that
    // wrapping inline elements (e.g. <code> spanning two lines) don't fill
    // the entire bounding-box rectangle with background color.
    if (!self_hidden && view_span->bound) {
        BackgroundProp* saved_bg = view_span->bound->background;
        view_span->bound->background = nullptr;
        render_bound(rdcon, lam::unsafe_view_block_api_span(view_span));
        view_span->bound->background = saved_bg;
    }

    View* view = view_span->first_child;
    if (view) {
        if (view_span->font) {
            setup_font(rdcon->ui_context, &rdcon->font, view_span->font);
        }
        if (view_span->in_line && view_span->in_line->has_color) {
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
    auto trc_start = std::chrono::high_resolution_clock::now();
    do {
        render_profiler_increment(rdcon->profiler, RENDER_PROFILE_DISPATCH);
        if (view->view_type == RDT_VIEW_BLOCK || view->view_type == RDT_VIEW_INLINE_BLOCK ||
            view->view_type == RDT_VIEW_TABLE || view->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
            view->view_type == RDT_VIEW_TABLE_ROW || view->view_type == RDT_VIEW_TABLE_CELL) {
            ViewBlock* block = lam::view_require_block(view);
            log_debug("[RENDER DISPATCH] view_type=%d, embed=%p, img=%p, width=%.0f, height=%.0f",
                      view->view_type, block->embed,
                      block->embed ? block->embed->img : NULL, block->width, block->height);
            if (block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form) {
                // Form control rendering (input, select, textarea, button)
                // For <button> elements with children, render default button background BEFORE
                // children so the gray fill doesn't cover the text content.
                if (block->form->control_type == FORM_CONTROL_BUTTON && block->first_child) {
                    render_form_control(rdcon, block);  // draw button chrome first
                    render_block_view(rdcon, block);    // then children (text) on top
                } else {
                    // Other form controls: render block first, then form decorations on top
                    log_debug("[RENDER DISPATCH] calling render_block_view for form control");
                    render_block_view(rdcon, block);
                    log_debug("[RENDER DISPATCH] calling render_form_control");
                    render_form_control(rdcon, block);
                }
            }
            else if (block->tag_id == HTM_TAG_SVG) {
                // Inline SVG element - paint CSS background/border first, then SVG content
                if (block->bound) { render_bound(rdcon, block); }
                log_debug("[RENDER DISPATCH] calling render_inline_svg for inline SVG");
                auto ts1 = std::chrono::high_resolution_clock::now();
                render_inline_svg(rdcon, block);
                auto ts2 = std::chrono::high_resolution_clock::now();
                render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_SVG,
                    std::chrono::duration<double, std::milli>(ts2 - ts1).count());
            }
            else if (block->embed && block->embed->img) {
                log_debug("[RENDER DISPATCH] calling render_image_view");
                auto ti1 = std::chrono::high_resolution_clock::now();
                render_image_view(rdcon, block);
                auto ti2 = std::chrono::high_resolution_clock::now();
                render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_IMAGE,
                    std::chrono::duration<double, std::milli>(ti2 - ti1).count());
            }
            else if (block->embed && block->embed->video) {
                log_debug("[RENDER DISPATCH] calling render_video_content for <video>");
                render_block_view(rdcon, block);
                render_video_content(rdcon, block);
            }
            else if (block->embed && block->embed->webview &&
                     block->embed->webview->mode == WEBVIEW_MODE_LAYER) {
                log_debug("[RENDER DISPATCH] calling render_webview_layer_content");
                render_block_view(rdcon, block);
                render_webview_layer_content(rdcon, block);
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
            render_litem_view(rdcon, lam::view_require_block(view));
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require_element(view);
            auto tiv1 = std::chrono::high_resolution_clock::now();
            render_inline_view(rdcon, span);
            auto tiv2 = std::chrono::high_resolution_clock::now();
            render_profiler_add_time(rdcon->profiler, RENDER_PROFILE_INLINE,
                std::chrono::duration<double, std::milli>(tiv2 - tiv1).count());
        }
        else if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(view);
            auto tt1 = std::chrono::high_resolution_clock::now();
            render_text_view(rdcon, text);
            auto tt2 = std::chrono::high_resolution_clock::now();
            render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_TEXT,
                std::chrono::duration<double, std::milli>(tt2 - tt1).count());
        }
        else if (view->view_type == RDT_VIEW_MARKER) {
            // List marker (bullet/number) with fixed width and vector graphics
            ViewSpan* marker = lam::view_require_element(view);
            render_marker_view(rdcon, marker);
        }
        else {
            log_debug("unknown view in rendering: %d", view->view_type);
        }
        view = view->next();
    } while (view);
    auto trc_end = std::chrono::high_resolution_clock::now();
    render_profiler_add_time(rdcon->profiler, RENDER_PROFILE_CHILDREN,
        std::chrono::duration<double, std::milli>(trc_end - trc_start).count());
}

void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file) {
    render_output_render_html_doc(uicon, view_tree, output_file);
}

void render_html_doc_tiled(UiContext* uicon, ViewTree* view_tree,
                           const char* output_file,
                           int total_width, int total_height) {
    render_output_render_tiled_png(uicon, view_tree, output_file, total_width, total_height);
}
