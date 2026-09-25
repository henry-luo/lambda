#pragma once

// First-party Lambda source parser POC. The core has a C ABI so the lexer and
// recursive-descent/Pratt parser stay small and are usable without Tree-sitter.
// Type-pattern interiors remain delegated to the Lambda-side type parser;
// paths are built from this parser's own tokens (no second path grammar).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../source_span.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    // §7.15: `\` is the RELATIVE path root, as `/` is the logical one, so a
    // relative path reads `\.a.b` or `\[1]`. Respelling it away from a bare
    // `.` is what frees a line-start `.ident` to mean member access and
    // nothing else. The rooted form `/.a` is unchanged.
    LAMBDA_TOK_PATH_REL,

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
    // Tier-3 statement heads (PTH58, PTH62, PTH68v3). A statement head bars a
    // binding name (S16.10.1), which is exactly why `before`/`after`/`into`
    // stayed CLAUSE words instead: they only ever appear after something else.
    LAMBDA_TOK_PUT,
    LAMBDA_TOK_DEL,
    LAMBDA_TOK_COMMIT,
    LAMBDA_TOK_ROLLBACK,
    LAMBDA_TOK_OPEN,
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
    // PTH47: `~key` is the current key/index accessor. It replaces `~#` so that
    // `#` means force everywhere (S1.7); `~word` is the fused focal-accessor
    // family, of which `~~` (LAMBDA_TOK_PARENT) is the other member.
    LAMBDA_TOK_TILDE_KEY,
    // `~` fused with a word that is not in the accessor table. It exists so the
    // parser can name the table instead of reporting a bare lexical error.
    LAMBDA_TOK_TILDE_ACCESSOR,
    LAMBDA_TOK_PARENT,
    LAMBDA_TOK_SLASH,
    LAMBDA_TOK_PLUS,
    LAMBDA_TOK_PLUS_PLUS,
    LAMBDA_TOK_MINUS,
    LAMBDA_TOK_STAR,
    LAMBDA_TOK_STAR_STAR,
    LAMBDA_TOK_PERCENT,
    LAMBDA_TOK_AMPERSAND,
    // PTH32: the force step. A pure POSTFIX token with no prefix role, which is
    // why it joins the S16.2.2v3 continuation set: a line beginning `#name`
    // continues the chain above it.
    LAMBDA_TOK_HASH,
    LAMBDA_TOK_PIPE,
    LAMBDA_TOK_PIPE_FORWARD,
    // S10.1.6: `|:` is the filter stage of the pipe family. Like `|>` it can
    // only continue an expression, so a line may begin with it (S16.2.2v3).
    LAMBDA_TOK_PIPE_FILTER,
    LAMBDA_TOK_BANG,
    LAMBDA_TOK_EQ,
    LAMBDA_TOK_EQ_EQ,
    // PTH45v2: `===` is reference equality. Like `==` it is a pure continuation
    // token (S16.2.2v3) -- it has no prefix reading, so a line may start with it.
    LAMBDA_TOK_EQ_EQ_EQ,
    LAMBDA_TOK_BANG_EQ,
    LAMBDA_TOK_LT_EQ,
    LAMBDA_TOK_SUBTYPE,
    LAMBDA_TOK_GT_EQ,
    LAMBDA_TOK_ARROW,
    LAMBDA_TOK_ELLIPSIS,
} LambdaTokenKind;

typedef struct LambdaToken {
    LambdaTokenKind kind;
    SourceSpan span;
    uint32_t line;
    uint32_t column;
    // S16.1.1: a line break carries no meaning of its own, so NEWLINE never
    // reaches the parser as a token. It survives only as this flag, which arms
    // the S16.2.3 line-start classification on the token that follows.
    bool nl_before;
} LambdaToken;

typedef struct LambdaLexer {
    const char* source;
    size_t length;
    size_t offset;
    uint32_t line;
    uint32_t column;
    // set by a step dot: the number that follows is an IntKey step, so
    // `a.1.2` and `\.1.2` are keys 1 then 2, never key 1.2.
    bool after_step_dot;
} LambdaLexer;

typedef enum LambdaParseStatus {
    LAMBDA_PARSE_OK = 0,
    LAMBDA_PARSE_INCOMPLETE,
    LAMBDA_PARSE_ERROR,
} LambdaParseStatus;

