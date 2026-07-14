#include "layout.hpp"
#include "view.hpp"
#include "grid_enhanced_adapter.hpp"  // Enhanced grid integration
#include "../lib/tagged.hpp"
#include "../lib/mem_grow.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
}

// Forward declarations
void expand_auto_repeat_tracks(GridContainerLayout* grid_layout);
void collapse_empty_auto_fit_tracks(GridContainerLayout* grid_layout);

static ViewBlock** grid_item_array_alloc(int count) {
    return reinterpret_cast<ViewBlock**>(mem_calloc(count, sizeof(ViewBlock*), MEM_CAT_LAYOUT));
}

// Initialize grid container layout state
void init_grid_container(LayoutContext* lycon, ViewBlock* container) {
    if (!container) return;
    log_debug("%s Initializing grid container for %p\n", container->source_loc(), container);

    GridContainerLayout* grid = (GridContainerLayout*)mem_calloc(1, sizeof(GridContainerLayout), MEM_CAT_LAYOUT);
    lycon->grid_container = grid;
    grid->lycon = lycon;  // Store layout context for intrinsic sizing

    // Initialize auto-placement cursors (grid lines are 1-indexed)
    grid->auto_row_cursor = 1;
    grid->auto_col_cursor = 1;

    // Debug: check what's available
    log_debug("%s container->embed=%p", container->source_loc(), (void*)container->embed);
    if (container->embed) {
        log_debug("%s container->embed->grid=%p", container->source_loc(), (void*)container->embed->grid);
        if (container->embed->grid) {
            log_debug("%s embed->grid values: row_gap=%.1f, column_gap=%.1f", container->source_loc(),
                      container->embed->grid->row_gap, container->embed->grid->column_gap);
        }
    }

    if (container->embed && container->embed->grid) {
        memcpy(grid, container->embed->grid, sizeof(GridProp));
        grid->lycon = lycon;  // Restore after memcpy
        log_debug("%s Copied grid props: row_gap=%.1f, column_gap=%.1f", container->source_loc(),
                  grid->row_gap, grid->column_gap);
    } else {
        // Set default values using enum names that align with Lexbor constants
        grid->justify_content = CSS_VALUE_START;
        grid->align_content = CSS_VALUE_START;
        grid->justify_items = CSS_VALUE_STRETCH;
        grid->align_items = CSS_VALUE_STRETCH;
        grid->grid_auto_flow = CSS_VALUE_ROW;
        // Initialize gaps
        grid->row_gap = 0;
        grid->column_gap = 0;
    }

    // Initialize dynamic arrays
    grid->allocated_items = 8;
    grid->grid_items = grid_item_array_alloc(grid->allocated_items);

    // Only allocate new areas array if not already copied from embed->grid
    if (!grid->grid_areas || grid->area_count == 0) {
        grid->allocated_areas = 4;
        grid->grid_areas = (GridArea*)mem_calloc(grid->allocated_areas, sizeof(GridArea), MEM_CAT_LAYOUT);
        grid->area_count = 0;  // Reset if we allocated new
        grid->owns_grid_areas = true;
    } else {
        grid->owns_grid_areas = false;
    }
    // If grid_areas was copied from embed->grid, keep it as-is
    log_debug("%s Grid areas after init: area_count=%d, grid_areas=%p", container->source_loc(), grid->area_count, (void*)grid->grid_areas);

    grid->allocated_line_names = 8;
    grid->line_names = (GridLineName*)mem_calloc(grid->allocated_line_names, sizeof(GridLineName), MEM_CAT_LAYOUT);

    // Initialize track lists - only create new ones if not already copied from embed->grid
    // Track ownership to avoid double-free
    if (!grid->grid_template_rows) {
        grid->grid_template_rows = create_grid_track_list(4);
        grid->owns_template_rows = true;
    } else {
        grid->owns_template_rows = false;  // Shared with embed->grid
    }
    if (!grid->grid_template_columns) {
        grid->grid_template_columns = create_grid_track_list(4);
        grid->owns_template_columns = true;
    } else {
        grid->owns_template_columns = false;  // Shared with embed->grid
    }
    if (!grid->grid_auto_rows) {
        grid->grid_auto_rows = create_grid_track_list(2);
        grid->owns_auto_rows = true;
    } else {
        grid->owns_auto_rows = false;  // Shared with embed->grid
    }
    if (!grid->grid_auto_columns) {
        grid->grid_auto_columns = create_grid_track_list(2);
        grid->owns_auto_columns = true;
    } else {
        grid->owns_auto_columns = false;  // Shared with embed->grid
    }

    grid->needs_reflow = false;

    log_debug("%s Grid container initialized successfully\n", container->source_loc());
}

// Cleanup grid container resources
void cleanup_grid_container(LayoutContext* lycon) {
    if (!lycon || !lycon->grid_container) return;
    log_debug("Cleaning up grid container for %p\n", lycon->grid_container);
    GridContainerLayout* grid = lycon->grid_container;

    // Free track lists only if we own them (not shared with embed->grid)
    if (grid->owns_template_rows) {
        destroy_grid_track_list(grid->grid_template_rows);
    }
    if (grid->owns_template_columns) {
        destroy_grid_track_list(grid->grid_template_columns);
    }
    if (grid->owns_auto_rows) {
        destroy_grid_track_list(grid->grid_auto_rows);
    }
    if (grid->owns_auto_columns) {
        destroy_grid_track_list(grid->grid_auto_columns);
    }

    // Free computed tracks
    if (grid->computed_rows) {
        for (int i = 0; i < grid->computed_row_count; i++) {
            // Only free size if we own it (created during init, not shared)
            if (grid->computed_rows[i].size && grid->computed_rows[i].owns_size) {
                destroy_grid_track_size(grid->computed_rows[i].size);
            }
        }
        mem_free(grid->computed_rows);
    }

    if (grid->computed_columns) {
        for (int i = 0; i < grid->computed_column_count; i++) {
            // Only free size if we own it (created during init, not shared)
            if (grid->computed_columns[i].size && grid->computed_columns[i].owns_size) {
                destroy_grid_track_size(grid->computed_columns[i].size);
            }
        }
        mem_free(grid->computed_columns);
    }

    // Free grid areas only if this layout state allocated them locally.
    if (grid->owns_grid_areas) {
        for (int i = 0; i < grid->area_count; i++) {
            destroy_grid_area(&grid->grid_areas[i]);
        }
        mem_free(grid->grid_areas);
    }

    // Free line names
    for (int i = 0; i < grid->line_name_count; i++) {
        mem_free(grid->line_names[i].name);
    }
    mem_free(grid->line_names);

    mem_free(grid->grid_items);
    mem_free(grid);
    log_debug("Grid container cleanup complete\n");
}

