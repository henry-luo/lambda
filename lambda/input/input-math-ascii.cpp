#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "lib/log.h"

using namespace lambda;

// Standalone ASCII Math parser
// Produces Lambda AST compliant with Math.md schema
// References: https://www1.chapman.edu/~jipsen/mathml/asciimathsyntax.html

// ASCII Math token types
typedef enum {
    ASCII_TOKEN_IDENTIFIER,     // x, y, variable names
    ASCII_TOKEN_NUMBER,         // 123, 45.67, -8.9
    ASCII_TOKEN_OPERATOR,       // +, -, *, /
    ASCII_TOKEN_FUNCTION,       // sin, cos, log, sqrt
    ASCII_TOKEN_SYMBOL,         // alpha, beta, pi, infinity
    ASCII_TOKEN_RELATION,       // =, <, >, <=, >=, !=
    ASCII_TOKEN_GROUPING,       // (, ), [, ], {, }
    ASCII_TOKEN_SPECIAL,        // ^, _, /
    ASCII_TOKEN_TEXT,           // "quoted text"
    ASCII_TOKEN_EOF
} ASCIITokenType;

typedef struct {
    ASCIITokenType type;
    const char* start;
    size_t length;
    const char* unicode_output;  // Unicode equivalent for rendering
} ASCIIToken;

// ASCII Math constants table
typedef struct {
    const char* ascii_input;     // "alpha", "beta", "sum"
    const char* unicode_output;  // "α", "β", "∑"
    const char* element_name;    // Lambda element name
    ASCIITokenType token_type;   // ASCII_TOKEN_SYMBOL
    bool is_function;           // true for sin, cos, etc.
} ASCIIConstant;

