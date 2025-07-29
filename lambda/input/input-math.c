#include "input.h"

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

// use common utility functions from input.c
#define create_math_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element

// skip whitespace helper
static void skip_math_whitespace(const char **math) {
    int whitespace_count = 0;
    const int max_whitespace = 1000; // safety limit
    
    while (**math && (**math == ' ' || **math == '\n' || **math == '\r' || **math == '\t') && 
           whitespace_count < max_whitespace) {
        (*math)++;
        whitespace_count++;
    }
    
    if (whitespace_count >= max_whitespace) {
        printf("WARNING: Hit whitespace limit, possible infinite loop in skip_math_whitespace\n");
    }
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

// parse latex command starting with backslash
static Item parse_latex_command(Input *input, const char **math) {
    if (**math != '\\') {
        return ITEM_ERROR;
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
        return ITEM_ERROR;
    }
    
    String *cmd_string = (String*)sb->str;
    cmd_string->len = sb->length - sizeof(uint32_t);
    cmd_string->ref_cnt = 0;
    
    // handle specific commands
    if (strcmp(cmd_string->chars, "frac") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_frac(input, math);
    } else if (strcmp(cmd_string->chars, "sqrt") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sqrt(input, math);
    } else if (strcmp(cmd_string->chars, "sin") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_function(input, math, "sin");
    } else if (strcmp(cmd_string->chars, "cos") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_function(input, math, "cos");
    } else if (strcmp(cmd_string->chars, "tan") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_function(input, math, "tan");
    } else if (strcmp(cmd_string->chars, "log") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_function(input, math, "log");
    } else if (strcmp(cmd_string->chars, "ln") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_function(input, math, "ln");
    }
    
    // for other commands, return as identifier for now
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
    printf("DEBUG: Starting math parsing with flavor: %s\n", flavor_str ? flavor_str : "latex");
    
    input->sb = strbuf_new_pooled(input->pool);
    const char *math = math_string;
    
    MathFlavor flavor = get_math_flavor(flavor_str);
    
    // parse the math expression
    skip_math_whitespace(&math);
    Item result = parse_math_expression(input, &math, flavor);
    
    if (result == ITEM_ERROR || result == ITEM_NULL) {
        printf("DEBUG: Math parsing failed\n");
        input->root = ITEM_ERROR;
        return;
    }
    
    input->root = result;
    printf("DEBUG: Math parsing completed successfully\n");
}
