#pragma once

#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"

// Multi-pass flex layout header
// Defines the enhanced flex layout functions with proper content measurement

// Main multi-pass layout function
void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container);

// Enhanced flex algorithm functions
void run_flex_algorithm_with_measurements(LayoutContext* lycon, ViewBlock* flex_container);
void collect_flex_items_with_measurements(FlexContainerLayout* flex_layout, ViewBlock* container);
void calculate_flex_basis_with_measurements(FlexContainerLayout* flex_layout);

// Enhanced flexible length resolution
void resolve_flexible_lengths_with_measurements(FlexContainerLayout* flex_layout, FlexLineInfo* line);

// Enhanced alignment functions
void align_items_main_axis_enhanced(FlexContainerLayout* flex_layout, FlexLineInfo* line);
bool has_main_axis_auto_margins(FlexLineInfo* line);
void handle_main_axis_auto_margins(FlexContainerLayout* flex_layout, FlexLineInfo* line);

// Final content layout
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container);
void layout_flex_item_final_content(LayoutContext* lycon, ViewBlock* flex_item);