// ASCII Math constants based on official specification
static const ASCIIConstant ascii_constants[] = {
    // Greek letters
    {"alpha", "α", "alpha", ASCII_TOKEN_SYMBOL, false},
    {"beta", "β", "beta", ASCII_TOKEN_SYMBOL, false},
    {"gamma", "γ", "gamma", ASCII_TOKEN_SYMBOL, false},
    {"delta", "δ", "delta", ASCII_TOKEN_SYMBOL, false},
    {"epsilon", "ε", "epsilon", ASCII_TOKEN_SYMBOL, false},
    {"zeta", "ζ", "zeta", ASCII_TOKEN_SYMBOL, false},
    {"eta", "η", "eta", ASCII_TOKEN_SYMBOL, false},
    {"theta", "θ", "theta", ASCII_TOKEN_SYMBOL, false},
    {"iota", "ι", "iota", ASCII_TOKEN_SYMBOL, false},
    {"kappa", "κ", "kappa", ASCII_TOKEN_SYMBOL, false},
    {"lambda", "λ", "lambda", ASCII_TOKEN_SYMBOL, false},
    {"mu", "μ", "mu", ASCII_TOKEN_SYMBOL, false},
    {"nu", "ν", "nu", ASCII_TOKEN_SYMBOL, false},
    {"xi", "ξ", "xi", ASCII_TOKEN_SYMBOL, false},
    {"pi", "π", "pi", ASCII_TOKEN_SYMBOL, false},
    {"rho", "ρ", "rho", ASCII_TOKEN_SYMBOL, false},
    {"sigma", "σ", "sigma", ASCII_TOKEN_SYMBOL, false},
    {"tau", "τ", "tau", ASCII_TOKEN_SYMBOL, false},
    {"upsilon", "υ", "upsilon", ASCII_TOKEN_SYMBOL, false},
    {"phi", "φ", "phi", ASCII_TOKEN_SYMBOL, false},
    {"chi", "χ", "chi", ASCII_TOKEN_SYMBOL, false},
    {"psi", "ψ", "psi", ASCII_TOKEN_SYMBOL, false},
    {"omega", "ω", "omega", ASCII_TOKEN_SYMBOL, false},

    // Capital Greek letters
    {"Gamma", "Γ", "Gamma", ASCII_TOKEN_SYMBOL, false},
    {"Delta", "Δ", "Delta", ASCII_TOKEN_SYMBOL, false},
    {"Theta", "Θ", "Theta", ASCII_TOKEN_SYMBOL, false},
    {"Lambda", "Λ", "Lambda", ASCII_TOKEN_SYMBOL, false},
    {"Xi", "Ξ", "Xi", ASCII_TOKEN_SYMBOL, false},
    {"Pi", "Π", "Pi", ASCII_TOKEN_SYMBOL, false},
    {"Sigma", "Σ", "Sigma", ASCII_TOKEN_SYMBOL, false},
    {"Upsilon", "Υ", "Upsilon", ASCII_TOKEN_SYMBOL, false},
    {"Phi", "Φ", "Phi", ASCII_TOKEN_SYMBOL, false},
    {"Psi", "Ψ", "Psi", ASCII_TOKEN_SYMBOL, false},
    {"Omega", "Ω", "Omega", ASCII_TOKEN_SYMBOL, false},

    // Functions
    {"sin", "sin", "sin", ASCII_TOKEN_FUNCTION, true},
    {"cos", "cos", "cos", ASCII_TOKEN_FUNCTION, true},
    {"tan", "tan", "tan", ASCII_TOKEN_FUNCTION, true},
    {"cot", "cot", "cot", ASCII_TOKEN_FUNCTION, true},
    {"sec", "sec", "sec", ASCII_TOKEN_FUNCTION, true},
    {"csc", "csc", "csc", ASCII_TOKEN_FUNCTION, true},
    {"log", "log", "log", ASCII_TOKEN_FUNCTION, true},
    {"ln", "ln", "ln", ASCII_TOKEN_FUNCTION, true},
    {"exp", "exp", "exp", ASCII_TOKEN_FUNCTION, true},
    {"sqrt", "√", "sqrt", ASCII_TOKEN_FUNCTION, true},
    {"abs", "|", "abs", ASCII_TOKEN_FUNCTION, true},
    {"floor", "⌊", "floor", ASCII_TOKEN_FUNCTION, true},
    {"ceil", "⌈", "ceil", ASCII_TOKEN_FUNCTION, true},

    // Special constants
    {"oo", "∞", "infinity", ASCII_TOKEN_SYMBOL, false},
    {"infty", "∞", "infinity", ASCII_TOKEN_SYMBOL, false},
    {"infinity", "∞", "infinity", ASCII_TOKEN_SYMBOL, false},
    {"emptyset", "∅", "emptyset", ASCII_TOKEN_SYMBOL, false},

    // Operators
    {"+-", "±", "pm", ASCII_TOKEN_OPERATOR, false},
    {"-+", "∓", "mp", ASCII_TOKEN_OPERATOR, false},
    {"**", "∗", "ast", ASCII_TOKEN_OPERATOR, false},
    {"//", "/", "div", ASCII_TOKEN_OPERATOR, false},
    {"\\\\", "\\", "setminus", ASCII_TOKEN_OPERATOR, false},
    {"xx", "×", "times", ASCII_TOKEN_OPERATOR, false},
    {"-:", "÷", "div", ASCII_TOKEN_OPERATOR, false},
    {"@", "∘", "circ", ASCII_TOKEN_OPERATOR, false},
    {"o+", "⊕", "oplus", ASCII_TOKEN_OPERATOR, false},
    {"ox", "⊗", "otimes", ASCII_TOKEN_OPERATOR, false},
    {"o.", "⊙", "odot", ASCII_TOKEN_OPERATOR, false},

    // Relations
    {"=", "=", "eq", ASCII_TOKEN_RELATION, false},
    {"!=", "≠", "neq", ASCII_TOKEN_RELATION, false},
    {"<", "<", "lt", ASCII_TOKEN_RELATION, false},
    {">", ">", "gt", ASCII_TOKEN_RELATION, false},
    {"<=", "≤", "leq", ASCII_TOKEN_RELATION, false},
    {">=", "≥", "geq", ASCII_TOKEN_RELATION, false},
    {"-<", "≺", "prec", ASCII_TOKEN_RELATION, false},
    {">-", "≻", "succ", ASCII_TOKEN_RELATION, false},
    {"in", "∈", "in", ASCII_TOKEN_RELATION, false},
    {"!in", "∉", "notin", ASCII_TOKEN_RELATION, false},
    {"sub", "⊂", "subset", ASCII_TOKEN_RELATION, false},
    {"sup", "⊃", "supset", ASCII_TOKEN_RELATION, false},
    {"sube", "⊆", "subseteq", ASCII_TOKEN_RELATION, false},
    {"supe", "⊇", "supseteq", ASCII_TOKEN_RELATION, false},
    {"-=", "≡", "equiv", ASCII_TOKEN_RELATION, false},
    {"~=", "≅", "cong", ASCII_TOKEN_RELATION, false},
    {"~~", "≈", "approx", ASCII_TOKEN_RELATION, false},
    {"prop", "∝", "propto", ASCII_TOKEN_RELATION, false},

    // Big operators
    {"sum", "∑", "sum", ASCII_TOKEN_FUNCTION, true},
    {"prod", "∏", "prod", ASCII_TOKEN_FUNCTION, true},
    {"int", "∫", "int", ASCII_TOKEN_FUNCTION, true},
    {"oint", "∮", "oint", ASCII_TOKEN_FUNCTION, true},
    {"lim", "lim", "lim", ASCII_TOKEN_FUNCTION, true},

    // Arrows
    {"->", "→", "to", ASCII_TOKEN_OPERATOR, false},
    {"<-", "←", "leftarrow", ASCII_TOKEN_OPERATOR, false},
    {"<->", "↔", "leftrightarrow", ASCII_TOKEN_OPERATOR, false},
    {"|->", "↦", "mapsto", ASCII_TOKEN_OPERATOR, false},

    {NULL, NULL, NULL, ASCII_TOKEN_EOF, false}  // Sentinel
};

