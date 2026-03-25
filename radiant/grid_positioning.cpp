#include "grid.hpp"
#include "view.hpp"
#include "layout_alignment.hpp"
#include "../lambda/input/css/css_style_node.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
}

// Position grid items based on computed track sizes
void position_grid_items(GridContainerLayout* grid_layout, ViewBlock* container) {
    if (!grid_layout || !container) return;

    log_debug(" Positioning grid items - container: %.0fx%.0f at (%.0f,%.0f)\n",
           container->width, container->height, container->x, container->y);
    log_debug(" Grid content dimensions: %dx%d\n",
           grid_layout->content_width, grid_layout->content_height);
    log_debug(" Grid gaps - row: %.1f, column: %.1f\n",
           grid_layout->row_gap, grid_layout->column_gap);
    log_debug("Positioning grid items\n");

    // Calculate track positions
    int* row_positions = (int*)mem_calloc(grid_layout->computed_row_count + 1, sizeof(int), MEM_CAT_LAYOUT);
    int* column_positions = (int*)mem_calloc(grid_layout->computed_column_count + 1, sizeof(int), MEM_CAT_LAYOUT);

    // First, calculate the total grid content size (all tracks + gaps)
    int total_row_size = 0;
    for (int i = 0; i < grid_layout->computed_row_count; i++) {
        total_row_size += grid_layout->computed_rows[i].computed_size;
        if (i < grid_layout->computed_row_count - 1) {
            total_row_size += (int)grid_layout->row_gap;
        }
    }

    int total_column_size = 0;
    for (int i = 0; i < grid_layout->computed_column_count; i++) {
        total_column_size += grid_layout->computed_columns[i].computed_size;
        if (i < grid_layout->computed_column_count - 1) {
            total_column_size += (int)grid_layout->column_gap;
        }
    }

    log_debug(" Total grid content: %dx%d, container content: %dx%d\n",
              total_column_size, total_row_size,
              grid_layout->content_width, grid_layout->content_height);

    // Calculate justify-content offset and spacing (horizontal)
    // Using unified alignment functions from layout_alignment.hpp
    int justify_offset = 0;
    float justify_spacing = 0;  // Additional spacing between tracks
    int extra_column_space = grid_layout->content_width - total_column_size;
    int col_count = grid_layout->computed_column_count;
    if (col_count > 0) {
        int32_t justify = grid_layout->justify_content;
        if (extra_column_space > 0 && radiant::alignment_is_space_distribution(justify)) {
            // Use space distribution for space-between/around/evenly (only with positive space)
            radiant::SpaceDistribution dist = radiant::compute_space_distribution(
                justify, (float)extra_column_space, col_count, 0.0f);
            justify_offset = (int)dist.gap_before_first;
            justify_spacing = dist.gap_between;
        } else {
            // Use single offset for start/end/center
            // Also handles negative free space (overflow centering)
            // Space distribution types fall back to start with negative space
            int32_t effective_align = justify;
            if (extra_column_space < 0 && radiant::alignment_is_space_distribution(justify)) {
                effective_align = CSS_VALUE_START;  // Fall back to start for overflow
            }
            justify_offset = (int)radiant::compute_alignment_offset_simple(effective_align, (float)extra_column_space);
        }
    }
    log_debug(" justify-content=%d, extra_space=%d, offset=%d, spacing=%.1f\n",
              grid_layout->justify_content, extra_column_space, justify_offset, justify_spacing);

    // Calculate align-content offset and spacing (vertical)
    // Using unified alignment functions from layout_alignment.hpp
    int align_offset = 0;
    float align_spacing = 0;  // Additional spacing between tracks
    int extra_row_space = grid_layout->content_height - total_row_size;
    int row_count = grid_layout->computed_row_count;
    if (row_count > 0) {
        int32_t align = grid_layout->align_content;
        if (extra_row_space > 0 && radiant::alignment_is_space_distribution(align)) {
            // Use space distribution for space-between/around/evenly (only with positive space)
            radiant::SpaceDistribution dist = radiant::compute_space_distribution(
                align, (float)extra_row_space, row_count, 0.0f);
            align_offset = (int)dist.gap_before_first;
            align_spacing = dist.gap_between;
        } else {
            // Use single offset for start/end/center
            // Also handles negative free space (overflow centering)
            // Space distribution types fall back to start with negative space
            int32_t effective_align = align;
            if (extra_row_space < 0 && radiant::alignment_is_space_distribution(align)) {
                effective_align = CSS_VALUE_START;  // Fall back to start for overflow
            }
            align_offset = (int)radiant::compute_alignment_offset_simple(effective_align, (float)extra_row_space);
        }
    }
    log_debug(" align-content=%d, extra_space=%d, offset=%d, spacing=%.1f\n",
              grid_layout->align_content, extra_row_space, align_offset, align_spacing);

    // Calculate row positions with align-content offset and spacing
    float current_y_f = align_offset;
    log_debug(" Calculating row positions for %d rows:\n", grid_layout->computed_row_count);
    for (int i = 0; i <= grid_layout->computed_row_count; i++) {
        row_positions[i] = (int)current_y_f;
        log_debug(" Row %d position: %d\n", i, row_positions[i]);
        if (i < grid_layout->computed_row_count) {
            int track_size = grid_layout->computed_rows[i].computed_size;
            log_debug(" Row %d size: %d\n", i, track_size);
            current_y_f += track_size;
            if (i < grid_layout->computed_row_count - 1) {
                current_y_f += grid_layout->row_gap;
                // Add space-* distribution spacing
                if (grid_layout->align_content == CSS_VALUE_SPACE_BETWEEN ||
                    grid_layout->align_content == CSS_VALUE_SPACE_AROUND ||
                    grid_layout->align_content == CSS_VALUE_SPACE_EVENLY) {
                    current_y_f += align_spacing;
                }
                log_debug(" Added row gap: %.1f + spacing %.1f, new current_y: %.1f\n",
                          grid_layout->row_gap, align_spacing, current_y_f);
            }
        }
    }

    // Calculate column positions with justify-content offset and spacing
    float current_x_f = justify_offset;
    log_debug(" Calculating column positions for %d columns:\n", grid_layout->computed_column_count);
    for (int i = 0; i <= grid_layout->computed_column_count; i++) {
        column_positions[i] = (int)current_x_f;
        log_debug(" Column %d position: %d\n", i, column_positions[i]);
        if (i < grid_layout->computed_column_count) {
            int track_size = grid_layout->computed_columns[i].computed_size;
            log_debug(" Column %d size: %d\n", i, track_size);
            current_x_f += track_size;
            if (i < grid_layout->computed_column_count - 1) {
                current_x_f += grid_layout->column_gap;
                // Add space-* distribution spacing
                if (grid_layout->justify_content == CSS_VALUE_SPACE_BETWEEN ||
                    grid_layout->justify_content == CSS_VALUE_SPACE_AROUND ||
                    grid_layout->justify_content == CSS_VALUE_SPACE_EVENLY) {
                    current_x_f += justify_spacing;
                }
                log_debug(" Added column gap: %.1f + spacing %.1f, new current_x: %.1f\n",
                          grid_layout->column_gap, justify_spacing, current_x_f);
            }
        }
    }

    // Position each grid item
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        log_debug(" Item pointer %d: %p, item->gi=%p\n", i, (void*)item, item ? (void*)item->gi : nullptr);
        if (!item->gi) continue;  // Skip items without grid item properties

        // Get grid area bounds (convert from 1-indexed to 0-indexed)
        int row_start = item->gi->computed_grid_row_start - 1;
        int row_end = item->gi->computed_grid_row_end - 1;
        int col_start = item->gi->computed_grid_column_start - 1;
        int col_end = item->gi->computed_grid_column_end - 1;

        // Clamp to valid ranges
        row_start = fmax(0, fmin(row_start, grid_layout->computed_row_count - 1));
        row_end = fmax(row_start + 1, fmin(row_end, grid_layout->computed_row_count));
        col_start = fmax(0, fmin(col_start, grid_layout->computed_column_count - 1));
        col_end = fmax(col_start + 1, fmin(col_end, grid_layout->computed_column_count));

        // Calculate item position and size
        int item_x = column_positions[col_start];
        int item_y = row_positions[row_start];

        // Calculate track width by summing individual track sizes (not from positions, which include gaps)
        int track_width = 0;
        for (int c = col_start; c < col_end; c++) {
            track_width += grid_layout->computed_columns[c].computed_size;
            // Add gap for interior tracks (not the last one in the span)
            if (c < col_end - 1) {
                track_width += (int)grid_layout->column_gap;
            }
        }

        int track_height = 0;
        for (int r = row_start; r < row_end; r++) {
            track_height += grid_layout->computed_rows[r].computed_size;
            // Add gap for interior tracks (not the last one in the span)
            if (r < row_end - 1) {
                track_height += (int)grid_layout->row_gap;
            }
        }

        // Store track area dimensions and base position for alignment phase
        if (item->gi) {
            item->gi->track_area_width = track_width;
            item->gi->track_area_height = track_height;
        }

        // Determine item dimensions - use CSS-specified size if available,
        // otherwise default to track size (will be adjusted during alignment)
        int item_width = track_width;
        int item_height = track_height;

        // Check if item has explicit CSS width
        if (item->blk && item->blk->given_width > 0) {
            item_width = (int)item->blk->given_width;
        }

        // Check if item has explicit CSS height
        if (item->blk && item->blk->given_height > 0) {
            item_height = (int)item->blk->given_height;
        }

        // CSS Box Model: border-box minimum — the box cannot be smaller than padding+border
        if (item->blk && item->blk->box_sizing == CSS_VALUE_BORDER_BOX && item->bound) {
            float h_pad_border = item->bound->padding.left + item->bound->padding.right;
            float v_pad_border = item->bound->padding.top + item->bound->padding.bottom;
            if (item->bound->border) {
                h_pad_border += item->bound->border->width.left + item->bound->border->width.right;
                v_pad_border += item->bound->border->width.top + item->bound->border->width.bottom;
            }
            if (item_width < (int)h_pad_border) {
                item_width = (int)h_pad_border;
            }
            if (item_height < (int)v_pad_border) {
                item_height = (int)v_pad_border;
            }
        }

        // Apply min-width/max-width and min-height/max-height constraints
        if (item->blk) {
            if (item->blk->given_max_width > 0 && item_width > (int)item->blk->given_max_width) {
                item_width = (int)item->blk->given_max_width;
            }
            if (item->blk->given_min_width > 0 && item_width < (int)item->blk->given_min_width) {
                item_width = (int)item->blk->given_min_width;
            }
            if (item->blk->given_max_height > 0 && item_height > (int)item->blk->given_max_height) {
                item_height = (int)item->blk->given_max_height;
            }
            if (item->blk->given_min_height > 0 && item_height < (int)item->blk->given_min_height) {
                item_height = (int)item->blk->given_min_height;
            }
        }

        // Apply container offset (borders and padding)
        int container_offset_x = 0;
        int container_offset_y = 0;

        if (container->bound) {
            container_offset_x += container->bound->padding.left;
            container_offset_y += container->bound->padding.top;
        }

        if (container->bound && container->bound->border) {
            container_offset_x += container->bound->border->width.left;
            container_offset_y += container->bound->border->width.top;
        }

        // Set item position and size (relative to parent's border box, per Radiant coordinate system)
        float new_x = container_offset_x + item_x;
        float new_y = container_offset_y + item_y;

        // Store base track position (before alignment) for later re-alignment
        if (item->gi) {
            item->gi->track_base_x = new_x;
            item->gi->track_base_y = new_y;
        }

        log_debug(" Assigning item %d: x=%.0f (%d+%d), y=%.0f, width=%d, height=%d\n",
               i, new_x, container_offset_x, item_x, new_y, item_width, item_height);
        log_debug(" Before assignment - item->x=%.0f, item->y=%.0f, item=%p\n", item->x, item->y, (void*)item);
        item->x = new_x;
        item->y = new_y;
        item->width = (float)item_width;
        item->height = (float)item_height;
        log_debug(" After assignment item %d: x=%.0f, y=%.0f, width=%.0f, height=%.0f at item=%p\n",
               i, item->x, item->y, item->width, item->height, (void*)item);

        log_debug(" Grid item %d positioning:\n", i);
        log_debug("  Grid area: row %d-%d, col %d-%d\n", row_start, row_end, col_start, col_end);
        log_debug("  Track positions: x=%d, y=%d\n", item_x, item_y);
        log_debug("  Track sizes: width=%d, height=%d\n", item_width, item_height);
        log_debug("  Container: offset=(%d,%d)\n", container_offset_x, container_offset_y);
        log_debug("  Final position: (%.0f,%.0f), size: %.0fx%.0f\n",
               item->x, item->y, item->width, item->height);

        log_debug("Positioned grid item %d: pos=(%d,%d), size=%dx%d, grid_area=(%d-%d, %d-%d)\n",
                  i, item->x, item->y, item->width, item->height,
                  row_start + 1, row_end, col_start + 1, col_end);
    }

    mem_free(row_positions);
    mem_free(column_positions);

    log_debug("Grid items positioned\n");
}

