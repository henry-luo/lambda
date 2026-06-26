#pragma once

#include "../lib/scratch_arena.h"

// Table metadata cache.
// Stores pre-analyzed table structure to avoid repeated DOM iterations.
struct TableMetadata {
    int column_count;
    int row_count;
    bool* grid_occupied;
    float* col_widths;
    float* col_single_min_widths;
    float* col_min_widths;
    float* col_max_widths;
    float* col_percent_widths;
    float* row_heights;
    float* row_y_positions;
    bool* row_collapsed;
    bool* col_collapsed;
    float* col_original_widths;
    bool* row_has_percent_height;
    float* col_edge_max_border;
    bool* col_has_explicit_width;

    float collapsed_border_top;
    float collapsed_border_right;
    float collapsed_border_bottom;
    float collapsed_border_left;

    ScratchArena* sa;

    TableMetadata(ScratchArena* scratch, int cols, int rows);
    ~TableMetadata();

    inline bool& grid(int row, int col) {
        return grid_occupied[row * column_count + col];
    }
};

// ============================================================================
// TableMetadata Heap Factory (audited boundary)
// ============================================================================
// Single audited construction site for `new TableMetadata` / `delete meta`.
TableMetadata* table_metadata_create(ScratchArena* scratch, int cols, int rows);
void table_metadata_destroy(TableMetadata* meta);
