// bash_arith.h — Runtime String-Based Arithmetic Evaluator (Phase C — Module 6)
//
// Provides runtime evaluation of arithmetic expressions from strings,
// complementing the MIR-compiled arithmetic for static expressions.
// Needed for: let "expr", declare -i var="expr", arr[expr], eval contexts.

#ifndef BASH_ARITH_H
#define BASH_ARITH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// Evaluate an arithmetic expression string, return the integer result as Item.
// Supports: +, -, *, /, %, **, <<, >>, &, |, ^, ~, !, &&, ||,
//           ==, !=, <, <=, >, >=, ? :, ++, --, variable references.
// Returns i2it(0) on error (and sets exit code to 1).
Item bash_arith_eval_string(Item expr_item);

// Evaluate an integer from a string expression (for array subscripts, etc.)
// Returns the integer value directly.
long long bash_arith_eval_to_int(const char* expr);

// Error state from last evaluation
int bash_arith_get_error(void);
const char* bash_arith_get_error_msg(void);

#ifdef __cplusplus
}
#endif

#endif // BASH_ARITH_H
