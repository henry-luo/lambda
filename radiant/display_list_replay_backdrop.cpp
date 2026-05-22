#include "display_list_replay_backdrop.hpp"

#include "render_composite.hpp"
#include <string.h>

static int dl_replay_max_i(int a, int b) {
    return a > b ? a : b;
}

static int dl_replay_min_i(int a, int b) {
    return a < b ? a : b;
}

void dl_replay_backdrop_init(DisplayReplayBackdropStack* stack) {
    if (!stack) return;
    memset(stack, 0, sizeof(DisplayReplayBackdropStack));
}

int dl_replay_backdrop_depth(const DisplayReplayBackdropStack* stack) {
    return stack ? stack->sp : 0;
}

void dl_replay_backdrop_save(DisplayReplayBackdropStack* stack,
                             ImageSurface* surface,
                             ScratchArena* scratch,
                             const DlSaveBackdrop* backdrop) {
    if (!stack || !surface || !surface->pixels || !scratch || !backdrop ||
        stack->sp >= DL_REPLAY_MAX_BACKDROP_DEPTH) {
        return;
    }

    int x0 = dl_replay_max_i(0, backdrop->x0);
    int y0 = dl_replay_max_i(0, backdrop->y0);
    int x1 = dl_replay_min_i(surface->width, backdrop->x0 + backdrop->w);
    int y1 = dl_replay_min_i(surface->height, backdrop->y0 + backdrop->h);
    int w = x1 - x0;
    int h = y1 - y0;
    if (w <= 0 || h <= 0) {
        stack->stack[stack->sp] = nullptr;
        stack->region[stack->sp][0] = 0;
        stack->region[stack->sp][1] = 0;
        stack->region[stack->sp][2] = 0;
        stack->region[stack->sp][3] = 0;
        stack->sp++;
        return;
    }

    int pitch = surface->pitch / 4;
    int sz = w * h;
    uint32_t* buf = (uint32_t*)scratch_alloc(scratch, sz * sizeof(uint32_t));
    uint32_t* px = (uint32_t*)surface->pixels;
    for (int row = 0; row < h; row++) {
        memcpy(buf + row * w,
               px + (y0 + row) * pitch + x0,
               w * sizeof(uint32_t));
    }
    for (int row = 0; row < h; row++) {
        memset(px + (y0 + row) * pitch + x0, 0, w * sizeof(uint32_t));
    }

    stack->stack[stack->sp] = buf;
    stack->region[stack->sp][0] = x0;
    stack->region[stack->sp][1] = y0;
    stack->region[stack->sp][2] = w;
    stack->region[stack->sp][3] = h;
    stack->sp++;
}

void dl_replay_backdrop_composite_opacity(DisplayReplayBackdropStack* stack,
                                          ImageSurface* surface,
                                          const DlCompositeOpacity* opacity) {
    if (!stack || !surface || !surface->pixels || !opacity || stack->sp <= 0) return;
    stack->sp--;
    uint32_t* backdrop = stack->stack[stack->sp];
    if (!backdrop) return;

    int bx = stack->region[stack->sp][0];
    int by = stack->region[stack->sp][1];
    int bw = stack->region[stack->sp][2];
    int bh = stack->region[stack->sp][3];
    if (opacity->premultiplied_source && opacity->opacity >= 0.999f) {
        render_composite_source_over_premul(surface, backdrop, bx, by, bw, bh);
    } else {
        render_composite_opacity(surface, backdrop, bx, by, bw, bh,
                                 opacity->opacity);
    }
}

void dl_replay_backdrop_apply_blend_mode(DisplayReplayBackdropStack* stack,
                                         ImageSurface* surface,
                                         const DlApplyBlendMode* blend) {
    if (!stack || !surface || !surface->pixels || !blend || stack->sp <= 0) return;
    stack->sp--;
    uint32_t* backdrop = stack->stack[stack->sp];
    if (!backdrop) return;

    int bx = stack->region[stack->sp][0];
    int by = stack->region[stack->sp][1];
    int bw = stack->region[stack->sp][2];
    int bh = stack->region[stack->sp][3];
    uint32_t* px = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < bh; row++) {
        for (int col = 0; col < bw; col++) {
            uint32_t bd = backdrop[row * bw + col];
            uint32_t source = px[(by + row) * pitch + (bx + col)];
            px[(by + row) * pitch + (bx + col)] =
                render_composite_blend_pixel(bd, source, (CssEnum)blend->blend_mode);
        }
    }
}
