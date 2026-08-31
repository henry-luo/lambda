#include "layout.hpp"
#include "view.hpp"
#include "../lib/scratch_arena.h"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/css_style_node.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
}

static bool grid_item_ignores_percentage_max_width(ViewBlock* item,
                                                    GridContainerLayout* grid_layout);

static bool grid_item_inline_area_is_definite(const GridItemProp* grid_item,
                                               const GridContainerLayout* grid_layout) {
    if (!grid_item || !grid_layout || !grid_layout->computed_columns) return false;
    int start = grid_item->computed_grid_column_start - 1;
    int end = grid_item->computed_grid_column_end - 1;
    if (start < 0 || end <= start || end > (int)grid_layout->computed_columns->count) {
        return false; // INT_CAST_OK: grid line indices are stored as int; track count is bounded.
    }
    for (int index = start; index < end; index++) {
        const radiant::grid::EnhancedGridTrack& track =
            (*grid_layout->computed_columns)[index];
        if (track.min_track_sizing_function.type != radiant::grid::SizingFunctionType::Length ||
            track.max_track_sizing_function.type != radiant::grid::SizingFunctionType::Length) {
            return false;
        }
    }
    return true;
}

static float grid_item_placed_axis_size(ViewBlock* item, float track_size,
                                        bool horizontal) {
    if (!item || !item->blk) return track_size;

    LayoutAxisRefs axis(item->block_mut(), horizontal);
    if (axis.given_percent && !isnan(*axis.given_percent)) {
        float content_size = track_size * *axis.given_percent / 100.0f;
        *axis.given = content_size;
        return layout_uses_border_box(item)
            ? layout_floor_border_box_axis(item, content_size, horizontal)
            : layout_border_size_from_content_box(item, content_size, horizontal);
    }
    if (axis.given && *axis.given > 0.0f) {
        return layout_css_size_to_border_box(
            item->bound, layout_box_sizing(item), *axis.given, horizontal);
    }
    return track_size;
}

template <typename Tracks>
static float grid_track_total(const Tracks* tracks, int count, float gap) {
    if (!tracks || count <= 0) return 0.0f;
    float total = 0.0f;
    for (int index = 0; index < count; index++) {
        total += (*tracks)[index].base_size;
    }
    return total + (count > 1 ? (count - 1) * gap : 0.0f);
}

template <typename Tracks>
static void grid_track_positions(const Tracks* tracks, int count, float gap,
                                 float offset, float distribution_spacing,
                                 bool add_distribution_spacing, float* positions,
                                 const char* label) {
    if (!tracks || !positions || count < 0) return;
    float current = offset;
    for (int index = 0; index <= count; index++) {
        positions[index] = current;
        if (index >= count) continue;
        float track_size = (*tracks)[index].base_size;
        current += track_size;
        if (index >= count - 1) continue;
        current += gap;
        if (add_distribution_spacing) current += distribution_spacing;
    }
}

typedef struct GridContentDistribution {
    float offset;
    float spacing;
} GridContentDistribution;

static GridContentDistribution grid_content_distribution(int32_t alignment,
                                                          float free_space,
                                                          int track_count) {
    GridContentDistribution result = {0.0f, 0.0f};
    if (track_count <= 0) return result;
    if (free_space > 0.0f && radiant::alignment_is_space_distribution(alignment)) {
        radiant::SpaceDistribution distribution = radiant::compute_space_distribution(
            alignment, free_space, track_count);
        result.offset = distribution.gap_before_first;
        result.spacing = distribution.gap_between;
        return result;
    }
    int32_t effective_alignment = alignment;
    if (free_space < 0.0f && radiant::alignment_is_space_distribution(alignment)) {
        // space distribution falls back to start when the tracks overflow.
        effective_alignment = CSS_VALUE_START;
    }
    result.offset = radiant::compute_alignment_offset_simple(
        effective_alignment, free_space);
    return result;
}

