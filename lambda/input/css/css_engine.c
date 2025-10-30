#include "css_engine.h"
#include "css_property_value_parser.h"
#include "css_parser.h"
#include "css_style_node.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Forward declarations for functions defined later in this file
static void css_detect_features_in_rule(CssStylesheet* stylesheet, CssRule* rule);
static void css_apply_rule_to_element(CssRule* rule, CssStyleNode* element, Pool* pool);

// Forward declarations for missing style node and engine functions
int css_style_node_compare(const void* a, const void* b, void* udata);
void css_style_node_cleanup(void* node, void* udata);
CssEngine* css_engine_create(Pool* pool);
void css_engine_destroy(CssEngine* engine);
CssEngine* css_engine_create(Pool* pool);

// Style node management functions (proper implementations)
static void css_style_node_init(CssStyleNode* node, const char* element_name, Pool* pool) {
    if (!node || !pool) return;
    memset(node, 0, sizeof(CssStyleNode));
    // CssStyleNode represents a single property, not an element
    // Element name is not stored in style nodes
    node->property_id = 0;
    node->winning_declaration = NULL;
    node->losing_declarations = NULL;
    node->losing_count = 0;
    node->losing_capacity = 0;
    node->has_custom_property = false;
}

static void css_style_node_set_element_name(CssStyleNode* node, const char* element_name) {
    // Note: CssStyleNode is for CSS properties, not DOM elements
    // Element names are handled by DomElement structures
    // This function is a no-op as it doesn't apply to style nodes
    (void)node;
    (void)element_name;
}

static void css_style_node_add_class(CssStyleNode* node, const char* class_name) {
    // Note: CssStyleNode is for CSS properties, not DOM elements
    // Class names are handled by DomElement structures
    // This function is a no-op as it doesn't apply to style nodes
    (void)node;
    (void)class_name;
}

static void css_style_node_add_property(CssStyleNode* node, const char* prop_name,
                                       void* value, Pool* pool) {
    // Set the property on this style node
    if (!node || !prop_name || !value || !pool) return;

    // Convert property name to ID
    CssPropertyId prop_id = css_property_id_from_name(prop_name);
    if (prop_id == 0) return; // Unknown property

    node->property_id = prop_id;

    // Create a declaration for this property with value
    CssSpecificity spec = css_specificity_create(0, 0, 0, 0, false);
    CssDeclaration* decl = css_declaration_create(prop_id, value, spec, CSS_ORIGIN_AUTHOR, pool);
    if (decl) {
        node->winning_declaration = decl;
    }
}

// Additional style node query functions
static bool css_style_node_has_class(CssStyleNode* node, const char* class_name) {
    // Note: CssStyleNode is for CSS properties, not DOM elements
    // Class checking should be done on DomElement structures
    // Return false as this doesn't apply to style nodes
    (void)node;
    (void)class_name;
    return false;
}

static bool css_style_node_matches_id(CssStyleNode* node, const char* id) {
    // Note: CssStyleNode is for CSS properties, not DOM elements
    // ID matching should be done on DomElement structures
    // Return false as this doesn't apply to style nodes
    (void)node;
    (void)id;
    return false;
}

static bool css_style_node_matches_element_name(CssStyleNode* node, const char* element_name) {
    // Note: CssStyleNode is for CSS properties, not DOM elements
    // Element name matching should be done on DomElement structures
    // Return false as this doesn't apply to style nodes
    (void)node;
    (void)element_name;
    return false;
}

// Enhanced CSS4+ pseudo-selector functions
static bool css_enhanced_pseudo_has_matches(CssStyleNode* node, CSSSelectorComponent* component) {
    // :has() pseudo-class matching
    // This would require checking if the node's element has descendants matching a selector
    // For now, return false as this requires full DOM traversal context
    (void)node;
    (void)component;
    return false;
}

static bool css_enhanced_pseudo_is_matches(CssStyleNode* node, CSSSelectorComponent* component) {
    // :is() pseudo-class matching (matches any selector in a list)
    // This would require checking if the node's element matches any selector in the component
    // For now, return false as this requires full selector matching context
    (void)node;
    (void)component;
    return false;
}

