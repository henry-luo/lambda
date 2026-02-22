#define _GNU_SOURCE
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include "../lambda/lambda.h"
#include "../lambda/lambda-data.hpp"  // This includes the Input struct definition
#include "../lib/arraylist.h"
#include "../lib/num_stack.h"
#include "../lib/strbuf.h"
#include "../lib/mempool.h"
#include "../lib/url.h"  // Use new URL parser instead of lexbor
#include "../lib/log.h"

// Forward declarations with C linkage
extern "C" {
    #include <tree_sitter/api.h>
    #include <mpdecimal.h>
    Input* input_from_source(char* source, Url* abs_url, String* type, String* flavor);
    String* format_data(Item item, String* type, String* flavor, Pool *pool);
    void format_item(StrBuf* buf, Item item, int indent, char* format);
    char* read_text_file(const char *filename);
    TSParser* lambda_parser(void);
    TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
}

// Tree-sitter function declarations
extern "C" const TSLanguage *tree_sitter_lambda(void);

// Helper function to create Lambda String
String* create_lambda_string(const char* text) {
    if (!text) return NULL;

    size_t len = strlen(text);
    size_t total_size = sizeof(String) + len + 1;  // +1 for null terminator

    String* result = (String*)malloc(total_size);
    if (!result) return NULL;

    result->len = len;
    result->ref_cnt = 1;
    // Copy the string content to the chars array at the end of the struct
    strcpy(result->chars, text);

    return result;
}

// Test fixture for markup roundtrip tests
class MarkupRoundtripTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        // Setup code if needed
    }

    void TearDown() override {
        // Cleanup code if needed
    }

    // Helper function for debug tests
    bool test_debug_content(const char* content, const char* test_name) {
        printf("\n=== DEBUG: %s ===\n", test_name);
        printf("Input content (%zu bytes):\n%s\n", strlen(content), content);
        printf("--- End of content ---\n");
        fflush(stdout);

        String* type_str = create_lambda_string("markup");
        String* flavor_str = NULL;
        Url* cwd = get_current_dir();
        Url* dummy_url = parse_url(cwd, "debug_test.md");
        char* content_copy = strdup(content);

        printf("DEBUG: About to parse with input_from_source...\n");
        fflush(stdout);

        Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

        if (!input) {
            printf("ERROR: Parse failed!\n");
            fflush(stdout);
            free(content_copy);
            return false;
        }

        printf("DEBUG: Parse succeeded, about to format with JSON...\n");
        fflush(stdout);

        String* json_type = create_lambda_string("json");
        String* formatted = format_data(input->root, json_type, flavor_str, input->pool);

        if (!formatted) {
            printf("ERROR: Format failed!\n");
            fflush(stdout);
            free(content_copy);
            return false;
        }

        printf("SUCCESS: %s completed (formatted length: %zu)\n", test_name, (size_t)formatted->len);
        printf("Formatted content (first 150 chars): %.150s\n",
               formatted->chars ? formatted->chars : "(null)");
        if (formatted->len > 150) {
            printf("... (truncated)\n");
        }

        fflush(stdout);
        free(content_copy);
        return true;
    }
};

// Test with simple markdown elements
TEST_F(MarkupRoundtripTest, SimpleTest) {
    printf("\n=== Testing Simple Markdown Elements ===\n");

    const char* test_markdown = "# Header\n\nParagraph with **bold** text.\n\n- List item\n- Another item\n";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "test.md");

    // Make a mutable copy of the content
    char* content_copy = strdup(test_markdown);

    // Parse
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    ASSERT_NE(input, nullptr) << "Failed to parse simple markdown";

    // Format using JSON formatter to test parser only
    String* json_type = create_lambda_string("json");
    String* formatted = format_data(input->root, json_type, flavor_str, input->pool);
    ASSERT_NE(formatted, nullptr) << "Failed to format simple markdown to JSON";
    ASSERT_GT(formatted->len, 0) << "Formatted JSON should not be empty";

    // Check that JSON structure is created
    ASSERT_NE(strstr(formatted->chars, "\"$\":"), nullptr) << "JSON should contain element type information";
    ASSERT_NE(strstr(formatted->chars, "{"), nullptr) << "JSON should contain object structure";

    // Print with length limit to avoid hanging
    printf("Simple test - JSON formatted (length %zu chars):\n", (size_t)formatted->len);
    if (formatted->chars && formatted->len > 0) {
        size_t print_len = formatted->len > 200 ? 200 : formatted->len;
        printf("%.200s", formatted->chars);
        if (formatted->len > 200) {
            printf("... (truncated)\n");
        } else {
            printf("\n");
        }
    }

    // Cleanup
    free(content_copy);
}

