#include "layout_multicol.hpp"
#include "layout.hpp"
#include "../lib/log.h"
#include <math.h>

// Max number of blocks that can be distributed in multicol layout
#define MAX_MULTICOL_BLOCKS 1024

// min/max macros for int and float
#define MIN_INT(a, b) ((a) < (b) ? (a) : (b))
#define MAX_INT(a, b) ((a) > (b) ? (a) : (b))
#define MIN_FLOAT(a, b) ((a) < (b) ? (a) : (b))
#define MAX_FLOAT(a, b) ((a) > (b) ? (a) : (b))

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
        column_count = MIN_INT(column_count, MAX_INT(1, max_by_width));
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
        column_count = MAX_INT(1, column_count);
        // Recalculate width to fill available space
        column_width = (available_width - (column_count - 1) * gap) / column_count;
    }
    else {
        // Neither specified: single column
        column_count = 1;
        column_width = available_width;
    }

    // Ensure at least 1 column
    column_count = MAX_INT(1, column_count);
    column_width = MAX_FLOAT(0.0f, column_width);

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
    // Phase 2: Collect block children and identify column groups
    // =========================================================================
    // CSS Multicol §7.1: Spanners divide content into "column groups".
    // Each column group is balanced independently.

    struct BlockInfo {
        ViewBlock* block;
        float height;       // Total height including margins
        float orig_y;       // Original Y position
        bool spans_all;     // column-span: all
    };
    BlockInfo blocks[MAX_MULTICOL_BLOCKS];
    int block_count = 0;

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

                if (block_count < MAX_MULTICOL_BLOCKS) {
                    blocks[block_count].block = child_block;
                    blocks[block_count].height = block_height;
                    blocks[block_count].orig_y = child_block->y;
                    blocks[block_count].spans_all = spans_all;
                    block_count++;
                }

                log_debug("[MULTICOL] Block %s: height=%.1f, y=%.1f, spans_all=%d",
                          child_block->node_name(), block_height, child_block->y, spans_all);
            }
        }
        child = child->next_sibling;
    }

    if (block_count == 0) {
        log_debug("[MULTICOL] No block children to distribute");
        return;
    }

    // =========================================================================
    // Phase 3: Assign blocks to columns, balancing each column group
    // =========================================================================
    // Process blocks in groups separated by spanners. For each group of
    // non-spanner blocks, compute a balanced height and distribute across
    // columns. Spanners are placed at full container width between groups.

    // CSS Box 4 §3.1: margin-trim:block-end — trim the last in-flow child's
    // block-end margin. We handle this here since layout_block_inner_content
    // returns early for multicol containers.
    bool trim_block_end = block->blk && (block->blk->margin_trim & MARGIN_TRIM_BLOCK_END);
    if (trim_block_end && block_count > 0) {
        ViewBlock* last_block = blocks[block_count - 1].block;
        if (last_block->bound && last_block->bound->margin.bottom != 0) {
            log_debug("[MULTICOL] margin-trim block-end: trimming margin.bottom=%.1f on last child %s",
                      last_block->bound->margin.bottom, last_block->node_name());
            float old_mb = last_block->bound->margin.bottom;
            last_block->bound->margin.bottom = 0;
            // Update the cached height in blocks array
            blocks[block_count - 1].height -= old_mb;
        }
    }

    float max_column_height = 0;  // running Y offset for the entire container
    float prev_margin_bottom = 0; // for margin collapsing between consecutive spanners

    int i = 0;
    while (i < block_count) {
        // --- Spanner: place at full width ---
        if (blocks[i].spans_all) {
            ViewBlock* child_block = blocks[i].block;
            float spanner_margin_top = child_block->bound ? child_block->bound->margin.top : 0;
            float spanner_margin_bottom = child_block->bound ? child_block->bound->margin.bottom : 0;

            // CSS 2.1 §8.3.1: Collapse margin between previous element and
            // this spanner. Use max of the two positive margins (simplified —
            // negative margin handling omitted for now).
            float collapsed_margin = MAX_FLOAT(prev_margin_bottom, spanner_margin_top);
            // Subtract the already-accounted prev_margin_bottom from max_column_height
            max_column_height -= prev_margin_bottom;
            max_column_height += collapsed_margin;

            child_block->x = 0;
            child_block->y = max_column_height;
            child_block->width = available_width;

            max_column_height += child_block->height + spanner_margin_bottom;
            prev_margin_bottom = spanner_margin_bottom;

            log_debug("[MULTICOL] Spanner %s at y=%.1f, margin_top=%.1f, margin_bottom=%.1f, collapsed=%.1f",
                      child_block->node_name(), child_block->y, spanner_margin_top, spanner_margin_bottom, collapsed_margin);
            i++;
            continue;
        }

        // --- Column group: collect consecutive non-spanner blocks ---
        int group_start = i;
        float group_total_height = 0;
        while (i < block_count && !blocks[i].spans_all) {
            group_total_height += blocks[i].height;
            i++;
        }
        int group_end = i;  // exclusive

        // Calculate balanced height for this column group
        float group_balanced = group_total_height / column_count;
        // CSS Multicol §7.2: column-fill:balance distributes content evenly.
        // Use ceiling to avoid underfilling the last column.
        group_balanced = ceilf(group_balanced);

        log_debug("[MULTICOL] Column group [%d..%d): total_h=%.1f, balanced_h=%.1f",
                  group_start, group_end, group_total_height, group_balanced);

        // Distribute this group's blocks across columns
        int current_column = 0;
        float column_y = 0;
        float group_max_height = 0;

        for (int j = group_start; j < group_end; j++) {
            BlockInfo& info = blocks[j];
            ViewBlock* cb = info.block;

            // Check if we should break to next column
            if (block->multicol->fill == COLUMN_FILL_BALANCE && column_y > 0 &&
                column_y + info.height > group_balanced &&
                current_column < column_count - 1) {
                group_max_height = MAX_FLOAT(group_max_height, column_y);
                current_column++;
                column_y = 0;
                log_debug("[MULTICOL] Column break -> column %d at y=%.1f", current_column, column_y);
            }

            float column_x = current_column * (column_width + gap);
            cb->x = column_x;
            cb->y = max_column_height + column_y;

            // Ensure width fits column
            if (cb->width > column_width) {
                cb->width = column_width;
            }

            log_debug("[MULTICOL] Placed %s in column %d at (%.1f, %.1f)",
                      cb->node_name(), current_column, cb->x, cb->y);

            column_y += info.height;
        }
        group_max_height = MAX_FLOAT(group_max_height, column_y);
        max_column_height += group_max_height;
        prev_margin_bottom = 0;  // column group doesn't have trailing margin
    }

    // Calculate total height including padding
    float content_start_y = 0;
    if (block->bound) {
        if (block->bound->border) {
            content_start_y += block->bound->border->width.top;
        }
        content_start_y += block->bound->padding.top;
    }

    // Set block height directly (like flex layout does)
    float total_height = max_column_height;
    if (block->bound) {
        total_height += block->bound->padding.top + block->bound->padding.bottom;
        if (block->bound->border) {
            total_height += block->bound->border->width.top + block->bound->border->width.bottom;
        }
    }
    block->height = total_height;
    block->content_height = max_column_height + (block->bound ? block->bound->padding.bottom : 0);

    // Update layout context's advance_y to reflect actual height
    lycon->block.advance_y = content_start_y + max_column_height;

    log_debug("[MULTICOL] Final layout: %d columns, max height=%.1f, block height=%.1f",
              column_count, max_column_height, block->height);
}
