#include "js_parser.h"

#include <string.h>

enum {
    JS_MAX_PARSE_DEPTH = 1024,
    JS_BP_SEQUENCE = 1,
    JS_BP_ASSIGNMENT = 2,
    JS_BP_CONDITIONAL = 3,
    JS_BP_NULLISH = 4,
    JS_BP_OR = 5,
    JS_BP_AND = 6,
    JS_BP_BIT_OR = 7,
    JS_BP_BIT_XOR = 8,
    JS_BP_BIT_AND = 9,
    JS_BP_EQUALITY = 10,
    JS_BP_RELATION = 11,
    JS_BP_SHIFT = 12,
    JS_BP_ADD = 13,
    JS_BP_MULTIPLY = 14,
    JS_BP_EXPONENT = 15,
    JS_BP_PREFIX = 16,
    JS_BP_POSTFIX = 17,
};

typedef struct JsParser {
    JsLexer lexer;
    JsToken current;
    JsToken next;
    JsToken previous;
    JsLexer current_start;
    JsLexer next_start;
    JsParseMode mode;
    const JsParseSink* sink;
    void* sink_context;
    JsParseMetrics* metrics;
    JsParseError* error;
    JsParseStatus status;
    uint32_t depth;
    uint32_t function_depth;
    uint32_t loop_depth;
    uint32_t switch_depth;
    bool in_async_function;
    bool in_generator;
    bool saw_directive_prologue;
    bool type_stop_at_arrow;
    bool stop_for_in_of;
    bool assignment_target_pattern;
    bool probing_assignment_member_base;
    bool in_ts_namespace;
    uint8_t pending_parameter_accessibility;
    bool pending_parameter_readonly;
    uint32_t type_depth;
    uint32_t suppress_type_reductions;
    bool last_statement_reduced;
    struct {
        JsToken token;
        bool iteration;
    } labels[JS_MAX_PARSE_DEPTH];
    uint32_t label_depth;
} JsParser;

typedef struct JsParserProbe {
    JsParser parser;
    JsParseMetrics metrics;
    JsParseError error;
} JsParserProbe;

static const char* error_expected_expression = "expected an expression";
static const char* error_expected_identifier = "expected an identifier";
static const char* error_expected_statement = "expected a statement";
static const char* error_expected_type = "expected a TypeScript type";
static const char* error_expected_semicolon = "expected ';' or a line terminator";
static const char* error_unexpected_eof = "unexpected end of input";
static const char* error_unexpected_token = "unexpected token";
static const char* error_unterminated = "unterminated construct";
static const char* error_line_terminator = "line terminator is not permitted here";
static const char* error_nesting = "maximum parser nesting exceeded";
static const char* error_sink = "parser reduction sink rejected a reduction";

static SourceSpan js_parser_span_from_tokens(JsToken first, JsToken last) {
    SourceSpan span = {first.span.start_byte, last.span.end_byte};
    return span;
}

static SourceSpan js_parser_span_from_start(JsToken first, SourceSpan end) {
    SourceSpan span = {first.span.start_byte, end.end_byte};
    return span;
}

static bool js_parser_parser_token_bit(JsTokenKind kind, uint64_t bits[4]) {
    uint32_t value = (uint32_t)kind;
    if (value >= 256) return false;
    bits[value / 64] |= (uint64_t)1u << (value % 64);
    return true;
}

static void js_parser_parser_record_error(JsParser* parser, JsParseErrorCode code,
        const char* message, JsTokenKind expected) {
    if (!parser || parser->status != JS_PARSE_OK) return;
    parser->status = parser->current.kind == JS_TOK_EOF
        ? JS_PARSE_INCOMPLETE : JS_PARSE_ERROR;
    if (parser->error) {
        memset(parser->error, 0, sizeof(*parser->error));
        parser->error->code = parser->current.kind == JS_TOK_EOF
            ? JS_PARSE_ERROR_UNEXPECTED_EOF : code;
        parser->error->actual_kind = parser->current.kind;
        parser->error->span = parser->current.span;
        parser->error->message = parser->current.kind == JS_TOK_EOF
            ? error_unexpected_eof : message;
        js_parser_parser_token_bit(expected, parser->error->expected_token_bits);
    }
}

static bool js_parser_parser_fail(JsParser* parser, JsParseErrorCode code,
        const char* message, JsTokenKind expected) {
    js_parser_parser_record_error(parser, code, message, expected);
    return false;
}

static void js_parser_parser_relex_current(JsParser* parser, JsLexGoal goal);

static bool js_parser_parser_enter(JsParser* parser) {
    if (!parser || parser->status != JS_PARSE_OK) return false;
    parser->depth++;
    if (parser->metrics && parser->depth > parser->metrics->max_recursion_depth) {
        parser->metrics->max_recursion_depth = parser->depth;
    }
    if (parser->depth <= JS_MAX_PARSE_DEPTH) return true;
    return js_parser_parser_fail(parser, JS_PARSE_ERROR_NESTING, error_nesting, JS_TOK_EOF);
}

static void js_parser_parser_leave(JsParser* parser) {
    if (parser && parser->depth) parser->depth--;
}

static bool js_parser_token_ends_expression(JsTokenKind kind) {
    switch (kind) {
    case JS_TOK_IDENTIFIER:
    case JS_TOK_PRIVATE_IDENTIFIER:
    case JS_TOK_NUMBER:
    case JS_TOK_BIGINT:
    case JS_TOK_STRING:
    case JS_TOK_REGEXP:
    case JS_TOK_TEMPLATE:
    case JS_TOK_TRUE:
    case JS_TOK_FALSE:
    case JS_TOK_NULL:
    case JS_TOK_THIS:
    case JS_TOK_SUPER:
    case JS_TOK_ASYNC:
    case JS_TOK_OF:
    case JS_TOK_GET:
    case JS_TOK_SET:
    case JS_TOK_AS:
    case JS_TOK_ASSERTS:
    case JS_TOK_ABSTRACT:
    case JS_TOK_ANY:
    case JS_TOK_BOOLEAN:
    case JS_TOK_DECLARE:
    case JS_TOK_ENUM:
    case JS_TOK_FROM:
    case JS_TOK_IMPLEMENTS:
    case JS_TOK_INFER:
    case JS_TOK_INTERFACE:
    case JS_TOK_IS:
    case JS_TOK_KEYOF:
    case JS_TOK_MODULE:
    case JS_TOK_NAMESPACE:
    case JS_TOK_NEVER:
    case JS_TOK_NUMBER_TYPE:
    case JS_TOK_OBJECT:
    case JS_TOK_PACKAGE:
    case JS_TOK_PRIVATE:
    case JS_TOK_PROTECTED:
    case JS_TOK_PUBLIC:
    case JS_TOK_READONLY:
    case JS_TOK_REQUIRE:
    case JS_TOK_SATISFIES:
    case JS_TOK_STATIC:
    case JS_TOK_STRING_TYPE:
    case JS_TOK_SYMBOL:
    case JS_TOK_TYPE:
    case JS_TOK_UNKNOWN:
    case JS_TOK_RPAREN:
    case JS_TOK_RBRACKET:
    case JS_TOK_RBRACE:
    case JS_TOK_PLUS_PLUS:
    case JS_TOK_MINUS_MINUS:
        return true;
    default:
        return false;
    }
}

static JsLexGoal js_parser_parser_goal_after(JsTokenKind kind) {
    return js_parser_token_ends_expression(kind) ? JS_LEX_DIV : JS_LEX_REGEXP;
}

static void js_parser_parser_advance(JsParser* parser) {
    if (!parser || parser->status != JS_PARSE_OK) return;
    parser->previous = parser->current;
    parser->current = parser->next;
    parser->current_start = parser->next_start;
    bool regex_expected = !js_parser_token_ends_expression(parser->previous.kind);
    JsLexGoal goal = js_parser_parser_goal_after(parser->current.kind);
    parser->next_start = parser->lexer;
    js_lexer_set_goal(&parser->next_start, goal);
    parser->lexer = parser->next_start;
    parser->next = js_lexer_next(&parser->lexer);
    if (parser->metrics) parser->metrics->token_count++;
    if ((regex_expected && parser->current.kind == JS_TOK_SLASH) ||
            (!regex_expected && parser->current.kind == JS_TOK_REGEXP)) {
        js_parser_parser_relex_current(parser, regex_expected ? JS_LEX_REGEXP : JS_LEX_DIV);
    }
    if (parser->current.kind == JS_TOK_ERROR) {
        js_parser_parser_fail(parser, JS_PARSE_ERROR_INVALID_TOKEN,
            error_unexpected_token, JS_TOK_EOF);
    }
}

static void js_parser_parser_relex_current(JsParser* parser, JsLexGoal goal) {
    if (!parser || (parser->current.kind != JS_TOK_SLASH &&
            parser->current.kind != JS_TOK_REGEXP)) return;
    bool line_terminator_before = parser->current.line_terminator_before;
    JsLexer lexer = parser->current_start;
    lexer.offset = parser->current.span.start_byte;
    lexer.line = parser->current.line;
    lexer.column = parser->current.column;
    lexer.template_continuation = false;
    js_lexer_set_goal(&lexer, goal);
    JsToken current = js_lexer_next(&lexer);
    current.line_terminator_before = line_terminator_before;
    js_lexer_set_goal(&lexer, js_parser_parser_goal_after(current.kind));
    parser->next_start = lexer;
    JsToken next = js_lexer_next(&lexer);
    parser->current = current;
    parser->next = next;
    parser->lexer = lexer;
}

static void js_parser_parser_relex_current_as_regex(JsParser* parser) {
    if (!parser || parser->current.kind != JS_TOK_SLASH) return;
    js_parser_parser_relex_current(parser, JS_LEX_REGEXP);
}

static bool js_parser_parser_accept(JsParser* parser, JsTokenKind kind) {
    if (!parser || parser->current.kind != kind) return false;
    js_parser_parser_advance(parser);
    return true;
}

static bool js_parser_parser_expect(JsParser* parser, JsTokenKind kind,
        const char* message) {
    if (js_parser_parser_accept(parser, kind)) return true;
    return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN, message, kind);
}

static bool js_parser_parser_reduce_with_child_flags(JsParser* parser,
        JsReductionKind kind,
        JsReductionForm form, SourceSpan span, JsToken introducer,
        JsToken operator_token, uint32_t flags, uint32_t child_count,
        uint32_t child_flags) {
    if (!parser || parser->status != JS_PARSE_OK) return false;
    if (parser->metrics) {
        parser->metrics->reduction_count++;
        parser->metrics->structural_hash ^= (uint64_t)kind +
            ((uint64_t)form << 8) + ((uint64_t)span.start_byte << 17) +
            ((uint64_t)span.end_byte << 33) + ((uint64_t)operator_token.kind << 3);
        parser->metrics->structural_hash *= UINT64_C(1099511628211);
    }
    if (parser->sink && parser->sink->reduce) {
        JsParseReduction reduction;
        reduction.kind = kind;
        reduction.form = form;
        reduction.span = span;
        reduction.introducer = introducer;
        reduction.operator_token = operator_token;
        reduction.flags = flags;
        reduction.child_count = child_count;
        reduction.child_flags = child_flags;
        reduction.parameter_accessibility =
            kind == JS_REDUCE_DECLARATION && form == JS_REDUCTION_PARAMETER
                ? parser->pending_parameter_accessibility : 0;
        reduction.parameter_readonly =
            kind == JS_REDUCE_DECLARATION && form == JS_REDUCTION_PARAMETER
                ? parser->pending_parameter_readonly : false;
        if (!parser->sink->reduce(parser->sink_context, &reduction)) {
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT, error_sink, JS_TOK_EOF);
        }
    }
    return true;
}

static bool js_parser_parser_reduce(JsParser* parser, JsReductionKind kind,
        JsReductionForm form, SourceSpan span, JsToken introducer,
        JsToken operator_token, uint32_t flags, uint32_t child_count) {
    return js_parser_parser_reduce_with_child_flags(parser, kind, form, span,
        introducer, operator_token, flags, child_count, 0);
}

static bool js_parser_token_is_name(JsTokenKind kind) {
    switch (kind) {
    case JS_TOK_IDENTIFIER:
    case JS_TOK_PRIVATE_IDENTIFIER:
    case JS_TOK_BREAK:
    case JS_TOK_CASE:
    case JS_TOK_CATCH:
    case JS_TOK_CLASS:
    case JS_TOK_CONST:
    case JS_TOK_CONTINUE:
    case JS_TOK_DEBUGGER:
    case JS_TOK_DEFAULT:
    case JS_TOK_DELETE:
    case JS_TOK_DO:
    case JS_TOK_ELSE:
    case JS_TOK_EXPORT:
    case JS_TOK_EXTENDS:
    case JS_TOK_FINALLY:
    case JS_TOK_FOR:
    case JS_TOK_FUNCTION:
    case JS_TOK_IF:
    case JS_TOK_IMPORT:
    case JS_TOK_IN:
    case JS_TOK_INSTANCEOF:
    case JS_TOK_LET:
    case JS_TOK_NEW:
    case JS_TOK_RETURN:
    case JS_TOK_SUPER:
    case JS_TOK_SWITCH:
    case JS_TOK_THIS:
    case JS_TOK_THROW:
    case JS_TOK_TRY:
    case JS_TOK_TYPEOF:
    case JS_TOK_VAR:
    case JS_TOK_VOID:
    case JS_TOK_WHILE:
    case JS_TOK_WITH:
    case JS_TOK_YIELD:
    case JS_TOK_ASYNC:
    case JS_TOK_AWAIT:
    case JS_TOK_OF:
    case JS_TOK_GET:
    case JS_TOK_SET:
    case JS_TOK_AS:
    case JS_TOK_ASSERTS:
    case JS_TOK_ABSTRACT:
    case JS_TOK_ANY:
    case JS_TOK_BOOLEAN:
    case JS_TOK_DECLARE:
    case JS_TOK_ENUM:
    case JS_TOK_FROM:
    case JS_TOK_IMPLEMENTS:
    case JS_TOK_INFER:
    case JS_TOK_INTERFACE:
    case JS_TOK_IS:
    case JS_TOK_KEYOF:
    case JS_TOK_MODULE:
    case JS_TOK_NAMESPACE:
    case JS_TOK_NEVER:
    case JS_TOK_NUMBER_TYPE:
    case JS_TOK_OBJECT:
    case JS_TOK_PACKAGE:
    case JS_TOK_PRIVATE:
    case JS_TOK_PROTECTED:
    case JS_TOK_PUBLIC:
    case JS_TOK_READONLY:
    case JS_TOK_REQUIRE:
    case JS_TOK_SATISFIES:
    case JS_TOK_STATIC:
    case JS_TOK_STRING_TYPE:
    case JS_TOK_SYMBOL:
    case JS_TOK_TYPE:
    case JS_TOK_UNKNOWN:
        return true;
    default:
        return false;
    }
}

static bool js_parser_token_is_property_name(JsTokenKind kind) {
    return js_parser_token_is_name(kind) || kind == JS_TOK_TRUE ||
        kind == JS_TOK_FALSE || kind == JS_TOK_NULL;
}

static bool js_parser_parser_label_matches(JsParser* parser, JsToken label,
        uint32_t index) {
    if (!parser || index >= parser->label_depth) return false;
    JsToken active = parser->labels[index].token;
    size_t active_length = active.span.end_byte - active.span.start_byte;
    size_t label_length = label.span.end_byte - label.span.start_byte;
    return active_length == label_length &&
        memcmp(parser->lexer.source + active.span.start_byte,
            parser->lexer.source + label.span.start_byte, active_length) == 0;
}

static bool js_parser_parser_label_find(JsParser* parser, JsToken label,
        bool* iteration_out) {
    if (!parser || label.span.end_byte <= label.span.start_byte) return false;
    for (uint32_t i = parser->label_depth; i > 0; i--) {
        if (js_parser_parser_label_matches(parser, label, i - 1)) {
            if (iteration_out) *iteration_out = parser->labels[i - 1].iteration;
            return true;
        }
    }
    return false;
}

static bool js_parser_token_is_type_name(JsTokenKind kind) {
    return js_parser_token_is_name(kind) || kind == JS_TOK_TRUE || kind == JS_TOK_FALSE ||
        kind == JS_TOK_NULL;
}

static bool js_parser_token_starts_expression(JsTokenKind kind) {
    if (js_parser_token_is_name(kind)) {
        switch (kind) {
        case JS_TOK_BREAK: case JS_TOK_CASE: case JS_TOK_CATCH:
        case JS_TOK_CLASS: case JS_TOK_CONST: case JS_TOK_CONTINUE:
        case JS_TOK_DEBUGGER: case JS_TOK_DEFAULT: case JS_TOK_DO:
        case JS_TOK_ELSE: case JS_TOK_EXPORT: case JS_TOK_EXTENDS:
        case JS_TOK_FINALLY: case JS_TOK_FOR: case JS_TOK_FUNCTION:
        case JS_TOK_IF: case JS_TOK_RETURN: case JS_TOK_SWITCH:
        case JS_TOK_THROW: case JS_TOK_TRY: case JS_TOK_VAR:
        case JS_TOK_WHILE: case JS_TOK_WITH: case JS_TOK_IMPORT:
            return false;
        default:
            return true;
        }
    }
    switch (kind) {
    case JS_TOK_NUMBER: case JS_TOK_BIGINT: case JS_TOK_STRING:
    case JS_TOK_REGEXP: case JS_TOK_TEMPLATE: case JS_TOK_TRUE:
    case JS_TOK_FALSE: case JS_TOK_NULL: case JS_TOK_LPAREN:
    case JS_TOK_LBRACKET: case JS_TOK_LBRACE: case JS_TOK_PLUS:
    case JS_TOK_MINUS: case JS_TOK_BANG: case JS_TOK_TILDE:
    case JS_TOK_PLUS_PLUS: case JS_TOK_MINUS_MINUS: case JS_TOK_AT:
        return true;
    default:
        return false;
    }
}

static bool js_parser_token_is_assignment(JsTokenKind kind) {
    switch (kind) {
    case JS_TOK_EQUAL: case JS_TOK_PLUS_EQUAL: case JS_TOK_MINUS_EQUAL:
    case JS_TOK_STAR_EQUAL: case JS_TOK_SLASH_EQUAL: case JS_TOK_PERCENT_EQUAL:
    case JS_TOK_EXP_EQUAL: case JS_TOK_AMP_EQUAL: case JS_TOK_PIPE_EQUAL:
    case JS_TOK_CARET_EQUAL: case JS_TOK_LSHIFT_EQUAL: case JS_TOK_RSHIFT_EQUAL:
    case JS_TOK_URSHIFT_EQUAL: case JS_TOK_AMP_AMP_EQUAL:
    case JS_TOK_PIPE_PIPE_EQUAL: case JS_TOK_NULLISH_EQUAL:
        return true;
    default:
        return false;
    }
}