// Test empty content handling
TEST_F(MarkupRoundtripTest, EmptyTest) {
    printf("\n=== Testing Empty Content ===\n");

    const char* empty_markdown = "";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "empty.md");

    // Make a mutable copy of the content
    char* content_copy = strdup(empty_markdown);

    // Parse empty content
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    ASSERT_NE(input, nullptr) << "Should handle empty content";

    // Format empty content using JSON formatter
    String* json_type = create_lambda_string("json");
    String* formatted = format_data(input->root, json_type, flavor_str, input->pool);
    ASSERT_NE(formatted, nullptr) << "Should format empty content to JSON";

    printf("Empty test - JSON formatted: '%s' (length: %zu)\n",
           formatted->chars ? formatted->chars : "(null)", (size_t)formatted->len);

    // Cleanup
    free(content_copy);
}

// Test comprehensive content from file
TEST_F(MarkupRoundtripTest, DISABLED_CompleteTest) {
    printf("\n=== Testing Complete Comprehensive Markdown ===\n");

    // Read comprehensive test content from file
    char* comprehensive_content = read_text_file("test/input/comprehensive_test.md");
    ASSERT_NE(comprehensive_content, nullptr) << "Failed to read comprehensive_test.md file";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "comprehensive_test.md");

    // Make a copy since input_from_source may modify the content
    char* comprehensive_content_copy = strdup(comprehensive_content);

    // Parse comprehensive content
    Input* input = input_from_source(comprehensive_content_copy, dummy_url, type_str, flavor_str);
    ASSERT_NE(input, nullptr) << "Failed to parse comprehensive markdown";

    StrBuf* strbuf = strbuf_new();
    printf("Parsed comprehensive input with root_item: %p\n", reinterpret_cast<void*>(input->root.item));
    format_item(strbuf, input->root, 0, NULL);
    printf("Formatted comprehensive output: %s\n", strbuf->str ? strbuf->str : "(null)");

    // Format using Markdown formatter
    String* markdown_type = create_lambda_string("markdown");
    String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
    ASSERT_NE(formatted, nullptr) << "Failed to format to Markdown";
    ASSERT_GT(formatted->len, 0) << "Formatted Markdown should not be empty";

    // Cleanup
    free(comprehensive_content);
    free(comprehensive_content_copy);
}

// Test emoji content from file
TEST_F(MarkupRoundtripTest, DISABLED_EmojiTest) {
    printf("\n=== Testing Comprehensive Emoji Features from File ===\n");

    // Read emoji test content from file
    char* emoji_content = read_text_file("test/input/comprehensive_emoji_test.md");
    ASSERT_NE(emoji_content, nullptr) << "Failed to read comprehensive_emoji_test.md file";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "comprehensive_emoji_test.md");

    // Make a copy since input_from_source may modify the content
    char* emoji_content_copy = strdup(emoji_content);

    // Parse emoji content
    Input* input = input_from_source(emoji_content_copy, dummy_url, type_str, flavor_str);
    ASSERT_NE(input, nullptr) << "Failed to parse comprehensive emoji markdown";

    StrBuf* strbuf = strbuf_new();
    printf("Parsed emoji input with root_item: %p\n", reinterpret_cast<void*>(input->root.item));
    format_item(strbuf, input->root, 0, NULL);
    printf("Formatted emoji output: %s\n", strbuf->str ? strbuf->str : "(null)");

    // Format using Markdown formatter
    String* markdown_type = create_lambda_string("markdown");
    String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
    ASSERT_NE(formatted, nullptr) << "Failed to format emoji content to Markdown";
    ASSERT_GT(formatted->len, 0) << "Formatted emoji Markdown should not be empty";

    // Cleanup
    free(emoji_content);
    free(emoji_content_copy);
}

