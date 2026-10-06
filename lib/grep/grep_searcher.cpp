// grep_searcher.cpp — the line-oriented search engine (vibe/Lambda_Lib_Grep.md
// §5.1 tiers, §5.5 terminators, §7.1 streaming, GRP20–GRP25).
//
// A block is a run of complete lines (plus, at the end of an input, one
// unterminated line). The engine finds the next line that matches — by a
// literal scan (tiers 0/1) or by RE2 over the block (tier 2) — and only then
// looks at the line: matches are enumerated by RE2 on the line alone, so "^",
// "$", "\A", "\z" and "\b" see exactly the line, and the "\r" of a "\r\n"
// never takes part (GRP18). Counting (GRP30) stops at the line: a selected
// line is confirmed and counted, never enumerated.

#include "grep_walk.hpp"
#include "../line_framer.h"
#include "../file.h"
#include "../log.h"
#include "../memtrack.h"

#include <re2/re2.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define GREP_SNIFF_BYTES 8192                         // a NUL in here makes an input binary
#define GREP_CHUNK_BYTES ((size_t)256 * 1024)         // file read size
#define GREP_DEFAULT_MAX_LINE ((size_t)64 * 1024 * 1024)

struct GrepLine {
    size_t start;        // first byte of the line
    size_t end;          // index of its '\n', or the block length
    size_t content_end;  // end without the '\r' of a "\r\n"
    size_t next;         // start of the following line
};

struct GrepSearcher {
    const GrepMatcher* m;
    re2::RE2* re;
    // per input
    const char* path;
    const GrepSink* sink;
    uint64_t abs_base;       // absolute offset of data[0]
    uint64_t line_at_done;   // line number of the line starting at data[done]
    uint64_t char_at_done;   // code-point offset of data[done]
    uint64_t matches;        // selected records reported (lines counted, counting) for this input
    uint64_t cap;            // the input ends after this many (0 = no cap)
    uint64_t emitted_end;    // absolute end of the last reported line (incl. terminator)
    int before;              // effective context sizes (0 without a context callback)
    int after;
    int after_left;          // after-context lines still owed
    bool confirm_lines;      // context or invert need to know a line matches before enumerating it
    bool input_done;         // skipped by the sink, or the per-file limit reached
    bool stopped;            // the sink returned GREP_STOP
    // per block
    const char* data;
    size_t len;
    size_t done;             // [0, done) was processed by an earlier block (context only)
    size_t line_cursor;      // a data index whose line number is known
    uint64_t line_cursor_no;
    size_t char_cursor;      // a data index whose code-point offset is known
    uint64_t char_cursor_off;
    bool has_cr;             // the block holds a '\r': a buffer-wide RE2 hit needs confirming
};

struct GrepStream {
    GrepSearcher* s;
    LineFramer framer;
    size_t retained;         // processed bytes kept at the front for before-context
    size_t max_line;
    bool sniffed;
    bool binary_skip;
    GrepStatus error;
};

// ── searcher lifecycle ─────────────────────────────────────────────────

GrepSearcher* grep_searcher_create(const GrepMatcher* matcher) {
    if (!matcher) return NULL;
    GrepSearcher* s = (GrepSearcher*)mem_calloc(1, sizeof(GrepSearcher), MEM_CAT_TEMP);
    if (!s) return NULL;
    s->m = matcher;
    // tier 0 never calls RE2, so it needs no private copy
    if (matcher->plan.tier != GREP_TIER_LITERAL) {
        s->re = grep_matcher_compile(matcher);
        if (!s->re) {
            mem_free(s);
            return NULL;
        }
    }
    return s;
}

void grep_searcher_destroy(GrepSearcher* searcher) {
    if (!searcher) return;
    grep_matcher_release_re(searcher->re);
    mem_free(searcher);
}

bool grep_searcher_stopped(const GrepSearcher* searcher) {
    return searcher && searcher->stopped;
}

void grep_searcher_reset_stop(GrepSearcher* searcher) {
    if (searcher) searcher->stopped = false;
}

