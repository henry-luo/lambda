// Grammar loader: `grammar Name { ... }` body text → TypeGrammar (GM5v2).
//
// The parser hands the body over as one span (lambda_lexer_skip_block_body),
// the way type slots reach parse_type_pattern.cpp. This file parses it, builds
// the rule table, compiles `:=` rules through the island compiler, analyses
// nullability and left recursion, and runs the lint (GM9). Diagnostics use the
// script compiler's reporters and existing codes (GM5.2).

#include "grammar.hpp"
#include "../parser/lambda_rd_parser.h"
#include "../ast_build.hpp"
#include "../type_build.hpp"
#include "../transpiler.hpp"
#include "../parse_type_pattern.hpp"
#include "../re2_wrapper.hpp"
#include "../lambda-error.h"
#include "../../../lib/log.h"
#include "../../../lib/str.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/escape.h"
#include "../../../lib/arraylist.h"

#include <string.h>

namespace {

enum GTokKind : uint8_t {
    GT_END, GT_WORD, GT_STRING, GT_SYMBOL, GT_INT, GT_DEFINE, GT_EQ, GT_PIPE,
    GT_COMMA, GT_LPAREN, GT_RPAREN, GT_QUESTION, GT_STAR, GT_PLUS, GT_LBRACE,
    GT_RBRACE, GT_AMP, GT_BANG, GT_TILDE, GT_COLON, GT_DOT, GT_SEMI, GT_BAD,
};

struct GTok {
    GTokKind kind;
    const char* p;
    size_t len;
};

struct GrammarImport {
    String* name;
    SourceSpan span;
    uint32_t rules_before;      // named local rules written before the import
    String* entry_name;         // the imported grammar's entry rule
};

// A local `:=` rule waits for its island to compile until the imports are
// known, so the island can name an imported `:=` rule (§4.5).
struct PendingToken {
    GrammarRule* rule;
    const char* from;           // island body text
    const char* to;
};

struct Loader {
    Transpiler* tp;
    const char* begin;          // body text, inside the braces
    const char* end;
    const char* p;              // scan position, just past `tok`
    GTok tok;
    TypeGrammar* grammar;
    ArrayList* rules;           // GrammarRule*
    ArrayList* refs;            // GrammarExpr* (GX_RULE) awaiting resolution
    NameScope* token_scope;     // `:=` rules already compiled, for later islands
    ArrayList* imports;         // GrammarImport*, in source order
    ArrayList* pending_tokens;  // PendingToken*, local `:=` rules in source order
    bool failed;                // a syntax error ended the body parse
};

const char* const modifier_words[] = {"memo", "nospace", "hidden"};

// the Lambda lexer's identifier bytes decide what a "word" is
bool word_char(char c) {
    return lambda_lexer_is_ident_continue_byte(c);
}

// A literal is keyword-like — and so matches whole words only (§4.2) — when it
// is made of these. `$` continues a Lambda identifier but is punctuation in the
// guest languages (`$x` in XPath), so it never makes a literal keyword-like.
bool keyword_char(char c) {
    return c != '$' && word_char(c);
}

bool word_start(char c) {
    return word_char(c) && !str_char_is_digit(c);
}

bool tok_is(const GTok& tok, const char* word) {
    size_t n = strlen(word);
    return tok.kind == GT_WORD && tok.len == n && memcmp(tok.p, word, n) == 0;
}

bool tok_is_modifier(const GTok& tok) {
    for (const char* word : modifier_words) {
        if (tok_is(tok, word)) return true;
    }
    return false;
}

SourceSpan span_of(Loader* L, const char* from, const char* to) {
    uint32_t base = (uint32_t)(L->begin - L->tp->source);
    return (SourceSpan){base + (uint32_t)(from - L->begin), base + (uint32_t)(to - L->begin)};
}

SourceSpan tok_span(Loader* L, const GTok& tok) {
    size_t len = tok.len ? tok.len : (tok.p < L->end ? 1 : 0);
    return span_of(L, tok.p, tok.p + len);
}

GTok scan_tok(const char* p, const char* end) {
    p = type_pattern_skip_space(p, end);
    GTok t = {GT_END, p, 0};
    if (p >= end) return t;
    const char* q = p;
    char c = *q;
    if (word_start(c)) {
        while (q < end && word_char(*q)) q++;
        t.kind = GT_WORD;
    } else if (str_char_is_digit(c)) {
        while (q < end && str_char_is_digit(*q)) q++;
        t.kind = GT_INT;
    } else if (c == '"' || c == '\'') {
        q++;
        while (q < end && *q != c && *q != '\n') {
            if (*q == '\\' && q + 1 < end) q++;
            q++;
        }
        if (q >= end || *q != c) {
            t.kind = GT_BAD;
            t.len = (size_t)(q - p);
            return t;
        }
        q++;
        t.kind = c == '"' ? GT_STRING : GT_SYMBOL;
    } else if (c == ':' && q + 1 < end && q[1] == '=') {
        q += 2;
        t.kind = GT_DEFINE;
    } else {
        q++;
        switch (c) {
        case '=': t.kind = (q < end && *q == '=') ? GT_BAD : GT_EQ; break;
        case '|': t.kind = GT_PIPE; break;
        case ',': t.kind = GT_COMMA; break;
        case '(': t.kind = GT_LPAREN; break;
        case ')': t.kind = GT_RPAREN; break;
        case '?': t.kind = GT_QUESTION; break;
        case '*': t.kind = GT_STAR; break;
        case '+': t.kind = GT_PLUS; break;
        case '{': t.kind = GT_LBRACE; break;
        case '}': t.kind = GT_RBRACE; break;
        case '&': t.kind = GT_AMP; break;
        case '!': t.kind = GT_BANG; break;
        case '~': t.kind = GT_TILDE; break;
        case ':': t.kind = GT_COLON; break;
        case '.': t.kind = GT_DOT; break;
        case ';': t.kind = GT_SEMI; break;
        default: t.kind = GT_BAD; break;
        }
    }
    t.len = (size_t)(q - p);
    return t;
}

void next_tok(Loader* L) {
    L->tok = scan_tok(L->p, L->end);
    L->p = L->tok.p + L->tok.len;
}

GTok peek_tok(Loader* L) {
    return scan_tok(L->p, L->end);
}

// A statement of the body begins at `p`: a rule head `[memo|nospace|hidden]*
// name (= | :=)`, or an `import name` (§4.5).
bool rule_head_at(const char* p, const char* end) {
    GTok t = scan_tok(p, end);
    if (tok_is(t, "import") && scan_tok(t.p + t.len, end).kind == GT_WORD) {
        GTok name = scan_tok(t.p + t.len, end);
        GTok after = scan_tok(name.p + name.len, end);
        if (after.kind != GT_EQ && after.kind != GT_DEFINE) return true;
    }
    while (t.kind == GT_WORD) {
        GTok after = scan_tok(t.p + t.len, end);
        if (after.kind == GT_EQ || after.kind == GT_DEFINE) return true;
        if (!tok_is_modifier(t)) return false;
        t = after;
    }
    return false;
}

void syntax_error(Loader* L, const GTok& at, const char* message) {
    if (L->failed) return;
    L->failed = true;
    record_semantic_error_span(L->tp, tok_span(L, at), ERR_SYNTAX_ERROR,
        "grammar '%.*s': %s", (int)L->grammar->name->len, L->grammar->name->chars, message);
}

bool expect(Loader* L, GTokKind kind, const char* message) {
    if (L->tok.kind != kind) {
        syntax_error(L, L->tok, message);
        return false;
    }
    next_tok(L);
    return true;
}

// `from` is the expression's first source byte; NULL for a node the loader
// synthesizes, whose caller sets the span.
GrammarExpr* new_expr(Loader* L, GrammarExprKind kind, const char* from) {
    GrammarExpr* e = (GrammarExpr*)pool_calloc(L->tp->pool, sizeof(GrammarExpr));
    e->kind = kind;
    e->rule_index = GRAMMAR_NO_RULE;
    if (from) e->span = span_of(L, from, L->tok.p > from ? L->tok.p : from);
    return e;
}

GrammarExpr* wrap_expr(Loader* L, GrammarExprKind kind, GrammarExpr* child, const char* from) {
    GrammarExpr* e = new_expr(L, kind, from);
    e->items = (GrammarExpr**)pool_calloc(L->tp->pool, sizeof(GrammarExpr*));
    e->items[0] = child;
    e->item_count = 1;
    return e;
}

String* intern(Loader* L, const char* text, size_t len) {
    return name_pool_create_strview(L->tp->name_pool, (StrView){text, len});
}

GrammarRule* new_rule(Loader* L, String* name, SourceSpan span) {
    GrammarRule* rule = (GrammarRule*)pool_calloc(L->tp->pool, sizeof(GrammarRule));
    rule->name = name;
    rule->span = span;
    arraylist_append(L->rules, rule);
    return rule;
}

GrammarRule* rule_at(Loader* L, uint32_t index) {
    return (GrammarRule*)L->rules->data[index];
}

uint32_t rule_count(Loader* L) {
    return (uint32_t)L->rules->length;
}

// A token rule named inside another rule's island is used (lint): walk the
// island's resolved references to the token scope's definitions.
// A sequence's items chain through `next`; any other island node has none.
void mark_island_refs(Loader* L, AstNode* node) {
    for (; node; node = node->next) {
        switch (node->node_type) {
        case AST_NODE_IDENT: {
            NameEntry* entry = ((AstIdentNode*)node)->entry;
            if (entry && entry->scope == L->token_scope) {
                for (uint32_t i = 0; i < rule_count(L); i++) {
                    if (rule_at(L, i)->name == entry->name) rule_at(L, i)->used = true;
                }
            }
            break;
        }
        case AST_NODE_PATTERN_SEQ:
            mark_island_refs(L, ((AstPatternSeqNode*)node)->first);
            break;
        case AST_NODE_BINARY_TYPE:
            mark_island_refs(L, ((AstBinaryNode*)node)->left);
            mark_island_refs(L, ((AstBinaryNode*)node)->right);
            break;
        case AST_NODE_UNARY_TYPE:
            mark_island_refs(L, ((AstUnaryNode*)node)->operand);
            break;
        case AST_NODE_LIST_TYPE:
            mark_island_refs(L, ((AstListNode*)node)->item);
            break;
        default:
            break;
        }
    }
}

// Compile one island body as a token rule. Earlier `:=` rules and imported
// ones are in scope (compile_token_rules), so `ident := "\a" word*` names a
// sibling exactly as a pattern names a pattern.
// The island's domain is the rule's (GM4.2): `"..."` emits strings, `'...'`
// symbols; `allow_symbol` is false for an inline class literal, which is
// always double-quoted. `*body_out` receives the AST for publish_token_rule.
TypePattern* compile_token_body(Loader* L, const char* from, const char* to,
        SourceSpan span, String* rule_name, bool allow_symbol, AstNode** body_out) {
    const char* label = rule_name ? rule_name->chars : "";
    TypePatternFailure failure = {ERR_INVALID_LITERAL, NULL};
    AstNode* island = parse_pattern_island_syntax(L->tp, from, to, span, &failure);
    if (!island) {
        record_semantic_error_span(L->tp, span, failure.code, "grammar '%.*s', rule '%s': %s",
            (int)L->grammar->name->len, L->grammar->name->chars, label,
            failure.message ? failure.message : "invalid token pattern");
        return NULL;
    }
    NameScope* saved = L->tp->current_scope;
    L->tp->current_scope = L->token_scope;
    int errors_before = L->tp->error_count;
    resolve_type_pattern(L->tp, island);
    L->tp->current_scope = saved;
    Type* island_type = island->type;
    TypePattern* pattern = island_type && island_type->type_id == LMD_TYPE_TYPE &&
        island_type->kind == TYPE_KIND_PATTERN ? (TypePattern*)island_type : NULL;
    if (!pattern || !pattern->re2 || (pattern->is_symbol && !allow_symbol)) {
        if (L->tp->error_count == errors_before) {
            record_semantic_error_span(L->tp, span, ERR_SEMANTIC_ERROR,
                "grammar '%.*s', rule '%s': this token pattern cannot be compiled",
                (int)L->grammar->name->len, L->grammar->name->chars, label);
        }
        return NULL;
    }
    // compile the offset matcher now, not lazily from a running parse
    pattern_get_unanchored(pattern);
    mark_island_refs(L, ((AstPatternIslandNode*)island)->pattern);
    if (body_out) *body_out = ((AstPatternIslandNode*)island)->pattern;
    return pattern;
}

void publish_token_rule(Loader* L, GrammarRule* rule, AstNode* island_body) {
    rule->pattern_body = island_body;
    AstPatternDefNode* def = (AstPatternDefNode*)pool_calloc(L->tp->pool, sizeof(AstPatternDefNode));
    def->node_type = rule->pattern->is_symbol ? AST_NODE_SYMBOL_PATTERN : AST_NODE_STRING_PATTERN;
    def->name = rule->name;
    def->as = island_body;
    def->type = (Type*)rule->pattern;
    def->is_symbol = rule->pattern->is_symbol;
    def->source_span = rule->span;
    NameEntry* entry = (NameEntry*)pool_calloc(L->tp->pool, sizeof(NameEntry));
    entry->name = rule->name;
    entry->node = (AstNode*)def;
    entry->scope = L->token_scope;
    NameScope* scope = L->token_scope;
    if (!scope->first) scope->first = entry;
    else scope->last->next = entry;
    scope->last = entry;
    (void)name_scope_index_add(L->tp->pool, scope, entry);
}

// The literal's text, with the escapes of a quoted Lambda string. A class
// escape (`"\d"`) makes it a pattern instead (GM4.1): *is_class is set and the
// caller compiles the quoted source as an anonymous token rule.
bool decode_literal(Loader* L, const GTok& tok, StrBuf* out, bool* is_class) {
    *is_class = false;
    const char* q = tok.p + 1;
    const char* stop = tok.p + tok.len - 1;
    while (q < stop) {
        char c = *q++;
        if (c != '\\') { strbuf_append_char(out, c); continue; }
        if (q >= stop) break;
        char e = *q++;
        if (e == 'd' || e == 'w' || e == 's' || e == 'a') { *is_class = true; return true; }
        if (e != 'u') {
            if (!strchr("\"'\\/bfnrt", e)) {
                syntax_error(L, tok, "invalid quoted escape in a literal");
                return false;
            }
            strbuf_append_char(out, escape_decode_js_char(e));
            continue;
        }
        uint32_t cp = 0;
        size_t consumed = 0;
        if (q < stop && *q == '{') {
            q++;
            const char* digits = q;
            while (q < stop && str_hex_val(*q) >= 0 && cp <= 0x10FFFF) {
                cp = cp * 16 + (uint32_t)str_hex_val(*q++);
            }
            if (q == digits || q >= stop || *q++ != '}' || cp > 0x10FFFF) {
                syntax_error(L, tok, "invalid Unicode escape in a literal");
                return false;
            }
        } else if (escape_decode_utf16_escape(q, (size_t)(stop - q), true, &cp, &consumed)) {
            q += consumed;
        } else {
            syntax_error(L, tok, "invalid Unicode escape in a literal");
            return false;
        }
        char utf8[4];
        size_t n = str_utf8_encode(cp, utf8, sizeof(utf8));
        strbuf_append_str_n(out, utf8, n);
    }
    return true;
}

GrammarExpr* parse_choice(Loader* L);

// The name a `'quoted'` symbol spells (GM4.3), with Lambda's quoted escapes.
String* quoted_name(Loader* L) {
    StrBuf* text = strbuf_new();
    bool is_class = false;
    String* name = NULL;
    if (decode_literal(L, L->tok, text, &is_class)) {
        if (is_class || !text->length) {
            syntax_error(L, L->tok, "a quoted name holds no class escape and is not empty");
        } else {
            name = intern(L, text->str, text->length);
        }
    }
    strbuf_free(text);
    return name;
}

GrammarExpr* rule_ref(Loader* L, String* name, const char* from) {
    GrammarExpr* e = new_expr(L, GX_RULE, from);
    e->text = name;
    arraylist_append(L->refs, e);
    return e;
}

GrammarExpr* parse_literal(Loader* L) {
    GTok tok = L->tok;
    StrBuf* text = strbuf_new();
    bool is_class = false;
    bool ok = decode_literal(L, tok, text, &is_class);
    GrammarExpr* e = NULL;
    if (ok && is_class) {
        // an anonymous token rule, named by its own source spelling
        String* name = intern(L, tok.p, tok.len);
        uint32_t index = GRAMMAR_NO_RULE;
        for (uint32_t i = 0; i < rule_count(L); i++) {
            if (rule_at(L, i)->anonymous && rule_at(L, i)->name == name) index = i;
        }
        if (index == GRAMMAR_NO_RULE) {
            SourceSpan span = tok_span(L, tok);
            TypePattern* pattern = compile_token_body(L, tok.p, tok.p + tok.len, span,
                name, false, NULL);
            if (pattern) {
                GrammarRule* rule = new_rule(L, name, span);
                rule->is_token = true;
                rule->anonymous = true;
                rule->used = true;
                rule->pattern = pattern;
                rule->type = (Type*)pattern;
                index = rule_count(L) - 1;
            }
        }
        e = new_expr(L, GX_RULE, tok.p);
        e->rule_index = index;
        e->text = name;
    } else if (ok) {
        e = new_expr(L, GX_LITERAL, tok.p);
        e->text = intern(L, text->str ? text->str : "", text->length);
        bool word = e->text->len > 0;
        for (uint32_t i = 0; i < e->text->len; i++) {
            if (!keyword_char(e->text->chars[i])) { word = false; break; }
        }
        e->word_boundary = word;
    }
    strbuf_free(text);
    next_tok(L);
    return e;
}

GrammarExpr* parse_prec(Loader* L) {
    const char* from = L->tok.p;
    GrammarAssoc assoc = GM_ASSOC_NONE;
    bool explicit_none = false;
    next_tok(L);  // `prec`
    if (L->tok.kind == GT_DOT) {
        next_tok(L);
        if (tok_is(L->tok, "left")) assoc = GM_ASSOC_LEFT;
        else if (tok_is(L->tok, "right")) assoc = GM_ASSOC_RIGHT;
        // GM6.1: explicitly non-associative — bare `prec` without the warning
        else if (tok_is(L->tok, "none")) explicit_none = true;
        else { syntax_error(L, L->tok, "expected 'left', 'right' or 'none' after 'prec.'"); return NULL; }
        next_tok(L);
    }
    if (!expect(L, GT_LPAREN, "expected '(' after prec")) return NULL;
    if (L->tok.kind != GT_INT) { syntax_error(L, L->tok, "expected a precedence level"); return NULL; }
    uint32_t level = (uint32_t)strtoul(L->tok.p, NULL, 10);
    next_tok(L);
    if (!expect(L, GT_COMMA, "expected ',' after the precedence level")) return NULL;
    GrammarExpr* body = parse_choice(L);
    if (!body || !expect(L, GT_RPAREN, "expected ')' closing prec")) return NULL;
    GrammarExpr* e = wrap_expr(L, GX_PREC, body, from);
    e->prec = level;
    e->assoc = assoc;
    e->annotated = true;
    e->explicit_none = explicit_none;
    return e;
}

GrammarExpr* parse_atom(Loader* L) {
    const char* from = L->tok.p;
    switch (L->tok.kind) {
    case GT_STRING:
        return parse_literal(L);
    case GT_SYMBOL: {
        // GM4.3: single quotes keep their Lambda meaning — a symbol is a name,
        // so `'x'` refers to the rule named x, as a bare `x` does
        String* name = quoted_name(L);
        if (!name) return NULL;
        next_tok(L);
        return rule_ref(L, name, from);
    }
    case GT_LPAREN: {
        next_tok(L);
        GrammarExpr* inner = parse_choice(L);
        if (!inner || !expect(L, GT_RPAREN, "expected ')'")) return NULL;
        return inner;
    }
    case GT_WORD: {
        GTok after = peek_tok(L);
        if (tok_is(L->tok, "prec") && (after.kind == GT_LPAREN || after.kind == GT_DOT)) {
            return parse_prec(L);
        }
        if (tok_is(L->tok, "hidden") && after.kind == GT_LPAREN) {
            next_tok(L);
            next_tok(L);
            GrammarExpr* inner = parse_choice(L);
            if (!inner || !expect(L, GT_RPAREN, "expected ')' closing hidden")) return NULL;
            return wrap_expr(L, GX_HIDDEN, inner, from);
        }
        String* name = intern(L, L->tok.p, L->tok.len);
        next_tok(L);
        return rule_ref(L, name, from);
    }
    default:
        syntax_error(L, L->tok, "expected a rule name, a \"literal\", or '('");
        return NULL;
    }
}

GrammarExpr* parse_postfix(Loader* L) {
    const char* from = L->tok.p;
    // GM13: `~x` starts where the previous token ended (Tree-sitter's
    // token.immediate). It marks one literal or rule name, so `~x*` is `(~x)*`.
    GTok tilde = L->tok;
    bool immediate = tilde.kind == GT_TILDE;
    if (immediate) next_tok(L);
    GrammarExpr* atom = parse_atom(L);
    if (!atom) return NULL;
    if (immediate) {
        if (atom->kind != GX_LITERAL && atom->kind != GX_RULE) {
            syntax_error(L, tilde, "`~` marks a literal or a rule name");
            return NULL;
        }
        atom->immediate = true;
    }
    uint32_t min = 0, max = 0;
    switch (L->tok.kind) {
    case GT_QUESTION: min = 0; max = 1; next_tok(L); break;
    case GT_STAR: min = 0; max = 0; next_tok(L); break;
    case GT_PLUS: min = 1; max = 0; next_tok(L); break;
    case GT_LBRACE: {
        // S16.8.6v3 counts: {n}, {n+}, {n,m}
        next_tok(L);
        if (L->tok.kind != GT_INT) { syntax_error(L, L->tok, "expected a count"); return NULL; }
        min = max = (uint32_t)strtoul(L->tok.p, NULL, 10);
        next_tok(L);
        bool exact = true;
        if (L->tok.kind == GT_PLUS) { max = 0; exact = false; next_tok(L); }
        else if (L->tok.kind == GT_COMMA) {
            exact = false;
            next_tok(L);
            if (L->tok.kind != GT_INT) { syntax_error(L, L->tok, "expected the upper count"); return NULL; }
            max = (uint32_t)strtoul(L->tok.p, NULL, 10);
            next_tok(L);
            if (max < min || max == 0) { syntax_error(L, L->tok, "a count {n,m} needs 0 < m and n <= m"); return NULL; }
        }
        if (exact && min == 0) { syntax_error(L, L->tok, "a count {0} matches nothing"); return NULL; }
        if (!expect(L, GT_RBRACE, "expected '}' closing the count")) return NULL;
        break;
    }
    default:
        return atom;
    }
    GrammarExpr* e = wrap_expr(L, GX_REPEAT, atom, from);
    e->min = min;
    e->max = max;
    return e;
}

GrammarExpr* parse_prefixed(Loader* L) {
    const char* from = L->tok.p;
    if (L->tok.kind == GT_AMP || L->tok.kind == GT_BANG) {
        GrammarExprKind kind = L->tok.kind == GT_AMP ? GX_AND : GX_NOT;
        next_tok(L);
        GrammarExpr* operand = parse_prefixed(L);
        return operand ? wrap_expr(L, kind, operand, from) : NULL;
    }
    if ((L->tok.kind == GT_WORD || L->tok.kind == GT_SYMBOL) && peek_tok(L).kind == GT_COLON) {
        String* label = L->tok.kind == GT_WORD ? intern(L, L->tok.p, L->tok.len) : quoted_name(L);
        if (!label) return NULL;
        next_tok(L);
        next_tok(L);
        GrammarExpr* operand = parse_prefixed(L);
        if (!operand) return NULL;
        GrammarExpr* e = wrap_expr(L, GX_LABEL, operand, from);
        e->text = label;
        return e;
    }
    return parse_postfix(L);
}

GrammarExpr* collect(Loader* L, GrammarExprKind kind, GrammarExpr* first,
        GTokKind separator, GrammarExpr* (*item)(Loader*), const char* from) {
    if (L->tok.kind != separator) return first;
    ArrayList* items = arraylist_new(4);
    arraylist_append(items, first);
    while (L->tok.kind == separator) {
        next_tok(L);
        GrammarExpr* next = item(L);
        if (!next) { arraylist_free(items); return NULL; }
        arraylist_append(items, next);
    }
    GrammarExpr* e = new_expr(L, kind, from);
    e->item_count = (uint32_t)items->length;
    e->items = (GrammarExpr**)pool_calloc(L->tp->pool, sizeof(GrammarExpr*) * e->item_count);
    memcpy(e->items, items->data, sizeof(GrammarExpr*) * e->item_count);
    arraylist_free(items);
    return e;
}

GrammarExpr* parse_seq(Loader* L) {
    const char* from = L->tok.p;
    GrammarExpr* first = parse_prefixed(L);
    return first ? collect(L, GX_SEQ, first, GT_COMMA, parse_prefixed, from) : NULL;
}

GrammarExpr* parse_choice(Loader* L) {
    const char* from = L->tok.p;
    GrammarExpr* first = parse_seq(L);
    return first ? collect(L, GX_CHOICE, first, GT_PIPE, parse_seq, from) : NULL;
}

// The body of a `:=` rule runs to the next rule head, a `;`, or the end. An
// island juxtaposes its parts, so only the head (`name =`, `name :=`, with any
// modifiers before it) can tell where the next rule starts (§4.1).
const char* token_body_end(Loader* L) {
    const char* p = L->p;
    int depth = 0;
    for (;;) {
        GTok t = scan_tok(p, L->end);
        if (t.kind == GT_END) return t.p;
        if (depth == 0 && t.kind == GT_SEMI) return t.p;
        if (depth == 0 && t.kind == GT_WORD && rule_head_at(t.p, L->end)) return t.p;
        if (t.kind == GT_LPAREN) depth++;
        if (t.kind == GT_RPAREN && depth > 0) depth--;
        p = t.p + (t.len ? t.len : 1);
    }
}

void parse_rule(Loader* L) {
    bool memo = false, nospace = false, hidden = false;
    while (L->tok.kind == GT_WORD && tok_is_modifier(L->tok)) {
        GTok after = peek_tok(L);
        if (after.kind == GT_EQ || after.kind == GT_DEFINE) break;
        if (tok_is(L->tok, "memo")) memo = true;
        else if (tok_is(L->tok, "nospace")) nospace = true;
        else hidden = true;
        next_tok(L);
    }
    if (L->tok.kind != GT_WORD) {
        syntax_error(L, L->tok, "expected a rule: `name = ...` or `name := ...`");
        return;
    }
    GTok name_tok = L->tok;
    String* name = intern(L, name_tok.p, name_tok.len);
    next_tok(L);
    for (uint32_t i = 0; i < rule_count(L); i++) {
        GrammarRule* other = rule_at(L, i);
        if (!other->anonymous && other->name == name) {
            record_semantic_error_span(L->tp, tok_span(L, name_tok), ERR_DUPLICATE_DEFINITION,
                "grammar '%.*s': rule '%.*s' is defined twice",
                (int)L->grammar->name->len, L->grammar->name->chars,
                (int)name->len, name->chars);
            break;
        }
    }
    if (L->tok.kind == GT_DEFINE) {
        const char* from = L->p;
        const char* to = token_body_end(L);
        GrammarRule* rule = new_rule(L, name, span_of(L, name_tok.p, to));
        rule->is_token = true;
        rule->hidden = hidden || name->chars[0] == '_';
        rule->is_space = name->len == 5 && memcmp(name->chars, "space", 5) == 0;
        PendingToken* pending = (PendingToken*)pool_calloc(L->tp->pool, sizeof(PendingToken));
        pending->rule = rule;
        pending->from = from;
        pending->to = to;
        arraylist_append(L->pending_tokens, pending);
        L->p = to;
        next_tok(L);
        return;
    }
    if (L->tok.kind != GT_EQ) {
        syntax_error(L, L->tok, "expected '=' or ':=' after the rule name");
        return;
    }
    if (name->len == 5 && memcmp(name->chars, "space", 5) == 0) {
        // GM3: `space` runs between tokens, so it is itself a token
        record_semantic_error_span(L->tp, tok_span(L, name_tok), ERR_SEMANTIC_ERROR,
            "grammar '%.*s': the space rule is a token rule; write `space := ...`",
            (int)L->grammar->name->len, L->grammar->name->chars);
    }
    next_tok(L);
    GrammarRule* rule = new_rule(L, name, span_of(L, name_tok.p, name_tok.p + name_tok.len));
    rule->memo = memo;
    rule->nospace = nospace;
    rule->hidden = hidden || name->chars[0] == '_';
    rule->body = parse_choice(L);
    if (!rule->body) return;
    rule->span = span_of(L, name_tok.p, L->tok.p);
    if (L->tok.kind != GT_END && L->tok.kind != GT_SEMI &&
            !(L->tok.kind == GT_WORD && rule_head_at(L->tok.p, L->end))) {
        syntax_error(L, L->tok, "expected ',' or '|' between items, or the next rule");
    }
}

void compile_token_rule(Loader* L, PendingToken* pending) {
    GrammarRule* rule = pending->rule;
    AstNode* island_body = NULL;
    rule->pattern = compile_token_body(L, pending->from, pending->to,
        span_of(L, pending->from, pending->to), rule->name, true, &island_body);
    if (!rule->pattern) return;
    rule->type = (Type*)rule->pattern;
    rule->word_boundary = pattern_is_literal_set(island_body, keyword_char);
    // `_lparen := "("` reads as `"("` in an error, not as its name
    String* source = rule->pattern->source;
    if (island_body && island_body->node_type == AST_NODE_PRIMARY &&
            pattern_is_literal_set(island_body, NULL) && source && source->len > 3) {
        rule->display = intern(L, source->chars + 2, source->len - 3);
    }
    publish_token_rule(L, rule, island_body);
}

// An island sees every imported `:=` rule that no local rule overrides (the
// composed table already holds exactly those), and the local `:=` rules
// written before it, as before composition existed.
void compile_token_rules(Loader* L) {
    for (uint32_t i = 0; i < rule_count(L); i++) {
        GrammarRule* rule = rule_at(L, i);
        if (rule->imported && rule->is_token && rule->pattern_body) {
            publish_token_rule(L, rule, rule->pattern_body);
        }
    }
    for (int i = 0; i < L->pending_tokens->length; i++) {
        compile_token_rule(L, (PendingToken*)L->pending_tokens->data[i]);
    }
}

// --- composition (§4.5) ----------------------------------------------------

// `import g` inside a body: g's rules join this grammar unless a local rule of
// the same name overrides them. Statements are collected while parsing and
// composed once every local rule is known.
void parse_import(Loader* L) {
    GTok head = L->tok;
    next_tok(L);
    GrammarImport* import = (GrammarImport*)pool_calloc(L->tp->pool, sizeof(GrammarImport));
    import->name = intern(L, L->tok.p, L->tok.len);
    import->span = span_of(L, head.p, L->tok.p + L->tok.len);
    for (uint32_t i = 0; i < rule_count(L); i++) {
        if (!rule_at(L, i)->anonymous) import->rules_before++;
    }
    arraylist_append(L->imports, import);
    next_tok(L);
}

// A copy of an imported expression tree for re-analysis in this grammar: the
// import's own analysis (implied prec wrappers, call modes, resolved indexes)
// is undone, and references resolve again by name, so a local override also
// changes what the imported rules call.
GrammarExpr* clone_expr(Loader* L, GrammarExpr* e, SourceSpan span) {
    if (e->kind == GX_PREC && !e->annotated) return clone_expr(L, e->items[0], span);
    GrammarExpr* copy = (GrammarExpr*)pool_alloc(L->tp->pool, sizeof(GrammarExpr));
    *copy = *e;
    copy->span = span;
    copy->call_mode = GM_CALL_PLAIN;
    copy->lr_alt = false;
    copy->trailing_self = false;
    if (copy->kind == GX_RULE) {
        // names are interned per module and compared by pointer: a grammar
        // imported from another module spells its references in that pool
        copy->text = intern(L, e->text->chars, e->text->len);
        copy->prec = 0;
        copy->rule_index = GRAMMAR_NO_RULE;
        arraylist_append(L->refs, copy);
    }
    if (e->item_count) {
        copy->items = (GrammarExpr**)pool_calloc(L->tp->pool, sizeof(GrammarExpr*) * e->item_count);
        for (uint32_t i = 0; i < e->item_count; i++) copy->items[i] = clone_expr(L, e->items[i], span);
    }
    return copy;
}

int32_t find_rule_index(Loader* L, String* name) {
    for (uint32_t i = 0; i < rule_count(L); i++) {
        if (rule_at(L, i)->name == name) return (int32_t)i;
    }
    return -1;
}

void compose_imports(Loader* L) {
    uint32_t local_count = rule_count(L);
    for (int i = 0; i < L->imports->length; i++) {
        GrammarImport* import = (GrammarImport*)L->imports->data[i];
        NameEntry* entry = lookup_name(L->tp, (StrView){import->name->chars, import->name->len});
        AstNode* node = entry ? entry->node : NULL;
        TypeGrammar* other = node && node->node_type == AST_NODE_VARIABLE_DECLARATOR
            ? lambda_type_grammar(node->type) : NULL;
        if (!other || other == L->grammar) {
            record_semantic_error_span(L->tp, import->span, ERR_UNDEFINED_TYPE,
                "grammar '%.*s': no grammar named '%.*s' to import",
                (int)L->grammar->name->len, L->grammar->name->chars,
                (int)import->name->len, import->name->chars);
            continue;
        }
        if (other->entry_rule != GRAMMAR_NO_RULE) {
            String* entry_name = other->rules[other->entry_rule]->name;
            import->entry_name = intern(L, entry_name->chars, entry_name->len);
        }
        for (uint32_t k = 0; k < other->rule_count; k++) {
            GrammarRule* source = other->rules[k];
            // re-interned here, as clone_expr does for references (cross-module)
            String* name = intern(L, source->name->chars, source->name->len);
            int32_t existing = find_rule_index(L, name);
            if (existing >= 0 && (uint32_t)existing < local_count) continue;
            GrammarRule* copy = (GrammarRule*)pool_alloc(L->tp->pool, sizeof(GrammarRule));
            *copy = *source;
            copy->name = name;
            copy->body = source->body ? clone_expr(L, source->body, import->span) : NULL;
            copy->type = source->is_token ? source->type : NULL;
            copy->span = import->span;
            copy->imported = true;
            copy->used = true;
            copy->nullable = false;
            copy->left_recursive = false;
            // a later import overrides an earlier one, as a local rule does
            if (existing >= 0) L->rules->data[existing] = copy;
            else arraylist_append(L->rules, copy);
        }
    }
}

// --- analysis --------------------------------------------------------------

bool expr_nullable(GrammarExpr* e, GrammarRule** rules) {
    switch (e->kind) {
    case GX_SEQ:
        for (uint32_t i = 0; i < e->item_count; i++) {
            if (!expr_nullable(e->items[i], rules)) return false;
        }
        return true;
    case GX_CHOICE:
        for (uint32_t i = 0; i < e->item_count; i++) {
            if (expr_nullable(e->items[i], rules)) return true;
        }
        return false;
    case GX_REPEAT:
        return e->min == 0 || expr_nullable(e->items[0], rules);
    case GX_LITERAL:
        return e->text->len == 0;
    case GX_RULE:
        return e->rule_index != GRAMMAR_NO_RULE && rules[e->rule_index]->nullable;
    case GX_AND: case GX_NOT:
        return true;
    default:
        return expr_nullable(e->items[0], rules);
    }
}

// Rules that can be called at the position an expression starts at: the
// nullable-prefix graph of §5.3.
void first_refs(GrammarExpr* e, GrammarRule** rules, bool* out) {
    switch (e->kind) {
    case GX_SEQ:
        for (uint32_t i = 0; i < e->item_count; i++) {
            first_refs(e->items[i], rules, out);
            if (!expr_nullable(e->items[i], rules)) return;
        }
        return;
    case GX_CHOICE:
        for (uint32_t i = 0; i < e->item_count; i++) first_refs(e->items[i], rules, out);
        return;
    case GX_LITERAL:
        return;
    case GX_RULE:
        if (e->rule_index != GRAMMAR_NO_RULE) out[e->rule_index] = true;
        return;
    default:
        first_refs(e->items[0], rules, out);
        return;
    }
}

GrammarExpr* unwrap_label(GrammarExpr* e) {
    while (e && e->kind == GX_LABEL) e = e->items[0];
    return e;
}

GrammarExpr* leading_item(GrammarExpr* e) {
    while (e && (e->kind == GX_PREC || e->kind == GX_LABEL || e->kind == GX_SEQ)) {
        e = e->items[0];
    }
    return e;
}

GrammarExpr* trailing_item(GrammarExpr* alt) {
    GrammarExpr* body = alt->kind == GX_PREC ? alt->items[0] : alt;
    GrammarExpr* last = body->kind == GX_SEQ ? body->items[body->item_count - 1] : body;
    return unwrap_label(last);
}

void warn(Loader* L, SourceSpan span, LambdaErrorCode code, const char* rule, const char* fmt, ...) {
    char message[384];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    if (rule) {
        record_semantic_warning_span(L->tp, span, code, "grammar lint: grammar '%.*s', rule '%s': %s",
            (int)L->grammar->name->len, L->grammar->name->chars, rule, message);
    } else {
        record_semantic_warning_span(L->tp, span, code, "grammar lint: grammar '%.*s': %s",
            (int)L->grammar->name->len, L->grammar->name->chars, message);
    }
}

// GM6: classify the alternatives of a directly left-recursive rule and mark
// how each self-reference is called (§5.2 step 4).
void classify_left_recursion(Loader* L, uint32_t index, GrammarRule** rules) {
    GrammarRule* rule = rules[index];
    GrammarExpr* body = rule->body;
    if (body->kind != GX_CHOICE) {
        GrammarExpr* choice = wrap_expr(L, GX_CHOICE, body, NULL);
        choice->span = body->span;
        rule->body = body = choice;
    }
    bool* firsts = (bool*)pool_calloc(L->tp->pool, sizeof(bool) * L->rules->length);
    bool annotated = false, implied = false;
    for (uint32_t i = 0; i < body->item_count; i++) {
        GrammarExpr* alt = body->items[i];
        memset(firsts, 0, sizeof(bool) * L->rules->length);
        first_refs(alt, rules, firsts);
        bool lr = firsts[index];
        if (lr) {
            GrammarExpr* lead = leading_item(alt);
            if (!lead || lead->kind != GX_RULE || lead->rule_index != index) {
                record_semantic_error_span(L->tp, alt->span, ERR_CIRCULAR_DEPENDENCY,
                    "grammar '%.*s': rule '%.*s' is left-recursive through a nullable prefix "
                    "or a group; begin the alternative with '%.*s' itself (GM8)",
                    (int)L->grammar->name->len, L->grammar->name->chars,
                    (int)rule->name->len, rule->name->chars,
                    (int)rule->name->len, rule->name->chars);
                continue;
            }
            if (alt->kind != GX_PREC) {
                // an unannotated left-recursive alternative grows left-associatively
                GrammarExpr* prec = wrap_expr(L, GX_PREC, alt, NULL);
                prec->span = alt->span;
                prec->assoc = GM_ASSOC_LEFT;
                body->items[i] = alt = prec;
                implied = true;
            } else {
                annotated = true;
            }
            alt->lr_alt = true;
            lead->call_mode = GM_CALL_SEED;
        }
        if (alt->kind == GX_PREC) {
            GrammarExpr* tail = trailing_item(alt);
            if (tail && tail->kind == GX_RULE && tail->rule_index == index &&
                    tail->call_mode != GM_CALL_SEED) {
                tail->call_mode = GM_CALL_TRAILING;
                tail->prec = alt->assoc == GM_ASSOC_RIGHT ? alt->prec : alt->prec + 1;
                alt->trailing_self = true;
                if (lr && alt->annotated && alt->assoc == GM_ASSOC_NONE && !alt->explicit_none &&
                        !rule->imported) {
                    warn(L, alt->span, ERR_SEMANTIC_ERROR, rule->name->chars,
                        "prec(%u, ...) has no associativity, so a chain such as `a op b op c` "
                        "will not parse; use prec.left or prec.right, or prec.none if that "
                        "is intended", alt->prec);
                }
            }
        }
    }
    if (annotated && implied && !rule->imported) {
        warn(L, rule->span, ERR_SEMANTIC_ERROR, rule->name->chars,
            "left-recursive alternatives without prec are read as prec.left(0); "
            "the precedence ladder may be incomplete");
    }
}

// GM8: left recursion through another rule. Reports each cycle once, at its
// lowest-numbered rule.
bool find_cycle(uint32_t from, uint32_t target, bool** edges, uint32_t n,
        bool* on_path, uint32_t* path, uint32_t* depth) {
    for (uint32_t next = 0; next < n; next++) {
        if (!edges[from][next] || next == from) continue;
        if (next == target) { path[(*depth)++] = next; return true; }
        if (on_path[next] || next < target) continue;
        on_path[next] = true;
        path[(*depth)++] = next;
        if (find_cycle(next, target, edges, n, on_path, path, depth)) return true;
        (*depth)--;
    }
    return false;
}

void check_indirect_left_recursion(Loader* L, GrammarRule** rules, uint32_t n) {
    bool** edges = (bool**)pool_calloc(L->tp->pool, sizeof(bool*) * n);
    for (uint32_t i = 0; i < n; i++) {
        edges[i] = (bool*)pool_calloc(L->tp->pool, sizeof(bool) * n);
        if (rules[i]->body) first_refs(rules[i]->body, rules, edges[i]);
    }
    bool* on_path = (bool*)pool_calloc(L->tp->pool, sizeof(bool) * n);
    uint32_t* path = (uint32_t*)pool_calloc(L->tp->pool, sizeof(uint32_t) * (n + 1));
    for (uint32_t i = 0; i < n; i++) {
        if (rules[i]->is_token) continue;
        memset(on_path, 0, sizeof(bool) * n);
        uint32_t depth = 0;
        path[depth++] = i;
        on_path[i] = true;
        if (!find_cycle(i, i, edges, n, on_path, path, &depth)) continue;
        StrBuf* chain = strbuf_new();
        for (uint32_t k = 0; k < depth; k++) {
            if (k) strbuf_append_str(chain, " -> ");
            strbuf_append_str_n(chain, rules[path[k]]->name->chars, rules[path[k]]->name->len);
        }
        record_semantic_error_span(L->tp, rules[i]->span, ERR_CIRCULAR_DEPENDENCY,
            "grammar '%.*s': rule '%.*s' is left-recursive through another rule (%s); "
            "only direct left recursion is supported (GM8)",
            (int)L->grammar->name->len, L->grammar->name->chars,
            (int)rules[i]->name->len, rules[i]->name->chars, chain->str);
        strbuf_free(chain);
    }
}

bool is_pure_literal(GrammarExpr* e) {
    return e && e->kind == GX_LITERAL;
}

bool literal_is_proper_prefix(GrammarExpr* shorter, GrammarExpr* longer) {
    return shorter->text->len < longer->text->len &&
        memcmp(shorter->text->chars, longer->text->chars, shorter->text->len) == 0;
}

// Lint pass over one expression tree (GM9), plus the literal reordering of
// §4.2: inside a choice, a literal never wins after its own prefix, so a
// longer literal moves ahead of the shorter one that would capture it.
void lint_expr(Loader* L, GrammarRule* rule, GrammarExpr* e, GrammarRule** rules) {
    if (!e) return;
    if (e->kind == GX_CHOICE) {
        for (uint32_t j = 1; j < e->item_count; j++) {
            if (!is_pure_literal(e->items[j])) continue;
            for (uint32_t i = 0; i < j; i++) {
                if (!is_pure_literal(e->items[i]) ||
                        !literal_is_proper_prefix(e->items[i], e->items[j])) continue;
                GrammarExpr* moved = e->items[j];
                memmove(&e->items[i + 1], &e->items[i], sizeof(GrammarExpr*) * (j - i));
                e->items[i] = moved;
                warn(L, moved->span, ERR_SEMANTIC_ERROR, rule->name->chars,
                    "moved \"%.*s\" before \"%.*s\": a literal never wins after its own prefix",
                    (int)moved->text->len, moved->text->chars,
                    (int)e->items[i + 1]->text->len, e->items[i + 1]->text->chars);
                break;
            }
        }
        for (uint32_t j = 1; j < e->item_count; j++) {
            GrammarExpr* lead_j = leading_item(e->items[j]);
            if (!lead_j || lead_j->kind != GX_LITERAL || is_pure_literal(e->items[j])) continue;
            for (uint32_t i = 0; i < j; i++) {
                if (is_pure_literal(e->items[i]) && literal_is_proper_prefix(e->items[i], lead_j)) {
                    warn(L, e->items[j]->span, ERR_SEMANTIC_ERROR, rule->name->chars,
                        "alternative %u starts with \"%.*s\", which alternative %u (\"%.*s\") "
                        "captures first", j + 1, (int)lead_j->text->len, lead_j->text->chars,
                        i + 1, (int)e->items[i]->text->len, e->items[i]->text->chars);
                    break;
                }
            }
        }
        for (uint32_t i = 0; i + 1 < e->item_count; i++) {
            if (expr_nullable(e->items[i], rules)) {
                warn(L, e->items[i + 1]->span, ERR_UNREACHABLE_CODE, rule->name->chars,
                    "alternative %u can match nothing and always succeeds, so the "
                    "alternatives after it are unreachable", i + 1);
                break;
            }
        }
        for (uint32_t i = 0; i < e->item_count; i++) {
            GrammarExpr* lead_i = leading_item(e->items[i]);
            if (!lead_i || lead_i->kind != GX_RULE || lead_i->call_mode == GM_CALL_SEED ||
                    lead_i->rule_index == GRAMMAR_NO_RULE) continue;
            GrammarRule* target = rules[lead_i->rule_index];
            if (target->is_token || target->memo || target->left_recursive) continue;
            for (uint32_t j = i + 1; j < e->item_count; j++) {
                GrammarExpr* lead_j = leading_item(e->items[j]);
                if (lead_j && lead_j->kind == GX_RULE && lead_j->rule_index == lead_i->rule_index &&
                        e->items[j] != lead_j) {
                    warn(L, e->items[j]->span, ERR_SEMANTIC_ERROR, rule->name->chars,
                        "alternatives %u and %u both begin with '%.*s', which is re-parsed "
                        "on backtracking; consider `memo %.*s = ...`", i + 1, j + 1,
                        (int)target->name->len, target->name->chars,
                        (int)target->name->len, target->name->chars);
                    i = e->item_count;
                    break;
                }
            }
        }
    }
    if (e->kind == GX_REPEAT && e->max == 0 && expr_nullable(e->items[0], rules)) {
        warn(L, e->span, ERR_SEMANTIC_ERROR, rule->name->chars,
            "a repetition of something that can match nothing; it stops when an "
            "iteration does not advance");
    }
    if (e->kind != GX_LITERAL && e->kind != GX_RULE) {
        for (uint32_t i = 0; i < e->item_count; i++) lint_expr(L, rule, e->items[i], rules);
    }
}

bool expr_has_literal(GrammarExpr* e) {
    if (!e) return false;
    if (e->kind == GX_LITERAL) return true;
    if (e->kind == GX_RULE) return false;
    for (uint32_t i = 0; i < e->item_count; i++) {
        if (expr_has_literal(e->items[i])) return true;
    }
    return false;
}

void resolve_refs(Loader* L) {
    for (int i = 0; i < L->refs->length; i++) {
        GrammarExpr* ref = (GrammarExpr*)L->refs->data[i];
        // an anonymous rule's name is its quoted spelling, so it never meets
        // a word name; cloned references to one resolve by that spelling
        int32_t found = find_rule_index(L, ref->text);
        if (found >= 0) ref->rule_index = (uint32_t)found;
        if (ref->rule_index == GRAMMAR_NO_RULE) {
            // GM5v2: rules are types, so an unknown rule is an unknown type
            record_semantic_error_span(L->tp, ref->span, ERR_UNDEFINED_TYPE,
                "grammar '%.*s': no rule named '%.*s'",
                (int)L->grammar->name->len, L->grammar->name->chars,
                (int)ref->text->len, ref->text->chars);
            continue;
        }
        GrammarRule* target = rule_at(L, ref->rule_index);
        target->used = true;
        if (ref->immediate && !target->is_token) {
            // only a token has a first character to keep the space away from
            record_semantic_error_span(L->tp, ref->span, ERR_SEMANTIC_ERROR,
                "grammar '%.*s': `~` marks a literal or a `:=` rule, and '%.*s' is a `=` rule",
                (int)L->grammar->name->len, L->grammar->name->chars,
                (int)ref->text->len, ref->text->chars);
        }
    }
}

void analyse(Loader* L) {
    uint32_t n = rule_count(L);
    GrammarRule** rules = L->grammar->rules;
    for (uint32_t i = 0; i < n; i++) {
        if (rules[i]->is_token && rules[i]->pattern) {
            rules[i]->nullable = pattern_full_match_chars(rules[i]->pattern, "", 0);
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (uint32_t i = 0; i < n; i++) {
            if (rules[i]->is_token || rules[i]->nullable || !rules[i]->body) continue;
            if (expr_nullable(rules[i]->body, rules)) { rules[i]->nullable = true; changed = true; }
        }
    }
    bool* firsts = (bool*)pool_calloc(L->tp->pool, sizeof(bool) * n);
    for (uint32_t i = 0; i < n; i++) {
        if (rules[i]->is_token || !rules[i]->body) continue;
        memset(firsts, 0, sizeof(bool) * n);
        first_refs(rules[i]->body, rules, firsts);
        rules[i]->left_recursive = firsts[i];
    }
    check_indirect_left_recursion(L, rules, n);
    for (uint32_t i = 0; i < n; i++) {
        if (rules[i]->left_recursive) classify_left_recursion(L, i, rules);
    }
    // a non-left-recursive alternative under prec still calls a trailing
    // self-reference at its level (a prefix operator's operand)
    for (uint32_t i = 0; i < n; i++) {
        if (rules[i]->left_recursive || !rules[i]->body) continue;
        GrammarExpr* body = rules[i]->body;
        uint32_t count = body->kind == GX_CHOICE ? body->item_count : 1;
        for (uint32_t k = 0; k < count; k++) {
            GrammarExpr* alt = body->kind == GX_CHOICE ? body->items[k] : body;
            if (alt->kind != GX_PREC) continue;
            GrammarExpr* tail = trailing_item(alt);
            if (tail && tail->kind == GX_RULE && tail->rule_index == i) {
                tail->call_mode = GM_CALL_TRAILING;
                tail->prec = alt->assoc == GM_ASSOC_RIGHT ? alt->prec : alt->prec + 1;
            }
        }
    }
}

void lint(Loader* L) {
    TypeGrammar* g = L->grammar;
    bool literals = false, syntax = false;
    for (uint32_t i = 0; i < g->rule_count; i++) {
        GrammarRule* rule = g->rules[i];
        // an imported rule was linted where its grammar was declared
        if (rule->anonymous || rule->imported) continue;
        if (!rule->is_token) {
            syntax = true;
            literals = literals || expr_has_literal(rule->body);
            lint_expr(L, rule, rule->body, g->rules);
        }
        if (!rule->used && i != g->entry_rule && !rule->is_space) {
            warn(L, rule->span, ERR_SEMANTIC_ERROR, rule->name->chars,
                "the rule is never used");
        }
    }
    if (syntax && literals && g->space_rule == GRAMMAR_NO_RULE) {
        warn(L, span_of(L, L->begin, L->begin), ERR_SEMANTIC_ERROR, NULL,
            "the grammar has no `space` rule, so whitespace between tokens is significant");
    }
    if (g->entry_rule != GRAMMAR_NO_RULE && g->rules[g->entry_rule]->is_space) {
        warn(L, g->rules[g->entry_rule]->span, ERR_SEMANTIC_ERROR, "space",
            "the first rule is the entry of `parse(text, g)`, and it is the space rule");
    }
}

}  // namespace

TypeGrammar* grammar_compile(Transpiler* tp, String* name, SourceSpan body) {
    TypeGrammar* grammar = (TypeGrammar*)pool_calloc(tp->pool, sizeof(TypeGrammar));
    grammar->type_id = LMD_TYPE_TYPE;
    grammar->kind = TYPE_KIND_GRAMMAR;
    grammar->type = &TYPE_STRING;
    grammar->name = name;
    grammar->space_rule = GRAMMAR_NO_RULE;
    grammar->entry_rule = GRAMMAR_NO_RULE;

    Loader L = {};
    L.tp = tp;
    L.begin = tp->source + body.start_byte;
    L.end = tp->source + body.end_byte;
    L.p = L.begin;
    L.grammar = grammar;
    L.rules = arraylist_new(16);
    L.refs = arraylist_new(32);
    L.token_scope = (NameScope*)pool_calloc(tp->pool, sizeof(NameScope));
    L.token_scope->parent = tp->current_scope;
    L.imports = arraylist_new(2);
    L.pending_tokens = arraylist_new(16);

    int errors_before = tp->error_count;
    next_tok(&L);
    while (!L.failed && L.tok.kind != GT_END) {
        if (L.tok.kind == GT_SEMI) { next_tok(&L); continue; }
        if (L.tok.kind == GT_BAD) { syntax_error(&L, L.tok, "unexpected character"); break; }
        if (tok_is(L.tok, "import") && peek_tok(&L).kind == GT_WORD) { parse_import(&L); continue; }
        parse_rule(&L);
    }
    if (!L.failed) compose_imports(&L);
    compile_token_rules(&L);

    grammar->rule_count = rule_count(&L);
    grammar->rules = (GrammarRule**)pool_calloc(tp->pool,
        sizeof(GrammarRule*) * (grammar->rule_count ? grammar->rule_count : 1));
    for (uint32_t i = 0; i < grammar->rule_count; i++) {
        GrammarRule* rule = rule_at(&L, i);
        grammar->rules[i] = rule;
        if (rule->anonymous) continue;
        if (grammar->entry_rule == GRAMMAR_NO_RULE) grammar->entry_rule = i;
        if (rule->is_space && rule->is_token) grammar->space_rule = i;
        if (!rule->is_token) {
            TypeGrammarRule* type = (TypeGrammarRule*)pool_calloc(tp->pool, sizeof(TypeGrammarRule));
            type->type_id = LMD_TYPE_TYPE;
            type->kind = TYPE_KIND_GRAMMAR_RULE;
            type->type = &TYPE_STRING;
            type->grammar = grammar;
            type->name = rule->name;
            type->index = i;
            rule->type = (Type*)type;
        }
    }
    // GM5.1 with composition: an import written before every local rule puts
    // its grammar's entry first (or the local rule that overrides it)
    if (L.imports->length) {
        GrammarImport* first = (GrammarImport*)L.imports->data[0];
        int32_t entry = first->rules_before == 0 && first->entry_name
            ? find_rule_index(&L, first->entry_name) : -1;
        if (entry >= 0) grammar->entry_rule = (uint32_t)entry;
    }
    if (!L.failed && grammar->entry_rule == GRAMMAR_NO_RULE) {
        record_semantic_error_span(tp, body, ERR_SYNTAX_ERROR,
            "grammar '%.*s' has no rules", (int)name->len, name->chars);
        L.failed = true;
    }
    if (!L.failed) resolve_refs(&L);
    if (!L.failed && tp->error_count == errors_before) {
        analyse(&L);
        if (tp->error_count == errors_before) lint(&L);
    }
    arraylist_free(L.rules);
    arraylist_free(L.refs);
    arraylist_free(L.imports);
    arraylist_free(L.pending_tokens);
    arraylist_append(tp->type_list, grammar);
    return grammar;
}

uint32_t grammar_find_rule(TypeGrammar* grammar, const char* name, size_t len) {
    if (!grammar || !name) return GRAMMAR_NO_RULE;
    for (uint32_t i = 0; i < grammar->rule_count; i++) {
        GrammarRule* rule = grammar->rules[i];
        if (!rule->anonymous && rule->name->len == len &&
                memcmp(rule->name->chars, name, len) == 0) return i;
    }
    return GRAMMAR_NO_RULE;
}

Type* grammar_rule_type(TypeGrammar* grammar, const char* name, size_t len) {
    uint32_t index = grammar_find_rule(grammar, name, len);
    return index == GRAMMAR_NO_RULE ? NULL : grammar->rules[index]->type;
}
