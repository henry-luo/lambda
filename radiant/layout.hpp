#ifndef LAYOUT_HPP
#define LAYOUT_HPP
#pragma once

// ============================================================================
// DIMENSION CONVENTION: All layout positions and sizes are float.
// View.x, y, width, height and all derived measurements (padding, margin,
// border, gap, offset, content_width, etc.) must use float, never int.
// If an (int) cast is truly needed, mark it: // INT_CAST_OK: <reason>
// Run `make check-int-cast` to verify compliance.
// ============================================================================

#include "view.hpp"
#include "grid_track.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style.hpp"
#include "../lib/arraylist.h"
#include "../lib/arraylist.hpp"
#include "../lib/hashmap.h"
#include "../lib/scratch_arena.h"
#include <cassert>
#include <cstdint>
#include <stdarg.h>
#include <math.h>

typedef struct LayoutContext LayoutContext;

// ============================================================================
// Layout Safety Guards
// ============================================================================

// maximum DOM nesting depth. guards call-stack overflow in layout_flow_node()
// and layout_abs_block() — both use the same lycon->depth counter.
#ifdef NDEBUG
constexpr int MAX_LAYOUT_DEPTH = 300;
#else
constexpr int MAX_LAYOUT_DEPTH = 100;
#endif

constexpr int MAX_LAYOUT_NODES = 50000;
constexpr int MAX_FLEX_DEPTH = 16;
constexpr int MAX_GRID_DEPTH = 4;
constexpr int MAX_IFRAME_DEPTH = 3;

// ============================================================================
// Available Space Type System
// ============================================================================

enum AvailableSizeType {
    AVAILABLE_SIZE_DEFINITE,
    AVAILABLE_SIZE_INDEFINITE,
    AVAILABLE_SIZE_MIN_CONTENT,
    AVAILABLE_SIZE_MAX_CONTENT
};

// tier-3: layout-transient, valid within pass
struct AvailableSize {
    AvailableSizeType type;
    float value;

    static AvailableSize make_definite(float px) {
        AvailableSize s;
        s.type = AVAILABLE_SIZE_DEFINITE;
        s.value = px;
        return s;
    }

    static AvailableSize make_indefinite() {
        AvailableSize s;
        s.type = AVAILABLE_SIZE_INDEFINITE;
        s.value = 0;
        return s;
    }

    static AvailableSize make_min_content() {
        AvailableSize s;
        s.type = AVAILABLE_SIZE_MIN_CONTENT;
        s.value = 0;
        return s;
    }

    static AvailableSize make_max_content() {
        AvailableSize s;
        s.type = AVAILABLE_SIZE_MAX_CONTENT;
        s.value = 0;
        return s;
    }

    bool is_definite() const { return type == AVAILABLE_SIZE_DEFINITE; }
    bool is_indefinite() const { return type == AVAILABLE_SIZE_INDEFINITE; }
    bool is_min_content() const { return type == AVAILABLE_SIZE_MIN_CONTENT; }
    bool is_max_content() const { return type == AVAILABLE_SIZE_MAX_CONTENT; }

    bool is_intrinsic() const {
        return is_min_content() || is_max_content();
    }

    float to_px_or_zero() const {
        return is_definite() ? value : 0;
    }

    float resolve(float fallback_for_indefinite) const {
        if (is_definite()) return value;
        if (is_indefinite()) return fallback_for_indefinite;
        return 0;
    }
};

// tier-3: layout-transient, valid within pass
struct AvailableSpace {
    AvailableSize width;
    AvailableSize height;

    static AvailableSpace make_definite(float w, float h) {
        AvailableSpace s;
        s.width = AvailableSize::make_definite(w);
        s.height = AvailableSize::make_definite(h);
        return s;
    }

    static AvailableSpace make_indefinite() {
        AvailableSpace s;
        s.width = AvailableSize::make_indefinite();
        s.height = AvailableSize::make_indefinite();
        return s;
    }

    static AvailableSpace make_min_content() {
        AvailableSpace s;
        s.width = AvailableSize::make_min_content();
        s.height = AvailableSize::make_indefinite();
        return s;
    }

    static AvailableSpace make_max_content() {
        AvailableSpace s;
        s.width = AvailableSize::make_max_content();
        s.height = AvailableSize::make_indefinite();
        return s;
    }

    static AvailableSpace make_width_definite(float w) {
        AvailableSpace s;
        s.width = AvailableSize::make_definite(w);
        s.height = AvailableSize::make_indefinite();
        return s;
    }

    bool is_intrinsic_sizing() const {
        return width.is_intrinsic() || height.is_intrinsic();
    }

    bool is_width_min_content() const {
        return width.is_min_content();
    }

    bool is_width_max_content() const {
        return width.is_max_content();
    }
};

inline float compute_shrink_to_fit_width(float min_content, float max_content, AvailableSize available) {
    if (available.is_indefinite() || available.is_max_content()) {
        return max_content;
    }
    if (available.is_min_content()) {
        return min_content;
    }
    float avail = available.value;
    if (avail < min_content) return min_content;
    if (avail > max_content) return max_content;
    return avail;
}

// ============================================================================
// Intrinsic Sizing and Measurement
// ============================================================================

// tier-3: layout-transient, valid within pass
struct TextIntrinsicWidths {
    float min_content;
    float max_content;
};

float calculate_min_content_width(LayoutContext* lycon, DomNode* node);
float calculate_max_content_width(LayoutContext* lycon, DomNode* node);
float calculate_min_content_height(LayoutContext* lycon, DomNode* node, float width);
float calculate_max_content_height(LayoutContext* lycon, DomNode* node, float width);
float calculate_fit_content_width(LayoutContext* lycon, DomNode* node, float available_width);

TextIntrinsicWidths measure_text_intrinsic_widths(LayoutContext* lycon,
                                                   const char* text,
                                                   size_t length,
                                                   CssEnum text_transform = CSS_VALUE_NONE,
                                                   CssEnum font_variant = CSS_VALUE_NONE,
                                                   CssEnum white_space = CSS_VALUE_NORMAL,
                                                   CssEnum overflow_wrap = CSS_VALUE_NORMAL,
                                                   CssEnum word_break = CSS_VALUE_NORMAL);
float measure_direct_text_children_intrinsic_width(LayoutContext* lycon,
                                                   DomElement* element,
                                                   bool use_min_content,
                                                   CssEnum text_transform);

CssEnum get_element_text_transform(DomElement* element);
CssEnum get_element_font_variant(DomElement* element);

float compute_text_height_at_width(LayoutContext* lycon,
                                    const char* text,
                                    size_t length,
                                    float available_width,
                                    float line_height,
                                    CssEnum text_transform = CSS_VALUE_NONE,
                                    CssEnum font_variant = CSS_VALUE_NONE);

IntrinsicSizes measure_element_intrinsic_widths(LayoutContext* lycon, DomElement* element,
                                                 bool content_only = false);

// tier-3: layout-transient, valid within pass
struct IntrinsicSizesBidirectional {
    float min_content_width;
    float max_content_width;
    float min_content_height;
    float max_content_height;
};

IntrinsicSizesBidirectional measure_intrinsic_sizes(
    LayoutContext* lycon,
    ViewBlock* element,
    AvailableSpace available_space
);

inline IntrinsicSizes intrinsic_sizes_for_axis(IntrinsicSizesBidirectional sizes, bool is_row_axis) {
    if (is_row_axis) {
        return {sizes.min_content_width, sizes.max_content_width};
    }
    return {sizes.min_content_height, sizes.max_content_height};
}

// tier-3: layout-transient, valid within pass
struct CellIntrinsicWidths {
    float min_width;
    float max_width;
};

// tier-3: layout-transient, valid within pass
typedef struct IntrinsicSize {
    float min_width;
    float max_width;
    float min_height;
    float max_height;
    bool has_baseline;
    float first_baseline;
    float last_baseline;
} IntrinsicSize;

IntrinsicSize layout_measure_replaced(LayoutContext* lycon, ViewBlock* block, AvailableSpace space);
IntrinsicSize layout_measure_form_control(LayoutContext* lycon, ViewBlock* block, AvailableSpace space);
float layout_select_combo_intrinsic_width(float max_text_width, bool has_ua_arrow);

IntrinsicSizes layout_measure_intrinsic_widths(LayoutContext* lycon, DomElement* element,
    const char* log_context = nullptr, bool content_only = false);

TextIntrinsicWidths layout_measure_text_intrinsic_widths(LayoutContext* lycon,
    const char* text, size_t length,
    CssEnum text_transform = CSS_VALUE_NONE,
    CssEnum font_variant = CSS_VALUE_NONE,
    CssEnum white_space = CSS_VALUE_NORMAL,
    CssEnum overflow_wrap = CSS_VALUE_NORMAL,
    CssEnum word_break = CSS_VALUE_NORMAL,
    const char* log_context = nullptr);

namespace radiant {

enum class RunMode : uint8_t {
    ComputeSize = 0,
    PerformLayout = 1,
    PerformHiddenLayout = 2
};

enum class SizingMode : uint8_t {
    InherentSize = 0,
    ContentSize = 1
};

} // namespace radiant

extern int64_t g_layout_cache_hits;
extern int64_t g_layout_cache_misses;
extern int64_t g_layout_cache_stores;

namespace radiant {

#define LAYOUT_CACHE_SIZE 9

// tier-3: layout-transient, valid within pass
struct KnownDimensions {
    float width;
    float height;
    bool has_width;
    bool has_height;
};

inline KnownDimensions known_dimensions_none() {
    return {0.0f, 0.0f, false, false};
}

// tier-3: layout-transient, valid within pass
struct SizeF {
    float width;
    float height;
};

inline SizeF size_f(float w, float h) {
    return {w, h};
}

inline SizeF size_f_zero() {
    return {0.0f, 0.0f};
}

// tier-3: layout-transient, valid within pass
struct CacheEntry {
    KnownDimensions known_dimensions;
    AvailableSpace available_space;
    SizeF computed_size;
    bool valid;
};

// tier-3: layout-transient, valid within pass
struct LayoutCache {
    CacheEntry final_layout;
    CacheEntry measure_entries[LAYOUT_CACHE_SIZE];
    float intrinsic_min_content_width;
    float intrinsic_max_content_width;
    bool is_empty;
    uint32_t generation;
};

inline void layout_cache_init(LayoutCache* cache, uint32_t generation = 0) {
    cache->final_layout.valid = false;
    for (int i = 0; i < LAYOUT_CACHE_SIZE; i++) {
        cache->measure_entries[i].valid = false;
    }
    cache->is_empty = true;
    cache->generation = generation;
}

inline void layout_cache_clear(LayoutCache* cache) {
    layout_cache_init(cache, cache ? cache->generation : 0);
}

inline int layout_cache_compute_slot(
    KnownDimensions known_dimensions,
    AvailableSpace available_space
) {
    bool has_width = known_dimensions.has_width;
    bool has_height = known_dimensions.has_height;

    if (has_width && has_height) return 0;
    if (has_width) {
        return available_space.height.is_min_content() ? 2 : 1;
    }
    if (has_height) {
        return available_space.width.is_min_content() ? 4 : 3;
    }

    bool width_is_min = available_space.width.is_min_content();
    bool height_is_min = available_space.height.is_min_content();

    if (!width_is_min && !height_is_min) return 5;
    if (!width_is_min && height_is_min) return 6;
    if (width_is_min && !height_is_min) return 7;
    return 8;
}

inline bool layout_cache_constraints_match(
    CacheEntry* entry,
    KnownDimensions known,
    AvailableSpace available,
    float tolerance = 0.1f
) {
    if (!entry->valid) return false;

    if (entry->known_dimensions.has_width != known.has_width) return false;
    if (entry->known_dimensions.has_height != known.has_height) return false;

    if (known.has_width) {
        float diff = entry->known_dimensions.width - known.width;
        if (diff < -tolerance || diff > tolerance) return false;
    }
    if (known.has_height) {
        float diff = entry->known_dimensions.height - known.height;
        if (diff < -tolerance || diff > tolerance) return false;
    }

    if (entry->available_space.width.type != available.width.type) return false;
    if (entry->available_space.height.type != available.height.type) return false;

    if (available.width.is_definite()) {
        float diff = entry->available_space.width.value - available.width.value;
        if (diff < -tolerance || diff > tolerance) return false;
    }
    if (available.height.is_definite()) {
        float diff = entry->available_space.height.value - available.height.value;
        if (diff < -tolerance || diff > tolerance) return false;
    }

    return true;
}

inline bool layout_cache_get(
    LayoutCache* cache,
    KnownDimensions known_dimensions,
    AvailableSpace available_space,
    RunMode mode,
    SizeF* out_size
) {
    if (cache->is_empty) return false;

    if (mode == RunMode::PerformLayout) {
        if (layout_cache_constraints_match(&cache->final_layout, known_dimensions, available_space)) {
            *out_size = cache->final_layout.computed_size;
            return true;
        }
        return false;
    }

    int slot = layout_cache_compute_slot(known_dimensions, available_space);
    CacheEntry* entry = &cache->measure_entries[slot];

    if (layout_cache_constraints_match(entry, known_dimensions, available_space)) {
        *out_size = entry->computed_size;
        return true;
    }

    return false;
}

inline void layout_cache_store(
    LayoutCache* cache,
    KnownDimensions known_dimensions,
    AvailableSpace available_space,
    RunMode mode,
    SizeF result
) {
    cache->is_empty = false;

    CacheEntry* entry;
    if (mode == RunMode::PerformLayout) {
        entry = &cache->final_layout;
    } else {
        int slot = layout_cache_compute_slot(known_dimensions, available_space);
        entry = &cache->measure_entries[slot];
    }

    entry->known_dimensions = known_dimensions;
    entry->available_space = available_space;
    entry->computed_size = result;
    entry->valid = true;
}

} // namespace radiant