// Position grid items based on computed track sizes
void position_grid_items(GridContainerLayout* grid_layout, ViewBlock* container, ScratchArena* sa) {
    if (!grid_layout || !container) return;

    // guard against zero-track grids — items cannot be positioned (fuzzer-found)
    if (grid_layout->computed_row_count <= 0 || grid_layout->computed_column_count <= 0) {
        return;
    }


    // Calculate track positions
    float* row_positions = (float*)scratch_calloc(sa, (grid_layout->computed_row_count + 1) * sizeof(float));
    float* column_positions = (float*)scratch_calloc(sa, (grid_layout->computed_column_count + 1) * sizeof(float));

    // First, calculate the total grid content size (all tracks + gaps)
    float total_row_size = grid_track_total(
        grid_layout->computed_rows, grid_layout->computed_row_count, grid_layout->row_gap);
    float total_column_size = grid_track_total(
        grid_layout->computed_columns, grid_layout->computed_column_count,
        grid_layout->column_gap);


    // Calculate both track-axis distributions with the same Box Alignment rule.
    float extra_column_space = grid_layout->content_width - total_column_size;
    GridContentDistribution justify_distribution = grid_content_distribution(
        grid_layout->justify_content, extra_column_space,
        grid_layout->computed_column_count);

    float extra_row_space = grid_layout->content_height - total_row_size;
    GridContentDistribution align_distribution = grid_content_distribution(
        grid_layout->align_content, extra_row_space,
        grid_layout->computed_row_count);

    grid_track_positions(
        grid_layout->computed_rows, grid_layout->computed_row_count, grid_layout->row_gap,
        align_distribution.offset, align_distribution.spacing,
        radiant::alignment_is_space_distribution(grid_layout->align_content),
        row_positions, "row");
    grid_track_positions(
        grid_layout->computed_columns, grid_layout->computed_column_count,
        grid_layout->column_gap, justify_distribution.offset, justify_distribution.spacing,
        radiant::alignment_is_space_distribution(grid_layout->justify_content),
        column_positions, "column");

    // Position each grid item
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        GridItemProp* gi = grid_item_prop(item);
        if (!gi) continue;  // Skip items without grid item properties

        // Get grid area bounds (convert from 1-indexed to 0-indexed)
        int row_start = gi->computed_grid_row_start - 1;
        int row_end = gi->computed_grid_row_end - 1;
        int col_start = gi->computed_grid_column_start - 1;
        int col_end = gi->computed_grid_column_end - 1;

        // Clamp to valid ranges
        row_start = fmax(0, fmin(row_start, grid_layout->computed_row_count - 1));
        row_end = fmax(row_start + 1, fmin(row_end, grid_layout->computed_row_count));
        col_start = fmax(0, fmin(col_start, grid_layout->computed_column_count - 1));
        col_end = fmax(col_start + 1, fmin(col_end, grid_layout->computed_column_count));

        // Calculate item position and size
        float item_x = column_positions[col_start];
        float item_y = row_positions[row_start];

        // Calculate track width by summing individual track sizes (not from positions, which include gaps)
        float track_width = 0;
        for (int c = col_start; c < col_end; c++) {
            track_width += (*grid_layout->computed_columns)[c].base_size;
            // Add gap for interior tracks (not the last one in the span)
            if (c < col_end - 1) {
                track_width += grid_layout->column_gap;
            }
        }

        float track_height = 0;
        for (int r = row_start; r < row_end; r++) {
            track_height += (*grid_layout->computed_rows)[r].base_size;
            // Add gap for interior tracks (not the last one in the span)
            if (r < row_end - 1) {
                track_height += grid_layout->row_gap;
            }
        }

        // a grid area's final size is the containing block for stretch min/max.
        // Resolving during intrinsic track sizing incorrectly turns the keyword into 0.
        layout_resolve_stretch_minmax_axis(item, track_width, true, true);
        layout_resolve_stretch_minmax_axis(item, track_height, true, false);

        // Store track area dimensions and base position for alignment phase
        gi->track_area_width = track_width;
        gi->track_area_height = track_height;

        // Determine item dimensions - use CSS-specified size if available,
        // otherwise default to track size (will be adjusted during alignment)
        float item_width = track_width;
        float item_height = track_height;

        // percentage sizes resolve against the final grid area; definite sizes
        // use the same border-box conversion for both physical axes.
        item_width = grid_item_placed_axis_size(item, track_width, true);
        item_height = grid_item_placed_axis_size(item, track_height, false);

        // grid positioning uses border-box geometry; content-box constraints must
        // be converted before clamping rather than compared to a border-box size.
        if (item->blk) {
            item_width = layout_apply_min_max_border_box_axis(
                item, item_width, true,
                grid_item_ignores_percentage_max_width(item, grid_layout));
            item_height = layout_apply_min_max_border_box_axis(item, item_height, false);
        }

        // Apply container offset (borders and padding)
        float container_offset_x = 0;
        float container_offset_y = 0;

        if (container->bound) {
            container_offset_x += container->boundary()->padding.left;
            container_offset_y += container->boundary()->padding.top;
        }

        if (container->bound && container->boundary_mut()->border) {
            container_offset_x += container->boundary()->border->width.left;
            container_offset_y += container->boundary()->border->width.top;
        }

        // Set item position and size (relative to parent's border box, per Radiant coordinate system)
        float new_x = container_offset_x + item_x;
        float new_y = container_offset_y + item_y;

        // Store base track position (before alignment) for later re-alignment
        gi->track_base_x = new_x;
        gi->track_base_y = new_y;

        item->x = new_x;
        item->y = new_y;
        item->width = item_width;
        item->height = item_height;


    }

    scratch_free(sa, column_positions);
    scratch_free(sa, row_positions);

}

// Helper: read the CSS percentage value for a horizontal margin side (left or right).
// If the margin declared in specified_style was a percentage, returns pct/100 * track_width.
// Returns the pre-resolved value from bound for fixed lengths, 0 for auto/absent.
static float resolve_margin_side(ViewBlock* item, CssBoxSide side, float track_width) {
    if (!item || !item->bound) return 0.0f;

    Margin* margin = &item->boundary_mut()->margin;
    float bound_val = *radiant_spacing_value(margin, side);
    CssEnum bound_type = *radiant_margin_type(margin, side);

    if (bound_type == CSS_VALUE_AUTO) return 0.0f;

    if (bound_type != CSS_VALUE__PERCENTAGE) {
        // Fixed length margin - use pre-resolved value
        return bound_val;
    }

    // Percentage margin: re-resolve against the actual track width.
    // CSS §11.4: for grid items, percentage margins resolve against the inline size of the grid area.
    // Try individual property first.
    static const CssPropertyCode side_props[] = {
        CSS_PROPERTY_MARGIN_TOP, CSS_PROPERTY_MARGIN_RIGHT,
        CSS_PROPERTY_MARGIN_BOTTOM, CSS_PROPERTY_MARGIN_LEFT
    };
    if (item->specified_style) {
        CssDeclaration* decl = style_tree_get_declaration(item->specified_style, side_props[side]);
        if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            return (float)(decl->value->data.percentage.value / 100.0) * track_width;
        }
        // Not set individually — check the shorthand margin property.
        CssDeclaration* sh = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN);
        if (sh && sh->value) {
            CssValue* pval = (CssValue*)css_box_shorthand_side_value(
                sh->value, side);
            if (pval && pval->type == CSS_VALUE_TYPE_PERCENTAGE) {
                return (float)(pval->data.percentage.value / 100.0) * track_width;
            }
        }
    }
    return 0.0f;
}

