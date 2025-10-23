#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <libgen.h>

// Helper function to check if a file exists
bool file_exists(const char* filepath) {
    struct stat buffer;
    return (stat(filepath, &buffer) == 0);
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

// Helper function to execute a shell command and capture output
int execute_command(const char* command, char** output) {
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return -1;
    }

    // Read command output
    char buffer[1024];
    size_t total_size = 0;
    char* result = NULL;

    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t len = strlen(buffer);
        char* new_result = (char*)realloc(result, total_size + len + 1);
        if (!new_result) {
            free(result);
            pclose(pipe);
            return -1;
        }
        result = new_result;
        memcpy(result + total_size, buffer, len);
        total_size += len;
    }

    if (result) {
        result[total_size] = '\0';
    }

    if (output) {
        *output = result;
    } else {
        free(result);
    }

    int status = pclose(pipe);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Helper function to compare two files
bool files_are_identical(const char* file1, const char* file2) {
    char* content1 = read_file_content(file1);
    char* content2 = read_file_content(file2);

    if (!content1 || !content2) {
        free(content1);
        free(content2);
        return false;
    }

    bool identical = (strcmp(content1, content2) == 0);

    free(content1);
    free(content2);

    return identical;
}

// Test fixture class for HTML roundtrip tests using CLI
class HtmlRoundtripTest : public ::testing::Test {
protected:
    const char* lambda_exe = "./lambda.exe";
    const char* temp_output = "/tmp/test_html_roundtrip_output.html";

    void SetUp() override {
        // Clean up any leftover temp files
        unlink(temp_output);
    }

    void TearDown() override {
        // Clean up temp files
        unlink(temp_output);
    }

    // Core roundtrip function: use CLI to convert HTML -> HTML
    // Returns: {success, error_message}
    struct RoundtripResult {
        bool success;
        const char* error_message;
    };

    RoundtripResult test_html_file_roundtrip_cli(const char* input_file, const char* test_name) {
        printf("\n=== Testing HTML roundtrip via CLI: %s ===\n", test_name);
        printf("Input file: %s\n", input_file);

        // Check if input file exists
        if (!file_exists(input_file)) {
            printf("ERROR: Input file does not exist: %s\n", input_file);
            return {false, "Input file does not exist"};
        }

        // Read original content for comparison
        char* original_content = read_file_content(input_file);
        if (!original_content) {
            printf("ERROR: Failed to read input file\n");
            return {false, "Failed to read input file"};
        }
        size_t original_len = strlen(original_content);
        printf("Original content length: %zu\n", original_len);

        // Build the CLI command
        char command[2048];
        snprintf(command, sizeof(command),
                "%s convert -f html -t html -o %s %s 2>&1",
                lambda_exe, temp_output, input_file);

        printf("Executing: %s\n", command);

        // Execute the command
        char* cmd_output = NULL;
        int exit_code = execute_command(command, &cmd_output);

        if (exit_code != 0) {
            printf("ERROR: Command failed with exit code %d\n", exit_code);
            if (cmd_output) {
                printf("Command output:\n%s\n", cmd_output);
            }
            free(cmd_output);
            free(original_content);
            return {false, "CLI command failed"};
        }

        if (cmd_output && strlen(cmd_output) > 0) {
            printf("Command output:\n%s\n", cmd_output);
        }
        free(cmd_output);

        // Check if output file was created
        if (!file_exists(temp_output)) {
            printf("ERROR: Output file was not created: %s\n", temp_output);
            free(original_content);
            return {false, "Output file not created"};
        }

        // Read the output file
        char* output_content = read_file_content(temp_output);
        if (!output_content) {
            printf("ERROR: Failed to read output file\n");
            free(original_content);
            return {false, "Failed to read output file"};
        }

        size_t output_len = strlen(output_content);
        printf("Output content length: %zu\n", output_len);

        // Compare original and output
        bool exact_match = (original_len == output_len &&
                           strcmp(original_content, output_content) == 0);

        printf("Roundtrip exact match: %s\n", exact_match ? "YES" : "NO");

        if (!exact_match) {
            printf("WARNING: Roundtrip mismatch!\n");
            printf("  Original length: %zu\n", original_len);
            printf("  Output length: %zu\n", output_len);
            printf("  Original (first 200 chars):\n%.200s\n", original_content);
            printf("  Output (first 200 chars):\n%.200s\n", output_content);

            // Show where the difference occurs
            size_t min_len = (original_len < output_len) ? original_len : output_len;
            for (size_t i = 0; i < min_len; i++) {
                if (original_content[i] != output_content[i]) {
                    printf("  First difference at position %zu:\n", i);
                    printf("    Original: '%c' (0x%02X)\n",
                           original_content[i], (unsigned char)original_content[i]);
                    printf("    Output: '%c' (0x%02X)\n",
                           output_content[i], (unsigned char)output_content[i]);
                    break;
                }
            }
        } else {
            printf("✅ Roundtrip successful!\n");
            printf("Output (first 200 chars):\n%.200s\n", output_content);
        }

        free(original_content);
        free(output_content);

        return {exact_match, exact_match ? nullptr : "Roundtrip content mismatch"};
    }