// ============================================================================
// Box Metrics
// ============================================================================

// tier-3: layout-transient, valid within pass
typedef struct BoxEdges {
    float left;
    float right;
    float top;
    float bottom;
} BoxEdges;

// tier-3: layout-transient, valid within pass
typedef struct BoxMetrics {
    BoxEdges margin;
    BoxEdges padding;
    BoxEdges border;
    float margin_h;
    float margin_v;
    float padding_h;
    float padding_v;
    float border_h;
    float border_v;
    float pad_border_h;
    float pad_border_v;
} BoxMetrics;

BoxMetrics layout_box_metrics(ViewBlock* block);
BoxMetrics layout_boundary_metrics(const BoundaryProp* bound);

float layout_padding_border_width(ViewBlock* block);
float layout_padding_border_height(ViewBlock* block);
float layout_boundary_padding_border_axis(const BoundaryProp* bound, bool horizontal);

float layout_content_width_from_border_box(ViewBlock* block, float border_width);
float layout_content_height_from_border_box(ViewBlock* block, float border_height);
float layout_border_width_from_content_box(ViewBlock* block, float content_width);
float layout_border_height_from_content_box(ViewBlock* block, float content_height);
float layout_boundary_content_size_from_border_box(const BoundaryProp* bound, float border_size, bool horizontal);
float layout_boundary_border_size_from_content_box(const BoundaryProp* bound, float content_size, bool horizontal);
float layout_content_size_from_border_box(ViewBlock* block, float border_size, bool horizontal);
float layout_border_size_from_content_box(ViewBlock* block, float content_size, bool horizontal);
float layout_css_size_to_content_box(const BoundaryProp* bound, CssEnum box_sizing, float css_size, bool horizontal);
float layout_css_size_to_border_box(const BoundaryProp* bound, CssEnum box_sizing, float css_size, bool horizontal);
float layout_floor_border_box_width(ViewBlock* block, float border_width);
float layout_floor_border_box_height(ViewBlock* block, float border_height);
float layout_floor_border_box_axis(ViewBlock* block, float border_size, bool horizontal);

static inline CssEnum layout_box_sizing(ViewBlock* block) {
    return (block && block->blk) ? block->block()->box_sizing : CSS_VALUE_CONTENT_BOX;
}

static inline bool layout_uses_border_box(ViewBlock* block) {
    return layout_box_sizing(block) == CSS_VALUE_BORDER_BOX;
}

float layout_apply_min_max_width(ViewBlock* block, float width, bool width_is_border_box);
float layout_apply_min_max_height(ViewBlock* block, float height, bool height_is_border_box);
float layout_apply_min_max_axis(ViewBlock* block, float size, bool horizontal, bool size_is_border_box);
float layout_clamp_min_max_width(ViewBlock* block, float width);
float layout_clamp_min_max_height(ViewBlock* block, float height);

static inline float layout_clamp_positive_min_max_width(ViewBlock* block, float width) {
    if (!block || !block->blk) return width;
    float constrained_width = width;
    if (block->block()->given_max_width > 0.0f && constrained_width > block->block()->given_max_width) {
        constrained_width = block->block()->given_max_width;
    }
    if (block->block()->given_min_width > 0.0f && constrained_width < block->block()->given_min_width) {
        constrained_width = block->block()->given_min_width;
    }
    return constrained_width;
}

static inline float layout_explicit_min_width_or(ViewBlock* block, float fallback) {
    return (block && block->blk && block->block()->given_min_width >= 0.0f)
        ? block->block()->given_min_width
        : fallback;
}

static inline float layout_explicit_min_height_or(ViewBlock* block, float fallback) {
    return (block && block->blk && block->block()->given_min_height >= 0.0f)
        ? block->block()->given_min_height
        : fallback;
}

static inline float layout_explicit_min_axis_or(ViewBlock* block, bool horizontal, float fallback) {
    return horizontal
        ? layout_explicit_min_width_or(block, fallback)
        : layout_explicit_min_height_or(block, fallback);
}

static inline float layout_explicit_max_width_or(ViewBlock* block, float fallback) {
    return (block && block->blk && block->block()->given_max_width >= 0.0f)
        ? block->block()->given_max_width
        : fallback;
}

static inline float layout_explicit_max_height_or(ViewBlock* block, float fallback) {
    return (block && block->blk && block->block()->given_max_height >= 0.0f)
        ? block->block()->given_max_height
        : fallback;
}

static inline bool layout_has_explicit_min_width(ViewBlock* block) {
    return layout_explicit_min_width_or(block, -1.0f) >= 0.0f;
}

static inline bool layout_has_explicit_min_height(ViewBlock* block) {
    return layout_explicit_min_height_or(block, -1.0f) >= 0.0f;
}

static inline float layout_positive_min_width(ViewBlock* block) {
    float min_width = layout_explicit_min_width_or(block, -1.0f);
    return min_width > 0.0f ? min_width : 0.0f;
}

static inline float layout_positive_min_height(ViewBlock* block) {
    float min_height = layout_explicit_min_height_or(block, -1.0f);
    return min_height > 0.0f ? min_height : 0.0f;
}

static inline float layout_positive_min_axis(ViewBlock* block, bool horizontal) {
    return horizontal
        ? layout_positive_min_width(block)
        : layout_positive_min_height(block);
}

static inline float layout_positive_max_width_or(ViewBlock* block, float fallback) {
    float max_width = layout_explicit_max_width_or(block, fallback);
    return max_width > 0.0f ? max_width : fallback;
}

static inline float layout_positive_max_height_or(ViewBlock* block, float fallback) {
    float max_height = layout_explicit_max_height_or(block, fallback);
    return max_height > 0.0f ? max_height : fallback;
}

static inline float layout_positive_max_axis_or(ViewBlock* block, bool horizontal, float fallback) {
    return horizontal
        ? layout_positive_max_width_or(block, fallback)
        : layout_positive_max_height_or(block, fallback);
}

static inline float layout_floor_min_width(ViewBlock* block, float width) {
    if (!block || !block->blk || block->block()->given_min_width < 0.0f) return width;
    return width < block->block()->given_min_width ? block->block()->given_min_width : width;
}

static inline float layout_floor_min_height(ViewBlock* block, float height) {
    if (!block || !block->blk || block->block()->given_min_height < 0.0f) return height;
    return height < block->block()->given_min_height ? block->block()->given_min_height : height;
}

static inline void layout_apply_positive_min_max_contribution(ViewBlock* block, bool horizontal,
                                                              float* min_size, float* max_size) {
    if (!block || !block->blk) return;
    if (horizontal) {
        if (min_size && block->block()->given_min_width > 0.0f && *min_size < block->block()->given_min_width) {
            *min_size = block->block()->given_min_width;
        }
        if (max_size && block->block()->given_max_width > 0.0f && *max_size > block->block()->given_max_width) {
            *max_size = block->block()->given_max_width;
        }
    } else {
        if (min_size && block->block()->given_min_height > 0.0f && *min_size < block->block()->given_min_height) {
            *min_size = block->block()->given_min_height;
        }
        if (max_size && block->block()->given_max_height > 0.0f && *max_size > block->block()->given_max_height) {
            *max_size = block->block()->given_max_height;
        }
    }
}

static inline const CssValue* css_box_shorthand_side_value(const CssValue* value, int side) {
    if (!value) return nullptr;
    if (value->type != CSS_VALUE_TYPE_LIST) return value;
    int count = value->data.list.count;
    CssValue** values = value->data.list.values;
    if (count <= 0 || !values) return nullptr;
    int index = 0;
    if (side == 0) {
        index = 0;                                // top
    } else if (side == 1) {
        index = (count >= 2) ? 1 : 0;             // right
    } else if (side == 2) {
        index = (count >= 3) ? 2 : 0;             // bottom
    } else {
        index = (count >= 4) ? 3 : ((count >= 2) ? 1 : 0); // left
    }
    return index < count ? values[index] : nullptr;
}

static inline float layout_non_negative_free_space(float value) {
    return value > 0.0f ? value : 0.0f;
}

static inline int layout_count_auto_margins(bool start_auto, bool end_auto) {
    return (start_auto ? 1 : 0) + (end_auto ? 1 : 0);
}

static inline float layout_auto_margin_share(float free_space, int auto_margin_count) {
    return (auto_margin_count > 0 && free_space > 0.0f)
        ? free_space / (float)auto_margin_count
        : 0.0f;
}

static inline void layout_resolve_auto_margin_pair(float available_size, float border_box_size,
                                                   bool start_auto, bool end_auto,
                                                   float* start_margin, float* end_margin) {
    if (!start_margin || !end_margin) return;
    if (start_auto && end_auto) {
        float used_margin = layout_non_negative_free_space(
            (available_size - border_box_size) / 2.0f);
        *start_margin = used_margin;
        *end_margin = used_margin;
    } else if (start_auto) {
        *start_margin = layout_non_negative_free_space(
            available_size - border_box_size - *end_margin);
    } else if (end_auto) {
        *end_margin = layout_non_negative_free_space(
            available_size - border_box_size - *start_margin);
    }
}

float adjust_min_max_width(ViewBlock* block, float width);
float adjust_min_max_height(ViewBlock* block, float height);
float adjust_border_padding_width(ViewBlock* block, float width);
float adjust_border_padding_height(ViewBlock* block, float height);

// ============================================================================
// Containing Blocks
// ============================================================================

// tier-3: layout-transient, valid within pass
typedef struct LayoutContainingBlock {
    ViewBlock* view;

    float border_x;
    float border_y;
    float border_width;
    float border_height;

    float padding_x;
    float padding_y;
    float padding_width;
    float padding_height;

    float content_x;
    float content_y;
    float content_width;
    float content_height;

    bool has_definite_width;
    bool has_definite_height;
} LayoutContainingBlock;

bool layout_view_is_abs_or_fixed(ViewBlock* block);
ViewBlock* layout_nearest_block_ancestor(ViewElement* view);
bool layout_is_initial_containing_block(LayoutContext* lycon, ViewBlock* block);

LayoutContainingBlock layout_containing_block_for_view(ViewBlock* block);
LayoutContainingBlock layout_initial_containing_block(LayoutContext* lycon);
LayoutContainingBlock layout_absolute_containing_block(LayoutContext* lycon, ViewBlock* block);

void layout_resolve_percent_size_for_child(LayoutContext* lycon, ViewBlock* child,
    LayoutContainingBlock cb, bool use_content_box, const char* log_context);
void layout_resolve_percent_offsets_for_child(ViewBlock* child,
    LayoutContainingBlock cb, const char* log_context);

// ============================================================================
// CSS Counters
// ============================================================================

typedef struct Arena Arena;
typedef struct DomElement DomElement;

// tier-3: layout-transient, valid within pass
typedef struct CounterValue {
    const char* name;
    int value;
    bool propagated;
    bool created_by_reset;
} CounterValue;

// tier-3: layout-transient, valid within pass
typedef struct CounterScope {
    HashMap* counters;
    CounterScope* parent;
} CounterScope;

