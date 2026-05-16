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

float layout_content_width_from_border_box(ViewBlock* block, float border_width);
float layout_content_height_from_border_box(ViewBlock* block, float border_height);
float layout_border_width_from_content_box(ViewBlock* block, float content_width);
float layout_border_height_from_content_box(ViewBlock* block, float content_height);

float layout_apply_min_max_width(ViewBlock* block, float width, bool width_is_border_box);
float layout_apply_min_max_height(ViewBlock* block, float height, bool height_is_border_box);

// Compatibility wrappers used throughout the existing layout code.
float adjust_min_max_width(ViewBlock* block, float width);
float adjust_min_max_height(ViewBlock* block, float height);
float adjust_border_padding_width(ViewBlock* block, float width);
float adjust_border_padding_height(ViewBlock* block, float height);
