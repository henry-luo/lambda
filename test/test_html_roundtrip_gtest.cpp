#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include "../lambda/lambda-data.hpp"
#include "../lib/arraylist.h"
#include "../lib/num_stack.h"
#include "../lib/strbuf.h"
#include "../lib/mempool.h"
#include "../lib/url.h"

extern "C" {
    Input* input_from_source(char* source, Url* abs_url, String* type, String* flavor);
    Input* input_from_url(String* url, String* type, String* flavor, Url* cwd);
    String* format_data(Item item, String* type, String* flavor, Pool *pool);

    // Use actual URL library functions
    Url* url_parse(const char* input);
    Url* url_parse_with_base(const char* input, const Url* base);
    void url_destroy(Url* url);
}

// Helper function to create a Lambda String from C string
String* create_lambda_string(const char* text) {
    if (!text) return NULL;

    size_t len = strlen(text);
    // Allocate String struct + space for the null-terminated string
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return NULL;

    result->len = len;
    result->ref_cnt = 1;
    // Copy the string content to the chars array at the end of the struct
    strcpy(result->chars, text);

    return result;
}

// Helper function to read file contents
char* read_file_content(const char* filepath) {
    FILE* file = fopen(filepath, "r");
    if (!file) {
        printf("ERROR: Failed to open file: %s\n", filepath);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(length + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    size_t read_bytes = fread(content, 1, length, file);
    content[read_bytes] = '\0';
    fclose(file);

    return content;
}

// Helper function to check if a string is valid
bool is_valid_string_content(const char* content) {
    if (!content) return false;

    // Basic validation - check for null bytes and reasonable length
    size_t len = strlen(content);
    if (len == 0 || len > 1000000) return false;  // Reject empty or huge strings

    return true;
}

// Helper function to normalize whitespace for comparison
char* normalize_whitespace(const char* input) {
    if (!input) return NULL;

    size_t len = strlen(input);
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;

    size_t write_pos = 0;
    bool in_whitespace = false;

    for (size_t i = 0; i < len; i++) {
        if (isspace(input[i])) {
            if (!in_whitespace && write_pos > 0) {
                result[write_pos++] = ' ';
                in_whitespace = true;
            }
        } else {
            result[write_pos++] = input[i];
            in_whitespace = false;
        }
    }

    // Remove trailing whitespace
    while (write_pos > 0 && isspace(result[write_pos - 1])) {
        write_pos--;
    }

    result[write_pos] = '\0';
    return result;
}

// Helper function to compare strings with normalized whitespace
bool strings_equal_normalized(const char* str1, const char* str2) {
    if (!str1 && !str2) return true;
    if (!str1 || !str2) return false;

    char* norm1 = normalize_whitespace(str1);
    char* norm2 = normalize_whitespace(str2);

    bool equal = false;
    if (norm1 && norm2) {
        equal = (strcmp(norm1, norm2) == 0);
    }

    free(norm1);
    free(norm2);
    return equal;
}

// Test fixture class for HTML roundtrip tests
class HtmlRoundtripTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test environment
    }

    void TearDown() override {
        // Clean up any test files or resources
    }

    // Core roundtrip function: parse HTML string, format back, verify result
    // Returns: {success, formatted_string, formatted_length}
    struct RoundtripResult {
        bool success;
        String* formatted;
        const char* error_message;
    };

    RoundtripResult test_html_source_roundtrip(const char* html_source, const char* source_name = "inline") {
        printf("\n=== Testing HTML roundtrip: %s ===\n", source_name);
        printf("Original content length: %zu\n", strlen(html_source));

        // Create Lambda strings for input parameters
        String* type_str = create_lambda_string("html");
        String* flavor_str = NULL;

        // Get current directory for URL resolution
        Url* cwd = url_parse("file://./");
        Url* dummy_url = url_parse_with_base("test.html", cwd);

        printf("Parsing HTML with input_from_source...\n");

        // Make a mutable copy of the content
        char* content_copy = strdup(html_source);

        // Parse the HTML content
        Input* parsed_input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

        if (!parsed_input) {
            printf("ERROR: Failed to parse HTML content\n");
            free(content_copy);
            url_destroy(dummy_url);
            url_destroy(cwd);
            return {false, nullptr, "Failed to parse HTML content"};
        }

        printf("HTML parsed successfully\n");

        // Get the root item from the parsed input
        Item root_item = parsed_input->root;

        printf("Formatting back to HTML...\n");

        // Format the parsed data back to HTML
        String* formatted = format_data(root_item, type_str, flavor_str, parsed_input->pool);

        if (!formatted) {
            printf("ERROR: Failed to format HTML data\n");
            free(content_copy);
            url_destroy(dummy_url);
            url_destroy(cwd);
            return {false, nullptr, "Failed to format HTML data"};
        }

        printf("Formatted content length: %u\n", formatted->len);

        // Validate the formatted content
        bool is_valid = (formatted->len > 0 && is_valid_string_content(formatted->chars));

        printf("Content validation result: %s\n", is_valid ? "VALID" : "INVALID");

        if (!is_valid) {
            printf("ERROR: Invalid formatted output\n");
            free(content_copy);
            url_destroy(dummy_url);
            url_destroy(cwd);
            return {false, formatted, "Invalid formatted output"};
        }

        // Verify exact roundtrip: input should match output
        size_t original_len = strlen(html_source);
        bool exact_match = (original_len == formatted->len &&
                           strcmp(html_source, formatted->chars) == 0);

        printf("Roundtrip exact match: %s\n", exact_match ? "YES" : "NO");

        if (!exact_match) {
            printf("ERROR: Roundtrip mismatch!\n");
            printf("  Original length: %zu\n", original_len);
            printf("  Formatted length: %u\n", formatted->len);
            printf("  Original (first 200 chars):\n%.200s\n", html_source);
            printf("  Formatted (first 200 chars):\n%.200s\n", formatted->chars);

            // Show where the difference occurs
            size_t min_len = (original_len < formatted->len) ? original_len : formatted->len;
            for (size_t i = 0; i < min_len; i++) {
                if (html_source[i] != formatted->chars[i]) {
                    printf("  First difference at position %zu:\n", i);
                    printf("    Original: '%c' (0x%02X)\n", html_source[i], (unsigned char)html_source[i]);
                    printf("    Formatted: '%c' (0x%02X)\n", formatted->chars[i], (unsigned char)formatted->chars[i]);
                    break;
                }
            }
        } else {
            printf("Formatted output (first 200 chars):\n%.200s\n", formatted->chars);
        }

        // Clean up
        free(content_copy);
        url_destroy(dummy_url);
        url_destroy(cwd);

        return {exact_match, formatted, exact_match ? nullptr : "Roundtrip content mismatch"};
    }

    // Convenience wrapper for file-based tests
    bool test_html_file_roundtrip(const char* test_file, const char* test_name) {
        printf("\n=== Testing HTML file roundtrip: %s ===\n", test_name);

        // Read the test file
        char* original_content = read_file_content(test_file);
        if (!original_content) {
            printf("ERROR: Failed to read test file: %s\n", test_file);
            return false;
        }

        // Use the core roundtrip function
        auto result = test_html_source_roundtrip(original_content, test_name);

        // Clean up
        free(original_content);

        return result.success;
    }
};

