// fts_query.cpp — the query language (FTX6, FTX13), its evaluation on one
// document, and the literal prefilter (§7.2).
//
// The grammar is PostgreSQL's websearch_to_tsquery plus FTS5's prefix and
// grouping: words (implicit and), "phrases", `or`, `-` before a word, phrase
// or group, `(` `)`, and `*` at either end of a word. It never fails: an
// unbalanced quote closes at the end, a stray `)` is skipped, and a word that
// yields no token is dropped. A word's text is tokenised as documents are, so
// a word that becomes several tokens is a phrase.

#include "fts_internal.hpp"
#include "../mem_grow.h"
#include "../str.h"
#include "../log.h"
#include "../hash.h"

#include <stdio.h>
#include <string.h>

// ── building ───────────────────────────────────────────────────────────

static int add_atom(FtsQuery* q, const char* text, size_t length, FtsAtomKind kind) {
    for (int i = 0; i < q->atom_count; i++) {
        const FtsAtom* a = &q->atoms[i];
        if (a->kind == kind && a->length == length && memcmp(a->text, text, length) == 0) return i;
    }
    if (!mem_grow_array_raw_int((void**)&q->atoms, sizeof(FtsAtom), &q->atom_cap, q->atom_count + 1, 8, MEM_CAT_TEMP)) return -1;
    char* copy = mem_dup_n(text, length, MEM_CAT_TEMP);
    if (!copy) return -1;
    FtsAtom* a = &q->atoms[q->atom_count];
    a->text = copy;
    a->length = length;
    a->kind = kind;
    return q->atom_count++;
}

static int add_leaf(FtsQuery* q, int* slots, int count) {
    if (!mem_grow_array_raw_int((void**)&q->leaves, sizeof(FtsLeaf), &q->leaf_cap, q->leaf_count + 1, 8, MEM_CAT_TEMP)) {
        mem_free(slots);
        return -1;
    }
    FtsLeaf* l = &q->leaves[q->leaf_count];
    l->slots = slots;
    l->count = count;
    l->item = -1;
    return q->leaf_count++;
}

// children are taken over (mem-owned); a node with none is not built
static int add_node(FtsQuery* q, FtsNodeKind kind, int* children, int count, int leaf) {
    if (!mem_grow_array_raw_int((void**)&q->nodes, sizeof(FtsNode), &q->node_cap, q->node_count + 1, 8, MEM_CAT_TEMP)) {
        if (children) mem_free(children);
        return -1;
    }
    FtsNode* n = &q->nodes[q->node_count];
    n->kind = kind;
    n->children = children;
    n->count = count;
    n->leaf = leaf;
    return q->node_count++;
}

static bool is_stop(const FtsQuery* q, const char* text, size_t length) {
    for (int i = 0; i < q->stop_count; i++) {
        if (q->stop_length[i] == length && memcmp(q->stop[i], text, length) == 0) return true;
    }
    return false;
}

// a growable list of ints
struct IntList {
    int* data;
    int count, cap;
};

static bool int_push(IntList* l, int v) {
    if (!mem_grow_array_raw_int((void**)&l->data, sizeof(int), &l->cap, l->count + 1, 4, MEM_CAT_TEMP)) return false;
    l->data[l->count++] = v;
    return true;
}

static void int_release(IntList* l) {
    if (l->data) mem_free(l->data);
    memset(l, 0, sizeof(*l));
}

// ── parsing ────────────────────────────────────────────────────────────

struct Parser {
    FtsQuery* q;
    FtsTokenizer tok;
    const char* s;
    size_t len;
    size_t pos;
    bool oom;
};

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
static bool is_word_end(char c) { return is_space(c) || c == '(' || c == ')' || c == '"'; }

static void skip_space(Parser* p) {
    while (p->pos < p->len && is_space(p->s[p->pos])) p->pos++;
}

