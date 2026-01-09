#include "grid.hpp"
#include "intrinsic_sizing.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
}

// Create a new grid track list
GridTrackList* create_grid_track_list(int initial_capacity) {
    GridTrackList* track_list = (GridTrackList*)mem_calloc(1, sizeof(GridTrackList), MEM_CAT_LAYOUT);
    if (!track_list) return nullptr;

    track_list->allocated_tracks = initial_capacity;
    track_list->tracks = (GridTrackSize**)mem_calloc(initial_capacity, sizeof(GridTrackSize*), MEM_CAT_LAYOUT);
    track_list->line_names = (char**)mem_calloc(initial_capacity + 1, sizeof(char*), MEM_CAT_LAYOUT); // +1 for end line
    track_list->track_count = 0;
    track_list->line_name_count = 0;
    track_list->is_repeat = false;
    track_list->repeat_count = 1;

    return track_list;
}

// Destroy a grid track list
void destroy_grid_track_list(GridTrackList* track_list) {
    if (!track_list) return;

    // Free tracks
    for (int i = 0; i < track_list->track_count; i++) {
        destroy_grid_track_size(track_list->tracks[i]);
    }
    mem_free(track_list->tracks);

    // Free line names
    for (int i = 0; i < track_list->line_name_count; i++) {
        mem_free(track_list->line_names[i]);
    }
    mem_free(track_list->line_names);

    mem_free(track_list);
}

// Create a new grid track size
GridTrackSize* create_grid_track_size(GridTrackSizeType type, int value) {
    GridTrackSize* track_size = (GridTrackSize*)mem_calloc(1, sizeof(GridTrackSize), MEM_CAT_LAYOUT);
    if (!track_size) return nullptr;

    track_size->type = type;
    track_size->value = value;
    track_size->is_percentage = false;
    track_size->min_size = nullptr;
    track_size->max_size = nullptr;
    track_size->fit_content_limit = 0;

    return track_size;
}

// Destroy a grid track size
void destroy_grid_track_size(GridTrackSize* track_size) {
    if (!track_size) return;

    if (track_size->min_size) {
        destroy_grid_track_size(track_size->min_size);
    }
    if (track_size->max_size) {
        destroy_grid_track_size(track_size->max_size);
    }

    mem_free(track_size);
}

// Create a new grid area
GridArea* create_grid_area(const char* name, int row_start, int row_end, int column_start, int column_end) {
    GridArea* area = (GridArea*)mem_calloc(1, sizeof(GridArea), MEM_CAT_LAYOUT);
    if (!area) return nullptr;

    area->name = mem_strdup(name, MEM_CAT_LAYOUT);
    area->row_start = row_start;
    area->row_end = row_end;
    area->column_start = column_start;
    area->column_end = column_end;

    return area;
}

// Destroy a grid area
void destroy_grid_area(GridArea* area) {
    if (!area) return;

    mem_free(area->name);
    // Don't free the area itself if it's part of an array
}

// Add a grid line name
void add_grid_line_name(GridContainerLayout* grid, const char* name, int line_number, bool is_row) {
    if (!grid || !name) return;

    // Ensure we have space for more line names
    if (grid->line_name_count >= grid->allocated_line_names) {
        grid->allocated_line_names *= 2;
        grid->line_names = (GridLineName*)mem_realloc(grid->line_names,
                                                 grid->allocated_line_names * sizeof(GridLineName), MEM_CAT_LAYOUT);
    }

    GridLineName* line_name = &grid->line_names[grid->line_name_count];
    line_name->name = mem_strdup(name, MEM_CAT_LAYOUT);
    line_name->line_number = line_number;
    line_name->is_row = is_row;

    grid->line_name_count++;

    log_debug("Added grid line name '%s' at line %d (%s)\n",
              name, line_number, is_row ? "row" : "column");
}

// Find a grid line by name
int find_grid_line_by_name(GridContainerLayout* grid, const char* name, bool is_row) {
    if (!grid || !name) return 0;

    for (int i = 0; i < grid->line_name_count; i++) {
        GridLineName* line_name = &grid->line_names[i];
        if (line_name->is_row == is_row && strcmp(line_name->name, name) == 0) {
            return line_name->line_number;
        }
    }

    return 0; // Not found
}