    // Test a simple HTML string by writing it to a temp file first
    RoundtripResult test_html_string_roundtrip_cli(const char* html_content, const char* test_name) {
        const char* temp_input = "/tmp/test_html_roundtrip_input.html";

        // Write content to temp file
        FILE* f = fopen(temp_input, "w");
        if (!f) {
            return {false, "Failed to create temp input file"};
        }
        fprintf(f, "%s", html_content);
        fclose(f);

        // Test the file
        auto result = test_html_file_roundtrip_cli(temp_input, test_name);

        // Clean up temp input file
        unlink(temp_input);

        return result;
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

    auto result = test_html_string_roundtrip_cli(simple_html, "SimpleHtmlRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

    printf("Simple HTML roundtrip completed successfully\n");
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

    auto result = test_html_string_roundtrip_cli(html_with_attrs, "HtmlWithAttributesRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

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

    auto result = test_html_string_roundtrip_cli(nested_html, "NestedElementsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

    printf("Nested HTML roundtrip completed successfully\n");
}

// HTML File Tests - test with actual files from ./test/html/
// Organized by complexity: Simple -> Intermediate -> Advanced -> Complex

// ==== SIMPLE HTML FILES (Basic structure, minimal CSS) ====
class SimpleHtmlFileTests : public HtmlRoundtripTest {};

TEST_F(SimpleHtmlFileTests, TestWhitespace) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_whitespace.html", "test_whitespace");
    EXPECT_TRUE(result.success) << "Whitespace test HTML should succeed";
}

TEST_F(SimpleHtmlFileTests, TestClearSimple) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_clear_simple.html", "test_clear_simple");
    EXPECT_TRUE(result.success) << "Simple clear test HTML should succeed";
}

TEST_F(SimpleHtmlFileTests, SimpleBoxTest) {
    auto result = test_html_file_roundtrip_cli("./test/html/simple_box_test.html", "simple_box_test");
    EXPECT_TRUE(result.success) << "Simple box test HTML should succeed";
}

TEST_F(SimpleHtmlFileTests, SimpleTableTest) {
    auto result = test_html_file_roundtrip_cli("./test/html/simple_table_test.html", "simple_table_test");
    EXPECT_TRUE(result.success) << "Simple table test HTML should succeed";
}

TEST_F(SimpleHtmlFileTests, TableSimple) {
    auto result = test_html_file_roundtrip_cli("./test/html/table_simple.html", "table_simple");
    EXPECT_TRUE(result.success) << "Simple table HTML should succeed";
}

TEST_F(SimpleHtmlFileTests, TableBasic) {
    auto result = test_html_file_roundtrip_cli("./test/html/table_basic.html", "table_basic");
    EXPECT_TRUE(result.success) << "Basic table HTML should succeed";
}

// ==== INTERMEDIATE HTML FILES (CSS styling, basic layouts) ====
class IntermediateHtmlFileTests : public HtmlRoundtripTest {};

TEST_F(IntermediateHtmlFileTests, Sample2) {
    auto result = test_html_file_roundtrip_cli("./test/html/sample2.html", "sample2");
    EXPECT_TRUE(result.success) << "Sample2 HTML with flexbox should succeed";
}

TEST_F(IntermediateHtmlFileTests, Sample3) {
    auto result = test_html_file_roundtrip_cli("./test/html/sample3.html", "sample3");
    EXPECT_TRUE(result.success) << "Sample3 HTML with navigation should succeed";
}

TEST_F(IntermediateHtmlFileTests, Sample4) {
    auto result = test_html_file_roundtrip_cli("./test/html/sample4.html", "sample4");
    EXPECT_TRUE(result.success) << "Sample4 landing page HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, SampleHtml) {
    auto result = test_html_file_roundtrip_cli("./test/html/sample.html", "sample");
    EXPECT_TRUE(result.success) << "Sample HTML file should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestFloatBasic) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_float_basic.html", "test_float_basic");
    EXPECT_TRUE(result.success) << "Basic float test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestClearLeft) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_clear_left.html", "test_clear_left");
    EXPECT_TRUE(result.success) << "Clear left test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestClearRight) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_clear_right.html", "test_clear_right");
    EXPECT_TRUE(result.success) << "Clear right test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestClearProperty) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_clear_property.html", "test_clear_property");
    EXPECT_TRUE(result.success) << "Clear property test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestLineHeight) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_line_height.html", "test_line_height");
    EXPECT_TRUE(result.success) << "Line height test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestLineBoxAdjustment) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_line_box_adjustment.html", "test_line_box_adjustment");
    EXPECT_TRUE(result.success) << "Line box adjustment test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestMarginCollapse) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_margin_collapse.html", "test_margin_collapse");
    EXPECT_TRUE(result.success) << "Margin collapse test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestOverflow) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_overflow.html", "test_overflow");
    EXPECT_TRUE(result.success) << "Overflow test HTML should succeed";
}