static int js_parser_token_binding_power(JsTokenKind kind, bool* right_associative) {
    if (right_associative) *right_associative = false;
    if (js_parser_token_is_assignment(kind)) {
        if (right_associative) *right_associative = true;
        return JS_BP_ASSIGNMENT;
    }
    switch (kind) {
    case JS_TOK_COMMA: return JS_BP_SEQUENCE;
    case JS_TOK_QUESTION: return JS_BP_CONDITIONAL;
    case JS_TOK_NULLISH: return JS_BP_NULLISH;
    case JS_TOK_PIPE_PIPE: return JS_BP_OR;
    case JS_TOK_AMP_AMP: return JS_BP_AND;
    case JS_TOK_PIPE: return JS_BP_BIT_OR;
    case JS_TOK_CARET: return JS_BP_BIT_XOR;
    case JS_TOK_AMP: return JS_BP_BIT_AND;
    case JS_TOK_EQUAL_EQUAL: case JS_TOK_STRICT_EQUAL:
    case JS_TOK_BANG_EQUAL: case JS_TOK_STRICT_BANG_EQUAL:
        return JS_BP_EQUALITY;
    case JS_TOK_LT: case JS_TOK_LTE: case JS_TOK_GT: case JS_TOK_GTE:
    case JS_TOK_IN: case JS_TOK_INSTANCEOF:
        return JS_BP_RELATION;
    case JS_TOK_LSHIFT: case JS_TOK_RSHIFT: case JS_TOK_URSHIFT:
        return JS_BP_SHIFT;
    case JS_TOK_PLUS: case JS_TOK_MINUS: return JS_BP_ADD;
    case JS_TOK_STAR: case JS_TOK_SLASH: case JS_TOK_PERCENT:
        return JS_BP_MULTIPLY;
    case JS_TOK_EXP:
        if (right_associative) *right_associative = true;
        return JS_BP_EXPONENT;
    default:
        return 0;
    }
}

static bool js_parser_parse_expression(JsParser* parser, int min_bp, SourceSpan* js_parser_span_out);
static bool js_parser_parse_primary(JsParser* parser, SourceSpan* js_parser_span_out);
static bool js_parser_parse_arguments(JsParser* parser, uint32_t* count_out,
        bool* has_spread_out);
static bool js_parser_parse_statement(JsParser* parser);
static bool js_parser_parse_statement_list(JsParser* parser, JsTokenKind terminator,
        uint32_t* count_out);
static bool js_parser_parse_type(JsParser* parser, int min_bp, SourceSpan* js_parser_span_out);
static bool js_parser_parse_pattern(JsParser* parser, SourceSpan* js_parser_span_out,
        bool* has_type_out);
static bool js_parser_parse_block(JsParser* parser);
static bool js_parser_parser_starts_object_statement(JsParser* parser);
static bool js_parser_parse_ts_declaration(JsParser* parser);
static bool js_parser_parse_function(JsParser* parser, bool declaration, bool async,
        bool allow_anonymous, SourceSpan* js_parser_span_out);
static bool js_parser_parse_class(JsParser* parser, bool declaration, SourceSpan* js_parser_span_out);
static bool js_parser_parse_if(JsParser* parser, uint32_t* child_count_out,
        uint32_t* child_flags_out);
static bool js_parser_class_member_modifier_candidate(JsTokenKind kind);
static void js_parser_parser_probe_begin(JsParser* parser, JsParserProbe* probe);
static void js_parser_parser_probe_end(JsParser* parser, const JsParserProbe* probe);

static bool js_parser_parse_semicolon(JsParser* parser) {
    if (js_parser_parser_accept(parser, JS_TOK_SEMICOLON)) return true;
    if (parser->current.kind == JS_TOK_SLASH &&
            parser->current.line_terminator_before) {
        js_parser_parser_relex_current_as_regex(parser);
        return true;
    }
    if (parser->current.kind == JS_TOK_EOF ||
            parser->current.kind == JS_TOK_RBRACE ||
            parser->current.line_terminator_before) return true;
    return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
        error_expected_semicolon, JS_TOK_SEMICOLON);
}

static bool js_parser_parse_do_while_terminator(JsParser* parser) {
    // The grammar permits ASI after the closing parenthesis of a do-while
    // statement even when the following statement starts on the same line.
    if (js_parser_parser_accept(parser, JS_TOK_SEMICOLON)) return true;
    return parser->status == JS_PARSE_OK;
}

static bool js_parser_parse_name(JsParser* parser, JsToken* js_parser_token_out) {
    if (!js_parser_token_is_name(parser->current.kind) ||
            parser->current.kind == JS_TOK_PRIVATE_IDENTIFIER) {
        return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
            error_expected_identifier, JS_TOK_IDENTIFIER);
    }
    if (js_parser_token_out) *js_parser_token_out = parser->current;
    js_parser_parser_advance(parser);
    return true;
}

static bool js_parser_parse_property_name(JsParser* parser, JsToken* js_parser_token_out) {
    if (parser->current.kind == JS_TOK_PRIVATE_IDENTIFIER) {
        if (js_parser_token_out) *js_parser_token_out = parser->current;
        js_parser_parser_advance(parser);
        return true;
    }
    if (!js_parser_token_is_property_name(parser->current.kind)) {
        return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
            error_expected_identifier, JS_TOK_IDENTIFIER);
    }
    if (js_parser_token_out) *js_parser_token_out = parser->current;
    js_parser_parser_advance(parser);
    return true;
}

static bool js_parser_parse_type_parameters(JsParser* parser, uint32_t* count_out) {
    if (count_out) *count_out = 0;
    if (!(parser->mode & JS_PARSE_TYPESCRIPT) ||
            parser->current.kind != JS_TOK_LT) return true;
    JsToken first = parser->current;
    js_parser_parser_advance(parser);
    if (parser->current.kind == JS_TOK_GT) {
        bool ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
            error_expected_identifier, JS_TOK_IDENTIFIER);
        return ok;
    }
    bool ok = true;
    uint32_t count = 0;
    for (;;) {
        JsToken name;
        if (!js_parser_parse_name(parser, &name)) { ok = false; break; }
        SourceSpan parameter_span = name.span;
        uint32_t child_count = 0;
        if (js_parser_parser_accept(parser, JS_TOK_EXTENDS)) {
            SourceSpan constraint_span;
            if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, &constraint_span)) {
                ok = false;
                break;
            }
            parameter_span.end_byte = constraint_span.end_byte;
            child_count++;
        }
        if (js_parser_parser_accept(parser, JS_TOK_EQUAL)) {
            SourceSpan default_span;
            if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, &default_span)) {
                ok = false;
                break;
            }
            parameter_span.end_byte = default_span.end_byte;
            child_count++;
        }
        if (!js_parser_parser_reduce(parser, JS_REDUCE_TYPE,
                JS_REDUCTION_TYPE_PARAMETER, parameter_span, name,
                (JsToken){0}, 0, child_count)) {
            ok = false;
            break;
        }
        count++;
        if (!js_parser_parser_accept(parser, JS_TOK_COMMA)) break;
        if (parser->current.kind == JS_TOK_GT) break;
    }
    if (ok) {
        ok = js_parser_parser_expect(parser, JS_TOK_GT,
            "expected '>' after type parameters");
    }
    if (ok) {
        SourceSpan span = js_parser_span_from_start(first, parser->previous.span);
        ok = js_parser_parser_reduce(parser, JS_REDUCE_TYPE,
            JS_REDUCTION_TYPE_PARAMETERS, span, first, (JsToken){0}, 0,
            count);
    }
    if (ok && count_out) *count_out = count;
    return ok;
}

static bool js_parser_parse_type_before_arrow(JsParser* parser) {
    bool saved = parser->type_stop_at_arrow;
    parser->type_stop_at_arrow = true;
    bool ok = js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL);
    parser->type_stop_at_arrow = saved;
    return ok;
}

static bool js_parser_parse_arrow_body_with_context(JsParser* parser, bool async,
        bool generator) {
    bool old_async = parser->in_async_function;
    bool old_generator = parser->in_generator;
    parser->in_async_function = async;
    parser->in_generator = generator;
    parser->function_depth++;
    bool ok = parser->current.kind == JS_TOK_LBRACE
        ? js_parser_parse_block(parser) : js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL);
    parser->function_depth--;
    parser->in_async_function = old_async;
    parser->in_generator = old_generator;
    return ok;
}

static bool js_parser_parse_arrow_body(JsParser* parser) {
    return js_parser_parse_arrow_body_with_context(parser, false, false);
}

static bool js_parser_parse_arguments(JsParser* parser, uint32_t* count_out,
        bool* has_spread_out) {
    uint32_t count = 0;
    bool has_spread = false;
    if (!js_parser_parser_expect(parser, JS_TOK_LPAREN, "expected '(' before arguments")) return false;
    if (js_parser_parser_accept(parser, JS_TOK_RPAREN)) goto done;
    for (;;) {
        if (parser->current.kind == JS_TOK_ELLIPSIS) {
            JsToken spread = parser->current;
            js_parser_parser_advance(parser);
            has_spread = true;
            if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) return false;
            SourceSpan spread_span = js_parser_span_from_start(spread, parser->previous.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_SPREAD, spread_span, spread, spread, 0, 1)) {
                return false;
            }
        } else if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) {
            return false;
        }
        count++;
        if (js_parser_parser_accept(parser, JS_TOK_COMMA)) {
            if (parser->current.kind == JS_TOK_RPAREN) break;
            continue;
        }
        break;
    }
    if (!js_parser_parser_expect(parser, JS_TOK_RPAREN, "expected ')' after arguments")) {
        return false;
    }
done:
    if (count_out) *count_out = count;
    if (has_spread_out) *has_spread_out = has_spread;
    return true;
}

static bool js_parser_parse_parameter_list_contents(JsParser* parser,
        uint32_t* count_out);

static bool js_parser_parse_parameter_list(JsParser* parser, uint32_t* count_out) {
    if (!js_parser_parser_expect(parser, JS_TOK_LPAREN, "expected '(' before parameters")) return false;
    return js_parser_parse_parameter_list_contents(parser, count_out);
}

// parse the contents after an opening parenthesis so arrow-head probes can
// discard all reductions before the `=>` commitment is visible.
static bool js_parser_parse_parameter_list_contents(JsParser* parser,
        uint32_t* count_out) {
    uint32_t count = 0;
    if (js_parser_parser_accept(parser, JS_TOK_RPAREN)) {
        if (count_out) *count_out = count;
        return true;
    }
    for (;;) {
        JsToken first = parser->current;
        uint8_t parameter_accessibility = 0;
        bool parameter_readonly = false;
        bool saw_modifier = true;
        while ((parser->mode & JS_PARSE_TYPESCRIPT) && saw_modifier) {
            saw_modifier = false;
            switch (parser->current.kind) {
            case JS_TOK_PUBLIC:
                parameter_accessibility = 1;
                saw_modifier = true;
                break;
            case JS_TOK_PRIVATE:
                parameter_accessibility = 2;
                saw_modifier = true;
                break;
            case JS_TOK_PROTECTED:
                parameter_accessibility = 3;
                saw_modifier = true;
                break;
            case JS_TOK_READONLY:
                parameter_readonly = true;
                saw_modifier = true;
                break;
            default:
                break;
            }
            if (saw_modifier) js_parser_parser_advance(parser);
        }
        SourceSpan pattern;
        bool pattern_has_type = false;
        bool rest = js_parser_parser_accept(parser, JS_TOK_ELLIPSIS);
        if (rest) {
            if (!js_parser_parse_pattern(parser, &pattern, &pattern_has_type)) return false;
        } else if (!js_parser_parse_pattern(parser, &pattern, &pattern_has_type)) {
            return false;
        }
        SourceSpan parameter = rest
            ? js_parser_span_from_start(first, parser->previous.span) : pattern;
        bool optional = js_parser_parser_accept(parser, JS_TOK_QUESTION);
        uint32_t flags = optional ? JS_REDUCTION_FLAG_OPTIONAL : 0;
        if (optional) parameter = js_parser_span_from_start(first, parser->previous.span);
        if (rest) flags |= JS_REDUCTION_FLAG_SPREAD;
        bool has_type = pattern_has_type;
        if (js_parser_parser_accept(parser, JS_TOK_COLON)) {
            if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) return false;
            has_type = true;
            parameter = js_parser_span_from_start(first, parser->previous.span);
        }
        bool has_default = false;
        if (js_parser_parser_accept(parser, JS_TOK_EQUAL)) {
            has_default = true;
            if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) return false;
            parameter = js_parser_span_from_start(first, parser->previous.span);
        }
        if (parameter_accessibility || parameter_readonly) {
            parameter.start_byte = first.span.start_byte;
        }
        parser->pending_parameter_accessibility = parameter_accessibility;
        parser->pending_parameter_readonly = parameter_readonly;
        bool reduced = js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
                JS_REDUCTION_PARAMETER, parameter, first, (JsToken){0},
                flags, 1u + (has_type ? 1u : 0u) +
                    (has_default ? 1u : 0u));
        parser->pending_parameter_accessibility = 0;
        parser->pending_parameter_readonly = false;
        if (!reduced) return false;
        count++;
        if (js_parser_parser_accept(parser, JS_TOK_COMMA)) {
            if (parser->current.kind == JS_TOK_RPAREN) break;
            continue;
        }
        break;
    }
    bool ok = js_parser_parser_expect(parser, JS_TOK_RPAREN,
        "expected ')' after parameters");
    if (ok && count_out) *count_out = count;
    return ok;
}

static bool js_parser_parse_array(JsParser* parser, SourceSpan* js_parser_span_out) {
    JsToken first = parser->current;
    uint32_t element_count = 0;
    uint32_t flags = 0;
    bool expect_element = true;
    js_parser_parser_advance(parser);
    while (parser->current.kind != JS_TOK_RBRACKET) {
        if (parser->current.kind == JS_TOK_EOF) {
            js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_EOF, error_unterminated,
                JS_TOK_RBRACKET);
            return false;
        }
        if (js_parser_parser_accept(parser, JS_TOK_COMMA)) {
            if (expect_element) {
                flags |= JS_REDUCTION_FLAG_HOLES;
                if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_HOLE, parser->previous.span,
                        parser->previous, (JsToken){0}, 0, 0)) return false;
                element_count++;
            }
            expect_element = true;
            continue;
        }
        if (parser->current.kind == JS_TOK_ELLIPSIS) {
            JsToken spread = parser->current;
            js_parser_parser_advance(parser);
            flags |= JS_REDUCTION_FLAG_SPREAD;
            if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) return false;
            SourceSpan spread_span = js_parser_span_from_start(spread, parser->previous.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_SPREAD, spread_span, spread, spread, 0, 1)) {
                return false;
            }
        } else if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) {
            return false;
        }
        element_count++;
        expect_element = false;
        if (js_parser_parser_accept(parser, JS_TOK_COMMA)) {
            expect_element = true;
        } else if (parser->current.kind != JS_TOK_RBRACKET) {
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                "expected ',' or ']' after array element", JS_TOK_COMMA);
        }
    }
    JsToken last = parser->current;
    js_parser_parser_advance(parser);
    SourceSpan span = js_parser_span_from_tokens(first, last);
    if (js_parser_span_out) *js_parser_span_out = span;
    return js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION, JS_REDUCTION_ARRAY,
        span, first, (JsToken){0}, flags, element_count);
}

static bool js_parser_parse_object_key(JsParser* parser, SourceSpan* js_parser_span_out,
        JsToken* js_parser_token_out, uint32_t* flags_out) {
    if (parser->current.kind == JS_TOK_LBRACKET) {
        JsToken first = parser->current;
        bool saved_stop_for_in_of = parser->stop_for_in_of;
        parser->stop_for_in_of = false;
        js_parser_parser_advance(parser);
        bool ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) &&
            js_parser_parser_expect(parser, JS_TOK_RBRACKET,
                "expected ']' after computed property");
        parser->stop_for_in_of = saved_stop_for_in_of;
        if (!ok) return false;
        if (js_parser_span_out) *js_parser_span_out = js_parser_span_from_tokens(first, parser->previous);
        if (js_parser_token_out) *js_parser_token_out = (JsToken){0};
        if (flags_out) *flags_out = JS_REDUCTION_FLAG_COMPUTED;
        return true;
    }
    if (parser->current.kind == JS_TOK_NUMBER ||
            parser->current.kind == JS_TOK_BIGINT ||
            parser->current.kind == JS_TOK_STRING ||
            parser->current.kind == JS_TOK_PRIVATE_IDENTIFIER ||
            js_parser_token_is_property_name(parser->current.kind)) {
        SourceSpan span = parser->current.span;
        JsToken token = parser->current;
        js_parser_parser_advance(parser);
        if (js_parser_span_out) *js_parser_span_out = span;
        if (js_parser_token_out) *js_parser_token_out = token;
        if (flags_out) *flags_out = 0;
        return true;
    }
    return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
        "expected an object property name", JS_TOK_IDENTIFIER);
}

static bool js_parser_parse_object(JsParser* parser, SourceSpan* js_parser_span_out) {
    JsToken first = parser->current;
    uint32_t property_count = 0;
    js_parser_parser_advance(parser);
    while (parser->current.kind != JS_TOK_RBRACE) {
        if (parser->current.kind == JS_TOK_EOF) {
            js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_EOF, error_unterminated,
                JS_TOK_RBRACE);
            return false;
        }
        SourceSpan member_start = parser->current.span;
        if (parser->current.kind == JS_TOK_ELLIPSIS) {
            JsToken spread = parser->current;
            js_parser_parser_advance(parser);
            if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) return false;
            SourceSpan spread_span = js_parser_span_from_start(spread, parser->previous.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_SPREAD, spread_span, spread, spread, 0, 1)) {
                return false;
            }
            property_count++;
        } else {
            SourceSpan key;
            JsToken key_token = {0};
            uint32_t key_flags = 0;
            uint32_t method_flags = 0;
            if (parser->current.kind == JS_TOK_STAR) {
                method_flags |= JS_REDUCTION_FLAG_GENERATOR;
                js_parser_parser_advance(parser);
            }
            if (parser->current.kind == JS_TOK_ASYNC &&
                    js_parser_class_member_modifier_candidate(parser->next.kind) &&
                    parser->next.kind != JS_TOK_LPAREN) {
                method_flags |= JS_REDUCTION_FLAG_ASYNC;
                js_parser_parser_advance(parser);
                if (js_parser_parser_accept(parser, JS_TOK_STAR)) {
                    method_flags |= JS_REDUCTION_FLAG_GENERATOR;
                }
            }
            if (parser->current.kind == JS_TOK_GET &&
                    js_parser_class_member_modifier_candidate(parser->next.kind) &&
                    parser->next.kind != JS_TOK_LPAREN) {
                method_flags |= JS_REDUCTION_FLAG_GETTER;
                js_parser_parser_advance(parser);
            } else if (parser->current.kind == JS_TOK_SET &&
                    js_parser_class_member_modifier_candidate(parser->next.kind) &&
                    parser->next.kind != JS_TOK_LPAREN) {
                method_flags |= JS_REDUCTION_FLAG_SETTER;
                js_parser_parser_advance(parser);
            }
            if (!js_parser_parse_object_key(parser, &key, &key_token, &key_flags)) return false;
            if (key_token.kind != JS_TOK_EOF) {
                if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_TOKEN, key, key_token, (JsToken){0},
                        JS_REDUCTION_FLAG_PROPERTY, 0)) return false;
            }
            uint32_t property_flags = key_flags;
            if (js_parser_parser_accept(parser, JS_TOK_COLON)) {
                if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) return false;
                if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_PROPERTY,
                        js_parser_span_from_start((JsToken){.span = key}, parser->previous.span),
                        key_token, (JsToken){0}, property_flags, 2)) return false;
            } else if (parser->current.kind == JS_TOK_LPAREN) {
                uint32_t parameter_count = 0;
                if (!js_parser_parse_parameter_list(parser, &parameter_count)) return false;
                bool old_async = parser->in_async_function;
                bool old_generator = parser->in_generator;
                parser->in_async_function =
                    (method_flags & JS_REDUCTION_FLAG_ASYNC) != 0;
                parser->in_generator =
                    (method_flags & JS_REDUCTION_FLAG_GENERATOR) != 0;
                parser->function_depth++;
                bool body_ok = js_parser_parse_block(parser);
                parser->function_depth--;
                parser->in_async_function = old_async;
                parser->in_generator = old_generator;
                if (!body_ok) return false;
                SourceSpan method_span = js_parser_span_from_start(
                    (JsToken){.span = member_start}, parser->previous.span);
                if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_OBJECT_METHOD, method_span, key_token,
                        (JsToken){0}, method_flags | key_flags,
                        parameter_count + 2)) return false;
            } else if (js_parser_parser_accept(parser, JS_TOK_QUESTION)) {
                if (parser->current.kind == JS_TOK_COLON) js_parser_parser_advance(parser);
                if (parser->current.kind == JS_TOK_EQUAL) {
                    js_parser_parser_advance(parser);
                    if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) return false;
                }
            } else {
                if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_PROPERTY, key, key_token, (JsToken){0},
                        property_flags | JS_REDUCTION_FLAG_SHORTHAND, 1)) return false;
            }
            property_count++;
        }
        if (js_parser_parser_accept(parser, JS_TOK_COMMA)) {
            if (parser->current.kind == JS_TOK_RBRACE) break;
            continue;
        }
        if (parser->current.kind != JS_TOK_RBRACE) {
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                "expected ',' or '}' after object property", JS_TOK_COMMA);
        }
    }
    JsToken last = parser->current;
    js_parser_parser_advance(parser);
    SourceSpan span = js_parser_span_from_tokens(first, last);
    if (js_parser_span_out) *js_parser_span_out = span;
    return js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION, JS_REDUCTION_OBJECT,
        span, first, (JsToken){0}, 0, property_count);
}

