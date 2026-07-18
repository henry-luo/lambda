#include "render.hpp"
#include "render_effect_raster_fallback.hpp"
#include "render_glyph_run_raster_lower.hpp"
#include "render.hpp"
#include "view.hpp"
#include "layout.hpp"

#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include "../lib/font/font.h"
#include "../lib/utf.h"
#include "../lambda/input/css/dom_element.hpp"
extern "C" {
#include "../lib/url.h"
#include "../lib/pdf_writer.h"
#include "../lib/memtrack.h"
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PDF_PATH_KAPPA 0.5522847498f
#define PDF_PAINT_TRANSFORM_STACK_MAX 64
#define PDF_PAINT_EFFECT_STACK_MAX 64

typedef struct PdfPaintLoweringState {
    int active_transform_depth;
    int skipped_transform_depth;
    int active_effect_depth;
    int passthrough_effect_depth;
    RdtMatrix current_transform;
    RdtMatrix transform_stack[PDF_PAINT_TRANSFORM_STACK_MAX];
    float current_opacity;
    float opacity_stack[PDF_PAINT_EFFECT_STACK_MAX];
    bool logged_unsupported_effect;
    int command_count;
    int emitted_count;
    int fallback_count;
    int unsupported_count;
} PdfPaintLoweringState;

typedef struct PdfEffectRasterFallback {
    bool active;
    int nested_depth;
    PaintEffectGroup group;
    PaintList paint_list;
} PdfEffectRasterFallback;

typedef struct PdfRenderContext {
    HPDF_Doc pdf_doc;
    HPDF_Page current_page;
    HPDF_Font current_font;
    UiContext* ui_context;

    float page_width;
    float page_height;
    float current_x;
    float current_y;

    FontBox font;
    Color color;
    BlockBlot block;  // Current block context for coordinate transformation
    PaintList paint_list;
    PdfEffectRasterFallback effect_fallback;
    Pool* page_backdrop_pool;
    Arena* page_backdrop_arena;
    DisplayList page_backdrop_dl;
    bool page_backdrop_ready;
    PdfPaintLoweringState paint_state;
    bool transform_emitted_stack[PDF_PAINT_TRANSFORM_STACK_MAX];
    int transform_emitted_depth;
    int transform_emitted_overflow_depth;
} PdfRenderContext;

// Forward declarations
static void render_text_view_pdf(PdfRenderContext* ctx, ViewText* text);
static RenderBackend pdf_make_backend(PdfRenderContext* ctx);


// Error handler for libharu
static void pdf_error_handler(HPDF_STATUS error_no, HPDF_STATUS detail_no, void* user_data) {
    log_error("PDF Error: error_no=0x%04X, detail_no=0x%04X",
              (unsigned int)error_no, (unsigned int)detail_no);
}

static void pdf_paint_lowering_state_init(PdfPaintLoweringState* state) {
    if (!state) return;
    memset(state, 0, sizeof(PdfPaintLoweringState));
    state->current_transform = rdt_matrix_identity();
    state->transform_stack[0] = state->current_transform;
    state->current_opacity = 1.0f;
    state->opacity_stack[0] = 1.0f;
}

static PaintList* pdf_active_paint_list(PdfRenderContext* ctx) {
    if (ctx && ctx->effect_fallback.active) {
        return &ctx->effect_fallback.paint_list;
    }
    return ctx ? &ctx->paint_list : nullptr;
}

static bool pdf_effect_group_needs_raster_fallback(const PaintEffectGroup* group) {
    return group &&
           (group->has_clip || group->blend_mode != 0 || group->filter ||
            group->backdrop || group->backdrop_filter ||
            group->shadow || group->isolation);
}

static bool pdf_effect_group_needs_page_backdrop(const PaintEffectGroup* group) {
    return group &&
           (group->blend_mode != 0 || group->backdrop || group->backdrop_filter);
}

static bool pdf_effect_group_is_opacity_only(const RenderExportTargetCaps* caps,
                                             const PaintEffectGroup* group) {
    return group && caps && caps->opacity_groups &&
           !group->has_clip &&
           !group->has_transform &&
           group->blend_mode == 0 &&
           group->filter == nullptr &&
           !group->backdrop &&
           group->backdrop_filter == nullptr &&
           !group->shadow &&
           !group->isolation;
}

static bool pdf_paint_list_balanced_effect_groups(const PaintList* paint_list) {
    if (!paint_list) return false;
    int effect_depth = 0;
    for (int i = 0; i < paint_list->count; i++) {
        PaintOp op = paint_list->cmds[i].op;
        if (paint_op_has_flags(op, PAINT_OP_FLAG_EFFECT_STACK | PAINT_OP_FLAG_STACK_PUSH)) {
            effect_depth++;
        } else if (paint_op_has_flags(op, PAINT_OP_FLAG_EFFECT_STACK | PAINT_OP_FLAG_STACK_POP)) {
            effect_depth--;
            if (effect_depth < 0) return false;
        }
    }
    return effect_depth == 0;
}

static void pdf_record_page_backdrop_paint_list(PdfRenderContext* ctx,
                                                const PaintList* paint_list) {
    if (!ctx || !ctx->page_backdrop_ready || !paint_list || paint_list_count(paint_list) <= 0) {
        return;
    }
    if (!pdf_paint_list_balanced_effect_groups(paint_list)) {
        return;
    }
    render_svg_inline_register_paint_ir_lowerers();
    paint_ir_register_glyph_run_raster_lowerer(render_glyph_run_raster_lower);
    paint_ir_lower_raster(paint_list, &ctx->page_backdrop_dl);
}

// Helper function to get PDF font name from system font
static const char* get_pdf_font_name(const char* font_family) {
    if (!font_family) return "Helvetica";

    if (strstr(font_family, "Arial") || strstr(font_family, "arial")) {
        return "Helvetica";
    } else if (strstr(font_family, "Times") || strstr(font_family, "times")) {
        return "Times-Roman";
    } else if (strstr(font_family, "Courier") || strstr(font_family, "courier")) {
        return "Courier";
    }

    return "Helvetica"; // Default fallback
}

// Set PDF color from Color struct
static void pdf_set_color(PdfRenderContext* ctx, Color color) {
    float r = color.r / 255.0f;
    float g = color.g / 255.0f;
    float b = color.b / 255.0f;

    HPDF_Page_SetRGBFill(ctx->current_page, r, g, b);
    HPDF_Page_SetRGBStroke(ctx->current_page, r, g, b);
}

// Render a rectangle (for backgrounds and borders)
static void pdf_render_rect(PdfRenderContext* ctx, float x, float y, float width, float height, Color color, bool fill) {
    if (color.a == 0) return; // Transparent, don't render

    pdf_set_color(ctx, color);

    // Convert coordinates (PDF origin is bottom-left, we use top-left)
    float pdf_y = ctx->page_height - y - height;

    HPDF_Page_Rectangle(ctx->current_page, x, pdf_y, width, height);

    if (fill) {
        HPDF_Page_Fill(ctx->current_page);
    } else {
        HPDF_Page_Stroke(ctx->current_page);
    }
}

static float pdf_coord_y(PdfRenderContext* ctx, float y) {
    return ctx->page_height - y;
}

static bool pdf_page_move_to(PdfRenderContext* ctx, float x, float y) {
    return HPDF_Page_MoveTo(ctx->current_page, x, pdf_coord_y(ctx, y)) == HPDF_OK;
}

static bool pdf_page_line_to(PdfRenderContext* ctx, float x, float y) {
    return HPDF_Page_LineTo(ctx->current_page, x, pdf_coord_y(ctx, y)) == HPDF_OK;
}

static bool pdf_page_curve_to(PdfRenderContext* ctx,
                              float x1, float y1, float x2, float y2,
                              float x3, float y3) {
    return HPDF_Page_CurveTo(ctx->current_page,
                             x1, pdf_coord_y(ctx, y1),
                             x2, pdf_coord_y(ctx, y2),
                             x3, pdf_coord_y(ctx, y3)) == HPDF_OK;
}

static bool pdf_page_close_path(PdfRenderContext* ctx) {
    return HPDF_Page_ClosePath(ctx->current_page) == HPDF_OK;
}

typedef struct PdfPathContext {
    PdfRenderContext* pdf;
    const RdtMatrix* transform;
    bool has_command;
    bool has_current;
    float current_x;
    float current_y;
    float subpath_x;
    float subpath_y;
} PdfPathContext;

static void pdf_path_set_current(PdfPathContext* ctx, float x, float y) {
    ctx->current_x = x;
    ctx->current_y = y;
    ctx->has_current = true;
}

static void pdf_path_transform_point(PdfPathContext* ctx, float x, float y,
                                     float* out_x, float* out_y) {
    if (ctx->transform) {
        *out_x = ctx->transform->e11 * x + ctx->transform->e12 * y + ctx->transform->e13;
        *out_y = ctx->transform->e21 * x + ctx->transform->e22 * y + ctx->transform->e23;
    } else {
        *out_x = x;
        *out_y = y;
    }
}

static bool pdf_path_move_to(PdfPathContext* ctx, float x, float y) {
    float tx = 0.0f, ty = 0.0f;
    pdf_path_transform_point(ctx, x, y, &tx, &ty);
    if (!pdf_page_move_to(ctx->pdf, tx, ty)) return false;
    pdf_path_set_current(ctx, x, y);
    ctx->subpath_x = x;
    ctx->subpath_y = y;
    return true;
}

static bool pdf_path_line_to(PdfPathContext* ctx, float x, float y) {
    float tx = 0.0f, ty = 0.0f;
    pdf_path_transform_point(ctx, x, y, &tx, &ty);
    if (!pdf_page_line_to(ctx->pdf, tx, ty)) return false;
    pdf_path_set_current(ctx, x, y);
    return true;
}

static bool pdf_path_curve_to(PdfPathContext* ctx,
                              float x1, float y1, float x2, float y2,
                              float x3, float y3) {
    float tx1 = 0.0f, ty1 = 0.0f;
    float tx2 = 0.0f, ty2 = 0.0f;
    float tx3 = 0.0f, ty3 = 0.0f;
    pdf_path_transform_point(ctx, x1, y1, &tx1, &ty1);
    pdf_path_transform_point(ctx, x2, y2, &tx2, &ty2);
    pdf_path_transform_point(ctx, x3, y3, &tx3, &ty3);
    if (!pdf_page_curve_to(ctx->pdf, tx1, ty1, tx2, ty2, tx3, ty3)) return false;
    pdf_path_set_current(ctx, x3, y3);
    return true;
}

static bool pdf_path_close_path(PdfPathContext* ctx) {
    if (!pdf_page_close_path(ctx->pdf)) return false;
    pdf_path_set_current(ctx, ctx->subpath_x, ctx->subpath_y);
    return true;
}

static bool pdf_path_emit_rounded_rect(PdfPathContext* ctx,
                                       float x, float y, float w, float h,
                                       float rx, float ry) {
    if (rx < 0.0f) rx = 0.0f;
    if (ry < 0.0f) ry = 0.0f;
    if (rx > w * 0.5f) rx = w * 0.5f;
    if (ry > h * 0.5f) ry = h * 0.5f;

    if (rx == 0.0f && ry == 0.0f) {
        if (!ctx->transform) {
            float pdf_y = ctx->pdf->page_height - y - h;
            if (HPDF_Page_Rectangle(ctx->pdf->current_page, x, pdf_y, w, h) != HPDF_OK) {
                return false;
            }
            pdf_path_set_current(ctx, x, y);
            ctx->subpath_x = x;
            ctx->subpath_y = y;
            return true;
        }
        if (!pdf_path_move_to(ctx, x, y)) return false;
        if (!pdf_path_line_to(ctx, x + w, y)) return false;
        if (!pdf_path_line_to(ctx, x + w, y + h)) return false;
        if (!pdf_path_line_to(ctx, x, y + h)) return false;
        return pdf_path_close_path(ctx);
    }

    float sx = x + rx;
    float sy = y;
    if (!pdf_path_move_to(ctx, sx, sy)) return false;

    if (!pdf_path_line_to(ctx, x + w - rx, y)) return false;
    if (!pdf_path_curve_to(ctx,
                           x + w - rx + rx * PDF_PATH_KAPPA, y,
                           x + w, y + ry - ry * PDF_PATH_KAPPA,
                           x + w, y + ry)) return false;
    if (!pdf_path_line_to(ctx, x + w, y + h - ry)) return false;
    if (!pdf_path_curve_to(ctx,
                           x + w, y + h - ry + ry * PDF_PATH_KAPPA,
                           x + w - rx + rx * PDF_PATH_KAPPA, y + h,
                           x + w - rx, y + h)) return false;
    if (!pdf_path_line_to(ctx, x + rx, y + h)) return false;
    if (!pdf_path_curve_to(ctx,
                           x + rx - rx * PDF_PATH_KAPPA, y + h,
                           x, y + h - ry + ry * PDF_PATH_KAPPA,
                           x, y + h - ry)) return false;
    if (!pdf_path_line_to(ctx, x, y + ry)) return false;
    if (!pdf_path_curve_to(ctx,
                           x, y + ry - ry * PDF_PATH_KAPPA,
                           x + rx - rx * PDF_PATH_KAPPA, y,
                           sx, sy)) return false;
    return pdf_path_close_path(ctx);
}

static bool pdf_path_emit_circle(PdfPathContext* ctx,
                                 float cx, float cy, float rx, float ry) {
    if (rx <= 0.0f || ry <= 0.0f) return false;

    float sx = cx + rx;
    float sy = cy;
    if (!pdf_path_move_to(ctx, sx, sy)) return false;

    if (!pdf_path_curve_to(ctx,
                           cx + rx, cy + ry * PDF_PATH_KAPPA,
                           cx + rx * PDF_PATH_KAPPA, cy + ry,
                           cx, cy + ry)) return false;
    if (!pdf_path_curve_to(ctx,
                           cx - rx * PDF_PATH_KAPPA, cy + ry,
                           cx - rx, cy + ry * PDF_PATH_KAPPA,
                           cx - rx, cy)) return false;
    if (!pdf_path_curve_to(ctx,
                           cx - rx, cy - ry * PDF_PATH_KAPPA,
                           cx - rx * PDF_PATH_KAPPA, cy - ry,
                           cx, cy - ry)) return false;
    if (!pdf_path_curve_to(ctx,
                           cx + rx * PDF_PATH_KAPPA, cy - ry,
                           cx + rx, cy - ry * PDF_PATH_KAPPA,
                           sx, sy)) return false;
    return pdf_path_close_path(ctx);
}

static bool pdf_path_visit(void* context, RdtPathCommand command,
                           const float* args, int arg_count) {
    PdfPathContext* ctx = (PdfPathContext*)context;
    if (!ctx || !ctx->pdf) return false;
    ctx->has_command = true;

    switch (command) {
    case RDT_PATH_MOVE:
        if (arg_count < 2) return false;
        if (!pdf_path_move_to(ctx, args[0], args[1])) return false;
        break;
    case RDT_PATH_LINE:
        if (arg_count < 2) return false;
        if (!pdf_path_line_to(ctx, args[0], args[1])) return false;
        break;
    case RDT_PATH_QUAD: {
        if (arg_count < 4 || !ctx->has_current) return false;
        float c1x = ctx->current_x + (args[0] - ctx->current_x) * (2.0f / 3.0f);
        float c1y = ctx->current_y + (args[1] - ctx->current_y) * (2.0f / 3.0f);
        float c2x = args[2] + (args[0] - args[2]) * (2.0f / 3.0f);
        float c2y = args[3] + (args[1] - args[3]) * (2.0f / 3.0f);
        if (!pdf_path_curve_to(ctx, c1x, c1y, c2x, c2y, args[2], args[3])) return false;
        break;
    }
    case RDT_PATH_CUBIC:
        if (arg_count < 6) return false;
        if (!pdf_path_curve_to(ctx, args[0], args[1], args[2], args[3],
                               args[4], args[5])) return false;
        break;
    case RDT_PATH_CLOSE:
        if (!pdf_path_close_path(ctx)) return false;
        break;
    case RDT_PATH_RECT:
        if (arg_count < 6) return false;
        if (!pdf_path_emit_rounded_rect(ctx, args[0], args[1], args[2], args[3],
                                        args[4], args[5])) return false;
        break;
    case RDT_PATH_CIRCLE:
        if (arg_count < 4) return false;
        if (!pdf_path_emit_circle(ctx, args[0], args[1], args[2], args[3])) return false;
        break;
    }

    return true;
}

static bool pdf_render_path(PdfRenderContext* ctx, RdtPath* path,
                            const RdtMatrix* transform) {
    if (!ctx || !path) return false;
    PdfPathContext path_ctx = {};
    path_ctx.pdf = ctx;
    path_ctx.transform = transform;
    if (!rdt_path_visit(path, pdf_path_visit, &path_ctx)) return false;
    return path_ctx.has_command;
}

static bool pdf_fill_rounded_rect_path(PdfRenderContext* ctx,
                                       const RdtMatrix* transform,
                                       float x, float y,
                                       float width, float height,
                                       float radius_x, float radius_y) {
    PdfPathContext path_ctx = {};
    path_ctx.pdf = ctx;
    path_ctx.transform = transform;
    if (!pdf_path_emit_rounded_rect(&path_ctx, x, y, width, height,
                                    radius_x, radius_y)) {
        return false;
    }
    HPDF_Page_Fill(ctx->current_page);
    return true;
}

static int pdf_image_stride_pixels(int src_w, int src_stride) {
    if (src_stride >= src_w * 4 && (src_stride % 4) == 0) {
        return src_stride / 4;
    }
    return src_stride;
}

static bool pdf_image_pixels_are_opaque(const uint32_t* pixels,
                                        int src_w, int src_h, int stride_pixels) {
    if (!pixels || src_w <= 0 || src_h <= 0 || stride_pixels < src_w) return false;
    for (int y = 0; y < src_h; y++) {
        const uint32_t* row = pixels + y * stride_pixels;
        for (int x = 0; x < src_w; x++) {
            if ((row[x] >> 24) != 0xff) return false;
        }
    }
    return true;
}

static void pdf_transform_point(const RdtMatrix* transform, float x, float y,
                                float* out_x, float* out_y) {
    if (transform) {
        *out_x = transform->e11 * x + transform->e12 * y + transform->e13;
        *out_y = transform->e21 * x + transform->e22 * y + transform->e23;
    } else {
        *out_x = x;
        *out_y = y;
    }
}

static const RdtMatrix* pdf_optional_transform(bool has_transform,
                                               const RdtMatrix* transform) {
    return has_transform ? transform : NULL;
}

static const RdtMatrix* pdf_compose_transform(const RdtMatrix* stack_transform,
                                              const RdtMatrix* local_transform,
                                              RdtMatrix* out) {
    if (stack_transform && local_transform) {
        *out = rdt_matrix_multiply(stack_transform, local_transform);
        return out;
    }
    if (stack_transform) return stack_transform;
    return local_transform;
}

static bool pdf_paint_effective_transform(const RenderExportTargetCaps* caps, const RdtMatrix* stack_transform,
                                          bool has_local_transform, const RdtMatrix* local_transform,
                                          RdtMatrix* composed_transform, const RdtMatrix** effective_transform) {
    if (has_local_transform && (!caps || !caps->transforms)) return false;
    *effective_transform = pdf_compose_transform(stack_transform,
        pdf_optional_transform(has_local_transform, local_transform), composed_transform);
    return true;
}

static bool pdf_draw_abgr_image_impl(PdfRenderContext* ctx,
                                     const uint32_t* pixels,
                                     int src_w, int src_h, int src_stride,
                                     float dst_x, float dst_y,
                                     float dst_w, float dst_h,
                                     uint8_t opacity,
                                     const RdtMatrix* transform,
                                     bool preserve_alpha) {
    if (!ctx || !pixels || src_w <= 0 || src_h <= 0 || dst_w <= 0.0f || dst_h <= 0.0f) {
        return false;
    }
    if (opacity != 255) return false;

    int stride_pixels = pdf_image_stride_pixels(src_w, src_stride);
    bool opaque = pdf_image_pixels_are_opaque(pixels, src_w, src_h, stride_pixels);
    if (!opaque && !preserve_alpha) {
        return false;
    }

    float x0 = 0.0f, y0 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f;
    pdf_transform_point(transform, dst_x, dst_y + dst_h, &x0, &y0);
    pdf_transform_point(transform, dst_x + dst_w, dst_y + dst_h, &x1, &y1);
    pdf_transform_point(transform, dst_x, dst_y, &x2, &y2);

    float a = x1 - x0;
    float b = pdf_coord_y(ctx, y1) - pdf_coord_y(ctx, y0);
    float c = x2 - x0;
    float d = pdf_coord_y(ctx, y2) - pdf_coord_y(ctx, y0);
    float e = x0;
    float f = pdf_coord_y(ctx, y0);

    if (opaque) {
        return HPDF_Page_DrawABGRImage(ctx->current_page, pixels, src_w, src_h,
                                       stride_pixels, a, b, c, d, e, f) == HPDF_OK;
    }
    return HPDF_Page_DrawABGRImageWithAlpha(ctx->current_page, pixels,
                                            src_w, src_h, stride_pixels,
                                            a, b, c, d, e, f) == HPDF_OK;
}

static bool pdf_draw_abgr_image(PdfRenderContext* ctx, const uint32_t* pixels,
                                int src_w, int src_h, int src_stride,
                                float dst_x, float dst_y, float dst_w, float dst_h,
                                uint8_t opacity, const RdtMatrix* transform) {
    return pdf_draw_abgr_image_impl(ctx, pixels, src_w, src_h, src_stride,
                                    dst_x, dst_y, dst_w, dst_h, opacity,
                                    transform, false);
}

static bool pdf_draw_abgr_image_preserve_alpha(PdfRenderContext* ctx,
                                               const uint32_t* pixels,
                                               int src_w, int src_h, int src_stride,
                                               float dst_x, float dst_y,
                                               float dst_w, float dst_h,
                                               uint8_t opacity,
                                               const RdtMatrix* transform) {
    return pdf_draw_abgr_image_impl(ctx, pixels, src_w, src_h, src_stride,
                                    dst_x, dst_y, dst_w, dst_h, opacity,
                                    transform, true);
}

static void pdf_paint_record_fallback(PdfPaintLoweringState* state,
                                      bool ok,
                                      const char* failure_message) {
    if (!state) return;
    if (!ok) {
        if (failure_message) log_error("%s", failure_message);
        state->unsupported_count++;
        return;
    }
    state->fallback_count++;
    state->emitted_count++;
}

static void pdf_begin_effect_raster_fallback(PdfRenderContext* ctx,
                                             const PaintEffectGroup* group) {
    if (!ctx || !group) return;
    ctx->effect_fallback.active = true;
    ctx->effect_fallback.nested_depth = 0;
    ctx->effect_fallback.group = *group;
    paint_list_clear(&ctx->effect_fallback.paint_list);
    paint_ir_register_glyph_run_raster_lowerer(render_glyph_run_raster_lower);
    paint_begin_effect_group(&ctx->effect_fallback.paint_list, group);
    log_error("[PDF_PAINT_IR] raster fallback effect group opacity=%.3f blend=%d filter=%p backdrop=%d backdrop_filter=%p shadow=%d isolation=%d",
              group->opacity, group->blend_mode, group->filter,
              group->backdrop ? 1 : 0, group->backdrop_filter,
              group->shadow ? 1 : 0,
              group->isolation ? 1 : 0);
}

static void pdf_finish_effect_raster_fallback(PdfRenderContext* ctx) {
    if (!ctx || !ctx->effect_fallback.active) return;
    paint_end_effect_group(&ctx->effect_fallback.paint_list);

    RenderEffectRasterImage image = {};
    bool ok = render_effect_rasterize_paint_list(
        &ctx->effect_fallback.paint_list,
        pdf_effect_group_needs_page_backdrop(&ctx->effect_fallback.group)
            ? &ctx->page_backdrop_dl
            : nullptr,
        &ctx->effect_fallback.group.bounds,
        ctx->page_width,
        ctx->page_height,
        false,
        &image,
        "[PDF_PAINT_IR]");
    ctx->paint_state.command_count += paint_list_count(&ctx->effect_fallback.paint_list);
    bool drew_fallback = ok && image.surface &&
        pdf_draw_abgr_image_preserve_alpha(ctx, (const uint32_t*)image.surface->pixels,
                                           image.surface->width, image.surface->height,
                                           image.surface->width,
                                           image.x, image.y, image.width, image.height,
                                           255, &ctx->paint_state.current_transform);
    pdf_paint_record_fallback(&ctx->paint_state, drew_fallback, nullptr);
    pdf_record_page_backdrop_paint_list(ctx, &ctx->effect_fallback.paint_list);
    if (image.surface) image_surface_destroy(image.surface);
    paint_list_clear(&ctx->effect_fallback.paint_list);
    ctx->effect_fallback.active = false;
    ctx->effect_fallback.nested_depth = 0;
}

static bool pdf_push_clip_path(PdfRenderContext* ctx, RdtPath* path,
                               const RdtMatrix* transform) {
    if (!ctx || !path) return false;
    if (HPDF_Page_GSave(ctx->current_page) != HPDF_OK) return false;
    if (!pdf_render_path(ctx, path, transform)) {
        HPDF_Page_GRestore(ctx->current_page);
        return false;
    }
    if (HPDF_Page_Clip(ctx->current_page) != HPDF_OK) {
        HPDF_Page_GRestore(ctx->current_page);
        return false;
    }
    return true;
}

static void pdf_text_matrix_from_transform(PdfRenderContext* ctx,
                                           const RdtMatrix* transform,
                                           float x, float baseline_y,
                                           float* a, float* b, float* c,
                                           float* d, float* e, float* f) {
    float tx = x;
    float ty = baseline_y;
    pdf_transform_point(transform, x, baseline_y, &tx, &ty);
    *a = transform->e11;
    *b = -transform->e21;
    *c = -transform->e12;
    *d = transform->e22;
    *e = tx;
    *f = ctx->page_height - ty;
}

static void pdf_show_text_word(PdfRenderContext* ctx, const char* word,
                               float x, float baseline_y,
                               const RdtMatrix* transform) {
    HPDF_Page_BeginText(ctx->current_page);
    if (transform) {
        float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = x;
        float f = ctx->page_height - baseline_y;
        pdf_text_matrix_from_transform(ctx, transform, x, baseline_y,
                                       &a, &b, &c, &d, &e, &f);
        HPDF_Page_SetTextMatrix(ctx->current_page, a, b, c, d, e, f);
        HPDF_Page_ShowText(ctx->current_page, word);
    } else {
        HPDF_Page_TextOut(ctx->current_page, x, ctx->page_height - baseline_y, word);
    }
    HPDF_Page_EndText(ctx->current_page);
}

static void pdf_render_glyph_run(PdfRenderContext* ctx, const PaintGlyphRun* run,
                                 const RdtMatrix* stack_transform) {
    if (!ctx || !run || !run->text) return;
    if (!ctx->current_font) return;

    float font_size = run->font_size > 0.0f ? run->font_size : 16.0f;
    HPDF_Page_SetFontAndSize(ctx->current_page, ctx->current_font, font_size);
    pdf_set_color(ctx, run->color);

    RdtMatrix composed_transform;
    const RdtMatrix* effective_transform =
        pdf_compose_transform(stack_transform,
                              pdf_optional_transform(run->has_transform, &run->transform),
                              &composed_transform);

    FontBox* font = (FontBox*)run->font;
    FontHandle* font_handle = font ? font->font_handle : nullptr;
    float space_width = font && font->style ? font->style->space_width : 4.0f;
    float adjusted_space_width = space_width + run->word_spacing;
    if (adjusted_space_width < 0.0f) adjusted_space_width = space_width;

    int text_len = run->text_len;
    if (text_len < 0) {
        text_len = (int)strlen(run->text); // INT_CAST_OK: text run byte length is bounded by source TextRect.
    }
    if (text_len <= 0) return;

    float x = run->x;
    int word_start = 0;
    for (int i = 0; i <= text_len; i++) {
        bool at_end = i == text_len;
        bool at_space = !at_end && run->text[i] == ' ';
        if (!at_end && !at_space) continue;

        if (i > word_start) {
            char word[256];
            int word_len = i - word_start;
            if (word_len < (int)sizeof(word)) { // INT_CAST_OK: fixed local buffer size check.
                memcpy(word, run->text + word_start, (size_t)word_len);
                word[word_len] = '\0';

                pdf_show_text_word(ctx, word, x, run->baseline_y,
                                   effective_transform);

                if (font_handle) {
                    for (int j = 0; j < word_len; j++) {
                        x += font_measure_char(font_handle, (uint32_t)word[j]);
                    }
                }
            }
        }

        if (!at_end) {
            x += adjusted_space_width;
        }
        word_start = i + 1;
    }
}

static Color pdf_gradient_composite_opaque(Color src) {
    if (src.a == 255) return src;
    Color out = {};
    uint32_t a = src.a;
    out.r = (uint8_t)((src.r * a + 255u * (255u - a)) / 255u);
    out.g = (uint8_t)((src.g * a + 255u * (255u - a)) / 255u);
    out.b = (uint8_t)((src.b * a + 255u * (255u - a)) / 255u);
    out.a = 255;
    return out;
}

static Color pdf_gradient_sample_stops(const RdtGradientStop* stops, int stop_count,
                                       float t) {
    Color out = {};
    out.a = 255;
    if (!stops || stop_count <= 0) return out;
    if (t <= stops[0].offset || stop_count == 1) {
        out.r = stops[0].r;
        out.g = stops[0].g;
        out.b = stops[0].b;
        out.a = stops[0].a;
        return pdf_gradient_composite_opaque(out);
    }
    for (int i = 1; i < stop_count; i++) {
        const RdtGradientStop* prev = &stops[i - 1];
        const RdtGradientStop* next = &stops[i];
        if (t > next->offset) continue;
        float span = next->offset - prev->offset;
        float local_t = span > 1e-6f ? (t - prev->offset) / span : 0.0f;
        if (local_t < 0.0f) local_t = 0.0f;
        if (local_t > 1.0f) local_t = 1.0f;
        out.r = (uint8_t)(prev->r + (next->r - prev->r) * local_t);
        out.g = (uint8_t)(prev->g + (next->g - prev->g) * local_t);
        out.b = (uint8_t)(prev->b + (next->b - prev->b) * local_t);
        out.a = (uint8_t)(prev->a + (next->a - prev->a) * local_t);
        return pdf_gradient_composite_opaque(out);
    }
    out.r = stops[stop_count - 1].r;
    out.g = stops[stop_count - 1].g;
    out.b = stops[stop_count - 1].b;
    out.a = stops[stop_count - 1].a;
    return pdf_gradient_composite_opaque(out);
}

static bool pdf_gradient_path_bounds(RdtPath* path, float* left, float* top,
                                     float* width, float* height) {
    if (!path || !left || !top || !width || !height) return false;
    float l = 0.0f;
    float t = 0.0f;
    float r = 0.0f;
    float b = 0.0f;
    if (!rdt_path_get_bounds(path, &l, &t, &r, &b)) return false;
    float w = r - l;
    float h = b - t;
    if (w <= 0.0f || h <= 0.0f) return false;
    *left = l;
    *top = t;
    *width = w;
    *height = h;
    return true;
}

static bool pdf_draw_gradient_surface(PdfRenderContext* ctx, RdtPath* path,
                                      const RdtMatrix* transform,
                                      ImageSurface* surface,
                                      float left, float top,
                                      float width, float height) {
    if (!ctx || !path || !surface || !surface->pixels) return false;
    bool clipped = pdf_push_clip_path(ctx, path, transform);
    if (!clipped) return false;
    bool ok = pdf_draw_abgr_image(ctx, (const uint32_t*)surface->pixels,
                                  surface->width, surface->height,
                                  surface->width,
                                  left, top, width, height,
                                  255, transform);
    HPDF_Page_GRestore(ctx->current_page);
    return ok;
}

typedef float (*PdfGradientPosition)(const void* paint, float x, float y);

static float pdf_linear_gradient_position(const void* paint, float x, float y) {
    const PaintFillLinearGradient* p = (const PaintFillLinearGradient*)paint;
    float dx = p->x2 - p->x1;
    float dy = p->y2 - p->y1;
    float denom = dx * dx + dy * dy;
    return denom > 1e-6f
        ? ((x - p->x1) * dx + (y - p->y1) * dy) / denom : 0.0f;
}

static float pdf_radial_gradient_position(const void* paint, float x, float y) {
    const PaintFillRadialGradient* p = (const PaintFillRadialGradient*)paint;
    return hypotf(x - p->cx, y - p->cy) / p->r;
}

static bool pdf_raster_fallback_gradient(PdfRenderContext* ctx,
                                         RdtPath* path,
                                         const RdtGradientStop* stops,
                                         int stop_count,
                                         const RdtMatrix* transform,
                                         const void* paint,
                                         PdfGradientPosition position,
                                         const char* kind) {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (!pdf_gradient_path_bounds(path, &left, &top, &width, &height)) return false;

    int surface_w = (int)ceilf(width); // INT_CAST_OK: PDF gradient fallback surface dimensions are integer pixels.
    int surface_h = (int)ceilf(height); // INT_CAST_OK: PDF gradient fallback surface dimensions are integer pixels.
    if (surface_w <= 0 || surface_h <= 0) return false;
    ImageSurface* surface = image_surface_create(surface_w, surface_h);
    if (!surface) return false;

    uint32_t* pixels = (uint32_t*)surface->pixels;
    for (int py = 0; py < surface_h; py++) {
        float y = top + ((float)py + 0.5f) * height / (float)surface_h;
        for (int px = 0; px < surface_w; px++) {
            float x = left + ((float)px + 0.5f) * width / (float)surface_w;
            float t = position(paint, x, y);
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            pixels[py * surface_w + px] =
                pdf_gradient_sample_stops(stops, stop_count, t).c;
        }
    }

    log_info("[PDF_PAINT_IR] raster fallback %s gradient %.1fx%.1f to %dx%d",
             kind, width, height, surface_w, surface_h);
    bool ok = pdf_draw_gradient_surface(ctx, path, transform, surface,
                                        left, top, width, height);
    image_surface_destroy(surface);
    return ok;
}

static bool pdf_raster_fallback_linear_gradient(PdfRenderContext* ctx,
                                                const PaintFillLinearGradient* p,
                                                const RdtMatrix* transform) {
    if (!ctx || !p || !p->path || !p->stops || p->stop_count <= 0) return false;
    return pdf_raster_fallback_gradient(ctx, p->path, p->stops, p->stop_count,
                                        transform, p,
                                        pdf_linear_gradient_position, "linear");
}

static bool pdf_raster_fallback_radial_gradient(PdfRenderContext* ctx,
                                                const PaintFillRadialGradient* p,
                                                const RdtMatrix* transform) {
    if (!ctx || !p || !p->path || !p->stops || p->stop_count <= 0 ||
        p->r <= 0.0f) {
        return false;
    }
    return pdf_raster_fallback_gradient(ctx, p->path, p->stops, p->stop_count,
                                        transform, p,
                                        pdf_radial_gradient_position, "radial");
}

static bool pdf_raster_fallback_svg_subscene(PdfRenderContext* ctx,
                                             const PaintSvgSubscene* subscene,
                                             const RdtMatrix* transform) {
    if (!ctx || !subscene || !subscene->svg_root ||
        subscene->viewport_width <= 0.0f ||
        subscene->viewport_height <= 0.0f) {
        return false;
    }

    int surface_w = (int)ceilf(subscene->viewport_width); // INT_CAST_OK: PDF SVG subscene fallback surface width is integer pixels.
    int surface_h = (int)ceilf(subscene->viewport_height); // INT_CAST_OK: PDF SVG subscene fallback surface height is integer pixels.
    if (surface_w <= 0 || surface_h <= 0) return false;

    ImageSurface* surface = image_surface_create(surface_w, surface_h);
    if (!surface) {
        log_error("[PDF_PAINT_IR] failed to allocate SVG subscene surface %dx%d",
                  surface_w, surface_h);
        return false;
    }

    uint32_t* pixels = (uint32_t*)surface->pixels;
    int pixel_count = surface_w * surface_h;
    for (int i = 0; i < pixel_count; i++) {
        pixels[i] = 0xffffffffu;
    }

    RdtVector vec = {};
    rdt_vector_init(&vec, pixels, surface_w, surface_h, surface_w);

    Color* current_color = subscene->has_color ? (Color*)&subscene->color : nullptr;
    Color* fill_color = subscene->has_fill ? (Color*)&subscene->fill : nullptr;
    Color* stroke_color = subscene->has_stroke ? (Color*)&subscene->stroke : nullptr;
    render_svg_to_vec_via_display_list(&vec,
                                       (Element*)subscene->svg_root,
                                       subscene->viewport_width,
                                       subscene->viewport_height,
                                       (Pool*)subscene->pool,
                                       subscene->pixel_ratio,
                                       (FontContext*)subscene->font_context,
                                       nullptr,
                                       current_color,
                                       fill_color,
                                       subscene->source_path,
                                       subscene->opacity,
                                       subscene->fill_none,
                                       stroke_color,
                                       subscene->stroke_none,
                                       subscene->stroke_width);
    rdt_vector_flush_batch(&vec);

    float dst_x = subscene->content_clip.left;
    float dst_y = subscene->content_clip.top;
    float dst_w = subscene->content_clip.right - subscene->content_clip.left;
    float dst_h = subscene->content_clip.bottom - subscene->content_clip.top;
    if (dst_w <= 0.0f) dst_w = subscene->viewport_width;
    if (dst_h <= 0.0f) dst_h = subscene->viewport_height;

    log_info("[PDF_PAINT_IR] raster fallback SVG subscene %.1fx%.1f to %dx%d",
             dst_w, dst_h, surface_w, surface_h);
    bool ok = pdf_draw_abgr_image(ctx, pixels, surface_w, surface_h, surface_w,
                                  dst_x, dst_y, dst_w, dst_h,
                                  255, transform);

    rdt_vector_destroy(&vec);
    image_surface_destroy(surface);
    return ok;
}

static void pdf_lower_paint_list(PdfRenderContext* ctx) {
    if (!ctx || ctx->effect_fallback.active) return;
    render_svg_inline_register_paint_ir_lowerers();

    PdfPaintLoweringState* state = &ctx->paint_state;
    bool streaming_transform =
        paint_list_has_op_flags(&ctx->paint_list, PAINT_OP_FLAG_TRANSFORM_STACK) ||
        state->active_transform_depth > 0 ||
        state->skipped_transform_depth > 0;
    bool streaming_effect =
        paint_list_has_op_flags(&ctx->paint_list, PAINT_OP_FLAG_EFFECT_STACK) ||
        state->active_effect_depth > 0 ||
        state->passthrough_effect_depth > 0;
    if (!streaming_transform && !streaming_effect &&
        !paint_ir_validate_or_log(&ctx->paint_list, "pdf_lower_paint_list")) {
        paint_list_clear(&ctx->paint_list);
        return;
    }
    pdf_record_page_backdrop_paint_list(ctx, &ctx->paint_list);
    const RenderExportTargetCaps* caps =
        render_export_target_get_caps(RENDER_EXPORT_TARGET_PDF);
    int active_clip_depth = 0;
    int skipped_clip_depth = 0;
    auto handle_transform_stack = [&](PaintCmd* cmd) -> bool {
        if (!paint_op_has_flags(cmd->op, PAINT_OP_FLAG_TRANSFORM_STACK)) return false;
        if (paint_op_has_flags(cmd->op, PAINT_OP_FLAG_STACK_PUSH)) {
            if (state->skipped_transform_depth > 0) {
                state->skipped_transform_depth++;
                return true;
            }
            if (!caps || !caps->transforms) {
                state->skipped_transform_depth++;
                return true;
            }
            if (state->active_transform_depth >= PDF_PAINT_TRANSFORM_STACK_MAX) {
                log_error("[PDF_PAINT_IR] transform stack overflow in lowerer");
                state->skipped_transform_depth++;
                return true;
            }
            PaintPushTransform* p = &cmd->push_transform;
            state->transform_stack[state->active_transform_depth] =
                state->current_transform;
            state->current_transform = state->active_transform_depth > 0
                ? rdt_matrix_multiply(&state->current_transform, &p->transform)
                : p->transform;
            state->active_transform_depth++;
            return true;
        }
        if (paint_op_has_flags(cmd->op, PAINT_OP_FLAG_STACK_POP)) {
            if (state->skipped_transform_depth > 0) {
                state->skipped_transform_depth--;
            } else if (state->active_transform_depth > 0) {
                state->active_transform_depth--;
                state->current_transform =
                    state->transform_stack[state->active_transform_depth];
            } else {
                log_error("[PDF_PAINT_IR] transform stack underflow in lowerer");
            }
            return true;
        }
        return false;
    };
    auto handle_effect_stack = [&](PaintCmd* cmd) -> bool {
        if (!paint_op_has_flags(cmd->op, PAINT_OP_FLAG_EFFECT_STACK)) return false;
        if (paint_op_has_flags(cmd->op, PAINT_OP_FLAG_STACK_PUSH)) {
            PaintEffectGroup* p = &cmd->effect_group;
            if (!pdf_effect_group_is_opacity_only(caps, p)) {
                state->passthrough_effect_depth++;
                if (!state->logged_unsupported_effect) {
                    log_error("[PDF_PAINT_IR] fallback effect group rendered as passthrough content");
                    state->logged_unsupported_effect = true;
                }
                state->fallback_count++;
                state->emitted_count++;
                return true;
            }
            if (state->active_effect_depth >= PDF_PAINT_EFFECT_STACK_MAX) {
                state->passthrough_effect_depth++;
                log_error("[PDF_PAINT_IR] opacity effect stack overflow; rendering group without opacity");
                return true;
            }
            state->opacity_stack[state->active_effect_depth++] = state->current_opacity;
            state->current_opacity *= p->opacity;
            if (state->current_opacity < 0.0f) state->current_opacity = 0.0f;
            if (state->current_opacity > 1.0f) state->current_opacity = 1.0f;
            if (HPDF_Page_GSave(ctx->current_page) != HPDF_OK) {
                log_error("[PDF_PAINT_IR] failed to save PDF graphics state for opacity group");
                return true;
            }
            HPDF_ExtGState ext_gstate = HPDF_CreateExtGState(ctx->pdf_doc);
            if (!ext_gstate) {
                log_error("[PDF_PAINT_IR] failed to create PDF opacity ExtGState");
                return true;
            }
            HPDF_ExtGState_SetAlphaFill(ext_gstate, state->current_opacity);
            HPDF_ExtGState_SetAlphaStroke(ext_gstate, state->current_opacity);
            if (HPDF_Page_SetExtGState(ctx->current_page, ext_gstate) != HPDF_OK) {
                log_error("[PDF_PAINT_IR] failed to apply PDF opacity ExtGState");
                return true;
            }
            state->emitted_count++;
            return true;
        }
        if (paint_op_has_flags(cmd->op, PAINT_OP_FLAG_STACK_POP)) {
            if (state->passthrough_effect_depth > 0) {
                state->passthrough_effect_depth--;
                state->emitted_count++;
                return true;
            }
            if (state->active_effect_depth <= 0) {
                log_error("[PDF_PAINT_IR] opacity effect stack underflow");
                return true;
            }
            state->current_opacity = state->opacity_stack[--state->active_effect_depth];
            HPDF_Page_GRestore(ctx->current_page);
            state->emitted_count++;
            return true;
        }
        return false;
    };

    for (int i = 0; i < ctx->paint_list.count; i++) {
        PaintCmd* cmd = &ctx->paint_list.cmds[i];
        state->command_count++;
        if (handle_transform_stack(cmd)) continue;
        if (handle_effect_stack(cmd)) continue;

        if (state->skipped_transform_depth > 0) {
            continue;
        }

        const RdtMatrix* stack_transform =
            state->active_transform_depth > 0 ? &state->current_transform : NULL;
        RdtMatrix composed_transform;
        const RdtMatrix* effective_transform = NULL;
        auto resolve_command_transform = [&](bool has_transform,
                                             const RdtMatrix* transform) -> bool {
            return pdf_paint_effective_transform(caps, stack_transform,
                                                 has_transform, transform,
                                                 &composed_transform,
                                                 &effective_transform);
        };
        auto handle_clip_stack = [&](PaintCmd* cmd) -> bool {
            if (!paint_op_has_flags(cmd->op, PAINT_OP_FLAG_CLIP_STACK)) return false;
            if (paint_op_has_flags(cmd->op, PAINT_OP_FLAG_STACK_PUSH)) {
                if (skipped_clip_depth > 0) {
                    skipped_clip_depth++;
                    return true;
                }
                if (!caps || !caps->clips) {
                    skipped_clip_depth++;
                    return true;
                }
                PaintPushClip* p = &cmd->push_clip;
                if (!resolve_command_transform(p->has_transform, &p->transform)) {
                    skipped_clip_depth++;
                    return true;
                }
                if (pdf_push_clip_path(ctx, p->clip_path,
                                       effective_transform)) {
                    active_clip_depth++;
                } else {
                    skipped_clip_depth++;
                }
                return true;
            }
            if (paint_op_has_flags(cmd->op, PAINT_OP_FLAG_STACK_POP)) {
                if (skipped_clip_depth > 0) {
                    skipped_clip_depth--;
                } else if (active_clip_depth > 0) {
                    HPDF_Page_GRestore(ctx->current_page);
                    active_clip_depth--;
                }
                return true;
            }
            return false;
        };
        if (handle_clip_stack(cmd)) continue;
        switch (cmd->op) {
        case PAINT_FILL_RECT: {
            if (!caps || !caps->rects) break;
            PaintFillRect* p = &cmd->fill_rect;
            if (!stack_transform) {
                pdf_render_rect(ctx, p->x, p->y, p->w, p->h, p->color, true);
                state->emitted_count++;
                break;
            }
            if (p->color.a == 0) break;
            pdf_set_color(ctx, p->color);
            if (pdf_fill_rounded_rect_path(ctx, stack_transform,
                                           p->x, p->y, p->w, p->h,
                                           0.0f, 0.0f)) {
                state->emitted_count++;
            }
            break;
        }
        case PAINT_FILL_ROUNDED_RECT: {
            if (!caps || !caps->rounded_rects) break;
            PaintFillRoundedRect* p = &cmd->fill_rounded_rect;
            if (p->color.a == 0) break;
            pdf_set_color(ctx, p->color);
            if (pdf_fill_rounded_rect_path(ctx, stack_transform,
                                           p->x, p->y, p->w, p->h,
                                           p->rx, p->ry)) {
                state->emitted_count++;
            }
            break;
        }
        case PAINT_FILL_PATH: {
            if (!caps || !caps->paths) break;
            PaintFillPath* p = &cmd->fill_path;
            if (p->color.a == 0 || p->rule != RDT_FILL_WINDING) break;
            if (!resolve_command_transform(p->has_transform, &p->transform)) break;
            pdf_set_color(ctx, p->color);
            if (pdf_render_path(ctx, p->path, effective_transform)) {
                HPDF_Page_Fill(ctx->current_page);
                state->emitted_count++;
            }
            break;
        }
        case PAINT_STROKE_PATH: {
            if (!caps || !caps->strokes) break;
            PaintStrokePath* p = &cmd->stroke_path;
            if (p->color.a == 0 || p->dash_count > 0) break;
            if (!resolve_command_transform(p->has_transform, &p->transform)) break;
            pdf_set_color(ctx, p->color);
            HPDF_Page_SetLineWidth(ctx->current_page, p->width);
            if (pdf_render_path(ctx, p->path, effective_transform)) {
                HPDF_Page_Stroke(ctx->current_page);
                state->emitted_count++;
            }
            break;
        }
        case PAINT_FILL_LINEAR_GRADIENT: {
            if (!caps || !caps->gradients) break;
            PaintFillLinearGradient* p = &cmd->fill_linear_gradient;
            if (!resolve_command_transform(p->has_transform, &p->transform)) break;
            pdf_paint_record_fallback(state,
                                      pdf_raster_fallback_linear_gradient(ctx, p, effective_transform),
                                      "[PDF_PAINT_IR] failed linear gradient raster fallback");
            break;
        }
        case PAINT_FILL_RADIAL_GRADIENT: {
            if (!caps || !caps->gradients) break;
            PaintFillRadialGradient* p = &cmd->fill_radial_gradient;
            if (!resolve_command_transform(p->has_transform, &p->transform)) break;
            pdf_paint_record_fallback(state,
                                      pdf_raster_fallback_radial_gradient(ctx, p, effective_transform),
                                      "[PDF_PAINT_IR] failed radial gradient raster fallback");
            break;
        }
        case PAINT_DRAW_IMAGE: {
            if (!caps || !caps->images) break;
            PaintDrawImage* p = &cmd->draw_image;
            if (!resolve_command_transform(p->has_transform, &p->transform)) break;
            pdf_draw_abgr_image(ctx, p->pixels, p->src_w, p->src_h, p->src_stride,
                                p->dst_x, p->dst_y, p->dst_w, p->dst_h,
                                p->opacity, effective_transform);
            state->emitted_count++;
            break;
        }
        case PAINT_DRAW_IMAGE_RESOURCE: {
            if (!caps || !caps->images) break;
            PaintDrawImageResource* p = &cmd->draw_image_resource;
            if (!resolve_command_transform(p->has_transform, &p->transform)) break;
            ImageSurface* img = p->image;
            if (!img || p->dst_w <= 0.0f || p->dst_h <= 0.0f) break;
            int target_w = (int)ceilf(p->dst_w); // INT_CAST_OK: image decode target width is integer pixels.
            int target_h = (int)ceilf(p->dst_h); // INT_CAST_OK: image decode target height is integer pixels.
            if (target_w <= 0 || target_h <= 0) break;
            image_surface_ensure_decoded(img, target_w, target_h);
            int src_w = img->decoded_width > 0 ? img->decoded_width : img->width;
            int src_h = img->decoded_height > 0 ? img->decoded_height : img->height;
            int src_stride = img->pitch > 0 ? img->pitch / 4 : src_w;
            if (!img->pixels || src_w <= 0 || src_h <= 0 || src_stride <= 0) break;
            pdf_draw_abgr_image(ctx, (const uint32_t*)img->pixels,
                                src_w, src_h, src_stride,
                                p->dst_x, p->dst_y, p->dst_w, p->dst_h,
                                p->opacity,
                                effective_transform);
            state->emitted_count++;
            break;
        }
        case PAINT_GLYPH_RUN: {
            if (!caps || !caps->glyph_runs) break;
            PaintGlyphRun* p = &cmd->glyph_run;
            pdf_render_glyph_run(ctx, p, stack_transform);
            state->emitted_count++;
            break;
        }
        case PAINT_SVG_SUBSCENE: {
            PaintSvgSubscene* p = &cmd->svg_subscene;
            pdf_paint_record_fallback(state,
                                      pdf_raster_fallback_svg_subscene(ctx, p, stack_transform),
                                      "[PDF_PAINT_IR] failed SVG subscene raster fallback");
            break;
        }
        default:
            break;
        }
    }
    paint_list_clear(&ctx->paint_list);
}

static void pdf_paint_fill_rect(PdfRenderContext* ctx,
                                float x, float y, float width, float height,
                                Color color) {
    paint_fill_rect(pdf_active_paint_list(ctx), x, y, width, height, color);
    pdf_lower_paint_list(ctx);
}

static PaintCmd* pdf_effect_fallback_latest_cmd(PdfRenderContext* ctx,
                                                PaintList* list,
                                                int index,
                                                PaintOp expected_op) {
    if (!ctx || !ctx->effect_fallback.active || !list || index < 0) return nullptr;
    if (list->count != index + 1) return nullptr;
    PaintCmd* cmd = &list->cmds[index];
    return cmd->op == expected_op ? cmd : nullptr;
}

static bool pdf_paint_fill_path(PdfRenderContext* ctx, RdtPath* path, Color color) {
    PaintList* list = pdf_active_paint_list(ctx);
    int index = list ? list->count : -1;
    paint_fill_path(list, path, color, RDT_FILL_WINDING, nullptr);
    bool owns_path = false;
    PaintCmd* cmd = pdf_effect_fallback_latest_cmd(ctx, list, index, PAINT_FILL_PATH);
    if (cmd) {
        cmd->fill_path.owns_path = true;
        owns_path = true;
    }
    pdf_lower_paint_list(ctx);
    return owns_path;
}

static void pdf_paint_draw_image(PdfRenderContext* ctx, ImageSurface* img,
                                 Rect* dst_rect) {
    if (!ctx || !img || !dst_rect) return;
    paint_draw_image_resource(pdf_active_paint_list(ctx), img,
                              dst_rect->x, dst_rect->y,
                              dst_rect->width, dst_rect->height,
                              255, nullptr);
    pdf_lower_paint_list(ctx);
}

static bool pdf_paint_stroke_path(PdfRenderContext* ctx, RdtPath* path,
                                  Color color, float width) {
    PaintList* list = pdf_active_paint_list(ctx);
    int index = list ? list->count : -1;
    paint_stroke_path(list, path, color, width,
                      RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0.0f, nullptr);
    bool owns_path = false;
    PaintCmd* cmd = pdf_effect_fallback_latest_cmd(ctx, list, index, PAINT_STROKE_PATH);
    if (cmd) {
        cmd->stroke_path.owns_path = true;
        owns_path = true;
    }
    pdf_lower_paint_list(ctx);
    return owns_path;
}

static bool pdf_paint_fill_linear_gradient(PdfRenderContext* ctx,
                                           BoundaryLinearGradientPaint* gradient,
                                           RdtGradientStop* stops) {
    if (!gradient) return false;
    PaintList* list = pdf_active_paint_list(ctx);
    int index = list ? list->count : -1;
    paint_fill_linear_gradient(list, gradient->path,
                               gradient->x1, gradient->y1,
                               gradient->x2, gradient->y2,
                               gradient->stops, gradient->stop_count,
                               RDT_FILL_WINDING, nullptr, nullptr);
    bool owns_payload = false;
    PaintCmd* cmd = pdf_effect_fallback_latest_cmd(ctx, list, index,
                                                   PAINT_FILL_LINEAR_GRADIENT);
    if (cmd) {
        cmd->fill_linear_gradient.owns_path = gradient->path != nullptr;
        cmd->fill_linear_gradient.owns_stops = stops != nullptr;
        owns_payload = true;
    }
    pdf_lower_paint_list(ctx);
    return owns_payload;
}

static bool pdf_paint_fill_radial_gradient(PdfRenderContext* ctx,
                                           BoundaryRadialGradientPaint* gradient,
                                           RdtGradientStop* stops) {
    if (!gradient) return false;
    PaintList* list = pdf_active_paint_list(ctx);
    int index = list ? list->count : -1;
    paint_fill_radial_gradient(list, gradient->path,
                               gradient->cx, gradient->cy, gradient->r,
                               gradient->stops, gradient->stop_count,
                               RDT_FILL_WINDING, nullptr, nullptr);
    bool owns_payload = false;
    PaintCmd* cmd = pdf_effect_fallback_latest_cmd(ctx, list, index,
                                                   PAINT_FILL_RADIAL_GRADIENT);
    if (cmd) {
        cmd->fill_radial_gradient.owns_path = gradient->path != nullptr;
        cmd->fill_radial_gradient.owns_stops = stops != nullptr;
        owns_payload = true;
    }
    pdf_lower_paint_list(ctx);
    return owns_payload;
}

static Corner pdf_corner_inset(const Corner* radius, float inset_x, float inset_y) {
    Corner out = *radius;
    out.top_left = fmaxf(0.0f, out.top_left - inset_x);
    out.top_left_y = fmaxf(0.0f, out.top_left_y - inset_y);
    out.top_right = fmaxf(0.0f, out.top_right - inset_x);
    out.top_right_y = fmaxf(0.0f, out.top_right_y - inset_y);
    out.bottom_right = fmaxf(0.0f, out.bottom_right - inset_x);
    out.bottom_right_y = fmaxf(0.0f, out.bottom_right_y - inset_y);
    out.bottom_left = fmaxf(0.0f, out.bottom_left - inset_x);
    out.bottom_left_y = fmaxf(0.0f, out.bottom_left_y - inset_y);
    return out;
}

static bool pdf_has_border_radius(const BorderProp* border) {
    if (!border) return false;
    const Corner* radius = &border->radius;
    return radius->top_left > 0.0f || radius->top_right > 0.0f ||
           radius->bottom_right > 0.0f || radius->bottom_left > 0.0f ||
           radius->top_left_y > 0.0f || radius->top_right_y > 0.0f ||
           radius->bottom_right_y > 0.0f || radius->bottom_left_y > 0.0f;
}

static bool pdf_border_is_uniform_solid(const BorderProp* border) {
    if (!border || border->width.top <= 0.0f || border->top_color.a == 0) {
        return false;
    }
    return border->width.top == border->width.right &&
           border->width.right == border->width.bottom &&
           border->width.bottom == border->width.left &&
           border->top_style == CSS_VALUE_SOLID &&
           border->right_style == CSS_VALUE_SOLID &&
           border->bottom_style == CSS_VALUE_SOLID &&
           border->left_style == CSS_VALUE_SOLID &&
           border->top_color.c == border->right_color.c &&
           border->right_color.c == border->bottom_color.c &&
           border->bottom_color.c == border->left_color.c;
}

// Render text view
static void render_text_view_pdf(PdfRenderContext* ctx, ViewText* text) {
    if (!text || !text->text_data()) return;
    CssEnum text_transform = render_text_inherited_transform(text);

    // Calculate absolute position using block context (like SVG renderer)
    // Extract the text content
    unsigned char* str = text->text_data();
    TextRect *text_rect = text->rect;
    NEXT_RECT:
    float base_x = (float)ctx->block.x + text_rect->x, y = (float)ctx->block.y + text_rect->y;

    char* text_content = render_text_create_export_segment(
        str, text_rect, text_transform, false);

    if (strlen(text_content) == 0) {
        mem_free(text_content);
        return;
    }

    // Set font if available
    float font_size = 16.0f;
    if (ctx->current_font) {
        font_size = ctx->font.style->font_size ? ctx->font.style->font_size : 16.0f;
        HPDF_Page_SetFontAndSize(ctx->current_page, ctx->current_font, font_size);
    }

    // Check if text is justified by comparing TextRect width with natural width
    // Use shared font backend to measure text width (similar to canvas renderer)
    float space_width = ctx->font.style ? ctx->font.style->space_width : 4.0f;
    float adjusted_space_width = space_width;

    // Calculate natural width using glyph metrics (excluding trailing spaces)
    float natural_width = 0.0f;
    int space_count = 0;
    size_t content_len = strlen(text_content);
    // Find end of non-whitespace content
    while (content_len > 0 && text_content[content_len - 1] == ' ') {
        content_len--;
    }

    if (ctx->font.font_handle) {
        for (size_t i = 0; i < content_len; i++) {  // Only count up to content_len
            if (text_content[i] == ' ') {
                natural_width += space_width;
                space_count++;
            } else {
                natural_width += font_measure_char(ctx->font.font_handle, (uint32_t)text_content[i]);
            }
        }
    }

    // If text_rect width is larger than natural width and there are spaces, apply justify
    if (space_count > 0 && natural_width > 0 && text_rect->width > natural_width + 0.5f) {
        float extra_space = text_rect->width - natural_width;
        adjusted_space_width = space_width + (extra_space / space_count);
    }

    float baseline_offset = font_size * 0.8f;
    float baseline_y = y + baseline_offset;
    PaintGlyphRun run = {};
    run.font = &ctx->font;
    run.color = ctx->color;
    run.text = text_content;
    run.text_len = (int)strlen(text_content); // INT_CAST_OK: text run byte length is bounded by TextRect input.
    run.owns_text = ctx && ctx->effect_fallback.active;
    run.font_family = ctx->font.style ? ctx->font.style->family : nullptr;
    run.font_size = font_size;
    run.x = base_x;
    run.baseline_y = baseline_y;
    run.word_spacing = adjusted_space_width - space_width;
    paint_glyph_run(pdf_active_paint_list(ctx), &run);
    pdf_lower_paint_list(ctx);

    if (!run.owns_text) mem_free(text_content);
    text_rect = text_rect->next;
    if (text_rect) { goto NEXT_RECT; }
}

// ============================================================================
// PDF RenderBackend vtable callbacks
// ============================================================================

static void pdf_cb_render_bound(void* vctx, ViewBlock* view, float abs_x, float abs_y) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (render_paint_boundary_emit_simple(pdf_active_paint_list(ctx), view, abs_x, abs_y)) {
        pdf_lower_paint_list(ctx);
        return;
    }

    float width = view->width;
    float height = view->height;

    if (ctx->effect_fallback.active && view->boundary_mut()->box_shadow) {
        render_paint_boundary_emit_outer_shadows(pdf_active_paint_list(ctx), view, abs_x, abs_y);
    }

    // Background
    if (view->boundary_mut()->background && view->boundary_mut()->background->color.a > 0) {
        if (pdf_has_border_radius(view->boundary()->border)) {
            Corner background_radius = view->boundary()->border->radius;
            constrain_corner_radii(&background_radius, width, height);
            Rect background_rect = {abs_x, abs_y, width, height};
            RdtPath* background_path =
                render_path_create_rounded_rect(background_rect, &background_radius);
            if (background_path) {
                bool owns_path =
                    pdf_paint_fill_path(ctx, background_path, view->boundary()->background->color);
                if (!owns_path) rdt_path_free(background_path);
            } else {
                pdf_paint_fill_rect(ctx, abs_x, abs_y, width, height, view->boundary()->background->color);
            }
        } else {
            pdf_paint_fill_rect(ctx, abs_x, abs_y, width, height, view->boundary()->background->color);
        }
    }

    if (view->boundary()->background &&
        view->boundary()->background->gradient_type == GRADIENT_LINEAR &&
        view->boundary()->background->linear_gradient &&
        view->boundary()->background->linear_gradient->stop_count >= 2) {
        int stop_count = view->boundary()->background->linear_gradient->stop_count;
        RdtGradientStop* stops = (RdtGradientStop*)mem_alloc(
            (size_t)stop_count * sizeof(RdtGradientStop), MEM_CAT_RENDER);
        BoundaryLinearGradientPaint gradient = {};
        bool owns_payload = false;
        if (stops &&
            render_paint_boundary_build_linear_gradient(view, abs_x, abs_y,
                                                        stops, stop_count,
                                                        &gradient)) {
            owns_payload = pdf_paint_fill_linear_gradient(ctx, &gradient, stops);
            if (!owns_payload) {
                rdt_path_free(gradient.path);
            }
        } else {
            if (stops) mem_free(stops);
            stops = nullptr;
        }
        if (stops && !owns_payload) mem_free(stops);
    } else if (view->boundary()->background &&
               view->boundary()->background->gradient_type == GRADIENT_RADIAL &&
               view->boundary()->background->radial_gradient &&
               view->boundary()->background->radial_gradient->stop_count >= 2) {
        int stop_count = view->boundary()->background->radial_gradient->stop_count;
        RdtGradientStop* stops = (RdtGradientStop*)mem_alloc(
            (size_t)stop_count * sizeof(RdtGradientStop), MEM_CAT_RENDER);
        BoundaryRadialGradientPaint gradient = {};
        bool owns_payload = false;
        if (stops &&
            render_paint_boundary_build_radial_gradient(view, abs_x, abs_y,
                                                        stops, stop_count,
                                                        &gradient)) {
            owns_payload = pdf_paint_fill_radial_gradient(ctx, &gradient, stops);
            if (!owns_payload) {
                rdt_path_free(gradient.path);
            }
        } else {
            if (stops) mem_free(stops);
            stops = nullptr;
        }
        if (stops && !owns_payload) mem_free(stops);
    }

    // Borders
    if (view->boundary()->border) {
        BorderProp* border = view->boundary()->border;
        if (pdf_has_border_radius(border) && pdf_border_is_uniform_solid(border) &&
            width > border->width.top && height > border->width.top) {
            float border_width = border->width.top;
            float half_width = border_width * 0.5f;
            Corner border_radius = border->radius;
            constrain_corner_radii(&border_radius, width, height);
            Corner stroke_radius = pdf_corner_inset(&border_radius, half_width, half_width);
            Rect stroke_rect = {abs_x + half_width, abs_y + half_width,
                                width - border_width, height - border_width};
            RdtPath* stroke_path = render_path_create_rounded_rect(stroke_rect, &stroke_radius);
            if (stroke_path) {
                bool owns_path =
                    pdf_paint_stroke_path(ctx, stroke_path, border->top_color, border_width);
                if (!owns_path) rdt_path_free(stroke_path);
                return;
            }
        }
        if (border->width.top > 0 && border->top_color.a > 0)
            pdf_paint_fill_rect(ctx, abs_x, abs_y, width, (float)border->width.top, border->top_color);
        if (border->width.right > 0 && border->right_color.a > 0)
            pdf_paint_fill_rect(ctx, abs_x + width - border->width.right, abs_y, (float)border->width.right, height, border->right_color);
        if (border->width.bottom > 0 && border->bottom_color.a > 0)
            pdf_paint_fill_rect(ctx, abs_x, abs_y + height - border->width.bottom, width, (float)border->width.bottom, border->bottom_color);
        if (border->width.left > 0 && border->left_color.a > 0)
            pdf_paint_fill_rect(ctx, abs_x, abs_y, (float)border->width.left, height, border->left_color);
    }
}

