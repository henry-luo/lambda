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
    float margin_h;
    float margin_v;
    float padding_h;
    float padding_v;
    float border_h;
    float border_v;
    float pad_border_h;
    float pad_border_v;
} BoxMetrics;

BoxMetrics layout_box_metrics(ViewBlock* block);
BoxMetrics layout_boundary_metrics(const BoundaryProp* bound);

float layout_padding_border_width(ViewBlock* block);
float layout_padding_border_height(ViewBlock* block);
float layout_boundary_padding_border_axis(const BoundaryProp* bound, bool horizontal);

float layout_content_width_from_border_box(ViewBlock* block, float border_width);
float layout_content_height_from_border_box(ViewBlock* block, float border_height);
float layout_border_width_from_content_box(ViewBlock* block, float content_width);
float layout_border_height_from_content_box(ViewBlock* block, float content_height);
float layout_padding_border_axis(ViewBlock* block, bool horizontal);
float layout_boundary_content_size_from_border_box(const BoundaryProp* bound, float border_size, bool horizontal);
float layout_boundary_border_size_from_content_box(const BoundaryProp* bound, float content_size, bool horizontal);
float layout_content_size_from_border_box(ViewBlock* block, float border_size, bool horizontal);
float layout_border_size_from_content_box(ViewBlock* block, float content_size, bool horizontal);
float layout_css_size_to_content_box(const BoundaryProp* bound, CssEnum box_sizing, float css_size, bool horizontal);
float layout_css_size_to_border_box(const BoundaryProp* bound, CssEnum box_sizing, float css_size, bool horizontal);
float layout_floor_border_box_width(ViewBlock* block, float border_width);
float layout_floor_border_box_height(ViewBlock* block, float border_height);
float layout_floor_border_box_axis(ViewBlock* block, float border_size, bool horizontal);

static inline CssEnum layout_box_sizing(ViewBlock* block) {
    return (block && block->blk) ? block->blk->box_sizing : CSS_VALUE_CONTENT_BOX;
}

static inline bool layout_uses_border_box(ViewBlock* block) {
    return layout_box_sizing(block) == CSS_VALUE_BORDER_BOX;
}

float layout_apply_min_max_width(ViewBlock* block, float width, bool width_is_border_box);
float layout_apply_min_max_height(ViewBlock* block, float height, bool height_is_border_box);
float layout_apply_min_max_axis(ViewBlock* block, float size, bool horizontal, bool size_is_border_box);
float layout_clamp_min_max_width(ViewBlock* block, float width);
float layout_clamp_min_max_height(ViewBlock* block, float height);
float layout_clamp_min_max_axis(ViewBlock* block, float size, bool horizontal);

static inline float layout_clamp_positive_min_max_width(ViewBlock* block, float width) {
    if (!block || !block->blk) return width;
    float constrained_width = width;
    if (block->blk->given_max_width > 0.0f && constrained_width > block->blk->given_max_width) {
        constrained_width = block->blk->given_max_width;
    }
    if (block->blk->given_min_width > 0.0f && constrained_width < block->blk->given_min_width) {
        constrained_width = block->blk->given_min_width;
    }
    return constrained_width;
}

static inline float layout_clamp_positive_min_max_height(ViewBlock* block, float height) {
    if (!block || !block->blk) return height;
    float constrained_height = height;
    if (block->blk->given_max_height > 0.0f && constrained_height > block->blk->given_max_height) {
        constrained_height = block->blk->given_max_height;
    }
    if (block->blk->given_min_height > 0.0f && constrained_height < block->blk->given_min_height) {
        constrained_height = block->blk->given_min_height;
    }
    return constrained_height;
}

static inline float layout_clamp_positive_min_max_axis(ViewBlock* block, float size, bool horizontal) {
    return horizontal
        ? layout_clamp_positive_min_max_width(block, size)
        : layout_clamp_positive_min_max_height(block, size);
}

static inline float layout_explicit_min_width_or(ViewBlock* block, float fallback) {
    return (block && block->blk && block->blk->given_min_width >= 0.0f)
        ? block->blk->given_min_width
        : fallback;
}