// tier-3: layout-transient, valid within pass
typedef struct CounterContext {
    Arena* arena;
    CounterScope* current_scope;
    lam::ArrayList<CounterScope*>* scope_stack;

    bool init(Arena* backing_arena);
    void destroy();
    void push_scope();
    void pop_scope();
    void pop_scope_propagate(bool propagate_resets = false);
} CounterContext;

CounterContext* counter_context_create(Arena* arena);
void counter_context_destroy(CounterContext* ctx);
void counter_push_scope(CounterContext* ctx);
void counter_pop_scope_propagate(CounterContext* ctx, bool propagate_resets = false);
void counter_reset(CounterContext* ctx, const char* counter_spec);
void counter_increment(CounterContext* ctx, const char* counter_spec);
void counter_set(CounterContext* ctx, const char* counter_spec);
int counter_get_value(CounterContext* ctx, const char* name);
void counter_get_all_values(CounterContext* ctx, const char* name, int** values, int* count);
int counter_format_value(int value, uint32_t style, char* buffer, size_t buffer_size);
int counter_format(CounterContext* ctx, const char* name, uint32_t style,
                   char* buffer, size_t buffer_size);
int counters_format(CounterContext* ctx, const char* name, const char* separator,
                    uint32_t style, char* buffer, size_t buffer_size);

// ============================================================================
// Layout Debugging and Profiling
// ============================================================================

namespace radiant {

enum LayoutDebugCategory : uint32_t {
    LAYOUT_DEBUG_NONE  = 0,
    LAYOUT_DEBUG_BOX   = 1u << 0,
    LAYOUT_DEBUG_PASS  = 1u << 1,
    LAYOUT_DEBUG_ABS   = 1u << 2,
    LAYOUT_DEBUG_FLEX  = 1u << 3,
    LAYOUT_DEBUG_GRID  = 1u << 4,
    LAYOUT_DEBUG_TABLE = 1u << 5,
    LAYOUT_DEBUG_TEXT  = 1u << 6,
    LAYOUT_DEBUG_CACHE = 1u << 7,
    LAYOUT_DEBUG_ALL   = 0xffffffffu
};

// tier-3: layout-transient, valid within pass
typedef struct LayoutDebugState {
    uint32_t enabled_categories;
    bool initialized;
} LayoutDebugState;

enum LayoutProfileBucket : uint8_t {
    LAYOUT_PROFILE_BLOCK = 0,
    LAYOUT_PROFILE_INLINE,
    LAYOUT_PROFILE_TEXT,
    LAYOUT_PROFILE_FLEX,
    LAYOUT_PROFILE_GRID,
    LAYOUT_PROFILE_TABLE,
    LAYOUT_PROFILE_INTRINSIC,
    LAYOUT_PROFILE_STYLE,
    LAYOUT_PROFILE_IMAGE,
    LAYOUT_PROFILE_BUCKET_COUNT
};

// tier-3: layout-transient, valid within pass
typedef struct LayoutProfileNode {
    const DomNode* node;
    LayoutProfileBucket bucket;
    double elapsed_ms;
} LayoutProfileNode;

// tier-3: layout-transient, valid within pass
typedef struct LayoutProfiler {
    double block_ms;
    double inline_ms;
    double text_ms;
    double flex_ms;
    double grid_ms;
    double table_ms;
    double intrinsic_ms;
    double style_ms;
    double image_ms;
    int64_t cache_hits;
    int64_t cache_misses;

    bool enabled;
    LayoutProfileNode top_nodes[8];
    int top_node_count;
} LayoutProfiler;

void layout_debug_init(LayoutDebugState* state);
bool layout_debug_enabled(LayoutContext* lycon, LayoutDebugCategory category);
void layout_debug_log(LayoutContext* lycon, LayoutDebugCategory category,
                      const DomNode* node, const char* format, ...);
void layout_debug_vlog(LayoutContext* lycon, LayoutDebugCategory category,
                       const DomNode* node, const char* format, va_list args);

void layout_profiler_init(LayoutProfiler* profiler);
void layout_profiler_set_bucket(LayoutProfiler* profiler, LayoutProfileBucket bucket,
                                double elapsed_ms);
void layout_profiler_add_bucket(LayoutProfiler* profiler, LayoutProfileBucket bucket,
                                double elapsed_ms);
void layout_profiler_record_node(LayoutProfiler* profiler, LayoutProfileBucket bucket,
                                 const DomNode* node, double elapsed_ms);
void layout_profiler_note_cache_hit(LayoutProfiler* profiler);
void layout_profiler_note_cache_miss(LayoutProfiler* profiler);
void layout_profiler_set_cache(LayoutProfiler* profiler, int64_t hits, int64_t misses);
void layout_profiler_report(LayoutContext* lycon);
double layout_profiler_now_ms();

// tier-3: layout-transient, valid within pass
struct LayoutProfileScope {
    LayoutContext* lycon;
    const DomNode* node;
    LayoutProfileBucket bucket;
    double start_ms;

    LayoutProfileScope(LayoutContext* l, LayoutProfileBucket b, const DomNode* n);
    ~LayoutProfileScope();

    LayoutProfileScope(const LayoutProfileScope&) = delete;
    LayoutProfileScope& operator=(const LayoutProfileScope&) = delete;
};

} // namespace radiant

// ============================================================================
// Layout Alignment
// ============================================================================

namespace radiant {

// tier-3: layout-transient, valid within pass
struct SpaceDistribution {
    float gap_before_first;
    float gap_between;
    float gap_after_last;
};

float compute_alignment_offset(
    int32_t alignment,
    float free_space,
    bool is_safe
);

inline float compute_alignment_offset_simple(int32_t alignment, float free_space) {
    return compute_alignment_offset(alignment, free_space, false);
}

SpaceDistribution compute_space_distribution(
    int32_t alignment,
    float free_space,
    int32_t item_count
);

int32_t alignment_fallback_for_overflow(int32_t alignment, float free_space);
bool alignment_is_space_distribution(int32_t alignment);
bool alignment_is_baseline(int32_t alignment);
bool alignment_is_stretch(int32_t alignment);
int32_t resolve_align_self(int32_t align_self, int32_t align_items);
int32_t resolve_justify_self(int32_t justify_self, int32_t justify_items);

float compute_font_baseline_ascender(
    ::LayoutContext* lycon,
    FontProp* font,
    bool use_normal_line_height,
    float fallback_ascender
);

float compute_element_first_baseline(
    ::LayoutContext* lycon,
    ViewBlock* element,
    bool is_row_direction
);

typedef float (*FirstBaselineRowCallback)(::LayoutContext* lycon, View* row);

float compute_view_first_text_baseline(
    ::LayoutContext* lycon,
    View* parent,
    float cumulative_y,
    bool use_normal_line_height,
    bool skip_block_children_of_table,
    FirstBaselineRowCallback row_baseline
);

} // namespace radiant

// ============================================================================
// Text Layout Utilities
// ============================================================================

CssEnum get_white_space_value(DomNode* node);
bool text_codepoint_has_zero_advance(uint32_t codepoint);

// ============================================================================
// Table Metadata
// ============================================================================

// tier-3: layout-transient, valid within pass
struct TableMetadata {
    int column_count;
    int row_count;
    bool* grid_occupied;
    float* col_widths;
    float* col_single_min_widths;
    float* col_min_widths;
    float* col_max_widths;
    float* col_percent_widths;
    float* row_heights;
    float* row_y_positions;
    bool* row_collapsed;
    bool* col_collapsed;
    float* col_original_widths;
    bool* row_has_percent_height;
    float* col_edge_max_border;
    bool* col_has_explicit_width;

    float collapsed_border_top;
    float collapsed_border_right;
    float collapsed_border_bottom;
    float collapsed_border_left;

    ScratchArena* sa;

    TableMetadata(ScratchArena* scratch, int cols, int rows);
    ~TableMetadata();

    inline bool& grid(int row, int col) {
        return grid_occupied[row * column_count + col];
    }
};

TableMetadata* table_metadata_create(ScratchArena* scratch, int cols, int rows);
void table_metadata_destroy(TableMetadata* meta);

// ============================================================================
// Custom Layout
// ============================================================================

// tier-3: layout-transient, valid within pass
typedef struct VelmtBox {
    float x;
    float y;
    float width;
    float height;
} VelmtBox;

// tier-3: layout-transient, valid within pass
typedef struct VelmtEdges {
    float left;
    float right;
    float top;
    float bottom;
} VelmtEdges;

// tier-3: layout-transient, valid within pass
typedef struct Velmt {
    View* view;
    DomElement* element;
    int index;
    VelmtBox border_box;
    VelmtEdges margin;
    VelmtEdges border;
    VelmtEdges padding;
} Velmt;

// tier-3: layout-transient, valid within pass
typedef struct CustomLayoutContext {
    LayoutContext* lycon;
    ViewBlock* parent;
    const char* layout_name;
    Velmt* children;
    int child_count;
    float available_width;
    float available_height;
    float css_width;
    float css_height;
    float child_available_width;
    float child_available_height;
    bool child_available_width_definite;
    bool child_available_height_definite;
    const char* child_available_width_source;
    const char* child_available_height_source;
    CssEnum direction;
    const char* writing_mode;
} CustomLayoutContext;

// tier-3: layout-transient, valid within pass
typedef struct CustomLayoutPlacement {
    int child_index;
    float x;
    float y;
    int z;
    bool has_z;
} CustomLayoutPlacement;

// tier-3: layout-transient, valid within pass
typedef struct CustomLayoutPaintLayer {
    Element* content;
    int z;
    int order;
} CustomLayoutPaintLayer;

// tier-3: layout-transient, valid within pass
typedef struct CustomLayoutPaintState {
    CustomLayoutPaintLayer* layers;
    int layer_count;
} CustomLayoutPaintState;

// tier-3: layout-transient, valid within pass
typedef struct CustomLayoutResult {
    CustomLayoutPlacement* placements;
    int placement_count;
    int placement_capacity;
    float width;
    float height;
    float baseline;
    bool has_width;
    bool has_height;
    bool has_baseline;
} CustomLayoutResult;

typedef bool (*CustomLayoutFn)(const CustomLayoutContext* context, CustomLayoutResult* result);

bool custom_layout_register(const char* name, CustomLayoutFn fn);
CustomLayoutFn custom_layout_lookup(const char* name);
void custom_layout_registry_clear(void);
bool custom_layout_result_place(CustomLayoutResult* result, int child_index, float x, float y);
void custom_layout_fill_velmt_from_view(Velmt* velmt, View* child, int index, bool normalize_origin);

const char* custom_layout_name_from_css_value(const CssValue* value);
const char* custom_layout_name_for_element(DomElement* element);
bool layout_custom_apply(LayoutContext* lycon, ViewBlock* block, const char* layout_name);

// ============================================================================
// List Layout and Counters
// ============================================================================

typedef struct StyleTree StyleTree;

void setup_list_container_counters(LayoutContext* lycon, ViewBlock* block, DomElement* dom_elem);
void compute_reversed_counter_initial(LayoutContext* lycon, DomElement* dom_elem);
void process_list_item(LayoutContext* lycon, ViewBlock* block, DomNode* elmt,
                       DomElement* dom_elem, DisplayValue display);
const char* extract_counter_spec_from_style(StyleTree* style, CssPropertyId css_property,
                                            LayoutContext* lycon);
void apply_pseudo_counter_ops(LayoutContext* lycon, StyleTree* style);

// ============================================================================
// Multi-column Layout
// ============================================================================

bool is_multicol_container(ViewBlock* block);
float multicol_normal_gap_size(ViewBlock* block);
void calculate_multicol_dimensions(
    MultiColumnProp* multicol,
    float available_width,
    float normal_gap_size,
    int* out_column_count,
    float* out_column_width,
    float* out_gap
);
void layout_multicol_content(LayoutContext* lycon, ViewBlock* block);

// tier-3: layout-transient, valid within pass
typedef struct StyleContext {
    struct StyleElement* parent;
    struct StyleNode* prev_node;
    // lxb_css_parser_t *css_parser;  // Removed: lexbor dependency
    void *css_parser;  // Placeholder for future CSS parser if needed
} StyleContext;

/**
 * FloatBox - Represents a positioned floating element
 * Tracks both the element position and its margin box for proper space calculations.
 */
// tier-3: layout-transient, valid within pass
typedef struct FloatBox {
    ViewBlock* element;         // The floating element

    // Margin box bounds (outer bounds including margins)
    float margin_box_top;
    float margin_box_bottom;
    float margin_box_left;
    float margin_box_right;

    // Border box bounds (element position and size)
    float x, y, width, height;

    CssEnum float_side;         // CSS_VALUE_LEFT or CSS_VALUE_RIGHT
    struct FloatBox* next;      // Linked list for multiple floats
} FloatBox;

