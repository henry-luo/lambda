#include "display_list_replay_backdrop.hpp"

#include "display_list_surface_region.hpp"
#include "render_composite.hpp"
#include <string.h>

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

    int region[4] = {};
    uint32_t* buf = surface_region_save(surface, scratch,
                                        backdrop->x0, backdrop->y0,
                                        backdrop->w, backdrop->h,
                                        region);
    if (!buf) {
        stack->stack[stack->sp] = nullptr;
        memcpy(stack->region[stack->sp], region, sizeof(region));
        stack->sp++;
        return;
    }

    surface_region_clear(surface, region);
    stack->stack[stack->sp] = buf;
    memcpy(stack->region[stack->sp], region, sizeof(region));
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
