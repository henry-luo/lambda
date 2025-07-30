#include "input.h"
#include "input-common.h"
#include <string.h>
#include <stdlib.h>

// math parser for latex math, typst math, and ascii math
// produces syntax tree of nested <expr op:...> elements

// math flavor types
typedef enum {
    MATH_FLAVOR_LATEX,
    MATH_FLAVOR_TYPST, 
    MATH_FLAVOR_ASCII
} MathFlavor;

// forward declarations
static Item parse_math_expression(Input *input, const char **math, MathFlavor flavor);
static Item parse_addition_expression(Input *input, const char **math, MathFlavor flavor);
static Item parse_multiplication_expression(Input *input, const char **math, MathFlavor flavor);
static Item parse_power_expression(Input *input, const char **math, MathFlavor flavor);
static Item parse_primary_with_postfix(Input *input, const char **math, MathFlavor flavor);
static Item parse_math_primary(Input *input, const char **math, MathFlavor flavor);
static Item parse_latex_frac(Input *input, const char **math);
static Item parse_latex_sqrt(Input *input, const char **math);
static Item parse_latex_superscript(Input *input, const char **math, Item base);
static Item parse_latex_subscript(Input *input, const char **math, Item base);
static Item parse_latex_command(Input *input, const char **math);
static Item parse_latex_function(Input *input, const char **math, const char* func_name);
static Item parse_typst_power(Input *input, const char **math, MathFlavor flavor, Item base);
static Item parse_typst_fraction(Input *input, const char **math, MathFlavor flavor);
// Forward declarations for function call parsing
static Item parse_function_call(Input *input, const char **math, MathFlavor flavor, const char* func_name);
static Item parse_ascii_power(Input *input, const char **math, MathFlavor flavor, Item base);
static Item parse_math_number(Input *input, const char **math);
static Item parse_math_identifier(Input *input, const char **math);
static Item create_binary_expr(Input *input, const char* op_name, Item left, Item right);
static void skip_math_whitespace(const char **math);

// New enhanced function declarations
static Item parse_latex_abs(Input *input, const char **math);
static Item parse_latex_ceil_floor(Input *input, const char **math, const char* func_name);
static Item parse_prime_notation(Input *input, const char **math, Item base);
static Item parse_number_set(Input *input, const char **math, const char* set_name);
static Item parse_set_operation(Input *input, const char **math, const char* op_name);
static Item parse_logic_operator(Input *input, const char **math, const char* op_name);

// NEW: Additional mathematical constructs
static Item parse_latex_binomial(Input *input, const char **math);
static Item parse_latex_derivative(Input *input, const char **math);
static Item parse_latex_vector(Input *input, const char **math);
static Item parse_latex_accent(Input *input, const char **math, const char* accent_type);
static Item parse_latex_arrow(Input *input, const char **math, const char* arrow_type);
static Item parse_latex_overunder(Input *input, const char **math, const char* construct_type);

static bool is_number_set(const char* cmd);
static bool is_set_operation(const char* cmd);
static bool is_logic_operator(const char* cmd);
static bool is_binomial_cmd(const char* cmd);
static bool is_derivative_cmd(const char* cmd);
static bool is_vector_cmd(const char* cmd);
static bool is_accent_cmd(const char* cmd);
static bool is_arrow_cmd(const char* cmd);

// use common utility functions from input.c and input-common.c
#define create_math_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element

// skip whitespace helper
static void skip_math_whitespace(const char **math) {
    skip_common_whitespace(math);
}

// parse a number (integer or float)
static Item parse_math_number(Input *input, const char **math) {
    StrBuf* sb = input->sb;
    strbuf_full_reset(sb);
    
    // handle negative sign
    bool is_negative = false;
    if (**math == '-') {
        is_negative = true;
        (*math)++;
    }
    
    // parse digits before decimal point
    while (**math && isdigit(**math)) {
        strbuf_append_char(sb, **math);
        (*math)++;
    }
    
    bool is_float = false;
    // parse decimal point and digits after
    if (**math == '.') {
        is_float = true;
        strbuf_append_char(sb, **math);
        (*math)++;
        while (**math && isdigit(**math)) {
            strbuf_append_char(sb, **math);
            (*math)++;
        }
    }
    
    if (sb->length <= sizeof(uint32_t)) {
        strbuf_full_reset(sb);
        return ITEM_ERROR;
    }
    
    String *num_string = (String*)sb->str;
    num_string->len = sb->length - sizeof(uint32_t);
    num_string->ref_cnt = 0;
    
    // Convert to proper Lambda number
    Item result;
    if (is_float) {
        // Parse as float
        double value = strtod(num_string->chars, NULL);
        if (is_negative) value = -value;
        result = push_d(value);
    } else {
        // Parse as integer
        long value = strtol(num_string->chars, NULL, 10);
        if (is_negative) value = -value;
        
        // Use appropriate integer type based on size
        if (value >= INT32_MIN && value <= INT32_MAX) {
            result = i2it((int)value);
        } else {
            result = push_l(value);
        }
    }
    
    strbuf_full_reset(sb);
    return result;
}

// parse identifier/variable name as symbol
static Item parse_math_identifier(Input *input, const char **math) {
    StrBuf* sb = input->sb;
    strbuf_full_reset(sb);
    
    // parse letters and digits
    while (**math && (isalpha(**math) || isdigit(**math))) {
        strbuf_append_char(sb, **math);
        (*math)++;
    }
    
    // Check if we have valid content (same pattern as command parsing)
    if (sb->length <= sizeof(uint32_t)) {
        strbuf_full_reset(sb);
        return ITEM_ERROR;
    }
    
    String *id_string = (String*)sb->str;
    id_string->len = sb->length - sizeof(uint32_t);
    
    // Create a proper copy of the identifier string to avoid buffer corruption
    String* id_copy = input_create_string(input, id_string->chars);
    if (!id_copy) {
        strbuf_full_reset(sb);
        return ITEM_ERROR;
    }
    
    // Create symbol from the copied string
    Item symbol_item = y2it(id_copy);
    
    // Now we can safely reset the buffer since we have a copy
    strbuf_full_reset(sb);
    
    return symbol_item;
}

