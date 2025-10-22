#ifndef CSS_PROPERTY_VALUE_PARSER_H
#define CSS_PROPERTY_VALUE_PARSER_H

#include "css_properties.h"
#include "css_tokenizer_enhanced.h"
#include "../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

// Enhanced CSS Value Types for CSS3+
typedef enum {
    // Basic types (from css_properties.h)
    CSS_VALUE_ENHANCED_KEYWORD = 0,
    CSS_VALUE_ENHANCED_LENGTH,
    CSS_VALUE_ENHANCED_PERCENTAGE,
    CSS_VALUE_ENHANCED_NUMBER,
    CSS_VALUE_ENHANCED_COLOR,
    CSS_VALUE_ENHANCED_STRING,
    CSS_VALUE_ENHANCED_URL,
    CSS_VALUE_ENHANCED_FUNCTION,
    CSS_VALUE_ENHANCED_IDENTIFIER,
    CSS_VALUE_ENHANCED_ANGLE,
    CSS_VALUE_ENHANCED_TIME,
    CSS_VALUE_ENHANCED_FREQUENCY,
    CSS_VALUE_ENHANCED_RESOLUTION,
    CSS_VALUE_ENHANCED_INTEGER,
    CSS_VALUE_ENHANCED_GLOBAL,
    
    // CSS3+ enhanced types
    CSS_VALUE_ENHANCED_CALC,           // calc() expressions
    CSS_VALUE_ENHANCED_VAR,            // CSS custom properties var(--name, fallback)
    CSS_VALUE_ENHANCED_ENV,            // env() environment variables
    CSS_VALUE_ENHANCED_ATTR,           // attr() attribute references
    CSS_VALUE_ENHANCED_CLAMP,          // clamp(min, preferred, max)
    CSS_VALUE_ENHANCED_MIN,            // min() function
    CSS_VALUE_ENHANCED_MAX,            // max() function
    CSS_VALUE_ENHANCED_ABS,            // abs() mathematical function
    CSS_VALUE_ENHANCED_SIGN,           // sign() mathematical function
    CSS_VALUE_ENHANCED_MOD,            // mod() mathematical function
    CSS_VALUE_ENHANCED_REM,            // rem() mathematical function
    CSS_VALUE_ENHANCED_SIN,            // sin() trigonometric function
    CSS_VALUE_ENHANCED_COS,            // cos() trigonometric function
    CSS_VALUE_ENHANCED_TAN,            // tan() trigonometric function
    CSS_VALUE_ENHANCED_ASIN,           // asin() trigonometric function
    CSS_VALUE_ENHANCED_ACOS,           // acos() trigonometric function
    CSS_VALUE_ENHANCED_ATAN,           // atan() trigonometric function
    CSS_VALUE_ENHANCED_ATAN2,          // atan2() trigonometric function
    CSS_VALUE_ENHANCED_POW,            // pow() exponential function
    CSS_VALUE_ENHANCED_SQRT,           // sqrt() square root function
    CSS_VALUE_ENHANCED_HYPOT,          // hypot() hypotenuse function
    CSS_VALUE_ENHANCED_LOG,            // log() logarithm function
    CSS_VALUE_ENHANCED_EXP,            // exp() exponential function
    CSS_VALUE_ENHANCED_ROUND,          // round() rounding function
    CSS_VALUE_ENHANCED_COLOR_MIX,      // color-mix() color blending
    CSS_VALUE_ENHANCED_COLOR_CONTRAST, // color-contrast() accessibility
    CSS_VALUE_ENHANCED_HWB,            // hwb() color space
    CSS_VALUE_ENHANCED_LAB,            // lab() color space
    CSS_VALUE_ENHANCED_LCH,            // lch() color space
    CSS_VALUE_ENHANCED_OKLAB,          // oklab() color space
    CSS_VALUE_ENHANCED_OKLCH,          // oklch() color space
    CSS_VALUE_ENHANCED_LIGHT_DARK,     // light-dark() color scheme
    CSS_VALUE_ENHANCED_LAYER,          // layer() function
    CSS_VALUE_ENHANCED_SUPPORTS,       // supports() query
    CSS_VALUE_ENHANCED_SELECTOR,       // selector() function
    CSS_VALUE_ENHANCED_MEDIA,          // media() query function
    CSS_VALUE_ENHANCED_CONTAINER,      // container() query
    CSS_VALUE_ENHANCED_STYLE,          // style() query
    CSS_VALUE_ENHANCED_FONT_TECH,      // font-tech() font feature
    CSS_VALUE_ENHANCED_FONT_FORMAT,    // font-format() font feature
    CSS_VALUE_ENHANCED_GRID_TEMPLATE,  // Grid template areas
    CSS_VALUE_ENHANCED_FIT_CONTENT,    // fit-content() sizing
    CSS_VALUE_ENHANCED_MINMAX,         // minmax() grid function
    CSS_VALUE_ENHANCED_REPEAT,         // repeat() grid function
    CSS_VALUE_ENHANCED_COUNTER,        // counter() function
    CSS_VALUE_ENHANCED_COUNTERS,       // counters() function
    CSS_VALUE_ENHANCED_SYMBOLS,        // symbols() function
    CSS_VALUE_ENHANCED_IMAGE_SET,      // image-set() responsive images
    CSS_VALUE_ENHANCED_CROSS_FADE,     // cross-fade() image blending
    CSS_VALUE_ENHANCED_ELEMENT,        // element() function
    CSS_VALUE_ENHANCED_PAINT,          // paint() worklet function
    
    // Composite types
    CSS_VALUE_ENHANCED_LIST,           // Space/comma separated values
    CSS_VALUE_ENHANCED_SEQUENCE,       // Ordered sequence of values
    CSS_VALUE_ENHANCED_OPTIONAL,       // Optional value wrapper
    CSS_VALUE_ENHANCED_ALTERNATIVES,   // One of multiple alternatives
    
    // Type constraints
    CSS_VALUE_ENHANCED_LENGTH_PERCENTAGE, // <length-percentage>
    CSS_VALUE_ENHANCED_FREQUENCY_PERCENTAGE, // <frequency-percentage>
    CSS_VALUE_ENHANCED_ANGLE_PERCENTAGE,  // <angle-percentage>
    CSS_VALUE_ENHANCED_TIME_PERCENTAGE,   // <time-percentage>
    CSS_VALUE_ENHANCED_NUMBER_PERCENTAGE, // <number-percentage>
    
    CSS_VALUE_ENHANCED_UNKNOWN = 999
} CSSValueEnhancedType;

