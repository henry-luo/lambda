#ifndef CSS_PARSER_H
#define CSS_PARSER_H

#include "css_style.h"
#include "../../../lib/mempool.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CSS Parser System
 *
 * This file contains types, structures, and functions that are only needed
 * during CSS parsing. These include tokenizer types, selector parsing,
 * calc() expression parsing, and other intermediate parsing structures.
 */

// ============================================================================
// CSS Token Types (for parsing)
// ============================================================================

typedef enum CssTokenType {
    CSS_TOKEN_IDENT,           // identifiers, keywords
    CSS_TOKEN_FUNCTION,        // function names followed by (
    CSS_TOKEN_AT_KEYWORD,      // @media, @keyframes, etc.
    CSS_TOKEN_HASH,            // #colors
    CSS_TOKEN_STRING,          // "quoted strings"
    CSS_TOKEN_URL,             // url() values
    CSS_TOKEN_NUMBER,          // numeric values
    CSS_TOKEN_DIMENSION,       // numbers with units (10px, 2em)
    CSS_TOKEN_PERCENTAGE,      // percentage values (50%)
    CSS_TOKEN_UNICODE_RANGE,   // U+0000-FFFF
    CSS_TOKEN_INCLUDE_MATCH,   // ~=
    CSS_TOKEN_DASH_MATCH,      // |=
    CSS_TOKEN_PREFIX_MATCH,    // ^=
    CSS_TOKEN_SUFFIX_MATCH,    // $=
    CSS_TOKEN_SUBSTRING_MATCH, // *=
    CSS_TOKEN_COLUMN,          // ||
    CSS_TOKEN_WHITESPACE,      // spaces, tabs, newlines
    CSS_TOKEN_COMMENT,         // /* comments */
    CSS_TOKEN_COLON,           // :
    CSS_TOKEN_SEMICOLON,       // ;
    CSS_TOKEN_LEFT_PAREN,      // (
    CSS_TOKEN_RIGHT_PAREN,     // )
    CSS_TOKEN_LEFT_BRACE,      // {
    CSS_TOKEN_RIGHT_BRACE,     // }
    CSS_TOKEN_LEFT_BRACKET,    // [
    CSS_TOKEN_RIGHT_BRACKET,   // ]
    CSS_TOKEN_COMMA,           // ,
    CSS_TOKEN_DELIM,           // any other single character
    CSS_TOKEN_EOF,             // end of file
    CSS_TOKEN_BAD_STRING,      // unclosed string
    CSS_TOKEN_BAD_URL,         // malformed URL
    CSS_TOKEN_IDENTIFIER,      // alias for CSS_TOKEN_IDENT
    CSS_TOKEN_MATCH,           // generic match tokenization error

    // CSS3+ specific tokens
    CSS_TOKEN_CDO,             // <!--
    CSS_TOKEN_CDC,             // -->
    CSS_TOKEN_CUSTOM_PROPERTY, // --custom-property
    CSS_TOKEN_CALC_FUNCTION,   // calc() with special parsing
    CSS_TOKEN_VAR_FUNCTION,    // var() with special parsing
    CSS_TOKEN_ENV_FUNCTION,    // env() environment variables
    CSS_TOKEN_ATTR_FUNCTION,   // attr() attribute references
    CSS_TOKEN_SUPPORTS_SELECTOR, // selector() in @supports
    CSS_TOKEN_LAYER_NAME,      // @layer name tokens
    CSS_TOKEN_CONTAINER_NAME,  // @container name tokens
    CSS_TOKEN_SCOPE_SELECTOR,  // @scope selector tokens
    CSS_TOKEN_NESTING_SELECTOR, // & nesting selector
    CSS_TOKEN_COLOR_FUNCTION,  // color(), oklch(), etc.
    CSS_TOKEN_ANGLE_FUNCTION,  // angle functions
    CSS_TOKEN_TIME_FUNCTION,   // time functions
    CSS_TOKEN_FREQUENCY_FUNCTION, // frequency functions
    CSS_TOKEN_RESOLUTION_FUNCTION // resolution functions
} CssTokenType;

