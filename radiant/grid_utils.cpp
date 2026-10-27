#include "layout.hpp"
#include "grid_track.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/css_parser.hpp"
#include "../lib/mem_grow.hpp"
#include "../lib/tagged.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
}

bool grid_append_track_line_names(StringBuf* output, const GridLineNames* names,
        int line_index, bool leading_space) {
    bool named = false;
    if (names) for (int i = 0; i < names->count; i++) {
        const GridNamedLine& name = names->entries[i];
        if (name.line_index != line_index) continue;
        size_t length = strlen(name.name);
        if (length > (SIZE_MAX - output->length - 4) / 7 ||
            !stringbuf_ensure_cap(output, output->length + length * 7 + 4)) return false;
        stringbuf_append_str(output, named ? " " : leading_space ? " [" : "[");
        css_append_identifier(output, name.name, length);
        named = true;
    }
    if (named) stringbuf_append_str(output, "]");
    return true;
}

bool grid_serialize_used_tracks(StringBuf* output, const GridContainerLayout* grid, bool columns) {
    if (!output || !grid || grid->storage_failed) return false;
    const radiant::grid::TrackArray* tracks = columns ? grid->computed_columns.get() : grid->computed_rows.get();
    const GridTrackList* definitions = columns ? grid->grid_template_columns.get() : grid->grid_template_rows.get();
    int negative = columns ? grid->negative_implicit_column_count : grid->negative_implicit_row_count;
    if (!tracks || !tracks->good()) return false;
    stringbuf_reset(output);
    for (size_t line = 0; line <= tracks->size(); line++) {
        if (definitions && !grid_append_track_line_names(output, &definitions->line_names,
            static_cast<int>(line) - negative, output->length > 0)) return false; // INT_CAST_OK: bounded track index.
        if (line < tracks->size() && !stringbuf_append_format(output, "%s%.6gpx",
            output->length ? " " : "", (*tracks)[line].base_size)) return false;
    }
    if (!output->length) stringbuf_append_str(output, "none");
    return output->length > 0;
}

// line identity is a set of decoded names at a line, including repeat-boundary unions.
bool grid_add_track_line_name(Pool* pool, ScratchArena* scratch, GridLineNames* names,
                              const char* name, int line_index) {
    if (!names || !name || (!pool && !scratch)) return false;
    for (int i = 0; i < names->count; i++) {
        if (names->entries[i].line_index == line_index &&
            strcmp(names->entries[i].name, name) == 0) return true;
    }
    if (names->count == INT_MAX) return false;
    bool grown = scratch
        ? lam::scratch_grow_array(scratch, &names->entries, &names->capacity,
                                  names->count, names->count + 1, 8)
        : lam::pool_grow_array(pool, &names->entries, &names->capacity, names->count + 1, 8);
    if (!grown) return false;
    char* copy = scratch ? grid_scratch_strdup(scratch, name) : pool_strdup(pool, name);
    if (!copy) return false;
    names->entries[names->count++] = {lam::own(copy), line_index};
    return true;
}

bool grid_copy_track_line_names(Pool* pool, ScratchArena* scratch, GridLineNames* destination,
                                const GridLineNames* source, int offset) {
    if (!source || !destination) return false;
    for (int i = 0; i < source->count; i++) {
        const GridNamedLine& line = source->entries[i];
        if (!grid_add_track_line_name(pool, scratch, destination, line.name,
                                      line.line_index + offset)) return false;
    }
    return true;
}

void grid_destroy_track_line_names(Pool* pool, GridLineNames* names) {
    if (!pool || !names) return;
    for (int i = 0; i < names->count; i++) lam::free_owned(pool, names->entries[i].name);
    lam::free_owned(pool, names->entries);
    *names = {};
}

