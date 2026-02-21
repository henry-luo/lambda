// Integration tests for the unified markup emitter (format_markup)
// Verifies that format_markup() with each MarkupOutputRules table produces
// correct output for common elements: headings, paragraphs, inline formatting,
// links, lists, code blocks, blockquotes, tables, and format-specific elements.

#include <gtest/gtest.h>
#include <string.h>
#include "../lambda/lambda.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/format/format.h"
#include "../lambda/format/format-markup.h"
#include "../lambda/format/format-utils.h"
#include "../lib/mempool.h"
#include "../lib/log.h"

extern "C" {
    Input* input_from_source(char* source, Url* abs_url, String* type, String* flavor);
    String* format_data(Item item, String* type, String* flavor, Pool* pool);
}

// Helper to create Lambda String from C string
static String* make_str(const char* text) {
    if (!text) return NULL;
    size_t len = strlen(text);
    String* result = (String*)malloc(sizeof(String) + len + 1);
    result->len = (uint32_t)len;
    result->ref_cnt = 1;
    strcpy(result->chars, text);
    return result;
}

static void free_str(String* s) { if (s) free(s); }

// Helper to format an Item with given rules and return C string
static const char* format_with_rules(Pool* pool, Item item, const MarkupOutputRules* rules) {
    StringBuf* sb = stringbuf_new(pool);
    format_markup(sb, item, rules);
    stringbuf_append_char(sb, '\0');
    return sb->str->chars;
}

// ==============================================================================
// Test Fixture
// ==============================================================================

class FormatMarkupTest : public ::testing::Test {
protected:
    Pool* pool_;
    Input* input_;
    MarkBuilder* mb_;

    void SetUp() override {
        log_init(NULL);
        pool_ = pool_create();
        input_ = Input::create(pool_);
        mb_ = new MarkBuilder(input_);
    }

    void TearDown() override {
        delete mb_;
        pool_destroy(pool_);
    }

    // Build a simple document with a heading and paragraph
    Item build_heading_paragraph() {
        return mb_->element("html")
            .beginChild("h1").text("Hello World").end()
            .beginChild("p").text("This is a paragraph.").end()
            .final();
    }

    // Build a document with bold, italic, code inline
    Item build_inline_formatting() {
        return mb_->element("html")
            .beginChild("p")
                .text("Text with ")
                .beginChild("strong").text("bold").end()
                .text(" and ")
                .beginChild("em").text("italic").end()
                .text(" and ")
                .beginChild("code").text("code").end()
                .text(".")
            .end()
            .final();
    }

    // Build a document with a link
    Item build_link() {
        return mb_->element("html")
            .beginChild("p")
                .text("Click ")
                .beginChild("a").attr("href", "https://example.com").text("here").end()
                .text(".")
            .end()
            .final();
    }

    // Build an unordered list
    Item build_unordered_list() {
        return mb_->element("html")
            .beginChild("ul")
                .beginChild("li").text("Item one").end()
                .beginChild("li").text("Item two").end()
                .beginChild("li").text("Item three").end()
            .end()
            .final();
    }

    // Build an ordered list
    Item build_ordered_list() {
        return mb_->element("html")
            .beginChild("ol")
                .beginChild("li").text("First").end()
                .beginChild("li").text("Second").end()
            .end()
            .final();
    }

    // Build a code block
    Item build_code_block() {
        return mb_->element("html")
            .beginChild("pre").attr("language", "python").text("print('hello')").end()
            .final();
    }

    // Build a blockquote
    Item build_blockquote() {
        return mb_->element("html")
            .beginChild("blockquote")
                .beginChild("p").text("Quoted text.").end()
            .end()
            .final();
    }

    // Build a horizontal rule
    Item build_hr() {
        return mb_->element("html")
            .beginChild("hr").end()
            .final();
    }

    // Build a simple table
    Item build_table() {
        return mb_->element("html")
            .beginChild("table")
                .beginChild("tr")
                    .beginChild("th").text("Name").end()
                    .beginChild("th").text("Age").end()
                .end()
                .beginChild("tr")
                    .beginChild("td").text("Alice").end()
                    .beginChild("td").text("30").end()
                .end()
            .end()
            .final();
    }
};

