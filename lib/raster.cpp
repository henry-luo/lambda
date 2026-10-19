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
    buffer->storage = byte_storage_alloc(buffer->size, MEM_CAT_CONTAINER);
    if (!buffer->storage) { mem_free(buffer); return nullptr; }
    buffer->pixels = buffer->storage->data;
    memset(buffer->pixels, 0, buffer->size);
    buffer->width = width;
    buffer->height = height;
    buffer->channels = channels;
    return buffer;
}

void raster_buffer_destroy(RasterBuffer* buffer) {
    if (!buffer) return;
    byte_storage_release(buffer->storage);
    for (size_t i = 0; buffer->tiles && i < (size_t)buffer->columns * buffer->rows; i++)
        byte_storage_release(buffer->tiles[i].storage);
    mem_free(buffer->tiles);
    mem_free(buffer);
}

RasterBuffer* raster_buffer_sparse(uint32_t width, uint32_t height, uint8_t channels, size_t limit) {
    if (!width || !height || !channels || channels > 4 || width > SIZE_MAX / height ||
            (size_t)width * height > limit / channels) return nullptr;
    RasterBuffer* result = (RasterBuffer*)mem_calloc(1, sizeof(RasterBuffer), MEM_CAT_CONTAINER);
    if (!result) return nullptr;
    result->width = width; result->height = height; result->channels = channels;
    result->size = (size_t)width * height * channels;
    result->columns = (width - 1) / RASTER_TILE_SIDE + 1;
    result->rows = (height - 1) / RASTER_TILE_SIDE + 1;
    result->tiles = (RasterTile*)mem_calloc((size_t)result->columns * result->rows, sizeof(RasterTile), MEM_CAT_CONTAINER);
    if (!result->tiles) { mem_free(result); return nullptr; }
    return result;
}

RasterBuffer* raster_buffer_fork(const RasterBuffer* source) {
    if (!source) return nullptr;
    RasterBuffer* result = raster_buffer_sparse(source->width, source->height, source->channels, source->size);
    if (!result) return nullptr;
    for (uint32_t y = 0; y < result->rows; y++) for (uint32_t x = 0; x < result->columns; x++) {
        RasterTile& tile = result->tiles[(size_t)y * result->columns + x];
        if (source->pixels) {
            tile.storage = byte_storage_retain(source->storage);
            tile.offset = ((size_t)y * RASTER_TILE_SIDE * source->width + x * RASTER_TILE_SIDE) * source->channels;
            tile.stride = (size_t)source->width * source->channels;
        } else {
            tile = source->tiles[(size_t)y * source->columns + x];
            byte_storage_retain(tile.storage);
        }
    }
    return result;
}

const uint8_t* raster_buffer_pixel(const RasterBuffer* buffer, uint32_t x, uint32_t y) {
    static const uint8_t zero[4] = {};
    if (!buffer || x >= buffer->width || y >= buffer->height) return zero;
    if (buffer->pixels) return buffer->pixels + ((size_t)y * buffer->width + x) * buffer->channels;
    const RasterTile& tile = buffer->tiles[(size_t)(y / RASTER_TILE_SIDE) * buffer->columns + x / RASTER_TILE_SIDE];
    return tile.storage ? tile.storage->data + tile.offset + (y % RASTER_TILE_SIDE) * tile.stride +
        (x % RASTER_TILE_SIDE) * buffer->channels : zero;
}

uint8_t* raster_buffer_edit_pixel(RasterBuffer* buffer, uint32_t x, uint32_t y) {
    if (!buffer || x >= buffer->width || y >= buffer->height) return nullptr;
    if (buffer->pixels) {
        if (byte_storage_ref_count(buffer->storage) != 1 || (buffer->storage->flags & BYTE_STORAGE_FLAG_READ_ONLY)) return nullptr;
        return buffer->pixels + ((size_t)y * buffer->width + x) * buffer->channels;
    }
    uint32_t column = x / RASTER_TILE_SIDE, row = y / RASTER_TILE_SIDE;
    uint32_t left = column * RASTER_TILE_SIDE, top = row * RASTER_TILE_SIDE;
    uint32_t width = buffer->width - left, height = buffer->height - top;
    if (width > RASTER_TILE_SIDE) width = RASTER_TILE_SIDE;
    if (height > RASTER_TILE_SIDE) height = RASTER_TILE_SIDE;
    RasterTile& tile = buffer->tiles[(size_t)row * buffer->columns + column];
    if (!tile.storage || byte_storage_ref_count(tile.storage) != 1 || tile.offset ||
            tile.stride != (size_t)width * buffer->channels || (tile.storage->flags & BYTE_STORAGE_FLAG_READ_ONLY)) {
        ByteStorage* replacement = byte_storage_alloc((size_t)width * height * buffer->channels, MEM_CAT_CONTAINER);
        if (!replacement) return nullptr;
        // old dense spans and untouched tiles stay shared; only this tile detaches.
        if (tile.storage) for (uint32_t v = 0; v < height; v++)
            memcpy(replacement->data + (size_t)v * width * buffer->channels,
                tile.storage->data + tile.offset + v * tile.stride, (size_t)width * buffer->channels);
        else memset(replacement->data, 0, replacement->capacity);
        byte_storage_release(tile.storage);
        tile = {replacement, 0, (size_t)width * buffer->channels};
    }
    return tile.storage->data + (y - top) * tile.stride + (x - left) * buffer->channels;
}

