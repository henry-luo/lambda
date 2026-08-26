#include "lambda_rd_parser.h"
#include <string.h>
enum {
    LAMBDA_RD_MAX_DEPTH = 1000,
    LAMBDA_BP_PIPE = 10,
    LAMBDA_BP_OR = 20,
    LAMBDA_BP_AND = 30,
    LAMBDA_BP_NOT = 35,
    LAMBDA_BP_MEMBERSHIP = 40,
    LAMBDA_BP_SET = 50,
    LAMBDA_BP_EQUALITY = 60,
    LAMBDA_BP_RELATION = 70,
    LAMBDA_BP_ADD = 80,
    LAMBDA_BP_MULTIPLY = 90,
    LAMBDA_BP_POWER = 100,
    LAMBDA_BP_PREFIX = 105,
    LAMBDA_BP_POSTFIX = 110,
};
typedef struct LambdaRdParser {
    LambdaLexer lexer;
    LambdaToken current;
    LambdaToken next;
    const LambdaParseSink* sink;
    void* sink_context;
    LambdaParseMetrics* metrics;
    LambdaParseError* error;
    LambdaParseStatus status;
    uint32_t depth;
    uint32_t stop_at_element_close;
    uint32_t stop_at_element_attribute_close;
    LambdaTokenKind prev_kind;
    uint32_t procedural_depth;
    bool last_statement_self_delimiting;
    bool last_statement_assignment;
    uint32_t expression_depth;
    uint32_t pipe_rhs_depth;
    bool pipe_rhs_has_current;
    bool top_level_statement_relation;
    LambdaParseValue last_parameter_list;
    bool last_parameter_variadic;
} LambdaRdParser;
static uint64_t mix_hash(uint64_t hash, uint64_t value) {
    hash ^= value + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) + (hash >> 2);
    return hash;
}
static LambdaParseValue parser_reduce_detail_ex(LambdaRdParser* parser,
        LambdaReductionKind kind, LambdaReductionForm form,
        SourceSpan span, LambdaToken detail_token,
        LambdaToken secondary_token, uint32_t flags,
        const LambdaToken* name_tokens, uint32_t name_count,
        const LambdaParseValue* children, uint32_t child_count) {
    uint64_t value = UINT64_C(0xcbf29ce484222325);
    value = mix_hash(value, (uint64_t)kind);
    value = mix_hash(value, (uint64_t)form);
    value = mix_hash(value, span.start_byte);
    value = mix_hash(value, span.end_byte);
    value = mix_hash(value, (uint64_t)detail_token.kind);
    value = mix_hash(value, detail_token.span.start_byte);
    value = mix_hash(value, detail_token.span.end_byte);
    value = mix_hash(value, (uint64_t)secondary_token.kind);
    value = mix_hash(value, secondary_token.span.start_byte);
    value = mix_hash(value, secondary_token.span.end_byte);
    value = mix_hash(value, flags);
    for (uint32_t i = 0; i < name_count; i++) {
        value = mix_hash(value, (uint64_t)name_tokens[i].kind);
        value = mix_hash(value, name_tokens[i].span.start_byte);
        value = mix_hash(value, name_tokens[i].span.end_byte);
    }
    for (uint32_t i = 0; i < child_count; i++) value = mix_hash(value, children[i]);
    if (parser->sink && parser->sink->reduce) {
        LambdaParseReduction reduction = {
            .kind = kind,
            .form = form,
            .span = span,
            .detail_token = detail_token,
            .secondary_token = secondary_token,
            .flags = flags,
            .name_tokens = name_tokens,
            .name_count = name_count,
            .children = children,
            .child_count = child_count,
        };
        LambdaParseValue sink_value = parser->sink->reduce(parser->sink_context,
            &reduction);
        if (sink_value) value = sink_value;
    }
    if (parser->metrics) {
        parser->metrics->reduction_count++;
        parser->metrics->structural_hash = mix_hash(parser->metrics->structural_hash, value);
    }
    return value;
}

static LambdaParseValue parser_reduce_detail(LambdaRdParser* parser,
        LambdaReductionKind kind, LambdaReductionForm form,
        SourceSpan span, LambdaToken detail_token,
        const LambdaParseValue* children, uint32_t child_count) {
    return parser_reduce_detail_ex(parser, kind, form, span, detail_token,
        (LambdaToken){0}, 0, NULL, 0, children, child_count);
}

static LambdaParseValue parser_reduce(LambdaRdParser* parser,
        LambdaReductionKind kind, SourceSpan span,
        const LambdaParseValue* children, uint32_t child_count) {
    LambdaToken no_detail = {0};
    return parser_reduce_detail(parser, kind, LAMBDA_REDUCTION_FORM_NONE, span,
        no_detail, children, child_count);
}

static LambdaParseValue parser_reduce_token(LambdaRdParser* parser,
        LambdaReductionKind kind, LambdaReductionForm form,
        SourceSpan span, LambdaToken detail_token,
        const LambdaParseValue* children, uint32_t child_count) {
    return parser_reduce_detail(parser, kind, form, span, detail_token, children,
        child_count);
}

static LambdaParseValue parser_reduce_tokens(LambdaRdParser* parser,
        LambdaReductionKind kind, LambdaReductionForm form,
        SourceSpan span, LambdaToken detail_token,
        LambdaToken secondary_token, uint32_t flags,
        const LambdaParseValue* children, uint32_t child_count) {
    return parser_reduce_detail_ex(parser, kind, form, span, detail_token,
        secondary_token, flags, NULL, 0, children, child_count);
}

static LambdaParseValue parser_reduce_name_tokens(LambdaRdParser* parser,
        LambdaReductionKind kind, SourceSpan span, LambdaToken first,
        const LambdaToken* names, uint32_t name_count, uint32_t flags,
        const LambdaParseValue* children, uint32_t child_count) {
    return parser_reduce_detail_ex(parser, kind,
        LAMBDA_REDUCTION_FORM_DECOMPOSE, span, first, (LambdaToken){0}, flags,
        names, name_count, children, child_count);
}
static LambdaParseValue parser_append_list(LambdaRdParser* parser, SourceSpan span,
        LambdaParseValue list, LambdaParseValue item, LambdaReductionForm form) {
    if (!list) {
        return parser_reduce_token(parser, LAMBDA_REDUCE_LIST,
            form, span, (LambdaToken){0}, &item, 1);
    }
    LambdaParseValue children[2] = {list, item};
    return parser_reduce_token(parser, LAMBDA_REDUCE_LIST,
        form, span, (LambdaToken){0}, children, 2);
}
static LambdaParseValue parser_list_append(LambdaRdParser* p, SourceSpan s,
        LambdaParseValue l, LambdaParseValue i) {
    return parser_append_list(p, s, l, i, LAMBDA_REDUCTION_FORM_NONE);
}
static LambdaParseValue parser_for_clause_append(LambdaRdParser* p, SourceSpan s,
        LambdaParseValue l, LambdaParseValue i) {
    return parser_append_list(p, s, l, i, LAMBDA_REDUCTION_FORM_FOR_CLAUSES);
}
static LambdaParseValue parser_content_append(LambdaRdParser* p, SourceSpan s,
        LambdaParseValue l, LambdaParseValue i) {
    return parser_append_list(p, s, l, i, LAMBDA_REDUCTION_FORM_CONTENT);
}
static LambdaParseValue parser_parameter_append(LambdaRdParser* p, SourceSpan s,
        LambdaParseValue l, LambdaParseValue i) {
    return parser_append_list(p, s, l, i, LAMBDA_REDUCTION_FORM_PARAMETERS);
}
static void parser_set_error(LambdaRdParser* parser, const char* message,
        LambdaTokenKind expected) {
    if (parser->status != LAMBDA_PARSE_OK) return;
    parser->status = parser->current.kind == LAMBDA_TOK_EOF ?
        LAMBDA_PARSE_INCOMPLETE : LAMBDA_PARSE_ERROR;
    if (!parser->error) return;
    parser->error->span = parser->current.span;
    memset(parser->error->expected_token_bits, 0, sizeof(parser->error->expected_token_bits));
    if ((unsigned int)expected < 256u) {
        parser->error->expected_token_bits[(unsigned int)expected / 64u] |=
            UINT64_C(1) << ((unsigned int)expected % 64u);
    }
    parser->error->actual_kind = parser->current.kind;
    parser->error->message = message;
}
static LambdaParseValue parser_fail(LambdaRdParser* parser, const char* message,
        LambdaTokenKind expected) {
    parser_set_error(parser, message, expected);
    return 0;
}
static bool parser_fail_bool(LambdaRdParser* parser, const char* message,
        LambdaTokenKind expected) {
    parser_set_error(parser, message, expected);
    return false;
}
static void parser_prepare_probe(LambdaRdParser* probe) {
    probe->sink = NULL;
    probe->sink_context = NULL;
    probe->metrics = NULL;
    probe->error = NULL;
}
static LambdaToken parser_next_significant(LambdaRdParser* parser) {
    LambdaToken token = lambda_lexer_next(&parser->lexer);
    bool saw_newline = false;
    while (token.kind == LAMBDA_TOK_NEWLINE) {
        saw_newline = true;
        token = lambda_lexer_next(&parser->lexer);
    }
    token.nl_before = saw_newline;
    return token;
}
static bool token_is_dual_role(LambdaTokenKind kind) {
    switch (kind) {
        case LAMBDA_TOK_LPAREN:
        case LAMBDA_TOK_LBRACKET:
        case LAMBDA_TOK_PLUS:
        case LAMBDA_TOK_MINUS:
        case LAMBDA_TOK_STAR:
        case LAMBDA_TOK_SLASH:
        case LAMBDA_TOK_CARET:
        case LAMBDA_TOK_LT:
        case LAMBDA_TOK_DOT:
            return true;
        default:
            return false;
    }
}
static bool token_is_control_statement(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_RETURN || kind == LAMBDA_TOK_BREAK ||
        kind == LAMBDA_TOK_CONTINUE;
}
static bool token_is_dot_led_number(const LambdaRdParser* parser,
        const LambdaToken* token) {
    return (token->kind == LAMBDA_TOK_FLOAT || token->kind == LAMBDA_TOK_DECIMAL) &&
        token->span.start_byte < parser->lexer.length &&
        parser->lexer.source[token->span.start_byte] == '.';
}
static void parser_advance(LambdaRdParser* parser) {
    if (parser->status != LAMBDA_PARSE_OK) return;
    parser->prev_kind = parser->current.kind;
    parser->current = parser->next;
    parser->next = parser_next_significant(parser);
    if (parser->metrics) parser->metrics->token_count++;
    if (parser->current.kind == LAMBDA_TOK_ERROR) {
        parser_set_error(parser, "invalid token", LAMBDA_TOK_EOF);
    }
}
static bool parser_accept(LambdaRdParser* parser, LambdaTokenKind kind) {
    if (parser->current.kind != kind) return false;
    parser_advance(parser);
    return true;
}
static bool parser_expect(LambdaRdParser* parser, LambdaTokenKind kind,
        const char* message) {
    if (parser_accept(parser, kind)) return true;
    return parser_fail_bool(parser, message, kind);
}
static bool token_is_identifier_like(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_IDENTIFIER || kind == LAMBDA_TOK_DIV ||
        kind == LAMBDA_TOK_STATE || kind == LAMBDA_TOK_APPLY ||
        kind == LAMBDA_TOK_VIEW || kind == LAMBDA_TOK_EDIT ||
        kind == LAMBDA_TOK_ON ||
        kind == LAMBDA_TOK_AND || kind == LAMBDA_TOK_OR ||
        kind == LAMBDA_TOK_TO || kind == LAMBDA_TOK_IS ||
        kind == LAMBDA_TOK_IN || kind == LAMBDA_TOK_AT ||
        kind == LAMBDA_TOK_THAT || kind == LAMBDA_TOK_WHERE ||
        kind == LAMBDA_TOK_ORDER || kind == LAMBDA_TOK_BY ||
        kind == LAMBDA_TOK_GROUP || kind == LAMBDA_TOK_INTO ||
        kind == LAMBDA_TOK_LIMIT || kind == LAMBDA_TOK_OFFSET ||
        kind == LAMBDA_TOK_ASC || kind == LAMBDA_TOK_DESC ||
        kind == LAMBDA_TOK_AS || kind == LAMBDA_TOK_EQ_WORD ||
        kind == LAMBDA_TOK_NE_WORD || kind == LAMBDA_TOK_LT_WORD ||
        kind == LAMBDA_TOK_LE_WORD || kind == LAMBDA_TOK_GE_WORD ||
        kind == LAMBDA_TOK_GT_WORD;
}
static bool token_is_key(LambdaTokenKind kind) {
    return token_is_identifier_like(kind) || kind == LAMBDA_TOK_SYMBOL ||
        kind == LAMBDA_TOK_BASE_TYPE || kind == LAMBDA_TOK_TYPE ||
        kind == LAMBDA_TOK_LET || kind == LAMBDA_TOK_PUB ||
        kind == LAMBDA_TOK_VAR || kind == LAMBDA_TOK_FN || kind == LAMBDA_TOK_PN ||
        kind == LAMBDA_TOK_IF || kind == LAMBDA_TOK_ELSE ||
        kind == LAMBDA_TOK_MATCH || kind == LAMBDA_TOK_CASE ||
        kind == LAMBDA_TOK_DEFAULT || kind == LAMBDA_TOK_LAST ||
        kind == LAMBDA_TOK_FOR || kind == LAMBDA_TOK_WHILE ||
        kind == LAMBDA_TOK_BREAK || kind == LAMBDA_TOK_CONTINUE ||
        kind == LAMBDA_TOK_RETURN || kind == LAMBDA_TOK_RAISE ||
        kind == LAMBDA_TOK_IMPORT ||
        kind == LAMBDA_TOK_STAR;
}
static bool token_is_element_name(LambdaTokenKind kind) {
    return token_is_identifier_like(kind) || kind == LAMBDA_TOK_SYMBOL ||
        kind == LAMBDA_TOK_BASE_TYPE || kind == LAMBDA_TOK_TYPE;
}
static bool token_is_literal(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_INTEGER || kind == LAMBDA_TOK_FLOAT ||
        kind == LAMBDA_TOK_DECIMAL || kind == LAMBDA_TOK_SIZED_INTEGER ||
        kind == LAMBDA_TOK_SIZED_FLOAT || kind == LAMBDA_TOK_IMAGINARY ||
        kind == LAMBDA_TOK_STRING || kind == LAMBDA_TOK_SYMBOL ||
        kind == LAMBDA_TOK_BINARY || kind == LAMBDA_TOK_DATETIME ||
        kind == LAMBDA_TOK_NAMED_VALUE || kind == LAMBDA_TOK_BASE_TYPE ||
        kind == LAMBDA_TOK_TYPE || kind == LAMBDA_TOK_APPLY ||
        kind == LAMBDA_TOK_PATTERN_ISLAND;
}
static bool token_starts_type(LambdaTokenKind kind) {
    return token_is_literal(kind) || token_is_identifier_like(kind) ||
        kind == LAMBDA_TOK_LPAREN || kind == LAMBDA_TOK_LBRACKET ||
        kind == LAMBDA_TOK_LBRACE || kind == LAMBDA_TOK_LT || kind == LAMBDA_TOK_FN ||
        kind == LAMBDA_TOK_BANG;
}
static bool token_starts_return_type(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_IDENTIFIER || kind == LAMBDA_TOK_BASE_TYPE ||
        kind == LAMBDA_TOK_TYPE;
}
static bool token_starts_expression(LambdaTokenKind kind) {
    return token_is_literal(kind) || token_is_identifier_like(kind) ||
        kind == LAMBDA_TOK_FN || kind == LAMBDA_TOK_LAST ||
        kind == LAMBDA_TOK_LPAREN || kind == LAMBDA_TOK_LBRACKET ||
        kind == LAMBDA_TOK_LBRACE || kind == LAMBDA_TOK_LT || kind == LAMBDA_TOK_DOT ||
        kind == LAMBDA_TOK_SLASH || kind == LAMBDA_TOK_TILDE ||
        kind == LAMBDA_TOK_TILDE_INDEX || kind == LAMBDA_TOK_PARENT ||
        kind == LAMBDA_TOK_CARET || kind == LAMBDA_TOK_ELLIPSIS ||
        kind == LAMBDA_TOK_NOT || kind == LAMBDA_TOK_BANG ||
        kind == LAMBDA_TOK_MINUS || kind == LAMBDA_TOK_PLUS ||
        kind == LAMBDA_TOK_STAR || kind == LAMBDA_TOK_LET ||
        kind == LAMBDA_TOK_IF || kind == LAMBDA_TOK_MATCH ||
        kind == LAMBDA_TOK_FOR || kind == LAMBDA_TOK_RAISE;
}
static LambdaTokenKind type_delimiter_close(LambdaTokenKind kind) {
    switch (kind) {
    case LAMBDA_TOK_LPAREN: return LAMBDA_TOK_RPAREN;
    case LAMBDA_TOK_LBRACKET: return LAMBDA_TOK_RBRACKET;
    case LAMBDA_TOK_LBRACE: return LAMBDA_TOK_RBRACE;
    case LAMBDA_TOK_LT: return LAMBDA_TOK_GT;
    default: return LAMBDA_TOK_ERROR;
    }
}
static bool parser_enter(LambdaRdParser* parser) {
    parser->depth++;
    if (parser->metrics && parser->depth > parser->metrics->max_recursion_depth) {
        parser->metrics->max_recursion_depth = parser->depth;
    }
    if (parser->depth <= LAMBDA_RD_MAX_DEPTH) return true;
    return parser_fail_bool(parser, "maximum parser nesting exceeded", LAMBDA_TOK_EOF);
}
static void parser_leave(LambdaRdParser* parser) {
    if (parser->depth) parser->depth--;
}
static LambdaParseValue parse_expression(LambdaRdParser* parser, int min_bp);
static bool parser_consume_balanced(LambdaRdParser* parser,
        LambdaTokenKind opener, LambdaTokenKind closer, const char* message) {
    uint32_t nesting = 0;
    do {
        if (parser->current.kind == opener) nesting++;
        if (parser->current.kind == closer && nesting) nesting--;
        parser_advance(parser);
        if (parser->current.kind == LAMBDA_TOK_EOF && nesting) {
            return parser_fail_bool(parser, message, closer);
        }
    } while (nesting);
    return true;
}
typedef LambdaParseValue (*LambdaParseItemFn)(LambdaRdParser* parser);
static LambdaParseValue parser_parse_expression_item(LambdaRdParser* parser) {
    return parse_expression(parser, 0);
}

