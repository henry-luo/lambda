/**
 * Lambda Unified Font Module — CoreText Rasterization Backend (macOS)
 *
 * Provides glyph rasterization and per-glyph metric extraction using
 * CoreText/CoreGraphics.  Replaces FreeType's FT_Load_Glyph(FT_LOAD_RENDER)
 * and FT_Load_Glyph(metrics-only) paths on macOS.
 *
 * Three entry points:
 *   font_rasterize_ct_create()   — create CTFont from raw TTF/OTF data
 *   font_rasterize_ct_metrics()  — glyph bounding box + advance (no bitmap)
 *   font_rasterize_ct_render()   — rasterize glyph to 8-bit or BGRA bitmap
 *
 * Copyright (c) 2025–2026 Lambda Script Project
 */

#ifdef __APPLE__

#include "font_internal.h"
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <math.h>
#include <string.h>

// ============================================================================
// Create CTFont from raw font data (for web fonts / data URIs)
// ============================================================================

void* font_rasterize_ct_create(const uint8_t* data, size_t len, float size_px,
                                int face_index) {
    if (!data || len == 0 || size_px <= 0) return NULL;

    CFDataRef cf_data = CFDataCreate(NULL, data, (CFIndex)len);
    if (!cf_data) return NULL;

    CGDataProviderRef provider = CGDataProviderCreateWithCFData(cf_data);
    CFRelease(cf_data);
    if (!provider) return NULL;

    CGFontRef cg_font = CGFontCreateWithDataProvider(provider);
    CGDataProviderRelease(provider);
    if (!cg_font) {
        log_debug("font_rasterize_ct: CGFontCreateWithDataProvider failed");
        return NULL;
    }

    // for TTC collections, CoreGraphics loads just the first face from the
    // data provider; face_index > 0 is not supported via this path
    // (the caller should pass the sub-font offset for TTC)
    (void)face_index;

    CTFontRef ct_font = CTFontCreateWithGraphicsFont(cg_font, (CGFloat)size_px,
                                                      NULL, NULL);
    CGFontRelease(cg_font);

    if (!ct_font) {
        log_debug("font_rasterize_ct: CTFontCreateWithGraphicsFont failed");
        return NULL;
    }

    log_debug("font_rasterize_ct: created CTFont from raw data (%zu bytes, size=%.1f)",
              len, size_px);
    return (void*)ct_font;
}

// ============================================================================
// Get glyph metrics via CoreText (no bitmap)
// ============================================================================

bool font_rasterize_ct_metrics(void* ct_font_ref, uint32_t codepoint,
                                float bitmap_scale,
                                GlyphInfo* out) {
    if (!ct_font_ref || !out) return false;
    CTFontRef font = (CTFontRef)ct_font_ref;

    // encode codepoint as UTF-16
    UniChar utf16[2];
    CFIndex char_count;
    if (codepoint <= 0xFFFF) {
        utf16[0] = (UniChar)codepoint;
        char_count = 1;
    } else if (codepoint <= 0x10FFFF) {
        uint32_t cp = codepoint - 0x10000;
        utf16[0] = (UniChar)(0xD800 + (cp >> 10));
        utf16[1] = (UniChar)(0xDC00 + (cp & 0x3FF));
        char_count = 2;
    } else {
        return false;
    }

    CGGlyph glyphs[2] = {0, 0};
    bool found = CTFontGetGlyphsForCharacters(font, utf16, glyphs, char_count);
    if (!found || glyphs[0] == 0) return false;

    CGGlyph glyph_id = glyphs[0];

    // get advance
    CGSize advance = {0, 0};
    CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal,
                                &glyph_id, &advance, 1);

    // get bounding rect
    CGRect bbox;
    CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationHorizontal,
                                     &glyph_id, &bbox, 1);

    out->id        = (uint32_t)glyph_id;
    out->advance_x = (float)advance.width * bitmap_scale;
    out->advance_y = 0;  // horizontal text only
    out->bearing_x = (float)bbox.origin.x * bitmap_scale;
    out->bearing_y = (float)(bbox.origin.y + bbox.size.height) * bitmap_scale;
    out->width     = (int)ceilf((float)bbox.size.width * bitmap_scale);
    out->height    = (int)ceilf((float)bbox.size.height * bitmap_scale);
    out->is_color  = CTFontGetSymbolicTraits(font) & kCTFontTraitColorGlyphs;

    return true;
}

