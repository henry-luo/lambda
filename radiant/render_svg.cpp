#include "render.hpp"
#include "geomap.hpp"
#include "svg_animation.hpp"
#include "radiant.hpp"
#include "../lambda/dom/dom.h"
#include "scene3d.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "render_effect_raster_fallback.hpp"
#include "render_glyph_run_raster_lower.hpp"
#include "event.hpp"
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include "../lib/font/font.h"
#include "../lib/utf.h"
#include "../lib/escape.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/core/mark_reader.hpp"
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
#include <cctype>
#include <cwctype>

// font, block, color and ui_context come from the shared RenderContext
struct SvgRenderContext : RenderContext {
    StrBuf* svg_content;
    int indent_level;
    float viewport_width;
    float viewport_height;
    PaintList paint_list;
    RenderEffectRasterFallback effect_fallback;
    Arena* page_backdrop_arena;
    DisplayList page_backdrop_dl;
    bool page_backdrop_ready;
    PaintSvgLoweringState paint_svg_state;
    PaintSvgLoweringStats paint_svg_stats;
    int paint_resource_id;
    bool transform_emitted_stack[64];
    int transform_emitted_depth;
    int transform_emitted_overflow_depth;
};

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

static void svg_record_page_backdrop_paint_list(SvgRenderContext* ctx,
                                                const PaintList* paint_list) {
    if (!ctx) return;
    render_effect_record_page_backdrop_paint_list(
        ctx->page_backdrop_ready, &ctx->page_backdrop_dl, paint_list,
        render_glyph_run_raster_lower, ctx->ui_context ? ui_context_raster_scale(ctx->ui_context) : 1.0f);
}