static bool js_parser_parser_starts_object_statement(JsParser* parser) {
    if (!parser || parser->current.kind != JS_TOK_LBRACE) return false;
    if (!js_parser_token_is_name(parser->next.kind)) return false;
    // A normal block can begin with a name, notably `return` or a label.
    // Only probe the recovery-shaped object form when the first name is
    // followed by a property colon.
    JsLexer after_next = parser->lexer;
    js_lexer_set_goal(&after_next, js_parser_parser_goal_after(parser->next.kind));
    JsToken separator = js_lexer_next(&after_next);
    if (separator.kind != JS_TOK_COLON) return false;
    // Tree-sitter retains a leading object-shaped expression as a block of
    // labeled statements in statement position, including its recovery-only
    // comma separators. Probe the expression grammar before committing to a
    // strict block parse so the direct AST keeps that established shape.
    JsParserProbe probe;
    js_parser_parser_probe_begin(parser, &probe);
    bool valid = js_parser_parse_object(parser, NULL) &&
        parser->current.kind == JS_TOK_SEMICOLON && js_parser_parse_semicolon(parser);
    js_parser_parser_probe_end(parser, &probe);
    return valid;
}

static bool js_parser_parse_block(JsParser* parser) {
    JsToken first = parser->current;
    uint32_t statement_count = 0;
    bool saved_stop_for_in_of = parser->stop_for_in_of;
    parser->stop_for_in_of = false;
    if (!js_parser_parser_expect(parser, JS_TOK_LBRACE, "expected '{' before block")) {
        parser->stop_for_in_of = saved_stop_for_in_of;
        return false;
    }
    if (!js_parser_parse_statement_list(parser, JS_TOK_RBRACE, &statement_count)) {
        parser->stop_for_in_of = saved_stop_for_in_of;
        return false;
    }
    if (!js_parser_parser_expect(parser, JS_TOK_RBRACE, "expected '}' after block")) {
        parser->stop_for_in_of = saved_stop_for_in_of;
        return false;
    }
    SourceSpan span = js_parser_span_from_start(first, parser->previous.span);
    bool ok = js_parser_parser_reduce(parser, JS_REDUCE_BLOCK, JS_REDUCTION_NONE, span,
        first, (JsToken){0}, 0, statement_count);
    parser->stop_for_in_of = saved_stop_for_in_of;
    return ok;
}

static bool js_parser_parse_function(JsParser* parser, bool declaration, bool async,
        bool allow_anonymous, SourceSpan* js_parser_span_out) {
    JsToken first = parser->current;
    if (async) {
        if (!js_parser_parser_expect(parser, JS_TOK_ASYNC, "expected 'async'")) return false;
    }
    if (!js_parser_parser_expect(parser, JS_TOK_FUNCTION, "expected 'function'")) return false;
    bool generator = js_parser_parser_accept(parser, JS_TOK_STAR);
    bool old_async = parser->in_async_function;
    bool old_generator = parser->in_generator;
    parser->in_async_function = async;
    parser->in_generator = generator;
    JsToken name_token = {0};
    // default-exported function declarations may omit a name, but a present
    // name still belongs to the declaration and must be consumed here.
    bool has_name = declaration || parser->current.kind != JS_TOK_LPAREN;
    if (allow_anonymous && parser->current.kind == JS_TOK_LPAREN) {
        has_name = false;
    }
    if (has_name) {
        if (!js_parser_parse_name(parser, &name_token)) return false;
        if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN, JS_REDUCTION_TOKEN,
                name_token.span, name_token, (JsToken){0},
                JS_REDUCTION_FLAG_BINDING, 0)) return false;
    }
    uint32_t parameter_count = 0;
    uint32_t type_parameter_count = 0;
    if (!js_parser_parse_type_parameters(parser, &type_parameter_count) ||
            !js_parser_parse_parameter_list(parser, &parameter_count)) return false;
    bool has_return_type = false;
    if (js_parser_parser_accept(parser, JS_TOK_COLON)) {
        if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) return false;
        has_return_type = true;
    }
    parser->function_depth++;
    bool ok = js_parser_parse_block(parser);
    parser->function_depth--;
    parser->in_async_function = old_async;
    parser->in_generator = old_generator;
    if (!ok) return false;
    SourceSpan span = js_parser_span_from_start(first, parser->previous.span);
    if (js_parser_span_out) *js_parser_span_out = span;
    uint32_t flags = (async ? 1u : 0u) | (generator ? 2u : 0u) |
        (declaration ? JS_REDUCTION_FLAG_DECLARATION : 0u) |
        (has_name ? JS_REDUCTION_FLAG_NAMED : 0u);
    return js_parser_parser_reduce(parser, declaration ? JS_REDUCE_DECLARATION :
        JS_REDUCE_EXPRESSION,
        JS_REDUCTION_FUNCTION, span, first, (JsToken){0},
        flags, (has_name ? 1u : 0u) + parameter_count +
            (has_return_type ? 1u : 0u) +
            (type_parameter_count ? 1u : 0u) + 1u);
}

static bool js_parser_class_member_modifier_candidate(JsTokenKind kind) {
    return kind == JS_TOK_IDENTIFIER || kind == JS_TOK_PRIVATE_IDENTIFIER ||
        kind == JS_TOK_NUMBER || kind == JS_TOK_BIGINT || kind == JS_TOK_STRING ||
        kind == JS_TOK_TRUE || kind == JS_TOK_FALSE || kind == JS_TOK_NULL ||
        js_parser_token_is_name(kind) || kind == JS_TOK_LBRACKET || kind == JS_TOK_STAR;
}

static bool js_parser_parse_class_member(JsParser* parser, uint32_t* count_out) {
    if (count_out) *count_out = 0;
    if (js_parser_parser_accept(parser, JS_TOK_SEMICOLON)) return true;
    while (parser->current.kind == JS_TOK_AT) {
        js_parser_parser_advance(parser);
        if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) return false;
    }
    JsToken first = parser->current;
    uint32_t flags = 0;
    if (parser->current.kind == JS_TOK_STATIC &&
            !parser->next.line_terminator_before &&
            (js_parser_class_member_modifier_candidate(parser->next.kind) ||
             parser->next.kind == JS_TOK_LBRACE) &&
            parser->next.kind != JS_TOK_LPAREN) {
        flags |= JS_REDUCTION_FLAG_STATIC;
        js_parser_parser_advance(parser);
        if (parser->current.kind == JS_TOK_LBRACE) {
            if (!js_parser_parse_block(parser)) return false;
            if (!js_parser_parser_reduce(parser, JS_REDUCE_CLASS_MEMBER,
                    JS_REDUCTION_STATIC_BLOCK,
                    js_parser_span_from_start(first, parser->previous.span), first,
                    (JsToken){0}, flags, 1)) return false;
            if (count_out) *count_out = 1;
            return true;
        }
    }
    if (parser->current.kind == JS_TOK_ASYNC &&
            js_parser_class_member_modifier_candidate(parser->next.kind) &&
            !parser->next.line_terminator_before &&
            parser->next.kind != JS_TOK_LPAREN) {
        flags |= JS_REDUCTION_FLAG_ASYNC;
        js_parser_parser_advance(parser);
    }
    if (parser->current.kind == JS_TOK_GET &&
            js_parser_class_member_modifier_candidate(parser->next.kind) &&
            !parser->next.line_terminator_before &&
            parser->next.kind != JS_TOK_LPAREN) {
        flags |= JS_REDUCTION_FLAG_GETTER;
        js_parser_parser_advance(parser);
    } else if (parser->current.kind == JS_TOK_SET &&
            js_parser_class_member_modifier_candidate(parser->next.kind) &&
            !parser->next.line_terminator_before &&
            parser->next.kind != JS_TOK_LPAREN) {
        flags |= JS_REDUCTION_FLAG_SETTER;
        js_parser_parser_advance(parser);
    }
    if (js_parser_parser_accept(parser, JS_TOK_STAR)) {
        flags |= JS_REDUCTION_FLAG_GENERATOR;
    }

    SourceSpan key;
    JsToken key_token = {0};
    uint32_t key_flags = 0;
    if (!js_parser_parse_object_key(parser, &key, &key_token, &key_flags)) return false;
    flags |= key_flags;
    if (key_token.kind != JS_TOK_EOF) {
        if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION, JS_REDUCTION_TOKEN,
                key, key_token, (JsToken){0}, JS_REDUCTION_FLAG_PROPERTY, 0)) {
            return false;
        }
    }
    if (js_parser_parser_accept(parser, JS_TOK_QUESTION) || js_parser_parser_accept(parser, JS_TOK_BANG)) {
        // TS optional and definite-assignment markers belong to the member.
    }
    uint32_t type_parameter_count = 0;
    if (parser->current.kind == JS_TOK_LT &&
            (parser->mode & JS_PARSE_TYPESCRIPT)) {
        if (!js_parser_parse_type_parameters(parser, &type_parameter_count)) return false;
    }
    if (parser->current.kind == JS_TOK_LPAREN) {
        uint32_t parameter_count = 0;
        if (!js_parser_parse_parameter_list(parser, &parameter_count)) return false;
        bool has_return_type = false;
        if (js_parser_parser_accept(parser, JS_TOK_COLON)) {
            if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) return false;
            has_return_type = true;
        }
        bool old_async = parser->in_async_function;
        bool old_generator = parser->in_generator;
        parser->in_async_function = (flags & JS_REDUCTION_FLAG_ASYNC) != 0;
        parser->in_generator = (flags & JS_REDUCTION_FLAG_GENERATOR) != 0;
        parser->function_depth++;
        bool body_ok = js_parser_parse_block(parser);
        parser->function_depth--;
        parser->in_async_function = old_async;
        parser->in_generator = old_generator;
        if (!body_ok) return false;
        SourceSpan span = js_parser_span_from_start(first, parser->previous.span);
        if ((parser->mode & JS_PARSE_TYPESCRIPT) &&
                parser->current.span.start_byte > span.end_byte) {
            // the TypeScript reference node owns trivia before the next class
            // token, including the newline after a method body.
            span.end_byte = parser->current.span.start_byte;
        }
        if (!js_parser_parser_reduce(parser, JS_REDUCE_CLASS_MEMBER,
                JS_REDUCTION_METHOD, span, first, (JsToken){0}, flags,
                parameter_count + (has_return_type ? 1u : 0u) +
                    (type_parameter_count ? 1u : 0u) + 2u)) return false;
        if (count_out) *count_out = 1;
        return true;
    }

    bool has_type = false;
    if (js_parser_parser_accept(parser, JS_TOK_COLON)) {
        if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) return false;
        has_type = true;
    }
    bool has_initializer = js_parser_parser_accept(parser, JS_TOK_EQUAL);
    if (has_initializer && !js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) {
        return false;
    }
    JsToken field_end = parser->previous;
    if (!js_parser_parse_semicolon(parser)) return false;
    SourceSpan span = js_parser_span_from_start(first, field_end.span);
    if (!js_parser_parser_reduce(parser, JS_REDUCE_CLASS_MEMBER, JS_REDUCTION_FIELD,
            span, first, (JsToken){0}, flags,
            1u + (has_type ? 1u : 0u) +
                (has_initializer ? 1u : 0u))) {
        return false;
    }
    if (count_out) *count_out = 1;
    return true;
}

static bool js_parser_parse_class(JsParser* parser, bool declaration, SourceSpan* js_parser_span_out) {
    JsToken first = parser->current;
    if (!js_parser_parser_expect(parser, JS_TOK_CLASS, "expected 'class'")) return false;
    JsToken name_token = {0};
    bool has_name = false;
    if (parser->current.kind != JS_TOK_LBRACE &&
            parser->current.kind != JS_TOK_EXTENDS &&
            parser->current.kind != JS_TOK_LT) {
        if (!js_parser_parse_name(parser, &name_token)) return false;
        has_name = true;
        if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN, JS_REDUCTION_TOKEN,
                name_token.span, name_token, (JsToken){0},
                JS_REDUCTION_FLAG_BINDING, 0)) return false;
    } else if (declaration) {
        return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
            error_expected_identifier, JS_TOK_IDENTIFIER);
    }
    uint32_t type_parameter_count = 0;
    if (!js_parser_parse_type_parameters(parser, &type_parameter_count)) return false;
    bool has_superclass = false;
    if (js_parser_parser_accept(parser, JS_TOK_EXTENDS)) {
        has_superclass = true;
        if (!js_parser_parse_expression(parser, JS_BP_RELATION + 1, NULL)) return false;
    }
    if (js_parser_parser_accept(parser, JS_TOK_IMPLEMENTS)) {
        if (!(parser->mode & JS_PARSE_TYPESCRIPT)) {
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT,
                error_unexpected_token, JS_TOK_IDENTIFIER);
        }
        parser->suppress_type_reductions++;
        do {
            if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) {
                parser->suppress_type_reductions--;
                return false;
            }
        } while (js_parser_parser_accept(parser, JS_TOK_COMMA));
        parser->suppress_type_reductions--;
    }
    if (!js_parser_parser_expect(parser, JS_TOK_LBRACE, "expected '{' before class body")) return false;
    JsToken body_first = parser->previous;
    uint32_t member_count = 0;
    while (parser->current.kind != JS_TOK_RBRACE) {
        if (parser->current.kind == JS_TOK_EOF) {
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_EOF,
                error_unterminated, JS_TOK_RBRACE);
        }
        uint32_t member_added = 0;
        if (!js_parser_parse_class_member(parser, &member_added)) return false;
        member_count += member_added;
    }
    JsToken last = parser->current;
    js_parser_parser_advance(parser);
    SourceSpan body_span = js_parser_span_from_tokens(body_first, last);
    if (!js_parser_parser_reduce(parser, JS_REDUCE_CLASS_MEMBER, JS_REDUCTION_CLASS_BODY,
            body_span, body_first, (JsToken){0}, 0, member_count)) return false;
    SourceSpan span = js_parser_span_from_tokens(first, last);
    if (js_parser_span_out) *js_parser_span_out = span;
    uint32_t flags = (declaration ? JS_REDUCTION_FLAG_DECLARATION : 0u) |
        (has_name ? JS_REDUCTION_FLAG_NAMED : 0u) |
        (has_superclass ? JS_REDUCTION_FLAG_SUPER : 0u);
    return js_parser_parser_reduce(parser, declaration ? JS_REDUCE_DECLARATION : JS_REDUCE_EXPRESSION,
        JS_REDUCTION_CLASS, span, first, (JsToken){0}, flags,
        (has_name ? 1u : 0u) + (has_superclass ? 1u : 0u) +
            (type_parameter_count ? 1u : 0u) + 1u);
}

static void js_parser_parser_probe_begin(JsParser* parser, JsParserProbe* probe) {
    probe->parser = *parser;
    probe->metrics = (JsParseMetrics){0};
    probe->error = (JsParseError){0};
    if (parser->metrics) probe->metrics = *parser->metrics;
    if (parser->error) probe->error = *parser->error;
    parser->sink = NULL;
    parser->metrics = NULL;
    parser->error = NULL;
}

static void js_parser_parser_probe_end(JsParser* parser, const JsParserProbe* probe) {
    *parser = probe->parser;
    if (probe->parser.metrics) *probe->parser.metrics = probe->metrics;
    if (probe->parser.error) *probe->parser.error = probe->error;
}

static bool js_parser_parser_has_arrow_head(JsParser* parser) {
    JsParserProbe probe;
    js_parser_parser_probe_begin(parser, &probe);
    bool valid = js_parser_parse_parameter_list_contents(parser, NULL);
    if (valid && (parser->mode & JS_PARSE_TYPESCRIPT) &&
            js_parser_parser_accept(parser, JS_TOK_COLON)) {
        valid = js_parser_parse_type_before_arrow(parser);
    }
    valid = valid && parser->current.kind == JS_TOK_ARROW &&
        !parser->current.line_terminator_before;
    js_parser_parser_probe_end(parser, &probe);
    return valid;
}

static bool js_parser_parser_has_ts_generic_arrow(JsParser* parser) {
    JsParserProbe probe;
    js_parser_parser_probe_begin(parser, &probe);
    bool valid = js_parser_parse_type_parameters(parser, NULL) &&
        js_parser_parse_parameter_list(parser, NULL);
    if (valid && js_parser_parser_accept(parser, JS_TOK_COLON)) {
        valid = js_parser_parse_type_before_arrow(parser);
    }
    valid = valid && parser->current.kind == JS_TOK_ARROW &&
        !parser->current.line_terminator_before;
    js_parser_parser_probe_end(parser, &probe);
    return valid;
}

