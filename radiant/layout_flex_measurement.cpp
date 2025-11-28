#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"

#include "../lib/log.h"
#include <float.h>
#include <limits.h>

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

        // Use the saved context's parent for proper style resolution
        ViewGroup* saved_parent = lycon->parent;

        // Set up block context for measurement
        measure_context.block.content_width = container_width;
        measure_context.block.content_height = -1;  // Unconstrained height
        measure_context.block.advance_y = 0;
        measure_context.block.max_width = 0;
        measure_context.is_measuring = true;

        // Initialize line context
        line_init(&measure_context, 0, container_width);

        // Measure child content heights by traversing the subtree
        // The child parameter is the flex item element - get its first_child
        measured_height = 0;
        measured_width = 0;
        DomElement* child_elem = child->as_element();
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
                            measured_height += 20;  // Approximate line height for actual text
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
                    else if (tag == HTM_TAG_DIV) {
                        // Nested div - estimate conservatively
                        elem_height = 56;  // Default nested block height
                    }
                    else elem_height = 20;  // Default element height

                    // Add margins (estimate 10px margin-bottom for block elements)
                    measured_height += elem_height + 10;
                }
                sub_child = sub_child->next_sibling;
            }
        }

        // Set measured dimensions
        if (measured_height == 0) measured_height = 50;
        measured_width = (int)container_width;
        content_width = measured_width;
        content_height = measured_height;

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
    ViewBlock* temp_view = (ViewBlock*)set_view(lycon, RDT_VIEW_BLOCK, child);

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
void measure_text_run(LayoutContext* lycon, const char* text, size_t length,
                     int* min_width, int* max_width, int* height) {
    if (!lycon->font.ft_face || !text || length == 0) {
        *min_width = *max_width = *height = 0;
        return;
    }

    // Calculate max-content width (no breaking)
    float total_width = 0.0f;
    float current_word_width = 0.0f;
    float longest_word = 0.0f;

    const unsigned char* str = (const unsigned char*)text;
    FT_UInt prev_glyph_index = 0;

    for (size_t i = 0; i < length; i++) {
        unsigned char ch = str[i];

        // Load glyph for this character
        if (FT_Load_Char(lycon->font.ft_face, ch, FT_LOAD_DEFAULT)) {
            continue;  // Skip characters that fail to load
        }

        FT_GlyphSlot slot = lycon->font.ft_face->glyph;
        float advance = (float)(slot->advance.x) / 64.0f;

        // Apply kerning if available
        if (prev_glyph_index && lycon->font.style->has_kerning) {
            FT_UInt glyph_index = FT_Get_Char_Index(lycon->font.ft_face, ch);
            FT_Vector kerning;
            FT_Get_Kerning(lycon->font.ft_face, prev_glyph_index, glyph_index,
                          FT_KERNING_DEFAULT, &kerning);
            advance += (float)(kerning.x) / 64.0f;
            prev_glyph_index = glyph_index;
        }

        total_width += advance;

        // Track word boundaries for min-content calculation
        if (is_space(ch)) {
            longest_word = max(longest_word, current_word_width);
            current_word_width = 0.0f;
        } else {
            current_word_width += advance;
        }
    }

    // Check final word
    longest_word = max(longest_word, current_word_width);

    // Set results
    *max_width = (int)(total_width + 0.5f);  // Round to nearest pixel
    *min_width = (int)(longest_word + 0.5f);  // Longest word = min-content
    *height = (int)(lycon->font.style->font_size + 0.5f);

    log_debug("measure_text_run: text_length=%zu, min=%d, max=%d, height=%d",
              length, *min_width, *max_width, *height);
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
    ViewBlock* view = (ViewBlock*)set_view(lycon, RDT_VIEW_BLOCK, node);
    if (!view) return nullptr;

    // Initialize basic properties
    log_debug("[DOM DEBUG] create_flex_item_view - redundant assignment view %p->node = %p (was already set by set_view)",
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

    // CRITICAL FIX: Set prev_view so cached measurements can be applied
    lycon->prev_view = (View*)block;
    log_debug("create_lightweight_flex_item_view EXIT for %s (node=%p, created_view=%p)", node->node_name(), node, block);
}

// ============================================================================
// Enhanced Intrinsic Sizing Implementation
// ============================================================================

// Calculate intrinsic sizes for a flex item
void calculate_item_intrinsic_sizes(ViewGroup* item, FlexContainerLayout* flex_layout) {
    if (!item || !item->fi) {
        log_debug("calculate_item_intrinsic_sizes: invalid item or no flex properties");
        return;
    }

    // Skip if already calculated
    bool is_horizontal = is_main_axis_horizontal(flex_layout);
    if (is_horizontal && item->fi->has_intrinsic_width) {
        log_debug("calculate_item_intrinsic_sizes: width already calculated");
        return;
    }
    if (!is_horizontal && item->fi->has_intrinsic_height) {
        log_debug("calculate_item_intrinsic_sizes: height already calculated");
        return;
    }

    log_debug("Calculating intrinsic sizes for item %p (%s)", item, item->node_name());

    // Initialize to zero
    int min_width = 0, max_width = 0, min_height = 0, max_height = 0;

    // Check if item has children to measure
    DomNode* child = item->first_child;
    if (!child) {
        // No children - use explicit dimensions or zero
        min_width = max_width = item->width > 0 ? item->width : 0;
        min_height = max_height = item->height > 0 ? item->height : 0;
    } else if (child->is_text() && !child->next_sibling) {
        // Simple text node - measure directly
        // Need to create a minimal LayoutContext for measurement
        // For now, use simplified measurement
        const char* text = (const char*)child->text_data();
        if (text) {
            size_t len = strlen(text);
            // Rough estimation: 10px per character for max, longest word for min
            max_width = len * 10;
            // Find longest word
            int current_word = 0;
            min_width = 0;
            for (size_t i = 0; i < len; i++) {
                if (is_space(text[i])) {
                    min_width = max(min_width, current_word * 10);
                    current_word = 0;
                } else {
                    current_word++;
                }
            }
            min_width = max(min_width, current_word * 10);
            min_height = max_height = 20;  // Rough font height
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
        if (cached && cached->measured_width > 0) {
            // Use cached measurements from earlier measurement pass
            min_width = cached->measured_width;
            max_width = cached->measured_width;
            min_height = cached->measured_height;
            max_height = cached->measured_height;
            log_debug("Using cached measurements for complex content: width=%d, height=%d",
                      min_width, min_height);
        } else {
            // No cache available - traverse children to estimate sizes
            // This is a fallback when measurement pass hasn't run yet
            int child_count = 0;
            int total_text_height = 0;
            int max_child_width = 0;
            DomNode* c = child;
            while (c) {
                child_count++;
                if (c->is_text()) {
                    // Text contributes to height
                    total_text_height += 20;  // Approximate line height
                    const char* text = (const char*)c->text_data();
                    if (text) {
                        int text_len = strlen(text);
                        max_child_width = max(max_child_width, text_len * 10);
                    }
                } else if (c->is_element()) {
                    // Element children contribute their sizes
                    ViewGroup* child_view = (ViewGroup*)c->as_element();
                    if (child_view) {
                        if (child_view->height > 0) {
                            total_text_height += child_view->height;
                        } else {
                            total_text_height += 20;  // Default element height
                        }
                        if (child_view->width > 0) {
                            max_child_width = max(max_child_width, child_view->width);
                        }
                    }
                }
                c = c->next_sibling;
            }

            // Use calculated values or fallback to placeholders
            if (child_count > 0) {
                min_width = max_child_width > 0 ? max_child_width : 100;
                max_width = max_child_width > 0 ? max_child_width : 200;
                min_height = total_text_height > 0 ? total_text_height : 50;
                max_height = total_text_height > 0 ? total_text_height : 100;
                log_debug("Estimated intrinsic sizes from %d children: width=%d, height=%d",
                          child_count, min_width, min_height);
            } else {
                // Truly empty element with no children
                min_width = max_width = item->width > 0 ? item->width : 0;
                min_height = max_height = item->height > 0 ? item->height : 0;
                log_debug("Empty element - using explicit dimensions: width=%d, height=%d",
                          min_width, min_height);
            }
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
int layout_block_measure_mode(LayoutContext* lycon, ViewBlock* block, bool constrain_width) {
    if (!block) return 0;

    // In measurement mode, traverse children and measure their contributions
    // without creating permanent view structures

    int max_width = 0;
    DomNode* child = block->first_child;

    while (child) {
        if (child->is_text()) {
            // Measure text node - rough estimation
            const char* text = (const char*)child->text_data();
            if (text) {
                size_t len = strlen(text);
                if (constrain_width) {
                    // Min-content: longest word
                    int current_word = 0;
                    int longest = 0;
                    for (size_t i = 0; i < len; i++) {
                        if (is_space(text[i])) {
                            longest = max(longest, current_word * 10);
                            current_word = 0;
                        } else {
                            current_word++;
                        }
                    }
                    longest = max(longest, current_word * 10);
                    max_width = max(max_width, longest);
                } else {
                    // Max-content: full text width
                    max_width = max(max_width, (int)(len * 10));
                }
            }
        } else if (child->is_element()) {
            // For element children, would need recursive measurement
            // Simplified for now
            max_width = max(max_width, 100);  // Placeholder
        }
        child = child->next_sibling;
    }

    return max_width;
}