// Helper: read the CSS percentage value for a horizontal margin side (left or right).
// If the margin declared in specified_style was a percentage, returns pct/100 * track_width.
// Returns the pre-resolved value from bound for fixed lengths, 0 for auto/absent.
// side: 0=left, 1=right, 2=top, 3=bottom
static float resolve_margin_side(ViewBlock* item, int side, float track_width) {
    if (!item || !item->bound) return 0.0f;

    float  bound_val  = 0.0f;
    CssEnum bound_type = CSS_VALUE__UNDEF;
    switch (side) {
        case 0: bound_val = item->bound->margin.left;   bound_type = item->bound->margin.left_type;   break;
        case 1: bound_val = item->bound->margin.right;  bound_type = item->bound->margin.right_type;  break;
        case 2: bound_val = item->bound->margin.top;    bound_type = item->bound->margin.top_type;    break;
        case 3: bound_val = item->bound->margin.bottom; bound_type = item->bound->margin.bottom_type; break;
    }

    if (bound_type == CSS_VALUE_AUTO) return 0.0f;

    if (bound_type != CSS_VALUE__PERCENTAGE) {
        // Fixed length margin - use pre-resolved value
        return bound_val;
    }

    // Percentage margin: re-resolve against the actual track width.
    // CSS §11.4: for grid items, percentage margins resolve against the inline size of the grid area.
    // Try individual property first.
    static const CssPropertyId side_props[] = {
        CSS_PROPERTY_MARGIN_LEFT, CSS_PROPERTY_MARGIN_RIGHT,
        CSS_PROPERTY_MARGIN_TOP,  CSS_PROPERTY_MARGIN_BOTTOM
    };
    if (item->specified_style) {
        CssDeclaration* decl = style_tree_get_declaration(item->specified_style, side_props[side]);
        if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            return (float)(decl->value->data.percentage.value / 100.0) * track_width;
        }
        // Not set individually — check the shorthand margin property.
        CssDeclaration* sh = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN);
        if (sh && sh->value) {
            CssValue* pval = nullptr;
            if (sh->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                pval = sh->value;  // single value: applies to all sides
            } else if (sh->value->type == CSS_VALUE_TYPE_LIST) {
                int cnt = sh->value->data.list.count;
                CssValue** vals = sh->value->data.list.values;
                // CSS margin shorthand: top [right [bottom [left]]]
                // left (side==0): idx 3 for cnt==4, idx 1 for cnt<4
                // right (side==1): idx 1 for cnt>=2, idx 0 for cnt==1
                // top (side==2): idx 0
                // bottom (side==3): idx 2 for cnt>=3, idx 0 for cnt<3
                int idx = 0;
                if (side == 0) idx = (cnt == 4) ? 3 : 1;       // left
                else if (side == 1) idx = (cnt >= 2) ? 1 : 0;  // right
                else if (side == 2) idx = 0;                    // top
                else               idx = (cnt >= 3) ? 2 : 0;   // bottom
                if (idx < cnt) pval = vals[idx];
            }
            if (pval && pval->type == CSS_VALUE_TYPE_PERCENTAGE) {
                return (float)(pval->data.percentage.value / 100.0) * track_width;
            }
        }
    }
    return 0.0f;
}