static void begin_input(GrepSearcher* s, const char* path, const GrepSink* sink) {
    const GrepOptions* o = &s->m->options;
    s->path = path;
    s->sink = sink;
    s->abs_base = 0;
    s->line_at_done = 1;
    s->char_at_done = 0;
    s->matches = 0;
    s->cap = o->max_matches_per_file;
    s->emitted_end = 0;
    s->after_left = 0;
    s->input_done = false;
    s->stopped = false;
    // a count reports no lines, so no context either
    bool ctx = sink && sink->context && !o->count_lines;
    s->before = ctx && o->before_context > 0 ? o->before_context : 0;
    s->after = ctx && o->after_context > 0 ? o->after_context : 0;
    // without context, invert or counting, enumerating a candidate line
    // already says whether it matches: a confirming RE2 call would be redundant
    s->confirm_lines = o->invert || o->count_lines || s->before > 0 || s->after > 0;
}

// ── positions ──────────────────────────────────────────────────────────

static uint64_t line_number_of(GrepSearcher* s, size_t at) {
    if (at >= s->line_cursor) {
        s->line_cursor_no += str_count_byte(s->data + s->line_cursor, at - s->line_cursor, '\n');
        s->line_cursor = at;
        return s->line_cursor_no;
    }
    // a before-context line behind the cursor: count back without moving it
    return s->line_cursor_no - str_count_byte(s->data + at, s->line_cursor - at, '\n');
}

static uint64_t char_offset_of(GrepSearcher* s, size_t at) {
    // str_utf8_count is what in-memory find uses, so the two agree (GRP4)
    if (at >= s->char_cursor) {
        s->char_cursor_off += str_utf8_count(s->data + s->char_cursor, at - s->char_cursor);
        s->char_cursor = at;
        return s->char_cursor_off;
    }
    return s->char_cursor_off - str_utf8_count(s->data + at, s->char_cursor - at);
}

static GrepLineEnding line_ending_of(const GrepLine& line) {
    size_t n = line.next - line.content_end;
    return n == 2 ? GREP_EOL_CRLF : n == 1 ? GREP_EOL_LF : GREP_EOL_NONE;
}

static GrepLine make_line(const GrepSearcher* s, size_t start, size_t end) {
    GrepLine line;
    line.start = start;
    line.end = end;
    line.next = end < s->len ? end + 1 : s->len;
    line.content_end = end;
    // the '\r' of a "\r\n" is part of the terminator; a lone '\r' is content (GRP18)
    if (end < s->len && end > start && s->data[end - 1] == '\r') line.content_end = end - 1;
    return line;
}

// the line starting at `start`
static GrepLine line_at(const GrepSearcher* s, size_t start) {
    size_t r = str_find_byte(s->data + start, s->len - start, '\n');
    return make_line(s, start, r == STR_NPOS ? s->len : start + r);
}

// the line holding byte `at`; `floor` (a line start) bounds the backward scan
static GrepLine line_around(const GrepSearcher* s, size_t floor, size_t at) {
    size_t r = str_rfind_byte(s->data + floor, at - floor, '\n');
    size_t start = r == STR_NPOS ? floor : floor + r + 1;
    size_t e = str_find_byte(s->data + at, s->len - at, '\n');
    return make_line(s, start, e == STR_NPOS ? s->len : at + e);
}

// ── delivery ───────────────────────────────────────────────────────────

static void deliver(GrepSearcher* s, bool context, const GrepMatch* gm) {
    GrepAction action = GREP_CONTINUE;
    if (context) action = s->sink->context(s->sink->user_data, gm);
    else if (s->sink->matched) action = s->sink->matched(s->sink->user_data, gm);
    if (action == GREP_STOP) {
        s->stopped = true;
        s->input_done = true;
    } else if (action == GREP_SKIP_FILE) {
        s->input_done = true;
    }
}

static void count_selected(GrepSearcher* s) {
    s->matches++;
    if (s->cap && s->matches >= s->cap) s->input_done = true;
}

// a whole line as a record (context lines and inverted selections)
static void fill_line_record(GrepSearcher* s, const GrepLine& line, GrepMatch* gm) {
    const GrepOptions* o = &s->m->options;
    memset(gm, 0, sizeof(*gm));
    gm->path = s->path;
    gm->text = s->data + line.start;
    gm->length = line.content_end - line.start;
    gm->byte_offset = s->abs_base + line.start;
    if (o->char_offsets) gm->char_offset = char_offset_of(s, line.start);
    if (o->line_numbers) gm->line_number = line_number_of(s, line.start);
    gm->line = gm->text;
    gm->line_length = gm->length;
    gm->line_ending = line_ending_of(line);
}

