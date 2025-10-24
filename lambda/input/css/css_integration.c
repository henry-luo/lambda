#include "css_integration.h"
#include "css_property_value_parser.h"
#include "css_parser.h"
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

// Style node management functions (stub implementations)
static void css_style_node_init(CssStyleNode* node, const char* element_name, Pool* pool) {
    if (!node || !pool) return;
    memset(node, 0, sizeof(CssStyleNode));
    // Basic initialization - element_name handling would go here
}

static void css_style_node_set_element_name(CssStyleNode* node, const char* element_name) {
    // Stub: element name setting would be implemented here
    if (!node || !element_name) return;
}

static void css_style_node_add_class(CssStyleNode* node, const char* class_name) {
    // Stub: class name addition would be implemented here
    if (!node || !class_name) return;
}

static void css_style_node_add_property(CssStyleNode* node, const char* prop_name,
                                       void* value, Pool* pool) {
    // Stub: property addition would be implemented here
    if (!node || !prop_name || !value || !pool) return;
}

// Additional style node query functions (stubs)
static bool css_style_node_has_class(CssStyleNode* node, const char* class_name) {
    // Stub: class checking would be implemented here
    if (!node || !class_name) return false;
    return false; // Default stub behavior
}

static bool css_style_node_matches_id(CssStyleNode* node, const char* id) {
    // Stub: ID matching would be implemented here
    if (!node || !id) return false;
    return false; // Default stub behavior
}

static bool css_style_node_matches_element_name(CssStyleNode* node, const char* element_name) {
    // Stub: element name matching would be implemented here
    if (!node || !element_name) return false;
    return false; // Default stub behavior
}

// Enhanced CSS4+ pseudo-selector functions (stubs)
static bool css_enhanced_pseudo_has_matches(CssStyleNode* node, CSSSelectorComponent* component) {
    // Stub: :has() pseudo-class matching
    if (!node || !component) return false;
    return false;
}

static bool css_enhanced_pseudo_is_matches(CssStyleNode* node, CSSSelectorComponent* component) {
    // Stub: :is() pseudo-class matching
    if (!node || !component) return false;
    return false;
}

static bool css_enhanced_pseudo_where_matches(CssStyleNode* node, CSSSelectorComponent* component) {
    // Stub: :where() pseudo-class matching
    if (!node || !component) return false;
    return false;
}

static bool css_enhanced_pseudo_not_matches(CssStyleNode* node, CSSSelectorComponent* component) {
    // Stub: :not() pseudo-class matching
    if (!node || !component) return false;
    return false;
}

static bool css_nesting_parent_matches(CssStyleNode* node, CSSSelectorComponent* component) {
    // Stub: CSS nesting parent selector (&) matching
    if (!node || !component) return false;
    return false;
}

// CSS pseudo-class matching (stub)
bool css_pseudo_class_matches(CssEngine* engine,
                             CssSelectorType pseudo_type,
                             CssStyleNode* element) {
    // Stub: pseudo-class matching would be implemented here
    if (!engine || !element) return false;
    return false;
}

static void css_style_node_set_id(CssStyleNode* node, const char* id) {
    // Stub: ID setting would be implemented here
    if (!node || !id) return;
}

static void css_style_node_add_attribute_selector(CssStyleNode* node, const char* attr_name, const char* attr_value) {
    // Stub: attribute selector addition would be implemented here
    if (!node || !attr_name) return;
}

// Enhanced rule matching functions (stubs)
static bool css_enhanced_rule_matches_element(CssRule* rule, CssStyleNode* element) {
    // Stub: enhanced rule matching would be implemented here
    if (!rule || !element) return false;
    return false; // Default stub behavior
}

static void css_enhanced_sort_rules_by_cascade(CssRule** rules, int rule_count) {
    // Stub: cascade sorting would be implemented here
    if (!rules || rule_count <= 0) return;
}

static void css_enhanced_apply_rule_to_element(CssRule* rule, CssStyleNode* element, Pool* pool) {
    // Stub: rule application would be implemented here
    if (!rule || !element || !pool) return;
}

