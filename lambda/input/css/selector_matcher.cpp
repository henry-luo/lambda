#include "selector_matcher.hpp"
#include "../../../lib/hashmap.h"
#include "../../../lib/arraylist.h"
#include "../../../lib/str.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

// ============================================================================
// Helper Functions
// ============================================================================

// Case-insensitive string comparison — delegates to str_icmp
static int strcasecmp_local(const char* s1, const char* s2) {
    return str_icmp(s1, strlen(s1), s2, strlen(s2));
}

// Case-insensitive substring search — delegates to str_ifind
static bool contains_substring_case_insensitive(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    return str_ifind(haystack, strlen(haystack), needle, strlen(needle)) != STR_NPOS;
}

// ============================================================================
// Selector Matcher Creation and Destruction
// ============================================================================

SelectorMatcher* selector_matcher_create(Pool* pool) {
    if (!pool) {
        return NULL;
    }

    SelectorMatcher* matcher = (SelectorMatcher*)pool_calloc(pool, sizeof(SelectorMatcher));
    if (!matcher) {
        return NULL;
    }

    matcher->pool = pool;
    matcher->cache_enabled = false; // Disabled for now - can add HashMap caching later
    matcher->strict_mode = false;
    matcher->quirks_mode = false;
    matcher->case_sensitive_classes = true;  // Default: case-sensitive
    matcher->case_sensitive_attrs = true;    // Default: case-sensitive

    // Create match cache - disabled for now
    matcher->match_cache = NULL;
    matcher->selector_entry_cache = NULL;

    // Initialize statistics
    matcher->total_matches = 0;
    matcher->cache_hits = 0;
    matcher->cache_misses = 0;

    // Initialize bloom filter (simple implementation) - disabled for now
    matcher->bloom_filter_size = 0;
    matcher->bloom_filter = NULL;

    return matcher;
}

void selector_matcher_destroy(SelectorMatcher* matcher) {
    if (!matcher) {
        return;
    }

    // Note: Memory is pool-allocated, so it will be freed when pool is destroyed
}

void selector_matcher_clear_cache(SelectorMatcher* matcher) {
    if (!matcher) {
        return;
    }

    // Reset statistics
    matcher->cache_hits = 0;
    matcher->cache_misses = 0;
}

void selector_matcher_set_cache_enabled(SelectorMatcher* matcher, bool enabled) {
    if (matcher) {
        matcher->cache_enabled = enabled;
    }
}

void selector_matcher_set_quirks_mode(SelectorMatcher* matcher, bool quirks) {
    if (matcher) {
        matcher->quirks_mode = quirks;
        // In quirks mode, class and attribute matching is case-insensitive
        if (quirks) {
            matcher->case_sensitive_classes = false;
            matcher->case_sensitive_attrs = false;
        } else {
            matcher->case_sensitive_classes = true;
            matcher->case_sensitive_attrs = true;
        }
    }
}

void selector_matcher_set_case_sensitive_classes(SelectorMatcher* matcher, bool case_sensitive) {
    if (matcher) {
        matcher->case_sensitive_classes = case_sensitive;
    }
}

void selector_matcher_set_case_sensitive_attributes(SelectorMatcher* matcher, bool case_sensitive) {
    if (matcher) {
        matcher->case_sensitive_attrs = case_sensitive;
    }
}

SelectorEntry* selector_matcher_get_entry(SelectorMatcher* matcher, CssSimpleSelector* selector) {
    if (!matcher || !selector) {
        return NULL;
    }

    // For now, create a temporary entry on the stack
    // In a full implementation, this would cache entries in matcher->selector_entry_cache
    SelectorEntry* entry = (SelectorEntry*)pool_alloc(matcher->pool, sizeof(SelectorEntry));
    if (!entry) {
        return NULL;
    }

    entry->selector = selector;
    entry->cached_tag_ptr = NULL;
    entry->cached_tag_id = 0;
    entry->use_count = 0;
    entry->cache_valid = false;

    return entry;
}

// ============================================================================
// Primary Matching Functions
// ============================================================================

bool selector_matcher_matches(SelectorMatcher* matcher,
                              CssSelector* selector,
                              DomElement* element,
                              MatchResult* result) {
    if (!matcher || !selector || !element) {
        return false;
    }

    matcher->total_matches++;

    // Initialize result
    MatchResult local_result = {
        .matches = false,
        .specificity = {0, 0, 0, 0, false},
        .pseudo_state_required = 0,
        .matches_with_pseudo = false,
        .pseudo_element = PSEUDO_ELEMENT_NONE
    };

    // Check for pseudo-element in the selector
    local_result.pseudo_element = selector_get_pseudo_element(selector);

    if (local_result.pseudo_element != PSEUDO_ELEMENT_NONE) {
        log_debug("selector_matcher: detected pseudo_element=%d for selector", local_result.pseudo_element);
    }

    // Check if selector is complex (has combinators)
    if (selector->compound_selector_count == 0) {
        if (result) *result = local_result;
        return false;
    }

    if (selector->compound_selector_count == 1) {
        // Simple case: single compound selector, no combinators
        local_result.matches = selector_matcher_matches_compound(
            matcher,
            selector->compound_selectors[0],
            element
        );

        if (local_result.matches) {
            local_result.specificity = selector->specificity;
        }
    } else {
        // Complex case: multiple compound selectors with combinators
        // We need to match the rightmost selector to the element,
        // then verify the combinators working backwards

        CssCompoundSelector* rightmost = selector->compound_selectors[selector->compound_selector_count - 1];

        if (!selector_matcher_matches_compound(matcher, rightmost, element)) {
            if (result) *result = local_result;
            return false;
        }

        // Now verify combinators from right to left
        DomElement* current_element = element;
        bool all_match = true;

        for (int i = selector->compound_selector_count - 2; i >= 0; i--) {
            CssCombinator combinator = selector->combinators[i];
            CssCompoundSelector* left = selector->compound_selectors[i];

            bool combinator_matches = false;

            switch (combinator) {
                case CSS_COMBINATOR_DESCENDANT:
                    combinator_matches = selector_matcher_has_ancestor(matcher, left, current_element);
                    break;

                case CSS_COMBINATOR_CHILD:
                    combinator_matches = selector_matcher_has_parent(matcher, left, current_element);
                    if (combinator_matches && current_element->parent) {
                        current_element = static_cast<DomElement*>(current_element->parent);
                    }
                    break;

                case CSS_COMBINATOR_NEXT_SIBLING:
                    combinator_matches = selector_matcher_has_prev_sibling(matcher, left, current_element);
                    if (combinator_matches) {
                        // Find previous element sibling (skip text nodes)
                        DomNode* prev_node = current_element->prev_sibling;
                        while (prev_node && !prev_node->is_element()) {
                            prev_node = prev_node->prev_sibling;
                        }
                        if (prev_node) {
                            current_element = static_cast<DomElement*>(prev_node);
                        }
                    }
                    break;

                case CSS_COMBINATOR_SUBSEQUENT_SIBLING:
                    combinator_matches = selector_matcher_has_preceding_sibling(matcher, left, current_element);
                    break;

                default:
                    combinator_matches = false;
                    break;
            }

            if (!combinator_matches) {
                all_match = false;
                break;
            }
        }

        local_result.matches = all_match;
        if (all_match) {
            local_result.specificity = selector->specificity;
        }
    }

    if (result) {
        *result = local_result;
    }

    return local_result.matches;
}