/**
 * FloatAvailableSpace - Result of space query at a given Y coordinate
 */
// tier-3: layout-transient, valid within pass
typedef struct FloatAvailableSpace {
    float left;                 // Left edge of available space
    float right;                // Right edge of available space
    bool has_left_float;        // True if a left float intrudes at this Y
    bool has_right_float;       // True if a right float intrudes at this Y
} FloatAvailableSpace;

/**
 * BlockContext - Unified Block Formatting Context
 *
 * Combines the functionality of:
 * - BlockContext (layout state)
 * - FloatContext (legacy float management)
 * - BlockFormattingContext (new BFC system)
 *
 * Per CSS 2.2 Section 9.4.1, a BFC is established by:
 * - Root element
 * - Floats (float != none)
 * - Absolutely positioned elements
 * - Inline-blocks
 * - Table cells/captions
 * - Overflow != visible
 * - display: flow-root
 * - Flex/Grid items
 */
// tier-3: layout-transient, valid within pass
typedef struct BlockContext {
    // =========================================================================
    // Layout State (from BlockContext)
    // =========================================================================
    float content_width;        // Computed content width for inner content
    float content_height;       // Computed content height for inner content
    float advance_y;            // Current vertical position (includes padding.top + border.top)
    float max_width;            // Maximum content width encountered
    float max_height;           // Maximum content height encountered
    float line_height;          // Current line height
    bool  line_height_is_normal; // true when line-height is 'normal', false when explicitly set
    float init_ascender;        // Initial ascender at line start
    float init_descender;       // Initial descender at line start
    float lead_y;               // Leading space when line_height > font size
    CssEnum text_align;         // Text alignment
    CssEnum text_align_last;    // text-align-last (CSS Text 3 §7.2): overrides text_align on last line
    CssEnum direction;          // CSS_VALUE_LTR or CSS_VALUE_RTL (CSS 2.1 §9.2.1)
    FontProp* block_container_font; // CSS Text 3 §4.2: block container's font for tab-size calculation
    float given_width;          // CSS specified width (-1 if auto)
    float given_height;         // CSS specified height (-1 if auto)
    float first_line_ascender;  // Baseline of first line (distance from border-box top, for flex baseline)
    float last_line_ascender;   // Baseline of last line (for inline-block baseline alignment)

    // CSS Inline 3 §5: line box metrics for text-box-trim calculation.
    // These store the max ascender/descender of the first and last line boxes,
    // capturing inline descendants' contributions to line box extent.
    float first_line_max_ascender;
    float first_line_max_descender;
    float last_line_max_ascender;
    float last_line_max_descender;

    // CSS text-indent: applies only to the first line of a block container
    float text_indent;          // Resolved text-indent value in pixels
    bool is_first_line;         // True if we're laying out the first line of this block

    // -webkit-line-clamp support
    int line_number;            // Current line number (1-based, incremented by line_break)
    int line_clamp;             // Max visible lines (0 = no clamp, from BlockProp)
    bool line_clamped;          // True after line_clamp lines have been laid out
    float line_clamp_advance_y; // advance_y at the clamp boundary
    float line_clamp_last_line_ascender;
    float line_clamp_last_line_max_ascender;
    float line_clamp_last_line_max_descender;
    CssEnum text_wrap_style;    // CSS Text 4 text-wrap-style
    bool balance_wrap_active;   // true when wrapping against balance_wrap_width
    float balance_wrap_width;   // inline measure used for text-wrap-style: balance

    // =========================================================================
    // BFC Hierarchy
    // =========================================================================
    struct BlockContext* parent;           // Parent block context
    ViewBlock* establishing_element;       // Element that established this BFC (if any)
    bool is_bfc_root;                      // True if this context establishes a new BFC

    // BFC coordinate origin (absolute position of content area top-left)
    float origin_x;
    float origin_y;

    // Offset from BFC origin to this block's border-box origin
    // Used to convert between BFC coordinates and local coordinates
    // Calculated once when entering a block, avoids repeated parent-chain walks
    float bfc_offset_x;
    float bfc_offset_y;

    // =========================================================================
    // Float Management (unified from FloatContext + BlockFormattingContext)
    // =========================================================================
    FloatBox* left_floats;      // Linked list of left floats (head)
    FloatBox* left_floats_tail; // Tail for O(1) append
    FloatBox* right_floats;     // Linked list of right floats (head)
    FloatBox* right_floats_tail;// Tail for O(1) append
    int left_float_count;
    int right_float_count;
    float lowest_float_bottom;  // Optimization: track lowest float edge

    // Content area bounds (for float calculations)
    float float_left_edge;      // Left edge of content area (usually 0)
    float float_right_edge;     // Right edge of content area

    // CSS 2.1 §9.5.2: Saved clear_y for deferred clearance computation.
    // Set in layout_block_content clear check, read in layout_block margin collapsing.
    // -1 = no clearance applied, >= 0 = clearance was applied (skip margin collapse).
    float saved_clear_y;

    // =========================================================================
    // Memory
    // =========================================================================
    Pool* pool;                 // Memory pool for float allocations
} BlockContext;

// Semantic break kind classification (CSS Text 3 §4–5 + UAX #14)
// Used to track the type of the last recorded break opportunity in a line box.
typedef enum BreakKind {
    // Content (not a break character itself)
    BRK_TEXT = 0,               // ordinary word/grapheme content

    // Whitespace kinds (CSS Text 3 §4)
    BRK_SPACE,                  // collapsible space (U+0020 in white-space: normal/nowrap/pre-line)
    BRK_PRESERVED_SPACE,        // non-collapsible space (U+0020 in pre/pre-wrap/break-spaces)
    BRK_TAB,                    // tab character (advance to next tab stop; CSS Text 3 §4.2)
    BRK_HARD_BREAK,             // newline (\n in pre/pre-wrap/pre-line; CSS Text 3 §4.1)

    // Non-breaking / glue (UAX #14 GL, WJ, ZWJ)
    BRK_GLUE,                   // visible non-breaking: NBSP U+00A0, NNBSP U+202F
    BRK_GLUE_ZW,                // zero-width non-breaking: WJ U+2060, ZWNBSP U+FEFF
    BRK_ZWJ,                    // zero-width joiner U+200D (suppresses break, joins emoji sequences)

    // Break opportunities (CSS Text 3 §5)
    BRK_ZERO_WIDTH_BREAK,       // ZWSP U+200B (invisible, breakable)
    BRK_SOFT_HYPHEN,            // SHY U+00AD (invisible unless broken, then visible '-')
    BRK_HYPHEN,                 // explicit hyphen U+002D, U+2010 (break after, includes width)

    // UAX #14 line break classes (CSS Text 3 §5.2)
    BRK_CJK,                    // CJK ideograph (break after, unless word-break: keep-all)
    BRK_OP,                     // opening punctuation — no break after (UAX #14 LB14)
    BRK_CL,                     // closing punctuation — no break before (UAX #14 LB15/16)
    BRK_NS,                     // non-starter — no break before when after CJK (UAX #14 LB20)
    BRK_EX_IS_SY,               // EX/IS/SY — no break before (UAX #14 LB13)
    BRK_CJ,                     // conditional Japanese starter (resolved to NS or ID per line-break mode)

    // Ideographic space
    BRK_IDEOGRAPHIC_SPACE,      // U+3000 (full-width space, hangable, break opportunity)
} BreakKind;

// tier-3: layout-transient, valid within pass
typedef struct Linebox {
    float left, right;                // left and right bounds of the line
    float align_left, align_right;    // original alignment bounds when wrap measure differs
    float effective_left;             // float-adjusted left bound
    float effective_right;            // float-adjusted right bound
    float advance_x;
    float max_ascender;
    float max_descender;
    float max_css_baseline_ascender; // font-table baseline for replaced inline alignment
    unsigned char* last_space;      // last space character in the line
    float last_space_pos;             // position of the last space in the line
    BreakKind last_space_kind;        // semantic type of the last recorded break opportunity
    unsigned char* last_non_shy_space; // previous non-SHY break opportunity before a soft hyphen
    float last_non_shy_space_pos;
    BreakKind last_non_shy_space_kind;
    float last_non_shy_space_hanging_width;
    float last_non_shy_space_hanging_text_trim;
    View* start_view;
    bool has_phantom_inline_fragment; // zero-height inline run still needing text-align
    CssEnum vertical_align;
    float vertical_align_offset;    // length/percentage vertical-align offset (px), positive = raise
    bool is_line_start;
    bool has_space;                 // whether last layout character is a space
    bool has_float_intrusion;       // true if floats affect this line
    bool has_replaced_content;      // true if line has inline replaced elements (images, inline-blocks)
    float max_desc_before_last_text; // max_descender value before last output_text (for trailing space rollback)
    bool has_expanded_inline_lh;    // true if an inline element's own line-height exceeds the parent block's
    float max_inline_line_height;   // max explicit line-height from baseline-aligned inline descendants
    float max_atomic_inline_height; // max margin-box height from inline-block/replaced descendants
    bool has_inline_spans;          // true if line contains inline span elements (for bbox correction)
    bool has_different_inline_font; // true if any inline text uses a different font from the block's strut
    float max_normal_line_height;   // max normal line-height across all inline boxes on this line
    bool has_c1_control_text;       // true when line contains visible C1 control glyphs
    bool has_non_c1_text;           // true when line contains visible non-C1 text glyphs
    bool has_direct_block_text;     // non-collapsed text in the block's anonymous inline box
    float c1_control_line_height;   // browser-sized C1 control glyph line strut
    // CSS 2.1 §10.8.1: parent font metrics for vertical-align keywords (text-top, text-bottom, etc.)
    // Set by span_vertical_align before recursing into children; defaults to block init values.
    float parent_font_ascender;     // parent element's font ascender (pixels)
    float parent_font_descender;    // parent element's font descender (pixels)
    float parent_font_size;         // parent element's font size (pixels)
    struct FontHandle* parent_font_handle; // parent element's font handle (for x-height)
    TextRect* last_text_rect;       // last text rect output on this line (for trailing space trimming)
    struct ViewText* last_text_view; // ViewText that owns last_text_rect (for bounds update after trimming)
    float trailing_space_width;     // width of trailing space in last text rect (CSS 2.1 §16.6.1)
    TextRect* committed_trailing_rect;  // text rect that had trailing space when output_text was called
    struct ViewText* committed_trailing_view;  // ViewText that owns committed_trailing_rect
    float committed_trailing_space;     // trailing space width saved at output_text time; survives
                                        // cross-node char processing so line_break can trim correctly
    float hanging_space_width;      // CSS Text 3 §4.1.3: accumulated trailing preserved space width
                                    // for pre-wrap mode; used to compute hanging space at wrap points
    float hanging_space_text_trim;  // portion of hanging_space_width from regular ASCII spaces only;
                                    // used to trim text rects (U+3000 has visible glyph, not trimmed)
    float last_space_hanging_width;  // hanging_space_width saved at the time last_space was recorded
    float last_space_hanging_text_trim; // hanging_space_text_trim saved at the time last_space was recorded
    float rtl_hanging_space;            // CSS Text 3 §4.1.3: hanging space width saved for RTL alignment adjust;
                                        // in RTL, trailing space hangs past the inline-end (left edge), so
                                        // after alignment, the last text rect's x must be shifted left by this
    bool wrap_opportunity_before_nowrap;  // CSS Text 3 §5: a wrappable break opportunity exists at the current
                                         // position (from collapsed inter-element whitespace in a wrappable
                                         // parent); allows nowrap content to break at this boundary
    bool is_last_line;              // CSS 2.1 §16.2: true when this is the last line of a block (for justify)
    float inline_start_edge_pending;  // CSS 2.1 §8.3: accumulated left margin+border+padding from
                                      // inline spans that haven't produced content yet; re-applied
                                      // after line break so the span's first content is indented
    float text_indent_offset;         // CSS 2.1 §16.1: RTL first-line text-indent amount that
                                      // narrows the wrap boundary and alignment width from the right
    FontBox line_start_font;
    uint32_t prev_glyph_index = 0;   // for kerning
    uint32_t prev_codepoint = 0;     // for CoreText GPOS kerning (codepoint-based)
    struct FontHandle* prev_kerning_font_handle = nullptr;
    bool has_cjk_text = false;       // true if line contains CJK characters (for line-height blending)
    float max_top_bottom_height = 0; // CSS 2.1 §10.8.1: max height of vertical-align:top/bottom elements
                                     // (used in second pass to expand line box if needed)
    float max_top_height = 0;        // CSS 2.1 §10.8.1: max height of vertical-align:top elements
    float max_bottom_height = 0;     // CSS 2.1 §10.8.1: max height of vertical-align:bottom elements
    float trailing_letter_spacing;   // CSS Text 3 §8: letter-spacing after the last character on a line;
                                     // trimmed at line breaks since letter-spacing is not applied at line ends

    inline void reset_space() {
        is_line_start = false;  has_space = false;  last_space = NULL;  last_space_pos = 0;
        last_space_kind = BRK_TEXT;  last_space_hanging_width = 0;
        last_non_shy_space = NULL;  last_non_shy_space_pos = 0;
        last_non_shy_space_kind = BRK_TEXT;  last_non_shy_space_hanging_width = 0;
        last_non_shy_space_hanging_text_trim = 0;
        trailing_space_width = 0;
        committed_trailing_rect = NULL;
        committed_trailing_view = NULL;
        committed_trailing_space = 0;
    }
} Linebox;

