#ifndef FLEX_LAYOUT_NEW_HPP
#define FLEX_LAYOUT_NEW_HPP

#include "flex.hpp"
#include "view.hpp"
#include "layout.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Flex line information for layout calculations
typedef struct FlexLineInfo {
    struct ViewBlock** items;  // Items in this line
    int item_count;
    int main_size;            // Total size along main axis
    int cross_size;           // Size along cross axis (height of tallest item)
    int free_space;           // Available space for distribution
    float total_flex_grow;    // Sum of flex-grow values
    float total_flex_shrink;  // Sum of flex-shrink values
    int baseline;             // Baseline for alignment
} FlexLineInfo;

// Main flex layout functions
extern void init_flex_container(struct ViewBlock* container);
extern void cleanup_flex_container(struct ViewBlock* container);
extern void layout_flex_container_new(LayoutContext* lycon, struct ViewBlock* container);

// Flex item collection and management
extern int collect_flex_items(struct ViewBlock* container, struct ViewBlock*** items);
extern void sort_flex_items_by_order(struct ViewBlock** items, int count);

// Flex line creation
extern int create_flex_lines(FlexContainerLayout* flex_layout, struct ViewBlock** items, int item_count);
extern void calculate_line_cross_sizes(FlexContainerLayout* flex_layout);

// Flexible length resolution
extern void resolve_flexible_lengths(FlexContainerLayout* flex_layout, FlexLineInfo* line);
extern int calculate_flex_basis(struct ViewBlock* item, FlexContainerLayout* flex_layout);
extern void distribute_free_space(FlexLineInfo* line, bool is_growing);

// Alignment functions
extern void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line);
extern void align_items_cross_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line);
extern void align_content(FlexContainerLayout* flex_layout);

// Utility functions
extern bool is_main_axis_horizontal(FlexContainerLayout* flex_layout);
extern int get_main_axis_size(struct ViewBlock* item, FlexContainerLayout* flex_layout);
extern int get_cross_axis_size(struct ViewBlock* item, FlexContainerLayout* flex_layout);
extern int get_cross_axis_position(struct ViewBlock* item, FlexContainerLayout* flex_layout);
extern void set_main_axis_position(struct ViewBlock* item, int position, FlexContainerLayout* flex_layout);
extern void set_cross_axis_position(struct ViewBlock* item, int position, FlexContainerLayout* flex_layout);
extern void set_main_axis_size(struct ViewBlock* item, int size, FlexContainerLayout* flex_layout);
extern void set_cross_axis_size(struct ViewBlock* item, int size, FlexContainerLayout* flex_layout);

// Helper functions for constraints and percentages
extern float clamp_value(float value, float min_val, float max_val);
extern int resolve_percentage(int value, bool is_percent, int container_size);
extern void apply_constraints(struct ViewBlock* item, int container_width, int container_height);
extern int find_max_baseline(FlexLineInfo* line);
extern bool is_valid_flex_item(struct ViewBlock* item);

// Gap handling
extern int calculate_gap_space(FlexContainerLayout* flex_layout, int item_count, bool is_main_axis);
extern void apply_gaps(FlexContainerLayout* flex_layout, FlexLineInfo* line);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_FLEX_LAYOUT_NEW_HPP
