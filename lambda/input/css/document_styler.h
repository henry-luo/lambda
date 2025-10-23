#ifndef DOCUMENT_STYLER_H
#define DOCUMENT_STYLER_H

#include "dom_element.h"
#include "css_style.h"
#include "css_parser.h"
#include "../../../lib/mempool.h"
#include "../../../lib/arraylist.h"
#include "../../../lib/hashmap.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Document-Level Style Management
 *
 * The DocumentStyler provides document-wide CSS management including:
 * - Stylesheet loading and parsing
 * - Selector matching and rule application
 * - Custom property (CSS variables) resolution
 * - Global style invalidation and recomputation
 * - Performance optimization through caching
 */

// Forward declarations
typedef struct DocumentStyler DocumentStyler;
typedef struct SelectorMatcher SelectorMatcher;

// ============================================================================
// Document Styler Structure
// ============================================================================

/**
 * DocumentStyler - Document-wide CSS style management
 *
 * Manages all CSS stylesheets, rules, and custom properties for a document.
 * Provides efficient selector matching and style application to elements.
 */
typedef struct DocumentStyler {
    Pool* pool;                      // Memory pool for allocations

    // Stylesheets
    ArrayList* stylesheets;          // List of CssStylesheet*
    ArrayList* user_agent_sheets;    // User-agent (browser default) stylesheets
    ArrayList* user_sheets;          // User-defined stylesheets
    ArrayList* author_sheets;        // Document author stylesheets

    // Custom properties (CSS variables)
    HashMap* custom_properties;      // Map of custom property names to values

    // Selector matching
    SelectorMatcher* selector_matcher; // Selector matching engine

    // Global versioning for cache invalidation
    uint32_t global_style_version;   // Incremented when stylesheets change

    // Performance statistics
    int total_rules;                 // Total number of CSS rules
    int total_selectors;             // Total number of selectors
    int total_declarations;          // Total number of declarations

    // Root element for custom property inheritance
    DomElement* root_element;        // Document root element
} DocumentStyler;

// ============================================================================
// Document Styler Creation and Destruction
// ============================================================================

/**
 * Create a new document styler
 * @param pool Memory pool for allocations
 * @return New DocumentStyler or NULL on failure
 */
DocumentStyler* document_styler_create(Pool* pool);

/**
 * Destroy a document styler
 * @param styler Styler to destroy
 */
void document_styler_destroy(DocumentStyler* styler);

/**
 * Clear all stylesheets from a document styler
 * @param styler Styler to clear
 */
void document_styler_clear(DocumentStyler* styler);

// ============================================================================
// Stylesheet Management
// ============================================================================

/**
 * Add a stylesheet to the document
 * @param styler Target document styler
 * @param sheet Stylesheet to add
 * @param origin Stylesheet origin (user-agent, user, or author)
 * @return true on success, false on failure
 */
bool document_styler_add_stylesheet(DocumentStyler* styler, CssStylesheet* sheet, CssOrigin origin);

/**
 * Remove a stylesheet from the document
 * @param styler Target document styler
 * @param sheet Stylesheet to remove
 * @return true if stylesheet was removed, false if not found
 */
bool document_styler_remove_stylesheet(DocumentStyler* styler, CssStylesheet* sheet);

/**
 * Parse and add a stylesheet from CSS text
 * @param styler Target document styler
 * @param css_text CSS source text
 * @param length Length of CSS text
 * @param origin Stylesheet origin
 * @return Parsed stylesheet or NULL on failure
 */
CssStylesheet* document_styler_add_stylesheet_from_text(DocumentStyler* styler,
                                                        const char* css_text,
                                                        size_t length,
                                                        CssOrigin origin);

/**
 * Parse and add a stylesheet from a file
 * @param styler Target document styler
 * @param filename Path to CSS file
 * @param origin Stylesheet origin
 * @return Parsed stylesheet or NULL on failure
 */
CssStylesheet* document_styler_add_stylesheet_from_file(DocumentStyler* styler,
                                                        const char* filename,
                                                        CssOrigin origin);

