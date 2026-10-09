#include "page_fo_expression.hpp"
#include "../lambda/input/css/css_parser.hpp"
#include "../lib/arraylist.h"
#include <string.h>
#include <math.h>

struct FoExpressionParser {
    Pool* pool;
    CssToken* tokens;
    size_t count, cursor = 0, max_nodes, max_depth;
    RadiantFoExpressionReference reference;
    void* reference_context;
    RadiantFoExpression result = {};

    const CssToken* peek() {
        while (cursor < count && tokens[cursor].type == CSS_TOKEN_WHITESPACE) cursor++;
        return cursor < count ? tokens + cursor : nullptr;
    }
    CssValue* fail(const char* reason, TypesetStatus status = TYPESET_INVALID) {
        if (result.status == TYPESET_OK) { result.status = status; result.reason = reason; }
        return nullptr;
    }
    bool charge(size_t depth) {
        if (++result.nodes > max_nodes || depth > max_depth) {
            fail("FO expression budget exhausted", TYPESET_BUDGET_EXHAUSTED); return false;
        }
        return true;
    }
    CssValue* allocated(CssValue* value) {
        return value ? value : fail("FO expression allocation failed", TYPESET_OUT_OF_MEMORY);
    }
    bool named(const CssToken* token, const char* name, bool function = false) {
        size_t length = strlen(name);
        return token && token->type == (function ? CSS_TOKEN_FUNCTION : CSS_TOKEN_IDENT) &&
            token->length == length + (function ? 1u : 0u) && !strncmp(token->start, name, length);
    }
    CssValue* function(const char* name, CssValue** args, size_t count) {
        // the common function constructor borrows its array, so retain arguments in the owner pool.
        CssValue* list = allocated(css_value_create_list(pool, args, count));
        if (!list) return nullptr;
        return allocated(css_value_create_function(pool, name, list->data.list.values, list->data.list.count));
    }
    CssValue* operation(CssValue* left, const char* op, CssValue* right) {
        if (!left || !right) return nullptr;
        CssValue* args[] = {left, allocated(css_value_create_keyword(pool, op)), right};
        if (!args[1]) return nullptr;
        CssValue* list = allocated(css_value_create_list(pool, args, 3));
        return list ? function("calc", &list, 1) : nullptr;
    }
    CssValue* sum(size_t depth);
    CssValue* product(size_t depth);
    CssValue* atom(size_t depth);
};

struct FoExpressionTerms {
    FoExpressionParser* parser;
    CssValue* first;
    ArrayList* values = nullptr;
    ~FoExpressionTerms() { if (values) arraylist_free(values); }
    bool append(const char* op, CssValue* right) {
        if (!right) return false;
        if (!values) {
            values = arraylist_new(8);
            if (!values || !arraylist_append(values, first)) {
                parser->fail("FO expression allocation failed", TYPESET_OUT_OF_MEMORY); return false;
            }
        }
        CssValue* operation = parser->allocated(css_value_create_keyword(parser->pool, op));
        if (!operation || !arraylist_append(values, operation) || !arraylist_append(values, right)) {
            parser->fail("FO expression allocation failed", TYPESET_OUT_OF_MEMORY); return false;
        }
        return true;
    }
    CssValue* finish() {
        if (!values) return first;
        // flat operator sequences keep left-associated arithmetic bounded in stack and allocation size.
        CssValue* list = parser->allocated(css_value_create_list(parser->pool,
            (CssValue**)values->data, (size_t)values->length));
        return list ? parser->function("calc", &list, 1) : nullptr;
    }
    void reset(CssValue* value) {
        if (values) { arraylist_free(values); values = nullptr; }
        first = value;
    }
};

