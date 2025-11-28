#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef __cplusplus
}
#endif

#include "layout.hpp"

// Forward declarations
struct ViewBlock;
struct LayoutContext;

// IntrinsicSizes type is now defined in view.hpp (shared with flex layout)
// typedef struct {
//     int min_content;  // Minimum content width (longest word/element)
//     int max_content;  // Maximum content width (no wrapping)
// } IntrinsicSizes;

// Grid track size types following CSS Grid specification
typedef enum {
    GRID_TRACK_SIZE_LENGTH,      // Fixed length (px, em, etc.)
    GRID_TRACK_SIZE_PERCENTAGE,  // Percentage of container
    GRID_TRACK_SIZE_FR,          // Fractional unit
    GRID_TRACK_SIZE_MIN_CONTENT, // min-content
    GRID_TRACK_SIZE_MAX_CONTENT, // max-content
    GRID_TRACK_SIZE_AUTO,        // auto
    GRID_TRACK_SIZE_FIT_CONTENT, // fit-content()
    GRID_TRACK_SIZE_MINMAX,      // minmax()
    GRID_TRACK_SIZE_REPEAT       // repeat()
} GridTrackSizeType;

// Grid track size definition
typedef struct GridTrackSize {
    GridTrackSizeType type;
    int value;                   // Length value or percentage
    bool is_percentage;
    struct GridTrackSize* min_size;     // For minmax()
    struct GridTrackSize* max_size;     // For minmax()
    int fit_content_limit;       // For fit-content()

    // For repeat() function
    int repeat_count;            // Number of repetitions (0 = auto-fill/auto-fit)
    struct GridTrackSize** repeat_tracks; // Array of track sizes to repeat
    int repeat_track_count;      // Number of tracks in repeat pattern
    bool is_auto_fill;           // repeat(auto-fill, ...)
    bool is_auto_fit;            // repeat(auto-fit, ...)
} GridTrackSize;

// Grid track list for template definitions
typedef struct GridTrackList {
    GridTrackSize** tracks;
    int track_count;
    int allocated_tracks;
    char** line_names;           // Named grid lines
    int line_name_count;
    bool is_repeat;              // Contains repeat() function
    int repeat_count;            // Number of repetitions
} GridTrackList;

// Computed grid track
typedef struct GridTrack {
    GridTrackSize* size;
    int computed_size;           // Final computed size in pixels
    int base_size;               // Base size for fr calculations
    float growth_limit;          // Growth limit for fr calculations
    bool is_flexible;            // Has fr units
    bool is_implicit;            // Created by auto-placement
} GridTrack;

// Named grid area
typedef struct GridArea {
    char* name;                  // Named area identifier
    int row_start;
    int row_end;
    int column_start;
    int column_end;
} GridArea;

// Grid line name mapping
typedef struct GridLineName {
    char* name;
    int line_number;
    bool is_row;                 // true for row, false for column
} GridLineName;

// Grid container layout state
typedef struct GridContainerLayout : GridProp {
    // Grid auto properties
    GridTrackList* grid_auto_rows;
    GridTrackList* grid_auto_columns;

    // Computed grid properties
    GridTrack* computed_rows;
    GridTrack* computed_columns;

    // Grid items
    struct ViewBlock** grid_items;
    int item_count;
    int allocated_items;

    // Grid line names
    GridLineName* line_names;
    int line_name_count;
    int allocated_line_names;

    // Layout state
    bool needs_reflow;
    int explicit_row_count;
    int explicit_column_count;
    int implicit_row_count;
    int implicit_column_count;

    // Container dimensions
    int container_width;
    int container_height;
    int content_width;           // Width excluding padding/border
    int content_height;          // Height excluding padding/border
} GridContainerLayout;

// Grid item placement state
typedef struct GridItemPlacement {
    int row_start;
    int row_end;
    int column_start;
    int column_end;
    bool has_explicit_row_start;
    bool has_explicit_row_end;
    bool has_explicit_column_start;
    bool has_explicit_column_end;
    char* grid_area_name;
    bool is_auto_placed;
} GridItemPlacement;

// IntrinsicSizes is defined in layout_flex_content.hpp - include it when needed

// Grid sizing algorithm state
typedef struct GridSizingState {
    GridTrack* tracks;
    int track_count;
    struct ViewBlock** items;
    int item_count;
    int available_space;
    bool is_row_axis;            // true for rows, false for columns
} GridSizingState;

