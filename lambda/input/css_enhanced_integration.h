#ifndef CSS_ENHANCED_INTEGRATION_H
#define CSS_ENHANCED_INTEGRATION_H

#include "css_tokenizer_enhanced.h"
#include "css_selector_parser.h"
#include "css_property_value_parser.h"
#include "../vibe/css_style_node.h"
#include "../vibe/avl_tree.h"
#include "../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

// Enhanced CSS Engine - Integrates all CSS3+ components
typedef struct CSSEnhancedEngine {
    Pool* pool;
    
    // Core enhanced components
    CSSTokenizerEnhanced* tokenizer;
    CSSSelectorParser* selector_parser;
    CSSPropertyValueParser* value_parser;
    
    // Style storage and cascade
    AvlTree* style_tree;        // AVL tree for style node storage
    CssStyleEngine* style_engine; // CSS cascade engine from Phase 1
    
    // Enhanced features configuration
    struct {
        bool css_nesting;           // CSS Nesting support
        bool css_cascade_layers;    // CSS Cascade Layers
        bool css_container_queries; // Container Queries
        bool css_scope;             // @scope support
        bool css_custom_selectors;  // Custom selectors
        bool css_mixins;            // CSS mixins (proposal)
        bool css_color_4;           // CSS Color Level 4
        bool css_logical_properties; // Logical properties
        bool css_subgrid;           // CSS Subgrid
        bool css_anchor_positioning; // CSS Anchor Positioning
    } features;
    
    // Performance options
    struct {
        bool cache_parsed_selectors;
        bool cache_computed_values;
        bool optimize_specificity;
        bool parallel_parsing;
        int max_cache_size;
    } performance;
    
    // Document context
    struct {
        const char* base_url;
        const char* document_charset;
        const char* color_scheme;   // light, dark, or auto
        double viewport_width;
        double viewport_height;
        double device_pixel_ratio;
        double root_font_size;
        bool reduced_motion;
        bool high_contrast;
    } context;
    
    // Statistics and monitoring
    struct {
        int rules_parsed;
        int selectors_cached;
        int values_computed;
        int cascade_calculations;
        double parse_time;
        double cascade_time;
        size_t memory_usage;
    } stats;
    
} CSSEnhancedEngine;

// Enhanced CSS Rule representation
typedef struct CSSEnhancedRule {
    CSSComplexSelector* selector_list;     // Parsed CSS4 selectors
    CSSValueEnhanced** property_values;    // Enhanced property values
    const char** property_names;
    int property_count;
    int property_capacity;
    
    // Rule metadata
    bool important;
    int cascade_layer;          // Cascade layer index
    const char* scope_root;     // @scope root selector
    const char* scope_limit;    // @scope limit selector
    
    // Source information
    const char* source_url;
    int source_line;
    int source_column;
    
    // Computed data
    CssSpecificity cached_specificity;
    bool specificity_computed;
    
    // Integration with style nodes
    CssStyleNode* style_node;   // Link to style node
    
} CSSEnhancedRule;

// Enhanced CSS Stylesheet
typedef struct CSSEnhancedStylesheet {
    CSSEnhancedRule** rules;
    int rule_count;
    int rule_capacity;
    
    // Stylesheet metadata
    const char* origin_url;
    int cascade_origin;         // Author, user, or user-agent
    int layer_index;           // Cascade layer
    
    // Features used in stylesheet
    bool uses_nesting;
    bool uses_custom_properties;
    bool uses_container_queries;
    bool uses_scope;
    bool uses_layers;
    
    // Performance data
    double parse_time;
    size_t memory_footprint;
    
} CSSEnhancedStylesheet;

// Integration functions

// Engine management
CSSEnhancedEngine* css_enhanced_engine_create(Pool* pool);
void css_enhanced_engine_destroy(CSSEnhancedEngine* engine);

