// test_input_sourcepos_gtest.cpp — the source editor's windowed highlight parse
// (vibe/radiant/Radiant_Design_Source_Editor.md §11 ring 2; CED15v2, CED16v3,
// CED17, CED18v2):
//   (a) a span's text is its construct as written;
//   (b) a window parse covers every line of its window with the same span
//       kinds and columns as a full parse, over the Markdown corpus;
//   (c) recording spans leaves the parse tree byte-identical;
//   (d) the HTML lexer agrees with the tree parser on tag names and
//       attribute values;
//   (e) after random edits, a parse with the cache carried through the
//       editor's realignment equals a parse without a cache.

#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>

#include "../lambda/lambda-data.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/markup/markup_highlight.hpp"
#include "../lambda/input/markup/markup_parser.hpp"
#include "../lambda/input/html5/html5_parser.h"
#include "../lambda/io/input-allocation-context.h"
#include "../lambda/core/mark_reader.hpp"
#include "../lib/arraylist.h"
#include "../lib/arraylist.hpp"
#include "../lib/file.h"
#include "../lib/file_utils.h"
#include "../lib/memtrack.h"
#include "../lib/mem_factory.h"
#include "../lib/log.h"
#include "../lib/url.h"

using namespace lambda::markup;

extern "C" {
    String* format_data(Item item, String* type, String* flavor, Pool* pool);
    Url* get_current_dir(void);
    Url* parse_url(Url* base, const char* url);
}

static const char* MARKDOWN_CORPUS = "test/markdown";

// ---------------------------------------------------------------------------
// A document as the editor holds it: lines in chunks, read in place.
// ---------------------------------------------------------------------------

struct TestDoc {
    lam::ArrayList<char*> lines;
    lam::ArrayList<int64_t> sizes;    // lines per chunk
    lam::ArrayList<int64_t> starts;   // first line of each chunk
};

static char* dup_line(const char* s, size_t n) {
    char* line = (char*)mem_alloc(n + 1, MEM_CAT_TEMP);
    memcpy(line, s, n);
    line[n] = '\0';
    return line;
}

static void doc_restart(TestDoc* d) {
    d->starts.clear();
    int64_t at = 0;
    for (size_t k = 0; k < d->sizes.length(); k++) {
        d->starts.push_back(at);
        at += d->sizes[k];
    }
}

// Split `text` on newlines (dropping a trailing CR) into chunks of `chunk` lines.
static void doc_load(TestDoc* d, const char* text, int64_t chunk) {
    const char* line = text;
    for (const char* p = text; ; p++) {
        if (*p == '\n' || *p == '\0') {
            size_t n = (size_t)(p - line);
            if (n > 0 && line[n - 1] == '\r') n--;
            d->lines.push_back(dup_line(line, n));
            if (*p == '\0') break;
            line = p + 1;
        }
    }
    for (int64_t at = 0; at < (int64_t)d->lines.length(); at += chunk) {
        int64_t rest = (int64_t)d->lines.length() - at;
        d->sizes.push_back(rest < chunk ? rest : chunk);
    }
    doc_restart(d);
}

static void doc_free(TestDoc* d) {
    for (size_t i = 0; i < d->lines.length(); i++) mem_free(d->lines[i]);
    d->lines.clear();
}

static const char* doc_line(void* ctx, int64_t index, size_t* len) {
    TestDoc* d = (TestDoc*)ctx;
    const char* line = index >= 0 && index < (int64_t)d->lines.length() ? d->lines[(size_t)index] : "";
    *len = strlen(line);
    return line;
}

static HighlightLines doc_source(TestDoc* d) {
    return HighlightLines{d, doc_line, (int64_t)d->lines.length(), d->starts.data(),
                          (int64_t)d->starts.length()};
}

static const char* kind_of(const HighlightResult& r, const MarkupSpan& s) {
    return r.kinds[(size_t)s.kind].name;
}

// ---------------------------------------------------------------------------
// Coverage: per line, per span kind, the column intervals spans cover. Two
// parses that agree on it agree on every class the walker paints, whatever
// the extent of a container that reaches outside the window.
// ---------------------------------------------------------------------------

