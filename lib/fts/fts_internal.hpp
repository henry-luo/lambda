// fts_internal.hpp — structures shared by the lib/fts translation units.
// Not part of the public API (lib/fts/fts.h).
#pragma once

#include "fts.h"

// ── tokens (FTX3, FTX4) ────────────────────────────────────────────────

struct FtsToken {
    size_t start, length;     // bytes in the tokenised text
    size_t chars;             // code points before the token, counted as str_utf8_count does
    size_t norm, norm_length; // the normalised form, in FtsTokenizer.norm
    bool stop;                // a stop word: holds its position, matches nothing
};

struct FtsTokenizer {
    bool fold;                // S17.7.1 simple case folding
    bool unaccent;            // NFD, drop Mn, NFC
    FtsToken* tokens;
    size_t count, cap;
    char* norm;               // every token's normalised form, back to back
    size_t norm_length, norm_cap;
    int32_t* runes;           // unaccent scratch
    size_t runes_cap;
};

void fts_tokenizer_init(FtsTokenizer* t, bool fold, bool unaccent);
void fts_tokenizer_release(FtsTokenizer* t);
// Replaces the tokenizer's tokens with those of `text`; false on memory.
bool fts_tokenize(FtsTokenizer* t, const char* text, size_t length);
// The canonical member of r's simple case folding orbit (S17.7.1), lowered.
uint32_t fts_fold_rune(uint32_t r);

// ── queries (FTX6, FTX13) ──────────────────────────────────────────────

enum FtsAtomKind {
    FTS_ATOM_EXACT = 0,       // the whole token
    FTS_ATOM_PREFIX,          // pre*
    FTS_ATOM_SUFFIX,          // *suf
    FTS_ATOM_CONTAINS,        // *mid*, or a bare term under word: false
};

// one token-level test; leaves share atoms that are equal
struct FtsAtom {
    char* text;               // normalised (mem-owned)
    size_t length;
    FtsAtomKind kind;
};

#define FTS_SLOT_ANY (-1)     // a stop word inside a phrase: any token

// a term (one slot) or a phrase (consecutive slots)
struct FtsLeaf {
    int* slots;               // atom ids, or FTS_SLOT_ANY (mem-owned)
    int count;
    int item;                 // index among the scored (positive) leaves, or -1 under a not
};

enum FtsNodeKind { FTS_NODE_AND, FTS_NODE_OR, FTS_NODE_NOT, FTS_NODE_LEAF };

struct FtsNode {
    FtsNodeKind kind;
    int* children;            // node ids (mem-owned); NOT has one
    int count;
    int leaf;                 // FTS_NODE_LEAF: the leaf id
};

struct FtsQuery {
    FtsOptions options;       // stopwords not kept: see stop
    FtsAtom* atoms;
    int atom_count, atom_cap;
    FtsLeaf* leaves;
    int leaf_count, leaf_cap;
    FtsNode* nodes;
    int node_count, node_cap;
    int root;                 // -1: the query kept no term
    int item_count;           // scored leaves
    char** stop;              // normalised stop words (mem-owned)
    size_t* stop_length;
    int stop_count;
    GrepMatcher* prefilter;   // NULL when no literal is necessary (§7.2)
};

// Per-document evaluation scratch, one per worker.
struct FtsEval {
    uint32_t** positions;     // per atom: token positions in the document, ascending
    size_t* count;
    size_t* cap;
    int atom_cap;
    uint32_t* tf;             // per leaf
    int leaf_cap;
};

void fts_eval_init(FtsEval* e);
void fts_eval_release(FtsEval* e);
// Marks stop words among t's tokens [first, last).
void fts_mark_stop(const FtsQuery* q, FtsTokenizer* t, size_t first, size_t last);
// Evaluates the query on tokens [first, last) of `t` as one document: fills
// e->tf per leaf (a phrase counts its occurrences); returns whether the
// document matches. false with *oom set on memory failure.
bool fts_eval_document(const FtsQuery* q, const FtsTokenizer* t, size_t first, size_t last,
                       FtsEval* e, bool* oom);