typedef enum LineFillStatus {
    RDT_NOT_SURE = 0,
    RDT_LINE_NOT_FILLED = 1,
    RDT_LINE_FILLED = 2,
} LineFillStatus;

// Stacking context for absolute/fixed positioned elements
// typedef struct StackingBox : BlockContext {
//     ViewBlock* establishing_element;  // element that creates the context
//     int z_index;                     // z-index of this context
//     struct StackingBox* parent;       // parent stacking context
//     ArrayList* positioned_children; // list of positioned child elements
// } StackingBox;

// ============================================================================
// Flex Layout
// ============================================================================

typedef enum {
    DIR_ROW = CSS_VALUE_ROW,
    DIR_ROW_REVERSE = CSS_VALUE_ROW_REVERSE,
    DIR_COLUMN = CSS_VALUE_COLUMN,
    DIR_COLUMN_REVERSE = CSS_VALUE_COLUMN_REVERSE
} FlexDirection;

typedef enum {
    WRAP_NOWRAP = CSS_VALUE_NOWRAP,
    WRAP_WRAP = CSS_VALUE_WRAP,
    WRAP_WRAP_REVERSE = CSS_VALUE_WRAP_REVERSE
} FlexWrap;

typedef enum {
    JUSTIFY_START = CSS_VALUE_FLEX_START,
    JUSTIFY_END = CSS_VALUE_FLEX_END,
    JUSTIFY_CENTER = CSS_VALUE_CENTER,
    JUSTIFY_SPACE_BETWEEN = CSS_VALUE_SPACE_BETWEEN,
    JUSTIFY_SPACE_AROUND = CSS_VALUE_SPACE_AROUND,
    JUSTIFY_SPACE_EVENLY = CSS_VALUE_SPACE_EVENLY
} JustifyContent;

// tier-3: layout-transient, valid within pass
typedef struct FlexLineInfo {
    View** items;
    int item_count;
    float main_size;
    float cross_size;
    float cross_position;
    float free_space;
    float total_flex_grow;
    float total_flex_shrink;
    float baseline;
} FlexLineInfo;

// tier-3: layout-transient, valid within pass
typedef struct FlexContainerLayout : FlexProp {
    // Layout state (computed during layout)
    View** flex_items;  // Array of child flex items
    int item_count;
    int allocated_items;  // For dynamic array growth

    // Line information
    struct FlexLineInfo* lines;
    int line_count;
    int allocated_lines;

    // Cached calculations
    float main_axis_size;
    float cross_axis_size;
    bool needs_reflow;

    // Sizing mode flags (CSS Flexbox spec §9.2)
    // When true, the axis size is indefinite (fit-content/shrink-to-fit)
    // and flex-grow should NOT distribute additional space
    bool main_axis_is_indefinite;

    // CSS Flexbox §9.4: Whether container has a definite cross size
    // True if container has explicit CSS height (row flex) or width (column flex)
    // False for auto-size containers that derive cross size from content
    bool has_definite_cross_size;

    // Layout context for intrinsic sizing (set during init_flex_container)
    struct LayoutContext* lycon;

    // pass-local flex state lives above this mark and is released together.
    ScratchMark scratch_mark;
} FlexContainerLayout;

// ============================================================================
// Layout Axis Helpers
// ============================================================================

typedef enum LayoutAxis {
    LAYOUT_AXIS_X,
    LAYOUT_AXIS_Y,
} LayoutAxis;

inline float layout_axis_size(ViewElement* item, LayoutAxis axis) {
    if (!item) return 0.0f;
    return axis == LAYOUT_AXIS_X ? item->width : item->height;
}

inline void layout_axis_set_size(ViewElement* item, LayoutAxis axis, float size) {
    if (!item) return;
    if (axis == LAYOUT_AXIS_X) {
        item->width = size;
    } else {
        item->height = size;
    }
}

inline float layout_axis_pos(ViewElement* item, LayoutAxis axis) {
    if (!item) return 0.0f;
    return axis == LAYOUT_AXIS_X ? item->x : item->y;
}

inline void layout_axis_set_pos(ViewElement* item, LayoutAxis axis, float pos) {
    if (!item) return;
    if (axis == LAYOUT_AXIS_X) {
        item->x = pos;
    } else {
        item->y = pos;
    }
}

inline bool layout_axis_is_horizontal(LayoutAxis axis) {
    return axis == LAYOUT_AXIS_X;
}

inline float layout_axis_given_size(const BlockProp* block, LayoutAxis axis) {
    if (!block) return -1.0f;
    return axis == LAYOUT_AXIS_X ? block->given_width : block->given_height;
}

inline float layout_axis_given_max_size(const BlockProp* block, LayoutAxis axis) {
    if (!block) return -1.0f;
    return axis == LAYOUT_AXIS_X ? block->given_max_width : block->given_max_height;
}

inline float layout_axis_spacing_start(const Spacing* spacing, LayoutAxis axis) {
    if (!spacing) return 0.0f;
    return axis == LAYOUT_AXIS_X ? spacing->left : spacing->top;
}

inline float layout_axis_spacing_end(const Spacing* spacing, LayoutAxis axis) {
    if (!spacing) return 0.0f;
    return axis == LAYOUT_AXIS_X ? spacing->right : spacing->bottom;
}

inline CssEnum layout_axis_margin_start_type(const Margin* margin, LayoutAxis axis) {
    if (!margin) return CSS_VALUE__UNDEF;
    return axis == LAYOUT_AXIS_X ? margin->left_type : margin->top_type;
}

inline CssEnum layout_axis_margin_end_type(const Margin* margin, LayoutAxis axis) {
    if (!margin) return CSS_VALUE__UNDEF;
    return axis == LAYOUT_AXIS_X ? margin->right_type : margin->bottom_type;
}

inline float layout_axis_border_start(const BorderProp* border, LayoutAxis axis) {
    return border ? layout_axis_spacing_start(&border->width, axis) : 0.0f;
}

inline float layout_axis_padding_start(const BoundaryProp* bound, LayoutAxis axis) {
    return bound ? layout_axis_spacing_start(&bound->padding, axis) : 0.0f;
}

inline float layout_axis_margin_start(const BoundaryProp* bound, LayoutAxis axis) {
    return bound ? layout_axis_spacing_start(&bound->margin, axis) : 0.0f;
}

inline float layout_axis_margin_end(const BoundaryProp* bound, LayoutAxis axis) {
    return bound ? layout_axis_spacing_end(&bound->margin, axis) : 0.0f;
}

inline LayoutAxis flex_main_axis_from_props(const FlexProp* flex) {
    if (!flex) return LAYOUT_AXIS_X;
    bool column_direction = flex->direction == CSS_VALUE_COLUMN ||
                            flex->direction == CSS_VALUE_COLUMN_REVERSE;
    if (flex->writing_mode == WM_VERTICAL_RL ||
        flex->writing_mode == WM_VERTICAL_LR) {
        return column_direction ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    }
    return column_direction ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
}

inline LayoutAxis flex_main_axis(FlexContainerLayout* flex) {
    return flex_main_axis_from_props(flex);
}

inline LayoutAxis flex_cross_axis(FlexContainerLayout* flex) {
    return flex_main_axis(flex) == LAYOUT_AXIS_X ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
}

inline float flex_gap_for_axis(FlexContainerLayout* flex, LayoutAxis axis) {
    if (!flex) return 0.0f;
    return axis == LAYOUT_AXIS_X ? flex->column_gap : flex->row_gap;
}

void init_flex_container(LayoutContext* lycon, ViewBlock* container);
void cleanup_flex_container(LayoutContext* lycon);

// Mirrors BlockContextScope: pass-local flex scratch and parent context must
// unwind together, even when nested layout exits early.
// tier-3: layout-transient, valid within pass
struct FlexLayoutScope {
    LayoutContext* lycon;
    FlexContainerLayout* saved;
    bool active;

    FlexLayoutScope(LayoutContext* l, ViewBlock* container);
    ~FlexLayoutScope();
    void close();

    FlexLayoutScope(const FlexLayoutScope&) = delete;
    FlexLayoutScope& operator=(const FlexLayoutScope&) = delete;
};

int collect_and_prepare_flex_items(LayoutContext* lycon, FlexContainerLayout* flex_layout, ViewBlock* container);
float calculate_flex_basis(ViewElement* item, FlexContainerLayout* flex_layout);
void resolve_flex_item_constraints(ViewElement* item, FlexContainerLayout* flex_layout);
void apply_constraints_to_flex_items(FlexContainerLayout* flex_layout);
float apply_flex_constraint(ViewElement* item, float computed_size, bool is_main_axis,
                          FlexContainerLayout* flex_layout, bool* hit_min, bool* hit_max);
float apply_flex_constraint(ViewElement* item, float computed_size, bool is_main_axis,
                          FlexContainerLayout* flex_layout);
float apply_stretch_constraint(ViewElement* item, float container_cross_size,
                             FlexContainerLayout* flex_layout);
void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line);
void align_items_cross_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line);
void align_content(FlexContainerLayout* flex_layout);
void reposition_baseline_items(LayoutContext* lycon, ViewBlock* flex_container);
bool is_main_axis_horizontal(FlexProp* flex);
float get_main_axis_size(ViewElement* item, FlexContainerLayout* flex_layout);
float get_cross_axis_size(ViewElement* item, FlexContainerLayout* flex_layout);
float get_cross_axis_position(ViewElement* item, FlexContainerLayout* flex_layout);
void set_main_axis_position(ViewElement* item, float position, FlexContainerLayout* flex_layout);
void set_cross_axis_position(ViewElement* item, float position, FlexContainerLayout* flex_layout);
void set_main_axis_size(ViewElement* item, float size, FlexContainerLayout* flex_layout);
void set_cross_axis_size(ViewElement* item, float size, FlexContainerLayout* flex_layout);
float get_item_flex_grow(ViewElement* item);
float get_item_flex_shrink(ViewElement* item);
float find_max_baseline(FlexLineInfo* line, int container_align_items);
float calculate_gap_space(FlexContainerLayout* flex_layout, int item_count, bool is_main_axis);

// tier-3: layout-transient, valid within pass
typedef struct MeasurementCacheEntry {
    DomNode* node;
    float measured_width;
    float measured_height;
    float content_width;
    float content_height;
    float context_width;
    uint32_t generation;
} MeasurementCacheEntry;

void measure_flex_child_content(LayoutContext* lycon, DomNode* child);
void measure_text_content(LayoutContext* lycon, DomNode* text_node, int* width, int* height);
void calculate_intrinsic_sizes(ViewBlock* view, LayoutContext* lycon);
void calculate_item_intrinsic_sizes(ViewElement* item, FlexContainerLayout* flex_layout);
void measure_text_content_accurate(LayoutContext* lycon, DomNode* text_node,
    int* min_width, int* max_width, int* height);
void measure_text_run(LayoutContext* lycon, const char* text, size_t length,
    int* min_width, int* max_width, int* height);
void store_measured_sizes(DomNode* node, ViewBlock* measured_view, LayoutContext* lycon);
void store_in_measurement_cache(DomNode* node, float width, float height, float content_width, float content_height, float context_width = -1);
MeasurementCacheEntry* get_from_measurement_cache(DomNode* node);
void clear_measurement_cache(ViewTree* tree);
void destroy_measurement_cache(ViewTree* tree);
void invalidate_measurement_cache_for_node(DomNode* node);
void advance_measurement_cache_generation(ViewTree* tree);
void init_flex_item_view(LayoutContext* lycon, DomNode* node);
void layout_block_with_measured_size(LayoutContext* lycon, DomNode* node,
    DisplayValue display, MeasurementCacheEntry* cached);

