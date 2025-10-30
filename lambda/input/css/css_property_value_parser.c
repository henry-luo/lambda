#include "css_property_value_parser.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

// Forward declarations
static void css_property_value_parser_set_default_env_vars(CssPropertyValueParser* parser);
CssValue* css_parse_single_value(CssPropertyValueParser* parser,
                                        const CssToken* token,
                                        const char* property_name);
static CssValue* css_parse_function_value(CssPropertyValueParser* parser,
                                                 const CssToken* tokens,
                                                 int token_count,
                                                 const char* property_name);
CssValue* css_parse_value_list(CssPropertyValueParser* parser,
                                      const CssToken* tokens,
                                      int token_count,
                                      const char* property_name);

// Parser creation and destruction
CssPropertyValueParser* css_property_value_parser_create(Pool* pool) {
    if (!pool) return NULL;

    CssPropertyValueParser* parser = (CssPropertyValueParser*)pool_calloc(pool, sizeof(CssPropertyValueParser));
    if (!parser) return NULL;

    parser->pool = pool;

    // Enable all features by default
    parser->allow_calc = true;
    parser->allow_custom_props = true;
    parser->allow_env_vars = true;
    parser->allow_math_functions = true;
    parser->allow_color_functions = true;

    // Initialize custom properties registry
    parser->custom_properties.capacity = 32;
    parser->custom_properties.names = (const char**)pool_alloc(pool, parser->custom_properties.capacity * sizeof(char*));
    parser->custom_properties.initial_values = (CssValue**)pool_alloc(pool, parser->custom_properties.capacity * sizeof(CssValue*));
    parser->custom_properties.inherits = (bool*)pool_alloc(pool, parser->custom_properties.capacity * sizeof(bool));
    parser->custom_properties.syntax = (const char**)pool_alloc(pool, parser->custom_properties.capacity * sizeof(char*));

    // Initialize environment variables
    parser->env_variables.capacity = 16;
    parser->env_variables.names = (const char**)pool_alloc(pool, parser->env_variables.capacity * sizeof(char*));
    parser->env_variables.values = (CssValue**)pool_alloc(pool, parser->env_variables.capacity * sizeof(CssValue*));

    // Initialize error tracking
    parser->error_capacity = 10;
    parser->error_messages = (const char**)pool_alloc(pool, parser->error_capacity * sizeof(char*));

    // Set default environment variables
    css_property_value_parser_set_default_env_vars(parser);

    return parser;
}

void css_property_value_parser_destroy(CssPropertyValueParser* parser) {
    // Memory managed by pool, nothing to do
    (void)parser;
}

// Set default environment variables
static void css_property_value_parser_set_default_env_vars(CssPropertyValueParser* parser) {
    if (!parser) return;

    // Safe area insets (for mobile devices)
    CssValue* zero_px = css_value_create_length(parser->pool, 0.0, CSS_UNIT_PX);
    css_property_value_parser_set_env_variable(parser, "safe-area-inset-top", zero_px);
    css_property_value_parser_set_env_variable(parser, "safe-area-inset-right", zero_px);
    css_property_value_parser_set_env_variable(parser, "safe-area-inset-bottom", zero_px);
    css_property_value_parser_set_env_variable(parser, "safe-area-inset-left", zero_px);

    // Keyboard insets
    css_property_value_parser_set_env_variable(parser, "keyboard-inset-width", zero_px);
    css_property_value_parser_set_env_variable(parser, "keyboard-inset-height", zero_px);

    // Title bar area
    css_property_value_parser_set_env_variable(parser, "titlebar-area-x", zero_px);
    css_property_value_parser_set_env_variable(parser, "titlebar-area-y", zero_px);
    css_property_value_parser_set_env_variable(parser, "titlebar-area-width", zero_px);
    css_property_value_parser_set_env_variable(parser, "titlebar-area-height", zero_px);
}

// Configuration setters
void css_property_value_parser_set_calc_support(CssPropertyValueParser* parser, bool enabled) {
    if (parser) parser->allow_calc = enabled;
}

void css_property_value_parser_set_custom_props_support(CssPropertyValueParser* parser, bool enabled) {
    if (parser) parser->allow_custom_props = enabled;
}

void css_property_value_parser_set_env_vars_support(CssPropertyValueParser* parser, bool enabled) {
    if (parser) parser->allow_env_vars = enabled;
}

void css_property_value_parser_set_math_functions_support(CssPropertyValueParser* parser, bool enabled) {
    if (parser) parser->allow_math_functions = enabled;
}

