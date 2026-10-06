/**
 * @file re2_wrapper.cpp
 * @brief RE2 regex wrapper implementation for Lambda string pattern matching
 * @author Henry Luo
 * @license MIT
 */

#include "re2_wrapper.hpp"
#include "ast.hpp"
#ifndef SIMPLE_SCHEMA_PARSER
#include "../lambda.hpp"
#include "heap_api.h"
#include "../input/input.hpp"
#endif
#include "../../lib/re2_glue.hpp"
#include "../../lib/log.h"
#include "../../lib/escape.h"
#include "../../lib/str.h"
#include "../../lib/string.h"
#include "../../lib/mempool.h"
#include "../../lib/mem_grow.h"
#include "../../lib/sort.h"

#include <re2/re2.h>
#include <stdint.h>
#include <string>

// runtime functions needed for pattern_find_all, pattern_split
// Only available in main executable, not in shared library (lambda-input-full-cpp)
#ifndef SIMPLE_SCHEMA_PARSER
extern "C" {
    List* list();
    void array_push(Array *array, Item item);
    void array_push_verbatim(Array *array, Item item);
    void* heap_calloc(size_t size, TypeId type_id);
    void* heap_data_calloc(size_t size);
    String* heap_strcpy(const char* src, int64_t len);
}
extern __thread EvalContext* context;
#endif

// S16.8.6v3: an island's counted occurrence is `{n}`, `{n,m}` or `{n+}`. The
// first two are already the regex spelling; the open form is not, so its `+`
// becomes RE2's trailing comma. The retired `[n]`, `[n, m]` and `[n+]` no
// longer parse.
static bool convert_occurrence_to_regex(StrBuf* regex, StrView* op_str) {
    if (!op_str || !op_str->str || op_str->length < 3 ||
            op_str->str[0] != '{' || op_str->str[op_str->length - 1] != '}') {
        return false;
    }
    for (size_t i = 0; i < op_str->length; i++) {
        char c = op_str->str[i];
        if (c == ' ' || c == '\t') continue;
        strbuf_append_char(regex, c == '+' ? ',' : c);
    }
    return true;
}

// Escape regex metacharacters in a literal string
static void escape_regex_chars(StrBuf* regex, const char* chars, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = chars[i];
        // RE2 metacharacters that need escaping
        switch (c) {
        case '\\': case '.': case '+': case '*': case '?':
        case '(': case ')': case '[': case ']': case '{': case '}':
        case '|': case '^': case '$':
            strbuf_append_char(regex, '\\');
            break;
        default:
            break;
        }
        strbuf_append_char(regex, c);
    }
}

void escape_regex_literal(StrBuf* regex, String* str) {
    if (str) escape_regex_chars(regex, str->chars, str->len);
}

// A named literal type reaches a pattern as a type value. The regex and
// surface renderings of a literal union live with compile_literal_type_pattern.
typedef enum {
    LITERAL_TYPE_REGEX,
    LITERAL_TYPE_SURFACE
} LiteralTypeRender;

static bool append_literal_type(StrBuf* output, Type* type, LiteralTypeRender render);

static Type* literal_type_unwrap(Type* type) {
    if (type && type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_SIMPLE) {
        type = ((TypeType*)type)->type;
    }
    return type;
}

// --- single-character sets (S11.1.2v3) --------------------------------------
//
// Island `!` complements a set of single characters: the one negation a regex
// engine compiles, as `[^…]`. A set is kept as code-point intervals so that
// union and nested negation compose, and is emitted as one RE2 class. The
// named classes are defined here once, for plain and negated uses alike.

typedef struct PatternCharRange {
    uint32_t lo;
    uint32_t hi;
} PatternCharRange;

typedef struct PatternCharSet {
    PatternCharRange* ranges;
    size_t count;
    size_t capacity;
} PatternCharSet;

static const uint32_t PATTERN_MAX_CODEPOINT = 0x10FFFF;
// named patterns may chain; a self-reference must not recurse forever
static const int PATTERN_MAX_DEPTH = 256;

static void char_set_free(PatternCharSet* set) {
    if (set->ranges) mem_free(set->ranges);
    set->ranges = NULL;
    set->count = 0;
    set->capacity = 0;
}

static bool char_set_add(PatternCharSet* set, uint32_t lo, uint32_t hi) {
    if (lo > hi) return true;  // an inverted range is empty (S11.1.3)
    if (!mem_grow_array_raw((void**)&set->ranges, sizeof(PatternCharRange),
            &set->capacity, set->count + 1, 8, MEM_CAT_PARSER)) {
        return false;
    }
    set->ranges[set->count].lo = lo;
    set->ranges[set->count].hi = hi;
    set->count++;
    return true;
}

static int char_range_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    uint32_t left = ((const PatternCharRange*)a)->lo;
    uint32_t right = ((const PatternCharRange*)b)->lo;
    return left < right ? -1 : (left > right ? 1 : 0);
}

// sort by lower bound, then merge intervals that overlap or touch
static void char_set_normalize(PatternCharSet* set) {
    insertion_sort(set->ranges, set->count, sizeof(PatternCharRange),
        char_range_compare, NULL);
    size_t kept = 0;
    for (size_t i = 0; i < set->count; i++) {
        PatternCharRange range = set->ranges[i];
        if (kept && range.lo <= set->ranges[kept - 1].hi + 1) {
            if (range.hi > set->ranges[kept - 1].hi) set->ranges[kept - 1].hi = range.hi;
        } else {
            set->ranges[kept++] = range;
        }
    }
    set->count = kept;
}

// replace a set by the characters it leaves out, over the whole code space
static bool char_set_complement(PatternCharSet* set) {
    char_set_normalize(set);
    PatternCharSet out = {};
    uint32_t next = 0;
    for (size_t i = 0; i < set->count; i++) {
        if (set->ranges[i].lo > next && !char_set_add(&out, next, set->ranges[i].lo - 1)) {
            char_set_free(&out);
            return false;
        }
        next = set->ranges[i].hi + 1;
    }
    if (next <= PATTERN_MAX_CODEPOINT && !char_set_add(&out, next, PATTERN_MAX_CODEPOINT)) {
        char_set_free(&out);
        return false;
    }
    char_set_free(set);
    *set = out;
    return true;
}

// `.` is every character but a newline, as RE2's `.` reads with dot_nl off
// (SPO10 is open); `...` is a run, never one character
static bool char_set_add_class(PatternCharSet* set, PatternCharClass char_class) {
    switch (char_class) {
    case PATTERN_DIGIT:
        return char_set_add(set, '0', '9');
    case PATTERN_WORD:
        return char_set_add(set, '0', '9') && char_set_add(set, 'A', 'Z') &&
            char_set_add(set, '_', '_') && char_set_add(set, 'a', 'z');
    case PATTERN_SPACE:  // RE2's `\s`: tab, newline, form feed, return, space
        return char_set_add(set, '\t', '\n') && char_set_add(set, '\f', '\r') &&
            char_set_add(set, ' ', ' ');
    case PATTERN_ALPHA:
        return char_set_add(set, 'A', 'Z') && char_set_add(set, 'a', 'z');
    case PATTERN_ANY:
        return char_set_add(set, 0, '\n' - 1) &&
            char_set_add(set, '\n' + 1, PATTERN_MAX_CODEPOINT);
    case PATTERN_ANY_STRING:
        return false;
    }
    return false;
}

