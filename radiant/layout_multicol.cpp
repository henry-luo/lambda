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
    return block->multicol->column_count > 1 ||
           block->multicol->column_width > 0 ||
           block->multicol->column_height > 0;
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
        int max_by_width = (int)floorf((available_width + gap) / (column_width + gap)); // INT_CAST_OK: integer column count
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
        column_count = (int)floorf((available_width + gap) / (column_width + gap)); // INT_CAST_OK: integer column count
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

struct MulticolFragment {
    int column_index;
    float x;
    float y;
    float width;
    float target_height;
    float used_height;
};

struct MulticolGroupPlacement {
    MulticolFragment fragments[MAX_MULTICOL_BLOCKS];
    int fragment_count;
    int current_column;
    float row_y;
    float column_y;
    float group_max_height;
    float target_height;
};

// Forward declarations for layout functions
void layout_flow_node(LayoutContext* lycon, DomNode* node);
void line_break(LayoutContext* lycon);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum outer_display);
void prescan_and_layout_floats(LayoutContext* lycon, DomNode* first_child, ViewBlock* block);

static float multicol_content_box_height_limit(ViewBlock* block) {
    if (!block || !block->blk) return -1;

    float limit = -1;
    if (block->multicol && block->multicol->column_height > 0) {
        limit = block->multicol->column_height;
    } else if (block->blk->given_height >= 0) {
        limit = block->blk->given_height;
    } else if (block->blk->given_max_height >= 0) {
        limit = block->blk->given_max_height;
    }

    if (limit < 0) return -1;

    if (block->bound && block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
        float border_padding = block->bound->padding.top + block->bound->padding.bottom;
        if (block->bound->border) {
            border_padding += block->bound->border->width.top + block->bound->border->width.bottom;
        }
        limit -= border_padding;
        if (limit < 0) limit = 0;
    }
    return limit;
}

static float multicol_row_gap(ViewBlock* block) {
    if (!block || !block->embed) return 0;
    if (block->embed->grid) return block->embed->grid->row_gap;
    if (block->embed->flex) return block->embed->flex->row_gap;
    return 0;
}

static bool multicol_is_out_of_flow(ViewBlock* block) {
    return block && block->position &&
           (block->position->position == CSS_VALUE_ABSOLUTE ||
            block->position->position == CSS_VALUE_FIXED);
}

static bool multicol_should_fragment_monolithic_child(
    ViewBlock* container,
    ViewBlock* child,
    float item_height,
    float fragment_height
) {
    if (!container || !container->multicol || !child) return false;
    bool has_fragmentainer_height =
        container->multicol->column_height > 0 ||
        (container->multicol->wrap == COLUMN_WRAP_WRAP &&
         multicol_content_box_height_limit(container) > 0);
    if (!has_fragmentainer_height) return false;
    if (fragment_height <= 0) return false;
    if (container->multicol->wrap == COLUMN_WRAP_NOWRAP) return false;
    if (container->multicol->wrap == COLUMN_WRAP_AUTO &&
        container->blk && container->blk->given_height >= 0) {
        return false;
    }
    return item_height > fragment_height;
}

static float multicol_fragmented_child_union(
    ViewBlock* container,
    ViewBlock* child,
    float item_height,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap,
    int* out_used_columns
) {
    float row_gap = multicol_row_gap(container);
    if (row_gap < 0) row_gap = 0;

    int fragment_count = (int)ceilf(item_height / fragment_height); // INT_CAST_OK: fragment count from positive heights
    if (fragment_count < 1) fragment_count = 1;
    int used_columns = MIN_INT(column_count, fragment_count);
    int row_count = (fragment_count + column_count - 1) / column_count;
    if (row_count < 1) row_count = 1;

    float union_width = used_columns * column_width + (used_columns - 1) * column_gap;
    float union_height = row_count * fragment_height + (row_count - 1) * row_gap;

    if (child->width < union_width) child->width = union_width;
    child->height = union_height;
    child->content_height = union_height;
    if (out_used_columns) *out_used_columns = used_columns;
    return union_height;
}