// Basic HTML Tests
class BasicHtmlTests : public HtmlRoundtripTest {};

TEST_F(BasicHtmlTests, SimpleHtmlRoundtrip) {
    const char* simple_html = "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>Test</title></head>\n"
        "<body>\n"
        "<h1>Hello Lambda</h1>\n"
        "<p>This is a simple test.</p>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_source_roundtrip(simple_html, "SimpleHtmlRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");
    ASSERT_NE(result.formatted, nullptr) << "Formatted HTML should not be null";
    ASSERT_GT(result.formatted->len, 0U) << "Formatted HTML should not be empty";

    printf("Simple HTML roundtrip completed successfully\n");
    printf("Original length: %zu, Formatted length: %u\n",
           strlen(simple_html), result.formatted->len);
}

TEST_F(BasicHtmlTests, HtmlWithAttributesRoundtrip) {
    const char* html_with_attrs = "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "<meta charset=\"UTF-8\">\n"
        "<title>Attribute Test</title>\n"
        "</head>\n"
        "<body>\n"
        "<div class=\"container\" id=\"main\">\n"
        "<p style=\"color: blue;\">Styled paragraph</p>\n"
        "<a href=\"https://example.com\" target=\"_blank\">Link</a>\n"
        "</div>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_source_roundtrip(html_with_attrs, "HtmlWithAttributesRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");
    ASSERT_NE(result.formatted, nullptr) << "Formatted HTML should not be null";
    ASSERT_GT(result.formatted->len, 0U) << "Formatted HTML should not be empty";

    printf("HTML with attributes roundtrip completed successfully\n");
}

