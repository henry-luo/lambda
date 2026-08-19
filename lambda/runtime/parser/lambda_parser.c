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
    bool last_statement_self_delimiting;
    bool last_statement_assignment;
    uint32_t expression_depth;
    bool top_level_statement_relation;
} LambdaRdParser;

static uint64_t mix_hash(uint64_t hash, uint64_t value) {
    hash ^= value + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) + (hash >> 2);
    return hash;
}

static LambdaParseValue parser_reduce(LambdaRdParser* parser,
        LambdaReductionKind kind, LambdaSourceSpan span,
        const LambdaParseValue* children, uint32_t child_count) {
    uint64_t value = UINT64_C(0xcbf29ce484222325);
    value = mix_hash(value, (uint64_t)kind);
    value = mix_hash(value, span.start_byte);
    value = mix_hash(value, span.end_byte);
    for (uint32_t i = 0; i < child_count; i++) value = mix_hash(value, children[i]);
    if (parser->sink && parser->sink->reduce) {
        LambdaParseValue sink_value = parser->sink->reduce(parser->sink_context,
            (int)kind, span, children, child_count);
        if (sink_value) value = sink_value;
    }
    if (parser->metrics) {
        parser->metrics->reduction_count++;
        parser->metrics->structural_hash = mix_hash(parser->metrics->structural_hash, value);
    }
    return value;
}

static LambdaParseValue parser_list_append(LambdaRdParser* parser,
        LambdaSourceSpan span, LambdaParseValue list, LambdaParseValue item) {
    if (!list) return parser_reduce(parser, LAMBDA_REDUCE_LIST, span, &item, 1);
    LambdaParseValue children[2] = {list, item};
    return parser_reduce(parser, LAMBDA_REDUCE_LIST, span, children, 2);
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

static void parser_advance(LambdaRdParser* parser) {
    if (parser->status != LAMBDA_PARSE_OK) return;
    parser->current = parser->next;
    parser->next = lambda_lexer_next(&parser->lexer);
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

static bool parser_expect_identifier_like(LambdaRdParser* parser, const char* message) {
    if (token_is_key(parser->current.kind)) {
        parser_advance(parser);
        return true;
    }
    parser_set_error(parser, message, LAMBDA_TOK_IDENTIFIER);
    return false;
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
// parse_path_expr.cpp after their TSNode diagnostic dependency is replaced by
// LambdaSourceSpan.  This POC parser only owns their outer placement.
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
        if (kind == LAMBDA_TOK_NEWLINE) {
            // Annotation operators may bracket a formatting newline, but a
            // plain newline still belongs to content as a statement boundary.
            if (nesting || need_atom || parser->next.kind == LAMBDA_TOK_PIPE ||
                    parser->next.kind == LAMBDA_TOK_AMPERSAND ||
                    parser->next.kind == LAMBDA_TOK_BANG) {
                parser_advance(parser);
                continue;
            }
            break;
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
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_TYPE_SLOT, span, NULL, 0);
}

static bool parse_annotation_type_slot(LambdaRdParser* parser) {
    if (!parse_type_slot(parser)) return false;
    // Ranges and `that` predicates belong to annotation syntax rather than
    // the reusable pattern interior, so consume their outer expression here.
    if (parser_accept(parser, LAMBDA_TOK_TO)) {
        parser_skip_newlines(parser);
        if (!parse_expression(parser, 0)) return false;
    }
    if (parser_accept(parser, LAMBDA_TOK_THAT)) {
        parser_skip_newlines(parser);
        if (!parse_expression(parser, 0)) return false;
    }
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
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_TYPE_SLOT, span, NULL, 0);
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
    } else if (!parser_expect(parser, LAMBDA_TOK_DOT, "expected path introducer")) {
        return 0;
    }
    if (!parse_path_segment(parser)) return 0;
    while (parser_accept(parser, LAMBDA_TOK_DOT)) {
        if (!parse_path_segment(parser)) return 0;
    }
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_PATH_SLOT, span, NULL, 0);
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
                (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte},
                items, item);
            parser_skip_newlines(parser);
            if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
            parser_skip_newlines(parser);
        } while (true);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACKET, "expected ']' after array items")) return 0;
    }
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_ARRAY, span, items ? &items : NULL, items ? 1u : 0u);
}

