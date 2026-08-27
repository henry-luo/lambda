#include "lambda_rd_parser.h"
#include <string.h>
enum {
    LAMBDA_RD_MAX_DEPTH = 1000, LAMBDA_BP_PIPE = 10, LAMBDA_BP_OR = 20, LAMBDA_BP_AND = 30, LAMBDA_BP_NOT = 35, LAMBDA_BP_MEMBERSHIP = 40, LAMBDA_BP_SET = 50, LAMBDA_BP_EQUALITY = 60, LAMBDA_BP_RELATION = 70, LAMBDA_BP_ADD = 80, LAMBDA_BP_MULTIPLY = 90, LAMBDA_BP_POWER = 100, LAMBDA_BP_PREFIX = 105, LAMBDA_BP_POSTFIX = 110,
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
} LambdaRdParser;

// keep parser diagnostics in one catalog. Call sites select a named message
// so wording can change or be translated without editing grammar flows.
/* generic and limit diagnostics */
static const char* const error_invalid_token = "invalid token";
static const char* const error_source_size_limit = "source is null or exceeds the parser size limit";
static const char* const error_maximum_parser_nesting = "maximum parser nesting exceeded";
static const char* const error_type_delimiter_nesting = "type pattern delimiter nesting exceeded";
static const char* const error_incomplete_type_pattern = "incomplete type pattern";
static const char* const error_incomplete_type_occurrence_suffix = "incomplete type occurrence suffix";
static const char* const error_incomplete_primary_type = "incomplete primary type";
static const char* const error_expected_expression = "expected an expression";
static const char* const error_unexpected_trailing_input = "unexpected trailing input";
static const char* const error_too_many_grouped_expressions = "too many grouped expressions in parser POC";
static const char* const error_too_many_call_arguments = "too many call arguments in parser POC";
static const char* const error_too_many_index_dimensions = "too many index dimensions in parser POC";

/* type, path, map, and element diagnostics */
static const char* const error_expected_type_pattern = "expected a type pattern";
static const char* const error_expected_type_pattern_after_operator = "expected a type pattern after type operator";
static const char* const error_expected_primary_type = "expected a primary type";
static const char* const error_expected_path_segment = "expected a path segment";
static const char* const error_map_string_key =
    "a map key is a symbol, not a string: write a bare name "
    "like {key: 1}, or single-quote it when it is not a name "
    "like {'data-node-id': 1}";
static const char* const error_expected_map_key = "expected a map key";
static const char* const error_expected_element_tag = "expected an element tag";
static const char* const error_expected_namespace_segment = "expected a namespace segment after '.'";
static const char* const error_expected_attribute_namespace_segment = "expected an attribute namespace segment";
static const char* const error_too_many_element_attributes = "too many element attributes in parser POC";
static const char* const error_element_no_attribute_comma = "an element with no attributes takes no ',' before its content";
static const char* const error_element_semicolon_content =
    "';' cannot open element content; a tag is followed directly by its "
    "content, and ';' only separates one content item from the next";
static const char* const error_element_expected_content_comma = "expected ',' between element attributes and content";
static const char* const error_element_trailing_comma = "trailing ',' is not a separator";
static const char* const error_too_many_element_content = "too many element content items in parser POC";

/* expression, control-flow, and pattern diagnostics */
static const char* const error_expected_arrow_parameter_name = "expected an arrow parameter name";
static const char* const error_expected_arrow_parameter_close = "expected ')' after arrow parameters";
static const char* const error_expected_parameter_name = "expected a parameter name";
static const char* const error_expected_parameter_close = "expected ')' after parameters";
static const char* const error_expected_expression_inside_parentheses = "expected an expression inside parentheses";
static const char* const error_arrow_body_expression =
    "'return', 'break', and 'continue' are statements; an arrow '=>' body is an expression";
static const char* const error_if_condition_close = "expected ')' after if condition";
static const char* const error_if_body_close = "expected '}' after if body";
static const char* const error_else_body_close = "expected '}' after else body";
static const char* const error_unbraced_if_body =
    "'return', 'break', and 'continue' are statements; an unbraced 'if' body "
    "is an expression - write 'if (cond) { ... }'";
static const char* const error_unbraced_else_body =
    "'return', 'break', and 'continue' are statements; an unbraced 'else' body "
    "is an expression - write 'else { ... }'";
static const char* const error_expected_for_binding_name = "expected a for binding name";
static const char* const error_expected_for_value_name = "expected a for value name";
static const char* const error_expected_for_binding_source = "expected 'in' or 'at' after for binding";
static const char* const error_expected_for_let_name = "expected a for-let name";
static const char* const error_expected_group_alias = "expected a group alias";
static const char* const error_expected_group_binding_name = "expected a group binding name";
static const char* const error_for_body_close = "expected '}' after for body";
static const char* const error_unbraced_for_body =
    "'return', 'break', and 'continue' are statements; an unbraced 'for' body "
    "is an expression - write 'for (...) { ... }'";
static const char* const error_for_body_open = "expected '{' after for statement header";
static const char* const error_control_body_open = "expected '{' before body";
static const char* const error_expected_match_arm = "expected case or default in match";
static const char* const error_match_arm_close = "expected '}' after match arm";
static const char* const error_match_arm_open = "expected '{' after match arm";
static const char* const error_match_arm_separator = "expected ':' or '{' after match arm";
static const char* const error_match_expression_body =
    "'return', 'break', and 'continue' are statements; a 'case T:' arm "
    "takes an expression - write 'case T { ... }'";
static const char* const error_while_condition_close = "expected ')' after while condition";
static const char* const error_while_body_open = "expected '{' after while condition";
static const char* const error_while_body_close = "expected '}' after while body";
static const char* const error_too_many_decomposition_names = "too many decomposition names";
static const char* const error_not_logical_negation = "'!' is not logical negation here; use 'not'";

/* declarations, handlers, and statement diagnostics */
static const char* const error_handler_same_line = "a handler body must open on the same line as its '^'";
static const char* const error_handler_body_open = "expected '{' after handler marker";
static const char* const error_handler_body_close = "expected '}' after handler body";
static const char* const error_handler_value_open = "expected '{' after handler value marker";
static const char* const error_handler_value_close = "expected '}' after handler value";
static const char* const error_expected_function_name = "expected a function name";
static const char* const error_procedure_body =
    "a procedure body is a statement block - write 'pn name() { ... }'; '=>' bodies are fn-only";
static const char* const error_function_body_open = "expected '{' after function declaration";
static const char* const error_function_body_close = "expected '}' after function body";
static const char* const error_expected_function_body = "expected a function body";
static const char* const error_expected_state_name = "expected a state name";
static const char* const error_expected_event_name = "expected an event name";
static const char* const error_view_body_open = "expected '{' after view declaration";
static const char* const error_view_body_close = "expected '}' after view body";
static const char* const error_event_body_open = "expected '{' after event parameters";
static const char* const error_event_body_close = "expected '}' after event body";
static const char* const error_expected_type_alias_name = "expected a type alias name";
static const char* const error_type_alias_equals = "expected '=' after type alias name";
static const char* const error_expected_type_name = "expected a type name";
static const char* const error_expected_inherited_type_name = "expected an inherited type name";
static const char* const error_object_type_separator = "object-type members are separated by ',', not ';'";
static const char* const error_object_field_colon = "expected ':' after object-type field name";
static const char* const error_expected_object_type_member = "expected an object type field, content type, or method";
static const char* const error_block_body_open = "expected '{' after block";
static const char* const error_block_body_close = "expected '}' after block";
static const char* const error_expected_relative_import_component = "expected a relative import component";
static const char* const error_expected_import_module = "expected an import module";
static const char* const error_expected_import_component = "expected an import component";
static const char* const error_pub_declaration = "'pub' modifies a declaration; write 'pub let'";
// S16.10.1: an import alias is a binding, so it takes no keyword and no
// quoted spelling — a quoted use site would be a symbol, which never reads a
// binding (S2.4.3). Rejecting here reports the import line itself instead of
// letting every later use fail.
static const char* const error_import_alias_reserved =
    "an import alias must be a plain identifier, not a keyword or quoted symbol";
static const char* const error_statement_relation_parentheses = "comparison requires parentheses at statement scope";
static const char* const error_trailing_statement_separator = "trailing ';' is not a statement separator";
static const char* const error_empty_statement_between_separators = "empty statement between ';' separators";
static const char* const error_empty_block_statement = "an empty '{}' statement has no effect";
static const char* const error_line_continuation =
    "this token cannot continue the previous line; write ';' to "
    "start a new statement, or move it to the end of that line";
static const char* const error_expected_statement_separator = "expected a statement separator";
static const char* const error_expected_binding_name_after_let = "expected a binding name after let";
static const char* const error_expected_equals_after_let = "expected '=' after let binding";
static const char* const error_expected_mutable_binding_name = "expected a mutable binding name";
static const char* const error_expected_equals_after_mutable_binding = "expected '=' after mutable binding";
static const char* const error_expected_public_binding_name = "expected a public binding name";
static const char* const error_expected_equals_after_public_binding = "expected '=' after public binding";

/* canonical token expectations */
static const char* const error_expected_lparen = "expected '('";
static const char* const error_expected_rparen = "expected ')'";
static const char* const error_expected_lbracket = "expected '['";
static const char* const error_expected_rbracket = "expected ']'";
static const char* const error_expected_lbrace = "expected '{'";
static const char* const error_expected_rbrace = "expected '}'";
static const char* const error_expected_comma = "expected ','";
static const char* const error_expected_colon = "expected ':'";
static const char* const error_expected_semicolon = "expected ';'";
static const char* const error_expected_dot = "expected '.'";
static const char* const error_expected_arrow = "expected '=>'";
static const char* const error_expected_let = "expected 'let'";
static const char* const error_expected_by = "expected 'by'";
static const char* const error_expected_into = "expected 'into'";
static const char* const error_expected_equals = "expected '='";
static const char* const error_expected_greater_than = "expected '>'";
static const char* const error_expected_path_introducer = "expected path introducer";
static const char* const error_expected_token = "expected token";

static uint64_t mix_hash(uint64_t hash, uint64_t value) {
    hash ^= value + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) + (hash >> 2);
    return hash;
}

static LambdaParseValue parser_reduce_detail_ex(LambdaRdParser* parser, LambdaReductionKind kind, LambdaReductionForm form, SourceSpan span, LambdaToken detail_token, LambdaToken secondary_token, uint32_t flags, const LambdaToken* name_tokens, uint32_t name_count, const LambdaParseValue* children, uint32_t child_count) {
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
        LambdaParseValue sink_value = parser->sink->reduce(parser->sink_context, &reduction);
        if (sink_value) value = sink_value;
    }
    if (parser->metrics) {
        parser->metrics->reduction_count++;
        parser->metrics->structural_hash = mix_hash(parser->metrics->structural_hash, value);
    }
    return value;
}

static LambdaParseValue parser_reduce_detail(LambdaRdParser* parser, LambdaReductionKind kind, LambdaReductionForm form, SourceSpan span, LambdaToken detail, const LambdaParseValue* children, uint32_t count) {
    return parser_reduce_detail_ex(parser, kind, form, span, detail, (LambdaToken){0}, 0, NULL, 0, children, count);
}

static LambdaParseValue parser_reduce(LambdaRdParser* parser, LambdaReductionKind kind, SourceSpan span, const LambdaParseValue* children, uint32_t count) {
    return parser_reduce_detail(parser, kind, LAMBDA_REDUCTION_FORM_NONE, span, (LambdaToken){0}, children, count);
}

static LambdaParseValue parser_reduce_token(LambdaRdParser* parser, LambdaReductionKind kind, LambdaReductionForm form, SourceSpan span, LambdaToken detail, const LambdaParseValue* children, uint32_t count) {
    return parser_reduce_detail(parser, kind, form, span, detail, children, count);
}

static LambdaParseValue parser_reduce_tokens(LambdaRdParser* parser, LambdaReductionKind kind, LambdaReductionForm form, SourceSpan span, LambdaToken detail, LambdaToken secondary, uint32_t flags, const LambdaParseValue* children, uint32_t count) {
    return parser_reduce_detail_ex(parser, kind, form, span, detail, secondary, flags, NULL, 0, children, count);
}

static LambdaParseValue parser_reduce_name_tokens(LambdaRdParser* parser, LambdaReductionKind kind, SourceSpan span, LambdaToken first, const LambdaToken* names, uint32_t name_count, uint32_t flags, const LambdaParseValue* children, uint32_t count) {
    return parser_reduce_detail_ex(parser, kind, LAMBDA_REDUCTION_FORM_DECOMPOSE, span, first, (LambdaToken){0}, flags, names, name_count, children, count);
}

static LambdaParseValue parser_reduce_one(LambdaRdParser* parser, LambdaReductionKind kind, LambdaReductionForm form, SourceSpan span, LambdaToken detail, LambdaParseValue child) {
    return parser_reduce_token(parser, kind, form, span, detail, &child, 1);
}

static LambdaParseValue parser_reduce_one_ex(LambdaRdParser* parser, LambdaReductionKind kind, LambdaReductionForm form, SourceSpan span, LambdaToken detail, LambdaToken secondary, uint32_t flags, LambdaParseValue child) {
    return parser_reduce_tokens(parser, kind, form, span, detail, secondary, flags, &child, 1);
}

