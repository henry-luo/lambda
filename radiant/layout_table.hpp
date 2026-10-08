#pragma once
#include <stddef.h>

// shared track distribution; callers resolve source-specific widths and spacing first.
void layout_table_distribute_fixed_columns(float* widths, size_t columns,
    float* content_width, float specified_width, size_t unspecified_columns);
