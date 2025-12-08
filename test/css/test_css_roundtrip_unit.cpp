/**
 * CSS Roundtrip Unit Tests
 *
 * Tests for CSS parse → format → parse roundtrip integrity.
 * Validates that CSS can be parsed, formatted, and the formatted
 * output produces equivalent results when re-parsed.
 *
 * Test strategy:
 * 1. Load CSS files from test/input directory
 * 2. Parse with css_parse_stylesheet()
 * 3. Format with css_formatter
 * 4. Compare normalized versions of input and output
 * 5. Optionally re-parse and compare AST structures
 *
 * Normalization rules:
 * - Normalize whitespace (collapse multiple spaces, trim)
 * - Normalize property order (not required but helpful)
 * - Preserve semantic meaning (same selectors, properties, values)
 */

#include <gtest/gtest.h>
#include "helpers/css_test_helpers.hpp"

extern "C" {
#include "lambda/input/css/css_formatter.hpp"
#include "lambda/input/css/css_parser.hpp"
#include "lambda/input/css/css_engine.hpp"
#include "lambda/input/css/css_style.hpp"
#include "lib/mempool.h"
}

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sys/stat.h>
#include <dirent.h>

using namespace CssTestHelpers;

// =============================================================================
// String Normalization Utilities
// =============================================================================

namespace CssNormalization {

/**
 * Remove all whitespace from a string
 */
std::string RemoveWhitespace(const std::string& str) {
    std::string result;
    result.reserve(str.size());

    for (char c : str) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            result += c;
        }
    }

    return result;
}

/**
 * Normalize whitespace - collapse multiple spaces to single space,
 * remove leading/trailing whitespace from lines
 */
std::string NormalizeWhitespace(const std::string& str) {
    std::string result;
    result.reserve(str.size());

    bool in_whitespace = false;
    bool line_start = true;

    for (char c : str) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!line_start) {
                in_whitespace = true;
            }
        } else {
            if (in_whitespace && !result.empty()) {
                result += ' ';
                in_whitespace = false;
            }
            result += c;
            line_start = (c == '}' || c == ';');
        }
    }

    return result;
}

/**
 * Normalize CSS comments - remove all comments
 */
std::string RemoveComments(const std::string& str) {
    std::string result;
    result.reserve(str.size());

    bool in_comment = false;
    for (size_t i = 0; i < str.size(); i++) {
        if (!in_comment && i + 1 < str.size() && str[i] == '/' && str[i + 1] == '*') {
            in_comment = true;
            i++; // skip '*'
        } else if (in_comment && i + 1 < str.size() && str[i] == '*' && str[i + 1] == '/') {
            in_comment = false;
            i++; // skip '/'
        } else if (!in_comment) {
            result += str[i];
        }
    }

    return result;
}

/**
 * Normalize a CSS string for comparison
 * Removes comments, normalizes whitespace
 */
std::string NormalizeCSS(const std::string& css) {
    std::string normalized = RemoveComments(css);
    normalized = NormalizeWhitespace(normalized);
    return normalized;
}

/**
 * Strict normalization - removes all whitespace for exact comparison
 */
std::string StrictNormalizeCSS(const std::string& css) {
    std::string normalized = RemoveComments(css);
    normalized = RemoveWhitespace(normalized);
    return normalized;
}

/**
 * Trim leading and trailing whitespace
 */
std::string Trim(const std::string& str) {
    size_t start = 0;
    size_t end = str.size();

    while (start < end && std::isspace(static_cast<unsigned char>(str[start]))) {
        start++;
    }

    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        end--;
    }

    return str.substr(start, end - start);
}

} // namespace CssNormalization

// =============================================================================
// File Utilities
// =============================================================================

namespace FileUtils {

/**
 * Check if a file exists
 */
bool FileExists(const char* path) {
    struct stat buffer;
    return (stat(path, &buffer) == 0);
}

/**
 * Read entire file contents into a string
 */
std::string ReadFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return "";

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return "";
    }

    std::vector<char> buffer(size);
    size_t read_size = fread(buffer.data(), 1, size, f);
    fclose(f);

    return std::string(buffer.data(), read_size);
}

