/**
 * Lambda Unified Font Module — ThorVG Glyph Rasterizer
 *
 * Rasterizes TrueType glyph outlines using ThorVG's software canvas.
 * Used on Linux (and future WASM) as the platform rasterization backend,
 * replacing FreeType's FT_Load_Glyph(FT_LOAD_RENDER).
 *
 * Pipeline:
 *   1. Extract outline via glyf_get_outline() (font_glyf.c)
 *   2. Convert quadratic Bézier → cubic Bézier (ThorVG only has cubicTo)
 *   3. Build ThorVG Shape from contours
 *   4. Rasterize via SwCanvas to ABGR8888 buffer
 *   5. Extract alpha channel → 8-bit grayscale (linear coverage, no gamma)
 *
 * Note: unlike the CoreText path, NO γ² correction is applied here. ThorVG
 * outputs linear coverage values directly. The γ² formula in font_rasterize_ct.c
 * undoes CoreGraphics' internal gamma encoding — that encoding does not exist
 * in ThorVG's software rasterizer.
 *
 * Copyright (c) 2026 Lambda Script Project
 */

#ifndef __APPLE__  // ThorVG rasterizer: Linux + Windows only (macOS uses CoreText)

#include "font_glyf.h"
#include "font_cbdt.h"
#include "font_colr.h"
#include "font_tables.h"
#include "../arena.h"
#include "../image.h"
#include "../log.h"

#include <thorvg_capi.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

// matching font.h types — include via the internal header path
#include "font.h"

// maximum COLR v0 layers per glyph
#define MAX_COLR_LAYERS 64

// ============================================================================
// ThorVG rasterization context (one per FontHandle, reusable)
// ============================================================================

typedef struct TvgRasterCtx {
    Tvg_Canvas canvas;          // SwCanvas instance (reused across glyphs)
    uint32_t*  pixel_buf;       // ABGR8888 buffer (reallocated as needed)
    int        buf_w, buf_h;    // current buffer dimensions
} TvgRasterCtx;

// ============================================================================
// Lifecycle
// ============================================================================

extern "C" void* font_rasterize_tvg_create(void) {
    TvgRasterCtx* ctx = (TvgRasterCtx*)calloc(1, sizeof(TvgRasterCtx));
    if (!ctx) return NULL;

    ctx->canvas = tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT);
    if (!ctx->canvas) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

extern "C" void font_rasterize_tvg_destroy(void* tvg_ctx) {
    if (!tvg_ctx) return;
    TvgRasterCtx* ctx = (TvgRasterCtx*)tvg_ctx;
    if (ctx->canvas) tvg_canvas_destroy(ctx->canvas);
    free(ctx->pixel_buf);
    free(ctx);
}

// ============================================================================
// Internal: ensure pixel buffer is large enough
// ============================================================================

static bool ensure_buffer(TvgRasterCtx* ctx, int w, int h) {
    if (w <= ctx->buf_w && h <= ctx->buf_h && ctx->pixel_buf) {
        return true;
    }
    // grow to at least the requested size (never shrink)
    int new_w = w > ctx->buf_w ? w : ctx->buf_w;
    int new_h = h > ctx->buf_h ? h : ctx->buf_h;
    // clamp to prevent enormous allocations from malformed fonts
    if (new_w > 4096) new_w = 4096;
    if (new_h > 4096) new_h = 4096;

    free(ctx->pixel_buf);
    ctx->pixel_buf = (uint32_t*)calloc((size_t)new_w * (size_t)new_h, sizeof(uint32_t));
    if (!ctx->pixel_buf) {
        ctx->buf_w = 0;
        ctx->buf_h = 0;
        return false;
    }
    ctx->buf_w = new_w;
    ctx->buf_h = new_h;
    return true;
}

// ============================================================================
// Internal: build ThorVG shape from glyph outline
// ============================================================================