TEST_F(IntermediateHtmlFileTests, TestPercentage) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_percentage.html", "test_percentage");
    EXPECT_TRUE(result.success) << "Percentage test HTML should succeed";
}

// ==== ADVANCED HTML FILES (Complex layouts, positioning, grid/flex) ====
class AdvancedHtmlFileTests : public HtmlRoundtripTest {};

TEST_F(AdvancedHtmlFileTests, BoxHtml) {
    auto result = test_html_file_roundtrip_cli("./test/html/box.html", "box");
    EXPECT_TRUE(result.success) << "Box HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, FlexHtml) {
    auto result = test_html_file_roundtrip_cli("./test/html/flex.html", "flex");
    EXPECT_TRUE(result.success) << "Flex HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestPositioningSimple) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_positioning_simple.html", "test_positioning_simple");
    EXPECT_TRUE(result.success) << "Simple positioning test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestPositioningBasic) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_positioning_basic.html", "test_positioning_basic");
    EXPECT_TRUE(result.success) << "Basic positioning test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestCompletePositioning) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_complete_positioning.html", "test_complete_positioning");
    EXPECT_TRUE(result.success) << "Complete positioning test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, PositionHtml) {
    auto result = test_html_file_roundtrip_cli("./test/html/position.html", "position");
    EXPECT_TRUE(result.success) << "Position HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, DebugPosition) {
    auto result = test_html_file_roundtrip_cli("./test/html/debug_position.html", "debug_position");
    EXPECT_TRUE(result.success) << "Debug position HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestGridBasic) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_grid_basic.html", "test_grid_basic");
    EXPECT_TRUE(result.success) << "Basic grid test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestGridAreas) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_grid_areas.html", "test_grid_areas");
    EXPECT_TRUE(result.success) << "Grid areas test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, TestGridAdvanced) {
    auto result = test_html_file_roundtrip_cli("./test/html/test_grid_advanced.html", "test_grid_advanced");
    EXPECT_TRUE(result.success) << "Advanced grid test HTML should succeed";
}