/**
 * Get all stylesheets of a specific origin
 * @param styler Target document styler
 * @param origin Stylesheet origin to filter by
 * @param count Output: number of stylesheets returned
 * @return Array of stylesheets or NULL if none
 */
CssStylesheet** document_styler_get_stylesheets(DocumentStyler* styler,
                                               CssOrigin origin,
                                               int* count);

// ============================================================================
// Style Application to Elements
// ============================================================================

/**
 * Apply all matching CSS rules to an element
 * @param styler Document styler
 * @param element Target element
 * @return Number of rules applied
 */
int document_styler_apply_to_element(DocumentStyler* styler, DomElement* element);

/**
 * Apply all matching CSS rules to an element tree (recursive)
 * @param styler Document styler
 * @param root Root element of tree
 * @return Total number of rules applied to all elements
 */
int document_styler_apply_to_tree(DocumentStyler* styler, DomElement* root);

/**
 * Recompute styles for all elements in a tree
 * @param styler Document styler
 * @param root Root element of tree
 * @return Number of elements recomputed
 */
int document_styler_recompute_tree(DocumentStyler* styler, DomElement* root);

/**
 * Invalidate all element styles in a tree
 * @param styler Document styler
 * @param root Root element of tree
 */
void document_styler_invalidate_tree(DocumentStyler* styler, DomElement* root);

// ============================================================================
// Custom Properties (CSS Variables)
// ============================================================================

/**
 * Register a custom property (CSS variable)
 * @param styler Document styler
 * @param name Custom property name (including --)
 * @param value Initial value
 * @return true on success, false on failure
 */
bool document_styler_register_custom_property(DocumentStyler* styler,
                                              const char* name,
                                              CssValue* value);

/**
 * Get a custom property value
 * @param styler Document styler
 * @param name Custom property name
 * @return Custom property value or NULL if not found
 */
CssValue* document_styler_get_custom_property(DocumentStyler* styler, const char* name);

/**
 * Remove a custom property
 * @param styler Document styler
 * @param name Custom property name
 * @return true if property was removed, false if not found
 */
bool document_styler_remove_custom_property(DocumentStyler* styler, const char* name);

/**
 * Resolve a var() reference in a CSS value
 * @param styler Document styler
 * @param element Element context for custom property lookup
 * @param var_name Variable name (without --)
 * @param fallback Optional fallback value
 * @return Resolved value or NULL if not found
 */
CssValue* document_styler_resolve_var(DocumentStyler* styler,
                                      DomElement* element,
                                      const char* var_name,
                                      CssValue* fallback);

// ============================================================================
// Rule Matching and Selection
// ============================================================================

/**
 * Find all rules that match an element
 * @param styler Document styler
 * @param element Target element
 * @param matched_rules Output: array of matched rules (allocated)
 * @param matched_count Output: number of matched rules
 * @return true on success, false on failure
 */
bool document_styler_match_rules(DocumentStyler* styler,
                                DomElement* element,
                                CssRule*** matched_rules,
                                int* matched_count);

/**
 * Find all elements that match a selector
 * @param styler Document styler
 * @param selector Selector to match
 * @param root Root element to search from
 * @param matched_elements Output: array of matched elements (allocated)
 * @param matched_count Output: number of matched elements
 * @return true on success, false on failure
 */
bool document_styler_query_selector_all(DocumentStyler* styler,
                                        CssSelector* selector,
                                        DomElement* root,
                                        DomElement*** matched_elements,
                                        int* matched_count);

/**
 * Find first element that matches a selector
 * @param styler Document styler
 * @param selector Selector to match
 * @param root Root element to search from
 * @return First matched element or NULL if none
 */
DomElement* document_styler_query_selector(DocumentStyler* styler,
                                           CssSelector* selector,
                                           DomElement* root);

// ============================================================================
// Inline Style Support
// ============================================================================

/**
 * Parse and apply inline style attribute to an element
 * @param styler Document styler
 * @param element Target element
 * @param style_text Inline style text (e.g., "color: red; font-size: 14px")
 * @return Number of declarations applied
 */
