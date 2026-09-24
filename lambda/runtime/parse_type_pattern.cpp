#include "parse_type_pattern.hpp"
#include "ast_build.hpp"
#include "type_build.hpp"
#include "transpiler.hpp"
#include "lambda-error.h"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include "../../lib/strview.h"

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
// wrapping, and type_list/const_list registration expected by downstream
// consumers — that fidelity is the contract (see the header). The shared
// Type-construction pieces live in type_build.hpp rather than being copied.
//
// The parse builds only nodes (each tagged with its LSF_TP_* form) and the
// lexical payload of literals; resolve_type_pattern() then does everything a
// name, a constant slot, a type index, or a diagnostic depends on. Those
// effects are order-sensitive, so resolution visits children before their
// parent: the order in which the productions complete.

namespace {

// syntax facts a type-pattern node carries until it resolves
enum : uint8_t {
    TP_FLAG_FN_PROC = 1u << 0,       // LSF_TP_FN: the `pn (...)` colour
    TP_FLAG_RAISES = 1u << 1,        // a `^` follows the return type
    TP_FLAG_ERROR_NODE = 1u << 2,    // the raised channel names its type
};

struct Lexer {
    Transpiler* tp;
    const char* p;
    const char* end;
    SourceSpan origin;
    bool failed;
    TypePatternFailure failure;
};

bool is_ident_start(char c) {
    return c == '_' || c == '$' || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || (unsigned char)c >= 0x80;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_ident_continue(char c) { return is_ident_start(c) || is_digit(c); }

// Keep the first diagnostic, which is the useful one. It is reported when the
// slot resolves, so a syntax error keeps its place among semantic errors.
void record_failure(Lexer* lx, LambdaErrorCode code, const char* what,
        const char* message) {
    if (lx->failed) { return; }
    lx->failed = true;
    size_t left = (size_t)(lx->end - lx->p);
    log_error("type-pattern: %s at '%.*s'", what, (int)(left > 24 ? 24 : left), lx->p);
    lx->failure.code = code;
    lx->failure.message = message;
}

void fail(Lexer* lx, const char* what) {
    record_failure(lx, ERR_INVALID_LITERAL, what, "invalid type pattern");
}

void fail_code(Lexer* lx, LambdaErrorCode code, const char* what) {
    record_failure(lx, code, what, what);
}

// Pattern text may carry `//` and `/* */` comments between its tokens.
static const char* skip_space_from(const char* p, const char* end) {
    for (;;) {
        p = strn_skip_ascii_space(p, end);
        if (p + 1 < end && p[0] == '/' && p[1] == '/') {
            while (p < end && *p != '\n') p++;
            continue;
        }
        if (p + 1 < end && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++;
            p = p + 2 < end ? p + 2 : end;
            continue;
        }
        return p;
    }
}

void skip_space(Lexer* lx) {
    if (lx->p) lx->p = skip_space_from(lx->p, lx->end);
}

bool at(Lexer* lx, char c) { skip_space(lx); return lx->p < lx->end && *lx->p == c; }

bool eat(Lexer* lx, char c) {
    if (!at(lx, c)) { return false; }
    lx->p++;
    return true;
}

// Read an identifier/keyword without consuming it on failure.
StrView peek_word(Lexer* lx) {
    if (!lx->p) return (StrView){lx->end, 0};
    skip_space(lx);
    StrView word = {lx->p, 0};
    if (lx->p >= lx->end || !is_ident_start(*lx->p)) return word;
    const char* q = lx->p;
    while (q < lx->end && is_ident_continue(*q)) q++;
    word.length = (size_t)(q - lx->p);
    return word;
}

StrView take_word(Lexer* lx) {
    StrView word = peek_word(lx);
    if (lx->p) lx->p += word.length;
    return word;
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
    size_t length = s ? strlen(s) : 0;
    return w.length == length && (!length ||
        (w.str && s && memcmp(w.str, s, length) == 0));
}

// the characters that open a type suffix (see apply_occurrence)
bool is_suffix_start(char c) {
    return c == '?' || c == '+' || c == '*' || c == '[' || c == '{';
}

AstNode* parse_union(Lexer* lx);
AstNode* parse_binder(Lexer* lx);
AstNode* parse_exclude(Lexer* lx);
AstNode* parse_intersect(Lexer* lx);
AstNode* parse_unary(Lexer* lx);
AstNode* parse_primary(Lexer* lx);
AstNode* parse_element_type(Lexer* lx);
AstNode* parse_fn_type(Lexer* lx, bool is_proc);
AstNode* parse_island(Lexer* lx);
AstNode* parse_island_body(Lexer* lx);

// Every hand node shares the type-slot source span, so nothing downstream may
// re-read source through it expecting a sub-span. The one consumer that does is the
// literal emitter for `&LIT_INT` / `&LIT_BOOL` typed nodes — which is why
// numeric literals here always carry value-bearing types instead (see below).
AstNode* new_node(Lexer* lx, AstNodeType kind, size_t size, LambdaSyntaxForm form) {
    AstNode* node = alloc_ast_node_from_span(lx->tp, kind, lx->origin, size);
    node->syntax_form = form;
    return node;
}

AstNode* make_binary_node(Lexer* lx, AstNode* left, AstNode* right, Operator op,
        const char* op_text, size_t op_len, LambdaSyntaxForm form) {
    AstBinaryNode* ast_node = (AstBinaryNode*)new_node(lx, AST_NODE_BINARY_TYPE,
        sizeof(AstBinaryNode), form);
    ast_node->op = op;
    ast_node->op_str = {.str = op_text, .length = op_len};
    ast_node->left = left;
    ast_node->right = right;
    return (AstNode*)ast_node;
}

// --- literals ---------------------------------------------------------------
// Value-bearing literal types. The payload is lexical, so it is decoded while
// parsing; the const_list slot the transpiler emits it from is claimed when
// the literal resolves, never from the node's source span.

AstNode* parse_string_literal(Lexer* lx, char quote) {
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
        sym->kind = SYMBOL_LAMBDA_NAME;
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
    AstNode* node = new_node(lx, AST_NODE_PRIMARY, sizeof(AstPrimaryNode),
        LSF_TP_LIT_STRING);
    node->type = (Type*)ts;
    return node;
}

AstNode* parse_number_literal(Lexer* lx) {
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

    Type* type;
    if (is_float) {
        // the float emitter reads double_val straight off the type
        TypeFloat* ft = (TypeFloat*)alloc_type(lx->tp->pool, LMD_TYPE_FLOAT, sizeof(TypeFloat));
        ft->double_val = strtod(buf, NULL);
        ft->is_const = 1;  ft->is_literal = 1;
        type = (Type*)ft;
    } else {
        // NOT &LIT_INT: that shared type makes the emitter re-parse the value
        // from the node's source span, and every hand node spans the whole
        // token. A pooled int is value-bearing, so the span never matters.
        TypeInt64* it = (TypeInt64*)alloc_type(lx->tp->pool, LMD_TYPE_INT, sizeof(TypeInt64));
        it->int64_val = strtoll(buf, NULL, 10);
        it->is_const = 1;  it->is_literal = 1;
        type = (Type*)it;
    }
    AstNode* node = new_node(lx, AST_NODE_PRIMARY, sizeof(AstPrimaryNode),
        LSF_TP_LIT_NUMBER);
    node->type = type;
    return node;
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

// the reserved word a word-spelled class came from; NULL for `.` and `...`
const char* island_char_class_word(PatternCharClass klass) {
    switch (klass) {
    case PATTERN_DIGIT: return "d";
    case PATTERN_WORD:  return "w";
    case PATTERN_SPACE: return "s";
    case PATTERN_ALPHA: return "a";
    default: return NULL;
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
        AstListNode* list = (AstListNode*)new_node(lx, AST_NODE_LIST_TYPE,
            sizeof(AstListNode), LSF_TP_ISLAND_GROUP);
        list->item = inner;
        return (AstNode*)list;
    }
    if (c == '.') {
        bool any_string = (lx->p + 2 < lx->end && lx->p[1] == '.' && lx->p[2] == '.');
        lx->p += any_string ? 3 : 1;
        AstPatternCharClassNode* cc = (AstPatternCharClassNode*)new_node(lx,
            AST_NODE_PATTERN_CHAR_CLASS, sizeof(AstPatternCharClassNode),
            LSF_TP_CHAR_CLASS);
        cc->char_class = any_string ? PATTERN_ANY_STRING : PATTERN_ANY;
        return (AstNode*)cc;
    }
    if (c == '"' || c == '\'') {
        AstNode* left = parse_string_literal(lx, c);
        if (!left) { return NULL; }
        // `"a" to "z"` — a character range
        StrView w = peek_word(lx);
        if (word_is(w, "to")) {
            lx->p += w.length;
            skip_space(lx);
            if (lx->p >= lx->end || (*lx->p != '"' && *lx->p != '\'')) {
                fail(lx, "expected a string literal after 'to'"); return NULL;
            }
            AstNode* upper = parse_string_literal(lx, *lx->p);
            if (!upper) { return NULL; }
            AstPatternRangeNode* range = (AstPatternRangeNode*)new_node(lx,
                AST_NODE_PATTERN_RANGE, sizeof(AstPatternRangeNode),
                LSF_TP_PATTERN_RANGE);
            range->start = left;
            range->end = upper;
            return (AstNode*)range;
        }
        return left;
    }

    StrView w = take_word(lx);
    if (!w.length) { fail(lx, "expected a pattern"); return NULL; }
    PatternCharClass klass;
    if (island_char_class(w, &klass)) {
        AstPatternCharClassNode* cc = (AstPatternCharClassNode*)new_node(lx,
            AST_NODE_PATTERN_CHAR_CLASS, sizeof(AstPatternCharClassNode),
            LSF_TP_CHAR_CLASS);
        cc->char_class = klass;
        return (AstNode*)cc;
    }
    // otherwise a reference to a named pattern; the regex compiler follows the
    // NameEntry to the definition's AST
    AstIdentNode* ident = (AstIdentNode*)new_node(lx, AST_NODE_IDENT,
        sizeof(AstIdentNode), LSF_TP_PATTERN_REF);
    ident->name = name_pool_create_strview(lx->tp->name_pool, w);
    return (AstNode*)ident;
}

AstNode* parse_island_unary(Lexer* lx) {
    skip_space(lx);
    bool negated = false;
    if (lx->p < lx->end && *lx->p == '!') { lx->p++; negated = true; }

    AstNode* operand = parse_island_primary(lx);
    if (!operand) { return NULL; }

    if (negated) {
        AstUnaryNode* un = (AstUnaryNode*)new_node(lx, AST_NODE_UNARY_TYPE,
            sizeof(AstUnaryNode), LSF_TP_ISLAND_UNARY);
        un->op = OPERATOR_NOT;
        un->op_str = {.str = "!", .length = 1};
        un->operand = operand;
        operand = (AstNode*)un;
    }

    // occurrence suffix — no space before it, so `d+ w` stays two atoms.
    // S11.1.2v2/S16.8.6v3: an island counts with the regex spelling `{n,m}`,
    // the same one the type families use; `[n]` there was retired with them.
    if (lx->p < lx->end && (*lx->p == '?' || *lx->p == '+' || *lx->p == '*' || *lx->p == '{')) {
        AstUnaryNode* un = (AstUnaryNode*)new_node(lx, AST_NODE_UNARY_TYPE,
            sizeof(AstUnaryNode), LSF_TP_ISLAND_UNARY);
        un->operand = operand;
        const char* op_start = lx->p;
        char c = *lx->p;
        if (c == '?')      { lx->p++; un->op = OPERATOR_OPTIONAL; }
        else if (c == '+') { lx->p++; un->op = OPERATOR_ONE_MORE; }
        else if (c == '*') { lx->p++; un->op = OPERATOR_ZERO_MORE; }
        else {
            int depth = 0;
            while (lx->p < lx->end) {
                if (*lx->p == '{') { depth++; }
                else if (*lx->p == '}') { depth--; if (!depth) { lx->p++; break; } }
                lx->p++;
            }
            if (depth) { fail(lx, "unterminated occurrence count"); return NULL; }
            un->op = OPERATOR_REPEAT;
        }
        // the regex compiler re-reads the occurrence spelling for REPEAT
        un->op_str.str = op_start;
        un->op_str.length = (size_t)(lx->p - op_start);
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
        AST_NODE_PATTERN_SEQ, sizeof(AstPatternSeqNode), LSF_TP_ISLAND_SEQ);
    seq->first = first;
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
        // a real union type, not a pattern placeholder: a literal-only island is
        // returned as this very AST, so its ->type becomes the annotation's type
        left = c == '|'
            ? make_binary_node(lx, left, right, OPERATOR_UNION, "|", 1, LSF_TP_BINARY)
            : make_binary_node(lx, left, right, OPERATOR_INTERSECT, "&", 1, LSF_TP_BINARY);
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
        AST_NODE_PATTERN_ISLAND, sizeof(AstPatternIslandNode), LSF_TP_ISLAND);
    node->is_symbol = is_symbol;
    node->pattern_index = -1;
    node->pattern = parse_island_body(lx);
    if (!node->pattern) { return NULL; }
    if (!eat(lx, ')')) { fail(lx, "expected ')' closing the pattern island"); return NULL; }

    // A body with a symbol literal keeps its island node so resolution can
    // report it (S11.1.2). Otherwise a literal-only island IS an ordinary
    // literal union; keeping that representation preserves the existing
    // matching path by returning the body AST. Literal types are lexical, so
    // both tests run before resolution.
    if (!pattern_ast_has_symbol_literal(node->pattern) && !is_symbol &&
            pattern_ast_literal_set(node->pattern)) {
        return node->pattern;
    }
    return (AstNode*)node;
}

// --- containers -------------------------------------------------------------

// `[T]`, `[T, U]` — a bracket type is a positional pattern (S11.1.1): a
// type-valued position is a pattern; a literal position is stored as its Item.
AstNode* parse_array_type(Lexer* lx) {
    AstArrayNode* ast_node = (AstArrayNode*)new_node(lx, AST_NODE_ARRAY_TYPE,
        sizeof(AstArrayNode), LSF_TP_ARRAY);
    AstNode* prev = NULL;
    int count = 0;
    if (!at(lx, ']')) {
        do {
            if (count >= 64) { fail(lx, "too many bracket-type positions"); return NULL; }
            AstNode* item = parse_binder(lx);
            if (!item) { return NULL; }
            if (prev) { prev->next = item; }
            else { ast_node->item = item; }
            prev = item;
            count++;
        } while (eat(lx, ','));
    }
    if (!eat(lx, ']')) { fail(lx, "expected ']'"); return NULL; }
    return (AstNode*)ast_node;
}

// S16.9.5: `a?: T` marks the FIELD optional — the whole field may be absent —
// as distinct from `a: T?`, where the field is present and its value nullable.
// Both field positions the ruling names (map-type items and element attributes)
// parse their own `name : type`, so the marker is read and applied here rather
// than duplicated at each site.
//
// The absent-field meaning is carried by wrapping the field type in
// OPERATOR_OPTIONAL, which is exactly what the validator's is_type_optional()
// already reads to decide whether a missing field is an error. That reuse is
// why `a?: T` and `a: T?` are currently indistinguishable downstream; telling
// them apart needs a field-level flag on ShapeEntry, which is recorded as the
// remaining half of the S16.9.5 gap rather than invented here.
static bool eat_optional_field_marker(Lexer* lx) {
    skip_space(lx);
    if (lx->p < lx->end && *lx->p == '?') { lx->p++; return true; }
    return false;
}

static AstNode* make_optional_field_type(Lexer* lx, AstNode* operand) {
    if (!operand) { return NULL; }
    AstUnaryNode* ast_node = (AstUnaryNode*)new_node(lx, AST_NODE_UNARY_TYPE,
        sizeof(AstUnaryNode), LSF_TP_OPTIONAL_FIELD);
    ast_node->operand = operand;
    ast_node->op = OPERATOR_OPTIONAL;
    return (AstNode*)ast_node;
}

// One `name: T` field: a KEY_EXPR like a map literal's field, so shape entries
// and downstream walks see the same node.
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
    bool field_optional = eat_optional_field_marker(lx);
    if (!eat(lx, ':')) { fail(lx, "expected ':' after a field name"); return NULL; }
    AstNode* field_type = parse_binder(lx);
    if (!field_type) { return NULL; }
    if (field_optional) {
        field_type = make_optional_field_type(lx, field_type);
        if (!field_type) { return NULL; }
    }

    AstNamedNode* named = (AstNamedNode*)new_node(lx, AST_NODE_KEY_EXPR,
        sizeof(AstNamedNode), LSF_TP_FIELD);
    named->name = name_pool_create_strview(lx->tp->name_pool, field);
    named->as = field_type;
    return named;
}

// `{a: int, b: [string]}` — field types are pattern-only (CT8v2), no `that`.
AstNode* parse_map_type(Lexer* lx) {
    AstMapNode* ast_node = (AstMapNode*)new_node(lx, AST_NODE_MAP_TYPE,
        sizeof(AstMapNode), LSF_TP_MAP);
    AstNode* prev_item = NULL;
    if (!at(lx, '}')) {
        do {
            AstNamedNode* item = parse_field(lx);
            if (!item) { return NULL; }
            if (!prev_item) { ast_node->item = (AstNode*)item; }
            else { prev_item->next = (AstNode*)item; }
            prev_item = (AstNode*)item;
        } while (eat(lx, ','));
    }
    if (!eat(lx, '}')) { fail(lx, "expected '}'"); return NULL; }
    return (AstNode*)ast_node;
}

// `(T)` groups (a single element unwraps); `(T, U)` is a tuple type.
AstNode* parse_paren_type(Lexer* lx) {
    AstNode* first = parse_binder(lx);
    if (!first) { return NULL; }
    if (eat(lx, ')')) { return first; }  // grouping — single element unwraps

    AstListNode* ast_node = (AstListNode*)new_node(lx, AST_NODE_LIST_TYPE,
        sizeof(AstListNode), LSF_TP_TUPLE);
    ast_node->item = first;
    AstNode* prev = first;
    while (eat(lx, ',')) {
        AstNode* next = parse_binder(lx);
        if (!next) { return NULL; }
        prev->next = next;
        prev = next;
    }
    if (!eat(lx, ')')) { fail(lx, "expected ')'"); return NULL; }
    return (AstNode*)ast_node;
}

// `<tag attr: T, attr2: U; content, content2>` — attribute defaults are
// literal-only inside a pattern (CT8v2).
AstNode* parse_element_type(Lexer* lx) {
    AstElementNode* ast_node = (AstElementNode*)new_node(lx, AST_NODE_ELMT_TYPE,
        sizeof(AstElementNode), LSF_TP_ELEMENT);
    StrView tag = take_qualified_tag(lx);
    if (!tag.length) { fail(lx, "expected an element tag"); return NULL; }
    // the tag is lexical; the element type carries it until resolution
    // completes the shape and registers the type
    TypeElmt* type = (TypeElmt*)alloc_type(lx->tp->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    String* pooled = name_pool_create_strview(lx->tp->name_pool, tag);
    type->name.str = pooled->chars;
    type->name.length = pooled->len;
    ast_node->type = (Type*)type;

    AstNode* prev_item = NULL;
    bool saw_content_sep = false;

    // attributes: only while the next item is `name :`
    while (!at(lx, '>') && lx->p < lx->end) {
        const char* save = lx->p;
        StrView field = take_attr_name(lx);
        bool field_optional = field.length ? eat_optional_field_marker(lx) : false;
        if (!field.length || !at(lx, ':')) { lx->p = save; break; }
        lx->p++;  // ':'
        AstNode* field_type = parse_binder(lx);
        if (!field_type) { return NULL; }
        if (field_optional) {
            field_type = make_optional_field_type(lx, field_type);
            if (!field_type) { return NULL; }
        }
        // A literal-only default (CT8v2) is not part of the type, but it still
        // resolves like any literal. It rides in `key` until then.
        AstNode* default_value = NULL;
        if (at(lx, '=')) {
            lx->p++;
            default_value = parse_primary(lx);
            if (!default_value) { return NULL; }
        }
        AstNamedNode* named = (AstNamedNode*)new_node(lx, AST_NODE_KEY_EXPR,
            sizeof(AstNamedNode), LSF_TP_FIELD);
        named->name = name_pool_create_strview(lx->tp->name_pool, field);
        named->as = field_type;
        named->key = default_value;
        if (!prev_item) { ast_node->item = (AstNode*)named; }
        else { prev_item->next = (AstNode*)named; }
        prev_item = (AstNode*)named;
        if (eat(lx, ',')) { continue; }
        if (eat(lx, ';')) { saw_content_sep = true; }
        break;
    }

    // content schema: a comma-separated pattern list held as a content node
    // whose TypeList is carried raw, not registered
    if (!at(lx, '>') && lx->p < lx->end) {
        if (!saw_content_sep) { eat(lx, ';'); }
        AstListNode* content = (AstListNode*)new_node(lx, AST_NODE_CONTENT_TYPE,
            sizeof(AstListNode), LSF_TP_CONTENT);
        AstNode* prev = NULL;
        do {
            if (at(lx, '>')) { break; }
            AstNode* item = parse_binder(lx);
            if (!item) { return NULL; }
            if (!prev) { content->item = item; }
            else { prev->next = item; }
            prev = item;
        } while (eat(lx, ','));
        ast_node->content = (AstNode*)content;
    }

    if (!eat(lx, '>')) { fail(lx, "expected '>'"); return NULL; }
    return (AstNode*)ast_node;
}

// `fn (a: int, b: string) ReturnType` — params are pattern-only (CT8v2) and the
// return may carry the raised channel (`T^`, `T^E`), the one place `^` survives
// (CT3v2/CT4). The node's param/vars stay null: the transpiler reads only
// ->type from a FUNC_TYPE node, and the TypeFunc carries the full contract.
// `pn (...)` spells the same contract with the procedure colour; `fn` and `pn`
// types are disjoint, `function` is their union (S11.1.5).
//
// Until resolution the TypeFunc holds the parameter list (each TypeParam's
// `type_expr` is its declared pattern), `body` holds the return pattern, and
// `params` the raised channel's named type.
AstNode* parse_fn_type(Lexer* lx, bool is_proc) {
    AstFuncNode* ast_node = (AstFuncNode*)new_node(lx, AST_NODE_FUNC_TYPE,
        sizeof(AstFuncNode), LSF_TP_FN);
    TypeFunc* fn_type = (TypeFunc*)alloc_type(lx->tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
    fn_type->is_proc = is_proc;
    ast_node->type = (Type*)fn_type;
    if (is_proc) ast_node->syntax_flags |= TP_FLAG_FN_PROC;

    TypeParam* prev_param = NULL;
    bool has_signature = false;
    if (eat(lx, '(')) {
        while (!at(lx, ')') && lx->p < lx->end) {
            StrView pname = take_word(lx);
            if (!pname.length) { fail(lx, "expected a parameter name"); return NULL; }
            TypeParam* param = alloc_type_param(lx->tp->pool, NULL);
            param->is_optional = eat(lx, '?');
            if (eat(lx, ':')) {
                AstNode* declared = parse_binder(lx);
                if (!declared) { return NULL; }
                param->type_expr = declared;
            }
            if (!prev_param) { fn_type->param = param; }
            else { prev_param->next = param; }
            prev_param = param;
            if (!eat(lx, ',')) { break; }
            // S16.1.2v2: `,` separates, so `fn (x: int,)` has an empty slot
            if (at(lx, ')')) {
                fail_code(lx, ERR_INVALID_LITERAL, "a trailing ',' is not a separator");
                return NULL;
            }
        }
        if (!eat(lx, ')')) { fail(lx, "expected ')'"); return NULL; }
        has_signature = true;
    }

    // S11.1.5v2: only a signature takes a return type, and it is optional --
    // `fn`, `fn ()`, `fn () int`; never `fn int`. It starts with a name on the
    // `)` line (S16.2.3v3); an `as` there is the parameter's binder.
    const char* after_close = lx->p;
    StrView return_word = has_signature ? peek_word(lx) : StrView{NULL, 0};
    if (return_word.length && !word_is(return_word, "as")) {
        if (memchr(after_close, '\n', (size_t)(lx->p - after_close))) {
            fail_code(lx, ERR_INVALID_LITERAL,
                "a function type's return type starts on the line of its ')'");
            return NULL;
        }
        AstNode* returned = parse_binder(lx);
        if (!returned) { return NULL; }
        ast_node->body = returned;
        if (eat(lx, '^')) {
            // `T^` is any error; `T^E` names one. The error arm is a simple
            // type pattern, never a whole annotation.
            ast_node->syntax_flags |= TP_FLAG_RAISES;
            skip_space(lx);
            if (lx->p < lx->end && is_ident_start(*lx->p)) {
                AstNode* err = parse_primary(lx);
                if (!err) { return NULL; }
                ast_node->params = err;
                ast_node->syntax_flags |= TP_FLAG_ERROR_NODE;
            }
        }
    }
    // A return type takes its own suffix (`fn () int?` returns `int?`), so one
    // left over would bind to the function type -- `fn ()?` against
    // `fn () int?`. The function type takes none; grouping spells it.
    const char* next = skip_space_from(lx->p, lx->end);
    if (next < lx->end && is_suffix_start(*next)) {
        fail_code(lx, ERR_INVALID_LITERAL,
            "a function type takes no suffix: group it, as in `(fn (x: int))?`");
        return NULL;
    }
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
    if (c == '"' || c == '\'') { return parse_string_literal(lx, c); }
    if (is_digit(c) || (c == '-' && lx->p + 1 < lx->end && is_digit(lx->p[1]))) {
        return parse_number_literal(lx);
    }

    StrView w = peek_word(lx);
    if (!w.length) { fail(lx, "expected a type"); return NULL; }
    lx->p += w.length;

    if (word_is(w, "fn")) { return parse_fn_type(lx, false); }
    if (word_is(w, "pn")) { return parse_fn_type(lx, true); }
    if (word_is(w, "true") || word_is(w, "false")) {
        // &LIT_BOOL makes the emitter re-read source through the node's span,
        // which is the whole token. A lone `case true:` token reads correctly;
        // a bool inside a larger pattern would not — no corpus use exists, and
        // the parse is still exact, only the emitted literal VALUE can drift.
        AstNode* node = new_node(lx, AST_NODE_PRIMARY, sizeof(AstPrimaryNode),
            LSF_TP_LIT_BOOL);
        node->type = (Type*)&LIT_BOOL;
        return node;
    }
    int base_index = lambda_base_type_index(w);
    if (base_index >= 0) {
        AstTypeNode* node = (AstTypeNode*)new_node(lx, AST_NODE_TYPE,
            sizeof(AstTypeNode), LSF_TP_BASE);
        node->syntax_aux = (uint16_t)base_index;
        return (AstNode*)node;
    }
    // a name in type position is a reference to a declared type, an IDENT
    // like a resolved value-position name so the transpiler's alias handling
    // works; a binder reference becomes an AST_NODE_TYPE when it resolves
    AstIdentNode* ident = (AstIdentNode*)new_node(lx, AST_NODE_IDENT,
        sizeof(AstIdentNode), LSF_TP_NAME);
    ident->name = name_pool_create_strview(lx->tp->name_pool, w);
    return (AstNode*)ident;
}

// `{n,}` with nothing after the comma: regex's open count, which S16.8.6v3
// spells `{n+}`. A comma followed by digits is the ordinary `{n,m}`.
static bool occurrence_count_is_open_comma(StrView op) {
    if (op.length < 3 || op.str[0] != '{') return false;
    const char* p = op.str + op.length - 1;          // the closing brace
    while (p > op.str && (p[-1] == ' ' || p[-1] == '\t')) p--;
    return p > op.str && p[-1] == ',';
}

// Apply one suffix: `?`, `+`, `*`, `{n}` / `{n,m}` / `{n+}`, `[n]`, or `[]`.
// S11.1.6v2 splits the two families by bracket: braces count a *run* (the
// occurrence family), brackets build an *array*. Counted occurrences never
// chain (`int{3}*` still needs grouping), but the array constructor does, and
// rank composes left to right (S11.1.1v3): `T[][]` is an array of arrays and
// `int[2][3]` is three arrays of two. `T?[]` keeps its existing
// nullable-element binding (D3.1.1v2), while `T[]?` is the nullable array.
AstNode* apply_occurrence(Lexer* lx, AstNode* operand) {
    if (lx->p >= lx->end) { return operand; }
    char c = *lx->p;
    if (!is_suffix_start(c)) { return operand; }

    AstUnaryNode* ast_node = (AstUnaryNode*)new_node(lx, AST_NODE_UNARY_TYPE,
        sizeof(AstUnaryNode), LSF_TP_OCCURRENCE);
    ast_node->operand = operand;

    const char* op_start = lx->p;
    if (c == '?')      { lx->p++; ast_node->op = OPERATOR_OPTIONAL; }
    else if (c == '+') { lx->p++; ast_node->op = OPERATOR_ONE_MORE; }
    else if (c == '*') { lx->p++; ast_node->op = OPERATOR_ZERO_MORE; }
    else if (c == '{') {
        // S16.8.6v3: the counted repetition is `{n}`, `{n,m}` and `{n+}`. A
        // line-start `{` never arrives here -- the statement parser ends the
        // type at the line break (S16.2.3), so it is a fresh statement.
        int depth = 0;
        while (lx->p < lx->end) {
            if (*lx->p == '{') { depth++; }
            else if (*lx->p == '}') { depth--; if (!depth) { lx->p++; break; } }
            lx->p++;
        }
        if (depth) { fail(lx, "unterminated occurrence count"); return NULL; }
        StrView op = {op_start, (size_t)(lx->p - op_start)};
        // The open bound is `{n+}`, not regex's trailing comma: a habit that
        // writes `{n,}` gets told, rather than parsing as an exact count.
        if (occurrence_count_is_open_comma(op)) {
            fail_code(lx, ERR_INVALID_LITERAL,
                "`T{n,}` is not the open count: write `T{n+}` for a run of n or more");
            return NULL;
        }
        ast_node->op = OPERATOR_REPEAT;
    }
    else {
        int depth = 0;
        while (lx->p < lx->end) {
            if (*lx->p == '[') { depth++; }
            else if (*lx->p == ']') { depth--; if (!depth) { lx->p++; break; } }
            lx->p++;
        }
        if (depth) { fail(lx, "unterminated array count"); return NULL; }
        StrView op = {op_start, (size_t)(lx->p - op_start)};
        // S11.1.6v2: brackets are the array family. `T[]` is any length and
        // `T[n]` is that length fixed; a count on a *run* is the occurrence
        // family, so `T[n+]` and `T[n, m]` are retired and name their
        // replacement instead of silently changing meaning.
        ast_node->op = OPERATOR_ARRAY;
        if (op.length > 2 &&
                (memchr(op.str, '+', op.length) || memchr(op.str, ',', op.length))) {
            // fail_code carries the teaching text into the diagnostic;
            // plain fail() would report only "invalid type pattern"
            fail_code(lx, ERR_INVALID_LITERAL,
                "`T[n+]` and `T[n, m]` are retired: write `T{n+}` or "
                "`T{n,m}` for a run of T, or `[T{n,m}]` for an array of one");
            return NULL;
        }
    }
    ast_node->op_str.str = op_start;
    ast_node->op_str.length = (size_t)(lx->p - op_start);

    // The chains above. Any bracket suffix may follow an array suffix -- a
    // retired `T[n+]` or `T[n, m]` still fails in the recursive call, and a
    // count no longer stops the chain now that `T[n]` is an array, not an
    // occurrence. A `?` after one binds outside the whole array: `int[]?` is
    // `int[] | null`, as `type V = int[]; V?` spells it. In `int?[]` the
    // array binds outside the nullable element.
    if (lx->p < lx->end && ((ast_node->op == OPERATOR_ARRAY &&
            (*lx->p == '[' || *lx->p == '?')) ||
            (ast_node->op == OPERATOR_OPTIONAL && *lx->p == '['))) {
        return apply_occurrence(lx, (AstNode*)ast_node);
    }
    return (AstNode*)ast_node;
}

AstNode* parse_unary(Lexer* lx) {
    skip_space(lx);
    if (lx->p < lx->end && *lx->p == '!') {
        // prefix negation: !T is `any ! T`
        lx->p++;
        AstNode* operand = parse_unary(lx);
        if (!operand) { return NULL; }
        AstNode* any_node = new_node(lx, AST_NODE_PRIMARY, sizeof(AstPrimaryNode),
            LSF_TP_ANY);
        return make_binary_node(lx, any_node, operand, OPERATOR_EXCLUDE, "!", 1,
            LSF_TP_BINARY);
    }

    AstNode* primary = parse_primary(lx);
    if (!primary) { return NULL; }

    // range type: `1 to 10`, `"a" to "z"`
    StrView w = peek_word(lx);
    if (word_is(w, "to")) {
        lx->p += w.length;
        AstNode* upper = parse_primary(lx);
        if (!upper) { return NULL; }
        AstBinaryNode* ast_node = (AstBinaryNode*)new_node(lx, AST_NODE_BINARY,
            sizeof(AstBinaryNode), LSF_TP_RANGE);
        ast_node->op = OPERATOR_TO;
        ast_node->op_str = {.str = "to", .length = 2};
        ast_node->left = primary;
        ast_node->right = upper;
        return (AstNode*)ast_node;
    }
    skip_space(lx);
    return apply_occurrence(lx, primary);
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
        // A type-level `&` is OPERATOR_INTERSECT, matching expression space
        // (build_ast.cpp) and the island body above. It used to be lowered to
        // OPERATOR_OR here, which is why an `int & string` annotation was
        // rejected while `x is (int & string)` worked (LR02-9): the boundary
        // checker keys on the set operators, and OPERATOR_OR is not one.
        // Consumers that still recognise the old spelling accept both.
        left = make_binary_node(lx, left, right, OPERATOR_INTERSECT, "&", 1,
            LSF_TP_BINARY);
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
        left = make_binary_node(lx, left, right, OPERATOR_EXCLUDE, "!", 1,
            LSF_TP_BINARY);
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
        left = make_binary_node(lx, left, right, OPERATOR_UNION, "|", 1,
            LSF_TP_BINARY);
    }
    return left;
}

// `as T` is the loosest type suffix: parse the complete union first, then
// register its name before the next parameter annotation is reduced.  The
// parser deliberately accepts the suffix only once; anything after T is
// trailing input instead of silently acquiring a different precedence.
AstNode* parse_binder(Lexer* lx) {
    AstNode* base = parse_union(lx);
    if (!base) return NULL;
    skip_space(lx);
    StrView keyword = peek_word(lx);
    if (!word_is(keyword, "as")) return base;
    lx->p += keyword.length;
    StrView name = take_word(lx);
    if (!name.length) {
        fail(lx, "expected a binder name after 'as'");
        return NULL;
    }
    return build_binder_type_syntax(lx->tp, lx->origin, base, name);
}

// Declaration return types deliberately admit only named/base atoms plus one
// occurrence suffix. Keeping that boundary explicit prevents a function body
// `{...}` or a nested fn/map type from being swallowed by the scanner token.
AstNode* parse_return_pattern_atom(Lexer* lx) {
    skip_space(lx);
    StrView word = peek_word(lx);
    if (!word.length || word_is(word, "fn") || word_is(word, "pn") ||
            word_is(word, "true") ||
            word_is(word, "false")) {
        fail(lx, "expected a return type");
        return NULL;
    }
    AstNode* atom = parse_primary(lx);
    if (!atom) { return NULL; }
    skip_space(lx);
    return apply_occurrence(lx, atom);
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
            (*op_text == '&' ? OPERATOR_INTERSECT : OPERATOR_EXCLUDE);
        left = make_binary_node(lx, left, right, op, op_text, 1,
            LSF_TP_REGISTERED_BINARY);
    }
    skip_space(lx);
    if (word_is(peek_word(lx), "as")) {
        fail_code(lx, ERR_BINDER_IN_RETURN, "a binder is not allowed in a return type");
        return NULL;
    }
    return left;
}

// ---- resolution --------------------------------------------------------------

Type* wrap_type(Transpiler* tp, Type* inner) {
    TypeType* tt = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
    tt->type = inner;
    return (Type*)tt;
}

// Containers and binaries register the WRAPPER in type_list; occurrence and
// range register the raw type — the split required by downstream consumers.
Type* register_wrapped(Transpiler* tp, Type* inner, int* index_out) {
    Type* wrapper = wrap_type(tp, inner);
    arraylist_append(tp->type_list, wrapper);
    *index_out = tp->type_list->length - 1;
    return wrapper;
}

// the transpiler emits literal payloads as const-pool loads
void resolve_literal_constant(Transpiler* tp, AstNode* node) {
    Type* type = node->type;
    if (type->type_id == LMD_TYPE_STRING || type->type_id == LMD_TYPE_SYMBOL) {
        TypeString* ts = (TypeString*)type;
        arraylist_append(tp->const_list, ts->string);
        ts->const_index = tp->const_list->length - 1;
    } else if (type->type_id == LMD_TYPE_FLOAT) {
        TypeFloat* ft = (TypeFloat*)type;
        arraylist_append(tp->const_list, &ft->double_val);
        ft->const_index = tp->const_list->length - 1;
    } else {
        TypeInt64* it = (TypeInt64*)type;
        arraylist_append(tp->const_list, &it->int64_val);
        it->const_index = tp->const_list->length - 1;
    }
}

void resolve_type_name(Transpiler* tp, AstIdentNode* ident) {
    StrView w = {ident->name->chars, ident->name->len};
    NameEntry* entry = lookup_name(tp, w);
    if (entry && entry->is_binder && entry->binder) {
        TypeBoundRef* ref = (TypeBoundRef*)alloc_type_kind(tp->pool,
            TYPE_KIND_BOUND_REF, sizeof(TypeBoundRef));
        ref->slot = entry->binder_slot;
        ref->bound = entry->binder->bound;
        // a binder reference is a type node, not a name: morph in place so
        // the parent keeps its child pointer
        static_assert(sizeof(AstTypeNode) <= sizeof(AstIdentNode),
            "a type-name node must be able to hold its bound-reference form");
        memset((char*)ident + sizeof(AstNode), 0,
            sizeof(AstIdentNode) - sizeof(AstNode));
        ident->node_type = AST_NODE_TYPE;
        ident->type = (Type*)ref;
        return;
    }
    ident->entry = entry;
    if (ident->entry && ident->entry->node && ident->entry->node->type) {
        AstNode* def = ident->entry->node;
        ident->type = def->type;
        // Type and pattern definitions are referenced through a plain TypeType
        // wrapper, exactly as resolve_identifier does. The wrap is load-bearing:
        // match_arm_is_error_handler blind-casts an arm's type as (TypeType*),
        // so a raw TypePattern here reads pattern_index as a pointer — SEGV.
        if (def->node_type == AST_NODE_TYPE_STAM ||
                (def->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                 ((AstDeclaratorNode*)def)->is_type_definition) ||
                def->node_type == AST_NODE_STRING_PATTERN ||
                def->node_type == AST_NODE_SYMBOL_PATTERN) {
            // direct aliases for literals/ranges already carry the first-class
            // TypeType wrapper; wrapping that carrier again hides the payload
            // from the matcher and makes `is Alias` fail (D2.2.2).
            ident->type = def->type && def->type->type_id == LMD_TYPE_TYPE
                ? def->type : wrap_type(tp, def->type);
        }
    } else if (base_type_alias_suggestion(w)) {
        // conceptual base-type spellings (int64, float32) get the canonical
        // suggestion and fail the annotation, as an unknown base-type word does
        record_unknown_base_type_span(tp, ident->source_span, w);
        ident->type = (Type*)&LIT_TYPE_ERROR;
    } else {
        // stay lenient like resolve_identifier: an unresolved name defers to ANY
        // so runtime paths (e.g. `?unknown` queries) degrade gracefully instead
        // of failing the whole compilation
        log_warn("type-pattern: unresolved type name '%.*s', using ANY", (int)w.length, w.str);
        ident->type = set_type_any(tp, ANY_LEGACY_UNCLASSIFIED);
    }
}

void resolve_array_type(Transpiler* tp, AstArrayNode* ast_node) {
    TypeArray* type = (TypeArray*)alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
    int count = 0;
    for (AstNode* item = ast_node->item; item; item = item->next) {
        resolve_type_pattern(tp, item);
        count++;
    }
    if (count > 0) {
        type->item_patterns = (Item*)pool_calloc(tp->pool, sizeof(Item) * (size_t)count);
        type->item_is_type_pattern = (uint8_t*)pool_calloc(tp->pool, sizeof(uint8_t) * (size_t)count);
        Type* nested = ast_node->item->type;
        int i = 0;
        for (AstNode* item = ast_node->item; item; item = item->next, i++) {
            if (item->type && item->type->type_id == LMD_TYPE_TYPE) {
                type->item_patterns[i].type = item->type;
                type->item_is_type_pattern[i] = 1;
            } else {
                Item literal = ItemNull;
                if (ast_static_literal_item(tp, item, &literal)) {
                    type->item_patterns[i] = literal;
                }
            }
            if (nested && item->type && item->type->type_id != nested->type_id) { nested = NULL; }
        }
        type->length = count;
        type->nested = nested;
        if (count == 1 && type->item_is_type_pattern[0]) {
            log_warn("lambda_array_pattern_hint: bare [T] is an exact one-item pattern; use T[] for homogeneous arrays");
        }
    }
    ast_node->type = register_wrapped(tp, (Type*)type, &type->type_index);
}

// Map fields and element attributes share one layout: shape entries in
// source order, each strided by its field's storage class.
void resolve_field_shape(Transpiler* tp, AstNode* first_field,
        ShapeEntry** shape, int64_t* length, int64_t* byte_size) {
    ShapeEntry* prev_entry = NULL;
    int byte_offset = 0;
    for (AstNode* item = first_field; item; item = item->next) {
        resolve_type_pattern(tp, item);
        AstNamedNode* named = (AstNamedNode*)item;
        append_shape_entry_typed(tp, named->name, named->type, shape, &prev_entry, byte_offset);
        (*length)++;
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
        byte_offset += lambda_lane_storage_size(named->type);
    }
    *byte_size = byte_offset;
}

void resolve_fn_type(Transpiler* tp, AstFuncNode* ast_node) {
    TypeFunc* fn_type = (TypeFunc*)ast_node->type;
    set_fn_return_contract(fn_type, &TYPE_ANY_NO_ERROR, false);
    int param_count = 0;
    int required_count = 0;
    for (TypeParam* param = fn_type->param; param; param = param->next) {
        AstNode* declared = param->type_expr;
        if (declared) {
            resolve_type_pattern(tp, declared);
            apply_declared_param_type(tp, param, declared->type);
            param->type_expr = NULL;
        }
        if (!param->is_optional) { required_count++; }
        param_count++;
    }
    fn_type->param_count = param_count;
    fn_type->required_param_count = required_count;

    AstNode* returned = ast_node->body;
    if (returned) {
        resolve_type_pattern(tp, returned);
        // The contract is the type a returned value has, not the type value
        // that spells it -- as a declaration's return is (build_ast.cpp's
        // direct_function_contract). Left wrapped, a call through the signature
        // types as a type value, and calling that result reads as a conversion
        // to the wrapped type: `mk(2)(3)` typed `fn (y: int) int`, so a map
        // literal laid out an int as a function pointer.
        Type* contract = unwrap_simple_type_type(returned->type);
        set_fn_return_contract(fn_type, contract, true);
        fn_type->returned = contract;
        if (ast_node->syntax_flags & TP_FLAG_RAISES) {
            if (ast_node->syntax_flags & TP_FLAG_ERROR_NODE) {
                resolve_type_pattern(tp, ast_node->params);
                fn_type->error_type = ast_node->params->type;
            } else {
                fn_type->error_type = (Type*)&LIT_TYPE_ERROR;
            }
        }
    }
    ast_node->body = NULL;
    ast_node->params = NULL;
    ast_node->type = register_wrapped(tp, (Type*)fn_type, &fn_type->type_index);
}

void resolve_occurrence(Transpiler* tp, AstUnaryNode* ast_node) {
    resolve_type_pattern(tp, ast_node->operand);
    TypeUnary* type = (TypeUnary*)alloc_type_kind(tp->pool, TYPE_KIND_UNARY, sizeof(TypeUnary));
    type->operand = ast_node->operand->type;
    type->min_count = 0;
    type->max_count = -1;
    switch (ast_node->op) {
    case OPERATOR_OPTIONAL:  type->min_count = 0; type->max_count = 1; break;
    case OPERATOR_ONE_MORE:  type->min_count = 1; type->max_count = -1; break;
    case OPERATOR_ZERO_MORE: type->min_count = 0; type->max_count = -1; break;
    case OPERATOR_REPEAT:
        parse_occurrence_count(ast_node->op_str, &type->min_count, &type->max_count);
        break;
    default:  // OPERATOR_ARRAY: `T[]` stays open, `T[n]` fixes the length
        if (ast_node->op_str.length > 2) {
            parse_occurrence_count(ast_node->op_str, &type->min_count, &type->max_count);
        }
        break;
    }
    type->op = ast_node->op;
    // occurrence registers the RAW type, not its TypeType wrapper
    arraylist_append(tp->type_list, type);
    type->type_index = tp->type_list->length - 1;
    ast_node->type = wrap_type(tp, (Type*)type);
}

}  // namespace