TEST_F(BasicHtmlTests, NestedElementsRoundtrip) {
    const char* nested_html = "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>Nested Elements</title></head>\n"
        "<body>\n"
        "<div>\n"
        "<ul>\n"
        "<li>Item 1</li>\n"
        "<li>Item 2\n"
        "<ul>\n"
        "<li>Nested 1</li>\n"
        "<li>Nested 2</li>\n"
        "</ul>\n"
        "</li>\n"
        "<li>Item 3</li>\n"
        "</ul>\n"
        "</div>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_source_roundtrip(nested_html, "NestedElementsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");
    ASSERT_NE(result.formatted, nullptr) << "Formatted HTML should not be null";
    ASSERT_GT(result.formatted->len, 0U) << "Formatted HTML should not be empty";

    printf("Nested HTML roundtrip completed successfully\n");
}

// HTML File Tests - test with actual files from ./test/html/
// Organized by complexity: Simple -> Intermediate -> Advanced -> Complex

// ==== SIMPLE HTML FILES (Basic structure, minimal CSS) ====
class SimpleHtmlFileTests : public HtmlRoundtripTest {};

TEST_F(SimpleHtmlFileTests, TestWhitespace) {
    bool result = test_html_file_roundtrip("./test/html/test_whitespace.html", "test_whitespace");
    EXPECT_TRUE(result) << "Whitespace test HTML should succeed";
}

TEST_F(SimpleHtmlFileTests, TestClearSimple) {
    bool result = test_html_file_roundtrip("./test/html/test_clear_simple.html", "test_clear_simple");
    EXPECT_TRUE(result) << "Simple clear test HTML should succeed";
}

TEST_F(SimpleHtmlFileTests, SimpleBoxTest) {
    bool result = test_html_file_roundtrip("./test/html/simple_box_test.html", "simple_box_test");
    EXPECT_TRUE(result) << "Simple box test HTML should succeed";
}

TEST_F(SimpleHtmlFileTests, SimpleTableTest) {
    bool result = test_html_file_roundtrip("./test/html/simple_table_test.html", "simple_table_test");
    EXPECT_TRUE(result) << "Simple table test HTML should succeed";
}

TEST_F(SimpleHtmlFileTests, TableSimple) {
    bool result = test_html_file_roundtrip("./test/html/table_simple.html", "table_simple");
    EXPECT_TRUE(result) << "Simple table HTML should succeed";
}

TEST_F(SimpleHtmlFileTests, TableBasic) {
    bool result = test_html_file_roundtrip("./test/html/table_basic.html", "table_basic");
    EXPECT_TRUE(result) << "Basic table HTML should succeed";
}

// ==== INTERMEDIATE HTML FILES (CSS styling, basic layouts) ====
class IntermediateHtmlFileTests : public HtmlRoundtripTest {};

TEST_F(IntermediateHtmlFileTests, Sample2) {
    bool result = test_html_file_roundtrip("./test/html/sample2.html", "sample2");
    EXPECT_TRUE(result) << "Sample2 HTML with flexbox should succeed";
}