typedef struct LambdaParseError {
    SourceSpan span;
    uint64_t expected_token_bits[4];
    LambdaTokenKind actual_kind;
    const char* message;
} LambdaParseError;

// A bounded diagnostic report for syntax-only editor/REPL checks. The direct
// AST entry point remains fail-fast; this report lets a caller recover at
// top-level separators without ever publishing partial reductions.
enum { LAMBDA_PARSE_MAX_DIAGNOSTICS = 16 };
typedef struct LambdaParseReport {
    LambdaParseStatus status;
    uint32_t error_count;
    bool recovered;
    LambdaParseError errors[LAMBDA_PARSE_MAX_DIAGNOSTICS];
} LambdaParseReport;

typedef struct LambdaParseMetrics {
    uint32_t token_count;
    uint32_t reduction_count;
    uint32_t max_recursion_depth;
    uint64_t structural_hash;
} LambdaParseMetrics;

// These are intentionally syntax-level reductions. The AST sink builds a
// syntax node for each; a sink-less parse only fingerprints them.
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
    LAMBDA_REDUCE_CONTENT,
    LAMBDA_REDUCE_CONTEXT,
    LAMBDA_REDUCE_ASSIGNMENT,
    LAMBDA_REDUCE_VIEW,
} LambdaReductionKind;

