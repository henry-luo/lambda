// grep.h — line-oriented text search on RE2 (vibe/Lambda_Lib_Grep.md).
//
// The library finds matches inside lines: a match never spans a line
// terminator (GRP1). A line ends at "\n" or "\r\n"; the "\r" of a pair belongs
// to the terminator, never to the line (GRP18). Results are matches (GRP4):
// one record per match, with byte offset always and code-point offset, line
// number and line text on request — or, counting, one number per input (GRP30).
//
// The API is C-callable and exposes no vendor type (GRP3): patterns go in as
// regex text (RE2 syntax), and RE2 could be replaced behind this header.
//
// Threading: a GrepMatcher is immutable after creation and may be shared. A
// GrepSearcher (and a GrepStream over it) belongs to one thread at a time.
// grep_search_paths() runs its own worker pool; see its comment for which
// thread calls the sink.

#ifndef LIB_GREP_H
#define LIB_GREP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GrepMatcher GrepMatcher;
typedef struct GrepSearcher GrepSearcher;
typedef struct GrepStream GrepStream;

typedef enum GrepStatus {
    GREP_OK = 0,
    GREP_ERR_SYNTAX,         // the pattern does not parse
    GREP_ERR_UNSUPPORTED,    // backreference or lookaround: the caller falls back (GRP12)
    GREP_ERR_LINE_TOO_LONG,  // a streamed line exceeded max_line_bytes
    GREP_ERR_IO,             // a path does not exist or cannot be read
    GREP_ERR_MEMORY,
    GREP_ERR_ARGUMENT,
} GrepStatus;

typedef struct GrepOptions {
    bool ignore_case;        // Unicode simple case folding (S17.7.1)
    bool fixed_string;       // patterns are literal text
    bool word;               // a match must not touch a letter, digit or '_' on either side
    bool whole_line;         // a pattern must match an entire line (GRP23)
    bool invert;             // report lines with no match instead of matches (GRP24)
    bool count_lines;        // count the selected lines instead of reporting them, as grep -c
                             //   (GRP30): no matched or context calls; file_done gets the count
    bool line_numbers;       // fill GrepMatch.line_number (1-based, as grep -n)
    bool char_offsets;       // fill GrepMatch.char_offset (code points, 0-based)
    bool line_text;          // fill GrepMatch.line / line_length (GRP20)
    bool binary_as_text;     // search inputs with a NUL in their first 8 KiB instead of skipping them
    int before_context;      // context lines reported before each selected line (GRP24)
    int after_context;       // ... and after it
    uint64_t max_matches_per_file;  // 0 = unlimited (GRP25); counts lines under invert or count_lines
    size_t max_line_bytes;   // streams only; 0 = 64 MiB
} GrepOptions;

// How a line ended (GRP31). The "\r" of a "\r\n" belongs to the terminator.
typedef enum GrepLineEnding {
    GREP_EOL_NONE = 0,       // the last line of an input, unterminated
    GREP_EOL_LF,             // "\n"
    GREP_EOL_CRLF,           // "\r\n"
} GrepLineEnding;

typedef struct GrepMatch {
    const char* path;        // NULL for buffer input unless a label was given
    const char* text;        // the matched bytes (the whole line under invert or for a
    size_t length;           //   context line); valid only during the callback
    uint64_t byte_offset;    // of text, from the start of the input, 0-based
    uint64_t char_offset;    // the same position in code points; 0 unless char_offsets
    uint64_t line_number;    // 1-based; 0 unless line_numbers
    const char* line;        // the enclosing line without its terminator; NULL unless
    size_t line_length;      //   line_text (always set for invert and context records)
    GrepLineEnding line_ending;  // the enclosing line's terminator (GRP31)
} GrepMatch;

typedef enum GrepAction {
    GREP_CONTINUE = 0,
    GREP_SKIP_FILE,          // stop searching the current input, go on to the next (GRP21)
    GREP_STOP,               // stop the whole search
} GrepAction;

typedef struct GrepSink {
    void* user_data;
    GrepAction (*matched)(void* user_data, const GrepMatch* match);
    // optional: a context line (GRP24); never a selected line
    GrepAction (*context)(void* user_data, const GrepMatch* line);
    // optional: an input was searched to its end or skipped by the sink (GRP21);
    // not called for inputs skipped as binary or failing with an error.
    // match_count is the selected records reported, or under count_lines the
    // selected lines (cut by a total limit as records would be)
    GrepAction (*file_done)(void* user_data, const char* path, uint64_t match_count);
} GrepSink;

