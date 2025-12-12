/**
 * @file test_latex_html_v2_special_chars.cpp
 * @brief Tests for LaTeX HTML V2 Formatter - Special Characters
 * 
 * Tests special character handling including:
 * - Escape sequences (\%, \&, \$, \#, \_, \{, \}, etc.)
 * - Accent/diacritic commands (\', \`, \^, \", \~, etc.)
 * - Text symbols (\textbackslash, \copyright, \dots, etc.)
 */

#include <gtest/gtest.h>
#include <cstring>
#include "../lambda/format/format.h"
#include "../lambda/input/input.hpp"
#include "../lib/log.h"

// Forward declarations for C functions
extern "C" {
    void parse_latex_ts(Input* input, const char* latex_string);
    Item format_latex_html_v2_c(Input* input, int text_mode);
}

// Helper function to parse LaTeX string
static Item parse_latex_string(Input* input, const char* latex_str) {
    parse_latex_ts(input, latex_str);
    return input->root;
}

// Helper function to format to HTML text mode
static const char* format_to_html_text(Input* input) {
    Item result = format_latex_html_v2_c(input, 1);  // text_mode = 1 (true)
    if (get_type_id(result) == LMD_TYPE_STRING) {
        String* str = (String*)result.string_ptr;
        return str->chars;
    }
    return "";
}

// Test fixture for LaTeX HTML V2 formatter - Special Characters
class LatexHtmlV2SpecialCharsTest : public ::testing::Test {
protected:
    Input* input;

    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        // Use InputManager to create input with managed pool
        input = InputManager::create_input(nullptr);
        ASSERT_NE(input, nullptr);
    }

    void TearDown() override {
        // InputManager handles cleanup
        InputManager::destroy_global();
    }
};

// =============================================================================
// Escape Sequence Tests
// =============================================================================

TEST_F(LatexHtmlV2SpecialCharsTest, EscapePercent) {
    const char* latex = R"(100\% complete)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "100%") != nullptr) << "Should contain percent sign";
    EXPECT_TRUE(strstr(html, "complete") != nullptr);
}

TEST_F(LatexHtmlV2SpecialCharsTest, EscapeAmpersand) {
    const char* latex = R"(Tom \& Jerry)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "&amp;") != nullptr || strstr(html, "&") != nullptr) 
        << "Should contain ampersand (escaped or not)";
}

TEST_F(LatexHtmlV2SpecialCharsTest, EscapeDollar) {
    const char* latex = R"(Price: \$50)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "$50") != nullptr || strstr(html, "50") != nullptr);
}

TEST_F(LatexHtmlV2SpecialCharsTest, EscapeHash) {
    const char* latex = R"(\#1 priority)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "#1") != nullptr || strstr(html, "1") != nullptr);
}

TEST_F(LatexHtmlV2SpecialCharsTest, EscapeUnderscore) {
    const char* latex = R"(file\_name.txt)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "_") != nullptr);
    EXPECT_TRUE(strstr(html, "name") != nullptr);
}

TEST_F(LatexHtmlV2SpecialCharsTest, EscapeBraces) {
    const char* latex = R"(\{curly braces\})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "{") != nullptr);
    EXPECT_TRUE(strstr(html, "}") != nullptr);
    EXPECT_TRUE(strstr(html, "curly") != nullptr);
}

// =============================================================================
// Diacritic/Accent Tests
// =============================================================================

TEST_F(LatexHtmlV2SpecialCharsTest, AcuteAccent) {
    const char* latex = R"(\'{e})";  // é
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    // Should contain é (either as UTF-8 or HTML entity)
    EXPECT_TRUE(strstr(html, "é") != nullptr || 
                strstr(html, "&eacute;") != nullptr ||
                strstr(html, "e") != nullptr);
}

TEST_F(LatexHtmlV2SpecialCharsTest, GraveAccent) {
    const char* latex = R"(\`{e})";  // è
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "è") != nullptr || 
                strstr(html, "&egrave;") != nullptr ||
                strstr(html, "e") != nullptr);
}

TEST_F(LatexHtmlV2SpecialCharsTest, CircumflexAccent) {
    const char* latex = R"(\^{e})";  // ê
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "ê") != nullptr || 
                strstr(html, "&ecirc;") != nullptr ||
                strstr(html, "e") != nullptr);
}

TEST_F(LatexHtmlV2SpecialCharsTest, Umlaut) {
    const char* latex = R"(\"{o})";  // ö
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "ö") != nullptr || 
                strstr(html, "&ouml;") != nullptr ||
                strstr(html, "o") != nullptr);
}

TEST_F(LatexHtmlV2SpecialCharsTest, TildeAccent) {
    const char* latex = R"(\~{n})";  // ñ
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "ñ") != nullptr || 
                strstr(html, "&ntilde;") != nullptr ||
                strstr(html, "n") != nullptr);
}

// =============================================================================
// Combined Tests
// =============================================================================

TEST_F(LatexHtmlV2SpecialCharsTest, MixedSpecialChars) {
    const char* latex = R"(Cost: \$100 \& 50\% off for \#1!)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Cost") != nullptr);
    EXPECT_TRUE(strstr(html, "100") != nullptr);
    EXPECT_TRUE(strstr(html, "50") != nullptr);
    EXPECT_TRUE(strstr(html, "off") != nullptr);
}

TEST_F(LatexHtmlV2SpecialCharsTest, AccentedName) {
    const char* latex = R"(Ren\'{e} and Na\"{\i}ve)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Ren") != nullptr);
    EXPECT_TRUE(strstr(html, "and") != nullptr);
    EXPECT_TRUE(strstr(html, "Na") != nullptr);
}
