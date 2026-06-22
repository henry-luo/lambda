// rdt_vector_cg.mm — Core Graphics backend for RdtVector (macOS)
#include "rdt_vector.hpp"
#include "render_svg_inline.hpp"
#include "../lib/log.h"
#include "../lib/mem_factory.h"
#include "../lib/mem.h"
#include "../lib/mempool.h"
#include "../lambda/input/input.hpp"
#include "../lambda/input/input-parsers.h"
#include "../lambda/lambda-data.hpp"
#define Rect MacOSRect
#include <CoreGraphics/CoreGraphics.h>
#undef Rect
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#ifdef __APPLE__

// ============================================================================
// Internal types
// ============================================================================

struct RdtVectorImpl {
    CGContextRef ctx;
    CGColorSpaceRef colorspace;
    uint32_t* pixels;
    uint32_t* premul_pixels;
    int width;
    int height;
    int stride;
    int batch_depth;
    float tile_offset_x;
    float tile_offset_y;
    bool dirty;
    bool needs_sync_from_target;
};

struct RdtPath {
    CGMutablePathRef cg;
};

typedef struct RdtSvgShared {
    Input* input;
    Pool* pool;
    Element* svg_root;
    int ref_count;
} RdtSvgShared;

struct RdtPicture {
    enum Kind { KIND_RASTER_IMAGE, KIND_SVG_DOM, KIND_TVG_PAINT };
    Kind kind;
    CGImageRef image;
    Tvg_Paint paint;
    RdtSvgShared* svg;
    char* source_path;
    float width;
    float height;
    RdtMatrix transform;
    bool has_transform;
};

static pthread_mutex_t g_svg_shared_mutex = PTHREAD_MUTEX_INITIALIZER;
static FontContext* g_picture_font_ctx = nullptr;

typedef struct CGClipEntry {
    CGPathRef path;
} CGClipEntry;

static const int RDT_CG_MAX_CLIP_DEPTH = 8;
static thread_local CGClipEntry s_cg_clip_stack[RDT_CG_MAX_CLIP_DEPTH];
static thread_local int s_cg_clip_depth = 0;

// ============================================================================
// Helpers
// ============================================================================

static inline CGAffineTransform cg_affine_from_rdt(const RdtMatrix* m) {
    // CGAffineTransform: [a b c d tx ty]
    // maps: x' = a*x + c*y + tx,  y' = b*x + d*y + ty
    // RdtMatrix row-major: e11 e12 e13 / e21 e22 e23
    // x' = e11*x + e12*y + e13,  y' = e21*x + e22*y + e23
    return CGAffineTransformMake(m->e11, m->e21, m->e12, m->e22, m->e13, m->e23);
}

static inline Tvg_Matrix cg_tvg_matrix_from_rdt(const RdtMatrix* m) {
    Tvg_Matrix tm = { m->e11, m->e12, m->e13,
                      m->e21, m->e22, m->e23,
                      m->e31, m->e32, m->e33 };
    return tm;
}

static inline void cg_apply_tile_offset(RdtVectorImpl* cg) {
    if (!cg) return;
    if (cg->tile_offset_x != 0.0f || cg->tile_offset_y != 0.0f) {
        CGContextTranslateCTM(cg->ctx, -cg->tile_offset_x, -cg->tile_offset_y);
    }
}

static void cg_apply_active_clips(RdtVectorImpl* cg) {
    if (!cg || s_cg_clip_depth <= 0) return;
    for (int i = 0; i < s_cg_clip_depth; i++) {
        if (!s_cg_clip_stack[i].path) continue;
        CGContextAddPath(cg->ctx, s_cg_clip_stack[i].path);
        CGContextClip(cg->ctx);
    }
}

static void cg_free_clip_range(int begin_depth, int end_depth) {
    if (begin_depth < 0) begin_depth = 0;
    if (end_depth > RDT_CG_MAX_CLIP_DEPTH) end_depth = RDT_CG_MAX_CLIP_DEPTH;
    for (int i = begin_depth; i < end_depth; i++) {
        if (s_cg_clip_stack[i].path) {
            CGPathRelease(s_cg_clip_stack[i].path);
            s_cg_clip_stack[i].path = nullptr;
        }
    }
}

static inline uint8_t cg_premul_channel(uint8_t channel, uint8_t alpha) {
    return (uint8_t)(((uint32_t)channel * alpha + 127) / 255);
}

static inline uint8_t cg_unpremul_channel(uint8_t channel, uint8_t alpha) {
    if (alpha == 0) return 0;
    if (alpha == 255) return channel;
    uint32_t value = ((uint32_t)channel * 255 + (alpha / 2)) / alpha;
    return (uint8_t)(value > 255 ? 255 : value);
}

static void cg_sync_from_target(RdtVectorImpl* cg) {
    if (!cg || !cg->pixels || !cg->premul_pixels) return;
    for (int y = 0; y < cg->height; y++) {
        uint32_t* dst = cg->premul_pixels + y * cg->stride;
        uint32_t* src = cg->pixels + y * cg->stride;
        for (int x = 0; x < cg->width; x++) {
            uint32_t p = src[x];
            uint8_t a = (p >> 24) & 0xff;
            uint8_t b = (p >> 16) & 0xff;
            uint8_t g = (p >> 8) & 0xff;
            uint8_t r = p & 0xff;
            dst[x] = ((uint32_t)a << 24) |
                     ((uint32_t)cg_premul_channel(b, a) << 16) |
                     ((uint32_t)cg_premul_channel(g, a) << 8) |
                     (uint32_t)cg_premul_channel(r, a);
        }
    }
    cg->needs_sync_from_target = false;
}

