// latex_expr_eval.cpp - LaTeX numeric expression evaluator
// Implements recursive descent parser for counter arithmetic expressions
// Grammar:
//   num_expr   → num_term ((+|-) num_term)*
//   num_term   → num_factor ((*|/) num_factor)*
//   num_factor → (+|-)? num_value
//   num_value  → "(" num_expr ")" | integer

#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include <ctype.h>
#include <string.h>

// forward declarations
static double parse_num_expr(const char** p);
static double parse_num_term(const char** p);
static double parse_num_factor(const char** p);
static double parse_num_value(const char** p);
static void skip_whitespace(const char** p);

// skip whitespace characters
static void skip_whitespace(const char** p) {
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

// parse primary value: number (float or integer) or parenthesized expression
// num_value → "(" num_expr ")" | number
static double parse_num_value(const char** p) {
    skip_whitespace(p);
    
    // parenthesized expression
    if (**p == '(') {
        (*p)++;
        double result = parse_num_expr(p);
        skip_whitespace(p);
        if (**p == ')') {
            (*p)++;
        }
        return result;
    }
    
    // number literal (integer or float)
    if (isdigit((unsigned char)**p) || **p == '.') {
        char* endptr;
        double value = strtod(*p, &endptr);
        *p = endptr;
        return value;
    }
    
    // no valid value found, return 0
    return 0;
}

// parse factor with optional unary signs (handles multiple: --, ---, etc.)
// num_factor → (+|-)* num_value
static double parse_num_factor(const char** p) {
    skip_whitespace(p);
    
    // handle multiple unary + or - signs
    double sign = 1.0;
    while (**p == '+' || **p == '-') {
        if (**p == '-') {
            sign *= -1.0;
        }
        (*p)++;
        skip_whitespace(p);
    }
    
    double value = parse_num_value(p);
    return sign * value;
}

// parse term with multiplication and division
// num_term → num_factor ((*|/) num_factor)*
// NOTE: LaTeX.js truncates after EACH operation, not just at the end!
static double parse_num_term(const char** p) {
    double result = parse_num_factor(p);
    
    while (true) {
        skip_whitespace(p);
        char op = **p;
        
        if (op == '*') {
            (*p)++;
            double rhs = parse_num_factor(p);
            result = (int)(result * rhs);  // Truncate after each multiplication
        } else if (op == '/') {
            (*p)++;
            double rhs = parse_num_factor(p);
            if (rhs != 0.0) {
                result = (int)(result / rhs);  // Truncate after each division
            }
            // note: division by zero leaves result unchanged
        } else {
            break;
        }
    }
    
    return result;
}

// parse expression with addition and subtraction
// num_expr → num_term ((+|-) num_term)*
static double parse_num_expr(const char** p) {
    double result = parse_num_term(p);
    
    while (true) {
        skip_whitespace(p);
        char op = **p;
        
        if (op == '+') {
            (*p)++;
            double rhs = parse_num_term(p);
            result = result + rhs;
        } else if (op == '-') {
            (*p)++;
            double rhs = parse_num_term(p);
            result = result - rhs;
        } else {
            break;
        }
    }
    
    return result;
}

// public API: evaluate a LaTeX numeric expression
// examples:
//   "42"           → 42
//   "10 + 5"       → 15
//   "3 * -(2+1)"   → -9
//   "20 / 4"       → 5
//   "5 * 2 + 3"    → 13
extern "C" int latex_eval_num_expr(const char* expr) {
    if (!expr) return 0;
    
    // debug: write to file
    FILE* debugf = fopen("/tmp/latex_debug.txt", "a");
    if (debugf) {
        fprintf(debugf, "latex_eval_num_expr: input='%s'\n", expr);
        fclose(debugf);
    }
    
    const char* p = expr;
    double result = parse_num_expr(&p);
    int int_result = (int)result;  // Truncate to integer
    
    debugf = fopen("/tmp/latex_debug.txt", "a");
    if (debugf) {
        fprintf(debugf, "latex_eval_num_expr: result=%f, int_result=%d\n", result, int_result);
        fclose(debugf);
    }
    
    return int_result;
}