static void svg_lower_paint_list(SvgRenderContext* ctx) {
    if (!ctx || ctx->effect_fallback.active) return;
    render_svg_inline_register_paint_ir_lowerers();
    svg_record_page_backdrop_paint_list(ctx, &ctx->paint_list);

    PaintSvgLoweringOptions options = {};
    options.indent_level = ctx->indent_level;
    options.caps = lam::up(render_export_target_get_caps(RENDER_EXPORT_TARGET_SVG));
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

static bool svg_lower_owned_path(SvgRenderContext* ctx, int index, PaintOp op,
                                  RdtPath* path, RdtGradientStop* stops = nullptr) {
    bool retained = ctx->effect_fallback.active &&
        paint_list_take_path_payload(svg_active_paint_list(ctx), index, op, stops);
    svg_lower_paint_list(ctx);
    if (!retained) rdt_path_free(path);
    return retained;
}

static bool svg_emit_raster_fallback_image(SvgRenderContext* ctx, ImageSurface* surface,
                                           float x, float y, float width, float height) {
    StrBuf* uri = render_encode_surface_data_uri(surface);
    if (!uri) return false;
    svg_indent(ctx);
    strbuf_append_format(ctx->svg_content,
        "<image data-radiant-fallback=\"effect-raster\" x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" href=\"",
        x, y, width, height);
    strbuf_append_str_n(ctx->svg_content, uri->str, uri->length);
    strbuf_append_str(ctx->svg_content, "\" />\n"); strbuf_free(uri);
    return true;
}

static void svg_begin_effect_raster_fallback(SvgRenderContext* ctx,
                                             const PaintEffectGroup* group) {
    if (!ctx || !group) return;
    render_effect_raster_begin(
        &ctx->effect_fallback, group, render_glyph_run_raster_lower);
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
        render_effect_group_needs_page_backdrop(&ctx->effect_fallback.group)
            ? &ctx->page_backdrop_dl
            : nullptr,
        &ctx->effect_fallback.group.bounds,
        ctx->viewport_width,
        ctx->viewport_height,
        false,
        ctx->ui_context ? ui_context_raster_scale(ctx->ui_context) : 1.0f,
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
    paint_svg_color_to_string(color, result, 32);
}

static void render_text_view_svg(SvgRenderContext* ctx, ViewText* text) {
    if (!text || !text->text_data()) return;
    unsigned char* str = text->text_data();

    RenderTextPaintIterator paint_iterator(ctx, text);
    TextRect* text_rect;
    while ((text_rect = paint_iterator.next(true))) {
    PaintGlyphRun run = {};
    const char* logical_text = nullptr;
    lam::Temp<char> text_content(render_text_rect_glyph_run(text, text_rect, &ctx->font, ctx->color,
        ctx->block.x, ctx->block.y, &run, paint_iterator.reverses_clusters(), &logical_text));
    if (!text_content.get()) continue;
    if (!run.font_family) run.font_family = lam::up("Arial");

    char deco_line[48];
    font_text_decoration_names(ctx->font.style, deco_line, sizeof(deco_line));
    bool has_text_deco = strcmp(deco_line, "none") != 0;
    DomElement* parent_elem = text->parent ? text->parent->as_element() : nullptr;
    bool has_text_shadow = parent_elem && parent_elem->font &&
                           parent_elem->fontp()->text_shadow;

    if (parent_elem && render_paint_text_background(svg_active_paint_list(ctx), ctx->ui_context,
            parent_elem, &run, {ctx->block.x, ctx->block.y, parent_elem->width, parent_elem->height})) {
        svg_lower_paint_list(ctx);
    }
    RenderTextDecorationSource decoration = {str, text_rect, ctx->block.x, ctx->block.y};
    if (has_text_shadow) {
        if (!render_paint_text_shadows(svg_active_paint_list(ctx), ctx->ui_context,
                &run, parent_elem->fontp()->text_shadow, &decoration)) {
            ctx->paint_svg_stats.unsupported_count++;
            log_error("[SVG_TEXT_SHADOW] cannot record complete shadow geometry");
        }
        svg_lower_paint_list(ctx);
    }
    // background glyph geometry is shared by ordinary, decorated and shadowed text.
    if (has_text_deco) {
        if (!render_paint_text_decorations(svg_active_paint_list(ctx), &ctx->font, str, text_rect,
            ctx->block.x, ctx->block.y, ctx->color, true)) {
            ctx->paint_svg_stats.unsupported_count++;
            log_error("[SVG_TEXT_DECORATION] cannot record complete decoration geometry");
        }
        svg_lower_paint_list(ctx);
    }
    // effect fallback retains commands until rasterization, so its paint list
    // must own text that immediate SVG lowering would otherwise consume.
    if (ctx->effect_fallback.active) run.owned_text = lam::own((const char*)text_content.release());
    // a receiving SVG viewer may substitute this face and change fitted glyph bounds (FT-15).
    RdtPath* outline = !ctx->effect_fallback.active
        ? render_path_create_text_run(&run, ctx->ui_context ? ctx->ui_context->font_ctx.get() : nullptr)
        : nullptr;
    if (outline) {
        // outlines retain accessible text without asking the viewer to paint a replacement font.
        RenderSemanticGroup label = {}; label.title = lam::up(logical_text);
        paint_begin_semantic_group(svg_active_paint_list(ctx), &label);
        int index = paint_list_count(svg_active_paint_list(ctx));
        paint_fill_path(svg_active_paint_list(ctx), outline, run.color, RDT_FILL_WINDING, nullptr);
        paint_end_semantic_group(svg_active_paint_list(ctx));
        svg_lower_owned_path(ctx, index, PAINT_FILL_PATH, outline);
    } else {
        paint_glyph_run(svg_active_paint_list(ctx), &run);
        svg_lower_paint_list(ctx);
    }

    if (!render_paint_text_emphasis(svg_active_paint_list(ctx),
            ctx->ui_context ? ctx->ui_context->font_ctx.get() : nullptr, &run)) {
        ctx->paint_svg_stats.unsupported_count++;
        log_error("[SVG_TEXT_EMPHASIS] cannot record selected mark geometry");
    }
    svg_lower_paint_list(ctx);
    if (has_text_deco) {
        if (!render_paint_text_decorations(svg_active_paint_list(ctx), &ctx->font, str, text_rect,
            ctx->block.x, ctx->block.y, ctx->color, false)) {
            ctx->paint_svg_stats.unsupported_count++;
            log_error("[SVG_TEXT_DECORATION] cannot record complete decoration geometry");
        }
        svg_lower_paint_list(ctx);
    }

    }
}

/**
 * Convenience: test whether a border has any non-zero corner radius.
 */
static bool svg_has_border_radius(BorderProp* border) {
    return border && radiant_corner_has_radius(&border->radius);
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
    const float widths[] = {bwt, bwr, bwb, bwl};
    float points[8];
    if (side < 0 || side >= 4 || !render_path_border_side_points({x, y, W, H}, side, widths[side],
        side == 0 || side == 2 ? bwl : bwt, side == 0 || side == 2 ? bwr : bwb, points)) {
        buf[0] = '\0'; return;
    }
    str_fmt(buf, buf_size, "%.2f,%.2f %.2f,%.2f %.2f,%.2f %.2f,%.2f",
        points[0], points[1], points[2], points[3], points[4], points[5], points[6], points[7]);
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

    Rect grid = render_geometry_boundary_rect(view, render_geometry_block_border_rect(&ctx->block, view, 1.0f));
    float x = grid.x, y = grid.y, width = grid.width, height = grid.height;

    if (render_paint_boundary_emit_simple(svg_active_paint_list(ctx), view, grid)) {
        svg_lower_paint_list(ctx);
        return;
    }

    bool image_border = render_paint_boundary_layers(svg_active_paint_list(ctx), ctx->ui_context,
        view, {x, y, width, height}, RENDER_EXPORT_TARGET_SVG, ctx->effect_fallback.active);
    svg_lower_paint_list(ctx);
    if (image_border) return;

    // Render borders with style-aware per-side SVG polygons
    if (view->boundary()->border) {
        BorderProp* border = view->boundary()->border;
        float bwt = border->width.values[0], bwr = border->width.values[1];
        float bwb = border->width.values[2], bwl = border->width.values[3];

        // When border-radius is present, clip border polygons to the outer
        // rounded rect — matching the raster path's radius_clip approach.
        bool has_radius = svg_has_border_radius(border);
        bool shared_border = has_radius && render_paint_boundary_emit_solid_border(
            svg_active_paint_list(ctx), border, {x, y, width, height});
        if (shared_border) svg_lower_paint_list(ctx);
        if (has_radius && !shared_border) {
            char clip_id[64];
            // retained IDs keep exported references independent of allocator addresses (D4.5.1v4).
            str_fmt(clip_id, sizeof(clip_id), "border-clip-%u", static_cast<View*>(view)->id);
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content, "<defs><clipPath id=\"%s\"><path d=\"", clip_id);
            render_path_append_svg_rounded_rect(
                ctx->svg_content, {x, y, width, height}, &border->radius);
            strbuf_append_str(ctx->svg_content, "\"/></clipPath></defs>\n");
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content, "<g clip-path=\"url(#%s)\">\n", clip_id);
        }

        for (int i = 0; !shared_border && i < 4; i++) {
            svg_emit_border_side(ctx, border->styles[i], border->colors[i],
                x, y, width, height, bwt, bwr, bwb, bwl, i);
        }

        if (has_radius && !shared_border) {
            svg_indent(ctx);
            strbuf_append_str(ctx->svg_content, "</g>\n");
        }
    }

    // Render outline (outside border-box)
    if (view->boundary()->outline) {
        OutlineProp* outline = view->boundary()->outline;
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
            if (svg_has_border_radius(view->boundary()->border)) {
                BorderProp* border = view->boundary()->border;
                strbuf_append_format(ctx->svg_content, "<path d=\"");
                Corner radius = radiant_corner_expand(&border->radius, expand, expand);
                render_path_append_svg_rounded_rect(
                    ctx->svg_content, {ox, oy, ow, oh}, &radius);
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
    RenderColumnRuleIterator iterator;
    if (!render_column_rules_begin(block, &iterator)) return;
    MultiColumnProp* mc = block->multicol_prop();
    float rule_x = 0.0f, block_y = 0.0f, rule_height = 0.0f;
    while (render_column_rule_next(&iterator, &rule_x, &block_y, &rule_height)) {
        rule_x += ctx->block.x + block->x;
        block_y += ctx->block.y + block->y;
        if (mc->rule_style == CSS_VALUE_DOUBLE) {
            float thin_width = mc->rule_width / 3.0f;
            float offset = thin_width;
            RdtPath* left = rdt_path_new();
            RdtPath* right = rdt_path_new();
            if (left && right) {
                rdt_path_move_to(left, rule_x - offset, block_y);
                rdt_path_line_to(left, rule_x - offset, block_y + rule_height);
                int index = paint_list_count(svg_active_paint_list(ctx));
                paint_stroke_path(svg_active_paint_list(ctx), left, mc->rule_color, thin_width,
                                  RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0.0f,
                                  nullptr);
                svg_lower_owned_path(ctx, index, PAINT_STROKE_PATH, left);
                left = nullptr;

                rdt_path_move_to(right, rule_x + offset, block_y);
                rdt_path_line_to(right, rule_x + offset, block_y + rule_height);
                index = paint_list_count(svg_active_paint_list(ctx));
                paint_stroke_path(svg_active_paint_list(ctx), right, mc->rule_color, thin_width,
                                  RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0.0f,
                                  nullptr);
                svg_lower_owned_path(ctx, index, PAINT_STROKE_PATH, right);
                right = nullptr;
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
                int index = paint_list_count(svg_active_paint_list(ctx));
                paint_stroke_path(svg_active_paint_list(ctx), path, mc->rule_color,
                                  mc->rule_width, RDT_CAP_BUTT, RDT_JOIN_MITER,
                                  dash, dash_count, 0.0f, nullptr);
                svg_lower_owned_path(ctx, index, PAINT_STROKE_PATH, path);
            }
        }

    }
}

// ============================================================================
// SVG RenderBackend vtable callbacks
// ============================================================================

static void svg_cb_render_bound(RenderContext* vctx, ViewBlock* view, float abs_x, float abs_y) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    // render_bound_svg reads ctx->block.{x,y} + view->{x,y}
    ctx->block.x = abs_x - view->x;
    ctx->block.y = abs_y - view->y;
    render_bound_svg(ctx, view);
}

