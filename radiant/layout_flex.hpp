#ifndef LAYOUT_FLEX_HPP
#define LAYOUT_FLEX_HPP

#include "view.hpp"
#include "layout.hpp"

// Flex direction enum (matches CSS values)
typedef enum {
    DIR_ROW = CSS_VALUE_ROW,
    DIR_ROW_REVERSE = CSS_VALUE_ROW_REVERSE,
    DIR_COLUMN = CSS_VALUE_COLUMN,
    DIR_COLUMN_REVERSE = CSS_VALUE_COLUMN_REVERSE
} FlexDirection;

// Flex wrap enum (matches CSS values)
typedef enum {
    WRAP_NOWRAP = CSS_VALUE_NOWRAP,
    WRAP_WRAP = CSS_VALUE_WRAP,
    WRAP_WRAP_REVERSE = CSS_VALUE_WRAP_REVERSE
} FlexWrap;

// Justify content enum (matches CSS values)
typedef enum {
    JUSTIFY_START = CSS_VALUE_FLEX_START,
    JUSTIFY_END = CSS_VALUE_FLEX_END,
    JUSTIFY_CENTER = CSS_VALUE_CENTER,
    JUSTIFY_SPACE_BETWEEN = CSS_VALUE_SPACE_BETWEEN,
    JUSTIFY_SPACE_AROUND = CSS_VALUE_SPACE_AROUND,
    JUSTIFY_SPACE_EVENLY = CSS_VALUE_SPACE_EVENLY
} JustifyContent;

// Flex line information for layout calculations
typedef struct FlexLineInfo {
    View** items;  // Items in this line
    int item_count;
    float main_size;            // Total size along main axis
    float cross_size;           // Size along cross axis (height of tallest item)
    float cross_position;       // Position along cross axis (set by align_content)
    float free_space;           // Available space for distribution
    float total_flex_grow;    // Sum of flex-grow values
    float total_flex_shrink;  // Sum of flex-shrink values
    float baseline;             // Baseline for alignment
} FlexLineInfo;

// Main flex layout functions
void init_flex_container(LayoutContext* lycon, ViewBlock* container);
void cleanup_flex_container(LayoutContext* lycon);
void layout_flex_container(LayoutContext* lycon, ViewBlock* container);

// Flex item collection and management
int collect_and_prepare_flex_items(LayoutContext* lycon, FlexContainerLayout* flex_layout, ViewBlock* container);

// Flexible length resolution
float calculate_flex_basis(ViewElement* item, FlexContainerLayout* flex_layout);

// Constraint resolution
void resolve_flex_item_constraints(ViewElement* item, FlexContainerLayout* flex_layout);
void apply_constraints_to_flex_items(FlexContainerLayout* flex_layout);

// Consolidated constraint application (Task 4)
// Single source of truth for applying min/max constraints to flex items
float apply_flex_constraint(ViewElement* item, float computed_size, bool is_main_axis,
                          FlexContainerLayout* flex_layout, bool* hit_min, bool* hit_max);
float apply_flex_constraint(ViewElement* item, float computed_size, bool is_main_axis,
                          FlexContainerLayout* flex_layout);
float apply_stretch_constraint(ViewElement* item, float container_cross_size,
                             FlexContainerLayout* flex_layout);

// Alignment functions
void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line);
void align_items_cross_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line);
void align_content(FlexContainerLayout* flex_layout);

// Baseline repositioning after nested content layout
// Called after layout_final_flex_content() to recalculate baselines with actual child dimensions
void reposition_baseline_items(LayoutContext* lycon, ViewBlock* flex_container);

// Utility functions
bool is_main_axis_horizontal(FlexProp* flex);
float get_main_axis_size(ViewElement* item, FlexContainerLayout* flex_layout);
float get_cross_axis_size(ViewElement* item, FlexContainerLayout* flex_layout);
float get_cross_axis_position(ViewElement* item, FlexContainerLayout* flex_layout);
void set_main_axis_position(ViewElement* item, float position, FlexContainerLayout* flex_layout);
void set_cross_axis_position(ViewElement* item, float position, FlexContainerLayout* flex_layout);
void set_main_axis_size(ViewElement* item, float size, FlexContainerLayout* flex_layout);
void set_cross_axis_size(ViewElement* item, float size, FlexContainerLayout* flex_layout);

// Flex item property helpers (support both flex items and form controls)
// Form controls store flex properties in FormControlProp instead of FlexItemProp
float get_item_flex_grow(ViewElement* item);
float get_item_flex_shrink(ViewElement* item);
float get_item_flex_basis(ViewElement* item);
bool get_item_flex_basis_is_percent(ViewElement* item);

// Helper functions for constraints and percentages
float find_max_baseline(FlexLineInfo* line, int container_align_items);

// Gap handling
float calculate_gap_space(FlexContainerLayout* flex_layout, int item_count, bool is_main_axis);

#endif // LAYOUT_FLEX_HPP
