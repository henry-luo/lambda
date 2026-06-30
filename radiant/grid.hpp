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
#include "form_control.hpp"

// Forward declarations
struct ViewBlock;
struct LayoutContext;

inline GridItemProp* grid_item_prop(ViewBlock* item) {
    if (!item) return nullptr;
    if (item->item_prop_type == DomElement::ITEM_PROP_GRID) return item->gi;
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) return item->form->grid_item;
    return nullptr;
}

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
    float computed_size;         // Final computed size in pixels
    float base_size;             // Base size for fr calculations
    float growth_limit;          // Growth limit for fr calculations
    bool is_flexible;            // Has fr units
    bool is_implicit;            // Created by auto-placement
    bool owns_size;              // True if we created size and should free it
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
    // Note: grid_auto_rows and grid_auto_columns are inherited from GridProp

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

    // Negative implicit track counts (tracks before the explicit grid)
    int negative_implicit_row_count;
    int negative_implicit_column_count;

    // Auto-placement cursor (for tracking current position during auto-placement)
    int auto_row_cursor;
    int auto_col_cursor;

    // Container dimensions
    float container_width;
    float container_height;
    float content_width;         // Width excluding padding/border
    float content_height;        // Height excluding padding/border
    bool has_explicit_height;    // True if container has CSS height set (not auto)
    bool is_shrink_to_fit_width; // True if container should shrink-to-fit (abs pos, no explicit width)
    float row_intrinsic_height;  // First-pass row height before pct re-resolution (-1 if not applicable)

    // Layout context for intrinsic sizing (set during init_grid_container)
    struct LayoutContext* lycon;

    // Auto-fit track indices (for collapsing empty tracks after placement)
    // These mark which column/row indices in the expanded template came from
    // a repeat(auto-fit, ...) expansion so we can collapse empty ones.
    bool auto_fit_columns[64];   // true for columns from auto-fit expansion
    bool auto_fit_rows[64];      // true for rows from auto-fit expansion
    int auto_fit_col_count;      // number of entries in auto_fit_columns
    int auto_fit_row_count;      // number of entries in auto_fit_rows

    // Ownership flags - if true, we own these and should free them
    bool owns_template_rows;
    bool owns_template_columns;
    bool owns_auto_rows;
    bool owns_auto_columns;
    bool owns_grid_areas;
} GridContainerLayout;

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
GridTrackSize* clone_grid_track_size(const GridTrackSize* track_size);
void destroy_grid_track_size(GridTrackSize* track_size);

// Grid area functions
GridArea* create_grid_area(const char* name, int row_start, int row_end, int column_start, int column_end);
void destroy_grid_area(GridArea* area);

// Grid line name functions
void add_grid_line_name(GridContainerLayout* grid, const char* name, int line_number, bool is_row);
int find_grid_line_by_name(GridContainerLayout* grid, const char* name, bool is_row);

// Grid item collection and placement
int collect_grid_items(GridContainerLayout* grid_layout, struct ViewBlock* container, struct ViewBlock*** items);

// Grid sizing algorithm
void determine_grid_size(GridContainerLayout* grid_layout);
void initialize_track_sizes(GridContainerLayout* grid_layout);

// Enhanced track sizing algorithm (uses Taffy-inspired implementation)
void resolve_track_sizes_enhanced(GridContainerLayout* grid_layout, struct ViewBlock* container);

// Grid item positioning and alignment
void position_grid_items(GridContainerLayout* grid_layout, struct ViewBlock* container, ScratchArena* sa);
void align_grid_items(GridContainerLayout* grid_layout);
void align_grid_item(struct ViewBlock* item, GridContainerLayout* grid_layout);

// Grid template area parsing
void parse_grid_template_areas(GridProp* grid_layout, const char* areas_string, ScratchArena* sa);
void resolve_grid_template_areas(GridContainerLayout* grid_layout);

// Grid content layout functions - now in layout_grid_multipass.cpp
// See layout_grid_multipass.hpp for the new multi-pass API

#ifdef __cplusplus
}

// C++ function declarations (outside extern "C")
IntrinsicSizes calculate_grid_item_intrinsic_sizes(LayoutContext* lycon, ViewBlock* item, bool is_row_axis);
void layout_grid_container(LayoutContext* lycon, ViewBlock* container);
#endif
