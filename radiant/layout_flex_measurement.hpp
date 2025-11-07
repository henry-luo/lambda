#pragma once

#include "layout.hpp"
#include "layout_flex.hpp"

// Content measurement for multi-pass flex layout
// This header defines the functions for the first pass of multi-pass flex layout

// Measurement cache entry
typedef struct {
    DomNodeBase* node;
    int measured_width;
    int measured_height;
    int content_width;
    int content_height;
} MeasurementCacheEntry;

// Main measurement functions
void measure_flex_child_content(LayoutContext* lycon, DomNodeBase* child);
void measure_all_flex_children_content(LayoutContext* lycon, ViewBlock* flex_container);
bool requires_content_measurement(ViewBlock* flex_container);

// Helper functions for measurement
ViewBlock* create_temporary_view_for_measurement(LayoutContext* lycon, DomNodeBase* child);
void measure_text_content(LayoutContext* lycon, DomNodeBase* text_node, int* width, int* height);
int estimate_text_width(LayoutContext* lycon, const unsigned char* text, size_t length);
void cleanup_temporary_view(ViewBlock* temp_view);
DisplayValue resolve_display_value(void* child);

// Intrinsic size calculation
void calculate_intrinsic_sizes(ViewBlock* view, LayoutContext* lycon);

// Measurement cache functions
void store_measured_sizes(DomNodeBase* node, ViewBlock* measured_view, LayoutContext* lycon);
void store_in_measurement_cache(DomNodeBase* node, int width, int height,
                               int content_width, int content_height);
MeasurementCacheEntry* get_from_measurement_cache(DomNodeBase* node);
void clear_measurement_cache();

// Enhanced layout functions that use measured sizes
void layout_flow_node_for_flex(LayoutContext* lycon, DomNodeBase* node);
ViewBlock* create_flex_item_view(LayoutContext* lycon, DomNodeBase* node);
void create_flex_item_view_only(LayoutContext* lycon, DomNodeBase* node);
void create_lightweight_flex_item_view(LayoutContext* lycon, DomNodeBase* node);
void setup_flex_item_properties(LayoutContext* lycon, ViewBlock* view, DomNodeBase* node);
void layout_block_with_measured_size(LayoutContext* lycon, DomNodeBase* node,
                                    DisplayValue display, MeasurementCacheEntry* cached);
