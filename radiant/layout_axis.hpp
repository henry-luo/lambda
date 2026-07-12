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

inline bool layout_axis_is_horizontal(LayoutAxis axis) {
    return axis == LAYOUT_AXIS_X;
}

inline float layout_axis_given_size(const BlockProp* block, LayoutAxis axis) {
    if (!block) return -1.0f;
    return axis == LAYOUT_AXIS_X ? block->given_width : block->given_height;
}

inline float layout_axis_given_max_size(const BlockProp* block, LayoutAxis axis) {
    if (!block) return -1.0f;
    return axis == LAYOUT_AXIS_X ? block->given_max_width : block->given_max_height;
}

inline float layout_axis_spacing_start(const Spacing* spacing, LayoutAxis axis) {
    if (!spacing) return 0.0f;
    return axis == LAYOUT_AXIS_X ? spacing->left : spacing->top;
}

inline float layout_axis_spacing_end(const Spacing* spacing, LayoutAxis axis) {
    if (!spacing) return 0.0f;
    return axis == LAYOUT_AXIS_X ? spacing->right : spacing->bottom;
}

inline CssEnum layout_axis_margin_start_type(const Margin* margin, LayoutAxis axis) {
    if (!margin) return CSS_VALUE__UNDEF;
    return axis == LAYOUT_AXIS_X ? margin->left_type : margin->top_type;
}

inline CssEnum layout_axis_margin_end_type(const Margin* margin, LayoutAxis axis) {
    if (!margin) return CSS_VALUE__UNDEF;
    return axis == LAYOUT_AXIS_X ? margin->right_type : margin->bottom_type;
}

inline float layout_axis_border_start(const BorderProp* border, LayoutAxis axis) {
    return border ? layout_axis_spacing_start(&border->width, axis) : 0.0f;
}

inline float layout_axis_border_end(const BorderProp* border, LayoutAxis axis) {
    return border ? layout_axis_spacing_end(&border->width, axis) : 0.0f;
}

inline float layout_axis_padding_start(const BoundaryProp* bound, LayoutAxis axis) {
    return bound ? layout_axis_spacing_start(&bound->padding, axis) : 0.0f;
}

inline float layout_axis_margin_start(const BoundaryProp* bound, LayoutAxis axis) {
    return bound ? layout_axis_spacing_start(&bound->margin, axis) : 0.0f;
}

inline float layout_axis_margin_end(const BoundaryProp* bound, LayoutAxis axis) {
    return bound ? layout_axis_spacing_end(&bound->margin, axis) : 0.0f;
}

inline LayoutAxis flex_main_axis_from_props(const FlexProp* flex) {
    if (!flex) return LAYOUT_AXIS_X;
    bool column_direction = flex->direction == CSS_VALUE_COLUMN ||
                            flex->direction == CSS_VALUE_COLUMN_REVERSE;
    if (flex->writing_mode == WM_VERTICAL_RL ||
        flex->writing_mode == WM_VERTICAL_LR) {
        return column_direction ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    }
    return column_direction ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
}

inline LayoutAxis flex_main_axis(FlexContainerLayout* flex) {
    return flex_main_axis_from_props(flex);
}

inline LayoutAxis flex_cross_axis(FlexContainerLayout* flex) {
    return flex_main_axis(flex) == LAYOUT_AXIS_X ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
}

inline float flex_gap_for_axis(FlexContainerLayout* flex, LayoutAxis axis) {
    if (!flex) return 0.0f;
    return axis == LAYOUT_AXIS_X ? flex->column_gap : flex->row_gap;
}
