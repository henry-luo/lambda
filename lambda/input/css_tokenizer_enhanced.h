#ifndef CSS_TOKENIZER_ENHANCED_H
#define CSS_TOKENIZER_ENHANCED_H

// Include existing CSS system first to avoid conflicts
#include "../../lib/css_property_system.h"
#include "../../lib/mempool.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Basic CSS Token Types (for compatibility)
typedef enum {
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
    CSS_TOKEN_MATCH            // generic match tokenization error
} CSSTokenType;

// Hash token subtypes
typedef enum {
    CSS_HASH_ID,               // valid identifier hash
    CSS_HASH_UNRESTRICTED      // unrestricted hash
} CSSHashType;

// CSS Token structure (basic compatibility)
typedef struct {
    CSSTokenType type;
    const char* start;         // pointer to start of token in input
    size_t length;             // length of token
    const char* value;         // null-terminated token value
    union {
        double number_value;   // for NUMBER, DIMENSION, PERCENTAGE tokens
        CSSHashType hash_type; // for HASH tokens
        char delimiter;        // for DELIM tokens
    } data;
} CSSToken;

// Type alias for consistency with properties API
typedef CSSToken css_token_t;

// Token stream for parser consumption
typedef struct {
    CSSToken* tokens;
    size_t current;
    size_t length;
    Pool* pool;
} CSSTokenStream;

// Enhanced CSS Token Types for CSS3+ features
typedef enum {
    // Basic tokens from original tokenizer
    CSS_TOKEN_ENHANCED_IDENT = CSS_TOKEN_IDENT,
    CSS_TOKEN_ENHANCED_FUNCTION = CSS_TOKEN_FUNCTION,
    CSS_TOKEN_ENHANCED_AT_KEYWORD = CSS_TOKEN_AT_KEYWORD,
    CSS_TOKEN_ENHANCED_HASH = CSS_TOKEN_HASH,
    CSS_TOKEN_ENHANCED_STRING = CSS_TOKEN_STRING,
    CSS_TOKEN_ENHANCED_URL = CSS_TOKEN_URL,
    CSS_TOKEN_ENHANCED_NUMBER = CSS_TOKEN_NUMBER,
    CSS_TOKEN_ENHANCED_DIMENSION = CSS_TOKEN_DIMENSION,
    CSS_TOKEN_ENHANCED_PERCENTAGE = CSS_TOKEN_PERCENTAGE,
    CSS_TOKEN_ENHANCED_UNICODE_RANGE = CSS_TOKEN_UNICODE_RANGE,
    CSS_TOKEN_ENHANCED_COMMA = CSS_TOKEN_COMMA,
    
    // Enhanced tokens for CSS3+ features
    CSS_TOKEN_ENHANCED_CDO = 100,          // <!--
    CSS_TOKEN_ENHANCED_CDC,                // -->
    CSS_TOKEN_ENHANCED_BAD_STRING,         // Unterminated string
    CSS_TOKEN_ENHANCED_BAD_URL,            // Malformed URL
    CSS_TOKEN_ENHANCED_CUSTOM_PROPERTY,    // --custom-property
    CSS_TOKEN_ENHANCED_CALC_FUNCTION,      // calc() with special parsing
    CSS_TOKEN_ENHANCED_VAR_FUNCTION,       // var() with special parsing
    CSS_TOKEN_ENHANCED_ENV_FUNCTION,       // env() environment variables
    CSS_TOKEN_ENHANCED_ATTR_FUNCTION,      // attr() attribute references
    CSS_TOKEN_ENHANCED_SUPPORTS_SELECTOR,  // selector() in @supports
    CSS_TOKEN_ENHANCED_LAYER_NAME,         // @layer name tokens
    CSS_TOKEN_ENHANCED_CONTAINER_NAME,     // @container name tokens
    CSS_TOKEN_ENHANCED_SCOPE_SELECTOR,     // @scope selector tokens
    CSS_TOKEN_ENHANCED_NESTING_SELECTOR,   // & nesting selector
    CSS_TOKEN_ENHANCED_COLOR_FUNCTION,     // color(), oklch(), etc.
    CSS_TOKEN_ENHANCED_ANGLE_FUNCTION,     // angle functions
    CSS_TOKEN_ENHANCED_TIME_FUNCTION,      // time functions
    CSS_TOKEN_ENHANCED_FREQUENCY_FUNCTION, // frequency functions
    CSS_TOKEN_ENHANCED_RESOLUTION_FUNCTION // resolution functions
} CSSTokenTypeEnhanced;

