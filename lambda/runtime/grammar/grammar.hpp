#pragma once

// Lambda grammar module (GM1–GM12).
//
// A `grammar Name { ... }` declaration is a type (GM5v2): the loader turns its
// body into a TypeGrammar at compile time, entered in the module type list
// beside every other declared type, and each rule is a type too — a `:=` rule
// is the TypePattern its island body compiles to, a `=` rule a TypeGrammarRule.
// `parse(text, g)` / `parse(text, g, 'rule')` run the PEG interpreter in
// grammar_parse.cpp and build the parse tree as Lambda elements (GM4).
//
// Design: vibe/Lambda_Design_Grammar_Parser.md
// Plan:   vibe/impl/Lambda_Impl_Grammar_Parser.md

#include "../../lambda-data.hpp"
#include "../source_span.h"

struct Transpiler;
struct AstNode;

#define GRAMMAR_NO_RULE UINT32_MAX

typedef enum GrammarExprKind : uint8_t {
    GX_SEQ,         // a, b, c
    GX_CHOICE,      // a | b — ordered, first match wins (GM2)
    GX_REPEAT,      // a? a* a+ a{n} a{n+} a{n,m} — greedy, possessive
    GX_LITERAL,     // "lit" (GM4.1)
    GX_RULE,        // a reference to a `:=` or `=` rule
    GX_AND,         // &a — positive lookahead
    GX_NOT,         // !a — negative lookahead
    GX_PREC,        // prec / prec.left / prec.right (GM6)
    GX_LABEL,       // name: a — the child becomes an attribute
    GX_HIDDEN,      // hidden(a) — a rule's children splice into the caller
} GrammarExprKind;

typedef enum GrammarAssoc : uint8_t {
    GM_ASSOC_NONE,
    GM_ASSOC_LEFT,
    GM_ASSOC_RIGHT,
} GrammarAssoc;

// How a reference to the enclosing left-recursive rule is called (§5.2):
// the leading reference of a growing alternative takes the seed, a trailing
// one is called at the precedence-derived level, any other one at level 0.
typedef enum GrammarCallMode : uint8_t {
    GM_CALL_PLAIN,
    GM_CALL_SEED,
    GM_CALL_TRAILING,
} GrammarCallMode;

typedef struct GrammarExpr {
    GrammarExprKind kind;
    GrammarAssoc assoc;             // GX_PREC
    GrammarCallMode call_mode;      // GX_RULE inside a left-recursive rule
    bool word_boundary;             // GX_LITERAL made of word characters
    bool annotated;                 // GX_PREC written by the author (not implied)
    bool lr_alt;                    // GX_PREC: an alternative that grows the seed
    bool trailing_self;             // GX_PREC: ends with a reference to its own rule
    bool explicit_none;             // GX_PREC written `prec.none` (GM6.1): no lint warning
    bool immediate;                 // GX_LITERAL / token GX_RULE written `~x` (GM13): no space before it
    uint32_t min, max;              // GX_REPEAT; max 0 = unbounded
    uint32_t prec;                  // GX_PREC; GX_RULE with GM_CALL_TRAILING: the level
    uint32_t rule_index;            // GX_RULE
    String* text;                   // GX_LITERAL text, GX_LABEL name
    struct GrammarExpr** items;     // GX_SEQ / GX_CHOICE children; one child otherwise
    uint32_t item_count;
    SourceSpan span;
} GrammarExpr;

typedef struct GrammarRule {
    String* name;
    bool is_token;                  // `:=` rule
    bool is_space;                  // the rule named `space` (GM3)
    bool nospace;                   // no `space` skipping inside its body (§4.3)
    bool memo;                      // memoized (§4.6)
    bool hidden;                    // `_name` or `hidden`: children splice into the caller
    bool used;                      // referenced by another rule (lint)
    bool anonymous;                 // a class literal such as "\d" in a `=` rule
    bool word_boundary;             // a literal-only word token such as `for := 'for'`
    bool imported;                  // cloned from an `import`ed grammar (§4.5)
    String* display;                // a single-literal token shown as its literal in errors
    bool nullable;                  // analysis
    bool left_recursive;            // direct left recursion (GM6)
    GrammarExpr* body;              // syntax rules
    TypePattern* pattern;           // token rules (string domain)
    AstNode* pattern_body;          // token rules: the island AST a later island inlines by name
    Type* type;                     // the rule as a type: TypePattern or TypeGrammarRule
    SourceSpan span;
} GrammarRule;

// TypeGrammar / TypeGrammarRule live in lambda-data.hpp beside TypePattern,
// so core code (printing) can read a grammar type value.

// Loader (grammar_load.cpp). Parses the body text of a declaration, checks it,
// runs the lint, and returns the grammar entered in the module type list. Errors
// are reported through the script compiler's diagnostics; NULL when the body
// could not be built at all.
TypeGrammar* grammar_compile(Transpiler* tp, String* name, SourceSpan body);

// Rule lookup by spelling; GRAMMAR_NO_RULE when absent.
uint32_t grammar_find_rule(TypeGrammar* grammar, const char* name, size_t len);

// The member read `g.rule` (GM5v2): the rule's type, or NULL.
Type* grammar_rule_type(TypeGrammar* grammar, const char* name, size_t len);

// Engine (grammar_parse.cpp). Parses the whole of `src` from `entry` and builds
// the tree into `input` (GM4); with `spans`, every element also carries `start`
// and `end` byte offsets (GM11). On failure returns false and leaves the
// furthest-failure message in `input->parse_error_message` (GM5.2).
bool grammar_parse(TypeGrammar* grammar, uint32_t entry, Input* input,
    const char* src, size_t len, bool spans);

// Membership for `text is g` / `text is g.rule` (GM5.3): the rule parses the
// whole text. No tree is built.
bool grammar_accepts(TypeGrammar* grammar, uint32_t entry, const char* src, size_t len);
