#include "layout.hpp"
#include "../lib/tagged.hpp"

extern "C" {
#include "../lib/log.h"
#include "../lib/memtrack.h"
}
// Multi-pass grid layout implementation
// Follows the same pattern as layout_flex_multipass.cpp
// External function declarations
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);
void layout_flow_node(LayoutContext* lycon, DomNode* node);
void line_init(LayoutContext* lycon, float left, float right);
void line_break(LayoutContext* lycon);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum display);
// Forward declarations for static functions
static void layout_grid_item_final_content_multipass(LayoutContext* lycon, ViewBlock* grid_item);

static float grid_container_content_size_for_item_percentages(LayoutContext* lycon,
                                                             ViewBlock* grid_container,
                                                             bool horizontal) {
    if (!grid_container) return horizontal ? 0.0f : -1.0f;

    float content_size = layout_block_used_content_size(grid_container, horizontal, false);
    float given_size = layout_block_given_content_size(grid_container, horizontal);
    if (given_size >= 0.0f) return given_size;
    if (horizontal) {
        if (content_size < 0.0f) content_size = 0.0f;
        float declared_size = layout_block_declared_content_size(
            lycon, grid_container, CSS_PROPERTY_WIDTH, true);
        return declared_size >= 0.0f ? declared_size : content_size;
    }
    return content_size > 0.0f ? content_size : -1.0f;
}

static float grid_item_percentage_base_from_parent(LayoutContext* lycon, ViewBlock* grid_item) {
    if (!grid_item) return 0.0f;
    // CSS Display 3: a contents ancestor does not establish the item's
    // containing block; find the grid box in the flattened ancestor chain.
    for (DomNode* ancestor = grid_item->parent; ancestor;
         ancestor = ancestor->parent) {
        if (!ancestor->is_element()) continue;
        DisplayValue display = resolve_display_value(ancestor);
        if (display.outer == CSS_VALUE_CONTENTS) continue;
        if (display.inner != CSS_VALUE_GRID) break;
        ViewBlock* grid_container = lam::view_as_block(ancestor->as_element());
        return grid_container_content_size_for_item_percentages(
            lycon, grid_container, true);
    }
    return grid_item->width;
}

static float grid_flex_container_auto_border_height(LayoutContext* lycon,
                                                    ViewBlock* flex_container,
                                                    float fallback_content_height) {
    if (!flex_container) return 0.0f;

    bool has_explicit_height = layout_axis_has_given_size(flex_container, false);
    if (has_explicit_height) {
        return flex_container->height;
    }

    BoxMetrics box = layout_box_metrics(flex_container);
    float content_width = layout_content_size_from_border_box(
        flex_container, flex_container->width, true);
    if (lycon && content_width > 0.0f) {
        // The old path guessed flex orientation from already-laid-out children,
        // which double-counted margins and drifted from flex intrinsic sizing.
        float measured_height = calculate_max_content_height(
            lycon, static_cast<DomNode*>(flex_container), content_width);
        if (measured_height > 0.0f) return measured_height;
    }

    return fallback_content_height + box.pad_border_v;
}

static int grid_inline_baseline_track(GridContainerLayout* grid_layout,
                                      bool prefer_last) {
    int baseline_track = 0;
    for (int i = 0; i < grid_layout->item_count; i++) {
        GridItemProp* grid_item = grid_item_prop(grid_layout->grid_items[i]);
        if (!grid_item) continue;
        int item_track = prefer_last ? grid_item->computed_grid_row_end - 1
                                     : grid_item->computed_grid_row_start;
        if (item_track <= 0) continue;
        if (baseline_track == 0 ||
            (prefer_last ? item_track > baseline_track : item_track < baseline_track)) {
            baseline_track = item_track;
        }
    }
    return baseline_track;
}

static void grid_store_inline_baseline(LayoutContext* lycon,
                                       GridContainerLayout* grid_layout,
                                       ViewBlock* grid_container,
                                       bool prefer_last) {
    int baseline_track = grid_inline_baseline_track(grid_layout, prefer_last);
    if (baseline_track == 0) return;

    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        GridItemProp* grid_item = grid_item_prop(item);
        if (!item || !grid_item) continue;
        int item_track = prefer_last ? grid_item->computed_grid_row_end - 1
                                     : grid_item->computed_grid_row_start;
        if (item_track != baseline_track) continue;

        float item_baseline = -1.0f;
        if (item->blk) {
            item_baseline = prefer_last ? item->block()->last_line_baseline
                                        : item->block()->first_line_baseline;
        }
        if (item_baseline <= 0.0f) {
            item_baseline = prefer_last ?
                radiant::compute_element_last_baseline(lycon, item, true) :
                radiant::compute_element_first_baseline(lycon, item, true);
        }
        if (item_baseline >= 0.0f) {
            float vertical_baseline = layout_vertical_item_baseline_from_border_edges(
                grid_container, static_cast<View*>(item));
            float grid_baseline = vertical_baseline >= 0.0f
                // Grid baseline caches are consumed from the border-box origin.
                ? layout_block_start_content_offset(grid_container) + vertical_baseline
                : item_baseline + (prefer_last ? item->y : 0.0f);
            if (prefer_last) {
                // The last set comes from an item ending in the block-end-most row;
                // using the first-row cache ignores baseline-source:last.
                grid_container->blk->last_line_baseline = grid_baseline;
                // finalize_block_flow runs after grid layout and persists its active
                // context; without this, it overwrites the grid's last baseline.
                lycon->block.last_line_ascender = grid_baseline;
            } else {
                grid_container->blk->first_line_baseline = grid_baseline;
                // The enclosing block finalizer owns the persistent BlockProp state,
                // so publish the baseline through its active flow context as well.
                lycon->block.first_line_ascender = grid_baseline;
            }
            return;
        }
    }
}

