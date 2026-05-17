#pragma once

#include "layout.hpp"

typedef enum LayoutAxis {
    LAYOUT_AXIS_X,
    LAYOUT_AXIS_Y,
} LayoutAxis;

inline float layout_axis_size(ViewElement* item, LayoutAxis axis) {
    if (!item) return 0.0f;
    return axis == LAYOUT_AXIS_X ? item->width : item->height;
}

inline void layout_axis_set_size(ViewElement* item, LayoutAxis axis, float size) {
    if (!item) return;
    if (axis == LAYOUT_AXIS_X) {
        item->width = size;
    } else {
        item->height = size;
    }
}

inline float layout_axis_pos(ViewElement* item, LayoutAxis axis) {
    if (!item) return 0.0f;
    return axis == LAYOUT_AXIS_X ? item->x : item->y;
}

inline void layout_axis_set_pos(ViewElement* item, LayoutAxis axis, float pos) {
    if (!item) return;
    if (axis == LAYOUT_AXIS_X) {
        item->x = pos;
    } else {
        item->y = pos;
    }
}

inline LayoutAxis flex_main_axis(FlexContainerLayout* flex) {
    if (!flex) return LAYOUT_AXIS_X;
    return (flex->direction == CSS_VALUE_COLUMN ||
            flex->direction == CSS_VALUE_COLUMN_REVERSE)
        ? LAYOUT_AXIS_Y
        : LAYOUT_AXIS_X;
}

inline LayoutAxis flex_cross_axis(FlexContainerLayout* flex) {
    return flex_main_axis(flex) == LAYOUT_AXIS_X ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
}