// Forward declarations
static ASCIIToken* ascii_tokenize(const char* input, size_t* token_count);
static Item parse_ascii_expression(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count);
static Item parse_ascii_simple_expression(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count);
static const ASCIIConstant* find_ascii_constant(const char* text, size_t length);
static void skip_ascii_whitespace(const char** text);

// Local helper functions to replace macros
static inline Element* create_math_element(Input* input, const char* tag_name) {
    MarkBuilder builder(input);
    return builder.element(tag_name).final().element;
}

static inline void add_attribute_to_element(Input* input, Element* element, const char* attr_name, const char* attr_value) {
    MarkBuilder builder(input);
    String* key = builder.createString(attr_name);
    String* value = builder.createString(attr_value);
    if (!key || !value) return;
    Item lambda_value = {.item = s2it(value)};
    builder.putToElement(element, key, lambda_value);
}

// Skip whitespace in ASCII math input
static void skip_ascii_whitespace(const char** text) {
    while (**text && isspace(**text)) {
        (*text)++;
    }
}

// Find ASCII constant by longest matching initial substring
static const ASCIIConstant* find_ascii_constant(const char* text, size_t length) {
    const ASCIIConstant* best_match = NULL;
    size_t best_length = 0;

    for (const ASCIIConstant* constant = ascii_constants; constant->ascii_input; constant++) {
        size_t const_len = strlen(constant->ascii_input);
        if (const_len <= length && const_len > best_length) {
            if (strncmp(text, constant->ascii_input, const_len) == 0) {
                best_match = constant;
                best_length = const_len;
            }
        }
    }

    return best_match;
}

