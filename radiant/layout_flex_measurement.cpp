#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_content.hpp"
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
    
    log_debug("Measuring flex child content for %s", child->name());
    
    // Save current layout context
    LayoutContext saved_context = *lycon;
    
    // Create measurement context with unlimited space
    LayoutContext measure_ctx = *lycon;
    measure_ctx.block.width = INT_MAX;   // Allow natural width expansion
    measure_ctx.block.height = INT_MAX;  // Allow natural height expansion
    measure_ctx.block.advance_y = 0;
    measure_ctx.block.max_width = 0;
    
    // Set up measurement environment
    measure_ctx.line.left = 0;
    measure_ctx.line.right = INT_MAX;
    measure_ctx.line.vertical_align = LXB_CSS_VALUE_BASELINE;
    line_init(&measure_ctx);
    
    if (child->is_element()) {
        lxb_html_element_t* elmt = child->as_element();
        DisplayValue display = resolve_display(elmt);
        
        log_debug("Measuring element %s with display outer=%d, inner=%d", 
                  child->name(), display.outer, display.inner);
        
        // For now, use simplified measurement approach
        // Store basic measurements in cache
        int measured_width = 100;  // Default width
        int measured_height = 20;  // Default height
        
        // Try to get better estimates from existing properties
        if (display.outer == LXB_CSS_VALUE_BLOCK) {
            measured_width = 200;  // Blocks tend to be wider
            measured_height = 50;  // And taller
        }
        
        // Store in measurement cache
        store_in_measurement_cache(child, measured_width, measured_height, 
                                  measured_width, measured_height);
        
    } else if (child->is_text()) {
        // Measure text content - simplified
        const unsigned char* text_data = child->text_data();
        int text_len = text_data ? strlen((const char*)text_data) : 0;
        int text_width = text_len * 8;  // Approximate character width
        int text_height = 18;  // Approximate line height
        
        store_in_measurement_cache(child, text_width, text_height, 
                                  text_width, text_height);
    }
    
    // Restore original context
    *lycon = saved_context;
    
    log_debug("Completed measuring flex child content");
}


// Enhanced layout_flow_node for flex items that uses measured sizes
void layout_flow_node_for_flex(LayoutContext* lycon, DomNode* node) {
    if (!node) return;
    
    log_debug("Layout flow node for flex: %s", node->name());
    
    // For now, just use the normal layout_flow_node
    // In the future, we can enhance this to use measured sizes
    layout_flow_node(lycon, node);
    
    // Check if we have measured sizes for this node and apply them
    MeasurementCacheEntry* cached = get_from_measurement_cache(node);
    if (cached && lycon->prev_view && lycon->prev_view->type == RDT_VIEW_BLOCK) {
        ViewBlock* view = (ViewBlock*)lycon->prev_view;
        if (view->node == node) {
            log_debug("Applying cached measurements to view: %dx%d", 
                      cached->measured_width, cached->measured_height);
            
            // Use measured dimensions as hints (don't override explicit sizes)
            if (view->width <= 0) {
                view->width = cached->measured_width;
            }
            if (view->height <= 0) {
                view->height = cached->measured_height;
            }
        }
    }
}
