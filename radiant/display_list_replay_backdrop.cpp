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
    dl_replay_backdrop_save_at_offset(stack, surface, scratch, backdrop, 0.0f, 0.0f);
}

void dl_replay_backdrop_save_at_offset(DisplayReplayBackdropStack* stack,
                                       ImageSurface* surface,
                                       ScratchArena* scratch,
                                       const DlSaveBackdrop* backdrop,
                                       float origin_x, float origin_y) {
    if (!stack || !surface || !surface->pixels || !scratch || !backdrop ||
        stack->sp >= DL_REPLAY_MAX_BACKDROP_DEPTH) {
        return;
    }

    IRect region = {};
    uint32_t* buf = surface_region_save(surface, scratch,
                                        backdrop->x0 - (int)origin_x,
                                        backdrop->y0 - (int)origin_y,
                                        backdrop->w, backdrop->h,
                                        &region);
    if (!buf) {
        stack->stack[stack->sp] = nullptr;
        stack->region[stack->sp] = region;
        stack->sp++;
        return;
    }

    surface_region_clear(surface, &region);
    stack->stack[stack->sp] = buf;
    stack->region[stack->sp] = region;
    stack->sp++;
}

void dl_replay_backdrop_push_empty(DisplayReplayBackdropStack* stack) {
    if (!stack || stack->sp >= DL_REPLAY_MAX_BACKDROP_DEPTH) return;
    stack->stack[stack->sp] = nullptr;
    memset(&stack->region[stack->sp], 0, sizeof(stack->region[stack->sp]));
    stack->sp++;
}

void dl_replay_backdrop_discard(DisplayReplayBackdropStack* stack,
                                ScratchArena* scratch) {
    if (!stack || stack->sp <= 0) return;
    stack->sp--;
    uint32_t* backdrop = stack->stack[stack->sp];
    if (backdrop && scratch) scratch_free(scratch, backdrop);
    stack->stack[stack->sp] = nullptr;
}

void dl_replay_backdrop_composite_opacity(DisplayReplayBackdropStack* stack,
                                          ImageSurface* surface,
                                          ScratchArena* scratch,
                                          const DlCompositeOpacity* opacity) {
    if (!stack || !surface || !surface->pixels || !opacity || stack->sp <= 0) return;
    stack->sp--;
    uint32_t* backdrop = stack->stack[stack->sp];
    if (!backdrop) return;

    int bx = stack->region[stack->sp].x;
    int by = stack->region[stack->sp].y;
    int bw = stack->region[stack->sp].w;
    int bh = stack->region[stack->sp].h;
    if (opacity->premultiplied_source && opacity->opacity >= 0.999f) {
        render_composite_source_over_premul(surface, backdrop, bx, by, bw, bh);
    } else {
        render_composite_opacity(surface, backdrop, bx, by, bw, bh,
                                 opacity->opacity);
    }
    if (scratch) scratch_free(scratch, backdrop);
    stack->stack[stack->sp] = nullptr;
}

void dl_replay_backdrop_apply_blend_mode(DisplayReplayBackdropStack* stack,
                                         ImageSurface* surface,
                                         ScratchArena* scratch,
                                         const DlApplyBlendMode* blend) {
    if (!stack || !surface || !surface->pixels || !blend || stack->sp <= 0) return;
    stack->sp--;
    uint32_t* backdrop = stack->stack[stack->sp];
    if (!backdrop) return;

    int bx = stack->region[stack->sp].x;
    int by = stack->region[stack->sp].y;
    int bw = stack->region[stack->sp].w;
    int bh = stack->region[stack->sp].h;
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
    if (scratch) scratch_free(scratch, backdrop);
    stack->stack[stack->sp] = nullptr;
}

bool dl_replay_backdrop_skip_item(DisplayReplayBackdropStack* stack,
                                  ScratchArena* scratch,
                                  const DisplayItem* item) {
    if (!item) return false;

    switch (item->op) {
        case DL_SAVE_BACKDROP:
            dl_replay_backdrop_push_empty(stack);
            return true;

        case DL_APPLY_BLEND_MODE:
        case DL_COMPOSITE_OPACITY:
            dl_replay_backdrop_discard(stack, scratch);
            return true;

        default:
            return false;
    }
}
