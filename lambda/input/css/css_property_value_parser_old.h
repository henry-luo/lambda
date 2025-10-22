#ifndef CSS_PROPERTY_VALUE_PARSER_H
#define CSS_PROPERTY_VALUE_PARSER_H

#include "css_parser.h"
#include "css_style.h"
#include "../../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CSS Property Value Parser
 * 
 * This file contains functions for parsing CSS property values.
 * Most types are now defined in css_parser.h and css_style.h.
 */

// Compatibility aliases for legacy code
typedef CssValue CSSValueParsed;
typedef CssCalcOperator CSSCalcOperator;
typedef CssCalcNode CSSCalcNode;
typedef CssCalcToken CSSCalcToken;

// Property value parsing functions (main implementations in css_parser.h)
CssValue* css_parse_property_value(const char* property_name, CssTokenStream* stream, Pool* pool);
CssValue* css_parse_color_value(CssTokenStream* stream, Pool* pool);
CssValue* css_parse_length_value(CssTokenStream* stream, Pool* pool);
CssValue* css_parse_number_value(CssTokenStream* stream, Pool* pool);
CssValue* css_parse_percentage_value(CssTokenStream* stream, Pool* pool);
CssValue* css_parse_string_value(CssTokenStream* stream, Pool* pool);
CssValue* css_parse_url_value(CssTokenStream* stream, Pool* pool);
CssValue* css_parse_function_value(CssTokenStream* stream, Pool* pool);

// Calc expression parsing (implementations in css_parser.h)
CssCalcNode* css_parse_calc_ast(CssCalcToken* tokens, size_t count, Pool* pool);
CssValue* css_evaluate_calc_expression(CssCalcNode* ast, Pool* pool);
bool css_validate_calc_expression(CssCalcToken* tokens, size_t count);

// Value validation
bool css_validate_property_value(CssPropertyId property_id, const CssValue* value);
bool css_value_matches_property_type(const CssValue* value, CssPropertyId property_id);

// Value conversion utilities
CssValue* css_convert_value_to_canonical(const CssValue* value, Pool* pool);
double css_resolve_relative_units(const CssValue* value, double font_size, double viewport_size);

// Shorthand property expansion
CssDeclaration** css_expand_shorthand(CssPropertyId shorthand_id, const CssValue* shorthand_value, 
                                     Pool* pool, size_t* declaration_count);

// Legacy compatibility
#define CSSValueTypeEnhanced CssValueType
#define CSS_VALUE_ENHANCED_KEYWORD CSS_VALUE_KEYWORD
#define CSS_VALUE_ENHANCED_LENGTH CSS_VALUE_LENGTH
#define CSS_VALUE_ENHANCED_COLOR CSS_VALUE_COLOR
#define CSS_VALUE_ENHANCED_NUMBER CSS_VALUE_NUMBER
        struct CSSCalcNode** operands;  // For operator nodes
    } data;
    
    int operand_count;
    struct CSSCalcNode* next;  // For lists
} CSSCalcNode;

// Custom property (CSS variable) reference
typedef struct CSSVarRef {
    const char* name;           // Variable name (without --)
    struct CSSValueEnhanced* fallback; // Fallback value
    bool has_fallback;
} CSSVarRef;

// Environment variable reference
typedef struct CSSEnvRef {
    const char* name;           // Environment variable name
    struct CSSValueEnhanced* fallback; // Fallback value
    bool has_fallback;
} CSSEnvRef;

// Attribute reference
typedef struct CSSAttrRef {
    const char* name;           // Attribute name
    const char* type_or_unit;   // Type or unit
    struct CSSValueEnhanced* fallback; // Fallback value
    bool has_fallback;
} CSSAttrRef;

// Color mixing function
typedef struct CSSColorMix {
    const char* color_space;    // Color space (srgb, hsl, hwb, etc.)
    struct CSSValueEnhanced* color1;
    struct CSSValueEnhanced* color2;
    double percentage1;         // Mixing percentage for color1
    double percentage2;         // Mixing percentage for color2
    bool has_percentages;
} CSSColorMix;