static bool grid_item_ignores_percentage_max_width(ViewBlock* item,
                                                    GridContainerLayout* grid_layout) {
    return item && item->blk && grid_layout && grid_layout->is_shrink_to_fit_width &&
        !isnan(item->block()->given_max_width_percent) &&
        layout_intrinsic_min_size_keyword(item, true) == CSS_VALUE_MIN_CONTENT;
}

static float compute_grid_item_alignment_baseline(::LayoutContext* lycon, ViewBlock* item) {
    float baseline = radiant::compute_element_first_baseline(lycon, item, true);
    if (!item || baseline < 0.0f) return baseline;

    // Child-derived baselines can carry absolute y after an earlier alignment
    // pass; grid row math needs the baseline relative to the item's border box.
    if (baseline > item->height && item->y > 0.0f) {
        float relative_baseline = baseline - item->y;
        if (relative_baseline >= 0.0f) {
            baseline = relative_baseline;
        }
    }
    return baseline;
}

static float compute_grid_item_baseline_alignment_height(GridContainerLayout* grid_layout,
                                                         ViewBlock* item) {
    if (!grid_layout || !item) return 0.0f;
    if (item->blk && item->block_mut()->given_height >= 0.0f) return item->height;

    IntrinsicSizes sizes = calculate_grid_item_intrinsic_sizes(
        grid_layout->lycon, item, true);
    float intrinsic_height = sizes.max_content;
    if (item->content_height > intrinsic_height) {
        intrinsic_height = item->content_height;
    }
    return intrinsic_height > 0.0f ? intrinsic_height : item->height;
}

// Align all grid items
void align_grid_items(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;


    // Pre-pass: baseline alignment for rows with align-items: baseline
    bool has_baseline_alignment = radiant::alignment_is_baseline(grid_layout->align_items);
    if (!has_baseline_alignment) {
        for (int i = 0; i < grid_layout->item_count; i++) {
            ViewBlock* item = grid_layout->grid_items[i];
            GridItemProp* gi = grid_item_prop(item);
            if (item && gi) {
                int resolved = radiant::resolve_align_self(
                    gi->align_self_grid, grid_layout->align_items);
                if (radiant::alignment_is_baseline(resolved)) {
                    has_baseline_alignment = true;
                    break;
                }
            }
        }
    }

    if (has_baseline_alignment && grid_layout->computed_row_count > 0) {
        int row_count = grid_layout->computed_row_count;
        ScratchArena* sa = &grid_layout->lycon->scratch;
        float* row_max_baseline = (float*)scratch_calloc(sa, row_count * sizeof(float));
        float* row_max_below = (float*)scratch_calloc(sa, row_count * sizeof(float));
        int* row_baseline_count = (int*)scratch_calloc(sa, row_count * sizeof(int));
        float* item_baseline_shift = (float*)scratch_calloc(
            sa, grid_layout->item_count * sizeof(float));

        // First pass: compute baselines and find per-row max above/below baseline
        for (int i = 0; i < grid_layout->item_count; i++) {
            ViewBlock* item = grid_layout->grid_items[i];
            GridItemProp* gi = grid_item_prop(item);
            if (!item || !gi) continue;

            int resolved = radiant::resolve_align_self(
                gi->align_self_grid, grid_layout->align_items);
            if (!radiant::alignment_is_baseline(resolved)) continue;

            int row_idx = gi->computed_grid_row_start - 1;
            if (row_idx < 0 || row_idx >= row_count) continue;
            row_baseline_count[row_idx]++;

            float baseline = compute_grid_item_alignment_baseline(grid_layout->lycon, item);
            if (baseline < 0) baseline = item->height;

            // Baseline track sizing uses the item's auto block size, not its
            // provisional track-stretched height from the positioning pass.
            float alignment_height = compute_grid_item_baseline_alignment_height(
                grid_layout, item);
            float below = alignment_height - baseline;
            if (below < 0.0f) below = 0.0f;
            if (baseline > row_max_baseline[row_idx])
                row_max_baseline[row_idx] = baseline;
            if (below > row_max_below[row_idx])
                row_max_below[row_idx] = below;
        }

        // Check if any row needs to resize for baseline alignment
        bool rows_changed = false;
        for (int r = 0; r < row_count; r++) {
            if (row_baseline_count[r] <= 0) continue;
            float needed = row_max_baseline[r] + row_max_below[r];
            radiant::grid::EnhancedGridTrack* row_track = &(*grid_layout->computed_rows)[r];
            bool track_allows_content_growth =
                radiant::grid::grid_track_allows_content_growth(*row_track);
            bool has_baseline_group = row_baseline_count[r] > 1;
            bool should_resize = has_baseline_group
                ? fabsf(needed - row_track->base_size) > 0.5f
                : needed > row_track->base_size + 0.5f;
            if (track_allows_content_growth && should_resize) {
                // True baseline-sharing groups need their fitted row size recomputed
                // across passes; single-item rows keep align-content stretch space.
                row_track->base_size = needed;
                rows_changed = true;
            }
        }

        // If rows changed, re-position all items to account for new row positions
        if (rows_changed) {
            float row_gap = grid_layout->row_gap;
            // Get container padding/border offset
            float content_top_offset = 0;
            if (grid_layout->item_count > 0 && grid_layout->grid_items[0]) {
                View* first_item = static_cast<View*>(grid_layout->grid_items[0]);
                ViewBlock* container = first_item->parent ? lam::view_as_block(first_item->parent) : NULL;
                if (container) {
                    if (container->bound) {
                        content_top_offset += container->boundary()->padding.top;
                        if (container->boundary()->border)
                            content_top_offset += container->boundary()->border->width.top;
                    }
                }
            }

            // Recompute row offsets
            float row_offset = 0;
            for (int r = 0; r < row_count; r++) {
                (*grid_layout->computed_rows)[r].offset = row_offset;
                row_offset += (*grid_layout->computed_rows)[r].base_size + row_gap;
            }

            // Re-position items based on new row offsets
            for (int i = 0; i < grid_layout->item_count; i++) {
                ViewBlock* item = grid_layout->grid_items[i];
                GridItemProp* gi = grid_item_prop(item);
                if (!item || !gi) continue;

                int row_idx = gi->computed_grid_row_start - 1;
                if (row_idx < 0 || row_idx >= row_count) continue;

                gi->track_base_y = (*grid_layout->computed_rows)[row_idx].offset
                    + content_top_offset;

                // Recompute track_area_height for multi-row items
                int row_end_idx = gi->computed_grid_row_end - 1;
                if (row_end_idx >= row_count) row_end_idx = row_count - 1;
                if (row_end_idx < row_idx) row_end_idx = row_idx;
                float area_h = 0;
                for (int r = row_idx; r < row_end_idx; r++) {
                    area_h += (*grid_layout->computed_rows)[r].base_size;
                    if (r < row_end_idx - 1) area_h += row_gap;
                }
                gi->track_area_height = area_h;
            }
        }

        // Baseline alignment can run more than once as grid content is laid out;
        // keep the shim transient so track_base_y remains the unaligned track origin.
        for (int i = 0; i < grid_layout->item_count; i++) {
            ViewBlock* item = grid_layout->grid_items[i];
            GridItemProp* gi = grid_item_prop(item);
            if (!item || !gi) continue;

            int resolved = radiant::resolve_align_self(
                gi->align_self_grid, grid_layout->align_items);
            if (!radiant::alignment_is_baseline(resolved)) continue;

            int row_idx = gi->computed_grid_row_start - 1;
            if (row_idx < 0 || row_idx >= row_count) continue;

            float baseline = compute_grid_item_alignment_baseline(grid_layout->lycon, item);
            if (baseline < 0) baseline = item->height;

            float shift = row_max_baseline[row_idx] - baseline;
            if (shift > 0.01f) {
                item_baseline_shift[i] = shift;
            }
        }

        for (int i = 0; i < grid_layout->item_count; i++) {
            ViewBlock* item = grid_layout->grid_items[i];
            GridItemProp* gi = grid_item_prop(item);
            if (!item || !gi) continue;
            float base_y = gi->track_base_y;
            gi->track_base_y = base_y + item_baseline_shift[i];
            align_grid_item(item, grid_layout);
            gi->track_base_y = base_y;
        }

        scratch_free(sa, item_baseline_shift);
        scratch_free(sa, row_baseline_count);
        scratch_free(sa, row_max_below);
        scratch_free(sa, row_max_baseline);
        return;
    }

    for (int i = 0; i < grid_layout->item_count; i++) {
        align_grid_item(grid_layout->grid_items[i], grid_layout);
    }

}