void resolve_type_pattern(Transpiler* tp, AstNode* node) {
    if (!node) return;
    uint8_t form = node->syntax_form;
    if (!lambda_syntax_form_is_type_pattern(form) && form != LSF_BINDER) return;
    node->syntax_form = LSF_NONE;
    switch (form) {
    case LSF_BINDER:
        resolve_type_pattern(tp, ((AstNamedNode*)node)->as);
        resolve_binder_type(tp, node);
        break;
    case LSF_TP_LIT_STRING:
    case LSF_TP_LIT_NUMBER:
        resolve_literal_constant(tp, node);
        break;
    case LSF_TP_LIT_BOOL:
        break;
    case LSF_TP_ANY:
        node->type = wrap_type(tp, set_type_any(tp, ANY_EXPLICIT));
        break;
    case LSF_TP_BASE:
        node->type = lambda_base_type_from_index(tp, node->syntax_aux);
        break;
    case LSF_TP_NAME:
        resolve_type_name(tp, (AstIdentNode*)node);
        break;
    case LSF_TP_CHAR_CLASS: {
        AstPatternCharClassNode* cc = (AstPatternCharClassNode*)node;
        const char* word = island_char_class_word(cc->char_class);
        // the reserved atoms shadow nothing: a surrounding `let d = ...` makes
        // `d` inside an island ambiguous, which is also invalid
        if (word && lookup_name(tp, (StrView){word, 1})) {
            record_semantic_error_span(tp, node->source_span, ERR_SEMANTIC_ERROR,
                "pattern class '%.*s' is reserved inside pattern islands; rename the surrounding binding",
                1, word);
        }
        cc->type = alloc_type_kind(tp->pool, TYPE_KIND_PATTERN, sizeof(TypePattern));
        break;
    }
    case LSF_TP_PATTERN_REF: {
        AstIdentNode* ident = (AstIdentNode*)node;
        ident->entry = lookup_name(tp, (StrView){ident->name->chars, ident->name->len});
        if (ident->entry && ident->entry->node) { ident->type = ident->entry->node->type; }
        break;
    }
    case LSF_TP_PATTERN_RANGE: {
        AstPatternRangeNode* range = (AstPatternRangeNode*)node;
        resolve_type_pattern(tp, range->start);
        resolve_type_pattern(tp, range->end);
        range->type = alloc_type_kind(tp->pool, TYPE_KIND_PATTERN, sizeof(TypePattern));
        break;
    }
    case LSF_TP_ISLAND_GROUP:
        resolve_type_pattern(tp, ((AstListNode*)node)->item);
        break;
    case LSF_TP_ISLAND_UNARY:
        resolve_type_pattern(tp, ((AstUnaryNode*)node)->operand);
        node->type = alloc_type_kind(tp->pool, TYPE_KIND_PATTERN, sizeof(TypePattern));
        break;
    case LSF_TP_ISLAND_SEQ:
        for (AstNode* child = ((AstPatternSeqNode*)node)->first; child; child = child->next) {
            resolve_type_pattern(tp, child);
        }
        node->type = alloc_type_kind(tp->pool, TYPE_KIND_PATTERN, sizeof(TypePattern));
        break;
    case LSF_TP_ISLAND: {
        AstPatternIslandNode* island = (AstPatternIslandNode*)node;
        resolve_type_pattern(tp, island->pattern);
        // Pattern bodies are content-only: the domain is the island's tag, so a
        // symbol literal inside one is a mistake (S11.1.2).
        if (pattern_ast_has_symbol_literal(island->pattern)) {
            record_semantic_error_span(tp, node->source_span, ERR_INVALID_LITERAL,
                "pattern bodies are content-only; use \\symbol(...) for the symbol domain and string literals for content");
            island->type = &TYPE_ERROR;
            break;
        }
        TypePattern* pattern_type = (TypePattern*)alloc_type_kind(tp->pool,
            TYPE_KIND_PATTERN, sizeof(TypePattern));
        pattern_type->pattern_index = -1;
        pattern_type->is_symbol = island->is_symbol;
        pattern_type->re2 = nullptr;
        pattern_type->re2_unanchored = nullptr;
        pattern_type->source = nullptr;
        pattern_type->regex_source = nullptr;
        island->type = (Type*)pattern_type;
        break;
    }
    case LSF_TP_BINARY: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        resolve_type_pattern(tp, binary->left);
        resolve_type_pattern(tp, binary->right);
        TypeBinary* type = (TypeBinary*)alloc_type_kind(tp->pool, TYPE_KIND_BINARY, sizeof(TypeBinary));
        type->op = binary->op;
        type->left = binary->left->type;
        type->right = binary->right->type;
        binary->type = register_wrapped(tp, (Type*)type, &type->type_index);
        break;
    }
    case LSF_TP_REGISTERED_BINARY: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        resolve_type_pattern(tp, binary->left);
        resolve_type_pattern(tp, binary->right);
        register_binary_type(tp, binary);
        break;
    }
    case LSF_TP_RANGE: {
        AstBinaryNode* ast_node = (AstBinaryNode*)node;
        resolve_type_pattern(tp, ast_node->left);
        resolve_type_pattern(tp, ast_node->right);
        TypeRange* range_type = (TypeRange*)alloc_type(tp->pool, LMD_TYPE_RANGE, sizeof(TypeRange));
        range_type->kind = TYPE_KIND_RANGE;
        range_type->start = ItemNull;
        range_type->end = ItemNull;
        range_type->is_char = false;
        Item start_item = ItemNull;
        Item end_item = ItemNull;
        if (ast_static_literal_item(tp, ast_node->left, &start_item) &&
                ast_static_literal_item(tp, ast_node->right, &end_item)) {
            range_type->start = start_item;
            range_type->end = end_item;
            range_type->is_char = get_type_id(start_item) == LMD_TYPE_STRING &&
                get_type_id(end_item) == LMD_TYPE_STRING;
        }
        ast_node->type = (Type*)range_type;
        arraylist_append(tp->type_list, (Type*)range_type);
        break;
    }
    case LSF_TP_OCCURRENCE:
        resolve_occurrence(tp, (AstUnaryNode*)node);
        break;
    case LSF_TP_OPTIONAL_FIELD: {
        AstUnaryNode* ast_node = (AstUnaryNode*)node;
        resolve_type_pattern(tp, ast_node->operand);
        TypeUnary* type = (TypeUnary*)alloc_type_kind(tp->pool, TYPE_KIND_UNARY, sizeof(TypeUnary));
        type->operand = ast_node->operand->type;
        type->min_count = 0;
        type->max_count = 1;
        // `type->op` is the field consumers read — validator is_type_optional()
        // checks the TypeUnary, not the AST node. apply_occurrence sets both; a
        // wrapper that sets only the AST side parses but is invisible downstream.
        type->op = OPERATOR_OPTIONAL;
        ast_node->type = wrap_type(tp, (Type*)type);
        break;
    }
    case LSF_TP_ARRAY:
        resolve_array_type(tp, (AstArrayNode*)node);
        break;
    case LSF_TP_FIELD: {
        AstNamedNode* named = (AstNamedNode*)node;
        resolve_type_pattern(tp, named->as);
        named->type = named->as->type;
        if (named->key) {
            // an attribute default resolves after its field type, and then
            // leaves the node: it is not part of the type
            resolve_type_pattern(tp, named->key);
            named->key = NULL;
        }
        break;
    }
    case LSF_TP_MAP: {
        AstMapNode* ast_node = (AstMapNode*)node;
        TypeMap* type = (TypeMap*)alloc_type(tp->pool, LMD_TYPE_MAP, sizeof(TypeMap));
        resolve_field_shape(tp, ast_node->item, &type->shape, &type->length,
            &type->byte_size);
        ast_node->type = register_wrapped(tp, (Type*)type, &type->type_index);
        break;
    }
    case LSF_TP_TUPLE: {
        AstListNode* ast_node = (AstListNode*)node;
        TypeType* node_type = (TypeType*)alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeType));
        TypeList* type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeList));
        node_type->type = (Type*)type;
        ast_node->type = (Type*)node_type;
        ast_node->list_type = type;
        for (AstNode* item = ast_node->item; item; item = item->next) {
            resolve_type_pattern(tp, item);
            type->length++;
        }
        arraylist_append(tp->type_list, ast_node->type);
        type->type_index = tp->type_list->length - 1;
        break;
    }
    case LSF_TP_CONTENT: {
        AstListNode* content = (AstListNode*)node;
        TypeList* content_type = (TypeList*)alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeList));
        content->type = (Type*)content_type;
        content->list_type = content_type;
        for (AstNode* item = content->item; item; item = item->next) {
            resolve_type_pattern(tp, item);
            content_type->length++;
        }
        break;
    }
    case LSF_TP_ELEMENT: {
        AstElementNode* ast_node = (AstElementNode*)node;
        TypeElmt* type = (TypeElmt*)ast_node->type;
        resolve_field_shape(tp, ast_node->item, &type->shape, &type->length,
            &type->byte_size);
        if (ast_node->content) {
            resolve_type_pattern(tp, ast_node->content);
            type->content_length = ((AstListNode*)ast_node->content)->list_type->length;
        }
        ast_node->type = register_wrapped(tp, (Type*)type, &type->type_index);
        break;
    }
    case LSF_TP_FN:
        resolve_fn_type(tp, (AstFuncNode*)node);
        break;
    default:
        break;
    }
}

AstNode* parse_type_pattern_syntax(Transpiler* tp, const char* begin,
        const char* end, SourceSpan span, TypePatternMode mode,
        TypePatternFailure* failure) {
    Lexer lx = {tp, begin, end, span, false, {ERR_INVALID_LITERAL, NULL}};
    AstNode* node = NULL;
    const char* trailing = NULL;
    switch (mode) {
    case TYPE_PATTERN_FULL:
        node = parse_binder(&lx);
        trailing = "trailing input";
        break;
    case TYPE_PATTERN_RETURN_VALUE:
        node = parse_return_type_pattern(&lx);
        trailing = "trailing return contract input";
        break;
    }
    if (node && !lx.failed && trailing) {
        skip_space(&lx);
        if (lx.p != lx.end) fail(&lx, trailing);
    }
    if (!node || lx.failed) {
        if (failure) *failure = lx.failure;
        return NULL;
    }
    return node;
}
