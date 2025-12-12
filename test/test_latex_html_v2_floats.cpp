/**
 * Test file for LaTeX to HTML v2 formatter - Float Environments
 * 
 * Tests the float environment commands:
 * - figure environment with includegraphics
 * - table environment with caption
 * - caption command
 * - label integration with floats
 */

#include <gtest/gtest.h>
#include "../lambda/format/format.h"
#include "../lambda/input/input.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/log.h"
#include <cstring>

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

// Test fixture for LaTeX HTML V2 formatter - Floats
class LatexHtmlV2FloatsTest : public ::testing::Test {
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
// Figure Environment Tests
// =============================================================================

TEST_F(LatexHtmlV2FloatsTest, SimpleFigure) {
    const char* latex = R"(
\begin{figure}
\includegraphics{image.png}
\caption{A sample figure}
\end{figure}
)";

    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "figure") != nullptr) << "Should contain figure tag";
    EXPECT_TRUE(strstr(html, "img") != nullptr) << "Should contain img tag";
    EXPECT_TRUE(strstr(html, "image.png") != nullptr) << "Should contain filename";
    EXPECT_TRUE(strstr(html, "caption") != nullptr) << "Should contain caption";
    EXPECT_TRUE(strstr(html, "A sample figure") != nullptr) << "Should contain caption text";
}

TEST_F(LatexHtmlV2FloatsTest, FigureWithPosition) {
    const char* latex = R"(
\begin{figure}[h]
\includegraphics{photo.jpg}
\caption{Here positioned figure}
\end{figure}
)";

    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "figure") != nullptr);
    EXPECT_TRUE(strstr(html, "photo.jpg") != nullptr);
    EXPECT_TRUE(strstr(html, "Here positioned figure") != nullptr);
}

TEST_F(LatexHtmlV2FloatsTest, FigureWithLabel) {
    const char* latex = R"(
\begin{figure}
\includegraphics{diagram.pdf}
\caption{A diagram}
\label{fig:diagram}
\end{figure}

See Figure \ref{fig:diagram} for details.
)";

    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "figure") != nullptr);
    EXPECT_TRUE(strstr(html, "diagram.pdf") != nullptr);
    EXPECT_TRUE(strstr(html, "A diagram") != nullptr);
    EXPECT_TRUE(strstr(html, "fig:diagram") != nullptr) << "Should have label";
}

// =============================================================================
// Table Float Environment Tests
// =============================================================================

TEST_F(LatexHtmlV2FloatsTest, TableFloat) {
    const char* latex = R"(
\begin{table}
\caption{Sample data}
\begin{tabular}{lcc}
A & B & C \\
1 & 2 & 3
\end{tabular}
\end{table}
)";

    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "figure") != nullptr) << "Table float uses figure tag";
    EXPECT_TRUE(strstr(html, "caption") != nullptr);
    EXPECT_TRUE(strstr(html, "Sample data") != nullptr);
    EXPECT_TRUE(strstr(html, "table") != nullptr) << "Should contain tabular table";
}

TEST_F(LatexHtmlV2FloatsTest, TableFloatWithPosition) {
    const char* latex = R"(
\begin{table}[t]
\caption{Top positioned table}
\begin{tabular}{ll}
Name & Value \\
Alpha & 100
\end{tabular}
\end{table}
)";

    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "caption") != nullptr);
    EXPECT_TRUE(strstr(html, "Top positioned table") != nullptr);
    EXPECT_TRUE(strstr(html, "Alpha") != nullptr);
    EXPECT_TRUE(strstr(html, "100") != nullptr);
}

TEST_F(LatexHtmlV2FloatsTest, TableFloatWithLabel) {
    const char* latex = R"(
\begin{table}
\caption{Results summary}
\label{tab:results}
\begin{tabular}{lc}
Item & Count \\
Total & 42
\end{tabular}
\end{table}

Table \ref{tab:results} shows the results.
)";

    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Results summary") != nullptr);
    EXPECT_TRUE(strstr(html, "tab:results") != nullptr) << "Should have label";
    EXPECT_TRUE(strstr(html, "42") != nullptr);
}

// =============================================================================
// Graphics Command Tests
// =============================================================================

TEST_F(LatexHtmlV2FloatsTest, IncludegraphicsBasic) {
    const char* latex = R"(
\includegraphics{logo.png}
)";

    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "img") != nullptr);
    EXPECT_TRUE(strstr(html, "logo.png") != nullptr);
}

TEST_F(LatexHtmlV2FloatsTest, IncludegraphicsWithOptions) {
    const char* latex = R"(
\includegraphics[width=5cm]{chart.pdf}
)";

    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "img") != nullptr);
    EXPECT_TRUE(strstr(html, "chart.pdf") != nullptr);
    // Options may be in attributes
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
