#ifndef CSS_SELECTOR_PARSER_H
#define CSS_SELECTOR_PARSER_H

#include "css_parser.h"
#include "css_style.h"
#include "css_style_node.h"
#include "../../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

// CSS4 Selector Types already defined in css_parser.h as CssSelectorType
// This file extends the parser with additional CSS4 features

// CSS4 combinator types already defined in css_parser.h

// nth-expression for :nth-child, etc.
typedef struct {
    int a;  // Coefficient (e.g., 2 in "2n+1")
    int b;  // Constant (e.g., 1 in "2n+1")
    bool odd;    // Special case for "odd"
    bool even;   // Special case for "even"
} CSSNthExpression;

// CSS Selector Component
typedef struct CSSSelectorComponent {
    CssSelectorType type;
    char* value;                    // Element name, class, id, attribute name
    char* attribute_value;          // For attribute selectors
    char* attribute_operator;       // =, ~=, |=, ^=, $=, *=
    bool case_insensitive;          // For attribute selectors with 'i' flag
    CSSNthExpression* nth_expr;     // For nth-child selectors
    char** function_args;           // For functional pseudo-classes
    int function_arg_count;
    struct CSSSelectorComponent* next; // Next component in compound selector
} CSSSelectorComponent;

// Complex Selector (sequence of components with combinators)
typedef struct CSSComplexSelector {
    CSSSelectorComponent* components;  // Compound selector components
    CssCombinator combinator;         // Combinator to next selector
    struct CSSComplexSelector* next;  // Next complex selector
} CSSComplexSelector;

// Selector List (comma-separated selectors)
typedef struct CSSSelectorList {
    CSSComplexSelector* selectors;    // List of complex selectors
    int selector_count;
    CssSpecificity max_specificity;   // Highest specificity in the list
    bool has_nesting;                 // Contains nesting selectors (&)
    bool has_scope;                   // Contains :scope selectors
} CSSSelectorList;

// Selector Parser Context
typedef struct CSSSelectorParser {
    CssToken* tokens;
    size_t token_count;
    size_t current_token;
    Pool* pool;
    bool allow_nesting;               // Allow & nesting selectors
    bool allow_scope;                 // Allow :scope selectors
    int nesting_depth;                // Current nesting depth
    char** error_messages;            // Parse error messages
    int error_count;
    bool strict_mode;                 // Strict CSS4 compliance
} CSSSelectorParser;

// Specificity calculation utilities
typedef struct CSSSpecificityDetail {
    int inline_style;      // a: inline style (always 0 for selectors)
    int ids;              // b: ID selectors
    int classes;          // c: class, attribute, pseudo-class selectors
    int elements;         // d: element, pseudo-element selectors
    bool important;       // !important flag (handled at declaration level)
    
    // CSS4 extensions
    bool is_forgiving;    // :is(), :where() forgiving parsing
    bool zero_specificity; // :where() has zero specificity
} CSSSpecificityDetail;

// Core parser functions
CSSSelectorParser* css_selector_parser_create(Pool* pool);
void css_selector_parser_destroy(CSSSelectorParser* parser);

CSSSelectorList* css_parse_selector_list(CSSSelectorParser* parser, 
                                        const char* selector_text);
CSSComplexSelector* css_parse_complex_selector(CSSSelectorParser* parser);
CSSSelectorComponent* css_parse_complex_compound_selector(CSSSelectorParser* parser);
CSSSelectorComponent* css_parse_complex_simple_selector(CSSSelectorParser* parser);

// Pseudo-class and pseudo-element parsing
CSSSelectorComponent* css_parse_pseudo_class(CSSSelectorParser* parser, const char* name);
CSSSelectorComponent* css_parse_pseudo_element(CSSSelectorParser* parser, const char* name);
CSSNthExpression* css_parse_nth_expression(CSSSelectorParser* parser, const char* expr);

// Attribute selector parsing
CSSSelectorComponent* css_parse_attribute_selector(CSSSelectorParser* parser);

// Functional pseudo-class parsing
CSSSelectorComponent* css_parse_not_selector(CSSSelectorParser* parser);
CSSSelectorComponent* css_parse_is_selector(CSSSelectorParser* parser);
CSSSelectorComponent* css_parse_where_selector(CSSSelectorParser* parser);
CSSSelectorComponent* css_parse_has_selector(CSSSelectorParser* parser);
CSSSelectorComponent* css_parse_lang_selector(CSSSelectorParser* parser);
CSSSelectorComponent* css_parse_dir_selector(CSSSelectorParser* parser);

// CSS Nesting support
CSSSelectorComponent* css_parse_nesting_selector(CSSSelectorParser* parser);
CSSSelectorList* css_resolve_nested_selectors(CSSSelectorList* nested, 
                                             CSSSelectorList* parent);

// Specificity calculation (CSS4 compliant)
CSSSpecificityDetail css_calculate_specificity_detailed(CSSComplexSelector* selector);
CssSpecificity css_calculate_complex_specificity(CSSComplexSelector* selector);
CssSpecificity css_selector_list_max_specificity(CSSSelectorList* list);

// Utility functions
bool css_selector_component_matches_element(CSSSelectorComponent* component, 
                                           const void* element);
bool css_complex_selector_matches_element(CSSComplexSelector* selector, 
                                         const void* element,
                                         const void* root);
bool css_selector_list_matches_element(CSSSelectorList* list, 
                                      const void* element,
                                      const void* root);

// Selector validation and normalization
bool css_validate_selector_syntax(const char* selector_text);
char* css_normalize_selector(const char* selector_text, Pool* pool);
bool css_is_valid_identifier(const char* identifier);

// CSS4 forgiving selector parsing
CSSSelectorList* css_parse_forgiving_selector_list(CSSSelectorParser* parser,
                                                   const char* selector_text);

// Selector serialization
char* css_serialize_selector_component(CSSSelectorComponent* component, Pool* pool);
char* css_serialize_complex_selector(CSSComplexSelector* selector, Pool* pool);
char* css_serialize_selector_list(CSSSelectorList* list, Pool* pool);

// Error handling
void css_selector_parser_add_error(CSSSelectorParser* parser, const char* message);
bool css_selector_parser_has_errors(CSSSelectorParser* parser);
void css_selector_parser_clear_errors(CSSSelectorParser* parser);

// CSS4 feature support queries
bool css_supports_nesting(void);
bool css_supports_scope(void);
bool css_supports_has(void);
bool css_supports_forgiving_selectors(void);

// Debug and introspection
void css_print_selector_specificity(CSSComplexSelector* selector);
void css_print_selector_structure(CSSSelectorList* list);
char* css_describe_selector_component(CSSSelectorComponent* component, Pool* pool);

// Specificity utility functions
CssSpecificity css_specificity_create(uint8_t ids, uint8_t classes, uint8_t elements, 
                                      uint8_t inline_style, bool important);

#ifdef __cplusplus
}
#endif

#endif // CSS_SELECTOR_PARSER_H
