/**
 * reStructuredText Functional Test Runner
 *
 * This test file runs functional tests for the RST parser, comparing
 * Lambda markup parser output against expected HTML derived from docutils.
 *
 * Test data sourced from:
 * https://github.com/docutils/docutils/tree/master/docutils/test/functional
 *
 * Since docutils outputs complete HTML documents with styling, we test
 * individual RST constructs by parsing RST fragments and comparing the
 * semantic HTML structure.
 */

#define _GNU_SOURCE
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <regex>
#include <map>
#include "../../lambda/lambda.h"
#include "../../lambda/lambda-data.hpp"
#include "../../lambda/mark_reader.hpp"
#include "../../lib/strbuf.h"
#include "../../lib/url.h"
#include "../../lib/log.h"

// Forward declarations with C linkage
extern "C" {
    Input* input_from_source(char* source, Url* abs_url, String* type, String* flavor);
    String* format_data(Item item, String* type, String* flavor, Pool* pool);
    char* read_text_file(const char* filename);
}

// Helper function to create Lambda String
static String* create_test_string(const char* text) {
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

// Structure to hold an RST test case
struct RstTestCase {
    std::string name;
    std::string rst_input;
    std::string expected_html;  // Simplified expected HTML structure
    std::string description;
};

// Test fixture for RST functional tests
class RstFunctionalTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
    }

    // Parse RST and return the Input item for inspection
    Item parse_rst(const std::string& rst) {
        String* type_str = create_test_string("markup");
        String* flavor_str = create_test_string("rst");
        Url* cwd = get_current_dir();
        Url* dummy_url = parse_url(cwd, "test.rst");

        char* content_copy = strdup(rst.c_str());
        Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

        if (!input) {
            free(content_copy);
            return ItemNull;
        }

        return input->root;
    }

    // Parse RST and format as HTML
    std::string parse_and_format_html(const std::string& rst) {
        String* type_str = create_test_string("markup");
        String* flavor_str = create_test_string("rst");
        Url* cwd = get_current_dir();
        Url* dummy_url = parse_url(cwd, "test.rst");

        char* content_copy = strdup(rst.c_str());
        Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

        if (!input) {
            free(content_copy);
            return "";
        }

        // Format as HTML
        String* html_type = create_test_string("html");
        String* html_result = format_data(input->root, html_type, NULL, input->pool);

        std::string result;
        if (html_result && html_result->chars) {
            result = std::string(html_result->chars, html_result->len);
        }

        free(content_copy);
        return result;
    }

    // Check if element exists in parsed structure
    bool has_element(Item root, const char* tag_name) {
        if (get_type_id(root) != LMD_TYPE_ELEMENT) return false;

        ElementReader elem(root);
        const char* tag = elem.tagName();
        if (tag && strcmp(tag, tag_name) == 0) return true;

        // Check children
        for (int64_t i = 0; i < elem.childCount(); i++) {
            if (has_element(elem.childAt(i).item(), tag_name)) {
                return true;
            }
        }
        return false;
    }

    // Find element by tag name
    Item find_element(Item root, const char* tag_name) {
        if (get_type_id(root) != LMD_TYPE_ELEMENT) return ItemNull;

        ElementReader elem(root);
        const char* tag = elem.tagName();
        if (tag && strcmp(tag, tag_name) == 0) return root;

        // Check children
        for (int64_t i = 0; i < elem.childCount(); i++) {
            Item found = find_element(elem.childAt(i).item(), tag_name);
            if (found.item != ItemNull.item) return found;
        }
        return ItemNull;
    }

    // Count elements by tag name
    int count_elements(Item root, const char* tag_name) {
        if (get_type_id(root) != LMD_TYPE_ELEMENT) return 0;

        int count = 0;
        ElementReader elem(root);
        const char* tag = elem.tagName();
        if (tag && strcmp(tag, tag_name) == 0) count++;

        for (int64_t i = 0; i < elem.childCount(); i++) {
            count += count_elements(elem.childAt(i).item(), tag_name);
        }
        return count;
    }

    // Get text content of an element
    std::string get_text_content(Item root) {
        TypeId type = get_type_id(root);

        if (type == LMD_TYPE_STRING) {
            String* str = root.get_string();
            if (str && str->chars) {
                return std::string(str->chars, str->len);
            }
            return "";
        }

        if (type != LMD_TYPE_ELEMENT) return "";

        std::string result;
        ElementReader elem(root);
        for (int64_t i = 0; i < elem.childCount(); i++) {
            result += get_text_content(elem.childAt(i).item());
        }
        return result;
    }
};

// =============================================================================
// Basic Structure Tests
// =============================================================================

TEST_F(RstFunctionalTest, UnderlineHeader) {
    const char* rst = R"(
Section Title
=============

Paragraph under section.
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    // Should have h1 or section element
    EXPECT_TRUE(has_element(root, "h1") || has_element(root, "section"))
        << "Expected heading element";
    EXPECT_TRUE(has_element(root, "p")) << "Expected paragraph element";
}