static bool js_parser_parser_has_async_arrow(JsParser* parser) {
    if (!parser || parser->current.kind != JS_TOK_ASYNC ||
            parser->next.line_terminator_before) return false;
    JsParserProbe probe;
    js_parser_parser_probe_begin(parser, &probe);
    js_parser_parser_advance(parser);
    bool valid = false;
    if (parser->current.kind == JS_TOK_LPAREN) {
        js_parser_parser_advance(parser);
        valid = js_parser_parser_has_arrow_head(parser);
    } else if ((parser->mode & JS_PARSE_TYPESCRIPT) &&
            parser->current.kind == JS_TOK_LT) {
        valid = js_parser_parser_has_ts_generic_arrow(parser);
    } else if (js_parser_token_is_name(parser->current.kind) ||
            parser->current.kind == JS_TOK_PRIVATE_IDENTIFIER) {
        js_parser_parser_advance(parser);
        valid = parser->current.kind == JS_TOK_ARROW &&
            !parser->current.line_terminator_before;
    }
    js_parser_parser_probe_end(parser, &probe);
    return valid;
}

static bool js_parser_parser_has_assignment_pattern(JsParser* parser) {
    if (!parser || parser->probing_assignment_member_base ||
            (parser->current.kind != JS_TOK_LBRACKET &&
            parser->current.kind != JS_TOK_LBRACE)) return false;
    JsParserProbe probe;
    js_parser_parser_probe_begin(parser, &probe);
    parser->assignment_target_pattern = true;
    bool valid = js_parser_parse_pattern(parser, NULL, NULL) &&
        parser->current.kind == JS_TOK_EQUAL;
    js_parser_parser_probe_end(parser, &probe);
    return valid;
}

static bool js_parser_parser_has_for_head_pattern(JsParser* parser) {
    if (!parser || (parser->current.kind != JS_TOK_LBRACKET &&
            parser->current.kind != JS_TOK_LBRACE)) return false;
    JsParserProbe probe;
    js_parser_parser_probe_begin(parser, &probe);
    parser->assignment_target_pattern = true;
    bool valid = js_parser_parse_pattern(parser, NULL, NULL) &&
        (parser->current.kind == JS_TOK_IN ||
         parser->current.kind == JS_TOK_OF);
    js_parser_parser_probe_end(parser, &probe);
    return valid;
}

static bool js_parser_parser_starts_let_declaration(JsParser* parser,
        bool allow_line_terminator) {
    if (!parser || parser->current.kind != JS_TOK_LET) return false;
    if (parser->next.kind == JS_TOK_IN || parser->next.kind == JS_TOK_OF) {
        return false;
    }
    if (!allow_line_terminator && parser->next.line_terminator_before) {
        return false;
    }
    return parser->next.kind == JS_TOK_IDENTIFIER ||
        parser->next.kind == JS_TOK_PRIVATE_IDENTIFIER ||
        parser->next.kind == JS_TOK_LBRACE ||
        parser->next.kind == JS_TOK_LBRACKET ||
        js_parser_token_is_name(parser->next.kind);
}

// Destructuring assignment targets may contain member/index expressions while
// declaration patterns may contain only bindings. Keep that distinction in the
// reduction stream so `{ value: receiver.slot } = source` retains an object
// pattern rather than becoming an object expression.
static bool js_parser_parse_assignment_target_pattern(JsParser* parser,
        SourceSpan* js_parser_span_out) {
    if (!parser || !parser->assignment_target_pattern) return false;
    JsToken first = parser->current;
    SourceSpan span;
    bool expression_base = false;
    if (!js_parser_token_is_name(parser->current.kind) &&
            parser->current.kind != JS_TOK_THIS &&
            parser->current.kind != JS_TOK_SUPER &&
            parser->current.kind != JS_TOK_PRIVATE_IDENTIFIER) {
        if (parser->current.kind != JS_TOK_LBRACE &&
                parser->current.kind != JS_TOK_LBRACKET) return false;
        expression_base = true;
    }
    if (expression_base) {
        if (!js_parser_parse_primary(parser, &span)) return false;
    } else {
        js_parser_parser_advance(parser);
        if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN, JS_REDUCTION_TOKEN,
                first.span, first, (JsToken){0}, 0, 0)) return false;
        span = first.span;
    }
    while (parser->current.kind == JS_TOK_LPAREN ||
            parser->current.kind == JS_TOK_DOT ||
            parser->current.kind == JS_TOK_LBRACKET) {
        JsToken access = parser->current;
        if (access.kind == JS_TOK_LPAREN) {
            uint32_t argument_count = 0;
            bool has_spread = false;
            if (!js_parser_parse_arguments(parser, &argument_count, &has_spread)) {
                return false;
            }
            span = js_parser_span_from_start(first, parser->previous.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_CALL, span, access, access,
                    has_spread ? JS_REDUCTION_FLAG_SPREAD : 0,
                    argument_count + 1)) return false;
        } else if (access.kind == JS_TOK_DOT) {
            js_parser_parser_advance(parser);
            JsToken property;
            if (!js_parser_parse_property_name(parser, &property)) return false;
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_TOKEN, property.span, property,
                    (JsToken){0}, JS_REDUCTION_FLAG_PROPERTY, 0)) return false;
            span = js_parser_span_from_start(first, property.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_MEMBER, span, access, access, 0, 2)) {
                return false;
            }
        } else {
            js_parser_parser_advance(parser);
            SourceSpan index_span;
            if (!js_parser_parse_expression(parser, JS_BP_SEQUENCE, &index_span) ||
                    !js_parser_parser_expect(parser, JS_TOK_RBRACKET,
                        "expected ']' after assignment target")) {
                return false;
            }
            span = js_parser_span_from_start(first, parser->previous.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_SUBSCRIPT, span, access, access,
                    JS_REDUCTION_FLAG_COMPUTED, 2)) return false;
        }
    }
    if (js_parser_span_out) *js_parser_span_out = span;
    return true;
}

static bool js_parser_parser_has_assignment_member_target(JsParser* parser) {
    if (!parser || (parser->current.kind != JS_TOK_LBRACE &&
            parser->current.kind != JS_TOK_LBRACKET)) return false;
    JsParserProbe probe;
    js_parser_parser_probe_begin(parser, &probe);
    // The member base is an expression. Suppress nested assignment-pattern
    // probes so ordinary nested literals are not recursively reparsed.
    parser->probing_assignment_member_base = true;
    SourceSpan base_span;
    bool valid = js_parser_parse_primary(parser, &base_span) &&
        (parser->current.kind == JS_TOK_DOT ||
         parser->current.kind == JS_TOK_LBRACKET);
    js_parser_parser_probe_end(parser, &probe);
    return valid;
}

static bool js_parser_parse_pattern_default_expression(JsParser* parser) {
    bool saved_stop_for_in_of = parser->stop_for_in_of;
    parser->stop_for_in_of = false;
    bool ok = js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL);
    parser->stop_for_in_of = saved_stop_for_in_of;
    return ok;
}

static bool js_parser_template_segment_has_substitution(JsParser* parser, JsToken token) {
    if (!parser || token.span.end_byte < token.span.start_byte ||
            token.span.end_byte > parser->lexer.length) return false;
    size_t length = token.span.end_byte - token.span.start_byte;
    const char* source = parser->lexer.source + token.span.start_byte;
    return length >= 2 && source[length - 2] == '$' && source[length - 1] == '{';
}

static bool js_parser_template_segment_is_tail(JsParser* parser, JsToken token) {
    if (!parser || token.span.end_byte <= token.span.start_byte ||
            token.span.end_byte > parser->lexer.length) return false;
    return parser->lexer.source[token.span.end_byte - 1] == '`';
}

// reduce template segments as they are lexed so substitutions remain ordinary
// expression reductions while the lexer owns nested `${...}` brace depth.
static bool js_parser_parse_template(JsParser* parser, SourceSpan* js_parser_span_out) {
    if (!parser || parser->current.kind != JS_TOK_TEMPLATE) return false;
    JsToken first = parser->current;
    JsToken segment = first;
    uint32_t part_count = 0;
    uint32_t expression_count = 0;
    for (;;) {
        bool substitution = js_parser_template_segment_has_substitution(parser, segment);
        bool tail = js_parser_template_segment_is_tail(parser, segment);
        if (!substitution && !tail) {
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                error_unterminated, JS_TOK_TEMPLATE);
        }
        uint32_t part_flags = tail ? JS_REDUCTION_FLAG_TEMPLATE_TAIL : 0;
        if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                JS_REDUCTION_TEMPLATE_PART, segment.span, segment,
                (JsToken){0}, part_flags, 0)) return false;
        part_count++;
        if (tail) {
            js_parser_parser_advance(parser);
            SourceSpan span = {first.span.start_byte, segment.span.end_byte};
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_TEMPLATE, span, first, first, 0,
                    part_count + expression_count)) return false;
            if (js_parser_span_out) *js_parser_span_out = span;
            return true;
        }

        js_parser_parser_advance(parser);
        // `${...}` starts a fresh expression; a leading slash is a regexp
        // literal even though the preceding template segment ends an item.
        js_parser_parser_relex_current_as_regex(parser);
        if (parser->current.kind == JS_TOK_RBRACE ||
                parser->current.kind == JS_TOK_EOF) {
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                error_expected_expression, JS_TOK_IDENTIFIER);
        }
        if (!js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL)) return false;
        if (!js_parser_parser_expect(parser, JS_TOK_RBRACE,
                "expected '}' after template substitution")) return false;
        expression_count++;
        if (parser->current.kind != JS_TOK_TEMPLATE) {
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                error_unterminated, JS_TOK_TEMPLATE);
        }
        segment = parser->current;
    }
}