static bool parser_parse_expression_list(LambdaRdParser* parser,
        LambdaTokenKind closer, LambdaParseValue* children, uint32_t limit,
        uint32_t* count, bool allow_empty, LambdaParseItemFn parse_item,
        const char* too_many,
        const char* expected_close) {
    *count = 0;
    if (allow_empty && parser_accept(parser, closer)) return true;
    do {
        if (*count == limit) {
            return parser_fail_bool(parser, too_many, closer);
        }
        children[(*count)++] = parse_item(parser);
        if (parser->status != LAMBDA_PARSE_OK) return false;
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
    } while (true);
    return parser_expect(parser, closer, expected_close);
}
static LambdaParseValue parse_content(LambdaRdParser* parser, LambdaTokenKind terminator);
static bool parser_parse_content_block(LambdaRdParser* parser, LambdaToken marker,
        LambdaReductionForm begin_form, LambdaReductionForm end_form,
        const char* close_message, LambdaParseValue* value_out) {
    if (!parser_accept(parser, LAMBDA_TOK_LBRACE)) return false;
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT, begin_form,
        marker.span, marker, NULL, 0);
    LambdaParseValue value = parse_content(parser, LAMBDA_TOK_RBRACE);
    if (!parser_expect(parser, LAMBDA_TOK_RBRACE, close_message)) return false;
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT, end_form,
        marker.span, marker, NULL, 0);
    if (value_out) *value_out = value;
    return parser->status == LAMBDA_PARSE_OK;
}
static bool parser_extend_element_name(LambdaRdParser* parser, LambdaToken* name,
        const char* message) {
    while (parser_accept(parser, LAMBDA_TOK_DOT)) {
        if (!token_is_element_name(parser->current.kind))
            return parser_fail_bool(parser, message, LAMBDA_TOK_IDENTIFIER);
        parser_advance(parser);
        name->span.end_byte = parser->current.span.start_byte;
    }
    return true;
}

