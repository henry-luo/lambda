/**
 * layout_alignment.cpp - Implementation of unified alignment functions
 */

#include "layout_alignment.hpp"
#include "layout.hpp"
#include "layout_box.hpp"
#include "view.hpp"
#include "../lib/font/font.h"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/css_value.hpp"

namespace radiant {

// ============================================================================
// Alignment Offset Computation
// ============================================================================

float compute_alignment_offset(
    int32_t alignment,
    float free_space,
    bool is_safe
) {
    // Safe alignment: prevent overflow by falling back to start
    if (is_safe && free_space < 0) {
        return 0.0f;
    }

    switch (alignment) {
        case CSS_VALUE_FLEX_START:
        case CSS_VALUE_START:
            return 0.0f;

        case CSS_VALUE_FLEX_END:
        case CSS_VALUE_END:
            return free_space;

        case CSS_VALUE_CENTER:
            return free_space / 2.0f;

        case CSS_VALUE_STRETCH:
            // For single items, stretch means start position
            // Stretching the size is handled separately
            return 0.0f;

        case CSS_VALUE_BASELINE:
            // Baseline alignment offset computed separately
            return 0.0f;

        // Space distribution types - caller should use compute_space_distribution
        case CSS_VALUE_SPACE_BETWEEN:
        case CSS_VALUE_SPACE_AROUND:
        case CSS_VALUE_SPACE_EVENLY:
            return 0.0f;

        default:
            return 0.0f;
    }
}

// ============================================================================
// Space Distribution
// ============================================================================

SpaceDistribution compute_space_distribution(
    int32_t alignment,
    float free_space,
    int32_t item_count,
    float existing_gap
) {
    (void)existing_gap;
    SpaceDistribution dist = {0.0f, 0.0f, 0.0f};

    // No items or single item - no distribution needed
    if (item_count <= 0) {
        return dist;
    }

    // space-* has no useful interval to distribute during overflow; other
    // alignments still use their signed offset so flex-end/center can overflow.
    if (free_space < 0 && alignment_is_space_distribution(alignment)) {
        return dist;
    }

    int32_t gap_count = item_count - 1;  // Gaps between items

    switch (alignment) {
        case CSS_VALUE_FLEX_START:
        case CSS_VALUE_START:
            // All items at start, all free space at end
            dist.gap_after_last = free_space;
            break;

        case CSS_VALUE_FLEX_END:
        case CSS_VALUE_END:
            // All items at end, all free space at start
            dist.gap_before_first = free_space;
            break;

        case CSS_VALUE_CENTER:
            // Items centered, free space split equally at start/end
            dist.gap_before_first = free_space / 2.0f;
            dist.gap_after_last = free_space / 2.0f;
            break;

        case CSS_VALUE_SPACE_BETWEEN:
            // First item at start, last at end, space between
            if (gap_count > 0) {
                dist.gap_between = free_space / gap_count;
            } else {
                // With only one subject there is no interval, so space-between
                // falls back to start instead of inventing centered edge space.
                dist.gap_after_last = free_space;
            }
            break;

        case CSS_VALUE_SPACE_AROUND:
            // Equal space around each item (half at edges)
            if (item_count > 0) {
                float per_item_space = free_space / item_count;
                dist.gap_before_first = per_item_space / 2.0f;
                dist.gap_between = per_item_space;
                dist.gap_after_last = per_item_space / 2.0f;
            }
            break;

        case CSS_VALUE_SPACE_EVENLY:
            // Equal space between all items and edges
            if (item_count > 0) {
                int32_t total_gaps = item_count + 1;  // Including edges
                float per_gap = free_space / total_gaps;
                dist.gap_before_first = per_gap;
                dist.gap_between = per_gap;
                dist.gap_after_last = per_gap;
            }
            break;

        case CSS_VALUE_STRETCH:
            // For content alignment: stretch items to fill
            // Items are stretched individually; gaps stay as-is
            break;

        default:
            // Unknown alignment - treat as flex-start
            dist.gap_after_last = free_space;
            break;
    }

    return dist;
}

// ============================================================================
// Safe Alignment Fallback
// ============================================================================

int32_t alignment_fallback_for_overflow(int32_t alignment, float free_space) {
    if (free_space >= 0) {
        return alignment;
    }

    // Negative free space - space distribution falls back to flex-start
    switch (alignment) {
        case CSS_VALUE_SPACE_BETWEEN:
        case CSS_VALUE_SPACE_AROUND:
        case CSS_VALUE_SPACE_EVENLY:
            return CSS_VALUE_FLEX_START;

        default:
            return alignment;
    }
}

// ============================================================================
// Alignment Value Helpers
// ============================================================================

bool alignment_is_space_distribution(int32_t alignment) {
    return alignment == CSS_VALUE_SPACE_BETWEEN ||
           alignment == CSS_VALUE_SPACE_AROUND ||
           alignment == CSS_VALUE_SPACE_EVENLY;
}

bool alignment_is_baseline(int32_t alignment) {
    return alignment == CSS_VALUE_BASELINE;
}

bool alignment_is_stretch(int32_t alignment) {
    return alignment == CSS_VALUE_STRETCH;
}

int32_t resolve_align_self(int32_t align_self, int32_t align_items) {
    // auto resolves to parent's align-items
    if (align_self == CSS_VALUE_AUTO || align_self == CSS_VALUE__UNDEF) {
        return align_items == CSS_VALUE_NORMAL ? CSS_VALUE_STRETCH : align_items;
    }
    if (align_self == CSS_VALUE_NORMAL) return CSS_VALUE_STRETCH;
    return align_self;
}

int32_t resolve_justify_self(int32_t justify_self, int32_t justify_items) {
    // auto resolves to parent's justify-items
    if (justify_self == CSS_VALUE_AUTO || justify_self == CSS_VALUE__UNDEF) {
        return justify_items == CSS_VALUE_NORMAL ? CSS_VALUE_STRETCH : justify_items;
    }
    if (justify_self == CSS_VALUE_NORMAL) return CSS_VALUE_STRETCH;
    return justify_self;
}

// ============================================================================
// Baseline Calculation
// ============================================================================

float compute_font_baseline_ascender(
    ::LayoutContext* lycon,
    FontProp* font,
    bool use_normal_line_height,
    float fallback_ascender
) {
    if (!font) return fallback_ascender;

    if (!lycon) {
        if (font->ascender > 0.0f) return font->ascender;
        if (font->font_size > 0.0f) return font->font_size * 0.8f;
        return fallback_ascender;
    }

    FontBox fbox = {};
    setup_font(lycon->ui_context, &fbox, font);
    if (!fbox.font_handle) {
        if (font->ascender > 0.0f) return font->ascender;
        if (font->font_size > 0.0f) return font->font_size * 0.8f;
        return fallback_ascender;
    }

    if (use_normal_line_height) {
        float split_asc = 0.0f;
        float split_desc = 0.0f;
        font_get_normal_lh_split(fbox.font_handle, &split_asc, &split_desc);
        return split_asc;
    }

    TypoMetrics typo = get_os2_typo_metrics(fbox.font_handle);
    if (typo.valid && typo.use_typo_metrics) {
        return typo.ascender;
    }

    const FontMetrics* metrics = font_get_metrics(fbox.font_handle);
    if (metrics) return metrics->hhea_ascender;

    if (font->ascender > 0.0f) return font->ascender;
    if (font->font_size > 0.0f) return font->font_size * 0.8f;
    return fallback_ascender;
}

float compute_element_first_baseline(
    ::LayoutContext* lycon,
    ViewBlock* element,
    bool is_row_direction
) {
    if (!element) return -1.0f;
    (void)lycon;
    (void)is_row_direction;

    // CSS 2.1 §10.8.1: use the first in-flow child baseline when one exists.
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        ViewBlock* child_block = lam::unsafe_view_block_element_storage(child->as_element());
        float child_baseline = compute_element_first_baseline(lycon, child_block, is_row_direction);
        if (child_baseline >= 0) {
            return child_block->y + child_baseline;
        }
    }

