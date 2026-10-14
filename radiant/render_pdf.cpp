#include "render.hpp"
#include "geomap.hpp"
#include "render_effect_raster_fallback.hpp"
#include "render_glyph_run_raster_lower.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "layout_paged.hpp"
#include "page_document.hpp"
#include "radiant.hpp"

#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include "../lib/font/font.h"
#include "../lib/utf.h"
#include "../lib/str.h"
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

struct PdfPaintRectClip {
    Bound bound;
    bool rectangular;
    lam::Own<PdfPaintRectClip> previous;
};

typedef struct PdfPaintLoweringState {
    int active_transform_depth;
    int skipped_transform_depth;
    int active_effect_depth;
    int passthrough_effect_depth;
    int active_clip_depth;
    int skipped_clip_depth;
    lam::Own<PdfPaintRectClip> rect_clip;
    int untracked_rect_clip_depth;
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

// font, color, block (the current block context for coordinate transformation)
// and ui_context come from the shared RenderContext
typedef struct PdfRenderContext : RenderContext {
    HPDF_Doc pdf_doc;
    HPDF_Page current_page;
    HPDF_Font current_font;

    float page_width;
    float page_height;
    float page_scale;
    float current_x;
    float current_y;

    PaintList paint_list;
    RenderEffectRasterFallback effect_fallback;
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

static const char* pdf_nearest_link(DomNode* node) {
    for (DomNode* current = node; current; current = current->parent) {
        DomElement* element = current->as_element();
        if (element && element->tag_id == MARKUP_NAME_A) {
            const char* href = element->get_attribute("href");
            if (href && *href) return href;
        }
    }
    return nullptr;
}

static void pdf_record_link_rect(PdfRenderContext* ctx, const char* href,
                                 float x, float y, float width, float height) {
    if (!ctx || !ctx->current_page || !href || !*href ||
        !isfinite(x) || !isfinite(y) || !isfinite(width) || !isfinite(height) ||
        width <= 0.0f || height <= 0.0f) return;
    float scale = ctx->page_scale;
    HPDF_STATUS status = HPDF_Page_AddLink(ctx->current_page,
        x * scale, (ctx->page_height - y - height) * scale,
        (x + width) * scale, (ctx->page_height - y) * scale, href);
    if (status != HPDF_OK) log_error("[PDF_LINK] Could not add annotation: status=%lu", status);
}

static void pdf_record_destination(PdfRenderContext* ctx, const char* id,
                                   float x, float y) {
    if (!ctx || !ctx->current_page || !id || !*id) return;
    HPDF_STATUS status = HPDF_Doc_AddNamedDestination(ctx->pdf_doc, id,
        ctx->current_page, x * ctx->page_scale,
        (ctx->page_height - y) * ctx->page_scale);
    if (status != HPDF_OK) log_error("[PDF_LINK] Could not add destination: status=%lu", status);
}

static DomElement* pdf_document_head(DomDocument* doc) {
    if (!doc || !doc->root) return nullptr;
    DomElement* root = doc->root;
    if (root->tag_id == MARKUP_NAME_HEAD) return root;
    for (DomNode* child = root->first_child; child; child = child->next_sibling) {
        DomElement* element = child->as_element();
        if (element && element->tag_id == MARKUP_NAME_HEAD) return element;
    }
    return nullptr;
}

static const char* pdf_head_meta(DomElement* head, const char* name) {
    if (!head) return nullptr;
    for (DomNode* child = head->first_child; child; child = child->next_sibling) {
        DomElement* element = child->as_element();
        if (!element || element->tag_id != MARKUP_NAME_META) continue;
        const char* meta_name = element->get_attribute("name");
        if (meta_name && str_icmp_cstr(meta_name, name) == 0)
            return element->get_attribute("content");
    }
    return nullptr;
}

static void pdf_apply_document_info(PdfRenderContext* ctx, DomDocument* doc) {
    if (!ctx || !doc) return;
    const char* title = session_extract_title(doc);
    DomElement* head = pdf_document_head(doc);
    const char* author = pdf_head_meta(head, "author");
    const char* subject = pdf_head_meta(head, "description");
    const char* keywords = pdf_head_meta(head, "keywords");
    if (title && *title) HPDF_SetInfoAttr(ctx->pdf_doc, HPDF_INFO_TITLE, title);
    if (author && *author) HPDF_SetInfoAttr(ctx->pdf_doc, HPDF_INFO_AUTHOR, author);
    if (subject && *subject) HPDF_SetInfoAttr(ctx->pdf_doc, HPDF_INFO_SUBJECT, subject);
    if (keywords && *keywords) HPDF_SetInfoAttr(ctx->pdf_doc, HPDF_INFO_KEYWORDS, keywords);
}

static bool pdf_collect_outlines(PdfRenderContext* ctx, DomElement* element) {
    if (!element) return true;
    const char* title = element->get_attribute("data-pdf-outline-title");
    if (title && *title) {
        const char* level_source = element->get_attribute("data-pdf-outline-level");
        char* ending = nullptr;
        long level = level_source ? strtol(level_source, &ending, 10) : 0;
        if (!element->id || level < 0 || level > 32 ||
            (level_source && (!ending || *ending || ending == level_source)) ||
            HPDF_Doc_AddOutline(ctx->pdf_doc, title, element->id,
                static_cast<unsigned int>(level)) != HPDF_OK) {
            log_error("[PDF_OUTLINE] invalid outline attributes");
            return false;
        }
    }
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        DomElement* next = child->as_element();
        if (next && !pdf_collect_outlines(ctx, next)) return false;
    }
    return true;
}

static void pdf_pop_rect_clip(PdfPaintLoweringState* state) {
    if (state->untracked_rect_clip_depth > 0) { state->untracked_rect_clip_depth--; return; }
    PdfPaintRectClip* clip = state->rect_clip;
    if (!clip) return;
    state->rect_clip = clip->previous;
    mem_free(clip);
}

static void pdf_release_rect_clips(PdfPaintLoweringState* state) {
    state->untracked_rect_clip_depth = 0;
    while (state->rect_clip) pdf_pop_rect_clip(state);
}

static void pdf_push_rect_clip(PdfPaintLoweringState* state, const RdtPath* path, const RdtMatrix* transform) {
    if (state->untracked_rect_clip_depth > 0) { state->untracked_rect_clip_depth++; return; }
    PdfPaintRectClip* clip = (PdfPaintRectClip*)mem_calloc(1, sizeof(PdfPaintRectClip), MEM_CAT_RENDER); // OBJ_HEAP_OK: the PDF clip stack releases each entry on pop or context teardown.
    if (!clip) { state->untracked_rect_clip_depth = 1; return; }
    clip->rectangular = rdt_path_get_rect_bound(path, transform, &clip->bound);
    clip->previous = state->rect_clip;
    state->rect_clip = lam::own(clip);
}

static bool pdf_intersect_rect_clips(const PdfPaintLoweringState* state, const Rect& rect,
        const RdtMatrix* transform, Rect* result) {
    if (!state->rect_clip || state->untracked_rect_clip_depth > 0 ||
            (transform && !rdt_matrix_is_scale_translation(transform))) return false;
    Bound bound = {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
    if (transform && !rdt_matrix_project_rect_bounds(transform, bound.left, bound.top, bound.right, bound.bottom,
            &bound.left, &bound.top, &bound.right, &bound.bottom)) return false;
    for (const PdfPaintRectClip* clip = state->rect_clip; clip; clip = clip->previous) {
        if (!clip->rectangular) return false;
        bound = view_geometry_intersect_bound_rect(bound,
            {clip->bound.left, clip->bound.top, clip->bound.right - clip->bound.left, clip->bound.bottom - clip->bound.top});
    }
    // trim geometry before native clipping so coincident fill/clip edges do not rasterize twice.
    *result = {bound.left, bound.top, fmaxf(0.0f, bound.right - bound.left), fmaxf(0.0f, bound.bottom - bound.top)};
    return true;
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
           !group->isolation && !group->mask.gradient;
}

static void pdf_record_page_backdrop_paint_list(PdfRenderContext* ctx,
                                                const PaintList* paint_list) {
    if (!ctx) return;
    render_effect_record_page_backdrop_paint_list(
        ctx->page_backdrop_ready, &ctx->page_backdrop_dl, paint_list,
        render_glyph_run_raster_lower, ctx->ui_context ? ui_context_raster_scale(ctx->ui_context) : 1.0f);
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
    // primitive alpha composes with the enclosing effect opacity and must reset for the next paint.
    HPDF_ExtGState alpha = HPDF_CreateExtGState(ctx->pdf_doc);
    if (alpha) {
        float opacity = color.a / 255.0f * ctx->paint_state.current_opacity;
        HPDF_ExtGState_SetAlphaFill(alpha, opacity);
        HPDF_ExtGState_SetAlphaStroke(alpha, opacity);
        HPDF_Page_SetExtGState(ctx->current_page, alpha);
    }
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

static void pdf_transform_point(const RdtMatrix* transform, float x, float y,
                                float* out_x, float* out_y);

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

static bool pdf_path_move_to(PdfPathContext* ctx, float x, float y) {
    float tx = 0.0f, ty = 0.0f;
    pdf_transform_point(ctx->transform, x, y, &tx, &ty);
    if (!pdf_page_move_to(ctx->pdf, tx, ty)) return false;
    pdf_path_set_current(ctx, x, y);
    ctx->subpath_x = x;
    ctx->subpath_y = y;
    return true;
}

static bool pdf_path_line_to(PdfPathContext* ctx, float x, float y) {
    float tx = 0.0f, ty = 0.0f;
    pdf_transform_point(ctx->transform, x, y, &tx, &ty);
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
    pdf_transform_point(ctx->transform, x1, y1, &tx1, &ty1);
    pdf_transform_point(ctx->transform, x2, y2, &tx2, &ty2);
    pdf_transform_point(ctx->transform, x3, y3, &tx3, &ty3);
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
        rdt_matrix_transform_point(transform, x, y, out_x, out_y);
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
                                     bool straight_alpha) {
    if (!ctx || !pixels || src_w <= 0 || src_h <= 0 || dst_w <= 0.0f || dst_h <= 0.0f) {
        return false;
    }
    if (opacity != 255) return false;

    int stride_pixels = pdf_image_stride_pixels(src_w, src_stride);
    bool opaque = pdf_image_pixels_are_opaque(pixels, src_w, src_h, stride_pixels);
    lam::Temp<uint32_t> straight;
    if (!opaque && !straight_alpha) {
        size_t count = 0, bytes = 0;
        if (!math_checked_mul((size_t)src_w, (size_t)src_h, &count) ||
            !math_checked_mul(count, sizeof(uint32_t), &bytes) ||
            !render_memory_allow_allocation(nullptr, bytes)) return false;
        straight = lam::temp_array<uint32_t>(count, MEM_CAT_RENDER);
        if (!straight) return false;
        // PDF stores straight RGB beside a soft alpha mask; replay buffers are premultiplied.
        for (int row = 0; row < src_h; row++) for (int column = 0; column < src_w; column++)
            straight[(size_t)row * (size_t)src_w + (size_t)column] =
                render_pixel_unpremultiply_abgr(pixels[(size_t)row * (size_t)stride_pixels + (size_t)column]);
        pixels = straight.get();
        stride_pixels = src_w;
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

    // the image owns its destination transform; another box clip changes overlapping edge coverage.
    if (opaque) {
        return HPDF_Page_DrawABGRImage(ctx->current_page, pixels, src_w, src_h,
            stride_pixels, a, b, c, d, e, f) == HPDF_OK;
    }
    return HPDF_Page_DrawABGRImageWithAlpha(ctx->current_page, pixels, src_w, src_h,
        stride_pixels, a, b, c, d, e, f) == HPDF_OK;
}

static bool pdf_draw_abgr_image(PdfRenderContext* ctx, const uint32_t* pixels,
                                int src_w, int src_h, int src_stride,
                                float dst_x, float dst_y, float dst_w, float dst_h,
                                uint8_t opacity, const RdtMatrix* transform,
                                bool straight_alpha = false) {
    return pdf_draw_abgr_image_impl(ctx, pixels, src_w, src_h, src_stride,
                                    dst_x, dst_y, dst_w, dst_h, opacity,
                                    transform, straight_alpha);
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
    render_effect_raster_begin(
        &ctx->effect_fallback, group, render_glyph_run_raster_lower);
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
        render_effect_group_needs_page_backdrop(&ctx->effect_fallback.group)
            ? &ctx->page_backdrop_dl
            : nullptr,
        &ctx->effect_fallback.group.bounds,
        ctx->page_width,
        ctx->page_height,
        false,
        ctx->ui_context ? ui_context_raster_scale(ctx->ui_context) : 1.0f,
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
                               const RdtMatrix* transform, RdtFillRule rule = RDT_FILL_WINDING) {
    if (!ctx || !path) return false;
    if (HPDF_Page_GSave(ctx->current_page) != HPDF_OK) return false;
    if (!pdf_render_path(ctx, path, transform)) {
        HPDF_Page_GRestore(ctx->current_page);
        return false;
    }
    if ((rule == RDT_FILL_EVEN_ODD ? HPDF_Page_Eoclip(ctx->current_page) :
        HPDF_Page_Clip(ctx->current_page)) != HPDF_OK) {
        HPDF_Page_GRestore(ctx->current_page);
        return false;
    }
    return true;
}

static void pdf_render_glyph_run(PdfRenderContext* ctx, const PaintGlyphRun* run,
                                 const RdtMatrix* stack_transform) {
    if (!ctx || !run) return;
    RdtPath* path = render_path_create_paint_glyph_run(run, ctx->ui_context ? ctx->ui_context->font_ctx.get() : nullptr);
    RdtMatrix composed;
    const RdtMatrix* transform = pdf_compose_transform(stack_transform,
        pdf_optional_transform(run->has_transform, &run->transform), &composed);
    float left, top, right, bottom;
    if (path && !rdt_path_get_bounds(path, &left, &top, &right, &bottom)) {
        rdt_path_free(path);
        return;
    }
    pdf_set_color(ctx, run->color);
    if (!path || !pdf_render_path(ctx, path, transform) || HPDF_Page_Fill(ctx->current_page) != HPDF_OK) {
        ctx->paint_state.unsupported_count++;
        log_error("[PDF_GLYPH_RUN] selected glyph outline could not be painted");
    }
    if (path) rdt_path_free(path);
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

static bool pdf_raster_fallback_gradient(PdfRenderContext* ctx,
                                         const PaintCmd* command,
                                         const RdtMatrix* transform) {
    if (!ctx || !command) return false;
    RdtPath* path = command->op == PAINT_FILL_LINEAR_GRADIENT
        ? command->fill_linear_gradient.path : command->fill_radial_gradient.path;
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (!pdf_gradient_path_bounds(path, &left, &top, &width, &height)) return false;

    Rect intersection;
    if (pdf_intersect_rect_clips(&ctx->paint_state, {left, top, width, height}, transform, &intersection)) {
        if (intersection.width <= 0.0f || intersection.height <= 0.0f) return true;
        Bound visible = {intersection.x, intersection.y,
            intersection.x + intersection.width, intersection.y + intersection.height};
        RdtMatrix inverse;
        if (transform && (!rdt_matrix_inverse(transform, &inverse) ||
                !rdt_matrix_project_rect_bounds(&inverse, visible.left, visible.top,
                    visible.right, visible.bottom, &visible.left, &visible.top,
                    &visible.right, &visible.bottom))) return false;
        // use the visible owner coordinates so clipping cannot change gradient sampling phase.
        float right = left + width, bottom = top + height;
        left = fmaxf(left, visible.left); top = fmaxf(top, visible.top);
        width = fminf(right, visible.right) - left;
        height = fminf(bottom, visible.bottom) - top;
        if (width <= 0.0f || height <= 0.0f) return true;
    }

    float density = ctx->ui_context ? ui_context_raster_scale(ctx->ui_context) : 1.0f;
    PaintList paint;
    RdtMatrix scale = rdt_matrix_scale(density, density);
    paint_push_transform(&paint, &scale);
    // shared replay preserves spread, ellipse/focal transforms, clipping and transparent stops.
    if (command->op == PAINT_FILL_LINEAR_GRADIENT) {
        const PaintFillLinearGradient& p = command->fill_linear_gradient;
        paint_fill_linear_gradient(&paint, path, p.x1, p.y1, p.x2, p.y2,
            p.stops, p.stop_count, p.rule, nullptr,
            pdf_optional_transform(p.has_gradient_transform, &p.gradient_transform), &p.options);
    } else {
        const PaintFillRadialGradient& p = command->fill_radial_gradient;
        paint_fill_radial_gradient(&paint, path, p.cx, p.cy, p.r,
            p.stops, p.stop_count, p.rule, nullptr,
            pdf_optional_transform(p.has_gradient_transform, &p.gradient_transform), &p.options);
    }
    paint_pop_transform(&paint);
    OffscreenRenderArenas arenas;
    if (!arenas.init("render.pdf.gradient", "render.pdf.gradient.list", "render.pdf.gradient.scratch")) return false;
    DisplayList dl = {}; dl_init(&dl, arenas.list_arena);
    paint_ir_lower_raster(&paint, &dl);
    Bound bounds = {left * density, top * density, (left + width) * density, (top + height) * density};
    ImageSurface* surface = render_display_list_snapshot(&dl,
        mem_context_process(MEM_ROLE_RENDER), bounds, density);
    dl_destroy(&dl);
    arenas.destroy();
    if (!surface) return false;
    // the snapshot already clips the path; another mask changes edge coverage.
    bool ok = pdf_draw_abgr_image_preserve_alpha(ctx, (const uint32_t*)surface->pixels,
        surface->width, surface->height, surface->pitch / 4, left, top,
        (float)surface->width / density, (float)surface->height / density, 255, transform);
    image_surface_destroy(surface);
    return ok;
}

static void pdf_lower_paint_list(PdfRenderContext* ctx, PaintList* commands = nullptr);

static bool pdf_export_paint_consume(PaintList* paint, void* context) {
    PdfRenderContext* source = (PdfRenderContext*)context;
    // accept only the native PDF operations with exact paint semantics; effects and unsupported strokes replay transparently.
    for (int i = 0; i < paint->item_count(); i++) {
        const PaintCmd& cmd = paint->data()[i];
        switch (cmd.op) {
        case PAINT_FILL_RECT: if (cmd.fill_rect.color.a != 255) return false; break;
        case PAINT_FILL_ROUNDED_RECT: if (cmd.fill_rounded_rect.color.a != 255) return false; break;
        case PAINT_FILL_PATH:
            if (cmd.fill_path.color.a != 255) return false;
            break;
        case PAINT_PUSH_CLIP: case PAINT_POP_CLIP:
        case PAINT_BEGIN_SEMANTIC_GROUP: case PAINT_END_SEMANTIC_GROUP: break;
        default: return false;
        }
    }
    PdfRenderContext local = {};
    local.pdf_doc = source->pdf_doc; local.current_page = source->current_page;
    local.ui_context = source->ui_context; local.page_width = source->page_width;
    local.page_height = source->page_height;
    // SVG's local lowerer shares the page graphics state and inherits its enclosing opacity.
    pdf_paint_lowering_state_init(&local.paint_state);
    local.paint_state.current_opacity = source->paint_state.current_opacity;
    pdf_lower_paint_list(&local, paint);
    source->paint_state.emitted_count += local.paint_state.emitted_count;
    pdf_release_rect_clips(&local.paint_state);
    return true;
}

static bool pdf_raster_fallback_svg_subscene(PdfRenderContext* ctx,
                                             const PaintSvgSubscene* subscene,
                                             const RdtMatrix* transform) {
    if (!ctx || !subscene || !subscene->svg_root ||
        subscene->viewport_width <= 0.0f ||
        subscene->viewport_height <= 0.0f) {
        return false;
    }

    Bound bounds = {};
    ImageSurface* surface = render_svg_subscene_rasterize(subscene, &bounds);
    if (!surface) return false;
    RdtMatrix placement = transform ? rdt_matrix_multiply(transform, &subscene->transform) : subscene->transform;
    bool ok = pdf_draw_abgr_image_preserve_alpha(ctx, (const uint32_t*)surface->pixels,
        surface->width, surface->height, surface->pitch / 4,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        255, &placement);
    image_surface_destroy(surface);
    return ok;
}

static void pdf_lower_paint_list(PdfRenderContext* ctx, PaintList* commands) {
    if (!ctx || ctx->effect_fallback.active) return;
    PaintList& paint = commands ? *commands : ctx->paint_list;
    render_svg_inline_register_paint_ir_lowerers();

    PdfPaintLoweringState* state = &ctx->paint_state;
    bool streaming_transform =
        paint_list_has_op_flags(&paint, PAINT_OP_FLAG_TRANSFORM_STACK) ||
        state->active_transform_depth > 0 ||
        state->skipped_transform_depth > 0;
    bool streaming_effect =
        paint_list_has_op_flags(&paint, PAINT_OP_FLAG_EFFECT_STACK) ||
        state->active_effect_depth > 0 ||
        state->passthrough_effect_depth > 0;
    // descendant clips span batches, just like transform and effect scopes.
    bool streaming_clip = paint_list_has_op_flags(&paint, PAINT_OP_FLAG_CLIP_STACK) ||
        state->active_clip_depth > 0 || state->skipped_clip_depth > 0;
    if (!streaming_transform && !streaming_effect && !streaming_clip &&
        !paint_ir_validate_or_log(&paint, "pdf_lower_paint_list")) {
        paint_list_clear(&paint);
        return;
    }
    pdf_record_page_backdrop_paint_list(ctx, &paint);
    const RenderExportTargetCaps* caps =
        render_export_target_get_caps(RENDER_EXPORT_TARGET_PDF);
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
            if (state->passthrough_effect_depth > 0) {
                state->passthrough_effect_depth++;
                state->emitted_count++;
                return true;
            }
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
            // PDF alpha belongs to the isolated group invocation, not each overlapping primitive.
            Bound bounds = p->bounds;
            if (state->active_transform_depth > 0 &&
                    !rdt_matrix_project_rect_bounds(&state->current_transform,
                        bounds.left, bounds.top, bounds.right, bounds.bottom,
                        &bounds.left, &bounds.top, &bounds.right, &bounds.bottom))
                bounds = {};
            if (!(bounds.right > bounds.left && bounds.bottom > bounds.top))
                bounds = {0.0f, 0.0f, ctx->page_width, ctx->page_height};
            // a Form BBox clips its contents; enclose fractional ink edges instead of clipping AA twice.
            float density = ui_context_raster_scale(ctx->ui_context);
            bounds = {floorf(bounds.left * density) / density,
                floorf(bounds.top * density) / density,
                ceilf(bounds.right * density) / density,
                ceilf(bounds.bottom * density) / density};
            if (HPDF_Page_BeginTransparencyGroup(ctx->current_page, p->opacity,
                    bounds.left, ctx->page_height - bounds.bottom,
                    bounds.right, ctx->page_height - bounds.top) != HPDF_OK) {
                state->passthrough_effect_depth++;
                state->fallback_count++;
                log_error("[PDF_PAINT_IR] failed to begin isolated opacity group");
                return true;
            }
            HPDF_Page_TrackTransparencyGroupInk(ctx->current_page, density);
            state->opacity_stack[state->active_effect_depth++] = state->current_opacity;
            state->current_opacity = 1.0f;
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
            if (HPDF_Page_EndTransparencyGroup(ctx->current_page) != HPDF_OK) {
                state->unsupported_count++;
                log_error("[PDF_PAINT_IR] failed to close isolated opacity group");
            }
            state->emitted_count++;
            return true;
        }
        return false;
    };

    for (int i = 0; i < paint.item_count(); i++) {
        PaintCmd* cmd = &paint.data()[i];
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
                if (state->skipped_clip_depth > 0) {
                    state->skipped_clip_depth++;
                    return true;
                }
                if (!caps || !caps->clips) {
                    state->skipped_clip_depth++;
                    return true;
                }
                PaintPushClip* p = &cmd->push_clip;
                if (!resolve_command_transform(p->has_transform, &p->transform)) {
                    state->skipped_clip_depth++;
                    return true;
                }
                if (pdf_push_clip_path(ctx, p->clip_path,
                                       effective_transform, p->rule)) {
                    pdf_push_rect_clip(state, p->clip_path, effective_transform);
                    state->active_clip_depth++;
                } else {
                    state->skipped_clip_depth++;
                }
                return true;
            }
            if (paint_op_has_flags(cmd->op, PAINT_OP_FLAG_STACK_POP)) {
                if (state->skipped_clip_depth > 0) {
                    state->skipped_clip_depth--;
                } else if (state->active_clip_depth > 0) {
                    HPDF_Page_GRestore(ctx->current_page);
                    pdf_pop_rect_clip(state);
                    state->active_clip_depth--;
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
            Rect intersection;
            if (pdf_intersect_rect_clips(state, {p->x, p->y, p->w, p->h}, stack_transform, &intersection)) {
                if (intersection.width > 0.0f && intersection.height > 0.0f)
                    pdf_render_rect(ctx, intersection.x, intersection.y, intersection.width, intersection.height, p->color, true);
                state->emitted_count++;
                break;
            }
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
            if (p->color.a == 0) break;
            if (!resolve_command_transform(p->has_transform, &p->transform)) break;
            pdf_set_color(ctx, p->color);
            if (pdf_render_path(ctx, p->path, effective_transform)) {
                // PaintIR fill rules apply to all compound paths, including geographic holes.
                if (p->rule == RDT_FILL_EVEN_ODD) HPDF_Page_Eofill(ctx->current_page);
                else HPDF_Page_Fill(ctx->current_page);
                state->emitted_count++;
            }
            break;
        }
        case PAINT_STROKE_PATH: {
            if (!caps || !caps->strokes) break;
            PaintStrokePath* p = &cmd->stroke_path;
            if (p->color.a == 0) break;
            // set even empty patterns so a later solid stroke cannot inherit earlier dashes.
            if (HPDF_Page_SetDashPattern(ctx->current_page, p->dash_array, p->dash_count, p->dash_phase) != HPDF_OK) break;
            if (!resolve_command_transform(p->has_transform, &p->transform)) break;
            pdf_set_color(ctx, p->color);
            HPDF_Page_SetLineWidth(ctx->current_page, p->width);
            HPDF_Page_SetLineCap(ctx->current_page, p->cap);
            HPDF_Page_SetLineJoin(ctx->current_page, p->join);
            HPDF_Page_SetMiterLimit(ctx->current_page, p->miter_limit);
            if (pdf_render_path(ctx, p->path, effective_transform)) {
                HPDF_Page_Stroke(ctx->current_page);
                state->emitted_count++;
            }
            break;
        }
        case PAINT_FILL_LINEAR_GRADIENT:
        case PAINT_FILL_RADIAL_GRADIENT: {
            if (!caps || !caps->gradients) break;
            bool has_transform = cmd->op == PAINT_FILL_LINEAR_GRADIENT
                ? cmd->fill_linear_gradient.has_transform : cmd->fill_radial_gradient.has_transform;
            const RdtMatrix* local = cmd->op == PAINT_FILL_LINEAR_GRADIENT
                ? &cmd->fill_linear_gradient.transform : &cmd->fill_radial_gradient.transform;
            if (!resolve_command_transform(has_transform, local)) break;
            pdf_paint_record_fallback(state, pdf_raster_fallback_gradient(ctx, cmd, effective_transform),
                "[PDF_PAINT_IR] failed gradient raster fallback");
            break;
        }
        case PAINT_DRAW_IMAGE: {
            if (!caps || !caps->images) break;
            PaintDrawImage* p = &cmd->draw_image;
            if (!resolve_command_transform(p->has_transform, &p->transform)) break;
            pdf_draw_abgr_image(ctx, p->pixels, p->src_w, p->src_h, p->src_stride,
                                p->dst_x, p->dst_y, p->dst_w, p->dst_h,
                                p->opacity, effective_transform,
                                p->resource_owner && p->resource_owner->alpha_mode == IMAGE_ALPHA_STRAIGHT);
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
                                effective_transform, img->alpha_mode == IMAGE_ALPHA_STRAIGHT);
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
        case PAINT_BEGIN_SEMANTIC_GROUP:
        case PAINT_END_SEMANTIC_GROUP:
            break; // semantic wrappers have no paint or PDF graphics-state effect
        case PAINT_SVG_SUBSCENE: {
            PaintSvgSubscene* p = &cmd->svg_subscene;
            PaintSvgSubscene vector_scene = *p;
            if (stack_transform) vector_scene.transform = rdt_matrix_multiply(stack_transform, &p->transform);
            if (render_svg_subscene_with_paint(&vector_scene, pdf_export_paint_consume, ctx)) break;
            pdf_paint_record_fallback(state,
                                      pdf_raster_fallback_svg_subscene(ctx, p, stack_transform),
                                      "[PDF_PAINT_IR] failed SVG subscene raster fallback");
            break;
        }
        default:
            break;
        }
    }
    paint_list_clear(&paint);
}

static void pdf_paint_fill_rect(PdfRenderContext* ctx,
                                float x, float y, float width, float height,
                                Color color) {
    paint_fill_rect(pdf_active_paint_list(ctx), x, y, width, height, color);
    pdf_lower_paint_list(ctx);
}



static bool pdf_paint_stroke_path(PdfRenderContext* ctx, RdtPath* path,
                                  Color color, float width) {
    PaintList* list = pdf_active_paint_list(ctx);
    int index = list ? list->item_count() : -1;
    paint_stroke_path(list, path, color, width,
                      RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0.0f, nullptr);
    bool owns_path = ctx->effect_fallback.active &&
        paint_list_take_path_payload(list, index, PAINT_STROKE_PATH);
    pdf_lower_paint_list(ctx);
    return owns_path;
}

static bool pdf_has_border_radius(const BorderProp* border) {
    return border && radiant_corner_has_radius(&border->radius);
}

// Render text view
static void render_text_view_pdf(PdfRenderContext* ctx, ViewText* text) {
    if (!text || !text->text_data()) return;
    CssEnum text_transform = render_text_inherited_transform(text);

    // Calculate absolute position using block context (like SVG renderer)
    // Extract the text content
    unsigned char* str = text->text_data();
    RenderTextPaintIterator paint_iterator(ctx, text);
    TextRect* text_rect;
    while ((text_rect = paint_iterator.next())) {
    float base_x = (float)ctx->block.x + text_rect->x, y = (float)ctx->block.y + text_rect->y;

    lam::Temp<char> text_content(render_text_create_export_segment(
        str, text_rect, text_transform, true, get_white_space_value(text)));

    if (!text_content.get() || !text_content.get()[0]) continue;

    // Set font if available
    float font_size = 16.0f;
    if (ctx->current_font) {
        font_size = ctx->font.style->font_size ? ctx->font.style->font_size : 16.0f;
        HPDF_Page_SetFontAndSize(ctx->current_page, ctx->current_font, font_size);
    }

    CssEnum justify_mode = text_justify_computed_value(text->parent);
    RenderTextJustification justification = render_text_justification(text_rect, justify_mode);

    float baseline_y = render_text_baseline_y(&ctx->font, y);
    PaintGlyphRun run = {};
    run.font = lam::up(&ctx->font);
    run.color = ctx->color;
    run.text = lam::up(text_content.get());
    run.text_len = (int)strlen(text_content.get()); // INT_CAST_OK: text run byte length is bounded by TextRect input.
    // effect fallback retains commands until rasterization, so the paint list keeps the text
    if (ctx && ctx->effect_fallback.active) run.owned_text = lam::own((const char*)text_content.release());
    run.font_family = ctx->font.style ? ctx->font.style->family : nullptr;
    run.font_size = font_size;
    run.x = base_x;
    run.baseline_y = baseline_y;
    run.word_spacing = justification.word;
    run.cjk_spacing = justification.character;
    run.justify_mode = justify_mode;
    DomElement* parent = text->parent ? text->parent->as_element() : nullptr;
    if (parent && render_paint_text_background(pdf_active_paint_list(ctx), ctx->ui_context,
            parent, &run, {ctx->block.x, ctx->block.y, parent->width, parent->height})) {
        pdf_lower_paint_list(ctx);
    }
    RenderTextDecorationSource decoration = {str, text_rect, ctx->block.x, ctx->block.y};
    if (parent && parent->font && parent->fontp()->text_shadow) {
        if (!render_paint_text_shadows(pdf_active_paint_list(ctx), ctx->ui_context,
                &run, parent->fontp()->text_shadow, &decoration)) {
            ctx->paint_state.unsupported_count++;
            log_error("[PDF_TEXT_SHADOW] cannot record complete shadow geometry");
        }
        pdf_lower_paint_list(ctx);
    }
    if (!render_paint_text_decorations(pdf_active_paint_list(ctx), &ctx->font, str, text_rect,
        ctx->block.x, ctx->block.y, ctx->color, true)) {
        ctx->paint_state.unsupported_count++;
        log_error("[PDF_TEXT_DECORATION] cannot record complete decoration geometry");
    }
    pdf_lower_paint_list(ctx);
    paint_glyph_run(pdf_active_paint_list(ctx), &run);
    pdf_lower_paint_list(ctx);
    if (!render_paint_text_emphasis(pdf_active_paint_list(ctx),
            ctx->ui_context ? ctx->ui_context->font_ctx.get() : nullptr, &run)) {
        ctx->paint_state.unsupported_count++;
        log_error("[PDF_TEXT_EMPHASIS] cannot record selected mark geometry");
    }
    pdf_lower_paint_list(ctx);
    if (!render_paint_text_decorations(pdf_active_paint_list(ctx), &ctx->font, str, text_rect,
        ctx->block.x, ctx->block.y, ctx->color, false)) {
        ctx->paint_state.unsupported_count++;
        log_error("[PDF_TEXT_DECORATION] cannot record complete decoration geometry");
    }
    pdf_lower_paint_list(ctx);

    pdf_record_link_rect(ctx, pdf_nearest_link(text), base_x, y,
        text_rect->width, text_rect->height);

    }
}

// ============================================================================
// PDF RenderBackend vtable callbacks
// ============================================================================

static void pdf_cb_render_bound(RenderContext* vctx, ViewBlock* view, float abs_x, float abs_y) {
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

    if (render_paint_background(pdf_active_paint_list(ctx), ctx->ui_context, view,
            {abs_x, abs_y, width, height}, RENDER_EXPORT_TARGET_PDF)) pdf_lower_paint_list(ctx);

    if (render_paint_border_image(pdf_active_paint_list(ctx), ctx->ui_context,
            view->boundary()->border, {abs_x, abs_y, width, height})) {
        pdf_lower_paint_list(ctx);
        return;
    }

    // Borders
    if (view->boundary()->border) {
        BorderProp* border = view->boundary()->border;
        if (pdf_has_border_radius(border) && render_paint_boundary_emit_solid_border(
                pdf_active_paint_list(ctx), border, {abs_x, abs_y, width, height})) {
            pdf_lower_paint_list(ctx);
            return;
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

static void pdf_cb_render_text(RenderContext* vctx, ViewText* text, float abs_x, float abs_y,
                               FontBox* font, Color color) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    ctx->block.x = abs_x;
    ctx->block.y = abs_y;
    ctx->font = *font;
    ctx->color = color;
    render_text_view_pdf(ctx, text);
}

static void pdf_cb_render_image(RenderContext* vctx, ViewBlock* block, float abs_x, float abs_y) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx || !block) return;
    ImageSurface* img = render_media_image_surface(block);
    if (!img) return;

    BlockBlot image_block = {};
    image_block.x = abs_x - block->x;
    image_block.y = abs_y - block->y;
    Rect content_rect = render_geometry_block_content_rect(&image_block, block, 1.0f);
    if (render_media_paint_image_content(pdf_active_paint_list(ctx), ctx->ui_context, block, &content_rect)) {
        pdf_lower_paint_list(ctx);
    }
    pdf_record_link_rect(ctx, pdf_nearest_link(block), abs_x, abs_y,
        block->width, block->height);
}

static void pdf_cb_visit_element(RenderContext* vctx, ViewElement* view,
                                  float abs_x, float abs_y) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    DomElement* element = lam::dom_require_element(lam::view_dom_node(view));
    if (!ctx || !element) return;
    if (element->id) pdf_record_destination(ctx, element->id, abs_x, abs_y);
    // semantic visitation includes hidden boxes so visible descendants keep their destinations.
    if (view->is_block() && element->tag_id == MARKUP_NAME_A &&
        !view_visibility_is_hidden(view)) {
        pdf_record_link_rect(ctx, element->get_attribute("href"), abs_x, abs_y,
            view->width, view->height);
    }
}

static void pdf_cb_render_geomap(RenderContext* vctx, ViewBlock* block, float abs_x, float abs_y) {
    auto* ctx = (PdfRenderContext*)vctx;
    BlockBlot parent = {};
    parent.x = abs_x - block->x; parent.y = abs_y - block->y;
    geomap_paint(pdf_active_paint_list(ctx), block->as_element(),
        render_geometry_block_content_rect(&parent, block, 1.0f));
    pdf_lower_paint_list(ctx);
}

static void pdf_cb_render_inline_svg(RenderContext* vctx, ViewBlock* block, float abs_x, float abs_y,
                                     FontBox* font, Color color) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx || !block) return;

    DomElement* dom_elem = lam::dom_require_element(lam::view_dom_node(block));
    if (!dom_elem || dom_elem->is_synthetic()) {
        return;
    }

    BlockBlot block_context = {};
    block_context.x = abs_x - block->x;
    block_context.y = abs_y - block->y;
    Rect content_rect = render_geometry_block_content_rect(&block_context, block, 1.0f);
    if (content_rect.width <= 0.0f || content_rect.height <= 0.0f) {
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
    RdtMatrix placement = rdt_matrix_translate(content_rect.x, content_rect.y);
    render_svg_build_subscene(&subscene,
                              dom_element_to_element(dom_elem),
                              content_rect.width,
                              content_rect.height,
                              pool,
                              ui_context_raster_scale(ctx->ui_context),
                              font_ctx,
                              &placement,
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
    paint_svg_subscene(pdf_active_paint_list(ctx), &subscene);
    pdf_lower_paint_list(ctx);
    pdf_record_link_rect(ctx, pdf_nearest_link(block), content_rect.x, content_rect.y,
        content_rect.width, content_rect.height);
    (void)font;
}

static void pdf_cb_render_svg_subscene(RenderContext* vctx, const PaintSvgSubscene* subscene) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx || !subscene) return;
    paint_svg_subscene(pdf_active_paint_list(ctx), subscene);
    pdf_lower_paint_list(ctx);
}