CssValue* FoExpressionParser::atom(size_t depth) {
    const CssToken* token = peek();
    if (!token || !charge(depth)) return nullptr;
    cursor++;
    if (token->type == CSS_TOKEN_NUMBER)
        return isfinite(token->data.number_value) ? allocated(css_value_create_number(pool, token->data.number_value))
            : fail("FO expression requires finite numeric literals");
    if (token->type == CSS_TOKEN_PERCENTAGE)
        return isfinite(token->data.number_value) ? allocated(css_value_create_percentage(pool, token->data.number_value))
            : fail("FO expression requires finite percentages");
    if (token->type == CSS_TOKEN_DIMENSION) {
        CssUnit unit = token->data.dimension.unit;
        if (unit != CSS_UNIT_EM && unit != CSS_UNIT_IN && unit != CSS_UNIT_CM && unit != CSS_UNIT_MM &&
            unit != CSS_UNIT_PT && unit != CSS_UNIT_PC && unit != CSS_UNIT_PX)
            return fail("FO expression requires an XSL length unit");
        return isfinite(token->data.dimension.value) ? allocated(css_value_create_length(pool, token->data.dimension.value, unit))
            : fail("FO expression requires finite lengths");
    }
    if (token->type == CSS_TOKEN_DELIM && token->data.delimiter == '-') {
        CssValue* operand = atom(depth + 1);
        return operand ? operation(allocated(css_value_create_number(pool, -1.0)), "*", operand) : nullptr;
    }
    if (token->type == CSS_TOKEN_LEFT_PAREN) {
        CssValue* value = sum(depth + 1); token = peek();
        if (!value) return nullptr;
        if (!token || token->type != CSS_TOKEN_RIGHT_PAREN) return fail("FO expression requires a closing parenthesis");
        cursor++; return value;
    }
    static const char* references[] = {"from-parent", "inherited-property-value", "from-nearest-specified-value"};
    for (const char* name : references) if (named(token, name, true)) {
        const char* property = nullptr;
        token = peek();
        if (token && token->type == CSS_TOKEN_IDENT) {
            property = pool_dup_n(pool, token->start, token->length);
            if (!property) return fail("FO expression allocation failed", TYPESET_OUT_OF_MEMORY);
            cursor++; token = peek();
        }
        if (!token || token->type != CSS_TOKEN_RIGHT_PAREN) return fail("FO property reference requires an optional property name");
        cursor++;
        if (!reference) return fail("FO property reference requires a selected style binding");
        CssValue* value = reference(reference_context, name, property);
        return value ? value : fail("FO property reference is unsupported in this context");
    }
    static const struct { const char* name; const char* target; const char* strategy; size_t count; bool number; } functions[] = {
        {"floor", "round", "down", 1, true}, {"ceiling", "round", "up", 1, true},
        {"round", "round", nullptr, 1, true}, {"abs", "abs", nullptr, 1, false},
        {"min", "min", nullptr, 2, false}, {"max", "max", nullptr, 2, false}
    };
    for (const auto& entry : functions) if (named(token, entry.name, true)) {
        CssValue* args[3] = {}; size_t offset = entry.strategy ? 1u : 0u;
        if (offset) args[0] = allocated(css_value_create_keyword(pool, entry.strategy));
        for (size_t i = 0; i < entry.count; i++) {
            args[i + offset] = sum(depth + 1); if (!args[i + offset]) return nullptr;
            CssMathType type = css_math_value_type(args[i + offset]);
            if (entry.number && type != CSS_MATH_NUMBER && type != CSS_MATH_DEFERRED)
                return fail("FO rounding functions require unitless numbers");
            token = peek();
            if (i + 1 < entry.count) {
                if (!token || token->type != CSS_TOKEN_COMMA) return fail("FO numeric function has the wrong arity");
                cursor++;
            }
        }
        token = peek();
        if (!token || token->type != CSS_TOKEN_RIGHT_PAREN) return fail("FO numeric function has the wrong arity or is unclosed");
        cursor++;
        return function(entry.target, args, entry.count + offset);
    }
    return fail("unsupported FO numeric expression token or function");
}

