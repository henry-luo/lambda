#include "grid.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
}

// Create a minmax() track size
GridTrackSize* create_minmax_track_size(GridTrackSize* min_size, GridTrackSize* max_size) {
    if (!min_size || !max_size) return NULL;

    GridTrackSize* track_size = (GridTrackSize*)mem_calloc(1, sizeof(GridTrackSize), MEM_CAT_LAYOUT);
    if (!track_size) return NULL;

    track_size->type = GRID_TRACK_SIZE_MINMAX;
    track_size->min_size = min_size;
    track_size->max_size = max_size;
    track_size->value = 0;
    track_size->is_percentage = false;

    log_debug("Created minmax track size\n");
    return track_size;
}

// Resolve minmax() track size to actual pixel value
int resolve_minmax_track_size(GridTrackSize* track_size, int available_space, int min_content, int max_content) {
    if (!track_size || track_size->type != GRID_TRACK_SIZE_MINMAX) return 0;

    // Resolve minimum size
    int min_value = 0;
    switch (track_size->min_size->type) {
        case GRID_TRACK_SIZE_LENGTH:
            min_value = track_size->min_size->value;
            break;
        case GRID_TRACK_SIZE_PERCENTAGE:
            min_value = (available_space * track_size->min_size->value) / 100;
            break;
        case GRID_TRACK_SIZE_MIN_CONTENT:
            min_value = min_content;
            break;
        case GRID_TRACK_SIZE_MAX_CONTENT:
            min_value = max_content;
            break;
        case GRID_TRACK_SIZE_AUTO:
            min_value = min_content;
            break;
        default:
            min_value = 0;
    }

    // Resolve maximum size
    int max_value = 0;
    switch (track_size->max_size->type) {
        case GRID_TRACK_SIZE_LENGTH:
            max_value = track_size->max_size->value;
            break;
        case GRID_TRACK_SIZE_PERCENTAGE:
            max_value = (available_space * track_size->max_size->value) / 100;
            break;
        case GRID_TRACK_SIZE_MIN_CONTENT:
            max_value = min_content;
            break;
        case GRID_TRACK_SIZE_MAX_CONTENT:
            max_value = max_content;
            break;
        case GRID_TRACK_SIZE_AUTO:
            max_value = max_content;
            break;
        case GRID_TRACK_SIZE_FR:
            // For fr units in max, treat as flexible
            max_value = available_space; // Will be resolved later in flexible track expansion
            break;
        default:
            max_value = available_space;
    }

    // Return the constrained value
    int result = max(min_value, min(max_value, available_space));
    log_debug("Resolved minmax(%d, %d) = %d\n", min_value, max_value, result);
    return result;
}

// Create a repeat() track size
GridTrackSize* create_repeat_track_size(int repeat_count, GridTrackSize** repeat_tracks, int track_count) {
    if (repeat_count <= 0 || !repeat_tracks || track_count <= 0) return NULL;

    GridTrackSize* track_size = (GridTrackSize*)mem_calloc(1, sizeof(GridTrackSize), MEM_CAT_LAYOUT);
    if (!track_size) return NULL;

    track_size->type = GRID_TRACK_SIZE_REPEAT;
    track_size->repeat_count = repeat_count;
    track_size->repeat_tracks = (GridTrackSize**)mem_calloc(track_count, sizeof(GridTrackSize*), MEM_CAT_LAYOUT);
    track_size->repeat_track_count = track_count;
    track_size->is_auto_fill = false;
    track_size->is_auto_fit = false;

    // Copy the track references
    for (int i = 0; i < track_count; i++) {
        track_size->repeat_tracks[i] = repeat_tracks[i];
    }

    log_debug("Created repeat(%d, %d tracks) track size\n", repeat_count, track_count);
    return track_size;
}

