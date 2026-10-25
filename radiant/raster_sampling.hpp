#pragma once

#include <math.h>

// integrate pixel-cell intersections once for both image colors and glyph coverage.
template <typename Visitor>
static inline float raster_visit_sample_area(int width, int height,
        float x0, float y0, float x1, float y1, Visitor visit) {
    float left = fmaxf(0.0f, x0), top = fmaxf(0.0f, y0);
    float right = fminf((float)width, x1), bottom = fminf((float)height, y1);
    if (!(left < right && top < bottom)) return 0.0f;
    int first_x = (int)floorf(left), last_x = (int)ceilf(right); // INT_CAST_OK: bounded source pixel indices.
    int first_y = (int)floorf(top), last_y = (int)ceilf(bottom); // INT_CAST_OK: bounded source pixel indices.
    for (int row = first_y; row < last_y; row++) {
        float vertical = fminf(bottom, (float)row + 1.0f) - fmaxf(top, (float)row);
        for (int column = first_x; column < last_x; column++) {
            float horizontal = fminf(right, (float)column + 1.0f) - fmaxf(left, (float)column);
            visit(row, column, horizontal * vertical);
        }
    }
    return (right - left) * (bottom - top);
}
