// radiant/pdf/coords.cpp
// PDF coordinate transformation utilities

#include "operators.h"
#include "../view.hpp"
#include "../../lib/log.h"
#include <math.h>

/**
 * Transform point from PDF coordinates to Radiant coordinates
 *
 * PDF uses bottom-left origin, y increases upward
 * Radiant uses top-left origin, y increases downward
 *
 * @param state Graphics state containing transformation matrices
 * @param page_height Height of the page in points
 * @param x Pointer to x coordinate (modified in place)
 * @param y Pointer to y coordinate (modified in place)
 */
void pdf_to_radiant_coords(PDFGraphicsState* state, double page_height,
                           double* x, double* y) {
    // Apply text matrix transformation
    double tx = state->tm[0] * (*x) + state->tm[2] * (*y) + state->tm[4];
    double ty = state->tm[1] * (*x) + state->tm[3] * (*y) + state->tm[5];

    // Apply CTM (current transformation matrix)
    double ctx = state->ctm[0] * tx + state->ctm[2] * ty + state->ctm[4];
    double cty = state->ctm[1] * tx + state->ctm[3] * ty + state->ctm[5];

    // Convert from PDF coordinates (bottom-left origin) to Radiant (top-left origin)
    *x = ctx;
    *y = page_height - cty;
}

/**
 * Apply matrix transformation to a point
 *
 * Matrix format: [a b c d e f]
 * Transformation: x' = ax + cy + e, y' = bx + dy + f
 *
 * @param matrix Transformation matrix [a b c d e f]
 * @param x Pointer to x coordinate (modified in place)
 * @param y Pointer to y coordinate (modified in place)
 */
void apply_matrix_transform(double* matrix, double* x, double* y) {
    double tx = matrix[0] * (*x) + matrix[2] * (*y) + matrix[4];
    double ty = matrix[1] * (*x) + matrix[3] * (*y) + matrix[5];
    *x = tx;
    *y = ty;
}

/**
 * Concatenate two transformation matrices
 *
 * result = m1 * m2
 *
 * @param m1 First matrix [a b c d e f]
 * @param m2 Second matrix [a b c d e f]
 * @param result Result matrix [a b c d e f]
 */
void concat_matrices(double* m1, double* m2, double* result) {
    result[0] = m1[0] * m2[0] + m1[1] * m2[2];
    result[1] = m1[0] * m2[1] + m1[1] * m2[3];
    result[2] = m1[2] * m2[0] + m1[3] * m2[2];
    result[3] = m1[2] * m2[1] + m1[3] * m2[3];
    result[4] = m1[4] * m2[0] + m1[5] * m2[2] + m2[4];
    result[5] = m1[4] * m2[1] + m1[5] * m2[3] + m2[5];
}

/**
 * Get rotation angle from transformation matrix
 *
 * @param matrix Transformation matrix [a b c d e f]
 * @return Rotation angle in degrees
 */
double get_rotation_angle(double* matrix) {
    return atan2(matrix[1], matrix[0]) * 180.0 / M_PI;
}

/**
 * Get scale factors from transformation matrix
 *
 * @param matrix Transformation matrix [a b c d e f]
 * @param scale_x Pointer to x scale factor (output)
 * @param scale_y Pointer to y scale factor (output)
 */
void get_scale_factors(double* matrix, double* scale_x, double* scale_y) {
    *scale_x = sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1]);
    *scale_y = sqrt(matrix[2] * matrix[2] + matrix[3] * matrix[3]);
}

/**
 * Convert PDF color (0.0-1.0) to Radiant color (0-255)
 *
 * @param pdf_color PDF color value (0.0 to 1.0)
 * @return Radiant color value (0 to 255)
 */
uint8_t pdf_color_to_radiant(double pdf_color) {
    if (pdf_color <= 0.0) return 0;
    if (pdf_color >= 1.0) return 255;
    return (uint8_t)(pdf_color * 255.0 + 0.5);
}

/**
 * Convert RGB color from PDF to Radiant Color structure
 *
 * @param r Red component (0.0 to 1.0)
 * @param g Green component (0.0 to 1.0)
 * @param b Blue component (0.0 to 1.0)
 * @return Radiant Color structure
 */
Color pdf_rgb_to_color(double r, double g, double b) {
    Color color;
    color.r = pdf_color_to_radiant(r);
    color.g = pdf_color_to_radiant(g);
    color.b = pdf_color_to_radiant(b);
    color.a = 255; // Fully opaque
    return color;
}
