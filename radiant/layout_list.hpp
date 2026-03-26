#ifndef LAYOUT_LIST_HPP
#define LAYOUT_LIST_HPP

#include "layout.hpp"

// Forward declarations
typedef struct StyleTree StyleTree;
typedef struct CssDeclaration CssDeclaration;

// ============================================================================
// List Container Counter Setup
// ============================================================================

/**
 * Apply implicit counter-reset for list containers (OL, UL, MENU, DIR).
 * CSS 2.1 §12.5: These elements have implicit counter-reset: list-item,
 * creating a new counter instance for nested numbering.
 * Also handles <ol reversed> and <ol start="N"> attributes.
 */
void setup_list_container_counters(LayoutContext* lycon, ViewBlock* block, DomElement* dom_elem);

/**
 * Compute initial values for reversed() counters in counter-reset.
 * CSS Lists 3 §4.4.2: For reversed() without explicit value, DFS-walk
 * the subtree to compute initial = -(total_non_zero_increments + last_nz).
 */
void compute_reversed_counter_initial(LayoutContext* lycon, DomElement* dom_elem);

// ============================================================================
// List Item Processing
// ============================================================================

/**
 * Process a display:list-item element: auto-increment the list-item counter,
 * handle <li value="N"> attribute, and generate the ::marker pseudo-element.
 */
void process_list_item(LayoutContext* lycon, ViewBlock* block, DomNode* elmt,
                       DomElement* dom_elem, DisplayValue display);

// ============================================================================
// Counter Spec Extraction (used by pseudo-element handling)
// ============================================================================

/**
 * Extract a counter property (counter-reset, counter-increment, counter-set)
 * from a StyleTree and convert it to a spec string suitable for
 * counter_reset() / counter_increment() / counter_set().
 * Returns nullptr if the property is not set or is "none".
 */
const char* extract_counter_spec_from_style(StyleTree* style, CssPropertyId css_property,
                                            LayoutContext* lycon);

/**
 * Apply counter operations (counter-reset, counter-increment, counter-set)
 * from a pseudo-element's StyleTree.
 */
void apply_pseudo_counter_ops(LayoutContext* lycon, StyleTree* style);

#endif // LAYOUT_LIST_HPP