struct Cover {
    int64_t line;
    char kind[24];
    int32_t block;
    int32_t s, e;
};

static int cover_cmp(const void* a, const void* b) {
    const Cover* x = (const Cover*)a;
    const Cover* y = (const Cover*)b;
    if (x->line != y->line) return x->line < y->line ? -1 : 1;
    int k = strcmp(x->kind, y->kind);
    if (k != 0) return k;
    if (x->block != y->block) return x->block - y->block;
    if (x->s != y->s) return x->s - y->s;
    return x->e - y->e;
}

static void coverage(TestDoc* d, const HighlightResult& r, int64_t first, int64_t last,
                     lam::ArrayList<Cover>* out) {
    lam::ArrayList<Cover> raw;
    for (size_t i = 0; i < r.spans.length(); i++) {
        const MarkupSpan& sp = r.spans[i];
        int64_t lo = sp.line > first ? sp.line : first;
        int64_t hi = sp.end_line < last ? sp.end_line : last;
        for (int64_t l = lo; l <= hi; l++) {
            Cover c = {};
            c.line = l;
            strncpy(c.kind, kind_of(r, sp), sizeof(c.kind) - 1);
            c.block = sp.block;
            c.s = l == sp.line ? sp.col : 0;
            c.e = l == sp.end_line ? sp.end_col : (int32_t)strlen(d->lines[(size_t)l]);
            if (c.e > c.s) raw.push_back(c);
        }
    }
    if (raw.length() > 0) qsort(raw.data(), raw.length(), sizeof(Cover), cover_cmp);
    // union of the intervals of one (line, kind, block)
    out->clear();
    for (size_t i = 0; i < raw.length(); i++) {
        Cover c = raw[i];
        if (out->length() > 0) {
            Cover& top = out->back();
            if (top.line == c.line && strcmp(top.kind, c.kind) == 0 && top.block == c.block && c.s <= top.e) {
                if (c.e > top.e) top.e = c.e;
                continue;
            }
        }
        out->push_back(c);
    }
}

static bool same_coverage(const lam::ArrayList<Cover>& a, const lam::ArrayList<Cover>& b) {
    if (a.length() != b.length()) return false;
    for (size_t i = 0; i < a.length(); i++) {
        if (cover_cmp(&a[i], &b[i]) != 0) return false;
    }
    return true;
}

