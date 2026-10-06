#include "view.hpp"
#include "../lambda/input/css/css_style_node.hpp"

#include <math.h>

namespace radiant {

static void destroy_transform_length_terms(Pool* pool, TransformLengthTerm* terms) {
    while (terms) {
        TransformLengthTerm* next = terms->next;
        css_value_destroy_owned(terms->expression, pool);
        pool_free(pool, terms);
        terms = next;
    }
}

TransformLengthTerm* clone_transform_length_terms(Pool* pool, const TransformLengthTerm* source,
                                                   float coefficient) {
    TransformLengthTerm* head = nullptr;
    TransformLengthTerm* tail = nullptr;
    for (; source; source = source->next) {
        TransformLengthTerm* copy = (TransformLengthTerm*)pool_calloc(pool, sizeof(TransformLengthTerm));
        if (!copy) { destroy_transform_length_terms(pool, head); return nullptr; }
        copy->coefficient = source->coefficient * coefficient;
        copy->expression = lam::own(css_value_clone_owned(source->expression, pool));
        if (source->expression && !copy->expression) {
            pool_free(pool, copy);
            destroy_transform_length_terms(pool, head);
            return nullptr;
        }
        if (tail) tail->next = lam::own(copy);
        else head = copy;
        tail = copy;
    }
    return head;
}

TransformLengthTerm* interpolate_transform_length_terms(Pool* pool, const TransformLengthTerm* from,
                                                        const TransformLengthTerm* to, float progress) {
    TransformLengthTerm* first = clone_transform_length_terms(pool, from, 1.0f - progress);
    TransformLengthTerm* second = clone_transform_length_terms(pool, to, progress);
    if ((from && !first) || (to && !second)) {
        destroy_transform_length_terms(pool, first);
        destroy_transform_length_terms(pool, second);
        return nullptr;
    }
    if (!first) return second;
    TransformLengthTerm* tail = first;
    while (tail->next) tail = tail->next;
    tail->next = lam::own(second);
    return first;
}

void destroy_transform_function_payload(Pool* pool, TransformFunction* function) {
    if (!function) return;
    for (auto& terms : function->translate_math) {
        destroy_transform_length_terms(pool, terms);
        terms = nullptr;
    }
    if (function->matrix_interpolation) {
        destroy_transform_list(pool, function->matrix_interpolation->from);
        destroy_transform_list(pool, function->matrix_interpolation->to);
        pool_free(pool, function->matrix_interpolation);
        function->matrix_interpolation = nullptr;
    }
}

void destroy_transform_list(Pool* pool, TransformFunction* functions) {
    while (functions) {
        TransformFunction* next = functions->next;
        destroy_transform_function_payload(pool, functions);
        pool_free(pool, functions);
        functions = next;
    }
}

TransformFunction* clone_transform_function(Pool* pool, const TransformFunction* source) {
    if (!source) return nullptr;
    TransformFunction* copy = (TransformFunction*)pool_calloc(pool, sizeof(TransformFunction));
    if (!copy) return nullptr;
    *copy = *source;
    copy->next = nullptr;
    copy->matrix_interpolation = nullptr;
    for (auto& terms : copy->translate_math) terms = nullptr;
    for (int axis = 0; axis < 2; axis++) {
        copy->translate_math[axis] = lam::own(clone_transform_length_terms(pool, source->translate_math[axis]));
        if (source->translate_math[axis] && !copy->translate_math[axis]) goto failed;
    }
    if (source->matrix_interpolation) {
        copy->matrix_interpolation = lam::own((TransformMatrixInterpolation*)pool_calloc(pool, sizeof(TransformMatrixInterpolation)));
        if (!copy->matrix_interpolation) goto failed;
        copy->matrix_interpolation->progress = source->matrix_interpolation->progress;
        copy->matrix_interpolation->from = lam::own(clone_transform_list(pool, source->matrix_interpolation->from));
        copy->matrix_interpolation->to = lam::own(clone_transform_list(pool, source->matrix_interpolation->to));
        if ((source->matrix_interpolation->from && !copy->matrix_interpolation->from) ||
            (source->matrix_interpolation->to && !copy->matrix_interpolation->to)) goto failed;
    }
    return copy;
failed:
    destroy_transform_list(pool, copy);
    return nullptr;
}

TransformFunction* clone_transform_list(Pool* pool, const TransformFunction* source) {
    TransformFunction* head = nullptr;
    TransformFunction* tail = nullptr;
    for (; source; source = source->next) {
        TransformFunction* copy = clone_transform_function(pool, source);
        if (!copy) { destroy_transform_list(pool, head); return nullptr; }
        if (tail) tail->next = lam::own(copy);
        else head = copy;
        tail = copy;
    }
    return head;
}

static float transform_translate_math(const TransformFunction* function, int axis, float extent) {
    float result = 0.0f;
    for (const TransformLengthTerm* term = function->translate_math[axis]; term; term = term->next) {
        if (term->coefficient != 0.0f)
            result += term->coefficient * resolve_computed_transform_length(term->expression, extent);
    }
    return result;
}

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

float normalize_transform_vector3(float vector[3]) {
    float magnitude = fmaxf(fabsf(vector[0]), fmaxf(fabsf(vector[1]), fabsf(vector[2])));
    if (magnitude == 0.0f || !isfinite(magnitude)) return magnitude;
    // scale first so tiny or large nonzero rotation axes remain normalizable.
    float scaled[3] = {vector[0] / magnitude, vector[1] / magnitude, vector[2] / magnitude};
    float length = hypotf(hypotf(scaled[0], scaled[1]), scaled[2]);
    for (int axis = 0; axis < 3; axis++) vector[axis] = scaled[axis] / length;
    return magnitude * length;
}

static RdtMatrix4 matrix4_from_rotate3d(float x, float y, float z,
                                        float angle) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    float axis[3] = {x, y, z};
    if (normalize_transform_vector3(axis) == 0.0f) return matrix;
    x = axis[0]; y = axis[1]; z = axis[2];
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

static RdtMatrix4 transform_function_matrix_3d(TransformFunction* function,
                                               float width, float height) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    if (!function) return matrix;
    if (function->matrix_interpolation) {
        // percentage-dependent suffixes become matrices only after reference-box sizing.
        const TransformMatrixInterpolation* sample = function->matrix_interpolation;
        RdtMatrix4 from = compute_transform_matrix_3d(sample->from, width, height, 0.0f, 0.0f);
        RdtMatrix4 to = compute_transform_matrix_3d(sample->to, width, height, 0.0f, 0.0f);
        if (interpolate_transform_matrix(&from, &to, sample->progress, &matrix)) return matrix;
        return sample->progress < .5f ? from : to;
    }
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
            x = transform_translate_component(x, function->translate_x_percent, width);
            y = transform_translate_component(y, function->translate_y_percent, height);
            x += transform_translate_math(function, 0, width);
            y += transform_translate_math(function, 1, height);
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
            matrix.values[14] = -1.0f / fmaxf(1.0f, distance);
            break;
        }
        default:
            break;
    }
    return matrix;
}

