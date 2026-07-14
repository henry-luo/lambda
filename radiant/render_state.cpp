#include "render.hpp"
#include "view.hpp"
#include "../lib/log.h"

RenderTransformScope render_state_push_transform(RenderContext* rdcon, ViewBlock* block,
                                                 const BlockBlot* parent_block) {
    RenderTransformScope scope = {
        rdcon,
        rdcon->transform,
        rdcon->has_transform,
        rdcon->perspective_distance,
        rdcon->perspective_origin_x,
        rdcon->perspective_origin_y,
        false
    };
    if (block->transform && block->transform->perspective > 0.0f) {
        float elem_x = parent_block->x + block->x;
        float elem_y = parent_block->y + block->y;
        rdcon->perspective_distance = block->transform->perspective;
        rdcon->perspective_origin_x = elem_x + block->transform->perspective_origin_x;
        rdcon->perspective_origin_y = elem_y + block->transform->perspective_origin_y;
        scope.active = true;
        log_debug("[TRANSFORM] Element %s: perspective active, distance=%.1f",
            block->node_name(), rdcon->perspective_distance);
    }

    if (!block->transform || !block->transform->functions) {
        return scope;
    }

    float origin_x = block->transform->origin_x_percent
        ? (block->transform->origin_x / 100.0f) * block->width
        : block->transform->origin_x;
    float origin_y = block->transform->origin_y_percent
        ? (block->transform->origin_y / 100.0f) * block->height
        : block->transform->origin_y;

    float elem_x = parent_block->x + block->x;
    float elem_y = parent_block->y + block->y;
    origin_x += elem_x;
    origin_y += elem_y;

    RdtMatrix next_transform = radiant::compute_transform_matrix(
        block->transform->functions, block->width, block->height, origin_x, origin_y,
        rdcon->perspective_distance, rdcon->perspective_origin_x, rdcon->perspective_origin_y);

    if (scope.previous_has_transform) {
        rdcon->transform = rdt_matrix_multiply(&scope.previous_transform, &next_transform);
    } else {
        rdcon->transform = next_transform;
    }
    rdcon->has_transform = true;
    scope.active = true;

    log_debug("[TRANSFORM] Element %s: transform active, origin=(%.1f,%.1f)",
        block->node_name(), origin_x, origin_y);
    return scope;
}

void render_state_pop_transform(RenderTransformScope* scope) {
    if (!scope || !scope->context) {
        return;
    }
    scope->context->transform = scope->previous_transform;
    scope->context->has_transform = scope->previous_has_transform;
    scope->context->perspective_distance = scope->previous_perspective_distance;
    scope->context->perspective_origin_x = scope->previous_perspective_origin_x;
    scope->context->perspective_origin_y = scope->previous_perspective_origin_y;
    scope->active = false;
}

const RdtMatrix* render_state_current_transform(RenderContext* rdcon) {
    if (!rdcon || !rdcon->has_transform) {
        return nullptr;
    }
    return &rdcon->transform;
}
