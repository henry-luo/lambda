#ifndef CSS_SELECTOR_PARSER_H
#define CSS_SELECTOR_PARSER_H

#include "css_tokenizer_enhanced.h"
#include "../../lib/css_style_node.h"
#include "../../lib/mempool.h"

// Use existing CSS enums from css_property_system.h to avoid conflicts
#include "../../lib/css_property_system.h"

#ifdef __cplusplus
extern "C" {
#endif

// CSS4 Selector Types (comprehensive)
typedef enum {
    // Basic selectors
    CSS_SELECTOR_TYPE_ELEMENT,      // div, span, h1
    CSS_SELECTOR_TYPE_CLASS,        // .classname
    CSS_SELECTOR_TYPE_ID,           // #identifier
    CSS_SELECTOR_TYPE_UNIVERSAL,    // *
    
    // Attribute selectors
    CSS_SELECTOR_ATTR_EXACT,        // [attr="value"]
    CSS_SELECTOR_ATTR_CONTAINS,     // [attr~="value"]
    CSS_SELECTOR_ATTR_BEGINS,       // [attr^="value"]
    CSS_SELECTOR_ATTR_ENDS,         // [attr$="value"]
    CSS_SELECTOR_ATTR_SUBSTRING,    // [attr*="value"]
    CSS_SELECTOR_ATTR_LANG,         // [attr|="value"]
    CSS_SELECTOR_ATTR_EXISTS,       // [attr]
    CSS_SELECTOR_ATTR_CASE_INSENSITIVE, // [attr="value" i]
    CSS_SELECTOR_ATTR_CASE_SENSITIVE,   // [attr="value" s]
    
    // Pseudo-classes (structural)
    CSS_SELECTOR_PSEUDO_ROOT,           // :root
    CSS_SELECTOR_PSEUDO_EMPTY,          // :empty
    CSS_SELECTOR_PSEUDO_FIRST_CHILD,    // :first-child
    CSS_SELECTOR_PSEUDO_LAST_CHILD,     // :last-child
    CSS_SELECTOR_PSEUDO_ONLY_CHILD,     // :only-child
    CSS_SELECTOR_PSEUDO_FIRST_OF_TYPE,  // :first-of-type
    CSS_SELECTOR_PSEUDO_LAST_OF_TYPE,   // :last-of-type
    CSS_SELECTOR_PSEUDO_ONLY_OF_TYPE,   // :only-of-type
    CSS_SELECTOR_PSEUDO_NTH_CHILD,      // :nth-child(an+b)
    CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD, // :nth-last-child(an+b)
    CSS_SELECTOR_PSEUDO_NTH_OF_TYPE,    // :nth-of-type(an+b)
    CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE, // :nth-last-of-type(an+b)
    
    // Pseudo-classes (user interaction)
    CSS_SELECTOR_PSEUDO_HOVER,         // :hover
    CSS_SELECTOR_PSEUDO_ACTIVE,        // :active
    CSS_SELECTOR_PSEUDO_FOCUS,         // :focus
    CSS_SELECTOR_PSEUDO_FOCUS_VISIBLE, // :focus-visible
    CSS_SELECTOR_PSEUDO_FOCUS_WITHIN,  // :focus-within
    CSS_SELECTOR_PSEUDO_VISITED,       // :visited
    CSS_SELECTOR_PSEUDO_LINK,          // :link
    CSS_SELECTOR_PSEUDO_TARGET,        // :target
    CSS_SELECTOR_PSEUDO_TARGET_WITHIN, // :target-within
    
    // Pseudo-classes (input/form)
    CSS_SELECTOR_PSEUDO_ENABLED,       // :enabled
    CSS_SELECTOR_PSEUDO_DISABLED,      // :disabled
    CSS_SELECTOR_PSEUDO_CHECKED,       // :checked
    CSS_SELECTOR_PSEUDO_INDETERMINATE, // :indeterminate
    CSS_SELECTOR_PSEUDO_VALID,         // :valid
    CSS_SELECTOR_PSEUDO_INVALID,       // :invalid
    CSS_SELECTOR_PSEUDO_REQUIRED,      // :required
    CSS_SELECTOR_PSEUDO_OPTIONAL,      // :optional
    CSS_SELECTOR_PSEUDO_READ_ONLY,     // :read-only
    CSS_SELECTOR_PSEUDO_READ_WRITE,    // :read-write
    CSS_SELECTOR_PSEUDO_PLACEHOLDER_SHOWN, // :placeholder-shown
    CSS_SELECTOR_PSEUDO_DEFAULT,       // :default
    CSS_SELECTOR_PSEUDO_IN_RANGE,      // :in-range
    CSS_SELECTOR_PSEUDO_OUT_OF_RANGE,  // :out-of-range
    
    // Pseudo-classes (functional)
    CSS_SELECTOR_PSEUDO_NOT,           // :not(selector)
    CSS_SELECTOR_PSEUDO_IS,            // :is(selector-list)
    CSS_SELECTOR_PSEUDO_WHERE,         // :where(selector-list)
    CSS_SELECTOR_PSEUDO_HAS,           // :has(relative-selector)
    CSS_SELECTOR_PSEUDO_DIR,           // :dir(ltr|rtl)
    CSS_SELECTOR_PSEUDO_LANG,          // :lang(language-code)
    
    // Pseudo-classes (CSS4 new)
    CSS_SELECTOR_PSEUDO_ANY_LINK,      // :any-link
    CSS_SELECTOR_PSEUDO_LOCAL_LINK,    // :local-link
    CSS_SELECTOR_PSEUDO_SCOPE,         // :scope
    CSS_SELECTOR_PSEUDO_CURRENT,       // :current
    CSS_SELECTOR_PSEUDO_PAST,          // :past
    CSS_SELECTOR_PSEUDO_FUTURE,        // :future
    CSS_SELECTOR_PSEUDO_PLAYING,       // :playing
    CSS_SELECTOR_PSEUDO_PAUSED,        // :paused
    CSS_SELECTOR_PSEUDO_SEEKING,       // :seeking
    CSS_SELECTOR_PSEUDO_BUFFERING,     // :buffering
    CSS_SELECTOR_PSEUDO_STALLED,       // :stalled
    CSS_SELECTOR_PSEUDO_MUTED,         // :muted
    CSS_SELECTOR_PSEUDO_VOLUME_LOCKED, // :volume-locked
    CSS_SELECTOR_PSEUDO_FULLSCREEN,    // :fullscreen
    CSS_SELECTOR_PSEUDO_PICTURE_IN_PICTURE, // :picture-in-picture
    CSS_SELECTOR_PSEUDO_USER_INVALID,  // :user-invalid
    CSS_SELECTOR_PSEUDO_USER_VALID,    // :user-valid
    
    // Pseudo-elements
    CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE,    // ::before
    CSS_SELECTOR_PSEUDO_ELEMENT_AFTER,     // ::after
    CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LINE, // ::first-line
    CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LETTER, // ::first-letter
    CSS_SELECTOR_PSEUDO_ELEMENT_SELECTION, // ::selection
    CSS_SELECTOR_PSEUDO_ELEMENT_BACKDROP,  // ::backdrop
    CSS_SELECTOR_PSEUDO_ELEMENT_PLACEHOLDER, // ::placeholder
    CSS_SELECTOR_PSEUDO_ELEMENT_MARKER,    // ::marker
    CSS_SELECTOR_PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON, // ::file-selector-button
    CSS_SELECTOR_PSEUDO_ELEMENT_TARGET_TEXT, // ::target-text
    CSS_SELECTOR_PSEUDO_ELEMENT_HIGHLIGHT, // ::highlight
    CSS_SELECTOR_PSEUDO_ELEMENT_SPELLING_ERROR, // ::spelling-error
    CSS_SELECTOR_PSEUDO_ELEMENT_GRAMMAR_ERROR,  // ::grammar-error
    CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION, // ::view-transition
    CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_GROUP, // ::view-transition-group
    CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_IMAGE_PAIR, // ::view-transition-image-pair
    CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_OLD, // ::view-transition-old
    CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_NEW, // ::view-transition-new
    
    // CSS Nesting
    CSS_SELECTOR_NESTING_PARENT,       // &
    CSS_SELECTOR_NESTING_DESCENDANT,   // & .child
    CSS_SELECTOR_NESTING_PSEUDO        // &:hover
} CSSSelectorType;

// CSS4 Combinator Types
typedef enum {
    CSS_COMBINATOR_DESCENDANT,     // space - descendant
    CSS_COMBINATOR_CHILD,          // > - direct child
    CSS_COMBINATOR_NEXT_SIBLING,   // + - adjacent sibling
    CSS_COMBINATOR_SIBLING,        // ~ - general sibling
    CSS_COMBINATOR_COLUMN,         // || - column combinator (CSS4)
    CSS_COMBINATOR_NONE           // No combinator (compound selector)
} CSSCombinator;

// nth-expression for :nth-child, etc.
typedef struct {
    int a;  // Coefficient (e.g., 2 in "2n+1")
    int b;  // Constant (e.g., 1 in "2n+1")
    bool odd;    // Special case for "odd"
    bool even;   // Special case for "even"
} CSSNthExpression;

// CSS Selector Component
typedef struct CSSSelectorComponent {
    CSSSelectorType type;
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
    CSSCombinator combinator;         // Combinator to next selector
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
    CSSTokenEnhanced* tokens;
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
CSSSelectorComponent* css_parse_compound_selector(CSSSelectorParser* parser);
CSSSelectorComponent* css_parse_simple_selector(CSSSelectorParser* parser);

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
CssSpecificity css_calculate_specificity(CSSComplexSelector* selector);
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