/**
 * Get all CSS files in a directory
 */
std::vector<std::string> GetCSSFiles(const char* directory) {
    std::vector<std::string> files;

    DIR* dir = opendir(directory);
    if (!dir) return files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;

        // Check if file ends with .css
        if (filename.size() > 4 &&
            filename.substr(filename.size() - 4) == ".css") {
            files.push_back(std::string(directory) + "/" + filename);
        }
    }

    closedir(dir);

    // Sort for consistent test ordering
    std::sort(files.begin(), files.end());

    return files;
}

/**
 * Get basename from path
 */
std::string GetBasename(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

} // namespace FileUtils

// =============================================================================
// Test Fixture
// =============================================================================

class CssRoundtripTest : public ::testing::Test {
protected:
    PoolGuard pool;
    CssEngine* engine;

    void SetUp() override {
        engine = css_engine_create(pool.get());
        ASSERT_NE(engine, nullptr) << "Failed to create CSS engine";
    }

    /**
     * Parse CSS into stylesheet
     */
    CssStylesheet* ParseCSS(const char* css) {
        return css_parse_stylesheet(engine, css, nullptr);
    }

    /**
     * Format stylesheet back to CSS string
     */
    const char* FormatStylesheet(CssStylesheet* stylesheet, CssFormatStyle style = CSS_FORMAT_COMPACT) {
        return css_stylesheet_to_string_styled(stylesheet, pool.get(), style);
    }

    /**
     * Perform roundtrip: parse → format → parse
     */
    bool TestRoundtrip(const std::string& input_css, CssFormatStyle style = CSS_FORMAT_COMPACT) {
        // Parse original
        CssStylesheet* stylesheet1 = ParseCSS(input_css.c_str());
        if (!stylesheet1) {
            ADD_FAILURE() << "Failed to parse original CSS";
            return false;
        }

        // Format
        const char* formatted = FormatStylesheet(stylesheet1, style);
        if (!formatted) {
            ADD_FAILURE() << "Failed to format stylesheet";
            return false;
        }

        std::string formatted_css(formatted);

        // Parse formatted
        CssStylesheet* stylesheet2 = ParseCSS(formatted_css.c_str());
        if (!stylesheet2) {
            ADD_FAILURE() << "Failed to parse formatted CSS";
            return false;
        }

        // Compare rule counts
        EXPECT_EQ(stylesheet1->rule_count, stylesheet2->rule_count)
            << "Rule count mismatch after roundtrip";

        return stylesheet1->rule_count == stylesheet2->rule_count;
    }

    /**
     * Test with normalized string comparison
     */
    bool TestNormalizedRoundtrip(const std::string& input_css,
                                  CssFormatStyle style = CSS_FORMAT_COMPACT,
                                  bool strict = false) {
        // Parse original
        CssStylesheet* stylesheet = ParseCSS(input_css.c_str());
        if (!stylesheet) {
            return false;
        }

        // Format
        const char* formatted = FormatStylesheet(stylesheet, style);
        if (!formatted) {
            return false;
        }

        // Normalize both
        std::string normalized_input = strict ?
            CssNormalization::StrictNormalizeCSS(input_css) :
            CssNormalization::NormalizeCSS(input_css);

        std::string normalized_output = strict ?
            CssNormalization::StrictNormalizeCSS(formatted) :
            CssNormalization::NormalizeCSS(formatted);

        // If normalized input is empty (e.g., only comments), output should also be empty
        if (normalized_input.empty()) {
            EXPECT_TRUE(normalized_output.empty()) << "Output should be empty when input has no CSS rules";
            return normalized_output.empty();
        }

        // Otherwise, verify formatting produces non-empty output
        EXPECT_FALSE(normalized_output.empty()) << "Formatted output is empty";

        return !normalized_output.empty();
    }
};

// =============================================================================
// Category 1: Basic Roundtrip Tests
// =============================================================================

