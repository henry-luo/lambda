// Grammar engine: a PEG recursive-descent interpreter (GM1) with ordered
// choice (GM2), parser-driven lexing and implicit `space` (GM3), bounded left
// recursion with per-rule precedence levels (GM6–GM8), and furthest-failure
// error reporting (GM12).
//
// A parse first builds scratch nodes in an arena that dies with the parse, so
// a failed branch costs only arena bytes; the successful tree is then
// converted to Lambda elements in the caller's Input (GM4, D4.2.6).

#include "grammar.hpp"
#include "../parser/lambda_rd_parser.h"
#include "../re2_wrapper.hpp"
#include "../lambda-stack.h"
#include "../../io/mark_builder.hpp"
#include "../../../lib/arena.h"
#include "../../../lib/arraylist.h"
#include "../../../lib/hashmap.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/str.h"
#include "../../../lib/log.h"

#include <string.h>

namespace {

// the same nesting bound the Lambda parser uses (LAMBDA_RD_MAX_DEPTH)
const uint32_t GRAMMAR_MAX_DEPTH = 1000;
const uint32_t GRAMMAR_MAX_EXPECTED = 32;

enum : uint8_t { PN_ELEMENT, PN_TEXT };

// A scratch parse node. Immutable once pushed, so memoized results can be
// replayed; a label is applied to a copy.
struct PNode {
    uint8_t kind;
    bool built;
    uint32_t rule;
    uint32_t start, end;
    String* label;
    PNode** children;
    uint32_t count;
    Item item;
};

struct MemoRecord {
    bool done;          // false while a left-recursive seed is growing
    bool ok;
    size_t end;
    PNode* node;        // the rule's element node
};

struct MemoKey {
    uint32_t rule;
    uint32_t level;     // precedence level, with the nospace context in bit 31
    uint64_t pos;
    MemoRecord* record;
};

struct Expected {
    String* text;
    bool quote;         // a literal, shown in double quotes
};

struct ParseState {
    TypeGrammar* g;
    const char* src;
    size_t len;
    size_t pos;
    uint32_t depth;
    bool nospace;
    const char* abort_reason;   // depth or stack limit reached
    size_t abort_pos;           // where it was reached
    size_t furthest;
    Expected expected[GRAMMAR_MAX_EXPECTED];
    uint32_t expected_count;
    bool expect_end;            // "end of input" joins the expected set
    Arena* arena;
    ArrayList* stack;           // PNode* under construction
    struct hashmap* memo;
    bool spans;                 // GM11: elements carry start/end offsets
};

// the Lambda lexer's identifier bytes decide what a "word" is
bool word_char(char c) {
    return lambda_lexer_is_ident_continue_byte(c);
}

uint64_t memo_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const MemoKey* key = (const MemoKey*)item;
    uint64_t parts[2] = {((uint64_t)key->rule << 32) | key->level, key->pos};
    return hashmap_xxhash3(parts, sizeof(parts), seed0, seed1);
}

int memo_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const MemoKey* x = (const MemoKey*)a;
    const MemoKey* y = (const MemoKey*)b;
    if (x->rule != y->rule) return x->rule < y->rule ? -1 : 1;
    if (x->level != y->level) return x->level < y->level ? -1 : 1;
    if (x->pos != y->pos) return x->pos < y->pos ? -1 : 1;
    return 0;
}

MemoRecord* memo_find(ParseState* S, uint32_t rule, uint32_t level, size_t pos) {
    if (!S->memo) return NULL;
    MemoKey key = {rule, level | (S->nospace ? 0x80000000u : 0u), (uint64_t)pos, NULL};
    const MemoKey* found = (const MemoKey*)hashmap_get(S->memo, &key);
    return found ? found->record : NULL;
}

MemoRecord* memo_insert(ParseState* S, uint32_t rule, uint32_t level, size_t pos) {
    if (!S->memo) {
        S->memo = hashmap_new(sizeof(MemoKey), 64, 0, 0, memo_hash, memo_compare, NULL, NULL);
    }
    MemoRecord* record = (MemoRecord*)arena_calloc(S->arena, sizeof(MemoRecord));
    MemoKey key = {rule, level | (S->nospace ? 0x80000000u : 0u), (uint64_t)pos, record};
    hashmap_set(S->memo, &key);
    return record;
}