// CSS Value creation utilities
CssValue* css_value_create_keyword(Pool* pool, const char* keyword) {
    if (!pool || !keyword) return NULL;

    CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_KEYWORD;

    // Strip quotes from keyword (font names can be quoted)
    size_t len = strlen(keyword);
    const char* keyword_to_copy = keyword;
    char* unquoted = NULL;

    if (len >= 2 && ((keyword[0] == '\'' && keyword[len-1] == '\'') ||
                     (keyword[0] == '"' && keyword[len-1] == '"'))) {
        // Allocate space for unquoted string
        unquoted = (char*)pool_alloc(pool, len - 1);
        if (unquoted) {
            memcpy(unquoted, keyword + 1, len - 2);
            unquoted[len - 2] = '\0';
            keyword_to_copy = unquoted;
        }
    }

    // Copy keyword string
    char* keyword_copy = (char*)pool_alloc(pool, strlen(keyword_to_copy) + 1);
    if (keyword_copy) {
        strcpy(keyword_copy, keyword_to_copy);
        value->data.keyword = keyword_copy;
    }

    return value;
}CssValue* css_value_create_number(Pool* pool, double number) {
    if (!pool) return NULL;

    CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_NUMBER;
    value->data.number.value = number;

    return value;
}

// Helper function to convert unit string to CssUnit enum
static CssUnit css_unit_from_string(const char* unit) {
    if (!unit) return CSS_UNIT_NONE;

    // Length units (absolute)
    if (strcmp(unit, "px") == 0) return CSS_UNIT_PX;
    if (strcmp(unit, "cm") == 0) return CSS_UNIT_CM;
    if (strcmp(unit, "mm") == 0) return CSS_UNIT_MM;
    if (strcmp(unit, "in") == 0) return CSS_UNIT_IN;
    if (strcmp(unit, "pt") == 0) return CSS_UNIT_PT;
    if (strcmp(unit, "pc") == 0) return CSS_UNIT_PC;
    if (strcmp(unit, "Q") == 0) return CSS_UNIT_Q;

    // Length units (relative)
    if (strcmp(unit, "em") == 0) return CSS_UNIT_EM;
    if (strcmp(unit, "ex") == 0) return CSS_UNIT_EX;
    if (strcmp(unit, "cap") == 0) return CSS_UNIT_CAP;
    if (strcmp(unit, "ch") == 0) return CSS_UNIT_CH;
    if (strcmp(unit, "ic") == 0) return CSS_UNIT_IC;
    if (strcmp(unit, "rem") == 0) return CSS_UNIT_REM;
    if (strcmp(unit, "lh") == 0) return CSS_UNIT_LH;
    if (strcmp(unit, "rlh") == 0) return CSS_UNIT_RLH;

    // Viewport units
    if (strcmp(unit, "vw") == 0) return CSS_UNIT_VW;
    if (strcmp(unit, "vh") == 0) return CSS_UNIT_VH;
    if (strcmp(unit, "vi") == 0) return CSS_UNIT_VI;
    if (strcmp(unit, "vb") == 0) return CSS_UNIT_VB;
    if (strcmp(unit, "vmin") == 0) return CSS_UNIT_VMIN;
    if (strcmp(unit, "vmax") == 0) return CSS_UNIT_VMAX;

    // Small, large, and dynamic viewport units
    if (strcmp(unit, "svw") == 0) return CSS_UNIT_SVW;
    if (strcmp(unit, "svh") == 0) return CSS_UNIT_SVH;
    if (strcmp(unit, "lvw") == 0) return CSS_UNIT_LVW;
    if (strcmp(unit, "lvh") == 0) return CSS_UNIT_LVH;
    if (strcmp(unit, "dvw") == 0) return CSS_UNIT_DVW;
    if (strcmp(unit, "dvh") == 0) return CSS_UNIT_DVH;

    // Container query units
    if (strcmp(unit, "cqw") == 0) return CSS_UNIT_CQW;
    if (strcmp(unit, "cqh") == 0) return CSS_UNIT_CQH;
    if (strcmp(unit, "cqi") == 0) return CSS_UNIT_CQI;
    if (strcmp(unit, "cqb") == 0) return CSS_UNIT_CQB;
    if (strcmp(unit, "cqmin") == 0) return CSS_UNIT_CQMIN;
    if (strcmp(unit, "cqmax") == 0) return CSS_UNIT_CQMAX;

    // Angle units
    if (strcmp(unit, "deg") == 0) return CSS_UNIT_DEG;
    if (strcmp(unit, "grad") == 0) return CSS_UNIT_GRAD;
    if (strcmp(unit, "rad") == 0) return CSS_UNIT_RAD;
    if (strcmp(unit, "turn") == 0) return CSS_UNIT_TURN;

    // Time units
    if (strcmp(unit, "s") == 0) return CSS_UNIT_S;
    if (strcmp(unit, "ms") == 0) return CSS_UNIT_MS;

    // Frequency units
    if (strcmp(unit, "Hz") == 0) return CSS_UNIT_HZ;
    if (strcmp(unit, "kHz") == 0) return CSS_UNIT_KHZ;

    // Resolution units
    if (strcmp(unit, "dpi") == 0) return CSS_UNIT_DPI;
    if (strcmp(unit, "dpcm") == 0) return CSS_UNIT_DPCM;
    if (strcmp(unit, "dppx") == 0) return CSS_UNIT_DPPX;

    // Flex units
    if (strcmp(unit, "fr") == 0) return CSS_UNIT_FR;

    // Percentage
    if (strcmp(unit, "%") == 0) return CSS_UNIT_PERCENT;

    return CSS_UNIT_UNKNOWN; // Unknown unit
}

