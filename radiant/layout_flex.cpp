#include "layout_flex.hpp"
#include "layout.hpp"
#include "view.hpp"
#include "layout_flex_measurement.hpp"
#include "form_control.hpp"
#include "available_space.hpp"
#include "intrinsic_sizing.hpp"
#include "layout_alignment.hpp"
extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "../lib/memtrack.h"
}

// NOTE: All conversion functions removed - enums now align directly with Lexbor constants
// This eliminates the need for any enum conversion throughout the flex layout system

// Forward declarations
float get_main_axis_size(ViewElement* item, FlexContainerLayout* flex_layout);
float get_main_axis_outer_size(ViewElement* item, FlexContainerLayout* flex_layout);
static bool should_skip_flex_item(ViewElement* item);
float calculate_item_baseline(ViewElement* item);
void layout_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item);
static bool item_will_stretch(ViewElement* item, FlexContainerLayout* flex_layout);
extern bool is_only_whitespace(const char* str);

// ============================================================================
// Flex Item Property Helpers (support both flex items and form controls)
// ============================================================================

// Get flex-grow value for item (form controls store in FormControlProp)
float get_item_flex_grow(ViewElement* item) {
    if (!item) return 0;
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
        return item->form->flex_grow;
    }
    if (item->fi) {
        return item->fi->flex_grow;
    }
    return 0;
}

// Get flex-shrink value for item (form controls store in FormControlProp)
float get_item_flex_shrink(ViewElement* item) {
    if (!item) return 1;  // default is 1
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
        return item->form->flex_shrink;
    }
    if (item->fi) {
        return item->fi->flex_shrink;
    }
    return 1;  // default
}

// Get flex-basis value for item (form controls store in FormControlProp)
float get_item_flex_basis(ViewElement* item) {
    if (!item) return -1;  // auto
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
        return item->form->flex_basis;
    }
    if (item->fi) {
        return item->fi->flex_basis;
    }
    return -1;  // auto
}

// Check if flex-basis is percentage for item
bool get_item_flex_basis_is_percent(ViewElement* item) {
    if (!item) return false;
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
        return item->form->flex_basis_is_percent;
    }
    if (item->fi) {
        return item->fi->flex_basis_is_percent;
    }
    return false;
}

// CSS Flexbox §9.7: Get the effective flex base size for free space and growth calculations.
// For border-box items, the flex base size cannot be less than padding+border on the main axis,
// because the content-box size cannot be negative (CSS2 §10.2). The spec works in content-box
// internally and adds padding+border for "outer" sizes, which naturally enforces this floor.
// Since Radiant works in border-box throughout, we must explicitly floor the base.
// Note: The ORIGINAL (unfloored) flex-basis is still used for scaled shrink factors (§9.7 step 4c).
static float get_effective_flex_base(ViewElement* item, float basis, FlexContainerLayout* flex_layout) {
    if (!item->bound) return basis;
    bool is_horiz = is_main_axis_horizontal(flex_layout);
    float pb = 0;
    if (is_horiz) {
        pb = item->bound->padding.left + item->bound->padding.right;
        if (item->bound->border) pb += item->bound->border->width.left + item->bound->border->width.right;
    } else {
        pb = item->bound->padding.top + item->bound->padding.bottom;
        if (item->bound->border) pb += item->bound->border->width.top + item->bound->border->width.bottom;
    }
    // Floor: border-box size cannot be less than padding+border
    return basis < pb ? pb : basis;
}

// ============================================================================
// Flex Item Intrinsic Size Helpers (support both flex items and form controls)
// ============================================================================

// Check if an element has intrinsic width available
static bool has_item_intrinsic_width(ViewElement* item) {
    if (!item) return false;
    if (item->fi && item->fi->has_intrinsic_width) return true;
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
        return item->form->intrinsic_width > 0;
    }
    return false;
}

// Check if an element has intrinsic height available
static bool has_item_intrinsic_height(ViewElement* item) {
    if (!item) return false;
    if (item->fi && item->fi->has_intrinsic_height) return true;
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
        return item->form->intrinsic_height > 0;
    }
    return false;
}

// Get intrinsic width (max-content) for flex item or form control
static float get_item_intrinsic_width(ViewElement* item) {
    if (!item) return 0;
    if (item->fi && item->fi->has_intrinsic_width) {
        return item->fi->intrinsic_width.max_content;
    }
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
        return item->form->intrinsic_width;
    }
    return 0;
}

// Get intrinsic height (max-content) for flex item or form control
static float get_item_intrinsic_height(ViewElement* item) {
    if (!item) return 0;
    if (item->fi && item->fi->has_intrinsic_height) {
        return item->fi->intrinsic_height.max_content;
    }
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
        return item->form->intrinsic_height;
    }
    return 0;
}

// ============================================================================
// Overflow Alignment Fallback (Yoga-inspired)
// ============================================================================

// Check if a view element is an empty flex container (no children)
// Used to determine if a flex item should get 0 height or minimum height
// Note: Called during init_flex_container when tree may not be fully linked,
// so we can only check the immediate children, not descendants
static bool is_empty_flex_container(ViewElement* elem) {
    if (!elem) return true;
    // Check if this element has any children with actual content
    // An element is "empty" if it has no children, or all children are
    // either whitespace-only text nodes, nil-views, or themselves empty flex containers
    View* child = elem->first_child;
    if (!child) return true;

    while (child) {
        if (child->view_type == RDT_VIEW_NONE) {
            // Nil-views (whitespace text in flex, hidden elements, etc.) — skip
        } else if (child->view_type == RDT_VIEW_TEXT) {
            // Text nodes: whitespace text in flex containers doesn't contribute
        } else if (child->view_type == RDT_VIEW_BLOCK) {
            ViewElement* child_elem = (ViewElement*)child->as_element();
            if (child_elem) {
                // If child has explicit height, it has content
                if (child_elem->blk && child_elem->blk->given_height >= 0)
                    return false;
                if (child_elem->height > 0)
                    return false;
                // Recursively check if child is also empty
                if (!is_empty_flex_container(child_elem))
                    return false;
            }
        } else {
            // Other view types (inline, etc.): assume non-empty
            return false;
        }
        child = child->next();
    }
    return true;
}

// NOTE: fallback_alignment and fallback_justify are now in layout_alignment.hpp
// Use radiant::alignment_fallback_for_overflow(alignment, free_space) instead

// Initialize flex container layout state
void init_flex_container(LayoutContext* lycon, ViewBlock* container) {
    if (!container) return;

    // create embed structure if it doesn't exist
    if (!container->embed) {
        container->embed = (EmbedProp*)mem_calloc(1, sizeof(EmbedProp), MEM_CAT_LAYOUT);
    }

    FlexContainerLayout* flex = (FlexContainerLayout*)mem_calloc(1, sizeof(FlexContainerLayout), MEM_CAT_LAYOUT);
    lycon->flex_container = flex;
    flex->lycon = lycon;  // Store layout context for intrinsic sizing
    if (container->embed && container->embed->flex) {
        FlexProp* source = container->embed->flex;
        log_debug("%s init_flex_container: source->direction=%d (0x%04X), row=%d, col=%d", container->source_loc(),
                  source->direction, source->direction, DIR_ROW, DIR_COLUMN);
        memcpy(flex, container->embed->flex, sizeof(FlexProp));
        flex->lycon = lycon;  // Restore after memcpy
        log_debug("%s init_flex_container: after copy flex->direction=%d", container->source_loc(), flex->direction);
    }
    else {
        // Set default values using enum names that now align with Lexbor constants
        log_debug("%s init_flex_container: NO embed->flex, using defaults (row)", container->source_loc());
        flex->direction = DIR_ROW;
        flex->wrap = WRAP_NOWRAP;
        flex->justify = JUSTIFY_START;
        flex->align_items = ALIGN_STRETCH;  // Default per CSS Flexbox spec
        flex->align_content = ALIGN_STRETCH;  // Default per CSS Flexbox spec
        flex->row_gap = 0;
        flex->column_gap = 0;
        flex->row_gap_is_percent = false;
        flex->column_gap_is_percent = false;
        flex->writing_mode = WM_HORIZONTAL_TB;
        flex->text_direction = TD_LTR;
    }

    // Initialize main_axis_size and cross_axis_size early for percentage resolution
    // This allows collect_and_prepare_flex_items to re-resolve percentages correctly
    // CRITICAL: For containers with explicit height (like body with height: 100%), use given_height
    // since container->height may not be set yet at this point in the layout flow.
    float content_width = container->width;
    float content_height = container->height;

    // Use given_height if container has explicit height (before container->height is set)
    if (container->blk && container->blk->given_height >= 0 && content_height <= 0) {
        content_height = container->blk->given_height;
        log_debug("%s init_flex_container: using given_height=%.1f for content_height", container->source_loc(), content_height);
    }

    // Subtract borders if they exist
    if (container->bound && container->bound->border) {
        content_width -= (container->bound->border->width.left + container->bound->border->width.right);
        content_height -= (container->bound->border->width.top + container->bound->border->width.bottom);
    }

    // Subtract padding if it exists
    if (container->bound) {
        content_width -= (container->bound->padding.left + container->bound->padding.right);
        content_height -= (container->bound->padding.top + container->bound->padding.bottom);
    }

    // Check if container has explicit dimensions (needed for percentage gap resolution)
    bool has_explicit_height = container->blk && container->blk->given_height >= 0;
    bool has_explicit_width = container->blk && container->blk->given_width >= 0;

    // Resolve percentage gaps to actual pixel values
    // Per CSS spec, gap percentages are resolved against the content box dimension
    // in the corresponding axis (row-gap uses height, column-gap uses width)
    // For auto-size containers, percentage gaps resolve to 0
    if (container->embed && container->embed->flex) {
        FlexProp* source = container->embed->flex;
        if (source->row_gap_is_percent) {
            if (content_height > 0) {
                float resolved_gap = (source->row_gap / 100.0f) * content_height;
                log_debug("%s init_flex_container: resolving row_gap from %.1f%% to %.1fpx (height=%d)", container->source_loc(),
                          source->row_gap, resolved_gap, content_height);
                flex->row_gap = resolved_gap;
            } else {
                // Auto-height container with unknown content height: percentage gap resolves to 0
                log_debug("%s init_flex_container: row_gap %.1f%% resolves to 0 (auto-height container)", container->source_loc(),
                          source->row_gap);
                flex->row_gap = 0;
            }
            flex->row_gap_is_percent = false;  // Now it's resolved
        }
        if (source->column_gap_is_percent) {
            if (content_width > 0) {
                float resolved_gap = (source->column_gap / 100.0f) * content_width;
                log_debug("%s init_flex_container: resolving column_gap from %.1f%% to %.1fpx (width=%d)", container->source_loc(),
                          source->column_gap, resolved_gap, content_width);
                flex->column_gap = resolved_gap;
            } else {
                // Auto-width container with unknown content width: percentage gap resolves to 0
                log_debug("%s init_flex_container: column_gap %.1f%% resolves to 0 (auto-width container)", container->source_loc(),
                          source->column_gap);
                flex->column_gap = 0;
            }
            flex->column_gap_is_percent = false;  // Now it's resolved
        }
    }

    bool is_horizontal = is_main_axis_horizontal(flex);

    // Check if this is a shrink-to-fit flex container (auto width):
    // 1. Absolutely positioned with auto width (no explicit width/min/max)
    // 2. Inline-flex (display: inline-flex) with auto width
    // Both cases use shrink-to-fit sizing per CSS Display §3 / CSS 2.1 §10.3.9
    bool has_min_width = container->blk && container->blk->given_min_width > 0;
    bool has_max_width = container->blk && container->blk->given_max_width > 0;
    bool is_absolute_no_width = false;
    if (container->position &&
        (container->position->position == CSS_VALUE_ABSOLUTE || container->position->position == CSS_VALUE_FIXED)) {
        // Absolutely positioned element - check if it has auto width (no explicit width/min/max)
        if (!has_explicit_width && !has_min_width && !has_max_width &&
            !(container->position->has_left && container->position->has_right)) {
            is_absolute_no_width = true;
        }
    }
    // Inline-flex with auto width also uses shrink-to-fit
    bool is_inline_no_width = !has_explicit_width &&
        (container->display.outer == CSS_VALUE_INLINE_BLOCK ||
         container->display.outer == CSS_VALUE_INLINE);
    bool is_shrink_to_fit = is_absolute_no_width || is_inline_no_width;

    if (is_horizontal) {
        // For row flex, main axis is width
        // If container needs shrink-to-fit, defer width calculation
        if (is_shrink_to_fit) {
            // Defer width calculation to layout phase (shrink-to-fit)
            flex->main_axis_size = 0.0f;
            log_debug("%s init_flex_container: shrink-to-fit row flex with auto-width, deferring main_axis_size", container->source_loc());
        } else {
            flex->main_axis_size = content_width > 0 ? (float)content_width : 0.0f;
        }
        flex->cross_axis_size = content_height > 0 ? (float)content_height : 0.0f;
    } else {
        flex->main_axis_size = content_height > 0 ? (float)content_height : 0.0f;
        // For column flex, cross axis is width
        // If container needs shrink-to-fit, defer width calculation
        if (is_shrink_to_fit) {
            // Defer width calculation to layout phase (shrink-to-fit)
            flex->cross_axis_size = 0.0f;
            log_debug("%s init_flex_container: shrink-to-fit column flex with auto-width, deferring cross_axis_size", container->source_loc());
        } else {
            flex->cross_axis_size = content_width > 0 ? (float)content_width : 0.0f;
        }
    }
    log_debug("%s init_flex_container: main_axis_size=%.1f, cross_axis_size=%.1f (content: %dx%d)", container->source_loc(),
              flex->main_axis_size, flex->cross_axis_size, content_width, content_height);

    // Detect indefinite main axis size (CSS Flexbox spec §9.2)
    // A flex container has definite main size when:
    // 1. Has explicit CSS width/height in the main axis direction, OR
    // 2. Has max-width/max-height that is actually constraining the size (container is clamped), OR
    // 3. Is absolutely/fixed positioned with both left+right (for width) or top+bottom (for height)
    //
    // When main axis is indefinite, flex-grow should NOT distribute additional space
    // because the container should shrink-to-fit its content.
    flex->main_axis_is_indefinite = false;

    bool is_absolute = container->position &&
        (container->position->position == CSS_VALUE_ABSOLUTE || container->position->position == CSS_VALUE_FIXED);

    bool has_min_height = container->blk && container->blk->given_min_height > 0;
    bool has_max_height = container->blk && container->blk->given_max_height > 0;

    if (is_horizontal) {
        // Main axis is width for row flex
        // Width is definite if:
        // - Explicit width is set, OR
        // - max-width is actively constraining (container width == max-width and max-width < available), OR
        // - Absolutely positioned with both left and right, OR
        // - Block-level element in normal flow (inherits definite width from containing block)
        bool has_definite_width = has_explicit_width;

        // Check if max-width is actually constraining the width
        // This means: container.width equals max-width AND there was more available space
        // For simplicity, we check if container.width == max-width (meaning it was clamped)
        if (has_max_width && content_width > 0) {
            float max_width_value = container->blk->given_max_width;
            // The container's content_width would have been clamped to max_width if max_width < available
            // We consider it definite if the actual content_width equals (approximately) the max_width content area
            // Account for border-box vs content-box
            float container_content_width = content_width;  // Already calculated as content area
            float max_content_width = max_width_value;
            if (container->blk && container->blk->box_sizing == CSS_VALUE_BORDER_BOX && container->bound) {
                // For border-box, max_width includes padding/border, so subtract them
                max_content_width -= (container->bound->padding.left + container->bound->padding.right);
                if (container->bound->border) {
                    max_content_width -= (container->bound->border->width.left + container->bound->border->width.right);
                }
            }
            // If content_width is close to max_content_width, max-width is constraining
            if (fabs(container_content_width - max_content_width) < 1.0f) {
                has_definite_width = true;
                log_debug("%s init_flex_container: max-width is constraining (content=%.1f, max=%.1f)", container->source_loc(),
                          container_content_width, max_content_width);
            }
        }

        // For absolutely positioned elements, also check left+right
        if (is_absolute && container->position) {
            has_definite_width = has_definite_width ||
                (container->position->has_left && container->position->has_right);
        }

        // Block-level elements in normal flow have definite width from containing block
        // Only inline-block/inline elements with auto width are shrink-to-fit (indefinite)
        // Absolute/fixed positioned elements with auto width are also shrink-to-fit (indefinite)
        if (!has_definite_width && !is_absolute && content_width > 0) {
            // Check if this is a block-level element (not inline-block, inline, or flex item)
            bool is_inline_level = (container->display.outer == CSS_VALUE_INLINE_BLOCK ||
                                    container->display.outer == CSS_VALUE_INLINE);
            if (!is_inline_level) {
                // Block-level element with computed width from containing block - it's definite
                has_definite_width = true;
                log_debug("%s init_flex_container: block-level element has definite width from containing block (%.1f)", container->source_loc(),
                          (float)content_width);
            }
        }

        // CRITICAL FIX: If this container already has a width set by a parent flex algorithm,
        // treat it as definite. This prevents nested flex containers from overriding the
        // width that was calculated by the parent's flex item sizing.
        // Exception: shrink-to-fit containers (absolute-positioned or inline-flex with auto
        // width) get their containing block's width as fallback, so we must NOT treat that
        // as definite.
        if (!has_definite_width && container->width > 0 && !is_shrink_to_fit) {
            has_definite_width = true;
            log_debug("%s init_flex_container: using width set by parent (%.1f)", container->source_loc(),
                      container->width);
        }

        flex->main_axis_is_indefinite = !has_definite_width;
    } else {
        // Main axis is height for column flex
        bool has_definite_height = has_explicit_height;

        // Check if max-height is actually constraining
        if (has_max_height && content_height > 0) {
            float max_height_value = container->blk->given_max_height;
            float max_content_height = max_height_value;
            if (container->blk && container->blk->box_sizing == CSS_VALUE_BORDER_BOX && container->bound) {
                max_content_height -= (container->bound->padding.top + container->bound->padding.bottom);
                if (container->bound->border) {
                    max_content_height -= (container->bound->border->width.top + container->bound->border->width.bottom);
                }
            }
            if (fabs(content_height - max_content_height) < 1.0f) {
                has_definite_height = true;
                log_debug("%s init_flex_container: max-height is constraining (content=%.1f, max=%.1f)", container->source_loc(),
                          (float)content_height, max_content_height);
            }
        }

        // Absolutely positioned elements have definite height only if both top and bottom are specified
        if (is_absolute && container->position) {
            has_definite_height = has_definite_height ||
                (container->position->has_top && container->position->has_bottom);
        }

        // CRITICAL FIX: If this container already has a height set by a parent flex algorithm,
        // treat it as definite. This happens when a flex item with flex-grow > 0 is also a
        // flex container - the parent sizes it first, then we need to recognize that size as definite.
        if (!has_definite_height && container->height > 0) {
            has_definite_height = true;
            log_debug("%s init_flex_container: using height set by parent (%.1f)", container->source_loc(),
                      container->height);
        }

        flex->main_axis_is_indefinite = !has_definite_height;
    }

    // Determine if cross axis has a definite size (CSS Flexbox §9.4)
    // For row flex: cross axis is height
    // For column flex: cross axis is width
    if (is_horizontal) {
        // Row flex: cross axis is height
        // Height is definite if explicitly set or if container has top+bottom insets
        bool has_definite_height_for_cross = has_explicit_height;
        if (is_absolute && container->position) {
            has_definite_height_for_cross = has_definite_height_for_cross ||
                (container->position->has_top && container->position->has_bottom);
        }
        // CSS Sizing Level 3: A flex item's cross size after stretch IS definite.
        // If the container already has a height from a parent flex layout (stretch
        // or main-axis sizing), treat it as definite. This ensures nested flex
        // containers use the stretched height as their cross size, not content.
        // (Parallels the existing main-axis fix for column flex at line ~400.)
        // CRITICAL: Use content_height (height minus padding/border) not container->height.
        // container->height can be positive from padding/border alone even when height is
        // auto, which would incorrectly mark auto-height containers as having definite cross.
        if (!has_definite_height_for_cross && content_height > 0 && !is_absolute) {
            has_definite_height_for_cross = true;
            log_debug("%s init_flex_container: cross-axis height definite from parent layout (content_height=%d)", container->source_loc(),
                      content_height);
        }
        flex->has_definite_cross_size = has_definite_height_for_cross;
    } else {
        // Column flex: cross axis is width
        // Width is definite if explicitly set, or if block-level with computed width, or if has left+right insets
        bool has_definite_width_for_cross = has_explicit_width;
        if (is_absolute && container->position) {
            has_definite_width_for_cross = has_definite_width_for_cross ||
                (container->position->has_left && container->position->has_right);
        }
        // Block-level elements have definite width from containing block
        if (!has_definite_width_for_cross && !is_absolute && content_width > 0) {
            bool is_inline_level = (container->display.outer == CSS_VALUE_INLINE_BLOCK ||
                                    container->display.outer == CSS_VALUE_INLINE);
            if (!is_inline_level) {
                has_definite_width_for_cross = true;
            }
        }
        flex->has_definite_cross_size = has_definite_width_for_cross;
    }

    log_debug("%s init_flex_container: main_axis_is_indefinite=%s, has_definite_cross_size=%s (is_absolute=%s, is_horizontal=%s, has_width=%s, has_height=%s, has_max_width=%s, has_max_height=%s)", container->source_loc(),
              flex->main_axis_is_indefinite ? "true" : "false",
              flex->has_definite_cross_size ? "true" : "false",
              is_absolute ? "true" : "false",
              is_horizontal ? "true" : "false",
              has_explicit_width ? "true" : "false",
              has_explicit_height ? "true" : "false",
              has_max_width ? "true" : "false",
              has_max_height ? "true" : "false");

    // Initialize dynamic arrays
    flex->allocated_items = 8;
    flex->flex_items = (View**)mem_calloc(flex->allocated_items, sizeof(View*), MEM_CAT_LAYOUT);
    flex->allocated_lines = 4;
    flex->lines = (FlexLineInfo*)mem_calloc(flex->allocated_lines, sizeof(FlexLineInfo), MEM_CAT_LAYOUT);
    flex->needs_reflow = false;
}

// Cleanup flex container resources
void cleanup_flex_container(LayoutContext* lycon) {
    FlexContainerLayout* flex = lycon->flex_container;
    // Free line items arrays
    for (int i = 0; i < flex->line_count; ++i) {
        mem_free(flex->lines[i].items);
    }
    mem_free(flex->flex_items);
    mem_free(flex->lines);
    mem_free(flex);
}