static float multicol_group_target_height(ViewBlock* block, float balanced_height, float group_total_height) {
    if (!block || !block->multicol) return balanced_height;

    float limit = multicol_content_box_height_limit(block);
    if (block->multicol->fill == COLUMN_FILL_AUTO) {
        if (limit >= 0) return limit;
        return group_total_height;
    }
    if (block->multicol->wrap == COLUMN_WRAP_WRAP && limit >= 0) {
        return limit;
    }
    return balanced_height;
}

static void multicol_group_init(
    MulticolGroupPlacement* group,
    float target_height,
    float column_width,
    float gap
) {
    group->fragment_count = 1;
    group->current_column = 0;
    group->row_y = 0;
    group->column_y = 0;
    group->group_max_height = 0;
    group->target_height = target_height;

    group->fragments[0].column_index = 0;
    group->fragments[0].x = 0;
    group->fragments[0].y = 0;
    group->fragments[0].width = column_width;
    group->fragments[0].target_height = target_height;
    group->fragments[0].used_height = 0;

    (void)gap;
}

static bool multicol_group_wraps_rows(ViewBlock* container) {
    if (!container || !container->multicol) return false;
    float fragment_height = multicol_content_box_height_limit(container);
    if (fragment_height <= 0) return false;
    if (container->multicol->wrap == COLUMN_WRAP_WRAP) return true;
    if (container->multicol->wrap == COLUMN_WRAP_AUTO &&
        (!container->blk || container->blk->given_height < 0)) {
        return true;
    }
    return false;
}

static bool multicol_group_should_break(
    ViewBlock* container,
    MulticolGroupPlacement* group,
    float item_height,
    int column_count
) {
    if (!container || !container->multicol || column_count <= 1) return false;
    if (group->column_y <= 0) return false;
    if (group->current_column >= column_count - 1 && !multicol_group_wraps_rows(container)) return false;

    if (container->multicol->fill == COLUMN_FILL_AUTO) {
        if (group->target_height < 0) return false;
        return group->column_y + item_height > group->target_height;
    }

    return group->column_y + item_height > group->target_height;
}

static void multicol_group_advance_column(
    ViewBlock* container,
    MulticolGroupPlacement* group,
    float column_width,
    float gap,
    int column_count
) {
    float row_gap = multicol_row_gap(container);
    if (row_gap < 0) row_gap = 0;

    if (group->column_y > group->group_max_height) {
        group->group_max_height = group->row_y + group->column_y;
    }
    if (group->fragment_count > 0) {
        group->fragments[group->fragment_count - 1].used_height = group->column_y;
    }

    if (group->current_column >= column_count - 1 && multicol_group_wraps_rows(container)) {
        group->current_column = 0;
        group->row_y += group->target_height + row_gap;
    } else {
        group->current_column++;
    }
    group->column_y = 0;

    if (group->fragment_count < MAX_MULTICOL_BLOCKS) {
        MulticolFragment* fragment = &group->fragments[group->fragment_count++];
        fragment->column_index = group->current_column;
        fragment->x = group->current_column * (column_width + gap);
        fragment->y = group->row_y;
        fragment->width = column_width;
        fragment->target_height = group->target_height;
        fragment->used_height = 0;
    }
}