bool selector_matcher_matches_group(SelectorMatcher* matcher,
                                    CssSelectorGroup* selector_group,
                                    DomElement* element,
                                    MatchResult* result) {
    if (!matcher || !selector_group || !element) {
        return false;
    }

    MatchResult best_result = {
        .matches = false,
        .specificity = {0, 0, 0, 0, false},
        .pseudo_state_required = 0,
        .matches_with_pseudo = false,
        .pseudo_element = PSEUDO_ELEMENT_NONE
    };

    // Try each selector in the group
    for (size_t i = 0; i < selector_group->selector_count; i++) {
        MatchResult current_result;
        if (selector_matcher_matches(matcher, selector_group->selectors[i], element, &current_result)) {
            if (!best_result.matches ||
                css_specificity_compare(current_result.specificity, best_result.specificity) > 0) {
                best_result = current_result;
            }
        }
    }

    if (result) {
        *result = best_result;
    }

    return best_result.matches;
}

// Helper for find_all - recursive tree traversal
static void traverse_and_collect_matches(SelectorMatcher* matcher,
                                         CssSelector* selector,
                                         DomElement* element,
                                         ArrayList* matched) {
    if (!element) return;

    // Check if current element matches
    if (selector_matcher_matches(matcher, selector, element, NULL)) {
        arraylist_append(matched, element);
    }

    // Traverse children (only element nodes)
    DomNode* child_node = element->first_child;
    while (child_node) {
        if (child_node->is_element()) {
            traverse_and_collect_matches(matcher, selector, static_cast<DomElement*>(child_node), matched);
        }
        child_node = child_node->next_sibling;
    }
}

bool selector_matcher_find_all(SelectorMatcher* matcher,
                               CssSelector* selector,
                               DomElement* root,
                               DomElement*** results,
                               int* count) {
    if (!matcher || !selector || !root || !results || !count) {
        return false;
    }

    // Use an ArrayList to collect results
    ArrayList* matched = arraylist_new(16); // Initial capacity
    if (!matched) {
        return false;
    }

    // Recursive tree traversal
    traverse_and_collect_matches(matcher, selector, root, matched);

    // Convert ArrayList to array
    *count = matched->length;
    if (*count > 0) {
        *results = (DomElement**)pool_alloc(matcher->pool, *count * sizeof(DomElement*));
        if (!*results) {
            arraylist_free(matched);
            return false;
        }

        for (int i = 0; i < *count; i++) {
            (*results)[i] = (DomElement*)matched->data[i];
        }
    } else {
        *results = NULL;
    }

    arraylist_free(matched);
    return true;
}

// Helper for find_first - recursive tree traversal with early exit
static DomElement* traverse_and_find_first_match(SelectorMatcher* matcher,
                                                 CssSelector* selector,
                                                 DomElement* element) {
    if (!element) return NULL;

    // Check if current element matches
    if (selector_matcher_matches(matcher, selector, element, NULL)) {
        return element;
    }

    // Traverse children - need to check node type since first_child is void*
    DomNode* child_node = element->first_child;
    while (child_node) {
        // check if this is an element node (not text or comment)
        if (child_node->is_element()) {
            DomElement* child = child_node->as_element();
            DomElement* found = traverse_and_find_first_match(matcher, selector, child);
            if (found) return found;
            child_node = child->next_sibling;
        } else {
            // skip text/comment nodes - get their next sibling
            child_node = child_node->next_sibling;
        }
    }

    return NULL;
}

DomElement* selector_matcher_find_first(SelectorMatcher* matcher,
                                        CssSelector* selector,
                                        DomElement* root) {
    if (!matcher || !selector || !root) {
        return NULL;
    }

    // Recursive tree traversal with early exit
    return traverse_and_find_first_match(matcher, selector, root);
}

// ============================================================================
// Selector Component Matching
// ============================================================================