static bool css_enhanced_pseudo_where_matches(CssStyleNode* node, CSSSelectorComponent* component) {
    // :where() pseudo-class matching (same as :is() but with 0 specificity)
    // This would require checking if the node's element matches any selector in the component
    // For now, return false as this requires full selector matching context
    (void)node;
    (void)component;
    return false;
}

static bool css_enhanced_pseudo_not_matches(CssStyleNode* node, CSSSelectorComponent* component) {
    // :not() pseudo-class matching (matches if element doesn't match selector)
    // This would require checking if the node's element does NOT match the selector
    // For now, return false as this requires full selector matching context
    (void)node;
    (void)component;
    return false;
}

static bool css_nesting_parent_matches(CssStyleNode* node, CSSSelectorComponent* component) {
    // CSS nesting parent selector (&) matching
    // This would require checking if the node's element matches the parent selector context
    // For now, return false as this requires nesting context
    (void)node;
    (void)component;
    return false;
}

// CSS pseudo-class matching
bool css_pseudo_class_matches(CssEngine* engine,
                             CssSelectorType pseudo_type,
                             CssStyleNode* element) {
    // Basic pseudo-class matching implementation
    if (!engine || !element) return false;

    // Pseudo-classes require DOM element context, not just style nodes
    // This function should be refactored to work with DomElement instead
    // For now, return false for all pseudo-classes
    (void)pseudo_type;
    return false;
}

static void css_style_node_set_id(CssStyleNode* node, const char* id) {
    // Note: CssStyleNode is for CSS properties, not DOM elements
    // ID setting should be done on DomElement structures
    // This is a no-op for style nodes
    (void)node;
    (void)id;
}

static void css_style_node_add_attribute_selector(CssStyleNode* node, const char* attr_name, const char* attr_value) {
    // Note: CssStyleNode is for CSS properties, not DOM elements
    // Attribute selectors should be handled on DomElement structures
    // This is a no-op for style nodes
    (void)node;
    (void)attr_name;
    (void)attr_value;
}

// Enhanced rule matching functions
static bool css_enhanced_rule_matches_element(CssRule* rule, CssStyleNode* element) {
    // Rule matching requires DOM element context, not just style nodes
    // CssStyleNode represents a single property, not an element
    // This function should be refactored to work with DomElement
    if (!rule || !element) return false;
    return false; // Cannot match rules to style nodes directly
}

static void css_enhanced_sort_rules_by_cascade(CssRule** rules, int rule_count) {
    // Sort CSS rules by cascade priority (specificity, origin, source order)
    if (!rules || rule_count <= 1) return;

    // Simple bubble sort by cascade order
    // In production, use qsort or merge sort for better performance
    for (int i = 0; i < rule_count - 1; i++) {
        for (int j = 0; j < rule_count - i - 1; j++) {
            CssRule* rule_a = rules[j];
            CssRule* rule_b = rules[j + 1];

            if (!rule_a || !rule_b) continue;

            // Simple comparison based on cached specificity
            // Lower specificity should come first (so higher specificity wins)
            if (rule_a->cached_specificity > rule_b->cached_specificity) {
                // Swap
                CssRule* temp = rules[j];
                rules[j] = rules[j + 1];
                rules[j + 1] = temp;
            }
        }
    }
}

static void css_enhanced_apply_rule_to_element(CssRule* rule, CssStyleNode* element, Pool* pool) {
    // Apply CSS rule declarations to an element
    // Note: This should work with DomElement and style trees, not CssStyleNode directly
    if (!rule || !element || !pool) return;

    // CssStyleNode represents a property, not an element
    // Rule application requires a style tree and DOM element context
    // This is a placeholder that does nothing without proper context
    (void)rule;
    (void)element;
    (void)pool;
}