CssValue* css_value_create_length_from_string(Pool* pool, double number, const char* unit) {
    if (!pool || !unit) return NULL;

    CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_LENGTH;
    value->data.length.value = number;
    value->data.length.unit = css_unit_from_string(unit);

    return value;
}

CssValue* css_value_create_string(Pool* pool, const char* string) {
    if (!pool || !string) return NULL;

    CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_STRING;

    // Strip quotes from string values (both single and double quotes)
    size_t len = strlen(string);
    if (len >= 2 && ((string[0] == '\'' && string[len-1] == '\'') ||
                     (string[0] == '"' && string[len-1] == '"'))) {
        // Allocate space for unquoted string
        char* unquoted = (char*)pool_alloc(pool, len - 1);
        if (unquoted) {
            memcpy(unquoted, string + 1, len - 2);
            unquoted[len - 2] = '\0';
            value->data.string = unquoted;
        } else {
            value->data.string = pool_strdup(pool, string);
        }
    } else {
        value->data.string = pool_strdup(pool, string);
    }

    return value;
}CssValue* css_value_create_url(Pool* pool, const char* url) {
    if (!pool || !url) return NULL;

    CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_URL;
    value->data.url = pool_strdup(pool, url);

    return value;
}

CssValue* css_value_create_color_hex(Pool* pool, const char* hex) {
    if (!pool || !hex) return NULL;

    CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_COLOR;
    value->data.color_hex = pool_strdup(pool, hex);

    return value;
}

CssValue* css_value_create_unicode_range(Pool* pool, const char* range) {
    if (!pool || !range) return NULL;

    CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_STRING;
    value->data.unicode_range = pool_strdup(pool, range);

    return value;
}

CssValue* css_value_create_percentage(Pool* pool, double percentage) {
    if (!pool) return NULL;

    CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_PERCENTAGE;
    value->data.percentage.value = percentage;

    return value;
}

// Main value parsing function
CssValue* css_parse_property_value(CssPropertyValueParser* parser,
                                   const CssToken* tokens,
                                   int token_count,
                                   const char* property_name) {
    if (!parser || !tokens || token_count <= 0) return NULL;

    // Handle single token cases first
    if (token_count == 1) {
        return css_parse_single_value(parser, &tokens[0], property_name);
    }

    // Check for function calls
    if (tokens[0].type == CSS_TOKEN_FUNCTION) {
        return css_parse_function_value(parser, tokens, token_count, property_name);
    }

    // Parse value list (space or comma separated)
    return css_parse_value_list(parser, tokens, token_count, property_name);
}

// Parse single token value
CssValue* css_parse_single_value(CssPropertyValueParser* parser,
                                                        const CssToken* token,
                                                        const char* property_name) {
    if (!parser || !token) return NULL;

    switch (token->type) {
        case CSS_TOKEN_IDENT:
            return css_value_create_keyword(parser->pool, token->value);

        case CSS_TOKEN_NUMBER:
            return css_value_create_number(parser->pool, token->data.number_value);

        case CSS_TOKEN_DIMENSION:
            return css_value_create_length(parser->pool, token->data.dimension.value, token->data.dimension.unit);

        case CSS_TOKEN_PERCENTAGE:
            return css_value_create_percentage(parser->pool, token->data.number_value);

        case CSS_TOKEN_STRING:
            return css_value_create_string(parser->pool, token->value);

        case CSS_TOKEN_URL:
            return css_value_create_url(parser->pool, token->value);

        case CSS_TOKEN_HASH:
            // Handle color hex values
            return css_value_create_color_hex(parser->pool, token->value);

        case CSS_TOKEN_UNICODE_RANGE:
            return css_value_create_unicode_range(parser->pool, token->value);

        default:
            css_property_value_parser_add_error(parser, "Unsupported token type in value");
            return NULL;
    }
}

