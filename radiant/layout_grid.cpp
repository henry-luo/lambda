#include "layout.hpp"
#include "view.hpp"
#include "grid_enhanced_adapter.hpp"  // Enhanced grid integration
#include "../lib/tagged.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
}
// Forward declarations
void expand_auto_repeat_tracks(GridContainerLayout* grid_layout);

char* grid_scratch_strdup(ScratchArena* scratch, const char* source) {
    if (!scratch || !source) return NULL;
    size_t length = strlen(source);
    char* copy = (char*)scratch_alloc(scratch, length + 1);
    if (copy) memcpy(copy, source, length + 1);
    return copy;
}

static GridTrackSize* grid_scratch_clone_track(ScratchArena* scratch,
                                               const GridTrackSize* source) {
    if (!scratch || !source) return NULL;
    GridTrackSize* copy = (GridTrackSize*)scratch_calloc(scratch, sizeof(GridTrackSize));
    if (!copy) return NULL;
    *copy = *source;
    copy->min_size = NULL;
    copy->max_size = NULL;
    copy->repeat_tracks = NULL;
    copy->repeat_track_count = 0;

    LayoutAxisPair<GridTrackSize**> nested = {&copy->min_size, &copy->max_size};
    LayoutAxisPair<GridTrackSize*> source_nested = {source->min_size, source->max_size};
    for (LayoutAxis axis : layout_axes()) {
        if (source_nested[axis]) {
            *nested[axis] = grid_scratch_clone_track(scratch, source_nested[axis]);
            if (!*nested[axis]) return NULL;
        }
    }
    if (source->repeat_tracks && source->repeat_track_count > 0) {
        copy->repeat_tracks = (GridTrackSize**)scratch_calloc(
            scratch, (size_t)source->repeat_track_count * sizeof(GridTrackSize*));
        if (!copy->repeat_tracks) return NULL;
        copy->repeat_track_count = source->repeat_track_count;
        for (int i = 0; i < source->repeat_track_count; i++) {
            copy->repeat_tracks[i] = grid_scratch_clone_track(scratch, source->repeat_tracks[i]);
            if (!copy->repeat_tracks[i]) return NULL;
        }
    }
    return copy;
}

static GridTrackList* grid_scratch_clone_track_list(ScratchArena* scratch,
                                                    const GridTrackList* source,
                                                    int default_capacity) {
    if (!scratch) return NULL;
    int capacity = source && source->allocated_tracks > source->track_count
        ? source->allocated_tracks : (source ? source->track_count : default_capacity);
    if (capacity < default_capacity) capacity = default_capacity;
    GridTrackList* copy = (GridTrackList*)scratch_calloc(scratch, sizeof(GridTrackList));
    if (!copy) return NULL;
    copy->allocated_tracks = capacity;
    copy->tracks = (GridTrackSize**)scratch_calloc(
        scratch, (size_t)capacity * sizeof(GridTrackSize*));
    copy->line_names = (char**)scratch_calloc(
        scratch, (size_t)(capacity + 1) * sizeof(char*));
    if (!copy->tracks || !copy->line_names) return NULL;
    if (!source) {
        copy->repeat_count = 1;
        return copy;
    }

    copy->track_count = source->track_count;
    copy->line_name_count = source->line_name_count;
    copy->is_repeat = source->is_repeat;
    copy->repeat_count = source->repeat_count;
    for (int i = 0; i < source->track_count; i++) {
        copy->tracks[i] = grid_scratch_clone_track(scratch, source->tracks[i]);
        if (source->tracks[i] && !copy->tracks[i]) return NULL;
    }
    int source_line_slots = source->allocated_tracks + 1;
    int copy_line_slots = capacity + 1;
    int slots = source_line_slots < copy_line_slots ? source_line_slots : copy_line_slots;
    for (int i = 0; i < slots; i++) {
        if (!source->line_names || !source->line_names[i]) continue;
        copy->line_names[i] = grid_scratch_strdup(scratch, source->line_names[i]);
        if (!copy->line_names[i]) return NULL;
    }
    return copy;
}

