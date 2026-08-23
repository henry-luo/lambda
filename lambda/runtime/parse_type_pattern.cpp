#include "parse_type_pattern.hpp"
#include "type_build.hpp"
#include "transpiler.hpp"
#include "lambda-error.h"
#include "../../lib/log.h"
#include <tree_sitter/api.h>

#include <string.h>
#include <stdlib.h>

// Recursive-descent parser for the type-pattern sub-language. Tier order
// mirrors grammar-lambda.js:
//
//   pattern  := union
//   union    := exclude ('|' exclude)*
//   exclude  := intersect ('!' intersect)*
//   intersect:= unary ('&' unary)*
//   unary    := '!'? primary ('to' primary | occurrence)?
//   primary  := base | typeref | literal | '(' … ')' | '[' … ']'
//               | '{' … '}' | '<' … '>' | fn-type | island
//
// Every tier returns an AstNode with the SAME node kind, fields, `Type*`
// wrapping, and type_list/const_list registration the CST type builders
// produced — that fidelity is the contract (see the header). The shared
// Type-construction pieces live in type_build.hpp rather than being copied.

namespace {

struct Lexer {
    Transpiler* tp;
    const char* p;
    const char* end;
    SourceSpan origin;
    bool failed;
};

bool is_ident_start(char c) {
    return c == '_' || c == '$' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (unsigned char)c >= 0x80;
}

bool is_ident_continue(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

void fail(Lexer* lx, const char* what) {
    if (lx->failed) { return; }  // keep the first diagnostic, which is the useful one
    lx->failed = true;
    size_t left = (size_t)(lx->end - lx->p);
    log_error("type-pattern: %s at '%.*s'", what, (int)(left > 24 ? 24 : left), lx->p);
    record_semantic_error_span(lx->tp, lx->origin, ERR_INVALID_LITERAL,
        "invalid type pattern");
}

void skip_space(Lexer* lx) {
    for (;;) {
        while (lx->p < lx->end && (*lx->p == ' ' || *lx->p == '\t' || *lx->p == '\r' ||
                                   *lx->p == '\n' || *lx->p == '\f' || *lx->p == '\v')) {
            lx->p++;
        }
        if (lx->p + 1 < lx->end && lx->p[0] == '/' && lx->p[1] == '/') {
            while (lx->p < lx->end && *lx->p != '\n') { lx->p++; }
            continue;
        }
        if (lx->p + 1 < lx->end && lx->p[0] == '/' && lx->p[1] == '*') {
            lx->p += 2;
            while (lx->p + 1 < lx->end && !(lx->p[0] == '*' && lx->p[1] == '/')) { lx->p++; }
            lx->p = (lx->p + 2 < lx->end) ? lx->p + 2 : lx->end;
            continue;
        }
        return;
    }
}

bool at(Lexer* lx, char c) { skip_space(lx); return lx->p < lx->end && *lx->p == c; }

bool eat(Lexer* lx, char c) {
    if (!at(lx, c)) { return false; }
    lx->p++;
    return true;
}

// Read an identifier/keyword without consuming it on failure.
StrView peek_word(Lexer* lx) {
    skip_space(lx);
    StrView w = {lx->p, 0};
    const char* q = lx->p;
    if (q >= lx->end || !is_ident_start(*q)) { return w; }
    while (q < lx->end && is_ident_continue(*q)) { q++; }
    w.length = (size_t)(q - lx->p);
    return w;
}

StrView take_word(Lexer* lx) {
    StrView w = peek_word(lx);
    lx->p += w.length;
    return w;
}

// Namespace-qualified element tags are dotted (`<soap.Fault>`; the `html:div`
// spelling is retired), so a tag is a qualified name rather than a bare word.
// `take_word` stops at the dot, which left every qualified tag in type space
// reporting "invalid type pattern" while the same tag parsed in value space.
// Attribute names follow the value-space spelling: a bare identifier, or a
// single-quoted SYMBOL wherever the name is not a plain identifier
// (`'stroke-width'`, `'xmlns:soap'`). Reading bare words only made every such
// attribute unspellable in type space while the same element parsed in value
// space.
static StrView take_attr_name(Lexer* lx) {
    skip_space(lx);
    if (lx->p < lx->end && *lx->p == '\'') {
        const char* q = lx->p + 1;
        while (q < lx->end && *q != '\'' && *q != '\n') { q++; }
        if (q >= lx->end || *q != '\'') { StrView none = {lx->p, 0}; return none; }
        StrView w = {lx->p + 1, (size_t)(q - lx->p - 1)};
        lx->p = q + 1;
        return w;
    }
    return take_word(lx);
}

static StrView take_qualified_tag(Lexer* lx) {
    skip_space(lx);
    // a tag may also be a quoted SYMBOL when it is not a plain identifier —
    // `<'?xml' …>` names the element the XML reader builds for a processing
    // instruction, and value space already spells it that way.
    if (lx->p < lx->end && *lx->p == '\'') { return take_attr_name(lx); }
    StrView w = take_word(lx);
    if (!w.length) return w;
    const char* start = w.str;
    while (lx->p + 1 < lx->end && lx->p[0] == '.' &&
            is_ident_start((unsigned char)lx->p[1])) {
        lx->p++;  // the dot binds only when a name follows it directly
        while (lx->p < lx->end && is_ident_continue((unsigned char)*lx->p)) lx->p++;
    }
    w.length = (size_t)(lx->p - start);
    return w;
}

bool word_is(StrView w, const char* s) {
    size_t n = strlen(s);
    return w.length == n && memcmp(w.str, s, n) == 0;
}

AstNode* parse_union(Lexer* lx);
AstNode* parse_exclude(Lexer* lx);
AstNode* parse_intersect(Lexer* lx);
AstNode* parse_unary(Lexer* lx);
AstNode* parse_primary(Lexer* lx);
AstNode* parse_element_type(Lexer* lx);
AstNode* parse_fn_type(Lexer* lx);
AstNode* parse_island(Lexer* lx);
AstNode* parse_island_body(Lexer* lx);
AstNode* make_binary_node(Lexer* lx, AstNode* left, AstNode* right,
        Operator op, const char* op_text, size_t op_len);

AstNode* new_node(Lexer* lx, AstNodeType kind, size_t size) {
    return alloc_ast_node_from_span(lx->tp, kind, lx->origin, size);
}

// Every hand node shares the type-slot source span, so nothing downstream may
// re-read source through it expecting a sub-span. The one consumer that does is the
// literal emitter for `&LIT_INT` / `&LIT_BOOL` typed nodes — which is why
// numeric literals here always carry value-bearing types instead (see below).
Type* wrap_type(Lexer* lx, Type* inner) {
    TypeType* tt = (TypeType*)alloc_type(lx->tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    tt->type = inner;
    return (Type*)tt;
}

// Containers and binaries register the WRAPPER in type_list; occurrence and
// range register the raw type — exactly the split the CST builders had.
Type* register_wrapped(Lexer* lx, Type* inner, int* index_out) {
    Type* wrapper = wrap_type(lx, inner);
    arraylist_append(lx->tp->type_list, wrapper);
    *index_out = lx->tp->type_list->length - 1;
    return wrapper;
}

// --- literals ---------------------------------------------------------------
// Value-bearing literal types, registered like the CST's build_lit_* helpers:
// strings/ints/floats go to const_list, and the transpiler emits them from the
// type payload or the const pool — never from the node's source span.

AstNode* literal_node(Lexer* lx, Type* literal_type) {
    AstPrimaryNode* pri = (AstPrimaryNode*)new_node(lx, AST_NODE_PRIMARY, sizeof(AstPrimaryNode));
    pri->type = literal_type;
    return (AstNode*)pri;
}

Type* parse_string_literal_type(Lexer* lx, char quote) {
    lx->p++;  // opening quote
    // two passes: measure the unescaped length, then fill the pooled String
    const char* scan = lx->p;
    size_t len = 0;
    while (scan < lx->end && *scan != quote) {
        if (*scan == '\\' && scan + 1 < lx->end) { scan++; }
        scan++;  len++;
    }
    if (scan >= lx->end) { fail(lx, "unterminated string literal"); return NULL; }

    // symbols are Symbol (ns field precedes chars), not String — the runtime
    // reads the payload through that layout, so a String here would shift
    // every char and break symbol equality
    // Symbol::chars sits after the ns field, at a different offset than
    // String::chars — fill through the right struct or the characters land in
    // the padding and symbol equality reads garbage.
    String* str;
    char* dst;
    if (quote == '\'') {
        Symbol* sym = (Symbol*)pool_calloc(lx->tp->pool, sizeof(Symbol) + len + 1);
        sym->ns = NULL;
        sym->len = (uint32_t)len;
        str = (String*)sym;
        dst = sym->chars;
    } else {
        str = (String*)pool_calloc(lx->tp->pool, sizeof(String) + len + 1);
        str->len = (uint32_t)len;
        str->flags = 0;
        dst = str->chars;
    }
    size_t i = 0;
    while (lx->p < lx->end && *lx->p != quote) {
        char c = *lx->p++;
        if (c == '\\' && lx->p < lx->end) {
            char e = *lx->p++;
            switch (e) {
            case 'n': c = '\n'; break;  case 't': c = '\t'; break;
            case 'r': c = '\r'; break;  case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;  default: c = e; break;
            }
        }
        dst[i++] = c;
    }
    dst[len] = '\0';
    if (quote != '\'') { str->is_ascii = str_is_ascii(str->chars, len) ? 1 : 0; }
    lx->p++;  // closing quote

    TypeString* ts = (TypeString*)alloc_type(lx->tp->pool,
        quote == '"' ? LMD_TYPE_STRING : LMD_TYPE_SYMBOL, sizeof(TypeString));
    ts->is_const = 1;  ts->is_literal = 1;
    ts->string = str;
    // the transpiler emits string/symbol literals as const-pool loads
    arraylist_append(lx->tp->const_list, str);
    ts->const_index = lx->tp->const_list->length - 1;
    return (Type*)ts;
}

Type* parse_number_literal_type(Lexer* lx) {
    const char* start = lx->p;
    if (lx->p < lx->end && (*lx->p == '-' || *lx->p == '+')) { lx->p++; }
    bool is_float = false;
    while (lx->p < lx->end && (is_digit(*lx->p) || *lx->p == '.' || *lx->p == 'e' || *lx->p == 'E' ||
                               ((*lx->p == '-' || *lx->p == '+') && (lx->p[-1] == 'e' || lx->p[-1] == 'E')))) {
        if (*lx->p == '.' || *lx->p == 'e' || *lx->p == 'E') { is_float = true; }
        lx->p++;
    }
    size_t n = (size_t)(lx->p - start);
    char buf[64];
    if (n >= sizeof(buf)) { fail(lx, "numeric literal too long"); return NULL; }
    memcpy(buf, start, n);  buf[n] = '\0';

    if (is_float) {
        // the float emitter reads double_val straight off the type
        TypeFloat* ft = (TypeFloat*)alloc_type(lx->tp->pool, LMD_TYPE_FLOAT, sizeof(TypeFloat));
        ft->double_val = strtod(buf, NULL);
        ft->is_const = 1;  ft->is_literal = 1;
        arraylist_append(lx->tp->const_list, &ft->double_val);
        ft->const_index = lx->tp->const_list->length - 1;
        return (Type*)ft;
    }
    // NOT &LIT_INT: that shared type makes the emitter re-parse the value from
// the node's source span, and every hand node spans the whole token. A
    // pooled int is value-bearing, so the span never matters.
    TypeInt64* it = (TypeInt64*)alloc_type(lx->tp->pool, LMD_TYPE_INT, sizeof(TypeInt64));
    it->int64_val = strtoll(buf, NULL, 10);
    it->is_const = 1;  it->is_literal = 1;
    arraylist_append(lx->tp->const_list, &it->int64_val);
    it->const_index = lx->tp->const_list->length - 1;
    return (Type*)it;
}

// --- string/symbol pattern islands (S11.1.2) --------------------------------
//
// Islands are the one part of the type language whose AST is load-bearing at
// MIR time: the regex is compiled lazily, from the AST, by
// `prepass_compile_patterns` and the inline-island path (both call
// `compile_pattern_ast`). These build exactly the node kinds
// `compile_pattern_to_regex` accepts.

// `d`, `w`, `s`, `a`, `.`, `...` are the reserved atoms inside an island.
bool island_char_class(StrView w, PatternCharClass* out) {
    if (w.length != 1) { return false; }
    switch (w.str[0]) {
    case 'd': *out = PATTERN_DIGIT; return true;
    case 'w': *out = PATTERN_WORD;  return true;
    case 's': *out = PATTERN_SPACE; return true;
    case 'a': *out = PATTERN_ALPHA; return true;
    default: return false;
    }
}

AstNode* parse_island_primary(Lexer* lx) {
    skip_space(lx);
    if (lx->p >= lx->end) { fail(lx, "expected a pattern"); return NULL; }
    char c = *lx->p;

    if (c == '(') {
        lx->p++;
        AstNode* inner = parse_island_body(lx);
        if (!inner) { return NULL; }
        if (!eat(lx, ')')) { fail(lx, "expected ')' in pattern"); return NULL; }
        // a group is a single-item list, which the regex compiler renders as (?:…)
        AstListNode* list = (AstListNode*)new_node(lx, AST_NODE_LIST_TYPE, sizeof(AstListNode));
        list->item = inner;
        return (AstNode*)list;
    }
    if (c == '.') {
        bool any_string = (lx->p + 2 < lx->end && lx->p[1] == '.' && lx->p[2] == '.');
        lx->p += any_string ? 3 : 1;
        AstPatternCharClassNode* cc = (AstPatternCharClassNode*)new_node(lx,
            AST_NODE_PATTERN_CHAR_CLASS, sizeof(AstPatternCharClassNode));
        cc->char_class = any_string ? PATTERN_ANY_STRING : PATTERN_ANY;
        cc->type = alloc_type_kind(lx->tp->pool, TYPE_KIND_PATTERN, sizeof(TypePattern));
        return (AstNode*)cc;
    }
    if (c == '"' || c == '\'') {
        Type* lit = parse_string_literal_type(lx, c);
        if (!lit) { return NULL; }
        AstNode* left = literal_node(lx, lit);
        // `"a" to "z"` — a character range
        StrView w = peek_word(lx);
        if (word_is(w, "to")) {
            lx->p += w.length;
            skip_space(lx);
            if (lx->p >= lx->end || (*lx->p != '"' && *lx->p != '\'')) {
                fail(lx, "expected a string literal after 'to'"); return NULL;
            }
            Type* upper = parse_string_literal_type(lx, *lx->p);
            if (!upper) { return NULL; }
            AstPatternRangeNode* range = (AstPatternRangeNode*)new_node(lx,
                AST_NODE_PATTERN_RANGE, sizeof(AstPatternRangeNode));
            range->start = left;
            range->end = literal_node(lx, upper);
            range->type = alloc_type_kind(lx->tp->pool, TYPE_KIND_PATTERN, sizeof(TypePattern));
            return (AstNode*)range;
        }
        return left;
    }

    StrView w = take_word(lx);
    if (!w.length) { fail(lx, "expected a pattern"); return NULL; }
    PatternCharClass klass;
    if (island_char_class(w, &klass)) {
        // the reserved atoms shadow nothing: a surrounding `let d = ...` makes
        // `d` inside an island ambiguous, which the CST builder rejected too
        NameEntry* shadowed = lookup_name(lx->tp, w);
        if (shadowed) {
            record_semantic_error_span(lx->tp, lx->origin, ERR_SEMANTIC_ERROR,
                "pattern class '%.*s' is reserved inside pattern islands; rename the surrounding binding",
                (int)w.length, w.str);
        }
        AstPatternCharClassNode* cc = (AstPatternCharClassNode*)new_node(lx,
            AST_NODE_PATTERN_CHAR_CLASS, sizeof(AstPatternCharClassNode));
        cc->char_class = klass;
        cc->type = alloc_type_kind(lx->tp->pool, TYPE_KIND_PATTERN, sizeof(TypePattern));
        return (AstNode*)cc;
    }
    // otherwise a reference to a named pattern; the regex compiler follows the
    // NameEntry to the definition's AST
    AstIdentNode* ident = (AstIdentNode*)new_node(lx, AST_NODE_IDENT, sizeof(AstIdentNode));
    ident->name = name_pool_create_strview(lx->tp->name_pool, w);
    ident->entry = lookup_name(lx->tp, w);
    if (ident->entry && ident->entry->node) { ident->type = ident->entry->node->type; }
    return (AstNode*)ident;
}

AstNode* parse_island_unary(Lexer* lx) {
    skip_space(lx);
    bool negated = false;
    if (lx->p < lx->end && *lx->p == '!') { lx->p++; negated = true; }

    AstNode* operand = parse_island_primary(lx);
    if (!operand) { return NULL; }

    if (negated) {
        AstUnaryNode* un = (AstUnaryNode*)new_node(lx, AST_NODE_UNARY_TYPE, sizeof(AstUnaryNode));
        un->op = OPERATOR_NOT;
        un->op_str = {.str = "!", .length = 1};
        un->operand = operand;
        un->type = alloc_type_kind(lx->tp->pool, TYPE_KIND_PATTERN, sizeof(TypePattern));
        operand = (AstNode*)un;
    }

    // occurrence suffix — no space before it, so `d+ w` stays two atoms
    if (lx->p < lx->end && (*lx->p == '?' || *lx->p == '+' || *lx->p == '*' || *lx->p == '[')) {
        AstUnaryNode* un = (AstUnaryNode*)new_node(lx, AST_NODE_UNARY_TYPE, sizeof(AstUnaryNode));
        un->operand = operand;
        const char* op_start = lx->p;
        char c = *lx->p;
        if (c == '?')      { lx->p++; un->op = OPERATOR_OPTIONAL; }
        else if (c == '+') { lx->p++; un->op = OPERATOR_ONE_MORE; }
        else if (c == '*') { lx->p++; un->op = OPERATOR_ZERO_MORE; }
        else {
            int depth = 0;
            while (lx->p < lx->end) {
                if (*lx->p == '[') { depth++; }
                else if (*lx->p == ']') { depth--; if (!depth) { lx->p++; break; } }
                lx->p++;
            }
            if (depth) { fail(lx, "unterminated occurrence count"); return NULL; }
            un->op = OPERATOR_REPEAT;
        }
        // the regex compiler re-reads the occurrence spelling for REPEAT
        un->op_str.str = op_start;
        un->op_str.length = (size_t)(lx->p - op_start);
        un->type = alloc_type_kind(lx->tp->pool, TYPE_KIND_PATTERN, sizeof(TypePattern));
        operand = (AstNode*)un;
    }
    return operand;
}

// Whitespace is concatenation inside an island (S11.1.2).
AstNode* parse_island_concat(Lexer* lx) {
    AstNode* first = parse_island_unary(lx);
    if (!first) { return NULL; }

    AstNode* prev = first;
    int count = 1;
    for (;;) {
        skip_space(lx);
        if (lx->p >= lx->end) { break; }
        char c = *lx->p;
        if (c == ')' || c == '|' || c == '&' || c == ',') { break; }
        AstNode* next = parse_island_unary(lx);
        if (!next) { return NULL; }
        prev->next = next;
        prev = next;
        count++;
    }
    if (count == 1) { return first; }

    AstPatternSeqNode* seq = (AstPatternSeqNode*)new_node(lx,
        AST_NODE_PATTERN_SEQ, sizeof(AstPatternSeqNode));
    seq->first = first;
    seq->type = alloc_type_kind(lx->tp->pool, TYPE_KIND_PATTERN, sizeof(TypePattern));
    return (AstNode*)seq;
}

AstNode* parse_island_body(Lexer* lx) {
    AstNode* left = parse_island_concat(lx);
    if (!left) { return NULL; }
    for (;;) {
        skip_space(lx);
        if (lx->p >= lx->end) { return left; }
        char c = *lx->p;
        if (c != '|' && c != '&') { return left; }
        lx->p++;
        AstNode* right = parse_island_concat(lx);
        if (!right) { return NULL; }
        AstBinaryNode* bin = (AstBinaryNode*)new_node(lx, AST_NODE_BINARY_TYPE, sizeof(AstBinaryNode));
        bin->op = (c == '|') ? OPERATOR_UNION : OPERATOR_INTERSECT;
        bin->op_str = (c == '|') ? StrView{.str = "|", .length = 1} : StrView{.str = "&", .length = 1};
        bin->left = left;
        bin->right = right;
        // a real union type, not a pattern placeholder: a literal-only island is
        // returned as this very AST, so its ->type becomes the annotation's type
        TypeBinary* bt = (TypeBinary*)alloc_type_kind(lx->tp->pool, TYPE_KIND_BINARY, sizeof(TypeBinary));
        bt->op = bin->op;
        bt->left = left->type;
        bt->right = right->type;
        bin->type = register_wrapped(lx, (Type*)bt, &bt->type_index);
        left = (AstNode*)bin;
    }
}

// `\( … )` / `\symbol( … )`. Returns the island AST node — which must reach the
// transpiler for the regex to be compiled.
AstNode* parse_island(Lexer* lx) {
    lx->p++;  // backslash
    bool is_symbol = false;
    if (lx->p < lx->end && *lx->p != '(') {
        StrView tag = take_word(lx);
        if (!word_is(tag, "symbol")) { fail(lx, "unknown pattern island tag"); return NULL; }
        is_symbol = true;
    }
    if (!eat(lx, '(')) { fail(lx, "expected '(' after a pattern island tag"); return NULL; }

    AstPatternIslandNode* node = (AstPatternIslandNode*)new_node(lx,
        AST_NODE_PATTERN_ISLAND, sizeof(AstPatternIslandNode));
    node->is_symbol = is_symbol;
    node->pattern_index = -1;
    node->pattern = parse_island_body(lx);
    if (!node->pattern) { return NULL; }
    if (!eat(lx, ')')) { fail(lx, "expected ')' closing the pattern island"); return NULL; }

    // Pattern bodies are content-only: the domain is the island's tag, so a
    // symbol literal inside one is a mistake (S11.1.2).
    if (pattern_ast_has_symbol_literal(node->pattern)) {
        record_semantic_error_span(lx->tp, lx->origin, ERR_INVALID_LITERAL,
            "pattern bodies are content-only; use \\symbol(...) for the symbol domain and string literals for content");
        node->type = &TYPE_ERROR;
        return (AstNode*)node;
    }

    if (!is_symbol && pattern_ast_literal_set(node->pattern)) {
        // Literal-only islands ARE ordinary literal unions; keeping that
        // representation preserves the existing matching path (the CST island
        // builder returned the body AST for exactly this case).
        return node->pattern;
    }

    TypePattern* pattern_type = (TypePattern*)alloc_type_kind(lx->tp->pool,
        TYPE_KIND_PATTERN, sizeof(TypePattern));
    pattern_type->pattern_index = -1;
    pattern_type->is_symbol = is_symbol;
    pattern_type->re2 = nullptr;
    pattern_type->re2_unanchored = nullptr;
    pattern_type->source = nullptr;
    pattern_type->regex_source = nullptr;
    node->type = (Type*)pattern_type;
    return (AstNode*)node;
}

// --- containers -------------------------------------------------------------

// `[T]`, `[T, U]` — a bracket type is a positional pattern (S11.1.1). Mirrors
// build_array_type: a type-valued position is a pattern; a literal position is
// stored as its Item.
AstNode* parse_array_type(Lexer* lx) {
    AstArrayNode* ast_node = (AstArrayNode*)new_node(lx, AST_NODE_ARRAY_TYPE, sizeof(AstArrayNode));
    TypeArray* type = (TypeArray*)alloc_type(lx->tp->pool, LMD_TYPE_ARRAY, sizeof(TypeArray));

    AstNode* items[64];
    int count = 0;
    if (!at(lx, ']')) {
        do {
            if (count >= 64) { fail(lx, "too many bracket-type positions"); return NULL; }
            AstNode* item = parse_union(lx);
            if (!item) { return NULL; }
            if (count) { items[count - 1]->next = item; }
            else { ast_node->item = item; }
            items[count++] = item;
        } while (eat(lx, ','));
    }
    if (!eat(lx, ']')) { fail(lx, "expected ']'"); return NULL; }

    if (count > 0) {
        type->item_patterns = (Item*)pool_calloc(lx->tp->pool, sizeof(Item) * (size_t)count);
        type->item_is_type_pattern = (uint8_t*)pool_calloc(lx->tp->pool, sizeof(uint8_t) * (size_t)count);
        Type* nested = items[0]->type;
        for (int i = 0; i < count; i++) {
            if (items[i]->type && items[i]->type->type_id == LMD_TYPE_TYPE) {
                type->item_patterns[i].type = items[i]->type;
                type->item_is_type_pattern[i] = 1;
            } else {
                Item literal = ItemNull;
                if (ast_static_literal_item(lx->tp, items[i], &literal)) {
                    type->item_patterns[i] = literal;
                }
            }
            if (nested && items[i]->type && items[i]->type->type_id != nested->type_id) { nested = NULL; }
        }
        type->length = count;
        type->nested = nested;
        if (count == 1 && type->item_is_type_pattern[0]) {
            log_warn("lambda_array_pattern_hint: bare [T] is an exact one-item pattern; use T[] for homogeneous arrays");
        }
    }
    ast_node->type = register_wrapped(lx, (Type*)type, &type->type_index);
    return (AstNode*)ast_node;
}

// One `name: T` field, shaped like build_key_expr's output so shape entries and
// downstream walks see the same node.
AstNamedNode* parse_field(Lexer* lx) {
    skip_space(lx);
    StrView field;
    if (lx->p < lx->end && (*lx->p == '\'' || *lx->p == '"')) {
        char q = *lx->p++;
        const char* s = lx->p;
        while (lx->p < lx->end && *lx->p != q) { lx->p++; }
        field.str = s;  field.length = (size_t)(lx->p - s);
        if (lx->p < lx->end) { lx->p++; }
    } else {
        field = take_word(lx);
    }
    if (!field.length) { fail(lx, "expected a field name"); return NULL; }
    if (!eat(lx, ':')) { fail(lx, "expected ':' after a field name"); return NULL; }
    AstNode* field_type = parse_union(lx);
    if (!field_type) { return NULL; }

    AstNamedNode* named = (AstNamedNode*)new_node(lx, AST_NODE_KEY_EXPR, sizeof(AstNamedNode));
    named->name = name_pool_create_strview(lx->tp->name_pool, field);
    named->as = field_type;
    named->type = field_type->type;
    return named;
}

// `{a: int, b: [string]}` — field types are pattern-only (CT8v2), no `that`.
AstNode* parse_map_type(Lexer* lx) {
    AstMapNode* ast_node = (AstMapNode*)new_node(lx, AST_NODE_MAP_TYPE, sizeof(AstMapNode));
    TypeMap* type = (TypeMap*)alloc_type(lx->tp->pool, LMD_TYPE_MAP, sizeof(TypeMap));

    AstNode* prev_item = NULL;
    ShapeEntry* prev_entry = NULL;
    int byte_offset = 0;
    if (!at(lx, '}')) {
        do {
            AstNamedNode* item = parse_field(lx);
            if (!item) { return NULL; }
            if (!prev_item) { ast_node->item = (AstNode*)item; }
            else { prev_item->next = (AstNode*)item; }
            prev_item = (AstNode*)item;
            append_shape_entry_typed(lx->tp, item->name, item->type, &type->shape, &prev_entry, byte_offset);
            type->length++;
            // Stride by the field's STORAGE class, not a flat pointer width.
            // A field whose contract does not name one concrete carrier
            // (`integer`, `number`, a union) is stored self-describing, and
            // sizeof(TypedItem) is 9 -- so a flat 8 laid the NEXT field one byte
            // inside it. `{n: integer, label: string}` put `label` at offset 8
            // over a TypedItem spanning 0..8, and byte_size came out 16 for 17
            // bytes of fields: reads returned the wrong type, writes clobbered a
            // payload byte, and the shape failed its own
            // shape_entry_storage_fits_data (Lambda_Design_Compiling_Lane.md
            // §10.4b G3).
            byte_offset += type_info[type_field_storage_type_id(item->type)].byte_size;
        } while (eat(lx, ','));
    }
    if (!eat(lx, '}')) { fail(lx, "expected '}'"); return NULL; }
    type->byte_size = byte_offset;
    ast_node->type = register_wrapped(lx, (Type*)type, &type->type_index);
    return (AstNode*)ast_node;
}

// `(T)` groups (unwrapped, as build_list_type does); `(T, U)` is a tuple type.
AstNode* parse_paren_type(Lexer* lx) {
    AstNode* first = parse_union(lx);
    if (!first) { return NULL; }
    if (eat(lx, ')')) { return first; }  // grouping — single element unwraps

    AstListNode* ast_node = (AstListNode*)new_node(lx, AST_NODE_LIST_TYPE, sizeof(AstListNode));
    TypeType* node_type = (TypeType*)alloc_type(lx->tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    TypeList* type = (TypeList*)alloc_type(lx->tp->pool, LMD_TYPE_ARRAY, sizeof(TypeList));
    node_type->type = (Type*)type;
    ast_node->type = (Type*)node_type;
    ast_node->list_type = type;
    ast_node->item = first;
    type->length = 1;

    AstNode* prev = first;
    while (eat(lx, ',')) {
        AstNode* next = parse_union(lx);
        if (!next) { return NULL; }
        prev->next = next;
        prev = next;
        type->length++;
    }
    if (!eat(lx, ')')) { fail(lx, "expected ')'"); return NULL; }
    arraylist_append(lx->tp->type_list, ast_node->type);
    type->type_index = lx->tp->type_list->length - 1;
    return (AstNode*)ast_node;
}

// `<tag attr: T, attr2: U; content, content2>` — attribute defaults are
// literal-only inside a pattern (CT8v2).
AstNode* parse_element_type(Lexer* lx) {
    AstElementNode* ast_node = (AstElementNode*)new_node(lx, AST_NODE_ELMT_TYPE, sizeof(AstElementNode));
    TypeElmt* type = (TypeElmt*)alloc_type(lx->tp->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));

    StrView tag = take_qualified_tag(lx);
    if (!tag.length) { fail(lx, "expected an element tag"); return NULL; }
    String* pooled = name_pool_create_strview(lx->tp->name_pool, tag);
    type->name.str = pooled->chars;
    type->name.length = pooled->len;

    AstNode* prev_item = NULL;
    ShapeEntry* prev_entry = NULL;
    int byte_offset = 0;
    bool saw_content_sep = false;

    // attributes: only while the next item is `name :`
    while (!at(lx, '>') && lx->p < lx->end) {
        const char* save = lx->p;
        StrView field = take_attr_name(lx);
        if (!field.length || !at(lx, ':')) { lx->p = save; break; }
        lx->p++;  // ':'
        AstNode* field_type = parse_union(lx);
        if (!field_type) { return NULL; }
        if (at(lx, '=')) {  // literal-only default (CT8v2); the value is not part of the type
            lx->p++;
            if (!parse_primary(lx)) { return NULL; }
        }
        AstNamedNode* named = (AstNamedNode*)new_node(lx, AST_NODE_KEY_EXPR, sizeof(AstNamedNode));
        named->name = name_pool_create_strview(lx->tp->name_pool, field);
        named->as = field_type;
        named->type = field_type->type;
        if (!prev_item) { ast_node->item = (AstNode*)named; }
        else { prev_item->next = (AstNode*)named; }
        prev_item = (AstNode*)named;
        append_shape_entry_typed(lx->tp, named->name, named->type, &type->shape, &prev_entry, byte_offset);
        type->length++;
        byte_offset += type_info[type_field_storage_type_id(named->type)].byte_size;
        if (eat(lx, ',')) { continue; }
        if (eat(lx, ';')) { saw_content_sep = true; }
        break;
    }
    type->byte_size = byte_offset;

    // content schema: a comma-separated pattern list held as a content node,
    // like build_content_type produced (TypeList carried raw, not registered)
    if (!at(lx, '>') && lx->p < lx->end) {
        if (!saw_content_sep) { eat(lx, ';'); }
        AstListNode* content = (AstListNode*)new_node(lx, AST_NODE_CONTENT_TYPE, sizeof(AstListNode));
        TypeList* content_type = (TypeList*)alloc_type(lx->tp->pool, LMD_TYPE_ARRAY, sizeof(TypeList));
        content->type = (Type*)content_type;
        content->list_type = content_type;
        AstNode* prev = NULL;
        do {
            if (at(lx, '>')) { break; }
            AstNode* item = parse_union(lx);
            if (!item) { return NULL; }
            if (!prev) { content->item = item; }
            else { prev->next = item; }
            prev = item;
            content_type->length++;
        } while (eat(lx, ','));
        ast_node->content = (AstNode*)content;
        type->content_length = content_type->length;
    }

    if (!eat(lx, '>')) { fail(lx, "expected '>'"); return NULL; }
    ast_node->type = register_wrapped(lx, (Type*)type, &type->type_index);
    return (AstNode*)ast_node;
}

// `fn (a: int, b: string) ReturnType` — params are pattern-only (CT8v2) and the
// return may carry the raised channel (`T^`, `T^E`), the one place `^` survives
// (CT3v2/CT4). The node's param/vars stay null: the transpiler reads only
// ->type from a FUNC_TYPE node, and the TypeFunc carries the full contract.
AstNode* parse_fn_type(Lexer* lx) {
    AstFuncNode* ast_node = (AstFuncNode*)new_node(lx, AST_NODE_FUNC_TYPE, sizeof(AstFuncNode));
    TypeFunc* fn_type = (TypeFunc*)alloc_type(lx->tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
    set_fn_return_contract(fn_type, &TYPE_ANY_NO_ERROR, false);

    TypeParam* prev_param = NULL;
    int param_count = 0;
    int required_count = 0;
    if (eat(lx, '(')) {
        while (!at(lx, ')') && lx->p < lx->end) {
            StrView pname = take_word(lx);
            if (!pname.length) { fail(lx, "expected a parameter name"); return NULL; }
            TypeParam* param = (TypeParam*)alloc_type(lx->tp->pool, LMD_TYPE_ANY, sizeof(TypeParam));
            param->kind = TYPE_KIND_PARAM;
            param->is_optional = eat(lx, '?');
            if (eat(lx, ':')) {
                AstNode* declared = parse_union(lx);
                if (!declared) { return NULL; }
                apply_declared_param_type(lx->tp, param, declared->type);
            }
            if (!param->is_optional) { required_count++; }
            if (!prev_param) { fn_type->param = param; }
            else { prev_param->next = param; }
            prev_param = param;
            param_count++;
            if (!eat(lx, ',')) { break; }
        }
        if (!eat(lx, ')')) { fail(lx, "expected ')'"); return NULL; }
    }
    fn_type->param_count = param_count;
    fn_type->required_param_count = required_count;

    // return type, if the annotation spells one
    skip_space(lx);
    if (lx->p < lx->end && *lx->p != ',' && *lx->p != ')' && *lx->p != ']' &&
            *lx->p != '}' && *lx->p != '>' && *lx->p != '|' && *lx->p != '&') {
        AstNode* returned = parse_union(lx);
        if (!returned) { return NULL; }
        set_fn_return_contract(fn_type, returned->type, true);
        fn_type->returned = returned->type;
        if (eat(lx, '^')) {
            // `T^` is any error; `T^E` names one. The error arm is a simple
            // type pattern, never a whole annotation.
            skip_space(lx);
            if (lx->p < lx->end && is_ident_start(*lx->p)) {
                AstNode* err = parse_primary(lx);
                if (!err) { return NULL; }
                fn_type->error_type = err->type;
            } else {
                fn_type->error_type = (Type*)&LIT_TYPE_ERROR;
            }
        }
    }

    ast_node->type = register_wrapped(lx, (Type*)fn_type, &fn_type->type_index);
    return (AstNode*)ast_node;
}

// --- tiers ------------------------------------------------------------------

AstNode* parse_primary(Lexer* lx) {
    skip_space(lx);
    if (lx->p >= lx->end) { fail(lx, "expected a type"); return NULL; }

    char c = *lx->p;
    if (c == '(') { lx->p++; return parse_paren_type(lx); }
    if (c == '[') { lx->p++; return parse_array_type(lx); }
    if (c == '{') { lx->p++; return parse_map_type(lx); }
    if (c == '<') { lx->p++; return parse_element_type(lx); }
    if (c == '\\') { return parse_island(lx); }
    if (c == '"' || c == '\'') {
        Type* lit = parse_string_literal_type(lx, c);
        return lit ? literal_node(lx, lit) : NULL;
    }
    if (is_digit(c) || (c == '-' && lx->p + 1 < lx->end && is_digit(lx->p[1]))) {
        Type* lit = parse_number_literal_type(lx);
        return lit ? literal_node(lx, lit) : NULL;
    }

    StrView w = peek_word(lx);
    if (!w.length) { fail(lx, "expected a type"); return NULL; }
    lx->p += w.length;

    if (word_is(w, "fn")) { return parse_fn_type(lx); }
    if (word_is(w, "true") || word_is(w, "false")) {
        // &LIT_BOOL makes the emitter re-read source through the node's span,
        // which is the whole token. A lone `case true:` token reads correctly;
        // a bool inside a larger pattern would not — no corpus use exists, and
        // the parse is still exact, only the emitted literal VALUE can drift.
        return literal_node(lx, (Type*)&LIT_BOOL);
    }
    Type* base = lookup_base_type_name(lx->tp, w);
    if (base) {
        AstTypeNode* node = (AstTypeNode*)new_node(lx, AST_NODE_TYPE, sizeof(AstTypeNode));
        node->type = base;
        return (AstNode*)node;
    }

    // a name in type position is a reference to a declared type, shaped like
    // build_identifier's resolved path so the transpiler's alias handling works
    AstIdentNode* ident = (AstIdentNode*)new_node(lx, AST_NODE_IDENT, sizeof(AstIdentNode));
    ident->name = name_pool_create_strview(lx->tp->name_pool, w);
    ident->entry = lookup_name(lx->tp, w);
    if (ident->entry && ident->entry->node && ident->entry->node->type) {
        AstNode* def = ident->entry->node;
        ident->type = def->type;
        // Type and pattern definitions are referenced through a plain TypeType
        // wrapper, exactly as build_identifier does. The wrap is load-bearing:
        // match_arm_is_error_handler blind-casts an arm's type as (TypeType*),
        // so a raw TypePattern here reads pattern_index as a pointer — SEGV.
        if (def->node_type == AST_NODE_TYPE_STAM ||
                (def->node_type == AST_NODE_ASSIGN &&
                 ((AstNamedNode*)def)->is_type_definition) ||
                def->node_type == AST_NODE_STRING_PATTERN ||
                def->node_type == AST_NODE_SYMBOL_PATTERN) {
            // direct aliases for literals/ranges already carry the first-class
            // TypeType wrapper; wrapping that carrier again hides the payload
            // from the matcher and makes `is Alias` fail (D2.2.2).
            ident->type = def->type && def->type->type_id == LMD_TYPE_TYPE
                ? def->type : wrap_type(lx, def->type);
        }
    } else if (base_type_alias_suggestion(w)) {
        // conceptual base-type spellings (int64, float32) get the canonical
        // suggestion and fail the annotation, exactly as build_base_type did
        record_unknown_base_type_span(lx->tp, lx->origin, w);
        ident->type = (Type*)&LIT_TYPE_ERROR;
    } else {
        // stay lenient like build_identifier: an unresolved name defers to ANY
        // so runtime paths (e.g. `?unknown` queries) degrade gracefully instead
        // of failing the whole compilation
        log_warn("type-pattern: unresolved type name '%.*s', using ANY", (int)w.length, w.str);
        ident->type = set_type_any(lx->tp, ANY_LEGACY_UNCLASSIFIED);
    }
    return (AstNode*)ident;
}

// Apply one occurrence suffix: `?`, `+`, `*`, `[n]`, `[n, m]`, `[n+]`, `[]`.
// No chaining — `int[3]*` does not parse; group explicitly. `T?[]` is the
// nullable-array special case (nullable binds before the array count).
AstNode* apply_occurrence(Lexer* lx, AstNode* operand) {
    if (lx->p >= lx->end) { return operand; }
    char c = *lx->p;
    if (c != '?' && c != '+' && c != '*' && c != '[') { return operand; }

    AstUnaryNode* ast_node = (AstUnaryNode*)new_node(lx, AST_NODE_UNARY_TYPE, sizeof(AstUnaryNode));
    TypeUnary* type = (TypeUnary*)alloc_type_kind(lx->tp->pool, TYPE_KIND_UNARY, sizeof(TypeUnary));
    ast_node->operand = operand;
    type->operand = operand->type;
    type->min_count = 0;
    type->max_count = -1;

    const char* op_start = lx->p;
    if (c == '?')      { lx->p++; ast_node->op = OPERATOR_OPTIONAL;  type->min_count = 0; type->max_count = 1; }
    else if (c == '+') { lx->p++; ast_node->op = OPERATOR_ONE_MORE;  type->min_count = 1; type->max_count = -1; }
    else if (c == '*') { lx->p++; ast_node->op = OPERATOR_ZERO_MORE; type->min_count = 0; type->max_count = -1; }
    else {
        int depth = 0;
        while (lx->p < lx->end) {
            if (*lx->p == '[') { depth++; }
            else if (*lx->p == ']') { depth--; if (!depth) { lx->p++; break; } }
            lx->p++;
        }
        if (depth) { fail(lx, "unterminated occurrence count"); return NULL; }
        StrView op = {op_start, (size_t)(lx->p - op_start)};
        ast_node->op = OPERATOR_REPEAT;
        parse_occurrence_count(op, &type->min_count, &type->max_count);
    }
    ast_node->op_str.str = op_start;
    ast_node->op_str.length = (size_t)(lx->p - op_start);
    type->op = ast_node->op;

    // occurrence registers the RAW type, per build_occurrence_type
    arraylist_append(lx->tp->type_list, type);
    type->type_index = lx->tp->type_list->length - 1;
    ast_node->type = wrap_type(lx, (Type*)type);

    // `int?[]` — the array count binds outside the nullable element
    if (ast_node->op == OPERATOR_OPTIONAL && lx->p < lx->end && *lx->p == '[') {
        return apply_occurrence(lx, (AstNode*)ast_node);
    }
    return (AstNode*)ast_node;
}

AstNode* parse_unary(Lexer* lx) {
    skip_space(lx);
    if (lx->p < lx->end && *lx->p == '!') {
        // prefix negation: !T is `any ! T` — mirrors build_negation_type
        lx->p++;
        AstNode* operand = parse_unary(lx);
        if (!operand) { return NULL; }
        AstPrimaryNode* any_node = (AstPrimaryNode*)new_node(lx,
            AST_NODE_PRIMARY, sizeof(AstPrimaryNode));
        any_node->type = wrap_type(lx, set_type_any(lx->tp, ANY_EXPLICIT));
        return make_binary_node(lx, (AstNode*)any_node, operand,
            OPERATOR_EXCLUDE, "!", 1);
    }

    AstNode* primary = parse_primary(lx);
    if (!primary) { return NULL; }

    // range type: `1 to 10`, `"a" to "z"` — mirrors build_range_type
    StrView w = peek_word(lx);
    if (word_is(w, "to")) {
        lx->p += w.length;
        AstNode* upper = parse_primary(lx);
        if (!upper) { return NULL; }
        AstBinaryNode* ast_node = (AstBinaryNode*)new_node(lx, AST_NODE_BINARY, sizeof(AstBinaryNode));
        ast_node->op = OPERATOR_TO;
        ast_node->op_str = {.str = "to", .length = 2};
        ast_node->left = primary;
        ast_node->right = upper;

        TypeRange* range_type = (TypeRange*)alloc_type(lx->tp->pool, LMD_TYPE_RANGE, sizeof(TypeRange));
        range_type->kind = TYPE_KIND_RANGE;
        range_type->start = ItemNull;
        range_type->end = ItemNull;
        range_type->is_char = false;
        Item start_item = ItemNull;
        Item end_item = ItemNull;
        if (ast_static_literal_item(lx->tp, ast_node->left, &start_item) &&
                ast_static_literal_item(lx->tp, ast_node->right, &end_item)) {
            range_type->start = start_item;
            range_type->end = end_item;
            range_type->is_char = get_type_id(start_item) == LMD_TYPE_STRING &&
                get_type_id(end_item) == LMD_TYPE_STRING;
        }
        ast_node->type = (Type*)range_type;
        arraylist_append(lx->tp->type_list, (Type*)range_type);
        return (AstNode*)ast_node;
    }
    skip_space(lx);
    return apply_occurrence(lx, primary);
}

AstNode* make_binary_node(Lexer* lx, AstNode* left, AstNode* right, Operator op,
        const char* op_text, size_t op_len) {
    AstBinaryNode* ast_node = (AstBinaryNode*)new_node(lx, AST_NODE_BINARY_TYPE, sizeof(AstBinaryNode));
    ast_node->op = op;
    ast_node->op_str = {.str = op_text, .length = op_len};
    ast_node->left = left;
    ast_node->right = right;
    TypeBinary* type = (TypeBinary*)alloc_type_kind(lx->tp->pool, TYPE_KIND_BINARY, sizeof(TypeBinary));
    type->op = op;
    type->left = left->type;
    type->right = right->type;
    ast_node->type = register_wrapped(lx, (Type*)type, &type->type_index);
    return (AstNode*)ast_node;
}

// Type operators, loosest to tightest: `|` union, `!` exclusion, `&`
// intersection — the order grammar-common.js gives them.
AstNode* parse_intersect(Lexer* lx) {
    AstNode* left = parse_unary(lx);
    if (!left) { return NULL; }
    while (at(lx, '&')) {
        lx->p++;
        AstNode* right = parse_unary(lx);
        if (!right) { return NULL; }
        // build_binary_type maps a type-level `&` to OPERATOR_OR, not
        // OPERATOR_INTERSECT. Odd, but it is the operator the rest of the
        // runtime sees today, so reproduce it rather than diverge (IS3).
        left = make_binary_node(lx, left, right, OPERATOR_OR, "&", 1);
    }
    return left;
}

AstNode* parse_exclude(Lexer* lx) {
    AstNode* left = parse_intersect(lx);
    if (!left) { return NULL; }
    while (at(lx, '!')) {
        lx->p++;
        AstNode* right = parse_intersect(lx);
        if (!right) { return NULL; }
        left = make_binary_node(lx, left, right, OPERATOR_EXCLUDE, "!", 1);
    }
    return left;
}

AstNode* parse_union(Lexer* lx) {
    AstNode* left = parse_exclude(lx);
    if (!left) { return NULL; }
    while (at(lx, '|')) {
        lx->p++;
        AstNode* right = parse_exclude(lx);
        if (!right) { return NULL; }
        left = make_binary_node(lx, left, right, OPERATOR_UNION, "|", 1);
    }
    return left;
}

// Declaration return types deliberately admit only named/base atoms plus one
// occurrence suffix. Keeping that boundary explicit prevents a function body
// `{...}` or a nested fn/map type from being swallowed by the scanner token.
AstNode* parse_return_pattern_atom(Lexer* lx) {
    skip_space(lx);
    StrView word = peek_word(lx);
    if (!word.length || word_is(word, "fn") || word_is(word, "true") ||
            word_is(word, "false")) {
        fail(lx, "expected a return type");
        return NULL;
    }
    AstNode* atom = parse_primary(lx);
    if (!atom) { return NULL; }
    skip_space(lx);
    return apply_occurrence(lx, atom);
}

Type* return_contract_type(AstNode* node) {
    if (!node || !node->type) { return &TYPE_ERROR; }
    if (node->type->type_id == LMD_TYPE_TYPE) {
        Type* inner = ((TypeType*)node->type)->type;
        return inner ? inner : &TYPE_ERROR;
    }
    return node->type;
}

AstNode* parse_return_type_pattern(Lexer* lx) {
    AstNode* left = parse_return_pattern_atom(lx);
    if (!left) { return NULL; }

    for (;;) {
        skip_space(lx);
        if (lx->p >= lx->end || (*lx->p != '|' && *lx->p != '&' && *lx->p != '!')) {
            break;
        }
        const char* op_text = lx->p++;
        AstNode* right = parse_return_pattern_atom(lx);
        if (!right) { return NULL; }
        Operator op = *op_text == '|' ? OPERATOR_UNION :
            (*op_text == '&' ? OPERATOR_OR : OPERATOR_EXCLUDE);
        left = (AstNode*)build_registered_binary_type_from_span(lx->tp, lx->origin,
            left, right, left->type, right->type, op, {op_text, 1});
    }
    return left;
}

AstNode* parse_view_pattern_primary(Lexer* lx) {
    skip_space(lx);
    if (lx->p < lx->end && *lx->p == '<') {
        lx->p++;
        return parse_element_type(lx);
    }
    StrView word = peek_word(lx);
    if (!word.length || word_is(word, "fn") || word_is(word, "true") ||
            word_is(word, "false")) {
        fail(lx, "expected a view pattern primary");
        return NULL;
    }
    return parse_primary(lx);
}

AstNode* parse_view_pattern(Lexer* lx) {
    AstNode* left = parse_view_pattern_primary(lx);
    if (!left) { return NULL; }
    while (at(lx, '|')) {
        const char* op_text = lx->p++;
        AstNode* right = parse_view_pattern_primary(lx);
        if (!right) { return NULL; }
        left = (AstNode*)build_registered_binary_type_from_span(lx->tp, lx->origin,
            left, right, left->type, right->type, OPERATOR_UNION,
            {op_text, 1});
    }
    return left;
}

}  // namespace

AstNode* parse_type_pattern_text_span(Transpiler* tp, const char* begin,
        const char* end, SourceSpan span) {
    Lexer lx = {tp, begin, end, span, false};
    AstNode* node = parse_union(&lx);
    if (!node || lx.failed) { return NULL; }
    skip_space(&lx);
    if (lx.p != lx.end) { fail(&lx, "trailing input"); return NULL; }
    return node;
}

AstNode* parse_primary_type_text_span(Transpiler* tp, const char* begin,
        const char* end, SourceSpan span) {
    Lexer lx = {tp, begin, end, span, false};
    AstNode* node = parse_primary(&lx);
    if (!node || lx.failed) { return NULL; }
    return node;
}

AstNode* parse_return_type_text_span(Transpiler* tp, const char* begin,
        const char* end, SourceSpan span) {
    Lexer lx = {tp, begin, end, span, false};
    AstNode* ok = parse_return_type_pattern(&lx);
    if (!ok || lx.failed) { return NULL; }

    skip_space(&lx);
    bool can_raise = eat(&lx, '^');
    Type* error_type = NULL;
    if (can_raise) {
        skip_space(&lx);
        if (lx.p < lx.end) {
            AstNode* error = parse_return_type_pattern(&lx);
            if (!error || lx.failed) { return NULL; }
            error_type = return_contract_type(error);
        } else {
            error_type = &TYPE_ERROR;
        }
    }
    skip_space(&lx);
    if (lx.p != lx.end) { fail(&lx, "trailing return contract input"); return NULL; }
    return build_function_return_contract_node_from_span(tp, span,
        return_contract_type(ok), error_type, can_raise);
}

AstNode* parse_view_pattern_text_span(Transpiler* tp, const char* begin,
        const char* end, SourceSpan span) {
    Lexer lx = {tp, begin, end, span, false};
    AstNode* pattern = parse_view_pattern(&lx);
    if (!pattern || lx.failed) { return NULL; }
    skip_space(&lx);
    if (lx.p != lx.end) { fail(&lx, "trailing view pattern input"); return NULL; }
    return pattern;
}

AstNode* parse_type_pattern_text(Transpiler* tp, const char* begin,
        const char* end, TSNode origin) {
    SourceSpan span = {ts_node_start_byte(origin), ts_node_end_byte(origin)};
    return parse_type_pattern_text_span(tp, begin, end, span);
}

AstNode* parse_primary_type_text(Transpiler* tp, const char* begin,
        const char* end, TSNode origin) {
    SourceSpan span = {ts_node_start_byte(origin), ts_node_end_byte(origin)};
    return parse_primary_type_text_span(tp, begin, end, span);
}

AstNode* parse_return_type_text(Transpiler* tp, const char* begin,
        const char* end, TSNode origin) {
    SourceSpan span = {ts_node_start_byte(origin), ts_node_end_byte(origin)};
    return parse_return_type_text_span(tp, begin, end, span);
}

AstNode* parse_view_pattern_text(Transpiler* tp, const char* begin,
        const char* end, TSNode origin) {
    SourceSpan span = {ts_node_start_byte(origin), ts_node_end_byte(origin)};
    return parse_view_pattern_text_span(tp, begin, end, span);
}