// Appends the slots of one word (leading and trailing `*` mark its first and
// last token) to `slots`. Stop words hold a position.
static bool word_slots(Parser* p, const char* w, size_t n, IntList* slots) {
    bool lead = false, trail = false;
    while (n && *w == '*') { lead = true; w++; n--; }
    while (n && w[n - 1] == '*') { trail = true; n--; }
    if (!n) return true;
    if (!fts_tokenize(&p->tok, w, n)) return false;
    FtsQuery* q = p->q;
    for (size_t i = 0; i < p->tok.count; i++) {
        const FtsToken* t = &p->tok.tokens[i];
        const char* text = fts_token_text(&p->tok, t);
        bool first = i == 0, last = i + 1 == p->tok.count;
        bool suffix = first && lead, prefix = last && trail;
        FtsAtomKind kind = suffix && prefix ? FTS_ATOM_CONTAINS
                         : suffix ? FTS_ATOM_SUFFIX
                         : prefix ? FTS_ATOM_PREFIX
                         : q->options.word ? FTS_ATOM_EXACT : FTS_ATOM_CONTAINS;
        // a stop word is a position, never a match; a `*` term is never one
        if (!suffix && !prefix && is_stop(q, text, t->norm_length)) {
            if (!int_push(slots, FTS_SLOT_ANY)) return false;
            continue;
        }
        int atom = add_atom(q, text, t->norm_length, kind);
        if (atom < 0 || !int_push(slots, atom)) return false;
    }
    return true;
}

// a leaf node from slots, any-slots at either end trimmed; -1 when none is left
static int leaf_node(Parser* p, IntList* slots) {
    int a = 0, b = slots->count;
    while (a < b && slots->data[a] == FTS_SLOT_ANY) a++;
    while (b > a && slots->data[b - 1] == FTS_SLOT_ANY) b--;
    if (a == b) {
        int_release(slots);
        return -1;
    }
    int* own = (int*)mem_alloc((size_t)(b - a) * sizeof(int), MEM_CAT_TEMP);
    if (!own) {
        p->oom = true;
        int_release(slots);
        return -1;
    }
    memcpy(own, slots->data + a, (size_t)(b - a) * sizeof(int));
    int_release(slots);
    int leaf = add_leaf(p->q, own, b - a);
    int node = leaf >= 0 ? add_node(p->q, FTS_NODE_LEAF, NULL, 0, leaf) : -1;
    if (node < 0) p->oom = true;
    return node;
}

static int parse_or(Parser* p, int depth);

static int parse_primary(Parser* p, int depth) {
    char c = p->s[p->pos];
    if (c == '(') {
        p->pos++;
        int node = depth < 64 ? parse_or(p, depth + 1) : -1;
        skip_space(p);
        if (p->pos < p->len && p->s[p->pos] == ')') p->pos++;
        return node;
    }
    IntList slots = {};
    if (c == '"') {
        // a phrase: its words in order, each may carry `*` (FTX13)
        size_t start = ++p->pos;
        while (p->pos < p->len && p->s[p->pos] != '"') p->pos++;
        size_t end = p->pos;
        if (p->pos < p->len) p->pos++;
        size_t i = start;
        while (i < end) {
            while (i < end && is_space(p->s[i])) i++;
            size_t w = i;
            while (i < end && !is_space(p->s[i])) i++;
            if (i > w && !word_slots(p, p->s + w, i - w, &slots)) {
                p->oom = true;
                int_release(&slots);
                return -1;
            }
        }
        return leaf_node(p, &slots);
    }
    size_t w = p->pos;
    while (p->pos < p->len && !is_word_end(p->s[p->pos])) p->pos++;
    if (!word_slots(p, p->s + w, p->pos - w, &slots)) {
        p->oom = true;
        int_release(&slots);
        return -1;
    }
    return leaf_node(p, &slots);
}

static bool at_or(Parser* p) {
    if (p->pos + 2 > p->len) return false;
    const char* s = p->s + p->pos;
    if ((s[0] != 'o' && s[0] != 'O') || (s[1] != 'r' && s[1] != 'R')) return false;
    return p->pos + 2 == p->len || is_word_end(s[2]);
}

