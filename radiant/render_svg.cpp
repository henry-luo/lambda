#include "render.hpp"
#include "render_backend.h"
#include "render_backend_caps.hpp"
#include "render_export_support.hpp"
#include "render_geometry.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "paint_ir.h"
#include "render_paint_boundary.hpp"
#include "render_path.hpp"
#include "render_effect_raster_fallback.hpp"
#include "render_glyph_run_raster_lower.hpp"
#include "render_profiler.hpp"
#include "render_svg_inline.hpp"
#include "state_store.hpp"
#include "font_face.h"
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include "../lib/font/font.h"
#include "../lib/utf.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/mark_reader.hpp"
extern "C" {
#include "../lib/url.h"
#include "../lib/base64.h"
#include "../lib/memtrack.h"
}
#include "../lib/strbuf.h"
#include "../lib/str.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <png.h>
#include <cctype>
#include <cwctype>

typedef struct SvgEffectRasterFallback {
    bool active;
    int nested_depth;
    PaintEffectGroup group;
    PaintList paint_list;
} SvgEffectRasterFallback;

typedef struct {
    StrBuf* svg_content;
    int indent_level;
    float viewport_width;
    float viewport_height;
    // Context from parent render context
    FontBox font;
    BlockBlot block;
    Color color;
    UiContext* ui_context;
    PaintList paint_list;
    SvgEffectRasterFallback effect_fallback;
    Pool* page_backdrop_pool;
    Arena* page_backdrop_arena;
    DisplayList page_backdrop_dl;
    bool page_backdrop_ready;
    PaintSvgLoweringState paint_svg_state;
    PaintSvgLoweringStats paint_svg_stats;
    int paint_resource_id;
    bool transform_emitted_stack[64];
    int transform_emitted_depth;
    int transform_emitted_overflow_depth;
} SvgRenderContext;

// Forward declarations
static void render_text_view_svg(SvgRenderContext* ctx, ViewText* text);
static void render_bound_svg(SvgRenderContext* ctx, ViewBlock* view);
static void render_column_rules_svg(SvgRenderContext* ctx, ViewBlock* block);
static RenderBackend svg_make_backend(SvgRenderContext* ctx);

// Helper functions for SVG output
static void svg_indent(SvgRenderContext* ctx) {
    for (int i = 0; i < ctx->indent_level; i++) {
        strbuf_append_str(ctx->svg_content, "  ");
    }
}

static PaintList* svg_active_paint_list(SvgRenderContext* ctx) {
    if (ctx && ctx->effect_fallback.active) {
        return &ctx->effect_fallback.paint_list;
    }
    return ctx ? &ctx->paint_list : nullptr;
}

static bool svg_effect_group_needs_raster_fallback(const PaintEffectGroup* group) {
    return group &&
           (group->has_clip || group->blend_mode != 0 || group->filter ||
            group->backdrop || group->backdrop_filter ||
            group->shadow || group->isolation);
}

static bool svg_effect_group_needs_page_backdrop(const PaintEffectGroup* group) {
    return group &&
           (group->blend_mode != 0 || group->backdrop || group->backdrop_filter);
}

static bool svg_paint_list_balanced_effect_groups(const PaintList* paint_list) {
    if (!paint_list) return false;
    int effect_depth = 0;
    for (int i = 0; i < paint_list->count; i++) {
        PaintOp op = paint_list->cmds[i].op;
        if (op == PAINT_BEGIN_EFFECT_GROUP) {
            effect_depth++;
        } else if (op == PAINT_END_EFFECT_GROUP) {
            effect_depth--;
            if (effect_depth < 0) return false;
        }
    }
    return effect_depth == 0;
}

static void svg_record_page_backdrop_paint_list(SvgRenderContext* ctx,
                                                const PaintList* paint_list) {
    if (!ctx || !ctx->page_backdrop_ready || !paint_list || paint_list_count(paint_list) <= 0) {
        return;
    }
    if (!svg_paint_list_balanced_effect_groups(paint_list)) {
        return;
    }
    render_svg_inline_register_paint_ir_lowerers();
    paint_ir_register_glyph_run_raster_lowerer(render_glyph_run_raster_lower);
    paint_ir_lower_raster(paint_list, &ctx->page_backdrop_dl);
}

static void svg_lower_paint_list(SvgRenderContext* ctx) {
    if (!ctx || ctx->effect_fallback.active) return;
    render_svg_inline_register_paint_ir_lowerers();
    svg_record_page_backdrop_paint_list(ctx, &ctx->paint_list);

    PaintSvgLoweringOptions options = {};
    options.indent_level = ctx->indent_level;
    options.caps = render_export_target_get_caps(RENDER_EXPORT_TARGET_SVG);
    options.resource_id_base = ctx->paint_resource_id;
    ctx->paint_svg_state.indent_level = ctx->indent_level;
    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg_stream(&ctx->paint_list, ctx->svg_content, &options,
                              &ctx->paint_svg_state, &stats);
    ctx->paint_svg_stats.command_count += stats.command_count;
    ctx->paint_svg_stats.emitted_count += stats.emitted_count;
    ctx->paint_svg_stats.fallback_count += stats.fallback_count;
    ctx->paint_svg_stats.unsupported_count += stats.unsupported_count;
    ctx->paint_resource_id += paint_list_count(&ctx->paint_list);
    ctx->indent_level = ctx->paint_svg_state.indent_level;
    paint_list_clear(&ctx->paint_list);
}

static void svg_png_write_to_strbuf(png_structp png_ptr,
                                    png_bytep data,
                                    png_size_t length) {
    StrBuf* out = (StrBuf*)png_get_io_ptr(png_ptr);
    if (!out || !data || length == 0) return;
    if (!strbuf_ensure_cap(out, out->length + length + 1)) return;
    memcpy(out->str + out->length, data, length);
    out->length += length;
    out->str[out->length] = '\0';
}

static StrBuf* svg_encode_surface_png(ImageSurface* surface) {
    if (!surface || !surface->pixels || surface->width <= 0 || surface->height <= 0) {
        return nullptr;
    }
    StrBuf* png_bytes = strbuf_new_cap((size_t)surface->width * (size_t)surface->height);
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        strbuf_free(png_bytes);
        return nullptr;
    }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        strbuf_free(png_bytes);
        return nullptr;
    }
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        strbuf_free(png_bytes);
        return nullptr;
    }

    png_set_write_fn(png_ptr, png_bytes, svg_png_write_to_strbuf, NULL);
    png_set_IHDR(png_ptr, info_ptr, surface->width, surface->height,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    png_bytep* rows = (png_bytep*)mem_alloc(sizeof(png_bytep) * surface->height, MEM_CAT_RENDER);
    if (!rows) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        strbuf_free(png_bytes);
        return nullptr;
    }
    for (int y = 0; y < surface->height; y++) {
        rows[y] = (png_bytep)((uint8_t*)surface->pixels + y * surface->pitch);
    }
    png_write_image(png_ptr, rows);
    png_write_end(png_ptr, NULL);

    mem_free(rows);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return png_bytes;
}

static void svg_append_base64(StrBuf* out, const uint8_t* data, size_t len) {
    if (!out || !data) return;
    char* b64 = base64_encode_alloc(data, len, BASE64_STD);
    if (!b64) return;
    strbuf_append_str(out, b64);
    mem_free(b64);
}

static bool svg_emit_raster_fallback_image(SvgRenderContext* ctx,
                                           ImageSurface* surface,
                                           float x,
                                           float y,
                                           float width,
                                           float height) {
    StrBuf* png_bytes = svg_encode_surface_png(surface);
    if (!png_bytes) return false;
    svg_indent(ctx);
    strbuf_append_format(ctx->svg_content,
        "<image data-radiant-fallback=\"effect-raster\" x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" href=\"data:image/png;base64,",
        x, y, width, height);
    svg_append_base64(ctx->svg_content, (const uint8_t*)png_bytes->str, png_bytes->length);
    strbuf_append_str(ctx->svg_content, "\" />\n");
    strbuf_free(png_bytes);
    return true;
}

static void svg_begin_effect_raster_fallback(SvgRenderContext* ctx,
                                             const PaintEffectGroup* group) {
    if (!ctx || !group) return;
    ctx->effect_fallback.active = true;
    ctx->effect_fallback.nested_depth = 0;
    ctx->effect_fallback.group = *group;
    paint_list_clear(&ctx->effect_fallback.paint_list);
    paint_ir_register_glyph_run_raster_lowerer(render_glyph_run_raster_lower);
    paint_begin_effect_group(&ctx->effect_fallback.paint_list, group);
    log_error("[SVG_PAINT_IR] raster fallback effect group opacity=%.3f blend=%d filter=%p backdrop=%d backdrop_filter=%p shadow=%d isolation=%d",
              group->opacity, group->blend_mode, group->filter,
              group->backdrop ? 1 : 0, group->backdrop_filter,
              group->shadow ? 1 : 0,
              group->isolation ? 1 : 0);
}

static void svg_finish_effect_raster_fallback(SvgRenderContext* ctx) {
    if (!ctx || !ctx->effect_fallback.active) return;
    paint_end_effect_group(&ctx->effect_fallback.paint_list);

    RenderEffectRasterImage image = {};
    bool ok = render_effect_rasterize_paint_list(
        &ctx->effect_fallback.paint_list,
        svg_effect_group_needs_page_backdrop(&ctx->effect_fallback.group)
            ? &ctx->page_backdrop_dl
            : nullptr,
        &ctx->effect_fallback.group.bounds,
        ctx->viewport_width,
        ctx->viewport_height,
        false,
        &image,
        "[SVG_PAINT_IR]");
    ctx->paint_svg_stats.command_count += paint_list_count(&ctx->effect_fallback.paint_list);
    if (ok && svg_emit_raster_fallback_image(ctx, image.surface,
                                             image.x, image.y,
                                             image.width, image.height)) {
        ctx->paint_svg_stats.emitted_count++;
        ctx->paint_svg_stats.fallback_count++;
    } else {
        ctx->paint_svg_stats.unsupported_count++;
    }
    svg_record_page_backdrop_paint_list(ctx, &ctx->effect_fallback.paint_list);
    if (image.surface) image_surface_destroy(image.surface);
    paint_list_clear(&ctx->effect_fallback.paint_list);
    ctx->effect_fallback.active = false;
    ctx->effect_fallback.nested_depth = 0;
}

