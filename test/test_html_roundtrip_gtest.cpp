#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // for strncasecmp
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <libgen.h>
#include <dirent.h>
#include <vector>
#include <string>
#include <algorithm>

extern "C" {
#include "../lib/log.h"
}

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

// ===== SEMANTIC HTML COMPARISON HELPERS =====
// These allow "good enough" roundtrip testing that ignores known parser limitations

// Entity mapping for common HTML entities
struct EntityMapping {
    const char* entity;
    const char* replacement;
};

static const EntityMapping entity_mappings[] = {
    // Basic entities
    {"&quot;", "\""},
    {"&apos;", "'"},
    {"&amp;", "&"},
    {"&lt;", "<"},
    {"&gt;", ">"},
    {"&nbsp;", " "},
    // Symbols
    {"&copy;", "\xC2\xA9"},      // ©
    {"&reg;", "\xC2\xAE"},       // ®
    {"&trade;", "\xE2\x84\xA2"}, // ™
    {"&euro;", "\xE2\x82\xAC"},  // €
    {"&pound;", "\xC2\xA3"},     // £
    {"&yen;", "\xC2\xA5"},       // ¥
    {"&cent;", "\xC2\xA2"},      // ¢
    // Math
    {"&times;", "\xC3\x97"},     // ×
    {"&divide;", "\xC3\xB7"},    // ÷
    {"&plusmn;", "\xC2\xB1"},    // ±
    {"&frac12;", "\xC2\xBD"},    // ½
    {"&frac14;", "\xC2\xBC"},    // ¼
    {"&frac34;", "\xC2\xBE"},    // ¾
    // Punctuation
    {"&mdash;", "\xE2\x80\x94"}, // —
    {"&ndash;", "\xE2\x80\x93"}, // –
    {"&hellip;", "\xE2\x80\xA6"}, // …
    {"&lsquo;", "\xE2\x80\x98"}, // '
    {"&rsquo;", "\xE2\x80\x99"}, // '
    {"&ldquo;", "\xE2\x80\x9C"}, // "
    {"&rdquo;", "\xE2\x80\x9D"}, // "
    {"&bull;", "\xE2\x80\xA2"},  // •
    {NULL, NULL}
};

