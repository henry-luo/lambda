#pragma once

#include "layout.hpp"

#define MAX_NESTED_CONTAINERS 50

// Forward declarations (commented out to avoid conflicts with existing types)
// struct ViewBlock;
// struct LayoutContext;

// Structure to represent containing block information
typedef struct {
    int width;
    int height;
    int x;
    int y;
} ContainingBlock;

// Core nested layout functions
void layout_nested_context(LayoutContext* lycon, ViewBlock* container);
void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container);
void layout_block_in_flex_context(LayoutContext* lycon, ViewBlock* block, ViewBlock* flex_parent);

// Constraint and sizing functions
void apply_flex_item_constraints(LayoutContext* lycon, ViewBlock* flex_item, ViewBlock* flex_parent);
void calculate_containing_block(ViewBlock* element, ViewBlock* parent, ContainingBlock* cb);
int resolve_percentage_in_nested_context(int percentage_value, bool is_width, ViewBlock* element, ViewBlock* containing_block);

// Specialized nested scenarios
void layout_nested_flex_containers(LayoutContext* lycon, ViewBlock* outer_flex, ViewBlock* inner_flex);
void layout_complex_nested_scenario(LayoutContext* lycon, ViewBlock* container, ViewBlock* nested_container);

// Validation and optimization
bool validate_nested_layout_structure(ViewBlock* container);
void optimize_nested_layout(LayoutContext* lycon, ViewBlock* container);
void batch_nested_layout_operations(LayoutContext* lycon, ViewBlock* container);
