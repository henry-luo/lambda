// ==========================================================================
// PaintIR — recording (PaintBuilder) and raster lowering (PaintIR -> DisplayList).
//
// See paint_ir.h for the model. Step 1 lowers the vector primitive ops 1:1 to
// the matching dl_* command so raster output is byte-for-byte identical to
// recording those dl_* calls directly.
// ==========================================================================

#include "paint_ir.h"
#include "../lib/memtrack.h"
#include <string.h>

#define PAINT_LIST_INITIAL_CAPACITY 1024

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void paint_list_init(PaintList* pl, Arena* backing_arena) {
    memset(pl, 0, sizeof(PaintList));
    pl->arena = backing_arena;
}

void paint_list_clear(PaintList* pl) {
    if (!pl) return;
    pl->count = 0;
}

void paint_list_destroy(PaintList* pl) {
    if (!pl) return;
    if (pl->cmds) {
        mem_free(pl->cmds);
        pl->cmds = nullptr;
    }
    pl->count = 0;
    pl->capacity = 0;
}

int paint_list_count(const PaintList* pl) {
    return pl ? pl->count : 0;
}

static PaintCmd* paint_alloc_cmd(PaintList* pl, PaintOp op) {
    if (!pl) return nullptr;
    if (pl->count >= pl->capacity) {
        int new_cap = pl->capacity ? pl->capacity * 2 : PAINT_LIST_INITIAL_CAPACITY;
        pl->cmds = (PaintCmd*)mem_realloc(pl->cmds, new_cap * sizeof(PaintCmd), MEM_CAT_RENDER);
        pl->capacity = new_cap;
    }
    PaintCmd* cmd = &pl->cmds[pl->count++];
    memset(cmd, 0, sizeof(PaintCmd));
    cmd->op = op;
    return cmd;
}

// ---------------------------------------------------------------------------
// PaintBuilder — recording API
// ---------------------------------------------------------------------------

void paint_fill_rect(PaintList* pl, float x, float y, float w, float h, Color color) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_FILL_RECT);
    if (!cmd) return;
    cmd->fill_rect = { x, y, w, h, color };
}

void paint_fill_rounded_rect(PaintList* pl, float x, float y, float w, float h,
                             float rx, float ry, Color color) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_FILL_ROUNDED_RECT);
    if (!cmd) return;
    cmd->fill_rounded_rect = { x, y, w, h, rx, ry, color };
}

void paint_fill_path(PaintList* pl, RdtPath* path, Color color,
                     RdtFillRule rule, const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_FILL_PATH);
    if (!cmd) return;
    cmd->fill_path.path = path;
    cmd->fill_path.color = color;
    cmd->fill_path.rule = rule;
    cmd->fill_path.has_transform = transform != nullptr;
    if (transform) cmd->fill_path.transform = *transform;
}

void paint_stroke_path(PaintList* pl, RdtPath* path, Color color, float width,
                       RdtStrokeCap cap, RdtStrokeJoin join,
                       const float* dash_array, int dash_count, float dash_phase,
                       const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_STROKE_PATH);
    if (!cmd) return;
    cmd->stroke_path.path = path;
    cmd->stroke_path.color = color;
    cmd->stroke_path.width = width;
    cmd->stroke_path.cap = cap;
    cmd->stroke_path.join = join;
    cmd->stroke_path.dash_array = dash_array;
    cmd->stroke_path.dash_count = dash_count;
    cmd->stroke_path.dash_phase = dash_phase;
    cmd->stroke_path.has_transform = transform != nullptr;
    if (transform) cmd->stroke_path.transform = *transform;
}

void paint_fill_linear_gradient(PaintList* pl, RdtPath* path,
                                float x1, float y1, float x2, float y2,
                                const RdtGradientStop* stops, int stop_count,
                                RdtFillRule rule, const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_FILL_LINEAR_GRADIENT);
    if (!cmd) return;
    cmd->fill_linear_gradient.path = path;
    cmd->fill_linear_gradient.x1 = x1;
    cmd->fill_linear_gradient.y1 = y1;
    cmd->fill_linear_gradient.x2 = x2;
    cmd->fill_linear_gradient.y2 = y2;
    cmd->fill_linear_gradient.stops = stops;
    cmd->fill_linear_gradient.stop_count = stop_count;
    cmd->fill_linear_gradient.rule = rule;
    cmd->fill_linear_gradient.has_transform = transform != nullptr;
    if (transform) cmd->fill_linear_gradient.transform = *transform;
}

void paint_fill_radial_gradient(PaintList* pl, RdtPath* path,
                                float cx, float cy, float r,
                                const RdtGradientStop* stops, int stop_count,
                                RdtFillRule rule, const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_FILL_RADIAL_GRADIENT);
    if (!cmd) return;
    cmd->fill_radial_gradient.path = path;
    cmd->fill_radial_gradient.cx = cx;
    cmd->fill_radial_gradient.cy = cy;
    cmd->fill_radial_gradient.r = r;
    cmd->fill_radial_gradient.stops = stops;
    cmd->fill_radial_gradient.stop_count = stop_count;
    cmd->fill_radial_gradient.rule = rule;
    cmd->fill_radial_gradient.has_transform = transform != nullptr;
    if (transform) cmd->fill_radial_gradient.transform = *transform;
}