size_t raster_buffer_index_bytes(const RasterBuffer* buffer) {
    return sizeof(RasterBuffer) + (buffer->tiles ? (size_t)buffer->columns * buffer->rows * sizeof(RasterTile) : 0);
}

bool raster_buffer_stores(RasterBuffer* buffer, bool (*visit)(ByteStorage*, void*), void* context) {
    if (buffer->storage && !visit(buffer->storage, context)) return false;
    for (size_t i = 0; buffer->tiles && i < (size_t)buffer->columns * buffer->rows; i++)
        if (buffer->tiles[i].storage && !visit(buffer->tiles[i].storage, context)) return false;
    return true;
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
        if (buffer->pixels && channel < 0)
            memcpy(output + (size_t)y * width * channels, raster_buffer_pixel(buffer, left, top + y), (size_t)width * channels);
        else for (uint32_t x = 0; x < width; x++) {
            const uint8_t* pixel = raster_buffer_pixel(buffer, left + x, top + y);
            if (channel < 0) memcpy(output + ((size_t)y * width + x) * channels, pixel, channels);
            else output[(size_t)y * width + x] = pixel[channel];
        }
    }
    return true;
}

double raster_linear_sample(uint8_t sample) {
    double n = sample / 255.0;
    return n <= 0.04045 ? n / 12.92 : pow((n + 0.055) / 1.055, 2.4);
}

uint8_t raster_encoded_sample(double n) {
    n = n <= 0.0031308 ? 12.92 * n : 1.055 * pow(n, 1.0 / 2.4) - 0.055;
    return (uint8_t)lround(fmax(0.0, fmin(1.0, n)) * 255);
}

void raster_mix_pixel(uint8_t* d, const uint8_t* s, uint8_t channels, double coverage, bool erase) {
    if (coverage <= 0) return;
    if (channels == 1) { d[0] = (uint8_t)lround(d[0] * (1 - coverage) + (erase ? 0 : s[0]) * coverage); return; }
    double sa = coverage * s[3] / 255.0, da = d[3] / 255.0;
    if (erase) { d[3] = (uint8_t)lround(d[3] * (1 - coverage)); return; }
    if (sa <= 0) return;
    if (sa >= 1) { memcpy(d, s, 4); return; }
    double alpha = sa + da * (1 - sa);
    for (uint8_t c = 0; c < 3; c++) d[c] = raster_encoded_sample(
        (raster_linear_sample(s[c]) * sa + raster_linear_sample(d[c]) * da * (1 - sa)) / alpha);
    d[3] = (uint8_t)lround(alpha * 255);
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
        const uint8_t* s = raster_buffer_pixel(source, (uint32_t)(x - left), (uint32_t)(y - top));
        uint8_t* d = destination->pixels + ((size_t)y * destination->width + x) * 4;
        double coverage = 1;
        if (mask) {
            int64_t mx = x - mask_left, my = y - mask_top;
            coverage = mx >= 0 && my >= 0 && mx < mask->width && my < mask->height
                ? raster_buffer_pixel(mask, (uint32_t)mx, (uint32_t)my)[0] / 255.0 : outside_coverage;
            if (invert_mask) coverage = 1 - coverage;
        }
        uint8_t rgba[4] = {s[0], s[source->channels <= 2 ? 0 : 1], s[source->channels <= 2 ? 0 : 2],
            (uint8_t)(source->channels == 2 || source->channels == 4 ? s[source->channels - 1] : 255)};
        raster_mix_pixel(d, rgba, 4, opacity * coverage, false);
    }
}
