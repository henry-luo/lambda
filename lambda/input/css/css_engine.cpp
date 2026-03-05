#include "css_engine.hpp"
#include "css_value_parser.hpp"
#include "css_parser.hpp"
#include "css_style_node.hpp"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../../lib/str.h"

// Forward declarations
int css_style_node_compare(const void* a, const void* b, void* udata);
void css_style_node_cleanup(void* node, void* udata);

// Enhanced CSS Engine creation
CssEngine* css_engine_create(Pool* pool) {
    // Note: CssValue size is 24 bytes on 64-bit systems (1 byte enum + 7 padding + 16 byte union)
    // The union size is determined by the largest member (16 bytes - either pointer+int for list, or color struct)
    // Color components (HSLA/HWBA/LABA/LCHA) use a pointer to reduce union size from 40 to 16 bytes
    static_assert(sizeof(CssValue) == 24, "Expected CssValue to be 24 bytes on 64-bit systems");

    if (!pool) return NULL;

    // Initialize CSS property system FIRST (required for property lookups)
    if (!css_property_system_init(pool)) {
        log_error("Failed to initialize CSS property system");
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
        str_copy(scheme_copy, len + 1, scheme, len);
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

    log_debug("Starting enhanced CSS parsing: %zu chars, base_url=%s", strlen(css_text), base_url ? base_url : "(none)");

    CssStylesheet* stylesheet = (CssStylesheet*)pool_calloc(engine->pool, sizeof(CssStylesheet));
    if (!stylesheet) {
        log_error("Failed to allocate CSS stylesheet");
        return NULL;
    }

    // Set stylesheet metadata
    if (base_url) {
        size_t url_len = strlen(base_url);
        char* url_copy = (char*)pool_alloc(engine->pool, url_len + 1);
        if (url_copy) {
            str_copy(url_copy, url_len + 1, base_url, url_len);
            stylesheet->origin_url = url_copy;
        }
    }

    // Initialize rule storage
    stylesheet->rule_capacity = 64;
    stylesheet->rules = (CssRule**)pool_alloc(engine->pool, stylesheet->rule_capacity * sizeof(CssRule*));

    // Tokenize the CSS
    CssToken* tokens;
    int token_count = css_tokenizer_tokenize(engine->tokenizer, css_text, strlen(css_text), &tokens);

    if (token_count <= 0) {
        log_debug("CSS tokenization returned %d tokens", token_count);
        clock_t end_time = clock();
        stylesheet->parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
        engine->stats.stylesheets_parsed++;
        return stylesheet;
    }

    // Parse rules from tokens
    log_debug("Parsing CSS rules from %d tokens", token_count);

    int token_index = 0;
    int rules_parsed = 0;
    int rules_skipped = 0;

    while (token_index < token_count) {
        // Skip whitespace between rules
        while (token_index < token_count &&
               (tokens[token_index].type == CSS_TOKEN_WHITESPACE ||
                tokens[token_index].type == CSS_TOKEN_COMMENT)) {
            token_index++;
        }

        if (token_index >= token_count) break;

        // Skip EOF token - nothing left to parse
        if (tokens[token_index].type == CSS_TOKEN_EOF) {
            break;
        }

        // Parse a rule
        CssRule* rule = NULL;
        int tokens_consumed = css_parse_rule_from_tokens_internal(
            tokens + token_index, token_count - token_index, engine->pool, &rule);

        if (tokens_consumed > 0) {
            token_index += tokens_consumed;

            if (rule) {
                rules_parsed++;
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
            } else {
                rules_skipped++;
            }
        } else {
            // Failed to parse, skip to next rule
            log_debug("CSS: Failed to parse rule at token %d, skipping", token_index);

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
            }

            rules_skipped++;
        }
    }

    log_debug("Parsed %d CSS rules (%d skipped)", rules_parsed, rules_skipped);

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

        }
    }
}

// Statistics and monitoring
void css_engine_update_stats(CssEngine* engine) {
    if (!engine) return;

    // Update memory usage (actual memory tracking would require walking all pools)
    engine->stats.memory_usage = 0; // Memory tracking requires pool introspection

    // Other statistics are updated during parsing and cascade operations
}

