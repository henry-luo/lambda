#pragma once
/**
 * flex_grid_item.hpp - Unified per-item layout state for flex/grid items
 *
 * FlexGridItem holds ALL intermediate calculations for a single flex or grid item
 * during layout. CSS properties remain in their original property structs
 * (FlexItemProp, GridItemProp); this is purely runtime state.
 *
 * LIFECYCLE:
 * 1. ALLOCATION: Array of FlexGridItem is allocated by FlexGridContext
 * 2. INITIALIZATION: collect_flex_items() or collect_grid_items() populates
 *    from CSS properties (FlexItemProp/GridItemProp)
 * 3. USAGE: Layout algorithm reads/writes intermediate fields
 * 4. CLEANUP: Freed when FlexGridContext is destroyed
 *
 * Memory is managed by FlexGridContext - do NOT manually allocate.
 */

#include <cstdint>
#include "layout_cache.hpp"  // For SizeF

// Forward declarations (avoid circular includes)
struct DomElement;
struct ViewBlock;

namespace radiant {

// ============================================================================
// Optional value types (avoiding std::optional)
// ============================================================================

/**
 * OptionalFloat - Explicit optional pattern for float values
 */
struct OptionalFloat {
    float value;
    bool has_value;
};

inline OptionalFloat optional_float_none() {
    return {0.0f, false};
}

inline OptionalFloat optional_float_some(float v) {
    return {v, true};
}

inline float optional_float_unwrap_or(OptionalFloat opt, float fallback) {
    return opt.has_value ? opt.value : fallback;
}

/**
 * OptionalSizeF - Optional size (width/height may or may not be specified)
 */
struct OptionalSizeF {
    float width;
    float height;
    bool has_width;
    bool has_height;
};

inline OptionalSizeF optional_size_none() {
    return {0.0f, 0.0f, false, false};
}

inline OptionalSizeF optional_size_width(float w) {
    return {w, 0.0f, true, false};
}

inline OptionalSizeF optional_size_height(float h) {
    return {0.0f, h, false, true};
}

inline OptionalSizeF optional_size_both(float w, float h) {
    return {w, h, true, true};
}

inline float optional_size_main(OptionalSizeF s, bool is_row, float fallback) {
    if (is_row) return s.has_width ? s.width : fallback;
    return s.has_height ? s.height : fallback;
}

inline float optional_size_cross(OptionalSizeF s, bool is_row, float fallback) {
    if (is_row) return s.has_height ? s.height : fallback;
    return s.has_width ? s.width : fallback;
}

// ============================================================================
// RectF - Four-sided float values (for margin/padding/border)
// ============================================================================

/**
 * RectF - top/right/bottom/left float values
 * Used for margins, padding, border widths
 */
struct RectF {
    float top;
    float right;
    float bottom;
    float left;
};

inline RectF rect_f_zero() {
    return {0.0f, 0.0f, 0.0f, 0.0f};
}

inline RectF rect_f(float top, float right, float bottom, float left) {
    return {top, right, bottom, left};
}

inline RectF rect_f_uniform(float v) {
    return {v, v, v, v};
}

// Get sum of horizontal (left + right)
inline float rect_f_horizontal(RectF r) {
    return r.left + r.right;
}

// Get sum of vertical (top + bottom)
inline float rect_f_vertical(RectF r) {
    return r.top + r.bottom;
}

// Get main-axis sum (start + end)
inline float rect_f_main(RectF r, bool is_row) {
    return is_row ? (r.left + r.right) : (r.top + r.bottom);
}

// Get cross-axis sum
inline float rect_f_cross(RectF r, bool is_row) {
    return is_row ? (r.top + r.bottom) : (r.left + r.right);
}

// Get main-axis start
inline float rect_f_main_start(RectF r, bool is_row) {
    return is_row ? r.left : r.top;
}

// Get main-axis end
inline float rect_f_main_end(RectF r, bool is_row) {
    return is_row ? r.right : r.bottom;
}

// Get cross-axis start
inline float rect_f_cross_start(RectF r, bool is_row) {
    return is_row ? r.top : r.left;
}

// Get cross-axis end
inline float rect_f_cross_end(RectF r, bool is_row) {
    return is_row ? r.bottom : r.right;
}

// ============================================================================
// IntrinsicSizesCache - Cached intrinsic measurements
// ============================================================================

/**
 * IntrinsicSizesCache - Holds min/max content sizes for both axes
 */
struct IntrinsicSizesCache {
    float min_content_width;
    float max_content_width;
    float min_content_height;
    float max_content_height;
    bool valid;  // Whether cache is populated
};

inline IntrinsicSizesCache intrinsic_sizes_cache_empty() {
    return {0.0f, 0.0f, 0.0f, 0.0f, false};
}

inline float intrinsic_sizes_min_content(IntrinsicSizesCache* cache, bool is_row) {
    return is_row ? cache->min_content_width : cache->min_content_height;
}

inline float intrinsic_sizes_max_content(IntrinsicSizesCache* cache, bool is_row) {
    return is_row ? cache->max_content_width : cache->max_content_height;
}

// ============================================================================
// FlexGridItem - Unified per-item layout state
// ============================================================================

/**
 * FlexGridItem - Unified intermediate layout state for ONE flex/grid item
 *
 * This structure holds ALL intermediate calculations for a single item during
 * flex or grid layout.
 */
struct FlexGridItem {
    // === Node reference ===
    DomElement* node;
    ViewBlock* view;
    uint32_t source_order;  // Original order in DOM