static LambdaParseValue parse_map(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue items = 0;
    parser_advance(parser);
    parser_skip_newlines(parser);
    if (!parser_accept(parser, LAMBDA_TOK_RBRACE)) {
        do {
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
            LambdaSourceSpan item_span = {key.span.start_byte, parser->current.span.start_byte};
            LambdaParseValue item = parser_reduce(parser, LAMBDA_REDUCE_STATEMENT,
                item_span, &value, 1);
            items = parser_list_append(parser,
                (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte},
                items, item);
            parser_skip_newlines(parser);
            if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
            parser_skip_newlines(parser);
        } while (true);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after map items")) return 0;
    }
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
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
    LambdaToken first = parser->current;
    LambdaParseValue children[64];
    uint32_t count = 0;
    parser_advance(parser);
    if (!token_is_element_name(parser->current.kind)) {
        parser_set_error(parser, "expected an element tag", LAMBDA_TOK_IDENTIFIER);
        return 0;
    }
    parser_advance(parser);
    // S2.4.3v2: in tag position a qualified name is maximal.  A path-like
    // child therefore starts only after the explicit content boundary.
    while (parser_accept(parser, LAMBDA_TOK_DOT)) {
        if (!token_is_element_name(parser->current.kind)) {
            parser_set_error(parser, "expected a namespace segment after '.'", LAMBDA_TOK_IDENTIFIER);
            return 0;
        }
        parser_advance(parser);
    }
    for (;;) {
        parser_skip_newlines(parser);
        if (!element_attribute_starts(parser)) break;
        parser_advance(parser);
        while (parser_accept(parser, LAMBDA_TOK_DOT)) {
            if (!token_is_element_name(parser->current.kind)) {
                parser_set_error(parser, "expected an attribute namespace segment", LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            parser_advance(parser);
        }
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
        children[count++] = parse_expression(parser, 0);
        parser->stop_at_element_attribute_close--;
        parser->stop_at_element_close--;
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        parser_skip_newlines(parser);
        if (!element_attribute_starts(parser)) {
            parser_set_error(parser, "expected an attribute after ','", LAMBDA_TOK_IDENTIFIER);
            return 0;
        }
    }
    parser_accept(parser, LAMBDA_TOK_SEMICOLON);
    parser_accept(parser, LAMBDA_TOK_NEWLINE);
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
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_ELEMENT, span, children, count);
}

static LambdaParseValue parse_group_or_arrow(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue children[64];
    uint32_t count = 0;
    parser_advance(parser);
    parser_skip_newlines(parser);
    // A typed parameter can never be a normal parenthesized value expression,
    // so its local `name:` prefix commits directly to the arrow-head form.
    if (token_is_key(parser->current.kind) &&
            (parser->next.kind == LAMBDA_TOK_COLON || parser->next.kind == LAMBDA_TOK_QUESTION)) {
        do {
            if (!token_is_key(parser->current.kind)) {
                parser_set_error(parser, "expected an arrow parameter name", LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            parser_advance(parser);
            parser_accept(parser, LAMBDA_TOK_QUESTION);
            if (parser_accept(parser, LAMBDA_TOK_COLON)) {
                parser_skip_newlines(parser);
                if (!parse_annotation_type_slot(parser)) return 0;
            }
            if (parser_accept(parser, LAMBDA_TOK_EQ)) {
                parser_skip_newlines(parser);
                if (!parse_expression(parser, 0)) return 0;
            }
            count++;
            parser_skip_newlines(parser);
            if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
            parser_skip_newlines(parser);
        } while (true);
        if (!parser_expect(parser, LAMBDA_TOK_RPAREN, "expected ')' after arrow parameters")) return 0;
        if (token_starts_return_type(parser->current.kind) && !parse_type_slot(parser)) return 0;
        if (!parser_expect(parser, LAMBDA_TOK_ARROW, "expected '=>' after typed arrow parameters")) return 0;
        parser_skip_newlines(parser);
        LambdaParseValue body = parse_expression(parser, 0);
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        children[count++] = body;
        LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        return parser_reduce(parser, LAMBDA_REDUCE_FUNCTION, span, children, count);
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
    if (parser_accept(parser, LAMBDA_TOK_ARROW)) {
        parser_skip_newlines(parser);
        LambdaParseValue body = parse_expression(parser, 0);
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        children[count++] = body;
        LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        return parser_reduce(parser, LAMBDA_REDUCE_FUNCTION, span, children, count);
    }
    if (empty_group) {
        parser_set_error(parser, "expected an expression inside parentheses", LAMBDA_TOK_IDENTIFIER);
        return 0;
    }
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_GROUP, span, children, count);
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
    return token_is_key(probe.current.kind) && probe.next.kind == LAMBDA_TOK_COLON;
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
    if (parser->current.kind == LAMBDA_TOK_LBRACE && !braced_expression_is_map(parser)) {
        parser_advance(parser);
        children[1] = parse_content(parser, LAMBDA_TOK_RBRACE);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after if body")) return 0;
    } else {
        children[1] = parse_expression(parser, 0);
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    parser_skip_newlines(parser);
    if (!parser_expect(parser, LAMBDA_TOK_ELSE, "expected else branch")) return 0;
    parser_skip_newlines(parser);
    if (parser->current.kind == LAMBDA_TOK_LBRACE && !braced_expression_is_map(parser)) {
        parser_advance(parser);
        children[2] = parse_content(parser, LAMBDA_TOK_RBRACE);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after else body")) return 0;
    } else {
        children[2] = parse_expression(parser, 0);
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_IF, span, children, 3);
}

static bool parse_for_binding(LambdaRdParser* parser) {
    if (!token_is_identifier_like(parser->current.kind)) {
        parser_set_error(parser, "expected a for binding name", LAMBDA_TOK_IDENTIFIER);
        return false;
    }
    parser_advance(parser);
    parser_skip_newlines(parser);
    if (parser_accept(parser, LAMBDA_TOK_COLON)) {
        parser_skip_newlines(parser);
        if (!parse_annotation_type_slot(parser)) return false;
        if (!parser_expect(parser, LAMBDA_TOK_COMMA, "expected ',' after typed for index")) return false;
        parser_skip_newlines(parser);
        if (!token_is_identifier_like(parser->current.kind)) {
            parser_set_error(parser, "expected a for value name", LAMBDA_TOK_IDENTIFIER);
            return false;
        }
        parser_advance(parser);
    } else if (parser_accept(parser, LAMBDA_TOK_COMMA)) {
        parser_skip_newlines(parser);
        if (!token_is_identifier_like(parser->current.kind)) {
            parser_set_error(parser, "expected a for value name", LAMBDA_TOK_IDENTIFIER);
            return false;
        }
        parser_advance(parser);
    }
    parser_accept(parser, LAMBDA_TOK_QUESTION);
    if (parser->current.kind != LAMBDA_TOK_IN && parser->current.kind != LAMBDA_TOK_AT) {
        parser_set_error(parser, "expected 'in' or 'at' after for binding", LAMBDA_TOK_IN);
        return false;
    }
    parser_advance(parser);
    parser_skip_newlines(parser);
    if (!parse_expression(parser, 0)) return false;
    parser_skip_newlines(parser);
    if (parser_accept(parser, LAMBDA_TOK_ON)) {
        parser_skip_newlines(parser);
        if (!parse_expression(parser, 0)) return false;
    }
    return true;
}

static bool parse_for_let_clause(LambdaRdParser* parser) {
    if (!parser_expect(parser, LAMBDA_TOK_LET, "expected let clause")) return false;
    if (!token_is_identifier_like(parser->current.kind)) {
        parser_set_error(parser, "expected a for-let name", LAMBDA_TOK_IDENTIFIER);
        return false;
    }
    parser_advance(parser);
    if (!parser_expect(parser, LAMBDA_TOK_EQ, "expected '=' after for-let name")) return false;
    return parse_expression(parser, 0) != 0;
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

static bool parse_for_group_clause(LambdaRdParser* parser) {
    if (!parser_expect(parser, LAMBDA_TOK_BY, "expected 'by' after group")) return false;
    parser_skip_newlines(parser);
    do {
        if (!parse_expression(parser, LAMBDA_BP_POSTFIX)) return false;
        if (parser_accept(parser, LAMBDA_TOK_AS)) {
            parser_skip_newlines(parser);
            if (!token_is_identifier_like(parser->current.kind)) {
                parser_set_error(parser, "expected a group alias", LAMBDA_TOK_IDENTIFIER);
                return false;
            }
            parser_advance(parser);
        }
        parser_skip_newlines(parser);
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        parser_skip_newlines(parser);
    } while (true);
    if (!parser_expect(parser, LAMBDA_TOK_INTO, "expected 'into' after group keys")) return false;
    if (!token_is_identifier_like(parser->current.kind)) {
        parser_set_error(parser, "expected a group binding name", LAMBDA_TOK_IDENTIFIER);
        return false;
    }
    parser_advance(parser);
    return true;
}

static LambdaParseValue parse_for_expression(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue children[2];
    parser_advance(parser);
    bool parenthesized = parser_accept(parser, LAMBDA_TOK_LPAREN);
    if (parenthesized) parser_skip_newlines(parser);
    if (!parse_for_binding(parser)) return 0;
    parser_skip_newlines(parser);
    while (for_binding_list_continues(parser)) {
        parser_advance(parser);
        parser_skip_newlines(parser);
        if (!parse_for_binding(parser)) return 0;
        parser_skip_newlines(parser);
    }
    while (parser->status == LAMBDA_PARSE_OK) {
        parser_skip_newlines(parser);
        if (parser_accept(parser, LAMBDA_TOK_COMMA)) {
            parser_skip_newlines(parser);
            if (!parse_for_let_clause(parser)) return 0;
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_WHERE)) {
            parser_skip_newlines(parser);
            if (!parse_expression(parser, 0)) return 0;
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_GROUP)) {
            parser_skip_newlines(parser);
            if (!parse_for_group_clause(parser)) return 0;
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_ORDER)) {
            if (!parser_expect(parser, LAMBDA_TOK_BY, "expected 'by' after order")) return 0;
            parser_skip_newlines(parser);
            do {
                if (!parse_expression(parser, 0)) return 0;
                if (parser->current.kind == LAMBDA_TOK_ASC || parser->current.kind == LAMBDA_TOK_DESC) {
                    parser_advance(parser);
                }
                parser_skip_newlines(parser);
            } while (parser_accept(parser, LAMBDA_TOK_COMMA));
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_LIMIT)) {
            parser_accept(parser, LAMBDA_TOK_LAST);
            parser_skip_newlines(parser);
            if (!parse_expression(parser, 0)) return 0;
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_OFFSET)) {
            parser_skip_newlines(parser);
            if (!parse_expression(parser, 0)) return 0;
            continue;
        }
        break;
    }
    if (parenthesized && !parser_expect(parser, LAMBDA_TOK_RPAREN, "expected ')' after for clauses")) return 0;
    if (parenthesized) parser_skip_newlines(parser);
    if (parenthesized && parser->current.kind == LAMBDA_TOK_LBRACE &&
            braced_expression_is_map(parser)) {
        children[0] = parse_expression(parser, 0);
    } else if (parser_accept(parser, LAMBDA_TOK_LBRACE)) {
        children[0] = parse_content(parser, LAMBDA_TOK_RBRACE);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after for body")) return 0;
    } else if (parenthesized) {
        children[0] = parse_expression(parser, 0);
    } else {
        parser_set_error(parser, "expected '{' after for statement header", LAMBDA_TOK_LBRACE);
        return 0;
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    children[1] = parser_reduce(parser, LAMBDA_REDUCE_ATOM,
        (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte}, NULL, 0);
    return parser_reduce(parser, LAMBDA_REDUCE_FOR,
        (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte}, children, 2);
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
        if (parser_accept(parser, LAMBDA_TOK_CASE)) {
            if (!parse_annotation_type_slot(parser)) return 0;
        } else if (!parser_accept(parser, LAMBDA_TOK_DEFAULT)) {
            parser_set_error(parser, "expected case or default in match", LAMBDA_TOK_CASE);
            return 0;
        }
        LambdaParseValue body = 0;
        if (parser_accept(parser, LAMBDA_TOK_COLON)) {
            parser_skip_newlines(parser);
            body = parse_expression(parser, 0);
        } else if (parser_accept(parser, LAMBDA_TOK_LBRACE)) {
            body = parse_content(parser, LAMBDA_TOK_RBRACE);
            if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after match arm")) return 0;
        } else {
            parser_set_error(parser, "expected ':' or '{' after match arm", LAMBDA_TOK_COLON);
            return 0;
        }
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        LambdaParseValue arm = parser_reduce(parser, LAMBDA_REDUCE_STATEMENT,
            (LambdaSourceSpan){arm_first.span.start_byte, parser->current.span.start_byte}, &body, 1);
        arms = parser_list_append(parser,
            (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte}, arms, arm);
        arm_count++;
    }
    if (!arm_count) {
        parser_set_error(parser, "expected case or default in match", LAMBDA_TOK_CASE);
        return 0;
    }
    if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after match arms")) return 0;
    LambdaParseValue children[2] = {scrutinee, arms};
    return parser_reduce(parser, LAMBDA_REDUCE_MATCH,
        (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte}, children, 2);
}

