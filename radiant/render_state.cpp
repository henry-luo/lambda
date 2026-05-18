#include "render_state.hpp"
#include "render.hpp"
#include "transform.hpp"
#include "../lib/log.h"

static RdtMatrix render_state_multiply_matrix(const RdtMatrix* left, const RdtMatrix* right) {
    RdtMatrix combined = {
        left->e11 * right->e11 + left->e12 * right->e21 + left->e13 * right->e31,
        left->e11 * right->e12 + left->e12 * right->e22 + left->e13 * right->e32,
        left->e11 * right->e13 + left->e12 * right->e23 + left->e13 * right->e33,
        left->e21 * right->e11 + left->e22 * right->e21 + left->e23 * right->e31,
        left->e21 * right->e12 + left->e22 * right->e22 + left->e23 * right->e32,
        left->e21 * right->e13 + left->e22 * right->e23 + left->e23 * right->e33,
        left->e31 * right->e11 + left->e32 * right->e21 + left->e33 * right->e31,
        left->e31 * right->e12 + left->e32 * right->e22 + left->e33 * right->e32,
        left->e31 * right->e13 + left->e32 * right->e23 + left->e33 * right->e33
    };
    return combined;
}

RenderTransformScope render_state_push_transform(RenderContext* rdcon, ViewBlock* block,
                                                 const BlockBlot* parent_block) {
    RenderTransformScope scope = {
        rdcon,
        rdcon->transform,
        rdcon->has_transform,
        false
    };
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
        block->transform->functions, block->width, block->height, origin_x, origin_y);

    if (scope.previous_has_transform) {
        rdcon->transform = render_state_multiply_matrix(&scope.previous_transform, &next_transform);
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
    scope->active = false;
}