static LambdaParseValue parser_reduce_plain_one(LambdaRdParser* parser, LambdaReductionKind kind, SourceSpan span, LambdaParseValue child) {
    return parser_reduce(parser, kind, span, &child, 1);
}

static void parser_context(LambdaRdParser* parser, LambdaReductionForm form, SourceSpan span, LambdaToken detail) {
    parser_reduce_token(parser, LAMBDA_REDUCE_CONTEXT, form, span, detail, NULL, 0);
}

static void parser_context_ex(LambdaRdParser* parser, LambdaReductionForm form, SourceSpan span, LambdaToken detail, LambdaToken secondary, uint32_t flags, const LambdaParseValue* children, uint32_t child_count) {
    parser_reduce_tokens(parser, LAMBDA_REDUCE_CONTEXT, form, span, detail, secondary, flags, children, child_count);
}

static LambdaParseValue parser_append_list(LambdaRdParser* parser, SourceSpan span, LambdaParseValue list, LambdaParseValue item, LambdaReductionForm form) {
    if (!list) {
        return parser_reduce_one(parser, LAMBDA_REDUCE_LIST, form, span, (LambdaToken){0}, item);
    }
    LambdaParseValue children[2] = {list, item};
    return parser_reduce_token(parser, LAMBDA_REDUCE_LIST, form, span, (LambdaToken){0}, children, 2);
}

#define parser_list_append(...) \
    parser_append_list(__VA_ARGS__, LAMBDA_REDUCTION_FORM_NONE)
#define parser_for_clause_append(...) \
    parser_append_list(__VA_ARGS__, LAMBDA_REDUCTION_FORM_FOR_CLAUSES)
#define parser_content_append(...) \
    parser_append_list(__VA_ARGS__, LAMBDA_REDUCTION_FORM_CONTENT)
#define parser_parameter_append(...) \
    parser_append_list(__VA_ARGS__, LAMBDA_REDUCTION_FORM_PARAMETERS)