// Create a new grid track list
GridTrackList* create_grid_track_list(Pool* pool, int initial_capacity) {
    if (!pool) return nullptr;
    GridTrackList* track_list = (GridTrackList*)pool_calloc(pool, sizeof(GridTrackList));
    if (!track_list) return nullptr;

    track_list->allocated_tracks = initial_capacity;
    track_list->tracks = lam::own_arr((GridTrackSize**)pool_calloc(pool, initial_capacity * sizeof(GridTrackSize*)));
    if (!track_list->tracks) {
        pool_free(pool, track_list);
        return nullptr;
    }
    track_list->track_count = 0;
    track_list->is_repeat = false;
    track_list->repeat_count = 1;

    return track_list;
}

// clone retained computed definitions independently of their source pool.
GridTrackList* clone_grid_track_list(Pool* pool, const GridTrackList* source) {
    if (!pool || !source) return nullptr;
    GridTrackList* copy = create_grid_track_list(pool, source->allocated_tracks);
    if (!copy || !copy->tracks) {
        destroy_grid_track_list(pool, copy);
        return nullptr;
    }
    copy->is_repeat = source->is_repeat;
    copy->repeat_count = source->repeat_count;
    for (int index = 0; index < source->track_count; index++) {
        copy->tracks[index] = clone_grid_track_size(pool, source->tracks[index]);
        if (!copy->tracks[index]) {
            destroy_grid_track_list(pool, copy);
            return nullptr;
        }
        copy->track_count++;
    }
    if (!grid_copy_track_line_names(pool, nullptr, &copy->line_names, &source->line_names)) {
        destroy_grid_track_list(pool, copy);
        return nullptr;
    }
    return copy;
}

// Destroy a grid track list
void destroy_grid_track_list(Pool* pool, GridTrackList* track_list) {
    if (!track_list || !pool) return;

    for (int i = 0; i < track_list->track_count; i++) {
        destroy_grid_track_size(pool, track_list->tracks[i]);
    }
    lam::free_owned(pool, track_list->tracks);

    grid_destroy_track_line_names(pool, &track_list->line_names);
    pool_free(pool, track_list);
}

// Create a new grid track size
GridTrackSize* create_grid_track_size(Pool* pool, GridTrackSizeType type, float value) {
    if (!pool) return nullptr;
    GridTrackSize* track_size = (GridTrackSize*)pool_calloc(pool, sizeof(GridTrackSize));
    if (!track_size) return nullptr;

    track_size->type = type;
    track_size->value = value;
    track_size->is_percentage = false;
    track_size->min_size = nullptr;
    track_size->max_size = nullptr;
    track_size->fit_content_limit = 0;

    return track_size;
}

GridTrackSize* clone_grid_track_size(Pool* pool, const GridTrackSize* track_size) {
    if (!track_size || !pool) return nullptr;

    GridTrackSize* copy = (GridTrackSize*)pool_calloc(pool, sizeof(GridTrackSize));
    if (!copy) return nullptr;

    *copy = *track_size;
    copy->expression_owner = lam::own(css_value_clone_owned(track_size->expression, pool));
    copy->expression = lam::up((const CssValue*)copy->expression_owner.get());
    if (track_size->expression && !copy->expression_owner) {
        pool_free(pool, copy);
        return nullptr;
    }
    copy->min_size = lam::own(clone_grid_track_size(pool, track_size->min_size));
    copy->max_size = lam::own(clone_grid_track_size(pool, track_size->max_size));
    copy->repeat_tracks = nullptr;
    copy->repeat_track_count = 0;
    copy->repeat_line_names = {};

    if ((track_size->min_size && !copy->min_size) ||
        (track_size->max_size && !copy->max_size)) {
        destroy_grid_track_size(pool, copy);
        return nullptr;
    }

    if (track_size->repeat_tracks && track_size->repeat_track_count > 0) {
        // repeat children belong to the cloned graph; destroy_grid_track_size releases them
        copy->repeat_tracks = lam::own_arr((GridTrackSize**)pool_calloc(
            pool, track_size->repeat_track_count * sizeof(GridTrackSize*)));
        if (!copy->repeat_tracks) {
            destroy_grid_track_size(pool, copy);
            return nullptr;
        }
        copy->repeat_track_count = track_size->repeat_track_count;
        for (int i = 0; i < track_size->repeat_track_count; i++) {
            copy->repeat_tracks[i] = clone_grid_track_size(pool, track_size->repeat_tracks[i]);
            if (track_size->repeat_tracks[i] && !copy->repeat_tracks[i]) {
                destroy_grid_track_size(pool, copy);
                return nullptr;
            }
        }
    }

    if (!grid_copy_track_line_names(pool, nullptr, &copy->repeat_line_names,
                                    &track_size->repeat_line_names)) {
        destroy_grid_track_size(pool, copy);
        return nullptr;
    }
    return copy;
}

