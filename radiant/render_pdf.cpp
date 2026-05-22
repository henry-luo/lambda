#include "render.hpp"
#include "render_backend.h"
#include "render_backend_caps.hpp"
#include "render_export_support.hpp"
#include "render_geometry.hpp"
#include "paint_ir.h"
#include "render_border.hpp"
#include "render_path.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "font_face.h"

#include "../lib/tagged.hpp"
#include "../lib/font/font.h"
#include "../lib/utf.h"
#include "../lambda/input/css/dom_element.hpp"
extern "C" {
#include "../lib/url.h"
#include "../lib/pdf_writer.h"
#include "../lib/memtrack.h"
}
#include <stdio.h>
#include <string.h>
#include <math.h>

#define PDF_PATH_KAPPA 0.5522847498f

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
} PdfRenderContext;

// Forward declarations
static void render_text_view_pdf(PdfRenderContext* ctx, ViewText* text);
static RenderBackend pdf_make_backend(PdfRenderContext* ctx);


// Error handler for libharu
static void pdf_error_handler(HPDF_STATUS error_no, HPDF_STATUS detail_no, void* user_data) {
    log_error("PDF Error: error_no=0x%04X, detail_no=0x%04X",
              (unsigned int)error_no, (unsigned int)detail_no);
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

static bool pdf_draw_abgr_image(PdfRenderContext* ctx, const uint32_t* pixels,
                                int src_w, int src_h, int src_stride,
                                float dst_x, float dst_y, float dst_w, float dst_h,
                                uint8_t opacity, const RdtMatrix* transform) {
    if (!ctx || !pixels || src_w <= 0 || src_h <= 0 || dst_w <= 0.0f || dst_h <= 0.0f) {
        return false;
    }
    if (opacity != 255) return false;

    int stride_pixels = pdf_image_stride_pixels(src_w, src_stride);
    if (!pdf_image_pixels_are_opaque(pixels, src_w, src_h, stride_pixels)) {
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

    return HPDF_Page_DrawABGRImage(ctx->current_page, pixels, src_w, src_h,
                                   stride_pixels, a, b, c, d, e, f) == HPDF_OK;
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

static void pdf_lower_paint_list(PdfRenderContext* ctx) {
    const RenderExportTargetCaps* caps =
        render_export_target_get_caps(RENDER_EXPORT_TARGET_PDF);
    int active_clip_depth = 0;
    int skipped_clip_depth = 0;
    for (int i = 0; i < ctx->paint_list.count; i++) {
        PaintCmd* cmd = &ctx->paint_list.cmds[i];
        switch (cmd->op) {
        case PAINT_FILL_RECT: {
            if (!caps || !caps->rects) break;
            PaintFillRect* p = &cmd->fill_rect;
            pdf_render_rect(ctx, p->x, p->y, p->w, p->h, p->color, true);
            break;
        }
        case PAINT_FILL_ROUNDED_RECT: {
            if (!caps || !caps->rounded_rects) break;
            PaintFillRoundedRect* p = &cmd->fill_rounded_rect;
            if (p->color.a == 0) break;
            pdf_set_color(ctx, p->color);
            PdfPathContext path_ctx = {};
            path_ctx.pdf = ctx;
            if (pdf_path_emit_rounded_rect(&path_ctx, p->x, p->y, p->w, p->h,
                                           p->rx, p->ry)) {
                HPDF_Page_Fill(ctx->current_page);
            }
            break;
        }
        case PAINT_FILL_PATH: {
            if (!caps || !caps->paths) break;
            PaintFillPath* p = &cmd->fill_path;
            if (p->color.a == 0 || p->rule != RDT_FILL_WINDING) break;
            if (p->has_transform && !caps->transforms) break;
            pdf_set_color(ctx, p->color);
            if (pdf_render_path(ctx, p->path, p->has_transform ? &p->transform : nullptr)) {
                HPDF_Page_Fill(ctx->current_page);
            }
            break;
        }
        case PAINT_STROKE_PATH: {
            if (!caps || !caps->strokes) break;
            PaintStrokePath* p = &cmd->stroke_path;
            if (p->color.a == 0 || p->dash_count > 0) break;
            if (p->has_transform && !caps->transforms) break;
            pdf_set_color(ctx, p->color);
            HPDF_Page_SetLineWidth(ctx->current_page, p->width);
            if (pdf_render_path(ctx, p->path, p->has_transform ? &p->transform : nullptr)) {
                HPDF_Page_Stroke(ctx->current_page);
            }
            break;
        }
        case PAINT_DRAW_IMAGE: {
            if (!caps || !caps->images) break;
            PaintDrawImage* p = &cmd->draw_image;
            if (p->has_transform && !caps->transforms) break;
            pdf_draw_abgr_image(ctx, p->pixels, p->src_w, p->src_h, p->src_stride,
                                p->dst_x, p->dst_y, p->dst_w, p->dst_h,
                                p->opacity, p->has_transform ? &p->transform : nullptr);
            break;
        }
        case PAINT_PUSH_CLIP: {
            if (skipped_clip_depth > 0) {
                skipped_clip_depth++;
                break;
            }
            if (!caps || !caps->clips) {
                skipped_clip_depth++;
                break;
            }
            PaintPushClip* p = &cmd->push_clip;
            if (p->has_transform && !caps->transforms) {
                skipped_clip_depth++;
                break;
            }
            if (pdf_push_clip_path(ctx, p->clip_path,
                                   p->has_transform ? &p->transform : nullptr)) {
                active_clip_depth++;
            } else {
                skipped_clip_depth++;
            }
            break;
        }
        case PAINT_POP_CLIP: {
            if (skipped_clip_depth > 0) {
                skipped_clip_depth--;
            } else if (active_clip_depth > 0) {
                HPDF_Page_GRestore(ctx->current_page);
                active_clip_depth--;
            }
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
    paint_fill_rect(&ctx->paint_list, x, y, width, height, color);
    pdf_lower_paint_list(ctx);
}

static void pdf_paint_fill_path(PdfRenderContext* ctx, RdtPath* path, Color color) {
    paint_fill_path(&ctx->paint_list, path, color, RDT_FILL_WINDING, nullptr);
    pdf_lower_paint_list(ctx);
}

static void pdf_paint_draw_image(PdfRenderContext* ctx, ImageSurface* img,
                                 Rect* dst_rect) {
    if (!ctx || !img || !dst_rect) return;
    int src_w = img->decoded_width > 0 ? img->decoded_width : img->width;
    int src_h = img->decoded_height > 0 ? img->decoded_height : img->height;
    if (!img->pixels || src_w <= 0 || src_h <= 0) return;
    paint_draw_image(&ctx->paint_list, (const uint32_t*)img->pixels,
                     src_w, src_h, src_w,
                     dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height,
                     255, nullptr, img);
    pdf_lower_paint_list(ctx);
}

static void pdf_paint_stroke_path(PdfRenderContext* ctx, RdtPath* path,
                                  Color color, float width) {
    paint_stroke_path(&ctx->paint_list, path, color, width,
                      RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0.0f, nullptr);
    pdf_lower_paint_list(ctx);
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

    // Get text-transform from parent elements
    CssEnum text_transform = CSS_VALUE_NONE;
    DomNode* parent = text->parent;
    while (parent) {
        if (parent->is_element()) {
            DomElement* elem = lam::dom_require_element(parent);
            CssEnum transform = get_text_transform_from_block(elem->blk);
            if (transform != CSS_VALUE_NONE) {
                text_transform = transform;
                break;
            }
        }
        parent = parent->parent;
    }

    // Calculate absolute position using block context (like SVG renderer)
    // Extract the text content
    unsigned char* str = text->text_data();
    TextRect *text_rect = text->rect;
    NEXT_RECT:
    float base_x = (float)ctx->block.x + text_rect->x, y = (float)ctx->block.y + text_rect->y;

    // Apply text-transform if needed
    char* text_content = (char*)mem_alloc(text_rect->length * 4 + 2, MEM_CAT_RENDER);  // Extra space for UTF-8 + trailing hyphen
    if (text_transform != CSS_VALUE_NONE) {
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

            // Skip soft hyphens (U+00AD) — invisible in rendered output
            if (codepoint == 0x00AD) { src += bytes; continue; }

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

    // Render text word by word with adjusted spacing
    pdf_set_color(ctx, ctx->color);
    float baseline_offset = font_size * 0.8f;
    float baseline_y = y + baseline_offset;
    float pdf_y = ctx->page_height - baseline_y;

    float x = base_x;
    size_t word_start = 0;
    for (size_t i = 0; i <= strlen(text_content); i++) {
        if (i == strlen(text_content) || text_content[i] == ' ') {
            if (i > word_start) {
                // Render word
                char word[256];
                size_t word_len = i - word_start;
                if (word_len < sizeof(word)) {
                    strncpy(word, text_content + word_start, word_len);
                    word[word_len] = '\0';

                    HPDF_Page_BeginText(ctx->current_page);
                    HPDF_Page_TextOut(ctx->current_page, x, pdf_y, word);
                    HPDF_Page_EndText(ctx->current_page);

                    // Calculate word width using font metrics
                    if (ctx->font.font_handle) {
                        for (size_t j = 0; j < word_len; j++) {
                            x += font_measure_char(ctx->font.font_handle, (uint32_t)word[j]);
                        }
                    }
                }
            }

            // Add space (adjusted if justified)
            if (i < strlen(text_content)) {
                x += adjusted_space_width;
            }
            word_start = i + 1;
        }
    }

    mem_free(text_content);
    text_rect = text_rect->next;
    if (text_rect) { goto NEXT_RECT; }
}

// ============================================================================
// PDF RenderBackend vtable callbacks
// ============================================================================

static void pdf_cb_render_bound(void* vctx, ViewBlock* view, float abs_x, float abs_y) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    float width = view->width;
    float height = view->height;

    // Background
    if (view->bound->background && view->bound->background->color.a > 0) {
        if (pdf_has_border_radius(view->bound->border)) {
            Corner background_radius = view->bound->border->radius;
            constrain_corner_radii(&background_radius, width, height);
            Rect background_rect = {abs_x, abs_y, width, height};
            RdtPath* background_path =
                render_path_create_rounded_rect(background_rect, &background_radius);
            if (background_path) {
                pdf_paint_fill_path(ctx, background_path, view->bound->background->color);
                rdt_path_free(background_path);
            } else {
                pdf_paint_fill_rect(ctx, abs_x, abs_y, width, height, view->bound->background->color);
            }
        } else {
            pdf_paint_fill_rect(ctx, abs_x, abs_y, width, height, view->bound->background->color);
        }
    }

    // Borders
    if (view->bound->border) {
        BorderProp* border = view->bound->border;
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
                pdf_paint_stroke_path(ctx, stroke_path, border->top_color, border_width);
                rdt_path_free(stroke_path);
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
    if (!ctx || !block || !block->embed || !block->embed->img) return;
    ImageSurface* img = block->embed->img;

    BlockBlot image_block = {};
    image_block.x = abs_x - block->x;
    image_block.y = abs_y - block->y;
    Rect content_rect = render_geometry_block_content_rect(&image_block, block, 1.0f);
    int target_w = (int)ceilf(content_rect.width); // INT_CAST_OK: image decode target width is integer pixels.
    int target_h = (int)ceilf(content_rect.height); // INT_CAST_OK: image decode target height is integer pixels.
    image_surface_ensure_decoded(img, target_w, target_h);
    pdf_paint_draw_image(ctx, img, &content_rect);
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
    b.render_inline_svg = NULL;
    b.begin_block_children  = NULL;
    b.end_block_children    = NULL;
    b.begin_inline_children = NULL;
    b.end_inline_children   = NULL;
    b.begin_opacity    = NULL;
    b.end_opacity      = NULL;
    b.begin_transform  = NULL;
    b.end_transform    = NULL;
    b.render_column_rules = NULL;
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

    paint_list_destroy(&ctx.paint_list);
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

    // Remember if we need to auto-size (viewport was 0)
    bool auto_width = (viewport_width == 0);
    bool auto_height = (viewport_height == 0);

    // Use reasonable defaults for layout if auto-sizing
    int layout_width = viewport_width > 0 ? viewport_width : 800;
    int layout_height = viewport_height > 0 ? viewport_height : 1200;

    // Initialize UI context in headless mode
    UiContext ui_context;
    if (ui_context_init(&ui_context, true) != 0) {
        log_debug("Failed to initialize UI context for PDF rendering");
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

    // Set scale for rendering (in headless mode, pixel_ratio is always 1.0)
    doc->given_scale = scale;
    doc->scale = scale;

    ui_context.document = doc;

    // Process @font-face rules before layout
    process_document_font_faces(&ui_context, doc);

    // Layout the document
    log_debug("Performing layout...");
    layout_html_doc(&ui_context, doc, false);

    // Calculate actual content dimensions
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

    // Render to PDF (apply scale to output dimensions)
    if (doc->view_tree && doc->view_tree->root) {
        log_debug("Rendering view tree to PDF...");
        // PDF output dimensions are scaled; coordinates inside are in CSS pixels with transform
        float pdf_width = content_max_x * scale;
        float pdf_height = content_max_y * scale;
        HPDF_Doc pdf_doc = render_view_tree_to_pdf(&ui_context, doc->view_tree->root,
                                                   pdf_width, pdf_height);
        if (pdf_doc) {
            if (save_pdf_to_file(pdf_doc, pdf_file)) {
                printf("Successfully rendered HTML to PDF: %s\\n", pdf_file);
                HPDF_Free(pdf_doc);
                url_destroy(cwd);
                ui_context_cleanup(&ui_context);
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
    url_destroy(cwd);
    ui_context_cleanup(&ui_context);
    return 1;
}

// ============================================================================
// Math Rendering Functions for PDF
// ============================================================================
// NOTE: MathBox rendering has been removed.
// ============================================================================