static void pdf_cb_render_text(void* vctx, ViewText* text, float abs_x, float abs_y,
                               FontBox* font, Color color) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    ctx->block.x = abs_x;
    ctx->block.y = abs_y;
    ctx->font = *font;
    ctx->color = color;
    render_text_view_pdf(ctx, text);
}

static void pdf_cb_render_image(void* vctx, ViewBlock* block, float abs_x, float abs_y) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx || !block || !block->embed || !block->embedp()->img) return;
    ImageSurface* img = block->embedp()->img;

    BlockBlot image_block = {};
    image_block.x = abs_x - block->x;
    image_block.y = abs_y - block->y;
    Rect content_rect = render_geometry_block_content_rect(&image_block, block, 1.0f);
    pdf_paint_draw_image(ctx, img, &content_rect);
}

static void pdf_cb_render_inline_svg(void* vctx, ViewBlock* block, float abs_x, float abs_y,
                                     FontBox* font, Color color) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx || !block) return;

    DomElement* dom_elem = lam::dom_require_element(lam::view_dom_node(block));
    if (!dom_elem || dom_elem->is_synthetic()) {
        log_debug("[PDF_SVG_SUBSCENE] inline SVG missing native element");
        return;
    }

    BlockBlot block_context = {};
    block_context.x = abs_x - block->x;
    block_context.y = abs_y - block->y;
    Rect content_rect = render_geometry_block_content_rect(&block_context, block, 1.0f);
    if (content_rect.width <= 0.0f || content_rect.height <= 0.0f) {
        log_debug("[PDF_SVG_SUBSCENE] skipped empty inline SVG %.1fx%.1f",
                  content_rect.width, content_rect.height);
        return;
    }

    FontContext* font_ctx = ctx->ui_context ? ctx->ui_context->font_ctx : nullptr;
    Pool* pool = (ctx->ui_context && ctx->ui_context->document)
        ? ctx->ui_context->document->document_pool
        : nullptr;
    SvgInitialPaint initial_paint;
    render_svg_initial_paint(block, color, &initial_paint);

    Bound content_clip = {content_rect.x, content_rect.y,
                          content_rect.x + content_rect.width,
                          content_rect.y + content_rect.height};
    PaintSvgSubscene subscene = {};
    RdtMatrix identity = rdt_matrix_identity();
    render_svg_build_subscene(&subscene,
                              dom_element_to_element(dom_elem),
                              content_rect.width,
                              content_rect.height,
                              pool,
                              1.0f,
                              font_ctx,
                              &identity,
                              &content_clip,
                              &initial_paint.current_color,
                              initial_paint.has_fill_color ? &initial_paint.fill_color : nullptr,
                              nullptr,
                              1.0f,
                              initial_paint.fill_none,
                              initial_paint.has_stroke_color ? &initial_paint.stroke_color : nullptr,
                              initial_paint.stroke_none,
                              initial_paint.stroke_width);
    paint_svg_subscene(pdf_active_paint_list(ctx), &subscene);
    pdf_lower_paint_list(ctx);
    (void)font;
}