static LambdaParseValue parse_while_statement(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    if (!parser_expect(parser, LAMBDA_TOK_LPAREN, "expected '(' after while")) return 0;
    LambdaParseValue condition = parse_expression(parser, 0);
    if (parser->status != LAMBDA_PARSE_OK ||
            !parser_expect(parser, LAMBDA_TOK_RPAREN, "expected ')' after while condition") ||
            !parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after while condition")) return 0;
    LambdaParseValue body = parse_content(parser, LAMBDA_TOK_RBRACE);
    if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after while body")) return 0;
    LambdaParseValue children[2] = {condition, body};
    return parser_reduce(parser, LAMBDA_REDUCE_FOR,
        (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte}, children, 2);
}

static LambdaParseValue parse_assignment_clause(LambdaRdParser* parser,
        const char* missing_name_message, const char* missing_equals_message) {
    LambdaToken first = parser->current;
    if (!token_is_key(parser->current.kind)) {
        parser_set_error(parser, missing_name_message, LAMBDA_TOK_IDENTIFIER);
        return 0;
    }
    parser_advance(parser);
    if (parser_accept(parser, LAMBDA_TOK_COLON)) {
        if (!parse_annotation_type_slot(parser)) return 0;
    }
    bool decomposed = false;
    while (parser_accept(parser, LAMBDA_TOK_COMMA)) {
        decomposed = true;
        if (!token_is_key(parser->current.kind)) {
            parser_set_error(parser, missing_name_message, LAMBDA_TOK_IDENTIFIER);
            return 0;
        }
        parser_advance(parser);
    }
    if (!parser_accept(parser, LAMBDA_TOK_EQ)) {
        if (!decomposed || !parser_accept(parser, LAMBDA_TOK_AT)) {
            parser_set_error(parser, missing_equals_message, LAMBDA_TOK_EQ);
            return 0;
        }
    }
    parser_skip_newlines(parser);
    LambdaParseValue value = parse_expression(parser, 0);
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_LET, span, &value, 1);
}