RdtMatrix4 compute_transform_matrix_3d(TransformFunction* functions,
                                       float width, float height,
                                       float origin_x, float origin_y,
                                       float origin_z) {
    RdtMatrix4 result = rdt_matrix4_translate(origin_x, origin_y, origin_z);
    for (TransformFunction* function = functions; function; function = function->next) {
        RdtMatrix4 local = transform_function_matrix_3d(function, width, height);
        result = rdt_matrix4_multiply(&result, &local);
    }
    RdtMatrix4 to_origin = rdt_matrix4_translate(-origin_x, -origin_y, -origin_z);
    return rdt_matrix4_multiply(&result, &to_origin);
}

struct TransformMatrix2dComponents {
    float translation[2], scale[2], angle, residual[4];
};

template <size_t count>
static void interpolate_transform_array(const float (&from)[count], const float (&to)[count],
                                         float progress, float (&result)[count]) {
    for (size_t component = 0; component < count; component++)
        result[component] = css_interpolate_float(from[component], to[component], progress);
}

static bool decompose_transform_matrix_2d(const RdtMatrix4* matrix,
                                          TransformMatrix2dComponents* result) {
    const float* values = matrix->values;
    float a = values[0], b = values[4], c = values[1], d = values[5];
    float determinant = a * d - b * c;
    if (!isfinite(determinant) || determinant == 0.0f) return false;
    result->translation[0] = values[3];
    result->translation[1] = values[7];
    result->scale[0] = hypotf(a, b);
    result->scale[1] = hypotf(c, d);
    // CSS Transforms 1 §13.2 keeps reflection signs before removing rotation.
    if (determinant < 0.0f) {
        if (a < d) result->scale[0] = -result->scale[0];
        else result->scale[1] = -result->scale[1];
    }
    a /= result->scale[0]; b /= result->scale[0];
    c /= result->scale[1]; d /= result->scale[1];
    result->angle = atan2f(b, a);
    result->residual[0] = a * a + b * b;
    result->residual[1] = a * c + b * d;
    result->residual[2] = -b * a + a * b;
    result->residual[3] = -b * c + a * d;
    return true;
}