// Hash token subtypes
typedef enum CssHashType {
    CSS_HASH_ID,               // valid identifier hash
    CSS_HASH_UNRESTRICTED      // unrestricted hash
} CssHashType;

// CSS Token structure for parsing
typedef struct CssToken {
    CssTokenType type;
    const char* start;         // pointer to start of token in input
    size_t length;             // length of token
    const char* value;         // null-terminated token value
    union {
        double number_value;   // for NUMBER, DIMENSION, PERCENTAGE tokens
        struct {
            double value;
            CssUnit unit;
        } dimension;
        struct {
            uint8_t r, g, b, a;
        } color;
        struct {
            const char* name;
            const char* fallback;
        } custom_property;
        CssHashType hash_type; // for HASH tokens
        char delimiter;        // for DELIM tokens
    } data;

    // Parse location metadata
    int line;
    int column;
    bool is_escaped;
    uint32_t unicode_codepoint; // For Unicode escapes
} CssToken;

// Token stream for parser consumption
typedef struct CssTokenStream {
    CssToken* tokens;
    size_t current;
    size_t length;
    Pool* pool;
} CssTokenStream;

// ============================================================================
// CSS Tokenizer
// ============================================================================

// Unicode support
typedef struct UnicodeChar {
    uint32_t codepoint;
    int byte_length;
} UnicodeChar;

// Tokenizer state
typedef struct CssTokenizer {
    Pool* pool;
    const char* input;
    size_t length;
    size_t position;
    int line;
    int column;
    bool supports_unicode;
    bool supports_css3;
} CssTokenizer;

// Tokenizer functions
CssTokenizer* css_tokenizer_create(Pool* pool);
void css_tokenizer_destroy(CssTokenizer* tokenizer);
int css_tokenizer_tokenize(CssTokenizer* tokenizer, const char* input, size_t length, CssToken** tokens);
CssToken* css_tokenize(const char* input, size_t length, Pool* pool, size_t* token_count);

// Unicode support functions
UnicodeChar css_parse_unicode_char(const char* input, size_t max_length);
bool css_is_valid_unicode_escape(const char* input);
char* css_decode_unicode_escapes(const char* input, Pool* pool);

// Character classification with Unicode support
bool css_is_name_start_char_unicode(uint32_t codepoint);
bool css_is_name_char_unicode(uint32_t codepoint);
bool css_is_whitespace_unicode(uint32_t codepoint);

// ============================================================================
// CSS Selector Parsing
// ============================================================================