static void svg_color_to_string(Color color, char* result) {
    if (color.a == 0) {
        str_copy(result, 32, "transparent", 11);
    } else if (color.a == 255) {
        str_fmt(result, 32, "rgb(%d,%d,%d)", color.r, color.g, color.b);
    } else {
        str_fmt(result, 32, "rgba(%d,%d,%d,%.3f)", color.r, color.g, color.b, color.a / 255.0f);
    }
}

static void render_text_view_svg(SvgRenderContext* ctx, ViewText* text) {
    if (!text || !text->text_data()) return;
    // Extract the text content
    unsigned char* str = text->text_data();

    // Get text-transform from the text node's parent elements
    CssEnum text_transform = CSS_VALUE_NONE;
    DomNode* parent = text->parent;
    while (parent) {
        if (parent->is_element()) {
            DomElement* elem = lam::dom_require_element(parent);
            text_transform = get_text_transform_from_block(elem->blk);
            if (text_transform != CSS_VALUE_NONE) break;
        }
        parent = parent->parent;
    }

    TextRect *text_rect = text->rect;
    NEXT_RECT:
    float x = ctx->block.x + text_rect->x, y = ctx->block.y + text_rect->y;

    // Transform text if needed
    char* text_content = (char*)mem_alloc(text_rect->length * 4 + 2, MEM_CAT_RENDER);  // Extra for UTF-8 + trailing hyphen
    if (text_transform != CSS_VALUE_NONE) {
        // Apply text-transform character by character
        unsigned char* src = str + text_rect->start_index;
        unsigned char* src_end = src + text_rect->length;
        char* dst = text_content;
        bool is_word_start = true;

        while (src < src_end) {
            uint32_t codepoint = *src;
            int bytes = 1;

            if (codepoint >= 128) {
                bytes = str_utf8_decode((const char*)src, (size_t)(src_end - src), &codepoint);
                if (bytes <= 0) bytes = 1;
            }

            // Skip soft hyphens (U+00AD) — they are invisible in rendered output
            if (codepoint == 0x00AD) { src += bytes; continue; }

            // Track word boundaries
            if (is_space(codepoint)) {
                is_word_start = true;
                *dst++ = *src;
                src += bytes;
                continue;
            }

            // Apply transformation (full case mapping: 1 codepoint may become 2-3)
            uint32_t tt_out[3];
            int tt_count = apply_text_transform_full(codepoint, text_transform, is_word_start, tt_out);
            is_word_start = false;

            // Encode all expanded codepoints back to UTF-8
            for (int tti = 0; tti < tt_count; tti++) {
            uint32_t transformed = tt_out[tti];
            if (transformed == 0) continue;
            dst += utf8_encode(transformed, dst);
            } // end for tti

            src += bytes;
        }
        *dst = '\0';
    } else {
        // No transformation — copy while stripping soft hyphens (U+00AD = 0xC2 0xAD)
        unsigned char* src = str + text_rect->start_index;
        unsigned char* src_end = src + text_rect->length;
        char* dst = text_content;
        while (src < src_end) {
            if (src[0] == 0xC2 && src + 1 < src_end && src[1] == 0xAD) {
                src += 2;  // skip soft hyphen
            } else {
                *dst++ = (char)*src++;
            }
        }
        *dst = '\0';
    }
    // CSS Text 3 §5.2: Soft hyphen — append visible '-' when line breaks at SHY
    if (text_rect->has_trailing_hyphen) {
        size_t len = strlen(text_content);
        text_content[len] = '-';
        text_content[len + 1] = '\0';
    }

    // -webkit-line-clamp: append ellipsis character
    if (text_rect->has_trailing_ellipsis) {
        size_t len = strlen(text_content);
        // U+2026 HORIZONTAL ELLIPSIS = 0xE2 0x80 0xA6 in UTF-8
        text_content[len] = (char)0xE2;
        text_content[len + 1] = (char)0x80;
        text_content[len + 2] = (char)0xA6;
        text_content[len + 3] = '\0';
    }

    // Calculate natural text width and gap count for justify rendering.
    // NOTE: includes trailing spaces. The layout's count_justify_opportunities
    // (see layout_text.cpp) counts every space including trailing ones, so the
    // text_rect->width has been expanded based on all spaces. Stripping trailing
    // spaces here would cause the renderer to compute a smaller space_count and
    // therefore a larger word-spacing per gap, visibly over-spacing the line.
    float natural_width = 0.0f;
    int space_count = 0;
    if (ctx->font.font_handle) {
        size_t content_len = strlen(text_content);

        unsigned char* scan = (unsigned char*)text_content;
        unsigned char* content_end = scan + content_len;
        while (scan < content_end) {  // Only scan up to content_end
            if (is_space(*scan)) {
                natural_width += ctx->font.style->space_width;
                space_count++;
                scan++;
            }
            else {
                uint32_t codepoint;
                int bytes = str_utf8_decode((const char*)scan, (size_t)(content_end - scan), &codepoint);
                if (bytes <= 0) { scan++; }
                else { scan += bytes; }

                // Skip VS16 (U+FE0F) — zero-width variation selector
                if (codepoint == 0xFE0F) continue;

                FontStyleDesc _sd = font_style_desc_from_prop(ctx->font.style);
                // Use emoji font for codepoints that default to emoji presentation
                bool emoji_pres = false;
                if (scan < content_end) {
                    uint32_t peek_cp;
                    int peek_bytes = str_utf8_decode((const char*)scan, (size_t)(content_end - scan), &peek_cp);
                    if (peek_bytes > 0 && peek_cp == 0xFE0F) emoji_pres = true;
                }
                // SVG text is rendered by the viewer, which handles emoji presentation.
                // Use regular font for advance metrics to match layout dimensions.
                // VS16-preceded codepoints use emoji font for correct advance.
                LoadedGlyph* glyph = emoji_pres
                    ? font_load_glyph_emoji(ctx->font.font_handle, &_sd, codepoint, false)
                    : font_load_glyph(ctx->font.font_handle, &_sd, codepoint, false);
                if (glyph) {
                    natural_width += glyph->advance_x;
                } else {
                    natural_width += ctx->font.style->space_width;
                }
            }
        }
    }

    // Calculate word-spacing for justified text
    float word_spacing = 0.0f;
    if (space_count > 0 && natural_width > 0 && text_rect->width > natural_width) {
        // This text is justified - calculate extra space per word
        float extra_space = text_rect->width - natural_width;
        word_spacing = extra_space / space_count;
    }

    // Use CSS font-size from style, fallback to 16 if not available
    float font_size = ctx->font.style->font_size > 0 ? ctx->font.style->font_size : 16;
    // Use font ascender from FontProp (already in pixels), or fallback to 80% of font_size
    float baseline_y = y + (ctx->font.style->ascender > 0 ? ctx->font.style->ascender : font_size * 0.8f);

    int font_weight = 0;
    if (ctx->font.style->font_weight_numeric > 0) {
        font_weight = ctx->font.style->font_weight_numeric;
    } else if (ctx->font.style->font_weight == CSS_VALUE_BOLD ||
               ctx->font.style->font_weight == CSS_VALUE_BOLDER) {
        font_weight = 700;
    } else if (ctx->font.style->font_weight == CSS_VALUE_LIGHTER) {
        font_weight = 300;
    }

    bool italic = ctx->font.style->font_style == CSS_VALUE_ITALIC;
    bool has_text_deco = ctx->font.style->text_deco != CSS_VALUE_NONE &&
                         ctx->font.style->text_deco != CSS_VALUE__UNDEF;
    DomElement* parent_elem = text->parent ? text->parent->as_element() : nullptr;
    bool has_text_shadow = parent_elem && parent_elem->font &&
                           parent_elem->font->text_shadow;

    if (!has_text_deco && !has_text_shadow) {
        PaintGlyphRun run = {};
        run.font = &ctx->font;
        run.color = ctx->color;
        run.text = text_content;
        run.text_len = (int)strlen(text_content); // INT_CAST_OK: UTF-8 text run byte length is bounded by TextRect input.
        run.font_family = ctx->font.font_handle
            ? font_handle_get_family_name(ctx->font.font_handle)
            : "Arial";
        run.font_size = font_size;
        run.x = x;
        run.baseline_y = baseline_y;
        run.word_spacing = word_spacing;
        run.font_weight = font_weight;
        run.italic = italic;
        paint_glyph_run(svg_active_paint_list(ctx), &run);
        svg_lower_paint_list(ctx);

        mem_free(text_content);
        text_rect = text_rect->next;
        if (text_rect) { goto NEXT_RECT; }
        return;
    }

    // Escape XML entities in text
    size_t transformed_len = strlen(text_content);
    StrBuf* escaped_text = strbuf_new_cap(transformed_len * 2);
    for (size_t i = 0; i < transformed_len; i++) {
        char c = text_content[i];
        switch (c) {
            case '<': strbuf_append_str(escaped_text, "&lt;"); break;
            case '>': strbuf_append_str(escaped_text, "&gt;"); break;
            case '&': strbuf_append_str(escaped_text, "&amp;"); break;
            case '"': strbuf_append_str(escaped_text, "&quot;"); break;
            case '\'': strbuf_append_str(escaped_text, "&#39;"); break;
            default: strbuf_append_char(escaped_text, c); break;
        }
    }

    svg_indent(ctx);

    char color_str[32];
    svg_color_to_string(ctx->color, color_str);

    strbuf_append_format(ctx->svg_content,
        "<text x=\"%.2f\" y=\"%.2f\" font-family=\"%s\" font-size=\"%.0f\" fill=\"%s\"",
        x, baseline_y,
        ctx->font.font_handle ? font_handle_get_family_name(ctx->font.font_handle) : "Arial",
        font_size,
        color_str);

    // Add font style attributes
    // Use font_weight_numeric (100-900) when available, otherwise map CssEnum keyword
    if (font_weight > 0 && font_weight != 400) {
        strbuf_append_format(ctx->svg_content, " font-weight=\"%d\"", font_weight);
    }

    if (italic) {
        strbuf_append_str(ctx->svg_content, " font-style=\"italic\"");
    }

    // Add text decoration (skip _UNDEF which means no decoration set)
    if (ctx->font.style->text_deco != CSS_VALUE_NONE && ctx->font.style->text_deco != CSS_VALUE__UNDEF) {
        const char* deco_line = nullptr;
        if (ctx->font.style->text_deco == CSS_VALUE_UNDERLINE) deco_line = "underline";
        else if (ctx->font.style->text_deco == CSS_VALUE_OVERLINE) deco_line = "overline";
        else if (ctx->font.style->text_deco == CSS_VALUE_LINE_THROUGH) deco_line = "line-through";
        if (deco_line) {
            if (ctx->font.style->text_deco_color.a > 0) {
                Color c = ctx->font.style->text_deco_color;
                strbuf_append_format(ctx->svg_content,
                    " style=\"text-decoration: %s; text-decoration-color: rgb(%d,%d,%d);\"",
                    deco_line, c.r, c.g, c.b);
            } else {
                strbuf_append_format(ctx->svg_content, " text-decoration=\"%s\"", deco_line);
            }
        }
    }

    // Add word-spacing for justified text
    if (word_spacing > 0.01f) {
        strbuf_append_format(ctx->svg_content, " word-spacing=\"%.2f\"", word_spacing);
    }

    // Add text-shadow as CSS style attribute
    if (parent_elem && parent_elem->font && parent_elem->font->text_shadow) {
        strbuf_append_str(ctx->svg_content, " style=\"text-shadow:");
        TextShadow* ts = parent_elem->font->text_shadow;
        bool first = true;
        while (ts) {
            if (!first) strbuf_append_char(ctx->svg_content, ',');
            char ts_color[32];
            svg_color_to_string(ts->color, ts_color);
            strbuf_append_format(ctx->svg_content, " %.1fpx %.1fpx %.1fpx %s",
                ts->offset_x, ts->offset_y, ts->blur_radius, ts_color);
            first = false;
            ts = ts->next;
        }
        strbuf_append_char(ctx->svg_content, '"');
    }

    strbuf_append_format(ctx->svg_content, ">%s</text>\n", escaped_text->str);

    mem_free(text_content);  strbuf_free(escaped_text);
    text_rect = text_rect->next;
    if (text_rect) { goto NEXT_RECT; }
}

