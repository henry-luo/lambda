#ifndef SELECTOR_MATCHER_H
#define SELECTOR_MATCHER_H

#include "dom_element.h"
#include "css_style.h"
#include "css_parser.h"
#include "../../../lib/mempool.h"
#include "../../../lib/hashmap.h"
#include "../../../lib/arraylist.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Selector Matching Engine
 *
 * Provides efficient CSS selector-to-element matching with:
 * - Fast selector matching algorithm
 * - Caching for performance optimization
 * - Support for all CSS3/4 selectors including pseudo-classes
 * - Bloom filter for quick element filtering
 * - Match result caching
 */

// Forward declarations
typedef struct SelectorMatcher SelectorMatcher;
typedef struct MatchResult MatchResult;
typedef struct SelectorCache SelectorCache;
typedef struct SelectorEntry SelectorEntry;

// ============================================================================
// Selector Entry (for caching)
// ============================================================================

/**
 * SelectorEntry - Cached selector with pre-computed data
 *
 * Caches expensive computations like tag name lookups to avoid
 * repeated string comparisons. Based on lexbor's entry caching strategy.
 */
typedef struct SelectorEntry {
    CssSimpleSelector* selector;  // Original selector
    void* cached_tag_ptr;         // Cached tag name pointer (from name_pool)
    uintptr_t cached_tag_id;      // Numeric ID for fastest comparison
    uint32_t use_count;           // Usage statistics
    bool cache_valid;             // Whether cached data is valid
} SelectorEntry;

// ============================================================================
// Match Result Structure
// ============================================================================

/**
 * MatchResult - Result of selector matching
 *
 * Contains information about whether a selector matches an element
 * and the specificity of the match.
 */
typedef struct MatchResult {
    bool matches;                    // Whether selector matches element
    CssSpecificity specificity;      // Specificity of the match
    uint32_t pseudo_state_required;  // Required pseudo-class states
    bool matches_with_pseudo;        // Whether match depends on pseudo-state
} MatchResult;

// ============================================================================
// Selector Matcher Structure
// ============================================================================

/**
 * SelectorMatcher - Selector matching engine
 *
 * Manages selector matching with caching and optimization.
 */
typedef struct SelectorMatcher {
    Pool* pool;                      // Memory pool for allocations

    // Caching
    HashMap* match_cache;            // Cache of selector-element match results
    HashMap* selector_entry_cache;   // Cache of SelectorEntry objects
    bool cache_enabled;              // Whether caching is enabled

    // Statistics
    uint64_t total_matches;          // Total match attempts
    uint64_t cache_hits;             // Cache hits
    uint64_t cache_misses;           // Cache misses

    // Configuration
    bool strict_mode;                // Whether to use strict matching
    bool quirks_mode;                // HTML quirks mode (case-insensitive matching)
    bool case_sensitive_classes;     // Case-sensitive class name matching
    bool case_sensitive_attrs;       // Case-sensitive attribute matching

    // Bloom filter (for quick filtering)
    uint8_t* bloom_filter;           // Bloom filter for element properties
    size_t bloom_filter_size;        // Size of bloom filter
} SelectorMatcher;

// ============================================================================
// Selector Matcher Creation and Destruction
// ============================================================================

/**
 * Create a new selector matcher
 * @param pool Memory pool for allocations
 * @return New SelectorMatcher or NULL on failure
 */
SelectorMatcher* selector_matcher_create(Pool* pool);

/**
 * Destroy a selector matcher
 * @param matcher Matcher to destroy
 */
void selector_matcher_destroy(SelectorMatcher* matcher);

/**
 * Clear all caches in the selector matcher
 * @param matcher Matcher to clear
 */
void selector_matcher_clear_cache(SelectorMatcher* matcher);

/**
 * Enable or disable caching
 * @param matcher Selector matcher
 * @param enabled true to enable, false to disable
 */