static bool js_parser_parse_primary(JsParser* parser, SourceSpan* js_parser_span_out) {
    if (!js_parser_parser_enter(parser)) return false;
    JsToken first = parser->current;
    bool ok = true;
    bool suppress_leaf = false;
    bool contextual_yield_identifier = first.kind == JS_TOK_YIELD &&
        !parser->in_generator && !(parser->mode & JS_PARSE_MODULE);
    bool contextual_await_identifier = first.kind == JS_TOK_AWAIT &&
        !parser->in_async_function && !(parser->mode & JS_PARSE_MODULE);
    SourceSpan span = first.span;

    // A single identifier arrow head is otherwise indistinguishable from a
    // normal identifier after the token reduction has been committed.
    if ((first.kind == JS_TOK_IDENTIFIER ||
            first.kind == JS_TOK_PRIVATE_IDENTIFIER ||
            contextual_await_identifier || contextual_yield_identifier) &&
            parser->next.kind == JS_TOK_ARROW &&
            !parser->next.line_terminator_before) {
        js_parser_parser_advance(parser);
        if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN, JS_REDUCTION_TOKEN,
                first.span, first, (JsToken){0},
                JS_REDUCTION_FLAG_BINDING, 0)) {
            js_parser_parser_leave(parser);
            return false;
        }
        JsToken arrow = parser->current;
        js_parser_parser_advance(parser);
        ok = js_parser_parse_arrow_body(parser);
        span = js_parser_span_from_start(first, parser->previous.span);
        if (ok) ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
            JS_REDUCTION_ARROW, span, arrow, arrow, 0, 2);
        if (ok && js_parser_span_out) *js_parser_span_out = span;
        js_parser_parser_leave(parser);
        return ok;
    }

    if (contextual_await_identifier || contextual_yield_identifier) {
        js_parser_parser_advance(parser);
        if (js_parser_span_out) *js_parser_span_out = span;
        js_parser_parser_leave(parser);
        return js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
            JS_REDUCTION_TOKEN, span, first, (JsToken){0}, 0, 0);
    }

    if (first.kind == JS_TOK_ASYNC && js_parser_parser_has_async_arrow(parser)) {
        js_parser_parser_advance(parser);
        uint32_t parameter_count = 0;
        uint32_t type_parameter_count = 0;
        bool has_return_type = false;
        bool single_parameter = parser->current.kind != JS_TOK_LPAREN &&
            parser->current.kind != JS_TOK_LT;
        if (single_parameter) {
            if (!js_parser_parse_pattern(parser, NULL, NULL)) {
                js_parser_parser_leave(parser);
                return false;
            }
            parameter_count = 1;
        } else {
            if ((parser->mode & JS_PARSE_TYPESCRIPT) &&
                    parser->current.kind == JS_TOK_LT &&
                    !js_parser_parse_type_parameters(parser, &type_parameter_count)) {
                js_parser_parser_leave(parser);
                return false;
            }
            if (!js_parser_parse_parameter_list(parser, &parameter_count)) {
                js_parser_parser_leave(parser);
                return false;
            }
            if ((parser->mode & JS_PARSE_TYPESCRIPT) &&
                    js_parser_parser_accept(parser, JS_TOK_COLON)) {
                if (!js_parser_parse_type_before_arrow(parser)) {
                    js_parser_parser_leave(parser);
                    return false;
                }
                has_return_type = true;
            }
        }
        if (!js_parser_parser_expect(parser, JS_TOK_ARROW,
                "expected '=>' after async arrow parameters") ||
                !js_parser_parse_arrow_body_with_context(parser, true, false)) {
            js_parser_parser_leave(parser);
            return false;
        }
        span = js_parser_span_from_start(first, parser->previous.span);
        bool reduced = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
            JS_REDUCTION_ARROW, span, first, first, 1u,
            parameter_count + (has_return_type ? 1u : 0u) +
                (type_parameter_count ? 1u : 0u) + 1u);
        if (reduced && js_parser_span_out) *js_parser_span_out = span;
        js_parser_parser_leave(parser);
        return reduced;
    }

    if (contextual_yield_identifier) {
        js_parser_parser_advance(parser);
    } else switch (parser->current.kind) {
    case JS_TOK_IDENTIFIER: case JS_TOK_PRIVATE_IDENTIFIER:
    case JS_TOK_TRUE: case JS_TOK_FALSE: case JS_TOK_NULL:
    case JS_TOK_THIS: case JS_TOK_SUPER: case JS_TOK_NUMBER:
    case JS_TOK_BIGINT: case JS_TOK_STRING: case JS_TOK_REGEXP:
    case JS_TOK_ASYNC:
        if (parser->next.kind == JS_TOK_FUNCTION &&
                !parser->next.line_terminator_before) {
            ok = js_parser_parse_function(parser, false, true, false, &span);
            suppress_leaf = true;
            break;
        }
        js_parser_parser_advance(parser);
        break;
    case JS_TOK_LET:
        if ((parser->mode & JS_PARSE_MODULE) && !parser->stop_for_in_of) {
            ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                error_expected_expression, JS_TOK_IDENTIFIER);
        } else {
            js_parser_parser_advance(parser);
        }
        break;
    case JS_TOK_AS: case JS_TOK_ASSERTS: case JS_TOK_ABSTRACT:
    case JS_TOK_ANY: case JS_TOK_BOOLEAN: case JS_TOK_DECLARE:
    case JS_TOK_ENUM: case JS_TOK_FROM: case JS_TOK_IMPLEMENTS:
    case JS_TOK_INFER: case JS_TOK_INTERFACE: case JS_TOK_IS: case JS_TOK_KEYOF:
    case JS_TOK_MODULE: case JS_TOK_NAMESPACE: case JS_TOK_NEVER:
    case JS_TOK_NUMBER_TYPE: case JS_TOK_OBJECT: case JS_TOK_PACKAGE:
    case JS_TOK_PRIVATE: case JS_TOK_PROTECTED: case JS_TOK_PUBLIC:
    case JS_TOK_READONLY: case JS_TOK_REQUIRE: case JS_TOK_SATISFIES:
    case JS_TOK_STATIC: case JS_TOK_STRING_TYPE: case JS_TOK_SYMBOL:
    case JS_TOK_TYPE: case JS_TOK_UNKNOWN: case JS_TOK_GET: case JS_TOK_SET:
    case JS_TOK_OF:
        js_parser_parser_advance(parser);
        break;
    case JS_TOK_FUNCTION:
        ok = js_parser_parse_function(parser, false, false, false, &span);
        break;
    case JS_TOK_CLASS:
        ok = js_parser_parse_class(parser, false, &span);
        break;
    case JS_TOK_TEMPLATE:
        ok = js_parser_parse_template(parser, &span);
        break;
    case JS_TOK_NEW:
        js_parser_parser_advance(parser);
        if (js_parser_parser_accept(parser, JS_TOK_DOT)) {
            JsToken target;
            ok = js_parser_parse_name(parser, &target);
            if (ok) {
                span = js_parser_span_from_start(first, target.span);
                ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_TOKEN, span, first, target, 0, 0);
            }
        } else {
            SourceSpan callee_span;
            ok = js_parser_parse_primary(parser, &callee_span);
            // Member access binds into a constructor target before the
            // constructor's own argument list: `new ns.Ctor(value)` is a
            // NEW expression whose callee is `ns.Ctor`, not a call of a
            // separately built member expression.
            while (ok && (parser->current.kind == JS_TOK_DOT ||
                    parser->current.kind == JS_TOK_LBRACKET)) {
                JsToken access = parser->current;
                if (access.kind == JS_TOK_DOT) {
                    js_parser_parser_advance(parser);
                    JsToken property;
                    if (!js_parser_parse_property_name(parser, &property)) {
                        ok = false;
                        break;
                    }
                    if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                            JS_REDUCTION_TOKEN, property.span, property,
                            (JsToken){0}, JS_REDUCTION_FLAG_PROPERTY, 0)) {
                        ok = false;
                        break;
                    }
                    SourceSpan member_span = js_parser_span_from_start(
                        (JsToken){.span = callee_span}, parser->previous.span);
                    ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_MEMBER, member_span, access, access,
                        0, 2);
                    callee_span = member_span;
                } else {
                    js_parser_parser_advance(parser);
                    ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) &&
                        js_parser_parser_expect(parser, JS_TOK_RBRACKET,
                            "expected ']' after constructor target");
                    if (ok) {
                        SourceSpan subscript_span = js_parser_span_from_start(
                            (JsToken){.span = callee_span},
                            parser->previous.span);
                        ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                            JS_REDUCTION_SUBSCRIPT, subscript_span, access,
                            access, JS_REDUCTION_FLAG_COMPUTED, 2);
                        callee_span = subscript_span;
                    }
                }
            }
            if (ok && parser->current.kind == JS_TOK_TEMPLATE) {
                JsToken template_token = parser->current;
                SourceSpan template_span;
                ok = js_parser_parse_template(parser, &template_span);
                if (ok) {
                    SourceSpan tagged_span = js_parser_span_from_start(
                        (JsToken){.span = callee_span}, template_span);
                    ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_TAGGED_TEMPLATE, tagged_span,
                        template_token, template_token, 0, 2);
                    callee_span = tagged_span;
                }
            }
            uint32_t argument_count = 0;
            bool has_spread = false;
            if (ok && parser->current.kind == JS_TOK_LPAREN) {
                ok = js_parser_parse_arguments(parser, &argument_count, &has_spread);
            }
            if (ok) {
                span = js_parser_span_from_start(first, parser->previous.span);
                ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_NEW, span, first, first,
                    has_spread ? JS_REDUCTION_FLAG_SPREAD : 0,
                    argument_count + 1);
            }
        }
        span = js_parser_span_from_start(first, parser->previous.span);
        break;
    case JS_TOK_IMPORT:
        js_parser_parser_advance(parser);
        if (js_parser_parser_accept(parser, JS_TOK_DOT)) {
            ok = js_parser_parse_name(parser, NULL);
            if (ok) {
                span = js_parser_span_from_start(first, parser->previous.span);
                ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_TOKEN, span, first, (JsToken){0}, 0, 0);
            }
        } else if (parser->current.kind == JS_TOK_LPAREN) {
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_TOKEN, first.span, first, (JsToken){0},
                    0, 0)) ok = false;
            uint32_t argument_count = 0;
            bool has_spread = false;
            if (ok) ok = js_parser_parse_arguments(parser, &argument_count,
                &has_spread);
            if (ok) {
                span = js_parser_span_from_start(first, parser->previous.span);
                ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_CALL, span, first, first,
                    has_spread ? JS_REDUCTION_FLAG_SPREAD : 0,
                    argument_count + 1);
            }
        } else {
            ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                error_expected_expression, JS_TOK_LPAREN);
        }
        span = js_parser_span_from_start(first, parser->previous.span);
        break;
    case JS_TOK_LPAREN:
        bool saved_stop_for_in_of = parser->stop_for_in_of;
        parser->stop_for_in_of = false;
        js_parser_parser_advance(parser);
        if (js_parser_parser_accept(parser, JS_TOK_RPAREN)) {
            bool has_return_type = false;
            if ((parser->mode & JS_PARSE_TYPESCRIPT) &&
                    js_parser_parser_accept(parser, JS_TOK_COLON)) {
                if (!js_parser_parse_type_before_arrow(parser)) ok = false;
                else has_return_type = true;
            }
            if (ok && !js_parser_parser_accept(parser, JS_TOK_ARROW)) {
                ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                    error_expected_expression, JS_TOK_IDENTIFIER);
            } else if (ok) {
                ok = js_parser_parse_arrow_body(parser);
                if (ok) {
                    span = js_parser_span_from_start(first, parser->previous.span);
                    ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_ARROW, span, parser->previous,
                        parser->previous, 0,
                        (has_return_type ? 1u : 0u) + 1u);
                }
            }
        } else if (js_parser_parser_has_arrow_head(parser)) {
            uint32_t parameter_count = 0;
            bool has_return_type = false;
            ok = js_parser_parse_parameter_list_contents(parser, &parameter_count);
            if (ok && (parser->mode & JS_PARSE_TYPESCRIPT) &&
                    js_parser_parser_accept(parser, JS_TOK_COLON)) {
                ok = js_parser_parse_type_before_arrow(parser);
                has_return_type = ok;
            }
            ok = ok &&
                js_parser_parser_expect(parser, JS_TOK_ARROW, "expected '=>' after arrow parameters");
            if (ok) {
                    ok = js_parser_parse_arrow_body(parser);
                    if (ok) {
                        span = js_parser_span_from_start(first, parser->previous.span);
                        ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                            JS_REDUCTION_ARROW, span, parser->previous,
                            parser->previous, 0, parameter_count +
                                (has_return_type ? 1u : 0u) + 1u);
                    }
            }
        } else if (js_parser_parser_has_assignment_pattern(parser)) {
            SourceSpan pattern_span;
            bool saved_assignment_target_pattern =
                parser->assignment_target_pattern;
            parser->assignment_target_pattern = true;
            if (!js_parser_parse_pattern(parser, &pattern_span, NULL)) {
                ok = false;
            } else {
                JsToken equal = parser->current;
                js_parser_parser_advance(parser);
                SourceSpan right_span;
                ok = js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, &right_span);
                SourceSpan assignment_span = {
                    pattern_span.start_byte, right_span.end_byte};
                if (ok) ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_ASSIGNMENT, assignment_span, equal, equal,
                    0, 2);
                while (ok && js_parser_parser_accept(parser, JS_TOK_COMMA)) {
                    JsToken comma = parser->previous;
                    SourceSpan next_span;
                    ok = js_parser_parse_expression(parser, JS_BP_ASSIGNMENT,
                        &next_span);
                    SourceSpan sequence_span = {
                        pattern_span.start_byte, next_span.end_byte};
                    if (ok) ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_SEQUENCE, sequence_span, comma, comma,
                        0, 2);
                    assignment_span = sequence_span;
                }
                if (ok) ok = js_parser_parser_expect(parser, JS_TOK_RPAREN,
                    "expected ')' after assignment");
                span = assignment_span;
            }
            parser->assignment_target_pattern =
                saved_assignment_target_pattern;
        } else {
            ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL);
            if (ok) ok = js_parser_parser_expect(parser, JS_TOK_RPAREN,
                "expected ')' after expression");
            if (ok && js_parser_parser_accept(parser, JS_TOK_ARROW)) {
                ok = js_parser_parse_arrow_body(parser);
            }
        }
        span = js_parser_span_from_start(first, parser->previous.span);
        parser->stop_for_in_of = saved_stop_for_in_of;
        break;
    case JS_TOK_LBRACKET:
        ok = js_parser_parse_array(parser, &span);
        break;
    case JS_TOK_LBRACE:
        ok = js_parser_parse_object(parser, &span);
        break;
    case JS_TOK_PLUS: case JS_TOK_MINUS: case JS_TOK_BANG:
    case JS_TOK_TILDE: case JS_TOK_TYPEOF: case JS_TOK_VOID:
    case JS_TOK_DELETE: case JS_TOK_AWAIT: case JS_TOK_YIELD:
    case JS_TOK_PLUS_PLUS: case JS_TOK_MINUS_MINUS:
        js_parser_parser_advance(parser);
        if (first.kind == JS_TOK_AWAIT && !parser->in_async_function &&
                !(parser->mode & JS_PARSE_MODULE)) {
            // await remains a contextual identifier in scripts; the token is
            // still accepted here so expression parsing stays mode-neutral.
        }
        if (first.kind == JS_TOK_YIELD && !parser->in_generator) {
            // the early-error pass owns the exact yield-context diagnostic.
        }
        if (first.kind == JS_TOK_YIELD) {
            bool delegate = js_parser_parser_accept(parser, JS_TOK_STAR);
            bool has_argument = parser->current.kind != JS_TOK_SEMICOLON &&
                parser->current.kind != JS_TOK_RBRACE &&
                parser->current.kind != JS_TOK_RBRACKET &&
                parser->current.kind != JS_TOK_RPAREN &&
                parser->current.kind != JS_TOK_COMMA &&
                parser->current.kind != JS_TOK_COLON &&
                parser->current.kind != JS_TOK_EOF &&
                (delegate || !parser->current.line_terminator_before);
            SourceSpan argument_span = first.span;
            if (has_argument) {
                ok = js_parser_parse_expression(parser, JS_BP_ASSIGNMENT,
                    &argument_span);
            }
            span = has_argument ? js_parser_span_from_start(first, argument_span) :
                first.span;
            if (ok) {
                ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_PREFIX, span, first, first,
                    delegate ? JS_REDUCTION_FLAG_YIELD_DELEGATE : 0,
                    has_argument ? 1 : 0);
            }
        } else {
            ok = js_parser_parse_expression(parser, JS_BP_PREFIX, &span);
            span = js_parser_span_from_start(first, span);
        }
        break;
    case JS_TOK_LT:
        if (parser->mode & JS_PARSE_TYPESCRIPT) {
            if (js_parser_parser_has_ts_generic_arrow(parser)) {
                uint32_t parameter_count = 0;
                uint32_t type_parameter_count = 0;
                bool has_return_type = false;
                if (!js_parser_parse_type_parameters(parser, &type_parameter_count) ||
                        !js_parser_parse_parameter_list(parser, &parameter_count)) {
                    ok = false;
                } else {
                    if (js_parser_parser_accept(parser, JS_TOK_COLON)) {
                        if (!js_parser_parse_type_before_arrow(parser)) ok = false;
                        else has_return_type = true;
                    }
                    if (ok && !js_parser_parser_expect(parser, JS_TOK_ARROW,
                            "expected '=>' after generic parameters")) ok = false;
                    if (ok) {
                        ok = js_parser_parse_arrow_body(parser);
                        if (ok) {
                            span = js_parser_span_from_start(first, parser->previous.span);
                                ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                                JS_REDUCTION_ARROW, span, first, first, 0,
                                parameter_count +
                                    (has_return_type ? 1u : 0u) +
                                    (type_parameter_count ? 1u : 0u) + 1u);
                        }
                    }
                }
            } else {
                JsToken assertion = first;
                js_parser_parser_advance(parser);
                ok = js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL) &&
                    js_parser_parser_expect(parser, JS_TOK_GT, "expected '>' after type assertion") &&
                    js_parser_parse_expression(parser, JS_BP_PREFIX, &span);
                if (ok) {
                    span = js_parser_span_from_start(assertion, span);
                    ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_TYPE_ASSERTION, span, assertion, assertion,
                        0, 2);
                }
            }
            span = js_parser_span_from_start(first, span);
        } else {
            ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                error_expected_expression, JS_TOK_IDENTIFIER);
        }
        break;
    default:
        ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
            error_expected_expression, JS_TOK_IDENTIFIER);
        break;
    }
    if (ok && js_parser_span_out) *js_parser_span_out = span;
    if (ok && (first.kind == JS_TOK_PLUS || first.kind == JS_TOK_MINUS ||
            first.kind == JS_TOK_BANG || first.kind == JS_TOK_TILDE ||
            first.kind == JS_TOK_TYPEOF || first.kind == JS_TOK_VOID ||
            first.kind == JS_TOK_DELETE || first.kind == JS_TOK_AWAIT ||
            first.kind == JS_TOK_YIELD || first.kind == JS_TOK_PLUS_PLUS ||
            first.kind == JS_TOK_MINUS_MINUS)) {
        if (first.kind != JS_TOK_YIELD) {
            ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                JS_REDUCTION_PREFIX, span, first, first, 0, 1);
        }
    }
    if (ok && first.kind != JS_TOK_FUNCTION && first.kind != JS_TOK_CLASS &&
            first.kind != JS_TOK_LPAREN && first.kind != JS_TOK_LBRACKET &&
            first.kind != JS_TOK_LBRACE &&
            first.kind != JS_TOK_PLUS && first.kind != JS_TOK_MINUS &&
            first.kind != JS_TOK_BANG && first.kind != JS_TOK_TILDE &&
            first.kind != JS_TOK_TYPEOF && first.kind != JS_TOK_VOID &&
            first.kind != JS_TOK_DELETE && first.kind != JS_TOK_AWAIT &&
            (first.kind != JS_TOK_YIELD || contextual_yield_identifier) &&
            first.kind != JS_TOK_PLUS_PLUS &&
            first.kind != JS_TOK_MINUS_MINUS && first.kind != JS_TOK_NEW &&
            first.kind != JS_TOK_IMPORT && first.kind != JS_TOK_TEMPLATE &&
            first.kind != JS_TOK_LT && !suppress_leaf) {
        ok = js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
            JS_REDUCTION_TOKEN, span, first, (JsToken){0}, 0, 0);
    }
    js_parser_parser_leave(parser);
    return ok;
}

static bool js_parser_parse_postfix(JsParser* parser, SourceSpan* js_parser_span_out) {
    SourceSpan span;
    if (!js_parser_parse_primary(parser, &span)) return false;
    for (;;) {
        JsToken op = parser->current;
        if (op.kind == JS_TOK_DOT) {
            js_parser_parser_advance(parser);
            JsToken property;
            if (!js_parser_parse_property_name(parser, &property)) return false;
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_TOKEN, property.span, property,
                    (JsToken){0}, JS_REDUCTION_FLAG_PROPERTY, 0)) return false;
            span = js_parser_span_from_start((JsToken){.span = span}, parser->previous.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_MEMBER, span, op, op, 0, 2)) return false;
        } else if (op.kind == JS_TOK_QUESTION_DOT && parser->next.kind == JS_TOK_LBRACKET) {
            js_parser_parser_advance(parser);
            op = parser->current;
            js_parser_parser_advance(parser);
            if (!js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) ||
                    !js_parser_parser_expect(parser, JS_TOK_RBRACKET,
                        "expected ']' after subscript")) return false;
            span = js_parser_span_from_start((JsToken){.span = span}, parser->previous.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_SUBSCRIPT, span, op, op,
                    JS_REDUCTION_FLAG_OPTIONAL | JS_REDUCTION_FLAG_COMPUTED,
                    2)) return false;
        } else if (op.kind == JS_TOK_LBRACKET) {
            js_parser_parser_advance(parser);
            if (!js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) ||
                    !js_parser_parser_expect(parser, JS_TOK_RBRACKET,
                        "expected ']' after subscript")) return false;
            span = js_parser_span_from_start((JsToken){.span = span}, parser->previous.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_SUBSCRIPT, span, op, op,
                    JS_REDUCTION_FLAG_COMPUTED, 2)) return false;
        } else if (op.kind == JS_TOK_QUESTION_DOT && parser->next.kind == JS_TOK_LPAREN) {
            js_parser_parser_advance(parser);
            op = parser->current;
            uint32_t argument_count = 0;
            bool has_spread = false;
            if (!js_parser_parse_arguments(parser, &argument_count, &has_spread)) return false;
            span = js_parser_span_from_start((JsToken){.span = span}, parser->previous.span);
            uint32_t flags = JS_REDUCTION_FLAG_OPTIONAL |
                (has_spread ? JS_REDUCTION_FLAG_SPREAD : 0);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_CALL, span, op, op, flags,
                    argument_count + 1)) return false;
        } else if (op.kind == JS_TOK_QUESTION_DOT) {
            js_parser_parser_advance(parser);
            op = parser->current;
            JsToken property;
            if (!js_parser_parse_property_name(parser, &property)) return false;
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_TOKEN, property.span, property,
                    (JsToken){0}, JS_REDUCTION_FLAG_PROPERTY, 0)) return false;
            span = js_parser_span_from_start((JsToken){.span = span}, parser->previous.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_MEMBER, span, op, op,
                    JS_REDUCTION_FLAG_OPTIONAL, 2)) return false;
        } else if (op.kind == JS_TOK_LPAREN) {
            uint32_t argument_count = 0;
            bool has_spread = false;
            if (!js_parser_parse_arguments(parser, &argument_count, &has_spread)) return false;
            span = js_parser_span_from_start((JsToken){.span = span}, parser->previous.span);
            uint32_t flags = has_spread ? JS_REDUCTION_FLAG_SPREAD : 0;
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_CALL, span, op, op, flags,
                    argument_count + 1)) return false;
        } else if (op.kind == JS_TOK_TEMPLATE) {
            SourceSpan quasi_span;
            if (!js_parser_parse_template(parser, &quasi_span)) return false;
            span = js_parser_span_from_start((JsToken){.span = span}, quasi_span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_TAGGED_TEMPLATE, span, op, op, 0, 2)) {
                return false;
            }
        } else if (op.kind == JS_TOK_BANG &&
                (parser->mode & JS_PARSE_TYPESCRIPT) &&
                !op.line_terminator_before) {
            js_parser_parser_advance(parser);
            span = js_parser_span_from_start((JsToken){.span = span}, op.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_NON_NULL, span, op, op, 0, 1)) {
                return false;
            }
        } else if ((op.kind == JS_TOK_PLUS_PLUS || op.kind == JS_TOK_MINUS_MINUS) &&
                !op.line_terminator_before) {
            js_parser_parser_advance(parser);
            span = js_parser_span_from_start((JsToken){.span = span}, op.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION, JS_REDUCTION_POSTFIX,
                span, op, op, 0, 1)) return false;
        } else {
            break;
        }
    }
    if (js_parser_span_out) *js_parser_span_out = span;
    return true;
}

static bool js_parser_parse_expression(JsParser* parser, int min_bp, SourceSpan* js_parser_span_out) {
    if (!js_parser_parser_enter(parser)) return false;
    SourceSpan left;
    if (min_bp <= JS_BP_ASSIGNMENT && js_parser_parser_has_assignment_pattern(parser)) {
        // Destructuring assignment heads are patterns, not array/object
        // expressions. Recognize them before the ordinary Pratt prefix so the
        // sink receives the same LHS node shape as the reference AST.
        SourceSpan pattern_span;
        bool saved_assignment_target_pattern =
            parser->assignment_target_pattern;
        parser->assignment_target_pattern = true;
        if (!js_parser_parse_pattern(parser, &pattern_span, NULL) ||
                !js_parser_parser_expect(parser, JS_TOK_EQUAL,
                    "expected '=' after assignment pattern")) {
            parser->assignment_target_pattern =
                saved_assignment_target_pattern;
            js_parser_parser_leave(parser);
            return false;
        }
        JsToken equal = parser->previous;
        SourceSpan right;
        if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, &right)) {
            js_parser_parser_leave(parser);
            return false;
        }
        left = js_parser_span_from_start((JsToken){.span = pattern_span}, right);
        if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                JS_REDUCTION_ASSIGNMENT, left, equal, equal, 0, 2)) {
            parser->assignment_target_pattern =
                saved_assignment_target_pattern;
            js_parser_parser_leave(parser);
            return false;
        }
        parser->assignment_target_pattern = saved_assignment_target_pattern;
    } else if (!js_parser_parse_postfix(parser, &left)) {
        js_parser_parser_leave(parser);
        return false;
    }
    // Identifier and parenthesized heads commit to an arrow only after its
    // introducer is visible; no sink call is made during that probe.
    if (parser->current.kind == JS_TOK_ARROW && min_bp <= JS_BP_ASSIGNMENT) {
        JsToken arrow = parser->current;
        js_parser_parser_advance(parser);
        bool ok = js_parser_parse_arrow_body(parser);
        if (!ok) {
            js_parser_parser_leave(parser);
            return false;
        }
        left = js_parser_span_from_start((JsToken){.span = left}, parser->previous.span);
        if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                JS_REDUCTION_ARROW, left, arrow, arrow, 0, 1)) {
            js_parser_parser_leave(parser);
            return false;
        }
    }
    while (parser->status == JS_PARSE_OK) {
        JsToken op = parser->current;
        if ((parser->mode & JS_PARSE_TYPESCRIPT) &&
                (op.kind == JS_TOK_AS || op.kind == JS_TOK_SATISFIES)) {
            if (min_bp > JS_BP_RELATION) break;
            js_parser_parser_advance(parser);
            if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) {
                js_parser_parser_leave(parser);
                return false;
            }
            left = js_parser_span_from_start((JsToken){.span = left}, parser->previous.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_BINARY, left, op, op, 0, 2)) {
                js_parser_parser_leave(parser);
                return false;
            }
            continue;
        }
        bool right_associative = false;
        int bp = js_parser_token_binding_power(op.kind, &right_associative);
        if (parser->stop_for_in_of &&
                (op.kind == JS_TOK_IN || op.kind == JS_TOK_OF)) bp = 0;
        if (!bp || bp < min_bp) break;
        js_parser_parser_advance(parser);
        if (op.kind == JS_TOK_QUESTION) {
            bool saved_stop_for_in_of = parser->stop_for_in_of;
            // Conditional branches are AssignmentExpressions[+In], even
            // when the conditional itself is the initializer of a for head.
            parser->stop_for_in_of = false;
            bool branch_ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL);
            parser->stop_for_in_of = saved_stop_for_in_of;
            if (!branch_ok || !js_parser_parser_expect(parser, JS_TOK_COLON,
                        "expected ':' in conditional expression")) {
                js_parser_parser_leave(parser);
                return false;
            }
            parser->stop_for_in_of = false;
            branch_ok = js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL);
            parser->stop_for_in_of = saved_stop_for_in_of;
            if (!branch_ok) {
                js_parser_parser_leave(parser);
                return false;
            }
        } else {
            int rhs_bp = right_associative ? bp : bp + 1;
            if (!js_parser_parse_expression(parser, rhs_bp, NULL)) {
                js_parser_parser_leave(parser);
                return false;
            }
        }
        left = js_parser_span_from_start((JsToken){.span = left}, parser->previous.span);
        JsReductionForm form = js_parser_token_is_assignment(op.kind)
            ? JS_REDUCTION_ASSIGNMENT
            : (op.kind == JS_TOK_COMMA ? JS_REDUCTION_SEQUENCE
                : (op.kind == JS_TOK_QUESTION ? JS_REDUCTION_CONDITIONAL
                    : JS_REDUCTION_BINARY));
        if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION, form,
                left, op, op, 0,
                form == JS_REDUCTION_CONDITIONAL ? 3 : 2)) {
            js_parser_parser_leave(parser);
            return false;
        }
    }
    if (js_parser_span_out) *js_parser_span_out = left;
    js_parser_parser_leave(parser);
    return true;
}

