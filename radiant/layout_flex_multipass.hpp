#pragma once

#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"

// Multi-pass flex layout header
// Defines the enhanced flex layout functions with proper content measurement

// Main multi-pass layout function
void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container);

// Final content layout
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container);
void layout_flex_item_final_content(LayoutContext* lycon, ViewBlock* flex_item);
