#ifndef CSS_ENGINE_H
#define CSS_ENGINE_H

#include "css_parser.hpp"
#include "css_style.hpp"
#include "../../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct CssPropertyValueParser;

// CSS Style Engine structure
typedef struct CssStyleEngine {
    Pool* pool;                   // Memory pool
    int version;                  // Engine version
    // Additional members can be added as needed
} CssStyleEngine;

/**
 * CSS Integration System
 *
 * This file integrates all CSS components including tokenizer, parser,
 * style system, and provides a unified CSS processing engine.
 */

// CSS Processing Engine
typedef struct CssEngine {
    Pool* pool;

    // Core components
    CssTokenizer* tokenizer;
    // Removed: selector_parser (legacy linked-list parser removed)
    struct CssPropertyValueParser* value_parser;
    struct CssStyleEngine* style_engine;

    // Style storage and cascade
    AvlTree* style_tree;        // AVL tree for style node storage
    CssComputedStyle* root_style; // Root element style

    // Feature flags
    struct {
        bool css_nesting;
        bool css_cascade_layers;
        bool css_container_queries;
        bool css_scope;
        bool css_custom_selectors;
        bool css_mixins;
        bool css_color_4;
        bool css_logical_properties;
        bool css_subgrid;
        bool css_anchor_positioning;
    } features;

    // Performance settings
    struct {
        bool cache_parsed_selectors;
        bool cache_computed_values;
        bool optimize_specificity;
        bool parallel_parsing;
        size_t max_cache_size;
    } performance;

    // Context settings
    struct {
        const char* base_url;
        const char* document_charset;
        const char* color_scheme;
        double viewport_width;
        double viewport_height;
        double device_pixel_ratio;
        double root_font_size;
        bool reduced_motion;
        bool high_contrast;
    } context;

    // Engine statistics
    struct {
        size_t stylesheets_parsed;
        size_t rules_parsed;          // Number of rules parsed
        size_t rules_processed;
        size_t selectors_matched;
        size_t selectors_cached;      // Number of selectors cached
        size_t properties_computed;
        size_t values_computed;       // Number of values computed
        size_t cache_hits;
        size_t cache_misses;
        double parse_time;            // Total time spent parsing
        double cascade_time;          // Time spent in cascade calculations
        size_t cascade_calculations;  // Number of cascade operations
        size_t memory_usage;          // Memory usage in bytes
    } stats;

    // Processing options
    bool supports_css3;
    bool supports_unicode;
    bool strict_parsing;

    // Performance tracking
    size_t rules_processed;
    size_t selectors_processed;
    size_t properties_processed;

    // Error handling
    size_t parse_errors;
    size_t validation_errors;
} CssEngine;
// CSS Processing Options
typedef struct CssProcessingOptions {
    // CSS feature support
    bool css_nesting;           // CSS Nesting support
    bool css_cascade_layers;    // CSS Cascade Layers
    bool css_container_queries; // Container Queries
    bool css_scope;             // @scope support
    bool css_custom_selectors;  // Custom selectors
    bool css_color_4;           // CSS Color Level 4
    bool css_logical_properties; // Logical properties
    bool css_subgrid;           // CSS Subgrid
    bool css_anchor_positioning; // CSS Anchor Positioning

    // Performance options
    bool cache_parsed_selectors;
    bool cache_computed_values;
    bool optimize_specificity;
    bool parallel_parsing;
    int max_cache_size;

    // Document context
    const char* base_url;
    const char* document_charset;
    const char* color_scheme;   // light, dark, or auto
    double viewport_width;
    double viewport_height;
    double device_pixel_ratio;
    double root_font_size;
    bool reduced_motion;
    bool high_contrast;
} CssProcessingOptions;

// ============================================================================
// CSS Integration Functions
// ============================================================================

// Engine management
CssEngine* css_engine_create(Pool* pool);
void css_engine_destroy(CssEngine* engine);

// Style engine management
struct CssStyleEngine* css_style_engine_create(Pool* pool);
void css_style_engine_destroy(struct CssStyleEngine* engine);

// Configuration
void css_engine_set_options(CssEngine* engine, const CssProcessingOptions* options);
void css_engine_set_viewport(CssEngine* engine, double width, double height);
void css_engine_set_color_scheme(CssEngine* engine, const char* scheme);
void css_engine_set_root_font_size(CssEngine* engine, double size);

