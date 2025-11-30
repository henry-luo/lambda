/**
 * Available Space Type System for Radiant Layout Engine
 *
 * This module provides type-safe representation of available space constraints
 * during layout, inspired by Ladybird's design. It allows layout code to
 * distinguish between:
 * - Definite sizes (concrete pixel values)
 * - Indefinite sizes (auto/unknown)
 * - Intrinsic sizing modes (min-content, max-content)
 *
 * Based on CSS Intrinsic & Extrinsic Sizing Module Level 3:
 * https://www.w3.org/TR/css-sizing-3/
 */

#pragma once
#include <cmath>

// ============================================================================
// Available Size Type
// ============================================================================

/**
 * Enumeration of available size constraint types.
 */
enum AvailableSizeType {
    AVAILABLE_SIZE_DEFINITE,      // Concrete pixel value
    AVAILABLE_SIZE_INDEFINITE,    // Unknown (auto)
    AVAILABLE_SIZE_MIN_CONTENT,   // Measuring min-content (break at every opportunity)
    AVAILABLE_SIZE_MAX_CONTENT    // Measuring max-content (never wrap)
};

/**
 * Represents a single dimension's available space constraint.
 *
 * This type encapsulates whether a dimension has a concrete size,
 * is indefinite, or is being measured for intrinsic sizing.
 */
struct AvailableSize {
    AvailableSizeType type;
    float value;  // Only valid when type == AVAILABLE_SIZE_DEFINITE

    // Factory methods for creating AvailableSize instances
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

    // Type query methods
    bool is_definite() const { return type == AVAILABLE_SIZE_DEFINITE; }
    bool is_indefinite() const { return type == AVAILABLE_SIZE_INDEFINITE; }
    bool is_min_content() const { return type == AVAILABLE_SIZE_MIN_CONTENT; }
    bool is_max_content() const { return type == AVAILABLE_SIZE_MAX_CONTENT; }

    /**
     * Check if this is an intrinsic sizing constraint.
     * When true, layout should measure content rather than fill available space.
     */
    bool is_intrinsic() const {
        return is_min_content() || is_max_content();
    }

    /**
     * Get the pixel value, or zero if not definite.
     * Useful for calculations where indefinite maps to zero.
     */
    float to_px_or_zero() const {
        return is_definite() ? value : 0;
    }

    /**
     * Get the pixel value, or a fallback if not definite.
     */
    float to_px_or(float fallback) const {
        return is_definite() ? value : fallback;
    }

    /**
     * Get the pixel value, or INFINITY if not definite.
     * Useful for max-width constraints where indefinite means no limit.
     */
    float to_px_or_infinity() const {
        return is_definite() ? value : INFINITY;
    }

    /**
     * Resolve to a definite value given a fallback for indefinite.
     * For intrinsic sizing, returns 0 (caller should handle specially).
     */
    float resolve(float fallback_for_indefinite) const {
        if (is_definite()) return value;
        if (is_indefinite()) return fallback_for_indefinite;
        return 0;  // Intrinsic sizing - caller handles
    }
};

// ============================================================================
// Available Space (2D)
// ============================================================================

/**
 * Represents available space in both dimensions.
 *
 * This is the primary type passed through layout functions to communicate
 * sizing constraints. It allows layout algorithms to adapt their behavior
 * based on whether they're doing normal layout or intrinsic size measurement.
 */
struct AvailableSpace {
    AvailableSize width;
    AvailableSize height;

    // Factory methods for common patterns
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

    /**
     * Create available space for min-content width measurement.
     * Width is min-content, height is typically indefinite.
     */
    static AvailableSpace make_min_content() {
        AvailableSpace s;
        s.width = AvailableSize::make_min_content();
        s.height = AvailableSize::make_indefinite();
        return s;
    }

    /**
     * Create available space for max-content width measurement.
     * Width is max-content, height is typically indefinite.
     */
    static AvailableSpace make_max_content() {
        AvailableSpace s;
        s.width = AvailableSize::make_max_content();
        s.height = AvailableSize::make_indefinite();
        return s;
    }

    /**
     * Create available space with definite width, indefinite height.
     * Common for block layout where width is constrained but height grows.
     */
    static AvailableSpace make_width_definite(float w) {
        AvailableSpace s;
        s.width = AvailableSize::make_definite(w);
        s.height = AvailableSize::make_indefinite();
        return s;
    }

    // Query methods

    /**
     * Check if this is an intrinsic sizing constraint (either axis).
     */
    bool is_intrinsic_sizing() const {
        return width.is_intrinsic() || height.is_intrinsic();
    }

    /**
     * Check if width is being measured for min-content.
     */
    bool is_width_min_content() const {
        return width.is_min_content();
    }

    /**
     * Check if width is being measured for max-content.
     */
    bool is_width_max_content() const {
        return width.is_max_content();
    }

    /**
     * Check if both dimensions are definite.
     */
    bool is_fully_definite() const {
        return width.is_definite() && height.is_definite();
    }
};

// ============================================================================
// Size Constraint Utilities
// ============================================================================

/**
 * Apply min/max constraints to a computed size.
 *
 * @param size The computed size
 * @param min_size Minimum size constraint (or 0 for none)
 * @param max_size Maximum size constraint (or INFINITY for none)
 * @return Clamped size value
 */
inline float apply_size_constraints(float size, float min_size, float max_size) {
    if (min_size > 0 && size < min_size) size = min_size;
    if (max_size > 0 && !std::isinf(max_size) && size > max_size) size = max_size;
    return size;
}

/**
 * Compute shrink-to-fit width (fit-content).
 *
 * fit-content = clamp(min-content, available, max-content)
 *
 * @param min_content Minimum content width
 * @param max_content Maximum content width
 * @param available Available width (may be indefinite)
 * @return Shrink-to-fit width
 */
inline float compute_shrink_to_fit_width(float min_content, float max_content, AvailableSize available) {
    if (available.is_indefinite() || available.is_max_content()) {
        // No constraint - use max-content
        return max_content;
    }
    if (available.is_min_content()) {
        // Min-content mode - use min-content
        return min_content;
    }
    // Definite available width - clamp between min and max content
    float avail = available.value;
    if (avail < min_content) return min_content;
    if (avail > max_content) return max_content;
    return avail;
}
