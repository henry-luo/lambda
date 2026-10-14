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
    // construct the row owner before setjmp so encoder failures release it normally.
    lam::Temp<uint32_t> straight_row;
    if (surface->alpha_mode != IMAGE_ALPHA_STRAIGHT) {
        size_t row_bytes = (size_t)surface->width * sizeof(uint32_t);
        if (!render_memory_allow_allocation(nullptr, row_bytes)) return nullptr;
        straight_row = lam::temp_array<uint32_t>(surface->width, MEM_CAT_IMAGE);
        if (!straight_row) return nullptr;
    }
    StrBuf* png_bytes = strbuf_new_cap((size_t)surface->width * (size_t)surface->height);
    if (!png_bytes) return nullptr;
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
    // png stores straight channels; use a private row so retained paint remains premultiplied.
    png_bytep row = (png_bytep)straight_row.get();
    for (int y = 0; y < surface->height; y++) {
        png_bytep source = (png_bytep)((uint8_t*)surface->pixels + (size_t)y * surface->pitch);
        if (row) render_pixel_unpremultiply_row((uint32_t*)row, (const uint32_t*)source, surface->width);
        png_write_row(png_ptr, row ? row : source);
    }
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return png_bytes;
}

StrBuf* render_encode_surface_data_uri(ImageSurface* surface) {
    if (!surface || !surface->pixels) return nullptr;
    StrBuf* png = render_encode_surface_png(surface);
    if (!png) return nullptr;
    lam::Temp<char> encoded(base64_encode_alloc(png->str, png->length, BASE64_STD));
    strbuf_free(png);
    if (!encoded) return nullptr;
    StrBuf* uri = strbuf_create("data:image/png;base64,");
    if (uri) strbuf_append_str(uri, encoded.get());
    return uri;
}