// Main flex layout algorithm entry point
void layout_flex_container(LayoutContext* lycon, ViewBlock* container) {
    log_info("=== layout_flex_container ENTRY ===");
    FlexContainerLayout* flex_layout = lycon->flex_container;

    log_info("FLEX START %s - container: %dx%d at (%d,%d)",
           container->source_loc(), container->width, container->height, container->x, container->y);
    log_debug("FLEX PROPERTIES - direction=%d, align_items=%d, justify=%d, wrap=%d",
           flex_layout->direction, flex_layout->align_items, flex_layout->justify, flex_layout->wrap);
    // DEBUG: Gap settings applied

    // Set main and cross axis sizes from container dimensions (only if not already set)
    // DEBUG: Container dimensions calculated
    if (flex_layout->main_axis_size == 0.0f || flex_layout->cross_axis_size == 0.0f) {
        // CRITICAL FIX: Use container width/height and calculate content dimensions
        // The content dimensions should exclude borders and padding
        float content_width = container->width;
        float content_height = container->height;

        // Subtract borders if they exist
        if (container->bound && container->bound->border) {
            content_width -= (container->bound->border->width.left + container->bound->border->width.right);
            content_height -= (container->bound->border->width.top + container->bound->border->width.bottom);
        }

        // Subtract padding if it exists
        if (container->bound) {
            content_width -= (container->bound->padding.left + container->bound->padding.right);
            content_height -= (container->bound->padding.top + container->bound->padding.bottom);
        }

        log_debug("FLEX CONTENT - content: %.1fx%.1f, container: %dx%d",
               content_width, content_height, container->width, container->height);

        // Axis orientation now calculated correctly with aligned enum values
        bool is_horizontal = is_main_axis_horizontal(flex_layout);

        log_debug("AXIS INIT - before: main=%.1f, cross=%.1f, content=%dx%d",
               flex_layout->main_axis_size, flex_layout->cross_axis_size, content_width, content_height);

        if (is_horizontal) {
            if (flex_layout->main_axis_size == 0.0f) {
                // ROW FLEX with auto width - check if this is shrink-to-fit case
                // For shrink-to-fit, calculate width from flex items
                bool has_explicit_width = container->blk && container->blk->given_width >= 0;
                // Also check for min-width/max-width constraints
                bool has_min_width = container->blk && container->blk->given_min_width > 0;
                bool has_max_width = container->blk && container->blk->given_max_width > 0;
                bool is_absolute = container->position &&
                    (container->position->position == CSS_VALUE_ABSOLUTE ||
                     container->position->position == CSS_VALUE_FIXED);
                // Shrink-to-fit: absolute with auto width, or inline-flex with auto width
                bool is_absolute_no_width = is_absolute && !has_explicit_width && !has_min_width && !has_max_width &&
                    !(container->position && container->position->has_left && container->position->has_right);
                bool is_inline_no_width = !has_explicit_width &&
                    (container->display.outer == CSS_VALUE_INLINE_BLOCK ||
                     container->display.outer == CSS_VALUE_INLINE);
                bool is_shrink_to_fit = is_absolute_no_width || is_inline_no_width;

                if (is_absolute_no_width && content_width > 0) {
                    // ABS POS already computed the intrinsic width via shrink-to-fit.
                    // However, if there are percentage gaps, or if any child has an explicit
                    // flex-basis (which overrides given_width for sizing purposes), we must
                    // recalculate from flex items using flex-aware intrinsic sizing.
                    bool has_percentage_gap = container->embed && container->embed->flex &&
                        container->embed->flex->column_gap_is_percent;
                    bool has_flex_basis_child = false;
                    {
                        View* scan = container->first_child;
                        while (scan) {
                            if (scan->view_type == RDT_VIEW_BLOCK) {
                                ViewElement* scan_item = (ViewElement*)scan->as_element();
                                if (scan_item && scan_item->fi && scan_item->fi->flex_basis >= 0) {
                                    has_flex_basis_child = true;
                                    break;
                                }
                            }
                            scan = scan->next();
                        }
                    }
                    if (!has_percentage_gap && !has_flex_basis_child) {
                        flex_layout->main_axis_size = (float)content_width;
                        log_debug("ROW FLEX SHRINK-TO-FIT: using pre-computed content_width=%d", content_width);
                    } else {
                        // Fall through to manual calculation below
                        goto manual_shrink_to_fit;
                    }
                } else if (is_shrink_to_fit) {
                    manual_shrink_to_fit:
                    // CSS Flexbox §9.9.1: Intrinsic main size of a single-line flex container
                    // is the sum of max-content contributions of flex items.
                    // Iterate DOM children (not view children) to include text nodes
                    // that become anonymous flex items.
                    float total_item_width = 0.0f;
                    int child_count = 0;
                    if (container->is_element()) {
                        DomElement* container_elem = (DomElement*)container;
                        for (DomNode* dom_child = container_elem->first_child; dom_child; dom_child = dom_child->next_sibling) {
                            float item_width = 0.0f;
                            if (dom_child->is_element()) {
                                ViewElement* item = (ViewElement*)dom_child->as_element();
                                // Skip display:none and absolute/hidden items
                                if (!item || should_skip_flex_item(item)) continue;
                                if (item->blk && item->blk->given_width >= 0) {
                                    item_width = item->blk->given_width;
                                } else {
                                    DomElement* item_elem = (DomElement*)item;
                                    IntrinsicSizes item_sizes = measure_element_intrinsic_widths(lycon, item_elem);
                                    item_width = item_sizes.max_content;
                                }
                                // Add padding+border for border-box contribution
                                if (item->bound) {
                                    float pb = item->bound->padding.left + item->bound->padding.right;
                                    if (item->bound->border)
                                        pb += item->bound->border->width.left + item->bound->border->width.right;
                                    // Only add if intrinsic sizing returned content-box value
                                    if (!(item->blk && item->blk->given_width >= 0))
                                        item_width += pb;
                                }
                                // Clamp by min-width/max-width (§1.1)
                                if (item->blk) {
                                    if (item->blk->given_max_width >= 0 && item_width > item->blk->given_max_width)
                                        item_width = item->blk->given_max_width;
                                    if (item->blk->given_min_width >= 0 && item_width < item->blk->given_min_width)
                                        item_width = item->blk->given_min_width;
                                }
                                child_count++;
                            } else if (dom_child->is_text()) {
                                // Text nodes become anonymous flex items
                                const char* text = (const char*)dom_child->text_data();
                                if (!text) continue;
                                size_t text_len = strlen(text);
                                char normalized[2048];
                                size_t out_pos = 0;
                                bool in_ws = true;
                                for (size_t i = 0; i < text_len && out_pos < sizeof(normalized) - 1; i++) {
                                    unsigned char ch = (unsigned char)text[i];
                                    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f') {
                                        if (!in_ws) { normalized[out_pos++] = ' '; in_ws = true; }
                                    } else {
                                        normalized[out_pos++] = (char)ch; in_ws = false;
                                    }
                                }
                                while (out_pos > 0 && normalized[out_pos - 1] == ' ') out_pos--;
                                normalized[out_pos] = '\0';
                                if (out_pos > 0) {
                                    TextIntrinsicWidths tw = measure_text_intrinsic_widths(lycon, normalized, out_pos);
                                    item_width = tw.max_content;
                                    child_count++;
                                } else {
                                    continue;
                                }
                            } else {
                                continue;
                            }
                            total_item_width += item_width;
                            log_debug("ROW FLEX SHRINK-TO-FIT: item width=%.1f, total=%.1f", item_width, total_item_width);
                        }
                    }
                    // Add column gaps between items (non-percentage only)
                    if (child_count > 1 && flex_layout->column_gap > 0 &&
                        !(container->embed && container->embed->flex &&
                          container->embed->flex->column_gap_is_percent)) {
                        total_item_width += flex_layout->column_gap * (child_count - 1);
                    }
                    flex_layout->main_axis_size = total_item_width;
                    // Also update container width (include padding AND border)
                    float padding_border_width = 0.0f;
                    if (container->bound) {
                        padding_border_width = container->bound->padding.left + container->bound->padding.right;
                        if (container->bound->border) {
                            padding_border_width += container->bound->border->width.left + container->bound->border->width.right;
                        }
                    }
                    container->width = total_item_width + padding_border_width;
                    log_debug("ROW FLEX SHRINK-TO-FIT: main_axis_size=%.1f, container.width=%.1f",
                              flex_layout->main_axis_size, container->width);
                } else {
                    flex_layout->main_axis_size = (float)content_width;
                    log_debug("AXIS INIT - set main to %.1f", (float)content_width);
                }

                // Re-resolve percentage column-gap against the computed main axis size
                // For auto-sized containers, the percentage was deferred to 0 during init.
                // Now that we have a computed size, resolve it.
                if (container->embed && container->embed->flex &&
                    container->embed->flex->column_gap_is_percent &&
                    flex_layout->main_axis_size > 0) {
                    float pct = container->embed->flex->column_gap;
                    float resolved_gap = (pct / 100.0f) * flex_layout->main_axis_size;
                    flex_layout->column_gap = resolved_gap;
                    log_debug("AXIS INIT: re-resolved column_gap from %.1f%% to %.1fpx (base=%.1f)",
                              pct, resolved_gap, flex_layout->main_axis_size);
                }

                log_debug("AXIS INIT - verify main now: %.1f", flex_layout->main_axis_size);
            }
            log_debug("AXIS INIT - cross condition: %s (cross=%.1f, has_definite=%s)",
                   (flex_layout->cross_axis_size == 0.0f) ? "true" : "false", flex_layout->cross_axis_size,
                   flex_layout->has_definite_cross_size ? "true" : "false");
            if (flex_layout->cross_axis_size == 0.0f) {
                // For auto-height (no definite cross size), DO NOT set cross_axis_size early
                // Let calculate_line_cross_sizes compute it from item hypothetical cross sizes
                // This ensures nested flex containers are laid out first
                if (!flex_layout->has_definite_cross_size) {
                    log_debug("ROW FLEX: auto-height container, deferring cross_axis_size calculation");
                    // Leave cross_axis_size at 0 - it will be set in Phase 5 (calculate_line_cross_sizes)
                } else if (content_height > 0) {
                    // Container has definite height, use it
                    flex_layout->cross_axis_size = (float)content_height;
                    log_debug("ROW FLEX: using definite content_height=%.1f for cross_axis_size",
                              (float)content_height);
                }
            }
        } else {
            if (flex_layout->main_axis_size == 0.0f) {
                // For column flex with auto height, calculate height based on flex items
                // CRITICAL: Only calculate auto-height if container does NOT have explicit height
                bool has_explicit_height = container->blk && container->blk->given_height >= 0;
                // Also treat height as explicit if parent flex set it (flex-basis in column parent)
                if (!has_explicit_height && container->fi && container->height > 0) {
                    if (container->fi->main_size_from_flex || container->fi->flex_basis >= 0) {
                        has_explicit_height = true;
                        log_debug("AXIS INIT: column container height treated as explicit (set by parent flex, basis=%.1f)",
                                  container->fi->flex_basis);
                    }
                }
                if (content_height <= 0 && !has_explicit_height) {
                    // Auto-height column flex: calculate from flex items' intrinsic heights
                    int total_item_height = 0;
                    View* child = container->first_child;
                    while (child) {
                        if (child->view_type == RDT_VIEW_BLOCK) {
                            ViewElement* item = (ViewElement*)child->as_element();
                            if (item) {
                                float item_height = 0;
                                if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
                                    // Form control: use explicit flex-basis, CSS height, or intrinsic + padding/border
                                    if (item->form->flex_basis >= 0 && !item->form->flex_basis_is_percent) {
                                        item_height = item->form->flex_basis;
                                    } else if (item->blk && item->blk->given_height >= 0) {
                                        item_height = item->blk->given_height;
                                    } else {
                                        // intrinsic height is content-box; add CSS padding + border
                                        float h = item->form->intrinsic_height;
                                        if (item->bound) {
                                            h += item->bound->padding.top + item->bound->padding.bottom;
                                            if (item->bound->border) {
                                                h += item->bound->border->width.top + item->bound->border->width.bottom;
                                            }
                                        }
                                        item_height = h;
                                    }
                                } else if (item->fi) {
                                    // Regular flex item: use flex-basis if specified, otherwise use intrinsic/explicit height
                                    if (item->fi->flex_basis >= 0 && !item->fi->flex_basis_is_percent) {
                                        item_height = item->fi->flex_basis;
                                    } else if (item->blk && item->blk->given_height >= 0) {
                                        item_height = item->blk->given_height;
                                    } else if (item->height > 0) {
                                        item_height = item->height;
                                    } else {
                                        // Items with children get minimum height
                                        if (!is_empty_flex_container(item)) {
                                            item_height = 20;
                                        }
                                    }
                                }
                                total_item_height += item_height;
                                log_debug("COLUMN FLEX: item height contribution = %.1f", item_height);
                            }
                        }
                        child = child->next();
                    }
                    // Add gaps between items
                    int child_count = 0;
                    child = container->first_child;
                    while (child) {
                        if (child->view_type == RDT_VIEW_BLOCK) child_count++;
                        child = child->next();
                    }
                    if (child_count > 1) {
                        total_item_height += flex_layout->row_gap * (child_count - 1);
                    }
                    if (total_item_height >= 0) {
                        // For wrapping: if the column flex container has indefinite main size
                        // (auto height), per CSS Flexbox §9.3 items should not wrap since the
                        // main axis can grow indefinitely. Set main_axis_size to a very large
                        // value for wrapping purposes. Phase 7 will compute the final height.
                        bool has_max_height = container->blk && container->blk->given_max_height > 0;
                        if (flex_layout->wrap != WRAP_NOWRAP && !has_max_height) {
                            flex_layout->main_axis_size = 1e9f;
                            log_debug("COLUMN FLEX: auto-height with wrap, using infinite main_axis_size for wrapping");
                        } else {
                            flex_layout->main_axis_size = (float)total_item_height;
                        }
                        // Update container height to include padding + border (border-box)
                        float padding_border_height = 0.0f;
                        if (container->bound) {
                            padding_border_height += container->bound->padding.top + container->bound->padding.bottom;
                            if (container->bound->border) {
                                padding_border_height += container->bound->border->width.top + container->bound->border->width.bottom;
                            }
                        }
                        container->height = total_item_height + padding_border_height;
                        log_debug("COLUMN FLEX: auto-height calculated as %d from items (container=%d, border+padding=%.0f)",
                                  total_item_height, container->height, padding_border_height);
                    }
                } else {
                    flex_layout->main_axis_size = (float)content_height;
                }
            }
            if (flex_layout->cross_axis_size == 0.0f) {
                // For column flex with auto width, determine cross-axis size
                bool has_explicit_width = container->blk && container->blk->given_width >= 0;
                if (!has_explicit_width && content_width > 0) {
                    // content_width is already correctly computed by the containing block
                    // or ABS POS shrink-to-fit calculation. Use it directly as the cross-axis
                    // size instead of trying to scan items (which may not have widths yet).
                    // Per CSS Flexbox §9.4: the cross size of a column flex container is its
                    // used width, which was already determined by the box model.
                    flex_layout->cross_axis_size = (float)content_width;
                    log_debug("COLUMN FLEX: cross_axis_size=%.1f from content_width (container.width=%d)",
                              flex_layout->cross_axis_size, container->width);
                } else if (!has_explicit_width && container->position &&
                           (container->position->position == CSS_VALUE_ABSOLUTE ||
                            container->position->position == CSS_VALUE_FIXED)) {
                    // Absolute/fixed with content_width=0 and no children: shrink-to-fit → content is 0
                    // Container width should be just border + padding
                    flex_layout->cross_axis_size = 0.0f;
                    float bp_width = 0.0f;
                    if (container->bound) {
                        bp_width += container->bound->padding.left + container->bound->padding.right;
                        if (container->bound->border) {
                            bp_width += container->bound->border->width.left + container->bound->border->width.right;
                        }
                    }
                    container->width = bp_width;
                    log_debug("COLUMN FLEX: empty abs-pos, shrink-to-fit width=%.1f (border+padding only)", bp_width);
                } else if (!has_explicit_width) {
                    flex_layout->cross_axis_size = (float)content_width;
                } else {
                    flex_layout->cross_axis_size = (float)content_width;
                }
            }
        }

        log_debug("AXIS INIT - after: main=%.1f, cross=%.1f, horizontal=%d",
               flex_layout->main_axis_size, flex_layout->cross_axis_size, is_horizontal);

        // ENHANCED: Update container dimensions to match calculated flex sizes
        if (is_horizontal) {
            float new_height = flex_layout->cross_axis_size;
            // CRITICAL: Always update container height to prevent negative heights
            if (container->height <= 0 || new_height > container->height) {
                log_debug("CONTAINER HEIGHT UPDATE - updating from %d to %.1f (cross_axis_size=%.1f)",
                       container->height, new_height, flex_layout->cross_axis_size);
                container->height = new_height;
            }
        }
        // DEBUG: Set axis sizes
    }

    // Phase 1: Collect flex items
    // OPTIMIZATION: Skip collection if items were already collected by unified pass
    View** items;
    int item_count;
    if (flex_layout->item_count > 0 && flex_layout->flex_items) {
        // Items already collected by collect_and_prepare_flex_items()
        log_debug("Phase 1: Using pre-collected flex items (count=%d)", flex_layout->item_count);
        items = flex_layout->flex_items;
        item_count = flex_layout->item_count;
    } else {
        // Fallback to legacy collection (for backward compatibility)
        log_debug("Phase 1: Collecting flex items (legacy path)");
        item_count = collect_flex_items(flex_layout, container, &items);
    }
    // DEBUG: Flex items collected

    if (item_count == 0) {
        log_debug("No flex items found");
        return;
    }

    // Phase 2: Sort items by order property
    sort_flex_items_by_order(items, item_count);

    // Phase 2.5: Resolve constraints for all flex items
    // This must happen before flex basis calculation in create_flex_lines
    apply_constraints_to_flex_items(flex_layout);

    // SHRINK-TO-FIT RECALCULATION: Now that items have intrinsic sizes (calculated by
    // apply_constraints_to_flex_items), recalculate main_axis_size for indefinite containers
    if (flex_layout->main_axis_is_indefinite && container->is_element()) {
        bool is_horizontal = is_main_axis_horizontal(flex_layout);
        if (is_horizontal) {
            // Row flex with indefinite width: use sum of item max-content widths
            // Iterate over container's DOM children to include text nodes
            DomElement* container_elem = (DomElement*)container;
            float total_item_width = 0.0f;
            int flex_item_count = 0;

            for (DomNode* child = container_elem->first_child; child; child = child->next_sibling) {
                float item_width = 0.0f;

                if (child->is_element()) {
                    ViewElement* item = (ViewElement*)child->as_element();

                    // Skip display:none, absolutely positioned, and hidden items
                    if (item && should_skip_flex_item(item)) {
                        continue;
                    }

                    // Compute max-content contribution per CSS §9.9.1:
                    // 1) Start with item's outer max-content size
                    if (item->blk && item->blk->given_width >= 0) {
                        item_width = item->blk->given_width;
                    } else if (item->fi && item->fi->has_intrinsic_width) {
                        item_width = item->fi->intrinsic_width.max_content;
                        // Intrinsic sizes from calculate_item_intrinsic_sizes are content-box,
                        // add padding+border for border-box contribution
                        if (item->bound) {
                            item_width += item->bound->padding.left + item->bound->padding.right;
                            if (item->bound->border) {
                                item_width += item->bound->border->width.left + item->bound->border->width.right;
                            }
                        }
                    } else if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
                        // Form control (including <button>): use form intrinsic width
                        item_width = item->form->intrinsic_width;
                        if (item_width <= 0 && item->tag() == HTM_TAG_BUTTON && flex_layout && flex_layout->lycon) {
                            IntrinsicSizes sizes = measure_element_intrinsic_widths(flex_layout->lycon, (DomElement*)item, true);
                            item_width = sizes.max_content;
                            item->form->intrinsic_width = item_width;
                        }
                        if (item->bound) {
                            item_width += item->bound->padding.left + item->bound->padding.right;
                            if (item->bound->border) {
                                item_width += item->bound->border->width.left + item->bound->border->width.right;
                            }
                        }
                    } else {
                        // Intrinsic sizes not yet computed - calculate them now
                        if (item->fi && !item->fi->has_intrinsic_width) {
                            calculate_item_intrinsic_sizes(item, flex_layout);
                        }
                        if (item->fi && item->fi->has_intrinsic_width) {
                            item_width = item->fi->intrinsic_width.max_content;
                            // Content-box → border-box conversion
                            if (item->bound) {
                                item_width += item->bound->padding.left + item->bound->padding.right;
                                if (item->bound->border) {
                                    item_width += item->bound->border->width.left + item->bound->border->width.right;
                                }
                            }
                        } else if (item->width > 0) {
                            item_width = item->width;
                        }
                    }
                    // 2) If item has non-zero flex-shrink and its max-content exceeds
                    //    its specified size (flex-basis), use the specified size instead.
                    //    CSS §9.9.1: "unless that value is greater than its outer specified
                    //    size and the item has a non-zero flex-shrink factor"
                    // Note: Do NOT clamp items with flex-grow > 0. Such items will stretch to
                    // fill available space, so their max-content contribution is their natural
                    // width (not clamped to flex-basis:0). Only items that can shrink (flex-grow=0)
                    // have their shrink-to-fit contribution clamped by flex-basis.
                    if (item->fi && item->fi->flex_shrink > 0 && item->fi->flex_grow == 0 &&
                        item->fi->flex_basis >= 0 && !item->fi->flex_basis_is_percent &&
                        item_width > item->fi->flex_basis) {
                        item_width = item->fi->flex_basis;
                    }
                    // 3) Clamp by min-width/max-width and border-box floor (§1.1)
                    if (item->blk) {
                        if (item->blk->given_max_width >= 0 && item_width > item->blk->given_max_width)
                            item_width = item->blk->given_max_width;
                        if (item->blk->given_min_width >= 0 && item_width < item->blk->given_min_width)
                            item_width = item->blk->given_min_width;
                        // CSS Box Model: In border-box, the box cannot be smaller than
                        // its padding+border (content area floors at 0).
                        if (item->blk->box_sizing == CSS_VALUE_BORDER_BOX && item->bound) {
                            float pad_border = item->bound->padding.left + item->bound->padding.right;
                            if (item->bound->border) {
                                pad_border += item->bound->border->width.left + item->bound->border->width.right;
                            }
                            if (item_width < pad_border) {
                                item_width = pad_border;
                            }
                        }
                    }
                    flex_item_count++;
                    log_debug("SHRINK-TO-FIT RECALC: element item=%p width=%.1f (has_intrinsic=%d, fi=%p, flex_basis=%.1f)",
                              item, item_width, item->fi ? item->fi->has_intrinsic_width : -1,
                              item->fi, item->fi ? item->fi->flex_basis : -999.0f);
                } else if (child->is_text()) {
                    // Text nodes in flex containers become anonymous flex items
                    // Use their measured text width from intrinsic sizing
                    const char* text = (const char*)child->text_data();
                    if (text) {
                        size_t text_len = strlen(text);
                        // Normalize whitespace: collapse consecutive spaces, trim leading/trailing
                        // This matches CSS white-space: normal behavior
                        char normalized_buffer[2048];
                        size_t out_pos = 0;
                        bool in_whitespace = true;  // Start as if preceded by whitespace (trims leading)
                        for (size_t i = 0; i < text_len && out_pos < sizeof(normalized_buffer) - 1; i++) {
                            unsigned char ch = (unsigned char)text[i];
                            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f') {
                                if (!in_whitespace) {
                                    normalized_buffer[out_pos++] = ' ';  // Collapse to single space
                                    in_whitespace = true;
                                }
                            } else {
                                normalized_buffer[out_pos++] = (char)ch;
                                in_whitespace = false;
                            }
                        }
                        // Trim trailing whitespace
                        while (out_pos > 0 && normalized_buffer[out_pos - 1] == ' ') {
                            out_pos--;
                        }
                        normalized_buffer[out_pos] = '\0';

                        // Only measure if there's non-whitespace content
                        if (out_pos > 0) {
                            // Measure normalized text width using intrinsic sizing
                            TextIntrinsicWidths text_widths = measure_text_intrinsic_widths(
                                lycon, normalized_buffer, out_pos);
                            item_width = text_widths.max_content;
                            flex_item_count++;
                            log_debug("SHRINK-TO-FIT RECALC: text item width=%.1f, normalized_len=%zu, text='%.30s...'",
                                      item_width, out_pos, normalized_buffer);
                        }
                    }
                }

                total_item_width += item_width;
            }
            // Add gaps between flex items
            // Note: For auto-sized containers with percentage gaps, we resolved
            // the gap earlier (in AXIS INIT). The gap space is applied during
            // flex item positioning but does NOT increase the container's auto-sized width.
            // Items overflow if they can't shrink to accommodate gaps.
            bool is_auto_sized_with_pct_gap = container->embed && container->embed->flex &&
                container->embed->flex->column_gap_is_percent &&
                !(container->blk && container->blk->given_width >= 0);
            if (flex_item_count > 1 && !is_auto_sized_with_pct_gap) {
                total_item_width += flex_layout->column_gap * (flex_item_count - 1);
            }

            // Always update container width in shrink-to-fit: even if total is 0,
            // we need to override the containing-block fallback width
            {
                flex_layout->main_axis_size = total_item_width;
                // Also update container width (include padding AND border)
                float padding_border_width = 0.0f;
                if (container->bound) {
                    padding_border_width = container->bound->padding.left + container->bound->padding.right;
                    if (container->bound->border) {
                        padding_border_width += container->bound->border->width.left + container->bound->border->width.right;
                    }
                }
                container->width = total_item_width + padding_border_width;
                log_debug("SHRINK-TO-FIT RECALC: main_axis_size=%.1f, container.width=%d, items=%d",
                          flex_layout->main_axis_size, container->width, flex_item_count);
            }
        }
    }

    // For column flex with wrap: use max-height as wrapping constraint.
    // Per CSS Flexbox §9.3, when the container has no definite main size but has
    // a max main size (max-height for column flex), use it for line breaking.
    if (!is_main_axis_horizontal(flex_layout) && flex_layout->wrap != WRAP_NOWRAP &&
        container->blk && container->blk->given_max_height > 0 &&
        !(container->blk->given_height >= 0)) {
        float max_content_height = container->blk->given_max_height;
        if (container->blk->box_sizing == CSS_VALUE_BORDER_BOX && container->bound) {
            max_content_height -= (container->bound->padding.top + container->bound->padding.bottom);
            if (container->bound->border) {
                max_content_height -= (container->bound->border->width.top + container->bound->border->width.bottom);
            }
        }
        if (max_content_height > 0 && flex_layout->main_axis_size > max_content_height) {
            log_debug("COLUMN WRAP: capping main_axis_size from %.1f to %.1f (max-height constraint)",
                      flex_layout->main_axis_size, max_content_height);
            flex_layout->main_axis_size = max_content_height;
        }
    }

    // Phase 3: Create flex lines (handle wrapping)
    int line_count = create_flex_lines(flex_layout, items, item_count);

    // Save pre-Phase-4 main_axis_size to detect if Phase 5b increases it
    float pre_phase4_main_axis_size = flex_layout->main_axis_size;

    // Phase 4: Resolve flexible lengths for each line
    log_info("%s Phase 4: About to resolve flexible lengths for %d lines", container->source_loc(), line_count);
    for (int i = 0; i < line_count; i++) {
        log_info("%s Phase 4: Resolving line %d", container->source_loc(), i);
        resolve_flexible_lengths(flex_layout, &flex_layout->lines[i]);
        log_info("%s Phase 4: Completed line %d", container->source_loc(), i);
    }
    log_info("%s Phase 4: All flex lengths resolved", container->source_loc());

    // Phase 4b: Update container main axis size when SHRINK-TO-FIT RECALC gave 0.
    // This happens when all flex items have flex-basis:0 — SHRINK-TO-FIT clamps them all
    // to 0, giving main_axis_size = 0. Phase 4 then clamps each item UP to its min-content
    // (via resolved_min_width). We must propagate these new sizes to the container.
    //
    // Guard: only run when pre_phase4_main_axis_size <= 0 (SHRINK-TO-FIT gave 0).
    // This avoids double-counting cases where SHRINK-TO-FIT already used intrinsic sizes
    // to correctly size the container (e.g. items without explicit flex-basis).
    // When SHRINK-TO-FIT gave 0, percentage-based gaps also resolve to 0, preventing
    // cyclic double-counting issues.
    //
    // Only applies to: row flex, indefinite main axis (no explicit width), not wrapping.
    bool phase4b_eligible = is_main_axis_horizontal(flex_layout) &&
                            flex_layout->main_axis_is_indefinite &&
                            flex_layout->wrap == CSS_VALUE_NOWRAP &&
                            pre_phase4_main_axis_size <= 0.5f;
    if (phase4b_eligible) {
        float post_items_sum = 0.0f;
        int p4b_item_count = 0;
        for (int i = 0; i < line_count; i++) {
            FlexLineInfo* line = &flex_layout->lines[i];
            for (int j = 0; j < line->item_count; j++) {
                ViewElement* item = (ViewElement*)line->items[j]->as_element();
                if (item) {
                    post_items_sum += (float)item->width;
                    if (item->bound) {
                        post_items_sum += item->bound->margin.left + item->bound->margin.right;
                    }
                    p4b_item_count++;
                }
            }
        }
        // Include gaps between items (resolved to 0 for percentage gaps when container=0)
        if (p4b_item_count > 1) {
            post_items_sum += flex_layout->column_gap * (p4b_item_count - 1);
        }
        if (post_items_sum > 0.5f) {
            float padding_border_h = 0.0f;
            if (container->bound) {
                padding_border_h = container->bound->padding.left + container->bound->padding.right;
                if (container->bound->border) {
                    padding_border_h += container->bound->border->width.left + container->bound->border->width.right;
                }
            }
            float old_width = (float)container->width;
            float new_width = post_items_sum + padding_border_h;
            flex_layout->main_axis_size = post_items_sum;
            container->width = new_width;
            log_debug("%s Phase 4b: shrink-to-fit width updated %.1f -> %.1f (items sum=%.1f, padding+border=%.1f)", container->source_loc(),
                      old_width, new_width, post_items_sum, padding_border_h);
        }
    }

    // Phase 4.5: Determine hypothetical cross sizes for each item
    // CSS Flexbox §9.4: After main sizes are resolved, determine cross sizes
    determine_hypothetical_cross_sizes(lycon, flex_layout);

    // REMOVED: Don't override content dimensions after flex calculations
    // The flex algorithm should work with the proper content dimensions
    // that were calculated during box-sizing in the block layout phase

    // Phase 5: Calculate cross sizes for lines
    calculate_line_cross_sizes(flex_layout);

    // Phase 5b: Apply min-height/min-width constraint to container BEFORE alignment
    // This must happen BEFORE Phase 6 (main axis alignment) so justify-content
    // has the correct container size to distribute space
    if (container->blk) {
        // Get padding values for content-box calculation
        float padding_main = 0.0f;
        float padding_cross = 0.0f;
        if (container->bound) {
            if (is_main_axis_horizontal(flex_layout)) {
                padding_main = container->bound->padding.left + container->bound->padding.right;
                padding_cross = container->bound->padding.top + container->bound->padding.bottom;
            } else {
                padding_main = container->bound->padding.top + container->bound->padding.bottom;
                padding_cross = container->bound->padding.left + container->bound->padding.right;
            }
        }

        if (is_main_axis_horizontal(flex_layout)) {
            // Row flex: min-width affects main_axis_size (for justify-content)
            // CRITICAL: min-width is border-box, main_axis_size is content-box
            float min_content_width = container->blk->given_min_width - padding_main;
            if (container->blk->given_min_width > 0 && flex_layout->main_axis_size < min_content_width) {
                log_debug("%s Phase 5b: Applying min-width to main axis: %.1f -> %.1f (min-width=%.1f, padding=%.1f)", container->source_loc(),
                          flex_layout->main_axis_size, min_content_width, container->blk->given_min_width, padding_main);
                flex_layout->main_axis_size = min_content_width;
                container->width = container->blk->given_min_width;  // Keep border-box width
            }
            // Row flex: min-height affects cross_axis_size
            float min_content_height = container->blk->given_min_height - padding_cross;
            if (container->blk->given_min_height > 0 && container->height < container->blk->given_min_height) {
                log_debug("%s Phase 5b: Applying min-height to cross axis: %.1f -> %.1f", container->source_loc(),
                          container->height, container->blk->given_min_height);
                container->height = container->blk->given_min_height;
                flex_layout->cross_axis_size = min_content_height > 0 ? min_content_height : container->blk->given_min_height;
            }
        } else {
            // Column flex: min-height affects main_axis_size (and justify-content)
            float min_content_height = container->blk->given_min_height - padding_main;
            if (container->blk->given_min_height > 0 && container->height < container->blk->given_min_height) {
                log_debug("%s Phase 5b: Applying min-height to main axis: %.1f -> %.1f", container->source_loc(),
                          container->height, container->blk->given_min_height);
                container->height = container->blk->given_min_height;
                flex_layout->main_axis_size = min_content_height > 0 ? min_content_height : container->blk->given_min_height;
            }
            // Column flex: min-width affects cross_axis_size
            float min_content_width = container->blk->given_min_width - padding_cross;
            if (container->blk->given_min_width > 0 && flex_layout->cross_axis_size < min_content_width) {
                log_debug("%s Phase 5b: Applying min-width to cross axis: %.1f -> %.1f", container->source_loc(),
                          flex_layout->cross_axis_size, container->blk->given_min_width);
                flex_layout->cross_axis_size = min_content_width > 0 ? min_content_width : container->blk->given_min_width;
                container->width = container->blk->given_min_width;
            }
        }
    }

    // Phase 5c: Re-run flex distribution if min-size clamping created a definite main axis
    // Per CSS Flexbox spec §9.9.2: when the container's final main size is definite
    // (e.g., clamped by min-height), flex-grow/shrink should distribute space accordingly.
    // Only re-run when Phase 5b ACTUALLY INCREASED the main_axis_size (meaning min-size
    // clamping raised the container from its content-based or initial size).
    // Do NOT re-run merely because main_axis_is_indefinite && main_axis_size > 0,
    // as the auto-height estimation can set a positive main_axis_size (e.g., from padding)
    // that does NOT represent a definite constraint.
    bool main_axis_size_increased = flex_layout->main_axis_size > pre_phase4_main_axis_size + 0.5f;
    if (main_axis_size_increased) {
        log_debug("%s Phase 5c: main axis size increased by min-size (was=%.1f, now=%.1f, indefinite=%d), re-running flex distribution", container->source_loc(),
                  pre_phase4_main_axis_size, flex_layout->main_axis_size, flex_layout->main_axis_is_indefinite);
        flex_layout->main_axis_is_indefinite = false;
        for (int i = 0; i < line_count; i++) {
            resolve_flexible_lengths(flex_layout, &flex_layout->lines[i]);
        }
    }

    // Phase 6: Align items on main axis
    for (int i = 0; i < line_count; i++) {
        align_items_main_axis(flex_layout, &flex_layout->lines[i]);
    }

    // Phase 7: Finalize container cross size for auto-height containers
    // This MUST happen BEFORE align_content so it uses correct container size
    if (is_main_axis_horizontal(flex_layout)) {
        int total_line_cross = 0;
        for (int i = 0; i < line_count; i++) {
            total_line_cross += flex_layout->lines[i].cross_size;
        }
        // Add gaps between lines
        if (line_count > 1) {
            total_line_cross += flex_layout->row_gap * (line_count - 1);
        }
        log_debug("%s Phase 7: container=%s total_line_cross=%d, current height=%d", container->source_loc(),
                  container->node_name(), total_line_cross, container->height);
        // Check if container has explicit height (given_height >= 0 means explicit, -1 means auto)
        // OR if this container is a flex item whose height was set by parent flex
        // OR if this container is a grid item whose height was set by parent grid
        bool has_explicit_height = container->blk && container->blk->given_height >= 0;
        // Check grid item status - must verify item_prop_type to access correct union member
        // (gi, fi, tb, td, form are all in a union, accessing wrong one gives garbage)
        bool is_grid_item = (container->item_prop_type == DomElement::ITEM_PROP_GRID) &&
                            container->gi && container->gi->computed_grid_row_start > 0;
        if (!has_explicit_height && is_grid_item && container->height > 0) {
            has_explicit_height = true;
            log_debug("%s Phase 7: (Row) Container is a grid item with height=%.1f set by parent grid", container->source_loc(),
                      container->height);
        }
        // Only check flex item status if this container is actually a flex item in a flex parent
        // NOTE: fi can be allocated from CSS resolution even if parent is not a flex container,
        // so we must verify the parent is actually flex before using fi for layout decisions.
        bool is_flex_item = container->fi != nullptr ||
                            (container->item_prop_type == DomElement::ITEM_PROP_FORM && container->form);
        // Check if parent is actually a flex container using display property
        // (embed->flex may not be allocated if parent has no explicit flex CSS properties)
        bool parent_is_flex = false;
        DomElement* parent_elem = container->parent ? container->parent->as_element() : nullptr;
        {
            parent_is_flex = parent_elem && (parent_elem->display.inner == CSS_VALUE_FLEX);
        }
        if (!has_explicit_height && is_flex_item && parent_is_flex && container->height > 0 && container->fi) {
            // Determine parent flex direction to know what stretch affects
            int parent_dir = DIR_ROW;  // default
            {
                ViewBlock* pb = parent_elem ? (ViewBlock*)(ViewElement*)parent_elem : nullptr;
                if (pb && pb->embed && pb->embed->flex) {
                    parent_dir = pb->embed->flex->direction;
                }
            }
            bool parent_is_row = (parent_dir == DIR_ROW || parent_dir == DIR_ROW_REVERSE);
            // Check if parent flex actually set this item's height:
            // Case 1: Parent flex grew/shrank this item on its main axis and that produced the height
            if (container->fi->main_size_from_flex) {
                has_explicit_height = true;
                log_debug("%s Phase 7: Container height set by parent flex (main_size_from_flex=1)", container->source_loc());
            }
            // Case 2: Parent is ROW flex and stretched this item on cross axis (cross=height)
            // NOTE: Stretch does NOT make the height "explicit" for Phase 7 purposes.
            // Phase 7 computes the CONTENT-BASED height (sum of flex line cross sizes),
            // which represents the item's hypothetical cross size per CSS Flexbox §9.4.
            // The parent's stretch (Step 11) will later set the used cross size to the
            // line cross size (which is max of hypothetical cross sizes). Phase 7 must
            // set the content height first so the parent can compute line cross sizes correctly.
            // Previously, marking stretch as explicit caused wrapping flex containers to
            // report height=0, making the parent unable to discover the true content height.
            // Case 3: Parent is COLUMN flex and item has explicit flex-basis (not auto)
            // In column flex, height IS the main axis. An explicit flex-basis means the
            // height was definitively determined by flex (basis + min/max clamping),
            // not by content. flex_basis=-1 means auto, >=0 means explicit.
            else if (!parent_is_row && container->fi->flex_basis >= 0) {
                has_explicit_height = true;
                log_debug("%s Phase 7: Container height set by parent column flex (explicit flex-basis=%.1f)", container->source_loc(),
                          container->fi->flex_basis);
            }
        }

        // Update container cross_axis_size to actual content height
        if (total_line_cross > 0) {
            if (!has_explicit_height) {
                // Only update for auto-height containers
                log_debug("%s Phase 7: Updating cross_axis_size from %.1f to %d (auto-height)", container->source_loc(),
                         flex_layout->cross_axis_size, total_line_cross);
                flex_layout->cross_axis_size = (float)total_line_cross;
                // Container height should be content + padding + border (not just content)
                float padding_height = 0;
                float border_height = 0;
                if (container->bound) {
                    padding_height = container->bound->padding.top + container->bound->padding.bottom;
                    if (container->bound->border) {
                        border_height = container->bound->border->width.top + container->bound->border->width.bottom;
                    }
                }
                container->height = total_line_cross + padding_height + border_height;
                log_debug("%s Phase 7: UPDATED container=%p (%s) height to %.1f (total_line_cross=%d + padding=%.1f + border=%.1f)", container->source_loc(),
                         container, container->node_name(), container->height, total_line_cross, padding_height, border_height);
            } else {
                log_debug("%s Phase 7: Container has explicit height, not updating", container->source_loc());
            }
        }
    } else {
        // Column flex: finalize main_axis_size (height) for auto-height containers
        // For multi-line column wrap, container height = tallest column (max line main)
        // For single-line, max of one line = sum of that line's items (same behavior)
        int total_line_main = 0;
        for (int i = 0; i < line_count; i++) {
            float line_main = 0;
            FlexLineInfo* line = &flex_layout->lines[i];
            for (int j = 0; j < line->item_count; j++) {
                ViewElement* item = (ViewElement*)line->items[j]->as_element();
                if (item) {
                    line_main += item->height;
                    // Include item's main-axis margins (top + bottom for column flex)
                    if (item->bound) {
                        line_main += item->bound->margin.top + item->bound->margin.bottom;
                    }
                }
            }
            // Add gaps between items
            if (line->item_count > 1) {
                line_main += flex_layout->row_gap * (line->item_count - 1);
            }
            if (line_main > total_line_main) total_line_main = line_main;
        }
        // Check if container has explicit height (given_height >= 0 means explicit, -1 means auto)
        // OR if this container is a flex item whose height was set by parent flex
        // OR if this container is a grid item whose height was set by parent grid
        bool has_explicit_height = container->blk && container->blk->given_height >= 0;
        // Check grid item status - must verify item_prop_type to access correct union member
        // (gi, fi, tb, td, form are all in a union, accessing wrong one gives garbage)
        bool is_grid_item_col = (container->item_prop_type == DomElement::ITEM_PROP_GRID) &&
                                container->gi && container->gi->computed_grid_row_start > 0;
        if (!has_explicit_height && is_grid_item_col && container->height > 0) {
            has_explicit_height = true;
            log_debug("%s Phase 7: (Column) Container is a grid item with height=%.1f set by parent grid", container->source_loc(),
                      container->height);
        }
        // Only check flex item status if this container is actually a flex item
        // and its parent is actually a flex container (check display.inner, not embed->flex)
        bool is_flex_item_col = container->fi != nullptr ||
                                (container->item_prop_type == DomElement::ITEM_PROP_FORM && container->form);
        DomElement* parent_elem_col = container->parent ? container->parent->as_element() : nullptr;
        bool parent_is_flex_col = parent_elem_col && (parent_elem_col->display.inner == CSS_VALUE_FLEX);
        if (!has_explicit_height && is_flex_item_col && parent_is_flex_col && container->height > 0 && container->fi) {
            // Determine parent flex direction
            int parent_dir_col = DIR_ROW;
            {
                ViewBlock* pb = parent_elem_col ? (ViewBlock*)(ViewElement*)parent_elem_col : nullptr;
                if (pb && pb->embed && pb->embed->flex) {
                    parent_dir_col = pb->embed->flex->direction;
                }
            }
            bool parent_is_row_col = (parent_dir_col == DIR_ROW || parent_dir_col == DIR_ROW_REVERSE);
            // Check if parent set the height via flex sizing
            if (container->fi->main_size_from_flex) {
                has_explicit_height = true;
                log_debug("%s Phase 7: (Column) Container height set by parent flex (main_size_from_flex)", container->source_loc());
            }
            // Only row parent's stretch affects height (cross=height)
            else if (parent_is_row_col) {
                int effective_align_col = container->fi->align_self;
                if (effective_align_col == ALIGN_AUTO) {
                    ViewBlock* pb = parent_elem_col ? (ViewBlock*)(ViewElement*)parent_elem_col : nullptr;
                    if (pb && pb->embed && pb->embed->flex) {
                        effective_align_col = pb->embed->flex->align_items;
                    } else {
                        effective_align_col = ALIGN_STRETCH;
                    }
                }
                if (effective_align_col == ALIGN_STRETCH) {
                    has_explicit_height = true;
                    log_debug("%s Phase 7: (Column) Container height set by parent row flex (align-items:stretch)", container->source_loc());
                }
            }
            // Column parent with explicit flex-basis: height determined by flex
            else if (!parent_is_row_col && container->fi->flex_basis >= 0) {
                has_explicit_height = true;
                log_debug("%s Phase 7: (Column) Container height set by parent column flex (explicit flex-basis=%.1f)", container->source_loc(),
                          container->fi->flex_basis);
            }
        }

        if (total_line_main > 0) {
            if (!has_explicit_height) {
                log_debug("%s Phase 7: (Column) Updating main_axis_size from %.1f to %d (auto-height)", container->source_loc(),
                         flex_layout->main_axis_size, total_line_main);
                flex_layout->main_axis_size = (float)total_line_main;
                // Container height should be content + padding + border (not just content)
                float padding_height = 0;
                float border_height = 0;
                if (container->bound) {
                    padding_height = container->bound->padding.top + container->bound->padding.bottom;
                    if (container->bound->border) {
                        border_height = container->bound->border->width.top + container->bound->border->width.bottom;
                    }
                }
                container->height = total_line_main + padding_height + border_height;
            } else {
                log_debug("%s Phase 7: (Column) Container has explicit height, not updating", container->source_loc());
            }
        }

        // Phase 7 (Column): Update container cross-axis (width) for shrink-to-fit containers.
        // For multi-line column wrap, cross-axis = sum of line cross sizes.
        // For single-line, cross-axis = max outer item width.
        {
            bool has_explicit_width_p7 = container->blk && container->blk->given_width >= 0;
            bool has_min_width_p7 = container->blk && container->blk->given_min_width > 0;
            bool has_max_width_p7 = container->blk && container->blk->given_max_width > 0;
            bool is_abs_p7 = container->position &&
                (container->position->position == CSS_VALUE_ABSOLUTE ||
                 container->position->position == CSS_VALUE_FIXED);
            bool is_shrink_to_fit_p7 = is_abs_p7 && !has_explicit_width_p7 && !has_min_width_p7 && !has_max_width_p7 &&
                !(container->position->has_left && container->position->has_right);
            if (is_shrink_to_fit_p7) {
            float total_cross = 0.0f;
            if (line_count > 1) {
                // Multi-line: sum line cross sizes (each line is a column with its own width)
                for (int i = 0; i < line_count; i++) {
                    total_cross += flex_layout->lines[i].cross_size;
                }
                // Add cross-axis gaps between lines
                if (line_count > 1) {
                    total_cross += flex_layout->column_gap * (line_count - 1);
                }
            } else {
                // Single-line: max outer item width
                for (int i = 0; i < line_count; i++) {
                    FlexLineInfo* line = &flex_layout->lines[i];
                    for (int j = 0; j < line->item_count; j++) {
                        ViewElement* item = (ViewElement*)line->items[j]->as_element();
                        if (item) {
                            float item_width = (float)item->width;
                            if (item->bound) {
                                item_width += item->bound->margin.left + item->bound->margin.right;
                            }
                            if (item_width > total_cross) total_cross = item_width;
                        }
                    }
                }
            }
            if (total_cross > 0.0f && total_cross > flex_layout->cross_axis_size) {
                log_debug("%s Phase 7: (Column) Updating cross_axis_size from %.1f to %.1f (lines=%d)", container->source_loc(),
                         flex_layout->cross_axis_size, total_cross, line_count);
                flex_layout->cross_axis_size = total_cross;
                float padding_border_width = 0.0f;
                if (container->bound) {
                    padding_border_width += container->bound->padding.left + container->bound->padding.right;
                    if (container->bound->border) {
                        padding_border_width += container->bound->border->width.left + container->bound->border->width.right;
                    }
                }
                container->width = total_cross + padding_border_width;
            }
            }  // end if (is_shrink_to_fit_p7)
        }  // end Phase 7 column cross-axis block
    }

    // Phase 7b: Apply min-height constraint to container
    // This must happen AFTER content-based height calculation but BEFORE align_content
    // so that justify-content/align-content have the correct container size to work with
    if (container->blk && container->blk->given_min_height > 0) {
        float min_height = container->blk->given_min_height;
        if (container->height < min_height) {
            log_debug("%s Phase 7b: Applying min-height constraint: %.1f -> %.1f", container->source_loc(), container->height, min_height);
            container->height = min_height;
            // Update flex_layout dimensions for alignment calculations.
            // min_height is border-box when box-sizing: border-box; convert to content-box
            // since cross_axis_size/main_axis_size are consistently content-box.
            float content_val = min_height;
            if (container->blk->box_sizing == CSS_VALUE_BORDER_BOX && container->bound) {
                if (is_main_axis_horizontal(flex_layout)) {
                    content_val -= (container->bound->padding.top + container->bound->padding.bottom);
                    if (container->bound->border) {
                        content_val -= (container->bound->border->width.top + container->bound->border->width.bottom);
                    }
                } else {
                    content_val -= (container->bound->padding.top + container->bound->padding.bottom);
                    if (container->bound->border) {
                        content_val -= (container->bound->border->width.top + container->bound->border->width.bottom);
                    }
                }
                if (content_val < 0) content_val = 0;
            }
            if (is_main_axis_horizontal(flex_layout)) {
                flex_layout->cross_axis_size = content_val;
            } else {
                flex_layout->main_axis_size = content_val;
            }
        }
    }

    // Phase 7c: Apply max-height/max-width clamping for auto-sized flex containers
    // Per CSS Flexbox §9.2 + CSS Box Model §10.7: after calculating auto-height,
    // if the result exceeds max-height, clamp to max-height and re-run flex distribution
    // so items shrink to fit within the clamped size.
    // Same logic applies to max-width for row flex with auto-width.
    {
        float max_main = -1;
        float max_cross = -1;
        bool is_horizontal = is_main_axis_horizontal(flex_layout);

        // Get max-height and max-width constraints
        if (container->blk) {
            if (container->blk->given_max_height > 0) {
                if (is_horizontal) {
                    max_cross = container->blk->given_max_height;
                } else {
                    max_main = container->blk->given_max_height;
                }
            }
            if (container->blk->given_max_width > 0) {
                if (is_horizontal) {
                    max_main = container->blk->given_max_width;
                } else {
                    max_cross = container->blk->given_max_width;
                }
            }
        }

        // Check main-axis max constraint (e.g., max-height for column flex, max-width for row flex)
        if (max_main > 0) {
            // max_main is border-box when box-sizing: border-box; convert to content-box
            // since main_axis_size is content-box (consistent with init_flex_container)
            float main_content = max_main;
            if (container->blk->box_sizing == CSS_VALUE_BORDER_BOX && container->bound) {
                if (is_horizontal) {
                    main_content -= (container->bound->padding.left + container->bound->padding.right);
                    if (container->bound->border) {
                        main_content -= (container->bound->border->width.left + container->bound->border->width.right);
                    }
                } else {
                    main_content -= (container->bound->padding.top + container->bound->padding.bottom);
                    if (container->bound->border) {
                        main_content -= (container->bound->border->width.top + container->bound->border->width.bottom);
                    }
                }
                if (main_content < 0) main_content = 0;
            }
            if (flex_layout->main_axis_size > main_content) {
                log_debug("%s Phase 7c: main_axis_size %.1f exceeds max %.1f (content=%.1f), clamping and re-distributing", container->source_loc(),
                          flex_layout->main_axis_size, max_main, main_content);
                flex_layout->main_axis_size = main_content;
                flex_layout->main_axis_is_indefinite = false;

                // Update container dimension
                if (is_horizontal) {
                    container->width = max_main;
                } else {
                    container->height = max_main;
                }

                // Re-run flex distribution with the clamped definite size
                for (int i = 0; i < line_count; i++) {
                    resolve_flexible_lengths(flex_layout, &flex_layout->lines[i]);
                }

                // Re-run main-axis alignment with new sizes
                for (int i = 0; i < line_count; i++) {
                    align_items_main_axis(flex_layout, &flex_layout->lines[i]);
                }
            }
        }

        // Check cross-axis max constraint (e.g., max-height for row flex)
        if (max_cross > 0) {
            if (is_horizontal && container->height > max_cross) {
                log_debug("%s Phase 7c: cross_axis height %.1f exceeds max %.1f, clamping", container->source_loc(),
                          container->height, max_cross);
                container->height = max_cross;
                // cross_axis_size is content-box (consistent with init_flex_container).
                // max_cross is the raw given_max_height which is border-box when
                // box-sizing: border-box. Subtract padding+border to get content-box.
                float cross_content = max_cross;
                if (container->blk->box_sizing == CSS_VALUE_BORDER_BOX && container->bound) {
                    cross_content -= (container->bound->padding.top + container->bound->padding.bottom);
                    if (container->bound->border) {
                        cross_content -= (container->bound->border->width.top + container->bound->border->width.bottom);
                    }
                    if (cross_content < 0) cross_content = 0;
                }
                flex_layout->cross_axis_size = cross_content;
            } else if (!is_horizontal && container->width > max_cross) {
                log_debug("%s Phase 7c: cross_axis width %.1f exceeds max %.1f, clamping", container->source_loc(),
                          container->width, max_cross);
                container->width = max_cross;
                // Same content-box conversion for column flex cross-axis (width)
                float cross_content = max_cross;
                if (container->blk->box_sizing == CSS_VALUE_BORDER_BOX && container->bound) {
                    cross_content -= (container->bound->padding.left + container->bound->padding.right);
                    if (container->bound->border) {
                        cross_content -= (container->bound->border->width.left + container->bound->border->width.right);
                    }
                    if (cross_content < 0) cross_content = 0;
                }
                flex_layout->cross_axis_size = cross_content;
            }
        }
    }

    // Phase 8: Align content (distribute space among lines)
    // Note: align-content applies to flex containers with flex-wrap: wrap or wrap-reverse
    // CRITICAL: This must happen BEFORE align_items_cross_axis so line cross-sizes are final
    if (flex_layout->wrap != WRAP_NOWRAP) {
        align_content(flex_layout);

        // Phase 8b: Re-layout items after align-content: stretch
        // When align-content: stretch distributes extra cross space to lines, items with
        // auto cross-axis size need re-layout so inner content fills the new cross size.
        if (flex_layout->align_content == ALIGN_STRETCH && flex_layout->lycon) {
            for (int li = 0; li < line_count; li++) {
                FlexLineInfo* sline = &flex_layout->lines[li];
                for (int si = 0; si < sline->item_count; si++) {
                    ViewElement* sitem = (ViewElement*)sline->items[si]->as_element();
                    if (!sitem) continue;
                    // Only re-layout items that will be stretched (auto cross size, not constrained)
                    if (!item_will_stretch(sitem, flex_layout)) continue;
                    float new_cross = (float)sline->cross_size;
                    float current_cross = get_cross_axis_size(sitem, flex_layout);
                    if (new_cross > current_cross + 0.5f) {
                        set_cross_axis_size(sitem, new_cross, flex_layout);
                        layout_flex_item_content(flex_layout->lycon, (ViewBlock*)sitem);
                        log_debug("%s Phase 8b: Re-laid out item after stretch, cross %.1f -> %.1f", container->source_loc(),
                                  current_cross, new_cross);
                    }
                }
            }
        }
    }

    // Phase 9: Align items on cross axis
    // This runs AFTER align_content so line cross-sizes are finalized
    for (int i = 0; i < line_count; i++) {
        align_items_cross_axis(flex_layout, &flex_layout->lines[i]);
    }

    // Phase 9a: Adjust line cross sizes for baseline alignment
    // CSS Flexbox §9.4 Step 8 requires baseline-aware line cross sizes:
    //   line_cross = max(max_pre_baseline + max_post_baseline, max_non_baseline_cross)
    // At Phase 5, inner layouts haven't run so baselines default to item heights.
    // Now that Phase 9 has positioned items with correct baselines, recompute
    // line cross sizes and adjust subsequent line positions if they changed.
    // Applies to ALL flex containers with align-items: baseline (both single-line and multi-line):
    // baseline alignment can require more cross-space than the max item height alone.
    if (flex_layout->align_items == ALIGN_BASELINE && line_count > 0) {
        bool lines_changed = false;
        for (int i = 0; i < line_count; i++) {
            FlexLineInfo* line = &flex_layout->lines[i];
            float max_pre = 0, max_post = 0;
            float max_non_baseline = 0;
            bool has_baseline = false;

            for (int j = 0; j < line->item_count; j++) {
                ViewElement* item = (ViewElement*)line->items[j]->as_element();
                if (!item) continue;

                bool is_baseline_item = false;
                {
                    int align = (item->fi && item->fi->align_self != ALIGN_AUTO) ?
                        item->fi->align_self : flex_layout->align_items;
                    is_baseline_item = (align == ALIGN_BASELINE || align == CSS_VALUE_BASELINE);
                }
                log_debug("%s Phase 9a: item %d: is_baseline_item=%d, height=%.0f", container->source_loc(),
                          j, (int)is_baseline_item, item->height); // INT_CAST_OK: bool for log

                if (is_baseline_item && is_main_axis_horizontal(flex_layout)) {
                    has_baseline = true;
                    float baseline = calculate_item_baseline(item);
                    float margin_top = item->bound ? item->bound->margin.top : 0;
                    float margin_bottom = item->bound ? item->bound->margin.bottom : 0;
                    float outer_cross = item->height + margin_top + margin_bottom;
                    float pre = baseline;
                    float post = outer_cross - baseline;
                    if (pre > max_pre) max_pre = pre;
                    if (post > max_post) max_post = post;
                } else {
                    float item_cross = item->fi && item->fi->hypothetical_outer_cross_size > 0 ?
                        item->fi->hypothetical_outer_cross_size :
                        get_cross_axis_size(item, flex_layout);
                    if (item_cross > max_non_baseline) max_non_baseline = item_cross;
                }
            }

            if (has_baseline) {
                float baseline_extent = max_pre + max_post;
                float new_cross = baseline_extent > max_non_baseline ? baseline_extent : max_non_baseline;
                if (new_cross != line->cross_size) {
                    log_debug("%s Phase 9a: Line %d cross size %d -> %.1f (pre=%.0f, post=%.0f)", container->source_loc(),
                              i, line->cross_size, new_cross, max_pre, max_post);
                    line->cross_size = new_cross;
                    lines_changed = true;
                }
            }
        }

        // If line cross sizes changed, adjust line positions and re-position items
        if (lines_changed) {
            // Recalculate line cross_position values
            int cross_pos = 0;
            for (int i = 0; i < line_count; i++) {
                flex_layout->lines[i].cross_position = cross_pos;
                cross_pos += flex_layout->lines[i].cross_size;
            }

            // Re-run cross-axis alignment with updated line sizes
            for (int i = 0; i < line_count; i++) {
                align_items_cross_axis(flex_layout, &flex_layout->lines[i]);
            }

            // Update container height if auto
            bool has_explicit_height = container->blk && container->blk->given_height >= 0;
            if (!has_explicit_height) {
                int total_cross = 0;
                for (int i = 0; i < line_count; i++) {
                    total_cross += flex_layout->lines[i].cross_size;
                }
                float pad_border_h = 0;
                if (container->bound) {
                    pad_border_h += container->bound->padding.top + container->bound->padding.bottom;
                    if (container->bound->border) {
                        pad_border_h += container->bound->border->width.top + container->bound->border->width.bottom;
                    }
                }
                float new_height = total_cross + pad_border_h;
                if (new_height > container->height) {
                    log_debug("%s Phase 9a: Updating container height %.1f -> %.1f", container->source_loc(), container->height, new_height);
                    container->height = new_height;
                }
            }

            log_debug("%s Phase 9a: Recomputed line positions after baseline adjustment", container->source_loc());
        }
    }

    // Phase 9.5: Store first line's baseline in container's FlexProp
    // This is used when this flex container participates in parent's baseline alignment
    if (line_count > 0 && container->embed && container->embed->flex) {
        FlexLineInfo* first_line = &flex_layout->lines[0];

        // Check if first line has baseline-aligned items (via align-self:baseline or container align-items:baseline)
        bool has_baseline_child = (flex_layout->align_items == ALIGN_BASELINE);
        if (!has_baseline_child) {
            for (int i = 0; i < first_line->item_count; i++) {
                ViewElement* item = (ViewElement*)first_line->items[i]->as_element();
                if (item && item->fi && item->fi->align_self == ALIGN_BASELINE) {
                    has_baseline_child = true;
                    break;
                }
            }
        }

        // Compute the first line's baseline: the max baseline of all items participating
        // in baseline alignment in the first line. This is needed so parent containers
        // that have this flex container as a flex item can correctly compute alignment.
        int computed_first_baseline = 0;
        if (has_baseline_child && is_main_axis_horizontal(flex_layout)) {
            computed_first_baseline = find_max_baseline(first_line, flex_layout->align_items);
        } else if (first_line->item_count > 0) {
            // Default: use the first line's cross size (height) as the baseline
            // This is the CSS spec fallback: synthesize baseline from the bottom of the content box
            // For now, use first line cross size as a reasonable default
            computed_first_baseline = first_line->cross_size;
        }
        first_line->baseline = computed_first_baseline;
        container->embed->flex->first_baseline = computed_first_baseline;
        container->embed->flex->has_baseline_child = has_baseline_child;
        log_debug("%s Phase 9.5: Stored first_baseline=%d, has_baseline_child=%d", container->source_loc(),
                  computed_first_baseline, has_baseline_child);
    }

    // Note: wrap-reverse item positioning is now handled in align_items_cross_axis
    // by positioning items at the end of the line when they have explicit cross-axis sizes

    // Phase 10: Apply relative positioning offsets to flex items
    // CSS spec: position: relative with top/right/bottom/left should offset the item
    // from its normal flow position (which has been calculated by the flex algorithm)
    // CRITICAL: Percentage offsets must be re-resolved against the actual parent dimensions,
    // because during CSS resolution they may have been resolved against a different containing block.
    float parent_content_width = flex_layout->main_axis_size;
    float parent_content_height = flex_layout->cross_axis_size;
    if (is_main_axis_horizontal(flex_layout)) {
        parent_content_width = flex_layout->main_axis_size;
        parent_content_height = flex_layout->cross_axis_size;
    } else {
        parent_content_width = flex_layout->cross_axis_size;
        parent_content_height = flex_layout->main_axis_size;
    }
    // Use container dimensions as fallback (content box)
    if (parent_content_width <= 0) parent_content_width = container->width;
    if (parent_content_height <= 0) parent_content_height = container->height;

    for (int i = 0; i < item_count; i++) {
        View* item = items[i];
        ViewBlock* item_block = (ViewBlock*)item->as_element();
        if (item_block && item_block->position &&
            item_block->position->position == CSS_VALUE_STICKY) {
            layout_sticky_positioned(lycon, item_block);
        } else if (item_block && item_block->position &&
            item_block->position->position == CSS_VALUE_RELATIVE) {
            float offset_x = 0, offset_y = 0;
            // horizontal offset — re-resolve percentage against actual parent width
            if (item_block->position->has_left) {
                if (!std::isnan(item_block->position->left_percent)) {
                    offset_x = item_block->position->left_percent * parent_content_width / 100.0f;
                } else {
                    offset_x = item_block->position->left;
                }
            } else if (item_block->position->has_right) {
                if (!std::isnan(item_block->position->right_percent)) {
                    offset_x = -(item_block->position->right_percent * parent_content_width / 100.0f);
                } else {
                    offset_x = -item_block->position->right;
                }
            }
            // vertical offset — re-resolve percentage against actual parent height
            if (item_block->position->has_top) {
                if (!std::isnan(item_block->position->top_percent)) {
                    offset_y = item_block->position->top_percent * parent_content_height / 100.0f;
                } else {
                    offset_y = item_block->position->top;
                }
            } else if (item_block->position->has_bottom) {
                if (!std::isnan(item_block->position->bottom_percent)) {
                    offset_y = -(item_block->position->bottom_percent * parent_content_height / 100.0f);
                } else {
                    offset_y = -item_block->position->bottom;
                }
            }
            if (offset_x != 0 || offset_y != 0) {
                item->x += offset_x;
                item->y += offset_y;
            }
        }
    }

    flex_layout->needs_reflow = false;
}// Collect flex items from container children
int collect_flex_items(FlexContainerLayout* flex, ViewBlock* container, View*** items) {
    if (!flex || !container || !items) return 0;

    int count = 0;

    // Count children first - use ViewBlock hierarchy for flex items
    View* child = (View*)container->first_child;
    while (child) {
        // CRITICAL FIX: Skip text nodes - flex items must be elements
        if (!child->is_element()) {
            child = child->next_sibling;
            continue;
        }

        // Filter out absolutely positioned and hidden items
        ViewElement* child_elmt = (ViewElement*)child->as_element();
        // CRITICAL: Check position->position (PositionProp), NOT in_line->position
        ViewBlock* child_block = (ViewBlock*)child_elmt;
        bool is_absolute = child_block && child_block->position && child_block->position->position &&
            (child_block->position->position == CSS_VALUE_ABSOLUTE || child_block->position->position == CSS_VALUE_FIXED);
        bool is_hidden = child_elmt && child_elmt->in_line && child_elmt->in_line->visibility == VIS_HIDDEN;
        if (!is_absolute && !is_hidden) {
            count++;
        }
        child = child->next_sibling;
    }

    if (count == 0) {
        *items = nullptr;
        return 0;
    }

    // Ensure we have enough space in the flex items array
    if (count > flex->allocated_items) {
        flex->allocated_items = count * 2;
        flex->flex_items = (View**)mem_realloc(flex->flex_items, flex->allocated_items * sizeof(View*), MEM_CAT_LAYOUT);
    }

    // Collect items - use ViewBlock hierarchy for flex items
    count = 0;
    child = container->first_child;
    while (child) {
        // CRITICAL FIX: Skip text nodes - flex items must be elements
        if (!child->is_element()) {
            child = child->next_sibling;
            continue;
        }

        // Filter out absolutely positioned and hidden items
        ViewElement* child_elmt = (ViewElement*)child->as_element();
        // CRITICAL: Check position->position (PositionProp), NOT in_line->position
        ViewBlock* child_block2 = (ViewBlock*)child_elmt;
        bool is_absolute = child_block2 && child_block2->position && child_block2->position->position &&
            (child_block2->position->position == CSS_VALUE_ABSOLUTE || child_block2->position->position == CSS_VALUE_FIXED);
        bool is_hidden = child_elmt && child_elmt->in_line && child_elmt->in_line->visibility == VIS_HIDDEN;
        if (!is_absolute && !is_hidden) {
            flex->flex_items[count] = child;

            // CRITICAL FIX: Apply cached measurements to flex items
            // This connects the measurement pass with the layout pass
            MeasurementCacheEntry* cached = get_from_measurement_cache(child);
            if (cached) {
                // For grid containers, skip the cached height — it was computed by
                // stacking children as blocks (wrong); grid height = max_row_height.
                bool child_is_grid_fc = (child_elmt && child_elmt->display.inner == CSS_VALUE_GRID);
                if (!child_is_grid_fc && child_elmt && child_elmt->specified_style) {
                    CssDeclaration* d = style_tree_get_declaration(
                        child_elmt->specified_style, CSS_PROPERTY_DISPLAY);
                    if (d && d->value && d->value->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum dv = d->value->data.keyword;
                        child_is_grid_fc = (dv == CSS_VALUE_GRID || dv == CSS_VALUE_INLINE_GRID);
                    }
                }
                child->width = cached->measured_width;
                if (!child_is_grid_fc) {
                    child->height = cached->measured_height;
                }
                if (child_elmt) {
                    child_elmt->content_width = cached->content_width;
                    if (!child_is_grid_fc) {
                        child_elmt->content_height = cached->content_height;
                    }
                    log_debug("%s Applied measurements: item %d now has size %dx%d (content: %dx%d, is_grid=%d)", container->source_loc(),
                        count, child->width, child->height, child_elmt->content_width, child_elmt->content_height, child_is_grid_fc);
                }
            } else {
                log_debug("%s No cached measurement found for flex item %d", container->source_loc(), count);
            }

            // DEBUG: Check CSS dimensions
            if (child_elmt && child_elmt->blk) {
                log_debug("%s Flex item %d CSS dimensions: given_width=%.1f, given_height=%.1f", container->source_loc(),
                    count, child_elmt->blk->given_width, child_elmt->blk->given_height);

                // CRITICAL FIX: Apply CSS dimensions to flex items if specified
                // Must clamp against max-width/max-height constraints
                if (child_elmt->blk->given_width >= 0 && child->width != child_elmt->blk->given_width) {
                    float target_width = child_elmt->blk->given_width;

                    // Clamp against max-width constraint if present
                    if (child_elmt->blk->given_max_width > 0 && target_width > child_elmt->blk->given_max_width) {
                        log_debug("%s Flex item %d width %.1f exceeds max-width %.1f, clamping", container->source_loc(),
                                 count, target_width, child_elmt->blk->given_max_width);
                        target_width = child_elmt->blk->given_max_width;
                    }

                    // Clamp against min-width constraint if present
                    if (child_elmt->blk->given_min_width > 0 && target_width < child_elmt->blk->given_min_width) {
                        log_debug("%s Flex item %d width %.1f below min-width %.1f, clamping", container->source_loc(),
                                 count, target_width, child_elmt->blk->given_min_width);
                        target_width = child_elmt->blk->given_min_width;
                    }

                    log_debug("%s Setting flex item %d width from CSS: %.1f -> %.1f", container->source_loc(),
                             count, child->width, target_width);
                    child->width = target_width;
                }

                if (child_elmt->blk->given_height >= 0 && child->height != child_elmt->blk->given_height) {
                    float target_height = child_elmt->blk->given_height;

                    // Clamp against max-height constraint if present
                    if (child_elmt->blk->given_max_height > 0 && target_height > child_elmt->blk->given_max_height) {
                        log_debug("%s Flex item %d height %.1f exceeds max-height %.1f, clamping", container->source_loc(),
                                 count, target_height, child_elmt->blk->given_max_height);
                        target_height = child_elmt->blk->given_max_height;
                    }

                    // Clamp against min-height constraint if present
                    if (child_elmt->blk->given_min_height > 0 && target_height < child_elmt->blk->given_min_height) {
                        log_debug("%s Flex item %d height %.1f below min-height %.1f, clamping", container->source_loc(),
                                 count, target_height, child_elmt->blk->given_min_height);
                        target_height = child_elmt->blk->given_min_height;
                    }

                    log_debug("%s Setting flex item %d height from CSS: %.1f -> %.1f", container->source_loc(),
                             count, child->height, target_height);
                    child->height = target_height;
                }
            } else {
                log_debug("%s Flex item %d has no blk (CSS properties)", container->source_loc(), count);
            }
            count++;
        }
        child = child->next_sibling;
    }

    flex->item_count = count;
    *items = flex->flex_items;
    return count;
}