static LambdaParseValue parse_let_expression(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    LambdaParseValue value = parse_assignment_clause(parser,
        "expected a binding name after let", "expected '=' after let binding");
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    return parser_reduce(parser, LAMBDA_REDUCE_LET,
        (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte}, &value, 1);
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
        parser_advance(parser);
        value = parser_reduce(parser, LAMBDA_REDUCE_ATOM, first.span, NULL, 0);
    } else if (first.kind == LAMBDA_TOK_LPAREN) {
        value = parse_group_or_arrow(parser);
    } else if (first.kind == LAMBDA_TOK_LBRACKET) {
        value = parse_array(parser);
    } else if (first.kind == LAMBDA_TOK_LBRACE) {
        value = parse_map(parser);
    } else if (first.kind == LAMBDA_TOK_LT) {
        value = parse_element(parser);
    } else if (first.kind == LAMBDA_TOK_DOT || first.kind == LAMBDA_TOK_SLASH) {
        value = parse_path_slot(parser);
    } else if (first.kind == LAMBDA_TOK_NOT || first.kind == LAMBDA_TOK_BANG ||
            first.kind == LAMBDA_TOK_MINUS || first.kind == LAMBDA_TOK_PLUS ||
            first.kind == LAMBDA_TOK_STAR) {
        parser_advance(parser);
        LambdaParseValue child = parse_expression(parser, LAMBDA_BP_PREFIX);
        if (parser->status == LAMBDA_PARSE_OK) {
            LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            value = parser_reduce(parser, LAMBDA_REDUCE_PREFIX, span, &child, 1);
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
            LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            value = parser_reduce(parser, LAMBDA_REDUCE_PREFIX, span, &child, 1);
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
    if (kind == LAMBDA_TOK_GT && parser->stop_at_element_close &&
            (parser->stop_at_element_attribute_close ||
             parser->next.kind == LAMBDA_TOK_LT ||
             parser->next.kind == LAMBDA_TOK_STRING ||
             parser->next.kind == LAMBDA_TOK_LBRACE ||
             parser->next.kind == LAMBDA_TOK_DOT ||
             !token_starts_expression(parser->next.kind))) return -1;
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

static LambdaParseValue parse_postfix(LambdaRdParser* parser, LambdaParseValue left) {
    for (;;) {
        LambdaToken first = parser->current;
        LambdaParseValue children[65];
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
                            children[argument_count + 1] = parser_reduce(parser,
                                LAMBDA_REDUCE_STATEMENT,
                                (LambdaSourceSpan){argument_first.span.start_byte,
                                    parser->current.span.start_byte},
                                &children[argument_count + 1], 1);
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
            LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            left = parser_reduce(parser, LAMBDA_REDUCE_POSTFIX, span, children,
                argument_count + 1);
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
            LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            left = parser_reduce(parser, LAMBDA_REDUCE_POSTFIX, span, children, index_count + 1);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_DOT)) {
            if (!parse_path_segment(parser)) return 0;
            LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            left = parser_reduce(parser, LAMBDA_REDUCE_POSTFIX, span, children, 1);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_QUESTION) ||
                parser_accept(parser, LAMBDA_TOK_DOT_QUESTION)) {
            children[1] = parse_primary_type_slot(parser);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            left = parser_reduce(parser, LAMBDA_REDUCE_POSTFIX, span, children, 2);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_CARET)) {
            if (parser_accept(parser, LAMBDA_TOK_LBRACE)) {
                children[1] = parse_content(parser, LAMBDA_TOK_RBRACE);
                if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after handler body")) return 0;
                if (parser_accept(parser, LAMBDA_TOK_TILDE)) {
                    if (!parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after handler value marker")) return 0;
                    children[1] = parse_content(parser, LAMBDA_TOK_RBRACE);
                    if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after handler value")) return 0;
                }
            }
            LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            left = parser_reduce(parser, LAMBDA_REDUCE_POSTFIX, span, children, 2);
            continue;
        }
        return left;
    }
}