void selector_matcher_set_cache_enabled(SelectorMatcher* matcher, bool enabled);

/**
 * Set quirks mode (case-insensitive matching)
 * @param matcher Selector matcher
 * @param quirks true for quirks mode, false for standards mode
 */
void selector_matcher_set_quirks_mode(SelectorMatcher* matcher, bool quirks);

/**
 * Set case sensitivity for class names
 * @param matcher Selector matcher
 * @param case_sensitive true for case-sensitive, false for case-insensitive
 */
void selector_matcher_set_case_sensitive_classes(SelectorMatcher* matcher, bool case_sensitive);

/**
 * Set case sensitivity for attribute names/values
 * @param matcher Selector matcher
 * @param case_sensitive true for case-sensitive, false for case-insensitive
 */
void selector_matcher_set_case_sensitive_attributes(SelectorMatcher* matcher, bool case_sensitive);

/**
 * Get or create a cached selector entry
 * @param matcher Selector matcher
 * @param selector Simple selector to cache
 * @return Cached selector entry
 */
SelectorEntry* selector_matcher_get_entry(SelectorMatcher* matcher, CssSimpleSelector* selector);

// ============================================================================
// Primary Matching Functions
// ============================================================================

/**
 * Check if a selector matches an element
 * @param matcher Selector matcher
 * @param selector Selector to match
 * @param element Element to test
 * @param result Output: match result (optional)
 * @return true if selector matches element
 */
bool selector_matcher_matches(SelectorMatcher* matcher,
                              CssSelector* selector,
                              DomElement* element,
                              MatchResult* result);

/**
 * Check if a selector group matches an element (any selector in group)
 * @param matcher Selector matcher
 * @param selector_group Selector group to match
 * @param element Element to test
 * @param result Output: match result with highest specificity (optional)
 * @return true if any selector in group matches element
 */
bool selector_matcher_matches_group(SelectorMatcher* matcher,
                                    CssSelectorGroup* selector_group,
                                    DomElement* element,
                                    MatchResult* result);

/**
 * Find all elements that match a selector in a tree
 * @param matcher Selector matcher
 * @param selector Selector to match
 * @param root Root element to search from
 * @param results Output: array of matched elements (allocated)
 * @param count Output: number of matched elements
 * @return true on success, false on failure
 */
bool selector_matcher_find_all(SelectorMatcher* matcher,
                               CssSelector* selector,
                               DomElement* root,
                               DomElement*** results,
                               int* count);

/**
 * Find first element that matches a selector in a tree
 * @param matcher Selector matcher
 * @param selector Selector to match
 * @param root Root element to search from
 * @return First matched element or NULL if none
 */
DomElement* selector_matcher_find_first(SelectorMatcher* matcher,
                                        CssSelector* selector,
                                        DomElement* root);

// ============================================================================
// Selector Component Matching
// ============================================================================

/**
 * Match a simple selector against an element
 * @param matcher Selector matcher
 * @param simple_selector Simple selector to match
 * @param element Element to test
 * @return true if simple selector matches element
 */
bool selector_matcher_matches_simple(SelectorMatcher* matcher,
                                     CssSimpleSelector* simple_selector,
                                     DomElement* element);

/**
 * Match a compound selector against an element
 * @param matcher Selector matcher
 * @param compound_selector Compound selector to match
 * @param element Element to test
 * @return true if compound selector matches element
 */
bool selector_matcher_matches_compound(SelectorMatcher* matcher,
                                       CssCompoundSelector* compound_selector,
                                       DomElement* element);

/**
 * Match an attribute selector against an element
 * @param matcher Selector matcher
 * @param attr_name Attribute name
 * @param attr_value Attribute value (NULL for existence check)
 * @param attr_type Attribute selector type
 * @param case_insensitive Case-insensitive matching
 * @param element Element to test
 * @return true if attribute selector matches element
 */
