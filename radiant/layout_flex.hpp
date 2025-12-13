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
    int main_size;            // Total size along main axis
    int cross_size;           // Size along cross axis (height of tallest item)
    int cross_position;       // Position along cross axis (set by align_content)
    int free_space;           // Available space for distribution
    float total_flex_grow;    // Sum of flex-grow values
    float total_flex_shrink;  // Sum of flex-shrink values
    int baseline;             // Baseline for alignment
} FlexLineInfo;

// Main flex layout functions
void init_flex_container(LayoutContext* lycon, ViewBlock* container);
void cleanup_flex_container(LayoutContext* lycon);
void layout_flex_container(LayoutContext* lycon, ViewBlock* container);

// Flex item collection and management
// UNIFIED: Single-pass collection that combines measurement + View creation + collection
int collect_and_prepare_flex_items(LayoutContext* lycon, FlexContainerLayout* flex_layout, ViewBlock* container);
// DEPRECATED: Use collect_and_prepare_flex_items instead (kept for compatibility)
int collect_flex_items(FlexContainerLayout* flex_layout, ViewBlock* container, View*** items);
void sort_flex_items_by_order(View** items, int count);

// Flex line creation
int create_flex_lines(FlexContainerLayout* flex_layout, View** items, int item_count);
void calculate_line_cross_sizes(FlexContainerLayout* flex_layout);

// Hypothetical cross size determination (CSS Flexbox spec §9.4)
// Computes hypothetical cross sizes for all flex items before line cross-size calculation
void determine_hypothetical_cross_sizes(LayoutContext* lycon, FlexContainerLayout* flex_layout);

// Container cross size determination (CSS Flexbox spec §9.4)
// Computes the flex container's cross size from line cross sizes
void determine_container_cross_size(FlexContainerLayout* flex_layout, ViewBlock* container);

// Flexible length resolution
void resolve_flexible_lengths(FlexContainerLayout* flex_layout, FlexLineInfo* line);
int calculate_flex_basis(ViewBlock* item, FlexContainerLayout* flex_layout);
void distribute_free_space(FlexLineInfo* line, bool is_growing);

// Constraint resolution
void resolve_flex_item_constraints(ViewElement* item, FlexContainerLayout* flex_layout);
void apply_constraints_to_flex_items(FlexContainerLayout* flex_layout);

// Consolidated constraint application (Task 4)
// Single source of truth for applying min/max constraints to flex items
int apply_flex_constraint(ViewElement* item, int computed_size, bool is_main_axis,
                          FlexContainerLayout* flex_layout, bool* hit_min, bool* hit_max);
int apply_flex_constraint(ViewElement* item, int computed_size, bool is_main_axis,
                          FlexContainerLayout* flex_layout);
int apply_stretch_constraint(ViewElement* item, int container_cross_size,
                             FlexContainerLayout* flex_layout);

// Alignment functions
void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line);
void align_items_cross_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line);
void align_content(FlexContainerLayout* flex_layout);

// Overflow fallback alignment (Yoga-inspired)
// Returns safe alignment value when remaining space is negative
int fallback_alignment(int align);
int fallback_justify(int justify);

// Baseline calculation (Yoga-inspired)
// Recursive baseline calculation through nested flex containers
float calculate_baseline_recursive(View* node, FlexContainerLayout* flex_layout);
bool is_baseline_layout(ViewBlock* node, FlexContainerLayout* flex_layout);

// Wrap-reverse final position adjustment
void apply_wrap_reverse_positions(FlexContainerLayout* flex_layout, ViewBlock* container);

// Baseline repositioning after nested content layout
// Called after layout_final_flex_content() to recalculate baselines with actual child dimensions
void reposition_baseline_items(LayoutContext* lycon, ViewBlock* flex_container);

// Utility functions
bool is_main_axis_horizontal(FlexProp* flex);
int get_main_axis_size(ViewElement* item, FlexContainerLayout* flex_layout);
int get_cross_axis_size(ViewElement* item, FlexContainerLayout* flex_layout);
int get_cross_axis_position(ViewElement* item, FlexContainerLayout* flex_layout);
void set_main_axis_position(ViewElement* item, int position, FlexContainerLayout* flex_layout);
void set_cross_axis_position(ViewElement* item, int position, FlexContainerLayout* flex_layout);
void set_main_axis_size(ViewElement* item, int size, FlexContainerLayout* flex_layout);
void set_cross_axis_size(ViewElement* item, int size, FlexContainerLayout* flex_layout);

// Helper functions for constraints and percentages
float clamp_value(float value, float min_val, float max_val);
int resolve_percentage(int value, bool is_percent, int container_size);
void apply_constraints(ViewElement* item, int container_width, int container_height);
int find_max_baseline(FlexLineInfo* line);
bool is_valid_flex_item(ViewElement* item);

// Gap handling
int calculate_gap_space(FlexContainerLayout* flex_layout, int item_count, bool is_main_axis);
void apply_gaps(FlexContainerLayout* flex_layout, FlexLineInfo* line);

#endif // LAYOUT_FLEX_HPP