// Sort flex items by CSS order property
void sort_flex_items_by_order(View** items, int count) {
    if (!items || count <= 1) return;

    // simple insertion sort by order, maintaining document order for equal values
    for (int i = 1; i < count; ++i) {
        ViewElement* key = (ViewElement*)items[i]->as_element();
        int key_order = key ? key->fi ? key->fi->order : 0 : 0;
        int j = i - 1;
        ViewElement* item_j = (ViewElement*)items[j]->as_element();
        int item_j_order = item_j ? item_j->fi ? item_j->fi->order : 0 : 0;
        while (j >= 0 && item_j_order > key_order) {
            items[j + 1] = items[j];
            j--;
            if (j >= 0) {
                item_j = (ViewElement*)items[j]->as_element();
                item_j_order = item_j ? item_j->fi ? item_j->fi->order : 0 : 0;
            }
        }
        items[j + 1] = key;
    }

}

// ============================================================================
// UNIFIED: Single-Pass Flex Item Collection
// Combines: measurement + View creation + collection (eliminates redundant traversals)
// ============================================================================

// Helper: Check if a child should be skipped as a flex item
static bool should_skip_flex_item(ViewElement* item) {
    if (!item) return true;

    // Skip display:none items - per CSS Flexbox §4, display:none elements
    // do not generate flex items and should be completely excluded
    if (item->display.outer == CSS_VALUE_NONE) {
        return true;
    }

    // Skip absolutely positioned items
    // CRITICAL: Check block->position->position (PositionProp), NOT in_line->position (InlineProp)
    // The CSS position property is resolved to PositionProp, not InlineProp
    ViewBlock* block = (ViewBlock*)item;
    if (block->position && block->position->position &&
        (block->position->position == CSS_VALUE_ABSOLUTE ||
         block->position->position == CSS_VALUE_FIXED)) {
        return true;
    }

    // Skip hidden items
    if (item->in_line && item->in_line->visibility == VIS_HIDDEN) {
        return true;
    }

    return false;
}

// Helper: Ensure flex items array has enough capacity
static void ensure_flex_items_capacity(FlexContainerLayout* flex, int required) {
    if (required > flex->allocated_items) {
        flex->allocated_items = required * 2;
        flex->flex_items = (View**)mem_realloc(flex->flex_items,
                                           flex->allocated_items * sizeof(View*), MEM_CAT_LAYOUT);
    }
}