// Main grid layout algorithm entry point
void layout_grid_container(LayoutContext* lycon, ViewBlock* container) {
    log_debug("%s layout_grid_container called with container=%p", container->source_loc(), container);
    if (!container) {
        log_debug("%s Early return - container is NULL\n", container->source_loc());
        return;
    }

    // Check if this is actually a grid container by display type
    // Note: embed->grid may be NULL if grid-template-* properties weren't resolved,
    // but we can still run grid layout with auto-placement
    if (container->display.inner != CSS_VALUE_GRID) {
        log_debug("%s Early return - not a grid container (display.inner=%d)\n", container->source_loc(), container->display.inner);
        return;
    }

    GridContainerLayout* grid_layout = lycon->grid_container;
    log_debug("%s Grid container found - template_columns=%p, template_rows=%p", container->source_loc(),
        grid_layout->grid_template_columns, grid_layout->grid_template_rows);
    if (grid_layout->grid_template_columns) {
        log_debug("%s DEBUG: Template columns track count: %d", container->source_loc(), grid_layout->grid_template_columns->track_count);
    }
    if (grid_layout->grid_template_rows) {
        log_debug("%s DEBUG: Template rows track count: %d", container->source_loc(), grid_layout->grid_template_rows->track_count);
    }

    log_debug("%s GRID START - container: %dx%d at (%d,%d)", container->source_loc(),
        container->width, container->height, container->x, container->y);

    // Check if container is shrink-to-fit (absolutely positioned with no explicit width,
    // or inline-grid which uses shrink-to-fit sizing)
    bool is_shrink_to_fit_width = false;
    if (container->display.outer == CSS_VALUE_INLINE_BLOCK &&
        (!container->blk || container->blk->given_width < 0)) {
        is_shrink_to_fit_width = true;
    } else if (container->position &&
        (container->position->position == CSS_VALUE_ABSOLUTE ||
         container->position->position == CSS_VALUE_FIXED)) {
        bool has_explicit_width = container->blk && container->blk->given_width > 0;
        bool has_left_right = container->position->has_left && container->position->has_right;
        if (!has_explicit_width && !has_left_right) {
            is_shrink_to_fit_width = true;
        }
    }
    grid_layout->is_shrink_to_fit_width = is_shrink_to_fit_width;
    grid_layout->row_intrinsic_height = -1.0f;
    log_debug("%s GRID: is_shrink_to_fit_width=%d", container->source_loc(), is_shrink_to_fit_width);

    // Set container dimensions
    grid_layout->container_width = container->width;
    grid_layout->container_height = container->height;

    // Determine if container has an explicit height (not auto)
    // This affects whether auto row tracks should stretch to fill the container
    grid_layout->has_explicit_height = (container->blk && container->blk->given_height >= 0);
    log_debug("%s GRID: has_explicit_height=%d (given_height=%.1f)", container->source_loc(),
              grid_layout->has_explicit_height,
              container->blk ? container->blk->given_height : -1);

    // Calculate content dimensions (excluding borders and padding)
    grid_layout->content_width = container->width;
    grid_layout->content_height = container->height;

    BoxMetrics container_box = layout_box_metrics(container);
    grid_layout->content_width -= container_box.pad_border_h;
    grid_layout->content_height -= container_box.pad_border_v;

    // Resolve percentage gaps against the container dimensions.
    // For definite containers, resolve immediately. For indefinite (shrink-to-fit),
    // the enhanced adapter handles the two-pass resolution.
    if (grid_layout->column_gap_is_percent) {
        if (!is_shrink_to_fit_width) {
            grid_layout->column_gap = grid_layout->content_width * (grid_layout->column_gap / 100.0f);
            grid_layout->column_gap_is_percent = false;
        }
        // For shrink-to-fit: keep percentage value in column_gap, column_gap_is_percent=true
        // The adapter will use gap=0 for first pass, then resolve against intrinsic width
    }
    if (grid_layout->row_gap_is_percent) {
        if (grid_layout->has_explicit_height) {
            grid_layout->row_gap = grid_layout->content_height * (grid_layout->row_gap / 100.0f);
            grid_layout->row_gap_is_percent = false;
        }
    }

    log_debug("%s GRID CONTENT - content: %dx%d, container: %dx%d\n", container->source_loc(),
              grid_layout->content_width, grid_layout->content_height,
              container->width, container->height);

    // Phase 1: Collect grid items (need count for auto-fit)
    log_debug("%s DEBUG: Phase 1 - Collecting grid items", container->source_loc());
    ViewBlock** items;
    int item_count = collect_grid_items(grid_layout, container, &items);

    log_debug("%s GRID - collected %d items", container->source_loc(), item_count);

    // Expand auto-fill/auto-fit repeat() tracks now that we know content_width and item_count
    expand_auto_repeat_tracks(grid_layout);

    if (item_count == 0) {
        log_debug("%s No grid items found - computing track sizes for empty grid (for absolute children and container sizing)", container->source_loc());
        // Even with no items, we need to resolve explicit track sizes so that:
        // 1. Absolutely positioned children can use grid-line positions for their containing block.
        // 2. The container gets the correct height from explicit grid-template-rows + padding.
        determine_grid_size(grid_layout);
        resolve_track_sizes_enhanced(grid_layout, container);

        // Update container height from explicit row heights + padding (mirroring the shrink-to-fit path)
        if (!grid_layout->has_explicit_height && grid_layout->computed_row_count > 0) {
            float total_row_height = 0;
            for (int r = 0; r < grid_layout->computed_row_count; r++) {
                total_row_height += grid_layout->computed_rows[r].base_size;
            }
            if (grid_layout->computed_row_count > 1) {
                total_row_height += grid_layout->row_gap * (grid_layout->computed_row_count - 1);
            }
            float new_h = total_row_height + container_box.pad_border_v;
            if (new_h > (float)container->height) {
                container->height = (int)new_h; // INT_CAST_OK: grid container height
                log_debug("%s GRID: Updated empty-grid container height to %.1f from explicit rows", container->source_loc(), new_h);
            }
        }
        grid_layout->needs_reflow = false;
        return;
    }

    // Phase 2: Resolve grid template areas
    log_debug("%s DEBUG: Phase 2 - Resolving grid template areas", container->source_loc());
    resolve_grid_template_areas(grid_layout);

    // Phase 2.5: Register named grid lines from track lists and template areas
    log_debug("%s DEBUG: Phase 2.5 - Registering named grid lines", container->source_loc());
    if (grid_layout->grid_template_columns) {
        GridTrackList* cols = grid_layout->grid_template_columns;
        for (int i = 0; i <= cols->track_count && i < cols->allocated_tracks + 1; i++) {
            if (cols->line_names[i]) {
                add_grid_line_name(grid_layout, cols->line_names[i], i + 1, false);
            }
        }
    }
    if (grid_layout->grid_template_rows) {
        GridTrackList* rows = grid_layout->grid_template_rows;
        for (int i = 0; i <= rows->track_count && i < rows->allocated_tracks + 1; i++) {
            if (rows->line_names[i]) {
                add_grid_line_name(grid_layout, rows->line_names[i], i + 1, true);
            }
        }
    }
    // Generate <area>-start / <area>-end named line aliases from grid-template-areas
    {
        char name_buf[512];
        for (int a = 0; a < grid_layout->area_count; a++) {
            GridArea* area = &grid_layout->grid_areas[a];
            if (!area->name) continue;
            snprintf(name_buf, sizeof(name_buf), "%s-start", area->name);
            add_grid_line_name(grid_layout, name_buf, area->column_start, false);
            add_grid_line_name(grid_layout, name_buf, area->row_start, true);
            snprintf(name_buf, sizeof(name_buf), "%s-end", area->name);
            add_grid_line_name(grid_layout, name_buf, area->column_end, false);
            add_grid_line_name(grid_layout, name_buf, area->row_end, true);
        }
    }

    // Phase 3: Determine initial grid size from templates (before placement)
    log_debug("%s DEBUG: Phase 3 - Determining initial grid size from templates", container->source_loc());
    determine_grid_size(grid_layout);

    // Phase 3.5: Resolve named line references into integer line numbers
    log_debug("%s DEBUG: Phase 3.5 - Resolving named line references", container->source_loc());
    for (int idx = 0; idx < item_count; idx++) {
        ViewBlock* item = items[idx];
        GridItemProp* gi = grid_item_prop(item);
        if (!gi) continue;
        if (gi->grid_column_start_name) {
            int ln = find_grid_line_by_name(grid_layout, gi->grid_column_start_name, false);
            if (ln > 0) { gi->grid_column_start = ln; gi->has_explicit_grid_column_start = true; }
        }
        if (gi->grid_column_end_name) {
            int ln = find_grid_line_by_name(grid_layout, gi->grid_column_end_name, false);
            if (ln > 0) { gi->grid_column_end = ln; gi->has_explicit_grid_column_end = true; }
        }
        if (gi->grid_row_start_name) {
            int ln = find_grid_line_by_name(grid_layout, gi->grid_row_start_name, true);
            if (ln > 0) { gi->grid_row_start = ln; gi->has_explicit_grid_row_start = true; }
        }
        if (gi->grid_row_end_name) {
            int ln = find_grid_line_by_name(grid_layout, gi->grid_row_end_name, true);
            if (ln > 0) { gi->grid_row_end = ln; gi->has_explicit_grid_row_end = true; }
        }
    }

    // Phase 4: Place grid items (using enhanced CellOccupancyMatrix algorithm)
    log_debug("%s DEBUG: Phase 4 - Placing grid items with enhanced algorithm", container->source_loc());

    // Use enhanced placement algorithm with proper collision detection.
    radiant::grid_adapter::place_items_with_occupancy(
        grid_layout,
        items,
        item_count,
        grid_layout->grid_auto_flow,
        grid_layout->is_dense_packing
    );

    // Phase 5: Update grid size after placement (may have grown due to auto-placement)
    // NOTE: place_items_with_occupancy already sets computed_column_count and computed_row_count
    // from the occupancy matrix which correctly accounts for negative implicit tracks.
    // We must NOT let determine_grid_size() shrink these values — it only sees item
    // end positions and misses the full grid extent when negative implicit tracks exist.
    // Instead, just ensure the counts are at least as large as what items require.
    log_debug("%s DEBUG: Phase 5 - Updating grid size after placement", container->source_loc());
    {
        int prev_col_count = grid_layout->computed_column_count;
        int prev_row_count = grid_layout->computed_row_count;
        int prev_implicit_cols = grid_layout->implicit_column_count;
        int prev_implicit_rows = grid_layout->implicit_row_count;

        determine_grid_size(grid_layout);

        // Restore if placement knew about more tracks (e.g. negative implicit)
        if (grid_layout->computed_column_count < prev_col_count) {
            grid_layout->computed_column_count = prev_col_count;
            grid_layout->implicit_column_count = prev_implicit_cols;
        }
        if (grid_layout->computed_row_count < prev_row_count) {
            grid_layout->computed_row_count = prev_row_count;
            grid_layout->implicit_row_count = prev_implicit_rows;
        }
    }

    // Phase 5.5: Auto-fit empty track collapsing is deferred until proper gutter
    // collapsing is implemented (CSS Grid §7.2.3.2 requires collapsed gutters too).

    // Phase 5.6: CSS Grid §11.7.1 — Adjust orthogonal flow items' width contributions.
    // For grid items with a vertical writing mode, the physical min-content width
    // (block size) depends on the available inline size (physical height from row tracks).
    // If the spanned row tracks have definite sizes, re-measure the item at that height.
    {
        int neg_row_offset = grid_layout->negative_implicit_row_count;
        int explicit_row_start = neg_row_offset;

        for (int idx = 0; idx < item_count; idx++) {
            ViewBlock* item = items[idx];
            GridItemProp* gi = grid_item_prop(item);
            if (!item || !gi || !gi->has_measured_size) continue;

            // Check if item has orthogonal writing mode
            bool is_orthogonal = false;
            if (item->embed && item->embed->flex) {
                WritingMode wm = item->embed->flex->writing_mode;
                is_orthogonal = (wm == WM_VERTICAL_LR || wm == WM_VERTICAL_RL);
            }
            if (!is_orthogonal) continue;

            // Get definite row height from template definitions.
            // Note: computed_rows are not yet allocated at this stage;
            // use grid_template_rows for explicit tracks directly.
            int rs = gi->computed_grid_row_start - 1;  // 0-based track index
            int re = gi->computed_grid_row_end - 1;
            if (rs < 0 || re <= rs || re > grid_layout->computed_row_count) continue;

            float definite_row_height = 0.0f;
            bool all_definite = true;
            for (int r = rs; r < re; r++) {
                // Map track index to template definition
                int tmpl_idx = r - explicit_row_start;  // index within explicit grid
                if (tmpl_idx >= 0 && tmpl_idx < grid_layout->explicit_row_count &&
                    grid_layout->grid_template_rows &&
                    tmpl_idx < grid_layout->grid_template_rows->track_count) {
                    // Explicit track — check template size
                    GridTrackSize* ts = grid_layout->grid_template_rows->tracks[tmpl_idx];
                    if (!ts) { all_definite = false; break; }
                    if (ts->type == GRID_TRACK_SIZE_LENGTH) {
                        definite_row_height += ts->value;
                    } else if (ts->type == GRID_TRACK_SIZE_PERCENTAGE &&
                               grid_layout->content_height > 0) {
                        definite_row_height += grid_layout->content_height *
                                               ts->value / 100.0f;
                    } else {
                        all_definite = false;
                        break;
                    }
                } else {
                    // Implicit track — not definite
                    all_definite = false;
                    break;
                }
            }
            // Add row gaps between spanned tracks
            int span = re - rs;
            if (span > 1 && grid_layout->row_gap > 0) {
                definite_row_height += (span - 1) * grid_layout->row_gap;
            }

            if (!all_definite || definite_row_height <= 0) continue;

            // Re-measure: compute block size (physical width) at the given inline size
            // (physical height) by walking the item's text children.
            float font_size = 16.0f;
            if (item->font) {
                font_size = item->font->font_size;
            } else if (lycon->font.style) {
                font_size = lycon->font.style->font_size;
            }

            // Set up font context for text measurement
            FontBox saved_font = lycon->font;
            bool font_changed = false;
            if (item->font && lycon->ui_context) {
                setup_font(lycon->ui_context, &lycon->font, item->font);
                font_changed = true;
            }

            float max_block_size = 0.0f;
            DomNode* child = item->first_child;
            while (child) {
                if (child->is_text()) {
                    const char* text = (const char*)child->text_data();
                    size_t text_len = text ? strlen(text) : 0;
                    if (text_len > 0) {
                        CssEnum text_transform = CSS_VALUE_NONE;
                        CssEnum font_variant = CSS_VALUE_NONE;
                        if (child->parent && child->parent->is_element()) {
                            text_transform = get_element_text_transform(
                                child->parent->as_element());
                            font_variant = get_element_font_variant(
                                child->parent->as_element());
                        }
                        TextIntrinsicWidths tw = measure_text_intrinsic_widths(
                            lycon, text, text_len, text_transform, font_variant);

                        // For vertical writing mode, horizontal text measurements
                        // approximate the inline-direction measurements (exact for
                        // monospaced/square fonts like Ahem).
                        // Compute number of vertical text "lines" at the available
                        // inline size (= definite row height).
                        float text_max_inline = tw.max_content;
                        float text_min_unit = tw.min_content;
                        float line_advance = font_size;

                        if (text_max_inline > definite_row_height && text_min_unit > 0) {
                            float eff = definite_row_height;
                            if (text_min_unit <= definite_row_height) {
                                int units = (int)(definite_row_height / text_min_unit); // INT_CAST_OK: intentional
                                if (units > 0) eff = units * text_min_unit;
                            } else {
                                eff = text_min_unit;
                            }
                            int num_lines = (int)ceilf(text_max_inline / eff); // INT_CAST_OK: integer line count
                            float w = num_lines * line_advance;
                            if (w > max_block_size) max_block_size = w;
                        } else {
                            // All text fits in one vertical line
                            if (line_advance > max_block_size)
                                max_block_size = line_advance;
                        }
                    }
                }
                child = child->next_sibling;
            }

            if (font_changed) lycon->font = saved_font;

            // Add padding and border in the block direction (horizontal)
            BoxMetrics item_box = layout_box_metrics(item);
            max_block_size += item_box.pad_border_h;

            if (max_block_size > 0) {
                log_debug("%s orthogonal item %s: row_height=%.1f -> width=%.1f (was min=%.1f max=%.1f)",
                          container->source_loc(), item->node_name(),
                          definite_row_height, max_block_size,
                          gi->measured_min_width, gi->measured_max_width);
                gi->measured_min_width = max_block_size;
                gi->measured_max_width = max_block_size;
            }
        }
    }

    // Phase 6: Resolve track sizes (using enhanced algorithm with intrinsic sizing)
    log_debug("%s DEBUG: Phase 6 - Resolving track sizes", container->source_loc());
    resolve_track_sizes_enhanced(grid_layout, container);

    // For shrink-to-fit containers, update container width based on resolved track sizes
    if (grid_layout->is_shrink_to_fit_width && grid_layout->computed_column_count > 0) {
        float total_column_width = grid_layout->content_width;

        // Add padding and border back to get container width
        float container_width = total_column_width + container_box.pad_border_h;

        container->width = container_width;
        grid_layout->container_width = (int)container->width; // INT_CAST_OK: grid container width
    }

    // Phase 6: Position grid items
    log_debug("%s DEBUG: Phase 6 - Positioning grid items", container->source_loc());
    position_grid_items(grid_layout, container, &lycon->scratch);

    // Phase 7: Align grid items
    log_debug("%s DEBUG: Phase 7 - Aligning grid items", container->source_loc());
    align_grid_items(grid_layout);

    // Phase 7.5: Apply relative/sticky positioning offsets to grid items
    log_debug("%s DEBUG: Phase 7.5 - Applying relative positioning offsets", container->source_loc());
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        if (!item || !item->position) continue;
        if (item->position->position == CSS_VALUE_STICKY) {
            layout_sticky_positioned(lycon, item);
            continue;
        }
        if (item->position->position != CSS_VALUE_RELATIVE) continue;
        float offset_x = 0, offset_y = 0;
        float parent_w = (float)container->width;
        float parent_h = (float)container->height;
        if (item->position->has_left) {
            offset_x = isnan(item->position->left_percent)
                ? item->position->left
                : item->position->left_percent * parent_w / 100.0f;
        } else if (item->position->has_right) {
            offset_x = isnan(item->position->right_percent)
                ? -item->position->right
                : -(item->position->right_percent * parent_w / 100.0f);
        }
        if (item->position->has_top) {
            offset_y = isnan(item->position->top_percent)
                ? item->position->top
                : item->position->top_percent * parent_h / 100.0f;
        } else if (item->position->has_bottom) {
            offset_y = isnan(item->position->bottom_percent)
                ? -item->position->bottom
                : -(item->position->bottom_percent * parent_h / 100.0f);
        }
        if (offset_x != 0 || offset_y != 0) {
            log_debug("%s Phase 7.5: grid item %d relative offset (%.0f, %.0f)", container->source_loc(), i, offset_x, offset_y);
            item->x += (int)offset_x; // INT_CAST_OK: grid item relative offset
            item->y += (int)offset_y; // INT_CAST_OK: grid item relative offset
        }
    }

    // Note: Phase 8 (content layout) is now handled by layout_grid_multipass.cpp Pass 3
    // The multipass flow calls layout_final_grid_content() after this function returns

    // Debug: Final item positions
    log_debug("%s DEBUG: FINAL GRID POSITIONS:", container->source_loc());
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        GridItemProp* gi = grid_item_prop(item);
        log_debug("%s FINAL_GRID_ITEM %d - pos: (%d,%d), size: %dx%d, grid_area: (%d-%d, %d-%d)", container->source_loc(),
            i, item->x, item->y, item->width, item->height,
            gi ? gi->computed_grid_row_start : 0,
            gi ? gi->computed_grid_row_end : 0,
            gi ? gi->computed_grid_column_start : 0,
            gi ? gi->computed_grid_column_end : 0);
    }
    log_debug("%s FINAL GRID POSITIONS:", container->source_loc());
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        log_debug("%s FINAL_GRID_ITEM %d - pos: (%d,%d), size: %dx%d", container->source_loc(),
                  i, item->x, item->y, item->width, item->height);
    }

    grid_layout->needs_reflow = false;
}