// Enhanced CSS Engine creation
CssEngine* css_engine_create(Pool* pool) {
    if (!pool) return NULL;

    // Initialize CSS property system FIRST (required for property lookups)
    if (!css_property_system_init(pool)) {
        fprintf(stderr, "[CSS Engine] Failed to initialize property system\n");
        return NULL;
    }

    CssEngine* engine = (CssEngine*)pool_calloc(pool, sizeof(CssEngine));
    if (!engine) return NULL;

    engine->pool = pool;

    // Initialize core components
    engine->tokenizer = css_tokenizer_create(pool);
    // Removed: engine->selector_parser (legacy linked-list parser removed)
    engine->value_parser = css_property_value_parser_create(pool);

    // Initialize style storage
    engine->style_tree = avl_tree_create(pool);
    engine->style_engine = css_style_engine_create(pool);

    // Enable all CSS3+ features by default
    engine->features.css_nesting = true;
    engine->features.css_cascade_layers = true;
    engine->features.css_container_queries = true;
    engine->features.css_scope = true;
    engine->features.css_custom_selectors = true;
    engine->features.css_mixins = false;  // Experimental
    engine->features.css_color_4 = true;
    engine->features.css_logical_properties = true;
    engine->features.css_subgrid = true;
    engine->features.css_anchor_positioning = false; // Experimental

    // Configure performance options
    engine->performance.cache_parsed_selectors = true;
    engine->performance.cache_computed_values = true;
    engine->performance.optimize_specificity = true;
    engine->performance.parallel_parsing = false; // Not implemented yet
    engine->performance.max_cache_size = 1000;

    // Set default document context
    engine->context.base_url = "";
    engine->context.document_charset = "UTF-8";
    engine->context.color_scheme = "auto";
    engine->context.viewport_width = 1920.0;
    engine->context.viewport_height = 1080.0;
    engine->context.device_pixel_ratio = 1.0;
    engine->context.root_font_size = 16.0;
    engine->context.reduced_motion = false;
    engine->context.high_contrast = false;

    // Initialize statistics
    memset(&engine->stats, 0, sizeof(engine->stats));

    return engine;
}

void css_engine_destroy(CssEngine* engine) {
    if (!engine) return;

    // Cleanup components
    css_tokenizer_destroy(engine->tokenizer);
    // Removed: css_selector_parser_destroy (legacy parser removed)
    css_property_value_parser_destroy(engine->value_parser);

    if (engine->style_tree) {
        avl_tree_destroy(engine->style_tree);
    }

    if (engine->style_engine) {
        css_style_engine_destroy(engine->style_engine);
    }

    // Engine itself is pool-allocated, so it will be cleaned up with the pool
}

// Configuration functions
void css_engine_enable_feature(CssEngine* engine, const char* feature_name, bool enabled) {
    if (!engine || !feature_name) return;

    if (strcmp(feature_name, "css-nesting") == 0) {
        engine->features.css_nesting = enabled;
    } else if (strcmp(feature_name, "cascade-layers") == 0) {
        engine->features.css_cascade_layers = enabled;
    } else if (strcmp(feature_name, "container-queries") == 0) {
        engine->features.css_container_queries = enabled;
    } else if (strcmp(feature_name, "css-scope") == 0) {
        engine->features.css_scope = enabled;
    } else if (strcmp(feature_name, "custom-selectors") == 0) {
        engine->features.css_custom_selectors = enabled;
    } else if (strcmp(feature_name, "css-mixins") == 0) {
        engine->features.css_mixins = enabled;
    } else if (strcmp(feature_name, "css-color-4") == 0) {
        engine->features.css_color_4 = enabled;
    } else if (strcmp(feature_name, "logical-properties") == 0) {
        engine->features.css_logical_properties = enabled;
    } else if (strcmp(feature_name, "css-subgrid") == 0) {
        engine->features.css_subgrid = enabled;
    } else if (strcmp(feature_name, "anchor-positioning") == 0) {
        engine->features.css_anchor_positioning = enabled;
    }
}

