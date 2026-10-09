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
    bool allow_proportional = false, has_proportional = false;
    RadiantFoExpression result = {};
    bool rgb_percentage_base = false;

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
    bool validate(CssValue* value) {
        CssMathEvaluationContext context = {}; context.preserve_percentages = true;
        bool rgb = value && value->type == CSS_VALUE_TYPE_FUNCTION && !strcmp(value->data.function->name, "rgb");
        size_t count = rgb ? (size_t)value->data.function->arg_count : 1u;
        for (size_t i = 0; i < count; i++) {
            CssMathResult result = css_math_evaluate(rgb ? value->data.function->args[i] : value, &context);
            if (result.type == CSS_MATH_INVALID || (result.resolved &&
                (!isfinite(result.value) || !isfinite(result.percentage)))) {
                fail("FO expression has unsupported dimensions or a nonfinite result"); return false;
            }
        }
        return true;
    }
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
    if (token->type == CSS_TOKEN_PERCENTAGE) {
        double value = token->data.number_value;
        if (!isfinite(value)) return fail("FO expression requires finite percentages");
        // XSL RGB arguments use the numeric color base (100% = 255), including nested arithmetic.
        return allocated(rgb_percentage_base ? css_value_create_number(pool, value / 100.0 * 255.0)
            : css_value_create_percentage(pool, value));
    }
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
    if (allow_proportional && named(token, "proportional-column-width", true)) {
        CssValue* argument = sum(depth + 1); token = peek();
        if (!argument || !token || token->type != CSS_TOKEN_RIGHT_PAREN)
            return fail("proportional-column-width requires one finite positive number");
        cursor++;
        CssMathEvaluationContext context = {};
        CssMathResult weight = css_math_evaluate(argument, &context);
        if (weight.type != CSS_MATH_NUMBER || !weight.resolved || !isfinite(weight.value) || weight.value <= 0.0)
            return fail("proportional-column-width requires one finite positive number");
        has_proportional = true;
        // fractional units are adapter-only symbols, removed before common CSS admission.
        return allocated(css_value_create_length(pool, weight.value, CSS_UNIT_FR));
    }
    static const char* references[] = {"from-parent", "inherited-property-value", "from-nearest-specified-value"};
    for (const char* name : references) if (named(token, name, true)) {
        const char* property = nullptr;
        token = peek();
        if (token && token->type == CSS_TOKEN_IDENT) {
            const char* start = token->start; size_t length = token->length;
            cursor++; token = peek();
            // XML property names can contain a compound-component dot; CSS identifiers cannot.
            if (token && token->type == CSS_TOKEN_DELIM && token->data.delimiter == '.' && token->start == start + length) {
                cursor++; token = peek();
                if (!token || token->type != CSS_TOKEN_IDENT || token->start != start + length + 1)
                    return fail("FO compound property reference requires an adjacent component name");
                length += token->length + 1; cursor++; token = peek();
            }
            property = pool_dup_n(pool, start, length);
            if (!property) return fail("FO expression allocation failed", TYPESET_OUT_OF_MEMORY);
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
        {"min", "min", nullptr, 2, false}, {"max", "max", nullptr, 2, false},
        {"rgb", "rgb", nullptr, 3, true}
    };
    for (const auto& entry : functions) if (named(token, entry.name, true)) {
        bool previous_rgb_base = rgb_percentage_base;
        if (!strcmp(entry.name, "rgb")) rgb_percentage_base = true;
        CssValue* args[3] = {}; size_t offset = entry.strategy ? 1u : 0u;
        if (offset) args[0] = allocated(css_value_create_keyword(pool, entry.strategy));
        for (size_t i = 0; i < entry.count; i++) {
            args[i + offset] = sum(depth + 1); if (!args[i + offset]) return nullptr;
            CssMathType type = css_math_value_type(args[i + offset]);
            if (entry.number && type != CSS_MATH_NUMBER && type != CSS_MATH_DEFERRED)
                return fail("FO function requires unitless numeric arguments");
            token = peek();
            if (i + 1 < entry.count) {
                if (!token || token->type != CSS_TOKEN_COMMA) return fail("FO numeric function has the wrong arity");
                cursor++;
            }
        }
        token = peek();
        if (!token || token->type != CSS_TOKEN_RIGHT_PAREN) return fail("FO numeric function has the wrong arity or is unclosed");
        cursor++;
        rgb_percentage_base = previous_rgb_base;
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

struct FoColumnTerm { CssValue* base; double weight; bool proportional; };

struct FoColumnProjection {
    FoExpressionParser* parser;

    bool scalar(CssValue* value, double* number) {
        CssMathEvaluationContext context = {};
        CssMathResult result = css_math_evaluate(value, &context);
        *number = result.value;
        return result.type == CSS_MATH_NUMBER && result.resolved && isfinite(result.value);
    }
    bool numeric(double left, const char* op, double right, double* number) {
        CssValue* value = parser->operation(parser->allocated(css_value_create_number(parser->pool, left)), op,
            parser->allocated(css_value_create_number(parser->pool, right)));
        return value && scalar(value, number);
    }
    bool combine(FoColumnTerm left, const char* op, FoColumnTerm right, double* weight) {
        if (!strcmp(op, "+") || !strcmp(op, "-")) return numeric(left.weight, op, right.weight, weight);
        if (strcmp(op, "*") && strcmp(op, "/")) return false;
        if ((!strcmp(op, "/") && right.proportional) || (left.proportional && right.proportional)) return false;
        double factor = 0.0;
        if (left.proportional) return scalar(right.base, &factor) &&
            numeric(left.weight, op, factor, weight);
        if (right.proportional) return scalar(left.base, &factor) && numeric(factor, "*", right.weight, weight);
        *weight = 0.0; return true;
    }
    bool project(CssValue* value, size_t depth, FoColumnTerm* result) {
        if (!value || !parser->charge(depth)) return false;
        *result = {value, 0.0, false};
        if (value->type == CSS_VALUE_TYPE_LENGTH && value->data.length.unit == CSS_UNIT_FR) {
            *result = {parser->allocated(css_value_create_length(parser->pool, 0.0, CSS_UNIT_PX)), value->data.length.value, true};
            return result->base != nullptr;
        }
        if (value->type == CSS_VALUE_TYPE_LIST) {
            auto& list = value->data.list;
            if (!list.count || !(list.count & 1) || list.comma_separated) return false;
            FoColumnTerm term = {};
            if (!project(list.values[0], depth + 1, &term)) return false;
            FoExpressionTerms bases = {parser, term.base};
            for (size_t i = 1; i < (size_t)list.count; i += 2) {
                const char* op = css_value_identifier_name(list.values[i]);
                FoColumnTerm right = {}; double weight = 0.0;
                if (!op || !project(list.values[i + 1], depth + 1, &right) || !combine(term, op, right, &weight)) return false;
                if (!bases.append(op, right.base)) return false;
                if (!term.proportional && !right.proportional) {
                    CssValue* aggregate = term.base ? parser->operation(term.base, op, right.base) : nullptr;
                    double number = 0.0;
                    term.base = aggregate && scalar(aggregate, &number)
                        ? parser->allocated(css_value_create_number(parser->pool, number)) : nullptr;
                }
                term.weight = weight; term.proportional |= right.proportional;
            }
            term.base = bases.finish(); *result = term; return term.base != nullptr;
        }
        if (value->type != CSS_VALUE_TYPE_FUNCTION) return true;
        const CssFunction* function = value->data.function;
        if (!function || !function->name || function->arg_count < 1) return false;
        size_t count = (size_t)function->arg_count;
        if (!strcmp(function->name, "calc") && count == 1) {
            if (!project(function->args[0], depth + 1, result)) return false;
            CssValue* args[] = {result->base};
            result->base = parser->function("calc", args, 1); return result->base != nullptr;
        }
        CssValue** bases = (CssValue**)pool_alloc(parser->pool, count * sizeof(CssValue*));
        if (!bases) { parser->fail("FO column projection allocation failed", TYPESET_OUT_OF_MEMORY); return false; }
        bool proportional = false, equal = true; double weight = 0.0;
        for (size_t i = 0; i < count; i++) {
            FoColumnTerm term = {};
            if (!project(function->args[i], depth + 1, &term)) return false;
            bases[i] = term.base; proportional |= term.proportional;
            if (!i) weight = term.weight;
            else equal &= weight == term.weight;
        }
        if (!proportional) return true;
        // equal coefficients commute with min/max; differing coefficients require a used-width solver.
        if ((!strcmp(function->name, "min") || !strcmp(function->name, "max")) && equal) {
            *result = {parser->function(function->name, bases, count), weight, true}; return result->base != nullptr;
        }
        return false;
    }
};

RadiantFoExpression radiant_fo_expression(Pool* pool, const char* text, size_t max_nodes, size_t max_depth,
        RadiantFoExpressionReference reference, void* context, double* proportion, RadiantFoExpressionSyntax syntax) {
    if (proportion) *proportion = 0.0;
    if (!pool || !text || syntax > FO_EXPRESSION_COMPONENTS || (proportion && syntax != FO_EXPRESSION_SCALAR))
        return {nullptr, TYPESET_INVALID, "FO expression requires an owner, source and compatible syntax", 0};
    size_t count = 0;
    CssToken* tokens = css_tokenize(text, strlen(text), pool, &count);
    if (!tokens) return {nullptr, TYPESET_OUT_OF_MEMORY, "FO expression token allocation failed", 0};
    FoExpressionParser parser = {pool, tokens, count, 0, max_nodes, max_depth, reference, context};
    parser.allow_proportional = proportion != nullptr;
    CssValue* value = nullptr;
    bool list = syntax != FO_EXPRESSION_SCALAR, components = syntax == FO_EXPRESSION_COMPONENTS;
    if (list) {
        // each shorthand/scale component keeps ordinary XSL arithmetic precedence and budgets.
        ArrayList* entries = arraylist_new(8);
        if (!entries) parser.fail("FO expression allocation failed", TYPESET_OUT_OF_MEMORY);
        else if (parser.charge(0)) while (parser.result.status == TYPESET_OK) {
            const CssToken* token = parser.peek();
            if (!token || token->type == CSS_TOKEN_EOF) break;
            CssValue* entry = nullptr;
            if ((components && token->type == CSS_TOKEN_IDENT) || parser.named(token, "any")) {
                if (!parser.charge(1)) break;
                parser.cursor++;
                const char* keyword = css_token_value_dup(token, pool);
                entry = keyword ? parser.allocated(css_value_create_keyword(pool, keyword)) :
                    parser.fail("FO expression allocation failed", TYPESET_OUT_OF_MEMORY);
            } else if (components && token->type == CSS_TOKEN_HASH) {
                if (!parser.charge(1)) break;
                parser.cursor++; uint8_t rgba[4] = {};
                if (!css_parse_hex_to_rgba(token->value, rgba, rgba + 1, rgba + 2, rgba + 3))
                    parser.fail("FO shorthand requires a valid hexadecimal color");
                else entry = parser.allocated(css_value_create_color_hex(pool, token->value));
            } else entry = parser.sum(1);
            if (!entry) break;
            if (!arraylist_append(entries, entry)) {
                parser.fail("FO expression allocation failed", TYPESET_OUT_OF_MEMORY); break;
            }
            token = parser.peek();
            if (token && token->type != CSS_TOKEN_EOF &&
                (!parser.cursor || parser.tokens[parser.cursor - 1].type != CSS_TOKEN_WHITESPACE)) {
                parser.fail("FO expression list requires whitespace between entries"); break;
            }
        }
        if (entries) {
            if (!entries->length) parser.fail("FO expression list requires at least one entry");
            if (parser.result.status == TYPESET_OK)
                value = parser.allocated(css_value_create_list(pool, (CssValue**)entries->data, (size_t)entries->length));
            arraylist_free(entries);
        }
    } else value = parser.sum(0);
    if (!value && parser.result.status == TYPESET_OK) parser.fail("FO expression requires a numeric value");
    const CssToken* trailing = parser.peek();
    if (value && (!trailing || trailing->type != CSS_TOKEN_EOF)) parser.fail("unexpected trailing FO expression tokens");
    if (value && parser.result.status == TYPESET_OK && parser.has_proportional) {
        FoColumnProjection projection = {&parser}; FoColumnTerm term = {};
        if (!projection.project(value, 0, &term) || !isfinite(term.weight) || term.weight <= 0.0)
            parser.fail("FO proportional expression requires a finite positive affine track coefficient");
        else { value = term.base; *proportion = term.weight; }
    }
    if (value && parser.result.status == TYPESET_OK) {
        size_t arguments = list ? (size_t)value->data.list.count : 1u;
        for (size_t i = 0; i < arguments; i++) {
            CssValue* entry = list ? value->data.list.values[i] : value;
            const char* keyword = list ? css_value_identifier_name(entry) : nullptr;
            if ((keyword && (components || !strcmp(keyword, "any"))) || (components && entry->type == CSS_VALUE_TYPE_COLOR)) continue;
            if (!parser.validate(entry)) break;
        }
        if (parser.result.status == TYPESET_OK) parser.result.value = value;
    }
    css_token_array_release(pool, tokens, count);
    return parser.result;
}
