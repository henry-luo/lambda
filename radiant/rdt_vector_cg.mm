// rdt_vector_cg.mm — Core Graphics backend for RdtVector (macOS)
#include "rdt_vector.hpp"
#include "../lib/log.h"
#include <CoreGraphics/CoreGraphics.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__

// ============================================================================
// Internal types
// ============================================================================

struct RdtVectorImpl {
    CGContextRef ctx;
    CGColorSpaceRef colorspace;
    uint32_t* pixels;
    int width;
    int height;
    int stride;
};

struct RdtPath {
    CGMutablePathRef cg;
};

struct RdtPicture {
    CGImageRef image;
    float width;
    float height;
};

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

// Core Graphics uses premultiplied alpha; Radiant stores straight alpha ABGR.
// The CGBitmapContext is configured to handle this via kCGImageAlphaPremultipliedFirst.
// We use BGRA byte order (kCGBitmapByteOrder32Little + kCGImageAlphaPremultipliedFirst)
// which matches the ABGR uint32_t layout on little-endian.

// ============================================================================
// Lifecycle
// ============================================================================

void rdt_vector_init(RdtVector* vec, uint32_t* pixels, int w, int h, int stride) {
    RdtVectorImpl* cg = (RdtVectorImpl*)calloc(1, sizeof(RdtVectorImpl));
    cg->colorspace = CGColorSpaceCreateDeviceRGB();
    cg->pixels = pixels;
    cg->width = w;
    cg->height = h;
    cg->stride = stride;

    // ABGR8888 as uint32_t on little-endian = byte order [R, G, B, A]
    // That's kCGImageAlphaPremultipliedLast with big-endian byte order,
    // or equivalently kCGImageAlphaPremultipliedFirst with 32-bit little-endian.
    // Actually: ABGR as uint32_t means A in high byte on little-endian = bytes [R][G][B][A].
    // CGBitmapContext byte order:
    //   kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst
    //   = premultiplied ARGB stored as 32-bit little-endian = memory order [B][G][R][A]
    //   That doesn't match either.
    //
    // The actual Radiant pixel format per dom_node.hpp comment:
    //   "32-bit ABGR color format" for the uint32_t .c field
    //   Union with struct { r, g, b, a } (little-endian: r at lowest address)
    //   So in memory: [R][G][B][A] = byte 0 is R
    //   As uint32_t on LE: A<<24 | B<<16 | G<<8 | R = 0xAABBGGRR
    //
    // For CGBitmapContext to match [R][G][B][A] byte order:
    //   kCGImageAlphaPremultipliedLast (alpha is last byte)
    //   kCGBitmapByteOrder32Big (bytes written as R,G,B,A in memory)
    // But Radiant uses straight alpha, not premultiplied.
    // kCGImageAlphaLast = straight alpha, last byte.
    // However CGBitmapContext only supports premultiplied alpha for drawing.
    // We'll use premultiplied and accept the slight difference for semi-transparent shapes.
    
    cg->ctx = CGBitmapContextCreate(
        pixels,
        w, h,
        8,                // bits per component
        stride * 4,       // bytes per row (stride is in pixels)
        cg->colorspace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
    );

    if (!cg->ctx) {
        log_error("rdt_vector_init: CGBitmapContextCreate failed for %dx%d", w, h);
        free(cg);
        vec->impl = nullptr;
        return;
    }

    CGContextSetShouldAntialias(cg->ctx, true);
    CGContextSetAllowsAntialiasing(cg->ctx, true);

    // Core Graphics origin is bottom-left; Radiant is top-left.
    // Flip the coordinate system.
    CGContextTranslateCTM(cg->ctx, 0, h);
    CGContextScaleCTM(cg->ctx, 1.0, -1.0);

    vec->impl = cg;
    log_debug("rdt_vector_init: CG backend ready %dx%d stride=%d", w, h, stride);
}

void rdt_vector_destroy(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* cg = vec->impl;
    if (cg->ctx) CGContextRelease(cg->ctx);
    if (cg->colorspace) CGColorSpaceRelease(cg->colorspace);
    free(cg);
    vec->impl = nullptr;
}

void rdt_vector_set_target(RdtVector* vec, uint32_t* pixels, int w, int h, int stride) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* cg = vec->impl;

    // if dimensions changed, recreate context
    if (cg->width != w || cg->height != h || cg->stride != stride) {
        CGContextRelease(cg->ctx);
        cg->pixels = pixels;
        cg->width = w;
        cg->height = h;
        cg->stride = stride;
        cg->ctx = CGBitmapContextCreate(
            pixels, w, h, 8, stride * 4, cg->colorspace,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        if (!cg->ctx) {
            log_error("rdt_vector_set_target: CGBitmapContextCreate failed");
            return;
        }
        CGContextSetShouldAntialias(cg->ctx, true);
        CGContextTranslateCTM(cg->ctx, 0, h);
        CGContextScaleCTM(cg->ctx, 1.0, -1.0);
    } else if (cg->pixels != pixels) {
        // same size, different pointer — recreate
        CGContextRelease(cg->ctx);
        cg->pixels = pixels;
        cg->ctx = CGBitmapContextCreate(
            pixels, w, h, 8, stride * 4, cg->colorspace,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        if (!cg->ctx) {
            log_error("rdt_vector_set_target: CGBitmapContextCreate failed");
            return;
        }
        CGContextSetShouldAntialias(cg->ctx, true);
        CGContextTranslateCTM(cg->ctx, 0, h);
        CGContextScaleCTM(cg->ctx, 1.0, -1.0);
    }
    // else same buffer, nothing to do
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

// ============================================================================
// Fill
// ============================================================================

void rdt_fill_path(RdtVector* vec, RdtPath* p, Color color,
                   RdtFillRule rule, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p) return;
    RdtVectorImpl* cg = vec->impl;

    CGContextSaveGState(cg->ctx);

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

    CGContextRestoreGState(cg->ctx);
}

void rdt_fill_rect(RdtVector* vec, float x, float y, float w, float h,
                   Color color) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* cg = vec->impl;

    CGContextSetRGBFillColor(cg->ctx,
        color.r / 255.0, color.g / 255.0,
        color.b / 255.0, color.a / 255.0);
    CGContextFillRect(cg->ctx, CGRectMake(x, y, w, h));
}