// Destroy a grid track size
void destroy_grid_track_size(Pool* pool, GridTrackSize* track_size) {
    if (!track_size || !pool) return;

    css_value_destroy_owned(track_size->expression_owner, pool);
    if (track_size->min_size) destroy_grid_track_size(pool, track_size->min_size);
    if (track_size->max_size) destroy_grid_track_size(pool, track_size->max_size);
    if (track_size->repeat_tracks) {
        for (int i = 0; i < track_size->repeat_track_count; i++) {
            destroy_grid_track_size(pool, track_size->repeat_tracks[i]);
        }
        lam::free_owned(pool, track_size->repeat_tracks);
    }
    grid_destroy_track_line_names(pool, &track_size->repeat_line_names);
    pool_free(pool, track_size);
}

// Destroy a grid area
void destroy_grid_area(Pool* pool, GridArea* area) {
    if (!area) return;

    if (pool) lam::free_owned(pool, area->name);
    // Don't free the area itself if it's part of an array
}

// Add a grid line name
void add_grid_line_name(GridContainerLayout* grid, const char* name, int line_number, bool is_row) {
    if (!grid || !name) return;

    for (int i = 0; i < grid->line_name_count; i++) {
        const GridLineName& line = grid->line_names[i];
        if (line.is_row == is_row && line.line_number == line_number &&
            strcmp(line.name, name) == 0) return;
    }
    // Line-name tables are pass-local; grow by allocating a larger scratch
    // table so the grid container mark can release every generation at once.
    if (grid->line_name_count >= grid->allocated_line_names) {
        if (!grid->lycon) { grid->storage_failed = true; return; }
        if (!lam::scratch_grow_array(&grid->lycon->scratch, &grid->line_names,
                                     &grid->allocated_line_names, grid->line_name_count,
                                     grid->line_name_count + 1, 8)) {
            grid->storage_failed = true;
            log_error("grid_utils: unable to grow grid line-name scratch table to %d",
                      grid->line_name_count + 1);
            return;
        }
    }

    GridLineName* line_name = &grid->line_names[grid->line_name_count];
    // Layout line-name projections share the grid pass mark; heap strings here
    // previously required a separate cleanup loop on every early-exit path.
    line_name->name = lam::up(grid_scratch_strdup(&grid->lycon->scratch, name));
    if (!line_name->name) { grid->storage_failed = true; return; }
    line_name->line_number = line_number;
    line_name->is_row = is_row;

    grid->line_name_count++;

}

static bool grid_named_line_matches(const char* actual, const char* name, const char* suffix) {
    size_t length = strlen(name);
    return suffix ? strncmp(actual, name, length) == 0 && strcmp(actual + length, suffix) == 0
                  : strcmp(actual, name) == 0;
}

