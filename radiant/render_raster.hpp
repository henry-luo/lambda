#pragma once

#include "view.hpp"
#include "clip_shape.h"

typedef struct RasterPaintContext {
    ImageSurface* surface;
    Bound* clip;
    ClipShape** clip_shapes;
    int clip_depth;
} RasterPaintContext;

void raster_fill_rect(RasterPaintContext* ctx, Rect* rect, uint32_t color);
void raster_blit_surface_scaled(RasterPaintContext* ctx, ImageSurface* src, Rect* src_rect,
                                Rect* dst_rect, ScaleMode scale_mode, uint8_t opacity = 255);
void raster_blit_pixels_scaled(RasterPaintContext* ctx, const uint32_t* pixels,
                               int src_w, int src_h, int src_stride,
                               Rect* dst_rect, ScaleMode scale_mode, uint8_t opacity = 255);
