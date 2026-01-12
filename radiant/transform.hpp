/**
 * transform.hpp - CSS Transform utilities for Radiant Layout Engine
 *
 * Provides functions to:
 * 1. Compute combined transform matrix from TransformFunction chain
 * 2. Apply transform matrix to ThorVG paint objects
 * 3. Convert between coordinate systems
 */

#ifndef RADIANT_TRANSFORM_HPP
#define RADIANT_TRANSFORM_HPP

#include "view.hpp"
#include <thorvg_capi.h>
#include <cmath>

namespace radiant {

/**
 * Compute the combined 3x3 affine transformation matrix from a chain of transform functions.
 * The matrix is in ThorVG format:
 *   [e11 e12 e13]   [a  c  tx]
 *   [e21 e22 e23] = [b  d  ty]
 *   [e31 e32 e33]   [0  0  1 ]
 *
 * @param functions Linked list of TransformFunction
 * @param width Element width (for percentage-based origins)
 * @param height Element height (for percentage-based origins)
 * @param origin_x Transform origin X
 * @param origin_y Transform origin Y
 * @return Combined transform matrix
 */
inline Tvg_Matrix compute_transform_matrix(TransformFunction* functions,
                                           float width, float height,
                                           float origin_x, float origin_y) {
    // Start with identity matrix
    Tvg_Matrix result = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };

    if (!functions) return result;

    // Matrix multiplication helper: result = a * b
    auto matrix_multiply = [](const Tvg_Matrix& a, const Tvg_Matrix& b) -> Tvg_Matrix {
        return {
            a.e11 * b.e11 + a.e12 * b.e21 + a.e13 * b.e31,
            a.e11 * b.e12 + a.e12 * b.e22 + a.e13 * b.e32,
            a.e11 * b.e13 + a.e12 * b.e23 + a.e13 * b.e33,

            a.e21 * b.e11 + a.e22 * b.e21 + a.e23 * b.e31,
            a.e21 * b.e12 + a.e22 * b.e22 + a.e23 * b.e32,
            a.e21 * b.e13 + a.e22 * b.e23 + a.e23 * b.e33,

            a.e31 * b.e11 + a.e32 * b.e21 + a.e33 * b.e31,
            a.e31 * b.e12 + a.e32 * b.e22 + a.e33 * b.e32,
            a.e31 * b.e13 + a.e32 * b.e23 + a.e33 * b.e33
        };
    };

    // Translate to origin
    Tvg_Matrix to_origin = {
        1.0f, 0.0f, -origin_x,
        0.0f, 1.0f, -origin_y,
        0.0f, 0.0f, 1.0f
    };

    // Translate back from origin
    Tvg_Matrix from_origin = {
        1.0f, 0.0f, origin_x,
        0.0f, 1.0f, origin_y,
        0.0f, 0.0f, 1.0f
    };

    // Apply transformations in reverse order (to_origin, transforms..., from_origin)
    result = to_origin;