int stack_mark(ParseState* S) {
    return S->stack->length;
}

void stack_truncate(ParseState* S, int mark) {
    S->stack->length = mark;
}

void push(ParseState* S, PNode* node) {
    arraylist_append(S->stack, node);
}

PNode* new_text(ParseState* S, size_t start, size_t end, uint32_t rule) {
    PNode* node = (PNode*)arena_calloc(S->arena, sizeof(PNode));
    node->kind = PN_TEXT;
    node->rule = rule;
    node->start = (uint32_t)start;
    node->end = (uint32_t)end;
    return node;
}

size_t token_start(ParseState* S, size_t pos);

// Wrap everything pushed since `mark` into one element node for `rule`.
PNode* wrap(ParseState* S, uint32_t rule, int mark, size_t start) {
    PNode* node = (PNode*)arena_calloc(S->arena, sizeof(PNode));
    node->kind = PN_ELEMENT;
    node->rule = rule;
    node->start = (uint32_t)start;
    node->end = (uint32_t)S->pos;
    node->count = (uint32_t)(S->stack->length - mark);
    if (node->count) {
        node->children = (PNode**)arena_alloc(S->arena, sizeof(PNode*) * node->count);
        memcpy(node->children, &S->stack->data[mark], sizeof(PNode*) * node->count);
    }
    if (S->spans) node->start = (uint32_t)token_start(S, start);
    stack_truncate(S, mark);
    return node;
}

PNode* labeled(ParseState* S, PNode* node, String* label) {
    PNode* copy = (PNode*)arena_alloc(S->arena, sizeof(PNode));
    *copy = *node;
    copy->label = label;
    return copy;
}

void note_failure(ParseState* S, String* text, bool quote) {
    if (S->pos > S->furthest) {
        S->furthest = S->pos;
        S->expected_count = 0;
        S->expect_end = false;
    }
    if (S->pos != S->furthest) return;
    for (uint32_t i = 0; i < S->expected_count; i++) {
        if (S->expected[i].text == text) return;
    }
    if (S->expected_count < GRAMMAR_MAX_EXPECTED) {
        S->expected[S->expected_count++] = (Expected){text, quote};
    }
}

void skip_space(ParseState* S) {
    if (S->nospace || S->g->space_rule == GRAMMAR_NO_RULE) return;
    TypePattern* space = S->g->rules[S->g->space_rule]->pattern;
    for (;;) {
        size_t end = S->pos;
        if (!pattern_match_prefix_at(space, S->src, S->len, S->pos, &end) || end == S->pos) return;
        S->pos = end;
    }
}

// Where a rule entered at `pos` began its first token: a token skips `space`
// before it, and a hidden token leaves no node to take the position from.
size_t token_start(ParseState* S, size_t pos) {
    size_t saved = S->pos;
    S->pos = pos;
    skip_space(S);
    size_t start = S->pos < saved ? S->pos : saved;
    S->pos = saved;
    return start;
}

bool match_literal(ParseState* S, GrammarExpr* e) {
    if (!e->immediate) skip_space(S);
    size_t n = e->text->len;
    if (S->pos + n <= S->len && memcmp(S->src + S->pos, e->text->chars, n) == 0 &&
            !(e->word_boundary && S->pos + n < S->len && word_char(S->src[S->pos + n]))) {
        push(S, new_text(S, S->pos, S->pos + n, GRAMMAR_NO_RULE));
        S->pos += n;
        return true;
    }
    note_failure(S, e->text, true);
    return false;
}

// `immediate`: the reference was written `~name` (GM13), so no space first
bool match_token(ParseState* S, uint32_t index, bool emit, bool immediate) {
    GrammarRule* rule = S->g->rules[index];
    if (!rule->is_space && !immediate) skip_space(S);
    size_t end = S->pos;
    // a keyword token (`for := 'for'`) matches whole words, as a literal does
    if (rule->pattern && pattern_match_prefix_at(rule->pattern, S->src, S->len, S->pos, &end) &&
            !(rule->word_boundary && end < S->len && word_char(S->src[end]))) {
        if (emit) push(S, new_text(S, S->pos, end, index));
        S->pos = end;
        return true;
    }
    note_failure(S, rule->display ? rule->display : rule->name, false);
    return false;
}