static void grid_store_inline_baselines(LayoutContext* lycon,
                                        GridContainerLayout* grid_layout,
                                        ViewBlock* grid_container) {
    if (!lycon || !grid_layout || !grid_container ||
        grid_container->display.outer != CSS_VALUE_INLINE_BLOCK ||
        grid_container->display.inner != CSS_VALUE_GRID) {
        return;
    }

    if (!grid_container->blk) {
        // CSS Grid §9.1: an inline-grid exports its track baseline to inline flow.
        grid_container->ensure_block(lycon);
    }
    if (!grid_container->blk) return;

    grid_container->blk->first_line_baseline = 0.0f;
    grid_container->blk->last_line_baseline = 0.0f;
    grid_store_inline_baseline(lycon, grid_layout, grid_container, false);
    grid_store_inline_baseline(lycon, grid_layout, grid_container, true);
}

static bool layout_grid_anonymous_text_content(
    LayoutContext* lycon, ViewBlock* grid_container) {
    if (!lycon || !grid_container) return false;

    bool has_text = false;
    for (DomNode* child = grid_container->first_child; child;
         child = child->next_sibling) {
        if (child->is_text() && layout_text_node_has_content(child)) {
            has_text = true;
        } else if (child->is_text()) {
            layout_suppress_ignorable_container_text(child);
        }
    }
    if (!has_text) return false;

    // css grid §4.2: non-empty direct text is laid out as an anonymous grid item.
    LayoutContentBox used_content = layout_content_box(grid_container);
    float content_width = used_content.width;
    if (content_width <= 0.0f) {
        content_width = layout_content_size_from_border_box(
            grid_container, grid_container->width, true);
    }
    float content_left = layout_axis_decoration_start(
        grid_container->bound, LAYOUT_AXIS_X);
    lycon->block.content_width = max(content_width, 0.0f);
    lycon->block.advance_y = 0.0f;
    line_init(lycon, content_left, content_left + max(content_width, 0.0f));
    for (DomNode* child = grid_container->first_child; child;
         child = child->next_sibling) {
        if (child->is_text() && layout_text_node_has_content(child)) {
            layout_flow_node(lycon, child);
        }
    }
    if (!lycon->line.is_line_start) line_break(lycon);

    float content_height = max(lycon->block.advance_y, 0.0f);
    if (layout_axis_has_given_size(grid_container, false)) {
        // CSS Grid §2.1: anonymous text must not replace a definite grid
        // container block size with its intrinsic line height.
        content_height = used_content.height;
    }
    grid_container->content_height = content_height;
    grid_container->height = layout_border_size_from_content_box(
        grid_container, content_height, false);
    return true;
}

// Main Entry Point

