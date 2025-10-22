#ifndef CSS_PROPERTY_VALUE_PARSER_H
#define CSS_PROPERTY_VALUE_PARSER_H

#include "css_parser.h"
#include "css_style.h"
#include "../../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

// Custom properties registry
typedef struct CssCustomProperties {
    const char** names;
    CssValue** initial_values;
    bool* inherits;
    const char** syntax;
    size_t capacity;
    size_t count;
} CssCustomProperties;

// Environment variables registry
typedef struct CssEnvVariables {
    const char** names;
    CssValue** values;
    size_t capacity;
    size_t count;
} CssEnvVariables;

typedef struct CssPropertyValueParser {
    Pool* pool;
    CssTokenStream* token_stream;
    const char* current_property;
    bool strict_mode;
    bool allow_calc;
    bool allow_custom_props;
    bool allow_env_vars;
    bool allow_math_functions;
    bool allow_color_functions;
    CssCustomProperties custom_properties;
    CssEnvVariables env_variables;
    char error_message[256];
    const char** error_messages;
    size_t error_capacity;
    size_t error_count;
    bool has_error;
} CssPropertyValueParser;

CssPropertyValueParser* css_property_value_parser_create(Pool* pool);
void css_property_value_parser_destroy(CssPropertyValueParser* parser);

CssValue* css_parse_property_value(CssPropertyValueParser* parser, const CssToken* tokens, int token_count, const char* property_name);
CssValue* css_parse_single_value(CssPropertyValueParser* parser, const CssToken* token, const char* property_name);
CssValue* css_parse_value_list(CssPropertyValueParser* parser, const CssToken* tokens, int token_count, const char* property_name);

// Environment variable management
bool css_property_value_parser_set_env_variable(CssPropertyValueParser* parser,
                                               const char* name,
                                               CssValue* value);

// Error handling
void css_property_value_parser_add_error(CssPropertyValueParser* parser, const char* message);

// Function parsers
CssValue* css_parse_calc_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CSSVarRef* css_parse_var_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CSSEnvRef* css_parse_env_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CSSAttrRef* css_parse_attr_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CssValue* css_parse_min_max_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count, int operation);
CssValue* css_parse_clamp_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CssValue* css_parse_math_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count, int operation);
CSSColorMix* css_parse_color_mix_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CssValue* css_parse_rgb_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CssValue* css_parse_hsl_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CssValue* css_parse_hwb_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CssValue* css_parse_lab_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CssValue* css_parse_lch_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CssValue* css_parse_oklab_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CssValue* css_parse_oklch_function(CssPropertyValueParser* parser, const CssToken* tokens, int token_count);
CssValue* css_parse_generic_function(CssPropertyValueParser* parser, const char* function_name, const CssToken* tokens, int token_count);

// CSS Value list functions
CssValue* css_value_list_create(Pool* pool, bool space_separated);
void css_value_list_add(CssValue* list, CssValue* value);

#ifdef __cplusplus
}
#endif

#endif