// Collect grid items from container children
int collect_grid_items(GridContainerLayout* grid_layout, ViewBlock* container, ViewBlock*** items) {
    log_debug("%s collect_grid_items called with container=%p, items=%p", container->source_loc(), container, items);
    if (!container || !items) {
        log_debug("%s Early return - container=%p, items=%p", container->source_loc(), container, items);
        return 0;
    }
    log_debug("%s grid=%p", container->source_loc(), grid_layout);
    if (!grid_layout) {
        log_debug("%s Early return - grid is NULL", container->source_loc());
        return 0;
    }

    int count = 0;

    // Count element children first - ONLY count element nodes, skip text nodes
    log_debug("%s About to access container->first_child", container->source_loc());
    DomNode* child_node = container->first_child;
    log_debug("%s first_child=%p", container->source_loc(), child_node);
    while (child_node) {
        // CRITICAL FIX: Only process element nodes, skip text nodes
        // CSS Grid §8.1: whitespace-only text in grid containers is not rendered
        if (!child_node->is_element()) {
            layout_suppress_ignorable_container_text(child_node);
            child_node = child_node->next_sibling;
            continue;
        }

        ViewBlock* child = lam::view_require_block(child_node);
        if (!layout_block_is_skipped_container_item(child)) {
            count++;
        }
        child_node = child_node->next_sibling;
    }

    log_debug("%s collect_grid_items: found %d element children", container->source_loc(), count);

    if (count == 0) {
        *items = nullptr;
        return 0;
    }

    // Ensure we have enough space in the grid items array
    if (count > grid_layout->allocated_items) {
        if (!lam::mem_grow_array(&grid_layout->grid_items, &grid_layout->allocated_items,
                                 count, 8, MEM_CAT_LAYOUT)) {
            // OOM/overflow: keep the old (still-valid) buffer and bail without collecting,
            // so the fill loop below cannot write past the allocation.
            *items = nullptr;
            return 0;
        }
    }

    // Collect items - ONLY collect element nodes, skip text nodes
    count = 0;
    child_node = container->first_child;
    while (child_node) {
        // CRITICAL FIX: Only process element nodes, skip text nodes
        if (!child_node->is_element()) {
            child_node = child_node->next_sibling;
            continue;
        }

        ViewBlock* child = lam::view_require_block(child_node);
        if (!layout_block_is_skipped_container_item(child)) {
            grid_layout->grid_items[count] = child;

            // Initialize grid item placement properties with defaults if not set
            // Note: Only initialize placement-related properties (row/column),
            // NOT alignment properties (justify_self/align_self_grid) which may be set via CSS
            GridItemProp* child_gi = grid_item_prop(child);
            bool has_explicit_placement = child_gi && (
                child_gi->grid_row_start != 0 || child_gi->grid_row_end != 0 ||
                child_gi->grid_column_start != 0 || child_gi->grid_column_end != 0);
            if (!has_explicit_placement && child_gi) {
                // Mark as auto-placed but preserve any CSS-set alignment properties
                child_gi->is_grid_auto_placed = true;
            }

            count++;
        }
        child_node = child_node->next_sibling;
    }

    grid_layout->item_count = count;

    // Sort items by CSS order property (stable sort - preserve DOM order for equal orders)
    // CSS Grid spec: items are placed in order-modified document order
    if (count > 1) {
        // Simple insertion sort (stable, good for small arrays)
        for (int i = 1; i < count; i++) {
            ViewBlock* key = grid_layout->grid_items[i];
            GridItemProp* key_gi = grid_item_prop(key);
            int key_order = key_gi ? key_gi->order : 0;
            int j = i - 1;
            while (j >= 0) {
                GridItemProp* j_gi = grid_item_prop(grid_layout->grid_items[j]);
                int j_order = j_gi ? j_gi->order : 0;
                if (j_order > key_order) {
                    grid_layout->grid_items[j + 1] = grid_layout->grid_items[j];
                    j--;
                } else {
                    break;
                }
            }
            grid_layout->grid_items[j + 1] = key;
        }
    }

    *items = grid_layout->grid_items;
    return count;
}