CssValue* FoExpressionParser::product(size_t depth) {
    CssValue* value = atom(depth);
    if (!value) return nullptr;
    FoExpressionTerms terms = {this, value};
    while (value) {
        const CssToken* token = peek();
        bool multiply = token && token->type == CSS_TOKEN_DELIM && token->data.delimiter == '*';
        bool divide = named(token, "div"), remainder = named(token, "mod");
        if (!multiply && !divide && !remainder) break;
        if (!charge(depth)) return nullptr;
        cursor++; CssValue* right = atom(depth); if (!right) return nullptr;
        if (divide || remainder) {
            CssMathEvaluationContext context = {}; context.preserve_percentages = true;
            CssMathResult divisor = css_math_evaluate(right, &context);
            if (divisor.resolved && !divisor.value && !divisor.percentage)
                return fail("FO division and remainder require a nonzero divisor");
        }
        if (remainder) {
            value = terms.finish(); if (!value) return nullptr;
            CssValue* args[] = {value, right};
            // XSL mod truncates toward zero; CSS rem has that sign rule, whereas CSS mod uses floor.
            value = function("rem", args, 2);
            terms.reset(value);
        } else if (!terms.append(divide ? "/" : "*", right)) return nullptr;
    }
    return value ? terms.finish() : nullptr;
}

CssValue* FoExpressionParser::sum(size_t depth) {
    CssValue* value = product(depth);
    if (!value) return nullptr;
    FoExpressionTerms terms = {this, value};
    while (value) {
        const CssToken* token = peek();
        bool delimiter = token && token->type == CSS_TOKEN_DELIM &&
            (token->data.delimiter == '+' || token->data.delimiter == '-');
        // CSS tokenization retains an adjacent numeric sign; XSL still treats it as a sum operator.
        bool signed_number = token && (token->type == CSS_TOKEN_NUMBER || token->type == CSS_TOKEN_DIMENSION ||
            token->type == CSS_TOKEN_PERCENTAGE) && token->length && (token->start[0] == '+' || token->start[0] == '-');
        if (!delimiter && !signed_number) break;
        if (!charge(depth)) return nullptr;
        const char* op = delimiter && token->data.delimiter == '-' ? "-" : "+";
        if (delimiter) cursor++;
        if (!terms.append(op, product(depth))) return nullptr;
    }
    return terms.finish();
}

RadiantFoExpression radiant_fo_expression(Pool* pool, const char* text, size_t max_nodes, size_t max_depth,
        RadiantFoExpressionReference reference, void* context) {
    if (!pool || !text) return {nullptr, TYPESET_INVALID, "FO expression requires an owner and source", 0};
    size_t count = 0;
    CssToken* tokens = css_tokenize(text, strlen(text), pool, &count);
    if (!tokens) return {nullptr, TYPESET_OUT_OF_MEMORY, "FO expression token allocation failed", 0};
    FoExpressionParser parser = {pool, tokens, count, 0, max_nodes, max_depth, reference, context};
    CssValue* value = parser.sum(0);
    if (!value && parser.result.status == TYPESET_OK) parser.fail("FO expression requires a numeric value");
    const CssToken* trailing = parser.peek();
    if (value && (!trailing || trailing->type != CSS_TOKEN_EOF)) parser.fail("unexpected trailing FO expression tokens");
    if (value && parser.result.status == TYPESET_OK) {
        CssMathEvaluationContext context = {}; context.preserve_percentages = true;
        CssMathResult evaluated = css_math_evaluate(value, &context);
        if (evaluated.type == CSS_MATH_INVALID || (evaluated.resolved &&
            (!isfinite(evaluated.value) || !isfinite(evaluated.percentage))))
            parser.fail("FO expression has unsupported dimensions or a nonfinite result");
        else parser.result.value = value;
    }
    css_token_array_release(pool, tokens, count);
    return parser.result;
}