static void svg_cb_render_text(RenderContext* vctx, ViewText* text, float abs_x, float abs_y,
                               FontBox* font, Color color) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    ctx->block.x = abs_x;
    ctx->block.y = abs_y;
    ctx->font = *font;
    ctx->color = color;
    render_text_view_svg(ctx, text);
}

static void svg_cb_render_image(RenderContext* vctx, ViewBlock* block, float abs_x, float abs_y) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    ImageSurface* img = render_media_image_surface(block);
    if (!img) return;

    BlockBlot image_block = {};
    image_block.x = abs_x - block->x;
    image_block.y = abs_y - block->y;
    Rect content_rect = render_geometry_block_content_rect(&image_block, block, 1.0f);
    PaintList* paint = svg_active_paint_list(ctx);
    if (render_media_paint_image_content(paint, ctx->ui_context, block, &content_rect)) {
        svg_lower_paint_list(ctx);
    }
}

static void svg_cb_render_scene3d(RenderContext* vctx, ViewBlock* block, float abs_x, float abs_y) {
    auto* ctx = (SvgRenderContext*)vctx;
    if (scene3d_paint_content(svg_active_paint_list(ctx),ctx->ui_context,block,abs_x,abs_y)) svg_lower_paint_list(ctx);
}

static void svg_cb_render_geomap(RenderContext* vctx, ViewBlock* block, float abs_x, float abs_y) {
    auto* ctx = (SvgRenderContext*)vctx;
    BlockBlot parent = {};
    parent.x = abs_x - block->x; parent.y = abs_y - block->y;
    geomap_paint(svg_active_paint_list(ctx), block->as_element(),
        render_geometry_block_content_rect(&parent, block, 1.0f));
    svg_lower_paint_list(ctx);
}