// Determine the size of the grid
void determine_grid_size(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug(" Determining grid size\n");
    log_debug("Determining grid size\n");

    // Count explicit tracks from template
    grid_layout->explicit_row_count = grid_layout->grid_template_rows ?
                                     grid_layout->grid_template_rows->track_count : 0;
    grid_layout->explicit_column_count = grid_layout->grid_template_columns ?
                                        grid_layout->grid_template_columns->track_count : 0;

    log_debug(" Explicit tracks - rows: %d, columns: %d\n",
           grid_layout->explicit_row_count, grid_layout->explicit_column_count);

    // Find maximum implicit tracks needed based on item placement
    int max_row = grid_layout->explicit_row_count;
    int max_column = grid_layout->explicit_column_count;

    log_debug(" Checking %d items for grid size requirements\n", grid_layout->item_count);
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        GridItemProp* gi = grid_item_prop(item);
        if (!gi) continue;  // Skip items without grid item properties
        log_debug(" Item %d placement - row: %d-%d, col: %d-%d\n",
               i, gi->computed_grid_row_start, gi->computed_grid_row_end,
               gi->computed_grid_column_start, gi->computed_grid_column_end);

        // CRITICAL FIX: Grid positions are 1-indexed, but we need the actual track count
        // If an item ends at position 2, it uses tracks 0 and 1 (2 tracks total)
        max_row = fmax(max_row, gi->computed_grid_row_end - 1);
        max_column = fmax(max_column, gi->computed_grid_column_end - 1);
    }

    // Ensure minimum grid size matches explicit template
    if (max_row < grid_layout->explicit_row_count) max_row = grid_layout->explicit_row_count;
    if (max_column < grid_layout->explicit_column_count) max_column = grid_layout->explicit_column_count;

    grid_layout->implicit_row_count = max_row - grid_layout->explicit_row_count;
    grid_layout->implicit_column_count = max_column - grid_layout->explicit_column_count;

    // Ensure non-negative implicit counts
    if (grid_layout->implicit_row_count < 0) grid_layout->implicit_row_count = 0;
    if (grid_layout->implicit_column_count < 0) grid_layout->implicit_column_count = 0;

    grid_layout->computed_row_count = max_row;
    grid_layout->computed_column_count = max_column;

    log_debug(" Final grid size - rows: %d (%d explicit + %d implicit), cols: %d (%d explicit + %d implicit)\n",
           grid_layout->computed_row_count, grid_layout->explicit_row_count, grid_layout->implicit_row_count,
           grid_layout->computed_column_count, grid_layout->explicit_column_count, grid_layout->implicit_column_count);

    log_debug("Grid size determined - rows: %d (%d explicit + %d implicit), cols: %d (%d explicit + %d implicit)\n",
              grid_layout->computed_row_count, grid_layout->explicit_row_count, grid_layout->implicit_row_count,
              grid_layout->computed_column_count, grid_layout->explicit_column_count, grid_layout->implicit_column_count);
}