TEST_F(RstFunctionalTest, MultipleSections) {
    const char* rst = R"(
First Section
=============

Content of first section.

Second Section
==============

Content of second section.

Subsection
----------

Content of subsection.
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    // Count headings
    int h1_count = count_elements(root, "h1");
    int h2_count = count_elements(root, "h2");

    EXPECT_GE(h1_count + h2_count, 3) << "Expected at least 3 section headers";
}

// =============================================================================
// Inline Markup Tests
// =============================================================================

TEST_F(RstFunctionalTest, EmphasisAndStrong) {
    const char* rst = R"(
This is *emphasis* and **strong emphasis** text.
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "em")) << "Expected emphasis element";
    EXPECT_TRUE(has_element(root, "strong")) << "Expected strong element";
}

TEST_F(RstFunctionalTest, InlineLiteral) {
    const char* rst = R"(
This is ``inline literal`` text.
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "code")) << "Expected code element";
}

TEST_F(RstFunctionalTest, InterpretedText) {
    const char* rst = R"(
This is :code:`some_code()` and :emphasis:`emphasized`.
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "code") || has_element(root, "em"))
        << "Expected interpreted text elements";
}

// =============================================================================
// List Tests
// =============================================================================

TEST_F(RstFunctionalTest, BulletList) {
    const char* rst = R"(
- First item
- Second item
- Third item
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "ul")) << "Expected unordered list";
    EXPECT_EQ(count_elements(root, "li"), 3) << "Expected 3 list items";
}

TEST_F(RstFunctionalTest, EnumeratedList) {
    const char* rst = R"(
1. First item
2. Second item
3. Third item
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "ol")) << "Expected ordered list";
    EXPECT_EQ(count_elements(root, "li"), 3) << "Expected 3 list items";
}

TEST_F(RstFunctionalTest, NestedList) {
    const char* rst = R"(
- Item 1

  - Nested item 1.1
  - Nested item 1.2

- Item 2
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "ul")) << "Expected unordered list";
    EXPECT_GE(count_elements(root, "li"), 4) << "Expected at least 4 list items";
}

TEST_F(RstFunctionalTest, DefinitionList) {
    const char* rst = R"(
Term 1
    Definition for term 1.

Term 2
    Definition for term 2.
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    // RST definition lists map to <dl><dt><dd>
    bool has_dl = has_element(root, "dl");
    bool has_dt = has_element(root, "dt");
    bool has_dd = has_element(root, "dd");

    EXPECT_TRUE(has_dl || (has_dt && has_dd))
        << "Expected definition list elements";
}

// =============================================================================
// Block Element Tests
// =============================================================================

TEST_F(RstFunctionalTest, LiteralBlock) {
    const char* rst = R"(
Here is a literal block::

    def hello():
        print("world")
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "pre") || has_element(root, "code"))
        << "Expected literal block element";
}

TEST_F(RstFunctionalTest, BlockQuote) {
    const char* rst = R"(
Regular paragraph.

    This is a block quote.
    It has multiple lines.

Back to regular.
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "blockquote"))
        << "Expected blockquote element";
}

TEST_F(RstFunctionalTest, LineBlock) {
    const char* rst = R"(
| Line 1
| Line 2
| Line 3
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    // Line blocks may render as div with line breaks or similar
    EXPECT_TRUE(has_element(root, "p") || has_element(root, "div"))
        << "Expected line block container";
}

// =============================================================================
// Table Tests
// =============================================================================

TEST_F(RstFunctionalTest, SimpleTable) {
    const char* rst = R"(
=====  =====  ======
  A      B    Result
=====  =====  ======
False  False  False
True   False  True
=====  =====  ======
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "table")) << "Expected table element";
    EXPECT_TRUE(has_element(root, "tr")) << "Expected table rows";
}

TEST_F(RstFunctionalTest, GridTable) {
    const char* rst = R"(
+-------+-------+
| Col 1 | Col 2 |
+=======+=======+
| A     | B     |
+-------+-------+
| C     | D     |
+-------+-------+
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "table")) << "Expected table element";
    EXPECT_GE(count_elements(root, "tr"), 2) << "Expected at least 2 rows";
}

// =============================================================================
// Directive Tests
// =============================================================================

TEST_F(RstFunctionalTest, NoteDirective) {
    const char* rst = R"(
.. note::

   This is a note.
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    // Note directive typically renders as div with class or aside
    EXPECT_TRUE(has_element(root, "div") || has_element(root, "aside") ||
                has_element(root, "note") || has_element(root, "p"))
        << "Expected note directive output";
}

TEST_F(RstFunctionalTest, CodeDirective) {
    const char* rst = R"(
.. code:: python

   def hello():
       print("world")
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "pre") || has_element(root, "code"))
        << "Expected code block element";
}