// Create an auto-repeat track size (auto-fill or auto-fit)
GridTrackSize* create_auto_repeat_track_size(bool is_auto_fill, GridTrackSize** repeat_tracks, int track_count) {
    if (!repeat_tracks || track_count <= 0) return NULL;

    GridTrackSize* track_size = (GridTrackSize*)mem_calloc(1, sizeof(GridTrackSize), MEM_CAT_LAYOUT);
    if (!track_size) return NULL;

    track_size->type = GRID_TRACK_SIZE_REPEAT;
    track_size->repeat_count = 0; // Will be calculated based on available space
    track_size->repeat_tracks = (GridTrackSize**)mem_calloc(track_count, sizeof(GridTrackSize*), MEM_CAT_LAYOUT);
    track_size->repeat_track_count = track_count;
    track_size->is_auto_fill = is_auto_fill;
    track_size->is_auto_fit = !is_auto_fill;

    // Copy the track references
    for (int i = 0; i < track_count; i++) {
        track_size->repeat_tracks[i] = repeat_tracks[i];
    }

    log_debug("Created auto-%s repeat track size with %d tracks\n",
              is_auto_fill ? "fill" : "fit", track_count);
    return track_size;
}

// Expand repeat() tracks in a track list
void expand_repeat_tracks(GridTrackList* track_list, int available_space) {
    if (!track_list) return;

    log_debug("Expanding repeat tracks in track list\n");

    // Find repeat tracks and expand them
    for (int i = 0; i < track_list->track_count; i++) {
        GridTrackSize* track = track_list->tracks[i];
        if (!track || track->type != GRID_TRACK_SIZE_REPEAT) continue;

        int repeat_count = track->repeat_count;

        // For auto-fill/auto-fit, calculate repeat count based on available space
        if (track->is_auto_fill || track->is_auto_fit) {
            // Calculate minimum size of one repetition
            int pattern_min_size = 0;
            for (int j = 0; j < track->repeat_track_count; j++) {
                GridTrackSize* repeat_track = track->repeat_tracks[j];
                if (repeat_track->type == GRID_TRACK_SIZE_LENGTH) {
                    pattern_min_size += repeat_track->value;
                } else {
                    pattern_min_size += 100; // Default minimum for flexible tracks
                }
            }

            if (pattern_min_size > 0) {
                repeat_count = max(1, available_space / pattern_min_size);
            } else {
                repeat_count = 1;
            }

            log_debug("Auto-repeat calculated count: %d (pattern size: %d, available: %d)\n",
                     repeat_count, pattern_min_size, available_space);
        }

        // TODO: Implement actual track list expansion
        // This would involve reallocating the track list and inserting the repeated tracks
        log_debug("Would expand repeat track %d times\n", repeat_count);
    }
}

// Enhanced auto-placement with dense packing
void auto_place_grid_items_dense(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug("Starting dense auto-placement\n");

    // Dense packing tries to fill holes in the grid by placing items
    // in the earliest possible position, even if it's before previous items

    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        if (!item || !item->gi || !item->gi->is_grid_auto_placed) continue;

        // Try to place the item starting from the beginning of the grid
        bool placed = false;
        for (int row = 1; row <= grid_layout->computed_row_count && !placed; row++) {
            for (int col = 1; col <= grid_layout->computed_column_count && !placed; col++) {
                if (try_place_item_dense(grid_layout, item, row, col)) {
                    placed = true;
                    log_debug("Dense placement: item %d placed at (%d, %d)\n", i, row, col);
                }
            }
        }

        if (!placed) {
            log_debug("Could not place item %d with dense packing\n", i);
        }
    }

    log_debug("Dense auto-placement completed\n");
}