bool limit_reached(ParseState* S) {
    if (S->abort_reason) return true;
    if (S->depth > GRAMMAR_MAX_DEPTH) {
        S->abort_reason = "maximum grammar nesting exceeded";
        S->abort_pos = S->pos;
        return true;
    }
    // the interpreter recurses natively; stop before the guard page
    if (lambda_stack_enabled() &&
            lambda_stack_pointer() < _lambda_stack_limit + LAMBDA_STACK_THROW_HEADROOM) {
        S->abort_reason = "grammar nesting exhausted the native stack";
        S->abort_pos = S->pos;
        return true;
    }
    return false;
}

bool parse_expr(ParseState* S, GrammarExpr* e);
bool parse_rule(ParseState* S, uint32_t index, uint32_t level, bool splice);

bool call_rule(ParseState* S, GrammarExpr* e, bool splice) {
    GrammarRule* rule = S->g->rules[e->rule_index];
    if (rule->is_token) return match_token(S, e->rule_index, !rule->hidden && !splice, e->immediate);
    uint32_t level = e->call_mode == GM_CALL_TRAILING ? e->prec : 0;
    return parse_rule(S, e->rule_index, level, splice || rule->hidden);
}

bool parse_lookahead(ParseState* S, GrammarExpr* e) {
    size_t pos = S->pos;
    int mark = stack_mark(S);
    // a lookahead's own failures are not what the input lacks
    size_t furthest = S->furthest;
    uint32_t expected_count = S->expected_count;
    bool expect_end = S->expect_end;
    Expected expected[GRAMMAR_MAX_EXPECTED];
    memcpy(expected, S->expected, sizeof(expected));
    bool ok = parse_expr(S, e->items[0]);
    S->pos = pos;
    stack_truncate(S, mark);
    S->furthest = furthest;
    S->expected_count = expected_count;
    S->expect_end = expect_end;
    memcpy(S->expected, expected, sizeof(expected));
    return e->kind == GX_AND ? ok : !ok;
}

bool parse_expr(ParseState* S, GrammarExpr* e) {
    if (S->abort_reason) return false;
    switch (e->kind) {
    case GX_LITERAL:
        return match_literal(S, e);
    case GX_RULE:
        return call_rule(S, e, false);
    case GX_SEQ: {
        size_t pos = S->pos;
        int mark = stack_mark(S);
        for (uint32_t i = 0; i < e->item_count; i++) {
            if (!parse_expr(S, e->items[i])) {
                S->pos = pos;
                stack_truncate(S, mark);
                return false;
            }
        }
        return true;
    }
    case GX_CHOICE: {
        size_t pos = S->pos;
        int mark = stack_mark(S);
        for (uint32_t i = 0; i < e->item_count; i++) {
            if (parse_expr(S, e->items[i])) return true;
            S->pos = pos;
            stack_truncate(S, mark);
            if (S->abort_reason) return false;
        }
        return false;
    }
    case GX_REPEAT: {
        size_t start = S->pos;
        int start_mark = stack_mark(S);
        uint32_t count = 0;
        while (e->max == 0 || count < e->max) {
            size_t pos = S->pos;
            int mark = stack_mark(S);
            if (!parse_expr(S, e->items[0])) {
                S->pos = pos;
                stack_truncate(S, mark);
                break;
            }
            count++;
            // an iteration that consumed nothing would repeat forever
            if (S->pos == pos) break;
        }
        if (count < e->min) {
            S->pos = start;
            stack_truncate(S, start_mark);
            return false;
        }
        return true;
    }
    case GX_AND:
    case GX_NOT:
        return parse_lookahead(S, e);
    case GX_PREC:
        return parse_expr(S, e->items[0]);
    case GX_LABEL: {
        int mark = stack_mark(S);
        if (!parse_expr(S, e->items[0])) return false;
        for (int i = mark; i < S->stack->length; i++) {
            S->stack->data[i] = labeled(S, (PNode*)S->stack->data[i], e->text);
        }
        return true;
    }
    case GX_HIDDEN:
        if (e->items[0]->kind == GX_RULE) return call_rule(S, e->items[0], true);
        return parse_expr(S, e->items[0]);
    }
    return false;
}

