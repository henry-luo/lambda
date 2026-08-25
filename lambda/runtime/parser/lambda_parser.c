#include "lambda_rd_parser.h"

#include <string.h>

// Phase 1 keeps only two tokens of lookahead.  The second token is sufficient
// for local decisions such as a named argument or an element attribute, and
// keeps speculative parsing allocation-free.

enum {
    LAMBDA_RD_MAX_DEPTH = 1000,
    LAMBDA_BP_PIPE = 10,
    LAMBDA_BP_OR = 20,
    LAMBDA_BP_AND = 30,
    // §7.2: `not` sits between `and` and the membership tier, so
    // `not a == b` is `not (a == b)` and `not a and b` is `(not a) and b` —
    // the Python placement. Its operand is therefore parsed at MEMBERSHIP.
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
    // §5.9v3: a bare `{}` statement is dead code in a procedural body (an
    // empty block, or a discarded empty map), so it is rejected there. In
    // value and content position `{}` remains a meaningful empty map.
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

static LambdaParseValue parser_list_append(LambdaRdParser* parser,
        SourceSpan span, LambdaParseValue list, LambdaParseValue item) {
    if (!list) return parser_reduce(parser, LAMBDA_REDUCE_LIST, span, &item, 1);
    LambdaParseValue children[2] = {list, item};
    return parser_reduce(parser, LAMBDA_REDUCE_LIST, span, children, 2);
}

static LambdaParseValue parser_for_clause_append(LambdaRdParser* parser,
        SourceSpan span, LambdaParseValue list, LambdaParseValue item) {
    if (!list) {
        return parser_reduce_token(parser, LAMBDA_REDUCE_LIST,
            LAMBDA_REDUCTION_FORM_FOR_CLAUSES, span, (LambdaToken){0}, &item, 1);
    }
    LambdaParseValue children[2] = {list, item};
    return parser_reduce_token(parser, LAMBDA_REDUCE_LIST,
        LAMBDA_REDUCTION_FORM_FOR_CLAUSES, span, (LambdaToken){0}, children, 2);
}

static LambdaParseValue parser_content_append(LambdaRdParser* parser,
        SourceSpan span, LambdaParseValue list, LambdaParseValue item) {
    if (!list) {
        return parser_reduce_token(parser, LAMBDA_REDUCE_LIST,
            LAMBDA_REDUCTION_FORM_CONTENT, span, (LambdaToken){0}, &item, 1);
    }
    LambdaParseValue children[2] = {list, item};
    return parser_reduce_token(parser, LAMBDA_REDUCE_LIST,
        LAMBDA_REDUCTION_FORM_CONTENT, span, (LambdaToken){0}, children, 2);
}

static LambdaParseValue parser_parameter_append(LambdaRdParser* parser,
        SourceSpan span, LambdaParseValue list, LambdaParseValue item) {
    if (!list) {
        return parser_reduce_token(parser, LAMBDA_REDUCE_LIST,
            LAMBDA_REDUCTION_FORM_PARAMETERS, span, (LambdaToken){0}, &item, 1);
    }
    LambdaParseValue children[2] = {list, item};
    return parser_reduce_token(parser, LAMBDA_REDUCE_LIST,
        LAMBDA_REDUCTION_FORM_PARAMETERS, span, (LambdaToken){0}, children, 2);
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

// S16.1.1: line breaks are not delimiters and never reach the parser. Collapse
// every run of NEWLINE tokens into the `nl_before` flag on the token that
// follows; that flag is the only thing the S16 rules consult.
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

// S16.2.3: a token that could either continue the preceding expression or begin
// a new one. After a line break neither reading may win, so both paths are
// closed and the parse fails loudly. §7.1 removed unary `!` (so `!` is pure
// infix and continues freely); §7.10 and §7.12 keep `*` and `+` as prefixes,
// which is why the whole arithmetic family stays here.
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

// S16.6.6: 'return'/'break'/'continue' are statements and are admitted only
// inside a braced body. Every unbraced control body, else body, 'case T:' arm
// and '=>' arrow body is an expression position; rejecting the keyword here
// with a repair-naming message beats the generic "expected an expression".
// 'raise' is NOT in this set — it is an expression and diverges cleanly.
// The greedy return (S16.2.5) is why the unbraced form cannot be admitted:
// 'if (c) return' ⏎ 'cleanup()' would silently parse as 'return cleanup()'.
static bool token_is_control_statement(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_RETURN || kind == LAMBDA_TOK_BREAK ||
        kind == LAMBDA_TOK_CONTINUE;
}

// §7.15: `.digit` remains dual-role even though the lexer folds `.5` into a
// single FLOAT token — across a line break it is equally a float literal
// starting a statement and an integer member field continuing one.
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
    parser_set_error(parser, message, kind);
    return false;
}

static void parser_skip_newlines(LambdaRdParser* parser) {
    while (parser_accept(parser, LAMBDA_TOK_NEWLINE)) {}
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
        // These declarations are context-sensitive names in field/binding
        // position. The lexer cannot make that syntactic distinction.
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
    // Tree-sitter resolves a base-type spelling as an identifier in a tag
    // position, so `<list>` and `<string>` remain ordinary element names.
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
    // The current return-contract grammar admits only a named/base primary;
    // treating a following function-body `{` as a map type makes an unclosed
    // body look like a type scan and can strand the scanner at EOF.
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
    parser_set_error(parser, "maximum parser nesting exceeded", LAMBDA_TOK_EOF);
    return false;
}

static void parser_leave(LambdaRdParser* parser) {
    if (parser->depth) parser->depth--;
}

static LambdaParseValue parse_expression(LambdaRdParser* parser, int min_bp);
static LambdaParseValue parse_content(LambdaRdParser* parser, LambdaTokenKind terminator);

// Type/path semantics deliberately do not live here.  The direct-AST phase
// will pass these committed source spans to parse_type_pattern.cpp and
// parse_path_expr.cpp after their parser-node diagnostic dependency is replaced by
// SourceSpan.  This POC parser only owns their outer placement.
static LambdaParseValue parse_type_slot(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (!token_starts_type(first.kind)) {
        parser_set_error(parser, "expected a type pattern", LAMBDA_TOK_BASE_TYPE);
        return 0;
    }

    LambdaTokenKind closing_stack[128];
    uint32_t nesting = 0;
    bool need_atom = true;
    bool function_type = first.kind == LAMBDA_TOK_FN;
    while (parser->status == LAMBDA_PARSE_OK) {
        LambdaTokenKind kind = parser->current.kind;
        if (kind == LAMBDA_TOK_EOF && nesting) {
            // EOF cannot close an open type delimiter; never advance an EOF
            // token here because it would otherwise keep the span loop live.
            parser_set_error(parser, "incomplete type pattern", closing_stack[nesting - 1]);
            return 0;
        }
        if (parser->current.nl_before && !nesting && !need_atom) {
            // S16.2.1 keeps an unfinished pattern going (nesting/need_atom);
            // otherwise only a type-level continuation operator may open the
            // line. `|`, `&`, and `!` are all in the S16.2.2 continuation set,
            // so type space needs no private rule of its own.
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
                    parser_set_error(parser, "type pattern delimiter nesting exceeded", close);
                    return 0;
                }
                closing_stack[nesting++] = close;
            }
            parser_advance(parser);
            need_atom = false;
            continue;
        }