// ==============================================================================
// Markdown Tests
// ==============================================================================

TEST_F(FormatMarkupTest, MarkdownHeading) {
    Item item = build_heading_paragraph();
    const char* out = format_with_rules(pool_, item, &MARKDOWN_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "# Hello World"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "This is a paragraph."), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, MarkdownInline) {
    Item item = build_inline_formatting();
    const char* out = format_with_rules(pool_, item, &MARKDOWN_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "**bold**"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "*italic*"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "`code`"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, MarkdownLink) {
    Item item = build_link();
    const char* out = format_with_rules(pool_, item, &MARKDOWN_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "[here](https://example.com)"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, MarkdownUnorderedList) {
    Item item = build_unordered_list();
    const char* out = format_with_rules(pool_, item, &MARKDOWN_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "- Item one"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "- Item two"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, MarkdownOrderedList) {
    Item item = build_ordered_list();
    const char* out = format_with_rules(pool_, item, &MARKDOWN_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "1."), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "First"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, MarkdownCodeBlock) {
    Item item = build_code_block();
    const char* out = format_with_rules(pool_, item, &MARKDOWN_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "```python"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "print('hello')"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "```\n"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, MarkdownBlockquote) {
    Item item = build_blockquote();
    const char* out = format_with_rules(pool_, item, &MARKDOWN_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "> "), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "Quoted text."), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, MarkdownHR) {
    Item item = build_hr();
    const char* out = format_with_rules(pool_, item, &MARKDOWN_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "---"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, MarkdownTable) {
    Item item = build_table();
    const char* out = format_with_rules(pool_, item, &MARKDOWN_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "Name"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "Alice"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "|"), nullptr) << "Output: " << out;
}

// ==============================================================================
// RST Tests
// ==============================================================================

TEST_F(FormatMarkupTest, RstHeading) {
    Item item = build_heading_paragraph();
    const char* out = format_with_rules(pool_, item, &RST_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "Hello World"), nullptr) << "Output: " << out;
    // RST h1 uses = underline
    EXPECT_NE(strstr(out, "==="), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, RstInline) {
    Item item = build_inline_formatting();
    const char* out = format_with_rules(pool_, item, &RST_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "**bold**"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "*italic*"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "``code``"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, RstLink) {
    Item item = build_link();
    const char* out = format_with_rules(pool_, item, &RST_RULES);
    ASSERT_NE(out, nullptr);
    // RST link: `text <url>`_
    EXPECT_NE(strstr(out, "`here <https://example.com>`_"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, RstCodeBlock) {
    Item item = build_code_block();
    const char* out = format_with_rules(pool_, item, &RST_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, ".. code-block:: python"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "print('hello')"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, RstUnorderedList) {
    Item item = build_unordered_list();
    const char* out = format_with_rules(pool_, item, &RST_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "- Item one"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, RstHR) {
    Item item = build_hr();
    const char* out = format_with_rules(pool_, item, &RST_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "----"), nullptr) << "Output: " << out;
}

// ==============================================================================
// Org-mode Tests
// ==============================================================================

TEST_F(FormatMarkupTest, OrgHeading) {
    Item item = build_heading_paragraph();
    const char* out = format_with_rules(pool_, item, &ORG_RULES);
    ASSERT_NE(out, nullptr);
    // Org uses * prefix for headings
    EXPECT_NE(strstr(out, "* Hello World"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "This is a paragraph."), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, OrgInline) {
    Item item = build_inline_formatting();
    const char* out = format_with_rules(pool_, item, &ORG_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "*bold*"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "/italic/"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "~code~"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, OrgLink) {
    Item item = build_link();
    const char* out = format_with_rules(pool_, item, &ORG_RULES);
    ASSERT_NE(out, nullptr);
    // Org link: [[url][desc]]
    EXPECT_NE(strstr(out, "[[https://example.com][here]]"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, OrgCodeBlock) {
    Item item = build_code_block();
    const char* out = format_with_rules(pool_, item, &ORG_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "#+BEGIN_SRC python"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "print('hello')"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "#+END_SRC"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, OrgUnorderedList) {
    Item item = build_unordered_list();
    const char* out = format_with_rules(pool_, item, &ORG_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "- Item one"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, OrgHR) {
    Item item = build_hr();
    const char* out = format_with_rules(pool_, item, &ORG_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "-----"), nullptr) << "Output: " << out;
}

// ==============================================================================
// Wiki Tests
// ==============================================================================

TEST_F(FormatMarkupTest, WikiHeading) {
    Item item = build_heading_paragraph();
    const char* out = format_with_rules(pool_, item, &WIKI_RULES);
    ASSERT_NE(out, nullptr);
    // Wiki h1: = Hello World =
    EXPECT_NE(strstr(out, "= Hello World ="), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, WikiInline) {
    Item item = build_inline_formatting();
    const char* out = format_with_rules(pool_, item, &WIKI_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "'''bold'''"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "''italic''"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "<code>code</code>"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, WikiLink) {
    Item item = build_link();
    const char* out = format_with_rules(pool_, item, &WIKI_RULES);
    ASSERT_NE(out, nullptr);
    // Wiki link: [url text]
    EXPECT_NE(strstr(out, "[https://example.com here]"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, WikiUnorderedList) {
    Item item = build_unordered_list();
    const char* out = format_with_rules(pool_, item, &WIKI_RULES);
    ASSERT_NE(out, nullptr);
    // Wiki uses * for unordered list items (depth repetition)
    EXPECT_NE(strstr(out, "* Item one"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, WikiOrderedList) {
    Item item = build_ordered_list();
    const char* out = format_with_rules(pool_, item, &WIKI_RULES);
    ASSERT_NE(out, nullptr);
    // Wiki uses # for ordered list items (depth repetition)
    EXPECT_NE(strstr(out, "# First"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, WikiTable) {
    Item item = build_table();
    const char* out = format_with_rules(pool_, item, &WIKI_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "{|"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "|}"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "Name"), nullptr) << "Output: " << out;
}

// ==============================================================================
// Textile Tests
// ==============================================================================

TEST_F(FormatMarkupTest, TextileHeading) {
    Item item = build_heading_paragraph();
    const char* out = format_with_rules(pool_, item, &TEXTILE_RULES);
    ASSERT_NE(out, nullptr);
    // Textile h1: h1. Hello World
    EXPECT_NE(strstr(out, "h1. Hello World"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, TextileInline) {
    Item item = build_inline_formatting();
    const char* out = format_with_rules(pool_, item, &TEXTILE_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "*bold*"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "_italic_"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "@code@"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, TextileLink) {
    Item item = build_link();
    const char* out = format_with_rules(pool_, item, &TEXTILE_RULES);
    ASSERT_NE(out, nullptr);
    // Textile link: "text":url
    EXPECT_NE(strstr(out, "\"here\":https://example.com"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, TextileUnorderedList) {
    Item item = build_unordered_list();
    const char* out = format_with_rules(pool_, item, &TEXTILE_RULES);
    ASSERT_NE(out, nullptr);
    // Textile uses * for unordered (depth repetition)
    EXPECT_NE(strstr(out, "* Item one"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, TextileCodeBlock) {
    Item item = build_code_block();
    const char* out = format_with_rules(pool_, item, &TEXTILE_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "bc."), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "print('hello')"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, TextileHR) {
    Item item = build_hr();
    const char* out = format_with_rules(pool_, item, &TEXTILE_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "---"), nullptr) << "Output: " << out;
}

// ==============================================================================
// Textile Custom Elements (cite, span, dl)
// ==============================================================================

TEST_F(FormatMarkupTest, TextileCite) {
    Item item = mb_->element("html")
        .beginChild("cite").text("A book title").end()
        .final();
    const char* out = format_with_rules(pool_, item, &TEXTILE_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "??A book title??"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, TextileSpan) {
    Item item = mb_->element("html")
        .beginChild("span").text("styled text").end()
        .final();
    const char* out = format_with_rules(pool_, item, &TEXTILE_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "%styled text%"), nullptr) << "Output: " << out;
}

TEST_F(FormatMarkupTest, TextileDefinitionList) {
    Item item = mb_->element("html")
        .beginChild("dl")
            .beginChild("dt").text("Term").end()
            .beginChild("dd").text("Definition").end()
        .end()
        .final();
    const char* out = format_with_rules(pool_, item, &TEXTILE_RULES);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "- Term"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, ":= Definition"), nullptr) << "Output: " << out;
}

// ==============================================================================
// get_markup_rules lookup test
// ==============================================================================

TEST_F(FormatMarkupTest, RulesLookup) {
    EXPECT_EQ(get_markup_rules("markdown"), &MARKDOWN_RULES);
    EXPECT_EQ(get_markup_rules("md"), &MARKDOWN_RULES);
    EXPECT_EQ(get_markup_rules("rst"), &RST_RULES);
    EXPECT_EQ(get_markup_rules("restructuredtext"), &RST_RULES);
    EXPECT_EQ(get_markup_rules("org"), &ORG_RULES);
    EXPECT_EQ(get_markup_rules("orgmode"), &ORG_RULES);
    EXPECT_EQ(get_markup_rules("wiki"), &WIKI_RULES);
    EXPECT_EQ(get_markup_rules("mediawiki"), &WIKI_RULES);
    EXPECT_EQ(get_markup_rules("textile"), &TEXTILE_RULES);
    EXPECT_EQ(get_markup_rules("unknown"), nullptr);
}

// ==============================================================================
// Parse-then-format roundtrip parity tests
// These parse markdown input, then format with the new unified emitter
// and verify the new emitter produces the expected markup content.
// ==============================================================================

class FormatMarkupParityTest : public ::testing::Test {
protected:
    void SetUp() override { log_init(NULL); }

    // Parse markdown source, format with the new emitter, return result
    const char* format_parsed(const char* md_source, const char* fmt_name) {
        Pool* pool = pool_create();
        String* type_str = make_str("markup");
        char* src = strdup(md_source);
        Url* cwd = get_current_dir();
        Url* url = parse_url(cwd, "parity_test.md");

        Input* input = input_from_source(src, url, type_str, NULL);
        if (!input) { free(src); free_str(type_str); return nullptr; }

        const MarkupOutputRules* rules = get_markup_rules(fmt_name);
        if (!rules) { free_str(type_str); return nullptr; }

        const char* result = format_with_rules(input->pool, input->root, rules);
        free_str(type_str);
        return result;
    }
};

// Parity test: simple heading + paragraph to markdown
TEST_F(FormatMarkupParityTest, MarkdownSimple) {
    const char* out = format_parsed("# Hello\n\nWorld\n", "markdown");
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "Hello"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "World"), nullptr) << "Output: " << out;
}

// Parity test: bold/italic to markdown
TEST_F(FormatMarkupParityTest, MarkdownInline) {
    const char* out = format_parsed("**bold** and *italic*\n", "markdown");
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "**bold**"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "*italic*"), nullptr) << "Output: " << out;
}

// Parity test: list to RST
TEST_F(FormatMarkupParityTest, RstList) {
    const char* out = format_parsed("- Item 1\n- Item 2\n", "rst");
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "Item 1"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "Item 2"), nullptr) << "Output: " << out;
}

// Parity test: heading to org
TEST_F(FormatMarkupParityTest, OrgHeading) {
    const char* out = format_parsed("# Title\n\nContent\n", "org");
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "Title"), nullptr) << "Output: " << out;
    EXPECT_NE(strstr(out, "Content"), nullptr) << "Output: " << out;
}

// Parity test: heading to wiki
TEST_F(FormatMarkupParityTest, WikiHeading) {
    const char* out = format_parsed("# Title\n", "wiki");
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "Title"), nullptr) << "Output: " << out;
}

// Parity test: heading to textile
TEST_F(FormatMarkupParityTest, TextileHeading) {
    const char* out = format_parsed("# Title\n", "textile");
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "Title"), nullptr) << "Output: " << out;
}