static void emit_context(GrepSearcher* s, const GrepLine& line) {
    if (s->abs_base + line.next <= s->emitted_end) return;  // already reported
    GrepMatch gm;
    fill_line_record(s, line, &gm);
    deliver(s, true, &gm);
    s->emitted_end = s->abs_base + line.next;
}

// up to `before` unreported lines ending right before `start`
static void emit_before(GrepSearcher* s, size_t start) {
    if (s->before <= 0) return;
    size_t floor = s->emitted_end > s->abs_base ? (size_t)(s->emitted_end - s->abs_base) : 0;
    if (floor > start) floor = start;
    // step back over at most `before` line starts, then report forward
    size_t first = start;
    for (int k = 0; k < s->before && first > floor; k++) {
        size_t r = str_rfind_byte(s->data + floor, (first - 1) - floor, '\n');
        first = r == STR_NPOS ? floor : floor + r + 1;
    }
    while (first < start && !s->input_done) {
        GrepLine line = line_at(s, first);
        emit_context(s, line);
        first = line.next;
    }
}

// after-context: the first lines of a run of unselected lines
static void unselected_region(GrepSearcher* s, size_t start, size_t end) {
    size_t p = start;
    while (s->after_left > 0 && p < end && !s->input_done) {
        GrepLine line = line_at(s, p);
        emit_context(s, line);
        s->after_left--;
        p = line.next;
    }
}

static bool line_matches(GrepSearcher* s, const GrepLine& line) {
    re2::StringPiece text(s->data + line.start, line.content_end - line.start);
    return s->re->Match(text, 0, text.size(), re2::RE2::UNANCHORED, NULL, 0);
}

static void report_match(GrepSearcher* s, const GrepLine& line, uint64_t line_no, size_t ms, size_t me) {
    const GrepOptions* o = &s->m->options;
    GrepMatch gm;
    memset(&gm, 0, sizeof(gm));
    gm.path = s->path;
    gm.text = s->data + ms;
    gm.length = me - ms;
    gm.byte_offset = s->abs_base + ms;
    if (o->char_offsets) gm.char_offset = char_offset_of(s, ms);
    gm.line_number = line_no;
    if (o->line_text) {
        gm.line = s->data + line.start;
        gm.line_length = line.content_end - line.start;
    }
    gm.line_ending = line_ending_of(line);
    deliver(s, false, &gm);
    count_selected(s);
}

// Tier 0: the leftmost occurrence of the plan's literals in [p, p+n), and of
// those starting there the first in pattern order. For a pure literal set
// that is exactly RE2's leftmost-first match (lit_expand lists the set in
// RE2's preference order), so no RE2 call is needed.
static size_t literal_set_find(const GrepLiteralPlan* plan, const char* p, size_t n, size_t* len) {
    if (plan->count == 1) {
        *len = plan->needles[0].len;
        return str_needle_find(&plan->needles[0], p, n);
    }
    size_t r = str_teddy_find(&plan->teddy, p, n, NULL);
    if (r == STR_NPOS) return r;
    for (int i = 0; i < plan->count; i++) {
        const StrNeedle* needle = &plan->needles[i];
        if (r + needle->len <= n && memcmp(p + r, needle->bytes, needle->len) == 0) {
            *len = needle->len;
            return r;
        }
    }
    return STR_NPOS;  // not reached: Teddy verified a literal there
}