#ifdef __cplusplus
extern "C" {
#endif

// Grid container management functions
void init_grid_container(LayoutContext* lycon, struct ViewBlock* container);
void cleanup_grid_container(LayoutContext* lycon);

// Grid track functions
GridTrackList* create_grid_track_list(int initial_capacity);
void destroy_grid_track_list(GridTrackList* track_list);
GridTrackSize* create_grid_track_size(GridTrackSizeType type, int value);
void destroy_grid_track_size(GridTrackSize* track_size);

// Grid area functions
GridArea* create_grid_area(const char* name, int row_start, int row_end, int column_start, int column_end);
void destroy_grid_area(GridArea* area);

// Grid line name functions
void add_grid_line_name(GridContainerLayout* grid, const char* name, int line_number, bool is_row);
int find_grid_line_by_name(GridContainerLayout* grid, const char* name, bool is_row);

// Grid item collection and placement
int collect_grid_items(GridContainerLayout* grid_layout, struct ViewBlock* container, struct ViewBlock*** items);
void place_grid_items(GridContainerLayout* grid_layout, struct ViewBlock** items, int item_count);
void auto_place_grid_item(GridContainerLayout* grid_layout, struct ViewBlock* item, GridItemPlacement* placement);

// Grid sizing algorithm
void determine_grid_size(GridContainerLayout* grid_layout);
void resolve_track_sizes(GridContainerLayout* grid_layout, struct ViewBlock* container);
void initialize_track_sizes(GridContainerLayout* grid_layout);
void resolve_intrinsic_track_sizes(GridContainerLayout* grid_layout);
void maximize_tracks(GridContainerLayout* grid_layout);
void expand_flexible_tracks(GridContainerLayout* grid_layout, struct ViewBlock* container);
int calculate_track_intrinsic_size(GridContainerLayout* grid_layout, int track_index, bool is_row, GridTrackSizeType size_type);
void expand_flexible_tracks_in_axis(GridTrack* tracks, int track_count, int available_space);

// Grid item positioning and alignment
void position_grid_items(GridContainerLayout* grid_layout, struct ViewBlock* container);
void align_grid_items(GridContainerLayout* grid_layout);
void align_grid_item(struct ViewBlock* item, GridContainerLayout* grid_layout);

// Utility functions
bool is_valid_grid_item(struct ViewBlock* item);
bool is_grid_item(struct ViewBlock* block);
IntrinsicSizes calculate_grid_item_intrinsic_sizes(struct ViewBlock* item, bool is_row_axis);
int resolve_grid_line_position(GridContainerLayout* grid_layout, int line_value, const char* line_name, bool is_row, bool is_end_line);

// Grid template area parsing
void parse_grid_template_areas(GridProp* grid_layout, const char* areas_string);
void resolve_grid_template_areas(GridContainerLayout* grid_layout);

// Grid content layout functions
void layout_grid_item_content(struct LayoutContext* lycon, struct ViewBlock* grid_item);
void layout_grid_item_content_for_sizing(struct LayoutContext* lycon, struct ViewBlock* grid_item);
void layout_grid_item_final_content(struct LayoutContext* lycon, struct ViewBlock* grid_item);
void layout_grid_items_content(struct LayoutContext* lycon, struct GridContainerLayout* grid_layout);

// Advanced grid features (Phase 6)
// Minmax function support
struct GridTrackSize* create_minmax_track_size(struct GridTrackSize* min_size, struct GridTrackSize* max_size);
int resolve_minmax_track_size(struct GridTrackSize* track_size, int available_space, int min_content, int max_content);

// Repeat function support
struct GridTrackSize* create_repeat_track_size(int repeat_count, struct GridTrackSize** repeat_tracks, int track_count);
struct GridTrackSize* create_auto_repeat_track_size(bool is_auto_fill, struct GridTrackSize** repeat_tracks, int track_count);
void expand_repeat_tracks(GridTrackList* track_list, int available_space);

// Enhanced auto-placement with dense packing
void auto_place_grid_items_dense(GridContainerLayout* grid_layout);
bool try_place_item_dense(GridContainerLayout* grid_layout, struct ViewBlock* item, int start_row, int start_col);

// Track template parsing
void parse_grid_template_tracks(GridTrackList* track_list, const char* template_string);
void parse_minmax_function(const char* minmax_str, struct GridTrackSize** min_size, struct GridTrackSize** max_size);
void parse_repeat_function(const char* repeat_str, struct GridTrackSize** result);

#ifdef __cplusplus
}

// C++ function declarations (outside extern "C")
void layout_grid_container(LayoutContext* lycon, ViewBlock* container);
#endif