static bool string_single_codepoint(String* str, uint32_t* codepoint) {
    if (!str || !str->len) return false;
    int used = str_utf8_decode(str->chars, str->len, codepoint);
    return used > 0 && (size_t)used == str->len;
}

// the one character a string-literal node spells
static bool node_single_codepoint(AstNode* node, uint32_t* codepoint) {
    return node && node->type && node->type->type_id == LMD_TYPE_STRING &&
        string_single_codepoint(((TypeString*)node->type)->string, codepoint);
}

// S11.1.3: a range runs between two single characters
static bool char_set_add_range(PatternCharSet* set, AstNode* start, AstNode* end) {
    uint32_t lo, hi;
    return node_single_codepoint(start, &lo) && node_single_codepoint(end, &hi) &&
        char_set_add(set, lo, hi);
}

// SP5, S11.1.3: a character range type (`type Lower = "a" to "z"`) is the set
// an island range spells. An integer range has no characters, and its bounds
// must not be read as code points.
static bool char_range_type_bounds(Type* type, uint32_t* lo, uint32_t* hi) {
    int64_t start = 0, end = 0;
    if (!lambda_type_is_range(type) || !((TypeRange*)type)->is_char ||
            !lambda_range_type_bounds(type, &start, &end)) return false;
    *lo = (uint32_t)start;
    *hi = (uint32_t)end;
    return true;
}

// SP7: a named literal union is the literal-only pattern it spells
static bool char_set_add_literal_type(PatternCharSet* set, Type* type) {
    type = literal_type_unwrap(type);
    if (!type) return false;
    uint32_t lo, hi;
    if (char_range_type_bounds(type, &lo, &hi)) return char_set_add(set, lo, hi);
    if (type->type_id == LMD_TYPE_STRING && type->is_literal) {
        uint32_t codepoint;
        return string_single_codepoint(((TypeString*)type)->string, &codepoint) &&
            char_set_add(set, codepoint, codepoint);
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BINARY) {
        TypeBinary* binary = (TypeBinary*)type;
        return binary->op == OPERATOR_UNION &&
            char_set_add_literal_type(set, binary->left) &&
            char_set_add_literal_type(set, binary->right);
    }
    return false;
}

// Add the characters a node denotes; false when the node is not a
// single-character set: a class, a range, a one-character string, a negated
// set, or a union, group or named pattern built only from these.
static bool char_set_add_node(PatternCharSet* set, AstNode* node, int depth) {
    if (!node || depth > PATTERN_MAX_DEPTH) return false;
    switch (node->node_type) {
    case AST_NODE_PATTERN_CHAR_CLASS:
        return char_set_add_class(set, ((AstPatternCharClassNode*)node)->char_class);
    case AST_NODE_PRIMARY: {
        AstPrimaryNode* primary = (AstPrimaryNode*)node;
        uint32_t codepoint;
        if (primary->type && primary->type->type_id == LMD_TYPE_STRING) {
            return node_single_codepoint(node, &codepoint) &&
                char_set_add(set, codepoint, codepoint);
        }
        return char_set_add_node(set, primary->expr, depth + 1);
    }
    case AST_NODE_PATTERN_RANGE: {
        AstPatternRangeNode* range = (AstPatternRangeNode*)node;
        return char_set_add_range(set, range->start, range->end);
    }
    case AST_NODE_BINARY:
    case AST_NODE_BINARY_TYPE: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        if (binary->op == OPERATOR_TO) return char_set_add_range(set, binary->left, binary->right);
        return binary->op == OPERATOR_UNION &&
            char_set_add_node(set, binary->left, depth + 1) &&
            char_set_add_node(set, binary->right, depth + 1);
    }
    case AST_NODE_UNARY:
    case AST_NODE_UNARY_TYPE: {
        // a repeated item is a run, not one character
        AstUnaryNode* unary = (AstUnaryNode*)node;
        if (unary->op != OPERATOR_NOT) return false;
        PatternCharSet inner = {};
        bool ok = char_set_add_node(&inner, unary->operand, depth + 1) &&
            char_set_complement(&inner);
        for (size_t i = 0; ok && i < inner.count; i++) {
            ok = char_set_add(set, inner.ranges[i].lo, inner.ranges[i].hi);
        }
        char_set_free(&inner);
        return ok;
    }
    case AST_NODE_LIST_TYPE: {
        AstListNode* group = (AstListNode*)node;
        if (!group->item) return false;
        for (AstNode* item = group->item; item; item = item->next) {
            if (!char_set_add_node(set, item, depth + 1)) return false;
        }
        return true;
    }
    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        AstNode* declared = ident->entry ? ident->entry->node : NULL;
        if (!declared) return false;
        if (declared->node_type == AST_NODE_STRING_PATTERN ||
                declared->node_type == AST_NODE_SYMBOL_PATTERN) {
            return char_set_add_node(set, ((AstPatternDefNode*)declared)->as, depth + 1);
        }
        return char_set_add_literal_type(set, declared->type);
    }
    default:
        return false;
    }
}

bool pattern_is_char_set(AstNode* node) {
    PatternCharSet set = {};
    bool ok = char_set_add_node(&set, node, 0);
    char_set_free(&set);
    return ok;
}

// class members print as themselves when alphanumeric, as \x{…} otherwise
static void char_set_emit_codepoint(StrBuf* regex, uint32_t codepoint) {
    if ((codepoint >= '0' && codepoint <= '9') || (codepoint >= 'A' && codepoint <= 'Z') ||
            (codepoint >= 'a' && codepoint <= 'z')) {
        strbuf_append_char(regex, (char)codepoint);
    } else {
        strbuf_append_format(regex, "\\x{%X}", codepoint);
    }
}

// one RE2 class; an empty set becomes a class that admits nothing
static void char_set_emit(StrBuf* regex, PatternCharSet* set) {
    char_set_normalize(set);
    if (!set->count) {
        strbuf_append_str(regex, "[^\\x{0}-\\x{10FFFF}]");
        return;
    }
    strbuf_append_char(regex, '[');
    for (size_t i = 0; i < set->count; i++) {
        char_set_emit_codepoint(regex, set->ranges[i].lo);
        if (set->ranges[i].hi > set->ranges[i].lo) {
            strbuf_append_char(regex, '-');
            char_set_emit_codepoint(regex, set->ranges[i].hi);
        }
    }
    strbuf_append_char(regex, ']');
}

// Lower a single-character set, or its complement, to one RE2 class. False
// when the node is not a set, which the resolver has already reported.
static bool compile_char_set(StrBuf* regex, AstNode* node, bool negate) {
    PatternCharSet set = {};
    bool ok = char_set_add_node(&set, node, 0) && (!negate || char_set_complement(&set));
    if (ok) char_set_emit(regex, &set);
    char_set_free(&set);
    return ok;
}