// Compile one or more patterns; several patterns match any of them (GRP5).
// On failure *out is NULL and error_buf (if given) says why.
GrepStatus grep_matcher_create(const char* const* patterns, const size_t* lengths, size_t count,
                               const GrepOptions* options, GrepMatcher** out,
                               char* error_buf, size_t error_buf_len);
void grep_matcher_destroy(GrepMatcher* matcher);
const GrepOptions* grep_matcher_options(const GrepMatcher* matcher);

// Searchers carry per-thread state, including their own compiled RE2 (GRP6).
GrepSearcher* grep_searcher_create(const GrepMatcher* matcher);
void grep_searcher_destroy(GrepSearcher* searcher);

// Search one input held in memory. `label` is reported as GrepMatch.path.
GrepStatus grep_search_buffer(GrepSearcher* searcher, const char* label,
                              const char* data, size_t length, const GrepSink* sink);
// Search one file, streamed in chunks (§7.1).
GrepStatus grep_search_file(GrepSearcher* searcher, const char* path, const GrepSink* sink);

// Push interface over one input: results do not depend on how it is chunked.
GrepStream* grep_stream_open(GrepSearcher* searcher, const char* label, const GrepSink* sink);
GrepStatus grep_stream_feed(GrepStream* stream, const char* data, size_t length);
// searches the final unterminated line, reports file_done, then frees the stream
GrepStatus grep_stream_finish(GrepStream* stream);

// True when the last search on this searcher ended because the sink returned
// GREP_STOP (or a total limit was reached).
bool grep_searcher_stopped(const GrepSearcher* searcher);

typedef struct GrepWalkOptions {
    bool hidden;             // search hidden entries (GRP14 layer 2 off)
    bool no_ignore;          // ignore files and the built-in directory list off
    const char* const* include_globs;   // GRP22: a file must match one, if any are given
    size_t include_count;
    const char* const* exclude_globs;   // a file or directory matching one is skipped
    size_t exclude_count;
    int max_depth;           // < 0 unlimited; the root's entries are at depth 1
    uint64_t max_file_size;  // bytes; 0 unlimited
    uint64_t max_matches_total;  // 0 unlimited (GRP25)
    bool sorted;             // report files in path order (deterministic, GRP25)
    int threads;             // <= 0 picks one per core, at most 8
} GrepWalkOptions;

// Search files and directories. A directory is walked with the ignore layers
// of GRP14, the caller filters of GRP22 and without following symlinks
// (GRP19); a path given here is searched even if those rules would skip it.
// A root that does not exist fails the call with GREP_ERR_IO; an unreadable
// entry met during the walk is logged and skipped.
//
// With `sorted` (or one thread), every sink callback runs on the calling
// thread, after the files are searched, in path order. Otherwise callbacks run
// on worker threads, one at a time, as files finish.
GrepStatus grep_search_paths(const GrepMatcher* matcher, const char* const* paths, size_t count,
                             const GrepWalkOptions* walk, const GrepSink* sink);

// The walk alone, shared with other per-file consumers such as lib/fts
// (FTX10): the same roots, ignore layers (GRP14), filters (GRP22), symlink rule
// (GRP19) and thread pool as grep_search_paths, which is built on it. `visit`
// runs on a worker thread for each selected file, with `slot` in
// [0, slot_count) held by no other visit while it runs, so per-slot state
// (a searcher, a tokeniser) needs no lock. `begin`, when given, runs once on
// the calling thread before any visit, with the slot count. A visit returning
// GREP_STOP ends the walk; files not yet visited are skipped. The options'
// max_matches_total and sorted are the consumer's, not the walk's.
typedef struct GrepWalkVisitor {
    void* user_data;
    bool (*begin)(void* user_data, int slot_count);
    GrepAction (*visit)(void* user_data, int slot, const char* open_path, const char* label,
                        size_t root_index);
} GrepWalkVisitor;

GrepStatus grep_walk_paths(const char* const* paths, size_t count, const GrepWalkOptions* walk,
                           const GrepWalkVisitor* visitor);

// The path order sorted results use: component by component, '/' before any
// other byte, so a directory's entries stay together (GRP25).
int grep_path_order(const char* a, const char* b);

// True when `data` is binary by the walk's rule: a NUL in its first 8 KiB.
bool grep_input_is_binary(const char* data, size_t length);

// Gitignore-style glob match (also used for include/exclude globs): `*` and
// `?` stop at '/', `**` spans directories, `[...]` classes, `\` escapes.
bool grep_glob_match(const char* pattern, size_t pattern_len, const char* text, size_t text_len);

#ifdef __cplusplus
}
#endif

#endif