// Align all grid items
void align_grid_items(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug("Aligning grid items\n");

    // Pre-pass: baseline alignment for rows with align-items: baseline
    bool has_baseline_alignment = radiant::alignment_is_baseline(grid_layout->align_items);
    if (!has_baseline_alignment) {
        for (int i = 0; i < grid_layout->item_count; i++) {
            ViewBlock* item = grid_layout->grid_items[i];
            if (item && item->gi) {
                int resolved = radiant::resolve_align_self(
                    item->gi->align_self_grid, grid_layout->align_items);
                if (radiant::alignment_is_baseline(resolved)) {
                    has_baseline_alignment = true;
                    break;
                }
            }
        }
    }

    if (has_baseline_alignment && grid_layout->computed_row_count > 0) {
        int row_count = grid_layout->computed_row_count;
        float* row_max_baseline = (float*)calloc(row_count, sizeof(float));
        float* row_max_below = (float*)calloc(row_count, sizeof(float));

        // First pass: compute baselines and find per-row max above/below baseline
        for (int i = 0; i < grid_layout->item_count; i++) {
            ViewBlock* item = grid_layout->grid_items[i];
            if (!item || !item->gi) continue;

            int resolved = radiant::resolve_align_self(
                item->gi->align_self_grid, grid_layout->align_items);
            if (!radiant::alignment_is_baseline(resolved)) continue;

            int row_idx = item->gi->computed_grid_row_start - 1;
            if (row_idx < 0 || row_idx >= row_count) continue;

            float baseline = radiant::compute_element_first_baseline(
                (radiant::LayoutContext*)grid_layout->lycon, item, true);
            if (baseline < 0) baseline = item->height;

            float below = item->height - baseline;
            if (baseline > row_max_baseline[row_idx])
                row_max_baseline[row_idx] = baseline;
            if (below > row_max_below[row_idx])
                row_max_below[row_idx] = below;
        }

        // Check if any row needs to grow for baseline alignment
        bool rows_changed = false;
        for (int r = 0; r < row_count; r++) {
            float needed = row_max_baseline[r] + row_max_below[r];
            if (needed > grid_layout->computed_rows[r].computed_size + 0.5f) {
                grid_layout->computed_rows[r].computed_size = (int)(needed + 0.5f);
                rows_changed = true;
            }
        }

        // If rows grew, re-position all items to account for new row positions
        if (rows_changed) {
            float row_gap = grid_layout->row_gap;
            // Get container padding/border offset
            float content_top_offset = 0;
            if (grid_layout->item_count > 0 && grid_layout->grid_items[0]) {
                ViewBlock* container = (ViewBlock*)((View*)grid_layout->grid_items[0])->parent;
                if (container) {
                    if (container->bound) {
                        content_top_offset += container->bound->padding.top;
                        if (container->bound->border)
                            content_top_offset += container->bound->border->width.top;
                    }
                }
            }

            // Recompute row offsets
            float row_offset = 0;
            for (int r = 0; r < row_count; r++) {
                grid_layout->computed_rows[r].base_size = (int)row_offset;
                row_offset += grid_layout->computed_rows[r].computed_size + row_gap;
            }

            // Re-position items based on new row offsets
            for (int i = 0; i < grid_layout->item_count; i++) {
                ViewBlock* item = grid_layout->grid_items[i];
                if (!item || !item->gi) continue;

                int row_idx = item->gi->computed_grid_row_start - 1;
                if (row_idx < 0 || row_idx >= row_count) continue;

                item->gi->track_base_y = (int)(grid_layout->computed_rows[row_idx].base_size
                                               + content_top_offset);

                // Recompute track_area_height for multi-row items
                int row_end_idx = item->gi->computed_grid_row_end - 1;
                if (row_end_idx >= row_count) row_end_idx = row_count - 1;
                if (row_end_idx < row_idx) row_end_idx = row_idx;
                float area_h = 0;
                for (int r = row_idx; r < row_end_idx; r++) {
                    area_h += grid_layout->computed_rows[r].computed_size;
                    if (r < row_end_idx - 1) area_h += row_gap;
                }
                item->gi->track_area_height = (int)area_h;
            }
        }

        // Apply baseline shifts to track_base_y
        for (int i = 0; i < grid_layout->item_count; i++) {
            ViewBlock* item = grid_layout->grid_items[i];
            if (!item || !item->gi) continue;

            int resolved = radiant::resolve_align_self(
                item->gi->align_self_grid, grid_layout->align_items);
            if (!radiant::alignment_is_baseline(resolved)) continue;

            int row_idx = item->gi->computed_grid_row_start - 1;
            if (row_idx < 0 || row_idx >= row_count) continue;

            float baseline = radiant::compute_element_first_baseline(
                (radiant::LayoutContext*)grid_layout->lycon, item, true);
            if (baseline < 0) baseline = item->height;

            float shift = row_max_baseline[row_idx] - baseline;
            if (shift > 0.01f) {
                item->gi->track_base_y += (int)shift;
            }
        }

        free(row_max_baseline);
        free(row_max_below);
    }

    for (int i = 0; i < grid_layout->item_count; i++) {
        align_grid_item(grid_layout->grid_items[i], grid_layout);
    }

    log_debug("Grid items aligned\n");
}