bool interpolate_transform_matrix_2d(const RdtMatrix4* from, const RdtMatrix4* to,
                                     float progress, RdtMatrix4* result) {
    if (!from || !to || !result || !rdt_matrix4_is_2d(from) || !rdt_matrix4_is_2d(to)) return false;
    TransformMatrix2dComponents a, b, sampled;
    if (!decompose_transform_matrix_2d(from, &a) || !decompose_transform_matrix_2d(to, &b)) return false;
    float pi = acosf(-1.0f);
    if ((a.scale[0] < 0.0f && b.scale[1] < 0.0f) ||
        (a.scale[1] < 0.0f && b.scale[0] < 0.0f)) {
        a.scale[0] = -a.scale[0]; a.scale[1] = -a.scale[1];
        a.angle += a.angle < 0.0f ? pi : -pi;
    }
    // Equivalent zero/full-turn angles need no artificial rotation to choose the shortest arc.
    if (b.angle - a.angle > pi) b.angle -= 2.0f * pi;
    else if (a.angle - b.angle > pi) a.angle -= 2.0f * pi;
    interpolate_transform_array(a.translation, b.translation, progress, sampled.translation);
    interpolate_transform_array(a.scale, b.scale, progress, sampled.scale);
    interpolate_transform_array(a.residual, b.residual, progress, sampled.residual);
    sampled.angle = css_interpolate_float(a.angle, b.angle, progress);
    float cosine = cosf(sampled.angle), sine = sinf(sampled.angle);
    *result = rdt_matrix4_identity();
    result->values[0] = (cosine * sampled.residual[0] - sine * sampled.residual[2]) * sampled.scale[0];
    result->values[4] = (sine * sampled.residual[0] + cosine * sampled.residual[2]) * sampled.scale[0];
    result->values[1] = (cosine * sampled.residual[1] - sine * sampled.residual[3]) * sampled.scale[1];
    result->values[5] = (sine * sampled.residual[1] + cosine * sampled.residual[3]) * sampled.scale[1];
    result->values[3] = sampled.translation[0];
    result->values[7] = sampled.translation[1];
    return true;
}

struct TransformMatrix3dComponents {
    float translation[3], scale[3], skew[3], perspective[4], quaternion[4];
};

static float transform_vector3_dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void transform_vector3_cross(const float a[3], const float b[3], float result[3]) {
    for (int axis = 0; axis < 3; axis++) {
        int next = (axis + 1) % 3, last = (axis + 2) % 3;
        result[axis] = a[next] * b[last] - a[last] * b[next];
    }
}

static void normalize_transform_quaternion(float quaternion[4]) {
    float length = hypotf(hypotf(quaternion[0], quaternion[1]), hypotf(quaternion[2], quaternion[3]));
    for (int component = 0; component < 4; component++) quaternion[component] /= length;
}