// Tokenize ASCII math input
static ASCIIToken* ascii_tokenize(const char* input, size_t* token_count) {
    if (!input || !token_count) {
        return NULL;
    }

    // First pass: count tokens
    const char* p = input;
    size_t count = 0;

    while (*p) {
        skip_ascii_whitespace(&p);
        if (!*p) break;

        // Try to match constants first (longest match)
        const ASCIIConstant* constant = find_ascii_constant(p, strlen(p));
        if (constant) {
            p += strlen(constant->ascii_input);
            count++;
            continue;
        }

        // Handle numbers
        if (isdigit(*p) || (*p == '.' && isdigit(*(p+1)))) {
            while (isdigit(*p) || *p == '.') {
                p++;
            }
            count++;
            continue;
        }

        // Handle identifiers - single characters only for implicit multiplication
        if (isalpha(*p)) {
            p++; // Only consume one character at a time
            count++;
            continue;
        }

        // Handle quoted text
        if (*p == '"') {
            p++; // skip opening quote
            while (*p && *p != '"') {
                p++;
            }
            if (*p == '"') p++; // skip closing quote
            count++;
            continue;
        }

        // Handle single character tokens
        if (strchr("()[]{}^_+-*/=<>!,", *p)) {
            p++;
            count++;
            continue;
        }

        // Skip unknown characters
        p++;
    }

    // Allocate token array
    ASCIIToken* tokens = (ASCIIToken*)malloc((count + 1) * sizeof(ASCIIToken));
    if (!tokens) {
        *token_count = 0;
        return NULL;
    }

    // Second pass: create tokens
    p = input;
    size_t token_idx = 0;

    while (*p && token_idx < count) {
        skip_ascii_whitespace(&p);
        if (!*p) break;

        ASCIIToken* token = &tokens[token_idx];
        token->start = p;

        // Try to match constants first
        const ASCIIConstant* constant = find_ascii_constant(p, strlen(p));
        if (constant) {
            token->type = constant->token_type;
            token->length = strlen(constant->ascii_input);
            token->unicode_output = constant->unicode_output;
            p += token->length;
            token_idx++;
            continue;
        }

        // Handle numbers
        if (isdigit(*p) || (*p == '.' && isdigit(*(p+1)))) {
            token->type = ASCII_TOKEN_NUMBER;
            const char* start = p;
            while (isdigit(*p) || *p == '.') {
                p++;
            }
            token->length = p - start;
            token->unicode_output = NULL;
            token_idx++;
            continue;
        }

        // Handle identifiers - single characters only for implicit multiplication
        if (isalpha(*p)) {
            token->type = ASCII_TOKEN_IDENTIFIER;
            const char* start = p;
            p++; // Only consume one character at a time
            token->length = p - start;
            token->unicode_output = NULL;
            token_idx++;
            continue;
        }

        // Handle quoted text
        if (*p == '"') {
            token->type = ASCII_TOKEN_TEXT;
            const char* start = p;
            p++; // skip opening quote
            while (*p && *p != '"') {
                p++;
            }
            if (*p == '"') p++; // skip closing quote
            token->length = p - start;
            token->unicode_output = NULL;
            token_idx++;
            continue;
        }

        // Handle grouping characters
        if (strchr("()[]{}", *p)) {
            token->type = ASCII_TOKEN_GROUPING;
            token->length = 1;
            token->unicode_output = NULL;
            p++;
            token_idx++;
            continue;
        }

        // Handle special characters
        if (strchr("^_", *p)) {
            token->type = ASCII_TOKEN_SPECIAL;
            token->length = 1;
            token->unicode_output = NULL;
            p++;
            token_idx++;
            continue;
        }

        // Handle operators and relations
        if (strchr("+-*/=<>!,", *p)) {
            token->type = (*p == '=' || *p == '<' || *p == '>' || *p == '!') ?
                         ASCII_TOKEN_RELATION : ASCII_TOKEN_OPERATOR;
            token->length = 1;
            token->unicode_output = NULL;
            p++;
            token_idx++;
            continue;
        }

        // Skip unknown characters
        p++;
    }

    // Add EOF token
    if (token_idx < count + 1) {
        tokens[token_idx].type = ASCII_TOKEN_EOF;
        tokens[token_idx].start = p;
        tokens[token_idx].length = 0;
        tokens[token_idx].unicode_output = NULL;
    }

    *token_count = token_idx;
    return tokens;
}

