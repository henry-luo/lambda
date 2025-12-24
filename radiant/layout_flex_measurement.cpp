#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "intrinsic_sizing.hpp"
#include "../lambda/input/css/css_style_node.hpp"

#include "../lib/log.h"
#include <float.h>
#include <limits.h>

// Forward declaration for recursive function
static float measure_content_height_recursive(DomNode* node, LayoutContext* lycon);

// Helper to get explicit CSS width from DOM element's specified_style
// Returns -1 if no explicit width is set, otherwise returns the width in pixels
static float get_explicit_css_width(LayoutContext* lycon, ViewElement* elem) {
    if (!elem || !elem->specified_style) return -1;

    CssDeclaration* width_decl = style_tree_get_declaration(
        elem->specified_style, CSS_PROPERTY_WIDTH);

    if (!width_decl || !width_decl->value) return -1;

    // Only consider absolute length values (not percentages, auto, etc.)
    if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
        float width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
        if (!isnan(width) && width > 0) {
            return width;
        }
    }

    return -1;
}

// Helper to get explicit CSS height from DOM element's specified_style
static float get_explicit_css_height(LayoutContext* lycon, ViewElement* elem) {
    if (!elem || !elem->specified_style) return -1;

    CssDeclaration* height_decl = style_tree_get_declaration(
        elem->specified_style, CSS_PROPERTY_HEIGHT);

    if (!height_decl || !height_decl->value) return -1;

    if (height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
        float height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
        if (!isnan(height) && height > 0) {
            return height;
        }
    }

    return -1;
}

// ============================================================================
// Recursive DOM-based content height measurement for nested flex containers
// ============================================================================
// This function recursively traverses the DOM tree to find the content-based
// height of nested flex containers before their Views are fully initialized.
static float measure_content_height_recursive(DomNode* node, LayoutContext* lycon) {
    if (!node || !node->is_element()) return 0;

    // Check if the View is already set with an explicit height
    ViewElement* elem = (ViewElement*)node->as_element();
    if (elem) {
        log_debug("measure_content_height_recursive: checking elem %s, blk=%p height=%.1f",
                  elem->tag_name ? elem->tag_name : "(null)",
                  (void*)elem->blk, elem->height);
        if (elem->blk && elem->blk->given_height > 0) {
            log_debug("measure_content_height_recursive: elem %s has given_height=%.1f",
                      elem->tag_name ? elem->tag_name : "(null)", elem->blk->given_height);
            return elem->blk->given_height;
        }
        if (elem->height > 0) {
            log_debug("measure_content_height_recursive: elem %s has height=%.1f",
                      elem->tag_name ? elem->tag_name : "(null)", elem->height);
            return (float)elem->height;
        }
        if (elem->fi && elem->fi->has_intrinsic_height && elem->fi->intrinsic_height.max_content > 0) {
            log_debug("measure_content_height_recursive: elem %s has intrinsic_height=%.1f",
                      elem->tag_name ? elem->tag_name : "(null)", elem->fi->intrinsic_height.max_content);
            return (float)elem->fi->intrinsic_height.max_content;
        }

        // Also check specified_style for explicit height
        if (elem->specified_style) {
            CssDeclaration* height_decl = style_tree_get_declaration(
                elem->specified_style, CSS_PROPERTY_HEIGHT);
            if (height_decl && height_decl->value &&
                height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                float explicit_height = (float)resolve_length_value(lycon, CSS_PROPERTY_HEIGHT,
                                                                    height_decl->value);
                if (explicit_height > 0) {
                    log_debug("measure_content_height_recursive: elem %s has specified height=%.1fpx",
                              elem->tag_name ? elem->tag_name : "(null)", explicit_height);
                    return explicit_height;
                }
            }
        }
    }

    // Check if this is a flex container
    DisplayValue display = resolve_display_value((void*)node);
    if (display.inner != CSS_VALUE_FLEX) {
        // Not a flex container - no recursive measurement needed
        return 0;
    }

    // Traverse children to calculate content-based height
    // Default to row direction (CSS default) unless we can determine otherwise
    bool is_row = true;  // CSS default is row

    float max_child_height = 0;
    float sum_child_height = 0;

    // Only elements can have children - get as_element() to access first_child
    DomElement* dom_elem = node->as_element();
    if (!dom_elem) return 0;

    DomNode* child = dom_elem->first_child;
    while (child) {
        if (child->is_element()) {
            float child_height = measure_content_height_recursive(child, lycon);
            log_debug("measure_content_height_recursive: child %s height=%.1f",
                      child->node_name(), child_height);

            if (is_row) {
                max_child_height = fmax(max_child_height, child_height);
            } else {
                sum_child_height += child_height;
            }
        }
        child = child->next_sibling;
    }

    float result = is_row ? max_child_height : sum_child_height;
    log_debug("measure_content_height_recursive: node %s = %.1f (is_row=%d)",
              node->node_name(), result, is_row);
    return result;
}

// Content measurement for multi-pass flex layout
// This file implements the first pass of the multi-pass flex layout algorithm

// Global measurement cache (simplified implementation)
static MeasurementCacheEntry measurement_cache[1000]; // Fixed size for simplicity
static int cache_count = 0;

void store_in_measurement_cache(DomNode* node, int width, int height,
                               int content_width, int content_height) {
    if (cache_count >= 1000) {
        log_error("Measurement cache overflow");
        return;
    }

    measurement_cache[cache_count].node = node;
    measurement_cache[cache_count].measured_width = width;
    measurement_cache[cache_count].measured_height = height;
    measurement_cache[cache_count].content_width = content_width;
    measurement_cache[cache_count].content_height = content_height;
    cache_count++;

    log_debug("Cached measurement for node %p: %dx%d (content: %dx%d)",
              node, width, height, content_width, content_height);
}