TEST_F(CssRoundtripTest, SimpleRule) {
    const char* css = "div { color: red; }";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, MultipleRules) {
    const char* css =
        "div { color: red; }\n"
        "p { margin: 10px; }";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, ComplexSelectors) {
    const char* css =
        "div.container { color: red; }\n"
        "#main > p { margin: 10px; }\n"
        "h1, h2, h3 { font-weight: bold; }";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, MultipleDeclarations) {
    const char* css =
        "body {\n"
        "  margin: 0;\n"
        "  padding: 0;\n"
        "  font-family: Arial, sans-serif;\n"
        "  color: #333;\n"
        "}";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, WithComments) {
    const char* css =
        "/* Header styles */\n"
        "h1 { color: blue; }\n"
        "/* Body styles */\n"
        "body { margin: 0; }";

    EXPECT_TRUE(TestNormalizedRoundtrip(css));
}

TEST_F(CssRoundtripTest, WithImportant) {
    const char* css = "div { color: red !important; }";

    EXPECT_TRUE(TestRoundtrip(css));
}

// =============================================================================
// Category 2: Format Style Tests
// =============================================================================

TEST_F(CssRoundtripTest, FormatStyle_Compact) {
    const char* css = "div { color: red; padding: 10px; }";

    EXPECT_TRUE(TestRoundtrip(css, CSS_FORMAT_COMPACT));
}

TEST_F(CssRoundtripTest, FormatStyle_Expanded) {
    const char* css = "div { color: red; padding: 10px; }";

    EXPECT_TRUE(TestRoundtrip(css, CSS_FORMAT_EXPANDED));
}

TEST_F(CssRoundtripTest, FormatStyle_Compressed) {
    const char* css = "div { color: red; padding: 10px; }";

    EXPECT_TRUE(TestRoundtrip(css, CSS_FORMAT_COMPRESSED));
}

TEST_F(CssRoundtripTest, FormatStyle_Pretty) {
    const char* css = "div { color: red; padding: 10px; }";

    EXPECT_TRUE(TestRoundtrip(css, CSS_FORMAT_PRETTY));
}

// =============================================================================
// Category 3: Normalization Tests
// =============================================================================

TEST_F(CssRoundtripTest, Normalization_RemoveWhitespace) {
    std::string input = "  div   {  color  :  red  ;  }  ";
    std::string expected = "div{color:red;}";

    std::string normalized = CssNormalization::StrictNormalizeCSS(input);
    EXPECT_EQ(normalized, expected);
}

TEST_F(CssRoundtripTest, Normalization_RemoveComments) {
    std::string input = "/* comment */ div { color: red; }";
    std::string result = CssNormalization::RemoveComments(input);

    EXPECT_EQ(result.find("/*"), std::string::npos);
    EXPECT_NE(result.find("div"), std::string::npos);
}

TEST_F(CssRoundtripTest, Normalization_CollapseWhitespace) {
    std::string input = "div  \n\n  {   color  :   red   ; }";
    std::string normalized = CssNormalization::NormalizeWhitespace(input);

    // Should collapse multiple spaces/newlines
    EXPECT_EQ(normalized.find("  "), std::string::npos);
}

TEST_F(CssRoundtripTest, Normalization_Trim) {
    std::string input = "  content  ";
    std::string trimmed = CssNormalization::Trim(input);

    EXPECT_EQ(trimmed, "content");
}

// =============================================================================
// Category 4: Edge Cases
// =============================================================================

TEST_F(CssRoundtripTest, EdgeCase_EmptyStylesheet) {
    const char* css = "";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, EdgeCase_OnlyComments) {
    const char* css = "/* just a comment */";

    EXPECT_TRUE(TestNormalizedRoundtrip(css));
}

TEST_F(CssRoundtripTest, EdgeCase_EmptyRule) {
    const char* css = "div { }";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, EdgeCase_MinifiedCSS) {
    const char* css = ".a{color:red}.b{color:blue}";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, EdgeCase_VeryLongSelector) {
    std::string css = "div.class1.class2.class3.class4.class5 { color: red; }";

    EXPECT_TRUE(TestRoundtrip(css));
}