// One alternative that grows a left-recursive seed: its leading reference
// takes the seed, the rest parses normally (§5.2 step 3).
bool parse_growth(ParseState* S, GrammarExpr* alt, PNode* seed) {
    GrammarExpr* body = alt->items[0];
    GrammarExpr* lead = body->kind == GX_SEQ ? body->items[0] : body;
    String* label = NULL;
    while (lead->kind == GX_LABEL) {
        if (!label) label = lead->text;
        lead = lead->items[0];
    }
    push(S, label ? labeled(S, seed, label) : seed);
    if (body->kind != GX_SEQ) return true;
    for (uint32_t i = 1; i < body->item_count; i++) {
        if (!parse_expr(S, body->items[i])) return false;
    }
    return true;
}

// Bounded left recursion with precedence levels (Warth et al. 2008, in the
// precedence form of Medeiros et al. 2014): seed from the non-left-recursive
// alternatives, then grow with every left-recursive alternative admissible at
// `level`, restarting from the first after each growth.
bool parse_left_recursive(ParseState* S, uint32_t index, uint32_t level) {
    GrammarRule* rule = S->g->rules[index];
    size_t start = S->pos;
    MemoRecord* memo = memo_find(S, index, level, start);
    if (memo) {
        if (!memo->done || !memo->ok) return false;
        S->pos = memo->end;
        push(S, memo->node);
        return true;
    }
    memo = memo_insert(S, index, level, start);
    GrammarExpr* body = rule->body;
    PNode* seed = NULL;
    size_t seed_end = start;
    for (uint32_t i = 0; i < body->item_count && !seed; i++) {
        GrammarExpr* alt = body->items[i];
        if (alt->lr_alt) continue;
        S->pos = start;
        int mark = stack_mark(S);
        if (parse_expr(S, alt)) {
            seed = wrap(S, index, mark, start);
            seed_end = S->pos;
        } else {
            stack_truncate(S, mark);
            if (S->abort_reason) break;
        }
    }
    if (!seed) {
        memo->done = true;
        S->pos = start;
        return false;
    }
    memo->ok = true;
    memo->end = seed_end;
    memo->node = seed;
    // A step whose alternative ends in the rule itself has let its right
    // operand take every operator above its level, so later steps stay at or
    // below it: at its level after prec.left, below it after prec / prec.right.
    // Without the cap a non-associative `a < b < c` would chain at this level.
    int64_t cap = INT64_MAX;
    for (bool grew = true; grew && !S->abort_reason;) {
        grew = false;
        for (uint32_t i = 0; i < body->item_count; i++) {
            GrammarExpr* alt = body->items[i];
            if (!alt->lr_alt || alt->prec < level || (int64_t)alt->prec > cap) continue;
            S->pos = seed_end;
            int mark = stack_mark(S);
            if (parse_growth(S, alt, seed) && S->pos > seed_end) {
                seed = wrap(S, index, mark, start);
                seed_end = S->pos;
                memo->end = seed_end;
                memo->node = seed;
                if (alt->trailing_self) {
                    int64_t step = alt->assoc == GM_ASSOC_LEFT ? (int64_t)alt->prec
                        : (int64_t)alt->prec - 1;
                    if (step < cap) cap = step;
                }
                grew = true;
                break;
            }
            stack_truncate(S, mark);
        }
    }
    memo->done = true;
    S->pos = seed_end;
    push(S, seed);
    return true;
}

bool parse_plain(ParseState* S, uint32_t index) {
    size_t start = S->pos;
    int mark = stack_mark(S);
    if (!parse_expr(S, S->g->rules[index]->body)) return false;
    push(S, wrap(S, index, mark, start));
    return true;
}

