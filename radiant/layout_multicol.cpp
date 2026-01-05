#include "layout_multicol.hpp"
#include "layout.hpp"
#include "../lib/log.h"
#include <math.h>
#include <algorithm>
#include <vector>

/**
 * CSS Multi-column Layout Implementation
 *
 * Multi-column layout creates a multi-column formatting context where content
 * flows from one column to the next. This implementation:
 *
 * 1. Creates pseudo-column boxes within the container
 * 2. Distributes block content across columns with balanced heights
 * 3. Handles column-span: all elements that span all columns
 * 4. Stores computed values for column rule rendering
 *
 * Limitations:
 * - Column breaks are at block boundaries only (no mid-paragraph breaks)
 * - Fragmentation properties (break-before/after) not yet implemented
 * - Column-fill: auto not fully implemented (requires height constraint)
 */

/**
 * Check if a block establishes a multi-column container
 */
bool is_multicol_container(ViewBlock* block) {
    if (!block->multicol) return false;

    // Container has columns if column-count > 1 or column-width > 0
    return block->multicol->column_count > 1 || block->multicol->column_width > 0;
}

/**
 * Calculate actual column dimensions based on CSS Multi-column spec
 */
void calculate_multicol_dimensions(
    MultiColumnProp* multicol,
    float available_width,
    int* out_column_count,
    float* out_column_width,
    float* out_gap
) {
    // Default gap is 1em (using 16px as typical default)
    float gap = multicol->column_gap_is_normal ? 16.0f : multicol->column_gap;
    if (gap < 0) gap = 0;

    int column_count = multicol->column_count;  // 0 = auto
    float column_width = multicol->column_width; // 0 = auto

    log_debug("[MULTICOL] Input: count=%d, width=%.1f, gap=%.1f, available=%.1f",
              column_count, column_width, gap, available_width);

    // CSS Multi-column §3.4: Pseudo-algorithm for column layout
    if (column_count > 0 && column_width > 0) {
        // Both specified: use min of count and what fits
        int max_by_width = (int)floorf((available_width + gap) / (column_width + gap));
        column_count = std::min(column_count, std::max(1, max_by_width));
        // Recalculate width to fill available space
        column_width = (available_width - (column_count - 1) * gap) / column_count;
    }
    else if (column_count > 0) {
        // Only count specified: divide width evenly
        column_width = (available_width - (column_count - 1) * gap) / column_count;
    }
    else if (column_width > 0) {
        // Only width specified: fit as many as possible
        column_count = (int)floorf((available_width + gap) / (column_width + gap));
        column_count = std::max(1, column_count);
        // Recalculate width to fill available space
        column_width = (available_width - (column_count - 1) * gap) / column_count;
    }
    else {
        // Neither specified: single column
        column_count = 1;
        column_width = available_width;
    }

    // Ensure at least 1 column
    column_count = std::max(1, column_count);
    column_width = std::max(0.0f, column_width);

    log_debug("[MULTICOL] Computed: count=%d, width=%.1f, gap=%.1f",
              column_count, column_width, gap);

    *out_column_count = column_count;
    *out_column_width = column_width;
    *out_gap = gap;
}

/**
 * Structure to track column state during layout
 */
struct ColumnState {
    int column_index;           // Current column (0-based)
    float column_top;           // Y position at column start
    float column_height;        // Height used in current column
    float max_column_height;    // Maximum height across all columns (for balancing)
    float balanced_height;      // Target height for balanced columns
    bool balancing;             // True if balancing pass
};

// Forward declarations for layout functions
void layout_flow_node(LayoutContext* lycon, DomNode* node);
void line_break(LayoutContext* lycon);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum outer_display);
void prescan_and_layout_floats(LayoutContext* lycon, DomNode* first_child, ViewBlock* block);

/**
 * Layout multi-column content
 *
 * Multi-column layout works by:
 * 1. Setting up a narrow column width in the layout context
 * 2. Running normal flow layout within that width
 * 3. Measuring total content height
 * 4. Repositioning blocks to distribute across columns
 *
 * This is a simplified implementation that:
 * - Uses block-level distribution (breaks between block elements)
 * - Supports column-fill: balance (default) which tries to equalize column heights
 * - Doesn't yet support fragmentation within block elements
 */
