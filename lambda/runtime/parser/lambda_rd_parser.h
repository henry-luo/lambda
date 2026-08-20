#pragma once

// First-party Lambda source parser POC. The core has a C ABI so the lexer and
// recursive-descent/Pratt parser stay small and are usable without Tree-sitter.
// Type-pattern and static-path interiors remain delegated to their existing
// Lambda-side parsers during the direct-AST phase.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LambdaSourceSpan {
    uint32_t start_byte;
    uint32_t end_byte;
} LambdaSourceSpan;

typedef enum LambdaTokenKind {
    LAMBDA_TOK_EOF = 0,
    LAMBDA_TOK_ERROR,
    LAMBDA_TOK_NEWLINE,

    LAMBDA_TOK_IDENTIFIER,
    LAMBDA_TOK_BASE_TYPE,
    LAMBDA_TOK_INTEGER,
    LAMBDA_TOK_FLOAT,
    LAMBDA_TOK_DECIMAL,
    LAMBDA_TOK_SIZED_INTEGER,
    LAMBDA_TOK_SIZED_FLOAT,
    LAMBDA_TOK_IMAGINARY,
    LAMBDA_TOK_STRING,
    LAMBDA_TOK_SYMBOL,
    LAMBDA_TOK_BINARY,
    LAMBDA_TOK_DATETIME,
    LAMBDA_TOK_NAMED_VALUE,
    LAMBDA_TOK_PATTERN_ISLAND,

    LAMBDA_TOK_LET,
    LAMBDA_TOK_PUB,
    LAMBDA_TOK_VAR,
    LAMBDA_TOK_TYPE,
    LAMBDA_TOK_FN,
    LAMBDA_TOK_PN,
    LAMBDA_TOK_VIEW,
    LAMBDA_TOK_EDIT,
    LAMBDA_TOK_STATE,
    LAMBDA_TOK_ON,
    LAMBDA_TOK_IF,
    LAMBDA_TOK_ELSE,
    LAMBDA_TOK_MATCH,
    LAMBDA_TOK_CASE,
    LAMBDA_TOK_DEFAULT,
    LAMBDA_TOK_FOR,
    LAMBDA_TOK_WHILE,
    LAMBDA_TOK_BREAK,
    LAMBDA_TOK_CONTINUE,
    LAMBDA_TOK_RETURN,
    LAMBDA_TOK_RAISE,
    LAMBDA_TOK_IMPORT,
    LAMBDA_TOK_APPLY,
    LAMBDA_TOK_NOT,
    LAMBDA_TOK_DIV,
    LAMBDA_TOK_AND,
    LAMBDA_TOK_OR,
    LAMBDA_TOK_TO,
    LAMBDA_TOK_IS,
    LAMBDA_TOK_IN,
    LAMBDA_TOK_AT,
    LAMBDA_TOK_THAT,
    LAMBDA_TOK_WHERE,
    LAMBDA_TOK_ORDER,
    LAMBDA_TOK_BY,
    LAMBDA_TOK_GROUP,
    LAMBDA_TOK_INTO,
    LAMBDA_TOK_LIMIT,
    LAMBDA_TOK_OFFSET,
    LAMBDA_TOK_ASC,
    LAMBDA_TOK_DESC,
    LAMBDA_TOK_LAST,
    LAMBDA_TOK_AS,
    LAMBDA_TOK_EQ_WORD,
    LAMBDA_TOK_NE_WORD,
    LAMBDA_TOK_LT_WORD,
    LAMBDA_TOK_LE_WORD,
    LAMBDA_TOK_GE_WORD,
    LAMBDA_TOK_GT_WORD,

    LAMBDA_TOK_LPAREN,
    LAMBDA_TOK_RPAREN,
    LAMBDA_TOK_LBRACKET,
    LAMBDA_TOK_RBRACKET,
    LAMBDA_TOK_LBRACE,
    LAMBDA_TOK_RBRACE,
    LAMBDA_TOK_LT,
    LAMBDA_TOK_GT,
    LAMBDA_TOK_COMMA,
    LAMBDA_TOK_COLON,
    LAMBDA_TOK_SEMICOLON,
    LAMBDA_TOK_DOT,
    LAMBDA_TOK_DOT_QUESTION,
    LAMBDA_TOK_QUESTION,
    LAMBDA_TOK_CARET,
    LAMBDA_TOK_TILDE,
    LAMBDA_TOK_TILDE_INDEX,
    LAMBDA_TOK_PARENT,
    LAMBDA_TOK_SLASH,
    LAMBDA_TOK_PLUS,
    LAMBDA_TOK_PLUS_PLUS,
    LAMBDA_TOK_MINUS,
    LAMBDA_TOK_STAR,
    LAMBDA_TOK_STAR_STAR,
    LAMBDA_TOK_PERCENT,
    LAMBDA_TOK_AMPERSAND,
    LAMBDA_TOK_PIPE,
    LAMBDA_TOK_PIPE_FORWARD,
    LAMBDA_TOK_BANG,
    LAMBDA_TOK_EQ,
    LAMBDA_TOK_EQ_EQ,
    LAMBDA_TOK_BANG_EQ,
    LAMBDA_TOK_LT_EQ,
    LAMBDA_TOK_GT_EQ,
    LAMBDA_TOK_ARROW,
    LAMBDA_TOK_ELLIPSIS,
} LambdaTokenKind;

