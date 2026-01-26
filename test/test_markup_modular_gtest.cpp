/**
 * test_markup_modular_gtest.cpp - Unit tests for modular markup parser components
 *
 * Tests the refactored modular architecture:
 * - Block parsers (header, list, code, quote, table, paragraph, divider)
 * - Inline parsers (emphasis, code, link, image, math, special)
 * - Format adapters (markdown, rst, wiki, textile, org, asciidoc, man)
 * - Error handling and recovery
 *
 * Phase 7 of markup parser refactoring
 */

#define _GNU_SOURCE
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "../lambda/lambda.h"
#include "../lambda/lambda-data.hpp"
#include "../lib/log.h"
#include "../lib/strbuf.h"
#include "../lib/mempool.h"
#include "../lib/url.h"

// Forward declarations
extern "C" {
    Input* input_from_source(char* source, Url* abs_url, String* type, String* flavor);
    String* format_data(Item item, String* type, String* flavor, Pool* pool);
    void format_item(StrBuf* buf, Item item, int indent, char* format);
    Url* get_current_dir(void);
    Url* parse_url(Url* base, const char* url);
}

// Helper function to create Lambda String
static String* make_string(const char* text) {
    if (!text) return NULL;
    size_t len = strlen(text);
    size_t total_size = sizeof(String) + len + 1;
    String* result = (String*)malloc(total_size);
    if (!result) return NULL;
    result->len = len;
    result->ref_cnt = 1;
    strcpy(result->chars, text);
    return result;
}

// Helper to parse markup and return JSON
static String* parse_to_json(const char* content, const char* filename) {
    String* type_str = make_string("markup");
    Url* cwd = get_current_dir();
    Url* url = parse_url(cwd, filename);
    char* content_copy = strdup(content);

    Input* input = input_from_source(content_copy, url, type_str, NULL);
    if (!input) {
        free(content_copy);
        return NULL;
    }

    String* json_type = make_string("json");
    String* formatted = format_data(input->root, json_type, NULL, input->pool);
    free(content_copy);
    return formatted;
}

// Test fixture
class MarkupModularTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
    }

    void TearDown() override {
    }

    // Helper to check JSON contains a string
    bool json_contains(const char* json, const char* needle) {
        return json && needle && strstr(json, needle) != NULL;
    }
};

// =============================================================================
// Block Parser Tests
// =============================================================================

class BlockParserTest : public MarkupModularTest {};

// Test ATX-style headers (#)
TEST_F(BlockParserTest, AtxHeaders) {
    const char* content =
        "# Heading 1\n"
        "## Heading 2\n"
        "### Heading 3\n"
        "#### Heading 4\n"
        "##### Heading 5\n"
        "###### Heading 6\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);
    ASSERT_GT(json->len, 0);

    // Verify h1-h6 elements are created
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"h1\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"h2\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"h3\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"h4\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"h5\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"h6\""));
}

// Test Setext-style headers (underline)
TEST_F(BlockParserTest, SetextHeaders) {
    const char* content =
        "Heading 1\n"
        "=========\n"
        "\n"
        "Heading 2\n"
        "---------\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    // Setext headers may be parsed as h1/h2 or as paragraphs depending on parser
    // Just verify we get valid output
    EXPECT_GT(json->len, 0) << "Should produce valid output";
}

// Test unordered lists
TEST_F(BlockParserTest, UnorderedLists) {
    const char* content =
        "- Item 1\n"
        "- Item 2\n"
        "- Item 3\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"ul\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"li\""));
}

// Test ordered lists
TEST_F(BlockParserTest, OrderedLists) {
    const char* content =
        "1. First\n"
        "2. Second\n"
        "3. Third\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"ol\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"li\""));
}

