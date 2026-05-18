#pragma once

#include "clip_shape.h"
#include "rdt_vector.hpp"
#include "../lib/scratch_arena.h"

struct RenderContext;
struct ViewBlock;

typedef struct RenderClipScope {
    ClipShape* shape;
    ClipShape inline_shape;
    bool active;
    bool pushed_shape;
    bool owns_shape;
} RenderClipScope;

RdtPath* render_clip_create_rounded_rect_path(float x, float y, float w, float h,
                                              float r_tl, float r_tr,
                                              float r_br, float r_bl);
RdtPath* render_clip_create_shape_path(ClipShape* shape);
ClipShape* render_clip_parse_css_shape(ScratchArena* scratch, const char* value,
                                       float elem_w, float elem_h,
                                       float abs_x, float abs_y);
void render_clip_free_shape(ScratchArena* scratch, ClipShape* shape);
RenderClipScope render_clip_push_css_scope(RenderContext* rdcon, ViewBlock* block,
                                           float parent_x, float parent_y, float scale);
RenderClipScope render_clip_push_overflow_scope(RenderContext* rdcon);
void render_clip_pop_scope(RenderContext* rdcon, RenderClipScope* scope);
