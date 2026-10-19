#pragma once
#include <stddef.h>
#include "scale.hpp"
struct DomElement;
struct CollapsedBorder;
struct ViewTable;
struct ViewTableRowGroup;
struct ViewBlock;
struct ViewElement;
struct ScratchScope;
struct LayoutTableAxis;
bool table_view_is_caption(ViewBlock* child);
bool table_caption_is_bottom(ViewBlock* caption);

// only the first header/footer groups are furniture; additional groups are body material.
struct LayoutTableSectionOrder {
    DomElement* header = nullptr;
    DomElement* footer = nullptr;
    bool accept_body_group(DomElement* group);
    bool accept_body_group(ViewTableRowGroup* group);
};
struct LayoutTableBodyEdge {
    DomElement* group = nullptr;
    DomElement* row = nullptr;
};
LayoutTableBodyEdge layout_table_source_body_edge(DomElement* table, bool after);

// captions belong to the transparent wrapper, outside the grid's border box.
RdtLogicalRect layout_table_grid_rect(const ViewTable* table);
float layout_table_column_visual_x(ViewTable* table, float* widths, float* positions,
    int first, int span, int columns);

size_t layout_table_cell_colspan(DomElement* element);
size_t layout_table_column_span(DomElement* element);
size_t layout_table_cell_rowspan(DomElement* element);
size_t layout_table_used_rowspan(size_t specified, size_t remaining);

// column groups with children report their bounding range after the leaf declarations.
typedef void (*LayoutTableColumnFn)(void*, ViewElement*, size_t first, size_t span, bool container);
void layout_table_visit_columns(ViewTable* table, void* context, LayoutTableColumnFn visitor, bool properties);
typedef void (*LayoutTableSourceColumnFn)(void*, DomElement*, size_t first, size_t span, bool container);
void layout_table_visit_source_columns(DomElement* table, void* context,
    LayoutTableSourceColumnFn visitor, bool properties = false);
template<typename Fn>
void layout_table_each_column(ViewTable* table, Fn fn, bool properties = false) {
    layout_table_visit_columns(table, &fn,
        [](void* context, ViewElement* column, size_t first, size_t span, bool container) {
            (*static_cast<Fn*>(context))(column, first, span, container);
        }, properties);
}
bool layout_table_column_range(ViewElement* source, size_t* first, size_t* span);
ViewBlock* layout_table_find_column_source(ViewTable* table, int column, bool group);

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

// collapsed-border conflict resolution (CSS 2.2 §17.6.2.1): hidden, then width, then style;
// the first argument wins full ties, so callers pass candidates in precedence order.
CollapsedBorder layout_table_collapsed_winner(const CollapsedBorder& a, const CollapsedBorder& b);

// borrowed computed boundaries and span slots; the caller owns the scratch scope.
struct LayoutTableBorderGrid {
    DomElement* table = nullptr;
    DomElement** rows = nullptr;
    DomElement** row_groups = nullptr;
    DomElement** cells = nullptr;
    DomElement** columns = nullptr;
    DomElement** column_groups = nullptr;
    int row_count = 0;
    int column_count = 0;
    DomElement* cell(int row, int column) const;
};
LayoutTableBorderGrid layout_table_border_grid_create(ScratchScope* scope,
    DomElement* table, int rows, int columns);
CollapsedBorder layout_table_collapsed_edge(const LayoutTableBorderGrid& grid,
    const LayoutTableAxis& axis, int row, int column);

// shared top/middle/bottom placement; valign uses TableCellProp::CELL_VALIGN_*.
float layout_table_cell_vertical_align_target(int valign, float content_area_height,
    float content_height, float content_start_y, bool clamp_to_content = false);

// shared track distribution; callers resolve source-specific widths and spacing first.
void layout_table_distribute_fixed_columns(float* widths, size_t columns,
    float* content_width, float specified_width, size_t unspecified_columns, const float* proportions = nullptr);

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
