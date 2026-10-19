#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "byte_storage.h"

enum { RASTER_TILE_SIDE = 256 };
typedef struct RasterTile {
    ByteStorage* storage;
    size_t offset, stride;
} RasterTile;

// immutable owned samples; edits construct a replacement, preserving snapshots.
typedef struct RasterBuffer {
    uint32_t width, height;
    uint8_t channels;
    size_t size;
    uint8_t* pixels;
    ByteStorage* storage;
    RasterTile* tiles;
    uint32_t columns, rows;
} RasterBuffer;

RasterBuffer* raster_buffer_create(uint32_t width, uint32_t height, uint8_t channels,
    size_t byte_limit);
void raster_buffer_destroy(RasterBuffer* buffer);
RasterBuffer* raster_buffer_sparse(uint32_t width, uint32_t height, uint8_t channels, size_t byte_limit);
RasterBuffer* raster_buffer_fork(const RasterBuffer* source);
const uint8_t* raster_buffer_pixel(const RasterBuffer* buffer, uint32_t x, uint32_t y);
uint8_t* raster_buffer_edit_pixel(RasterBuffer* buffer, uint32_t x, uint32_t y);
size_t raster_buffer_index_bytes(const RasterBuffer* buffer);
// callbacks visit retained allocations; repeated shared stores are intentional.
bool raster_buffer_stores(RasterBuffer* buffer, bool (*visit)(ByteStorage*, void*), void* context);
double raster_linear_sample(uint8_t sample);
uint8_t raster_encoded_sample(double value);
void raster_mix_pixel(uint8_t* destination, const uint8_t* source, uint8_t channels,
    double coverage, bool erase);
bool raster_buffer_copy_region(const RasterBuffer* buffer, uint32_t left, uint32_t top,
    uint32_t width, uint32_t height, int channel, uint8_t* output, size_t capacity);
void raster_buffer_over(RasterBuffer* destination, const RasterBuffer* source,
    int64_t left, int64_t top, double opacity, const RasterBuffer* mask,
    int64_t mask_left, int64_t mask_top, double outside_coverage, bool invert_mask);

// blend functions use linear-light RGB; samples remain straight encoded sRGB.
typedef enum RasterBlend { RASTER_NORMAL, RASTER_MULTIPLY, RASTER_SCREEN, RASTER_OVERLAY,
    RASTER_DARKEN, RASTER_LIGHTEN, RASTER_DIFFERENCE, RASTER_EXCLUSION } RasterBlend;
void raster_mix_blend(uint8_t* destination, const uint8_t* source, double coverage, RasterBlend mode);
void raster_rgba_pixel(const RasterBuffer* source, uint32_t x, uint32_t y, uint8_t* rgba);
bool raster_buffer_blend(RasterBuffer* destination, const RasterBuffer* source,
    double left, double top, const double* transform, RasterBlend mode, double opacity,
    const RasterBuffer* mask, double mask_left, double mask_top, double outside, bool inverse, bool atop);

void raster_buffer_crossfade(RasterBuffer* destination, const RasterBuffer* source, double opacity,
    const RasterBuffer* mask, int64_t left, int64_t top, double outside, bool inverse, bool atop);

#ifdef __cplusplus
struct RasterOwner {
    RasterBuffer* result = nullptr;
    RasterOwner() = default;
    RasterOwner(const RasterOwner&) = delete;
    RasterOwner& operator=(const RasterOwner&) = delete;
    ~RasterOwner() { raster_buffer_destroy(result); }
    void reset(RasterBuffer* replacement) { raster_buffer_destroy(result); result = replacement; }
};

#endif