float layout_grid_row_border_box_extent(ViewBlock* container,
                                        GridContainerLayout* grid_layout) {
    if (!container || !grid_layout || !grid_layout->computed_rows ||
        grid_layout->computed_row_count <= 0) {
        return 0.0f;
    }
    float content_extent = 0.0f;
    for (int i = 0; i < grid_layout->computed_row_count; i++) {
        content_extent += (*grid_layout->computed_rows)[i].base_size;
    }
    content_extent += grid_layout->row_gap * (grid_layout->computed_row_count - 1);
    if (grid_layout->row_intrinsic_height > 0.0f) {
        content_extent = grid_layout->row_intrinsic_height;
    }
    return content_extent + layout_axis_box_metrics(container, LAYOUT_AXIS_Y).pad_border;
}
// Initialize grid container layout state
void init_grid_container(LayoutContext* lycon, ViewBlock* container) {
    if (!lycon || !container) return;

    ScratchMark mark = scratch_mark(&lycon->scratch);
    GridContainerLayout* grid = (GridContainerLayout*)scratch_calloc(&lycon->scratch, sizeof(GridContainerLayout));
    if (!grid) {
        log_error("layout_grid: unable to allocate grid container scratch state for %s",
                  container->source_loc());
        return;
    }
    lycon->grid_container = grid;
    grid->scratch_mark = mark;
    grid->lycon = lycon;  // Store layout context for intrinsic sizing
    if (container->embed && container->embedp()->grid) {
        memcpy(grid, container->embedp()->grid, sizeof(GridProp));
        grid->scratch_mark = mark;
        grid->lycon = lycon;  // Restore after memcpy
    } else {
        // Set default values using enum names that align with Lexbor constants
        grid->justify_content = CSS_VALUE_START;
        grid->align_content = CSS_VALUE_START;
        // Keep the CSS initial normal value so aspect-ratio items can tell it
        // apart from an explicitly requested stretch during self-alignment.
        grid->justify_items = CSS_VALUE_NORMAL;
        grid->justify_items_detail.value = CSS_VALUE_NORMAL;
        grid->align_items = CSS_VALUE_NORMAL;
        grid->grid_auto_flow = CSS_VALUE_ROW;
        // Initialize gaps
        grid->row_gap = 0;
        grid->column_gap = 0;
    }
    // Pass-local mutations must never alias the persistent CSS GridProp graph;
    // clone every area and name into this container's scratch lifetime.
    GridArea* source_areas = grid->grid_areas;
    int source_area_count = grid->area_count;
    grid->allocated_areas = source_area_count > 4 ? source_area_count : 4;
    grid->grid_areas = (GridArea*)scratch_calloc(&lycon->scratch,
        (size_t)grid->allocated_areas * sizeof(GridArea));
    if (!grid->grid_areas) {
        cleanup_grid_container(lycon);
        return;
    }
    for (int i = 0; i < source_area_count; i++) {
        grid->grid_areas[i] = source_areas[i];
        if (source_areas[i].name) {
            grid->grid_areas[i].name = grid_scratch_strdup(&lycon->scratch,
                                                           source_areas[i].name);
            if (!grid->grid_areas[i].name) {
                cleanup_grid_container(lycon);
                return;
            }
        }
    }
    GridTrackList** track_lists[4] = {
        &grid->grid_template_rows, &grid->grid_template_columns,
        &grid->grid_auto_rows, &grid->grid_auto_columns};
    GridTrackList* source_lists[4] = {
        grid->grid_template_rows, grid->grid_template_columns,
        grid->grid_auto_rows, grid->grid_auto_columns};
    const int default_capacities[4] = {4, 4, 2, 2};
    for (int i = 0; i < 4; i++) {
        *track_lists[i] = grid_scratch_clone_track_list(
            &lycon->scratch, source_lists[i], default_capacities[i]);
    }
    if (!grid->grid_template_rows || !grid->grid_template_columns ||
        !grid->grid_auto_rows || !grid->grid_auto_columns) {
        cleanup_grid_container(lycon);
        return;
    }
    // Immediate children bound the pass-local grid item array; scratch arrays are
    // released by the container mark instead of individual heap frees.
    int item_capacity = layout_count_potential_items(container, false);
    grid->allocated_items = item_capacity;
    if (item_capacity > 0) {
        grid->grid_items = (ViewBlock**)scratch_calloc(&lycon->scratch,
            (size_t)item_capacity * sizeof(ViewBlock*));
        if (!grid->grid_items) {
            log_error("layout_grid: unable to allocate %d grid item slots for %s",
                      item_capacity, container->source_loc());
            cleanup_grid_container(lycon);
            return;
        }
    }

    int line_capacity = 8 + grid->area_count * 4;
    if (grid->grid_template_columns) line_capacity += grid->grid_template_columns->line_name_count;
    if (grid->grid_template_rows) line_capacity += grid->grid_template_rows->line_name_count;
    grid->allocated_line_names = line_capacity;
    grid->line_names = (GridLineName*)scratch_calloc(&lycon->scratch,
        (size_t)line_capacity * sizeof(GridLineName));
    if (!grid->line_names) {
        log_error("layout_grid: unable to allocate %d grid line-name slots for %s",
                  line_capacity, container->source_loc());
        cleanup_grid_container(lycon);
        return;
    }

}
// Cleanup grid container resources
void cleanup_grid_container(LayoutContext* lycon) {
    if (!lycon || !lycon->grid_container) return;
    GridContainerLayout* grid = lycon->grid_container;

    ScratchMark mark = grid->scratch_mark;
    lycon->grid_container = NULL;
    scratch_restore(&lycon->scratch, mark);
}