int document_styler_apply_inline_style(DocumentStyler* styler,
                                       DomElement* element,
                                       const char* style_text);

/**
 * Get inline style text from an element
 * @param styler Document styler
 * @param element Source element
 * @return Inline style text or NULL if none
 */
char* document_styler_get_inline_style(DocumentStyler* styler, DomElement* element);

// ============================================================================
// Dynamic Style Updates
// ============================================================================

/**
 * Update an element's style property dynamically
 * @param styler Document styler
 * @param element Target element
 * @param property_name Property name
 * @param value_text Value text (will be parsed)
 * @return true on success, false on failure
 */
bool document_styler_set_property(DocumentStyler* styler,
                                  DomElement* element,
                                  const char* property_name,
                                  const char* value_text);

/**
 * Remove a style property from an element
 * @param styler Document styler
 * @param element Target element
 * @param property_name Property name
 * @return true if property was removed, false if not found
 */
bool document_styler_remove_property(DocumentStyler* styler,
                                     DomElement* element,
                                     const char* property_name);

/**
 * Add a CSS rule dynamically
 * @param styler Document styler
 * @param rule_text CSS rule text (e.g., ".class { color: red; }")
 * @param origin Rule origin
 * @return Parsed and added rule or NULL on failure
 */
CssRule* document_styler_add_rule(DocumentStyler* styler,
                                  const char* rule_text,
                                  CssOrigin origin);

/**
 * Remove a CSS rule dynamically
 * @param styler Document styler
 * @param rule Rule to remove
 * @return true if rule was removed, false if not found
 */
bool document_styler_remove_rule(DocumentStyler* styler, CssRule* rule);

// ============================================================================
// Pseudo-Class Management
// ============================================================================

/**
 * Set pseudo-class state on an element and update matching styles
 * @param styler Document styler
 * @param element Target element
 * @param pseudo_class Pseudo-class name (e.g., "hover", "focus")
 * @param enabled true to enable, false to disable
 * @return true on success, false on failure
 */
bool document_styler_set_pseudo_class(DocumentStyler* styler,
                                      DomElement* element,
                                      const char* pseudo_class,
                                      bool enabled);

/**
 * Toggle a pseudo-class state on an element
 * @param styler Document styler
 * @param element Target element
 * @param pseudo_class Pseudo-class name
 * @return true if now enabled, false if disabled
 */
bool document_styler_toggle_pseudo_class(DocumentStyler* styler,
                                         DomElement* element,
                                         const char* pseudo_class);

// ============================================================================
// Performance and Statistics
// ============================================================================

/**
 * Get document styler statistics
 * @param styler Document styler
 * @param total_sheets Output: total number of stylesheets
 * @param total_rules Output: total number of rules
 * @param total_declarations Output: total number of declarations
 * @param cache_hit_rate Output: selector matcher cache hit rate
 */
void document_styler_get_statistics(DocumentStyler* styler,
                                    int* total_sheets,
                                    int* total_rules,
                                    int* total_declarations,
                                    double* cache_hit_rate);

/**
 * Clear selector matcher cache
 * @param styler Document styler
 */
void document_styler_clear_cache(DocumentStyler* styler);

/**
 * Print document styler information for debugging
 * @param styler Document styler
 */
void document_styler_print_info(DocumentStyler* styler);

/**
 * Print all CSS rules for debugging
 * @param styler Document styler
 * @param origin Optional origin filter (0 for all)
 */
void document_styler_print_rules(DocumentStyler* styler, CssOrigin origin);

// ============================================================================
// Root Element Management
// ============================================================================

/**
 * Set the document root element
 * @param styler Document styler
 * @param root Root element
 */
void document_styler_set_root(DocumentStyler* styler, DomElement* root);

/**
 * Get the document root element
 * @param styler Document styler
 * @return Root element or NULL if not set
 */
DomElement* document_styler_get_root(DocumentStyler* styler);

#ifdef __cplusplus
}
#endif

#endif // DOCUMENT_STYLER_H