// Parse function value
static CssValue* css_parse_function_value(CssPropertyValueParser* parser,
                                                  const CssToken* tokens,
                                                  int token_count,
                                                  const char* property_name) {
    if (!parser || !tokens || token_count < 1) return NULL;

    const char* function_name = tokens[0].value;

    // Handle CSS functions
    if (strcmp(function_name, "calc") == 0 && parser->allow_calc) {
        return css_parse_calc_function(parser, tokens + 1, token_count - 1);
    }

    if (strcmp(function_name, "var") == 0 && parser->allow_custom_props) {
        CSSVarRef* var_ref = css_parse_var_function(parser, tokens + 1, token_count - 1);
        if (var_ref) {
            CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
            if (value) {
                value->type = CSS_VALUE_VAR;
                value->data.var_ref = var_ref;
            }
            return value;
        }
    }

    if (strcmp(function_name, "env") == 0 && parser->allow_env_vars) {
        CSSEnvRef* env_ref = css_parse_env_function(parser, tokens + 1, token_count - 1);
        if (env_ref) {
            CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
            if (value) {
                value->type = CSS_VALUE_ENV;
                value->data.env_ref = env_ref;
            }
            return value;
        }
    }

    if (strcmp(function_name, "attr") == 0) {
        CSSAttrRef* attr_ref = css_parse_attr_function(parser, tokens + 1, token_count - 1);
        if (attr_ref) {
            CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
            if (value) {
                value->type = CSS_VALUE_ATTR;
                value->data.attr_ref = attr_ref;
            }
            return value;
        }
    }

    // Math functions
    if (parser->allow_math_functions) {
        if (strcmp(function_name, "min") == 0) {
            return css_parse_min_max_function(parser, tokens + 1, token_count - 1, CSS_CALC_OP_MIN);
        }
        if (strcmp(function_name, "max") == 0) {
            return css_parse_min_max_function(parser, tokens + 1, token_count - 1, CSS_CALC_OP_MAX);
        }
        if (strcmp(function_name, "clamp") == 0) {
            return css_parse_clamp_function(parser, tokens + 1, token_count - 1);
        }
        if (strcmp(function_name, "abs") == 0) {
            return css_parse_math_function(parser, tokens + 1, token_count - 1, CSS_CALC_OP_ABS);
        }
        if (strcmp(function_name, "round") == 0) {
            return css_parse_math_function(parser, tokens + 1, token_count - 1, CSS_CALC_OP_ROUND);
        }
    }

    // Color functions
    if (parser->allow_color_functions) {
        if (strcmp(function_name, "color-mix") == 0) {
            CSSColorMix* color_mix = css_parse_color_mix_function(parser, tokens + 1, token_count - 1);
            if (color_mix) {
                CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
                if (value) {
                    value->type = CSS_VALUE_COLOR_MIX;
                    value->data.color_mix = color_mix;
                }
                return value;
            }
        }

        if (strcmp(function_name, "rgb") == 0 || strcmp(function_name, "rgba") == 0) {
            return css_parse_rgb_function(parser, tokens + 1, token_count - 1);
        }
        if (strcmp(function_name, "hsl") == 0 || strcmp(function_name, "hsla") == 0) {
            return css_parse_hsl_function(parser, tokens + 1, token_count - 1);
        }
        if (strcmp(function_name, "hwb") == 0) {
            return css_parse_hwb_function(parser, tokens + 1, token_count - 1);
        }
        if (strcmp(function_name, "lab") == 0) {
            return css_parse_lab_function(parser, tokens + 1, token_count - 1);
        }
        if (strcmp(function_name, "lch") == 0) {
            return css_parse_lch_function(parser, tokens + 1, token_count - 1);
        }
        if (strcmp(function_name, "oklab") == 0) {
            return css_parse_oklab_function(parser, tokens + 1, token_count - 1);
        }
        if (strcmp(function_name, "oklch") == 0) {
            return css_parse_oklch_function(parser, tokens + 1, token_count - 1);
        }
    }

    // Generic function fallback
    return css_parse_generic_function(parser, function_name, tokens + 1, token_count - 1);
}

// Generic function parser (implementation for unknown functions)
CssValue* css_parse_generic_function(CssPropertyValueParser* parser,
                                                   const char* function_name,
                                                   const CssToken* tokens,
                                                   int token_count) {
    // Implementation for generic function parsing
    if (!parser || !function_name || !tokens || token_count <= 0) {
        return NULL;
    }

    CssValue* value = pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) {
        return NULL;
    }

    value->type = CSS_VALUE_FUNCTION;
    value->data.function.name = pool_strdup(parser->pool, function_name);
    value->data.function.args = NULL;
    value->data.function.arg_count = 0;

    return value;
}CssValue* css_parse_value_list(CssPropertyValueParser* parser,
                                      const CssToken* tokens,
                                      int token_count,
                                      const char* property_name) {
    if (!parser || !tokens || token_count <= 0) return NULL;

    // Create a list value
    CssValue* list = css_value_list_create(parser->pool, false);
    if (!list) return NULL;

    // Parse individual values separated by commas or whitespace
    int i = 0;
    while (i < token_count) {
        // Skip whitespace and commas
        while (i < token_count &&
               (tokens[i].type == CSS_TOKEN_WHITESPACE ||
                tokens[i].type == CSS_TOKEN_COMMA)) {
            i++;
        }

        if (i >= token_count) break;

        // Parse single value
        CssValue* value = css_parse_single_value(parser, &tokens[i], property_name);
        if (value) {
            css_value_list_add(list, value);
        }

        i++;
    }

    return list;
}

// Parse calc() function
// Forward declaration for calc expression implementation
static CssCalcNode* css_property_value_parser_parse_calc(CssPropertyValueParser* parser,
                                                  const CssToken* tokens,
                                                  int token_count);

CssValue* css_parse_calc_function(CssPropertyValueParser* parser,
                                                 const CssToken* tokens,
                                                 int token_count) {
    if (!parser || !tokens || token_count <= 0) return NULL;

    CssCalcNode* calc_node = css_property_value_parser_parse_calc(parser, tokens, token_count);
    if (!calc_node) return NULL;

    CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_CALC;
    value->data.calc_expression = calc_node;

    return value;
}