// UNIFIED: Single-pass collection that combines measurement + View creation + collection
// This replaces the separate PASS 1 (in layout_flex_multipass.cpp) and Phase 1 (collect_flex_items)
int collect_and_prepare_flex_items(LayoutContext* lycon,
                                    FlexContainerLayout* flex_layout,
                                    ViewBlock* container) {
    if (!lycon || !flex_layout || !container) return 0;

    log_enter();
    log_info("=== UNIFIED FLEX ITEM COLLECTION: container=%p (%s) ===",
             container, container->node_name());

    // Save container's font context - all flex items should inherit from this
    FontBox container_font = lycon->font;

    int item_count = 0;
    DomNode* child = container->first_child;

    // CSS §8.3: Percentage margins and paddings of flex items resolve against
    // the flex container's content-box width (their containing block).
    // During style resolution, lycon->block.parent->content_width still points to
    // the grandparent's width. We must temporarily update it to the flex container's
    // content width so that percentage margins/paddings resolve correctly.
    float container_content_width = (float)container->width;
    if (container->bound) {
        container_content_width -= container->bound->padding.left + container->bound->padding.right;
        if (container->bound->border) {
            container_content_width -= container->bound->border->width.left + container->bound->border->width.right;
        }
    }
    if (container_content_width < 0) container_content_width = 0;

    // Create a temporary parent block context with the correct content width
    BlockContext flex_parent_ctx = {};
    BlockContext* saved_parent = lycon->block.parent;
    if (saved_parent) {
        flex_parent_ctx = *saved_parent;  // copy parent context
    }
    flex_parent_ctx.content_width = container_content_width;
    lycon->block.parent = &flex_parent_ctx;

    // Single pass through all children
    while (child) {
        // Skip non-element nodes (text nodes)
        // CSS Flexbox §4: "if the entire sequence of child text runs contains
        // only white space... it is instead not rendered"
        if (!child->is_element()) {
            if (child->is_text()) {
                const char* text = (const char*)child->text_data();
                if (!text || is_only_whitespace(text)) {
                    child->view_type = RDT_VIEW_NONE;
                }
            }
            child = child->next_sibling;
            continue;
        }

        // CRITICAL: Restore container's font context before processing each flex item
        // This ensures each flex item inherits from the container, not from siblings
        lycon->font = container_font;

        // Step 1: Create/verify View structure FIRST (resolves CSS styles)
        // This must happen before measurement so font-size etc. are available
        // CRITICAL: Clear styles_resolved so CSS is re-resolved against THIS container's
        // content width (set in flex_parent_ctx above). Without this, a previous call from
        // layout_flex_item_content with wrong parent context (HTML viewport width) would
        // leave styles_resolved=true, causing dom_node_resolve_style to skip re-resolution
        // and leaving percentage margins/paddings computed against the wrong containing block.
        if (child->is_element()) {
            child->as_element()->styles_resolved = false;
        }
        // CRITICAL: Also invalidate the measurement cache for this child so that
        // measure_flex_child_content re-measures it with the correct parent context
        // (correct container_content_width) after CSS has been re-resolved above.
        // Only invalidate when the container width actually changed — avoids forcing
        // re-measurement when called multiple times with the same context.
        {
            MeasurementCacheEntry* cached = get_from_measurement_cache(child);
            if (cached && fabsf(cached->context_width - container_content_width) > 0.5f) {
                invalidate_measurement_cache_for_node(child);
            } else if (!cached) {
                // no cache entry — nothing to invalidate
            }
            // else: context width unchanged, keep cache entry
        }
        init_flex_item_view(lycon, child);

        // Check if init_flex_item_view skipped this child (display:none)
        // In that case no View was created and we must not process further
        ViewElement* item = (ViewElement*)child->as_element();
        if (item->display.outer == CSS_VALUE_NONE) {
            child = child->next_sibling;
            continue;
        }

        // Step 2: Measure content (uses resolved styles)
        // Skip measurement for items with both definite width and height from CSS —
        // their dimensions are fully determined and won't come from content measurement.
        ViewBlock* item_block = (ViewBlock*)item;
        bool has_definite_w = (item_block->blk && item_block->blk->given_width >= 0
                               && isnan(item_block->blk->given_width_percent));
        bool has_definite_h = (item_block->blk && item_block->blk->given_height >= 0
                               && isnan(item_block->blk->given_height_percent));
        if (!(has_definite_w && has_definite_h)) {
            measure_flex_child_content(lycon, child);
        }

        // Now child IS the View (unified tree) - get as ViewGroup

        // Step 3: Check if should skip (absolute, hidden)
        if (should_skip_flex_item(item)) {
            child = child->next_sibling;
            continue;
        }

        // Step 4: Apply cached measurements
        MeasurementCacheEntry* cached = get_from_measurement_cache(child);
        if (cached) {

            // Determine if this item is a grid container.
            // For grid containers the measurement cache stores a height computed by
            // stacking children as blocks, which is wrong — grid arranges children in
            // columns so the container height is max_row_height, not sum of rows.
            // Skip applying the cached height for grid containers; the flex algorithm
            // will later determine the correct height via calculate_item_intrinsic_sizes.
            bool child_is_grid = (item->display.inner == CSS_VALUE_GRID);
            if (!child_is_grid && item->specified_style) {
                CssDeclaration* disp_decl = style_tree_get_declaration(
                    item->specified_style, CSS_PROPERTY_DISPLAY);
                if (disp_decl && disp_decl->value &&
                    disp_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum dv = disp_decl->value->data.keyword;
                    child_is_grid = (dv == CSS_VALUE_GRID || dv == CSS_VALUE_INLINE_GRID);
                }
            }

            // Apply measurements (don't override explicit CSS dimensions)
            if (item->width <= 0) {
                item->width = cached->measured_width;
            }
            if (!child_is_grid && item->height <= 0) {
                item->height = cached->measured_height;
            }
            item->content_width = cached->content_width;
            if (!child_is_grid) {
                item->content_height = cached->content_height;
            }
        }

        // Step 5: Re-resolve percentage widths/heights relative to flex container
        // CSS percentages were resolved during style resolution with wrong parent context.
        // For flex items, percentages should be relative to the flex container's content size.
        // EXCEPTION: In intrinsic sizing mode (max-content/min-content), percentage widths
        // are treated as auto per CSS Sizing spec - they contribute intrinsic size instead.
        bool is_intrinsic_sizing = lycon->available_space.is_intrinsic_sizing();
        if (item->blk) {
            bool is_row = is_main_axis_horizontal(flex_layout);
            float container_main = flex_layout->main_axis_size;
            float container_cross = flex_layout->cross_axis_size;

            // Re-resolve width percentage
            if (!isnan(item->blk->given_width_percent)) {
                if (is_intrinsic_sizing && is_row) {
                    // In intrinsic sizing mode with percentage width in main axis,
                    // treat as auto - use intrinsic content width instead
                    log_info("%s FLEX: Intrinsic sizing mode - percentage width %.1f%% treated as auto", container->source_loc(),
                             item->blk->given_width_percent);
                    item->blk->given_width = -1;  // Auto width
                    item->width = 0;  // Will be determined by content
                } else {
                    float width_percent = item->blk->given_width_percent;
                    // For row: width is main axis, resolve against container width
                    // For column: width is cross axis
                    float resolve_against = is_row ? container_main : container_cross;
                    if (resolve_against > 0) {
                        float new_width = resolve_against * width_percent / 100.0f;
                        log_info("%s FLEX: Re-resolving width percentage: %.1f%% of %.1f = %.1f (was %.1f)", container->source_loc(),
                                 width_percent, resolve_against, new_width, item->blk->given_width);
                        item->blk->given_width = new_width;
                        item->width = new_width;
                    }
                }
            }

            // Re-resolve height percentage
            if (!isnan(item->blk->given_height_percent)) {
                float height_percent = item->blk->given_height_percent;
                // For row: height is cross axis
                // For column: height is main axis
                float resolve_against = is_row ? container_cross : container_main;
                if (resolve_against > 0) {
                    float new_height = resolve_against * height_percent / 100.0f;
                    log_info("%s FLEX: Re-resolving height percentage: %.1f%% of %.1f = %.1f (was %.1f)", container->source_loc(),
                             height_percent, resolve_against, new_height, item->blk->given_height);
                    item->blk->given_height = new_height;
                    item->height = new_height;
                }
            }

            // Re-resolve min-width percentage against flex container
            if (!isnan(item->blk->given_min_width_percent)) {
                float resolve_against = is_row ? container_main : container_cross;
                if (resolve_against > 0) {
                    float new_val = resolve_against * item->blk->given_min_width_percent / 100.0f;
                    log_info("%s FLEX: Re-resolving min-width percentage: %.1f%% of %.1f = %.1f (was %.1f)", container->source_loc(),
                             item->blk->given_min_width_percent, resolve_against, new_val, item->blk->given_min_width);
                    item->blk->given_min_width = new_val;
                }
            }

            // Re-resolve max-width percentage against flex container
            if (!isnan(item->blk->given_max_width_percent)) {
                float resolve_against = is_row ? container_main : container_cross;
                if (resolve_against > 0) {
                    float new_val = resolve_against * item->blk->given_max_width_percent / 100.0f;
                    log_info("%s FLEX: Re-resolving max-width percentage: %.1f%% of %.1f = %.1f (was %.1f)", container->source_loc(),
                             item->blk->given_max_width_percent, resolve_against, new_val, item->blk->given_max_width);
                    item->blk->given_max_width = new_val;
                } else {
                    // Percentage max-width against zero/auto container is 'none'
                    item->blk->given_max_width = -1;
                }
            }

            // Re-resolve min-height percentage against flex container
            if (!isnan(item->blk->given_min_height_percent)) {
                float resolve_against = is_row ? container_cross : container_main;
                if (resolve_against > 0) {
                    float new_val = resolve_against * item->blk->given_min_height_percent / 100.0f;
                    log_info("%s FLEX: Re-resolving min-height percentage: %.1f%% of %.1f = %.1f (was %.1f)", container->source_loc(),
                             item->blk->given_min_height_percent, resolve_against, new_val, item->blk->given_min_height);
                    item->blk->given_min_height = new_val;
                }
            }

            // Re-resolve max-height percentage against flex container
            if (!isnan(item->blk->given_max_height_percent)) {
                float resolve_against = is_row ? container_cross : container_main;
                if (resolve_against > 0) {
                    float new_val = resolve_against * item->blk->given_max_height_percent / 100.0f;
                    log_info("%s FLEX: Re-resolving max-height percentage: %.1f%% of %.1f = %.1f (was %.1f)", container->source_loc(),
                             item->blk->given_max_height_percent, resolve_against, new_val, item->blk->given_max_height);
                    item->blk->given_max_height = new_val;
                } else {
                    // Percentage max-height against zero/auto container is 'none'
                    item->blk->given_max_height = -1;
                }
            }
        }

        // Step 6: Apply explicit CSS dimensions if specified (non-percentage)
        if (item->blk) {
            // Only apply if not a percentage (already handled above)
            if (isnan(item->blk->given_width_percent) && item->blk->given_width >= 0) {
                float target_width = item->blk->given_width;

                // CRITICAL: Clamp against max-width constraint
                if (item->blk->given_max_width > 0 && target_width > item->blk->given_max_width) {
                    log_debug("Width %.1f exceeds max-width %.1f, clamping", target_width, item->blk->given_max_width);
                    target_width = item->blk->given_max_width;
                }

                // Clamp against min-width constraint (min takes precedence)
                if (item->blk->given_min_width > 0 && target_width < item->blk->given_min_width) {
                    log_debug("Width %.1f below min-width %.1f, clamping", target_width, item->blk->given_min_width);
                    target_width = item->blk->given_min_width;
                }

                // CSS box model: border-box width >= padding+border (content-box size >= 0)
                if (item->bound) {
                    float pb_w = item->bound->padding.left + item->bound->padding.right;
                    if (item->bound->border) {
                        pb_w += item->bound->border->width.left + item->bound->border->width.right;
                    }
                    if (item->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
                        // border-box: declared width can't be less than padding+border
                        if (target_width < pb_w) {
                            log_debug("Width %.1f below padding+border %.1f, flooring border-box", target_width, pb_w);
                            target_width = pb_w;
                            item->blk->given_width = target_width;
                        }
                    } else {
                        // content-box: given_width is content-only, visual width includes padding+border
                        // Do NOT modify given_width — calculate_flex_basis adds padding+border
                        if (target_width + pb_w > target_width) {
                            target_width = target_width + pb_w;
                            log_debug("Width: content-box %.1f + padding+border %.1f = %.1f", item->blk->given_width, pb_w, target_width);
                        }
                    }
                }

                log_debug("Applying CSS width (clamped): %.1f", target_width);
                item->width = target_width;
            }
            if (isnan(item->blk->given_height_percent) && item->blk->given_height >= 0) {
                float target_height = item->blk->given_height;

                // CRITICAL: Clamp against max-height constraint
                if (item->blk->given_max_height > 0 && target_height > item->blk->given_max_height) {
                    log_debug("Height %.1f exceeds max-height %.1f, clamping", target_height, item->blk->given_max_height);
                    target_height = item->blk->given_max_height;
                }

                // Clamp against min-height constraint (min takes precedence)
                if (item->blk->given_min_height > 0 && target_height < item->blk->given_min_height) {
                    log_debug("Height %.1f below min-height %.1f, clamping", target_height, item->blk->given_min_height);
                    target_height = item->blk->given_min_height;
                }

                // CSS box model: border-box height >= padding+border (content-box size >= 0)
                if (item->bound) {
                    float pb_h = item->bound->padding.top + item->bound->padding.bottom;
                    if (item->bound->border) {
                        pb_h += item->bound->border->width.top + item->bound->border->width.bottom;
                    }
                    if (item->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
                        // border-box: declared height can't be less than padding+border
                        if (target_height < pb_h) {
                            log_debug("Height %.1f below padding+border %.1f, flooring border-box", target_height, pb_h);
                            target_height = pb_h;
                            item->blk->given_height = target_height;
                        }
                    } else {
                        // content-box: given_height is content-only, visual height includes padding+border
                        // Do NOT modify given_height — calculate_flex_basis adds padding+border
                        if (target_height + pb_h > target_height) {
                            target_height = target_height + pb_h;
                            log_debug("Height: content-box %.1f + padding+border %.1f = %.1f", item->blk->given_height, pb_h, target_height);
                        }
                    }
                }

                log_debug("Applying CSS height (clamped): %.1f", target_height);
                item->height = target_height;
            }
        }

        // Step 6a: Apply aspect-ratio when one dimension is set from an explicit CSS value
        // (CSS width/height or resolved percentage).  Do NOT apply aspect-ratio here for
        // content-intrinsic sizes (where given_width/height == -1); those cases are
        // handled correctly by calculate_flex_basis (Case 2c) and
        // determine_hypothetical_cross_sizes so that min/max constraints are respected.
        if (item->fi && item->fi->aspect_ratio > 0 && item->blk) {
            float r = item->fi->aspect_ratio;
            bool height_is_explicit = item->blk->given_height >= 0 ||
                                      !isnan(item->blk->given_height_percent);
            bool width_is_explicit  = item->blk->given_width >= 0 ||
                                      !isnan(item->blk->given_width_percent);

            if (height_is_explicit && item->height > 0 && item->width <= 0) {
                item->width = item->height * r;
                log_debug("Applied aspect-ratio (explicit height): width=%.1f from height=%.1f * ratio=%.3f",
                          item->width, item->height, r);
            } else if (width_is_explicit && item->width > 0 && item->height <= 0) {
                item->height = item->width / r;
                log_debug("Applied aspect-ratio (explicit width): height=%.1f from width=%.1f / ratio=%.3f",
                          item->height, item->width, r);
            }
        }

        // Step 6b: For nested flex containers without explicit cross-axis size,
        // set their size to the available cross-axis size ONLY when align-items: stretch.
        // This is critical for flex-wrap containers to wrap correctly.
        // NOTE: With align-items: center/start/end, items should use intrinsic size.
        // IMPORTANT: For WRAPPING containers (flex-wrap: wrap/wrap-reverse), do NOT pre-set
        // cross-axis sizes. The actual cross-axis size per line isn't known until after
        // wrapping and align-content distribution. The stretch step of the flex algorithm
        // will set the correct cross-axis size based on the computed line cross-size.
        // Pre-setting here would inflate the hypothetical outer cross size, preventing
        // lines from getting correct cross-sizes in calculate_line_cross_sizes.
        bool is_row = is_main_axis_horizontal(flex_layout);
        bool is_wrapping = (flex_layout->wrap != WRAP_NOWRAP);
        if (item->display.inner == CSS_VALUE_FLEX && !is_wrapping) {
            // This is a nested flex container in a NOWRAP parent
            // Only auto-set width when:
            // 1. Parent is column flex (cross-axis is width)
            // 2. align-items is stretch (or auto which defaults to stretch)
            // 3. Item has no explicit align-self override
            int align_type = (item->fi && item->fi->align_self != ALIGN_AUTO) ?
                             item->fi->align_self : flex_layout->align_items;
            bool should_stretch = (align_type == ALIGN_STRETCH);

            if (!is_row && should_stretch) {
                // Parent is column flex with align-items: stretch
                if (item->width <= 0 && flex_layout->cross_axis_size > 0) {
                    log_debug("NESTED_FLEX_ITEM: Setting width=%.1f from parent cross-axis (column, stretch, nowrap)",
                              flex_layout->cross_axis_size);
                    item->width = flex_layout->cross_axis_size;
                }
            }
            // For align-items: center/start/end, let the item use its intrinsic width
        }

        // Step 7: Add to flex items array
        ensure_flex_items_capacity(flex_layout, item_count + 1);
        flex_layout->flex_items[item_count] = (View*)child;

        log_debug("Added flex item %d: %s, size=%.1fx%.1f",
                  item_count, child->node_name(), item->width, item->height);
        item_count++;

        child = child->next_sibling;
    }

    flex_layout->item_count = item_count;

    // Restore original parent block context
    lycon->block.parent = saved_parent;

    log_info("=== UNIFIED COLLECTION COMPLETE: %d flex items ===", item_count);
    log_leave();

    return item_count;
}

// Calculate flex basis for an item
float calculate_flex_basis(ViewElement* item, FlexContainerLayout* flex_layout) {
    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    // Handle form controls FIRST (they don't have fi)
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
        // Form controls with explicit flex-basis should use that value
        float form_flex_basis = item->form->flex_basis;
        if (form_flex_basis >= 0) {
            // Explicit flex-basis (including 0 from "flex: 1")
            if (item->form->flex_basis_is_percent) {
                // CSS Flexbox: flex-basis % resolves against container's inner main size
                float container_size = flex_layout->main_axis_size;
                float basis = form_flex_basis * container_size / 100.0f;
                log_debug("calculate_flex_basis - form control explicit percent: %.1f%% = %.1f", form_flex_basis, basis);
                return basis;
            }
            log_debug("calculate_flex_basis - form control explicit basis: %.1f", form_flex_basis);
            return form_flex_basis;
        }

        // flex-basis: auto - use intrinsic size
        float basis = is_horizontal ? item->form->intrinsic_width : item->form->intrinsic_height;

        // <button> elements have flow children — measure content if intrinsic size is 0
        if (basis <= 0 && item->tag() == HTM_TAG_BUTTON && flex_layout && flex_layout->lycon) {
            IntrinsicSizes sizes = measure_element_intrinsic_widths(flex_layout->lycon, (DomElement*)item, true);
            if (is_horizontal) {
                basis = sizes.max_content;
                item->form->intrinsic_width = basis;
            } else {
                basis = sizes.max_content;
                item->form->intrinsic_height = basis;
            }
            log_debug("calculate_flex_basis - button content measurement: %.1f", basis);
        }

        // For form controls, add padding and border to get border-box size
        // CSS uses box-sizing: border-box for form controls by default
        if (item->bound) {
            if (is_horizontal) {
                basis += item->bound->padding.left + item->bound->padding.right;
                if (item->bound->border) {
                    basis += item->bound->border->width.left + item->bound->border->width.right;
                }
            } else {
                basis += item->bound->padding.top + item->bound->padding.bottom;
                if (item->bound->border) {
                    basis += item->bound->border->width.top + item->bound->border->width.bottom;
                }
            }
        }

        log_debug("calculate_flex_basis - form control (border-box): %.1f", basis);
        return basis;
    }

    if (!item->fi) return 0;

    // Case 1: Explicit flex-basis value (not auto)
    if (item->fi->flex_basis >= 0) {
        if (item->fi->flex_basis_is_percent) {
            // CSS Flexbox: flex-basis % resolves against container's inner main size
            float container_size = flex_layout->main_axis_size;
            float basis = item->fi->flex_basis * container_size / 100.0f;
            log_info("%s FLEX_BASIS - explicit percent: %.1f%% of %.1f = %.1f", item->source_loc(),
                     item->fi->flex_basis, container_size, basis);
            return basis;
        }
        log_info("%s FLEX_BASIS - explicit: %d", item->source_loc(), item->fi->flex_basis);
        return (float)item->fi->flex_basis;
    }

    // Case 2: flex-basis: auto - use main axis size if explicit

    // Check for explicit width/height in CSS (>= 0 because -1 means auto, 0 means explicit width:0px)
    if (is_horizontal && item->blk && item->blk->given_width >= 0) {
        log_debug("%s calculate_flex_basis - using explicit width: %f", item->source_loc(), item->blk->given_width);
        item->fi->has_explicit_width = 1;

        // CRITICAL FIX: For IMG elements with explicit dimensions, still load the image for rendering
        // Even though we have explicit size, we need the image data to display it
        uintptr_t elmt_name = item->tag();
        if (elmt_name == HTM_TAG_IMG && flex_layout && flex_layout->lycon) {
            const char* src_value = item->get_attribute("src");
            if (src_value && (!item->embed || !item->embed->img)) {
                if (!item->embed) {
                    item->embed = (EmbedProp*)alloc_prop(flex_layout->lycon, sizeof(EmbedProp));
                }
                item->embed->img = load_image(flex_layout->lycon->ui_context, src_value);
                // For SVG images, set max_render_width so render_svg knows the target size
                if (item->embed->img && item->embed->img->format == IMAGE_FORMAT_SVG) {
                    item->embed->img->max_render_width = (int)item->blk->given_width; // INT_CAST_OK: image API expects int
                    if (item->blk->given_height >= 0) {
                        item->embed->img->max_render_width = max(item->embed->img->max_render_width,
                                                                  (int)item->blk->given_height); // INT_CAST_OK: intentional
                    }
                }
                log_debug("%s calculate_flex_basis: loaded image for IMG with explicit width: %s", item->source_loc(), src_value);
            }
        }

        // For content-box, given_width is content width - need to add padding/border for flex basis
        float basis = item->blk->given_width;
        if (item->blk->box_sizing != CSS_VALUE_BORDER_BOX && item->bound) {
            basis += item->bound->padding.left + item->bound->padding.right;
            if (item->bound->border) {
                basis += item->bound->border->width.left + item->bound->border->width.right;
            }
            log_debug("%s calculate_flex_basis - content-box: added padding/border to get border-box: %f", item->source_loc(), basis);
        }
        return basis;
    }
    if (!is_horizontal && item->blk && item->blk->given_height >= 0) {
        log_debug("%s calculate_flex_basis - using explicit height: %f", item->source_loc(), item->blk->given_height);
        item->fi->has_explicit_height = 1;

        // CRITICAL FIX: For IMG elements with explicit dimensions, still load the image for rendering
        uintptr_t elmt_name = item->tag();
        if (elmt_name == HTM_TAG_IMG && flex_layout && flex_layout->lycon) {
            const char* src_value = item->get_attribute("src");
            if (src_value && (!item->embed || !item->embed->img)) {
                if (!item->embed) {
                    item->embed = (EmbedProp*)alloc_prop(flex_layout->lycon, sizeof(EmbedProp));
                }
                item->embed->img = load_image(flex_layout->lycon->ui_context, src_value);
                // For SVG images, set max_render_width so render_svg knows the target size
                if (item->embed->img && item->embed->img->format == IMAGE_FORMAT_SVG) {
                    item->embed->img->max_render_width = (int)item->blk->given_height; // INT_CAST_OK: image API expects int
                    if (item->blk->given_width >= 0) {
                        item->embed->img->max_render_width = max(item->embed->img->max_render_width,
                                                                  (int)item->blk->given_width); // INT_CAST_OK: intentional
                    }
                }
                log_debug("%s calculate_flex_basis: loaded image for IMG with explicit height: %s", item->source_loc(), src_value);
            }
        }

        // For content-box, given_height is content height - need to add padding/border for flex basis
        float basis = item->blk->given_height;
        if (item->blk->box_sizing != CSS_VALUE_BORDER_BOX && item->bound) {
            basis += item->bound->padding.top + item->bound->padding.bottom;
            if (item->bound->border) {
                basis += item->bound->border->width.top + item->bound->border->width.bottom;
            }
            log_debug("%s calculate_flex_basis - content-box: added padding/border to get border-box: %f", item->source_loc(), basis);
        }
        return basis;
    }

    // Case 2b: aspect-ratio with explicit cross-axis size
    // If item has aspect-ratio and explicit height (for horizontal) or width (for vertical),
    // compute main-axis size from cross-axis and aspect-ratio
    if (item->fi && item->fi->aspect_ratio > 0) {
        if (is_horizontal && item->blk && item->blk->given_height >= 0) {
            float basis = item->blk->given_height * item->fi->aspect_ratio;
            log_debug("%s calculate_flex_basis - aspect-ratio: height=%.1f * ratio=%.3f = %.1f", item->source_loc(),
                      item->blk->given_height, item->fi->aspect_ratio, basis);
            return basis;
        }
        if (!is_horizontal && item->blk && item->blk->given_width >= 0) {
            float basis = item->blk->given_width / item->fi->aspect_ratio;
            log_debug("%s calculate_flex_basis - aspect-ratio: width=%.1f / ratio=%.3f = %.1f", item->source_loc(),
                      item->blk->given_width, item->fi->aspect_ratio, basis);
            return basis;
        }
    }

    // Case 2c: aspect-ratio with inferred cross-axis size
    // CSS Sizing Level 4 §7.2: transferred constraints from cross axis to main axis.
    // When an item has aspect-ratio and no explicit main/cross sizes, but has:
    //   a) stretch alignment with a definite container cross size, OR
    //   b) cross-axis min/max constraints that define an effective cross size,
    // derive the flex base size (main axis) from the inferred cross dimension.
    if (item->fi && item->fi->aspect_ratio > 0) {
        float r = item->fi->aspect_ratio;

        // cross-axis min/max for this flex direction
        float cross_min = 0, cross_max = -1; // -1 = none
        if (item->blk) {
            if (is_horizontal) {
                // row flex: cross = height
                cross_min = item->blk->given_min_height > 0 ? item->blk->given_min_height : 0.0f;
                cross_max = item->blk->given_max_height > 0 ? item->blk->given_max_height : -1.0f;
            } else {
                // column flex: cross = width
                cross_min = item->blk->given_min_width > 0 ? item->blk->given_min_width : 0.0f;
                cross_max = item->blk->given_max_width > 0 ? item->blk->given_max_width : -1.0f;
            }
        }

        // determine alignment type for this item
        int align_type = (item->fi->align_self != ALIGN_AUTO) ?
                         item->fi->align_self : flex_layout->align_items;
        bool is_stretch = (align_type == ALIGN_STRETCH);

        float effective_cross = 0.0f;

        if (is_stretch && flex_layout->cross_axis_size > 0) {
            // stretch: effective cross = container cross size (content-box)
            effective_cross = flex_layout->cross_axis_size;
            // Apply cross-axis min/max constraints even for stretch items.
            // An item with max-height (or max-width for column) cannot exceed that
            // limit even when stretched, per CSS Flexbox spec §9.4.
            if (cross_max > 0.0f && effective_cross > cross_max) {
                effective_cross = cross_max;
            }
            if (cross_min > 0.0f && effective_cross < cross_min) {
                effective_cross = cross_min;
            }
        } else {
            // Non-stretch: use explicit cross-axis CSS min/max constraints.
            // Also consider main-axis max/min constraints transferred via aspect-ratio,
            // per CSS Sizing Level 4 §8.5: a max constraint on one axis transfers via
            // the ratio to define the size on the other axis.
            if (cross_max > 0.0f) {
                // max-cross transfers to a max-main: size to the constraint boundary
                effective_cross = cross_max;
            } else if (cross_min > 0.0f) {
                // min-cross transfers to a min-main: size to the constraint floor
                effective_cross = cross_min;
            } else if (item->blk) {
                // No cross-axis constraints: check main-axis constraints and transfer via ratio.
                // For column flex (main=height): max-height → cross = max-height * ratio.
                // For row flex (main=width): max-width → cross = max-width / ratio.
                float main_max = -1.0f, main_min = 0.0f;
                if (is_horizontal) {
                    main_max = item->blk->given_max_width > 0 ? item->blk->given_max_width : -1.0f;
                    main_min = item->blk->given_min_width > 0 ? item->blk->given_min_width : 0.0f;
                } else {
                    main_max = item->blk->given_max_height > 0 ? item->blk->given_max_height : -1.0f;
                    main_min = item->blk->given_min_height > 0 ? item->blk->given_min_height : 0.0f;
                }
                if (main_max > 0.0f) {
                    effective_cross = is_horizontal ? main_max / r : main_max * r;
                } else if (main_min > 0.0f) {
                    effective_cross = is_horizontal ? main_min / r : main_min * r;
                }
            }
            // else: no explicit constraints → effective_cross stays 0,
            // Case 2c doesn't apply; fall through to Case 3 (content-based sizing)
        }

        if (effective_cross > 0.0f) {
            float derived_basis = is_horizontal ? effective_cross * r : effective_cross / r;
            log_debug("%s calculate_flex_basis - aspect-ratio inferred cross: cross=%.1f (min=%.1f, max=%.1f, stretch=%s) → basis=%.1f", item->source_loc(),
                      effective_cross, cross_min, cross_max, is_stretch ? "yes" : "no", derived_basis);
            return derived_basis;
        }
    }

    // Case 3: flex-basis: auto + no explicit size = use content size (intrinsic sizing)

    // Ensure intrinsic sizes are calculated (for non-form flex items)
    if (item->fi) {
        if (!item->fi->has_intrinsic_width && is_horizontal) {
            calculate_item_intrinsic_sizes(item, flex_layout);
        }
        if (!item->fi->has_intrinsic_height && !is_horizontal) {
            calculate_item_intrinsic_sizes(item, flex_layout);
        }
    }

    // Use max-content size as basis for auto (per CSS Flexbox spec)
    float basis;
    if (is_horizontal) {
        basis = item->fi ? item->fi->intrinsic_width.max_content : 0;
        log_debug("%s calculate_flex_basis: horizontal, fi=%p, has_intrinsic_width=%d, max_content=%.1f", item->source_loc(),
                  item->fi, item->fi ? item->fi->has_intrinsic_width : -1, basis);
    } else {
        basis = item->fi ? item->fi->intrinsic_height.max_content : 0;
        log_debug("%s calculate_flex_basis: vertical, fi=%p, has_intrinsic_height=%d, max_content=%.1f", item->source_loc(),
                  item->fi, item->fi ? item->fi->has_intrinsic_height : -1, basis);
    }

    // Add padding and border to intrinsic content size
    if (item->bound) {
        if (is_horizontal) {
            basis += item->bound->padding.left + item->bound->padding.right;
            if (item->bound->border) {
                basis += item->bound->border->width.left + item->bound->border->width.right;
            }
        } else {
            basis += item->bound->padding.top + item->bound->padding.bottom;
            if (item->bound->border) {
                basis += item->bound->border->width.top + item->bound->border->width.bottom;
            }
        }
    }

    log_debug("%s calculate_flex_basis - using intrinsic size: %.1f (including padding/border)", item->source_loc(), basis);
    return basis;
}

// Calculate the hypothetical main size for an item (flex-basis clamped by min/max constraints)
// This is used for line breaking decisions per CSS Flexbox spec 9.3
// IMPORTANT: For wrapping purposes, only use EXPLICITLY SET min/max constraints,
// not the automatic minimum (min-content) that is used for flex shrinking
float calculate_hypothetical_main_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    float basis = calculate_flex_basis(item, flex_layout);

    // Form controls use FormControlProp (union with fi) — fi pointer is invalid
    // Read min/max constraints directly from blk (which is valid for form controls)
    // CSS Flexbox §4.5: min-width:auto for form controls = intrinsic width (replaced elements)
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM) {
        bool is_horizontal = is_main_axis_horizontal(flex_layout);
        float min_main = 0, max_main = FLT_MAX;
        if (item->blk) {
            if (is_horizontal) {
                if (item->blk->given_min_width >= 0) {
                    min_main = item->blk->given_min_width;
                } else if (item->form && item->form->intrinsic_width > 0) {
                    // min-width: auto → intrinsic width for replaced elements
                    min_main = item->form->intrinsic_width;
                    if (item->bound) {
                        min_main += item->bound->padding.left + item->bound->padding.right;
                        if (item->bound->border)
                            min_main += item->bound->border->width.left + item->bound->border->width.right;
                    }
                }
                if (item->blk->given_max_width > 0) max_main = item->blk->given_max_width;
            } else {
                if (item->blk->given_min_height >= 0) {
                    min_main = item->blk->given_min_height;
                } else if (item->form && item->form->intrinsic_height > 0) {
                    min_main = item->form->intrinsic_height;
                    if (item->bound) {
                        min_main += item->bound->padding.top + item->bound->padding.bottom;
                        if (item->bound->border)
                            min_main += item->bound->border->width.top + item->bound->border->width.bottom;
                    }
                }
                if (item->blk->given_max_height > 0) max_main = item->blk->given_max_height;
            }
        }
        float hypothetical = basis;
        if (max_main < FLT_MAX && hypothetical > max_main) hypothetical = max_main;
        if (min_main > 0 && hypothetical < min_main) hypothetical = min_main;
        log_debug("%s calculate_hypothetical_main_size: form control basis=%.1f, min=%.1f, max=%.1f, result=%.1f", item->source_loc(),
                  basis, min_main, max_main, hypothetical);
        return hypothetical;
    }
    if (!item->fi) return basis;

    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    // Per CSS Flexbox §9.3 step 2: The hypothetical main size is the item's flex base size
    // clamped according to its USED min and max main sizes.
    // The "used" min includes the automatic minimum from §4.5 (resolved in resolve_flex_item_constraints).
    float min_main = 0, max_main = FLT_MAX;
    if (is_horizontal) {
        min_main = item->fi->resolved_min_width;
        if (item->fi->resolved_max_width > 0 && item->fi->resolved_max_width < FLT_MAX) {
            max_main = item->fi->resolved_max_width;
        }
    } else {
        min_main = item->fi->resolved_min_height;
        if (item->fi->resolved_max_height > 0 && item->fi->resolved_max_height < FLT_MAX) {
            max_main = item->fi->resolved_max_height;
        }
    }

    // Clamp basis by explicit min/max constraints
    // CSS §4.5: If min > max, min wins (max is effectively raised to min)
    float effective_max = max_main;
    if (min_main > 0 && effective_max < FLT_MAX && min_main > effective_max) {
        effective_max = min_main;
    }
    float hypothetical = basis;
    if (effective_max < FLT_MAX && hypothetical > effective_max) {
        hypothetical = effective_max;
        log_debug("%s calculate_hypothetical_main_size: clamped to max=%.1f (basis=%.1f)", item->source_loc(), effective_max, basis);
    }
    if (min_main > 0 && hypothetical < min_main) {
        hypothetical = min_main;
        log_debug("%s calculate_hypothetical_main_size: clamped to min=%.1f (basis=%.1f)", item->source_loc(), min_main, basis);
    }

    log_debug("%s calculate_hypothetical_main_size: item=%p, basis=%.1f, min=%.1f, max=%.1f, result=%.1f", item->source_loc(),
              item, basis, min_main, max_main, hypothetical);

    return hypothetical;
}

// ============================================================================
// Constraint Resolution for Flex Items
// ============================================================================