static bool grid_axis_stretches_aspect_ratio(int self_alignment,
                                              int container_alignment) {
    if (self_alignment == CSS_VALUE_STRETCH) return true;
    if (self_alignment == CSS_VALUE_AUTO || self_alignment == CSS_VALUE__UNDEF) {
        return container_alignment == CSS_VALUE_STRETCH;
    }
    // CSS Box Alignment: normal behaves as start for an aspect-ratio grid item.
    return false;
}

static int resolve_grid_item_self_alignment(const ViewBlock* item,
                                            const GridItemProp* grid_item,
                                            int container_alignment,
                                            bool horizontal) {
    CssEnum self_alignment = horizontal ? (CssEnum)grid_item->justify_self
                                        : (CssEnum)grid_item->align_self_grid;
    int resolved = horizontal
        ? radiant::resolve_justify_self(self_alignment, container_alignment)
        : radiant::resolve_align_self(self_alignment, container_alignment);
    bool resolves_from_normal = self_alignment == CSS_VALUE_NORMAL ||
        ((self_alignment == CSS_VALUE_AUTO || self_alignment == CSS_VALUE__UNDEF) &&
         container_alignment == CSS_VALUE_NORMAL);
    if (resolves_from_normal && item->display.inner == RDT_DISPLAY_REPLACED &&
        !item->form) {
        // Native controls use the replaced layout path internally, but their outer
        // widget boxes still stretch under normal grid alignment in browsers.
        return CSS_VALUE_START;
    }
    return resolved;
}

static CssSelfAlignment resolve_grid_item_self_alignment_detail(
        ViewBlock* item, const GridItemProp* grid_item,
        ViewBlock* container, int container_alignment, bool horizontal) {
    CssSelfAlignment result = {};
    if (!item || !grid_item) return result;

    // GridItemProp keeps the effective keyword for track sizing; style storage
    // preserves the safe/unsafe modifier needed by the final positioning pass.
    result = horizontal ? item->block()->justify_self : item->block()->align_self;
    CssEnum requested = horizontal ? (CssEnum)grid_item->justify_self
                                   : (CssEnum)grid_item->align_self_grid;
    if (horizontal && (requested == CSS_VALUE_AUTO || requested == CSS_VALUE__UNDEF) &&
        container && container->embed && container->embedp()->grid) {
        result = container->embedp()->grid->justify_items_detail;
    }
    result.value = requested;
    int resolved = resolve_grid_item_self_alignment(
        item, grid_item, container_alignment, horizontal);
    result.value = static_cast<CssEnum>(resolved);

    if ((result.value == CSS_VALUE_SELF_START || result.value == CSS_VALUE_SELF_END) &&
        container) {
        CssSelfAlignment logical = result;
        logical.value = requested;
        CssEnum physical = layout_resolve_self_alignment_value(
            logical, item, container, horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y);
        if (physical != CSS_VALUE__UNDEF) result.value = physical;
    }
    return result;
}