static LambdaParseValue parse_type_slot(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (!token_starts_type(first.kind)) {
        return parser_fail(parser, "expected a type pattern", LAMBDA_TOK_BASE_TYPE);
    }
    LambdaTokenKind closing_stack[128];
    uint32_t nesting = 0;
    bool need_atom = true;
    bool function_type = first.kind == LAMBDA_TOK_FN;
    while (parser->status == LAMBDA_PARSE_OK) {
        LambdaTokenKind kind = parser->current.kind;
        if (kind == LAMBDA_TOK_EOF && nesting) {
            return parser_fail(parser, "incomplete type pattern", closing_stack[nesting - 1]);
        }
        if (parser->current.nl_before && !nesting && !need_atom) {
            if (kind != LAMBDA_TOK_PIPE && kind != LAMBDA_TOK_AMPERSAND &&
                    kind != LAMBDA_TOK_BANG) {
                break;
            }
        }
        if (need_atom) {
            if (kind == LAMBDA_TOK_BANG) {
                parser_advance(parser);
                continue;
            }
            if (!token_starts_type(kind)) break;
            LambdaTokenKind close = type_delimiter_close(kind);
            if (close != LAMBDA_TOK_ERROR) {
                if (nesting == 128) {
            return parser_fail(parser, "type pattern delimiter nesting exceeded", close);
                }
                closing_stack[nesting++] = close;
            }
            parser_advance(parser);
            need_atom = false;
            continue;
        }
        if (!nesting && first.kind == LAMBDA_TOK_FN && kind == LAMBDA_TOK_LPAREN) {
            closing_stack[nesting++] = LAMBDA_TOK_RPAREN;
            parser_advance(parser);
            continue;
        }
        if (nesting && kind == closing_stack[nesting - 1]) {
            nesting--;
            parser_advance(parser);
            if (function_type && !nesting) need_atom = true;
            continue;
        }
        if (nesting) {
            LambdaTokenKind close = type_delimiter_close(kind);
            if (close != LAMBDA_TOK_ERROR) {
                if (nesting == 128) {
            return parser_fail(parser, "type pattern delimiter nesting exceeded", close);
                }
                closing_stack[nesting++] = close;
            }
            parser_advance(parser);
            continue;
        }
        if (kind == LAMBDA_TOK_QUESTION || kind == LAMBDA_TOK_PLUS ||
                kind == LAMBDA_TOK_STAR) {
            parser_advance(parser);
            continue;
        }
        if (kind == LAMBDA_TOK_LBRACKET) {
            if (!parser_consume_balanced(parser, LAMBDA_TOK_LBRACKET,
                    LAMBDA_TOK_RBRACKET, "incomplete type occurrence suffix")) return 0;
            continue;
        }
        if (kind == LAMBDA_TOK_PIPE || kind == LAMBDA_TOK_AMPERSAND ||
                kind == LAMBDA_TOK_BANG) {
            parser_advance(parser);
            need_atom = true;
            continue;
        }
        break;
    }
    if (need_atom) {
        return parser_fail(parser, "expected a type pattern after type operator",
            LAMBDA_TOK_BASE_TYPE);
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce_token(parser, LAMBDA_REDUCE_TYPE_SLOT,
        LAMBDA_REDUCTION_FORM_TOKEN, span, first, NULL, 0);
}

static bool parse_annotation_type_slot_value(LambdaRdParser* parser,
        LambdaParseValue* value_out) {
    LambdaToken first = parser->current;
    LambdaParseValue value = parse_type_slot(parser);
    if (!value) return false;
    if (parser_accept(parser, LAMBDA_TOK_TO)) {
        if (!parse_expression(parser, 0)) return false;
        SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        value = parser_reduce_token(parser, LAMBDA_REDUCE_TYPE_SLOT,
            LAMBDA_REDUCTION_FORM_TOKEN, span, first, NULL, 0);
    }
    if (parser_accept(parser, LAMBDA_TOK_THAT)) {
        LambdaParseValue constraint = parse_expression(parser, 0);
        if (!constraint) return false;
        LambdaParseValue children[2] = {value, constraint};
        SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        value = parser_reduce_tokens(parser, LAMBDA_REDUCE_TYPE_SLOT,
            LAMBDA_REDUCTION_FORM_TOKEN, span, first, (LambdaToken){0},
            LAMBDA_REDUCTION_FLAG_ANNOTATION_CONSTRAINT, children, 2);
    }
    if (value_out) *value_out = value;
    return true;
}
static LambdaParseValue parse_primary_type_slot(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (!token_starts_type(first.kind)) {
        return parser_fail(parser, "expected a primary type", LAMBDA_TOK_BASE_TYPE);
    }
    if (first.kind != LAMBDA_TOK_LPAREN && first.kind != LAMBDA_TOK_LBRACKET &&
            first.kind != LAMBDA_TOK_LBRACE && first.kind != LAMBDA_TOK_LT) {
        parser_advance(parser);
    } else {
        LambdaTokenKind close = first.kind == LAMBDA_TOK_LPAREN ? LAMBDA_TOK_RPAREN :
            (first.kind == LAMBDA_TOK_LBRACKET ? LAMBDA_TOK_RBRACKET :
            (first.kind == LAMBDA_TOK_LBRACE ? LAMBDA_TOK_RBRACE : LAMBDA_TOK_GT));
        if (!parser_consume_balanced(parser, first.kind, close,
                "incomplete primary type")) return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce_token(parser, LAMBDA_REDUCE_TYPE_SLOT,
        LAMBDA_REDUCTION_FORM_TOKEN, span, first, NULL, 0);
}
static bool parse_path_segment(LambdaRdParser* parser) {
    if (!token_is_key(parser->current.kind) && parser->current.kind != LAMBDA_TOK_PARENT &&
            parser->current.kind != LAMBDA_TOK_SLASH &&
            parser->current.kind != LAMBDA_TOK_INTEGER &&
            parser->current.kind != LAMBDA_TOK_STAR_STAR) {
        return parser_fail_bool(parser, "expected a path segment", LAMBDA_TOK_IDENTIFIER);
    }
    parser_advance(parser);
    return true;
}
static LambdaParseValue parse_path_slot(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (parser_accept(parser, LAMBDA_TOK_SLASH)) {
        if (!parser_expect(parser, LAMBDA_TOK_DOT, "expected '.' after path root")) return 0;
    } else if (!parser_expect(parser, LAMBDA_TOK_PATH_REL, "expected path introducer")) {
        // §7.15: the relative introducer is `\.`, not a bare dot.
        return 0;
    }
    if (!parse_path_segment(parser)) return 0;
    while (parser_accept(parser, LAMBDA_TOK_DOT)) {
        if (!parse_path_segment(parser)) return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce_token(parser, LAMBDA_REDUCE_PATH_SLOT,
        LAMBDA_REDUCTION_FORM_TOKEN, span, first, NULL, 0);
}

static LambdaParseValue parse_array(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue items = 0;
    parser_advance(parser);
    if (!parser_accept(parser, LAMBDA_TOK_RBRACKET)) {
        do {
            LambdaParseValue item = parse_expression(parser, 0);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            items = parser_list_append(parser,
                (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
                items, item);
            if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        } while (true);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACKET, "expected ']' after array items")) return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_ARRAY, span, items ? &items : NULL, items ? 1u : 0u);
}

static LambdaParseValue parse_map(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue items = 0;
    parser_advance(parser);
    if (!parser_accept(parser, LAMBDA_TOK_RBRACE)) {
        do {
            if (parser->current.kind == LAMBDA_TOK_STRING) {
                return parser_fail(parser,
                    "a map key is a symbol, not a string: write a bare name "
                    "like {key: 1}, or single-quote it when it is not a name "
                    "like {'data-node-id': 1}", LAMBDA_TOK_IDENTIFIER);
            }
            if (!token_is_key(parser->current.kind)) {
        return parser_fail(parser, "expected a map key", LAMBDA_TOK_IDENTIFIER);
            }
            LambdaToken key = parser->current;
            parser_advance(parser);
            if (!parser_expect(parser, LAMBDA_TOK_COLON, "expected ':' after map key")) return 0;
            LambdaParseValue value = parse_expression(parser, 0);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            SourceSpan item_span = {key.span.start_byte, parser->current.span.start_byte};
            LambdaParseValue item = parser_reduce_token(parser,
                LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_MAP_ITEM,
                item_span, key, &value, 1);
            items = parser_list_append(parser,
                (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
                items, item);
            if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        } while (true);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after map items")) return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_MAP, span, items ? &items : NULL, items ? 1u : 0u);
}
static bool element_attribute_starts(const LambdaRdParser* parser) {
    LambdaRdParser probe = *parser;
    parser_prepare_probe(&probe);
    if (!token_is_key(probe.current.kind)) return false;
    parser_advance(&probe);
    while (probe.current.kind == LAMBDA_TOK_DOT) {
        parser_advance(&probe);
        if (!token_is_element_name(probe.current.kind)) return false;
        parser_advance(&probe);
    }
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind == LAMBDA_TOK_COLON;
}

static LambdaParseValue parse_element(LambdaRdParser* parser) {
    bool had_attributes = false;
    bool boundary_comma = false;
    LambdaToken first = parser->current;
    LambdaToken tag = {0};
    LambdaParseValue children[64];
    uint32_t count = 0;
    parser_advance(parser);
    if (!token_is_element_name(parser->current.kind)) {
        return parser_fail(parser, "expected an element tag", LAMBDA_TOK_IDENTIFIER);
    }
    tag = parser->current;
    parser_advance(parser);
    if (!parser_extend_element_name(parser, &tag,
            "expected a namespace segment after '.'")) return 0;
    for (;;) {
        if (!element_attribute_starts(parser)) break;
        LambdaToken attribute_name = parser->current;
        parser_advance(parser);
        if (!parser_extend_element_name(parser, &attribute_name,
                "expected an attribute namespace segment")) return 0;
        parser_advance(parser);
        if (count == 64) {
            return parser_fail(parser, "too many element attributes in parser POC", LAMBDA_TOK_GT);
        }
        parser->stop_at_element_close++;
        parser->stop_at_element_attribute_close++;
        LambdaParseValue value = parse_expression(parser, 0);
        parser->stop_at_element_attribute_close--;
        parser->stop_at_element_close--;
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        SourceSpan attribute_span = {attribute_name.span.start_byte,
            parser->current.span.start_byte};
        children[count++] = parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
            LAMBDA_REDUCTION_FORM_ELEMENT_ATTRIBUTE, attribute_span,
            attribute_name, &value, 1);
        had_attributes = true;
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        if (!element_attribute_starts(parser)) {
            boundary_comma = true;
            break;
        }
    }
    if (!had_attributes && parser->current.kind == LAMBDA_TOK_COMMA) {
        return parser_fail(parser,
            "an element with no attributes takes no ',' before its content",
            LAMBDA_TOK_GT);
    }
    if (!had_attributes && parser->current.kind == LAMBDA_TOK_SEMICOLON) {
        return parser_fail(parser,
            "';' cannot open element content; a tag is followed directly by its "
            "content, and ';' only separates one content item from the next",
            LAMBDA_TOK_GT);
    }
    if (had_attributes && !boundary_comma && parser->current.kind != LAMBDA_TOK_GT) {
        return parser_fail(parser,
            "expected ',' between element attributes and content",
            LAMBDA_TOK_COMMA);
    }
    if (boundary_comma && parser->current.kind == LAMBDA_TOK_GT) {
        return parser_fail(parser, "trailing ',' is not a separator", LAMBDA_TOK_GT);
    }
    if (parser->current.kind != LAMBDA_TOK_GT) {
        if (count == 64) {
            return parser_fail(parser, "too many element content items in parser POC", LAMBDA_TOK_GT);
        }
        parser->stop_at_element_close++;
        children[count++] = parse_content(parser, LAMBDA_TOK_GT);
        parser->stop_at_element_close--;
        if (parser->status != LAMBDA_PARSE_OK) return 0;
    }
    if (!parser_expect(parser, LAMBDA_TOK_GT, "expected '>' after element")) return 0;
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce_token(parser, LAMBDA_REDUCE_ELEMENT,
        LAMBDA_REDUCTION_FORM_TOKEN, span, tag, children, count);
}

static bool arrow_return_tail_candidate(LambdaRdParser* probe) {
    parser_advance(probe);
    if (token_starts_return_type(probe->current.kind)) {
        if (!parse_type_slot(probe)) return false;
        if (probe->current.kind == LAMBDA_TOK_CARET) {
            parser_advance(probe);
            if (token_starts_return_type(probe->current.kind) &&
                    !parse_type_slot(probe)) return false;
        }
    }
    return probe->current.kind == LAMBDA_TOK_ARROW;
}

static bool arrow_head_candidate(const LambdaRdParser* parser) {
    LambdaRdParser probe = *parser;
    parser_prepare_probe(&probe);
    if (probe.current.kind == LAMBDA_TOK_RPAREN) {
        return arrow_return_tail_candidate(&probe);
    }
    for (;;) {
        if (!token_is_key(probe.current.kind)) return false;
        parser_advance(&probe);
        if (probe.current.kind == LAMBDA_TOK_QUESTION) parser_advance(&probe);
        if (probe.current.kind == LAMBDA_TOK_COLON) {
            parser_advance(&probe);
            LambdaParseValue type_value = 0;
            if (!parse_annotation_type_slot_value(&probe, &type_value)) return false;
        }
        if (probe.current.kind == LAMBDA_TOK_EQ) {
            parser_advance(&probe);
            if (!parse_expression(&probe, 0)) return false;
        }
        if (probe.current.kind == LAMBDA_TOK_RPAREN) {
            return arrow_return_tail_candidate(&probe);
        }
        if (probe.current.kind != LAMBDA_TOK_COMMA) return false;
        parser_advance(&probe);
    }
}

static bool parse_parameter_items(LambdaRdParser* parser, bool allow_variadic,
        const char* name_message, const char* close_message,
        LambdaParseValue* value_out, bool* variadic_out) {
    LambdaParseValue parameters = 0;
    if (variadic_out) *variadic_out = false;
    if (parser_accept(parser, LAMBDA_TOK_RPAREN)) {
        if (value_out) *value_out = 0;
        return true;
    }
    do {
        if (allow_variadic && parser_accept(parser, LAMBDA_TOK_ELLIPSIS)) {
            if (variadic_out) *variadic_out = true;
            break;
        }
        bool is_var = parser_accept(parser, LAMBDA_TOK_VAR);
        if (!token_is_key(parser->current.kind)) {
            return parser_fail_bool(parser, name_message, LAMBDA_TOK_IDENTIFIER);
        }
        LambdaToken name = parser->current;
        parser_advance(parser);
        bool optional = parser_accept(parser, LAMBDA_TOK_QUESTION);
        LambdaParseValue type_value = 0;
        if (parser_accept(parser, LAMBDA_TOK_COLON) &&
                !parse_annotation_type_slot_value(parser, &type_value)) return false;
        LambdaParseValue default_value = 0;
        if (parser_accept(parser, LAMBDA_TOK_EQ)) {
            default_value = parse_expression(parser, 0);
            if (!default_value) return false;
            optional = true;
        }
        LambdaParseValue parameter_children[2];
        uint32_t child_count = 0;
        if (type_value) parameter_children[child_count++] = type_value;
        if (default_value) parameter_children[child_count++] = default_value;
        LambdaParseValue parameter = parser_reduce_tokens(parser,
            LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_PARAMETER,
            (SourceSpan){name.span.start_byte, parser->current.span.start_byte}, name,
            (LambdaToken){0}, (optional ? LAMBDA_REDUCTION_FLAG_OPTIONAL : 0u) |
                (is_var ? LAMBDA_REDUCTION_FLAG_VAR : 0u), parameter_children, child_count);
        parameters = parser_parameter_append(parser,
            (SourceSpan){name.span.start_byte, parser->current.span.start_byte},
            parameters, parameter);
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
    } while (true);
    if (!parser_expect(parser, LAMBDA_TOK_RPAREN, close_message)) return false;
    if (value_out) *value_out = parameters;
    return true;
}
static LambdaParseValue parse_arrow_parameters(LambdaRdParser* parser) {
    LambdaParseValue parameters = 0;
    if (!parse_parameter_items(parser, false, "expected an arrow parameter name",
            "expected ')' after arrow parameters", &parameters, NULL)) return 0;
    return parameters;
}

static LambdaParseValue parse_group_or_arrow(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue children[64] = {0};
    uint32_t count = 0;
    parser_advance(parser);
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_GROUP_BEGIN, first.span, first, NULL, 0);
    if (arrow_head_candidate(parser)) {
        parser_reduce_tokens(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_FUNCTION_BEGIN, first.span, first,
            (LambdaToken){0}, 0, NULL, 0);
        LambdaParseValue params = parse_arrow_parameters(parser);
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        children[count++] = params;
        bool raised = false;
        if (token_starts_return_type(parser->current.kind)) {
            children[count++] = parse_type_slot(parser);
            if (!children[count - 1]) return 0;
            if (parser_accept(parser, LAMBDA_TOK_CARET)) {
                raised = true;
                if (token_starts_return_type(parser->current.kind)) {
                    children[count++] = parse_type_slot(parser);
                    if (!children[count - 1]) return 0;
                }
            }
        }
        if (!parser_expect(parser, LAMBDA_TOK_ARROW, "expected '=>' after arrow parameters")) return 0;
        if (token_is_control_statement(parser->current.kind)) {
            return parser_fail(parser, "'return', 'break', and 'continue' are "
                "statements; an arrow '=>' body is an expression", LAMBDA_TOK_LBRACE);
        }
        uint32_t arrow_saved_depth = parser->procedural_depth;
        parser->procedural_depth = 0;
        children[count++] = parse_expression(parser, 0);
        parser->procedural_depth = arrow_saved_depth;
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        LambdaParseValue result = parser_reduce_tokens(parser, LAMBDA_REDUCE_FUNCTION,
            LAMBDA_REDUCTION_FORM_FUNCTION, span, first, (LambdaToken){0},
            raised ? LAMBDA_REDUCTION_FLAG_RAISED : 0u, children, count);
        parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_FUNCTION_END, first.span, first, NULL, 0);
        parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_GROUP_END, first.span, first, NULL, 0);
        return result;
    }
    bool empty_group = parser_accept(parser, LAMBDA_TOK_RPAREN);
    if (!empty_group) {
        if (!parser_parse_expression_list(parser, LAMBDA_TOK_RPAREN, children, 64,
                &count, false, parser_parse_expression_item,
                "too many grouped expressions in parser POC",
                "expected ')' after grouped expression")) return 0;
    }
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_GROUP_END, first.span, first, NULL, 0);
    if (empty_group) {
        return parser_fail(parser, "expected an expression inside parentheses", LAMBDA_TOK_IDENTIFIER);
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce_token(parser, LAMBDA_REDUCE_GROUP,
        LAMBDA_REDUCTION_FORM_GROUP, span, first, children, count);
}
static bool braced_expression_is_map(const LambdaRdParser* parser) {
    LambdaRdParser probe = *parser;
    parser_prepare_probe(&probe);
    if (probe.current.kind != LAMBDA_TOK_LBRACE) return false;
    parser_advance(&probe);
    if (probe.next.kind != LAMBDA_TOK_COLON) return false;
    return token_is_key(probe.current.kind) ||
        probe.current.kind == LAMBDA_TOK_STRING;
}
static bool control_body_brace_is_map(const LambdaRdParser* parser) {
    if (braced_expression_is_map(parser)) return true;
    return parser->current.kind == LAMBDA_TOK_LBRACE &&
        parser->next.kind == LAMBDA_TOK_RBRACE &&
        parser->procedural_depth == 0;
}