// Convert character class to regex
static void compile_char_class(StrBuf* regex, PatternCharClass char_class) {
    if (char_class == PATTERN_ANY) {
        strbuf_append_char(regex, '.');
        return;
    }
    if (char_class == PATTERN_ANY_STRING) {
        // S16.8.6v3: `...` is `any*`, newlines included; RE2's `.` stops at one
        strbuf_append_str(regex, "(?s:.*)");
        return;
    }
    PatternCharSet set = {};
    if (char_set_add_class(&set, char_class)) char_set_emit(regex, &set);
    char_set_free(&set);
}

// SP7: a named literal union is the literal-only pattern it spells. Render it
// whole or not at all: a failed rendering stops partway through its buffer.
static bool append_literal_pattern(StrBuf* regex, Type* type) {
    StrBuf* literal = strbuf_new_cap(64);
    bool ok = append_literal_type(literal, type, LITERAL_TYPE_REGEX);
    if (ok) strbuf_append_str_n(regex, literal->str, literal->length);
    strbuf_free(literal);
    return ok;
}

// Why a pattern cannot be lowered to a regex. The first reason wins, and the
// resolver reports it as a compile-time diagnostic: these were only logged,
// leaving a pattern that silently matched nothing, or only "" (§7 #3).
static bool pattern_lowering_failed(StrBuf* error, const char* reason) {
    log_error("compile_pattern_to_regex: %s", reason);
    if (error && !error->length) strbuf_append_str(error, reason);
    return false;
}

// S11.1.2v3: an island may name a pattern definition, or a literal type that
// is a set of strings: a literal union (SP7) or a character range (SP5).
bool pattern_can_name(AstNode* declared) {
    if (!declared) return false;
    if (declared->node_type == AST_NODE_STRING_PATTERN ||
            declared->node_type == AST_NODE_SYMBOL_PATTERN) return true;
    StrBuf* scratch = strbuf_new_cap(64);
    bool ok = append_literal_type(scratch, declared->type, LITERAL_TYPE_REGEX);
    strbuf_free(scratch);
    return ok;
}

// `depth` counts named-pattern expansions, so a definition that reaches
// itself fails instead of expanding without end.
static bool lower_pattern(StrBuf* regex, AstNode* node, StrBuf* error, int depth);

// `(?:node)` followed by `suffix`
static bool lower_pattern_group(StrBuf* regex, AstNode* node, StrBuf* error, int depth,
        const char* suffix) {
    strbuf_append_str(regex, "(?:");
    if (!lower_pattern(regex, node, error, depth)) return false;
    strbuf_append_char(regex, ')');
    if (suffix) strbuf_append_str(regex, suffix);
    return true;
}

bool compile_pattern_to_regex(StrBuf* regex, AstNode* node, StrBuf* error) {
    return lower_pattern(regex, node, error, 0);
}

static bool lower_pattern(StrBuf* regex, AstNode* node, StrBuf* error, int depth) {
    if (!node) return pattern_lowering_failed(error, "a pattern part is missing");

    switch (node->node_type) {
    case AST_NODE_PRIMARY: {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->type && pri->type->type_id == LMD_TYPE_STRING) {
            TypeString* str_type = (TypeString*)pri->type;
            if (str_type->string) escape_regex_literal(regex, str_type->string);
            return true;
        }
        if (pri->expr) return lower_pattern(regex, pri->expr, error, depth);
        return pattern_lowering_failed(error, "a pattern holds only string literals");
    }

    case AST_NODE_PATTERN_CHAR_CLASS:
        compile_char_class(regex, ((AstPatternCharClassNode*)node)->char_class);
        return true;

    case AST_NODE_PATTERN_RANGE:
        // "a" to "z" -> [a-z]; bounds are code points, never their first byte
        return compile_char_set(regex, node, false) ||
            pattern_lowering_failed(error, "a range in a pattern runs between two single characters");

    case AST_NODE_BINARY:
    case AST_NODE_BINARY_TYPE: {
        AstBinaryNode* bin = (AstBinaryNode*)node;
        if (bin->op == OPERATOR_UNION) {
            // a | b -> (?:a|b); `|` is the island's only binary operator (S11.1.2v3)
            strbuf_append_str(regex, "(?:");
            if (!lower_pattern(regex, bin->left, error, depth)) return false;
            strbuf_append_char(regex, '|');
            if (!lower_pattern(regex, bin->right, error, depth)) return false;
            strbuf_append_char(regex, ')');
            return true;
        }
        if (bin->op == OPERATOR_TO) {
            return compile_char_set(regex, node, false) ||
                pattern_lowering_failed(error, "a range in a pattern runs between two single characters");
        }
        return pattern_lowering_failed(error, "`|` is the only operator that joins two patterns");
    }

    case AST_NODE_UNARY:
    case AST_NODE_UNARY_TYPE: {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        switch (unary->op) {
        case OPERATOR_OPTIONAL:
            return lower_pattern_group(regex, unary->operand, error, depth, "?");
        case OPERATOR_ONE_MORE:
            return lower_pattern_group(regex, unary->operand, error, depth, "+");
        case OPERATOR_ZERO_MORE:
            return lower_pattern_group(regex, unary->operand, error, depth, "*");
        case OPERATOR_REPEAT:
            // `{n}` / `{n+}` / `{n,m}` -> (?:a){n} / (?:a){n,} / (?:a){n,m}; the
            // island parser has checked the spelling (S16.8.6v3)
            if (!lower_pattern_group(regex, unary->operand, error, depth, NULL)) return false;
            return convert_occurrence_to_regex(regex, &unary->op_str) ||
                pattern_lowering_failed(error, "a count is `{n}`, `{n,m}` or `{n+}`");
        case OPERATOR_NOT:
            // S11.1.2v3: `!` complements a single-character set, lowered to one
            // `[^…]`-style class; RE2 has no string complement or look-around
            return compile_char_set(regex, unary->operand, true) ||
                pattern_lowering_failed(error, "`!` in a pattern negates a single character");
        default:
            return pattern_lowering_failed(error, "unsupported pattern operator");
        }
    }

    case AST_NODE_PATTERN_SEQ:
        // whitespace concatenation (S11.1.2v3)
        for (AstNode* child = ((AstPatternSeqNode*)node)->first; child; child = child->next) {
            if (!lower_pattern(regex, child, error, depth)) return false;
        }
        return true;

    case AST_NODE_LIST_TYPE:
    case AST_NODE_ARRAY_TYPE: {
        // a group `( … )`; several items are alternatives
        AstNode* first = node->node_type == AST_NODE_LIST_TYPE ?
            ((AstListNode*)node)->item : ((AstArrayNode*)node)->item;
        if (!first) return pattern_lowering_failed(error, "an empty group in a pattern");
        strbuf_append_str(regex, "(?:");
        for (AstNode* item = first; item; item = item->next) {
            if (item != first) strbuf_append_char(regex, '|');
            if (!lower_pattern(regex, item, error, depth)) return false;
        }
        strbuf_append_char(regex, ')');
        return true;
    }

    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        AstNode* declared = ident->entry ? ident->entry->node : NULL;
        if (declared && (declared->node_type == AST_NODE_STRING_PATTERN ||
                declared->node_type == AST_NODE_SYMBOL_PATTERN)) {
            if (depth >= PATTERN_MAX_DEPTH) {
                return pattern_lowering_failed(error, "a pattern cannot refer to itself");
            }
            return lower_pattern(regex, ((AstPatternDefNode*)declared)->as, error, depth + 1);
        }
        if (declared && append_literal_pattern(regex, declared->type)) return true;
        // the resolver reports an unusable name at the name itself; this is the
        // backstop for any path that reaches lowering without it
        return pattern_lowering_failed(error,
            "a pattern names only patterns, literal unions and character ranges defined before it");
    }

    default:
        return pattern_lowering_failed(error, "unsupported pattern part");
    }
}