// ── SVG rounded-rect path helper ─────────────────────────────────────────────

// Kappa constant for circular arcs using cubic Bézier curves
#define SVG_KAPPA 0.5522847498f

/**
 * Append SVG path data for a rounded rectangle with per-corner radii.
 * Uses cubic Bézier curves (C commands) matching the raster backend's
 * build_rounded_rect_path() logic.
 */
static void svg_append_rounded_rect_path(StrBuf* buf, float x, float y, float w, float h,
                                          float r_tl, float r_tr, float r_br, float r_bl) {
    // start after top-left corner arc
    strbuf_append_format(buf, "M%.2f,%.2f", x + r_tl, y);
    // top edge
    strbuf_append_format(buf, " L%.2f,%.2f", x + w - r_tr, y);
    // top-right corner
    if (r_tr > 0) {
        strbuf_append_format(buf, " C%.2f,%.2f %.2f,%.2f %.2f,%.2f",
            x + w - r_tr + r_tr * SVG_KAPPA, y,
            x + w, y + r_tr - r_tr * SVG_KAPPA,
            x + w, y + r_tr);
    }
    // right edge
    strbuf_append_format(buf, " L%.2f,%.2f", x + w, y + h - r_br);
    // bottom-right corner
    if (r_br > 0) {
        strbuf_append_format(buf, " C%.2f,%.2f %.2f,%.2f %.2f,%.2f",
            x + w, y + h - r_br + r_br * SVG_KAPPA,
            x + w - r_br + r_br * SVG_KAPPA, y + h,
            x + w - r_br, y + h);
    }
    // bottom edge
    strbuf_append_format(buf, " L%.2f,%.2f", x + r_bl, y + h);
    // bottom-left corner
    if (r_bl > 0) {
        strbuf_append_format(buf, " C%.2f,%.2f %.2f,%.2f %.2f,%.2f",
            x + r_bl - r_bl * SVG_KAPPA, y + h,
            x, y + h - r_bl + r_bl * SVG_KAPPA,
            x, y + h - r_bl);
    }
    // left edge
    strbuf_append_format(buf, " L%.2f,%.2f", x, y + r_tl);
    // top-left corner
    if (r_tl > 0) {
        strbuf_append_format(buf, " C%.2f,%.2f %.2f,%.2f %.2f,%.2f",
            x, y + r_tl - r_tl * SVG_KAPPA,
            x + r_tl - r_tl * SVG_KAPPA, y,
            x + r_tl, y);
    }
    strbuf_append_str(buf, " Z");
}

/**
 * Convenience: test whether a border has any non-zero corner radius.
 */
static bool svg_has_border_radius(BorderProp* border) {
    return border && (border->radius.top_left > 0 || border->radius.top_right > 0 ||
                      border->radius.bottom_right > 0 || border->radius.bottom_left > 0);
}

static bool svg_get_uniform_border_radius(BorderProp* border, float* radius) {
    if (!svg_has_border_radius(border)) return false;
    float value = border->radius.top_left;
    if (border->radius.top_right != value ||
        border->radius.bottom_right != value ||
        border->radius.bottom_left != value) {
        return false;
    }
    if (radius) *radius = value;
    return true;
}

// ── SVG border style helpers ─────────────────────────────────────────────────

/**
 * Build SVG polygon point-string for one border side trapezoid.
 * Corners are miter-cut so adjacent sides share the diagonal corner region.
 *   side 0=top, 1=right, 2=bottom, 3=left
 */
static void svg_border_poly(char* buf, int buf_size, int side,
    float x, float y, float W, float H,
    float bwt, float bwr, float bwb, float bwl) {
    switch (side) {
        case 0: // top outer-edge TL→TR → inner-edge (TR-bwr,bwt)→(TL+bwl,bwt)
            str_fmt(buf, buf_size, "%.2f,%.2f %.2f,%.2f %.2f,%.2f %.2f,%.2f",
                x, y, x+W, y, x+W-bwr, y+bwt, x+bwl, y+bwt);
            break;
        case 1: // right
            str_fmt(buf, buf_size, "%.2f,%.2f %.2f,%.2f %.2f,%.2f %.2f,%.2f",
                x+W-bwr, y+bwt, x+W, y, x+W, y+H, x+W-bwr, y+H-bwb);
            break;
        case 2: // bottom
            str_fmt(buf, buf_size, "%.2f,%.2f %.2f,%.2f %.2f,%.2f %.2f,%.2f",
                x+bwl, y+H-bwb, x+W-bwr, y+H-bwb, x+W, y+H, x, y+H);
            break;
        case 3: // left
            str_fmt(buf, buf_size, "%.2f,%.2f %.2f,%.2f %.2f,%.2f %.2f,%.2f",
                x, y, x+bwl, y+bwt, x+bwl, y+H-bwb, x, y+H);
            break;
        default: buf[0] = '\0'; break;
    }
}

static Color svg_darken(Color c, float f) {
    Color out; out.r = (uint8_t)(c.r*f); out.g = (uint8_t)(c.g*f); out.b = (uint8_t)(c.b*f); out.a = c.a;
    return out;
}
static Color svg_lighten(Color c, float f) {
    Color out;
    out.r = (uint8_t)(c.r + (255-c.r)*f < 255 ? (int)(c.r + (255-c.r)*f) : 255);
    out.g = (uint8_t)(c.g + (255-c.g)*f < 255 ? (int)(c.g + (255-c.g)*f) : 255);
    out.b = (uint8_t)(c.b + (255-c.b)*f < 255 ? (int)(c.b + (255-c.b)*f) : 255);
    out.a = c.a;
    return out;
}

/**
 * Emit one or two SVG <polygon> elements for a single border side, handling
 * solid, double, groove, ridge, inset, and outset styles.
 */