static LambdaParseValue parse_if_expression(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue children[3];
    parser_advance(parser);
    bool paren_condition = parser_accept(parser, LAMBDA_TOK_LPAREN);
    children[0] = parse_expression(parser, 0);
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    if (paren_condition && !parser_expect(parser, LAMBDA_TOK_RPAREN, "expected ')' after if condition")) return 0;
    if (parser->current.kind == LAMBDA_TOK_LBRACE && !control_body_brace_is_map(parser)) {
        if (!parser_parse_content_block(parser, first,
                LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN,
                LAMBDA_REDUCTION_FORM_IF_BRANCH_END,
                "expected '}' after if body", &children[1])) return 0;
    } else {
        if (token_is_control_statement(parser->current.kind)) {
            return parser_fail(parser, "'return', 'break', and 'continue' are "
                "statements; an unbraced 'if' body is an expression - write "
                "'if (cond) { ... }'", LAMBDA_TOK_LBRACE);
        }
        children[1] = parse_expression(parser, 0);
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    if (parser->current.kind != LAMBDA_TOK_ELSE) {
        SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        return parser_reduce(parser, LAMBDA_REDUCE_IF, span, children, 2);
    }
    parser_advance(parser);
    if (parser->current.kind == LAMBDA_TOK_LBRACE && !control_body_brace_is_map(parser)) {
        if (!parser_parse_content_block(parser, first,
                LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN,
                LAMBDA_REDUCTION_FORM_IF_BRANCH_END,
                "expected '}' after else body", &children[2])) return 0;
    } else {
        if (token_is_control_statement(parser->current.kind)) {
            return parser_fail(parser, "'return', 'break', and 'continue' are "
                "statements; an unbraced 'else' body is an expression - write "
                "'else { ... }'", LAMBDA_TOK_LBRACE);
        }
        children[2] = parse_expression(parser, 0);
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_IF, span, children, 3);
}
static LambdaParseValue parse_for_binding(LambdaRdParser* parser) {
    LambdaToken name = parser->current;
    LambdaToken index = {0};
    LambdaParseValue children[3];
    uint32_t child_count = 0;
    uint32_t flags = 0;
    if (!token_is_identifier_like(name.kind)) {
        return parser_fail(parser, "expected a for binding name", LAMBDA_TOK_IDENTIFIER);
    }
    parser_advance(parser);
    if (parser_accept(parser, LAMBDA_TOK_COLON)) {
        LambdaParseValue index_type = 0;
        if (!parse_annotation_type_slot_value(parser, &index_type)) return 0;
        flags |= LAMBDA_REDUCTION_FLAG_INDEX_TYPED;
        children[child_count++] = index_type;
        if (!parser_expect(parser, LAMBDA_TOK_COMMA, "expected ',' after typed for index")) return 0;
        if (!token_is_identifier_like(parser->current.kind)) {
            return parser_fail(parser, "expected a for value name", LAMBDA_TOK_IDENTIFIER);
        }
        index = name;
        name = parser->current;
        parser_advance(parser);
    } else if (parser_accept(parser, LAMBDA_TOK_COMMA)) {
        if (!token_is_identifier_like(parser->current.kind)) {
            return parser_fail(parser, "expected a for value name", LAMBDA_TOK_IDENTIFIER);
        }
        index = name;
        name = parser->current;
        parser_advance(parser);
    }
    if (parser_accept(parser, LAMBDA_TOK_QUESTION)) flags |= LAMBDA_REDUCTION_FLAG_OPTIONAL;
    if (parser->current.kind != LAMBDA_TOK_IN && parser->current.kind != LAMBDA_TOK_AT) {
        return parser_fail(parser, "expected 'in' or 'at' after for binding", LAMBDA_TOK_IN);
    }
    if (parser->current.kind == LAMBDA_TOK_AT) flags |= LAMBDA_REDUCTION_FLAG_KEY_ONLY;
    parser_advance(parser);
    LambdaParseValue source = parse_expression(parser, 0);
    if (!source) return 0;
    children[child_count++] = source;
    if (parser_accept(parser, LAMBDA_TOK_ON)) {
        LambdaParseValue join = parse_expression(parser, 0);
        if (!join) return 0;
        children[child_count++] = join;
    }
    return parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT,
        LAMBDA_REDUCTION_FORM_FOR_BINDING,
        (SourceSpan){name.span.start_byte, parser->current.span.start_byte},
        name, index, flags, children, child_count);
}
static LambdaParseValue parse_for_let_clause(LambdaRdParser* parser) {
    if (!parser_expect(parser, LAMBDA_TOK_LET, "expected let clause")) return 0;
    LambdaToken name = parser->current;
    if (!token_is_identifier_like(parser->current.kind)) {
        return parser_fail(parser, "expected a for-let name", LAMBDA_TOK_IDENTIFIER);
    }
    parser_advance(parser);
    if (!parser_expect(parser, LAMBDA_TOK_EQ, "expected '=' after for-let name")) return 0;
    LambdaParseValue value = parse_expression(parser, 0);
    if (!value) return 0;
    return parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
        LAMBDA_REDUCTION_FORM_FOR_LET,
        (SourceSpan){name.span.start_byte, parser->current.span.start_byte},
        name, &value, 1);
}
static bool for_binding_list_continues(const LambdaRdParser* parser) {
    if (parser->current.kind != LAMBDA_TOK_COMMA) return false;
    LambdaRdParser probe = *parser;
    parser_prepare_probe(&probe);
    parser_advance(&probe);
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind != LAMBDA_TOK_LET;
}
static LambdaParseValue parse_for_group_clause(LambdaRdParser* parser) {
    if (!parser_expect(parser, LAMBDA_TOK_BY, "expected 'by' after group")) return false;
    LambdaParseValue keys = 0;
    do {
        LambdaToken key_first = parser->current;
        LambdaParseValue key = parse_expression(parser, LAMBDA_BP_POSTFIX);
        if (!key) return 0;
        LambdaToken alias = {0};
        if (parser_accept(parser, LAMBDA_TOK_AS)) {
            if (!token_is_identifier_like(parser->current.kind)) {
                return parser_fail(parser, "expected a group alias", LAMBDA_TOK_IDENTIFIER);
            }
            alias = parser->current;
            parser_advance(parser);
        }
        LambdaParseValue key_item = parser_reduce_token(parser,
            LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_FOR_GROUP_KEY,
            (SourceSpan){key_first.span.start_byte, parser->current.span.start_byte},
            alias, &key, 1);
        keys = parser_list_append(parser,
            (SourceSpan){key_first.span.start_byte, parser->current.span.start_byte},
            keys, key_item);
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
    } while (true);
    if (!parser_expect(parser, LAMBDA_TOK_INTO, "expected 'into' after group keys")) return 0;
    LambdaToken name = parser->current;
    if (!token_is_identifier_like(parser->current.kind)) {
        return parser_fail(parser, "expected a group binding name", LAMBDA_TOK_IDENTIFIER);
    }
    parser_advance(parser);
    return parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
        LAMBDA_REDUCTION_FORM_FOR_GROUP,
        (SourceSpan){name.span.start_byte, parser->current.span.start_byte},
        name, &keys, 1);
}

static LambdaParseValue parse_for_expression(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue children[2];
    LambdaParseValue clauses = 0;
    parser_advance(parser);
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_FOR_BEGIN, first.span, first, NULL, 0);
    bool parenthesized = parser_accept(parser, LAMBDA_TOK_LPAREN);
    LambdaParseValue binding = parse_for_binding(parser);
    if (!binding) return 0;
    clauses = parser_for_clause_append(parser, first.span, clauses, binding);
    while (for_binding_list_continues(parser)) {
        parser_advance(parser);
        binding = parse_for_binding(parser);
        if (!binding) return 0;
        clauses = parser_for_clause_append(parser, first.span, clauses, binding);
    }
    while (parser->status == LAMBDA_PARSE_OK) {
        if (parser_accept(parser, LAMBDA_TOK_COMMA)) {
            LambdaParseValue let_clause = parse_for_let_clause(parser);
            if (!let_clause) return 0;
            clauses = parser_for_clause_append(parser, first.span, clauses, let_clause);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_WHERE)) {
            LambdaParseValue where = parse_expression(parser, 0);
            if (!where) return 0;
            clauses = parser_for_clause_append(parser, first.span, clauses,
                parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
                    LAMBDA_REDUCTION_FORM_FOR_WHERE, first.span, first, &where, 1));
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_GROUP)) {
            LambdaParseValue group = parse_for_group_clause(parser);
            if (!group) return 0;
            clauses = parser_for_clause_append(parser, first.span, clauses, group);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_ORDER)) {
            if (!parser_expect(parser, LAMBDA_TOK_BY, "expected 'by' after order")) return 0;
            do {
                LambdaParseValue order = parse_expression(parser, 0);
                if (!order) return 0;
                LambdaToken direction = {0};
                if (parser->current.kind == LAMBDA_TOK_ASC || parser->current.kind == LAMBDA_TOK_DESC) {
                    direction = parser->current;
                    parser_advance(parser);
                }
                clauses = parser_for_clause_append(parser, first.span, clauses,
                    parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
                        LAMBDA_REDUCTION_FORM_FOR_ORDER, first.span, direction,
                        &order, 1));
            } while (parser_accept(parser, LAMBDA_TOK_COMMA));
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_LIMIT)) {
            LambdaToken last = parser->current;
            bool from_end = parser_accept(parser, LAMBDA_TOK_LAST);
            LambdaParseValue limit = parse_expression(parser, 0);
            if (!limit) return 0;
            clauses = parser_for_clause_append(parser, first.span, clauses,
                parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT,
                    LAMBDA_REDUCTION_FORM_FOR_LIMIT, first.span, last, (LambdaToken){0},
                    from_end ? LAMBDA_REDUCTION_FLAG_OPTIONAL : 0, &limit, 1));
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_OFFSET)) {
            LambdaParseValue offset = parse_expression(parser, 0);
            if (!offset) return 0;
            clauses = parser_for_clause_append(parser, first.span, clauses,
                parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
                    LAMBDA_REDUCTION_FORM_FOR_OFFSET, first.span, first, &offset, 1));
            continue;
        }
        break;
    }
    if (parenthesized && !parser_expect(parser, LAMBDA_TOK_RPAREN, "expected ')' after for clauses")) return 0;
    if (parser->current.kind == LAMBDA_TOK_LBRACE &&
            control_body_brace_is_map(parser)) {
        children[0] = parse_expression(parser, 0);
    } else if (parser_accept(parser, LAMBDA_TOK_LBRACE)) {
        children[0] = parse_content(parser, LAMBDA_TOK_RBRACE);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after for body")) return 0;
    } else if (parenthesized) {
        if (token_is_control_statement(parser->current.kind)) {
            return parser_fail(parser, "'return', 'break', and 'continue' are "
                "statements; an unbraced 'for' body is an expression - write "
                "'for (...) { ... }'", LAMBDA_TOK_LBRACE);
        }
        children[0] = parse_expression(parser, 0);
    } else {
        return parser_fail(parser, "expected '{' after for statement header", LAMBDA_TOK_LBRACE);
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    children[1] = children[0];
    children[0] = clauses;
    uint32_t flags = parenthesized ? 0 : LAMBDA_REDUCTION_FLAG_BODY_BLOCK;
    LambdaParseValue result = parser_reduce_tokens(parser, LAMBDA_REDUCE_FOR,
        LAMBDA_REDUCTION_FORM_NONE,
        (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
        first, (LambdaToken){0}, flags, children, 2);
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_FOR_END, first.span, first, NULL, 0);
    return result;
}

static LambdaParseValue parse_match_expression(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue arms = 0;
    uint32_t arm_count = 0;
    parser_advance(parser);
    LambdaParseValue scrutinee = parse_expression(parser, 0);
    if (parser->status != LAMBDA_PARSE_OK ||
            !parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after match value")) return 0;
    while (parser->status == LAMBDA_PARSE_OK && parser->current.kind != LAMBDA_TOK_RBRACE) {
        if (parser->current.kind == LAMBDA_TOK_RBRACE) break;
        LambdaToken arm_first = parser->current;
        LambdaParseValue pattern = 0;
        if (parser_accept(parser, LAMBDA_TOK_CASE)) {
            LambdaToken pattern_first = parser->current;
            pattern = parse_type_slot(parser);
            if (!pattern) return 0;
            if (parser_accept(parser, LAMBDA_TOK_TO)) {
                if (!parse_expression(parser, 0)) return 0;
                SourceSpan span = {pattern_first.span.start_byte,
                    parser->current.span.start_byte};
                pattern = parser_reduce_token(parser, LAMBDA_REDUCE_TYPE_SLOT,
                    LAMBDA_REDUCTION_FORM_TOKEN, span, pattern_first, NULL, 0);
            }
            if (parser_accept(parser, LAMBDA_TOK_THAT)) {
                LambdaParseValue constraint = parse_expression(parser, 0);
                if (!constraint) return 0;
                LambdaParseValue pattern_children[2] = {pattern, constraint};
                SourceSpan span = {pattern_first.span.start_byte,
                    parser->current.span.start_byte};
                pattern = parser_reduce_tokens(parser, LAMBDA_REDUCE_TYPE_SLOT,
                    LAMBDA_REDUCTION_FORM_TOKEN, span, pattern_first, (LambdaToken){0},
                    LAMBDA_REDUCTION_FLAG_ANNOTATION_CONSTRAINT,
                    pattern_children, 2);
            }
        } else if (!parser_accept(parser, LAMBDA_TOK_DEFAULT)) {
            return parser_fail(parser, "expected case or default in match", LAMBDA_TOK_CASE);
        }
        parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_MATCH_ARM_BEGIN, arm_first.span, arm_first,
            NULL, 0);
        LambdaParseValue body = 0;
        bool arm_body_braced = false;
        if (parser_accept(parser, LAMBDA_TOK_COLON)) {
            if (token_is_control_statement(parser->current.kind)) {
                return parser_fail(parser, "'return', 'break', and 'continue' are "
                    "statements; a 'case T:' arm takes an expression - write "
                    "'case T { ... }'", LAMBDA_TOK_LBRACE);
            }
            body = parse_expression(parser, 0);
        } else if (parser_accept(parser, LAMBDA_TOK_LBRACE)) {
            arm_body_braced = true;
            body = parse_content(parser, LAMBDA_TOK_RBRACE);
            if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after match arm")) return 0;
        } else {
            return parser_fail(parser, "expected ':' or '{' after match arm", LAMBDA_TOK_COLON);
        }
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_MATCH_ARM_END, arm_first.span, arm_first,
            NULL, 0);
        LambdaParseValue arm_children[2];
        uint32_t arm_child_count = 0;
        if (pattern) arm_children[arm_child_count++] = pattern;
        arm_children[arm_child_count++] = body;
        LambdaParseValue arm = parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT,
            LAMBDA_REDUCTION_FORM_MATCH_ARM,
            (SourceSpan){arm_first.span.start_byte, parser->current.span.start_byte},
            arm_first, (LambdaToken){0},
            arm_body_braced ? LAMBDA_REDUCTION_FLAG_BODY_BLOCK : 0u,
            arm_children, arm_child_count);
        arms = parser_list_append(parser,
            (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, arms, arm);
        arm_count++;
    }
    if (!arm_count) {
            return parser_fail(parser, "expected case or default in match", LAMBDA_TOK_CASE);
    }
    if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after match arms")) return 0;
    LambdaParseValue children[2] = {scrutinee, arms};
    return parser_reduce(parser, LAMBDA_REDUCE_MATCH,
        (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, children, 2);
}