// Parse simple expression (constants, bracketed expressions, unary operations)
static Item parse_ascii_simple_expression(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count) {
    Input* input = ctx.input();
    MarkBuilder& builder = ctx.builder;

    if (*pos >= token_count || tokens[*pos].type == ASCII_TOKEN_EOF) {
        return {.item = ITEM_ERROR};
    }

    ASCIIToken* token = &tokens[*pos];

    // Handle numbers
    if (token->type == ASCII_TOKEN_NUMBER) {
        String* number_string = builder.createString(token->start, token->length);
        (*pos)++;
        return {.item = s2it(number_string)};
    }

    // Handle identifiers and symbols
    if (token->type == ASCII_TOKEN_IDENTIFIER || token->type == ASCII_TOKEN_SYMBOL) {
        String* name_string = builder.createString(token->start, token->length);
        (*pos)++;
        return {.item = y2it(name_string)};
    }

    // Handle functions
    if (token->type == ASCII_TOKEN_FUNCTION) {
        // Use stack buffer for function name (all function names < 32 chars)
        char func_name_buf[32];
        size_t name_len = token->length < 31 ? token->length : 31;
        memcpy(func_name_buf, token->start, name_len);
        func_name_buf[name_len] = '\0';

        // Find the corresponding constant to get element name
        const ASCIIConstant* constant = find_ascii_constant(token->start, token->length);
        const char* element_name = constant ? constant->element_name : func_name_buf;

        Element* func_element = create_math_element(input, element_name);
        if (!func_element) {
            return {.item = ITEM_ERROR};
        }

        add_attribute_to_element(input, func_element, "type", "function");

        (*pos)++;

        // Special handling for sum/prod/int/lim with bounds notation: sum_(lower)^upper summand
        if (strcmp(element_name, "sum") == 0 || strcmp(element_name, "prod") == 0 ||
            strcmp(element_name, "int") == 0 || strcmp(element_name, "oint") == 0 ||
            strcmp(element_name, "lim") == 0) {

            // Check for subscript (lower bound)
            if (*pos < token_count && tokens[*pos].type == ASCII_TOKEN_SPECIAL &&
                tokens[*pos].start[0] == '_') {
                (*pos)++; // skip _

                Item lower_bound = parse_ascii_simple_expression(ctx, tokens, pos, token_count);
                if (lower_bound.item != ITEM_ERROR) {
                    list_push((List*)func_element, lower_bound);
                }

                // Check for superscript (upper bound)
                if (*pos < token_count && tokens[*pos].type == ASCII_TOKEN_SPECIAL &&
                    tokens[*pos].start[0] == '^') {
                    (*pos)++; // skip ^

                    Item upper_bound = parse_ascii_simple_expression(ctx, tokens, pos, token_count);
                    if (upper_bound.item != ITEM_ERROR) {
                        list_push((List*)func_element, upper_bound);
                    }
                }

                // Parse summand/integrand (the expression after the bounds)
                if (*pos < token_count) {
                    Item summand = parse_ascii_simple_expression(ctx, tokens, pos, token_count);
                    if (summand.item != ITEM_ERROR) {
                        list_push((List*)func_element, summand);
                    }
                }
            } else {
                // Regular function argument parsing for functions without bounds
                if (*pos < token_count && tokens[*pos].type == ASCII_TOKEN_GROUPING &&
                    tokens[*pos].start[0] == '(') {
                    (*pos)++; // skip (

                    Item arg = parse_ascii_expression(ctx, tokens, pos, token_count);
                    if (arg.item != ITEM_ERROR) {
                        list_push((List*)func_element, arg);
                    }

                    // Skip closing )
                    if (*pos < token_count && tokens[*pos].type == ASCII_TOKEN_GROUPING &&
                        tokens[*pos].start[0] == ')') {
                        (*pos)++;
                    }
                }
            }
        } else {
            // Regular function argument parsing for other functions
            if (*pos < token_count && tokens[*pos].type == ASCII_TOKEN_GROUPING &&
                tokens[*pos].start[0] == '(') {
                (*pos)++; // skip (

                Item arg = parse_ascii_expression(ctx, tokens, pos, token_count);
                if (arg.item != ITEM_ERROR) {
                    list_push((List*)func_element, arg);
                }

                // Skip closing )
                if (*pos < token_count && tokens[*pos].type == ASCII_TOKEN_GROUPING &&
                    tokens[*pos].start[0] == ')') {
                    (*pos)++;
                }
            }
        }

        // Set content length
        ((TypeElmt*)func_element->type)->content_length = ((List*)func_element)->length;

        return {.item = (uint64_t)func_element};
    }

    // Handle parentheses
    if (token->type == ASCII_TOKEN_GROUPING && token->start[0] == '(') {
        (*pos)++; // skip (
        Item expr = parse_ascii_expression(ctx, tokens, pos, token_count);

        // Skip closing )
        if (*pos < token_count && tokens[*pos].type == ASCII_TOKEN_GROUPING &&
            tokens[*pos].start[0] == ')') {
            (*pos)++;
        }

        return expr;
    }

    return {.item = ITEM_ERROR};
}