void layout_grid_content(LayoutContext* lycon, ViewBlock* grid_container) {
    if (!grid_container) return;
    // guard against stack overflow from deeply nested grid containers (fuzzer-found).
    // nested grid items recurse here via layout_grid_item_final_content_multipass
    // without passing through layout_flow_node, so they bypass that depth guard.
    // uses a dedicated, small limit because each grid multipass frame is very large
    // and overflows the stack at far shallower nesting than MAX_LAYOUT_DEPTH.
    if (lycon->grid_depth >= MAX_GRID_DEPTH) {
        log_error("layout_grid_content: grid_depth=%d at limit (%d), skipping nested grid %s",
                  lycon->grid_depth, MAX_GRID_DEPTH, grid_container->node_name());
        return;
    }
    struct GridDepthGuard {
        LayoutContext* lycon;
        GridDepthGuard(LayoutContext* lycon) : lycon(lycon) { lycon->grid_depth++; }
        ~GridDepthGuard() { lycon->grid_depth--; }
    } depth_guard(lycon);

    log_enter();

    LayoutViewScope view_scope(lycon);
    lycon->elmt = static_cast<DomNode*>(grid_container);
    lycon->view = static_cast<View*>(grid_container);
    // CACHE LOOKUP: Check if we have a cached result for these constraints
    // This avoids redundant layout for repeated measurements with same inputs
    DomElement* dom_elem = lam::dom_require<DOM_NODE_ELEMENT>(grid_container);
    radiant::KnownDimensions known_dims = radiant::layout_known_dimensions_from_context(lycon);
    // Try cache lookup
    radiant::SizeF cached_size;
    if (radiant::layout_pass_cache_get(lycon, dom_elem, known_dims, &cached_size, "GRID")) {
        grid_container->width = cached_size.width;
        grid_container->height = cached_size.height;
        log_leave();
        return;
    }
    // EARLY BAILOUT: For ComputeSize mode, check if dimensions are already known
    // This optimization avoids redundant layout when only measurements are needed
    if (lycon->run_mode == radiant::RunMode::ComputeSize) {
        // Check if both dimensions are explicitly set via CSS
        bool has_definite_width = (lycon->block.given_width >= 0);
        bool has_definite_height = (lycon->block.given_height >= 0);

        if (has_definite_width && has_definite_height) {
            // Both dimensions known - can skip full layout
            grid_container->width = lycon->block.given_width;
            grid_container->height = lycon->block.given_height;
            log_leave();
            return;
        }
    }
    // Initialize grid container
    GridLayoutScope grid_scope(lycon, grid_container);
    // Note: Grid properties (grid-template-columns/rows) may not be populated in embed->grid
    // at this point if they haven't been resolved in resolve_css_style.cpp.
    // The grid algorithm will use defaults in this case.
    // PASS 0: Style Resolution and View Initialization
    int item_count = resolve_grid_item_styles(lycon, grid_container);

    if (item_count == 0) {
        if (layout_grid_anonymous_text_content(lycon, grid_container)) {
            log_leave();
            return;
        }
        // Check whether positioned children need the resolved grid lines.
        bool has_absolute_children = false;
        int node_capacity = lycon->grid_container->allocated_items;
        DomNode** nodes = node_capacity > 0
            ? (DomNode**)scratch_calloc(&lycon->scratch,
                (size_t)node_capacity * sizeof(DomNode*)) : nullptr;
        int node_count = nodes
            ? collect_grid_item_nodes(lycon, grid_container,
                grid_container->first_child, nodes, node_capacity, false) : 0;
        for (int node_index = 0; node_index < node_count; node_index++) {
            DomNode* ch = nodes[node_index];
            if (ch->is_element()) {
                DomElement* ce = ch->as_element();
                if (ce->position &&
                    (ce->positionp()->position == CSS_VALUE_ABSOLUTE ||
                     ce->positionp()->position == CSS_VALUE_FIXED)) {
                    // Styles were already resolved in resolve_grid_item_styles above
                    // (init_grid_item_view was called for all children). Only call again
                    // if for some reason gi was not populated yet (defensive guard).
                    ViewBlock* ce_block = lam::view_as_block(ce);
                    if (!grid_item_prop(ce_block)) {
                        init_grid_item_view(lycon, ch);
                    }
                    has_absolute_children = true;
                }
            }
        }
        if (nodes) scratch_free(&lycon->scratch, nodes);
        // CSS Grid §11: explicit tracks size an empty grid, even without
        // positioned children that consume its grid lines.
        layout_grid_container(lycon, grid_container);
        if (has_absolute_children) {
            layout_grid_absolute_children(lycon, grid_container);
        }
        log_leave();
        return;
    }
    // PASS 1: Content Measurement (for intrinsic track sizing)
    measure_grid_items(lycon, lycon->grid_container);
    // PASS 2: Grid Algorithm Execution
    layout_grid_container(lycon, grid_container);
    // PASS 3: Final Content Layout
    layout_final_grid_content(lycon, lycon->grid_container);
    // Update row heights based on actual content heights from Pass 3.
    // The intrinsic height measurement in Pass 2 may underestimate because it doesn't
    // fully handle complex layouts (flex, margins, etc.). After content layout, items
    // have accurate content_height values.
    {
        GridContainerLayout* gl = lycon->grid_container;
        bool rows_changed = false;
        for (int r = 0; r < gl->computed_row_count; r++) {
            float max_content_h = 0;
            for (int i = 0; i < gl->item_count; i++) {
                ViewBlock* item = gl->grid_items[i];
                GridItemProp* gi = grid_item_prop(item);
                if (!item || !gi) continue;
                int rs = gi->computed_grid_row_start - 1;
                int re = gi->computed_grid_row_end - 1;
                if (rs != r || re != r + 1) continue; // only single-row-span items
                float h = item->content_height;
                // CSS Grid: if item has max-height, its contribution to the row is capped
                if (item->blk && item->block_mut()->given_max_height >= 0) {
                    float bb_max = layout_css_size_to_border_box(
                        item->bound, layout_box_sizing(item), item->block()->given_max_height, false);
                    if (layout_uses_border_box(item)) {
                        bb_max = layout_floor_border_box_axis(item, bb_max, false);
                    }
                    if (h > bb_max) h = bb_max;
                }
                if (h > max_content_h) max_content_h = h;
            }
            radiant::grid::EnhancedGridTrack* row_track = &(*gl->computed_rows)[r];
            if (max_content_h > row_track->base_size + 0.5f) {
                // A finite max track size (for example minmax(auto, 50px))
                // caps the row; overflowing items must not enlarge it afterward.
                if (!radiant::grid::grid_track_allows_content_growth(*row_track)) {
                    continue;
                }
                row_track->base_size = max_content_h;
                rows_changed = true;
            }
        }
        if (rows_changed) {
            position_grid_items(gl, grid_container, &lycon->scratch);
        }
    }
    // Re-align items after content is laid out (now items have final heights)
    // This is needed for align-items: center/end to work correctly
    align_grid_items(lycon->grid_container);
    // Apply relative positioning offsets (position:relative + top/left/bottom/right)
    // Must be done AFTER final alignment so offsets are relative to the aligned position
    {
        GridContainerLayout* gl = lycon->grid_container;
        float parent_w = (float)grid_container->width;
        float parent_h = (float)grid_container->height;
        for (int i = 0; i < gl->item_count; i++) {
            ViewBlock* item = gl->grid_items[i];
            if (!item || !item->position) continue;
            if (item->positionp()->position == CSS_VALUE_STICKY) {
                layout_sticky_positioned(lycon, item);
                continue;
            }
            if (item->positionp()->position != CSS_VALUE_RELATIVE) continue;
            float ox = layout_relative_axis_offset(item, true, parent_w);
            float oy = layout_relative_axis_offset(item, false, parent_h);
            if (ox != 0 || oy != 0) {
                item->x += ox;
                item->y += oy;
            }
        }
    }

    log_info("=== GRID PASS 3 COMPLETE ===");
    // Update container height based on actual item positions and sizes
    // This is needed because content layout may cause items to exceed their
    // track-allocated sizes (e.g., when item content is larger than track)
    // Only do this for containers with auto height (no explicit height set)
    GridContainerLayout* grid_layout = lycon->grid_container;
    // Size containment excludes in-flow grid items from the container's size;
    // their overflowing tracks must not feed back into its intrinsic fallback.
    bool has_explicit_height = !layout_block_has_automatic_height(grid_container) ||
        layout_block_has_size_containment_in_axis(grid_container, false);
    // A row with a fixed maximum establishes the auto grid block size;
    // overflowing items must not feed back into that size.
    bool has_non_definite_row = false;
    if (grid_layout) {
        for (int row = 0; row < grid_layout->computed_row_count; row++) {
            if (radiant::grid::grid_track_allows_content_growth(
                    (*grid_layout->computed_rows)[row])) {
                has_non_definite_row = true;
                break;
            }
        }
    }

    if (grid_layout && grid_layout->item_count > 0 && !has_explicit_height && has_non_definite_row) {
        // Find the maximum extent of all grid items
        float max_item_bottom = 0;
        for (int i = 0; i < grid_layout->item_count; i++) {
            ViewBlock* item = grid_layout->grid_items[i];
            if (!item) continue;

            float item_bottom = item->y + item->height;
            if (item_bottom > max_item_bottom) {
                max_item_bottom = item_bottom;
            }
        }

        float required_height = max_item_bottom +
            layout_axis_decoration_end(grid_container->bound, LAYOUT_AXIS_Y);
        // Update container height if needed
        if (required_height > grid_container->height) {
            // When percentage row tracks overflow the intrinsic height, cap the
            // container height at the intrinsic row height (items overflow the container)
            if (grid_layout->row_intrinsic_height > 0.0f) {
                float capped_height = grid_layout->row_intrinsic_height;
                capped_height += layout_box_metrics(grid_container).pad_border_v;
                if (capped_height > grid_container->height) {
                    grid_container->height = capped_height;
                }
            } else {
                grid_container->height = required_height;
            }
            // Also fix any item with negative y position (pushed above due to centering)
            // by shifting all items down
            float min_item_y = 0;
            for (int i = 0; i < grid_layout->item_count; i++) {
                ViewBlock* item = grid_layout->grid_items[i];
                if (!item) continue;
                if (item->y < min_item_y) {
                    min_item_y = item->y;
                }
            }
            if (min_item_y < 0) {
                // Shift all items down by the negative offset
                float shift = -min_item_y;
                for (int i = 0; i < grid_layout->item_count; i++) {
                    ViewBlock* item = grid_layout->grid_items[i];
                    if (!item) continue;
                    item->y += shift;
                }
                grid_container->height += shift;
            }
        }
    }
    // Fallback: also check row-based calculation for containers without items
    // Only apply if container height is auto (not explicitly set)
    if (grid_layout && grid_layout->computed_row_count > 0 && !has_explicit_height) {
        float container_height = layout_grid_row_border_box_extent(grid_container, grid_layout);
        // Only update if calculated height is greater (content overflow case)
        if (container_height > grid_container->height) {
            grid_container->height = container_height;
        }
    }
    // Update container width based on grid content (for shrink-to-fit containers)
    // NOTE: layout_grid_container (Pass 2) already sets the container width for
    // shrink-to-fit grids using the correct first-pass intrinsic width (before
    // percentage re-resolution). This secondary update is only needed as a
    // fallback when layout_grid_container didn't handle it (e.g., empty grid).
    // When percentage tracks exist, the container width must be the first-pass
    // max-content width, NOT the sum of resolved column sizes (which would be
    // smaller due to percentage clamping).
    if (grid_layout && grid_layout->computed_column_count > 0) {
        // Check if container is shrink-to-fit (inline-grid, or absolutely
        // positioned with no explicit width)
        if (layout_is_shrink_to_fit_width(grid_container) &&
            grid_layout->is_shrink_to_fit_width) {
            // layout_grid_container already set the correct width in Pass 2
            // (including the first-pass intrinsic width for pct tracks).
            // Use grid_layout->content_width which is already correct.
            float container_width = (float)grid_layout->content_width;
            container_width += layout_box_metrics(grid_container).pad_border_h;
            grid_container->width = container_width;
        }
    }

    WritingMode grid_writing_mode = layout_block_writing_mode(grid_container);
    if (grid_writing_mode == WM_VERTICAL_LR || grid_writing_mode == WM_VERTICAL_RL) {
        bool preserve_surrogate_axes =
            radiant::layout_inline_context_has_explicit_baseline_source(grid_container);
        if (preserve_surrogate_axes) {
            // Explicit baseline-source publication consumes the legacy surrogate
            // track axes; retain that representation until the parent maps it.
            float logical_width = grid_container->width;
            float logical_height = grid_container->height;
            grid_container->width = logical_height;
            grid_container->height = logical_width;
        }
        LayoutContentBox grid_content = layout_content_box(grid_container);
        grid_container->content_width = grid_content.width;
        grid_container->content_height = grid_content.height;
        layout_publish_vertical_children(grid_container, grid_writing_mode,
            preserve_surrogate_axes,
            lycon->block.line_height, lycon->block.first_line_max_descender);
    }

    grid_store_inline_baselines(lycon, lycon->grid_container, grid_container);
    // PASS 4: Absolute Positioned Children
    layout_grid_absolute_children(lycon, grid_container);
    // CACHE STORE: Save computed result for future lookups
    radiant::SizeF result = radiant::size_f(grid_container->width, grid_container->height);
    radiant::layout_pass_cache_store(lycon, dom_elem, known_dims, result, "GRID");
    // Cleanup and restore parent context
    grid_scope.close();

    log_leave();
}
// Pass 0: Style Resolution and View Initialization

