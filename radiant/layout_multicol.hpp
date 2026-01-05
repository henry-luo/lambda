#pragma once
#include "layout.hpp"
#include "view.hpp"

/**
 * CSS Multi-column Layout Implementation
 *
 * Implements CSS Multi-column Layout Module Level 1:
 * - column-count: explicit number of columns
 * - column-width: ideal column width
 * - column-gap: space between columns
 * - column-rule: border between columns
 * - column-span: elements that span all columns
 * - column-fill: balance vs auto fill
 *
 * Algorithm Overview:
 * 1. Calculate actual column count and width based on container width
 * 2. Layout content into first column
 * 3. When content height exceeds balanced height (or column height), break to next column
 * 4. Position columns side-by-side
 * 5. Render column rules between columns
 *
 * References:
 * - CSS Multi-column Layout Module Level 1: https://www.w3.org/TR/css-multicol-1/
 */

/**
 * Check if block establishes a multi-column container
 */
bool is_multicol_container(ViewBlock* block);

/**
 * Calculate the actual number of columns and their widths
 *
 * Per CSS Multi-column spec:
 * - If column-width and column-count are both non-auto:
 *   N = min(column-count, floor((available-width + gap) / (column-width + gap)))
 * - If only column-count is specified: divide width evenly
 * - If only column-width is specified: N = floor((available-width + gap) / (column-width + gap))
 *
 * @param block The multi-column container
 * @param available_width Available width for columns
 * @param out_column_count Output: computed number of columns
 * @param out_column_width Output: computed column width
 * @param out_gap Output: computed gap between columns
 */
void calculate_multicol_dimensions(
    MultiColumnProp* multicol,
    float available_width,
    int* out_column_count,
    float* out_column_width,
    float* out_gap
);

/**
 * Layout a multi-column container
 *
 * Creates pseudo-column boxes and distributes content across them.
 * Content flow follows CSS column-break rules.
 *
 * @param lycon Layout context
 * @param block The multi-column container block
 */
void layout_multicol_content(LayoutContext* lycon, ViewBlock* block);

/**
 * Render column rules between columns
 *
 * @param rdcon Render context
 * @param block The multi-column container
 */
void render_column_rules(struct RenderContext* rdcon, ViewBlock* block);