static void pdf_cb_render_svg_subscene(void* vctx, const PaintSvgSubscene* subscene) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx || !subscene) return;
    paint_svg_subscene(pdf_active_paint_list(ctx), subscene);
    pdf_lower_paint_list(ctx);
}

static void pdf_cb_render_column_rules(void* vctx, ViewBlock* block, float abs_x, float abs_y) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx || !block || !block->multicol_prop()) return;

    MultiColumnProp* mc = block->multicol_prop();
    int rule_column_count = mc->computed_used_column_count > 0
        ? mc->computed_used_column_count
        : mc->computed_column_count;
    if (rule_column_count <= 1 || mc->rule_width <= 0.0f ||
        mc->rule_style == CSS_VALUE_NONE || mc->rule_color.a == 0) {
        return;
    }

    if (mc->rule_style == CSS_VALUE_DOTTED ||
        mc->rule_style == CSS_VALUE_DASHED) {
        log_debug("[PDF_PAINT_IR] column-rule style requires dashed strokes; skipping style=%d",
                  mc->rule_style);
        return;
    }

    float column_width = mc->computed_column_width;
    float gap = mc->column_gap_is_normal ? 16.0f : mc->column_gap;
    float block_x = abs_x;
    float block_y = abs_y;
    if (block->bound) {
        block_x += block->boundary()->padding.left;
        block_y += block->boundary()->padding.top;
    }

    float rule_height = block->height;
    if (block->bound) {
        rule_height -= layout_box_metrics(block).pad_border_v;
    }

    if (rule_height <= 0.0f) {
        View* child = static_cast<View*>(block->first_child);
        float max_bottom = 0.0f;
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
    if (rule_height <= 0.0f) return;

    for (int i = 0; i < rule_column_count - 1; i++) {
        float rule_x = block_x + (i + 1) * column_width + i * gap +
                       gap / 2.0f - mc->rule_width / 2.0f;

        if (mc->rule_style == CSS_VALUE_DOUBLE) {
            float thin_width = mc->rule_width / 3.0f;
            RdtPath* left = rdt_path_new();
            RdtPath* right = rdt_path_new();
            if (left && right) {
                rdt_path_move_to(left, rule_x - thin_width, block_y);
                rdt_path_line_to(left, rule_x - thin_width, block_y + rule_height);
                bool left_owned = pdf_paint_stroke_path(ctx, left, mc->rule_color, thin_width);
                if (left_owned) left = nullptr;

                rdt_path_move_to(right, rule_x + thin_width, block_y);
                rdt_path_line_to(right, rule_x + thin_width, block_y + rule_height);
                bool right_owned = pdf_paint_stroke_path(ctx, right, mc->rule_color, thin_width);
                if (right_owned) right = nullptr;
            }
            if (left) rdt_path_free(left);
            if (right) rdt_path_free(right);
        } else {
            RdtPath* path = rdt_path_new();
            if (path) {
                rdt_path_move_to(path, rule_x, block_y);
                rdt_path_line_to(path, rule_x, block_y + rule_height);
                bool owns_path = pdf_paint_stroke_path(ctx, path, mc->rule_color, mc->rule_width);
                if (!owns_path) rdt_path_free(path);
            }
        }
    }
}