static void cg_flush_to_target(RdtVectorImpl* cg) {
    if (!cg || !cg->dirty || !cg->pixels || !cg->premul_pixels) return;
    if (cg->ctx) CGContextFlush(cg->ctx);
    for (int y = 0; y < cg->height; y++) {
        uint32_t* dst = cg->pixels + y * cg->stride;
        uint32_t* src = cg->premul_pixels + y * cg->stride;
        for (int x = 0; x < cg->width; x++) {
            uint32_t p = src[x];
            uint8_t a = (p >> 24) & 0xff;
            uint8_t b = (p >> 16) & 0xff;
            uint8_t g = (p >> 8) & 0xff;
            uint8_t r = p & 0xff;
            dst[x] = ((uint32_t)a << 24) |
                     ((uint32_t)cg_unpremul_channel(b, a) << 16) |
                     ((uint32_t)cg_unpremul_channel(g, a) << 8) |
                     (uint32_t)cg_unpremul_channel(r, a);
        }
    }
    cg->dirty = false;
    cg->needs_sync_from_target = true;
}

static bool cg_create_bitmap_context(RdtVectorImpl* cg) {
    if (!cg || !cg->colorspace || !cg->pixels || cg->width <= 0 ||
        cg->height <= 0 || cg->stride <= 0) {
        return false;
    }
    size_t bytes = (size_t)cg->stride * (size_t)cg->height * sizeof(uint32_t);
    cg->premul_pixels = (uint32_t*)calloc(1, bytes);
    if (!cg->premul_pixels) return false;
    cg->needs_sync_from_target = true;
    cg_sync_from_target(cg);
    cg->ctx = CGBitmapContextCreate(
        cg->premul_pixels,
        cg->width, cg->height,
        8,
        cg->stride * 4,
        cg->colorspace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
    );
    if (!cg->ctx) {
        free(cg->premul_pixels);
        cg->premul_pixels = nullptr;
        return false;
    }
    CGContextSetShouldAntialias(cg->ctx, true);
    CGContextSetAllowsAntialiasing(cg->ctx, true);
    CGContextTranslateCTM(cg->ctx, 0, cg->height);
    CGContextScaleCTM(cg->ctx, 1.0, -1.0);
    return true;
}

static void cg_destroy_bitmap_context(RdtVectorImpl* cg) {
    if (!cg) return;
    if (cg->ctx) {
        CGContextRelease(cg->ctx);
        cg->ctx = nullptr;
    }
    if (cg->premul_pixels) {
        free(cg->premul_pixels);
        cg->premul_pixels = nullptr;
    }
    cg->dirty = false;
    cg->needs_sync_from_target = true;
}

static void cg_mark_dirty(RdtVectorImpl* cg) {
    if (!cg) return;
    cg->dirty = true;
    if (cg->batch_depth <= 0) {
        cg_flush_to_target(cg);
    }
}

static void cg_begin_draw_state(RdtVectorImpl* cg) {
    if (!cg) return;
    if (cg->needs_sync_from_target) {
        cg_sync_from_target(cg);
    }
    CGContextSaveGState(cg->ctx);
    cg_apply_tile_offset(cg);
    cg_apply_active_clips(cg);
}

static void cg_end_draw_state(RdtVectorImpl* cg) {
    if (!cg) return;
    CGContextRestoreGState(cg->ctx);
    cg_mark_dirty(cg);
}

static uint32_t* cg_copy_premul_image_data(const uint32_t* pixels, int src_w,
                                           int src_h, int src_stride) {
    if (!pixels || src_w <= 0 || src_h <= 0 || src_stride <= 0) return nullptr;
    uint32_t* copy = (uint32_t*)malloc((size_t)src_w * (size_t)src_h * sizeof(uint32_t));
    if (!copy) return nullptr;
    for (int y = 0; y < src_h; y++) {
        const uint32_t* src = pixels + y * src_stride;
        uint32_t* dst = copy + y * src_w;
        for (int x = 0; x < src_w; x++) {
            uint32_t p = src[x];
            uint8_t a = (p >> 24) & 0xff;
            uint8_t b = (p >> 16) & 0xff;
            uint8_t g = (p >> 8) & 0xff;
            uint8_t r = p & 0xff;
            dst[x] = ((uint32_t)a << 24) |
                     ((uint32_t)cg_premul_channel(b, a) << 16) |
                     ((uint32_t)cg_premul_channel(g, a) << 8) |
                     (uint32_t)cg_premul_channel(r, a);
        }
    }
    return copy;
}

// Core Graphics only draws to premultiplied-alpha bitmap contexts. Radiant's
// public surface stays straight-alpha ABGR; this backend keeps a private
// premultiplied backing surface and converts at vector flush boundaries.

// ============================================================================
// Lifecycle
// ============================================================================

static const RdtVectorCaps g_cg_caps = {
    RDT_VECTOR_BACKEND_CORE_GRAPHICS,
    "CoreGraphics",
    true,   // vector_paths
    true,   // rounded_rects
    true,   // gradients
    true,   // nested_clips
    true,   // image_scaling
    true,   // picture_svg
    true,   // picture_duplication
    true,   // svg_dom_pictures
    false,  // opacity_group
    false,  // blend_modes
    true,   // gaussian_blur
    false,  // color_matrix_filters
    false,  // native_text_runs
    true,   // vector_batching
    false,  // premultiplied_surface
    true,   // tile_offsets
    true,   // clip_depth_save_restore
};

void rdt_vector_init(RdtVector* vec, uint32_t* pixels, int w, int h, int stride) {
    RdtVectorImpl* cg = (RdtVectorImpl*)calloc(1, sizeof(RdtVectorImpl));
    cg->colorspace = CGColorSpaceCreateDeviceRGB();
    cg->pixels = pixels;
    cg->width = w;
    cg->height = h;
    cg->stride = stride;

    if (!cg_create_bitmap_context(cg)) {
        log_error("rdt_vector_init: CGBitmapContextCreate failed for %dx%d", w, h);
        if (cg->colorspace) CGColorSpaceRelease(cg->colorspace);
        free(cg);
        vec->impl = nullptr;
        return;
    }

    vec->impl = cg;
    log_debug("rdt_vector_init: CG backend ready %dx%d stride=%d", w, h, stride);
}

