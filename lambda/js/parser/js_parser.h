#pragma once

// First-party JavaScript/TypeScript syntax parser.  The parser is deliberately
// syntax-only for this slice: committed reductions carry parser facts to a
// caller-owned sink, while AST construction remains in the existing JS seam.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../runtime/source_span.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum JsLexGoal {
    JS_LEX_DIV = 0,
    JS_LEX_REGEXP,
    JS_LEX_TEMPLATE,
    JS_LEX_TEMPLATE_TAIL,
    JS_LEX_JSX_TEXT,
    JS_LEX_TS_TYPE,
} JsLexGoal;

typedef enum JsTokenKind {
    JS_TOK_EOF = 0,
    JS_TOK_ERROR,
    JS_TOK_HASHBANG,
    JS_TOK_IDENTIFIER,
    JS_TOK_PRIVATE_IDENTIFIER,
    JS_TOK_NUMBER,
    JS_TOK_BIGINT,
    JS_TOK_STRING,
    JS_TOK_REGEXP,
    JS_TOK_TEMPLATE,

    JS_TOK_BREAK,
    JS_TOK_CASE,
    JS_TOK_CATCH,
    JS_TOK_CLASS,
    JS_TOK_CONST,
    JS_TOK_CONTINUE,
    JS_TOK_DEBUGGER,
    JS_TOK_DEFAULT,
    JS_TOK_DELETE,
    JS_TOK_DO,
    JS_TOK_ELSE,
    JS_TOK_EXPORT,
    JS_TOK_EXTENDS,
    JS_TOK_FINALLY,
    JS_TOK_FOR,
    JS_TOK_FUNCTION,
    JS_TOK_IF,
    JS_TOK_IMPORT,
    JS_TOK_IN,
    JS_TOK_INSTANCEOF,
    JS_TOK_LET,
    JS_TOK_NEW,
    JS_TOK_RETURN,
    JS_TOK_SUPER,
    JS_TOK_SWITCH,
    JS_TOK_THIS,
    JS_TOK_THROW,
    JS_TOK_TRY,
    JS_TOK_TYPEOF,
    JS_TOK_VAR,
    JS_TOK_VOID,
    JS_TOK_WHILE,
    JS_TOK_WITH,
    JS_TOK_YIELD,
    JS_TOK_ASYNC,
    JS_TOK_AWAIT,
    JS_TOK_OF,
    JS_TOK_GET,
    JS_TOK_SET,

    JS_TOK_TRUE,
    JS_TOK_FALSE,
    JS_TOK_NULL,

    // TypeScript words are retained as tokens; JS mode treats them as
    // ordinary contextual identifiers where the grammar permits one.
    JS_TOK_AS,
    JS_TOK_ASSERTS,
    JS_TOK_ABSTRACT,
    JS_TOK_ANY,
    JS_TOK_BOOLEAN,
    JS_TOK_DECLARE,
    JS_TOK_ENUM,
    JS_TOK_FROM,
    JS_TOK_IMPLEMENTS,
    JS_TOK_INFER,
    JS_TOK_INTERFACE,
    JS_TOK_IS,
    JS_TOK_KEYOF,
    JS_TOK_MODULE,
    JS_TOK_NAMESPACE,
    JS_TOK_NEVER,
    JS_TOK_NUMBER_TYPE,
    JS_TOK_OBJECT,
    JS_TOK_PACKAGE,
    JS_TOK_PRIVATE,
    JS_TOK_PROTECTED,
    JS_TOK_PUBLIC,
    JS_TOK_READONLY,
    JS_TOK_REQUIRE,
    JS_TOK_SATISFIES,
    JS_TOK_STATIC,
    JS_TOK_STRING_TYPE,
    JS_TOK_SYMBOL,
    JS_TOK_TYPE,
    JS_TOK_UNKNOWN,

    JS_TOK_LPAREN,
    JS_TOK_RPAREN,
    JS_TOK_LBRACKET,
    JS_TOK_RBRACKET,
    JS_TOK_LBRACE,
    JS_TOK_RBRACE,
    JS_TOK_DOT,
    JS_TOK_QUESTION_DOT,
    JS_TOK_COMMA,
    JS_TOK_SEMICOLON,
    JS_TOK_COLON,
    JS_TOK_QUESTION,
    JS_TOK_AT,

    JS_TOK_PLUS,
    JS_TOK_MINUS,
    JS_TOK_STAR,
    JS_TOK_SLASH,
    JS_TOK_PERCENT,
    JS_TOK_EXP,
    JS_TOK_PLUS_PLUS,
    JS_TOK_MINUS_MINUS,
    JS_TOK_BANG,
    JS_TOK_TILDE,
    JS_TOK_AMP,
    JS_TOK_PIPE,
    JS_TOK_CARET,
    JS_TOK_LSHIFT,
    JS_TOK_RSHIFT,
    JS_TOK_URSHIFT,
    JS_TOK_AMP_AMP,
    JS_TOK_PIPE_PIPE,
    JS_TOK_NULLISH,
    JS_TOK_EQUAL,
    JS_TOK_EQUAL_EQUAL,
    JS_TOK_STRICT_EQUAL,
    JS_TOK_BANG_EQUAL,
    JS_TOK_STRICT_BANG_EQUAL,
    JS_TOK_LT,
    JS_TOK_LTE,
    JS_TOK_GT,
    JS_TOK_GTE,
    JS_TOK_ARROW,
    JS_TOK_ELLIPSIS,
    JS_TOK_PLUS_EQUAL,
    JS_TOK_MINUS_EQUAL,
    JS_TOK_STAR_EQUAL,
    JS_TOK_SLASH_EQUAL,
    JS_TOK_PERCENT_EQUAL,
    JS_TOK_EXP_EQUAL,
    JS_TOK_AMP_EQUAL,
    JS_TOK_PIPE_EQUAL,
    JS_TOK_CARET_EQUAL,
    JS_TOK_LSHIFT_EQUAL,
    JS_TOK_RSHIFT_EQUAL,
    JS_TOK_URSHIFT_EQUAL,
    JS_TOK_AMP_AMP_EQUAL,
    JS_TOK_PIPE_PIPE_EQUAL,
    JS_TOK_NULLISH_EQUAL,
} JsTokenKind;