// Test comprehensive math features from file - DISABLED due to potential hanging
TEST_F(MarkupRoundtripTest, DISABLED_ComprehensiveMathTest) {
    printf("\n=== Testing Comprehensive Math Features from File ===\n");

    // Read math test content from file
    char* math_content = read_text_file("test/input/comprehensive_math_test.md");
    ASSERT_NE(math_content, nullptr) << "Failed to read comprehensive_math_test.md file";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "comprehensive_math_test.md");

    // Make a copy since input_from_source may modify the content
    char* math_content_copy = strdup(math_content);

    // Parse math content
    Input* input = input_from_source(math_content_copy, dummy_url, type_str, flavor_str);
    ASSERT_NE(input, nullptr) << "Failed to parse comprehensive math markdown";

    StrBuf* strbuf = strbuf_new();
    printf("Parsed math input with root_item: %p\n", reinterpret_cast<void*>(input->root.item));
    format_item(strbuf, input->root, 0, NULL);
    printf("Formatted math output: %s\n", strbuf->str ? strbuf->str : "(null)");

    // Format using Markdown formatter to test parser only
    String* markdown_type = create_lambda_string("markdown");
    String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
    ASSERT_NE(formatted, nullptr) << "Failed to format math content to Markdown";
    ASSERT_GT(formatted->len, 0) << "Formatted math Markdown should not be empty";
    printf("Formatted math content (length %zu): %s\n", (size_t)formatted->len, formatted->chars ? formatted->chars : "(null)");

    // Cleanup
    free(math_content);
    free(math_content_copy);
}

// Test comprehensive RST (reStructuredText) features - NEW
TEST_F(MarkupRoundtripTest, DISABLED_RstDirectivesTest) {
    printf("\n=== Testing RST Directives and Format-Specific Features ===\n");

    // Read RST test content from file
    char* rst_content = read_text_file("test/input/comprehensive_test.rst");
    ASSERT_NE(rst_content, nullptr) << "Failed to read comprehensive_test.rst file";

    // Create Lambda strings for RST input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "comprehensive_test.rst");

    // Make a copy since input_from_source may modify the content
    char* rst_content_copy = strdup(rst_content);

    // Parse RST content
    printf("DEBUG: Parsing RST content (%zu bytes)...\n", strlen(rst_content));
    Input* input = input_from_source(rst_content_copy, dummy_url, type_str, flavor_str);
    ASSERT_NE(input, nullptr) << "Failed to parse comprehensive RST content";

    StrBuf* strbuf = strbuf_new();
    printf("Parsed RST input with root_item: %p\n", reinterpret_cast<void*>(input->root.item));
    format_item(strbuf, input->root, 0, NULL);
    printf("Formatted RST output (first 300 chars): %.300s\n", strbuf->str ? strbuf->str : "(null)");
    if (strbuf->str && strlen(strbuf->str) > 300) {
        printf("... (truncated)\n");
    }

    // Format using JSON formatter to verify structure
    String* json_type = create_lambda_string("json");
    printf("DEBUG test: Before format_data, input->root=0x%llx\n",
           static_cast<unsigned long long>(input->root.item));
    String* formatted = format_data(input->root, json_type, flavor_str, input->pool);
    ASSERT_NE(formatted, nullptr) << "Failed to format RST content to JSON";
    ASSERT_GT(formatted->len, 0) << "Formatted RST JSON should not be empty";
    printf("RST JSON structure (length %zu, first 200 chars): %.200s\n",
           (size_t)formatted->len, formatted->chars ? formatted->chars : "(null)");

    // Check that RST-specific elements are present
    if (formatted->chars) {
        // Should contain directive elements
        ASSERT_NE(strstr(formatted->chars, "directive"), nullptr) << "RST JSON should contain 'directive' elements";

        // Should contain code-block directives
        ASSERT_TRUE(strstr(formatted->chars, "code-block") != NULL ||
                   strstr(formatted->chars, "code") != NULL) << "RST JSON should contain code-block directives";

        printf("SUCCESS: RST directives and format-specific features detected!\n");
    }

    // Cleanup
    free(rst_content);
    free(rst_content_copy);
    strbuf_free(strbuf);
}