static void pdf_cb_begin_transform(void* vctx, ViewBlock* block, float abs_x, float abs_y) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx || !block) return;
    if (ctx->transform_emitted_depth >= PDF_PAINT_TRANSFORM_STACK_MAX) {
        log_error("[PDF_PAINT_IR] transform callback stack overflow while rendering %s",
                  block->node_name());
        ctx->transform_emitted_overflow_depth++;
        return;
    }

    RdtMatrix transform = {};
    bool has = render_geometry_transform_matrix(block->transform, abs_x, abs_y,
                                                block->width, block->height,
                                                &transform);
    ctx->transform_emitted_stack[ctx->transform_emitted_depth++] = has;
    if (has) {
        paint_push_transform(pdf_active_paint_list(ctx), &transform);
        pdf_lower_paint_list(ctx);
    }
}

static void pdf_cb_end_transform(void* vctx) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx) return;
    if (ctx->transform_emitted_overflow_depth > 0) {
        ctx->transform_emitted_overflow_depth--;
        return;
    }
    if (ctx->transform_emitted_depth <= 0) {
        log_error("[PDF_PAINT_IR] transform callback stack underflow");
        return;
    }

    bool emitted = ctx->transform_emitted_stack[--ctx->transform_emitted_depth];
    if (emitted) {
        paint_pop_transform(pdf_active_paint_list(ctx));
        pdf_lower_paint_list(ctx);
    }
}

