/**
 * markup_highlight.cpp - windowed Markdown parse for source highlighting
 * (vibe/radiant/Radiant_Design_Source_Editor.md CED15, CED16v2, CED17).
 *
 * Three parts:
 *  - the restart scan: the state before each chunk boundary (inside a fence,
 *    an HTML block or front matter), cached by the caller between calls, and
 *    the safe line to start a window parse from;
 *  - the span sink: block and inline parsers report where each construct
 *    lies while a MarkupParser runs with `span_sink` set;
 *  - the window parse itself, on a private pool released before returning.
 */
#include "markup_highlight.hpp"
#include "markup_parser.hpp"
#include "block/block_common.hpp"
#include "../input.hpp"
#include "../../io/input-allocation-context.h"
#include "../../../lib/mem_factory.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/log.h"
#include <string.h>

namespace lambda {
namespace markup {

// Lines after `last` the window parse also reads: a setext underline or a
// table delimiter row decides what the line above it is.
static const int64_t HIGHLIGHT_LOOKAHEAD = 4;

// ============================================================================
// Restart scan
// ============================================================================

static bool line_blank(const char* s) {
    for (; *s; s++) {
        if (*s != ' ' && *s != '\t' && *s != '\r') return false;
    }
    return true;
}

static int line_indent(const char* s) {
    int n = 0;
    while (s[n] == ' ') n++;
    return n;
}

// YAML front matter opens with `---` on the first line and closes with `---`
// or `...`; the markup engine leaves it to its callers, the walker colors it.
static bool front_matter_delimiter(const char* s, bool closing) {
    bool dashes = strncmp(s, "---", 3) == 0;
    bool dots = closing && strncmp(s, "...", 3) == 0;
    return (dashes || dots) && line_blank(s + 3);
}

// One line through the restart state machine, with the parser's own fence and
// HTML-block rules (block_code.cpp, block_html.cpp).
static void restart_step(RestartState* st, const char* line, int64_t index) {
    switch (st->kind) {
    case RESTART_FENCE:
        if (is_code_fence_close(line, (char)st->a, st->b)) *st = RestartState{};
        return;
    case RESTART_FRONT_MATTER:
        if (front_matter_delimiter(line, true)) *st = RestartState{};
        return;
    case RESTART_HTML:
        // types 6 and 7 end before a blank line; 1-5 at their end marker
        if (st->a >= (int32_t)HtmlBlockType::TYPE_6) {
            if (line_blank(line)) *st = RestartState{};
        } else if (check_html_block_end(line, (HtmlBlockType)st->a, false)) {
            *st = RestartState{};
        }
        return;
    default:
        break;
    }
    if (index == 0 && front_matter_delimiter(line, false)) {
        *st = RestartState{RESTART_FRONT_MATTER, 0, 0};
        return;
    }
    if (line_blank(line) || line_indent(line) >= 4) return;
    if (is_code_fence(line)) {
        char fence_char = 0;
        int fence_len = 0;
        get_fence_info(line, &fence_char, &fence_len);
        *st = RestartState{RESTART_FENCE, fence_char, fence_len};
        return;
    }
    HtmlBlockType html = detect_html_block_type(line);
    if (html == HtmlBlockType::NONE) return;
    if (html < HtmlBlockType::TYPE_6 && check_html_block_end(line, html, false)) return;
    *st = RestartState{RESTART_HTML, (int32_t)html, 0};
}

static bool restart_equal(const RestartState& a, const RestartState& b) {
    return a.kind == b.kind && a.a == b.a && a.b == b.b;
}

static const char* src_line(const HighlightLines* src, int64_t index) {
    size_t len = 0;
    const char* line = src->line(src->ctx, index, &len);
    return line ? line : "";
}

static int64_t chunk_end(const HighlightLines* src, int64_t k) {
    return k + 1 < src->chunk_count ? src->chunk_starts[k + 1] : src->count;
}

// The chunk holding `line`: the last chunk starting at or before it.
static int64_t chunk_index(const HighlightLines* src, int64_t line) {
    int64_t lo = 0, hi = src->chunk_count;
    while (lo < hi) {
        int64_t mid = (lo + hi) / 2;
        if (src->chunk_starts[mid] <= line) lo = mid + 1; else hi = mid;
    }
    return lo > 0 ? lo - 1 : 0;
}

// Extend `states` (a valid prefix) through boundary `target`. A recomputed
// suspect boundary equal to its cached state revalidates every later cached
// entry, since nothing before it changed the scan (CED16v2).
static void extend_states(const HighlightLines* src, const RestartState* cache,
                          int64_t cache_count, lam::ArrayList<RestartState>* states,
                          int64_t target) {
    while ((int64_t)states->length() <= target) {
        int64_t k = (int64_t)states->length() - 1;
        RestartState st = (*states)[(size_t)k];
        for (int64_t i = src->chunk_starts[k]; i < chunk_end(src, k); i++) {
            restart_step(&st, src_line(src, i), i);
        }
        states->push_back(st);
        int64_t boundary = k + 1;
        if (boundary < cache_count && restart_equal(st, cache[boundary])) {
            for (int64_t j = boundary + 1; j < cache_count && j < src->chunk_count; j++) {
                states->push_back(cache[j]);
            }
        }
    }
}

// The last line at or before `first` where a window parse may start: the
// first line of a block that follows a blank line, at column 0, outside any
// fence, HTML block or front matter. Line 0 always qualifies.
static int64_t restart_line(const HighlightLines* src,
                            const lam::ArrayList<RestartState>& states, int64_t first) {
    for (int64_t k = chunk_index(src, first); k >= 0; k--) {
        RestartState st = states[(size_t)k];
        bool prev_blank = false;
        int64_t found = -1;
        for (int64_t i = src->chunk_starts[k]; i <= first; i++) {
            const char* line = src_line(src, i);
            if (prev_blank && st.kind == RESTART_NONE && line[0] != ' ' && line[0] != '\t' &&
                !line_blank(line)) {
                found = i;
            }
            prev_blank = st.kind == RESTART_NONE && line_blank(line);
            restart_step(&st, line, i);
        }
        if (found >= 0) return found;
    }
    return 0;
}

// ============================================================================
// Span sink
// ============================================================================

static bool sink_active(MarkupParser* parser) {
    return parser && parser->span_sink && parser->lines == parser->span_sink->root_lines;
}

static int32_t sink_kind(MarkupSpanSink* sink, const char* kind) {
    for (size_t i = 0; i < sink->kinds.length(); i++) {
        if (strncmp(sink->kinds[i].name, kind, sizeof(SpanKindName::name) - 1) == 0) {
            return (int32_t)i; // INT_CAST_OK: a handful of distinct tag names
        }
    }
    SpanKindName name = {};
    strncpy(name.name, kind, sizeof(name.name) - 1);
    sink->kinds.push_back(name);
    return (int32_t)sink->kinds.length() - 1; // INT_CAST_OK: a handful of distinct tag names
}

void highlight_note_block(MarkupParser* parser, const char* kind, int first, int end) {
    if (!sink_active(parser) || !kind || end <= first) return;
    // trailing blank lines a block parser consumed separate blocks
    while (end > first + 1 && line_blank(parser->lines[end - 1])) end--;
    MarkupSpan span = {sink_kind(parser->span_sink, kind), 1, first, 0, end - 1,
                       (int32_t)strlen(parser->lines[end - 1])}; // INT_CAST_OK: line byte length
    parser->span_sink->spans.push_back(span);
}

void highlight_begin_inline(MarkupParser* parser) {
    if (!parser || !parser->span_sink) return;
    parser->span_sink->segments.clear();
    parser->span_sink->segments_valid = sink_active(parser);
}

void highlight_add_segment(MarkupParser* parser, int64_t text_off, int line, int col) {
    if (!parser || !parser->span_sink || !parser->span_sink->segments_valid) return;
    parser->span_sink->segments.push_back(MarkupSpanSink::Segment{text_off, line, col});
}

void highlight_end_inline(MarkupParser* parser) {
    if (!parser || !parser->span_sink) return;
    parser->span_sink->segments.clear();
    parser->span_sink->segments_valid = false;
}

// A root-text offset as a parser line and byte column.
static bool segment_position(MarkupSpanSink* sink, int64_t off, int32_t* line, int32_t* col) {
    const MarkupSpanSink::Segment* hit = nullptr;
    for (size_t i = 0; i < sink->segments.length(); i++) {
        if (sink->segments[i].text_off > off) break;
        hit = &sink->segments[i];
    }
    if (!hit) return false;
    *line = hit->line;
    *col = hit->col + (int32_t)(off - hit->text_off); // INT_CAST_OK: within one line
    return true;
}

void highlight_note_inline(MarkupParser* parser, const char* start, const char* end,
                           const char* kind) {
    MarkupSpanSink* sink = parser ? parser->span_sink : nullptr;
    if (!sink || !kind || sink->inline_base_off < 0 || !sink->segments_valid || end <= start) return;
    int64_t s = sink->inline_base_off + (start - sink->inline_base);
    int64_t e = sink->inline_base_off + (end - sink->inline_base);
    MarkupSpan span = {sink_kind(sink, kind), 0, 0, 0, 0, 0};
    if (!segment_position(sink, s, &span.line, &span.col) ||
        !segment_position(sink, e, &span.end_line, &span.end_col)) return;
    sink->spans.push_back(span);
}

void highlight_set_child_origin(MarkupParser* parser, const char* content) {
    MarkupSpanSink* sink = parser ? parser->span_sink : nullptr;
    if (!sink) return;
    sink->inline_next_off = sink->inline_base_off >= 0
        ? sink->inline_base_off + (content - sink->inline_base) : -1;
}

const char* highlight_item_kind(uint64_t item) {
    Item value = {.item = item};
    if (get_type_id(value) != LMD_TYPE_ELEMENT) return nullptr;
    Element* element = (Element*)item;
    return element->type ? ((TypeElmt*)element->type)->name.str : nullptr;
}

InlineOriginScope::InlineOriginScope(MarkupParser* parser)
    : sink(parser ? parser->span_sink : nullptr), saved_base(nullptr), saved_off(-1), entered(false) {
    if (sink) {
        saved_base = sink->inline_base;
        saved_off = sink->inline_base_off;
    }
}

void InlineOriginScope::enter(const char* buffer) {
    if (!sink) return;
    // the root call maps through the block's segments; a nested call adopts
    // the origin its caller set just before it
    int64_t off = sink->inline_depth == 0 ? (sink->segments_valid ? 0 : -1) : sink->inline_next_off;
    sink->inline_next_off = -1;
    sink->inline_base = buffer;
    sink->inline_base_off = off;
    sink->inline_depth++;
    entered = true;
}

InlineOriginScope::~InlineOriginScope() {
    if (!sink || !entered) return;
    sink->inline_depth--;
    sink->inline_base = saved_base;
    sink->inline_base_off = saved_off;
}

// ============================================================================
// Window parse
// ============================================================================

// Bytes to code points within one line.
static int32_t code_points(const char* line, int32_t bytes) {
    int32_t n = 0;
    for (int32_t i = 0; i < bytes && line[i]; i++) {
        if (((unsigned char)line[i] & 0xC0) != 0x80) n++;
    }
    return n;
}

bool markdown_highlight_window(const HighlightLines* src, int64_t first, int64_t last,
                               const RestartState* cache, int64_t cache_count, int64_t valid,
                               HighlightResult* out) {
    if (!src || !out || src->count <= 0 || src->chunk_count <= 0) return false;
    first = first < 0 ? 0 : (first >= src->count ? src->count - 1 : first);
    last = last < first ? first : (last >= src->count ? src->count - 1 : last);

    // the valid prefix of the cache, never less than boundary 0 (no state)
    if (valid > cache_count) valid = cache_count;
    out->states.clear();
    for (int64_t k = 0; k < valid; k++) out->states.push_back(cache[k]);
    if (out->states.length() == 0) out->states.push_back(RestartState{});
    extend_states(src, cache, cache_count, &out->states, chunk_index(src, first));
    int64_t start = restart_line(src, out->states, first);
    out->restart_line = start;

    int64_t stop = last + 1 + HIGHLIGHT_LOOKAHEAD;
    if (stop > src->count) stop = src->count;
    StrBuf* text = strbuf_new_cap(4096);
    if (!text) return false;
    for (int64_t i = start; i < stop; i++) {
        if (i > start) strbuf_append_char(text, '\n');
        strbuf_append_str(text, src_line(src, i));
    }

    Pool* pool = mem_pool_create(NULL, MEM_ROLE_INPUT, "markup.highlight");
    Input* input = pool ? Input::create(pool, nullptr, nullptr) : nullptr;
    if (!input) {
        if (pool) mem_pool_destroy(pool);
        strbuf_free(text);
        return false;
    }
    InputAllocationContext allocation = {pool, input->arena, false, input};
    InputAllocationContext* saved_allocation = input_allocation_context;
    input_allocation_context = &allocation;

    ParseConfig cfg;
    cfg.format = Format::MARKDOWN;
    cfg.collect_metadata = false;
    MarkupSpanSink sink;
    sink.line_base = start;
    MarkupParser* parser = markup_parser_create(input, cfg);
    if (parser) {
        parser->span_sink = &sink;
        parser->parseContent(text->str);
        markup_parser_destroy(parser);
    }
    input_allocation_context = saved_allocation;

    // parser lines and byte columns become source lines and code points
    out->kinds.clear();
    for (size_t i = 0; i < sink.kinds.length(); i++) out->kinds.push_back(sink.kinds[i]);
    out->spans.clear();
    for (size_t i = 0; i < sink.spans.length(); i++) {
        MarkupSpan span = sink.spans[i];
        int64_t line = start + span.line;
        int64_t end_line = start + span.end_line;
        if (line >= src->count || end_line >= src->count) continue;
        span.col = code_points(src_line(src, line), span.col);
        span.end_col = code_points(src_line(src, end_line), span.end_col);
        span.line = (int32_t)line;          // INT_CAST_OK: editor line counts fit int32
        span.end_line = (int32_t)end_line;  // INT_CAST_OK: editor line counts fit int32
        out->spans.push_back(span);
    }
    mem_pool_destroy(pool);
    strbuf_free(text);
    log_debug("markup-highlight: window %lld-%lld restart %lld, %zu spans",
              (long long)first, (long long)last, (long long)start, out->spans.length());
    return true;
}

} // namespace markup
} // namespace lambda