static void svg_emit_border_side(SvgRenderContext* ctx, CssEnum style, Color c,
    float x, float y, float W, float H,
    float bwt, float bwr, float bwb, float bwl,
    int side) {  // 0=top, 1=right, 2=bottom, 3=left

    if (style == CSS_VALUE_NONE || style == CSS_VALUE_HIDDEN || c.a == 0) return;
    float bw = (side == 0) ? bwt : (side == 1) ? bwr : (side == 2) ? bwb : bwl;
    if (bw <= 0) return;

    char pts[256], col[32];
    svg_border_poly(pts, sizeof(pts), side, x, y, W, H, bwt, bwr, bwb, bwl);

    if (style == CSS_VALUE_DOUBLE && bw >= 3) {
        // Two thin fills with a gap: outer (at edge, thickness = floor(bw/3))
        // and inner (inset by bw - floor(bw/3) from outer edge)
        float lw = floorf(bw / 3.0f);
        if (lw < 1) lw = 1;

        char opts[256];
        svg_border_poly(opts, sizeof(opts), side, x, y, W, H,
            (side == 0) ? lw : bwt, (side == 1) ? lw : bwr,
            (side == 2) ? lw : bwb, (side == 3) ? lw : bwl);
        svg_color_to_string(c, col);
        svg_indent(ctx);
        strbuf_append_format(ctx->svg_content, "<polygon points=\"%s\" fill=\"%s\" />\n", opts, col);

        // Inner fill (inset the border box by bw-lw on this side)
        float offset = bw - lw;
        float ix = x + (side == 3 ? offset : 0);
        float iy = y + (side == 0 ? offset : 0);
        float iW = W - (side == 1 ? offset : 0) - (side == 3 ? offset : 0);
        float iH = H - (side == 0 ? offset : 0) - (side == 2 ? offset : 0);
        char ipts[256];
        svg_border_poly(ipts, sizeof(ipts), side, ix, iy, iW, iH,
            (side == 0) ? lw : bwt, (side == 1) ? lw : bwr,
            (side == 2) ? lw : bwb, (side == 3) ? lw : bwl);
        svg_indent(ctx);
        strbuf_append_format(ctx->svg_content, "<polygon points=\"%s\" fill=\"%s\" />\n", ipts, col);

    } else if (style == CSS_VALUE_GROOVE || style == CSS_VALUE_RIDGE) {
        Color dark  = svg_darken(c, 0.5f);
        Color light = svg_lighten(c, 0.35f);
        Color outer_c = (style == CSS_VALUE_GROOVE) ? dark : light;
        Color inner_c = (style == CSS_VALUE_GROOVE) ? light : dark;
        float hw = bw / 2.0f;

        // Outer half (at edge, thickness = hw)
        char outer_pts[256];
        svg_border_poly(outer_pts, sizeof(outer_pts), side, x, y, W, H,
            (side == 0) ? hw : bwt, (side == 1) ? hw : bwr,
            (side == 2) ? hw : bwb, (side == 3) ? hw : bwl);
        svg_color_to_string(outer_c, col);
        svg_indent(ctx);
        strbuf_append_format(ctx->svg_content, "<polygon points=\"%s\" fill=\"%s\" />\n", outer_pts, col);

        // Inner half (inset by hw on this side)
        float ix = x + (side == 3 ? hw : 0);
        float iy = y + (side == 0 ? hw : 0);
        float iW = W - (side == 1 ? hw : 0) - (side == 3 ? hw : 0);
        float iH = H - (side == 0 ? hw : 0) - (side == 2 ? hw : 0);
        char inner_pts[256];
        svg_border_poly(inner_pts, sizeof(inner_pts), side, ix, iy, iW, iH,
            (side == 0) ? hw : bwt, (side == 1) ? hw : bwr,
            (side == 2) ? hw : bwb, (side == 3) ? hw : bwl);
        svg_color_to_string(inner_c, col);
        svg_indent(ctx);
        strbuf_append_format(ctx->svg_content, "<polygon points=\"%s\" fill=\"%s\" />\n", inner_pts, col);

    } else if (style == CSS_VALUE_INSET || style == CSS_VALUE_OUTSET) {
        // inset: top+left dark, bottom+right light; outset: opposite
        Color dark  = svg_darken(c, 0.5f);
        Color light = svg_lighten(c, 0.35f);
        Color side_c;
        if (style == CSS_VALUE_INSET)
            side_c = (side == 0 || side == 3) ? dark : light;
        else
            side_c = (side == 0 || side == 3) ? light : dark;
        svg_color_to_string(side_c, col);
        svg_indent(ctx);
        strbuf_append_format(ctx->svg_content, "<polygon points=\"%s\" fill=\"%s\" />\n", pts, col);

    } else {
        // solid / dotted / dashed — render as filled trapezoid
        // (dash patterns are a raster-only feature; SVG uses fill for correctness)
        svg_color_to_string(c, col);
        svg_indent(ctx);
        strbuf_append_format(ctx->svg_content, "<polygon points=\"%s\" fill=\"%s\" />\n", pts, col);
    }
}

// ─────────────────────────────────────────────────────────────────────────────