// CSS Selector Types
typedef enum CssSelectorType {
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
    CSS_SELECTOR_PSEUDO_ENABLED,       // :enabled
    CSS_SELECTOR_PSEUDO_DISABLED,      // :disabled
    CSS_SELECTOR_PSEUDO_CHECKED,       // :checked
    CSS_SELECTOR_PSEUDO_INDETERMINATE, // :indeterminate
    CSS_SELECTOR_PSEUDO_VALID,         // :valid
    CSS_SELECTOR_PSEUDO_INVALID,       // :invalid

    // Pseudo-classes (CSS4+)
    CSS_SELECTOR_PSEUDO_HAS,            // :has()
    CSS_SELECTOR_PSEUDO_IS,             // :is()
    CSS_SELECTOR_PSEUDO_WHERE,          // :where()
    CSS_SELECTOR_PSEUDO_NOT,            // :not()
    CSS_SELECTOR_PSEUDO_CURRENT,        // :current()
    CSS_SELECTOR_PSEUDO_PAST,           // :past()
    CSS_SELECTOR_PSEUDO_FUTURE,         // :future()
    CSS_SELECTOR_PSEUDO_DIR,            // :dir()
    CSS_SELECTOR_PSEUDO_LANG,           // :lang()
    CSS_SELECTOR_PSEUDO_ANY_LINK,       // :any-link
    CSS_SELECTOR_PSEUDO_LOCAL_LINK,     // :local-link
    CSS_SELECTOR_PSEUDO_TARGET,         // :target
    CSS_SELECTOR_PSEUDO_TARGET_WITHIN,  // :target-within
    CSS_SELECTOR_PSEUDO_SCOPE,          // :scope

    // Form-related pseudo-classes (CSS4)
    CSS_SELECTOR_PSEUDO_REQUIRED,       // :required
    CSS_SELECTOR_PSEUDO_OPTIONAL,       // :optional
    CSS_SELECTOR_PSEUDO_READ_ONLY,      // :read-only
    CSS_SELECTOR_PSEUDO_READ_WRITE,     // :read-write
    CSS_SELECTOR_PSEUDO_PLACEHOLDER_SHOWN, // :placeholder-shown
    CSS_SELECTOR_PSEUDO_DEFAULT,        // :default
    CSS_SELECTOR_PSEUDO_IN_RANGE,       // :in-range
    CSS_SELECTOR_PSEUDO_OUT_OF_RANGE,   // :out-of-range
    CSS_SELECTOR_PSEUDO_FULLSCREEN,     // :fullscreen

    // Pseudo-elements
    CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE,     // ::before
    CSS_SELECTOR_PSEUDO_ELEMENT_AFTER,      // ::after
    CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LINE, // ::first-line
    CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LETTER, // ::first-letter
    CSS_SELECTOR_PSEUDO_ELEMENT_SELECTION,  // ::selection
    CSS_SELECTOR_PSEUDO_ELEMENT_BACKDROP,   // ::backdrop
    CSS_SELECTOR_PSEUDO_ELEMENT_PLACEHOLDER, // ::placeholder
    CSS_SELECTOR_PSEUDO_ELEMENT_MARKER,     // ::marker
    CSS_SELECTOR_PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON, // ::file-selector-button
    CSS_SELECTOR_PSEUDO_ELEMENT_HIGHLIGHT,  // ::highlight
    CSS_SELECTOR_PSEUDO_ELEMENT_SPELLING_ERROR, // ::spelling-error
    CSS_SELECTOR_PSEUDO_ELEMENT_GRAMMAR_ERROR,  // ::grammar-error
    CSS_SELECTOR_PSEUDO_ELEMENT_TARGET_TEXT,    // ::target-text
    CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION, // ::view-transition
    CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_GROUP, // ::view-transition-group
    CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_IMAGE_PAIR, // ::view-transition-image-pair
    CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_OLD, // ::view-transition-old
    CSS_SELECTOR_PSEUDO_ELEMENT_VIEW_TRANSITION_NEW, // ::view-transition-new

    // Combinators
    CSS_SELECTOR_COMBINATOR_DESCENDANT,     // space
    CSS_SELECTOR_COMBINATOR_CHILD,          // >
    CSS_SELECTOR_COMBINATOR_NEXT_SIBLING,   // +
    CSS_SELECTOR_COMBINATOR_SUBSEQUENT_SIBLING, // ~
    CSS_SELECTOR_COMBINATOR_COLUMN,         // ||

    // CSS4 additional pseudo-classes
    CSS_SELECTOR_PSEUDO_PLAYING,            // :playing
    CSS_SELECTOR_PSEUDO_PAUSED,             // :paused
    CSS_SELECTOR_PSEUDO_SEEKING,            // :seeking
    CSS_SELECTOR_PSEUDO_BUFFERING,          // :buffering
    CSS_SELECTOR_PSEUDO_STALLED,            // :stalled
    CSS_SELECTOR_PSEUDO_MUTED,              // :muted
    CSS_SELECTOR_PSEUDO_VOLUME_LOCKED,      // :volume-locked
    CSS_SELECTOR_PSEUDO_PICTURE_IN_PICTURE, // :picture-in-picture
    CSS_SELECTOR_PSEUDO_USER_INVALID,       // :user-invalid
    CSS_SELECTOR_PSEUDO_USER_VALID,         // :user-valid

    // CSS Nesting
    CSS_SELECTOR_NESTING,                    // &
    CSS_SELECTOR_NESTING_PARENT,             // & (parent selector)
    CSS_SELECTOR_NESTING_DESCENDANT          // & (descendant context)
} CssSelectorType;