typedef struct JsToken {
    JsTokenKind kind;
    SourceSpan span;
    uint32_t line;
    uint32_t column;
    bool line_terminator_before;
    bool escaped_identifier;
    bool has_invalid_escape;
} JsToken;

typedef struct JsLexer {
    const char* source;
    size_t length;
    size_t offset;
    uint32_t line;
    uint32_t column;
    bool line_terminator_before;
    JsLexGoal goal;
    uint32_t template_depth;
    uint32_t template_braces[32];
    bool template_continuation;
} JsLexer;

typedef enum JsParseMode {
    JS_PARSE_SCRIPT = 0,
    JS_PARSE_MODULE = 1u << 0,
    JS_PARSE_TYPESCRIPT = 1u << 1,
    JS_PARSE_JSX = 1u << 2,
} JsParseMode;

typedef enum JsParseStatus {
    JS_PARSE_OK = 0,
    JS_PARSE_INCOMPLETE,
    JS_PARSE_ERROR,
} JsParseStatus;

typedef enum JsParseErrorCode {
    JS_PARSE_ERROR_NONE = 0,
    JS_PARSE_ERROR_INVALID_SOURCE,
    JS_PARSE_ERROR_INVALID_TOKEN,
    JS_PARSE_ERROR_UNEXPECTED_TOKEN,
    JS_PARSE_ERROR_UNEXPECTED_EOF,
    JS_PARSE_ERROR_UNTERMINATED_LITERAL,
    JS_PARSE_ERROR_UNTERMINATED_COMMENT,
    JS_PARSE_ERROR_LINE_TERMINATOR,
    JS_PARSE_ERROR_CONTEXT,
    JS_PARSE_ERROR_NESTING,
} JsParseErrorCode;

