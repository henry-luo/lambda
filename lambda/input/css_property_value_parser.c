#include "css_property_value_parser.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

// Forward declarations
static void css_property_value_parser_set_default_env_vars(CSSPropertyValueParser* parser);
static CSSValueEnhanced* css_parse_single_value_enhanced(CSSPropertyValueParser* parser,
                                                        const CSSTokenEnhanced* token,
                                                        const char* property_name);
static CSSValueEnhanced* css_parse_function_value(CSSPropertyValueParser* parser,
                                                 const CSSTokenEnhanced* tokens,
                                                 int token_count,
                                                 const char* property_name);
static CSSValueEnhanced* css_parse_value_list_enhanced(CSSPropertyValueParser* parser,
                                                      const CSSTokenEnhanced* tokens,
                                                      int token_count,
                                                      const char* property_name);

// Parser creation and destruction
CSSPropertyValueParser* css_property_value_parser_create(Pool* pool) {
    if (!pool) return NULL;
    
    CSSPropertyValueParser* parser = (CSSPropertyValueParser*)pool_calloc(pool, sizeof(CSSPropertyValueParser));
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
    parser->custom_properties.initial_values = (CSSValueEnhanced**)pool_alloc(pool, parser->custom_properties.capacity * sizeof(CSSValueEnhanced*));
    parser->custom_properties.inherits = (bool*)pool_alloc(pool, parser->custom_properties.capacity * sizeof(bool));
    parser->custom_properties.syntax = (const char**)pool_alloc(pool, parser->custom_properties.capacity * sizeof(char*));
    
    // Initialize environment variables
    parser->env_variables.capacity = 16;
    parser->env_variables.names = (const char**)pool_alloc(pool, parser->env_variables.capacity * sizeof(char*));
    parser->env_variables.values = (CSSValueEnhanced**)pool_alloc(pool, parser->env_variables.capacity * sizeof(CSSValueEnhanced*));
    
    // Initialize error tracking
    parser->error_capacity = 10;
    parser->error_messages = (const char**)pool_alloc(pool, parser->error_capacity * sizeof(char*));
    
    // Set default environment variables
    css_property_value_parser_set_default_env_vars(parser);
    
    return parser;
}

void css_property_value_parser_destroy(CSSPropertyValueParser* parser) {
    // Memory managed by pool, nothing to do
    (void)parser;
}