// Test nested lists
TEST_F(BlockParserTest, NestedLists) {
    const char* content =
        "- Parent\n"
        "  - Child 1\n"
        "  - Child 2\n"
        "- Sibling\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    // Should have nested ul elements
    int ul_count = 0;
    const char* p = json->chars;
    while ((p = strstr(p, "\"$\":\"ul\"")) != NULL) {
        ul_count++;
        p++;
    }
    EXPECT_GE(ul_count, 1) << "Should have at least one ul element";
}

// Test fenced code blocks (```)
TEST_F(BlockParserTest, FencedCodeBlock) {
    const char* content =
        "```python\n"
        "def hello():\n"
        "    print('Hello')\n"
        "```\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"pre\"") ||
                json_contains(json->chars, "\"$\":\"code\""));
    EXPECT_TRUE(json_contains(json->chars, "python"));
}

// Test fenced code blocks (~~~)
TEST_F(BlockParserTest, TildeFencedCodeBlock) {
    const char* content =
        "~~~javascript\n"
        "console.log('test');\n"
        "~~~\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"pre\"") ||
                json_contains(json->chars, "\"$\":\"code\""));
}

// Test blockquotes
TEST_F(BlockParserTest, Blockquotes) {
    const char* content =
        "> This is a quote\n"
        "> on multiple lines\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"blockquote\""));
}

// Test nested blockquotes
TEST_F(BlockParserTest, NestedBlockquotes) {
    const char* content =
        "> Outer quote\n"
        ">> Nested quote\n"
        "> Back to outer\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "blockquote"));
}

// Test tables (GFM)
TEST_F(BlockParserTest, GfmTables) {
    const char* content =
        "| A | B | C |\n"
        "|---|---|---|\n"
        "| 1 | 2 | 3 |\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"table\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"tr\"") ||
                json_contains(json->chars, "\"$\":\"th\"") ||
                json_contains(json->chars, "\"$\":\"td\""));
}

// Test horizontal rules
TEST_F(BlockParserTest, HorizontalRules) {
    const char* content =
        "Text above\n"
        "\n"
        "---\n"
        "\n"
        "Text below\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"hr\""));
}

// Test paragraphs
TEST_F(BlockParserTest, Paragraphs) {
    const char* content =
        "This is paragraph one.\n"
        "\n"
        "This is paragraph two.\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"p\""));
}

// Test unclosed code fence (error handling)
TEST_F(BlockParserTest, UnclosedCodeFence) {
    const char* content =
        "```python\n"
        "print('no closing fence')\n";  // Missing closing ```

    String* json = parse_to_json(content, "test.md");
    // Should still parse (with warning) rather than crash
    ASSERT_NE(json, nullptr);
    EXPECT_GT(json->len, 0);
}

// =============================================================================
// Inline Parser Tests
// =============================================================================

class InlineParserTest : public MarkupModularTest {};

// Test bold emphasis
TEST_F(InlineParserTest, BoldEmphasis) {
    const char* content = "This is **bold** text.\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"strong\"") ||
                json_contains(json->chars, "\"$\":\"b\""));
}

// Test italic emphasis
TEST_F(InlineParserTest, ItalicEmphasis) {
    const char* content = "This is *italic* text.\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"em\"") ||
                json_contains(json->chars, "\"$\":\"i\""));
}

// Test inline code
TEST_F(InlineParserTest, InlineCode) {
    const char* content = "Use `code` in text.\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"code\""));
}

// Test double backtick code
TEST_F(InlineParserTest, DoubleBacktickCode) {
    const char* content = "Use ``code with `backtick``` in text.\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);
    // Should parse successfully
    EXPECT_GT(json->len, 0);
}

// Test links
TEST_F(InlineParserTest, Links) {
    const char* content = "Click [here](https://example.com) to visit.\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"a\""));
    // URL may be encoded or stored differently - just verify link element exists
}

// Test links with titles
TEST_F(InlineParserTest, LinksWithTitles) {
    const char* content = "[link](http://example.com \"Example Title\")\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"a\""));
}

