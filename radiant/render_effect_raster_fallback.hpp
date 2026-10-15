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
            group->shadow || group->isolation || group->mask.gradient);
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
    for (int i = 0; i < paint_list->item_count(); i++) {
        PaintOp op = paint_list->data()[i].op;
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
        const PaintList* paint_list, PaintGlyphRunRasterLowerFn glyph_lowerer, float density) {
    if (!page_backdrop_ready || !page_backdrop || !paint_list ||
        paint_list_count(paint_list) <= 0 ||
        !render_paint_list_effect_groups_balanced(paint_list)) {
        return;
    }
    render_svg_inline_register_paint_ir_lowerers();
    paint_ir_register_glyph_run_raster_lowerer(glyph_lowerer);
    // vector callbacks can stream clip scopes across fragments; apply export density once.
    RdtMatrix scale = rdt_matrix_scale(density, density);
    paint_ir_lower_raster_fragment(paint_list, page_backdrop, &scale);
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

static inline bool render_effect_rasterize_paint_list(const PaintList* paint_list,
                                                      DisplayList* backdrop_display_list,
                                                      const Bound* bounds,
                                                      float viewport_width,
                                                      float viewport_height,
                                                      bool opaque_background,
                                                      float density,
                                                      RenderEffectRasterImage* out,
                                                      const char* log_prefix,
                                                      const RdtMatrix* placement = nullptr,
                                                      const Bound* capture_clip = nullptr) {
    if (!paint_list || !out) return false;
    memset(out, 0, sizeof(RenderEffectRasterImage));

    if (!isfinite(density) || density <= 0.0f) return false;
    Bound capture = bounds ? *bounds : Bound{0.0f, 0.0f, viewport_width, viewport_height};
    if (capture.right <= capture.left || capture.bottom <= capture.top)
        capture = Bound{0.0f, 0.0f, viewport_width, viewport_height};
    if (placement && !rdt_matrix_project_rect_bounds(placement, capture.left, capture.top,
            capture.right, capture.bottom, &capture.left, &capture.top, &capture.right, &capture.bottom)) return false;
    capture = Bound{floorf(capture.left * density), floorf(capture.top * density),
        ceilf(capture.right * density), ceilf(capture.bottom * density)};
    OffscreenRenderArenas arenas;
    if (!arenas.init("render.effect.raster", "render.effect.list_arena",
                     "render.effect.scratch_arena")) {
        return false;
    }

    DisplayList dl = {};
    dl_init(&dl, arenas.list_arena);
    RdtMatrix scale = rdt_matrix_scale(density, density);
    if (placement) scale = rdt_matrix_multiply(&scale, placement);
    // export PaintIR is logical; replay and filter regions use the page's physical density once.
    paint_ir_lower_raster_transformed(paint_list, &dl, &scale);
    Bound ink = dl_content_bounds(&dl);
    if (capture_clip && !opaque_background) {
        // a PDF resource needs painted ink, not transparent space in its owner's layout box.
        capture = {floorf(ink.left), floorf(ink.top), ceilf(ink.right), ceilf(ink.bottom)};
    } else if (ink.right > ink.left && ink.bottom > ink.top)
        capture = {floorf(min(capture.left, ink.left)), floorf(min(capture.top, ink.top)),
            ceilf(max(capture.right, ink.right)), ceilf(max(capture.bottom, ink.bottom))};
    if (!isfinite(capture.left) || !isfinite(capture.top) ||
        !isfinite(capture.right) || !isfinite(capture.bottom)) {
        log_error("%s nonfinite raster fallback bounds", log_prefix ? log_prefix : "[EFFECT_RASTER]");
        dl_destroy(&dl); arenas.destroy(); return false;
    }
    if (capture_clip) {
        // PDF captures page coordinates, including transformed resources outside the sheet.
        capture = {max(capture.left, capture_clip->left), max(capture.top, capture_clip->top),
            min(capture.right, capture_clip->right), min(capture.bottom, capture_clip->bottom)};
        if (capture.right <= capture.left || capture.bottom <= capture.top) {
            bool valid = dl_validate_or_log(&dl, "empty raster resource");
            dl_destroy(&dl); arenas.destroy(); return valid;
        }
    }
    size_t bytes = 0;
    if (!render_surface_allocation_size(capture.right - capture.left, capture.bottom - capture.top, &bytes)) {
        log_error("%s invalid raster fallback bounds", log_prefix ? log_prefix : "[EFFECT_RASTER]");
        dl_destroy(&dl); arenas.destroy(); return false;
    }

    ImageSurface* surface = render_display_list_snapshot_with_backdrop(&dl,
        mem_context_process(MEM_ROLE_RENDER), capture, density,
        Color{opaque_background ? 0xffffffffu : 0u}, backdrop_display_list);

    // effect rasterization is one-shot; dl_clear() retains the heap-grown item
    // buffer and leaked it when the backing arena was destroyed immediately after.
    dl_destroy(&dl);
    arenas.destroy();

    if (!surface) {
        log_error("%s cannot capture raster region [%g %g %g %g]", log_prefix ? log_prefix : "[EFFECT_RASTER]",
            capture.left, capture.top, capture.right, capture.bottom);
        return false;
    }
    float offset_x = 0.0f, offset_y = 0.0f;
    // conservative recording bounds must not add transparent reader-sampled image borders.
    image_surface_trim_transparent_margin(surface, &offset_x, &offset_y);
    capture.left += offset_x; capture.top += offset_y;
    out->surface = surface;
    out->x = capture.left / density;
    out->y = capture.top / density;
    out->width = (float)surface->width / density;
    out->height = (float)surface->height / density;
    return true;
}