bool parse_memo(ParseState* S, uint32_t index) {
    size_t start = S->pos;
    MemoRecord* memo = memo_find(S, index, 0, start);
    if (memo) {
        if (!memo->ok) return false;
        S->pos = memo->end;
        push(S, memo->node);
        return true;
    }
    memo = memo_insert(S, index, 0, start);
    memo->ok = parse_plain(S, index);
    memo->done = true;
    if (memo->ok) {
        memo->end = S->pos;
        memo->node = (PNode*)S->stack->data[S->stack->length - 1];
    }
    return memo->ok;
}

bool parse_rule(ParseState* S, uint32_t index, uint32_t level, bool splice) {
    GrammarRule* rule = S->g->rules[index];
    S->depth++;
    if (limit_reached(S)) {
        S->depth--;
        return false;
    }
    bool saved_nospace = S->nospace;
    // a space-free rule entered from spaced text skips the space before it
    if (rule->nospace && !S->nospace) {
        skip_space(S);
        S->nospace = true;
    }
    bool ok = rule->left_recursive ? parse_left_recursive(S, index, level)
        : rule->memo ? parse_memo(S, index) : parse_plain(S, index);
    S->nospace = saved_nospace;
    S->depth--;
    if (ok && splice) {
        // a hidden rule leaves its children, labels included, to the caller
        PNode* node = (PNode*)S->stack->data[--S->stack->length];
        for (uint32_t i = 0; i < node->count; i++) push(S, node->children[i]);
    }
    return ok;
}

bool run(ParseState* S, uint32_t entry) {
    GrammarRule* rule = S->g->rules[entry];
    bool ok = rule->is_token ? match_token(S, entry, true, false) : parse_rule(S, entry, 0, false);
    if (ok) {
        skip_space(S);
        if (S->pos != S->len) {
            if (S->pos >= S->furthest) {
                if (S->pos > S->furthest) S->expected_count = 0;
                S->furthest = S->pos;
                S->expect_end = true;
            }
            ok = false;
        }
    }
    return ok;
}

void append_location(StrBuf* out, const char* src, size_t pos) {
    uint32_t line = 1, column = 1;
    for (size_t i = 0; i < pos; i++) {
        if (src[i] == '\n') { line++; column = 1; }
        else if (((unsigned char)src[i] & 0xC0) != 0x80) column++;
    }
    strbuf_append_format(out, "%u:%u", line, column);
}

void append_found(StrBuf* out, ParseState* S) {
    if (S->furthest >= S->len) {
        strbuf_append_str(out, ", found end of input");
        return;
    }
    size_t n = 0;
    while (S->furthest + n < S->len && n < 12) {
        char c = S->src[S->furthest + n];
        if (c == '\n' || c == '\r' || ((c == ' ' || c == '\t') && n > 0)) break;
        n++;
    }
    strbuf_append_str(out, ", found \"");
    strbuf_append_str_n(out, S->src + S->furthest, n);
    strbuf_append_char(out, '"');
}

// `grammar 'calc': expected ")" or "+" at 1:7, found "]"` (GM12)
char* failure_message(ParseState* S, Pool* pool) {
    StrBuf* out = strbuf_new();
    strbuf_append_format(out, "grammar '%.*s': ", (int)S->g->name->len, S->g->name->chars);
    if (S->abort_reason) {
        strbuf_append_str(out, S->abort_reason);
        strbuf_append_str(out, " at ");
        append_location(out, S->src, S->abort_pos);
    } else {
        uint32_t total = S->expected_count + (S->expect_end ? 1 : 0);
        if (total) {
            strbuf_append_str(out, "expected ");
            for (uint32_t i = 0; i < total; i++) {
                if (i) strbuf_append_str(out, i + 1 == total ? " or " : ", ");
                if (i == S->expected_count) { strbuf_append_str(out, "end of input"); continue; }
                Expected* e = &S->expected[i];
                if (e->quote) strbuf_append_char(out, '"');
                strbuf_append_str_n(out, e->text->chars, e->text->len);
                if (e->quote) strbuf_append_char(out, '"');
            }
        } else {
            strbuf_append_str(out, "no match");
        }
        strbuf_append_str(out, " at ");
        append_location(out, S->src, S->furthest);
        append_found(out, S);
    }
    char* message = (char*)pool_calloc(pool, out->length + 1);
    if (message) memcpy(message, out->str, out->length);
    strbuf_free(out);
    return message;
}