static void parser_set_error(LambdaRdParser* parser, const char* message, LambdaTokenKind expected) {
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

static LambdaParseValue parser_fail(LambdaRdParser* parser, const char* message, LambdaTokenKind expected) {
    parser_set_error(parser, message, expected);
    return 0;
}

static void parser_prepare_probe(LambdaRdParser* probe) {
    probe->sink = NULL;
    probe->sink_context = NULL;
    probe->metrics = NULL;
    probe->error = NULL;
}

static LambdaRdParser parser_probe(const LambdaRdParser* parser) {
    LambdaRdParser probe = *parser;
    parser_prepare_probe(&probe);
    return probe;
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
    return kind == LAMBDA_TOK_LPAREN || kind == LAMBDA_TOK_LBRACKET || kind == LAMBDA_TOK_PLUS || kind == LAMBDA_TOK_MINUS || kind == LAMBDA_TOK_STAR || kind == LAMBDA_TOK_SLASH || kind == LAMBDA_TOK_CARET || kind == LAMBDA_TOK_LT || kind == LAMBDA_TOK_DOT;
}

static bool token_is_control_statement(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_RETURN || kind == LAMBDA_TOK_BREAK || kind == LAMBDA_TOK_CONTINUE;
}

static bool token_is_dot_led_number(const LambdaRdParser* parser, const LambdaToken* token) {
    return (token->kind == LAMBDA_TOK_FLOAT || token->kind == LAMBDA_TOK_DECIMAL) && token->span.start_byte < parser->lexer.length && parser->lexer.source[token->span.start_byte] == '.';
}

static void parser_advance(LambdaRdParser* parser) {
    if (parser->status != LAMBDA_PARSE_OK) return;
    parser->prev_kind = parser->current.kind;
    parser->current = parser->next;
    parser->next = parser_next_significant(parser);
    if (parser->metrics) parser->metrics->token_count++;
    if (parser->current.kind == LAMBDA_TOK_ERROR) {
        parser_set_error(parser, error_invalid_token, LAMBDA_TOK_EOF);
    }
}

static bool parser_accept(LambdaRdParser* parser, LambdaTokenKind kind) {
    if (parser->current.kind != kind) return false;
    parser_advance(parser);
    return true;
}

// generic token diagnostics live here so parser_expect call sites only name
// the token they require. contextual wording remains opt-in below.
static const char* const parser_expected_messages[LAMBDA_TOK_ELLIPSIS + 1] = {
    [LAMBDA_TOK_LPAREN] = error_expected_lparen,
    [LAMBDA_TOK_RPAREN] = error_expected_rparen,
    [LAMBDA_TOK_LBRACKET] = error_expected_lbracket,
    [LAMBDA_TOK_RBRACKET] = error_expected_rbracket,
    [LAMBDA_TOK_LBRACE] = error_expected_lbrace,
    [LAMBDA_TOK_RBRACE] = error_expected_rbrace,
    [LAMBDA_TOK_COMMA] = error_expected_comma,
    [LAMBDA_TOK_COLON] = error_expected_colon,
    [LAMBDA_TOK_SEMICOLON] = error_expected_semicolon,
    [LAMBDA_TOK_DOT] = error_expected_dot,
    [LAMBDA_TOK_ARROW] = error_expected_arrow,
    [LAMBDA_TOK_LET] = error_expected_let,
    [LAMBDA_TOK_BY] = error_expected_by,
    [LAMBDA_TOK_INTO] = error_expected_into,
    [LAMBDA_TOK_EQ] = error_expected_equals,
    [LAMBDA_TOK_GT] = error_expected_greater_than,
    [LAMBDA_TOK_PATH_REL] = error_expected_path_introducer,
};

static const char* parser_expected_message(LambdaTokenKind kind) {
    if ((unsigned int)kind <= (unsigned int)LAMBDA_TOK_ELLIPSIS && parser_expected_messages[kind]) {
        return parser_expected_messages[kind];
    }
    return error_expected_token;
}

static bool parser_expect_message(LambdaRdParser* parser, LambdaTokenKind kind, const char* message) {
    if (parser_accept(parser, kind)) return true;
    return parser_fail(parser, message, kind);
}

static bool parser_expect(LambdaRdParser* parser, LambdaTokenKind kind) {
    return parser_expect_message(parser, kind, parser_expected_message(kind));
}

static bool token_is_identifier_like(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_IDENTIFIER || kind == LAMBDA_TOK_DIV || kind == LAMBDA_TOK_STATE || kind == LAMBDA_TOK_APPLY || kind == LAMBDA_TOK_VIEW || kind == LAMBDA_TOK_EDIT || kind == LAMBDA_TOK_ON || kind == LAMBDA_TOK_AND || kind == LAMBDA_TOK_OR || kind == LAMBDA_TOK_TO || kind == LAMBDA_TOK_IS || kind == LAMBDA_TOK_IN || kind == LAMBDA_TOK_AT || kind == LAMBDA_TOK_THAT || kind == LAMBDA_TOK_WHERE || kind == LAMBDA_TOK_ORDER || kind == LAMBDA_TOK_BY || kind == LAMBDA_TOK_GROUP || kind == LAMBDA_TOK_INTO || kind == LAMBDA_TOK_LIMIT || kind == LAMBDA_TOK_OFFSET || kind == LAMBDA_TOK_ASC || kind == LAMBDA_TOK_DESC || kind == LAMBDA_TOK_AS || kind == LAMBDA_TOK_EQ_WORD || kind == LAMBDA_TOK_NE_WORD || kind == LAMBDA_TOK_LT_WORD || kind == LAMBDA_TOK_LE_WORD || kind == LAMBDA_TOK_GE_WORD || kind == LAMBDA_TOK_GT_WORD;
}

static bool token_is_identifier(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_IDENTIFIER;
}

// S16.10.2: data-name positions admit every keyword spelling. Both the key
// and element-name predicates derive from this one set so a tag, an
// attribute, and a map key can never drift apart as keywords are added.
static bool token_is_name_word(LambdaTokenKind kind) {
    return token_is_identifier_like(kind) || kind == LAMBDA_TOK_BASE_TYPE || kind == LAMBDA_TOK_TYPE || kind == LAMBDA_TOK_LET || kind == LAMBDA_TOK_PUB || kind == LAMBDA_TOK_VAR || kind == LAMBDA_TOK_FN || kind == LAMBDA_TOK_PN || kind == LAMBDA_TOK_IF || kind == LAMBDA_TOK_ELSE || kind == LAMBDA_TOK_MATCH || kind == LAMBDA_TOK_CASE || kind == LAMBDA_TOK_DEFAULT || kind == LAMBDA_TOK_LAST || kind == LAMBDA_TOK_FOR || kind == LAMBDA_TOK_WHILE || kind == LAMBDA_TOK_BREAK || kind == LAMBDA_TOK_CONTINUE || kind == LAMBDA_TOK_RETURN || kind == LAMBDA_TOK_RAISE || kind == LAMBDA_TOK_IMPORT;
}

static bool token_is_key(LambdaTokenKind kind) {
    return token_is_name_word(kind) || kind == LAMBDA_TOK_SYMBOL || kind == LAMBDA_TOK_STAR;
}

static bool token_is_element_name(LambdaTokenKind kind) {
    return token_is_name_word(kind) || kind == LAMBDA_TOK_SYMBOL;
}

static bool token_is_literal(LambdaTokenKind kind) {
    return (kind >= LAMBDA_TOK_INTEGER && kind <= LAMBDA_TOK_PATTERN_ISLAND) || kind == LAMBDA_TOK_BASE_TYPE || kind == LAMBDA_TOK_TYPE || kind == LAMBDA_TOK_APPLY;
}

static bool token_starts_type(LambdaTokenKind kind) {
    return token_is_literal(kind) || token_is_identifier_like(kind) || kind == LAMBDA_TOK_LPAREN || kind == LAMBDA_TOK_LBRACKET || kind == LAMBDA_TOK_LBRACE || kind == LAMBDA_TOK_LT || kind == LAMBDA_TOK_FN || kind == LAMBDA_TOK_BANG;
}

static bool token_starts_return_type(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_IDENTIFIER || kind == LAMBDA_TOK_BASE_TYPE || kind == LAMBDA_TOK_TYPE;
}

static bool token_starts_expression(LambdaTokenKind kind) {
    return token_is_literal(kind) || token_is_identifier_like(kind) || kind == LAMBDA_TOK_FN || kind == LAMBDA_TOK_LAST || kind == LAMBDA_TOK_LPAREN || kind == LAMBDA_TOK_LBRACKET || kind == LAMBDA_TOK_LBRACE || kind == LAMBDA_TOK_LT || kind == LAMBDA_TOK_DOT || kind == LAMBDA_TOK_SLASH || kind == LAMBDA_TOK_TILDE || kind == LAMBDA_TOK_TILDE_INDEX || kind == LAMBDA_TOK_PARENT || kind == LAMBDA_TOK_CARET || kind == LAMBDA_TOK_ELLIPSIS || kind == LAMBDA_TOK_NOT || kind == LAMBDA_TOK_BANG || kind == LAMBDA_TOK_MINUS || kind == LAMBDA_TOK_PLUS || kind == LAMBDA_TOK_STAR || kind == LAMBDA_TOK_LET || kind == LAMBDA_TOK_IF || kind == LAMBDA_TOK_MATCH || kind == LAMBDA_TOK_FOR || kind == LAMBDA_TOK_RAISE;
}

typedef bool (*LambdaTokenKindPredicate)(LambdaTokenKind kind);

static bool parser_take_name(LambdaRdParser* parser, LambdaTokenKindPredicate predicate, const char* message, LambdaToken* name_out) {
    if (!predicate(parser->current.kind)) {
        parser_fail(parser, message, LAMBDA_TOK_IDENTIFIER);
        return false;
    }
    *name_out = parser->current;
    parser_advance(parser);
    return true;
}

static const LambdaTokenKind type_closer[LAMBDA_TOK_ELLIPSIS + 1] = {
    [LAMBDA_TOK_LPAREN] = LAMBDA_TOK_RPAREN,
    [LAMBDA_TOK_LBRACKET] = LAMBDA_TOK_RBRACKET,
    [LAMBDA_TOK_LBRACE] = LAMBDA_TOK_RBRACE,
    [LAMBDA_TOK_LT] = LAMBDA_TOK_GT,
};

static LambdaTokenKind type_delimiter_close(LambdaTokenKind kind) {
    return kind <= LAMBDA_TOK_ELLIPSIS && type_closer[kind]
        ? type_closer[kind] : LAMBDA_TOK_ERROR;
}

static bool parser_push_type_delimiter(LambdaRdParser* parser, LambdaTokenKind* stack, uint32_t* depth, LambdaTokenKind opener) {
    LambdaTokenKind close = type_delimiter_close(opener);
    if (close == LAMBDA_TOK_ERROR) return true;
    if (*depth == 128) {
        parser_fail(parser, error_type_delimiter_nesting, close);
        return false;
    }
    stack[(*depth)++] = close;
    return true;
}

static bool parser_enter(LambdaRdParser* parser) {
    parser->depth++;
    if (parser->metrics && parser->depth > parser->metrics->max_recursion_depth) {
        parser->metrics->max_recursion_depth = parser->depth;
    }
    if (parser->depth <= LAMBDA_RD_MAX_DEPTH) return true;
    return parser_fail(parser, error_maximum_parser_nesting, LAMBDA_TOK_EOF);
}

static void parser_leave(LambdaRdParser* parser) {
    if (parser->depth) parser->depth--;
}

static LambdaParseValue parse_expression(LambdaRdParser* parser, int min_bp);
static LambdaParseValue parse_if_expression(LambdaRdParser* parser);
static LambdaParseValue parse_if_statement(LambdaRdParser* parser);
static bool if_starts_block_statement(const LambdaRdParser* parser);
static bool parser_parse_scoped_expression(LambdaRdParser* parser, uint32_t procedural_depth, LambdaParseValue* value_out);

static bool parser_consume_balanced(LambdaRdParser* parser, LambdaTokenKind opener, LambdaTokenKind closer, const char* message) {
    uint32_t nesting = 0;
    do {
        if (parser->current.kind == opener) nesting++;
        if (parser->current.kind == closer && nesting) nesting--;
        parser_advance(parser);
        if (parser->current.kind == LAMBDA_TOK_EOF && nesting) {
            return parser_fail(parser, message, closer);
        }
    } while (nesting);
    return true;
}

typedef LambdaParseValue (*LambdaParseItemFn)(LambdaRdParser* parser);

static LambdaParseValue parser_parse_expression_item(LambdaRdParser* parser) {
    return parse_expression(parser, 0);
}

static bool parser_parse_expression_value(LambdaRdParser* parser, int min_bp, LambdaParseValue* value_out) {
    *value_out = parse_expression(parser, min_bp);
    return parser->status == LAMBDA_PARSE_OK;
}

static bool parser_parse_scoped_expression(LambdaRdParser* parser, uint32_t procedural_depth, LambdaParseValue* value_out) {
    uint32_t saved_depth = parser->procedural_depth;
    parser->procedural_depth = procedural_depth;
    bool result = parser_parse_expression_value(parser, 0, value_out);
    parser->procedural_depth = saved_depth;
    return result;
}

static bool parser_parse_expression_list(LambdaRdParser* parser, LambdaTokenKind closer, LambdaParseValue* children, uint32_t limit, uint32_t* count, bool allow_empty, LambdaParseItemFn parse_item, const char* too_many) {
    *count = 0;
    if (allow_empty && parser_accept(parser, closer)) return true;
    do {
        if (*count == limit) {
            return parser_fail(parser, too_many, closer);
        }
        children[(*count)++] = parse_item(parser);
        if (parser->status != LAMBDA_PARSE_OK) return false;
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
    } while (true);
    return parser_expect(parser, closer);
}

static LambdaParseValue parse_content(LambdaRdParser* parser, LambdaTokenKind terminator);

static bool parser_parse_braced(LambdaRdParser* parser, LambdaToken marker, LambdaReductionForm begin_form, LambdaReductionForm end_form, const char* open_message, const char* close_message, LambdaParseValue* value_out) {
    bool opened = open_message ? parser_expect_message(parser, LAMBDA_TOK_LBRACE, open_message) : parser_accept(parser, LAMBDA_TOK_LBRACE);
    if (!opened) return false;
    if (begin_form != LAMBDA_REDUCTION_FORM_NONE) parser_context(parser, begin_form, marker.span, marker);
    LambdaParseValue value = parse_content(parser, LAMBDA_TOK_RBRACE);
    if (!parser_expect_message(parser, LAMBDA_TOK_RBRACE, close_message)) return false;
    if (end_form != LAMBDA_REDUCTION_FORM_NONE) parser_context(parser, end_form, marker.span, marker);
    if (value_out) *value_out = value;
    return parser->status == LAMBDA_PARSE_OK;
}

static bool parser_parse_plain_braced(LambdaRdParser* parser, const char* open_message, const char* close_message, LambdaParseValue* value_out) {
    return parser_parse_braced(parser, (LambdaToken){0}, LAMBDA_REDUCTION_FORM_NONE,
        LAMBDA_REDUCTION_FORM_NONE, open_message, close_message, value_out);
}

static bool parser_parse_scoped_braced(LambdaRdParser* parser, uint32_t procedural_depth, const char* open_message, const char* close_message, LambdaParseValue* value_out) {
    uint32_t saved_depth = parser->procedural_depth;
    parser->procedural_depth = procedural_depth;
    bool result = parser_parse_plain_braced(parser, open_message, close_message, value_out);
    parser->procedural_depth = saved_depth;
    return result;
}

static bool parser_extend_element_name(LambdaRdParser* parser, LambdaToken* name, const char* message) {
    while (parser_accept(parser, LAMBDA_TOK_DOT)) {
        if (!token_is_element_name(parser->current.kind))
            return parser_fail(parser, message, LAMBDA_TOK_IDENTIFIER);
        parser_advance(parser);
        name->span.end_byte = parser->current.span.start_byte;
    }
    return true;
}

static LambdaParseValue parse_type_slot(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (!token_starts_type(first.kind)) {
        return parser_fail(parser, error_expected_type_pattern, LAMBDA_TOK_BASE_TYPE);
    }
    LambdaTokenKind closing_stack[128];
    uint32_t nesting = 0;
    bool need_atom = true;
    bool function_type = first.kind == LAMBDA_TOK_FN;
    while (parser->status == LAMBDA_PARSE_OK) {
        LambdaTokenKind kind = parser->current.kind;
        if (kind == LAMBDA_TOK_EOF && nesting) {
            return parser_fail(parser, error_incomplete_type_pattern, closing_stack[nesting - 1]);
        }
        if (parser->current.nl_before && !nesting && !need_atom) {
            if (kind != LAMBDA_TOK_PIPE && kind != LAMBDA_TOK_AMPERSAND && kind != LAMBDA_TOK_BANG) {
                break;
            }
        }
        bool function_arguments = !need_atom && !nesting && function_type && kind == LAMBDA_TOK_LPAREN;
        if (need_atom || function_arguments) {
            if (need_atom && kind == LAMBDA_TOK_BANG) {
                parser_advance(parser);
                continue;
            }
            if (!function_arguments && !token_starts_type(kind)) break;
            if (!parser_push_type_delimiter(parser, closing_stack, &nesting, kind)) return 0;
            parser_advance(parser);
            need_atom = false;
            continue;
        }
        if (nesting && kind == closing_stack[nesting - 1]) {
            nesting--;
            parser_advance(parser);
            if (function_type && !nesting) need_atom = true;
            continue;
        }
        if (nesting) {
            if (!parser_push_type_delimiter(parser, closing_stack, &nesting, kind)) return 0;
            parser_advance(parser);
            continue;
        }
        if (kind == LAMBDA_TOK_QUESTION || kind == LAMBDA_TOK_PLUS || kind == LAMBDA_TOK_STAR) {
            parser_advance(parser);
            continue;
        }
        if (kind == LAMBDA_TOK_LBRACKET) {
            if (!parser_consume_balanced(parser, LAMBDA_TOK_LBRACKET, LAMBDA_TOK_RBRACKET, error_incomplete_type_occurrence_suffix)) return 0;
            continue;
        }
        if (kind == LAMBDA_TOK_PIPE || kind == LAMBDA_TOK_AMPERSAND || kind == LAMBDA_TOK_BANG) {
            parser_advance(parser);
            need_atom = true;
            continue;
        }
        break;
    }
    if (need_atom) {
        return parser_fail(parser, error_expected_type_pattern_after_operator, LAMBDA_TOK_BASE_TYPE);
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce_token(parser, LAMBDA_REDUCE_TYPE_SLOT, LAMBDA_REDUCTION_FORM_TOKEN, span, first, NULL, 0);
}

static bool parse_annotation_type_slot_value(LambdaRdParser* parser, LambdaParseValue* value_out) {
    LambdaToken first = parser->current;
    LambdaParseValue value = parse_type_slot(parser);
    if (!value) return false;
    if (parser_accept(parser, LAMBDA_TOK_TO)) {
        if (!parse_expression(parser, 0)) return false;
        SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        value = parser_reduce_token(parser, LAMBDA_REDUCE_TYPE_SLOT, LAMBDA_REDUCTION_FORM_TOKEN, span, first, NULL, 0);
    }
    if (parser_accept(parser, LAMBDA_TOK_THAT)) {
        LambdaParseValue constraint;
        if (!parser_parse_expression_value(parser, 0, &constraint)) return false;
        LambdaParseValue children[2] = {value, constraint};
        SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        value = parser_reduce_tokens(parser, LAMBDA_REDUCE_TYPE_SLOT, LAMBDA_REDUCTION_FORM_TOKEN, span, first, (LambdaToken){0}, LAMBDA_REDUCTION_FLAG_ANNOTATION_CONSTRAINT, children, 2);
    }
    if (value_out) *value_out = value;
    return true;
}

static LambdaParseValue parse_primary_type_slot(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (!token_starts_type(first.kind)) {
        return parser_fail(parser, error_expected_primary_type, LAMBDA_TOK_BASE_TYPE);
    }
    LambdaTokenKind close = type_delimiter_close(first.kind);
    if (close == LAMBDA_TOK_ERROR) {
        parser_advance(parser);
    } else {
        if (!parser_consume_balanced(parser, first.kind, close,
                error_incomplete_primary_type)) return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce_token(parser, LAMBDA_REDUCE_TYPE_SLOT, LAMBDA_REDUCTION_FORM_TOKEN, span, first, NULL, 0);
}

static bool parse_path_segment(LambdaRdParser* parser) {
    if (!token_is_key(parser->current.kind) && parser->current.kind != LAMBDA_TOK_PARENT && parser->current.kind != LAMBDA_TOK_SLASH && parser->current.kind != LAMBDA_TOK_INTEGER && parser->current.kind != LAMBDA_TOK_STAR_STAR) {
        return parser_fail(parser, error_expected_path_segment, LAMBDA_TOK_IDENTIFIER);
    }
    parser_advance(parser);
    return true;
}

static LambdaParseValue parse_path_slot(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (parser_accept(parser, LAMBDA_TOK_SLASH)) {
        if (!parser_expect(parser, LAMBDA_TOK_DOT)) return 0;
    } else if (!parser_expect(parser, LAMBDA_TOK_PATH_REL)) {
        // §7.15: the relative introducer is `\.`, not a bare dot.
        return 0;
    }
    if (!parse_path_segment(parser)) return 0;
    while (parser_accept(parser, LAMBDA_TOK_DOT)) {
        if (!parse_path_segment(parser)) return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce_token(parser, LAMBDA_REDUCE_PATH_SLOT, LAMBDA_REDUCTION_FORM_TOKEN, span, first, NULL, 0);
}

static LambdaParseValue parse_array(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue items = 0;
    parser_advance(parser);
    if (!parser_accept(parser, LAMBDA_TOK_RBRACKET)) {
        for (;;) {
            LambdaParseValue item;
            if (!parser_parse_expression_value(parser, 0, &item)) return 0;
            items = parser_list_append(parser, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, items, item);
            if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        }
        if (!parser_expect(parser, LAMBDA_TOK_RBRACKET)) return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_ARRAY, span, items ? &items : NULL, items ? 1u : 0u);
}

static LambdaParseValue parse_map(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue items = 0;
    parser_advance(parser);
    if (!parser_accept(parser, LAMBDA_TOK_RBRACE)) {
        for (;;) {
            if (parser->current.kind == LAMBDA_TOK_STRING) {
                return parser_fail(parser, error_map_string_key, LAMBDA_TOK_IDENTIFIER);
            }
            LambdaToken key;
            if (!parser_take_name(parser, token_is_key, error_expected_map_key, &key)) return 0;
            if (!parser_expect(parser, LAMBDA_TOK_COLON)) return 0;
            LambdaParseValue value;
            if (!parser_parse_expression_value(parser, 0, &value)) return 0;
            SourceSpan item_span = {key.span.start_byte, parser->current.span.start_byte};
            LambdaParseValue item = parser_reduce_one(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_MAP_ITEM, item_span, key, value);
            items = parser_list_append(parser, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, items, item);
            if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        }
        if (!parser_expect(parser, LAMBDA_TOK_RBRACE)) return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_MAP, span, items ? &items : NULL, items ? 1u : 0u);
}

static bool element_attribute_starts(const LambdaRdParser* parser) {
    LambdaRdParser probe = parser_probe(parser);
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
        return parser_fail(parser, error_expected_element_tag, LAMBDA_TOK_IDENTIFIER);
    }
    tag = parser->current;
    parser_advance(parser);
    if (!parser_extend_element_name(parser, &tag,
            error_expected_namespace_segment)) return 0;
    for (;;) {
        if (!element_attribute_starts(parser)) break;
        LambdaToken attribute_name = parser->current;
        parser_advance(parser);
        if (!parser_extend_element_name(parser, &attribute_name,
                error_expected_attribute_namespace_segment)) return 0;
        parser_advance(parser);
        if (count == 64) {
            return parser_fail(parser, error_too_many_element_attributes, LAMBDA_TOK_GT);
        }
        parser->stop_at_element_close++;
        parser->stop_at_element_attribute_close++;
        LambdaParseValue value = parse_expression(parser, 0);
        parser->stop_at_element_attribute_close--;
        parser->stop_at_element_close--;
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        SourceSpan attribute_span = {attribute_name.span.start_byte, parser->current.span.start_byte};
        children[count++] = parser_reduce_one(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_ELEMENT_ATTRIBUTE, attribute_span, attribute_name, value);
        had_attributes = true;
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
        if (!element_attribute_starts(parser)) {
            boundary_comma = true;
            break;
        }
    }
    if (!had_attributes && parser->current.kind == LAMBDA_TOK_COMMA) {
        return parser_fail(parser, error_element_no_attribute_comma, LAMBDA_TOK_GT);
    }
    if (!had_attributes && parser->current.kind == LAMBDA_TOK_SEMICOLON) {
        return parser_fail(parser, error_element_semicolon_content, LAMBDA_TOK_GT);
    }
    if (had_attributes && !boundary_comma && parser->current.kind != LAMBDA_TOK_GT) {
        return parser_fail(parser, error_element_expected_content_comma, LAMBDA_TOK_COMMA);
    }
    if (boundary_comma && parser->current.kind == LAMBDA_TOK_GT) {
        return parser_fail(parser, error_element_trailing_comma, LAMBDA_TOK_GT);
    }
    if (parser->current.kind != LAMBDA_TOK_GT) {
        if (count == 64) {
            return parser_fail(parser, error_too_many_element_content, LAMBDA_TOK_GT);
        }
        parser->stop_at_element_close++;
        children[count++] = parse_content(parser, LAMBDA_TOK_GT);
        parser->stop_at_element_close--;
        if (parser->status != LAMBDA_PARSE_OK) return 0;
    }
    if (!parser_expect(parser, LAMBDA_TOK_GT)) return 0;
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce_token(parser, LAMBDA_REDUCE_ELEMENT, LAMBDA_REDUCTION_FORM_TOKEN, span, tag, children, count);
}

static bool parse_parameter_items(LambdaRdParser* parser, bool allow_variadic, const char* name_message, const char* close_message, LambdaParseValue* value_out, bool* variadic_out);

typedef struct LambdaCallableSignature {
    LambdaParseValue parameters;
    LambdaParseValue return_types[2];
    uint32_t return_count;
    bool variadic;
    bool raised;
} LambdaCallableSignature;

static bool parser_parse_return_types(LambdaRdParser* parser, LambdaParseValue* values, uint32_t* count, bool* raised_out) {
    bool raised = false;
    if (token_starts_return_type(parser->current.kind)) {
        LambdaParseValue returned = parse_type_slot(parser);
        if (!returned) return false;
        if (values && count) values[(*count)++] = returned;
        if (parser_accept(parser, LAMBDA_TOK_CARET)) {
            raised = true;
            if (token_starts_return_type(parser->current.kind)) {
                LambdaParseValue error_type = parse_type_slot(parser);
                if (!error_type) return false;
                if (values && count) values[(*count)++] = error_type;
            }
        }
    }
    if (raised_out) *raised_out = raised;
    return true;
}

static bool parse_parameter_items(LambdaRdParser* parser, bool allow_variadic, const char* name_message, const char* close_message, LambdaParseValue* value_out, bool* variadic_out) {
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
        LambdaToken name;
        if (!parser_take_name(parser, token_is_key, name_message, &name)) return false;
        bool optional = parser_accept(parser, LAMBDA_TOK_QUESTION);
        LambdaParseValue type_value = 0;
        if (parser_accept(parser, LAMBDA_TOK_COLON) && !parse_annotation_type_slot_value(parser, &type_value)) return false;
        LambdaParseValue default_value = 0;
        if (parser_accept(parser, LAMBDA_TOK_EQ)) {
            if (!parser_parse_expression_value(parser, 0, &default_value)) return false;
            optional = true;
        }
        LambdaParseValue parameter_children[2];
        uint32_t child_count = 0;
        if (type_value) parameter_children[child_count++] = type_value;
        if (default_value) parameter_children[child_count++] = default_value;
        LambdaParseValue parameter = parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_PARAMETER, (SourceSpan){name.span.start_byte, parser->current.span.start_byte}, name, (LambdaToken){0}, (optional ? LAMBDA_REDUCTION_FLAG_OPTIONAL : 0u) |
                (is_var ? LAMBDA_REDUCTION_FLAG_VAR : 0u), parameter_children, child_count);
        parameters = parser_parameter_append(parser, (SourceSpan){name.span.start_byte, parser->current.span.start_byte}, parameters, parameter);
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
    } while (true);
    if (!parser_expect_message(parser, LAMBDA_TOK_RPAREN, close_message)) return false;
    if (value_out) *value_out = parameters;
    return true;
}

static bool parser_parse_callable_signature(LambdaRdParser* parser, bool opener_consumed, bool allow_variadic, const char* name_message, const char* close_message, LambdaCallableSignature* signature) {
    memset(signature, 0, sizeof(*signature));
    if (!opener_consumed && !parser_expect(parser, LAMBDA_TOK_LPAREN)) return false;
    if (!parse_parameter_items(parser, allow_variadic, name_message, close_message,
            &signature->parameters, &signature->variadic)) return false;
    return parser_parse_return_types(parser, signature->return_types,
        &signature->return_count, &signature->raised);
}

static bool arrow_head_candidate(const LambdaRdParser* parser) {
    LambdaRdParser probe = parser_probe(parser);
    LambdaCallableSignature signature;
    if (!parser_parse_callable_signature(&probe, true, false,
            error_expected_arrow_parameter_name,
            error_expected_arrow_parameter_close, &signature)) return false;
    return probe.current.kind == LAMBDA_TOK_ARROW;
}

static bool parser_parse_arrow_body(LambdaRdParser* parser, LambdaParseValue* value_out) {
    if (!parser_expect(parser, LAMBDA_TOK_ARROW)) return false;
    if (token_is_control_statement(parser->current.kind)) {
        parser_fail(parser, error_arrow_body_expression, LAMBDA_TOK_LBRACE);
        return false;
    }
    return parser_parse_scoped_expression(parser, 0, value_out);
}

static LambdaParseValue parse_group_or_arrow(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue children[64] = {0};
    uint32_t count = 0;
    parser_advance(parser);
    parser_context(parser, LAMBDA_REDUCTION_FORM_GROUP_BEGIN, first.span, first);
    if (arrow_head_candidate(parser)) {
        parser_context(parser, LAMBDA_REDUCTION_FORM_FUNCTION_BEGIN, first.span, first);
        LambdaCallableSignature signature;
        if (!parser_parse_callable_signature(parser, true, false,
                error_expected_arrow_parameter_name,
                error_expected_arrow_parameter_close, &signature)) return 0;
        children[count++] = signature.parameters;
        for (uint32_t i = 0; i < signature.return_count; i++) {
            children[count++] = signature.return_types[i];
        }
        if (!parser_parse_arrow_body(parser, &children[count++])) return 0;
        SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        LambdaParseValue result = parser_reduce_tokens(parser, LAMBDA_REDUCE_FUNCTION, LAMBDA_REDUCTION_FORM_FUNCTION, span, first, (LambdaToken){0}, signature.raised ? LAMBDA_REDUCTION_FLAG_RAISED : 0u, children, count);
        parser_context(parser, LAMBDA_REDUCTION_FORM_FUNCTION_END, first.span, first);
        parser_context(parser, LAMBDA_REDUCTION_FORM_GROUP_END, first.span, first);
        return result;
    }
    bool empty_group = parser_accept(parser, LAMBDA_TOK_RPAREN);
    if (!empty_group) {
        if (!parser_parse_expression_list(parser, LAMBDA_TOK_RPAREN, children, 64, &count, false, parser_parse_expression_item,
                error_too_many_grouped_expressions)) return 0;
    }
    parser_context(parser, LAMBDA_REDUCTION_FORM_GROUP_END, first.span, first);
    if (empty_group) {
        return parser_fail(parser, error_expected_expression_inside_parentheses, LAMBDA_TOK_IDENTIFIER);
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce_token(parser, LAMBDA_REDUCE_GROUP, LAMBDA_REDUCTION_FORM_GROUP, span, first, children, count);
}

static bool braced_expression_is_map(const LambdaRdParser* parser) {
    LambdaRdParser probe = parser_probe(parser);
    if (probe.current.kind != LAMBDA_TOK_LBRACE) return false;
    parser_advance(&probe);
    if (probe.next.kind != LAMBDA_TOK_COLON) return false;
    return token_is_key(probe.current.kind) || probe.current.kind == LAMBDA_TOK_STRING;
}

static bool control_body_brace_is_map(const LambdaRdParser* parser) {
    if (braced_expression_is_map(parser)) return true;
    return parser->current.kind == LAMBDA_TOK_LBRACE && parser->next.kind == LAMBDA_TOK_RBRACE && parser->procedural_depth == 0;
}

static bool parser_parse_control_body(LambdaRdParser* parser, LambdaToken marker, LambdaReductionForm begin_form, LambdaReductionForm end_form, const char* close_message, const char* expression_message, const char* missing_message, bool allow_expression, bool statement_context, LambdaParseValue* value_out) {
    if (statement_context && parser->current.kind == LAMBDA_TOK_IF) {
        *value_out = if_starts_block_statement(parser) ? parse_if_statement(parser) : parse_if_expression(parser);
        return parser->status == LAMBDA_PARSE_OK;
    }
    if (parser->current.kind == LAMBDA_TOK_LBRACE && (statement_context || !control_body_brace_is_map(parser))) {
        if (begin_form == LAMBDA_REDUCTION_FORM_NONE) {
            return parser_parse_plain_braced(parser, error_control_body_open, close_message, value_out);
        }
        return parser_parse_braced(parser, marker, begin_form, end_form, NULL, close_message, value_out);
    }
    if (!allow_expression) return parser_fail(parser, missing_message, LAMBDA_TOK_LBRACE);
    if (token_is_control_statement(parser->current.kind)) {
        return parser_fail(parser, expression_message, LAMBDA_TOK_LBRACE);
    }
    *value_out = parse_expression(parser, 0);
    return parser->status == LAMBDA_PARSE_OK;
}

static bool parser_parse_condition(LambdaRdParser* parser, const char* close_message, LambdaParseValue* value_out) {
    bool parenthesized = parser_accept(parser, LAMBDA_TOK_LPAREN);
    if (!parser_parse_expression_value(parser, 0, value_out)) return false;
    return !parenthesized || parser_expect_message(parser, LAMBDA_TOK_RPAREN, close_message);
}

static LambdaParseValue parse_if_expression(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue children[3];
    parser_advance(parser);
    if (!parser_parse_condition(parser, error_if_condition_close, &children[0])) return 0;
    if (!parser_parse_control_body(parser, first, LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN,
            LAMBDA_REDUCTION_FORM_IF_BRANCH_END, error_if_body_close,
            error_unbraced_if_body, NULL, true, false, &children[1])) return 0;
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    if (parser->current.kind != LAMBDA_TOK_ELSE) {
        SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
        return parser_reduce(parser, LAMBDA_REDUCE_IF, span, children, 2);
    }
    parser_advance(parser);
    if (!parser_parse_control_body(parser, first, LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN,
            LAMBDA_REDUCTION_FORM_IF_BRANCH_END, error_else_body_close,
            error_unbraced_else_body, NULL, true, false, &children[2])) return 0;
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_IF, span, children, 3);
}

static LambdaParseValue parse_for_binding(LambdaRdParser* parser) {
    LambdaToken name;
    LambdaToken index = {0};
    LambdaParseValue children[3];
    uint32_t child_count = 0;
    uint32_t flags = 0;
    if (!parser_take_name(parser, token_is_identifier_like,
            error_expected_for_binding_name, &name)) return 0;
    bool has_index = parser_accept(parser, LAMBDA_TOK_COLON);
    if (has_index) {
        LambdaParseValue index_type = 0;
        if (!parse_annotation_type_slot_value(parser, &index_type)) return 0;
        flags |= LAMBDA_REDUCTION_FLAG_INDEX_TYPED;
        children[child_count++] = index_type;
        if (!parser_expect(parser, LAMBDA_TOK_COMMA)) return 0;
    } else {
        has_index = parser_accept(parser, LAMBDA_TOK_COMMA);
    }
    if (has_index) {
        index = name;
        if (!parser_take_name(parser, token_is_identifier_like,
                error_expected_for_value_name, &name)) return 0;
    }
    if (parser_accept(parser, LAMBDA_TOK_QUESTION)) flags |= LAMBDA_REDUCTION_FLAG_OPTIONAL;
    if (parser->current.kind != LAMBDA_TOK_IN && parser->current.kind != LAMBDA_TOK_AT) {
        return parser_fail(parser, error_expected_for_binding_source, LAMBDA_TOK_IN);
    }
    if (parser->current.kind == LAMBDA_TOK_AT) flags |= LAMBDA_REDUCTION_FLAG_KEY_ONLY;
    parser_advance(parser);
    LambdaParseValue source;
    if (!parser_parse_expression_value(parser, 0, &source)) return 0;
    children[child_count++] = source;
    if (parser_accept(parser, LAMBDA_TOK_ON)) {
        LambdaParseValue join;
        if (!parser_parse_expression_value(parser, 0, &join)) return 0;
        children[child_count++] = join;
    }
    return parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_FOR_BINDING, (SourceSpan){name.span.start_byte, parser->current.span.start_byte}, name, index, flags, children, child_count);
}

