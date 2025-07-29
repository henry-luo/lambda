#include "input.h"
#include "input-common.h"

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
static Item parse_ascii_power(Input *input, const char **math, MathFlavor flavor, Item base);
static Item parse_math_number(Input *input, const char **math);
static Item parse_math_identifier(Input *input, const char **math);
static Item create_binary_expr(Input *input, const char* op_name, Item left, Item right);
static void skip_math_whitespace(const char **math);

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
    if (**math == '-') {
        strbuf_append_char(sb, **math);
        (*math)++;
    }
    
    // parse digits before decimal point
    while (**math && isdigit(**math)) {
        strbuf_append_char(sb, **math);
        (*math)++;
    }
    
    // parse decimal point and digits after
    if (**math == '.') {
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
    strbuf_full_reset(sb);
    
    return (Item)s2it(num_string);
}

// parse identifier/variable name
static Item parse_math_identifier(Input *input, const char **math) {
    StrBuf* sb = input->sb;
    strbuf_full_reset(sb);
    
    // parse letters and digits
    while (**math && (isalpha(**math) || isdigit(**math))) {
        strbuf_append_char(sb, **math);
        (*math)++;
    }
    
    if (sb->length <= sizeof(uint32_t)) {
        strbuf_full_reset(sb);
        return ITEM_ERROR;
    }
    
    String *id_string = (String*)sb->str;
    id_string->len = sb->length - sizeof(uint32_t);
    id_string->ref_cnt = 0;
    strbuf_full_reset(sb);
    
    return (Item)s2it(id_string);
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
    Element* frac_element = create_math_element(input, "expr");
    if (!frac_element) {
        return ITEM_ERROR;
    }
    
    // add op attribute
    add_attribute_to_element(input, frac_element, "op", "frac");
    
    // add numerator and denominator as children
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
    Element* sqrt_element = create_math_element(input, "expr");
    if (!sqrt_element) {
        return ITEM_ERROR;
    }
    
    // add op attribute
    add_attribute_to_element(input, sqrt_element, "op", "sqrt");
    
    // add inner expression as child
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
    Element* pow_element = create_math_element(input, "expr");
    if (!pow_element) {
        return ITEM_ERROR;
    }
    
    // add op attribute
    add_attribute_to_element(input, pow_element, "op", "pow");
    
    // add base and exponent as children
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
    Element* sub_element = create_math_element(input, "expr");
    if (!sub_element) {
        return ITEM_ERROR;
    }
    
    // add op attribute
    add_attribute_to_element(input, sub_element, "op", "sub");
    
    // add base and subscript as children
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
    } else if (is_trig_function(cmd_string->chars) || is_log_function(cmd_string->chars)) {
        const char* func_name = cmd_string->chars;
        strbuf_full_reset(sb);
        return parse_latex_function(input, math, func_name);
    } else if (is_greek_letter(cmd_string->chars) || is_math_operator(cmd_string->chars)) {
        // Greek letters and math operators are treated as symbols
        strbuf_full_reset(sb);
        return (Item)s2it(cmd_string);
    }
    
    // for other commands, return as identifier for now
    // Unknown LaTeX command
    strbuf_full_reset(sb);
    return (Item)s2it(cmd_string);
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
    Element* pow_element = create_math_element(input, "expr");
    if (!pow_element) {
        return ITEM_ERROR;
    }
    
    // add op attribute
    add_attribute_to_element(input, pow_element, "op", "pow");
    
    // add base and exponent as children
    list_push((List*)pow_element, base);
    list_push((List*)pow_element, exponent);
    
    // set content length
    ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;
    
    return (Item)pow_element;
}

// parse typst fraction using / operator
static Item parse_typst_fraction(Input *input, const char **math, MathFlavor flavor) {
    // In Typst, fractions are just division with / operator
    // This function shouldn't be called directly, as fractions are handled by the division operator
    return ITEM_ERROR;
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
    Element* pow_element = create_math_element(input, "expr");
    if (!pow_element) {
        return ITEM_ERROR;
    }
    
    // add op attribute
    add_attribute_to_element(input, pow_element, "op", "pow");
    
    // add base and exponent as children
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
    }
    
    return ITEM_ERROR;
}