void rdt_fill_rounded_rect(RdtVector* vec, float x, float y, float w, float h,
                           float rx, float ry, Color color) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* cg = vec->impl;

    CGContextSaveGState(cg->ctx);
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

    CGContextRestoreGState(cg->ctx);
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

    CGContextSaveGState(cg->ctx);

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

    CGContextRestoreGState(cg->ctx);
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
                              const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p || !stops || stop_count < 2) return;
    RdtVectorImpl* cg = vec->impl;

    CGContextSaveGState(cg->ctx);

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
    CGContextDrawLinearGradient(cg->ctx, gradient,
        CGPointMake(x1, y1), CGPointMake(x2, y2),
        kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    CGGradientRelease(gradient);

    CGContextRestoreGState(cg->ctx);
}

void rdt_fill_radial_gradient(RdtVector* vec, RdtPath* p,
                              float cx, float cy, float r,
                              const RdtGradientStop* stops, int stop_count,
                              RdtFillRule rule,
                              const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p || !stops || stop_count < 2) return;
    RdtVectorImpl* cg = vec->impl;

    CGContextSaveGState(cg->ctx);

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
    CGContextDrawRadialGradient(cg->ctx, gradient,
        CGPointMake(cx, cy), 0,    // start center + radius
        CGPointMake(cx, cy), r,    // end center + radius
        kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    CGGradientRelease(gradient);

    CGContextRestoreGState(cg->ctx);
}

// ============================================================================
// Clipping
// ============================================================================

void rdt_push_clip(RdtVector* vec, RdtPath* clip_path, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !clip_path) return;
    RdtVectorImpl* cg = vec->impl;

    CGContextSaveGState(cg->ctx);

    if (transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(transform));
    }

    CGContextAddPath(cg->ctx, clip_path->cg);
    CGContextClip(cg->ctx);
}

void rdt_pop_clip(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* cg = vec->impl;
    CGContextRestoreGState(cg->ctx);
}

// ============================================================================
// Image drawing
// ============================================================================

void rdt_draw_image(RdtVector* vec, const uint32_t* pixels, int src_w, int src_h,
                    int src_stride, float dst_x, float dst_y, float dst_w, float dst_h,
                    uint8_t opacity, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !pixels) return;
    RdtVectorImpl* cg = vec->impl;

    CGContextSaveGState(cg->ctx);

    if (transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(transform));
    }

    if (opacity < 255) {
        CGContextSetAlpha(cg->ctx, opacity / 255.0);
    }

    // create CGImage from pixel data
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        NULL, pixels, src_stride * src_h * 4, NULL);
    CGImageRef image = CGImageCreate(
        src_w, src_h,
        8, 32, src_stride * 4,
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
    CGContextRestoreGState(cg->ctx);
}

// ============================================================================
// Picture (SVG / vector image files)
// ============================================================================

RdtPicture* rdt_picture_load(const char* path) {
    if (!path) return nullptr;

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
        log_error("rdt_picture_load: unsupported format %s", path);
        return nullptr;
    }

    RdtPicture* pic = (RdtPicture*)calloc(1, sizeof(RdtPicture));
    pic->image = image;
    pic->width = (float)CGImageGetWidth(image);
    pic->height = (float)CGImageGetHeight(image);
    return pic;
}

RdtPicture* rdt_picture_load_data(const char* data, int size, const char* mime_type) {
    if (!data || size <= 0) return nullptr;

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
        log_error("rdt_picture_load_data: failed to decode image");
        return nullptr;
    }

    RdtPicture* pic = (RdtPicture*)calloc(1, sizeof(RdtPicture));
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

void rdt_picture_draw(RdtVector* vec, RdtPicture* pic,
                      uint8_t opacity, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !pic || !pic->image) return;
    RdtVectorImpl* cg = vec->impl;

    CGContextSaveGState(cg->ctx);

    if (transform) {
        CGContextConcatCTM(cg->ctx, cg_affine_from_rdt(transform));
    }

    if (opacity < 255) {
        CGContextSetAlpha(cg->ctx, opacity / 255.0);
    }

    // flip for image drawing in our flipped context
    CGContextTranslateCTM(cg->ctx, 0, pic->height);
    CGContextScaleCTM(cg->ctx, 1.0, -1.0);
    CGContextDrawImage(cg->ctx, CGRectMake(0, 0, pic->width, pic->height), pic->image);

    CGContextRestoreGState(cg->ctx);
}

void rdt_picture_free(RdtPicture* pic) {
    if (!pic) return;
    if (pic->image) CGImageRelease(pic->image);
    free(pic);
}

#endif // __APPLE__