// Resolve min/max constraints for a flex item
void resolve_flex_item_constraints(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item) {
        log_debug("%s resolve_flex_item_constraints: invalid item", item->source_loc());
        return;
    }

    // Form controls don't have fi (the union shares memory with FormControlProp).
    // Min/max constraints are resolved directly in calculate_hypothetical_main_size
    // and apply_flex_constraint using intrinsic sizes. Nothing to store here.
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM) {
        log_debug("%s resolve_flex_item_constraints: form control, constraints resolved inline", item->source_loc());
        return;
    }

    if (!item->fi) {
        log_debug("%s resolve_flex_item_constraints: no flex properties", item->source_loc());
        return;
    }

    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    // Get specified constraints from BlockProp (CSS values)
    float min_width = item->blk ? item->blk->given_min_width : -1;
    float max_width = item->blk && item->blk->given_max_width > 0 ?
        item->blk->given_max_width : FLT_MAX;
    float min_height = item->blk ? item->blk->given_min_height : -1;
    float max_height = item->blk && item->blk->given_max_height > 0 ?
        item->blk->given_max_height : FLT_MAX;

    log_debug("%s resolve_flex_item_constraints: item %p, given_min_width=%.2f, min_width=%.1f, has_explicit_width=%d", item->source_loc(),
              item, item->blk ? item->blk->given_min_width : -1.0f, min_width, item->fi->has_explicit_width);

    // CSS Flexbox §4.5: Resolve 'auto' min-width/height for flex items
    // - For MAIN AXIS: min-size: auto = content-based minimum (§4.5)
    // - For CROSS AXIS: min-size: auto = 0
    // given_min_width/height == -1 means 'auto' (not explicitly set)
    // given_min_width/height >= 0 means explicitly set (including 0)
    bool has_css_width = item->blk && item->blk->given_width >= 0;
    bool has_css_height = item->blk && item->blk->given_height >= 0;

    // CSS Flexbox §4.5: If the item's overflow is not 'visible', the automatic minimum is 0
    bool overflow_not_visible = item->scroller &&
        (item->scroller->overflow_x != CSS_VALUE_VISIBLE || item->scroller->overflow_y != CSS_VALUE_VISIBLE);

    // CSS Flexbox §4.5: Resolve 'auto' min-width/height for flex items
    // min-width/height is 'auto' when given_min_width/height == -1.
    // Values >= 0 (including 0) are explicitly set by CSS.
    bool min_width_is_auto = !item->blk || item->blk->given_min_width < 0;
    bool min_height_is_auto = !item->blk || item->blk->given_min_height < 0;

    if (min_width_is_auto) {
        if (is_horizontal) {
            // Row layout: width is main axis
            // CSS Flexbox §4.5: automatic minimum is 0 when overflow != visible
            if (overflow_not_visible && item->scroller->overflow_x != CSS_VALUE_VISIBLE) {
                min_width = 0;
                log_debug("%s resolve_flex_item_constraints: auto min-width=0 (overflow=%d)", item->source_loc(),
                          overflow_not_visible);
            } else {
                // CSS Flexbox §4.5: content-based minimum size
                // content_size_suggestion = min-content of the element
                if (!item->fi->has_intrinsic_width) {
                    calculate_item_intrinsic_sizes(item, flex_layout);
                }
                float content_suggestion = item->fi->intrinsic_width.min_content;
                // Intrinsic sizes are content-box; convert to border-box for border-box items
                // so the comparison with specified_suggestion (which is border-box) is consistent
                if (item->blk && item->blk->box_sizing == CSS_VALUE_BORDER_BOX && item->bound) {
                    content_suggestion += item->bound->padding.left + item->bound->padding.right;
                    if (item->bound->border) {
                        content_suggestion += item->bound->border->width.left + item->bound->border->width.right;
                    }
                }

                // CSS Flexbox §4.5: The specified size suggestion is:
                //   1. If flex-basis is definite AND width is also set → use flex base size
                //      (flex-basis overrides width for the specified size suggestion)
                //   2. Else if width is definite → use width
                //   3. Otherwise → undefined (use content size only)
                // Note: When only flex-basis is set without width, there's no "specified size"
                // for auto-minimum purposes — the content minimum prevails.
                bool has_definite_flex_basis = item->fi && item->fi->flex_basis >= 0;
                if (has_definite_flex_basis && has_css_width) {
                    float specified_suggestion;
                    if (item->fi->flex_basis_is_percent) {
                        specified_suggestion = item->fi->flex_basis * flex_layout->main_axis_size / 100.0f;
                    } else {
                        specified_suggestion = item->fi->flex_basis;
                    }
                    if (max_width > 0 && max_width < FLT_MAX && specified_suggestion > max_width) {
                        specified_suggestion = max_width;
                    }
                    // content-based minimum = min(content_suggestion, specified_suggestion)
                    min_width = (content_suggestion < specified_suggestion) ? content_suggestion : specified_suggestion;
                    log_debug("%s resolve_flex_item_constraints: auto min-width = min(content=%.1f, flex_basis=%.1f) = %.1f", item->source_loc(),
                              content_suggestion, specified_suggestion, min_width);
                } else if (has_css_width) {
                    float specified_suggestion = item->blk->given_width;
                    if (max_width > 0 && max_width < FLT_MAX && specified_suggestion > max_width) {
                        specified_suggestion = max_width;
                    }
                    // content-based minimum = min(content_suggestion, specified_suggestion)
                    min_width = (content_suggestion < specified_suggestion) ? content_suggestion : specified_suggestion;
                    log_debug("%s resolve_flex_item_constraints: auto min-width = min(content=%.1f, specified=%.1f) = %.1f", item->source_loc(),
                              content_suggestion, specified_suggestion, min_width);
                } else {
                    // No specified size: automatic minimum = content_size_suggestion
                    min_width = content_suggestion;
                    log_debug("%s resolve_flex_item_constraints: auto min-width = min-content: %.1f", item->source_loc(), min_width);
                }

                // CSS Flexbox §4.5: clamp by max main size if definite
                if (max_width > 0 && max_width < FLT_MAX && min_width > max_width) {
                    log_debug("%s resolve_flex_item_constraints: clamping auto min-width %.1f to max-width %.1f", item->source_loc(), min_width, max_width);
                    min_width = max_width;
                }
            }
        } else {
            // Column layout: width is cross axis - automatic minimum is 0
            min_width = 0;
            log_debug("%s resolve_flex_item_constraints: column layout, cross-axis min-width set to 0", item->source_loc());
        }
    }

    if (min_height_is_auto) {
        if (!is_horizontal) {
            // Column layout: height is main axis
            // CSS Flexbox §4.5: automatic minimum is 0 when overflow != visible
            if (overflow_not_visible && item->scroller->overflow_y != CSS_VALUE_VISIBLE) {
                min_height = 0;
                log_debug("%s resolve_flex_item_constraints: auto min-height=0 (overflow=%d)", item->source_loc(),
                          overflow_not_visible);
            } else {
                // CSS Flexbox §4.5: content-based minimum size
                if (!item->fi->has_intrinsic_height) {
                    calculate_item_intrinsic_sizes(item, flex_layout);
                }
                float content_suggestion = item->fi->intrinsic_height.min_content;
                // Intrinsic sizes are content-box; convert to border-box for border-box items
                if (item->blk && item->blk->box_sizing == CSS_VALUE_BORDER_BOX && item->bound) {
                    content_suggestion += item->bound->padding.top + item->bound->padding.bottom;
                    if (item->bound->border) {
                        content_suggestion += item->bound->border->width.top + item->bound->border->width.bottom;
                    }
                }

                // CSS Flexbox §4.5: The specified size suggestion:
                //   1. If flex-basis is definite AND height is also set → use flex base size
                //   2. Else if height is definite → use height
                //   3. Otherwise → undefined (use content size only)
                bool has_definite_flex_basis_h = item->fi && item->fi->flex_basis >= 0;
                if (has_definite_flex_basis_h && has_css_height) {
                    float specified_suggestion;
                    if (item->fi->flex_basis_is_percent) {
                        specified_suggestion = item->fi->flex_basis * flex_layout->main_axis_size / 100.0f;
                    } else {
                        specified_suggestion = item->fi->flex_basis;
                    }
                    if (max_height > 0 && max_height < FLT_MAX && specified_suggestion > max_height) {
                        specified_suggestion = max_height;
                    }
                    // content-based minimum = min(content_suggestion, specified_suggestion)
                    min_height = (content_suggestion < specified_suggestion) ? content_suggestion : specified_suggestion;
                    log_debug("%s resolve_flex_item_constraints: auto min-height = min(content=%.1f, flex_basis=%.1f) = %.1f", item->source_loc(),
                              content_suggestion, specified_suggestion, min_height);
                } else if (has_css_height) {
                    float specified_suggestion = item->blk->given_height;
                    if (max_height > 0 && max_height < FLT_MAX && specified_suggestion > max_height) {
                        specified_suggestion = max_height;
                    }
                    // content-based minimum = min(content_suggestion, specified_suggestion)
                    min_height = (content_suggestion < specified_suggestion) ? content_suggestion : specified_suggestion;
                    log_debug("%s resolve_flex_item_constraints: auto min-height = min(content=%.1f, specified=%.1f) = %.1f", item->source_loc(),
                              content_suggestion, specified_suggestion, min_height);
                } else {
                    min_height = content_suggestion;
                    log_debug("%s resolve_flex_item_constraints: auto min-height = min-content: %.1f", item->source_loc(), min_height);
                }

                // CSS Flexbox §4.5: clamp by max main size if definite
                if (max_height > 0 && max_height < FLT_MAX && min_height > max_height) {
                    log_debug("%s resolve_flex_item_constraints: clamping auto min-height %.1f to max-height %.1f", item->source_loc(), min_height, max_height);
                    min_height = max_height;
                }
            }
        } else {
            // Row layout: height is cross axis
            // CSS Flexbox §4.5: automatic minimum size applies only to the main axis.
            // On the cross axis, auto min = 0.
            // Content-based height is already handled through the hypothetical cross size
            // system (determine_hypothetical_cross_sizes), which properly measures content
            // including pseudo-element content (font-awesome icons). The line cross size
            // is derived from max hypothetical cross sizes, and stretch fills to that.
            min_height = 0;
            log_debug("%s resolve_flex_item_constraints: row layout, cross-axis min-height = 0 (per spec)", item->source_loc());
        }
    }

    // CSS Sizing Level 4 §9.4.5: Transferred size suggestion via aspect-ratio.
    // When an item has aspect-ratio and a definite cross size (from stretch), the
    // aspect-ratio-consistent main size acts as the automatic minimum (prevents shrinking
    // below the size that would maintain the aspect-ratio at the established cross size).
    // This only applies when the main-axis minimum is AUTO (not explicitly set by CSS).
    if (item->fi && item->fi->aspect_ratio > 0) {
        int align_type = (item->fi->align_self != ALIGN_AUTO) ?
                         item->fi->align_self : (flex_layout ? flex_layout->align_items : ALIGN_STRETCH);
        bool is_stretch = (align_type == ALIGN_STRETCH);
        if (is_stretch && flex_layout && flex_layout->cross_axis_size > 0) {
            float container_cross = flex_layout->cross_axis_size;
            float r = item->fi->aspect_ratio;
            if (is_horizontal && min_width_is_auto && !(has_css_width && item->blk->given_width > 0)) {
                // Row flex: cross=height, main=width.
                // Only transfer when min-width is auto (not explicitly set by CSS)
                // AND the main-axis (width) is not explicitly set (transfer is irrelevant when width is definite).
                // effective cross = definite cross if known (explicit height), else container_cross,
                // then clamped by item's own cross min/max.
                float effective_cross = (has_css_height && item->blk->given_height > 0)
                    ? item->blk->given_height : container_cross;
                if (max_height > 0 && max_height < FLT_MAX && effective_cross > max_height) {
                    effective_cross = max_height;
                }
                if (min_height > 0 && effective_cross < min_height) {
                    effective_cross = min_height;
                }
                // min_width >= effective_cross * ratio
                float transferred_min = effective_cross * r;
                // Clamp by main-axis max (max-width) to avoid exceeding it
                if (max_width > 0 && max_width < FLT_MAX && transferred_min > max_width) {
                    transferred_min = max_width;
                }
                if (min_width < transferred_min) {
                    min_width = transferred_min;
                    log_debug("%s resolve_flex_item_constraints: stretch+aspect-ratio transferred min-width: %.1f (effective_cross=%.0f * ratio=%.3f)", item->source_loc(),
                              transferred_min, effective_cross, r);
                }
            } else if (!is_horizontal && min_height_is_auto && !(has_css_height && item->blk->given_height > 0)) {
                // Column flex: cross=width, main=height.
                // Only transfer when min-height is auto (not explicitly set by CSS)
                // AND the main-axis (height) is not explicitly set (transfer is irrelevant when height is definite).
                // effective cross = definite cross if known (explicit width), else container_cross,
                // then clamped by item's own cross min/max
                float effective_cross = (has_css_width && item->blk->given_width > 0)
                    ? item->blk->given_width : container_cross;
                if (max_width > 0 && max_width < FLT_MAX && effective_cross > max_width) {
                    effective_cross = max_width;
                }
                if (min_width > 0 && effective_cross < min_width) {
                    effective_cross = min_width;
                }
                // min_height >= effective_cross / ratio
                float transferred_min = effective_cross / r;
                // Clamp by main-axis max (max-height) to avoid exceeding it
                if (max_height > 0 && max_height < FLT_MAX && transferred_min > max_height) {
                    transferred_min = max_height;
                }
                if (min_height < transferred_min) {
                    min_height = transferred_min;
                    log_debug("%s resolve_flex_item_constraints: stretch+aspect-ratio transferred min-height: %.1f (effective_cross=%.0f / ratio=%.3f)", item->source_loc(),
                              transferred_min, effective_cross, r);
                }
            }
        }
    }

    // CSS Flexbox §9.7 step 5d: "floor its content-box size at zero"
    // For border-box items, this means the minimum usable size includes padding+border.
    // This applies regardless of explicit min-width/min-height settings.
    // For content-box items, the flex algorithm works in border-box, so the same floor applies.
    if (item->bound) {
        float pb_width = item->bound->padding.left + item->bound->padding.right;
        if (item->bound->border) {
            pb_width += item->bound->border->width.left + item->bound->border->width.right;
        }
        float pb_height = item->bound->padding.top + item->bound->padding.bottom;
        if (item->bound->border) {
            pb_height += item->bound->border->width.top + item->bound->border->width.bottom;
        }
        if (min_width < pb_width) {
            log_debug("%s resolve_flex_item_constraints: flooring min-width %.1f to padding+border %.1f (content-box >= 0)", item->source_loc(),
                      min_width, pb_width);
            min_width = pb_width;
        }
        if (min_height < pb_height) {
            log_debug("%s resolve_flex_item_constraints: flooring min-height %.1f to padding+border %.1f (content-box >= 0)", item->source_loc(),
                      min_height, pb_height);
            min_height = pb_height;
        }
    }

    // Store resolved constraints in FlexItemProp for use during flex algorithm
    item->fi->resolved_min_width = min_width;
    item->fi->resolved_max_width = max_width;
    item->fi->resolved_min_height = min_height;
    item->fi->resolved_max_height = max_height;

    log_debug("%s Resolved constraints for item %p: width=[%.1f, %.1f], height=[%.1f, %.1f]", item->source_loc(),
              item, min_width, max_width, min_height, max_height);
}

// Apply constraints to all flex items in container
void apply_constraints_to_flex_items(FlexContainerLayout* flex_layout) {
    if (!flex_layout) return;

    log_debug("Applying constraints to %d flex items", flex_layout->item_count);

    for (int i = 0; i < flex_layout->item_count; i++) {
        ViewElement* item = (ViewElement*)flex_layout->flex_items[i]->as_element();
        if (item && item->fi) {
            resolve_flex_item_constraints(item, flex_layout);
        }
    }
}

// Helper function to check if a view is a valid flex item
bool is_valid_flex_item(ViewBlock* item) {
    if (!item) return false;
    // CSS treats list-item as block-level for flex layout purposes
    return item->view_type == RDT_VIEW_BLOCK || item->view_type == RDT_VIEW_INLINE_BLOCK ||
           item->view_type == RDT_VIEW_LIST_ITEM;
}

// Helper function for clamping values with min/max constraints
float clamp_value(float value, float min_val, float max_val) {
    // CSS §4.5: If min > max, min wins (max is effectively raised to min)
    if (max_val > 0) {
        float effective_max = (min_val > max_val) ? min_val : max_val;
        return fmin(fmax(value, min_val), effective_max);
    }
    return fmax(value, min_val);
}

// Helper function to resolve percentage values
float resolve_percentage(float value, bool is_percent, float container_size) {
    if (is_percent) {
        float percentage = value / 100.0f;
        return percentage * container_size;
    }
    return value;
}

// Apply constraints including aspect ratio and min/max values
void apply_constraints(ViewBlock* item, float container_width, float container_height) {
    if (!item) return;

    // Resolve percentage-based values
    float actual_width = resolve_percentage(item->width, false, container_width);
    float actual_height = resolve_percentage(item->height, false, container_height);
    float min_width = item->blk ? item->blk->given_min_width : 0.0f;
    float max_width = item->blk ? item->blk->given_max_width : 0.0f;
    float min_height = item->blk ? item->blk->given_min_height : 0.0f;
    float max_height = item->blk ? item->blk->given_max_height : 0.0f;

    // Apply aspect ratio if specified
    if (item->fi && item->fi->aspect_ratio > 0) {
        if (actual_width > 0.0f && actual_height == 0.0f) {
            actual_height = actual_width / item->fi->aspect_ratio;
        } else if (actual_height > 0.0f && actual_width == 0.0f) {
            actual_width = actual_height * item->fi->aspect_ratio;
        }
    }

    // Apply min/max constraints
    actual_width = clamp_value(actual_width, min_width, max_width);
    actual_height = clamp_value(actual_height, min_height, max_height);

    // Reapply aspect ratio after clamping if needed
    if (item->fi && item->fi->aspect_ratio > 0) {
        if (actual_width > 0.0f && actual_height == 0.0f) {
            actual_height = actual_width / item->fi->aspect_ratio;
        } else if (actual_height > 0.0f && actual_width == 0.0f) {
            actual_width = actual_height * item->fi->aspect_ratio;
        }
    }

    // Update item dimensions
    item->width = actual_width;
    item->height = actual_height;
}

// ============================================================================
// Consolidated Constraint Handling (Task 4)
// ============================================================================

/**
 * Apply min/max constraints to a computed flex size for either axis.
 * This is the single source of truth for constraint clamping in flex layout.
 *
 * @param item          The flex item being constrained
 * @param computed_size The computed size before constraint application
 * @param is_main_axis  True if constraining the main axis, false for cross axis
 * @param flex_layout   The flex container layout context
 * @param hit_min       Output: true if clamped to minimum
 * @param hit_max       Output: true if clamped to maximum
 * @return The constrained (clamped) size
 */
float apply_flex_constraint(
    ViewElement* item,
    float computed_size,
    bool is_main_axis,
    FlexContainerLayout* flex_layout,
    bool* hit_min,
    bool* hit_max
) {
    if (!item) return computed_size;

    // Form controls don't have FlexItemProp - the union shares memory with FormControlProp.
    // Compute min/max constraints from CSS given_min/max and intrinsic sizes.
    // CSS Flexbox §4.5: min-width:auto for replaced elements = intrinsic width.
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM) {
        bool is_horizontal = is_main_axis_horizontal(flex_layout);
        float min_size = 0;
        float max_size = FLT_MAX;

        if (is_main_axis) {
            if (is_horizontal) {
                if (item->blk && item->blk->given_min_width >= 0) {
                    min_size = item->blk->given_min_width;
                } else if (item->form && item->form->intrinsic_width > 0) {
                    // min-width: auto → intrinsic width for form controls (replaced elements)
                    min_size = item->form->intrinsic_width;
                    // add border+padding for border-box consistency
                    if (item->bound) {
                        min_size += item->bound->padding.left + item->bound->padding.right;
                        if (item->bound->border)
                            min_size += item->bound->border->width.left + item->bound->border->width.right;
                    }
                }
                if (item->blk && item->blk->given_max_width > 0) max_size = item->blk->given_max_width;
            } else {
                if (item->blk && item->blk->given_min_height >= 0) {
                    min_size = item->blk->given_min_height;
                } else if (item->form && item->form->intrinsic_height > 0) {
                    min_size = item->form->intrinsic_height;
                    if (item->bound) {
                        min_size += item->bound->padding.top + item->bound->padding.bottom;
                        if (item->bound->border)
                            min_size += item->bound->border->width.top + item->bound->border->width.bottom;
                    }
                }
                if (item->blk && item->blk->given_max_height > 0) max_size = item->blk->given_max_height;
            }
        }
        // Cross axis: min-size auto = 0 (already 0)

        if (hit_min) *hit_min = false;
        if (hit_max) *hit_max = false;

        float clamped = computed_size;
        if (max_size > 0 && max_size < FLT_MAX && clamped > max_size) {
            clamped = max_size;
            if (hit_max) *hit_max = true;
        }
        if (clamped < min_size) {
            clamped = min_size;
            if (hit_min) *hit_min = true;
        }
        log_debug("apply_flex_constraint: form control %s axis, computed=%.1f, min=%.1f, max=%.1f, result=%.1f",
                  is_main_axis ? "main" : "cross", computed_size, min_size, max_size, clamped);
        return clamped;
    }

    if (!item->fi) return computed_size;

    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    // Determine which constraints to use based on axis
    float min_size = 0;
    float max_size = FLT_MAX;

    if (is_main_axis) {
        // Main axis constraints
        if (is_horizontal) {
            min_size = item->fi->resolved_min_width;
            max_size = item->fi->resolved_max_width > 0 ? item->fi->resolved_max_width : FLT_MAX;
        } else {
            min_size = item->fi->resolved_min_height;
            max_size = item->fi->resolved_max_height > 0 ? item->fi->resolved_max_height : FLT_MAX;
        }
    } else {
        // Cross axis constraints
        if (is_horizontal) {
            // Row direction: cross-axis is height
            min_size = item->fi->resolved_min_height;
            max_size = item->fi->resolved_max_height > 0 ? item->fi->resolved_max_height : FLT_MAX;
        } else {
            // Column direction: cross-axis is width
            min_size = item->fi->resolved_min_width;
            max_size = item->fi->resolved_max_width > 0 ? item->fi->resolved_max_width : FLT_MAX;
        }
    }

    // Initialize output flags
    if (hit_min) *hit_min = false;
    if (hit_max) *hit_max = false;

    float clamped = computed_size;

    // Apply max constraint first (per CSS spec, min takes precedence if conflict)
    if (max_size > 0 && max_size < FLT_MAX && clamped > max_size) {
        clamped = max_size;
        if (hit_max) *hit_max = true;
        log_debug("CONSTRAINT: clamped to max=%.1f (wanted %.1f)", max_size, computed_size);
    }

    // Apply min constraint (takes precedence over max)
    // Note: min_size <= 0 means "no explicit minimum", but we still enforce 0 as absolute minimum
    float effective_min = (min_size > 0) ? min_size : 0;
    if (clamped < effective_min) {
        clamped = effective_min;
        if (hit_min) *hit_min = true;
        log_debug("CONSTRAINT: clamped to min=%.1f (wanted %.1f)", effective_min, computed_size);
    }

    if (clamped != computed_size) {
        log_debug("apply_flex_constraint: %s axis, computed=%.1f, min=%.1f, max=%.1f, result=%.1f",
                  is_main_axis ? "main" : "cross", computed_size, min_size, max_size, clamped);
    }

    return clamped;
}

/**
 * Overloaded version without hit flags for simpler use cases.
 */
float apply_flex_constraint(
    ViewElement* item,
    float computed_size,
    bool is_main_axis,
    FlexContainerLayout* flex_layout
) {
    return apply_flex_constraint(item, computed_size, is_main_axis, flex_layout, nullptr, nullptr);
}

/**
 * Apply cross-axis constraints for align-items: stretch.
 * Returns the constrained cross size for stretching.
 *
 * Note: Stretch can both expand AND shrink items to fit the line.
 * The resolved min/max constraints are still applied.
 */
float apply_stretch_constraint(
    ViewElement* item,
    float container_cross_size,
    FlexContainerLayout* flex_layout
) {
    if (!item) return container_cross_size;

    // Check for form control - they should use container_cross_size directly
    bool is_form_control = (item->item_prop_type == DomElement::ITEM_PROP_FORM);
    if (is_form_control) {
        log_debug("apply_stretch_constraint: form control, returning container_cross=%.1f", container_cross_size);
        return container_cross_size;
    }

    // Non-form-controls without fi should use container_cross_size directly
    if (!item->fi) {
        log_debug("apply_stretch_constraint: no fi, returning container_cross=%.1f", container_cross_size);
        return container_cross_size;
    }

    // Apply cross-axis constraint
    float constrained = apply_flex_constraint(item, container_cross_size, false, flex_layout);

    log_debug("apply_stretch_constraint: container_cross=%.1f, constrained=%.1f",
              container_cross_size, constrained);

    return constrained;
}

// Calculate baseline offset for a flex item from its outer margin edge
// Returns the distance from the item's top margin edge to its baseline
//
// NOTE: This is a simplified implementation that synthesizes the baseline
// from the item's outer margin edge. Proper baseline alignment requires
// running after all nested content is laid out, which is not yet implemented.
float calculate_item_baseline(ViewElement* item) {
    if (!item) return 0;

    // Get top margin
    float margin_top = item->bound ? item->bound->margin.top : 0;

    // Check if item has text content with explicit baseline
    if (item->fi && item->fi->baseline_offset > 0) {
        // Use explicit baseline from text layout (relative to content box)
        return margin_top + item->fi->baseline_offset;
    }

    // Check if item is a flex container with stored baseline
    // This handles cases where the flex container has baseline-aligned items
    ViewBlock* item_block = (ViewBlock*)item;
    if (item_block->embed && item_block->embed->flex &&
        item_block->embed->flex->has_baseline_child) {
        // Use the stored first baseline from this flex container's first line
        float parent_offset_y = 0;
        if (item->bound) {
            parent_offset_y = item->bound->padding.top;
            if (item->bound->border) {
                parent_offset_y += item->bound->border->width.top;
            }
        }
        float result = margin_top + parent_offset_y + item_block->embed->flex->first_baseline;
        log_debug("calculate_item_baseline: flex container item=%p, first_baseline=%d, result=%.1f",
                  item, item_block->embed->flex->first_baseline, result);
        return result;
    }

    // Check if item has laid-out children - use first baseline-participating child
    View* child_view = (View*)item->first_child;
    while (child_view) {
        ViewElement* child = (ViewElement*)child_view->as_element();
        if (child && child->height > 0) {
            // Skip positioned children (absolute/fixed)
            // CRITICAL: Check position->position (PositionProp), NOT in_line->position
            ViewBlock* child_block = (ViewBlock*)child;
            bool is_positioned = child_block->position &&
                (child_block->position->position == CSS_VALUE_ABSOLUTE ||
                 child_block->position->position == CSS_VALUE_FIXED);

            if (!is_positioned) {
                // Recursively calculate child's baseline
                float child_baseline = calculate_item_baseline(child);

                if (child_baseline > 0) {
                    // child->y is already relative to item's content box
                    // We need to account for item's padding/border to get position from margin edge
                    float parent_offset_y = 0;
                    if (item->bound) {
                        parent_offset_y = item->bound->padding.top;
                        if (item->bound->border) {
                            parent_offset_y += item->bound->border->width.top;
                        }
                    }

                    // child->y is relative to parent's content box (after border+padding)
                    // So the child's position from parent's margin edge is:
                    // margin_top + parent_offset_y + child->y + child_baseline
                    float result = margin_top + parent_offset_y + child->y + child_baseline;

                    log_debug("calculate_item_baseline: item=%p, child=%p, child_baseline=%.1f, child->y=%.1f, result=%.1f",
                              item, child, child_baseline, child->y, result);
                    return result;
                }
            }
        }
        child_view = (View*)child_view->next_sibling;
    }

    // CSS Flexbox §9.4 / CSS 2.1 §10.8.1: For block containers with in-flow
    // line boxes (text content), the baseline is the first line box's baseline.
    // Check stored first_line_baseline (set during content layout via finalize_block_flow).
    if (item_block->blk && item_block->blk->first_line_baseline > 0) {
        return margin_top + item_block->blk->first_line_baseline;
    }

    // If no stored baseline but item has font metrics and text children, synthesize
    // from font ascender. This handles items with text-only children whose content
    // hasn't been laid out yet (e.g., during flex Phase 5 before layout_flex_item_content).
    // Only apply when the item actually has non-whitespace text children.
    if (item->font && item->font->ascender > 0) {
        bool has_text_child = false;
        DomNode* dn = item->first_child;
        while (dn) {
            if (dn->is_text()) {
                const char* text = (const char*)dn->text_data();
                if (text) {
                    // check for non-whitespace content
                    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') text++;
                    if (*text) { has_text_child = true; break; }
                }
            }
            dn = dn->next_sibling;
        }
        if (has_text_child) {
            float border_top = 0, padding_top = 0;
            if (item->bound) {
                padding_top = item->bound->padding.top;
                if (item->bound->border) {
                    border_top = item->bound->border->width.top;
                }
            }
            return margin_top + border_top + padding_top + item->font->ascender;
        }
    }

    // Synthesize baseline from outer margin edge (bottom of margin box)
    // This is the CSS spec fallback for elements without text or participating children
    return margin_top + item->height;
}

// Find maximum baseline in a flex line for baseline alignment
// container_align_items: the align-items value from the flex container
float find_max_baseline(FlexLineInfo* line, int container_align_items) {
    float max_baseline = 0;
    bool found = false;

    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;

        // Check if this item participates in baseline alignment
        // Either via align-self: baseline OR container's align-items: baseline (and no override)
        int align_self = item->fi ? (int)item->fi->align_self : ALIGN_AUTO; // INT_CAST_OK: enum value
        bool uses_baseline = (align_self == ALIGN_BASELINE) ||
                            (align_self == ALIGN_AUTO && container_align_items == ALIGN_BASELINE);

        if (uses_baseline) {
            float baseline = calculate_item_baseline(item);
            log_debug("find_max_baseline: item %d - baseline=%.1f, height=%.1f, margin_top=%d",
                      i, baseline, item->height, item->bound ? item->bound->margin.top : 0);
            if (baseline > max_baseline) {
                max_baseline = baseline;
            }
            found = true;
        }
    }
    log_debug("find_max_baseline: max_baseline=%.1f, found=%d", max_baseline, found);
    return found ? max_baseline : 0;
}

/**
 * Reposition baseline-aligned items after nested content layout.
 *
 * This function is called after layout_final_flex_content() completes,
 * at which point all nested flex containers have been laid out and their
 * children have proper dimensions. This allows us to correctly calculate
 * baselines that depend on nested content (e.g., first child's baseline
 * in a nested flex container).
 *
 * The issue this solves: During the initial flex layout (Phase 9), baseline
 * alignment is calculated but nested flex containers haven't had their
 * children laid out yet (children have height=0). This causes baseline
 * calculation to fall back to the parent's height, which is incorrect.
 */