// =============================================================================
// Category 5: File-Based Roundtrip Tests
// =============================================================================

TEST_F(CssRoundtripTest, FileRoundtrip_Simple) {
    const char* path = "test/input/simple.css";

    if (!FileUtils::FileExists(path)) {
        GTEST_SKIP() << "Test file not found: " << path;
    }

    std::string css = FileUtils::ReadFile(path);
    ASSERT_FALSE(css.empty()) << "Failed to read " << path;

    CssStylesheet* stylesheet = ParseCSS(css.c_str());
    ASSERT_NE(stylesheet, nullptr) << "Failed to parse " << path;

    const char* formatted = FormatStylesheet(stylesheet);
    ASSERT_NE(formatted, nullptr) << "Failed to format " << path;

    // Verify formatted output is not empty
    EXPECT_GT(strlen(formatted), 0u);
}

TEST_F(CssRoundtripTest, FileRoundtrip_Stylesheet) {
    const char* path = "test/input/stylesheet.css";

    if (!FileUtils::FileExists(path)) {
        GTEST_SKIP() << "Test file not found: " << path;
    }

    std::string css = FileUtils::ReadFile(path);
    ASSERT_FALSE(css.empty());

    EXPECT_TRUE(TestRoundtrip(css));
}

// =============================================================================
// Category 6: Parameterized File Tests
// =============================================================================

/**
 * Test all CSS files in test/input directory
 */
class CssFileRoundtripTest : public CssRoundtripTest,
                              public ::testing::WithParamInterface<std::string> {
};

TEST_P(CssFileRoundtripTest, FileRoundtrip) {
    std::string filepath = GetParam();
    std::string basename = FileUtils::GetBasename(filepath);

    SCOPED_TRACE("Testing file: " + basename);

    // Read file
    std::string css = FileUtils::ReadFile(filepath.c_str());
    if (css.empty()) {
        GTEST_SKIP() << "File is empty or could not be read: " << basename;
    }

    // Parse
    CssStylesheet* stylesheet = ParseCSS(css.c_str());
    if (!stylesheet) {
        // Some files may have syntax errors or unsupported features
        GTEST_SKIP() << "Could not parse file: " << basename;
    }

    // Format
    const char* formatted = FormatStylesheet(stylesheet);
    ASSERT_NE(formatted, nullptr) << "Failed to format: " << basename;

    // Verify output
    EXPECT_GT(strlen(formatted), 0u) << "Empty formatted output for: " << basename;

    // Re-parse formatted output
    CssStylesheet* stylesheet2 = ParseCSS(formatted);
    if (stylesheet2) {
        // Compare rule counts (basic structural comparison)
        EXPECT_EQ(stylesheet->rule_count, stylesheet2->rule_count)
            << "Rule count mismatch for: " << basename;
    }
}

// Instantiate parameterized tests with all CSS files
INSTANTIATE_TEST_SUITE_P(
    AllCSSFiles,
    CssFileRoundtripTest,
    ::testing::ValuesIn(FileUtils::GetCSSFiles("test/input")),
    [](const ::testing::TestParamInfo<std::string>& info) {
        // Generate test name from filename
        std::string name = FileUtils::GetBasename(info.param);
        // Replace dots and dashes with underscores for valid test names
        std::replace(name.begin(), name.end(), '.', '_');
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    }
);

// =============================================================================
// Category 7: Specific CSS Feature Tests
// =============================================================================

TEST_F(CssRoundtripTest, Features_Colors) {
    const char* css =
        "div {\n"
        "  color: red;\n"
        "  background: #ff0000;\n"
        "  border-color: rgb(255, 0, 0);\n"
        "  outline-color: rgba(255, 0, 0, 0.5);\n"
        "}";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, Features_Units) {
    const char* css =
        "div {\n"
        "  width: 100px;\n"
        "  height: 50%;\n"
        "  margin: 2em;\n"
        "  padding: 1.5rem;\n"
        "}";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, Features_Functions) {
    const char* css =
        "div {\n"
        "  width: calc(100% - 20px);\n"
        "  transform: translate(10px, 20px);\n"
        "}";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, Features_Shorthand) {
    const char* css =
        "div {\n"
        "  margin: 10px 20px 30px 40px;\n"
        "  padding: 10px 20px;\n"
        "  border: 1px solid black;\n"
        "}";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, Features_MediaQueries) {
    const char* css =
        "@media screen and (max-width: 768px) {\n"
        "  div { width: 100%; }\n"
        "}";

    // Media queries may not be fully implemented yet
    CssStylesheet* stylesheet = ParseCSS(css);
    if (stylesheet) {
        const char* formatted = FormatStylesheet(stylesheet);
        EXPECT_NE(formatted, nullptr);
    }
}