static bool js_parser_parse_type_primary(JsParser* parser, SourceSpan* js_parser_span_out) {
    if (!js_parser_parser_enter(parser)) return false;
    JsToken first = parser->current;
    SourceSpan span = first.span;
    bool ok = true;
    if (parser->current.kind == JS_TOK_LPAREN) {
        js_parser_parser_advance(parser);
        if (parser->current.kind != JS_TOK_RPAREN) {
            do {
                bool named_parameter = js_parser_token_is_type_name(parser->current.kind) &&
                    (parser->next.kind == JS_TOK_COLON ||
                        parser->next.kind == JS_TOK_QUESTION);
                if (named_parameter) {
                    if (!js_parser_parse_name(parser, NULL)) { ok = false; break; }
                    js_parser_parser_accept(parser, JS_TOK_QUESTION);
                    if (!js_parser_parser_expect(parser, JS_TOK_COLON,
                            "expected ':' in function type parameter") ||
                            !js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) {
                        ok = false;
                        break;
                    }
                } else if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) {
                    ok = false;
                    break;
                }
                if (js_parser_parser_accept(parser, JS_TOK_COMMA)) continue;
                break;
            } while (true);
        }
        if (ok) ok = js_parser_parser_expect(parser, JS_TOK_RPAREN,
            "expected ')' in type expression");
        if (ok && js_parser_parser_accept(parser, JS_TOK_ARROW)) {
            ok = js_parser_parse_type(parser, JS_BP_ASSIGNMENT, NULL);
        }
    } else if (parser->current.kind == JS_TOK_LBRACE) {
        js_parser_parser_advance(parser);
        while (parser->current.kind != JS_TOK_RBRACE) {
            if (parser->current.kind == JS_TOK_EOF) { ok = false; break; }
            if (js_parser_parser_accept(parser, JS_TOK_LPAREN)) {
                // call signatures in object types
                if (!js_parser_parser_expect(parser, JS_TOK_RPAREN,
                        "expected ')' in call signature") ||
                        !js_parser_parser_expect(parser, JS_TOK_COLON,
                            "expected ':' in call signature") ||
                        !js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) {
                    ok = false;
                    break;
                }
            } else {
                if (!js_parser_parse_name(parser, NULL)) { ok = false; break; }
                if (js_parser_parser_accept(parser, JS_TOK_QUESTION)) {}
                if (!js_parser_parser_expect(parser, JS_TOK_COLON,
                        "expected ':' in object type") ||
                        !js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) {
                    ok = false;
                    break;
                }
            }
            if (!js_parser_parser_accept(parser, JS_TOK_SEMICOLON)) js_parser_parser_accept(parser, JS_TOK_COMMA);
        }
        if (ok) ok = js_parser_parser_expect(parser, JS_TOK_RBRACE,
            "expected '}' after object type");
    } else if (parser->current.kind == JS_TOK_LBRACKET) {
        js_parser_parser_advance(parser);
        if (parser->current.kind != JS_TOK_RBRACKET) {
            do {
                if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) { ok = false; break; }
            } while (js_parser_parser_accept(parser, JS_TOK_COMMA));
        }
        if (ok) ok = js_parser_parser_expect(parser, JS_TOK_RBRACKET,
            "expected ']' after tuple type");
    } else if (js_parser_token_is_type_name(parser->current.kind)) {
        js_parser_parser_advance(parser);
        if ((parser->mode & JS_PARSE_TYPESCRIPT) && parser->current.kind == JS_TOK_LT) {
            js_parser_parser_advance(parser);
            if (parser->current.kind != JS_TOK_GT) {
                do {
                    if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) { ok = false; break; }
                } while (js_parser_parser_accept(parser, JS_TOK_COMMA));
            }
            if (ok) ok = js_parser_parser_expect(parser, JS_TOK_GT,
                "expected '>' after type arguments");
        }
    } else if (parser->current.kind == JS_TOK_STRING ||
            parser->current.kind == JS_TOK_NUMBER) {
        js_parser_parser_advance(parser);
    } else {
        ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
            error_expected_type, JS_TOK_IDENTIFIER);
    }
    span = js_parser_span_from_start(first, parser->previous.span);
    if (ok && js_parser_span_out) *js_parser_span_out = span;
    js_parser_parser_leave(parser);
    return ok;
}

static bool js_parser_parse_type(JsParser* parser, int min_bp, SourceSpan* js_parser_span_out) {
    if (!js_parser_parser_enter(parser)) return false;
    parser->type_depth++;
    bool outer_type = parser->type_depth == 1;
    SourceSpan left;
    if (parser->current.kind == JS_TOK_TYPEOF ||
            parser->current.kind == JS_TOK_KEYOF ||
            parser->current.kind == JS_TOK_READONLY) {
        JsToken first = parser->current;
        js_parser_parser_advance(parser);
        if (!js_parser_parse_type_primary(parser, &left)) {
            parser->type_depth--;
            js_parser_parser_leave(parser);
            return false;
        }
        left = js_parser_span_from_start(first, left);
    } else if (!js_parser_parse_type_primary(parser, &left)) {
        parser->type_depth--;
        js_parser_parser_leave(parser);
        return false;
    }
    while (parser->status == JS_PARSE_OK) {
        JsToken op = parser->current;
        int bp = op.kind == JS_TOK_PIPE || op.kind == JS_TOK_AMP
            ? JS_BP_OR : (op.kind == JS_TOK_ARROW ? JS_BP_ASSIGNMENT : 0);
        if (parser->type_stop_at_arrow && op.kind == JS_TOK_ARROW) bp = 0;
        if (!bp || bp < min_bp) break;
        js_parser_parser_advance(parser);
        if (!js_parser_parse_type(parser, bp + (op.kind == JS_TOK_ARROW ? 0 : 1), NULL)) {
            parser->type_depth--;
            js_parser_parser_leave(parser);
            return false;
        }
        left = js_parser_span_from_start((JsToken){.span = left}, parser->previous.span);
    }
    while (js_parser_parser_accept(parser, JS_TOK_LBRACKET)) {
        if (!js_parser_parser_expect(parser, JS_TOK_RBRACKET,
                "expected ']' after array type")) {
            parser->type_depth--;
            js_parser_parser_leave(parser);
            return false;
        }
        left = js_parser_span_from_start((JsToken){.span = left}, parser->previous.span);
    }
    if (js_parser_span_out) *js_parser_span_out = left;
    if (outer_type && parser->suppress_type_reductions == 0) {
        if (!js_parser_parser_reduce(parser, JS_REDUCE_TYPE, JS_REDUCTION_TYPE, left,
                (JsToken){.span = left}, (JsToken){0}, 0, 0)) {
            parser->type_depth--;
            js_parser_parser_leave(parser);
            return false;
        }
    }
    parser->type_depth--;
    js_parser_parser_leave(parser);
    return true;
}

static bool js_parser_parse_pattern(JsParser* parser, SourceSpan* js_parser_span_out,
        bool* has_type_out) {
    if (!js_parser_parser_enter(parser)) return false;
    if (has_type_out) *has_type_out = false;
    JsToken first = parser->current;
    SourceSpan span = first.span;
    bool ok = true;
    if (parser->assignment_target_pattern &&
            (parser->current.kind == JS_TOK_LBRACE ||
             parser->current.kind == JS_TOK_LBRACKET) &&
            js_parser_parser_has_assignment_member_target(parser)) {
        SourceSpan target_span;
        ok = js_parser_parse_assignment_target_pattern(parser, &target_span);
        if (ok && js_parser_span_out) *js_parser_span_out = target_span;
        js_parser_parser_leave(parser);
        return ok;
    }
    if (parser->current.kind == JS_TOK_LBRACKET) {
        uint32_t element_count = 0;
        uint32_t flags = 0;
        js_parser_parser_advance(parser);
        while (parser->current.kind != JS_TOK_RBRACKET) {
            if (parser->current.kind == JS_TOK_EOF) {
                ok = false;
                break;
            }
            if (js_parser_parser_accept(parser, JS_TOK_COMMA)) {
                if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                        JS_REDUCTION_HOLE, parser->previous.span,
                        parser->previous, (JsToken){0}, 0, 0)) {
                    ok = false;
                    break;
                }
                element_count++;
                continue;
            }
            SourceSpan item_span;
            JsToken item_first = parser->current;
            if (js_parser_parser_accept(parser, JS_TOK_ELLIPSIS)) {
                if (!js_parser_parse_pattern(parser, &item_span, NULL)) {
                    ok = false;
                    break;
                }
                item_span = js_parser_span_from_start(item_first, parser->previous.span);
                if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                        JS_REDUCTION_SPREAD, item_span, item_first,
                        item_first, 0, 1)) {
                    ok = false;
                    break;
                }
                flags |= JS_REDUCTION_FLAG_SPREAD;
            } else {
                if (!js_parser_parse_pattern(parser, &item_span, NULL)) {
                    ok = false;
                    break;
                }
                if (js_parser_parser_accept(parser, JS_TOK_EQUAL)) {
                    JsToken equal = parser->previous;
                    if (!js_parser_parse_pattern_default_expression(parser)) {
                        ok = false;
                        break;
                    }
                    item_span = js_parser_span_from_start((JsToken){.span = item_span},
                        parser->previous.span);
                    if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                            JS_REDUCTION_ASSIGNMENT, item_span, item_first,
                            equal, 0, 2)) {
                        ok = false;
                        break;
                    }
                }
            }
            element_count++;
            if (!js_parser_parser_accept(parser, JS_TOK_COMMA) &&
                    parser->current.kind != JS_TOK_RBRACKET) {
                ok = false;
                break;
            }
        }
        if (ok) ok = js_parser_parser_expect(parser, JS_TOK_RBRACKET,
            "expected ']' after array pattern");
        if (ok) {
            span = js_parser_span_from_start(first, parser->previous.span);
            ok = js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                JS_REDUCTION_ARRAY, span, first, (JsToken){0}, flags,
                element_count);
        }
    } else if (parser->current.kind == JS_TOK_LBRACE) {
        uint32_t property_count = 0;
        js_parser_parser_advance(parser);
        while (parser->current.kind != JS_TOK_RBRACE) {
            if (parser->current.kind == JS_TOK_EOF) { ok = false; break; }
            JsToken property_first = parser->current;
            if (js_parser_parser_accept(parser, JS_TOK_ELLIPSIS)) {
                SourceSpan rest_span;
                if (!js_parser_parse_pattern(parser, &rest_span, NULL)) { ok = false; break; }
                rest_span = js_parser_span_from_start(property_first, parser->previous.span);
                if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                        JS_REDUCTION_SPREAD, rest_span, property_first,
                        property_first, JS_REDUCTION_FLAG_PROPERTY, 1)) {
                    ok = false;
                    break;
                }
                property_count++;
            } else {
                SourceSpan key_span;
                JsToken key_token = {0};
                uint32_t key_flags = 0;
                if (!js_parser_parse_object_key(parser, &key_span, &key_token,
                        &key_flags)) { ok = false; break; }
                if (key_token.kind != JS_TOK_EOF) {
                    if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                            JS_REDUCTION_TOKEN, key_token.span, key_token,
                            (JsToken){0}, 0, 0)) { ok = false; break; }
                }
                uint32_t property_flags = key_flags;
                uint32_t property_children = 1;
                if (js_parser_parser_accept(parser, JS_TOK_COLON)) {
                    SourceSpan value_span;
                    if (!js_parser_parse_pattern(parser, &value_span, NULL)) {
                        ok = false;
                        break;
                    }
                    property_children++;
                    if (js_parser_parser_accept(parser, JS_TOK_EQUAL)) {
                        JsToken equal = parser->previous;
                        if (!js_parser_parse_pattern_default_expression(parser)) {
                            ok = false;
                            break;
                        }
                        value_span = js_parser_span_from_start((JsToken){.span = value_span},
                            parser->previous.span);
                        if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                                JS_REDUCTION_ASSIGNMENT, value_span,
                                (JsToken){.span = value_span}, equal, 0, 2)) {
                            ok = false;
                            break;
                        }
                    }
                } else {
                    if (key_token.kind == JS_TOK_EOF) {
                        ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                            "computed pattern property requires a value",
                            JS_TOK_COLON);
                        break;
                    }
                    if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                            JS_REDUCTION_TOKEN, key_token.span, key_token,
                            (JsToken){0}, JS_REDUCTION_FLAG_BINDING, 0)) {
                        ok = false;
                        break;
                    }
                    property_children++;
                    property_flags |= JS_REDUCTION_FLAG_SHORTHAND;
                    if (js_parser_parser_accept(parser, JS_TOK_EQUAL)) {
                        JsToken equal = parser->previous;
                        if (!js_parser_parse_pattern_default_expression(parser)) {
                            ok = false;
                            break;
                        }
                        SourceSpan value_span = js_parser_span_from_start(key_token,
                            parser->previous.span);
                        if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                                JS_REDUCTION_ASSIGNMENT, value_span,
                                key_token, equal, 0, 2)) {
                            ok = false;
                            break;
                        }
                        property_children = 2;
                    }
                }
                SourceSpan property_span = js_parser_span_from_start(property_first,
                    parser->previous.span);
                if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                        JS_REDUCTION_PROPERTY, property_span, property_first,
                        (JsToken){0}, property_flags, property_children)) {
                    ok = false;
                    break;
                }
                property_count++;
            }
            if (!js_parser_parser_accept(parser, JS_TOK_COMMA) &&
                    parser->current.kind != JS_TOK_RBRACE) { ok = false; break; }
        }
        if (ok) ok = js_parser_parser_expect(parser, JS_TOK_RBRACE,
            "expected '}' after object pattern");
        if (ok) {
            span = js_parser_span_from_start(first, parser->previous.span);
            ok = js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                JS_REDUCTION_OBJECT, span, first, (JsToken){0}, 0,
                property_count);
        }
    } else {
        if (parser->assignment_target_pattern) {
            SourceSpan target_span;
            if (js_parser_parse_assignment_target_pattern(parser, &target_span)) {
                if (js_parser_span_out) *js_parser_span_out = target_span;
                js_parser_parser_leave(parser);
                return true;
            }
        }
        JsToken name_token = {0};
        ok = js_parser_parse_name(parser, &name_token);
        SourceSpan name_span = name_token.span;
        bool has_type = false;
        if (ok) {
            span = name_span;
            ok = js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                JS_REDUCTION_TOKEN, name_span, name_token, (JsToken){0},
                JS_REDUCTION_FLAG_BINDING, 0);
        }
        if (ok && js_parser_parser_accept(parser, JS_TOK_COLON)) {
            if (!(parser->mode & JS_PARSE_TYPESCRIPT)) {
                ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT,
                    error_unexpected_token, JS_TOK_IDENTIFIER);
            } else {
                ok = js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL);
                has_type = ok;
            }
        }
        if (ok && has_type_out) *has_type_out = has_type;
    }
    if (ok) {
        span = js_parser_span_from_start(first, parser->previous.span);
        if (js_parser_span_out) *js_parser_span_out = span;
    }
    js_parser_parser_leave(parser);
    return ok;
}

static bool js_parser_parse_variable_declaration(JsParser* parser, bool in_for) {
    JsToken first = parser->current;
    JsTokenKind declaration_kind = parser->current.kind;
    uint32_t declarator_count = 0;
    js_parser_parser_advance(parser);
    for (;;) {
        SourceSpan pattern;
        bool pattern_has_type = false;
        if (!js_parser_parse_pattern(parser, &pattern, &pattern_has_type)) return false;
        SourceSpan declarator = pattern;
        if (js_parser_parser_accept(parser, JS_TOK_COLON)) {
            if (!(parser->mode & JS_PARSE_TYPESCRIPT) ||
                    !js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) return false;
            pattern_has_type = true;
        }
        bool has_initializer = js_parser_parser_accept(parser, JS_TOK_EQUAL);
        if (has_initializer) {
            if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) return false;
            declarator = js_parser_span_from_start((JsToken){.span = pattern},
                parser->previous.span);
        }
        if (!js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
                JS_REDUCTION_DECLARATOR, declarator, first, (JsToken){0},
                has_initializer ? 1u : 0u,
                1u + (pattern_has_type ? 1u : 0u) +
                    (has_initializer ? 1u : 0u))) {
            return false;
        }
        declarator_count++;
        if (!js_parser_parser_accept(parser, JS_TOK_COMMA)) break;
    }
    if (!in_for && !js_parser_parse_semicolon(parser)) return false;
    SourceSpan declaration_span = js_parser_span_from_start(first, parser->previous.span);
    if (in_for && parser->current.kind == JS_TOK_SEMICOLON) {
        // Tree-sitter includes the first for-clause separator in its variable
        // declaration span, even though it is consumed by the loop parser.
        declaration_span = js_parser_span_from_start(first, parser->current.span);
    }
    return js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
        JS_REDUCTION_VARIABLE_DECLARATION,
        declaration_span, first, (JsToken){0},
        (uint32_t)declaration_kind, declarator_count);
}

static bool js_parser_parse_if(JsParser* parser, uint32_t* child_count_out,
        uint32_t* child_flags_out) {
    uint32_t child_count = 1;
    uint32_t child_flags = 0;
    js_parser_parser_advance(parser);
    if (!js_parser_parser_expect(parser, JS_TOK_LPAREN, "expected '(' after if") ||
            !js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) ||
            !js_parser_parser_expect(parser, JS_TOK_RPAREN, "expected ')' after if condition") ||
            !js_parser_parse_statement(parser)) {
        return false;
    }
    bool has_consequent = parser->last_statement_reduced;
    if (has_consequent) child_count++;
    if (js_parser_parser_accept(parser, JS_TOK_ELSE)) {
        if (!has_consequent) {
            child_flags |= JS_REDUCTION_CHILD_EMPTY_CONSEQUENT;
        }
        if (!js_parser_parse_statement(parser)) return false;
        if (parser->last_statement_reduced) child_count++;
    }
    if (child_count_out) *child_count_out = child_count;
    if (child_flags_out) *child_flags_out = child_flags;
    return true;
}

