#include "grid.hpp"
#include "intrinsic_sizing.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
}

// Create a new grid track list
GridTrackList* create_grid_track_list(int initial_capacity) {
    GridTrackList* track_list = (GridTrackList*)calloc(1, sizeof(GridTrackList));
    if (!track_list) return nullptr;

    track_list->allocated_tracks = initial_capacity;
    track_list->tracks = (GridTrackSize**)calloc(initial_capacity, sizeof(GridTrackSize*));
    track_list->line_names = (char**)calloc(initial_capacity + 1, sizeof(char*)); // +1 for end line
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
    free(track_list->tracks);

    // Free line names
    for (int i = 0; i < track_list->line_name_count; i++) {
        free(track_list->line_names[i]);
    }
    free(track_list->line_names);

    free(track_list);
}

// Create a new grid track size
GridTrackSize* create_grid_track_size(GridTrackSizeType type, int value) {
    GridTrackSize* track_size = (GridTrackSize*)calloc(1, sizeof(GridTrackSize));
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

    free(track_size);
}

// Create a new grid area
GridArea* create_grid_area(const char* name, int row_start, int row_end, int column_start, int column_end) {
    GridArea* area = (GridArea*)calloc(1, sizeof(GridArea));
    if (!area) return nullptr;

    area->name = strdup(name);
    area->row_start = row_start;
    area->row_end = row_end;
    area->column_start = column_start;
    area->column_end = column_end;

    return area;
}

// Destroy a grid area
void destroy_grid_area(GridArea* area) {
    if (!area) return;

    free(area->name);
    // Don't free the area itself if it's part of an array
}