int resolve_grid_item_styles(LayoutContext* lycon, ViewBlock* grid_container) {
    log_enter();
    // CSS Grid §11.4: percentage margins/paddings of grid items resolve against the
    // inline size of their grid area (their containing block for percentage purposes).
    // Since grid areas are not yet known, we use the grid container's content-box width
    // as the initial resolution base. This matches CSS Grid spec behaviour where percentage
    // widths/margins/paddings on grid items use the container's definite inline size.
    // (Same pattern as flex layout — see layout_flex.cpp and CSS §8.3.)
    // For an indefinite container (content_width == 0), percentages resolve to 0 (= auto),
    // which is correct per CSS Grid spec §7.2.1.
    float container_content_width = grid_container_content_size_for_item_percentages(
        lycon, grid_container, true);

    float container_content_height = grid_container_content_size_for_item_percentages(
        lycon, grid_container, false);
    // style resolution must see the grid area as the containing block; the
    // scope restores the outer context after every grid-item setup pass.
    LayoutContainingBlockScope grid_parent_scope(
        lycon, container_content_width, container_content_height,
        container_content_height > 0.0f);

    int node_capacity = layout_count_potential_items(grid_container, false);
    DomNode** nodes = node_capacity > 0
        ? (DomNode**)scratch_calloc(&lycon->scratch,
            (size_t)node_capacity * sizeof(DomNode*)) : nullptr;
    int node_count = nodes
        ? collect_grid_item_nodes(lycon, grid_container,
            grid_container->first_child, nodes, node_capacity, true) : 0;
    int item_count = 0;
    for (int node_index = 0; node_index < node_count; node_index++) {
        DomNode* child = nodes[node_index];
        if (child->is_element()) {
            DomElement* elem = child->as_element();
            // Always resolve styles first (position:absolute may not be known until after cascade)
            init_grid_item_view(lycon, child);
            layout_reresolve_percentage_box(lam::view_as_block(elem), container_content_width);
            // Re-check absolute positioning AFTER style resolution
            ViewBlock* elem_block = lam::view_as_block(elem);
            bool is_absolute = layout_view_is_abs_or_fixed(elem_block);

            if (!is_absolute) {
                // check display:none AFTER style resolution (display may be set via cascade)
                if (!layout_element_is_display_none(elem)) {
                    item_count++;
                }
            } else {
                // Absolute item: gi was populated (for grid-placement containing block),
                // but do not count as an in-flow grid item.
            }
        }
    }

    if (nodes) scratch_free(&lycon->scratch, nodes);

    log_leave();
    return item_count;
}