GridLayoutScope::GridLayoutScope(LayoutContext* l, ViewBlock* container)
    : lycon(l), saved(l ? l->grid_container : NULL), active(l != NULL) {
    if (lycon && container) {
        init_grid_container(lycon, container);
    }
}

GridLayoutScope::~GridLayoutScope() {
    close();
}

void GridLayoutScope::close() {
    if (!active) return;
    if (lycon->grid_container && lycon->grid_container != saved) {
        cleanup_grid_container(lycon);
    }
    lycon->grid_container = saved;
    active = false;
}
// Main grid layout algorithm entry point
void layout_grid_container(LayoutContext* lycon, ViewBlock* container) {
    // validate before diagnostics; a null container cannot provide a source location.
    if (!container) return;
    // Check if this is actually a grid container by display type
    // Note: embed->grid may be NULL if grid-template-* properties weren't resolved,
    // but we can still run grid layout with auto-placement
    if (container->display.inner != CSS_VALUE_GRID) {
        return;
    }

    GridContainerLayout* grid_layout = lycon->grid_container;
    // Check if container is shrink-to-fit (absolutely positioned with no explicit width,
    // or inline-grid which uses shrink-to-fit sizing)
    bool is_shrink_to_fit_width = layout_is_shrink_to_fit_width(container);
    grid_layout->is_shrink_to_fit_width = is_shrink_to_fit_width;
    // An intrinsic width keyword constrains track sizing itself; treating its
    // post-layout used width as definite incorrectly lets auto tracks consume free space.
    bool contain_size_has_inline_fallback =
        layout_block_has_size_containment_in_axis(container, true) &&
        container->block()->contain_intrinsic_width >= 0.0f;
    // Size containment resolves an intrinsic width keyword to its intrinsic-size
    // fallback before grid sizing; tracks then use that definite available width.
    grid_layout->is_min_content_width = !contain_size_has_inline_fallback &&
        (lycon->available_space.is_width_min_content() ||
         (container->blk && container->block()->given_width_type == CSS_VALUE_MIN_CONTENT));
    grid_layout->is_max_content_width = !contain_size_has_inline_fallback &&
        (lycon->available_space.is_width_max_content() ||
         (container->blk && container->block()->given_width_type == CSS_VALUE_MAX_CONTENT));
    grid_layout->row_intrinsic_height = -1.0f;
    // Set container dimensions
    grid_layout->container_width = container->width;
    grid_layout->container_height = container->height;
    // Determine if container has an explicit height (not auto)
    // This affects whether auto row tracks should stretch to fill the container
    grid_layout->has_explicit_height = (container->blk && container->block_mut()->given_height >= 0);
    // Calculate content dimensions (excluding borders and padding)
    LayoutContentBox container_content = layout_content_box(container);
    grid_layout->content_width = container_content.width;
    grid_layout->content_height = container_content.height;
    // Resolve percentage gaps against the container dimensions.
    // For definite containers, resolve immediately. For indefinite (shrink-to-fit),
    // the enhanced adapter handles the two-pass resolution.
    LayoutAxisPair<float*> gaps = {&grid_layout->column_gap, &grid_layout->row_gap};
    LayoutAxisPair<bool*> gap_is_percent = {
        &grid_layout->column_gap_is_percent, &grid_layout->row_gap_is_percent
    };
    LayoutAxisPair<float> content_sizes = {
        grid_layout->content_width, grid_layout->content_height
    };
    LayoutAxisPair<bool> definite_gap_base = {
        !is_shrink_to_fit_width, grid_layout->has_explicit_height
    };
    for (LayoutAxis axis : layout_axes()) {
        if (*gap_is_percent[axis] && definite_gap_base[axis]) {
            *gaps[axis] = content_sizes[axis] * (*gaps[axis] / 100.0f);
            *gap_is_percent[axis] = false;
        }
    }
    // Phase 1: Collect grid items (need count for auto-fit)
    ViewBlock** items;
    int item_count = collect_grid_items(grid_layout, container, &items);
    // Expand auto-fill/auto-fit repeat() tracks now that we know content_width and item_count
    expand_auto_repeat_tracks(grid_layout);

    if (item_count == 0) {
        // Even with no items, we need to resolve explicit track sizes so that:
        // 1. Absolutely positioned children can use grid-line positions for their containing block.
        // 2. The container gets the correct height from explicit grid-template-rows + padding.
        determine_grid_size(grid_layout);
        resolve_track_sizes_enhanced(grid_layout, container);
        // Update container height from explicit row heights + padding (mirroring the shrink-to-fit path)
        if (!grid_layout->has_explicit_height && grid_layout->computed_row_count > 0) {
            float new_h = layout_grid_row_border_box_extent(container, grid_layout);
            if (new_h > (float)container->height) {
                container->height = new_h;
            }
        }
        return;
    }
    // Phase 2: Resolve grid template areas
    // Phase 2.5: Register named grid lines from track lists and template areas
    LayoutAxisPair<GridTrackList*> template_tracks = {
        grid_layout->grid_template_columns, grid_layout->grid_template_rows
    };
    for (LayoutAxis axis : layout_axes()) {
        GridTrackList* tracks = template_tracks[axis];
        if (!tracks) continue;
        for (int i = 0; i <= tracks->track_count && i < tracks->allocated_tracks + 1; i++) {
            if (tracks->line_names[i]) {
                add_grid_line_name(grid_layout, tracks->line_names[i], i + 1,
                                   axis == LAYOUT_AXIS_Y);
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
    determine_grid_size(grid_layout);
    // Phase 3.5: Resolve named line references into integer line numbers
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
            WritingMode wm = layout_block_writing_mode(item);
            is_orthogonal = wm == WM_VERTICAL_LR || wm == WM_VERTICAL_RL;
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
                font_size = item->fontp()->font_size;
            } else if (lycon->font.style) {
                font_size = lycon->font.style->font_size;
            }
            // Set up font context for text measurement
            LayoutFontScope font_scope(lycon);
            if (item->font && lycon->ui_context) {
                setup_font(lycon->ui_context, &lycon->font, item->font);
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
            // Add padding and border in the block direction (horizontal)
            BoxMetrics item_box = layout_box_metrics(item);
            max_block_size += item_box.pad_border_h;

            if (max_block_size > 0) {
                gi->measured_min_width = max_block_size;
                gi->measured_max_width = max_block_size;
            }
        }
    }
    // Phase 6: Resolve track sizes (using enhanced algorithm with intrinsic sizing)
    resolve_track_sizes_enhanced(grid_layout, container);
    // An intrinsic width constraint is resolved by the track-sizing result, just like
    // shrink-to-fit; retaining the provisional width makes auto tracks visibly too wide.
    if ((grid_layout->is_shrink_to_fit_width || grid_layout->is_min_content_width ||
         grid_layout->is_max_content_width) && grid_layout->computed_column_count > 0) {
        float total_column_width = grid_layout->content_width;

        container->width = layout_border_size_from_content_box(
            container, total_column_width, true);
        grid_layout->container_width = container->width;
    }
    // Phase 6: Position grid items
    position_grid_items(grid_layout, container, &lycon->scratch);
    // Phase 7: Align grid items
    align_grid_items(grid_layout);
    // Phase 7.5: Apply relative/sticky positioning offsets to grid items
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        if (!item || !item->position) continue;
        if (item->positionp()->position == CSS_VALUE_STICKY) {
            layout_sticky_positioned(lycon, item);
            continue;
        }
        if (item->positionp()->position != CSS_VALUE_RELATIVE) continue;
        LayoutAxisPair<float> containing_sizes = layout_axis_pair(
            container->width, container->height);
        for (LayoutAxis axis : layout_axes()) {
            LayoutAxisRefs refs(item, axis);
            float offset = layout_relative_axis_offset(
                item, layout_axis_is_horizontal(axis), containing_sizes[axis]);
            refs.set_position(refs.get_position() + offset);
        }
    }
    // Note: Phase 8 (content layout) is now handled by layout_grid_multipass.cpp Pass 3
    // The multipass flow calls layout_final_grid_content() after this function returns

}
// Collect the flattened-tree element nodes that can become grid items.
int collect_grid_item_nodes(LayoutContext* lycon, ViewBlock* container,
                            DomNode* first_child, DomNode** nodes, int capacity,
                            bool initialize_contents) {
    if (!container || !nodes || capacity <= 0) return 0;

    int count = 0;
    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;

        DomElement* elem = child->as_element();
        DisplayValue display = resolve_display_value(child);
        if (layout_display_is_none(display)) {
            elem->view_type = RDT_VIEW_NONE;
            continue;
        }
        if (display.outer == CSS_VALUE_CONTENTS) {
            // CSS Display 3: retain the DOM node as a boxless view while its
            // descendants participate directly in the grid formatting context.
            if (initialize_contents) {
                layout_init_display_contents_view(lycon, elem);
            }
            count += collect_grid_item_nodes(
                lycon, container, elem->first_child, nodes + count,
                capacity - count, initialize_contents);
            continue;
        }
        if (count < capacity) nodes[count++] = child;
    }
    return count;
}