// return explicit-grid coordinates, including zero/negative lines before its start.
int find_grid_line_by_name(GridContainerLayout* grid, const char* name, bool is_row,
                           int occurrence, bool is_end, int anchor, int direction) {
    if (!grid || !name) return 0;
    int last = (is_row ? grid->explicit_row_count : grid->explicit_column_count) + 1;
    if (occurrence == 0 && direction == 0) {
        int first = 0;
        const char* suffix = is_end ? "-end" : "-start";
        for (int i = 0; i < grid->line_name_count; i++) {
            const GridLineName& line = grid->line_names[i];
            if (line.is_row == is_row && grid_named_line_matches(line.name, name, suffix) &&
                (!first || line.line_number < first)) first = line.line_number;
        }
        if (first) return first;
    }
    if (direction == 0) {
        direction = occurrence < 0 ? -1 : 1;
        anchor = direction < 0 ? last + 1 : 0;
    }
    int64_t remaining = occurrence < 0 ? -(int64_t)occurrence : occurrence;
    if (remaining == 0) remaining = 1;
    int64_t current = direction > 0 ? (anchor < 1 ? 1 : (int64_t)anchor + 1)
                                : (anchor > last ? last : (int64_t)anchor - 1);
    for (; current >= 1 && current <= last; current += direction) {
        bool matches = false;
        for (int i = 0; i < grid->line_name_count && !matches; i++) {
            const GridLineName& line = grid->line_names[i];
            matches = line.is_row == is_row && line.line_number == current &&
                grid_named_line_matches(line.name, name, nullptr);
        }
        if (matches && --remaining == 0) return static_cast<int>(current); // INT_CAST_OK: explicit line is bounded by track policy.
    }
    // only implicit lines beyond the searched edge acquire a missing name.
    int64_t result = direction > 0 ? (anchor > last ? anchor : last) + remaining
                                  : (anchor < 1 ? anchor : 1) - remaining;
    // retain an outside-edge sentinel until the complete item area is clipped.
    if (result > radiant::grid::GRID_MAX_TRACK_LINE + 2) result = radiant::grid::GRID_MAX_TRACK_LINE + 2;
    if (result < radiant::grid::GRID_MIN_TRACK_LINE) result = radiant::grid::GRID_MIN_TRACK_LINE;
    return static_cast<int>(result); // INT_CAST_OK: bounded grid line sentinel.
}

// Enhanced grid template areas parser
void clear_grid_template_areas(Pool* pool, GridProp* grid) {
    if (!grid) return;

    for (int i = 0; i < grid->area_count; i++) {
        if (grid->grid_areas) lam::free_owned(pool, grid->grid_areas[i].name);
    }
    grid->area_count = 0;
    grid->template_area_rows = grid->template_area_columns = 0;
}

struct GridAreaProjection { Pool* pool; GridProp* grid; };

static bool grid_project_template_area(const CssGridAreaBounds& source, void* context) {
    GridAreaProjection* projection = (GridAreaProjection*)context;
    Pool* pool = projection->pool;
    GridProp* grid = projection->grid;
    // names outside the explicit-grid policy disappear with their clipped tracks.
    if (source.first_row >= radiant::grid::GRID_MAX_EXPLICIT_TRACKS ||
        source.first_column >= radiant::grid::GRID_MAX_EXPLICIT_TRACKS) return true;
    if (grid->area_count == INT_MAX || !lam::pool_grow_array(pool, &grid->grid_areas,
            &grid->allocated_areas, grid->area_count + 1, 4)) return false;
    GridArea& area = grid->grid_areas[grid->area_count];
    area.name = lam::own(pool_dup_n(pool, source.name, source.name_length));
    if (!area.name) return false;
    area.row_start = static_cast<int>(source.first_row) + 1; // INT_CAST_OK: explicit row bounded above.
    area.column_start = static_cast<int>(source.first_column) + 1; // INT_CAST_OK: explicit column bounded above.
    area.row_end = static_cast<int>(radiant::grid::grid_min_value(source.last_row + 1, (size_t)radiant::grid::GRID_MAX_EXPLICIT_TRACKS)) + 1; // INT_CAST_OK: bounded explicit row.
    area.column_end = static_cast<int>(radiant::grid::grid_min_value(source.last_column + 1, (size_t)radiant::grid::GRID_MAX_EXPLICIT_TRACKS)) + 1; // INT_CAST_OK: bounded explicit column.
    grid->area_count++;
    return true;
}