static void render_pattern_literal(StrBuf* source, String* value) {
    strbuf_append_char(source, '"');
    if (value) escape_append_js_quoted(source, value->chars, value->len, '"');
    strbuf_append_char(source, '"');
}

static void render_pattern_surface(StrBuf* source, AstNode* node) {
    if (!node) return;
    switch (node->node_type) {
    case AST_NODE_PRIMARY: {
        AstPrimaryNode* primary = (AstPrimaryNode*)node;
        if (primary->type && primary->type->type_id == LMD_TYPE_STRING) {
            render_pattern_literal(source, ((TypeString*)primary->type)->string);
        } else {
            render_pattern_surface(source, primary->expr);
        }
        break;
    }
    case AST_NODE_PATTERN_CHAR_CLASS: {
        AstPatternCharClassNode* cc = (AstPatternCharClassNode*)node;
        switch (cc->char_class) {
        case PATTERN_DIGIT: strbuf_append_char(source, 'd'); break;
        case PATTERN_WORD: strbuf_append_char(source, 'w'); break;
        case PATTERN_SPACE: strbuf_append_char(source, 's'); break;
        case PATTERN_ALPHA: strbuf_append_char(source, 'a'); break;
        case PATTERN_ANY: strbuf_append_char(source, '.'); break;
        case PATTERN_ANY_STRING: strbuf_append_str(source, "..."); break;
        }
        break;
    }
    case AST_NODE_PATTERN_RANGE: {
        AstPatternRangeNode* range = (AstPatternRangeNode*)node;
        render_pattern_surface(source, range->start);
        strbuf_append_str(source, " to ");
        render_pattern_surface(source, range->end);
        break;
    }
    case AST_NODE_BINARY:
    case AST_NODE_BINARY_TYPE: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        strbuf_append_char(source, '(');
        render_pattern_surface(source, binary->left);
        if (binary->op == OPERATOR_UNION) strbuf_append_str(source, " | ");
        else if (binary->op == OPERATOR_TO) strbuf_append_str(source, " to ");
        render_pattern_surface(source, binary->right);
        strbuf_append_char(source, ')');
        break;
    }
    case AST_NODE_UNARY:
    case AST_NODE_UNARY_TYPE: {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        if (unary->op == OPERATOR_NOT) {
            strbuf_append_char(source, '!');
            strbuf_append_char(source, '(');
            render_pattern_surface(source, unary->operand);
            strbuf_append_char(source, ')');
        } else {
            strbuf_append_char(source, '(');
            render_pattern_surface(source, unary->operand);
            strbuf_append_char(source, ')');
            if (unary->op_str.str && unary->op_str.length > 0) {
                strbuf_append_str_n(source, unary->op_str.str, unary->op_str.length);
            }
        }
        break;
    }
    case AST_NODE_PATTERN_SEQ: {
        AstPatternSeqNode* sequence = (AstPatternSeqNode*)node;
        AstNode* child = sequence->first;
        bool first = true;
        while (child) {
            if (!first) strbuf_append_char(source, ' ');
            render_pattern_surface(source, child);
            first = false;
            child = child->next;
        }
        break;
    }
    case AST_NODE_LIST_TYPE: {
        AstListNode* list = (AstListNode*)node;
        strbuf_append_char(source, '(');
        AstNode* item = list->item;
        bool first = true;
        while (item) {
            if (!first) strbuf_append_str(source, " | ");
            render_pattern_surface(source, item);
            first = false;
            item = item->next;
        }
        strbuf_append_char(source, ')');
        break;
    }
    case AST_NODE_ARRAY_TYPE: {
        AstArrayNode* array = (AstArrayNode*)node;
        strbuf_append_char(source, '[');
        render_pattern_surface(source, array->item);
        strbuf_append_char(source, ']');
        break;
    }
    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        if (ident->name) strbuf_append_str_n(source, ident->name->chars, ident->name->len);
        break;
    }
    case AST_NODE_PATTERN_ISLAND: {
        AstPatternIslandNode* island = (AstPatternIslandNode*)node;
        strbuf_append_str(source, island->is_symbol ? "\\symbol(" : "\\(");
        render_pattern_surface(source, island->pattern);
        strbuf_append_char(source, ')');
        break;
    }
    default:
        log_error("render_pattern_surface: unsupported AST node type %d", node->node_type);
        break;
    }
}

static String* pattern_pool_string(Pool* pool, const char* chars, size_t length) {
    return string_from_strview(strview_init(chars, length), pool);
}

// Compile Lambda pattern AST to RE2 regex
TypePattern* compile_pattern_ast(Pool* pool, AstNode* pattern_ast, bool is_symbol, const char** error_msg) {
    if (!pattern_ast) {
        if (error_msg) *error_msg = "null pattern AST";
        return nullptr;
    }

    // Build regex string
    StrBuf* regex = strbuf_new_cap(256);
    strbuf_append_str(regex, "^");  // anchor start for full match
    StrBuf* lowering_error = strbuf_new_cap(64);
    bool lowered = compile_pattern_to_regex(regex, pattern_ast, lowering_error);
    strbuf_append_str(regex, "$");  // anchor end
    static char error_buffer[256];
    if (!lowered) {
        if (error_msg) {
            snprintf(error_buffer, sizeof(error_buffer), "%s", lowering_error->str);
            *error_msg = error_buffer;
        }
        strbuf_free(lowering_error);
        strbuf_free(regex);
        return nullptr;
    }
    strbuf_free(lowering_error);

    StrBuf* surface = strbuf_new_cap(256);
    strbuf_append_str(surface, is_symbol ? "\\symbol(" : "\\(");
    render_pattern_surface(surface, pattern_ast);
    strbuf_append_char(surface, ')');

    log_debug("Compiled pattern regex: %s", regex->str);

    re2::RE2::Options options = lam::re2_glue_default_options();
    re2::RE2* re2 = lam::re2_glue_compile(
        regex->str, regex->length, options, "compile_pattern_ast",
        error_msg ? error_buffer : nullptr, sizeof(error_buffer));
    if (!re2) {
        if (error_msg) *error_msg = error_buffer;
        strbuf_free(regex);
        strbuf_free(surface);
        return nullptr;
    }

    // Allocate TypePattern
    TypePattern* pattern = (TypePattern*)pool_calloc(pool, sizeof(TypePattern));
    pattern->type_id = LMD_TYPE_TYPE;
    pattern->kind = TYPE_KIND_PATTERN;
    pattern->is_symbol = is_symbol;
    pattern->re2 = re2;
    pattern->re2_unanchored = nullptr;
    pattern->pattern_index = -1;  // Will be set when registered

    // Keep the diagnostic surface separate from the anchored regex consumed by
    // partial matching; otherwise source rendering changes break find/replace.
    pattern->source = pattern_pool_string(pool, surface->str, surface->length);
    pattern->regex_source = pattern_pool_string(pool, regex->str, regex->length);

    strbuf_free(regex);
    strbuf_free(surface);
    return pattern;
}