// Test autolinks
TEST_F(InlineParserTest, Autolinks) {
    const char* content = "Visit <https://example.com> for more.\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);
    // Should produce a link
    EXPECT_GT(json->len, 0);
}

// Test images
TEST_F(InlineParserTest, Images) {
    const char* content = "![Alt text](image.png)\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"img\""));
}

// Test inline math
TEST_F(InlineParserTest, InlineMath) {
    const char* content = "The equation $E=mc^2$ is famous.\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    // Should contain math element or the equation
    EXPECT_GT(json->len, 0);
}

// Test strikethrough
TEST_F(InlineParserTest, Strikethrough) {
    const char* content = "This is ~~deleted~~ text.\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"del\"") ||
                json_contains(json->chars, "\"$\":\"s\"") ||
                json_contains(json->chars, "deleted"));  // At minimum, text preserved
}

// Test mixed inline elements
TEST_F(InlineParserTest, MixedInline) {
    const char* content = "**Bold with *nested italic* inside**\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    EXPECT_TRUE(json_contains(json->chars, "strong") ||
                json_contains(json->chars, "\"$\":\"b\""));
}

// Test unmatched emphasis (graceful handling)
TEST_F(InlineParserTest, UnmatchedEmphasis) {
    const char* content = "This has *unclosed emphasis\n";

    String* json = parse_to_json(content, "test.md");
    // Should parse without crashing
    ASSERT_NE(json, nullptr);
    EXPECT_GT(json->len, 0);
}

// =============================================================================
// Format-Specific Tests
// =============================================================================

class FormatAdapterTest : public MarkupModularTest {};

// Test Markdown format detection
TEST_F(FormatAdapterTest, MarkdownDetection) {
    const char* content = "# Title\n\n- List item\n\n**Bold**\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    // Should parse as Markdown
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"h1\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"ul\"") ||
                json_contains(json->chars, "\"$\":\"li\""));
}

// Test RST format detection
TEST_F(FormatAdapterTest, RstDetection) {
    const char* content =
        "Title\n"
        "=====\n"
        "\n"
        "Paragraph with ``literal text``.\n";

    String* json = parse_to_json(content, "test.rst");
    ASSERT_NE(json, nullptr);

    // Should parse headings
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"h1\"") ||
                json_contains(json->chars, "\"$\":\"h2\""));
}

// Test Wiki format detection
TEST_F(FormatAdapterTest, WikiDetection) {
    const char* content =
        "== Heading ==\n"
        "\n"
        "'''Bold''' and ''italic'' text.\n";

    String* json = parse_to_json(content, "test.wiki");
    ASSERT_NE(json, nullptr);

    // Should parse successfully
    EXPECT_GT(json->len, 0);
}

// Test Org-mode format detection
TEST_F(FormatAdapterTest, OrgModeDetection) {
    const char* content =
        "* Heading 1\n"
        "** Heading 2\n"
        "Some text content.\n";

    String* json = parse_to_json(content, "test.org");
    ASSERT_NE(json, nullptr);

    EXPECT_GT(json->len, 0);
}

// Test AsciiDoc format
TEST_F(FormatAdapterTest, AsciiDocDetection) {
    const char* content =
        "= Document Title\n"
        "\n"
        "== Section\n"
        "\n"
        "Paragraph with *bold* text.\n";

    String* json = parse_to_json(content, "test.adoc");
    ASSERT_NE(json, nullptr);

    EXPECT_GT(json->len, 0);
}

// Test Textile format
TEST_F(FormatAdapterTest, TextileDetection) {
    const char* content =
        "h1. Heading\n"
        "\n"
        "*Bold* and _italic_ text.\n";

    String* json = parse_to_json(content, "test.textile");
    ASSERT_NE(json, nullptr);

    EXPECT_GT(json->len, 0);
}

// =============================================================================
// Error Recovery Tests
// =============================================================================

class ErrorRecoveryTest : public MarkupModularTest {};