static bool js_parser_parse_for(JsParser* parser) {
    JsToken first = parser->current;
    js_parser_parser_advance(parser);
    bool is_await = js_parser_parser_accept(parser, JS_TOK_AWAIT);
    if (!js_parser_parser_expect(parser, JS_TOK_LPAREN, "expected '(' after for")) return false;
    parser->loop_depth++;
    bool ok = true;
    bool has_init = false;
    bool has_test = false;
    bool has_update = false;
    bool init_is_declaration = false;
    JsReductionForm loop_form = JS_REDUCTION_FOR;
    uint32_t loop_flags = is_await ? JS_REDUCTION_FLAG_FOR_AWAIT : 0;
    uint32_t child_count = 0;
    if (parser->current.kind == JS_TOK_SEMICOLON) {
        js_parser_parser_advance(parser);
    } else {
        bool saved_stop = parser->stop_for_in_of;
        parser->stop_for_in_of = true;
        if (parser->current.kind == JS_TOK_VAR ||
                parser->current.kind == JS_TOK_CONST ||
                js_parser_parser_starts_let_declaration(parser, true)) {
            init_is_declaration = true;
            ok = js_parser_parse_variable_declaration(parser, true);
        }
        else if (js_parser_parser_has_for_head_pattern(parser)) {
            bool saved_assignment_target_pattern =
                parser->assignment_target_pattern;
            parser->assignment_target_pattern = true;
            ok = js_parser_parse_pattern(parser, NULL, NULL);
            parser->assignment_target_pattern =
                saved_assignment_target_pattern;
        } else ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL);
        parser->stop_for_in_of = saved_stop;
        if (!ok) goto done;
        has_init = true;
        if (parser->current.kind == JS_TOK_IN || parser->current.kind == JS_TOK_OF) {
            loop_form = parser->current.kind == JS_TOK_IN
                ? JS_REDUCTION_FOR_IN : JS_REDUCTION_FOR_OF;
            js_parser_parser_advance(parser);
            if (!js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) ||
                    !js_parser_parser_expect(parser, JS_TOK_RPAREN, "expected ')' after for head") ||
                    !js_parser_parse_statement(parser)) { ok = false; goto done; }
            loop_flags |= init_is_declaration ? JS_REDUCTION_FLAG_FOR_DECLARATION : 0;
            child_count = 2 + (parser->last_statement_reduced ? 1u : 0u);
            goto done;
        }
        if (!js_parser_parser_expect(parser, JS_TOK_SEMICOLON, "expected ';' in for statement")) {
            ok = false;
            goto done;
        }
    }
    if (parser->current.kind != JS_TOK_SEMICOLON) {
        if (!js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL)) { ok = false; goto done; }
        has_test = true;
    }
    if (!js_parser_parser_expect(parser, JS_TOK_SEMICOLON, "expected ';' in for statement")) { ok = false; goto done; }
    if (parser->current.kind != JS_TOK_RPAREN) {
        if (!js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL)) { ok = false; goto done; }
        has_update = true;
    }
    if (!js_parser_parser_expect(parser, JS_TOK_RPAREN, "expected ')' after for clauses") ||
            !js_parser_parse_statement(parser)) ok = false;
    if (has_init) loop_flags |= JS_REDUCTION_FLAG_FOR_INIT;
    if (has_test) loop_flags |= JS_REDUCTION_FLAG_FOR_TEST;
    if (has_update) loop_flags |= JS_REDUCTION_FLAG_FOR_UPDATE;
    child_count = (has_init ? 1u : 0u) + (has_test ? 1u : 0u) +
        (has_update ? 1u : 0u) + (parser->last_statement_reduced ? 1u : 0u);
done:
    parser->loop_depth--;
    if (!ok) return false;
    return js_parser_parser_reduce(parser, JS_REDUCE_STATEMENT, loop_form,
        js_parser_span_from_start(first, parser->previous.span), first, (JsToken){0},
        loop_flags, child_count);
}

static bool js_parser_parse_switch(JsParser* parser) {
    JsToken first = parser->current;
    js_parser_parser_advance(parser);
    if (!js_parser_parser_expect(parser, JS_TOK_LPAREN, "expected '(' after switch") ||
            !js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) ||
            !js_parser_parser_expect(parser, JS_TOK_RPAREN, "expected ')' after switch expression") ||
            !js_parser_parser_expect(parser, JS_TOK_LBRACE, "expected '{' after switch")) return false;
    parser->switch_depth++;
    uint32_t case_count = 0;
    while (parser->current.kind != JS_TOK_RBRACE) {
        if (parser->current.kind == JS_TOK_EOF) {
            parser->switch_depth--;
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_EOF,
                error_unterminated, JS_TOK_RBRACE);
        }
        bool is_default = false;
        JsToken case_token = parser->current;
        if (js_parser_parser_accept(parser, JS_TOK_CASE)) {
            if (!js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) ||
                    !js_parser_parser_expect(parser, JS_TOK_COLON,
                        "expected ':' after case")) {
                parser->switch_depth--;
                return false;
            }
        } else if (js_parser_parser_accept(parser, JS_TOK_DEFAULT)) {
            is_default = true;
            if (!js_parser_parser_expect(parser, JS_TOK_COLON,
                    "expected ':' after default")) {
                parser->switch_depth--;
                return false;
            }
        } else {
            parser->switch_depth--;
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                "expected 'case' or 'default' in switch", JS_TOK_CASE);
        }
        uint32_t statement_count = 0;
        while (parser->current.kind != JS_TOK_CASE &&
                parser->current.kind != JS_TOK_DEFAULT &&
                parser->current.kind != JS_TOK_RBRACE) {
            if (!js_parser_parse_statement(parser)) {
                parser->switch_depth--;
                return false;
            }
            if (parser->last_statement_reduced) statement_count++;
        }
        uint32_t child_count = statement_count + (is_default ? 0u : 1u);
        uint32_t flags = is_default ? JS_REDUCTION_FLAG_DEFAULT : 0u;
        if (!js_parser_parser_reduce(parser, JS_REDUCE_STATEMENT, JS_REDUCTION_CASE,
                js_parser_span_from_start(case_token, parser->previous.span), case_token,
                (JsToken){0}, flags, child_count)) {
            parser->switch_depth--;
            return false;
        }
        case_count++;
    }
    parser->switch_depth--;
    js_parser_parser_advance(parser);
    return js_parser_parser_reduce(parser, JS_REDUCE_STATEMENT, JS_REDUCTION_SWITCH,
        js_parser_span_from_start(first, parser->previous.span), first, (JsToken){0}, 0,
        case_count + 1);
}

static bool js_parser_parse_try(JsParser* parser) {
    JsToken first = parser->current;
    JsToken finalizer_token = {0};
    js_parser_parser_advance(parser);
    if (!js_parser_parse_block(parser)) return false;
    uint32_t child_count = 1;
    uint32_t try_flags = 0;
    bool has_handler = false;
    if (js_parser_parser_accept(parser, JS_TOK_CATCH)) {
        has_handler = true;
        try_flags |= JS_REDUCTION_FLAG_TRY_HANDLER;
        JsToken catch_token = parser->previous;
        uint32_t catch_children = 0;
        if (js_parser_parser_accept(parser, JS_TOK_LPAREN)) {
            if (!js_parser_parse_pattern(parser, NULL, NULL) ||
                    !js_parser_parser_expect(parser, JS_TOK_RPAREN, "expected ')' after catch binding")) return false;
            catch_children = 1;
        }
        if (!js_parser_parse_block(parser)) return false;
        catch_children++;
        if (!js_parser_parser_reduce(parser, JS_REDUCE_STATEMENT, JS_REDUCTION_CATCH,
                js_parser_span_from_start(catch_token, parser->previous.span), catch_token,
                (JsToken){0}, catch_children == 2 ? JS_REDUCTION_FLAG_CATCH_PARAM : 0,
                catch_children)) return false;
        child_count++;
    }
    if (js_parser_parser_accept(parser, JS_TOK_FINALLY)) {
        // retain the finally-clause start so the direct AST can preserve the
        // reference builder's wrapper block around its body block.
        finalizer_token = parser->previous;
        has_handler = true;
        try_flags |= JS_REDUCTION_FLAG_TRY_FINALIZER;
        if (!js_parser_parse_block(parser)) return false;
        child_count++;
    }
    if (!has_handler) {
        return js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT,
            "expected 'catch' or 'finally' after try block", JS_TOK_CATCH);
    }
    return js_parser_parser_reduce(parser, JS_REDUCE_STATEMENT, JS_REDUCTION_TRY,
        js_parser_span_from_start(first, parser->previous.span), first, finalizer_token, try_flags,
        child_count);
}

static bool js_parser_parse_import_declaration(JsParser* parser) {
    JsToken first = parser->current;
    uint32_t flags = 0;
    uint32_t child_count = 0;
    js_parser_parser_advance(parser);
    if (parser->current.kind == JS_TOK_STRING) {
        JsToken source = parser->current;
        js_parser_parser_advance(parser);
        if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION, JS_REDUCTION_TOKEN,
                source.span, source, (JsToken){0}, 0, 0)) return false;
        child_count = 1;
    } else {
        if ((parser->mode & JS_PARSE_TYPESCRIPT) && parser->current.kind == JS_TOK_TYPE) {
            flags |= JS_REDUCTION_FLAG_IMPORT_TYPE;
            js_parser_parser_advance(parser);
        }
        if (parser->current.kind == JS_TOK_IDENTIFIER || js_parser_token_is_name(parser->current.kind)) {
            JsToken local;
            if (!js_parser_parse_name(parser, &local) ||
                    !js_parser_parser_reduce(parser, JS_REDUCE_PATTERN, JS_REDUCTION_TOKEN,
                        local.span, local, (JsToken){0},
                        JS_REDUCTION_FLAG_BINDING, 0)) return false;
            flags |= JS_REDUCTION_FLAG_IMPORT_DEFAULT;
            child_count++;
            js_parser_parser_accept(parser, JS_TOK_COMMA);
        }
        if (js_parser_parser_accept(parser, JS_TOK_STAR)) {
            if (!js_parser_parser_expect(parser, JS_TOK_AS, "expected 'as' in namespace import") ||
                    !js_parser_parse_name(parser, NULL)) return false;
            JsToken local = parser->previous;
            if (!js_parser_parser_reduce(parser, JS_REDUCE_PATTERN, JS_REDUCTION_TOKEN,
                    local.span, local, (JsToken){0},
                    JS_REDUCTION_FLAG_BINDING, 0)) return false;
            flags |= JS_REDUCTION_FLAG_IMPORT_NAMESPACE;
            child_count++;
        } else if (js_parser_parser_accept(parser, JS_TOK_LBRACE)) {
            while (parser->current.kind != JS_TOK_RBRACE) {
                if ((parser->mode & JS_PARSE_TYPESCRIPT) && parser->current.kind == JS_TOK_TYPE) {
                    js_parser_parser_advance(parser);
                }
                JsToken remote;
                if (!js_parser_parse_property_name(parser, &remote) ||
                        !js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                            JS_REDUCTION_TOKEN, remote.span, remote,
                            (JsToken){0}, JS_REDUCTION_FLAG_PROPERTY, 0)) return false;
                uint32_t specifier_children = 1;
                if (js_parser_parser_accept(parser, JS_TOK_AS)) {
                    JsToken local;
                    if (!js_parser_parse_name(parser, &local) ||
                            !js_parser_parser_reduce(parser, JS_REDUCE_PATTERN,
                                JS_REDUCTION_TOKEN, local.span, local,
                                (JsToken){0}, JS_REDUCTION_FLAG_BINDING, 0)) return false;
                    specifier_children = 2;
                }
                if (!js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
                        JS_REDUCTION_IMPORT_SPECIFIER,
                        js_parser_span_from_start(remote, parser->previous.span), remote,
                        (JsToken){0}, 0, specifier_children)) return false;
                child_count++;
                if (!js_parser_parser_accept(parser, JS_TOK_COMMA) && parser->current.kind != JS_TOK_RBRACE)
                    return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                        "expected ',' or '}' in import list", JS_TOK_COMMA);
            }
            if (!js_parser_parser_expect(parser, JS_TOK_RBRACE, "expected '}' in import list")) return false;
        }
        if (!js_parser_parser_expect(parser, JS_TOK_FROM, "expected 'from' in import declaration") ||
                parser->current.kind != JS_TOK_STRING) {
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                "expected module string in import declaration", JS_TOK_STRING);
        }
        JsToken source = parser->current;
        js_parser_parser_advance(parser);
        if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION, JS_REDUCTION_TOKEN,
                source.span, source, (JsToken){0}, 0, 0)) return false;
        child_count++;
    }
    if (js_parser_parser_accept(parser, JS_TOK_WITH)) {
        if (!js_parser_parser_expect(parser, JS_TOK_LBRACE,
                "expected '{' after import attributes")) return false;
        while (parser->current.kind != JS_TOK_RBRACE) {
            if (parser->current.kind == JS_TOK_EOF) {
                return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_EOF,
                    error_unterminated, JS_TOK_RBRACE);
            }
            js_parser_parser_advance(parser);
        }
        js_parser_parser_advance(parser);
    } else if (js_parser_parser_accept(parser, JS_TOK_LBRACE)) {
        // import assertions / attributes are grammar-owned but semantically
        // opaque at this stage.
        while (parser->current.kind != JS_TOK_RBRACE) {
            js_parser_parser_advance(parser);
            if (parser->current.kind == JS_TOK_EOF) return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_EOF, error_unterminated, JS_TOK_RBRACE);
        }
        js_parser_parser_advance(parser);
    }
    if (!js_parser_parse_semicolon(parser)) return false;
    return js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION, JS_REDUCTION_IMPORT,
        js_parser_span_from_start(first, parser->previous.span), first, (JsToken){0},
        flags, child_count);
}

static bool js_parser_parse_export_declaration(JsParser* parser) {
    JsToken first = parser->current;
    uint32_t flags = 0;
    uint32_t child_count = 0;
    js_parser_parser_advance(parser);
    if (parser->current.kind == JS_TOK_DEFAULT) {
        flags |= JS_REDUCTION_FLAG_DEFAULT;
        js_parser_parser_advance(parser);
        if (parser->current.kind == JS_TOK_ASYNC &&
                parser->next.kind == JS_TOK_FUNCTION &&
                !parser->next.line_terminator_before) {
            if (!js_parser_parse_function(parser, true, true, true, NULL)) return false;
        } else if (parser->current.kind == JS_TOK_FUNCTION) {
            if (!js_parser_parse_function(parser, true, false, true, NULL)) return false;
        } else if (parser->current.kind == JS_TOK_CLASS) {
            // An anonymous default class is an expression in the reference
            // AST; a named default class remains a declaration.
            bool named_default_class = parser->next.kind != JS_TOK_LBRACE &&
                parser->next.kind != JS_TOK_EXTENDS &&
                parser->next.kind != JS_TOK_LT;
            if (!js_parser_parse_class(parser, named_default_class, NULL)) return false;
        } else if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL) || !js_parser_parse_semicolon(parser)) {
            return false;
        }
        child_count = 1;
    } else if (js_parser_parser_accept(parser, JS_TOK_STAR)) {
        flags |= JS_REDUCTION_FLAG_EXPORT_STAR;
        JsToken namespace_start = parser->previous;
        if (js_parser_parser_accept(parser, JS_TOK_AS)) {
            JsToken local;
            if (!js_parser_parse_name(parser, &local) ||
                    !js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_TOKEN, local.span, local,
                        (JsToken){0}, JS_REDUCTION_FLAG_PROPERTY, 0)) return false;
            if (!js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
                    JS_REDUCTION_EXPORT_SPECIFIER,
                    js_parser_span_from_start(namespace_start, local.span), local,
                    (JsToken){0}, 0, 1)) return false;
            flags |= JS_REDUCTION_FLAG_EXPORT_NAMESPACE;
            child_count++;
        }
        if (!js_parser_parser_expect(parser, JS_TOK_FROM, "expected 'from' in export declaration") ||
                parser->current.kind != JS_TOK_STRING) {
            return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                "expected module string in export declaration", JS_TOK_STRING);
        }
        JsToken source = parser->current;
        js_parser_parser_advance(parser);
        if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION, JS_REDUCTION_TOKEN,
                source.span, source, (JsToken){0}, 0, 0)) return false;
        flags |= JS_REDUCTION_FLAG_EXPORT_SOURCE;
        child_count++;
        if (!js_parser_parse_semicolon(parser)) return false;
    } else if (js_parser_parser_accept(parser, JS_TOK_LBRACE)) {
        while (parser->current.kind != JS_TOK_RBRACE) {
            JsToken local;
            if (!js_parser_parse_name(parser, &local) ||
                    !js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                        JS_REDUCTION_TOKEN, local.span, local,
                        (JsToken){0}, JS_REDUCTION_FLAG_PROPERTY, 0)) return false;
            uint32_t specifier_children = 1;
            if (js_parser_parser_accept(parser, JS_TOK_AS)) {
                JsToken exported;
                if (!js_parser_parse_property_name(parser, &exported) ||
                        !js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                            JS_REDUCTION_TOKEN, exported.span, exported,
                            (JsToken){0}, JS_REDUCTION_FLAG_PROPERTY, 0)) return false;
                specifier_children = 2;
            }
            if (!js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
                    JS_REDUCTION_EXPORT_SPECIFIER,
                    js_parser_span_from_start(local, parser->previous.span), local,
                    (JsToken){0}, 0, specifier_children)) return false;
            child_count++;
            if (!js_parser_parser_accept(parser, JS_TOK_COMMA) && parser->current.kind != JS_TOK_RBRACE)
                return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                    "expected ',' or '}' in export list", JS_TOK_COMMA);
        }
        if (!js_parser_parser_expect(parser, JS_TOK_RBRACE, "expected '}' in export list")) return false;
        if (js_parser_parser_accept(parser, JS_TOK_FROM)) {
            if (parser->current.kind != JS_TOK_STRING) {
                return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                    "expected module string in export declaration", JS_TOK_STRING);
            }
            JsToken source = parser->current;
            js_parser_parser_advance(parser);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_TOKEN, source.span, source,
                    (JsToken){0}, 0, 0)) return false;
            flags |= JS_REDUCTION_FLAG_EXPORT_SOURCE;
            child_count++;
        }
        if (!js_parser_parse_semicolon(parser)) return false;
    } else if ((parser->mode & JS_PARSE_TYPESCRIPT) &&
            (parser->current.kind == JS_TOK_TYPE ||
             parser->current.kind == JS_TOK_INTERFACE ||
             parser->current.kind == JS_TOK_ENUM ||
             parser->current.kind == JS_TOK_NAMESPACE)) {
        if (!js_parser_parse_ts_declaration(parser)) return false;
    } else if (parser->current.kind == JS_TOK_VAR || parser->current.kind == JS_TOK_LET ||
            parser->current.kind == JS_TOK_CONST) {
        if (!js_parser_parse_variable_declaration(parser, false)) return false;
    } else if (parser->current.kind == JS_TOK_FUNCTION) {
        if (!js_parser_parse_function(parser, true, false, false, NULL)) return false;
    } else if (parser->current.kind == JS_TOK_CLASS) {
        if (!js_parser_parse_class(parser, true, NULL)) return false;
    } else {
        return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
            "expected declaration after export", JS_TOK_IDENTIFIER);
    }
    if (!(flags & JS_REDUCTION_FLAG_EXPORT_SOURCE) &&
            !(flags & JS_REDUCTION_FLAG_EXPORT_STAR) &&
            child_count == 0) {
        child_count = 1;
    }
    return js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION, JS_REDUCTION_EXPORT,
        js_parser_span_from_start(first, parser->previous.span), first, (JsToken){0},
        flags, child_count);
}