static LambdaParseValue parse_for_let_clause(LambdaRdParser* parser) {
    if (!parser_expect(parser, LAMBDA_TOK_LET)) return 0;
    LambdaToken name;
    if (!parser_take_name(parser, token_is_identifier_like,
            error_expected_for_let_name, &name)) return 0;
    if (!parser_expect(parser, LAMBDA_TOK_EQ)) return 0;
    LambdaParseValue value;
    if (!parser_parse_expression_value(parser, 0, &value)) return 0;
    return parser_reduce_one(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_FOR_LET, (SourceSpan){name.span.start_byte, parser->current.span.start_byte}, name, value);
}

static bool for_binding_list_continues(const LambdaRdParser* parser) {
    if (parser->current.kind != LAMBDA_TOK_COMMA) return false;
    LambdaRdParser probe = parser_probe(parser);
    parser_advance(&probe);
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind != LAMBDA_TOK_LET;
}

static void parser_append_for_clause(LambdaRdParser* parser, SourceSpan list_span, LambdaParseValue* clauses, LambdaReductionForm form, LambdaToken detail, uint32_t flags, LambdaParseValue value) {
    LambdaParseValue clause = parser_reduce_one_ex(parser, LAMBDA_REDUCE_STATEMENT, form, list_span, detail, (LambdaToken){0}, flags, value);
    *clauses = parser_for_clause_append(parser, list_span, *clauses, clause);
}