// Enhanced CSS Engine creation
CssEngine* css_enhanced_engine_create(Pool* pool) {
    if (!pool) return NULL;

    CssEngine* engine = (CssEngine*)pool_calloc(pool, sizeof(CssEngine));
    if (!engine) return NULL;

    engine->pool = pool;

    // Initialize core components
    engine->tokenizer = css_tokenizer_enhanced_create(pool);
    engine->selector_parser = css_selector_parser_create(pool);
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

void css_enhanced_engine_destroy(CssEngine* engine) {
    if (!engine) return;

    // Cleanup components
    css_tokenizer_enhanced_destroy(engine->tokenizer);
    css_selector_parser_destroy(engine->selector_parser);
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
void css_enhanced_engine_enable_feature(CssEngine* engine, const char* feature_name, bool enabled) {
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

void css_enhanced_engine_set_viewport(CssEngine* engine, double width, double height) {
    if (!engine) return;

    engine->context.viewport_width = width;
    engine->context.viewport_height = height;
}

void css_enhanced_engine_set_color_scheme(CssEngine* engine, const char* scheme) {
    if (!engine || !scheme) return;

    // Copy scheme string
    size_t len = strlen(scheme);
    char* scheme_copy = (char*)pool_alloc(engine->pool, len + 1);
    if (scheme_copy) {
        strcpy(scheme_copy, scheme);
        engine->context.color_scheme = scheme_copy;
    }
}

void css_enhanced_engine_set_root_font_size(CssEngine* engine, double size) {
    if (!engine || size <= 0) return;

    engine->context.root_font_size = size;
}

// Enhanced CSS parsing
CssStylesheet* css_enhanced_parse_stylesheet(CssEngine* engine,
    const char* css_text, const char* base_url) {
    if (!engine || !css_text) return NULL;

    clock_t start_time = clock();

    log_debug("Starting enhanced CSS parsing");
    CssStylesheet* stylesheet = (CssStylesheet*)pool_calloc(engine->pool, sizeof(CssStylesheet));
    if (!stylesheet) return NULL;

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
    log_debug("Tokenizing CSS input");
    int token_count = css_tokenizer_enhanced_tokenize(engine->tokenizer, css_text, strlen(css_text), &tokens);

    if (token_count <= 0) {
        clock_t end_time = clock();
        stylesheet->parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
        return stylesheet;
    }

    // Parse rules from tokens
    fprintf(stderr, "[DEBUG] Parsing CSS rules from %d tokens\n", token_count);
    log_debug("Parsing CSS rules from %d tokens", token_count);

    int token_index = 0;
    while (token_index < token_count) {
        // Skip whitespace between rules
        while (token_index < token_count &&
               (tokens[token_index].type == CSS_TOKEN_WHITESPACE ||
                tokens[token_index].type == CSS_TOKEN_COMMENT)) {
            token_index++;
        }

        if (token_index >= token_count) break;

        // Parse a rule
        CssRule* rule = NULL;
        int tokens_consumed = css_parse_rule_from_tokens_internal(
            tokens + token_index, token_count - token_index, engine->pool, &rule);

        if (tokens_consumed > 0) {
            token_index += tokens_consumed;

            if (rule) {
                // Add rule to stylesheet
                if (stylesheet->rule_count >= stylesheet->rule_capacity) {
                    // Expand capacity
                    stylesheet->rule_capacity *= 2;
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
            }
        } else {
            // Failed to parse, skip to next rule
            // Look for the next closing brace or semicolon
            while (token_index < token_count &&
                   tokens[token_index].type != CSS_TOKEN_RIGHT_BRACE &&
                   tokens[token_index].type != CSS_TOKEN_SEMICOLON) {
                token_index++;
            }
            if (token_index < token_count) token_index++; // skip the terminator
        }
    }

    fprintf(stderr, "[DEBUG] Parsed %zu CSS rules\n", stylesheet->rule_count);
    log_debug("Parsed %zu CSS rules", stylesheet->rule_count);

    clock_t end_time = clock();
    stylesheet->parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

    // Update engine statistics
    engine->stats.rules_parsed += stylesheet->rule_count;
    engine->stats.parse_time += stylesheet->parse_time;
    log_debug("Finished enhanced CSS parsing");

    return stylesheet;
}

// Feature detection in rules
void css_enhanced_detect_features_in_rule(CssStylesheet* stylesheet, CssRule* rule) {
    if (!stylesheet || !rule) return;

    // Check for CSS Nesting (& selector)
    if (rule->selector_list) {
        CSSComplexSelector* current = rule->selector_list;
        while (current) {
            CSSSelectorComponent* component = current->components;
            while (component) {
                if (component->type == CSS_SELECTOR_NESTING_PARENT ||
                    component->type == CSS_SELECTOR_NESTING_DESCENDANT) {
                    stylesheet->uses_nesting = true;
                }
                component = component->next;
            }
            current = current->next;
        }
    }

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

// Style node integration
bool css_enhanced_rule_to_style_node(CssEngine* engine,
                                     CssRule* rule,
                                     CssStyleNode** style_nodes,
                                     int* node_count) {
    if (!engine || !rule || !style_nodes || !node_count) return false;

    *node_count = 0;

    // Convert each selector in the selector list to a style node
    CSSComplexSelector* current_selector = rule->selector_list;
    int max_nodes = 10; // Reasonable limit
    *style_nodes = (CssStyleNode*)pool_alloc(engine->pool, max_nodes * sizeof(CssStyleNode));

    while (current_selector && *node_count < max_nodes) {
        CssStyleNode* node = css_enhanced_selector_to_style_node(engine, current_selector);
        if (node) {
            (*style_nodes)[*node_count] = *node;

            // Add properties to style node
            for (int i = 0; i < rule->property_count; i++) {
                const char* prop_name = rule->property_names[i];
                CssValue* enhanced_value = rule->property_values[i];

                // Convert enhanced value to basic CSS value for style node
                // Convert enhanced value to basic representation for compatibility
                const char* value_str = css_value_enhanced_to_string(enhanced_value, engine->pool);
                css_style_node_add_property(&(*style_nodes)[*node_count], prop_name, (void*)value_str, engine->pool);
            }

            // Set specificity for cascade calculation
            if (!rule->specificity_computed) {
                CssSpecificity spec = css_calculate_specificity(current_selector);
                // Convert specificity to numeric value: important + (inline << 24) + (ids << 16) + (classes << 8) + elements
                rule->cached_specificity = (spec.important ? 0x80000000 : 0) +
                                         (spec.inline_style << 24) +
                                         (spec.ids << 16) +
                                         (spec.classes << 8) +
                                         spec.elements;
                rule->specificity_computed = true;
            }
            // Note: specificity is handled via winning_decl in CssStyleNode

            (*node_count)++;
        }

        current_selector = current_selector->next;
    }

    return *node_count > 0;
}

CssStyleNode* css_enhanced_selector_to_style_node(CssEngine* engine,
                                                  CSSComplexSelector* selector) {
    if (!engine || !selector) return NULL;

    CssStyleNode* node = (CssStyleNode*)pool_calloc(engine->pool, sizeof(CssStyleNode));
    if (!node) return NULL;

    // Initialize style node
    css_style_node_init(node, "element", engine->pool);

    // Process selector components
    CSSSelectorComponent* component = selector->components;
    while (component) {
        switch (component->type) {
            case CSS_SELECTOR_TYPE_ELEMENT:
                if (component->value) {
                    css_style_node_set_element_name(node, component->value);
                }
                break;

            case CSS_SELECTOR_TYPE_CLASS:
                if (component->value) {
                    css_style_node_add_class(node, component->value);
                }
                break;

            case CSS_SELECTOR_TYPE_ID:
                if (component->value) {
                    css_style_node_set_id(node, component->value);
                }
                break;

            case CSS_SELECTOR_ATTR_EXACT:
            case CSS_SELECTOR_ATTR_CONTAINS:
            case CSS_SELECTOR_ATTR_BEGINS:
            case CSS_SELECTOR_ATTR_ENDS:
            case CSS_SELECTOR_ATTR_SUBSTRING:
            case CSS_SELECTOR_ATTR_EXISTS:
                // Add attribute selectors to style node
                if (component->value) {
                    css_style_node_add_attribute_selector(node, component->value, component->attribute_value);
                }
                break;

            default:
                // Handle pseudo-classes and other selector types
                break;
        }

        component = component->next;
    }

    return node;
}

// Value conversion from enhanced to string representation
static const char* css_enhanced_value_to_string_repr(CssEngine* engine,
                                                    CssValue* enhanced_value,
                                                    const char* property_name) {
    if (!enhanced_value || !engine) return "invalid";

    // Use the existing css_value_enhanced_to_string function
    return css_value_enhanced_to_string(enhanced_value, engine->pool);
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

// Statistics and monitoring
void css_enhanced_engine_update_stats(CssEngine* engine) {
    if (!engine) return;

    // Update memory usage (using stub value)
    engine->stats.memory_usage = 0; // Stub: actual memory tracking would be implemented here

    // Other statistics are updated during parsing and cascade operations
}

void css_enhanced_engine_print_stats(CssEngine* engine) {
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

double css_enhanced_engine_get_parse_time(CssEngine* engine) {
    return engine ? engine->stats.parse_time : 0.0;
}

size_t css_enhanced_engine_get_memory_usage(CssEngine* engine) {
    return engine ? engine->stats.memory_usage : 0;
}

// Missing function stubs - to be implemented
int css_style_node_compare(const void* a, const void* b, void* udata) {
    (void)a; (void)b; (void)udata;
    return 0; // Stub
}

void css_style_node_cleanup(void* node, void* udata) {
    (void)node; (void)udata;
    // Stub
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
    // Stub implementation for CSS nesting parent selector (&)
    // In a full implementation, this would check if the node matches the parent selector context
    (void)selector; (void)node;
    return false; // Default to no match for safety
}

bool css_enhanced_pseudo_class_matches(const CssSelector* selector, const CssStyleNode* node) {
    // Stub implementation for enhanced pseudo-class matching
    // In a full implementation, this would handle :hover, :focus, :nth-child, etc.
    (void)selector; (void)node;
    return false; // Default to no match for safety
}

// Wrapper functions for API compatibility
CssEngine* css_engine_create(Pool* pool) {
    return css_enhanced_engine_create(pool);
}

void css_engine_destroy(CssEngine* engine) {
    return css_enhanced_engine_destroy(engine);
}

void css_engine_set_viewport(CssEngine* engine, double width, double height) {
    return css_enhanced_engine_set_viewport(engine, width, height);
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