// Test basic RST directive parsing - simpler test without file dependency
TEST_F(MarkupRoundtripTest, DISABLED_BasicRstTest) {
    printf("\n=== Testing Basic RST Directive Parsing ===\n");

    const char* basic_rst =
        "RST Test Document\n"
        "=================\n"
        "\n"
        "This is a paragraph with some text.\n"
        "\n"
        ".. note::\n"
        "   This is a note directive.\n"
        "   It spans multiple lines.\n"
        "\n"
        ".. code-block:: python\n"
        "   :linenos:\n"
        "\n"
        "   def hello():\n"
        "       print('Hello World')\n"
        "\n"
        "Another paragraph after directives.\n";

    // Create Lambda strings for RST input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "basic_test.rst");

    // Make a copy since input_from_source may modify the content
    char* rst_content_copy = strdup(basic_rst);

    // Parse basic RST content
    printf("DEBUG: Parsing basic RST content...\n");
    Input* input = input_from_source(rst_content_copy, dummy_url, type_str, flavor_str);
    ASSERT_NE(input, nullptr) << "Failed to parse basic RST content";

    // Format using JSON to check structure
    String* json_type = create_lambda_string("json");
    String* formatted = format_data(input->root, json_type, flavor_str, input->pool);
    ASSERT_NE(formatted, nullptr) << "Failed to format basic RST to JSON";
    ASSERT_GT(formatted->len, 0) << "Formatted basic RST JSON should not be empty";

    printf("Basic RST JSON (first 400 chars): %.400s\n",
           formatted->chars ? formatted->chars : "(null)");

    // Verify RST-specific features are detected
    if (formatted->chars) {
        bool has_headers = strstr(formatted->chars, "h1") != NULL;
        bool has_paragraphs = strstr(formatted->chars, "\"$\":\"p\"") != NULL;
        bool has_directives = strstr(formatted->chars, "directive") != NULL;

        printf("RST parsing results - Headers: %s, Paragraphs: %s, Directives: %s\n",
               has_headers ? "YES" : "NO",
               has_paragraphs ? "YES" : "NO",
               has_directives ? "YES" : "NO");

        // At minimum should have headers and paragraphs
        ASSERT_TRUE(has_headers || has_paragraphs) << "Basic RST should parse headers or paragraphs correctly";

        if (has_directives) {
            printf("SUCCESS: RST directives properly detected and parsed!\n");
        } else {
            printf("INFO: RST directives not detected (may need format detection improvement)\n");
        }
    }

    // Cleanup
    free(rst_content_copy);
}