static float grid_item_intrinsic_minimum_size(GridContainerLayout* grid_layout,
                                              ViewBlock* item,
                                              GridItemProp* grid_item,
                                              bool horizontal) {
    if (!item || !grid_item) return 0.0f;
    CssEnum keyword = layout_intrinsic_min_size_keyword(item, horizontal);
    if (keyword != CSS_VALUE_MIN_CONTENT && keyword != CSS_VALUE_MAX_CONTENT) return 0.0f;

    if (horizontal && grid_item->has_measured_size) {
        return keyword == CSS_VALUE_MIN_CONTENT ? grid_item->measured_min_width
                                                : grid_item->measured_max_width;
    }

    if (!grid_layout || !grid_layout->lycon) return 0.0f;
    // Row measurements are deferred until column tracks have their final widths,
    // so an intrinsic min-height cannot use the width-only pass-one cache.
    IntrinsicSizes sizes = calculate_grid_item_intrinsic_sizes(
        grid_layout->lycon, item, !horizontal);
    return keyword == CSS_VALUE_MIN_CONTENT ? sizes.min_content : sizes.max_content;
}

static bool grid_item_has_cyclic_replaced_percentage_block_size(ViewBlock* item) {
    return item && item->display.inner == RDT_DISPLAY_REPLACED &&
        layout_axis_size_is_percentage(item, false);
}

static bool grid_stretch_axis(ViewBlock* item, LayoutAxis axis, float available,
                              float maximum, bool has_explicit_size,
                              bool use_aspect_ratio) {
    if (!item || has_explicit_size || use_aspect_ratio) return false;
    layout_axis_set_size(static_cast<ViewElement*>(item), axis, available);

    LayoutAxisRefs constraints(item->block_mut(), axis);
    LayoutAxisBoxMetrics box = layout_axis_box_metrics(item, axis);
    if (maximum > 0.0f) {
        float border_box_max = maximum;
        if (!layout_uses_border_box(item)) border_box_max += box.pad_border;
        else if (border_box_max < box.pad_border) border_box_max = box.pad_border;
        if (layout_axis_size(static_cast<ViewElement*>(item), axis) > border_box_max) {
            layout_axis_set_size(static_cast<ViewElement*>(item), axis, border_box_max);
        }
    }
    if (constraints.minimum && *constraints.minimum > 0.0f) {
        float border_box_min = *constraints.minimum;
        if (!layout_uses_border_box(item)) border_box_min += box.pad_border;
        if (layout_axis_size(static_cast<ViewElement*>(item), axis) < border_box_min) {
            layout_axis_set_size(static_cast<ViewElement*>(item), axis, border_box_min);
        }
    }
    return true;
}