static void svg_cb_render_inline_svg(RenderContext* vctx, ViewBlock* block, float abs_x, float abs_y,
                                     FontBox* font, Color color) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (!ctx || !block) return;
    DomElement* dom_elem = lam::dom_require_element(lam::view_dom_node(block));
    if (!dom_elem || dom_elem->is_synthetic()) return;

    BlockBlot block_context = {};
    block_context.x = abs_x - block->x;
    block_context.y = abs_y - block->y;
    Rect content_rect = render_geometry_block_content_rect(&block_context, block, 1.0f);
    if (content_rect.width <= 0.0f || content_rect.height <= 0.0f) return;

    SvgInitialPaint initial_paint;
    render_svg_initial_paint(block, color, &initial_paint);

    RdtMatrix transform = rdt_matrix_translate(content_rect.x, content_rect.y);
    Bound content_clip = {content_rect.x, content_rect.y,
                          content_rect.x + content_rect.width,
                          content_rect.y + content_rect.height};
    PaintSvgSubscene subscene = {};
    render_svg_build_subscene(&subscene,
                              dom_element_to_element(dom_elem),
                              content_rect.width,
                              content_rect.height,
                              nullptr,
                              1.0f,
                              ctx->ui_context ? ctx->ui_context->font_ctx : nullptr,
                              &transform,
                              &content_clip,
                              &initial_paint.current_color,
                              initial_paint.has_fill_color ? &initial_paint.fill_color : nullptr,
                              nullptr,
                              1.0f,
                              initial_paint.fill_none,
                              initial_paint.has_stroke_color ? &initial_paint.stroke_color : nullptr,
                              initial_paint.stroke_none,
                              initial_paint.stroke_width, ctx->ui_context);
    subscene.id_scope = lam::up(render_svg_reference_scope(dom_elem));
    paint_svg_subscene(svg_active_paint_list(ctx), &subscene);
    svg_lower_paint_list(ctx);
    if (font) ctx->font = *font;
    ctx->color = color;
}

static void svg_cb_render_svg_subscene(RenderContext* vctx, const PaintSvgSubscene* subscene) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (!ctx || !subscene) return;
    paint_svg_subscene(svg_active_paint_list(ctx), subscene);
    svg_lower_paint_list(ctx);
}

static const char* SVG_GRAPH_SEMANTIC_ATTRS[] = {
    "data-graph-role", "data-node-id", "data-edge-id", "data-cluster-id",
    "data-parent-cluster-id", "data-subgraph-id", "data-shape",
    "data-label", "data-label-format", "data-owner-kind", "data-owner-id",
    "data-annotation-kind",
    "data-direction", "data-marker-start", "data-marker-end",
    "data-route", "data-route-kind", "data-from", "data-to",
    "data-fill", "data-stroke", "data-stroke-width", "data-color",
    "data-opacity", "data-dash-array"
};

static void svg_append_graph_semantic_attrs(SvgRenderContext* ctx,
                                            ViewBlock* block) {
    if (!ctx || !block) return;
    const char* role = block->get_attribute("data-graph-role");
    if (!role || !role[0]) return;

    for (size_t i = 0; i < sizeof(SVG_GRAPH_SEMANTIC_ATTRS) /
                                  sizeof(SVG_GRAPH_SEMANTIC_ATTRS[0]); i++) {
        const char* name = SVG_GRAPH_SEMANTIC_ATTRS[i];
        const char* value = block->get_attribute(name);
        if (!value) continue;
        strbuf_append_char(ctx->svg_content, ' ');
        strbuf_append_all(ctx->svg_content, 2, name, "=\"");
        escape_append(ctx->svg_content, value, strlen(value), ESCAPE_RULES_HTML_ATTR,
                      ESCAPE_RULES_HTML_ATTR_COUNT, ESCAPE_CTRL_NONE);
        strbuf_append_char(ctx->svg_content, '\"');
    }

    RdtLogicalPoint origin = view_geometry_local_to_block_document(
        static_cast<View*>(block), {block->x, block->y});
    strbuf_append_format(ctx->svg_content,
        " data-x=\"%.3f\" data-y=\"%.3f\" data-width=\"%.3f\" data-height=\"%.3f\"",
        origin.x, origin.y, block->width, block->height);
}

static void svg_cb_begin_block_children(RenderContext* vctx, ViewBlock* block) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    svg_indent(ctx);
    strbuf_append_format(ctx->svg_content, "<g class=\"block\" data-element=\"%s\"",
                         block->node_name());
    svg_append_graph_semantic_attrs(ctx, block);
    strbuf_append_str(ctx->svg_content, ">\n");
    ctx->indent_level++;
}

static void svg_cb_end_block_children(RenderContext* vctx, ViewBlock* block) {
    (void)block;
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    ctx->indent_level--;
    svg_indent(ctx);
    strbuf_append_str(ctx->svg_content, "</g>\n");
}

static void svg_cb_begin_inline_children(RenderContext* vctx, ViewSpan* span) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    svg_indent(ctx);
    strbuf_append_format(ctx->svg_content, "<g class=\"inline\" data-element=\"%s\">\n",
                         span->node_name());
    ctx->indent_level++;
}

static void svg_cb_end_inline_children(RenderContext* vctx, ViewSpan* span) {
    (void)span;
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    ctx->indent_level--;
    svg_indent(ctx);
    strbuf_append_str(ctx->svg_content, "</g>\n");
}

