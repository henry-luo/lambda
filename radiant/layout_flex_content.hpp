#pragma once

#include "layout.hpp"

// Structure to hold intrinsic size information
typedef struct {
    int min_content;  // Minimum content width (longest word/element)
    int max_content;  // Maximum content width (no wrapping)
} IntrinsicSizes;

// Core flex item content layout functions
void layout_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item);  // Phase 3.1 enhanced version

// Intrinsic size calculation
void calculate_flex_item_intrinsic_sizes(ViewBlock* flex_item);
IntrinsicSizes calculate_child_intrinsic_sizes(View* child);
IntrinsicSizes calculate_block_intrinsic_sizes(ViewBlock* block);
IntrinsicSizes calculate_text_intrinsic_sizes(ViewText* text);
IntrinsicSizes calculate_inline_intrinsic_sizes(View* inline_view);
void apply_intrinsic_size_constraints(ViewBlock* flex_item, IntrinsicSizes* sizes);

// Utility functions
bool is_block_level_child(View* child);
void layout_flex_item_content_for_sizing(LayoutContext* lycon, ViewBlock* flex_item);
void layout_flex_item_final_content(LayoutContext* lycon, ViewBlock* flex_item);