void css_engine_set_viewport(CssEngine* engine, double width, double height) {
    if (!engine) return;

    engine->context.viewport_width = width;
    engine->context.viewport_height = height;
}

void css_engine_set_color_scheme(CssEngine* engine, const char* scheme) {
    if (!engine || !scheme) return;

    // Copy scheme string
    size_t len = strlen(scheme);
    char* scheme_copy = (char*)pool_alloc(engine->pool, len + 1);
    if (scheme_copy) {
        strcpy(scheme_copy, scheme);
        engine->context.color_scheme = scheme_copy;
    }
}

void css_engine_set_root_font_size(CssEngine* engine, double size) {
    if (!engine || size <= 0) return;

    engine->context.root_font_size = size;
}

// Enhanced CSS parsing
CssStylesheet* css_enhanced_parse_stylesheet(CssEngine* engine,
    const char* css_text, const char* base_url) {
    if (!engine || !css_text) return NULL;

    clock_t start_time = clock();

    fprintf(stderr, "[CSS Integration] Starting enhanced CSS parsing\n");
    fprintf(stderr, "[CSS Integration] Input length: %zu chars\n", strlen(css_text));
    fprintf(stderr, "[CSS Integration] Base URL: %s\n", base_url ? base_url : "(none)");
    log_debug("Starting enhanced CSS parsing");

    CssStylesheet* stylesheet = (CssStylesheet*)pool_calloc(engine->pool, sizeof(CssStylesheet));
    if (!stylesheet) {
        fprintf(stderr, "[CSS Integration] ERROR: Failed to allocate stylesheet\n");
        return NULL;
    }

    // Set stylesheet metadata
    if (base_url) {
        size_t url_len = strlen(base_url);
        char* url_copy = (char*)pool_alloc(engine->pool, url_len + 1);
        if (url_copy) {
            strcpy(url_copy, base_url);
            stylesheet->origin_url = url_copy;
        }
    }

    // Initialize rule storage
    stylesheet->rule_capacity = 64;
    stylesheet->rules = (CssRule**)pool_alloc(engine->pool, stylesheet->rule_capacity * sizeof(CssRule*));

    // Tokenize the CSS
    CssToken* tokens;
    fprintf(stderr, "[CSS Integration] Starting tokenization...\n");
    log_debug("Tokenizing CSS input");
    int token_count = css_tokenizer_tokenize(engine->tokenizer, css_text, strlen(css_text), &tokens);

    if (token_count <= 0) {
        fprintf(stderr, "[CSS Integration] WARNING: Tokenization returned %d tokens\n", token_count);
        clock_t end_time = clock();
        stylesheet->parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
        engine->stats.stylesheets_parsed++;
        return stylesheet;
    }

    fprintf(stderr, "[CSS Integration] Tokenization complete: %d tokens\n", token_count);

    // Parse rules from tokens
    fprintf(stderr, "[CSS Integration] Parsing CSS rules from %d tokens\n", token_count);
    log_debug("Parsing CSS rules from %d tokens", token_count);

    int token_index = 0;
    int rules_parsed = 0;
    int rules_skipped = 0;

    while (token_index < token_count) {
        // Skip whitespace between rules
        int whitespace_start = token_index;
        while (token_index < token_count &&
               (tokens[token_index].type == CSS_TOKEN_WHITESPACE ||
                tokens[token_index].type == CSS_TOKEN_COMMENT)) {
            token_index++;
        }

        if (token_index > whitespace_start) {
            fprintf(stderr, "[CSS Integration] Skipped %d whitespace/comment tokens\n",
                    token_index - whitespace_start);
        }

        if (token_index >= token_count) break;

        fprintf(stderr, "[CSS Integration] Parsing rule starting at token %d (type=%d)\n",
                token_index, tokens[token_index].type);

        // Parse a rule
        CssRule* rule = NULL;
        int tokens_consumed = css_parse_rule_from_tokens_internal(
            tokens + token_index, token_count - token_index, engine->pool, &rule);

        if (tokens_consumed > 0) {
            fprintf(stderr, "[CSS Integration] Consumed %d tokens, rule=%p\n",
                    tokens_consumed, (void*)rule);
            token_index += tokens_consumed;

            if (rule) {
                rules_parsed++;
                // Add rule to stylesheet
                if (stylesheet->rule_count >= stylesheet->rule_capacity) {
                    // Expand capacity
                    stylesheet->rule_capacity *= 2;
                    fprintf(stderr, "[CSS Integration] Expanding rule capacity to %zu\n",
                            stylesheet->rule_capacity);
                    CssRule** new_rules = (CssRule**)pool_alloc(engine->pool,
                                                                             stylesheet->rule_capacity * sizeof(CssRule*));
                    if (new_rules) {
                        memcpy(new_rules, stylesheet->rules, stylesheet->rule_count * sizeof(CssRule*));
                        stylesheet->rules = new_rules;
                    }
                }

                if (stylesheet->rule_count < stylesheet->rule_capacity) {
                    stylesheet->rules[stylesheet->rule_count++] = rule;

                    // Update feature usage flags
                    css_enhanced_detect_features_in_rule(stylesheet, rule);
                }
            } else {
                rules_skipped++;
                fprintf(stderr, "[CSS Integration] Rule was NULL (likely @-rule, skipped)\n");
            }
        } else {
            // Failed to parse, skip to next rule
            fprintf(stderr, "[CSS Integration] ERROR: Failed to parse rule, searching for next rule\n");

            // We need to skip to the end of this failed rule's declaration block
            // Look for the opening brace first (in case we failed before it)
            int brace_depth = 0;
            bool found_open_brace = false;

            // Search for opening brace
            while (token_index < token_count && !found_open_brace) {
                if (tokens[token_index].type == CSS_TOKEN_LEFT_BRACE) {
                    found_open_brace = true;
                    brace_depth = 1;
                    token_index++;
                    break;
                } else if (tokens[token_index].type == CSS_TOKEN_SEMICOLON) {
                    // Hit a semicolon before opening brace, might be @-rule
                    token_index++;
                    break;
                }
                token_index++;
            }

            // If we found an opening brace, skip to matching closing brace
            if (found_open_brace) {
                while (token_index < token_count && brace_depth > 0) {
                    if (tokens[token_index].type == CSS_TOKEN_LEFT_BRACE) {
                        brace_depth++;
                    } else if (tokens[token_index].type == CSS_TOKEN_RIGHT_BRACE) {
                        brace_depth--;
                    }
                    token_index++;
                }
                fprintf(stderr, "[CSS Integration] Skipped to end of failed rule block\n");
            }

            rules_skipped++;
        }
    }

    fprintf(stderr, "[CSS Integration] Parsed %d CSS rules (%d skipped)\n", rules_parsed, rules_skipped);
    fprintf(stderr, "[CSS Integration] Total rules in stylesheet: %zu\n", stylesheet->rule_count);
    log_debug("Parsed %zu CSS rules", stylesheet->rule_count);

    clock_t end_time = clock();
    stylesheet->parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

    // Update engine statistics
    engine->stats.rules_parsed += stylesheet->rule_count;
    engine->stats.stylesheets_parsed++;
    engine->stats.parse_time += stylesheet->parse_time;
    log_debug("Finished enhanced CSS parsing");

    return stylesheet;
}