void css_engine_print_stats(CssEngine* engine) {
    if (!engine) return;

    log_info("css engine stats: rules_parsed=%zu selectors_cached=%zu values_computed=%zu cascade_calcs=%zu",
             engine->stats.rules_parsed, engine->stats.selectors_cached,
             engine->stats.values_computed, engine->stats.cascade_calculations);
    log_info("css engine timing: parse=%.4fs cascade=%.4fs memory=%zu bytes",
             engine->stats.parse_time, engine->stats.cascade_time, engine->stats.memory_usage);
    log_info("css features: nesting=%d layers=%d containers=%d scope=%d color4=%d",
             engine->features.css_nesting, engine->features.css_cascade_layers,
             engine->features.css_container_queries, engine->features.css_scope,
             engine->features.css_color_4);
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
// Media Query Evaluation
// ============================================================================

/**
 * Parse a length value from a media query condition.
 * Returns the value in pixels, or -1 on error.
 */
static double parse_media_length(const char* value) {
    if (!value) return -1;

    // Skip whitespace
    while (*value && (*value == ' ' || *value == '\t')) value++;

    char* end;
    double num = strtod(value, &end);
    if (end == value) return -1;  // No number found

    // Skip whitespace before unit
    while (*end && (*end == ' ' || *end == '\t')) end++;

    // Parse unit
    if (strncmp(end, "px", 2) == 0) {
        return num;
    } else if (strncmp(end, "em", 2) == 0) {
        return num * 16.0;  // Assume 16px root font size
    } else if (strncmp(end, "rem", 3) == 0) {
        return num * 16.0;  // Assume 16px root font size
    } else if (strncmp(end, "vw", 2) == 0) {
        return -1;  // Would need viewport context
    } else if (strncmp(end, "vh", 2) == 0) {
        return -1;  // Would need viewport context
    } else if (*end == '\0' || *end == ')') {
        // Unitless - treat as pixels
        return num;
    }

    return -1;
}

/**
 * Evaluate a single media feature condition like "(min-width: 768px)"
 */
static bool evaluate_media_feature(CssEngine* engine, const char* feature, const char* value) {
    if (!engine || !feature) return false;

    log_debug("[Media Query] Evaluating feature: %s = %s", feature, value ? value : "(no value)");

    double viewport_width = engine->context.viewport_width;
    double viewport_height = engine->context.viewport_height;

    // min-width
    if (strcmp(feature, "min-width") == 0) {
        double min_w = parse_media_length(value);
        if (min_w < 0) return false;
        bool result = viewport_width >= min_w;
        log_debug("[Media Query] min-width: viewport=%f >= min=%f -> %s",
                  viewport_width, min_w, result ? "true" : "false");
        return result;
    }

    // max-width
    if (strcmp(feature, "max-width") == 0) {
        double max_w = parse_media_length(value);
        if (max_w < 0) return false;
        bool result = viewport_width <= max_w;
        log_debug("[Media Query] max-width: viewport=%f <= max=%f -> %s",
                  viewport_width, max_w, result ? "true" : "false");
        return result;
    }

    // min-height
    if (strcmp(feature, "min-height") == 0) {
        double min_h = parse_media_length(value);
        if (min_h < 0) return false;
        bool result = viewport_height >= min_h;
        log_debug("[Media Query] min-height: viewport=%f >= min=%f -> %s",
                  viewport_height, min_h, result ? "true" : "false");
        return result;
    }

    // max-height
    if (strcmp(feature, "max-height") == 0) {
        double max_h = parse_media_length(value);
        if (max_h < 0) return false;
        bool result = viewport_height <= max_h;
        log_debug("[Media Query] max-height: viewport=%f <= max=%f -> %s",
                  viewport_height, max_h, result ? "true" : "false");
        return result;
    }

    // width (exact match)
    if (strcmp(feature, "width") == 0) {
        double w = parse_media_length(value);
        if (w < 0) return false;
        return viewport_width == w;
    }

    // height (exact match)
    if (strcmp(feature, "height") == 0) {
        double h = parse_media_length(value);
        if (h < 0) return false;
        return viewport_height == h;
    }

    // orientation
    if (strcmp(feature, "orientation") == 0) {
        if (!value) return false;
        if (strcmp(value, "portrait") == 0) {
            return viewport_height >= viewport_width;
        } else if (strcmp(value, "landscape") == 0) {
            return viewport_width > viewport_height;
        }
        return false;
    }

    // prefers-color-scheme
    if (strcmp(feature, "prefers-color-scheme") == 0) {
        if (!value || !engine->context.color_scheme) return false;
        // Treat "auto" as "light" (standard default for web content)
        const char* effective_scheme = engine->context.color_scheme;
        if (strcmp(effective_scheme, "auto") == 0) {
            effective_scheme = "light";
        }
        return strcmp(effective_scheme, value) == 0;
    }

    // prefers-reduced-motion
    if (strcmp(feature, "prefers-reduced-motion") == 0) {
        if (!value) return false;
        if (strcmp(value, "reduce") == 0) {
            return engine->context.reduced_motion;
        } else if (strcmp(value, "no-preference") == 0) {
            return !engine->context.reduced_motion;
        }
        return false;
    }

    // Unknown feature - assume it doesn't match
    log_debug("[Media Query] Unknown feature: %s", feature);
    return false;
}

/**
 * Evaluate a media type like "screen", "print", "all"
 */
static bool evaluate_media_type(const char* type) {
    if (!type) return true;  // No type specified = matches all

    // Skip leading whitespace
    while (*type && (*type == ' ' || *type == '\t')) type++;

    // Check known media types
    if (strcmp(type, "all") == 0) return true;
    if (strcmp(type, "screen") == 0) return true;  // Assume screen media
    if (strcmp(type, "print") == 0) return false;  // Not print media
    if (strcmp(type, "speech") == 0) return false;

    // Unknown type - assume it matches (forward compatibility)
    return true;
}

/**
 * Evaluate a complete media query string.
 *
 * Supports:
 * - Media types: screen, print, all
 * - Features: min-width, max-width, min-height, max-height, orientation
 * - Logical operators: and, not, or (via comma)
 * - Parenthesized feature conditions
 *
 * Examples:
 * - "screen"
 * - "screen and (min-width: 768px)"
 * - "(min-width: 768px) and (max-width: 1024px)"
 * - "screen, print"
 */
bool css_evaluate_media_query(CssEngine* engine, const char* media_query) {
    if (!engine || !media_query) return true;  // Empty query matches all

    log_debug("[Media Query] Evaluating: '%s'", media_query);
    log_debug("[Media Query] Viewport: %f x %f",
              engine->context.viewport_width, engine->context.viewport_height);

    // Make a copy we can modify
    size_t len = strlen(media_query);
    char* query = (char*)pool_alloc(engine->pool, len + 1);
    if (!query) return false;
    str_copy(query, len + 1, media_query, len);

    // Handle comma-separated queries (OR logic)
    char* saveptr1;
    char* query_part = strtok_r(query, ",", &saveptr1);

    while (query_part) {
        // Trim whitespace
        while (*query_part && (*query_part == ' ' || *query_part == '\t')) query_part++;
        char* end = query_part + strlen(query_part) - 1;
        while (end > query_part && (*end == ' ' || *end == '\t')) *end-- = '\0';

        if (strlen(query_part) == 0) {
            query_part = strtok_r(NULL, ",", &saveptr1);
            continue;
        }

        log_debug("[Media Query] Processing part: '%s'", query_part);

        // Check for 'not' prefix
        bool negated = false;
        if (strncmp(query_part, "not ", 4) == 0) {
            negated = true;
            query_part += 4;
            while (*query_part == ' ') query_part++;
        }

        // Check for 'only' prefix (ignore it, just for compatibility)
        if (strncmp(query_part, "only ", 5) == 0) {
            query_part += 5;
            while (*query_part == ' ') query_part++;
        }

        bool part_result = true;

        // Split by 'and' (AND logic within a query)
        // Make another copy for tokenizing by 'and'
        char* and_copy = (char*)pool_alloc(engine->pool, strlen(query_part) + 1);
        if (!and_copy) return false;
        str_copy(and_copy, strlen(query_part) + 1, query_part, strlen(query_part));

        // Replace " and " with null terminators to split
        char* condition = and_copy;
        while (*condition && part_result) {
            // Find next " and " or end
            char* and_pos = strstr(condition, " and ");
            if (and_pos) {
                *and_pos = '\0';  // Terminate current condition
            }

            // Trim the condition
            while (*condition && (*condition == ' ' || *condition == '\t')) condition++;
            char* cond_end = condition + strlen(condition) - 1;
            while (cond_end > condition && (*cond_end == ' ' || *cond_end == '\t')) *cond_end-- = '\0';

            if (strlen(condition) > 0) {
                // Check if this is a parenthesized feature
                if (condition[0] == '(') {
                    // Parse feature condition: (feature: value) or (feature)
                    char* paren_end = strchr(condition, ')');
                    if (paren_end) {
                        *paren_end = '\0';
                        char* feature_start = condition + 1;

                        // Find colon separator
                        char* colon = strchr(feature_start, ':');
                        if (colon) {
                            *colon = '\0';
                            char* feature_name = feature_start;
                            char* feature_value = colon + 1;

                            // Trim feature name and value
                            while (*feature_name == ' ') feature_name++;
                            char* name_end = feature_name + strlen(feature_name) - 1;
                            while (name_end > feature_name && *name_end == ' ') *name_end-- = '\0';

                            while (*feature_value == ' ') feature_value++;
                            char* val_end = feature_value + strlen(feature_value) - 1;
                            while (val_end > feature_value && *val_end == ' ') *val_end-- = '\0';

                            part_result = evaluate_media_feature(engine, feature_name, feature_value);
                        } else {
                            // Boolean feature like (color)
                            while (*feature_start == ' ') feature_start++;
                            char* feat_end = feature_start + strlen(feature_start) - 1;
                            while (feat_end > feature_start && *feat_end == ' ') *feat_end-- = '\0';

                            // For now, assume all boolean features are supported
                            part_result = true;
                        }
                    }
                } else {
                    // Media type
                    part_result = evaluate_media_type(condition);
                }
            }

            if (and_pos) {
                condition = and_pos + 5;  // Move past " and "
            } else {
                break;
            }
        }

        // Apply negation
        if (negated) {
            part_result = !part_result;
        }

        log_debug("[Media Query] Part result: %s", part_result ? "true" : "false");

        // If any comma-separated part matches, the whole query matches
        if (part_result) {
            log_debug("[Media Query] MATCHES: '%s'", media_query);
            return true;
        }

        query_part = strtok_r(NULL, ",", &saveptr1);
    }

    log_debug("[Media Query] DOES NOT MATCH: '%s'", media_query);
    return false;
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
