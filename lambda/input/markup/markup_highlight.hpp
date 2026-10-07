/**
 * markup_highlight.hpp - windowed Markdown parse for source highlighting
 *
 * parse(lines, {type: 'markdown', sourcepos: 'spans', window: [first, last],
 * prescan: …}) parses only a window of a large document and reports where
 * every construct lies, for the source editor's highlighter
 * (vibe/radiant/Radiant_Design_Source_Editor.md CED15v2, CED16v3, CED17, CED18v2).
 *
 * The parse runs on a private pool that is released before returning, so a
 * parse per frame accumulates nothing; the caller copies the spans out.
 * Positions are 0-based source lines and code-point columns.
 */
#ifndef MARKUP_HIGHLIGHT_HPP
#define MARKUP_HIGHLIGHT_HPP

#include <stddef.h>
#include <stdint.h>
#include "../../../lib/arraylist.hpp"

namespace lambda {
namespace markup {

class MarkupParser;

// The document's lines, read on demand (the editor's chunked buffer is never
// flattened). `chunk_starts` are the first line of each chunk; the restart
// cache keeps one state per chunk boundary.
struct HighlightLines {
    void* ctx;
    const char* (*line)(void* ctx, int64_t index, size_t* len);
    int64_t count;
    const int64_t* chunk_starts;
    int64_t chunk_count;
};

// The parser state a window parse needs from before its first line: inside a
// fenced code block (fence char and length), inside an HTML block, or inside
// YAML front matter. Three ints per chunk boundary in the cache.
struct RestartState {
    int32_t kind;   // RESTART_NONE / _FENCE / _HTML / _FRONT_MATTER
    int32_t a;      // fence: the fence character; html: the block kind
    int32_t b;      // fence: the fence length
};
enum : int32_t { RESTART_NONE = 0, RESTART_FENCE = 1, RESTART_HTML = 2, RESTART_FRONT_MATTER = 3 };

// A span kind is the construct's tag name, copied: tag strings live in the
// parse's private pool, which is gone before the caller reads the result.
struct SpanKindName { char name[24]; };

struct MarkupSpan {
    int32_t kind;   // index into the result's kinds
    int32_t block;  // 1 for a block, 0 for an inline construct
    int32_t line, col, end_line, end_col;   // code-point columns; end exclusive
};

struct HighlightResult {
    lam::ArrayList<SpanKindName> kinds;     // distinct span kinds (tag names)
    lam::ArrayList<MarkupSpan> spans;
    lam::ArrayList<RestartState> states;    // one per chunk boundary scanned
    int64_t restart_line = 0;               // where the window parse began
};

// Collects spans while a MarkupParser runs in highlight mode. Positions are
// gathered in parser coordinates (line index, byte column) and converted to
// code points once, when the result is built.
struct MarkupSpanSink {
    int64_t line_base = 0;          // source line of parser line 0
    char** root_lines = nullptr;    // the window's own line array
    lam::ArrayList<SpanKindName> kinds;
    lam::ArrayList<MarkupSpan> spans;   // parser lines and byte columns until finished

    // The inline text being parsed maps to source through segments recorded
    // by the block that collected it (a paragraph joins lines, a heading
    // drops its markers). Offsets are relative to that root text.
    struct Segment { int64_t text_off; int32_t line; int32_t col; };
    lam::ArrayList<Segment> segments;
    bool segments_valid = false;

    // Containers (list items, block quotes) parse stripped copies of their
    // lines: the column map of CED17. Copy line i is parent line
    // `parent_first + i` from byte `offset[i]`; -1 when the copy is not a
    // suffix of its parent line (a tab was expanded), so positions on it
    // cannot be mapped and its inline spans fall back to none (OQ10).
    struct LineMap { char** lines; char** parent; int64_t parent_first; int64_t count; int32_t* offset; };
    lam::ArrayList<LineMap> line_maps;  // innermost last

    // Nested inline parses copy exact substrings; each call knows where its
    // buffer starts within the root text (-1: unknown, record nothing).
    const char* inline_base = nullptr;
    int64_t inline_base_off = -1;
    int64_t inline_next_off = -1;
    int inline_depth = 0;
};

// Record a block span in parser coordinates: lines [first, end) of the
// current line array.
void highlight_note_block(MarkupParser* parser, const char* kind, int first, int end);
// Around a container's parse of its stripped `lines`, which came from the
// current array starting at `parent_first`, one line each.
void highlight_push_lines(MarkupParser* parser, char** lines, size_t count, int64_t parent_first);
void highlight_pop_lines(MarkupParser* parser, char** lines);
// Start a root inline text: the block that built it adds its segments.
void highlight_begin_inline(MarkupParser* parser);
void highlight_add_segment(MarkupParser* parser, int64_t text_off, int line, int col);
void highlight_end_inline(MarkupParser* parser);
// Record an inline construct spanning [start, end) of the current inline buffer.
void highlight_note_inline(MarkupParser* parser, const char* start, const char* end,
                           const char* kind);
// Before a nested parse_inline_spans over a copy of [content, …) of the current buffer.
void highlight_set_child_origin(MarkupParser* parser, const char* content);
// The tag name of an inline parse result, or null for plain text.
const char* highlight_item_kind(uint64_t item);

// Scope for one parse_inline_spans call: adopts the origin its caller set.
struct InlineOriginScope {
    MarkupSpanSink* sink;
    const char* saved_base;
    int64_t saved_off;
    bool entered;
    InlineOriginScope(MarkupParser* parser);
    void enter(const char* buffer);     // the call's own copy of its text
    ~InlineOriginScope();
};

// Parse the window [first, last] of `src` (Markdown). `cache` is the restart
// state per chunk boundary from an earlier call; entries at index >= `valid`
// are suspect and recomputed. Returns false on allocation failure.
bool markdown_highlight_window(const HighlightLines* src, int64_t first, int64_t last,
                               const RestartState* cache, int64_t cache_count, int64_t valid,
                               HighlightResult* out);

// The same contract for HTML: spans from the tokenizer in lexical mode
// (html5_lex_spans), restarting at any line that begins in the data state.
bool html_highlight_window(const HighlightLines* src, int64_t first, int64_t last,
                           const RestartState* cache, int64_t cache_count, int64_t valid,
                           HighlightResult* out);

} // namespace markup
} // namespace lambda

#endif // MARKUP_HIGHLIGHT_HPP