void init_grid_item_view(LayoutContext* lycon, DomNode* child) {
    if (!child || !child->is_element()) return;

    // CSS Grid style resolution must not leak an item's inherited font into the
    // container pass; later intrinsic queries resolve relative units from the container.
    LayoutFontScope font_scope(lycon);

    DomElement* elem = child->as_element();
    // Resolve and store display value for this element
    // This is crucial for detecting nested grid/flex containers
    elem->display = resolve_display_value((void*)child);
    // Allocate table-role state before grid blockification; table layout later
    // reads border-spacing from that state even though the grid item is a block.
    bool is_table = elem->display.inner == CSS_VALUE_TABLE;
    bool had_table_prop = is_table && elem->table_prop();
    if (is_table && !had_table_prop) set_view(lycon, RDT_VIEW_TABLE, child);
    // table blockification changes grid participation, not the table role;
    // clearing the view tag after allocating the role makes table layout cast
    // the same storage as a generic block and trips its structural invariant.
    elem->view_type = is_table ? RDT_VIEW_TABLE : RDT_VIEW_BLOCK;
    // Initialize dimensions (will be set by grid algorithm)
    elem->x = 0;
    elem->y = 0;
    elem->width = 0;
    elem->height = 0;
    // Force boundary properties allocation for proper measurement
    elem->ensure_boundary(lycon);
    // The parent-item union is independent of the element-role union, so form
    // controls retain their role data while receiving ordinary grid item state.
    ViewBlock* elem_block = lam::view_as_block(elem);
    if (!grid_item_prop(elem_block)) {
        elem->ensure_grid_item(lycon->doc->view_tree);
    }
    // CRITICAL: Set lycon->view to this element so style resolution
    // applies properties to this element, not some other view
    LayoutViewScope view_scope(lycon);
    lycon->view = static_cast<View*>(elem);
    // Resolve styles for this element (CSS cascade, inheritance, etc.)
    // This will now correctly apply padding/margin/border to elem->bound
    dom_node_resolve_style(child, lycon);

}
// Pass 1: Content Measurement

void measure_grid_items(LayoutContext* lycon, GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_enter();
    // Iterate through all grid items and measure their content
    ViewBlock* container = lam::view_as_block(lycon->elmt);
    float container_content_width = grid_container_content_size_for_item_percentages(
        lycon, container, true);
    float container_content_height = grid_container_content_size_for_item_percentages(
        lycon, container, false);
    // measurement uses the same percentage containing block as style setup;
    // keeping one scope prevents the two grid passes from diverging.
    LayoutContainingBlockScope grid_parent_scope(
        lycon, container_content_width, container_content_height,
        container_content_height > 0.0f);

    int node_capacity = layout_count_potential_items(container, false);
    DomNode** nodes = node_capacity > 0
        ? (DomNode**)scratch_calloc(&lycon->scratch,
            (size_t)node_capacity * sizeof(DomNode*)) : nullptr;
    int node_count = nodes
        ? collect_grid_item_nodes(lycon, container, container->first_child,
            nodes, node_capacity, false) : 0;
    for (int node_index = 0; node_index < node_count; node_index++) {
        DomNode* child = nodes[node_index];
        if (child->is_element()) {
            ViewBlock* item = lam::view_require_block(child);
            // Skip absolute positioned and display:none items
            bool is_absolute = layout_view_is_abs_or_fixed(item);
            if (!is_absolute && !layout_block_is_display_none(item)) {
                float min_width = 0, max_width = 0, min_height = 0, max_height = 0;
                measure_grid_item_intrinsic(lycon, item, &min_width, &max_width,
                                            &min_height, &max_height);
                layout_reresolve_percentage_box(item, container_content_width);
                // Store only WIDTH measurements in the item for later use
                // HEIGHT measurements are intentionally NOT stored here because:
                // - Heights depend on the actual column width (after column sizing)
                // - Row sizing will calculate heights on-demand using item->width
                // This follows CSS Grid spec §11.5 where row sizing happens after column sizing
                GridItemProp* gi = grid_item_prop(item);
                if (gi) {
                    gi->measured_min_width = min_width;
                    gi->measured_max_width = max_width;
                    // Note: We don't set measured_min/max_height here.
                    // The calculate_grid_item_intrinsic_sizes function will compute
                    // heights on-demand using the actual column width.
                    gi->has_measured_size = true;  // Indicates width measurements are valid
                }
            }
        }
    }

    if (nodes) scratch_free(&lycon->scratch, nodes);

    log_leave();
}

