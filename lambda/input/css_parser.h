#ifndef CSS_PARSER_H
#define CSS_PARSER_H

#include "css_tokenizer.h"
#include "css_properties.h"
#include "../../lib/mem-pool/include/mem_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct css_parser css_parser_t;
typedef struct css_rule css_rule_t;
typedef struct css_selector css_selector_t;
typedef struct css_at_rule css_at_rule_t;

// CSS Parser Error Types
typedef enum {
    CSS_ERROR_NONE,
    CSS_ERROR_UNEXPECTED_TOKEN,
    CSS_ERROR_INVALID_SELECTOR,
    CSS_ERROR_INVALID_PROPERTY,
    CSS_ERROR_INVALID_VALUE,
    CSS_ERROR_MISSING_SEMICOLON,
    CSS_ERROR_MISSING_BRACE,
    CSS_ERROR_UNTERMINATED_BLOCK,
    CSS_ERROR_INVALID_AT_RULE,
    CSS_ERROR_MEMORY_ERROR
} css_error_type_t;

// CSS Parser Error
typedef struct css_error {
    css_error_type_t type;
    const char* message;
    int line;
    int column;
    const char* context;
} css_error_t;

// CSS Selector Types
typedef enum {
    CSS_SELECTOR_TYPE,          // element
    CSS_SELECTOR_CLASS,         // .class
    CSS_SELECTOR_ID,            // #id
    CSS_SELECTOR_ATTRIBUTE,     // [attr]
    CSS_SELECTOR_PSEUDO_CLASS,  // :hover
    CSS_SELECTOR_PSEUDO_ELEMENT,// ::before
    CSS_SELECTOR_UNIVERSAL,     // *
    CSS_SELECTOR_DESCENDANT,    // space
    CSS_SELECTOR_CHILD,         // >
    CSS_SELECTOR_SIBLING,       // ~
    CSS_SELECTOR_ADJACENT       // +
} css_selector_type_t;

// CSS Selector Component
typedef struct css_selector_component {
    css_selector_type_t type;
    const char* name;           // Element name, class name, id, etc.
    const char* value;          // For attribute selectors
    const char* attr_operator;  // For attribute selectors (=, ~=, |=, etc.)
    struct css_selector_component* next;
} css_selector_component_t;

// CSS Selector (can be compound or complex)
struct css_selector {
    css_selector_component_t* components;
    int specificity;            // Calculated specificity
    struct css_selector* next;  // For selector groups
};

// CSS Rule Types
typedef enum {
    CSS_RULE_STYLE,
    CSS_RULE_AT_RULE,
    CSS_RULE_COMMENT
} css_rule_type_t;

// CSS Style Rule
typedef struct css_style_rule {
    css_selector_t* selectors;
    css_declaration_t** declarations;
    int declaration_count;
    int declaration_capacity;
} css_style_rule_t;

// CSS At-Rule Types
typedef enum {
    CSS_AT_RULE_MEDIA,
    CSS_AT_RULE_KEYFRAMES,
    CSS_AT_RULE_FONT_FACE,
    CSS_AT_RULE_IMPORT,
    CSS_AT_RULE_CHARSET,
    CSS_AT_RULE_NAMESPACE,
    CSS_AT_RULE_SUPPORTS,
    CSS_AT_RULE_PAGE,
    CSS_AT_RULE_LAYER,
    CSS_AT_RULE_CONTAINER,
    CSS_AT_RULE_UNKNOWN
} css_at_rule_type_t;

// CSS At-Rule
struct css_at_rule {
    css_at_rule_type_t type;
    const char* name;           // @media, @keyframes, etc.
    const char* prelude;        // Rule conditions/parameters
    css_rule_t** rules;         // Nested rules (for block at-rules)
    int rule_count;
    int rule_capacity;
    css_declaration_t** declarations; // For non-block at-rules
    int declaration_count;
    int declaration_capacity;
};

// CSS Rule (union of different rule types)
struct css_rule {
    css_rule_type_t type;
    union {
        css_style_rule_t* style_rule;
        css_at_rule_t* at_rule;
        const char* comment;
    } data;
    struct css_rule* next;
};

// CSS Stylesheet
typedef struct css_stylesheet {
    css_rule_t* rules;
    int rule_count;
    css_error_t* errors;
    int error_count;
    int error_capacity;
} css_stylesheet_t;

// CSS Parser State
typedef struct css_parser {
    CSSTokenStream* tokens;
    css_property_db_t* property_db;
    VariableMemPool* pool;
    css_error_t* errors;
    int error_count;
    int error_capacity;
    bool strict_mode;           // Whether to fail on errors or continue
    bool preserve_comments;     // Whether to preserve comments in AST
} css_parser_t;