bool selector_matcher_matches_attribute(SelectorMatcher* matcher,
                                        const char* attr_name,
                                        const char* attr_value,
                                        CssSelectorType attr_type,
                                        bool case_insensitive,
                                        DomElement* element);

// ============================================================================
// Pseudo-Class Matching
// ============================================================================

/**
 * Match a pseudo-class against an element
 * @param matcher Selector matcher
 * @param pseudo_type Pseudo-class type
 * @param pseudo_arg Pseudo-class argument (for :nth-child, etc.)
 * @param element Element to test
 * @return true if pseudo-class matches element
 */
bool selector_matcher_matches_pseudo_class(SelectorMatcher* matcher,
                                           CssSelectorType pseudo_type,
                                           const char* pseudo_arg,
                                           DomElement* element);

/**
 * Match a structural pseudo-class against an element
 * @param matcher Selector matcher
 * @param pseudo_type Structural pseudo-class type
 * @param element Element to test
 * @return true if structural pseudo-class matches element
 */
bool selector_matcher_matches_structural(SelectorMatcher* matcher,
                                         CssSelectorType pseudo_type,
                                         DomElement* element);

/**
 * Match an nth-child formula against an element
 * @param matcher Selector matcher
 * @param formula nth-child formula (an+b)
 * @param element Element to test
 * @param from_end Count from end (for :nth-last-child)
 * @return true if formula matches element
 */
bool selector_matcher_matches_nth_child(SelectorMatcher* matcher,
                                        CssNthFormula* formula,
                                        DomElement* element,
                                        bool from_end);

// ============================================================================
// Combinator Matching
// ============================================================================

/**
 * Match a selector with combinator against an element
 * @param matcher Selector matcher
 * @param left_selector Left side of combinator
 * @param combinator Combinator type
 * @param right_selector Right side of combinator
 * @param element Element to test (should match right_selector)
 * @return true if combinator relationship holds
 */
bool selector_matcher_matches_combinator(SelectorMatcher* matcher,
                                         CssCompoundSelector* left_selector,
                                         CssCombinator combinator,
                                         CssCompoundSelector* right_selector,
                                         DomElement* element);

/**
 * Check descendant relationship (ancestor selector matches any ancestor)
 * @param matcher Selector matcher
 * @param selector Ancestor selector
 * @param element Descendant element
 * @return true if element has an ancestor matching selector
 */
bool selector_matcher_has_ancestor(SelectorMatcher* matcher,
                                   CssCompoundSelector* selector,
                                   DomElement* element);

/**
 * Check child relationship (parent selector matches immediate parent)
 * @param matcher Selector matcher
 * @param selector Parent selector
 * @param element Child element
 * @return true if element's parent matches selector
 */
bool selector_matcher_has_parent(SelectorMatcher* matcher,
                                 CssCompoundSelector* selector,
                                 DomElement* element);

/**
 * Check next sibling relationship
 * @param matcher Selector matcher
 * @param selector Previous sibling selector
 * @param element Next sibling element
 * @return true if element's previous sibling matches selector
 */
bool selector_matcher_has_prev_sibling(SelectorMatcher* matcher,
                                       CssCompoundSelector* selector,
                                       DomElement* element);

/**
 * Check subsequent sibling relationship
 * @param matcher Selector matcher
 * @param selector Preceding sibling selector
 * @param element Subsequent sibling element
 * @return true if element has a preceding sibling matching selector
 */
bool selector_matcher_has_preceding_sibling(SelectorMatcher* matcher,
                                            CssCompoundSelector* selector,
                                            DomElement* element);

// ============================================================================
// CSS4 Advanced Selectors
// ============================================================================

/**
 * Match :is() pseudo-class (matches any of the selectors)
 * @param matcher Selector matcher
 * @param selectors Array of selectors
 * @param count Number of selectors
 * @param element Element to test
 * @return true if any selector matches
 */
bool selector_matcher_matches_is(SelectorMatcher* matcher,
                                 CssSelector** selectors,
                                 int count,
                                 DomElement* element);