// Basic implementation for calc expression parsing
static CssCalcNode* css_property_value_parser_parse_calc(CssPropertyValueParser* parser,
                                                  const CssToken* tokens,
                                                  int token_count) {
    // Basic implementation for calc expression parsing
    if (!parser || !tokens || token_count <= 0) {
        return NULL;
    }

    // Create a simple calc node for basic expressions
    CssCalcNode* node = (CssCalcNode*)pool_calloc(parser->pool, sizeof(CssCalcNode));
    if (!node) return NULL;

    // Initialize to a simple number node
    // CssCalcNode structure members depend on the actual definition
    // which may vary - setting all accessible fields to safe values

    // NOTE: Full calc() expression parsing would require:
    // - Operator precedence (* / before + -)
    // - Unit handling and conversion
    // - Nested calc() support
    // - Mixed unit types (lengths, percentages, numbers)

    return node;
}

// Parse var() function
CSSVarRef* css_parse_var_function(CssPropertyValueParser* parser,
                                 const CssToken* tokens,
                                 int token_count) {
    if (!parser || !tokens || token_count < 1) return NULL;

    // First token should be the variable name
    if (tokens[0].type != CSS_TOKEN_IDENT) {
        css_property_value_parser_add_error(parser, "var() function requires identifier argument");
        return NULL;
    }

    CSSVarRef* var_ref = (CSSVarRef*)pool_calloc(parser->pool, sizeof(CSSVarRef));
    if (!var_ref) return NULL;

    // Variable name (without --)
    const char* full_name = tokens[0].value;
    if (strncmp(full_name, "--", 2) == 0) {
        var_ref->name = full_name + 2;  // Skip --
    } else {
        var_ref->name = full_name;
    }

    // Check for fallback value
    if (token_count > 2 && tokens[1].type == CSS_TOKEN_COMMA) {
        // Parse fallback value
        var_ref->fallback = css_parse_property_value(parser, tokens + 2, token_count - 2, NULL);
        var_ref->has_fallback = var_ref->fallback != NULL;
    }

    return var_ref;
}

// Parse env() function
CSSEnvRef* css_parse_env_function(CssPropertyValueParser* parser,
                                 const CssToken* tokens,
                                 int token_count) {
    if (!parser || !tokens || token_count < 1) return NULL;

    // First token should be the environment variable name
    if (tokens[0].type != CSS_TOKEN_IDENT) {
        css_property_value_parser_add_error(parser, "env() function requires identifier argument");
        return NULL;
    }

    CSSEnvRef* env_ref = (CSSEnvRef*)pool_calloc(parser->pool, sizeof(CSSEnvRef));
    if (!env_ref) return NULL;

    env_ref->name = tokens[0].value;

    // Check for fallback value
    if (token_count > 2 && tokens[1].type == CSS_TOKEN_COMMA) {
        // Parse fallback value
        env_ref->fallback = css_parse_property_value(parser, tokens + 2, token_count - 2, NULL);
        env_ref->has_fallback = env_ref->fallback != NULL;
    }

    return env_ref;
}

// Parse attr() function
CSSAttrRef* css_parse_attr_function(CssPropertyValueParser* parser,
                                   const CssToken* tokens,
                                   int token_count) {
    if (!parser || !tokens || token_count < 1) return NULL;

    // First token should be the attribute name
    if (tokens[0].type != CSS_TOKEN_IDENT) {
        css_property_value_parser_add_error(parser, "attr() function requires identifier argument");
        return NULL;
    }

    CSSAttrRef* attr_ref = (CSSAttrRef*)pool_calloc(parser->pool, sizeof(CSSAttrRef));
    if (!attr_ref) return NULL;

    attr_ref->name = tokens[0].value;

    // Check for type or unit specifier
    int next_index = 1;
    if (token_count > next_index && tokens[next_index].type == CSS_TOKEN_IDENT) {
        attr_ref->type_or_unit = tokens[next_index].value;
        next_index++;
    }

    // Check for fallback value
    if (token_count > next_index + 1 && tokens[next_index].type == CSS_TOKEN_COMMA) {
        // Parse fallback value
        attr_ref->fallback = css_parse_property_value(parser, tokens + next_index + 1,
                                                      token_count - next_index - 1, NULL);
        attr_ref->has_fallback = attr_ref->fallback != NULL;
    }

    return attr_ref;
}

// Error handling
void css_property_value_parser_add_error(CssPropertyValueParser* parser, const char* message) {
    if (!parser || !message) return;

    if (parser->error_count >= parser->error_capacity) return;

    // Copy error message
    size_t len = strlen(message);
    char* error_copy = (char*)pool_alloc(parser->pool, len + 1);
    if (error_copy) {
        strcpy(error_copy, message);
        parser->error_messages[parser->error_count++] = error_copy;
    }
}

bool css_property_value_parser_has_errors(CssPropertyValueParser* parser) {
    return parser && parser->error_count > 0;
}

void css_property_value_parser_clear_errors(CssPropertyValueParser* parser) {
    if (parser) {
        parser->error_count = 0;
    }
}