    // === Resolved CSS properties (computed once from FlexItemProp/GridItemProp) ===
    OptionalSizeF size;           // width/height if specified (in pixels)
    OptionalSizeF min_size;       // min-width/min-height
    OptionalSizeF max_size;       // max-width/max-height
    RectF margin;                 // Resolved margins (pixels)
    RectF padding;                // Resolved padding (pixels)
    RectF border;                 // Resolved border widths (pixels)
    OptionalFloat aspect_ratio;

    // === Flex-specific resolved properties ===
    float flex_grow;
    float flex_shrink;
    float flex_basis;             // Resolved flex-basis (pixels, -1 for auto)
    int32_t align_self;           // CSS enum value (CSS_VALUE_*)
    int32_t order;                // CSS order property

    // === Grid-specific resolved properties ===
    int32_t justify_self;         // CSS enum value
    int32_t row_start;            // Grid line numbers (-1 for auto)
    int32_t row_end;
    int32_t col_start;
    int32_t col_end;
    bool row_start_is_span;       // True if row_start means "span N"
    bool row_end_is_span;
    bool col_start_is_span;
    bool col_end_is_span;

    // === Intrinsic size cache (shared by flex and grid) ===
    IntrinsicSizesCache intrinsic_cache;

    // === Intermediate calculations (flex algorithm) ===
    float inner_flex_basis;              // Flex basis after clamping
    float outer_flex_basis;              // inner_flex_basis + margins
    float resolved_minimum_main_size;    // Automatic minimum size
    SizeF hypothetical_inner_size;       // After clamping to min/max
    SizeF hypothetical_outer_size;       // With margins
    SizeF target_size;                   // After flex factor distribution
    SizeF outer_target_size;             // With margins
    float violation;                     // For min/max violation resolution
    float content_flex_fraction;         // For fr unit resolution
    bool frozen;                         // Item frozen during flex resolution

    // === Intermediate calculations (grid algorithm) ===
    int32_t placed_row;                  // Resolved row after auto-placement
    int32_t placed_col;
    int32_t row_span;                    // Actual row span
    int32_t col_span;                    // Actual column span

    // === Final output (flex and grid) ===
    float offset_main;                   // Offset in main axis (from line start)
    float offset_cross;                  // Offset in cross axis (from line start)
    float baseline;                      // First baseline offset from top

