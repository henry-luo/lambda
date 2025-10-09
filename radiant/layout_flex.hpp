#ifndef LAYOUT_FLEX_HPP
#define LAYOUT_FLEX_HPP

#include "flex.hpp"
#include "view.hpp"
#include "layout.hpp"

// Flex line information for layout calculations
typedef struct FlexLineInfo {
    ViewBlock** items;  // Items in this line
    int item_count;
    int main_size;            // Total size along main axis
    int cross_size;           // Size along cross axis (height of tallest item)
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
int collect_flex_items(FlexContainerLayout* flex_layout, ViewBlock* container, ViewBlock*** items);
void sort_flex_items_by_order(ViewBlock** items, int count);

// Flex line creation
int create_flex_lines(FlexContainerLayout* flex_layout, ViewBlock** items, int item_count);
void calculate_line_cross_sizes(FlexContainerLayout* flex_layout);

// Flexible length resolution
void resolve_flexible_lengths(FlexContainerLayout* flex_layout, FlexLineInfo* line);
int calculate_flex_basis(ViewBlock* item, FlexContainerLayout* flex_layout);
void distribute_free_space(FlexLineInfo* line, bool is_growing);

// Alignment functions
void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line);
void align_items_cross_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line);
void align_content(FlexContainerLayout* flex_layout);

// Utility functions
bool is_main_axis_horizontal(FlexProp* flex);
int get_main_axis_size(ViewBlock* item, FlexContainerLayout* flex_layout);
int get_cross_axis_size(ViewBlock* item, FlexContainerLayout* flex_layout);
int get_cross_axis_position(ViewBlock* item, FlexContainerLayout* flex_layout);
void set_main_axis_position(ViewBlock* item, int position, FlexContainerLayout* flex_layout);
void set_cross_axis_position(ViewBlock* item, int position, FlexContainerLayout* flex_layout);
void set_main_axis_size(ViewBlock* item, int size, FlexContainerLayout* flex_layout);
void set_cross_axis_size(ViewBlock* item, int size, FlexContainerLayout* flex_layout);

// Helper functions for constraints and percentages
float clamp_value(float value, float min_val, float max_val);
int resolve_percentage(int value, bool is_percent, int container_size);
void apply_constraints(ViewBlock* item, int container_width, int container_height);
int find_max_baseline(FlexLineInfo* line);
bool is_valid_flex_item(ViewBlock* item);

// Gap handling
int calculate_gap_space(FlexContainerLayout* flex_layout, int item_count, bool is_main_axis);
void apply_gaps(FlexContainerLayout* flex_layout, FlexLineInfo* line);

#endif // LAYOUT_FLEX_HPP