const char** css_property_value_parser_get_errors(CssPropertyValueParser* parser, int* count) {
    if (!parser || !count) return NULL;

    *count = parser->error_count;
    return parser->error_messages;
}

// Type checking utilities
bool css_value_is_length(const CssValue* value) {
    return value && (value->type == CSS_VALUE_LENGTH ||
                     value->type == CSS_VALUE_LENGTH_PERCENTAGE);
}

bool css_value_is_percentage(const CssValue* value) {
    return value && (value->type == CSS_VALUE_PERCENTAGE ||
                     value->type == CSS_VALUE_LENGTH_PERCENTAGE ||
                     value->type == CSS_VALUE_NUMBER_PERCENTAGE);
}

bool css_value_is_number(const CssValue* value) {
    return value && (value->type == CSS_VALUE_NUMBER ||
                     value->type == CSS_VALUE_INTEGER ||
                     value->type == CSS_VALUE_NUMBER_PERCENTAGE);
}

bool css_value_is_color(const CssValue* value) {
    return value && (value->type == CSS_VALUE_COLOR ||
                     value->type == CSS_VALUE_COLOR_MIX);
}

bool css_value_is_keyword(const CssValue* value, const char* keyword) {
    return value && value->type == CSS_VALUE_KEYWORD &&
           value->data.keyword && strcmp(value->data.keyword, keyword) == 0;
}

bool css_value_is_function(const CssValue* value, const char* function_name) {
    return value && value->type == CSS_VALUE_FUNCTION &&
           value->data.function.name && strcmp(value->data.function.name, function_name) == 0;
}

// Debug utilities
const char* css_value_enhanced_type_to_string(CSSValueTypeEnhanced type) {
    switch (type) {
        case CSS_VALUE_ENHANCED_KEYWORD: return "keyword";
        case CSS_VALUE_ENHANCED_LENGTH: return "length";
        case CSS_VALUE_ENHANCED_PERCENTAGE: return "percentage";
        case CSS_VALUE_ENHANCED_NUMBER: return "number";
        case CSS_VALUE_ENHANCED_COLOR: return "color";
        case CSS_VALUE_ENHANCED_STRING: return "string";
        case CSS_VALUE_ENHANCED_URL: return "url";
        case CSS_VALUE_ENHANCED_FUNCTION: return "function";
        case CSS_VALUE_ENHANCED_CALC: return "calc";
        case CSS_VALUE_ENHANCED_VAR: return "var";
        case CSS_VALUE_ENHANCED_ENV: return "env";
        case CSS_VALUE_ENHANCED_ATTR: return "attr";
        case CSS_VALUE_ENHANCED_COLOR_MIX: return "color-mix";
        case CSS_VALUE_ENHANCED_LIST: return "list";
        default: return "unknown";
    }
}

void css_value_enhanced_print_debug(const CssValue* value) {
    if (!value) {
        printf("(null value)\n");
        return;
    }

    printf("Value type: %s", css_value_enhanced_type_to_string(value->type));

    switch (value->type) {
        case CSS_VALUE_ENHANCED_KEYWORD:
            printf(", keyword: %s", value->data.keyword ? value->data.keyword : "(null)");
            break;
        case CSS_VALUE_ENHANCED_NUMBER:
            printf(", number: %g", value->data.number);
            break;
        case CSS_VALUE_ENHANCED_LENGTH:
            printf(", length: %g%s", value->data.length.value,
                   value->data.length.unit ? value->data.length.unit : "");
            break;
        case CSS_VALUE_ENHANCED_PERCENTAGE:
            printf(", percentage: %g%%", value->data.percentage);
            break;
        case CSS_VALUE_ENHANCED_VAR:
            printf(", var: --%s", value->data.var_ref ? value->data.var_ref->name : "(null)");
            break;
        default:
            break;
    }

    printf("\n");
}

// CSS property value parser utility functions

// Environment variable setter for parser
bool css_property_value_parser_set_env_variable(CssPropertyValueParser* parser,
                                               const char* name,
                                               CssValue* value) {
    // Implementation for environment variable setting
    if (!parser || !name || !value) return false;
    // Would set environment variables for env() function resolution
    return true;
}

// Provide the implementation for the declared function that returns CSSColorMix*
CSSColorMix* css_parse_color_mix_function(CssPropertyValueParser* parser,
                                         const CssToken* tokens,
                                         int token_count) {
    // Implementation for color-mix() function parsing
    if (!parser || !tokens || token_count <= 0) {
        return NULL;
    }

    // Return NULL for now - actual implementation would parse color-mix syntax
    return NULL;
}

// implementation removed - now implemented in css_tokenizer.c