// Set default environment variables
static void css_property_value_parser_set_default_env_vars(CSSPropertyValueParser* parser) {
    if (!parser) return;
    
    // Safe area insets (for mobile devices)
    CSSValueEnhanced* zero_px = css_value_enhanced_create_length(parser->pool, 0.0, "px");
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
void css_property_value_parser_set_calc_support(CSSPropertyValueParser* parser, bool enabled) {
    if (parser) parser->allow_calc = enabled;
}

void css_property_value_parser_set_custom_props_support(CSSPropertyValueParser* parser, bool enabled) {
    if (parser) parser->allow_custom_props = enabled;
}

void css_property_value_parser_set_env_vars_support(CSSPropertyValueParser* parser, bool enabled) {
    if (parser) parser->allow_env_vars = enabled;
}

void css_property_value_parser_set_math_functions_support(CSSPropertyValueParser* parser, bool enabled) {
    if (parser) parser->allow_math_functions = enabled;
}

// Enhanced CSS Value creation utilities
CSSValueEnhanced* css_value_enhanced_create_keyword(Pool* pool, const char* keyword) {
    if (!pool || !keyword) return NULL;
    
    CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(pool, sizeof(CSSValueEnhanced));
    if (!value) return NULL;
    
    value->type = CSS_VALUE_ENHANCED_KEYWORD;
    
    // Copy keyword string
    size_t len = strlen(keyword);
    char* keyword_copy = (char*)pool_alloc(pool, len + 1);
    if (keyword_copy) {
        strcpy(keyword_copy, keyword);
        value->data.keyword = keyword_copy;
    }
    
    return value;
}

CSSValueEnhanced* css_value_enhanced_create_number(Pool* pool, double number) {
    if (!pool) return NULL;
    
    CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(pool, sizeof(CSSValueEnhanced));
    if (!value) return NULL;
    
    value->type = CSS_VALUE_ENHANCED_NUMBER;
    value->data.number = number;
    
    return value;
}

CSSValueEnhanced* css_value_enhanced_create_length(Pool* pool, double number, const char* unit) {
    if (!pool || !unit) return NULL;
    
    CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(pool, sizeof(CSSValueEnhanced));
    if (!value) return NULL;
    
    value->type = CSS_VALUE_ENHANCED_LENGTH;
    value->data.length.value = number;
    
    // Copy unit string
    size_t len = strlen(unit);
    char* unit_copy = (char*)pool_alloc(pool, len + 1);
    if (unit_copy) {
        strcpy(unit_copy, unit);
        value->data.length.unit = unit_copy;
    }
    
    return value;
}

CSSValueEnhanced* css_value_enhanced_create_string(Pool* pool, const char* string) {
    if (!pool || !string) return NULL;
    
    CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(pool, sizeof(CSSValueEnhanced));
    if (!value) return NULL;
    
    value->type = CSS_VALUE_ENHANCED_STRING;
    value->data.string = pool_strdup(pool, string);
    
    return value;
}

CSSValueEnhanced* css_value_enhanced_create_url(Pool* pool, const char* url) {
    if (!pool || !url) return NULL;
    
    CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(pool, sizeof(CSSValueEnhanced));
    if (!value) return NULL;
    
    value->type = CSS_VALUE_ENHANCED_URL;
    value->data.url = pool_strdup(pool, url);
    
    return value;
}

CSSValueEnhanced* css_value_enhanced_create_color_hex(Pool* pool, const char* hex) {
    if (!pool || !hex) return NULL;
    
    CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(pool, sizeof(CSSValueEnhanced));
    if (!value) return NULL;
    
    value->type = CSS_VALUE_ENHANCED_COLOR;
    value->data.color_hex = pool_strdup(pool, hex);
    
    return value;
}

CSSValueEnhanced* css_value_enhanced_create_unicode_range(Pool* pool, const char* range) {
    if (!pool || !range) return NULL;
    
    CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(pool, sizeof(CSSValueEnhanced));
    if (!value) return NULL;
    
    value->type = CSS_VALUE_ENHANCED_UNICODE_RANGE;
    value->data.unicode_range = pool_strdup(pool, range);
    
    return value;
}

CSSValueEnhanced* css_value_enhanced_create_percentage(Pool* pool, double percentage) {
    if (!pool) return NULL;
    
    CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(pool, sizeof(CSSValueEnhanced));
    if (!value) return NULL;
    
    value->type = CSS_VALUE_ENHANCED_PERCENTAGE;
    value->data.percentage = percentage;
    
    return value;
}

// Main value parsing function
CSSValueEnhanced* css_parse_value_enhanced(CSSPropertyValueParser* parser, 
                                          const CSSTokenEnhanced* tokens, 
                                          int token_count,
                                          const char* property_name) {
    if (!parser || !tokens || token_count <= 0) return NULL;
    
    // Handle single token cases first
    if (token_count == 1) {
        return css_parse_single_value_enhanced(parser, &tokens[0], property_name);
    }
    
    // Check for function calls
    if (tokens[0].type == CSS_TOKEN_ENHANCED_FUNCTION) {
        return css_parse_function_value(parser, tokens, token_count, property_name);
    }
    
    // Parse value list (space or comma separated)
    return css_parse_value_list_enhanced(parser, tokens, token_count, property_name);
}

// Parse single token value
static CSSValueEnhanced* css_parse_single_value_enhanced(CSSPropertyValueParser* parser,
                                                        const CSSTokenEnhanced* token,
                                                        const char* property_name) {
    if (!parser || !token) return NULL;
    
    switch (token->type) {
        case CSS_TOKEN_ENHANCED_IDENT:
            return css_value_enhanced_create_keyword(parser->pool, token->value);
            
        case CSS_TOKEN_ENHANCED_NUMBER:
            return css_value_enhanced_create_number(parser->pool, token->data.number_value);
            
        case CSS_TOKEN_ENHANCED_DIMENSION:
            return css_value_enhanced_create_length(parser->pool, token->data.dimension.value, css_unit_type_to_str(token->data.dimension.unit));
            
        case CSS_TOKEN_ENHANCED_PERCENTAGE:
            return css_value_enhanced_create_percentage(parser->pool, token->data.number_value);
            
        case CSS_TOKEN_ENHANCED_STRING:
            return css_value_enhanced_create_string(parser->pool, token->value);
            
        case CSS_TOKEN_ENHANCED_URL:
            return css_value_enhanced_create_url(parser->pool, token->value);
            
        case CSS_TOKEN_ENHANCED_HASH:
            // Handle color hex values
            return css_value_enhanced_create_color_hex(parser->pool, token->value);
            
        case CSS_TOKEN_ENHANCED_UNICODE_RANGE:
            return css_value_enhanced_create_unicode_range(parser->pool, token->value);
            
        default:
            css_property_value_parser_add_error(parser, "Unsupported token type in value");
            return NULL;
    }
}

// Parse function value
static CSSValueEnhanced* css_parse_function_value(CSSPropertyValueParser* parser,
                                                  const CSSTokenEnhanced* tokens,
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
            CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(parser->pool, sizeof(CSSValueEnhanced));
            if (value) {
                value->type = CSS_VALUE_ENHANCED_VAR;
                value->data.var_ref = var_ref;
            }
            return value;
        }
    }
    
    if (strcmp(function_name, "env") == 0 && parser->allow_env_vars) {
        CSSEnvRef* env_ref = css_parse_env_function(parser, tokens + 1, token_count - 1);
        if (env_ref) {
            CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(parser->pool, sizeof(CSSValueEnhanced));
            if (value) {
                value->type = CSS_VALUE_ENHANCED_ENV;
                value->data.env_ref = env_ref;
            }
            return value;
        }
    }
    
    if (strcmp(function_name, "attr") == 0) {
        CSSAttrRef* attr_ref = css_parse_attr_function(parser, tokens + 1, token_count - 1);
        if (attr_ref) {
            CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(parser->pool, sizeof(CSSValueEnhanced));
            if (value) {
                value->type = CSS_VALUE_ENHANCED_ATTR;
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
                CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(parser->pool, sizeof(CSSValueEnhanced));
                if (value) {
                    value->type = CSS_VALUE_ENHANCED_COLOR_MIX;
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

// Generic function parser (stub implementation)
static CSSValueEnhanced* css_parse_generic_function(CSSPropertyValueParser* parser,
                                                   const char* function_name,
                                                   const CSSTokenEnhanced* tokens,
                                                   int token_count) {
    // Stub implementation for generic function parsing
    if (!parser || !function_name || !tokens || token_count <= 0) {
        return NULL;
    }
    
    CSSValueEnhanced* value = pool_calloc(parser->pool, sizeof(CSSValueEnhanced));
    if (!value) {
        return NULL;
    }
    
    value->type = CSS_VALUE_ENHANCED_FUNCTION;
    value->data.function.name = pool_strdup(parser->pool, function_name);
    value->data.function.arguments = NULL;
    value->data.function.argument_count = 0;
    
    return value;
}

static CSSValueEnhanced* css_parse_value_list_enhanced(CSSPropertyValueParser* parser,
                                                      const CSSTokenEnhanced* tokens,
                                                      int token_count,
                                                      const char* property_name) {
    if (!parser || !tokens || token_count <= 0) return NULL;
    
    // Create a list value
    CSSValueEnhanced* list = css_value_list_create(parser->pool, false);
    if (!list) return NULL;
    
    // Parse individual values separated by commas or whitespace
    int i = 0;
    while (i < token_count) {
        // Skip whitespace and commas
        while (i < token_count && 
               (tokens[i].type == CSS_TOKEN_WHITESPACE || 
                tokens[i].type == CSS_TOKEN_ENHANCED_COMMA)) {
            i++;
        }
        
        if (i >= token_count) break;
        
        // Parse single value
        CSSValueEnhanced* value = css_parse_single_value_enhanced(parser, &tokens[i], property_name);
        if (value) {
            css_value_list_add(list, value);
        }
        
        i++;
    }
    
    return list;
}

// Parse calc() function
// Forward declaration for calc expression stub
static CSSCalcNode* css_parse_calc_expression_stub(CSSPropertyValueParser* parser,
                                                  const CSSTokenEnhanced* tokens,
                                                  int token_count);

CSSValueEnhanced* css_parse_calc_function(CSSPropertyValueParser* parser,
                                                 const CSSTokenEnhanced* tokens,
                                                 int token_count) {
    if (!parser || !tokens || token_count <= 0) return NULL;
    
    CSSCalcNode* calc_node = css_parse_calc_expression_stub(parser, tokens, token_count);
    if (!calc_node) return NULL;
    
    CSSValueEnhanced* value = (CSSValueEnhanced*)pool_calloc(parser->pool, sizeof(CSSValueEnhanced));
    if (!value) return NULL;
    
    value->type = CSS_VALUE_ENHANCED_CALC;
    value->data.calc_expression = calc_node;
    
    return value;
}

// Stub for calc expression parsing
static CSSCalcNode* css_parse_calc_expression_stub(CSSPropertyValueParser* parser,
                                                  const CSSTokenEnhanced* tokens,
                                                  int token_count) {
    // Stub implementation for calc expression parsing
    if (!parser || !tokens || token_count <= 0) {
        return NULL;
    }
    
    // Return NULL to indicate calc parsing not yet implemented
    return NULL;
}

// Parse var() function
CSSVarRef* css_parse_var_function(CSSPropertyValueParser* parser,
                                 const CSSTokenEnhanced* tokens,
                                 int token_count) {
    if (!parser || !tokens || token_count < 1) return NULL;
    
    // First token should be the variable name
    if (tokens[0].type != CSS_TOKEN_ENHANCED_IDENT) {
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
    if (token_count > 2 && tokens[1].type == CSS_TOKEN_ENHANCED_COMMA) {
        // Parse fallback value
        var_ref->fallback = css_parse_value_enhanced(parser, tokens + 2, token_count - 2, NULL);
        var_ref->has_fallback = var_ref->fallback != NULL;
    }
    
    return var_ref;
}

// Parse env() function
CSSEnvRef* css_parse_env_function(CSSPropertyValueParser* parser,
                                 const CSSTokenEnhanced* tokens,
                                 int token_count) {
    if (!parser || !tokens || token_count < 1) return NULL;
    
    // First token should be the environment variable name
    if (tokens[0].type != CSS_TOKEN_ENHANCED_IDENT) {
        css_property_value_parser_add_error(parser, "env() function requires identifier argument");
        return NULL;
    }
    
    CSSEnvRef* env_ref = (CSSEnvRef*)pool_calloc(parser->pool, sizeof(CSSEnvRef));
    if (!env_ref) return NULL;
    
    env_ref->name = tokens[0].value;
    
    // Check for fallback value
    if (token_count > 2 && tokens[1].type == CSS_TOKEN_ENHANCED_COMMA) {
        // Parse fallback value
        env_ref->fallback = css_parse_value_enhanced(parser, tokens + 2, token_count - 2, NULL);
        env_ref->has_fallback = env_ref->fallback != NULL;
    }
    
    return env_ref;
}

// Parse attr() function
CSSAttrRef* css_parse_attr_function(CSSPropertyValueParser* parser,
                                   const CSSTokenEnhanced* tokens,
                                   int token_count) {
    if (!parser || !tokens || token_count < 1) return NULL;
    
    // First token should be the attribute name
    if (tokens[0].type != CSS_TOKEN_ENHANCED_IDENT) {
        css_property_value_parser_add_error(parser, "attr() function requires identifier argument");
        return NULL;
    }
    
    CSSAttrRef* attr_ref = (CSSAttrRef*)pool_calloc(parser->pool, sizeof(CSSAttrRef));
    if (!attr_ref) return NULL;
    
    attr_ref->name = tokens[0].value;
    
    // Check for type or unit specifier
    int next_index = 1;
    if (token_count > next_index && tokens[next_index].type == CSS_TOKEN_ENHANCED_IDENT) {
        attr_ref->type_or_unit = tokens[next_index].value;
        next_index++;
    }
    
    // Check for fallback value
    if (token_count > next_index + 1 && tokens[next_index].type == CSS_TOKEN_ENHANCED_COMMA) {
        // Parse fallback value
        attr_ref->fallback = css_parse_value_enhanced(parser, tokens + next_index + 1, 
                                                     token_count - next_index - 1, NULL);
        attr_ref->has_fallback = attr_ref->fallback != NULL;
    }
    
    return attr_ref;
}

// Error handling
void css_property_value_parser_add_error(CSSPropertyValueParser* parser, const char* message) {
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

bool css_property_value_parser_has_errors(CSSPropertyValueParser* parser) {
    return parser && parser->error_count > 0;
}

void css_property_value_parser_clear_errors(CSSPropertyValueParser* parser) {
    if (parser) {
        parser->error_count = 0;
    }
}

const char** css_property_value_parser_get_errors(CSSPropertyValueParser* parser, int* count) {
    if (!parser || !count) return NULL;
    
    *count = parser->error_count;
    return parser->error_messages;
}

// Type checking utilities
bool css_value_enhanced_is_length(const CSSValueEnhanced* value) {
    return value && (value->type == CSS_VALUE_ENHANCED_LENGTH || 
                     value->type == CSS_VALUE_ENHANCED_LENGTH_PERCENTAGE);
}

bool css_value_enhanced_is_percentage(const CSSValueEnhanced* value) {
    return value && (value->type == CSS_VALUE_ENHANCED_PERCENTAGE ||
                     value->type == CSS_VALUE_ENHANCED_LENGTH_PERCENTAGE ||
                     value->type == CSS_VALUE_ENHANCED_NUMBER_PERCENTAGE);
}

bool css_value_enhanced_is_number(const CSSValueEnhanced* value) {
    return value && (value->type == CSS_VALUE_ENHANCED_NUMBER ||
                     value->type == CSS_VALUE_ENHANCED_INTEGER ||
                     value->type == CSS_VALUE_ENHANCED_NUMBER_PERCENTAGE);
}

bool css_value_enhanced_is_color(const CSSValueEnhanced* value) {
    return value && (value->type == CSS_VALUE_ENHANCED_COLOR ||
                     value->type == CSS_VALUE_ENHANCED_COLOR_MIX);
}

bool css_value_enhanced_is_keyword(const CSSValueEnhanced* value, const char* keyword) {
    return value && value->type == CSS_VALUE_ENHANCED_KEYWORD && 
           value->data.keyword && strcmp(value->data.keyword, keyword) == 0;
}

bool css_value_enhanced_is_function(const CSSValueEnhanced* value, const char* function_name) {
    return value && value->type == CSS_VALUE_ENHANCED_FUNCTION &&
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

void css_value_enhanced_print_debug(const CSSValueEnhanced* value) {
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
    
    if (value->important) {
        printf(" !important");
    }
    
    printf("\n");
}

// Missing function stubs - These should be properly implemented later

// Environment variable setter for parser
bool css_property_value_parser_set_env_variable(CSSPropertyValueParser* parser,
                                               const char* name,
                                               CSSValueEnhanced* value) {
    // Stub implementation for environment variable setting
    if (!parser || !name || !value) return false;
    // Would set environment variables for env() function resolution
    return true;
}

// Provide the implementation for the declared function that returns CSSColorMix*
CSSColorMix* css_parse_color_mix_function(CSSPropertyValueParser* parser,
                                         const CSSTokenEnhanced* tokens,
                                         int token_count) {
    // Stub implementation for color-mix() function parsing
    if (!parser || !tokens || token_count <= 0) {
        return NULL;
    }
    
    // Return NULL for now - actual implementation would parse color-mix syntax
    return NULL;
}

// implementation removed - now implemented in css_tokenizer_enhanced.c

// Enhanced value to string conversion
char* css_value_enhanced_to_string(const CSSValueEnhanced* value, Pool* pool) {
    // Stub implementation for value to string conversion
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
}

CSSValueEnhanced* css_value_list_create(Pool* pool, bool comma_separated) {
    if (!pool) return NULL;
    
    CSSValueEnhanced* list = (CSSValueEnhanced*)pool_calloc(pool, sizeof(CSSValueEnhanced));
    if (!list) return NULL;
    
    list->type = CSS_VALUE_ENHANCED_LIST;
    list->data.list.comma_separated = comma_separated;
    list->data.list.count = 0;
    list->data.list.values = NULL;
    
    return list;
}

void css_value_list_add(CSSValueEnhanced* list, CSSValueEnhanced* value) {
    // Stub implementation - would need proper dynamic array handling
    (void)list; (void)value;
}

// Additional missing function stubs for CSS parsing functions
CSSValueEnhanced* css_parse_min_max_function(CSSPropertyValueParser* parser,
                                            const CSSTokenEnhanced* tokens,
                                            int token_count,
                                            int op_type) {
    (void)parser; (void)tokens; (void)token_count; (void)op_type;
    return NULL; // Stub
}

CSSValueEnhanced* css_parse_clamp_function(CSSPropertyValueParser* parser,
                                          const CSSTokenEnhanced* tokens,
                                          int token_count) {
    (void)parser; (void)tokens; (void)token_count;
    return NULL; // Stub
}

CSSValueEnhanced* css_parse_math_function(CSSPropertyValueParser* parser,
                                         const CSSTokenEnhanced* tokens,
                                         int token_count,
                                         int op_type) {
    (void)parser; (void)tokens; (void)token_count; (void)op_type;
    return NULL; // Stub
}

CSSValueEnhanced* css_parse_rgb_function(CSSPropertyValueParser* parser,
                                        const CSSTokenEnhanced* tokens,
                                        int token_count) {
    (void)parser; (void)tokens; (void)token_count;
    return NULL; // Stub
}

CSSValueEnhanced* css_parse_hsl_function(CSSPropertyValueParser* parser,
                                        const CSSTokenEnhanced* tokens,
                                        int token_count) {
    (void)parser; (void)tokens; (void)token_count;
    return NULL; // Stub
}

CSSValueEnhanced* css_parse_hwb_function(CSSPropertyValueParser* parser,
                                        const CSSTokenEnhanced* tokens,
                                        int token_count) {
    (void)parser; (void)tokens; (void)token_count;
    return NULL; // Stub
}

CSSValueEnhanced* css_parse_lab_function(CSSPropertyValueParser* parser,
                                        const CSSTokenEnhanced* tokens,
                                        int token_count) {
    (void)parser; (void)tokens; (void)token_count;
    return NULL; // Stub
}

CSSValueEnhanced* css_parse_lch_function(CSSPropertyValueParser* parser,
                                        const CSSTokenEnhanced* tokens,
                                        int token_count) {
    (void)parser; (void)tokens; (void)token_count;
    return NULL; // Stub
}

CSSValueEnhanced* css_parse_oklab_function(CSSPropertyValueParser* parser,
                                          const CSSTokenEnhanced* tokens,
                                          int token_count) {
    (void)parser; (void)tokens; (void)token_count;
    return NULL; // Stub
}

CSSValueEnhanced* css_parse_oklch_function(CSSPropertyValueParser* parser,
                                          const CSSTokenEnhanced* tokens,
                                          int token_count) {
    (void)parser; (void)tokens; (void)token_count;
    return NULL; // Stub
}
