#include "layout.hpp"
#include "../lib/memtrack.h"
#include <new>

// The arrays belong to the caller's scratch scope, which outlives the
// metadata and frees them together with the caller's other pass arrays.
TableMetadata::TableMetadata(ScratchScope* scope, int cols, int rows)
    : column_count(cols), row_count(rows), grid_occupied(nullptr),
      col_widths(nullptr), col_single_min_widths(nullptr),
      col_min_widths(nullptr), col_max_widths(nullptr),
      col_percent_widths(nullptr),
      row_heights(nullptr), row_base_heights(nullptr), row_reference_heights(nullptr),
      row_y_positions(nullptr), row_collapsed(nullptr),
      col_collapsed(nullptr), col_original_widths(nullptr),
      row_has_percent_height(nullptr), row_has_specified_height(nullptr),
      col_edge_max_border(nullptr),
      col_has_explicit_width(nullptr), collapsed_border_top(0),
      collapsed_border_right(0), collapsed_border_bottom(0),
      collapsed_border_left(0) {
    grid_occupied = lam::own_arr(scope->array_zero<bool>((size_t)rows * (size_t)cols));
    col_widths = lam::own_arr(scope->array_zero<float>(cols));
    col_single_min_widths = lam::own_arr(scope->array_zero<float>(cols));
    col_min_widths = lam::own_arr(scope->array_zero<float>(cols));
    col_max_widths = lam::own_arr(scope->array_zero<float>(cols));
    col_percent_widths = lam::own_arr(scope->array_zero<float>(cols));
    row_heights = lam::own_arr(scope->array_zero<float>(rows));
    row_base_heights = lam::own_arr(scope->array_zero<float>(rows));
    row_reference_heights = lam::own_arr(scope->array_zero<float>(rows));
    row_y_positions = lam::own_arr(scope->array_zero<float>(rows));
    row_collapsed = lam::own_arr(scope->array_zero<bool>(rows));
    col_collapsed = lam::own_arr(scope->array_zero<bool>(cols));
    col_original_widths = lam::own_arr(scope->array_zero<float>(cols));
    row_has_percent_height = lam::own_arr(scope->array_zero<bool>(rows));
    row_has_specified_height = lam::own_arr(scope->array_zero<bool>(rows));
    col_edge_max_border = lam::own_arr(scope->array_zero<float>((size_t)cols + 1));
    col_has_explicit_width = lam::own_arr(scope->array_zero<bool>(cols));
}

//------------------------------------------------------------------------------
// Heap factory (audited boundary for `new TableMetadata` / `delete meta`)
//------------------------------------------------------------------------------

TableMetadata* table_metadata_create(ScratchScope* scope, int cols, int rows) {
    TableMetadata* meta = (TableMetadata*)mem_alloc(sizeof(TableMetadata), MEM_CAT_LAYOUT);
    if (!meta) return nullptr;
    new (meta) TableMetadata(scope, cols, rows); // NEW_DELETE_OK: single audited construction boundary for TableMetadata.
    return meta;
}

void table_metadata_destroy(TableMetadata* meta) {
    if (!meta) return;
    lam::Temp<TableMetadata> storage(meta);  // destroyed below, storage freed on return
    meta->~TableMetadata(); // NEW_DELETE_OK: paired with table_metadata_create.
}