// A generic reduction kind is insufficient for a direct AST sink: an atom
// must retain whether it was an identifier or a literal, and a Pratt reduction
// must retain its operator spelling. The parser publishes that committed
// syntactic form here instead of asking the sink to re-lex source text.
typedef enum LambdaReductionForm {
    LAMBDA_REDUCTION_FORM_NONE = 0,
    LAMBDA_REDUCTION_FORM_TOKEN,
    LAMBDA_REDUCTION_FORM_DECOMPOSE,
    LAMBDA_REDUCTION_FORM_GROUP,
    LAMBDA_REDUCTION_FORM_CALL,
    LAMBDA_REDUCTION_FORM_INDEX,
    LAMBDA_REDUCTION_FORM_MEMBER,
    // PTH32: `expr#`. One child, the reference. The fragment sugar `p#name`
    // (PTH33) reduces to this followed by an ordinary MEMBER/INDEX, so nothing
    // downstream needs a second spelling for "force then navigate".
    LAMBDA_REDUCTION_FORM_FORCE,
    // Tier 3. PUT carries its clause in `flags` (see LAMBDA_REDUCTION_FLAG_PUT_*)
    // so one form covers `= v`, `before`, `after` and `into`.
    LAMBDA_REDUCTION_FORM_PUT,
    // The comma-joined `put`/`del` statement; children are its clauses.
    LAMBDA_REDUCTION_FORM_CRUD_SEQ,
    LAMBDA_REDUCTION_FORM_DEL,
    LAMBDA_REDUCTION_FORM_COMMIT,
    LAMBDA_REDUCTION_FORM_ROLLBACK,
    LAMBDA_REDUCTION_FORM_OPEN,
    // The alias binding has to exist BEFORE the block body is reduced, or `t`
    // inside `open t = doc { put t.a = 1 }` is an unbound name. Same shape as
    // FOR_BEGIN/FOR_END: a scope opens, the alias is declared into it, the body
    // reduces, the scope closes.
    LAMBDA_REDUCTION_FORM_OPEN_BEGIN,
    LAMBDA_REDUCTION_FORM_OPEN_END,
    LAMBDA_REDUCTION_FORM_QUERY,
    LAMBDA_REDUCTION_FORM_HANDLER,
    LAMBDA_REDUCTION_FORM_PROPAGATE,
    LAMBDA_REDUCTION_FORM_NAMED_ARGUMENT,
    LAMBDA_REDUCTION_FORM_MAP_ITEM,
    LAMBDA_REDUCTION_FORM_COMPUTED_MAP_ITEM,
    LAMBDA_REDUCTION_FORM_ELEMENT_ATTRIBUTE,
    LAMBDA_REDUCTION_FORM_COMPUTED_ELEMENT_ATTRIBUTE,
    LAMBDA_REDUCTION_FORM_CONTENT,
    LAMBDA_REDUCTION_FORM_PARAMETERS,
    LAMBDA_REDUCTION_FORM_PARAMETER,
    LAMBDA_REDUCTION_FORM_FUNCTION,
    LAMBDA_REDUCTION_FORM_MATCH_ARM,
    LAMBDA_REDUCTION_FORM_MATCH_ARM_BEGIN,
    LAMBDA_REDUCTION_FORM_MATCH_ARM_END,
    LAMBDA_REDUCTION_FORM_HANDLER_BEGIN,
    LAMBDA_REDUCTION_FORM_HANDLER_END,
    LAMBDA_REDUCTION_FORM_FOR_BINDING,
    LAMBDA_REDUCTION_FORM_FOR_LET,
    LAMBDA_REDUCTION_FORM_FOR_WHERE,
    LAMBDA_REDUCTION_FORM_FOR_GROUP,
    LAMBDA_REDUCTION_FORM_FOR_ORDER,
    LAMBDA_REDUCTION_FORM_FOR_LIMIT,
    LAMBDA_REDUCTION_FORM_FOR_OFFSET,
    LAMBDA_REDUCTION_FORM_FOR_WHILE,
    LAMBDA_REDUCTION_FORM_FOR_BEGIN,
    LAMBDA_REDUCTION_FORM_FOR_END,
    LAMBDA_REDUCTION_FORM_WHILE_BEGIN,
    LAMBDA_REDUCTION_FORM_WHILE_END,
    LAMBDA_REDUCTION_FORM_FOR_CLAUSES,
    LAMBDA_REDUCTION_FORM_FOR_GROUP_KEY,
    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_FIELD,
    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT,
    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONTENT,
    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT_BEGIN,
    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_CONSTRAINT_END,
    LAMBDA_REDUCTION_FORM_GROUP_BEGIN,
    LAMBDA_REDUCTION_FORM_GROUP_END,
    LAMBDA_REDUCTION_FORM_IF_BRANCH_BEGIN,
    LAMBDA_REDUCTION_FORM_IF_BRANCH_END,
    LAMBDA_REDUCTION_FORM_FUNCTION_BEGIN,
    LAMBDA_REDUCTION_FORM_FUNCTION_END,
    LAMBDA_REDUCTION_FORM_IMPORT,
    LAMBDA_REDUCTION_FORM_TYPE_ALIAS,
    LAMBDA_REDUCTION_FORM_TYPE_OBJECT,
    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_BEGIN,
    LAMBDA_REDUCTION_FORM_TYPE_OBJECT_END,
    LAMBDA_REDUCTION_FORM_VAR,
    LAMBDA_REDUCTION_FORM_THAT_BEGIN,
    LAMBDA_REDUCTION_FORM_THAT_END,
    LAMBDA_REDUCTION_FORM_VIEW,
    LAMBDA_REDUCTION_FORM_VIEW_STATE,
    LAMBDA_REDUCTION_FORM_VIEW_HANDLER,
    LAMBDA_REDUCTION_FORM_VIEW_BEGIN,
    LAMBDA_REDUCTION_FORM_VIEW_END,
    LAMBDA_REDUCTION_FORM_VIEW_HANDLER_BEGIN,
    LAMBDA_REDUCTION_FORM_VIEW_HANDLER_END,
    LAMBDA_REDUCTION_FORM_RETURN,
    LAMBDA_REDUCTION_FORM_BREAK,
    LAMBDA_REDUCTION_FORM_CONTINUE,
    LAMBDA_REDUCTION_FORM_TYPE_ALIAS_BEGIN,
} LambdaReductionForm;

