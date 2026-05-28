#pragma once

#include "view.hpp"
#include "rdt_vector.hpp"

struct RenderContext;

typedef struct RenderTransformScope {
    RenderContext* context;
    RdtMatrix previous_transform;
    bool previous_has_transform;
    float previous_perspective_distance;
    float previous_perspective_origin_x;
    float previous_perspective_origin_y;
    bool active;
} RenderTransformScope;

RenderTransformScope render_state_push_transform(RenderContext* rdcon, ViewBlock* block,
                                                 const BlockBlot* parent_block);
void render_state_pop_transform(RenderTransformScope* scope);
const RdtMatrix* render_state_current_transform(RenderContext* rdcon);
