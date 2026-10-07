/**
 * markup_highlight.cpp - windowed Markdown and HTML parse for source highlighting
 * (vibe/radiant/Radiant_Design_Source_Editor.md CED15v2, CED16v3, CED17, CED18v2).
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
#include "../html5/html5_parser.h"
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

static bool boundary_equal(const BoundaryState& a, const BoundaryState& b) {
    return a.restart.kind == b.restart.kind && a.restart.a == b.restart.a && a.restart.b == b.restart.b &&
           a.link == b.link && a.skip == b.skip;
}

// ----------------------------------------------------------------------------
// Link labels per chunk: the parser's own pre-scan (prescanLinkDefinitions)
// over one chunk at a time, resumed from the state at the chunk's boundary.
// ----------------------------------------------------------------------------

void HighlightLabels::add(const char* label, size_t len) {
    offsets.push_back((int64_t)bytes.length());
    for (size_t i = 0; i < len; i++) bytes.push_back(label[i]);
    bytes.push_back('\0');
}

void HighlightLabels::copy_chunk(const HighlightLabels& from, int64_t k) {
    for (int64_t i = from.first[(size_t)k]; i < from.first[(size_t)k + 1]; i++) {
        const char* label = from.label(i);
        add(label, strlen(label));
    }
    close_chunk();
}

static int32_t pack_link_state(const LinkPrescanState& st) {
    int32_t length = st.fence_length > 0x7FFF ? 0x7FFF : st.fence_length;
    return (st.in_fenced_code ? 1 : 0) | (st.in_paragraph ? 2 : 0) |
           ((int32_t)(unsigned char)st.fence_char << 8) | (length << 16);
}

static LinkPrescanState unpack_link_state(int32_t link, int32_t skip) {
    LinkPrescanState st;
    st.in_fenced_code = (link & 1) != 0;
    st.in_paragraph = (link & 2) != 0;
    st.fence_char = (char)((link >> 8) & 0xFF);
    st.fence_length = (link >> 16) & 0x7FFF;
    st.skip = skip;
    return st;
}

// A parser that only pre-scans, and the lines of the chunk it reads: the
// chunk and the next one, since a definition may run past the chunk's end.
struct LinkLabelScan {
    MarkupParser* parser = nullptr;
    lam::ArrayList<char*> lines;
};

static void collect_label(void* ctx, const char* label) {
    ((HighlightLabels*)ctx)->add(label, strlen(label));
}

// ----------------------------------------------------------------------------
// HTML restart scan: a line is a safe start exactly when it begins in the
// tokenizer's data state, so the state only tracks what crosses a line end:
// a comment, a CDATA section, an open tag (with its quote), or the raw text of
// script/style/textarea/title and their kin, which only their end tag closes.
// ----------------------------------------------------------------------------

enum : int32_t { RESTART_HTML_COMMENT = 4, RESTART_HTML_CDATA = 5, RESTART_HTML_TAG = 6,
                 RESTART_HTML_RAW = 7 };

static const char* const k_html_raw_tags[] = {
    "script", "style", "textarea", "title", "xmp", "iframe", "noembed", "noframes", "noscript",
};
static const int32_t k_html_raw_tag_count = 9;

// Index of a raw-text tag name [name, name + len), or -1.
static int32_t html_raw_tag(const char* name, size_t len) {
    for (int32_t i = 0; i < k_html_raw_tag_count; i++) {
        if (strlen(k_html_raw_tags[i]) == len && strncasecmp(name, k_html_raw_tags[i], len) == 0) return i;
    }
    return -1;
}

static bool html_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

// The end tag "</name" closing raw tag `raw` in [p, …), or null.
static const char* html_raw_end(const char* p, int32_t raw) {
    const char* name = k_html_raw_tags[raw];
    size_t len = strlen(name);
    for (const char* q = strstr(p, "</"); q; q = strstr(q + 2, "</")) {
        if (strncasecmp(q + 2, name, len) == 0 && !html_name_char(q[2 + len])) return q + 2 + len;
    }
    return nullptr;
}

static void html_restart_step(RestartState* st, const char* line, int64_t index) {
    (void)index;
    const char* p = line;
    while (*p) {
        if (st->kind == RESTART_HTML_COMMENT || st->kind == RESTART_HTML_CDATA) {
            const char* close = strstr(p, st->kind == RESTART_HTML_COMMENT ? "-->" : "]]>");
            if (!close) return;
            p = close + 3;
            *st = RestartState{};
        } else if (st->kind == RESTART_HTML_RAW) {
            const char* close = html_raw_end(p, st->a);
            if (!close) return;
            p = close;
            *st = RestartState{RESTART_HTML_TAG, 0, 0};   // the end tag runs to its '>'
        } else if (st->kind == RESTART_HTML_TAG) {
            if (st->a) {
                const char* close = strchr(p, (char)st->a);
                if (!close) return;
                p = close + 1;
                st->a = 0;
            } else if (*p == '"' || *p == '\'') {
                st->a = *p++;
            } else if (*p == '>') {
                p++;
                // b holds the raw tag index + 1 of a start tag
                *st = st->b > 0 ? RestartState{RESTART_HTML_RAW, st->b - 1, 0} : RestartState{};
            } else {
                p++;
            }
        } else {
            const char* open = strchr(p, '<');
            if (!open) return;
            p = open + 1;
            if (strncmp(p, "!--", 3) == 0) {
                *st = RestartState{RESTART_HTML_COMMENT, 0, 0};
                p += 3;
            } else if (strncmp(p, "![CDATA[", 8) == 0) {
                *st = RestartState{RESTART_HTML_CDATA, 0, 0};
                p += 8;
            } else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
                const char* name = p;
                while (html_name_char(*p)) p++;
                int32_t raw = html_raw_tag(name, (size_t)(p - name));
                *st = RestartState{RESTART_HTML_TAG, 0, raw + 1};
            } else if (*p == '/' || *p == '!' || *p == '?') {
                *st = RestartState{RESTART_HTML_TAG, 0, 0};
            }
        }
    }
}

// Per-format restart rules: how a line moves the state, whether a window
// parse may begin at a line given the state before it, and whether each
// chunk's link labels are collected (Markdown only).
struct RestartRules {
    void (*step)(RestartState* st, const char* line, int64_t index);
    bool (*safe)(const RestartState& before, const char* line, bool prev_blank);
    bool labels;
};

// Markdown: the first line of a block that follows a blank line, at column 0,
// outside any fence, HTML block or front matter.
static bool markdown_restart_safe(const RestartState& before, const char* line, bool prev_blank) {
    return prev_blank && before.kind == RESTART_NONE && line[0] != ' ' && line[0] != '\t' &&
           !line_blank(line);
}

static bool html_restart_safe(const RestartState& before, const char* line, bool prev_blank) {
    (void)line; (void)prev_blank;
    return before.kind == RESTART_NONE;
}

static const RestartRules k_markdown_rules = {restart_step, markdown_restart_safe, true};
static const RestartRules k_html_rules = {html_restart_step, html_restart_safe, false};

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

// Chunk k's link labels, pre-scanned from the state at its boundary; the
// state after it goes back into `st`. The parser reads the buffer's lines in
// place and hands them back before anything could free them.
static void scan_chunk_labels(LinkLabelScan* scan, const HighlightLines* src, int64_t k,
                              BoundaryState* st, HighlightLabels* labels) {
    int64_t start = src->chunk_starts[k];
    int64_t end = chunk_end(src, k);
    int64_t stop = k + 1 < src->chunk_count ? chunk_end(src, k + 1) : end;
    scan->lines.clear();
    for (int64_t i = start; i < stop; i++) scan->lines.push_back((char*)src_line(src, i));
    MarkupParser* parser = scan->parser;
    parser->lines = scan->lines.data();
    parser->line_count = (int)(stop - start);   // a chunk pair is a few hundred lines
    parser->current_line = 0;
    parser->clearLinkDefinitions();
    LinkPrescanState prescan = unpack_link_state(st->link, st->skip);
    parser->prescanLinkDefinitions(0, (int)(end - start), &prescan);
    st->link = pack_link_state(prescan);
    st->skip = prescan.skip;
    parser->forEachLinkLabel(collect_label, labels);
    labels->close_chunk();
    parser->lines = nullptr;
    parser->line_count = 0;
}

// Extend `states` (a valid prefix) through boundary `target`, and with a
// label scan the labels of every chunk before it. A recomputed suspect
// boundary equal to its cached state revalidates every later cached entry,
// and the labels of the chunks between them, since nothing before it changed
// the scan (CED16v3).
static void extend_states(const RestartRules& rules, const HighlightLines* src,
                          const HighlightCache& cache, LinkLabelScan* scan,
                          lam::ArrayList<BoundaryState>* states, HighlightLabels* labels,
                          int64_t target) {
    int64_t cached_label_chunks = cache.labels ? cache.labels->chunks() : 0;
    while ((int64_t)states->length() <= target) {
        int64_t k = (int64_t)states->length() - 1;
        BoundaryState st = (*states)[(size_t)k];
        for (int64_t i = src->chunk_starts[k]; i < chunk_end(src, k); i++) {
            rules.step(&st.restart, src_line(src, i), i);
        }
        if (scan) scan_chunk_labels(scan, src, k, &st, labels);
        states->push_back(st);
        int64_t boundary = k + 1;
        if (boundary < cache.count && boundary_equal(st, cache.states[boundary])) {
            for (int64_t j = boundary + 1; j < cache.count && j <= src->chunk_count; j++) {
                // the labels of the chunk that ends at boundary j come along
                if (scan && j - 1 >= cached_label_chunks) break;
                states->push_back(cache.states[j]);
                if (scan) labels->copy_chunk(*cache.labels, j - 1);
            }
        }
    }
}

// The last line at or before `first` where a window parse may start, by the
// format's rule. Line 0 always qualifies.
static int64_t restart_line(const RestartRules& rules, const HighlightLines* src,
                            const lam::ArrayList<BoundaryState>& states, int64_t first) {
    for (int64_t k = chunk_index(src, first); k >= 0; k--) {
        RestartState st = states[(size_t)k].restart;
        bool prev_blank = false;
        int64_t found = -1;
        for (int64_t i = src->chunk_starts[k]; i <= first; i++) {
            const char* line = src_line(src, i);
            if (rules.safe(st, line, prev_blank)) found = i;
            prev_blank = st.kind == RESTART_NONE && line_blank(line);
            rules.step(&st, line, i);
        }
        if (found >= 0) return found;
    }
    return 0;
}

// ============================================================================
// Span sink
// ============================================================================

// The parser reads either the window's own lines or a container's copy whose
// whole ancestry maps to them.
static bool sink_active(MarkupParser* parser) {
    MarkupSpanSink* sink = parser ? parser->span_sink : nullptr;
    if (!sink) return false;
    if (parser->lines == sink->root_lines) return true;
    size_t n = sink->line_maps.length();
    return n > 0 && sink->line_maps[n - 1].lines == parser->lines;
}

// A position in `lines` (the root array or a container's copy) as a root
// line and byte column, through the container column maps.
static bool sink_root_position(MarkupSpanSink* sink, char** lines, int64_t* line, int64_t* col) {
    size_t k = sink->line_maps.length();
    while (lines != sink->root_lines) {
        if (k == 0) return false;
        const MarkupSpanSink::LineMap& map = sink->line_maps[--k];
        if (map.lines != lines || *line < 0 || *line >= map.count || map.offset[*line] < 0) return false;
        *col += map.offset[*line];
        *line += map.parent_first;
        lines = map.parent;
    }
    return true;
}

void highlight_push_lines(MarkupParser* parser, char** lines, size_t count, int64_t parent_first) {
    // a container inside an unmapped one stays unmapped too
    if (!sink_active(parser) || !lines) return;
    int32_t* offset = (int32_t*)mem_alloc(sizeof(int32_t) * (count ? count : 1), MEM_CAT_INPUT_MARKUP);
    if (!offset) return;
    for (size_t i = 0; i < count; i++) {
        int64_t p = parent_first + (int64_t)i;
        offset[i] = -1;
        if (p < 0 || p >= parser->line_count || !lines[i]) continue;
        size_t parent_len = strlen(parser->lines[p]);
        size_t copy_len = strlen(lines[i]);
        if (copy_len <= parent_len &&
            memcmp(parser->lines[p] + parent_len - copy_len, lines[i], copy_len) == 0) {
            offset[i] = (int32_t)(parent_len - copy_len); // INT_CAST_OK: byte offset within one line
        }
    }
    parser->span_sink->line_maps.push_back(
        MarkupSpanSink::LineMap{lines, parser->lines, parent_first, (int64_t)count, offset});
}

void highlight_pop_lines(MarkupParser* parser, char** lines) {
    MarkupSpanSink* sink = parser ? parser->span_sink : nullptr;
    size_t n = sink ? sink->line_maps.length() : 0;
    if (n == 0 || sink->line_maps[n - 1].lines != lines) return;
    mem_free(sink->line_maps[n - 1].offset);
    sink->line_maps.remove(n - 1);
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
    MarkupSpanSink* sink = parser->span_sink;
    int64_t line = first, col = 0;
    int64_t end_line = end - 1, end_col = (int64_t)strlen(parser->lines[end - 1]);
    if (!sink_root_position(sink, parser->lines, &line, &col) ||
        !sink_root_position(sink, parser->lines, &end_line, &end_col)) return;
    MarkupSpan span = {sink_kind(sink, kind), 1, (int32_t)line, (int32_t)col, // INT_CAST_OK: window line, byte column
                       (int32_t)end_line, (int32_t)end_col}; // INT_CAST_OK: window line, byte column
    sink->spans.push_back(span);
}

void highlight_note_item(MarkupParser* parser, uint64_t item, int first) {
    if (!parser || !parser->span_sink) return;
    highlight_note_block(parser, highlight_item_kind(item), first, parser->current_line);
}

void highlight_begin_inline(MarkupParser* parser) {
    if (!parser || !parser->span_sink) return;
    parser->span_sink->segments.clear();
    parser->span_sink->segments_valid = sink_active(parser);
}

void highlight_add_segment(MarkupParser* parser, int64_t text_off, int line, int col) {
    MarkupSpanSink* sink = parser ? parser->span_sink : nullptr;
    if (!sink || !sink->segments_valid) return;
    int64_t root_line = line, root_col = col;
    // an unmappable line would misplace every span after it: drop the text's spans
    if (!sink_root_position(sink, parser->lines, &root_line, &root_col)) {
        sink->segments_valid = false;
        return;
    }
    sink->segments.push_back(MarkupSpanSink::Segment{text_off, (int32_t)root_line, // INT_CAST_OK: window line
                                                     (int32_t)root_col}); // INT_CAST_OK: byte column
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

// The format-specific step of a window parse: report spans of `text` (the
// window's lines joined with '\n') into `sink`, in parser lines and bytes.
typedef void (*WindowParse)(Input* input, const char* text, size_t len, MarkupSpanSink* sink);

static void markdown_window_parse(Input* input, const char* text, size_t len, MarkupSpanSink* sink) {
    (void)len;
    ParseConfig cfg;
    cfg.format = Format::MARKDOWN;
    cfg.collect_metadata = false;
    MarkupParser* parser = markup_parser_create(input, cfg);
    if (!parser) return;
    parser->span_sink = sink;
    // a reference link resolves against definitions anywhere in the document,
    // as in the full parse; the window's own pre-scan adds the rest
    if (sink->labels) {
        for (int64_t i = 0; i < sink->labels->count(); i++) {
            const char* label = sink->labels->label(i);
            parser->addLinkDefinition(label, strlen(label), "", 0, nullptr, 0);
        }
    }
    parser->parseContent(text);
    // every container pops its map; one left by an early return is freed here
    while (sink->line_maps.length() > 0) highlight_pop_lines(parser, sink->line_maps[sink->line_maps.length() - 1].lines);
    markup_parser_destroy(parser);
}

// HTML spans arrive as byte ranges of the joined text; `starts` are its line
// starts, so a range becomes a parser line and byte column.
struct HtmlSpanCollector {
    MarkupSpanSink* sink;
    lam::ArrayList<size_t> starts;
};

static void html_text_position(const HtmlSpanCollector* c, size_t off, int32_t* line, int32_t* col) {
    size_t lo = 0, hi = c->starts.length();
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c->starts[mid] <= off) lo = mid + 1; else hi = mid;
    }
    size_t l = lo > 0 ? lo - 1 : 0;
    *line = (int32_t)l;                          // INT_CAST_OK: window line index
    *col = (int32_t)(off - c->starts[l]);        // INT_CAST_OK: byte column within a line
}

static void html_collect_span(void* ctx, const char* kind, size_t start, size_t end) {
    HtmlSpanCollector* c = (HtmlSpanCollector*)ctx;
    if (end <= start) return;
    MarkupSpan span = {sink_kind(c->sink, kind), 0, 0, 0, 0, 0};
    html_text_position(c, start, &span.line, &span.col);
    html_text_position(c, end, &span.end_line, &span.end_col);
    c->sink->spans.push_back(span);
}

static void html_window_parse(Input* input, const char* text, size_t len, MarkupSpanSink* sink) {
    HtmlSpanCollector collector;
    collector.sink = sink;
    collector.starts.push_back(0);
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') collector.starts.push_back(i + 1);
    }
    html5_lex_spans(input, text, len, html_collect_span, &collector);
}

static bool highlight_window(const RestartRules& rules, WindowParse parse,
                             const HighlightLines* src, int64_t first, int64_t last,
                             const HighlightCache* given, HighlightResult* out) {
    if (!src || !out || src->count <= 0 || src->chunk_count <= 0) return false;
    first = first < 0 ? 0 : (first >= src->count ? src->count - 1 : first);
    last = last < first ? first : (last >= src->count ? src->count - 1 : last);
    HighlightCache cache = given ? *given : HighlightCache{};

    // the label scan and the window parse share one private pool
    Pool* pool = mem_pool_create(NULL, MEM_ROLE_INPUT, "markup.highlight");
    Input* input = pool ? Input::create(pool, nullptr, nullptr) : nullptr;
    if (!input) {
        if (pool) mem_pool_destroy(pool);
        return false;
    }
    InputAllocationContext allocation = {pool, input->arena, false, input};
    InputAllocationContext* saved_allocation = input_allocation_context;
    input_allocation_context = &allocation;

    LinkLabelScan scan;
    if (rules.labels) {
        ParseConfig cfg;
        cfg.format = Format::MARKDOWN;
        cfg.collect_metadata = false;
        scan.parser = markup_parser_create(input, cfg);
    }

    // the valid prefix of the cache, never less than boundary 0 (no state);
    // with labels, only as far as the cached labels reach
    int64_t valid = cache.valid > cache.count ? cache.count : cache.valid;
    if (scan.parser) {
        int64_t label_chunks = cache.labels ? cache.labels->chunks() : 0;
        if (valid > label_chunks + 1) valid = label_chunks + 1;
    }
    out->states.clear();
    out->labels.reset();
    for (int64_t k = 0; k < valid; k++) out->states.push_back(cache.states[k]);
    if (out->states.length() == 0) out->states.push_back(BoundaryState{});
    for (int64_t k = 0; scan.parser && k + 1 < (int64_t)out->states.length(); k++) {
        out->labels.copy_chunk(*cache.labels, k);
    }
    // labels must cover the whole document; restart states only reach the window
    extend_states(rules, src, cache, scan.parser ? &scan : nullptr, &out->states, &out->labels,
                  scan.parser ? src->chunk_count : chunk_index(src, first));
    if (scan.parser) markup_parser_destroy(scan.parser);
    int64_t start = restart_line(rules, src, out->states, first);
    out->restart_line = start;

    int64_t stop = last + 1 + HIGHLIGHT_LOOKAHEAD;
    if (stop > src->count) stop = src->count;
    StrBuf* text = strbuf_new_cap(4096);
    if (!text) {
        input_allocation_context = saved_allocation;
        mem_pool_destroy(pool);
        return false;
    }
    for (int64_t i = start; i < stop; i++) {
        if (i > start) strbuf_append_char(text, '\n');
        strbuf_append_str(text, src_line(src, i));
    }

    MarkupSpanSink sink;
    sink.line_base = start;
    sink.labels = rules.labels ? &out->labels : nullptr;
    parse(input, text->str ? text->str : "", text->length, &sink);
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

bool markdown_highlight_window(const HighlightLines* src, int64_t first, int64_t last,
                               const HighlightCache* cache, HighlightResult* out) {
    return highlight_window(k_markdown_rules, markdown_window_parse, src, first, last, cache, out);
}

bool html_highlight_window(const HighlightLines* src, int64_t first, int64_t last,
                           const HighlightCache* cache, HighlightResult* out) {
    return highlight_window(k_html_rules, html_window_parse, src, first, last, cache, out);
}

} // namespace markup
} // namespace lambda
