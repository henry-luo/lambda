#include "view.hpp"

#include <math.h>

namespace radiant {

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
    return elem && elem->transform && elem->transform->functions;
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
