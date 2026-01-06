// input-math2.hpp - LaTeX math parser declarations
//
// Parser for converting LaTeX math strings to MathNode trees.

#ifndef LAMBDA_INPUT_MATH2_HPP
#define LAMBDA_INPUT_MATH2_HPP

#include "../lambda-data.hpp"
#include "input.hpp"

namespace lambda {

// Parse a LaTeX math string and return a MathNode tree
// The source should be the content between $ delimiters (not including them)
// Returns ItemNull on parse error
Item parse_math(const char* source, Input* input);

// Debug: print the raw parse tree to log
void debug_print_math_tree(const char* source);

} // namespace lambda

#endif // LAMBDA_INPUT_MATH2_HPP
