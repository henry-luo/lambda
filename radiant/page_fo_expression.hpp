#pragma once
#include "typeset.hpp"
#include "../lambda/input/css/css_style.hpp"

struct RadiantFoExpression {
    CssValue* value;
    TypesetStatus status;
    const char* reason;
    size_t nodes;
};

// FO syntax lowers to common typed CSS math and RGB values; this adapter does not evaluate lengths or colors.
// requesting a proportion projects affine column expressions into a CSS base and track coefficient.
typedef CssValue* (*RadiantFoExpressionReference)(void* context, const char* function, const char* property);
enum RadiantFoExpressionSyntax : uint8_t { FO_EXPRESSION_SCALAR, FO_EXPRESSION_SCALES, FO_EXPRESSION_COMPONENTS };
RadiantFoExpression radiant_fo_expression(Pool* pool, const char* text, size_t max_nodes, size_t max_depth,
    RadiantFoExpressionReference reference = nullptr, void* context = nullptr, double* proportion = nullptr,
    RadiantFoExpressionSyntax syntax = FO_EXPRESSION_SCALAR);