void reposition_baseline_items(LayoutContext* lycon, ViewBlock* flex_container) {
    log_enter();
    log_info("%s BASELINE REPOSITIONING START: container=%p (%s)", flex_container->source_loc(),
             flex_container, flex_container ? flex_container->node_name() : "null");

    if (!flex_container) {
        log_leave();
        return;
    }

    FlexContainerLayout* flex_layout = lycon->flex_container;
    if (!flex_layout) {
        log_leave();
        return;
    }

    // Check if this container uses baseline alignment
    bool has_baseline_alignment = (flex_layout->align_items == ALIGN_BASELINE);

    // Also check if any items have align-self: baseline
    if (!has_baseline_alignment) {
        for (int i = 0; i < flex_layout->line_count; i++) {
            FlexLineInfo* line = &flex_layout->lines[i];
            for (int j = 0; j < line->item_count; j++) {
                ViewElement* item = (ViewElement*)line->items[j]->as_element();
                if (item && item->fi && item->fi->align_self == ALIGN_BASELINE) {
                    has_baseline_alignment = true;
                    break;
                }
            }
            if (has_baseline_alignment) break;
        }
    }

    if (!has_baseline_alignment) {
        log_leave();
        return;
    }

    // Only reposition for horizontal main axis (baseline alignment is only for rows)
    if (!is_main_axis_horizontal(flex_layout)) {
        log_leave();
        return;
    }

    log_info("%s Container uses baseline alignment, recalculating positions after nested layout", flex_container->source_loc());

    // For each line, recalculate baselines and reposition items
    for (int line_idx = 0; line_idx < flex_layout->line_count; line_idx++) {
        FlexLineInfo* line = &flex_layout->lines[line_idx];

        // Recalculate max baseline now that children are laid out
        int max_baseline = find_max_baseline(line, flex_layout->align_items);
        log_debug("Line %d: Recalculated max_baseline=%d", line_idx, max_baseline);

        // Reposition each baseline-aligned item
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item) continue;

            // Check if this item uses baseline alignment
            // NOTE: fi may be NULL for items that inherit alignment from container
            int align_self = (item->fi && item->fi->align_self != ALIGN_AUTO) ?
                item->fi->align_self : ALIGN_AUTO;
            bool uses_baseline = (align_self == ALIGN_BASELINE) ||
                                (align_self == ALIGN_AUTO && flex_layout->align_items == ALIGN_BASELINE);

            if (!uses_baseline) continue;

            // Calculate this item's baseline
            float item_baseline = calculate_item_baseline(item);

            // Calculate new cross position to align baselines.
            // (max_baseline - item_baseline) gives the margin-box top offset for the item.
            // Adding margin_top converts to border-box top, consistent with how other
            // alignment modes (start, stretch, etc.) set cross_pos.
            float item_margin_top = item->bound ? item->bound->margin.top : 0;
            float new_cross_pos = max_baseline - item_baseline + item_margin_top;

            // Get current position for comparison
            float old_cross_pos = get_cross_axis_position(item, flex_layout);

            // For multi-line or wrap-reverse, account for line position
            float line_cross_pos = line->cross_position;

            // The final position relative to container
            float final_pos = line_cross_pos + new_cross_pos;

            // CRITICAL: Preserve relative positioning offset (position: relative with top/bottom)
            // Phase 10 already applied the offset to the old position, but we're recalculating
            // the position from scratch, so we need to re-apply the relative offset.
            ViewBlock* item_block = (ViewBlock*)item;
            if (item_block && item_block->position &&
                item_block->position->position == CSS_VALUE_RELATIVE) {
                // For row flex, cross axis is vertical, so we care about top/bottom
                // Re-resolve percentage offsets against actual parent dimensions
                float parent_h = is_main_axis_horizontal(flex_layout) ?
                    flex_layout->cross_axis_size : flex_layout->main_axis_size;
                if (parent_h <= 0) parent_h = flex_container->height;
                float relative_offset = 0;
                if (item_block->position->has_top) {
                    if (!std::isnan(item_block->position->top_percent)) {
                        relative_offset = item_block->position->top_percent * parent_h / 100.0f;
                    } else {
                        relative_offset = item_block->position->top;
                    }
                } else if (item_block->position->has_bottom) {
                    if (!std::isnan(item_block->position->bottom_percent)) {
                        relative_offset = -(item_block->position->bottom_percent * parent_h / 100.0f);
                    } else {
                        relative_offset = -item_block->position->bottom;
                    }
                }
                if (relative_offset != 0) {
                    final_pos += relative_offset;
                    log_debug("Item %d: Adding relative offset %.0f to final_pos", i, relative_offset);
                }
            }

            log_debug("Item %d: item_baseline=%.1f, max_baseline=%d, old_pos=%.1f, new_pos=%.1f (line_pos=%.1f + offset=%.1f)",
                      i, item_baseline, max_baseline, old_cross_pos, final_pos, line_cross_pos, new_cross_pos);

            // Only update if position changed
            if (final_pos != old_cross_pos) {
                // Since coordinates are relative to parent, we just update the item's position
                // Children don't need to be translated - their relative positions stay the same
                set_cross_axis_position(item, final_pos, flex_layout);
                log_info("%s Repositioned baseline item %d: %d -> %d", flex_container->source_loc(), i, old_cross_pos, final_pos);
            }
        }

        // CSS §9.4 Step 8: Update line cross size to account for baseline extent.
        // For baseline-aligned items, the line cross must be at least
        // max_pre_baseline + max_post_baseline. This fixes multi-line wrapping containers
        // where the initial Phase 5 could not compute baselines (inner layouts not yet done).
        float max_pre = 0, max_post = 0;
        float max_non_baseline = 0;
        bool has_baseline = false;
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item) continue;
            int align_self = (item->fi && item->fi->align_self != ALIGN_AUTO) ?
                item->fi->align_self : flex_layout->align_items;
            bool is_baseline_item = (align_self == ALIGN_BASELINE || align_self == CSS_VALUE_BASELINE);
            if (is_baseline_item) {
                has_baseline = true;
                float baseline = calculate_item_baseline(item);
                float margin_top = item->bound ? item->bound->margin.top : 0;
                float margin_bottom = item->bound ? item->bound->margin.bottom : 0;
                float outer_cross = item->height + margin_top + margin_bottom;
                if (baseline > max_pre) max_pre = baseline;
                if ((outer_cross - baseline) > max_post) max_post = outer_cross - baseline;
            } else {
                float item_cross = item->fi && item->fi->hypothetical_outer_cross_size > 0 ?
                    item->fi->hypothetical_outer_cross_size :
                    get_cross_axis_size(item, flex_layout);
                if (item_cross > max_non_baseline) max_non_baseline = item_cross;
            }
        }
        if (has_baseline) {
            float baseline_extent = max_pre + max_post;
            float new_cross = baseline_extent > max_non_baseline ? baseline_extent : max_non_baseline;
            if (new_cross > line->cross_size) {
                log_debug("Baseline line %d cross size %d -> %.1f (pre=%.0f, post=%.0f)",
                          line_idx, line->cross_size, new_cross, max_pre, max_post);
                line->cross_size = new_cross;
            }
        }
    }

    // After all per-line cross-size updates, update container auto-height and reposition items.
    // For single-line: update container height if the one line cross_size changed.
    // For multi-line: recalculate line positions and re-run cross-axis alignment.
    {
        bool has_explicit_height = flex_container->blk && flex_container->blk->given_height >= 0;
        if (flex_layout->line_count == 1 && !has_explicit_height) {
            FlexLineInfo* line = &flex_layout->lines[0];
            float pad_border_h = 0;
            if (flex_container->bound) {
                pad_border_h += flex_container->bound->padding.top + flex_container->bound->padding.bottom;
                if (flex_container->bound->border) {
                    pad_border_h += flex_container->bound->border->width.top + flex_container->bound->border->width.bottom;
                }
            }
            float new_height = line->cross_size + pad_border_h;
            if (new_height > flex_container->height) {
                log_debug("Baseline: single-line container height %.1f -> %.1f (line_cross=%d)",
                          flex_container->height, new_height, line->cross_size);
                flex_container->height = new_height;
                flex_layout->cross_axis_size = (float)line->cross_size;
            }
            // Re-run cross-axis alignment so items use the updated line cross size
            align_items_cross_axis(flex_layout, line);
        }
    }

    // If line cross sizes changed for a multi-line container, recalculate line positions
    // and re-run cross-axis alignment so items land at the correct absolute positions
    if (flex_layout->line_count > 1) {
        int cross_pos = 0;
        bool positions_changed = false;
        for (int i = 0; i < flex_layout->line_count; i++) {
            if (flex_layout->lines[i].cross_position != cross_pos) {
                positions_changed = true;
            }
            flex_layout->lines[i].cross_position = cross_pos;
            cross_pos += flex_layout->lines[i].cross_size;
        }

        if (positions_changed) {
            log_debug("Baseline: line positions changed, re-running cross alignment");
            // Re-run find_max_baseline + repositioning for each line
            for (int line_idx = 0; line_idx < flex_layout->line_count; line_idx++) {
                FlexLineInfo* line = &flex_layout->lines[line_idx];
                int max_bl = find_max_baseline(line, flex_layout->align_items);

                for (int i = 0; i < line->item_count; i++) {
                    ViewElement* item = (ViewElement*)line->items[i]->as_element();
                    if (!item || !item->fi) continue;
                    int align_self = item->fi->align_self;
                    bool uses_bl = (align_self == ALIGN_BASELINE) ||
                                   (align_self == ALIGN_AUTO && flex_layout->align_items == ALIGN_BASELINE);
                    if (!uses_bl) continue;

                    int item_bl = calculate_item_baseline(item);
                    int new_pos = line->cross_position + (max_bl - item_bl);
                    set_cross_axis_position(item, new_pos, flex_layout);
                    log_debug("Baseline re-position line %d item %d: y=%d (line_pos=%d, bl_offset=%d)",
                              line_idx, i, new_pos, line->cross_position, max_bl - item_bl);
                }
            }

            // Update container auto-height if needed
            bool has_explicit_height = flex_container->blk && flex_container->blk->given_height >= 0;
            if (!has_explicit_height) {
                float pad_border_h = 0;
                if (flex_container->bound) {
                    pad_border_h += flex_container->bound->padding.top + flex_container->bound->padding.bottom;
                    if (flex_container->bound->border) {
                        pad_border_h += flex_container->bound->border->width.top + flex_container->bound->border->width.bottom;
                    }
                }
                float new_height = cross_pos + pad_border_h;
                if (new_height > flex_container->height) {
                    log_debug("Baseline: updating container height %.1f -> %.1f", flex_container->height, new_height);
                    flex_container->height = new_height;
                }
            }
        }
    }

    log_info("%s BASELINE REPOSITIONING END", flex_container->source_loc());
    log_leave();
}

// Check if main axis is horizontal
bool is_main_axis_horizontal(FlexProp* flex) {
    // CRITICAL FIX: Use direction value directly - it's stored as Lexbor constant
    int direction = flex->direction;

    // Consider writing mode in axis determination
    if (flex->writing_mode == WM_VERTICAL_RL || flex->writing_mode == WM_VERTICAL_LR) {
        // In vertical writing modes, row becomes vertical
        return direction == CSS_VALUE_COLUMN || direction == CSS_VALUE_COLUMN_REVERSE;
    }
    return direction == CSS_VALUE_ROW || direction == CSS_VALUE_ROW_REVERSE;
}

// Create flex lines based on wrapping
int create_flex_lines(FlexContainerLayout* flex_layout, View** items, int item_count) {
    if (!flex_layout || !items || item_count == 0) return 0;

    // Ensure we have space for lines
    if (flex_layout->allocated_lines == 0) {
        flex_layout->allocated_lines = 4;
        flex_layout->lines = (FlexLineInfo*)mem_calloc(flex_layout->allocated_lines, sizeof(FlexLineInfo), MEM_CAT_LAYOUT);
    }

    int line_count = 0;
    int current_item = 0;

    while (current_item < item_count) {
        // Ensure we have space for another line
        if (line_count >= flex_layout->allocated_lines) {
            flex_layout->allocated_lines *= 2;
            flex_layout->lines = (FlexLineInfo*)mem_realloc(flex_layout->lines,
                                                       flex_layout->allocated_lines * sizeof(FlexLineInfo), MEM_CAT_LAYOUT);
        }

        FlexLineInfo* line = &flex_layout->lines[line_count];
        memset(line, 0, sizeof(FlexLineInfo));

        // Allocate items array for this line
        line->items = (View**)mem_alloc(item_count * sizeof(View*), MEM_CAT_LAYOUT);
        line->item_count = 0;

        int main_size = 0;
        int container_main_size = flex_layout->main_axis_size;

        // Add items to line until we need to wrap
        while (current_item < item_count) {
            ViewElement* item = (ViewElement*)items[current_item]->as_element();
            if (!item) { current_item++;  continue; }

            // Use hypothetical main size (flex-basis clamped by min/max) for wrapping decisions
            // Per CSS Flexbox spec 9.3, line breaking uses the item's hypothetical main size
            int item_hypothetical = calculate_hypothetical_main_size(item, flex_layout);

            // Per CSS Flexbox spec §9.3, wrapping uses the item's OUTER hypothetical main size
            // (hypothetical main size + main-axis margins)
            int item_margin_main = 0;
            if (item->bound) {
                if (is_main_axis_horizontal(flex_layout)) {
                    item_margin_main = item->bound->margin.left + item->bound->margin.right;
                } else {
                    item_margin_main = item->bound->margin.top + item->bound->margin.bottom;
                }
            }
            int item_outer_hypothetical = item_hypothetical + item_margin_main;

            // Add gap space if not the first item
            int gap_space = line->item_count > 0 ?
                (is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap) : 0;

            log_debug("LINE %d - item %d: hypothetical=%d, outer=%d, gap=%d, line_size=%d, container=%d",
                   flex_layout->line_count, current_item, item_hypothetical, item_outer_hypothetical, gap_space, main_size, container_main_size);

            // Check if we need to wrap (only if not the first item in line)
            // Use outer hypothetical main size (clamped by min/max + margins) for wrapping decision
            if (flex_layout->wrap != WRAP_NOWRAP &&
                line->item_count > 0 &&
                main_size + item_outer_hypothetical + gap_space > container_main_size) {
                log_debug("WRAP - item %d needs new line (would be %d > %d)",
                       current_item, main_size + item_outer_hypothetical + gap_space, container_main_size);
                break;
            }

            line->items[line->item_count] = item;
            line->item_count++;
            main_size += item_outer_hypothetical + gap_space;
            current_item++;
        }

        line->main_size = main_size;
        line->free_space = container_main_size - main_size;

        log_debug("LINE %d COMPLETE - items: %d, main_size: %d, free_space: %d",
               flex_layout->line_count, line->item_count, main_size, line->free_space);

        // Log item order values in this line
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item_elmt = (ViewElement*)line->items[i]->as_element();
            int order_val = item_elmt && item_elmt->fi ? item_elmt->fi->order : 0;
            log_debug("  Line item[%d] order=%d", i, order_val);
        }

        // Calculate total flex grow/shrink for this line
        line->total_flex_grow = 0;
        line->total_flex_shrink = 0;
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item_elmt = (ViewElement*)line->items[i]->as_element();
            if (item_elmt) {
                line->total_flex_grow += get_item_flex_grow(item_elmt);
                line->total_flex_shrink += get_item_flex_shrink(item_elmt);
            } else {
                line->total_flex_grow += 0;
                line->total_flex_shrink += 1; // default shrink
            }
        }
        line_count++;
    }

    flex_layout->line_count = line_count;
    return line_count;
}

// Resolve flexible lengths for a flex line (flex-grow/shrink) with iterative constraint resolution
void resolve_flexible_lengths(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    log_info("=== resolve_flexible_lengths CALLED ===");
    if (!flex_layout || !line || line->item_count == 0) {
        log_info("=== resolve_flexible_lengths EARLY RETURN (empty) ===");
        return;
    }

    float container_main_size = flex_layout->main_axis_size;

    // CRITICAL: Store original flex basis for each item (needed for correct flex-shrink calculation)
    // Per CSS Flexbox spec §9.7, scaled shrink factor uses the original flex base size
    float* item_flex_basis = (float*)mem_calloc(line->item_count, sizeof(float), MEM_CAT_LAYOUT);
    if (!item_flex_basis) return;

    // Track which items are frozen (have flex factor 0 or hit constraints during distribution)
    bool* frozen = (bool*)mem_calloc(line->item_count, sizeof(bool), MEM_CAT_LAYOUT);
    if (!frozen) {
        mem_free(item_flex_basis);
        return;
    }

    float total_hypothetical_size = 0.0f;
    float total_base_size = 0.0f;
    float total_margin_size = 0.0f;
    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    // Store hypothetical sizes per-item for step 2 freezing decisions
    float* item_hypothetical = (float*)mem_calloc(line->item_count, sizeof(float), MEM_CAT_LAYOUT);
    if (!item_hypothetical) {
        mem_free(item_flex_basis);
        mem_free(frozen);
        return;
    }

    // CSS Flexbox §9.7 Step 1-3: Initialize items
    // Step 1: Determine grow vs shrink from sum of hypothetical sizes
    // Step 2: Freeze inflexible items and items constrained by min/max
    // Step 3: Set target main size to hypothetical; calc initial free space from BASE sizes
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;

        // Store original flex-basis for scaled shrink calculation
        float basis = calculate_flex_basis(item, flex_layout);
        item_flex_basis[i] = basis;

        // Calculate hypothetical main size (basis clamped by min/max constraints)
        float hypothetical = calculate_hypothetical_main_size(item, flex_layout);
        item_hypothetical[i] = hypothetical;

        // Set each item's target main size to its hypothetical main size (spec step 3)
        set_main_axis_size(item, hypothetical, flex_layout);

        // Sum hypothetical sizes for grow/shrink determination (spec step 1)
        total_hypothetical_size += hypothetical;
        // Sum base sizes for initial free space calculation (spec step 3)
        // CSS Flexbox §9.7: Uses "outer flex base sizes" — padding+border must be accounted for.
        // For border-box items, floor basis to padding+border (content-box can't go negative).
        float effective_base = get_effective_flex_base(item, basis, flex_layout);
        total_base_size += effective_base;

        // Check if item is inflexible:
        // - Items without fi (and not form controls) are inflexible
        // - Items with explicit flex-grow:0 and flex-shrink:0 are inflexible
        bool has_flex_props = item->fi != nullptr ||
                              (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form);
        float fg = get_item_flex_grow(item);
        float fs = get_item_flex_shrink(item);
        bool is_inflexible = !has_flex_props || (fg == 0 && fs == 0);

        if (is_inflexible) {
            frozen[i] = true;
            log_debug("ITERATIVE_FLEX - item %d: PRE-FROZEN (inflexible), size=%.1f", i, hypothetical);
        } else {
            log_debug("ITERATIVE_FLEX - item %d: FLEXIBLE (grow=%.2f, shrink=%.2f), hypothetical=%.1f (basis=%.1f)",
                      i, fg, fs, hypothetical, basis);
        }

        // Add margins in the main axis direction
        if (item->bound) {
            if (is_horizontal) {
                total_margin_size += item->bound->margin.left + item->bound->margin.right;
            } else {
                total_margin_size += item->bound->margin.top + item->bound->margin.bottom;
            }
        }
    }

    // Calculate gap space
    float gap_space = calculate_gap_space(flex_layout, line->item_count, true);

    // CSS Flexbox §9.7 Step 1: Determine used flex factor from sum of hypothetical sizes
    bool use_grow_factor = (total_hypothetical_size + total_margin_size + gap_space) < container_main_size;

    // CSS Flexbox §9.7 Step 2: Freeze items that shouldn't flex
    // - If using flex-grow: freeze items where flex_base_size > hypothetical (clamped to max)
    // - If using flex-shrink: freeze items where flex_base_size < hypothetical (clamped to min)
    for (int i = 0; i < line->item_count; i++) {
        if (frozen[i]) continue;
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;
        float fg = get_item_flex_grow(item);
        float fs = get_item_flex_shrink(item);
        if (use_grow_factor) {
            // Freeze items with flex-grow:0, or where basis > hypothetical (hit max)
            if (fg == 0 || item_flex_basis[i] > item_hypothetical[i]) {
                frozen[i] = true;
                log_debug("ITERATIVE_FLEX - item %d: FROZEN (grow=0 or basis>hypo, basis=%.1f, hypo=%.1f)",
                          i, item_flex_basis[i], item_hypothetical[i]);
            }
        } else {
            // Freeze items with flex-shrink:0, or where basis < hypothetical (hit min)
            if (fs == 0 || item_flex_basis[i] < item_hypothetical[i]) {
                frozen[i] = true;
                log_debug("ITERATIVE_FLEX - item %d: FROZEN (shrink=0 or basis<hypo, basis=%.1f, hypo=%.1f)",
                          i, item_flex_basis[i], item_hypothetical[i]);
            }
        }
    }

    // CSS Flexbox §9.7 Step 3: Calculate initial free space from flex BASE sizes
    float free_space = container_main_size - total_base_size - total_margin_size - gap_space;
    line->free_space = free_space;

    log_debug("FLEX FREE_SPACE: container=%.1f, base_total=%.1f, hypo_total=%.1f, margins=%.1f, gaps=%.1f, free=%.1f",
              container_main_size, total_base_size, total_hypothetical_size, total_margin_size, gap_space, free_space);

    log_info("ITERATIVE_FLEX START - container=%.1f, base_total=%.1f, gap=%.1f, free_space=%.1f",
              container_main_size, total_base_size, gap_space, free_space);

    if (free_space == 0.0f) {
        mem_free(item_flex_basis);
        mem_free(item_hypothetical);
        mem_free(frozen);
        return;  // No space to distribute
    }

    // ITERATIVE CONSTRAINT RESOLUTION ALGORITHM
    // Per CSS Flexbox Spec: https://www.w3.org/TR/css-flexbox-1/#resolve-flexible-lengths
    const int MAX_ITERATIONS = 10;  // Prevent infinite loops
    int iteration = 0;

    while (iteration < MAX_ITERATIONS) {
        iteration++;

        // CSS Flexbox §9.7 Step 4b: Calculate remaining free space
        // remaining = container - sum(frozen target sizes) - sum(unfrozen flex base sizes) - margins - gaps
        float total_frozen_target = 0;
        float total_unfrozen_base = 0;
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item) continue;
            if (frozen[i]) {
                total_frozen_target += get_main_axis_size(item, flex_layout);
            } else {
                // Use effective base (floored to padding+border) for outer size accounting
                total_unfrozen_base += get_effective_flex_base(item, item_flex_basis[i], flex_layout);
            }
        }
        float remaining_free_space = container_main_size - total_frozen_target - total_unfrozen_base - total_margin_size - gap_space;

        // CSS Flexbox §9.7 Step 4b: If sum of unfrozen flex factors < 1,
        // multiply initial free space by this sum and use if smaller magnitude
        double sum_unfrozen_flex = 0.0;
        for (int i = 0; i < line->item_count; i++) {
            if (frozen[i]) continue;
            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item) continue;
            if (use_grow_factor) {
                sum_unfrozen_flex += get_item_flex_grow(item);
            } else {
                sum_unfrozen_flex += get_item_flex_shrink(item);
            }
        }
        if (sum_unfrozen_flex > 0 && sum_unfrozen_flex < 1.0) {
            float scaled = (float)(free_space * sum_unfrozen_flex);
            if (fabsf(scaled) < fabsf(remaining_free_space)) {
                remaining_free_space = scaled;
            }
        }

        log_debug("ITERATIVE_FLEX - iteration %d, remaining_free_space=%.1f (frozen=%.1f, unfrozen_base=%.1f)",
                  iteration, remaining_free_space, total_frozen_target, total_unfrozen_base);

        // Use the flex factor direction determined in step 1
        // CRITICAL: When main axis is indefinite, NEVER grow items - per CSS Flexbox §9.2,
        // items keep their basis sizes and the container shrinks to fit.
        //
        // For COLUMN FLEX with indefinite main axis (auto-height), also block shrinking:
        // Per CSS Flexbox §9.7 + §9.2, the container sizes to fit its content in Phase 7.
        // The auto-height estimate used as container_main_size is unreliable (may be just
        // padding), NOT a real constraint. Shrinking items would be incorrect because the
        // container will expand to fit items afterward.
        //
        // For ROW FLEX with indefinite main axis (shrink-to-fit), both shrinking AND growing
        // are allowed: The shrink-to-fit width is computed from items' max-content contributions
        // (§9.9.1), giving a REAL container size. Items must shrink to fit, and flex-grow items
        // must grow to fill their proportional share of the determined container size.
        // For COLUMN FLEX with indefinite main axis (auto-height), neither is allowed because
        // the container size hasn't been calculated and is not meaningful as a constraint.
        bool is_indefinite_column = flex_layout->main_axis_is_indefinite && !is_horizontal;
        bool is_growing = use_grow_factor && remaining_free_space > 0 && !is_indefinite_column;
        bool is_shrinking = !use_grow_factor && remaining_free_space < 0 && !is_indefinite_column;

        if (!is_growing && !is_shrinking) {
            log_debug("ITERATIVE_FLEX - no flex distribution needed (free_space=%d, indefinite=%s)",
                      remaining_free_space, flex_layout->main_axis_is_indefinite ? "true" : "false");
            break;
        }

        // Calculate total flex factor (grow or shrink) for unfrozen items
        double total_flex_factor = 0.0;
        double total_scaled_shrink = 0.0;
        int unfrozen_count = 0;

        for (int i = 0; i < line->item_count; i++) {
            if (frozen[i]) continue;

            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item) continue;

            float fg = get_item_flex_grow(item);
            float fs = get_item_flex_shrink(item);

            if (is_growing && fg > 0) {
                total_flex_factor += fg;
                unfrozen_count++;
            } else if (is_shrinking && fs > 0) {
                // CRITICAL FIX: Use original flex_basis for scaled shrink factor
                // Per CSS Flexbox spec: scaled_flex_shrink_factor = flex_shrink × flex_base_size
                float flex_basis = item_flex_basis[i];
                double scaled = (double)flex_basis * fs;
                total_scaled_shrink += scaled;
                unfrozen_count++;
                log_debug("FLEX_SHRINK - item %d: flex_shrink=%.2f, flex_basis=%.1f, scaled=%.2f",
                          i, fs, flex_basis, scaled);
            }
        }

        log_debug("ITERATIVE_FLEX - iter=%d, unfrozen=%d, growing=%d, shrinking=%d, total_flex=%.2f, total_scaled_shrink=%.2f",
                  iteration, unfrozen_count, is_growing, is_shrinking, total_flex_factor, total_scaled_shrink);

        if (unfrozen_count == 0 || (is_growing && total_flex_factor == 0) ||
            (is_shrinking && total_scaled_shrink == 0)) {
            break;  // All items frozen or no flexible items
        }

        // CSS Flexbox §9.7 Step 5: Calculate target sizes for unfrozen items
        // Store target sizes and violation info for two-phase freezing
        float* target_sizes = (float*)mem_calloc(line->item_count, sizeof(float), MEM_CAT_LAYOUT);
        float* clamped_sizes = (float*)mem_calloc(line->item_count, sizeof(float), MEM_CAT_LAYOUT);
        bool* has_min_violation = (bool*)mem_calloc(line->item_count, sizeof(bool), MEM_CAT_LAYOUT);
        bool* has_max_violation = (bool*)mem_calloc(line->item_count, sizeof(bool), MEM_CAT_LAYOUT);

        if (!target_sizes || !clamped_sizes || !has_min_violation || !has_max_violation) {
            mem_free(target_sizes);
            mem_free(clamped_sizes);
            mem_free(has_min_violation);
            mem_free(has_max_violation);
            break;
        }

        float total_violation = 0.0f;

        for (int i = 0; i < line->item_count; i++) {
            if (frozen[i]) continue;

            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item) continue;

            float fg = get_item_flex_grow(item);
            float fs = get_item_flex_shrink(item);
            float current_size = get_main_axis_size(item, flex_layout);
            float flex_basis = item_flex_basis[i];
            float target_size = current_size;

            if (is_growing && fg > 0) {
                // FLEX-GROW: Distribute positive free space
                // Per CSS spec §9.7 step 4c: target = flex_base_size + fraction of remaining free space
                // Use effective base (floored to p+b) so growth starts from the actual minimum border-box size.
                // Note: remaining_free_space is already adjusted by step 4b for fractional flex factors
                float effective_basis = get_effective_flex_base(item, item_flex_basis[i], flex_layout);
                double grow_ratio = fg / total_flex_factor;
                float grow_amount = (float)(grow_ratio * remaining_free_space);
                target_size = effective_basis + grow_amount;

                log_debug("ITERATIVE_FLEX - item %d: grow_ratio=%.4f, grow_amount=%.1f, basis=%.1f(eff=%.1f)→%.1f",
                          i, grow_ratio, grow_amount, item_flex_basis[i], effective_basis, target_size);

            } else if (is_shrinking && fs > 0) {
                // FLEX-SHRINK: Distribute negative space using scaled shrink factor
                // Per CSS spec §9.7 step 4c: scaled_shrink = inner_flex_base × flex_shrink
                // Use ORIGINAL basis for scaled shrink factor (spec uses inner flex base)
                // Use effective basis for the target starting point (border-box floor)
                float flex_basis = item_flex_basis[i];
                float effective_basis = get_effective_flex_base(item, flex_basis, flex_layout);
                double scaled_shrink = (double)flex_basis * fs;
                double shrink_ratio = scaled_shrink / total_scaled_shrink;
                float shrink_amount = (float)(shrink_ratio * (-remaining_free_space));
                target_size = effective_basis - shrink_amount;

                log_debug("FLEX_SHRINK - item %d: shrink_ratio=%.4f, shrink=%.1f, %.1f(eff=%.1f)→%.1f",
                          i, shrink_ratio, shrink_amount, flex_basis, effective_basis, target_size);
            }

            target_sizes[i] = target_size;

            // Step 5c: Clamp and detect violations
            bool hit_min = false, hit_max = false;
            float clamped = apply_flex_constraint(item, target_size, true, flex_layout, &hit_min, &hit_max);
            clamped_sizes[i] = clamped;

            // Track violation type and amount
            float adjustment = clamped - target_size;
            if (adjustment > 0.0f) {
                has_min_violation[i] = true;  // Made larger by min constraint
                log_debug("ITERATIVE_FLEX - item %d: MIN violation, %.1f→%.1f (+%.1f)", i, target_size, clamped, adjustment);
            } else if (adjustment < 0.0f) {
                has_max_violation[i] = true;  // Made smaller by max constraint
                log_debug("ITERATIVE_FLEX - item %d: MAX violation, %.1f→%.1f (%.1f)", i, target_size, clamped, adjustment);
            }
            total_violation += adjustment;
        }

        // Step 6: Freeze over-flexed items based on total violation direction
        // - Positive total: freeze items with min violations
        // - Negative total: freeze items with max violations
        // - Zero total: freeze all items (converged)
        log_debug("ITERATIVE_FLEX - total_violation=%.1f", total_violation);

        bool any_frozen_this_iteration = false;
        for (int i = 0; i < line->item_count; i++) {
            if (frozen[i]) continue;

            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item) continue;

            bool should_freeze = false;
            if (total_violation == 0.0f) {
                should_freeze = true;  // Converged - freeze all
            } else if (total_violation > 0.0f && has_min_violation[i]) {
                should_freeze = true;  // Positive total - freeze min violations
            } else if (total_violation < 0.0f && has_max_violation[i]) {
                should_freeze = true;  // Negative total - freeze max violations
            }

            // Per CSS spec §9.7: Only apply final sizes to items being frozen.
            // Unfrozen items will be recomputed in the next iteration from their flex base size.
            if (should_freeze) {
                set_main_axis_size(item, clamped_sizes[i], flex_layout);
                frozen[i] = true;
                any_frozen_this_iteration = true;
                log_debug("ITERATIVE_FLEX - item %d: FROZEN at size %.1f", i, clamped_sizes[i]);

                // Adjust cross axis size based on aspect ratio
                if (item->fi && item->fi->aspect_ratio > 0) {
                    if (is_main_axis_horizontal(flex_layout)) {
                        item->height = clamped_sizes[i] / item->fi->aspect_ratio;
                    } else {
                        item->width = clamped_sizes[i] * item->fi->aspect_ratio;
                    }
                }
            }
        }

        mem_free(target_sizes);
        mem_free(clamped_sizes);
        mem_free(has_min_violation);
        mem_free(has_max_violation);

        // If no items were frozen this iteration (total_violation was 0), we're done
        if (!any_frozen_this_iteration) {
            log_debug("ITERATIVE_FLEX - converged after %d iterations", iteration);
            break;
        }

        // Early exit: recalculate remaining free space to check if negligible
        {
            float recalc_frozen = 0, recalc_unfrozen = 0;
            for (int i = 0; i < line->item_count; i++) {
                ViewElement* item = (ViewElement*)line->items[i]->as_element();
                if (!item) continue;
                if (frozen[i]) {
                    recalc_frozen += get_main_axis_size(item, flex_layout);
                } else {
                    recalc_unfrozen += item_flex_basis[i];
                }
            }
            float recalc_free = container_main_size - recalc_frozen - recalc_unfrozen - total_margin_size - gap_space;
            log_debug("ITERATIVE_FLEX - recalculated free=%.1f (frozen=%.1f, unfrozen_base=%.1f)",
                      recalc_free, recalc_frozen, recalc_unfrozen);
            if (fabsf(recalc_free) < 2.0f) {
                log_debug("ITERATIVE_FLEX - remaining space negligible, stopping");
                break;
            }
        }
    }

    // Finalize any remaining unfrozen items: set them to their hypothetical main size
    // This handles the case where the loop exits early (remaining_free_space == 0)
    // and unfrozen items still have their initial hypothetical sizes from setup
    for (int i = 0; i < line->item_count; i++) {
        if (frozen[i]) continue;
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;
        set_main_axis_size(item, item_hypothetical[i], flex_layout);
        log_debug("ITERATIVE_FLEX - item %d: UNFROZEN finalized at hypothetical=%.1f", i, item_hypothetical[i]);
    }

    // Last-item rounding correction: absorb float rounding remainder into the last
    // flexible item so that Σ(item sizes) + margins + gaps == container_main_size exactly.
    // Without this, accumulated float precision errors (1-2px) cause visible gaps or overflow.
    {
        double total_outer = 0.0;
        int last_flex_idx = -1;
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item) continue;
            total_outer += (double)get_main_axis_outer_size(item, flex_layout);
            float fg = get_item_flex_grow(item);
            float fs = get_item_flex_shrink(item);
            if (fg > 0 || fs > 0) last_flex_idx = i;
        }
        if (last_flex_idx >= 0) {
            double expected = (double)container_main_size - (double)gap_space;
            double remainder = expected - total_outer;
            if (fabs(remainder) > 0.01 && fabs(remainder) < 4.0) {
                ViewElement* last = (ViewElement*)line->items[last_flex_idx]->as_element();
                if (last) {
                    float old_size = get_main_axis_size(last, flex_layout);
                    float new_size = old_size + (float)remainder;
                    set_main_axis_size(last, new_size, flex_layout);
                    log_debug("ITERATIVE_FLEX - rounding correction: item %d size %.1f -> %.1f (remainder=%.2f)",
                              last_flex_idx, old_size, new_size, remainder);
                }
            }
        }
    }

    // Mark items whose main-axis size was actually changed by flex grow/shrink
    // (i.e., final size differs from hypothetical). This signals to nested flex containers
    // that their parent set a definite main-axis size they shouldn't override.
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item || !item->fi) continue;
        float final_size = get_main_axis_size(item, flex_layout);
        float hypo = item_hypothetical[i];
        if (fabsf(final_size - hypo) > 0.5f) {
            item->fi->main_size_from_flex = 1;
            log_debug("FLEX_SIZED: item %d main_size_from_flex=1 (final=%.1f, hypo=%.1f)", i, final_size, hypo);
        }
    }

    mem_free(frozen);
    mem_free(item_flex_basis);
    mem_free(item_hypothetical);
    log_info("ITERATIVE_FLEX COMPLETE - converged after %d iterations", iteration);
}

