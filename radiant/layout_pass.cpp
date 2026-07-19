#include "layout.hpp"
#include "view.hpp"

#include "../lib/log.h"
#include "../lib/scratch_arena.h"

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
    float cached_min_content_width;
    float cached_max_content_width;
    bool has_cached_intrinsic_widths;
    bool measuring_intrinsic_width;
    bool has_form;
    float form_intrinsic_width;
    float form_intrinsic_height;
    bool has_block_prop;
    float block_given_width;
    float block_given_height;
    CssEnum block_given_width_type;
    CssEnum block_given_height_type;
};

static void layout_measure_snapshot_append(::LayoutContext* lycon,
                                            ArrayList* snapshots, ::DomNode* node) {
    if (!lycon || !snapshots || !node) return;

    LayoutViewSnapshot* snapshot =
        (LayoutViewSnapshot*)scratch_calloc(&lycon->scratch, sizeof(LayoutViewSnapshot));
    if (!snapshot) return;

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
        snapshot->cached_min_content_width = element->layout_cache
            ? element->layout_cache->intrinsic_min_content_width : 0.0f;
        snapshot->cached_max_content_width = element->layout_cache
            ? element->layout_cache->intrinsic_max_content_width : 0.0f;
        snapshot->has_cached_intrinsic_widths = element->has_cached_intrinsic_widths();
        snapshot->measuring_intrinsic_width = element->measuring_intrinsic_width();
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
        }
    }

    if (!arraylist_append(snapshots, snapshot)) {
        scratch_free(&lycon->scratch, snapshot);
        return;
    }

    if (node->is_element()) {
        for (::DomNode* child = node->as_element()->first_child; child; child = child->next_sibling) {
            layout_measure_snapshot_append(lycon, snapshots, child);
        }
    }
}

static void layout_measure_snapshot_restore(::LayoutContext* lycon, ArrayList* snapshots) {
    if (!lycon || !snapshots) return;

    for (int i = snapshots->length - 1; i >= 0; i--) {
        LayoutViewSnapshot* snapshot = (LayoutViewSnapshot*)snapshots->data[i];
        if (!snapshot || !snapshot->node) {
            scratch_free(&lycon->scratch, snapshot);
            continue;
        }

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
            if (element->layout_cache) {
                element->layout_cache->intrinsic_min_content_width = snapshot->cached_min_content_width;
                element->layout_cache->intrinsic_max_content_width = snapshot->cached_max_content_width;
            }
            element->set_has_cached_intrinsic_widths(snapshot->has_cached_intrinsic_widths);
            element->set_measuring_intrinsic_width(snapshot->measuring_intrinsic_width);
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
            }
        }

        scratch_free(&lycon->scratch, snapshot);
    }
    arraylist_free(snapshots);
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
      saved_views(nullptr) {
    if (!lycon) return;

    saved_block = lycon->block;
    saved_line = lycon->line;
    saved_font = lycon->font;
    saved_elmt = lycon->elmt;
    saved_run_mode = lycon->run_mode;
    saved_sizing_mode = lycon->sizing_mode;
    saved_available_space = lycon->available_space;
    saved_views = arraylist_new(8);
    layout_measure_snapshot_append(lycon, saved_views, measure_elmt);

    lycon->run_mode = RunMode::ComputeSize;
    lycon->elmt = measure_elmt;
}

LayoutMeasureScope::~LayoutMeasureScope() {
    if (!lycon) return;

    lycon->block = saved_block;
    lycon->line = saved_line;
    lycon->font = saved_font;
    lycon->elmt = saved_elmt;
    lycon->run_mode = saved_run_mode;
    lycon->sizing_mode = saved_sizing_mode;
    lycon->available_space = saved_available_space;
    layout_measure_snapshot_restore(lycon, saved_views);
    saved_views = nullptr;
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
    uint32_t generation = lycon->doc && lycon->doc->view_tree
        ? lycon->doc->view_tree->layout_generation : 0;
    if (element->layout_cache->generation != generation) {
        // A retained prop block may survive reflow, but its measurements may
        // only be consumed by the generation that produced them.
        layout_cache_init(element->layout_cache, generation);
        return false;
    }

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
    if (!cache && lycon->pool) {
        cache = (LayoutCache*)pool_calloc(lycon->pool, sizeof(LayoutCache));
        if (cache) {
            uint32_t generation = lycon->doc && lycon->doc->view_tree
                ? lycon->doc->view_tree->layout_generation : 0;
            layout_cache_init(cache, generation);
            element->layout_cache = cache;
            if (element->doc) element->doc->services.layout_cache_allocations++;
        }
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