static bool parser_add_for_value_clause(LambdaRdParser* parser, SourceSpan list_span, LambdaParseValue* clauses, LambdaReductionForm form, LambdaToken detail, uint32_t flags) {
    LambdaParseValue value;
    if (!parser_parse_expression_value(parser, 0, &value)) return false;
    parser_append_for_clause(parser, list_span, clauses, form, detail, flags, value);
    return true;
}

static LambdaParseValue parse_for_group_clause(LambdaRdParser* parser) {
    if (!parser_expect(parser, LAMBDA_TOK_BY)) return false;
    LambdaParseValue keys = 0;
    do {
        LambdaToken key_first = parser->current;
        LambdaParseValue key;
        if (!parser_parse_expression_value(parser, LAMBDA_BP_POSTFIX, &key)) return 0;
        LambdaToken alias = {0};
        if (parser_accept(parser, LAMBDA_TOK_AS)) {
            if (!parser_take_name(parser, token_is_identifier_like,
                    error_expected_group_alias, &alias)) return 0;
        }
        LambdaParseValue key_item = parser_reduce_one(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_FOR_GROUP_KEY, (SourceSpan){key_first.span.start_byte, parser->current.span.start_byte}, alias, key);
        keys = parser_list_append(parser, (SourceSpan){key_first.span.start_byte, parser->current.span.start_byte}, keys, key_item);
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) break;
    } while (true);
    if (!parser_expect(parser, LAMBDA_TOK_INTO)) return 0;
    LambdaToken name;
    if (!parser_take_name(parser, token_is_identifier_like,
            error_expected_group_binding_name, &name)) return 0;
    return parser_reduce_one(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_FOR_GROUP, (SourceSpan){name.span.start_byte, parser->current.span.start_byte}, name, keys);
}

// mirror grammar.js's _loop_head: bindings, local lets, then trailing clauses.
static bool parser_parse_for_header(LambdaRdParser* parser, LambdaToken first, LambdaParseValue* clauses_out) {
    LambdaParseValue clauses = 0;
    LambdaParseValue binding = parse_for_binding(parser);
    if (!binding) return false;
    clauses = parser_for_clause_append(parser, first.span, clauses, binding);
    while (for_binding_list_continues(parser)) {
        parser_advance(parser);
        binding = parse_for_binding(parser);
        if (!binding) return false;
        clauses = parser_for_clause_append(parser, first.span, clauses, binding);
    }
    while (parser->status == LAMBDA_PARSE_OK) {
        if (parser_accept(parser, LAMBDA_TOK_COMMA)) {
            LambdaParseValue let_clause = parse_for_let_clause(parser);
            if (!let_clause) return false;
            clauses = parser_for_clause_append(parser, first.span, clauses, let_clause);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_WHERE)) {
            if (!parser_add_for_value_clause(parser, first.span, &clauses, LAMBDA_REDUCTION_FORM_FOR_WHERE, first, 0)) return false;
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_GROUP)) {
            LambdaParseValue group = parse_for_group_clause(parser);
            if (!group) return false;
            clauses = parser_for_clause_append(parser, first.span, clauses, group);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_ORDER)) {
            if (!parser_expect(parser, LAMBDA_TOK_BY)) return false;
            do {
                LambdaParseValue order;
                if (!parser_parse_expression_value(parser, 0, &order)) return false;
                LambdaToken direction = {0};
                if (parser->current.kind == LAMBDA_TOK_ASC || parser->current.kind == LAMBDA_TOK_DESC) {
                    direction = parser->current;
                    parser_advance(parser);
                }
                parser_append_for_clause(parser, first.span, &clauses, LAMBDA_REDUCTION_FORM_FOR_ORDER, direction, 0, order);
            } while (parser_accept(parser, LAMBDA_TOK_COMMA));
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_LIMIT)) {
            LambdaToken last = parser->current;
            bool from_end = parser_accept(parser, LAMBDA_TOK_LAST);
            if (!parser_add_for_value_clause(parser, first.span, &clauses, LAMBDA_REDUCTION_FORM_FOR_LIMIT, last, from_end ? LAMBDA_REDUCTION_FLAG_OPTIONAL : 0)) return false;
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_OFFSET)) {
            if (!parser_add_for_value_clause(parser, first.span, &clauses, LAMBDA_REDUCTION_FORM_FOR_OFFSET, first, 0)) return false;
            continue;
        }
        break;
    }
    *clauses_out = clauses;
    return true;
}

static LambdaParseValue parse_for_expression(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue children[2];
    parser_advance(parser);
    parser_context(parser, LAMBDA_REDUCTION_FORM_FOR_BEGIN, first.span, first);
    bool parenthesized = parser_accept(parser, LAMBDA_TOK_LPAREN);
    LambdaParseValue clauses;
    if (!parser_parse_for_header(parser, first, &clauses)) return 0;
    if (parenthesized && !parser_expect(parser, LAMBDA_TOK_RPAREN)) return 0;
    if (!parser_parse_control_body(parser, first, LAMBDA_REDUCTION_FORM_NONE, LAMBDA_REDUCTION_FORM_NONE, error_for_body_close,
            error_unbraced_for_body, error_for_body_open, parenthesized, false, &children[0])) return 0;
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    children[1] = children[0];
    children[0] = clauses;
    uint32_t flags = parenthesized ? 0 : LAMBDA_REDUCTION_FLAG_BODY_BLOCK;
    LambdaParseValue result = parser_reduce_tokens(parser, LAMBDA_REDUCE_FOR, LAMBDA_REDUCTION_FORM_NONE, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, first, (LambdaToken){0}, flags, children, 2);
    parser_context(parser, LAMBDA_REDUCTION_FORM_FOR_END, first.span, first);
    return result;
}

static LambdaParseValue parse_match_expression(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue arms = 0;
    uint32_t arm_count = 0;
    parser_advance(parser);
    LambdaParseValue scrutinee = parse_expression(parser, 0);
    if (parser->status != LAMBDA_PARSE_OK || !parser_expect(parser, LAMBDA_TOK_LBRACE)) return 0;
    while (parser->status == LAMBDA_PARSE_OK && parser->current.kind != LAMBDA_TOK_RBRACE) {
        if (parser->current.kind == LAMBDA_TOK_RBRACE) break;
        LambdaToken arm_first = parser->current;
        LambdaParseValue pattern = 0;
        if (parser_accept(parser, LAMBDA_TOK_CASE)) {
            if (!parse_annotation_type_slot_value(parser, &pattern)) return 0;
        } else if (!parser_accept(parser, LAMBDA_TOK_DEFAULT)) {
            return parser_fail(parser, error_expected_match_arm, LAMBDA_TOK_CASE);
        }
        parser_context(parser, LAMBDA_REDUCTION_FORM_MATCH_ARM_BEGIN, arm_first.span, arm_first);
        LambdaParseValue body = 0;
        bool arm_body_braced = false;
        if (parser_accept(parser, LAMBDA_TOK_COLON)) {
            if (!parser_parse_control_body(parser, arm_first, LAMBDA_REDUCTION_FORM_NONE,
                    LAMBDA_REDUCTION_FORM_NONE, error_match_arm_close,
                    error_match_expression_body, NULL, true, false, &body)) return 0;
        } else if (parser->current.kind == LAMBDA_TOK_LBRACE) {
            arm_body_braced = true;
            if (!parser_parse_plain_braced(parser, error_match_arm_open, error_match_arm_close, &body)) return 0;
        } else {
            return parser_fail(parser, error_match_arm_separator, LAMBDA_TOK_COLON);
        }
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        parser_context(parser, LAMBDA_REDUCTION_FORM_MATCH_ARM_END, arm_first.span, arm_first);
        LambdaParseValue arm_children[2];
        uint32_t arm_child_count = 0;
        if (pattern) arm_children[arm_child_count++] = pattern;
        arm_children[arm_child_count++] = body;
        LambdaParseValue arm = parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_MATCH_ARM, (SourceSpan){arm_first.span.start_byte, parser->current.span.start_byte}, arm_first, (LambdaToken){0}, arm_body_braced ? LAMBDA_REDUCTION_FLAG_BODY_BLOCK : 0u, arm_children, arm_child_count);
        arms = parser_list_append(parser, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, arms, arm);
        arm_count++;
    }
    if (!arm_count) {
            return parser_fail(parser, error_expected_match_arm, LAMBDA_TOK_CASE);
    }
    if (!parser_expect(parser, LAMBDA_TOK_RBRACE)) return 0;
    LambdaParseValue children[2] = {scrutinee, arms};
    return parser_reduce(parser, LAMBDA_REDUCE_MATCH, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, children, 2);
}

static LambdaParseValue parse_while_statement(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    parser_context(parser, LAMBDA_REDUCTION_FORM_WHILE_BEGIN, first.span, first);
    LambdaParseValue condition = 0;
    if (!parser_parse_condition(parser, error_while_condition_close, &condition)) return 0;
    LambdaParseValue body = 0;
    if (!parser_parse_plain_braced(parser, error_while_body_open, error_while_body_close, &body)) return 0;
    LambdaParseValue children[2] = {condition, body};
    LambdaParseValue result = parser_reduce_tokens(parser, LAMBDA_REDUCE_FOR, LAMBDA_REDUCTION_FORM_FOR_WHILE, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, first, (LambdaToken){0}, 0, children, 2);
    parser_context(parser, LAMBDA_REDUCTION_FORM_WHILE_END, first.span, first);
    return result;
}

static LambdaParseValue parse_assignment_clause(LambdaRdParser* parser, const char* missing_name_message, const char* missing_equals_message) {
    LambdaToken first;
    LambdaToken names[64];
    uint32_t name_count = 1;
    if (!parser_take_name(parser, token_is_key, missing_name_message, &first)) return 0;
    names[0] = first;
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
        return parser_fail(parser, error_too_many_decomposition_names, LAMBDA_TOK_IDENTIFIER);
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
    LambdaParseValue value;
    if (!parser_parse_expression_value(parser, 0, &value)) return 0;
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    LambdaParseValue children[2];
    uint32_t child_count = 0;
    if (type_value) children[child_count++] = type_value;
    children[child_count++] = value;
    if (name_count > 1 || named_decompose) {
        return parser_reduce_name_tokens(parser, LAMBDA_REDUCE_LET, span, first, names, name_count, named_decompose ? LAMBDA_REDUCTION_FLAG_DECOMPOSE_NAMED : 0, children, child_count);
    }
    return parser_reduce_tokens(parser, LAMBDA_REDUCE_LET, LAMBDA_REDUCTION_FORM_TOKEN, span, first, (LambdaToken){0}, flags, children, child_count);
}

static LambdaParseValue parse_let_expression(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    LambdaParseValue value = parse_assignment_clause(parser,
        error_expected_binding_name_after_let, error_expected_equals_after_let);
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    return parser_reduce_plain_one(parser, LAMBDA_REDUCE_LET, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, value);
}

static LambdaParseValue parse_prefix_operator(LambdaRdParser* parser, LambdaToken first, int binding_power) {
    parser_advance(parser);
    LambdaParseValue child;
    if (!parser_parse_expression_value(parser, binding_power, &child)) return 0;
    return parser_reduce_one(parser, LAMBDA_REDUCE_PREFIX, LAMBDA_REDUCTION_FORM_TOKEN, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, first, child);
}