bool selector_matcher_matches_simple(SelectorMatcher* matcher,
                                     CssSimpleSelector* simple_selector,
                                     DomElement* element) {
    if (!matcher || !simple_selector || !element) {
        return false;
    }

    matcher->total_matches++;

    switch (simple_selector->type) {
        case CSS_SELECTOR_TYPE_ELEMENT:
            // Match element type
            if (simple_selector->value) {
                // safety check for NULL or invalid tag_name
                if (!element->tag_name || (uintptr_t)element->tag_name < 0x1000) {
                    log_error("Invalid tag_name pointer in element: %p", element->tag_name);
                    return false;
                }
                // Use case-insensitive comparison for HTML element names (standard)
                return strcasecmp_local(element->tag_name, simple_selector->value) == 0;
            }
            return true; // No type specified matches any element

        case CSS_SELECTOR_TYPE_CLASS:
            // Match class - with case sensitivity based on configuration
            if (!simple_selector->value) return false;
            for (int i = 0; i < element->class_count; i++) {
                int cmp = matcher->case_sensitive_classes
                    ? strcmp(element->class_names[i], simple_selector->value)
                    : strcasecmp_local(element->class_names[i], simple_selector->value);
                if (cmp == 0) return true;
            }
            return false;

        case CSS_SELECTOR_TYPE_ID:
            // Match ID
            return element->id && strcmp(element->id, simple_selector->value) == 0;

        case CSS_SELECTOR_TYPE_UNIVERSAL:
            // Universal selector matches everything
            return true;

        case CSS_SELECTOR_ATTR_EXISTS:
            // Attribute exists
            return dom_element_has_attribute(element, simple_selector->attribute.name);

        case CSS_SELECTOR_ATTR_EXACT:
        case CSS_SELECTOR_ATTR_CONTAINS:
        case CSS_SELECTOR_ATTR_BEGINS:
        case CSS_SELECTOR_ATTR_ENDS:
        case CSS_SELECTOR_ATTR_SUBSTRING:
        case CSS_SELECTOR_ATTR_LANG:
        case CSS_SELECTOR_ATTR_CASE_INSENSITIVE:
        case CSS_SELECTOR_ATTR_CASE_SENSITIVE:
            {
                // Use case insensitivity from selector or matcher configuration
                bool case_insensitive = simple_selector->attribute.case_insensitive
                    || !matcher->case_sensitive_attrs;
                return selector_matcher_matches_attribute(
                    matcher,
                    simple_selector->attribute.name,
                    simple_selector->attribute.value,
                    simple_selector->type,
                    case_insensitive,
                    element
                );
            }

        // Pseudo-elements (::before, ::after, etc.) - always return true
        // The caller should check for pseudo-elements separately and track them
        case CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE:
        case CSS_SELECTOR_PSEUDO_ELEMENT_AFTER:
        case CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LINE:
        case CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LETTER:
        case CSS_SELECTOR_PSEUDO_ELEMENT_SELECTION:
        case CSS_SELECTOR_PSEUDO_ELEMENT_BACKDROP:
        case CSS_SELECTOR_PSEUDO_ELEMENT_PLACEHOLDER:
        case CSS_SELECTOR_PSEUDO_ELEMENT_MARKER:
        case CSS_SELECTOR_PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON:
            // Pseudo-elements are matched at a higher level (compound/selector matching)
            // Here we just return true to not block the match
            return true;

        // Pseudo-classes
        default:
            if (simple_selector->type >= CSS_SELECTOR_PSEUDO_ROOT &&
                simple_selector->type <= CSS_SELECTOR_PSEUDO_OUT_OF_RANGE) {
                return selector_matcher_matches_pseudo_class(
                    matcher,
                    simple_selector->type,
                    simple_selector->argument,
                    element
                );
            }
            return false;
    }
}

// Helper function to detect pseudo-element in a compound selector
static PseudoElementType get_pseudo_element_from_compound(CssCompoundSelector* compound) {
    if (!compound) return PSEUDO_ELEMENT_NONE;

    for (size_t i = 0; i < compound->simple_selector_count; i++) {
        CssSimpleSelector* simple = compound->simple_selectors[i];
        if (!simple) continue;

        switch (simple->type) {
            case CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE:
                return PSEUDO_ELEMENT_BEFORE;
            case CSS_SELECTOR_PSEUDO_ELEMENT_AFTER:
                return PSEUDO_ELEMENT_AFTER;
            case CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LINE:
                return PSEUDO_ELEMENT_FIRST_LINE;
            case CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LETTER:
                return PSEUDO_ELEMENT_FIRST_LETTER;
            case CSS_SELECTOR_PSEUDO_ELEMENT_SELECTION:
                return PSEUDO_ELEMENT_SELECTION;
            case CSS_SELECTOR_PSEUDO_ELEMENT_MARKER:
                return PSEUDO_ELEMENT_MARKER;
            case CSS_SELECTOR_PSEUDO_ELEMENT_PLACEHOLDER:
                return PSEUDO_ELEMENT_PLACEHOLDER;
            default:
                break;
        }
    }

    return PSEUDO_ELEMENT_NONE;
}

// Helper function to detect pseudo-element in a selector
PseudoElementType selector_get_pseudo_element(CssSelector* selector) {
    if (!selector || selector->compound_selector_count == 0) {
        return PSEUDO_ELEMENT_NONE;
    }

    // Pseudo-element should be in the rightmost compound selector
    CssCompoundSelector* rightmost = selector->compound_selectors[selector->compound_selector_count - 1];
    return get_pseudo_element_from_compound(rightmost);
}