static void render_bound_svg(SvgRenderContext* ctx, ViewBlock* view) {
    if (!view->bound) return;

    float x = ctx->block.x + view->x;
    float y = ctx->block.y + view->y;
    float width = view->width;
    float height = view->height;

    if (render_paint_boundary_emit_simple(svg_active_paint_list(ctx), view, x, y)) {
        svg_lower_paint_list(ctx);
        return;
    }

    if (ctx->effect_fallback.active && view->bound->box_shadow) {
        render_paint_boundary_emit_outer_shadows(svg_active_paint_list(ctx), view, x, y);
    }

    // Render background
    if (view->bound->background && view->bound->background->color.a > 0) {
        // Check for border radius
        if (svg_has_border_radius(view->bound->border)) {
            BorderProp* border = view->bound->border;
            float radius = 0.0f;
            if (svg_get_uniform_border_radius(border, &radius)) {
                paint_fill_rounded_rect(svg_active_paint_list(ctx), x, y, width, height,
                                        radius, radius,
                                        view->bound->background->color);
                svg_lower_paint_list(ctx);
            } else {
                Rect rect = {x, y, width, height};
                Corner radius_shape = {};
                radius_shape.top_left = border->radius.top_left;
                radius_shape.top_right = border->radius.top_right;
                radius_shape.bottom_right = border->radius.bottom_right;
                radius_shape.bottom_left = border->radius.bottom_left;
                radius_shape.top_left_y = border->radius.top_left;
                radius_shape.top_right_y = border->radius.top_right;
                radius_shape.bottom_right_y = border->radius.bottom_right;
                radius_shape.bottom_left_y = border->radius.bottom_left;
                RdtPath* path = render_path_create_rounded_rect(rect, &radius_shape);
                paint_fill_path(svg_active_paint_list(ctx), path, view->bound->background->color,
                                RDT_FILL_WINDING, nullptr);
                svg_lower_paint_list(ctx);
                rdt_path_free(path);
            }
        } else {
            paint_fill_rect(svg_active_paint_list(ctx), x, y, width, height,
                            view->bound->background->color);
            svg_lower_paint_list(ctx);
        }
    }

    // Render background gradient (linear or radial)
    if (view->bound->background && view->bound->background->gradient_type != GRADIENT_NONE) {
        BackgroundProp* bg = view->bound->background;
        if (bg->gradient_type == GRADIENT_LINEAR && bg->linear_gradient &&
            bg->linear_gradient->stop_count >= 2) {
            int stop_count = bg->linear_gradient->stop_count;
            RdtGradientStop* stops = (RdtGradientStop*)mem_alloc(
                (size_t)stop_count * sizeof(RdtGradientStop), MEM_CAT_RENDER);
            BoundaryLinearGradientPaint gradient = {};
            if (stops &&
                render_paint_boundary_build_linear_gradient(view, x, y, stops,
                                                            stop_count, &gradient)) {
                paint_fill_linear_gradient(svg_active_paint_list(ctx), gradient.path,
                                           gradient.x1, gradient.y1,
                                           gradient.x2, gradient.y2,
                                           gradient.stops, gradient.stop_count,
                                           RDT_FILL_WINDING, nullptr, nullptr);
                svg_lower_paint_list(ctx);
                rdt_path_free(gradient.path);
            }
            if (stops) mem_free(stops);
        } else if (bg->gradient_type == GRADIENT_RADIAL && bg->radial_gradient &&
                   bg->radial_gradient->stop_count >= 2) {
            int stop_count = bg->radial_gradient->stop_count;
            RdtGradientStop* stops = (RdtGradientStop*)mem_alloc(
                (size_t)stop_count * sizeof(RdtGradientStop), MEM_CAT_RENDER);
            BoundaryRadialGradientPaint gradient = {};
            if (stops &&
                render_paint_boundary_build_radial_gradient(view, x, y, stops,
                                                            stop_count, &gradient)) {
                paint_fill_radial_gradient(svg_active_paint_list(ctx), gradient.path,
                                           gradient.cx, gradient.cy, gradient.r,
                                           gradient.stops, gradient.stop_count,
                                           RDT_FILL_WINDING, nullptr, nullptr);
                svg_lower_paint_list(ctx);
                rdt_path_free(gradient.path);
            }
            if (stops) mem_free(stops);
        }
    }

    // Render background image (background-image, background-size, background-position, background-repeat)
    if (view->bound->background && view->bound->background->image) {
        BackgroundProp* bg = view->bound->background;
        const char* img_url = bg->image;

        // Border widths and padding in CSS px (same coordinate space as x/y/width/height in SVG path)
        float bwt = 0, bwr = 0, bwb = 0, bwl = 0;
        float pt = 0, pr = 0, pb = 0, pl = 0;
        if (view->bound->border) {
            bwt = view->bound->border->width.top;
            bwr = view->bound->border->width.right;
            bwb = view->bound->border->width.bottom;
            bwl = view->bound->border->width.left;
        }
        pt = view->bound->padding.top;
        pr = view->bound->padding.right;
        pb = view->bound->padding.bottom;
        pl = view->bound->padding.left;

        // Compute positioning area (background-origin, default: padding-box)
        CssEnum origin = bg->bg_origin ? bg->bg_origin : CSS_VALUE_PADDING_BOX;
        float ox = x, oy = y, ow = width, oh = height;
        if (origin == CSS_VALUE_PADDING_BOX || origin == CSS_VALUE_CONTENT_BOX) {
            ox += bwl; oy += bwt; ow -= bwl + bwr; oh -= bwt + bwb;
        }
        if (origin == CSS_VALUE_CONTENT_BOX) {
            ox += pl; oy += pt; ow -= pl + pr; oh -= pt + pb;
        }
        if (ow < 0) ow = 0;
        if (oh < 0) oh = 0;

        // Compute paint area (background-clip, default: border-box)
        CssEnum clip_box = bg->bg_clip ? bg->bg_clip : CSS_VALUE_BORDER_BOX;
        float cx = x, cy = y, cw = width, ch = height;
        if (clip_box == CSS_VALUE_PADDING_BOX || clip_box == CSS_VALUE_CONTENT_BOX) {
            cx += bwl; cy += bwt; cw -= bwl + bwr; ch -= bwt + bwb;
        }
        if (clip_box == CSS_VALUE_CONTENT_BOX) {
            cx += pl; cy += pt; cw -= pl + pr; ch -= pt + pb;
        }
        if (cw < 0) cw = 0;
        if (ch < 0) ch = 0;

        // Compute rendered image size
        float img_w = ow, img_h = oh;
        const char* preserve_ratio = "none";
        if (bg->bg_size_type == CSS_VALUE_COVER) {
            img_w = ow; img_h = oh;
            preserve_ratio = "xMidYMid slice";
        } else if (bg->bg_size_type == CSS_VALUE_CONTAIN) {
            img_w = ow; img_h = oh;
            preserve_ratio = "xMidYMid meet";
        } else if (bg->bg_size_type == (CssEnum)0) {
            // Explicit dimensions
            if (!bg->bg_size_width_auto) {
                img_w = bg->bg_size_width_is_percent ? ow * bg->bg_size_width / 100.0f : bg->bg_size_width;
            }
            if (!bg->bg_size_height_auto) {
                img_h = bg->bg_size_height_is_percent ? oh * bg->bg_size_height / 100.0f : bg->bg_size_height;
            }
            preserve_ratio = "none";
        }

        // Compute image position within origin box
        float pos_x = ox, pos_y = oy;
        if (bg->bg_position_set) {
            pos_x = bg->bg_position_x_is_percent
                ? ox + (ow - img_w) * bg->bg_position_x / 100.0f
                : ox + bg->bg_position_x;
            pos_y = bg->bg_position_y_is_percent
                ? oy + (oh - img_h) * bg->bg_position_y / 100.0f
                : oy + bg->bg_position_y;
        }

        bool no_repeat = (bg->bg_repeat_x == CSS_VALUE_NO_REPEAT && bg->bg_repeat_y == CSS_VALUE_NO_REPEAT);
        uintptr_t view_id = (uintptr_t)view;

        if (no_repeat) {
            // Single image with a clip-path limited to the paint area
            char clip_id[64];
            str_fmt(clip_id, sizeof(clip_id), "bgclip-%lx", (unsigned long)view_id);
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<defs><clipPath id=\"%s\"><rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\"/></clipPath></defs>\n",
                clip_id, cx, cy, cw, ch);
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<image x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" "
                "preserveAspectRatio=\"%s\" href=\"%s\" clip-path=\"url(#%s)\" />\n",
                pos_x, pos_y, img_w, img_h, preserve_ratio, img_url, clip_id);
        } else {
            // Tiled image via SVG <pattern>
            char pat_id[64], clip_id[64];
            str_fmt(pat_id,  sizeof(pat_id),  "bgpat-%lx",  (unsigned long)view_id);
            str_fmt(clip_id, sizeof(clip_id), "bgclip-%lx", (unsigned long)view_id);
            svg_indent(ctx);
            // pattern origin at pos_x/pos_y so tiling is offset correctly
            strbuf_append_format(ctx->svg_content,
                "<defs>"
                "<clipPath id=\"%s\"><rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\"/></clipPath>"
                "<pattern id=\"%s\" x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" patternUnits=\"userSpaceOnUse\">"
                "<image x=\"0\" y=\"0\" width=\"%.2f\" height=\"%.2f\" preserveAspectRatio=\"%s\" href=\"%s\"/>"
                "</pattern>"
                "</defs>\n",
                clip_id, cx, cy, cw, ch,
                pat_id, pos_x, pos_y, img_w, img_h,
                img_w, img_h, preserve_ratio, img_url);
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" "
                "fill=\"url(#%s)\" clip-path=\"url(#%s)\" />\n",
                cx, cy, cw, ch, pat_id, clip_id);
        }
    }

    // Render borders with style-aware per-side SVG polygons
    if (view->bound->border) {
        BorderProp* border = view->bound->border;
        float bwt = border->width.top, bwr = border->width.right;
        float bwb = border->width.bottom, bwl = border->width.left;

        // When border-radius is present, clip border polygons to the outer
        // rounded rect — matching the raster path's radius_clip approach.
        bool has_radius = svg_has_border_radius(border);
        if (has_radius) {
            char clip_id[64];
            str_fmt(clip_id, sizeof(clip_id), "border-clip-%lx", (unsigned long)(uintptr_t)view);
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content, "<defs><clipPath id=\"%s\"><path d=\"", clip_id);
            svg_append_rounded_rect_path(ctx->svg_content, x, y, width, height,
                border->radius.top_left, border->radius.top_right,
                border->radius.bottom_right, border->radius.bottom_left);
            strbuf_append_str(ctx->svg_content, "\"/></clipPath></defs>\n");
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content, "<g clip-path=\"url(#%s)\">\n", clip_id);
        }

        // Top
        svg_emit_border_side(ctx, border->top_style, border->top_color,
            x, y, width, height, bwt, bwr, bwb, bwl, 0);
        // Right
        svg_emit_border_side(ctx, border->right_style, border->right_color,
            x, y, width, height, bwt, bwr, bwb, bwl, 1);
        // Bottom
        svg_emit_border_side(ctx, border->bottom_style, border->bottom_color,
            x, y, width, height, bwt, bwr, bwb, bwl, 2);
        // Left
        svg_emit_border_side(ctx, border->left_style, border->left_color,
            x, y, width, height, bwt, bwr, bwb, bwl, 3);

        if (has_radius) {
            svg_indent(ctx);
            strbuf_append_str(ctx->svg_content, "</g>\n");
        }
    }

    // Render outline (outside border-box)
    if (view->bound->outline) {
        OutlineProp* outline = view->bound->outline;
        if (outline->width > 0 && outline->style != CSS_VALUE_NONE &&
            outline->style != CSS_VALUE_HIDDEN && outline->color.a > 0) {

            float expand = outline->width * 0.5f + outline->offset;
            float ox = x - expand;
            float oy = y - expand;
            float ow = width + expand * 2;
            float oh = height + expand * 2;

            char outline_color[32];
            svg_color_to_string(outline->color, outline_color);

            const char* dash_attr = "";
            char dash_buf[64] = {0};
            if (outline->style == CSS_VALUE_DOTTED) {
                str_fmt(dash_buf, sizeof(dash_buf),
                    " stroke-dasharray=\"%.1f,%.1f\"", outline->width, outline->width * 2);
                dash_attr = dash_buf;
            } else if (outline->style == CSS_VALUE_DASHED) {
                str_fmt(dash_buf, sizeof(dash_buf),
                    " stroke-dasharray=\"%.1f,%.1f\"", outline->width * 3, outline->width * 3);
                dash_attr = dash_buf;
            }

            svg_indent(ctx);
            // Use rounded rect if border has radius
            if (svg_has_border_radius(view->bound->border)) {
                BorderProp* border = view->bound->border;
                float r_tl = fmaxf(0, border->radius.top_left + expand);
                float r_tr = fmaxf(0, border->radius.top_right + expand);
                float r_br = fmaxf(0, border->radius.bottom_right + expand);
                float r_bl = fmaxf(0, border->radius.bottom_left + expand);
                strbuf_append_format(ctx->svg_content, "<path d=\"");
                svg_append_rounded_rect_path(ctx->svg_content, ox, oy, ow, oh, r_tl, r_tr, r_br, r_bl);
                strbuf_append_format(ctx->svg_content,
                    "\" fill=\"none\" stroke=\"%s\" stroke-width=\"%.1f\"%s />\n",
                    outline_color, outline->width, dash_attr);
            } else {
                strbuf_append_format(ctx->svg_content,
                    "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" "
                    "fill=\"none\" stroke=\"%s\" stroke-width=\"%.1f\"%s />\n",
                    ox, oy, ow, oh, outline_color, outline->width, dash_attr);
            }
        }
    }
}

// Render multi-column rules (vertical lines between columns)
static void render_column_rules_svg(SvgRenderContext* ctx, ViewBlock* block) {
    if (!block->multicol) return;

    MultiColumnProp* mc = block->multicol;

    // Only render rules between columns that actually received content.
    int rule_column_count = mc->computed_used_column_count > 0
        ? mc->computed_used_column_count
        : mc->computed_column_count;
    if (rule_column_count <= 1 || mc->rule_width <= 0 ||
        mc->rule_style == CSS_VALUE_NONE) {
        return;
    }

    float column_width = mc->computed_column_width;
    float gap = mc->column_gap_is_normal ? 16.0f : mc->column_gap;

    // Calculate block position
    float block_x = ctx->block.x + block->x;
    float block_y = ctx->block.y + block->y;

    // Adjust for padding
    if (block->bound) {
        block_x += block->bound->padding.left;
        block_y += block->bound->padding.top;
    }

    // Rule height is the content area height
    float rule_height = block->height;
    if (block->bound) {
        rule_height -= block->bound->padding.top + block->bound->padding.bottom;
        if (block->bound->border) {
            rule_height -= block->bound->border->width.top + block->bound->border->width.bottom;
        }
    }

    // Ensure minimum rule height - compute from children if needed
    if (rule_height <= 0) {
        View* child = static_cast<View*>(block->first_child);
        float max_bottom = 0;
        while (child) {
            if (child->is_element()) {
                ViewBlock* child_block = lam::view_require_block(child);
                float child_bottom = child_block->y + child_block->height;
                if (child_bottom > max_bottom) max_bottom = child_bottom;
            }
            child = child->next();
        }
        rule_height = max_bottom;
    }

    if (rule_height <= 0) return;

    log_debug("[MULTICOL SVG] Rendering %d column rules, width=%.1f, style=%d, height=%.1f",
              rule_column_count - 1, mc->rule_width, mc->rule_style, rule_height);

    // Draw rule between each pair of columns
    for (int i = 0; i < rule_column_count - 1; i++) {
        float rule_x = block_x + (i + 1) * column_width + i * gap + gap / 2.0f;
        if (mc->rule_style == CSS_VALUE_DOUBLE) {
            float thin_width = mc->rule_width / 3.0f;
            float offset = mc->rule_width / 2.0f;
            RdtPath* left = rdt_path_new();
            RdtPath* right = rdt_path_new();
            if (left && right) {
                rdt_path_move_to(left, rule_x - offset, block_y);
                rdt_path_line_to(left, rule_x - offset, block_y + rule_height);
                paint_stroke_path(svg_active_paint_list(ctx), left, mc->rule_color, thin_width,
                                  RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0.0f,
                                  nullptr);
                svg_lower_paint_list(ctx);

                rdt_path_move_to(right, rule_x + offset, block_y);
                rdt_path_line_to(right, rule_x + offset, block_y + rule_height);
                paint_stroke_path(svg_active_paint_list(ctx), right, mc->rule_color, thin_width,
                                  RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0.0f,
                                  nullptr);
                svg_lower_paint_list(ctx);
            }
            if (left) rdt_path_free(left);
            if (right) rdt_path_free(right);
        } else {
            float* dash = nullptr;
            int dash_count = 0;
            float dash_pattern[2];
            if (mc->rule_style == CSS_VALUE_DOTTED) {
                dash_pattern[0] = mc->rule_width;
                dash_pattern[1] = mc->rule_width * 2.0f;
                dash = dash_pattern;
                dash_count = 2;
            } else if (mc->rule_style == CSS_VALUE_DASHED) {
                dash_pattern[0] = mc->rule_width * 3.0f;
                dash_pattern[1] = mc->rule_width * 2.0f;
                dash = dash_pattern;
                dash_count = 2;
            }

            RdtPath* path = rdt_path_new();
            if (path) {
                rdt_path_move_to(path, rule_x, block_y);
                rdt_path_line_to(path, rule_x, block_y + rule_height);
                paint_stroke_path(svg_active_paint_list(ctx), path, mc->rule_color,
                                  mc->rule_width, RDT_CAP_BUTT, RDT_JOIN_MITER,
                                  dash, dash_count, 0.0f, nullptr);
                svg_lower_paint_list(ctx);
                rdt_path_free(path);
            }
        }

        log_debug("[MULTICOL SVG] Rule %d at x=%.1f, height=%.1f", i, rule_x, rule_height);
    }
}