static void pdf_cb_begin_effect_group(void* vctx, const PaintEffectGroup* group) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx) return;
    if (ctx->effect_fallback.active) {
        paint_begin_effect_group(&ctx->effect_fallback.paint_list, group);
        ctx->effect_fallback.nested_depth++;
        return;
    }
    if (pdf_effect_group_needs_raster_fallback(group)) {
        pdf_begin_effect_raster_fallback(ctx, group);
        return;
    }
    paint_begin_effect_group(&ctx->paint_list, group);
    pdf_lower_paint_list(ctx);
}

static void pdf_cb_end_effect_group(void* vctx) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx) return;
    if (ctx->effect_fallback.active) {
        if (ctx->effect_fallback.nested_depth > 0) {
            paint_end_effect_group(&ctx->effect_fallback.paint_list);
            ctx->effect_fallback.nested_depth--;
        } else {
            pdf_finish_effect_raster_fallback(ctx);
        }
        return;
    }
    paint_end_effect_group(&ctx->paint_list);
    pdf_lower_paint_list(ctx);
}

static void pdf_cb_on_font_change(void* vctx, FontProp* font_prop) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    const char* pdf_font_name = get_pdf_font_name(font_prop->family);
    HPDF_Font font = HPDF_GetFont(ctx->pdf_doc, pdf_font_name, NULL);
    if (font) {
        ctx->current_font = font;
        HPDF_Page_SetFontAndSize(ctx->current_page, font, font_prop->font_size);
    }
}