// Combinator types
typedef enum CssCombinator {
    CSS_COMBINATOR_NONE,
    CSS_COMBINATOR_DESCENDANT,      // space
    CSS_COMBINATOR_CHILD,           // >
    CSS_COMBINATOR_NEXT_SIBLING,    // +
    CSS_COMBINATOR_SUBSEQUENT_SIBLING, // ~
    CSS_COMBINATOR_COLUMN           // ||
} CssCombinator;

// nth-child/nth-of-type formula (an+b)
typedef struct CssNthFormula {
    int a;  // coefficient
    int b;  // constant
    bool odd;   // true for "odd"
    bool even;  // true for "even"
} CssNthFormula;

// Simple selector (component of a compound selector)
typedef struct CssSimpleSelector {
    CssSelectorType type;
    const char* value;          // element name, class, id, etc.

    // Attribute selector data
    struct {
        const char* name;
        const char* value;
        bool case_insensitive;
    } attribute;

    // nth-child/nth-of-type data
    CssNthFormula nth_formula;

    // Pseudo-class argument (for :lang(), :dir(), etc.)
    const char* argument;

    // Function selectors (:is(), :where(), :has(), :not())
    struct CssSelector** function_selectors;
    size_t function_selector_count;
} CssSimpleSelector;

// Compound selector (sequence of simple selectors)
typedef struct CssCompoundSelector {
    CssSimpleSelector** simple_selectors;
    size_t simple_selector_count;
} CssCompoundSelector;

// Complete selector with combinators
typedef struct CssSelector {
    CssCompoundSelector** compound_selectors;
    CssCombinator* combinators;
    size_t compound_selector_count;
    CssSpecificity specificity;
} CssSelector;

// Selector group (comma-separated selectors)
typedef struct CssSelectorGroup {
    CssSelector** selectors;
    size_t selector_count;
} CssSelectorGroup;

// ============================================================================
// CSS Calc() Expression Parsing
// ============================================================================

// Calc() expression operators
typedef enum CssCalcOperator {
    CSS_CALC_OP_ADD,        // +
    CSS_CALC_OP_SUBTRACT,   // -
    CSS_CALC_OP_MULTIPLY,   // *
    CSS_CALC_OP_DIVIDE,     // /
    CSS_CALC_OP_MOD,        // mod
    CSS_CALC_OP_REM,        // rem
    CSS_CALC_OP_MIN,        // min()
    CSS_CALC_OP_MAX,        // max()
    CSS_CALC_OP_CLAMP,      // clamp()
    CSS_CALC_OP_ABS,        // abs()
    CSS_CALC_OP_SIGN,       // sign()
    CSS_CALC_OP_SIN,        // sin()
    CSS_CALC_OP_COS,        // cos()
    CSS_CALC_OP_TAN,        // tan()
    CSS_CALC_OP_ASIN,       // asin()
    CSS_CALC_OP_ACOS,       // acos()
    CSS_CALC_OP_ATAN,       // atan()
    CSS_CALC_OP_ATAN2,      // atan2()
    CSS_CALC_OP_POW,        // pow()
    CSS_CALC_OP_SQRT,       // sqrt()
    CSS_CALC_OP_HYPOT,      // hypot()
    CSS_CALC_OP_LOG,        // log()
    CSS_CALC_OP_EXP,        // exp()
    CSS_CALC_OP_ROUND       // round()
} CssCalcOperator;