void rdt_vector_destroy(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* cg = vec->impl;
    cg_flush_to_target(cg);
    cg_free_clip_range(0, s_cg_clip_depth);
    s_cg_clip_depth = 0;
    cg_destroy_bitmap_context(cg);
    if (cg->colorspace) CGColorSpaceRelease(cg->colorspace);
    free(cg);
    vec->impl = nullptr;
}

void rdt_vector_set_target(RdtVector* vec, uint32_t* pixels, int w, int h, int stride) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* cg = vec->impl;
    cg_flush_to_target(cg);

    // if dimensions changed, recreate context
    if (cg->width != w || cg->height != h || cg->stride != stride) {
        cg_destroy_bitmap_context(cg);
        cg->pixels = pixels;
        cg->width = w;
        cg->height = h;
        cg->stride = stride;
        if (!cg_create_bitmap_context(cg)) {
            log_error("rdt_vector_set_target: CGBitmapContextCreate failed");
            return;
        }
    } else if (cg->pixels != pixels) {
        // same size, different pointer — recreate
        cg_destroy_bitmap_context(cg);
        cg->pixels = pixels;
        if (!cg_create_bitmap_context(cg)) {
            log_error("rdt_vector_set_target: CGBitmapContextCreate failed");
            return;
        }
    } else {
        cg->needs_sync_from_target = true;
    }
}

void rdt_vector_set_tile_offset_y(RdtVector* vec, float offset_y) {
    if (!vec || !vec->impl) return;
    vec->impl->tile_offset_y = offset_y;
}

void rdt_vector_set_tile_offset_x(RdtVector* vec, float offset_x) {
    if (!vec || !vec->impl) return;
    vec->impl->tile_offset_x = offset_x;
}

const RdtVectorCaps* rdt_vector_get_caps(const RdtVector* vec) {
    (void)vec;
    return &g_cg_caps;
}

bool rdt_vector_get_target(const RdtVector* vec, RdtVectorTarget* out) {
    if (!vec || !vec->impl || !out) return false;
    out->pixels = vec->impl->pixels;
    out->width = vec->impl->width;
    out->height = vec->impl->height;
    out->stride = vec->impl->stride;
    out->tile_offset_x = vec->impl->tile_offset_x;
    out->tile_offset_y = vec->impl->tile_offset_y;
    return out->pixels && out->width > 0 && out->height > 0 && out->stride > 0;
}

void rdt_vector_begin_batch(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    vec->impl->batch_depth++;
}

void rdt_vector_flush_batch(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    cg_flush_to_target(vec->impl);
}

void rdt_vector_end_batch(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* cg = vec->impl;
    if (cg->batch_depth > 0) cg->batch_depth--;
    if (cg->batch_depth <= 0) {
        cg_flush_to_target(cg);
    }
}

// ============================================================================
// Path construction
// ============================================================================

RdtPath* rdt_path_new(void) {
    RdtPath* p = (RdtPath*)calloc(1, sizeof(RdtPath));
    p->cg = CGPathCreateMutable();
    return p;
}

void rdt_path_move_to(RdtPath* p, float x, float y) {
    CGPathMoveToPoint(p->cg, NULL, x, y);
}

void rdt_path_line_to(RdtPath* p, float x, float y) {
    CGPathAddLineToPoint(p->cg, NULL, x, y);
}

void rdt_path_cubic_to(RdtPath* p, float cx1, float cy1,
                       float cx2, float cy2, float x, float y) {
    CGPathAddCurveToPoint(p->cg, NULL, cx1, cy1, cx2, cy2, x, y);
}

void rdt_path_close(RdtPath* p) {
    CGPathCloseSubpath(p->cg);
}

void rdt_path_add_rect(RdtPath* p, float x, float y, float w, float h,
                       float rx, float ry) {
    if (rx <= 0 && ry <= 0) {
        CGPathAddRect(p->cg, NULL, CGRectMake(x, y, w, h));
    } else {
        // rounded rectangle via CGPath
        CGPathAddRoundedRect(p->cg, NULL, CGRectMake(x, y, w, h), rx, ry);
    }
}

void rdt_path_add_circle(RdtPath* p, float cx, float cy, float rx, float ry) {
    if (rx == ry) {
        // true circle
        CGPathAddEllipseInRect(p->cg, NULL, CGRectMake(cx - rx, cy - ry, rx * 2, ry * 2));
    } else {
        // ellipse
        CGPathAddEllipseInRect(p->cg, NULL, CGRectMake(cx - rx, cy - ry, rx * 2, ry * 2));
    }
}

void rdt_path_free(RdtPath* p) {
    if (!p) return;
    if (p->cg) CGPathRelease(p->cg);
    free(p);
}

RdtPath* rdt_path_clone(const RdtPath* src) {
    if (!src) return nullptr;
    RdtPath* dst = (RdtPath*)calloc(1, sizeof(RdtPath));
    if (!dst) return nullptr;
    dst->cg = src->cg ? CGPathCreateMutableCopy(src->cg) : CGPathCreateMutable();
    return dst;
}

bool rdt_path_get_bounds(const RdtPath* p, float* left, float* top,
                         float* right, float* bottom) {
    if (!p || !p->cg || !left || !top || !right || !bottom) return false;
    if (CGPathIsEmpty(p->cg)) return false;
    CGRect box = CGPathGetBoundingBox(p->cg);
    if (CGRectIsNull(box) || CGRectIsEmpty(box)) return false;
    *left = (float)CGRectGetMinX(box);
    *top = (float)CGRectGetMinY(box);
    *right = (float)CGRectGetMaxX(box);
    *bottom = (float)CGRectGetMaxY(box);
    return true;
}