// Calc() expression operators
typedef enum {
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
} CSSCalcOperator;

// Calc() expression node
typedef struct CSSCalcNode {
    CSSCalcOperator operator;
    CSSValueEnhancedType value_type;
    
    // Value data
    union {
        double number;
        struct {
            double value;
            const char* unit;
        } length;
        double percentage;
        const char* identifier;
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
    CSSValueEnhancedType type;
    
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
CSSCalcNode* css_parse_calc_expression(CSSPropertyValueParser* parser, 
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

// Unit conversion and normalization
double css_value_enhanced_to_pixels(const CSSValueEnhanced* value, double base_font_size, double viewport_width, double viewport_height);
CSSValueEnhanced* css_value_enhanced_normalize_units(const CSSValueEnhanced* value, Pool* pool);

// Error handling
void css_property_value_parser_add_error(CSSPropertyValueParser* parser, const char* message);
bool css_property_value_parser_has_errors(CSSPropertyValueParser* parser);
void css_property_value_parser_clear_errors(CSSPropertyValueParser* parser);
const char** css_property_value_parser_get_errors(CSSPropertyValueParser* parser, int* count);

// Debug utilities
void css_value_enhanced_print_debug(const CSSValueEnhanced* value);
const char* css_value_enhanced_type_to_string(CSSValueEnhancedType type);

#ifdef __cplusplus
}
#endif

#endif // CSS_PROPERTY_VALUE_PARSER_H