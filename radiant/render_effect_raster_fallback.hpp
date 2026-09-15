#pragma once
// internal implementation header — do not include outside radiant/

#include "render.hpp"
#include "view.hpp"
#include "../lib/arena.h"
#include "../lib/mem_factory.h"
#include "../lib/mempool.h"
#include <math.h>
#include <string.h>
extern "C" {
#include "../lib/log.h"
}

typedef struct RenderEffectRasterImage {
    ImageSurface* surface;
    float x;
    float y;
    float width;
    float height;
} RenderEffectRasterImage;

typedef struct RenderEffectRasterFallback {
    bool active;
    int nested_depth;
    PaintEffectGroup group;
    PaintList paint_list;
} RenderEffectRasterFallback;

static inline bool render_effect_group_needs_raster_fallback(
        const PaintEffectGroup* group) {
    return group &&
           (group->has_clip || group->blend_mode != 0 || group->filter ||
            group->backdrop || group->backdrop_filter ||
            group->shadow || group->isolation);
}

static inline bool render_effect_group_needs_page_backdrop(
        const PaintEffectGroup* group) {
    return group &&
           (group->blend_mode != 0 || group->backdrop || group->backdrop_filter);
}

static inline bool render_paint_list_effect_groups_balanced(
        const PaintList* paint_list) {
    if (!paint_list) return false;
    int effect_depth = 0;
    for (int i = 0; i < paint_list->count; i++) {
        PaintOp op = paint_list->cmds[i].op;
        if (paint_op_has_flags(op, PAINT_OP_FLAG_EFFECT_STACK | PAINT_OP_FLAG_STACK_PUSH)) {
            effect_depth++;
        } else if (paint_op_has_flags(op, PAINT_OP_FLAG_EFFECT_STACK | PAINT_OP_FLAG_STACK_POP)) {
            effect_depth--;
            if (effect_depth < 0) return false;
        }
    }
    return effect_depth == 0;
}

static inline void render_effect_record_page_backdrop_paint_list(
        bool page_backdrop_ready, DisplayList* page_backdrop,
        const PaintList* paint_list, PaintGlyphRunRasterLowerFn glyph_lowerer) {
    if (!page_backdrop_ready || !page_backdrop || !paint_list ||
        paint_list_count(paint_list) <= 0 ||
        !render_paint_list_effect_groups_balanced(paint_list)) {
        return;
    }
    render_svg_inline_register_paint_ir_lowerers();
    paint_ir_register_glyph_run_raster_lowerer(glyph_lowerer);
    paint_ir_lower_raster(paint_list, page_backdrop);
}

static inline void render_effect_raster_begin(
        RenderEffectRasterFallback* fallback, const PaintEffectGroup* group,
        PaintGlyphRunRasterLowerFn lowerer) {
    if (!fallback || !group) return;
    fallback->active = true;
    fallback->nested_depth = 0;
    fallback->group = *group;
    paint_list_clear(&fallback->paint_list);
    paint_ir_register_glyph_run_raster_lowerer(lowerer);
    paint_begin_effect_group(&fallback->paint_list, group);
}

static inline bool render_effect_raster_bounds(const Bound* bounds,
                                               float viewport_width,
                                               float viewport_height,
                                               int* x0,
                                               int* y0,
                                               int* w,
                                               int* h) {
    if (!x0 || !y0 || !w || !h) return false;
    float left = bounds ? floorf(bounds->left) : 0.0f;
    float top = bounds ? floorf(bounds->top) : 0.0f;
    float right = bounds ? ceilf(bounds->right) : viewport_width;
    float bottom = bounds ? ceilf(bounds->bottom) : viewport_height;
    if (right <= left || bottom <= top) {
        left = 0.0f;
        top = 0.0f;
        right = viewport_width;
        bottom = viewport_height;
    }
    if (right <= left || bottom <= top) return false;
    *x0 = (int)left; // INT_CAST_OK: raster fallback capture bounds are integral pixel tiles.
    *y0 = (int)top; // INT_CAST_OK: raster fallback capture bounds are integral pixel tiles.
    *w = (int)(right - left); // INT_CAST_OK: raster fallback capture width is an integer pixel tile.
    *h = (int)(bottom - top); // INT_CAST_OK: raster fallback capture height is an integer pixel tile.
    return *w > 0 && *h > 0;
}

static inline void render_effect_raster_fill_surface(ImageSurface* surface,
                                                     uint32_t color) {
    if (!surface || !surface->pixels) return;
    uint32_t* pixels = (uint32_t*)surface->pixels;
    int count = surface->width * surface->height;
    for (int i = 0; i < count; i++) {
        pixels[i] = color;
    }
}

static inline bool render_effect_rasterize_paint_list(const PaintList* paint_list,
                                                      DisplayList* backdrop_display_list,
                                                      const Bound* bounds,
                                                      float viewport_width,
                                                      float viewport_height,
                                                      bool opaque_background,
                                                      RenderEffectRasterImage* out,
                                                      const char* log_prefix) {
    if (!paint_list || !out) return false;
    memset(out, 0, sizeof(RenderEffectRasterImage));

    int x0 = 0, y0 = 0, w = 0, h = 0;
    if (!render_effect_raster_bounds(bounds, viewport_width, viewport_height,
                                     &x0, &y0, &w, &h)) {
        log_error("%s invalid raster fallback bounds", log_prefix ? log_prefix : "[EFFECT_RASTER]");
        return false;
    }

    ImageSurface* surface = image_surface_create(w, h);
    if (!surface) {
        log_error("%s failed to allocate raster fallback surface %dx%d",
                  log_prefix ? log_prefix : "[EFFECT_RASTER]", w, h);
        return false;
    }
    if (opaque_background) {
        render_effect_raster_fill_surface(surface, 0xffffffffu);
    }

    Pool* temp_pool = mem_pool_create(NULL, MEM_ROLE_RENDER, "render.effect.raster");
    if (!temp_pool) {
        image_surface_destroy(surface);
        return false;
    }
    Arena* temp_arena = mem_arena_create(NULL, MEM_ROLE_RENDER, "render.effect.arena");
    if (!temp_arena) {
        mem_pool_destroy(temp_pool);
        image_surface_destroy(surface);
        return false;
    }

    DisplayList dl = {};
    ScratchArena scratch = {};
    dl_init(&dl, temp_arena);
    mem_scratch_init(NULL, &scratch, temp_arena, MEM_ROLE_RENDER, "render.effect.scratch");

    RdtVector vec = {};
    rdt_vector_init(&vec, (uint32_t*)surface->pixels, w, h, w);
    if (backdrop_display_list) {
        dl_replay_tile(backdrop_display_list, &vec, surface, &scratch,
                       (float)x0, (float)y0, (float)w, (float)h, 1.0f);
    }
    paint_ir_lower_raster(paint_list, &dl);
    dl_replay_tile(&dl, &vec, surface, &scratch,
                   (float)x0, (float)y0, (float)w, (float)h, 1.0f);
    rdt_vector_destroy(&vec);

    scratch_release(&scratch);
    // effect rasterization is one-shot; dl_clear() retains the heap-grown item
    // buffer and leaked it when the backing arena was destroyed immediately after.
    dl_destroy(&dl);
    // The arena object and its chunks live inside temp_pool, so destroying the
    // pool is the single owner teardown and also unregisters the arena cascade.
    mem_pool_destroy(temp_pool);

    out->surface = surface;
    out->x = (float)x0;
    out->y = (float)y0;
    out->width = (float)w;
    out->height = (float)h;
    return true;
}