bool selector_matcher_matches_compound(SelectorMatcher* matcher,
                                       CssCompoundSelector* compound_selector,
                                       DomElement* element) {
    if (!matcher || !compound_selector || !element) {
        return false;
    }

    // All simple selectors in the compound must match
    for (size_t i = 0; i < compound_selector->simple_selector_count; i++) {
        if (!selector_matcher_matches_simple(matcher, compound_selector->simple_selectors[i], element)) {
            return false;
        }
    }

    return true;
}
bool selector_matcher_matches_attribute(SelectorMatcher* matcher,
                                        const char* attr_name,
                                        const char* attr_value,
                                        CssSelectorType attr_type,
                                        bool case_insensitive,
                                        DomElement* element) {
    if (!matcher || !attr_name || !element) {
        return false;
    }

    // For existence check (no value specified), use has_attribute
    // This handles the case where attribute exists but has empty value (stored as null)
    if (!attr_value || attr_type == CSS_SELECTOR_ATTR_EXISTS) {
        return dom_element_has_attribute(element, attr_name);
    }

    const char* element_attr = dom_element_get_attribute(element, attr_name);
    if (!element_attr) {
        // Attribute exists but has empty/null value - can't match any value-based selector
        return false;
    }

    // Determine comparison function - respect both parameter AND matcher configuration
    bool use_case_insensitive = case_insensitive || !matcher->case_sensitive_attrs;
    int (*compare_func)(const char*, const char*) = use_case_insensitive ? strcasecmp_local : strcmp;

    switch (attr_type) {
        case CSS_SELECTOR_ATTR_EXACT:
            // [attr="value"] - exact match
            return compare_func(element_attr, attr_value) == 0;

        case CSS_SELECTOR_ATTR_CONTAINS:
            // [attr~="value"] - space-separated list contains value
            {
                size_t value_len = strlen(attr_value);
                const char* pos = element_attr;
                while (*pos) {
                    // Skip whitespace
                    while (*pos && isspace(*pos)) pos++;
                    if (!*pos) break;

                    // Check if this word matches
                    const char* word_start = pos;
                    while (*pos && !isspace(*pos)) pos++;

                    size_t word_len = pos - word_start;
                    if (word_len == value_len) {
                        if (use_case_insensitive) {
                            if (str_ieq(word_start, value_len, attr_value, value_len)) {
                                return true;
                            }
                        } else {
                            if (strncmp(word_start, attr_value, value_len) == 0) {
                                return true;
                            }
                        }
                    }
                }
                return false;
            }

        case CSS_SELECTOR_ATTR_BEGINS:
            // [attr^="value"] - begins with
            {
                size_t value_len = strlen(attr_value);
                if (use_case_insensitive) {
                    return str_istarts_with(element_attr, strlen(element_attr), attr_value, value_len);
                } else {
                    return strncmp(element_attr, attr_value, value_len) == 0;
                }
            }

        case CSS_SELECTOR_ATTR_ENDS:
            // [attr$="value"] - ends with
            {
                size_t attr_len = strlen(element_attr);
                size_t value_len = strlen(attr_value);
                if (value_len > attr_len) {
                    return false;
                }
                const char* suffix = element_attr + (attr_len - value_len);
                return compare_func(suffix, attr_value) == 0;
            }

        case CSS_SELECTOR_ATTR_SUBSTRING:
            // [attr*="value"] - contains substring
            if (use_case_insensitive) {
                return contains_substring_case_insensitive(element_attr, attr_value);
            } else {
                return strstr(element_attr, attr_value) != NULL;
            }

        case CSS_SELECTOR_ATTR_LANG:
            // [attr|="value"] - language prefix match
            {
                size_t value_len = strlen(attr_value);
                if (strncmp(element_attr, attr_value, value_len) == 0) {
                    // Must be exact match or followed by hyphen
                    return element_attr[value_len] == '\0' || element_attr[value_len] == '-';
                }
                return false;
            }

        default:
            return false;
    }
}

// ============================================================================
// Pseudo-Class Matching
// ============================================================================

bool selector_matcher_matches_pseudo_class(SelectorMatcher* matcher,
                                           CssSelectorType pseudo_type,
                                           const char* pseudo_arg,
                                           DomElement* element) {
    if (!matcher || !element) {
        return false;
    }

    switch (pseudo_type) {
        // User interaction pseudo-classes
        case CSS_SELECTOR_PSEUDO_HOVER:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_HOVER);
        case CSS_SELECTOR_PSEUDO_ACTIVE:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_ACTIVE);
        case CSS_SELECTOR_PSEUDO_FOCUS:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_FOCUS);
        case CSS_SELECTOR_PSEUDO_FOCUS_VISIBLE:
            // :focus-visible matches when focused via keyboard navigation
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_FOCUS_VISIBLE);
        case CSS_SELECTOR_PSEUDO_FOCUS_WITHIN:
            // :focus-within matches when element or any descendant has focus
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_FOCUS_WITHIN);
        case CSS_SELECTOR_PSEUDO_VISITED:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_VISITED);
        case CSS_SELECTOR_PSEUDO_LINK:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_LINK);
        case CSS_SELECTOR_PSEUDO_TARGET:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_TARGET);

        // Form pseudo-classes
        case CSS_SELECTOR_PSEUDO_ENABLED:
            // :enabled matches when NOT disabled
            return !dom_element_has_pseudo_state(element, PSEUDO_STATE_DISABLED);
        case CSS_SELECTOR_PSEUDO_DISABLED:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_DISABLED);
        case CSS_SELECTOR_PSEUDO_CHECKED:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_CHECKED);
        case CSS_SELECTOR_PSEUDO_REQUIRED:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_REQUIRED);
        case CSS_SELECTOR_PSEUDO_OPTIONAL:
            // :optional matches when NOT required
            return !dom_element_has_pseudo_state(element, PSEUDO_STATE_REQUIRED);
        case CSS_SELECTOR_PSEUDO_VALID:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_VALID);
        case CSS_SELECTOR_PSEUDO_INVALID:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_INVALID);
        case CSS_SELECTOR_PSEUDO_READ_ONLY:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_READ_ONLY);
        case CSS_SELECTOR_PSEUDO_READ_WRITE:
            // :read-write matches when NOT read-only
            return !dom_element_has_pseudo_state(element, PSEUDO_STATE_READ_ONLY);
        case CSS_SELECTOR_PSEUDO_PLACEHOLDER_SHOWN:
            return dom_element_has_pseudo_state(element, PSEUDO_STATE_PLACEHOLDER_SHOWN);

        // Structural pseudo-classes
        case CSS_SELECTOR_PSEUDO_ROOT:
        case CSS_SELECTOR_PSEUDO_EMPTY:
        case CSS_SELECTOR_PSEUDO_FIRST_CHILD:
        case CSS_SELECTOR_PSEUDO_LAST_CHILD:
        case CSS_SELECTOR_PSEUDO_ONLY_CHILD:
        case CSS_SELECTOR_PSEUDO_FIRST_OF_TYPE:
        case CSS_SELECTOR_PSEUDO_LAST_OF_TYPE:
        case CSS_SELECTOR_PSEUDO_ONLY_OF_TYPE:
            return selector_matcher_matches_structural(matcher, pseudo_type, element);

        // nth-child pseudo-classes
        case CSS_SELECTOR_PSEUDO_NTH_CHILD:
        case CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD:
        case CSS_SELECTOR_PSEUDO_NTH_OF_TYPE:
        case CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE:
            if (pseudo_arg) {
                CssNthFormula formula;
                if (selector_matcher_parse_nth_formula(pseudo_arg, &formula)) {
                    bool from_end = (pseudo_type == CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD ||
                                    pseudo_type == CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE);
                    bool result = selector_matcher_matches_nth_child(matcher, &formula, element, from_end);
                    return result;
                }
            }
            return false;

        default:
            return false;
    }
}

