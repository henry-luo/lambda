// fts.h — full-text search over files without an index
// (vibe/Lambda_IO_Fulltext_Search.md, FTX#).
//
// A query is a web-search string (FTX6): words, "phrases", `or`, `-` for
// not, `pre*` / `*suf` / `*mid*` part-of-token terms (FTX13) and grouping.
// Documents are files, paragraphs or lines (FTX2); a document is a set of
// tokens (FTX3) normalised by simple case folding (FTX4). Every search scans
// its sources: the walk and its thread pool are lib/grep's (FTX10), files
// with none of the query's literals are skipped by a lib/grep prefilter
// (§7.2), and BM25 statistics are exact over the documents searched (FTX8).
//
// The API is C-callable and exposes no vendor type (GRP3). Threading: an
// FtsQuery is immutable after creation and may be shared; an FtsSearch
// belongs to one thread, and runs its own workers inside fts_search_add.

#ifndef LIB_FTS_H
#define LIB_FTS_H

#include "../grep/grep.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FtsStatus {
    FTS_OK = 0,
    FTS_ERR_MEMORY,
    FTS_ERR_IO,              // a root does not exist
    FTS_ERR_ARGUMENT,
    FTS_ERR_LANGUAGE,        // no stemmer for the language (FTX5)
} FtsStatus;

typedef enum FtsUnit {
    FTS_UNIT_FILE = 0,
    FTS_UNIT_PARAGRAPH,      // a maximal run of lines that are not blank
    FTS_UNIT_LINE,
} FtsUnit;

typedef enum FtsRank {
    FTS_RANK_BM25 = 0,       // FTX7
    FTS_RANK_TF,             // BM25 with idf = 1: no corpus statistics (FTX9)
    FTS_RANK_NONE,           // path order, no score
} FtsRank;

typedef struct FtsOptions {
    bool ignore_case;        // fold tokens by S17.7.1 simple case folding (FTX4)
    bool word;               // a bare term matches whole tokens; false: any part (FTX13)
    bool unaccent;           // drop combining marks (NFD, drop Mn, NFC) before folding
    const char* language;    // stemming (FTX5); NULL for none
    const char* const* stopwords;   // dropped from documents and queries, keeping positions
    size_t stopword_count;
    FtsUnit unit;
    FtsRank rank;
    bool binary;             // search files with a NUL in their first 8 KiB
    uint64_t file_cap;       // FTS_RANK_NONE only: stop evaluating a file after this many
                             // matching documents (FTX9; 0 = none)
} FtsOptions;

typedef struct FtsQuery FtsQuery;

// Compiles a query; never fails on its syntax (FTX6). A query with no term
// matches nothing. error_buf (if given) says why on failure.
FtsStatus fts_query_create(const char* text, size_t length, const FtsOptions* options,
                           FtsQuery** out, char* error_buf, size_t error_buf_len);
void fts_query_destroy(FtsQuery* query);
// True when the query kept no term: it matches nothing.
bool fts_query_is_empty(const FtsQuery* query);

// A document that matched.
typedef struct FtsHit {
    const char* path;        // the file as reported; valid until fts_search_destroy
    size_t root_index;       // root_base plus the root's index in its fts_search_add call
    double score;            // 0 under FTS_RANK_NONE
    uint64_t byte_offset;    // the document's span in its file, without a final terminator
    uint64_t byte_length;
    uint64_t char_offset;    // code points from the start of the file (S17.4.1)
    uint64_t line;           // 1-based line the document starts on
    GrepLineEnding line_ending;  // the terminator after the document's last line (GRP31)
} FtsHit;

typedef struct FtsSearch FtsSearch;

FtsSearch* fts_search_create(const FtsQuery* query);
void fts_search_destroy(FtsSearch* search);
// Walks `paths` (as grep_walk_paths does) and evaluates the query on every
// selected file; may be called once per source, statistics accumulate.
FtsStatus fts_search_add(FtsSearch* search, const char* const* paths, size_t count, size_t root_base,
                         const GrepWalkOptions* walk);
// Scores and orders the matching documents: by score, best first, then path
// order and position; under FTS_RANK_NONE by path order and position. At most
// `limit_per_file` per file (the best of each) and `limit` in all (0 = none).
FtsStatus fts_search_finish(FtsSearch* search, uint64_t limit, uint64_t limit_per_file,
                            const FtsHit** hits, size_t* count);

// A span of a document's text that satisfied a positive term.
typedef struct FtsSpan {
    size_t byte_offset;      // in the text given
    size_t byte_length;
    size_t char_offset;      // code points from the start of the text given
} FtsSpan;

typedef bool (*FtsSpanFn)(void* user_data, const FtsSpan* span);

// Calls `fn` for every token occurrence in `text` (one document) that
// satisfied a positive term — each token of a phrase occurrence — in order.
FtsStatus fts_document_matches(const FtsQuery* query, const char* text, size_t length,
                               FtsSpanFn fn, void* user_data);

// The window of about `tokens` tokens of `text` that holds the most distinct
// positive terms: its byte span, and whether text was cut before or after it.
FtsStatus fts_document_snippet(const FtsQuery* query, const char* text, size_t length, int tokens,
                               size_t* start, size_t* end, bool* cut_before, bool* cut_after);

#ifdef __cplusplus
}
#endif

#endif