// Resolve grid line position
int resolve_grid_line_position(GridContainerLayout* grid_layout, int line_value, const char* line_name, bool is_row, bool is_end_line) {
    if (!grid_layout) return 1;

    // If line name is provided, try to resolve it first
    if (line_name) {
        int named_line = find_grid_line_by_name(grid_layout, line_name, is_row);
        if (named_line > 0) {
            return named_line;
        }
    }

    // If line value is provided, use it
    if (line_value != 0) {
        if (line_value < 0) {
            // Negative values count from the end
            int track_count = is_row ? grid_layout->computed_row_count : grid_layout->computed_column_count;
            return track_count + line_value + 2; // +2 because lines are 1-indexed and we want the line after the last track
        }
        return line_value;
    }

    // Default to auto (will be resolved during auto-placement)
    return 0;
}

// Enhanced grid template areas parser
// Parses CSS grid-template-areas syntax like:
//   "header header header"
//   "sidebar main aside"
//   "footer footer footer"
void parse_grid_template_areas(GridProp* grid, const char* areas_string) {
    log_debug("parse_grid_template_areas: grid=%p, areas='%s'", grid, areas_string ? areas_string : "NULL");
    if (!grid || !areas_string || areas_string[0] == '\0') {
        return;
    }

    // Free existing area names before clearing
    for (int i = 0; i < grid->area_count; i++) {
        if (grid->grid_areas && grid->grid_areas[i].name) {
            mem_free(grid->grid_areas[i].name);
            grid->grid_areas[i].name = nullptr;
        }
    }
    grid->area_count = 0;

    // Constants for grid limits
    const int MAX_GRID_SIZE = 16;   // Support up to 16x16 grids
    const int MAX_NAME_LEN = 32;    // Area names up to 31 chars
    const int MAX_AREAS = 32;       // Max unique named areas

    // Heap-allocated grid cell storage to avoid stack overflow
    char*** grid_cells = (char***)mem_calloc(MAX_GRID_SIZE, sizeof(char**), MEM_CAT_LAYOUT);
    if (!grid_cells) {
        log_debug("parse_grid_template_areas: allocation failed");
        return;
    }
    for (int r = 0; r < MAX_GRID_SIZE; r++) {
        grid_cells[r] = (char**)mem_calloc(MAX_GRID_SIZE, sizeof(char*), MEM_CAT_LAYOUT);
        if (!grid_cells[r]) {
            // cleanup and return
            for (int j = 0; j < r; j++) mem_free(grid_cells[j]);
            mem_free(grid_cells);
            return;
        }
        for (int c = 0; c < MAX_GRID_SIZE; c++) {
            grid_cells[r][c] = (char*)mem_calloc(MAX_NAME_LEN, sizeof(char), MEM_CAT_LAYOUT);
        }
    }

    int rows = 0, cols = 0;
    const char* p = areas_string;

    // Parse quoted strings (each quoted string is one row)
    while (*p && rows < MAX_GRID_SIZE) {
        // Skip whitespace and find opening quote
        while (*p && *p != '"') p++;
        if (!*p) break;
        p++; // skip opening quote

        // Parse area names within this row
        int col = 0;
        while (*p && *p != '"' && col < MAX_GRID_SIZE) {
            // Skip leading whitespace
            while (*p && *p != '"' && (*p == ' ' || *p == '\t')) p++;
            if (!*p || *p == '"') break;

            // Read area name
            int name_len = 0;
            while (*p && *p != '"' && *p != ' ' && *p != '\t' && name_len < MAX_NAME_LEN - 1) {
                grid_cells[rows][col][name_len++] = *p++;
            }
            grid_cells[rows][col][name_len] = '\0';

            if (name_len > 0) {
                col++;
            }
        }

        if (col > cols) cols = col;
        if (col > 0) rows++;

        // Skip to closing quote
        while (*p && *p != '"') p++;
        if (*p == '"') p++;
    }

    log_debug("parse_grid_template_areas: parsed %d rows x %d cols", rows, cols);

    if (rows == 0 || cols == 0) {
        // cleanup
        for (int r = 0; r < MAX_GRID_SIZE; r++) {
            for (int c = 0; c < MAX_GRID_SIZE; c++) mem_free(grid_cells[r][c]);
            mem_free(grid_cells[r]);
        }
        mem_free(grid_cells);
        return;
    }

    // Update grid dimensions from template areas
    grid->computed_row_count = rows;
    grid->computed_column_count = cols;

    // Collect unique area names (excluding "." which means empty cell)
    char** unique_names = (char**)mem_calloc(MAX_AREAS, sizeof(char*), MEM_CAT_LAYOUT);
    int unique_count = 0;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const char* name = grid_cells[r][c];
            if (!name[0] || strcmp(name, ".") == 0) continue;

            // Check if already in unique list
            bool found = false;
            for (int i = 0; i < unique_count; i++) {
                if (strcmp(unique_names[i], name) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found && unique_count < MAX_AREAS) {
                unique_names[unique_count] = mem_strdup(name, MEM_CAT_LAYOUT);
                unique_count++;
            }
        }
    }

    // Ensure we have enough space for areas
    if (grid->allocated_areas < unique_count) {
        grid->grid_areas = (GridArea*)mem_realloc(grid->grid_areas, unique_count * sizeof(GridArea), MEM_CAT_LAYOUT);
        grid->allocated_areas = unique_count;
    }

    // Calculate bounds for each unique area
    for (int i = 0; i < unique_count; i++) {
        const char* area_name = unique_names[i];
        int min_row = rows, max_row = -1;
        int min_col = cols, max_col = -1;

        // Find bounding box
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (strcmp(grid_cells[r][c], area_name) == 0) {
                    if (r < min_row) min_row = r;
                    if (r > max_row) max_row = r;
                    if (c < min_col) min_col = c;
                    if (c > max_col) max_col = c;
                }
            }
        }

        // Validate rectangle (all cells in bounding box must have same name)
        bool is_rectangle = true;
        for (int r = min_row; r <= max_row && is_rectangle; r++) {
            for (int c = min_col; c <= max_col && is_rectangle; c++) {
                if (strcmp(grid_cells[r][c], area_name) != 0) {
                    is_rectangle = false;
                    log_debug("parse_grid_template_areas: area '%s' is not rectangular", area_name);
                }
            }
        }

        if (is_rectangle && min_row <= max_row && min_col <= max_col) {
            GridArea* area = &grid->grid_areas[grid->area_count];
            // Allocate and copy name (GridArea.name is char*)
            area->name = mem_strdup(area_name, MEM_CAT_LAYOUT);
            // Convert to 1-based CSS grid line numbers
            area->row_start = min_row + 1;
            area->row_end = max_row + 2;      // +2 because end line is exclusive
            area->column_start = min_col + 1;
            area->column_end = max_col + 2;
            grid->area_count++;

            log_debug("parse_grid_template_areas: area '%s' -> rows %d-%d, cols %d-%d",
                      area_name, area->row_start, area->row_end, area->column_start, area->column_end);
        }
    }

    // Cleanup
    for (int i = 0; i < unique_count; i++) {
        mem_free(unique_names[i]);
    }
    mem_free(unique_names);

    for (int r = 0; r < MAX_GRID_SIZE; r++) {
        for (int c = 0; c < MAX_GRID_SIZE; c++) mem_free(grid_cells[r][c]);
        mem_free(grid_cells[r]);
    }
    mem_free(grid_cells);

    log_debug("parse_grid_template_areas: successfully parsed %d areas", grid->area_count);
}