static void enumerate_matches(GrepSearcher* s, const GrepLine& line) {
    const GrepMatcher* m = s->m;
    uint64_t line_no = m->options.line_numbers ? line_number_of(s, line.start) : 0;
    if (m->plan.tier == GREP_TIER_LITERAL) {
        size_t p = line.start;
        while (p < line.content_end && !s->input_done) {
            size_t len = 0;
            size_t r = literal_set_find(&m->plan, s->data + p, line.content_end - p, &len);
            if (r == STR_NPOS) break;
            report_match(s, line, line_no, p + r, p + r + len);
            p += r + len;
        }
        return;
    }
    // leftmost-first matches stepping as in-memory find does (S17.6.1): after
    // an empty match the search moves on one code point
    int g = m->report_group;
    re2::StringPiece text(s->data + line.start, line.content_end - line.start);
    re2::StringPiece sub[2];
    size_t n = text.size();
    size_t start = 0;
    while (start <= n && !s->input_done) {
        if (!s->re->Match(text, start, n, re2::RE2::UNANCHORED, sub, g + 1)) break;
        if (!sub[g].data()) break;
        size_t ms = (size_t)(sub[g].data() - text.data());
        size_t me = ms + sub[g].size();
        report_match(s, line, line_no, line.start + ms, line.start + me);
        if (me > ms) {
            start = me;
        } else {
            if (me >= n) break;
            size_t step = str_utf8_char_len((unsigned char)text[me]);
            start = me + (step ? step : 1);
        }
    }
}

static void select_line(GrepSearcher* s, const GrepLine& line) {
    if (s->m->options.count_lines) {
        count_selected(s);   // confirmed already (confirm_lines), never enumerated
        return;
    }
    emit_before(s, line.start);
    if (s->input_done) return;
    if (s->m->options.invert) {
        GrepMatch gm;
        fill_line_record(s, line, &gm);
        deliver(s, false, &gm);
        count_selected(s);
    } else {
        enumerate_matches(s, line);
    }
    s->emitted_end = s->abs_base + line.next;
    s->after_left = s->after;
}

// ── finding the next matching line ─────────────────────────────────────

// leftmost hit of any plan literal at or after pos: one literal through the
// packed pair, a set through Teddy in one pass (GRP29)
static size_t literal_find(GrepSearcher* s, size_t pos) {
    const GrepLiteralPlan* plan = &s->m->plan;
    size_t r = plan->count == 1
        ? str_needle_find(&plan->needles[0], s->data + pos, s->len - pos)
        : str_teddy_find(&plan->teddy, s->data + pos, s->len - pos, NULL);
    return r == STR_NPOS ? STR_NPOS : pos + r;
}

static bool next_matching_line(GrepSearcher* s, size_t pos, GrepLine* out) {
    const GrepLiteralPlan* plan = &s->m->plan;
    if (plan->tier != GREP_TIER_REGEX) {
        while (pos < s->len) {
            size_t hit = literal_find(s, pos);
            if (hit == STR_NPOS) return false;
            GrepLine line = line_around(s, pos, hit);
            if (plan->tier == GREP_TIER_LITERAL || !s->confirm_lines || line_matches(s, line)) {
                *out = line;
                return true;
            }
            pos = line.next;
        }
        return false;
    }
    // a buffer-wide search would read \A and \z as the block's ends, and miss
    // "x$" before a "\r\n": such inputs go line by line (§5.5)
    if (plan->has_text_anchor || (plan->has_end_line && s->has_cr)) {
        while (pos < s->len) {
            GrepLine line = line_at(s, pos);
            if (line_matches(s, line)) {
                *out = line;
                return true;
            }
            pos = line.next;
        }
        return false;
    }
    re2::StringPiece text(s->data, s->len);
    re2::StringPiece sub;
    while (pos < s->len) {
        if (!s->re->Match(text, pos, s->len, re2::RE2::UNANCHORED, &sub, 1)) return false;
        size_t at = (size_t)(sub.data() - s->data);
        // an empty match at the very end belongs to no line of a terminated block
        if (at >= s->len && s->data[s->len - 1] == '\n') return false;
        if (at > s->len) return false;
        GrepLine line = line_around(s, pos, at < s->len ? at : s->len);
        // a hit may exist only through a '\r' that belongs to the terminator
        if (!s->has_cr || !s->confirm_lines || line_matches(s, line)) {
            *out = line;
            return true;
        }
        pos = line.next;
    }
    return false;
}

// Counting (GRP30) the lines of data[start, end) without looking at them, when
// all of them are selected. `end` is a line start or the block's end; only an
// input's final block ends inside a line, and that line counts too.
static void count_lines_in(GrepSearcher* s, size_t start, size_t end) {
    if (start >= end) return;
    uint64_t n = str_count_byte(s->data + start, end - start, '\n');
    if (s->data[end - 1] != '\n') n++;
    if (s->cap && s->matches + n >= s->cap) {
        n = s->cap - s->matches;
        s->input_done = true;
    }
    s->matches += n;
}