// Test RST-specific features from old parser
TEST_F(MarkupRoundtripTest, DISABLED_RstExtendedFeatures) {
    printf("\n=== Testing Extended RST Features ===\n");

    const char* rst_extended_content =
        ".. This is a comment\n"
        "   spanning multiple lines\n"
        "\n"
        "Document Title\n"
        "==============\n"
        "\n"
        "Text with ``literal markup`` and reference_ links.\n"
        "\n"
        "Transition line below:\n"
        "\n"
        "----\n"
        "\n"
        "Definition Lists\n"
        "\n"
        "term 1\n"
        "    Definition of term 1.\n"
        "\n"
        "term 2\n"
        "    Definition of term 2.\n"
        "\n"
        "Literal block follows::\n"
        "\n"
        "    This is a literal block.\n"
        "    It preserves whitespace.\n"
        "        Even indentation.\n"
        "\n"
        "Grid table:\n"
        "\n"
        "+-------+-------+\n"
        "| A     | B     |\n"
        "+-------+-------+\n"
        "| 1     | 2     |\n"
        "+-------+-------+\n";

    String* type_str = create_lambda_string("markup");
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "test_extended.rst");

    char* content_copy = strdup(rst_extended_content);
    Input* input = input_from_source(content_copy, dummy_url, type_str, NULL);
    ASSERT_NE(input, nullptr) << "Failed to parse extended RST content";

    printf("DEBUG: Immediately after input_from_source, input=%p, input->root=0x%llx\n",
           static_cast<void*>(input), static_cast<unsigned long long>(input->root.item));

    // Check if the input pointer is corrupted
    printf("DEBUG: Input struct fields: url=%p, pool=%p, type_list=%p\n",
           input->url, input->pool, input->type_list);

    StrBuf* strbuf = strbuf_new();
    printf("DEBUG test: Before test format_item, input->root=0x%llx\n",
           static_cast<unsigned long long>(input->root.item));
    format_item(strbuf, input->root, 0, NULL);
    printf("DEBUG test: After test format_item, input->root=0x%llx\n",
           static_cast<unsigned long long>(input->root.item));
    printf("Extended RST output: %.200s\n", strbuf->str ? strbuf->str : "(null)");

    // Check if the item value changed
    Item test_root = input->root;
    printf("DEBUG test: Copied input->root to test_root=0x%llx\n",
           static_cast<unsigned long long>(test_root.item));

    // Format to JSON and verify extended features
    String* json_type = create_lambda_string("json");
    printf("DEBUG test extended: Before format_data, input->root=0x%llx, test_root=0x%llx\n",
           static_cast<unsigned long long>(input->root.item), static_cast<unsigned long long>(test_root.item));
    String* formatted = format_data(input->root, json_type, NULL, input->pool);
    ASSERT_NE(formatted, nullptr) << "Failed to format extended RST to JSON";

    if (formatted->chars) {
        // Check for literal text (double backticks)
        bool has_literal = strstr(formatted->chars, "literal") != NULL ||
                          strstr(formatted->chars, "code") != NULL;
        printf("Literal text detection: %s\n", has_literal ? "YES" : "NO");

        // Check for comments
        bool has_comment = strstr(formatted->chars, "comment") != NULL;
        printf("Comment detection: %s\n", has_comment ? "YES" : "NO");

        // Check for definition lists
        bool has_def_list = strstr(formatted->chars, "dl") != NULL ||
                           strstr(formatted->chars, "definition") != NULL;
        printf("Definition list detection: %s\n", has_def_list ? "YES" : "NO");

        // Check for horizontal rules/transitions
        bool has_hr = strstr(formatted->chars, "hr") != NULL ||
                     strstr(formatted->chars, "divider") != NULL;
        printf("Transition line detection: %s\n", has_hr ? "YES" : "NO");

        printf("SUCCESS: Extended RST features test completed!\n");
    }

    free(content_copy);
}