// Parser creation and destruction
css_parser_t* css_parser_create(VariableMemPool* pool);
void css_parser_destroy(css_parser_t* parser);

// Main parsing functions
css_stylesheet_t* css_parse_stylesheet(css_parser_t* parser, const char* css_text);
css_rule_t* css_parse_rule(css_parser_t* parser);
css_style_rule_t* css_parse_style_rule(css_parser_t* parser);
css_at_rule_t* css_parse_at_rule(css_parser_t* parser);

// Selector parsing
css_selector_t* css_parse_selector_list(css_parser_t* parser);
css_selector_t* css_parse_selector(css_parser_t* parser);
css_selector_component_t* css_parse_selector_component(css_parser_t* parser);

// Declaration parsing
css_declaration_t* css_parse_declaration(css_parser_t* parser);
css_token_t* css_parse_declaration_value(css_parser_t* parser, const char* property, int* token_count);
css_token_t* css_parse_function(css_parser_t* parser, const char* function_name, int* token_count);

// At-rule specific parsing
css_at_rule_t* css_parse_media_rule(css_parser_t* parser);
css_at_rule_t* css_parse_keyframes_rule(css_parser_t* parser);
css_at_rule_t* css_parse_font_face_rule(css_parser_t* parser);
css_at_rule_t* css_parse_import_rule(css_parser_t* parser);
css_at_rule_t* css_parse_supports_rule(css_parser_t* parser);

// Parser utilities
css_token_t* css_parser_current_token(css_parser_t* parser);
css_token_t* css_parser_peek_token(css_parser_t* parser, int offset);
void css_parser_advance(css_parser_t* parser);
bool css_parser_expect_token(css_parser_t* parser, CSSTokenType type);
bool css_parser_consume_token(css_parser_t* parser, CSSTokenType type);

// Error handling
void css_parser_add_error(css_parser_t* parser, css_error_type_t type, 
                         const char* message, css_token_t* token);
bool css_parser_has_errors(css_parser_t* parser);
void css_parser_clear_errors(css_parser_t* parser);

// Parser configuration
void css_parser_set_strict_mode(css_parser_t* parser, bool strict);
void css_parser_set_preserve_comments(css_parser_t* parser, bool preserve);

// AST manipulation functions
css_rule_t* css_rule_create_style(css_parser_t* parser, css_selector_t* selectors);
css_rule_t* css_rule_create_at_rule(css_parser_t* parser, css_at_rule_t* at_rule);
css_rule_t* css_rule_create_comment(css_parser_t* parser, const char* comment);

void css_style_rule_add_declaration(css_style_rule_t* rule, css_declaration_t* decl, 
                                   VariableMemPool* pool);
void css_at_rule_add_rule(css_at_rule_t* at_rule, css_rule_t* rule, VariableMemPool* pool);
void css_at_rule_add_declaration(css_at_rule_t* at_rule, css_declaration_t* decl, 
                                VariableMemPool* pool);

// Selector specificity calculation
int css_selector_calculate_specificity(const css_selector_t* selector);
int css_selector_component_specificity(const css_selector_component_t* component);

// Selector matching (for future use)
bool css_selector_matches_element(const css_selector_t* selector, const char* element_name,
                                 const char* id, const char** classes, int class_count);

// AST traversal callbacks
typedef void (*css_rule_visitor_t)(css_rule_t* rule, void* user_data);
typedef void (*css_declaration_visitor_t)(css_declaration_t* decl, void* user_data);

void css_stylesheet_visit_rules(css_stylesheet_t* stylesheet, css_rule_visitor_t visitor, 
                               void* user_data);
void css_rule_visit_declarations(css_rule_t* rule, css_declaration_visitor_t visitor, 
                                void* user_data);

// Legacy API compatibility - commented out for now
// Item css_parse_stylesheet_legacy(Input* input, const char** css);
// String* css_format_stylesheet_legacy(Item item, VariableMemPool* pool);

// Validation and optimization
bool css_stylesheet_validate(css_stylesheet_t* stylesheet, css_property_db_t* property_db);
void css_stylesheet_optimize(css_stylesheet_t* stylesheet);

// Debug and introspection
void css_parser_print_ast(css_stylesheet_t* stylesheet);
void css_parser_print_errors(css_parser_t* parser);
const char* css_error_type_to_string(css_error_type_t type);
const char* css_selector_type_to_string(css_selector_type_t type);
const char* css_at_rule_type_to_string(css_at_rule_type_t type);

#ifdef __cplusplus
}
#endif

#endif // CSS_PARSER_H