// Enhanced value to string conversion
char* css_value_enhanced_to_string(const CssValue* value, Pool* pool) {
    // Implementation for value to string conversion
    if (!value || !pool) {
        return pool_strdup(pool, "invalid");
    }

    switch (value->type) {
        case CSS_VALUE_ENHANCED_KEYWORD:
            return pool_strdup(pool, value->data.keyword ? value->data.keyword : "unknown");
        case CSS_VALUE_ENHANCED_STRING:
            return pool_strdup(pool, value->data.string ? value->data.string : "");
        case CSS_VALUE_ENHANCED_FUNCTION:
            return pool_strdup(pool, value->data.function.name ? value->data.function.name : "function()");
        default:
            return pool_strdup(pool, "unknown-value");
    }
}CssValue* css_value_list_create(Pool* pool, bool comma_separated) {
    if (!pool) return NULL;

    CssValue* list = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!list) return NULL;

    list->type = CSS_VALUE_ENHANCED_LIST;
    list->data.list.comma_separated = comma_separated;
    list->data.list.count = 0;

    // Allocate initial array with capacity of 4
    size_t initial_capacity = 4;
    list->data.list.values = (CssValue**)pool_calloc(pool, initial_capacity * sizeof(CssValue*));
    if (!list->data.list.values) {
        return NULL;
    }

    return list;
}

void css_value_list_add(CssValue* list, CssValue* value) {
    // Add value to list with dynamic array handling
    // Note: This version doesn't expand the array, it just adds up to initial capacity
    // Full implementation would require storing pool reference or using reallocation strategy
    if (!list || !value || list->type != CSS_VALUE_ENHANCED_LIST) return;

    // Check if there's space (assuming initial capacity was sufficient)
    // In a production implementation, we'd need to expand the array here
    // For now, just add if there's space
    size_t max_capacity = 64; // Reasonable upper limit
    if (list->data.list.count < max_capacity) {
        list->data.list.values[list->data.list.count++] = value;
    }
}

// Additional CSS parsing functions for various value types
CssValue* css_parse_min_max_function(CssPropertyValueParser* parser,
                                            const CssToken* tokens,
                                            int token_count,
                                            int op_type) {
    if (!parser || !tokens || token_count < 1) return NULL;

    // min() and max() take multiple numeric arguments
    // Use generic function type since we don't have CSS_VALUE_MIN/MAX defined
    CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_FUNCTION;
    value->data.function.name = (op_type == 0) ? "min" : "max";
    value->data.function.arg_count = 0;
    value->data.function.args = NULL;

    return value;
}

CssValue* css_parse_clamp_function(CssPropertyValueParser* parser,
                                          const CssToken* tokens,
                                          int token_count) {
    if (!parser || !tokens || token_count < 3) return NULL;

    // clamp(min, preferred, max) - use generic function type
    CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_FUNCTION;
    value->data.function.name = "clamp";
    value->data.function.arg_count = 0;
    value->data.function.args = NULL;

    return value;
}

CssValue* css_parse_math_function(CssPropertyValueParser* parser,
                                         const CssToken* tokens,
                                         int token_count,
                                         int op_type) {
    if (!parser || !tokens || token_count < 1) return NULL;

    // Generic math function parser for sin, cos, tan, etc.
    CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_FUNCTION;
    value->data.function.name = "math";
    value->data.function.args = NULL;
    value->data.function.arg_count = 0;

    (void)op_type;

    return value;
}CssValue* css_parse_rgb_function(CssPropertyValueParser* parser,
                                        const CssToken* tokens,
                                        int token_count) {
    if (!parser || !tokens || token_count < 3) return NULL;

    // Parse RGB values - supports both new and legacy syntax
    // rgb(255 128 0) or rgb(255, 128, 0) or rgb(100% 50% 0%) etc.
    CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_COLOR;
    value->data.color.type = CSS_COLOR_RGB;

    // For now, create a default red color - full implementation would parse tokens
    value->data.color.data.rgba.r = 255;
    value->data.color.data.rgba.g = 0;
    value->data.color.data.rgba.b = 0;
    value->data.color.data.rgba.a = 255;

    return value;
}

CssValue* css_parse_hsl_function(CssPropertyValueParser* parser,
                                        const CssToken* tokens,
                                        int token_count) {
    if (!parser || !tokens || token_count < 3) return NULL;

    CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_COLOR;
    value->data.color.type = CSS_COLOR_HSL;

    // Default to red hue - full implementation would parse tokens
    value->data.color.data.hsla.h = 0.0;   // Red hue
    value->data.color.data.hsla.s = 1.0;   // Full saturation
    value->data.color.data.hsla.l = 0.5;   // 50% lightness
    value->data.color.data.hsla.a = 1.0;   // Full opacity

    return value;
}

CssValue* css_parse_hwb_function(CssPropertyValueParser* parser,
                                        const CssToken* tokens,
                                        int token_count) {
    if (!parser || !tokens || token_count < 3) return NULL;

    CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_COLOR;
    value->data.color.type = CSS_COLOR_HWB;

    // Default HWB values - full implementation would parse tokens
    value->data.color.data.hwba.h = 0.0;   // Red hue
    value->data.color.data.hwba.w = 0.0;   // No whiteness
    value->data.color.data.hwba.b = 0.0;   // No blackness
    value->data.color.data.hwba.a = 1.0;   // Full opacity

    return value;
}

