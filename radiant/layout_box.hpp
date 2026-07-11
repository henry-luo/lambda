#pragma once

#include "view.hpp"

typedef struct BoxEdges {
    float left;
    float right;
    float top;
    float bottom;
} BoxEdges;

typedef struct BoxMetrics {
    BoxEdges margin;
    BoxEdges padding;
    BoxEdges border;
    float padding_h;
    float padding_v;
    float border_h;
    float border_v;
    float pad_border_h;
    float pad_border_v;
} BoxMetrics;

BoxMetrics layout_box_metrics(ViewBlock* block);

float layout_padding_border_width(ViewBlock* block);
float layout_padding_border_height(ViewBlock* block);

float layout_content_width_from_border_box(ViewBlock* block, float border_width);
float layout_content_height_from_border_box(ViewBlock* block, float border_height);
float layout_border_width_from_content_box(ViewBlock* block, float content_width);
float layout_border_height_from_content_box(ViewBlock* block, float content_height);
float layout_floor_border_box_width(ViewBlock* block, float border_width);
float layout_floor_border_box_height(ViewBlock* block, float border_height);

float layout_apply_min_max_width(ViewBlock* block, float width, bool width_is_border_box);
float layout_apply_min_max_height(ViewBlock* block, float height, bool height_is_border_box);

static inline const CssValue* css_box_shorthand_side_value(const CssValue* value, int side) {
    if (!value) return nullptr;
    if (value->type != CSS_VALUE_TYPE_LIST) return value;
    int count = value->data.list.count;
    CssValue** values = value->data.list.values;
    if (count <= 0 || !values) return nullptr;
    int index = 0;
    if (side == 0) {
        index = 0;                                // top
    } else if (side == 1) {
        index = (count >= 2) ? 1 : 0;             // right
    } else if (side == 2) {
        index = (count >= 3) ? 2 : 0;             // bottom
    } else {
        index = (count >= 4) ? 3 : ((count >= 2) ? 1 : 0); // left
    }
    return index < count ? values[index] : nullptr;
}

static inline float layout_non_negative_free_space(float value) {
    return value > 0.0f ? value : 0.0f;
}

static inline int layout_count_auto_margins(bool start_auto, bool end_auto) {
    return (start_auto ? 1 : 0) + (end_auto ? 1 : 0);
}

static inline float layout_auto_margin_share(float free_space, int auto_margin_count) {
    return (auto_margin_count > 0 && free_space > 0.0f)
        ? free_space / (float)auto_margin_count
        : 0.0f;
}

static inline void layout_resolve_auto_margin_pair(float available_size, float border_box_size,
                                                   bool start_auto, bool end_auto,
                                                   float* start_margin, float* end_margin) {
    if (!start_margin || !end_margin) return;
    if (start_auto && end_auto) {
        float used_margin = layout_non_negative_free_space(
            (available_size - border_box_size) / 2.0f);
        *start_margin = used_margin;
        *end_margin = used_margin;
    } else if (start_auto) {
        *start_margin = layout_non_negative_free_space(
            available_size - border_box_size - *end_margin);
    } else if (end_auto) {
        *end_margin = layout_non_negative_free_space(
            available_size - border_box_size - *start_margin);
    }
}

// Compatibility wrappers used throughout the existing layout code.
float adjust_min_max_width(ViewBlock* block, float width);
float adjust_min_max_height(ViewBlock* block, float height);
float adjust_border_padding_width(ViewBlock* block, float width);
float adjust_border_padding_height(ViewBlock* block, float height);