void measure_grid_item_intrinsic(LayoutContext* lycon, ViewBlock* item,
                                  float* min_width, float* max_width,
                                  float* min_height, float* max_height) {
    if (!item) {
        *min_width = *max_width = *min_height = *max_height = 0;
        return;
    }

    MeasurementCacheEntry* cached = get_from_measurement_cache(static_cast<DomNode*>(item));
    if (cached) {
        *min_width = cached->content_width;
        *max_width = cached->measured_width;
        *min_height = cached->content_height;
        *max_height = cached->measured_height;
        return;
    }

    IntrinsicSizes width = {0.0f, 0.0f};
    IntrinsicSizes height = {0.0f, 0.0f};
    if (lycon) {
        // pass-one grid width is content measurement; stretch limits resolve only
        // after the automatic track establishes its containing block.
        width = layout_measure_intrinsic_widths(
            lycon, lam::dom_require<DOM_NODE_ELEMENT>(item));
        if (item->blk && !layout_axis_size_is_percentage(item, true) &&
            item->block()->given_width > 0.0f) {
            float explicit_width = layout_css_size_to_border_box(
                item->bound, layout_box_sizing(item), item->block()->given_width, true);
            width.min_content = width.max_content =
                layout_floor_border_box_axis(item, explicit_width, true);
        }

        bool explicit_height = item->blk && item->block()->given_height > 0.0f &&
            layout_intrinsic_min_size_keyword(item, false) == CSS_VALUE__UNDEF &&
            layout_intrinsic_max_size_keyword(item, false) == CSS_VALUE__UNDEF;
        if (explicit_height) {
            float value = layout_css_size_to_border_box(
                item->bound, layout_box_sizing(item), item->block()->given_height, false);
            height.min_content = height.max_content =
                layout_floor_border_box_axis(item, value, false);
        } else {
            float width_for_height = min(width.max_content, 2000.0f);
            float content_height = calculate_max_content_height(
                lycon, static_cast<DomNode*>(item), width_for_height);
            height.min_content = height.max_content = content_height;
        }
    } else {
        width.max_content = item->width > 0.0f ? item->width : 0.0f;
        height.max_content = item->height > 0.0f ? item->height : 0.0f;
    }
    *min_width = width.min_content;
    *max_width = width.max_content;
    *min_height = height.min_content;
    *max_height = height.max_content;
    store_in_measurement_cache(static_cast<DomNode*>(item), *max_width, *max_height,
                               *min_width, *min_height);
}
// Pass 3: Final Content Layout