static int finish_list(Parser* p, FtsNodeKind kind, IntList* items) {
    if (items->count == 0) {
        int_release(items);
        return -1;
    }
    if (items->count == 1) {
        int only = items->data[0];
        int_release(items);
        return only;
    }
    int node = add_node(p->q, kind, items->data, items->count, -1);
    if (node < 0) p->oom = true;
    memset(items, 0, sizeof(*items));
    return node;
}

static int parse_and(Parser* p, int depth) {
    IntList items = {};
    for (;;) {
        skip_space(p);
        // a stray `)` at the top level is skipped; in a group it closes it
        if (depth == 0 && p->pos < p->len && p->s[p->pos] == ')') {
            p->pos++;
            continue;
        }
        if (p->pos >= p->len || p->s[p->pos] == ')' || at_or(p) || p->oom) break;
        bool negate = false;
        if (p->s[p->pos] == '-') {
            // `-` before a word, phrase or group negates it; alone it is nothing
            p->pos++;
            if (p->pos >= p->len || is_space(p->s[p->pos]) || p->s[p->pos] == ')') continue;
            negate = true;
        }
        int node = parse_primary(p, depth);
        if (node >= 0 && negate) {
            int* child = (int*)mem_alloc(sizeof(int), MEM_CAT_TEMP);
            if (!child) {
                p->oom = true;
                break;
            }
            child[0] = node;
            node = add_node(p->q, FTS_NODE_NOT, child, 1, -1);
            if (node < 0) p->oom = true;
        }
        if (node >= 0 && !int_push(&items, node)) p->oom = true;
    }
    return finish_list(p, FTS_NODE_AND, &items);
}

static int parse_or(Parser* p, int depth) {
    IntList items = {};
    for (;;) {
        int node = parse_and(p, depth);
        if (node >= 0 && !int_push(&items, node)) p->oom = true;
        skip_space(p);
        if (p->pos < p->len && at_or(p) && !p->oom) {
            p->pos += 2;
            continue;
        }
        break;
    }
    return finish_list(p, FTS_NODE_OR, &items);
}

// ── evaluation ─────────────────────────────────────────────────────────

void fts_eval_init(FtsEval* e) { memset(e, 0, sizeof(*e)); }

void fts_eval_release(FtsEval* e) {
    for (int i = 0; i < e->atom_cap; i++) {
        if (e->positions[i]) mem_free(e->positions[i]);
    }
    if (e->positions) mem_free(e->positions);
    if (e->count) mem_free(e->count);
    if (e->cap) mem_free(e->cap);
    if (e->tf) mem_free(e->tf);
    memset(e, 0, sizeof(*e));
}

static bool eval_reserve(FtsEval* e, int atoms, int leaves) {
    if (atoms > e->atom_cap) {
        uint32_t** pos = (uint32_t**)mem_realloc(e->positions, (size_t)atoms * sizeof(uint32_t*), MEM_CAT_TEMP);
        if (!pos) return false;
        e->positions = pos;
        size_t* count = (size_t*)mem_realloc(e->count, (size_t)atoms * sizeof(size_t), MEM_CAT_TEMP);
        if (!count) return false;
        e->count = count;
        size_t* cap = (size_t*)mem_realloc(e->cap, (size_t)atoms * sizeof(size_t), MEM_CAT_TEMP);
        if (!cap) return false;
        e->cap = cap;
        for (int i = e->atom_cap; i < atoms; i++) {
            e->positions[i] = NULL;
            e->count[i] = e->cap[i] = 0;
        }
        e->atom_cap = atoms;
    }
    if (leaves > e->leaf_cap) {
        uint32_t* tf = (uint32_t*)mem_realloc(e->tf, (size_t)leaves * sizeof(uint32_t), MEM_CAT_TEMP);
        if (!tf) return false;
        e->tf = tf;
        e->leaf_cap = leaves;
    }
    return true;
}

