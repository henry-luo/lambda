#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"

#include "../lib/log.h"

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
        // Measure element content
        temp_view = create_temporary_view_for_measurement(&measure_context, child);
        if (temp_view) {
            // For now, use simplified measurement instead of full layout
            // TODO: Implement proper content layout measurement
            temp_view->width = 100;  // Default measured width
            temp_view->height = 50;  // Default measured height
            temp_view->content_width = 80;
            temp_view->content_height = 30;

            // Extract measurement results
            measured_width = temp_view->width;
            measured_height = temp_view->height;
            content_width = temp_view->content_width;
            content_height = temp_view->content_height;

            log_debug("Measured element %s: %dx%d (content: %dx%d)",
                      child->node_name(), measured_width, measured_height,
                      content_width, content_height);

            // Cleanup temporary view
            cleanup_temporary_view(temp_view);
        }
    }

    // Store measurement results
    store_in_measurement_cache(child, measured_width, measured_height,
                              content_width, content_height);

    // Restore original context
    *lycon = saved_context;

    log_debug("Content measurement complete for %s", child->node_name());
}

// Helper functions for measurement
ViewBlock* create_temporary_view_for_measurement(LayoutContext* lycon, DomNode* child) {
    // Create truly temporary ViewBlock for measurement without affecting main layout
    log_debug("*** TEMP_VIEW TRACE: Creating temporary view for measurement of %s", child->node_name());

    // Save current layout context to avoid affecting main layout
    ViewGroup* saved_parent = lycon->parent;
    View* saved_prev_view = lycon->prev_view;

    // Temporarily disconnect from layout hierarchy
    lycon->parent = nullptr;
    lycon->prev_view = nullptr;

    // Create View that won't be linked to layout hierarchy
    ViewBlock* temp_view = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, child);

    // Restore layout context immediately
    lycon->parent = saved_parent;
    lycon->prev_view = saved_prev_view;

    if (temp_view) {
        // Initialize with unconstrained dimensions for intrinsic measurement
        temp_view->width = 0;
        temp_view->height = 0;
        // CRITICAL FIX: Do NOT set next_sibling to nullptr!
        // In the merged DOM/View tree, temp_view IS the actual DOM node,
        // so modifying next_sibling breaks the DOM sibling chain!
        // temp_view->next_sibling = nullptr;  // REMOVED - breaks DOM chain

        // CRITICAL: Ensure this View is not linked to any parent
        temp_view->parent = nullptr;
        log_debug("*** TEMP_VIEW TRACE: Created isolated temp view %p for %s", temp_view, child->node_name());
    }
    return temp_view;
}

void measure_text_content(LayoutContext* lycon, DomNode* text_node, int* width, int* height) {
    // Measure text content dimensions
    // This would involve font metrics and text measurement

    const char* text_data = (const char*)text_node->text_data();
    size_t text_length = text_data ? strlen(text_data) : 0;

    if (text_data && text_length > 0) {
        // Calculate text dimensions based on current font
        int text_width = estimate_text_width(lycon, (const unsigned char*)text_data, text_length);
        int text_height = lycon->font.style->font_size;

        *width = text_width;
        *height = text_height;

        log_debug("Measured text: %dx%d (\"%.*s\")",
                  text_width, text_height, (int)min(text_length, 20), text_data);
    } else {
        *width = 0;
        *height = 0;
    }
}

int estimate_text_width(LayoutContext* lycon, const unsigned char* text, size_t length) {
    // Simple text width estimation
    // In a full implementation, this would use proper font metrics
    (void)text; // Suppress unused parameter warning

    float avg_char_width = lycon->font.style->font_size * 0.6f; // Rough estimate
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

    log_debug("TRACE: About to call create_lightweight_flex_item_view for %s", node->node_name());
    // Create lightweight View for flex item element only (no child processing)
    create_lightweight_flex_item_view(lycon, node);
    log_debug("TRACE: Completed create_lightweight_flex_item_view for %s", node->node_name());

    // Apply measured sizes if available
    MeasurementCacheEntry* cached = get_from_measurement_cache(node);
    log_debug("DEBUG: cached = %p", cached);
    log_debug("DEBUG: lycon->prev_view = %p", lycon->prev_view);
    if (lycon->prev_view) {
        log_debug("DEBUG: lycon->prev_view->type = %d", lycon->prev_view->view_type);
    }

    if (cached && lycon->prev_view && lycon->prev_view->view_type == RDT_VIEW_BLOCK) {
        ViewBlock* view = (ViewBlock*)lycon->prev_view;
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
        log_debug("DEBUG: Failed measurement application - cached=%p, prev_view=%p", cached, lycon->prev_view);
    }
}

// Create a ViewBlock for a flex item without full content layout
ViewBlock* create_flex_item_view(LayoutContext* lycon, DomNode* node) {
    if (!node || !node->is_element()) return nullptr;

    // Create ViewBlock for the flex item
    ViewBlock* view = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, node);
    if (!view) return nullptr;

    // Initialize basic properties
    fprintf(stderr, "[DOM DEBUG] create_flex_item_view - redundant assignment view %p->node = %p (was already set by alloc_view)\n",
            (void*)view, (void*)node);
    view->parent = lycon->parent;
    view->view_type = RDT_VIEW_BLOCK;

    // Update layout context
    lycon->prev_view = (View*)view;
    return view;
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

// Create View for flex item element only (no children processing)
void create_flex_item_view_only(LayoutContext* lycon, DomNode* node) {
    if (!node || !node->is_element()) return;

    log_debug("Creating View for flex item element only: %s", node->node_name());

    // Create ViewBlock directly using the existing create_flex_item_view function
    // but ensure we don't process children
    ViewBlock* view = create_flex_item_view(lycon, node);
    if (!view) {
        log_error("Failed to create View for flex item: %s", node->node_name());
        return;
    }

    // Set up basic properties
    setup_flex_item_properties(lycon, view, node);

    log_debug("Created View for flex item element: %s", node->node_name());
}

// Create lightweight View for flex item element only (no child processing)
void create_lightweight_flex_item_view(LayoutContext* lycon, DomNode* node) {
    if (!node || !node->is_element()) return;

    log_debug("*** TRACE: create_lightweight_flex_item_view ENTRY for %s (node=%p)", node->node_name(), node);
    log_debug("*** TRACE: Current prev_view before creation: %p", lycon->prev_view);

    // Get display properties for the element
    DisplayValue display = resolve_display_value(node);

    // Create ViewBlock directly (similar to layout_block but without child processing)
    ViewBlock* block = (ViewBlock*)alloc_view(lycon,
        display.outer == CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK :
        display.outer == CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM :
        display.inner == CSS_VALUE_TABLE ? RDT_VIEW_TABLE : RDT_VIEW_BLOCK,
        node);

    if (!block) {
        log_error("Failed to allocate View for flex item: %s", node->node_name());
        return;
    }

    block->display = display;

    // Set up basic CSS properties (minimal setup for flex items)
    dom_node_resolve_style(node, lycon);

    // CRITICAL FIX: Ensure flex item properties are allocated
    // Even if no flex CSS properties are specified, we need fi for the flex algorithm
    alloc_flex_item_prop(lycon, block);

    // Initialize dimensions (will be set by flex algorithm)
    block->width = 0;  block->height = 0;
    block->content_width = 0;  block->content_height = 0;

    // CRITICAL FIX: Set prev_view so cached measurements can be applied
    lycon->prev_view = (View*)block;
    log_debug("create_lightweight_flex_item_view EXIT for %s (node=%p, created_view=%p)", node->node_name(), node, block);
}