typedef struct CGPathVisitContext {
    RdtPathVisitFn fn;
    void* context;
    bool ok;
} CGPathVisitContext;

static void cg_path_visit_apply(void* info, const CGPathElement* element) {
    CGPathVisitContext* visit = (CGPathVisitContext*)info;
    if (!visit || !visit->ok || !visit->fn || !element) return;

    float args[6] = {};
    RdtPathCommand command = RDT_PATH_MOVE;
    int arg_count = 0;

    switch (element->type) {
    case kCGPathElementMoveToPoint:
        command = RDT_PATH_MOVE;
        arg_count = 2;
        args[0] = (float)element->points[0].x;
        args[1] = (float)element->points[0].y;
        break;
    case kCGPathElementAddLineToPoint:
        command = RDT_PATH_LINE;
        arg_count = 2;
        args[0] = (float)element->points[0].x;
        args[1] = (float)element->points[0].y;
        break;
    case kCGPathElementAddQuadCurveToPoint:
        command = RDT_PATH_QUAD;
        arg_count = 4;
        args[0] = (float)element->points[0].x;
        args[1] = (float)element->points[0].y;
        args[2] = (float)element->points[1].x;
        args[3] = (float)element->points[1].y;
        break;
    case kCGPathElementAddCurveToPoint:
        command = RDT_PATH_CUBIC;
        arg_count = 6;
        args[0] = (float)element->points[0].x;
        args[1] = (float)element->points[0].y;
        args[2] = (float)element->points[1].x;
        args[3] = (float)element->points[1].y;
        args[4] = (float)element->points[2].x;
        args[5] = (float)element->points[2].y;
        break;
    case kCGPathElementCloseSubpath:
        command = RDT_PATH_CLOSE;
        arg_count = 0;
        break;
    }

    visit->ok = visit->fn(visit->context, command, args, arg_count);
}

bool rdt_path_visit(const RdtPath* p, RdtPathVisitFn fn, void* context) {
    if (!p || !p->cg || !fn) return false;
    CGPathVisitContext visit = {};
    visit.fn = fn;
    visit.context = context;
    visit.ok = true;
    CGPathApply(p->cg, &visit, cg_path_visit_apply);
    return visit.ok;
}

// ============================================================================
// Fill
// ============================================================================

void rdt_fill_path(RdtVector* vec, RdtPath* p, Color color,
                   RdtFillRule rule, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p) return;
    RdtVectorImpl* cg = vec->impl;

    cg_begin_draw_state(cg);

    if (transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(transform));
    }

    CGContextSetRGBFillColor(cg->ctx,
        color.r / 255.0, color.g / 255.0,
        color.b / 255.0, color.a / 255.0);

    CGContextAddPath(cg->ctx, p->cg);

    if (rule == RDT_FILL_EVEN_ODD) {
        CGContextEOFillPath(cg->ctx);
    } else {
        CGContextFillPath(cg->ctx);
    }

    cg_end_draw_state(cg);
}

void rdt_fill_rect(RdtVector* vec, float x, float y, float w, float h,
                   Color color) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* cg = vec->impl;

    cg_begin_draw_state(cg);
    CGContextSetRGBFillColor(cg->ctx,
        color.r / 255.0, color.g / 255.0,
        color.b / 255.0, color.a / 255.0);
    CGContextFillRect(cg->ctx, CGRectMake(x, y, w, h));
    cg_end_draw_state(cg);
}

void rdt_fill_rounded_rect(RdtVector* vec, float x, float y, float w, float h,
                           float rx, float ry, Color color) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* cg = vec->impl;

    cg_begin_draw_state(cg);
    CGContextSetRGBFillColor(cg->ctx,
        color.r / 255.0, color.g / 255.0,
        color.b / 255.0, color.a / 255.0);

    if (rx <= 0 && ry <= 0) {
        CGContextFillRect(cg->ctx, CGRectMake(x, y, w, h));
    } else {
        CGMutablePathRef path = CGPathCreateMutable();
        CGPathAddRoundedRect(path, NULL, CGRectMake(x, y, w, h), rx, ry);
        CGContextAddPath(cg->ctx, path);
        CGContextFillPath(cg->ctx);
        CGPathRelease(path);
    }

    cg_end_draw_state(cg);
}

// ============================================================================
// Stroke
// ============================================================================

void rdt_stroke_path(RdtVector* vec, RdtPath* p, Color color, float width,
                     RdtStrokeCap cap, RdtStrokeJoin join,
                     const float* dash_array, int dash_count, float dash_phase,
                     const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p) return;
    RdtVectorImpl* cg = vec->impl;

    cg_begin_draw_state(cg);

    if (transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(transform));
    }

    CGContextSetRGBStrokeColor(cg->ctx,
        color.r / 255.0, color.g / 255.0,
        color.b / 255.0, color.a / 255.0);
    CGContextSetLineWidth(cg->ctx, width);

    // line cap
    CGLineCap cg_cap;
    switch (cap) {
        case RDT_CAP_ROUND:  cg_cap = kCGLineCapRound; break;
        case RDT_CAP_SQUARE: cg_cap = kCGLineCapSquare; break;
        default:             cg_cap = kCGLineCapButt; break;
    }
    CGContextSetLineCap(cg->ctx, cg_cap);

    // line join
    CGLineJoin cg_join;
    switch (join) {
        case RDT_JOIN_ROUND: cg_join = kCGLineJoinRound; break;
        case RDT_JOIN_BEVEL: cg_join = kCGLineJoinBevel; break;
        default:             cg_join = kCGLineJoinMiter; break;
    }
    CGContextSetLineJoin(cg->ctx, cg_join);

    // dash pattern
    if (dash_array && dash_count > 0) {
        // convert float to CGFloat
        CGFloat cg_dash[16];
        int n = dash_count > 16 ? 16 : dash_count;
        for (int i = 0; i < n; i++) cg_dash[i] = dash_array[i];
        CGContextSetLineDash(cg->ctx, (CGFloat)dash_phase, cg_dash, n);
    }

    CGContextAddPath(cg->ctx, p->cg);
    CGContextStrokePath(cg->ctx);

    cg_end_draw_state(cg);
}