void paint_draw_image(PaintList* pl, const uint32_t* pixels,
                      int src_w, int src_h, int src_stride,
                      float dst_x, float dst_y, float dst_w, float dst_h,
                      uint8_t opacity, const RdtMatrix* transform,
                      void* resource_owner) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_DRAW_IMAGE);
    if (!cmd) return;
    cmd->draw_image.pixels = pixels;
    cmd->draw_image.src_w = src_w;
    cmd->draw_image.src_h = src_h;
    cmd->draw_image.src_stride = src_stride;
    cmd->draw_image.dst_x = dst_x;
    cmd->draw_image.dst_y = dst_y;
    cmd->draw_image.dst_w = dst_w;
    cmd->draw_image.dst_h = dst_h;
    cmd->draw_image.opacity = opacity;
    cmd->draw_image.has_transform = transform != nullptr;
    if (transform) cmd->draw_image.transform = *transform;
    cmd->draw_image.resource_owner = resource_owner;
}

void paint_draw_picture(PaintList* pl, RdtPicture* picture,
                        uint8_t opacity, const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_DRAW_PICTURE);
    if (!cmd) return;
    cmd->draw_picture.picture = picture;
    cmd->draw_picture.opacity = opacity;
    cmd->draw_picture.has_transform = transform != nullptr;
    if (transform) cmd->draw_picture.transform = *transform;
}

void paint_push_clip(PaintList* pl, RdtPath* clip_path, const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_PUSH_CLIP);
    if (!cmd) return;
    cmd->push_clip.clip_path = clip_path;
    cmd->push_clip.has_transform = transform != nullptr;
    if (transform) cmd->push_clip.transform = *transform;
}

void paint_pop_clip(PaintList* pl) {
    paint_alloc_cmd(pl, PAINT_POP_CLIP);
}

// ---------------------------------------------------------------------------
// Raster lowering: PaintIR -> DisplayList
// ---------------------------------------------------------------------------

void paint_ir_lower_raster(const PaintList* pl, DisplayList* dl) {
    if (!pl || !dl) return;

    for (int i = 0; i < pl->count; i++) {
        const PaintCmd* cmd = &pl->cmds[i];
        switch (cmd->op) {
        case PAINT_FILL_RECT: {
            const PaintFillRect* p = &cmd->fill_rect;
            dl_fill_rect(dl, p->x, p->y, p->w, p->h, p->color);
            break;
        }
        case PAINT_FILL_ROUNDED_RECT: {
            const PaintFillRoundedRect* p = &cmd->fill_rounded_rect;
            dl_fill_rounded_rect(dl, p->x, p->y, p->w, p->h, p->rx, p->ry, p->color);
            break;
        }
        case PAINT_FILL_PATH: {
            const PaintFillPath* p = &cmd->fill_path;
            dl_fill_path(dl, p->path, p->color, p->rule,
                         p->has_transform ? &p->transform : nullptr);
            break;
        }
        case PAINT_STROKE_PATH: {
            const PaintStrokePath* p = &cmd->stroke_path;
            dl_stroke_path(dl, p->path, p->color, p->width, p->cap, p->join,
                           p->dash_array, p->dash_count, p->dash_phase,
                           p->has_transform ? &p->transform : nullptr);
            break;
        }
        case PAINT_FILL_LINEAR_GRADIENT: {
            const PaintFillLinearGradient* p = &cmd->fill_linear_gradient;
            dl_fill_linear_gradient(dl, p->path, p->x1, p->y1, p->x2, p->y2,
                                    p->stops, p->stop_count, p->rule,
                                    p->has_transform ? &p->transform : nullptr);
            break;
        }
        case PAINT_FILL_RADIAL_GRADIENT: {
            const PaintFillRadialGradient* p = &cmd->fill_radial_gradient;
            dl_fill_radial_gradient(dl, p->path, p->cx, p->cy, p->r,
                                    p->stops, p->stop_count, p->rule,
                                    p->has_transform ? &p->transform : nullptr);
            break;
        }
        case PAINT_DRAW_IMAGE: {
            const PaintDrawImage* p = &cmd->draw_image;
            ImageSurface* owner = (ImageSurface*)p->resource_owner;
            dl_draw_image(dl, p->pixels, p->src_w, p->src_h, p->src_stride,
                          p->dst_x, p->dst_y, p->dst_w, p->dst_h, p->opacity,
                          p->has_transform ? &p->transform : nullptr,
                          owner, owner ? owner->generation : 0);
            break;
        }
        case PAINT_DRAW_PICTURE: {
            const PaintDrawPicture* p = &cmd->draw_picture;
            dl_draw_picture(dl, p->picture, p->opacity,
                            p->has_transform ? &p->transform : nullptr);
            break;
        }
        case PAINT_PUSH_CLIP: {
            const PaintPushClip* p = &cmd->push_clip;
            dl_push_clip(dl, p->clip_path, p->has_transform ? &p->transform : nullptr);
            break;
        }
        case PAINT_POP_CLIP:
            dl_pop_clip(dl);
            break;

        // Higher-level semantic ops are lowered in later phases (E: effects,
        // F: inline SVG). They are not yet emitted by the live render path.
        case PAINT_GLYPH_RUN:
        case PAINT_BEGIN_EFFECT_GROUP:
        case PAINT_END_EFFECT_GROUP:
        case PAINT_SVG_SUBSCENE:
            break;
        }
    }
}