static bool svg_build_transform_matrix(const TransformProp* tp, float elem_x, float elem_y,
                                       float elem_w, float elem_h, RdtMatrix* out_matrix) {
    if (!tp || !tp->functions) return false;

    // Compose 2D affine matrix: [a c e; b d f; 0 0 1]
    // Identity:
    double ma = 1, mb = 0, mc = 0, md = 1, me = 0, mf = 0;

    // Resolve transform-origin
    float ox = tp->origin_x_percent ? elem_x + elem_w * tp->origin_x / 100.0f : elem_x + tp->origin_x;
    float oy = tp->origin_y_percent ? elem_y + elem_h * tp->origin_y / 100.0f : elem_y + tp->origin_y;

    // Pre-translate for transform-origin
    me += ox; mf += oy;

    for (TransformFunction* tf = tp->functions; tf; tf = tf->next) {
        // accumulate: result = result * local
        double la=1, lb=0, lc=0, ld=1, le=0, lf=0;
        switch (tf->type) {
            case TRANSFORM_TRANSLATE:
            case TRANSFORM_TRANSLATEX:
            case TRANSFORM_TRANSLATEY: {
                float tx = tf->params.translate.x, ty = tf->params.translate.y;
                if (!isnan(tf->translate_x_percent)) tx = tf->translate_x_percent * elem_w / 100.0f;
                if (!isnan(tf->translate_y_percent)) ty = tf->translate_y_percent * elem_h / 100.0f;
                le = tx; lf = ty;
                break;
            }
            case TRANSFORM_TRANSLATE3D:
            case TRANSFORM_TRANSLATEZ:
                le = tf->params.translate3d.x; lf = tf->params.translate3d.y;
                break;
            case TRANSFORM_SCALE:
            case TRANSFORM_SCALEX:
            case TRANSFORM_SCALEY:
                la = tf->params.scale.x > 0 ? tf->params.scale.x : 1.0;
                ld = tf->params.scale.y > 0 ? tf->params.scale.y : 1.0;
                break;
            case TRANSFORM_ROTATE:
            case TRANSFORM_ROTATEZ: {
                double ang = tf->params.angle;  // radians
                la = cos(ang); lc = -sin(ang); lb = sin(ang); ld = cos(ang);
                break;
            }
            case TRANSFORM_SKEWX:
                lc = tan((double)tf->params.angle);
                break;
            case TRANSFORM_SKEWY:
                lb = tan((double)tf->params.angle);
                break;
            case TRANSFORM_MATRIX:
                la = tf->params.matrix.a; lb = tf->params.matrix.b;
                lc = tf->params.matrix.c; ld = tf->params.matrix.d;
                le = tf->params.matrix.e; lf = tf->params.matrix.f;
                break;
            default:
                break;
        }
        // result = result * L
        double na = ma*la + mc*lb, nb = mb*la + md*lb;
        double nc = ma*lc + mc*ld, nd = mb*lc + md*ld;
        double ne = ma*le + mc*lf + me, nf = mb*le + md*lf + mf;
        ma=na; mb=nb; mc=nc; md=nd; me=ne; mf=nf;
    }

    // Post-translate: undo transform-origin shift
    me -= ox; mf -= oy;

    // Skip if effectively identity
    bool is_identity = (fabs(ma-1)<1e-5 && fabs(mb)<1e-5 && fabs(mc)<1e-5
                     && fabs(md-1)<1e-5 && fabs(me)<1e-5 && fabs(mf)<1e-5);
    if (is_identity) return false;

    if (out_matrix) {
        out_matrix->e11 = ma;
        out_matrix->e12 = mc;
        out_matrix->e13 = me;
        out_matrix->e21 = mb;
        out_matrix->e22 = md;
        out_matrix->e23 = mf;
        out_matrix->e31 = 0.0f;
        out_matrix->e32 = 0.0f;
        out_matrix->e33 = 1.0f;
    }
    return true;
}

// ============================================================================
// SVG RenderBackend vtable callbacks
// ============================================================================

static void svg_cb_render_bound(void* vctx, ViewBlock* view, float abs_x, float abs_y) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    // render_bound_svg reads ctx->block.{x,y} + view->{x,y}
    ctx->block.x = abs_x - view->x;
    ctx->block.y = abs_y - view->y;
    render_bound_svg(ctx, view);
}

static void svg_cb_render_text(void* vctx, ViewText* text, float abs_x, float abs_y,
                               FontBox* font, Color color) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    ctx->block.x = abs_x;
    ctx->block.y = abs_y;
    ctx->font = *font;
    ctx->color = color;
    render_text_view_svg(ctx, text);
}

static void svg_cb_render_image(void* vctx, ViewBlock* block, float abs_x, float abs_y) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (!block->embed || !block->embed->img) return;
    ImageSurface* img = block->embed->img;

    BlockBlot image_block = {};
    image_block.x = abs_x - block->x;
    image_block.y = abs_y - block->y;
    Rect content_rect = render_geometry_block_content_rect(&image_block, block, 1.0f);
    float img_width = content_rect.width;
    float img_height = content_rect.height;

    log_debug("[SVG IMAGE RENDER] url=%s, format=%d, img_size=%dx%d, view_size=%.0fx%.0f, pos=(%.0f,%.0f)",
              img->url && img->url->href ? img->url->href->chars : "unknown",
              img->format, img->width, img->height,
              img_width, img_height, content_rect.x, content_rect.y);

    if (img->url && img->url->href) {
        paint_draw_image_resource(svg_active_paint_list(ctx), img,
                                  content_rect.x, content_rect.y,
                                  img_width, img_height, 255, nullptr);
        svg_lower_paint_list(ctx);
    }
}

static void svg_cb_render_inline_svg(void* vctx, ViewBlock* block, float abs_x, float abs_y,
                                     FontBox* font, Color color) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (!ctx || !block) return;
    DomElement* dom_elem = lam::dom_require_element(lam::view_dom_node(block));
    if (!dom_elem || !dom_elem->native_element) return;

    BlockBlot block_context = {};
    block_context.x = abs_x - block->x;
    block_context.y = abs_y - block->y;
    Rect content_rect = render_geometry_block_content_rect(&block_context, block, 1.0f);
    if (content_rect.width <= 0.0f || content_rect.height <= 0.0f) return;

    Color initial_current_color = color;
    Color initial_fill_color = {};
    Color initial_stroke_color = {};
    Color* current_color_ptr = &initial_current_color;
    Color* fill_color_ptr = nullptr;
    Color* stroke_color_ptr = nullptr;
    bool initial_fill_none = false;
    bool initial_stroke_none = true;
    float initial_stroke_width = -1.0f;
    if (block->in_line && block->in_line->has_color) {
        initial_current_color = block->in_line->color;
    }
    if (block->in_line && block->in_line->has_svg_fill) {
        if (block->in_line->svg_fill_none) {
            initial_fill_none = true;
        } else {
            initial_fill_color = block->in_line->svg_fill_color;
            fill_color_ptr = &initial_fill_color;
        }
    }
    if (block->in_line && block->in_line->has_svg_stroke) {
        if (block->in_line->svg_stroke_none) {
            initial_stroke_none = true;
        } else {
            initial_stroke_color = block->in_line->svg_stroke_color;
            stroke_color_ptr = &initial_stroke_color;
            initial_stroke_none = false;
        }
    }
    if (block->in_line && block->in_line->has_svg_stroke_width) {
        initial_stroke_width = block->in_line->svg_stroke_width;
    }

    RdtMatrix transform = rdt_matrix_translate(content_rect.x, content_rect.y);
    Bound content_clip = {content_rect.x, content_rect.y,
                          content_rect.x + content_rect.width,
                          content_rect.y + content_rect.height};
    PaintSvgSubscene subscene = {};
    render_svg_build_subscene(&subscene,
                              dom_elem->native_element,
                              content_rect.width,
                              content_rect.height,
                              nullptr,
                              1.0f,
                              ctx->ui_context ? ctx->ui_context->font_ctx : nullptr,
                              &transform,
                              &content_clip,
                              current_color_ptr,
                              fill_color_ptr,
                              nullptr,
                              1.0f,
                              initial_fill_none,
                              stroke_color_ptr,
                              initial_stroke_none,
                              initial_stroke_width);
    paint_svg_subscene(svg_active_paint_list(ctx), &subscene);
    svg_lower_paint_list(ctx);
    if (font) ctx->font = *font;
    ctx->color = color;
}

static void svg_cb_begin_block_children(void* vctx, ViewBlock* block) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    svg_indent(ctx);
    strbuf_append_format(ctx->svg_content, "<g class=\"block\" data-element=\"%s\">\n",
                         block->node_name());
    ctx->indent_level++;
}