static void svg_cb_begin_effect_group(RenderContext* vctx, const PaintEffectGroup* group) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (ctx->effect_fallback.active) {
        paint_begin_effect_group(&ctx->effect_fallback.paint_list, group);
        ctx->effect_fallback.nested_depth++;
        return;
    }
    if (render_effect_group_needs_raster_fallback(group)) {
        svg_begin_effect_raster_fallback(ctx, group);
        return;
    }
    paint_begin_effect_group(&ctx->paint_list, group);
    svg_lower_paint_list(ctx);
}

static void svg_cb_end_effect_group(RenderContext* vctx) {
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

static void svg_cb_begin_transform(RenderContext* vctx, ViewBlock* block, float abs_x, float abs_y) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (!block) return;
    if (ctx->transform_emitted_depth >= 64) {
        log_error("[SVG_PAINT_IR] transform stack overflow while rendering %s",
                  block->node_name());
        ctx->transform_emitted_overflow_depth++;
        return;
    }

    RdtMatrix transform = {};
    bool has = render_geometry_transform_matrix(
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

static void svg_cb_end_transform(RenderContext* vctx) {
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

static bool svg_cb_begin_clip(RenderContext* vctx, ViewElement* element,
                              float abs_x, float abs_y, RenderClipKind kind) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (!ctx || !render_clip_push_vector(svg_active_paint_list(ctx), element,
            abs_x, abs_y, kind, {0.0f, 0.0f, ctx->viewport_width, ctx->viewport_height},
            ctx->has_overflow_clip_transform ? &ctx->overflow_clip_transform : nullptr)) return false;
    svg_lower_paint_list(ctx);
    return true;
}

static void svg_cb_end_clip(RenderContext* vctx) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    if (!ctx) return;
    paint_pop_clip(svg_active_paint_list(ctx));
    svg_lower_paint_list(ctx);
}

static void svg_cb_render_marker(RenderContext* vctx, ViewSpan* marker, float abs_x, float abs_y,
                                  FontBox* font, Color color) {
    if (!marker || !marker->is_element()) return;
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;

    DomElement* elem = lam::dom_require_element(lam::view_dom_node(marker));
    MarkerProp* marker_prop = elem->marker_prop();
    if (!marker_prop) return;
    if (render_paint_marker_image(svg_active_paint_list(ctx), ctx->ui_context,
            marker, abs_x, abs_y, font)) {
        svg_lower_paint_list(ctx);
        return;
    }
    if (marker_prop->has_color) color = marker_prop->color;
    FontBox marker_font_box = elem->font
        ? FontBox{lam::up(elem->font), font_prop_used_size(elem->font)} : *font;
    font = &marker_font_box;

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
                float baseline_y = render_marker_baseline_y(marker_prop, y);
                const char* src = marker_prop->text_content;
                StrBuf* escaped = strbuf_new_cap(strlen(src) * 2);
                escape_append(escaped, src, strlen(src), ESCAPE_RULES_HTML_TEXT,
                              ESCAPE_RULES_HTML_TEXT_COUNT, ESCAPE_CTRL_NONE);
                FontHandle* marker_font_handle = font_box_handle(font);
                const char* family = marker_font_handle
                    ? font_handle_get_family_name(marker_font_handle) : "Arial";
                // right-align text in marker box
                float text_x = x + width;
                svg_indent(ctx);
                strbuf_append_format(ctx->svg_content,
                    "<text x=\"%.2f\" y=\"%.2f\" font-family=\"%s\" font-size=\"%.0f\" "
                    "fill=\"%s\"",
                    text_x, baseline_y, family, font_size, color_str);
                int font_weight = font->style->font_weight_numeric > 0
                    ? font->style->font_weight_numeric
                    : font->style->font_weight == CSS_VALUE_BOLD ? 700 : 400;
                if (font_weight != 400) {
                    strbuf_append_format(ctx->svg_content,
                        " font-weight=\"%d\"", font_weight);
                }
                if (font->style->font_style == CSS_VALUE_ITALIC ||
                    font->style->font_style == CSS_VALUE_OBLIQUE) {
                    strbuf_append_str(ctx->svg_content, " font-style=\"italic\"");
                }
                strbuf_append_format(ctx->svg_content,
                    " text-anchor=\"end\">%s</text>\n", escaped->str);
                strbuf_free(escaped);
            }
            break;
        }
    }
}

static void svg_cb_render_column_rules(RenderContext* vctx, ViewBlock* block, float abs_x, float abs_y) {
    SvgRenderContext* ctx = (SvgRenderContext*)vctx;
    // render_column_rules_svg reads ctx->block.{x,y} + block->{x,y}
    ctx->block.x = abs_x - block->x;
    ctx->block.y = abs_y - block->y;
    render_column_rules_svg(ctx, block);
}

static RenderBackend svg_make_backend(SvgRenderContext* ctx) {
    RenderBackend b = {};
    b.ctx                   = lam::up(ctx);
    b.render_bound          = svg_cb_render_bound;
    b.render_text           = svg_cb_render_text;
    b.render_image          = svg_cb_render_image;
    b.render_inline_svg     = svg_cb_render_inline_svg;
    b.render_geomap         = svg_cb_render_geomap;
    b.render_scene3d        = svg_cb_render_scene3d;
    b.render_svg_subscene   = svg_cb_render_svg_subscene;
    b.begin_block_children  = svg_cb_begin_block_children;
    b.end_block_children    = svg_cb_end_block_children;
    b.begin_inline_children = svg_cb_begin_inline_children;
    b.end_inline_children   = svg_cb_end_inline_children;
    b.begin_effect_group    = svg_cb_begin_effect_group;
    b.end_effect_group      = svg_cb_end_effect_group;
    b.begin_transform       = svg_cb_begin_transform;
    b.end_transform         = svg_cb_end_transform;
    b.begin_clip            = svg_cb_begin_clip;
    b.end_clip              = svg_cb_end_clip;
    b.render_column_rules   = svg_cb_render_column_rules;
    b.render_marker         = svg_cb_render_marker;
    b.on_font_change        = NULL;
    return b;
}

