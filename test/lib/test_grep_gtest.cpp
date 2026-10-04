// test_grep_gtest.cpp — lib/grep (vibe/Lambda_Lib_Grep.md).
//
// The tier tests compare every literal-accelerated plan with an independent
// reference: RE2 run line by line by this file, with the "\r" of a "\r\n"
// removed, stepping as in-memory find does. A plan that disagrees with it on
// any corpus pattern is unsound.

#include <gtest/gtest.h>
#include <re2/re2.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "../../lib/grep/grep.h"
#include "../../lib/grep/grep_internal.hpp"
#include "../../lib/str.h"
#include "../../lib/file.h"
#include "../../lib/file_utils.h"
#include "../../lib/log.h"

namespace {

struct Rec {
    bool context;
    char path[128];
    char text[160];
    char line[160];
    uint64_t byte_offset, char_offset, line_number;
};

#define MAX_RECS 1024

struct Collector {
    Rec recs[MAX_RECS];
    int n = 0;
    int files_done = 0;
    char done_paths[64][256];
    uint64_t done_counts[64];
    GrepAction on_match = GREP_CONTINUE;
    int stop_after = -1;  // return GREP_STOP after this many matches
};

void copy_bounded(char* dst, size_t cap, const char* src, size_t len) {
    size_t n = len < cap - 1 ? len : cap - 1;
    if (src && n) memcpy(dst, src, n);
    dst[n] = '\0';
}

GrepAction collect(Collector* c, const GrepMatch* gm, bool context) {
    if (c->n < MAX_RECS) {
        Rec& r = c->recs[c->n++];
        memset(&r, 0, sizeof(r));
        r.context = context;
        if (gm->path) copy_bounded(r.path, sizeof(r.path), gm->path, strlen(gm->path));
        copy_bounded(r.text, sizeof(r.text), gm->text, gm->length);
        if (gm->line) copy_bounded(r.line, sizeof(r.line), gm->line, gm->line_length);
        r.byte_offset = gm->byte_offset;
        r.char_offset = gm->char_offset;
        r.line_number = gm->line_number;
    }
    if (!context && c->stop_after >= 0) {
        int matches = 0;
        for (int i = 0; i < c->n; i++) matches += !c->recs[i].context;
        if (matches >= c->stop_after) return GREP_STOP;
    }
    return context ? GREP_CONTINUE : c->on_match;
}

GrepAction on_matched(void* ud, const GrepMatch* gm) { return collect((Collector*)ud, gm, false); }
GrepAction on_context(void* ud, const GrepMatch* gm) { return collect((Collector*)ud, gm, true); }
GrepAction on_done(void* ud, const char* path, uint64_t count) {
    Collector* c = (Collector*)ud;
    if (c->files_done < 64) {
        copy_bounded(c->done_paths[c->files_done], 256, path ? path : "", path ? strlen(path) : 0);
        c->done_counts[c->files_done] = count;
    }
    c->files_done++;
    return GREP_CONTINUE;
}

GrepSink sink_for(Collector* c, bool with_context = false) {
    GrepSink s = {c, on_matched, with_context ? on_context : NULL, on_done};
    return s;
}

GrepOptions opts() {
    GrepOptions o;
    memset(&o, 0, sizeof(o));
    return o;
}

GrepMatcher* make(const char* pattern, const GrepOptions& o, GrepStatus* status = NULL) {
    const char* pats[1] = {pattern};
    size_t lens[1] = {strlen(pattern)};
    GrepMatcher* m = NULL;
    char err[256];
    GrepStatus st = grep_matcher_create(pats, lens, 1, &o, &m, err, sizeof(err));
    if (status) *status = st;
    return m;
}

// search a buffer with a fresh searcher
void search(const GrepMatcher* m, const char* data, size_t len, Collector* c, bool context = false) {
    GrepSearcher* s = grep_searcher_create(m);
    ASSERT_NE(s, nullptr);
    GrepSink sink = sink_for(c, context);
    EXPECT_EQ(grep_search_buffer(s, "buf", data, len, &sink), GREP_OK);
    grep_searcher_destroy(s);
}

void search_chunked(const GrepMatcher* m, const char* data, size_t len, size_t chunk, Collector* c, bool context = false) {
    GrepSearcher* s = grep_searcher_create(m);
    ASSERT_NE(s, nullptr);
    GrepSink sink = sink_for(c, context);
    GrepStream* st = grep_stream_open(s, "buf", &sink);
    ASSERT_NE(st, nullptr);
    for (size_t p = 0; p < len; p += chunk) {
        size_t n = len - p < chunk ? len - p : chunk;
        ASSERT_EQ(grep_stream_feed(st, data + p, n), GREP_OK);
    }
    EXPECT_EQ(grep_stream_finish(st), GREP_OK);
    grep_searcher_destroy(s);
}

// Reference: RE2 on each line alone (the "\r" of a "\r\n" removed), stepping
// as in-memory find does. Independent of the library's tiers.
int reference(const char* regex, bool ignore_case, const char* data, size_t len, Rec* out, int cap) {
    re2::RE2::Options o;
    o.set_log_errors(false);
    o.set_never_nl(true);
    o.set_case_sensitive(!ignore_case);
    re2::RE2 re(re2::StringPiece(regex, strlen(regex)), o);
    if (!re.ok()) return -1;
    int n = 0;
    uint64_t line_no = 1;
    size_t p = 0;
    while (p < len) {
        const char* nl = (const char*)memchr(data + p, '\n', len - p);
        size_t end = nl ? (size_t)(nl - data) : len;
        size_t cend = end;
        if (nl && cend > p && data[cend - 1] == '\r') cend--;
        re2::StringPiece line(data + p, cend - p), sub;
        size_t start = 0;
        while (start <= line.size()) {
            if (!re.Match(line, start, line.size(), re2::RE2::UNANCHORED, &sub, 1)) break;
            size_t ms = (size_t)(sub.data() - line.data()), me = ms + sub.size();
            if (n < cap) {
                Rec& r = out[n];
                memset(&r, 0, sizeof(r));
                copy_bounded(r.text, sizeof(r.text), sub.data(), sub.size());
                r.byte_offset = p + ms;
                r.line_number = line_no;
                r.char_offset = str_utf8_count(data, p + ms);
            }
            n++;
            if (me > ms) start = me;
            else if (me >= line.size()) break;
            else start = me + str_utf8_char_len((unsigned char)line[me]);
        }
        line_no++;
        p = nl ? end + 1 : len;
    }
    return n;
}

void expect_same(const Collector& c, const Rec* ref, int nref, const char* what) {
    ASSERT_EQ(c.n, nref) << what;
    for (int i = 0; i < nref; i++) {
        EXPECT_STREQ(c.recs[i].text, ref[i].text) << what << " #" << i;
        EXPECT_EQ(c.recs[i].byte_offset, ref[i].byte_offset) << what << " #" << i;
        EXPECT_EQ(c.recs[i].line_number, ref[i].line_number) << what << " #" << i;
        EXPECT_EQ(c.recs[i].char_offset, ref[i].char_offset) << what << " #" << i;
    }
}

const char CORPUS[] =
    "int main(void) {\n"
    "    printf(\"hello, world\\n\");   // TODO: fix\r\n"
    "    return 0;\n"
    "}\n"
    "\n"
    "Kelvin \xe2\x84\xaa and long \xc5\xbf: case folding\r\n"
    "caf\xc3\xa9 au lait, CAF\xc3\x89 FORT\n"
    "foo foo foobar barfoo foo_x (foo) foo\n"
    "TODO: refactor 42 items; todo: 7 more\r\n"
    "\r\n"
    "aab aaa b\n"
    "x$y \\d+ end\n"
    "abcd abc ab abbcd mem_calloc mem_free mem_alloc\n"
    "last line without newline 123";

const char* const CORPUS_PATTERNS[] = {
    "foo", "TODO", "hello", "\\d+", "TODO: \\w+", "(foo|bar)baz?", "fo+", "a*",
    "^\\s*return", "end$", "\\bfoo\\b", "[0-9]{2}", "caf\xc3\xa9", "line", "\\w+\\s+\\d+",
    "x\\$y", "^$", ".+", "o\\b", "\\Afoo", "\\d+\\z", "Kelvin|long", "(?i)kelvin",
    "ab|abc", "(?:hello|world)", "z+", "\\(foo\\)", "e\\s", "\\s$", "\\S+$",
    // literal sets past the old 8-literal cap go through Teddy (GRP29)
    "first|second|items|more|lait|FORT|refactor|world|fix|void|return|line|end",
    "(?i)(?:todo|fix|lait|kelvin|long|unicode|hello)",
    "(?:alpha|beta|gamma|delta|epsilon|zeta|eta|theta|iota|kappa)\\b",
    // pure literal sets run without RE2: the first alternative in pattern order
    // wins at a position, also through RE2's factoring and cross products
    "abc|ab", "(?:a|ab)(?:c|bcd)", "mem_alloc|mem_free|mem_calloc", "b|ab|abb",
};

TEST(GrepMatcher, ReportsSyntaxAndUnsupported) {
    GrepStatus st;
    EXPECT_EQ(make("a(b", opts(), &st), nullptr);
    EXPECT_EQ(st, GREP_ERR_SYNTAX);
    EXPECT_EQ(make("(a)\\1", opts(), &st), nullptr);
    EXPECT_EQ(st, GREP_ERR_UNSUPPORTED);
    EXPECT_EQ(make("foo(?=bar)", opts(), &st), nullptr);
    EXPECT_EQ(st, GREP_ERR_UNSUPPORTED);
    EXPECT_EQ(make("(?<!x)y", opts(), &st), nullptr);
    EXPECT_EQ(st, GREP_ERR_UNSUPPORTED);
    // an escaped or bracketed lookalike is not a lookaround
    GrepMatcher* m = make("[(?=]x", opts(), &st);
    EXPECT_EQ(st, GREP_OK);
    grep_matcher_destroy(m);
    // a fixed string never fails, whatever it contains
    GrepOptions o = opts();
    o.fixed_string = true;
    m = make("a(b)\\1[", o, &st);
    ASSERT_NE(m, nullptr);
    Collector c;
    const char text[] = "x a(b)\\1[ y";
    search(m, text, strlen(text), &c);
    ASSERT_EQ(c.n, 1);
    EXPECT_STREQ(c.recs[0].text, "a(b)\\1[");
    grep_matcher_destroy(m);
}

TEST(GrepMatcher, PlansTiers) {
    struct { const char* pattern; bool icase; GrepTier tier; } cases[] = {
        {"foo", false, GREP_TIER_LITERAL},
        {"foo\\d+", false, GREP_TIER_REQUIRED},
        {"(?:foo|bar)\\d", false, GREP_TIER_REQUIRED},
        {"\\w+\\s+\\d+", false, GREP_TIER_REGEX},
        {"hello", true, GREP_TIER_REQUIRED},     // folded literal, confirmed by RE2
        {"x*", false, GREP_TIER_REGEX},
        {"a\\nb", false, GREP_TIER_REGEX},       // never_nl: a literal newline never matches
        {"e", false, GREP_TIER_LITERAL},         // a pure literal is exact, however common
        {"e\\d", false, GREP_TIER_REGEX},        // a common single byte is not worth scanning for
        // a pure literal set needs no RE2: tier 0, through Teddy (GRP29)
        {"a1|b2|c3|d4|e5|f6|g7|h8|i9|j0|k1|l2", false, GREP_TIER_LITERAL},
        {"mem_alloc|mem_free", false, GREP_TIER_LITERAL},   // RE2 factors it as mem_(?:alloc|free)
        {"(?:a|ab)(?:c|bcd)", false, GREP_TIER_LITERAL},
        {"a|", false, GREP_TIER_REGEX},                      // the empty alternative matches everywhere
        {"(?i)(?:hello|world)", false, GREP_TIER_REQUIRED},  // folded literals are confirmed by RE2
    };
    for (auto& tc : cases) {
        GrepOptions o = opts();
        o.ignore_case = tc.icase;
        GrepMatcher* m = make(tc.pattern, o);
        ASSERT_NE(m, nullptr) << tc.pattern;
        EXPECT_EQ(m->plan.tier, tc.tier) << tc.pattern;
        grep_matcher_destroy(m);
    }
}

// every corpus pattern, through its planned tier and forced to tier 2, in
// "\n" and "\r\n" flavours, agrees with the reference (G2 gate)
TEST(GrepSearch, TiersAgreeWithReference) {
    static Rec ref[MAX_RECS];
    for (const char* pattern : CORPUS_PATTERNS) {
        for (int icase = 0; icase < 2; icase++) {
            int nref = reference(pattern, icase, CORPUS, strlen(CORPUS), ref, MAX_RECS);
            ASSERT_GE(nref, 0) << pattern;
            for (int force = 0; force < 2; force++) {
                GrepOptions o = opts();
                o.ignore_case = icase;
                o.line_numbers = true;
                o.char_offsets = true;
                GrepMatcher* m = make(pattern, o);
                ASSERT_NE(m, nullptr) << pattern;
                if (force) {
                    grep_literal_plan_release(&m->plan);
                    m->plan.tier = GREP_TIER_REGEX;
                }
                static Collector c;
                c.n = 0;
                search(m, CORPUS, strlen(CORPUS), &c);
                char what[128];
                snprintf(what, sizeof(what), "/%s/ icase=%d forced=%d", pattern, icase, force);
                expect_same(c, ref, nref, what);
                grep_matcher_destroy(m);
            }
        }
    }
}

TEST(GrepSearch, NoMatchSpansALine) {
    GrepMatcher* m = make("a\\sb", opts());
    Collector c;
    const char text[] = "a\nb a b";
    search(m, text, strlen(text), &c);
    ASSERT_EQ(c.n, 1);
    EXPECT_EQ(c.recs[0].byte_offset, 4u);
    grep_matcher_destroy(m);
    // (?s) cannot make '.' cross a line either
    m = make("(?s)a.b", opts());
    c.n = 0;
    search(m, text, strlen(text), &c);
    EXPECT_EQ(c.n, 1);
    grep_matcher_destroy(m);
}

TEST(GrepSearch, CrlfTerminatorIsNotContent) {
    const char text[] = "foo\r\nbar\r\n\r\nend\rx\n";
    GrepOptions o = opts();
    o.line_text = true;
    GrepMatcher* m = make("o$", o);
    Collector c;
    search(m, text, strlen(text), &c);
    ASSERT_EQ(c.n, 1);
    EXPECT_STREQ(c.recs[0].line, "foo");
    grep_matcher_destroy(m);
    // ".+" never matches the empty CRLF line, and a lone '\r' is content
    m = make(".+", o);
    c.n = 0;
    search(m, text, strlen(text), &c);
    ASSERT_EQ(c.n, 3);
    EXPECT_STREQ(c.recs[2].text, "end\rx");
    grep_matcher_destroy(m);
}

TEST(GrepSearch, EmptyMatchesStepLikeFind) {
    // S17.6.1: "aa" at 0, then "" at 2 and "" at 3
    GrepMatcher* m = make("a*", opts());
    Collector c;
    search(m, "aab", 3, &c);
    ASSERT_EQ(c.n, 3);
    EXPECT_STREQ(c.recs[0].text, "aa");
    EXPECT_EQ(c.recs[1].byte_offset, 2u);
    EXPECT_EQ(c.recs[2].byte_offset, 3u);
    grep_matcher_destroy(m);
}

TEST(GrepSearch, PositionsInCodePoints) {
    GrepOptions o = opts();
    o.char_offsets = true;
    o.line_numbers = true;
    GrepMatcher* m = make("abc", o);
    Collector c;
    const char text[] = "\xc3\xa9" "abc\n\xe4\xb8\xad" "abc";
    search(m, text, strlen(text), &c);
    ASSERT_EQ(c.n, 2);
    EXPECT_EQ(c.recs[0].char_offset, 1u);
    EXPECT_EQ(c.recs[0].byte_offset, 2u);
    EXPECT_EQ(c.recs[1].char_offset, 6u);   // é a b c \n 中 -> 6
    EXPECT_EQ(c.recs[1].line_number, 2u);
    grep_matcher_destroy(m);
}

TEST(GrepSearch, WordAndWholeLine) {
    GrepOptions o = opts();
    o.word = true;
    GrepMatcher* m = make("foo", o);
    Collector c;
    const char text[] = "foo foo foobar barfoo foo_x (foo) foo";
    search(m, text, strlen(text), &c);
    ASSERT_EQ(c.n, 4);
    EXPECT_EQ(c.recs[0].byte_offset, 0u);
    EXPECT_EQ(c.recs[1].byte_offset, 4u);
    EXPECT_EQ(c.recs[2].byte_offset, 29u);
    EXPECT_EQ(c.recs[3].byte_offset, 34u);
    grep_matcher_destroy(m);
    // a word match is not stopped by a non-ASCII letter
    m = make("caf", o);
    c.n = 0;
    search(m, "caf\xc3\xa9 caf", 9, &c);
    ASSERT_EQ(c.n, 1);
    EXPECT_EQ(c.recs[0].byte_offset, 6u);
    grep_matcher_destroy(m);

    o = opts();
    o.whole_line = true;
    m = make("ab+", o);
    c.n = 0;
    const char lines[] = "ab\nabb x\nabbb\r\n";
    search(m, lines, strlen(lines), &c);
    ASSERT_EQ(c.n, 2);
    EXPECT_STREQ(c.recs[1].text, "abbb");
    grep_matcher_destroy(m);
}

TEST(GrepSearch, SeveralPatternsMatchAny) {
    const char* pats[2] = {"foo|x", "bar"};
    size_t lens[2] = {5, 3};
    GrepOptions o = opts();
    GrepMatcher* m = NULL;
    ASSERT_EQ(grep_matcher_create(pats, lens, 2, &o, &m, NULL, 0), GREP_OK);
    Collector c;
    const char text[] = "bar x foo";
    search(m, text, strlen(text), &c);
    ASSERT_EQ(c.n, 3);
    grep_matcher_destroy(m);
}

TEST(GrepSearch, InvertAndContext) {
    const char text[] = "a1\nb\nc\nd2\ne\nf\ng\nh3\ni\n";
    GrepOptions o = opts();
    o.line_numbers = true;
    o.before_context = 1;
    o.after_context = 1;
    GrepMatcher* m = make("\\d", o);
    Collector c;
    search(m, text, strlen(text), &c, true);
    // a1, b(after), c(before), d2, e(after), g(before), h3, i(after)
    const char* expect[] = {"1", "b", "c", "2", "e", "g", "3", "i"};
    ASSERT_EQ(c.n, 8);
    for (int i = 0; i < 8; i++) EXPECT_STREQ(c.recs[i].text, expect[i]) << i;
    EXPECT_TRUE(c.recs[1].context);
    EXPECT_EQ(c.recs[5].line_number, 7u);
    grep_matcher_destroy(m);

    o = opts();
    o.invert = true;
    m = make("\\d", o);
    c.n = 0;
    search(m, text, strlen(text), &c);
    ASSERT_EQ(c.n, 6);
    EXPECT_STREQ(c.recs[0].text, "b");
    EXPECT_STREQ(c.recs[5].text, "i");
    EXPECT_STREQ(c.recs[5].line, "i");
    grep_matcher_destroy(m);
}

TEST(GrepSearch, LimitsAndSinkActions) {
    const char text[] = "x1 x2\nx3\nx4\n";
    GrepOptions o = opts();
    o.max_matches_per_file = 3;
    GrepMatcher* m = make("x\\d", o);
    Collector c;
    search(m, text, strlen(text), &c);
    EXPECT_EQ(c.n, 3);
    EXPECT_EQ(c.files_done, 1);
    EXPECT_EQ(c.done_counts[0], 3u);
    grep_matcher_destroy(m);

    m = make("x\\d", opts());
    c.n = 0;
    c.files_done = 0;
    c.on_match = GREP_SKIP_FILE;
    search(m, text, strlen(text), &c);
    EXPECT_EQ(c.n, 1);
    EXPECT_EQ(c.files_done, 1);   // a skipped input still reports file_done
    c.n = 0;
    c.files_done = 0;
    c.on_match = GREP_CONTINUE;
    c.stop_after = 2;
    GrepSearcher* s = grep_searcher_create(m);
    GrepSink sink = sink_for(&c);
    grep_search_buffer(s, "buf", text, strlen(text), &sink);
    EXPECT_EQ(c.n, 2);
    EXPECT_EQ(c.files_done, 0);   // a stop ends everything
    EXPECT_TRUE(grep_searcher_stopped(s));
    grep_searcher_destroy(s);
    grep_matcher_destroy(m);
}

TEST(GrepSearch, BinaryInputsAreSkipped) {
    char data[64] = "match here\n";
    data[20] = '\0';
    data[30] = 'm';
    GrepMatcher* m = make("match", opts());
    Collector c;
    search(m, data, sizeof(data), &c);
    EXPECT_EQ(c.n, 0);
    EXPECT_EQ(c.files_done, 0);
    grep_matcher_destroy(m);
    GrepOptions o = opts();
    o.binary_as_text = true;
    m = make("match", o);
    search(m, data, sizeof(data), &c);
    EXPECT_EQ(c.n, 1);
    grep_matcher_destroy(m);
}

// G3 gate: results do not depend on chunking, including a boundary between
// "\r" and "\n", context retention across chunks and the binary sniff window
TEST(GrepStream, ResultsIndependentOfChunkSize) {
    // 40 copies pass the 8 KiB binary sniff window, so lines are split across
    // chunks both while the sniff waits and after it
    static char big[40 * sizeof(CORPUS) + 64];
    size_t big_len = 0;
    for (int i = 0; i < 40; i++) {
        memcpy(big + big_len, CORPUS, strlen(CORPUS));
        big_len += strlen(CORPUS);
        big[big_len++] = '\n';
    }
    ASSERT_GT(big_len, (size_t)8192);
    const char* patterns[] = {"foo", "\\d+", "o$", "^$", "\\bfoo\\b", "(?i)todo", "e\\s"};
    for (const char* pattern : patterns) {
        for (int ctx = 0; ctx < 2; ctx++) {
            GrepOptions o = opts();
            o.line_numbers = true;
            o.char_offsets = true;
            o.line_text = true;
            o.before_context = ctx ? 2 : 0;
            o.after_context = ctx ? 1 : 0;
            GrepMatcher* m = make(pattern, o);
            ASSERT_NE(m, nullptr);
            static Collector whole, part;
            whole.n = 0;
            search(m, big, big_len, &whole, ctx);
            ASSERT_LT(whole.n, MAX_RECS) << pattern;
            for (size_t chunk = 1; chunk <= 40; chunk++) {
                part.n = 0;
                search_chunked(m, big, big_len, chunk, &part, ctx);
                ASSERT_EQ(part.n, whole.n) << pattern << " chunk " << chunk << " ctx " << ctx;
                for (int i = 0; i < whole.n; i++) {
                    EXPECT_STREQ(part.recs[i].text, whole.recs[i].text);
                    EXPECT_STREQ(part.recs[i].line, whole.recs[i].line);
                    EXPECT_EQ(part.recs[i].context, whole.recs[i].context);
                    EXPECT_EQ(part.recs[i].byte_offset, whole.recs[i].byte_offset);
                    EXPECT_EQ(part.recs[i].char_offset, whole.recs[i].char_offset);
                    EXPECT_EQ(part.recs[i].line_number, whole.recs[i].line_number);
                }
            }
            grep_matcher_destroy(m);
        }
    }
}

TEST(GrepStream, LongLineFailsTheInput) {
    GrepOptions o = opts();
    o.max_line_bytes = 100;
    GrepMatcher* m = make("x", o);
    GrepSearcher* s = grep_searcher_create(m);
    Collector c;
    GrepSink sink = sink_for(&c);
    GrepStream* st = grep_stream_open(s, "buf", &sink);
    char chunk[64];
    memset(chunk, 'a', sizeof(chunk));
    GrepStatus last = GREP_OK;
    for (int i = 0; i < 300 && last == GREP_OK; i++) last = grep_stream_feed(st, chunk, sizeof(chunk));
    EXPECT_EQ(last, GREP_ERR_LINE_TOO_LONG);
    EXPECT_EQ(grep_stream_finish(st), GREP_ERR_LINE_TOO_LONG);
    EXPECT_EQ(c.files_done, 0);
    grep_searcher_destroy(s);
    grep_matcher_destroy(m);
}

// G7: the SIMD prefix kernel patched into RE2 (patches/re2-simd-prefix-accel.patch)
// finds the same leftmost match as a plain search, at every alignment and
// haystack length around the vector width
TEST(Re2PrefixAccel, MatchesNaiveSearchAtEveryAlignment) {
    unsigned seed = 99;
    auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
    const char alphabet[] = "abcab";
    static char buf[256];
    const char* needles[] = {"ab", "abc", "bca", "cab", "abcab", "ccc"};
    for (const char* needle : needles) {
        re2::RE2 re(needle);
        ASSERT_TRUE(re.ok());
        size_t m = strlen(needle);
        for (int iter = 0; iter < 3000; iter++) {
            size_t offset = next() % 16, n = next() % 100;
            char* hay = buf + offset;
            for (size_t i = 0; i < n; i++) hay[i] = alphabet[next() % 5];
            size_t expect = str_find(hay, n, needle, m);
            re2::StringPiece text(hay, n), sub;
            bool found = re.Match(text, 0, n, re2::RE2::UNANCHORED, &sub, 1);
            ASSERT_EQ(found, expect != STR_NPOS) << needle << " n=" << n << " offset=" << offset;
            if (found) ASSERT_EQ((size_t)(sub.data() - hay), expect) << needle << " n=" << n;
        }
    }
}

TEST(GrepGlob, GitignoreSemantics) {
    struct { const char* glob; const char* text; bool expect; } cases[] = {
        {"*.ls", "a.ls", true},
        {"*.ls", "dir/a.ls", false},         // '*' stops at '/'
        {"**/a.ls", "a.ls", true},
        {"**/a.ls", "x/y/a.ls", true},
        {"x/**", "x/y/z", true},
        {"x/**", "x", false},
        {"a/**/b", "a/b", true},
        {"a/**/b", "a/x/y/b", true},
        {"a/**/b", "a/xb", false},
        {"a?c", "abc", true},
        {"a?c", "a/c", false},
        {"[a-c]x", "bx", true},
        {"[!a-c]x", "bx", false},
        {"[]]x", "]x", true},
        {"\\*x", "*x", true},
        {"\\*x", "ax", false},
        {"foo**bar", "fooXbar", true},
        {"[abc", "[abc", true},             // an unterminated class is literal
    };
    for (auto& tc : cases) {
        EXPECT_EQ(grep_glob_match(tc.glob, strlen(tc.glob), tc.text, strlen(tc.text)), tc.expect)
            << tc.glob << " vs " << tc.text;
    }
}

// ── directory walk ─────────────────────────────────────────────────────

void put(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    ASSERT_NE(f, nullptr) << path;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

class GrepWalkTest : public ::testing::Test {
protected:
    static constexpr const char* ROOT = "temp/grep_gtest_tree";
    void SetUp() override {
        file_delete_recursive(ROOT);
        const char* dirs[] = {"", "/src", "/src/deep", "/src/deep/er", "/node_modules", "/build",
                              "/.hidden", "/logs", "/target"};
        for (const char* d : dirs) {
            char p[256];
            snprintf(p, sizeof(p), "%s%s", ROOT, d);
            create_dir_recursive(p);
        }
        char p[256];
        auto at = [&](const char* rel) { snprintf(p, sizeof(p), "%s/%s", ROOT, rel); return p; };
        // the tree is its own repository, so the enclosing checkout's
        // .gitignore does not reach it
        snprintf(p, sizeof(p), "%s/.git", ROOT);
        create_dir_recursive(p);
        put(at(".gitignore"), "# comment\n*.log\nbuild/\n!keep.log\n/rootonly.txt\n*.secret\n");
        put(at("src/x.secret"), "needle secret\n");
        put(at("a.txt"), "needle one\nneedle two\n");
        put(at("rootonly.txt"), "needle\n");
        put(at("keep.log"), "needle kept\n");
        put(at("drop.log"), "needle dropped\n");
        put(at("src/main.ls"), "let x = 1 // needle\n");
        put(at("src/rootonly.txt"), "needle in src\n");       // "/rootonly.txt" is anchored
        put(at("src/.ignore"), "*.tmp\n");
        put(at("src/scratch.tmp"), "needle tmp\n");
        put(at("src/deep/b.md"), "needle deep\n");
        put(at("src/deep/er/c.md"), "needle deeper\n");
        put(at("node_modules/pkg.js"), "needle\n");
        put(at("build/out.txt"), "needle\n");
        put(at(".hidden/h.txt"), "needle\n");
        put(at(".dotfile"), "needle\n");
        put(at("logs/x.txt"), "needle in logs\n");
        put(at("target/t.txt"), "needle in target\n");       // ambiguous names are not skipped
#ifndef _WIN32
        symlink("../a.txt", at("src/link.txt"));
#endif
    }
    void TearDown() override { file_delete_recursive(ROOT); }

    void run(const GrepWalkOptions& w, Collector* c, const char* pattern = "needle",
             const GrepOptions* o = NULL, const char* const* roots = NULL, size_t nroots = 0) {
        GrepOptions dflt = opts();
        GrepMatcher* m = make(pattern, o ? *o : dflt);
        ASSERT_NE(m, nullptr);
        const char* only[1] = {ROOT};
        GrepSink sink = sink_for(c);
        EXPECT_EQ(grep_search_paths(m, roots ? roots : only, roots ? nroots : 1, &w, &sink), GREP_OK);
        grep_matcher_destroy(m);
    }
};

GrepWalkOptions walk_opts() {
    GrepWalkOptions w;
    memset(&w, 0, sizeof(w));
    w.max_depth = -1;
    w.sorted = true;
    return w;
}

TEST_F(GrepWalkTest, DefaultLayersAndPathOrder) {
    Collector c;
    run(walk_opts(), &c);
    const char* expect[] = {
        "temp/grep_gtest_tree/a.txt", "temp/grep_gtest_tree/a.txt",
        "temp/grep_gtest_tree/keep.log",
        "temp/grep_gtest_tree/logs/x.txt",
        "temp/grep_gtest_tree/src/deep/b.md",
        "temp/grep_gtest_tree/src/deep/er/c.md",
        "temp/grep_gtest_tree/src/main.ls",
        "temp/grep_gtest_tree/src/rootonly.txt",
        "temp/grep_gtest_tree/target/t.txt",
    };
    ASSERT_EQ(c.n, (int)(sizeof(expect) / sizeof(expect[0])));
    for (int i = 0; i < c.n; i++) EXPECT_STREQ(c.recs[i].path, expect[i]) << i;
    EXPECT_EQ(c.files_done, 8);
}

TEST_F(GrepWalkTest, LayersCanBeTurnedOff) {
    GrepWalkOptions w = walk_opts();
    w.hidden = true;
    w.no_ignore = true;
    Collector c;
    run(w, &c);
    // everything but the symlink (GRP19)
    EXPECT_EQ(c.n, 17);
    for (int i = 0; i < c.n; i++) EXPECT_EQ(strstr(c.recs[i].path, "link.txt"), nullptr);
}

TEST_F(GrepWalkTest, CallerFilters) {
    GrepWalkOptions w = walk_opts();
    const char* inc[] = {"*.md"};
    w.include_globs = inc;
    w.include_count = 1;
    w.max_depth = 3;
    Collector c;
    run(w, &c);
    ASSERT_EQ(c.n, 1);   // er/c.md is at depth 4
    EXPECT_STREQ(c.recs[0].path, "temp/grep_gtest_tree/src/deep/b.md");

    w = walk_opts();
    const char* exc[] = {"src", "*.txt"};
    w.exclude_globs = exc;
    w.exclude_count = 2;
    Collector c2;
    run(w, &c2);
    ASSERT_EQ(c2.n, 1);
    EXPECT_STREQ(c2.recs[0].path, "temp/grep_gtest_tree/keep.log");
}

TEST_F(GrepWalkTest, ExplicitRootsBypassRules) {
    const char* roots[] = {"temp/grep_gtest_tree/drop.log", "temp/grep_gtest_tree/node_modules",
#ifndef _WIN32
                           "temp/grep_gtest_tree/src/link.txt",
#endif
    };
    Collector c;
    run(walk_opts(), &c, "needle", NULL, roots, sizeof(roots) / sizeof(roots[0]));
#ifndef _WIN32
    ASSERT_EQ(c.n, 4);
    EXPECT_STREQ(c.recs[2].path, "temp/grep_gtest_tree/src/link.txt");
#else
    ASSERT_EQ(c.n, 2);
#endif
    EXPECT_STREQ(c.recs[0].path, "temp/grep_gtest_tree/drop.log");
    EXPECT_STREQ(c.recs[1].path, "temp/grep_gtest_tree/node_modules/pkg.js");
}

TEST_F(GrepWalkTest, MissingRootFails) {
    GrepMatcher* m = make("x", opts());
    const char* roots[] = {"temp/grep_gtest_tree/nope"};
    Collector c;
    GrepSink sink = sink_for(&c);
    GrepWalkOptions w = walk_opts();
    EXPECT_EQ(grep_search_paths(m, roots, 1, &w, &sink), GREP_ERR_IO);
    grep_matcher_destroy(m);
}

// G4 gate: a sorted search with a total limit gives the same matches on every
// thread count; per-file limits apply on top
TEST_F(GrepWalkTest, LimitsAreDeterministic) {
    Collector first;
    for (int threads = 1; threads <= 8; threads++) {
        GrepWalkOptions w = walk_opts();
        w.threads = threads;
        w.max_matches_total = 4;
        Collector c;
        run(w, &c);
        ASSERT_EQ(c.n, 4) << threads;
        // a.txt (2), keep.log, logs/x.txt: the file the limit ends still
        // reports file_done
        EXPECT_EQ(c.files_done, 3) << threads;
        if (threads == 1) first = c;
        for (int i = 0; i < 4; i++) {
            EXPECT_STREQ(c.recs[i].path, first.recs[i].path);
            EXPECT_EQ(c.recs[i].byte_offset, first.recs[i].byte_offset);
        }
    }
    GrepWalkOptions w = walk_opts();
    GrepOptions o = opts();
    o.max_matches_per_file = 1;
    Collector c;
    run(w, &c, "needle", &o);
    EXPECT_EQ(c.n, 8);   // a.txt contributes one instead of two
}

TEST_F(GrepWalkTest, UnsortedLimitStillReportsTheCutFile) {
    GrepWalkOptions w = walk_opts();
    w.sorted = false;
    w.threads = 1;
    w.max_matches_total = 1;
    Collector c;
    run(w, &c);
    EXPECT_EQ(c.n, 1);
    EXPECT_EQ(c.files_done, 1);
    EXPECT_STREQ(c.done_paths[0], c.recs[0].path);
}

TEST_F(GrepWalkTest, UnsortedDeliversEveryMatch) {
    GrepWalkOptions w = walk_opts();
    w.sorted = false;
    w.threads = 4;
    Collector c;
    run(w, &c);
    EXPECT_EQ(c.n, 9);
    EXPECT_EQ(c.files_done, 8);
}

// the ignore files of the repository enclosing a root apply below it
TEST_F(GrepWalkTest, AncestorIgnoreFilesApply) {
    const char* roots[] = {"temp/grep_gtest_tree/src"};
    Collector c;
    run(walk_opts(), &c, "needle", NULL, roots, 1);
    // x.secret is ignored from the tree's root .gitignore; the anchored
    // "/rootonly.txt" there does not reach src/rootonly.txt
    const char* expect[] = {
        "temp/grep_gtest_tree/src/deep/b.md",
        "temp/grep_gtest_tree/src/deep/er/c.md",
        "temp/grep_gtest_tree/src/main.ls",
        "temp/grep_gtest_tree/src/rootonly.txt",
    };
    ASSERT_EQ(c.n, 4);
    for (int i = 0; i < 4; i++) EXPECT_STREQ(c.recs[i].path, expect[i]) << i;
}

}  // namespace