static LambdaParseValue parse_prefix(LambdaRdParser* parser) {
    if (!parser_enter(parser)) return 0;
    LambdaToken first = parser->current;
    LambdaParseValue value = 0;
    if (token_is_literal(first.kind) || token_is_identifier_like(first.kind) || first.kind == LAMBDA_TOK_FN || first.kind == LAMBDA_TOK_LAST || first.kind == LAMBDA_TOK_TILDE || first.kind == LAMBDA_TOK_TILDE_INDEX || first.kind == LAMBDA_TOK_PARENT || first.kind == LAMBDA_TOK_CARET || first.kind == LAMBDA_TOK_ELLIPSIS) {
        if (parser->pipe_rhs_depth && (first.kind == LAMBDA_TOK_TILDE || first.kind == LAMBDA_TOK_TILDE_INDEX)) {
            parser->pipe_rhs_has_current = true;
        }
        parser_advance(parser);
        value = parser_reduce_token(parser, LAMBDA_REDUCE_ATOM, LAMBDA_REDUCTION_FORM_TOKEN, first.span, first, NULL, 0);
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
            if (!parser_parse_plain_braced(parser, error_block_body_open, error_block_body_close, &value)) return 0;
        }
    } else if (first.kind == LAMBDA_TOK_LT) {
        value = parse_element(parser);
    } else if (first.kind == LAMBDA_TOK_PATH_REL || first.kind == LAMBDA_TOK_SLASH) {
        value = parse_path_slot(parser);
    } else if (first.kind == LAMBDA_TOK_BANG) {
        // §7.1: `!x` used to mean type complement and silently produced a
        // TYPE where every C/JS habit expects negation.
        return parser_fail(parser, error_not_logical_negation, LAMBDA_TOK_NOT);
    } else if (first.kind == LAMBDA_TOK_NOT || first.kind == LAMBDA_TOK_MINUS || first.kind == LAMBDA_TOK_PLUS || first.kind == LAMBDA_TOK_STAR) {
        value = parse_prefix_operator(parser, first, first.kind == LAMBDA_TOK_NOT ? LAMBDA_BP_MEMBERSHIP : LAMBDA_BP_PREFIX);
    } else if (first.kind == LAMBDA_TOK_LET) {
        value = parse_let_expression(parser);
    } else if (first.kind == LAMBDA_TOK_IF) {
        value = parse_if_expression(parser);
    } else if (first.kind == LAMBDA_TOK_FOR) {
        value = parse_for_expression(parser);
    } else if (first.kind == LAMBDA_TOK_MATCH) {
        value = parse_match_expression(parser);
    } else if (first.kind == LAMBDA_TOK_RAISE) {
        value = parse_prefix_operator(parser, first, 0);
    } else {
        parser_set_error(parser, error_expected_expression, LAMBDA_TOK_IDENTIFIER);
    }
    parser_leave(parser);
    return value;
}

static const int infix_bp[LAMBDA_TOK_ELLIPSIS + 1] = {
    [LAMBDA_TOK_PIPE_FORWARD] = LAMBDA_BP_PIPE, [LAMBDA_TOK_THAT] = LAMBDA_BP_PIPE,
    [LAMBDA_TOK_OR] = LAMBDA_BP_OR, [LAMBDA_TOK_AND] = LAMBDA_BP_AND,
    [LAMBDA_TOK_IS] = LAMBDA_BP_MEMBERSHIP, [LAMBDA_TOK_IN] = LAMBDA_BP_MEMBERSHIP,
    [LAMBDA_TOK_AT] = LAMBDA_BP_MEMBERSHIP, [LAMBDA_TOK_PIPE] = LAMBDA_BP_SET,
    [LAMBDA_TOK_AMPERSAND] = LAMBDA_BP_SET, [LAMBDA_TOK_BANG] = LAMBDA_BP_SET,
    [LAMBDA_TOK_TO] = LAMBDA_BP_SET, [LAMBDA_TOK_EQ_EQ] = LAMBDA_BP_EQUALITY,
    [LAMBDA_TOK_BANG_EQ] = LAMBDA_BP_EQUALITY, [LAMBDA_TOK_EQ_WORD] = LAMBDA_BP_EQUALITY,
    [LAMBDA_TOK_NE_WORD] = LAMBDA_BP_EQUALITY, [LAMBDA_TOK_LT] = LAMBDA_BP_RELATION,
    [LAMBDA_TOK_LT_EQ] = LAMBDA_BP_RELATION, [LAMBDA_TOK_GT] = LAMBDA_BP_RELATION,
    [LAMBDA_TOK_GT_EQ] = LAMBDA_BP_RELATION, [LAMBDA_TOK_LT_WORD] = LAMBDA_BP_RELATION,
    [LAMBDA_TOK_LE_WORD] = LAMBDA_BP_RELATION, [LAMBDA_TOK_GE_WORD] = LAMBDA_BP_RELATION,
    [LAMBDA_TOK_GT_WORD] = LAMBDA_BP_RELATION, [LAMBDA_TOK_PLUS] = LAMBDA_BP_ADD,
    [LAMBDA_TOK_PLUS_PLUS] = LAMBDA_BP_ADD, [LAMBDA_TOK_MINUS] = LAMBDA_BP_ADD,
    [LAMBDA_TOK_STAR] = LAMBDA_BP_MULTIPLY, [LAMBDA_TOK_SLASH] = LAMBDA_BP_MULTIPLY,
    [LAMBDA_TOK_DIV] = LAMBDA_BP_MULTIPLY, [LAMBDA_TOK_PERCENT] = LAMBDA_BP_MULTIPLY,
    [LAMBDA_TOK_STAR_STAR] = LAMBDA_BP_POWER,
};

static int infix_binding_power(LambdaRdParser* parser, LambdaTokenKind kind, bool* right_associative) {
    *right_associative = kind == LAMBDA_TOK_STAR_STAR;
    if (parser->stop_at_element_close && (kind == LAMBDA_TOK_GT || kind == LAMBDA_TOK_LT || kind == LAMBDA_TOK_GT_EQ || kind == LAMBDA_TOK_LT_EQ)) return -1;
    int bp = kind <= LAMBDA_TOK_ELLIPSIS ? infix_bp[kind] : 0;
    return bp ? bp : -1;
}

static LambdaParseValue parse_call_argument(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    if (token_is_key(first.kind) && parser->next.kind == LAMBDA_TOK_COLON) {
        parser_advance(parser);
        parser_advance(parser);
        LambdaParseValue value;
        if (!parser_parse_expression_value(parser, 0, &value)) return 0;
        return parser_reduce_one(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_NAMED_ARGUMENT, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, first, value);
    }
    return parse_expression(parser, 0);
}

static bool parser_parse_postfix_delimited(LambdaRdParser* parser, LambdaParseValue left, uint32_t left_start_byte, LambdaToken first, bool call, LambdaParseValue* value_out) {
    LambdaParseValue children[65] = {left};
    uint32_t count = 0;
    LambdaTokenKind closer = call ? LAMBDA_TOK_RPAREN : LAMBDA_TOK_RBRACKET;
    LambdaParseItemFn parse_item = call ? parse_call_argument : parser_parse_expression_item;
    if (!parser_accept(parser, call ? LAMBDA_TOK_LPAREN : LAMBDA_TOK_LBRACKET)) return false;
    if (!parser_parse_expression_list(parser, closer, children + 1, 64, &count, call, parse_item,
            call ? error_too_many_call_arguments : error_too_many_index_dimensions)) return false;
    SourceSpan span = {left_start_byte, parser->current.span.start_byte};
    if (call) {
        uint32_t flags = parser->pipe_rhs_depth == parser->expression_depth && !parser->pipe_rhs_has_current
            ? LAMBDA_REDUCTION_FLAG_PIPE_INJECT : 0u;
        *value_out = parser_reduce_tokens(parser, LAMBDA_REDUCE_POSTFIX, LAMBDA_REDUCTION_FORM_CALL,
            span, first, (LambdaToken){0}, flags, children, count + 1);
    } else {
        *value_out = parser_reduce_token(parser, LAMBDA_REDUCE_POSTFIX, LAMBDA_REDUCTION_FORM_INDEX,
            span, first, children, count + 1);
    }
    return true;
}

static LambdaParseValue parse_postfix(LambdaRdParser* parser, LambdaParseValue left, uint32_t left_start_byte) {
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
            bool member_chain = first.kind == LAMBDA_TOK_DOT && token_is_key(parser->next.kind);
            if (!member_chain) return left;
        }
        LambdaParseValue children[65] = {0};
        children[0] = left;
        if (parser->current.kind == LAMBDA_TOK_LPAREN || parser->current.kind == LAMBDA_TOK_LBRACKET) {
            if (!parser_parse_postfix_delimited(parser, left, left_start_byte, first,
                    parser->current.kind == LAMBDA_TOK_LPAREN, &left)) return 0;
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_DOT)) {
            LambdaToken field = parser->current;
            if (!parse_path_segment(parser)) return 0;
            SourceSpan span = {left_start_byte, parser->current.span.start_byte};
            left = parser_reduce_token(parser, LAMBDA_REDUCE_POSTFIX, LAMBDA_REDUCTION_FORM_MEMBER, span, field, children, 1);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_QUESTION) || parser_accept(parser, LAMBDA_TOK_DOT_QUESTION)) {
            children[1] = parse_primary_type_slot(parser);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            SourceSpan span = {left_start_byte, parser->current.span.start_byte};
            left = parser_reduce_token(parser, LAMBDA_REDUCE_POSTFIX, LAMBDA_REDUCTION_FORM_QUERY, span, first, children, 2);
            continue;
        }
        if (parser_accept(parser, LAMBDA_TOK_CARET)) {
            uint32_t handler_child_count = 1;
            if (parser->current.kind == LAMBDA_TOK_LBRACE && parser->current.nl_before) {
                return parser_fail(parser, error_handler_same_line, LAMBDA_TOK_LBRACE);
            }
            bool handler = parser->current.kind == LAMBDA_TOK_LBRACE;
            if (handler) {
                parser_context(parser, LAMBDA_REDUCTION_FORM_HANDLER_BEGIN, first.span, first);
                if (!parser_parse_plain_braced(parser, error_handler_body_open, error_handler_body_close, &children[1])) return 0;
                parser_context(parser, LAMBDA_REDUCTION_FORM_HANDLER_END, first.span, first);
                handler_child_count = 2;
                if (parser_accept(parser, LAMBDA_TOK_TILDE)) {
                    if (!parser_parse_plain_braced(parser, error_handler_value_open, error_handler_value_close, &children[2])) return 0;
                    handler_child_count = 3;
                }
            }
            SourceSpan span = {left_start_byte, parser->current.span.start_byte};
            left = parser_reduce_token(parser, LAMBDA_REDUCE_POSTFIX, handler ? LAMBDA_REDUCTION_FORM_HANDLER :
                    LAMBDA_REDUCTION_FORM_PROPAGATE, span, first, children, handler_child_count);
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
        if (left_is_element && parser->stop_at_element_close && parser->current.kind == LAMBDA_TOK_LT) {
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
            parser_context(parser, LAMBDA_REDUCTION_FORM_THAT_BEGIN, op.span, op);
        }
        bool is_named_value_rhs = op.kind == LAMBDA_TOK_IS && parser->current.kind == LAMBDA_TOK_NAMED_VALUE;
        LambdaParseValue right = op.kind == LAMBDA_TOK_IS && !is_named_value_rhs
            ? parse_type_slot(parser)
            : parse_expression(parser, bp + (right_associative ? 0 : 1));
        parser->pipe_rhs_depth = prior_pipe_rhs_depth;
        bool rhs_has_current = parser->pipe_rhs_has_current;
        parser->pipe_rhs_has_current = prior_pipe_rhs_has_current || rhs_has_current;
        if (op.kind == LAMBDA_TOK_THAT) {
            parser_context(parser, LAMBDA_REDUCTION_FORM_THAT_END, op.span, op);
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
        left = parser_reduce_token(parser, LAMBDA_REDUCE_BINARY, LAMBDA_REDUCTION_FORM_TOKEN, span, op, children, 2);
        left_is_element = false;
    }
    parser->expression_depth--;
    return left;
}

static bool if_starts_block_statement(const LambdaRdParser* parser) {
    LambdaRdParser probe = parser_probe(parser);
    parser_advance(&probe);
    (void)parse_expression(&probe, 0);
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind == LAMBDA_TOK_LBRACE;
}

static bool parser_parse_parameter_list(LambdaRdParser* parser, LambdaParseValue* parameters_out, bool* variadic_out) {
    if (parameters_out) *parameters_out = 0;
    if (variadic_out) *variadic_out = false;
    if (!parser_expect(parser, LAMBDA_TOK_LPAREN)) return false;
    return parse_parameter_items(parser, true, error_expected_parameter_name,
        error_expected_parameter_close, parameters_out, variadic_out);
}