// parse latex fraction \frac{numerator}{denominator}
static Item parse_latex_frac(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // expect opening brace for numerator
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse numerator
    Item numerator = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (numerator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    skip_math_whitespace(math);
    
    // expect opening brace for denominator
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse denominator
    Item denominator = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (denominator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create fraction expression element
    Element* frac_element = create_math_element(input, "frac");
    if (!frac_element) {
        return ITEM_ERROR;
    }
    
    // add numerator and denominator as children (no op attribute needed)
    list_push((List*)frac_element, numerator);
    list_push((List*)frac_element, denominator);
    
    // set content length
    ((TypeElmt*)frac_element->type)->content_length = ((List*)frac_element)->length;
    
    return (Item)frac_element;
}

// parse latex square root \sqrt{expression}
static Item parse_latex_sqrt(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // expect opening brace
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse expression inside sqrt
    Item inner_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (inner_expr == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create sqrt expression element
    Element* sqrt_element = create_math_element(input, "sqrt");
    if (!sqrt_element) {
        return ITEM_ERROR;
    }
    
    // add inner expression as child (no op attribute needed)
    list_push((List*)sqrt_element, inner_expr);
    
    // set content length
    ((TypeElmt*)sqrt_element->type)->content_length = ((List*)sqrt_element)->length;
    
    return (Item)sqrt_element;
}

// parse latex superscript ^{expression}
static Item parse_latex_superscript(Input *input, const char **math, Item base) {
    skip_math_whitespace(math);
    
    Item exponent;
    if (**math == '{') {
        // braced superscript ^{expr}
        (*math)++; // skip {
        exponent = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (exponent == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        if (**math != '}') {
            return ITEM_ERROR;
        }
        (*math)++; // skip }
    } else {
        // single character superscript ^x
        exponent = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
        if (exponent == ITEM_ERROR) {
            return ITEM_ERROR;
        }
    }
    
    // create power expression element
    Element* pow_element = create_math_element(input, "pow");
    if (!pow_element) {
        return ITEM_ERROR;
    }
    
    // add base and exponent as children (no op attribute needed)
    list_push((List*)pow_element, base);
    list_push((List*)pow_element, exponent);
    
    // set content length
    ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;
    
    return (Item)pow_element;
}

// parse latex subscript _{expression}
static Item parse_latex_subscript(Input *input, const char **math, Item base) {
    skip_math_whitespace(math);
    
    Item subscript;
    if (**math == '{') {
        // braced subscript _{expr}
        (*math)++; // skip {
        subscript = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (subscript == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        if (**math != '}') {
            return ITEM_ERROR;
        }
        (*math)++; // skip }
    } else {
        // single character subscript _x
        subscript = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
        if (subscript == ITEM_ERROR) {
            return ITEM_ERROR;
        }
    }
    
    // create subscript expression element
    Element* sub_element = create_math_element(input, "sub");
    if (!sub_element) {
        return ITEM_ERROR;
    }
    
    // add base and subscript as children (no op attribute needed)
    list_push((List*)sub_element, base);
    list_push((List*)sub_element, subscript);
    
    // set content length
    ((TypeElmt*)sub_element->type)->content_length = ((List*)sub_element)->length;
    
    return (Item)sub_element;
}

// Forward declarations for advanced LaTeX features
static Item parse_latex_sum_or_prod(Input *input, const char **math, const char* op_name);
static Item parse_latex_integral(Input *input, const char **math);
static Item parse_latex_limit(Input *input, const char **math);
static Item parse_latex_matrix(Input *input, const char **math, const char* matrix_type);
static Item parse_latex_cases(Input *input, const char **math);
static Item parse_latex_equation(Input *input, const char **math);
static Item parse_latex_align(Input *input, const char **math);
static Item parse_latex_aligned(Input *input, const char **math);
static Item parse_latex_gather(Input *input, const char **math);

// parse latex command starting with backslash
static Item parse_latex_command(Input *input, const char **math) {
    if (**math != '\\') {
        return ITEM_ERROR;
    }
    
    // Check for \begin{environment} syntax first
    if (strncmp(*math, "\\begin{", 7) == 0) {
        const char* env_start = *math + 7;
        const char* env_end = env_start;
        
        // Find the environment name
        while (*env_end && *env_end != '}') {
            env_end++;
        }
        
        if (*env_end == '}') {
            size_t env_len = env_end - env_start;
            
            // Check for matrix environments
            if (env_len == 6 && strncmp(env_start, "matrix", 6) == 0) {
                return parse_latex_matrix(input, math, "matrix");
            } else if (env_len == 7 && strncmp(env_start, "pmatrix", 7) == 0) {
                return parse_latex_matrix(input, math, "pmatrix");
            } else if (env_len == 7 && strncmp(env_start, "bmatrix", 7) == 0) {
                return parse_latex_matrix(input, math, "bmatrix");
            } else if (env_len == 7 && strncmp(env_start, "vmatrix", 7) == 0) {
                return parse_latex_matrix(input, math, "vmatrix");
            } else if (env_len == 8 && strncmp(env_start, "Vmatrix", 8) == 0) {
                return parse_latex_matrix(input, math, "Vmatrix");
            } else if (env_len == 11 && strncmp(env_start, "smallmatrix", 11) == 0) {
                return parse_latex_matrix(input, math, "smallmatrix");
            } else if (env_len == 5 && strncmp(env_start, "cases", 5) == 0) {
                return parse_latex_cases(input, math);
            } else if (env_len == 8 && strncmp(env_start, "equation", 8) == 0) {
                return parse_latex_equation(input, math);
            } else if (env_len == 5 && strncmp(env_start, "align", 5) == 0) {
                return parse_latex_align(input, math);
            } else if (env_len == 7 && strncmp(env_start, "aligned", 7) == 0) {
                return parse_latex_aligned(input, math);
            } else if (env_len == 6 && strncmp(env_start, "gather", 6) == 0) {
                return parse_latex_gather(input, math);
            }
            
            // For unknown environments, try to parse as generic environment
            printf("WARNING: Unknown LaTeX environment: ");
            fwrite(env_start, 1, env_len, stdout);
            printf("\n");
        }
    }
    
    (*math)++; // skip backslash
    
    // parse command name
    StrBuf* sb = input->sb;
    strbuf_full_reset(sb);
    
    while (**math && isalpha(**math)) {
        strbuf_append_char(sb, **math);
        (*math)++;
    }
    
    if (sb->length <= sizeof(uint32_t)) {
        strbuf_full_reset(sb);
        printf("ERROR: Empty or invalid LaTeX command\n");
        return ITEM_ERROR;
    }
    
    String *cmd_string = (String*)sb->str;
    cmd_string->len = sb->length - sizeof(uint32_t);
    cmd_string->ref_cnt = 0;
    
    // Handle specific commands
    if (strcmp(cmd_string->chars, "frac") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_frac(input, math);
    } else if (strcmp(cmd_string->chars, "sqrt") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sqrt(input, math);
    } else if (strcmp(cmd_string->chars, "sum") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sum_or_prod(input, math, "sum");
    } else if (strcmp(cmd_string->chars, "prod") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sum_or_prod(input, math, "prod");
    } else if (strcmp(cmd_string->chars, "int") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_integral(input, math);
    } else if (strcmp(cmd_string->chars, "lim") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_limit(input, math);
    } else if (strcmp(cmd_string->chars, "matrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "matrix");
    } else if (strcmp(cmd_string->chars, "pmatrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "pmatrix");
    } else if (strcmp(cmd_string->chars, "bmatrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "bmatrix");
    } else if (strcmp(cmd_string->chars, "vmatrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "vmatrix");
    } else if (strcmp(cmd_string->chars, "Vmatrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "Vmatrix");
    } else if (strcmp(cmd_string->chars, "smallmatrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "smallmatrix");
    } else if (strcmp(cmd_string->chars, "cases") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_cases(input, math);
    } else if (strcmp(cmd_string->chars, "left") == 0) {
        strbuf_full_reset(sb);
        // Handle \left| for absolute value
        skip_math_whitespace(math);
        if (**math == '|') {
            (*math)++; // skip |
            return parse_latex_abs(input, math);
        }
        // For other \left delimiters, treat as symbol for now
        return y2it("left");
    } else if (strcmp(cmd_string->chars, "abs") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_abs(input, math);
    } else if (strcmp(cmd_string->chars, "lceil") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_ceil_floor(input, math, "ceil");
    } else if (strcmp(cmd_string->chars, "lfloor") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_ceil_floor(input, math, "floor");
    } else if (is_number_set(cmd_string->chars)) {
        strbuf_full_reset(sb);
        return parse_number_set(input, math, cmd_string->chars);
    } else if (is_set_operation(cmd_string->chars)) {
        const char* op_name = cmd_string->chars;
        strbuf_full_reset(sb);
        return parse_set_operation(input, math, op_name);
    } else if (is_logic_operator(cmd_string->chars)) {
        const char* op_name = cmd_string->chars;
        strbuf_full_reset(sb);
        return parse_logic_operator(input, math, op_name);
    } else if (is_binomial_cmd(cmd_string->chars)) {
        strbuf_full_reset(sb);
        return parse_latex_binomial(input, math);
    } else if (is_derivative_cmd(cmd_string->chars)) {
        strbuf_full_reset(sb);
        return parse_latex_derivative(input, math);
    } else if (is_vector_cmd(cmd_string->chars)) {
        strbuf_full_reset(sb);
        return parse_latex_vector(input, math);
    } else if (is_accent_cmd(cmd_string->chars)) {
        const char* accent_type = cmd_string->chars;
        strbuf_full_reset(sb);
        return parse_latex_accent(input, math, accent_type);
    } else if (is_arrow_cmd(cmd_string->chars)) {
        const char* arrow_type = cmd_string->chars;
        strbuf_full_reset(sb);
        return parse_latex_arrow(input, math, arrow_type);
    } else if (strcmp(cmd_string->chars, "overline") == 0 || strcmp(cmd_string->chars, "underline") == 0 ||
               strcmp(cmd_string->chars, "overbrace") == 0 || strcmp(cmd_string->chars, "underbrace") == 0) {
        const char* construct_type = cmd_string->chars;
        strbuf_full_reset(sb);
        return parse_latex_overunder(input, math, construct_type);
    } else if (strcmp(cmd_string->chars, "infty") == 0) {
        strbuf_full_reset(sb);
        Element* infty_element = create_math_element(input, "infty");
        if (!infty_element) {
            return ITEM_ERROR;
        }
        add_attribute_to_element(input, infty_element, "symbol", "∞");
        ((TypeElmt*)infty_element->type)->content_length = ((List*)infty_element)->length;
        return (Item)infty_element;
    } else if (strcmp(cmd_string->chars, "partial") == 0) {
        strbuf_full_reset(sb);
        Element* partial_element = create_math_element(input, "partial");
        if (!partial_element) {
            return ITEM_ERROR;
        }
        add_attribute_to_element(input, partial_element, "symbol", "∂");
        ((TypeElmt*)partial_element->type)->content_length = ((List*)partial_element)->length;
        return (Item)partial_element;
    } else if (is_trig_function(cmd_string->chars) || is_log_function(cmd_string->chars)) {
        const char* func_name = cmd_string->chars;
        strbuf_full_reset(sb);
        return parse_latex_function(input, math, func_name);
    } else if (is_greek_letter(cmd_string->chars) || is_math_operator(cmd_string->chars)) {
        // Greek letters and math operators are treated as symbols
        Item symbol_item = y2it(cmd_string->chars);
        strbuf_full_reset(sb);
        return symbol_item;
    }
    
    // for other commands, return as symbol for now
    // Unknown LaTeX command
    Item symbol_item = y2it(cmd_string->chars);
    strbuf_full_reset(sb);
    return symbol_item;
}

// parse typst power expression with ^ operator  
static Item parse_typst_power(Input *input, const char **math, MathFlavor flavor, Item base) {
    // In Typst, power is x^y
    skip_math_whitespace(math);
    
    Item exponent = parse_math_primary(input, math, flavor);
    if (exponent == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // create power expression element
    Element* pow_element = create_math_element(input, "pow");
    if (!pow_element) {
        return ITEM_ERROR;
    }
    
    // add base and exponent as children (no op attribute needed)
    list_push((List*)pow_element, base);
    list_push((List*)pow_element, exponent);
    
    // set content length
    ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;
    
    return (Item)pow_element;
}

// parse typst fraction using / operator or frac() function
static Item parse_typst_fraction(Input *input, const char **math, MathFlavor flavor) {
    // In Typst, fractions can be: frac(a, b) or just a/b (handled by division)
    // This handles the frac(a, b) syntax
    
    // Expect "frac("
    if (strncmp(*math, "frac(", 5) != 0) {
        return ITEM_ERROR;
    }
    *math += 5; // skip "frac("
    
    skip_math_whitespace(math);
    
    // Parse numerator
    Item numerator = parse_math_expression(input, math, flavor);
    if (numerator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Expect comma
    if (**math != ',') {
        return ITEM_ERROR;
    }
    (*math)++; // skip comma
    
    skip_math_whitespace(math);
    
    // Parse denominator
    Item denominator = parse_math_expression(input, math, flavor);
    if (denominator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Expect closing parenthesis
    if (**math != ')') {
        return ITEM_ERROR;
    }
    (*math)++; // skip )
    
    // Create fraction element
    Element* frac_element = create_math_element(input, "frac");
    if (!frac_element) {
        return ITEM_ERROR;
    }
    
    list_push((List*)frac_element, numerator);
    list_push((List*)frac_element, denominator);
    
    ((TypeElmt*)frac_element->type)->content_length = ((List*)frac_element)->length;
    
    return (Item)frac_element;
}

// parse function call notation: func(arg1, arg2, ...)
static Item parse_function_call(Input *input, const char **math, MathFlavor flavor, const char* func_name) {
    // Expect opening parenthesis
    if (**math != '(') {
        return ITEM_ERROR;
    }
    (*math)++; // skip (
    
    skip_math_whitespace(math);
    
    // Create function element
    Element* func_element = create_math_element(input, func_name);
    if (!func_element) {
        return ITEM_ERROR;
    }
    
    // Parse arguments (comma-separated)
    if (**math != ')') { // Not empty argument list
        do {
            skip_math_whitespace(math);
            
            Item arg = parse_math_expression(input, math, flavor);
            if (arg == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            
            if (arg != ITEM_NULL) {
                list_push((List*)func_element, arg);
            }
            
            skip_math_whitespace(math);
            
            if (**math == ',') {
                (*math)++; // skip comma
            } else {
                break;
            }
        } while (**math && **math != ')');
    }
    
    // Expect closing parenthesis
    if (**math != ')') {
        return ITEM_ERROR;
    }
    (*math)++; // skip )
    
    ((TypeElmt*)func_element->type)->content_length = ((List*)func_element)->length;
    
    return (Item)func_element;
}

// parse ascii power expression with ^ or ** operators
static Item parse_ascii_power(Input *input, const char **math, MathFlavor flavor, Item base) {
    // ASCII math supports both ^ and ** for power
    bool double_star = false;
    if (**math == '*' && *(*math + 1) == '*') {
        double_star = true;
        (*math) += 2; // skip **
    } else {
        (*math)++; // skip ^
    }
    
    skip_math_whitespace(math);
    
    Item exponent = parse_math_primary(input, math, flavor);
    if (exponent == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // create power expression element
    Element* pow_element = create_math_element(input, "pow");
    if (!pow_element) {
        return ITEM_ERROR;
    }
    
    // add base and exponent as children (no op attribute needed)
    list_push((List*)pow_element, base);
    list_push((List*)pow_element, exponent);
    
    // set content length
    ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;
    
    return (Item)pow_element;
}

// parse primary expression (numbers, identifiers, parentheses, commands)
static Item parse_math_primary(Input *input, const char **math, MathFlavor flavor) {
    skip_math_whitespace(math);
    
    if (!**math) {
        return ITEM_NULL;
    }
    
    switch (flavor) {
        case MATH_FLAVOR_LATEX:
            // latex specific parsing
            if (**math == '\\') {
                return parse_latex_command(input, math);
            } else if (isdigit(**math) || (**math == '-' && isdigit(*(*math + 1)))) {
                return parse_math_number(input, math);
            } else if (isalpha(**math)) {
                return parse_math_identifier(input, math);
            } else if (**math == '(') {
                (*math)++; // skip (
                Item expr = parse_math_expression(input, math, flavor);
                if (**math == ')') {
                    (*math)++; // skip )
                }
                return expr;
            }
            break;
            
        case MATH_FLAVOR_TYPST:
        case MATH_FLAVOR_ASCII:
            // basic parsing for now
            if (isdigit(**math) || (**math == '-' && isdigit(*(*math + 1)))) {
                return parse_math_number(input, math);
            } else if (isalpha(**math)) {
                // Check if this is a function call by looking ahead for '('
                const char* lookahead = *math;
                while (*lookahead && (isalpha(*lookahead) || isdigit(*lookahead))) {
                    lookahead++;
                }
                
                if (*lookahead == '(') {
                    // This is a function call, parse the function name first
                    StrBuf* sb = input->sb;
                    strbuf_full_reset(sb);
                    
                    while (**math && (isalpha(**math) || isdigit(**math))) {
                        strbuf_append_char(sb, **math);
                        (*math)++;
                    }
                    
                    if (sb->length <= sizeof(uint32_t)) {
                        strbuf_full_reset(sb);
                        return ITEM_ERROR;
                    }
                    
                    String *func_string = (String*)sb->str;
                    func_string->len = sb->length - sizeof(uint32_t);
                    func_string->ref_cnt = 0;
                    
                    // Handle special Typst functions
                    if (flavor == MATH_FLAVOR_TYPST && strcmp(func_string->chars, "frac") == 0) {
                        // Reset math pointer to before function name
                        *math -= strlen("frac");
                        strbuf_full_reset(sb);
                        return parse_typst_fraction(input, math, flavor);
                    }
                    
                    // Check if it's a known mathematical function
                    const char* func_name = func_string->chars;
                    bool is_known_func = is_trig_function(func_name) || is_log_function(func_name) ||
                                        strcmp(func_name, "sqrt") == 0 || strcmp(func_name, "abs") == 0 ||
                                        strcmp(func_name, "ceil") == 0 || strcmp(func_name, "floor") == 0 ||
                                        strcmp(func_name, "exp") == 0 || strcmp(func_name, "pow") == 0 ||
                                        strcmp(func_name, "min") == 0 || strcmp(func_name, "max") == 0;
                    
                    // Make a copy of the function name before resetting the buffer
                    char func_name_copy[64]; // reasonable limit for function names
                    strncpy(func_name_copy, func_name, sizeof(func_name_copy) - 1);
                    func_name_copy[sizeof(func_name_copy) - 1] = '\0';
                    
                    if (is_known_func) {
                        strbuf_full_reset(sb);
                        Item result = parse_function_call(input, math, flavor, func_name_copy);
                        return result;
                    } else {
                        // For unknown functions, first try parsing as function call
                        // Save the current position in case we need to backtrack
                        const char* saved_pos = *math;
                        strbuf_full_reset(sb);
                        Item result = parse_function_call(input, math, flavor, func_name_copy);
                        if (result != ITEM_ERROR) {
                            return result;
                        } else {
                            // If function call parsing fails, restore position and treat as identifier
                            *math = saved_pos - strlen(func_name_copy);
                            return parse_math_identifier(input, math);
                        }
                    }
                } else {
                    // Regular identifier
                    return parse_math_identifier(input, math);
                }
            } else if (**math == '(') {
                (*math)++; // skip (
                Item expr = parse_math_expression(input, math, flavor);
                if (**math == ')') {
                    (*math)++; // skip )
                }
                return expr;
            }
            break;
    }
    
    return ITEM_ERROR;
}

// parse binary operation - use operator name as element name
static Item create_binary_expr(Input *input, const char* op_name, Item left, Item right) {
    Element* expr_element = create_math_element(input, op_name);
    if (!expr_element) {
        return ITEM_ERROR;
    }
    
    // add operands as children (no op attribute needed)
    list_push((List*)expr_element, left);
    list_push((List*)expr_element, right);
    
    // set content length
    ((TypeElmt*)expr_element->type)->content_length = ((List*)expr_element)->length;
    
    return (Item)expr_element;
}

// parse math expression with operator precedence (handles * and / before + and -)
static Item parse_math_expression(Input *input, const char **math, MathFlavor flavor) {
    return parse_addition_expression(input, math, flavor);
}

// parse addition and subtraction (lowest precedence)
static Item parse_addition_expression(Input *input, const char **math, MathFlavor flavor) {
    Item left = parse_multiplication_expression(input, math, flavor);
    if (left == ITEM_ERROR || left == ITEM_NULL) {
        return left;
    }
    
    skip_math_whitespace(math);
    
    while (**math && (**math == '+' || **math == '-')) {
        char op = **math;
        const char* op_name = (op == '+') ? "add" : "sub";
        
        (*math)++; // skip operator
        skip_math_whitespace(math);
        
        Item right = parse_multiplication_expression(input, math, flavor);
        if (right == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        left = create_binary_expr(input, op_name, left, right);
        if (left == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        skip_math_whitespace(math);
    }
    
    return left;
}

// parse multiplication and division (higher precedence than + and -)
static Item parse_multiplication_expression(Input *input, const char **math, MathFlavor flavor) {
    Item left = parse_power_expression(input, math, flavor);
    if (left == ITEM_ERROR || left == ITEM_NULL) {
        return left;
    }
    
    skip_math_whitespace(math);
    
    while (**math) {
        bool explicit_op = false;
        const char* op_name = "mul";
        
        // Check for explicit multiplication or division operators
        if (**math == '*' || **math == '/') {
            explicit_op = true;
            char op = **math;
            op_name = (op == '*') ? "mul" : "div";
            (*math)++; // skip operator
            skip_math_whitespace(math);
        }
        // Check for implicit multiplication (consecutive terms)
        else if ((**math == '\\' && flavor == MATH_FLAVOR_LATEX) ||  // LaTeX commands
                 (isalpha(**math) && (flavor == MATH_FLAVOR_TYPST || flavor == MATH_FLAVOR_ASCII)) ||  // identifiers
                 **math == '(' ||  // parentheses
                 isdigit(**math)) {  // numbers
            // This is implicit multiplication - don't advance the pointer yet
            explicit_op = false;
            op_name = "mul";
        } else {
            // No multiplication operation detected
            break;
        }
        
        Item right = parse_power_expression(input, math, flavor);
        if (right == ITEM_ERROR) {
            if (explicit_op) {
                // If we found an explicit operator, this is a real error
                return ITEM_ERROR;
            } else {
                // If it was implicit multiplication and failed, just stop parsing more terms
                break;
            }
        }
        
        if (right == ITEM_NULL) {
            if (explicit_op) {
                // Explicit operator requires a right operand
                return ITEM_ERROR;
            } else {
                // No more terms for implicit multiplication
                break;
            }
        }
        
        left = create_binary_expr(input, op_name, left, right);
        if (left == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        skip_math_whitespace(math);
    }
    
    return left;
}

// parse power expressions (^ and ** operators) - right associative
static Item parse_power_expression(Input *input, const char **math, MathFlavor flavor) {
    Item left = parse_primary_with_postfix(input, math, flavor);
    if (left == ITEM_ERROR || left == ITEM_NULL) {
        return left;
    }
    
    skip_math_whitespace(math);
    
    // Handle power operations for ASCII flavor
    if (flavor == MATH_FLAVOR_ASCII) {
        if (**math == '^') {
            (*math)++; // skip ^
            skip_math_whitespace(math);
            
            // Power is right-associative, so we recursively call parse_power_expression
            Item right = parse_power_expression(input, math, flavor);
            if (right == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            
            // create power expression element
            Element* pow_element = create_math_element(input, "pow");
            if (!pow_element) {
                return ITEM_ERROR;
            }
            
            // add base and exponent as children
            list_push((List*)pow_element, left);
            list_push((List*)pow_element, right);
            
            // set content length
            ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;
            
            return (Item)pow_element;
        } else if (**math == '*' && *(*math + 1) == '*') {
            (*math) += 2; // skip **
            skip_math_whitespace(math);
            
            // Power is right-associative, so we recursively call parse_power_expression
            Item right = parse_power_expression(input, math, flavor);
            if (right == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            
            // create power expression element
            Element* pow_element = create_math_element(input, "pow");
            if (!pow_element) {
                return ITEM_ERROR;
            }
            
            // add base and exponent as children
            list_push((List*)pow_element, left);
            list_push((List*)pow_element, right);
            
            // set content length
            ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;
            
            return (Item)pow_element;
        }
    }
    
    return left;
}

// parse primary expression with postfix operators (superscript, subscript)
static Item parse_primary_with_postfix(Input *input, const char **math, MathFlavor flavor) {
    Item left = parse_math_primary(input, math, flavor);
    if (left == ITEM_ERROR || left == ITEM_NULL) {
        return left;
    }
    
    skip_math_whitespace(math);
    
    // handle postfix operators (superscript, subscript, prime)
    while (true) {
        bool processed = false;
        
        if (flavor == MATH_FLAVOR_LATEX) {
            if (**math == '^') {
                (*math)++; // skip ^
                left = parse_latex_superscript(input, math, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            } else if (**math == '_') {
                (*math)++; // skip _
                left = parse_latex_subscript(input, math, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            }
            
            // Handle prime notation for all flavors
            if (**math == '\'') {
                left = parse_prime_notation(input, math, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            }
        } else if (flavor == MATH_FLAVOR_TYPST) {
            if (**math == '^') {
                (*math)++; // skip ^
                left = parse_typst_power(input, math, flavor, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            }
            
            // Handle prime notation for all flavors
            if (**math == '\'') {
                left = parse_prime_notation(input, math, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            }
        } else if (flavor == MATH_FLAVOR_ASCII) {
            // Handle prime notation for all flavors
            if (**math == '\'') {
                left = parse_prime_notation(input, math, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            }
        }
        
        if (!processed) {
            break;
        }
        skip_math_whitespace(math);
    }
    // Note: ASCII power operations are now handled in parse_power_expression
    
    return left;
}

// determine math flavor from string
// parse LaTeX mathematical functions like \sin{x}, \cos{x}, etc.
static Item parse_latex_function(Input *input, const char **math, const char* func_name) {
    skip_math_whitespace(math);
    
    Item arg = ITEM_NULL;
    
    // check for optional argument in braces
    if (**math == '{') {
        (*math)++; // skip '{'
        skip_math_whitespace(math);
        
        arg = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (arg == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        skip_math_whitespace(math);
        if (**math == '}') {
            (*math)++; // skip '}'
        }
    } else {
        // parse the next primary expression as the argument
        arg = parse_primary_with_postfix(input, math, MATH_FLAVOR_LATEX);
        if (arg == ITEM_ERROR) {
            return ITEM_ERROR;
        }
    }
    
    // create function expression
    Element* func_element = create_math_element(input, func_name);
    if (!func_element) {
        return ITEM_ERROR;
    }
    
    // add argument as child (no op attribute needed)
    list_push((List*)func_element, arg);
    
    return (Item)func_element;
}

// Parse LaTeX sum or product with limits: \sum_{i=1}^{n} or \prod_{i=0}^{n}
static Item parse_latex_sum_or_prod(Input *input, const char **math, const char* op_name) {
    skip_math_whitespace(math);
    
    // Create the sum/prod element
    Element* op_element = create_math_element(input, op_name);
    if (!op_element) {
        return ITEM_ERROR;
    }
    
    // Parse optional subscript (lower limit)
    if (**math == '_') {
        (*math)++; // skip _
        skip_math_whitespace(math);
        
        Item lower_limit;
        if (**math == '{') {
            (*math)++; // skip {
            lower_limit = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (lower_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            if (**math != '}') {
                return ITEM_ERROR;
            }
            (*math)++; // skip }
        } else {
            lower_limit = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
            if (lower_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
        }
        
        // Add lower limit as first child
        list_push((List*)op_element, lower_limit);
        skip_math_whitespace(math);
    }
    
    // Parse optional superscript (upper limit)
    if (**math == '^') {
        (*math)++; // skip ^
        skip_math_whitespace(math);
        
        Item upper_limit;
        if (**math == '{') {
            (*math)++; // skip {
            upper_limit = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (upper_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            if (**math != '}') {
                return ITEM_ERROR;
            }
            (*math)++; // skip }
        } else {
            upper_limit = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
            if (upper_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
        }
        
        // Add upper limit as second child
        list_push((List*)op_element, upper_limit);
        skip_math_whitespace(math);
    }
    
    // Parse the expression being summed/multiplied
    Item expr = parse_primary_with_postfix(input, math, MATH_FLAVOR_LATEX);
    if (expr == ITEM_ERROR) {
        // If no expression follows, this is still valid (like \sum x)
        expr = ITEM_NULL;
    }
    
    if (expr != ITEM_NULL) {
        list_push((List*)op_element, expr);
    }
    
    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    return (Item)op_element;
}

// Parse LaTeX integral with limits: \int_{a}^{b} f(x) dx
static Item parse_latex_integral(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // Create the integral element
    Element* int_element = create_math_element(input, "int");
    if (!int_element) {
        return ITEM_ERROR;
    }
    
    // Parse optional subscript (lower limit)
    if (**math == '_') {
        (*math)++; // skip _
        skip_math_whitespace(math);
        
        Item lower_limit;
        if (**math == '{') {
            (*math)++; // skip {
            lower_limit = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (lower_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            if (**math != '}') {
                return ITEM_ERROR;
            }
            (*math)++; // skip }
        } else {
            lower_limit = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
            if (lower_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
        }
        
        list_push((List*)int_element, lower_limit);
        skip_math_whitespace(math);
    }
    
    // Parse optional superscript (upper limit)
    if (**math == '^') {
        (*math)++; // skip ^
        skip_math_whitespace(math);
        
        Item upper_limit;
        if (**math == '{') {
            (*math)++; // skip {
            upper_limit = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (upper_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            if (**math != '}') {
                return ITEM_ERROR;
            }
            (*math)++; // skip }
        } else {
            upper_limit = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
            if (upper_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
        }
        
        list_push((List*)int_element, upper_limit);
        skip_math_whitespace(math);
    }
    
    // Parse the integrand expression
    Item integrand = parse_primary_with_postfix(input, math, MATH_FLAVOR_LATEX);
    if (integrand != ITEM_ERROR && integrand != ITEM_NULL) {
        list_push((List*)int_element, integrand);
    }
    
    ((TypeElmt*)int_element->type)->content_length = ((List*)int_element)->length;
    return (Item)int_element;
}

// Parse LaTeX limit: \lim_{x \to 0} f(x)
static Item parse_latex_limit(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // Create the limit element
    Element* lim_element = create_math_element(input, "lim");
    if (!lim_element) {
        return ITEM_ERROR;
    }
    
    // Parse subscript (limit expression like x \to 0)
    if (**math == '_') {
        (*math)++; // skip _
        skip_math_whitespace(math);
        
        Item limit_expr;
        if (**math == '{') {
            (*math)++; // skip {
            limit_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (limit_expr == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            if (**math != '}') {
                return ITEM_ERROR;
            }
            (*math)++; // skip }
        } else {
            limit_expr = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
            if (limit_expr == ITEM_ERROR) {
                return ITEM_ERROR;
            }
        }
        
        list_push((List*)lim_element, limit_expr);
        skip_math_whitespace(math);
    }
    
    // Parse the function expression
    Item func_expr = parse_primary_with_postfix(input, math, MATH_FLAVOR_LATEX);
    if (func_expr != ITEM_ERROR && func_expr != ITEM_NULL) {
        list_push((List*)lim_element, func_expr);
    }
    
    ((TypeElmt*)lim_element->type)->content_length = ((List*)lim_element)->length;
    return (Item)lim_element;
}

// Forward declaration for full matrix environment parsing
static Item parse_latex_matrix_environment(Input *input, const char **math, const char* matrix_type);

// Parse LaTeX matrix: \begin{matrix} ... \end{matrix} or \begin{pmatrix} ... \end{pmatrix}
// Also supports simplified syntax \matrix{a & b \\ c & d}
static Item parse_latex_matrix(Input *input, const char **math, const char* matrix_type) {
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{matrix}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        return parse_latex_matrix_environment(input, math, matrix_type);
    }
    
    // Simplified matrix syntax: \matrix{content}
    if (**math != '{') {
        printf("ERROR: Expected '{' after matrix command\n");
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // Create the matrix element
    Element* matrix_element = create_math_element(input, matrix_type);
    if (!matrix_element) {
        printf("ERROR: Failed to create matrix element\n");
        return ITEM_ERROR;
    }
    
    // Parse matrix rows (separated by \\)
    Element* current_row = create_math_element(input, "row");
    if (!current_row) {
        printf("ERROR: Failed to create matrix row element\n");
        return ITEM_ERROR;
    }
    
    int row_count = 0;
    int col_count = 0;
    int current_col = 0;
    
    while (**math && **math != '}') {
        skip_math_whitespace(math);
        
        if (strncmp(*math, "\\\\", 2) == 0) {
            // End of row
            (*math) += 2; // skip \\
            
            // Validate column count consistency
            if (row_count == 0) {
                col_count = current_col + (((List*)current_row)->length > 0 ? 1 : 0);
            } else if (current_col + (((List*)current_row)->length > 0 ? 1 : 0) != col_count) {
                printf("WARNING: Inconsistent column count in matrix row %d\n", row_count + 1);
            }
            
            // Add current row to matrix
            ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
            list_push((List*)matrix_element, (Item)current_row);
            row_count++;
            current_col = 0;
            
            // Start new row
            current_row = create_math_element(input, "row");
            if (!current_row) {
                printf("ERROR: Failed to create matrix row element\n");
                return ITEM_ERROR;
            }
            skip_math_whitespace(math);
            continue;
        }
        
        if (**math == '&') {
            // Column separator - parse as next cell in row
            (*math)++; // skip &
            current_col++;
            skip_math_whitespace(math);
            continue;
        }
        
        // Parse matrix cell content
        Item cell = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (cell == ITEM_ERROR) {
            printf("ERROR: Failed to parse matrix cell at row %d, col %d\n", row_count + 1, current_col + 1);
            return ITEM_ERROR;
        }
        
        if (cell != ITEM_NULL) {
            list_push((List*)current_row, cell);
        }
        
        skip_math_whitespace(math);
    }
    
    if (**math != '}') {
        printf("ERROR: Expected '}' to close matrix\n");
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // Add the last row if it has content
    if (((List*)current_row)->length > 0) {
        // Validate final row column count
        if (row_count > 0 && current_col + 1 != col_count) {
            printf("WARNING: Inconsistent column count in final matrix row\n");
        }
        ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
        list_push((List*)matrix_element, (Item)current_row);
        row_count++;
    }
    
    // Add matrix dimensions as attributes
    char row_str[16], col_str[16];
    snprintf(row_str, sizeof(row_str), "%d", row_count);
    snprintf(col_str, sizeof(col_str), "%d", col_count);
    add_attribute_to_element(input, matrix_element, "rows", row_str);
    add_attribute_to_element(input, matrix_element, "cols", col_str);
    
    ((TypeElmt*)matrix_element->type)->content_length = ((List*)matrix_element)->length;
    // Matrix parsing completed successfully
    return (Item)matrix_element;
}

// Parse full LaTeX matrix environment: \begin{matrix} ... \end{matrix}
static Item parse_latex_matrix_environment(Input *input, const char **math, const char* matrix_type) {
    // Expected format: \begin{matrix} content \end{matrix}
    
    // Skip \begin{
    if (strncmp(*math, "\\begin{", 7) != 0) {
        printf("ERROR: Expected \\begin{ for matrix environment\n");
        return ITEM_ERROR;
    }
    *math += 7;
    
    // Find the environment name
    const char* env_start = *math;
    while (**math && **math != '}') {
        (*math)++;
    }
    
    if (**math != '}') {
        printf("ERROR: Expected '}' after \\begin{environment\n");
        return ITEM_ERROR;
    }
    
    size_t env_len = *math - env_start;
    (*math)++; // skip }
    
    // Validate environment name matches expected matrix type
    if (strncmp(env_start, matrix_type, env_len) != 0 || strlen(matrix_type) != env_len) {
        char env_name[32];
        strncpy(env_name, env_start, env_len < 31 ? env_len : 31);
        env_name[env_len < 31 ? env_len : 31] = '\0';
        printf("WARNING: Environment name '%s' doesn't match expected '%s'\n", env_name, matrix_type);
    }
    
    skip_math_whitespace(math);
    
    // Create the matrix element
    Element* matrix_element = create_math_element(input, matrix_type);
    if (!matrix_element) {
        printf("ERROR: Failed to create matrix environment element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, matrix_element, "env", "true");
    
    // Parse matrix content (same as simplified syntax but without outer braces)
    Element* current_row = create_math_element(input, "row");
    if (!current_row) {
        printf("ERROR: Failed to create matrix row element\n");
        return ITEM_ERROR;
    }
    
    int row_count = 0;
    int col_count = 0;
    int current_col = 0;
    
    while (**math) {
        skip_math_whitespace(math);
        
        // Check for end of environment
        if (strncmp(*math, "\\end{", 5) == 0) {
            break;
        }
        
        if (strncmp(*math, "\\\\", 2) == 0) {
            // End of row
            (*math) += 2; // skip \\
            
            // Validate column count consistency
            if (row_count == 0) {
                col_count = current_col + (((List*)current_row)->length > 0 ? 1 : 0);
            } else if (current_col + (((List*)current_row)->length > 0 ? 1 : 0) != col_count) {
                printf("WARNING: Inconsistent column count in matrix row %d\n", row_count + 1);
            }
            
            // Add current row to matrix
            ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
            list_push((List*)matrix_element, (Item)current_row);
            row_count++;
            current_col = 0;
            
            // Start new row
            current_row = create_math_element(input, "row");
            if (!current_row) {
                printf("ERROR: Failed to create matrix row element\n");
                return ITEM_ERROR;
            }
            skip_math_whitespace(math);
            continue;
        }
        
        if (**math == '&') {
            // Column separator
            (*math)++; // skip &
            current_col++;
            skip_math_whitespace(math);
            continue;
        }
        
        // Parse matrix cell content
        Item cell = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (cell == ITEM_ERROR) {
            printf("ERROR: Failed to parse matrix cell at row %d, col %d\n", row_count + 1, current_col + 1);
            return ITEM_ERROR;
        }
        
        if (cell != ITEM_NULL) {
            list_push((List*)current_row, cell);
        }
        
        skip_math_whitespace(math);
    }
    
    // Parse \end{environment}
    if (strncmp(*math, "\\end{", 5) != 0) {
        printf("ERROR: Expected \\end{%s} to close matrix environment\n", matrix_type);
        return ITEM_ERROR;
    }
    *math += 5;
    
    // Validate end environment name
    const char* end_env_start = *math;
    while (**math && **math != '}') {
        (*math)++;
    }
    
    if (**math != '}') {
        printf("ERROR: Expected '}' after \\end{environment\n");
        return ITEM_ERROR;
    }
    
    size_t end_env_len = *math - end_env_start;
    (*math)++; // skip }
    
    if (strncmp(end_env_start, matrix_type, end_env_len) != 0 || strlen(matrix_type) != end_env_len) {
        char end_env_name[32];
        strncpy(end_env_name, end_env_start, end_env_len < 31 ? end_env_len : 31);
        end_env_name[end_env_len < 31 ? end_env_len : 31] = '\0';
        printf("ERROR: Mismatched environment: \\begin{%s} but \\end{%s}\n", matrix_type, end_env_name);
        return ITEM_ERROR;
    }
    
    // Add the last row if it has content
    if (((List*)current_row)->length > 0) {
        // Validate final row column count
        if (row_count > 0 && current_col + 1 != col_count) {
            printf("WARNING: Inconsistent column count in final matrix row\n");
        }
        ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
        list_push((List*)matrix_element, (Item)current_row);
        row_count++;
    }
    
    // Add matrix dimensions as attributes
    char row_str[16], col_str[16];
    snprintf(row_str, sizeof(row_str), "%d", row_count);
    snprintf(col_str, sizeof(col_str), "%d", col_count);
    add_attribute_to_element(input, matrix_element, "rows", row_str);
    add_attribute_to_element(input, matrix_element, "cols", col_str);
    
    ((TypeElmt*)matrix_element->type)->content_length = ((List*)matrix_element)->length;
    // Matrix environment parsing completed successfully
    return (Item)matrix_element;
}

// Parse LaTeX cases environment: \begin{cases} ... \end{cases}
static Item parse_latex_cases(Input *input, const char **math) {
    // Expected format: \begin{cases} expr1 & condition1 \\ expr2 & condition2 \\ ... \end{cases}
    
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{cases}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{cases}
        if (strncmp(*math, "\\begin{cases}", 13) != 0) {
            printf("ERROR: Expected \\begin{cases} for cases environment\n");
            return ITEM_ERROR;
        }
        *math += 13;
    } else {
        printf("ERROR: Expected \\begin{cases} for cases environment\n");
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Create the cases element
    Element* cases_element = create_math_element(input, "cases");
    if (!cases_element) {
        printf("ERROR: Failed to create cases element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, cases_element, "env", "true");
    
    // Parse case rows (each row has expression & condition)
    int case_count = 0;
    
    while (**math) {
        skip_math_whitespace(math);
        
        // Check for end of environment
        if (strncmp(*math, "\\end{cases}", 11) == 0) {
            *math += 11;
            break;
        }
        
        // Create a case row element
        Element* case_row = create_math_element(input, "case");
        if (!case_row) {
            printf("ERROR: Failed to create case row element\n");
            return ITEM_ERROR;
        }
        
        // Parse the expression (left side of &)
        Item expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (expr == ITEM_ERROR) {
            printf("ERROR: Failed to parse case expression at case %d\n", case_count + 1);
            return ITEM_ERROR;
        }
        
        if (expr != ITEM_NULL) {
            list_push((List*)case_row, expr);
        }
        
        skip_math_whitespace(math);
        
        // Expect & separator
        if (**math == '&') {
            (*math)++; // skip &
            skip_math_whitespace(math);
            
            // Parse the condition (right side of &)
            Item condition = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (condition == ITEM_ERROR) {
                printf("ERROR: Failed to parse case condition at case %d\n", case_count + 1);
                return ITEM_ERROR;
            }
            
            if (condition != ITEM_NULL) {
                list_push((List*)case_row, condition);
            }
        }
        
        skip_math_whitespace(math);
        
        // Check for row separator \\
        if (strncmp(*math, "\\\\", 2) == 0) {
            (*math) += 2; // skip \\
        }
        
        // Add the case row to cases element
        ((TypeElmt*)case_row->type)->content_length = ((List*)case_row)->length;
        list_push((List*)cases_element, (Item)case_row);
        case_count++;
        
        skip_math_whitespace(math);
    }
    
    // Add case count as attribute
    char case_str[16];
    snprintf(case_str, sizeof(case_str), "%d", case_count);
    add_attribute_to_element(input, cases_element, "cases", case_str);
    
    ((TypeElmt*)cases_element->type)->content_length = ((List*)cases_element)->length;
    return (Item)cases_element;
}

// Parse LaTeX equation environment: \begin{equation} ... \end{equation}
static Item parse_latex_equation(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{equation}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{equation}
        if (strncmp(*math, "\\begin{equation}", 16) != 0) {
            printf("ERROR: Expected \\begin{equation} for equation environment\n");
            return ITEM_ERROR;
        }
        *math += 16;
    } else {
        printf("ERROR: Expected \\begin{equation} for equation environment\n");
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Create the equation element
    Element* eq_element = create_math_element(input, "equation");
    if (!eq_element) {
        printf("ERROR: Failed to create equation element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, eq_element, "env", "true");
    add_attribute_to_element(input, eq_element, "numbered", "true");
    
    // Parse equation content until \end{equation}
    const char* content_start = *math;
    const char* content_end = strstr(*math, "\\end{equation}");
    
    if (!content_end) {
        printf("ERROR: Expected \\end{equation} to close equation environment\n");
        return ITEM_ERROR;
    }
    
    // Create a temporary null-terminated string for the content
    size_t content_length = content_end - content_start;
    char* temp_content = malloc(content_length + 1);
    if (!temp_content) {
        printf("ERROR: Failed to allocate memory for equation content\n");
        return ITEM_ERROR;
    }
    strncpy(temp_content, content_start, content_length);
    temp_content[content_length] = '\0';
    
    // Parse the content
    const char* temp_ptr = temp_content;
    Item content = parse_math_expression(input, &temp_ptr, MATH_FLAVOR_LATEX);
    free(temp_content);
    
    if (content == ITEM_ERROR) {
        printf("ERROR: Failed to parse equation content\n");
        return ITEM_ERROR;
    }
    
    if (content != ITEM_NULL) {
        list_push((List*)eq_element, content);
    }
    
    // Move past the content to \end{equation}
    *math = content_end;
    
    // Parse \end{equation}
    if (strncmp(*math, "\\end{equation}", 14) != 0) {
        printf("ERROR: Expected \\end{equation} to close equation environment\n");
        return ITEM_ERROR;
    }
    *math += 14;
    
    ((TypeElmt*)eq_element->type)->content_length = ((List*)eq_element)->length;
    return (Item)eq_element;
}

// Parse LaTeX align environment: \begin{align} ... \end{align}
static Item parse_latex_align(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{align}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{align}
        if (strncmp(*math, "\\begin{align}", 13) != 0) {
            printf("ERROR: Expected \\begin{align} for align environment\n");
            return ITEM_ERROR;
        }
        *math += 13;
    } else {
        printf("ERROR: Expected \\begin{align} for align environment\n");
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Create the align element
    Element* align_element = create_math_element(input, "align");
    if (!align_element) {
        printf("ERROR: Failed to create align element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, align_element, "env", "true");
    add_attribute_to_element(input, align_element, "numbered", "true");
    
    // Parse alignment rows (separated by \\)
    int eq_count = 0;
    
    while (**math) {
        skip_math_whitespace(math);
        
        // Check for end of environment
        if (strncmp(*math, "\\end{align}", 11) == 0) {
            *math += 11;
            break;
        }
        
        // Create an equation row element
        Element* eq_row = create_math_element(input, "equation");
        if (!eq_row) {
            printf("ERROR: Failed to create align row element\n");
            return ITEM_ERROR;
        }
        
        // Parse left side of alignment
        Item left_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (left_expr == ITEM_ERROR) {
            printf("ERROR: Failed to parse left side of align equation %d\n", eq_count + 1);
            return ITEM_ERROR;
        }
        
        if (left_expr != ITEM_NULL) {
            list_push((List*)eq_row, left_expr);
        }
        
        skip_math_whitespace(math);
        
        // Check for alignment point &
        if (**math == '&') {
            (*math)++; // skip &
            skip_math_whitespace(math);
            
            // Parse right side of alignment
            Item right_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (right_expr == ITEM_ERROR) {
                printf("ERROR: Failed to parse right side of align equation %d\n", eq_count + 1);
                return ITEM_ERROR;
            }
            
            if (right_expr != ITEM_NULL) {
                list_push((List*)eq_row, right_expr);
            }
        }
        
        skip_math_whitespace(math);
        
        // Check for row separator \\
        if (strncmp(*math, "\\\\", 2) == 0) {
            (*math) += 2; // skip \\
        }
        
        // Add the equation row to align element
        ((TypeElmt*)eq_row->type)->content_length = ((List*)eq_row)->length;
        list_push((List*)align_element, (Item)eq_row);
        eq_count++;
        
        skip_math_whitespace(math);
    }
    
    // Add equation count as attribute
    char eq_str[16];
    snprintf(eq_str, sizeof(eq_str), "%d", eq_count);
    add_attribute_to_element(input, align_element, "equations", eq_str);
    
    ((TypeElmt*)align_element->type)->content_length = ((List*)align_element)->length;
    return (Item)align_element;
}

// Parse LaTeX aligned environment: \begin{aligned} ... \end{aligned}
static Item parse_latex_aligned(Input *input, const char **math) {
    // Expected format: \begin{aligned} expr1 &= expr2 \\ expr3 &= expr4 \\ ... \end{aligned}
    // Similar to align but typically used inside other environments and not numbered
    
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{aligned}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{aligned}
        if (strncmp(*math, "\\begin{aligned}", 15) != 0) {
            printf("ERROR: Expected \\begin{aligned} for aligned environment\n");
            return ITEM_ERROR;
        }
        *math += 15;
    } else {
        printf("ERROR: Expected \\begin{aligned} for aligned environment\n");
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Create the aligned element
    Element* aligned_element = create_math_element(input, "aligned");
    if (!aligned_element) {
        printf("ERROR: Failed to create aligned element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, aligned_element, "env", "true");
    add_attribute_to_element(input, aligned_element, "numbered", "false");
    
    // Parse alignment rows (separated by \\)
    int eq_count = 0;
    
    while (**math) {
        skip_math_whitespace(math);
        
        // Check for end of environment
        if (strncmp(*math, "\\end{aligned}", 13) == 0) {
            *math += 13;
            break;
        }
        
        // Create an equation row element
        Element* eq_row = create_math_element(input, "equation");
        if (!eq_row) {
            printf("ERROR: Failed to create aligned row element\n");
            return ITEM_ERROR;
        }
        
        // Parse left side of alignment
        Item left_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (left_expr == ITEM_ERROR) {
            printf("ERROR: Failed to parse left side of aligned equation %d\n", eq_count + 1);
            return ITEM_ERROR;
        }
        
        if (left_expr != ITEM_NULL) {
            list_push((List*)eq_row, left_expr);
        }
        
        skip_math_whitespace(math);
        
        // Check for alignment point &
        if (**math == '&') {
            (*math)++; // skip &
            skip_math_whitespace(math);
            
            // Parse right side of alignment
            Item right_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (right_expr == ITEM_ERROR) {
                printf("ERROR: Failed to parse right side of aligned equation %d\n", eq_count + 1);
                return ITEM_ERROR;
            }
            
            if (right_expr != ITEM_NULL) {
                list_push((List*)eq_row, right_expr);
            }
        }
        
        skip_math_whitespace(math);
        
        // Check for row separator \\
        if (strncmp(*math, "\\\\", 2) == 0) {
            (*math) += 2; // skip \\
        }
        
        // Add the equation row to aligned element
        ((TypeElmt*)eq_row->type)->content_length = ((List*)eq_row)->length;
        list_push((List*)aligned_element, (Item)eq_row);
        eq_count++;
        
        skip_math_whitespace(math);
    }
    
    // Add equation count as attribute
    char eq_str[16];
    snprintf(eq_str, sizeof(eq_str), "%d", eq_count);
    add_attribute_to_element(input, aligned_element, "equations", eq_str);
    
    ((TypeElmt*)aligned_element->type)->content_length = ((List*)aligned_element)->length;
    return (Item)aligned_element;
}

// Parse LaTeX gather environment: \begin{gather} ... \end{gather}
static Item parse_latex_gather(Input *input, const char **math) {
    // Expected format: \begin{gather} expr1 \\ expr2 \\ ... \end{gather}
    // Center-aligned equations, each numbered
    
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{gather}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{gather}
        if (strncmp(*math, "\\begin{gather}", 14) != 0) {
            printf("ERROR: Expected \\begin{gather} for gather environment\n");
            return ITEM_ERROR;
        }
        *math += 14;
    } else {
        printf("ERROR: Expected \\begin{gather} for gather environment\n");
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Create the gather element
    Element* gather_element = create_math_element(input, "gather");
    if (!gather_element) {
        printf("ERROR: Failed to create gather element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, gather_element, "env", "true");
    add_attribute_to_element(input, gather_element, "numbered", "true");
    add_attribute_to_element(input, gather_element, "alignment", "center");
    
    // Parse equations (separated by \\)
    int eq_count = 0;
    
    while (**math) {
        skip_math_whitespace(math);
        
        // Check for end of environment
        if (strncmp(*math, "\\end{gather}", 12) == 0) {
            *math += 12;
            break;
        }
        
        // Create an equation element
        Element* eq_element = create_math_element(input, "equation");
        if (!eq_element) {
            printf("ERROR: Failed to create gather equation element\n");
            return ITEM_ERROR;
        }
        
        // Parse the equation content
        Item eq_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (eq_expr == ITEM_ERROR) {
            printf("ERROR: Failed to parse gather equation %d\n", eq_count + 1);
            return ITEM_ERROR;
        }
        
        if (eq_expr != ITEM_NULL) {
            list_push((List*)eq_element, eq_expr);
        }
        
        skip_math_whitespace(math);
        
        // Check for row separator \\
        if (strncmp(*math, "\\\\", 2) == 0) {
            (*math) += 2; // skip \\
        }
        
        // Add the equation to gather element
        ((TypeElmt*)eq_element->type)->content_length = ((List*)eq_element)->length;
        list_push((List*)gather_element, (Item)eq_element);
        eq_count++;
        
        skip_math_whitespace(math);
    }
    
    // Add equation count as attribute
    char eq_str[16];
    snprintf(eq_str, sizeof(eq_str), "%d", eq_count);
    add_attribute_to_element(input, gather_element, "equations", eq_str);
    
    ((TypeElmt*)gather_element->type)->content_length = ((List*)gather_element)->length;
    return (Item)gather_element;
}

// Enhanced helper functions for new mathematical constructs
static bool is_number_set(const char* cmd) {
    return strcmp(cmd, "mathbb") == 0;
}

static bool is_set_operation(const char* cmd) {
    return strcmp(cmd, "in") == 0 || strcmp(cmd, "notin") == 0 ||
           strcmp(cmd, "subset") == 0 || strcmp(cmd, "supset") == 0 ||
           strcmp(cmd, "cup") == 0 || strcmp(cmd, "cap") == 0 ||
           strcmp(cmd, "emptyset") == 0;
}

static bool is_logic_operator(const char* cmd) {
    return strcmp(cmd, "forall") == 0 || strcmp(cmd, "exists") == 0 ||
           strcmp(cmd, "land") == 0 || strcmp(cmd, "lor") == 0 ||
           strcmp(cmd, "neg") == 0 || strcmp(cmd, "Rightarrow") == 0 ||
           strcmp(cmd, "Leftrightarrow") == 0;
}

static bool is_binomial_cmd(const char* cmd) {
    return strcmp(cmd, "binom") == 0 || strcmp(cmd, "choose") == 0 ||
           strcmp(cmd, "tbinom") == 0 || strcmp(cmd, "dbinom") == 0;
}

static bool is_derivative_cmd(const char* cmd) {
    return strcmp(cmd, "frac") == 0 && strstr(cmd, "d") != NULL; // This will be handled specially in frac parsing
}

static bool is_vector_cmd(const char* cmd) {
    return strcmp(cmd, "vec") == 0 || strcmp(cmd, "overrightarrow") == 0 ||
           strcmp(cmd, "overleftarrow") == 0;
}

static bool is_accent_cmd(const char* cmd) {
    return strcmp(cmd, "hat") == 0 || strcmp(cmd, "widehat") == 0 ||
           strcmp(cmd, "dot") == 0 || strcmp(cmd, "ddot") == 0 ||
           strcmp(cmd, "bar") == 0 || strcmp(cmd, "tilde") == 0 ||
           strcmp(cmd, "widetilde") == 0 || strcmp(cmd, "acute") == 0 ||
           strcmp(cmd, "grave") == 0 || strcmp(cmd, "check") == 0 ||
           strcmp(cmd, "breve") == 0;
}

static bool is_arrow_cmd(const char* cmd) {
    return strcmp(cmd, "rightarrow") == 0 || strcmp(cmd, "leftarrow") == 0 ||
           strcmp(cmd, "to") == 0 || strcmp(cmd, "gets") == 0 ||
           strcmp(cmd, "uparrow") == 0 || strcmp(cmd, "downarrow") == 0 ||
           strcmp(cmd, "updownarrow") == 0 || strcmp(cmd, "leftrightarrow") == 0;
}

// Parse absolute value: \left| x \right| or \abs{x}
static Item parse_latex_abs(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    Item inner;
    
    // Check if this is \abs{} format or \left| format
    if (**math == '{') {
        (*math)++; // skip opening brace
        
        // Parse the inner expression
        inner = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (inner == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        skip_math_whitespace(math);
        
        // Expect closing brace
        if (**math != '}') {
            printf("ERROR: Expected } for absolute value, found: %.10s\n", *math);
            return ITEM_ERROR;
        }
        (*math)++; // skip closing brace
        
    } else {
        // This is \left| format, parse until \right|
        inner = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (inner == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        skip_math_whitespace(math);
        
        // Expect \right|
        if (strncmp(*math, "\\right|", 7) != 0) {
            printf("ERROR: Expected \\right| for absolute value, found: %.10s\n", *math);
            return ITEM_ERROR;
        }
        *math += 7;
    }
    
    // Create abs element
    Element* abs_element = create_math_element(input, "abs");
    if (!abs_element) {
        return ITEM_ERROR;
    }
    
    list_push((List*)abs_element, inner);
    ((TypeElmt*)abs_element->type)->content_length = ((List*)abs_element)->length;
    
    return (Item)abs_element;
}

// Parse ceiling/floor functions: \lceil x \rceil, \lfloor x \rfloor
static Item parse_latex_ceil_floor(Input *input, const char **math, const char* func_name) {
    skip_math_whitespace(math);
    
    // Parse the inner expression - no braces expected, just parse until the closing delimiter
    Item inner = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (inner == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Expect appropriate closing delimiter
    if (strcmp(func_name, "ceil") == 0 && strncmp(*math, "\\rceil", 6) == 0) {
        *math += 6;
    } else if (strcmp(func_name, "floor") == 0 && strncmp(*math, "\\rfloor", 7) == 0) {
        *math += 7;
    } else {
        printf("ERROR: Expected closing delimiter for %s, found: %.10s\n", func_name, *math);
        return ITEM_ERROR;
    }
    
    // Create function element
    Element* func_element = create_math_element(input, func_name);
    if (!func_element) {
        return ITEM_ERROR;
    }
    
    list_push((List*)func_element, inner);
    ((TypeElmt*)func_element->type)->content_length = ((List*)func_element)->length;
    
    return (Item)func_element;
}

// Parse prime notation: f'(x), f''(x), f'''(x)
static Item parse_prime_notation(Input *input, const char **math, Item base) {
    int prime_count = 0;
    
    // Count consecutive apostrophes
    while (**math == '\'') {
        prime_count++;
        (*math)++;
    }
    
    // Create prime element
    Element* prime_element = create_math_element(input, "prime");
    if (!prime_element) {
        return ITEM_ERROR;
    }
    
    // Add base expression
    list_push((List*)prime_element, base);
    
    // Add prime count as attribute
    char count_str[16];
    snprintf(count_str, sizeof(count_str), "%d", prime_count);
    add_attribute_to_element(input, prime_element, "count", count_str);
    
    ((TypeElmt*)prime_element->type)->content_length = ((List*)prime_element)->length;
    
    return (Item)prime_element;
}

// Parse number sets: \mathbb{R}, \mathbb{N}, etc.
static Item parse_number_set(Input *input, const char **math, const char* set_name) {
    // Expect opening brace
    skip_math_whitespace(math);
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++;
    
    // Parse set identifier (single letter)
    skip_math_whitespace(math);
    if (!**math || !isalpha(**math)) {
        return ITEM_ERROR;
    }
    
    char set_char = **math;
    (*math)++;
    
    // Expect closing brace
    skip_math_whitespace(math);
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++;
    
    // Create set element with appropriate name
    const char* set_symbol;
    switch (set_char) {
        case 'R': set_symbol = "reals"; break;
        case 'N': set_symbol = "naturals"; break;
        case 'Z': set_symbol = "integers"; break;
        case 'Q': set_symbol = "rationals"; break;
        case 'C': set_symbol = "complex"; break;
        default: set_symbol = "set"; break;
    }
    
    Element* set_element = create_math_element(input, set_symbol);
    if (!set_element) {
        return ITEM_ERROR;
    }
    
    // Add set type as attribute
    char set_str[2] = {set_char, '\0'};
    add_attribute_to_element(input, set_element, "type", set_str);
    
    ((TypeElmt*)set_element->type)->content_length = ((List*)set_element)->length;
    
    return (Item)set_element;
}

// Parse set operations: \in, \subset, \cup, \cap, etc.
static Item parse_set_operation(Input *input, const char **math, const char* op_name) {
    Element* op_element = create_math_element(input, op_name);
    if (!op_element) {
        return ITEM_ERROR;
    }
    
    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    return (Item)op_element;
}

// Parse logic operators: \forall, \exists, \land, \lor, etc.
static Item parse_logic_operator(Input *input, const char **math, const char* op_name) {
    Element* op_element = create_math_element(input, op_name);
    if (!op_element) {
        return ITEM_ERROR;
    }
    
    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    return (Item)op_element;
}

// Parse binomial coefficients: \binom{n}{k} or \choose{n}{k}
static Item parse_latex_binomial(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // expect opening brace for first argument
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse first argument (n)
    Item n = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (n == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    skip_math_whitespace(math);
    
    // expect opening brace for second argument
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse second argument (k)
    Item k = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (k == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create binomial expression element
    Element* binom_element = create_math_element(input, "binom");
    if (!binom_element) {
        return ITEM_ERROR;
    }
    
    // add n and k as children
    list_push((List*)binom_element, n);
    list_push((List*)binom_element, k);
    
    // set content length
    ((TypeElmt*)binom_element->type)->content_length = ((List*)binom_element)->length;
    
    return (Item)binom_element;
}

// Parse derivative notation: \frac{d}{dx} or \frac{\partial}{\partial x}
static Item parse_latex_derivative(Input *input, const char **math) {
    // This function is called when we detect derivative patterns in \frac commands
    // For now, we'll handle this in the regular frac parser by detecting 'd' patterns
    return parse_latex_frac(input, math);
}

// Parse vector notation: \vec{v}, \overrightarrow{AB}, etc.
static Item parse_latex_vector(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // expect opening brace
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse vector content
    Item vector_content = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (vector_content == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create vector expression element
    Element* vec_element = create_math_element(input, "vec");
    if (!vec_element) {
        return ITEM_ERROR;
    }
    
    // add vector content as child
    list_push((List*)vec_element, vector_content);
    
    // set content length
    ((TypeElmt*)vec_element->type)->content_length = ((List*)vec_element)->length;
    
    return (Item)vec_element;
}

// Parse accent marks: \hat{x}, \dot{x}, \bar{x}, etc.
static Item parse_latex_accent(Input *input, const char **math, const char* accent_type) {
    skip_math_whitespace(math);
    
    // expect opening brace
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse accented content
    Item accented_content = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (accented_content == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create accent expression element
    Element* accent_element = create_math_element(input, accent_type);
    if (!accent_element) {
        return ITEM_ERROR;
    }
    
    // add accented content as child
    list_push((List*)accent_element, accented_content);
    
    // set content length
    ((TypeElmt*)accent_element->type)->content_length = ((List*)accent_element)->length;
    
    return (Item)accent_element;
}

// Parse arrow notation: \to, \rightarrow, \leftarrow, etc.
static Item parse_latex_arrow(Input *input, const char **math, const char* arrow_type) {
    // Most arrow commands don't take arguments, they're just symbols
    Element* arrow_element = create_math_element(input, arrow_type);
    if (!arrow_element) {
        return ITEM_ERROR;
    }
    
    // Add arrow direction as attribute
    if (strcmp(arrow_type, "rightarrow") == 0 || strcmp(arrow_type, "to") == 0) {
        add_attribute_to_element(input, arrow_element, "direction", "right");
    } else if (strcmp(arrow_type, "leftarrow") == 0 || strcmp(arrow_type, "gets") == 0) {
        add_attribute_to_element(input, arrow_element, "direction", "left");
    } else if (strcmp(arrow_type, "uparrow") == 0) {
        add_attribute_to_element(input, arrow_element, "direction", "up");
    } else if (strcmp(arrow_type, "downarrow") == 0) {
        add_attribute_to_element(input, arrow_element, "direction", "down");
    } else if (strcmp(arrow_type, "leftrightarrow") == 0) {
        add_attribute_to_element(input, arrow_element, "direction", "both");
    }
    
    ((TypeElmt*)arrow_element->type)->content_length = ((List*)arrow_element)->length;
    return (Item)arrow_element;
}

// Parse over/under constructs: \overline{x}, \underline{x}, \overbrace{x}, \underbrace{x}
static Item parse_latex_overunder(Input *input, const char **math, const char* construct_type) {
    skip_math_whitespace(math);
    
    // expect opening brace
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse content
    Item content = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (content == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create over/under expression element
    Element* construct_element = create_math_element(input, construct_type);
    if (!construct_element) {
        return ITEM_ERROR;
    }
    
    // add content as child
    list_push((List*)construct_element, content);
    
    // Add position attribute
    if (strstr(construct_type, "over") != NULL) {
        add_attribute_to_element(input, construct_element, "position", "over");
    } else if (strstr(construct_type, "under") != NULL) {
        add_attribute_to_element(input, construct_element, "position", "under");
    }
    
    // set content length
    ((TypeElmt*)construct_element->type)->content_length = ((List*)construct_element)->length;
    
    return (Item)construct_element;
}

static MathFlavor get_math_flavor(const char* flavor_str) {
    if (!flavor_str || strcmp(flavor_str, "latex") == 0) {
        return MATH_FLAVOR_LATEX;
    } else if (strcmp(flavor_str, "typst") == 0) {
        return MATH_FLAVOR_TYPST;
    } else if (strcmp(flavor_str, "ascii") == 0) {
        return MATH_FLAVOR_ASCII;
    }
    return MATH_FLAVOR_LATEX; // default
}

// main parser function
void parse_math(Input* input, const char* math_string, const char* flavor_str) {
    input->sb = strbuf_new_pooled(input->pool);
    const char *math = math_string;
    
    MathFlavor flavor = get_math_flavor(flavor_str);
    
    // parse the math expression
    skip_math_whitespace(&math);
    Item result = parse_math_expression(input, &math, flavor);
    
    if (result == ITEM_ERROR || result == ITEM_NULL) {
        input->root = ITEM_ERROR;
        return;
    }
    
    input->root = result;
}