static inline float layout_explicit_min_height_or(ViewBlock* block, float fallback) {
    return (block && block->blk && block->blk->given_min_height >= 0.0f)
        ? block->blk->given_min_height
        : fallback;
}

static inline float layout_explicit_min_axis_or(ViewBlock* block, bool horizontal, float fallback) {
    return horizontal
        ? layout_explicit_min_width_or(block, fallback)
        : layout_explicit_min_height_or(block, fallback);
}

static inline float layout_explicit_max_width_or(ViewBlock* block, float fallback) {
    return (block && block->blk && block->blk->given_max_width >= 0.0f)
        ? block->blk->given_max_width
        : fallback;
}

static inline float layout_explicit_max_height_or(ViewBlock* block, float fallback) {
    return (block && block->blk && block->blk->given_max_height >= 0.0f)
        ? block->blk->given_max_height
        : fallback;
}

static inline float layout_explicit_max_axis_or(ViewBlock* block, bool horizontal, float fallback) {
    return horizontal
        ? layout_explicit_max_width_or(block, fallback)
        : layout_explicit_max_height_or(block, fallback);
}

static inline bool layout_has_explicit_min_width(ViewBlock* block) {
    return layout_explicit_min_width_or(block, -1.0f) >= 0.0f;
}

static inline bool layout_has_explicit_min_height(ViewBlock* block) {
    return layout_explicit_min_height_or(block, -1.0f) >= 0.0f;
}

static inline float layout_positive_min_width(ViewBlock* block) {
    float min_width = layout_explicit_min_width_or(block, -1.0f);
    return min_width > 0.0f ? min_width : 0.0f;
}

static inline float layout_positive_min_height(ViewBlock* block) {
    float min_height = layout_explicit_min_height_or(block, -1.0f);
    return min_height > 0.0f ? min_height : 0.0f;
}

static inline float layout_positive_min_axis(ViewBlock* block, bool horizontal) {
    return horizontal
        ? layout_positive_min_width(block)
        : layout_positive_min_height(block);
}

static inline float layout_positive_max_width_or(ViewBlock* block, float fallback) {
    float max_width = layout_explicit_max_width_or(block, fallback);
    return max_width > 0.0f ? max_width : fallback;
}

static inline float layout_positive_max_height_or(ViewBlock* block, float fallback) {
    float max_height = layout_explicit_max_height_or(block, fallback);
    return max_height > 0.0f ? max_height : fallback;
}

static inline float layout_positive_max_axis_or(ViewBlock* block, bool horizontal, float fallback) {
    return horizontal
        ? layout_positive_max_width_or(block, fallback)
        : layout_positive_max_height_or(block, fallback);
}

static inline float layout_floor_min_width(ViewBlock* block, float width) {
    if (!block || !block->blk || block->blk->given_min_width < 0.0f) return width;
    return width < block->blk->given_min_width ? block->blk->given_min_width : width;
}

static inline float layout_floor_min_height(ViewBlock* block, float height) {
    if (!block || !block->blk || block->blk->given_min_height < 0.0f) return height;
    return height < block->blk->given_min_height ? block->blk->given_min_height : height;
}

static inline float layout_floor_min_axis(ViewBlock* block, float size, bool horizontal) {
    return horizontal ? layout_floor_min_width(block, size) : layout_floor_min_height(block, size);
}

static inline void layout_apply_positive_min_max_contribution(ViewBlock* block, bool horizontal,
                                                              float* min_size, float* max_size) {
    if (!block || !block->blk) return;
    if (horizontal) {
        if (min_size && block->blk->given_min_width > 0.0f && *min_size < block->blk->given_min_width) {
            *min_size = block->blk->given_min_width;
        }
        if (max_size && block->blk->given_max_width > 0.0f && *max_size > block->blk->given_max_width) {
            *max_size = block->blk->given_max_width;
        }
    } else {
        if (min_size && block->blk->given_min_height > 0.0f && *min_size < block->blk->given_min_height) {
            *min_size = block->blk->given_min_height;
        }
        if (max_size && block->blk->given_max_height > 0.0f && *max_size > block->blk->given_max_height) {
            *max_size = block->blk->given_max_height;
        }
    }
}

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
