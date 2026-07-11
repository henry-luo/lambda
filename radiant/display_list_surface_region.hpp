#pragma once

#include "display_list.h"
#include "clip_shape.h"
#include "../lib/math_utils.h"
#include <string.h>

static inline bool surface_region_clip(ImageSurface* surface,
                                       int rx, int ry, int rw, int rh,
                                       int out_region[4]) {
    if (!surface || !surface->pixels || !out_region) return false;
    int x0 = LMB_MAX(0, rx);
    int y0 = LMB_MAX(0, ry);
    int x1 = LMB_MIN(surface->width, rx + rw);
    int y1 = LMB_MIN(surface->height, ry + rh);
    int w = x1 - x0;
    int h = y1 - y0;
    out_region[0] = x0;
    out_region[1] = y0;
    out_region[2] = w > 0 ? w : 0;
    out_region[3] = h > 0 ? h : 0;
    return w > 0 && h > 0;
}

static inline uint32_t* surface_region_save(ImageSurface* surface,
                                            ScratchArena* scratch,
                                            int rx, int ry, int rw, int rh,
                                            int out_region[4]) {
    if (!scratch || !surface_region_clip(surface, rx, ry, rw, rh, out_region)) return nullptr;
    int x0 = out_region[0];
    int y0 = out_region[1];
    int w = out_region[2];
    int h = out_region[3];
    uint32_t* saved = (uint32_t*)scratch_alloc(scratch, (size_t)w * h * sizeof(uint32_t));
    if (!saved) return nullptr;
    uint32_t* px = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < h; row++) {
        memcpy(saved + row * w,
               px + (y0 + row) * pitch + x0,
               (size_t)w * sizeof(uint32_t));
    }
    return saved;
}

static inline void surface_region_clear(ImageSurface* surface, const int region[4]) {
    if (!surface || !surface->pixels || !region) return;
    int x0 = region[0];
    int y0 = region[1];
    int w = region[2];
    int h = region[3];
    if (w <= 0 || h <= 0) return;
    uint32_t* px = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < h; row++) {
        memset(px + (y0 + row) * pitch + x0, 0, (size_t)w * sizeof(uint32_t));
    }
}

static inline void surface_region_restore_masked(ImageSurface* surface,
                                                 const uint32_t* saved,
                                                 const int region[4],
                                                 ClipShape* mask,
                                                 bool restore_inside) {
    if (!surface || !surface->pixels || !saved || !region || !mask) return;
    int x0 = region[0];
    int y0 = region[1];
    int w = region[2];
    int h = region[3];
    if (w <= 0 || h <= 0) return;
    uint32_t* px = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            float fx = (float)(x0 + col) + 0.5f;
            float fy = (float)(y0 + row) + 0.5f;
            bool inside = clip_point_in_shape(mask, fx, fy);
            if (restore_inside ? inside : !inside) {
                px[(y0 + row) * pitch + (x0 + col)] = saved[row * w + col];
            }
        }
    }
}