void parse_grid_template_areas(Pool* pool, GridProp* grid, const CssValue* value) {
    if (!pool || !grid) return;
    clear_grid_template_areas(pool, grid);
    GridAreaProjection projection{pool, grid};
    CssGridAreaDimensions dimensions = {};
    if (!css_grid_visit_template_areas(value, &dimensions, grid_project_template_area, &projection)) {
        clear_grid_template_areas(pool, grid);
        return;
    }
    grid->template_area_rows = static_cast<int>(radiant::grid::grid_min_value(dimensions.rows, (size_t)radiant::grid::GRID_MAX_EXPLICIT_TRACKS)); // INT_CAST_OK: bounded explicit row count.
    grid->template_area_columns = static_cast<int>(radiant::grid::grid_min_value(dimensions.columns, (size_t)radiant::grid::GRID_MAX_EXPLICIT_TRACKS)); // INT_CAST_OK: bounded explicit column count.
}

bool copy_grid_template_areas(Pool* pool, GridProp* target, const GridProp* source) {
    if (!pool || !target) return false;
    if (target == source) return true;
    clear_grid_template_areas(pool, target);
    if (!source) return true;
    GridAreaProjection projection{pool, target};
    for (int i = 0; i < source->area_count; i++) {
        const GridArea& area = source->grid_areas[i];
        CssGridAreaBounds bounds{area.name, strlen(area.name), (size_t)area.row_start - 1,
            (size_t)area.row_end - 2, (size_t)area.column_start - 1, (size_t)area.column_end - 2, 0};
        if (!grid_project_template_area(bounds, &projection)) {
            clear_grid_template_areas(pool, target);
            return false;
        }
    }
    target->template_area_rows = source->template_area_rows;
    target->template_area_columns = source->template_area_columns;
    return true;
}

LayoutAxis layout_grid_track_axis(const GridContainerLayout* grid, bool columns) {
    bool vertical = grid && grid->container &&
        layout_block_inline_axis_is_vertical(grid->container);
    return columns != vertical ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
}

Rect layout_grid_area_rect(const GridContainerLayout* grid, float column_start,
        float row_start, float column_size, float row_size) {
    Rect area = {};
    if (!grid || !grid->container) return area;
    LayoutAxis column_axis = layout_grid_track_axis(grid, true);
    LayoutAxisPair<float> starts = {}, sizes = {}, content = {};
    starts[column_axis] = column_start;
    sizes[column_axis] = column_size;
    content[column_axis] = grid->content_width;
    LayoutAxis row_axis = layout_grid_track_axis(grid, false);
    starts[row_axis] = row_start;
    sizes[row_axis] = row_size;
    content[row_axis] = grid->content_height;
    for (LayoutAxis axis : layout_axes()) {
        if (!layout_logical_start_is_physical_start(grid->container, axis))
            starts[axis] = content[axis] - starts[axis] - sizes[axis];
        starts[axis] += layout_axis_decoration_start(grid->container->bound, axis);
    }
    return {starts.x, starts.y, sizes.x, sizes.y};
}

static float grid_item_column_span_size(GridContainerLayout* grid, GridItemProp* item) {
    if (!grid || !item || !grid->computed_columns) return -1.0f;
    int start = item->computed_grid_column_start - 1;
    int end = item->computed_grid_column_end - 1;
    if (start < 0 || end <= start || end > grid->computed_column_count) return -1.0f;
    return radiant::grid::spanned_tracks_size(
        *grid->computed_columns, start, end - start, grid->column_gap);
}