// ============================================================================
// Rasterize glyph to bitmap via CoreText/CoreGraphics
// ============================================================================

GlyphBitmap* font_rasterize_ct_render(void* ct_font_ref, uint32_t codepoint,
                                       GlyphRenderMode mode, float bitmap_scale,
                                       float pixel_ratio, Arena* arena) {
    if (!ct_font_ref || !arena) return NULL;
    CTFontRef font = (CTFontRef)ct_font_ref;

    // encode codepoint
    UniChar utf16[2];
    CFIndex char_count;
    if (codepoint <= 0xFFFF) {
        utf16[0] = (UniChar)codepoint;
        char_count = 1;
    } else if (codepoint <= 0x10FFFF) {
        uint32_t cp = codepoint - 0x10000;
        utf16[0] = (UniChar)(0xD800 + (cp >> 10));
        utf16[1] = (UniChar)(0xDC00 + (cp & 0x3FF));
        char_count = 2;
    } else {
        return NULL;
    }

    CGGlyph glyphs[2] = {0, 0};
    if (!CTFontGetGlyphsForCharacters(font, utf16, glyphs, char_count) || glyphs[0] == 0) {
        return NULL;
    }

    CGGlyph glyph_id = glyphs[0];

    // get bounding rect to determine bitmap dimensions
    CGRect bbox;
    CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationHorizontal,
                                     &glyph_id, &bbox, 1);

    // apply pixel_ratio for physical pixel rendering
    float render_scale = pixel_ratio;

    // Compute pixel-space bounding box using roundOut + outset(1,1), matching
    // Skia/Chrome: skBounds.roundOut(&mx.bounds); mx.bounds.outset(1, 1);
    // roundOut: floor for left/bottom, ceil for right/top → integer boundaries
    // outset(1,1): extend each edge by 1 pixel for AA bleed margins
    // This ensures the glyph baseline lands at an integer CG pixel position,
    // preventing sub-pixel AA bleed that causes round-bottom glyphs (e, c, o)
    // to appear 1px lower than flat-bottom glyphs.
    int px_left   = (int)floorf((float)bbox.origin.x * render_scale) - 1;
    int px_bottom = (int)floorf((float)bbox.origin.y * render_scale) - 1;
    int px_right  = (int)ceilf((float)(bbox.origin.x + bbox.size.width) * render_scale) + 1;
    int px_top    = (int)ceilf((float)(bbox.origin.y + bbox.size.height) * render_scale) + 1;

    int bmp_width  = px_right - px_left;
    int bmp_height = px_top - px_bottom;

    if (bmp_width <= 0 || bmp_height <= 0) {
        // zero-width glyph (e.g. space) — return empty bitmap
        GlyphBitmap* bmp = (GlyphBitmap*)arena_calloc(arena, sizeof(GlyphBitmap));
        if (!bmp) return NULL;
        bmp->mode = mode;
        bmp->pixel_mode = GLYPH_PIXEL_GRAY;
        bmp->bitmap_scale = bitmap_scale;
        return bmp;
    }

    // check if this is a color emoji glyph
    bool is_color = (CTFontGetSymbolicTraits(font) & kCTFontTraitColorGlyphs) != 0;

    int bytes_per_pixel;
    CGColorSpaceRef color_space;
    uint32_t bitmap_info;

    if (is_color) {
        // BGRA for color emoji
        bytes_per_pixel = 4;
        color_space = CGColorSpaceCreateDeviceRGB();
        bitmap_info = kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst;
    } else {
        // 8-bit grayscale for regular glyphs
        bytes_per_pixel = 1;
        color_space = CGColorSpaceCreateDeviceGray();
        bitmap_info = kCGImageAlphaNone;
    }

    int pitch = bmp_width * bytes_per_pixel;
    size_t buf_size = (size_t)(pitch * bmp_height);

    uint8_t* buffer = (uint8_t*)arena_alloc(arena, buf_size);
    if (!buffer) {
        CGColorSpaceRelease(color_space);
        return NULL;
    }
    memset(buffer, 0, buf_size);

    CGContextRef cg_ctx = CGBitmapContextCreate(buffer, (size_t)bmp_width, (size_t)bmp_height,
                                                 8, (size_t)pitch, color_space, bitmap_info);
    CGColorSpaceRelease(color_space);

    if (!cg_ctx) {
        log_debug("font_rasterize_ct: CGBitmapContextCreate failed (%dx%d)", bmp_width, bmp_height);
        return NULL;
    }

    // enable anti-aliasing and font smoothing for stroke thickening.
    // CoreGraphics font smoothing adds stroke thickening that matches browser
    // rendering weight.  However, CG also applies a gamma ~2.0 encoding to
    // coverage values in the grayscale context, making raw output appear
    // excessively bold.  We undo this gamma after drawing (see below) using
    // the same approach as Skia/Chrome: new_pixel = (pixel² + 128) / 255.
    // This preserves the thickened strokes while linearizing the tonal curve,
    // producing glyph weight within ~2% of Chrome/Safari.
    CGContextSetAllowsAntialiasing(cg_ctx, true);
    CGContextSetShouldAntialias(cg_ctx, true);
    CGContextSetAllowsFontSmoothing(cg_ctx, true);
    CGContextSetShouldSmoothFonts(cg_ctx, true);

    if (!is_color) {
        // for grayscale, set white foreground on black background
        CGContextSetGrayFillColor(cg_ctx, 1.0, 1.0);
    }

    // scale and position: CoreGraphics origin is bottom-left
    CGContextScaleCTM(cg_ctx, (CGFloat)render_scale, (CGFloat)render_scale);

    // position glyph origin at integer pixel coordinates (-px_left, -px_bottom)
    // to align the baseline to the pixel grid (matches Skia's roundOut approach)
    CGPoint position;
    position.x = (CGFloat)(-px_left) / render_scale;
    position.y = (CGFloat)(-px_bottom) / render_scale;

    CTFontDrawGlyphs(font, &glyph_id, &position, 1, cg_ctx);

    CGContextRelease(cg_ctx);

    // Linearize CG's gamma-encoded coverage (grayscale glyphs only).
    // CoreGraphics applies gamma ~2.0 to coverage values in offscreen contexts.
    // Undo with: new = (old * old + 128) / 255  (same formula as Skia's
    // gLinearCoverageFromCGLCDValue in SkScalerContext_mac_ct.cpp).
    // This preserves stroke thickening from font smoothing while removing the
    // gamma-induced weight excess — matching Chrome/Skia's rendering pipeline.
    if (!is_color) {
        for (int row = 0; row < bmp_height; row++) {
            uint8_t* p = buffer + row * pitch;
            for (int col = 0; col < bmp_width; col++) {
                uint8_t v = p[col];
                p[col] = (uint8_t)((v * v + 128) / 255);
            }
        }
    }

    // fill output bitmap
    GlyphBitmap* bmp = (GlyphBitmap*)arena_calloc(arena, sizeof(GlyphBitmap));
    if (!bmp) return NULL;

    bmp->buffer    = buffer;
    bmp->width     = (uint32_t)bmp_width;
    bmp->height    = (uint32_t)bmp_height;
    bmp->pitch     = pitch;
    bmp->bearing_x = px_left;   // = floor(origin.x * s) - 1
    bmp->bearing_y = px_top;    // = ceil((origin.y + height) * s) + 1
    bmp->bitmap_scale = bitmap_scale;
    bmp->mode      = mode;

    if (is_color) {
        bmp->pixel_mode = GLYPH_PIXEL_BGRA;
    } else if (mode == GLYPH_RENDER_MONO) {
        bmp->pixel_mode = GLYPH_PIXEL_MONO;
    } else {
        bmp->pixel_mode = GLYPH_PIXEL_GRAY;
    }

    return bmp;
}

#endif /* __APPLE__ */