static Tvg_Paint build_shape_from_outline(GlyphOutline* outline, float scale,
                                           float offset_x, float offset_y,
                                           float bmp_h, float synth_bold_stroke) {
    Tvg_Paint shape = tvg_shape_new();
    if (!shape) return NULL;

    for (int c = 0; c < outline->num_contours; c++) {
        GlyfContour* contour = &outline->contours[c];
        if (contour->num_points < 2) continue;

        // find first on-curve point to start the path
        int first_on = -1;
        for (int i = 0; i < contour->num_points; i++) {
            if (contour->points[i].on_curve) {
                first_on = i;
                break;
            }
        }
        if (first_on < 0) continue; // all off-curve — shouldn't happen after midpoint insertion

        float fx = contour->points[first_on].x * scale + offset_x;
        // flip Y: font coordinates are Y-up, bitmap is Y-down
        float fy = bmp_h - (contour->points[first_on].y * scale + offset_y);
        tvg_shape_move_to(shape, fx, fy);

        int n = contour->num_points;
        for (int raw_i = 1; raw_i < n; raw_i++) {
            int i = (first_on + raw_i) % n;
            GlyfPoint* pt = &contour->points[i];

            float px = pt->x * scale + offset_x;
            float py = bmp_h - (pt->y * scale + offset_y);

            if (pt->on_curve) {
                tvg_shape_line_to(shape, px, py);
            } else {
                // quadratic Bézier: need the next on-curve point as endpoint
                int next_i = (first_on + raw_i + 1) % n;
                GlyfPoint* next_pt = &contour->points[next_i];
                float ex = next_pt->x * scale + offset_x;
                float ey = bmp_h - (next_pt->y * scale + offset_y);

                // get current position for the conversion
                // previous on-curve point is where we are now
                // we need P0 (current pos) — retrieve from the shape state
                // but ThorVG doesn't expose current position, so track it
                // Actually, we compute from the previous point:
                int prev_i = (first_on + raw_i - 1) % n;
                if (prev_i < 0) prev_i += n;
                GlyfPoint* prev_pt = &contour->points[prev_i];
                float p0x = prev_pt->x * scale + offset_x;
                float p0y = bmp_h - (prev_pt->y * scale + offset_y);

                // quadratic → cubic conversion:
                // CP1 = P0 + 2/3 * (P1 - P0)
                // CP2 = P2 + 2/3 * (P1 - P2)
                float cp1x = p0x + (2.0f / 3.0f) * (px - p0x);
                float cp1y = p0y + (2.0f / 3.0f) * (py - p0y);
                float cp2x = ex + (2.0f / 3.0f) * (px - ex);
                float cp2y = ey + (2.0f / 3.0f) * (py - ey);

                tvg_shape_cubic_to(shape, cp1x, cp1y, cp2x, cp2y, ex, ey);

                // skip the next point since we consumed it as the endpoint
                raw_i++;
            }
        }

        tvg_shape_close(shape);
    }

    // white fill with non-zero winding rule (standard for TrueType)
    tvg_shape_set_fill_color(shape, 255, 255, 255, 255);
    tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_NON_ZERO);

    // synthetic bold: thicken strokes when no true bold font variant was found.
    // stroke_width ≈ 1/14th em in pixels (~0.07*size_px), matching Chrome's approach.
    if (synth_bold_stroke > 0.0f) {
        tvg_shape_set_stroke_width(shape, synth_bold_stroke);
        tvg_shape_set_stroke_color(shape, 255, 255, 255, 255);
        tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_ROUND);
        tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_ROUND);
    }

    return shape;
}

// ============================================================================
// CBDT PNG bitmap rendering
// ============================================================================