static bool atom_matches(const FtsAtom* a, const char* text, size_t length) {
    switch (a->kind) {
    case FTS_ATOM_EXACT:
        return length == a->length && memcmp(text, a->text, length) == 0;
    case FTS_ATOM_PREFIX:
        return length >= a->length && memcmp(text, a->text, a->length) == 0;
    case FTS_ATOM_SUFFIX:
        return length >= a->length && memcmp(text + length - a->length, a->text, a->length) == 0;
    case FTS_ATOM_CONTAINS:
        return length >= a->length && str_find(text, length, a->text, a->length) != STR_NPOS;
    }
    return false;
}

static bool has_position(const FtsEval* e, int atom, uint32_t pos) {
    const uint32_t* v = e->positions[atom];
    size_t lo = 0, hi = e->count[atom];
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (v[mid] < pos) lo = mid + 1;
        else hi = mid;
    }
    return lo < e->count[atom] && v[lo] == pos;
}

// whether a leaf (a term, or a phrase from its head) occurs at position p
static bool occurs_at(const FtsLeaf* l, const FtsEval* e, uint32_t p, uint32_t ntokens) {
    if (p + (uint32_t)l->count > ntokens) return false;
    for (int i = 1; i < l->count; i++) {
        int s = l->slots[i];
        if (s != FTS_SLOT_ANY && !has_position(e, s, p + (uint32_t)i)) return false;
    }
    return true;
}

// occurrences of a leaf: a term's positions, or a phrase's start positions
static uint32_t leaf_tf(const FtsLeaf* l, const FtsEval* e, uint32_t ntokens) {
    int head = l->slots[0];  // never FTS_SLOT_ANY: phrases are trimmed
    if (l->count == 1) return (uint32_t)e->count[head];
    uint32_t tf = 0;
    for (size_t k = 0; k < e->count[head]; k++) {
        if (occurs_at(l, e, e->positions[head][k], ntokens)) tf++;
    }
    return tf;
}

static bool eval_node(const FtsQuery* q, int id, const uint32_t* tf) {
    const FtsNode* n = &q->nodes[id];
    switch (n->kind) {
    case FTS_NODE_LEAF: return tf[n->leaf] > 0;
    case FTS_NODE_NOT: return !eval_node(q, n->children[0], tf);
    case FTS_NODE_AND:
        for (int i = 0; i < n->count; i++) {
            if (!eval_node(q, n->children[i], tf)) return false;
        }
        return true;
    case FTS_NODE_OR:
        for (int i = 0; i < n->count; i++) {
            if (eval_node(q, n->children[i], tf)) return true;
        }
        return false;
    }
    return false;
}

void fts_mark_stop(const FtsQuery* q, FtsTokenizer* t, size_t first, size_t last) {
    if (!q->stop_count) return;
    for (size_t i = first; i < last; i++) {
        FtsToken* tok = &t->tokens[i];
        tok->stop = is_stop(q, fts_token_text(t, tok), tok->norm_length);
    }
}

static inline uint64_t length_bit(size_t n) { return (uint64_t)1 << (n < 63 ? n : 63); }

// the exact atom equal to a token's text, or -1
static int exact_atom(const FtsQuery* q, const char* text, size_t length) {
    if (!(q->exact_lengths & length_bit(length))) return -1;
    for (uint32_t h = hash_djb2(text, length) & q->exact_mask;; h = (h + 1) & q->exact_mask) {
        int a = q->exact_table[h];
        if (a < 0) return -1;
        if (q->atoms[a].length == length && memcmp(q->atoms[a].text, text, length) == 0) return a;
    }
}

static bool push_position(FtsEval* e, int a, uint32_t pos) {
    if (!mem_grow_array_raw((void**)&e->positions[a], sizeof(uint32_t), &e->cap[a], e->count[a] + 1, 8, MEM_CAT_TEMP)) return false;
    e->positions[a][e->count[a]++] = pos;
    return true;
}

