// test_fts_gtest.cpp — lib/fts (vibe/Lambda_IO_Fulltext_Search.md).
//
// Tokens (FTX3, FTX4), the query language (FTX6, FTX13), BM25 against the
// formula computed here (FTX7, FTX8), documents (FTX2), limits (FTX9), the
// prefilter's soundness under case folding (§7.2), and a parallel walk giving
// the same result as one thread (FTX10).

#include <gtest/gtest.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../lib/fts/fts.h"
#include "../../lib/fts/fts_internal.hpp"
#include "../../lib/file.h"
#include "../../lib/file_utils.h"
#include "../../lib/str.h"

namespace {

FtsOptions fts_opts() {
    FtsOptions o;
    memset(&o, 0, sizeof(o));
    o.ignore_case = true;
    o.word = true;
    o.unit = FTS_UNIT_FILE;
    o.rank = FTS_RANK_BM25;
    return o;
}

// the normal forms of `text`'s tokens, joined by '|'; stop words as "_"
void tokens_of(const char* text, char* out, size_t cap, bool fold = true, bool unaccent = false) {
    FtsTokenizer t;
    fts_tokenizer_init(&t, fold, unaccent);
    ASSERT_TRUE(fts_tokenize(&t, text, strlen(text)));
    size_t n = 0;
    out[0] = '\0';
    for (size_t i = 0; i < t.count; i++) {
        const FtsToken* tok = &t.tokens[i];
        n += (size_t)snprintf(out + n, cap - n, "%s%.*s", i ? "|" : "", (int)tok->norm_length, t.norm + tok->norm);
    }
    fts_tokenizer_release(&t);
}

TEST(FtsTokens, WordsDigitsAndSeparators) {
    char out[512];
    tokens_of("Full-text search, don't stop: v2 x_y", out, sizeof(out));
    EXPECT_STREQ(out, "full|text|search|don|t|stop|v2|x|y");
}

TEST(FtsTokens, CjkAndHangulAreUnigrams) {
    char out[512];
    tokens_of("全文检索 works", out, sizeof(out));
    EXPECT_STREQ(out, "全|文|检|索|works");
    tokens_of("학교에서 공부", out, sizeof(out));
    EXPECT_STREQ(out, "학|교|에|서|공|부");
    tokens_of("カタカナとひらがな", out, sizeof(out));
    EXPECT_STREQ(out, "カ|タ|カ|ナ|と|ひ|ら|が|な");
}

TEST(FtsTokens, SimpleCaseFoldingOrbits) {
    char out[512];
    // KELVIN SIGN folds with k, LONG S with s, capital sharp s with ß (S17.7.1)
    tokens_of("\xE2\x84\xAA" "elvin \xC5\xBF" "un STRAẞE Straße ΣΑΣ", out, sizeof(out));
    EXPECT_STREQ(out, "kelvin|sun|straße|straße|σασ");
    tokens_of("MiXeD", out, sizeof(out), false);
    EXPECT_STREQ(out, "MiXeD");
}

TEST(FtsTokens, MarksAttachAndUnaccent) {
    char out[512];
    // a combining acute stays in its word; unaccent drops it, precomposed or not
    tokens_of("cafe\xCC\x81 café cafe", out, sizeof(out));
    EXPECT_STREQ(out, "cafe\xCC\x81|café|cafe");
    tokens_of("cafe\xCC\x81 café cafe", out, sizeof(out), true, true);
    EXPECT_STREQ(out, "cafe|cafe|cafe");
    // Hangul decomposes into jamo, which are letters: it recomposes whole
    tokens_of("한국", out, sizeof(out), true, true);
    EXPECT_STREQ(out, "한|국");
}

TEST(FtsTokens, OffsetsCountCodePointsAsStrUtf8Count) {
    FtsTokenizer t;
    fts_tokenizer_init(&t, true, false);
    const char* text = "é \xFF" "ab \x80" "cd 文x";
    ASSERT_TRUE(fts_tokenize(&t, text, strlen(text)));
    ASSERT_EQ(t.count, 5u);
    for (size_t i = 0; i < t.count; i++) {
        EXPECT_EQ(t.tokens[i].chars, str_utf8_count(text, t.tokens[i].start)) << i;
    }
    fts_tokenizer_release(&t);
}

// does `query` match the document `text`?
bool matches(const char* query, const char* text, FtsOptions o = fts_opts()) {
    FtsQuery* q = NULL;
    EXPECT_EQ(fts_query_create(query, strlen(query), &o, &q, NULL, 0), FTS_OK);
    if (!q) return false;
    FtsTokenizer t;
    fts_tokenizer_init(&t, o.ignore_case, o.unaccent);
    EXPECT_TRUE(fts_tokenize(&t, text, strlen(text)));
    fts_mark_stop(q, &t, 0, t.count);
    FtsEval e;
    fts_eval_init(&e);
    bool oom = false;
    bool m = fts_eval_document(q, &t, 0, t.count, &e, &oom);
    EXPECT_FALSE(oom);
    fts_eval_release(&e);
    fts_tokenizer_release(&t);
    fts_query_destroy(q);
    return m;
}

TEST(FtsQuery, BooleanPhraseAndGrouping) {
    EXPECT_TRUE(matches("full text", "text that is full"));
    EXPECT_FALSE(matches("\"full text\"", "text that is full"));
    EXPECT_TRUE(matches("\"full-text\"", "a full, text search"));
    EXPECT_TRUE(matches("lambda or scheme", "a scheme dialect"));
    EXPECT_TRUE(matches("lambda OR scheme", "lambda only"));
    EXPECT_FALSE(matches("search -draft", "a search draft"));
    EXPECT_TRUE(matches("search -\"to do\"", "search to the end, do it"));
    EXPECT_FALSE(matches("search -\"to do\"", "search: to do"));
    EXPECT_TRUE(matches("(lambda or scheme) jit", "scheme with a jit"));
    EXPECT_FALSE(matches("(lambda or scheme) jit", "scheme interpreted"));
    EXPECT_TRUE(matches("-draft", "final text"));
    EXPECT_TRUE(matches("SEARCH", "search"));
    FtsOptions exact = fts_opts();
    exact.ignore_case = false;
    EXPECT_FALSE(matches("SEARCH", "search", exact));
}

TEST(FtsQuery, PartOfTokenTerms) {
    EXPECT_TRUE(matches("pars*", "a parser"));
    EXPECT_FALSE(matches("pars*", "sparse"));
    EXPECT_TRUE(matches("*port", "import"));
    EXPECT_FALSE(matches("*port", "portable"));
    EXPECT_TRUE(matches("*port*", "reporting"));
    EXPECT_FALSE(matches("port", "import"));
    FtsOptions part = fts_opts();
    part.word = false;  // a bare term matches any part; `*` keeps its meaning
    EXPECT_TRUE(matches("port", "import", part));
    EXPECT_FALSE(matches("*port", "portable", part));
    EXPECT_TRUE(matches("\"*port scan*\"", "import scanner"));
}

TEST(FtsQuery, NeverFailsToParse) {
    const char* odd[] = {"\"unbalanced quote", "stray ) paren", "((nested", "-", "- -", "or", "or a or",
                         "*", "**", "\"\"", "()", ")(", "a -", "-(", "\xFF\xFE", ""};
    FtsOptions o = fts_opts();
    for (const char* s : odd) {
        FtsQuery* q = NULL;
        EXPECT_EQ(fts_query_create(s, strlen(s), &o, &q, NULL, 0), FTS_OK) << s;
        fts_query_destroy(q);
    }
    EXPECT_TRUE(matches("\"unbalanced quote", "an unbalanced quote"));
    EXPECT_TRUE(matches("stray ) paren", "stray paren"));
    EXPECT_FALSE(matches("stray ) paren", "stray only"));
    EXPECT_FALSE(matches("", "anything"));
    EXPECT_FALSE(matches("*", "anything"));
}

TEST(FtsQuery, StopWordsHoldPositions) {
    FtsOptions o = fts_opts();
    const char* stop[] = {"the", "of"};
    o.stopwords = stop;
    o.stopword_count = 2;
    EXPECT_TRUE(matches("\"bank of england\"", "the Bank of England", o));
    EXPECT_TRUE(matches("\"bank of england\"", "bank the england", o));
    EXPECT_FALSE(matches("\"bank of england\"", "bank england", o));
    EXPECT_FALSE(matches("the", "the end", o));  // a lone stop word is dropped
}

TEST(FtsQuery, LanguageWithoutStemmerIsAnError) {
    FtsOptions o = fts_opts();
    o.language = "klingon";
    FtsQuery* q = NULL;
    char err[128];
    EXPECT_EQ(fts_query_create("x", 1, &o, &q, err, sizeof(err)), FTS_ERR_LANGUAGE);
    EXPECT_EQ(q, nullptr);
}

// ── searching files ────────────────────────────────────────────────────

void put(const char* path, const char* text, size_t length = (size_t)-1) {
    FILE* f = fopen(path, "wb");
    ASSERT_NE(f, nullptr);
    fwrite(text, 1, length == (size_t)-1 ? strlen(text) : length, f);
    fclose(f);
}

class FtsSearchTest : public ::testing::Test {
protected:
    static constexpr const char* ROOT = "temp/fts_gtest_tree";
    char p[256];
    const char* at(const char* rel) { snprintf(p, sizeof(p), "%s/%s", ROOT, rel); return p; }
    void SetUp() override {
        file_delete_recursive(ROOT);
        create_dir_recursive(ROOT);
        create_dir_recursive(at(".git"));  // its own repository: outer ignore files stay out
        create_dir_recursive(at("sub"));
        put(at("a.txt"), "apple banana apple\n");                     // 19 bytes
        put(at("b.txt"), "banana cherry\n");                          // 14 bytes
        put(at("c.txt"), "cherry date elderberry fig grape\n");       // 33 bytes
        put(at("sub/d.md"), "Apple pie.\n\nBanana split and apple.\nmore apple\n");
        put(at("kelvin.txt"), "\xE2\x84\xAA" "iwi\n");                // KELVIN SIGN + iwi
        put(at("bin.dat"), "apple\0apple", 11);
    }
    void TearDown() override { file_delete_recursive(ROOT); }

