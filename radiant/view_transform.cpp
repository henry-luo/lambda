#include "view.hpp"

#include <math.h>

static double matrix4_cofactor(const RdtMatrix4* matrix, int row, int column) {
    double minor[9];
    unsigned count = 0;
    for (int r = 0; r < 4; r++) {
        if (r == row) continue;
        for (int c = 0; c < 4; c++) {
            if (c != column) minor[count++] = matrix->values[r * 4 + c];
        }
    }
    double determinant = minor[0] * (minor[4] * minor[8] - minor[5] * minor[7]) -
        minor[1] * (minor[3] * minor[8] - minor[5] * minor[6]) +
        minor[2] * (minor[3] * minor[7] - minor[4] * minor[6]);
    return (row + column) % 2 ? -determinant : determinant;
}

bool rdt_matrix4_backface_visible(const RdtMatrix4* matrix) {
    if (!matrix) return false;
    double determinant = 0.0;
    for (int column = 0; column < 4; column++)
        determinant += matrix->values[column] * matrix4_cofactor(matrix, 0, column);
    double normal_z = matrix4_cofactor(matrix, 2, 2);
    // surface normals use the inverse transpose; m33 alone fails with perspective and skew.
    return determinant != 0.0 && isfinite(determinant) && isfinite(normal_z) &&
        normal_z != 0.0 && signbit(normal_z) != signbit(determinant);
}

namespace radiant {

RdtLogicalPoint transform_origin(const TransformProp* transform,
                                 float x, float y,
                                 float width, float height) {
    RdtLogicalPoint origin = {x, y};
    if (!transform) return origin;
    origin.x += transform->origin_x_percent
        ? width * transform->origin_x / 100.0f : transform->origin_x;
    origin.y += transform->origin_y_percent
        ? height * transform->origin_y / 100.0f : transform->origin_y;
    return origin;
}

static RdtMatrix4 matrix4_from_scale(float x, float y, float z) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    matrix.values[0] = x;
    matrix.values[5] = y;
    matrix.values[10] = z;
    return matrix;
}

static RdtMatrix4 matrix4_from_plane_rotation(float angle, int first_axis,
                                              int second_axis, float orientation) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    float cosine = cosf(angle);
    float sine = sinf(angle) * orientation;
    matrix.values[first_axis * 4 + first_axis] = cosine;
    matrix.values[first_axis * 4 + second_axis] = -sine;
    matrix.values[second_axis * 4 + first_axis] = sine;
    matrix.values[second_axis * 4 + second_axis] = cosine;
    return matrix;
}

static RdtMatrix4 matrix4_from_rotate_x(float angle) {
    return matrix4_from_plane_rotation(angle, 1, 2, 1.0f);
}

static RdtMatrix4 matrix4_from_rotate_y(float angle) {
    return matrix4_from_plane_rotation(angle, 0, 2, -1.0f);
}

static RdtMatrix4 matrix4_from_rotate_z(float angle) {
    return matrix4_from_plane_rotation(angle, 0, 1, 1.0f);
}

static RdtMatrix4 matrix4_from_rotate3d(float x, float y, float z,
                                        float angle) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    float length = sqrtf(x * x + y * y + z * z);
    if (length <= 0.0001f) return matrix;
    x /= length;
    y /= length;
    z /= length;
    float cosine = cosf(angle);
    float sine = sinf(angle);
    float one_minus_cosine = 1.0f - cosine;
    matrix.values[0] = cosine + x * x * one_minus_cosine;
    matrix.values[1] = x * y * one_minus_cosine - z * sine;
    matrix.values[2] = x * z * one_minus_cosine + y * sine;
    matrix.values[4] = y * x * one_minus_cosine + z * sine;
    matrix.values[5] = cosine + y * y * one_minus_cosine;
    matrix.values[6] = y * z * one_minus_cosine - x * sine;
    matrix.values[8] = z * x * one_minus_cosine - y * sine;
    matrix.values[9] = z * y * one_minus_cosine + x * sine;
    matrix.values[10] = cosine + z * z * one_minus_cosine;
    return matrix;
}

