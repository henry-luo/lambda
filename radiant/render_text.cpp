#include "render_text.hpp"
#include "render_glyph.hpp"
#include "render_background.hpp"
#include "render_profiler.hpp"
#include "layout.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/font/font.h"

#include <chrono>
#include <math.h>
#include <string.h>

// Check if a codepoint has Emoji_Presentation=Yes (Unicode 15.0, UTS #51).
// Mirrors is_emoji_presentation_default in layout_text.cpp.
static inline bool render_text_is_emoji_presentation_default(uint32_t cp) {
    if (cp >= 0x1F000 && cp <= 0x1FFFF) return true;
    if (cp >= 0xE0020 && cp <= 0xE007F) return true;
    if (cp < 0x231A || cp > 0x3299) return false;
    if (cp <= 0x231B) return true;
    if (cp >= 0x23E9 && cp <= 0x23F3) return true;
    if (cp >= 0x23F8 && cp <= 0x23FA) return true;
    if (cp == 0x25AA || cp == 0x25AB) return true;
    if (cp == 0x25B6 || cp == 0x25C0) return true;
    if (cp >= 0x25FB && cp <= 0x25FE) return true;
    if (cp == 0x2614 || cp == 0x2615) return true;
    if (cp >= 0x2648 && cp <= 0x2653) return true;
    if (cp == 0x267F || cp == 0x2693 || cp == 0x26A1) return true;
    if (cp == 0x26AA || cp == 0x26AB) return true;
    if (cp == 0x26BD || cp == 0x26BE) return true;
    if (cp == 0x26C4 || cp == 0x26C5) return true;
    if (cp == 0x26CE || cp == 0x26D4 || cp == 0x26EA) return true;
    if (cp == 0x26F2 || cp == 0x26F3 || cp == 0x26F5) return true;
    if (cp == 0x26FA || cp == 0x26FD) return true;
    if (cp == 0x2702 || cp == 0x2705) return true;
    if (cp >= 0x2708 && cp <= 0x270D) return true;
    if (cp == 0x270F || cp == 0x2712) return true;
    if (cp == 0x2714 || cp == 0x2716) return true;
    if (cp == 0x271D || cp == 0x2721 || cp == 0x2728) return true;
    if (cp == 0x2733 || cp == 0x2734) return true;
    if (cp == 0x2744 || cp == 0x2747) return true;
    if (cp == 0x274C || cp == 0x274E) return true;
    if (cp >= 0x2753 && cp <= 0x2755) return true;
    if (cp == 0x2757) return true;
    if (cp == 0x2763 || cp == 0x2764) return true;
    if (cp >= 0x2795 && cp <= 0x2797) return true;
    if (cp == 0x27A1 || cp == 0x27B0 || cp == 0x27BF) return true;
    if (cp == 0x2934 || cp == 0x2935) return true;
    if (cp >= 0x2B05 && cp <= 0x2B07) return true;
    if (cp == 0x2B1B || cp == 0x2B1C) return true;
    if (cp == 0x2B50 || cp == 0x2B55) return true;
    if (cp == 0x3030 || cp == 0x303D) return true;
    if (cp == 0x3297 || cp == 0x3299) return true;
    return false;
}

static inline bool render_text_preserve_spaces(CssEnum ws) {
    return ws == CSS_VALUE_PRE || ws == CSS_VALUE_PRE_WRAP || ws == CSS_VALUE_BREAK_SPACES;
}

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
        rdcon->font = saved_font;
        rdcon->color = saved_color;
        return;
    }

    // Get the white-space property for this text node
    CssEnum white_space = get_white_space_value(text_view);
    bool preserve_spaces = render_text_preserve_spaces(white_space);

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