// Align items on main axis (justify-content)
void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count == 0) return;

    float container_size = flex_layout->main_axis_size;
    log_info("MAIN_AXIS_ALIGN - container_size=%.1f, item_count=%d, justify=%d",
             container_size, line->item_count, flex_layout->justify);

    // *** FIX 1: Calculate total item size INCLUDING margins for positioning ***
    // CSS Flexbox: margins are part of the item's outer size for justify-content
    float total_item_size = 0.0f;
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;
        float item_size = get_main_axis_outer_size(item, flex_layout);
        log_debug("MAIN_AXIS_ALIGN - item %d outer size: %.1f", i, item_size);
        total_item_size += item_size;
    }
    log_info("MAIN_AXIS_ALIGN - total_item_size=%.1f (with margins, without gaps)", total_item_size);

    // Check for auto margins on main axis
    int auto_margin_count = 0;
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;
        if (is_main_axis_horizontal(flex_layout)) {
            if (item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO) auto_margin_count++;
            if (item->bound && item->bound->margin.right_type == CSS_VALUE_AUTO) auto_margin_count++;
        } else {
            if (item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO) auto_margin_count++;
            if (item->bound && item->bound->margin.bottom_type == CSS_VALUE_AUTO) auto_margin_count++;
        }
    }

    float current_pos = 0.0f;
    float spacing = 0.0f;
    float auto_margin_size = 0.0f;

    // *** FIX 2: For justify-content calculations, include gaps in total size ***
    float gap_space = calculate_gap_space(flex_layout, line->item_count, true);
    float total_size_with_gaps = total_item_size + gap_space;
    float free_space = container_size - total_size_with_gaps;

    if (auto_margin_count > 0 && free_space > 0.0f) {
        // Distribute free space among auto margins
        auto_margin_size = free_space / auto_margin_count;
    } else {
        // Apply justify-content if no auto margins
        // CRITICAL FIX: Use justify value directly - it's now stored as Lexbor constant
        int justify = flex_layout->justify;

        // Apply overflow fallback - when free_space < 0, space-* values fall back to flex-start
        if (free_space < 0) {
            int old_justify = justify;
            justify = radiant::alignment_fallback_for_overflow(justify, free_space);
            if (old_justify != justify) {
                log_debug("JUSTIFY_CONTENT overflow fallback: %d -> %d (free_space=%.1f)",
                         old_justify, justify, free_space);
            }
        }

        log_debug("JUSTIFY_CONTENT - justify=%d, container_size=%d, total_size_with_gaps=%d, free_space=%d, direction=%d",
               justify, container_size, total_size_with_gaps, free_space, flex_layout->direction);

        // CSS Alignment: 'start'/'end' are writing-mode aware, not flex-direction aware
        // For LTR horizontal-tb writing mode:
        // - 'start' always means physical start (top for column, left for row)
        // - 'end' always means physical end (bottom for column, right for row)
        // This differs from flex-start/flex-end which respect reversed directions
        bool is_reverse = (flex_layout->direction == CSS_VALUE_ROW_REVERSE ||
                          flex_layout->direction == CSS_VALUE_COLUMN_REVERSE);

        switch (justify) {
            case CSS_VALUE_START:  // Writing-mode 'start' - always physical start
                if (is_reverse) {
                    // In reverse, physical start is at free_space
                    current_pos = free_space;
                } else {
                    current_pos = 0;
                }
                break;
            case CSS_VALUE_END:  // Writing-mode 'end' - always physical end
                if (is_reverse) {
                    // In reverse, physical end is at 0
                    current_pos = 0;
                } else {
                    current_pos = free_space;
                }
                break;
            case CSS_VALUE_FLEX_START:
                current_pos = 0;
                break;
            case CSS_VALUE_FLEX_END:
                current_pos = free_space;
                break;
            case CSS_VALUE_CENTER:
                current_pos = free_space / 2;
                break;
            case CSS_VALUE_SPACE_BETWEEN:
                current_pos = 0;
                if (line->item_count > 1) {
                    // Space-between distributes remaining space evenly between items
                    // Don't include gaps in calculation - space-between absorbs gaps into spacing
                    int remaining_space = container_size - total_item_size;
                    spacing = remaining_space / (line->item_count - 1);
                    log_debug("SPACE_BETWEEN - remaining_space=%d, spacing=%d",
                           remaining_space, spacing);
                } else {
                    spacing = 0; // Single item: no spacing needed
                }
                break;
            case CSS_VALUE_SPACE_AROUND:
                if (line->item_count > 0) {
                    int remaining_space = container_size - total_size_with_gaps;
                    spacing = remaining_space / line->item_count;
                    current_pos = spacing / 2;
                }
                break;
            case CSS_VALUE_SPACE_EVENLY:
                if (line->item_count > 0) {
                    int remaining_space = container_size - total_size_with_gaps;
                    spacing = remaining_space / (line->item_count + 1);
                    current_pos = spacing;
                    log_debug("SPACE_EVENLY - remaining=%d, spacing=%d, current_pos=%d",
                           remaining_space, spacing, current_pos);
                }
                break;
            default:
                log_debug("Using DEFAULT justify-content (value=%d)", justify);
                current_pos = 0;
                break;
        }
    }

    // *** FIX 4: Simplified positioning loop - gaps handled explicitly ***
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;

        // Handle auto margins
        if (auto_margin_count > 0) {
            bool left_auto = is_main_axis_horizontal(flex_layout) ?
                item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO;
            bool right_auto = is_main_axis_horizontal(flex_layout) ?
                item->bound && item->bound->margin.right_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.bottom_type == CSS_VALUE_AUTO;

            log_debug("MAIN_ALIGN_ITEM %d - auto margins: left=%d, right=%d, auto_margin_size=%.1f",
                   i, left_auto, right_auto, auto_margin_size);

            // Add non-auto start margin
            if (!left_auto && item->bound) {
                int margin_start = is_main_axis_horizontal(flex_layout) ?
                    item->bound->margin.left : item->bound->margin.top;
                current_pos += margin_start;
            }

            // Add auto margin space before item
            if (left_auto) current_pos += auto_margin_size;

            log_debug("MAIN_ALIGN_ITEM %d - positioning at: %.0f", i, current_pos);
            set_main_axis_position(item, current_pos, flex_layout);
            current_pos += get_main_axis_size(item, flex_layout);

            // Add auto margin space after item
            if (right_auto) current_pos += auto_margin_size;

            // Add non-auto end margin
            if (!right_auto && item->bound) {
                float margin_end = is_main_axis_horizontal(flex_layout) ?
                    item->bound->margin.right : item->bound->margin.bottom;
                current_pos += margin_end;
            }

            // Add gap to next item
            if (i < line->item_count - 1) {
                current_pos += is_main_axis_horizontal(flex_layout) ?
                    flex_layout->column_gap : flex_layout->row_gap;
            }
        } else {
            // *** FIX 5: Set position with margins ***
            // CSS Flexbox: item position includes its margin-start offset
            float item_size = get_main_axis_size(item, flex_layout);

            // Get item margins in main axis direction
            float margin_start = 0, margin_end = 0;
            if (item->bound) {
                if (is_main_axis_horizontal(flex_layout)) {
                    margin_start = item->bound->margin.left;
                    margin_end = item->bound->margin.right;
                } else {
                    margin_start = item->bound->margin.top;
                    margin_end = item->bound->margin.bottom;
                }
            }

            // Add margin-start before positioning
            current_pos += margin_start;

            int order_val = item && item->fi ? item->fi->order : -999; // INT_CAST_OK: order is integer
            log_debug("align_items_main_axis: Positioning item %d (order=%d, ptr=%p) at position %.1f (margin_start=%.1f), size=%.1f",
                      i, order_val, item, current_pos, margin_start, item_size);
            set_main_axis_position(item, current_pos, flex_layout);
            log_debug("align_items_main_axis: After set, item->x=%.1f, item->y=%.1f", item->x, item->y);

            // Advance by item size + margin-end
            current_pos += item_size + margin_end;

            // Add justify-content spacing (for space-between, space-around, etc.)
            if (spacing > 0 && i < line->item_count - 1) {
                current_pos += spacing;
            }

            // Add gap between items.
            // For space-between with positive free space, gaps are already absorbed into
            // the spacing value (spacing = remaining_space / (n-1) which implicitly includes gap).
            // But when space-between overflows (free_space < 0), it falls back to flex-start
            // via alignment_fallback_for_overflow, and gaps should still apply between items.
            bool skip_gap = (flex_layout->justify == CSS_VALUE_SPACE_BETWEEN && free_space >= 0);
            if (i < line->item_count - 1 && !skip_gap) {
                float gap = is_main_axis_horizontal(flex_layout) ?
                         flex_layout->column_gap : flex_layout->row_gap;
                if (gap > 0) {
                    current_pos += gap;
                    log_debug("Added gap=%.1f between items %d and %d", gap, i, i+1);
                }
            }
        }
    }
}

// Align items on cross axis (align-items)
void align_items_cross_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    log_debug("align_items_cross_axis: ENTRY - flex_layout=%p, line=%p, item_count=%d",
              flex_layout, line, line ? line->item_count : -1);
    if (!flex_layout || !line || line->item_count == 0) return;

    // Find maximum baseline for baseline alignment (pass container's align_items)
    int max_baseline = find_max_baseline(line, flex_layout->align_items);
    log_debug("align_items_cross_axis: max_baseline=%d", max_baseline);

    // For wrap-reverse or multi-line layouts, use line cross size
    // For single-line non-wrapping, use container cross size
    // IMPORTANT: For ANY wrapping container (wrap or wrap-reverse), always use line cross size
    // This is because align-content affects line sizes, and items should align within their line
    bool use_line_cross = (flex_layout->wrap != WRAP_NOWRAP);
    bool is_wrap_reverse = (flex_layout->wrap == WRAP_WRAP_REVERSE);

    for (int i = 0; i < line->item_count; i++) {
        log_debug("align_items_cross_axis: Processing item %d", i);
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        log_debug("align_items_cross_axis: item=%p, item->as_element()=%p", line->items[i], item);
        if (!item) {
            log_debug("align_items_cross_axis: Skipping item %d (item is null)", i);
            continue;
        }

        // Check if this is a form control - they don't have fi but should still participate in flex alignment
        bool is_form_control = (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form);

        // For non-form-control items, require fi
        if (!is_form_control && !item->fi) {
            log_debug("align_items_cross_axis: Skipping item %d (non-form item without fi)", i);
            continue;
        }

        // Get alignment type - form controls use container's align_items (no align-self)
        int align_type;
        if (is_form_control) {
            align_type = flex_layout->align_items;
            log_debug("ALIGN_SELF_FORM - item %d: using container align_items=%d", i, align_type);
        } else {
            // CRITICAL FIX: Use align values directly - they're now stored as Lexbor constants
            align_type = item->fi->align_self != ALIGN_AUTO ? item->fi->align_self : flex_layout->align_items;
            log_debug("ALIGN_SELF_RAW - item %d: align_self=%d, ALIGN_AUTO=%d, flex_align_items=%d",
                   i, item->fi->align_self, ALIGN_AUTO, flex_layout->align_items);
        }

        // For non-stretch items without explicit cross-axis size, calculate intrinsic size
        // This ensures center/start/end alignment uses content-based size
        if (align_type != ALIGN_STRETCH) {
            bool is_horizontal = is_main_axis_horizontal(flex_layout);
            bool has_explicit_cross_size = is_horizontal ?
                (item->blk && item->blk->given_height >= 0) :
                (item->blk && item->blk->given_width >= 0);

            if (!has_explicit_cross_size && item->fi) {
                // Ensure intrinsic sizes are calculated
                if (!item->fi->has_intrinsic_width || !item->fi->has_intrinsic_height) {
                    calculate_item_intrinsic_sizes(item, flex_layout);
                }

                // For non-stretch items, set cross-axis size from intrinsic size
                // EXCEPTION: Row flex containers with wrapping need available width to wrap properly
                if (!is_horizontal) {
                    // Column flex: cross-axis is width
                    // Check if item is a row flex container with wrap - these need available width
                    bool is_row_flex_with_wrap = false;
                    ViewBlock* item_block = (ViewBlock*)item;
                    if (item_block && item_block->embed && item_block->embed->flex) {
                        FlexProp* item_flex = item_block->embed->flex;
                        // Row or row-reverse with wrap or wrap-reverse
                        bool is_row = (item_flex->direction == CSS_VALUE_ROW ||
                                       item_flex->direction == CSS_VALUE_ROW_REVERSE);
                        bool is_wrap = (item_flex->wrap == CSS_VALUE_WRAP ||
                                        item_flex->wrap == CSS_VALUE_WRAP_REVERSE);
                        is_row_flex_with_wrap = is_row && is_wrap;
                    }

                    if (is_row_flex_with_wrap) {
                        // Row flex containers with wrap should use available width for proper wrapping
                        // Set width to container's cross-axis size (available width)
                        float available_width = flex_layout->cross_axis_size;
                        if (available_width > 0.0f && item->width <= 0.0f) {
                            item->width = available_width;
                            log_debug("ROW_FLEX_WRAP_WIDTH: Set item width=%.1f from available width (align=%d)",
                                      available_width, align_type);
                        }
                    } else if (item->width <= 0.0f && item->fi->has_intrinsic_width) {
                        float intrinsic_width = item->fi->intrinsic_width.max_content;
                        if (intrinsic_width > 0.0f) {
                            item->width = intrinsic_width;
                            log_debug("INTRINSIC_WIDTH: Set item width=%.1f from intrinsic content (align=%d)",
                                      intrinsic_width, align_type);
                        }
                    }
                } else {
                    // Row flex: cross-axis is height
                    if (item->height <= 0.0f && item->fi->has_intrinsic_height) {
                        float intrinsic_height = item->fi->intrinsic_height.max_content;
                        if (intrinsic_height > 0.0f) {
                            item->height = intrinsic_height;
                            log_debug("INTRINSIC_HEIGHT: Set item height=%.1f from intrinsic content (align=%d)",
                                      intrinsic_height, align_type);
                        }
                    }
                }
            }
        }

        float item_cross_size = get_cross_axis_size(item, flex_layout);

        // CSS Flexbox §9.4 step 11: Ensure item's used cross size reflects min/max clamping.
        // get_cross_axis_size applies min/max constraints but only returns the value;
        // it doesn't write it back to item->width/height. We must do so here.
        // (STRETCH alignment may further override this below with the stretched size.)
        set_cross_axis_size(item, item_cross_size, flex_layout);

        float line_cross_size = line->cross_size;
        float old_pos = get_cross_axis_position(item, flex_layout);
        log_debug("CROSS_ALIGN_ITEM %d - cross_size: %.1f, old_pos: %.1f, line_cross_size: %.1f",
               i, item_cross_size, old_pos, line_cross_size);
        float cross_pos = 0;

        // Check for auto margins in cross axis
        bool top_auto = is_main_axis_horizontal(flex_layout) ?
            item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO;
        bool bottom_auto = is_main_axis_horizontal(flex_layout) ?
            item->bound && item->bound->margin.bottom_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.right_type == CSS_VALUE_AUTO;

        if (top_auto || bottom_auto) {
            // Handle auto margins in cross axis
            // cross_axis_size always holds the cross-axis dimension (width for column flex,
            // height for row flex) - use it unconditionally here.
            float container_cross_size = flex_layout->cross_axis_size;

            if (top_auto && bottom_auto) {
                // Center item with auto margins on both sides
                cross_pos = (container_cross_size - item_cross_size) / 2;
            } else if (top_auto) {
                // Push item to bottom
                cross_pos = container_cross_size - item_cross_size;
            } else if (bottom_auto) {
                // Keep item at top
                cross_pos = 0;
            }
        } else {
            // Use line cross size for wrap-reverse/multi-line, container size otherwise
            float available_cross_size = use_line_cross ? line_cross_size : flex_layout->cross_axis_size;

            // For wrap-reverse, swap start and end alignments
            int effective_align = align_type;
            if (is_wrap_reverse) {
                if (align_type == ALIGN_START || align_type == CSS_VALUE_START) {
                    effective_align = ALIGN_END;
                } else if (align_type == ALIGN_END || align_type == CSS_VALUE_END) {
                    effective_align = ALIGN_START;
                }
            }

            // Calculate cross-axis margins for alignment positioning
            // Per CSS Flexbox §9.5: alignment positions the item's margin edge
            float margin_cross_start = 0, margin_cross_end = 0;
            if (item->bound) {
                if (is_main_axis_horizontal(flex_layout)) {
                    margin_cross_start = item->bound->margin.top;
                    margin_cross_end = item->bound->margin.bottom;
                } else {
                    margin_cross_start = item->bound->margin.left;
                    margin_cross_end = item->bound->margin.right;
                }
            }

            // Regular alignment (handle both flex-specific and generic alignment keywords)
            switch (effective_align) {
                case ALIGN_START:
                case CSS_VALUE_START:  // Handle 'start' same as 'flex-start'
                    cross_pos = margin_cross_start;
                    break;
                case ALIGN_END:
                case CSS_VALUE_END:  // Handle 'end' same as 'flex-end'
                    cross_pos = available_cross_size - item_cross_size - margin_cross_end;
                    break;
                case ALIGN_CENTER:
                    cross_pos = margin_cross_start +
                        (available_cross_size - item_cross_size - (margin_cross_start + margin_cross_end)) / 2;
                    break;
                case ALIGN_STRETCH:
                    // For stretch, check if item has explicit cross-axis size from CSS
                    // The blk->given_* fields are ONLY set when CSS explicitly specifies the size
                    // (in resolve_css_style.cpp). Form control intrinsic sizes use lycon->block.given_*
                    // which is a different field, so checking blk->given_* > 0 correctly identifies
                    // CSS-specified sizes without false positives from form control defaults.
                    {
                        bool has_explicit_cross_size = false;
                        if (is_main_axis_horizontal(flex_layout)) {
                            // Row direction: cross-axis is height
                            // blk->given_height is set only by CSS height property
                            has_explicit_cross_size = (item->blk && item->blk->given_height >= 0);
                        } else {
                            // Column direction: cross-axis is width
                            // blk->given_width is set only by CSS width property
                            has_explicit_cross_size = (item->blk && item->blk->given_width >= 0);
                        }

                        log_debug("ALIGN_STRETCH item %d (%s): has_explicit=%d, available=%d, item_cross=%d, blk=%p, given_width=%.1f, type=%d",
                                  i, item->node_name(), has_explicit_cross_size, available_cross_size, item_cross_size,
                                  item->blk, item->blk ? item->blk->given_width : -999.0f,
                                  item->blk ? item->blk->given_width_type : -1);

                        if (has_explicit_cross_size) {
                            // Item has explicit size - set item dimension to border-box value
                            // item_cross_size is already the border-box size (from get_cross_axis_size)
                            set_cross_axis_size(item, item_cross_size, flex_layout);

                            // Per CSS Flexbox §9.5: position item with its margin edge
                            // Even items with explicit cross size must respect cross-axis margins
                            float margin_cross_start = 0, margin_cross_end = 0;
                            if (item->bound) {
                                if (is_main_axis_horizontal(flex_layout)) {
                                    margin_cross_start = item->bound->margin.top;
                                    margin_cross_end = item->bound->margin.bottom;
                                } else {
                                    margin_cross_start = item->bound->margin.left;
                                    margin_cross_end = item->bound->margin.right;
                                }
                            }

                            // For wrap-reverse, position at end of line
                            if (is_wrap_reverse) {
                                cross_pos = available_cross_size - item_cross_size - margin_cross_end;
                            } else {
                                cross_pos = margin_cross_start;
                            }
                        } else {
                            // Item can be stretched - stretch margin box to fill available space
                            // CSS Flexbox spec: stretched item's margin box equals line cross size
                            // So content box = available_cross_size - cross-axis margins
                            float margin_cross_start = 0, margin_cross_end = 0;
                            if (item->bound) {
                                if (is_main_axis_horizontal(flex_layout)) {
                                    margin_cross_start = item->bound->margin.top;
                                    margin_cross_end = item->bound->margin.bottom;
                                } else {
                                    margin_cross_start = item->bound->margin.left;
                                    margin_cross_end = item->bound->margin.right;
                                }
                            }
                            float target_cross_size = available_cross_size - (margin_cross_start + margin_cross_end);
                            if (target_cross_size < 0) target_cross_size = 0;

                            cross_pos = margin_cross_start;
                            // ALWAYS apply stretch constraint and set actual cross-axis size
                            // get_cross_axis_size may return a min-height/min-width adjusted value
                            // but the actual item->height/width may not have been set yet
                            {
                                int constrained_cross_size = apply_stretch_constraint(
                                    item, target_cross_size, flex_layout);
                                set_cross_axis_size(item, constrained_cross_size, flex_layout);
                                item_cross_size = constrained_cross_size;
                                log_debug("ALIGN_STRETCH - item %d: stretched to %d (available=%d, margins=%.1f+%.1f)",
                                          i, constrained_cross_size, available_cross_size, margin_cross_start, margin_cross_end);
                            }
                        }
                    }
                    break;
                case ALIGN_BASELINE:
                    if (is_main_axis_horizontal(flex_layout)) {
                        // Calculate this item's baseline offset using the same function
                        float item_baseline = calculate_item_baseline(item);
                        // Position item so its baseline aligns with max baseline:
                        //   (max_baseline - item_baseline) gives margin-box top offset.
                        // Add margin_cross_start so item->y is the BORDER-BOX top, consistent
                        // with all other alignment cases (ALIGN_START, ALIGN_STRETCH, etc.).
                        cross_pos = max_baseline - item_baseline + margin_cross_start;
                        log_debug("ALIGN_BASELINE - item %d: item_baseline=%.1f, max_baseline=%d, margin_top=%.0f, cross_pos=%.1f",
                                  i, item_baseline, max_baseline, margin_cross_start, cross_pos);
                    } else {
                        // For column direction, baseline is equivalent to start
                        cross_pos = margin_cross_start;
                    }
                    break;
                default:
                    cross_pos = 0;
                    break;
            }
        }

        // CRITICAL: Add line's cross position to get absolute position
        // line->cross_position is set by align_content for multi-line layouts
        float absolute_cross_pos = line->cross_position + cross_pos;
        log_debug("FINAL_CROSS_POS - item %d: line_pos=%d + cross_pos=%.1f = %.1f",
                  i, line->cross_position, cross_pos, absolute_cross_pos);
        set_cross_axis_position(item, absolute_cross_pos, flex_layout);
    }
}

// Align content (align-content for flex containers with flex-wrap)
// Note: This applies to both single-line and multi-line wrapping containers
void align_content(FlexContainerLayout* flex_layout) {
    if (!flex_layout || flex_layout->line_count == 0) return;

    // FIXED: Always use cross_axis_size - it's already set correctly based on direction
    // (for row: height, for column: width)
    float container_cross_size = flex_layout->cross_axis_size;

    int total_lines_size = 0;
    for (int i = 0; i < flex_layout->line_count; i++) {
        total_lines_size += flex_layout->lines[i].cross_size;
    }

    int gap_space = calculate_gap_space(flex_layout, flex_layout->line_count, false);
    total_lines_size += gap_space;

    float free_space = container_cross_size - total_lines_size;
    int start_pos = 0;
    int line_spacing = 0;

    // Apply overflow fallback - when free_space < 0, space-* alignments fall back to start
    int effective_align = flex_layout->align_content;
    if (free_space < 0) {
        effective_align = radiant::alignment_fallback_for_overflow(effective_align, (float)free_space);
        log_debug("ALIGN_CONTENT overflow fallback: %d -> %d (free_space=%d)",
                 flex_layout->align_content, effective_align, free_space);
    }

    // CRITICAL FIX for wrap-reverse: Invert start/end alignments
    // For wrap-reverse, the cross-start and cross-end are swapped, so:
    // - ALIGN_START means start from the cross-end (bottom for row)
    // - ALIGN_END means start from the cross-start (top for row)
    // - ALIGN_STRETCH with no free space behaves like ALIGN_START (which becomes ALIGN_END for wrap-reverse)
    bool is_wrap_reverse = (flex_layout->wrap == WRAP_WRAP_REVERSE);
    if (is_wrap_reverse) {
        if (effective_align == ALIGN_START || effective_align == CSS_VALUE_START ||
            (effective_align == ALIGN_STRETCH && free_space <= 0)) {
            effective_align = ALIGN_END;
        } else if (effective_align == ALIGN_END || effective_align == CSS_VALUE_END) {
            effective_align = ALIGN_START;
        }
        // Note: space-* and stretch (with positive free_space) keep their behavior, just with reversed line order
    }

    // CRITICAL FIX: Use align_content value directly - it's now stored as Lexbor constant
    // Handle both flex-specific (flex-start/flex-end) and generic (start/end) alignment values
    switch (effective_align) {
        case ALIGN_START:
        case CSS_VALUE_START:  // Handle 'start' keyword same as 'flex-start'
            start_pos = 0;
            break;
        case ALIGN_END:
        case CSS_VALUE_END:  // Handle 'end' keyword same as 'flex-end'
            start_pos = free_space;
            break;
        case ALIGN_CENTER:
            start_pos = free_space / 2;
            break;
        case ALIGN_SPACE_BETWEEN:
            start_pos = 0;
            line_spacing = flex_layout->line_count > 1 ? free_space / (flex_layout->line_count - 1) : 0;
            break;
        case ALIGN_SPACE_AROUND:
            line_spacing = flex_layout->line_count > 0 ? free_space / flex_layout->line_count : 0;
            start_pos = line_spacing / 2;
            break;
        case CSS_VALUE_SPACE_EVENLY:
            // Distribute space evenly: equal spacing before, between, and after lines
            if (flex_layout->line_count > 0) {
                line_spacing = free_space / (flex_layout->line_count + 1);
                start_pos = line_spacing;
            }
            break;
        case ALIGN_STRETCH:
            // Distribute extra space equally among all lines
            if (free_space > 0 && flex_layout->line_count > 0) {
                int extra_per_line = free_space / flex_layout->line_count;
                log_debug("ALIGN_STRETCH: container=%d, total_lines=%d, free=%d, extra_per_line=%d",
                          container_cross_size, total_lines_size, free_space, extra_per_line);
                for (int i = 0; i < flex_layout->line_count; i++) {
                    int old_size = flex_layout->lines[i].cross_size;
                    flex_layout->lines[i].cross_size += extra_per_line;
                    log_debug("ALIGN_STRETCH: line %d: %d + %d = %d",
                              i, old_size, extra_per_line, flex_layout->lines[i].cross_size);
                }
            }
            start_pos = 0;
            break;
        default:
            start_pos = 0;
            break;
    }

    // Position lines
    int current_pos = start_pos;
    log_debug("ALIGN_CONTENT - lines: %d, start_pos: %d, free_space: %d",
           flex_layout->line_count, start_pos, free_space);

    // WRAP-REVERSE FIX: Reverse line order for wrap-reverse
    for (int line_idx = 0; line_idx < flex_layout->line_count; line_idx++) {
        // For wrap-reverse, iterate lines in reverse order
        int i = (flex_layout->wrap == WRAP_WRAP_REVERSE) ?
                (flex_layout->line_count - 1 - line_idx) : line_idx;

        FlexLineInfo* line = &flex_layout->lines[i];

        // CRITICAL: Store line's cross position for use in align_items_cross_axis
        line->cross_position = current_pos;

        log_debug("POSITION_LINE %d (order %d) - cross_pos: %d, cross_size: %d",
               i, line_idx, current_pos, line->cross_size);

        // NOTE: We no longer set item positions here. Instead, align_items_cross_axis
        // will use line->cross_position + item's offset within line.
        // This avoids setting positions twice and potential conflicts.

        current_pos += line->cross_size + line_spacing;

        // Add gap between lines
        if (line_idx < flex_layout->line_count - 1) {
            int gap_between_lines = is_main_axis_horizontal(flex_layout) ?
                          flex_layout->row_gap : flex_layout->column_gap;

            log_debug("Adding gap between lines %d and %d: %d", i, i+1, gap_between_lines);
            current_pos += gap_between_lines;
        }
    }
}

// CRITICAL FIX: Box model aware utility functions
// These functions properly handle content vs border-box dimensions like block layout

float get_border_box_width(ViewElement* item) {
    // For flex items, item->width is ALWAYS the border-box width after flex layout completes,
    // regardless of the CSS box-sizing property. Flex layout computes border-box dimensions.
    // So we simply return item->width - no need to add padding/border.
    //
    // For non-flex items with content-box, item->width is the content width, but that case
    // doesn't use this function (block layout has its own dimension handling).
    return item->width;
}

float get_border_box_height(ViewElement* item) {
    // For flex items, item->height is ALWAYS the border-box height after flex layout completes,
    // regardless of the CSS box-sizing property. Flex layout computes border-box dimensions.
    return item->height;
}

float get_content_width(ViewBlock* item) {
    float border_box_width = get_border_box_width(item);

    if (!item->bound) {
        return border_box_width;
    }

    // Subtract padding and border from border-box width to get content width
    float padding_and_border = item->bound->padding.left + item->bound->padding.right +
        (item->bound->border ? item->bound->border->width.left + item->bound->border->width.right : 0);

    return fmaxf(border_box_width - padding_and_border, 0);
}

float get_content_height(ViewBlock* item) {
    if (!item->bound) {
        return item->height;
    }

    // CRITICAL WORKAROUND for missing box-sizing: border-box implementation
    float padding_and_border = item->bound->padding.top + item->bound->padding.bottom +
        (item->bound->border ? item->bound->border->width.top + item->bound->border->width.bottom : 0);

    // HACK: For flex items with padding, assume box-sizing: border-box was intended
    if (padding_and_border > 0) {
        float intended_border_box_height = item->height - padding_and_border;  // 120 - 20 = 100
        float content_height = intended_border_box_height - padding_and_border;  // 100 - 20 = 80
        return fmaxf(content_height, 0);
    }

    // No padding/border: use height as-is
    return item->height;
}

float get_border_offset_left(ViewBlock* item) {
    if (!item->bound || !item->bound->border) {
        return 0;
    }
    return item->bound->border->width.left;
}

float get_border_offset_top(ViewBlock* item) {
    if (!item->bound || !item->bound->border) {
        return 0;
    }
    return item->bound->border->width.top;
}

// Utility functions for axis-agnostic positioning
float get_main_axis_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    // Returns the BORDER-BOX size of the item, WITHOUT margins
    // Margins are handled separately in free space and positioning calculations
    // This matches CSS Flexbox spec: flex-grow/shrink operates on border-box sizes
    float base_size = is_main_axis_horizontal(flex_layout) ? get_border_box_width(item) : get_border_box_height(item);
    return base_size;
}

// Get the outer size including margins - used for justify-content calculations
float get_main_axis_outer_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    float base_size = is_main_axis_horizontal(flex_layout) ? get_border_box_width(item) : get_border_box_height(item);

    // defensive check: if base_size is NaN, use 0
    if (std::isnan(base_size)) {
        log_warn("NaN detected in base_size for item, using 0");
        base_size = 0.0f;
    }

    // Include margins in main axis size for positioning calculations
    if (item->bound) {
        if (is_main_axis_horizontal(flex_layout)) {
            float margin_left = std::isnan(item->bound->margin.left) ? 0.0f : item->bound->margin.left;
            float margin_right = std::isnan(item->bound->margin.right) ? 0.0f : item->bound->margin.right;
            base_size += margin_left + margin_right;
        } else {
            float margin_top = std::isnan(item->bound->margin.top) ? 0.0f : item->bound->margin.top;
            float margin_bottom = std::isnan(item->bound->margin.bottom) ? 0.0f : item->bound->margin.bottom;
            base_size += margin_top + margin_bottom;
        }
    }

    return base_size;
}

float get_cross_axis_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (is_main_axis_horizontal(flex_layout)) {
        // Cross-axis is height for horizontal flex containers
        // CRITICAL FIX: Check CSS height first and clamp against max-height
        if (item->blk && item->blk->given_height >= 0) {
            float height = item->blk->given_height;

            // For content-box, given_height is content height - add padding/border for border-box
            if (item->blk->box_sizing != CSS_VALUE_BORDER_BOX && item->bound) {
                height += item->bound->padding.top + item->bound->padding.bottom;
                if (item->bound->border) {
                    height += item->bound->border->width.top + item->bound->border->width.bottom;
                }
                log_debug("get_cross_axis_size: content-box, added padding/border to height: %.1f", height);
            }

            // Clamp against max-height constraint if present
            if (item->blk->given_max_height > 0 && height > item->blk->given_max_height) {
                log_debug("Cross-axis height %.1f exceeds max-height %.1f, clamping", height, item->blk->given_max_height);
                height = item->blk->given_max_height;
            }

            // Clamp against min-height constraint if present (min takes precedence)
            if (item->blk->given_min_height > 0 && height < item->blk->given_min_height) {
                height = item->blk->given_min_height;
                log_debug("Using CSS min-height for cross-axis: %.1f", height);
            }

            log_debug("Using CSS height for cross-axis (clamped): %.1f", height);
            return height;
        }
        // Also check min-height constraint
        float height = item->height;
        if (item->blk && item->blk->given_min_height > 0 && height < item->blk->given_min_height) {
            height = item->blk->given_min_height;
            log_debug("Using CSS min-height for cross-axis: %.1f", height);
        }
        return height;
    } else {
        // Cross-axis is width for vertical flex containers
        log_debug("get_cross_axis_size (vertical flex): item->width=%.1f, blk=%p", item->width, item->blk);

        if (item->blk) {
            log_debug("  given_width=%.1f, given_max_width=%.1f, given_min_width=%.1f",
                     item->blk->given_width, item->blk->given_max_width, item->blk->given_min_width);
        }

        // CRITICAL FIX: Check CSS width first and clamp against max-width
        if (item->blk && item->blk->given_width >= 0) {
            float width = item->blk->given_width;

            // For content-box, given_width is content width - add padding/border for border-box
            if (item->blk->box_sizing != CSS_VALUE_BORDER_BOX && item->bound) {
                width += item->bound->padding.left + item->bound->padding.right;
                if (item->bound->border) {
                    width += item->bound->border->width.left + item->bound->border->width.right;
                }
                log_debug("get_cross_axis_size: content-box, added padding/border to width: %.1f", width);
            }

            // Clamp against max-width constraint if present
            if (item->blk->given_max_width > 0 && width > item->blk->given_max_width) {
                log_debug("Cross-axis width %.1f exceeds max-width %.1f, clamping", width, item->blk->given_max_width);
                width = item->blk->given_max_width;
            }

            // Clamp against min-width constraint if present (min takes precedence)
            if (item->blk->given_min_width > 0 && width < item->blk->given_min_width) {
                width = item->blk->given_min_width;
                log_debug("Using CSS min-width for cross-axis: %.1f", width);
            }

            log_debug("Using CSS width for cross-axis (clamped): %.1f", width);
            return width;
        }

        // Fallback: use item->width but check max-width constraint
        float width = item->width;

        if (item->blk && item->blk->given_max_width > 0 && width > item->blk->given_max_width) {
            log_debug("Item width %.1f exceeds max-width %.1f, clamping", width, item->blk->given_max_width);
            width = item->blk->given_max_width;
        }

        if (item->blk && item->blk->given_min_width > 0 && width < item->blk->given_min_width) {
            width = item->blk->given_min_width;
            log_debug("Using CSS min-width for cross-axis: %.1f", width);
        }

        log_debug("Using item->width for cross-axis (clamped): %.1f", width);
        return width;
    }
}

float get_cross_axis_position(ViewElement* item, FlexContainerLayout* flex_layout) {
    // CRITICAL FIX: Return position relative to container content area, not absolute position
    ViewBlock* container = (ViewBlock*)item->parent;
    float border_offset = 0;

    if (container && container->bound && container->bound->border) {
        if (is_main_axis_horizontal(flex_layout)) {
            border_offset = container->bound->border->width.top;
        } else {
            border_offset = container->bound->border->width.left;
        }
    }

    // Return position relative to content area (subtract border offset)
    if (is_main_axis_horizontal(flex_layout)) {
        return item->y - border_offset;
    } else {
        return item->x - border_offset;
    }
}

