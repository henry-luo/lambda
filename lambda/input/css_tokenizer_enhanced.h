#ifndef CSS_TOKENIZER_ENHANCED_H
#define CSS_TOKENIZER_ENHANCED_H

#include "css_tokenizer.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
    CSS_VALUE_ENHANCED_GRADIENT           // gradient functions
} CSSValueTypeEnhanced;

// Enhanced CSS Unit Types
typedef enum {
    // Length units
    CSS_UNIT_PX, CSS_UNIT_EM, CSS_UNIT_REM, CSS_UNIT_EX, CSS_UNIT_CH,
    CSS_UNIT_VW, CSS_UNIT_VH, CSS_UNIT_VMIN, CSS_UNIT_VMAX,
    CSS_UNIT_CM, CSS_UNIT_MM, CSS_UNIT_IN, CSS_UNIT_PT, CSS_UNIT_PC,
    CSS_UNIT_Q,    // Quarter-millimeters
    CSS_UNIT_LH,   // Line height
    CSS_UNIT_RLH,  // Root line height
    CSS_UNIT_VI,   // Viewport inline
    CSS_UNIT_VB,   // Viewport block
    CSS_UNIT_SVW, CSS_UNIT_SVH, CSS_UNIT_LVW, CSS_UNIT_LVH, CSS_UNIT_DVW, CSS_UNIT_DVH,
    
    // Angle units
    CSS_UNIT_DEG, CSS_UNIT_GRAD, CSS_UNIT_RAD, CSS_UNIT_TURN,
    
    // Time units
    CSS_UNIT_S, CSS_UNIT_MS,
    
    // Frequency units
    CSS_UNIT_HZ, CSS_UNIT_KHZ,
    
    // Resolution units
    CSS_UNIT_DPI, CSS_UNIT_DPCM, CSS_UNIT_DPPX,
    
    // Grid units
    CSS_UNIT_FR,  // Fractional units for CSS Grid
    
    // Percentage
    CSS_UNIT_PERCENT,
    
    // Dimensionless
    CSS_UNIT_NONE
} CSSUnitTypeEnhanced;

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
            CSSUnitTypeEnhanced unit;
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

// Enhanced CSS Color Support
typedef enum {
    CSS_COLOR_HEX,         // #rrggbb, #rgb
    CSS_COLOR_RGB,         // rgb(), rgba()
    CSS_COLOR_HSL,         // hsl(), hsla()
    CSS_COLOR_HWB,         // hwb()
    CSS_COLOR_LAB,         // lab()
    CSS_COLOR_LCH,         // lch()
    CSS_COLOR_OKLAB,       // oklab()
    CSS_COLOR_OKLCH,       // oklch()
    CSS_COLOR_COLOR,       // color()
    CSS_COLOR_KEYWORD,     // named colors
    CSS_COLOR_TRANSPARENT, // transparent
    CSS_COLOR_CURRENT,     // currentColor
    CSS_COLOR_SYSTEM       // system colors
} CSSColorTypeEnhanced;

// CSS Function Information
typedef struct {
    const char* name;
    int min_args;
    int max_args;
    CSSValueTypeEnhanced* arg_types;
    bool variadic;
    bool supports_calc;
} CSSFunctionInfo;

// Enhanced tokenizer functions
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
CSSColorTypeEnhanced css_detect_color_type(const char* color_str);
bool css_parse_color_enhanced(const char* color_str, 
                             CSSColorTypeEnhanced* type,
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
        struct { double value; CSSUnitTypeEnhanced unit; } dimension;
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
    CSSUnitTypeEnhanced* track_units;
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
    CSSUnitTypeEnhanced unit;
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
    CSSUnitTypeEnhanced unit;
} CSSContainerToken;

CSSContainerToken* css_parse_container_query(const char* container_query, Pool* pool, size_t* token_count);

// Utility functions for enhanced tokenizer
const char* css_token_type_enhanced_to_str(CSSTokenTypeEnhanced type);
const char* css_unit_type_to_str(CSSUnitTypeEnhanced unit);
const char* css_color_type_to_str(CSSColorTypeEnhanced type);

// Error recovery and validation
bool css_token_is_recoverable_error(CSSTokenEnhanced* token);
void css_token_fix_common_errors(CSSTokenEnhanced* token, Pool* pool);

#ifdef __cplusplus
}
#endif

#endif // CSS_TOKENIZER_ENHANCED_H