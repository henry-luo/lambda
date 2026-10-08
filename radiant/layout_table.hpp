#pragma once
#include <stddef.h>
struct DomElement;

size_t layout_table_cell_colspan(DomElement* element);
size_t layout_table_column_span(DomElement* element);
size_t layout_table_cell_rowspan(DomElement* element);
size_t layout_table_used_rowspan(size_t specified, size_t remaining);

// both producers skip occupied slots before covering a cell's rectangular span.
template<typename Index, typename Occupied, typename Cover>
Index layout_table_place_span(Index rows, Index columns, Index row, Index col,
        Index row_span, Index col_span, Occupied occupied, Cover cover,
        Index* start_col = nullptr, Index* max_col_used = nullptr) {
    while (col < columns && occupied(row, col)) col++;
    if (start_col) *start_col = col;
    for (Index r = row; r < row + row_span && r < rows; r++)
        for (Index c = col; c < col + col_span && c < columns; c++) cover(r, c);
    Index right = col + col_span;
    if (max_col_used && right > *max_col_used) *max_col_used = right;
    return right;
}

void layout_table_distribute_rowspan_height(float* heights, size_t count, size_t start,
    size_t span, float required, float spacing, size_t empty_target);

// shared top/middle/bottom placement; valign uses TableCellProp::CELL_VALIGN_*.
float layout_table_cell_vertical_align_target(int valign, float content_area_height,
    float content_height, float content_start_y, bool clamp_to_content = false);

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
float layout_table_span_width(const float* widths, size_t count, size_t column, size_t span, float spacing);
void layout_table_distribute_span_extra(float* widths, const LayoutTableColumnWidths& tracks,
    size_t column, size_t span, float extra);
float layout_table_percent_preferred_width(const LayoutTableColumnWidths& tracks, float total_percent);
bool layout_table_distribute_percent_columns(const LayoutTableColumnWidths& tracks, float* widths,
    float total_percent, float available, float minimum);
void layout_table_distribute_auto_columns(const LayoutTableColumnWidths& tracks, float* widths,
    float available, float minimum, float preferred);