void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container);
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container);
void layout_flex_item_final_content(LayoutContext* lycon, ViewBlock* flex_item);

// ============================================================================
// Grid Layout
// ============================================================================

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace radiant {
namespace grid {

struct TrackCounts;

template <typename T>
constexpr T grid_min_value(T a, T b) {
    return (a < b) ? a : b;
}

template <typename T>
constexpr T grid_max_value(T a, T b) {
    return (a > b) ? a : b;
}

enum class CellOccupancyState : uint8_t {
    Unoccupied = 0,
    DefinitelyPlaced = 1,
    AutoPlaced = 2
};

// tier-3: layout-transient, valid within pass
struct OriginZeroLine {
    int16_t value;

    constexpr OriginZeroLine() : value(0) {}
    constexpr explicit OriginZeroLine(int16_t v) : value(v) {}

    constexpr OriginZeroLine operator+(OriginZeroLine other) const {
        return OriginZeroLine(value + other.value);
    }
    constexpr OriginZeroLine operator-(OriginZeroLine other) const {
        return OriginZeroLine(value - other.value);
    }
    constexpr OriginZeroLine operator+(uint16_t n) const {
        return OriginZeroLine(value + static_cast<int16_t>(n));
    }
    constexpr OriginZeroLine operator-(uint16_t n) const {
        return OriginZeroLine(value - static_cast<int16_t>(n));
    }
    OriginZeroLine& operator+=(uint16_t n) {
        value += static_cast<int16_t>(n);
        return *this;
    }

    constexpr bool operator==(OriginZeroLine other) const { return value == other.value; }
    constexpr bool operator!=(OriginZeroLine other) const { return value != other.value; }
    constexpr bool operator<(OriginZeroLine other) const { return value < other.value; }
    constexpr bool operator<=(OriginZeroLine other) const { return value <= other.value; }
    constexpr bool operator>(OriginZeroLine other) const { return value > other.value; }
    constexpr bool operator>=(OriginZeroLine other) const { return value >= other.value; }
};

// tier-3: layout-transient, valid within pass
struct GridLine {
    int16_t value;

    constexpr GridLine() : value(0) {}
    constexpr explicit GridLine(int16_t v) : value(v) {}

    constexpr bool is_valid() const { return value != 0; }
};

// tier-3: layout-transient, valid within pass
struct LineSpan {
    OriginZeroLine start;
    OriginZeroLine end;

    constexpr LineSpan() : start(), end() {}
    constexpr LineSpan(OriginZeroLine s, OriginZeroLine e) : start(s), end(e) {}

    constexpr uint16_t span() const {
        int16_t diff = end.value - start.value;
        return diff > 0 ? static_cast<uint16_t>(diff) : 0;
    }
};

// tier-3: layout-transient, valid within pass
struct TrackCounts {
    uint16_t negative_implicit;
    uint16_t explicit_count;
    uint16_t positive_implicit;

    constexpr TrackCounts() : negative_implicit(0), explicit_count(0), positive_implicit(0) {}
    constexpr TrackCounts(uint16_t neg, uint16_t exp, uint16_t pos)
        : negative_implicit(neg), explicit_count(exp), positive_implicit(pos) {}

    constexpr size_t len() const {
        return static_cast<size_t>(negative_implicit + explicit_count + positive_implicit);
    }

    constexpr OriginZeroLine implicit_start_line() const {
        return OriginZeroLine(-static_cast<int16_t>(negative_implicit));
    }

    constexpr OriginZeroLine implicit_end_line() const {
        return OriginZeroLine(static_cast<int16_t>(explicit_count + positive_implicit));
    }

    constexpr int16_t oz_line_to_next_track(OriginZeroLine line) const {
        return line.value + static_cast<int16_t>(negative_implicit);
    }

    constexpr void oz_line_range_to_track_range(
        LineSpan span,
        int16_t& out_start,
        int16_t& out_end
    ) const {
        out_start = oz_line_to_next_track(span.start);
        out_end = oz_line_to_next_track(span.end);
    }

    constexpr OriginZeroLine track_to_prev_oz_line(uint16_t track_idx) const {
        return OriginZeroLine(static_cast<int16_t>(track_idx) - static_cast<int16_t>(negative_implicit));
    }
};

enum class AbsoluteAxis : uint8_t {
    Horizontal = 0,
    Vertical = 1
};

constexpr AbsoluteAxis other_axis(AbsoluteAxis axis) {
    return axis == AbsoluteAxis::Horizontal ? AbsoluteAxis::Vertical : AbsoluteAxis::Horizontal;
}

} // namespace grid
} // namespace radiant

inline GridItemProp* grid_item_prop(ViewBlock* item) {
    return item ? item->grid_item() : nullptr;
}

typedef enum GridTrackSizeType {
    GRID_TRACK_SIZE_LENGTH,
    GRID_TRACK_SIZE_PERCENTAGE,
    GRID_TRACK_SIZE_FR,
    GRID_TRACK_SIZE_MIN_CONTENT,
    GRID_TRACK_SIZE_MAX_CONTENT,
    GRID_TRACK_SIZE_AUTO,
    GRID_TRACK_SIZE_FIT_CONTENT,
    GRID_TRACK_SIZE_MINMAX,
    GRID_TRACK_SIZE_REPEAT
} GridTrackSizeType;

// tier-3: layout-transient, valid within pass
typedef struct GridTrackSize {
    GridTrackSizeType type;
    int value;
    bool is_percentage;
    struct GridTrackSize* min_size;
    struct GridTrackSize* max_size;
    int fit_content_limit;
    int repeat_count;
    struct GridTrackSize** repeat_tracks;
    int repeat_track_count;
    bool is_auto_fill;
    bool is_auto_fit;
} GridTrackSize;

// tier-3: layout-transient, valid within pass
typedef struct GridTrackList {
    GridTrackSize** tracks;
    int track_count;
    int allocated_tracks;
    char** line_names;
    int line_name_count;
    bool is_repeat;
    int repeat_count;
} GridTrackList;

// tier-3: layout-transient, valid within pass
typedef struct GridArea {
    char* name;
    int row_start;
    int row_end;
    int column_start;
    int column_end;
} GridArea;

// tier-3: layout-transient, valid within pass
typedef struct GridLineName {
    char* name;
    int line_number;
    bool is_row;
} GridLineName;

// tier-3: layout-transient, valid within pass
typedef struct GridContainerLayout : GridProp {
    radiant::grid::TrackArray* computed_rows;
    radiant::grid::TrackArray* computed_columns;
    struct ViewBlock** grid_items;
    int item_count;
    int allocated_items;
    GridLineName* line_names;
    int line_name_count;
    int allocated_line_names;
    bool needs_reflow;
    int explicit_row_count;
    int explicit_column_count;
    int implicit_row_count;
    int implicit_column_count;
    int negative_implicit_row_count;
    int negative_implicit_column_count;
    int auto_row_cursor;
    int auto_col_cursor;
    float container_width;
    float container_height;
    float content_width;
    float content_height;
    bool has_explicit_height;
    bool is_shrink_to_fit_width;
    float row_intrinsic_height;
    struct LayoutContext* lycon;
    bool auto_fit_columns[64];
    bool auto_fit_rows[64];
    int auto_fit_col_count;
    int auto_fit_row_count;
    // pass-local grid state lives above this mark and is released together.
    ScratchMark scratch_mark;
} GridContainerLayout;

void init_grid_container(LayoutContext* lycon, struct ViewBlock* container);
void cleanup_grid_container(LayoutContext* lycon);

// Mirrors BlockContextScope: pass-local grid scratch and parent context must
// unwind together, even when no-item or absolute-only grid paths return early.
// tier-3: layout-transient, valid within pass
struct GridLayoutScope {
    LayoutContext* lycon;
    GridContainerLayout* saved;
    bool active;

    GridLayoutScope(LayoutContext* l, ViewBlock* container);
    ~GridLayoutScope();
    void close();

    GridLayoutScope(const GridLayoutScope&) = delete;
    GridLayoutScope& operator=(const GridLayoutScope&) = delete;
};

GridTrackList* create_grid_track_list(int initial_capacity);
void destroy_grid_track_list(GridTrackList* track_list);
GridTrackSize* create_grid_track_size(GridTrackSizeType type, int value);
GridTrackSize* clone_grid_track_size(const GridTrackSize* track_size);
void destroy_grid_track_size(GridTrackSize* track_size);
GridArea* create_grid_area(const char* name, int row_start, int row_end, int column_start, int column_end);
char* grid_scratch_strdup(ScratchArena* scratch, const char* source);
void destroy_grid_area(GridArea* area);
void add_grid_line_name(GridContainerLayout* grid, const char* name, int line_number, bool is_row);
int find_grid_line_by_name(GridContainerLayout* grid, const char* name, bool is_row);
int collect_grid_items(GridContainerLayout* grid_layout, struct ViewBlock* container, struct ViewBlock*** items);
void determine_grid_size(GridContainerLayout* grid_layout);
void initialize_track_sizes(GridContainerLayout* grid_layout);
void resolve_track_sizes_enhanced(GridContainerLayout* grid_layout, struct ViewBlock* container);
void position_grid_items(GridContainerLayout* grid_layout, struct ViewBlock* container, ScratchArena* sa);
void align_grid_items(GridContainerLayout* grid_layout);
void align_grid_item(struct ViewBlock* item, GridContainerLayout* grid_layout);
void parse_grid_template_areas(GridProp* grid_layout, const char* areas_string, ScratchArena* sa);
void resolve_grid_template_areas(GridContainerLayout* grid_layout);
IntrinsicSizes calculate_grid_item_intrinsic_sizes(LayoutContext* lycon, ViewBlock* item, bool is_row_axis);
void layout_grid_container(LayoutContext* lycon, ViewBlock* container);
void layout_grid_content(LayoutContext* lycon, ViewBlock* grid_container);
int resolve_grid_item_styles(LayoutContext* lycon, ViewBlock* grid_container);
void init_grid_item_view(LayoutContext* lycon, DomNode* child);
void measure_grid_items(LayoutContext* lycon, GridContainerLayout* grid_layout);
void measure_grid_item_intrinsic(LayoutContext* lycon, ViewBlock* item,
                                  float* min_width, float* max_width,
                                  float* min_height, float* max_height);
void layout_final_grid_content(LayoutContext* lycon, GridContainerLayout* grid_layout);
void layout_grid_absolute_children(LayoutContext* lycon, ViewBlock* container);

// tier-3: layout-transient, valid within pass
typedef struct LayoutContext {
    View* view;  // current view
    DomNode* elmt;  // current dom element, used before the view is created

    BlockContext block;  // unified block context (layout state + floats + BFC)
    Linebox line;  // current linebox
    FontBox font;  // current font style
    float root_font_size;
    // StackingBox* stacking;  // current stacking context for positioned elements
    FlexContainerLayout* flex_container; // integrated flex container layout
    GridContainerLayout* grid_container; // integrated grid container layout

    DomDocument* doc;
    UiContext* ui_context;
    // Additional fields for test compatibility
    float width, height;  // context dimensions
    float dpi;           // dots per inch
    Pool* pool;  // memory pool for view allocation

    // Available space constraints for current layout
    // This enables layout code to distinguish between:
    // - Normal layout (definite width/height)
    // - Intrinsic sizing (min-content/max-content measurement)
    AvailableSpace available_space;

    // Run mode for layout optimization (Taffy-inspired)
    // - ComputeSize: Only compute dimensions, skip positioning (for measurement)
    // - PerformLayout: Full layout with final positioning
    // - PerformHiddenLayout: Minimal layout for display:none
    radiant::RunMode run_mode;

    // Sizing mode for intrinsic size computation
    // - InherentSize: Use element's own CSS size properties
    // - ContentSize: Use content-based size (ignore CSS width/height)
    radiant::SizingMode sizing_mode;

    // Counter tracking for CSS counters (counter-reset, counter-increment, counter(), counters())
    CounterContext* counter_context;

    // LIFO scratch allocator for scoped temporary buffers (table metadata, grid arrays, etc.)
    ScratchArena scratch;

    // Recursion depth guard against deeply nested DOM trees (fuzzer-found stack overflow)
    int depth;

    // Flex-specific nesting depth guard (flex-in-flex recursion)
    int flex_depth;

    // Grid-specific nesting depth guard (grid-in-grid multipass recursion)
    int grid_depth;

    // Total node count guard against pathological layouts (fuzzer-found timeouts)
    int node_count;

    // Structured layout debug categories and optional release profiling buckets
    radiant::LayoutDebugState layout_debug;
    radiant::LayoutProfiler profiler;
} LayoutContext;

