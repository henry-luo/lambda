// format-math2.hpp - Formatter for new MathNode-based math trees
//
// This formatter converts MathNode trees (Map-based) to various output formats.

#ifndef LAMBDA_FORMAT_MATH2_HPP
#define LAMBDA_FORMAT_MATH2_HPP

#include "../lambda-data.hpp"

// Format MathNode tree to LaTeX
String* format_math2_latex(Pool* pool, Item root);

// Format MathNode tree to Typst
String* format_math2_typst(Pool* pool, Item root);

// Format MathNode tree to ASCII
String* format_math2_ascii(Pool* pool, Item root);

// Format MathNode tree to MathML
String* format_math2_mathml(Pool* pool, Item root);

// Check if an item is a new-style MathNode (Map with "node" field)
bool is_math_node_item(Item item);

#endif // LAMBDA_FORMAT_MATH2_HPP