// Search data[done, len): complete lines, plus a final unterminated line when
// the input ends here. data[0, done) holds lines of earlier blocks kept only
// as before-context.
static void search_block(GrepSearcher* s, const char* data, size_t len, size_t done) {
    const GrepOptions* o = &s->m->options;
    s->data = data;
    s->len = len;
    s->done = done;
    s->line_cursor = done;
    s->line_cursor_no = s->line_at_done;
    s->char_cursor = done;
    s->char_cursor_off = s->char_at_done;
    size_t pos = done;
    if (o->count_lines && s->m->every_line) {
        // every line matches (grep -c ''): the count is the number of lines
        if (!o->invert) count_lines_in(s, done, len);
        pos = len;
    } else {
        s->has_cr = len > done && memchr(data + done, '\r', len - done) != NULL;
    }
    while (pos < len && !s->input_done) {
        GrepLine found;
        bool hit = next_matching_line(s, pos, &found);
        size_t gap_end = hit ? found.start : len;
        if (o->invert && o->count_lines) {
            count_lines_in(s, pos, gap_end);   // the lines between matches are the selected ones
        } else if (o->invert) {
            // every line in the gap is selected; the matching line is context
            size_t p = pos;
            while (p < gap_end && !s->input_done) {
                GrepLine line = line_at(s, p);
                select_line(s, line);
                p = line.next;
            }
            if (hit && !s->input_done) unselected_region(s, found.start, found.next);
        } else {
            if (pos < gap_end) unselected_region(s, pos, gap_end);
            if (hit && !s->input_done) select_line(s, found);
        }
        if (!hit) break;
        pos = found.next;
    }
    // carry the counters to data[len], which the next block starts from
    if (o->line_numbers) s->line_at_done = line_number_of(s, len);
    if (o->char_offsets) s->char_at_done = char_offset_of(s, len);
}

bool grep_input_is_binary(const char* data, size_t length) {
    return memchr(data, 0, length < GREP_SNIFF_BYTES ? length : GREP_SNIFF_BYTES) != NULL;
}

static bool looks_binary(const GrepSearcher* s, const char* data, size_t len) {
    if (s->m->options.binary_as_text) return false;
    return grep_input_is_binary(data, len);
}

static void report_file_done(GrepSearcher* s) {
    if (!s->sink || !s->sink->file_done || s->stopped) return;
    if (s->sink->file_done(s->sink->user_data, s->path, s->matches) == GREP_STOP) s->stopped = true;
}

GrepStatus grep_search_buffer(GrepSearcher* searcher, const char* label,
                              const char* data, size_t length, const GrepSink* sink) {
    if (!searcher || !sink || (!data && length)) return GREP_ERR_ARGUMENT;
    begin_input(searcher, label, sink);
    if (looks_binary(searcher, data, length)) return GREP_OK;
    if (length) search_block(searcher, data, length, 0);
    report_file_done(searcher);
    return GREP_OK;
}

// ── streaming (§7.1) ───────────────────────────────────────────────────

GrepStream* grep_stream_open(GrepSearcher* searcher, const char* label, const GrepSink* sink) {
    if (!searcher || !sink) return NULL;
    GrepStream* st = (GrepStream*)mem_calloc(1, sizeof(GrepStream), MEM_CAT_TEMP);
    if (!st) return NULL;
    if (!line_framer_init(&st->framer, 64 * 1024, MEM_CAT_TEMP)) {
        mem_free(st);
        return NULL;
    }
    st->s = searcher;
    size_t cap = searcher->m->options.max_line_bytes;
    st->max_line = cap ? cap : GREP_DEFAULT_MAX_LINE;
    // nothing to sniff for when binary inputs are searched anyway
    st->sniffed = searcher->m->options.binary_as_text;
    begin_input(searcher, label, sink);
    return st;
}