// Calc() token for expression parsing
typedef struct CssCalcToken {
    enum {
        CSS_CALC_NUMBER,
        CSS_CALC_DIMENSION,
        CSS_CALC_PERCENTAGE,
        CSS_CALC_OPERATOR,
        CSS_CALC_FUNCTION,
        CSS_CALC_PAREN_OPEN,
        CSS_CALC_PAREN_CLOSE
    } type;
    union {
        double number;
        struct { double value; CssUnit unit; } dimension;
        char op;
        const char* function_name;
    } data;
} CssCalcToken;

// Calc() expression node (AST)
typedef struct CssCalcNode {
    CssCalcOperator op;
    CssValueType value_type;

    // Value data
    union {
        double number;
        struct {
            double value;
            CssUnit unit;
        } dimension;

        // For binary operators
        struct {
            struct CssCalcNode* left;
            struct CssCalcNode* right;
        } binary;

        // For unary operators
        struct CssCalcNode* operand;

        // For function calls
        struct {
            struct CssCalcNode** args;
            size_t arg_count;
        } function;
    } data;
} CssCalcNode;

// ============================================================================
// CSS Grid Template Parsing
// ============================================================================

typedef struct CssGridTemplate {
    char** line_names;
    double* track_sizes;
    CssUnit* track_units;
    int track_count;
    bool has_repeat;
    int repeat_count;
} CssGridTemplate;

// ============================================================================
// CSS Media Query Parsing
// ============================================================================

typedef enum CssMediaTokenType {
    CSS_MEDIA_TYPE,        // screen, print, etc.
    CSS_MEDIA_FEATURE,     // (width: 768px)
    CSS_MEDIA_OPERATOR,    // and, or, not
    CSS_MEDIA_RANGE        // (min-width: 768px)
} CssMediaTokenType;

typedef struct CssMediaToken {
    CssMediaTokenType type;
    const char* value;
    double number_value;
    CssUnit unit;
} CssMediaToken;

// ============================================================================
// CSS Container Query Parsing
// ============================================================================

typedef struct CssContainerToken {
    enum {
        CSS_CONTAINER_SIZE,
        CSS_CONTAINER_INLINE_SIZE,
        CSS_CONTAINER_STYLE
    } type;
    const char* feature;
    double value;
    CssUnit unit;
} CssContainerToken;

// ============================================================================
// CSS Function Information (for parsing)
// ============================================================================

typedef struct CssFunctionInfo {
    const char* name;
    int min_args;
    int max_args;
    CssValueType* arg_types;
    bool variadic;
    bool supports_calc;
} CssFunctionInfo;

// ============================================================================
// CSS Parser Functions
// ============================================================================

// Selector parsing
CssSelectorGroup* css_parse_selector_group(CssTokenStream* stream, Pool* pool);
CssSelector* css_parse_selector(CssTokenStream* stream, Pool* pool);
CssCompoundSelector* css_parse_compound_selector(CssTokenStream* stream, Pool* pool);
CssSimpleSelector* css_parse_simple_selector(CssTokenStream* stream, Pool* pool);
CssSpecificity css_calculate_specificity(const CssSelector* selector);

// Value parsing
CssValue* css_parse_value(CssTokenStream* stream, CssPropertyId property_id, Pool* pool);
CssValue* css_parse_number(CssTokenStream* stream, Pool* pool);
CssValue* css_parse_percentage(CssTokenStream* stream, Pool* pool);
CssValue* css_parse_string(CssTokenStream* stream, Pool* pool);
CssValue* css_parse_url(CssTokenStream* stream, Pool* pool);
CssValue* css_parse_function(CssTokenStream* stream, Pool* pool);

// Calc() expression parsing
CssCalcToken* css_parse_calc_expression(const char* expr, Pool* pool, size_t* token_count);
bool css_validate_calc_expression(CssCalcToken* tokens, size_t count);
CssCalcNode* css_parse_calc_ast(CssCalcToken* tokens, size_t count, Pool* pool);
CssValue* css_evaluate_calc_expression(CssCalcNode* ast, Pool* pool);