static GlyphBitmap* render_cbdt_png(const CbdtBitmap* cbdt, uint16_t target_ppem,
                                     float bitmap_scale, Arena* arena) {
    int img_w = 0, img_h = 0, channels = 0;
    unsigned char* rgba = image_load_from_memory(cbdt->png_data, cbdt->png_len,
                                                  &img_w, &img_h, &channels);
    if (!rgba || img_w <= 0 || img_h <= 0) {
        if (rgba) image_free(rgba);
        return NULL;
    }

    // convert RGBA → BGRA (in-place swizzle: swap R and B)
    int total_pixels = img_w * img_h;
    for (int i = 0; i < total_pixels; i++) {
        uint8_t tmp = rgba[i * 4 + 0];          // R
        rgba[i * 4 + 0] = rgba[i * 4 + 2];      // R = B
        rgba[i * 4 + 2] = tmp;                    // B = R
    }

    // compute scale for fixed-size bitmap: strike_ppem vs target_ppem
    float strike_scale = (cbdt->ppem > 0 && target_ppem > 0)
                           ? (float)target_ppem / (float)cbdt->ppem
                           : 1.0f;

    // copy to arena (image_load_from_memory uses mem_alloc which needs image_free)
    size_t buf_size = (size_t)img_w * (size_t)img_h * 4;
    uint8_t* arena_buf = (uint8_t*)arena_alloc(arena, buf_size);
    if (!arena_buf) {
        image_free(rgba);
        return NULL;
    }
    memcpy(arena_buf, rgba, buf_size);
    image_free(rgba);

    GlyphBitmap* bmp = (GlyphBitmap*)arena_calloc(arena, sizeof(GlyphBitmap));
    if (!bmp) return NULL;

    bmp->buffer     = arena_buf;
    bmp->width      = img_w;
    bmp->height     = img_h;
    bmp->pitch      = img_w * 4;
    bmp->bearing_x  = (int)((float)cbdt->bearing_x * strike_scale);
    bmp->bearing_y  = (int)((float)cbdt->bearing_y * strike_scale);
    bmp->bitmap_scale = bitmap_scale * strike_scale;
    bmp->mode       = GLYPH_RENDER_NORMAL;
    bmp->pixel_mode = GLYPH_PIXEL_BGRA;

    return bmp;
}

// ============================================================================
// COLR v0 layered color glyph rendering
// ============================================================================