        if (!nesting && first.kind == LAMBDA_TOK_FN && kind == LAMBDA_TOK_LPAREN) {
            // A function type is the sole atom whose parameter delimiter
            // follows its initial word.  Other opening delimiters end the
            // outer type slot (for example `case int { ... }`).
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
                    parser_set_error(parser, "type pattern delimiter nesting exceeded", close);
                    return 0;
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
            // `T[]`/`T[n]` are occurrence suffixes owned by the existing
            // type-pattern parser.  The POC must still consume their complete
            // outer span so a following parameter or declaration delimiter is
            // not mistaken for part of the type slot.
            uint32_t bracket_depth = 0;
            do {
                if (parser->current.kind == LAMBDA_TOK_LBRACKET) bracket_depth++;
                if (parser->current.kind == LAMBDA_TOK_RBRACKET) bracket_depth--;
                parser_advance(parser);
                if (parser->current.kind == LAMBDA_TOK_EOF && bracket_depth) {
                    parser_set_error(parser, "incomplete type occurrence suffix", LAMBDA_TOK_RBRACKET);
                    return 0;
                }
            } while (bracket_depth);
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
        parser_set_error(parser, "expected a type pattern after type operator",
            LAMBDA_TOK_BASE_TYPE);
        return 0;
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
    // Ranges and `that` predicates belong to annotation syntax rather than
    // the reusable pattern interior, so consume their outer expression here.
    if (parser_accept(parser, LAMBDA_TOK_TO)) {
        parser_skip_newlines(parser);
        if (!parse_expression(parser, 0)) return false;
        // The type-pattern parser owns range construction. Re-submit the
        // committed source span after consuming the upper expression so the
        // direct sink receives a TypeRange instead of the lower literal.
        SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        value = parser_reduce_token(parser, LAMBDA_REDUCE_TYPE_SLOT,
            LAMBDA_REDUCTION_FORM_TOKEN, span, first, NULL, 0);
    }
    if (parser_accept(parser, LAMBDA_TOK_THAT)) {
        parser_skip_newlines(parser);
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

// Query syntax accepts only the existing primary-type sublanguage.  In
// particular, `value.?int * 2` must leave `* 2` to Pratt instead of treating
// the multiplication token as a type occurrence modifier.
static LambdaParseValue parse_primary_type_slot(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (!token_starts_type(first.kind)) {
        parser_set_error(parser, "expected a primary type", LAMBDA_TOK_BASE_TYPE);
        return 0;
    }
    if (first.kind != LAMBDA_TOK_LPAREN && first.kind != LAMBDA_TOK_LBRACKET &&
            first.kind != LAMBDA_TOK_LBRACE && first.kind != LAMBDA_TOK_LT) {
        parser_advance(parser);
    } else {
        LambdaTokenKind close = first.kind == LAMBDA_TOK_LPAREN ? LAMBDA_TOK_RPAREN :
            (first.kind == LAMBDA_TOK_LBRACKET ? LAMBDA_TOK_RBRACKET :
            (first.kind == LAMBDA_TOK_LBRACE ? LAMBDA_TOK_RBRACE : LAMBDA_TOK_GT));
        uint32_t nesting = 0;
        do {
            if (parser->current.kind == first.kind) nesting++;
            if (parser->current.kind == close) nesting--;
            parser_advance(parser);
            if (parser->current.kind == LAMBDA_TOK_EOF && nesting) {
                parser_set_error(parser, "incomplete primary type", close);
                return 0;
            }
        } while (nesting);
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
        parser_set_error(parser, "expected a path segment", LAMBDA_TOK_IDENTIFIER);
        return false;
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
    parser_skip_newlines(parser);
    if (!parser_accept(parser, LAMBDA_TOK_RBRACKET)) {
        do {
            LambdaParseValue item = parse_expression(parser, 0);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            items = parser_list_append(parser,
                (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
                items, item);
            parser_skip_newlines(parser);
            if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
            parser_skip_newlines(parser);
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
    parser_skip_newlines(parser);
    if (!parser_accept(parser, LAMBDA_TOK_RBRACE)) {
        do {
            if (parser->current.kind == LAMBDA_TOK_STRING) {
                parser_set_error(parser,
                    "a map key is a symbol, not a string: write a bare name "
                    "like {key: 1}, or single-quote it when it is not a name "
                    "like {'data-node-id': 1}",
                    LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            if (!token_is_key(parser->current.kind)) {
                parser_set_error(parser, "expected a map key", LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            LambdaToken key = parser->current;
            parser_advance(parser);
            if (!parser_expect(parser, LAMBDA_TOK_COLON, "expected ':' after map key")) return 0;
            parser_skip_newlines(parser);
            LambdaParseValue value = parse_expression(parser, 0);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            SourceSpan item_span = {key.span.start_byte, parser->current.span.start_byte};
            LambdaParseValue item = parser_reduce_token(parser,
                LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_MAP_ITEM,
                item_span, key, &value, 1);
            items = parser_list_append(parser,
                (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
                items, item);
            parser_skip_newlines(parser);
            if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
            parser_skip_newlines(parser);
        } while (true);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after map items")) return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_MAP, span, items ? &items : NULL, items ? 1u : 0u);
}

static bool element_attribute_starts(const LambdaRdParser* parser) {
    // A qualified attribute name needs more than the normal two-token lookahead:
    // `<svg svg.width: 100>` must commit to an attribute, whereas `<svg; .x>`
    // leaves `.x` as content under S2.4.3v2.  Probe without observable output.
    LambdaRdParser probe = *parser;
    probe.sink = NULL;
    probe.sink_context = NULL;
    probe.metrics = NULL;
    probe.error = NULL;
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
        parser_set_error(parser, "expected an element tag", LAMBDA_TOK_IDENTIFIER);
        return 0;
    }
    tag = parser->current;
    parser_advance(parser);
    // S2.4.3v2: in tag position a qualified name is maximal.  A path-like
    // child therefore starts only after the explicit content boundary.
    while (parser_accept(parser, LAMBDA_TOK_DOT)) {
        if (!token_is_element_name(parser->current.kind)) {
            parser_set_error(parser, "expected a namespace segment after '.'", LAMBDA_TOK_IDENTIFIER);
            return 0;
        }
        parser_advance(parser);
        tag.span.end_byte = parser->current.span.start_byte;
    }
    for (;;) {
        parser_skip_newlines(parser);
        if (!element_attribute_starts(parser)) break;
        LambdaToken attribute_name = parser->current;
        parser_advance(parser);
        while (parser_accept(parser, LAMBDA_TOK_DOT)) {
            if (!token_is_element_name(parser->current.kind)) {
                parser_set_error(parser, "expected an attribute namespace segment", LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            parser_advance(parser);
        }
        attribute_name.span.end_byte = parser->current.span.start_byte;
        parser_advance(parser);
        parser_skip_newlines(parser);
        if (count == 64) {
            parser_set_error(parser, "too many element attributes in parser POC", LAMBDA_TOK_GT);
            return 0;
        }
        // Attribute expressions exclude symbolic relations, so their closing
        // `>` must remain owned by this element instead of entering Pratt as
        // a binary operator.
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
            // §7.11v2: this comma ended the attribute list rather than
            // separating two attributes — the boundary comma, which is
            // REQUIRED whenever an element carries both attributes and
            // content. It is also what settles a greedy attribute value:
            // without it, `<div a: x (y)>` makes the call `x(y)` the
            // attribute value.
            boundary_comma = true;
            break;
        }
    }
    // §7.11v2 makes the comma a biconditional — present exactly when both
    // attributes and content are. A leading comma with no attributes is now an
    // error: the case that once needed it (`<svg, .rect>` versus the
    // maximal-munch tag `svg.rect`) dissolved when §7.15 respelled the relative
    // path `\.`, so `<svg \.rect>` is unambiguous on its own.
    if (!had_attributes && parser->current.kind == LAMBDA_TOK_COMMA) {
        parser_set_error(parser,
            "an element with no attributes takes no ',' before its content",
            LAMBDA_TOK_GT);
        return 0;
    }
    // S16.9.3: `;` separates content ITEMS, so it is legal between them
    // (`<div "a"; "b">`) but cannot open the content — there is no preceding
    // item for it to separate. Without this the token reached parse_content and
    // reported a bare "expected an expression", which points at the wrong place
    // and teaches nothing. It is the likeliest mistake to make here, because
    // `;` is the separator everywhere else in the language. The attribute-
    // bearing form `<div k: 1; "a">` is already covered by the boundary-comma
    // check below, which names the required `,`.
    if (!had_attributes && parser->current.kind == LAMBDA_TOK_SEMICOLON) {
        parser_set_error(parser,
            "';' cannot open element content; a tag is followed directly by its "
            "content, and ';' only separates one content item from the next",
            LAMBDA_TOK_GT);
        return 0;
    }
    if (had_attributes && !boundary_comma && parser->current.kind != LAMBDA_TOK_GT) {
        parser_set_error(parser,
            "expected ',' between element attributes and content",
            LAMBDA_TOK_COMMA);
        return 0;
    }
    if (boundary_comma && parser->current.kind == LAMBDA_TOK_GT) {
        parser_set_error(parser, "trailing ',' is not a separator", LAMBDA_TOK_GT);
        return 0;
    }
    if (parser->current.kind != LAMBDA_TOK_GT) {
        if (count == 64) {
            parser_set_error(parser, "too many element content items in parser POC", LAMBDA_TOK_GT);
            return 0;
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
    probe.sink = NULL;
    probe.sink_context = NULL;
    probe.metrics = NULL;
    probe.error = NULL;
    if (probe.current.kind == LAMBDA_TOK_RPAREN) {
        return arrow_return_tail_candidate(&probe);
    }
    for (;;) {
        parser_skip_newlines(&probe);
        if (!token_is_key(probe.current.kind)) return false;
        parser_advance(&probe);
        if (probe.current.kind == LAMBDA_TOK_QUESTION) parser_advance(&probe);
        if (probe.current.kind == LAMBDA_TOK_COLON) {
            parser_advance(&probe);
            parser_skip_newlines(&probe);
            LambdaParseValue type_value = 0;
            if (!parse_annotation_type_slot_value(&probe, &type_value)) return false;
        }
        if (probe.current.kind == LAMBDA_TOK_EQ) {
            parser_advance(&probe);
            parser_skip_newlines(&probe);
            if (!parse_expression(&probe, 0)) return false;
        }
        parser_skip_newlines(&probe);
        if (probe.current.kind == LAMBDA_TOK_RPAREN) {
            return arrow_return_tail_candidate(&probe);
        }
        if (probe.current.kind != LAMBDA_TOK_COMMA) return false;
        parser_advance(&probe);
    }
}

static LambdaParseValue parse_arrow_parameters(LambdaRdParser* parser) {
    LambdaParseValue parameters = 0;
    if (parser_accept(parser, LAMBDA_TOK_RPAREN)) return 0;
    do {
        bool is_var = parser_accept(parser, LAMBDA_TOK_VAR);
        if (!token_is_key(parser->current.kind)) {
            parser_set_error(parser, "expected an arrow parameter name", LAMBDA_TOK_IDENTIFIER);
            return 0;
        }
        LambdaToken name = parser->current;
        parser_advance(parser);
        bool optional = parser_accept(parser, LAMBDA_TOK_QUESTION);
        LambdaParseValue type_value = 0;
        if (parser_accept(parser, LAMBDA_TOK_COLON)) {
            parser_skip_newlines(parser);
            if (!parse_annotation_type_slot_value(parser, &type_value)) return 0;
        }
        LambdaParseValue default_value = 0;
        if (parser_accept(parser, LAMBDA_TOK_EQ)) {
            parser_skip_newlines(parser);
            default_value = parse_expression(parser, 0);
            if (!default_value) return 0;
            optional = true;
        }
        LambdaParseValue parameter_children[2];
        uint32_t child_count = 0;
        if (type_value) parameter_children[child_count++] = type_value;
        if (default_value) parameter_children[child_count++] = default_value;
        LambdaParseValue parameter = parser_reduce_tokens(parser,
            LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_PARAMETER,
            (SourceSpan){name.span.start_byte, parser->current.span.start_byte},
            name, (LambdaToken){0},
            (optional ? LAMBDA_REDUCTION_FLAG_OPTIONAL : 0u) |
                (is_var ? LAMBDA_REDUCTION_FLAG_VAR : 0u),
            parameter_children, child_count);
        parameters = parser_parameter_append(parser,
            (SourceSpan){name.span.start_byte, parser->current.span.start_byte},
            parameters, parameter);
        parser_skip_newlines(parser);
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        parser_skip_newlines(parser);
    } while (true);
    if (!parser_expect(parser, LAMBDA_TOK_RPAREN, "expected ')' after arrow parameters")) return 0;
    return parameters;
}

static LambdaParseValue parse_group_or_arrow(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue children[64] = {0};
    uint32_t count = 0;
    parser_advance(parser);
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_GROUP_BEGIN, first.span, first, NULL, 0);
    parser_skip_newlines(parser);
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
        parser_skip_newlines(parser);
        if (token_is_control_statement(parser->current.kind)) {
            parser_set_error(parser, "'return', 'break', and 'continue' are "
                "statements; an arrow '=>' body is an expression", LAMBDA_TOK_LBRACE);
            return 0;
        }
        // S16.4.2: an arrow body is fn context BY DEFINITION, even written
        // inside a `pn` — the arrow is a functional value wherever it appears,
        // so `() => {}` mid-procedure is still the empty map.
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
        do {
            if (count == 64) {
                parser_set_error(parser, "too many grouped expressions in parser POC", LAMBDA_TOK_RPAREN);
                return 0;
            }
            children[count++] = parse_expression(parser, 0);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            parser_skip_newlines(parser);
            if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
            parser_skip_newlines(parser);
        } while (true);
        if (!parser_expect(parser, LAMBDA_TOK_RPAREN, "expected ')' after grouped expression")) return 0;
    }
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_GROUP_END, first.span, first, NULL, 0);
    if (empty_group) {
        parser_set_error(parser, "expected an expression inside parentheses", LAMBDA_TOK_IDENTIFIER);
        return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce_token(parser, LAMBDA_REDUCE_GROUP,
        LAMBDA_REDUCTION_FORM_GROUP, span, first, children, count);
}

static bool braced_expression_is_map(const LambdaRdParser* parser) {
    // The expression grammar gives a block arm priority, but `{key: value}`
    // has no statement interpretation.  Recognize only that committed map
    // prefix; `{}` and every other prefix retain the control-block form.
    LambdaRdParser probe = *parser;
    probe.sink = NULL;
    probe.sink_context = NULL;
    probe.metrics = NULL;
    probe.error = NULL;
    if (probe.current.kind != LAMBDA_TOK_LBRACE) return false;
    parser_advance(&probe);
    parser_skip_newlines(&probe);
    if (probe.next.kind != LAMBDA_TOK_COLON) return false;
    // A double-quoted key is NOT a key (S16.9.x: keys are symbols — bare when
    // they are names, single-quoted otherwise). Route it to the map parser
    // anyway: read as a block, `{"k": 1}` failed at the `:` with a bare
    // "expected an expression", naming neither the rule nor the repair. The
    // map parser rejects it with both. This changes no accepted program —
    // `{"k": …}` has no valid reading either way.
    return token_is_key(probe.current.kind) ||
        probe.current.kind == LAMBDA_TOK_STRING;
}

// S16.4.2: `{}` in an `if`/`for` body is the one genuine tie — an empty
// interior says nothing, so S16.4.1v2 cannot decide it. fn context resolves to
// the empty MAP (control bodies produce values), pn context to the empty BLOCK
// (their value is discarded). Every other position is already decided: value
// position is always the empty map, and declaration bodies are always blocks.
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
    parser_skip_newlines(parser);
    if (parser->current.kind == LAMBDA_TOK_LBRACE && !control_body_brace_is_map(parser)) {
        parser_advance(parser);
        parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN, first.span, first, NULL, 0);
        children[1] = parse_content(parser, LAMBDA_TOK_RBRACE);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after if body")) return 0;
        parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_IF_BRANCH_END, first.span, first, NULL, 0);
    } else {
        if (token_is_control_statement(parser->current.kind)) {
            parser_set_error(parser, "'return', 'break', and 'continue' are "
                "statements; an unbraced 'if' body is an expression - write "
                "'if (cond) { ... }'", LAMBDA_TOK_LBRACE);
            return 0;
        }
        children[1] = parse_expression(parser, 0);
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    // S16.6.3: `else` is OPTIONAL. An absent else yields null in value
    // position and contributes nothing in content position, which retires the
    // ternary-style requirement the expression form used to carry.
    if (parser->current.kind != LAMBDA_TOK_ELSE) {
        SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        return parser_reduce(parser, LAMBDA_REDUCE_IF, span, children, 2);
    }
    parser_advance(parser);
    if (parser->current.kind == LAMBDA_TOK_LBRACE && !control_body_brace_is_map(parser)) {
        parser_advance(parser);
        parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN, first.span, first, NULL, 0);
        children[2] = parse_content(parser, LAMBDA_TOK_RBRACE);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after else body")) return 0;
        parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_IF_BRANCH_END, first.span, first, NULL, 0);
    } else {
        if (token_is_control_statement(parser->current.kind)) {
            parser_set_error(parser, "'return', 'break', and 'continue' are "
                "statements; an unbraced 'else' body is an expression - write "
                "'else { ... }'", LAMBDA_TOK_LBRACE);
            return 0;
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
    // NOTE: a base-type spelling is deliberately NOT accepted here. `let time`
    // does accept one, but a base-type NAME does not shadow the type keyword —
    // `let time = 1.5` then `time` evaluates to `datetime`, not 1.5. Until that
    // resolution bug is fixed, rejecting the name loudly beats binding it to a
    // value no read can ever see.
    if (!token_is_identifier_like(name.kind)) {
        parser_set_error(parser, "expected a for binding name", LAMBDA_TOK_IDENTIFIER);
        return 0;
    }
    parser_advance(parser);
    parser_skip_newlines(parser);
    if (parser_accept(parser, LAMBDA_TOK_COLON)) {
        parser_skip_newlines(parser);
        LambdaParseValue index_type = 0;
        if (!parse_annotation_type_slot_value(parser, &index_type)) return 0;
        flags |= LAMBDA_REDUCTION_FLAG_INDEX_TYPED;
        children[child_count++] = index_type;
        if (!parser_expect(parser, LAMBDA_TOK_COMMA, "expected ',' after typed for index")) return 0;
        parser_skip_newlines(parser);
        if (!token_is_identifier_like(parser->current.kind)) {
            parser_set_error(parser, "expected a for value name", LAMBDA_TOK_IDENTIFIER);
            return 0;
        }
        index = name;
        name = parser->current;
        parser_advance(parser);
    } else if (parser_accept(parser, LAMBDA_TOK_COMMA)) {
        parser_skip_newlines(parser);
        if (!token_is_identifier_like(parser->current.kind)) {
            parser_set_error(parser, "expected a for value name", LAMBDA_TOK_IDENTIFIER);
            return 0;
        }
        index = name;
        name = parser->current;
        parser_advance(parser);
    }
    if (parser_accept(parser, LAMBDA_TOK_QUESTION)) flags |= LAMBDA_REDUCTION_FLAG_OPTIONAL;
    if (parser->current.kind != LAMBDA_TOK_IN && parser->current.kind != LAMBDA_TOK_AT) {
        parser_set_error(parser, "expected 'in' or 'at' after for binding", LAMBDA_TOK_IN);
        return 0;
    }
    if (parser->current.kind == LAMBDA_TOK_AT) flags |= LAMBDA_REDUCTION_FLAG_KEY_ONLY;
    parser_advance(parser);
    parser_skip_newlines(parser);
    LambdaParseValue source = parse_expression(parser, 0);
    if (!source) return 0;
    children[child_count++] = source;
    parser_skip_newlines(parser);
    if (parser_accept(parser, LAMBDA_TOK_ON)) {
        parser_skip_newlines(parser);
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
        parser_set_error(parser, "expected a for-let name", LAMBDA_TOK_IDENTIFIER);
        return 0;
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
    // A newline is an extra in the shipped grammar, so two-token lookahead
    // alone cannot tell `,\n let` from the next loop binding.
    LambdaRdParser probe = *parser;
    probe.sink = NULL;
    probe.sink_context = NULL;
    probe.metrics = NULL;
    probe.error = NULL;
    parser_advance(&probe);
    parser_skip_newlines(&probe);
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind != LAMBDA_TOK_LET;
}

static LambdaParseValue parse_for_group_clause(LambdaRdParser* parser) {
    if (!parser_expect(parser, LAMBDA_TOK_BY, "expected 'by' after group")) return false;
    parser_skip_newlines(parser);
    LambdaParseValue keys = 0;
    do {
        LambdaToken key_first = parser->current;
        LambdaParseValue key = parse_expression(parser, LAMBDA_BP_POSTFIX);
        if (!key) return 0;
        LambdaToken alias = {0};
        if (parser_accept(parser, LAMBDA_TOK_AS)) {
            parser_skip_newlines(parser);
            if (!token_is_identifier_like(parser->current.kind)) {
                parser_set_error(parser, "expected a group alias", LAMBDA_TOK_IDENTIFIER);
                return 0;
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
        parser_skip_newlines(parser);
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        parser_skip_newlines(parser);
    } while (true);
    if (!parser_expect(parser, LAMBDA_TOK_INTO, "expected 'into' after group keys")) return 0;
    LambdaToken name = parser->current;
    if (!token_is_identifier_like(parser->current.kind)) {
        parser_set_error(parser, "expected a group binding name", LAMBDA_TOK_IDENTIFIER);
        return 0;
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
    if (parenthesized) parser_skip_newlines(parser);
    LambdaParseValue binding = parse_for_binding(parser);
    if (!binding) return 0;
    clauses = parser_for_clause_append(parser, first.span, clauses, binding);
    parser_skip_newlines(parser);
    while (for_binding_list_continues(parser)) {
        parser_advance(parser);
        parser_skip_newlines(parser);
        binding = parse_for_binding(parser);
        if (!binding) return 0;
        clauses = parser_for_clause_append(parser, first.span, clauses, binding);
        parser_skip_newlines(parser);
    }
    while (parser->status == LAMBDA_PARSE_OK) {
        parser_skip_newlines(parser);
        if (parser_accept(parser, LAMBDA_TOK_COMMA)) {
            parser_skip_newlines(parser);
            LambdaParseValue let_clause = parse_for_let_clause(parser);
            if (!let_clause) return 0;
            clauses = parser_for_clause_append(parser, first.span, clauses, let_clause);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_WHERE)) {
            parser_skip_newlines(parser);
            LambdaParseValue where = parse_expression(parser, 0);
            if (!where) return 0;
            clauses = parser_for_clause_append(parser, first.span, clauses,
                parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
                    LAMBDA_REDUCTION_FORM_FOR_WHERE, first.span, first, &where, 1));
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_GROUP)) {
            parser_skip_newlines(parser);
            LambdaParseValue group = parse_for_group_clause(parser);
            if (!group) return 0;
            clauses = parser_for_clause_append(parser, first.span, clauses, group);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_ORDER)) {
            if (!parser_expect(parser, LAMBDA_TOK_BY, "expected 'by' after order")) return 0;
            parser_skip_newlines(parser);
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
                parser_skip_newlines(parser);
            } while (parser_accept(parser, LAMBDA_TOK_COMMA));
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_LIMIT)) {
            LambdaToken last = parser->current;
            bool from_end = parser_accept(parser, LAMBDA_TOK_LAST);
            parser_skip_newlines(parser);
            LambdaParseValue limit = parse_expression(parser, 0);
            if (!limit) return 0;
            clauses = parser_for_clause_append(parser, first.span, clauses,
                parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT,
                    LAMBDA_REDUCTION_FORM_FOR_LIMIT, first.span, last, (LambdaToken){0},
                    from_end ? LAMBDA_REDUCTION_FLAG_OPTIONAL : 0, &limit, 1));
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_OFFSET)) {
            parser_skip_newlines(parser);
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
    if (parenthesized) parser_skip_newlines(parser);
    // S16.4.1v2: the interior decides, in BOTH spellings — gating the map
    // reading on `parenthesized` made `for x in l {a: x}` reject a body that
    // `for (x in l) {a: x}` accepts, which is exactly the flip the ruling bans.
    if (parser->current.kind == LAMBDA_TOK_LBRACE &&
            control_body_brace_is_map(parser)) {
        children[0] = parse_expression(parser, 0);
    } else if (parser_accept(parser, LAMBDA_TOK_LBRACE)) {
        children[0] = parse_content(parser, LAMBDA_TOK_RBRACE);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after for body")) return 0;
    } else if (parenthesized) {
        if (token_is_control_statement(parser->current.kind)) {
            parser_set_error(parser, "'return', 'break', and 'continue' are "
                "statements; an unbraced 'for' body is an expression - write "
                "'for (...) { ... }'", LAMBDA_TOK_LBRACE);
            return 0;
        }
        children[0] = parse_expression(parser, 0);
    } else {
        parser_set_error(parser, "expected '{' after for statement header", LAMBDA_TOK_LBRACE);
        return 0;
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
    parser_skip_newlines(parser);
    LambdaParseValue scrutinee = parse_expression(parser, 0);
    if (parser->status != LAMBDA_PARSE_OK ||
            !parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after match value")) return 0;
    while (parser->status == LAMBDA_PARSE_OK && parser->current.kind != LAMBDA_TOK_RBRACE) {
        while (parser_accept(parser, LAMBDA_TOK_NEWLINE) || parser_accept(parser, LAMBDA_TOK_SEMICOLON)) {}
        if (parser->current.kind == LAMBDA_TOK_RBRACE) break;
        LambdaToken arm_first = parser->current;
        LambdaParseValue pattern = 0;
        if (parser_accept(parser, LAMBDA_TOK_CASE)) {
            LambdaToken pattern_first = parser->current;
            pattern = parse_type_slot(parser);
            if (!pattern) return 0;
            if (parser_accept(parser, LAMBDA_TOK_TO)) {
                parser_skip_newlines(parser);
                if (!parse_expression(parser, 0)) return 0;
                SourceSpan span = {pattern_first.span.start_byte,
                    parser->current.span.start_byte};
                pattern = parser_reduce_token(parser, LAMBDA_REDUCE_TYPE_SLOT,
                    LAMBDA_REDUCTION_FORM_TOKEN, span, pattern_first, NULL, 0);
            }
            if (parser_accept(parser, LAMBDA_TOK_THAT)) {
                parser_skip_newlines(parser);
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
            parser_set_error(parser, "expected case or default in match", LAMBDA_TOK_CASE);
            return 0;
        }
        // A match arm owns its local bindings even when its body is a single
        // expression. Without this scope, same-named lets in sibling arms
        // collide during direct AST construction.
        parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_MATCH_ARM_BEGIN, arm_first.span, arm_first,
            NULL, 0);
        LambdaParseValue body = 0;
        // S16.6.8 bars a procedural block after `case T:` but not after the
        // braced arm spelling, so build_ast needs to tell the two apart; the
        // AST node is identical either way (both reduce to CONTENT).
        bool arm_body_braced = false;
        if (parser_accept(parser, LAMBDA_TOK_COLON)) {
            parser_skip_newlines(parser);
            if (token_is_control_statement(parser->current.kind)) {
                parser_set_error(parser, "'return', 'break', and 'continue' are "
                    "statements; a 'case T:' arm takes an expression - write "
                    "'case T { ... }'", LAMBDA_TOK_LBRACE);
                return 0;
            }
            body = parse_expression(parser, 0);
        } else if (parser_accept(parser, LAMBDA_TOK_LBRACE)) {
            arm_body_braced = true;
            body = parse_content(parser, LAMBDA_TOK_RBRACE);
            if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after match arm")) return 0;
        } else {
            parser_set_error(parser, "expected ':' or '{' after match arm", LAMBDA_TOK_COLON);
            return 0;
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
        parser_set_error(parser, "expected case or default in match", LAMBDA_TOK_CASE);
        return 0;
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
    // S16.6.1: one node, two spellings — `while (c) { }` and `while c { }`.
    // S16.6.2: `(` right after the keyword COMMITS to the parenthesized form,
    // so a bare condition may not begin with `(`.
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
        parser_set_error(parser, missing_name_message, LAMBDA_TOK_IDENTIFIER);
        return 0;
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
            parser_set_error(parser, missing_name_message, LAMBDA_TOK_IDENTIFIER);
            return 0;
        }
        if (name_count == 64) {
            parser_set_error(parser, "too many decomposition names", LAMBDA_TOK_IDENTIFIER);
            return 0;
        }
        names[name_count++] = parser->current;
        parser_advance(parser);
    }
    bool named_decompose = false;
    if (!parser_accept(parser, LAMBDA_TOK_EQ)) {
        if (!decomposed || !parser_accept(parser, LAMBDA_TOK_AT)) {
            parser_set_error(parser, missing_equals_message, LAMBDA_TOK_EQ);
            return 0;
        }
        named_decompose = true;
    }
    parser_skip_newlines(parser);
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
        // §5.10: `(` opens an island. Inside it the element scope's suppression
        // of `<`/`>` lifts, so `<div a: (1 > 0)>` is a comparison rather than a
        // premature element close.
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
        // §5.9v3: braces are resolved by INTERIOR, not position. A map needs
        // `key ':'`, which statement space cannot spell, so anything else is a
        // block expression (value = last expression). This is what gives arrow
        // functions block bodies with no `({...})` quirk. `{}` stays a map.
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
        parser_set_error(parser, "'!' is not logical negation here; use 'not'",
            LAMBDA_TOK_NOT);
        return 0;
    } else if (first.kind == LAMBDA_TOK_NOT ||
            first.kind == LAMBDA_TOK_MINUS || first.kind == LAMBDA_TOK_PLUS ||
            first.kind == LAMBDA_TOK_STAR) {
        parser_advance(parser);
        // §7.2: `not` is the one LOOSE prefix — it takes everything above the
        // `and`/`or` tiers, so a comparison lands inside it. The arithmetic
        // prefixes keep their tight binding.
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
    // S16.5.1 / §5.10: inside element scope the symbol relationals are NOT
    // operators at all — `>` unconditionally terminates and `<` opens a child.
    // The old heuristic asked what followed the `>`, so `<p "x">` ⏎ `1` read
    // the close as a comparison against the next statement and then ran to EOF.
    // Comparisons inside an element use a parenthesized island, which lifts
    // these flags (see parse_prefix), or the keyword operators.
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
            parser_skip_newlines(parser);
            if (!parser_accept(parser, LAMBDA_TOK_RPAREN)) {
                do {
                    if (argument_count == 64) {
                        parser_set_error(parser, "too many call arguments in parser POC", LAMBDA_TOK_RPAREN);
                        return 0;
                    }
                    LambdaToken argument_first = parser->current;
                    if (token_is_key(parser->current.kind) && parser->next.kind == LAMBDA_TOK_COLON) {
                        parser_advance(parser);
                        parser_advance(parser);
                        children[argument_count + 1] = parse_expression(parser, 0);
                        if (parser->status == LAMBDA_PARSE_OK) {
                            children[argument_count + 1] = parser_reduce_token(parser,
                                LAMBDA_REDUCE_STATEMENT,
                                LAMBDA_REDUCTION_FORM_NAMED_ARGUMENT,
                                (SourceSpan){argument_first.span.start_byte,
                                    parser->current.span.start_byte},
                                argument_first, &children[argument_count + 1], 1);
                        }
                    } else {
                        children[argument_count + 1] = parse_expression(parser, 0);
                    }
                    if (parser->status != LAMBDA_PARSE_OK) return 0;
                    argument_count++;
                    parser_skip_newlines(parser);
                    if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
                    parser_skip_newlines(parser);
                } while (true);
                if (!parser_expect(parser, LAMBDA_TOK_RPAREN, "expected ')' after arguments")) return 0;
            }
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
            parser_skip_newlines(parser);
            do {
                if (index_count == 64) {
                    parser_set_error(parser, "too many index dimensions in parser POC", LAMBDA_TOK_RBRACKET);
                    return 0;
                }
                children[index_count + 1] = parse_expression(parser, 0);
                if (parser->status != LAMBDA_PARSE_OK) return 0;
                index_count++;
                parser_skip_newlines(parser);
                if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
                parser_skip_newlines(parser);
            } while (true);
            if (!parser_expect(parser, LAMBDA_TOK_RBRACKET, "expected ']' after index")) return 0;
            SourceSpan span = {left_start_byte, parser->current.span.start_byte};
            left = parser_reduce_token(parser, LAMBDA_REDUCE_POSTFIX,
                LAMBDA_REDUCTION_FORM_INDEX, span, first, children,
                index_count + 1);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_DOT)) {
            // Publish the committed field token, not the dot introducer.  The
            // direct sink needs the field spelling without re-lexing the span.
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
            // §3.6: the handler body must open on the same line as its `^`.
            // Across a line break `{` is dual-role — handler body, or a new map
            // or block statement after a bare propagate — so neither reading
            // may win silently (S16.2.3).
            if (parser->current.kind == LAMBDA_TOK_LBRACE && parser->current.nl_before) {
                parser_set_error(parser,
                    "a handler body must open on the same line as its '^'",
                    LAMBDA_TOK_LBRACE);
                return 0;
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
        // S16.2.2/S16.2.3: after a COMPLETE expression, an operator that can
        // only continue (`|> | & % > == != <= >=`, `++`, `**`, every word
        // operator) opens a line freely, so nothing is checked for it. A
        // dual-role operator may not: the expression stops here, and
        // parse_content turns the leftover token into the loud error.
        if (parser->current.nl_before && token_is_dual_role(parser->current.kind)) {
            break;
        }
        if (left_is_element && parser->stop_at_element_close &&
                parser->current.kind == LAMBDA_TOK_LT) {
            // Adjacent element children are legal content; their `<` starts a
            // sibling, not a relation whose right operand is the tag name.
            break;
        }
        bool right_associative = false;
        int bp = infix_binding_power(parser, parser->current.kind, &right_associative);
        if (bp < min_bp) break;
        LambdaToken op = parser->current;
        parser_advance(parser);
        parser_skip_newlines(parser);
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
        // `is` consumes the type-pattern sublanguage as its RHS. Parsing it as
        // an ordinary expression turns `[int]` into a value array, changing
        // both the AST type witness and runtime membership semantics.
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
    // The only hand-parser ambiguity whose decision depends on the whole
    // condition is `if (cond) value else value` versus `if cond { ... }`.
    // Probe with no sink, metrics, or diagnostics so lookahead cannot publish
    // reductions before the branch is committed.
    LambdaRdParser probe = *parser;
    probe.sink = NULL;
    probe.sink_context = NULL;
    probe.metrics = NULL;
    probe.error = NULL;
    parser_advance(&probe);
    (void)parse_expression(&probe, 0);
    parser_skip_newlines(&probe);
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind == LAMBDA_TOK_LBRACE;
}

static bool parse_parameter_list(LambdaRdParser* parser) {
    parser->last_parameter_list = 0;
    parser->last_parameter_variadic = false;
    if (!parser_expect(parser, LAMBDA_TOK_LPAREN, "expected '(' before parameters")) return false;
    parser_skip_newlines(parser);
    if (parser_accept(parser, LAMBDA_TOK_RPAREN)) return true;
    LambdaParseValue parameters = 0;
    do {
        if (parser_accept(parser, LAMBDA_TOK_ELLIPSIS)) {
            parser->last_parameter_variadic = true;
            break;
        }
        bool is_var = parser_accept(parser, LAMBDA_TOK_VAR);
        if (!token_is_key(parser->current.kind)) {
            parser_set_error(parser, "expected a parameter name", LAMBDA_TOK_IDENTIFIER);
            return false;
        }
        LambdaToken name = parser->current;
        parser_advance(parser);
        bool optional = parser_accept(parser, LAMBDA_TOK_QUESTION);
        LambdaParseValue type_value = 0;
        if (parser_accept(parser, LAMBDA_TOK_COLON)) {
            parser_skip_newlines(parser);
            if (!parse_annotation_type_slot_value(parser, &type_value)) return false;
        }
        LambdaParseValue default_value = 0;
        if (parser_accept(parser, LAMBDA_TOK_EQ)) {
            parser_skip_newlines(parser);
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
            (SourceSpan){name.span.start_byte, parser->current.span.start_byte},
            name, (LambdaToken){0},
            (optional ? LAMBDA_REDUCTION_FLAG_OPTIONAL : 0u) |
                (is_var ? LAMBDA_REDUCTION_FLAG_VAR : 0u),
            parameter_children, child_count);
        parameters = parser_parameter_append(parser,
            (SourceSpan){name.span.start_byte, parser->current.span.start_byte},
            parameters, parameter);
        parser_skip_newlines(parser);
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        parser_skip_newlines(parser);
    } while (true);
    if (!parser_expect(parser, LAMBDA_TOK_RPAREN, "expected ')' after parameters")) return false;
    parser->last_parameter_list = parameters;
    return true;
}

static LambdaParseValue parse_function_declaration(LambdaRdParser* parser,
        bool is_public) {
    LambdaToken first = parser->current;
    bool is_proc = first.kind == LAMBDA_TOK_PN;
    parser_advance(parser);
    if (!token_is_key(parser->current.kind)) {
        parser_set_error(parser, "expected a function name", LAMBDA_TOK_IDENTIFIER);
        return 0;
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
    parser_skip_newlines(parser);
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
    // S16.4.2 reads `procedural_depth` to break the empty-brace tie, so the
    // depth must track the ENCLOSING function's effect kind, not the nesting
    // count: a `fn` written inside a `pn` is fn context, and must not inherit
    // the outer procedural depth.
    uint32_t saved_depth = parser->procedural_depth;
    // S16.6.7: a procedure has exactly one body form, the braced statement
    // block. The Tree-sitter reference grammar already rejected 'pn p() =>';
    // the C parser accepting it was a front-end divergence.
    if (is_proc && parser->current.kind == LAMBDA_TOK_ARROW) {
        parser_set_error(parser, "a procedure body is a statement block - write "
            "'pn name() { ... }'; '=>' bodies are fn-only", LAMBDA_TOK_LBRACE);
        return 0;
    }
    if (parser_accept(parser, LAMBDA_TOK_ARROW)) {
        parser_skip_newlines(parser);
        if (token_is_control_statement(parser->current.kind)) {
            parser_set_error(parser, "'return', 'break', and 'continue' are "
                "statements; an arrow '=>' body is an expression", LAMBDA_TOK_LBRACE);
            return 0;
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
        parser_set_error(parser, "expected a function body", LAMBDA_TOK_LBRACE);
        return 0;
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
                parser_set_error(parser, "expected a state name", LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            LambdaToken state_name = parser->current;
            parser_advance(parser);
            // `name: expr` declares template-local state; a bare `name` binds
            // engine-backed state, whose value the host owns, so it carries no
            // initializer and reduces with no value child.
            LambdaParseValue value = 0;
            if (parser_accept(parser, LAMBDA_TOK_COLON)) {
                parser_skip_newlines(parser);
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
            parser_set_error(parser, "expected an event name", LAMBDA_TOK_IDENTIFIER);
            return 0;
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
        parser_set_error(parser, "expected a type name", LAMBDA_TOK_IDENTIFIER);
        return 0;
    }
    LambdaToken name = parser->current;
    parser_advance(parser);
    if (parser_accept(parser, LAMBDA_TOK_EQ)) {
        LambdaParseValue type_value = 0;
        if (!parse_annotation_type_slot_value(parser, &type_value)) return 0;
        LambdaParseValue declarations = parser_reduce_tokens(parser,
            LAMBDA_REDUCE_DECLARATION, LAMBDA_REDUCTION_FORM_TYPE_ALIAS,
            (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
            name, (LambdaToken){0},
            is_public ? LAMBDA_REDUCTION_FLAG_PUBLIC : 0u, &type_value, 1);
        while (parser_accept(parser, LAMBDA_TOK_COMMA)) {
            if (!token_is_key(parser->current.kind)) {
                parser_set_error(parser, "expected a type alias name", LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            name = parser->current;
            parser_advance(parser);
            if (!parser_expect(parser, LAMBDA_TOK_EQ, "expected '=' after type alias name")) return 0;
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
        // The object begin reduction carries the optional base token in its
        // detail slot; the direct sink does not need the `type` keyword after
        // the parser has committed this declaration.
        LambdaToken base = {0};
        if (parser_accept(parser, LAMBDA_TOK_COLON)) {
            if (!token_is_key(parser->current.kind)) {
                parser_set_error(parser, "expected an inherited type name", LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            base = parser->current;
            parser_advance(parser);
        }
        parser_reduce_tokens(parser, LAMBDA_REDUCE_CONTEXT,
            LAMBDA_REDUCTION_FORM_TYPE_OBJECT_BEGIN, first.span, base,
            name, is_public ? LAMBDA_REDUCTION_FLAG_PUBLIC : 0u, NULL, 0);
        if (!parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after object type name")) return 0;
        while (parser->status == LAMBDA_PARSE_OK && parser->current.kind != LAMBDA_TOK_RBRACE) {
            // §7.11: fields, the object-level constraint, and methods are ONE
            // comma list. The `;` section divider is retired — `;` now has a
            // single role language-wide, statement separation.
            while (parser_accept(parser, LAMBDA_TOK_COMMA)) {}
            if (parser->current.kind == LAMBDA_TOK_RBRACE) break;
            if (parser->current.kind == LAMBDA_TOK_SEMICOLON) {
                parser_set_error(parser,
                    "object-type members are separated by ',', not ';'",
                    LAMBDA_TOK_COMMA);
                return 0;
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
            // §7.22: `a?: T` marks the FIELD optional — it may be absent —
            // which is a different claim from `a: T?`, where the field is
            // present and its VALUE is nullable.
            // `name?` alone is an OCCURRENCE type used as content; only
            // `name?:` is an optional field, so the `:` must be confirmed one
            // character past the `?` before taking the field path.
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
                    parser_skip_newlines(parser);
                    children[child_count++] = parse_expression(parser, 0);
                    if (!children[child_count - 1]) return 0;
                }
                parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT,
                    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_FIELD,
                    (SourceSpan){field.span.start_byte, parser->current.span.start_byte},
                    field, (LambdaToken){0}, field_flags, children, child_count);
                continue;
            }
            // A bare type is the object/element content schema.  Its inner
            // grammar remains owned by the existing type-pattern parser.
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
            parser_set_error(parser, "expected an object type field, content type, or method",
                LAMBDA_TOK_IDENTIFIER);
            return 0;
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
    // Leave the optional `;` for parse_content so it remains the separator
    // between this declaration and the next statement.
    return parser_reduce_token(parser, LAMBDA_REDUCE_STATEMENT,
        LAMBDA_REDUCTION_FORM_VAR,
        (SourceSpan){first.span.start_byte, parser->current.span.start_byte},
        first, &declarations, 1);
}


static bool if_statement_body_is_map(const LambdaRdParser* parser) {
    LambdaRdParser probe = *parser;
    probe.sink = NULL;
    probe.sink_context = NULL;
    probe.metrics = NULL;
    probe.error = NULL;
    parser_advance(&probe);
    // S16.4.1v2: the brace is resolved by its INTERIOR, so a parenthesized head
    // must not flip the reading — `if c {a: 1}` and `if (c) {a: 1}` are the same
    // program. Bailing out on LPAREN here (an Option-2 leftover, where a paren
    // head committed to an expression body and `{` was therefore a block) made
    // the paren spelling reject every map body.
    (void)parse_expression(&probe, 0);
    if (probe.status != LAMBDA_PARSE_OK) return false;
    return control_body_brace_is_map(&probe);
}

static LambdaParseValue parse_if_statement(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (if_statement_body_is_map(parser)) {
        // S16.6.1: one node, two spellings — and §5.9v3 resolves the braces by
        // interior, so `()` never flips the reading: `if c {a: 1}` and
        // `if (c) {a: 1}` both yield the map.
        return parse_if_expression(parser);
    }
    parser_advance(parser);
    LambdaParseValue condition = parse_expression(parser, 0);
    if (parser->status != LAMBDA_PARSE_OK ||
            !parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after if condition")) return 0;
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN, first.span, first, NULL, 0);
    LambdaParseValue body = parse_content(parser, LAMBDA_TOK_RBRACE);
    if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after if body")) return 0;
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
        LAMBDA_REDUCTION_FORM_IF_BRANCH_END, first.span, first, NULL, 0);
    LambdaParseValue children[3] = {condition, body, 0};
    uint32_t child_count = 2;
    // S16.2.2 classifies `else` as continuation-only, so it attaches across a
    // line break with nothing to skip: line breaks never reach the parser.
    if (parser_accept(parser, LAMBDA_TOK_ELSE)) {
        parser_skip_newlines(parser);
        if (parser->current.kind == LAMBDA_TOK_IF) {
            // An `else if` arm can be the value form without braces; reuse
            // the same branch probe as a top-level `if` to avoid forcing it
            // through the block-statement parser.
            children[2] = if_starts_block_statement(parser)
                ? parse_if_statement(parser)
                : parse_if_expression(parser);
        } else if (parser_accept(parser, LAMBDA_TOK_LBRACE)) {
            parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
                LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN, first.span, first, NULL, 0);
            children[2] = parse_content(parser, LAMBDA_TOK_RBRACE);
            if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after else body")) return 0;
            parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT,
                LAMBDA_REDUCTION_FORM_IF_BRANCH_END, first.span, first, NULL, 0);
        } else {
            if (token_is_control_statement(parser->current.kind)) {
                parser_set_error(parser, "'return', 'break', and 'continue' are "
                    "statements; an unbraced 'else' body is an expression - write "
                    "'else { ... }'", LAMBDA_TOK_LBRACE);
                return 0;
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
                // Package segments are names, so a base-type spelling such
                // as `.atoms.array` remains valid after lexer classification.
                if (!token_is_key(parser->current.kind)) {
                    parser_set_error(parser, "expected a relative import component", LAMBDA_TOK_IDENTIFIER);
                    return 0;
                }
                module_last = parser->current;
                parser_advance(parser);
            } else if (token_is_key(parser->current.kind)) {
                module_first = parser->current;
                module_last = parser->current;
                parser_advance(parser);
            } else {
                parser_set_error(parser, "expected an import module", LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            while (parser->current.kind == LAMBDA_TOK_DOT ||
                    parser->current.kind == LAMBDA_TOK_SLASH) {
                parser_advance(parser);
                if (!token_is_key(parser->current.kind)) {
                    parser_set_error(parser, "expected an import component", LAMBDA_TOK_IDENTIFIER);
                    return 0;
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
            // §7.6: `pub` modifies a declaration keyword — `pub let`, `pub fn`,
            // `pub type`. The old spelling replaced `let` outright, so one
            // keyword composed two different ways; `pub var` stays illegal by
            // the modifier simply not composing with `var`.
            parser_set_error(parser, "'pub' modifies a declaration; write 'pub let'",
                LAMBDA_TOK_LET);
            return 0;
        }
        if (parser->current.kind == LAMBDA_TOK_FN || parser->current.kind == LAMBDA_TOK_PN) {
            return parse_function_declaration(parser, is_public);
        }
        if (parser->current.kind == LAMBDA_TOK_VIEW || parser->current.kind == LAMBDA_TOK_EDIT) {
            return parse_view_declaration(parser);
        }
        // `type` is both the declaration introducer and the base-type value.
        // A following `(` is a normal call such as `type(value)`.
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
            // Content owns statement separators; consuming only `apply` keeps
            // a following statement from becoming adjacent to this one.
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
                parser_skip_newlines(parser);
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
                // S16.9.1 made `pub` a modifier that composes with `let`, but the
                // flag is only honoured on a DECLARATION reduction. Reducing
                // `pub let` as a plain statement dropped it silently, so the
                // binding was never exported and `mod.name` read as an
                // undefined variable while `pub fn` kept working.
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
                parser_skip_newlines(parser);
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
            // Control flow must reach the AST sink as a committed reduction.
            // Dropping `return` here made every direct-parser procedure fall
            // through as null instead of preserving its procedural effect (D6.1.2).
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
                parser_skip_newlines(parser);
                LambdaParseValue value = parse_expression(parser, 0);
                if (!value) return 0;
                LambdaParseValue children[2] = {expr, value};
                SourceSpan span = {first.span.start_byte,
                    parser->current.span.start_byte};
                return parser_reduce(parser, LAMBDA_REDUCE_ASSIGNMENT, span,
                    children, 2);
            }
            if (parser->top_level_statement_relation && !parser->last_statement_assignment) {
                parser_set_error(parser, "comparison requires parentheses at statement scope",
                    LAMBDA_TOK_NEWLINE);
                return 0;
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
    probe.sink = NULL;
    probe.sink_context = NULL;
    probe.metrics = NULL;
    probe.error = NULL;
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
            // These are the repeatable child forms in `_content_expr`. Parse
            // the child before Pratt can reinterpret a following `<` as the
            // relational operator rather than a sibling element.
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
            // §5.9v3: dead under either reading — a no-op block or a discarded
            // empty map. This design answers meaningless input with an error.
            parser_set_error(parser, "an empty '{}' statement has no effect",
                LAMBDA_TOK_RBRACE);
            return 0;
        }
        LambdaParseValue statement = parse_statement(parser);
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        content = parser_content_append(parser,
            (SourceSpan){statement_token.span.start_byte,
                parser->current.span.start_byte}, content, statement);
        if (!has_content) content_start = statement_token.span.start_byte;
        has_content = true;
        if (parser->current.kind == terminator || parser->current.kind == LAMBDA_TOK_EOF) break;
        // §7.14: a CLOSED-tail statement — one ending in the structural closer
        // of a non-postfixable construct, or a self-complete keyword — needs no
        // separator at all: no dual-role token can continue it, so the next
        // statement simply juxtaposes ("after a block, never ';'"). The tail,
        // not the statement kind, is what matters: `type T = int` and
        // `fn f() => x` end in expressions and stay open.
        bool closed_tail =
            statement_first == LAMBDA_TOK_BREAK ||
            statement_first == LAMBDA_TOK_CONTINUE ||
            // An import ends on a module NAME, which no dual-role token can
            // continue, so it needs no separator before the next statement.
            statement_first == LAMBDA_TOK_IMPORT ||
            (parser->prev_kind == LAMBDA_TOK_RBRACE &&
                (statement_first == LAMBDA_TOK_FN || statement_first == LAMBDA_TOK_PN ||
                 statement_first == LAMBDA_TOK_TYPE || statement_first == LAMBDA_TOK_VIEW ||
                 statement_first == LAMBDA_TOK_EDIT || statement_first == LAMBDA_TOK_WHILE ||
                 statement_first == LAMBDA_TOK_MATCH || statement_first == LAMBDA_TOK_IF ||
                 statement_first == LAMBDA_TOK_FOR || statement_first == LAMBDA_TOK_PUB));
        if (closed_tail) {
            // §7.14 makes the separator UNNECESSARY after a closed tail, not
            // illegal — S16.1.2 still admits one `;` between any two
            // statements, and `let`/`type` already accept it. Skipping the
            // consume left the `;` to be read as the next statement.
            if (parser_accept(parser, LAMBDA_TOK_SEMICOLON)) {
                if (parser->current.kind == terminator ||
                        parser->current.kind == LAMBDA_TOK_EOF) {
                    parser_set_error(parser, "trailing ';' is not a statement separator",
                        terminator);
                    return 0;
                }
                if (parser->current.kind == LAMBDA_TOK_SEMICOLON) {
                    parser_set_error(parser, "empty statement between ';' separators",
                        terminator);
                    return 0;
                }
            }
            continue;
        }
        // S16.1.2: `;` is a STRICT separator — exactly one, between two
        // statements. A trailing `;` (nothing but the terminator after it) and
        // a doubled `;` are both syntax errors.
        if (parser_accept(parser, LAMBDA_TOK_SEMICOLON)) {
            if (parser->current.kind == terminator || parser->current.kind == LAMBDA_TOK_EOF) {
                parser_set_error(parser, "trailing ';' is not a statement separator",
                    terminator);
                return 0;
            }
            if (parser->current.kind == LAMBDA_TOK_SEMICOLON) {
                parser_set_error(parser, "empty statement between ';' separators",
                    terminator);
                return 0;
            }
            continue;
        }
        // S16.1.3: no separator is needed when the next token cannot continue
        // the statement just parsed. S16.2.3: when it is dual-role, neither
        // reading wins and the parse fails here, naming both repairs.
        if (token_is_dual_role(parser->current.kind) ||
                (parser->current.nl_before &&
                 token_is_dot_led_number(parser, &parser->current))) {
            parser_set_error(parser,
                parser->current.nl_before
                    ? "this token cannot continue the previous line; write ';' to "
                      "start a new statement, or move it to the end of that line"
                    : "expected a statement separator",
                LAMBDA_TOK_SEMICOLON);
            return 0;
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