static void pdf_cb_render_column_rules(RenderContext* vctx, ViewBlock* block, float abs_x, float abs_y) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    RenderColumnRuleIterator iterator;
    if (!ctx || !render_column_rules_begin(block, &iterator)) return;
    MultiColumnProp* mc = block->multicol_prop();
    if (mc->rule_color.a == 0) return;

    if (mc->rule_style == CSS_VALUE_DOTTED ||
        mc->rule_style == CSS_VALUE_DASHED) {
        log_debug("[PDF_PAINT_IR] column-rule style requires dashed strokes; skipping style=%d",
                  mc->rule_style);
        return;
    }

    float rule_x = 0.0f, block_y = 0.0f, rule_height = 0.0f;
    while (render_column_rule_next(&iterator, &rule_x, &block_y, &rule_height)) {
        rule_x += abs_x;
        block_y += abs_y;

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

static void pdf_cb_begin_transform(RenderContext* vctx, ViewBlock* block, float abs_x, float abs_y) {
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

static void pdf_cb_end_transform(RenderContext* vctx) {
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

static bool pdf_cb_begin_clip(RenderContext* vctx, ViewElement* element,
                              float abs_x, float abs_y, RenderClipKind kind) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx || !render_clip_push_vector(pdf_active_paint_list(ctx), element,
            abs_x, abs_y, kind)) return false;
    pdf_lower_paint_list(ctx);
    return true;
}

static void pdf_cb_end_clip(RenderContext* vctx) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx) return;
    paint_pop_clip(pdf_active_paint_list(ctx));
    pdf_lower_paint_list(ctx);
}