// export extents follow the same accumulated transforms and overflow clips as painting.
static void calculate_visible_content_bounds(View* view, Bound clip,
                                               RdtMatrix transform,
                                               bool apply_clips, Bound* extent) {
    if (!view) return;
    RdtLogicalPoint origin = view_geometry_node_document_origin(view);
    ViewBlock* block = lam::view_as_block(view);
    RdtMatrix local;
    if (block && render_geometry_transform_matrix(block->transform,
            origin.x, origin.y, view->width, view->height, &local)) {
        transform = rdt_matrix_multiply(&transform, &local);
    }
    Bound projected = {origin.x, origin.y,
        origin.x + view->width, origin.y + view->height};
    // retain the existing finite box when a projective rectangle crosses w=0.
    rdt_matrix_project_rect_bounds(&transform, projected.left, projected.top,
        projected.right, projected.bottom, &projected.left, &projected.top,
        &projected.right, &projected.bottom);
    Bound visible = view_geometry_intersect_bound_rect(clip,
        {projected.left, projected.top, projected.right - projected.left,
         projected.bottom - projected.top});
    if (visible.right > visible.left && visible.bottom > visible.top) {
        extent->left = fminf(extent->left, visible.left);
        extent->top = fminf(extent->top, visible.top);
        extent->right = fmaxf(extent->right, visible.right);
        extent->bottom = fmaxf(extent->bottom, visible.bottom);
    }
    if (!view->is_element()) return;
    ViewElement* element = lam::view_require_element(view);
    if (block) {
        if (view_contents_are_skipped(block)) return;
        Bound overflow; Corner radius;
        if (apply_clips && render_clip_overflow_bounds(block, origin.x, origin.y,
                1.0f, clip, &transform, &overflow, &radius)) {
            rdt_matrix_project_rect_bounds(&transform, overflow.left, overflow.top,
                overflow.right, overflow.bottom, &overflow.left, &overflow.top,
                &overflow.right, &overflow.bottom);
            clip = view_geometry_intersect_bound_rect(clip,
                {overflow.left, overflow.top,
                 overflow.right - overflow.left, overflow.bottom - overflow.top});
        }
    }
    for (View* child = element->first_child; child; child = child->next())
        calculate_visible_content_bounds(child, clip, transform, apply_clips, extent);
}

void calculate_content_bounds(View* view, int* max_x, int* max_y) {
    Bound initial = {0.0f, 0.0f, (float)*max_x, (float)*max_y}, uncut = initial, visible = initial;
    // first bound open axes by actual descendant geometry, then apply the owner-space strips.
    calculate_visible_content_bounds(view,
        {-INFINITY, -INFINITY, INFINITY, INFINITY}, rdt_matrix_identity(), false, &uncut);
    calculate_visible_content_bounds(view, uncut, rdt_matrix_identity(), true, &visible);
    *max_x = (int)ceilf(visible.right); // INT_CAST_OK: the legacy export API uses integral surface extents.
    *max_y = (int)ceilf(visible.bottom); // INT_CAST_OK: the legacy export API uses integral surface extents.
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

    RdtLogicalPoint origin = view_geometry_local_to_block_document(
        view, {caret_x + iframe_offset_x, caret_y + iframe_offset_y});
    float x = origin.x;
    float y = origin.y;

    float height = caret_height;

    // Render caret as a line
    svg_indent(ctx);
    strbuf_append_format(ctx->svg_content,
        "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" "
        "stroke=\"black\" stroke-width=\"1.5\" id=\"caret\" />\n",
        x, y, x, y + height);

}