// ============================================================================
// Gradient fill
// ============================================================================

static CGGradientRef create_cg_gradient(CGColorSpaceRef colorspace,
                                         const RdtGradientStop* stops, int count) {
    CGFloat* components = (CGFloat*)alloca(count * 4 * sizeof(CGFloat));
    CGFloat* locations = (CGFloat*)alloca(count * sizeof(CGFloat));
    for (int i = 0; i < count; i++) {
        components[i * 4 + 0] = stops[i].r / 255.0;
        components[i * 4 + 1] = stops[i].g / 255.0;
        components[i * 4 + 2] = stops[i].b / 255.0;
        components[i * 4 + 3] = stops[i].a / 255.0;
        locations[i] = stops[i].offset;
    }
    return CGGradientCreateWithColorComponents(colorspace, components, locations, count);
}

void rdt_fill_linear_gradient(RdtVector* vec, RdtPath* p,
                              float x1, float y1, float x2, float y2,
                              const RdtGradientStop* stops, int stop_count,
                              RdtFillRule rule,
                              const RdtMatrix* transform,
                              const RdtMatrix* gradient_transform) {
    if (!vec || !vec->impl || !p || !stops || stop_count < 2) return;
    RdtVectorImpl* cg = vec->impl;

    cg_begin_draw_state(cg);

    if (transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(transform));
    }

    // clip to path
    CGContextAddPath(cg->ctx, p->cg);
    if (rule == RDT_FILL_EVEN_ODD) {
        CGContextEOClip(cg->ctx);
    } else {
        CGContextClip(cg->ctx);
    }

    CGGradientRef gradient = create_cg_gradient(cg->colorspace, stops, stop_count);
    if (gradient_transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(gradient_transform));
    }
    CGContextDrawLinearGradient(cg->ctx, gradient,
        CGPointMake(x1, y1), CGPointMake(x2, y2),
        kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    CGGradientRelease(gradient);

    cg_end_draw_state(cg);
}

void rdt_fill_radial_gradient(RdtVector* vec, RdtPath* p,
                              float cx, float cy, float r,
                              const RdtGradientStop* stops, int stop_count,
                              RdtFillRule rule,
                              const RdtMatrix* transform,
                              const RdtMatrix* gradient_transform) {
    if (!vec || !vec->impl || !p || !stops || stop_count < 2) return;
    RdtVectorImpl* cg = vec->impl;

    cg_begin_draw_state(cg);

    if (transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(transform));
    }

    // clip to path
    CGContextAddPath(cg->ctx, p->cg);
    if (rule == RDT_FILL_EVEN_ODD) {
        CGContextEOClip(cg->ctx);
    } else {
        CGContextClip(cg->ctx);
    }

    CGGradientRef gradient = create_cg_gradient(cg->colorspace, stops, stop_count);
    if (gradient_transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(gradient_transform));
    }
    CGContextDrawRadialGradient(cg->ctx, gradient,
        CGPointMake(cx, cy), 0,    // start center + radius
        CGPointMake(cx, cy), r,    // end center + radius
        kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    CGGradientRelease(gradient);

    cg_end_draw_state(cg);
}

// ============================================================================
// Clipping
// ============================================================================

void rdt_push_clip(RdtVector* vec, RdtPath* clip_path, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !clip_path) return;
    if (s_cg_clip_depth >= RDT_CG_MAX_CLIP_DEPTH) {
        log_error("rdt_push_clip: Core Graphics clip stack overflow (depth %d)", s_cg_clip_depth);
        return;
    }
    CGPathRef copied_path = nullptr;
    if (transform) {
        CGAffineTransform t = cg_affine_from_rdt(transform);
        copied_path = CGPathCreateCopyByTransformingPath(clip_path->cg, &t);
    } else {
        copied_path = CGPathCreateMutableCopy(clip_path->cg);
    }
    if (!copied_path) return;
    s_cg_clip_stack[s_cg_clip_depth++].path = copied_path;
}

void rdt_pop_clip(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    if (s_cg_clip_depth <= 0) {
        log_error("rdt_pop_clip: Core Graphics clip stack underflow");
        return;
    }
    s_cg_clip_depth--;
    cg_free_clip_range(s_cg_clip_depth, s_cg_clip_depth + 1);
}

int rdt_clip_save_depth() {
    int saved = s_cg_clip_depth;
    s_cg_clip_depth = 0;
    return saved;
}

void rdt_clip_restore_depth(int saved_depth) {
    if (saved_depth < 0) saved_depth = 0;
    if (saved_depth > RDT_CG_MAX_CLIP_DEPTH) saved_depth = RDT_CG_MAX_CLIP_DEPTH;
    cg_free_clip_range(saved_depth, s_cg_clip_depth);
    s_cg_clip_depth = saved_depth;
}

// ============================================================================
// Image drawing
// ============================================================================