bool compile_runtime_pattern(Pool* pool, ArrayList* type_list, TypePattern* pattern,
                             AstNode* pattern_ast, bool is_symbol, const char** error_msg) {
    if (!pool || !type_list || !pattern || !pattern_ast) return false;
    if (pattern->re2) {
        if (pattern->pattern_index >= 0) return true;
        // A direct parser may compile the regex before the module type-list
        // publication pass. Publish that existing compiled identity instead
        // of rejecting an otherwise valid named pattern at its first use.
        arraylist_append(type_list, pattern);
        pattern->pattern_index = type_list->length - 1;
        return true;
    }

    const char* reason = NULL;
    TypePattern* compiled = compile_pattern_ast(pool, pattern_ast, is_symbol, &reason);
    if (!compiled) {
        log_error("pattern compile: failed to build regex: %s", reason ? reason : "unknown error");
        if (error_msg) *error_msg = reason;
        return false;
    }
    pattern->re2 = compiled->re2;
    pattern->re2_unanchored = NULL;
    pattern->source = compiled->source;
    pattern->regex_source = compiled->regex_source;
    arraylist_append(type_list, pattern);
    pattern->pattern_index = type_list->length - 1;
    return true;
}

// Walk the declaration-bearing shapes that can contain a named pattern. This
// stays beside compile_pattern_ast so MIR lowering and the AST interpreter
// publish exactly one module-local TypePattern identity per definition.
bool compile_script_pattern_definitions(Pool* pool, ArrayList* type_list,
                                        AstNode* node) {
    if (!pool || !type_list) return false;
    bool ok = true;
    while (node) {
        switch (node->node_type) {
        case AST_NODE_STRING_PATTERN:
        case AST_NODE_SYMBOL_PATTERN: {
            AstPatternDefNode* definition = (AstPatternDefNode*)node;
            TypePattern* pattern = (TypePattern*)definition->type;
            if (!pattern || !definition->as) {
                log_error("pattern prepass: invalid definition '%.*s'",
                    definition->name ? (int)definition->name->len : 0,
                    definition->name ? definition->name->chars : "");
                ok = false;
                break;
            }
            bool needs_compile = !pattern->re2;
            if (!compile_runtime_pattern(pool, type_list, pattern, definition->as,
                    definition->is_symbol)) {
                log_error("pattern prepass: failed to compile '%.*s'",
                    (int)definition->name->len, definition->name->chars);
                ok = false;
                break;
            }
            if (needs_compile) {
                log_debug("pattern prepass: compiled '%.*s' index=%d",
                    (int)definition->name->len, definition->name->chars,
                    pattern->pattern_index);
            }
            break;
        }
        case AST_NODE_CONTENT:
        case AST_NODE_LIST: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare &&
                    !compile_script_pattern_definitions(pool, type_list, list->declare)) {
                ok = false;
            }
            if (!compile_script_pattern_definitions(pool, type_list, list->item)) ok = false;
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_TYPE_STAM:
        case AST_NODE_VAR_STAM:
            if (!compile_script_pattern_definitions(pool, type_list,
                    ((AstLetNode*)node)->declare)) ok = false;
            break;
        case AST_NODE_OBJECT_TYPE: {
            AstObjectTypeNode* object = (AstObjectTypeNode*)node;
            if (!compile_script_pattern_definitions(pool, type_list, object->methods)) ok = false;
            break;
        }
        case AST_NODE_FUNC:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_EXPR:
            if (!compile_script_pattern_definitions(pool, type_list,
                    ((AstFuncNode*)node)->body)) ok = false;
            break;
        default:
            break;
        }
        node = node->next;
    }
    return ok;
}

static bool append_literal_type(StrBuf* output, Type* type, LiteralTypeRender render) {
    type = literal_type_unwrap(type);
    if (!type) return false;
    uint32_t lo, hi;
    if (char_range_type_bounds(type, &lo, &hi)) {
        TypeRange* range = (TypeRange*)type;
        if (render == LITERAL_TYPE_SURFACE) {
            render_pattern_literal(output, range->start.get_safe_string());
            strbuf_append_str(output, " to ");
            render_pattern_literal(output, range->end.get_safe_string());
            return true;
        }
        PatternCharSet set = {};
        bool ok = char_set_add(&set, lo, hi);
        if (ok) char_set_emit(output, &set);
        char_set_free(&set);
        return ok;
    }
    if (type->type_id == LMD_TYPE_STRING && type->is_literal) {
        String* literal = ((TypeString*)type)->string;
        if (!literal) return false;
        if (render == LITERAL_TYPE_REGEX) escape_regex_literal(output, literal);
        else render_pattern_literal(output, literal);
        return true;
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BINARY) {
        TypeBinary* binary = (TypeBinary*)type;
        if (binary->op != OPERATOR_UNION) return false;
        strbuf_append_str(output, render == LITERAL_TYPE_REGEX ? "(?:" : "(");
        if (!append_literal_type(output, binary->left, render)) return false;
        strbuf_append_str(output, render == LITERAL_TYPE_REGEX ? "|" : " | ");
        if (!append_literal_type(output, binary->right, render)) return false;
        strbuf_append_char(output, ')');
        return true;
    }
    return false;
}