// Enhanced CSS Value Structure
typedef struct CSSValueEnhanced {
    CSSValueTypeEnhanced type;
    
    union {
        // Basic types
        const char* keyword;
        double number;
        struct {
            double value;
            const char* unit;
        } length;
        double percentage;
        const char* string;
        const char* url;
        const char* identifier;
        
        // Enhanced types
        CSSCalcNode* calc_expression;
        CSSVarRef* var_ref;
        CSSEnvRef* env_ref;
        CSSAttrRef* attr_ref;
        CSSColorMix* color_mix;
        
        // Function calls
        struct {
            const char* name;
            struct CSSValueEnhanced** arguments;
            int argument_count;
        } function;
        
        // Composite types
        struct {
            struct CSSValueEnhanced** values;
            int count;
            bool comma_separated;  // true for comma, false for space
        } list;
        
        // Alternative values (for fallbacks)
        struct {
            struct CSSValueEnhanced** alternatives;
            int count;
        } alternatives;
        
        // Additional CSS3+ value types
        const char* color_hex;      // Hex color values
        const char* unicode_range;  // Unicode range values
        
    } data;
    
    // Metadata
    bool important;
    bool computed;              // Whether value has been computed
    double computed_value;      // Computed numeric value (for calc)
    const char* computed_unit;  // Computed unit
    
    struct CSSValueEnhanced* next; // For value lists
} CSSValueEnhanced;

// Enhanced Property Value Parser
typedef struct CSSPropertyValueParser {
    Pool* pool;
    
    // Parser options
    bool allow_calc;            // Allow calc() expressions
    bool allow_custom_props;    // Allow CSS custom properties
    bool allow_env_vars;        // Allow env() variables
    bool allow_math_functions;  // Allow sin(), cos(), etc.
    bool allow_color_functions; // Allow color-mix(), etc.
    
    // Custom property registry
    struct {
        const char** names;
        CSSValueEnhanced** initial_values;
        bool* inherits;
        const char** syntax;    // Property syntax string
        int count;
        int capacity;
    } custom_properties;
    
    // Environment variables
    struct {
        const char** names;
        CSSValueEnhanced** values;
        int count;
        int capacity;
    } env_variables;
    
    // Error tracking
    const char** error_messages;
    int error_count;
    int error_capacity;
    
} CSSPropertyValueParser;

// Parser creation and configuration
CSSPropertyValueParser* css_property_value_parser_create(Pool* pool);
void css_property_value_parser_destroy(CSSPropertyValueParser* parser);

void css_property_value_parser_set_calc_support(CSSPropertyValueParser* parser, bool enabled);
void css_property_value_parser_set_custom_props_support(CSSPropertyValueParser* parser, bool enabled);
void css_property_value_parser_set_env_vars_support(CSSPropertyValueParser* parser, bool enabled);
void css_property_value_parser_set_math_functions_support(CSSPropertyValueParser* parser, bool enabled);

// Main parsing functions
CSSValueEnhanced* css_parse_value_enhanced(CSSPropertyValueParser* parser, 
                                          const CSSTokenEnhanced* tokens, 
                                          int token_count,
                                          const char* property_name);

CSSValueEnhanced* css_parse_declaration_value_enhanced(CSSPropertyValueParser* parser,
                                                      const char* property,
                                                      const char* value_text);

// Specific value type parsers
CSSCalcNode* css_parse_calc_expression_enhanced(CSSPropertyValueParser* parser, 
                                      const CSSTokenEnhanced* tokens, 
                                      int token_count);

CSSVarRef* css_parse_var_function(CSSPropertyValueParser* parser,
                                 const CSSTokenEnhanced* tokens,
                                 int token_count);

CSSEnvRef* css_parse_env_function(CSSPropertyValueParser* parser,
                                 const CSSTokenEnhanced* tokens,
                                 int token_count);

CSSAttrRef* css_parse_attr_function(CSSPropertyValueParser* parser,
                                   const CSSTokenEnhanced* tokens,
                                   int token_count);

CSSColorMix* css_parse_color_mix_function(CSSPropertyValueParser* parser,
                                         const CSSTokenEnhanced* tokens,
                                         int token_count);

// Value validation and computation
bool css_value_enhanced_validate(const CSSValueEnhanced* value, const char* property_name);
CSSValueEnhanced* css_value_enhanced_compute(CSSPropertyValueParser* parser, 
                                            const CSSValueEnhanced* value,
                                            const char* property_name);

double css_calc_node_evaluate(const CSSCalcNode* node, CSSPropertyValueParser* parser);
bool css_calc_node_validate_units(const CSSCalcNode* node);

// Custom property management
bool css_property_value_parser_register_custom_property(CSSPropertyValueParser* parser,
                                                       const char* name,
                                                       const char* syntax,
                                                       CSSValueEnhanced* initial_value,
                                                       bool inherits);

