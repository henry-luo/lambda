#include "layout.hpp"
#include "view.hpp"
#include "css_resolved_style.hpp"
#include "event.hpp"

#include "../lib/log.h"
#include "../lib/scratch_arena.h"
#include "../lib/tagged.hpp"

namespace radiant {

struct LayoutViewSnapshot {
    ::DomNode* node;
    ViewType view_type;
    float x;
    float y;
    float width;
    float height;
    bool layout_dirty;
    float layout_height_contribution;
    bool is_element;
    float content_width;
    float content_height;
    bool measuring_intrinsic_width;
    bool main_size_from_flex;
    bool flex_has_intrinsic_width, flex_has_intrinsic_height;
    IntrinsicSizes flex_intrinsic_width, flex_intrinsic_height;
    bool has_form;
    float form_intrinsic_width;
    float form_intrinsic_height;
    bool has_block_prop;
    float block_given_width;
    float block_given_height;
    CssEnum block_given_width_type;
    CssEnum block_given_height_type;
    bool block_aspect_ratio_auto_height;
    bool block_percentage_height_resolved_late;
};

static void layout_view_snapshot_capture(DomNode* node, LayoutViewSnapshot* snapshot) {
    snapshot->node = node;
    snapshot->view_type = node->view_type;
    snapshot->x = node->x;
    snapshot->y = node->y;
    snapshot->width = node->width;
    snapshot->height = node->height;
    snapshot->layout_dirty = node->layout_dirty;
    snapshot->layout_height_contribution = node->layout_height_contribution;
    snapshot->is_element = node->is_element();

    if (snapshot->is_element) {
        ::DomElement* element = node->as_element();
        snapshot->content_width = element->content_width;
        snapshot->content_height = element->content_height;
        snapshot->measuring_intrinsic_width = element->measuring_intrinsic_width();
        snapshot->main_size_from_flex = element->flex_item() && element->flex_item()->main_size_from_flex;
        if (FlexItemProp* item = element->flex_item()) {
            snapshot->flex_has_intrinsic_width = item->has_intrinsic_width;
            snapshot->flex_has_intrinsic_height = item->has_intrinsic_height;
            snapshot->flex_intrinsic_width = item->intrinsic_width;
            snapshot->flex_intrinsic_height = item->intrinsic_height;
        }
        snapshot->has_form = element->form_control();
        if (snapshot->has_form) {
            snapshot->form_intrinsic_width = element->form->intrinsic_width;
            snapshot->form_intrinsic_height = element->form->intrinsic_height;
        }
        snapshot->has_block_prop = element->blk != nullptr;
        if (snapshot->has_block_prop) {
            snapshot->block_given_width = element->block()->given_width;
            snapshot->block_given_height = element->block()->given_height;
            snapshot->block_given_width_type = element->block()->given_width_type;
            snapshot->block_given_height_type = element->block()->given_height_type;
            snapshot->block_aspect_ratio_auto_height =
                element->block()->aspect_ratio_auto_height;
            snapshot->block_percentage_height_resolved_late =
                element->block()->percentage_height_resolved_late;
        }
    }

}

static void layout_view_snapshot_restore(const LayoutViewSnapshot* snapshot) {
    ::DomNode* node = snapshot->node;
    node->view_type = snapshot->view_type;
    node->x = snapshot->x;
    node->y = snapshot->y;
    node->width = snapshot->width;
    node->height = snapshot->height;
    node->layout_dirty = snapshot->layout_dirty;
    node->layout_height_contribution = snapshot->layout_height_contribution;

    if (snapshot->is_element && node->is_element()) {
        ::DomElement* element = node->as_element();
        element->content_width = snapshot->content_width;
        element->content_height = snapshot->content_height;
        // Intrinsic results are pure for one layout generation. Retain them
        // across a temporary measurement so nested flex/grid queries share it.
        element->set_measuring_intrinsic_width(snapshot->measuring_intrinsic_width);
        if (FlexItemProp* item = element->flex_item()) {
            item->main_size_from_flex = snapshot->main_size_from_flex;
            item->has_intrinsic_width = snapshot->flex_has_intrinsic_width;
            item->has_intrinsic_height = snapshot->flex_has_intrinsic_height;
            item->intrinsic_width = snapshot->flex_intrinsic_width;
            item->intrinsic_height = snapshot->flex_intrinsic_height;
        }
        if (snapshot->has_form &&
            element->form_control()) {
            element->form->intrinsic_width = snapshot->form_intrinsic_width;
            element->form->intrinsic_height = snapshot->form_intrinsic_height;
        }
        if (snapshot->has_block_prop && element->blk) {
            element->blk->given_width = snapshot->block_given_width;
            element->blk->given_height = snapshot->block_given_height;
            element->blk->given_width_type = snapshot->block_given_width_type;
            element->blk->given_height_type = snapshot->block_given_height_type;
            element->blk->aspect_ratio_auto_height =
                snapshot->block_aspect_ratio_auto_height;
            element->blk->percentage_height_resolved_late =
                snapshot->block_percentage_height_resolved_late;
        }
    }
}

// Snapshots live in the measurement's scope, ended after they are restored.
static void layout_measure_snapshot_append(::LayoutContext* lycon, ScratchMark* scope,
                                            ArrayList* snapshots, ::DomNode* node) {
    if (!lycon || !snapshots || !node) return;
    // trial snapshots restore scalar boxes, not the live fragments of contained flex items.
    if (node->is_element()) flex_retained_forget_content(node->as_element());

    LayoutViewSnapshot* snapshot = (LayoutViewSnapshot*)scratch_scope_calloc(
        &lycon->scratch, scope, sizeof(LayoutViewSnapshot));
    if (!snapshot) return;

    layout_view_snapshot_capture(node, snapshot);

    if (!arraylist_append(snapshots, snapshot)) {
        return;
    }

    if (node->is_element()) {
        for (::DomNode* child = node->as_element()->first_child; child; child = child->next_sibling) {
            layout_measure_snapshot_append(lycon, scope, snapshots, child);
        }
    }
}

static void layout_measure_snapshot_restore(::LayoutContext* lycon, ArrayList* snapshots) {
    if (!lycon || !snapshots) return;

    for (int i = snapshots->length - 1; i >= 0; i--) {
        LayoutViewSnapshot* snapshot = (LayoutViewSnapshot*)snapshots->data[i];
        if (!snapshot || !snapshot->node) {
            continue;
        }

        layout_view_snapshot_restore(snapshot);
    }
    arraylist_free(snapshots);
}

template<typename T, size_t offset>
struct LayoutOutputTail {
    uint8_t bytes[sizeof(T) - offset];
    void capture(const T* value) { if (value) memcpy(bytes, (const uint8_t*)value + offset, sizeof(bytes)); }
    void restore(T* value) const { if (value) memcpy((uint8_t*)value + offset, bytes, sizeof(bytes)); }
};

struct LayoutFlowNode {
    LayoutViewSnapshot box;
    CssUsedGeometry geometry;
    LayoutOutputTail<BlockProp, offsetof(BlockProp, first_line_max_ascender)> block_output;
    float collapsed_through_mb, margin_chain_positive, margin_chain_negative;
    bool has_clearance, clearance_in_margin_chain;
    Margin flow_margin;
    bool has_flow_margin;
    FragmentUnion fragments[FRAGMENT_UNION_COUNT];
    uint8_t fragment_presence_mask;
    int inline_line_number;
    FontProp* text_font;
    lam::Own<TextRect> text_rects;
    lam::Own<PositionProp> position;
    lam::Own<FlexProp> flex;
    lam::Own<FlexItemProp> flex_item;
    lam::Own<TableProp> table;
    lam::Own<TableCellProp> cell;
    CollapsedBorder cell_borders[4];
    bool cell_border_present[4];
    int grid_placement[4];
    Rect grid_area;
    lam::Own<char> grid_tracks;
    size_t grid_columns_offset;
    bool has_grid, has_grid_item;
    float native_image_width, native_image_height;
    bool native_image_sized, broken_alt_fallback;
    bool has_embed;
    uint8_t text_trim_applied;
    float text_trim_start, text_trim_end, initial_letter_trimmed_start;
    float ruby_anchor, ruby_width, ruby_advance, ruby_start_overhang, ruby_empty_advance;
    bool ruby_simple, ruby_empty;
    bool inline_strut;
    lam::Own<LayoutFlowNode> next;
};

struct LayoutFlowResult {
    lam::Own<LayoutFlowNode> first;
    DomNode* root;
    uint32_t generation;
    uint32_t input_revision;
    uint64_t style_epoch, mutation_epoch;
};

static TextRect* layout_flow_clone_rects(Pool* pool, const TextRect* source) {
    lam::Own<TextRect> first = nullptr;
    TextRect* previous = nullptr;
    for (; source; source = source->next) {
        TextRect* copy = (TextRect*)pool_alloc(pool, sizeof(TextRect));
        if (!copy) { lam::free_owned_list(pool, first); return nullptr; }
        *copy = *source;
        copy->next = nullptr;
        if (previous) previous->next = lam::own(copy);
        else first = lam::own(copy);
        previous = copy;
    }
    return first.get();
}

void layout_flow_result_release(Pool* pool, LayoutFlowResult* result) {
    if (!result) return;
    lam::Own<LayoutFlowNode> current = result->first;
    while (current) {
        lam::Own<LayoutFlowNode> next = current->next;
        lam::free_owned_list(pool, current->text_rects);
        lam::free_owned(pool, current->position);
        lam::free_owned(pool, current->flex);
        lam::free_owned(pool, current->flex_item);
        lam::free_owned(pool, current->table);
        lam::free_owned(pool, current->cell);
        lam::free_owned(pool, current->grid_tracks);
        lam::free_owned(pool, current);
        current = next;
    }
    pool_free(pool, result);
}

static bool layout_flow_capture_nodes(LayoutContext* context, DomNode* node,
                                      LayoutFlowNode** tail, unsigned depth, int* remaining) {
    if (depth >= MAX_LAYOUT_DEPTH || --*remaining < 0) return false;
    LayoutFlowNode* output = (LayoutFlowNode*)pool_calloc(context->pool, sizeof(LayoutFlowNode));
    if (!output) return false;
    (*tail)->next = lam::own(output);
    *tail = output;
    layout_view_snapshot_capture(node, &output->box);
    if (node->is_text()) {
        DomText* text = node->as_text();
        output->text_font = text->font;
        output->text_rects = lam::own(layout_flow_clone_rects(context->pool, text->rect));
        if (text->rect && !output->text_rects) return false;
    } else if (node->is_element()) {
        DomElement* element = node->as_element();
        css_used_geometry_capture(context, element, &output->geometry);
        output->block_output.capture(element->block());
        if (element->blk) {
            const BlockProp* block = element->block();
            output->text_trim_applied = block->text_box_trim_applied;
            output->text_trim_start = block->text_box_trim_start_amount;
            output->text_trim_end = block->text_box_trim_end_amount;
            output->initial_letter_trimmed_start = block->initial_letter_trimmed_start_contribution;
        }
        output->inline_strut = element->inl()->content_strut_establishes_line;
        if (element->bound) {
            output->flow_margin = element->boundary()->flow_margin;
            output->has_flow_margin = element->boundary()->has_flow_margin;
            output->collapsed_through_mb = element->boundary()->collapsed_through_mb;
            output->margin_chain_positive = element->boundary()->margin_chain_positive;
            output->margin_chain_negative = element->boundary()->margin_chain_negative;
            output->has_clearance = element->boundary()->has_clearance;
            output->clearance_in_margin_chain = element->boundary()->clearance_in_margin_chain;
        }
        output->inline_line_number = element->inline_line_number;
        if (FlexItemProp* item = element->flex_item()) {
            // anonymous items are scratch-lived and cannot be nodes of this retained DOM subtree.
            if (item->anonymous_text_runs) return false;
            output->flex_item = lam::own((FlexItemProp*)pool_alloc(context->pool, sizeof(FlexItemProp)));
            if (!output->flex_item) return false;
            *output->flex_item = *item;
            // only a memo describing these captured boxes can accompany the owned fragments.
            FlexRetainedPass* retained = &output->flex_item->retained;
            retained->has_content = retained->has_content &&
                retained->laid_width == node->width && retained->laid_height == node->height &&
                retained->laid_content_width == element->content_width &&
                retained->laid_content_height == element->content_height;
        }
        if (element->position) {
            output->position = lam::own((PositionProp*)pool_alloc(context->pool, sizeof(PositionProp)));
            if (!output->position) return false;
            *output->position = *element->position;
        }
        if (TableProp* table = element->table_prop()) {
            output->table = lam::own((TableProp*)pool_alloc(context->pool, sizeof(TableProp)));
            if (!output->table) return false;
            *output->table = *table;
        }
        if (TableCellProp* cell = element->cell_prop()) {
            output->cell = lam::own((TableCellProp*)pool_alloc(context->pool, sizeof(TableCellProp)));
            if (!output->cell) return false;
            *output->cell = *cell;
            CollapsedBorder* borders[] = {cell->top_resolved, cell->right_resolved,
                cell->bottom_resolved, cell->left_resolved};
            for (unsigned side = 0; side < 4; side++) {
                output->cell_border_present[side] = borders[side] != nullptr;
                if (borders[side]) output->cell_borders[side] = *borders[side];
            }
        }
        if (GridItemProp* grid = element->grid_item()) {
            output->has_grid_item = true;
            output->grid_placement[0] = grid->computed_grid_row_start;
            output->grid_placement[1] = grid->computed_grid_row_end;
            output->grid_placement[2] = grid->computed_grid_column_start;
            output->grid_placement[3] = grid->computed_grid_column_end;
            output->grid_area = {grid->track_base_x, grid->track_base_y, grid->track_area_width, grid->track_area_height};
        }
        if (element->embed) {
            output->has_embed = true;
            output->native_image_width = element->embedp()->native_image_width;
            output->native_image_height = element->embedp()->native_image_height;
            output->native_image_sized = element->embedp()->native_image_sized;
            output->broken_alt_fallback = element->embedp()->broken_alt_fallback;
            if (element->embedp()->flex) {
                output->flex = lam::own((FlexProp*)pool_alloc(context->pool, sizeof(FlexProp)));
                if (!output->flex) return false;
                *output->flex = *element->embedp()->flex;
            }
            if (const GridProp* grid = element->embedp()->grid) {
                output->has_grid = true;
                output->grid_columns_offset = grid->used_columns_offset;
                if (grid->used_track_text) {
                    output->grid_tracks = lam::own(pool_strdup(context->pool, grid->used_track_text));
                    if (!output->grid_tracks) return false;
                }
            }
        }
        if (element->ext) {
            memcpy(output->fragments, element->ext->frags, sizeof(output->fragments));
            output->fragment_presence_mask = element->ext->fragment_presence_mask;
            output->ruby_anchor = element->ext->ruby_column_anchor_x;
            output->ruby_width = element->ext->ruby_column_width;
            output->ruby_advance = element->ext->ruby_column_inline_advance;
            output->ruby_start_overhang = element->ext->ruby_column_start_overhang;
            output->ruby_empty_advance = element->ext->ruby_empty_inline_advance;
            output->ruby_simple = element->ext->has_simple_ruby_column_geometry;
            output->ruby_empty = element->ext->has_empty_ruby_base;
        }
        for (DomNode* child = element->first_child; child; child = child->next_sibling) {
            if (!layout_flow_capture_nodes(context, child, tail, depth + 1, remaining)) return false;
        }
    }
    return true;
}

LayoutFlowResult* layout_flow_result_capture(LayoutContext* context, DomNode* root) {
    if (!context || !root || !context->doc || !context->doc->view_tree) return nullptr;
    LayoutFlowResult* result = (LayoutFlowResult*)pool_calloc(context->pool, sizeof(LayoutFlowResult));
    if (!result) return nullptr;
    LayoutFlowNode sentinel = {};
    LayoutFlowNode* tail = &sentinel;
    int remaining = context->node_limit - context->node_count;
    bool complete = layout_flow_capture_nodes(context, root, &tail, 0, &remaining);
    result->first = sentinel.next;
    result->root = root;
    result->generation = context->doc->view_tree->measurement_cache_generation;
    result->input_revision = root->is_element() ? root->as_element()->intrinsic_input_revision : 0;
    result->style_epoch = context->doc->style_query_epoch;
    result->mutation_epoch = context->doc->mutation_epoch;
    if (!complete) { layout_flow_result_release(context->pool, result); return nullptr; }
    return result;
}

bool layout_flow_result_apply(LayoutContext* context, const LayoutFlowResult* result) {
    if (!context || !result || result->generation != context->doc->view_tree->measurement_cache_generation ||
        result->style_epoch != context->doc->style_query_epoch ||
        result->mutation_epoch != context->doc->mutation_epoch ||
        (result->root->is_element() && result->input_revision != result->root->as_element()->intrinsic_input_revision))
        return false;
    ScratchScope scratch(&context->scratch);
    // allocate owned payloads before publication; failure leaves live fragments intact.
    struct ReplayPayload { lam::Own<TextRect> rects; lam::Own<char> tracks; ReplayPayload* next; };
    ReplayPayload* first = nullptr;
    ReplayPayload** tail = &first;
    bool complete = true;
    for (const LayoutFlowNode* output = result->first; output; output = output->next) {
        if (!output->box.node->is_text() && !output->has_grid) continue;
        ReplayPayload* copy = (ReplayPayload*)scratch.alloc_zero(sizeof(ReplayPayload));
        if (!copy) { complete = false; break; }
        *tail = copy; tail = &copy->next;
        copy->rects = lam::own(layout_flow_clone_rects(context->pool, output->text_rects));
        if (output->grid_tracks) copy->tracks = lam::own(pool_strdup(context->pool, output->grid_tracks));
        if ((output->text_rects && !copy->rects) || (output->grid_tracks && !copy->tracks)) {
            complete = false; break;
        }
    }
    if (!complete) {
        for (ReplayPayload* copy = first; copy; copy = copy->next) {
            lam::free_owned_list(context->pool.get(), copy->rects);
            lam::free_owned(context->pool.get(), copy->tracks);
        }
        return false;
    }
    DomNode* root = result->root;
    float x = root->x, y = root->y, width = root->width, height = root->height;
    ReplayPayload* payload = first;
    for (const LayoutFlowNode* output = result->first; output; output = output->next) {
        DomNode* node = output->box.node;
        if (node->is_element()) {
            DomElement* element = node->as_element();
            // ancestor replay replaces descendants' live fragments, but their owned query entries survive.
            flex_retained_forget_content(element);
            css_used_geometry_restore(context, element, output->geometry);
            output->block_output.restore(element->block_mut());
            if (element->blk) {
                BlockProp* block = element->block_mut();
                block->text_box_trim_applied = output->text_trim_applied;
                block->text_box_trim_start_amount = output->text_trim_start;
                block->text_box_trim_end_amount = output->text_trim_end;
                block->initial_letter_trimmed_start_contribution = output->initial_letter_trimmed_start;
            }
            if (element->in_line && element->inl()->content_strut_establishes_line != output->inline_strut)
                element->ensure_inline(context)->content_strut_establishes_line = output->inline_strut;
            if (element->bound) {
                element->boundary_mut()->flow_margin = output->flow_margin;
                element->boundary_mut()->has_flow_margin = output->has_flow_margin;
                element->boundary_mut()->collapsed_through_mb = output->collapsed_through_mb;
                element->boundary_mut()->margin_chain_positive = output->margin_chain_positive;
                element->boundary_mut()->margin_chain_negative = output->margin_chain_negative;
                element->boundary_mut()->has_clearance = output->has_clearance;
                element->boundary_mut()->clearance_in_margin_chain = output->clearance_in_margin_chain;
            }
            element->inline_line_number = output->inline_line_number;
            if (output->flex_item && node != root && element->flex_item()) {
                *element->fi = *output->flex_item;
            }
            if (output->position && element->position) *element->position = *output->position;
            if (output->table && element->table_prop()) *element->tb = *output->table;
            if (output->cell && element->cell_prop()) {
                TableCellProp* cell = element->td;
                *cell = *output->cell;
                CollapsedBorder* borders[] = {cell->top_resolved, cell->right_resolved,
                    cell->bottom_resolved, cell->left_resolved};
                for (unsigned side = 0; side < 4; side++) {
                    if (borders[side] && output->cell_border_present[side])
                        *borders[side] = output->cell_borders[side];
                }
            }
            if (output->has_grid_item && element->grid_item()) {
                GridItemProp* grid = element->grid_item();
                grid->computed_grid_row_start = output->grid_placement[0];
                grid->computed_grid_row_end = output->grid_placement[1];
                grid->computed_grid_column_start = output->grid_placement[2];
                grid->computed_grid_column_end = output->grid_placement[3];
                grid->track_base_x = output->grid_area.x; grid->track_base_y = output->grid_area.y;
                grid->track_area_width = output->grid_area.width; grid->track_area_height = output->grid_area.height;
            }
            if (output->has_embed && element->embed) {
                element->embed->native_image_width = output->native_image_width;
                element->embed->native_image_height = output->native_image_height;
                element->embed->native_image_sized = output->native_image_sized;
                element->embed->broken_alt_fallback = output->broken_alt_fallback;
                if (output->flex && element->embedp()->flex) *element->embed->flex = *output->flex;
                if (output->has_grid && element->embedp()->grid) {
                    GridProp* grid = element->embed->grid;
                    lam::free_owned(context->pool.get(), grid->used_track_text);
                    grid->used_track_text = payload->tracks;
                    grid->used_columns_offset = output->grid_columns_offset;
                }
            }
            if (output->has_grid) payload = payload->next;

            if (element->ext) {
                memcpy(element->ext->frags, output->fragments, sizeof(output->fragments));
                element->ext->fragment_presence_mask = output->fragment_presence_mask;
                element->ext->ruby_column_anchor_x = output->ruby_anchor;
                element->ext->ruby_column_width = output->ruby_width;
                element->ext->ruby_column_inline_advance = output->ruby_advance;
                element->ext->ruby_column_start_overhang = output->ruby_start_overhang;
                element->ext->ruby_empty_inline_advance = output->ruby_empty_advance;
                element->ext->has_simple_ruby_column_geometry = output->ruby_simple;
                element->ext->has_empty_ruby_base = output->ruby_empty;
            }
        } else if (node->is_text()) {
            DomText* text = node->as_text();
            context->doc->view_tree->recycle_text_rects(text->rect);
            text->rect = lam::view_prop(payload->rects.get());
            text->font = lam::view_ref(output->text_font);
            payload = payload->next;
        }
        layout_view_snapshot_restore(&output->box);
        if (node != root && node->is_element() && node->as_element()->scroller) {
            ViewBlock* block = lam::unsafe_view_block_element_storage(node->as_element());
            update_scroller(block, block->content_width,
                layout_border_size_from_content_box(block, block->content_height, false));
        }
    }
    // flex owns the result root's final allocation and external position.
    layout_set_view_geometry(static_cast<View*>(root), x, y, width, height);
    return true;
}

LayoutRunModeScope::LayoutRunModeScope(::LayoutContext* l, RunMode mode)
    : lycon(l), saved_run_mode(RunMode::PerformLayout) {
    if (!lycon) return;
    saved_run_mode = lycon->run_mode;
    lycon->run_mode = mode;
}

LayoutRunModeScope::~LayoutRunModeScope() {
    if (!lycon) return;
    lycon->run_mode = saved_run_mode;
}

LayoutMeasureScope::LayoutMeasureScope(::LayoutContext* l, ::DomNode* measure_elmt)
    : lycon(l),
      saved_block{},
      saved_line{},
      saved_font{},
      saved_elmt(nullptr),
      saved_run_mode(RunMode::PerformLayout),
      saved_sizing_mode(SizingMode::InherentSize),
      saved_available_space(AvailableSpace::make_indefinite()),
      saved_views(nullptr),
      snapshot_scope{} {
    if (!lycon) return;

    saved_block = lycon->block;
    saved_line = lycon->line;
    saved_font = lycon->font;
    saved_elmt = lycon->elmt;
    saved_run_mode = lycon->run_mode;
    saved_sizing_mode = lycon->sizing_mode;
    saved_available_space = lycon->available_space;
    saved_views = lam::up(arraylist_new(8));
    snapshot_scope = scratch_scope_begin(&lycon->scratch);
    layout_measure_snapshot_append(lycon, &snapshot_scope, saved_views, measure_elmt);
    // a trial layout rewrites line and text geometry the snapshot does not
    // restore, so no enclosing flex item may replay its recorded content pass.
    flex_retained_forget_containing_content(measure_elmt);

    lycon->run_mode = RunMode::ComputeSize;
    lycon->elmt = lam::up(measure_elmt);
}

LayoutMeasureScope::~LayoutMeasureScope() {
    if (!lycon) return;

    lycon->block = saved_block;
    lycon->line = saved_line;
    lycon->font = saved_font;
    lycon->elmt = lam::up(saved_elmt);
    lycon->run_mode = saved_run_mode;
    lycon->sizing_mode = saved_sizing_mode;
    lycon->available_space = saved_available_space;
    layout_measure_snapshot_restore(lycon, saved_views);
    saved_views = nullptr;
    scratch_scope_end(&lycon->scratch, &snapshot_scope);
}

KnownDimensions layout_known_dimensions_from_block(::ViewBlock* block) {
    KnownDimensions known = known_dimensions_none();
    if (block && block->blk && block->block_mut()->given_width > 0.0f) {
        known.width = block->block()->given_width;
        known.has_width = true;
    }
    if (block && block->blk && block->block_mut()->given_height > 0.0f) {
        known.height = block->block()->given_height;
        known.has_height = true;
    }
    return known;
}

KnownDimensions layout_known_dimensions_from_context(::LayoutContext* lycon) {
    KnownDimensions known = known_dimensions_none();
    if (!lycon) return known;

    if (lycon->block.given_width >= 0.0f) {
        known.width = lycon->block.given_width;
        known.has_width = true;
    }
    if (lycon->block.given_height >= 0.0f) {
        known.height = lycon->block.given_height;
        known.has_height = true;
    }
    return known;
}

bool layout_pass_cache_get(::LayoutContext* lycon, ::DomElement* element,
    KnownDimensions known_dimensions, SizeF* out_size, const char* label) {
    if (!lycon) return false;
    return layout_pass_cache_get_for_space(lycon, element, known_dimensions,
                                           lycon->available_space, out_size, label);
}

void layout_pass_cache_store(::LayoutContext* lycon, ::DomElement* element,
    KnownDimensions known_dimensions, SizeF result, const char* label) {
    if (!lycon) return;
    layout_pass_cache_store_for_space(lycon, element, known_dimensions,
                                      lycon->available_space, result, label);
}

bool layout_pass_cache_get_for_space(::LayoutContext* lycon, ::DomElement* element,
    KnownDimensions known_dimensions, AvailableSpace available_space,
    SizeF* out_size, const char* label) {
    if (!lycon || !element || !element->layout_cache || !out_size) return false;
    if (lycon->run_mode != RunMode::ComputeSize) return false;
    // the prop pool survives document updates; unkeyed scalar entries do not.
    layout_pass_ensure_cache(lycon, element);

    if (layout_cache_get(element->layout_cache, known_dimensions, available_space,
                         lycon->run_mode, out_size)) {
        g_layout_cache_hits++;
        layout_profiler_note_cache_hit(&lycon->profiler);
        log_info("%s %s CACHE HIT: element=%s, size=(%.1f x %.1f), mode=%d",
                 element->source_loc(), label ? label : "LAYOUT",
                 element->node_name(), out_size->width, out_size->height,
                 (int)lycon->run_mode); // INT_CAST_OK: enum for log
        layout_debug_log(lycon, LAYOUT_DEBUG_CACHE, element,
                         "%s cache hit size=(%.1f x %.1f)",
                         label ? label : "LAYOUT", out_size->width, out_size->height);
        return true;
    }

    g_layout_cache_misses++;
    layout_profiler_note_cache_miss(&lycon->profiler);
    log_debug("%s %s CACHE MISS: element=%s, mode=%d",
              element->source_loc(), label ? label : "LAYOUT",
              element->node_name(), (int)lycon->run_mode); // INT_CAST_OK: enum for log
    layout_debug_log(lycon, LAYOUT_DEBUG_CACHE, element,
                     "%s cache miss", label ? label : "LAYOUT");
    return false;
}

LayoutCache* layout_pass_ensure_cache(::LayoutContext* lycon, ::DomElement* element) {
    if (!lycon || !element) return nullptr;
    LayoutCache* cache = element->layout_cache;
    uint32_t generation = lycon->doc && lycon->doc->view_tree
        ? lycon->doc->view_tree->measurement_cache_generation : 0;
    if (!cache && lycon->pool) {
        cache = (LayoutCache*)pool_calloc(lycon->pool, sizeof(LayoutCache));
        if (cache) {
            layout_cache_init(cache, generation);
            element->layout_cache = lam::view_prop(cache);
            if (element->doc) element->doc->services.layout_cache_allocations++;
        }
    } else if (cache && cache->generation != generation) {
        layout_cache_clear(cache, lycon->pool);
        cache->generation = generation;
    }
    return cache;
}

void layout_pass_cache_store_for_space(::LayoutContext* lycon, ::DomElement* element,
    KnownDimensions known_dimensions, AvailableSpace available_space,
    SizeF result, const char* label) {
    if (!lycon || !element) return;
    if (lycon->run_mode != RunMode::ComputeSize) return;

    LayoutCache* cache = layout_pass_ensure_cache(lycon, element);
    if (!cache) return;

    layout_cache_store(cache, known_dimensions, available_space, lycon->run_mode, result);
    g_layout_cache_stores++;
    log_debug("%s %s CACHE STORE: element=%s, size=(%.1f x %.1f), mode=%d",
              element->source_loc(), label ? label : "LAYOUT",
              element->node_name(), result.width, result.height,
              (int)lycon->run_mode); // INT_CAST_OK: enum for log
    layout_debug_log(lycon, LAYOUT_DEBUG_CACHE, element,
                     "%s cache store size=(%.1f x %.1f)",
                     label ? label : "LAYOUT", result.width, result.height);
}

} // namespace radiant