// Enhanced Unicode support
typedef struct {
    uint32_t codepoint;
    int byte_length;
} UnicodeChar;

// Enhanced CSS Value Types for CSS3+ data types
typedef enum {
    CSS_VALUE_ENHANCED_KEYWORD,
    CSS_VALUE_ENHANCED_LENGTH,
    CSS_VALUE_ENHANCED_PERCENTAGE,
    CSS_VALUE_ENHANCED_NUMBER,
    CSS_VALUE_ENHANCED_INTEGER,
    CSS_VALUE_ENHANCED_COLOR,
    CSS_VALUE_ENHANCED_STRING,
    CSS_VALUE_ENHANCED_URL,
    CSS_VALUE_ENHANCED_ANGLE,
    CSS_VALUE_ENHANCED_TIME,
    CSS_VALUE_ENHANCED_FREQUENCY,
    CSS_VALUE_ENHANCED_RESOLUTION,
    CSS_VALUE_ENHANCED_FLEX,               // CSS Grid/Flexbox fr unit
    CSS_VALUE_ENHANCED_POSITION,           // position values
    CSS_VALUE_ENHANCED_CUSTOM_PROPERTY,    // --custom-property references
    CSS_VALUE_ENHANCED_CALC,               // calc() expressions
    CSS_VALUE_ENHANCED_VAR,                // var() function calls
    CSS_VALUE_ENHANCED_ENV,                // env() environment variables
    CSS_VALUE_ENHANCED_ATTR,               // attr() attribute references
    CSS_VALUE_ENHANCED_MIN_MAX,            // min(), max() functions
    CSS_VALUE_ENHANCED_CLAMP,              // clamp() function
    CSS_VALUE_ENHANCED_COLOR_FUNCTION,     // color(), lab(), lch(), etc.
    CSS_VALUE_ENHANCED_GRID_TEMPLATE,      // grid template syntax
    CSS_VALUE_ENHANCED_TRANSFORM_FUNCTION, // transform functions
    CSS_VALUE_ENHANCED_FILTER_FUNCTION,    // filter functions
    CSS_VALUE_ENHANCED_GRADIENT,           // gradient functions
    CSS_VALUE_ENHANCED_UNICODE_RANGE,      // unicode-range values
    CSS_VALUE_ENHANCED_MIN,                // min() function values
    CSS_VALUE_ENHANCED_MAX,                // max() function values
    CSS_VALUE_ENHANCED_COLOR_MIX,          // color-mix() function
    CSS_VALUE_ENHANCED_LIST,               // list values
    CSS_VALUE_ENHANCED_LENGTH_PERCENTAGE,  // length or percentage values
    CSS_VALUE_ENHANCED_NUMBER_PERCENTAGE,  // number or percentage values
    CSS_VALUE_ENHANCED_FUNCTION            // generic function value
} CSSValueTypeEnhanced;

// Enhanced CSS Token with additional metadata
typedef struct {
    CSSTokenTypeEnhanced type;
    const char* start;
    size_t length;
    const char* value;
    union {
        double number_value;
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
        CSSHashType hash_type;
        char delimiter;
    } data;
    
    // Enhanced metadata
    int line;
    int column;
    bool is_escaped;
    uint32_t unicode_codepoint; // For Unicode escapes
} CSSTokenEnhanced;

// CSS Function Information
typedef struct {
    const char* name;
    int min_args;
    int max_args;
    CSSValueTypeEnhanced* arg_types;
    bool variadic;
    bool supports_calc;
} CSSFunctionInfo;

// Enhanced tokenizer state
typedef struct CSSTokenizerEnhanced {
    Pool* pool;
    const char* input;
    size_t length;
    size_t position;
    int line;
    int column;
    bool supports_unicode;
    bool supports_css3;
} CSSTokenizerEnhanced;

// Enhanced tokenizer functions
CSSTokenizerEnhanced* css_tokenizer_enhanced_create(Pool* pool);
void css_tokenizer_enhanced_destroy(CSSTokenizerEnhanced* tokenizer);
int css_tokenizer_enhanced_tokenize(CSSTokenizerEnhanced* tokenizer, 
                                   const char* input, size_t length, 
                                   CSSTokenEnhanced** tokens);
CSSTokenEnhanced* css_tokenize_enhanced(const char* input, size_t length, 
                                       Pool* pool, size_t* token_count);