TypePattern* compile_literal_type_pattern(Pool* pool, Type* type, bool is_symbol,
                                          const char** error_msg) {
    if (!pool || !type) {
        if (error_msg) *error_msg = "null literal type";
        return nullptr;
    }

    StrBuf* regex = strbuf_new_cap(128);
    strbuf_append_char(regex, '^');
    if (!append_literal_type(regex, type, LITERAL_TYPE_REGEX)) {
        if (error_msg) *error_msg = "type is not a literal string union";
        strbuf_free(regex);
        return nullptr;
    }
    strbuf_append_char(regex, '$');

    StrBuf* surface = strbuf_new_cap(128);
    strbuf_append_str(surface, is_symbol ? "\\symbol(" : "\\(");
    if (!append_literal_type(surface, type, LITERAL_TYPE_SURFACE)) {
        if (error_msg) *error_msg = "type is not a literal string union";
        strbuf_free(regex);
        strbuf_free(surface);
        return nullptr;
    }
    strbuf_append_char(surface, ')');

    re2::RE2::Options options = lam::re2_glue_default_options();
    static char error_buffer[256];
    re2::RE2* re2 = lam::re2_glue_compile(
        regex->str, regex->length, options, "compile_literal_type_pattern",
        error_msg ? error_buffer : nullptr, sizeof(error_buffer));
    if (!re2) {
        if (error_msg) *error_msg = error_buffer;
        strbuf_free(regex);
        strbuf_free(surface);
        return nullptr;
    }

    TypePattern* pattern = (TypePattern*)pool_calloc(pool, sizeof(TypePattern));
    pattern->type_id = LMD_TYPE_TYPE;
    pattern->kind = TYPE_KIND_PATTERN;
    pattern->is_symbol = is_symbol;
    pattern->re2 = re2;
    pattern->re2_unanchored = nullptr;
    pattern->pattern_index = -1;
    pattern->source = pattern_pool_string(pool, surface->str, surface->length);
    pattern->regex_source = pattern_pool_string(pool, regex->str, regex->length);

    strbuf_free(regex);
    strbuf_free(surface);
    return pattern;
}

// Match string against pattern (full match)
bool pattern_full_match(TypePattern* pattern, String* str) {
    if (!pattern || !pattern->re2 || !str) {
        return false;
    }

    re2::StringPiece input(str->chars, str->len);
    return re2::RE2::FullMatch(input, *pattern->re2);
}

bool pattern_full_match_chars(TypePattern* pattern, const char* chars, size_t len) {
    if (!pattern || !pattern->re2 || !chars) {
        return false;
    }

    re2::StringPiece input(chars, len);
    return re2::RE2::FullMatch(input, *pattern->re2);
}

// Match string against pattern (partial match)
bool pattern_partial_match(TypePattern* pattern, String* str) {
    if (!pattern || !pattern->re2 || !str) {
        return false;
    }

    re2::StringPiece input(str->chars, str->len);
    return re2::RE2::PartialMatch(input, *pattern->re2);
}

// Destroy a compiled pattern
void pattern_destroy(TypePattern* pattern) {
    if (pattern && pattern->re2) {
        lam::re2_glue_release(pattern->re2);
        pattern->re2 = nullptr;
    }
    if (pattern && pattern->re2_unanchored) {
        lam::re2_glue_release(pattern->re2_unanchored);
        pattern->re2_unanchored = nullptr;
    }
}

// One-shot RE2 helpers — see re2_wrapper.hpp. These are the C+-convention
// boundary: rb_runtime/py_stdlib/etc. call these instead of `new re2::RE2`
// so the new/delete stays inside the wrapper.
re2::RE2* re2_compile(const char* pattern, size_t pattern_len) {
    re2::RE2::Options opts = lam::re2_glue_default_options();
    return lam::re2_glue_compile(pattern, pattern_len, opts, nullptr);
}

void re2_release(re2::RE2* re) {
    lam::re2_glue_release(re);
}

static String* pattern_regex_source(TypePattern* pattern) {
    return pattern ? (pattern->regex_source ? pattern->regex_source : pattern->source) : nullptr;
}

bool pattern_unanchored_source(TypePattern* pattern, const char** out, size_t* out_len) {
    // the source is "^<regex>$"; the partial-match operations search <regex>
    String* regex_source = pattern_regex_source(pattern);
    if (!regex_source) return false;
    const char* src = regex_source->chars;
    size_t len = regex_source->len;
    if (len < 2 || src[0] != '^' || src[len - 1] != '$') {
        log_error("pattern_unanchored_source: unexpected source format: %s", src);
        return false;
    }
    *out = src + 1;
    *out_len = len - 2;
    return true;
}

#ifndef SIMPLE_SCHEMA_PARSER
static re2::RE2* pattern_get_unanchored_options(TypePattern* pattern, bool ignore_case, bool* must_release) {
    *must_release = false;
    if (!ignore_case) return pattern_get_unanchored(pattern);
    const char* src = NULL;
    size_t len = 0;
    if (!pattern_unanchored_source(pattern, &src, &len)) return nullptr;
    re2::RE2::Options opts = lam::re2_glue_default_options();
    opts.set_case_sensitive(false);
    re2::RE2* re = lam::re2_glue_compile(
        src, len, opts, "pattern_get_unanchored_options");
    if (!re) return nullptr;
    *must_release = true;
    return re;
}

static size_t pattern_next_match_pos(const char* str, size_t len,
                                     size_t start, size_t match_len) {
    size_t next = start + match_len;
    if (match_len != 0) return next;
    // A zero-width match must advance by a code point, even though RE2 uses bytes.
    size_t step = next < len ? str_utf8_char_len((unsigned char)str[next]) : 1;
    return next + (step ? step : 1);
}

static int64_t pattern_count_matches_with_re(re2::RE2* re, const char* str, size_t len) {
    re2::StringPiece input(str, len);
    re2::StringPiece match;
    size_t pos = 0;
    int64_t count = 0;
    while (pos <= len) {
        if (!re->Match(input, pos, len, re2::RE2::UNANCHORED, &match, 1)) break;
        int64_t match_start = (int64_t)(match.data() - str);
        size_t match_len_val = match.size();
        count++;
        pos = pattern_next_match_pos(str, len, (size_t)match_start, match_len_val);
    }
    return count;
}
#endif

// Get or create unanchored RE2 for partial matching operations.
// The source string is stored as "^<regex>$"; we strip the anchors.
re2::RE2* pattern_get_unanchored(TypePattern* pattern) {
    if (!pattern) return nullptr;
    if (pattern->re2_unanchored) return pattern->re2_unanchored;

    const char* src = NULL;
    size_t len = 0;
    if (!pattern_unanchored_source(pattern, &src, &len)) return nullptr;
    re2::RE2::Options opts = lam::re2_glue_default_options();
    pattern->re2_unanchored = lam::re2_glue_compile(
        src, len, opts, "pattern_get_unanchored");
    if (!pattern->re2_unanchored) {
        return nullptr;
    }

    log_debug("pattern_get_unanchored: compiled '%.*s'", (int)len, src);
    return pattern->re2_unanchored;
}

// The following functions are only used in the main executable, not the shared library.
// They depend on runtime symbols (heap_alloc, list, array_push, etc.) not available in lambda-input-full-cpp.
#ifndef SIMPLE_SCHEMA_PARSER

// helper: create a heap-allocated String from a char* + len
static String* make_heap_string(const char* src, size_t len) {
    return heap_strcpy(src, len);
}