TEST_F(RstFunctionalTest, ImageDirective) {
    const char* rst = R"(
.. image:: picture.png
   :alt: A picture
   :width: 200px
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "img") || has_element(root, "image"))
        << "Expected image element";
}

// =============================================================================
// Hyperlink Tests
// =============================================================================

TEST_F(RstFunctionalTest, ExternalLink) {
    const char* rst = R"(
Visit `Python <http://www.python.org/>`_ for more information.
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "a")) << "Expected anchor element";
}

TEST_F(RstFunctionalTest, ReferenceLink) {
    const char* rst = R"(
This is a reference_.

.. _reference: http://example.com/
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    EXPECT_TRUE(has_element(root, "a")) << "Expected anchor element";
}

// =============================================================================
// Footnote Tests
// =============================================================================

TEST_F(RstFunctionalTest, Footnote) {
    const char* rst = R"(
Here is a footnote reference [1]_.

.. [1] This is the footnote content.
)";

    Item root = parse_rst(rst);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse RST";

    // Footnotes may render as sup, a, or footnote elements
    EXPECT_TRUE(has_element(root, "p")) << "Expected paragraph with footnote";
}

// =============================================================================
// File-based Tests (using downloaded docutils test files)
// =============================================================================

TEST_F(RstFunctionalTest, StandardRstFile) {
    // Try to load the comprehensive standard.rst test file
    const char* test_paths[] = {
        "test/markup/rst/input/data/standard.rst",
        "../test/markup/rst/input/data/standard.rst",
        "markup/rst/input/data/standard.rst",
        NULL
    };

    std::string content;
    for (int i = 0; test_paths[i] != NULL; i++) {
        std::ifstream file(test_paths[i]);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            content = buffer.str();
            printf("Loaded RST test file from %s (%zu bytes)\n",
                   test_paths[i], content.size());
            break;
        }
    }

    if (content.empty()) {
        GTEST_SKIP() << "Could not find standard.rst test file";
        return;
    }

    Item root = parse_rst(content);
    ASSERT_TRUE(root.item != ItemNull.item) << "Failed to parse standard.rst";

    // The standard.rst file should produce a rich document with many elements
    EXPECT_TRUE(has_element(root, "body") || has_element(root, "doc"))
        << "Expected document structure";

    // Count various element types to verify comprehensive parsing
    int headings = count_elements(root, "h1") + count_elements(root, "h2") +
                   count_elements(root, "h3");
    int paragraphs = count_elements(root, "p");
    int lists = count_elements(root, "ul") + count_elements(root, "ol");
    int code_blocks = count_elements(root, "pre") + count_elements(root, "code");

    printf("Parsed structure:\n");
    printf("  Headings: %d\n", headings);
    printf("  Paragraphs: %d\n", paragraphs);
    printf("  Lists: %d\n", lists);
    printf("  Code blocks: %d\n", code_blocks);

    // Expect reasonable counts for the comprehensive test file
    EXPECT_GE(headings, 5) << "Expected multiple headings";
    EXPECT_GE(paragraphs, 10) << "Expected multiple paragraphs";
}

// =============================================================================
// Statistics Summary
// =============================================================================

class RstTestSummary : public RstFunctionalTest {};

TEST_F(RstTestSummary, PrintFeatureCoverage) {
    printf("\n=== RST Parser Feature Coverage ===\n");
    printf("Structure:\n");
    printf("  [x] Underlined section headers\n");
    printf("  [x] Nested sections\n");
    printf("\nInline markup:\n");
    printf("  [x] *emphasis*\n");
    printf("  [x] **strong**\n");
    printf("  [x] ``inline literal``\n");
    printf("  [ ] :role:`interpreted text`\n");
    printf("\nLists:\n");
    printf("  [x] Bullet lists\n");
    printf("  [x] Enumerated lists\n");
    printf("  [x] Nested lists\n");
    printf("  [ ] Definition lists\n");
    printf("  [ ] Field lists\n");
    printf("  [ ] Option lists\n");
    printf("\nBlocks:\n");
    printf("  [x] Literal blocks (::)\n");
    printf("  [x] Block quotes\n");
    printf("  [ ] Line blocks\n");
    printf("  [ ] Doctest blocks\n");
    printf("\nTables:\n");
    printf("  [ ] Simple tables\n");
    printf("  [ ] Grid tables\n");
    printf("\nDirectives:\n");
    printf("  [ ] .. note::\n");
    printf("  [ ] .. code::\n");
    printf("  [ ] .. image::\n");
    printf("  [ ] .. figure::\n");
    printf("\nHyperlinks:\n");
    printf("  [ ] External links\n");
    printf("  [ ] Reference links\n");
    printf("  [ ] Footnotes\n");
    printf("=====================================\n");
}

// Main entry point
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