// Forward declarations for precedence levels
static Item parse_ascii_relation(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count);
static Item parse_ascii_addition(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count);
static Item parse_ascii_multiplication(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count);
static Item parse_ascii_power(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count);

// Parse full ASCII math expression (lowest precedence - relations)
static Item parse_ascii_expression(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count) {
    return parse_ascii_relation(ctx, tokens, pos, token_count);
}

// Parse relations (=, <, >, etc.) - lowest precedence
static Item parse_ascii_relation(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count) {
    Input* input = ctx.input();

    Item left = parse_ascii_addition(ctx, tokens, pos, token_count);
    if (left.item == ITEM_ERROR) {
        return left;
    }

    while (*pos < token_count && tokens[*pos].type == ASCII_TOKEN_RELATION) {
        ASCIIToken* op_token = &tokens[*pos];
        const char* rel_name = NULL;

        if (op_token->start[0] == '=' && op_token->length == 1) rel_name = "eq";
        else if (op_token->start[0] == '!' && op_token->length == 2 && op_token->start[1] == '=') rel_name = "neq";
        else if (op_token->start[0] == '<' && op_token->length == 2 && op_token->start[1] == '=') rel_name = "leq";
        else if (op_token->start[0] == '>' && op_token->length == 2 && op_token->start[1] == '=') rel_name = "geq";
        else if (op_token->start[0] == '<' && op_token->length == 1) rel_name = "lt";
        else if (op_token->start[0] == '>' && op_token->length == 1) rel_name = "gt";

        if (rel_name) {
            (*pos)++; // skip relation
            Item right = parse_ascii_addition(ctx, tokens, pos, token_count);
            if (right.item == ITEM_ERROR) {
                return right;
            }

            Element* rel_element = create_math_element(input, rel_name);
            if (!rel_element) {
                return {.item = ITEM_ERROR};
            }

            add_attribute_to_element(input, rel_element, "type", "relation");
            list_push((List*)rel_element, left);
            list_push((List*)rel_element, right);
            ((TypeElmt*)rel_element->type)->content_length = ((List*)rel_element)->length;

            left = {.item = (uint64_t)rel_element};
        } else {
            break;
        }
    }

    return left;
}

