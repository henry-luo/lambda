#include "display_list_replay_shadow.hpp"

#include <string.h>

static int dl_replay_shadow_max_i(int a, int b) {
    return a > b ? a : b;
}

static int dl_replay_shadow_min_i(int a, int b) {
    return a < b ? a : b;
}

void dl_replay_shadow_clip_init(DisplayReplayShadowClip* clip) {
    if (!clip) return;
    memset(clip, 0, sizeof(DisplayReplayShadowClip));
}

void dl_replay_shadow_clip_save(DisplayReplayShadowClip* clip,
                                ImageSurface* surface,
                                ScratchArena* scratch,
                                const DlShadowClipSave* save) {
    if (!clip) return;
    clip->saved = nullptr;
    if (!surface || !surface->pixels || !scratch || !save) return;

    int sw = surface->width;
    int sh = surface->height;
    int x0 = dl_replay_shadow_max_i(0, save->rx);
    int y0 = dl_replay_shadow_max_i(0, save->ry);
    int x1 = dl_replay_shadow_min_i(sw, save->rx + save->rw);
    int y1 = dl_replay_shadow_min_i(sh, save->ry + save->rh);
    int w = x1 - x0;
    int h = y1 - y0;
    if (w <= 0 || h <= 0) return;

    clip->saved = (uint32_t*)scratch_alloc(scratch, (size_t)w * h * sizeof(uint32_t));
    uint32_t* px = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < h; row++) {
        memcpy(clip->saved + row * w,
               px + (y0 + row) * pitch + x0,
               w * sizeof(uint32_t));
    }
    clip->region[0] = x0;
    clip->region[1] = y0;
    clip->region[2] = w;
    clip->region[3] = h;
}

void dl_replay_shadow_clip_restore(DisplayReplayShadowClip* clip,
                                   ImageSurface* surface,
                                   const DlShadowClipRestore* restore) {
    if (!clip) return;
    if (clip->saved && surface && surface->pixels && restore && restore->exclude_type) {
        int x0 = clip->region[0];
        int y0 = clip->region[1];
        int w = clip->region[2];
        int h = clip->region[3];
        ClipShape ex = clip_shape_from_params(restore->exclude_type, restore->exclude_params);
        uint32_t* px = (uint32_t*)surface->pixels;
        int pitch = surface->pitch / 4;
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                float fx = (float)(x0 + col) + 0.5f;
                float fy = (float)(y0 + row) + 0.5f;
                bool inside = clip_point_in_shape(&ex, fx, fy);
                if (restore->restore_inside ? inside : !inside) {
                    px[(y0 + row) * pitch + (x0 + col)] = clip->saved[row * w + col];
                }
            }
        }
    }
    clip->saved = nullptr;
}