// Resolve grid template areas
void resolve_grid_template_areas(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug("Resolving grid template areas\n");

    // Validate that all areas form rectangles and don't overlap
    for (int i = 0; i < grid_layout->area_count; i++) {
        GridArea* area = &grid_layout->grid_areas[i];

        // Ensure area forms a valid rectangle
        if (area->row_start >= area->row_end || area->column_start >= area->column_end) {
            log_debug("Warning: Invalid grid area '%s' - not a valid rectangle\n", area->name);
            continue;
        }

        // Check for overlaps with other areas
        for (int j = i + 1; j < grid_layout->area_count; j++) {
            GridArea* other_area = &grid_layout->grid_areas[j];

            // Check if areas overlap
            bool row_overlap = !(area->row_end <= other_area->row_start || other_area->row_end <= area->row_start);
            bool col_overlap = !(area->column_end <= other_area->column_start || other_area->column_end <= area->column_start);

            if (row_overlap && col_overlap) {
                log_debug("Warning: Grid areas '%s' and '%s' overlap\n", area->name, other_area->name);
            }
        }
    }

    log_debug("Grid template areas resolved\n");
}

// Calculate intrinsic sizes for grid items using unified intrinsic sizing API
IntrinsicSizes calculate_grid_item_intrinsic_sizes(LayoutContext* lycon, ViewBlock* item, bool is_row_axis) {
    IntrinsicSizes sizes = {0};

    if (!item) return sizes;

    // First, check if we have pre-computed measurements from Pass 1 (layout_grid_multipass.cpp)
    // These measurements were calculated with proper width constraints for height calculation
    if (item->gi) {
        if (is_row_axis) {
            // Use pre-computed height measurements if available
            log_debug("Checking pre-computed height for %s (gi=%p): min=%.1f, max=%.1f, has_measured=%d",
                      item->node_name(), item->gi,
                      item->gi->measured_min_height, item->gi->measured_max_height,
                      item->gi->has_measured_size);
            if (item->gi->has_measured_size && (item->gi->measured_min_height > 0 || item->gi->measured_max_height > 0)) {
                sizes.min_content = item->gi->measured_min_height;
                sizes.max_content = item->gi->measured_max_height;

                // Don't force minimum height - empty items should have 0 height
                if (sizes.max_content < sizes.min_content) {
                    sizes.max_content = sizes.min_content;
                }

                log_debug("Using pre-computed height for %s: min=%.1f, max=%.1f",
                          item->node_name(), sizes.min_content, sizes.max_content);

                // Apply height constraints from BlockProp
                if (item->blk) {
                    if (item->blk->given_min_height > 0) {
                        sizes.min_content = fmax(sizes.min_content, item->blk->given_min_height);
                    }
                    if (item->blk->given_max_height > 0) {
                        sizes.max_content = fmin(sizes.max_content, item->blk->given_max_height);
                    }
                }
                return sizes;
            }
        } else {
            // Use pre-computed width measurements if available
            if (item->gi->measured_min_width > 0 || item->gi->measured_max_width > 0) {
                sizes.min_content = item->gi->measured_min_width;
                sizes.max_content = item->gi->measured_max_width;

                // Don't force minimum width - empty items should have 0 width
                if (sizes.max_content < sizes.min_content) {
                    sizes.max_content = sizes.min_content;
                }

                log_debug("Using pre-computed width for %s: min=%.1f, max=%.1f",
                          item->node_name(), sizes.min_content, sizes.max_content);

                // Apply width constraints from BlockProp
                if (item->blk) {
                    if (item->blk->given_min_width > 0) {
                        sizes.min_content = fmax(sizes.min_content, item->blk->given_min_width);
                    }
                    if (item->blk->given_max_width > 0) {
                        sizes.max_content = fmin(sizes.max_content, item->blk->given_max_width);
                    }
                }
                return sizes;
            }
        }
    }

    // Fallback: Use unified intrinsic sizing API if layout context is available
    if (lycon) {
        // Determine available space for measurement
        AvailableSpace available;

        if (is_row_axis) {
            // For row axis (height measurement), we need to determine the width constraint
            // Height depends on width due to text wrapping
            float width = 200; // Default fallback

            if (item->width > 0) {
                // Item already has a width (e.g., from previous layout pass)
                width = item->width;
            } else if (item->gi && lycon->grid_container) {
                // Calculate width from computed column tracks
                GridContainerLayout* grid = lycon->grid_container;
                int col_start = item->gi->computed_grid_column_start - 1;
                int col_end = item->gi->computed_grid_column_end - 1;

                // Clamp to valid ranges
                if (col_start >= 0 && col_end > col_start && col_end <= grid->computed_column_count) {
                    int span_width = 0;
                    bool has_unsized_fr_track = false;

                    for (int c = col_start; c < col_end; c++) {
                        int track_size = grid->computed_columns[c].computed_size;
                        if (track_size > 0) {
                            span_width += track_size;
                            if (c < col_end - 1) {
                                span_width += (int)grid->column_gap;
                            }
                        } else {
                            // Track has size 0 - might be FR track not yet sized
                            has_unsized_fr_track = true;
                        }
                    }

                    if (span_width > 0 && !has_unsized_fr_track) {
                        // All tracks are sized, use actual span width
                        int box_adjustment = 0;
                        if (item->bound) {
                            box_adjustment += item->bound->padding.left + item->bound->padding.right;
                            if (item->bound->border) {
                                box_adjustment += (int)(item->bound->border->width.left + item->bound->border->width.right);
                            }
                        }
                        width = (float)(span_width - box_adjustment);
                        if (width < 10) width = 10;
                        log_debug("Row sizing: using column span width %.1f for %s (cols %d-%d)",
                                  width, item->node_name(), col_start + 1, col_end);
                    } else if (grid->content_width > 0) {
                        // FR tracks not sized yet - estimate from container width
                        int col_count = grid->computed_column_count > 0 ? grid->computed_column_count : 1;
                        int total_gaps = (col_count - 1) * (int)grid->column_gap;
                        int span_cols = col_end - col_start;
                        width = (float)((grid->content_width - total_gaps) * span_cols) / col_count;

                        // Subtract item's own padding/border
                        if (item->bound) {
                            width -= item->bound->padding.left + item->bound->padding.right;
                            if (item->bound->border) {
                                width -= item->bound->border->width.left + item->bound->border->width.right;
                            }
                        }
                        if (width < 10) width = 10;
                        log_debug("Row sizing: estimating width %.1f for %s (FR tracks, container=%d, cols=%d)",
                                  width, item->node_name(), grid->content_width, col_count);
                    }
                }
            }

            // Create available space with definite width for height calculation
            available = AvailableSpace::make_width_definite(width);
        } else {
            // For column axis (width measurement), use max-content
            available = AvailableSpace::make_max_content();
        }

        // Use unified API to measure all sizes
        IntrinsicSizesBidirectional all_sizes = measure_intrinsic_sizes(lycon, item, available);

        // Extract the axis we need
        sizes = intrinsic_sizes_for_axis(all_sizes, !is_row_axis);

        // Ensure max >= min
        if (sizes.max_content < sizes.min_content) {
            sizes.max_content = sizes.min_content;
        }
    } else {
        // Fallback: use item dimensions if no layout context
        // Empty items should have 0 height/width - the grid algorithm handles min track sizes
        if (is_row_axis) {
            sizes.min_content = 0;
            sizes.max_content = item->height > 0 ? item->height : 0;
        } else {
            sizes.min_content = 0;
            sizes.max_content = item->width > 0 ? item->width : 0;
        }
    }

    // Consider constraints from BlockProp
    if (item->blk) {
        if (item->blk->given_min_width > 0 && !is_row_axis) {
            sizes.min_content = fmax(sizes.min_content, item->blk->given_min_width);
        }
        if (item->blk->given_max_width > 0 && !is_row_axis) {
            sizes.max_content = fmin(sizes.max_content, item->blk->given_max_width);
        }
        if (item->blk->given_min_height > 0 && is_row_axis) {
            sizes.min_content = fmax(sizes.min_content, item->blk->given_min_height);
        }
        if (item->blk->given_max_height > 0 && is_row_axis) {
            sizes.max_content = fmin(sizes.max_content, item->blk->given_max_height);
        }
    }

    return sizes;
}