// Parse addition and subtraction
static Item parse_ascii_addition(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count) {
    Input* input = ctx.input();

    Item left = parse_ascii_multiplication(ctx, tokens, pos, token_count);
    if (left.item == ITEM_ERROR) {
        return left;
    }

    while (*pos < token_count && tokens[*pos].type == ASCII_TOKEN_OPERATOR) {
        ASCIIToken* op_token = &tokens[*pos];
        const char* op_name = NULL;

        if (op_token->start[0] == '+') op_name = "add";
        else if (op_token->start[0] == '-') op_name = "sub";
        else break; // Not addition/subtraction

        (*pos)++; // skip operator
        Item right = parse_ascii_multiplication(ctx, tokens, pos, token_count);
        if (right.item == ITEM_ERROR) {
            return right;
        }

        Element* op_element = create_math_element(input, op_name);
        if (!op_element) {
            return {.item = ITEM_ERROR};
        }

        add_attribute_to_element(input, op_element, "type", "binary_op");
        list_push((List*)op_element, left);
        list_push((List*)op_element, right);
        ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;

        left = {.item = (uint64_t)op_element};
    }

    return left;
}

// Parse multiplication, division, and implicit multiplication
static Item parse_ascii_multiplication(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count) {
    Input* input = ctx.input();

    Item left = parse_ascii_power(ctx, tokens, pos, token_count);
    if (left.item == ITEM_ERROR) {
        return left;
    }

    while (*pos < token_count) {
        ASCIIToken* op_token = &tokens[*pos];

        // Handle explicit multiplication and division
        if (op_token->type == ASCII_TOKEN_OPERATOR &&
            (op_token->start[0] == '*' || op_token->start[0] == '/')) {
            const char* op_name = (op_token->start[0] == '*') ? "mul" : "div";
            (*pos)++; // skip operator

            Item right = parse_ascii_power(ctx, tokens, pos, token_count);
            if (right.item == ITEM_ERROR) {
                return right;
            }

            Element* op_element = create_math_element(input, op_name);
            if (!op_element) {
                return {.item = ITEM_ERROR};
            }

            add_attribute_to_element(input, op_element, "type", "binary_op");
            list_push((List*)op_element, left);
            list_push((List*)op_element, right);
            ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;

            left = {.item = (uint64_t)op_element};
            continue;
        }

        // Handle implicit multiplication between adjacent identifiers/numbers
        if (op_token->type == ASCII_TOKEN_IDENTIFIER || op_token->type == ASCII_TOKEN_NUMBER) {
            Item right = parse_ascii_power(ctx, tokens, pos, token_count);
            if (right.item == ITEM_ERROR) {
                return right;
            }

            Element* mul_element = create_math_element(input, "implicit_mul");
            if (!mul_element) {
                return {.item = ITEM_ERROR};
            }

            add_attribute_to_element(input, mul_element, "type", "binary_op");
            list_push((List*)mul_element, left);
            list_push((List*)mul_element, right);
            ((TypeElmt*)mul_element->type)->content_length = ((List*)mul_element)->length;

            left = {.item = (uint64_t)mul_element};
            continue;
        }

        break;
    }

    return left;
}