TEST_F(IntermediateHtmlFileTests, Sample3) {
    bool result = test_html_file_roundtrip("./test/html/sample3.html", "sample3");
    EXPECT_TRUE(result) << "Sample3 HTML with navigation should succeed";
}

TEST_F(IntermediateHtmlFileTests, Sample4) {
    bool result = test_html_file_roundtrip("./test/html/sample4.html", "sample4");
    EXPECT_TRUE(result) << "Sample4 landing page HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, SampleHtml) {
    bool result = test_html_file_roundtrip("./test/html/sample.html", "sample");
    EXPECT_TRUE(result) << "Sample HTML file should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestFloatBasic) {
    bool result = test_html_file_roundtrip("./test/html/test_float_basic.html", "test_float_basic");
    EXPECT_TRUE(result) << "Basic float test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestClearLeft) {
    bool result = test_html_file_roundtrip("./test/html/test_clear_left.html", "test_clear_left");
    EXPECT_TRUE(result) << "Clear left test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestClearRight) {
    bool result = test_html_file_roundtrip("./test/html/test_clear_right.html", "test_clear_right");
    EXPECT_TRUE(result) << "Clear right test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestClearProperty) {
    bool result = test_html_file_roundtrip("./test/html/test_clear_property.html", "test_clear_property");
    EXPECT_TRUE(result) << "Clear property test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestLineHeight) {
    bool result = test_html_file_roundtrip("./test/html/test_line_height.html", "test_line_height");
    EXPECT_TRUE(result) << "Line height test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestLineBoxAdjustment) {
    bool result = test_html_file_roundtrip("./test/html/test_line_box_adjustment.html", "test_line_box_adjustment");
    EXPECT_TRUE(result) << "Line box adjustment test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestMarginCollapse) {
    bool result = test_html_file_roundtrip("./test/html/test_margin_collapse.html", "test_margin_collapse");
    EXPECT_TRUE(result) << "Margin collapse test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestOverflow) {
    bool result = test_html_file_roundtrip("./test/html/test_overflow.html", "test_overflow");
    EXPECT_TRUE(result) << "Overflow test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestPercentage) {
    bool result = test_html_file_roundtrip("./test/html/test_percentage.html", "test_percentage");
    EXPECT_TRUE(result) << "Percentage test HTML should succeed";
}

// ==== ADVANCED HTML FILES (Complex layouts, positioning, grid/flex) ====
class AdvancedHtmlFileTests : public HtmlRoundtripTest {};

TEST_F(AdvancedHtmlFileTests, BoxHtml) {
    bool result = test_html_file_roundtrip("./test/html/box.html", "box");
    EXPECT_TRUE(result) << "Box HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, FlexHtml) {
    bool result = test_html_file_roundtrip("./test/html/flex.html", "flex");
    EXPECT_TRUE(result) << "Flex HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestPositioningSimple) {
    bool result = test_html_file_roundtrip("./test/html/test_positioning_simple.html", "test_positioning_simple");
    EXPECT_TRUE(result) << "Simple positioning test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestPositioningBasic) {
    bool result = test_html_file_roundtrip("./test/html/test_positioning_basic.html", "test_positioning_basic");
    EXPECT_TRUE(result) << "Basic positioning test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestCompletePositioning) {
    bool result = test_html_file_roundtrip("./test/html/test_complete_positioning.html", "test_complete_positioning");
    EXPECT_TRUE(result) << "Complete positioning test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, PositionHtml) {
    bool result = test_html_file_roundtrip("./test/html/position.html", "position");
    EXPECT_TRUE(result) << "Position HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, DebugPosition) {
    bool result = test_html_file_roundtrip("./test/html/debug_position.html", "debug_position");
    EXPECT_TRUE(result) << "Debug position HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestGridBasic) {
    bool result = test_html_file_roundtrip("./test/html/test_grid_basic.html", "test_grid_basic");
    EXPECT_TRUE(result) << "Basic grid test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestGridAreas) {
    bool result = test_html_file_roundtrip("./test/html/test_grid_areas.html", "test_grid_areas");
    EXPECT_TRUE(result) << "Grid areas test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestGridAdvanced) {
    bool result = test_html_file_roundtrip("./test/html/test_grid_advanced.html", "test_grid_advanced");
    EXPECT_TRUE(result) << "Advanced grid test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, GridHtml) {
    bool result = test_html_file_roundtrip("./test/html/grid.html", "grid");
    EXPECT_TRUE(result) << "Grid HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, TableHtml) {
    bool result = test_html_file_roundtrip("./test/html/table.html", "table");
    EXPECT_TRUE(result) << "Table HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, IndexHtml) {
    bool result = test_html_file_roundtrip("./test/html/index.html", "index");
    EXPECT_TRUE(result) << "Index HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, LayoutHtm) {
    bool result = test_html_file_roundtrip("./test/html/layout.htm", "layout");
    EXPECT_TRUE(result) << "Layout HTM file should succeed";
}

