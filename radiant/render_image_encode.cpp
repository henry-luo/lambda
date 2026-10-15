#include "render.hpp"
#include "../lambda/io/image_io.h"
#include "../lib/base64.h"
#include "../lib/ownership.hpp"

static ImagePixelView render_surface_pixel_view(ImageSurface* surface) {
    if (!surface || !surface->pixels || surface->width <= 0 || surface->height <= 0 || surface->pitch <= 0) return {};
    uint32_t width = surface->decoded_width > 0 ? (uint32_t)surface->decoded_width : (uint32_t)surface->width;
    uint32_t height = surface->decoded_height > 0 ? (uint32_t)surface->decoded_height : (uint32_t)surface->height;
    return {(const uint8_t*)surface->pixels, (size_t)surface->pitch * height,
        (size_t)surface->pitch, width, height, 4, surface->alpha_mode != IMAGE_ALPHA_STRAIGHT};
}

// codec work and straight-alpha conversion are shared with every other image writer.
StrBuf* render_encode_surface_png(ImageSurface* surface) {
    ImagePixelView pixels = render_surface_pixel_view(surface);
    ImageEncodeOptions options; image_encode_options_init(&options, IMAGE_CODEC_PNG);
    ImageEncoded encoded = {}; ImageError error = {};
    if (image_encode_to_memory(NULL, &pixels, &options, &encoded, &error) != IMAGE_OK) {
        log_error("IMAGE_IO: encode surface PNG: %s", error.message); return nullptr;
    }
    StrBuf* output = strbuf_new_cap(encoded.length + 1);
    if (output) {
        memcpy(output->str, encoded.data, encoded.length); output->length = encoded.length;
        output->str[output->length] = 0;
    }
    image_free(encoded.data); return output;
}

ImageStatus render_save_surface_image(ImageSurface* surface, const char* path,
    const ImageEncodeOptions* options, ImageError* error) {
    ImagePixelView pixels = render_surface_pixel_view(surface);
    return image_io_save_path(NULL, path, &pixels, options, error);
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
