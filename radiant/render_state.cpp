#include "render.hpp"
#include "view.hpp"
#include "../lib/log.h"

RenderTransformScope render_state_push_transform(RasterRenderContext* rdcon, ViewBlock* block,
                                                 const BlockBlot* parent_block) {
    RenderTransformScope scope = {
        lam::up(rdcon),
        rdcon->transform,
        rdcon->has_transform,
        rdcon->transform_3d,
        rdcon->children_transform_3d,
        rdcon->has_transform_3d,
        false
    };
    float scale = rdcon->raster_scale > 0.0f ? rdcon->raster_scale : 1.0f;
    // Paint positions are in device pixels; CSS transforms are defined in
    // layout pixels, then conjugated into the paint coordinate space below.
    float elem_x = parent_block->x / scale + block->x;
    float elem_y = parent_block->y / scale + block->y;
    RdtLogicalPoint origin = radiant::transform_origin(
        block->transformp(), elem_x, elem_y, block->width, block->height);
    RdtMatrix4 local = radiant::compute_transform_matrix_3d(block->transformp(),
        block->width, block->height, origin.x, origin.y,
        block->transform ? block->transformp()->origin_z : 0.0f);
    RdtMatrix4 parent = scope.previous_has_transform_3d
        ? scope.previous_children_transform_3d : rdt_matrix4_identity();
    if (!scope.previous_has_transform_3d && scope.previous_has_transform) {
        // an external planar paint basis is already in device coordinates.
        const RdtMatrix& basis = scope.previous_transform;
        parent.values[0] = basis.e11; parent.values[1] = basis.e12;
        parent.values[3] = basis.e13 / scale;
        parent.values[4] = basis.e21; parent.values[5] = basis.e22;
        parent.values[7] = basis.e23 / scale;
        parent.values[12] = basis.e31 * scale;
        parent.values[13] = basis.e32 * scale; parent.values[15] = basis.e33;
    }
    // Retain depth through the whole context; composing projected 3x3 matrices
    // here loses nested translations/rotations and applies perspective twice.
    rdcon->transform_3d = rdt_matrix4_multiply(&parent, &local);
    RdtMatrix4 boundary = radiant::compute_child_projection_matrix_3d(block->transformp(),
        block->width, block->height, elem_x, elem_y, radiant::transform_preserves_3d(block));
    rdcon->children_transform_3d = rdt_matrix4_multiply(&rdcon->transform_3d, &boundary);
    rdcon->has_transform_3d = true;
    RdtMatrix next_transform = radiant::matrix4_project_to_2d(&rdcon->transform_3d);
    next_transform.e13 *= scale;
    next_transform.e23 *= scale;
    next_transform.e31 /= scale;
    next_transform.e32 /= scale;

    rdcon->transform = next_transform;
    rdcon->has_transform = scope.previous_has_transform || transform_has_functions(block->transform) ||
        next_transform.e11 != 1.0f || next_transform.e22 != 1.0f || next_transform.e33 != 1.0f ||
        next_transform.e12 != 0.0f || next_transform.e13 != 0.0f || next_transform.e21 != 0.0f ||
        next_transform.e23 != 0.0f || next_transform.e31 != 0.0f || next_transform.e32 != 0.0f;
    scope.active = true;

    return scope;
}

void render_state_pop_transform(RenderTransformScope* scope) {
    if (!scope || !scope->context) {
        return;
    }
    scope->context->transform = scope->previous_transform;
    scope->context->has_transform = scope->previous_has_transform;
    scope->context->transform_3d = scope->previous_transform_3d;
    scope->context->children_transform_3d = scope->previous_children_transform_3d;
    scope->context->has_transform_3d = scope->previous_has_transform_3d;
    scope->active = false;
}

const RdtMatrix* render_state_current_transform(RasterRenderContext* rdcon) {
    if (!rdcon || !rdcon->has_transform) {
        return nullptr;
    }
    return &rdcon->transform;
}
