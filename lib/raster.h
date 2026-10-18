#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// immutable owned samples; edits construct a replacement, preserving snapshots.
typedef struct RasterBuffer {
    uint32_t width, height;
    uint8_t channels;
    size_t size;
    uint8_t* pixels;
} RasterBuffer;

RasterBuffer* raster_buffer_create(uint32_t width, uint32_t height, uint8_t channels,
    size_t byte_limit);
void raster_buffer_destroy(RasterBuffer* buffer);
bool raster_buffer_copy_region(const RasterBuffer* buffer, uint32_t left, uint32_t top,
    uint32_t width, uint32_t height, int channel, uint8_t* output, size_t capacity);
void raster_buffer_over(RasterBuffer* destination, const RasterBuffer* source,
    int64_t left, int64_t top, double opacity, const RasterBuffer* mask,
    int64_t mask_left, int64_t mask_top, double outside_coverage, bool invert_mask);