// Parse power operations (highest precedence)
static Item parse_ascii_power(InputContext& ctx, ASCIIToken* tokens, size_t* pos, size_t token_count) {
    Input* input = ctx.input();

    Item left = parse_ascii_simple_expression(ctx, tokens, pos, token_count);
    if (left.item == ITEM_ERROR) {
        return left;
    }

    while (*pos < token_count && (tokens[*pos].type == ASCII_TOKEN_SPECIAL ||
                                  (tokens[*pos].type == ASCII_TOKEN_OPERATOR &&
                                   tokens[*pos].length == 2 &&
                                   tokens[*pos].start[0] == '*' && tokens[*pos].start[1] == '*'))) {
        ASCIIToken* op_token = &tokens[*pos];

        // Handle subscript (_) - higher precedence, parse first
        if (op_token->start[0] == '_') {
            (*pos)++; // skip _
            Item right = parse_ascii_simple_expression(ctx, tokens, pos, token_count);
            if (right.item == ITEM_ERROR) {
                return right;
            }

            Element* sub_element = create_math_element(input, "subscript");
            if (!sub_element) {
                return {.item = ITEM_ERROR};
            }

            add_attribute_to_element(input, sub_element, "type", "binary_op");
            list_push((List*)sub_element, left);
            list_push((List*)sub_element, right);
            ((TypeElmt*)sub_element->type)->content_length = ((List*)sub_element)->length;

            left = {.item = (uint64_t)sub_element};

            // After subscript, check for power
            if (*pos < token_count &&
                ((tokens[*pos].type == ASCII_TOKEN_SPECIAL && tokens[*pos].start[0] == '^') ||
                 (tokens[*pos].type == ASCII_TOKEN_OPERATOR && tokens[*pos].length == 2 &&
                  tokens[*pos].start[0] == '*' && tokens[*pos].start[1] == '*'))) {
                (*pos)++; // skip ^
                Item power = parse_ascii_simple_expression(ctx, tokens, pos, token_count);
                if (power.item == ITEM_ERROR) {
                    return power;
                }

                Element* pow_element = create_math_element(input, "pow");
                if (!pow_element) {
                    return {.item = ITEM_ERROR};
                }

                add_attribute_to_element(input, pow_element, "type", "binary_op");
                list_push((List*)pow_element, left);
                list_push((List*)pow_element, power);
                ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;

                left = {.item = (uint64_t)pow_element};
            }
            continue;
        }

        // Handle power (^ or **) without subscript
        if (op_token->start[0] == '^' ||
            (op_token->length == 2 && op_token->start[0] == '*' && op_token->start[1] == '*')) {
            (*pos)++; // skip ^
            Item right = parse_ascii_simple_expression(ctx, tokens, pos, token_count);
            if (right.item == ITEM_ERROR) {
                return right;
            }

            Element* pow_element = create_math_element(input, "pow");
            if (!pow_element) {
                return {.item = ITEM_ERROR};
            }

            add_attribute_to_element(input, pow_element, "type", "binary_op");
            list_push((List*)pow_element, left);
            list_push((List*)pow_element, right);
            ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;

            left = {.item = (uint64_t)pow_element};
            continue;
        }

        break; // No more power operations
    }

    return left;
}

// Main entry point for ASCII math parsing
Item parse_ascii_math(Input* input, const char* math_text) {
    if (!input || !math_text) {
        return {.item = ITEM_ERROR};
    }

    // create unified InputContext with source tracking
    InputContext ctx(input, math_text, strlen(math_text));

    log_debug("DEBUG: ASCII math parsing: '%s'\n", math_text);

    size_t token_count;
    ASCIIToken* tokens = ascii_tokenize(math_text, &token_count);
    if (!tokens) {
        ctx.addError(ctx.tracker.location(), "Failed to tokenize ASCII math expression");
        log_debug("DEBUG: Tokenization failed\n");
        return {.item = ITEM_ERROR};
    }

    log_debug("DEBUG: Tokenized into %zu tokens\n", token_count);
    for (size_t i = 0; i < token_count; i++) {
        log_debug("DEBUG: Token %zu: type=%d, text='%.*s'\n", i, tokens[i].type, (int)tokens[i].length, tokens[i].start);
    }

    // Parse expression
    size_t pos = 0;
    log_debug("DEBUG: About to parse ASCII expression\n");
    Item result = parse_ascii_expression(ctx, tokens, &pos, token_count);
    log_debug("DEBUG: Parse result: item=0x%lx\n", result.item);

    if (ctx.hasErrors()) {
        ctx.logErrors();
    }

    // Clean up
    free(tokens);
    return result;
}

// Entry point for ASCII math parsing (called from input.cpp)
Item input_ascii_math(Input* input, const char* ascii_math) {
    return parse_ascii_math(input, ascii_math);
}