static RenderBackend pdf_make_backend(PdfRenderContext* ctx) {
    RenderBackend b = {};
    b.ctx              = ctx;
    b.render_bound     = pdf_cb_render_bound;
    b.render_text      = pdf_cb_render_text;
    b.render_image     = pdf_cb_render_image;
    b.render_inline_svg = pdf_cb_render_inline_svg;
    b.render_svg_subscene = pdf_cb_render_svg_subscene;
    b.begin_block_children  = NULL;
    b.end_block_children    = NULL;
    b.begin_inline_children = NULL;
    b.end_inline_children   = NULL;
    b.begin_effect_group = pdf_cb_begin_effect_group;
    b.end_effect_group = pdf_cb_end_effect_group;
    b.begin_transform  = pdf_cb_begin_transform;
    b.end_transform    = pdf_cb_end_transform;
    b.render_column_rules = pdf_cb_render_column_rules;
    b.on_font_change   = pdf_cb_on_font_change;
    return b;
}

// Main PDF rendering function
static HPDF_Doc render_view_tree_to_pdf(UiContext* uicon, View* root_view, float width, float height) {
    if (!root_view || !uicon) {
        return NULL;
    }

    PdfRenderContext ctx;
    memset(&ctx, 0, sizeof(PdfRenderContext));
    pdf_paint_lowering_state_init(&ctx.paint_state);

    // Create PDF document
    ctx.pdf_doc = HPDF_New(pdf_error_handler, NULL);
    if (!ctx.pdf_doc) {
        log_error("Failed to create PDF document");
        return NULL;
    }

    // Set PDF compression
    HPDF_SetCompressionMode(ctx.pdf_doc, HPDF_COMP_ALL);

    // Set document info
    HPDF_SetInfoAttr(ctx.pdf_doc, HPDF_INFO_CREATOR, "Lambda Script Renderer");
    HPDF_SetInfoAttr(ctx.pdf_doc, HPDF_INFO_PRODUCER, "Lambda PDF Renderer");

    // Add a page
    ctx.current_page = HPDF_AddPage(ctx.pdf_doc);
    if (!ctx.current_page) {
        log_error("Failed to add PDF page");
        HPDF_Free(ctx.pdf_doc);
        return NULL;
    }

    // Set page size to match content dimensions
    ctx.page_width = width;
    ctx.page_height = height;
    HPDF_Page_SetWidth(ctx.current_page, width);
    HPDF_Page_SetHeight(ctx.current_page, height);

    // Initialize context
    ctx.ui_context = uicon;
    ctx.color.r = 0; ctx.color.g = 0; ctx.color.b = 0; ctx.color.a = 255; // Black text
    ctx.current_x = 0;
    ctx.current_y = 0;
    paint_list_init(&ctx.paint_list, nullptr);
    paint_list_init(&ctx.effect_fallback.paint_list, nullptr);
    ctx.page_backdrop_pool = mem_pool_create(NULL, MEM_ROLE_RENDER, "render.pdf.backdrop");
    if (ctx.page_backdrop_pool) {
        ctx.page_backdrop_arena = mem_arena_create(NULL, ctx.page_backdrop_pool, MEM_ROLE_RENDER, "render.pdf.backdrop.arena");
        if (ctx.page_backdrop_arena) {
            dl_init(&ctx.page_backdrop_dl, ctx.page_backdrop_arena);
            ctx.page_backdrop_ready = true;
            Color white = {};
            white.r = 255;
            white.g = 255;
            white.b = 255;
            white.a = 255;
            dl_fill_rect(&ctx.page_backdrop_dl, 0.0f, 0.0f, width, height, white);
        } else {
            log_error("[PDF_PAINT_IR] page backdrop arena allocation failed");
        }
    } else {
        log_error("[PDF_PAINT_IR] page backdrop pool allocation failed");
    }

    // Initialize block context (starting at origin)
    ctx.block.x = 0;
    ctx.block.y = 0;

    // Initialize font from default
    ctx.font.style = &uicon->default_font;

    // Set default font
    ctx.current_font = HPDF_GetFont(ctx.pdf_doc, "Helvetica", NULL);
    if (ctx.current_font) {
        HPDF_Page_SetFontAndSize(ctx.current_page, ctx.current_font, 16.0f);
    }

    // Render the root view via shared tree walker
    RenderBackend backend = pdf_make_backend(&ctx);
    RenderWalkState walk_state = {};
    walk_state.x = 0;
    walk_state.y = 0;
    walk_state.font = ctx.font;
    walk_state.color = ctx.color;
    walk_state.ui_context = uicon;

    if (root_view->view_type == RDT_VIEW_BLOCK) {
        render_walk_block(&backend, &walk_state, lam::view_require_block(root_view));
    } else if (root_view->view_type >= RDT_VIEW_INLINE) {
        render_walk_children(&backend, &walk_state, root_view);
    }

    RenderPathTrace trace = {};
    trace.target = "pdf";
    trace.replay_mode = "paint_ir_pdf";
    trace.backend_name = "pdf_export";
    trace.display_list_recorded = false;
    trace.paint_ir_enabled = true;
    trace.surface_width = (int)width; // INT_CAST_OK: PDF trace width is logged as whole document units.
    trace.surface_height = (int)height; // INT_CAST_OK: PDF trace height is logged as whole document units.
    trace.paint_ir_commands = ctx.paint_state.command_count;
    trace.paint_ir_emitted = ctx.paint_state.emitted_count;
    trace.paint_ir_fallbacks = ctx.paint_state.fallback_count;
    trace.paint_ir_unsupported = ctx.paint_state.unsupported_count;
    const RenderExportTargetCaps* caps =
        render_export_target_get_caps(RENDER_EXPORT_TARGET_PDF);
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
    render_profiler_emit_path_trace(nullptr, uicon, nullptr, &trace);

    paint_list_destroy(&ctx.paint_list);
    paint_list_clear(&ctx.effect_fallback.paint_list);
    paint_list_destroy(&ctx.effect_fallback.paint_list);
    if (ctx.page_backdrop_ready) {
        dl_destroy(&ctx.page_backdrop_dl);
    }
    if (ctx.page_backdrop_pool) {
        // Page backdrop display-list storage is arena-in-pool; pool teardown owns the arena chunks.
        mem_pool_destroy(ctx.page_backdrop_pool);
    }
    return ctx.pdf_doc;
}