// Configuration
void css_enhanced_engine_enable_feature(CSSEnhancedEngine* engine, const char* feature_name, bool enabled);
void css_enhanced_engine_set_viewport(CSSEnhancedEngine* engine, double width, double height);
void css_enhanced_engine_set_color_scheme(CSSEnhancedEngine* engine, const char* scheme);
void css_enhanced_engine_set_root_font_size(CSSEnhancedEngine* engine, double size);

// CSS parsing with enhanced features
CSSEnhancedStylesheet* css_enhanced_parse_stylesheet(CSSEnhancedEngine* engine, 
                                                     const char* css_text, 
                                                     const char* base_url);

CSSEnhancedRule* css_enhanced_parse_rule(CSSEnhancedEngine* engine, const char* rule_text);

// Style node integration
bool css_enhanced_rule_to_style_node(CSSEnhancedEngine* engine, 
                                     CSSEnhancedRule* rule, 
                                     CssStyleNode** style_nodes, 
                                     int* node_count);

CssStyleNode* css_enhanced_selector_to_style_node(CSSEnhancedEngine* engine, 
                                                  CSSComplexSelector* selector);

// Value computation and resolution
CSSValueEnhanced* css_enhanced_compute_value(CSSEnhancedEngine* engine,
                                            CSSValueEnhanced* declared_value,
                                            const char* property_name,
                                            CssStyleNode* element_node);

double css_enhanced_resolve_length(CSSEnhancedEngine* engine,
                                  CSSValueEnhanced* value,
                                  double container_size,
                                  double font_size);

// Custom property management
bool css_enhanced_register_custom_property(CSSEnhancedEngine* engine,
                                          const char* name,
                                          const char* syntax,
                                          CSSValueEnhanced* initial_value,
                                          bool inherits);

CSSValueEnhanced* css_enhanced_get_custom_property(CSSEnhancedEngine* engine,
                                                   CssStyleNode* element,
                                                   const char* property_name);

// Cascade integration with enhanced features
int css_enhanced_calculate_cascade_priority(CSSEnhancedEngine* engine,
                                           CSSEnhancedRule* rule,
                                           CssStyleNode* element);

bool css_enhanced_apply_cascade(CSSEnhancedEngine* engine,
                               CssStyleNode* element,
                               CSSEnhancedStylesheet** stylesheets,
                               int stylesheet_count);

// CSS Nesting support
CSSComplexSelector* css_enhanced_resolve_nesting(CSSEnhancedEngine* engine,
                                                 CSSComplexSelector* nested_selector,
                                                 CSSComplexSelector* parent_selector);

// Container Queries
typedef struct CSSContainerQuery {
    const char* container_name;
    const char* query_condition;
    CSSValueEnhanced* size_condition;
    bool matches_current_context;
} CSSContainerQuery;

bool css_enhanced_evaluate_container_query(CSSEnhancedEngine* engine,
                                          CSSContainerQuery* query,
                                          CssStyleNode* container_element);

// CSS Scope
typedef struct CSSScopeRule {
    CSSComplexSelector* scope_root;
    CSSComplexSelector* scope_limit;
    CSSEnhancedRule** scoped_rules;
    int rule_count;
} CSSScopeRule;

bool css_enhanced_element_in_scope(CSSEnhancedEngine* engine,
                                  CSSScopeRule* scope,
                                  CssStyleNode* element);

// Cascade Layers
typedef struct CSSCascadeLayer {
    const char* name;
    int priority;
    CSSEnhancedRule** rules;
    int rule_count;
    CSSCascadeLayer** sublayers;
    int sublayer_count;
} CSSCascadeLayer;

void css_enhanced_register_cascade_layer(CSSEnhancedEngine* engine,
                                        const char* layer_name,
                                        int priority);

int css_enhanced_get_layer_priority(CSSEnhancedEngine* engine,
                                   const char* layer_name);

// Performance optimizations
void css_enhanced_cache_selector_specificity(CSSEnhancedEngine* engine,
                                            CSSComplexSelector* selector,
                                            CssSpecificity specificity);