    // === Margin auto detection (for auto margin distribution) ===
    bool margin_top_is_auto;
    bool margin_right_is_auto;
    bool margin_bottom_is_auto;
    bool margin_left_is_auto;
};

// ============================================================================
// FlexGridItem helper functions
// ============================================================================

/**
 * Initialize a FlexGridItem with default values
 */
inline void flex_grid_item_init(FlexGridItem* item) {
    item->node = nullptr;
    item->view = nullptr;
    item->source_order = 0;

    item->size = optional_size_none();
    item->min_size = optional_size_none();
    item->max_size = optional_size_none();
    item->margin = rect_f_zero();
    item->padding = rect_f_zero();
    item->border = rect_f_zero();
    item->aspect_ratio = optional_float_none();

    item->flex_grow = 0.0f;
    item->flex_shrink = 1.0f;
    item->flex_basis = -1.0f;  // auto
    item->align_self = 0;      // CSS_VALUE_AUTO
    item->order = 0;

    item->justify_self = 0;
    item->row_start = -1;
    item->row_end = -1;
    item->col_start = -1;
    item->col_end = -1;
    item->row_start_is_span = false;
    item->row_end_is_span = false;
    item->col_start_is_span = false;
    item->col_end_is_span = false;

    item->intrinsic_cache = intrinsic_sizes_cache_empty();

    item->inner_flex_basis = 0.0f;
    item->outer_flex_basis = 0.0f;
    item->resolved_minimum_main_size = 0.0f;
    item->hypothetical_inner_size = size_f_zero();
    item->hypothetical_outer_size = size_f_zero();
    item->target_size = size_f_zero();
    item->outer_target_size = size_f_zero();
    item->violation = 0.0f;
    item->content_flex_fraction = 0.0f;
    item->frozen = false;

    item->placed_row = -1;
    item->placed_col = -1;
    item->row_span = 1;
    item->col_span = 1;

    item->offset_main = 0.0f;
    item->offset_cross = 0.0f;
    item->baseline = -1.0f;

    item->margin_top_is_auto = false;
    item->margin_right_is_auto = false;
    item->margin_bottom_is_auto = false;
    item->margin_left_is_auto = false;
}

/**
 * Get padding + border in main axis direction
 */
inline float flex_grid_item_padding_border_main(FlexGridItem* item, bool is_row) {
    return rect_f_main(item->padding, is_row) + rect_f_main(item->border, is_row);
}

/**
 * Get padding + border in cross axis direction
 */
inline float flex_grid_item_padding_border_cross(FlexGridItem* item, bool is_row) {
    return rect_f_cross(item->padding, is_row) + rect_f_cross(item->border, is_row);
}

/**
 * Get margin in main axis direction
 */
inline float flex_grid_item_margin_main(FlexGridItem* item, bool is_row) {
    return rect_f_main(item->margin, is_row);
}

/**
 * Get margin in cross axis direction
 */
inline float flex_grid_item_margin_cross(FlexGridItem* item, bool is_row) {
    return rect_f_cross(item->margin, is_row);
}

/**
 * Check if item has auto margin in main axis start
 */
inline bool flex_grid_item_has_auto_margin_main_start(FlexGridItem* item, bool is_row) {
    return is_row ? item->margin_left_is_auto : item->margin_top_is_auto;
}

/**
 * Check if item has auto margin in main axis end
 */
inline bool flex_grid_item_has_auto_margin_main_end(FlexGridItem* item, bool is_row) {
    return is_row ? item->margin_right_is_auto : item->margin_bottom_is_auto;
}

/**
 * Check if item has auto margin in cross axis start
 */
inline bool flex_grid_item_has_auto_margin_cross_start(FlexGridItem* item, bool is_row) {
    return is_row ? item->margin_top_is_auto : item->margin_left_is_auto;
}

/**
 * Check if item has auto margin in cross axis end
 */
inline bool flex_grid_item_has_auto_margin_cross_end(FlexGridItem* item, bool is_row) {
    return is_row ? item->margin_bottom_is_auto : item->margin_right_is_auto;
}

/**
 * Get content box size (inner size after padding+border)
 */
inline SizeF flex_grid_item_content_box_size(FlexGridItem* item, SizeF outer_size, bool is_row) {
    float pb_main = flex_grid_item_padding_border_main(item, is_row);
    float pb_cross = flex_grid_item_padding_border_cross(item, is_row);
    if (is_row) {
        return size_f(outer_size.width - pb_main, outer_size.height - pb_cross);
    } else {
        return size_f(outer_size.width - pb_cross, outer_size.height - pb_main);
    }
}

} // namespace radiant