static void svg_cb_end_block_children(void* vctx, ViewBlock* block) {
    (void)block;
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    ctx->indent_level--;
    svg_indent(ctx);
    strbuf_append_str(ctx->svg_content, "</g>\n");
}

static void svg_cb_begin_inline_children(void* vctx, ViewSpan* span) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    svg_indent(ctx);
    strbuf_append_format(ctx->svg_content, "<g class=\"inline\" data-element=\"%s\">\n",
                         span->node_name());
    ctx->indent_level++;
}

static void svg_cb_end_inline_children(void* vctx, ViewSpan* span) {
    (void)span;
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    ctx->indent_level--;
    svg_indent(ctx);
    strbuf_append_str(ctx->svg_content, "</g>\n");
}

static void svg_cb_begin_effect_group(void* vctx, const PaintEffectGroup* group) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (ctx->effect_fallback.active) {
        paint_begin_effect_group(&ctx->effect_fallback.paint_list, group);
        ctx->effect_fallback.nested_depth++;
        return;
    }
    if (svg_effect_group_needs_raster_fallback(group)) {
        svg_begin_effect_raster_fallback(ctx, group);
        return;
    }
    paint_begin_effect_group(&ctx->paint_list, group);
    svg_lower_paint_list(ctx);
}

static void svg_cb_end_effect_group(void* vctx) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (ctx->effect_fallback.active) {
        if (ctx->effect_fallback.nested_depth > 0) {
            paint_end_effect_group(&ctx->effect_fallback.paint_list);
            ctx->effect_fallback.nested_depth--;
        } else {
            svg_finish_effect_raster_fallback(ctx);
        }
        return;
    }
    paint_end_effect_group(&ctx->paint_list);
    svg_lower_paint_list(ctx);
}

static void svg_cb_begin_transform(void* vctx, ViewBlock* block, float abs_x, float abs_y) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (!block) return;
    if (ctx->transform_emitted_depth >= 64) {
        log_error("[SVG_PAINT_IR] transform stack overflow while rendering %s",
                  block->node_name());
        ctx->transform_emitted_overflow_depth++;
        return;
    }

    RdtMatrix transform = {};
    bool has = svg_build_transform_matrix(
        block->transform,
        abs_x, abs_y,
        block->width, block->height,
        &transform);
    ctx->transform_emitted_stack[ctx->transform_emitted_depth++] = has;
    if (has) {
        paint_push_transform(svg_active_paint_list(ctx), &transform);
        svg_lower_paint_list(ctx);
    }
}

static void svg_cb_end_transform(void* vctx) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (ctx->transform_emitted_overflow_depth > 0) {
        ctx->transform_emitted_overflow_depth--;
        return;
    }
    if (ctx->transform_emitted_depth <= 0) {
        log_error("[SVG_PAINT_IR] transform stack underflow");
        return;
    }
    bool emitted = ctx->transform_emitted_stack[--ctx->transform_emitted_depth];
    if (emitted) {
        paint_pop_transform(svg_active_paint_list(ctx));
        svg_lower_paint_list(ctx);
    }
}

static void svg_cb_render_marker(void* vctx, ViewSpan* marker, float abs_x, float abs_y,
                                  FontBox* font, Color color) {
    if (!marker || !marker->is_element()) return;
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;

    DomElement* elem = lam::dom_require_element(lam::view_dom_node(marker));
    MarkerProp* marker_prop = (MarkerProp*)elem->blk;
    if (!marker_prop) return;

    float x = abs_x + marker->x;
    float y = abs_y + marker->y;
    float width = marker_prop->width;
    float bullet_size = marker_prop->bullet_size;
    CssEnum marker_type = marker_prop->marker_type;

    char color_str[32];
    svg_color_to_string(color, color_str);

    float font_size = font->style && font->style->font_size > 0 ? font->style->font_size : 16;
    float center_x = x + width - font_size;
    float center_y = y + marker->height / 2.0f;

    switch (marker_type) {
        case CSS_VALUE_DISC: {
            float radius = bullet_size / 2.0f;
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"%.2f\" fill=\"%s\" />\n",
                center_x, center_y, radius, color_str);
            break;
        }
        case CSS_VALUE_CIRCLE: {
            float radius = bullet_size / 2.0f;
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"%.2f\" fill=\"none\" stroke=\"%s\" stroke-width=\"1\" />\n",
                center_x, center_y, radius, color_str);
            break;
        }
        case CSS_VALUE_SQUARE: {
            float sx = center_x - bullet_size / 2.0f;
            float sy = center_y - bullet_size / 2.0f;
            paint_fill_rect(svg_active_paint_list(ctx), sx, sy, bullet_size, bullet_size, color);
            svg_lower_paint_list(ctx);
            break;
        }
        case CSS_VALUE_DISCLOSURE_CLOSED: {
            float tri_size = bullet_size * 1.6f;
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<polygon points=\"%.2f,%.2f %.2f,%.2f %.2f,%.2f\" fill=\"%s\" />\n",
                center_x, center_y - tri_size / 2.0f,
                center_x + tri_size, center_y,
                center_x, center_y + tri_size / 2.0f,
                color_str);
            break;
        }
        case CSS_VALUE_DISCLOSURE_OPEN: {
            float tri_size = bullet_size * 1.6f;
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<polygon points=\"%.2f,%.2f %.2f,%.2f %.2f,%.2f\" fill=\"%s\" />\n",
                center_x - tri_size / 2.0f, center_y - tri_size / 2.0f,
                center_x + tri_size / 2.0f, center_y - tri_size / 2.0f,
                center_x, center_y + tri_size / 2.0f,
                color_str);
            break;
        }
        default: {
            // text markers (decimal, roman, alpha, etc.)
            if (marker_prop->text_content && *marker_prop->text_content) {
                float baseline_y = y + (font->style && font->style->ascender > 0 ? font->style->ascender : font_size * 0.8f);
                // escape XML entities
                const char* src = marker_prop->text_content;
                StrBuf* escaped = strbuf_new_cap(strlen(src) * 2);
                while (*src) {
                    switch (*src) {
                        case '<': strbuf_append_str(escaped, "&lt;"); break;
                        case '>': strbuf_append_str(escaped, "&gt;"); break;
                        case '&': strbuf_append_str(escaped, "&amp;"); break;
                        default: strbuf_append_char(escaped, *src); break;
                    }
                    src++;
                }
                const char* family = font->font_handle
                    ? font_handle_get_family_name(font->font_handle) : "Arial";
                // right-align text in marker box
                float text_x = x + width;
                svg_indent(ctx);
                strbuf_append_format(ctx->svg_content,
                    "<text x=\"%.2f\" y=\"%.2f\" font-family=\"%s\" font-size=\"%.0f\" "
                    "fill=\"%s\" text-anchor=\"end\">%s</text>\n",
                    text_x, baseline_y, family, font_size, color_str,
                    escaped->str);
                strbuf_free(escaped);
            }
            break;
        }
    }
}

static void svg_cb_render_column_rules(void* vctx, ViewBlock* block, float abs_x, float abs_y) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    // render_column_rules_svg reads ctx->block.{x,y} + block->{x,y}
    ctx->block.x = abs_x - block->x;
    ctx->block.y = abs_y - block->y;
    render_column_rules_svg(ctx, block);
}

static RenderBackend svg_make_backend(SvgRenderContext* ctx) {
    RenderBackend b = {};
    b.ctx                   = ctx;
    b.render_bound          = svg_cb_render_bound;
    b.render_text           = svg_cb_render_text;
    b.render_image          = svg_cb_render_image;
    b.render_inline_svg     = svg_cb_render_inline_svg;
    b.begin_block_children  = svg_cb_begin_block_children;
    b.end_block_children    = svg_cb_end_block_children;
    b.begin_inline_children = svg_cb_begin_inline_children;
    b.end_inline_children   = svg_cb_end_inline_children;
    b.begin_effect_group    = svg_cb_begin_effect_group;
    b.end_effect_group      = svg_cb_end_effect_group;
    b.begin_transform       = svg_cb_begin_transform;
    b.end_transform         = svg_cb_end_transform;
    b.render_column_rules   = svg_cb_render_column_rules;
    b.render_marker         = svg_cb_render_marker;
    b.on_font_change        = NULL;
    return b;
}

// Calculate the actual content bounds recursively
void calculate_content_bounds(View* view, int* max_x, int* max_y) {
    if (!view) return;

    if (view->view_type == RDT_VIEW_BLOCK) {
        ViewBlock* block = lam::view_require_block(view);
        int right = block->x + block->width;
        int bottom = block->y + block->height;
        if (right > *max_x) *max_x = right;
        if (bottom > *max_y) *max_y = bottom;
    }
    else if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require_text(view);
        int right = text->x + text->width;
        int bottom = text->y + text->height;
        if (right > *max_x) *max_x = right;
        if (bottom > *max_y) *max_y = bottom;
    }

    // Recursively check children
    if (view->view_type >= RDT_VIEW_INLINE) {
        ViewElement* group = lam::view_require_element(view);
        View* child = group->first_child;
        while (child) {
            calculate_content_bounds(child, max_x, max_y);
            child = child->next_sibling;
        }
    }
}

// Render caret to SVG
static void render_caret_svg(SvgRenderContext* ctx, DocState* state) {
    View* view = NULL;
    float caret_x = 0, caret_y = 0, caret_height = 0;
    float iframe_offset_x = 0, iframe_offset_y = 0;
    bool caret_visible = false;
    if (!caret_get_render_snapshot(state, &view, NULL, &caret_x, &caret_y,
            &caret_height, &iframe_offset_x, &iframe_offset_y, &caret_visible) ||
        !caret_visible) {
        return;
    }

    // Calculate absolute position (CSS pixels)
    float x = caret_x;
    float y = caret_y;

    // Walk up the tree to get absolute coordinates
    View* parent = view;
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* parent_block = lam::view_require_block(parent);
            x += parent_block->x;
            y += parent_block->y;
        }
        parent = parent->parent;
    }

    // Add iframe offset (if the caret is inside an iframe, parent chain stops at iframe doc root)
    x += iframe_offset_x;
    y += iframe_offset_y;

    float height = caret_height;

    // Render caret as a line
    svg_indent(ctx);
    strbuf_append_format(ctx->svg_content,
        "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" "
        "stroke=\"black\" stroke-width=\"1.5\" id=\"caret\" />\n",
        x, y, x, y + height);

    log_debug("[CARET SVG] Rendered caret at (%.1f, %.1f) height=%.1f", x, y, height);
}