// parse binary operation
static Item create_binary_expr(Input *input, const char* op_name, Item left, Item right) {
    Element* expr_element = create_math_element(input, "expr");
    if (!expr_element) {
        return ITEM_ERROR;
    }
    
    // add op attribute
    add_attribute_to_element(input, expr_element, "op", op_name);
    
    // add operands as children
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
    Item left = parse_primary_with_postfix(input, math, flavor);
    if (left == ITEM_ERROR || left == ITEM_NULL) {
        return left;
    }
    
    skip_math_whitespace(math);
    
    while (**math && (**math == '*' || **math == '/')) {
        char op = **math;
        const char* op_name = (op == '*') ? "mul" : "div";
        
        (*math)++; // skip operator
        skip_math_whitespace(math);
        
        Item right = parse_primary_with_postfix(input, math, flavor);
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

// parse primary expression with postfix operators (superscript, subscript)
static Item parse_primary_with_postfix(Input *input, const char **math, MathFlavor flavor) {
    Item left = parse_math_primary(input, math, flavor);
    if (left == ITEM_ERROR || left == ITEM_NULL) {
        return left;
    }
    
    skip_math_whitespace(math);
    
    // handle postfix operators (superscript, subscript)
    if (flavor == MATH_FLAVOR_LATEX) {
        if (**math == '^') {
            (*math)++; // skip ^
            left = parse_latex_superscript(input, math, left);
            if (left == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            skip_math_whitespace(math);
        }
        
        if (**math == '_') {
            (*math)++; // skip _
            left = parse_latex_subscript(input, math, left);
            if (left == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            skip_math_whitespace(math);
        }
    } else if (flavor == MATH_FLAVOR_TYPST) {
        if (**math == '^') {
            (*math)++; // skip ^
            left = parse_typst_power(input, math, flavor, left);
            if (left == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            skip_math_whitespace(math);
        }
    } else if (flavor == MATH_FLAVOR_ASCII) {
        if (**math == '^') {
            (*math)++; // skip ^
            left = parse_ascii_power(input, math, flavor, left);
            if (left == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            skip_math_whitespace(math);
        } else if (**math == '*' && *(*math + 1) == '*') {
            // handle ** power operator
            left = parse_ascii_power(input, math, flavor, left);
            if (left == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            skip_math_whitespace(math);
        }
    }
    
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
    Element* func_element = create_math_element(input, "expr");
    if (!func_element) {
        return ITEM_ERROR;
    }
    
    // add op attribute
    add_attribute_to_element(input, func_element, "op", func_name);
    
    // add argument as child
    list_push((List*)func_element, arg);
    
    return (Item)func_element;
}

// Parse LaTeX sum or product with limits: \sum_{i=1}^{n} or \prod_{i=0}^{n}
static Item parse_latex_sum_or_prod(Input *input, const char **math, const char* op_name) {
    skip_math_whitespace(math);
    
    // Create the sum/prod element
    Element* op_element = create_math_element(input, "expr");
    if (!op_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, op_element, "op", op_name);
    
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
    Element* int_element = create_math_element(input, "expr");
    if (!int_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, int_element, "op", "int");
    
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
    Element* lim_element = create_math_element(input, "expr");
    if (!lim_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, lim_element, "op", "lim");
    
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
    Element* matrix_element = create_math_element(input, "expr");
    if (!matrix_element) {
        printf("ERROR: Failed to create matrix element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, matrix_element, "op", matrix_type);
    
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
    Element* matrix_element = create_math_element(input, "expr");
    if (!matrix_element) {
        printf("ERROR: Failed to create matrix environment element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, matrix_element, "op", matrix_type);
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
