/**
 * Lambda Unified Font Module - Platform backend facade
 *
 * Keeps the general font pipeline platform-independent.  Platform-specific
 * APIs are accessed through this small layer so loaders, glyph measurement,
 * and rendering do not grow separate Windows/macOS/Linux control flow.
 */

#include "font_internal.h"

bool font_backend_create(FontHandle* handle, const uint8_t* data, size_t len,
                         int face_index, FontWeight weight, FontSlant slant) {
    if (!handle) return false;
    handle->backend_kind = FONT_BACKEND_NONE;
    handle->platform_font_ref = NULL;
    handle->platform_aux_ref = NULL;

#ifdef __APPLE__
    if (data && len > 0) {
        handle->ct_raster_ref = font_rasterize_ct_create(data, len, handle->size_px,
                                                          face_index, weight, slant);
        handle->platform_font_ref = handle->ct_raster_ref;
        handle->backend_kind = handle->ct_raster_ref ? FONT_BACKEND_CORETEXT : FONT_BACKEND_NONE;
    }

    {
        const char* ps_name = NULL;
        const char* family = handle->family_name;
        if (handle->tables) {
            NameTable* name = font_tables_get_name(handle->tables);
            if (name) ps_name = name->postscript_name;
        }
        handle->ct_font_ref = font_platform_create_ct_font(ps_name, family,
                                                           handle->size_px, (int)weight);
        handle->platform_aux_ref = handle->ct_font_ref;
    }
    return handle->backend_kind != FONT_BACKEND_NONE;
#elif defined(LAMBDA_HAS_DWRITE)
    (void)weight; (void)slant;
    if (data && len > 0) {
        handle->platform_font_ref = font_backend_dwrite_create(data, len,
                                                               face_index,
                                                               handle->size_px);
        if (handle->platform_font_ref) {
            handle->backend_kind = FONT_BACKEND_DWRITE;
        }
    }
    if (handle->tables) {
        handle->tvg_raster_ctx = font_rasterize_tvg_create();
        if (!handle->platform_font_ref) {
            handle->platform_font_ref = handle->tvg_raster_ctx;
            handle->backend_kind = handle->tvg_raster_ctx ? FONT_BACKEND_TVG : FONT_BACKEND_NONE;
        }
    }
    return handle->backend_kind != FONT_BACKEND_NONE;
#else
    (void)data; (void)len; (void)face_index; (void)weight; (void)slant;
    if (handle->tables) {
        handle->tvg_raster_ctx = font_rasterize_tvg_create();
        handle->platform_font_ref = handle->tvg_raster_ctx;
        handle->backend_kind = handle->tvg_raster_ctx ? FONT_BACKEND_TVG : FONT_BACKEND_NONE;
    }
    return handle->backend_kind != FONT_BACKEND_NONE;
#endif
}

void font_backend_destroy(FontHandle* handle) {
    if (!handle) return;
#ifdef __APPLE__
    if (handle->ct_font_ref) {
        font_platform_destroy_ct_font(handle->ct_font_ref);
        handle->ct_font_ref = NULL;
    }
    if (handle->ct_raster_ref) {
        font_platform_destroy_ct_font(handle->ct_raster_ref);
        handle->ct_raster_ref = NULL;
    }
#else
#ifdef LAMBDA_HAS_DWRITE
    if (handle->backend_kind == FONT_BACKEND_DWRITE && handle->platform_font_ref) {
        font_backend_dwrite_destroy(handle->platform_font_ref);
    }
#endif
    if (handle->tvg_raster_ctx) {
        font_rasterize_tvg_destroy(handle->tvg_raster_ctx);
        handle->tvg_raster_ctx = NULL;
    }
#endif
    handle->platform_font_ref = NULL;
    handle->platform_aux_ref = NULL;
    handle->backend_kind = FONT_BACKEND_NONE;
}

bool font_backend_metrics(FontHandle* handle, uint32_t codepoint, GlyphInfo* out) {
    if (!handle || !out) return false;
#ifdef __APPLE__
    if (handle->ct_raster_ref) {
        return font_rasterize_ct_metrics(handle->ct_raster_ref, codepoint,
                                         handle->bitmap_scale, out);
    }
#else
#ifdef LAMBDA_HAS_DWRITE
    if (handle->backend_kind == FONT_BACKEND_DWRITE && handle->platform_font_ref) {
        if (font_backend_dwrite_metrics(handle->platform_font_ref, codepoint,
                                        handle->bitmap_scale, out)) {
            return true;
        }
    }
#endif
    if (handle->tvg_raster_ctx && handle->tables) {
        return font_rasterize_tvg_metrics(handle->tables, codepoint, handle->size_px,
                                          handle->bitmap_scale, out);
    }
#endif
    return false;
}

GlyphBitmap* font_backend_render(FontHandle* handle, uint32_t codepoint,
                                 GlyphRenderMode mode, Arena* arena) {
    if (!handle || !arena) return NULL;
    float pixel_ratio = (handle->ctx && handle->ctx->config.pixel_ratio > 0)
                            ? handle->ctx->config.pixel_ratio : 1.0f;
#ifdef __APPLE__
    if (handle->ct_raster_ref) {
        return font_rasterize_ct_render(handle->ct_raster_ref, codepoint, mode,
                                        handle->bitmap_scale, pixel_ratio, arena);
    }
#else
#ifdef LAMBDA_HAS_DWRITE
    if (handle->backend_kind == FONT_BACKEND_DWRITE && handle->platform_font_ref) {
        GlyphBitmap* dwrite_bmp = font_backend_dwrite_render(handle->platform_font_ref,
                                                             codepoint, mode,
                                                             handle->bitmap_scale,
                                                             pixel_ratio, arena);
        if (dwrite_bmp) return dwrite_bmp;
    }
#endif
    if (handle->tvg_raster_ctx && handle->tables) {
        float synth_bold_stroke = 0.0f;
        int req_w = (int)handle->weight;
        int act_w = (handle->actual_font_weight > 0) ? handle->actual_font_weight : req_w;
        if (req_w >= 600 && act_w < 550) {
            synth_bold_stroke = handle->size_px / 14.0f * pixel_ratio;
        }
        return font_rasterize_tvg_render(handle->tvg_raster_ctx, handle->tables,
                                         codepoint, handle->size_px,
                                         handle->bitmap_scale, pixel_ratio,
                                         synth_bold_stroke, arena);
    }
#endif
    return NULL;
}

float font_backend_glyph_advance(FontHandle* handle, uint32_t codepoint) {
    if (!handle) return -1.0f;
#ifdef __APPLE__
    if (handle->ct_font_ref) {
        return font_platform_get_glyph_advance(handle->ct_font_ref, codepoint);
    }
#endif
#ifdef LAMBDA_HAS_DWRITE
    if (handle->backend_kind == FONT_BACKEND_DWRITE && handle->platform_font_ref) {
        return font_backend_dwrite_glyph_advance(handle->platform_font_ref, codepoint,
                                                 handle->bitmap_scale);
    }
#else
    (void)codepoint;
#endif
    return -1.0f;
}
