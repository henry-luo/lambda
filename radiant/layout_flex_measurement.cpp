#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "lambda_css_resolve.h"

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

    // Check if already measured
    MeasurementCacheEntry* cached = get_from_measurement_cache(child);
    if (cached) {
        log_debug("Using cached measurement for %s", child->name());
        return;
    }

    // Save current layout context
    LayoutContext saved_context = *lycon;

    // Create temporary measurement context
    LayoutContext measure_context = *lycon;
    measure_context.block.width = -1;  // Unconstrained width for measurement
    measure_context.block.height = -1; // Unconstrained height for measurement
    measure_context.block.advance_y = 0;
    measure_context.block.max_width = 0;

    // Set up measurement environment
    measure_context.line.left = 0;
    measure_context.line.right = 10000; // Large but finite for measurement
    measure_context.line.vertical_align = LXB_CSS_VALUE_BASELINE;
    line_init(&measure_context);

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
                      child->name(), measured_width, measured_height,
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

    log_debug("Content measurement complete for %s", child->name());
}

// Helper functions for measurement

ViewBlock* create_temporary_view_for_measurement(LayoutContext* lycon, DomNode* child) {
    // Create truly temporary ViewBlock for measurement without affecting main layout
    log_debug("*** TEMP_VIEW TRACE: Creating temporary view for measurement of %s", child->name());

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

        // CRITICAL: Ensure this View is not linked to any parent
        temp_view->parent = nullptr;
        temp_view->next = nullptr;

        log_debug("*** TEMP_VIEW TRACE: Created isolated temp view %p for %s", temp_view, child->name());
    }

    return temp_view;
}