// Try to place an item at a specific position for dense packing
bool try_place_item_dense(GridContainerLayout* grid_layout, ViewBlock* item, int start_row, int start_col) {
    if (!grid_layout || !item || !item->gi) return false;

    // Calculate item span (default to 1x1 if not specified)
    int row_span = (item->gi->computed_grid_row_end > item->gi->computed_grid_row_start) ?
                   (item->gi->computed_grid_row_end - item->gi->computed_grid_row_start) : 1;
    int col_span = (item->gi->computed_grid_column_end > item->gi->computed_grid_column_start) ?
                   (item->gi->computed_grid_column_end - item->gi->computed_grid_column_start) : 1;

    // Check if the item fits at this position
    int end_row = start_row + row_span;
    int end_col = start_col + col_span;

    if (end_row > grid_layout->computed_row_count + 1 ||
        end_col > grid_layout->computed_column_count + 1) {
        return false; // Doesn't fit in grid
    }

    // Check for conflicts with existing items
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* existing_item = grid_layout->grid_items[i];
        if (!existing_item || existing_item == item || !existing_item->gi) continue;

        // Check if areas overlap
        bool row_overlap = !(end_row <= existing_item->gi->computed_grid_row_start ||
                           existing_item->gi->computed_grid_row_end <= start_row);
        bool col_overlap = !(end_col <= existing_item->gi->computed_grid_column_start ||
                           existing_item->gi->computed_grid_column_end <= start_col);

        if (row_overlap && col_overlap) {
            return false; // Conflict detected
        }
    }

    // Place the item
    item->gi->computed_grid_row_start = start_row;
    item->gi->computed_grid_row_end = end_row;
    item->gi->computed_grid_column_start = start_col;
    item->gi->computed_grid_column_end = end_col;

    return true;
}

