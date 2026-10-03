#include "render.hpp"
#include "../lib/base64.h"
#include "../lib/ownership.hpp"
#include <limits.h>
#include <png.h>
#include <setjmp.h>

// pixel encoding is independent of document loading and export-session ownership.
static void render_png_write_to_strbuf(png_structp png_ptr,
                                       png_bytep data, png_size_t length) {
    StrBuf* out = (StrBuf*)png_get_io_ptr(png_ptr);
    if (!out || !data || length == 0) return;
    if (!strbuf_ensure_cap(out, out->length + length + 1)) return;
    memcpy(out->str + out->length, data, length);
    out->length += length;
    out->str[out->length] = '\0';
}

StrBuf* render_encode_surface_png(ImageSurface* surface) {
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
    png_set_write_fn(png_ptr, png_bytes, render_png_write_to_strbuf, NULL);
    png_set_IHDR(png_ptr, info_ptr, surface->width, surface->height,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);
    png_bytep* rows = (png_bytep*)mem_alloc(
        sizeof(png_bytep) * surface->height, MEM_CAT_RENDER);
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

StrBuf* render_encode_surface_data_uri(ImageSurface* surface) {
    if (!surface || !surface->pixels) return nullptr;
    ImageSurface straight = *surface;
    lam::Temp<uint32_t> pixels;
    if (surface->alpha_mode != IMAGE_ALPHA_STRAIGHT) {
        if (surface->width <= 0 || surface->height <= 0 || surface->width > INT_MAX / 4) return nullptr;
        size_t pixel_count = 0, pixel_bytes = 0, allocation_bytes = 0;
        if (!math_checked_mul((size_t)surface->width, (size_t)surface->height, &pixel_count) ||
            !math_checked_mul(pixel_count, sizeof(uint32_t), &pixel_bytes) ||
            !math_checked_add(pixel_bytes, sizeof(ImageSurface), &allocation_bytes) ||
            !render_memory_allow_allocation(nullptr, allocation_bytes)) return nullptr;
        pixels = lam::temp_array<uint32_t>(pixel_count, MEM_CAT_IMAGE);
        // retain the surface allocator's critical-pressure retry for the private encoding copy.
        if (!pixels) {
            mem_context_request_reclaim(nullptr, MEM_PRESSURE_CRITICAL, allocation_bytes);
            if (render_memory_allow_allocation(nullptr, allocation_bytes)) {
                pixels = lam::temp_array<uint32_t>(pixel_count, MEM_CAT_IMAGE);
            }
        }
        if (!pixels) return nullptr;
        straight.pixels = pixels.get(); straight.pitch = surface->width * 4;
        // image encoders require straight channels; private replay surfaces contain premultiplied paint.
        for (int y = 0; y < surface->height; y++) {
            const uint32_t* src = (const uint32_t*)((uint8_t*)surface->pixels + (size_t)y * surface->pitch);
            uint32_t* dst = (uint32_t*)((uint8_t*)straight.pixels + (size_t)y * straight.pitch);
            for (int x = 0; x < surface->width; x++) dst[x] = render_pixel_unpremultiply_abgr(src[x]);
        }
        straight.alpha_mode = IMAGE_ALPHA_STRAIGHT;
    }
    StrBuf* png = render_encode_surface_png(&straight);
    if (!png) return nullptr;
    char* encoded = base64_encode_alloc(png->str, png->length, BASE64_STD);
    strbuf_free(png);
    if (!encoded) return nullptr;
    StrBuf* uri = strbuf_create("data:image/png;base64,");
    if (uri) strbuf_append_str(uri, encoded);
    mem_free(encoded);
    return uri;
}