// Add a grid line name
void add_grid_line_name(GridContainerLayout* grid, const char* name, int line_number, bool is_row) {
    if (!grid || !name) return;

    // Ensure we have space for more line names
    if (grid->line_name_count >= grid->allocated_line_names) {
        grid->allocated_line_names *= 2;
        grid->line_names = (GridLineName*)realloc(grid->line_names,
                                                 grid->allocated_line_names * sizeof(GridLineName));
    }

    GridLineName* line_name = &grid->line_names[grid->line_name_count];
    line_name->name = strdup(name);
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
void parse_grid_template_areas(GridProp* grid, const char* areas_string) {
    log_debug("parse_grid_template_areas called with grid_layout=%p, areas_string='%s'\n", grid, areas_string);
    if (!grid || !areas_string) {
        log_debug("Early return - grid_layout=%p, areas_string=%p\n", grid, areas_string);
        return;
    }

    log_debug("grid_layout->grid_areas=%p, allocated_areas=%d\n", grid->grid_areas, grid->allocated_areas);
    log_debug("Parsing grid template areas: %s\n", areas_string);

    // TEMPORARY: Skip complex parsing to avoid stack overflow
    log_debug("Skipping grid-template-areas parsing to avoid crash\n");
    grid->area_count = 0;
    return;

    // Clear existing areas
    grid->area_count = 0;

    // Parse CSS grid-template-areas syntax
    // Format: "area1 area2 area3" "area4 area5 area6" "area7 area8 area9"

    char work_string[512];
    strncpy(work_string, areas_string, sizeof(work_string) - 1);
    work_string[sizeof(work_string) - 1] = '\0';

    // Use smaller fixed-size grid to prevent stack overflow
    const int MAX_GRID_SIZE = 4;
    const int MAX_NAME_LEN = 8;
    char grid_cells[4][4][8]; // Max 4x4 grid, 8 char area names
    int rows = 0, cols = 0;

    // Parse quoted strings (rows)
    char* row_start = work_string;
    char* current = work_string;

    while (*current && rows < MAX_GRID_SIZE) {
        // Find start of quoted string
        while (*current && *current != '"') current++;
        if (!*current) break;

        current++; // Skip opening quote
        char* row_content_start = current;

        // Find end of quoted string
        while (*current && *current != '"') current++;
        if (!*current) break;

        *current = '\0'; // Null terminate the row content
        current++; // Skip closing quote

        // Parse the row content (space-separated area names)
        char* token = strtok(row_content_start, " \t");
        int col = 0;

        while (token && col < MAX_GRID_SIZE) {
            strncpy(grid_cells[rows][col], token, MAX_NAME_LEN - 1);
            grid_cells[rows][col][MAX_NAME_LEN - 1] = '\0';
            col++;
            token = strtok(NULL, " \t");
        }

        if (col > cols) cols = col;
        rows++;
    }

    log_debug("Parsed grid: %d rows x %d columns\n", rows, cols);

    // Update grid layout dimensions
    grid->computed_row_count = rows;
    grid->computed_column_count = cols;

    // Extract unique areas and calculate their bounds
    char area_names[8][8]; // Max 8 unique areas
    int area_count = 0;

    // Find all unique area names
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const char* area_name = grid_cells[r][c];
            if (strlen(area_name) == 0 || strcmp(area_name, ".") == 0) continue;

            // Check if we already have this area name
            bool found = false;
            for (int i = 0; i < area_count; i++) {
                if (strcmp(area_names[i], area_name) == 0) {
                    found = true;
                    break;
                }
            }

            if (!found && area_count < 8) {
                strncpy(area_names[area_count], area_name, 7);
                area_names[area_count][7] = '\0';
                area_count++;
            }
        }
    }

    // Calculate bounds for each area
    for (int i = 0; i < area_count && grid->area_count < grid->allocated_areas; i++) {
        const char* area_name = area_names[i];
        int min_row = rows, max_row = -1;
        int min_col = cols, max_col = -1;

        // Find the bounding rectangle for this area
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

        // Validate that the area forms a rectangle
        bool is_rectangle = true;
        for (int r = min_row; r <= max_row && is_rectangle; r++) {
            for (int c = min_col; c <= max_col && is_rectangle; c++) {
                if (strcmp(grid_cells[r][c], area_name) != 0) {
                    is_rectangle = false;
                    log_debug("Warning: Area '%s' is not rectangular\n", area_name);
                }
            }
        }

        if (is_rectangle && min_row <= max_row && min_col <= max_col) {
            log_debug("Creating area '%s' - bounds: row %d-%d, col %d-%d\n", area_name, min_row, max_row, min_col, max_col);
            log_debug("area_count=%d, allocated_areas=%d\n", grid->area_count, grid->allocated_areas);

            // Create the area (convert to 1-based indexing)
            GridArea area;
            log_debug("area_name pointer=%p\n", area_name);
            if (!area_name) {
                log_debug("ERROR: area_name is NULL!\n");
                continue;
            }
            log_debug("About to strncpy area_name='%s'\n", area_name);
            strncpy(area.name, area_name, sizeof(area.name) - 1);
            area.name[sizeof(area.name) - 1] = '\0';
            log_debug("strncpy completed\n");
            area.row_start = min_row + 1;
            area.row_end = max_row + 2;
            area.column_start = min_col + 1;
            area.column_end = max_col + 2;

            log_debug("About to assign area to grid_areas[%d]\n", grid->area_count);
            grid->grid_areas[grid->area_count] = area;
            log_debug("Area assigned successfully\n");
            grid->area_count++;

            log_debug("Created area '%s': rows %d-%d, columns %d-%d\n",
                     area_name, area.row_start, area.row_end - 1,
                     area.column_start, area.column_end - 1);
        }
    }

    log_debug("Successfully parsed %d grid areas\n", grid->area_count);
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
    // ViewBlock inherits from DomNode through the chain: ViewBlock -> ViewSpan -> ViewGroup -> DomElement -> DomNode
    DomNode* node = (DomNode*)item;
    if (lycon && node) {
        if (is_row_axis) {
            // For row axis, we're measuring height
            // Height depends on width, so we need to determine the correct width to use:
            // 1. If item->width is already set, use it
            // 2. Otherwise, try to calculate from computed column tracks (if sized)
            // 3. For FR tracks (computed_size == 0), estimate from container width
            // 4. Fall back to a reasonable estimate
            int width = 200; // Default fallback
            
            if (item->width > 0) {
                // Item already has a width (e.g., from previous layout pass)
                width = (int)item->width;
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
                                box_adjustment += item->bound->border->width.left + item->bound->border->width.right;
                            }
                        }
                        width = span_width - box_adjustment;
                        if (width < 10) width = 10;
                        log_debug("Row sizing: using column span width %d for %s (cols %d-%d)",
                                  width, item->node_name(), col_start + 1, col_end);
                    } else if (grid->content_width > 0) {
                        // FR tracks not sized yet - estimate from container width
                        // For a simple estimate, divide container width by column count
                        int col_count = grid->computed_column_count > 0 ? grid->computed_column_count : 1;
                        int total_gaps = (col_count - 1) * (int)grid->column_gap;
                        int span_cols = col_end - col_start;
                        width = ((grid->content_width - total_gaps) * span_cols) / col_count;
                        
                        // Subtract item's own padding/border
                        if (item->bound) {
                            width -= item->bound->padding.left + item->bound->padding.right;
                            if (item->bound->border) {
                                width -= item->bound->border->width.left + item->bound->border->width.right;
                            }
                        }
                        if (width < 10) width = 10;
                        log_debug("Row sizing: estimating width %d for %s (FR tracks, container=%d, cols=%d)",
                                  width, item->node_name(), grid->content_width, col_count);
                    }
                }
            }
            
            sizes.min_content = calculate_min_content_height(lycon, node, width);
            sizes.max_content = calculate_max_content_height(lycon, node, width);

            // Don't force minimum height - empty items should have 0 height
            // The grid algorithm will handle minimum track sizes based on content
            if (sizes.max_content < sizes.min_content) {
                sizes.max_content = sizes.min_content;
            }
        } else {
            // For column axis, we're measuring width
            sizes.min_content = calculate_min_content_width(lycon, node);
            sizes.max_content = calculate_max_content_width(lycon, node);

            // Don't force minimum width - empty items should have 0 width
            if (sizes.max_content < sizes.min_content) {
                sizes.max_content = sizes.min_content;
            }
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
