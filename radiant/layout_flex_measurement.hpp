#pragma once

#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_content.hpp"

// Content measurement for multi-pass flex layout
// This header defines the functions for the first pass of multi-pass flex layout

// Measurement cache entry
typedef struct {
    DomNode* node;
    int measured_width;
    int measured_height;
    int content_width;
    int content_height;
} MeasurementCacheEntry;

// Main measurement functions
void measure_flex_child_content(LayoutContext* lycon, DomNode* child);
ViewBlock* measure_block_content(LayoutContext* lycon, DomNode* node, DisplayValue display);
ViewBlock* measure_inline_content(LayoutContext* lycon, DomNode* node, DisplayValue display);
IntrinsicSizes measure_text_content(LayoutContext* lycon, DomNode* text_node);

// Intrinsic size calculation
void calculate_intrinsic_sizes(ViewBlock* view, LayoutContext* lycon);

// Measurement cache functions
void store_measured_sizes(DomNode* node, ViewBlock* measured_view, LayoutContext* lycon);
void store_in_measurement_cache(DomNode* node, int width, int height, 
                               int content_width, int content_height);
MeasurementCacheEntry* get_from_measurement_cache(DomNode* node);
void clear_measurement_cache();

// Enhanced layout functions that use measured sizes
void layout_flow_node_for_flex(LayoutContext* lycon, DomNode* node);
void layout_block_with_measured_size(LayoutContext* lycon, DomNode* node, 
                                    DisplayValue display, MeasurementCacheEntry* cached);

// Helper functions
ViewBlock* find_view_for_node(LayoutContext* lycon, DomNode* node);