bool selector_matcher_matches_structural(SelectorMatcher* matcher,
                                         CssSelectorType pseudo_type,
                                         DomElement* element) {
    if (!matcher || !element) {
        return false;
    }

    switch (pseudo_type) {
        case CSS_SELECTOR_PSEUDO_ROOT: {
            bool is_root = element->parent == NULL;
            log_debug("[SELECTOR] :root check for <%s>: parent=%p, is_root=%d",
                      element->tag_name, element->parent, is_root);
            return is_root;
        }

        case CSS_SELECTOR_PSEUDO_EMPTY:
            return element->first_child == NULL;

        case CSS_SELECTOR_PSEUDO_FIRST_CHILD:
            return dom_element_is_first_child(element);

        case CSS_SELECTOR_PSEUDO_LAST_CHILD:
            return dom_element_is_last_child(element);

        case CSS_SELECTOR_PSEUDO_ONLY_CHILD:
            return dom_element_is_only_child(element);

        case CSS_SELECTOR_PSEUDO_FIRST_OF_TYPE:
            // First of its type among siblings
            if (!element->parent) return true;
            {
                DomElement* parent = static_cast<DomElement*>(element->parent);
                DomNode* sibling_node = parent->first_child;
                while (sibling_node) {
                    if (sibling_node->is_element()) {
                        DomElement* sibling = static_cast<DomElement*>(sibling_node);
                        if (selector_matcher_same_tag(sibling, element)) {
                            return sibling == element;
                        }
                    }
                    sibling_node = sibling_node->next_sibling;
                }
            }
            return false;

        case CSS_SELECTOR_PSEUDO_LAST_OF_TYPE:
            // Last of its type among siblings
            if (!element->parent) return true;
            {
                DomElement* parent = static_cast<DomElement*>(element->parent);
                DomNode* sibling_node = parent->first_child;
                DomElement* last_of_type = NULL;
                while (sibling_node) {
                    if (sibling_node->is_element()) {
                        DomElement* sibling = static_cast<DomElement*>(sibling_node);
                        if (selector_matcher_same_tag(sibling, element)) {
                            last_of_type = sibling;
                        }
                    }
                    sibling_node = sibling_node->next_sibling;
                }
                return last_of_type == element;
            }

        case CSS_SELECTOR_PSEUDO_ONLY_OF_TYPE:
            // Only element of its type among siblings
            if (!element->parent) return true;
            {
                int count = 0;
                DomElement* parent = static_cast<DomElement*>(element->parent);
                DomNode* sibling_node = parent->first_child;
                while (sibling_node) {
                    if (sibling_node->is_element()) {
                        DomElement* sibling = static_cast<DomElement*>(sibling_node);
                        if (selector_matcher_same_tag(sibling, element)) {
                            count++;
                            if (count > 1) return false;
                        }
                    }
                    sibling_node = sibling_node->next_sibling;
                }
                return count == 1;
            }

        default:
            return false;
    }
}

bool selector_matcher_matches_nth_child(SelectorMatcher* matcher,
                                        CssNthFormula* formula,
                                        DomElement* element,
                                        bool from_end) {
    if (!matcher || !formula || !element) {
        return false;
    }

    // Handle general an+b formula (including special odd/even cases)
    if (from_end) {
        // For nth-last-child, we need to count from the end
        int total_children = element->parent ? dom_element_count_child_elements(static_cast<DomElement*>(element->parent)) : 1;
        int index = dom_element_get_child_index(element);
        int reverse_index = total_children - index;

        // Handle special cases with reverse counting
        int a = formula->a;
        int b = formula->b;
        if (formula->odd) {
            a = 2;
            b = 1;
        } else if (formula->even) {
            a = 2;
            b = 0;
        }

        if (a == 0) {
            return reverse_index == b;
        }

        int diff = reverse_index - b;
        if (diff < 0) return false;
        return (diff % a) == 0;
    } else {
        // For nth-child, use forward counting
        if (formula->odd) {
            return dom_element_matches_nth_child(element, 2, 1);
        }
        if (formula->even) {
            return dom_element_matches_nth_child(element, 2, 0);
        }
        return dom_element_matches_nth_child(element, formula->a, formula->b);
    }
}

// ============================================================================
// Combinator Matching
// ============================================================================