static GlyphBitmap* render_colr_glyph(TvgRasterCtx* ctx, FontTables* tables,
                                        uint16_t glyph_id, float size_px,
                                        float bitmap_scale, float pixel_ratio,
                                        Arena* arena) {
    ColrLayer layers[MAX_COLR_LAYERS];
    int num_layers = colr_get_layers(tables, glyph_id, layers, MAX_COLR_LAYERS);
    if (num_layers <= 0) return NULL;

    HeadTable* head = font_tables_get_head(tables);
    if (!head || head->units_per_em == 0) return NULL;

    float design_scale = size_px / (float)head->units_per_em;
    float total_scale = design_scale * pixel_ratio;

    // compute composite bounding box from all layer outlines
    float xmin = 1e30f, ymin = 1e30f, xmax = -1e30f, ymax = -1e30f;
    for (int i = 0; i < num_layers; i++) {
        int16_t bx0, by0, bx1, by1;
        if (font_tables_get_glyph_bbox(tables, layers[i].glyph_id, &bx0, &by0, &bx1, &by1)) {
            if ((float)bx0 < xmin) xmin = (float)bx0;
            if ((float)by0 < ymin) ymin = (float)by0;
            if ((float)bx1 > xmax) xmax = (float)bx1;
            if ((float)by1 > ymax) ymax = (float)by1;
        }
    }
    if (xmin >= xmax || ymin >= ymax) return NULL;

    // pixel bounds with outset
    int px_left   = (int)floorf(xmin * total_scale) - 1;
    int px_bottom = (int)floorf(ymin * total_scale) - 1;
    int px_right  = (int)ceilf(xmax * total_scale) + 1;
    int px_top    = (int)ceilf(ymax * total_scale) + 1;

    int bmp_w = px_right - px_left;
    int bmp_h = px_top - px_bottom;
    if (bmp_w > 4096 || bmp_h > 4096 || bmp_w <= 0 || bmp_h <= 0) return NULL;

    if (!ensure_buffer(ctx, bmp_w, bmp_h)) return NULL;
    memset(ctx->pixel_buf, 0, (size_t)bmp_w * (size_t)bmp_h * sizeof(uint32_t));

    Tvg_Result res = tvg_swcanvas_set_target(ctx->canvas, ctx->pixel_buf, (uint32_t)bmp_w,
                                              (uint32_t)bmp_w, (uint32_t)bmp_h,
                                              TVG_COLORSPACE_ABGR8888);
    if (res != TVG_RESULT_SUCCESS) return NULL;

    float offset_x = (float)(-px_left);
    float offset_y = (float)(-px_bottom);
    float bmp_h_f = (float)bmp_h;

    // render each layer bottom-to-top
    for (int i = 0; i < num_layers; i++) {
        GlyphOutline outline = {0};
        if (glyf_get_outline(tables, layers[i].glyph_id, &outline, arena) != 0)
            continue;
        if (outline.num_contours == 0) continue;

        Tvg_Paint shape = build_shape_from_outline(&outline, total_scale,
                                                    offset_x, offset_y, bmp_h_f,
                                                    0.0f);  // no synth bold for color layers
        if (!shape) continue;

        // override fill color with CPAL layer color
        tvg_shape_set_fill_color(shape, layers[i].r, layers[i].g, layers[i].b, layers[i].a);
        tvg_canvas_push(ctx->canvas, shape);
    }

    tvg_canvas_draw(ctx->canvas, true);
    tvg_canvas_sync(ctx->canvas);
    tvg_canvas_remove(ctx->canvas, NULL);

    // copy ABGR8888 → BGRA for GLYPH_PIXEL_BGRA output
    // ABGR8888 layout: [A][B][G][R] in memory (little-endian uint32)
    // BGRA layout: [B][G][R][A] in memory
    size_t buf_size = (size_t)bmp_w * (size_t)bmp_h * 4;
    uint8_t* bgra_buf = (uint8_t*)arena_alloc(arena, buf_size);
    if (!bgra_buf) return NULL;

    for (int row = 0; row < bmp_h; row++) {
        for (int col = 0; col < bmp_w; col++) {
            uint32_t px = ctx->pixel_buf[row * bmp_w + col];
            // ABGR8888 as uint32 (little-endian): byte0=R, byte1=G, byte2=B, byte3=A
            uint8_t r = (uint8_t)(px & 0xFF);
            uint8_t g = (uint8_t)((px >> 8) & 0xFF);
            uint8_t b = (uint8_t)((px >> 16) & 0xFF);
            uint8_t a = (uint8_t)((px >> 24) & 0xFF);
            size_t idx = (size_t)(row * bmp_w + col) * 4;
            bgra_buf[idx + 0] = b;
            bgra_buf[idx + 1] = g;
            bgra_buf[idx + 2] = r;
            bgra_buf[idx + 3] = a;
        }
    }

    GlyphBitmap* bmp = (GlyphBitmap*)arena_calloc(arena, sizeof(GlyphBitmap));
    if (!bmp) return NULL;

    bmp->buffer     = bgra_buf;
    bmp->width      = bmp_w;
    bmp->height     = bmp_h;
    bmp->pitch      = bmp_w * 4;
    bmp->bearing_x  = px_left;
    bmp->bearing_y  = px_top;
    bmp->bitmap_scale = bitmap_scale;
    bmp->mode       = GLYPH_RENDER_NORMAL;
    bmp->pixel_mode = GLYPH_PIXEL_BGRA;

    return bmp;
}

// ============================================================================
// Public: get glyph metrics from FontTables (no rasterization)
// ============================================================================