static LambdaParseValue parse_while_statement(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_WHILE_BEGIN, first.span, first, NULL, 0);
    bool paren_condition = parser_accept(parser, LAMBDA_TOK_LPAREN);
    LambdaParseValue condition = parse_expression(parser, 0);
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    if (paren_condition &&
            !parser_expect(parser, LAMBDA_TOK_RPAREN, "expected ')' after while condition")) return 0;
    if (!parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after while condition")) return 0;
    LambdaParseValue body = parse_content(parser, LAMBDA_TOK_RBRACE);
    if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after while body")) return 0;
    LambdaParseValue children[2] = {condition, body};
    LambdaParseValue result = parser_reduce_tokens(parser, LAMBDA_REDUCE_FOR,
        LAMBDA_REDUCTION_FORM_FOR_WHILE,
        (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
        first, (LambdaToken){0}, 0, children, 2);
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_WHILE_END, first.span, first, NULL, 0);
    return result;
}

static LambdaParseValue parse_assignment_clause(LambdaRdParser* parser,
        const char* missing_name_message, const char* missing_equals_message) {
    LambdaToken first = parser->current;
    LambdaToken names[64];
    uint32_t name_count = 1;
    names[0] = first;
    if (!token_is_key(parser->current.kind)) {
        return parser_fail(parser, missing_name_message, LAMBDA_TOK_IDENTIFIER);
    }
    parser_advance(parser);
    LambdaParseValue type_value = 0;
    uint32_t flags = 0;
    if (parser_accept(parser, LAMBDA_TOK_COLON)) {
        if (!parse_annotation_type_slot_value(parser, &type_value)) return 0;
        flags |= LAMBDA_REDUCTION_FLAG_TYPED;
    }
    bool decomposed = false;
    while (parser_accept(parser, LAMBDA_TOK_COMMA)) {
        decomposed = true;
        if (!token_is_key(parser->current.kind)) {
            return parser_fail(parser, missing_name_message, LAMBDA_TOK_IDENTIFIER);
        }
        if (name_count == 64) {
        return parser_fail(parser, "too many decomposition names", LAMBDA_TOK_IDENTIFIER);
        }
        names[name_count++] = parser->current;
        parser_advance(parser);
    }
    bool named_decompose = false;
    if (!parser_accept(parser, LAMBDA_TOK_EQ)) {
        if (!decomposed || !parser_accept(parser, LAMBDA_TOK_AT)) {
            return parser_fail(parser, missing_equals_message, LAMBDA_TOK_EQ);
        }
        named_decompose = true;
    }
    LambdaParseValue value = parse_expression(parser, 0);
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    LambdaParseValue children[2];
    uint32_t child_count = 0;
    if (type_value) children[child_count++] = type_value;
    children[child_count++] = value;
    if (name_count > 1 || named_decompose) {
        return parser_reduce_name_tokens(parser, LAMBDA_REDUCE_LET, span,
            first, names, name_count,
            named_decompose ? LAMBDA_REDUCTION_FLAG_DECOMPOSE_NAMED : 0,
            children, child_count);
    }
    return parser_reduce_tokens(parser, LAMBDA_REDUCE_LET,
        LAMBDA_REDUCTION_FORM_TOKEN, span, first, (LambdaToken){0}, flags,
        children, child_count);
}
static LambdaParseValue parse_let_expression(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    LambdaParseValue value = parse_assignment_clause(parser,
        "expected a binding name after let", "expected '=' after let binding");
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    return parser_reduce(parser, LAMBDA_REDUCE_LET,
        (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, &value, 1);
}

static LambdaParseValue parse_prefix(LambdaRdParser* parser) {
    if (!parser_enter(parser)) return 0;
    LambdaToken first = parser->current;
    LambdaParseValue value = 0;
    if (token_is_literal(first.kind) || token_is_identifier_like(first.kind) ||
            first.kind == LAMBDA_TOK_FN || first.kind == LAMBDA_TOK_LAST ||
            first.kind == LAMBDA_TOK_TILDE || first.kind == LAMBDA_TOK_TILDE_INDEX ||
            first.kind == LAMBDA_TOK_PARENT || first.kind == LAMBDA_TOK_CARET ||
            first.kind == LAMBDA_TOK_ELLIPSIS) {
        if (parser->pipe_rhs_depth &&
                (first.kind == LAMBDA_TOK_TILDE ||
                 first.kind == LAMBDA_TOK_TILDE_INDEX)) {
            parser->pipe_rhs_has_current = true;
        }
        parser_advance(parser);
        value = parser_reduce_token(parser, LAMBDA_REDUCE_ATOM,
            LAMBDA_REDUCTION_FORM_TOKEN, first.span, first, NULL, 0);
    } else if (first.kind == LAMBDA_TOK_LPAREN) {
        uint32_t saved_close = parser->stop_at_element_close;
        uint32_t saved_attr_close = parser->stop_at_element_attribute_close;
        parser->stop_at_element_close = 0;
        parser->stop_at_element_attribute_close = 0;
        value = parse_group_or_arrow(parser);
        parser->stop_at_element_close = saved_close;
        parser->stop_at_element_attribute_close = saved_attr_close;
    } else if (first.kind == LAMBDA_TOK_LBRACKET) {
        value = parse_array(parser);
    } else if (first.kind == LAMBDA_TOK_LBRACE) {
        if (braced_expression_is_map(parser) || parser->next.kind == LAMBDA_TOK_RBRACE) {
            value = parse_map(parser);
        } else {
            parser_advance(parser);
            value = parse_content(parser, LAMBDA_TOK_RBRACE);
            if (parser->status == LAMBDA_PARSE_OK &&
                    !parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after block")) {
                return 0;
            }
        }
    } else if (first.kind == LAMBDA_TOK_LT) {
        value = parse_element(parser);
    } else if (first.kind == LAMBDA_TOK_PATH_REL || first.kind == LAMBDA_TOK_SLASH) {
        value = parse_path_slot(parser);
    } else if (first.kind == LAMBDA_TOK_BANG) {
        // §7.1: `!x` used to mean type complement and silently produced a
        // TYPE where every C/JS habit expects negation.
        return parser_fail(parser, "'!' is not logical negation here; use 'not'",
            LAMBDA_TOK_NOT);
    } else if (first.kind == LAMBDA_TOK_NOT ||
            first.kind == LAMBDA_TOK_MINUS || first.kind == LAMBDA_TOK_PLUS ||
            first.kind == LAMBDA_TOK_STAR) {
        parser_advance(parser);
        LambdaParseValue child = parse_expression(parser,
            first.kind == LAMBDA_TOK_NOT ? LAMBDA_BP_MEMBERSHIP : LAMBDA_BP_PREFIX);
        if (parser->status == LAMBDA_PARSE_OK) {
            SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            value = parser_reduce_token(parser, LAMBDA_REDUCE_PREFIX,
                LAMBDA_REDUCTION_FORM_TOKEN, span, first, &child, 1);
        }
    } else if (first.kind == LAMBDA_TOK_LET) {
        value = parse_let_expression(parser);
    } else if (first.kind == LAMBDA_TOK_IF) {
        value = parse_if_expression(parser);
    } else if (first.kind == LAMBDA_TOK_FOR) {
        value = parse_for_expression(parser);
    } else if (first.kind == LAMBDA_TOK_MATCH) {
        value = parse_match_expression(parser);
    } else if (first.kind == LAMBDA_TOK_RAISE) {
        parser_advance(parser);
        LambdaParseValue child = parse_expression(parser, 0);
        if (parser->status == LAMBDA_PARSE_OK) {
            SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            value = parser_reduce_token(parser, LAMBDA_REDUCE_PREFIX,
                LAMBDA_REDUCTION_FORM_TOKEN, span, first, &child, 1);
        }
    } else {
        parser_set_error(parser, "expected an expression", LAMBDA_TOK_IDENTIFIER);
    }
    parser_leave(parser);
    return value;
}

static int infix_binding_power(LambdaRdParser* parser, LambdaTokenKind kind,
        bool* right_associative) {
    *right_associative = false;
    if (parser->stop_at_element_close &&
            (kind == LAMBDA_TOK_GT || kind == LAMBDA_TOK_LT ||
             kind == LAMBDA_TOK_GT_EQ || kind == LAMBDA_TOK_LT_EQ)) return -1;
    switch (kind) {
    case LAMBDA_TOK_PIPE_FORWARD:
    case LAMBDA_TOK_THAT: return LAMBDA_BP_PIPE;
    case LAMBDA_TOK_OR: return LAMBDA_BP_OR;
    case LAMBDA_TOK_AND: return LAMBDA_BP_AND;
    case LAMBDA_TOK_IS:
    case LAMBDA_TOK_IN:
    case LAMBDA_TOK_AT: return LAMBDA_BP_MEMBERSHIP;
    case LAMBDA_TOK_PIPE:
    case LAMBDA_TOK_AMPERSAND:
    case LAMBDA_TOK_BANG: return LAMBDA_BP_SET;
    case LAMBDA_TOK_TO: return LAMBDA_BP_SET;
    case LAMBDA_TOK_EQ_EQ:
    case LAMBDA_TOK_BANG_EQ:
    case LAMBDA_TOK_EQ_WORD:
    case LAMBDA_TOK_NE_WORD: return LAMBDA_BP_EQUALITY;
    case LAMBDA_TOK_LT:
    case LAMBDA_TOK_LT_EQ:
    case LAMBDA_TOK_GT:
    case LAMBDA_TOK_GT_EQ:
    case LAMBDA_TOK_LT_WORD:
    case LAMBDA_TOK_LE_WORD:
    case LAMBDA_TOK_GE_WORD:
    case LAMBDA_TOK_GT_WORD: return LAMBDA_BP_RELATION;
    case LAMBDA_TOK_PLUS:
    case LAMBDA_TOK_PLUS_PLUS:
    case LAMBDA_TOK_MINUS: return LAMBDA_BP_ADD;
    case LAMBDA_TOK_STAR:
    case LAMBDA_TOK_SLASH:
    case LAMBDA_TOK_DIV:
    case LAMBDA_TOK_PERCENT: return LAMBDA_BP_MULTIPLY;
    case LAMBDA_TOK_STAR_STAR:
        *right_associative = true;
        return LAMBDA_BP_POWER;
    default: return -1;
    }
}
static LambdaParseValue parse_call_argument(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (token_is_key(first.kind) && parser->next.kind == LAMBDA_TOK_COLON) {
        parser_advance(parser);
        parser_advance(parser);
        LambdaParseValue value = parse_expression(parser, 0);
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        return parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
            LAMBDA_REDUCTION_FORM_NAMED_ARGUMENT,
            (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
            first, &value, 1);
    }
    return parse_expression(parser, 0);
}

static LambdaParseValue parse_postfix(LambdaRdParser* parser,
        LambdaParseValue left, uint32_t left_start_byte) {
    for (;;) {
        LambdaToken first = parser->current;
        // S16.2.3: `(`, `[`, `.`, and `^` all open a postfix form AND can
        // begin a new statement, so across a line break none of them may
        // continue this expression. The one exception is S16.2.4's `.ident(`
        // member call, which no path body or float literal can spell.
        if (first.nl_before && token_is_dual_role(first.kind)) {
            // S16.2.4v2 (§7.15): with the relative path respelled `\.`, a
            // line-start `.ident` has no start reading left, so member access
            // continues across the break for ANY member — full leading-dot
            // fluent chains, not just the `.ident(` call form. `.digit` stays
            // dual-role: `a.5` is an integer member field, `.5` is a float.
            //
            // The admitted set must be the one `parse_path_segment` accepts for
            // a member name, minus the spellings that keep a start reading.
            // Testing LAMBDA_TOK_IDENTIFIER alone desynced the two: a member
            // whose name is a type keyword (`.map(`, `.int(`, `.string(`) lexes
            // as LAMBDA_TOK_BASE_TYPE, so the guard rejected the very chains the
            // member parser would then have accepted on one line (LR02-11).
            // `token_is_key` is that shared set; INTEGER/SLASH/PARENT/STAR_STAR
            // stay out because each still has a non-member reading at line start.
            bool member_chain = first.kind == LAMBDA_TOK_DOT &&
                token_is_key(parser->next.kind);
            if (!member_chain) return left;
        }
        LambdaParseValue children[65] = {0};
        children[0] = left;
        if (parser_accept(parser, LAMBDA_TOK_LPAREN)) {
            uint32_t argument_count = 0;
            if (!parser_parse_expression_list(parser, LAMBDA_TOK_RPAREN,
                    children + 1, 64, &argument_count, true, parse_call_argument,
                    "too many call arguments in parser POC",
                    "expected ')' after arguments")) return 0;
            SourceSpan span = {left_start_byte, parser->current.span.start_byte};
            uint32_t call_flags = parser->pipe_rhs_depth == parser->expression_depth &&
                !parser->pipe_rhs_has_current
                ? LAMBDA_REDUCTION_FLAG_PIPE_INJECT : 0u;
            left = parser_reduce_tokens(parser, LAMBDA_REDUCE_POSTFIX,
                LAMBDA_REDUCTION_FORM_CALL, span, first, (LambdaToken){0},
                call_flags, children, argument_count + 1);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_LBRACKET)) {
            uint32_t index_count = 0;
            if (!parser_parse_expression_list(parser, LAMBDA_TOK_RBRACKET,
                    children + 1, 64, &index_count, false,
                    parser_parse_expression_item,
                    "too many index dimensions in parser POC",
                    "expected ']' after index")) return 0;
            SourceSpan span = {left_start_byte, parser->current.span.start_byte};
            left = parser_reduce_token(parser, LAMBDA_REDUCE_POSTFIX,
                LAMBDA_REDUCTION_FORM_INDEX, span, first, children,
                index_count + 1);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_DOT)) {
            LambdaToken field = parser->current;
            if (!parse_path_segment(parser)) return 0;
            SourceSpan span = {left_start_byte, parser->current.span.start_byte};
            left = parser_reduce_token(parser, LAMBDA_REDUCE_POSTFIX,
                LAMBDA_REDUCTION_FORM_MEMBER, span, field, children, 1);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_QUESTION) ||
                parser_accept(parser, LAMBDA_TOK_DOT_QUESTION)) {
            children[1] = parse_primary_type_slot(parser);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            SourceSpan span = {left_start_byte, parser->current.span.start_byte};
            left = parser_reduce_token(parser, LAMBDA_REDUCE_POSTFIX,
                LAMBDA_REDUCTION_FORM_QUERY, span, first, children, 2);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_CARET)) {
            uint32_t handler_child_count = 1;
            if (parser->current.kind == LAMBDA_TOK_LBRACE && parser->current.nl_before) {
                return parser_fail(parser,
                    "a handler body must open on the same line as its '^'",
                    LAMBDA_TOK_LBRACE);
            }
            bool handler = parser_accept(parser, LAMBDA_TOK_LBRACE);
            if (handler) {
                parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
                    LAMBDA_REDUCTION_FORM_HANDLER_BEGIN,
                    first.span, first, NULL, 0);
                children[1] = parse_content(parser, LAMBDA_TOK_RBRACE);
                parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
                    LAMBDA_REDUCTION_FORM_HANDLER_END,
                    first.span, first, NULL, 0);
                if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after handler body")) return 0;
                handler_child_count = 2;
                if (parser_accept(parser, LAMBDA_TOK_TILDE)) {
                    if (!parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after handler value marker")) return 0;
                    children[2] = parse_content(parser, LAMBDA_TOK_RBRACE);
                    if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after handler value")) return 0;
                    handler_child_count = 3;
                }
            }
            SourceSpan span = {left_start_byte, parser->current.span.start_byte};
            left = parser_reduce_token(parser, LAMBDA_REDUCE_POSTFIX,
                handler ? LAMBDA_REDUCTION_FORM_HANDLER :
                    LAMBDA_REDUCTION_FORM_PROPAGATE, span, first, children,
                handler_child_count);
            continue;
        }
        return left;
    }
}