void measure_text_content(LayoutContext* lycon, DomNode* text_node, int* width, int* height) {
    // Measure text content dimensions
    // This would involve font metrics and text measurement

    const unsigned char* text_data = text_node->text_data();
    size_t text_length = text_data ? strlen((const char*)text_data) : 0;

    if (text_data && text_length > 0) {
        // Calculate text dimensions based on current font
        int text_width = estimate_text_width(lycon, text_data, text_length);
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

DisplayValue resolve_display_value(DomNode* child) {
    // Resolve display value for a DOM node
    DisplayValue display = {LXB_CSS_VALUE_BLOCK, LXB_CSS_VALUE_FLOW};

    if (child && child->is_element()) {
        if (child->type == LEXBOR_ELEMENT) {
            // Lexbor element - use existing resolve_display
            lxb_html_element_t* elmt = child->as_element();
            if (elmt) {
                display = resolve_display(elmt);
            }
        } else if (child->type == MARK_ELEMENT) {
            // Lambda CSS element - resolve display from CSS if available
            const char* tag_name = child->name();

            // First, try to get display from CSS
            DomElement* dom_elem = (DomElement*)child->style;
            if (dom_elem && dom_elem->specified_style) {
                StyleTree* style_tree = dom_elem->specified_style;
                if (style_tree->tree) {
                    // Look for display property (ID=1) in the AVL tree
                    AvlNode* node = avl_tree_search(style_tree->tree, CSS_PROPERTY_DISPLAY);
                    if (node) {
                        StyleNode* style_node = (StyleNode*)node->declaration;
                        if (style_node && style_node->winning_decl) {
                            CssDeclaration* decl = style_node->winning_decl;
                            if (decl->value && decl->value->type == CSS_VALUE_KEYWORD) {
                                const char* keyword = decl->value->data.keyword;

                                // Map keyword to display values
                                if (strcmp(keyword, "flex") == 0) {
                                    display.outer = LXB_CSS_VALUE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_FLEX;
                                    return display;
                                } else if (strcmp(keyword, "inline-flex") == 0) {
                                    display.outer = LXB_CSS_VALUE_INLINE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_FLEX;
                                    return display;
                                } else if (strcmp(keyword, "grid") == 0) {
                                    display.outer = LXB_CSS_VALUE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_GRID;
                                    return display;
                                } else if (strcmp(keyword, "inline-grid") == 0) {
                                    display.outer = LXB_CSS_VALUE_INLINE;
                                    display.inner = LXB_CSS_VALUE_GRID;
                                    return display;
                                } else if (strcmp(keyword, "block") == 0) {
                                    display.outer = LXB_CSS_VALUE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_FLOW;
                                    return display;
                                } else if (strcmp(keyword, "inline") == 0) {
                                    display.outer = LXB_CSS_VALUE_INLINE;
                                    display.inner = LXB_CSS_VALUE_FLOW;
                                    return display;
                                } else if (strcmp(keyword, "inline-block") == 0) {
                                    display.outer = LXB_CSS_VALUE_INLINE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_FLOW;
                                    return display;
                                } else if (strcmp(keyword, "none") == 0) {
                                    display.outer = LXB_CSS_VALUE_NONE;
                                    display.inner = LXB_CSS_VALUE_NONE;
                                    return display;
                                } else if (strcmp(keyword, "table") == 0) {
                                    display.outer = LXB_CSS_VALUE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_TABLE;
                                    return display;
                                } else if (strcmp(keyword, "inline-table") == 0) {
                                    display.outer = LXB_CSS_VALUE_INLINE;
                                    display.inner = LXB_CSS_VALUE_TABLE;
                                    return display;
                                } else if (strcmp(keyword, "table-row") == 0) {
                                    display.outer = LXB_CSS_VALUE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_TABLE_ROW;
                                    return display;
                                } else if (strcmp(keyword, "table-cell") == 0) {
                                    display.outer = LXB_CSS_VALUE_TABLE_CELL;
                                    display.inner = LXB_CSS_VALUE_TABLE_CELL;
                                    return display;
                                } else if (strcmp(keyword, "table-row-group") == 0) {
                                    display.outer = LXB_CSS_VALUE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_TABLE_ROW_GROUP;
                                    return display;
                                } else if (strcmp(keyword, "table-header-group") == 0) {
                                    display.outer = LXB_CSS_VALUE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_TABLE_HEADER_GROUP;
                                    return display;
                                } else if (strcmp(keyword, "table-footer-group") == 0) {
                                    display.outer = LXB_CSS_VALUE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_TABLE_FOOTER_GROUP;
                                    return display;
                                } else if (strcmp(keyword, "table-column") == 0) {
                                    display.outer = LXB_CSS_VALUE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_TABLE_COLUMN;
                                    return display;
                                } else if (strcmp(keyword, "table-column-group") == 0) {
                                    display.outer = LXB_CSS_VALUE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_TABLE_COLUMN_GROUP;
                                    return display;
                                } else if (strcmp(keyword, "table-caption") == 0) {
                                    display.outer = LXB_CSS_VALUE_BLOCK;
                                    display.inner = LXB_CSS_VALUE_TABLE_CAPTION;
                                    return display;
                                }
                            }
                        }
                    }
                }
            }

            // Fall back to default display values based on tag name
            if (strcmp(tag_name, "body") == 0 || strcmp(tag_name, "h1") == 0 ||
                strcmp(tag_name, "h2") == 0 || strcmp(tag_name, "h3") == 0 ||
                strcmp(tag_name, "h4") == 0 || strcmp(tag_name, "h5") == 0 ||
                strcmp(tag_name, "h6") == 0 || strcmp(tag_name, "p") == 0 ||
                strcmp(tag_name, "div") == 0 || strcmp(tag_name, "center") == 0 ||
                strcmp(tag_name, "ul") == 0 || strcmp(tag_name, "ol") == 0 ||
                strcmp(tag_name, "header") == 0 || strcmp(tag_name, "main") == 0 ||
                strcmp(tag_name, "section") == 0 || strcmp(tag_name, "footer") == 0 ||
                strcmp(tag_name, "article") == 0 || strcmp(tag_name, "aside") == 0 ||
                strcmp(tag_name, "nav") == 0 || strcmp(tag_name, "address") == 0 ||
                strcmp(tag_name, "blockquote") == 0 || strcmp(tag_name, "details") == 0 ||
                strcmp(tag_name, "dialog") == 0 || strcmp(tag_name, "figure") == 0 ||
                strcmp(tag_name, "menu") == 0) {
                display.outer = LXB_CSS_VALUE_BLOCK;
                display.inner = LXB_CSS_VALUE_FLOW;
            } else if (strcmp(tag_name, "li") == 0 || strcmp(tag_name, "summary") == 0) {
                display.outer = LXB_CSS_VALUE_LIST_ITEM;
                display.inner = LXB_CSS_VALUE_FLOW;
            } else if (strcmp(tag_name, "img") == 0 || strcmp(tag_name, "video") == 0 ||
                       strcmp(tag_name, "input") == 0 || strcmp(tag_name, "select") == 0 ||
                       strcmp(tag_name, "textarea") == 0 || strcmp(tag_name, "button") == 0 ||
                       strcmp(tag_name, "iframe") == 0) {
                display.outer = LXB_CSS_VALUE_INLINE_BLOCK;
                display.inner = RDT_DISPLAY_REPLACED;
            } else if (strcmp(tag_name, "hr") == 0) {
                display.outer = LXB_CSS_VALUE_BLOCK;
                display.inner = RDT_DISPLAY_REPLACED;
            } else if (strcmp(tag_name, "script") == 0 || strcmp(tag_name, "style") == 0 ||
                       strcmp(tag_name, "svg") == 0) {
                display.outer = LXB_CSS_VALUE_NONE;
                display.inner = LXB_CSS_VALUE_NONE;
            } else if (strcmp(tag_name, "table") == 0) {
                display.outer = LXB_CSS_VALUE_BLOCK;
                display.inner = LXB_CSS_VALUE_TABLE;
            } else if (strcmp(tag_name, "caption") == 0) {
                display.outer = LXB_CSS_VALUE_BLOCK;
                display.inner = LXB_CSS_VALUE_FLOW;
            } else if (strcmp(tag_name, "thead") == 0 || strcmp(tag_name, "tbody") == 0 ||
                       strcmp(tag_name, "tfoot") == 0) {
                display.outer = LXB_CSS_VALUE_BLOCK;
                display.inner = LXB_CSS_VALUE_TABLE_ROW_GROUP;
            } else if (strcmp(tag_name, "tr") == 0) {
                display.outer = LXB_CSS_VALUE_BLOCK;
                display.inner = LXB_CSS_VALUE_TABLE_ROW;
            } else if (strcmp(tag_name, "th") == 0 || strcmp(tag_name, "td") == 0) {
                display.outer = LXB_CSS_VALUE_TABLE_CELL;
                display.inner = LXB_CSS_VALUE_TABLE_CELL;
            } else if (strcmp(tag_name, "colgroup") == 0) {
                display.outer = LXB_CSS_VALUE_BLOCK;
                display.inner = LXB_CSS_VALUE_TABLE_COLUMN_GROUP;
            } else if (strcmp(tag_name, "col") == 0) {
                display.outer = LXB_CSS_VALUE_BLOCK;
                display.inner = LXB_CSS_VALUE_TABLE_COLUMN;
            } else {
                // Default for unknown elements (inline)
                display.outer = LXB_CSS_VALUE_INLINE;
                display.inner = LXB_CSS_VALUE_FLOW;
            }

            // TODO: Check for CSS display property in child->style (DomElement)
            // For now, using tag-based defaults is sufficient
        }
    }

    return display;
}

bool requires_content_measurement(ViewBlock* flex_container) {
    // Determine if content measurement is needed
    // This could be based on flex properties, content types, etc.

    if (!flex_container || !flex_container->node) return false;

    // Check if any children have auto flex-basis or need intrinsic sizing
    DomNode* child = flex_container->node->first_child();
    while (child) {
        // If child has complex content or auto sizing, measurement is needed
        if (child->first_child() || child->is_text()) {
            return true;
        }
        child = child->next_sibling();
    }

    return false;
}

void measure_all_flex_children_content(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container || !flex_container->node) return;

    log_debug("Measuring all flex children content");

    DomNode* child = flex_container->node->first_child();
    int child_count = 0;
    const int MAX_CHILDREN = 100; // Safety limit

    while (child && child_count < MAX_CHILDREN) {
        measure_flex_child_content(lycon, child);
        child = child->next_sibling();
        child_count++;
    }

    log_debug("Content measurement complete for %d children", child_count);
}

// Lightweight View creation for flex items with measured sizes
void layout_flow_node_for_flex(LayoutContext* lycon, DomNode* node) {
    if (!node) return;

    log_debug("=== TRACE: layout_flow_node_for_flex ENTRY for %s (node=%p)", node->name(), node);

    // Skip text nodes - flex layout only processes element nodes
    if (!node->is_element()) {
        log_debug("TRACE: Skipping text node in flex container: %s", node->name());
        return;
    }

    log_debug("TRACE: About to call create_lightweight_flex_item_view for %s", node->name());
    // Create lightweight View for flex item element only (no child processing)
    create_lightweight_flex_item_view(lycon, node);
    log_debug("TRACE: Completed create_lightweight_flex_item_view for %s", node->name());

    // Apply measured sizes if available
    MeasurementCacheEntry* cached = get_from_measurement_cache(node);
    log_debug("DEBUG: cached = %p", cached);
    log_debug("DEBUG: lycon->prev_view = %p", lycon->prev_view);
    if (lycon->prev_view) {
        log_debug("DEBUG: lycon->prev_view->type = %d", lycon->prev_view->type);
    }

    if (cached && lycon->prev_view && lycon->prev_view->type == RDT_VIEW_BLOCK) {
        ViewBlock* view = (ViewBlock*)lycon->prev_view;
        log_debug("DEBUG: view->node = %p, node = %p", view->node, node);
        if (view->node == node) {
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
    view->node = node;
    view->parent = lycon->parent;
    view->type = RDT_VIEW_BLOCK;

    // Add to parent's child list
    if (lycon->parent) {
        if (!lycon->parent->child) {
            lycon->parent->child = (View*)view;
        } else {
            View* last_child = lycon->parent->child;
            while (last_child->next) {
                last_child = last_child->next;
            }
            last_child->next = (View*)view;
        }
    }

    // Update layout context
    lycon->prev_view = (View*)view;

    return view;
}

// Set up basic flex item properties without content layout
void setup_flex_item_properties(LayoutContext* lycon, ViewBlock* view, DomNode* node) {
    (void)lycon; // Suppress unused parameter warning
    if (!view || !node) return;

    // Resolve CSS properties for the flex item
    lxb_html_element_t* element = node->as_element();
    if (!element) return;

    // Get display properties
    view->display = resolve_display(element);

    // Initialize position and sizing
    view->x = 0;
    view->y = 0;

    // Note: flex-specific properties (flex_grow, flex_shrink, flex_basis) and
    // box model properties (margin, padding, border) will be resolved by the flex algorithm
    // during CSS property resolution. We don't need to initialize them here.

    log_debug("Set up basic properties for flex item: %s", node->name());
}

// Create View for flex item element only (no children processing)
void create_flex_item_view_only(LayoutContext* lycon, DomNode* node) {
    if (!node || !node->is_element()) return;

    log_debug("Creating View for flex item element only: %s", node->name());

    // Create ViewBlock directly using the existing create_flex_item_view function
    // but ensure we don't process children
    ViewBlock* view = create_flex_item_view(lycon, node);
    if (!view) {
        log_error("Failed to create View for flex item: %s", node->name());
        return;
    }

    // Set up basic properties
    setup_flex_item_properties(lycon, view, node);

    log_debug("Created View for flex item element: %s", node->name());
}

// Create lightweight View for flex item element only (no child processing)
void create_lightweight_flex_item_view(LayoutContext* lycon, DomNode* node) {
    if (!node || !node->is_element()) return;

    log_debug("*** TRACE: create_lightweight_flex_item_view ENTRY for %s (node=%p)", node->name(), node);
    log_debug("*** TRACE: Current prev_view before creation: %p", lycon->prev_view);

    // Get display properties for the element
    lxb_html_element_t* elmt = node->as_element();
    DisplayValue display = resolve_display(elmt);

    // Create ViewBlock directly (similar to layout_block but without child processing)
    ViewBlock* block = (ViewBlock*)alloc_view(lycon,
        display.outer == LXB_CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK :
        display.outer == LXB_CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM :
        display.inner == LXB_CSS_VALUE_TABLE ? RDT_VIEW_TABLE : RDT_VIEW_BLOCK,
        node);

    if (!block) {
        log_error("Failed to allocate View for flex item: %s", node->name());
        return;
    }

    block->display = display;

    // Set up basic CSS properties (minimal setup for flex items)
    dom_node_resolve_style(node, lycon);

    // Initialize dimensions (will be set by flex algorithm)
    block->width = 0;
    block->height = 0;
    block->content_width = 0;
    block->content_height = 0;

    // CRITICAL FIX: Set prev_view so cached measurements can be applied
    lycon->prev_view = (View*)block;

    log_debug("*** TRACE: create_lightweight_flex_item_view EXIT for %s (node=%p, created_view=%p)", node->name(), node, block);
    log_debug("*** TRACE: Set lycon->prev_view = %p", lycon->prev_view);
}