static bool js_parser_parse_ts_declaration(JsParser* parser) {
    JsToken first = parser->current;
    if (js_parser_parser_accept(parser, JS_TOK_DECLARE)) {
        if (!js_parser_parse_statement(parser)) return false;
        return true;
    }
    if (js_parser_parser_accept(parser, JS_TOK_INTERFACE)) {
        uint32_t type_parameter_count = 0;
        if (!js_parser_parse_name(parser, NULL) ||
                !js_parser_parse_type_parameters(parser, &type_parameter_count)) return false;
        uint32_t extends_count = 0;
        if (js_parser_parser_accept(parser, JS_TOK_EXTENDS)) {
            do {
                if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) return false;
                extends_count++;
            } while (js_parser_parser_accept(parser, JS_TOK_COMMA));
        }
        if (!js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL)) return false;
        return js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
            JS_REDUCTION_TYPE, js_parser_span_from_start(first, parser->previous.span),
            first, (JsToken){0}, 0, extends_count + 1u +
                (type_parameter_count ? 1u : 0u));
    } else if (js_parser_parser_accept(parser, JS_TOK_TYPE)) {
        uint32_t type_parameter_count = 0;
        if (!js_parser_parse_name(parser, NULL) ||
                !js_parser_parse_type_parameters(parser, &type_parameter_count) ||
                !js_parser_parser_expect(parser, JS_TOK_EQUAL, "expected '=' in type alias") ||
                !js_parser_parse_type(parser, JS_BP_SEQUENCE, NULL) || !js_parser_parse_semicolon(parser)) return false;
        return js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
            JS_REDUCTION_TYPE, js_parser_span_from_start(first, parser->previous.span),
            first, (JsToken){0}, 0, 1u +
                (type_parameter_count ? 1u : 0u));
    } else if (js_parser_parser_accept(parser, JS_TOK_ENUM)) {
        if (!js_parser_parse_name(parser, NULL) || !js_parser_parser_expect(parser, JS_TOK_LBRACE,
                "expected '{' before enum body")) return false;
        uint32_t member_count = 0;
        while (parser->current.kind != JS_TOK_RBRACE) {
            JsToken member_name;
            if (!js_parser_parse_name(parser, &member_name)) return false;
            if (!js_parser_parser_reduce(parser, JS_REDUCE_EXPRESSION,
                    JS_REDUCTION_TOKEN, member_name.span, member_name,
                    (JsToken){0}, JS_REDUCTION_FLAG_PROPERTY, 0)) return false;
            bool has_initializer = js_parser_parser_accept(parser, JS_TOK_EQUAL);
            if (has_initializer &&
                    !js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) return false;
            SourceSpan member_span = js_parser_span_from_start(member_name,
                parser->previous.span);
            if (!js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
                    JS_REDUCTION_ENUM_MEMBER, member_span, member_name,
                    (JsToken){0}, 0, has_initializer ? 2 : 1)) return false;
            member_count++;
            if (!js_parser_parser_accept(parser, JS_TOK_COMMA)) break;
        }
        if (!js_parser_parser_expect(parser, JS_TOK_RBRACE, "expected '}' after enum body")) return false;
        return js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
            JS_REDUCTION_TYPE, js_parser_span_from_start(first, parser->previous.span),
            first, (JsToken){0}, 0, member_count);
    } else if (js_parser_parser_accept(parser, JS_TOK_NAMESPACE) || js_parser_parser_accept(parser, JS_TOK_MODULE)) {
        if (!js_parser_parse_name(parser, NULL)) return false;
        bool saved_in_ts_namespace = parser->in_ts_namespace;
        parser->in_ts_namespace = true;
        bool block_ok = js_parser_parse_block(parser);
        parser->in_ts_namespace = saved_in_ts_namespace;
        if (!block_ok) return false;
        return js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
            JS_REDUCTION_TYPE, js_parser_span_from_start(first, parser->previous.span),
            first, (JsToken){0}, 0, 1);
    } else {
        return js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
            "expected TypeScript declaration", JS_TOK_IDENTIFIER);
    }
}

static bool js_parser_parse_statement(JsParser* parser) {
    if (!js_parser_parser_enter(parser)) return false;
    js_parser_parser_relex_current_as_regex(parser);
    parser->last_statement_reduced = false;
    JsToken first = parser->current;
    bool ok = true;
    bool emit_statement = true;
    JsReductionForm statement_form = JS_REDUCTION_STATEMENT_WRAPPER;
    uint32_t statement_child_count = 1;
    uint32_t statement_child_flags = 0;
    JsToken statement_secondary = {0};
    if (parser->current.kind == JS_TOK_AT) {
        uint32_t decorator_count = 0;
        JsToken decorator_first = parser->current;
        do {
            JsToken decorator_at = parser->current;
            js_parser_parser_advance(parser);
            if (!js_parser_parse_expression(parser, JS_BP_ASSIGNMENT, NULL)) {
                ok = false;
                break;
            }
            if (ok && !js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
                    JS_REDUCTION_DECORATOR,
                    js_parser_span_from_start(decorator_at, parser->previous.span),
                    decorator_at, (JsToken){0}, 0, 1)) {
                ok = false;
                break;
            }
            decorator_count++;
        } while (parser->current.kind == JS_TOK_AT);
        if (ok && parser->current.kind == JS_TOK_CLASS) {
            ok = js_parser_parse_class(parser, true, NULL);
        } else if (ok && parser->current.kind == JS_TOK_FUNCTION) {
            ok = js_parser_parse_function(parser, true, false, false, NULL);
        } else if (ok) {
            ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT,
                "decorators must precede a class or function declaration", JS_TOK_CLASS);
        }
        if (ok && decorator_count) {
            ok = js_parser_parser_reduce(parser, JS_REDUCE_DECLARATION,
                JS_REDUCTION_DECORATED_DECLARATION,
                js_parser_span_from_start(decorator_first, parser->previous.span),
                decorator_first, (JsToken){0}, 0, decorator_count + 1u);
        }
    } else switch (parser->current.kind) {
    case JS_TOK_SEMICOLON:
        js_parser_parser_advance(parser);
        emit_statement = false;
        break;
    case JS_TOK_LBRACE:
        if (js_parser_parser_starts_object_statement(parser)) {
            statement_form = JS_REDUCTION_EXPRESSION_STATEMENT;
            ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) &&
                js_parser_parse_semicolon(parser);
        } else {
            ok = js_parser_parse_block(parser);
        }
        break;
    case JS_TOK_FUNCTION:
        ok = js_parser_parse_function(parser, true, false, false, NULL);
        break;
    case JS_TOK_CLASS:
        ok = js_parser_parse_class(parser, true, NULL);
        break;
    case JS_TOK_VAR: case JS_TOK_CONST:
        ok = js_parser_parse_variable_declaration(parser, false);
        break;
    case JS_TOK_LET:
        if (js_parser_parser_starts_let_declaration(parser, false)) {
            ok = js_parser_parse_variable_declaration(parser, false);
        } else {
            statement_form = JS_REDUCTION_EXPRESSION_STATEMENT;
            ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) &&
                js_parser_parse_semicolon(parser);
        }
        break;
    case JS_TOK_IF:
        statement_form = JS_REDUCTION_IF;
        ok = js_parser_parse_if(parser, &statement_child_count, &statement_child_flags);
        break;
    case JS_TOK_FOR: ok = js_parser_parse_for(parser); break;
    case JS_TOK_SWITCH: ok = js_parser_parse_switch(parser); break;
    case JS_TOK_TRY: ok = js_parser_parse_try(parser); break;
    case JS_TOK_WHILE:
        statement_form = JS_REDUCTION_WHILE;
        statement_child_count = 1;
        js_parser_parser_advance(parser);
        if (!js_parser_parser_expect(parser, JS_TOK_LPAREN, "expected '(' after while") ||
                !js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) ||
                !js_parser_parser_expect(parser, JS_TOK_RPAREN, "expected ')' after while condition")) ok = false;
        parser->loop_depth++;
        if (ok) ok = js_parser_parse_statement(parser);
        parser->loop_depth--;
        if (ok && parser->last_statement_reduced) statement_child_count++;
        break;
    case JS_TOK_DO:
        statement_form = JS_REDUCTION_DO_WHILE;
        statement_child_count = 1;
        js_parser_parser_advance(parser);
        parser->loop_depth++;
        ok = js_parser_parse_statement(parser);
        bool has_body = parser->last_statement_reduced;
        parser->loop_depth--;
        if (ok && !js_parser_parser_expect(parser, JS_TOK_WHILE, "expected 'while' after do body")) ok = false;
        if (ok && (!js_parser_parser_expect(parser, JS_TOK_LPAREN, "expected '(' after while") ||
                !js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) ||
                !js_parser_parser_expect(parser, JS_TOK_RPAREN, "expected ')' after do condition") ||
                !js_parser_parse_do_while_terminator(parser))) ok = false;
        if (has_body) statement_child_count++;
        else statement_child_flags |= JS_REDUCTION_CHILD_MISSING_BODY;
        break;
    case JS_TOK_RETURN:
        statement_form = JS_REDUCTION_RETURN;
        statement_child_count = 0;
        js_parser_parser_advance(parser);
        if (parser->function_depth == 0) {
            ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT,
                "return is only valid inside a function", JS_TOK_SEMICOLON);
        } else if (!parser->current.line_terminator_before &&
                parser->current.kind != JS_TOK_SEMICOLON &&
                parser->current.kind != JS_TOK_RBRACE &&
                parser->current.kind != JS_TOK_EOF) {
            ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL);
            if (ok) statement_child_count = 1;
        }
        if (ok) ok = js_parser_parse_semicolon(parser);
        break;
    case JS_TOK_THROW:
        statement_form = JS_REDUCTION_THROW;
        statement_child_count = 1;
        js_parser_parser_advance(parser);
        if (parser->current.line_terminator_before) {
            ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_LINE_TERMINATOR,
                error_line_terminator, JS_TOK_IDENTIFIER);
        } else {
            ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) && js_parser_parse_semicolon(parser);
        }
        break;
    case JS_TOK_BREAK: case JS_TOK_CONTINUE:
        statement_form = first.kind == JS_TOK_CONTINUE
            ? JS_REDUCTION_CONTINUE : JS_REDUCTION_BREAK;
        statement_child_count = 0;
        js_parser_parser_advance(parser);
        if (parser->current.kind != JS_TOK_SEMICOLON &&
                parser->current.kind != JS_TOK_RBRACE &&
                parser->current.kind != JS_TOK_EOF &&
                !parser->current.line_terminator_before) {
            ok = js_parser_parse_name(parser, &statement_secondary);
        }
        if (ok && statement_secondary.span.end_byte >
                statement_secondary.span.start_byte) {
            bool iteration = false;
            if (!js_parser_parser_label_find(parser, statement_secondary, &iteration) ||
                    (first.kind == JS_TOK_CONTINUE && !iteration)) {
                ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT,
                    first.kind == JS_TOK_CONTINUE
                        ? "continue label is not an iteration label"
                        : "break label is not defined", JS_TOK_SEMICOLON);
            }
        } else if (ok && first.kind == JS_TOK_BREAK &&
                parser->loop_depth == 0 && parser->switch_depth == 0) {
            ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT,
                "break is not inside a loop or switch", JS_TOK_SEMICOLON);
        } else if (ok && first.kind == JS_TOK_CONTINUE &&
                parser->loop_depth == 0) {
            ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT,
                "continue is not inside a loop", JS_TOK_SEMICOLON);
        }
        if (ok) ok = js_parser_parse_semicolon(parser);
        break;
    case JS_TOK_DEBUGGER:
        js_parser_parser_advance(parser);
        ok = js_parser_parse_semicolon(parser);
        emit_statement = false;
        break;
    case JS_TOK_WITH:
        statement_form = JS_REDUCTION_WITH;
        statement_child_count = 1;
        js_parser_parser_advance(parser);
        ok = js_parser_parser_expect(parser, JS_TOK_LPAREN, "expected '(' after with") &&
            js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) &&
            js_parser_parser_expect(parser, JS_TOK_RPAREN, "expected ')' after with") &&
            js_parser_parse_statement(parser);
        if (ok && parser->last_statement_reduced) statement_child_count++;
        break;
    case JS_TOK_IMPORT:
        if (parser->next.kind == JS_TOK_LPAREN || parser->next.kind == JS_TOK_DOT) {
            statement_form = JS_REDUCTION_EXPRESSION_STATEMENT;
            ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) && js_parser_parse_semicolon(parser);
        } else if (parser->mode & JS_PARSE_MODULE) {
            ok = js_parser_parse_import_declaration(parser);
        } else {
            ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT,
                "import declarations require module mode", JS_TOK_STRING);
        }
        break;
    case JS_TOK_EXPORT:
        if ((parser->mode & JS_PARSE_MODULE) || parser->in_ts_namespace) {
            ok = js_parser_parse_export_declaration(parser);
        }
        else ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT,
            "export declarations require module mode", JS_TOK_IDENTIFIER);
        break;
    case JS_TOK_ASYNC:
        if (parser->next.kind == JS_TOK_FUNCTION &&
                !parser->next.line_terminator_before) ok = js_parser_parse_function(parser, true, true, false, NULL);
        else {
            statement_form = JS_REDUCTION_EXPRESSION_STATEMENT;
            ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) &&
                js_parser_parse_semicolon(parser);
        }
        break;
    case JS_TOK_INTERFACE: case JS_TOK_ENUM: case JS_TOK_NAMESPACE:
    case JS_TOK_MODULE: case JS_TOK_TYPE: case JS_TOK_DECLARE:
        if (parser->mode & JS_PARSE_TYPESCRIPT) ok = js_parser_parse_ts_declaration(parser);
        else {
            statement_form = JS_REDUCTION_EXPRESSION_STATEMENT;
            ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) &&
                js_parser_parse_semicolon(parser);
        }
        break;
    case JS_TOK_ABSTRACT: case JS_TOK_PUBLIC: case JS_TOK_PRIVATE:
    case JS_TOK_PROTECTED: case JS_TOK_READONLY:
        if (parser->mode & JS_PARSE_TYPESCRIPT) {
            js_parser_parser_advance(parser);
            ok = js_parser_parse_statement(parser);
        } else {
            statement_form = JS_REDUCTION_EXPRESSION_STATEMENT;
            ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) &&
                js_parser_parse_semicolon(parser);
        }
        break;
    default:
        if (js_parser_token_is_name(parser->current.kind) && parser->next.kind == JS_TOK_COLON) {
            JsToken label = parser->current;
            statement_form = JS_REDUCTION_LABELED;
            statement_secondary = label;
            ok = js_parser_parse_name(parser, &label) &&
                js_parser_parser_expect(parser, JS_TOK_COLON, "expected ':' after label");
            bool iteration = parser->current.kind == JS_TOK_FOR ||
                parser->current.kind == JS_TOK_WHILE ||
                parser->current.kind == JS_TOK_DO;
            if (ok && js_parser_parser_label_find(parser, label, NULL)) {
                ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_CONTEXT,
                    "duplicate label", JS_TOK_IDENTIFIER);
            }
            if (ok) {
                if (parser->label_depth >= JS_MAX_PARSE_DEPTH) {
                    ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_NESTING,
                        error_nesting, JS_TOK_EOF);
                } else {
                    parser->labels[parser->label_depth].token = label;
                    parser->labels[parser->label_depth].iteration = iteration;
                    parser->label_depth++;
                    ok = js_parser_parse_statement(parser);
                    parser->label_depth--;
                }
            }
            if (ok && !parser->last_statement_reduced) statement_child_count = 0;
        } else if (!js_parser_token_starts_expression(parser->current.kind)) {
            ok = js_parser_parser_fail(parser, JS_PARSE_ERROR_UNEXPECTED_TOKEN,
                error_expected_statement, JS_TOK_IDENTIFIER);
        } else {
            statement_form = JS_REDUCTION_EXPRESSION_STATEMENT;
            ok = js_parser_parse_expression(parser, JS_BP_SEQUENCE, NULL) && js_parser_parse_semicolon(parser);
        }
        break;
    }
    if (ok && emit_statement) {
        ok = js_parser_parser_reduce_with_child_flags(parser, JS_REDUCE_STATEMENT,
            statement_form,
            js_parser_span_from_start(first, parser->previous.span), first,
            statement_secondary, 0, statement_child_count,
            statement_child_flags);
        parser->last_statement_reduced = ok;
    }
    js_parser_parser_leave(parser);
    return ok;
}

static bool js_parser_parse_statement_list(JsParser* parser, JsTokenKind terminator,
        uint32_t* count_out) {
    uint32_t count = 0;
    while (parser->status == JS_PARSE_OK && parser->current.kind != terminator) {
        if (parser->current.kind == JS_TOK_EOF) {
            return terminator == JS_TOK_EOF || js_parser_parser_fail(parser,
                JS_PARSE_ERROR_UNEXPECTED_EOF, error_unterminated, terminator);
        }
        if (!js_parser_parse_statement(parser)) return false;
        if (parser->last_statement_reduced) count++;
    }
    if (count_out) *count_out = count;
    return parser->status == JS_PARSE_OK;
}

JsParseStatus js_parser_parse_source(const char* source, size_t length,
        JsParseMode mode, const JsParseSink* sink, void* sink_context,
        JsParseMetrics* metrics, JsParseError* error) {
    if (metrics) memset(metrics, 0, sizeof(*metrics));
    if (error) memset(error, 0, sizeof(*error));
    if (!source || length > UINT32_MAX) {
        if (error) {
            error->code = JS_PARSE_ERROR_INVALID_SOURCE;
            error->message = "source is null or exceeds the parser size limit";
        }
        return JS_PARSE_ERROR;
    }
    JsParser parser;
    memset(&parser, 0, sizeof(parser));
    parser.mode = mode;
    parser.sink = sink;
    parser.sink_context = sink_context;
    parser.metrics = metrics;
    parser.error = error;
    parser.status = JS_PARSE_OK;
    js_lexer_init(&parser.lexer, source, length);
    parser.current = js_lexer_next(&parser.lexer);
    js_lexer_set_goal(&parser.lexer, js_parser_parser_goal_after(parser.current.kind));
    parser.next_start = parser.lexer;
    parser.next = js_lexer_next(&parser.lexer);
    if (metrics) metrics->token_count = 2;
    if (parser.current.kind == JS_TOK_HASHBANG) js_parser_parser_advance(&parser);
    if (parser.current.kind == JS_TOK_ERROR || parser.next.kind == JS_TOK_ERROR) {
        js_parser_parser_record_error(&parser, JS_PARSE_ERROR_INVALID_TOKEN,
            error_unexpected_token, JS_TOK_EOF);
        return parser.status;
    }
    if (!js_parser_parse_statement_list(&parser, JS_TOK_EOF, NULL)) return parser.status;
    if (!js_parser_parser_reduce(&parser, JS_REDUCE_PROGRAM, JS_REDUCTION_NONE,
            (SourceSpan){0, (uint32_t)length}, (JsToken){0}, (JsToken){0},
            (uint32_t)mode, 0)) return parser.status;
    return JS_PARSE_OK;
}
