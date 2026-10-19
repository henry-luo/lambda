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
