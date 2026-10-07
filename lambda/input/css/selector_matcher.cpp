#include "selector_matcher.hpp"
#include "../../core/well_known_markup_names.h"
#include "../../../lib/hashmap.h"
#include "../../../lib/arraylist.h"
#include "../../../lib/mem.h"
#include "../../../lib/str.h"
#include "../../../lib/url.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

// CSS-only targets do not host a custom-element registry.
extern "C" __attribute__((weak)) bool dom_css_custom_element_defined(const char* /*name*/) {
    return false;
}
extern "C" __attribute__((weak)) bool dom_css_element_is_default(void* /*element*/) {
    return false;
}
extern "C" __attribute__((weak)) bool dom_css_element_is_indeterminate(void* /*element*/) {
    return false;
}
extern "C" __attribute__((weak)) bool dom_css_element_matches_range(
        void* /*element*/, bool /*out_of_range*/) {
    return false;
}
extern "C" __attribute__((weak)) int dom_css_element_matches_validity(
        void* /*element*/, bool /*invalid*/, bool /*user*/) {
    return -1;
}
extern "C" __attribute__((weak)) int dom_css_element_placeholder_shown(
        void* /*element*/) {
    return -1;
}

// ============================================================================
// Helper Functions
// ============================================================================

// Case-insensitive string comparison — delegates to str_icmp_cstr.

// Case-insensitive substring search — str_ifind handles the bounded comparison.

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

    selector_matcher_init(matcher, pool);
    return matcher;
}

void selector_matcher_init(SelectorMatcher* matcher, Pool* pool) {
    *matcher = {};
    matcher->pool = pool;
    matcher->cache_enabled = false; // Disabled for now - can add HashMap caching later
    matcher->strict_mode = false;
    matcher->quirks_mode = false;
    matcher->case_sensitive_classes = true;  // Default: case-sensitive
    matcher->case_sensitive_attrs = true;    // Default: case-sensitive
    matcher->pseudo_state_resolver = NULL;
    matcher->pseudo_state_context = NULL;

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

}

bool SelectorQueryScratch::parse_list(const char* text) {
    if (!ensure_pool() || !text || !*text) return false;
    group = css_parse_selector_group_text(text, strlen(text), pool);
    return group && !css_selector_group_contains_generic_pseudo(group);
}

void selector_matcher_set_scope_element(SelectorMatcher* matcher, DomElement* scope_element) {
    if (!matcher) return;
    matcher->scope_element = scope_element;
}

void css_scope_visit_roots(CssRule* rule, DomElement* element, SelectorMatcher* matcher,
                           CssScopeRootVisitor visitor, void* context) {
    if (!rule || rule->type != CSS_RULE_SCOPE || rule->data.conditional_rule.invalid_scope ||
        !element || !matcher || !visitor) return;
    DomElement* outer = matcher->scope_element;
    DomElement* implicit = rule->stylesheet && rule->stylesheet->owner_element
        ? dom_parent_element(rule->stylesheet->owner_element) : nullptr;
    if (!implicit && element->doc) implicit = element->doc->root;
    uint32_t hops = 0;
    for (DomElement* root = element; root; root = dom_parent_element(root), hops++) {
        CssSelectorGroup* start = rule->data.conditional_rule.scope_start;
        matcher->scope_element = outer;
        bool matches = start ? selector_matcher_matches_group(matcher, start, root, nullptr)
                             : root == implicit;
        if (matches) {
            matcher->scope_element = root;
            bool excluded = false;
            CssSelectorGroup* end = rule->data.conditional_rule.scope_end;
            for (DomElement* candidate = element; end && candidate && candidate != root;
                 candidate = dom_parent_element(candidate)) {
                if (selector_matcher_matches_group(matcher, end, candidate, nullptr)) {
                    excluded = true;
                    break;
                }
            }
            if (!excluded) visitor(context, hops + 1);
        }
        if (root == outer) break;
    }
    matcher->scope_element = outer;
}