MeasurementCacheEntry* get_from_measurement_cache(DomNode* node) {
    for (int i = 0; i < cache_count; i++) {
        if (measurement_cache[i].node == node) {
            return &measurement_cache[i];
        }
    }
    return nullptr;
}

void clear_measurement_cache() {
    cache_count = 0;
    log_debug("Cleared measurement cache");
}

// Measure flex child content without applying final sizing
void measure_flex_child_content(LayoutContext* lycon, DomNode* child) {
    if (!child) return;

    log_debug("Measuring flex child content for %s", child->node_name());

    // Check if already measured
    MeasurementCacheEntry* cached = get_from_measurement_cache(child);
    if (cached) {
        log_debug("Using cached measurement for %s", child->node_name());
        return;
    }

    // Save current layout context
    LayoutContext saved_context = *lycon;

    // Create temporary measurement context
    LayoutContext measure_context = *lycon;
    measure_context.block.content_width = -1;  // Unconstrained width for measurement
    measure_context.block.content_height = -1; // Unconstrained height for measurement
    measure_context.block.advance_y = 0;
    measure_context.block.max_width = 0;

    // Set up measurement environment
    line_init(&measure_context, 0, 10000);

    // Perform layout in measurement mode to determine intrinsic sizes
    ViewBlock* temp_view = nullptr;
    int measured_width = 0;
    int measured_height = 0;
    int content_width = 0;
    int content_height = 0;

    if (child->is_text()) {
        // Measure text content
        measure_text_content(&measure_context, child, &measured_width, &measured_height);
        content_width = measured_width;
        content_height = measured_height;
    } else {
        // Measure element content by performing a preliminary layout
        // Set up the measurement context with the container's width constraint
        float container_width = lycon->block.content_width;
        if (container_width <= 0) container_width = 366;  // Default fallback

        // Set up block context for measurement
        measure_context.block.content_width = container_width;
        measure_context.block.content_height = -1;  // Unconstrained height
        measure_context.block.advance_y = 0;
        measure_context.block.max_width = 0;
        measure_context.is_measuring = true;

        // Initialize line context
        line_init(&measure_context, 0, container_width);

        // Check if this element is a row flex container
        // For row flex containers, we should use MAX of child heights, not SUM
        ViewElement* elem_view = (ViewElement*)child->as_element();
        bool is_row_flex = false;
        if (elem_view) {
            log_debug("measure_flex_child_content: elem_view=%p, view_type=%d, display.inner=%d (CSS_VALUE_FLEX=%d)",
                      elem_view, elem_view->view_type, elem_view->display.inner, CSS_VALUE_FLEX);
            // Check display property directly on the DOM element
            if (elem_view->display.inner == CSS_VALUE_FLEX) {
                // It's a flex container - check direction
                ViewBlock* block_view = (ViewBlock*)elem_view;
                if (block_view->embed && block_view->embed->flex) {
                    int dir = block_view->embed->flex->direction;
                    is_row_flex = (dir == CSS_VALUE_ROW || dir == CSS_VALUE_ROW_REVERSE);
                    log_debug("Element %s is%s a row flex container (direction=%d)",
                              child->node_name(), is_row_flex ? "" : " NOT", dir);
                } else {
                    // Default flex direction is row
                    is_row_flex = true;
                    log_debug("Element %s is a flex container with default row direction",
                              child->node_name());
                }
            }
        }

        // Measure child content heights by traversing the subtree
        // The child parameter is the flex item element - get its first_child
        measured_height = 0;
        int max_child_height = 0;  // For row flex containers
        measured_width = 0;
        DomElement* child_elem = child->as_element();

        // Get font-size from resolved styles (after init_flex_item_view resolved CSS)
        ViewElement* view_elem = (ViewElement*)child_elem;
        int elem_font_size = 16;  // default fallback
        if (view_elem && view_elem->font && view_elem->font->font_size > 0) {
            elem_font_size = (int)(view_elem->font->font_size + 0.5f);
        }

        // Calculate actual line height using the font's metrics (Chrome-compatible)
        // This requires setting up the font first to get accurate FT_Face metrics
        int text_line_height = elem_font_size;  // fallback
        if (lycon->ui_context && view_elem && view_elem->font) {
            // Set up font at the element's font size
            FontBox temp_font;
            setup_font(lycon->ui_context, &temp_font, view_elem->font);
            if (temp_font.ft_face) {
                // Use the Chrome-compatible line height calculation
                text_line_height = (int)(calc_normal_line_height(temp_font.ft_face) + 0.5f);
            }
        }
        log_debug("measure_flex_child_content: elem_font_size=%d, text_line_height=%d",
                  elem_font_size, text_line_height);

        if (child_elem) {
            DomNode* sub_child = child_elem->first_child;
            while (sub_child) {
                if (sub_child->is_text()) {
                    // Text content - skip whitespace-only text, estimate lines for real content
                    const char* text = (const char*)sub_child->text_data();
                    if (text && strlen(text) > 0) {
                        // Check if text is only whitespace
                        bool is_whitespace_only = true;
                        for (const char* p = text; *p; p++) {
                            if (!is_space((unsigned char)*p)) {
                                is_whitespace_only = false;
                                break;
                            }
                        }
                        if (!is_whitespace_only) {
                            // Use computed line height based on element's font-size
                            int text_height = text_line_height;
                            if (is_row_flex) {
                                // Row flex: text is a flex item, use max
                                if (text_height > max_child_height) {
                                    max_child_height = text_height;
                                }
                            } else {
                                measured_height += text_height;
                            }
                        }
                    }
                } else if (sub_child->is_element()) {
                    // Element - check for block-level element heights
                    DomElement* elem = sub_child->as_element();

                    // Estimate element height based on type
                    uintptr_t tag = sub_child->tag();
                    int elem_height = 0;

                    // Common block elements with typical heights
                    if (tag == HTM_TAG_H1) elem_height = 32;
                    else if (tag == HTM_TAG_H2) elem_height = 28;
                    else if (tag == HTM_TAG_H3) elem_height = 24;
                    else if (tag == HTM_TAG_H4) elem_height = 20;
                    else if (tag == HTM_TAG_H5 || tag == HTM_TAG_H6) elem_height = 18;
                    else if (tag == HTM_TAG_P) elem_height = 36;  // Typically 2-3 lines
                    else if (tag == HTM_TAG_UL || tag == HTM_TAG_OL) {
                        // Count list items
                        int li_count = 0;
                        if (elem) {
                            DomNode* li = elem->first_child;
                            while (li) {
                                if (li->is_element()) li_count++;
                                li = li->next_sibling;
                            }
                        }
                        elem_height = li_count * 18;  // ~18px per list item
                    }
                    else if (tag == HTM_TAG_DIV || tag == HTM_TAG_SECTION ||
                             tag == HTM_TAG_ARTICLE || tag == HTM_TAG_NAV ||
                             tag == HTM_TAG_HEADER || tag == HTM_TAG_FOOTER ||
                             tag == HTM_TAG_ASIDE || tag == HTM_TAG_MAIN) {
                        // Container elements - check if it's a flex container and has actual content
                        // For empty containers (especially flex items), height should be 0
                        // For nested flex containers, DON'T use estimation - let flex layout
                        // determine height based on actual content

                        // Check if this is a flex container by looking at DOM's specified_style
                        // (display.inner may not be resolved yet during measurement pass)
                        bool is_nested_flex = false;

                        // First check view's display.inner if already resolved
                        ViewElement* nested_view = (ViewElement*)elem;
                        if (nested_view && nested_view->display.inner == CSS_VALUE_FLEX) {
                            is_nested_flex = true;
                        }

                        // If not resolved, check DOM's specified CSS style
                        if (!is_nested_flex && elem && elem->specified_style) {
                            CssDeclaration* display_decl = style_tree_get_declaration(
                                elem->specified_style, CSS_PROPERTY_DISPLAY);
                            if (display_decl && display_decl->value &&
                                display_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                                CssEnum display_value = display_decl->value->data.keyword;
                                if (display_value == CSS_VALUE_FLEX ||
                                    display_value == CSS_VALUE_INLINE_FLEX) {
                                    is_nested_flex = true;
                                    log_debug("Nested flex container detected via specified_style");
                                }
                            }
                        }

                        if (is_nested_flex) {
                            // Nested flex container - don't estimate, use 0
                            // The flex layout will determine actual height from content
                            elem_height = 0;
                            log_debug("Nested flex container: using 0 for height estimation");
                        } else {
                            bool has_content = false;
                            if (elem) {
                                DomNode* content = elem->first_child;
                                while (content && !has_content) {
                                    if (content->is_element()) {
                                        // Has nested element
                                        has_content = true;
                                    } else if (content->is_text()) {
                                        // Check if text is non-whitespace
                                        const char* text = (const char*)content->text_data();
                                        if (text) {
                                            for (const char* p = text; *p; p++) {
                                                if (!is_space((unsigned char)*p)) {
                                                    has_content = true;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                    content = content->next_sibling;
                                }
                            }
                            // Only estimate height if container has actual content
                            elem_height = has_content ? 56 : 0;
                        }
                    }
                    else {
                        // For other elements (span, a, etc.), check if they have text content
                        // and use text_line_height if so
                        bool has_text_content = false;
                        if (elem) {
                            DomNode* content = elem->first_child;
                            while (content) {
                                if (content->is_text()) {
                                    const char* text = (const char*)content->text_data();
                                    if (text) {
                                        for (const char* p = text; *p; p++) {
                                            if (!is_space((unsigned char)*p)) {
                                                has_text_content = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (has_text_content) break;
                                content = content->next_sibling;
                            }
                        }
                        if (has_text_content) {
                            // Use the computed text line height for text-containing elements
                            elem_height = text_line_height;
                            log_debug("Element %s has text content, using text_line_height=%d",
                                      sub_child->node_name(), elem_height);
                        } else {
                            elem_height = 20;  // Default element height
                        }
                    }

                    // Add margins only if element has height
                    if (elem_height > 0) {
                        if (is_row_flex) {
                            // Row flex: use MAX of child heights
                            // Don't add margin for text-containing inline elements
                            int margin = (elem_height == text_line_height) ? 0 : 10;
                            int total_elem_height = elem_height + margin;
                            if (total_elem_height > max_child_height) {
                                max_child_height = total_elem_height;
                            }
                        } else {
                            // Column flex or normal block: SUM of child heights
                            measured_height += elem_height + 10;
                        }
                    }
                }
                sub_child = sub_child->next_sibling;
            }
        }

        // For row flex containers, use max_child_height instead of accumulated sum
        if (is_row_flex && max_child_height > 0) {
            measured_height = max_child_height;
            log_debug("Row flex container: using max child height %d", measured_height);
        }

        // Set measured dimensions
        // CRITICAL FIX: For elements without explicit width, measured_width should be based
        // on content, not container. Only use container_width if the element has explicit width.
        ViewElement* elem = (ViewElement*)child->as_element();
        bool has_explicit_width = (elem && elem->blk && elem->blk->given_width > 0);

        if (has_explicit_width) {
            measured_width = (int)elem->blk->given_width;
            log_debug("Measured element %s: using explicit width %d", child->node_name(), measured_width);
        } else {
            // For elements without explicit width, use 0 as intrinsic width
            // The actual width will be determined by flex layout (stretch, etc.)
            measured_width = 0;
            log_debug("Measured element %s: no explicit width, using 0", child->node_name());
        }
        content_width = measured_width;
        content_height = measured_height;

        // Add padding and border to measured height for total height
        // CSS box model: total_height = content_height + padding + border
        if (elem && elem->bound) {
            measured_height += (int)(elem->bound->padding.top + elem->bound->padding.bottom);
            if (elem->bound->border) {
                measured_height += (int)(elem->bound->border->width.top + elem->bound->border->width.bottom);
            }
            log_debug("Added box model to height: content=%d, total=%d (padding+border)",
                      content_height, measured_height);
        }

        log_debug("Measured element %s: %dx%d (content-based estimation)",
                  child->node_name(), measured_width, measured_height);
    }

    // Store measurement results
    store_in_measurement_cache(child, measured_width, measured_height,
                              content_width, content_height);

    // Restore original context
    *lycon = saved_context;

    log_debug("Content measurement complete for %s", child->node_name());
}

void measure_text_content(LayoutContext* lycon, DomNode* text_node, int* width, int* height) {
    // Legacy function - redirects to accurate measurement
    int min_w, max_w, h;
    measure_text_content_accurate(lycon, text_node, &min_w, &max_w, &h);
    *width = max_w;  // Use max-content for width
    *height = h;
}

// Enhanced accurate text measurement for intrinsic sizing
void measure_text_content_accurate(LayoutContext* lycon, DomNode* text_node,
                                   int* min_width, int* max_width, int* height) {
    const char* text_data = (const char*)text_node->text_data();
    size_t text_length = text_data ? strlen(text_data) : 0;

    if (!text_data || text_length == 0) {
        *min_width = *max_width = *height = 0;
        return;
    }

    // Measure using actual font metrics
    measure_text_run(lycon, text_data, text_length, min_width, max_width, height);

    log_debug("Measured text accurately: min=%d, max=%d, height=%d (\"%.*s\")",
              *min_width, *max_width, *height, (int)min(text_length, 20), text_data);
}

// Measure a text run with actual font metrics
// REFACTORED: Now uses unified intrinsic_sizing.hpp for text measurement
void measure_text_run(LayoutContext* lycon, const char* text, size_t length,
                     int* min_width, int* max_width, int* height) {
    if (!text || length == 0) {
        *min_width = *max_width = *height = 0;
        return;
    }

    // Use unified intrinsic sizing API
    TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, length);

    *max_width = widths.max_content;
    *min_width = widths.min_content;
    *height = (lycon->font.style && lycon->font.style->font_size > 0) ?
              (int)(lycon->font.style->font_size + 0.5f) : 20;

    log_debug("measure_text_run (unified): text_length=%zu, min=%d, max=%d, height=%d",
              length, *min_width, *max_width, *height);
}

int estimate_text_width(LayoutContext* lycon, const unsigned char* text, size_t length) {
    // Use unified intrinsic sizing API for accurate text width
    if (lycon && text && length > 0) {
        TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, (const char*)text, length);
        return widths.max_content;
    }
    // Fallback: rough estimate when no context available
    float avg_char_width = (lycon && lycon->font.style) ? lycon->font.style->font_size * 0.6f : 10.0f;
    return (int)(length * avg_char_width);
}

void cleanup_temporary_view(ViewBlock* temp_view) {
    // Cleanup temporary view and its resources
    if (temp_view) {
        // Free any allocated resources
        // Note: In practice, this might be handled by the memory pool
        log_debug("Cleaned up temporary measurement view");
    }
}

bool requires_content_measurement(ViewBlock* flex_container) {
    // Determine if content measurement is needed
    // This could be based on flex properties, content types, etc.

    if (!flex_container) return false;

    // Check if any children have auto flex-basis or need intrinsic sizing
    DomNode* child = nullptr;
    if (flex_container->is_element()) {
        child = ((DomElement*)flex_container)->first_child;
    }
    while (child) {
        // If child has complex content or auto sizing, measurement is needed
        DomNode* child_first = nullptr;
        if (child->is_element()) {
            child_first = static_cast<DomElement*>(child)->first_child;
        }
        if (child_first || child->is_text()) {
            return true;
        }
        child = child->next_sibling;
    }

    return false;
}

void measure_all_flex_children_content(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container) return;

    log_debug("Measuring all flex children content");
    DomNode* child = flex_container->first_child;
    int child_count = 0;  const int MAX_CHILDREN = 100; // Safety limit
    while (child && child_count < MAX_CHILDREN) {
        measure_flex_child_content(lycon, child);
        child = child->next_sibling;
        child_count++;
    }
    log_debug("Content measurement complete for %d children", child_count);
}

// Lightweight View creation for flex items with measured sizes
void layout_flow_node_for_flex(LayoutContext* lycon, DomNode* node) {
    if (!node) return;
    log_debug("=== TRACE: layout_flow_node_for_flex ENTRY for %s (node=%p)", node->node_name(), node);
    // Skip text nodes - flex layout only processes element nodes
    if (!node->is_element()) {
        log_debug("TRACE: Skipping text node in flex container: %s", node->node_name());
        return;
    }

    log_debug("TRACE: About to call init_flex_item_view for %s", node->node_name());
    // Create lightweight View for flex item element only (no child processing)
    init_flex_item_view(lycon, node);
    log_debug("TRACE: Completed init_flex_item_view for %s", node->node_name());

    // Apply measured sizes if available
    MeasurementCacheEntry* cached = get_from_measurement_cache(node);
    log_debug("DEBUG: cached = %p", cached);

    if (cached && node->view_type == RDT_VIEW_BLOCK) {
        ViewBlock* view = (ViewBlock*)node;
        log_debug("DEBUG: view = %p, node = %p", view, node);
        if (view == node) {
            log_debug("Applying cached measurements to flex item: %dx%d",
                cached->measured_width, cached->measured_height);

            // Use measured dimensions as hints (don't override explicit sizes)
            if (view->width <= 0) {
                view->width = cached->measured_width;
            }
            if (view->height <= 0) {
                view->height = cached->measured_height;
            }
            log_debug("Applied measurements: view size now %dx%d", view->width, view->height);
        } else {
            log_debug("DEBUG: Node mismatch - cached for different node");
        }
    } else {
        log_debug("DEBUG: Failed measurement application - cached=%p, node=%p", cached, node);
    }
}

// Set up basic flex item properties without content layout
void setup_flex_item_properties(LayoutContext* lycon, ViewBlock* view, DomNode* node) {
    (void)lycon; // Suppress unused parameter warning
    if (!view || !node) return;

    // Get display properties
    view->display = resolve_display_value(node);

    // Initialize position and sizing
    view->x = 0;
    view->y = 0;

    // Note: flex-specific properties (flex_grow, flex_shrink, flex_basis) and
    // box model properties (margin, padding, border) will be resolved by the flex algorithm
    // during CSS property resolution. We don't need to initialize them here.
    log_debug("Set up basic properties for flex item: %s", node->node_name());
}

// Create lightweight View for flex item element only (no child processing)
void init_flex_item_view(LayoutContext* lycon, DomNode* node) {
    if (!node || !node->is_element()) return;

    log_debug("*** TRACE: init_flex_item_view ENTRY for %s (node=%p)", node->node_name(), node);
    // Get display properties for the element
    DisplayValue display = resolve_display_value(node);

    // Create ViewBlock directly (similar to layout_block but without child processing)
    ViewBlock* block = (ViewBlock*)set_view(lycon,
        display.outer == CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK :
        display.outer == CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM :
        display.inner == CSS_VALUE_TABLE ? RDT_VIEW_TABLE : RDT_VIEW_BLOCK,
        node);

    if (!block) {
        log_error("Failed to allocate View for flex item: %s", node->node_name());
        return;
    }

    block->display = display;
    log_debug("*** SET DISPLAY: node=%p (%s), display={%d,%d}", node, node->node_name(), display.outer, display.inner);

    // Set up basic CSS properties (minimal setup for flex items)
    dom_node_resolve_style(node, lycon);

    // CRITICAL FIX: Ensure flex item properties are allocated
    // Even if no flex CSS properties are specified, we need fi for the flex algorithm
    alloc_flex_item_prop(lycon, block);

    // Initialize dimensions (will be set by flex algorithm)
    block->width = 0;  block->height = 0;
    block->content_width = 0;  block->content_height = 0;

    log_debug("init_flex_item_view EXIT for %s (node=%p, created_view=%p)", node->node_name(), node, block);
}

// ============================================================================
// Enhanced Intrinsic Sizing Implementation
// ============================================================================

// Calculate intrinsic sizes for a flex item
void calculate_item_intrinsic_sizes(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item || !item->fi) {
        log_debug("calculate_item_intrinsic_sizes: invalid item or no flex properties");
        return;
    }

    // Skip if BOTH intrinsic sizes are already calculated
    // We need both because cross-axis alignment may need the cross-axis intrinsic size
    if (item->fi->has_intrinsic_width && item->fi->has_intrinsic_height) {
        log_debug("calculate_item_intrinsic_sizes: both sizes already calculated");
        return;
    }

    log_debug("Calculating intrinsic sizes for item %p (%s)", item, item->node_name());

    // CRITICAL FIX: Set up font for the flex item BEFORE measuring text
    // This ensures text measurement uses the correct font (e.g., bold, specific size)
    LayoutContext* lycon = flex_layout ? flex_layout->lycon : nullptr;
    FontBox saved_font;  // Save current font to restore later
    bool font_changed = false;
    if (lycon && item->font) {
        saved_font = lycon->font;
        setup_font(lycon->ui_context, &lycon->font, item->font);
        font_changed = true;
    }

    // Initialize to zero
    // Use float to preserve precision from text measurement (avoids truncation)
    float min_width = 0, max_width = 0, min_height = 0, max_height = 0;

    // Check if this is a replaced element (img, video) - needs special handling
    uintptr_t elmt_name = item->tag();
    bool is_replaced = (elmt_name == HTM_TAG_IMG || elmt_name == HTM_TAG_VIDEO ||
                        elmt_name == HTM_TAG_IFRAME || elmt_name == HTM_TAG_CANVAS);

    if (is_replaced && lycon && elmt_name == HTM_TAG_IMG) {
        // Load image to get intrinsic dimensions
        log_debug("calculate_item_intrinsic_sizes: loading image for flex item %s", item->node_name());
        const char* src_value = item->get_attribute("src");
        if (src_value) {
            if (!item->embed) {
                item->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
            }
            if (!item->embed->img) {
                item->embed->img = load_image(lycon->ui_context, src_value);
            }
            if (item->embed->img) {
                ImageSurface* img = item->embed->img;
                float w = img->width * lycon->ui_context->pixel_ratio;
                float h = img->height * lycon->ui_context->pixel_ratio;

                // Check for explicit CSS dimensions
                float explicit_width = (item->blk && item->blk->given_width > 0) ?
                    item->blk->given_width : -1;
                float explicit_height = (item->blk && item->blk->given_height > 0) ?
                    item->blk->given_height : -1;

                // Also check max-width as constraint
                float max_width_constraint = (item->blk && item->blk->given_max_width > 0) ?
                    item->blk->given_max_width : -1;

                if (explicit_width > 0 && explicit_height > 0) {
                    // Both dimensions specified
                    min_width = max_width = explicit_width;
                    min_height = max_height = explicit_height;
                } else if (explicit_width > 0) {
                    // Width specified, compute height from aspect ratio
                    min_width = max_width = explicit_width;
                    min_height = max_height = explicit_width * h / w;
                } else if (explicit_height > 0) {
                    // Height specified, compute width from aspect ratio
                    min_height = max_height = explicit_height;
                    min_width = max_width = explicit_height * w / h;
                } else if (max_width_constraint > 0 && max_width_constraint < w) {
                    // Max-width constrains the image
                    min_width = max_width = max_width_constraint;
                    min_height = max_height = max_width_constraint * h / w;
                } else {
                    // Use intrinsic dimensions
                    min_width = max_width = w;
                    min_height = max_height = h;
                }
                log_debug("calculate_item_intrinsic_sizes: image intrinsic size=%.1fx%.1f (source=%.1fx%.1f)",
                          min_width, min_height, w, h);
            } else {
                // Failed to load image - use placeholder size
                log_debug("calculate_item_intrinsic_sizes: failed to load image %s", src_value);
                min_width = max_width = 40;
                min_height = max_height = 30;
            }
        } else {
            // No src attribute - use placeholder
            min_width = max_width = 40;
            min_height = max_height = 30;
        }

        // Store computed intrinsic sizes
        item->fi->intrinsic_width.min_content = min_width;
        item->fi->intrinsic_width.max_content = max_width;
        item->fi->has_intrinsic_width = true;

        item->fi->intrinsic_height.min_content = min_height;
        item->fi->intrinsic_height.max_content = max_height;
        item->fi->has_intrinsic_height = true;

        log_debug("calculate_item_intrinsic_sizes: image final intrinsic=%.1fx%.1f", max_width, max_height);
        
        // Restore font before returning
        if (font_changed) {
            lycon->font = saved_font;
        }
        return;
    }

    // Check if item has children to measure
    DomNode* child = item->first_child;
    if (!child) {
        // No children - use explicit dimensions if specified, otherwise 0
        // CRITICAL FIX: Don't use item->width/height which may have been pre-set
        // to container size. Use given_width/given_height which are the CSS-specified values.
        if (item->blk && item->blk->given_width > 0) {
            min_width = max_width = item->blk->given_width;
        } else {
            min_width = max_width = 0;  // No explicit width, intrinsic is 0
        }
        if (item->blk && item->blk->given_height > 0) {
            min_height = max_height = item->blk->given_height;
        } else {
            min_height = max_height = 0;  // No explicit height, intrinsic is 0
        }
        log_debug("Empty element intrinsic sizes: width=%.1f, height=%.1f (explicit)", min_width, min_height);
    } else if (child->is_text() && !child->next_sibling) {
        // Simple text node - use unified intrinsic sizing API if available
        const char* text = (const char*)child->text_data();
        if (text) {
            size_t len = strlen(text);
            LayoutContext* lycon = flex_layout ? flex_layout->lycon : nullptr;
            if (lycon) {
                // Use accurate FreeType-based measurement
                TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, len);
                min_width = widths.min_content;
                max_width = widths.max_content;
                min_height = max_height = (lycon->font.style && lycon->font.style->font_size > 0) ?
                    lycon->font.style->font_size : 20.0f;
            } else {
                // Fallback: rough estimation when no layout context
                max_width = len * 10.0f;
                float current_word = 0.0f;
                min_width = 0.0f;
                for (size_t i = 0; i < len; i++) {
                    if (is_space(text[i])) {
                        min_width = fmaxf(min_width, current_word * 10.0f);
                        current_word = 0.0f;
                    } else {
                        current_word += 1.0f;
                    }
                }
                min_width = fmaxf(min_width, current_word * 10.0f);
                min_height = max_height = 20.0f;
            }
        }
    } else {
        // Complex content - check measurement cache first
        // The measurement cache is populated during the first pass of multi-pass flex layout
        log_debug("calculate_item_intrinsic_sizes: checking cache for item %p", item);
        MeasurementCacheEntry* cached = get_from_measurement_cache((DomNode*)item);
        log_debug("calculate_item_intrinsic_sizes: cache lookup returned %p", cached);
        if (cached) {
            log_debug("calculate_item_intrinsic_sizes: cached entry - measured_width=%d, measured_height=%d",
                      cached->measured_width, cached->measured_height);
        }

        // CRITICAL FIX: For items without explicit dimensions, the cached values may be
        // based on container size, not intrinsic size. In such cases, we should NOT use
        // the cache for the axis that doesn't have an explicit size.
        bool has_explicit_width = (item->blk && item->blk->given_width > 0);
        bool has_explicit_height = (item->blk && item->blk->given_height > 0);

        // Check if this item is a row flex container
        // For row flex containers, the cached height from measure_flex_child_content might be incorrect
        // because it sums child heights instead of taking the max
        bool is_row_flex_container = false;
        if (item->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* block_view = (ViewBlock*)item;
            if (block_view->embed && block_view->embed->flex) {
                int dir = block_view->embed->flex->direction;
                is_row_flex_container = (dir == CSS_VALUE_ROW || dir == CSS_VALUE_ROW_REVERSE);
                log_debug("calculate_item_intrinsic_sizes: is_row_flex_container=%d (direction=%d)",
                          is_row_flex_container, dir);
            }
        }

        // First, try to calculate intrinsic sizes from children
        // This handles both width and height by traversing child elements
        float max_child_width = 0.0f;
        float total_child_width = 0.0f;  // For row flex containers: sum of child widths
        float total_child_height = 0.0f;
        int child_count = 0;  // Count children for gap calculation

        {
            DomNode* c = child;
            LayoutContext* lycon = flex_layout ? flex_layout->lycon : nullptr;

            while (c) {
                if (c->is_text()) {
                    const char* text = (const char*)c->text_data();
                    if (text) {
                        bool is_whitespace_only = true;
                        for (const char* p = text; *p; p++) {
                            if (!is_space((unsigned char)*p)) {
                                is_whitespace_only = false;
                                break;
                            }
                        }

                        if (!is_whitespace_only) {
                            int text_len = strlen(text);
                            float text_width, text_height;
                            if (lycon) {
                                TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, text_len);
                                text_width = widths.max_content;
                                text_height = (lycon->font.style && lycon->font.style->font_size > 0) ?
                                    lycon->font.style->font_size : 20.0f;
                            } else {
                                text_width = text_len * 10.0f;
                                text_height = 20.0f;
                            }
                            max_child_width = max(max_child_width, text_width);
                            total_child_height += text_height;
                        }
                    }
                } else if (c->is_element()) {
                    ViewElement* child_view = (ViewElement*)c->as_element();
                    if (child_view) {
                        // First check View-level resolved dimensions
                        bool child_has_explicit_width = (child_view->blk && child_view->blk->given_width > 0);
                        bool child_has_explicit_height = (child_view->blk && child_view->blk->given_height > 0);

                        // If View-level dimensions aren't set yet, check DOM CSS
                        // This handles the case where intrinsic sizing runs before CSS resolution
                        float dom_css_width = -1;
                        float dom_css_height = -1;
                        LayoutContext* lycon = flex_layout ? flex_layout->lycon : nullptr;

                        if (!child_has_explicit_width && lycon) {
                            dom_css_width = get_explicit_css_width(lycon, child_view);
                            if (dom_css_width > 0) {
                                child_has_explicit_width = true;
                                log_debug("Got explicit CSS width from DOM: %.1f", dom_css_width);
                            }
                        }

                        if (!child_has_explicit_height && lycon) {
                            dom_css_height = get_explicit_css_height(lycon, child_view);
                            if (dom_css_height > 0) {
                                child_has_explicit_height = true;
                                log_debug("Got explicit CSS height from DOM: %.1f", dom_css_height);
                            }
                        }

                        float child_width = 0.0f;
                        float child_height = 0.0f;

                        // Get child width - explicit (from View or DOM) or intrinsic
                        if (child_has_explicit_width) {
                            if (child_view->blk && child_view->blk->given_width > 0) {
                                child_width = child_view->blk->given_width;
                            } else if (dom_css_width > 0) {
                                child_width = dom_css_width;
                            }
                        } else if (child_view->fi) {
                            // Child has fi - use cached intrinsic or calculate
                            if (!child_view->fi->has_intrinsic_width) {
                                calculate_item_intrinsic_sizes(child_view, flex_layout);
                            }
                            if (child_view->fi->has_intrinsic_width) {
                                child_width = child_view->fi->intrinsic_width.max_content;
                            }
                        } else if (lycon) {
                            // Child doesn't have fi yet - use measure_element_intrinsic_widths
                            // This handles the case where intrinsic sizing runs before fi is initialized
                            IntrinsicSizes child_sizes = measure_element_intrinsic_widths(lycon, (DomElement*)child_view);
                            child_width = child_sizes.max_content;
                            log_debug("Used measure_element_intrinsic_widths for child: width=%.1f", child_width);
                        }

                        // Get child height - explicit (from View or DOM) or intrinsic
                        if (child_has_explicit_height) {
                            if (child_view->blk && child_view->blk->given_height > 0) {
                                child_height = child_view->blk->given_height;
                            } else if (dom_css_height > 0) {
                                child_height = dom_css_height;
                            }
                        } else if (child_view->fi) {
                            // Child has fi - use cached intrinsic or calculate recursively
                            if (!child_view->fi->has_intrinsic_height) {
                                calculate_item_intrinsic_sizes(child_view, flex_layout);
                            }
                            if (child_view->fi->has_intrinsic_height) {
                                child_height = child_view->fi->intrinsic_height.max_content;
                            }
                        }

                        // CRITICAL: If child is a flex container without proper height,
                        // recursively measure its content-based height from the DOM tree
                        if (child_height == 0.0f && !child_has_explicit_height) {
                            // Check if child is a flex container by resolving display from DOM
                            DisplayValue child_display = resolve_display_value((void*)c);
                            log_debug("Child height is 0, checking if flex container - display.inner=%d",
                                      child_display.inner);
                            if (child_display.inner == CSS_VALUE_FLEX) {
                                // Use recursive DOM-based measurement
                                child_height = measure_content_height_recursive(c, lycon);
                                log_debug("Nested flex child height from recursive measurement: %.1f", child_height);
                            }
                        }
                        // Note: For height, we may not have a good fallback - leave as 0

                        // For width: row flex sums widths, column flex takes max
                        if (is_row_flex_container) {
                            total_child_width += child_width;
                        } else {
                            max_child_width = max(max_child_width, child_width);
                        }
                        child_count++;

                        // For height, column flex containers sum heights, row flex takes max
                        if (is_row_flex_container) {
                            total_child_height = max(total_child_height, child_height);
                        } else {
                            total_child_height += child_height;
                        }

                        log_debug("Child element: width=%.1f, height=%.1f (explicit=%d/%d)",
                                  child_width, child_height, child_has_explicit_width, child_has_explicit_height);
                    }
                }
                c = c->next_sibling;
            }

            // For row flex containers, add gaps to total width
            if (is_row_flex_container && child_count > 1) {
                // Get gap from the flex container properties
                float gap = 0;
                if (item->view_type == RDT_VIEW_BLOCK) {
                    ViewBlock* block_view = (ViewBlock*)item;
                    if (block_view->embed && block_view->embed->flex) {
                        gap = block_view->embed->flex->column_gap;
                    }
                }
                total_child_width += gap * (child_count - 1);
                log_debug("Row flex: added %d gaps of %.1f = %.1f total gap pixels",
                          child_count - 1, gap, gap * (child_count - 1));
            }

            log_debug("Traversed children: max_width=%.1f, total_width=%.1f, total_height=%.1f, is_row_flex=%d",
                      max_child_width, total_child_width, total_child_height, is_row_flex_container);
        }

        // Use cached width if available and item has explicit width, otherwise use calculated
        if (cached && cached->measured_width > 0 && has_explicit_width) {
            min_width = cached->measured_width;
            max_width = cached->measured_width;
            log_debug("Using cached width for complex content (has explicit width): width=%.1f", min_width);
        } else if (is_row_flex_container && total_child_width > 0.0f) {
            // Row flex container: use sum of child widths + gaps
            min_width = total_child_width;
            max_width = total_child_width;
            log_debug("Using sum of child widths for row flex container: width=%.1f", min_width);
        } else if (max_child_width > 0.0f) {
            min_width = max_child_width;
            max_width = max_child_width;
            log_debug("Using calculated width from children: width=%.1f", min_width);
        } else {
            min_width = max_width = 0;
            log_debug("No width from children or cache, using 0");
        }

        // Use cached height if available and item has explicit height, otherwise use calculated
        if (cached && cached->measured_height > 0 && has_explicit_height) {
            min_height = cached->measured_height;
            max_height = cached->measured_height;
            log_debug("Using cached height for complex content (has explicit height): height=%.1f", min_height);
        } else if (total_child_height > 0.0f) {
            min_height = total_child_height;
            max_height = total_child_height;
            log_debug("Using calculated height from children: height=%.1f", min_height);
        } else if (cached && cached->measured_height > 0) {
            // Fallback to cache without explicit height requirement
            min_height = cached->measured_height;
            max_height = cached->measured_height;
            log_debug("Using cached height for complex content: height=%.1f", min_height);
        } else {
            min_height = max_height = 0;
            log_debug("No height from children or cache, using 0");
        }
    }

    // Store results
    item->fi->intrinsic_width.min_content = min_width;
    item->fi->intrinsic_width.max_content = max_width;
    item->fi->intrinsic_height.min_content = min_height;
    item->fi->intrinsic_height.max_content = max_height;
    item->fi->has_intrinsic_width = 1;
    item->fi->has_intrinsic_height = 1;

    log_debug("Intrinsic sizes calculated: width=[%d, %d], height=[%d, %d]",
              min_width, max_width, min_height, max_height);

    // Restore font after measurement
    if (font_changed) {
        lycon->font = saved_font;
    }
}

// Measure block intrinsic sizes (full implementation)
void measure_block_intrinsic_sizes(LayoutContext* lycon, ViewBlock* block,
                                   int* min_width, int* max_width,
                                   int* min_height, int* max_height) {
    if (!block) {
        *min_width = *max_width = *min_height = *max_height = 0;
        return;
    }

    // Save current layout context
    LayoutContext saved = *lycon;

    // Mark as measurement mode
    lycon->is_measuring = true;

    // Phase 1: Max-content measurement (no width constraint)
    lycon->block.content_width = FLT_MAX;
    *max_width = layout_block_measure_mode(lycon, block, false);

    // Phase 2: Min-content measurement (maximum wrapping)
    lycon->block.content_width = 0;
    *min_width = layout_block_measure_mode(lycon, block, true);

    // Height measurement would require laying out with specific width
    // For now, estimate based on content
    *min_height = *max_height = (int)lycon->block.advance_y;

    // Restore context
    *lycon = saved;

    log_debug("Block intrinsic sizes: width=[%d, %d], height=[%d, %d]",
              *min_width, *max_width, *min_height, *max_height);
}

// Layout block in measurement mode (without creating permanent views)
// REFACTORED: Now uses unified intrinsic sizing API
float layout_block_measure_mode(LayoutContext* lycon, ViewBlock* block, bool constrain_width) {
    if (!block) return 0;

    // In measurement mode, traverse children and measure their contributions
    // using the unified intrinsic sizing API

    float max_width = 0;
    DomNode* child = block->first_child;

    while (child) {
        if (child->is_text()) {
            const char* text = (const char*)child->text_data();
            if (text && lycon) {
                size_t len = strlen(text);
                TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, len);
                if (constrain_width) {
                    // Min-content: use longest word width
                    max_width = fmaxf(max_width, widths.min_content);
                } else {
                    // Max-content: use full text width
                    max_width = fmaxf(max_width, widths.max_content);
                }
            } else if (text) {
                // Fallback when no layout context
                size_t len = strlen(text);
                max_width = fmaxf(max_width, len * 10.0f);
            }
        } else if (child->is_element()) {
            // For element children, use unified API if available
            if (lycon && child) {
                if (constrain_width) {
                    max_width = fmaxf(max_width, calculate_min_content_width(lycon, child));
                } else {
                    max_width = fmaxf(max_width, calculate_max_content_width(lycon, child));
                }
            } else {
                max_width = fmaxf(max_width, 100.0f);  // Fallback placeholder
            }
        }
        child = child->next_sibling;
    }

    return max_width;
}