// Main SVG rendering function
char* render_view_tree_to_svg(UiContext* uicon, View* root_view, float width, float height, DocState* state, float output_scale) {
    if (!root_view || !uicon || !isfinite(output_scale) || output_scale <= 0) {
        return NULL;
    }

    SvgRenderContext ctx = {};

    ctx.svg_content = strbuf_new_cap(8192);
    ctx.indent_level = 0;
    ctx.viewport_width = width;
    ctx.viewport_height = height;
    ctx.ui_context = lam::up(uicon);
    paint_list_init(&ctx.paint_list, nullptr);
    paint_list_init(&ctx.effect_fallback.paint_list, nullptr);
    ctx.page_backdrop_arena = mem_arena_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_RENDER, "render.svg.backdrop.arena");
    if (ctx.page_backdrop_arena) {
        dl_init(&ctx.page_backdrop_dl, ctx.page_backdrop_arena);
        ctx.page_backdrop_ready = true;
    } else {
        log_error("[SVG_PAINT_IR] page backdrop arena allocation failed");
    }
    paint_svg_lowering_state_init(&ctx.paint_svg_state, ctx.indent_level);

    // Initialize default font and color
    ctx.color.r = 0; ctx.color.g = 0; ctx.color.b = 0; ctx.color.a = 255; // Black text
    ctx.block.x = 0; ctx.block.y = 0;

    // Initialize font from default
    ctx.font.style = lam::up(&uicon->default_font);

    // SVG header
    strbuf_append_format(ctx.svg_content,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "width=\"%.6g\" height=\"%.6g\" viewBox=\"0 0 %.6g %.6g\">\n",
        width * output_scale, height * output_scale, width, height);

    ctx.indent_level++;

    // Add background
    // the document canvas includes propagated root/body paint even when its layout box is empty.
    Color background = render_document_output_background(root_view);
    paint_fill_rect(&ctx.paint_list, 0.0f, 0.0f, width, height, background);
    svg_lower_paint_list(&ctx);

    // Render the root view via shared tree walker
    RenderBackend backend = svg_make_backend(&ctx);
    RenderWalkState walk_state = {};
    walk_state.x = 0;
    walk_state.y = 0;
    walk_state.font = ctx.font;
    walk_state.color = ctx.color;
    walk_state.ui_context = lam::up(uicon);

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
    trace.target = lam::up("svg");
    trace.replay_mode = lam::up("paint_ir_svg");
    trace.backend_name = lam::up("svg_export");
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
    radiant_apply_export_caps(&trace, caps);
    render_profiler_emit_path_trace(nullptr, uicon, state, &trace);

    // Extract the final SVG string
    char* result = uicon->render_failed ? nullptr : mem_strdup(ctx.svg_content->str, MEM_CAT_RENDER);
    strbuf_free(ctx.svg_content);
    paint_list_destroy(&ctx.paint_list);
    paint_list_destroy(&ctx.effect_fallback.paint_list);
    if (ctx.page_backdrop_ready) {
        dl_destroy(&ctx.page_backdrop_dl);
    }
    if (ctx.page_backdrop_arena) mem_arena_destroy(ctx.page_backdrop_arena);

    return result;
}

// Function to save SVG to file
bool save_svg_to_file(const char* svg_content, const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        return false;
    }

    size_t len = strlen(svg_content);
    size_t written = fwrite(svg_content, 1, len, file);
    fclose(file);

    if (written != len) {
        return false;
    }

    return true;
}

struct SvgDomSnapshot {
    Pool* pool;
    DomXmlAttributeOverride* attributes;
};

static void svg_snapshot_attribute(SvgDomSnapshot* snapshot, const char* name, const char* value) {
    if (!value) return;
    DomXmlAttributeOverride* attribute = (DomXmlAttributeOverride*)pool_calloc(snapshot->pool, sizeof(*attribute));
    attribute->name = pool_strdup(snapshot->pool, name);
    attribute->value = pool_strdup(snapshot->pool, value);
    // SMIL's namespaced reference spelling keeps its XML expanded name on export.
    if (!strncmp(name, "xlink:", 6)) attribute->namespace_uri = "http://www.w3.org/1999/xlink";
    else if (!strncmp(name, "xml:", 4)) attribute->namespace_uri = "http://www.w3.org/XML/1998/namespace";
    attribute->next = snapshot->attributes;
    snapshot->attributes = attribute;
}

static void svg_snapshot_sample(void* context, const char* name, const char* value, bool css) {
    // CSS samples use the presentation selector below, including its priority rules.
    if (!css) svg_snapshot_attribute((SvgDomSnapshot*)context, name, value);
}

static bool svg_snapshot_include(void*, void* node) {
    DomElement* element = (DomElement*)node;
    const char* name = element->local_name();
    if (!strcmp(name, "script")) return false;
    return !dom_element_is_svg(element) || (strcmp(name, "animate") && strcmp(name, "set") &&
        strcmp(name, "animateTransform") && strcmp(name, "animateMotion") && strcmp(name, "discard"));
}

static void svg_snapshot_style_property(SvgDomSnapshot* snapshot, DomElement* element,
    StrBuf* style, const char* name, CssPropertyCode id, bool inherits) {
    char* owned = nullptr;
    bool from_css = false;
    const char* value = svg_get_dom_presentation_property(element, name,
        inherits, nullptr, 0, &from_css, &owned);
    if (value) {
        strbuf_append_str(style, name);
        strbuf_append_char(style, ':');
        if (id == CSS_PROPERTY_TRANSFORM && !from_css) {
            SvgLengthContext lengths = dom_svg_length_context(element);
            RdtMatrix matrix = svg_resolve_local_transform(value, false, nullptr, nullptr, &lengths);
            strbuf_append_format(style, "matrix(%g,%g,%g,%g,%g,%g)",
                matrix.e11, matrix.e21, matrix.e12, matrix.e22, matrix.e13, matrix.e23);
        } else if (css_property_is_svg_length(id)) {
            // SMIL stores user-unit numbers; CSS lengths in the saved style need px units.
            CssValue* parsed = css_parse_component_value_text(value, strlen(value), snapshot->pool);
            String* computed = parsed ? css_prop_serialize_svg_value(snapshot->pool, element, id, parsed) : nullptr;
            strbuf_append_str(style, computed ? computed->chars : value);
        } else strbuf_append_str(style, value);
        strbuf_append_str(style, "!important;");
    }
    if (owned) mem_free(owned);
}