static LambdaParseValue parse_expression(LambdaRdParser* parser, int min_bp) {
    parser->expression_depth++;
    bool left_is_element = parser->current.kind == LAMBDA_TOK_LT;
    LambdaParseValue left = parse_prefix(parser);
    if (parser->status != LAMBDA_PARSE_OK) {
        parser->expression_depth--;
        return 0;
    }
    left = parse_postfix(parser, left);
    while (parser->status == LAMBDA_PARSE_OK) {
        if (parser->current.kind == LAMBDA_TOK_NEWLINE &&
                parser->next.kind != LAMBDA_TOK_LT && parser->next.kind != LAMBDA_TOK_GT) {
            bool newline_right_associative = false;
            if (infix_binding_power(parser, parser->next.kind,
                    &newline_right_associative) >= min_bp) {
                // An operator cannot begin an independent statement here, so
                // retain the expression across a formatting line break.
                parser_advance(parser);
            }
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
        LambdaParseValue right = parse_expression(parser, bp + (right_associative ? 0 : 1));
        if (parser->status != LAMBDA_PARSE_OK) {
            parser->expression_depth--;
            return 0;
        }
        if (op.kind == LAMBDA_TOK_LT && parser->expression_depth == 1) {
            parser->top_level_statement_relation = true;
        }
        LambdaParseValue children[2] = {left, right};
        LambdaSourceSpan span = {op.span.start_byte, parser->current.span.start_byte};
        left = parser_reduce(parser, LAMBDA_REDUCE_BINARY, span, children, 2);
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
    if (!parser_expect(parser, LAMBDA_TOK_LPAREN, "expected '(' before parameters")) return false;
    parser_skip_newlines(parser);
    if (parser_accept(parser, LAMBDA_TOK_RPAREN)) return true;
    do {
        if (parser_accept(parser, LAMBDA_TOK_ELLIPSIS)) break;
        parser_accept(parser, LAMBDA_TOK_VAR);
        if (!token_is_key(parser->current.kind)) {
            parser_set_error(parser, "expected a parameter name", LAMBDA_TOK_IDENTIFIER);
            return false;
        }
        parser_advance(parser);
        parser_accept(parser, LAMBDA_TOK_QUESTION);
        if (parser_accept(parser, LAMBDA_TOK_COLON)) {
            parser_skip_newlines(parser);
            if (!parse_annotation_type_slot(parser)) return false;
        }
        if (parser_accept(parser, LAMBDA_TOK_EQ)) {
            parser_skip_newlines(parser);
            if (!parse_expression(parser, 0)) return false;
        }
        parser_skip_newlines(parser);
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        parser_skip_newlines(parser);
    } while (true);
    return parser_expect(parser, LAMBDA_TOK_RPAREN, "expected ')' after parameters");
}

static LambdaParseValue parse_function_declaration(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    if (!token_is_key(parser->current.kind)) {
        parser_set_error(parser, "expected a function name", LAMBDA_TOK_IDENTIFIER);
        return 0;
    }
    parser_advance(parser);
    if (!parse_parameter_list(parser)) return 0;
    parser_skip_newlines(parser);
    if (token_starts_return_type(parser->current.kind)) {
        if (!parse_type_slot(parser)) return 0;
        if (parser_accept(parser, LAMBDA_TOK_CARET) && token_starts_return_type(parser->current.kind) &&
                !parse_type_slot(parser)) return 0;
    }
    LambdaParseValue child = 0;
    if (parser_accept(parser, LAMBDA_TOK_ARROW)) {
        parser_skip_newlines(parser);
        child = parse_expression(parser, 0);
    } else if (parser_accept(parser, LAMBDA_TOK_LBRACE)) {
        child = parse_content(parser, LAMBDA_TOK_RBRACE);
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after function body")) return 0;
    } else {
        parser_set_error(parser, "expected a function body", LAMBDA_TOK_LBRACE);
        return 0;
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_FUNCTION, span, &child, 1);
}

static LambdaParseValue parse_view_declaration(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    if (parser->current.kind == LAMBDA_TOK_IDENTIFIER && parser->next.kind == LAMBDA_TOK_COLON) {
        parser_advance(parser);
        parser_advance(parser);
    }
    if (!parse_type_slot(parser)) return 0;
    if (parser->current.kind == LAMBDA_TOK_LPAREN && !parse_parameter_list(parser)) return 0;
    if (token_starts_return_type(parser->current.kind)) {
        if (!parse_type_slot(parser)) return 0;
        if (parser_accept(parser, LAMBDA_TOK_CARET) && token_starts_return_type(parser->current.kind) &&
                !parse_type_slot(parser)) return 0;
    }
    if (parser_accept(parser, LAMBDA_TOK_STATE)) {
        do {
            if (!parser_expect(parser, LAMBDA_TOK_IDENTIFIER, "expected a state name")) return 0;
            if (!parser_expect(parser, LAMBDA_TOK_COLON, "expected ':' after state name")) return 0;
            parser_skip_newlines(parser);
            if (!parse_expression(parser, 0)) return 0;
        } while (parser_accept(parser, LAMBDA_TOK_COMMA));
    }
    if (!parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after view declaration")) return 0;
    LambdaParseValue body = parse_content(parser, LAMBDA_TOK_RBRACE);
    if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after view body")) return 0;
    while (parser->current.kind == LAMBDA_TOK_NEWLINE && parser->next.kind == LAMBDA_TOK_ON) {
        parser_advance(parser);
    }
    while (parser_accept(parser, LAMBDA_TOK_ON)) {
        if (!parser_expect(parser, LAMBDA_TOK_IDENTIFIER, "expected an event name")) return 0;
        if (!parse_parameter_list(parser)) return 0;
        if (!parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after event parameters")) return 0;
        if (!parse_content(parser, LAMBDA_TOK_RBRACE)) return 0;
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after event body")) return 0;
        while (parser->current.kind == LAMBDA_TOK_NEWLINE && parser->next.kind == LAMBDA_TOK_ON) {
            parser_advance(parser);
        }
    }
    return parser_reduce(parser, LAMBDA_REDUCE_DECLARATION,
        (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte}, &body, 1);
}

static LambdaParseValue parse_type_declaration(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    if (!token_is_key(parser->current.kind)) {
        parser_set_error(parser, "expected a type name", LAMBDA_TOK_IDENTIFIER);
        return 0;
    }
    parser_advance(parser);
    if (parser_accept(parser, LAMBDA_TOK_EQ)) {
        if (!parse_annotation_type_slot(parser)) return 0;
        while (parser_accept(parser, LAMBDA_TOK_COMMA)) {
            if (!token_is_key(parser->current.kind)) {
                parser_set_error(parser, "expected a type alias name", LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            parser_advance(parser);
            if (!parser_expect(parser, LAMBDA_TOK_EQ, "expected '=' after type alias name") ||
                    !parse_annotation_type_slot(parser)) return 0;
        }
    } else {
        if (parser_accept(parser, LAMBDA_TOK_COLON)) {
            if (!token_is_key(parser->current.kind)) {
                parser_set_error(parser, "expected an inherited type name", LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            parser_advance(parser);
        }
        if (!parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after object type name")) return 0;
        while (parser->status == LAMBDA_PARSE_OK && parser->current.kind != LAMBDA_TOK_RBRACE) {
            while (parser_accept(parser, LAMBDA_TOK_NEWLINE) || parser_accept(parser, LAMBDA_TOK_COMMA)) {}
            if (parser->current.kind == LAMBDA_TOK_RBRACE) break;
            if (parser_accept(parser, LAMBDA_TOK_SEMICOLON)) continue;
            if (parser->current.kind == LAMBDA_TOK_FN || parser->current.kind == LAMBDA_TOK_PN) {
                if (!parse_function_declaration(parser)) return 0;
                continue;
            }
            if (parser_accept(parser, LAMBDA_TOK_THAT)) {
                if (!parse_expression(parser, 0)) return 0;
                continue;
            }
            if (token_is_key(parser->current.kind) && parser->next.kind == LAMBDA_TOK_COLON) {
                parser_advance(parser);
                parser_advance(parser);
                parser_skip_newlines(parser);
                if (!parse_annotation_type_slot(parser)) return 0;
                if (parser_accept(parser, LAMBDA_TOK_EQ)) {
                    parser_skip_newlines(parser);
                    if (!parse_expression(parser, 0)) return 0;
                }
                continue;
            }
            // A bare type is the object/element content schema.  Its inner
            // grammar remains owned by the existing type-pattern parser.
            if (token_starts_type(parser->current.kind)) {
                if (!parse_type_slot(parser)) return 0;
                continue;
            }
            parser_set_error(parser, "expected an object type field, content type, or method",
                LAMBDA_TOK_IDENTIFIER);
            return 0;
        }
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after object type body")) return 0;
    }
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_DECLARATION, span, NULL, 0);
}

static LambdaParseValue parse_var_statement(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    do {
        if (!parse_assignment_clause(parser, "expected a mutable binding name",
                "expected '=' after mutable binding")) return 0;
    } while (parser_accept(parser, LAMBDA_TOK_COMMA));
    // Leave the optional `;` for parse_content so it remains the separator
    // between this declaration and the next statement.
    return parser_reduce(parser, LAMBDA_REDUCE_STATEMENT,
        (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte}, NULL, 0);
}

static bool newlines_lead_to(const LambdaRdParser* parser, LambdaTokenKind kind) {
    LambdaRdParser probe = *parser;
    probe.sink = NULL;
    probe.sink_context = NULL;
    probe.metrics = NULL;
    probe.error = NULL;
    parser_skip_newlines(&probe);
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind == kind;
}

static LambdaParseValue parse_if_statement(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    LambdaParseValue condition = parse_expression(parser, 0);
    if (parser->status != LAMBDA_PARSE_OK ||
            !parser_expect(parser, LAMBDA_TOK_LBRACE, "expected '{' after if condition")) return 0;
    LambdaParseValue body = parse_content(parser, LAMBDA_TOK_RBRACE);
    if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after if body")) return 0;
    LambdaParseValue children[2] = {condition, body};
    // Comment-only/blank lines still emit several newlines in this lexer;
    // keep them attached to `else` rather than ending the block-if statement.
    while (parser->current.kind == LAMBDA_TOK_NEWLINE &&
            newlines_lead_to(parser, LAMBDA_TOK_ELSE)) {
        parser_advance(parser);
    }
    if (parser_accept(parser, LAMBDA_TOK_ELSE)) {
        parser_skip_newlines(parser);
        if (parser->current.kind == LAMBDA_TOK_IF) {
            // An `else if` arm can be the value form without braces; reuse
            // the same branch probe as a top-level `if` to avoid forcing it
            // through the block-statement parser.
            children[1] = if_starts_block_statement(parser)
                ? parse_if_statement(parser)
                : parse_if_expression(parser);
        } else if (parser_accept(parser, LAMBDA_TOK_LBRACE)) {
            children[1] = parse_content(parser, LAMBDA_TOK_RBRACE);
            if (!parser_expect(parser, LAMBDA_TOK_RBRACE, "expected '}' after else body")) return 0;
        } else {
            children[1] = parse_expression(parser, 0);
        }
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    parser->last_statement_self_delimiting = true;
    return parser_reduce(parser, LAMBDA_REDUCE_IF, span, children, 2);
}

static LambdaParseValue parse_statement(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser->last_statement_self_delimiting = false;
    parser->last_statement_assignment = false;
    if (parser_accept(parser, LAMBDA_TOK_IMPORT)) {
        do {
            if (token_is_key(parser->current.kind) && parser->next.kind == LAMBDA_TOK_COLON) {
                parser_advance(parser);
                parser_advance(parser);
            }
            if (parser_accept(parser, LAMBDA_TOK_DOT) || parser_accept(parser, LAMBDA_TOK_SLASH)) {
                if (!parser_expect_identifier_like(parser, "expected a relative import component")) return 0;
            } else if (token_is_key(parser->current.kind)) {
                parser_advance(parser);
            } else {
                parser_set_error(parser, "expected an import module", LAMBDA_TOK_IDENTIFIER);
                return 0;
            }
            while (parser_accept(parser, LAMBDA_TOK_DOT) || parser_accept(parser, LAMBDA_TOK_SLASH)) {
                if (!parser_expect_identifier_like(parser, "expected an import component")) return 0;
            }
        } while (parser_accept(parser, LAMBDA_TOK_COMMA));
    } else {
        bool is_public = parser_accept(parser, LAMBDA_TOK_PUB);
        (void)is_public;
        if (parser->current.kind == LAMBDA_TOK_FN || parser->current.kind == LAMBDA_TOK_PN) {
            return parse_function_declaration(parser);
        }
        if (parser->current.kind == LAMBDA_TOK_VIEW || parser->current.kind == LAMBDA_TOK_EDIT) {
            return parse_view_declaration(parser);
        }
        // `type` is both the declaration introducer and the base-type value.
        // A following `(` is a normal call such as `type(value)`.
        if (parser->current.kind == LAMBDA_TOK_TYPE &&
                parser->next.kind != LAMBDA_TOK_LPAREN &&
                parser->next.kind != LAMBDA_TOK_NEWLINE &&
                parser->next.kind != LAMBDA_TOK_SEMICOLON &&
                parser->next.kind != LAMBDA_TOK_EOF) {
            return parse_type_declaration(parser);
        }
        if (parser->current.kind == LAMBDA_TOK_VAR) {
            return parse_var_statement(parser);
        }
        if (parser->current.kind == LAMBDA_TOK_APPLY && parser->next.kind == LAMBDA_TOK_SEMICOLON) {
            // Content owns statement separators; consuming only `apply` keeps
            // a following statement from becoming adjacent to this one.
            parser_advance(parser);
            return parser_reduce(parser, LAMBDA_REDUCE_STATEMENT,
                (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte}, NULL, 0);
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
            while (parser_accept(parser, LAMBDA_TOK_COMMA)) {
                parser_skip_newlines(parser);
                if (!parse_assignment_clause(parser, "expected a binding name after let",
                        "expected '=' after let binding")) return 0;
            }
            LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            return parser_reduce(parser, LAMBDA_REDUCE_STATEMENT, span, &let_value, 1);
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
            return parser_reduce(parser, LAMBDA_REDUCE_DECLARATION,
                (LambdaSourceSpan){first.span.start_byte, parser->current.span.start_byte},
                &public_value, 1);
        }
        if (parser->current.kind == LAMBDA_TOK_RETURN || parser->current.kind == LAMBDA_TOK_BREAK ||
                parser->current.kind == LAMBDA_TOK_CONTINUE) {
            parser_advance(parser);
            if (token_starts_expression(parser->current.kind)) {
                if (!parse_expression(parser, 0)) return 0;
            }
        } else {
            parser->top_level_statement_relation = false;
            LambdaParseValue expr = parse_expression(parser, 0);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            if (parser_accept(parser, LAMBDA_TOK_EQ)) {
                parser->last_statement_assignment = true;
                parser_skip_newlines(parser);
                if (!parse_expression(parser, 0)) return 0;
            }
            if (parser->top_level_statement_relation && !parser->last_statement_assignment) {
                parser_set_error(parser, "comparison requires parentheses at statement scope",
                    LAMBDA_TOK_NEWLINE);
                return 0;
            }
            LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            return parser_reduce(parser, LAMBDA_REDUCE_STATEMENT, span, &expr, 1);
        }
    }
    LambdaSourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_DECLARATION, span, NULL, 0);
}

static bool element_content_starts_sibling(const LambdaRdParser* parser) {
    LambdaRdParser probe = *parser;
    probe.sink = NULL;
    probe.sink_context = NULL;
    probe.metrics = NULL;
    probe.error = NULL;
    LambdaParseValue child = parse_prefix(&probe);
    if (probe.status != LAMBDA_PARSE_OK) return false;
    (void)parse_postfix(&probe, child);
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind == LAMBDA_TOK_LT;
}

static bool assignment_statement_starts(const LambdaRdParser* parser) {
    LambdaRdParser probe = *parser;
    probe.sink = NULL;
    probe.sink_context = NULL;
    probe.metrics = NULL;
    probe.error = NULL;
    (void)parse_expression(&probe, 0);
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind == LAMBDA_TOK_EQ;
}

static LambdaParseValue parse_content(LambdaRdParser* parser, LambdaTokenKind terminator) {
    LambdaParseValue last = 0;
    while (parser->status == LAMBDA_PARSE_OK && parser->current.kind != terminator &&
            parser->current.kind != LAMBDA_TOK_EOF) {
        while (parser_accept(parser, LAMBDA_TOK_NEWLINE) || parser_accept(parser, LAMBDA_TOK_SEMICOLON)) {}
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
            child = parse_postfix(parser, child);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            last = parser_reduce(parser, LAMBDA_REDUCE_STATEMENT,
                (LambdaSourceSpan){child_first.span.start_byte, parser->current.span.start_byte},
                &child, 1);
            continue;
        }
        LambdaTokenKind statement_first = parser->current.kind;
        last = parse_statement(parser);
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        if (parser->last_statement_self_delimiting && parser->current.kind == LAMBDA_TOK_FOR) {
            // `if cond { ... } for ... { ... }` is two unambiguous content
            // statements; the grammar does not require a separator between
            // their closing/opening structural delimiters.
            continue;
        }
        if (parser->last_statement_assignment && assignment_statement_starts(parser)) {
            // `assign_stam` has an optional semicolon, so a procedural block
            // may place the next unambiguous assignment directly after a call.
            continue;
        }
        if ((statement_first == LAMBDA_TOK_STRING || statement_first == LAMBDA_TOK_LBRACE ||
                statement_first == LAMBDA_TOK_LT) &&
                (parser->current.kind == LAMBDA_TOK_STRING ||
                 parser->current.kind == LAMBDA_TOK_LBRACE ||
                 parser->current.kind == LAMBDA_TOK_LT)) {
            // String/map content may repeat directly at document or element
            // scope; scalar statements still require an explicit separator.
            continue;
        }
        if (parser->current.kind != terminator && parser->current.kind != LAMBDA_TOK_EOF &&
                parser->current.kind != LAMBDA_TOK_NEWLINE && parser->current.kind != LAMBDA_TOK_SEMICOLON) {
            parser_set_error(parser, "expected a statement separator", LAMBDA_TOK_NEWLINE);
            return 0;
        }
    }
    return last;
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
    parser.current = lambda_lexer_next(&parser.lexer);
    parser.next = lambda_lexer_next(&parser.lexer);
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
        LambdaSourceSpan span = {0, (uint32_t)length};
        parser_reduce(&parser, LAMBDA_REDUCE_DOCUMENT, span, &content, content ? 1u : 0u);
    }
    return parser.status;
}