TypeMap* runtime_result_shape(const char* const* names, const TypeId* types, int count) {
    // Impl_Map_Transition_Coverage P3: every result map of one field list --
    // each regex match, each grep record -- shares the runtime tree's node for
    // it, found by following one edge per field from the root, instead of a
    // fresh TypeMap and chain per call. The private build stays the fallback
    // for a tree that declines.
    if (Input* tree = runtime_shape_tree()) {
        TypeMap* node = NULL;
        for (int i = 0; i < count; i++) {
            node = type_tree_add_map_field_chars(tree, node, names[i],
                (uint32_t)strlen(names[i]), types[i], NULL);
            if (!node) break;
        }
        if (node && count > 0) return node;
    }
    Pool* pool = context->pool;
    ShapeEntry* first = NULL;
    ShapeEntry* prev = NULL;
    int64_t byte_size = 0;
    for (int i = 0; i < count; i++) {
        ShapeEntry* entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry) + sizeof(StrView));
        StrView* name = (StrView*)((char*)entry + sizeof(ShapeEntry));
        name->str = names[i];
        name->length = strlen(names[i]);
        entry->name = name;
        shape_entry_set_type(entry, type_info[types[i]].type);
        entry->byte_offset = byte_size;
        byte_size += entry->storage.byte_size;
        if (prev) prev->chain_next = entry;
        else first = entry;
        prev = entry;
    }
    TypeMap* mt = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
    mt->shape = first;
    mt->last = prev;
    mt->length = count;
    mt->byte_size = byte_size;
    // result shapes die with the execution pool and must not enter a cached
    // module's compiler registry (D8.5.1v7).
    mt->type_index = -1;
    typemap_hash_build(mt, pool);
    return mt;
}

// helper: create a match map {value: string, index: int}
// allocates TypeMap + ShapeEntry chain + data buffer on heap
Map* create_match_map(const char* match_str, size_t match_len, int64_t index) {
    static const char* const names[] = {"value", "index"};
    static const TypeId types[] = {LMD_TYPE_STRING, LMD_TYPE_INT};
    TypeMap* mt = runtime_result_shape(names, types, 2);
    ShapeEntry* e_value = mt->shape;
    ShapeEntry* e_index = mt->last;
    int64_t byte_size = mt->byte_size;

    // create Map container
    Map* mp = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    RootFrame roots(1);
    Rooted<Map*> rooted_map(roots, mp);
    mp->type_id = LMD_TYPE_MAP;
    mp->type = mt;
    mp->data = heap_data_calloc(byte_size);

    // store value field (String* pointer)
    String* val_str = make_heap_string(match_str, match_len);
    // Both data-buffer and string allocation may collect; the map is the only
    // owner of its data and is not visible to the caller until this returns.
    mp = rooted_map.get();
    *(String**)((char*)mp->data + e_value->byte_offset) = val_str;

    // Shaped int fields are int64 lanes. Writing IEEE bits here makes the
    // map reader decode an ordinary match offset as the `+inf` sentinel.
    *(int64_t*)((char*)mp->data + e_index->byte_offset) = lambda_double_to_int_lane((double)index);

    return mp;
}

List* pattern_find_all_options(TypePattern* pattern, const char* str, size_t len,
                               int64_t limit, bool ignore_case) {
    List* result = list();  // find() constructs an array (S2.5.7)
    RootFrame roots(2);
    Rooted<List*> rooted_result(roots, result);
    Rooted<Map*> rooted_match(roots, (Map*)NULL);
    // an empty subject is searched too: `find("", \(d*))` has one empty match
    if (!pattern || !str) return rooted_result.get();

    bool must_release = false;
    re2::RE2* re = pattern_get_unanchored_options(pattern, ignore_case, &must_release);
    if (!re) return rooted_result.get();

    int64_t total = pattern_count_matches_with_re(re, str, len);
    int64_t first = 0, selected_count = 0;
    lam::re2_glue_select_match_window(total, limit, &first, &selected_count);
    if (selected_count == 0) {
        if (must_release) lam::re2_glue_release(re);
        return rooted_result.get();
    }

    re2::StringPiece input(str, len);
    re2::StringPiece match;
    size_t pos = 0;
    size_t indexed_byte = 0;
    int64_t indexed_codepoints = 0;
    int64_t ordinal = 0;
    int64_t pushed = 0;

    while (pos <= len) {
        if (!re->Match(input, pos, len, re2::RE2::UNANCHORED, &match, 1)) {
            break;
        }

        size_t match_start = (size_t)(match.data() - str);
        size_t match_len_val = match.size();

        if (ordinal >= first && pushed < selected_count) {
            // Count only the new prefix so match indices stay linear in input size.
            indexed_codepoints += (int64_t)str_utf8_count(str + indexed_byte,
                match_start - indexed_byte);
            indexed_byte = match_start;
            Map* m = create_match_map(match.data(), match_len_val, indexed_codepoints);
            // Keep a new match live until the rooted result list owns it.
            rooted_match.set(m);
            // find() builds an array of matches: the verbatim append
            array_push_verbatim((Array*)rooted_result.get(), {.map = rooted_match.get()});
            rooted_match.set((Map*)NULL);
            pushed++;
        }
        ordinal++;

        // advance past match; if zero-length match, advance by 1 char
        pos = pattern_next_match_pos(str, len, match_start, match_len_val);
        if (pushed >= selected_count) break;
    }

    if (must_release) lam::re2_glue_release(re);
    return rooted_result.get();
}

// Find all non-overlapping matches of pattern in string
List* pattern_find_all(TypePattern* pattern, const char* str, size_t len) {
    return pattern_find_all_options(pattern, str, len, 0, false);
}

String* pattern_replace_all_options(TypePattern* pattern, const char* str, size_t str_len,
                                    const char* repl, size_t repl_len,
                                    int64_t limit, bool ignore_case) {
    if (!pattern || !str) return nullptr;

    bool must_release = false;
    re2::RE2* re = pattern_get_unanchored_options(pattern, ignore_case, &must_release);
    if (!re) return nullptr;

    int64_t total = pattern_count_matches_with_re(re, str, str_len);
    int64_t first = 0, selected_count = 0;
    lam::re2_glue_select_match_window(total, limit, &first, &selected_count);
    if (selected_count == 0) {
        if (must_release) lam::re2_glue_release(re);
        return make_heap_string(str, str_len);
    }

    StrBuf* out = strbuf_new_cap(str_len + 1);
    re2::StringPiece input(str, str_len);
    re2::StringPiece match;
    size_t pos = 0;
    size_t copy_pos = 0;
    int64_t ordinal = 0;
    int64_t replaced = 0;

    while (pos <= str_len) {
        if (!re->Match(input, pos, str_len, re2::RE2::UNANCHORED, &match, 1)) break;
        size_t match_start = (size_t)(match.data() - str);
        size_t match_len_val = match.size();
        bool selected = ordinal >= first && replaced < selected_count;

        if (selected) {
            if (match_start > copy_pos) strbuf_append_str_n(out, str + copy_pos, match_start - copy_pos);
            if (repl && repl_len > 0) strbuf_append_str_n(out, repl, repl_len);
            copy_pos = match_start + match_len_val;
            replaced++;
        }

        ordinal++;
        pos = pattern_next_match_pos(str, str_len, match_start, match_len_val);
        if (replaced >= selected_count) break;
    }
    if (copy_pos < str_len) strbuf_append_str_n(out, str + copy_pos, str_len - copy_pos);

    String* result = make_heap_string(out->str, out->length);
    strbuf_free(out);
    if (must_release) lam::re2_glue_release(re);
    return result;
}

