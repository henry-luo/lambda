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
    // If it's a flex container with no children, it's empty
    // Note: We can't rely on display.inner here as styles may not be resolved yet
    // Just check if it has children - if no children, assume empty
    return elem->first_child == nullptr;
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
        log_debug("init_flex_container: source->direction=%d (0x%04X), row=%d, col=%d",
                  source->direction, source->direction, DIR_ROW, DIR_COLUMN);
        memcpy(flex, container->embed->flex, sizeof(FlexProp));
        flex->lycon = lycon;  // Restore after memcpy
        log_debug("init_flex_container: after copy flex->direction=%d", flex->direction);
    }
    else {
        // Set default values using enum names that now align with Lexbor constants
        log_debug("init_flex_container: NO embed->flex, using defaults (row)");
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
    int content_width = container->width;
    int content_height = container->height;

    // Use given_height if container has explicit height (before container->height is set)
    if (container->blk && container->blk->given_height > 0 && content_height <= 0) {
        content_height = (int)container->blk->given_height;
        log_debug("init_flex_container: using given_height=%d for content_height", content_height);
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
    bool has_explicit_height = container->blk && container->blk->given_height > 0;
    bool has_explicit_width = container->blk && container->blk->given_width > 0;

    // Resolve percentage gaps to actual pixel values
    // Per CSS spec, gap percentages are resolved against the content box dimension
    // in the corresponding axis (row-gap uses height, column-gap uses width)
    // For auto-size containers, percentage gaps resolve to 0
    if (container->embed && container->embed->flex) {
        FlexProp* source = container->embed->flex;
        if (source->row_gap_is_percent) {
            if (has_explicit_height && content_height > 0) {
                float resolved_gap = (source->row_gap / 100.0f) * content_height;
                log_debug("init_flex_container: resolving row_gap from %.1f%% to %.1fpx (height=%d)",
                          source->row_gap, resolved_gap, content_height);
                flex->row_gap = resolved_gap;
            } else {
                // Auto-height container: percentage gap resolves to 0
                log_debug("init_flex_container: row_gap %.1f%% resolves to 0 (auto-height container)",
                          source->row_gap);
                flex->row_gap = 0;
            }
            flex->row_gap_is_percent = false;  // Now it's resolved
        }
        if (source->column_gap_is_percent) {
            if (has_explicit_width && content_width > 0) {
                float resolved_gap = (source->column_gap / 100.0f) * content_width;
                log_debug("init_flex_container: resolving column_gap from %.1f%% to %.1fpx (width=%d)",
                          source->column_gap, resolved_gap, content_width);
                flex->column_gap = resolved_gap;
            } else {
                // Auto-width container: percentage gap resolves to 0
                log_debug("init_flex_container: column_gap %.1f%% resolves to 0 (auto-width container)",
                          source->column_gap);
                flex->column_gap = 0;
            }
            flex->column_gap_is_percent = false;  // Now it's resolved
        }
    }

    bool is_horizontal = is_main_axis_horizontal(flex);

    // Check if this is an absolutely positioned element with auto width (shrink-to-fit)
    // Also check for min-width/max-width constraints - if present, don't use shrink-to-fit
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

    if (is_horizontal) {
        // For row flex, main axis is width
        // If container is absolutely positioned with auto width, use shrink-to-fit
        if (is_absolute_no_width) {
            // Defer width calculation to layout phase (shrink-to-fit)
            flex->main_axis_size = 0.0f;
            log_debug("init_flex_container: absolute row flex with auto-width, deferring main_axis_size");
        } else {
            flex->main_axis_size = content_width > 0 ? (float)content_width : 0.0f;
        }
        flex->cross_axis_size = content_height > 0 ? (float)content_height : 0.0f;
    } else {
        flex->main_axis_size = content_height > 0 ? (float)content_height : 0.0f;
        // For column flex, cross axis is width
        // If container is absolutely positioned with auto width, use shrink-to-fit
        if (is_absolute_no_width) {
            // Defer width calculation to layout phase (shrink-to-fit)
            flex->cross_axis_size = 0.0f;
            log_debug("init_flex_container: absolute column flex with auto-width, deferring cross_axis_size");
        } else {
            flex->cross_axis_size = content_width > 0 ? (float)content_width : 0.0f;
        }
    }
    log_debug("init_flex_container: main_axis_size=%.1f, cross_axis_size=%.1f (content: %dx%d)",
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
                log_debug("init_flex_container: max-width is constraining (content=%.1f, max=%.1f)",
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
                log_debug("init_flex_container: block-level element has definite width from containing block (%.1f)",
                          (float)content_width);
            }
        }

        // CRITICAL FIX: If this container already has a width set by a parent flex algorithm,
        // treat it as definite. This prevents nested flex containers from overriding the
        // width that was calculated by the parent's flex item sizing.
        // Exception: absolute-positioned elements with auto width get their containing block's
        // width as fallback, so we must NOT treat that as definite.
        if (!has_definite_width && container->width > 0 && !is_absolute_no_width) {
            has_definite_width = true;
            log_debug("init_flex_container: using width set by parent (%.1f)",
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
                log_debug("init_flex_container: max-height is constraining (content=%.1f, max=%.1f)",
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
            log_debug("init_flex_container: using height set by parent (%.1f)",
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

    log_debug("init_flex_container: main_axis_is_indefinite=%s, has_definite_cross_size=%s (is_absolute=%s, is_horizontal=%s, has_width=%s, has_height=%s, has_max_width=%s, has_max_height=%s)",
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
    log_info("FLEX START - container: %dx%d at (%d,%d)",
           container->width, container->height, container->x, container->y);
    log_debug("FLEX PROPERTIES - direction=%d, align_items=%d, justify=%d, wrap=%d",
           flex_layout->direction, flex_layout->align_items, flex_layout->justify, flex_layout->wrap);
    // DEBUG: Gap settings applied

    // Set main and cross axis sizes from container dimensions (only if not already set)
    // DEBUG: Container dimensions calculated
    if (flex_layout->main_axis_size == 0.0f || flex_layout->cross_axis_size == 0.0f) {
        // CRITICAL FIX: Use container width/height and calculate content dimensions
        // The content dimensions should exclude borders and padding
        int content_width = container->width;
        int content_height = container->height;

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

        log_debug("FLEX CONTENT - content: %dx%d, container: %dx%d",
               content_width, content_height, container->width, container->height);

        // Axis orientation now calculated correctly with aligned enum values
        bool is_horizontal = is_main_axis_horizontal(flex_layout);

        log_debug("AXIS INIT - before: main=%.1f, cross=%.1f, content=%dx%d",
               flex_layout->main_axis_size, flex_layout->cross_axis_size, content_width, content_height);
        log_debug("AXIS INIT - flex_layout pointer: %p", (void*)flex_layout);

        if (is_horizontal) {
            log_debug("AXIS INIT - horizontal branch");
            log_debug("AXIS INIT - main condition: %s (main=%.1f)",
                   (flex_layout->main_axis_size == 0.0f) ? "true" : "false", flex_layout->main_axis_size);
            if (flex_layout->main_axis_size == 0.0f) {
                // ROW FLEX with auto width - check if this is shrink-to-fit case
                // For shrink-to-fit, calculate width from flex items
                bool has_explicit_width = container->blk && container->blk->given_width > 0;
                // Also check for min-width/max-width constraints
                bool has_min_width = container->blk && container->blk->given_min_width > 0;
                bool has_max_width = container->blk && container->blk->given_max_width > 0;
                bool is_absolute = container->position &&
                    (container->position->position == CSS_VALUE_ABSOLUTE ||
                     container->position->position == CSS_VALUE_FIXED);
                // Only use shrink-to-fit if truly auto-width (no width, min-width, or max-width constraints)
                bool is_absolute_no_width = is_absolute && !has_explicit_width && !has_min_width && !has_max_width &&
                    !(container->position && container->position->has_left && container->position->has_right);

                if (is_absolute_no_width) {
                    // Calculate width from flex items (shrink-to-fit)
                    float total_item_width = 0.0f;
                    int child_count = 0;
                    View* child = container->first_child;
                    while (child) {
                        if (child->view_type == RDT_VIEW_BLOCK) {
                            ViewElement* item = (ViewElement*)child->as_element();
                            // Skip display:none and absolute/hidden items
                            if (item && !should_skip_flex_item(item)) {
                                float item_width = 0.0f;
                                if (item->blk && item->blk->given_width > 0) {
                                    item_width = item->blk->given_width;
                                } else if (item->width > 0) {
                                    item_width = item->width;
                                } else if (item->fi && item->fi->has_intrinsic_width) {
                                    item_width = item->fi->intrinsic_width.max_content;
                                }
                                // Clamp by min-width/max-width if set
                                if (item->blk) {
                                    if (item->blk->given_max_width > 0 && item_width > item->blk->given_max_width) {
                                        item_width = item->blk->given_max_width;
                                    }
                                    // min takes precedence over max per CSS spec
                                    if (item->blk->given_min_width > 0 && item_width < item->blk->given_min_width) {
                                        item_width = item->blk->given_min_width;
                                    }
                                }
                                total_item_width += item_width;
                                child_count++;
                                log_debug("ROW FLEX SHRINK-TO-FIT: item width=%.1f, total=%.1f",
                                          item_width, total_item_width);
                            }
                        }
                        child = child->next();
                    }
                    // Add gaps
                    if (child_count > 1) {
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
                    log_debug("ROW FLEX SHRINK-TO-FIT: main_axis_size=%.1f, container.width=%d",
                              flex_layout->main_axis_size, container->width);
                } else {
                    flex_layout->main_axis_size = (float)content_width;
                    log_debug("AXIS INIT - set main to %.1f", (float)content_width);
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
            log_debug("AXIS INIT - vertical branch");
            if (flex_layout->main_axis_size == 0.0f) {
                // For column flex with auto height, calculate height based on flex items
                // CRITICAL: Only calculate auto-height if container does NOT have explicit height
                bool has_explicit_height = container->blk && container->blk->given_height > 0;
                if (content_height <= 0 && !has_explicit_height) {
                    // Auto-height column flex: calculate from flex items' intrinsic heights
                    int total_item_height = 0;
                    View* child = container->first_child;
                    while (child) {
                        if (child->view_type == RDT_VIEW_BLOCK) {
                            ViewElement* item = (ViewElement*)child->as_element();
                            if (item && item->fi) {
                                // Use flex-basis if specified, otherwise use intrinsic/explicit height
                                int item_height = 0;
                                if (item->fi->flex_basis >= 0 && !item->fi->flex_basis_is_percent) {
                                    item_height = item->fi->flex_basis;
                                } else if (item->blk && item->blk->given_height > 0) {
                                    item_height = (int)item->blk->given_height;
                                } else if (item->height > 0) {
                                    item_height = item->height;
                                } else {
                                    // Empty flex items (no children) get 0 height
                                    // Items with children get minimum height (may have content)
                                    if (!is_empty_flex_container(item)) {
                                        item_height = 20;  // Minimum for items with content
                                    }
                                    // else: empty flex items get 0
                                }
                                total_item_height += item_height;
                                log_debug("COLUMN FLEX: item height contribution = %d", item_height);
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
                        flex_layout->main_axis_size = (float)total_item_height;
                        // Update container height to include padding + border (border-box)
                        float padding_border_height = 0.0f;
                        if (container->bound) {
                            padding_border_height += container->bound->padding.top + container->bound->padding.bottom;
                            if (container->bound->border) {
                                padding_border_height += container->bound->border->width.top + container->bound->border->width.bottom;
                            }
                        }
                        container->height = total_item_height + (int)padding_border_height;
                        log_debug("COLUMN FLEX: auto-height calculated as %d from items (container=%d, border+padding=%.0f)",
                                  total_item_height, container->height, padding_border_height);
                    }
                } else {
                    flex_layout->main_axis_size = (float)content_height;
                }
            }
            if (flex_layout->cross_axis_size == 0.0f) {
                // For column flex with auto width, calculate width based on flex items
                bool has_explicit_width = container->blk && container->blk->given_width > 0;
                if (!has_explicit_width && content_width > 0) {
                    // Calculate max width from flex items (cross-axis for column flex)
                    float max_item_width = 0.0f;
                    View* child = container->first_child;
                    while (child) {
                        if (child->view_type == RDT_VIEW_BLOCK) {
                            ViewElement* item = (ViewElement*)child->as_element();
                            if (item) {
                                float item_width = 0.0f;
                                if (item->blk && item->blk->given_width > 0) {
                                    item_width = item->blk->given_width;
                                } else if (item->width > 0) {
                                    item_width = item->width;
                                } else if (item->fi && item->fi->has_intrinsic_width) {
                                    item_width = item->fi->intrinsic_width.max_content;
                                }
                                if (item_width > max_item_width) {
                                    max_item_width = item_width;
                                }
                                log_debug("COLUMN FLEX: item width = %.1f, max = %.1f", item_width, max_item_width);
                            }
                        }
                        child = child->next();
                    }
                    if (max_item_width > 0.0f) {
                        flex_layout->cross_axis_size = max_item_width;
                        // Also update container width for shrink-to-fit behavior
                        float padding_width = 0.0f;
                        if (container->bound) {
                            padding_width = container->bound->padding.left + container->bound->padding.right;
                        }
                        float border_width = 0.0f;
                        if (container->bound && container->bound->border) {
                            border_width = container->bound->border->width.left + container->bound->border->width.right;
                        }
                        container->width = max_item_width + padding_width + border_width;
                        log_debug("COLUMN FLEX: auto-width calculated as %.1f from items (container=%.1f)",
                                  max_item_width, container->width);
                    } else if (container->position &&
                               (container->position->position == CSS_VALUE_ABSOLUTE ||
                                container->position->position == CSS_VALUE_FIXED)) {
                        // Absolute/fixed with no children: shrink-to-fit → content is 0
                        // Container width should be just border + padding
                        flex_layout->cross_axis_size = 0.0f;
                        float bp_width = 0.0f;
                        if (container->bound) {
                            bp_width += container->bound->padding.left + container->bound->padding.right;
                            if (container->bound->border) {
                                bp_width += container->bound->border->width.left + container->bound->border->width.right;
                            }
                        }
                        container->width = (int)bp_width;
                        log_debug("COLUMN FLEX: empty abs-pos, shrink-to-fit width=%.1f (border+padding only)",
                                  bp_width);
                    } else {
                        flex_layout->cross_axis_size = (float)content_width;
                    }
                } else {
                    flex_layout->cross_axis_size = (float)content_width;
                }
            }
        }

        log_debug("AXIS INIT - after: main=%.1f, cross=%.1f, horizontal=%d",
               flex_layout->main_axis_size, flex_layout->cross_axis_size, is_horizontal);
        log_debug("FLEX AXES - main: %.1f, cross: %.1f, horizontal: %d",
               flex_layout->main_axis_size, flex_layout->cross_axis_size, is_horizontal);

        // ENHANCED: Update container dimensions to match calculated flex sizes
        if (is_horizontal) {
            int new_height = (int)flex_layout->cross_axis_size;
            // CRITICAL: Always update container height to prevent negative heights
            if (container->height <= 0 || new_height > container->height) {
                log_debug("CONTAINER HEIGHT UPDATE - updating from %d to %d (cross_axis_size=%.1f)",
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

    // Debug: Print initial item dimensions
    for (int i = 0; i < item_count; i++) {
        ViewElement* item = (ViewElement*)items[i]->as_element();
        log_debug("Item %d initial: %dx%d at (%d,%d)", i, item->width, item->height, item->x, item->y);
        if (item->blk) {
            log_debug("Item %d box-sizing: %d, given: %dx%d", i, item->blk->box_sizing,
                   item->blk->given_width, item->blk->given_height);
        }
        if (item->bound) {
            log_debug("Item %d padding: l=%d r=%d t=%d b=%d", i,
                   item->bound->padding.left, item->bound->padding.right,
                   item->bound->padding.top, item->bound->padding.bottom);
        }
    }

    if (item_count == 0) {
        log_debug("No flex items found");
        return;
    }

    // Phase 2: Sort items by order property
    sort_flex_items_by_order(items, item_count);

    // Phase 2.5: Resolve constraints for all flex items
    // This must happen before flex basis calculation in create_flex_lines
    log_debug("Phase 2.5: Resolving constraints for flex items");
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
                    if (item->blk && item->blk->given_width > 0) {
                        item_width = item->blk->given_width;
                    } else if (item->fi && item->fi->has_intrinsic_width) {
                        item_width = item->fi->intrinsic_width.max_content;
                    } else if (item->width > 0) {
                        item_width = item->width;
                    }
                    // 2) If item has non-zero flex-shrink and its max-content exceeds
                    //    its specified size (flex-basis), use the specified size instead.
                    //    CSS §9.9.1: "unless that value is greater than its outer specified
                    //    size and the item has a non-zero flex-shrink factor"
                    if (item->fi && item->fi->flex_shrink > 0 &&
                        item->fi->flex_basis >= 0 && !item->fi->flex_basis_is_percent &&
                        item_width > item->fi->flex_basis) {
                        item_width = item->fi->flex_basis;
                    }
                    // 3) Clamp by min-width/max-width if set
                    if (item->blk) {
                        if (item->blk->given_max_width > 0 && item_width > item->blk->given_max_width) {
                            item_width = item->blk->given_max_width;
                        }
                        if (item->blk->given_min_width > 0 && item_width < item->blk->given_min_width) {
                            item_width = item->blk->given_min_width;
                        }
                    }
                    flex_item_count++;
                    log_debug("SHRINK-TO-FIT RECALC: element item width=%.1f (has_intrinsic=%d)",
                              item_width, item->fi ? item->fi->has_intrinsic_width : -1);
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
            if (flex_item_count > 1) {
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
                container->width = (int)(total_item_width + padding_border_width);
                log_debug("SHRINK-TO-FIT RECALC: main_axis_size=%.1f, container.width=%d, items=%d",
                          flex_layout->main_axis_size, container->width, flex_item_count);
            }
        }
    }

    // Phase 3: Create flex lines (handle wrapping)
    int line_count = create_flex_lines(flex_layout, items, item_count);

    // Phase 4: Resolve flexible lengths for each line
    log_info("Phase 4: About to resolve flexible lengths for %d lines", line_count);
    for (int i = 0; i < line_count; i++) {
        log_info("Phase 4: Resolving line %d", i);
        resolve_flexible_lengths(flex_layout, &flex_layout->lines[i]);
        log_info("Phase 4: Completed line %d", i);
    }
    log_info("Phase 4: All flex lengths resolved");

    // Phase 4.5: Determine hypothetical cross sizes for each item
    // CSS Flexbox §9.4: After main sizes are resolved, determine cross sizes
    log_debug("Phase 4.5: About to determine hypothetical cross sizes");
    determine_hypothetical_cross_sizes(lycon, flex_layout);
    log_debug("Phase 4.5: Completed determining hypothetical cross sizes");

    // REMOVED: Don't override content dimensions after flex calculations
    // The flex algorithm should work with the proper content dimensions
    // that were calculated during box-sizing in the block layout phase

    // Phase 5: Calculate cross sizes for lines
    log_debug("Phase 5: About to calculate line cross sizes");
    calculate_line_cross_sizes(flex_layout);
    log_debug("Phase 5: Completed calculating line cross sizes");

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
                log_debug("Phase 5b: Applying min-width to main axis: %.1f -> %.1f (min-width=%.1f, padding=%.1f)",
                          flex_layout->main_axis_size, min_content_width, container->blk->given_min_width, padding_main);
                flex_layout->main_axis_size = min_content_width;
                container->width = (int)container->blk->given_min_width;  // Keep border-box width
            }
            // Row flex: min-height affects cross_axis_size
            float min_content_height = container->blk->given_min_height - padding_cross;
            if (container->blk->given_min_height > 0 && container->height < container->blk->given_min_height) {
                log_debug("Phase 5b: Applying min-height to cross axis: %.1f -> %.1f",
                          container->height, container->blk->given_min_height);
                container->height = container->blk->given_min_height;
                flex_layout->cross_axis_size = min_content_height > 0 ? min_content_height : container->blk->given_min_height;
            }
        } else {
            // Column flex: min-height affects main_axis_size (and justify-content)
            float min_content_height = container->blk->given_min_height - padding_main;
            if (container->blk->given_min_height > 0 && container->height < container->blk->given_min_height) {
                log_debug("Phase 5b: Applying min-height to main axis: %.1f -> %.1f",
                          container->height, container->blk->given_min_height);
                container->height = container->blk->given_min_height;
                flex_layout->main_axis_size = min_content_height > 0 ? min_content_height : container->blk->given_min_height;
            }
            // Column flex: min-width affects cross_axis_size
            float min_content_width = container->blk->given_min_width - padding_cross;
            if (container->blk->given_min_width > 0 && flex_layout->cross_axis_size < min_content_width) {
                log_debug("Phase 5b: Applying min-width to cross axis: %.1f -> %.1f",
                          flex_layout->cross_axis_size, container->blk->given_min_width);
                flex_layout->cross_axis_size = min_content_width > 0 ? min_content_width : container->blk->given_min_width;
                container->width = (int)container->blk->given_min_width;
            }
        }
    }

    // Phase 6: Align items on main axis
    log_debug("Phase 6: About to align items on main axis for %d lines", line_count);
    for (int i = 0; i < line_count; i++) {
        log_debug("Phase 6: Aligning line %d on main axis", i);
        align_items_main_axis(flex_layout, &flex_layout->lines[i]);
        log_debug("Phase 6: Completed aligning line %d on main axis", i);
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
        log_debug("Phase 7: container=%s total_line_cross=%d, current height=%d",
                  container->node_name(), total_line_cross, container->height);
        // Check if container has explicit height (given_height > 0 means explicit)
        // OR if this container is a flex item whose height was set by parent flex
        // OR if this container is a grid item whose height was set by parent grid
        bool has_explicit_height = container->blk && container->blk->given_height > 0;
        // Check grid item status - must verify item_prop_type to access correct union member
        // (gi, fi, tb, td, form are all in a union, accessing wrong one gives garbage)
        bool is_grid_item = (container->item_prop_type == DomElement::ITEM_PROP_GRID) &&
                            container->gi && container->gi->computed_grid_row_start > 0;
        if (!has_explicit_height && is_grid_item && container->height > 0) {
            has_explicit_height = true;
            log_debug("Phase 7: (Row) Container is a grid item with height=%.1f set by parent grid",
                      container->height);
        }
        // Only check flex item status if this container is actually a flex item
        bool is_flex_item = container->fi != nullptr ||
                            (container->item_prop_type == DomElement::ITEM_PROP_FORM && container->form);
        if (!has_explicit_height && is_flex_item && container->height > 0 && container->fi) {
            // Check if parent set the height via flex sizing
            float fg = get_item_flex_grow(container);
            float fs = get_item_flex_shrink(container);
            if (fg > 0 || fs > 0) {
                has_explicit_height = true;
                log_debug("Phase 7: Container is a flex item with height set by parent flex");
            }
        }

        // Update container cross_axis_size to actual content height
        if (total_line_cross > 0) {
            if (!has_explicit_height) {
                // Only update for auto-height containers
                log_debug("Phase 7: Updating cross_axis_size from %.1f to %d (auto-height)",
                         flex_layout->cross_axis_size, total_line_cross);
                flex_layout->cross_axis_size = (float)total_line_cross;
                // Container height should be content + padding + border (not just content)
                int padding_height = 0;
                int border_height = 0;
                if (container->bound) {
                    padding_height = (int)(container->bound->padding.top + container->bound->padding.bottom);
                    if (container->bound->border) {
                        border_height = (int)(container->bound->border->width.top + container->bound->border->width.bottom);
                    }
                }
                container->height = total_line_cross + padding_height + border_height;
                log_debug("Phase 7: UPDATED container=%p (%s) height to %.1f (total_line_cross=%d + padding=%d + border=%d)",
                         container, container->node_name(), container->height, total_line_cross, padding_height, border_height);
            } else {
                log_debug("Phase 7: Container has explicit height, not updating");
            }
        }
    } else {
        // Column flex: finalize main_axis_size (height) for auto-height containers
        int total_line_main = 0;
        for (int i = 0; i < line_count; i++) {
            // For column flex, sum up item heights (main sizes)
            FlexLineInfo* line = &flex_layout->lines[i];
            for (int j = 0; j < line->item_count; j++) {
                ViewElement* item = (ViewElement*)line->items[j]->as_element();
                if (item) {
                    total_line_main += item->height;
                }
            }
            // Add gaps between items
            if (line->item_count > 1) {
                total_line_main += flex_layout->row_gap * (line->item_count - 1);
            }
        }
        // Check if container has explicit height (given_height > 0 means explicit)
        // OR if this container is a flex item whose height was set by parent flex
        // OR if this container is a grid item whose height was set by parent grid
        bool has_explicit_height = container->blk && container->blk->given_height > 0;
        // Check grid item status - must verify item_prop_type to access correct union member
        // (gi, fi, tb, td, form are all in a union, accessing wrong one gives garbage)
        bool is_grid_item_col = (container->item_prop_type == DomElement::ITEM_PROP_GRID) &&
                                container->gi && container->gi->computed_grid_row_start > 0;
        if (!has_explicit_height && is_grid_item_col && container->height > 0) {
            has_explicit_height = true;
            log_debug("Phase 7: (Column) Container is a grid item with height=%.1f set by parent grid",
                      container->height);
        }
        // Only check flex item status if this container is actually a flex item
        bool is_flex_item_col = container->fi != nullptr ||
                                (container->item_prop_type == DomElement::ITEM_PROP_FORM && container->form);
        if (!has_explicit_height && is_flex_item_col && container->height > 0) {
            // Check if parent set the height via flex sizing
            float fg = get_item_flex_grow(container);
            float fs = get_item_flex_shrink(container);
            if (fg > 0 || fs > 0) {
                has_explicit_height = true;
                log_debug("Phase 7: (Column) Container is a flex item with height set by parent flex");
            }
        }

        if (total_line_main > 0) {
            if (!has_explicit_height) {
                log_debug("Phase 7: (Column) Updating main_axis_size from %.1f to %d (auto-height)",
                         flex_layout->main_axis_size, total_line_main);
                flex_layout->main_axis_size = (float)total_line_main;
                // Container height should be content + padding + border (not just content)
                int padding_height = 0;
                int border_height = 0;
                if (container->bound) {
                    padding_height = (int)(container->bound->padding.top + container->bound->padding.bottom);
                    if (container->bound->border) {
                        border_height = (int)(container->bound->border->width.top + container->bound->border->width.bottom);
                    }
                }
                container->height = total_line_main + padding_height + border_height;
            } else {
                log_debug("Phase 7: (Column) Container has explicit height, not updating");
            }
        }
    }

    // Phase 7b: Apply min-height constraint to container
    // This must happen AFTER content-based height calculation but BEFORE align_content
    // so that justify-content/align-content have the correct container size to work with
    if (container->blk && container->blk->given_min_height > 0) {
        float min_height = container->blk->given_min_height;
        if (container->height < min_height) {
            log_debug("Phase 7b: Applying min-height constraint: %.1f -> %.1f", container->height, min_height);
            container->height = min_height;
            // Update flex_layout dimensions for alignment calculations
            if (is_main_axis_horizontal(flex_layout)) {
                flex_layout->cross_axis_size = min_height;
            } else {
                flex_layout->main_axis_size = min_height;
            }
        }
    }

    // Phase 8: Align content (distribute space among lines)
    // Note: align-content applies to flex containers with flex-wrap: wrap or wrap-reverse
    // CRITICAL: This must happen BEFORE align_items_cross_axis so line cross-sizes are final
    if (flex_layout->wrap != WRAP_NOWRAP) {
        log_debug("Phase 8: About to align content for %d lines", line_count);
        align_content(flex_layout);
        log_debug("Phase 8: Completed align content");
    }

    // Phase 9: Align items on cross axis
    // This runs AFTER align_content so line cross-sizes are finalized
    log_debug("Phase 9: About to align items on cross axis for %d lines", line_count);
    for (int i = 0; i < line_count; i++) {
        log_debug("Phase 9: Aligning line %d on cross axis", i);
        align_items_cross_axis(flex_layout, &flex_layout->lines[i]);
        log_debug("Phase 9: Completed aligning line %d on cross axis", i);
    }

    // Phase 9.5: Store first line's baseline in container's FlexProp
    // This is used when this flex container participates in parent's baseline alignment
    if (line_count > 0 && container->embed && container->embed->flex) {
        FlexLineInfo* first_line = &flex_layout->lines[0];
        // Check if first line has baseline-aligned items
        bool has_baseline_child = false;
        for (int i = 0; i < first_line->item_count; i++) {
            ViewElement* item = (ViewElement*)first_line->items[i]->as_element();
            if (item && item->fi && item->fi->align_self == ALIGN_BASELINE) {
                has_baseline_child = true;
                break;
            }
        }
        // Also check container's align-items: baseline
        if (!has_baseline_child && flex_layout->align_items == ALIGN_BASELINE) {
            has_baseline_child = true;
        }
        container->embed->flex->first_baseline = first_line->baseline;
        container->embed->flex->has_baseline_child = has_baseline_child;
        log_debug("Phase 9.5: Stored first_baseline=%d, has_baseline_child=%d",
                  first_line->baseline, has_baseline_child);
    }

    // Note: wrap-reverse item positioning is now handled in align_items_cross_axis
    // by positioning items at the end of the line when they have explicit cross-axis sizes

    // Phase 10: Apply relative positioning offsets to flex items
    // CSS spec: position: relative with top/right/bottom/left should offset the item
    // from its normal flow position (which has been calculated by the flex algorithm)
    // CRITICAL: Percentage offsets must be re-resolved against the actual parent dimensions,
    // because during CSS resolution they may have been resolved against a different containing block.
    log_debug("Phase 10: Applying relative positioning to flex items");
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
                log_debug("Phase 10: Applying relative offset (%.0f, %.0f) to item %d at (%.0f, %.0f)",
                          offset_x, offset_y, i, item->x, item->y);
                item->x += offset_x;
                item->y += offset_y;
            }
        }
    }

    log_debug("FINAL FLEX POSITIONS:");
    for (int i = 0; i < item_count; i++) {
        View* item = items[i];
        ViewElement* item_elmt = (ViewElement*)item->as_element();
        int order_val = item_elmt && item_elmt->fi ? item_elmt->fi->order : -999;
        log_debug("FINAL_ITEM %d (order=%d, ptr=%p) - pos: (%.0f,%.0f), size: %.0fx%.0f", i, order_val, item, item->x, item->y, item->width, item->height);
    }

    flex_layout->needs_reflow = false;
}// Collect flex items from container children
int collect_flex_items(FlexContainerLayout* flex, ViewBlock* container, View*** items) {
    if (!flex || !container || !items) return 0;

    log_debug("*** COLLECT_FLEX_ITEMS TRACE: ENTRY - container=%p, container->first_child=%p",
              container, container->first_child);
    int count = 0;

    // Count children first - use ViewBlock hierarchy for flex items
    log_debug("*** COLLECT_FLEX_ITEMS TRACE: Starting to count children of container %p", container);
    View* child = (View*)container->first_child;
    while (child) {
        log_debug("*** COLLECT_FLEX_ITEMS TRACE: Found child view %p (type=%d, node=%s)",
            child, child->view_type, child->node_name());

        // CRITICAL FIX: Skip text nodes - flex items must be elements
        if (!child->is_element()) {
            log_debug("*** COLLECT_FLEX_ITEMS TRACE: Skipped text node %p", child);
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
            log_debug("*** COLLECT_FLEX_ITEMS TRACE: Counted child %p as flex item #%d", child, count);
        } else {
            log_debug("*** COLLECT_FLEX_ITEMS TRACE: Skipped child %p (absolute=%d, hidden=%d)", child, is_absolute, is_hidden);
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
    log_debug("*** COLLECT_FLEX_ITEMS TRACE: Starting to collect %d flex items", count);
    count = 0;
    child = container->first_child;
    while (child) {
        log_debug("*** COLLECT_FLEX_ITEMS TRACE: Processing child view %p for collection", child);

        // CRITICAL FIX: Skip text nodes - flex items must be elements
        if (!child->is_element()) {
            log_debug("*** COLLECT_FLEX_ITEMS TRACE: Skipped text node %p in collection", child);
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
            log_debug("*** COLLECT_FLEX_ITEMS TRACE: Added child %p as flex item [%d]", child, count);

            // CRITICAL FIX: Apply cached measurements to flex items
            // This connects the measurement pass with the layout pass
            MeasurementCacheEntry* cached = get_from_measurement_cache(child);
            if (cached) {
                log_debug("Applying cached measurements to flex item %d: %dx%d (content: %dx%d)",
                    count, cached->measured_width, cached->measured_height, cached->content_width, cached->content_height);
                child->width = cached->measured_width;
                child->height = cached->measured_height;
                if (child_elmt) {
                    child_elmt->content_width = cached->content_width;
                    child_elmt->content_height = cached->content_height;
                    log_debug("Applied measurements: item %d now has size %dx%d (content: %dx%d)",
                        count, child->width, child->height, child_elmt->content_width, child_elmt->content_height);
                }
            } else {
                log_debug("No cached measurement found for flex item %d", count);
            }

            // DEBUG: Check CSS dimensions
            if (child_elmt && child_elmt->blk) {
                log_debug("Flex item %d CSS dimensions: given_width=%.1f, given_height=%.1f",
                    count, child_elmt->blk->given_width, child_elmt->blk->given_height);

                // CRITICAL FIX: Apply CSS dimensions to flex items if specified
                // Must clamp against max-width/max-height constraints
                if (child_elmt->blk->given_width > 0 && child->width != child_elmt->blk->given_width) {
                    float target_width = child_elmt->blk->given_width;

                    // Clamp against max-width constraint if present
                    if (child_elmt->blk->given_max_width > 0 && target_width > child_elmt->blk->given_max_width) {
                        log_debug("Flex item %d width %.1f exceeds max-width %.1f, clamping",
                                 count, target_width, child_elmt->blk->given_max_width);
                        target_width = child_elmt->blk->given_max_width;
                    }

                    // Clamp against min-width constraint if present
                    if (child_elmt->blk->given_min_width > 0 && target_width < child_elmt->blk->given_min_width) {
                        log_debug("Flex item %d width %.1f below min-width %.1f, clamping",
                                 count, target_width, child_elmt->blk->given_min_width);
                        target_width = child_elmt->blk->given_min_width;
                    }

                    log_debug("Setting flex item %d width from CSS: %.1f -> %.1f",
                             count, child->width, target_width);
                    child->width = target_width;
                }

                if (child_elmt->blk->given_height > 0 && child->height != child_elmt->blk->given_height) {
                    float target_height = child_elmt->blk->given_height;

                    // Clamp against max-height constraint if present
                    if (child_elmt->blk->given_max_height > 0 && target_height > child_elmt->blk->given_max_height) {
                        log_debug("Flex item %d height %.1f exceeds max-height %.1f, clamping",
                                 count, target_height, child_elmt->blk->given_max_height);
                        target_height = child_elmt->blk->given_max_height;
                    }

                    // Clamp against min-height constraint if present
                    if (child_elmt->blk->given_min_height > 0 && target_height < child_elmt->blk->given_min_height) {
                        log_debug("Flex item %d height %.1f below min-height %.1f, clamping",
                                 count, target_height, child_elmt->blk->given_min_height);
                        target_height = child_elmt->blk->given_min_height;
                    }

                    log_debug("Setting flex item %d height from CSS: %.1f -> %.1f",
                             count, child->height, target_height);
                    child->height = target_height;
                }
            } else {
                log_debug("Flex item %d has no blk (CSS properties)", count);
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

    // Log initial order
    log_debug("sort_flex_items_by_order: Sorting %d items", count);
    for (int i = 0; i < count; i++) {
        ViewElement* item = (ViewElement*)items[i]->as_element();
        int order_val = item && item->fi ? item->fi->order : 0;
        log_debug("  Before sort: items[%d] order=%d", i, order_val);
    }

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

    // Log final order
    for (int i = 0; i < count; i++) {
        ViewElement* item = (ViewElement*)items[i]->as_element();
        int order_val = item && item->fi ? item->fi->order : 0;
        log_debug("  After sort: items[%d] order=%d", i, order_val);
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

    // Single pass through all children
    while (child) {
        log_debug("Processing child: %p (%s), is_element=%d",
                  child, child->node_name(), child->is_element());

        // Skip non-element nodes (text nodes)
        if (!child->is_element()) {
            log_debug("Skipping text node: %s", child->node_name());
            child = child->next_sibling;
            continue;
        }

        // CRITICAL: Restore container's font context before processing each flex item
        // This ensures each flex item inherits from the container, not from siblings
        lycon->font = container_font;

        // Step 1: Create/verify View structure FIRST (resolves CSS styles)
        // This must happen before measurement so font-size etc. are available
        log_debug("Step 1: Creating View for %s", child->node_name());
        init_flex_item_view(lycon, child);

        // Step 2: Measure content (uses resolved styles)
        log_debug("Step 2: Measuring content for %s", child->node_name());
        measure_flex_child_content(lycon, child);

        // Now child IS the View (unified tree) - get as ViewGroup
        ViewElement* item = (ViewElement*)child->as_element();

        // Step 3: Check if should skip (absolute, hidden)
        if (should_skip_flex_item(item)) {
            log_debug("Skipping flex item (absolute/hidden): %s", child->node_name());
            child = child->next_sibling;
            continue;
        }

        // Step 4: Apply cached measurements
        MeasurementCacheEntry* cached = get_from_measurement_cache(child);
        if (cached) {
            log_debug("Applying cached measurements to %s: %dx%d (content: %dx%d)",
                      child->node_name(), cached->measured_width, cached->measured_height,
                      cached->content_width, cached->content_height);

            // Apply measurements (don't override explicit CSS dimensions)
            if (item->width <= 0) {
                item->width = cached->measured_width;
            }
            if (item->height <= 0) {
                item->height = cached->measured_height;
            }
            item->content_width = cached->content_width;
            item->content_height = cached->content_height;
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
                    log_info("FLEX: Intrinsic sizing mode - percentage width %.1f%% treated as auto",
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
                        log_info("FLEX: Re-resolving width percentage: %.1f%% of %.1f = %.1f (was %.1f)",
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
                    log_info("FLEX: Re-resolving height percentage: %.1f%% of %.1f = %.1f (was %.1f)",
                             height_percent, resolve_against, new_height, item->blk->given_height);
                    item->blk->given_height = new_height;
                    item->height = new_height;
                }
            }
        }

        // Step 6: Apply explicit CSS dimensions if specified (non-percentage)
        if (item->blk) {
            // Only apply if not a percentage (already handled above)
            if (isnan(item->blk->given_width_percent) && item->blk->given_width > 0) {
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

                log_debug("Applying CSS width (clamped): %.1f", target_width);
                item->width = target_width;
            }
            if (isnan(item->blk->given_height_percent) && item->blk->given_height > 0) {
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

                log_debug("Applying CSS height (clamped): %.1f", target_height);
                item->height = target_height;
            }
        }

        // Step 6a: Apply aspect-ratio if item has height but no width
        // This handles cases like height: 100%; aspect-ratio: 1
        if (item->fi && item->fi->aspect_ratio > 0) {
            if (item->height > 0 && item->width <= 0) {
                item->width = item->height * item->fi->aspect_ratio;
                log_debug("Applied aspect-ratio: width=%.1f from height=%.1f * ratio=%.3f",
                          item->width, item->height, item->fi->aspect_ratio);
            } else if (item->width > 0 && item->height <= 0) {
                item->height = item->width / item->fi->aspect_ratio;
                log_debug("Applied aspect-ratio: height=%.1f from width=%.1f / ratio=%.3f",
                          item->height, item->width, item->fi->aspect_ratio);
            }
        }

        // Step 6b: For nested flex containers without explicit cross-axis size,
        // set their size to the available cross-axis size ONLY when align-items: stretch.
        // This is critical for flex-wrap containers to wrap correctly.
        // NOTE: With align-items: center/start/end, items should use intrinsic size.
        bool is_row = is_main_axis_horizontal(flex_layout);
        if (item->display.inner == CSS_VALUE_FLEX) {
            // This is a nested flex container
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
                    log_debug("NESTED_FLEX_ITEM: Setting width=%.1f from parent cross-axis (column, stretch)",
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

    log_info("=== UNIFIED COLLECTION COMPLETE: %d flex items ===", item_count);
    log_leave();

    return item_count;
}

// Calculate flex basis for an item
float calculate_flex_basis(ViewElement* item, FlexContainerLayout* flex_layout) {
    log_debug("calculate_flex_basis for item %p", item);

    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    // Handle form controls FIRST (they don't have fi)
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM && item->form) {
        // Form controls with explicit flex-basis should use that value
        float form_flex_basis = item->form->flex_basis;
        if (form_flex_basis >= 0) {
            // Explicit flex-basis (including 0 from "flex: 1")
            if (item->form->flex_basis_is_percent) {
                float container_size = is_horizontal ?
                    flex_layout->main_axis_size : flex_layout->cross_axis_size;
                float basis = form_flex_basis * container_size / 100.0f;
                log_debug("calculate_flex_basis - form control explicit percent: %.1f%% = %.1f", form_flex_basis, basis);
                return basis;
            }
            log_debug("calculate_flex_basis - form control explicit basis: %.1f", form_flex_basis);
            return form_flex_basis;
        }

        // flex-basis: auto - use intrinsic size
        float basis = is_horizontal ? item->form->intrinsic_width : item->form->intrinsic_height;

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
            // Percentage relative to container (including 0%)
            float container_size = is_main_axis_horizontal(flex_layout) ?
                flex_layout->main_axis_size : flex_layout->cross_axis_size;
            float basis = item->fi->flex_basis * container_size / 100.0f;
            log_info("FLEX_BASIS - explicit percent: %.1f%% of %.1f = %.1f",
                     item->fi->flex_basis, container_size, basis);
            return basis;
        }
        log_info("FLEX_BASIS - explicit: %d", item->fi->flex_basis);
        return (float)item->fi->flex_basis;
    }

    // Case 2: flex-basis: auto - use main axis size if explicit

    // Check for explicit width/height in CSS
    if (is_horizontal && item->blk && item->blk->given_width > 0) {
        log_debug("calculate_flex_basis - using explicit width: %f", item->blk->given_width);
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
                    item->embed->img->max_render_width = (int)item->blk->given_width;
                    if (item->blk->given_height > 0) {
                        item->embed->img->max_render_width = max(item->embed->img->max_render_width,
                                                                  (int)item->blk->given_height);
                    }
                }
                log_debug("calculate_flex_basis: loaded image for IMG with explicit width: %s", src_value);
            }
        }

        // For content-box, given_width is content width - need to add padding/border for flex basis
        float basis = item->blk->given_width;
        if (item->blk->box_sizing != CSS_VALUE_BORDER_BOX && item->bound) {
            basis += item->bound->padding.left + item->bound->padding.right;
            if (item->bound->border) {
                basis += item->bound->border->width.left + item->bound->border->width.right;
            }
            log_debug("calculate_flex_basis - content-box: added padding/border to get border-box: %f", basis);
        }
        return basis;
    }
    if (!is_horizontal && item->blk && item->blk->given_height > 0) {
        log_debug("calculate_flex_basis - using explicit height: %f", item->blk->given_height);
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
                    item->embed->img->max_render_width = (int)item->blk->given_height;
                    if (item->blk->given_width > 0) {
                        item->embed->img->max_render_width = max(item->embed->img->max_render_width,
                                                                  (int)item->blk->given_width);
                    }
                }
                log_debug("calculate_flex_basis: loaded image for IMG with explicit height: %s", src_value);
            }
        }

        // For content-box, given_height is content height - need to add padding/border for flex basis
        float basis = item->blk->given_height;
        if (item->blk->box_sizing != CSS_VALUE_BORDER_BOX && item->bound) {
            basis += item->bound->padding.top + item->bound->padding.bottom;
            if (item->bound->border) {
                basis += item->bound->border->width.top + item->bound->border->width.bottom;
            }
            log_debug("calculate_flex_basis - content-box: added padding/border to get border-box: %f", basis);
        }
        return basis;
    }

    // Case 2b: aspect-ratio with explicit cross-axis size
    // If item has aspect-ratio and explicit height (for horizontal) or width (for vertical),
    // compute main-axis size from cross-axis and aspect-ratio
    if (item->fi && item->fi->aspect_ratio > 0) {
        if (is_horizontal && item->blk && item->blk->given_height > 0) {
            float basis = item->blk->given_height * item->fi->aspect_ratio;
            log_debug("calculate_flex_basis - aspect-ratio: height=%.1f * ratio=%.3f = %.1f",
                      item->blk->given_height, item->fi->aspect_ratio, basis);
            return basis;
        }
        if (!is_horizontal && item->blk && item->blk->given_width > 0) {
            float basis = item->blk->given_width / item->fi->aspect_ratio;
            log_debug("calculate_flex_basis - aspect-ratio: width=%.1f / ratio=%.3f = %.1f",
                      item->blk->given_width, item->fi->aspect_ratio, basis);
            return basis;
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
        log_debug("calculate_flex_basis: horizontal, fi=%p, has_intrinsic_width=%d, max_content=%.1f",
                  item->fi, item->fi ? item->fi->has_intrinsic_width : -1, basis);
    } else {
        basis = item->fi ? item->fi->intrinsic_height.max_content : 0;
        log_debug("calculate_flex_basis: vertical, fi=%p, has_intrinsic_height=%d, max_content=%.1f",
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

    log_debug("calculate_flex_basis - using intrinsic size: %.1f (including padding/border)", basis);
    return basis;
}

// Calculate the hypothetical main size for an item (flex-basis clamped by min/max constraints)
// This is used for line breaking decisions per CSS Flexbox spec 9.3
// IMPORTANT: For wrapping purposes, only use EXPLICITLY SET min/max constraints,
// not the automatic minimum (min-content) that is used for flex shrinking
float calculate_hypothetical_main_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    float basis = calculate_flex_basis(item, flex_layout);

    if (!item->fi) return basis;

    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    // Get EXPLICIT min/max constraints from CSS (not automatic minimum)
    // Only use explicitly set min-width/min-height values, not resolved auto values
    float min_main = 0, max_main = FLT_MAX;
    if (is_horizontal) {
        // Only use explicitly set min-width (given_min_width > 0)
        if (item->blk && item->blk->given_min_width > 0) {
            min_main = item->blk->given_min_width;
        }
        if (item->blk && item->blk->given_max_width > 0) {
            max_main = item->blk->given_max_width;
        }
    } else {
        // Only use explicitly set min-height (given_min_height > 0)
        if (item->blk && item->blk->given_min_height > 0) {
            min_main = item->blk->given_min_height;
        }
        if (item->blk && item->blk->given_max_height > 0) {
            max_main = item->blk->given_max_height;
        }
    }

    // Clamp basis by explicit min/max constraints
    float hypothetical = basis;
    if (min_main > 0 && hypothetical < min_main) {
        hypothetical = min_main;
        log_debug("calculate_hypothetical_main_size: clamped to min=%.1f (basis=%.1f)", min_main, basis);
    }
    if (max_main < FLT_MAX && hypothetical > max_main) {
        hypothetical = max_main;
        log_debug("calculate_hypothetical_main_size: clamped to max=%.1f (basis=%.1f)", max_main, basis);
    }

    log_debug("calculate_hypothetical_main_size: item=%p, basis=%.1f, min=%.1f, max=%.1f, result=%.1f",
              item, basis, min_main, max_main, hypothetical);

    return hypothetical;
}

// ============================================================================
// Constraint Resolution for Flex Items
// ============================================================================

// Resolve min/max constraints for a flex item
void resolve_flex_item_constraints(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item) {
        log_debug("resolve_flex_item_constraints: invalid item");
        return;
    }

    // Form controls don't have fi - they use intrinsic sizes directly
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM) {
        log_debug("resolve_flex_item_constraints: form control, using intrinsic sizes");
        return;
    }

    if (!item->fi) {
        log_debug("resolve_flex_item_constraints: no flex properties");
        return;
    }

    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    // Get specified constraints from BlockProp (CSS values)
    int min_width = item->blk ? (int)item->blk->given_min_width : -1;
    int max_width = item->blk && item->blk->given_max_width > 0 ?
        (int)item->blk->given_max_width : INT_MAX;
    int min_height = item->blk ? (int)item->blk->given_min_height : -1;
    int max_height = item->blk && item->blk->given_max_height > 0 ?
        (int)item->blk->given_max_height : INT_MAX;

    log_debug("resolve_flex_item_constraints: item %p, given_min_width=%.2f, min_width=%d, has_explicit_width=%d",
              item, item->blk ? item->blk->given_min_width : -1.0f, min_width, item->fi->has_explicit_width);

    // Resolve 'auto' min-width/height for flex items
    // Per CSS Flexbox spec section 4.5:
    // - For MAIN AXIS: min-size: auto = min-content (automatic minimum size)
    //   EXCEPT when flex-basis is 0 (not 0%), the automatic minimum is 0
    // - For CROSS AXIS: min-size: auto = 0 (no automatic minimum for cross-axis)
    //
    // For cross-axis in row layout, we still use intrinsic height to prevent shrinking
    // below content (this is different from the automatic minimum spec - it's about
    // respecting existing content size during stretch operations).
    bool has_css_width = item->blk && item->blk->given_width > 0;
    bool has_css_height = item->blk && item->blk->given_height > 0;

    // Check if flex-basis is exactly 0 (not 0% and not auto)
    bool flex_basis_is_zero = item->fi->flex_basis == 0 && !item->fi->flex_basis_is_percent;

    // CSS Flexbox §4.5: If the item's overflow is not 'visible', the automatic minimum is 0
    bool overflow_not_visible = item->scroller &&
        (item->scroller->overflow_x != CSS_VALUE_VISIBLE || item->scroller->overflow_y != CSS_VALUE_VISIBLE);

    if (min_width <= 0 && !has_css_width) {
        if (is_horizontal) {
            // Row layout: width is main axis
            if (flex_basis_is_zero || (overflow_not_visible && item->scroller->overflow_x != CSS_VALUE_VISIBLE)) {
                // flex-basis: 0 or overflow != visible → automatic minimum is 0
                min_width = 0;
                log_debug("resolve_flex_item_constraints: auto min-width=0 (basis_zero=%d, overflow=%d)",
                          flex_basis_is_zero, overflow_not_visible);
            } else {
                // Use min-content for automatic minimum
                if (!item->fi->has_intrinsic_width) {
                    calculate_item_intrinsic_sizes(item, flex_layout);
                }
                min_width = item->fi->intrinsic_width.min_content;
                log_debug("resolve_flex_item_constraints: main axis auto min-width = min-content: %d", min_width);

                // CSS Flexbox §4.5: If the item has a definite main-axis max size,
                // the automatic minimum size is clamped to max-width/max-height
                // This ensures min-content doesn't exceed max-width constraint
                if (max_width > 0 && max_width < INT_MAX && min_width > max_width) {
                    log_debug("resolve_flex_item_constraints: clamping auto min-width %d to max-width %d", min_width, max_width);
                    min_width = max_width;
                }
            }
        } else {
            // Column layout: width is cross axis - automatic minimum is 0
            min_width = 0;
            log_debug("resolve_flex_item_constraints: column layout, cross-axis min-width set to 0");
        }
    }

    if (min_height <= 0 && !has_css_height) {
        if (!is_horizontal) {
            // Column layout: height is main axis
            if (flex_basis_is_zero || (overflow_not_visible && item->scroller->overflow_y != CSS_VALUE_VISIBLE)) {
                // flex-basis: 0 or overflow != visible → automatic minimum is 0
                min_height = 0;
                log_debug("resolve_flex_item_constraints: auto min-height=0 (basis_zero=%d, overflow=%d)",
                          flex_basis_is_zero, overflow_not_visible);
            } else {
                // Use min-content for automatic minimum
                if (!item->fi->has_intrinsic_height) {
                    calculate_item_intrinsic_sizes(item, flex_layout);
                }
                min_height = item->fi->intrinsic_height.min_content;
                log_debug("resolve_flex_item_constraints: main axis auto min-height = min-content: %d", min_height);

                // CSS Flexbox §4.5: If the item has a definite main-axis max size,
                // the automatic minimum size is clamped to max-height
                if (max_height > 0 && max_height < INT_MAX && min_height > max_height) {
                    log_debug("resolve_flex_item_constraints: clamping auto min-height %d to max-height %d", min_height, max_height);
                    min_height = max_height;
                }
            }
        } else {
            // Row layout: height is cross axis
            // Use intrinsic height to prevent shrinking below content during stretch
            // IMPORTANT: Add padding and border to get border-box min-height
            if (!item->fi->has_intrinsic_height) {
                calculate_item_intrinsic_sizes(item, flex_layout);
            }
            min_height = item->fi->intrinsic_height.min_content;
            // Add padding and border to intrinsic content height
            if (item->bound) {
                min_height += (int)(item->bound->padding.top + item->bound->padding.bottom);
                if (item->bound->border) {
                    min_height += (int)(item->bound->border->width.top + item->bound->border->width.bottom);
                }
            }
            log_debug("resolve_flex_item_constraints: row layout, cross-axis min-height = intrinsic %d (with padding/border)", min_height);
        }
    }

    // Store resolved constraints in FlexItemProp for use during flex algorithm
    item->fi->resolved_min_width = min_width;
    item->fi->resolved_max_width = max_width;
    item->fi->resolved_min_height = min_height;
    item->fi->resolved_max_height = max_height;

    log_debug("Resolved constraints for item %p: width=[%d, %d], height=[%d, %d]",
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
    if (max_val > 0) {
        return fmin(fmax(value, min_val), max_val);
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

    // Form controls don't have FlexItemProp - the union shares memory with FormControlProp
    // They use intrinsic sizes and don't have resolved min/max constraints
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM) {
        log_debug("apply_flex_constraint: form control, skipping constraint (computed=%.1f)", computed_size);
        return computed_size;
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
    int margin_top = item->bound ? item->bound->margin.top : 0;

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
        int parent_offset_y = 0;
        if (item->bound) {
            parent_offset_y = item->bound->padding.top;
            if (item->bound->border) {
                parent_offset_y += item->bound->border->width.top;
            }
        }
        int result = margin_top + parent_offset_y + item_block->embed->flex->first_baseline;
        log_debug("calculate_item_baseline: flex container item=%p, first_baseline=%d, result=%d",
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
                int child_baseline = calculate_item_baseline(child);

                if (child_baseline > 0) {
                    // child->y is already relative to item's content box
                    // We need to account for item's padding/border to get position from margin edge
                    int parent_offset_y = 0;
                    if (item->bound) {
                        parent_offset_y = item->bound->padding.top;
                        if (item->bound->border) {
                            parent_offset_y += item->bound->border->width.top;
                        }
                    }

                    // child->y is relative to parent's content box (after border+padding)
                    // So the child's position from parent's margin edge is:
                    // margin_top + parent_offset_y + child->y + child_baseline
                    int result = margin_top + parent_offset_y + (int)child->y + child_baseline;

                    log_debug("calculate_item_baseline: item=%p, child=%p, child_baseline=%d, child->y=%d, result=%d",
                              item, child, child_baseline, (int)child->y, result);
                    return result;
                }
            }
        }
        child_view = (View*)child_view->next_sibling;
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
        int align_self = item->fi ? (int)item->fi->align_self : ALIGN_AUTO;
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
    log_info("BASELINE REPOSITIONING START: container=%p (%s)",
             flex_container, flex_container ? flex_container->node_name() : "null");

    if (!flex_container) {
        log_leave();
        return;
    }

    FlexContainerLayout* flex_layout = lycon->flex_container;
    if (!flex_layout) {
        log_debug("No flex layout context, skipping baseline repositioning");
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
        log_debug("Container doesn't use baseline alignment, skipping");
        log_leave();
        return;
    }

    // Only reposition for horizontal main axis (baseline alignment is only for rows)
    if (!is_main_axis_horizontal(flex_layout)) {
        log_debug("Column direction, baseline alignment equivalent to start, skipping");
        log_leave();
        return;
    }

    log_info("Container uses baseline alignment, recalculating positions after nested layout");

    // For each line, recalculate baselines and reposition items
    for (int line_idx = 0; line_idx < flex_layout->line_count; line_idx++) {
        FlexLineInfo* line = &flex_layout->lines[line_idx];

        // Recalculate max baseline now that children are laid out
        int max_baseline = find_max_baseline(line, flex_layout->align_items);
        log_debug("Line %d: Recalculated max_baseline=%d", line_idx, max_baseline);

        // Reposition each baseline-aligned item
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item || !item->fi) continue;

            // Check if this item uses baseline alignment
            int align_self = item->fi->align_self;
            bool uses_baseline = (align_self == ALIGN_BASELINE) ||
                                (align_self == ALIGN_AUTO && flex_layout->align_items == ALIGN_BASELINE);

            if (!uses_baseline) continue;

            // Calculate this item's baseline
            int item_baseline = calculate_item_baseline(item);

            // Calculate new cross position to align baselines
            // The item should be positioned so that:
            // line_cross_position + cross_pos + item_baseline = line_cross_position + max_baseline
            // Therefore: cross_pos = max_baseline - item_baseline
            int new_cross_pos = max_baseline - item_baseline;

            // Get current position for comparison
            int old_cross_pos = get_cross_axis_position(item, flex_layout);

            // For multi-line or wrap-reverse, account for line position
            int line_cross_pos = line->cross_position;

            // The final position relative to container
            int final_pos = line_cross_pos + new_cross_pos;

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
                    final_pos += (int)relative_offset;
                    log_debug("Item %d: Adding relative offset %.0f to final_pos", i, relative_offset);
                }
            }

            log_debug("Item %d: item_baseline=%d, max_baseline=%d, old_pos=%d, new_pos=%d (line_pos=%d + offset=%d)",
                      i, item_baseline, max_baseline, old_cross_pos, final_pos, line_cross_pos, new_cross_pos);

            // Only update if position changed
            if (final_pos != old_cross_pos) {
                // Since coordinates are relative to parent, we just update the item's position
                // Children don't need to be translated - their relative positions stay the same
                set_cross_axis_position(item, final_pos, flex_layout);
                log_info("Repositioned baseline item %d: %d -> %d", i, old_cross_pos, final_pos);
            }
        }
    }

    log_info("BASELINE REPOSITIONING END");
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

            // Add gap space if not the first item
            int gap_space = line->item_count > 0 ?
                (is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap) : 0;

            log_debug("LINE %d - item %d: hypothetical=%d, gap=%d, line_size=%d, container=%d",
                   flex_layout->line_count, current_item, item_hypothetical, gap_space, main_size, container_main_size);

            // Check if we need to wrap (only if not the first item in line)
            // Use hypothetical main size (clamped by min/max) for wrapping decision
            if (flex_layout->wrap != WRAP_NOWRAP &&
                line->item_count > 0 &&
                main_size + item_hypothetical + gap_space > container_main_size) {
                log_debug("WRAP - item %d needs new line (would be %d > %d)",
                       current_item, main_size + item_hypothetical + gap_space, container_main_size);
                break;
            }

            line->items[line->item_count] = item;
            line->item_count++;
            main_size += item_hypothetical + gap_space;
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
        total_base_size += basis;

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
                total_unfrozen_base += item_flex_basis[i];
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
        // CRITICAL: When main axis is indefinite (shrink-to-fit), NEVER grow items
        // per CSS Flexbox spec §9.2 - items keep their basis sizes, container shrinks to fit
        bool is_growing = use_grow_factor && remaining_free_space > 0 && !flex_layout->main_axis_is_indefinite;
        bool is_shrinking = !use_grow_factor && remaining_free_space < 0;

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
                // Note: remaining_free_space is already adjusted by step 4b for fractional flex factors
                float flex_basis = item_flex_basis[i];
                double grow_ratio = fg / total_flex_factor;
                float grow_amount = (float)(grow_ratio * remaining_free_space);
                target_size = flex_basis + grow_amount;

                log_debug("ITERATIVE_FLEX - item %d: grow_ratio=%.4f, grow_amount=%.1f, basis=%.1f→%.1f",
                          i, grow_ratio, grow_amount, flex_basis, target_size);

            } else if (is_shrinking && fs > 0) {
                // FLEX-SHRINK: Distribute negative space using scaled shrink factor
                // Per CSS spec §9.7 step 4c: target = flex_base_size - fraction of abs(remaining free space)
                float flex_basis = item_flex_basis[i];
                double scaled_shrink = (double)flex_basis * fs;
                double shrink_ratio = scaled_shrink / total_scaled_shrink;
                float shrink_amount = (float)(shrink_ratio * (-remaining_free_space));
                target_size = flex_basis - shrink_amount;

                log_debug("FLEX_SHRINK - item %d: shrink_ratio=%.4f, shrink=%.1f, %.1f→%.1f",
                          i, shrink_ratio, shrink_amount, flex_basis, target_size);
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
                log_debug("JUSTIFY_CONTENT overflow fallback: %d -> %d (free_space=%d)",
                         old_justify, justify, (int)free_space);
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
                    // ENHANCED: Space-between distributes remaining space evenly between items
                    // Don't include gaps in calculation - space-between replaces gaps
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
            if (left_auto) current_pos += (int)auto_margin_size;

            log_debug("MAIN_ALIGN_ITEM %d - positioning at: %.0f", i, current_pos);
            set_main_axis_position(item, current_pos, flex_layout);
            current_pos += get_main_axis_size(item, flex_layout);

            // Add auto margin space after item
            if (right_auto) current_pos += (int)auto_margin_size;

            // Add non-auto end margin
            if (!right_auto && item->bound) {
                int margin_end = is_main_axis_horizontal(flex_layout) ?
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
            int item_size = get_main_axis_size(item, flex_layout);

            // Get item margins in main axis direction
            int margin_start = 0, margin_end = 0;
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

            int order_val = item && item->fi ? item->fi->order : -999;
            log_debug("align_items_main_axis: Positioning item %d (order=%d, ptr=%p) at position %d (margin_start=%d), size=%d",
                      i, order_val, item, current_pos, margin_start, item_size);
            set_main_axis_position(item, current_pos, flex_layout);
            log_debug("align_items_main_axis: After set, item->x=%d, item->y=%d", item->x, item->y);

            // Advance by item size + margin-end
            current_pos += item_size + margin_end;

            // Add justify-content spacing (for space-between, space-around, etc.)
            if (spacing > 0 && i < line->item_count - 1) {
                current_pos += spacing;
            }

            // Add gap between items (but not after the last item or with space-between)
            if (i < line->item_count - 1 && flex_layout->justify != CSS_VALUE_SPACE_BETWEEN) {
                int gap = is_main_axis_horizontal(flex_layout) ?
                         flex_layout->column_gap : flex_layout->row_gap;
                if (gap > 0) {
                    current_pos += gap;
                    log_debug("Added gap=%d between items %d and %d", gap, i, i+1);
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
                (item->blk && item->blk->given_height > 0) :
                (item->blk && item->blk->given_width > 0);

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

        int item_cross_size = get_cross_axis_size(item, flex_layout);
        int line_cross_size = line->cross_size;
        int old_pos = get_cross_axis_position(item, flex_layout);
        log_debug("CROSS_ALIGN_ITEM %d - cross_size: %d, old_pos: %d, line_cross_size: %d",
               i, item_cross_size, old_pos, line_cross_size);
        int cross_pos = 0;

        // Check for auto margins in cross axis
        bool top_auto = is_main_axis_horizontal(flex_layout) ?
            item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO;
        bool bottom_auto = is_main_axis_horizontal(flex_layout) ?
            item->bound && item->bound->margin.bottom_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.right_type == CSS_VALUE_AUTO;

        if (top_auto || bottom_auto) {
            // Handle auto margins in cross axis
            int container_cross_size = is_main_axis_horizontal(flex_layout) ?
                                     flex_layout->cross_axis_size : flex_layout->main_axis_size;

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
            int available_cross_size = use_line_cross ? line_cross_size : (int)flex_layout->cross_axis_size;

            // For wrap-reverse, swap start and end alignments
            int effective_align = align_type;
            if (is_wrap_reverse) {
                if (align_type == ALIGN_START || align_type == CSS_VALUE_START) {
                    effective_align = ALIGN_END;
                } else if (align_type == ALIGN_END || align_type == CSS_VALUE_END) {
                    effective_align = ALIGN_START;
                }
            }

            // Regular alignment (handle both flex-specific and generic alignment keywords)
            switch (effective_align) {
                case ALIGN_START:
                case CSS_VALUE_START:  // Handle 'start' same as 'flex-start'
                    cross_pos = 0;
                    break;
                case ALIGN_END:
                case CSS_VALUE_END:  // Handle 'end' same as 'flex-end'
                    cross_pos = available_cross_size - item_cross_size;
                    break;
                case ALIGN_CENTER:
                    cross_pos = (available_cross_size - item_cross_size) / 2;
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
                            has_explicit_cross_size = (item->blk && item->blk->given_height > 0);
                        } else {
                            // Column direction: cross-axis is width
                            // blk->given_width is set only by CSS width property
                            has_explicit_cross_size = (item->blk && item->blk->given_width > 0);
                        }

                        log_debug("ALIGN_STRETCH item %d (%s): has_explicit=%d, available=%d, item_cross=%d, blk=%p, given_width=%.1f, type=%d",
                                  i, item->node_name(), has_explicit_cross_size, available_cross_size, item_cross_size,
                                  item->blk, item->blk ? item->blk->given_width : -999.0f,
                                  item->blk ? item->blk->given_width_type : -1);

                        if (has_explicit_cross_size) {
                            // Item has explicit size - set item dimension to border-box value
                            // item_cross_size is already the border-box size (from get_cross_axis_size)
                            set_cross_axis_size(item, item_cross_size, flex_layout);

                            // For wrap-reverse, position at end of line
                            if (is_wrap_reverse) {
                                cross_pos = available_cross_size - item_cross_size;
                            } else {
                                cross_pos = 0;
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
                            int target_cross_size = available_cross_size - (int)(margin_cross_start + margin_cross_end);
                            if (target_cross_size < 0) target_cross_size = 0;

                            cross_pos = (int)margin_cross_start;
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
                        int item_baseline = calculate_item_baseline(item);
                        // Position item so its baseline aligns with max baseline
                        // cross_pos is relative to line start, item_baseline is from item's margin edge
                        // max_baseline - item_baseline gives the offset needed
                        cross_pos = max_baseline - item_baseline;
                        log_debug("ALIGN_BASELINE - item %d: item_baseline=%d, max_baseline=%d, cross_pos=%d",
                                  i, item_baseline, max_baseline, cross_pos);
                    } else {
                        // For column direction, baseline is equivalent to start
                        cross_pos = 0;
                    }
                    break;
                default:
                    cross_pos = 0;
                    break;
            }
        }

        // CRITICAL: Add line's cross position to get absolute position
        // line->cross_position is set by align_content for multi-line layouts
        int absolute_cross_pos = line->cross_position + cross_pos;
        log_debug("FINAL_CROSS_POS - item %d: line_pos=%d + cross_pos=%d = %d",
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
    int container_cross_size = (int)flex_layout->cross_axis_size;

    int total_lines_size = 0;
    for (int i = 0; i < flex_layout->line_count; i++) {
        total_lines_size += flex_layout->lines[i].cross_size;
    }

    int gap_space = calculate_gap_space(flex_layout, flex_layout->line_count, false);
    total_lines_size += gap_space;

    int free_space = container_cross_size - total_lines_size;
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
        if (item->blk && item->blk->given_height > 0) {
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
        if (item->blk && item->blk->given_width > 0) {
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
            int space_to_distribute = (int)((flex_factor / total_flex) * free_space);

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
        // given_height > 0 means explicit height (0 or negative means auto)
        return item->blk->given_height > 0;
    } else {
        // Cross-axis is width for column direction
        // given_width > 0 means explicit width (0 or negative means auto)
        return item->blk->given_width > 0;
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
        flex_layout->lines[0].cross_size = (int)flex_layout->cross_axis_size;
        log_debug("LINE_CROSS_SIZE: nowrap with definite cross, line 0 = %.1f (container cross)",
                  flex_layout->cross_axis_size);
        return;
    }

    // Otherwise, calculate line cross sizes from item hypothetical cross sizes
    // IMPORTANT: align-content: stretch only affects MULTI-LINE containers.
    // For single-line (nowrap) containers, align-content has no effect per CSS spec.
    // The optimization below only applies to multi-line containers.
    bool is_wrapping = (flex_layout->wrap != WRAP_NOWRAP);
    bool align_content_stretch = is_wrapping && (flex_layout->align_content == ALIGN_STRETCH);

    for (int i = 0; i < flex_layout->line_count; i++) {
        FlexLineInfo* line = &flex_layout->lines[i];
        float max_cross_size = 0;

        // Find the maximum hypothetical outer cross size among items in this line
        for (int j = 0; j < line->item_count; j++) {
            ViewElement* item = (ViewElement*)line->items[j]->as_element();
            if (!item) continue;

            bool has_definite = item_has_definite_cross_size(item, flex_layout);
            bool will_stretch = item_will_stretch(item, flex_layout);

            // For MULTI-LINE containers with align-content: stretch, items with auto cross-size
            // AND align-self: stretch should NOT contribute their content size to line cross-size
            // because the lines will be stretched to fill the container anyway.
            // However, they should still contribute their min-cross-size (e.g., min-height)!
            // NOTE: This does NOT apply to single-line (nowrap) containers!
            if (align_content_stretch &&
                !has_definite &&
                will_stretch) {
                // Even for stretch items, consider their minimum size
                float min_cross_size = 0;
                if (item->fi) {
                    if (is_main_axis_horizontal(flex_layout)) {
                        min_cross_size = item->fi->resolved_min_height;
                    } else {
                        min_cross_size = item->fi->resolved_min_width;
                    }
                }
                if (min_cross_size > 0) {
                    log_debug("STRETCH_ITEM_MIN: line %d item %d - using min-cross-size: %.1f",
                              i, j, min_cross_size);
                    if (min_cross_size > max_cross_size) {
                        max_cross_size = min_cross_size;
                    }
                } else {
                    log_debug("SKIP_STRETCH_ITEM: line %d item %d - auto cross-size with stretch, skipping",
                              i, j);
                }
                continue;
            }

            // Use hypothetical outer cross size if available (computed in Phase 4.5)
            // Otherwise fall back to get_cross_axis_size
            float item_cross_size = 0;
            if (item->fi && item->fi->hypothetical_outer_cross_size > 0) {
                item_cross_size = item->fi->hypothetical_outer_cross_size;
                log_debug("LINE_CROSS: item[%d][%d] using hypothetical_outer_cross=%.1f",
                          i, j, item_cross_size);
            } else {
                item_cross_size = get_cross_axis_size(item, flex_layout);
                log_debug("LINE_CROSS: item[%d][%d] using fallback cross=%.1f", i, j, item_cross_size);
            }

            if (item_cross_size > max_cross_size) {
                max_cross_size = item_cross_size;
            }
        }

        line->cross_size = (int)max_cross_size;
        log_debug("LINE_CROSS_SIZE: line %d = %d", i, (int)max_cross_size);
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
    if (elem->blk && elem->blk->given_height > 0) {
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
    // Check if intrinsic height was calculated (intrinsic_height is content-based)
    if (elem->fi && elem->fi->has_intrinsic_height && elem->fi->intrinsic_height.max_content > 0) {
        return (float)elem->fi->intrinsic_height.max_content;
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
            if (!item || !item->fi) continue;

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
                if (item->blk && item->blk->given_height > 0) {
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
                    // FIX: For items without explicit height, use recursive measurement
                    // This handles nested flex containers properly
                    float measured_height = measure_flex_content_height(item);
                    if (measured_height > 0) {
                        // Add padding and border to content height to get border-box height
                        float padding_border_height = 0;
                        if (item->bound) {
                            padding_border_height += item->bound->padding.top + item->bound->padding.bottom;
                            if (item->bound->border) {
                                padding_border_height += item->bound->border->width.top + item->bound->border->width.bottom;
                            }
                        }
                        hypothetical_cross = measured_height + padding_border_height;
                        // Update item dimensions so alignment uses correct size
                        item->height = (int)hypothetical_cross;
                        item->content_height = (int)measured_height;
                    } else {
                        // use measured/content height
                        hypothetical_cross = item->height > 0 ? item->height : item->content_height;
                    }
                }
            } else {
                // cross-axis is width
                min_cross = item->fi->resolved_min_width;
                max_cross = (item->fi->resolved_max_width > 0) ?
                            item->fi->resolved_max_width : INFINITY;

                // check for explicit cross-axis size (CSS width)
                if (item->blk && item->blk->given_width > 0) {
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
    log_debug("CONTAINER_CROSS: Determining cross size, is_horizontal=%d", is_horizontal);

    // check if container has definite cross size
    bool has_definite_cross = false;
    float definite_cross = 0;

    if (is_horizontal) {
        // cross-axis is height
        if (container->blk && container->blk->given_height > 0) {
            has_definite_cross = true;
            definite_cross = container->blk->given_height;
            log_debug("CONTAINER_CROSS: Container has definite height=%.1f", definite_cross);
        }
    } else {
        // cross-axis is width
        if (container->blk && container->blk->given_width > 0) {
            has_definite_cross = true;
            definite_cross = container->blk->given_width;
            log_debug("CONTAINER_CROSS: Container has definite width=%.1f", definite_cross);
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
            log_debug("CONTAINER_CROSS: Container is flex item with cross size from parent=%.1f",
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
        log_debug("CONTAINER_CROSS: Using definite cross size=%.1f", definite_cross);
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
            log_debug("CONTAINER_CROSS: Applied min constraint, now=%.1f", total_cross);
        }
        if (max_cross > 0 && total_cross > max_cross) {
            total_cross = max_cross;
            log_debug("CONTAINER_CROSS: Applied max constraint, now=%.1f", total_cross);
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
        log_debug("CONTAINER_CROSS: Final cross_axis_size=%.1f (lines=%d)",
                  total_cross, flex_layout->line_count);
    } else {
        log_debug("CONTAINER_CROSS: No cross size computed, keeping existing=%.1f",
                  flex_layout->cross_axis_size);
    }
}