typedef struct LambdaToken {
    LambdaTokenKind kind;
    LambdaSourceSpan span;
    uint32_t line;
    uint32_t column;
} LambdaToken;

typedef struct LambdaLexer {
    const char* source;
    size_t length;
    size_t offset;
    uint32_t line;
    uint32_t column;
} LambdaLexer;

typedef enum LambdaParseStatus {
    LAMBDA_PARSE_OK = 0,
    LAMBDA_PARSE_INCOMPLETE,
    LAMBDA_PARSE_ERROR,
} LambdaParseStatus;

typedef struct LambdaParseError {
    LambdaSourceSpan span;
    uint64_t expected_token_bits[4];
    LambdaTokenKind actual_kind;
    const char* message;
} LambdaParseError;

typedef struct LambdaParseMetrics {
    uint32_t token_count;
    uint32_t reduction_count;
    uint32_t max_recursion_depth;
    uint64_t structural_hash;
} LambdaParseMetrics;

// These are intentionally syntax-level reductions.  Phase 1 fingerprints
// them; Phase 2 maps the same committed reductions to the shared AST helpers.
typedef enum LambdaReductionKind {
    LAMBDA_REDUCE_ATOM = 1,
    LAMBDA_REDUCE_PREFIX,
    LAMBDA_REDUCE_POSTFIX,
    LAMBDA_REDUCE_BINARY,
    LAMBDA_REDUCE_GROUP,
    LAMBDA_REDUCE_ARRAY,
    LAMBDA_REDUCE_MAP,
    LAMBDA_REDUCE_ELEMENT,
    LAMBDA_REDUCE_LET,
    LAMBDA_REDUCE_IF,
    LAMBDA_REDUCE_MATCH,
    LAMBDA_REDUCE_FOR,
    LAMBDA_REDUCE_FUNCTION,
    LAMBDA_REDUCE_DECLARATION,
    LAMBDA_REDUCE_STATEMENT,
    LAMBDA_REDUCE_DOCUMENT,
    LAMBDA_REDUCE_TYPE_SLOT,
    LAMBDA_REDUCE_PATH_SLOT,
    LAMBDA_REDUCE_LIST,
} LambdaReductionKind;

// The sink remains deliberately small. Phase 1 uses it for deterministic
// reduction fingerprints; Phase 2 will supply values backed by AstNode*.
typedef uint64_t LambdaParseValue;
typedef struct LambdaParseSink {
    LambdaParseValue (*reduce)(void* context, int reduction_kind,
        LambdaSourceSpan span, const LambdaParseValue* children,
        uint32_t child_count);
} LambdaParseSink;

void lambda_lexer_init(LambdaLexer* lexer, const char* source, size_t length);
LambdaToken lambda_lexer_next(LambdaLexer* lexer);
const char* lambda_token_kind_name(LambdaTokenKind kind);

// This entry point is implemented by the recursive-descent/Pratt core. It is
// intentionally separate from the Tree-sitter lambda_parser() compatibility
// wrapper while both front ends coexist.
LambdaParseStatus lambda_rd_parse_source(const char* source, size_t length,
    const LambdaParseSink* sink, void* sink_context, LambdaParseMetrics* metrics,
    LambdaParseError* error);

#ifdef __cplusplus
}
#endif