static LambdaParseValue parse_function_declaration(LambdaRdParser* parser, bool is_public) {
    LambdaToken first = parser->current;
    bool is_proc = first.kind == LAMBDA_TOK_PN;
    parser_advance(parser);
    LambdaToken name;
    if (!parser_take_name(parser, token_is_key, error_expected_function_name, &name)) return 0;
    uint32_t function_flags = is_proc ? LAMBDA_REDUCTION_FLAG_PROC : 0u;
    if (is_public) function_flags |= LAMBDA_REDUCTION_FLAG_PUBLIC;
    parser_context_ex(parser, LAMBDA_REDUCTION_FORM_FUNCTION_BEGIN, (SourceSpan){first.span.start_byte, name.span.end_byte}, first, name, function_flags, NULL, 0);
    LambdaCallableSignature signature;
    if (!parser_parse_callable_signature(parser, false, true,
            error_expected_parameter_name,
            error_expected_parameter_close, &signature)) return 0;
    LambdaParseValue children[5];
    uint32_t child_count = 0;
    if (signature.parameters) children[child_count++] = signature.parameters;
    for (uint32_t i = 0; i < signature.return_count; i++) {
        children[child_count++] = signature.return_types[i];
    }
    LambdaParseValue child = 0;
    uint32_t flags = function_flags;
    if (signature.variadic) flags |= LAMBDA_REDUCTION_FLAG_VARIADIC;
    if (signature.raised) flags |= LAMBDA_REDUCTION_FLAG_RAISED;
    if (is_proc && parser->current.kind == LAMBDA_TOK_ARROW) {
        return parser_fail(parser, error_procedure_body, LAMBDA_TOK_LBRACE);
    }
    if (parser->current.kind == LAMBDA_TOK_ARROW) {
        if (!parser_parse_arrow_body(parser, &child)) return 0;
    } else if (parser->current.kind == LAMBDA_TOK_LBRACE) {
        flags |= LAMBDA_REDUCTION_FLAG_BODY_BLOCK;
        if (!parser_parse_scoped_braced(parser, is_proc ? parser->procedural_depth + 1 : 0,
                error_function_body_open, error_function_body_close, &child)) return 0;
    } else {
        return parser_fail(parser, error_expected_function_body, LAMBDA_TOK_LBRACE);
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    children[child_count++] = child;
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    LambdaParseValue result = parser_reduce_tokens(parser, LAMBDA_REDUCE_FUNCTION, LAMBDA_REDUCTION_FORM_FUNCTION, span, first, name, flags, children, child_count);
    parser_context(parser, LAMBDA_REDUCTION_FORM_FUNCTION_END, first.span, first);
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
    parser_context_ex(parser, LAMBDA_REDUCTION_FORM_VIEW_BEGIN, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, first, name, 0, &pattern, 1);
    LambdaParseValue parameters = 0;
    if (parser->current.kind == LAMBDA_TOK_LPAREN) {
        LambdaCallableSignature signature;
        if (!parser_parse_callable_signature(parser, false, true,
                error_expected_parameter_name,
                error_expected_parameter_close, &signature)) return 0;
        parameters = signature.parameters;
    } else {
        if (!parser_parse_return_types(parser, NULL, NULL, NULL)) return 0;
    }
    if (parser_accept(parser, LAMBDA_TOK_STATE)) {
        do {
            LambdaToken state_name;
            if (!parser_take_name(parser, token_is_identifier,
                    error_expected_state_name, &state_name)) return 0;
            LambdaParseValue value = 0;
            if (parser_accept(parser, LAMBDA_TOK_COLON)) {
                if (!parser_parse_expression_value(parser, 0, &value)) return 0;
            }
            parser_reduce_tokens(parser, LAMBDA_REDUCE_VIEW, LAMBDA_REDUCTION_FORM_VIEW_STATE, (SourceSpan){state_name.span.start_byte, parser->current.span.start_byte}, state_name, (LambdaToken){0}, 0, value ? &value : NULL, value ? 1 : 0);
        } while (parser_accept(parser, LAMBDA_TOK_COMMA));
    }
    LambdaParseValue body = 0;
    if (!parser_parse_plain_braced(parser, error_view_body_open, error_view_body_close, &body)) return 0;
    while (parser->current.kind == LAMBDA_TOK_ON) {
        LambdaToken on = parser->current;
        parser_advance(parser);
        LambdaToken event;
        if (!parser_take_name(parser, token_is_identifier,
                error_expected_event_name, &event)) return 0;
        parser_context(parser, LAMBDA_REDUCTION_FORM_VIEW_HANDLER_BEGIN, (SourceSpan){on.span.start_byte, parser->current.span.start_byte}, event);
        LambdaParseValue handler_parameters = 0;
        if (!parser_parse_parameter_list(parser, &handler_parameters, NULL)) return 0;
        LambdaParseValue handler_body = 0;
        if (!parser_parse_plain_braced(parser, error_event_body_open, error_event_body_close, &handler_body)) return 0;
        if (!handler_body) return 0;
        LambdaParseValue handler_children[2] = {
            handler_parameters, handler_body,
        };
        parser_reduce_tokens(parser, LAMBDA_REDUCE_VIEW, LAMBDA_REDUCTION_FORM_VIEW_HANDLER, (SourceSpan){on.span.start_byte, parser->current.span.start_byte}, event, (LambdaToken){0}, 0, handler_children, 2);
        parser_context(parser, LAMBDA_REDUCTION_FORM_VIEW_HANDLER_END, (SourceSpan){on.span.start_byte, parser->current.span.start_byte}, event);
    }
    LambdaParseValue view_children[2] = {parameters, body};
    LambdaParseValue result = parser_reduce_tokens(parser, LAMBDA_REDUCE_VIEW, LAMBDA_REDUCTION_FORM_VIEW, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, first, name, 0, view_children, 2);
    parser_context(parser, LAMBDA_REDUCTION_FORM_VIEW_END, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, first);
    return result;
}

static LambdaParseValue parse_type_aliases(LambdaRdParser* parser, LambdaToken first, LambdaToken name, bool is_public) {
    const uint32_t flags = is_public ? LAMBDA_REDUCTION_FLAG_PUBLIC : 0u;
    LambdaParseValue declarations = 0;
    for (;;) {
        if (parser->current.kind != LAMBDA_TOK_PATTERN_ISLAND) {
            parser_context_ex(parser, LAMBDA_REDUCTION_FORM_TYPE_ALIAS_BEGIN, name.span, name, (LambdaToken){0}, flags, NULL, 0);
        }
        LambdaParseValue type_value = 0;
        if (!parse_annotation_type_slot_value(parser, &type_value)) return 0;
        LambdaParseValue alias = parser_reduce_one_ex(parser, LAMBDA_REDUCE_DECLARATION, LAMBDA_REDUCTION_FORM_TYPE_ALIAS, (SourceSpan){name.span.start_byte, parser->current.span.start_byte}, name, (LambdaToken){0}, flags, type_value);
        declarations = parser_list_append(parser, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, declarations, alias);
        if (!parser_accept(parser, LAMBDA_TOK_COMMA)) return declarations;
        if (!parser_take_name(parser, token_is_key,
                error_expected_type_alias_name, &name)) return 0;
        if (!parser_expect_message(parser, LAMBDA_TOK_EQ, error_type_alias_equals)) return 0;
    }
}

static bool parser_object_type_field_starts(const LambdaRdParser* parser) {
    if (!token_is_key(parser->current.kind)) return false;
    if (parser->next.kind == LAMBDA_TOK_COLON) return true;
    if (parser->next.kind != LAMBDA_TOK_QUESTION) return false;
    size_t at = parser->next.span.end_byte;
    while (at < parser->lexer.length && (parser->lexer.source[at] == ' ' || parser->lexer.source[at] == '\t')) at++;
    return at < parser->lexer.length && parser->lexer.source[at] == ':';
}

static bool parser_parse_object_type_constraint(LambdaRdParser* parser) {
    LambdaToken that = parser->current;
    parser_advance(parser);
    parser_context(parser, LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT_BEGIN, that.span, that);
    LambdaParseValue constraint;
    if (!parser_parse_expression_value(parser, 0, &constraint)) return false;
    parser_context(parser, LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT_END, that.span, that);
    parser_reduce_one(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT, (SourceSpan){that.span.start_byte, parser->current.span.start_byte}, that, constraint);
    return true;
}

static bool parser_parse_object_type_field(LambdaRdParser* parser) {
    LambdaToken field = parser->current;
    parser_advance(parser);
    uint32_t field_flags = parser_accept(parser, LAMBDA_TOK_QUESTION)
        ? LAMBDA_REDUCTION_FLAG_OPTIONAL : 0u;
    if (!parser_expect_message(parser, LAMBDA_TOK_COLON, error_object_field_colon)) return false;
    LambdaParseValue type_value = 0;
    if (!parse_annotation_type_slot_value(parser, &type_value)) return false;
    LambdaParseValue children[2] = {type_value, 0};
    uint32_t child_count = 1;
    if (parser_accept(parser, LAMBDA_TOK_EQ)) {
        if (!parser_parse_expression_value(parser, 0, &children[child_count])) return false;
        child_count++;
    }
    parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_TYPE_OBJECT_FIELD, (SourceSpan){field.span.start_byte, parser->current.span.start_byte}, field, (LambdaToken){0}, field_flags, children, child_count);
    return true;
}

static bool parser_parse_object_type_member(LambdaRdParser* parser) {
    if (parser->current.kind == LAMBDA_TOK_FN || parser->current.kind == LAMBDA_TOK_PN) {
        return parse_function_declaration(parser, false) != 0;
    }
    if (parser->current.kind == LAMBDA_TOK_THAT) {
        return parser_parse_object_type_constraint(parser);
    }
    if (parser_object_type_field_starts(parser)) {
        return parser_parse_object_type_field(parser);
    }
    if (token_starts_type(parser->current.kind)) {
        LambdaToken content = parser->current;
        LambdaParseValue content_type = parse_type_slot(parser);
        if (!content_type) return false;
        parser_reduce_one(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONTENT, (SourceSpan){content.span.start_byte, parser->current.span.start_byte}, content, content_type);
        return true;
    }
    parser_fail(parser, error_expected_object_type_member, LAMBDA_TOK_IDENTIFIER);
    return false;
}

// object members are one strict comma list: field, constraint, method, or content type.
static bool parser_parse_object_type_members(LambdaRdParser* parser) {
    if (!parser_expect(parser, LAMBDA_TOK_LBRACE)) return false;
    while (parser->status == LAMBDA_PARSE_OK && parser->current.kind != LAMBDA_TOK_RBRACE) {
        while (parser_accept(parser, LAMBDA_TOK_COMMA)) {}
        if (parser->current.kind == LAMBDA_TOK_RBRACE) break;
        if (parser->current.kind == LAMBDA_TOK_SEMICOLON) {
            parser_fail(parser, error_object_type_separator, LAMBDA_TOK_COMMA);
            return false;
        }
        if (!parser_parse_object_type_member(parser)) return false;
    }
    return parser_expect(parser, LAMBDA_TOK_RBRACE);
}

static LambdaParseValue parse_type_declaration(LambdaRdParser* parser, bool is_public) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    LambdaToken name;
    if (!parser_take_name(parser, token_is_key, error_expected_type_name, &name)) return 0;
    if (parser_accept(parser, LAMBDA_TOK_EQ)) {
        return parse_type_aliases(parser, first, name, is_public);
    } else {
        LambdaToken base = {0};
        if (parser_accept(parser, LAMBDA_TOK_COLON)) {
            if (!parser_take_name(parser, token_is_key,
                    error_expected_inherited_type_name, &base)) return 0;
        }
        parser_context_ex(parser, LAMBDA_REDUCTION_FORM_TYPE_OBJECT_BEGIN, first.span, base, name, is_public ? LAMBDA_REDUCTION_FLAG_PUBLIC : 0u, NULL, 0);
        if (!parser_parse_object_type_members(parser)) return 0;
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    if (first.kind == LAMBDA_TOK_TYPE && parser->current.kind != LAMBDA_TOK_EQ) {
        parser_context(parser, LAMBDA_REDUCTION_FORM_TYPE_OBJECT_END, first.span, first);
    }
    return parser_reduce_tokens(parser, LAMBDA_REDUCE_DECLARATION, LAMBDA_REDUCTION_FORM_TYPE_OBJECT, span, name, (LambdaToken){0}, is_public ? LAMBDA_REDUCTION_FLAG_PUBLIC : 0u, NULL, 0);
}

static LambdaParseValue parse_var_statement(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    parser_advance(parser);
    LambdaParseValue declarations = 0;
    do {
        LambdaParseValue declaration = parse_assignment_clause(parser,
            error_expected_mutable_binding_name, error_expected_equals_after_mutable_binding);
        if (!declaration) return 0;
        declarations = parser_list_append(parser, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, declarations, declaration);
    } while (parser_accept(parser, LAMBDA_TOK_COMMA));
    return parser_reduce_one(parser, LAMBDA_REDUCE_STATEMENT, LAMBDA_REDUCTION_FORM_VAR, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, first, declarations);
}

static bool if_statement_body_is_map(const LambdaRdParser* parser) {
    LambdaRdParser probe = parser_probe(parser);
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
    LambdaParseValue condition;
    if (!parser_parse_expression_value(parser, 0, &condition)) return 0;
    LambdaParseValue body = 0;
    if (!parser_parse_braced(parser, first, LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN,
            LAMBDA_REDUCTION_FORM_IF_BRANCH_END, NULL, error_if_body_close, &body)) return 0;
    LambdaParseValue children[3] = {condition, body, 0};
    uint32_t child_count = 2;
    if (parser_accept(parser, LAMBDA_TOK_ELSE)) {
        if (!parser_parse_control_body(parser, first, LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN,
                LAMBDA_REDUCTION_FORM_IF_BRANCH_END, error_else_body_close,
                error_unbraced_else_body, NULL, true, true, &children[2])) return 0;
        child_count = 3;
    }
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    parser->last_statement_self_delimiting = true;
    return parser_reduce(parser, LAMBDA_REDUCE_IF, span, children, child_count);
}