TEST_F(AdvancedHtmlFileTests, CssListHtm) {
    bool result = test_html_file_roundtrip("./test/html/css-list.htm", "css-list");
    EXPECT_TRUE(result) << "CSS list HTM file should succeed";
}

// ==== COMPLEX HTML FILES (Multiple features, real-world pages) ====
class ComplexHtmlFileTests : public HtmlRoundtripTest {};

TEST_F(ComplexHtmlFileTests, Sample5) {
    bool result = test_html_file_roundtrip("./test/html/sample5.html", "sample5");
    EXPECT_TRUE(result) << "Sample5 AI CodeX landing page should succeed";
}

TEST_F(ComplexHtmlFileTests, SampleList) {
    bool result = test_html_file_roundtrip("./test/html/sample_list.htm", "sample_list");
    EXPECT_TRUE(result) << "Sample list HTM should succeed";
}

TEST_F(ComplexHtmlFileTests, SampleOverflow) {
    bool result = test_html_file_roundtrip("./test/html/sample_overflow.htm", "sample_overflow");
    EXPECT_TRUE(result) << "Sample overflow HTM should succeed";
}

TEST_F(ComplexHtmlFileTests, SampleSpanBoundary) {
    bool result = test_html_file_roundtrip("./test/html/sample_span_boundary.htm", "sample_span_boundary");
    EXPECT_TRUE(result) << "Sample span boundary HTM should succeed";
}

TEST_F(ComplexHtmlFileTests, PixeRatio) {
    bool result = test_html_file_roundtrip("./test/html/pixe_ratio.htm", "pixe_ratio");
    EXPECT_TRUE(result) << "Pixel ratio HTM should succeed";
}

TEST_F(ComplexHtmlFileTests, Facatology) {
    bool result = test_html_file_roundtrip("./test/html/Facatology.html", "Facatology");
    EXPECT_TRUE(result) << "Facatology HTML should succeed";
}

TEST_F(ComplexHtmlFileTests, Facatology0) {
    bool result = test_html_file_roundtrip("./test/html/Facatology0.html", "Facatology0");
    EXPECT_TRUE(result) << "Facatology0 HTML should succeed";
}

// Advanced HTML Features Tests
class AdvancedHtmlTests : public HtmlRoundtripTest {};

TEST_F(AdvancedHtmlTests, HtmlWithCommentsRoundtrip) {
    const char* html_with_comments = "<!DOCTYPE html>\n"
        "<html>\n"
        "<!-- This is a comment -->\n"
        "<head>\n"
        "<!-- Head comment -->\n"
        "<title>Comments Test</title>\n"
        "</head>\n"
        "<body>\n"
        "<!-- Body comment -->\n"
        "<p>Content with <!-- inline comment --> comments</p>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_source_roundtrip(html_with_comments, "HtmlWithCommentsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");
    ASSERT_NE(result.formatted, nullptr) << "Formatted HTML should not be null";
    ASSERT_GT(result.formatted->len, 0U) << "Formatted HTML should not be empty";

    printf("HTML with comments roundtrip completed\n");
}

