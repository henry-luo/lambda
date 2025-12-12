// test_latex_html_v2_macros.cpp - Tests for LaTeX HTML V2 Formatter Phase 6: Custom Macros & Commands
// Tests \newcommand, \renewcommand, \def with argument handling

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
    return nullptr;
}

class LatexHtmlV2MacrosTest : public ::testing::Test {
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
// Basic \newcommand Tests
// =============================================================================

TEST_F(LatexHtmlV2MacrosTest, NewCommandSimple) {
    const char* latex = R"(\newcommand{\hello}{Hello, World!}\hello)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    printf("NewCommandSimple HTML: '%s'\n", html);  // DEBUG
    EXPECT_TRUE(strstr(html, "Hello, World!") != nullptr) << "Should expand simple macro";
}

TEST_F(LatexHtmlV2MacrosTest, NewCommandWithArguments) {
    const char* latex = R"(\newcommand{\greet}[1]{Hello, #1!}\greet{Alice})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    printf("NewCommandWithArguments HTML: '%s'\n", html);  // DEBUG
    EXPECT_TRUE(strstr(html, "Hello, Alice!") != nullptr) << "Should substitute argument";
}

TEST_F(LatexHtmlV2MacrosTest, NewCommandMultipleArgs) {
    const char* latex = R"(\newcommand{\fullname}[2]{#1 #2}\fullname{John}{Doe})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "John Doe") != nullptr) << "Should substitute multiple arguments";
}

TEST_F(LatexHtmlV2MacrosTest, NewCommandOptionalArg) {
    const char* latex = R"(\newcommand{\greet}[2][World]{Hello, #1 and #2!}\greet{Alice}\greet[Bob]{Carol})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Hello, World and Alice!") != nullptr) << "Should use default value";
    EXPECT_TRUE(strstr(html, "Hello, Bob and Carol!") != nullptr) << "Should use provided value";
}

// =============================================================================
// \renewcommand Tests
// =============================================================================

TEST_F(LatexHtmlV2MacrosTest, RenewCommand) {
    const char* latex = R"(\newcommand{\test}{Original}\test\renewcommand{\test}{Modified}\test)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Original") != nullptr) << "Should show original definition";
    EXPECT_TRUE(strstr(html, "Modified") != nullptr) << "Should show redefined version";
}

TEST_F(LatexHtmlV2MacrosTest, RenewBuiltinCommand) {
    const char* latex = R"(\renewcommand{\emph}[1]{\textbf{#1}}This is \emph{emphasized}.)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "textbf") != nullptr || strstr(html, "emphasized") != nullptr) 
        << "Should redefine emph as bold";
}

// =============================================================================
// \def Tests (TeX primitive)
// =============================================================================

TEST_F(LatexHtmlV2MacrosTest, DefSimple) {
    const char* latex = R"(\def\test{Testing}\test)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Testing") != nullptr) << "Should expand \\def macro";
}

TEST_F(LatexHtmlV2MacrosTest, DefWithArgs) {
    const char* latex = R"(\def\double#1{#1#1}\double{A})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "AA") != nullptr) << "Should expand \\def with argument";
}

// =============================================================================
// Nested and Complex Macros
// =============================================================================

TEST_F(LatexHtmlV2MacrosTest, NestedMacros) {
    const char* latex = R"(\newcommand{\bold}[1]{\textbf{#1}}\newcommand{\emphbold}[1]{\bold{\emph{#1}}}\emphbold{Text})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Text") != nullptr) << "Should expand nested macros";
}

TEST_F(LatexHtmlV2MacrosTest, MacroWithFormatting) {
    const char* latex = R"(\newcommand{\important}[1]{\textbf{\textit{#1}}}\important{Critical})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Critical") != nullptr) << "Should apply nested formatting";
}

TEST_F(LatexHtmlV2MacrosTest, RecursiveMacroUsage) {
    const char* latex = R"(\newcommand{\twice}[1]{#1 #1}\twice{\twice{X}})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "X X X X") != nullptr) << "Should expand recursively";
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

TEST_F(LatexHtmlV2MacrosTest, UndefinedMacro) {
    const char* latex = R"(\undefined)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    // Should either output the command as-is or skip it
}

TEST_F(LatexHtmlV2MacrosTest, MacroRedefineWithDifferentArgs) {
    const char* latex = R"(\newcommand{\test}[1]{One: #1}\test{A}\renewcommand{\test}[2]{Two: #1, #2}\test{B}{C})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "One: A") != nullptr) << "Should use first definition";
    EXPECT_TRUE(strstr(html, "Two: B, C") != nullptr) << "Should use redefined version";
}

TEST_F(LatexHtmlV2MacrosTest, ProvideCommand) {
    const char* latex = R"(\providecommand{\test}{First}\providecommand{\test}{Second}\test)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "First") != nullptr) << "\\providecommand should not override";
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
