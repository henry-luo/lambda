#pragma once
#include <stddef.h>

// shared track distribution; callers resolve source-specific widths and spacing first.
void layout_table_distribute_fixed_columns(float* widths, size_t columns,
    float* content_width, float specified_width, size_t unspecified_columns);

// borrowed intrinsic tracks; adapters retain their own measurement storage.
struct LayoutTableColumnWidths {
    const float* minimum;
    const float* maximum;
    const float* single_minimum;
    const float* percentage;
    const bool* constrained;
    size_t count;
};
float layout_table_percent_preferred_width(const LayoutTableColumnWidths& tracks, float total_percent);
bool layout_table_distribute_percent_columns(const LayoutTableColumnWidths& tracks, float* widths,
    float total_percent, float available, float minimum);
void layout_table_distribute_auto_columns(const LayoutTableColumnWidths& tracks, float* widths,
    float available, float minimum, float preferred);