// Calculate minimum size of a track pattern for auto-fill/auto-fit expansion
static int calculate_track_pattern_min_size(GridTrackSize** tracks, int track_count) {
    int pattern_size = 0;
    for (int i = 0; i < track_count; i++) {
        GridTrackSize* ts = tracks[i];
        if (!ts) continue;

        if (ts->type == GRID_TRACK_SIZE_LENGTH) {
            pattern_size += ts->value;
        } else if (ts->type == GRID_TRACK_SIZE_MINMAX && ts->min_size) {
            // Use the min value from minmax()
            if (ts->min_size->type == GRID_TRACK_SIZE_LENGTH) {
                pattern_size += ts->min_size->value;
            } else {
                pattern_size += 100; // Default for auto/min-content/max-content
            }
        } else if (ts->type == GRID_TRACK_SIZE_FR ||
                   ts->type == GRID_TRACK_SIZE_AUTO) {
            pattern_size += 100; // Default minimum for flexible/auto tracks
        } else {
            pattern_size += 50; // Fallback
        }
    }
    return pattern_size;
}

static void destroy_grid_track_entries(GridTrackSize** tracks, int track_count) {
    if (!tracks) return;
    for (int i = 0; i < track_count; i++) {
        destroy_grid_track_size(tracks[i]);
        tracks[i] = nullptr;
    }
}