TEST_F(AdvancedHtmlFileTests, GridHtml) {
    auto result = test_html_file_roundtrip_cli("./test/html/grid.html", "grid");
    EXPECT_TRUE(result.success) << "Grid HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, TableHtml) {
    auto result = test_html_file_roundtrip_cli("./test/html/table.html", "table");
    EXPECT_TRUE(result.success) << "Table HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, IndexHtml) {
    auto result = test_html_file_roundtrip_cli("./test/html/index.html", "index");
    EXPECT_TRUE(result.success) << "Index HTML file should succeed";
}

TEST_F(AdvancedHtmlFileTests, LayoutHtm) {
    auto result = test_html_file_roundtrip_cli("./test/html/layout.htm", "layout");
    EXPECT_TRUE(result.success) << "Layout HTM file should succeed";
}

TEST_F(AdvancedHtmlFileTests, CssListHtm) {
    auto result = test_html_file_roundtrip_cli("./test/html/css-list.htm", "css-list");
    EXPECT_TRUE(result.success) << "CSS list HTM file should succeed";
}

// ==== COMPLEX HTML FILES (Multiple features, real-world pages) ====
class ComplexHtmlFileTests : public HtmlRoundtripTest {};

TEST_F(ComplexHtmlFileTests, Sample5) {
    auto result = test_html_file_roundtrip_cli("./test/html/sample5.html", "sample5");
    EXPECT_TRUE(result.success) << "Sample5 AI CodeX landing page should succeed";
}

TEST_F(ComplexHtmlFileTests, SampleList) {
    auto result = test_html_file_roundtrip_cli("./test/html/sample_list.htm", "sample_list");
    EXPECT_TRUE(result.success) << "Sample list HTM should succeed";
}

TEST_F(ComplexHtmlFileTests, SampleOverflow) {
    auto result = test_html_file_roundtrip_cli("./test/html/sample_overflow.htm", "sample_overflow");
    EXPECT_TRUE(result.success) << "Sample overflow HTM should succeed";
}

TEST_F(ComplexHtmlFileTests, SampleSpanBoundary) {
    auto result = test_html_file_roundtrip_cli("./test/html/sample_span_boundary.htm", "sample_span_boundary");
    EXPECT_TRUE(result.success) << "Sample span boundary HTM should succeed";
}

TEST_F(ComplexHtmlFileTests, PixeRatio) {
    auto result = test_html_file_roundtrip_cli("./test/html/pixe_ratio.htm", "pixe_ratio");
    EXPECT_TRUE(result.success) << "Pixel ratio HTM should succeed";
}

TEST_F(ComplexHtmlFileTests, Facatology) {
    auto result = test_html_file_roundtrip_cli("./test/html/Facatology.html", "Facatology");
    EXPECT_TRUE(result.success) << "Facatology HTML should succeed";
}

TEST_F(ComplexHtmlFileTests, Facatology0) {
    auto result = test_html_file_roundtrip_cli("./test/html/Facatology0.html", "Facatology0");
    EXPECT_TRUE(result.success) << "Facatology0 HTML should succeed";
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

    auto result = test_html_string_roundtrip_cli(html_with_comments, "HtmlWithCommentsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

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

    auto result = test_html_string_roundtrip_cli(html_with_entities, "HtmlWithEntitiesRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

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

    auto result = test_html_string_roundtrip_cli(html_with_forms, "HtmlWithFormElementsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

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

    auto result = test_html_string_roundtrip_cli(html_with_self_closing, "HtmlWithSelfClosingTagsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

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

    auto result = test_html_string_roundtrip_cli(html5_semantic, "Html5SemanticElementsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

    printf("HTML5 semantic elements roundtrip completed\n");
}