// positions of every atom among tokens [first, last), numbered from `base`:
// exact atoms by one hash probe (most tokens fail the length filter before
// it), the rest by a scan
static bool collect_positions(const FtsQuery* q, const FtsTokenizer* t, size_t first, size_t last,
                              uint32_t base, FtsEval* e) {
    for (size_t i = first; i < last; i++) {
        const FtsToken* tok = &t->tokens[i];
        if (tok->stop) continue;
        const char* text = fts_token_text(t, tok);
        uint32_t pos = base + (uint32_t)(i - first);
        if (q->exact_count) {
            int a = exact_atom(q, text, tok->norm_length);
            if (a >= 0 && !push_position(e, a, pos)) return false;
        }
        for (int k = 0; k < q->affix_count; k++) {
            int a = q->affix[k];
            if (atom_matches(&q->atoms[a], text, tok->norm_length) && !push_position(e, a, pos)) return false;
        }
    }
    return true;
}

// the exact-atom hash table and the affix list (atoms are unique by text and kind)
static bool build_atom_index(FtsQuery* q) {
    for (int a = 0; a < q->atom_count; a++) q->exact_count += q->atoms[a].kind == FTS_ATOM_EXACT;
    uint32_t size = 8;
    while (size < 2 * (uint32_t)q->exact_count) size *= 2;
    q->exact_table = (int*)mem_alloc(size * sizeof(int), MEM_CAT_TEMP);
    q->affix = (int*)mem_alloc((size_t)(q->atom_count ? q->atom_count : 1) * sizeof(int), MEM_CAT_TEMP);
    if (!q->exact_table || !q->affix) return false;
    q->exact_mask = size - 1;
    for (uint32_t i = 0; i < size; i++) q->exact_table[i] = -1;
    for (int a = 0; a < q->atom_count; a++) {
        const FtsAtom* atom = &q->atoms[a];
        if (atom->kind != FTS_ATOM_EXACT) {
            q->affix[q->affix_count++] = a;
            continue;
        }
        q->exact_lengths |= length_bit(atom->length);
        uint32_t h = hash_djb2(atom->text, atom->length) & q->exact_mask;
        while (q->exact_table[h] >= 0) h = (h + 1) & q->exact_mask;
        q->exact_table[h] = a;
    }
    return true;
}

bool fts_eval_begin(const FtsQuery* q, FtsEval* e) {
    if (!eval_reserve(e, q->atom_count, q->leaf_count)) return false;
    for (int a = 0; a < q->atom_count; a++) e->count[a] = 0;
    return true;
}

bool fts_eval_add(const FtsQuery* q, const FtsTokenizer* t, size_t first, size_t last, uint32_t base,
                  FtsEval* e) {
    return collect_positions(q, t, first, last, base, e);
}

bool fts_eval_finish(const FtsQuery* q, FtsEval* e, uint32_t ntokens) {
    if (q->root < 0) return false;
    for (int l = 0; l < q->leaf_count; l++) e->tf[l] = leaf_tf(&q->leaves[l], e, ntokens);
    return eval_node(q, q->root, e->tf);
}

bool fts_eval_document(const FtsQuery* q, const FtsTokenizer* t, size_t first, size_t last,
                       FtsEval* e, bool* oom) {
    *oom = false;
    if (q->root < 0) return false;
    if (!fts_eval_begin(q, e) || !fts_eval_add(q, t, first, last, 0, e)) {
        *oom = true;
        return false;
    }
    return fts_eval_finish(q, e, (uint32_t)(last - first));
}

// ── compiling ──────────────────────────────────────────────────────────

// scored leaves are those under no `not`
static void number_items(FtsQuery* q, int id, bool negated) {
    const FtsNode* n = &q->nodes[id];
    if (n->kind == FTS_NODE_LEAF) {
        if (!negated && q->leaves[n->leaf].item < 0) q->leaves[n->leaf].item = q->item_count++;
        return;
    }
    for (int i = 0; i < n->count; i++) number_items(q, n->children[i], negated || n->kind == FTS_NODE_NOT);
}