void rdt_draw_image(RdtVector* vec, const uint32_t* pixels, int src_w, int src_h,
                    int src_stride, float dst_x, float dst_y, float dst_w, float dst_h,
                    uint8_t opacity, const RdtMatrix* transform, uint64_t resource_generation) {
    (void)resource_generation;
    if (!vec || !vec->impl || !pixels) return;
    RdtVectorImpl* cg = vec->impl;

    cg_begin_draw_state(cg);

    if (transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(transform));
    }

    if (opacity < 255) {
        CGContextSetAlpha(cg->ctx, opacity / 255.0);
    }

    uint32_t* premul_src = cg_copy_premul_image_data(pixels, src_w, src_h, src_stride);
    if (!premul_src) {
        cg_end_draw_state(cg);
        return;
    }

    // create CGImage from premultiplied pixel data
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        NULL, premul_src, (size_t)src_w * (size_t)src_h * sizeof(uint32_t), NULL);
    CGImageRef image = CGImageCreate(
        src_w, src_h,
        8, 32, src_w * 4,
        cg->colorspace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big,
        provider,
        NULL, false, kCGRenderingIntentDefault);

    if (image) {
        // CG draws images upside down in our flipped context, so flip locally
        CGContextTranslateCTM(cg->ctx, dst_x, dst_y + dst_h);
        CGContextScaleCTM(cg->ctx, 1.0, -1.0);
        CGContextDrawImage(cg->ctx, CGRectMake(0, 0, dst_w, dst_h), image);
        CGImageRelease(image);
    }

    CGDataProviderRelease(provider);
    free(premul_src);
    cg_end_draw_state(cg);
}

// ============================================================================
// Picture (SVG / vector image files)
// ============================================================================

static bool cg_ascii_ends_with_svg(const char* path) {
    if (!path) return false;
    size_t len = strlen(path);
    if (len < 4) return false;
    const char* ext = path + len - 4;
    return (ext[0] == '.') &&
           (ext[1] == 's' || ext[1] == 'S') &&
           (ext[2] == 'v' || ext[2] == 'V') &&
           (ext[3] == 'g' || ext[3] == 'G');
}

static bool cg_mime_is_svg(const char* mime_type) {
    return mime_type && (strstr(mime_type, "svg") || strstr(mime_type, "xml"));
}

static RdtSvgShared* cg_svg_shared_new(Input* input, Pool* pool, Element* svg_root) {
    RdtSvgShared* shared = (RdtSvgShared*)mem_calloc(1, sizeof(RdtSvgShared), MEM_CAT_RENDER);
    if (!shared) return nullptr;
    shared->input = input;
    shared->pool = pool;
    shared->svg_root = svg_root;
    shared->ref_count = 1;
    return shared;
}

static void cg_svg_shared_retain(RdtSvgShared* shared) {
    if (!shared) return;
    pthread_mutex_lock(&g_svg_shared_mutex);
    shared->ref_count++;
    pthread_mutex_unlock(&g_svg_shared_mutex);
}

static void cg_svg_shared_release(RdtSvgShared* shared) {
    if (!shared) return;
    bool destroy = false;
    pthread_mutex_lock(&g_svg_shared_mutex);
    shared->ref_count--;
    if (shared->ref_count <= 0) destroy = true;
    pthread_mutex_unlock(&g_svg_shared_mutex);
    if (destroy) {
        if (shared->pool) pool_destroy(shared->pool);
        mem_free(shared);
    }
}

static RdtPicture* cg_svg_picture_create(const char* data, int size, const char* source_path) {
    if (!data || size <= 0) return nullptr;

    Pool* pool = mem_pool_create(NULL, MEM_ROLE_MEDIA, "rdt.vector.cg");
    if (!pool) {
        log_error("cg_svg_picture_create: pool_create failed");
        return nullptr;
    }
    Input* input = Input::create(pool, nullptr);
    if (!input) {
        pool_destroy(pool);
        return nullptr;
    }
    input->ui_mode = false;

    char* buf = (char*)mem_alloc((size_t)size + 1, MEM_CAT_RENDER);
    if (!buf) {
        pool_destroy(pool);
        return nullptr;
    }
    memcpy(buf, data, (size_t)size);
    buf[size] = '\0';
    Element* svg_root = html5_parse_svg_document(input, buf, nullptr);
    mem_free(buf);

    if (!input->root.item || input->root.item == ITEM_ERROR || !svg_root) {
        log_error("cg_svg_picture_create: failed to parse SVG picture");
        pool_destroy(pool);
        return nullptr;
    }

    SvgIntrinsicSize isz = calculate_svg_intrinsic_size(svg_root);
    float w = isz.width > 0 ? isz.width : 300.0f;
    float h = isz.height > 0 ? isz.height : 150.0f;

    RdtSvgShared* shared = cg_svg_shared_new(input, pool, svg_root);
    if (!shared) {
        pool_destroy(pool);
        return nullptr;
    }

    RdtPicture* pic = (RdtPicture*)calloc(1, sizeof(RdtPicture));
    if (!pic) {
        cg_svg_shared_release(shared);
        return nullptr;
    }
    pic->kind = RdtPicture::KIND_SVG_DOM;
    pic->svg = shared;
    pic->source_path = source_path ? mem_strdup(source_path, MEM_CAT_RENDER) : nullptr;
    pic->width = w;
    pic->height = h;
    return pic;
}

static char* cg_read_file_bytes(const char* path, int* out_size) {
    if (out_size) *out_size = 0;
    FILE* fp = fopen(path, "rb");
    if (!fp) return nullptr;
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsz <= 0) {
        fclose(fp);
        return nullptr;
    }
    char* buf = (char*)mem_alloc((size_t)fsz, MEM_CAT_RENDER);
    if (!buf) {
        fclose(fp);
        return nullptr;
    }
    size_t rd = fread(buf, 1, (size_t)fsz, fp);
    fclose(fp);
    if (rd == 0) {
        mem_free(buf);
        return nullptr;
    }
    if (out_size) *out_size = (int)rd;
    return buf;
}

static RdtPicture* cg_svg_picture_load_file(const char* path) {
    int size = 0;
    char* data = cg_read_file_bytes(path, &size);
    if (!data) return nullptr;
    RdtPicture* pic = cg_svg_picture_create(data, size, path);
    mem_free(data);
    return pic;
}