void layout_final_grid_content(LayoutContext* lycon, GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_enter();
    log_info("FINAL GRID CONTENT LAYOUT START");
    // Layout content within each grid item with their final sizes
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        if (!item) continue;

        layout_grid_item_final_content_multipass(lycon, item);
    }

    log_info("FINAL GRID CONTENT LAYOUT END");
    log_leave();
}
// Layout final content of a single grid item (multipass version with nested support)
static void layout_grid_item_final_content_multipass(LayoutContext* lycon, ViewBlock* grid_item) {
    if (!grid_item) return;

    log_enter();
    log_info("Layout grid item content: item=%p (%s), size=%.0fx%.0f at (%.0f,%.0f)",
             grid_item, grid_item->node_name(),
             grid_item->width, grid_item->height,
             grid_item->x, grid_item->y);

    LayoutContextScope context_scope(lycon);

    float grid_item_percentage_base = grid_item_percentage_base_from_parent(lycon, grid_item);
    layout_reresolve_percentage_box(grid_item, grid_item_percentage_base);
    // Update font context to use the grid item's own computed font
    // This ensures text nodes inside the item inherit the correct font-size
    if (grid_item->font && lycon->ui_context) {
        setup_font(lycon->ui_context, &lycon->font, grid_item->font);
    }
    // CSS Grid: final item content establishes its own block formatting context;
    // stale parent strut metrics otherwise shift text while breaks stay anchored.
    setup_line_height(lycon, grid_item);
    layout_setup_block_font_metrics(lycon);
    lycon->block.block_container_font = lycon->font.style;
    // Calculate content area dimensions accounting for box model
    LayoutContentBox content = layout_content_box(grid_item);
    float content_width = content.width;
    float content_height = content.height;
    float content_x_offset = content.offset_x;
    float content_y_offset = content.offset_y;
    // Set up block formatting context for nested content
    lycon->block.content_width = content_width;
    lycon->block.content_height = content_height;
    lycon->block.given_width = content_width;
    lycon->block.given_height = -1;  // Auto height
    lycon->block.advance_y = content_y_offset;  // Start after padding/border top
    lycon->block.max_width = 0;
    lycon->elmt = static_cast<DomNode*>(grid_item);
    // Inherit text alignment from grid item if specified
    if (grid_item->blk) {
        lycon->block.text_align = grid_item->block()->text_align;
    }
    // Set up line formatting context
    // Add a small subpixel tolerance to the right boundary to compensate for integer truncation
    // of float track widths (e.g. 173.28px -> 173px causes 153.28px text to spuriously wrap).
    // This only affects grid item layout, not float/normal flow layouts.
    line_init(lycon, content_x_offset, content_x_offset + content_width);
    lycon->line.right += 0.5f;
    // Check if this grid item is itself a grid or flex container (nested)
    if (grid_item->display.inner == CSS_VALUE_GRID) {
        log_info(">>> NESTED GRID DETECTED: item=%p (%s)", grid_item, grid_item->node_name());
        // Recursively handle nested grid
        {
            LayoutContainingBlockScope item_parent_scope(
                lycon, LAYOUT_AXIS_X, grid_item_percentage_base);
            layout_grid_content(lycon, grid_item);
        }

    } else if (grid_item->display.inner == CSS_VALUE_FLEX) {
        log_info(">>> NESTED FLEX DETECTED: item=%p (%s)", grid_item, grid_item->node_name());
        // Use flex layout for nested flex container
        // The flex layout will initialize its own flex items with init_flex_item_view
        // Do NOT call init_grid_item_view for flex children - they are flex items, not grid items!
        extern void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container);
        {
            LayoutContainingBlockScope item_parent_scope(
                lycon, LAYOUT_AXIS_X, grid_item_percentage_base);
            layout_flex_container_with_nested_content(lycon, grid_item);
        }
        grid_item->content_height = grid_flex_container_auto_border_height(
            lycon, grid_item, lycon->block.advance_y);

    } else if (grid_item->display.inner == CSS_VALUE_TABLE) {
        // A table keeps its own role while gi describes participation in this grid.
        LayoutViewScope view_scope(lycon);
        lycon->view = grid_item;
        bool has_table_max_constraint = false;
        if (grid_item->is_element() && grid_item->as_element()->specified_style) {
            CssDeclaration* max_width = style_tree_get_declaration(
                grid_item->as_element()->specified_style, CSS_PROPERTY_MAX_WIDTH);
            CssDeclaration* max_height = style_tree_get_declaration(
                grid_item->as_element()->specified_style, CSS_PROPERTY_MAX_HEIGHT);
            has_table_max_constraint = (max_width && max_width->value) ||
                (max_height && max_height->value);
        }
        if (has_table_max_constraint && grid_item->blk) {
            // max-constrained grid tables receive a definite used box from grid
            // stretch before their empty table tracks are distributed.
            grid_item->block_mut()->given_width = content_width;
            grid_item->block_mut()->given_width_type = CSS_VALUE__UNDEF;
            grid_item->block_mut()->given_height = content_height;
            grid_item->block_mut()->given_height_type = CSS_VALUE__UNDEF;
        }
        layout_table_content(lycon, grid_item, grid_item->display);
    } else {
        // Standard flow layout for grid item content
        DomNode* child = grid_item->first_child;
        while (child) {
            layout_flow_node(lycon, child);
            child = child->next_sibling;
        }
        // Finalize any pending line content
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }
    // Update grid item content dimensions
    // Note: max_width and advance_y are relative to the content box
    // We need to add padding for the full content dimensions
    layout_reresolve_percentage_box(grid_item, grid_item_percentage_base);
    grid_item->content_width = lycon->block.max_width;
    if (grid_item->display.inner != CSS_VALUE_FLEX) {
        grid_item->content_width += layout_axis_padding_end(grid_item->boundary(), LAYOUT_AXIS_X);
        grid_item->content_height = lycon->block.advance_y +
            layout_axis_padding_end(grid_item->boundary(), LAYOUT_AXIS_Y);
    }

    if (grid_item->blk) {
        // Grid item layout bypasses finalize_block_flow, so retain both
        // in-flow baseline sets for the inline-grid container.
        grid_item->blk->first_line_baseline = lycon->block.first_line_ascender;
        grid_item->blk->last_line_baseline = lycon->block.last_line_ascender;
    }

    log_info("Grid item content layout complete: %s, content=%dx%d",
             grid_item->node_name(), grid_item->content_width, grid_item->content_height);
    log_leave();
}
// Grid Absolute Positioning Helpers
// Calculate track positions for a given axis
// Returns an array of (track_count + 1) positions representing grid line positions
static float* calculate_grid_line_positions(GridContainerLayout* grid_layout, LayoutAxis axis,
                                            float container_offset, int* out_line_count) {
    if (!grid_layout || !grid_layout->lycon) return nullptr;
    bool row_axis = axis == LAYOUT_AXIS_Y;
    int track_count = row_axis ? grid_layout->computed_row_count : grid_layout->computed_column_count;
    radiant::grid::TrackArray* tracks = row_axis
        ? grid_layout->computed_rows : grid_layout->computed_columns;
    float gap = row_axis ? grid_layout->row_gap : grid_layout->column_gap;
    // We need (track_count + 1) positions for grid lines
    int line_count = track_count + 1;
    float* positions = (float*)scratch_calloc(&grid_layout->lycon->scratch,
        (size_t)line_count * sizeof(float));
    if (!positions) return nullptr;

    float current_pos = container_offset;
    for (int i = 0; i <= track_count; i++) {
        positions[i] = current_pos;
        if (i < track_count) {
            current_pos += (*tracks)[i].base_size;
            if (i < track_count - 1) {
                current_pos += gap;
            }
        }
    }

    *out_line_count = line_count;
    return positions;
}
// Compute the containing block for an absolutely positioned grid item
// Based on its grid-column and grid-row properties
// Returns true if grid area was computed, false if should use normal containing block
static bool compute_grid_area_for_absolute(
    GridContainerLayout* grid_layout,
    ViewBlock* container,
    ViewBlock* item,
    float* out_x, float* out_y, float* out_width, float* out_height
) {
    if (!grid_layout || !container || !item) return false;
    // Get grid item properties (if any)
    GridItemProp* gi = grid_item_prop(item);
    // Check if item has any grid placement
    bool has_col_start = gi && gi->has_explicit_grid_column_start && gi->grid_column_start != 0;
    bool has_col_end = gi && gi->has_explicit_grid_column_end && gi->grid_column_end != 0;
    bool has_row_start = gi && gi->has_explicit_grid_row_start && gi->grid_row_start != 0;
    bool has_row_end = gi && gi->has_explicit_grid_row_end && gi->grid_row_end != 0;
    // If no explicit grid placement, use normal containing block (whole grid padding box)
    if (!has_col_start && !has_col_end && !has_row_start && !has_row_end) {
        return false;
    }

    LayoutAxisPair<float> offsets = {
        layout_axis_decoration_start(container->bound, LAYOUT_AXIS_X),
        layout_axis_decoration_start(container->bound, LAYOUT_AXIS_Y)
    };
    LayoutAxisPair<int> line_counts = {};
    LayoutAxisPair<float*> positions = {};
    for (int i = LAYOUT_AXIS_X; i <= LAYOUT_AXIS_Y; i++) {
        LayoutAxis axis = (LayoutAxis)i;
        positions[axis] = calculate_grid_line_positions(
            grid_layout, axis, offsets[axis], &line_counts[axis]);
    }

    if (!positions[LAYOUT_AXIS_X] || !positions[LAYOUT_AXIS_Y]) {
        scratch_free(&grid_layout->lycon->scratch, positions[LAYOUT_AXIS_X]);
        scratch_free(&grid_layout->lycon->scratch, positions[LAYOUT_AXIS_Y]);
        return false;
    }
    // CSS Grid §9.1: For absolutely positioned items, when a start or end is auto,
    // the containing block extends to the outer edge of the padding box at that end.
    // This is the BORDER edge (not the content/padding), so it includes padding but not border.
    BoxEdges border = layout_boundary_border_edges(container->bound);
    float border_left = border.left;
    float border_right = border.right;
    float border_top = border.top;
    float border_bottom = border.bottom;
    // Padding-box outer edges in container border-box coordinates
    float col_auto_start_pos = border_left;
    float col_auto_end_pos   = (float)container->width  - border_right;
    float row_auto_start_pos = border_top;
    float row_auto_end_pos   = (float)container->height - border_bottom;
    // Resolve explicit grid lines (1-based CSS to 0-based index into positions array)
    // Handle negative line numbers (count from end)
    LayoutAxisPair<bool> has_start = {has_col_start, has_row_start};
    LayoutAxisPair<bool> has_end = {has_col_end, has_row_end};
    LayoutAxisPair<int> authored_start = {
        has_col_start ? gi->grid_column_start : 0,
        has_row_start ? gi->grid_row_start : 0
    };
    LayoutAxisPair<int> authored_end = {
        has_col_end ? gi->grid_column_end : 0,
        has_row_end ? gi->grid_row_end : 0
    };
    LayoutAxisPair<float> auto_start = {col_auto_start_pos, row_auto_start_pos};
    LayoutAxisPair<float> auto_end = {col_auto_end_pos, row_auto_end_pos};
    LayoutAxisPair<float> start = {};
    LayoutAxisPair<float> end = {};
    for (int i = LAYOUT_AXIS_X; i <= LAYOUT_AXIS_Y; i++) {
        LayoutAxis axis = (LayoutAxis)i;
        int start_line = authored_start[axis];
        int end_line = authored_end[axis];
        if (start_line < 0) start_line = line_counts[axis] + start_line + 1;
        if (end_line < 0) end_line = line_counts[axis] + end_line + 1;
        start[axis] = has_start[axis]
            ? positions[axis][start_line >= 1 && start_line <= line_counts[axis]
                ? start_line - 1 : 0] : auto_start[axis];
        end[axis] = has_end[axis]
            ? positions[axis][end_line >= 1 && end_line <= line_counts[axis]
                ? end_line - 1 : line_counts[axis] - 1] : auto_end[axis];
        if (start[axis] > end[axis]) {
            float swap = start[axis];
            start[axis] = end[axis];
            end[axis] = swap;
        }
    }

    *out_x = start[LAYOUT_AXIS_X];
    *out_y = start[LAYOUT_AXIS_Y];
    *out_width = end[LAYOUT_AXIS_X] - start[LAYOUT_AXIS_X];
    *out_height = end[LAYOUT_AXIS_Y] - start[LAYOUT_AXIS_Y];

    scratch_free(&grid_layout->lycon->scratch, positions[LAYOUT_AXIS_Y]);
    scratch_free(&grid_layout->lycon->scratch, positions[LAYOUT_AXIS_X]);
    return true;
}