void layout_multicol_content(LayoutContext* lycon, ViewBlock* block) {
    if (!block->multicol) {
        log_error("[MULTICOL] layout_multicol_content called without multicol prop");
        return;
    }

    log_debug("[MULTICOL] Starting layout for %s", block->node_name());

    // Calculate available width (content box)
    float available_width = lycon->block.content_width;

    // Calculate column dimensions
    int column_count;
    float column_width, gap;
    calculate_multicol_dimensions(block->multicol, available_width,
                                   &column_count, &column_width, &gap);

    // Store computed values for rendering
    block->multicol->computed_column_count = column_count;
    block->multicol->computed_column_width = column_width;

    // If only 1 column, fall back to normal flow
    if (column_count <= 1) {
        log_debug("[MULTICOL] Single column, falling back to normal flow");
        block->multicol->computed_column_count = 1;

        // Run normal flow layout
        DomNode* child = block->first_child;
        if (child) {
            prescan_and_layout_floats(lycon, child, block);
            do {
                layout_flow_node(lycon, child);
                child = child->next_sibling;
            } while (child);
            if (!lycon->line.is_line_start) {
                line_break(lycon);
            }
        }
        return;
    }

    log_debug("[MULTICOL] Layout with %d columns, width=%.1f, gap=%.1f",
              column_count, column_width, gap);

    // =========================================================================
    // Phase 1: Layout all content within single column width
    // =========================================================================

    // Save original line bounds
    float orig_line_left = lycon->line.left;
    float orig_line_right = lycon->line.right;
    float orig_content_width = lycon->block.content_width;

    // Constrain layout to column width
    lycon->block.content_width = column_width;
    lycon->line.left = 0;
    lycon->line.right = column_width;

    // Layout children normally within column width
    DomNode* child = block->first_child;
    if (child) {
        prescan_and_layout_floats(lycon, child, block);
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling;
        } while (child);
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }

    // Get total content height after layout
    float total_content_height = lycon->block.advance_y;
    log_debug("[MULTICOL] Total content height after layout: %.1f", total_content_height);

    // Restore original widths (for container sizing)
    lycon->line.left = orig_line_left;
    lycon->line.right = orig_line_right;
    lycon->block.content_width = orig_content_width;

    // If content fits in one column, no redistribution needed
    if (total_content_height <= 0) {
        log_debug("[MULTICOL] No content to distribute");
        return;
    }

    // =========================================================================
    // Phase 2: Calculate balanced height and redistribute blocks
    // =========================================================================

    // Calculate target height for balanced columns
    float balanced_height = total_content_height / column_count;
    // Add some slack to handle uneven distribution
    balanced_height = ceilf(balanced_height * 1.05f);

    log_debug("[MULTICOL] Balanced height target: %.1f", balanced_height);

    // Collect block children and their heights
    struct BlockInfo {
        ViewBlock* block;
        float height;       // Total height including margins
        float orig_y;       // Original Y position
        bool spans_all;     // column-span: all
    };
    std::vector<BlockInfo> blocks;

    child = block->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* child_elem = (DomElement*)child;
            ViewBlock* child_block = (ViewBlock*)child_elem;

            if (child_block->view_type == RDT_VIEW_BLOCK ||
                child_block->view_type == RDT_VIEW_INLINE_BLOCK ||
                child_block->view_type == RDT_VIEW_TEXT) {

                float block_height = child_block->height;
                if (child_block->bound) {
                    block_height += child_block->bound->margin.top +
                                    child_block->bound->margin.bottom;
                }

                bool spans_all = child_elem->multicol &&
                                 child_elem->multicol->span == COLUMN_SPAN_ALL;

                blocks.push_back({child_block, block_height, child_block->y, spans_all});

                log_debug("[MULTICOL] Block %s: height=%.1f, y=%.1f, spans_all=%d",
                          child_block->node_name(), block_height, child_block->y, spans_all);
            }
        }
        child = child->next_sibling;
    }

    if (blocks.empty()) {
        log_debug("[MULTICOL] No block children to distribute");
        return;
    }

    // =========================================================================
    // Phase 3: Assign blocks to columns
    // =========================================================================

    int current_column = 0;
    float column_y = 0;
    float max_column_height = 0;

    for (size_t i = 0; i < blocks.size(); i++) {
        BlockInfo& info = blocks[i];
        ViewBlock* child_block = info.block;

        // Handle column-span: all
        if (info.spans_all) {
            // Spanning element spans all columns
            max_column_height = std::max(max_column_height, column_y);

            child_block->x = 0;
            child_block->y = max_column_height;
            child_block->width = available_width;  // Full width

            max_column_height += info.height;
            current_column = 0;
            column_y = max_column_height;

            log_debug("[MULTICOL] Spanning element %s at y=%.1f, full width",
                      child_block->node_name(), child_block->y);
            continue;
        }

        // Check if we should break to next column
        bool should_break = false;
        if (block->multicol->fill == COLUMN_FILL_BALANCE) {
            // Break when exceeding balanced height (but not on first block in column)
            should_break = (column_y > 0) &&
                           (column_y + info.height > balanced_height) &&
                           (current_column < column_count - 1);
        }

        if (should_break) {
            log_debug("[MULTICOL] Column break: column %d -> %d at y=%.1f",
                      current_column, current_column + 1, column_y);
            max_column_height = std::max(max_column_height, column_y);
            current_column++;
            column_y = 0;
        }

        // Position block in current column
        float column_x = current_column * (column_width + gap);
        child_block->x = column_x;
        child_block->y = column_y;

        // Ensure width fits column
        if (child_block->width > column_width) {
            child_block->width = column_width;
        }

        log_debug("[MULTICOL] Placed %s in column %d at (%.1f, %.1f)",
                  child_block->node_name(), current_column, child_block->x, child_block->y);

        column_y += info.height;
    }

    // Final column height
    max_column_height = std::max(max_column_height, column_y);

    // Update layout context's advance_y to reflect actual height
    lycon->block.advance_y = max_column_height;

    log_debug("[MULTICOL] Final layout: %d columns, max height=%.1f",
              column_count, max_column_height);
}