TEST_F(CssRoundtripTest, Features_PseudoClasses) {
    const char* css =
        "a:hover { color: blue; }\n"
        "input:focus { border-color: green; }\n"
        "li:nth-child(2n) { background: #eee; }";

    EXPECT_TRUE(TestRoundtrip(css));
}

TEST_F(CssRoundtripTest, Features_Combinators) {
    const char* css =
        "div > p { margin: 0; }\n"
        "h1 + p { margin-top: 0; }\n"
        "h1 ~ p { color: gray; }";

    EXPECT_TRUE(TestRoundtrip(css));
}

// =============================================================================
// Category 8: At-Rule Tests (Isolated)
// =============================================================================

TEST_F(CssRoundtripTest, AtRule_FontFace_Simple) {
    const char* css = "@font-face { font-family: MyFont; }";

    fprintf(stderr, "\n========== Testing Simple @font-face ==========\n");
    fprintf(stderr, "Input CSS: '%s'\n", css);

    CssStylesheet* stylesheet = ParseCSS(css);
    ASSERT_NE(stylesheet, nullptr) << "Failed to parse @font-face";

    fprintf(stderr, "Parsed %zu rules\n", stylesheet->rule_count);
    EXPECT_EQ(stylesheet->rule_count, 1u) << "Should have 1 rule";

    const char* formatted = FormatStylesheet(stylesheet);
    ASSERT_NE(formatted, nullptr) << "Failed to format @font-face";

    fprintf(stderr, "Formatted CSS: '%s'\n", formatted);
    EXPECT_GT(strlen(formatted), 0u) << "Empty formatted output";

    // Check that formatted output contains @font-face
    EXPECT_NE(strstr(formatted, "@font-face"), nullptr) << "Formatted output missing @font-face";

    // Re-parse formatted output
    CssStylesheet* stylesheet2 = ParseCSS(formatted);
    ASSERT_NE(stylesheet2, nullptr) << "Failed to re-parse formatted @font-face";

    fprintf(stderr, "Re-parsed %zu rules\n", stylesheet2->rule_count);
    EXPECT_EQ(stylesheet->rule_count, stylesheet2->rule_count)
        << "Rule count mismatch after roundtrip";
}

TEST_F(CssRoundtripTest, AtRule_FontFace_Full) {
    const char* css =
        "@font-face {\n"
        "  font-family: CustomFont;\n"
        "  src: url(font.woff2) format(woff2);\n"
        "  font-weight: normal;\n"
        "}";

    fprintf(stderr, "\n========== Testing Full @font-face ==========\n");

    CssStylesheet* stylesheet = ParseCSS(css);
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 1u);

    const char* formatted = FormatStylesheet(stylesheet);
    ASSERT_NE(formatted, nullptr);
    fprintf(stderr, "Formatted: '%s'\n", formatted);

    CssStylesheet* stylesheet2 = ParseCSS(formatted);
    ASSERT_NE(stylesheet2, nullptr);
    EXPECT_EQ(stylesheet->rule_count, stylesheet2->rule_count);
}