typedef struct JsParseError {
    JsParseErrorCode code;
    JsTokenKind actual_kind;
    SourceSpan span;
    uint64_t expected_token_bits[4];
    const char* message;
} JsParseError;

typedef struct JsParseMetrics {
    uint32_t token_count;
    uint32_t reduction_count;
    uint32_t max_recursion_depth;
    uint64_t structural_hash;
} JsParseMetrics;

typedef enum JsReductionKind {
    JS_REDUCE_PROGRAM = 1,
    JS_REDUCE_STATEMENT,
    JS_REDUCE_DECLARATION,
    JS_REDUCE_EXPRESSION,
    JS_REDUCE_PATTERN,
    JS_REDUCE_TYPE,
    JS_REDUCE_LIST,
    JS_REDUCE_BLOCK,
    JS_REDUCE_CLASS_MEMBER,
} JsReductionKind;

typedef enum JsReductionForm {
    JS_REDUCTION_NONE = 0,
    JS_REDUCTION_TOKEN,
    JS_REDUCTION_PREFIX,
    JS_REDUCTION_POSTFIX,
    JS_REDUCTION_BINARY,
    JS_REDUCTION_ASSIGNMENT,
    JS_REDUCTION_CALL,
    JS_REDUCTION_NEW,
    JS_REDUCTION_MEMBER,
    JS_REDUCTION_SUBSCRIPT,
    JS_REDUCTION_ARRAY,
    JS_REDUCTION_SEQUENCE,
    JS_REDUCTION_CONDITIONAL,
    JS_REDUCTION_PROPERTY,
    JS_REDUCTION_SPREAD,
    JS_REDUCTION_DECLARATOR,
    JS_REDUCTION_VARIABLE_DECLARATION,
    JS_REDUCTION_EXPRESSION_STATEMENT,
    JS_REDUCTION_STATEMENT_WRAPPER,
    JS_REDUCTION_IF,
    JS_REDUCTION_WHILE,
    JS_REDUCTION_DO_WHILE,
    JS_REDUCTION_RETURN,
    JS_REDUCTION_THROW,
    JS_REDUCTION_BREAK,
    JS_REDUCTION_CONTINUE,
    JS_REDUCTION_PARAMETER,
    JS_REDUCTION_CLASS_BODY,
    JS_REDUCTION_METHOD,
    JS_REDUCTION_FIELD,
    JS_REDUCTION_STATIC_BLOCK,
    JS_REDUCTION_OBJECT,
    JS_REDUCTION_FUNCTION,
    JS_REDUCTION_ARROW,
    JS_REDUCTION_CLASS,
    JS_REDUCTION_TEMPLATE,
    JS_REDUCTION_IMPORT,
    JS_REDUCTION_EXPORT,
    JS_REDUCTION_TYPE,
    JS_REDUCTION_FOR,
    JS_REDUCTION_FOR_IN,
    JS_REDUCTION_FOR_OF,
    JS_REDUCTION_SWITCH,
    JS_REDUCTION_CASE,
    JS_REDUCTION_TRY,
    JS_REDUCTION_CATCH,
    JS_REDUCTION_IMPORT_SPECIFIER,
    JS_REDUCTION_EXPORT_SPECIFIER,
    JS_REDUCTION_OBJECT_METHOD,
    JS_REDUCTION_TEMPLATE_PART,
    JS_REDUCTION_TAGGED_TEMPLATE,
    JS_REDUCTION_HOLE,
    JS_REDUCTION_NON_NULL,
    JS_REDUCTION_TYPE_ASSERTION,
    JS_REDUCTION_ENUM_MEMBER,
    JS_REDUCTION_LABELED,
    JS_REDUCTION_WITH,
    JS_REDUCTION_TYPE_PARAMETER,
    JS_REDUCTION_TYPE_PARAMETERS,
    JS_REDUCTION_DECORATOR,
    JS_REDUCTION_DECORATED_DECLARATION,
} JsReductionForm;