// Feature detection in rules
void css_enhanced_detect_features_in_rule(CssStylesheet* stylesheet, CssRule* rule) {
    if (!stylesheet || !rule) return;

    // Legacy selector_list field removed - nesting detection moved to modern selector_group
    // TODO: Implement nesting detection using CssSelectorGroup* format

    // Check for custom properties and other features
    for (int i = 0; i < rule->property_count; i++) {
        CssValue* value = rule->property_values[i];
        if (value) {
            switch (value->type) {
                case CSS_VALUE_ENHANCED_VAR:
                    stylesheet->uses_custom_properties = true;
                    break;
                case CSS_VALUE_ENHANCED_CALC:
                case CSS_VALUE_ENHANCED_MIN:
                case CSS_VALUE_ENHANCED_MAX:
                case CSS_VALUE_ENHANCED_CLAMP:
                    // Math functions are part of core CSS now
                    break;
                default:
                    break;
            }
        }
    }
}

// Enhanced cascade integration
bool css_enhanced_apply_cascade(CssEngine* engine,
                               CssStyleNode* element,
                               CssStylesheet** stylesheets,
                               int stylesheet_count) {
    if (!engine || !element || !stylesheets) return false;

    clock_t start_time = clock();

    // Collect all matching rules from all stylesheets
    CssRule** matching_rules = (CssRule**)pool_alloc(engine->pool, 1000 * sizeof(CssRule*));
    int matching_count = 0;

    for (int i = 0; i < stylesheet_count; i++) {
        CssStylesheet* stylesheet = stylesheets[i];
        if (!stylesheet) continue;

        for (int j = 0; j < stylesheet->rule_count; j++) {
            CssRule* rule = stylesheet->rules[j];
            if (!rule) continue;

            // Check if any selector in the rule matches the element
            if (css_enhanced_rule_matches_element(rule, element)) {
                if (matching_count < 1000) {
                    matching_rules[matching_count++] = rule;
                }
            }
        }
    }

    // Sort rules by cascade priority (specificity, source order, importance, etc.)
    css_enhanced_sort_rules_by_cascade(matching_rules, matching_count);

    // Apply rules in cascade order
    for (int i = 0; i < matching_count; i++) {
        CssRule* rule = matching_rules[i];
        css_enhanced_apply_rule_to_element(rule, element, engine->pool);
    }

    clock_t end_time = clock();
    engine->stats.cascade_time += ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    engine->stats.cascade_calculations++;

    return true;
}