extern "C" bool font_rasterize_tvg_metrics(FontTables* tables, uint32_t codepoint,
                                            float size_px, float bitmap_scale,
                                            GlyphInfo* out) {
    if (!tables || !out) return false;

    CmapTable* cmap = font_tables_get_cmap(tables);
    if (!cmap) return false;
    uint16_t glyph_id = cmap_lookup(cmap, codepoint);
    if (glyph_id == 0) return false;

    HeadTable* head = font_tables_get_head(tables);
    HmtxTable* hmtx = font_tables_get_hmtx(tables);
    if (!head || !hmtx || head->units_per_em == 0) return false;

    float scale = size_px / (float)head->units_per_em;

    out->id = glyph_id;
    out->advance_x = (float)hmtx_get_advance(hmtx, glyph_id) * scale * bitmap_scale;
    out->advance_y = 0;

    // detect color glyph (CBDT bitmap emoji or COLR layered)
    {
        CbdtBitmap cbdt_tmp = {0};
        bool has_cbdt = cbdt_has_table(tables)
                     && cbdt_get_bitmap(tables, glyph_id, (uint16_t)(size_px * bitmap_scale), &cbdt_tmp);
        bool has_colr = colr_has_table(tables) && colr_has_glyph(tables, glyph_id);
        out->is_color = has_cbdt || has_colr;
    }

    // get bounding box for bearing/dimensions
    int16_t x_min, y_min, x_max, y_max;
    if (font_tables_get_glyph_bbox(tables, glyph_id, &x_min, &y_min, &x_max, &y_max)) {
        out->bearing_x = (float)x_min * scale * bitmap_scale;
        out->bearing_y = (float)y_max * scale * bitmap_scale;
        out->width  = (int)ceilf((float)(x_max - x_min) * scale * bitmap_scale);
        out->height = (int)ceilf((float)(y_max - y_min) * scale * bitmap_scale);
    } else {
        out->bearing_x = 0;
        out->bearing_y = 0;
        out->width = 0;
        out->height = 0;
    }

    return true;
}

// ============================================================================
// Public: rasterize a glyph to 8-bit grayscale bitmap
// ============================================================================