    FtsSearch* run(const char* query, const FtsOptions& o, FtsQuery** qout, int threads = 0) {
        FtsQuery* q = NULL;
        EXPECT_EQ(fts_query_create(query, strlen(query), &o, &q, NULL, 0), FTS_OK);
        FtsSearch* s = fts_search_create(q);
        GrepWalkOptions w;
        memset(&w, 0, sizeof(w));
        w.max_depth = -1;
        w.threads = threads;
        const char* roots[1] = {ROOT};
        EXPECT_EQ(fts_search_add(s, roots, 1, 0, &w), FTS_OK);
        *qout = q;
        return s;
    }
};

double bm25(double tf, double df, double n, double len, double avglen) {
    double idf = log(1.0 + (n - df + 0.5) / (df + 0.5));
    return idf * tf * 2.2 / (tf + 1.2 * (0.25 + 0.75 * len / avglen));
}

TEST_F(FtsSearchTest, Bm25MatchesTheFormulaOverBytes) {
    FtsOptions o = fts_opts();
    FtsQuery* q;
    FtsSearch* s = run("apple", o, &q);
    const FtsHit* hits;
    size_t n;
    ASSERT_EQ(fts_search_finish(s, 0, 0, &hits, &n), FTS_OK);
    // corpus: every text file (the binary one is skipped); N and the lengths
    // count files the prefilter skipped too
    const char* corpus[] = {"a.txt", "b.txt", "c.txt", "sub/d.md", "kelvin.txt"};
    double total = 0;
    for (const char* f : corpus) total += (double)file_size(at(f));
    double avg = total / 5;
    double d_len = (double)file_size(at("sub/d.md"));
    ASSERT_EQ(n, 2u);
    EXPECT_STREQ(hits[0].path, "temp/fts_gtest_tree/a.txt");
    EXPECT_NEAR(hits[0].score, bm25(2, 2, 5, 19, avg), 1e-12);
    EXPECT_STREQ(hits[1].path, "temp/fts_gtest_tree/sub/d.md");
    EXPECT_NEAR(hits[1].score, bm25(3, 2, 5, d_len, avg), 1e-12);
    fts_search_destroy(s);
    fts_query_destroy(q);
}

TEST_F(FtsSearchTest, ParagraphsAndLinesAreDocuments) {
    FtsOptions o = fts_opts();
    o.unit = FTS_UNIT_PARAGRAPH;
    FtsQuery* q;
    FtsSearch* s = run("apple banana", o, &q);
    const FtsHit* hits;
    size_t n;
    ASSERT_EQ(fts_search_finish(s, 0, 0, &hits, &n), FTS_OK);
    ASSERT_EQ(n, 2u);  // a.txt, and d.md's second paragraph
    const FtsHit* d = strstr(hits[0].path, "d.md") ? &hits[0] : &hits[1];
    EXPECT_EQ(d->line, 3u);
    EXPECT_EQ(d->byte_offset, 12u);
    EXPECT_EQ(d->byte_length, 34u);  // "Banana split and apple.\nmore apple"
    EXPECT_EQ(d->char_offset, 12u);
    fts_search_destroy(s);
    fts_query_destroy(q);

    o.unit = FTS_UNIT_LINE;
    o.rank = FTS_RANK_NONE;
    s = run("apple", o, &q);
    ASSERT_EQ(fts_search_finish(s, 0, 0, &hits, &n), FTS_OK);
    ASSERT_EQ(n, 4u);  // path order, then position
    EXPECT_STREQ(hits[0].path, "temp/fts_gtest_tree/a.txt");
    EXPECT_EQ(hits[1].line, 1u);
    EXPECT_EQ(hits[2].line, 3u);
    EXPECT_EQ(hits[3].line, 4u);
    EXPECT_EQ(hits[3].line_ending, GREP_EOL_LF);
    fts_search_destroy(s);
    fts_query_destroy(q);
}

TEST_F(FtsSearchTest, LimitsKeepTheBest) {
    FtsOptions o = fts_opts();
    o.unit = FTS_UNIT_LINE;
    FtsQuery* q;
    FtsSearch* s = run("apple", o, &q);
    const FtsHit* hits;
    size_t n;
    ASSERT_EQ(fts_search_finish(s, 0, 1, &hits, &n), FTS_OK);
    EXPECT_EQ(n, 2u);  // one line per file
    ASSERT_EQ(fts_search_finish(s, 1, 0, &hits, &n), FTS_OK);
    EXPECT_EQ(n, 1u);
    fts_search_destroy(s);
    fts_query_destroy(q);
}

TEST_F(FtsSearchTest, PrefilterFindsEveryFoldedSpelling) {
    // the prefilter literal is the folded "kiwi"; RE2's (?i) finds the KELVIN
    // SIGN spelling, so the file is not skipped
    FtsOptions o = fts_opts();
    FtsQuery* q;
    FtsSearch* s = run("KIWI", o, &q);
    ASSERT_NE(q->prefilter, nullptr);
    const FtsHit* hits;
    size_t n;
    ASSERT_EQ(fts_search_finish(s, 0, 0, &hits, &n), FTS_OK);
    ASSERT_EQ(n, 1u);
    EXPECT_STREQ(hits[0].path, "temp/fts_gtest_tree/kelvin.txt");
    fts_search_destroy(s);
    fts_query_destroy(q);
}

TEST_F(FtsSearchTest, NoPrefilterWhenAbsenceCanMatch) {
    FtsOptions o = fts_opts();
    FtsQuery* q = NULL;
    ASSERT_EQ(fts_query_create("-apple", 6, &o, &q, NULL, 0), FTS_OK);
    EXPECT_EQ(q->prefilter, nullptr);
    fts_query_destroy(q);
    ASSERT_EQ(fts_query_create("fig or -apple", 13, &o, &q, NULL, 0), FTS_OK);
    EXPECT_EQ(q->prefilter, nullptr);
    fts_query_destroy(q);
    ASSERT_EQ(fts_query_create("fig -apple", 10, &o, &q, NULL, 0), FTS_OK);
    EXPECT_NE(q->prefilter, nullptr);
    fts_query_destroy(q);
}

TEST_F(FtsSearchTest, ThreadsDoNotChangeTheResult) {
    FtsOptions o = fts_opts();
    o.unit = FTS_UNIT_LINE;
    FtsQuery* q1;
    FtsQuery* q8;
    FtsSearch* one = run("apple or cherry", o, &q1, 1);
    FtsSearch* eight = run("apple or cherry", o, &q8, 8);
    const FtsHit* a;
    const FtsHit* b;
    size_t na, nb;
    ASSERT_EQ(fts_search_finish(one, 0, 0, &a, &na), FTS_OK);
    ASSERT_EQ(fts_search_finish(eight, 0, 0, &b, &nb), FTS_OK);
    ASSERT_EQ(na, nb);
    for (size_t i = 0; i < na; i++) {
        EXPECT_STREQ(a[i].path, b[i].path);
        EXPECT_EQ(a[i].byte_offset, b[i].byte_offset);
        EXPECT_EQ(a[i].score, b[i].score);  // exactly: same sums, same order
    }
    fts_search_destroy(one);
    fts_search_destroy(eight);
    fts_query_destroy(q1);
    fts_query_destroy(q8);
}

TEST_F(FtsSearchTest, MissingRootFails) {
    FtsOptions o = fts_opts();
    FtsQuery* q = NULL;
    ASSERT_EQ(fts_query_create("x", 1, &o, &q, NULL, 0), FTS_OK);
    FtsSearch* s = fts_search_create(q);
    const char* roots[1] = {"temp/fts_gtest_tree/none"};
    EXPECT_EQ(fts_search_add(s, roots, 1, 0, NULL), FTS_ERR_IO);
    fts_search_destroy(s);
    fts_query_destroy(q);
}

// ── a returned document's extras ───────────────────────────────────────

struct Spans {
    FtsSpan s[16];
    int n = 0;
};

bool on_span(void* ud, const FtsSpan* span) {
    Spans* c = (Spans*)ud;
    if (c->n < 16) c->s[c->n++] = *span;
    return true;
}

TEST(FtsExtras, MatchesAndSnippet) {
    FtsOptions o = fts_opts();
    FtsQuery* q = NULL;
    const char* query = "\"full text\" ranking -draft";
    ASSERT_EQ(fts_query_create(query, strlen(query), &o, &q, NULL, 0), FTS_OK);
    const char* doc = "é Full text search and ranking. Full text again, not text.";
    Spans c;
    ASSERT_EQ(fts_document_matches(q, doc, strlen(doc), on_span, &c), FTS_OK);
    ASSERT_EQ(c.n, 5);  // full text, ranking, full text; the lone "text" is no phrase
    EXPECT_EQ(c.s[0].byte_offset, 3u);
    EXPECT_EQ(c.s[0].char_offset, 2u);
    EXPECT_EQ(c.s[2].byte_length, 7u);
    size_t start, end;
    bool before, after;
    ASSERT_EQ(fts_document_snippet(q, doc, strlen(doc), 5, &start, &end, &before, &after), FTS_OK);
    EXPECT_EQ(memcmp(doc + start, "Full text search and ranking", end - start), 0);
    EXPECT_TRUE(before);
    EXPECT_TRUE(after);
    fts_query_destroy(q);
}

}  // namespace