// CSS parsing
CssStylesheet* css_parse_stylesheet(CssEngine* engine, const char* css_text, const char* base_url);
CssRule* css_parse_rule(CssEngine* engine, const char* rule_text);

// Style system integration
bool css_rule_to_style_nodes(CssEngine* engine, CssRule* rule, CssComputedStyle* target_style);
bool css_apply_stylesheet(CssEngine* engine, CssStylesheet* stylesheet, CssComputedStyle* target_style);

// Value computation and resolution
CssValue* css_compute_value(CssEngine* engine, CssValue* declared_value,
                           CssPropertyId property_id, const CssComputedStyle* parent_style);
double css_resolve_length(CssEngine* engine, const CssValue* value,
                         double container_size, double font_size);

// Custom property management
bool css_register_custom_property(CssEngine* engine, const char* name,
                                 const char* syntax, CssValue* initial_value, bool inherits);
CssValue* css_get_custom_property(CssEngine* engine, const CssComputedStyle* style, const char* name);
bool css_set_custom_property(CssEngine* engine, CssComputedStyle* style,
                            const char* name, CssValue* value);

// Media query evaluation
bool css_evaluate_media_query(CssEngine* engine, const char* media_query);

// Statistics and debugging
typedef struct CssEngineStats {
    size_t rules_processed;
    size_t selectors_processed;
    size_t properties_processed;
    size_t parse_errors;
    size_t validation_errors;
    double parse_time;
    double cascade_time;
    size_t memory_usage;
} CssEngineStats;

CssEngineStats css_engine_get_stats(const CssEngine* engine);
void css_engine_reset_stats(CssEngine* engine);

// Compatibility functions for legacy code
typedef CssEngine CSSEngine;
typedef CssRule CSSRule;
typedef CssStylesheet CSSStylesheet;

// Cascade integration features
int css_calculate_cascade_priority(CssEngine* engine,
                                  CssRule* rule,
                                  CssStyleNode* element);

bool css_apply_cascade(CssEngine* engine,
                      CssStyleNode* element,
                      CssStylesheet** stylesheets,
                      int stylesheet_count);

// CSS Nesting support
CssSelector* css_resolve_nesting(CssEngine* engine,
                                 CssSelector* nested_selector,
                                 CssSelector* parent_selector);

// Container Queries
typedef struct CSSContainerQuery {
    const char* container_name;
    const char* query_condition;
    CssValue* size_condition;
    bool matches_current_context;
} CSSContainerQuery;



bool css_pseudo_class_matches(CssEngine* engine,
                             CssSelectorType pseudo_type,
                             CssStyleNode* element);

// Style application
void css_apply_styles_to_element(CssEngine* engine,
                                CssStyleNode* element,
                                CssStylesheet** stylesheets,
                                int stylesheet_count);

// CSS-in-JS integration
typedef struct CssInJSRule {
    const char* selector_template;
    const char** property_templates;
    CssValue** dynamic_values;
    int property_count;
} CssInJSRule;

CssRule* css_compile_css_in_js(CssEngine* engine,
                               CssInJSRule* template_rule,
                               void* dynamic_context);

// Error handling and diagnostics
typedef enum {
    CSS_ERROR_NONE,
    CSS_ERROR_PARSE_FAILED,
    CSS_ERROR_SELECTOR_INVALID,
    CSS_ERROR_VALUE_INVALID,
    CSS_ERROR_FEATURE_UNSUPPORTED,
    CSS_ERROR_MEMORY_ERROR,
    CSS_ERROR_CIRCULAR_DEPENDENCY
} CssErrorCode;

typedef struct CssError {
    CssErrorCode type;
    const char* message;
    const char* source_location;
    int line_number;
    int column_number;
    const char* suggestion;
} CssError;

void css_engine_add_error(CssEngine* engine, CssError* error);
CssError** css_engine_get_errors(CssEngine* engine, int* count);
void css_engine_clear_errors(CssEngine* engine);

// Statistics and monitoring

// Enhanced CSS functions (for backward compatibility)
void css_enhanced_detect_features_in_rule(CssStylesheet* stylesheet, CssRule* rule);
bool css_enhanced_nesting_parent_matches(const CssSelector* selector, const CssStyleNode* node);
bool css_enhanced_pseudo_class_matches(const CssSelector* selector, const CssStyleNode* node);

#ifdef __cplusplus
}
#endif

#endif // CSS_ENGINE_H