// Collect grid items from the flattened container children.
int collect_grid_items(GridContainerLayout* grid_layout, ViewBlock* container, ViewBlock*** items) {
    // validate before diagnostics; this helper is also called by empty-grid paths.
    if (!container || !items || !grid_layout) return 0;

    int node_capacity = grid_layout->allocated_items > 0
        ? grid_layout->allocated_items : 0;
    DomNode** nodes = node_capacity > 0
        ? (DomNode**)scratch_calloc(&grid_layout->lycon->scratch,
            (size_t)node_capacity * sizeof(DomNode*)) : nullptr;
    int node_count = nodes
        ? collect_grid_item_nodes(grid_layout->lycon, container,
            container->first_child, nodes, node_capacity, false) : 0;
    int count = 0;
    for (int node_index = 0; node_index < node_count; node_index++) {
        DomNode* child_node = nodes[node_index];
        ViewBlock* child = lam::view_require_block(child_node);
        if (layout_block_is_skipped_container_item(child)) {
            continue;
        }
        // layout_count_potential_items supplies this scratch capacity; keep the
        // guard here because malformed DOM trees must not overrun the pass array.
        if (count >= grid_layout->allocated_items || !grid_layout->grid_items) {
            *items = nullptr;
            grid_layout->item_count = 0;
            return 0;
        }
        grid_layout->grid_items[count++] = child;
        GridItemProp* child_gi = grid_item_prop(child);
        bool has_explicit_placement = child_gi && (
            child_gi->grid_row_start != 0 || child_gi->grid_row_end != 0 ||
            child_gi->grid_column_start != 0 || child_gi->grid_column_end != 0);
        if (child_gi && !has_explicit_placement) {
            child_gi->is_grid_auto_placed = true;
        }
    }

    if (nodes) scratch_free(&grid_layout->lycon->scratch, nodes);

    if (count == 0) {
        *items = nullptr;
        return 0;
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
    // Count explicit tracks from template
    grid_layout->explicit_row_count = grid_layout->grid_template_rows ?
                                     grid_layout->grid_template_rows->track_count : 0;
    grid_layout->explicit_column_count = grid_layout->grid_template_columns ?
                                        grid_layout->grid_template_columns->track_count : 0;
    // Find maximum implicit tracks needed based on item placement
    int max_row = grid_layout->explicit_row_count;
    int max_column = grid_layout->explicit_column_count;

    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        GridItemProp* gi = grid_item_prop(item);
        if (!gi) continue;  // Skip items without grid item properties
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

static bool append_cloned_grid_track(ScratchArena* scratch, GridTrackSize** tracks,
                                     int* dest, GridTrackSize* source) {
    if (!scratch || !tracks || !dest || !source) return false;
    GridTrackSize* copy = grid_scratch_clone_track(scratch, source);
    if (!copy) return false;
    tracks[*dest] = copy;
    (*dest)++;
    return true;
}

static GridTrackSize** expand_repeat_track_entries(ScratchArena* scratch,
                                                   GridTrackList* tracks, int repeat_index,
                                                   GridTrackSize* repeat, int repeat_count,
                                                   int new_track_count, int* repeat_start,
                                                   int* repeat_end) {
    GridTrackSize** expanded = (GridTrackSize**)scratch_calloc(
        scratch, (size_t)new_track_count * sizeof(GridTrackSize*));
    if (!expanded) return nullptr;

    int dest = 0;
    for (int i = 0; i < repeat_index; i++) {
        if (!append_cloned_grid_track(scratch, expanded, &dest, tracks->tracks[i])) return NULL;
    }
    *repeat_start = dest;
    for (int i = 0; i < repeat_count; i++) {
        for (int j = 0; j < repeat->repeat_track_count; j++) {
            if (!append_cloned_grid_track(scratch, expanded, &dest,
                                          repeat->repeat_tracks[j])) return NULL;
        }
    }
    *repeat_end = dest;
    for (int i = repeat_index + 1; i < tracks->track_count; i++) {
        if (!append_cloned_grid_track(scratch, expanded, &dest, tracks->tracks[i])) return NULL;
    }
    return expanded;
}

static bool expand_auto_repeat_axis(GridContainerLayout* grid_layout, bool is_column) {
    GridTrackList** list_slot = is_column ? &grid_layout->grid_template_columns
                                         : &grid_layout->grid_template_rows;
    bool* auto_fit_tracks = is_column ? grid_layout->auto_fit_columns
                                      : grid_layout->auto_fit_rows;
    int* auto_fit_count = is_column ? &grid_layout->auto_fit_col_count
                                    : &grid_layout->auto_fit_row_count;
    GridTrackList* tracks = *list_slot;
    if (!tracks || tracks->track_count <= 0) return true;

    float available_size = is_column ? grid_layout->content_width : grid_layout->content_height;
    float gap = is_column ? grid_layout->column_gap : grid_layout->row_gap;

    for (int repeat_index = 0; repeat_index < tracks->track_count; repeat_index++) {
        GridTrackSize* repeat = tracks->tracks[repeat_index];
        if (!repeat || repeat->type != GRID_TRACK_SIZE_REPEAT ||
            (!repeat->is_auto_fill && !repeat->is_auto_fit)) continue;

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
        // Preserve the existing column-only cap until empty auto-fit gutters collapse fully.
        if (is_column && repeat->is_auto_fit && grid_layout->item_count > 0 &&
            repeat_count > grid_layout->item_count) {
            repeat_count = grid_layout->item_count;
        }

        long long expanded = (long long)repeat_count * (long long)repeat->repeat_track_count;
        long long total_tracks = (long long)tracks->track_count - 1 + expanded;
        if (total_tracks <= 0 || total_tracks != (long long)(int)total_tracks) return false;
        int new_track_count = (int)total_tracks; // INT_CAST_OK: range equality above proves the count fits.
        int auto_fit_start = 0;
        int auto_fit_end = 0;
        GridTrackSize** new_tracks = expand_repeat_track_entries(
            &grid_layout->lycon->scratch, tracks, repeat_index, repeat,
            repeat_count, new_track_count,
            &auto_fit_start, &auto_fit_end);
        if (!new_tracks) return false;

        if (repeat->is_auto_fit && new_track_count <= 64) {
            for (int i = 0; i < new_track_count; i++) {
                auto_fit_tracks[i] = i >= auto_fit_start && i < auto_fit_end;
            }
            *auto_fit_count = new_track_count;
        }
        // Auto-repeat mutates only the pass-local clone; abandoned generations
        // remain owned by the container scratch mark and need no free chain.
        tracks->tracks = new_tracks;
        tracks->track_count = new_track_count;
        tracks->allocated_tracks = new_track_count;
        tracks->is_repeat = false;
        tracks->line_names = (char**)scratch_calloc(
            &grid_layout->lycon->scratch,
            (size_t)(new_track_count + 1) * sizeof(char*));
        tracks->line_name_count = 0;
        if (!tracks->line_names) return false;
        *list_slot = tracks;

        break; // CSS permits only one auto-repeat per axis.
    }
    return true;
}
// Expand auto-fill/auto-fit repeat() tracks based on available space.
void expand_auto_repeat_tracks(GridContainerLayout* grid_layout) {
    if (!grid_layout || !expand_auto_repeat_axis(grid_layout, true)) return;
    expand_auto_repeat_axis(grid_layout, false);
}
