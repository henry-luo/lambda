#pragma once

#include "layout.hpp"
#include "layout_flex.hpp"

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
void measure_all_flex_children_content(LayoutContext* lycon, ViewBlock* flex_container);
bool requires_content_measurement(ViewBlock* flex_container);

// Helper functions for measurement
ViewBlock* create_temporary_view_for_measurement(LayoutContext* lycon, DomNode* child);
void measure_text_content(LayoutContext* lycon, DomNode* text_node, int* width, int* height);
int estimate_text_width(LayoutContext* lycon, const unsigned char* text, size_t length);
void cleanup_temporary_view(ViewBlock* temp_view);
DisplayValue resolve_display_value(void* child);

// Intrinsic size calculation
void calculate_intrinsic_sizes(ViewBlock* view, LayoutContext* lycon);
void calculate_item_intrinsic_sizes(ViewGroup* item, struct FlexContainerLayout* flex_layout);

// Enhanced measurement functions for accurate intrinsic sizing
void measure_text_content_accurate(LayoutContext* lycon, DomNode* text_node,
                                   int* min_width, int* max_width, int* height);
void measure_block_intrinsic_sizes(LayoutContext* lycon, ViewBlock* block,
                                   int* min_width, int* max_width,
                                   int* min_height, int* max_height);
int layout_block_measure_mode(LayoutContext* lycon, ViewBlock* block, bool constrain_width);
void measure_text_run(LayoutContext* lycon, const char* text, size_t length,
                     int* min_width, int* max_width, int* height);

// Measurement cache functions
void store_measured_sizes(DomNode* node, ViewBlock* measured_view, LayoutContext* lycon);
void store_in_measurement_cache(DomNode* node, int width, int height,
                               int content_width, int content_height);
MeasurementCacheEntry* get_from_measurement_cache(DomNode* node);
void clear_measurement_cache();

// Enhanced layout functions that use measured sizes
void layout_flow_node_for_flex(LayoutContext* lycon, DomNode* node);
ViewBlock* create_flex_item_view(LayoutContext* lycon, DomNode* node);
void create_flex_item_view_only(LayoutContext* lycon, DomNode* node);
void create_lightweight_flex_item_view(LayoutContext* lycon, DomNode* node);
void setup_flex_item_properties(LayoutContext* lycon, ViewBlock* view, DomNode* node);
void layout_block_with_measured_size(LayoutContext* lycon, DomNode* node,
                                    DisplayValue display, MeasurementCacheEntry* cached);