/**
 * Match :where() pseudo-class (same as :is() but with 0 specificity)
 * @param matcher Selector matcher
 * @param selectors Array of selectors
 * @param count Number of selectors
 * @param element Element to test
 * @return true if any selector matches
 */
bool selector_matcher_matches_where(SelectorMatcher* matcher,
                                    CssSelector** selectors,
                                    int count,
                                    DomElement* element);

/**
 * Match :not() pseudo-class (negation)
 * @param matcher Selector matcher
 * @param selectors Array of selectors to negate
 * @param count Number of selectors
 * @param element Element to test
 * @return true if none of the selectors match
 */
bool selector_matcher_matches_not(SelectorMatcher* matcher,
                                  CssSelector** selectors,
                                  int count,
                                  DomElement* element);

/**
 * Match :has() pseudo-class (relational selector)
 * @param matcher Selector matcher
 * @param selectors Array of selectors
 * @param count Number of selectors
 * @param element Element to test
 * @return true if element has a descendant matching any selector
 */
bool selector_matcher_matches_has(SelectorMatcher* matcher,
                                  CssSelector** selectors,
                                  int count,
                                  DomElement* element);

// ============================================================================
// Specificity Calculation
// ============================================================================

/**
 * Calculate specificity for a selector
 * @param matcher Selector matcher
 * @param selector Selector to analyze
 * @return Calculated specificity
 */
CssSpecificity selector_matcher_calculate_specificity(SelectorMatcher* matcher,
                                                      CssSelector* selector);

/**
 * Calculate specificity for a selector group (maximum specificity)
 * @param matcher Selector matcher
 * @param selector_group Selector group to analyze
 * @return Maximum specificity in the group
 */
CssSpecificity selector_matcher_calculate_group_specificity(SelectorMatcher* matcher,
                                                            CssSelectorGroup* selector_group);

// ============================================================================
// Performance and Statistics
// ============================================================================

/**
 * Get selector matcher statistics
 * @param matcher Selector matcher
 * @param total_matches Output: total match attempts
 * @param cache_hits Output: cache hits
 * @param cache_misses Output: cache misses
 * @param hit_rate Output: cache hit rate (0.0 to 1.0)
 */
void selector_matcher_get_statistics(SelectorMatcher* matcher,
                                     uint64_t* total_matches,
                                     uint64_t* cache_hits,
                                     uint64_t* cache_misses,
                                     double* hit_rate);

/**
 * Reset selector matcher statistics
 * @param matcher Selector matcher
 */
void selector_matcher_reset_statistics(SelectorMatcher* matcher);

/**
 * Print selector matcher information for debugging
 * @param matcher Selector matcher
 */
void selector_matcher_print_info(SelectorMatcher* matcher);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Check if two elements have the same tag name
 * @param element1 First element
 * @param element2 Second element
 * @return true if tag names match (case-insensitive)
 */
bool selector_matcher_same_tag(DomElement* element1, DomElement* element2);

/**
 * Parse an nth-child formula string (e.g., "2n+1", "odd", "even")
 * @param formula_str Formula string
 * @param formula Output: parsed formula
 * @return true on success, false on parse error
 */
bool selector_matcher_parse_nth_formula(const char* formula_str, CssNthFormula* formula);

/**
 * Convert pseudo-class name to state flag
 * @param pseudo_class Pseudo-class name (e.g., "hover", "focus")
 * @return Pseudo-state flag or 0 if unknown
 */
uint32_t selector_matcher_pseudo_class_to_flag(const char* pseudo_class);

/**
 * Get pseudo-class name from state flag
 * @param flag Pseudo-state flag
 * @return Pseudo-class name or NULL if unknown
 */
const char* selector_matcher_flag_to_pseudo_class(uint32_t flag);

#ifdef __cplusplus
}
#endif

#endif // SELECTOR_MATCHER_H