#if 0  // DISABLED: Legacy function using removed CSSComplexSelector type
// Selector matching with enhanced features
bool css_enhanced_selector_matches_element(CssEngine* engine,
                                          CSSComplexSelector* selector,
                                          CssStyleNode* element,
                                          CssStyleNode* scope_root) {
    if (!engine || !selector || !element) return false;

    // Process selector components
    CSSSelectorComponent* component = selector->components;
    bool matches = true;

    while (component && matches) {
        switch (component->type) {
            case CSS_SELECTOR_TYPE_ELEMENT:
                matches = css_style_node_matches_element_name(element, component->value);
                break;

            case CSS_SELECTOR_TYPE_CLASS:
                matches = css_style_node_has_class(element, component->value);
                break;

            case CSS_SELECTOR_TYPE_ID:
                matches = css_style_node_matches_id(element, component->value);
                break;

            case CSS_SELECTOR_TYPE_UNIVERSAL:
                // Universal selector always matches
                matches = true;
                break;

            // Enhanced pseudo-classes
            case CSS_SELECTOR_PSEUDO_HAS:
                matches = css_enhanced_pseudo_has_matches(element, component);
                break;

            case CSS_SELECTOR_PSEUDO_IS:
                matches = css_enhanced_pseudo_is_matches(element, component);
                break;

            case CSS_SELECTOR_PSEUDO_WHERE:
                matches = css_enhanced_pseudo_where_matches(element, component);
                break;

            case CSS_SELECTOR_PSEUDO_NOT:
                matches = !css_enhanced_pseudo_not_matches(element, component);
                break;

            // Nesting selectors
            case CSS_SELECTOR_NESTING_PARENT:
                // & selector - check if element matches parent context
                matches = css_enhanced_nesting_parent_matches(component, element);
                break;

            default:
                // Handle other pseudo-classes and attributes
                matches = css_enhanced_pseudo_class_matches(component, element);
                break;
        }

        component = component->next;
    }

    return matches;
}
#endif  // DISABLED

// Statistics and monitoring
void css_engine_update_stats(CssEngine* engine) {
    if (!engine) return;

    // Update memory usage (actual memory tracking would require walking all pools)
    engine->stats.memory_usage = 0; // Memory tracking requires pool introspection

    // Other statistics are updated during parsing and cascade operations
}