static void free_grid_line_name_slots(GridTrackList* list) {
    if (!list || !list->line_names) return;
    int slot_count = list->allocated_tracks + 1;
    for (int i = 0; i < slot_count; i++) {
        mem_free(list->line_names[i]);
        list->line_names[i] = nullptr;
    }
    mem_free(list->line_names);
    list->line_names = nullptr;
    list->line_name_count = 0;
}

static bool append_cloned_grid_track(GridTrackSize** tracks, int* dest, GridTrackSize* source) {
    if (!tracks || !dest || !source) return false;
    GridTrackSize* copy = clone_grid_track_size(source);
    if (!copy) return false;
    tracks[*dest] = copy;
    (*dest)++;
    return true;
}

// CSS Grid §7.2.3.2: Collapse empty auto-fit tracks after item placement.
// Empty auto-fit tracks are treated as having a fixed track sizing function of 0px,
// and their gutters are also collapsed.
void collapse_empty_auto_fit_tracks(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    // Process columns
    if (grid_layout->auto_fit_col_count > 0 && grid_layout->grid_template_columns) {
        GridTrackList* cols = grid_layout->grid_template_columns;
        // Build occupancy bitmap: which column indices (0-based) have items
        bool col_occupied[64] = {};
        for (int idx = 0; idx < grid_layout->item_count; idx++) {
            ViewBlock* item = grid_layout->grid_items[idx];
            GridItemProp* gi = grid_item_prop(item);
            if (!item || !gi) continue;
            // computed positions are 1-based line numbers
            int cs = gi->computed_grid_column_start - 1;
            int ce = gi->computed_grid_column_end - 1;
            for (int c = cs; c < ce && c < 64; c++) {
                if (c >= 0) col_occupied[c] = true;
            }
        }
        // Collapse unoccupied auto-fit tracks by setting their size to 0px
        for (int c = 0; c < cols->track_count && c < 64; c++) {
            if (c < grid_layout->auto_fit_col_count &&
                grid_layout->auto_fit_columns[c] && !col_occupied[c]) {
                // Replace with a 0px fixed track
                GridTrackSize* zero_track = create_grid_track_size(GRID_TRACK_SIZE_LENGTH, 0);
                if (!zero_track) continue;
                destroy_grid_track_size(cols->tracks[c]);
                cols->tracks[c] = zero_track;
                log_debug("GRID: auto-fit collapse column %d (empty)", c);
            }
        }
    }

    // Process rows
    if (grid_layout->auto_fit_row_count > 0 && grid_layout->grid_template_rows) {
        GridTrackList* rows = grid_layout->grid_template_rows;
        bool row_occupied[64] = {};
        for (int idx = 0; idx < grid_layout->item_count; idx++) {
            ViewBlock* item = grid_layout->grid_items[idx];
            GridItemProp* gi = grid_item_prop(item);
            if (!item || !gi) continue;
            int rs = gi->computed_grid_row_start - 1;
            int re = gi->computed_grid_row_end - 1;
            for (int r = rs; r < re && r < 64; r++) {
                if (r >= 0) row_occupied[r] = true;
            }
        }
        for (int r = 0; r < rows->track_count && r < 64; r++) {
            if (r < grid_layout->auto_fit_row_count &&
                grid_layout->auto_fit_rows[r] && !row_occupied[r]) {
                GridTrackSize* zero_track = create_grid_track_size(GRID_TRACK_SIZE_LENGTH, 0);
                if (!zero_track) continue;
                destroy_grid_track_size(rows->tracks[r]);
                rows->tracks[r] = zero_track;
                log_debug("GRID: auto-fit collapse row %d (empty)", r);
            }
        }
    }
}