static DomXmlAttributeOverride* svg_snapshot_attributes(void* context, void* node) {
    SvgDomSnapshot* snapshot = (SvgDomSnapshot*)context;
    DomElement* element = (DomElement*)node;
    snapshot->attributes = nullptr;
    if (dom_element_is_svg(element)) svg_animation_visit_values(element, svg_snapshot_sample, snapshot);
    StrBuf* style = strbuf_new();
    const char* authored = element->get_attribute("style");
    if (authored) strbuf_append_str(style, authored);
    strbuf_append_str(style, ";animation:none!important;transition:none!important;");
    // resolve host CSS and inherited presentation before the SVG leaves its document.
    for (unsigned code = 1; code < CSS_PROPERTY_CUSTOM; code++) {
        CssPropertyCode id = (CssPropertyCode)code;
        bool shape_geometry = css_property_is_svg_geometry(id) && !strcmp(element->local_name(), "rect");
        if (!css_property_is_svg_presentation(id) && !shape_geometry && id != CSS_PROPERTY_COLOR &&
            id != CSS_PROPERTY_OPACITY && id != CSS_PROPERTY_TRANSFORM &&
            id != CSS_PROPERTY_TRANSLATE && id != CSS_PROPERTY_ROTATE && id != CSS_PROPERTY_SCALE &&
            id != CSS_PROPERTY_TRANSFORM_ORIGIN &&
            id != CSS_PROPERTY_VISIBILITY && id != CSS_PROPERTY_DISPLAY) continue;
        const CssProperty* property = css_property_get_by_code(id);
        if (!property || !dom_element_is_svg(element)) continue;
        svg_snapshot_style_property(snapshot, element, style, property->name, id,
            property->inheritance == PROP_INHERIT_YES);
    }
    if (dom_element_is_svg(element)) {
        // transform-box has no registered dispatch key; text keeps its host font when reopened.
        const char* names[] = {"transform-box", "font-family", "font-size", "font-weight", "font-style",
            "letter-spacing", "word-spacing", "text-decoration", "dominant-baseline", "baseline-shift"};
        for (const char* name : names)
            svg_snapshot_style_property(snapshot, element, style, name, css_property_code_from_name(name),
                strcmp(name, "transform-box") != 0);
    }
    svg_snapshot_attribute(snapshot, "style", style->str);
    strbuf_free(style);
    return snapshot->attributes;
}

int render_svg_dom_to_file(DomElement* root, const char* filename, bool freeze) {
    if (!root || !dom_element_is_svg(root) || !filename) return 1;
    Pool* pool = pool_create();
    StrBuf* output = strbuf_new();
    if (!pool || !output) { if (pool) pool_destroy(pool); if (output) strbuf_free(output); return 1; }
    SvgDomSnapshot snapshot = {pool, nullptr};
    DomXmlProjection projection = {&snapshot, svg_snapshot_include, svg_snapshot_attributes};
    bool success = dom_serialize_xml(root, output, freeze ? &projection : nullptr) &&
        save_svg_to_file(output->str, filename);
    strbuf_free(output);
    pool_destroy(pool);
    return success ? 0 : 1;
}

int render_svg_source_document_to_file(UiContext* ui, const char* filename) {
    DomDocument* document = ui ? ui->document : nullptr;
    DomElement* body = dom_document_body_element(document);
    for (DomNode* node = body ? body->first_child : nullptr; node; node = node->next_sibling) {
        if (node->is_element() && dom_element_is_svg(node) && !strcmp(node->as_element()->local_name(), "svg"))
            return render_svg_dom_to_file(node->as_element(), filename, radiant_document_virtual_clock(ui, nullptr));
    }
    return 1;
}

int render_export_session_to_svg(RenderExportSession* session, const char* svg_file) {
    if (!session) return 1;
    UiContext* ui_context = session->ui_context;
    DomDocument* doc = session->document;

    if (doc->services.svg_source_document)
        return render_svg_source_document_to_file(ui_context, svg_file);
    // Render to SVG (apply scale to output dimensions)
    if (doc->view_tree && doc->view_tree->root) {
        // only the outer dimensions scale; the viewBox retains CSS coordinates for every subscene.
        lam::Temp<char> svg_content(render_view_tree_to_svg(ui_context, doc->view_tree->root,
            (float)session->content_width, (float)session->content_height, doc->state, session->output_scale));
        if (svg_content) {
            if (save_svg_to_file(svg_content.get(), svg_file)) {
                log_info("Successfully rendered HTML to SVG: %s", svg_file);
                return 0;
            }
        } else {
        }
    } else {
    }

    return 1;
}

// Main function to layout HTML and render to SVG.
// scale: User-specified scale factor (default 1.0, use 2.0 for high-DPI output)
int render_html_to_svg(const char* html_file, const char* svg_file, int viewport_width,
        int viewport_height, float scale, float device_scale) {
    RenderExportSession session;
    if (!render_export_session_begin(
            &session, html_file, viewport_width, viewport_height, 1200, 800, scale, false, false, device_scale)) {
        return 1;
    }
    int result = render_export_session_to_svg(&session, svg_file);
    render_export_session_end(&session);
    return result;
}

int render_document_transform_to_svg(const char* document_file,
        const LambdaDocumentTransformConfig* transform,
        const LambdaDocumentTransformOption* options, int option_count,
        const char* svg_file, int viewport_width, int viewport_height, float scale, float device_scale) {
    RenderExportSession session;
    if (!render_export_session_begin_document_transform(&session, document_file, transform,
            options, option_count, viewport_width, viewport_height, 1200, 800, scale,
            device_scale, false)) {
        return 1;
    }
    int result = render_export_session_to_svg(&session, svg_file);
    render_export_session_end(&session);
    return result;
}