// S17.7.1: literal text searched case-insensitively folds as a pattern does,
// through RE2's Unicode simple case folding. The literal scan folded ASCII
// only, so `find("É", "é", {ignore_case: true})` missed what `\("é" d)` found,
// and so did the literal-only island `\("é")`, which is that literal (SP7).
// The needle becomes a transient pattern backed only by its anchored source.
static String* literal_regex_source(const char* needle, size_t needle_len) {
    StrBuf* regex = strbuf_new_cap(needle_len + 8);
    strbuf_append_char(regex, '^');
    escape_regex_chars(regex, needle, needle_len);
    strbuf_append_char(regex, '$');
    String* source = (String*)mem_alloc(sizeof(String) + regex->length + 1, MEM_CAT_PARSER);
    if (source) {
        memcpy(source->chars, regex->str, regex->length);
        source->chars[regex->length] = '\0';
        source->len = (uint32_t)regex->length;
        source->flags = 0;
        source->is_ascii = str_is_ascii(source->chars, source->len) ? 1 : 0;
    }
    strbuf_free(regex);
    return source;
}

List* literal_find_all_ignore_case(const char* str, size_t len,
                                   const char* needle, size_t needle_len, int64_t limit) {
    TypePattern pattern = {};
    pattern.regex_source = literal_regex_source(needle, needle_len);
    if (!pattern.regex_source) return nullptr;
    List* result = pattern_find_all_options(&pattern, str, len, limit, true);
    mem_free(pattern.regex_source);
    return result;
}

String* literal_replace_all_ignore_case(const char* str, size_t str_len,
                                        const char* needle, size_t needle_len,
                                        const char* repl, size_t repl_len, int64_t limit) {
    TypePattern pattern = {};
    pattern.regex_source = literal_regex_source(needle, needle_len);
    if (!pattern.regex_source) return nullptr;
    String* result = pattern_replace_all_options(&pattern, str, str_len, repl, repl_len,
        limit, true);
    mem_free(pattern.regex_source);
    return result;
}


static String* make_heap_rooted_slice(Rooted<Item>& rooted_source, size_t offset, size_t len) {
    String* value = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
    // heap_alloc may compact the source; derive its character pointer only
    // after the safepoint instead of retaining a raw nursery interior pointer.
    const char* src = rooted_source.get().get_chars() + offset;
    value->len = (uint32_t)len;
    value->flags = 0;
    value->is_ascii = 1;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)src[i] >= 128) { value->is_ascii = 0; break; }
    }
    str_copy(value->chars, len + 1, src, len);
    return value;
}

// Split string by pattern matches
// Split segments are separate elements by definition, so every append here uses
// array_push, which stores the item verbatim (D2.6.5). list_push applies
// S16.7's content rules — it concatenates a string onto the previous element —
// which collapsed every pattern split into one string, and with keep_delim
// reassembled the input.
List* pattern_split(TypePattern* pattern, Item source, bool keep_delim) {
    RootFrame roots(2);
    Rooted<Item> rooted_source(roots, source);
    Rooted<List*> rooted_result(roots, (List*)NULL);
    List* result = list();
    rooted_result.set(result);
    if (!pattern || !source.get_chars()) return rooted_result.get();
    size_t len = source.get_len();

    re2::RE2* re = pattern_get_unanchored(pattern);
    if (!re) return rooted_result.get();

    if (len == 0) {
        // S17.1.1: an empty subject yields [] when the delimiter matches the
        // empty string and [""] otherwise — the delimiter "consumes" the whole
        // (empty) subject in the first case but not the second.
        re2::StringPiece probe(source.get_chars(), (size_t)0);
        re2::StringPiece hit;
        if (re->Match(probe, 0, 0, re2::RE2::UNANCHORED, &hit, 1)) {
            return rooted_result.get();
        }
        String* only = make_heap_rooted_slice(rooted_source, 0, 0);
        array_push((Array*)rooted_result.get(), {.item = s2it(only)});
        return rooted_result.get();
    }

    // `seg_start` opens the pending segment; `search` is where the next match
    // is looked for. They only differ after a zero-length match, which must
    // advance the search alone — one shared cursor stepped the segment start
    // over the character at the match position and dropped it from the output
    // entirely (split("ab", \(d*)) returned three empty strings, no "a"/"b").
    size_t seg_start = 0;
    size_t search = 0;

    // S17.1.1 follows ECMAScript here: the bound is `search < len`, not `<= len`.
    // The trailing segment is always pushed after the loop, so searching at
    // `len` would let a zero-width match there emit one segment too many.
    while (search < len) {
        // Each preceding result allocation can move the source string.
        const char* str = rooted_source.get().get_chars();
        re2::StringPiece input(str, len);
        re2::StringPiece match;
        if (!re->Match(input, search, len, re2::RE2::UNANCHORED, &match, 1)) {
            break;
        }

        size_t match_start = match.data() - str;
        size_t match_len_val = match.size();
        size_t match_end = match_start + match_len_val;

        if (match_end == seg_start) {
            // ECMAScript's `e == p` rule: a match ending where the pending
            // segment starts contributes nothing and only advances the search.
            // This is the whole reason JS reports ["a","b"] for
            // "ab".split(/\d*/) where Python reports ['','a','b',''] — it
            // suppresses the leading and trailing empties alike. Step a whole
            // codepoint so the next slice stays on a character boundary.
            search = pattern_next_match_pos(rooted_source.get().get_chars(),
                len, match_start, 0);
            continue;
        }

        // push the part before the match
        size_t part_len = match_start - seg_start;
        String* part = make_heap_rooted_slice(rooted_source, seg_start, part_len);
        array_push((Array*)rooted_result.get(), {.item = s2it(part)});

        // optionally push the delimiter
        if (keep_delim && match_len_val > 0) {
            String* delim = make_heap_rooted_slice(rooted_source, match_start, match_len_val);
            array_push((Array*)rooted_result.get(), {.item = s2it(delim)});
        }

        // A zero-length match here sits past seg_start, so it yields a real
        // segment; the next pass sees match_end == seg_start and advances.
        seg_start = match_end;
        search = match_end;
    }

    // push remaining part after last match
    if (seg_start <= len) {
        size_t part_len = len - seg_start;
        String* part = make_heap_rooted_slice(rooted_source, seg_start, part_len);
        array_push((Array*)rooted_result.get(), {.item = s2it(part)});
    }

    return rooted_result.get();
}

// C-linkage wrapper for create_match_map, callable from lambda-eval.cpp
extern "C" Map* create_match_map_ext(const char* match_str, size_t match_len, int64_t index) {
    return create_match_map(match_str, match_len, index);
}

#endif // SIMPLE_SCHEMA_PARSER