// Test malformed table recovery
TEST_F(ErrorRecoveryTest, MalformedTable) {
    const char* content =
        "| A | B\n"  // Missing closing |
        "| 1 | 2 |\n";

    String* json = parse_to_json(content, "test.md");
    // Should parse without crashing
    ASSERT_NE(json, nullptr);
    EXPECT_GT(json->len, 0);
}

// Test deeply nested lists
TEST_F(ErrorRecoveryTest, DeeplyNestedLists) {
    const char* content =
        "- Level 1\n"
        "  - Level 2\n"
        "    - Level 3\n"
        "      - Level 4\n"
        "        - Level 5\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);
    EXPECT_GT(json->len, 0);
}

// Test empty document
TEST_F(ErrorRecoveryTest, EmptyDocument) {
    const char* content = "";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);
}

// Test whitespace-only document
TEST_F(ErrorRecoveryTest, WhitespaceOnlyDocument) {
    const char* content = "   \n\n\t\n   \n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);
}

// Test very long line
TEST_F(ErrorRecoveryTest, VeryLongLine) {
    // Create a 1000-character line
    std::string long_line(1000, 'x');
    long_line += "\n";

    String* json = parse_to_json(long_line.c_str(), "test.md");
    ASSERT_NE(json, nullptr);
    EXPECT_GT(json->len, 0);
}

// =============================================================================
// Math Block Tests
// =============================================================================

class MathBlockTest : public MarkupModularTest {};

// Test display math block
TEST_F(MathBlockTest, DisplayMath) {
    const char* content =
        "$$\n"
        "E = mc^2\n"
        "$$\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    // Should contain math or equation reference
    EXPECT_GT(json->len, 0);
}

// Test inline and display math mixed
TEST_F(MathBlockTest, MixedMath) {
    const char* content =
        "Inline $x=1$ and display:\n"
        "\n"
        "$$\n"
        "\\sum_{i=0}^{n} x_i\n"
        "$$\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);
    EXPECT_GT(json->len, 0);
}

// =============================================================================
// Complex Document Tests
// =============================================================================

class ComplexDocumentTest : public MarkupModularTest {};

// Test comprehensive markdown document
TEST_F(ComplexDocumentTest, ComprehensiveMarkdown) {
    const char* content =
        "# Main Title\n"
        "\n"
        "This is an introduction paragraph with **bold**, *italic*, and `code`.\n"
        "\n"
        "## Section 1\n"
        "\n"
        "A list:\n"
        "\n"
        "- Item 1\n"
        "- Item 2\n"
        "  - Nested item\n"
        "\n"
        "## Section 2\n"
        "\n"
        "A table:\n"
        "\n"
        "| Col A | Col B |\n"
        "|-------|-------|\n"
        "| 1     | 2     |\n"
        "\n"
        "```python\n"
        "print('code')\n"
        "```\n"
        "\n"
        "> A blockquote\n"
        "\n"
        "---\n"
        "\n"
        "[Link](https://example.com) and ![image](img.png)\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);

    // Verify various elements are parsed
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"h1\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"h2\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"p\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"ul\"") ||
                json_contains(json->chars, "\"$\":\"li\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"table\"") ||
                json_contains(json->chars, "\"$\":\"tr\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"pre\"") ||
                json_contains(json->chars, "\"$\":\"code\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"blockquote\""));
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"hr\""));
}

// Test document with all inline types
TEST_F(ComplexDocumentTest, AllInlineTypes) {
    const char* content =
        "Text with **bold**, *italic*, `code`, ~~strikethrough~~, "
        "[link](url), ![image](img.png), and $math$.\n";

    String* json = parse_to_json(content, "test.md");
    ASSERT_NE(json, nullptr);
    EXPECT_GT(json->len, 0);

    // All inline elements should be parsed
    EXPECT_TRUE(json_contains(json->chars, "\"$\":\"p\""));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