// A file can be skipped when it contains none of the leaves' literals, if the
// query is false with every leaf absent; each leaf's literal is its longest
// atom, which every match of the leaf contains (§7.2). Unaccented and stemmed
// tokens need not occur in the file's bytes, so they get no prefilter.
static bool build_prefilter(FtsQuery* q, char* error_buf, size_t error_buf_len) {
    q->prefilter = NULL;
    if (q->root < 0 || q->options.unaccent || q->options.language) return true;
    bool ok = true;
    uint32_t* none = (uint32_t*)mem_calloc((size_t)(q->leaf_count ? q->leaf_count : 1), sizeof(uint32_t), MEM_CAT_TEMP);
    if (!none) return false;
    bool matches_empty = eval_node(q, q->root, none);
    mem_free(none);
    if (matches_empty) return true;
    const char** pats = (const char**)mem_calloc((size_t)q->leaf_count, sizeof(char*), MEM_CAT_TEMP);
    size_t* lens = (size_t*)mem_calloc((size_t)q->leaf_count, sizeof(size_t), MEM_CAT_TEMP);
    size_t n = 0;
    ok = pats && lens;
    for (int l = 0; ok && l < q->leaf_count; l++) {
        const FtsLeaf* leaf = &q->leaves[l];
        const FtsAtom* best = NULL;
        for (int i = 0; i < leaf->count; i++) {
            if (leaf->slots[i] == FTS_SLOT_ANY) continue;
            const FtsAtom* a = &q->atoms[leaf->slots[i]];
            if (!best || a->length > best->length) best = a;
        }
        if (!best || best->length == 0) {
            n = 0;
            break;
        }
        bool dup = false;
        for (size_t k = 0; k < n && !dup; k++) dup = lens[k] == best->length && memcmp(pats[k], best->text, best->length) == 0;
        if (!dup) {
            pats[n] = best->text;
            lens[n] = best->length;
            n++;
        }
    }
    if (ok && n) {
        GrepOptions go;
        memset(&go, 0, sizeof(go));
        go.fixed_string = true;
        go.ignore_case = q->options.ignore_case;
        go.binary_as_text = true;  // the binary rule is applied before the prefilter
        if (grep_matcher_create(pats, lens, n, &go, &q->prefilter, error_buf, error_buf_len) != GREP_OK) {
            // a literal set RE2 cannot take only costs speed: scan every file
            log_error("fts: prefilter not built: %s", error_buf ? error_buf : "");
            q->prefilter = NULL;
        }
    }
    if (pats) mem_free(pats);
    if (lens) mem_free(lens);
    return ok;
}

bool fts_query_is_empty(const FtsQuery* q) { return !q || q->root < 0; }

void fts_query_destroy(FtsQuery* q) {
    if (!q) return;
    for (int i = 0; i < q->atom_count; i++) mem_free(q->atoms[i].text);
    for (int i = 0; i < q->leaf_count; i++) mem_free(q->leaves[i].slots);
    for (int i = 0; i < q->node_count; i++) {
        if (q->nodes[i].children) mem_free(q->nodes[i].children);
    }
    for (int i = 0; i < q->stop_count; i++) mem_free(q->stop[i]);
    if (q->atoms) mem_free(q->atoms);
    if (q->leaves) mem_free(q->leaves);
    if (q->nodes) mem_free(q->nodes);
    if (q->stop) mem_free(q->stop);
    if (q->stop_length) mem_free(q->stop_length);
    if (q->prefilter) grep_matcher_destroy(q->prefilter);
    if (q->exact_table) mem_free(q->exact_table);
    if (q->affix) mem_free(q->affix);
    if (q->options.language) mem_free((void*)q->options.language);
    mem_free(q);
}