static void pdf_cb_begin_effect_group(RenderContext* vctx, const PaintEffectGroup* group) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    if (!ctx) return;
    if (ctx->effect_fallback.active) {
        paint_begin_effect_group(&ctx->effect_fallback.paint_list, group);
        ctx->effect_fallback.nested_depth++;
        return;
    }
    if (render_effect_group_needs_raster_fallback(group)) {
        pdf_begin_effect_raster_fallback(ctx, group);
        return;
    }
    paint_begin_effect_group(&ctx->paint_list, group);
    pdf_lower_paint_list(ctx);
}

static void pdf_cb_end_effect_group(RenderContext* vctx) {
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

static void pdf_cb_on_font_change(RenderContext* vctx, FontProp* font_prop) {
    PdfRenderContext* ctx = (PdfRenderContext*)vctx;
    const char* pdf_font_name = get_pdf_font_name(font_prop->family);
    HPDF_Font font = HPDF_GetFont(ctx->pdf_doc, pdf_font_name, NULL);
    if (font) {
        ctx->current_font = font;
        HPDF_Page_SetFontAndSize(ctx->current_page, font, font_prop->font_size);
    }
}

static void pdf_cb_render_marker(RenderContext* context, ViewSpan* marker, float x, float y,
                                 FontBox* font, Color color) {
    PdfRenderContext* pdf = (PdfRenderContext*)context;
    if (render_paint_marker_image(pdf_active_paint_list(pdf), pdf->ui_context, marker, x, y, font))
        pdf_lower_paint_list(pdf);
}

static RenderBackend pdf_make_backend(PdfRenderContext* ctx) {
    RenderBackend b = {};
    b.ctx              = lam::up(ctx);
    b.render_bound     = pdf_cb_render_bound;
    b.render_text      = pdf_cb_render_text;
    b.render_marker    = pdf_cb_render_marker;
    b.render_image     = pdf_cb_render_image;
    b.render_inline_svg = pdf_cb_render_inline_svg;
    b.render_geomap = pdf_cb_render_geomap;
    b.render_svg_subscene = pdf_cb_render_svg_subscene;
    b.visit_element     = pdf_cb_visit_element;
    b.begin_block_children  = NULL;
    b.end_block_children    = NULL;
    b.begin_inline_children = NULL;
    b.end_inline_children   = NULL;
    b.begin_effect_group = pdf_cb_begin_effect_group;
    b.end_effect_group = pdf_cb_end_effect_group;
    b.begin_transform  = pdf_cb_begin_transform;
    b.end_transform    = pdf_cb_end_transform;
    b.begin_clip       = pdf_cb_begin_clip;
    b.end_clip         = pdf_cb_end_clip;
    b.render_column_rules = pdf_cb_render_column_rules;
    b.on_font_change   = pdf_cb_on_font_change;
    return b;
}

static void pdf_context_destroy(PdfRenderContext* ctx) {
    pdf_release_rect_clips(&ctx->paint_state);
    paint_list_destroy(&ctx->paint_list);
    paint_list_destroy(&ctx->effect_fallback.paint_list);
    if (ctx->page_backdrop_ready) dl_destroy(&ctx->page_backdrop_dl);
    if (ctx->page_backdrop_arena) mem_arena_destroy(ctx->page_backdrop_arena);
    ctx->page_backdrop_ready = false;
    ctx->page_backdrop_arena = nullptr;
}

static bool pdf_context_begin(PdfRenderContext* ctx, UiContext* ui) {
    ctx->pdf_doc = HPDF_New(pdf_error_handler, nullptr);
    if (!ctx->pdf_doc) return false;
    HPDF_SetCompressionMode(ctx->pdf_doc, HPDF_COMP_ALL);
    HPDF_SetInfoAttr(ctx->pdf_doc, HPDF_INFO_CREATOR, "Lambda Script Renderer");
    HPDF_SetInfoAttr(ctx->pdf_doc, HPDF_INFO_PRODUCER, "Lambda PDF Renderer");
    if (ui && ui->document) pdf_apply_document_info(ctx, ui->document);
    if (ui && ui->document && !pdf_collect_outlines(ctx, ui->document->root)) {
        HPDF_Free(ctx->pdf_doc);
        ctx->pdf_doc = nullptr;
        return false;
    }
    ctx->ui_context = lam::up(ui);
    ctx->color = {.r = 0, .g = 0, .b = 0, .a = 255};
    if (ui) ctx->font.style = lam::up(&ui->default_font);
    ctx->current_font = HPDF_GetFont(ctx->pdf_doc, "Helvetica", nullptr);
    paint_list_init(&ctx->paint_list, nullptr);
    paint_list_init(&ctx->effect_fallback.paint_list, nullptr);
    return true;
}

static bool pdf_page_begin(PdfRenderContext* ctx, float width, float height, float scale) {
    if (!isfinite(width) || !isfinite(height) || !isfinite(scale) || width <= 0.0f || height <= 0.0f || scale <= 0.0f) return false;
    if (ctx->page_backdrop_ready) dl_destroy(&ctx->page_backdrop_dl);
    if (ctx->page_backdrop_arena) mem_arena_destroy(ctx->page_backdrop_arena);
    ctx->page_backdrop_ready = false;
    ctx->page_backdrop_arena = nullptr;
    paint_list_clear(&ctx->paint_list);
    paint_list_clear(&ctx->effect_fallback.paint_list);
    pdf_release_rect_clips(&ctx->paint_state);
    pdf_paint_lowering_state_init(&ctx->paint_state);
    ctx->current_page = HPDF_AddPage(ctx->pdf_doc);
    if (!ctx->current_page || HPDF_Page_SetWidth(ctx->current_page, width * scale) != HPDF_OK ||
        HPDF_Page_SetHeight(ctx->current_page, height * scale) != HPDF_OK ||
        HPDF_Page_Concat(ctx->current_page, scale, 0, 0, scale, 0, 0) != HPDF_OK) return false;
    ctx->page_width = width; ctx->page_height = height; ctx->page_scale = scale;
    ctx->current_x = ctx->current_y = 0.0f;
    ctx->block.x = ctx->block.y = 0.0f;
    ctx->transform_emitted_depth = ctx->transform_emitted_overflow_depth = 0;
    if (ctx->current_font) HPDF_Page_SetFontAndSize(ctx->current_page, ctx->current_font, 16.0f);
    // backdrop and lowering stacks belong to one physical page, never the preview root.
    ctx->page_backdrop_arena = mem_arena_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_RENDER, "render.pdf.backdrop.arena");
    if (!ctx->page_backdrop_arena) return false;
    dl_init(&ctx->page_backdrop_dl, ctx->page_backdrop_arena);
    ctx->page_backdrop_ready = true;
    Color white = {.r = 255, .g = 255, .b = 255, .a = 255};
    dl_fill_rect(&ctx->page_backdrop_dl, 0.0f, 0.0f, width, height, white);
    return true;
}

