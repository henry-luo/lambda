#include "raster.h"
#include "mem.h"
#include <string.h>
#include <math.h>

RasterBuffer* raster_buffer_create(uint32_t width, uint32_t height, uint8_t channels,
        size_t byte_limit) {
    if (!width || !height || !channels || channels > 4 ||
            width > SIZE_MAX / height || (size_t)width * height > byte_limit / channels)
        return nullptr;
    RasterBuffer* buffer = (RasterBuffer*)mem_calloc(1, sizeof(RasterBuffer), MEM_CAT_CONTAINER);
    if (!buffer) return nullptr;
    buffer->size = (size_t)width * height * channels;
    buffer->pixels = (uint8_t*)mem_calloc(buffer->size, 1, MEM_CAT_CONTAINER);
    if (!buffer->pixels) { mem_free(buffer); return nullptr; }
    buffer->width = width;
    buffer->height = height;
    buffer->channels = channels;
    return buffer;
}

void raster_buffer_destroy(RasterBuffer* buffer) {
    if (!buffer) return;
    mem_free(buffer->pixels);
    mem_free(buffer);
}

bool raster_buffer_copy_region(const RasterBuffer* buffer, uint32_t left, uint32_t top,
        uint32_t width, uint32_t height, int channel, uint8_t* output, size_t capacity) {
    if (!buffer || !output || !width || !height || left > buffer->width ||
            top > buffer->height || width > buffer->width - left ||
            height > buffer->height - top || channel < -1 || channel >= buffer->channels)
        return false;
    size_t channels = channel < 0 ? buffer->channels : 1;
    if ((size_t)width * height > capacity / channels) return false;
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = buffer->pixels + ((size_t)(top + y) * buffer->width + left) * buffer->channels;
        if (channel < 0) memcpy(output + (size_t)y * width * channels, row, (size_t)width * channels);
        else for (uint32_t x = 0; x < width; x++) output[(size_t)y * width + x] = row[(size_t)x * buffer->channels + channel];
    }
    return true;
}

static double raster_linear(uint8_t sample) {
    double n = sample / 255.0;
    return n <= 0.04045 ? n / 12.92 : pow((n + 0.055) / 1.055, 2.4);
}

static uint8_t raster_encoded(double n) {
    n = n <= 0.0031308 ? 12.92 * n : 1.055 * pow(n, 1.0 / 2.4) - 0.055;
    return (uint8_t)lround(fmax(0.0, fmin(1.0, n)) * 255);
}

void raster_buffer_over(RasterBuffer* destination, const RasterBuffer* source,
        int64_t left, int64_t top, double opacity, const RasterBuffer* mask,
        int64_t mask_left, int64_t mask_top, double outside_coverage, bool invert_mask) {
    if (!destination || destination->channels != 4 || !source) return;
    int64_t x0 = left > 0 ? left : 0, y0 = top > 0 ? top : 0;
    int64_t x1 = left + source->width, y1 = top + source->height;
    if (x1 > destination->width) x1 = destination->width;
    if (y1 > destination->height) y1 = destination->height;
    for (int64_t y = y0; y < y1; y++) for (int64_t x = x0; x < x1; x++) {
        const uint8_t* s = source->pixels + ((size_t)(y - top) * source->width + x - left) * source->channels;
        uint8_t* d = destination->pixels + ((size_t)y * destination->width + x) * 4;
        double coverage = 1;
        if (mask) {
            int64_t mx = x - mask_left, my = y - mask_top;
            coverage = mx >= 0 && my >= 0 && mx < mask->width && my < mask->height
                ? mask->pixels[((size_t)my * mask->width + mx) * mask->channels] / 255.0 : outside_coverage;
            if (invert_mask) coverage = 1 - coverage;
        }
        bool alpha = source->channels == 2 || source->channels == 4;
        double sa = (alpha ? s[source->channels - 1] / 255.0 : 1) * opacity * coverage;
        double da = d[3] / 255.0, output_alpha = sa + da * (1 - sa);
        if (output_alpha <= 0) continue;
        // composite premultiplied linear-light color, then restore straight samples.
        for (uint8_t c = 0; c < 3; c++) {
            double sc = raster_linear(s[source->channels <= 2 ? 0 : c]);
            double dc = raster_linear(d[c]);
            d[c] = raster_encoded((sc * sa + dc * da * (1 - sa)) / output_alpha);
        }
        d[3] = (uint8_t)lround(output_alpha * 255);
    }
}