// Align a single grid item
void align_grid_item(ViewBlock* item, GridContainerLayout* grid_layout) {
    GridItemProp* gi = grid_item_prop(item);
    if (!item || !grid_layout || !gi) return;

    float old_x = item->x;
    float old_y = item->y;

    // Reset to base track position before applying alignment
    // This allows align_grid_item to be called multiple times (e.g., after content layout)
    item->x = gi->track_base_x;
    item->y = gi->track_base_y;

    // Use stored track area dimensions from positioning phase
    float available_width = gi->track_area_width;
    float available_height = gi->track_area_height;

    // CSS Grid §8.1: fixed (non-auto) margins reduce the usable grid area for alignment.
    // The item's alignment box = grid area minus fixed margins.  Auto margins are handled
    // separately (they consume the remaining free space after fixed-margin reduction).
    // Percentage margins are resolved against the actual track width (CSS §11.4).
    float margin_left   = resolve_margin_side(item, CSS_BOX_SIDE_LEFT, available_width);
    float margin_right  = resolve_margin_side(item, CSS_BOX_SIDE_RIGHT, available_width);
    float margin_top    = resolve_margin_side(item, CSS_BOX_SIDE_TOP, available_width);
    float margin_bottom = resolve_margin_side(item, CSS_BOX_SIDE_BOTTOM, available_width);

    // CSS Box 4 §3.2 margin-trim for grid containers: trim margins of items
    // adjacent to the container edges.
    ViewBlock* grid_container = item->parent && item->parent->is_block() ? lam::view_require_block(item->parent) : NULL;
    uint8_t grid_margin_trim = (grid_container && grid_container->blk) ? grid_container->block()->margin_trim : 0;
    if (grid_margin_trim) {
        const LayoutAxis axes[2] = {LAYOUT_AXIS_X, LAYOUT_AXIS_Y};
        const uint8_t trim_start[2] = {
            MARGIN_TRIM_INLINE_START, MARGIN_TRIM_BLOCK_START};
        const uint8_t trim_end[2] = {
            MARGIN_TRIM_INLINE_END, MARGIN_TRIM_BLOCK_END};
        const int starts[2] = {
            gi->computed_grid_column_start - 1,
            gi->computed_grid_row_start - 1};
        const int ends[2] = {
            gi->computed_grid_column_end - 1,
            gi->computed_grid_row_end - 1};
        const int track_counts[2] = {
            grid_layout->computed_column_count,
            grid_layout->computed_row_count};
        for (int i = 0; i < 2; i++) {
            LayoutAxisRefs refs(item, axes[i]);
            if ((grid_margin_trim & trim_start[i]) && starts[i] == 0) {
                if (refs.margins.start) *refs.margins.start = 0.0f;
            }
            if ((grid_margin_trim & trim_end[i]) && ends[i] >= track_counts[i]) {
                if (refs.margins.end) *refs.margins.end = 0.0f;
            }
        }
        LayoutAxisRefs x_margins(item, LAYOUT_AXIS_X);
        LayoutAxisRefs y_margins(item, LAYOUT_AXIS_Y);
        margin_left = x_margins.margin_start();
        margin_right = x_margins.margin_end();
        margin_top = y_margins.margin_start();
        margin_bottom = y_margins.margin_end();
    }

    // Reduce available area by the fixed margins before any alignment calculation
    available_width  -= margin_left + margin_right;
    available_height -= margin_top  + margin_bottom;
    if (available_width  < 0) available_width  = 0;
    if (available_height < 0) available_height = 0;
    // Offset the base position so alignment operates inside the margin box
    item->x += margin_left;
    item->y += margin_top;

    // Grid items share the aspect-ratio resolver with intrinsic sizing so an
    // image's `auto <ratio>` uses its natural ratio after the image has loaded.
    float aspect_ratio = layout_used_preferred_aspect_ratio(item);
    // CSS Grid §11.7: Stretch only applies when the item's size is 'auto'.
    // Intrinsic sizing keywords (fit-content, min-content, max-content) are NOT auto
    // and should prevent stretch alignment.
    auto is_intrinsic_keyword = [](CssEnum kw) {
        return kw == CSS_VALUE_FIT_CONTENT ||
               kw == CSS_VALUE_MIN_CONTENT ||
               kw == CSS_VALUE_MAX_CONTENT;
    };
    bool has_explicit_width = (item->blk && (item->block()->given_width > 0 ||
        is_intrinsic_keyword(item->block()->given_width_type)));
    bool has_intrinsic_height_constraint =
        layout_intrinsic_min_size_keyword(item, false) != CSS_VALUE__UNDEF ||
        layout_intrinsic_max_size_keyword(item, false) != CSS_VALUE__UNDEF;
    bool has_explicit_height = (item->blk &&
        (item->block()->given_height > 0 ||
         is_intrinsic_keyword(item->block()->given_height_type)) &&
        !has_intrinsic_height_constraint);
    float max_width = (item->blk && item->block_mut()->given_max_width > 0) ? item->block_mut()->given_max_width : 0;
    float max_height = (item->blk && item->block_mut()->given_max_height > 0) ? item->block_mut()->given_max_height : 0;
    float min_width = (item->blk && item->block_mut()->given_min_width > 0) ? item->block_mut()->given_min_width : 0;
    float min_height = (item->blk && item->block_mut()->given_min_height > 0) ? item->block_mut()->given_min_height : 0;
    bool definite_inline_area = grid_item_inline_area_is_definite(gi, grid_layout);
    if (definite_inline_area && item->blk && !isnan(item->block()->given_max_width_percent)) {
        // Grid-item percentage constraints resolve against the grid area, not
        // the auto-sized grid container used during the intrinsic pass.
        max_width = gi->track_area_width * item->block()->given_max_width_percent / 100.0f;
    }

    bool inline_stretches_ratio = grid_axis_stretches_aspect_ratio(
        gi->justify_self, grid_layout->justify_items);
    bool block_stretches_ratio = grid_axis_stretches_aspect_ratio(
        gi->align_self_grid, grid_layout->align_items);
    // A stretch axis combined with a definite opposite axis determines both
    // used sizes, so CSS Sizing §4.2 does not apply the preferred ratio.
    bool ratio_is_ignored = (inline_stretches_ratio && block_stretches_ratio) ||
        (has_explicit_width && block_stretches_ratio) ||
        (has_explicit_height && inline_stretches_ratio);
    bool use_aspect_ratio = aspect_ratio > 0.0f && !ratio_is_ignored;


    // If aspect-ratio is set, choose one primary axis and derive the other.
    if (use_aspect_ratio) {
        LayoutAxisRefs width_refs(item, LAYOUT_AXIS_X);
        LayoutAxisRefs height_refs(item, LAYOUT_AXIS_Y);
        if (has_explicit_width && !has_explicit_height) {
            height_refs.set_size(width_refs.get_size() / aspect_ratio);
        } else if (!has_explicit_width && has_explicit_height) {
            width_refs.set_size(height_refs.get_size() * aspect_ratio);
        } else if (!has_explicit_width) {
            LayoutAxis primary = LAYOUT_AXIS_X;
            if (max_width > 0.0f && max_height > 0.0f) {
                primary = max_width / aspect_ratio <= max_height
                    ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
            } else if (max_height > 0.0f ||
                       (block_stretches_ratio && !inline_stretches_ratio) ||
                       (min_height > 0.0f && min_width == 0.0f)) {
                primary = LAYOUT_AXIS_Y;
            }
            float primary_size;
            if (primary == LAYOUT_AXIS_X) {
                primary_size = max_width > 0.0f ? max_width : available_width;
                width_refs.set_size(primary_size);
                height_refs.set_size(primary_size / aspect_ratio);
            } else {
                primary_size = max_height > 0.0f ? max_height : available_height;
                height_refs.set_size(primary_size);
                width_refs.set_size(primary_size * aspect_ratio);
            }
        }

        const float maximums[2] = {max_width, max_height};
        const float minimums[2] = {min_width, min_height};
        const bool explicit_sizes[2] = {has_explicit_width, has_explicit_height};
        LayoutAxisRefs sizes[2] = {width_refs, height_refs};
        for (int i = 0; i < 2; i++) {
            if (maximums[i] > 0.0f && sizes[i].get_size() > maximums[i]) {
                sizes[i].set_size(maximums[i]);
                int other = 1 - i;
                if (!explicit_sizes[other]) {
                    sizes[other].set_size(i == 0
                        ? maximums[i] / aspect_ratio : maximums[i] * aspect_ratio);
                }
            }
            if (minimums[i] > 0.0f && sizes[i].get_size() < minimums[i]) {
                sizes[i].set_size(minimums[i]);
                int other = 1 - i;
                if (!explicit_sizes[other]) {
                    sizes[other].set_size(i == 0
                        ? minimums[i] / aspect_ratio : minimums[i] * aspect_ratio);
                }
            }
        }
    }

    float intrinsic_min_width = grid_item_intrinsic_minimum_size(grid_layout, item, gi, true);
    float intrinsic_min_height = grid_item_intrinsic_minimum_size(grid_layout, item, gi, false);
    bool cyclic_replaced_percentage_height =
        grid_item_has_cyclic_replaced_percentage_block_size(item);
    // Intrinsic min-size keywords floor the used grid item size after its
    // percentage size resolves; treating them as zero loses that CSS constraint.
    if (intrinsic_min_width > item->width) item->width = intrinsic_min_width;
    // CSS Sizing 3 §5.2.1: a replaced cyclic percentage is zeroed only for its
    // intrinsic contribution; once grid has resolved the track, its used size
    // must not be replaced by the intrinsic keyword contribution.
    if (!cyclic_replaced_percentage_height && intrinsic_min_height > item->height) {
        item->height = intrinsic_min_height;
    }
    if (item->blk && is_intrinsic_keyword(item->block()->given_width_type)) {
        IntrinsicSizes intrinsic_width = calculate_grid_item_intrinsic_sizes(
            grid_layout->lycon, item, false);
        CssEnum width_keyword = item->block()->given_width_type;
        float preferred_width = layout_resolve_intrinsic_size_keyword(
            width_keyword, intrinsic_width.min_content, intrinsic_width.max_content,
            available_width);
        // A preferred intrinsic width is explicit for alignment, but placement
        // still starts from the track size; restore the preferred contribution first.
        if (preferred_width > 0.0f) item->width = preferred_width;
    }
    if (!has_explicit_height && has_intrinsic_height_constraint &&
        !cyclic_replaced_percentage_height) {
        IntrinsicSizes intrinsic_height = calculate_grid_item_intrinsic_sizes(
            grid_layout->lycon, item, true);
        // A constrained intrinsic block size must replace the provisional CSS
        // height before non-stretch alignment preserves that provisional value.
        if (intrinsic_height.max_content > 0.0f) {
            item->height = intrinsic_height.max_content;
        }
    }

    // P6: Auto margins override justify-self/align-self, consuming available free space (CSS Grid §8.1)
    bool applied_horiz_auto = false, applied_vert_auto = false;
    if (item->bound) {
        const LayoutAxis axes[2] = {LAYOUT_AXIS_X, LAYOUT_AXIS_Y};
        const bool explicit_sizes[2] = {has_explicit_width, has_explicit_height};
        const float available[2] = {available_width, available_height};
        const float content[2] = {item->content_width, item->content_height};
        bool* applied[2] = {&applied_horiz_auto, &applied_vert_auto};
        for (int i = 0; i < 2; i++) {
            LayoutAxisRefs refs(item, axes[i]);
            bool start_auto = refs.margins.start_type &&
                *refs.margins.start_type == CSS_VALUE_AUTO;
            bool end_auto = refs.margins.end_type &&
                *refs.margins.end_type == CSS_VALUE_AUTO;
            if (!start_auto && !end_auto) continue;
            *applied[i] = true;
            bool content_is_usable = i == 0
                ? content[i] > 0.0f && content[i] < available[i]
                // An intrinsic block size equal to the track still occupies
                // the track; rejecting it turns auto margins into a zero-size box.
                : content[i] >= 0.0f && content[i] <= available[i];
            float actual = explicit_sizes[i] && refs.given ? *refs.given
                : (content_is_usable ? content[i] : refs.get_size());
            float free_space = available[i] - actual;
            refs.set_size(actual);
            if (start_auto && end_auto) {
                refs.set_position(refs.get_position() + free_space * 0.5f);
            } else if (start_auto) {
                refs.set_position(refs.get_position() + free_space);
            }
        }
    }

    // Apply justify-self (horizontal alignment)
    // Using unified resolve function from layout_alignment.hpp
    CssSelfAlignment justify_alignment = resolve_grid_item_self_alignment_detail(
        item, gi, grid_container, grid_layout->justify_items, true);
    int justify = justify_alignment.value;

    // For non-stretch alignment, an auto-sized grid item uses fit-content sizing.
    // Its max-content width is capped by the grid area but never below min-content.
    float actual_width = item->width;
    if (!applied_horiz_auto && justify != CSS_VALUE_STRETCH && !has_explicit_width) {
        if (gi->has_measured_size) {
            actual_width = fmaxf(gi->measured_min_width,
                                 fminf(gi->measured_max_width, available_width));
            actual_width = layout_apply_min_max_border_box_axis(
                item, actual_width, true,
                grid_item_ignores_percentage_max_width(item, grid_layout));
            item->width = actual_width;
        } else if (item->content_width > 0) {
            // Without an intrinsic measurement, the laid-out content is the only
            // available max-content estimate, so do not let it bypass the track.
            actual_width = fminf(item->content_width, available_width);
            item->width = actual_width;
        }
    }

    auto align_axis = [&](LayoutAxis axis, CssSelfAlignment alignment, bool auto_margin,
                          bool explicit_size, float available, float actual,
                          float maximum) {
        if (auto_margin) return;
        if (!radiant::alignment_is_stretch(alignment.value)) {
            layout_axis_set_pos(static_cast<ViewElement*>(item), axis,
                layout_axis_pos(static_cast<ViewElement*>(item), axis) +
                // CSS Align 3 §6 resolves logical start/end against the grid's axis.
                layout_self_alignment_offset(alignment.value, alignment.safe,
                                             grid_container, axis, available - actual));
            return;
        }
        if (!grid_stretch_axis(item, axis, available, maximum, explicit_size,
                               use_aspect_ratio)) {
            // CSS Align 3 §5.3: an ineligible stretch falls back to logical start.
            layout_axis_set_pos(static_cast<ViewElement*>(item), axis,
                layout_axis_pos(static_cast<ViewElement*>(item), axis) +
                layout_self_alignment_offset(CSS_VALUE_START, false, grid_container,
                                             axis, available - actual));
        }
    };
    align_axis(LAYOUT_AXIS_X, justify_alignment, applied_horiz_auto, has_explicit_width,
               available_width, actual_width, max_width);

    if (definite_inline_area && max_width > 0.0f && item->width > 0.0f) {
        float max_width_border_box = layout_css_size_to_border_box(
            item->bound, layout_box_sizing(item), max_width, true);
        if (item->width > max_width_border_box) {
            // Intrinsic width keywords still obey a definite max-width after
            // grid alignment has selected the item's used size.
            item->width = max_width_border_box;
        }
    }

    // Apply align-self (vertical alignment)
    // Using unified resolve function from layout_alignment.hpp
    CssSelfAlignment align_alignment = resolve_grid_item_self_alignment_detail(
        item, gi, grid_container, grid_layout->align_items, false);
    int align = align_alignment.value;

    // For non-stretch alignment, use content height if available (set by Pass 3 content layout)
    // This allows center/start/end to work correctly with intrinsic content size
    float actual_height = item->height;
    if (!applied_vert_auto && align != CSS_VALUE_STRETCH && !has_explicit_height &&
        !cyclic_replaced_percentage_height) {
        // Use content height if it was computed in Pass 3
        // Content height should be used regardless of whether it's smaller or larger
        // than available height - the item should size to its content for non-stretch alignment
        if (item->content_height > 0) {
            actual_height = item->content_height;
            item->height = actual_height;
        } else {
            // Empty replaced items bypass flow measurement; retaining the track size
            // here makes an auto-sized item fill a finite row despite start alignment.
            IntrinsicSizes intrinsic_height = calculate_grid_item_intrinsic_sizes(
                grid_layout->lycon, item, true);
            if (intrinsic_height.max_content > 0.0f) {
                actual_height = intrinsic_height.max_content;
                item->height = actual_height;
            }
        }
    }

    // CSS Grid §11.7 / CSS Box Alignment §5.3: intrinsic sizing keywords
    // (fit-content, min-content, max-content) are NOT auto — the item should
    // NOT be stretched even when align-self resolves to stretch.
    // Use content_height which was computed during Pass 3 content layout.
    bool has_intrinsic_height = item->blk &&
        is_intrinsic_keyword(item->block()->given_height_type);
    if (!applied_vert_auto && has_intrinsic_height && item->content_height > 0 &&
        !cyclic_replaced_percentage_height) {
        actual_height = item->content_height;
        item->height = actual_height;
    }

    align_axis(LAYOUT_AXIS_Y, align_alignment, applied_vert_auto, has_explicit_height,
               available_height, actual_height, max_height);

    bool has_block_flow_child = false;
    for (View* child = lam::view_require_element(item)->first_placed_child();
         child; child = child->next()) {
        if (child->view_type == RDT_VIEW_BLOCK ||
            child->view_type == RDT_VIEW_LIST_ITEM ||
            child->view_type == RDT_VIEW_TABLE ||
            child->view_type == RDT_VIEW_TABLE_CELL) {
            has_block_flow_child = true;
            break;
        }
    }
    if (!has_block_flow_child && (item->display.inner == CSS_VALUE_FLOW ||
                                  item->display.inner == CSS_VALUE_FLOW_ROOT)) {
        CssEnum text_align = item->blk ? item->block()->text_align : CSS_VALUE_START;
        if (text_align == CSS_VALUE_START) {
            text_align = item->blk && item->block()->direction == CSS_VALUE_RTL
                ? CSS_VALUE_RIGHT : CSS_VALUE_LEFT;
        } else if (text_align == CSS_VALUE_END) {
            text_align = item->blk && item->block()->direction == CSS_VALUE_RTL
                ? CSS_VALUE_LEFT : CSS_VALUE_RIGHT;
        }
        if (text_align == CSS_VALUE_CENTER || text_align == CSS_VALUE_RIGHT) {
            // CSS Grid §11.7: auto margins can resolve an item's used width
            // after its provisional content line was laid out; realign that
            // deferred line in the final content box.
            LayoutContentBox final_content = layout_content_box(item);
            layout_align_deferred_inline_line_runs(
                lam::view_require_element(item), final_content.width, text_align);
        }
    }

    layout_shift_static_positioned_abs_descendants(
        lam::view_require_element(item), item->x - old_x, item->y - old_y);
}