// Align a single grid item
void align_grid_item(ViewBlock* item, GridContainerLayout* grid_layout) {
    if (!item || !grid_layout || !item->gi) return;

    // Reset to base track position before applying alignment
    // This allows align_grid_item to be called multiple times (e.g., after content layout)
    item->x = item->gi->track_base_x;
    item->y = item->gi->track_base_y;

    // Use stored track area dimensions from positioning phase
    int available_width = item->gi->track_area_width;
    int available_height = item->gi->track_area_height;

    // CSS Grid §8.1: fixed (non-auto) margins reduce the usable grid area for alignment.
    // The item's alignment box = grid area minus fixed margins.  Auto margins are handled
    // separately (they consume the remaining free space after fixed-margin reduction).
    // Percentage margins are resolved against the actual track width (CSS §11.4).
    float margin_left   = resolve_margin_side(item, 0, (float)available_width);
    float margin_right  = resolve_margin_side(item, 1, (float)available_width);
    float margin_top    = resolve_margin_side(item, 2, (float)available_width);
    float margin_bottom = resolve_margin_side(item, 3, (float)available_width);

    // CSS Box 4 §3.2 margin-trim for grid containers: trim margins of items
    // adjacent to the container edges.
    ViewBlock* grid_container = item->parent->is_block() ? (ViewBlock*)item->parent : NULL;
    uint8_t grid_margin_trim = (grid_container && grid_container->blk) ? grid_container->blk->margin_trim : 0;
    if (grid_margin_trim) {
        int row_start = item->gi->computed_grid_row_start - 1;
        int row_end = item->gi->computed_grid_row_end - 1;
        int col_start = item->gi->computed_grid_column_start - 1;
        int col_end = item->gi->computed_grid_column_end - 1;
        // block-start: first row items
        if ((grid_margin_trim & MARGIN_TRIM_BLOCK_START) && row_start == 0) {
            log_debug("[MARGIN-TRIM] grid block-start: trimming margin_top=%f", margin_top);
            margin_top = 0;
            if (item->bound) item->bound->margin.top = 0;
        }
        // block-end: last row items
        if ((grid_margin_trim & MARGIN_TRIM_BLOCK_END) && row_end >= grid_layout->computed_row_count) {
            log_debug("[MARGIN-TRIM] grid block-end: trimming margin_bottom=%f", margin_bottom);
            margin_bottom = 0;
            if (item->bound) item->bound->margin.bottom = 0;
        }
        // inline-start: first column items
        if ((grid_margin_trim & MARGIN_TRIM_INLINE_START) && col_start == 0) {
            log_debug("[MARGIN-TRIM] grid inline-start: trimming margin_left=%f", margin_left);
            margin_left = 0;
            if (item->bound) item->bound->margin.left = 0;
        }
        // inline-end: last column items
        if ((grid_margin_trim & MARGIN_TRIM_INLINE_END) && col_end >= grid_layout->computed_column_count) {
            log_debug("[MARGIN-TRIM] grid inline-end: trimming margin_right=%f", margin_right);
            margin_right = 0;
            if (item->bound) item->bound->margin.right = 0;
        }
    }

    // Reduce available area by the fixed margins before any alignment calculation
    available_width  -= (int)(margin_left + margin_right);
    available_height -= (int)(margin_top  + margin_bottom);
    if (available_width  < 0) available_width  = 0;
    if (available_height < 0) available_height = 0;
    // Offset the base position so alignment operates inside the margin box
    item->x += margin_left;
    item->y += margin_top;

    // Check if item has aspect-ratio constraint
    // IMPORTANT: fi and gi are in a union - for grid items, fi is overwritten by gi
    // So we need to check specified_style directly for aspect-ratio
    float aspect_ratio = 0;

    // First check fi (only valid for flex items)
    if (item->item_prop_type == DomElement::ITEM_PROP_FLEX && item->fi && item->fi->aspect_ratio > 0) {
        aspect_ratio = item->fi->aspect_ratio;
    }
    // For grid items, check specified_style directly
    else if (item->specified_style) {
        CssDeclaration* aspect_decl = style_tree_get_declaration(
            item->specified_style, CSS_PROPERTY_ASPECT_RATIO);
        if (aspect_decl && aspect_decl->value) {
            if (aspect_decl->value->type == CSS_VALUE_TYPE_NUMBER) {
                aspect_ratio = (float)aspect_decl->value->data.number.value;
                log_debug("align_grid_item: aspect-ratio from specified_style: %.3f", aspect_ratio);
            } else if (aspect_decl->value->type == CSS_VALUE_TYPE_LIST &&
                       aspect_decl->value->data.list.count >= 2) {
                // Handle "width / height" format - find two numbers in the list
                double numerator = 0, denominator = 0;
                bool got_numerator = false, got_denominator = false;
                for (int i = 0; i < aspect_decl->value->data.list.count && !got_denominator; i++) {
                    CssValue* v = aspect_decl->value->data.list.values[i];
                    if (v && v->type == CSS_VALUE_TYPE_NUMBER) {
                        if (!got_numerator) {
                            numerator = v->data.number.value;
                            got_numerator = true;
                        } else {
                            denominator = v->data.number.value;
                            got_denominator = true;
                        }
                    }
                }
                if (got_numerator && got_denominator && denominator > 0) {
                    aspect_ratio = (float)(numerator / denominator);
                    log_debug("align_grid_item: aspect-ratio from specified_style list: %.3f", aspect_ratio);
                } else if (got_numerator) {
                    aspect_ratio = (float)numerator;
                }
            }
        }
    }
    bool has_explicit_width = (item->blk && item->blk->given_width > 0);
    bool has_explicit_height = (item->blk && item->blk->given_height > 0);
    float max_width = (item->blk && item->blk->given_max_width > 0) ? item->blk->given_max_width : 0;
    float max_height = (item->blk && item->blk->given_max_height > 0) ? item->blk->given_max_height : 0;

    log_debug("align_grid_item: aspect_ratio=%.6f, available=%dx%d",
              aspect_ratio, available_width, available_height);

    // If aspect-ratio is set, compute the missing dimension
    if (aspect_ratio > 0) {
        if (has_explicit_width && !has_explicit_height) {
            // Width is explicit, compute height from aspect ratio
            item->height = item->width / aspect_ratio;
            log_debug("align_grid_item: computed height=%.1f from width=%.1f and aspect_ratio=%.3f",
                      item->height, item->width, aspect_ratio);
        } else if (has_explicit_height && !has_explicit_width) {
            // Height is explicit, compute width from aspect ratio
            item->width = item->height * aspect_ratio;
            log_debug("align_grid_item: computed width=%.1f from height=%.1f and aspect_ratio=%.3f",
                      item->width, item->height, aspect_ratio);
        } else if (!has_explicit_width && !has_explicit_height) {
            // Neither dimension is explicit
            // Check for max-width/max-height constraints first
            if (max_width > 0 && max_height > 0) {
                // Both max constraints - use whichever is more constraining
                float w1 = max_width;
                float h1 = max_width / aspect_ratio;
                float w2 = max_height * aspect_ratio;
                float h2 = max_height;
                if (h1 <= max_height) {
                    item->width = w1;
                    item->height = h1;
                } else {
                    item->width = w2;
                    item->height = h2;
                }
            } else if (max_width > 0) {
                // max-width constraint, compute height from it
                item->width = max_width;
                item->height = max_width / aspect_ratio;
                log_debug("align_grid_item: computed from max_width=%.1f: width=%.1f, height=%.1f",
                          max_width, item->width, item->height);
            } else if (max_height > 0) {
                // max-height constraint, compute width from it
                item->height = max_height;
                item->width = max_height * aspect_ratio;
                log_debug("align_grid_item: computed from max_height=%.1f: width=%.1f, height=%.1f",
                          max_height, item->width, item->height);
            } else {
                // No explicit size, no max constraints.
                // CSS Grid: with default stretch alignment, determine which axis takes priority.
                // If min-height is specified (block-axis constraint), anchor the block axis first
                // and derive inline from it (height → width). This transfers the block constraint
                // through the aspect-ratio to produce the correct minimum inline size.
                // Otherwise use the inline axis first (width → height), per CSS Grid §6.6.
                float min_h = (item->blk && item->blk->given_min_height > 0) ? item->blk->given_min_height : 0;
                float min_w = (item->blk && item->blk->given_min_width > 0) ? item->blk->given_min_width : 0;
                if (min_h > 0 && min_w == 0) {
                    // Block-axis minimum: anchor at available_height (stretch), derive width
                    item->height = (float)available_height;
                    item->width = item->height * aspect_ratio;
                    log_debug("align_grid_item: aspect-ratio block-primary (min-height=%.1f): width=%.1f, height=%.1f",
                              min_h, item->width, item->height);
                } else {
                    // Inline-axis priority: anchor at available_width (stretch), derive height
                    item->width = (float)available_width;
                    item->height = (float)available_width / aspect_ratio;
                    log_debug("align_grid_item: aspect-ratio from available_width=%d: width=%.1f, height=%.1f",
                              available_width, item->width, item->height);
                }
            }
        }

        // Apply max-width/max-height constraints after aspect-ratio calculation
        if (max_width > 0 && item->width > max_width) {
            item->width = max_width;
            item->height = max_width / aspect_ratio;
        }
        if (max_height > 0 && item->height > max_height) {
            item->height = max_height;
            item->width = max_height * aspect_ratio;
        }
        // Apply min-width/min-height constraints (min wins over max per CSS spec)
        float min_width = (item->blk && item->blk->given_min_width > 0) ? item->blk->given_min_width : 0;
        float min_height = (item->blk && item->blk->given_min_height > 0) ? item->blk->given_min_height : 0;
        if (min_width > 0 && item->width < min_width) {
            item->width = min_width;
            item->height = min_width / aspect_ratio;
        }
        if (min_height > 0 && item->height < min_height) {
            item->height = min_height;
            item->width = min_height * aspect_ratio;
        }
    }

    // P6: Auto margins override justify-self/align-self, consuming available free space (CSS Grid §8.1)
    bool applied_horiz_auto = false, applied_vert_auto = false;
    if (item->bound) {
        bool left_auto  = (item->bound->margin.left_type   == CSS_VALUE_AUTO);
        bool right_auto = (item->bound->margin.right_type  == CSS_VALUE_AUTO);
        bool top_auto   = (item->bound->margin.top_type    == CSS_VALUE_AUTO);
        bool bot_auto   = (item->bound->margin.bottom_type == CSS_VALUE_AUTO);
        // CSS Grid §8.1: auto margins consume the free space between the item's margin box and the
        // grid area.  The item's actual (content) size must be used, not the stretch-assigned size.
        // Pass 3 sets content_width/content_height; fall back to current item->width/height if not set.
        // When the item has an explicit size (given_width/given_height > 0), the explicit size takes
        // priority over content_height (which may be 0 for an empty flex container despite height: Npx).
        // For items WITHOUT explicit size, content_height=0 is valid (empty element), so >= 0 is used
        // to correctly center empty elements at height=0 with margin:auto.
        float actual_item_width  = has_explicit_width
                                   ? (float)item->blk->given_width
                                   : ((item->content_width > 0 && item->content_width < (float)available_width)
                                      ? item->content_width : item->width);
        float actual_item_height = has_explicit_height
                                   ? (float)item->blk->given_height
                                   : ((item->content_height >= 0 && item->content_height < (float)available_height)
                                      ? item->content_height : item->height);
        float horiz_free = (float)available_width  - actual_item_width;
        float vert_free  = (float)available_height - actual_item_height;
        if (left_auto || right_auto) {
            applied_horiz_auto = true;
            item->width = actual_item_width;   // shrink to content size before shifting
            if (left_auto && right_auto) item->x += horiz_free * 0.5f;
            else if (left_auto)          item->x += horiz_free;
            // right_auto alone: item stays at track start (no shift)
        }
        if (top_auto || bot_auto) {
            applied_vert_auto = true;
            item->height = actual_item_height; // shrink to content size before shifting
            if (top_auto && bot_auto) item->y += vert_free * 0.5f;
            else if (top_auto)        item->y += vert_free;
            // bot_auto alone: item stays at track start (no shift)
        }
    }

    // Apply justify-self (horizontal alignment)
    // Using unified resolve function from layout_alignment.hpp
    int justify = radiant::resolve_justify_self(item->gi->justify_self, grid_layout->justify_items);

    // For non-stretch alignment, use content width if available (set by Pass 3 content layout)
    // This allows center/start/end to work correctly with intrinsic content size
    float actual_width = item->width;
    if (!applied_horiz_auto && justify != CSS_VALUE_STRETCH && !has_explicit_width) {
        // Use content width if it was computed in Pass 3
        if (item->content_width > 0 && item->content_width < available_width) {
            actual_width = item->content_width;
            item->width = actual_width;
        }
    }

    // Apply horizontal alignment offset (skipped when auto margins already consumed free space)
    float free_width = available_width - actual_width;
    if (!applied_horiz_auto) {
        if (!radiant::alignment_is_stretch(justify)) {
            item->x += radiant::compute_alignment_offset_simple(justify, free_width);
        } else {
            // Stretch to fill track area (unless item has explicit width or aspect-ratio)
            if (!has_explicit_width && aspect_ratio <= 0) {
                item->width = available_width;
            }
        }
    }

    // Apply align-self (vertical alignment)
    // Using unified resolve function from layout_alignment.hpp
    int align = radiant::resolve_align_self(item->gi->align_self_grid, grid_layout->align_items);

    // For non-stretch alignment, use content height if available (set by Pass 3 content layout)
    // This allows center/start/end to work correctly with intrinsic content size
    float actual_height = item->height;
    if (!applied_vert_auto && align != CSS_VALUE_STRETCH && !has_explicit_height) {
        // Use content height if it was computed in Pass 3
        // Content height should be used regardless of whether it's smaller or larger
        // than available height - the item should size to its content for non-stretch alignment
        if (item->content_height > 0) {
            actual_height = item->content_height;
            item->height = actual_height;
            log_debug("align_grid_item: using content_height=%.1f for non-stretch alignment", item->content_height);
        }
    }

    // Apply vertical alignment offset (skipped when auto margins already consumed free space)
    float free_height = available_height - actual_height;
    if (!applied_vert_auto) {
        if (!radiant::alignment_is_stretch(align)) {
            item->y += radiant::compute_alignment_offset_simple(align, free_height);
        } else {
            // Stretch to fill track area (unless item has explicit height or aspect-ratio)
            if (!has_explicit_height && aspect_ratio <= 0) {
                item->height = available_height;
            }
        }
    }

    log_debug("Aligned grid item: justify=%d, align=%d, final_pos=(%.0f,%.0f), final_size=%.0fx%.0f\n",
              justify, align, item->x, item->y, item->width, item->height);
}