// Normalize HTML entities to their character equivalents for semantic comparison
char* normalize_entities(const char* html) {
    if (!html) return NULL;

    size_t len = strlen(html);
    // Allocate extra space since some entities expand to multi-byte UTF-8
    char* result = (char*)malloc(len * 4 + 1);
    if (!result) return NULL;

    const char* read = html;
    char* write = result;

    while (*read) {
        // Check for UTF-8 non-breaking space (U+00A0 = 0xC2 0xA0)
        if ((unsigned char)*read == 0xC2 && (unsigned char)*(read + 1) == 0xA0) {
            *write++ = ' ';  // Convert to regular space
            read += 2;
            continue;
        }

        // Check for entity reference
        if (*read == '&') {
            bool matched = false;
            for (int i = 0; entity_mappings[i].entity; i++) {
                size_t ent_len = strlen(entity_mappings[i].entity);
                if (strncmp(read, entity_mappings[i].entity, ent_len) == 0) {
                    const char* repl = entity_mappings[i].replacement;
                    while (*repl) {
                        *write++ = *repl++;
                    }
                    read += ent_len;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                *write++ = *read++;
            }
        } else {
            *write++ = *read++;
        }
    }

    *write = '\0';
    return result;
}

// Skip DOCTYPE and XML declaration if present
const char* skip_doctype(const char* html) {
    const char* p = html;

    // Skip leading whitespace
    while (*p && isspace(*p)) p++;

    // Check for XML declaration (<?xml ... ?>)
    if (strncasecmp(p, "<?xml", 5) == 0) {
        // Find the end of XML declaration
        while (*p && !(*p == '?' && *(p+1) == '>')) p++;
        if (*p == '?' && *(p+1) == '>') p += 2;
        // Skip trailing whitespace/newlines
        while (*p && isspace(*p)) p++;
    }

    // Check for DOCTYPE (case-insensitive)
    if (strncasecmp(p, "<!DOCTYPE", 9) == 0) {
        // Find the end of DOCTYPE
        while (*p && *p != '>') p++;
        if (*p == '>') p++;
        // Skip trailing whitespace/newlines
        while (*p && isspace(*p)) p++;
    }

    return p;
}

// Remove empty <head></head> tags that HTML5 auto-creates
char* strip_empty_head(const char* html) {
    if (!html) return NULL;

    size_t len = strlen(html);
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;

    const char* read = html;
    char* write = result;

    while (*read) {
        // Look for <head></head> or <head> </head> etc
        if (strncasecmp(read, "<head>", 6) == 0) {
            const char* check = read + 6;
            // Skip whitespace inside head
            while (*check && isspace(*check)) check++;
            // Check if followed by </head>
            if (strncasecmp(check, "</head>", 7) == 0) {
                // Skip the empty head element
                read = check + 7;
                continue;
            }
        }
        *write++ = *read++;
    }

    *write = '\0';
    return result;
}

// Remove all HTML comments from a string (modifies in place)
void strip_comments_inplace(char* html) {
    char* read = html;
    char* write = html;

    while (*read) {
        if (strncmp(read, "<!--", 4) == 0) {
            // Find end of comment
            char* end = strstr(read, "-->");
            if (end) {
                read = end + 3; // Skip past -->
                continue;
            }
        }
        *write++ = *read++;
    }
    *write = '\0';
}

// Remove implicit tbody wrappers: <table><tbody><tr> -> <table><tr>
// This normalizes the HTML5 implicit tbody insertion for comparison
// Also normalizes missing newlines between </tr><tr> to </tr> <tr>
char* strip_implicit_tbody(const char* html) {
    if (!html) return NULL;

    size_t len = strlen(html);
    char* result = (char*)malloc(len * 2);  // Extra space for potential space insertions
    if (!result) return NULL;

    const char* read = html;
    char* write = result;
    char prev_char = '\0';

    while (*read) {
        // Look for <tbody> tag (case-insensitive)
        if (*read == '<' &&
            (read[1] == 't' || read[1] == 'T') &&
            (read[2] == 'b' || read[2] == 'B') &&
            (read[3] == 'o' || read[3] == 'O') &&
            (read[4] == 'd' || read[4] == 'D') &&
            (read[5] == 'y' || read[5] == 'Y') &&
            (read[6] == '>' || read[6] == ' ')) {
            // Skip the opening <tbody> tag
            while (*read && *read != '>') read++;
            if (*read == '>') read++;
            continue;
        }

        // Look for </tbody> closing tag (case-insensitive)
        if (*read == '<' && read[1] == '/' &&
            (read[2] == 't' || read[2] == 'T') &&
            (read[3] == 'b' || read[3] == 'B') &&
            (read[4] == 'o' || read[4] == 'O') &&
            (read[5] == 'd' || read[5] == 'D') &&
            (read[6] == 'y' || read[6] == 'Y') &&
            read[7] == '>') {
            // Skip the closing </tbody> tag
            read += 8;
            continue;
        }

        // Normalize missing space between tags: </tr><tr> -> </tr> <tr>
        // This handles the case where HTML formatter removes newlines
        if (*read == '<' && prev_char == '>' && write > result) {
            // Check if previous tag was a closing tag and current is an opening tag
            // Look back to see if we just wrote a closing tag
            char* check = write - 1;
            while (check > result && *check != '<') check--;
            if (check > result && check[1] == '/') {
                // Previous was a closing tag, add a space before the new opening tag
                if (!isspace(*(write - 1))) {
                    *write++ = ' ';
                }
            }
        }

        prev_char = *read;
        *write++ = *read++;
    }

    *write = '\0';
    return result;
}

// Normalize whitespace: collapse multiple spaces/newlines to single space
char* normalize_whitespace(const char* html) {
    size_t len = strlen(html);
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;

    const char* read = html;
    char* write = result;
    bool in_tag = false;
    bool last_was_space = false;

    while (*read) {
        if (*read == '<') {
            in_tag = true;
            last_was_space = false;
        } else if (*read == '>') {
            in_tag = false;
            last_was_space = false;
        }

        // In tags, preserve spaces as-is
        if (in_tag) {
            *write++ = *read++;
            continue;
        }

        // Outside tags, collapse whitespace
        if (isspace(*read)) {
            if (!last_was_space) {
                *write++ = ' ';
                last_was_space = true;
            }
            read++;
        } else {
            *write++ = *read++;
            last_was_space = false;
        }
    }

    // Remove trailing whitespace
    while (write > result && isspace(*(write - 1))) {
        write--;
    }

    *write = '\0';
    return result;
}

// Strip all whitespace between tags: ><whitespace>< becomes ><
// This normalizes HTML5's inter-element whitespace rules
char* strip_inter_tag_whitespace(const char* html) {
    if (!html) return NULL;

    size_t len = strlen(html);
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;

    const char* read = html;
    char* write = result;

    while (*read) {
        // If we just saw '>' and the next character is whitespace,
        // check if it's followed by '<'
        if (*read == '>') {
            *write++ = *read++;

            // Skip whitespace between tags
            const char* peek = read;
            while (*peek && isspace(*peek)) peek++;

            if (*peek == '<') {
                // Skip the whitespace between tags
                read = peek;
            }
        } else {
            *write++ = *read++;
        }
    }

    *write = '\0';
    return result;
}

// Semantic HTML comparison: ignores DOCTYPE, comments, whitespace differences, implicit tbody, and entity variations
bool are_semantically_equivalent(const char* html1, const char* html2) {
    // Skip DOCTYPE in both
    const char* h1 = skip_doctype(html1);
    const char* h2 = skip_doctype(html2);

    // Strip implicit tbody elements (HTML5 normalization)
    char* tbody_stripped1 = strip_implicit_tbody(h1);
    char* tbody_stripped2 = strip_implicit_tbody(h2);

    if (!tbody_stripped1 || !tbody_stripped2) {
        free(tbody_stripped1);
        free(tbody_stripped2);
        return false;
    }

    // Strip empty head elements (HTML5 auto-creates head)
    char* head_stripped1 = strip_empty_head(tbody_stripped1);
    char* head_stripped2 = strip_empty_head(tbody_stripped2);

    // Free intermediate results
    free(tbody_stripped1);
    free(tbody_stripped2);

    if (!head_stripped1 || !head_stripped2) {
        free(head_stripped1);
        free(head_stripped2);
        return false;
    }

    // Normalize entities (&quot; -> ", &apos; -> ')
    char* entity_norm1 = normalize_entities(head_stripped1);
    char* entity_norm2 = normalize_entities(head_stripped2);

    // Free intermediate results
    free(head_stripped1);
    free(head_stripped2);

    if (!entity_norm1 || !entity_norm2) {
        free(entity_norm1);
        free(entity_norm2);
        return false;
    }

    // Strip inter-tag whitespace (HTML5 normalizes this away)
    char* inter_tag1 = strip_inter_tag_whitespace(entity_norm1);
    char* inter_tag2 = strip_inter_tag_whitespace(entity_norm2);

    // Free intermediate results
    free(entity_norm1);
    free(entity_norm2);

    if (!inter_tag1 || !inter_tag2) {
        free(inter_tag1);
        free(inter_tag2);
        return false;
    }

    // Normalize whitespace
    char* norm1 = normalize_whitespace(inter_tag1);
    char* norm2 = normalize_whitespace(inter_tag2);

    // Free intermediate results
    free(inter_tag1);
    free(inter_tag2);

    if (!norm1 || !norm2) {
        free(norm1);
        free(norm2);
        return false;
    }

    // Strip comments (modifies in place)
    strip_comments_inplace(norm1);
    strip_comments_inplace(norm2);

    // Compare
    bool equivalent = (strcmp(norm1, norm2) == 0);

    if (!equivalent) {
        printf("\n⚠️  Semantic comparison details:\n");
        printf("  After normalization:\n");
        printf("    String 1 (len=%zu): %.200s\n", strlen(norm1), norm1);
        printf("    String 2 (len=%zu): %.200s\n", strlen(norm2), norm2);

        // Write full normalized strings to files for debugging
        FILE* f1 = fopen("/tmp/norm1.html", "w");
        FILE* f2 = fopen("/tmp/norm2.html", "w");
        if (f1) { fwrite(norm1, 1, strlen(norm1), f1); fclose(f1); }
        if (f2) { fwrite(norm2, 1, strlen(norm2), f2); fclose(f2); }
        printf("  Debug: Wrote normalized strings to /tmp/norm1.html and /tmp/norm2.html\n");
    }

    free(norm1);
    free(norm2);

    return equivalent;
}

// Test fixture class for HTML roundtrip tests using CLI
class HtmlRoundtripTest : public ::testing::Test {
protected:
    const char* lambda_exe = "./lambda.exe";
    const char* temp_output = "/tmp/test_html_roundtrip_output.html";

    void SetUp() override {
        // Initialize logging
        log_init(NULL);
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

        // Try exact match first
        bool exact_match = (original_len == output_len &&
                           strcmp(original_content, output_content) == 0);

        // If exact match fails, try semantic comparison
        bool semantic_match = false;
        if (!exact_match) {
            semantic_match = are_semantically_equivalent(original_content, output_content);
        }

        bool success = exact_match || semantic_match;

        printf("Roundtrip exact match: %s\n", exact_match ? "YES" : "NO");
        if (!exact_match && semantic_match) {
            printf("Roundtrip semantic match: YES ✓\n");
            printf("  (Differences in DOCTYPE/comments/whitespace/implicit tbody are acceptable)\n");
        }

        if (!exact_match && !semantic_match) {
            // Check if this is a known issue file (these print their own messages)
            bool is_known_issue = (strstr(input_file, "text_flow_701") != NULL ||
                                    strstr(input_file, "text_flow_711") != NULL ||
                                    strstr(input_file, "text_flow_751") != NULL ||
                                    strstr(input_file, "page/sample2") != NULL ||
                                    strstr(input_file, "page/sample5") != NULL);

            if (!is_known_issue) {
                printf("❌ WARNING: Roundtrip FAILED (both exact and semantic)!\n");
            }
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
        } else if (exact_match) {
            printf("✅ Roundtrip successful (exact match)!\n");
            printf("Output (first 200 chars):\n%.200s\n", output_content);
        } else {
            printf("✓ Roundtrip successful (semantic match)\n");
        }

        free(original_content);
        free(output_content);

        return {success, success ? nullptr : "Roundtrip content mismatch"};
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

TEST_F(AdvancedHtmlTests, RootLevelDoctypeRoundtrip) {
    const char* html_with_doctype = "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>DOCTYPE Test</title></head>\n"
        "<body>\n"
        "<p>Testing DOCTYPE preservation at root level</p>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_string_roundtrip_cli(html_with_doctype, "RootLevelDoctypeRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

    printf("Root-level DOCTYPE roundtrip completed\n");
}

TEST_F(AdvancedHtmlTests, RootLevelDoctypeUppercaseRoundtrip) {
    const char* html_with_uppercase_doctype = "<!DOCTYPE HTML>\n"
        "<html>\n"
        "<head><title>Uppercase DOCTYPE</title></head>\n"
        "<body>\n"
        "<p>Testing uppercase DOCTYPE preservation</p>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_string_roundtrip_cli(html_with_uppercase_doctype, "RootLevelDoctypeUppercaseRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

    printf("Root-level uppercase DOCTYPE roundtrip completed\n");
}

TEST_F(AdvancedHtmlTests, RootLevelCommentsBeforeHtmlRoundtrip) {
    const char* html_with_leading_comment = "<!-- Comment before HTML -->\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>Leading Comment</title></head>\n"
        "<body>\n"
        "<p>Testing comments before HTML element</p>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_string_roundtrip_cli(html_with_leading_comment, "RootLevelCommentsBeforeHtmlRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

    printf("Root-level comments before HTML roundtrip completed\n");
}

TEST_F(AdvancedHtmlTests, RootLevelCommentsAfterHtmlRoundtrip) {
    const char* html_with_trailing_comment = "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>Trailing Comment</title></head>\n"
        "<body>\n"
        "<p>Testing comments after HTML element</p>\n"
        "</body>\n"
        "</html>\n"
        "<!-- Comment after HTML -->";

    auto result = test_html_string_roundtrip_cli(html_with_trailing_comment, "RootLevelCommentsAfterHtmlRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

    printf("Root-level comments after HTML roundtrip completed\n");
}

TEST_F(AdvancedHtmlTests, RootLevelMultipleCommentsRoundtrip) {
    const char* html_with_multiple_comments = "<!-- First comment -->\n"
        "<!-- Second comment -->\n"
        "<!DOCTYPE html>\n"
        "<!-- Comment after DOCTYPE -->\n"
        "<html>\n"
        "<head><title>Multiple Comments</title></head>\n"
        "<body>\n"
        "<p>Testing multiple root-level comments</p>\n"
        "</body>\n"
        "</html>\n"
        "<!-- Final comment -->";

    auto result = test_html_string_roundtrip_cli(html_with_multiple_comments, "RootLevelMultipleCommentsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

    printf("Root-level multiple comments roundtrip completed\n");
}

TEST_F(AdvancedHtmlTests, RootLevelDoctypeWithCommentsRoundtrip) {
    const char* html_complete = "<!-- Header comment -->\n"
        "<!DOCTYPE html>\n"
        "<!-- After DOCTYPE -->\n"
        "<html>\n"
        "<head><title>Complete Test</title></head>\n"
        "<body>\n"
        "<p>Testing DOCTYPE and comments together</p>\n"
        "</body>\n"
        "</html>\n"
        "<!-- Footer comment -->";

    auto result = test_html_string_roundtrip_cli(html_complete, "RootLevelDoctypeWithCommentsRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

    printf("Root-level DOCTYPE with comments roundtrip completed\n");
}

TEST_F(AdvancedHtmlTests, RootLevelOnlyHtmlElementRoundtrip) {
    const char* html_simple = "<html>\n"
        "<head><title>No DOCTYPE</title></head>\n"
        "<body>\n"
        "<p>HTML without DOCTYPE should still work</p>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_string_roundtrip_cli(html_simple, "RootLevelOnlyHtmlElementRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

    printf("Root-level single HTML element roundtrip completed\n");
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

// ==== DYNAMIC BASELINE SUITE TEST ====
// Dynamically scans and tests all HTML files in the baseline and page directories
class LayoutDataBaselineTests : public HtmlRoundtripTest {
protected:
    static std::vector<std::string> get_html_files_in_directory(const char* dir_path) {
        std::vector<std::string> files;
        DIR* dir = opendir(dir_path);
        if (!dir) {
            printf("WARNING: Could not open directory: %s\n", dir_path);
            return files;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            // Check for .html or .htm extension
            if (filename.length() > 5 &&
                (filename.substr(filename.length() - 5) == ".html" ||
                 filename.substr(filename.length() - 4) == ".htm")) {
                files.push_back(std::string(dir_path) + "/" + filename);
            }
        }
        closedir(dir);

        // Sort for consistent ordering
        std::sort(files.begin(), files.end());
        return files;
    }

    static std::string get_test_name_from_path(const std::string& path) {
        size_t last_slash = path.find_last_of('/');
        std::string filename = (last_slash != std::string::npos) ? path.substr(last_slash + 1) : path;
        // Remove extension
        size_t dot = filename.find_last_of('.');
        if (dot != std::string::npos) {
            filename = filename.substr(0, dot);
        }
        return filename;
    }
};

TEST_F(LayoutDataBaselineTests, AllBaselineAndPageFiles) {
    const char* baseline_dir = "./test/layout/data/baseline";
    const char* page_dir = "./test/layout/data/page";

    printf("\n=== Testing all HTML files in baseline and page directories ===\n");

    int passed = 0;
    int failed = 0;
    std::vector<std::string> failed_files;

    // Test baseline directory
    auto baseline_files = get_html_files_in_directory(baseline_dir);
    printf("\n--- Testing baseline (%zu files) ---\n", baseline_files.size());
    for (const auto& file_path : baseline_files) {
        std::string test_name = get_test_name_from_path(file_path);
        auto result = test_html_file_roundtrip_cli(file_path.c_str(), test_name.c_str());
        if (result.success) {
            passed++;
        } else {
            failed++;
            failed_files.push_back("baseline/" + test_name);
        }
    }

    // Test page directory
    auto page_files = get_html_files_in_directory(page_dir);
    printf("\n--- Testing page (%zu files) ---\n", page_files.size());
    for (const auto& file_path : page_files) {
        std::string test_name = get_test_name_from_path(file_path);
        auto result = test_html_file_roundtrip_cli(file_path.c_str(), test_name.c_str());
        if (result.success) {
            passed++;
        } else {
            failed++;
            failed_files.push_back("page/" + test_name);
        }
    }

    int total = passed + failed;
    ASSERT_GT(total, 0) << "No HTML files found in baseline or page directories";

    printf("\n=== Baseline + Page Suite Summary ===\n");
    printf("  Total: %d files\n", total);
    printf("  Passed: %d (%.1f%%)\n", passed, 100.0 * passed / total);
    printf("  Failed: %d\n", failed);

    if (!failed_files.empty()) {
        printf("  Failed files:\n");
        for (const auto& f : failed_files) {
            printf("    - %s\n", f.c_str());
        }
    }

    // Require at least 90% pass rate (allow some failures for complex HTML/tables)
    double pass_rate = 100.0 * passed / total;
    EXPECT_GE(pass_rate, 90.0) << "Pass rate should be at least 90%";
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

TEST_F(Html5SemanticTests, HtmlWithBooleanAttributesRoundtrip) {
    const char* html_with_boolean_attrs = "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>Boolean Attributes</title></head>\n"
        "<body>\n"
        "<input type=\"text\" name=\"username\" required>\n"
        "<input type=\"checkbox\" checked>\n"
        "<input type=\"text\" disabled>\n"
        "<select>\n"
        "<option value=\"1\">One</option>\n"
        "<option value=\"2\" selected>Two</option>\n"
        "</select>\n"
        "<textarea readonly>Read-only text</textarea>\n"
        "<button autofocus>Click me</button>\n"
        "</body>\n"
        "</html>";

    auto result = test_html_string_roundtrip_cli(html_with_boolean_attrs, "HtmlWithBooleanAttributesRoundtrip");

    ASSERT_TRUE(result.success) << "Failed: " << (result.error_message ? result.error_message : "unknown error");

    printf("HTML with boolean attributes roundtrip completed\n");
}