static GridTrackSize** expand_repeat_track_entries(GridTrackList* tracks, int repeat_index,
                                                   GridTrackSize* repeat, int repeat_count,
                                                   int new_track_count, int* repeat_start,
                                                   int* repeat_end) {
    GridTrackSize** expanded = (GridTrackSize**)mem_calloc(
        new_track_count, sizeof(GridTrackSize*), MEM_CAT_LAYOUT);
    if (!expanded) return nullptr;

    int dest = 0;
    for (int i = 0; i < repeat_index; i++) {
        if (!append_cloned_grid_track(expanded, &dest, tracks->tracks[i])) goto fail;
    }
    *repeat_start = dest;
    for (int i = 0; i < repeat_count; i++) {
        for (int j = 0; j < repeat->repeat_track_count; j++) {
            if (!append_cloned_grid_track(expanded, &dest, repeat->repeat_tracks[j])) goto fail;
        }
    }
    *repeat_end = dest;
    for (int i = repeat_index + 1; i < tracks->track_count; i++) {
        if (!append_cloned_grid_track(expanded, &dest, tracks->tracks[i])) goto fail;
    }
    return expanded;

fail:
    destroy_grid_track_entries(expanded, dest);
    mem_free(expanded);
    return nullptr;
}

static bool expand_auto_repeat_axis(GridContainerLayout* grid_layout, bool is_column) {
    GridTrackList** list_slot = is_column ? &grid_layout->grid_template_columns
                                         : &grid_layout->grid_template_rows;
    bool* owns_list = is_column ? &grid_layout->owns_template_columns
                                : &grid_layout->owns_template_rows;
    bool* auto_fit_tracks = is_column ? grid_layout->auto_fit_columns
                                      : grid_layout->auto_fit_rows;
    int* auto_fit_count = is_column ? &grid_layout->auto_fit_col_count
                                    : &grid_layout->auto_fit_row_count;
    GridTrackList* tracks = *list_slot;
    if (!tracks || tracks->track_count <= 0) return true;

    const char* axis = is_column ? "column" : "row";
    const char* available_axis = is_column ? "width" : "height";
    float available_size = is_column ? grid_layout->content_width : grid_layout->content_height;
    float gap = is_column ? grid_layout->column_gap : grid_layout->row_gap;

    for (int repeat_index = 0; repeat_index < tracks->track_count; repeat_index++) {
        GridTrackSize* repeat = tracks->tracks[repeat_index];
        if (!repeat || repeat->type != GRID_TRACK_SIZE_REPEAT ||
            (!repeat->is_auto_fill && !repeat->is_auto_fit)) continue;

        log_debug("GRID: Expanding auto-%s %ss (available %s: %.0f, item_count: %d)",
                  repeat->is_auto_fill ? "fill" : "fit", axis, available_axis,
                  available_size, grid_layout->item_count);

        int pattern_size = calculate_track_pattern_min_size(
            repeat->repeat_tracks, repeat->repeat_track_count);
        if (pattern_size <= 0) pattern_size = 100;

        int fixed_track_space = 0;
        int fixed_track_count = 0;
        for (int i = 0; i < tracks->track_count; i++) {
            if (i == repeat_index) continue;
            GridTrackSize* other = tracks->tracks[i];
            if (other && other->type == GRID_TRACK_SIZE_LENGTH) {
                fixed_track_space += other->value;
                fixed_track_count++;
            }
        }
        int gap_for_fixed = fixed_track_count > 0
            ? (int)(fixed_track_count * gap) : 0; // INT_CAST_OK: grid track count needs an integral gap budget.
        int available = (int)available_size - fixed_track_space - gap_for_fixed; // INT_CAST_OK: auto-repeat count is discrete.
        int repeat_count = 1;
        if (pattern_size + gap > 0.0f) {
            repeat_count = (int)((available + gap) / (pattern_size + gap)); // INT_CAST_OK: CSS repeat count is integral.
            if (repeat_count < 1) repeat_count = 1;
        }
        log_debug("GRID: %s pattern size=%d, gap=%.1f, available=%d -> %d repetitions",
                  axis, pattern_size, gap, available, repeat_count);

        // Preserve the existing column-only cap until empty auto-fit gutters collapse fully.
        if (is_column && repeat->is_auto_fit && grid_layout->item_count > 0 &&
            repeat_count > grid_layout->item_count) {
            repeat_count = grid_layout->item_count;
            log_debug("GRID: auto-fit adjusted repeat_count to %d (item_count)", repeat_count);
        }

        long long expanded = (long long)repeat_count * (long long)repeat->repeat_track_count;
        long long total_tracks = (long long)tracks->track_count - 1 + expanded;
        if (total_tracks <= 0 || total_tracks != (long long)(int)total_tracks) return false;
        int new_track_count = (int)total_tracks; // INT_CAST_OK: range equality above proves the count fits.
        int auto_fit_start = 0;
        int auto_fit_end = 0;
        GridTrackSize** new_tracks = expand_repeat_track_entries(
            tracks, repeat_index, repeat, repeat_count, new_track_count,
            &auto_fit_start, &auto_fit_end);
        if (!new_tracks) return false;

        if (repeat->is_auto_fit && new_track_count <= 64) {
            for (int i = 0; i < new_track_count; i++) {
                auto_fit_tracks[i] = i >= auto_fit_start && i < auto_fit_end;
            }
            *auto_fit_count = new_track_count;
        }

        if (*owns_list) {
            destroy_grid_track_entries(tracks->tracks, tracks->track_count);
            mem_free(tracks->tracks);
            tracks->tracks = new_tracks;
            tracks->track_count = new_track_count;
            tracks->allocated_tracks = new_track_count;
            tracks->is_repeat = false;
            free_grid_line_name_slots(tracks);
            tracks->line_names = (char**)mem_calloc(
                new_track_count + 1, sizeof(char*), MEM_CAT_LAYOUT);
        } else {
            GridTrackList* expanded_list = create_grid_track_list(new_track_count);
            if (!expanded_list) {
                destroy_grid_track_entries(new_tracks, new_track_count);
                mem_free(new_tracks);
                return false;
            }
            mem_free(expanded_list->tracks);
            expanded_list->tracks = new_tracks;
            expanded_list->track_count = new_track_count;
            expanded_list->allocated_tracks = new_track_count;
            expanded_list->is_repeat = false;
            *list_slot = expanded_list;
            *owns_list = true;
        }

        log_debug("GRID: Expanded to %d %s tracks", new_track_count, axis);
        break; // CSS permits only one auto-repeat per axis.
    }
    return true;
}

// Expand auto-fill/auto-fit repeat() tracks based on available space.
void expand_auto_repeat_tracks(GridContainerLayout* grid_layout) {
    if (!grid_layout || !expand_auto_repeat_axis(grid_layout, true)) return;
    expand_auto_repeat_axis(grid_layout, false);
}