// Save PDF to file
static bool save_pdf_to_file(HPDF_Doc pdf_doc, const char* filename) {
    if (!pdf_doc || !filename) {
        return false;
    }

    HPDF_STATUS status = HPDF_SaveToFile(pdf_doc, filename);
    if (status != HPDF_OK) {
        log_error("Failed to save PDF to file: %s", filename);
        return false;
    }

    return true;
}

// Main function to layout HTML and render to PDF
int render_html_to_pdf(const char* html_file, const char* pdf_file, int viewport_width, int viewport_height, float scale) {
    log_debug("render_html_to_pdf called with html_file='%s', pdf_file='%s', viewport=%dx%d, scale=%.2f",
              html_file, pdf_file, viewport_width, viewport_height, scale);

    RenderExportSession session;
    if (!render_export_session_begin(
            &session, html_file, viewport_width, viewport_height, 800, 1200, scale)) {
        return 1;
    }
    UiContext* ui_context = session.ui_context;
    DomDocument* doc = session.document;

    // Render to PDF (apply scale to output dimensions)
    if (doc->view_tree && doc->view_tree->root) {
        log_debug("Rendering view tree to PDF...");
        // PDF output dimensions are scaled; coordinates inside are in CSS pixels with transform
        float pdf_width = session.content_width * session.scale;
        float pdf_height = session.content_height * session.scale;
        HPDF_Doc pdf_doc = render_view_tree_to_pdf(ui_context, doc->view_tree->root,
                                                   pdf_width, pdf_height);
        if (pdf_doc) {
            if (save_pdf_to_file(pdf_doc, pdf_file)) {
                log_info("Successfully rendered HTML to PDF: %s", pdf_file);
                HPDF_Free(pdf_doc);
                render_export_session_end(&session);
                return 0;
            } else {
                log_debug("Failed to save PDF to file: %s", pdf_file);
                HPDF_Free(pdf_doc);
            }
        } else {
            log_debug("Failed to render view tree to PDF");
        }
    } else {
        log_debug("No view tree available for rendering");
    }

    // Cleanup
    render_export_session_end(&session);
    return 1;
}

// ============================================================================
// Math Rendering Functions for PDF
// ============================================================================
// NOTE: MathBox rendering has been removed.
// ============================================================================