static bool parser_parse_import_module(LambdaRdParser* parser, LambdaToken* first_out, LambdaToken* last_out) {
    LambdaToken first = parser->current;
    LambdaToken last = first;
    bool relative = first.kind == LAMBDA_TOK_DOT || first.kind == LAMBDA_TOK_SLASH;
    if (relative) parser_advance(parser);
    if (!parser_take_name(parser, token_is_key, relative
            ? error_expected_relative_import_component : error_expected_import_module, &last)) return false;
    if (!relative) first = last;
    while (parser->current.kind == LAMBDA_TOK_DOT || parser->current.kind == LAMBDA_TOK_SLASH) {
        parser_advance(parser);
        if (!parser_take_name(parser, token_is_key,
                error_expected_import_component, &last)) return false;
    }
    *first_out = first;
    *last_out = last;
    return true;
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
                // S16.10.1v2: the alias is a binding, so a capture-real word
                // is out; a quoted symbol is out too, since a symbol never
                // reads a binding at the use site (S2.4.3).
                size_t alias_len = parser->current.span.end_byte -
                    parser->current.span.start_byte;
                if (parser->current.kind == LAMBDA_TOK_SYMBOL ||
                        (parser->current.span.start_byte < parser->lexer.length &&
                         lambda_lexer_word_bars_binding(
                             parser->lexer.source + parser->current.span.start_byte,
                             alias_len))) {
                    return parser_fail(parser, error_import_alias_reserved,
                        LAMBDA_TOK_IDENTIFIER);
                }
                alias = parser->current;
                parser_advance(parser);
                parser_advance(parser);
            }
            LambdaToken module_first, module_last;
            if (!parser_parse_import_module(parser, &module_first, &module_last)) return 0;
            LambdaToken module = module_first;
            module.kind = LAMBDA_TOK_IDENTIFIER;
            module.span.end_byte = module_last.span.end_byte;
            LambdaParseValue item = parser_reduce_tokens(parser, LAMBDA_REDUCE_DECLARATION, LAMBDA_REDUCTION_FORM_IMPORT, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, alias, module, 0, NULL, 0);
            imports = parser_list_append(parser, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, imports, item);
        } while (parser_accept(parser, LAMBDA_TOK_COMMA));
        return imports;
    } else {
        bool is_public = parser_accept(parser, LAMBDA_TOK_PUB);
        if (is_public && parser->current.kind == LAMBDA_TOK_IDENTIFIER) {
            return parser_fail(parser, error_pub_declaration, LAMBDA_TOK_LET);
        }
        if (parser->current.kind == LAMBDA_TOK_FN || parser->current.kind == LAMBDA_TOK_PN) {
            return parse_function_declaration(parser, is_public);
        }
        if (parser->current.kind == LAMBDA_TOK_VIEW || parser->current.kind == LAMBDA_TOK_EDIT) {
            return parse_view_declaration(parser);
        }
        if (parser->current.kind == LAMBDA_TOK_TYPE && parser->next.kind != LAMBDA_TOK_LPAREN && !parser->next.nl_before && parser->next.kind != LAMBDA_TOK_SEMICOLON && parser->next.kind != LAMBDA_TOK_EOF) {
            return parse_type_declaration(parser, is_public);
        }
        if (parser->current.kind == LAMBDA_TOK_VAR) {
            return parse_var_statement(parser);
        }
        if (parser->current.kind == LAMBDA_TOK_APPLY && parser->next.kind == LAMBDA_TOK_SEMICOLON) {
            parser_advance(parser);
            return parser_reduce(parser, LAMBDA_REDUCE_STATEMENT, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, NULL, 0);
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
                    error_expected_binding_name_after_let,
                    error_expected_equals_after_let);
                if (!binding) return 0;
                bindings = parser_list_append(parser, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, bindings, binding);
            }
            SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            if (is_public) {
                return parser_reduce_one_ex(parser, LAMBDA_REDUCE_DECLARATION, LAMBDA_REDUCTION_FORM_NONE, span, first, (LambdaToken){0}, LAMBDA_REDUCTION_FLAG_PUBLIC, bindings);
            }
            return parser_reduce_plain_one(parser, LAMBDA_REDUCE_STATEMENT, span, bindings);
        }
        if (is_public) {
            LambdaParseValue public_value = parse_assignment_clause(parser,
                error_expected_public_binding_name, error_expected_equals_after_public_binding);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            while (parser_accept(parser, LAMBDA_TOK_COMMA)) {
                if (!parse_assignment_clause(parser, error_expected_public_binding_name,
                        error_expected_equals_after_public_binding)) return 0;
            }
            return parser_reduce_one_ex(parser, LAMBDA_REDUCE_DECLARATION, LAMBDA_REDUCTION_FORM_NONE, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, first, (LambdaToken){0}, LAMBDA_REDUCTION_FLAG_PUBLIC, public_value);
        }
        if (parser->current.kind == LAMBDA_TOK_RETURN || parser->current.kind == LAMBDA_TOK_BREAK || parser->current.kind == LAMBDA_TOK_CONTINUE) {
            LambdaToken control = parser->current;
            parser_advance(parser);
            LambdaParseValue value = 0;
            if (control.kind == LAMBDA_TOK_RETURN && token_starts_expression(parser->current.kind)) {
                if (!parser_parse_expression_value(parser, 0, &value)) return 0;
            }
            LambdaReductionForm form = control.kind == LAMBDA_TOK_RETURN
                ? LAMBDA_REDUCTION_FORM_RETURN
                : control.kind == LAMBDA_TOK_BREAK
                    ? LAMBDA_REDUCTION_FORM_BREAK
                    : LAMBDA_REDUCTION_FORM_CONTINUE;
            return parser_reduce_tokens(parser, LAMBDA_REDUCE_STATEMENT, form, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, control, (LambdaToken){0}, 0, value ? &value : NULL, value ? 1 : 0);
        } else {
            parser->top_level_statement_relation = false;
            LambdaParseValue expr;
            if (!parser_parse_expression_value(parser, 0, &expr)) return 0;
            if (parser_accept(parser, LAMBDA_TOK_EQ)) {
                parser->last_statement_assignment = true;
                LambdaParseValue value;
                if (!parser_parse_expression_value(parser, 0, &value)) return 0;
                LambdaParseValue children[2] = {expr, value};
                SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
                return parser_reduce(parser, LAMBDA_REDUCE_ASSIGNMENT, span, children, 2);
            }
            if (parser->top_level_statement_relation && !parser->last_statement_assignment) {
                return parser_fail(parser, error_statement_relation_parentheses, LAMBDA_TOK_NEWLINE);
            }
            SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
            return parser_reduce_plain_one(parser, LAMBDA_REDUCE_STATEMENT, span, expr);
        }
    }
    SourceSpan span = {first.span.start_byte, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_DECLARATION, span, NULL, 0);
}

static bool element_content_starts_sibling(const LambdaRdParser* parser) {
    LambdaRdParser probe = parser_probe(parser);
    LambdaToken child_first = probe.current;
    LambdaParseValue child = parse_prefix(&probe);
    if (probe.status != LAMBDA_PARSE_OK) return false;
    (void)parse_postfix(&probe, child, child_first.span.start_byte);
    return probe.status == LAMBDA_PARSE_OK && probe.current.kind == LAMBDA_TOK_LT;
}

static LambdaParseValue parse_content_child(LambdaRdParser* parser) {
    LambdaToken first = parser->current;
    LambdaParseValue child = parse_prefix(parser);
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    child = parse_postfix(parser, child, first.span.start_byte);
    if (parser->status != LAMBDA_PARSE_OK) return 0;
    return parser_reduce_plain_one(parser, LAMBDA_REDUCE_STATEMENT, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, child);
}

static void parser_add_content(LambdaRdParser* parser, LambdaParseValue* content, bool* has_content, uint32_t* start, LambdaToken first, LambdaParseValue statement) {
    *content = parser_content_append(parser, (SourceSpan){first.span.start_byte, parser->current.span.start_byte}, *content, statement);
    if (!*has_content) *start = first.span.start_byte;
    *has_content = true;
}

static bool parser_consume_separator(LambdaRdParser* parser, LambdaTokenKind terminator) {
    if (!parser_accept(parser, LAMBDA_TOK_SEMICOLON)) return false;
    if (parser->current.kind == terminator || parser->current.kind == LAMBDA_TOK_EOF) {
        parser_fail(parser, error_trailing_statement_separator, terminator);
        return false;
    }
    if (parser->current.kind == LAMBDA_TOK_SEMICOLON) {
        parser_fail(parser, error_empty_statement_between_separators, terminator);
        return false;
    }
    return true;
}

static LambdaParseValue parse_content(LambdaRdParser* parser, LambdaTokenKind terminator) {
    LambdaParseValue content = 0;
    uint32_t content_start = parser->current.span.start_byte;
    bool has_content = false;
    while (parser->status == LAMBDA_PARSE_OK && parser->current.kind != terminator && parser->current.kind != LAMBDA_TOK_EOF) {
        if ((parser->current.kind == LAMBDA_TOK_LT && element_content_starts_sibling(parser)) || (parser->current.kind == LAMBDA_TOK_STRING && (parser->next.kind == LAMBDA_TOK_STRING || parser->next.kind == LAMBDA_TOK_LBRACE || parser->next.kind == LAMBDA_TOK_LT)) || (terminator == LAMBDA_TOK_GT && parser->current.kind == LAMBDA_TOK_LBRACE)) {
            LambdaToken child_first = parser->current;
            LambdaParseValue statement = parse_content_child(parser);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            parser_add_content(parser, &content, &has_content, &content_start, child_first, statement);
            continue;
        }
        LambdaToken statement_token = parser->current;
        LambdaTokenKind statement_first = statement_token.kind;
        if (parser->procedural_depth && statement_first == LAMBDA_TOK_LBRACE && parser->next.kind == LAMBDA_TOK_RBRACE) {
            return parser_fail(parser, error_empty_block_statement, LAMBDA_TOK_RBRACE);
        }
        LambdaParseValue statement = parse_statement(parser);
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        parser_add_content(parser, &content, &has_content, &content_start, statement_token, statement);
        if (parser->current.kind == terminator || parser->current.kind == LAMBDA_TOK_EOF) break;
        bool closed_tail =
            statement_first == LAMBDA_TOK_BREAK || statement_first == LAMBDA_TOK_CONTINUE || statement_first == LAMBDA_TOK_IMPORT || (parser->prev_kind == LAMBDA_TOK_RBRACE && (statement_first == LAMBDA_TOK_FN || statement_first == LAMBDA_TOK_PN || statement_first == LAMBDA_TOK_TYPE || statement_first == LAMBDA_TOK_VIEW || statement_first == LAMBDA_TOK_EDIT || statement_first == LAMBDA_TOK_WHILE || statement_first == LAMBDA_TOK_MATCH || statement_first == LAMBDA_TOK_IF || statement_first == LAMBDA_TOK_FOR || statement_first == LAMBDA_TOK_PUB));
        if (closed_tail) {
            parser_consume_separator(parser, terminator);
            if (parser->status != LAMBDA_PARSE_OK) return 0;
            continue;
        }
        if (parser_consume_separator(parser, terminator)) {
            continue;
        }
        if (parser->status != LAMBDA_PARSE_OK) return 0;
        if (token_is_dual_role(parser->current.kind) || (parser->current.nl_before && token_is_dot_led_number(parser, &parser->current))) {
            return parser_fail(parser, parser->current.nl_before
                    ? error_line_continuation : error_expected_statement_separator,
                    LAMBDA_TOK_SEMICOLON);
        }
    }
    SourceSpan span = {content_start, parser->current.span.start_byte};
    return parser_reduce(parser, LAMBDA_REDUCE_CONTENT, span, content ? &content : NULL, content ? 1u : 0u);
}

LambdaParseStatus lambda_rd_parse_source(const char* source, size_t length, const LambdaParseSink* sink, void* sink_context, LambdaParseMetrics* metrics, LambdaParseError* error) {
    if (metrics) memset(metrics, 0, sizeof(*metrics));
    if (error) memset(error, 0, sizeof(*error));
    if (!source || length > UINT32_MAX) {
        if (error) {
            error->message = error_source_size_limit;
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
        parser_set_error(&parser, error_invalid_token, LAMBDA_TOK_EOF);
        return parser.status;
    }
    LambdaParseValue content = parse_content(&parser, LAMBDA_TOK_EOF);
    if (parser.status == LAMBDA_PARSE_OK && parser.current.kind != LAMBDA_TOK_EOF) {
        parser_set_error(&parser, error_unexpected_trailing_input, LAMBDA_TOK_EOF);
    }
    if (parser.status == LAMBDA_PARSE_OK) {
        SourceSpan span = {0, (uint32_t)length};
        parser_reduce(&parser, LAMBDA_REDUCE_DOCUMENT, span, &content, content ? 1u : 0u);
    }
    return parser.status;
}

static bool parser_recovery_opener(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_LPAREN || kind == LAMBDA_TOK_LBRACKET || kind == LAMBDA_TOK_LBRACE;
}

static bool parser_recovery_closer(LambdaTokenKind kind) {
    return kind == LAMBDA_TOK_RPAREN || kind == LAMBDA_TOK_RBRACKET || kind == LAMBDA_TOK_RBRACE;
}

static size_t parser_recovery_sync(const char* source, size_t length, size_t after) {
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
        if (nesting == 0 && token.span.start_byte >= after && token.kind == LAMBDA_TOK_SEMICOLON) {
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

static void parser_recovery_append(LambdaParseReport* report, const LambdaParseError* error) {
    if (!report || !error || report->error_count >= LAMBDA_PARSE_MAX_DIAGNOSTICS) return;
    report->errors[report->error_count++] = *error;
}

LambdaParseStatus lambda_rd_parse_recovering(const char* source, size_t length, LambdaParseReport* report) {
    if (!report) {
        LambdaParseError error = {0};
        return lambda_rd_parse_source(source, length, NULL, NULL, NULL, &error);
    }
    memset(report, 0, sizeof(*report));
    LambdaParseError first = {0};
    LambdaParseStatus status = lambda_rd_parse_source(source, length, NULL, NULL, NULL, &first);
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
