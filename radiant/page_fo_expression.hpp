#pragma once
#include "typeset.hpp"
#include "../lambda/input/css/css_style.hpp"

struct RadiantFoExpression {
    CssValue* value;
    TypesetStatus status;
    const char* reason;
    size_t nodes;
};

// FO syntax lowers to common typed CSS math; this adapter does not evaluate lengths.
typedef CssValue* (*RadiantFoExpressionReference)(void* context, const char* function, const char* property);
RadiantFoExpression radiant_fo_expression(Pool* pool, const char* text, size_t max_nodes, size_t max_depth,
    RadiantFoExpressionReference reference = nullptr, void* context = nullptr);