CssSpecificity css_enhanced_get_cached_specificity(CSSEnhancedEngine* engine,
                                                  CSSComplexSelector* selector);

void css_enhanced_optimize_stylesheet(CSSEnhancedEngine* engine,
                                     CSSEnhancedStylesheet* stylesheet);

// Element matching with enhanced selectors
bool css_enhanced_selector_matches_element(CSSEnhancedEngine* engine,
                                          CSSComplexSelector* selector,
                                          CssStyleNode* element,
                                          CssStyleNode* scope_root);

bool css_enhanced_pseudo_class_matches(CSSEnhancedEngine* engine,
                                      CSSSelectorType pseudo_type,
                                      CssStyleNode* element);

// Style application
void css_enhanced_apply_styles_to_element(CSSEnhancedEngine* engine,
                                         CssStyleNode* element,
                                         CSSEnhancedStylesheet** stylesheets,
                                         int stylesheet_count);

// CSS-in-JS integration
typedef struct CSSInJSRule {
    const char* selector_template;
    const char** property_templates;
    CSSValueEnhanced** dynamic_values;
    int property_count;
} CSSInJSRule;

CSSEnhancedRule* css_enhanced_compile_css_in_js(CSSEnhancedEngine* engine,
                                               CSSInJSRule* template_rule,
                                               void* dynamic_context);

// Error handling and diagnostics
typedef enum {
    CSS_ENHANCED_ERROR_NONE,
    CSS_ENHANCED_ERROR_PARSE_FAILED,
    CSS_ENHANCED_ERROR_SELECTOR_INVALID,
    CSS_ENHANCED_ERROR_VALUE_INVALID,
    CSS_ENHANCED_ERROR_FEATURE_UNSUPPORTED,
    CSS_ENHANCED_ERROR_MEMORY_ERROR,
    CSS_ENHANCED_ERROR_CIRCULAR_DEPENDENCY
} CSSEnhancedErrorType;

typedef struct CSSEnhancedError {
    CSSEnhancedErrorType type;
    const char* message;
    const char* source_location;
    int line_number;
    int column_number;
    const char* suggestion;
} CSSEnhancedError;

void css_enhanced_engine_add_error(CSSEnhancedEngine* engine, CSSEnhancedError* error);
CSSEnhancedError** css_enhanced_engine_get_errors(CSSEnhancedEngine* engine, int* count);
void css_enhanced_engine_clear_errors(CSSEnhancedEngine* engine);

// Statistics and monitoring
void css_enhanced_engine_update_stats(CSSEnhancedEngine* engine);
void css_enhanced_engine_print_stats(CSSEnhancedEngine* engine);
double css_enhanced_engine_get_parse_time(CSSEnhancedEngine* engine);
size_t css_enhanced_engine_get_memory_usage(CSSEnhancedEngine* engine);

// Serialization for debugging
char* css_enhanced_rule_to_string(CSSEnhancedRule* rule, Pool* pool);
char* css_enhanced_stylesheet_to_string(CSSEnhancedStylesheet* stylesheet, Pool* pool);
void css_enhanced_print_cascade_debug(CSSEnhancedEngine* engine, CssStyleNode* element);

// Future extensibility
typedef struct CSSEnhancedPlugin {
    const char* name;
    const char* version;
    bool (*initialize)(CSSEnhancedEngine* engine);
    void (*cleanup)(CSSEnhancedEngine* engine);
    bool (*parse_extension)(const char* syntax, void** parsed_data);
    bool (*apply_extension)(CssStyleNode* element, void* parsed_data);
} CSSEnhancedPlugin;

bool css_enhanced_engine_register_plugin(CSSEnhancedEngine* engine, CSSEnhancedPlugin* plugin);
void css_enhanced_engine_unregister_plugin(CSSEnhancedEngine* engine, const char* plugin_name);

#ifdef __cplusplus
}
#endif

#endif // CSS_ENHANCED_INTEGRATION_H