TEST_F(AdvancedHtmlTests, HtmlWithEntitiesRoundtrip) {
    const char* html_with_entities = "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>Entities Test</title></head>\n"
        "<body>\n"
        "<p>Special characters: &lt; &gt; &amp; &quot; &apos;</p>\n"
        "<p>Symbols: &copy; &reg; &trade; &euro; &pound;</p>\n"
        "<p>Math: &times; &divide; &plusmn; &frac12;</p>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_source_roundtrip(html_with_entities, "HtmlWithEntitiesRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");
    ASSERT_NE(result.formatted, nullptr) << "Formatted HTML should not be null";
    ASSERT_GT(result.formatted->len, 0U) << "Formatted HTML should not be empty";

    printf("HTML with entities roundtrip completed\n");
}

TEST_F(AdvancedHtmlTests, HtmlWithFormElementsRoundtrip) {
    const char* html_with_forms = "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>Form Test</title></head>\n"
        "<body>\n"
        "<form action=\"/submit\" method=\"post\">\n"
        "<label for=\"name\">Name:</label>\n"
        "<input type=\"text\" id=\"name\" name=\"name\" required>\n"
        "<input type=\"email\" name=\"email\" placeholder=\"email@example.com\">\n"
        "<textarea name=\"message\" rows=\"4\" cols=\"50\"></textarea>\n"
        "<select name=\"option\">\n"
        "<option value=\"1\">Option 1</option>\n"
        "<option value=\"2\" selected>Option 2</option>\n"
        "</select>\n"
        "<input type=\"submit\" value=\"Submit\">\n"
        "</form>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_source_roundtrip(html_with_forms, "HtmlWithFormElementsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");
    ASSERT_NE(result.formatted, nullptr) << "Formatted HTML should not be null";
    ASSERT_GT(result.formatted->len, 0U) << "Formatted HTML should not be empty";

    printf("HTML with form elements roundtrip completed\n");
}

TEST_F(AdvancedHtmlTests, HtmlWithSelfClosingTagsRoundtrip) {
    const char* html_with_self_closing = "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<meta charset=\"UTF-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "<link rel=\"stylesheet\" href=\"styles.css\">\n"
        "<title>Self-Closing Tags</title>\n"
        "</head>\n"
        "<body>\n"
        "<img src=\"image.jpg\" alt=\"Test Image\">\n"
        "<br>\n"
        "<hr>\n"
        "<input type=\"text\" name=\"test\">\n"
        "</body>\n"
        "</html>";

    auto result = test_html_source_roundtrip(html_with_self_closing, "HtmlWithSelfClosingTagsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");
    ASSERT_NE(result.formatted, nullptr) << "Formatted HTML should not be null";
    ASSERT_GT(result.formatted->len, 0U) << "Formatted HTML should not be empty";

    printf("HTML with self-closing tags roundtrip completed\n");
}

// HTML5 Semantic Elements Tests
class Html5SemanticTests : public HtmlRoundtripTest {};

TEST_F(Html5SemanticTests, Html5SemanticElementsRoundtrip) {
    const char* html5_semantic = "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>HTML5 Semantic</title></head>\n"
        "<body>\n"
        "<header>\n"
        "<nav>\n"
        "<a href=\"/\">Home</a>\n"
        "<a href=\"/about\">About</a>\n"
        "</nav>\n"
        "</header>\n"
        "<main>\n"
        "<article>\n"
        "<h1>Article Title</h1>\n"
        "<section>\n"
        "<p>Article content</p>\n"
        "</section>\n"
        "</article>\n"
        "<aside>\n"
        "<p>Sidebar content</p>\n"
        "</aside>\n"
        "</main>\n"
        "<footer>\n"
        "<p>Copyright 2025</p>\n"
        "</footer>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_source_roundtrip(html5_semantic, "Html5SemanticElementsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");
    ASSERT_NE(result.formatted, nullptr) << "Formatted HTML should not be null";
    ASSERT_GT(result.formatted->len, 0U) << "Formatted HTML should not be empty";

    printf("HTML5 semantic elements roundtrip completed\n");
}