extern "C" GlyphBitmap* font_rasterize_tvg_render(void* tvg_ctx, FontTables* tables,
                                                    uint32_t codepoint, float size_px,
                                                    float bitmap_scale, float pixel_ratio,
                                                    float synth_bold_stroke,
                                                    Arena* arena) {
    if (!tvg_ctx || !tables || !arena) return NULL;
    TvgRasterCtx* ctx = (TvgRasterCtx*)tvg_ctx;

    CmapTable* cmap = font_tables_get_cmap(tables);
    if (!cmap) return NULL;
    uint16_t glyph_id = cmap_lookup(cmap, codepoint);
    if (glyph_id == 0) return NULL;

    HeadTable* head = font_tables_get_head(tables);
    if (!head || head->units_per_em == 0) return NULL;

    // ── CBDT bitmap emoji (highest priority) ──
    if (cbdt_has_table(tables)) {
        uint16_t target_ppem = (uint16_t)(size_px * pixel_ratio);
        CbdtBitmap cbdt_bmp = {0};
        if (cbdt_get_bitmap(tables, glyph_id, target_ppem, &cbdt_bmp) && cbdt_bmp.png_data) {
            GlyphBitmap* bmp = render_cbdt_png(&cbdt_bmp, target_ppem, bitmap_scale, arena);
            if (bmp) return bmp;
        }
    }

    // ── COLR v0 layered color glyphs ──
    if (colr_has_table(tables) && colr_has_glyph(tables, glyph_id)) {
        GlyphBitmap* bmp = render_colr_glyph(ctx, tables, glyph_id,
                                              size_px, bitmap_scale, pixel_ratio, arena);
        if (bmp) return bmp;
    }

    // ── Standard glyf outline rendering ──
    // extract outline
    GlyphOutline outline = {0};
    if (glyf_get_outline(tables, glyph_id, &outline, arena) != 0) {
        return NULL;
    }

    float render_scale = pixel_ratio;
    float design_scale = size_px / (float)head->units_per_em;

    // empty glyph (space, etc.)
    if (outline.num_contours == 0) {
        GlyphBitmap* bmp = (GlyphBitmap*)arena_calloc(arena, sizeof(GlyphBitmap));
        if (!bmp) return NULL;
        bmp->mode = GLYPH_RENDER_NORMAL;
        bmp->pixel_mode = GLYPH_PIXEL_GRAY;
        bmp->bitmap_scale = bitmap_scale;
        return bmp;
    }

    // compute pixel bounds using Skia-style roundOut + outset(1,1)
    float total_scale = design_scale * render_scale;
    int px_left   = (int)floorf((float)outline.x_min * total_scale) - 1;
    int px_bottom = (int)floorf((float)outline.y_min * total_scale) - 1;
    int px_right  = (int)ceilf((float)outline.x_max * total_scale) + 1;
    int px_top    = (int)ceilf((float)outline.y_max * total_scale) + 1;

    int bmp_w = px_right - px_left;
    int bmp_h = px_top - px_bottom;

    // clamp extreme sizes
    if (bmp_w > 4096 || bmp_h > 4096 || bmp_w <= 0 || bmp_h <= 0) {
        GlyphBitmap* bmp = (GlyphBitmap*)arena_calloc(arena, sizeof(GlyphBitmap));
        if (!bmp) return NULL;
        bmp->mode = GLYPH_RENDER_NORMAL;
        bmp->pixel_mode = GLYPH_PIXEL_GRAY;
        bmp->bitmap_scale = bitmap_scale;
        return bmp;
    }

    // ensure pixel buffer is large enough
    if (!ensure_buffer(ctx, bmp_w, bmp_h)) return NULL;

    // clear the region we'll use
    memset(ctx->pixel_buf, 0, (size_t)bmp_w * (size_t)bmp_h * sizeof(uint32_t));

    // set canvas target
    Tvg_Result res = tvg_swcanvas_set_target(ctx->canvas, ctx->pixel_buf, (uint32_t)bmp_w,
                                              (uint32_t)bmp_w, (uint32_t)bmp_h,
                                              TVG_COLORSPACE_ABGR8888);
    if (res != TVG_RESULT_SUCCESS) {
        log_debug("font_rasterize_tvg: set_target failed (%d)", res);
        return NULL;
    }

    // build shape: offset so glyph origin maps to bitmap origin
    float offset_x = (float)(-px_left);
    float offset_y = (float)(-px_bottom);
    float bmp_h_f = (float)bmp_h;

    Tvg_Paint shape = build_shape_from_outline(&outline, total_scale,
                                                offset_x, offset_y, bmp_h_f,
                                                synth_bold_stroke);
    if (!shape) return NULL;

    // render
    tvg_canvas_push(ctx->canvas, shape);
    tvg_canvas_draw(ctx->canvas, true);  // true = clear canvas before draw
    tvg_canvas_sync(ctx->canvas);
    tvg_canvas_remove(ctx->canvas, NULL);

    // extract alpha channel → 8-bit grayscale + gamma linearization
    int pitch = bmp_w;
    size_t gray_size = (size_t)pitch * (size_t)bmp_h;
    uint8_t* gray_buf = (uint8_t*)arena_alloc(arena, gray_size);
    if (!gray_buf) return NULL;

    for (int row = 0; row < bmp_h; row++) {
        for (int col = 0; col < bmp_w; col++) {
            // ABGR8888: alpha is in the highest byte
            uint32_t pixel = ctx->pixel_buf[row * bmp_w + col];
            uint8_t alpha = (uint8_t)(pixel >> 24);
            // ThorVG outputs linear coverage values directly (no gamma encoding),
            // so no gamma correction is needed here. The γ² formula used in the
            // CoreText path is only needed to undo CoreGraphics' internal gamma ~2.0
            // encoding in offscreen grayscale contexts — it does NOT apply here.
            gray_buf[row * pitch + col] = alpha;
        }
    }

    // fill output GlyphBitmap
    GlyphBitmap* bmp = (GlyphBitmap*)arena_calloc(arena, sizeof(GlyphBitmap));
    if (!bmp) return NULL;

    bmp->buffer     = gray_buf;
    bmp->width      = bmp_w;
    bmp->height     = bmp_h;
    bmp->pitch      = pitch;
    bmp->bearing_x  = px_left;
    bmp->bearing_y  = px_top;
    bmp->bitmap_scale = bitmap_scale;
    bmp->mode       = GLYPH_RENDER_NORMAL;
    bmp->pixel_mode = GLYPH_PIXEL_GRAY;

    return bmp;
}

#endif // !__APPLE__