bool selector_matcher_matches_combinator(SelectorMatcher* matcher,
                                         CssCompoundSelector* left_selector,
                                         CssCombinator combinator,
                                         CssCompoundSelector* right_selector,
                                         DomElement* element) {
    if (!matcher || !left_selector || !right_selector || !element) {
        return false;
    }

    // Element should match right selector
    if (!selector_matcher_matches_compound(matcher, right_selector, element)) {
        return false;
    }

    // Check combinator relationship
    switch (combinator) {
        case CSS_COMBINATOR_DESCENDANT:
            return selector_matcher_has_ancestor(matcher, left_selector, element);
        case CSS_COMBINATOR_CHILD:
            return selector_matcher_has_parent(matcher, left_selector, element);
        case CSS_COMBINATOR_NEXT_SIBLING:
            return selector_matcher_has_prev_sibling(matcher, left_selector, element);
        case CSS_COMBINATOR_SUBSEQUENT_SIBLING:
            return selector_matcher_has_preceding_sibling(matcher, left_selector, element);
        default:
            return false;
    }
}

bool selector_matcher_has_ancestor(SelectorMatcher* matcher,
                                   CssCompoundSelector* selector,
                                   DomElement* element) {
    if (!matcher || !selector || !element) {
        return false;
    }

    DomElement* ancestor = static_cast<DomElement*>(element->parent);
    while (ancestor) {
        if (selector_matcher_matches_compound(matcher, selector, ancestor)) {
            return true;
        }
        ancestor = static_cast<DomElement*>(ancestor->parent);
    }

    return false;
}

bool selector_matcher_has_parent(SelectorMatcher* matcher,
                                 CssCompoundSelector* selector,
                                 DomElement* element) {
    if (!matcher || !selector || !element || !element->parent) {
        return false;
    }

    return selector_matcher_matches_compound(matcher, selector, static_cast<DomElement*>(element->parent));
}

bool selector_matcher_has_prev_sibling(SelectorMatcher* matcher,
                                       CssCompoundSelector* selector,
                                       DomElement* element) {
    if (!matcher || !selector || !element) {
        return false;
    }

    // Find the previous element sibling (skip text nodes, comments)
    DomNode* sibling = element->prev_sibling;
    while (sibling && !sibling->is_element()) {
        sibling = sibling->prev_sibling;
    }
    if (!sibling) return false;

    return selector_matcher_matches_compound(matcher, selector, static_cast<DomElement*>(sibling));
}

bool selector_matcher_has_preceding_sibling(SelectorMatcher* matcher,
                                            CssCompoundSelector* selector,
                                            DomElement* element) {
    if (!matcher || !selector || !element) {
        return false;
    }

    DomNode* sibling_node = element->prev_sibling;
    while (sibling_node) {
        if (sibling_node->is_element()) {
            DomElement* sibling = static_cast<DomElement*>(sibling_node);
            if (selector_matcher_matches_compound(matcher, selector, sibling)) {
                return true;
            }
        }
        sibling_node = sibling_node->prev_sibling;
    }

    return false;
}

// ============================================================================
// CSS4 Advanced Selectors
// ============================================================================

bool selector_matcher_matches_is(SelectorMatcher* matcher,
                                 CssSelector** selectors,
                                 int count,
                                 DomElement* element) {
    if (!matcher || !selectors || count <= 0 || !element) {
        return false;
    }

    // Match if ANY selector matches
    for (int i = 0; i < count; i++) {
        if (selector_matcher_matches(matcher, selectors[i], element, NULL)) {
            return true;
        }
    }

    return false;
}

bool selector_matcher_matches_where(SelectorMatcher* matcher,
                                    CssSelector** selectors,
                                    int count,
                                    DomElement* element) {
    // :where() has the same matching logic as :is(), just different specificity
    return selector_matcher_matches_is(matcher, selectors, count, element);
}

bool selector_matcher_matches_not(SelectorMatcher* matcher,
                                  CssSelector** selectors,
                                  int count,
                                  DomElement* element) {
    if (!matcher || !selectors || count <= 0 || !element) {
        return false;
    }

    // Match if NONE of the selectors match
    for (int i = 0; i < count; i++) {
        if (selector_matcher_matches(matcher, selectors[i], element, NULL)) {
            return false;
        }
    }

    return true;
}