static GrepStatus stream_pump(GrepStream* st, bool final) {
    GrepSearcher* s = st->s;
    size_t avail = 0;
    const char* buf = line_framer_data(&st->framer, &avail);
    if (!st->sniffed) {
        // the decision waits for the whole sniff window, so it does not
        // depend on how the input is chunked
        if (!final && avail < GREP_SNIFF_BYTES) return GREP_OK;
        if (looks_binary(s, buf, avail)) {
            st->binary_skip = true;
            return GREP_OK;
        }
        st->sniffed = true;
    }
    size_t end = avail;
    if (!final) {
        size_t r = avail > st->retained ? str_rfind_byte(buf + st->retained, avail - st->retained, '\n') : STR_NPOS;
        if (r == STR_NPOS) {
            if (avail - st->retained > st->max_line) {
                log_error("grep stream: line over %zu bytes in '%s'", st->max_line, s->path ? s->path : "(buffer)");
                return st->error = GREP_ERR_LINE_TOO_LONG;
            }
            return GREP_OK;
        }
        end = st->retained + r + 1;
    }
    if (end > st->retained) search_block(s, buf, end, st->retained);
    if (final) return GREP_OK;
    // keep the last `before` lines for the before-context of the next block
    size_t keep_from = end;
    for (int i = 0; i < s->before && keep_from > 0; i++) {
        size_t r = str_rfind_byte(buf, keep_from - 1, '\n');
        keep_from = r == STR_NPOS ? 0 : r + 1;
    }
    line_framer_consume(&st->framer, keep_from);
    s->abs_base += keep_from;
    st->retained = end - keep_from;
    return GREP_OK;
}

GrepStatus grep_stream_feed(GrepStream* stream, const char* data, size_t length) {
    if (!stream) return GREP_ERR_ARGUMENT;
    if (stream->error) return stream->error;
    if (stream->s->input_done || stream->binary_skip || length == 0) return GREP_OK;
    if (!line_framer_append(&stream->framer, data, length)) return stream->error = GREP_ERR_MEMORY;
    return stream_pump(stream, false);
}

static bool stream_wants_more(const GrepStream* st) {
    return !st->error && !st->binary_skip && !st->s->input_done;
}

GrepStatus grep_stream_finish(GrepStream* stream) {
    if (!stream) return GREP_ERR_ARGUMENT;
    GrepStatus status = stream->error;
    if (status == GREP_OK && stream_wants_more(stream)) status = stream_pump(stream, true);
    if (status == GREP_OK && !stream->binary_skip) report_file_done(stream->s);
    line_framer_destroy(&stream->framer);
    mem_free(stream);
    return status;
}

GrepStatus grep_search_file(GrepSearcher* searcher, const char* path, const GrepSink* sink) {
    return grep_search_file_as(searcher, path, path, sink, 0);
}

GrepStatus grep_search_file_as(GrepSearcher* searcher, const char* open_path, const char* label,
                               const GrepSink* sink, uint64_t cap) {
    if (!searcher || !open_path || !sink) return GREP_ERR_ARGUMENT;
    const char* path = label ? label : open_path;
    FILE* f = file_open_regular_read(open_path);
    if (!f) {
        log_error("grep: cannot open '%s': %s", open_path, strerror(errno));
        return GREP_ERR_IO;
    }
    char* buf = (char*)mem_alloc(GREP_CHUNK_BYTES, MEM_CAT_TEMP);
    GrepStream* st = buf ? grep_stream_open(searcher, path, sink) : NULL;
    if (!st) {
        if (buf) mem_free(buf);
        fclose(f);
        return GREP_ERR_MEMORY;
    }
    if (cap && (!searcher->cap || cap < searcher->cap)) searcher->cap = cap;
    GrepStatus status = GREP_OK;
    while (status == GREP_OK && stream_wants_more(st)) {
        size_t n = fread(buf, 1, GREP_CHUNK_BYTES, f);
        if (n == 0) {
            if (ferror(f)) {
                log_error("grep: read error on '%s'", path);
                status = GREP_ERR_IO;
            }
            break;
        }
        status = grep_stream_feed(st, buf, n);
    }
    fclose(f);
    mem_free(buf);
    // a read error fails the input: no final line, no file_done
    if (status != GREP_OK) st->error = status;
    GrepStatus finish = grep_stream_finish(st);
    return status != GREP_OK ? status : finish;
}
