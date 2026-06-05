#pragma once

#include "clip_shape.h"
#include "rdt_vector.hpp"
#include "../lib/scratch_arena.h"

struct RenderContext;
struct ViewBlock;
typedef struct Bound Bound;

typedef struct RenderClipScope {
    ClipShape* shape;
    bool active;
    bool pushed_shape;
    bool owns_shape;
} RenderClipScope;

RenderClipScope render_clip_push_css_scope(RenderContext* rdcon, ViewBlock* block,
                                           float parent_x, float parent_y, float scale);
RenderClipScope render_clip_push_rect_scope(RenderContext* rdcon, const Bound* clip);
RenderClipScope render_clip_push_overflow_scope(RenderContext* rdcon);
void render_clip_pop_scope(RenderContext* rdcon, RenderClipScope* scope);