static RdtMatrix4 transform_function_matrix_3d(const TransformFunction* function,
                                               float width, float height) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    if (!function) return matrix;
    switch (function->type) {
        case TRANSFORM_TRANSLATE:
        case TRANSFORM_TRANSLATEX:
        case TRANSFORM_TRANSLATEY:
        case TRANSFORM_TRANSLATE3D: {
            bool is_3d_translate = function->type == TRANSFORM_TRANSLATE3D;
            float x = is_3d_translate ? function->params.translate3d.x
                                      : function->params.translate.x;
            float y = is_3d_translate ? function->params.translate3d.y
                                      : function->params.translate.y;
            if (!isnan(function->translate_x_percent)) {
                x = function->translate_x_percent * width / 100.0f;
            }
            if (!isnan(function->translate_y_percent)) {
                y = function->translate_y_percent * height / 100.0f;
            }
            matrix = rdt_matrix4_translate(
                x, y, is_3d_translate ? function->params.translate3d.z : 0.0f);
            break;
        }
        case TRANSFORM_TRANSLATEZ:
            matrix = rdt_matrix4_translate(0.0f, 0.0f,
                function->params.translate3d.z);
            break;
        case TRANSFORM_SCALE:
        case TRANSFORM_SCALEX:
        case TRANSFORM_SCALEY:
            matrix = matrix4_from_scale(function->params.scale.x,
                                        function->params.scale.y, 1.0f);
            break;
        case TRANSFORM_SCALE3D:
            matrix = matrix4_from_scale(function->params.scale3d.x,
                                        function->params.scale3d.y,
                                        function->params.scale3d.z);
            break;
        case TRANSFORM_SCALEZ:
            matrix = matrix4_from_scale(1.0f, 1.0f,
                                        function->params.scale3d.z);
            break;
        case TRANSFORM_ROTATE:
        case TRANSFORM_ROTATEZ:
            matrix = matrix4_from_rotate_z(function->params.angle);
            break;
        case TRANSFORM_ROTATEX:
            matrix = matrix4_from_rotate_x(function->params.angle);
            break;
        case TRANSFORM_ROTATEY:
            matrix = matrix4_from_rotate_y(function->params.angle);
            break;
        case TRANSFORM_ROTATE3D:
            matrix = matrix4_from_rotate3d(
                function->params.rotate3d.x,
                function->params.rotate3d.y,
                function->params.rotate3d.z,
                function->params.rotate3d.angle);
            break;
        case TRANSFORM_SKEW:
            matrix.values[1] = tanf(function->params.skew.x);
            matrix.values[4] = tanf(function->params.skew.y);
            break;
        case TRANSFORM_SKEWX:
            matrix.values[1] = tanf(function->params.angle);
            break;
        case TRANSFORM_SKEWY:
            matrix.values[4] = tanf(function->params.angle);
            break;
        case TRANSFORM_MATRIX: {
            matrix.values[0] = function->params.matrix.a;
            matrix.values[1] = function->params.matrix.c;
            matrix.values[3] = function->params.matrix.e;
            matrix.values[4] = function->params.matrix.b;
            matrix.values[5] = function->params.matrix.d;
            matrix.values[7] = function->params.matrix.f;
            break;
        }
        case TRANSFORM_MATRIX3D:
            // CSS matrix3d() arguments are column-major; the runtime matrix is row-major.
            for (int row = 0; row < 4; row++) {
                for (int column = 0; column < 4; column++) {
                    matrix.values[row * 4 + column] =
                        function->params.matrix3d[column * 4 + row];
                }
            }
            break;
        case TRANSFORM_PERSPECTIVE: {
            float distance = function->params.perspective;
            if (distance > 0.0f) matrix.values[14] = -1.0f / distance;
            break;
        }
        default:
            break;
    }
    return matrix;
}

static RdtMatrix4 compose_transform_matrix_3d(TransformFunction* functions,
                                             const TransformFunction* individual,
                                             float width, float height,
                                             float origin_x, float origin_y,
                                             float origin_z) {
    RdtMatrix4 result = rdt_matrix4_translate(origin_x, origin_y, origin_z);
    // individual properties precede the transform list inside the same origin pair.
    if (individual) {
        for (int i = 0; i < 3; i++) {
            RdtMatrix4 local = transform_function_matrix_3d(&individual[i], width, height);
            result = rdt_matrix4_multiply(&result, &local);
        }
    }
    for (TransformFunction* function = functions; function; function = function->next) {
        RdtMatrix4 local = transform_function_matrix_3d(function, width, height);
        result = rdt_matrix4_multiply(&result, &local);
    }
    RdtMatrix4 to_origin = rdt_matrix4_translate(-origin_x, -origin_y, -origin_z);
    return rdt_matrix4_multiply(&result, &to_origin);
}

RdtMatrix4 compute_transform_matrix_3d(TransformFunction* functions,
                                       float width, float height,
                                       float origin_x, float origin_y,
                                       float origin_z) {
    return compose_transform_matrix_3d(functions, nullptr, width, height,
                                      origin_x, origin_y, origin_z);
}

RdtMatrix4 compute_transform_matrix_3d(const TransformProp* transform,
                                       float width, float height,
                                       float origin_x, float origin_y,
                                       float origin_z) {
    return compose_transform_matrix_3d(transform ? transform->functions.get() : nullptr,
        transform ? transform->individual : nullptr, width, height, origin_x, origin_y, origin_z);
}