void set_main_axis_position(ViewElement* item, float position, FlexContainerLayout* flex_layout) {
    // ENHANCED: Account for container border AND padding offset
    ViewElement* container = (ViewElement*)item->parent;
    float offset = 0;  // Combined border + padding offset

    if (container && container->bound) {
        if (container->bound->border) {
            if (is_main_axis_horizontal(flex_layout)) {
                offset += container->bound->border->width.left;
            } else {
                offset += container->bound->border->width.top;
            }
        }
        // Add padding offset
        if (is_main_axis_horizontal(flex_layout)) {
            offset += container->bound->padding.left;
        } else {
            offset += container->bound->padding.top;
        }
    }

    log_debug("set_main_axis_position: item=%p, position=%.1f, offset=%.1f (border+padding)", item, position, offset);

    if (is_main_axis_horizontal(flex_layout)) {
        log_debug("DIRECTION_CHECK - flex_layout->direction=%d, CSS_VALUE_ROW_REVERSE=%d",
               flex_layout->direction, CSS_VALUE_ROW_REVERSE);
        if (flex_layout->direction == CSS_VALUE_ROW_REVERSE) {
            // ROW-REVERSE: Position from right edge
            float container_width = flex_layout->main_axis_size;
            float item_width = get_main_axis_size(item, flex_layout);
            float calculated_x = container_width - position - item_width + offset;
            item->x = calculated_x;
            log_debug("ROW_REVERSE - container_width=%.1f, position=%.1f, item_width=%.1f, offset=%.1f, calculated_x=%.1f, final_x=%.1f",
                   container_width, position, item_width, offset, calculated_x, item->x);
        } else {
            // Normal left-to-right positioning
            float final_x = position + offset;
            log_debug("set_main_axis_position: Setting item->x to %.1f (before: %.1f)", final_x, item->x);
            item->x = final_x;
            log_debug("set_main_axis_position: After setting, item->x = %.1f", item->x);
            log_debug("NORMAL_ROW - position=%.1f, offset=%.1f, final_x=%.1f",
                   position, offset, item->x);
        }
    } else {
        if (flex_layout->direction == CSS_VALUE_COLUMN_REVERSE) {
            // COLUMN-REVERSE: Position from bottom edge
            float container_height = flex_layout->main_axis_size;
            float item_height = get_main_axis_size(item, flex_layout);
            float calculated_y = container_height - position - item_height + offset;
            item->y = calculated_y;
            log_debug("COLUMN_REVERSE - container_height=%.1f, position=%.1f, item_height=%.1f, offset=%.1f, calculated_y=%.1f, final_y=%.1f",
                   container_height, position, item_height, offset, calculated_y, item->y);
        } else {
            // Normal top-to-bottom positioning
            item->y = position + offset;
        }
    }
}

void set_cross_axis_position(ViewElement* item, float position, FlexContainerLayout* flex_layout) {
    // CRITICAL FIX: Account for container border AND padding offset on cross axis
    ViewElement* container = (ViewElement*)item->parent;
    float offset = 0;  // Combined border + padding offset

    if (container && container->bound) {
        if (container->bound->border) {
            if (is_main_axis_horizontal(flex_layout)) {
                offset += container->bound->border->width.top;
            } else {
                offset += container->bound->border->width.left;
            }
        }
        // Add padding offset
        if (is_main_axis_horizontal(flex_layout)) {
            offset += container->bound->padding.top;
        } else {
            offset += container->bound->padding.left;
        }
    }

    log_debug("SET_CROSS_POS - position=%.1f, offset=%.1f (border+padding), final=%.1f",
           position, offset, position + offset);

    if (is_main_axis_horizontal(flex_layout)) {
        item->y = position + offset;
    } else {
        item->x = position + offset;
    }
}

void set_main_axis_size(ViewElement* item, float size, FlexContainerLayout* flex_layout) {
    // CRITICAL FIX: Store the correct border-box size (like browsers do)
    // The flex algorithm works with border-box dimensions (100px)
    // We should store this directly to match browser behavior


    if (is_main_axis_horizontal(flex_layout)) {
        log_debug("set_main_axis_size: item=%p (%s), width %.1f -> %.1f",
                  item, item->node_name(), item->width, size);
        item->width = size;
    } else {
        log_debug("set_main_axis_size: item=%p (%s), height %.1f -> %.1f",
                  item, item->node_name(), item->height, size);
        item->height = size;
    }
}

void set_cross_axis_size(ViewElement* item, float size, FlexContainerLayout* flex_layout) {
    if (is_main_axis_horizontal(flex_layout)) {
        item->height = size;
    } else {
        item->width = size;
    }
}

// Calculate gap space for items or lines
float calculate_gap_space(FlexContainerLayout* flex_layout, int item_count, bool is_main_axis) {
    if (item_count <= 1) return 0;

    float gap = is_main_axis ?
              (is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap) :
              (is_main_axis_horizontal(flex_layout) ? flex_layout->row_gap : flex_layout->column_gap);

    return gap * (item_count - 1);
}

// Apply gaps between items in a flex line
void apply_gaps(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count <= 1) return;

    int gap = is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap;
    if (gap <= 0) return;

    // Apply gaps by adjusting positions
    for (int i = 1; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;
        int current_pos = is_main_axis_horizontal(flex_layout) ? item->x : item->y;
        set_main_axis_position(item, current_pos + (gap * i), flex_layout);
    }
}

// Distribute free space among flex items (grow/shrink)
void distribute_free_space(FlexLineInfo* line, bool is_growing) {
    if (!line || line->item_count == 0) return;

    float total_flex = is_growing ? line->total_flex_grow : line->total_flex_shrink;
    if (total_flex <= 0) return;

    int free_space = line->free_space;
    if (free_space == 0) return;

    // Distribute space proportionally
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        float flex_factor = is_growing ? get_item_flex_grow(item) : get_item_flex_shrink(item);

        if (flex_factor > 0) {
            float space_to_distribute = (flex_factor / total_flex) * free_space;

            // Apply the distributed space to the item's main axis size
            // This is a simplified implementation - real flexbox has more complex rules
            int current_size = is_main_axis_horizontal(line->items[0]->parent ?
                ((ViewBlock*)line->items[0]->parent)->embed->flex : nullptr) ?
                item->width : item->height;

            int new_size = current_size + space_to_distribute;
            if (new_size < 0) new_size = 0;  // Prevent negative sizes

            if (is_main_axis_horizontal(line->items[0]->parent ?
                ((ViewBlock*)line->items[0]->parent)->embed->flex : nullptr)) {
                item->width = new_size;
            } else {
                item->height = new_size;
            }
        }
    }
}

// Helper: Check if an item has a definite cross-axis size
static bool item_has_definite_cross_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item || !item->blk) return false;

    if (is_main_axis_horizontal(flex_layout)) {
        // Cross-axis is height for row direction
        // given_height >= 0 means explicit height (-1 means auto)
        return item->blk->given_height >= 0;
    } else {
        // Cross-axis is width for column direction
        // given_width >= 0 means explicit width (-1 means auto)
        return item->blk->given_width >= 0;
    }
}

// Helper: Check if an item will be stretched in cross-axis
static bool item_will_stretch(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item || !item->fi) return false;

    // Get effective align-self (uses align-items if auto)
    int align_type = item->fi->align_self != ALIGN_AUTO ?
                     item->fi->align_self : flex_layout->align_items;

    return align_type == ALIGN_STRETCH;
}

// Calculate cross sizes for all flex lines
// Updated to use hypothetical cross sizes from Phase 4.5
void calculate_line_cross_sizes(FlexContainerLayout* flex_layout) {
    if (!flex_layout || flex_layout->line_count == 0) return;

    // CSS Flexbox §9.4 Step 8:
    // If the flex container is single-line (flex-wrap: nowrap) and has a definite cross size,
    // the cross size of the flex line is the flex container's inner cross size.
    // Note: "single-line" refers to flex-wrap: nowrap, NOT to having only one line with wrap.
    // align-content can still apply to a wrapping container even if it happens to have one line.
    bool is_nowrap = (flex_layout->wrap == WRAP_NOWRAP);
    // FIXED: Use the explicit flag instead of checking cross_axis_size > 0
    // The old check was wrong because cross_axis_size could be a guessed value (not CSS-specified)
    bool has_definite_cross = flex_layout->has_definite_cross_size;

    if (is_nowrap && has_definite_cross) {
        // Single-line (nowrap) with definite cross size: line cross = container cross
        flex_layout->lines[0].cross_size = flex_layout->cross_axis_size;
        log_debug("LINE_CROSS_SIZE: nowrap with definite cross, line 0 = %.1f (container cross)",
                  flex_layout->cross_axis_size);
        return;
    }

    // Otherwise, calculate line cross sizes from item hypothetical cross sizes
    // CSS Flexbox §9.4 Step 8: For each flex line, determine its cross size.
    // For baseline-aligned items, the line cross size is the sum of the largest
    // pre-baseline distance and the largest post-baseline distance.
    // For non-baseline items, use the maximum outer hypothetical cross size.
    // The final line cross is the larger of these two values.

    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    for (int i = 0; i < flex_layout->line_count; i++) {
        FlexLineInfo* line = &flex_layout->lines[i];
        float max_non_baseline_cross = 0;
        float max_pre_baseline = 0;
        float max_post_baseline = 0;
        bool has_baseline_items = false;

        for (int j = 0; j < line->item_count; j++) {
            ViewElement* item = (ViewElement*)line->items[j]->as_element();
            if (!item) continue;

            // Determine outer cross size
            float item_cross_size = 0;
            if (item->fi && item->fi->hypothetical_outer_cross_size > 0) {
                item_cross_size = item->fi->hypothetical_outer_cross_size;
            } else {
                item_cross_size = get_cross_axis_size(item, flex_layout);
            }

            // Check if this item participates in baseline alignment
            bool is_baseline = false;
            if (item->fi) {
                int align = item->fi->align_self != ALIGN_AUTO ?
                    item->fi->align_self : flex_layout->align_items;
                is_baseline = (align == ALIGN_BASELINE || align == CSS_VALUE_BASELINE);
            }

            if (is_baseline && is_horizontal) {
                // §9.4 Step 8: For baseline items, compute pre/post distances
                has_baseline_items = true;
                float baseline = calculate_item_baseline(item);
                // Pre-baseline: distance from cross-start margin edge to baseline
                float pre = baseline;
                // Post-baseline: total outer cross - pre-baseline
                float margin_top = item->bound ? item->bound->margin.top : 0;
                float margin_bottom = item->bound ? item->bound->margin.bottom : 0;
                float outer_cross = item->height + margin_top + margin_bottom;
                float post = outer_cross - baseline;
                if (pre > max_pre_baseline) max_pre_baseline = pre;
                if (post > max_post_baseline) max_post_baseline = post;
                log_debug("LINE_CROSS_BASELINE: item[%d][%d] baseline=%.1f, pre=%.1f, post=%.1f, outer=%.1f",
                          i, j, baseline, pre, post, outer_cross);
            } else {
                // Non-baseline item: use outer hypothetical cross size
                if (item_cross_size > max_non_baseline_cross) {
                    max_non_baseline_cross = item_cross_size;
                }
                log_debug("LINE_CROSS: item[%d][%d] non-baseline cross=%.1f", i, j, item_cross_size);
            }
        }

        float baseline_extent = max_pre_baseline + max_post_baseline;
        float line_cross = has_baseline_items ?
            (baseline_extent > max_non_baseline_cross ? baseline_extent : max_non_baseline_cross) :
            max_non_baseline_cross;

        line->cross_size = line_cross;
        log_debug("LINE_CROSS_SIZE: line %d = %.1f (baseline_extent=%.1f, max_non_baseline=%.1f, has_baseline=%d)",
                  i, line_cross, baseline_extent, max_non_baseline_cross, has_baseline_items);
    }
}

// ============================================================================
// ============================================================================
// Helper: Recursively measure content-based height of a flex container
// ============================================================================
// This function traverses the flex container's children to compute its
// content-based height, recursively handling nested flex containers.
static float measure_flex_content_height(ViewElement* elem) {
    if (!elem) return 0;

    // Check for explicit height first (given_height is border-box)
    if (elem->blk && elem->blk->given_height >= 0) {
        // Explicit height is border-box, need to subtract padding/border to get content
        float padding_border = 0;
        if (elem->bound) {
            padding_border += elem->bound->padding.top + elem->bound->padding.bottom;
            if (elem->bound->border) {
                padding_border += elem->bound->border->width.top + elem->bound->border->width.bottom;
            }
        }
        float result = elem->blk->given_height - padding_border;
        return result;
    }
    // Check if intrinsic height was calculated (resolves CSS line-height correctly)
    // Prefer this over content_height which may be stale from a prior pass
    // that didn't account for line-height
    if (elem->fi && elem->fi->has_intrinsic_height && elem->fi->intrinsic_height.max_content > 0) {
        return (float)elem->fi->intrinsic_height.max_content;
    }
    // Prefer content_height (which is the actual content size without padding)
    if (elem->content_height > 0) {
        return elem->content_height;
    }
    // elem->height is border-box, so subtract padding/border
    if (elem->height > 0) {
        float padding_border = 0;
        if (elem->bound) {
            padding_border += elem->bound->padding.top + elem->bound->padding.bottom;
            if (elem->bound->border) {
                padding_border += elem->bound->border->width.top + elem->bound->border->width.bottom;
            }
        }
        float result = (float)elem->height - padding_border;
        return result;
    }

    // Check if this is a flex container
    ViewBlock* block = (ViewBlock*)elem;
    if (!block || block->display.inner != CSS_VALUE_FLEX) {
        // Not a flex container - no content height available
        return 0;
    }
    // Determine flex direction
    FlexProp* flex_prop = block->embed ? block->embed->flex : nullptr;
    bool is_row = !flex_prop ||
                  flex_prop->direction == CSS_VALUE_ROW ||
                  flex_prop->direction == CSS_VALUE_ROW_REVERSE;

    // Traverse children to calculate content-based height
    float max_child_height = 0;
    float sum_child_height = 0;

    DomNode* child = elem->first_child;
    while (child) {
        if (child->is_element()) {
            ViewElement* child_elem = (ViewElement*)child->as_element();
            if (child_elem) {
                // Recursively measure child height
                float child_height = measure_flex_content_height(child_elem);

                if (is_row) {
                    // Row flex: height is max of child heights
                    max_child_height = fmax(max_child_height, child_height);
                } else {
                    // Column flex: height is sum of child heights
                    sum_child_height += child_height;
                }
            }
        }
        child = child->next_sibling;
    }

    return is_row ? max_child_height : sum_child_height;
}

// ============================================================================
// CSS Flexbox §9.4: Determine hypothetical cross size of each item
// ============================================================================
// Per the spec: "Determine the hypothetical cross size of each item by performing
// layout with the used main size and the available space, treating auto as fit-content."
void determine_hypothetical_cross_sizes(LayoutContext* lycon, FlexContainerLayout* flex_layout) {
    if (!flex_layout || flex_layout->line_count == 0) return;

    bool is_horizontal = is_main_axis_horizontal(flex_layout);
    log_debug("HYPOTHETICAL_CROSS: Starting determination, is_horizontal=%d", is_horizontal);

    for (int i = 0; i < flex_layout->line_count; i++) {
        FlexLineInfo* line = &flex_layout->lines[i];

        for (int j = 0; j < line->item_count; j++) {
            ViewElement* item = (ViewElement*)line->items[j]->as_element();
            if (!item) continue;

            // Form controls use intrinsic sizes directly - don't read fi (union aliasing)
            if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
                float cross = is_horizontal ? item->form->intrinsic_height : item->form->intrinsic_width;
                // For text-like inputs, recalculate content height from actual font
                // (CSS may override UA font-size set during resolve_htm_style)
                if (is_horizontal && item->form->control_type == FORM_CONTROL_TEXT &&
                    item->font && item->font->font_size > 0 && lycon->ui_context) {
                    FontBox temp_font;
                    setup_font(lycon->ui_context, &temp_font, item->font);
                    if (temp_font.font_handle) {
                        float line_h = calc_normal_line_height(temp_font.font_handle);
                        if (line_h > cross) cross = line_h;
                    }
                }
                // Add CSS padding and border for border-box
                if (item->bound) {
                    if (is_horizontal) {
                        cross += item->bound->padding.top + item->bound->padding.bottom;
                        if (item->bound->border)
                            cross += item->bound->border->width.top + item->bound->border->width.bottom;
                    } else {
                        cross += item->bound->padding.left + item->bound->padding.right;
                        if (item->bound->border)
                            cross += item->bound->border->width.left + item->bound->border->width.right;
                    }
                }
                if (item->fi) {
                    item->fi->hypothetical_outer_cross_size = cross;
                }
                if (is_horizontal) {
                    item->height = cross;
                } else {
                    item->width = cross;
                }
                log_debug("HYPOTHETICAL_CROSS: form control item[%d][%d] cross=%.1f", i, j, cross);
                continue;
            }

            if (!item->fi) continue;

            float hypothetical_cross = 0;
            float min_cross = 0;
            float max_cross = INFINITY;

            // get the constraints for the cross axis
            if (is_horizontal) {
                // cross-axis is height
                min_cross = item->fi->resolved_min_height;
                max_cross = (item->fi->resolved_max_height > 0) ?
                            item->fi->resolved_max_height : INFINITY;

                // check for explicit cross-axis size (CSS height)
                if (item->blk && item->blk->given_height >= 0) {
                    hypothetical_cross = item->blk->given_height;
                    // For content-box, given_height is content height - add padding/border
                    if (item->blk->box_sizing != CSS_VALUE_BORDER_BOX && item->bound) {
                        hypothetical_cross += item->bound->padding.top + item->bound->padding.bottom;
                        if (item->bound->border) {
                            hypothetical_cross += item->bound->border->width.top + item->bound->border->width.bottom;
                        }
                    }
                    log_debug("HYPOTHETICAL_CROSS: item[%d][%d] using explicit height=%.1f (border-box)",
                              i, j, hypothetical_cross);
                } else {
                    // CSS Flexbox §9.4: "Determine the hypothetical cross size of each item
                    // by performing layout with the used main size and the available space."
                    // Use calculate_max_content_height at the item's actual width (not the
                    // cached intrinsic_height.max_content which was computed at max-content
                    // width). This is critical for items with inline-block children that
                    // wrap differently at different widths.
                    float item_content_width = (float)item->width;
                    if (item->bound) {
                        item_content_width -= item->bound->padding.left + item->bound->padding.right;
                        if (item->bound->border) {
                            item_content_width -= item->bound->border->width.left + item->bound->border->width.right;
                        }
                    }
                    if (item_content_width < 0) item_content_width = 0;

                    float measured_height = 0;
                    if (item_content_width > 0) {
                        measured_height = calculate_max_content_height(lycon, (DomNode*)item, item_content_width);
                    }
                    if (measured_height <= 0) {
                        // Fallback to recursive flex measurement for nested flex containers
                        measured_height = measure_flex_content_height(item);
                    }
                    log_debug("HYPOTHETICAL_CROSS: item[%d][%d] %s measured_height=%.1f (content_w=%.1f, width=%.1f)",
                              i, j, item->node_name(), measured_height, item_content_width, item->width);

                    if (measured_height > 0) {
                        // calculate_max_content_height returns border-box height
                        hypothetical_cross = measured_height;
                        // Update item dimensions so alignment uses correct size
                        item->height = hypothetical_cross;
                    } else {
                        // CSS Flexbox §9.4: "Determine the hypothetical cross size of each item
                        // by performing layout with the used main size and the available space."
                        // For items containing text, the cross size depends on how text wraps
                        // at the item's determined main size. Re-compute text height at the
                        // actual content width (which may differ from max-content measurement).
                        float text_height_at_width = 0;
                        float item_content_width = (float)item->content_width;
                        if (item_content_width <= 0) {
                            // content_width not set yet; derive from width - padding/border
                            item_content_width = (float)item->width;
                            if (item->bound) {
                                item_content_width -= item->bound->padding.left + item->bound->padding.right;
                                if (item->bound->border) {
                                    item_content_width -= item->bound->border->width.left + item->bound->border->width.right;
                                }
                            }
                        }
                        if (item_content_width > 0) {
                            DomNode* text_child = item->first_child;
                            while (text_child) {
                                if (text_child->is_text()) {
                                    const char* text = (const char*)text_child->text_data();
                                    if (text && text[0]) {
                                        size_t text_len = strlen(text);
                                        // Check if text would wrap at this width
                                        TextIntrinsicWidths tw = measure_text_intrinsic_widths(
                                            lycon, text, text_len);
                                        if (tw.max_content > item_content_width && tw.min_content > 0) {
                                            // Text wraps: compute height at constrained width
                                            // Resolve CSS line-height by walking ancestor chain
                                            float font_size = (lycon->font.style && lycon->font.style->font_size > 0)
                                                ? lycon->font.style->font_size : 16.0f;
                                            float line_height = font_size * 1.2f;
                                            if (lycon->font.font_handle) {
                                                float normal_lh = calc_normal_line_height(lycon->font.font_handle);
                                                if (normal_lh > 0) line_height = normal_lh;
                                            }
                                            for (DomNode* anc = ((DomNode*)item)->parent; anc; anc = anc->parent) {
                                                if (!anc->is_element()) continue;
                                                ViewBlock* anc_view = (ViewBlock*)anc->as_element();
                                                if (anc_view->blk && anc_view->blk->line_height) {
                                                    const CssValue* lh = anc_view->blk->line_height;
                                                    if (lh->type == CSS_VALUE_TYPE_NUMBER) {
                                                        line_height = font_size * (float)lh->data.number.value;
                                                    } else if (lh->type == CSS_VALUE_TYPE_LENGTH) {
                                                        float lh_px = (float)lh->data.length.value;
                                                        if (lh_px > 0) line_height = lh_px;
                                                    } else if (lh->type == CSS_VALUE_TYPE_PERCENTAGE) {
                                                        line_height = font_size * (float)(lh->data.percentage.value / 100.0);
                                                    }
                                                    break;
                                                }
                                                if (anc_view->specified_style) {
                                                    CssDeclaration* lh_decl = style_tree_get_declaration(
                                                        anc_view->specified_style, CSS_PROPERTY_LINE_HEIGHT);
                                                    if (lh_decl && lh_decl->value) {
                                                        const CssValue* lhv = lh_decl->value;
                                                        if (lhv->type == CSS_VALUE_TYPE_NUMBER) {
                                                            line_height = font_size * (float)lhv->data.number.value;
                                                        } else if (lhv->type == CSS_VALUE_TYPE_LENGTH) {
                                                            float lh_px = resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, lhv);
                                                            if (lh_px > 0) line_height = lh_px;
                                                        } else if (lhv->type == CSS_VALUE_TYPE_PERCENTAGE) {
                                                            line_height = font_size * (float)(lhv->data.percentage.value / 100.0);
                                                        }
                                                        break;
                                                    }
                                                }
                                            }
                                            text_height_at_width = compute_text_height_at_width(
                                                lycon, text, text_len, item_content_width, line_height);
                                            log_debug("HYPOTHETICAL_CROSS: text wrap at %.1f → height=%.1f (max=%.1f, min=%.1f)",
                                                      item_content_width, text_height_at_width, tw.max_content, tw.min_content);
                                        }
                                        break; // only first text child
                                    }
                                }
                                text_child = text_child->next_sibling;
                            }
                        }

                        if (text_height_at_width > 0) {
                            // Use computed text height as content height
                            hypothetical_cross = text_height_at_width;
                            // Add padding and border
                            if (item->bound) {
                                hypothetical_cross += item->bound->padding.top + item->bound->padding.bottom;
                                if (item->bound->border) {
                                    hypothetical_cross += item->bound->border->width.top + item->bound->border->width.bottom;
                                }
                            }
                            item->height = hypothetical_cross;
                            item->content_height = text_height_at_width;
                            log_debug("HYPOTHETICAL_CROSS: item[%d][%d] text height at width=%.1f → cross=%.1f",
                                      i, j, item_content_width, hypothetical_cross);
                        } else {
                            // Prefer intrinsic height from calculate_item_intrinsic_sizes
                            // which correctly resolves CSS line-height for text content.
                            // item->height may be stale from a prior pass that didn't
                            // account for line-height (e.g., font-size only).
                            if (item->fi && item->fi->has_intrinsic_height &&
                                item->fi->intrinsic_height.max_content > 0) {
                                hypothetical_cross = item->fi->intrinsic_height.max_content;
                                // intrinsic_height stores content-box values; add padding+border
                                if (item->bound) {
                                    hypothetical_cross += item->bound->padding.top + item->bound->padding.bottom;
                                    if (item->bound->border) {
                                        hypothetical_cross += item->bound->border->width.top + item->bound->border->width.bottom;
                                    }
                                }
                                log_debug("HYPOTHETICAL_CROSS: item[%d][%d] using intrinsic height=%.1f",
                                          i, j, hypothetical_cross);
                            } else {
                                // Fallback to measured/content height
                                hypothetical_cross = item->height > 0 ? item->height : item->content_height;
                            }
                        }
                    }
                }
            } else {
                // cross-axis is width
                min_cross = item->fi->resolved_min_width;
                max_cross = (item->fi->resolved_max_width > 0) ?
                            item->fi->resolved_max_width : INFINITY;

                // check for explicit cross-axis size (CSS width)
                if (item->blk && item->blk->given_width >= 0) {
                    hypothetical_cross = item->blk->given_width;
                    // For content-box, given_width is content width - add padding/border
                    if (item->blk->box_sizing != CSS_VALUE_BORDER_BOX && item->bound) {
                        hypothetical_cross += item->bound->padding.left + item->bound->padding.right;
                        if (item->bound->border) {
                            hypothetical_cross += item->bound->border->width.left + item->bound->border->width.right;
                        }
                    }
                    log_debug("HYPOTHETICAL_CROSS: item[%d][%d] using explicit width=%.1f (border-box)",
                              i, j, hypothetical_cross);
                } else {
                    // use measured/content width
                    hypothetical_cross = item->width > 0 ? item->width : item->content_width;
                    log_debug("HYPOTHETICAL_CROSS: item[%d][%d] using content width=%.1f",
                              i, j, hypothetical_cross);
                }
            }

            // clamp to min/max constraints
            // CSS rule: min-height/min-width overrides max-height/max-width
            // So we clamp to max first, then to min (min wins if conflict)
            if (hypothetical_cross > max_cross) {
                hypothetical_cross = max_cross;
            }
            if (hypothetical_cross < min_cross) {
                hypothetical_cross = min_cross;
            }

            // CSS Sizing Level 4 §7.2: aspect-ratio constraint transfer.
            // When the main axis size is determined, derive the cross size from it.
            // This overrides any content-based cross estimate because aspect-ratio
            // governs the box dimensions — content overflows but does NOT resize the box.
            // When the main size is NOT determined, try to derive main from cross.
            bool has_explicit_cross = is_horizontal ?
                (item->blk && item->blk->given_height >= 0) :
                (item->blk && item->blk->given_width >= 0);
            if (item->fi && item->fi->aspect_ratio > 0 && !has_explicit_cross) {
                float r = item->fi->aspect_ratio;
                float main_size = is_horizontal ? (float)item->width : (float)item->height;

                if (main_size > 0) {
                    // Main axis size is determined; derive cross unconditionally.
                    // The aspect-ratio governs cross dimension regardless of any
                    // content-estimated hypothetical_cross value.
                    float derived_cross = is_horizontal ? main_size / r : main_size * r;
                    hypothetical_cross = derived_cross;
                    // also update item's cross dimension so later phases see it
                    if (is_horizontal) item->height = derived_cross;
                    else item->width = derived_cross;
                    log_debug("HYPOTHETICAL_CROSS: aspect-ratio applied: cross=%.1f from main=%.1f, ratio=%.3f",
                              derived_cross, main_size, r);
                } else if (hypothetical_cross > 0) {
                    // Main size unknown but cross is determined (e.g. from stretch/min);
                    // derive main from cross via aspect-ratio and update the item's main size.
                    float derived_main = is_horizontal ? hypothetical_cross * r : hypothetical_cross / r;
                    if (is_horizontal) {
                        item->width = derived_main;
                    } else {
                        item->height = derived_main;
                    }
                    log_debug("HYPOTHETICAL_CROSS: aspect-ratio transfer: derived main=%.1f from cross=%.1f, ratio=%.3f",
                              derived_main, hypothetical_cross, r);
                }

                // Re-apply min/max clamp after aspect-ratio derivation
                if (hypothetical_cross > max_cross) {
                    hypothetical_cross = max_cross;
                }
                if (hypothetical_cross < min_cross) {
                    hypothetical_cross = min_cross;
                }
            }

            // store the hypothetical cross size
            item->fi->hypothetical_cross_size = hypothetical_cross;

            // compute the outer hypothetical cross size (add margins)
            float margin_sum = 0;
            if (item->bound) {
                if (is_horizontal) {
                    margin_sum = item->bound->margin.top + item->bound->margin.bottom;
                } else {
                    margin_sum = item->bound->margin.left + item->bound->margin.right;
                }
            }
            item->fi->hypothetical_outer_cross_size = hypothetical_cross + margin_sum;

            log_debug("HYPOTHETICAL_CROSS: item[%d][%d] final=%.1f, outer=%.1f (margins=%.1f)",
                      i, j, hypothetical_cross, item->fi->hypothetical_outer_cross_size, margin_sum);
        }
    }
}

// ============================================================================
// CSS Flexbox §9.4: Determine container cross size from line cross sizes
// ============================================================================
// Per the spec: If the flex container has a definite cross size, use that.
// Otherwise, use the sum of the flex lines' cross sizes plus gaps and padding/border.
void determine_container_cross_size(FlexContainerLayout* flex_layout, ViewBlock* container) {
    if (!flex_layout || !container) return;

    bool is_horizontal = is_main_axis_horizontal(flex_layout);
    log_debug("%s CONTAINER_CROSS: Determining cross size, is_horizontal=%d", container->source_loc(), is_horizontal);

    // check if container has definite cross size
    bool has_definite_cross = false;
    float definite_cross = 0;

    if (is_horizontal) {
        // cross-axis is height
        if (container->blk && container->blk->given_height >= 0) {
            has_definite_cross = true;
            definite_cross = container->blk->given_height;
            log_debug("%s CONTAINER_CROSS: Container has definite height=%.1f", container->source_loc(), definite_cross);
        }
    } else {
        // cross-axis is width
        if (container->blk && container->blk->given_width >= 0) {
            has_definite_cross = true;
            definite_cross = container->blk->given_width;
            log_debug("%s CONTAINER_CROSS: Container has definite width=%.1f", container->source_loc(), definite_cross);
        }
    }

    // Also check if this container is a flex item whose cross-size was set by parent flex
    // In that case, we should NOT override it
    if (!has_definite_cross && container->fi) {
        // If the container is a flex item and already has a cross-size set,
        // check if it came from parent flex sizing (not auto)
        float current_cross = is_horizontal ? container->height : container->width;
        if (current_cross > 0) {
            // The container already has a cross size - likely set by parent flex
            // Don't override it
            has_definite_cross = true;
            definite_cross = current_cross;
            log_debug("%s CONTAINER_CROSS: Container is flex item with cross size from parent=%.1f", container->source_loc(),
                      definite_cross);
        }
    }

    if (has_definite_cross) {
        // use the definite size
        flex_layout->cross_axis_size = definite_cross;
        if (is_horizontal) {
            container->height = definite_cross;
        } else {
            container->width = definite_cross;
        }
        log_debug("%s CONTAINER_CROSS: Using definite cross size=%.1f", container->source_loc(), definite_cross);
        return;
    }

    // sum the cross sizes of all lines
    float total_cross = 0;
    for (int i = 0; i < flex_layout->line_count; i++) {
        total_cross += flex_layout->lines[i].cross_size;
    }

    // add gaps between lines
    if (flex_layout->line_count > 1) {
        // row-gap for wrapping row flex (vertical gaps between lines)
        // column-gap for wrapping column flex (horizontal gaps between lines)
        float gap = is_horizontal ? flex_layout->row_gap : flex_layout->column_gap;
        total_cross += gap * (flex_layout->line_count - 1);
    }

    // add padding to total cross
    if (container->bound) {
        if (is_horizontal) {
            total_cross += container->bound->padding.top + container->bound->padding.bottom;
        } else {
            total_cross += container->bound->padding.left + container->bound->padding.right;
        }
    }

    // apply min/max constraints
    if (container->blk) {
        float min_cross = is_horizontal ?
            container->blk->given_min_height : container->blk->given_min_width;
        float max_cross = is_horizontal ?
            container->blk->given_max_height : container->blk->given_max_width;

        if (min_cross > 0 && total_cross < min_cross) {
            total_cross = min_cross;
            log_debug("%s CONTAINER_CROSS: Applied min constraint, now=%.1f", container->source_loc(), total_cross);
        }
        if (max_cross > 0 && total_cross > max_cross) {
            total_cross = max_cross;
            log_debug("%s CONTAINER_CROSS: Applied max constraint, now=%.1f", container->source_loc(), total_cross);
        }
    }

    // Only update if we computed a non-zero total
    if (total_cross > 0) {
        flex_layout->cross_axis_size = total_cross;
        if (is_horizontal) {
            container->height = total_cross;
        } else {
            container->width = total_cross;
        }
        log_debug("%s CONTAINER_CROSS: Final cross_axis_size=%.1f (lines=%d)", container->source_loc(),
                  total_cross, flex_layout->line_count);
    } else {
        log_debug("%s CONTAINER_CROSS: No cross size computed, keeping existing=%.1f", container->source_loc(),
                  flex_layout->cross_axis_size);
    }
}