static const char* cg_picture_elem_attr(Element* element, const char* attr_name) {
    if (!element || !element->data || !attr_name) return nullptr;
    TypeElmt* elem_type = (TypeElmt*)element->type;
    if (!elem_type) return nullptr;
    TypeMap* map_type = (TypeMap*)elem_type;
    if (!map_type->shape) return nullptr;

    size_t attr_len = strlen(attr_name);
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->name->str &&
            field->name->length == attr_len &&
            strncmp(field->name->str, attr_name, attr_len) == 0 &&
            field->type && field->type->type_id == LMD_TYPE_STRING) {
            void* data = ((char*)element->data) + field->byte_offset;
            String* str_val = *(String**)data;
            return str_val ? str_val->chars : nullptr;
        }
        field = field->next;
    }
    return nullptr;
}

static Element* cg_picture_find_id_recursive(Element* elem, const char* id) {
    if (!elem || !id) return nullptr;
    const char* elem_id = cg_picture_elem_attr(elem, "id");
    if (elem_id && strcmp(elem_id, id) == 0) return elem;
    for (int64_t i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        if (get_type_id(child) != LMD_TYPE_ELEMENT) continue;
        Element* found = cg_picture_find_id_recursive(child.element, id);
        if (found) return found;
    }
    return nullptr;
}

RdtPicture* rdt_picture_load(const char* path) {
    if (!path) return nullptr;

    if (cg_ascii_ends_with_svg(path)) {
        RdtPicture* svg = cg_svg_picture_load_file(path);
        if (svg) return svg;
    }

    CGDataProviderRef provider = CGDataProviderCreateWithFilename(path);
    if (!provider) {
        log_error("rdt_picture_load: failed to open %s", path);
        return nullptr;
    }

    // try PNG first, then JPEG
    CGImageRef image = CGImageCreateWithPNGDataProvider(provider, NULL, false, kCGRenderingIntentDefault);
    if (!image) {
        image = CGImageCreateWithJPEGDataProvider(provider, NULL, false, kCGRenderingIntentDefault);
    }
    CGDataProviderRelease(provider);

    if (!image) {
        RdtPicture* svg = cg_svg_picture_load_file(path);
        if (svg) return svg;
        log_error("rdt_picture_load: unsupported format %s", path);
        return nullptr;
    }

    RdtPicture* pic = (RdtPicture*)calloc(1, sizeof(RdtPicture));
    pic->kind = RdtPicture::KIND_RASTER_IMAGE;
    pic->image = image;
    pic->width = (float)CGImageGetWidth(image);
    pic->height = (float)CGImageGetHeight(image);
    return pic;
}

RdtPicture* rdt_picture_load_data(const char* data, int size, const char* mime_type) {
    if (!data || size <= 0) return nullptr;

    if (cg_mime_is_svg(mime_type)) {
        RdtPicture* svg = cg_svg_picture_create(data, size, nullptr);
        if (svg) return svg;
    }

    CFDataRef cf_data = CFDataCreate(NULL, (const UInt8*)data, size);
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(cf_data);
    CFRelease(cf_data);

    CGImageRef image = nullptr;
    if (mime_type && strstr(mime_type, "png")) {
        image = CGImageCreateWithPNGDataProvider(provider, NULL, false, kCGRenderingIntentDefault);
    } else if (mime_type && (strstr(mime_type, "jpeg") || strstr(mime_type, "jpg"))) {
        image = CGImageCreateWithJPEGDataProvider(provider, NULL, false, kCGRenderingIntentDefault);
    } else {
        // try PNG, then JPEG
        image = CGImageCreateWithPNGDataProvider(provider, NULL, false, kCGRenderingIntentDefault);
        if (!image) {
            image = CGImageCreateWithJPEGDataProvider(provider, NULL, false, kCGRenderingIntentDefault);
        }
    }
    CGDataProviderRelease(provider);

    if (!image) {
        RdtPicture* svg = cg_svg_picture_create(data, size, nullptr);
        if (svg) return svg;
        log_error("rdt_picture_load_data: failed to decode image");
        return nullptr;
    }

    RdtPicture* pic = (RdtPicture*)calloc(1, sizeof(RdtPicture));
    pic->kind = RdtPicture::KIND_RASTER_IMAGE;
    pic->image = image;
    pic->width = (float)CGImageGetWidth(image);
    pic->height = (float)CGImageGetHeight(image);
    return pic;
}

void rdt_picture_get_size(RdtPicture* pic, float* w, float* h) {
    if (!pic) { if (w) *w = 0; if (h) *h = 0; return; }
    if (w) *w = pic->width;
    if (h) *h = pic->height;
}

void rdt_picture_set_size(RdtPicture* pic, float w, float h) {
    if (!pic) return;
    pic->width = w;
    pic->height = h;
}

RdtPicture* rdt_picture_dup(RdtPicture* pic) {
    if (!pic) return nullptr;
    RdtPicture* dup = (RdtPicture*)calloc(1, sizeof(RdtPicture));
    if (!dup) return nullptr;
    dup->kind = pic->kind;
    if (pic->kind == RdtPicture::KIND_SVG_DOM) {
        dup->svg = pic->svg;
        cg_svg_shared_retain(dup->svg);
        dup->source_path = pic->source_path ? mem_strdup(pic->source_path, MEM_CAT_RENDER) : nullptr;
    } else if (pic->kind == RdtPicture::KIND_TVG_PAINT) {
        if (!pic->paint) {
            free(dup);
            return nullptr;
        }
        dup->paint = tvg_paint_duplicate(pic->paint);
        if (!dup->paint) {
            free(dup);
            return nullptr;
        }
    } else {
        if (!pic->image) {
            free(dup);
            return nullptr;
        }
        dup->image = CGImageRetain(pic->image);
    }
    dup->width = pic->width;
    dup->height = pic->height;
    dup->transform = pic->transform;
    dup->has_transform = pic->has_transform;
    return dup;
}