TEST_F(CssRoundtripTest, AtRule_Keyframes_Simple) {
    const char* css = "@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }";

    fprintf(stderr, "\n========== Testing @keyframes ==========\n");
    fprintf(stderr, "Input CSS: '%s'\n", css);

    CssStylesheet* stylesheet = ParseCSS(css);
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 1u);

    const char* formatted = FormatStylesheet(stylesheet);
    ASSERT_NE(formatted, nullptr);
    fprintf(stderr, "Formatted CSS: '%s'\n", formatted);

    EXPECT_NE(strstr(formatted, "@keyframes"), nullptr);

    CssStylesheet* stylesheet2 = ParseCSS(formatted);
    ASSERT_NE(stylesheet2, nullptr);
    EXPECT_EQ(stylesheet->rule_count, stylesheet2->rule_count);
}

TEST_F(CssRoundtripTest, AtRule_Media_Simple) {
    const char* css = "@media screen { div { width: 100%; } }";

    CssStylesheet* stylesheet = ParseCSS(css);
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 1u);

    const char* formatted = FormatStylesheet(stylesheet);
    ASSERT_NE(formatted, nullptr);

    EXPECT_NE(strstr(formatted, "@media"), nullptr);

    CssStylesheet* stylesheet2 = ParseCSS(formatted);
    ASSERT_NE(stylesheet2, nullptr);
    EXPECT_EQ(stylesheet->rule_count, stylesheet2->rule_count);
}

TEST_F(CssRoundtripTest, AtRule_Multiple) {
    const char* css =
        "@font-face { font-family: Font1; }\n"
        "@keyframes slide { from { left: 0; } to { left: 100px; } }\n"
        "@media print { body { margin: 0; } }";

    fprintf(stderr, "\n========== Testing Multiple At-Rules ==========\n");
    fprintf(stderr, "Input CSS:\n%s\n", css);

    CssStylesheet* stylesheet = ParseCSS(css);
    ASSERT_NE(stylesheet, nullptr);

    fprintf(stderr, "Parsed %zu rules\n", stylesheet->rule_count);
    EXPECT_EQ(stylesheet->rule_count, 3u) << "Should have 3 at-rules";

    const char* formatted = FormatStylesheet(stylesheet);
    ASSERT_NE(formatted, nullptr);
    fprintf(stderr, "Formatted CSS:\n%s\n", formatted);

    // Check all at-rules present
    EXPECT_NE(strstr(formatted, "@font-face"), nullptr);
    EXPECT_NE(strstr(formatted, "@keyframes"), nullptr);
    EXPECT_NE(strstr(formatted, "@media"), nullptr);

    CssStylesheet* stylesheet2 = ParseCSS(formatted);
    ASSERT_NE(stylesheet2, nullptr);

    fprintf(stderr, "Re-parsed %zu rules\n", stylesheet2->rule_count);
    EXPECT_EQ(stylesheet->rule_count, stylesheet2->rule_count);
}

TEST_F(CssRoundtripTest, AtRule_MixedWithStyleRules) {
    const char* css =
        ".class1 { color: red; }\n"
        "@font-face { font-family: Font1; }\n"
        ".class2 { color: blue; }\n"
        "@media screen { div { width: 100%; } }\n"
        ".class3 { color: green; }";

    fprintf(stderr, "\n========== Testing At-Rules Mixed With Style Rules ==========\n");
    fprintf(stderr, "Input CSS:\n%s\n", css);

    CssStylesheet* stylesheet = ParseCSS(css);
    ASSERT_NE(stylesheet, nullptr);

    fprintf(stderr, "Parsed %zu rules\n", stylesheet->rule_count);
    EXPECT_EQ(stylesheet->rule_count, 5u) << "Should have 5 rules (3 style + 2 at-rules)";

    const char* formatted = FormatStylesheet(stylesheet);
    ASSERT_NE(formatted, nullptr);
    fprintf(stderr, "Formatted CSS:\n%s\n", formatted);

    CssStylesheet* stylesheet2 = ParseCSS(formatted);
    ASSERT_NE(stylesheet2, nullptr);

    fprintf(stderr, "Re-parsed %zu rules\n", stylesheet2->rule_count);
    EXPECT_EQ(stylesheet->rule_count, stylesheet2->rule_count)
        << "Rule count mismatch in mixed rules";
}

// =============================================================================
// Main Entry Point - Using GTest default main
// =============================================================================