// ============================================================================
// LayoutContext Run Mode Helpers
// ============================================================================

/**
 * Check if layout is in measurement mode (only computing sizes)
 */
inline bool layout_context_is_measuring(LayoutContext* lycon) {
    return lycon->run_mode == radiant::RunMode::ComputeSize;
}

/**
 * Check if layout should perform full positioning
 */
// ============================================================================
// Percentage Resolution
// ============================================================================

bool layout_resolve_percentage_value(const CssValue* value, float percentage_base, float* out);
bool layout_resolve_deferred_percentage(float percent, float percentage_base, float* out);
bool layout_apply_deferred_percentage(float percent, float percentage_base, float* target, float* resolved);
float layout_block_used_content_size(ViewBlock* block, bool horizontal, bool require_positive);
float layout_block_given_content_size(ViewBlock* block, bool horizontal);
float layout_block_declared_content_size(LayoutContext* lycon, ViewBlock* block, CssPropertyId property, bool horizontal);
float layout_block_auto_content_width_from_inline_base(ViewBlock* block, float inline_base);
void layout_reresolve_percentage_box(ViewBlock* block, float inline_base);

// ============================================================================
// Table Captions
// ============================================================================

float relayout_table_caption(LayoutContext* lycon, ViewBlock* cap, float table_width);
float adjust_table_caption_width(ViewBlock* cap, float wrapper_content_width);

// ============================================================================
// Table Layout
// ============================================================================

void layout_table_content(LayoutContext* lycon, DomNode* elmt, DisplayValue display);
struct ViewTable* build_table_tree(LayoutContext* lycon, DomNode* elmt);
void table_auto_layout(LayoutContext* lycon, struct ViewTable* table);
void adjust_table_text_positions_final(struct ViewTable* table);
float find_first_baseline_recursive(LayoutContext* lycon, View* parent, float cumulative_y, bool use_normal_lh = false);
void adjust_row_text_positions_final(struct ViewTable* table, struct ViewBlock* row,
    float table_abs_x, float cell_border, float cell_padding);
void adjust_cell_text_positions_final(struct ViewBlock* cell, float text_abs_x);
bool wrap_orphaned_table_children(LayoutContext* lycon, struct DomElement* parent);
bool is_table_internal_display(CssEnum display);

// ============================================================================
// Absolute Children
// ============================================================================

typedef enum AbsStaticContextKind {
    ABS_STATIC_BLOCK,
    ABS_STATIC_FLEX,
    ABS_STATIC_GRID,
} AbsStaticContextKind;

struct AbsStaticContext;

// tier-3: layout-transient, valid within pass
typedef struct AbsChildLayoutState {
    DomNode* child;
    ViewBlock* child_block;
    LayoutContainingBlock containing_block;
    BlockContext parent_block;
    Linebox parent_line;
    float original_given_width;
    float original_given_height;
    bool has_grid_area;
    float grid_area_x;
    float grid_area_y;
    float grid_area_width;
    float grid_area_height;
} AbsChildLayoutState;

typedef void (*AbsPrepareChildFn)(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx, AbsChildLayoutState* state);
typedef void (*AbsAfterChildFn)(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx, AbsChildLayoutState* state);

// tier-3: layout-transient, valid within pass
typedef struct AbsStaticContext {
    AbsStaticContextKind kind;
    LayoutContainingBlock containing_block;
    FlexContainerLayout* flex;
    GridContainerLayout* grid;
    bool resolve_percent_against_content_box;
    const char* log_context;
    AbsPrepareChildFn prepare_child;
    AbsAfterChildFn after_child;
} AbsStaticContext;

void layout_absolute_children_in_context(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx);

namespace radiant {

// tier-3: layout-transient, valid within pass
struct LayoutRunModeScope {
    ::LayoutContext* lycon;
    RunMode saved_run_mode;

    LayoutRunModeScope(::LayoutContext* l, RunMode mode);
    ~LayoutRunModeScope();

    LayoutRunModeScope(const LayoutRunModeScope&) = delete;
    LayoutRunModeScope& operator=(const LayoutRunModeScope&) = delete;
};

// tier-3: layout-transient, valid within pass
struct LayoutMeasureScope {
    ::LayoutContext* lycon;
    BlockContext saved_block;
    Linebox saved_line;
    FontBox saved_font;
    ::DomNode* saved_elmt;
    RunMode saved_run_mode;
    SizingMode saved_sizing_mode;
    AvailableSpace saved_available_space;
    ArrayList* saved_views;

    LayoutMeasureScope(::LayoutContext* l, ::DomNode* measure_elmt);
    ~LayoutMeasureScope();

    LayoutMeasureScope(const LayoutMeasureScope&) = delete;
    LayoutMeasureScope& operator=(const LayoutMeasureScope&) = delete;
};

KnownDimensions layout_known_dimensions_from_block(::ViewBlock* block);
KnownDimensions layout_known_dimensions_from_context(::LayoutContext* lycon);

bool layout_pass_cache_get(::LayoutContext* lycon, ::DomElement* element,
    KnownDimensions known_dimensions, SizeF* out_size, const char* label);

void layout_pass_cache_store(::LayoutContext* lycon, ::DomElement* element,
    KnownDimensions known_dimensions, SizeF result, const char* label);

bool layout_pass_cache_get_for_space(::LayoutContext* lycon, ::DomElement* element,
    KnownDimensions known_dimensions, AvailableSpace available_space,
    SizeF* out_size, const char* label);

void layout_pass_cache_store_for_space(::LayoutContext* lycon, ::DomElement* element,
    KnownDimensions known_dimensions, AvailableSpace available_space,
    SizeF result, const char* label);

LayoutCache* layout_pass_ensure_cache(::LayoutContext* lycon, ::DomElement* element);

} // namespace radiant

// ============================================================================
// BlockContext API - Unified Block Formatting Context Functions
// ============================================================================

/**
 * Initialize a BlockContext for an element
 * Sets up layout state and float tracking
 */
void block_context_init(BlockContext* ctx, ViewBlock* element, Pool* pool);

/**
 * Reset BlockContext for a new BFC
 * Clears float lists but keeps layout state
 */
void block_context_reset_floats(BlockContext* ctx);

/**
 * Check if an element establishes a new BFC
 * Per CSS 2.2 Section 9.4.1
 */
bool block_context_establishes_bfc(ViewBlock* block);

/**
 * Add a positioned float to the BlockContext
 */
void block_context_add_float(BlockContext* ctx, ViewBlock* float_elem);

// recompute the cached lowest edge after existing float boxes are translated.
void block_context_recompute_lowest_float_bottom(BlockContext* ctx);

/**
 * Position and add a float at the current layout position
 * Implements CSS 2.2 Section 9.5.1 Rules
 */
/**
 * Get available horizontal space at a given Y coordinate
 * @param ctx The block context
 * @param y Y coordinate relative to content area
 * @param height Height of the line/element being placed
 * @return Available space bounds adjusted for floats
 */
FloatAvailableSpace block_context_space_at_y(BlockContext* ctx, float y, float height);

/**
 * Find the lowest Y where a given width is available
 * @param element_height Height of the element to place (queries full height range for floats)
 */
float block_context_find_y_for_width(BlockContext* ctx, float required_width, float min_y, float element_height = 1.0f);

/**
 * Find Y position to clear floats
 * @param clear_type CSS_VALUE_LEFT, CSS_VALUE_RIGHT, or CSS_VALUE_BOTH
 */
float block_context_clear_y(BlockContext* ctx, CssEnum clear_type);

/**
 * Allocate a FloatBox from the pool
 */
FloatBox* block_context_alloc_float_box(BlockContext* ctx);

/**
 * Update line effective bounds for BFC floats
 * Adjusts line.effective_left and line.effective_right based on floats at current Y
 * @param query_height If > 0, use this height for float queries instead of line-height.
 *                     Used for inline-blocks whose height exceeds the line-height.
 */
void update_line_for_bfc_floats(LayoutContext* lycon, float query_height = 0);

/**
 * Find the BFC root for a given BlockContext
 * Walks up the parent chain to find the nearest BFC-establishing BlockContext
 * @param ctx The starting block context
 * @return The BFC root BlockContext, or NULL if none found
 */
BlockContext* block_context_find_bfc(BlockContext* ctx);

/**
 * Calculate the offset from BFC origin to a view's border-box origin
 * This is used to convert between BFC coordinates and local coordinates
 * @param view The view to calculate offset for
 * @param bfc The BFC root context
 * @param offset_x Output: X offset from BFC to view's border-box
 * @param offset_y Output: Y offset from BFC to view's border-box
 */
void block_context_calc_bfc_offset(ViewElement* view, BlockContext* bfc, float* offset_x, float* offset_y);

// ============================================================================
// Property Allocation
// ============================================================================

static inline void position_prop_init_defaults(PositionProp* prop) {
    if (!prop) return;
    prop->position = CSS_VALUE_STATIC;
    prop->top = prop->right = prop->bottom = prop->left = 0.0f;
    prop->top_percent = prop->right_percent = prop->bottom_percent = prop->left_percent = NAN;
    prop->z_index = 0;
    prop->custom_layout_z_index = 0;
    prop->has_top = prop->has_right = prop->has_bottom = prop->has_left = false;
    prop->has_custom_layout_z_index = false;
    prop->clear = CSS_VALUE_NONE;
    prop->float_prop = CSS_VALUE_NONE;
    prop->static_x_needs_parent_offset = false;
    prop->static_y_needs_parent_offset = false;
    prop->has_static_parent_offset_x = false;
    prop->has_static_parent_offset_y = false;
    prop->static_parent_offset_x = 0.0f;
    prop->static_parent_offset_y = 0.0f;
}

void* alloc_prop(LayoutContext* lycon, size_t size);
FontProp* alloc_font_prop(LayoutContext* lycon);
void alloc_flex_prop(LayoutContext* lycon, ViewBlock* block);
void alloc_flex_item_prop(LayoutContext* lycon, ViewSpan* block);
void reset_flex_item_prop_for_style(LayoutContext* lycon, ViewSpan* block);
void alloc_grid_prop(LayoutContext* lycon, ViewBlock* block);
void alloc_grid_item_prop(LayoutContext* lycon, ViewSpan* span);
PseudoContentProp* alloc_pseudo_content_prop(LayoutContext* lycon, ViewBlock* block);
void generate_pseudo_element_content(LayoutContext* lycon, ViewBlock* block, bool is_before);
void insert_pseudo_into_dom(DomElement* parent, DomElement* pseudo, bool is_before);
View* set_view(LayoutContext* lycon, ViewType type, DomNode* node);

// ============================================================================
// Keyword Mapping: Lambda CSS strings → Lexbor enum values
// ============================================================================

/**
 * Map CSS keyword string to Lexbor enum value
 *
 * @param keyword CSS keyword string (e.g., "block", "inline", "flex")
 * @return Lexbor CSS_VALUE_* constant, or 0 if unknown
 */
int map_css_keyword_to_lexbor(const char* keyword);

/**
 * Map Lambda font-size keyword to pixel value
 * @param keyword const char* keyword string (e.g., "small", "large")
 * @return float font size in pixels
 */
float map_lambda_font_size_keyword(CssEnum keyword_enum);

/**
 * Map Lambda font-weight keyword to numeric value
 * @param keyword const char* keyword string (e.g., "normal", "bold")
 * @return int font weight (100-900)
 */
int map_lambda_font_weight_keyword(const char* keyword);

/**
 * Map Lambda font-family keyword to font name
 * @param keyword const char* keyword string (e.g., "serif", "sans-serif")
 * @return const char* font family name
 */
const char* map_lambda_font_family_keyword(const char* keyword);

void line_break(LayoutContext* lycon);
void line_align(LayoutContext* lycon);
View* layout_inline_fragment_root(View* view);
void layout_flow_node(LayoutContext* lycon, DomNode* node);
void layout_block(LayoutContext* lycon, DomNode* elmt, DisplayValue display);
void layout_text(LayoutContext* lycon, DomNode* text_node);
void layout_inline(LayoutContext* lycon, DomNode* elmt, DisplayValue display);
void layout_flex_container(LayoutContext* lycon, ViewBlock* container);
void layout_html_root(LayoutContext* lycon, DomNode* elmt);
bool is_only_whitespace(const char* str);