// Main SVG rendering function
char* render_view_tree_to_svg(UiContext* uicon, View* root_view, int width, int height, DocState* state) {
    if (!root_view || !uicon) {
        return NULL;
    }

    SvgRenderContext ctx;
    memset(&ctx, 0, sizeof(SvgRenderContext));

    ctx.svg_content = strbuf_new_cap(8192);
    ctx.indent_level = 0;
    ctx.viewport_width = width;
    ctx.viewport_height = height;
    ctx.ui_context = uicon;
    paint_list_init(&ctx.paint_list, nullptr);
    paint_list_init(&ctx.effect_fallback.paint_list, nullptr);
    ctx.page_backdrop_pool = mem_pool_create(NULL, MEM_ROLE_RENDER, "render.svg.backdrop");
    if (ctx.page_backdrop_pool) {
        ctx.page_backdrop_arena = mem_arena_create(NULL, ctx.page_backdrop_pool, MEM_ROLE_RENDER, "render.svg.backdrop.arena");
        if (ctx.page_backdrop_arena) {
            dl_init(&ctx.page_backdrop_dl, ctx.page_backdrop_arena);
            ctx.page_backdrop_ready = true;
        } else {
            log_error("[SVG_PAINT_IR] page backdrop arena allocation failed");
        }
    } else {
        log_error("[SVG_PAINT_IR] page backdrop pool allocation failed");
    }
    paint_svg_lowering_state_init(&ctx.paint_svg_state, ctx.indent_level);

    // Initialize default font and color
    ctx.color.r = 0; ctx.color.g = 0; ctx.color.b = 0; ctx.color.a = 255; // Black text
    ctx.block.x = 0; ctx.block.y = 0;

    // Initialize font from default
    ctx.font.style = &uicon->default_font;
    ctx.font.font_handle = NULL;

    // SVG header
    strbuf_append_format(ctx.svg_content,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n",
        width, height, width, height);

    ctx.indent_level++;

    // Add background
    Color white = {};
    white.r = 255;
    white.g = 255;
    white.b = 255;
    white.a = 255;
    paint_fill_rect(&ctx.paint_list, 0.0f, 0.0f, width, height, white);
    svg_lower_paint_list(&ctx);

    // Render the root view via shared tree walker
    RenderBackend backend = svg_make_backend(&ctx);
    RenderWalkState walk_state = {};
    walk_state.x = 0;
    walk_state.y = 0;
    walk_state.font = ctx.font;
    walk_state.color = ctx.color;
    walk_state.ui_context = uicon;

    if (root_view->view_type == RDT_VIEW_BLOCK) {
        render_walk_block(&backend, &walk_state, lam::view_require_block(root_view));
    } else {
        render_walk_children(&backend, &walk_state, root_view);
    }

    // Render caret if present
    render_caret_svg(&ctx, state);

    ctx.indent_level--;

    // SVG footer
    strbuf_append_str(ctx.svg_content, "</svg>\n");

    RenderPathTrace trace = {};
    trace.target = "svg";
    trace.replay_mode = "paint_ir_svg";
    trace.backend_name = "svg_export";
    trace.display_list_recorded = false;
    trace.paint_ir_enabled = true;
    trace.surface_width = width;
    trace.surface_height = height;
    trace.paint_ir_commands = ctx.paint_svg_stats.command_count;
    trace.paint_ir_emitted = ctx.paint_svg_stats.emitted_count;
    trace.paint_ir_fallbacks = ctx.paint_svg_stats.fallback_count;
    trace.paint_ir_unsupported = ctx.paint_svg_stats.unsupported_count;
    const RenderExportTargetCaps* caps =
        render_export_target_get_caps(RENDER_EXPORT_TARGET_SVG);
    if (caps) {
        trace.backend_vector_paths = caps->paths;
        trace.backend_gradients = caps->gradients;
        trace.backend_nested_clips = caps->clips;
        trace.backend_picture_svg = true;
        trace.backend_opacity_group = caps->opacity_groups;
        trace.backend_blend_modes = caps->blend_modes;
        trace.backend_gaussian_blur = caps->filters;
        trace.backend_color_matrix_filters = caps->filters;
        trace.backend_native_text_runs = caps->glyph_runs;
    }
    render_profiler_emit_path_trace(nullptr, uicon, state, &trace);

    // Extract the final SVG string
    char* result = mem_strdup(ctx.svg_content->str, MEM_CAT_RENDER);
    strbuf_free(ctx.svg_content);
    paint_list_destroy(&ctx.paint_list);
    paint_list_destroy(&ctx.effect_fallback.paint_list);
    if (ctx.page_backdrop_ready) {
        dl_destroy(&ctx.page_backdrop_dl);
    }
    if (ctx.page_backdrop_pool) {
        mem_pool_destroy(ctx.page_backdrop_pool);
    }

    return result;
}

// Function to save SVG to file
bool save_svg_to_file(const char* svg_content, const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        log_debug("Failed to open file for writing: %s", filename);
        return false;
    }

    size_t len = strlen(svg_content);
    size_t written = fwrite(svg_content, 1, len, file);
    fclose(file);

    if (written != len) {
        log_debug("Failed to write complete SVG content to file: %s", filename);
        return false;
    }

    return true;
}

// Main function to layout HTML and render to SVG
// scale: User-specified scale factor (default 1.0, use 2.0 for high-DPI output)
int render_html_to_svg(const char* html_file, const char* svg_file, int viewport_width, int viewport_height, float scale) {
    log_debug("render_html_to_svg called with html_file='%s', svg_file='%s', viewport=%dx%d, scale=%.2f",
              html_file, svg_file, viewport_width, viewport_height, scale);

    // Validate scale
    if (scale <= 0) scale = 1.0f;

    // Remember if we need to auto-size (viewport was 0)
    bool auto_width = (viewport_width == 0);
    bool auto_height = (viewport_height == 0);

    // Use reasonable defaults for layout if auto-sizing
    int layout_width = viewport_width > 0 ? viewport_width : 1200;
    int layout_height = viewport_height > 0 ? viewport_height : 800;

    // Initialize UI context in headless mode
    UiContext ui_context;
    if (ui_context_init(&ui_context, true) != 0) {
        log_debug("Failed to initialize UI context for SVG rendering");
        return 1;
    }

    // Create a surface for layout calculations with layout dimensions
    ui_context_create_surface(&ui_context, layout_width, layout_height);

    // Update UI context viewport dimensions for layout calculations
    ui_context.window_width = layout_width;
    ui_context.window_height = layout_height;

    // Get current directory for relative path resolution
    Url* cwd = get_current_dir();
    if (!cwd) {
        log_debug("Could not get current directory");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    // Load HTML document
    log_debug("Loading HTML document: %s", html_file);
    DomDocument* doc = load_html_doc(cwd, (char*)html_file, layout_width, layout_height);
    if (!doc) {
        log_debug("Could not load HTML file: %s", html_file);
        url_destroy(cwd);
        ui_context_cleanup(&ui_context);
        return 1;
    }

    ui_context.document = doc;

    // Set document scale for rendering
    doc->given_scale = scale;
    doc->scale = scale;  // In headless mode, pixel_ratio is always 1.0

    // Process @font-face rules before layout
    process_document_font_faces(&ui_context, doc);

    // Layout the document (produces CSS logical pixels)
    log_debug("Performing layout...");
    layout_html_doc(&ui_context, doc, false);

    // Calculate actual content dimensions (in CSS logical pixels)
    int content_max_x = layout_width;
    int content_max_y = layout_height;
    if (doc->view_tree && doc->view_tree->root) {
        int bounds_x = 0, bounds_y = 0;
        calculate_content_bounds(doc->view_tree->root, &bounds_x, &bounds_y);
        // Add some padding to ensure nothing is cut off
        bounds_x += 50;
        bounds_y += 50;

        // If auto-sizing, use content bounds; otherwise use minimum of viewport and content
        if (auto_width) {
            content_max_x = bounds_x;
        } else {
            content_max_x = (bounds_x > layout_width) ? bounds_x : layout_width;
        }
        if (auto_height) {
            content_max_y = bounds_y;
        } else {
            content_max_y = (bounds_y > layout_height) ? bounds_y : layout_height;
        }

        if (auto_width || auto_height) {
            log_info("Auto-sized output dimensions: %dx%d (content bounds with 50px padding)", content_max_x, content_max_y);
        } else {
            log_debug("Calculated content bounds: %dx%d", content_max_x, content_max_y);
        }
    }

    // Render to SVG (apply scale to output dimensions)
    if (doc->view_tree && doc->view_tree->root) {
        log_debug("Rendering view tree to SVG...");
        // SVG output dimensions are scaled; coordinates inside are in CSS pixels with viewBox transform
        int svg_width = (int)(content_max_x * scale);
        int svg_height = (int)(content_max_y * scale);
        char* svg_content = render_view_tree_to_svg(&ui_context, doc->view_tree->root,
                                                   svg_width, svg_height, doc->state);
        if (svg_content) {
            if (save_svg_to_file(svg_content, svg_file)) {
                log_info("Successfully rendered HTML to SVG: %s", svg_file);
                mem_free(svg_content);
                url_destroy(cwd);
                ui_context_cleanup(&ui_context);
                return 0;
            } else {
                log_debug("Failed to save SVG to file: %s", svg_file);
                mem_free(svg_content);
            }
        } else {
            log_debug("Failed to render view tree to SVG");
        }
    } else {
        log_debug("No view tree available for rendering");
    }

    // Cleanup
    url_destroy(cwd);
    ui_context_cleanup(&ui_context);
    return 1;
}