Element* rdt_picture_get_svg_root(RdtPicture* pic) {
    if (!pic || pic->kind != RdtPicture::KIND_SVG_DOM || !pic->svg) return nullptr;
    return pic->svg->svg_root;
}

Element* rdt_picture_find_svg_element_by_id(RdtPicture* pic, const char* id) {
    if (!pic || pic->kind != RdtPicture::KIND_SVG_DOM || !pic->svg || !id || !*id) return nullptr;
    return cg_picture_find_id_recursive(pic->svg->svg_root, id);
}

Pool* rdt_picture_get_pool(RdtPicture* pic) {
    if (!pic || pic->kind != RdtPicture::KIND_SVG_DOM || !pic->svg) return nullptr;
    return pic->svg->pool;
}

const char* rdt_picture_get_source_path(RdtPicture* pic) {
    if (!pic || pic->kind != RdtPicture::KIND_SVG_DOM) return nullptr;
    return pic->source_path;
}

void rdt_picture_draw(RdtVector* vec, RdtPicture* pic,
                      uint8_t opacity, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !pic) return;

    if (pic->kind == RdtPicture::KIND_TVG_PAINT) {
        if (!pic->paint) return;
        RdtVectorImpl* cg = vec->impl;
        Tvg_Paint draw = tvg_paint_duplicate(pic->paint);
        if (!draw) return;
        RdtMatrix base = rdt_matrix_identity();
        if (pic->has_transform) base = pic->transform;
        if (transform) base = rdt_matrix_multiply(transform, &base);
        Tvg_Matrix tm = cg_tvg_matrix_from_rdt(&base);
        tvg_paint_set_transform(draw, &tm);
        if (opacity < 255) {
            tvg_paint_set_opacity(draw, opacity);
        }

        size_t count = (size_t)cg->stride * (size_t)cg->height;
        uint32_t* temp = (uint32_t*)calloc(count, sizeof(uint32_t));
        if (!temp) {
            tvg_paint_unref(draw, true);
            return;
        }
        Tvg_Canvas canvas = tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT);
        if (!canvas) {
            free(temp);
            tvg_paint_unref(draw, true);
            return;
        }
        if (tvg_swcanvas_set_target(canvas, temp, cg->stride, cg->width, cg->height,
                                    TVG_COLORSPACE_ABGR8888) == TVG_RESULT_SUCCESS) {
            tvg_canvas_push(canvas, draw);
            tvg_canvas_draw(canvas, true);
            tvg_canvas_sync(canvas);
            rdt_draw_image(vec, temp, cg->width, cg->height, cg->stride,
                           0, 0, (float)cg->width, (float)cg->height, 255,
                           nullptr, 0);
            tvg_canvas_remove(canvas, draw);
        } else {
            tvg_paint_unref(draw, true);
        }
        tvg_canvas_destroy(canvas);
        free(temp);
        return;
    }

    if (pic->kind == RdtPicture::KIND_SVG_DOM) {
        if (!pic->svg || !pic->svg->svg_root) return;
        RdtMatrix base = rdt_matrix_identity();
        if (pic->has_transform) base = pic->transform;
        if (transform) base = rdt_matrix_multiply(transform, &base);
        render_svg_to_vec_via_display_list(vec, pic->svg->svg_root, pic->width, pic->height,
                          pic->svg->pool, 1.0f, g_picture_font_ctx, &base,
                          nullptr, nullptr, pic->source_path,
                          (float)opacity / 255.0f);
        return;
    }

    if (!pic->image) return;
    RdtVectorImpl* cg = vec->impl;

    cg_begin_draw_state(cg);

    if (transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(transform));
    }
    if (pic->has_transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(&pic->transform));
    }

    if (opacity < 255) {
        CGContextSetAlpha(cg->ctx, opacity / 255.0);
    }

    // flip for image drawing in our flipped context
    CGContextTranslateCTM(cg->ctx, 0, pic->height);
    CGContextScaleCTM(cg->ctx, 1.0, -1.0);
    CGContextDrawImage(cg->ctx, CGRectMake(0, 0, pic->width, pic->height), pic->image);

    cg_end_draw_state(cg);
}

void rdt_picture_draw_dup(RdtVector* vec, RdtPicture* pic,
                          uint8_t opacity, const RdtMatrix* transform) {
    rdt_picture_draw(vec, pic, opacity, transform);
}

bool rdt_picture_get_transform(RdtPicture* pic, RdtMatrix* out) {
    if (!pic || !out || !pic->has_transform) return false;
    *out = pic->transform;
    return true;
}

void rdt_picture_set_transform(RdtPicture* pic, const RdtMatrix* m) {
    if (!pic || !m) return;
    pic->transform = *m;
    pic->has_transform = true;
}

void rdt_picture_free(RdtPicture* pic) {
    if (!pic) return;
    if (pic->image) CGImageRelease(pic->image);
    if (pic->paint) tvg_paint_unref(pic->paint, true);
    if (pic->svg) cg_svg_shared_release(pic->svg);
    if (pic->source_path) mem_free(pic->source_path);
    free(pic);
}

void rdt_engine_init(int threads) {
    tvg_engine_init((unsigned)threads);
}

void rdt_engine_term(void) {
    tvg_engine_term();
}

void rdt_font_load(const char* font_path) {
    (void)font_path;
}

void rdt_set_font_context(FontContext* ctx) {
    g_picture_font_ctx = ctx;
}

RdtPicture* rdt_picture_take_tvg_paint(Tvg_Paint paint, float w, float h) {
    if (!paint) return nullptr;
    RdtPicture* pic = (RdtPicture*)calloc(1, sizeof(RdtPicture));
    if (!pic) {
        tvg_paint_unref(paint, true);
        return nullptr;
    }
    pic->kind = RdtPicture::KIND_TVG_PAINT;
    pic->paint = paint;
    pic->width = w;
    pic->height = h;
    return pic;
}

#endif // __APPLE__
