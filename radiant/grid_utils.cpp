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

static bool grid_text_has_in_flow_content(DomNode* node) {
    const char* text = node ? (const char*)node->text_data() : nullptr;
    if (!text) return false;
    for (const char* p = text; *p; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '\f') return true;
    }
    return false;
}

static bool grid_item_has_in_flow_content(ViewBlock* item) {
    return layout_element_has_in_flow_content(item, grid_text_has_in_flow_content);
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

// Calculate intrinsic sizes for grid items using unified intrinsic sizing API
IntrinsicSizes calculate_grid_item_intrinsic_sizes(LayoutContext* lycon, ViewBlock* item, bool is_row_axis) {
    IntrinsicSizes sizes = {0};

    if (!item) return sizes;

    // First, check if we have pre-computed measurements from Pass 1 (layout_grid_multipass.cpp)
    // These measurements were calculated with proper width constraints for height calculation
    GridItemProp* gi = grid_item_prop(item);
    if (gi) {
        if (is_row_axis) {
            // Use pre-computed height measurements if available
            if (gi->has_measured_size && (gi->measured_min_height > 0 || gi->measured_max_height > 0)) {
                sizes.min_content = gi->measured_min_height;
                sizes.max_content = gi->measured_max_height;

                // Don't force minimum height - empty items should have 0 height
                if (sizes.max_content < sizes.min_content) {
                    sizes.max_content = sizes.min_content;
                }


                layout_apply_positive_min_max_contribution(item, false,
                    &sizes.min_content, &sizes.max_content);
                return sizes;
            }
        } else {
            // Use pre-computed width measurements if available
            // has_measured_size=true means measurement ran; 0 is valid for empty items.
            if (gi->has_measured_size) {
                sizes.min_content = gi->measured_min_width;
                sizes.max_content = gi->measured_max_width;

                // Don't force minimum width - empty items should have 0 width
                if (sizes.max_content < sizes.min_content) {
                    sizes.max_content = sizes.min_content;
                }


                layout_apply_positive_min_max_contribution(item, true,
                    &sizes.min_content, &sizes.max_content);
                return sizes;
            }
        }
    }

    if (is_row_axis && gi && gi->anonymous_text_runs && lycon) {
        GridContainerLayout* grid = lycon->grid_container;
        float width = item->width;
        if (grid && grid->computed_columns) {
            // Grid §11.5: wrapping contributions use the resolved column area.
            int start = gi->computed_grid_column_start - 1;
            int end = gi->computed_grid_column_end - 1;
            if (start >= 0 && end > start && end <= grid->computed_column_count) {
                width = radiant::grid::spanned_tracks_size(
                    *grid->computed_columns, start, end - start, grid->column_gap);
            }
        }
        radiant::SizeF measured = layout_measure_anonymous_text(
            lycon, item, gi->anonymous_text_runs, width);
        sizes.min_content = sizes.max_content = measured.height;
        return sizes;
    }

    if (is_row_axis &&
        (!item->blk || item->block()->given_height < 0.0f) &&
        item->display.inner != RDT_DISPLAY_REPLACED &&
        !grid_item_has_in_flow_content(item) &&
        layout_used_preferred_aspect_ratio(item) <= 0.0f) {
        BoxMetrics box = layout_box_metrics(item);
        sizes.min_content = box.pad_border_v;
        sizes.max_content = box.pad_border_v;
        return sizes;
    }

    // Fallback: Use unified intrinsic sizing API if layout context is available
    if (lycon) {
        // Determine available space for measurement
        AvailableSpace available;

        if (is_row_axis) {
            // For row axis (height measurement), we need to determine the width constraint
            // Height depends on width due to text wrapping
            float width = 200; // Default fallback

            bool has_definite_inline_size = item->blk &&
                item->block()->given_width >= 0.0f &&
                isnan(item->block()->given_width_percent) &&
                item->block()->given_width_type != CSS_VALUE_AUTO &&
                item->block()->given_width_type != CSS_VALUE_MIN_CONTENT &&
                item->block()->given_width_type != CSS_VALUE_MAX_CONTENT &&
                item->block()->given_width_type != CSS_VALUE_FIT_CONTENT;
            if (has_definite_inline_size) {
                // A definite item width wins over the stretched grid area; otherwise
                // a ratio-dependent height would incorrectly inherit the track width.
                width = layout_css_size_to_content_box(item->bound, layout_box_sizing(item),
                                                       item->block()->given_width, true);
            } else if (item->width > 0) {
                // Item already has a width (e.g., from previous layout pass)
                width = item->width;
            } else if (gi && lycon->grid_container) {
                // Calculate width from computed column tracks
                GridContainerLayout* grid = lycon->grid_container;
                int col_start = gi->computed_grid_column_start - 1;
                int col_end = gi->computed_grid_column_end - 1;

                // Clamp to valid ranges
                if (col_start >= 0 && col_end > col_start && col_end <= grid->computed_column_count) {
                    int span_width = 0;
                    bool has_unsized_fr_track = false;

                    for (int c = col_start; c < col_end; c++) {
                        int track_size = (int)(*grid->computed_columns)[c].base_size; // INT_CAST_OK: legacy intrinsic width accumulator
                        if (track_size > 0) {
                            span_width += track_size;
                            if (c < col_end - 1) {
                                span_width += (int)grid->column_gap; // INT_CAST_OK: grid gap accumulation
                            }
                        } else {
                            // Track has size 0 - might be FR track not yet sized
                            has_unsized_fr_track = true;
                        }
                    }

                    if (span_width > 0 && !has_unsized_fr_track) {
                        // All tracks are sized, use actual span width
                        int box_adjustment = 0;
                        if (item->bound) {
                            box_adjustment = (int)layout_boundary_metrics(item->bound).pad_border_h; // INT_CAST_OK: integer track sizing adjustment
                        }
                        width = (float)(span_width - box_adjustment);
                        // Add a small subpixel margin to compensate for integer truncation
                        // of track sizes: if a track was 173.28px→173, content becomes 153px,
                        // but text measured at 153.28px would spuriously wrap. The +0.5px
                        // tolerance avoids false wrapping due to float→int rounding loss.
                        if (width > 10) width += 0.5f;
                        if (width < 10) width = 10;
                    } else if (grid->content_width > 0) {
                        // FR tracks not sized yet - estimate from container width
                        int col_count = grid->computed_column_count > 0 ? grid->computed_column_count : 1;
                        int total_gaps = (col_count - 1) * (int)grid->column_gap; // INT_CAST_OK: grid gap accumulation
                        int span_cols = col_end - col_start;
                        width = (float)((grid->content_width - total_gaps) * span_cols) / col_count;

                        // Subtract item's own padding/border
                        if (item->bound) {
                            width -= layout_boundary_metrics(item->bound).pad_border_h;
                        }
                        if (width < 10) width = 10;
                    }
                }
            }

            // Create available space with definite width for height calculation
            available = AvailableSpace::make_width_definite(width);
        } else {
            // For column axis (width measurement), use max-content
            available = AvailableSpace::make_max_content();
        }

        // grid track queries consume one axis; measuring the other can recursively
        // enter nested grids and repeat the entire subtree for an unused result.
        bool override_parent = is_row_axis && lycon->grid_container &&
                               lycon->grid_container->content_width > 0;
        {
            LayoutContainingBlockScope grid_parent_scope(lycon, LAYOUT_AXIS_X,
                override_parent ? lycon->grid_container->content_width : 0.0f, override_parent);
            if (is_row_axis) {
                float height = calculate_max_content_height(lycon, item, available.width.value);
                sizes = {height, height};
            } else {
                sizes = calculate_node_intrinsic_widths(lycon, item);
            }
        }

        // Ensure max >= min
        if (sizes.max_content < sizes.min_content) {
            sizes.max_content = sizes.min_content;
        }
    } else {
        // Fallback: use item dimensions if no layout context
        // Empty items should have 0 height/width - the grid algorithm handles min track sizes
        if (is_row_axis) {
            sizes.min_content = 0;
            sizes.max_content = item->height > 0 ? item->height : 0;
        } else {
            sizes.min_content = 0;
            sizes.max_content = item->width > 0 ? item->width : 0;
        }
    }

    layout_apply_positive_min_max_contribution(item, !is_row_axis,
        &sizes.min_content, &sizes.max_content);

    // CSS Grid §6.4: transfer the final column size only when both axes are
    // auto; a definite inline size already determines the block contribution.
    if (is_row_axis && item->specified_style) {
        bool has_explicit_height = (item->blk && item->block_mut()->given_height > 0);
        bool has_explicit_width = (item->blk && item->block_mut()->given_width > 0);
        if (!has_explicit_width && !has_explicit_height) {
            float aspect_ratio = layout_used_preferred_aspect_ratio(item);
            if (aspect_ratio > 0.0f) {
                // Compute the effective track width (after column sizing)
                float track_width = 0.0f;
                GridItemProp* gi = grid_item_prop(item);
                if (gi && lycon && lycon->grid_container) {
                    GridContainerLayout* grid = lycon->grid_container;
                    int col_start = gi->computed_grid_column_start - 1;
                    int col_end   = gi->computed_grid_column_end   - 1;
                    if (col_start >= 0 && col_end > col_start && col_end <= grid->computed_column_count) {
                        for (int c = col_start; c < col_end; c++) {
                            track_width += (*grid->computed_columns)[c].base_size;
                            if (c < col_end - 1) track_width += grid->column_gap;
                        }
                    }
                }
                if (track_width <= 0.0f) track_width = (float)(item->width > 0 ? item->width : 0);
                if (track_width > 0.0f) {
                    float h_from_ratio = track_width / aspect_ratio;
                    sizes.min_content = h_from_ratio;
                    sizes.max_content = h_from_ratio;
                }
            }
        }
    }

    return sizes;
}