void selector_matcher_destroy(SelectorMatcher* matcher) {
    if (!matcher) {
        return;
    }
    // A created matcher owns only its own block; its pool is often the
    // long-lived document pool, so per-call matchers must give it back.
    pool_free(matcher->pool, matcher);
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

void selector_matcher_set_pseudo_state_resolver(SelectorMatcher* matcher,
                                                SelectorPseudoStateResolver resolver,
                                                void* context) {
    if (matcher) {
        matcher->pseudo_state_resolver = resolver;
        matcher->pseudo_state_context = context;
    }
}

bool selector_element_is_open_disclosure(DomElement* element) {
    return element && element->tag_name &&
        (str_icmp_cstr(element->tag_name, "details") == 0 ||
         str_icmp_cstr(element->tag_name, "dialog") == 0) &&
        element->has_attribute("open");
}

static bool selector_matcher_get_pseudo_state(SelectorMatcher* matcher,
                                              DomElement* element,
                                              uint32_t pseudo_state) {
    if (!element) return false;
    if (matcher && matcher->pseudo_state_resolver) {
        return matcher->pseudo_state_resolver(matcher->pseudo_state_context, element, pseudo_state);
    }

    switch (pseudo_state) {
        case PSEUDO_STATE_LINK:
            return element->has_attribute("href");
        case PSEUDO_STATE_CHECKED:
            return element->has_attribute("checked");
        case PSEUDO_STATE_DISABLED:
            return element->has_attribute("disabled");
        case PSEUDO_STATE_ENABLED:
            return !element->has_attribute("disabled");
        case PSEUDO_STATE_REQUIRED:
            return element->has_attribute("required");
        case PSEUDO_STATE_OPTIONAL:
            return !element->has_attribute("required");
        case PSEUDO_STATE_READ_ONLY:
            return element->has_attribute("readonly");
        case PSEUDO_STATE_READ_WRITE:
            return !element->has_attribute("readonly");
        case PSEUDO_STATE_SELECTED:
            // One owner for selectedness (F21): the node bit `option.selected`,
            // form submission and the listbox painter read, which falls back to
            // the `selected` content attribute itself when nothing has selected
            // anything yet. Reading the attribute directly here made `:selected`
            // match the page's *default* selection forever.
            return dom_option_is_selected(element);
        case PSEUDO_STATE_OPEN:
            return selector_element_is_open_disclosure(element);
        case PSEUDO_STATE_PLACEHOLDER_SHOWN:
            {
                const char* placeholder = element->get_attribute("placeholder");
                const char* value = element->get_attribute("value");
                return placeholder && placeholder[0] && (!value || !value[0]);
            }
        default:
            return false;
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

static bool selector_matcher_matches_column(SelectorMatcher* matcher,
    DomElement* cell, CssSelector* selector, int compound_index,
    CssCompoundSelector* left_compound);

static bool selector_matcher_matches_complex_at(SelectorMatcher* matcher,
                                                CssSelector* selector,
                                                int compound_index,
                                                DomElement* element) {
    if (!matcher || !selector || compound_index < 0 || !element) return false;
    if (!selector_matcher_matches_compound(
            matcher, selector->compound_selectors[compound_index], element)) {
        return false;
    }
    if (compound_index == 0) return true;

    CssCombinator combinator = selector->combinators[compound_index - 1];
    switch (combinator) {
        case CSS_COMBINATOR_DESCENDANT: {
            // A descendant combinator may need to skip a matching ancestor so
            // that an earlier child/sibling combinator can still be satisfied.
            for (DomElement* ancestor = static_cast<DomElement*>(element->parent);
                 ancestor; ancestor = static_cast<DomElement*>(ancestor->parent)) {
                if (selector_matcher_matches_complex_at(
                        matcher, selector, compound_index - 1, ancestor)) return true;
            }
            return false;
        }
        case CSS_COMBINATOR_CHILD: {
            DomElement* parent = element->parent
                ? static_cast<DomElement*>(element->parent) : nullptr;
            return parent && selector_matcher_matches_complex_at(
                matcher, selector, compound_index - 1, parent);
        }
        case CSS_COMBINATOR_NEXT_SIBLING: {
            DomNode* sibling = element->prev_sibling;
            while (sibling && !sibling->is_element()) sibling = sibling->prev_sibling;
            return sibling && selector_matcher_matches_complex_at(
                matcher, selector, compound_index - 1,
                static_cast<DomElement*>(sibling));
        }
        case CSS_COMBINATOR_SUBSEQUENT_SIBLING: {
            for (DomNode* sibling = element->prev_sibling; sibling;
                 sibling = sibling->prev_sibling) {
                if (sibling->is_element() && selector_matcher_matches_complex_at(
                        matcher, selector, compound_index - 1,
                        static_cast<DomElement*>(sibling))) return true;
            }
            return false;
        }
        case CSS_COMBINATOR_COLUMN:
            return selector_matcher_matches_column(matcher, element, selector,
                compound_index - 1, nullptr);
        default:
            return false;
    }
}

struct SelectorColumnOccupation {
    int first;
    int end;
    int until_row;
};

static int selector_html_table_span(DomElement* element, const char* attribute,
                                    int maximum, bool allow_zero = false) {
    const char* text = element->get_attribute(attribute);
    if (!text || !*text) return 1;
    char* end = nullptr;
    long value = strtol(text, &end, 10);
    if (end == text || *end || value < 0 || (!allow_zero && value == 0)) return 1;
    return value > maximum ? maximum : (int)value;
}

static void selector_column_occupations_clear(ArrayList* occupations) {
    if (!occupations) return;
    for (int i = 0; i < occupations->length; i++)
        mem_free(occupations->data[i]);
    occupations->length = 0;
}

static bool selector_table_cell_columns(DomElement* table, DomElement* cell,
                                        int* first, int* end) {
    ArrayList* occupations = arraylist_new(4);
    if (!occupations) return false;
    DomNode* row_group = nullptr;
    int row_index = 0;
    bool found = false;
    // HTML table membership is structural, so the result is available before layout.
    for (DomNode* node = table->first_child; node && !found;) {
        DomNode* next = node->next_sibling;
        DomElement* element = node->is_element() ? node->as_element() : nullptr;
        bool nested_table = element && element->tag() == MARKUP_NAME_TABLE;
        if (element && element->tag() == MARKUP_NAME_TR) {
            if (row_group != node->parent) {
                // HTML rowspans stop at their row-group boundary.
                selector_column_occupations_clear(occupations);
                row_group = node->parent;
                row_index = 0;
            }
            int active_count = 0;
            for (int i = 0; i < occupations->length; i++) {
                SelectorColumnOccupation* span =
                    (SelectorColumnOccupation*)occupations->data[i];
                if (span->until_row <= row_index) mem_free(span);
                else occupations->data[active_count++] = span;
            }
            occupations->length = active_count;
            int column = 0;
            for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                if (!child->is_element()) continue;
                DomElement* candidate = child->as_element();
                if (candidate->tag() != MARKUP_NAME_TD &&
                    candidate->tag() != MARKUP_NAME_TH) continue;
                bool occupied = true;
                while (occupied) {
                    occupied = false;
                    for (int i = 0; i < occupations->length; i++) {
                        SelectorColumnOccupation* span =
                            (SelectorColumnOccupation*)occupations->data[i];
                        if (row_index < span->until_row && column >= span->first &&
                            column < span->end) {
                            column = span->end;
                            occupied = true;
                            break;
                        }
                    }
                }
                int colspan = selector_html_table_span(candidate, "colspan", 1000);
                if (candidate == cell) {
                    *first = column;
                    *end = column + colspan;
                    found = true;
                    break;
                }
                int rowspan = selector_html_table_span(candidate, "rowspan", 65534, true);
                if (rowspan != 1) {
                    SelectorColumnOccupation* span = (SelectorColumnOccupation*)
                        mem_alloc(sizeof(SelectorColumnOccupation), MEM_CAT_TEMP);
                    if (!span) break;
                    span->first = column;
                    span->end = column + colspan;
                    span->until_row = rowspan == 0 || row_index > INT_MAX - rowspan
                        ? INT_MAX : row_index + rowspan;
                    if (!arraylist_append(occupations, span)) {
                        mem_free(span);
                        break;
                    }
                }
                column += colspan;
            }
            row_index++;
        }
        if (!nested_table && element && element->first_child) {
            node = element->first_child;
            continue;
        }
        while (!next && node->parent != table) {
            node = node->parent;
            next = node->next_sibling;
        }
        node = next;
    }
    selector_column_occupations_clear(occupations);
    arraylist_free(occupations);
    return found;
}

static bool selector_column_left_matches(SelectorMatcher* matcher,
    DomElement* column, CssSelector* selector, int compound_index,
    CssCompoundSelector* left_compound) {
    return selector
        ? selector_matcher_matches_complex_at(matcher, selector, compound_index, column)
        : selector_matcher_matches_compound(matcher, left_compound, column);
}

static bool selector_matcher_matches_column(SelectorMatcher* matcher,
    DomElement* cell, CssSelector* selector, int compound_index,
    CssCompoundSelector* left_compound) {
    if (!cell || (cell->tag() != MARKUP_NAME_TD && cell->tag() != MARKUP_NAME_TH))
        return false;
    DomElement* table = nullptr;
    for (DomNode* ancestor = cell->parent; ancestor; ancestor = ancestor->parent) {
        if (ancestor->is_element() && ancestor->as_element()->tag() == MARKUP_NAME_TABLE) {
            table = ancestor->as_element();
            break;
        }
    }
    if (!table) return false;
    int cell_first = 0, cell_end = 0;
    if (!selector_table_cell_columns(table, cell, &cell_first, &cell_end)) return false;
    int column = 0;
    for (DomNode* node = table->first_child; node; node = node->next_sibling) {
        if (!node->is_element()) continue;
        DomElement* group = node->as_element();
        if (group->tag() == MARKUP_NAME_COL) {
            int end = column + selector_html_table_span(group, "span", 1000);
            if (column < cell_end && end > cell_first &&
                selector_column_left_matches(matcher, group, selector,
                    compound_index, left_compound)) return true;
            column = end;
        } else if (group->tag() == MARKUP_NAME_COLGROUP) {
            int start = column;
            bool has_columns = false;
            for (DomNode* child = group->first_child; child; child = child->next_sibling) {
                if (!child->is_element() || child->as_element()->tag() != MARKUP_NAME_COL)
                    continue;
                has_columns = true;
                DomElement* col = child->as_element();
                int end = column + selector_html_table_span(col, "span", 1000);
                if (column < cell_end && end > cell_first &&
                    selector_column_left_matches(matcher, col, selector,
                        compound_index, left_compound)) return true;
                column = end;
            }
            if (!has_columns)
                column += selector_html_table_span(group, "span", 1000);
            if (start < cell_end && column > cell_first &&
                selector_column_left_matches(matcher, group, selector,
                    compound_index, left_compound)) return true;
        }
    }
    return false;
}

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
        // Complex selectors require backtracking across descendant relationships;
        // the nearest matching ancestor is not always the one satisfying the
        // next child or sibling combinator.
        local_result.matches = selector_matcher_matches_complex_at(
            matcher, selector, selector->compound_selector_count - 1, element);
        if (local_result.matches) {
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

static DomElement* selector_slotted_host(DomElement* element) {
    if (!element || !element->parent || !element->parent->is_element()) return nullptr;

    DomElement* parent = element->parent->as_element();
    DomElement* host = parent->shadow_root_element();
    if (!host) {
        if (!parent->tag_name || strcmp(parent->tag_name, "#document-fragment") != 0) {
            return nullptr;
        }
        host = parent->shadow_host_element();
    }
    if (!host || !host->shadow_root_element()) return nullptr;

    // ::slotted() only represents direct light-DOM children of this host.
    for (DomNode* child = host->first_child; child; child = child->next_sibling) {
        if (child == element) return host;
    }
    return nullptr;
}

static bool selector_slotted_has_slot(DomNode* node, DomElement* element) {
    if (!node || !element) return false;
    if (node->is_element()) {
        DomElement* candidate = node->as_element();
        if (candidate->tag_name && strcmp(candidate->tag_name, "slot") == 0) {
            const char* slot_name = candidate->get_attribute("name");
            const char* assigned_name = element->get_attribute("slot");
            if (!slot_name) slot_name = "";
            if (!assigned_name) assigned_name = "";
            if (strcmp(slot_name, assigned_name) == 0) return true;
        }
        for (DomNode* child = candidate->first_child; child; child = child->next_sibling) {
            if (selector_slotted_has_slot(child, element)) return true;
        }
    }
    return false;
}

static bool selector_matcher_matches_slotted(SelectorMatcher* matcher,
                                             CssSimpleSelector* simple_selector,
                                             DomElement* element) {
    DomElement* host = selector_slotted_host(element);
    if (!host || !selector_slotted_has_slot(host->shadow_root_element(), element)) {
        return false;
    }
    return selector_matcher_matches_is(matcher,
        simple_selector->function_selectors,
        (int)simple_selector->function_selector_count,
        element);
}

static bool selector_matcher_matches_nth_filtered(SelectorMatcher* matcher,
        const CssNthFormula* formula, DomElement* element, bool from_end,
        bool of_type, CssSelector** filter, size_t filter_count);

static bool selector_matcher_type_namespace_matches(
        const CssSimpleSelector* selector, DomElement* element) {
    if (selector->namespace_prefix && selector->namespace_prefix[0] &&
        strcmp(selector->namespace_prefix, "*") != 0 &&
        !selector->namespace_url) return false;
    return !selector->namespace_url ||
        strcmp(dom_element_namespace_uri(element), selector->namespace_url) == 0;
}

static bool selector_matcher_matches_attribute_value(
    SelectorMatcher* matcher, const char* element_value, bool exists,
    const char* attr_value, CssSelectorType attr_type, bool case_insensitive,
    bool case_sensitive);

static bool selector_matcher_matches_namespaced_attribute(
    SelectorMatcher* matcher, CssSimpleSelector* selector, DomElement* element) {
    const char* wanted_uri = selector->namespace_url;
    if (selector->namespace_prefix && selector->namespace_prefix[0] &&
        strcmp(selector->namespace_prefix, "*") != 0 && !wanted_uri) return false;
    for (DomNamespacedAttribute* attr = dom_element_namespaced_attributes(element);
         attr; attr = attr->next) {
        if (!attr->active ||
            (wanted_uri && strcmp(attr->namespace_uri, wanted_uri) != 0) ||
            strcmp(attr->local_name, selector->attribute.name) != 0) continue;
        if (selector_matcher_matches_attribute_value(matcher, attr->value, true,
                selector->attribute.value, selector->type,
                selector->attribute.case_insensitive,
                selector->attribute.case_sensitive)) return true;
    }
    if (wanted_uri && !*wanted_uri &&
        !dom_element_namespaced_attributes(element)) {
        const char* name = selector->attribute.name;
        return selector_matcher_matches_attribute_value(matcher,
            element->get_attribute(name), element->has_attribute(name),
            selector->attribute.value, selector->type,
            selector->attribute.case_insensitive,
            selector->attribute.case_sensitive);
    }
    int count = 0;
    const char** names = element->attribute_names(&count);
    if (!names) return false;
    bool html = strcmp(dom_element_namespace_uri(element),
        "http://www.w3.org/1999/xhtml") == 0;
    for (int i = 0; i < count; i++) {
        const char* local = nullptr;
        const char* uri = dom_element_attribute_namespace_uri(element, names[i], &local);
        if (!uri || (wanted_uri && strcmp(uri, wanted_uri) != 0)) continue;
        if (html ? str_icmp_cstr(local, selector->attribute.name) != 0
                 : strcmp(local, selector->attribute.name) != 0) continue;
        if (selector_matcher_matches_attribute_value(matcher,
                element->get_attribute(names[i]), element->has_attribute(names[i]),
                selector->attribute.value, selector->type,
                selector->attribute.case_insensitive,
                selector->attribute.case_sensitive)) return true;
    }
    return false;
}

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
            if (!selector_matcher_type_namespace_matches(simple_selector, element)) {
                return false;
            }
            if (simple_selector->value) {
                // safety check for NULL or invalid tag_name
                if (!element->tag_name || (uintptr_t)element->tag_name.get() < 0x1000) {
                    log_error("Invalid tag_name pointer in element: %p", element->tag_name);
                    return false;
                }
                // Prefixes identify namespaces; the type selector compares local names.
                return strcmp(dom_element_namespace_uri(element),
                    "http://www.w3.org/1999/xhtml") == 0
                    ? str_icmp_cstr(element->local_name(), simple_selector->value) == 0
                    : strcmp(element->local_name(), simple_selector->value) == 0;
            }
            return true; // No type specified matches any element

        case CSS_SELECTOR_TYPE_CLASS:
            // Match class - with case sensitivity based on configuration
            if (!simple_selector->value) return false;
            for (int i = 0; i < element->class_count; i++) {
                int cmp = matcher->case_sensitive_classes
                    ? strcmp(element->class_names[i], simple_selector->value)
                    : str_icmp_cstr(element->class_names[i], simple_selector->value);
                if (cmp == 0) return true;
            }
            return false;

        case CSS_SELECTOR_TYPE_ID:
            // Match ID
            return element->id && strcmp(element->id, simple_selector->value) == 0;

        case CSS_SELECTOR_TYPE_UNIVERSAL:
            return selector_matcher_type_namespace_matches(simple_selector, element);

        case CSS_SELECTOR_ATTR_EXISTS:
            // Attribute exists
            return selector_matcher_matches_namespaced_attribute(
                matcher, simple_selector, element);

        case CSS_SELECTOR_ATTR_EXACT:
        case CSS_SELECTOR_ATTR_CONTAINS:
        case CSS_SELECTOR_ATTR_BEGINS:
        case CSS_SELECTOR_ATTR_ENDS:
        case CSS_SELECTOR_ATTR_SUBSTRING:
        case CSS_SELECTOR_ATTR_LANG:
        case CSS_SELECTOR_ATTR_CASE_INSENSITIVE:
        case CSS_SELECTOR_ATTR_CASE_SENSITIVE:
            {
                return selector_matcher_matches_namespaced_attribute(
                    matcher, simple_selector, element);
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
        case CSS_SELECTOR_PSEUDO_ELEMENT_FOOTNOTE_CALL:
        case CSS_SELECTOR_PSEUDO_ELEMENT_FOOTNOTE_MARKER:
            // Pseudo-elements are matched at a higher level (compound/selector matching)
            // Here we just return true to not block the match
            return true;
        case CSS_SELECTOR_PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON:
            return element->tag_name &&
                str_icmp_cstr(element->tag_name, "input") == 0 &&
                element->get_attribute("type") &&
                str_icmp_cstr(element->get_attribute("type"), "file") == 0;

        // Functional pseudo-classes: :not(), :is(), :where(), :has()
        case CSS_SELECTOR_PSEUDO_NOT:
            return selector_matcher_matches_not(matcher,
                simple_selector->function_selectors,
                (int)simple_selector->function_selector_count,
                element);
        case CSS_SELECTOR_PSEUDO_IS:
            return selector_matcher_matches_is(matcher,
                simple_selector->function_selectors,
                (int)simple_selector->function_selector_count,
                element);
        case CSS_SELECTOR_PSEUDO_WHERE:
            return selector_matcher_matches_where(matcher,
                simple_selector->function_selectors,
                (int)simple_selector->function_selector_count,
                element);
        case CSS_SELECTOR_PSEUDO_HAS:
            return selector_matcher_matches_has(matcher,
                simple_selector->function_selectors,
                (int)simple_selector->function_selector_count,
                element);
        case CSS_SELECTOR_PSEUDO_SLOTTED:
            return selector_matcher_matches_slotted(matcher, simple_selector, element);

        case CSS_SELECTOR_PSEUDO_NTH_CHILD:
        case CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD:
        case CSS_SELECTOR_PSEUDO_NTH_OF_TYPE:
        case CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE: {
            bool from_end = simple_selector->type == CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD ||
                            simple_selector->type == CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE;
            bool of_type = simple_selector->type == CSS_SELECTOR_PSEUDO_NTH_OF_TYPE ||
                           simple_selector->type == CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE;
            return selector_matcher_matches_nth_filtered(matcher,
                &simple_selector->nth_formula, element, from_end, of_type,
                simple_selector->function_selectors,
                simple_selector->function_selector_count);
        }

        case CSS_SELECTOR_PSEUDO_SCOPE:
            // Element query APIs must bind :scope to their receiver; jQuery
            // uses this form to evaluate relative selectors such as "> h3".
            return matcher->scope_element
                ? matcher->scope_element == element
                : element->doc && element->doc->root == element;

        // Other pseudo-classes
        default:
            if (simple_selector->type >= CSS_SELECTOR_PSEUDO_ROOT &&
                (simple_selector->type <= CSS_SELECTOR_PSEUDO_POPOVER_OPEN ||
                 simple_selector->type == CSS_SELECTOR_PSEUDO_DEFINED ||
                 simple_selector->type == CSS_SELECTOR_PSEUDO_USER_INVALID ||
                 simple_selector->type == CSS_SELECTOR_PSEUDO_USER_VALID)) {
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
            case CSS_SELECTOR_PSEUDO_ELEMENT_BACKDROP:
                return PSEUDO_ELEMENT_BACKDROP;
            case CSS_SELECTOR_PSEUDO_ELEMENT_MARKER:
                return PSEUDO_ELEMENT_MARKER;
            case CSS_SELECTOR_PSEUDO_ELEMENT_PLACEHOLDER:
                return PSEUDO_ELEMENT_PLACEHOLDER;
            case CSS_SELECTOR_PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON:
                return PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON;
            case CSS_SELECTOR_PSEUDO_ELEMENT_FOOTNOTE_CALL:
                return PSEUDO_ELEMENT_FOOTNOTE_CALL;
            case CSS_SELECTOR_PSEUDO_ELEMENT_FOOTNOTE_MARKER:
                return PSEUDO_ELEMENT_FOOTNOTE_MARKER;
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
static bool selector_matcher_matches_attribute_value(
    SelectorMatcher* matcher, const char* element_attr, bool exists,
    const char* attr_value, CssSelectorType attr_type, bool case_insensitive,
    bool case_sensitive) {
    if (!matcher) return false;
    // For existence check (no value specified), use has_attribute
    // This handles the case where attribute exists but has empty value (stored as null)
    if (!attr_value || attr_type == CSS_SELECTOR_ATTR_EXISTS) {
        return exists;
    }
    if (!exists) return false;
    if (!element_attr) {
        // Empty attributes may be stored without a value pointer; exact-empty
        // selectors must still distinguish them from absent attributes.
        if (attr_type == CSS_SELECTOR_ATTR_EXACT && attr_value[0] == '\0') {
            return true;
        }
        return false;
    }

    // Determine comparison function - respect both parameter AND matcher configuration
    bool use_case_insensitive = !case_sensitive &&
        (case_insensitive || !matcher->case_sensitive_attrs);
    int (*compare_func)(const char*, const char*) = use_case_insensitive ? str_icmp_cstr : strcmp;

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
                    pos = str_skip_ascii_space(pos);
                    if (!*pos) break;

                    // Check if this word matches
                    const char* word_start = pos;
                    while (*pos && !str_char_is_ascii_space(*pos)) pos++;

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
                return str_ifind(element_attr, strlen(element_attr), attr_value,
                                 strlen(attr_value)) != STR_NPOS;
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

bool selector_matcher_matches_attribute(SelectorMatcher* matcher,
                                        const char* attr_name,
                                        const char* attr_value,
                                        CssSelectorType attr_type,
                                        bool case_insensitive,
                                        DomElement* element) {
    if (!matcher || !attr_name || !element) return false;
    return selector_matcher_matches_attribute_value(matcher,
        element->get_attribute(attr_name), element->has_attribute(attr_name),
        attr_value, attr_type, case_insensitive, false);
}

// ============================================================================
// Pseudo-Class Matching
// ============================================================================

static bool selector_matcher_matches_lang(const char* argument, DomElement* element) {
    if (!argument || !element) return false;
    const char* language = nullptr;
    for (DomElement* current = element; current; current = current->parent_element()) {
        language = current->get_attribute("lang");
        if (!language) language = current->get_attribute("xml:lang");
        if (language) break;
    }
    if (!language || !language[0]) return false;
    size_t language_len = strlen(language);
    for (const char* item = argument; *item;) {
        const char* comma = strchr(item, ',');
        const char* end = comma ? comma : item + strlen(item);
        if (end > item + 1 &&
            ((*item == '"' && end[-1] == '"') ||
             (*item == '\'' && end[-1] == '\''))) {
            item++;
            end--;
        }
        size_t length = (size_t)(end - item);
        if ((length == 1 && item[0] == '*') ||
            (length > 0 && language_len >= length &&
             str_ieq(language, length, item, length) &&
             (language_len == length || language[length] == '-'))) return true;
        item = comma ? comma + 1 : end;
    }
    return false;
}

static bool selector_matcher_matches_dir(const char* argument, DomElement* element) {
    if (!argument || !element) return false;
    bool want_rtl = str_icmp_cstr(argument, "rtl") == 0;
    for (DomElement* current = element; current;
         current = current->parent_element()) {
        const char* dir = current->get_attribute("dir");
        if (dir && str_icmp_cstr(dir, "rtl") == 0) return want_rtl;
        if (dir && str_icmp_cstr(dir, "ltr") == 0) return !want_rtl;
        bool auto_dir = dir && str_icmp_cstr(dir, "auto") == 0;
        if (!dir && current->tag_name &&
            str_icmp_cstr(current->tag_name, "bdi") == 0) auto_dir = true;
        if (!auto_dir) continue;
        for (DomNode* child = current->first_child; child;
             child = child->next_sibling) {
            int strong = dom_find_strong_direction(child, true, true);
            if (strong != 0) return want_rtl ? strong > 0 : strong < 0;
        }
        return !want_rtl;
    }
    return !want_rtl;
}

static bool selector_matcher_matches_local_link(SelectorMatcher* matcher,
                                                 DomElement* element) {
    if (!selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_LINK) ||
        !element->doc || !element->doc->url) return false;
    const char* href = element->get_attribute("href");
    if (!href) return false;
    Url* target = url_parse_with_base(href, element->doc->url);
    if (!target || !url_is_valid(target)) {
        if (target) url_destroy(target);
        return false;
    }
    bool matches = url_equals_without_fragment(target, element->doc->url);
    url_destroy(target);
    return matches;
}

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
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_HOVER);
        case CSS_SELECTOR_PSEUDO_ACTIVE:
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_ACTIVE);
        case CSS_SELECTOR_PSEUDO_FOCUS:
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_FOCUS);
        case CSS_SELECTOR_PSEUDO_FOCUS_VISIBLE:
            // :focus-visible matches when focused via keyboard navigation
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_FOCUS_VISIBLE);
        case CSS_SELECTOR_PSEUDO_FOCUS_WITHIN:
            // :focus-within matches when element or any descendant has focus
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_FOCUS_WITHIN);
        case CSS_SELECTOR_PSEUDO_VISITED:
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_VISITED);
        case CSS_SELECTOR_PSEUDO_LINK:
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_LINK);
        case CSS_SELECTOR_PSEUDO_ANY_LINK:
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_LINK);
        case CSS_SELECTOR_PSEUDO_LOCAL_LINK:
            return selector_matcher_matches_local_link(matcher, element);
        case CSS_SELECTOR_PSEUDO_LANG:
            return selector_matcher_matches_lang(pseudo_arg, element);
        case CSS_SELECTOR_PSEUDO_DIR:
            return selector_matcher_matches_dir(pseudo_arg, element);
        case CSS_SELECTOR_PSEUDO_TARGET:
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_TARGET);

        // Form pseudo-classes
        case CSS_SELECTOR_PSEUDO_ENABLED:
            // :enabled matches when NOT disabled
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_ENABLED);
        case CSS_SELECTOR_PSEUDO_DISABLED:
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_DISABLED);
        case CSS_SELECTOR_PSEUDO_CHECKED:
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_CHECKED);
        case CSS_SELECTOR_PSEUDO_SELECTED:
            // :selected reads the option's live IDL selectedness, not its
            // defaultSelected content attribute.
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_SELECTED);
        case CSS_SELECTOR_PSEUDO_REQUIRED:
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_REQUIRED);
        case CSS_SELECTOR_PSEUDO_OPTIONAL:
            // :optional matches when NOT required
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_OPTIONAL);
        case CSS_SELECTOR_PSEUDO_VALID:
        case CSS_SELECTOR_PSEUDO_INVALID: {
            bool invalid = pseudo_type == CSS_SELECTOR_PSEUDO_INVALID;
            int native_match = dom_css_element_matches_validity(element, invalid, false);
            return native_match >= 0 ? native_match != 0 :
                selector_matcher_get_pseudo_state(matcher, element,
                    invalid ? PSEUDO_STATE_INVALID : PSEUDO_STATE_VALID);
        }
        case CSS_SELECTOR_PSEUDO_USER_INVALID:
        case CSS_SELECTOR_PSEUDO_USER_VALID:
            return dom_css_element_matches_validity(element,
                pseudo_type == CSS_SELECTOR_PSEUDO_USER_INVALID, true) > 0;
        case CSS_SELECTOR_PSEUDO_OPEN:
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_OPEN);
        case CSS_SELECTOR_PSEUDO_MODAL:
            return element->tag_name &&
                str_icmp_cstr(element->tag_name, "dialog") == 0 &&
                element->has_attribute("open") && element->is_dialog_modal();
        case CSS_SELECTOR_PSEUDO_POPOVER_OPEN:
            return element->has_attribute("popover") &&
                element->is_popover_open();
        case CSS_SELECTOR_PSEUDO_DEFINED:
            // Built-in/foreign elements are defined without a registry entry;
            // autonomous custom elements become defined on registration.
            return !element->tag_name || !strchr(element->tag_name, '-') ||
                dom_css_custom_element_defined(element->tag_name);
        case CSS_SELECTOR_PSEUDO_READ_ONLY:
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_READ_ONLY);
        case CSS_SELECTOR_PSEUDO_READ_WRITE:
            // :read-write matches when NOT read-only
            return selector_matcher_get_pseudo_state(matcher, element, PSEUDO_STATE_READ_WRITE);
        case CSS_SELECTOR_PSEUDO_PLACEHOLDER_SHOWN:
        {
            int native_match = dom_css_element_placeholder_shown(element);
            return native_match >= 0 ? native_match != 0 :
                selector_matcher_get_pseudo_state(matcher, element,
                    PSEUDO_STATE_PLACEHOLDER_SHOWN);
        }
        case CSS_SELECTOR_PSEUDO_DEFAULT:
            return dom_css_element_is_default(element);
        case CSS_SELECTOR_PSEUDO_INDETERMINATE:
            return dom_css_element_is_indeterminate(element);
        case CSS_SELECTOR_PSEUDO_IN_RANGE:
            return dom_css_element_matches_range(element, false);
        case CSS_SELECTOR_PSEUDO_OUT_OF_RANGE:
            return dom_css_element_matches_range(element, true);

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
                    bool of_type = pseudo_type == CSS_SELECTOR_PSEUDO_NTH_OF_TYPE ||
                                   pseudo_type == CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE;
                    bool result = selector_matcher_matches_nth_filtered(
                        matcher, &formula, element, from_end, of_type, nullptr, 0);
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
            bool is_root = element->doc && element->doc->root == element;
            log_debug("[SELECTOR] :root check for <%s>: doc_root=%p, is_root=%d",
                      element->tag_name, element->doc ? element->doc->root : nullptr, is_root);
            return is_root;
        }

        case CSS_SELECTOR_PSEUDO_EMPTY: {
            for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                if (child->is_comment()) continue;
                if (child->is_text() && child->as_text()->length == 0) continue;
                if (child->is_element() && child->as_element()->is_synthetic()) continue;
                // Comments and empty text nodes do not contribute content.
                return false;
            }
            return true;
        }

        case CSS_SELECTOR_PSEUDO_FIRST_CHILD:
            return element->is_first_child();

        case CSS_SELECTOR_PSEUDO_LAST_CHILD:
            return element->is_last_child();

        case CSS_SELECTOR_PSEUDO_ONLY_CHILD:
            return element->is_only_child();

        case CSS_SELECTOR_PSEUDO_FIRST_OF_TYPE:
            // First of its type among siblings
            if (!element->parent) return true;
            {
                DomElement* parent = static_cast<DomElement*>(element->parent);
                DomNode* sibling_node = parent->first_child;
                while (sibling_node) {
                    if (dom_is_css_element_child(sibling_node)) {
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
                    if (dom_is_css_element_child(sibling_node)) {
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
                    if (dom_is_css_element_child(sibling_node)) {
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

static bool selector_matcher_matches_nth_index(const CssNthFormula* formula, int index) {
    int a = formula->odd || formula->even ? 2 : formula->a;
    int b = formula->odd ? 1 : formula->even ? 0 : formula->b;
    if (a == 0) return index == b;
    int diff = index - b;
    return diff % a == 0 && diff / a >= 0;
}

static bool selector_matcher_matches_nth_filtered(SelectorMatcher* matcher,
        const CssNthFormula* formula, DomElement* element, bool from_end,
        bool of_type, CssSelector** filter, size_t filter_count) {
    if (!matcher || !formula || !element) {
        return false;
    }
    if (!element->parent) {
        return filter_count == 0 && selector_matcher_matches_nth_index(formula, 1);
    }

    // Count within the filtered sibling list before applying An+B.
    int total = 0;
    int selected_index = 0;
    DomElement* parent = static_cast<DomElement*>(element->parent);
    for (DomNode* node = parent->first_child; node; node = node->next_sibling) {
        if (!dom_is_css_element_child(node)) continue;
        DomElement* sibling = static_cast<DomElement*>(node);
        if (of_type && !selector_matcher_same_tag(sibling, element)) continue;
        if (filter_count > 0) {
            bool included = false;
            for (size_t i = 0; i < filter_count; i++) {
                if (selector_matcher_matches(matcher, filter[i], sibling, nullptr)) {
                    included = true;
                    break;
                }
            }
            if (!included) continue;
        }
        total++;
        if (sibling == element) selected_index = total;
    }
    if (selected_index == 0) return false;
    int index = from_end ? total - selected_index + 1 : selected_index;
    return selector_matcher_matches_nth_index(formula, index);
}

bool selector_matcher_matches_nth_child(SelectorMatcher* matcher,
                                        CssNthFormula* formula,
                                        DomElement* element,
                                        bool from_end) {
    return selector_matcher_matches_nth_filtered(
        matcher, formula, element, from_end, false, nullptr, 0);
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
            return selector_matcher_has_ancestor(matcher, left_selector, element, nullptr);
        case CSS_COMBINATOR_CHILD:
            return selector_matcher_has_parent(matcher, left_selector, element);
        case CSS_COMBINATOR_NEXT_SIBLING:
            return selector_matcher_has_prev_sibling(matcher, left_selector, element);
        case CSS_COMBINATOR_SUBSEQUENT_SIBLING:
            return selector_matcher_has_preceding_sibling(matcher, left_selector, element, nullptr);
        case CSS_COMBINATOR_COLUMN:
            return selector_matcher_matches_column(matcher, element, nullptr, 0,
                left_selector);
        default:
            return false;
    }
}

bool selector_matcher_has_ancestor(SelectorMatcher* matcher,
                                   CssCompoundSelector* selector,
                                   DomElement* element,
                                   DomElement** matched_ancestor) {
    if (!matcher || !selector || !element) {
        return false;
    }

    DomElement* ancestor = static_cast<DomElement*>(element->parent);
    while (ancestor) {
        if (selector_matcher_matches_compound(matcher, selector, ancestor)) {
            if (matched_ancestor) *matched_ancestor = ancestor;
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
                                            DomElement* element,
                                            DomElement** matched_sibling) {
    if (!matcher || !selector || !element) {
        return false;
    }

    DomNode* sibling_node = element->prev_sibling;
    while (sibling_node) {
        if (sibling_node->is_element()) {
            DomElement* sibling = static_cast<DomElement*>(sibling_node);
            if (selector_matcher_matches_compound(matcher, selector, sibling)) {
                if (matched_sibling) *matched_sibling = sibling;
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

static bool selector_matcher_match_list(SelectorMatcher* matcher,
                                         CssSelector** selectors,
                                         int count,
                                         DomElement* element,
                                         bool want_match) {
    if (!matcher || !selectors || count <= 0 || !element) return false;
    for (int i = 0; i < count; i++) {
        bool matched = selector_matcher_matches(matcher, selectors[i], element, NULL);
        if (matched == want_match) return want_match;
    }
    return !want_match;
}

bool selector_matcher_matches_is(SelectorMatcher* matcher,
                                 CssSelector** selectors,
                                 int count,
                                 DomElement* element) {
    return selector_matcher_match_list(matcher, selectors, count, element, true);
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

    // :not() accepts the element only when none of its argument selectors match;
    // passing want_match=false to the positive-list helper reverses that result.
    return !selector_matcher_match_list(matcher, selectors, count, element, true);
}

static bool selector_matcher_relative_from(SelectorMatcher* matcher,
        CssSelector* selector, DomElement* anchor, size_t index);

static bool selector_matcher_relative_candidate(SelectorMatcher* matcher,
        CssSelector* selector, DomNode* node, size_t index) {
    if (!node || !node->is_element()) return false;
    DomElement* element = node->as_element();
    return selector_matcher_matches_compound(
        matcher, selector->compound_selectors[index], element) &&
        (index + 1 == selector->compound_selector_count ||
         selector_matcher_relative_from(matcher, selector, element, index + 1));
}

static bool selector_matcher_relative_from(SelectorMatcher* matcher,
        CssSelector* selector, DomElement* anchor, size_t index) {
    CssCombinator relation = index == 0 ? selector->leading_combinator
        : selector->combinators[index - 1];
    if (relation == CSS_COMBINATOR_NONE) relation = CSS_COMBINATOR_DESCENDANT;
    if (relation == CSS_COMBINATOR_CHILD) {
        for (DomNode* child = anchor->first_child; child; child = child->next_sibling) {
            if (selector_matcher_relative_candidate(matcher, selector, child, index)) return true;
        }
        return false;
    }
    if (relation == CSS_COMBINATOR_NEXT_SIBLING ||
        relation == CSS_COMBINATOR_SUBSEQUENT_SIBLING) {
        for (DomNode* sibling = anchor->next_sibling; sibling;
             sibling = sibling->next_sibling) {
            if (!sibling->is_element()) continue;
            if (selector_matcher_relative_candidate(matcher, selector, sibling, index)) return true;
            if (relation == CSS_COMBINATOR_NEXT_SIBLING) return false;
        }
        return false;
    }
    if (relation != CSS_COMBINATOR_DESCENDANT) return false;
    // Walk the anchored subtree so a compound before a descendant combinator
    // cannot match an ancestor outside the :has() candidate.
    for (DomNode* node = anchor->first_child; node;) {
        if (selector_matcher_relative_candidate(matcher, selector, node, index)) return true;
        DomNode* first_child = node->is_element()
            ? node->as_element()->first_child : nullptr;
        if (first_child) {
            node = first_child;
            continue;
        }
        while (node != anchor && !node->next_sibling) node = node->parent;
        node = node == anchor ? nullptr : node->next_sibling;
    }
    return false;
}

bool selector_matcher_matches_has(SelectorMatcher* matcher,
                                  CssSelector** selectors,
                                  int count,
                                  DomElement* element) {
    if (!matcher || !selectors || count <= 0 || !element) return false;
    for (int i = 0; i < count; i++) {
        CssSelector* selector = selectors[i];
        if (selector && selector->compound_selector_count > 0 &&
            selector_matcher_relative_from(matcher, selector, element, 0)) return true;
    }
    return false;
}

// ============================================================================
// Specificity Calculation
// ============================================================================

static CssSpecificity selector_matcher_function_max_specificity(
        SelectorMatcher* matcher, const CssSimpleSelector* simple) {
    CssSpecificity max_spec = {0, 0, 0, 0, false};
    for (size_t i = 0; i < simple->function_selector_count; i++) {
        CssSpecificity candidate = selector_matcher_calculate_specificity(
            matcher, simple->function_selectors[i]);
        if (css_specificity_compare(candidate, max_spec) > 0) max_spec = candidate;
    }
    return max_spec;
}

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

                case CSS_SELECTOR_PSEUDO_NOT:
                case CSS_SELECTOR_PSEUDO_IS:
                case CSS_SELECTOR_PSEUDO_HAS: {
                    CssSpecificity argument_spec = selector_matcher_function_max_specificity(
                        matcher, simple);
                    // Selectors 4: these functional pseudo-classes take the
                    // specificity of their most specific complex argument.
                    spec.ids += argument_spec.ids;
                    spec.classes += argument_spec.classes;
                    spec.elements += argument_spec.elements;
                    break;
                }

                case CSS_SELECTOR_PSEUDO_SLOTTED: {
                    CssSpecificity argument_spec = selector_matcher_function_max_specificity(
                        matcher, simple);
                    spec.ids += argument_spec.ids;
                    spec.classes += argument_spec.classes;
                    spec.elements += argument_spec.elements + 1;
                    break;
                }

                case CSS_SELECTOR_PSEUDO_NTH_CHILD:
                case CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD: {
                    CssSpecificity argument_spec = selector_matcher_function_max_specificity(
                        matcher, simple);
                    spec.ids += argument_spec.ids;
                    spec.classes += argument_spec.classes + 1;
                    spec.elements += argument_spec.elements;
                    break;
                }

                case CSS_SELECTOR_TYPE_ELEMENT:
                    spec.elements++;
                    break;

                // Universal selector and :where() don't add specificity
                case CSS_SELECTOR_TYPE_UNIVERSAL:
                case CSS_SELECTOR_PSEUDO_WHERE:
                    break;

                default:
                    // enum ranges cover every ordinary pseudo without a second
                    // hand-maintained list that silently loses specificity.
                    if (simple->type >= CSS_SELECTOR_PSEUDO_ROOT &&
                        (simple->type < CSS_SELECTOR_PSEUDO_GENERIC ||
                         simple->type == CSS_SELECTOR_PSEUDO_DEFINED ||
                         simple->type == CSS_SELECTOR_PSEUDO_USER_INVALID ||
                         simple->type == CSS_SELECTOR_PSEUDO_USER_VALID)) {
                        spec.classes++;
                    } else if ((simple->type == CSS_SELECTOR_PSEUDO_GENERIC ||
                                simple->type == CSS_SELECTOR_PSEUDO_ELEMENT_GENERIC) &&
                               css_selector_generic_pseudo_is_known(simple)) {
                        // Known but unrendered pseudo selectors still carry
                        // their ordinary specificity in selector lists.
                        if (simple->type == CSS_SELECTOR_PSEUDO_GENERIC) spec.classes++;
                        else spec.elements++;
                    } else if (simple->type >= CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE &&
                               simple->type < CSS_SELECTOR_PSEUDO_ELEMENT_GENERIC) {
                        spec.elements++;
                    }
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
#ifndef LAMBDA_NO_CONSOLE_DUMP
    if (!matcher) {
        log_info("Selector Matcher: NULL");
        return;
    }

    log_info("Selector Matcher:");
    log_info("  Cache enabled: %s", matcher->cache_enabled ? "yes" : "no");
    log_info("  Strict mode: %s", matcher->strict_mode ? "yes" : "no");
    log_info("  Case-sensitive attributes: %s", matcher->case_sensitive_attrs ? "yes" : "no");
    log_info("  Total matches: %llu", (unsigned long long)matcher->total_matches);
    log_info("  Cache hits: %llu", (unsigned long long)matcher->cache_hits);
    log_info("  Cache misses: %llu", (unsigned long long)matcher->cache_misses);

    if (matcher->total_matches > 0) {
        double hit_rate = (double)matcher->cache_hits / (double)matcher->total_matches;
        log_info("  Cache hit rate: %.2f%%", hit_rate * 100.0);
    }
#endif
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

    return str_icmp_cstr(element1->tag_name, element2->tag_name) == 0;
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
    while (*formula_str && str_char_is_ascii_space(*formula_str)) {
        formula_str++;
    }

    // Check for "odd" or "even"
    if (str_ieq_cstr(formula_str, "odd")) {
        formula->odd = true;
        return true;
    }
    if (str_ieq_cstr(formula_str, "even")) {
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
        p = str_skip_ascii_space(p);
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
    } else if (str_char_is_digit(*p)) {
        formula->a = (int)str_to_int64_default(p, strlen(p), 0);
        while (*p && str_char_is_digit(*p)) p++;
    } else {
        formula->a = 1;
    }

    // Skip whitespace
    p = str_skip_ascii_space(p);

    // Check for 'n'
    if (*p == 'n' || *p == 'N') {
        p++;

        // Skip whitespace
        p = str_skip_ascii_space(p);

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
    if (str_ieq_const(pseudo_class, pc_len, "open")) return PSEUDO_STATE_OPEN;
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
        case PSEUDO_STATE_OPEN: return "open";
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