static bool same_spans(const HighlightResult& a, const HighlightResult& b) {
    if (a.spans.length() != b.spans.length()) return false;
    for (size_t i = 0; i < a.spans.length(); i++) {
        const MarkupSpan& x = a.spans[i];
        const MarkupSpan& y = b.spans[i];
        if (strcmp(kind_of(a, x), kind_of(b, y)) != 0 || x.block != y.block || x.line != y.line ||
            x.col != y.col || x.end_line != y.end_line || x.end_col != y.end_col) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Corpus
// ---------------------------------------------------------------------------

struct CorpusFile {
    char path[512];
};

static void corpus_files(const char* dir, const char* suffix, lam::ArrayList<CorpusFile>* out) {
    ArrayList* entries = dir_list(dir);
    if (!entries) return;
    for (int i = 0; i < entries->length; i++) {
        DirEntry* e = (DirEntry*)entries->data[i];
        size_t n = strlen(e->name), m = strlen(suffix);
        if (!e->is_dir && n > m && strcmp(e->name + n - m, suffix) == 0) {
            CorpusFile f = {};
            snprintf(f.path, sizeof(f.path), "%s/%s", dir, e->name);
            out->push_back(f);
        }
        dir_entry_free(e);
    }
    arraylist_free(entries);
    if (out->length() > 0) {
        qsort(out->data(), out->length(), sizeof(CorpusFile),
              [](const void* a, const void* b) { return strcmp(((const CorpusFile*)a)->path,
                                                               ((const CorpusFile*)b)->path); });
    }
}

class SourcePosTest : public ::testing::Test {
protected:
    void SetUp() override { log_init(NULL); }
};

// ---------------------------------------------------------------------------
// (b) window parse ≡ full parse restricted to the window
// ---------------------------------------------------------------------------

TEST_F(SourcePosTest, MarkdownWindowsMatchFullParseOverCorpus) {
    lam::ArrayList<CorpusFile> files;
    corpus_files(MARKDOWN_CORPUS, ".md", &files);
    if (files.length() == 0) GTEST_SKIP() << "no Markdown corpus at " << MARKDOWN_CORPUS;
    const int64_t W = 40, STEP = 17;
    int64_t lines_compared = 0, windows = 0, mismatched = 0;
    for (size_t f = 0; f < files.length(); f++) {
        char* text = read_text_file(files[f].path);
        if (!text) continue;
        TestDoc d;
        doc_load(&d, text, 256);
        mem_free(text);
        HighlightLines src = doc_source(&d);
        HighlightResult full;
        ASSERT_TRUE(markdown_highlight_window(&src, 0, src.count - 1, nullptr, &full)) << files[f].path;
        for (int64_t s = 0; s < src.count; s += STEP) {
            int64_t e = s + W - 1 < src.count - 1 ? s + W - 1 : src.count - 1;
            HighlightResult win;
            ASSERT_TRUE(markdown_highlight_window(&src, s, e, nullptr, &win));
            lam::ArrayList<Cover> a, b;
            coverage(&d, full, s, e, &a);
            coverage(&d, win, s, e, &b);
            windows++;
            lines_compared += e - s + 1;
            if (!same_coverage(a, b)) {
                mismatched++;
                ADD_FAILURE() << files[f].path << ": window " << s << "-" << e << " differs from the full parse";
            }
        }
        doc_free(&d);
    }
    log_info("sourcepos-gtest: %lld windows, %lld lines, %lld mismatched windows",
             (long long)windows, (long long)lines_compared, (long long)mismatched);
    RecordProperty("windows", (int)windows);
    RecordProperty("lines", (int)lines_compared);
    EXPECT_GT(windows, (int64_t)files.length());
    EXPECT_EQ(mismatched, 0);
}

// ---------------------------------------------------------------------------
// (a) a span's text is the construct as written
// ---------------------------------------------------------------------------

static bool starts_with(const char* s, int32_t from, const char* prefix) {
    return strncmp(s + from, prefix, strlen(prefix)) == 0;
}

// Code points to bytes within one line.
static int32_t byte_at(const char* line, int32_t col) {
    int32_t b = 0, c = 0;
    while (line[b] && c < col) {
        b++;
        while ((((unsigned char)line[b]) & 0xC0) == 0x80) b++;
        c++;
    }
    return b;
}

// The single-line inline construct [s, e) of `line` reads as `kind`.
static bool markdown_text_matches(const char* kind, const char* line, int32_t s, int32_t e) {
    int32_t bs = byte_at(line, s), be = byte_at(line, e);
    char last = be > bs ? line[be - 1] : '\0';
    if (strcmp(kind, "code") == 0) return line[bs] == '`' && last == '`';
    if (strcmp(kind, "strong") == 0) return (starts_with(line, bs, "**") || starts_with(line, bs, "__")) &&
                                            (last == '*' || last == '_');
    if (strcmp(kind, "em") == 0) return (line[bs] == '*' || line[bs] == '_') && (last == '*' || last == '_');
    if (strcmp(kind, "del") == 0) return line[bs] == '~' && last == '~';
    if (strcmp(kind, "img") == 0) return starts_with(line, bs, "![");
    if (strcmp(kind, "a") == 0) return line[bs] == '[' || line[bs] == '<' || line[bs] == 'h' || line[bs] == 'w';
    return true;
}

TEST_F(SourcePosTest, MarkdownSpanTextIsTheConstruct) {
    lam::ArrayList<CorpusFile> files;
    corpus_files(MARKDOWN_CORPUS, ".md", &files);
    if (files.length() == 0) GTEST_SKIP() << "no Markdown corpus at " << MARKDOWN_CORPUS;
    int64_t checked = 0;
    for (size_t f = 0; f < files.length(); f++) {
        char* text = read_text_file(files[f].path);
        if (!text) continue;
        TestDoc d;
        doc_load(&d, text, 256);
        mem_free(text);
        HighlightLines src = doc_source(&d);
        HighlightResult full;
        ASSERT_TRUE(markdown_highlight_window(&src, 0, src.count - 1, nullptr, &full));
        for (size_t i = 0; i < full.spans.length(); i++) {
            const MarkupSpan& sp = full.spans[i];
            if (sp.block || sp.line != sp.end_line) continue;
            const char* line = d.lines[(size_t)sp.line];
            checked++;
            EXPECT_TRUE(markdown_text_matches(kind_of(full, sp), line, sp.col, sp.end_col))
                << files[f].path << ":" << sp.line + 1 << " " << kind_of(full, sp) << " [" << sp.col << ", "
                << sp.end_col << ") in: " << line;
        }
        doc_free(&d);
    }
    EXPECT_GT(checked, 0);
}

// ---------------------------------------------------------------------------
// (c) recording spans leaves the parse tree byte-identical
// ---------------------------------------------------------------------------

// The JSON of a Markdown parse of `text`, with or without a span sink.
static char* markdown_json(const char* text, bool with_sink) {
    Pool* pool = mem_pool_create(NULL, MEM_ROLE_INPUT, "sourcepos.gtest");
    Input* input = Input::create(pool, nullptr, nullptr);
    InputAllocationContext allocation = {pool, input->arena, false, input};
    InputAllocationContext* saved = input_allocation_context;
    input_allocation_context = &allocation;
    ParseConfig cfg;
    cfg.format = Format::MARKDOWN;
    MarkupParser* parser = markup_parser_create(input, cfg);
    MarkupSpanSink sink;
    if (with_sink) parser->span_sink = &sink;
    Item tree = parser->parseContent(text);
    while (with_sink && sink.line_maps.length() > 0) {
        highlight_pop_lines(parser, sink.line_maps[sink.line_maps.length() - 1].lines);
    }
    String* type = (String*)mem_alloc(sizeof(String) + 5, MEM_CAT_TEMP);
    type->len = 4;
    memcpy(type->chars, "json", 5);
    String* json = format_data(tree, type, NULL, pool);
    char* out = json ? dup_line(json->chars, json->len) : nullptr;
    mem_free(type);
    markup_parser_destroy(parser);
    input_allocation_context = saved;
    mem_pool_destroy(pool);
    return out;
}

TEST_F(SourcePosTest, SpanRecordingLeavesTheTreeUnchanged) {
    lam::ArrayList<CorpusFile> files;
    corpus_files(MARKDOWN_CORPUS, ".md", &files);
    if (files.length() == 0) GTEST_SKIP() << "no Markdown corpus at " << MARKDOWN_CORPUS;
    for (size_t f = 0; f < files.length(); f++) {
        char* text = read_text_file(files[f].path);
        if (!text) continue;
        char* plain = markdown_json(text, false);
        char* recorded = markdown_json(text, true);
        ASSERT_TRUE(plain && recorded) << files[f].path;
        EXPECT_STREQ(plain, recorded) << files[f].path;
        mem_free(plain);
        mem_free(recorded);
        mem_free(text);
    }
}

// ---------------------------------------------------------------------------
// (d) the HTML lexer agrees with the tree parser
// ---------------------------------------------------------------------------

static const char* const k_html_docs[] = {
    "<!DOCTYPE html>\n<html lang=\"en\"><head><title>T &amp; U</title>\n"
    "<style>p > a { color: red }</style></head>\n"
    "<body><div id=\"main\" class='box wide' data-x=1>\n<p>Hello <a href=\"http://x.y/\" title=\"go\">link</a></p>\n"
    "<!-- a comment\nacross lines -->\n<img src=\"a.png\" alt=\"An image\">\n"
    "<script>if (a < b) { x = \"</p>\"; }</script>\n"
    "<textarea name=t rows=\"3\">raw <b>text</b></textarea></div></body></html>\n",

    "<!doctype html>\n<html><head><meta charset=\"utf-8\"><link rel=\"stylesheet\" href=\"s.css\"></head>\n"
    "<body>\n<ul class=\"list\">\n<li data-n=\"1\">one</li>\n<li data-n=\"2\">two &copy;</li>\n</ul>\n"
    "<form action=\"/send\" method=\"post\"><input type=\"text\" name=\"q\" value=\"a b\">"
    "<button type=\"submit\" disabled>Go</button></form>\n</body></html>\n",
};

// Start tags in source order, each with its attributes as written.
struct LexTag {
    char name[32];
    int64_t first_attr;   // index into the collector's attributes
};

struct LexAttr {
    char* name;
    char* value;          // without quotes; null for a bare attribute
};

struct LexCollector {
    const char* text = nullptr;
    lam::ArrayList<LexTag> tags;
    lam::ArrayList<LexAttr> attrs;
    int64_t pending = 0;     // first attribute not yet claimed by a tag
    bool in_start_tag = false;
};

// The tokenizer reports a tag's attributes while it reads them and the tag
// itself once it is complete, so the attributes since the previous tag belong
// to the tag that follows them; its name follows the tag span.
static void lex_collect(void* ctx, const char* kind, size_t start, size_t end) {
    LexCollector* c = (LexCollector*)ctx;
    const char* t = c->text;
    size_t n = end - start;
    if (strcmp(kind, "tag") == 0) {
        char next = end > start + 1 ? t[start + 1] : '\0';
        c->in_start_tag = t[start] == '<' && ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z'));
        if (c->in_start_tag) {
            LexTag tag = {};
            tag.first_attr = c->pending;
            c->tags.push_back(tag);
        }
        c->pending = (int64_t)c->attrs.length();
    } else if (strcmp(kind, "tag-name") == 0 && c->in_start_tag && c->tags.length() > 0) {
        LexTag& tag = c->tags.back();
        size_t m = n < sizeof(tag.name) - 1 ? n : sizeof(tag.name) - 1;
        for (size_t i = 0; i < m; i++) {
            char ch = t[start + i];
            tag.name[i] = ch >= 'A' && ch <= 'Z' ? (char)(ch - 'A' + 'a') : ch;
        }
    } else if (strcmp(kind, "attr-name") == 0) {
        c->attrs.push_back(LexAttr{dup_line(t + start, n), nullptr});
    } else if (strcmp(kind, "attr-value") == 0 && (int64_t)c->attrs.length() > c->pending) {
        // the value as written, without its quotes
        if (n >= 2 && (t[start] == '"' || t[start] == '\'') && t[end - 1] == t[start]) { start++; n -= 2; }
        c->attrs.back().value = dup_line(t + start, n);
    }
}

static void collect_elements(Item item, lam::ArrayList<Item>* out) {
    if (get_type_id(item) != LMD_TYPE_ELEMENT) return;
    out->push_back(item);
    ElementReader r(item);
    for (int64_t i = 0; i < r.childCount(); i++) collect_elements(r.childAt(i).item(), out);
}

TEST_F(SourcePosTest, HtmlLexerAgreesWithTreeParser) {
    for (size_t doc = 0; doc < sizeof(k_html_docs) / sizeof(k_html_docs[0]); doc++) {
        const char* text = k_html_docs[doc];
        Pool* pool = mem_pool_create(NULL, MEM_ROLE_INPUT, "sourcepos.gtest.html");
        Input* lex_input = Input::create(pool, nullptr, nullptr);
        LexCollector lex;
        lex.text = text;
        html5_lex_spans(lex_input, text, strlen(text), lex_collect, &lex);

        String* type = (String*)mem_alloc(sizeof(String) + 5, MEM_CAT_TEMP);
        type->len = 4;
        memcpy(type->chars, "html", 5);
        char* copy = dup_line(text, strlen(text));
        Url* url = parse_url(get_current_dir(), "sourcepos_gtest.html");
        Input* tree = input_from_source(copy, url, type, NULL);
        ASSERT_NE(tree, nullptr);
        lam::ArrayList<Item> elements;
        collect_elements(tree->root, &elements);

        // elements in document order are the start tags in source order
        lam::ArrayList<Item> named;
        for (size_t i = 0; i < elements.length(); i++) {
            ElementReader r(elements[i]);
            if (r.tagName() && r.tagName()[0] != '#') named.push_back(elements[i]);
        }
        ASSERT_EQ(named.length(), lex.tags.length()) << "document " << doc;
        for (size_t i = 0; i < named.length(); i++) {
            ElementReader r(named[i]);
            const LexTag& tag = lex.tags[i];
            EXPECT_STREQ(r.tagName(), tag.name) << "document " << doc << ", tag " << i;
            int64_t end = i + 1 < lex.tags.length() ? lex.tags[i + 1].first_attr : lex.pending;
            for (int64_t a = tag.first_attr; a < end; a++) {
                const LexAttr& attr = lex.attrs[(size_t)a];
                const char* value = r.get_attr_string(attr.name);
                const char* written = attr.value ? attr.value : "";
                // the tree decodes character references; compare plain values only
                if (strchr(written, '&')) continue;
                EXPECT_STREQ(value ? value : "", written)
                    << "document " << doc << ", <" << tag.name << " " << attr.name << ">";
            }
        }
        for (size_t a = 0; a < lex.attrs.length(); a++) {
            mem_free(lex.attrs[a].name);
            if (lex.attrs[a].value) mem_free(lex.attrs[a].value);
        }
        mem_free(copy);
        mem_free(type);
        mem_pool_destroy(pool);
    }
}

// ---------------------------------------------------------------------------
// (e) random edits: the carried cache equals no cache
// ---------------------------------------------------------------------------

static uint64_t g_seed = 0x5eed2026ULL;
static int64_t rnd(int64_t n) {
    g_seed = g_seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return n <= 0 ? 0 : (int64_t)((g_seed >> 33) % (uint64_t)n);
}

static const char* const k_edit_lines[] = {
    "```", "~~~", "```js", "let x = 1;", "<!--", "-->", "<div>", "</div>", "", "", "",
    "# Heading", "Some *em* and **strong** text.", "- item", "  continued", "> quote",
    "[r1]: http://r1", "[r2]: http://r2 \"title\"", "See [a][r1] and [b][r2] and [r3].",
    "[r3]:", "  http://r3", "| a | b |", "|---|---|", "---", "Setext", "=====",
};
static const int64_t k_edit_line_count = sizeof(k_edit_lines) / sizeof(k_edit_lines[0]);

// The editor's realignment after rebuilding chunks [k, k + removed) as
// `added` (source_highlight.ls scan_after).
static void realign(lam::ArrayList<BoundaryState>* states, int64_t* valid, HighlightLabels* labels,
                    int64_t k, int64_t removed, int64_t added) {
    lam::ArrayList<BoundaryState> next;
    for (int64_t i = 0; i <= k && i < (int64_t)states->length(); i++) next.push_back((*states)[(size_t)i]);
    for (int64_t i = 1; i < added; i++) {
        BoundaryState unknown = {};
        unknown.restart.kind = -1;
        next.push_back(unknown);
    }
    for (int64_t i = k + removed; i < (int64_t)states->length(); i++) next.push_back((*states)[(size_t)i]);
    states->clear();
    for (size_t i = 0; i < next.length(); i++) states->push_back(next[i]);
    if (*valid > k + 1) *valid = k + 1;

    HighlightLabels moved;
    moved.reset();
    for (int64_t c = 0; c < k && c < labels->chunks(); c++) moved.copy_chunk(*labels, c);
    for (int64_t i = 0; i < added; i++) moved.close_chunk();
    for (int64_t c = k + removed; c < labels->chunks(); c++) moved.copy_chunk(*labels, c);
    labels->reset();
    for (int64_t c = 0; c < moved.chunks(); c++) labels->copy_chunk(moved, c);
}

TEST_F(SourcePosTest, CarriedCacheEqualsColdParseAfterRandomEdits) {
    const int64_t CHUNK = 16;
    lam::ArrayList<char> text;
    for (int64_t i = 0; i < 600; i++) {
        const char* line = k_edit_lines[rnd(k_edit_line_count)];
        for (const char* p = line; *p; p++) text.push_back(*p);
        text.push_back('\n');
    }
    text.push_back('\0');
    TestDoc d;
    doc_load(&d, text.data(), CHUNK);

    lam::ArrayList<BoundaryState> states;
    HighlightLabels labels;
    labels.reset();
    int64_t valid = 0;
    int64_t mismatched = 0;
    for (int64_t round = 0; round < 300; round++) {
        // edit inside one chunk: replace, insert or delete lines
        int64_t k = rnd((int64_t)d.sizes.length());
        int64_t base = d.starts[(size_t)k];
        int64_t at = base + rnd(d.sizes[(size_t)k]);
        int64_t op = rnd(3);
        int64_t delta = 0;
        if (op == 0) {
            const char* line = k_edit_lines[rnd(k_edit_line_count)];
            mem_free(d.lines[(size_t)at]);
            d.lines[(size_t)at] = dup_line(line, strlen(line));
        } else if (op == 1) {
            int64_t n = 1 + rnd(24);
            for (int64_t i = 0; i < n; i++) {
                const char* line = k_edit_lines[rnd(k_edit_line_count)];
                d.lines.insert((size_t)at, dup_line(line, strlen(line)));
            }
            delta = n;
        } else {
            int64_t room = base + d.sizes[(size_t)k] - at;
            int64_t n = 1 + rnd(room < 8 ? room : 8);
            if (n >= d.sizes[(size_t)k]) n = d.sizes[(size_t)k] - 1;
            for (int64_t i = 0; i < n; i++) {
                mem_free(d.lines[(size_t)at]);
                d.lines.remove((size_t)at);
            }
            delta = -n;
        }
        // the edited chunk splits or merges as the editor's buffer does
        d.sizes[(size_t)k] += delta;
        int64_t removed = 1, added = 1;
        if (d.sizes[(size_t)k] > 2 * CHUNK) {
            int64_t half = d.sizes[(size_t)k] / 2;
            d.sizes.insert((size_t)k + 1, d.sizes[(size_t)k] - half);
            d.sizes[(size_t)k] = half;
            added = 2;
        } else if (d.sizes[(size_t)k] < CHUNK / 4 && k + 1 < (int64_t)d.sizes.length()) {
            d.sizes[(size_t)k] += d.sizes[(size_t)k + 1];
            d.sizes.remove((size_t)k + 1);
            removed = 2;
        }
        doc_restart(&d);
        realign(&states, &valid, &labels, k, removed, added);

        HighlightLines src = doc_source(&d);
        int64_t first = rnd(src.count);
        int64_t last = first + 30 < src.count - 1 ? first + 30 : src.count - 1;
        HighlightCache cache = {states.data(), (int64_t)states.length(), valid, &labels};
        HighlightResult carried, cold;
        ASSERT_TRUE(markdown_highlight_window(&src, first, last, &cache, &carried));
        ASSERT_TRUE(markdown_highlight_window(&src, first, last, nullptr, &cold));
        bool same = same_spans(carried, cold) && carried.states.length() == cold.states.length() &&
                    carried.labels.count() == cold.labels.count() && carried.restart_line == cold.restart_line;
        for (size_t i = 0; same && i < carried.states.length(); i++) {
            same = memcmp(&carried.states[i], &cold.states[i], sizeof(BoundaryState)) == 0;
        }
        for (int64_t i = 0; same && i < carried.labels.count(); i++) {
            same = strcmp(carried.labels.label(i), cold.labels.label(i)) == 0;
        }
        if (!same) {
            mismatched++;
            ADD_FAILURE() << "round " << round << ": the carried cache differs from a cold parse";
        }
        states.clear();
        for (size_t i = 0; i < carried.states.length(); i++) states.push_back(carried.states[i]);
        valid = (int64_t)states.length();
        labels.reset();
        for (int64_t c = 0; c < carried.labels.chunks(); c++) labels.copy_chunk(carried.labels, c);
    }
    EXPECT_EQ(mismatched, 0);
    doc_free(&d);
}