    for (TransformFunction* tf = functions; tf; tf = tf->next) {
        Tvg_Matrix m = {
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f
        };

        switch (tf->type) {
            case TRANSFORM_TRANSLATE:
            case TRANSFORM_TRANSLATEX:
            case TRANSFORM_TRANSLATEY: {
                // Handle percentage values: resolve against element's own dimensions
                float tx = tf->params.translate.x;
                float ty = tf->params.translate.y;
                if (!std::isnan(tf->translate_x_percent)) {
                    tx = tf->translate_x_percent * width / 100.0f;
                }
                if (!std::isnan(tf->translate_y_percent)) {
                    ty = tf->translate_y_percent * height / 100.0f;
                }
                m.e13 = tx;
                m.e23 = ty;
                break;
            }

            case TRANSFORM_TRANSLATE3D:
            case TRANSFORM_TRANSLATEZ:
                // 3D translate: just use X, Y for 2D rendering
                m.e13 = tf->params.translate3d.x;
                m.e23 = tf->params.translate3d.y;
                // Z translation affects perspective (ignored in 2D)
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
                // Z scale ignored in 2D
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
                // rotateX in 2D: compresses Y axis
                float cos_a = cosf(tf->params.angle);
                m.e22 = cos_a;
                break;
            }

            case TRANSFORM_ROTATEY: {
                // rotateY in 2D: compresses X axis
                float cos_a = cosf(tf->params.angle);
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
                // CSS matrix(a,b,c,d,e,f) = [a c e; b d f; 0 0 1]
                // ThorVG matrix is [e11 e12 e13; e21 e22 e23; 0 0 1]
                // So: e11=a, e12=c, e13=e, e21=b, e22=d, e23=f
                m.e11 = tf->params.matrix.a;
                m.e12 = tf->params.matrix.c;
                m.e13 = tf->params.matrix.e;
                m.e21 = tf->params.matrix.b;
                m.e22 = tf->params.matrix.d;
                m.e23 = tf->params.matrix.f;
                break;

            case TRANSFORM_PERSPECTIVE:
                // Perspective in 2D: approximate effect
                // True perspective requires 4x4 matrix, but we can simulate mild effects
                // For now, just ignore (identity)
                break;

            case TRANSFORM_ROTATE3D:
                // rotate3d(x, y, z, angle) - complex 3D rotation
                // Simplified: if mostly Z-axis, treat as 2D rotate
                // Otherwise, approximate
                {
                    float x = tf->params.rotate3d.x;
                    float y = tf->params.rotate3d.y;
                    float z = tf->params.rotate3d.z;
                    float len = sqrtf(x*x + y*y + z*z);
                    if (len > 0.001f) {
                        x /= len; y /= len; z /= len;
                        // If mostly Z-axis, use 2D rotation
                        if (fabsf(z) > 0.9f) {
                            float cos_a = cosf(tf->params.rotate3d.angle);
                            float sin_a = sinf(tf->params.rotate3d.angle);
                            if (z < 0) sin_a = -sin_a;
                            m.e11 = cos_a;  m.e12 = -sin_a;
                            m.e21 = sin_a;  m.e22 = cos_a;
                        }
                        // Otherwise, apply approximate rotation (Rodrigues' formula simplified)
                    }
                }
                break;

            case TRANSFORM_MATRIX3D:
                // 4x4 matrix - extract 2D portion
                // matrix3d uses column-major: [0-3] col0, [4-7] col1, [8-11] col2, [12-15] col3
                // 2D: m11=m[0], m12=m[4], m21=m[1], m22=m[5], tx=m[12], ty=m[13]
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

        result = matrix_multiply(result, m);
    }

    // Apply from_origin translation
    result = matrix_multiply(result, from_origin);

    return result;
}

/**
 * Apply transform to a ThorVG paint object
 *
 * @param paint ThorVG paint object
 * @param transform TransformProp containing transform functions and origin
 * @param x Element X position (border-box left)
 * @param y Element Y position (border-box top)
 * @param width Element width (border-box)
 * @param height Element height (border-box)
 */
inline void apply_transform(Tvg_Paint paint, TransformProp* transform,
                           float x, float y, float width, float height) {
    if (!paint || !transform || !transform->functions) return;

    // Calculate origin in element coordinates
    float origin_x = transform->origin_x_percent
        ? (transform->origin_x / 100.0f) * width
        : transform->origin_x;
    float origin_y = transform->origin_y_percent
        ? (transform->origin_y / 100.0f) * height
        : transform->origin_y;

    // Origin is relative to element's border-box
    origin_x += x;
    origin_y += y;

    Tvg_Matrix m = compute_transform_matrix(transform->functions, width, height, origin_x, origin_y);

    tvg_paint_set_transform(paint, &m);
}

/**
 * Check if an element has any transforms applied
 */
inline bool has_transform(DomElement* elem) {
    return elem && elem->transform && elem->transform->functions;
}

/**
 * Transform a point through the element's transform matrix
 */
inline void transform_point(float& x, float& y, const Tvg_Matrix& m) {
    float new_x = m.e11 * x + m.e12 * y + m.e13;
    float new_y = m.e21 * x + m.e22 * y + m.e23;
    x = new_x;
    y = new_y;
}

} // namespace radiant

#endif // RADIANT_TRANSFORM_HPP