static void layout_grid_abs_prepare_child(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx, AbsChildLayoutState* state) {
    (void)lycon;
    state->has_grid_area = compute_grid_area_for_absolute(ctx->grid, container,
        state->child_block, &state->grid_area_x, &state->grid_area_y,
        &state->grid_area_width, &state->grid_area_height);
    state->parent_line.left = state->containing_block.padding_x;
    state->parent_block.advance_y = state->containing_block.padding_y;
}

static float grid_abs_axis_position(float area_start, float area_size, float child_size,
                                    LayoutAxisRefs& refs, float* used_size) {
    if (refs.has_start() && refs.has_end()) {
        *used_size = area_size - *refs.insets.start.value - *refs.insets.end.value -
            refs.margin_start() - refs.margin_end();
        return area_start + *refs.insets.start.value + refs.margin_start();
    }
    if (refs.has_start()) {
        return area_start + *refs.insets.start.value +
            (refs.margin_start() > 0.0f ? refs.margin_start() : 0.0f);
    }
    if (refs.has_end()) {
        return area_start + area_size - *refs.insets.end.value - child_size -
            (refs.margin_end() > 0.0f ? refs.margin_end() : 0.0f);
    }
    return area_start;
}

static void layout_grid_abs_after_child(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx, AbsChildLayoutState* state) {
    (void)lycon;  (void)container;
    ViewBlock* child_block = state->child_block;
    if (!child_block || !child_block->position) return;

    LayoutContainingBlock cb = state->containing_block;
    GridContainerLayout* grid_layout = ctx->grid;

    if (state->has_grid_area) {
        LayoutAxisPair<float> area_start =
            layout_axis_pair(state->grid_area_x, state->grid_area_y);
        LayoutAxisPair<float> area_size =
            layout_axis_pair(state->grid_area_width, state->grid_area_height);
        for (int i = LAYOUT_AXIS_X; i <= LAYOUT_AXIS_Y; i++) {
            LayoutAxis axis = (LayoutAxis)i;
            LayoutAxisRefs refs(child_block, axis);
            refs.set_position(grid_abs_axis_position(
                area_start[axis], area_size[axis], refs.get_size(), refs, refs.size_slot));
        }
        return;
    }

    GridItemProp* gi = grid_item_prop(child_block);
    if (!gi) return;

    LayoutAxisPair<float> available = layout_axis_pair(cb.padding_width, cb.padding_height);
    LayoutAxisPair<int> self_alignment = {gi->justify_self, gi->align_self_grid};
    LayoutAxisPair<int> container_alignment = {
        grid_layout ? grid_layout->justify_items : CSS_VALUE_STRETCH,
        grid_layout ? grid_layout->align_items : CSS_VALUE_STRETCH
    };
    for (int i = LAYOUT_AXIS_X; i <= LAYOUT_AXIS_Y; i++) {
        LayoutAxis axis = (LayoutAxis)i;
        LayoutAxisRefs refs(child_block, axis);
        if (refs.has_any_inset()) continue;
        int alignment = axis == LAYOUT_AXIS_X
            ? radiant::resolve_justify_self(self_alignment[axis], container_alignment[axis])
            : radiant::resolve_align_self(self_alignment[axis], container_alignment[axis]);
        float free_space = available[axis] - refs.get_size() -
            refs.margin_start() - refs.margin_end();
        float offset = radiant::compute_alignment_offset_simple(alignment, free_space);
        if (offset != 0.0f) refs.set_position(refs.get_position() + offset);
    }
}

void layout_grid_absolute_children(LayoutContext* lycon, ViewBlock* container) {
    AbsStaticContext ctx = {};
    ctx.kind = ABS_STATIC_GRID;
    ctx.containing_block = layout_containing_block_for_view(container);
    ctx.grid = lycon ? lycon->grid_container : nullptr;
    ctx.prepare_child = layout_grid_abs_prepare_child;
    ctx.after_child = layout_grid_abs_after_child;
    layout_absolute_children_in_context(lycon, container, &ctx);
}