enum JsReductionFlags {
    JS_REDUCTION_FLAG_OPTIONAL = 1u << 0,
    JS_REDUCTION_FLAG_COMPUTED = 1u << 1,
    JS_REDUCTION_FLAG_SPREAD = 1u << 2,
    JS_REDUCTION_FLAG_HOLES = 1u << 3,
    JS_REDUCTION_FLAG_PROPERTY = 1u << 4,
    JS_REDUCTION_FLAG_SHORTHAND = 1u << 5,
    JS_REDUCTION_FLAG_BINDING = 1u << 6,
    JS_REDUCTION_FLAG_DECLARATION = 1u << 7,
    JS_REDUCTION_FLAG_NAMED = 1u << 8,
    JS_REDUCTION_FLAG_STATIC = 1u << 9,
    JS_REDUCTION_FLAG_GETTER = 1u << 10,
    JS_REDUCTION_FLAG_SETTER = 1u << 11,
    JS_REDUCTION_FLAG_SUPER = 1u << 12,
    JS_REDUCTION_FLAG_ASYNC = 1u << 13,
    JS_REDUCTION_FLAG_GENERATOR = 1u << 14,
    JS_REDUCTION_FLAG_FOR_INIT = 1u << 15,
    JS_REDUCTION_FLAG_FOR_TEST = 1u << 16,
    JS_REDUCTION_FLAG_FOR_UPDATE = 1u << 17,
    JS_REDUCTION_FLAG_FOR_AWAIT = 1u << 18,
    JS_REDUCTION_FLAG_FOR_DECLARATION = 1u << 19,
    JS_REDUCTION_FLAG_CATCH_PARAM = 1u << 20,
    JS_REDUCTION_FLAG_DEFAULT = 1u << 21,
    JS_REDUCTION_FLAG_TRY_HANDLER = 1u << 22,
    JS_REDUCTION_FLAG_TRY_FINALIZER = 1u << 23,
    JS_REDUCTION_FLAG_IMPORT_DEFAULT = 1u << 24,
    JS_REDUCTION_FLAG_IMPORT_NAMESPACE = 1u << 25,
    JS_REDUCTION_FLAG_IMPORT_TYPE = 1u << 26,
    JS_REDUCTION_FLAG_EXPORT_SOURCE = 1u << 27,
    JS_REDUCTION_FLAG_EXPORT_STAR = 1u << 28,
    JS_REDUCTION_FLAG_EXPORT_NAMESPACE = 1u << 29,
    JS_REDUCTION_FLAG_TEMPLATE_TAIL = 1u << 30,
};

#define JS_REDUCTION_FLAG_YIELD_DELEGATE (UINT32_C(1) << 31)

typedef struct JsParseReduction {
    JsReductionKind kind;
    JsReductionForm form;
    SourceSpan span;
    JsToken introducer;
    JsToken operator_token;
    uint32_t flags;
    uint32_t child_count;
    uint32_t child_flags;
    uint8_t parameter_accessibility;
    bool parameter_readonly;
} JsParseReduction;

enum JsReductionChildFlags {
    JS_REDUCTION_CHILD_EMPTY_CONSEQUENT = 1u << 0,
    JS_REDUCTION_CHILD_MISSING_BODY = 1u << 1,
};

typedef bool (*JsParseReduceFn)(void* context, const JsParseReduction* reduction);

typedef struct JsParseSink {
    JsParseReduceFn reduce;
} JsParseSink;

void js_lexer_init(JsLexer* lexer, const char* source, size_t length);
void js_lexer_set_goal(JsLexer* lexer, JsLexGoal goal);
JsToken js_lexer_next(JsLexer* lexer);

JsParseStatus js_parser_parse_source(const char* source, size_t length,
        JsParseMode mode, const JsParseSink* sink, void* sink_context,
        JsParseMetrics* metrics, JsParseError* error);

#ifdef __cplusplus
}
#endif