static bool decompose_transform_matrix_3d(const RdtMatrix4* matrix,
                                          TransformMatrix3dComponents* result) {
    if (!isfinite(matrix->values[15]) || matrix->values[15] == 0.0f) return false;
    RdtMatrix4 normalized;
    for (int index = 0; index < 16; index++) {
        normalized.values[index] = matrix->values[index] / matrix->values[15];
        if (!isfinite(normalized.values[index])) return false;
    }
    float columns[3][3], cofactors[3][3];
    for (int column = 0; column < 3; column++) {
        result->translation[column] = normalized.values[column * 4 + 3];
        for (int row = 0; row < 3; row++) columns[column][row] = normalized.values[row * 4 + column];
    }
    for (int column = 0; column < 3; column++)
        transform_vector3_cross(columns[(column + 1) % 3], columns[(column + 2) % 3], cofactors[column]);
    float determinant = transform_vector3_dot(columns[0], cofactors[0]);
    if (!isfinite(determinant) || determinant == 0.0f) return false;
    // the affine inverse transpose isolates perspective without a general 4x4 inverse.
    for (int axis = 0; axis < 3; axis++) {
        result->perspective[axis] = 0.0f;
        for (int column = 0; column < 3; column++)
            result->perspective[axis] += cofactors[column][axis] * normalized.values[12 + column] / determinant;
    }
    result->perspective[3] = 1.0f - transform_vector3_dot(result->perspective, result->translation);
    // orthogonalization separates the three ordered shears from scale and rotation.
    for (int column = 0; column < 3; column++) {
        for (int prior = 0; prior < column; prior++) {
            float projection = transform_vector3_dot(columns[prior], columns[column]);
            result->skew[column + prior - 1] = projection;
            for (int axis = 0; axis < 3; axis++) columns[column][axis] -= projection * columns[prior][axis];
        }
        result->scale[column] = normalize_transform_vector3(columns[column]);
        if (!isfinite(result->scale[column]) || result->scale[column] == 0.0f) return false;
        for (int prior = 0; prior < column; prior++) result->skew[column + prior - 1] /= result->scale[column];
    }
    float orientation[3];
    transform_vector3_cross(columns[1], columns[2], orientation);
    if (transform_vector3_dot(columns[0], orientation) < 0.0f) {
        for (int column = 0; column < 3; column++) {
            result->scale[column] = -result->scale[column];
            for (int axis = 0; axis < 3; axis++) columns[column][axis] = -columns[column][axis];
        }
    }
    float trace = columns[0][0] + columns[1][1] + columns[2][2];
    if (trace > 0.0f) {
        float divisor = 2.0f * sqrtf(1.0f + trace);
        result->quaternion[3] = .25f * divisor;
        for (int axis = 0; axis < 3; axis++) {
            int next = (axis + 1) % 3, last = (axis + 2) % 3;
            result->quaternion[axis] = (columns[next][last] - columns[last][next]) / divisor;
        }
    } else {
        // choose the largest diagonal so half-turn axes retain their relative signs.
        int axis = 2;
        for (int candidate = 1; candidate >= 0; candidate--)
            if (columns[candidate][candidate] > columns[axis][axis]) axis = candidate;
        int next = (axis + 1) % 3, last = (axis + 2) % 3;
        float divisor = 2.0f * sqrtf(1.0f + columns[axis][axis] - columns[next][next] - columns[last][last]);
        result->quaternion[axis] = .25f * divisor;
        result->quaternion[next] = (columns[axis][next] + columns[next][axis]) / divisor;
        result->quaternion[last] = (columns[axis][last] + columns[last][axis]) / divisor;
        result->quaternion[3] = (columns[next][last] - columns[last][next]) / divisor;
    }
    normalize_transform_quaternion(result->quaternion);
    return true;
}

static void interpolate_transform_quaternion(const float from[4], const float to[4],
                                              float progress, float result[4]) {
    float product = 0.0f;
    for (int component = 0; component < 4; component++) product += from[component] * to[component];
    // q and -q describe the same orientation; keep interpolation on the shorter arc.
    float orientation = product < 0.0f ? -1.0f : 1.0f;
    product = fminf(fabsf(product), 1.0f);
    float from_weight = 1.0f - progress, to_weight = progress;
    if (product < 1.0f - .000001f) {
        float angle = acosf(product), denominator = sinf(angle);
        from_weight = sinf((1.0f - progress) * angle) / denominator;
        to_weight = sinf(progress * angle) / denominator;
    }
    for (int component = 0; component < 4; component++)
        result[component] = from_weight * from[component] + to_weight * orientation * to[component];
    normalize_transform_quaternion(result);
}