static bool add_stopwords(FtsQuery* q, FtsTokenizer* tok, const char* const* words, size_t count) {
    for (size_t w = 0; w < count; w++) {
        if (!words[w] || !fts_tokenize(tok, words[w], strlen(words[w]))) {
            if (words[w]) return false;
            continue;
        }
        for (size_t i = 0; i < tok->count; i++) {
            const FtsToken* t = &tok->tokens[i];
            const char* text = fts_token_text(tok, t);
            if (is_stop(q, text, t->norm_length)) continue;
            char** stop = (char**)mem_realloc(q->stop, (size_t)(q->stop_count + 1) * sizeof(char*), MEM_CAT_TEMP);
            if (!stop) return false;
            q->stop = stop;
            size_t* lens = (size_t*)mem_realloc(q->stop_length, (size_t)(q->stop_count + 1) * sizeof(size_t), MEM_CAT_TEMP);
            if (!lens) return false;
            q->stop_length = lens;
            char* copy = mem_dup_n(text, t->norm_length, MEM_CAT_TEMP);
            if (!copy) return false;
            q->stop[q->stop_count] = copy;
            q->stop_length[q->stop_count++] = t->norm_length;
        }
    }
    return true;
}

FtsStatus fts_query_create(const char* text, size_t length, const FtsOptions* options,
                           FtsQuery** out, char* error_buf, size_t error_buf_len) {
    *out = NULL;
    if (error_buf && error_buf_len) error_buf[0] = '\0';
    if (!options || (!text && length)) return FTS_ERR_ARGUMENT;
    if (options->language && options->language[0]) {
        // FTX5: English (Porter2) is the engine's one stemmer; it is not built yet
        if (error_buf) snprintf(error_buf, error_buf_len, "no stemmer for language '%s'", options->language);
        return FTS_ERR_LANGUAGE;
    }
    FtsQuery* q = (FtsQuery*)mem_calloc(1, sizeof(FtsQuery), MEM_CAT_TEMP);
    if (!q) return FTS_ERR_MEMORY;
    q->options = *options;
    q->options.stopwords = NULL;
    q->options.stopword_count = 0;
    q->options.language = NULL;
    q->root = -1;

    Parser p;
    memset(&p, 0, sizeof(p));
    p.q = q;
    p.s = text;
    p.len = length;
    fts_tokenizer_init(&p.tok, options->ignore_case, options->unaccent);
    bool ok = add_stopwords(q, &p.tok, options->stopwords, options->stopword_count);
    if (ok && length) {
        q->root = parse_or(&p, 0);
        ok = !p.oom;
    }
    fts_tokenizer_release(&p.tok);
    if (ok && q->root >= 0) number_items(q, q->root, false);
    if (ok) ok = build_atom_index(q);
    if (ok) ok = build_prefilter(q, error_buf, error_buf_len);
    if (!ok) {
        fts_query_destroy(q);
        return FTS_ERR_MEMORY;
    }
    *out = q;
    return FTS_OK;
}

// ── a returned document's extras ───────────────────────────────────────

// For every token position of [0, ntokens): the scored leaf it took part in,
// or -1. A phrase occurrence marks each of its positions.
static bool mark_hits(const FtsQuery* q, const FtsEval* e, uint32_t ntokens, int* mark) {
    for (uint32_t i = 0; i < ntokens; i++) mark[i] = -1;
    for (int l = 0; l < q->leaf_count; l++) {
        const FtsLeaf* leaf = &q->leaves[l];
        if (leaf->item < 0) continue;
        int head = leaf->slots[0];
        for (size_t k = 0; k < e->count[head]; k++) {
            uint32_t p = e->positions[head][k];
            if (!occurs_at(leaf, e, p, ntokens)) continue;
            for (int i = 0; i < leaf->count; i++) {
                if (mark[p + i] < 0) mark[p + i] = leaf->item;
            }
        }
    }
    return true;
}

// tokenises and evaluates one document's text for its extras
struct DocScan {
    FtsTokenizer tok;
    FtsEval eval;
    int* mark;
};

