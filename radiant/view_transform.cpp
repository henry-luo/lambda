#include "view.hpp"

#include <math.h>

namespace radiant {

static RdtMatrix4 matrix4_from_scale(float x, float y, float z) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    matrix.values[0] = x;
    matrix.values[5] = y;
    matrix.values[10] = z;
    return matrix;
}

static RdtMatrix4 matrix4_from_rotate_x(float angle) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    float cosine = cosf(angle);
    float sine = sinf(angle);
    matrix.values[5] = cosine;
    matrix.values[6] = -sine;
    matrix.values[9] = sine;
    matrix.values[10] = cosine;
    return matrix;
}

static RdtMatrix4 matrix4_from_rotate_y(float angle) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    float cosine = cosf(angle);
    float sine = sinf(angle);
    matrix.values[0] = cosine;
    matrix.values[2] = sine;
    matrix.values[8] = -sine;
    matrix.values[10] = cosine;
    return matrix;
}

static RdtMatrix4 matrix4_from_rotate_z(float angle) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    float cosine = cosf(angle);
    float sine = sinf(angle);
    matrix.values[0] = cosine;
    matrix.values[1] = -sine;
    matrix.values[4] = sine;
    matrix.values[5] = cosine;
    return matrix;
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

static RdtMatrix4 transform_function_matrix_3d(TransformFunction* function,
                                               float width, float height) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    if (!function) return matrix;
    switch (function->type) {
        case TRANSFORM_TRANSLATE:
        case TRANSFORM_TRANSLATEX:
        case TRANSFORM_TRANSLATEY: {
            float x = function->params.translate.x;
            float y = function->params.translate.y;
            if (!isnan(function->translate_x_percent)) {
                x = function->translate_x_percent * width / 100.0f;
            }
            if (!isnan(function->translate_y_percent)) {
                y = function->translate_y_percent * height / 100.0f;
            }
            matrix = rdt_matrix4_translate(x, y, 0.0f);
            break;
        }
        case TRANSFORM_TRANSLATE3D:
            matrix = rdt_matrix4_translate(
                function->params.translate3d.x,
                function->params.translate3d.y,
                function->params.translate3d.z);
            break;
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

RdtMatrix compute_transform_matrix(TransformFunction* functions,
                                   float width, float height,
                                   float origin_x, float origin_y,
                                   float perspective_distance,
                                   float perspective_origin_x,
                                   float perspective_origin_y) {
    RdtMatrix result = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };

    if (!functions) return result;

    if (perspective_distance <= 0.0f) {
        RdtMatrix4 matrix = compute_transform_matrix_3d(
            functions, width, height, origin_x, origin_y);
        return matrix4_project_to_2d(&matrix);
    }

    if (perspective_distance > 0.0f) {
        bool has_projected_3d = false;
        for (TransformFunction* tf = functions; tf; tf = tf->next) {
            if (tf->type == TRANSFORM_ROTATEX || tf->type == TRANSFORM_ROTATEY ||
                tf->type == TRANSFORM_TRANSLATEZ || tf->type == TRANSFORM_TRANSLATE3D) {
                has_projected_3d = true;
                break;
            }
        }
        if (has_projected_3d) {
            TransformFunction* stack[64];
            int stack_count = 0;
            for (TransformFunction* tf = functions; tf && stack_count < 64; tf = tf->next) {
                stack[stack_count++] = tf;
            }

            struct Point3D {
                float x;
                float y;
                float z;
            };
            auto apply_function = [&](Point3D p, TransformFunction* tf) -> Point3D {
                switch (tf->type) {
                    case TRANSFORM_ROTATEY: {
                        float c = cosf(tf->params.angle);
                        float s = sinf(tf->params.angle);
                        Point3D out = { c * p.x + s * p.z, p.y, -s * p.x + c * p.z };
                        return out;
                    }
                    case TRANSFORM_ROTATEX: {
                        float c = cosf(tf->params.angle);
                        float s = sinf(tf->params.angle);
                        Point3D out = { p.x, c * p.y - s * p.z, s * p.y + c * p.z };
                        return out;
                    }
                    case TRANSFORM_TRANSLATE:
                    case TRANSFORM_TRANSLATEX:
                    case TRANSFORM_TRANSLATEY: {
                        float dx = tf->params.translate.x;
                        float dy = tf->params.translate.y;
                        if (!isnan(tf->translate_x_percent)) dx = tf->translate_x_percent * width / 100.0f;
                        if (!isnan(tf->translate_y_percent)) dy = tf->translate_y_percent * height / 100.0f;
                        Point3D out = { p.x + dx, p.y + dy, p.z };
                        return out;
                    }
                    case TRANSFORM_TRANSLATE3D: {
                        Point3D out = {
                            p.x + tf->params.translate3d.x,
                            p.y + tf->params.translate3d.y,
                            p.z + tf->params.translate3d.z
                        };
                        return out;
                    }
                    case TRANSFORM_TRANSLATEZ: {
                        Point3D out = { p.x, p.y, p.z + tf->params.translate3d.z };
                        return out;
                    }
                    default:
                        return p;
                }
            };

            float x0 = origin_x - width * 0.5f;
            float y0 = origin_y - height * 0.5f;
            float x1 = x0 + width;
            float y1 = y0 + height;
            float src_x[4] = { x0, x1, x1, x0 };
            float src_y[4] = { y0, y0, y1, y1 };
            float dst_x[4];
            float dst_y[4];
            float px = perspective_origin_x;
            float py = perspective_origin_y;

            for (int i = 0; i < 4; i++) {
                Point3D p = { src_x[i] - origin_x, src_y[i] - origin_y, 0.0f };
                for (int j = stack_count - 1; j >= 0; j--) {
                    p = apply_function(p, stack[j]);
                }
                float wx = origin_x + p.x;
                float wy = origin_y + p.y;
                float scale = perspective_distance / (perspective_distance - p.z);
                dst_x[i] = px + (wx - px) * scale;
                dst_y[i] = py + (wy - py) * scale;
            }

            float dx1 = dst_x[1] - dst_x[2];
            float dy1 = dst_y[1] - dst_y[2];
            float dx2 = dst_x[3] - dst_x[2];
            float dy2 = dst_y[3] - dst_y[2];
            float sx = dst_x[0] - dst_x[1] + dst_x[2] - dst_x[3];
            float sy = dst_y[0] - dst_y[1] + dst_y[2] - dst_y[3];
            float denom = dx1 * dy2 - dx2 * dy1;
            float g = 0.0f;
            float h = 0.0f;
            if (fabsf(denom) > 0.0001f) {
                g = (sx * dy2 - dx2 * sy) / denom;
                h = (dx1 * sy - sx * dy1) / denom;
            }

            RdtMatrix unit_to_quad = {
                dst_x[1] - dst_x[0] + g * dst_x[1],
                dst_x[3] - dst_x[0] + h * dst_x[3],
                dst_x[0],
                dst_y[1] - dst_y[0] + g * dst_y[1],
                dst_y[3] - dst_y[0] + h * dst_y[3],
                dst_y[0],
                g, h, 1.0f
            };
            RdtMatrix rect_to_unit = {
                1.0f / width, 0.0f, -x0 / width,
                0.0f, 1.0f / height, -y0 / height,
                0.0f, 0.0f, 1.0f
            };
            return rdt_matrix_multiply(&unit_to_quad, &rect_to_unit);
        }
    }

    RdtMatrix to_origin = {
        1.0f, 0.0f, -origin_x,
        0.0f, 1.0f, -origin_y,
        0.0f, 0.0f, 1.0f
    };

    RdtMatrix from_origin = {
        1.0f, 0.0f, origin_x,
        0.0f, 1.0f, origin_y,
        0.0f, 0.0f, 1.0f
    };

    result = from_origin;

    float active_perspective = perspective_distance;
    for (TransformFunction* tf = functions; tf; tf = tf->next) {
        RdtMatrix m = {
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f
        };

        switch (tf->type) {
            case TRANSFORM_TRANSLATE:
            case TRANSFORM_TRANSLATEX:
            case TRANSFORM_TRANSLATEY: {
                float tx = tf->params.translate.x;
                float ty = tf->params.translate.y;
                if (!isnan(tf->translate_x_percent)) {
                    tx = tf->translate_x_percent * width / 100.0f;
                }
                if (!isnan(tf->translate_y_percent)) {
                    ty = tf->translate_y_percent * height / 100.0f;
                }
                m.e13 = tx;
                m.e23 = ty;
                break;
            }

            case TRANSFORM_TRANSLATE3D:
            case TRANSFORM_TRANSLATEZ:
                m.e13 = tf->params.translate3d.x;
                m.e23 = tf->params.translate3d.y;
                break;

            case TRANSFORM_SCALE:
            case TRANSFORM_SCALEX:
            case TRANSFORM_SCALEY:
                m.e11 = tf->params.scale.x;
                m.e22 = tf->params.scale.y;
                break;

            case TRANSFORM_SCALE3D:
            case TRANSFORM_SCALEZ:
                m.e11 = tf->params.scale3d.x;
                m.e22 = tf->params.scale3d.y;
                break;

            case TRANSFORM_ROTATE:
            case TRANSFORM_ROTATEZ: {
                float cos_a = cosf(tf->params.angle);
                float sin_a = sinf(tf->params.angle);
                m.e11 = cos_a;  m.e12 = -sin_a;
                m.e21 = sin_a;  m.e22 = cos_a;
                break;
            }

            case TRANSFORM_ROTATEX: {
                float cos_a = cosf(tf->params.angle);
                if (active_perspective > 0.0f) {
                    float sin_a = sinf(tf->params.angle);
                    m.e22 = cos_a;
                    m.e12 = -sin_a * 0.08f;
                    break;
                }
                m.e22 = cos_a;
                break;
            }

            case TRANSFORM_ROTATEY: {
                float cos_a = cosf(tf->params.angle);
                if (active_perspective > 0.0f) {
                    float sin_a = sinf(tf->params.angle);
                    m.e11 = cos_a;
                    m.e31 = sin_a / active_perspective;
                    break;
                }
                m.e11 = cos_a;
                break;
            }

            case TRANSFORM_SKEW:
                m.e12 = tanf(tf->params.skew.x);
                m.e21 = tanf(tf->params.skew.y);
                break;

            case TRANSFORM_SKEWX:
                m.e12 = tanf(tf->params.angle);
                break;

            case TRANSFORM_SKEWY:
                m.e21 = tanf(tf->params.angle);
                break;

            case TRANSFORM_MATRIX:
                m.e11 = tf->params.matrix.a;
                m.e12 = tf->params.matrix.c;
                m.e13 = tf->params.matrix.e;
                m.e21 = tf->params.matrix.b;
                m.e22 = tf->params.matrix.d;
                m.e23 = tf->params.matrix.f;
                break;

            case TRANSFORM_PERSPECTIVE:
                active_perspective = tf->params.perspective;
                break;

            case TRANSFORM_ROTATE3D: {
                float x = tf->params.rotate3d.x;
                float y = tf->params.rotate3d.y;
                float z = tf->params.rotate3d.z;
                float len = sqrtf(x*x + y*y + z*z);
                if (len > 0.001f) {
                    x /= len; y /= len; z /= len;
                    if (fabsf(z) > 0.9f) {
                        float cos_a = cosf(tf->params.rotate3d.angle);
                        float sin_a = sinf(tf->params.rotate3d.angle);
                        if (z < 0) sin_a = -sin_a;
                        m.e11 = cos_a;  m.e12 = -sin_a;
                        m.e21 = sin_a;  m.e22 = cos_a;
                    }
                }
                break;
            }

            case TRANSFORM_MATRIX3D:
                m.e11 = tf->params.matrix3d[0];
                m.e12 = tf->params.matrix3d[4];
                m.e13 = tf->params.matrix3d[12];
                m.e21 = tf->params.matrix3d[1];
                m.e22 = tf->params.matrix3d[5];
                m.e23 = tf->params.matrix3d[13];
                break;

            default:
                break;
        }

        result = rdt_matrix_multiply(&result, &m);
    }

    result = rdt_matrix_multiply(&result, &to_origin);

    return result;
}

bool has_transform(DomElement* elem) {
    return elem && elem->transform && elem->transformp()->functions;
}

void transform_point(float& x, float& y, const RdtMatrix& m) {
    float w = m.e31 * x + m.e32 * y + m.e33;
    if (fabsf(w) < 0.0001f) w = 1.0f;
    float new_x = (m.e11 * x + m.e12 * y + m.e13) / w;
    float new_y = (m.e21 * x + m.e22 * y + m.e23) / w;
    x = new_x;
    y = new_y;
}

} // namespace radiant
