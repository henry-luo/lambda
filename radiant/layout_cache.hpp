#pragma once
/**
 * layout_cache.hpp - 9-slot layout cache for avoiding redundant computations
 *
 * Implements Taffy-style caching where each node can store up to 9 cached
 * measurement results plus one final layout result:
 *
 * Slot 0: Both dimensions known
 * Slots 1-2: Width known (MaxContent/MinContent height)
 * Slots 3-4: Height known (MaxContent/MinContent width)
 * Slots 5-8: Neither known (4 combinations of MinContent/MaxContent)
 *
 * The cache dramatically reduces redundant layout computation when elements
 * are measured multiple times with the same constraints.
 */

#include <cstdint>
#include "available_space.hpp"
#include "layout_mode.hpp"
#include "../lib/mempool.h"  // for pool_calloc

// Forward declaration to avoid circular include
struct DomElement;

// ============================================================================
// Global cache statistics (defined in layout.cpp)
// ============================================================================
extern int64_t g_layout_cache_hits;
extern int64_t g_layout_cache_misses;
extern int64_t g_layout_cache_stores;

namespace radiant {

// ============================================================================
// Constants
// ============================================================================

#define LAYOUT_CACHE_SIZE 9

// ============================================================================
// KnownDimensions - Input constraint tracking
// ============================================================================

/**
 * KnownDimensions tracks which dimensions are explicitly provided as input
 * to layout. This affects cache slot selection.
 */
struct KnownDimensions {
    float width;
    float height;
    bool has_width;
    bool has_height;
};

// Constructors
inline KnownDimensions known_dimensions_none() {
    return {0.0f, 0.0f, false, false};
}

inline KnownDimensions known_dimensions_width(float w) {
    return {w, 0.0f, true, false};
}

inline KnownDimensions known_dimensions_height(float h) {
    return {0.0f, h, false, true};
}

inline KnownDimensions known_dimensions_both(float w, float h) {
    return {w, h, true, true};
}

// ============================================================================
// SizeF - Simple float size (width, height)
// ============================================================================

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

// ============================================================================
// CacheEntry - Single cached measurement result
// ============================================================================

struct CacheEntry {
    KnownDimensions known_dimensions;  // Input: known sizes
    AvailableSpace available_space;    // Input: constraints (2D)
    SizeF computed_size;               // Output: computed dimensions
    bool valid;                        // Whether this entry is populated
};

// ============================================================================
// LayoutCache - 9-slot measurement cache + final layout
// ============================================================================

struct LayoutCache {
    CacheEntry final_layout;                        // For PerformLayout mode
    CacheEntry measure_entries[LAYOUT_CACHE_SIZE];  // For ComputeSize mode
    bool is_empty;                                  // True if cache has never been used
};

// ============================================================================
// C-style API
// ============================================================================

/**
 * Initialize a layout cache (zeroes all entries)
 */
inline void layout_cache_init(LayoutCache* cache) {
    cache->final_layout.valid = false;
    for (int i = 0; i < LAYOUT_CACHE_SIZE; i++) {
        cache->measure_entries[i].valid = false;
    }
    cache->is_empty = true;
}

/**
 * Clear all cached entries
 */
inline void layout_cache_clear(LayoutCache* cache) {
    layout_cache_init(cache);
}

/**
 * Compute cache slot index from constraints (0-8)
 *
 * Slot allocation:
 *   0: Both dimensions known
 *   1: Width known, height MaxContent/Definite
 *   2: Width known, height MinContent
 *   3: Height known, width MaxContent/Definite
 *   4: Height known, width MinContent
 *   5: Neither known, both MaxContent/Definite
 *   6: Neither known, width MaxContent, height MinContent
 *   7: Neither known, width MinContent, height MaxContent
 *   8: Neither known, both MinContent
 */
inline int layout_cache_compute_slot(
    KnownDimensions known_dimensions,
    AvailableSpace available_space
) {
    bool has_width = known_dimensions.has_width;
    bool has_height = known_dimensions.has_height;

    // Slot 0: Both dimensions known
    if (has_width && has_height) return 0;

    // Slots 1-2: Width known, height unknown
    if (has_width) {
        return available_space.height.is_min_content() ? 2 : 1;
    }

    // Slots 3-4: Height known, width unknown
    if (has_height) {
        return available_space.width.is_min_content() ? 4 : 3;
    }

    // Slots 5-8: Neither known
    bool width_is_min = available_space.width.is_min_content();
    bool height_is_min = available_space.height.is_min_content();

    if (!width_is_min && !height_is_min) return 5;  // Both MaxContent/Definite
    if (!width_is_min && height_is_min) return 6;   // Width MaxContent, Height MinContent
    if (width_is_min && !height_is_min) return 7;   // Width MinContent, Height MaxContent
    return 8;                                        // Both MinContent
}

/**
 * Check if constraints match a cache entry (with tolerance for floats)
 */
inline bool layout_cache_constraints_match(
    CacheEntry* entry,
    KnownDimensions known,
    AvailableSpace available,
    float tolerance = 0.1f
) {
    if (!entry->valid) return false;

    // Check known dimensions match
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

    // Check available space types match
    if (entry->available_space.width.type != available.width.type) return false;
    if (entry->available_space.height.type != available.height.type) return false;

    // For definite available space, check values match
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

/**
 * Try to get cached result.
 * Returns true if found, writes result to out_size.
 */
inline bool layout_cache_get(
    LayoutCache* cache,
    KnownDimensions known_dimensions,
    AvailableSpace available_space,
    RunMode mode,
    SizeF* out_size
) {
    if (cache->is_empty) return false;

    if (mode == RunMode::PerformLayout) {
        // Check final layout cache
        if (layout_cache_constraints_match(&cache->final_layout, known_dimensions, available_space)) {
            *out_size = cache->final_layout.computed_size;
            return true;
        }
        return false;
    }

    // ComputeSize mode - check measurement cache slot
    int slot = layout_cache_compute_slot(known_dimensions, available_space);
    CacheEntry* entry = &cache->measure_entries[slot];

    if (layout_cache_constraints_match(entry, known_dimensions, available_space)) {
        *out_size = entry->computed_size;
        return true;
    }

    return false;
}

/**
 * Store computed result in cache
 */
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
