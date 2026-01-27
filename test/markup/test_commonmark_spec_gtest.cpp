/**
 * CommonMark Specification Test Runner
 *
 * This test file parses the official CommonMark spec.txt and runs each
 * example as a test case, comparing the Lambda markup parser output
 * (formatted as HTML) against the expected HTML from the spec.
 *
 * Spec format:
 * ```````````````````````````````` example
 * markdown input
 * .
 * expected html output
 * ````````````````````````````````
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
#include "commonmark_html_formatter.hpp"

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

// Structure to hold a single CommonMark test case
struct CommonMarkExample {
    int example_number;
    std::string section;
    std::string markdown;
    std::string expected_html;
    int line_number;
};

// Parse CommonMark spec.txt and extract all examples
static std::vector<CommonMarkExample> parse_commonmark_spec(const char* spec_path) {
    std::vector<CommonMarkExample> examples;

    std::ifstream file(spec_path);
    if (!file.is_open()) {
        fprintf(stderr, "ERROR: Cannot open spec file: %s\n", spec_path);
        return examples;
    }

    std::string line;
    std::string current_section = "Unknown";
    int line_number = 0;
    int example_number = 0;

    // Regex to match section headers (# Section Name)
    std::regex section_regex("^#{1,6}\\s+(.+)$");

    while (std::getline(file, line)) {
        line_number++;

        // Check for section header
        std::smatch section_match;
        if (std::regex_match(line, section_match, section_regex)) {
            current_section = section_match[1].str();
            continue;
        }

        // Check for example start (40 backticks followed by " example")
        if (line.find("````````````````````````````````") == 0 &&
            line.find("example") != std::string::npos) {

            example_number++;
            CommonMarkExample example;
            example.example_number = example_number;
            example.section = current_section;
            example.line_number = line_number;

            std::string markdown_content;
            std::string html_content;
            bool in_html = false;

            // Read example content
            while (std::getline(file, line)) {
                line_number++;

                // Check for example end
                if (line.find("````````````````````````````````") == 0) {
                    break;
                }

                // Check for separator between markdown and HTML
                if (line == ".") {
                    in_html = true;
                    continue;
                }

                // Replace → with actual tab character
                std::string processed_line = line;
                size_t pos;
                while ((pos = processed_line.find("→")) != std::string::npos) {
                    processed_line.replace(pos, strlen("→"), "\t");
                }

                if (in_html) {
                    if (!html_content.empty()) html_content += "\n";
                    html_content += processed_line;
                } else {
                    if (!markdown_content.empty()) markdown_content += "\n";
                    markdown_content += processed_line;
                }
            }

            example.markdown = markdown_content;
            example.expected_html = html_content;
            examples.push_back(example);
        }
    }

    return examples;
}

// Normalize HTML for comparison (trim whitespace, normalize newlines)
static std::string normalize_html(const std::string& html) {
    std::string result = html;

    // Trim leading/trailing whitespace
    size_t start = result.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = result.find_last_not_of(" \t\n\r");
    result = result.substr(start, end - start + 1);

    // Normalize multiple whitespace to single space (optional, can be refined)
    // For now, just trim and return

    return result;
}

// Test fixture for CommonMark spec tests
class CommonMarkSpecTest : public ::testing::Test {
protected:
    static std::vector<CommonMarkExample> examples;
    static bool examples_loaded;

    void SetUp() override {
        log_init(NULL);

        if (!examples_loaded) {
            // Try multiple paths to find the spec file
            const char* spec_paths[] = {
                "test/markup/commonmark/spec.txt",
                "../test/markup/commonmark/spec.txt",
                "markup/commonmark/spec.txt",
                NULL
            };

            for (int i = 0; spec_paths[i] != NULL; i++) {
                examples = parse_commonmark_spec(spec_paths[i]);
                if (!examples.empty()) {
                    printf("Loaded %zu CommonMark examples from %s\n",
                           examples.size(), spec_paths[i]);
                    break;
                }
            }
            examples_loaded = true;
        }
    }

    // Parse markdown and format as CommonMark-style HTML fragment
    std::string parse_and_format_html(const std::string& markdown) {
        String* type_str = create_test_string("markup");
        String* flavor_str = create_test_string("commonmark");
        Url* cwd = get_current_dir();
        Url* dummy_url = parse_url(cwd, "test.md");

        char* content_copy = strdup(markdown.c_str());
        Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

        if (!input) {
            free(content_copy);
            return "";
        }

        // Use CommonMark-specific HTML formatter
        std::string result = format_commonmark_html(input->root);

        free(content_copy);
        return result;
    }
};

// Static member initialization
std::vector<CommonMarkExample> CommonMarkSpecTest::examples;
bool CommonMarkSpecTest::examples_loaded = false;

// Test that we can load the spec file
TEST_F(CommonMarkSpecTest, LoadSpec) {
    ASSERT_FALSE(examples.empty()) << "Failed to load CommonMark spec examples";
    printf("Total examples loaded: %zu\n", examples.size());
}

// Test case counter and statistics
struct TestStats {
    int passed = 0;
    int failed = 0;
    int skipped = 0;
};

static TestStats global_stats;

// Parameterized test for individual examples
class CommonMarkExampleTest : public CommonMarkSpecTest,
                               public ::testing::WithParamInterface<int> {
};

// Run a specific example by index
TEST_P(CommonMarkExampleTest, Example) {
    int index = GetParam();

    if (index >= (int)examples.size()) {
        GTEST_SKIP() << "Example index out of range";
        return;
    }

    const CommonMarkExample& ex = examples[index];

    // Parse markdown and get HTML output
    std::string actual_html = parse_and_format_html(ex.markdown);
    std::string normalized_actual = normalize_html(actual_html);
    std::string normalized_expected = normalize_html(ex.expected_html);

    // Compare outputs using GTest assertion
    EXPECT_EQ(normalized_expected, normalized_actual)
        << "\n=== Example " << ex.example_number << " FAILED ===\n"
        << "Section: " << ex.section << "\n"
        << "Line: " << ex.line_number << "\n"
        << "--- Markdown input ---\n" << ex.markdown << "\n"
        << "--- Expected HTML ---\n" << ex.expected_html << "\n"
        << "--- Actual HTML ---\n" << actual_html << "\n"
        << "======================\n";

    if (normalized_actual == normalized_expected) {
        global_stats.passed++;
    } else {
        global_stats.failed++;
    }
}

// Generate test parameters for first N examples (for quick testing)
// Use INSTANTIATE_TEST_SUITE_P with a range
INSTANTIATE_TEST_SUITE_P(
    AllExamples,
    CommonMarkExampleTest,
    ::testing::Range(0, 655),  // Test all 655 examples
    [](const ::testing::TestParamInfo<int>& info) {
        return "Example_" + std::to_string(info.param + 1);
    }
);

// Test specific categories
TEST_F(CommonMarkSpecTest, CountExamplesBySection) {
    if (examples.empty()) {
        GTEST_SKIP() << "No examples loaded";
        return;
    }

    std::map<std::string, int> section_counts;
    for (const auto& ex : examples) {
        section_counts[ex.section]++;
    }

    printf("\nExamples by section:\n");
    for (const auto& pair : section_counts) {
        printf("  %s: %d\n", pair.first.c_str(), pair.second);
    }
}

// Test basic headers (examples typically in "ATX headings" section)
TEST_F(CommonMarkSpecTest, ATXHeadings) {
    int passed = 0, failed = 0;

    for (const auto& ex : examples) {
        if (ex.section.find("ATX heading") != std::string::npos ||
            ex.section.find("ATX Heading") != std::string::npos) {

            std::string actual = parse_and_format_html(ex.markdown);
            std::string normalized_actual = normalize_html(actual);
            std::string normalized_expected = normalize_html(ex.expected_html);

            if (normalized_actual == normalized_expected) {
                passed++;
            } else {
                failed++;
                printf("ATX Heading Example %d failed\n", ex.example_number);
                printf("  Input: %s\n", ex.markdown.c_str());
                printf("  Expected: %s\n", ex.expected_html.c_str());
                printf("  Actual: %s\n", actual.c_str());
            }
        }
    }

    printf("ATX Headings: %d passed, %d failed\n", passed, failed);
}

// Test paragraphs
TEST_F(CommonMarkSpecTest, Paragraphs) {
    int passed = 0, failed = 0;

    for (const auto& ex : examples) {
        if (ex.section.find("Paragraph") != std::string::npos) {
            std::string actual = parse_and_format_html(ex.markdown);
            std::string normalized_actual = normalize_html(actual);
            std::string normalized_expected = normalize_html(ex.expected_html);

            if (normalized_actual == normalized_expected) {
                passed++;
            } else {
                failed++;
            }
        }
    }

    printf("Paragraphs: %d passed, %d failed\n", passed, failed);
}

// Test code blocks
TEST_F(CommonMarkSpecTest, CodeBlocks) {
    int passed = 0, failed = 0;

    for (const auto& ex : examples) {
        if (ex.section.find("code") != std::string::npos ||
            ex.section.find("Code") != std::string::npos) {
            std::string actual = parse_and_format_html(ex.markdown);
            std::string normalized_actual = normalize_html(actual);
            std::string normalized_expected = normalize_html(ex.expected_html);

            if (normalized_actual == normalized_expected) {
                passed++;
            } else {
                failed++;
            }
        }
    }

    printf("Code blocks: %d passed, %d failed\n", passed, failed);
}

// Comprehensive statistics by section
TEST_F(CommonMarkSpecTest, ComprehensiveStats) {
    if (examples.empty()) {
        GTEST_SKIP() << "No examples loaded";
        return;
    }

    std::map<std::string, std::pair<int, int>> section_stats; // {passed, failed}
    int total_passed = 0;
    int total_failed = 0;

    for (const auto& ex : examples) {
        std::string actual = parse_and_format_html(ex.markdown);
        std::string normalized_actual = normalize_html(actual);
        std::string normalized_expected = normalize_html(ex.expected_html);

        bool passed = (normalized_actual == normalized_expected);

        if (passed) {
            section_stats[ex.section].first++;
            total_passed++;
        } else {
            section_stats[ex.section].second++;
            total_failed++;
        }
    }

    printf("\n");
    printf("========================================\n");
    printf("CommonMark Spec Compliance Report\n");
    printf("========================================\n\n");

    printf("%-40s %6s %6s %7s\n", "Section", "Pass", "Fail", "Rate");
    printf("%-40s %6s %6s %7s\n", "----------------------------------------", "------", "------", "-------");

    for (const auto& pair : section_stats) {
        int passed = pair.second.first;
        int failed = pair.second.second;
        int total = passed + failed;
        double rate = 100.0 * passed / total;

        printf("%-40s %6d %6d %6.1f%%\n",
               pair.first.substr(0, 40).c_str(),
               passed, failed, rate);
    }

    printf("%-40s %6s %6s %7s\n", "----------------------------------------", "------", "------", "-------");
    double overall_rate = 100.0 * total_passed / (total_passed + total_failed);
    printf("%-40s %6d %6d %6.1f%%\n", "TOTAL", total_passed, total_failed, overall_rate);
    printf("\n");
}

// Print final statistics
TEST_F(CommonMarkSpecTest, FinalStatistics) {
    printf("\n========================================\n");
    printf("CommonMark Spec Test Summary\n");
    printf("========================================\n");
    printf("Total examples: %zu\n", examples.size());
    printf("Passed: %d\n", global_stats.passed);
    printf("Failed: %d\n", global_stats.failed);
    printf("Skipped: %d\n", global_stats.skipped);
    if (!examples.empty()) {
        double pass_rate = 100.0 * global_stats.passed / examples.size();
        printf("Pass rate: %.1f%%\n", pass_rate);
    }
    printf("========================================\n");
}

// Main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