// Unicode support functions
UnicodeChar css_parse_unicode_char(const char* input, size_t max_length);
bool css_is_valid_unicode_escape(const char* input);
char* css_decode_unicode_escapes(const char* input, Pool* pool);

// Enhanced character classification with Unicode support
bool css_is_name_start_char_unicode(uint32_t codepoint);
bool css_is_name_char_unicode(uint32_t codepoint);
bool css_is_whitespace_unicode(uint32_t codepoint);

// CSS3+ specific parsing functions
bool css_parse_custom_property_name(const char* input, size_t length);
CSSFunctionInfo* css_get_function_info(const char* function_name);
bool css_is_valid_css_function(const char* name);

// Enhanced color parsing
CssColorType css_detect_color_type(const char* color_str);
bool css_parse_color_enhanced(const char* color_str, 
                             CssColorType* type,
                             uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a);

// CSS calc() expression parsing
typedef struct CSSCalcToken {
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
        char operator;
        const char* function_name;
    } data;
} CSSCalcToken;

CSSCalcToken* css_parse_calc_expression(const char* expr, Pool* pool, size_t* token_count);
bool css_validate_calc_expression(CSSCalcToken* tokens, size_t count);

// CSS Grid template parsing
typedef struct CSSGridTemplate {
    char** line_names;
    double* track_sizes;
    CssUnit* track_units;
    int track_count;
    bool has_repeat;
    int repeat_count;
} CSSGridTemplate;

CSSGridTemplate* css_parse_grid_template(const char* template_str, Pool* pool);

// Enhanced media query parsing support
typedef enum {
    CSS_MEDIA_TYPE,        // screen, print, etc.
    CSS_MEDIA_FEATURE,     // (width: 768px)
    CSS_MEDIA_OPERATOR,    // and, or, not
    CSS_MEDIA_RANGE        // (min-width: 768px)
} CSSMediaTokenType;

typedef struct CSSMediaToken {
    CSSMediaTokenType type;
    const char* value;
    double number_value;
    CssUnit unit;
} CSSMediaToken;

CSSMediaToken* css_parse_media_query(const char* media_query, Pool* pool, size_t* token_count);

// Container query parsing support
typedef struct CSSContainerToken {
    enum {
        CSS_CONTAINER_SIZE,
        CSS_CONTAINER_INLINE_SIZE,
        CSS_CONTAINER_STYLE
    } type;
    const char* feature;
    double value;
    CssUnit unit;
} CSSContainerToken;

CSSContainerToken* css_parse_container_query(const char* container_query, Pool* pool, size_t* token_count);

// Utility functions for enhanced tokenizer
const char* css_token_type_enhanced_to_str(CSSTokenTypeEnhanced type);
const char* css_unit_type_to_str(CssUnit unit);
const char* css_color_type_to_str(CssColorType type);

// Error recovery and validation
bool css_token_is_recoverable_error(CSSTokenEnhanced* token);
void css_token_fix_common_errors(CSSTokenEnhanced* token, Pool* pool);

// Basic CSS tokenizer compatibility functions
CSSToken* css_tokenize(const char* input, size_t length, Pool* pool, size_t* token_count);
void css_free_tokens(CSSToken* tokens);
const char* css_token_type_to_str(CSSTokenType type);

// Token stream utilities
CSSTokenStream* css_token_stream_create(CSSToken* tokens, size_t length, Pool* pool);
void css_token_stream_free(CSSTokenStream* stream);
CSSToken* css_token_stream_current(CSSTokenStream* stream);
CSSToken* css_token_stream_peek(CSSTokenStream* stream, size_t offset);
bool css_token_stream_advance(CSSTokenStream* stream);
bool css_token_stream_consume(CSSTokenStream* stream, CSSTokenType expected);
bool css_token_stream_at_end(CSSTokenStream* stream);

// Token utility functions
bool css_token_is_whitespace(const CSSToken* token);
bool css_token_is_comment(const CSSToken* token);
bool css_token_equals_string(const CSSToken* token, const char* str);
char* css_token_to_string(const CSSToken* token, Pool* pool);

// Character classification helpers
bool css_is_name_start_char(int c);
bool css_is_name_char(int c);
bool css_is_non_printable(int c);
bool css_is_newline(int c);
bool css_is_whitespace(int c);
bool css_is_digit(int c);
bool css_is_hex_digit(int c);

#ifdef __cplusplus
}
#endif

#endif // CSS_TOKENIZER_ENHANCED_H
