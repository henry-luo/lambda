#include "layout_pass.hpp"

#include "../lib/log.h"

namespace radiant {

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
      saved_available_space(AvailableSpace::make_indefinite()) {
    if (!lycon) return;

    saved_block = lycon->block;
    saved_line = lycon->line;
    saved_font = lycon->font;
    saved_elmt = lycon->elmt;
    saved_run_mode = lycon->run_mode;
    saved_sizing_mode = lycon->sizing_mode;
    saved_available_space = lycon->available_space;

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
}

KnownDimensions layout_known_dimensions_from_block(::ViewBlock* block) {
    KnownDimensions known = known_dimensions_none();
    if (block && block->blk && block->blk->given_width > 0.0f) {
        known.width = block->blk->given_width;
        known.has_width = true;
    }
    if (block && block->blk && block->blk->given_height > 0.0f) {
        known.height = block->blk->given_height;
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
    if (!lycon || !element || !element->layout_cache || !out_size) return false;

    if (layout_cache_get(element->layout_cache, known_dimensions, lycon->available_space,
                         lycon->run_mode, out_size)) {
        g_layout_cache_hits++;
        log_info("%s %s CACHE HIT: element=%s, size=(%.1f x %.1f), mode=%d",
                 element->source_loc(), label ? label : "LAYOUT",
                 element->node_name(), out_size->width, out_size->height,
                 (int)lycon->run_mode); // INT_CAST_OK: enum for log
        return true;
    }

    g_layout_cache_misses++;
    log_debug("%s %s CACHE MISS: element=%s, mode=%d",
              element->source_loc(), label ? label : "LAYOUT",
              element->node_name(), (int)lycon->run_mode); // INT_CAST_OK: enum for log
    return false;
}

void layout_pass_cache_store(::LayoutContext* lycon, ::DomElement* element,
    KnownDimensions known_dimensions, SizeF result, const char* label) {
    if (!lycon || !element) return;

    LayoutCache* cache = element->layout_cache;
    if (!cache && lycon->pool) {
        cache = (LayoutCache*)pool_calloc(lycon->pool, sizeof(LayoutCache));
        if (cache) {
            layout_cache_init(cache);
            element->layout_cache = cache;
        }
    }
    if (!cache) return;

    layout_cache_store(cache, known_dimensions, lycon->available_space, lycon->run_mode, result);
    g_layout_cache_stores++;
    log_debug("%s %s CACHE STORE: element=%s, size=(%.1f x %.1f), mode=%d",
              element->source_loc(), label ? label : "LAYOUT",
              element->node_name(), result.width, result.height,
              (int)lycon->run_mode); // INT_CAST_OK: enum for log
}

} // namespace radiant