// Main PDF rendering function
static HPDF_Doc render_view_tree_to_pdf(UiContext* uicon, View* root_view, float width, float height,
                                       float output_scale, float content_x, float content_y) {
    if (!root_view || !uicon || !isfinite(output_scale) || output_scale <= 0) {
        return NULL;
    }

    PdfRenderContext ctx = {};
    if (!pdf_context_begin(&ctx, uicon)) return nullptr;
    if (!pdf_page_begin(&ctx, width, height, output_scale)) {
        pdf_context_destroy(&ctx); HPDF_Free(ctx.pdf_doc); return nullptr;
    }

    Color background = render_document_output_background(root_view);
    paint_fill_rect(&ctx.paint_list, 0, 0, width, height, background);
    pdf_lower_paint_list(&ctx);

    // Render the root view via shared tree walker
    RenderBackend backend = pdf_make_backend(&ctx);
    RenderWalkState walk_state = {};
    walk_state.x = content_x;
    walk_state.y = content_y;
    walk_state.font = ctx.font;
    walk_state.color = ctx.color;
    walk_state.ui_context = lam::up(uicon);

    if (root_view->view_type == RDT_VIEW_BLOCK) {
        render_walk_block(&backend, &walk_state, lam::view_require_block(root_view));
    } else if (root_view->view_type >= RDT_VIEW_INLINE) {
        render_walk_children(&backend, &walk_state, root_view);
    }
    RenderPathTrace trace = {};
    trace.target = lam::up("pdf");
    trace.replay_mode = lam::up("paint_ir_pdf");
    trace.backend_name = lam::up("pdf_export");
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
    radiant_apply_export_caps(&trace, caps);
    render_profiler_emit_path_trace(nullptr, uicon, nullptr, &trace);

    pdf_context_destroy(&ctx);
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

static void pdf_record_fragment_semantics(PdfRenderContext* ctx, DomDocument* doc,
                                           LayoutViewNode* node) {
    if (!ctx || !doc || !node) return;
    DomNode* source = dom_node_ref_validate(doc, node->source);
    if (source) {
        DomElement* element = source->as_element();
        if (element && element->id) {
            pdf_record_destination(ctx, element->id, node->rect.x, node->rect.y);
        }
        if (node->glyph_run || (node->paint_box && !node->first_child)) {
            pdf_record_link_rect(ctx, pdf_nearest_link(source), node->rect.x,
                node->rect.y, node->rect.width, node->rect.height);
        }
    }
    for (LayoutViewNode* child = node->first_child; child; child = child->next_sibling)
        pdf_record_fragment_semantics(ctx, doc, child);
}

static bool pdf_secondary_page(ViewTree* tree, const ViewPageBox* page,
        const ViewPagePlacement*, void* context) {
    PdfRenderContext* ctx = (PdfRenderContext*)context;
    // CSS reference pixels are 1/96 inch; PDF points are 1/72 inch.
    if (!pdf_page_begin(ctx, page->node.rect.width, page->node.rect.height, 72.0f / 96.0f)) return false;
    if (page->fixed && page->fixed->label &&
        HPDF_Page_SetLabel(ctx->current_page, page->fixed->label) != HPDF_OK) return false;
    if (page->fixed && page->fixed->geometry.box_mask) {
        const RadiantFixedGeometry& geometry = page->fixed->geometry;
        static const HPDF_PageBox kinds[] = {HPDF_PAGE_BOX_MEDIA, HPDF_PAGE_BOX_CROP,
            HPDF_PAGE_BOX_BLEED, HPDF_PAGE_BOX_TRIM, HPDF_PAGE_BOX_ART};
        for (size_t i = 0; i < RADIANT_SOURCE_BOX_COUNT; i++) if (geometry.box_mask & (1u << i)) {
            const RdtLogicalRect& box = geometry.boxes[i];
            if (HPDF_Page_SetBox(ctx->current_page, kinds[i], box.x, box.y, box.x + box.width, box.y + box.height) != HPDF_OK) return false;
        }
        RdtMatrix matrix = radiant_fixed_page_source_transform(&geometry);
        if (HPDF_Page_SetRotate(ctx->current_page, geometry.rotation) != HPDF_OK ||
            HPDF_Page_Concat(ctx->current_page, matrix.e11, matrix.e21, matrix.e12, matrix.e22, matrix.e13, matrix.e23) != HPDF_OK) return false;
    }
    if (!layout_secondary_paint_page(tree, page, &ctx->paint_list) ||
        !paint_ir_validate_or_log(&ctx->paint_list, "pdf secondary page")) return false;
    for (LayoutViewNode* child = page->node.first_child; child; child = child->next_sibling)
        pdf_record_fragment_semantics(ctx, tree->model->document, child);
    pdf_lower_paint_list(ctx);
    return ctx->paint_state.unsupported_count == 0 && ctx->paint_state.active_transform_depth == 0 &&
        ctx->paint_state.active_effect_depth == 0 && ctx->paint_state.active_clip_depth == 0 &&
        ctx->paint_state.skipped_clip_depth == 0;
}

bool render_secondary_view_to_pdf(ViewTree* tree, const char* filename,
        const ViewPageSelection* selection, UiContext* ui) {
    if (!filename || !tree || !tree->model || !tree->model->committed ||
        !view_tree_model_source_valid(tree) || tree->model->environment.presentation != VIEW_PRESENTATION_PAGED) return false;
    PdfRenderContext ctx = {};
    if (!pdf_context_begin(&ctx, ui)) return false;
    ViewModelStatus status = view_tree_pages_visit(tree, selection, pdf_secondary_page, &ctx);
    bool ok = status == VIEW_MODEL_OK && ctx.current_page && save_pdf_to_file(ctx.pdf_doc, filename);
    pdf_context_destroy(&ctx);
    HPDF_Free(ctx.pdf_doc);
    return ok;
}

int render_export_session_to_pdf(RenderExportSession* session, const char* pdf_file) {
    if (!session) return 1;
    if (session->paged_view) {
        bool ok = render_secondary_view_to_pdf(session->paged_view, pdf_file, nullptr, session->ui_context);
        if (ok) log_info("[EXPORT_PAGED] Saved %zu physical pages to %s", session->paged_view->model->page_count, pdf_file);
        else log_error("[EXPORT_PAGED] Could not encode physical pages to %s", pdf_file);
        return ok ? 0 : 1;
    }
    UiContext* ui_context = session->ui_context;
    DomDocument* doc = session->document;

    // Render to PDF (apply scale to output dimensions)
    if (doc->view_tree && doc->view_tree->root) {
        // @page controls the paper box; the continuous view stays inside its content rect.
        float pdf_width = session->has_page_geometry ? session->page_width : (float)session->content_width;
        float pdf_height = session->has_page_geometry ? session->page_height : (float)session->content_height;
        // CSS physical lengths use 96 px/in; PDF MediaBox coordinates use 72 pt/in.
        float page_scale = session->output_scale *
            (session->has_page_geometry ? 72.0f / 96.0f : 1.0f);
        HPDF_Doc pdf_doc = render_view_tree_to_pdf(ui_context, doc->view_tree->root,
                                                   pdf_width, pdf_height, page_scale,
                                                   session->has_page_geometry ? session->page_content_x : 0.0f,
                                                   session->has_page_geometry ? session->page_content_y : 0.0f);
        if (pdf_doc) {
            if (save_pdf_to_file(pdf_doc, pdf_file)) {
                log_info("Successfully rendered HTML to PDF: %s", pdf_file);
                HPDF_Free(pdf_doc);
                return 0;
            } else {
                HPDF_Free(pdf_doc);
            }
        } else {
        }
    } else {
    }

    return 1;
}

// Main function to layout HTML and render to PDF.
int render_html_to_pdf(const char* html_file, const char* pdf_file, int viewport_width,
        int viewport_height, float scale, bool paged, float device_scale) {
    RenderExportSession session;
    if (!render_export_session_begin(
            &session, html_file, viewport_width, viewport_height, 800, 1200, scale,
            true, paged, device_scale)) {
        return 1;
    }
    int result = render_export_session_to_pdf(&session, pdf_file);
    render_export_session_end(&session);
    return result;
}

int render_document_transform_to_pdf(const char* document_file,
        const LambdaDocumentTransformConfig* transform,
        const LambdaDocumentTransformOption* options, int option_count,
        const char* pdf_file, int viewport_width, int viewport_height, float scale, bool paged, float device_scale) {
    RenderExportSession session;
    if (!render_export_session_begin_document_transform(&session, document_file, transform,
            options, option_count, viewport_width, viewport_height, 800, 1200, scale,
            device_scale, false, paged, true)) {
        return 1;
    }
    int result = render_export_session_to_pdf(&session, pdf_file);
    render_export_session_end(&session);
    return result;
}

// ============================================================================
// Math Rendering Functions for PDF
// ============================================================================
// NOTE: MathBox rendering has been removed.
// ============================================================================