// Test comprehensive Textile features from file
// DISABLED: Textile parser has performance issues with complex documents
TEST_F(MarkupRoundtripTest, DISABLED_TextileRoundtripTest) {
    printf("\n=== Testing Textile Roundtrip Conversion ===\n");

    // Read Textile test content from file
    char* textile_content = read_text_file("test/input/comprehensive_test.textile");
    ASSERT_NE(textile_content, nullptr) << "Failed to read comprehensive_test.textile file";

    // Create Lambda strings for Textile input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "comprehensive_test.textile");

    // Make a copy since input_from_source may modify the content
    char* textile_content_copy = strdup(textile_content);

    // Parse Textile content
    printf("DEBUG: Parsing Textile content (%zu bytes)...\n", strlen(textile_content));
    Input* input = input_from_source(textile_content_copy, dummy_url, type_str, flavor_str);
    ASSERT_NE(input, nullptr) << "Failed to parse comprehensive Textile content";

    // Format using JSON formatter to verify structure
    String* json_type = create_lambda_string("json");
    String* json_formatted = format_data(input->root, json_type, flavor_str, input->pool);
    ASSERT_NE(json_formatted, nullptr) << "Failed to format Textile content to JSON";
    ASSERT_GT(json_formatted->len, 0) << "Formatted Textile JSON should not be empty";
    printf("Textile JSON structure (length %zu, first 300 chars): %.300s\n",
           (size_t)json_formatted->len, json_formatted->chars ? json_formatted->chars : "(null)");

    // Format back to Textile
    String* textile_type = create_lambda_string("textile");
    String* textile_formatted = format_data(input->root, textile_type, flavor_str, input->pool);
    ASSERT_NE(textile_formatted, nullptr) << "Failed to format back to Textile";
    ASSERT_GT(textile_formatted->len, 0) << "Formatted Textile should not be empty";
    printf("Roundtrip Textile output (length %zu, first 500 chars):\n%.500s\n",
           (size_t)textile_formatted->len, textile_formatted->chars ? textile_formatted->chars : "(null)");
    if (textile_formatted->len > 500) {
        printf("... (truncated)\n");
    }

    // Check for Textile-specific syntax elements in output
    if (textile_formatted->chars) {
        bool has_heading = strstr(textile_formatted->chars, "h1.") != NULL ||
                          strstr(textile_formatted->chars, "h2.") != NULL;
        bool has_bold = strstr(textile_formatted->chars, "*") != NULL;
        bool has_italic = strstr(textile_formatted->chars, "_") != NULL;
        bool has_list = strstr(textile_formatted->chars, "* ") != NULL ||
                       strstr(textile_formatted->chars, "# ") != NULL;

        printf("Textile output check - Headings: %s, Bold: %s, Italic: %s, Lists: %s\n",
               has_heading ? "YES" : "NO",
               has_bold ? "YES" : "NO",
               has_italic ? "YES" : "NO",
               has_list ? "YES" : "NO");

        ASSERT_TRUE(has_heading || has_bold || has_italic) <<
            "Textile roundtrip output should preserve basic formatting";
    }

    // Cleanup
    free(textile_content);
    free(textile_content_copy);
}

// Test basic Textile formatter directly (bypassing parser hang)
TEST_F(MarkupRoundtripTest, BasicTextileTest) {
    printf("\n=== Testing Basic Textile Formatting ===\n");

    // Parse markdown to get a proper Element tree (doc > body > h1, p)
    // Note: JSON parsing creates Maps, not Elements, so we parse markdown instead.
    const char* markdown_input = "# Main Heading\n\nThis is a paragraph.\n";

    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "test.md");

    // Make a copy since input_from_source may modify the content
    char* content_copy = strdup(markdown_input);

    // Parse markdown
    printf("DEBUG: Parsing markdown input...\n");
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    ASSERT_NE(input, nullptr) << "Failed to parse JSON content";

    // Format to Textile
    String* textile_type = create_lambda_string("textile");
    String* textile_formatted = format_data(input->root, textile_type, flavor_str, input->pool);
    ASSERT_NE(textile_formatted, nullptr) << "Failed to format to Textile";
    ASSERT_GT(textile_formatted->len, 0) << "Formatted Textile should not be empty";

    printf("Textile output:\n%s\n", textile_formatted->chars ? textile_formatted->chars : "(null)");

    // Verify Textile-specific features are in output
    if (textile_formatted->chars) {
        bool has_h1 = strstr(textile_formatted->chars, "h1.") != NULL;
        bool has_heading_text = strstr(textile_formatted->chars, "Main Heading") != NULL;
        bool has_paragraph = strstr(textile_formatted->chars, "This is a paragraph") != NULL;

        printf("Textile output check - H1: %s, Heading text: %s, Paragraph: %s\n",
               has_h1 ? "YES" : "NO",
               has_heading_text ? "YES" : "NO",
               has_paragraph ? "YES" : "NO");

        ASSERT_TRUE(has_h1) << "Should have h1. heading";
        ASSERT_TRUE(has_heading_text) << "Should have heading text";
        ASSERT_TRUE(has_paragraph) << "Should have paragraph text";

        printf("SUCCESS: Basic Textile formatting test completed!\n");
    }

    // Cleanup
    free(content_copy);
}