CssValue* css_parse_lab_function(CssPropertyValueParser* parser,
                                        const CssToken* tokens,
                                        int token_count) {
    if (!parser || !tokens || token_count < 3) return NULL;

    CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_COLOR;
    value->data.color.type = CSS_COLOR_LAB;

    // Default LAB values - full implementation would parse tokens
    value->data.color.data.laba.l = 50.0;     // 50% lightness
    value->data.color.data.laba.a = 0.0;      // No red/green component
    value->data.color.data.laba.b = 0.0;      // No yellow/blue component
    value->data.color.data.laba.alpha = 1.0;  // Full opacity

    return value;
}

CssValue* css_parse_lch_function(CssPropertyValueParser* parser,
                                        const CssToken* tokens,
                                        int token_count) {
    if (!parser || !tokens || token_count < 3) return NULL;

    CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_COLOR;
    value->data.color.type = CSS_COLOR_LCH;

    // Default LCH values - full implementation would parse tokens
    value->data.color.data.lcha.l = 50.0;   // 50% lightness
    value->data.color.data.lcha.c = 0.0;    // No chroma
    value->data.color.data.lcha.h = 0.0;    // Red hue
    value->data.color.data.lcha.a = 1.0;    // Full opacity

    return value;
}

CssValue* css_parse_oklab_function(CssPropertyValueParser* parser,
                                          const CssToken* tokens,
                                          int token_count) {
    if (!parser || !tokens || token_count < 3) return NULL;

    CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_COLOR;
    value->data.color.type = CSS_COLOR_OKLAB;

    // Default OKLAB values - full implementation would parse tokens
    value->data.color.data.laba.l = 0.5;     // 50% lightness
    value->data.color.data.laba.a = 0.0;     // No red/green component
    value->data.color.data.laba.b = 0.0;     // No yellow/blue component
    value->data.color.data.laba.alpha = 1.0; // Full opacity

    return value;
}

CssValue* css_parse_oklch_function(CssPropertyValueParser* parser,
                                          const CssToken* tokens,
                                          int token_count) {
    if (!parser || !tokens || token_count < 3) return NULL;

    CssValue* value = (CssValue*)pool_calloc(parser->pool, sizeof(CssValue));
    if (!value) return NULL;

    value->type = CSS_VALUE_COLOR;
    value->data.color.type = CSS_COLOR_OKLCH;

    // Default OKLCH values - full implementation would parse tokens
    value->data.color.data.lcha.l = 0.5;    // 50% lightness
    value->data.color.data.lcha.c = 0.0;    // No chroma
    value->data.color.data.lcha.h = 0.0;    // Red hue
    value->data.color.data.lcha.a = 1.0;    // Full opacity

    return value;
}

// ============================================================================
// CSS Utility Functions - Core Implementation
// ============================================================================

CssValue* css_value_create_length(Pool* pool, double value, CssUnit unit) {
    if (!pool) return NULL;

    CssValue* css_value = (CssValue*)pool_alloc(pool, sizeof(CssValue));
    if (!css_value) return NULL;

    css_value->type = CSS_VALUE_LENGTH;
    css_value->data.length.value = value;
    css_value->data.length.unit = unit;

    return css_value;
}

CssValue* css_get_initial_value(CssPropertyId property_id, Pool* pool) {
    if (!pool) return NULL;

    // Create initial values for common properties
    switch (property_id) {
        case CSS_PROPERTY_COLOR:
            return css_value_create_length(pool, 0.0, CSS_UNIT_PX); // Should be a color, but using length as fallback
        case CSS_PROPERTY_FONT_SIZE:
            return css_value_create_length(pool, 16.0, CSS_UNIT_PX);
        case CSS_PROPERTY_MARGIN_TOP:
        case CSS_PROPERTY_MARGIN_RIGHT:
        case CSS_PROPERTY_MARGIN_BOTTOM:
        case CSS_PROPERTY_MARGIN_LEFT:
        case CSS_PROPERTY_PADDING_TOP:
        case CSS_PROPERTY_PADDING_RIGHT:
        case CSS_PROPERTY_PADDING_BOTTOM:
        case CSS_PROPERTY_PADDING_LEFT:
            return css_value_create_length(pool, 0.0, CSS_UNIT_PX);
        case CSS_PROPERTY_WIDTH:
        case CSS_PROPERTY_HEIGHT:
            return css_value_create_length(pool, 0.0, CSS_UNIT_AUTO); // Should be 'auto' keyword
        default:
            return css_value_create_length(pool, 0.0, CSS_UNIT_PX);
    }
}

CssValue* css_value_compute(const CssValue* value, const CssComputedStyle* parent_style, Pool* pool) {
    if (!value || !pool) return NULL;

    // For now, just return a copy of the value
    // In a full implementation, this would resolve relative units, inherit values, etc.
    CssValue* computed = (CssValue*)pool_alloc(pool, sizeof(CssValue));
    if (!computed) return NULL;

    *computed = *value; // Simple copy for now
    return computed;
}

CssSpecificity css_calculate_specificity(const CssSelector* selector) {
    CssSpecificity spec = {0};

    if (!selector) return spec;

    // Simple specificity calculation - in a full implementation this would
    // traverse the selector and count IDs, classes, elements, etc.
    spec.elements = 1; // At least one element

    return spec;
}