// grid tracks select the physical query axis; cached width/height slots are columns/rows.
IntrinsicSizes calculate_grid_item_intrinsic_sizes(LayoutContext* lycon, ViewBlock* item, bool is_row_axis) {
    IntrinsicSizes sizes = {};
    if (!item) return sizes;
    GridContainerLayout* grid = lycon ? lycon->grid_container.get() : nullptr;
    LayoutAxis axis = layout_grid_track_axis(grid, !is_row_axis);
    bool horizontal = axis == LAYOUT_AXIS_X;
    GridItemProp* gi = grid_item_prop(item);
    if (gi && gi->has_measured_size && (!is_row_axis ||
        gi->measured_min_height > 0.0f || gi->measured_max_height > 0.0f)) {
        sizes = is_row_axis
            ? IntrinsicSizes{gi->measured_min_height, gi->measured_max_height}
            : IntrinsicSizes{gi->measured_min_width, gi->measured_max_width};
        layout_apply_positive_min_max_contribution(item, horizontal, &sizes.min_content, &sizes.max_content);
        return sizes;
    }
    float column_span = grid_item_column_span_size(grid, gi);
    if (gi && gi->anonymous_text_runs && lycon) {
        radiant::SizeF measured = layout_measure_anonymous_text(
            lycon, item, gi->anonymous_text_runs, max(column_span, 0.0f));
        sizes.min_content = sizes.max_content = measured.height;
        return sizes;
    }
    if (!lycon) return {0.0f, layout_axis_size(item, axis)};

    bool explicit_size = layout_axis_has_given_size(item, horizontal) &&
        !layout_axis_size_is_percentage(item, horizontal) && !layout_css_size_is_automatic(item, horizontal);
    bool intrinsic_constraint = layout_intrinsic_min_size_keyword(item, horizontal) != CSS_VALUE__UNDEF ||
        layout_intrinsic_max_size_keyword(item, horizontal) != CSS_VALUE__UNDEF;
    if (explicit_size && !intrinsic_constraint) {
        float preferred = layout_axis_given_size(item->blk, axis);
        float extent = layout_css_size_to_border_box(item->bound, layout_box_sizing(item), preferred, horizontal);
        extent = layout_floor_border_box_axis(item, extent, horizontal);
        sizes = {extent, extent};
    } else if (horizontal) {
        // vertical rows are queried after columns supply the child's physical inline height.
        LayoutContainingBlockScope column_scope(lycon, LAYOUT_AXIS_Y,
            max(column_span, 0.0f), is_row_axis && column_span >= 0.0f);
        sizes = layout_measure_intrinsic_widths(lycon, item);
    } else if (!explicit_size && !is_row_axis && layout_block_inline_axis_is_vertical(item) &&
               layout_measure_direct_inline_intrinsics(lycon, item, &sizes)) {
        float decoration = layout_padding_border_axis(item, false);
        sizes.min_content += decoration;
        sizes.max_content += decoration;
    } else {
        // a block-axis query wraps at the solved column area; an unsolved or
        // orthogonal area uses the item's intrinsic width, never a guessed width.
        float width = -1.0f;
        if (layout_axis_has_given_size(item, true) && !layout_css_size_is_automatic(item, true)) {
            width = layout_content_size_if_border_box(item, item->block()->given_width, true);
        } else if (is_row_axis && column_span >= 0.0f) {
            width = layout_content_size_from_border_box(item, column_span, true);
        } else {
            IntrinsicSizes widths = layout_measure_intrinsic_widths(lycon, item);
            width = layout_content_size_from_border_box(item, widths.max_content, true);
        }
        bool has_parent_basis = grid && layout_grid_track_axis(grid, true) == LAYOUT_AXIS_X;
        LayoutContainingBlockScope parent_scope(lycon, LAYOUT_AXIS_X,
            has_parent_basis ? grid->content_width : 0.0f, has_parent_basis);
        float height = calculate_max_content_height(lycon, item, max(width, 0.0f));
        sizes = {height, height};
    }
    // non-replaced auto items can transfer the solved inline area through an authored ratio.
    if (is_row_axis && column_span >= 0.0f && !layout_element_is_css_replaced(item->as_element()) &&
        layout_css_size_is_automatic(item, true) && layout_css_size_is_automatic(item, false)) {
        float ratio = layout_used_preferred_aspect_ratio(item);
        if (ratio > 0.0f) {
            float transferred = layout_ratio_transfer_axis(item, column_span, !horizontal, ratio,
                true, true, layout_uses_border_box(item) && !layout_aspect_ratio_uses_content_box(item));
            if (transferred >= 0.0f) sizes = {transferred, transferred};
        }
    }
    sizes.max_content = max(sizes.max_content, sizes.min_content);
    layout_apply_positive_min_max_contribution(item, horizontal, &sizes.min_content, &sizes.max_content);
    return sizes;
}