static inline bool layout_suppress_ignorable_container_text(DomNode* node) {
    if (!node || !node->is_text()) return false;
    const char* text = (const char*)node->text_data();
    if (text && !is_only_whitespace(text)) return false;
    node->view_type = RDT_VIEW_NONE;
    return true;
}

static inline bool layout_text_node_has_content(DomNode* node) {
    if (!node || !node->is_text()) return false;
    const char* text = (const char*)node->text_data();
    return text && !is_only_whitespace(text);
}

static inline bool layout_display_is_none(DisplayValue display) {
    return display.outer == CSS_VALUE_NONE || display.inner == CSS_VALUE_NONE;
}

static inline bool layout_block_is_display_none(const ViewBlock* block) {
    return !block || layout_display_is_none(block->display);
}

static inline bool layout_element_is_display_none(const DomElement* element) {
    return !element || layout_display_is_none(element->display);
}

// CSS Positioning functions
void layout_relative_position_offset(ViewBlock* block, float* offset_x, float* offset_y);
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block);
void layout_sticky_positioned(LayoutContext* lycon, ViewBlock* block);
bool element_has_float(ViewBlock* block);
ViewBlock* find_initial_containing_view_block(ViewBlock* element);
ViewBlock* find_positioned_containing_block(ViewElement* view);
ViewBlock* find_containing_block(ViewBlock* element, CssEnum position_type);
void layout_float_element(LayoutContext* lycon, ViewBlock* block);
void adjust_line_for_floats(LayoutContext* lycon);
void layout_clear_element(LayoutContext* lycon, ViewBlock* block);
void re_resolve_abs_children_vertical(ViewBlock* containing_block);
void layout_finalize_static_positioned_abs_descendants(ViewBlock* root);
void layout_shift_static_positioned_abs_descendants(ViewElement* root, float delta_x, float delta_y);

static inline bool layout_position_is_abs_fixed(const PositionProp* position) {
    return position &&
           (position->position == CSS_VALUE_ABSOLUTE ||
            position->position == CSS_VALUE_FIXED);
}

static inline bool layout_position_is_floated(const PositionProp* position) {
    return position &&
           (position->float_prop == CSS_VALUE_LEFT ||
            position->float_prop == CSS_VALUE_RIGHT);
}

static inline bool layout_view_is_out_of_flow_positioned(const View* view) {
    const DomNode* node = static_cast<const DomNode*>(view);
    const DomElement* element = node ? node->as_element() : nullptr;
    return element && layout_position_is_abs_fixed(element->position);
}

static inline bool layout_view_is_out_of_flow(const View* view) {
    const DomNode* node = static_cast<const DomNode*>(view);
    const DomElement* element = node ? node->as_element() : nullptr;
    return element &&
           (layout_position_is_abs_fixed(element->position) ||
            layout_position_is_floated(element->position));
}

static inline bool layout_block_is_out_of_flow_positioned(const ViewBlock* block) {
    return block && layout_position_is_abs_fixed(block->position);
}

static inline bool layout_block_is_out_of_flow(const ViewBlock* block) {
    return block &&
           (layout_position_is_abs_fixed(block->position) ||
            layout_position_is_floated(block->position));
}

static inline bool layout_block_is_hidden_or_display_none(const ViewBlock* block) {
    return !block ||
           layout_block_is_display_none(block) ||
           (block->in_line && block->inl()->visibility == VIS_HIDDEN);
}

static inline bool layout_block_is_skipped_container_item(const ViewBlock* block) {
    return layout_block_is_hidden_or_display_none(block) ||
           layout_block_is_out_of_flow_positioned(block);
}

void line_init(LayoutContext* lycon, float left, float right);
void line_reset(LayoutContext* lycon);
float vertical_align_baseline_shift(LayoutContext* lycon, CssEnum align,
                                    float valign_offset = 0);
float calculate_vertical_align_offset(LayoutContext* lycon, CssEnum align, float item_height, float line_height, float baseline_pos, float item_baseline, float valign_offset = 0);
bool layout_zero_sized_atomic_in_vertical_lr(ViewBlock* block);
float layout_unresolved_html_cell_horizontal_box_extra(DomElement* cell);
void view_vertical_align(LayoutContext* lycon, View* view);
float line_baseline_position(LayoutContext* lycon, float* out_line_height);
bool layout_quirks_block_ignores_line_height(LayoutContext* lycon, ViewBlock* block);
float layout_inline_font_box_y(LayoutContext* lycon, ViewSpan* span,
                               float span_line_height,
                               float ascender, float descender,
                               float baseline_pos, float border_top, float padding_top);

// Structure for OS/2 sTypo metrics (shared across layout modules)
// tier-3: layout-transient, valid within pass
struct TypoMetrics {
    float ascender;      // sTypoAscender in CSS pixels
    float descender;     // sTypoDescender in CSS pixels (positive value)
    float line_gap;      // sTypoLineGap in CSS pixels (floored at 0)
    bool valid;
    bool use_typo_metrics;  // fsSelection bit 7
};

// Get OS/2 sTypo metrics for a font handle
// Returns metrics with valid=false if no OS/2 table is available
TypoMetrics get_os2_typo_metrics(struct FontHandle* handle);

// Calculate normal line height following Chrome's algorithm
// Delegates to font_calc_normal_line_height() from lib/font/
float calc_normal_line_height(struct FontHandle* handle);
bool layout_quirky_container_ignores_child_margin_bottom(
    LayoutContext* lycon, ViewBlock* container, ViewBlock* child);
CssEnum layout_specified_keyword(DomElement* element, CssPropertyId property,
                                 CssEnum fallback = (CssEnum)0);
float layout_resolve_line_height_value(LayoutContext* lycon, const CssValue* value,
                                       DomElement* owner, float target_font_size);
float layout_measure_space_advance(LayoutContext* lycon, struct FontHandle* handle,
                                   FontProp* style);
size_t layout_normalize_collapsible_whitespace(const char* text, size_t length,
                                               char* buffer, size_t buffer_size);

// DomNode style resolution
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);

CssValue inherit_line_height(LayoutContext* lycon, ViewBlock* block);
void setup_line_height(LayoutContext* lycon, ViewBlock* block);
void layout_setup_block_font_metrics(LayoutContext* lycon);

// ViewSpan bounding box computation
void compute_span_bounding_box(ViewSpan* span, bool is_multi_line = false, struct FontHandle* fallback_fh = nullptr);
void recompute_span_bounding_box_after_line_layout(
    ViewSpan* span, bool is_multi_line, struct FontHandle* fallback_fh = nullptr);
bool inline_span_has_multiple_line_fragments(ViewSpan* span);
bool inline_span_float_continuation_x(
    ViewSpan* span, float* continuation_x, bool* has_left_float);

// ============================================================================
// CSS text-transform
// ============================================================================

/**
 * Apply CSS text-transform to a single Unicode codepoint.
 * @param codepoint Input Unicode codepoint
 * @param text_transform CSS text-transform value (CSS_VALUE_UPPERCASE, etc.)
 * @param is_word_start True if this is the first character of a word (for capitalize)
 * @return Transformed codepoint
 */
uint32_t apply_text_transform(uint32_t codepoint, CssEnum text_transform, bool is_word_start);

/**
 * Apply CSS text-transform with full Unicode case mapping (1-to-many expansion).
 * Writes up to 3 codepoints to out[] and returns the count.
 */
int apply_text_transform_full(uint32_t codepoint, CssEnum text_transform,
    bool is_word_start, uint32_t* out);

/**
 * Get text-transform property from a BlockProp.
 * @param blk BlockProp structure (can be NULL)
 * @return CSS text-transform value or CSS_VALUE_NONE
 */
CssEnum get_text_transform_from_block(BlockProp* blk);
CssEnum get_text_transform_from_node(DomNode* node);

// ============================================================================
// CJK Justification Utilities (CSS Text 3 §7.3)
// ============================================================================

/**
 * Check if a codepoint has UAX#14 line break class ID (Ideographic).
 * Characters with ID class allow line breaks before and after them.
 * Used for CJK inter-character justification and line-breaking.
 */
bool has_id_line_break_class(uint32_t cp);

/**
 * Count justification opportunities in a UTF-8 text segment.
 * Counts word spaces AND CJK inter-character boundaries per CSS Text 3 §7.3.
 * For CJK text, gaps between adjacent ID-class characters are opportunities.
 * @param str UTF-8 text data
 * @param len byte length of text segment
 * @return number of justification opportunities (spaces + CJK inter-char gaps)
 */
int count_justify_opportunities(const char* str, int len);
int count_rendered_justify_opportunities(ViewText* text, const TextRect* rect,
                                         bool trim_trailing_space,
                                         bool* out_suppressed = nullptr);

// ============================================================================
// Context Scope Guards (§1.8: Prevent context leaks on early returns)
// ============================================================================

/**
 * RAII guard that saves and restores lycon->block on scope exit.
 *
 * Usage:
 *   BlockContextScope bscope(lycon);
 *   // ... layout child, context auto-restored when bscope goes out of scope ...
 */
// tier-3: layout-transient, valid within pass
struct BlockContextScope {
    LayoutContext* lycon;
    BlockContext saved;
    explicit BlockContextScope(LayoutContext* l) : lycon(l), saved(l->block) {}
    ~BlockContextScope() { lycon->block = saved; }
    // Non-copyable
    BlockContextScope(const BlockContextScope&) = delete;
    BlockContextScope& operator=(const BlockContextScope&) = delete;
};

/**
 * RAII guard that saves and restores lycon->block, lycon->line, and lycon->font.
 * Use when all three need to be preserved across a nested layout operation.
 */
// tier-3: layout-transient, valid within pass
struct LayoutContextScope {
    LayoutContext* lycon;
    BlockContext saved_block;
    Linebox saved_line;
    FontBox saved_font;
    explicit LayoutContextScope(LayoutContext* l)
        : lycon(l), saved_block(l->block), saved_line(l->line), saved_font(l->font) {}
    ~LayoutContextScope() {
        lycon->block = saved_block;
        lycon->line  = saved_line;
        lycon->font  = saved_font;
    }
    LayoutContextScope(const LayoutContextScope&) = delete;
    LayoutContextScope& operator=(const LayoutContextScope&) = delete;
};

/**
 * RAII guard for a top-level layout pass.
 * Mirrors BlockContextScope: constructor initializes pass resources, destructor
 * releases scratch/counters even when later layout code returns early.
 */
void layout_init(LayoutContext* lycon, DomDocument* doc, UiContext* uicon);
void layout_cleanup(LayoutContext* lycon);

// tier-3: layout-transient, valid within pass
struct LayoutPassScope {
    LayoutContext* lycon;
    bool active;
    LayoutPassScope(LayoutContext* l, DomDocument* doc, UiContext* uicon)
        : lycon(l), active(false) {
        if (!lycon) return;
        layout_init(lycon, doc, uicon);
        active = true;
    }
    ~LayoutPassScope() {
        // Pass resources are paired structurally so post-init early returns
        // cannot strand counter or scratch registrations.
        if (active) layout_cleanup(lycon);
    }
    LayoutPassScope(const LayoutPassScope&) = delete;
    LayoutPassScope& operator=(const LayoutPassScope&) = delete;
};

// Forward declaration
struct DocState;

// View tree printing functions (output CSS logical pixels directly)
void print_view_tree(ViewElement* view_root, Url* url, const char* output_path = nullptr);
void print_view_tree_json(ViewElement* view_root, Url* url, const char* output_path = nullptr);
bool view_memory_profile_write(DomDocument* doc, const char* input_file,
                               const char* output_path);

// Print caret state to view_tree.txt (appends caret info)
void print_caret_state(DocState* state, const char* output_path = nullptr);
void print_block_json(ViewBlock* block, StrBuf* buf, int indent, bool is_root = false);
void print_text_json(ViewText* text, StrBuf* buf, int indent);
void print_br_json(View* br, StrBuf* buf, int indent);
void print_inline_json(ViewSpan* span, StrBuf* buf, int indent);

const char* form_button_label_text(ViewBlock* block, FormControlProp* form);

// Text combination control for view tree output
// When false, consecutive text nodes are output separately (useful for PDF testing)
void set_combine_text_nodes(bool combine);

// HTML version detection functions
int detect_html_version_lambda_css(DomDocument* doc);
HtmlVersion detect_html_version_from_lambda_element(Element* html_root, Input* input);

#endif // LAYOUT_HPP