bool selector_matcher_matches_has(SelectorMatcher* matcher,
                                  CssSelector** selectors,
                                  int count,
                                  DomElement* element) {
    if (!matcher || !selectors || count <= 0 || !element) {
        return false;
    }

    // Check if element has any descendant matching any of the selectors
    for (int i = 0; i < count; i++) {
        DomElement* match = selector_matcher_find_first(matcher, selectors[i], element);
        if (match && match != element) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Specificity Calculation
// ============================================================================

CssSpecificity selector_matcher_calculate_specificity(SelectorMatcher* matcher,
                                                      CssSelector* selector) {
    if (!matcher || !selector) {
        CssSpecificity zero = {0, 0, 0, 0, false};
        return zero;
    }

    // If already calculated, return cached value
    if (selector->specificity.inline_style != 0 ||
        selector->specificity.ids != 0 ||
        selector->specificity.classes != 0 ||
        selector->specificity.elements != 0) {
        return selector->specificity;
    }

    CssSpecificity spec = {0, 0, 0, 0, false};

    // Sum specificity for all compound selectors
    for (size_t i = 0; i < selector->compound_selector_count; i++) {
        CssCompoundSelector* compound = selector->compound_selectors[i];

        for (size_t j = 0; j < compound->simple_selector_count; j++) {
            CssSimpleSelector* simple = compound->simple_selectors[j];

            switch (simple->type) {
                case CSS_SELECTOR_TYPE_ID:
                    spec.ids++;
                    break;

                case CSS_SELECTOR_TYPE_CLASS:
                case CSS_SELECTOR_ATTR_EXACT:
                case CSS_SELECTOR_ATTR_CONTAINS:
                case CSS_SELECTOR_ATTR_BEGINS:
                case CSS_SELECTOR_ATTR_ENDS:
                case CSS_SELECTOR_ATTR_SUBSTRING:
                case CSS_SELECTOR_ATTR_LANG:
                case CSS_SELECTOR_ATTR_EXISTS:
                    spec.classes++;
                    break;

                // Pseudo-classes count as classes
                case CSS_SELECTOR_PSEUDO_HOVER:
                case CSS_SELECTOR_PSEUDO_ACTIVE:
                case CSS_SELECTOR_PSEUDO_FOCUS:
                case CSS_SELECTOR_PSEUDO_VISITED:
                case CSS_SELECTOR_PSEUDO_LINK:
                case CSS_SELECTOR_PSEUDO_FIRST_CHILD:
                case CSS_SELECTOR_PSEUDO_LAST_CHILD:
                case CSS_SELECTOR_PSEUDO_NTH_CHILD:
                case CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD:
                    spec.classes++;
                    break;

                case CSS_SELECTOR_TYPE_ELEMENT:
                    spec.elements++;
                    break;

                // Universal selector and :where() don't add specificity
                case CSS_SELECTOR_TYPE_UNIVERSAL:
                case CSS_SELECTOR_PSEUDO_WHERE:
                    break;

                default:
                    break;
            }
        }
    }

    // Cache the calculated specificity in the selector
    selector->specificity = spec;
    log_debug("[SPECIFICITY] Calculated and cached specificity for selector: (%d, %d, %d, %d)",
              spec.inline_style, spec.ids, spec.classes, spec.elements);

    return spec;
}

CssSpecificity selector_matcher_calculate_group_specificity(SelectorMatcher* matcher,
                                                            CssSelectorGroup* selector_group) {
    if (!matcher || !selector_group) {
        CssSpecificity zero = {0, 0, 0, 0, false};
        return zero;
    }

    CssSpecificity max_spec = {0, 0, 0, 0, false};

    for (size_t i = 0; i < selector_group->selector_count; i++) {
        CssSpecificity spec = selector_matcher_calculate_specificity(matcher, selector_group->selectors[i]);
        if (css_specificity_compare(spec, max_spec) > 0) {
            max_spec = spec;
        }
    }

    return max_spec;
}

// ============================================================================
// Performance and Statistics
// ============================================================================

void selector_matcher_get_statistics(SelectorMatcher* matcher,
                                     uint64_t* total_matches,
                                     uint64_t* cache_hits,
                                     uint64_t* cache_misses,
                                     double* hit_rate) {
    if (!matcher) {
        if (total_matches) *total_matches = 0;
        if (cache_hits) *cache_hits = 0;
        if (cache_misses) *cache_misses = 0;
        if (hit_rate) *hit_rate = 0.0;
        return;
    }

    if (total_matches) *total_matches = matcher->total_matches;
    if (cache_hits) *cache_hits = matcher->cache_hits;
    if (cache_misses) *cache_misses = matcher->cache_misses;

    if (hit_rate) {
        if (matcher->total_matches > 0) {
            *hit_rate = (double)matcher->cache_hits / (double)matcher->total_matches;
        } else {
            *hit_rate = 0.0;
        }
    }
}

void selector_matcher_reset_statistics(SelectorMatcher* matcher) {
    if (!matcher) {
        return;
    }

    matcher->total_matches = 0;
    matcher->cache_hits = 0;
    matcher->cache_misses = 0;
}

void selector_matcher_print_info(SelectorMatcher* matcher) {
    if (!matcher) {
        printf("Selector Matcher: NULL\n");
        return;
    }

    printf("Selector Matcher:\n");
    printf("  Cache enabled: %s\n", matcher->cache_enabled ? "yes" : "no");
    printf("  Strict mode: %s\n", matcher->strict_mode ? "yes" : "no");
    printf("  Case-sensitive attributes: %s\n", matcher->case_sensitive_attrs ? "yes" : "no");
    printf("  Total matches: %llu\n", (unsigned long long)matcher->total_matches);
    printf("  Cache hits: %llu\n", (unsigned long long)matcher->cache_hits);
    printf("  Cache misses: %llu\n", (unsigned long long)matcher->cache_misses);

    if (matcher->total_matches > 0) {
        double hit_rate = (double)matcher->cache_hits / (double)matcher->total_matches;
        printf("  Cache hit rate: %.2f%%\n", hit_rate * 100.0);
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

bool selector_matcher_same_tag(DomElement* element1, DomElement* element2) {
    if (!element1 || !element2) {
        return false;
    }

    if (!element1->tag_name || !element2->tag_name) {
        return false;
    }

    return strcasecmp_local(element1->tag_name, element2->tag_name) == 0;
}

bool selector_matcher_parse_nth_formula(const char* formula_str, CssNthFormula* formula) {
    if (!formula_str || !formula) {
        return false;
    }

    // Initialize formula
    formula->a = 0;
    formula->b = 0;
    formula->odd = false;
    formula->even = false;

    // Trim whitespace
    while (*formula_str && isspace(*formula_str)) {
        formula_str++;
    }

    // Check for "odd" or "even"
    if (str_ieq_const(formula_str, strlen(formula_str), "odd")) {
        formula->odd = true;
        return true;
    }
    if (str_ieq_const(formula_str, strlen(formula_str), "even")) {
        formula->even = true;
        return true;
    }

    // Parse an+b format
    const char* p = formula_str;

    // Check for 'n' (which means 1n+0)
    if (*p == 'n' || *p == 'N') {
        formula->a = 1;
        formula->b = 0;
        p++;

        // Check for +b or -b
        while (*p && isspace(*p)) p++;
        if (*p == '+' || *p == '-') {
            formula->b = (int)str_to_int64_default(p, strlen(p), 0);
        }
        return true;
    }

    // Parse coefficient 'a'
    if (*p == '-') {
        formula->a = -1;
        p++;
    } else if (*p == '+') {
        formula->a = 1;
        p++;
    } else if (isdigit(*p)) {
        formula->a = (int)str_to_int64_default(p, strlen(p), 0);
        while (*p && isdigit(*p)) p++;
    } else {
        formula->a = 1;
    }

    // Skip whitespace
    while (*p && isspace(*p)) p++;

    // Check for 'n'
    if (*p == 'n' || *p == 'N') {
        p++;

        // Skip whitespace
        while (*p && isspace(*p)) p++;

        // Parse constant 'b'
        if (*p == '+' || *p == '-') {
            formula->b = (int)str_to_int64_default(p, strlen(p), 0);
        }
    } else {
        // No 'n', so this is just a number (0n+b)
        formula->b = formula->a;
        formula->a = 0;
    }

    return true;
}

uint32_t selector_matcher_pseudo_class_to_flag(const char* pseudo_class) {
    if (!pseudo_class) {
        return 0;
    }

    size_t pc_len = strlen(pseudo_class);
    if (str_ieq_const(pseudo_class, pc_len, "hover")) return PSEUDO_STATE_HOVER;
    if (str_ieq_const(pseudo_class, pc_len, "active")) return PSEUDO_STATE_ACTIVE;
    if (str_ieq_const(pseudo_class, pc_len, "focus")) return PSEUDO_STATE_FOCUS;
    if (str_ieq_const(pseudo_class, pc_len, "focus-visible")) return PSEUDO_STATE_FOCUS_VISIBLE;
    if (str_ieq_const(pseudo_class, pc_len, "focus-within")) return PSEUDO_STATE_FOCUS_WITHIN;
    if (str_ieq_const(pseudo_class, pc_len, "visited")) return PSEUDO_STATE_VISITED;
    if (str_ieq_const(pseudo_class, pc_len, "link")) return PSEUDO_STATE_LINK;
    if (str_ieq_const(pseudo_class, pc_len, "target")) return PSEUDO_STATE_TARGET;
    if (str_ieq_const(pseudo_class, pc_len, "enabled")) return PSEUDO_STATE_ENABLED;
    if (str_ieq_const(pseudo_class, pc_len, "disabled")) return PSEUDO_STATE_DISABLED;
    if (str_ieq_const(pseudo_class, pc_len, "checked")) return PSEUDO_STATE_CHECKED;
    if (str_ieq_const(pseudo_class, pc_len, "indeterminate")) return PSEUDO_STATE_INDETERMINATE;
    if (str_ieq_const(pseudo_class, pc_len, "valid")) return PSEUDO_STATE_VALID;
    if (str_ieq_const(pseudo_class, pc_len, "invalid")) return PSEUDO_STATE_INVALID;
    if (str_ieq_const(pseudo_class, pc_len, "required")) return PSEUDO_STATE_REQUIRED;
    if (str_ieq_const(pseudo_class, pc_len, "optional")) return PSEUDO_STATE_OPTIONAL;
    if (str_ieq_const(pseudo_class, pc_len, "read-only")) return PSEUDO_STATE_READ_ONLY;
    if (str_ieq_const(pseudo_class, pc_len, "read-write")) return PSEUDO_STATE_READ_WRITE;
    if (str_ieq_const(pseudo_class, pc_len, "placeholder-shown")) return PSEUDO_STATE_PLACEHOLDER_SHOWN;
    if (str_ieq_const(pseudo_class, pc_len, "selected")) return PSEUDO_STATE_SELECTED;
    if (str_ieq_const(pseudo_class, pc_len, "first-child")) return PSEUDO_STATE_FIRST_CHILD;
    if (str_ieq_const(pseudo_class, pc_len, "last-child")) return PSEUDO_STATE_LAST_CHILD;
    if (str_ieq_const(pseudo_class, pc_len, "only-child")) return PSEUDO_STATE_ONLY_CHILD;

    return 0;
}

const char* selector_matcher_flag_to_pseudo_class(uint32_t flag) {
    switch (flag) {
        case PSEUDO_STATE_HOVER: return "hover";
        case PSEUDO_STATE_ACTIVE: return "active";
        case PSEUDO_STATE_FOCUS: return "focus";
        case PSEUDO_STATE_FOCUS_VISIBLE: return "focus-visible";
        case PSEUDO_STATE_FOCUS_WITHIN: return "focus-within";
        case PSEUDO_STATE_VISITED: return "visited";
        case PSEUDO_STATE_LINK: return "link";
        case PSEUDO_STATE_TARGET: return "target";
        case PSEUDO_STATE_ENABLED: return "enabled";
        case PSEUDO_STATE_DISABLED: return "disabled";
        case PSEUDO_STATE_CHECKED: return "checked";
        case PSEUDO_STATE_INDETERMINATE: return "indeterminate";
        case PSEUDO_STATE_VALID: return "valid";
        case PSEUDO_STATE_INVALID: return "invalid";
        case PSEUDO_STATE_REQUIRED: return "required";
        case PSEUDO_STATE_OPTIONAL: return "optional";
        case PSEUDO_STATE_READ_ONLY: return "read-only";
        case PSEUDO_STATE_READ_WRITE: return "read-write";
        case PSEUDO_STATE_PLACEHOLDER_SHOWN: return "placeholder-shown";
        case PSEUDO_STATE_SELECTED: return "selected";
        case PSEUDO_STATE_FIRST_CHILD: return "first-child";
        case PSEUDO_STATE_LAST_CHILD: return "last-child";
        case PSEUDO_STATE_ONLY_CHILD: return "only-child";
        default: return NULL;
    }
}