// Scratch tree → Lambda elements, post-order with an explicit stack: a long
// left-associative chain grows a deep tree without deep parse recursion.
Item convert(ParseState* S, MarkBuilder& builder, PNode* root) {
    ArrayList* work = arraylist_new(64);
    arraylist_append(work, root);
    while (work->length) {
        PNode* node = (PNode*)work->data[work->length - 1];
        if (node->built) { work->length--; continue; }
        if (node->kind == PN_TEXT) {
            // GM4.2: a `'...'` token rule emits a symbol, everything else a string
            GrammarRule* rule = node->rule == GRAMMAR_NO_RULE ? NULL : S->g->rules[node->rule];
            const char* text = S->src + node->start;
            size_t len = node->end - node->start;
            node->item = rule && rule->pattern && rule->pattern->is_symbol
                ? (Item){.item = y2it(builder.createSymbol(text, len))}
                : builder.createStringItem(text, len);
            node->built = true;
            work->length--;
            continue;
        }
        bool ready = true;
        for (uint32_t i = 0; i < node->count; i++) {
            if (!node->children[i]->built) {
                arraylist_append(work, node->children[i]);
                ready = false;
            }
        }
        if (!ready) continue;
        ElementBuilder element = builder.element(S->g->rules[node->rule]->name->chars);
        for (uint32_t i = 0; i < node->count; i++) {
            PNode* child = node->children[i];
            if (!child->label) { element.child(child->item); continue; }
            // children sharing a label become one attribute: a value, or an array
            bool first = true, repeated = false;
            for (uint32_t k = 0; k < node->count; k++) {
                if (k == i || node->children[k]->label != child->label) continue;
                if (k < i) first = false;
                repeated = true;
            }
            if (!first) continue;
            if (!repeated) { element.attr(child->label, child->item); continue; }
            ArrayBuilder values = builder.array();
            for (uint32_t k = i; k < node->count; k++) {
                if (node->children[k]->label == child->label) values.append(node->children[k]->item);
            }
            element.attr(child->label, values.final());
        }
        if (S->spans) {
            element.attr("start", (int64_t)node->start);
            element.attr("end", (int64_t)node->end);
        }
        node->item = element.final();
        node->built = true;
        work->length--;
    }
    arraylist_free(work);
    return root->item;
}

void state_init(ParseState* S, TypeGrammar* grammar, const char* src, size_t len) {
    memset(S, 0, sizeof(*S));
    S->g = grammar;
    S->src = src ? src : "";
    S->len = src ? len : 0;
    S->arena = arena_create_default();
    S->stack = arraylist_new(64);
}

void state_release(ParseState* S) {
    if (S->memo) hashmap_free(S->memo);
    arraylist_free(S->stack);
    arena_destroy(S->arena);
}

}  // namespace

bool grammar_parse(TypeGrammar* grammar, uint32_t entry, Input* input,
        const char* src, size_t len, bool spans) {
    if (!grammar || !input || entry >= grammar->rule_count) return false;
    ParseState S;
    state_init(&S, grammar, src, len);
    S.spans = spans;
    bool ok = run(&S, entry);
    if (ok) {
        MarkBuilder builder(input);
        input->root = convert(&S, builder, (PNode*)S.stack->data[S.stack->length - 1]);
    } else {
        input->parse_failed = true;
        input->parse_error_message = failure_message(&S, input->pool);
        input->root = ItemError;
        log_debug("grammar_parse: %s", input->parse_error_message ? input->parse_error_message : "");
    }
    state_release(&S);
    return ok;
}

bool grammar_accepts(TypeGrammar* grammar, uint32_t entry, const char* src, size_t len) {
    if (!grammar || entry >= grammar->rule_count) return false;
    ParseState S;
    state_init(&S, grammar, src, len);
    bool ok = run(&S, entry);
    state_release(&S);
    return ok;
}