static RdtMatrix matrix4_project_to_2d(const RdtMatrix4* matrix) {
    RdtMatrix result = {};
    result.e11 = matrix->values[0];
    result.e12 = matrix->values[1];
    result.e13 = matrix->values[3];
    result.e21 = matrix->values[4];
    result.e22 = matrix->values[5];
    result.e23 = matrix->values[7];
    result.e31 = matrix->values[12];
    result.e32 = matrix->values[13];
    result.e33 = matrix->values[15];
    return result;
}

RdtMatrix4 compute_parent_perspective_matrix_3d(float distance,
                                                float origin_x, float origin_y) {
    if (distance <= 0.0f) return rdt_matrix4_identity();
    RdtMatrix4 perspective = rdt_matrix4_identity();
    perspective.values[14] = -1.0f / distance;
    RdtMatrix4 from_origin = rdt_matrix4_translate(origin_x, origin_y, 0.0f);
    RdtMatrix4 to_origin = rdt_matrix4_translate(-origin_x, -origin_y, 0.0f);
    RdtMatrix4 result = rdt_matrix4_multiply(&from_origin, &perspective);
    return rdt_matrix4_multiply(&result, &to_origin);
}

static RdtMatrix project_transform_matrix(RdtMatrix4 matrix, float perspective_distance,
                                          float perspective_origin_x, float perspective_origin_y) {
    RdtMatrix4 perspective = compute_parent_perspective_matrix_3d(
        perspective_distance, perspective_origin_x, perspective_origin_y);
    matrix = rdt_matrix4_multiply(&perspective, &matrix);
    return matrix4_project_to_2d(&matrix);
}

RdtMatrix compute_transform_matrix(TransformFunction* functions,
                                   float width, float height,
                                   float origin_x, float origin_y,
                                   float perspective_distance,
                                   float perspective_origin_x,
                                   float perspective_origin_y) {
    if (!functions) return rdt_matrix_identity();
    return project_transform_matrix(compute_transform_matrix_3d(
        functions, width, height, origin_x, origin_y), perspective_distance,
        perspective_origin_x, perspective_origin_y);
}

RdtMatrix compute_transform_matrix(const TransformProp* transform,
                                   float width, float height,
                                   float origin_x, float origin_y,
                                   float perspective_distance,
                                   float perspective_origin_x,
                                   float perspective_origin_y) {
    if (!transform_has_functions(transform)) return rdt_matrix_identity();
    return project_transform_matrix(compute_transform_matrix_3d(
        transform, width, height, origin_x, origin_y, transform->origin_z), perspective_distance,
        perspective_origin_x, perspective_origin_y);
}

bool has_transform(DomElement* elem) {
    return elem && transform_has_functions(elem->transform);
}

bool transform_preserves_3d(DomElement* elem) {
    if (!elem || elem->transformp()->transform_style != CSS_VALUE_PRESERVE_3D) return false;
    const ScrollProp* scroll = elem->scroll();
    const InlineProp* inline_prop = elem->inl();
    // grouping effects flatten descendants while preserving the authored computed keyword.
    return (scroll->overflow_x == CSS_VALUE_VISIBLE || scroll->overflow_x == CSS_VALUE_CLIP) &&
        (scroll->overflow_y == CSS_VALUE_VISIBLE || scroll->overflow_y == CSS_VALUE_CLIP) &&
        !(inline_prop->opacity >= 0.0f && inline_prop->opacity < 1.0f) &&
        (!inline_prop->mix_blend_mode || inline_prop->mix_blend_mode == CSS_VALUE_NORMAL) &&
        !elem->filterp()->functions &&
        !(elem->boundary()->mask && elem->boundary()->mask->has_radial_gradient) &&
        !elem->block()->contain_paint && !elem->block()->content_visibility_hidden;
}

bool transform_establishes_containing_block(DomElement* elem) {
    if (!elem) return false;
    const TransformProp* transform = elem->transformp();
    if (has_transform(elem) || transform->perspective > 0.0f ||
        transform->transform_style == CSS_VALUE_PRESERVE_3D) return true;
    ViewElement* parent = elem->parent_view();
    // hidden backfaces establish a containing block when the element participates in a 3D context.
    return transform->backface_visibility == CSS_VALUE_HIDDEN && parent &&
        transform_preserves_3d(parent);
}

void transform_point(float& x, float& y, const RdtMatrix& m) {
    float transformed_x = x;
    float transformed_y = y;
    if (rdt_matrix_project_point(&m, x, y,
                                 &transformed_x, &transformed_y)) {
        x = transformed_x;
        y = transformed_y;
    }
}

} // namespace radiant