// Grid template parsing
CssGridTemplate* css_parse_grid_template(const char* template_str, Pool* pool);

// Media query parsing
CssMediaToken* css_parse_media_query(const char* media_query, Pool* pool, size_t* token_count);

// Container query parsing
CssContainerToken* css_parse_container_query(const char* container_query, Pool* pool, size_t* token_count);

// CSS3+ specific parsing
bool css_parse_custom_property_name(const char* input, size_t length);
CssFunctionInfo* css_get_function_info(const char* function_name);
bool css_is_valid_css_function(const char* name);

// Color parsing
CssColorType css_detect_color_type(const char* color_str);
bool css_parse_color_rgba(const char* color_str, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a);
bool css_parse_color_hsla(const char* color_str, double* h, double* s, double* l, double* a);

// Token stream utilities
CssTokenStream* css_token_stream_create(CssToken* tokens, size_t length, Pool* pool);
void css_token_stream_free(CssTokenStream* stream);
CssToken* css_token_stream_current(CssTokenStream* stream);
CssToken* css_token_stream_peek(CssTokenStream* stream, size_t offset);
bool css_token_stream_advance(CssTokenStream* stream);
bool css_token_stream_consume(CssTokenStream* stream, CssTokenType expected);
bool css_token_stream_at_end(CssTokenStream* stream);

// Token utility functions
bool css_token_is_whitespace(const CssToken* token);
bool css_token_is_comment(const CssToken* token);
bool css_token_equals_string(const CssToken* token, const char* str);
char* css_token_to_string(const CssToken* token, Pool* pool);
void css_free_tokens(CssToken* tokens);

// Character classification helpers
bool css_is_name_start_char(int c);
bool css_is_name_char(int c);
bool css_is_non_printable(int c);
bool css_is_newline(int c);
bool css_is_whitespace(int c);
bool css_is_digit(int c);
bool css_is_hex_digit(int c);

// Utility functions
const char* css_token_type_to_string(CssTokenType type);
const char* css_selector_type_to_string(CssSelectorType type);

// Error recovery and validation
bool css_token_is_recoverable_error(CssToken* token);
void css_token_fix_common_errors(CssToken* token, Pool* pool);

// Tokenizer creation and management
CssTokenizer* css_tokenizer_create(Pool* pool);
void css_tokenizer_destroy(CssTokenizer* tokenizer);
int css_tokenizer_tokenize(CssTokenizer* tokenizer,
                                   const char* input, size_t length,
                                   CssToken** tokens);

// Tokenizer aliases for compatibility
#define css_tokenizer_create css_tokenizer_create
#define css_tokenizer_destroy css_tokenizer_destroy
#define css_tokenizer_tokenize css_tokenizer_tokenize

// ============================================================================
// CSS Rule Parsing Functions (from css_parser.c)
// ============================================================================

// Token navigation helpers
int css_skip_whitespace_tokens(const CssToken* tokens, int start, int token_count);

// Selector parsing
CssSimpleSelector* css_parse_simple_selector_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool);
CssCompoundSelector* css_parse_compound_selector_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool);
CssSelector* css_parse_selector_with_combinators(const CssToken* tokens, int* pos, int token_count, Pool* pool);
CssSelectorGroup* css_parse_selector_group_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool);

// Declaration parsing
CssDeclaration* css_parse_declaration_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool);

// Rule parsing
int css_parse_rule_from_tokens_internal(const CssToken* tokens, int token_count, Pool* pool, CssRule** out_rule);
CssRule* css_parse_rule_from_tokens(const CssToken* tokens, int token_count, Pool* pool);

// Backward compatibility wrapper
CssRule* css_enhanced_parse_rule_from_tokens(const CssToken* tokens, int token_count, Pool* pool);

#ifdef __cplusplus
}
#endif

#endif // CSS_PARSER_H