void render_text_inline_background(RenderContext* rdcon, ViewText* text_view,
                                   TextRect* text_rect, DomElement* parent_elem,
                                   float x, float y) {
    if (!rdcon || !text_view || !text_rect || !parent_elem ||
        parent_elem->view_type != RDT_VIEW_INLINE ||
        !parent_elem->bound || !parent_elem->bound->background ||
        parent_elem->bound->background->color.a <= 0) {
        return;
    }

    float s = rdcon->scale;
    Color* bg_color = &parent_elem->bound->background->color;
    float bg_pad_top = parent_elem->bound->padding.top * s;
    float bg_pad_right = parent_elem->bound->padding.right * s;
    float bg_pad_bottom = parent_elem->bound->padding.bottom * s;
    float bg_pad_left = parent_elem->bound->padding.left * s;
    float bg_radius = parent_elem->bound->border ?
        parent_elem->bound->border->radius.top_left * s : 0;

    bool is_first = (text_rect == text_view->rect);
    bool is_last = (text_rect->next == nullptr);
    float pl = is_first ? bg_pad_left : 0;
    float pr = is_last ? bg_pad_right : 0;
    Rect bg_rect = {
        x - pl,
        y - bg_pad_top,
        text_rect->width * s + pl + pr,
        text_rect->height * s + bg_pad_top + bg_pad_bottom
    };

    if (bg_radius > 0) {
        float r_left = is_first ? bg_radius : 0;
        float r_right = is_last ? bg_radius : 0;
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

typedef struct SkipInkGap {
    float x0;
    float x1;
} SkipInkGap;

bool render_text_paint_blurred_shadows(RenderContext* rdcon, unsigned char* str,
                                       TextRect* text_rect, TextShadow* text_shadow,
                                       CssEnum text_transform, bool preserve_spaces,
                                       float space_width, float scaled_space_width,
                                       float x, float y) {
    if (!rdcon || !str || !text_rect || !text_shadow ||
        !rdcon->font.font_handle || !rdcon->font.style) {
        return false;
    }

    float s = rdcon->scale;
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
    bool s_has_space = false;
    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);
    bool s_word_start = true;

    while (sp < s_end) {
        if (is_space(*sp)) {
            if (preserve_spaces || !s_has_space) {
                s_has_space = true;
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

            LoadedGlyph* s_glyph = font_load_glyph(rdcon->font.font_handle, &sd, s_tt_cp, true);
            if (!s_glyph) {
                sx_pos += scaled_space_width;
                continue;
            }

            float s_ascend;
            {
                auto tfm1 = std::chrono::high_resolution_clock::now();
                s_ascend = font_get_rendering_ascender(rdcon->font.font_handle) * rdcon->scale;
                auto tfm2 = std::chrono::high_resolution_clock::now();
                render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_FONT_METRICS,
                    std::chrono::duration<double, std::milli>(tfm2 - tfm1).count());
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
            sx_pos += s_glyph->advance_x + rdcon->font.style->letter_spacing * s;
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
        if (rdcon->dl) {
            dl_box_blur_region(rdcon->dl, bx, by, bw, bh, max_shadow_blur, 0, nullptr);
        } else {
            box_blur_region(&rdcon->scratch, rdcon->ui_context->surface, bx, by, bw, bh, max_shadow_blur);
        }
        log_debug("[TEXT-SHADOW] Applied blur radius=%.1f to region (%d,%d,%d,%d) dl=%d",
            max_shadow_blur, bx, by, bw, bh, rdcon->dl != nullptr);
    }

    return true;
}

LoadedGlyph* render_text_load_glyph_for_paint(RenderContext* rdcon, uint32_t codepoint,
                                              unsigned char* cursor, unsigned char* end) {
    if (!rdcon || !rdcon->font.font_handle || !rdcon->font.style) {
        return nullptr;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);
    bool emoji_presentation = false;
    if (cursor < end) {
        uint32_t peek_cp;
        int peek_bytes = str_utf8_decode((const char*)cursor, (size_t)(end - cursor), &peek_cp);
        if (peek_bytes > 0 && peek_cp == 0xFE0F) {
            emoji_presentation = true;
            log_debug("render emoji: VS16 peek hit for U+%04X", codepoint);
        }
    }
    if (!emoji_presentation && render_text_is_emoji_presentation_default(codepoint)) {
        emoji_presentation = true;
    }

    LoadedGlyph* glyph = emoji_presentation
        ? font_load_glyph_emoji(rdcon->font.font_handle, &sd, codepoint, true)
        : font_load_glyph(rdcon->font.font_handle, &sd, codepoint, true);
    auto t2 = std::chrono::high_resolution_clock::now();
    render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_GLYPH_LOAD,
        std::chrono::duration<double, std::milli>(t2 - t1).count());
    return glyph;
}

void render_text_paint_glyph_shadows(RenderContext* rdcon, LoadedGlyph* glyph,
                                     TextShadow* text_shadow, float x, float y,
                                     float ascend) {
    if (!rdcon || !glyph || !text_shadow) {
        return;
    }

    Color saved_shadow_color = rdcon->color;
    TextShadow* ts = text_shadow;
    while (ts) {
        rdcon->color = ts->color;
        float sx = x + glyph->bitmap.bearing_x + ts->offset_x * rdcon->scale;
        float sy = y + ascend - glyph->bitmap.bearing_y + ts->offset_y * rdcon->scale;
        draw_glyph(rdcon, &glyph->bitmap, lroundf(sx), lroundf(sy));
        ts = ts->next;
    }
    rdcon->color = saved_shadow_color;
}

float render_text_trailing_marks(RenderContext* rdcon, TextRect* text_rect,
                                 float x, float y) {
    if (!rdcon || !rdcon->font.font_handle || !rdcon->font.style || !text_rect) {
        return x;
    }

    if (text_rect->has_trailing_hyphen) {
        FontStyleDesc sd_h = font_style_desc_from_prop(rdcon->font.style);
        LoadedGlyph* h_glyph = font_load_glyph(rdcon->font.font_handle, &sd_h, '-', true);
        if (h_glyph) {
            float ascend = font_get_rendering_ascender(rdcon->font.font_handle) * rdcon->scale;
            draw_glyph(rdcon, &h_glyph->bitmap,
                       lroundf(x + h_glyph->bitmap.bearing_x),
                       lroundf(y + ascend - h_glyph->bitmap.bearing_y));
            x += h_glyph->advance_x;
        }
    }

    if (text_rect->has_trailing_ellipsis) {
        FontStyleDesc sd_e = font_style_desc_from_prop(rdcon->font.style);
        LoadedGlyph* e_glyph = font_load_glyph(rdcon->font.font_handle, &sd_e, 0x2026, true);
        if (e_glyph) {
            float ascend = font_get_rendering_ascender(rdcon->font.font_handle) * rdcon->scale;
            draw_glyph(rdcon, &e_glyph->bitmap,
                       lroundf(x + e_glyph->bitmap.bearing_x),
                       lroundf(y + ascend - e_glyph->bitmap.bearing_y));
        }
    }

    return x;
}

static int collect_skip_ink_gaps(RenderContext* rdcon, unsigned char* str,
                                 TextRect* text_rect, float deco_y_top, float deco_y_bot,
                                 SkipInkGap* gaps, int max_gaps) {
    float s = rdcon->scale;
    float x = rdcon->block.x + text_rect->x * s;
    float y_base = rdcon->block.y + text_rect->y * s;
    float ascend = font_get_rendering_ascender(rdcon->font.font_handle) * s;
    int gap_count = 0;
    float pad = fmaxf(2.0f, (deco_y_bot - deco_y_top));

    unsigned char* p = str + text_rect->start_index;
    unsigned char* end = p + text_rect->length;
    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);

    while (p < end && gap_count < max_gaps) {
        if (is_space(*p)) {
            x += rdcon->font.style->space_width * s;
            p++;
            continue;
        }

        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)p, (size_t)(end - p), &codepoint);
        if (bytes <= 0) {
            p++;
            continue;
        }
        p += bytes;
        if (codepoint == 0x00AD) continue;

        LoadedGlyph* glyph = font_load_glyph(rdcon->font.font_handle, &sd, codepoint, true);
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

static void draw_deco_with_gaps(RenderContext* rdcon, Rect rect, uint32_t color,
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

void render_text_decorations(RenderContext* rdcon, unsigned char* str, TextRect* text_rect) {
    if (!rdcon || !rdcon->font.style || !text_rect) {
        return;
    }
    if (rdcon->font.style->text_deco == CSS_VALUE_NONE ||
        rdcon->font.style->text_deco == CSS_VALUE__UNDEF) {
        return;
    }

    float s = rdcon->scale;
    const FontMetrics* deco_m = font_get_metrics(rdcon->font.font_handle);
    float thickness = rdcon->font.style->text_deco_thickness > 0
        ? rdcon->font.style->text_deco_thickness
        : fmaxf(deco_m ? deco_m->underline_thickness : 1.0f, 1.0f);
    thickness = fmaxf(roundf(thickness), 1.0f);

    Color deco_color = rdcon->font.style->text_deco_color.a > 0
        ? rdcon->font.style->text_deco_color
        : rdcon->color;
    CssEnum deco_style = rdcon->font.style->text_deco_style;
    if (deco_style == CSS_VALUE__UNDEF || deco_style == 0) deco_style = CSS_VALUE_SOLID;

    Rect rect = {0, 0, 0, 0};
    bool draw_deco = true;
    float deco_ascend = font_get_rendering_ascender(rdcon->font.font_handle) * s;
    if (rdcon->font.style->text_deco == CSS_VALUE_UNDERLINE) {
        float underline_pos = deco_m ? deco_m->underline_position : 0;
        float offset = rdcon->font.style->text_underline_offset;
        rect.x = rdcon->block.x + text_rect->x * s;
        rect.y = roundf(rdcon->block.y + text_rect->y * s + deco_ascend - underline_pos * s + offset);
    }
    else if (rdcon->font.style->text_deco == CSS_VALUE_OVERLINE) {
        rect.x = rdcon->block.x + text_rect->x * s;
        rect.y = floorf(rdcon->block.y + text_rect->y * s);
    }
    else if (rdcon->font.style->text_deco == CSS_VALUE_LINE_THROUGH) {
        rect.x = rdcon->block.x + text_rect->x * s;
        float strike_y;
        if (deco_m && deco_m->x_height > 0) {
            strike_y = deco_m->x_height * 0.5f;
        } else if (deco_m && deco_m->strikeout_position > 0) {
            strike_y = deco_m->strikeout_position;
        } else {
            strike_y = deco_ascend * 0.3f / s;
        }
        rect.y = roundf(rdcon->block.y + text_rect->y * s + deco_ascend - strike_y * s);
        if (deco_m && deco_m->strikeout_size > 0)
            thickness = fmaxf(ceilf(deco_m->strikeout_size), 1.0f);
    }
    else {
        draw_deco = false;
    }
    if (!draw_deco) {
        return;
    }

    rect.width = text_rect->width * s;
    rect.height = thickness;
    log_debug("text deco: %d style=%d, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f",
        rdcon->font.style->text_deco, deco_style, rect.x, rect.y, rect.width, rect.height);

    if (deco_style == CSS_VALUE_DASHED) {
        float dash_len = thickness * 3.0f;
        float gap_len = thickness * 3.0f;
        float dx = rect.x;
        while (dx < rect.x + rect.width) {
            float seg_w = fminf(dash_len, rect.x + rect.width - dx);
            Rect seg = {dx, rect.y, seg_w, thickness};
            rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &seg, deco_color.c,
                &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            dx += dash_len + gap_len;
        }
    } else if (deco_style == CSS_VALUE_DOTTED) {
        float dx = rect.x;
        while (dx < rect.x + rect.width) {
            float seg_w = fminf(thickness, rect.x + rect.width - dx);
            Rect seg = {dx, rect.y, seg_w, thickness};
            rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &seg, deco_color.c,
                &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            dx += thickness * 2.0f;
        }
    } else if (deco_style == CSS_VALUE_DOUBLE) {
        float line_t = thickness;
        float gap = fmaxf(1.0f, thickness - 1.0f);
        Rect top_line = {rect.x, rect.y, rect.width, line_t};
        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &top_line, deco_color.c,
            &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
        Rect bot_line = {rect.x, rect.y + line_t + gap, rect.width, line_t};
        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &bot_line, deco_color.c,
            &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
    } else if (deco_style == CSS_VALUE_WAVY) {
        float wave_amp = thickness * 1.5f;
        float wave_len = thickness * 4.0f;
        float wave_center = rect.y + wave_amp;
        RdtPath* p = rdt_path_new();
        float wx = rect.x;
        rdt_path_move_to(p, wx, wave_center);
        while (wx < rect.x + rect.width) {
            float half = fminf(wave_len / 2.0f, rect.x + rect.width - wx);
            rdt_path_cubic_to(p, wx + half * 0.33f, wave_center - wave_amp,
                              wx + half * 0.67f, wave_center - wave_amp,
                              wx + half, wave_center);
            wx += half;
            if (wx >= rect.x + rect.width) break;
            half = fminf(wave_len / 2.0f, rect.x + rect.width - wx);
            rdt_path_cubic_to(p, wx + half * 0.33f, wave_center + wave_amp,
                              wx + half * 0.67f, wave_center + wave_amp,
                              wx + half, wave_center);
            wx += half;
        }
        rc_stroke_path(rdcon, p, deco_color, fmaxf(1.0f, thickness * 0.5f),
                       RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, NULL);
        rdt_path_free(p);
    } else {
        bool apply_skip_ink = (rdcon->font.style->text_deco == CSS_VALUE_UNDERLINE);
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