static void multicol_group_finish(MulticolGroupPlacement* group) {
    if (group->row_y + group->column_y > group->group_max_height) {
        group->group_max_height = group->row_y + group->column_y;
    }
    if (group->fragment_count > 0) {
        group->fragments[group->fragment_count - 1].used_height = group->column_y;
    }
}

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
    block->multicol->computed_used_column_count = 1;

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

                if (multicol_is_out_of_flow(child_block)) {
                    log_debug("[MULTICOL] Skipping out-of-flow child %s in column distribution",
                              child_block->node_name());
                    child = child->next_sibling;
                    continue;
                }

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
        // No block children — content is inline-only (text lines).
        // Redistribute TextRects across columns based on balanced height.
        log_debug("[MULTICOL] No block children; redistributing inline text across columns");

        // Collect all TextRects from all text children
        struct LineRect {
            TextRect* rect;
            DomText* text_node;  // owning text node
            float line_y;        // original y
            float line_height;   // height of this rect
        };
        LineRect lines[512];
        int line_count = 0;

        child = block->first_child;
        while (child) {
            if (child->node_type == DOM_NODE_TEXT) {
                DomText* tnode = (DomText*)child;
                TextRect* tr = tnode->rect;
                while (tr && line_count < 512) {
                    lines[line_count].rect = tr;
                    lines[line_count].text_node = tnode;
                    lines[line_count].line_y = tr->y;
                    lines[line_count].line_height = tr->height;
                    line_count++;
                    tr = tr->next;
                }
            }
            child = child->next_sibling;
        }

        if (line_count == 0) {
            log_debug("[MULTICOL] No text rects to distribute");
            return;
        }

        // Calculate fragmentainer height for this inline-only column group.
        float balanced_height = ceilf(total_content_height / column_count);
        float target_height = multicol_group_target_height(block, balanced_height, total_content_height);

        log_debug("[MULTICOL] Inline redistribution: %d rects, total_h=%.1f, target_h=%.1f",
                  line_count, total_content_height, target_height);

        // Distribute rects across columns
        int current_col = 0;
        float col_y = 0;           // y offset within current column
        float col_start_y = 0;     // the original y of the first line assigned to the current column
        bool col_started = false;
        float max_col_height = 0;

        for (int li = 0; li < line_count; li++) {
            LineRect& lr = lines[li];

            // Relative y within original single-column layout
            float rel_y = lr.line_y - lines[0].line_y;

            // Check if this line should go to the next column.
            // Break only when including this line would overshoot AND
            // excluding it is closer to balanced than including it.
            // This matches browser behavior of preferring more content
            // in earlier columns when lines are indivisible.
            if (col_started && current_col < column_count - 1) {
                float col_h_with = rel_y - col_start_y + lr.line_height;
                if (col_h_with > target_height) {
                    float col_h_without = rel_y - col_start_y;
                    float overshoot = col_h_with - target_height;
                    float undershoot = target_height - col_h_without;
                    bool should_break = block->multicol->fill == COLUMN_FILL_AUTO ||
                                        undershoot <= overshoot;
                    if (should_break) {
                        // closer to balanced without this line — break here
                        if (col_h_without > max_col_height) max_col_height = col_h_without;
                        current_col++;
                        col_start_y = rel_y;
                        log_debug("[MULTICOL] Inline column break -> column %d at rel_y=%.1f", current_col, rel_y);
                    }
                }
            }
            col_started = true;

            // Reposition: shift x by column offset, reset y within column
            float col_x_offset = current_col * (column_width + gap);
            lr.rect->x += col_x_offset;
            lr.rect->y = lines[0].line_y + (rel_y - col_start_y);

            col_y = (rel_y - col_start_y) + lr.line_height;
        }
        if (col_y > max_col_height) max_col_height = col_y;

        // Update block height to the max column height (not total content height)
        float final_height = max_col_height;

        // Update text node bounds (x, y, width, height)
        child = block->first_child;
        while (child) {
            if (child->node_type == DOM_NODE_TEXT) {
                DomText* tnode = (DomText*)child;
                // Recalculate text node bounding box from its rects
                float min_x = 1e9f, min_y = 1e9f, max_x = 0, max_y_val = 0;
                TextRect* tr = tnode->rect;
                while (tr) {
                    if (tr->x < min_x) min_x = tr->x;
                    if (tr->y < min_y) min_y = tr->y;
                    float rx = tr->x + tr->width;
                    float ry = tr->y + tr->height;
                    if (rx > max_x) max_x = rx;
                    if (ry > max_y_val) max_y_val = ry;
                    tr = tr->next;
                }
                // DomText doesn't have x/y/width/height directly, but
                // the parent block uses content_height from advance_y
            }
            child = child->next_sibling;
        }

        // Set block height: use CSS given height if specified, otherwise balanced column height
        float total_height = final_height;
        if (block->blk && block->blk->given_height >= 0) {
            total_height = block->blk->given_height;
        } else if (block->multicol->fill == COLUMN_FILL_AUTO &&
                   multicol_content_box_height_limit(block) >= 0 &&
                   final_height > multicol_content_box_height_limit(block)) {
            total_height = multicol_content_box_height_limit(block);
        } else if (block->bound) {
            total_height += block->bound->padding.top + block->bound->padding.bottom;
            if (block->bound->border) {
                total_height += block->bound->border->width.top + block->bound->border->width.bottom;
            }
        }
        block->height = total_height;
        block->content_height = final_height + (block->bound ? block->bound->padding.bottom : 0);
        block->multicol->computed_used_column_count = current_col + 1;

        float content_start_y = 0;
        if (block->bound) {
            if (block->bound->border) content_start_y += block->bound->border->width.top;
            content_start_y += block->bound->padding.top;
        }
        lycon->block.advance_y = content_start_y + final_height;

        log_debug("[MULTICOL] Inline redistribution complete: %d columns, max_col_h=%.1f, block_h=%.1f",
                  column_count, max_col_height, block->height);
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

        // Calculate target fragmentainer height for this column group
        float group_balanced = group_total_height / column_count;
        // CSS Multicol §7.2: column-fill:balance distributes content evenly.
        // Use ceiling to avoid underfilling the last column.
        group_balanced = ceilf(group_balanced);
        float group_target = multicol_group_target_height(block, group_balanced, group_total_height);

        log_debug("[MULTICOL] Column group [%d..%d): total_h=%.1f, target_h=%.1f",
                  group_start, group_end, group_total_height, group_target);

        // Distribute this group's blocks across columns
        MulticolGroupPlacement group;
        multicol_group_init(&group, group_target, column_width, gap);

        for (int j = group_start; j < group_end; j++) {
            BlockInfo& info = blocks[j];
            ViewBlock* cb = info.block;
            float placed_height = info.height;

            // Check if we should break to next column
            if (multicol_group_should_break(block, &group, info.height, column_count)) {
                multicol_group_advance_column(block, &group, column_width, gap, column_count);
                log_debug("[MULTICOL] Column break -> column %d at y=%.1f",
                          group.current_column, group.row_y);
            }

            float column_x = group.current_column * (column_width + gap);
            cb->x = column_x;
            cb->y = max_column_height + group.row_y + group.column_y;

            if (multicol_should_fragment_monolithic_child(block, cb, info.height, group_target)) {
                int used_columns = 1;
                placed_height = multicol_fragmented_child_union(
                    block, cb, info.height, group_target, column_count, column_width, gap, &used_columns);
                if (used_columns > group.fragment_count) {
                    group.fragment_count = used_columns;
                }
                log_debug("[MULTICOL] Fragmented monolithic %s into %d columns, union height=%.1f",
                          cb->node_name(), used_columns, placed_height);
            }

            log_debug("[MULTICOL] Placed %s in column %d at (%.1f, %.1f)",
                      cb->node_name(), group.current_column, cb->x, cb->y);

            group.column_y += placed_height;
        }
        multicol_group_finish(&group);
        int used_column_count = 1;
        for (int fi = 0; fi < group.fragment_count; fi++) {
            int candidate = group.fragments[fi].column_index + 1;
            if (candidate > used_column_count) used_column_count = candidate;
        }
        if (used_column_count > block->multicol->computed_used_column_count) {
            block->multicol->computed_used_column_count = used_column_count;
        }
        max_column_height += group.group_max_height;
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

    // Set block height: use CSS given height if specified, otherwise computed
    float total_height = max_column_height;
    if (block->blk && block->blk->given_height >= 0) {
        total_height = block->blk->given_height;
    } else {
        if (block->bound) {
            total_height += block->bound->padding.top + block->bound->padding.bottom;
            if (block->bound->border) {
                total_height += block->bound->border->width.top + block->bound->border->width.bottom;
            }
        }
    }
    block->height = total_height;
    block->content_height = max_column_height + (block->bound ? block->bound->padding.bottom : 0);

    // Update layout context's advance_y to reflect actual height
    lycon->block.advance_y = content_start_y + max_column_height;

    log_debug("[MULTICOL] Final layout: %d columns, max height=%.1f, block height=%.1f",
              column_count, max_column_height, block->height);
}