// Parse grid template tracks (enhanced implementation)
void parse_grid_template_tracks(GridTrackList* track_list, const char* template_string) {
    if (!track_list || !template_string) {
        log_debug("parse_grid_template_tracks early return - track_list=%p, template_string=%p\n", track_list, template_string);
        return;
    }

    // CRITICAL: Validate input length to prevent buffer overflow
    size_t input_len = strlen(template_string);
    if (input_len >= 512) {
        log_debug("ERROR: Template string too long (%zu chars), truncating to 511\n", input_len);
    }

    log_debug("Parsing grid template tracks: '%s' (length: %zu)\n", template_string, input_len);

    // Clear existing tracks
    track_list->track_count = 0;

    // This is an enhanced parser that handles basic cases properly
    char work_string[512];
    strncpy(work_string, template_string, sizeof(work_string) - 1);
    work_string[sizeof(work_string) - 1] = '\0';

    // Split by spaces and parse each track
    char* token = strtok(work_string, " \t");
    int safety_counter = 0;
    const int MAX_TOKENS = 32; // Safety limit to prevent infinite loops

    while (token && track_list->track_count < track_list->allocated_tracks && safety_counter < MAX_TOKENS) {
        safety_counter++;
        GridTrackSize* track_size = NULL;

        log_debug("Parsing token: '%s'\n", token);

        // Check for different track types
        if (strstr(token, "minmax(")) {
            // TODO: Parse minmax function
            log_debug("Found minmax function: %s\n", token);
            track_size = create_grid_track_size(GRID_TRACK_SIZE_AUTO, 0);
        } else if (strstr(token, "repeat(")) {
            // TODO: Parse repeat function
            log_debug("Found repeat function: %s\n", token);
            track_size = create_grid_track_size(GRID_TRACK_SIZE_AUTO, 0);
        } else if (strstr(token, "fr")) {
            // Fractional unit - enhanced parsing
            float fr_value = strtof(token, NULL);
            log_debug("Parsed fr value: %.2f from token '%s'\n", fr_value, token);

            // Store as integer multiplied by 100 for precision
            int stored_value = (int)(fr_value * 100);
            track_size = create_grid_track_size(GRID_TRACK_SIZE_FR, stored_value);

            log_debug("Created FR track with stored_value=%d (%.2ffr)\n", stored_value, fr_value);
        } else if (strstr(token, "px")) {
            // Pixel value
            int px_value = strtol(token, NULL, 10);
            log_debug("Parsed px value: %d from token '%s'\n", px_value, token);
            track_size = create_grid_track_size(GRID_TRACK_SIZE_LENGTH, px_value);
        } else if (strstr(token, "%")) {
            // Percentage value
            float percent_value = strtof(token, NULL);
            log_debug("Parsed percentage value: %.2f from token '%s'\n", percent_value, token);
            track_size = create_grid_track_size(GRID_TRACK_SIZE_PERCENTAGE, (int)percent_value);
            track_size->is_percentage = true;
        } else if (strcmp(token, "auto") == 0) {
            log_debug("Parsed auto track\n");
            track_size = create_grid_track_size(GRID_TRACK_SIZE_AUTO, 0);
        } else if (strcmp(token, "min-content") == 0) {
            log_debug("Parsed min-content track\n");
            track_size = create_grid_track_size(GRID_TRACK_SIZE_MIN_CONTENT, 0);
        } else if (strcmp(token, "max-content") == 0) {
            log_debug("Parsed max-content track\n");
            track_size = create_grid_track_size(GRID_TRACK_SIZE_MAX_CONTENT, 0);
        } else {
            // Try to parse as a number (could be unitless)
            char* endptr;
            float numeric_value = strtof(token, &endptr);
            if (endptr != token && *endptr == '\0') {
                // Pure number, treat as pixels
                log_debug("Parsed unitless number: %.2f, treating as pixels\n", numeric_value);
                track_size = create_grid_track_size(GRID_TRACK_SIZE_LENGTH, (int)numeric_value);
            } else {
                log_debug("Unknown token format: '%s', treating as auto\n", token);
                track_size = create_grid_track_size(GRID_TRACK_SIZE_AUTO, 0);
            }
        }

        if (track_size) {
            // Ensure we have space
            if (track_list->track_count >= track_list->allocated_tracks) {
                log_debug("Expanding track list capacity from %d to %d\n",
                       track_list->allocated_tracks, track_list->allocated_tracks * 2);

                int new_capacity = track_list->allocated_tracks * 2;
                GridTrackSize** new_tracks = (GridTrackSize**)realloc(track_list->tracks,
                                                                     new_capacity * sizeof(GridTrackSize*));

                // CRITICAL: Check if realloc failed
                if (!new_tracks) {
                    log_debug("ERROR: Failed to reallocate track list memory\n");
                    // Clean up the track_size we just created
                    destroy_grid_track_size(track_size);
                    return; // Exit parsing to prevent crash
                }

                track_list->tracks = new_tracks;
                track_list->allocated_tracks = new_capacity;
            }

            track_list->tracks[track_list->track_count] = track_size;
            log_debug("Added track %d: type=%d, value=%d\n",
                   track_list->track_count, track_size->type, track_size->value);
            track_list->track_count++;
        } else {
            log_debug("Failed to create track for token '%s'\n", token);
        }

        token = strtok(NULL, " \t");
    }

    // CRITICAL: Check if we hit safety limits
    if (safety_counter >= MAX_TOKENS) {
        log_debug("WARNING: Hit safety limit parsing template string, may have truncated tracks\n");
    }

    log_debug("Finished parsing - created %d tracks (processed %d tokens)\n",
           track_list->track_count, safety_counter);

    // Debug: Print all parsed tracks
    for (int i = 0; i < track_list->track_count; i++) {
        GridTrackSize* track = track_list->tracks[i];
        // CRITICAL: Check for null pointer before accessing
        if (track) {
            log_debug("Track %d - type=%d, value=%d\n", i, track->type, track->value);
        } else {
            log_debug("Track %d - NULL POINTER!\n", i);
        }
    }
}

// Parse minmax() function (simplified)
void parse_minmax_function(const char* minmax_str, GridTrackSize** min_size, GridTrackSize** max_size) {
    if (!minmax_str || !min_size || !max_size) return;

    log_debug("Parsing minmax function: %s\n", minmax_str);

    // This is a placeholder for full minmax parsing
    // Full implementation would parse the CSS function syntax

    *min_size = create_grid_track_size(GRID_TRACK_SIZE_MIN_CONTENT, 0);
    *max_size = create_grid_track_size(GRID_TRACK_SIZE_MAX_CONTENT, 0);

    log_debug("Created placeholder minmax sizes\n");
}

// Parse repeat() function (simplified)
void parse_repeat_function(const char* repeat_str, GridTrackSize** result) {
    if (!repeat_str || !result) return;

    log_debug("Parsing repeat function: %s\n", repeat_str);

    // This is a placeholder for full repeat parsing
    // Full implementation would parse the CSS function syntax

    *result = create_grid_track_size(GRID_TRACK_SIZE_AUTO, 0);

    log_debug("Created placeholder repeat track\n");
}
