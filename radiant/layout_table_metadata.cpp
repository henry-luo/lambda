#include "layout_table_metadata.hpp"
#include "../lib/memtrack.h"
#include <new>

TableMetadata::TableMetadata(ScratchArena* scratch, int cols, int rows)
    : column_count(cols), row_count(rows), grid_occupied(nullptr),
      col_widths(nullptr), col_single_min_widths(nullptr),
      col_min_widths(nullptr), col_max_widths(nullptr),
      col_percent_widths(nullptr),
      row_heights(nullptr), row_y_positions(nullptr), row_collapsed(nullptr),
      col_collapsed(nullptr), col_original_widths(nullptr),
      row_has_percent_height(nullptr), col_edge_max_border(nullptr),
      col_has_explicit_width(nullptr), collapsed_border_top(0),
      collapsed_border_right(0), collapsed_border_bottom(0),
      collapsed_border_left(0), sa(scratch) {
    grid_occupied = (bool*)scratch_calloc(sa, rows * cols * sizeof(bool));
    col_widths = (float*)scratch_calloc(sa, cols * sizeof(float));
    col_single_min_widths = (float*)scratch_calloc(sa, cols * sizeof(float));
    col_min_widths = (float*)scratch_calloc(sa, cols * sizeof(float));
    col_max_widths = (float*)scratch_calloc(sa, cols * sizeof(float));
    col_percent_widths = (float*)scratch_calloc(sa, cols * sizeof(float));
    row_heights = (float*)scratch_calloc(sa, rows * sizeof(float));
    row_y_positions = (float*)scratch_calloc(sa, rows * sizeof(float));
    row_collapsed = (bool*)scratch_calloc(sa, rows * sizeof(bool));
    col_collapsed = (bool*)scratch_calloc(sa, cols * sizeof(bool));
    col_original_widths = (float*)scratch_calloc(sa, cols * sizeof(float));
    row_has_percent_height = (bool*)scratch_calloc(sa, rows * sizeof(bool));
    col_edge_max_border = (float*)scratch_calloc(sa, (cols + 1) * sizeof(float));
    col_has_explicit_width = (bool*)scratch_calloc(sa, cols * sizeof(bool));
}

TableMetadata::~TableMetadata() {
    scratch_free(sa, col_has_explicit_width);
    scratch_free(sa, col_edge_max_border);
    scratch_free(sa, row_has_percent_height);
    scratch_free(sa, col_original_widths);
    scratch_free(sa, col_collapsed);
    scratch_free(sa, row_collapsed);
    scratch_free(sa, row_y_positions);
    scratch_free(sa, row_heights);
    scratch_free(sa, col_percent_widths);
    scratch_free(sa, col_max_widths);
    scratch_free(sa, col_min_widths);
    scratch_free(sa, col_single_min_widths);
    scratch_free(sa, col_widths);
    scratch_free(sa, grid_occupied);
}

//------------------------------------------------------------------------------
// Heap factory (audited boundary for `new TableMetadata` / `delete meta`)
//------------------------------------------------------------------------------

TableMetadata* table_metadata_create(ScratchArena* scratch, int cols, int rows) {
    TableMetadata* meta = (TableMetadata*)mem_alloc(sizeof(TableMetadata), MEM_CAT_LAYOUT);
    if (!meta) return nullptr;
    new (meta) TableMetadata(scratch, cols, rows); // NEW_DELETE_OK: single audited construction boundary for TableMetadata.
    return meta;
}

void table_metadata_destroy(TableMetadata* meta) {
    if (!meta) return;
    meta->~TableMetadata(); // NEW_DELETE_OK: paired with table_metadata_create.
    mem_free(meta);
}