    bool has_text_content = false;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (layout_text_node_has_content(child)) {
            has_text_content = true;
            break;
        }
    }
    if (has_text_content && element->font) {
        BoxMetrics box = layout_box_metrics(element);
        float fallback = element->font->font_size * 0.8f;
        return box.padding.top + box.border.top +
            compute_font_baseline_ascender(lycon, element->font, false, fallback);
    }

    // No child with baseline: synthesize from the bottom border edge.
    return element->height;
}

float compute_view_first_text_baseline(
    ::LayoutContext* lycon,
    View* parent,
    float cumulative_y,
    bool use_normal_line_height,
    bool skip_block_children_of_table,
    FirstBaselineRowCallback row_baseline
) {
    if (!parent) return -1.0f;

    for (View* child = lam::view_require_element(parent)->first_child; child; child = child->next_sibling) {
        if (!child->view_type) continue;

        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            float fallback_ascent = text->font ? text->font->font_size * 0.8f : 0.0f;
            float ascent = compute_font_baseline_ascender(
                lycon, text->font, use_normal_line_height, fallback_ascent);
            float baseline = cumulative_y + child->y + ascent;
            log_debug("compute_view_first_text_baseline: text cumulative_y=%.1f, text.y=%.1f, ascent=%.1f -> baseline=%.1f",
                      cumulative_y, child->y, ascent, baseline);
            return baseline;
        }

        bool is_table_structure = (child->view_type == RDT_VIEW_TABLE ||
                                   child->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
                                   child->view_type == RDT_VIEW_TABLE_ROW ||
                                   child->view_type == RDT_VIEW_TABLE_CELL);
        bool is_block_like = (child->view_type == RDT_VIEW_BLOCK ||
                              child->view_type == RDT_VIEW_INLINE ||
                              child->view_type == RDT_VIEW_INLINE_BLOCK ||
                              child->view_type == RDT_VIEW_LIST_ITEM);

        if (is_table_structure) {
            float result = compute_view_first_text_baseline(
                lycon, child, cumulative_y + child->y,
                use_normal_line_height, skip_block_children_of_table,
                row_baseline);
            if (result >= 0) return result;
            if (child->view_type == RDT_VIEW_TABLE_ROW && row_baseline) {
                float row_result = row_baseline(lycon, child);
                if (row_result >= 0) return cumulative_y + child->y + row_result;
            }
        } else if (is_block_like) {
            bool parent_is_table = parent->view_type == RDT_VIEW_TABLE;
            if (!skip_block_children_of_table || !parent_is_table) {
                float result = compute_view_first_text_baseline(
                    lycon, child, cumulative_y + child->y,
                    use_normal_line_height, skip_block_children_of_table,
                    row_baseline);
                if (result >= 0) return result;
            }
        }
    }
    return -1.0f;
}

float compute_element_last_baseline(
    ::LayoutContext* lycon,
    ViewBlock* element,
    bool is_row_direction
) {
    // TODO: Implement proper last baseline calculation
    return -1.0f;  // No baseline
}

// ============================================================================
// Cross-axis Size Resolution (Stretch)
// ============================================================================

float compute_stretched_cross_size(
    float item_cross_size,
    float line_cross_size,
    float margin_cross,
    float min_cross,
    float max_cross,
    bool has_definite_size
) {
    // If item has definite cross size, don't stretch
    if (has_definite_size) {
        return item_cross_size;
    }

    // Stretch to fill line (minus margins)
    float available_cross = line_cross_size - margin_cross;
    if (available_cross < 0) available_cross = 0;

    // Apply min/max constraints
    float stretched = available_cross;
    if (min_cross > 0 && stretched < min_cross) {
        stretched = min_cross;
    }
    if (max_cross > 0 && stretched > max_cross) {
        stretched = max_cross;
    }

    return stretched;
}

} // namespace radiant