static FtsStatus scan_document(const FtsQuery* q, const char* text, size_t length, DocScan* d) {
    fts_tokenizer_init(&d->tok, q->options.ignore_case, q->options.unaccent);
    fts_eval_init(&d->eval);
    d->mark = NULL;
    if (!fts_tokenize(&d->tok, text, length)) return FTS_ERR_MEMORY;
    fts_mark_stop(q, &d->tok, 0, d->tok.count);
    bool oom = false;
    if (q->root >= 0) fts_eval_document(q, &d->tok, 0, d->tok.count, &d->eval, &oom);
    if (oom) return FTS_ERR_MEMORY;
    d->mark = (int*)mem_alloc((d->tok.count ? d->tok.count : 1) * sizeof(int), MEM_CAT_TEMP);
    if (!d->mark) return FTS_ERR_MEMORY;
    if (q->root >= 0) mark_hits(q, &d->eval, (uint32_t)d->tok.count, d->mark);
    else for (size_t i = 0; i < d->tok.count; i++) d->mark[i] = -1;
    return FTS_OK;
}

static void scan_release(DocScan* d) {
    fts_tokenizer_release(&d->tok);
    fts_eval_release(&d->eval);
    if (d->mark) mem_free(d->mark);
}

FtsStatus fts_document_matches(const FtsQuery* q, const char* text, size_t length,
                               FtsSpanFn fn, void* user_data) {
    if (!q || !fn || (!text && length)) return FTS_ERR_ARGUMENT;
    DocScan d;
    FtsStatus st = scan_document(q, text, length, &d);
    for (size_t i = 0; st == FTS_OK && i < d.tok.count; i++) {
        if (d.mark[i] < 0) continue;
        const FtsToken* t = &d.tok.tokens[i];
        FtsSpan span = {t->start, t->length, t->chars};
        if (!fn(user_data, &span)) break;
    }
    scan_release(&d);
    return st;
}

FtsStatus fts_document_snippet(const FtsQuery* q, const char* text, size_t length, int tokens,
                               size_t* start, size_t* end, bool* cut_before, bool* cut_after) {
    if (!q || (!text && length) || tokens <= 0) return FTS_ERR_ARGUMENT;
    *start = *end = 0;
    *cut_before = *cut_after = false;
    DocScan d;
    FtsStatus st = scan_document(q, text, length, &d);
    size_t n = d.tok.count;
    if (st == FTS_OK && n) {
        size_t w = (size_t)tokens < n ? (size_t)tokens : n;
        size_t best = 0;
        if (w < n) {
            // slide the window: the most distinct scored leaves, then the most
            // marked tokens, the earliest window on a tie
            int* seen = (int*)mem_calloc((size_t)(q->item_count ? q->item_count : 1), sizeof(int), MEM_CAT_TEMP);
            if (!seen) {
                st = FTS_ERR_MEMORY;
            } else {
                int distinct = 0, marked = 0, best_distinct = -1, best_marked = -1;
                for (size_t i = 0; i < n; i++) {
                    if (d.mark[i] >= 0) {
                        marked++;
                        if (seen[d.mark[i]]++ == 0) distinct++;
                    }
                    if (i >= w) {
                        int out = d.mark[i - w];
                        if (out >= 0) {
                            marked--;
                            if (--seen[out] == 0) distinct--;
                        }
                    }
                    if (i + 1 >= w && (distinct > best_distinct || (distinct == best_distinct && marked > best_marked))) {
                        best_distinct = distinct;
                        best_marked = marked;
                        best = i + 1 - w;
                    }
                }
                mem_free(seen);
            }
        }
        if (st == FTS_OK) {
            const FtsToken* a = &d.tok.tokens[best];
            const FtsToken* b = &d.tok.tokens[best + w - 1];
            *start = a->start;
            *end = b->start + b->length;
            *cut_before = best > 0;
            *cut_after = best + w < n;
        }
    }
    scan_release(&d);
    return st;
}