static LambdaParseValue parse_expression(LambdaRdParser* parser, int min_bp) {
    LambdaToken first = parser->current;
    parser->expression_depth++;
    bool left_is_element = parser->current.kind == LAMBDA_TOK_LT;
    LambdaParseValue left = parse_prefix(parser);
    if (parser->status != LAMBDA_PARSE_OK) {
        parser->expression_depth--;
        return 0;
    }
    left = parse_postfix(parser, left, first.span.start_byte);
    while (parser->status == LAMBDA_PARSE_OK) {
        if (parser->current.nl_before && token_is_dual_role(parser->current.kind)) {
            break;
        }
        if (left_is_element && parser->stop_at_element_close &&
                parser->current.kind == LAMBDA_TOK_LT) {
            break;
        }
        bool right_associative = false;
        int bp = infix_binding_power(parser, parser->current.kind, &right_associative);
        if (bp < min_bp) break;
        LambdaToken op = parser->current;
        parser_advance(parser);
        uint32_t prior_pipe_rhs_depth = parser->pipe_rhs_depth;
        bool prior_pipe_rhs_has_current = parser->pipe_rhs_has_current;
        if (op.kind == LAMBDA_TOK_PIPE_FORWARD) {
            parser->pipe_rhs_depth = parser->expression_depth + 1;
            parser->pipe_rhs_has_current = false;
        }
        if (op.kind == LAMBDA_TOK_THAT) {
            parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
                LAMBDA_REDUCTION_FORM_THAT_BEGIN, op.span, op, NULL, 0);
        }
        bool is_named_value_rhs = op.kind == LAMBDA_TOK_IS &&
            parser->current.kind == LAMBDA_TOK_NAMED_VALUE;
        LambdaParseValue right = op.kind == LAMBDA_TOK_IS && !is_named_value_rhs
            ? parse_type_slot(parser)
            : parse_expression(parser, bp + (right_associative ? 0 : 1));
        parser->pipe_rhs_depth = prior_pipe_rhs_depth;
        bool rhs_has_current = parser->pipe_rhs_has_current;
        parser->pipe_rhs_has_current = prior_pipe_rhs_has_current || rhs_has_current;
        if (op.kind == LAMBDA_TOK_THAT) {
            parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
                LAMBDA_REDUCTION_FORM_THAT_END, op.span, op, NULL, 0);
        }
        if (parser->status != LAMBDA_PARSE_OK) {
            parser->expression_depth--;
            return 0;
        }
        if (op.kind == LAMBDA_TOK_LT && parser->expression_depth == 1) {
            parser->top_level_statement_relation = true;
        }
        LambdaParseValue children[2] = {left, right};
        SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        left = parser_reduce_token(parser, LAMBDA_REDUCE_BINARY,
            LAMBDA_REDUCTION_FORM_TOKEN, span, op, children, 2);
        left_is_element = false;
    }
    parser->expression_depth--;
    return left;
}
static bool if_starts_block_statement(const LambdaRdParser* parser) {
    LambdaRdParser probe = *parser;
    parser_prepare_probe(&probe);
    parser_advance(&probe);
    (void)parse_expression(&probe, 0);
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind == LAMBDA_TOK_LBRACE;
}
static bool parse_parameter_list(LambdaRdParser* parser) {
    parser->last_parameter_list = 0;
    parser->last_parameter_variadic = false;
    if (!parser_expect(parser, LAMBDA_TOK_LPAREN, "expected '(' before parameters")) return false;
    return parse_parameter_items(parser, true, "expected a parameter name",
        "expected ')' after parameters", &parser->last_parameter_list,
        &parser->last_parameter_variadic);
}

static LambdaParseValue parse_function_declaration(LambdaRdParser* parser,
        bool is_public) {
    LambdaToken first = parser->current;
    bool is_proc = first.kind == LAMBDA_TOK_PN;
    parser_advance(parser);
    if (!token_is_key(parser->current.kind)) {
        return parser_fail(parser, "expected a function name", LAMBDA_TOK_IDENTIFIER);
    }
    LambdaToken name = parser->current;
    parser_advance(parser);
    uint32_t function_flags = is_proc ? LAMBDA_REDUCTION_FLAG_PROC : 0u;
    if (is_public) function_flags |= LAMBDA_REDUCTION_FLAG_PUBLIC;
    parser_reduce_tokens(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_FUNCTION_BEGIN,
        (SourceSpan){first.span.start_byte, name.span.end_byte},
        first, name, function_flags, NULL, 0);
    if (!parse_parameter_list(parser)) return 0;
    LambdaParseValue children[5];
    uint32_t child_count = 0;
    if (parser->last_parameter_list) children[child_count++] = parser->last_parameter_list;
    bool raised = false;
    if (token_starts_return_type(parser->current.kind)) {
        LambdaParseValue returned = parse_type_slot(parser);
        if (!returned) return 0;
        children[child_count++] = returned;
        if (parser_accept(parser, LAMBDA_TOK_CARET)) {
            raised = true;
            if (token_starts_return_type(parser->current.kind)) {
                LambdaParseValue error_type = parse_type_slot(parser);
                if (!error_type) return 0;
                children[child_count++] = error_type;
            }
        }
    }
    LambdaParseValue child = 0;
    uint32_t flags = function_flags;
    if (parser->last_parameter_variadic) flags |= LAMBDA_REDUCTION_FLAG_VARIADIC;
    if (raised) flags |= LAMBDA_REDUCTION_FLAG_RAISED;
    uint32_t saved_depth = parser->procedural_depth;
    if (is_proc && parser->current.kind == LAMBDA_TOK_ARROW) {
        return parser_fail(parser, "a procedure body is a statement block - write "
            "'pn name() { ... }'; '=>' bodies are fn-only", LAMBDA_TOK_LBRACE);
    }
    if (parser_accept(parser, LAMBDA_TOK_ARROW)) {
        if (token_is_control_statement(parser->current.kind)) {
            return parser_fail(parser, "'return', 'break', and 'continue' are "
                "statements; an arrow '=>' body is an expression", LAMBDA_TOK_LBRACE);
        }
        parser->procedural_depth = is_proc ? saved_depth + 1 : 0;
        child = parse_expression(parser, 0);
        parser->procedural_depth = saved_depth;
    } else if (parser_accept(parser, LAMBDA_TOK_LBRACE)) {
        flags |= LAMBDA_REDUCTION_FLAG_BODY_BLOCK;
        parser->procedural_depth = is_proc ? saved_depth + 1 : 0;
        child = parse_content(parser, LAMBDA_TOK_RBRACE);
        parser->procedural_depth = saved_depth;
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after function body")) return 0;
    } else {
        return parser_fail(parser, "expected a function body", LAMBDA_TOK_LBRACE);
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    children[child_count++] = child;
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    LambdaParseValue result = parser_reduce_tokens(parser, LAMBDA_REDUCE_FUNCTION,
        LAMBDA_REDUCTION_FORM_FUNCTION, span, first, name, flags,
        children, child_count);
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_FUNCTION_END, first.span, first, NULL, 0);
    return result;
}