CSSValueEnhanced* css_property_value_parser_get_custom_property(CSSPropertyValueParser* parser,
                                                               const char* name);

bool css_property_value_parser_set_custom_property(CSSPropertyValueParser* parser,
                                                   const char* name,
                                                   CSSValueEnhanced* value);

// Environment variable management
bool css_property_value_parser_set_env_variable(CSSPropertyValueParser* parser,
                                               const char* name,
                                               CSSValueEnhanced* value);

CSSValueEnhanced* css_property_value_parser_get_env_variable(CSSPropertyValueParser* parser,
                                                            const char* name);

// Value creation functions
CSSValueEnhanced* css_value_enhanced_create_keyword(Pool* pool, const char* keyword);
CSSValueEnhanced* css_value_enhanced_create_number(Pool* pool, double number);
CSSValueEnhanced* css_value_enhanced_create_length(Pool* pool, double number, const char* unit);
CSSValueEnhanced* css_value_enhanced_create_percentage(Pool* pool, double percentage);
CSSValueEnhanced* css_value_enhanced_create_string(Pool* pool, const char* string);
CSSValueEnhanced* css_value_enhanced_create_url(Pool* pool, const char* url);
CSSValueEnhanced* css_value_enhanced_create_color_hex(Pool* pool, const char* hex);
CSSValueEnhanced* css_value_enhanced_create_unicode_range(Pool* pool, const char* range);

// Serialization and formatting
char* css_value_enhanced_to_string(const CSSValueEnhanced* value, Pool* pool);
char* css_calc_node_to_string(const CSSCalcNode* node, Pool* pool);

// CSS Value List utilities
CSSValueEnhanced* css_value_list_create(Pool* pool, bool comma_separated);
void css_value_list_add(CSSValueEnhanced* list, CSSValueEnhanced* value);
int css_value_list_length(const CSSValueEnhanced* list);
CSSValueEnhanced* css_value_list_get(const CSSValueEnhanced* list, int index);

// Type checking utilities
bool css_value_enhanced_is_length(const CSSValueEnhanced* value);
bool css_value_enhanced_is_percentage(const CSSValueEnhanced* value);
bool css_value_enhanced_is_number(const CSSValueEnhanced* value);
bool css_value_enhanced_is_color(const CSSValueEnhanced* value);
bool css_value_enhanced_is_keyword(const CSSValueEnhanced* value, const char* keyword);
bool css_value_enhanced_is_function(const CSSValueEnhanced* value, const char* function_name);

// CSS Function parsing
CSSValueEnhanced* css_parse_calc_function(CSSPropertyValueParser* parser,
                                         const CSSTokenEnhanced* tokens,
                                         int token_count);
CSSValueEnhanced* css_parse_min_max_function(CSSPropertyValueParser* parser,
                                            const CSSTokenEnhanced* tokens,
                                            int token_count,
                                            int op_type);
CSSValueEnhanced* css_parse_clamp_function(CSSPropertyValueParser* parser,
                                          const CSSTokenEnhanced* tokens,
                                          int token_count);
CSSValueEnhanced* css_parse_math_function(CSSPropertyValueParser* parser,
                                         const CSSTokenEnhanced* tokens,
                                         int token_count,
                                         int op_type);
CSSValueEnhanced* css_parse_rgb_function(CSSPropertyValueParser* parser,
                                        const CSSTokenEnhanced* tokens,
                                        int token_count);
CSSValueEnhanced* css_parse_hsl_function(CSSPropertyValueParser* parser,
                                        const CSSTokenEnhanced* tokens,
                                        int token_count);
CSSValueEnhanced* css_parse_hwb_function(CSSPropertyValueParser* parser,
                                        const CSSTokenEnhanced* tokens,
                                        int token_count);
CSSValueEnhanced* css_parse_lab_function(CSSPropertyValueParser* parser,
                                        const CSSTokenEnhanced* tokens,
                                        int token_count);
CSSValueEnhanced* css_parse_lch_function(CSSPropertyValueParser* parser,
                                        const CSSTokenEnhanced* tokens,
                                        int token_count);
CSSValueEnhanced* css_parse_oklab_function(CSSPropertyValueParser* parser,
                                          const CSSTokenEnhanced* tokens,
                                          int token_count);
CSSValueEnhanced* css_parse_oklch_function(CSSPropertyValueParser* parser,
                                          const CSSTokenEnhanced* tokens,
                                          int token_count);
#ifdef __cplusplus
}
#endif

#endif // CSS_PROPERTY_VALUE_PARSER_H