void css_engine_print_stats(CssEngine* engine) {
    if (!engine) return;

    printf("CSS Enhanced Engine Statistics:\n");
    printf("  Rules parsed: %d\n", engine->stats.rules_parsed);
    printf("  Selectors cached: %d\n", engine->stats.selectors_cached);
    printf("  Values computed: %d\n", engine->stats.values_computed);
    printf("  Cascade calculations: %d\n", engine->stats.cascade_calculations);
    printf("  Parse time: %.4f seconds\n", engine->stats.parse_time);
    printf("  Cascade time: %.4f seconds\n", engine->stats.cascade_time);
    printf("  Memory usage: %zu bytes\n", engine->stats.memory_usage);

    printf("\nFeatures enabled:\n");
    printf("  CSS Nesting: %s\n", engine->features.css_nesting ? "Yes" : "No");
    printf("  Cascade Layers: %s\n", engine->features.css_cascade_layers ? "Yes" : "No");
    printf("  Container Queries: %s\n", engine->features.css_container_queries ? "Yes" : "No");
    printf("  CSS Scope: %s\n", engine->features.css_scope ? "Yes" : "No");
    printf("  Color Level 4: %s\n", engine->features.css_color_4 ? "Yes" : "No");
}

double css_engine_get_parse_time(CssEngine* engine) {
    return engine ? engine->stats.parse_time : 0.0;
}

size_t css_engine_get_memory_usage(CssEngine* engine) {
    return engine ? engine->stats.memory_usage : 0;
}

// Additional utility functions for the CSS integration layer

// Compare function for style nodes in AVL tree
int css_style_node_compare(const void* a, const void* b, void* udata) {
    (void)a; (void)b; (void)udata;
    return 0; // Simple comparison, full implementation would compare property IDs
}

void css_style_node_cleanup(void* node, void* udata) {
    (void)node; (void)udata;
    // Cleanup is handled by pool deallocation
}

CssStyleEngine* css_style_engine_create(Pool* pool) {
    if (!pool) return NULL;

    CssStyleEngine* engine = (CssStyleEngine*)pool_calloc(pool, sizeof(CssStyleEngine));
    if (!engine) return NULL;

    engine->pool = pool;
    engine->version = 1;

    return engine;
}

void css_style_engine_destroy(CssStyleEngine* engine) {
    (void)engine;
    // Memory managed by pool
}

// ============================================================================
// CSS Enhanced Functions - Implementation
// ============================================================================

bool css_enhanced_nesting_parent_matches(const CssSelector* selector, const CssStyleNode* node) {
    // CSS nesting parent selector (&) matching
    // This would require checking if the node's element matches the parent selector context
    // For now, return false as this requires full DOM traversal context
    (void)selector; (void)node;
    return false; // Requires nesting context
}

bool css_enhanced_pseudo_class_matches(const CssSelector* selector, const CssStyleNode* node) {
    // Enhanced pseudo-class matching
    // This would require checking if the node's element matches pseudo-class state
    // For now, return false as this requires full selector matching context
    (void)selector; (void)node;
    return false; // Requires DOM element context
}

CssEngineStats css_engine_get_stats(const CssEngine* engine) {
    CssEngineStats stats = {0};
    if (!engine) return stats;

    stats.rules_processed = engine->stats.rules_processed;
    stats.selectors_processed = engine->selectors_processed; // Top-level field
    stats.properties_processed = engine->stats.properties_computed;
    stats.parse_errors = engine->parse_errors; // Top-level field
    stats.validation_errors = engine->validation_errors; // Top-level field
    stats.parse_time = engine->stats.parse_time;
    stats.cascade_time = engine->stats.cascade_time;
    stats.memory_usage = engine->stats.memory_usage;
    return stats;
}

CssStylesheet* css_parse_stylesheet(CssEngine* engine, const char* css_text, const char* source_url) {
    return css_enhanced_parse_stylesheet(engine, css_text, source_url);
}