static LambdaParseValue parse_view_declaration(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    LambdaToken name = {0};
    if (parser->current.kind == LAMBDA_TOK_IDENTIFIER && parser->next.kind == LAMBDA_TOK_COLON) {
        name = parser->current;
        parser_advance(parser);
        parser_advance(parser);
    }
    LambdaParseValue pattern = parse_type_slot(parser);
    if (!pattern) return 0;
    parser_reduce_tokens(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_VIEW_BEGIN,
        (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
        first, name, 0, &pattern, 1);
    LambdaParseValue parameters = 0;
    if (parser->current.kind == LAMBDA_TOK_LPAREN) {
        if (!parse_parameter_list(parser)) return 0;
        parameters = parser->last_parameter_list;
    }
    if (token_starts_return_type(parser->current.kind)) {
        if (!parse_type_slot(parser)) return 0;
        if (parser_accept(parser, LAMBDA_TOK_CARET) && token_starts_return_type(parser->current.kind) &&
                !parse_type_slot(parser)) return 0;
    }
    if (parser_accept(parser, LAMBDA_TOK_STATE)) {
        do {
            if (parser->current.kind != LAMBDA_TOK_IDENTIFIER) {
                return parser_fail(parser, "expected a state name", LAMBDA_TOK_IDENTIFIER);
            }
            LambdaToken state_name = parser->current;
            parser_advance(parser);
            LambdaParseValue value = 0;
            if (parser_accept(parser, LAMBDA_TOK_COLON)) {
                value = parse_expression(parser, 0);
                if (!value) return 0;
            }
            parser_reduce_tokens(parser, LAMBDA_REDUCE_VIEW,
                LAMBDA_REDUCTION_FORM_VIEW_STATE,
                (SourceSpan){state_name.span.start_byte, parser->current.span.start_byte},
                state_name, (LambdaToken){0}, 0,
                value ? &value : NULL, value ? 1 : 0);
        } while (parser_accept(parser, LAMBDA_TOK_COMMA));
    }
    if (!parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after view declaration")) return 0;
    LambdaParseValue body = parse_content(parser, LAMBDA_TOK_RBRACE);
    if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after view body")) return 0;
    while (parser->current.kind == LAMBDA_TOK_ON) {
        LambdaToken on = parser->current;
        parser_advance(parser);
        if (parser->current.kind != LAMBDA_TOK_IDENTIFIER) {
            return parser_fail(parser, "expected an event name", LAMBDA_TOK_IDENTIFIER);
        }
        LambdaToken event = parser->current;
        parser_advance(parser);
        parser_reduce_tokens(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_VIEW_HANDLER_BEGIN,
            (SourceSpan){on.span.start_byte, parser->current.span.start_byte},
            event, (LambdaToken){0}, 0, NULL, 0);
        if (!parse_parameter_list(parser)) return 0;
        LambdaParseValue handler_parameters = parser->last_parameter_list;
        if (!parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after event parameters")) return 0;
        LambdaParseValue handler_body = parse_content(parser, LAMBDA_TOK_RBRACE);
        if (!handler_body) return 0;
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after event body")) return 0;
        LambdaParseValue handler_children[2] = {
            handler_parameters,
            handler_body,
        };
        parser_reduce_tokens(parser, LAMBDA_REDUCE_VIEW,
            LAMBDA_REDUCTION_FORM_VIEW_HANDLER,
            (SourceSpan){on.span.start_byte, parser->current.span.start_byte},
            event, (LambdaToken){0}, 0, handler_children, 2);
        parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_VIEW_HANDLER_END,
            (SourceSpan){on.span.start_byte, parser->current.span.start_byte},
            event, NULL, 0);
    }
    LambdaParseValue view_children[2] = {parameters, body};
    LambdaParseValue result = parser_reduce_tokens(parser, LAMBDA_REDUCE_VIEW,
        LAMBDA_REDUCTION_FORM_VIEW,
        (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
        first, name, 0, view_children, 2);
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_VIEW_END,
        (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
        first, NULL, 0);
    return result;
}

static LambdaParseValue parse_type_declaration(LambdaRdParser* parser,
        bool is_public) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    if (!token_is_key(parser->current.kind)) {
        return parser_fail(parser, "expected a type name", LAMBDA_TOK_IDENTIFIER);
    }
    LambdaToken name = parser->current;
    parser_advance(parser);
    if (parser_accept(parser, LAMBDA_TOK_EQ)) {
        if (parser->current.kind != LAMBDA_TOK_PATTERN_ISLAND) {
            parser_reduce_tokens(parser, LAMBDA_REDUCE_CONTEXT,
                LAMBDA_REDUCTION_FORM_TYPE_ALIAS_BEGIN, name.span, name,
                (LambdaToken){0},
                is_public ? LAMBDA_REDUCTION_FLAG_PUBLIC : 0u, NULL, 0);
        }
        LambdaParseValue type_value = 0;
        if (!parse_annotation_type_slot_value(parser, &type_value)) return 0;
        LambdaParseValue declarations = parser_reduce_tokens(parser,
            LAMBDA_REDUCE_DECLARATION, LAMBDA_REDUCTION_FORM_TYPE_ALIAS,
            (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
            name, (LambdaToken){0},
            is_public ? LAMBDA_REDUCTION_FLAG_PUBLIC : 0u, &type_value, 1);
        while (parser_accept(parser, LAMBDA_TOK_COMMA)) {
            if (!token_is_key(parser->current.kind)) {
                return parser_fail(parser, "expected a type alias name", LAMBDA_TOK_IDENTIFIER);
            }
            name = parser->current;
            parser_advance(parser);
            if (!parser_expect(parser, LAMBDA_TOK_EQ, "expected '=' after type alias name")) return 0;
            if (parser->current.kind != LAMBDA_TOK_PATTERN_ISLAND) {
                parser_reduce_tokens(parser, LAMBDA_REDUCE_CONTEXT,
                    LAMBDA_REDUCTION_FORM_TYPE_ALIAS_BEGIN, name.span, name,
                    (LambdaToken){0},
                    is_public ? LAMBDA_REDUCTION_FLAG_PUBLIC : 0u, NULL, 0);
            }
            type_value = 0;
            if (!parse_annotation_type_slot_value(parser, &type_value)) return 0;
            LambdaParseValue alias = parser_reduce_tokens(parser,
                LAMBDA_REDUCE_DECLARATION, LAMBDA_REDUCTION_FORM_TYPE_ALIAS,
                (SourceSpan){name.span.start_byte, parser->current.span.start_byte},
                name, (LambdaToken){0},
                is_public ? LAMBDA_REDUCTION_FLAG_PUBLIC : 0u, &type_value, 1);
            declarations = parser_list_append(parser,
                (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
                declarations, alias);
        }
        return declarations;
    } else {
        LambdaToken base = {0};
        if (parser_accept(parser, LAMBDA_TOK_COLON)) {
            if (!token_is_key(parser->current.kind)) {
                return parser_fail(parser, "expected an inherited type name", LAMBDA_TOK_IDENTIFIER);
            }
            base = parser->current;
            parser_advance(parser);
        }
        parser_reduce_tokens(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_TYPE_OBJECT_BEGIN, first.span, base,
            name, is_public ? LAMBDA_REDUCTION_FLAG_PUBLIC : 0u, NULL, 0);
        if (!parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after object type name")) return 0;
        while (parser->status == LAMBDA_PARSE_OK && parser->current.kind != LAMBDA_TOK_RBRACE) {
            while (parser_accept(parser, LAMBDA_TOK_COMMA)) {}
            if (parser->current.kind == LAMBDA_TOK_RBRACE) break;
            if (parser->current.kind == LAMBDA_TOK_SEMICOLON) {
                return parser_fail(parser,
                    "object-type members are separated by ',', not ';'",
                    LAMBDA_TOK_COMMA);
            }
            if (parser->current.kind == LAMBDA_TOK_FN || parser->current.kind == LAMBDA_TOK_PN) {
                if (!parse_function_declaration(parser, false)) return 0;
                continue;
            }
            if (parser->current.kind == LAMBDA_TOK_THAT) {
                LambdaToken that = parser->current;
                parser_advance(parser);
                parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
                    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT_BEGIN,
                    that.span, that, NULL, 0);
                LambdaParseValue constraint = parse_expression(parser, 0);
                if (!constraint) return 0;
                parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
                    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT_END,
                    that.span, that, NULL, 0);
                parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
                    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT,
                    (SourceSpan){that.span.start_byte, parser->current.span.start_byte},
                    that, &constraint, 1);
                continue;
            }
            bool optional_field = false;
            if (parser->next.kind == LAMBDA_TOK_QUESTION) {
                size_t at = parser->next.span.end_byte;
                while (at < parser->lexer.length &&
                        (parser->lexer.source[at] == ' ' ||
                         parser->lexer.source[at] == '\t')) { at++; }
                optional_field = at < parser->lexer.length &&
                    parser->lexer.source[at] == ':';
            }
            if (token_is_key(parser->current.kind) &&
                    (parser->next.kind == LAMBDA_TOK_COLON || optional_field)) {
                LambdaToken field = parser->current;
                parser_advance(parser);
                uint32_t field_flags = 0;
                if (parser_accept(parser, LAMBDA_TOK_QUESTION)) {
                    field_flags |= LAMBDA_REDUCTION_FLAG_OPTIONAL;
                }
                if (!parser_expect(parser, LAMBDA_TOK_COLON,
                        "expected ':' after object-type field name")) return 0;
                LambdaParseValue type_value = 0;
                if (!parse_annotation_type_slot_value(parser, &type_value)) return 0;
                LambdaParseValue children[2] = {type_value, 0};
                uint32_t child_count = 1;
                if (parser_accept(parser, LAMBDA_TOK_EQ)) {
                    children[child_count++] = parse_expression(parser, 0);
                    if (!children[child_count - 1]) return 0;
                }
                parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT,
                    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_FIELD,
                    (SourceSpan){field.span.start_byte, parser->current.span.start_byte},
                    field, (LambdaToken){0}, field_flags, children, child_count);
                continue;
            }
            if (token_starts_type(parser->current.kind)) {
                LambdaToken content = parser->current;
                LambdaParseValue content_type = parse_type_slot(parser);
                if (!content_type) return 0;
                parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
                    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONTENT,
                    (SourceSpan){content.span.start_byte, parser->current.span.start_byte},
                    content, &content_type, 1);
                continue;
            }
            return parser_fail(parser, "expected an object type field, content type, or method",
                LAMBDA_TOK_IDENTIFIER);
        }
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after object type body")) return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    if (first.kind == LAMBDA_TOK_TYPE && parser->current.kind != LAMBDA_TOK_EQ) {
        parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_TYPE_OBJECT_END, first.span, first, NULL, 0);
    }
    return parser_reduce_tokens(parser, LAMBDA_REDUCE_DECLARATION,
        LAMBDA_REDUCTION_FORM_TYPE_OBJECT, span, name, (LambdaToken){0},
        is_public ? LAMBDA_REDUCTION_FLAG_PUBLIC : 0u, NULL, 0);
}
static LambdaParseValue parse_var_statement(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    LambdaParseValue declarations = 0;
    do {
        LambdaParseValue declaration = parse_assignment_clause(parser,
            "expected a mutable binding name", "expected '=' after mutable binding");
        if (!declaration) return 0;
        declarations = parser_list_append(parser,
            (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
            declarations, declaration);
    } while (parser_accept(parser, LAMBDA_TOK_COMMA));
    return parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
        LAMBDA_REDUCTION_FORM_VAR,
        (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
        first, &declarations, 1);
}
static bool if_statement_body_is_map(const LambdaRdParser* parser) {
    LambdaRdParser probe = *parser;
    parser_prepare_probe(&probe);
    parser_advance(&probe);
    (void)parse_expression(&probe, 0);
    if (probe.status != LAMBDA_PARSE_OK) return false;
    return control_body_brace_is_map(&probe);
}
static LambdaParseValue parse_if_statement(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (if_statement_body_is_map(parser)) {
        return parse_if_expression(parser);
    }
    parser_advance(parser);
    LambdaParseValue condition = parse_expression(parser, 0);
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    LambdaParseValue body = 0;
    if (!parser_parse_content_block(parser, first,
            LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN,
            LAMBDA_REDUCTION_FORM_IF_BRANCH_END,
            "expected '}' after if body", &body)) return 0;
    LambdaParseValue children[3] = {condition, body, 0};
    uint32_t child_count = 2;
    if (parser_accept(parser, LAMBDA_TOK_ELSE)) {
        if (parser->current.kind == LAMBDA_TOK_IF) {
            children[2] = if_starts_block_statement(parser)
                ? parse_if_statement(parser)
                : parse_if_expression(parser);
        } else if (parser->current.kind == LAMBDA_TOK_LBRACE) {
            if (!parser_parse_content_block(parser, first,
                    LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN,
                    LAMBDA_REDUCTION_FORM_IF_BRANCH_END,
                    "expected '}' after else body", &children[2])) return 0;
        } else {
            if (token_is_control_statement(parser->current.kind)) {
                return parser_fail(parser, "'return', 'break', and 'continue' are "
                    "statements; an unbraced 'else' body is an expression - write "
                    "'else { ... }'", LAMBDA_TOK_LBRACE);
            }
            children[2] = parse_expression(parser, 0);
        }
        child_count = 3;
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    parser->last_statement_self_delimiting = true;
    return parser_reduce(parser, LAMBDA_REDUCE_IF, span, children, child_count);
}

static LambdaParseValue parse_statement(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser->last_statement_self_delimiting = false;
    parser->last_statement_assignment = false;
    if (parser_accept(parser, LAMBDA_TOK_IMPORT)) {
        LambdaParseValue imports = 0;
        do {
            LambdaToken alias = {0};
            if (token_is_key(parser->current.kind) && parser->next.kind == LAMBDA_TOK_COLON) {
                alias = parser->current;
                parser_advance(parser);
                parser_advance(parser);
            }
            LambdaToken module_first = parser->current;
            LambdaToken module_last = parser->current;
            if (parser->current.kind == LAMBDA_TOK_DOT ||
                    parser->current.kind == LAMBDA_TOK_SLASH) {
                module_first = parser->current;
                parser_advance(parser);
                if (!token_is_key(parser->current.kind)) {
                    return parser_fail(parser, "expected a relative import component", LAMBDA_TOK_IDENTIFIER);
                }
                module_last = parser->current;
                parser_advance(parser);
            } else if (token_is_key(parser->current.kind)) {
                module_first = parser->current;
                module_last = parser->current;
                parser_advance(parser);
            } else {
                return parser_fail(parser, "expected an import module", LAMBDA_TOK_IDENTIFIER);
            }
            while (parser->current.kind == LAMBDA_TOK_DOT ||
                    parser->current.kind == LAMBDA_TOK_SLASH) {
                parser_advance(parser);
                if (!token_is_key(parser->current.kind)) {
                    return parser_fail(parser, "expected an import component", LAMBDA_TOK_IDENTIFIER);
                }
                module_last = parser->current;
                parser_advance(parser);
            }
            LambdaToken module = module_first;
            module.kind = LAMBDA_TOK_IDENTIFIER;
            module.span.end_byte = module_last.span.end_byte;
            LambdaParseValue item = parser_reduce_tokens(parser,
                LAMBDA_REDUCE_DECLARATION, LAMBDA_REDUCTION_FORM_IMPORT,
                (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
                alias, module, 0, NULL, 0);
            imports = parser_list_append(parser,
                (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
                imports, item);
        } while (parser_accept(parser, LAMBDA_TOK_COMMA));
        return imports;
    } else {
        bool is_public = parser_accept(parser, LAMBDA_TOK_PUB);
        if (is_public && parser->current.kind == LAMBDA_TOK_IDENTIFIER) {
            return parser_fail(parser, "'pub' modifies a declaration; write 'pub let'",
                LAMBDA_TOK_LET);
        }
        if (parser->current.kind == LAMBDA_TOK_FN || parser->current.kind == LAMBDA_TOK_PN) {
            return parse_function_declaration(parser, is_public);
        }
        if (parser->current.kind == LAMBDA_TOK_VIEW || parser->current.kind == LAMBDA_TOK_EDIT) {
            return parse_view_declaration(parser);
        }
        if (parser->current.kind == LAMBDA_TOK_TYPE &&
                parser->next.kind != LAMBDA_TOK_LPAREN &&
                !parser->next.nl_before &&
                parser->next.kind != LAMBDA_TOK_SEMICOLON &&
                parser->next.kind != LAMBDA_TOK_EOF) {
            return parse_type_declaration(parser, is_public);
        }
        if (parser->current.kind == LAMBDA_TOK_VAR) {
            return parse_var_statement(parser);
        }
        if (parser->current.kind == LAMBDA_TOK_APPLY && parser->next.kind == LAMBDA_TOK_SEMICOLON) {
            parser_advance(parser);
            return parser_reduce(parser, LAMBDA_REDUCE_STATEMENT,
                (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, NULL, 0);
        }
        if (parser->current.kind == LAMBDA_TOK_IF && if_starts_block_statement(parser)) {
            return parse_if_statement(parser);
        }
        if (parser->current.kind == LAMBDA_TOK_WHILE) {
            return parse_while_statement(parser);
        }
        if (parser->current.kind == LAMBDA_TOK_LET) {
            LambdaParseValue let_value = parse_let_expression(parser);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            LambdaParseValue bindings = let_value;
            while (parser_accept(parser, LAMBDA_TOK_COMMA)) {
                LambdaParseValue binding = parse_assignment_clause(parser,
                    "expected a binding name after let",
                    "expected '=' after let binding");
                if (!binding) return 0;
                bindings = parser_list_append(parser,
                    (SourceSpan){first.span.start_byte,
                        parser->current.span.start_byte}, bindings, binding);
            }
            SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            if (is_public) {
                return parser_reduce_tokens(parser, LAMBDA_REDUCE_DECLARATION,
                    LAMBDA_REDUCTION_FORM_NONE, span, first, (LambdaToken){0},
                    LAMBDA_REDUCTION_FLAG_PUBLIC, &bindings, 1);
            }
            return parser_reduce(parser, LAMBDA_REDUCE_STATEMENT, span, &bindings, 1);
        }
        if (is_public) {
            LambdaParseValue public_value = parse_assignment_clause(parser,
                "expected a public binding name", "expected '=' after public binding");
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            while (parser_accept(parser, LAMBDA_TOK_COMMA)) {
                if (!parse_assignment_clause(parser, "expected a public binding name",
                        "expected '=' after public binding")) return 0;
            }
            return parser_reduce_tokens(parser, LAMBDA_REDUCE_DECLARATION,
                LAMBDA_REDUCTION_FORM_NONE,
                (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
                first, (LambdaToken){0}, LAMBDA_REDUCTION_FLAG_PUBLIC,
                &public_value, 1);
        }
        if (parser->current.kind == LAMBDA_TOK_RETURN || parser->current.kind == LAMBDA_TOK_BREAK ||
                parser->current.kind == LAMBDA_TOK_CONTINUE) {
            LambdaToken control = parser->current;
            parser_advance(parser);
            LambdaParseValue value = 0;
            if (control.kind == LAMBDA_TOK_RETURN &&
                    token_starts_expression(parser->current.kind)) {
                value = parse_expression(parser, 0);
                if (!value) return 0;
            }
            LambdaReductionForm form = control.kind == LAMBDA_TOK_RETURN
                ? LAMBDA_REDUCTION_FORM_RETURN
                : control.kind == LAMBDA_TOK_BREAK
                    ? LAMBDA_REDUCTION_FORM_BREAK
                    : LAMBDA_REDUCTION_FORM_CONTINUE;
            return parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT, form,
                (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
                control, (LambdaToken){0}, 0, value ? &value : NULL,
                value ? 1 : 0);
        } else {
            parser->top_level_statement_relation = false;
            LambdaParseValue expr = parse_expression(parser, 0);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            if (parser_accept(parser, LAMBDA_TOK_EQ)) {
                parser->last_statement_assignment = true;
                LambdaParseValue value = parse_expression(parser, 0);
                if (!value) return 0;
                LambdaParseValue children[2] = {expr, value};
                SourceSpan span = {first.span.start_byte,
                    parser->current.span.start_byte};
                return parser_reduce(parser, LAMBDA_REDUCE_ASSIGNMENT, span,
                    children, 2);
            }
            if (parser->top_level_statement_relation && !parser->last_statement_assignment) {
                return parser_fail(parser, "comparison requires parentheses at statement scope",
                    LAMBDA_TOK_NEWLINE);
            }
            SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            return parser_reduce(parser, LAMBDA_REDUCE_STATEMENT, span, &expr, 1);
        }
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_DECLARATION, span, NULL, 0);
}
static bool element_content_starts_sibling(const LambdaRdParser* parser) {
    LambdaRdParser probe = *parser;
    parser_prepare_probe(&probe);
    LambdaToken child_first = probe.current;
    LambdaParseValue child = parse_prefix(&probe);
    if (probe.status != LAMBDA_PARSE_OK) return false;
    (void)parse_postfix(&probe, child, child_first.span.start_byte);
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind == LAMBDA_TOK_LT;
}

static LambdaParseValue parse_content(LambdaRdParser* parser, LambdaTokenKind terminator) {
    LambdaParseValue content = 0;
    uint32_t content_start = parser->current.span.start_byte;
    bool has_content = false;
    while (parser->status == LAMBDA_PARSE_OK && parser->current.kind != terminator &&
            parser->current.kind != LAMBDA_TOK_EOF) {
        if (parser->current.kind == terminator || parser->current.kind == LAMBDA_TOK_EOF) break;
        if ((parser->current.kind == LAMBDA_TOK_LT && element_content_starts_sibling(parser)) ||
                (parser->current.kind == LAMBDA_TOK_STRING &&
                 (parser->next.kind == LAMBDA_TOK_STRING ||
                  parser->next.kind == LAMBDA_TOK_LBRACE ||
                  parser->next.kind == LAMBDA_TOK_LT)) ||
                (terminator == LAMBDA_TOK_GT && parser->current.kind == LAMBDA_TOK_LBRACE)) {
            LambdaToken child_first = parser->current;
            LambdaParseValue child = parse_prefix(parser);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            child = parse_postfix(parser, child, child_first.span.start_byte);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            LambdaParseValue statement = parser_reduce(parser, LAMBDA_REDUCE_STATEMENT,
                (SourceSpan){child_first.span.start_byte, parser->current.span.start_byte},
                &child, 1);
            content = parser_content_append(parser,
                (SourceSpan){child_first.span.start_byte, parser->current.span.start_byte},
                content, statement);
            if (!has_content) content_start = child_first.span.start_byte;
            has_content = true;
            continue;
        }
        LambdaToken statement_token = parser->current;
        LambdaTokenKind statement_first = statement_token.kind;
        if (parser->procedural_depth && statement_first == LAMBDA_TOK_LBRACE &&
                parser->next.kind == LAMBDA_TOK_RBRACE) {
            return parser_fail(parser, "an empty '{}' statement has no effect",
                LAMBDA_TOK_RBRACE);
        }
        LambdaParseValue statement = parse_statement(parser);
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        content = parser_content_append(parser,
            (SourceSpan){statement_token.span.start_byte,
                parser->current.span.start_byte}, content, statement);
        if (!has_content) content_start = statement_token.span.start_byte;
        has_content = true;
        if (parser->current.kind == terminator || parser->current.kind == LAMBDA_TOK_EOF) break;
        bool closed_tail =
            statement_first == LAMBDA_TOK_BREAK ||
            statement_first == LAMBDA_TOK_CONTINUE ||
            statement_first == LAMBDA_TOK_IMPORT ||
            (parser->prev_kind == LAMBDA_TOK_RBRACE &&
                (statement_first == LAMBDA_TOK_FN || statement_first == LAMBDA_TOK_PN ||
                 statement_first == LAMBDA_TOK_TYPE || statement_first == LAMBDA_TOK_VIEW ||
                 statement_first == LAMBDA_TOK_EDIT || statement_first == LAMBDA_TOK_WHILE ||
                 statement_first == LAMBDA_TOK_MATCH || statement_first == LAMBDA_TOK_IF ||
                 statement_first == LAMBDA_TOK_FOR || statement_first == LAMBDA_TOK_PUB));
        if (closed_tail) {
            if (parser_accept(parser, LAMBDA_TOK_SEMICOLON)) {
                if (parser->current.kind == terminator ||
                        parser->current.kind == LAMBDA_TOK_EOF) {
                    return parser_fail(parser, "trailing ';' is not a statement separator",
                        terminator);
                }
                if (parser->current.kind == LAMBDA_TOK_SEMICOLON) {
                    return parser_fail(parser, "empty statement between ';' separators",
                        terminator);
                }
            }
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_SEMICOLON)) {
            if (parser->current.kind == terminator || parser->current.kind == LAMBDA_TOK_EOF) {
                return parser_fail(parser, "trailing ';' is not a statement separator",
                    terminator);
            }
            if (parser->current.kind == LAMBDA_TOK_SEMICOLON) {
                return parser_fail(parser, "empty statement between ';' separators",
                    terminator);
            }
            continue;
        }
        if (token_is_dual_role(parser->current.kind) ||
                (parser->current.nl_before &&
                 token_is_dot_led_number(parser, &parser->current))) {
            return parser_fail(parser,
                parser->current.nl_before
                    ? "this token cannot continue the previous line; write ';' to "
                      "start a new statement, or move it to the end of that line"
                    : "expected a statement separator",
                LAMBDA_TOK_SEMICOLON);
        }
    }
    SourceSpan span = {content_start, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_CONTENT, span,
        content ? &content : NULL, content ? 1u : 0u);
}

LambdaParseStatus lambda_rd_parse_source(const char* source, size_t length,
        const LambdaParseSink* sink, void* sink_context, LambdaParseMetrics* metrics,
        LambdaParseError* error) {
    if (metrics) memset(metrics, 0, sizeof(*metrics));
    if (error) memset(error, 0, sizeof(*error));
    if (!source || length > UINT32_MAX) {
        if (error) {
            error->message = "source is null or exceeds the parser size limit";
            error->actual_kind = LAMBDA_TOK_ERROR;
        }
        return LAMBDA_PARSE_ERROR;
    }
    LambdaRdParser parser;
    memset(&parser, 0, sizeof(parser));
    lambda_lexer_init(&parser.lexer, source, length);
    parser.sink = sink;
    parser.sink_context = sink_context;
    parser.metrics = metrics;
    parser.error = error;
    parser.status = LAMBDA_PARSE_OK;
    parser.current = parser_next_significant(&parser);
    parser.next = parser_next_significant(&parser);
    if (metrics) {
        metrics->token_count = 2;
        metrics->structural_hash = UINT64_C(0xcbf29ce484222325);
    }
    if (parser.current.kind == LAMBDA_TOK_ERROR) {
        parser_set_error(&parser, "invalid token", LAMBDA_TOK_EOF);
        return parser.status;
    }
    LambdaParseValue content = parse_content(&parser, LAMBDA_TOK_EOF);
    if (parser.status == LAMBDA_PARSE_OK && parser.current.kind != LAMBDA_TOK_EOF) {
        parser_set_error(&parser, "unexpected trailing input", LAMBDA_TOK_EOF);
    }
    if (parser.status == LAMBDA_PARSE_OK) {
        SourceSpan span = {0, (uint32_t)length};
        parser_reduce(&parser, LAMBDA_REDUCE_DOCUMENT, span, &content, content ? 1u : 0u);
    }
    return parser.status;
}
static bool parser_recovery_opener(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_LPAREN || kind == LAMBDA_TOK_LBRACKET ||
        kind == LAMBDA_TOK_LBRACE;
}
static bool parser_recovery_closer(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_RPAREN || kind == LAMBDA_TOK_RBRACKET ||
        kind == LAMBDA_TOK_RBRACE;
}

static size_t parser_recovery_sync(const char* source, size_t length,
        size_t after) {
    LambdaLexer lexer;
    lambda_lexer_init(&lexer, source, length);
    uint32_t nesting = 0;
    for (;;) {
        LambdaToken token = lambda_lexer_next(&lexer);
        if (token.kind == LAMBDA_TOK_EOF) return length;
        if (parser_recovery_opener(token.kind)) {
            nesting++;
            continue;
        }
        if (parser_recovery_closer(token.kind)) {
            if (nesting) nesting--;
            continue;
        }
        if (nesting == 0 && token.span.start_byte >= after &&
                token.kind == LAMBDA_TOK_SEMICOLON) {
            return token.span.end_byte;
        }
    }
}
static void parser_recovery_adjust_span(LambdaParseError* error, size_t base) {
    if (!error) return;
    uint64_t start = (uint64_t)error->span.start_byte + base;
    uint64_t end = (uint64_t)error->span.end_byte + base;
    error->span.start_byte = start > UINT32_MAX ? UINT32_MAX : (uint32_t)start;
    error->span.end_byte = end > UINT32_MAX ? UINT32_MAX : (uint32_t)end;
}
static void parser_recovery_append(LambdaParseReport* report,
        const LambdaParseError* error) {
    if (!report || !error || report->error_count >= LAMBDA_PARSE_MAX_DIAGNOSTICS) return;
    report->errors[report->error_count++] = *error;
}

LambdaParseStatus lambda_rd_parse_recovering(const char* source, size_t length,
        LambdaParseReport* report) {
    if (!report) {
        LambdaParseError error = {0};
        return lambda_rd_parse_source(source, length, NULL, NULL, NULL, &error);
    }
    memset(report, 0, sizeof(*report));
    LambdaParseError first = {0};
    LambdaParseStatus status = lambda_rd_parse_source(source, length,
        NULL, NULL, NULL, &first);
    report->status = status;
    if (status == LAMBDA_PARSE_OK) return status;
    parser_recovery_append(report, &first);
    if (status != LAMBDA_PARSE_ERROR || !source) return status;
    size_t cursor = first.span.end_byte > first.span.start_byte
        ? first.span.end_byte : first.span.start_byte;
    for (uint32_t attempt = 0;
            attempt + 1 < LAMBDA_PARSE_MAX_DIAGNOSTICS && cursor < length;
            attempt++) {
        size_t next_start = parser_recovery_sync(source, length, cursor);
        if (next_start >= length) break;
        LambdaParseError next = {0};
        LambdaParseStatus next_status = lambda_rd_parse_source(
            source + next_start, length - next_start, NULL, NULL, NULL, &next);
        if (next_status == LAMBDA_PARSE_OK) {
            report->recovered = true;
            break;
        }
        parser_recovery_adjust_span(&next, next_start);
        parser_recovery_append(report, &next);
        if (next_status == LAMBDA_PARSE_INCOMPLETE) break;
        size_t next_cursor = next.span.end_byte > next.span.start_byte
            ? next.span.end_byte : next.span.start_byte;
        cursor = next_cursor > next_start ? next_cursor : next_start + 1;
    }
    return status;
}
