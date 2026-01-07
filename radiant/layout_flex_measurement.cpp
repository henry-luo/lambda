#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "intrinsic_sizing.hpp"
#include "form_control.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"

#include "../lib/log.h"
#include <float.h>
#include <limits.h>

// Forward declaration for get_white_space_value from layout_text.cpp
CssEnum get_white_space_value(DomNode* node);

// Helper: Check if whitespace should be collapsed according to white-space property
// Returns true for: normal, nowrap, pre-line
static inline bool should_collapse_whitespace(CssEnum ws) {
    return ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_NOWRAP ||
           ws == CSS_VALUE_PRE_LINE || ws == 0;  // 0 = undefined, treat as normal
}

// Helper: Normalize whitespace to a buffer
// Collapses consecutive whitespace to single space, trims leading/trailing
// Returns length of normalized text (0 if all whitespace)
static size_t normalize_whitespace_for_flex(const char* text, size_t length, char* buffer, size_t buffer_size) {
    if (!text || length == 0 || !buffer || buffer_size == 0) return 0;

    size_t out_pos = 0;
    bool in_whitespace = true;  // Start as if preceded by whitespace (trims leading)

    for (size_t i = 0; i < length && out_pos < buffer_size - 1; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f') {
            if (!in_whitespace) {
                buffer[out_pos++] = ' ';  // Collapse to single space
                in_whitespace = true;
            }
        } else {
            buffer[out_pos++] = (char)ch;
            in_whitespace = false;
        }
    }

    // Trim trailing whitespace
    while (out_pos > 0 && buffer[out_pos - 1] == ' ') {
        out_pos--;
    }

    buffer[out_pos] = '\0';
    return out_pos;
}

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

            // If recursive measurement returned 0, try other measurement methods
            if (child_height == 0.0f && lycon) {
                ViewElement* child_view = (ViewElement*)child->as_element();
                if (child_view) {
                    // Check for explicit height first
                    if (child_view->blk && child_view->blk->given_height > 0) {
                        child_height = child_view->blk->given_height;
                        log_debug("measure_content_height_recursive: child %s explicit height=%.1f",
                                  child->node_name(), child_height);
                    } else if (child_view->height > 0) {
                        child_height = (float)child_view->height;
                        log_debug("measure_content_height_recursive: child %s view height=%.1f",
                                  child->node_name(), child_height);
                    } else {
                        // Use calculate_max_content_height as fallback
                        child_height = calculate_max_content_height(lycon, child, 10000.0f);
                        log_debug("measure_content_height_recursive: child %s from calculate_max_content_height=%.1f",
                                  child->node_name(), child_height);
                    }
                }
            } else {
                log_debug("measure_content_height_recursive: child %s height=%.1f",
                          child->node_name(), child_height);
            }

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
                            // Text content is always inline - use MAX, not SUM
                            // Text flows horizontally and should not stack heights
                            if (text_height > max_child_height) {
                                max_child_height = text_height;
                            }
                        }
                    }
                } else if (sub_child->is_element()) {
                    // Element - check for block-level element heights
                    DomElement* elem = sub_child->as_element();

                    // Estimate element height based on type
                    uintptr_t tag = sub_child->tag();
                    int elem_height = 0;
                    bool has_explicit_height_css = false;  // Track if element has explicit CSS height

                    // Common block elements with typical heights
                    if (tag == HTM_TAG_H1) elem_height = 32;
                    else if (tag == HTM_TAG_H2) elem_height = 28;
                    else if (tag == HTM_TAG_H3) elem_height = 24;
                    else if (tag == HTM_TAG_H4) elem_height = 20;
                    else if (tag == HTM_TAG_H5 || tag == HTM_TAG_H6) elem_height = 18;
                    else if (tag == HTM_TAG_P) elem_height = 36;  // Typically 2-3 lines
                    else if (tag == HTM_TAG_IFRAME || tag == HTM_TAG_IMG ||
                             tag == HTM_TAG_VIDEO || tag == HTM_TAG_CANVAS) {
                        // Replaced elements - use explicit CSS dimensions if available
                        if (elem && elem->specified_style) {
                            CssDeclaration* height_decl = style_tree_get_declaration(
                                elem->specified_style, CSS_PROPERTY_HEIGHT);
                            if (height_decl && height_decl->value &&
                                height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                elem_height = (int)resolve_length_value(lycon, CSS_PROPERTY_HEIGHT,
                                                                         height_decl->value);
                                has_explicit_height_css = true;
                                log_debug("Replaced element %s has explicit CSS height=%d",
                                          sub_child->node_name(), elem_height);
                            }
                        }
                        // Default sizes for replaced elements without explicit height
                        if (!has_explicit_height_css) {
                            if (tag == HTM_TAG_IFRAME) elem_height = 150;  // CSS default iframe height
                            else if (tag == HTM_TAG_VIDEO) elem_height = 150;
                            else elem_height = 0;  // Other replaced elements need explicit size
                        }
                    }
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
                            // Nested flex container - check if it has actual content
                            // For empty flex containers (e.g., flex items with no children or
                            // only empty children), the height should be 0
                            // For flex containers with content, use recursive measurement or estimate

                            // First check if direct children have explicit heights
                            bool has_children_with_explicit_height = false;
                            bool has_text_content = false;
                            bool has_element_content = false;
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
                                    } else if (content->is_element()) {
                                        has_element_content = true;
                                        // Check if this nested element has explicit height
                                        DomElement* nested = content->as_element();
                                        if (nested && nested->specified_style) {
                                            CssDeclaration* h_decl = style_tree_get_declaration(
                                                nested->specified_style, CSS_PROPERTY_HEIGHT);
                                            if (h_decl && h_decl->value &&
                                                h_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                                float h = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, h_decl->value);
                                                if (h > 0) {
                                                    has_children_with_explicit_height = true;
                                                }
                                            }
                                        }
                                    }
                                    content = content->next_sibling;
                                }
                            }

                            if (!has_text_content && !has_element_content) {
                                // Empty flex container - height is 0
                                elem_height = 0;
                                has_explicit_height_css = true;  // 0 is reliable, no margin needed
                                log_debug("Nested flex container: empty, height=0");
                            } else if (has_children_with_explicit_height) {
                                // Children have explicit heights - use recursive measurement
                                float content_height = measure_content_height_recursive((DomNode*)elem, lycon);
                                if (content_height > 0) {
                                    elem_height = (int)content_height;
                                    has_explicit_height_css = true;  // Reliable measured height
                                    log_debug("Nested flex container: measured content height=%d", elem_height);
                                } else {
                                    // Fallback: use 0 and let flex layout determine height
                                    elem_height = 0;
                                    has_explicit_height_css = false;
                                    log_debug("Nested flex container: measurement returned 0, using 0");
                                }
                            } else {
                                // Has content (text or elements) but no explicit heights
                                // Use text_line_height as a reasonable estimate
                                elem_height = text_line_height;
                                has_explicit_height_css = false;
                                log_debug("Nested flex container with content: using text_line_height=%d", elem_height);
                            }
                        } else {
                            // Non-flex container div - distinguish text-only vs. nested elements
                            bool has_text_content = false;
                            bool has_block_element = false;
                            bool has_inline_element = false;
                            if (elem) {
                                DomNode* content = elem->first_child;
                                while (content) {
                                    if (content->is_element()) {
                                        // Check if this is an inline element (a, span, etc.) or block
                                        uintptr_t child_tag = content->tag();
                                        bool is_inline = (child_tag == HTM_TAG_A || child_tag == HTM_TAG_SPAN ||
                                                          child_tag == HTM_TAG_EM || child_tag == HTM_TAG_STRONG ||
                                                          child_tag == HTM_TAG_B || child_tag == HTM_TAG_I ||
                                                          child_tag == HTM_TAG_SMALL || child_tag == HTM_TAG_SUB ||
                                                          child_tag == HTM_TAG_SUP || child_tag == HTM_TAG_ABBR ||
                                                          child_tag == HTM_TAG_CODE || child_tag == HTM_TAG_KBD ||
                                                          child_tag == HTM_TAG_MARK || child_tag == HTM_TAG_Q ||
                                                          child_tag == HTM_TAG_S || child_tag == HTM_TAG_SAMP ||
                                                          child_tag == HTM_TAG_VAR || child_tag == HTM_TAG_TIME ||
                                                          child_tag == HTM_TAG_U || child_tag == HTM_TAG_CITE ||
                                                          child_tag == HTM_TAG_BDI || child_tag == HTM_TAG_BDO ||
                                                          child_tag == HTM_TAG_BR);
                                        if (is_inline) {
                                            has_inline_element = true;
                                        } else {
                                            has_block_element = true;
                                        }
                                    } else if (content->is_text()) {
                                        // Check if text is non-whitespace
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
                                    content = content->next_sibling;
                                }
                            }
                            // Estimate height based on content type:
                            // - Divs with block elements: use 56 as fallback
                            // - Divs with only inline elements or text: use text_line_height
                            // - Empty divs: use 0
                            if (has_block_element) {
                                elem_height = 56;
                                log_debug("Non-flex div with block elements: using estimate height=56");
                            } else if (has_inline_element || has_text_content) {
                                elem_height = text_line_height;
                                log_debug("Non-flex div with inline/text content: using text_line_height=%d", elem_height);
                            } else {
                                elem_height = 0;
                            }
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
                            // Check for explicit CSS height property before using default
                            log_debug("Checking explicit CSS height for %s, elem=%p, specified_style=%p",
                                      sub_child->node_name(), elem, elem ? elem->specified_style : nullptr);
                            if (elem && elem->specified_style) {
                                CssDeclaration* height_decl = style_tree_get_declaration(
                                    elem->specified_style, CSS_PROPERTY_HEIGHT);
                                log_debug("  height_decl=%p, value=%p, type=%d",
                                          height_decl, height_decl ? height_decl->value : nullptr,
                                          height_decl && height_decl->value ? height_decl->value->type : -1);
                                if (height_decl && height_decl->value &&
                                    height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                    elem_height = (int)resolve_length_value(lycon, CSS_PROPERTY_HEIGHT,
                                                                             height_decl->value);
                                    has_explicit_height_css = true;
                                    log_debug("Element %s has explicit CSS height=%d",
                                              sub_child->node_name(), elem_height);
                                }
                            }
                            if (!has_explicit_height_css) {
                                elem_height = 20;  // Default element height
                                log_debug("Element %s using default height=20", sub_child->node_name());
                            }
                        }
                    }

                    // Add margins only if element has height
                    // Don't add margin for elements with explicit CSS height or text content
                    if (elem_height > 0) {
                        // Check if this child is an inline element (a, span, etc.)
                        // Inline elements should not be summed - they flow on the same line
                        bool is_inline_child = false;
                        if (tag == HTM_TAG_A || tag == HTM_TAG_SPAN || tag == HTM_TAG_EM ||
                            tag == HTM_TAG_STRONG || tag == HTM_TAG_B || tag == HTM_TAG_I ||
                            tag == HTM_TAG_SMALL || tag == HTM_TAG_SUB || tag == HTM_TAG_SUP ||
                            tag == HTM_TAG_ABBR || tag == HTM_TAG_CODE || tag == HTM_TAG_KBD ||
                            tag == HTM_TAG_MARK || tag == HTM_TAG_Q || tag == HTM_TAG_S ||
                            tag == HTM_TAG_SAMP || tag == HTM_TAG_VAR || tag == HTM_TAG_TIME ||
                            tag == HTM_TAG_U || tag == HTM_TAG_CITE || tag == HTM_TAG_BDI ||
                            tag == HTM_TAG_BDO) {
                            is_inline_child = true;
                        }

                        if (is_row_flex || is_inline_child) {
                            // Row flex or inline children: use MAX of child heights
                            // Don't add margin for text-containing inline elements or explicit height elements
                            int margin = (elem_height == text_line_height || has_explicit_height_css || is_inline_child) ? 0 : 10;
                            int total_elem_height = elem_height + margin;
                            if (total_elem_height > max_child_height) {
                                max_child_height = total_elem_height;
                            }
                        } else {
                            // Column flex or normal block with block children: SUM of child heights
                            int margin = has_explicit_height_css ? 0 : 10;
                            measured_height += elem_height + margin;
                        }
                    }
                }
                sub_child = sub_child->next_sibling;
            }
        }

        // For row flex containers OR blocks with only inline children, use max_child_height
        // This is because inline children flow horizontally and should not stack heights
        if (max_child_height > 0 && (is_row_flex || measured_height == 0)) {
            measured_height = max_child_height;
            log_debug("Using max child height %d (is_row_flex=%d)", measured_height, is_row_flex);
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

        // Special handling for form controls - use intrinsic size as content
        if (elem && elem->item_prop_type == DomElement::ITEM_PROP_FORM && elem->form) {
            // Form controls have intrinsic sizes stored in form property
            content_height = (int)elem->form->intrinsic_height;
            measured_height = content_height;
            content_width = (int)elem->form->intrinsic_width;
            measured_width = content_width;

            // Special handling for buttons with child content (e.g., <button>Subscribe</button>)
            // The intrinsic_width may not be set because buttons go through normal layout flow
            if (elem->form->control_type == FORM_CONTROL_BUTTON &&
                elem->form->intrinsic_width <= 0 && elem->first_child) {
                // Get text-transform from parent element chain
                CssEnum btn_text_transform = CSS_VALUE_NONE;
                DomNode* tt_node = elem;
                while (tt_node) {
                    if (tt_node->is_element()) {
                        DomElement* tt_elem = tt_node->as_element();
                        ViewBlock* tt_view = (ViewBlock*)tt_elem;
                        if (tt_view->blk && tt_view->blk->text_transform != 0 &&
                            tt_view->blk->text_transform != CSS_VALUE_INHERIT) {
                            btn_text_transform = tt_view->blk->text_transform;
                            break;
                        }
                        if (tt_elem->specified_style) {
                            CssDeclaration* decl = style_tree_get_declaration(
                                tt_elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
                            if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                                CssEnum val = decl->value->data.keyword;
                                if (val != CSS_VALUE_INHERIT && val != CSS_VALUE_NONE) {
                                    btn_text_transform = val;
                                    break;
                                }
                            }
                        }
                    }
                    tt_node = tt_node->parent;
                }

                // Measure text content of button
                DomNode* btn_child = elem->first_child;
                float max_text_width = 0;
                while (btn_child) {
                    if (btn_child->is_text()) {
                        const char* text = (const char*)btn_child->text_data();
                        if (text && *text) {
                            size_t len = strlen(text);
                            TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, len, btn_text_transform);
                            if (widths.max_content > max_text_width) {
                                max_text_width = widths.max_content;
                            }
                        }
                    }
                    btn_child = btn_child->next_sibling;
                }
                if (max_text_width > 0) {
                    // Store intrinsic size in form property for flex-basis calculation
                    // Use FormDefaults::TEXT_HEIGHT to match input element height
                    elem->form->intrinsic_width = max_text_width;
                    elem->form->intrinsic_height = FormDefaults::TEXT_HEIGHT;

                    // Update content sizes (intrinsic, without padding/border)
                    // Padding/border will be added below in the generic code
                    content_width = (int)max_text_width;
                    measured_width = content_width;
                    content_height = (int)FormDefaults::TEXT_HEIGHT;
                    measured_height = content_height;

                    log_debug("Button %s: measured text content width=%.1f, intrinsic=%dx%d",
                              child->node_name(), max_text_width,
                              (int)elem->form->intrinsic_width, (int)elem->form->intrinsic_height);
                }
            }

            log_debug("Form control %s: using intrinsic size %dx%d",
                      child->node_name(), measured_width, measured_height);
        }

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
    if (!item) {
        log_debug("calculate_item_intrinsic_sizes: invalid item");
        return;
    }
    
    // Form controls use FormControlProp instead of FlexItemProp (they're in a union).
    // Form controls have their intrinsic sizes in form->intrinsic_width/height,
    // not fi->intrinsic_width/height. Skip this function for form controls.
    if (item->item_prop_type == DomElement::ITEM_PROP_FORM) {
        log_debug("calculate_item_intrinsic_sizes: skipping form control (uses FormControlProp)");
        return;
    }
    
    if (!item->fi) {
        log_debug("calculate_item_intrinsic_sizes: no flex properties");
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

    // Note: Form controls are handled in calculate_flex_basis directly since
    // they don't have fi (FlexItemProp) allocated - form properties use a union
    // with flex item properties, so form controls store their intrinsic sizes
    // in form->intrinsic_width/height instead.

    // Check if item has children to measure
    DomNode* child = item->first_child;
    if (!child) {
        // No children - check for pseudo-element content (::before/::after)
        // This is critical for icon fonts like FontAwesome which use ::before with content
        bool has_pseudo_content = false;
        float pseudo_width = 0, pseudo_height = 0;

        if (lycon) {
            DomElement* elem = item;
            bool has_before = dom_element_has_before_content(elem);
            bool has_after = dom_element_has_after_content(elem);

            if (has_before || has_after) {
                log_debug("calculate_item_intrinsic_sizes: element has pseudo-element content (before=%d, after=%d)",
                          has_before, has_after);

                // Get content of pseudo-elements and measure using parent's font
                // For FontAwesome icons, the icon font-family is inherited from parent
                if (has_before) {
                    const char* before_content = dom_element_get_pseudo_element_content(elem, PSEUDO_ELEMENT_BEFORE);
                    if (before_content && *before_content) {
                        // Set up font for measurement using parent element's font
                        FontBox saved = lycon->font;
                        if (item->font) {
                            setup_font(lycon->ui_context, &lycon->font, item->font);
                        }

                        // Measure the pseudo-element content
                        size_t content_len = strlen(before_content);
                        TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, before_content, content_len);

                        pseudo_width += widths.max_content;
                        float line_height = (lycon->font.style && lycon->font.style->font_size > 0) ?
                                            lycon->font.style->font_size : 16.0f;
                        if (lycon->font.ft_face) {
                            line_height = calc_normal_line_height(lycon->font.ft_face);
                        }
                        if (line_height > pseudo_height) {
                            pseudo_height = line_height;
                        }

                        log_debug("calculate_item_intrinsic_sizes: ::before content='%s' -> width=%.1f, height=%.1f",
                                  before_content, widths.max_content, line_height);

                        lycon->font = saved;
                        has_pseudo_content = true;
                    }
                }

                if (has_after) {
                    const char* after_content = dom_element_get_pseudo_element_content(elem, PSEUDO_ELEMENT_AFTER);
                    if (after_content && *after_content) {
                        FontBox saved = lycon->font;
                        if (item->font) {
                            setup_font(lycon->ui_context, &lycon->font, item->font);
                        }

                        size_t content_len = strlen(after_content);
                        TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, after_content, content_len);

                        pseudo_width += widths.max_content;
                        float line_height = (lycon->font.style && lycon->font.style->font_size > 0) ?
                                            lycon->font.style->font_size : 16.0f;
                        if (lycon->font.ft_face) {
                            line_height = calc_normal_line_height(lycon->font.ft_face);
                        }
                        if (line_height > pseudo_height) {
                            pseudo_height = line_height;
                        }

                        log_debug("calculate_item_intrinsic_sizes: ::after content='%s' -> width=%.1f, height=%.1f",
                                  after_content, widths.max_content, line_height);

                        lycon->font = saved;
                        has_pseudo_content = true;
                    }
                }
            }
        }

        // Use explicit dimensions if specified, otherwise use pseudo-element content size
        if (item->blk && item->blk->given_width > 0) {
            min_width = max_width = item->blk->given_width;
        } else if (has_pseudo_content) {
            min_width = max_width = pseudo_width;
        } else {
            min_width = max_width = 0;
        }
        if (item->blk && item->blk->given_height > 0) {
            min_height = max_height = item->blk->given_height;
        } else if (has_pseudo_content) {
            min_height = max_height = pseudo_height;
        } else {
            min_height = max_height = 0;
        }
        log_debug("Empty element intrinsic sizes: width=%.1f, height=%.1f (pseudo_content=%d)",
                  min_width, min_height, has_pseudo_content);
    } else if (child->is_text() && !child->next_sibling) {
        // Simple text node - use unified intrinsic sizing API if available
        const char* text = (const char*)child->text_data();
        if (text) {
            size_t len = strlen(text);
            LayoutContext* lycon = flex_layout ? flex_layout->lycon : nullptr;
            if (lycon) {
                // CRITICAL FIX: Normalize whitespace according to CSS white-space property
                // before measuring. For normal/nowrap, trailing whitespace should be collapsed.
                CssEnum ws = get_white_space_value(child);
                const char* measure_text = text;
                size_t measure_len = len;
                static thread_local char normalized_buffer[4096];

                if (should_collapse_whitespace(ws)) {
                    measure_len = normalize_whitespace_for_flex(text, len, normalized_buffer, sizeof(normalized_buffer));
                    measure_text = normalized_buffer;
                    log_debug("Normalized text for intrinsic sizing: '%s' -> '%s' (ws=%d)",
                              text, normalized_buffer, ws);
                }

                // Get text-transform from parent element (text inherits from parent)
                // Look up parent chain since text-transform is inherited
                CssEnum text_transform = CSS_VALUE_NONE;
                DomNode* tt_node = item;
                while (tt_node) {
                    if (tt_node->is_element()) {
                        DomElement* tt_elem = tt_node->as_element();
                        ViewBlock* tt_view = (ViewBlock*)tt_elem;
                        if (tt_view->blk && tt_view->blk->text_transform != 0 &&
                            tt_view->blk->text_transform != CSS_VALUE_INHERIT) {
                            text_transform = tt_view->blk->text_transform;
                            break;
                        }
                        if (tt_elem->specified_style) {
                            CssDeclaration* decl = style_tree_get_declaration(
                                tt_elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
                            if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                                CssEnum val = decl->value->data.keyword;
                                if (val != CSS_VALUE_INHERIT && val != CSS_VALUE_NONE) {
                                    text_transform = val;
                                    break;
                                }
                            }
                        }
                    }
                    tt_node = tt_node->parent;
                }

                // Use accurate FreeType-based measurement
                TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, measure_text, measure_len, text_transform);
                min_width = widths.min_content;
                max_width = widths.max_content;

                // Calculate height using CSS line-height if available, otherwise font metrics
                // Line-height is inherited, so walk up the parent chain to find it
                float resolved_line_height = 0;
                DomNode* lh_node = item;
                while (lh_node) {
                    if (lh_node->is_element()) {
                        DomElement* lh_elem = lh_node->as_element();
                        ViewBlock* lh_view = (ViewBlock*)lh_elem;

                        // Check blk->line_height first (resolved CSS property)
                        if (lh_view->blk && lh_view->blk->line_height) {
                            const CssValue* lh_val = lh_view->blk->line_height;
                            // Skip 'inherit' keyword - continue to parent
                            if (lh_val->type == CSS_VALUE_TYPE_KEYWORD &&
                                lh_val->data.keyword == CSS_VALUE_INHERIT) {
                                lh_node = lh_node->parent;
                                continue;
                            }
                            // Resolve the line-height value
                            if (lh_val->type == CSS_VALUE_TYPE_NUMBER) {
                                // Unitless number: multiply by font-size
                                resolved_line_height = lh_val->data.number.value * lycon->font.current_font_size;
                            } else if (lh_val->type == CSS_VALUE_TYPE_KEYWORD &&
                                       lh_val->data.keyword == CSS_VALUE_NORMAL) {
                                // 'normal' - use font metrics
                                if (lycon->font.ft_face) {
                                    resolved_line_height = calc_normal_line_height(lycon->font.ft_face);
                                }
                            } else {
                                // Length or percentage
                                resolved_line_height = resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, lh_val);
                            }
                            if (resolved_line_height > 0) {
                                log_debug("calculate_item_intrinsic_sizes: using CSS line-height=%.1f from %s",
                                          resolved_line_height, lh_node->node_name());
                                break;
                            }
                        }

                        // Also check specified_style for line-height declaration
                        if (lh_elem->specified_style) {
                            CssDeclaration* lh_decl = style_tree_get_declaration(
                                lh_elem->specified_style, CSS_PROPERTY_LINE_HEIGHT);
                            if (lh_decl && lh_decl->value) {
                                const CssValue* lh_val = lh_decl->value;
                                // Skip 'inherit' keyword
                                if (lh_val->type == CSS_VALUE_TYPE_KEYWORD &&
                                    lh_val->data.keyword == CSS_VALUE_INHERIT) {
                                    lh_node = lh_node->parent;
                                    continue;
                                }
                                if (lh_val->type == CSS_VALUE_TYPE_NUMBER) {
                                    resolved_line_height = lh_val->data.number.value * lycon->font.current_font_size;
                                } else if (lh_val->type == CSS_VALUE_TYPE_KEYWORD &&
                                           lh_val->data.keyword == CSS_VALUE_NORMAL) {
                                    if (lycon->font.ft_face) {
                                        resolved_line_height = calc_normal_line_height(lycon->font.ft_face);
                                    }
                                } else {
                                    resolved_line_height = resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, lh_val);
                                }
                                if (resolved_line_height > 0) {
                                    log_debug("calculate_item_intrinsic_sizes: using CSS line-height=%.1f from specified_style of %s",
                                              resolved_line_height, lh_node->node_name());
                                    break;
                                }
                            }
                        }
                    }
                    lh_node = lh_node->parent;
                }

                // Use resolved line-height, or fallback to font metrics
                if (resolved_line_height > 0) {
                    min_height = max_height = resolved_line_height;
                } else if (lycon->font.ft_face) {
                    min_height = max_height = calc_normal_line_height(lycon->font.ft_face);
                } else if (lycon->font.style && lycon->font.style->font_size > 0) {
                    min_height = max_height = lycon->font.style->font_size;
                } else {
                    min_height = max_height = 20.0f;
                }
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
        bool is_flex_container = false;
        // Check both block and inline-block view types for flex detection
        if (item->view_type == RDT_VIEW_BLOCK || item->view_type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* block_view = (ViewBlock*)item;
            if (block_view->embed && block_view->embed->flex) {
                is_flex_container = true;
                int dir = block_view->embed->flex->direction;
                is_row_flex_container = (dir == CSS_VALUE_ROW || dir == CSS_VALUE_ROW_REVERSE);
                log_debug("calculate_item_intrinsic_sizes: is_row_flex_container=%d (direction=%d)",
                          is_row_flex_container, dir);
            }
        }

        // CRITICAL FIX: For non-flex containers (regular block elements with inline content),
        // use measure_element_intrinsic_widths which correctly sums inline children's widths.
        // The manual child iteration below doesn't handle inline content properly - it takes
        // max of children's widths instead of summing for inline elements.
        LayoutContext* lycon = flex_layout ? flex_layout->lycon : nullptr;
        if (!is_flex_container && lycon) {
            IntrinsicSizes item_sizes = measure_element_intrinsic_widths(lycon, (DomElement*)item);
            min_width = item_sizes.min_content;
            max_width = item_sizes.max_content;
            log_debug("calculate_item_intrinsic_sizes: non-flex container, using measure_element_intrinsic_widths: min=%.1f, max=%.1f",
                      min_width, max_width);

            // For height, calculate from children or use cached value
            // Check cache for height
            if (cached && cached->measured_height > 0) {
                min_height = max_height = cached->measured_height;
                log_debug("calculate_item_intrinsic_sizes: using cached height: %.1f", min_height);
            } else {
                // Calculate height from max-content height function
                float available_width = 10000.0f;  // Large enough for single-line
                min_height = max_height = calculate_max_content_height(lycon, (DomNode*)item, available_width);
                log_debug("calculate_item_intrinsic_sizes: calculated height: %.1f", min_height);
            }

            // Skip the manual child iteration since we've already calculated sizes
            goto store_results;
        }

        // First, try to calculate intrinsic sizes from children
        // This handles both width and height by traversing child elements
        // Track min and max content widths separately per CSS intrinsic sizing spec
        float min_child_width = 0.0f;  // For min-content: max of children's min-content
        float max_child_width = 0.0f;  // For max-content: max of children's max-content
        float total_child_width = 0.0f;  // For row flex containers: sum of child widths
        float total_child_height = 0.0f;
        int child_count = 0;  // Count children for gap calculation

        {
            DomNode* c = child;

            // Set up parent context with item's height so children with percentage heights
            // and aspect-ratio can compute their intrinsic width
            BlockContext* saved_parent = nullptr;
            BlockContext temp_parent = {};
            bool need_restore_parent = false;
            if (lycon) {
                // Get item's explicit height (from CSS or resolved)
                float item_height = -1;
                if (item->blk && item->blk->given_height > 0) {
                    item_height = item->blk->given_height;
                } else {
                    // Try to get from CSS
                    item_height = get_explicit_css_height(lycon, item);
                }
                if (item_height > 0) {
                    saved_parent = lycon->block.parent;
                    temp_parent.content_height = item_height;
                    temp_parent.given_height = item_height;
                    lycon->block.parent = &temp_parent;
                    need_restore_parent = true;
                    log_debug("calculate_item_intrinsic_sizes: set up parent context with height=%.1f", item_height);
                }
            }

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
                            float text_min_width, text_max_width, text_height;
                            if (lycon) {
                                // CRITICAL FIX: Normalize whitespace according to CSS white-space property
                                CssEnum ws = get_white_space_value(c);
                                const char* measure_text = text;
                                size_t measure_len = text_len;
                                static thread_local char normalized_buffer2[4096];

                                if (should_collapse_whitespace(ws)) {
                                    measure_len = normalize_whitespace_for_flex(text, text_len,
                                                                                 normalized_buffer2, sizeof(normalized_buffer2));
                                    measure_text = normalized_buffer2;
                                }

                                // Get text-transform from parent element chain
                                CssEnum text_transform = CSS_VALUE_NONE;
                                DomNode* tt_node = item;
                                while (tt_node) {
                                    if (tt_node->is_element()) {
                                        DomElement* tt_elem = tt_node->as_element();
                                        ViewBlock* tt_view = (ViewBlock*)tt_elem;
                                        if (tt_view->blk && tt_view->blk->text_transform != 0 &&
                                            tt_view->blk->text_transform != CSS_VALUE_INHERIT) {
                                            text_transform = tt_view->blk->text_transform;
                                            break;
                                        }
                                        if (tt_elem->specified_style) {
                                            CssDeclaration* decl = style_tree_get_declaration(
                                                tt_elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
                                            if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                                                CssEnum val = decl->value->data.keyword;
                                                if (val != CSS_VALUE_INHERIT && val != CSS_VALUE_NONE) {
                                                    text_transform = val;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                    tt_node = tt_node->parent;
                                }

                                TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, measure_text, measure_len, text_transform);
                                text_min_width = widths.min_content;
                                text_max_width = widths.max_content;
                                // BUGFIX: Use line height instead of font size for text height
                                // This matches browser behavior where text takes up line-height space
                                if (lycon->font.ft_face) {
                                    text_height = calc_normal_line_height(lycon->font.ft_face);
                                } else if (lycon->font.style && lycon->font.style->font_size > 0) {
                                    text_height = lycon->font.style->font_size;  // Fallback to font-size
                                } else {
                                    text_height = 20.0f;  // Ultimate fallback
                                }
                            } else {
                                text_max_width = text_len * 10.0f;
                                text_min_width = text_max_width;  // Fallback: same as max
                                text_height = 20.0f;
                            }

                            // CRITICAL FIX: For row flex containers, text nodes should be summed
                            // into total_child_width just like element children. Previously text
                            // was only MAX'd which caused incorrect intrinsic width calculation
                            // for inline-flex containers with both text and element children.
                            if (is_row_flex_container) {
                                total_child_width += text_max_width;  // Row flex: sum for max-content
                                child_count++;  // Count text as a child for gap calculation
                            } else {
                                min_child_width = max(min_child_width, text_min_width);
                                max_child_width = max(max_child_width, text_max_width);
                            }

                            // For height, row flex takes max, column flex sums
                            if (is_row_flex_container) {
                                total_child_height = max(total_child_height, text_height);
                            } else {
                                total_child_height += text_height;
                            }
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

                        float child_min_width = 0.0f;
                        float child_max_width = 0.0f;
                        float child_height = 0.0f;

                        // Get child width - explicit (from View or DOM) or intrinsic
                        if (child_has_explicit_width) {
                            float explicit_w = 0.0f;
                            if (child_view->blk && child_view->blk->given_width > 0) {
                                explicit_w = child_view->blk->given_width;
                            } else if (dom_css_width > 0) {
                                explicit_w = dom_css_width;
                            }
                            // Explicit width is both min and max
                            child_min_width = child_max_width = explicit_w;
                        } else if (child_view->fi) {
                            // Child has fi - use cached intrinsic or calculate
                            if (!child_view->fi->has_intrinsic_width) {
                                calculate_item_intrinsic_sizes(child_view, flex_layout);
                            }
                            if (child_view->fi->has_intrinsic_width) {
                                child_min_width = child_view->fi->intrinsic_width.min_content;
                                child_max_width = child_view->fi->intrinsic_width.max_content;
                            }
                        } else if (lycon) {
                            // Child doesn't have fi yet - use measure_element_intrinsic_widths
                            // This handles the case where intrinsic sizing runs before fi is initialized

                            // CRITICAL FIX: Set up child's font context before measuring
                            // Otherwise text will be measured with parent's font (wrong width)
                            FontBox saved_child_font = lycon->font;
                            bool child_font_changed = false;
                            if (child_view->font) {
                                setup_font(lycon->ui_context, &lycon->font, child_view->font);
                                child_font_changed = true;
                            }

                            IntrinsicSizes child_sizes = measure_element_intrinsic_widths(lycon, (DomElement*)child_view);
                            child_min_width = child_sizes.min_content;
                            child_max_width = child_sizes.max_content;
                            log_debug("Used measure_element_intrinsic_widths for child: min=%.1f, max=%.1f",
                                      child_min_width, child_max_width);

                            // Restore parent font
                            if (child_font_changed) {
                                lycon->font = saved_child_font;
                            }
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

                        // CRITICAL: If child height is still 0 without explicit height,
                        // try to measure content-based height from the DOM tree
                        if (child_height == 0.0f && !child_has_explicit_height) {
                            DisplayValue child_display = resolve_display_value((void*)c);
                            log_debug("Child height is 0, checking display - display.inner=%d, display.outer=%d",
                                      child_display.inner, child_display.outer);
                            if (child_display.inner == CSS_VALUE_FLEX) {
                                // For flex containers, use recursive DOM-based measurement
                                child_height = measure_content_height_recursive(c, lycon);
                                log_debug("Nested flex child height from recursive measurement: %.1f", child_height);
                            } else if (child_display.outer == CSS_VALUE_BLOCK && lycon) {
                                // For regular block elements (like h2), measure content height
                                // using intrinsic sizing module
                                // Use a large available width for max-content calculation
                                float available_width = 10000.0f;  // Large enough for single-line
                                child_height = calculate_max_content_height(lycon, c, available_width);
                                log_debug("Block child height from calculate_max_content_height: %.1f", child_height);
                            }
                        }

                        // For width: row flex sums widths, column flex takes max
                        // Track both min and max content widths separately
                        if (is_row_flex_container) {
                            total_child_width += child_max_width;  // Row flex: sum for max-content
                        } else {
                            min_child_width = max(min_child_width, child_min_width);
                            max_child_width = max(max_child_width, child_max_width);
                        }
                        child_count++;

                        // For height, column flex containers sum heights, row flex takes max
                        if (is_row_flex_container) {
                            total_child_height = max(total_child_height, child_height);
                        } else {
                            total_child_height += child_height;
                        }

                        log_debug("Child element: min_width=%.1f, max_width=%.1f, height=%.1f (explicit=%d/%d)",
                                  child_min_width, child_max_width, child_height, child_has_explicit_width, child_has_explicit_height);
                    }
                }
                c = c->next_sibling;
            }

            // For row flex containers, add gaps to total width
            if (is_row_flex_container && child_count > 1) {
                // Get gap from the flex container properties
                float gap = 0;
                if (item->view_type == RDT_VIEW_BLOCK || item->view_type == RDT_VIEW_INLINE_BLOCK) {
                    ViewBlock* block_view = (ViewBlock*)item;
                    if (block_view->embed && block_view->embed->flex) {
                        gap = block_view->embed->flex->column_gap;
                    }
                }
                total_child_width += gap * (child_count - 1);
                log_debug("Row flex: added %d gaps of %.1f = %.1f total gap pixels",
                          child_count - 1, gap, gap * (child_count - 1));
            }

            log_debug("Traversed children: min_width=%.1f, max_width=%.1f, total_width=%.1f, total_height=%.1f, is_row_flex=%d",
                      min_child_width, max_child_width, total_child_width, total_child_height, is_row_flex_container);

            // Restore parent context
            if (need_restore_parent && lycon) {
                lycon->block.parent = saved_parent;
            }
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
        } else if (min_child_width > 0.0f || max_child_width > 0.0f) {
            // Use properly tracked min and max content widths
            min_width = min_child_width;
            max_width = max_child_width;
            log_debug("Using calculated widths from children: min=%.1f, max=%.1f", min_width, max_width);
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

store_results:
    // Store results
    item->fi->intrinsic_width.min_content = min_width;
    item->fi->intrinsic_width.max_content = max_width;
    item->fi->intrinsic_height.min_content = min_height;
    item->fi->intrinsic_height.max_content = max_height;
    item->fi->has_intrinsic_width = 1;
    item->fi->has_intrinsic_height = 1;

    log_debug("Intrinsic sizes calculated: width=[%.1f, %.1f], height=[%.1f, %.1f]",
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
                // Get text-transform from parent element chain
                CssEnum text_transform = CSS_VALUE_NONE;
                DomNode* tt_node = block;
                while (tt_node) {
                    if (tt_node->is_element()) {
                        DomElement* tt_elem = tt_node->as_element();
                        ViewBlock* tt_view = (ViewBlock*)tt_elem;
                        if (tt_view->blk && tt_view->blk->text_transform != 0 &&
                            tt_view->blk->text_transform != CSS_VALUE_INHERIT) {
                            text_transform = tt_view->blk->text_transform;
                            break;
                        }
                        if (tt_elem->specified_style) {
                            CssDeclaration* decl = style_tree_get_declaration(
                                tt_elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
                            if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                                CssEnum val = decl->value->data.keyword;
                                if (val != CSS_VALUE_INHERIT && val != CSS_VALUE_NONE) {
                                    text_transform = val;
                                    break;
                                }
                            }
                        }
                    }
                    tt_node = tt_node->parent;
                }
                TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, len, text_transform);
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