enum {
    LAMBDA_REDUCTION_FLAG_OPTIONAL = 1u << 0,
    LAMBDA_REDUCTION_FLAG_VAR = 1u << 1,
    LAMBDA_REDUCTION_FLAG_PROC = 1u << 2,
    LAMBDA_REDUCTION_FLAG_BODY_BLOCK = 1u << 3,
    LAMBDA_REDUCTION_FLAG_FUNCTION_HEADER = 1u << 4,
    LAMBDA_REDUCTION_FLAG_VARIADIC = 1u << 5,
    LAMBDA_REDUCTION_FLAG_KEY_ONLY = 1u << 6,
    LAMBDA_REDUCTION_FLAG_INDEX_TYPED = 1u << 7,
    LAMBDA_REDUCTION_FLAG_PUBLIC = 1u << 8,
    LAMBDA_REDUCTION_FLAG_RAISED = 1u << 9,
    LAMBDA_REDUCTION_FLAG_TYPED = 1u << 10,
    LAMBDA_REDUCTION_FLAG_PIPE_INJECT = 1u << 11,
    LAMBDA_REDUCTION_FLAG_ANNOTATION_CONSTRAINT = 1u << 12,
    LAMBDA_REDUCTION_FLAG_DECOMPOSE_NAMED = 1u << 13,
    LAMBDA_REDUCTION_FLAG_RETURN_TYPE = 1u << 14,
    LAMBDA_REDUCTION_FLAG_ANNOTATION_BINDER = 1u << 15,
    LAMBDA_REDUCTION_FLAG_ANNOTATION_IMPLICIT_BINDER = 1u << 16,
    // S12.1.4v2: a `function` declaration (colour-polymorphic `fn`)
    LAMBDA_REDUCTION_FLAG_COLOUR_POLY = 1u << 17,
    // Which `put` clause was written (PTH70v4). Absent means `put target = v`.
    LAMBDA_REDUCTION_FLAG_PUT_BEFORE = 1u << 18,
    LAMBDA_REDUCTION_FLAG_PUT_AFTER = 1u << 19,
    LAMBDA_REDUCTION_FLAG_PUT_INTO = 1u << 20,
    // `x: T to e` re-reads its whole span as one range type; the two parts
    // reduced on the way are its children, which the range type supersedes.
    LAMBDA_REDUCTION_FLAG_ANNOTATION_RANGE = 1u << 21,
};

// A reduction value is whatever the sink returned for it. A sink must never
// return zero for a reduction the parser may pass on as a child: the parser
// reads zero as "no value" and substitutes the reduction's structural hash.
typedef uint64_t LambdaParseValue;
typedef struct LambdaParseReduction {
    LambdaReductionKind kind;
    LambdaReductionForm form;
    // This is the complete production range, including the left child of a
    // Pratt/postfix reduction. It is therefore directly usable as AstNode's
    // source_span without reconstructing a range from opaque child values.
    SourceSpan span;
    // The committed introducer/operator token. It is zeroed when the form has
    // no token-specific interpretation.
    LambdaToken detail_token;
    // A second committed token is used only where a production has two
    // source-facing names (for example `fn name` or `let name`). Keeping it in
    // the reduction avoids making the sink rediscover declaration spelling.
    LambdaToken secondary_token;
    uint32_t flags;
    // Decomposition names are valid only during the synchronous sink callback;
    // carrying their tokens keeps the sink from re-lexing the let span.
    const LambdaToken* name_tokens;
    uint32_t name_count;
    const LambdaParseValue* children;
    uint32_t child_count;
} LambdaParseReduction;

typedef struct LambdaParseSink {
    LambdaParseValue (*reduce)(void* context,
        const LambdaParseReduction* reduction);
} LambdaParseSink;

void lambda_lexer_init(LambdaLexer* lexer, const char* source, size_t length);
LambdaToken lambda_lexer_next(LambdaLexer* lexer);
// Re-reads a dot-led number (`.1`) as a `.` step introducer and returns that
// DOT token; the lexer resumes after the dot. The parser calls it after a
// leading root `/`, the one place only syntax can tell a step from a float.
LambdaToken lambda_lexer_rescan_dot_step(LambdaLexer* lexer, LambdaToken number);
// true when the word may not name a binding: a declaration/statement keyword,
// a base-type name, or a named value. Clause words and infix word operators
// are capture-safe and return false. See S16.10.1v2.
bool lambda_lexer_word_bars_binding(const char* text, size_t length);

// This entry point is implemented by the recursive-descent/Pratt core and is
// consumed by the direct AST sink.
LambdaParseStatus lambda_rd_parse_source(const char* source, size_t length,
    const LambdaParseSink* sink, void* sink_context, LambdaParseMetrics* metrics,
    LambdaParseError* error);

// Syntax-only entry point with separator-based recovery. It records the first
// error and then retries after top-level ';' boundaries. No sink is
// invoked, so recovered diagnostics cannot expose a partial AST.
LambdaParseStatus lambda_rd_parse_recovering(const char* source, size_t length,
    LambdaParseReport* report);

#ifdef __cplusplus
}
#endif