static RdtMatrix4 recompose_transform_matrix_3d(const TransformMatrix3dComponents* sample) {
    RdtMatrix4 result = rdt_matrix4_identity();
    for (int component = 0; component < 4; component++) result.values[12 + component] = sample->perspective[component];
    RdtMatrix4 translation = rdt_matrix4_translate(sample->translation[0], sample->translation[1], sample->translation[2]);
    result = rdt_matrix4_multiply(&result, &translation);
    RdtMatrix4 rotation = rdt_matrix4_identity();
    const float* q = sample->quaternion;
    for (int axis = 0; axis < 3; axis++) {
        int next = (axis + 1) % 3, last = (axis + 2) % 3;
        rotation.values[axis * 4 + axis] = 1.0f - 2.0f * (q[next] * q[next] + q[last] * q[last]);
        rotation.values[axis * 4 + next] = 2.0f * (q[axis] * q[next] - q[last] * q[3]);
        rotation.values[axis * 4 + last] = 2.0f * (q[axis] * q[last] + q[next] * q[3]);
    }
    result = rdt_matrix4_multiply(&result, &rotation);
    RdtMatrix4 shear = rdt_matrix4_identity();
    shear.values[1] = sample->skew[0];
    shear.values[2] = sample->skew[1];
    shear.values[6] = sample->skew[2];
    result = rdt_matrix4_multiply(&result, &shear);
    for (int column = 0; column < 3; column++)
        for (int row = 0; row < 4; row++) result.values[row * 4 + column] *= sample->scale[column];
    return result;
}

bool interpolate_transform_matrix(const RdtMatrix4* from, const RdtMatrix4* to,
                                   float progress, RdtMatrix4* result) {
    if (!from || !to || !result) return false;
    if (rdt_matrix4_is_2d(from) && rdt_matrix4_is_2d(to))
        return interpolate_transform_matrix_2d(from, to, progress, result);
    TransformMatrix3dComponents a, b, sampled;
    if (!decompose_transform_matrix_3d(from, &a) || !decompose_transform_matrix_3d(to, &b)) return false;
    interpolate_transform_array(a.translation, b.translation, progress, sampled.translation);
    interpolate_transform_array(a.scale, b.scale, progress, sampled.scale);
    interpolate_transform_array(a.skew, b.skew, progress, sampled.skew);
    interpolate_transform_array(a.perspective, b.perspective, progress, sampled.perspective);
    interpolate_transform_quaternion(a.quaternion, b.quaternion, progress, sampled.quaternion);
    *result = recompose_transform_matrix_3d(&sampled);
    return true;
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
    if (result.e31 == 0.0f && result.e32 == 0.0f && result.e33 > 0.0f && result.e33 != 1.0f) {
        // a constant positive homogeneous divisor is affine; exports need its actual scale.
        float reciprocal = 1.0f / result.e33;
        result = {result.e11 * reciprocal, result.e12 * reciprocal, result.e13 * reciprocal,
            result.e21 * reciprocal, result.e22 * reciprocal, result.e23 * reciprocal, 0.0f, 0.0f, 1.0f};
    }
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

RdtMatrix compute_transform_matrix(TransformFunction* functions,
                                   float width, float height,
                                   float origin_x, float origin_y,
                                   float perspective_distance,
                                   float perspective_origin_x,
                                   float perspective_origin_y) {
    if (!functions) return rdt_matrix_identity();
    RdtMatrix4 matrix = compute_transform_matrix_3d(
        functions, width, height, origin_x, origin_y);
    RdtMatrix4 perspective = compute_parent_perspective_matrix_3d(
        perspective_distance, perspective_origin_x, perspective_origin_y);
    matrix = rdt_matrix4_multiply(&perspective, &matrix);
    return matrix4_project_to_2d(&matrix);
}

bool has_transform(DomElement* elem) {
    return elem && elem->transform && elem->transformp()->functions;
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
