/**
 * @file test_latex_html_v2_graphics_color.cpp
 * @brief Tests for LaTeX HTML V2 Formatter - Advanced Graphics & Color
 * 
 * Tests color package and extended graphics options including:
 * - \textcolor{color}{text} command
 * - \colorbox{color}{text} command
 * - \fcolorbox{framecolor}{bgcolor}{text} command
 * - \definecolor{name}{model}{spec} command
 * - \color{name} command
 * - \includegraphics[options]{file} with width, height, scale, angle
 * - Multiple color models: rgb, RGB, HTML, gray, named colors
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
    return nullptr;
}

class LatexHtmlV2GraphicsColorTest : public ::testing::Test {
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
// Text Color Tests
// =============================================================================

TEST_F(LatexHtmlV2GraphicsColorTest, TextColorNamed) {
    const char* latex = R"(This is \textcolor{red}{red text} and \textcolor{blue}{blue text}.)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    printf("TextColorNamed HTML: '%s'\n", html);  // DEBUG
    EXPECT_TRUE(strstr(html, "red") != nullptr) << "Should contain red color";
    EXPECT_TRUE(strstr(html, "blue") != nullptr) << "Should contain blue color";
    EXPECT_TRUE(strstr(html, "red text") != nullptr) << "Should contain colored text";
}

TEST_F(LatexHtmlV2GraphicsColorTest, TextColorRGB) {
    const char* latex = R"(\textcolor[rgb]{1,0,0}{Red text using RGB})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "rgb") != nullptr || strstr(html, "color") != nullptr) 
        << "Should contain color styling";
}

TEST_F(LatexHtmlV2GraphicsColorTest, TextColorHTML) {
    const char* latex = R"(\textcolor[HTML]{FF0000}{Red text using HTML color})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "FF0000") != nullptr || strstr(html, "color") != nullptr) 
        << "Should contain color styling";
}

TEST_F(LatexHtmlV2GraphicsColorTest, ColorCommand) {
    const char* latex = R"(Normal text {\color{red} red text continues} back to normal.)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "red") != nullptr) << "Should contain red color";
    EXPECT_TRUE(strstr(html, "red text continues") != nullptr) << "Should contain colored text";
}

// =============================================================================
// Color Box Tests
// =============================================================================

TEST_F(LatexHtmlV2GraphicsColorTest, ColorBox) {
    const char* latex = R"(\colorbox{yellow}{Text on yellow background})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "yellow") != nullptr || strstr(html, "background") != nullptr) 
        << "Should contain background color";
    EXPECT_TRUE(strstr(html, "Text on yellow background") != nullptr) << "Should contain text";
}

TEST_F(LatexHtmlV2GraphicsColorTest, FColorBox) {
    const char* latex = R"(\fcolorbox{red}{yellow}{Text with frame})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "red") != nullptr || strstr(html, "yellow") != nullptr) 
        << "Should contain frame and background colors";
    EXPECT_TRUE(strstr(html, "Text with frame") != nullptr) << "Should contain text";
}

// =============================================================================
// Color Definition Tests
// =============================================================================

TEST_F(LatexHtmlV2GraphicsColorTest, DefineColorRGB) {
    const char* latex = R"(
\definecolor{myred}{rgb}{0.8,0.1,0.1}
This is \textcolor{myred}{custom red text}.
)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "custom red text") != nullptr) << "Should contain text";
    // Color definition may be stored for later use
}

TEST_F(LatexHtmlV2GraphicsColorTest, DefineColorHTML) {
    const char* latex = R"(
\definecolor{myblue}{HTML}{0066CC}
Text in \textcolor{myblue}{custom blue}.
)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "custom blue") != nullptr) << "Should contain text";
}

// =============================================================================
// Graphics Options Tests
// =============================================================================

TEST_F(LatexHtmlV2GraphicsColorTest, IncludegraphicsWidth) {
    const char* latex = R"(\includegraphics[width=5cm]{image.png})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "image.png") != nullptr) << "Should contain image filename";
    EXPECT_TRUE(strstr(html, "width") != nullptr || strstr(html, "5cm") != nullptr) 
        << "Should contain width attribute";
}

TEST_F(LatexHtmlV2GraphicsColorTest, IncludegraphicsHeight) {
    const char* latex = R"(\includegraphics[height=3cm]{image.jpg})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "image.jpg") != nullptr) << "Should contain image filename";
    EXPECT_TRUE(strstr(html, "height") != nullptr || strstr(html, "3cm") != nullptr) 
        << "Should contain height attribute";
}

TEST_F(LatexHtmlV2GraphicsColorTest, IncludegraphicsScale) {
    const char* latex = R"(\includegraphics[scale=0.5]{diagram.pdf})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "diagram.pdf") != nullptr) << "Should contain image filename";
    EXPECT_TRUE(strstr(html, "scale") != nullptr || strstr(html, "0.5") != nullptr) 
        << "Should contain scale attribute";
}

TEST_F(LatexHtmlV2GraphicsColorTest, IncludegraphicsAngle) {
    const char* latex = R"(\includegraphics[angle=90]{rotated.png})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "rotated.png") != nullptr) << "Should contain image filename";
    EXPECT_TRUE(strstr(html, "angle") != nullptr || strstr(html, "90") != nullptr || 
                strstr(html, "rotate") != nullptr || strstr(html, "transform") != nullptr) 
        << "Should contain rotation attribute";
}

TEST_F(LatexHtmlV2GraphicsColorTest, IncludegraphicsMultipleOptions) {
    const char* latex = R"(\includegraphics[width=10cm,height=5cm,angle=45]{complex.svg})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "complex.svg") != nullptr) << "Should contain image filename";
    // Should handle multiple options gracefully
}

// =============================================================================
// Combined Color and Graphics Tests
// =============================================================================

TEST_F(LatexHtmlV2GraphicsColorTest, ColoredFigure) {
    const char* latex = R"(
\begin{figure}
{\color{blue}
\includegraphics[width=5cm]{chart.png}
\caption{Blue colored figure}
}
\end{figure}
)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "chart.png") != nullptr) << "Should contain image";
    // Caption may have ligatures: "figure" → "ﬁgure"
    EXPECT_TRUE(strstr(html, "Blue colored") != nullptr && strstr(html, "gure") != nullptr) << "Should contain caption";
}

TEST_F(LatexHtmlV2GraphicsColorTest, MultipleColors) {
    const char* latex = R"(
Text can be \textcolor{red}{red}, \textcolor{green}{green}, 
or \textcolor{blue}{blue}. Use \colorbox{yellow}{highlighted} text too.
)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "red") != nullptr) << "Should contain red";
    EXPECT_TRUE(strstr(html, "green") != nullptr) << "Should contain green";
    EXPECT_TRUE(strstr(html, "blue") != nullptr) << "Should contain blue";
    EXPECT_TRUE(strstr(html, "highlighted") != nullptr) << "Should contain highlighted text";
}

TEST_F(LatexHtmlV2GraphicsColorTest, NestedColors) {
    const char* latex = R"(\textcolor{red}{Red text with \textcolor{blue}{nested blue} back to red})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Red text") != nullptr) << "Should contain outer text";
    EXPECT_TRUE(strstr(html, "nested blue") != nullptr) << "Should contain nested text";
}

TEST_F(LatexHtmlV2GraphicsColorTest, GrayScale) {
    const char* latex = R"(\textcolor[gray]{0.5}{Gray text})";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Gray text") != nullptr) << "Should contain text";
    // Gray 0.5 should convert to rgb(127,127,127)
    EXPECT_TRUE(strstr(html, "127") != nullptr || strstr(html, "gray") != nullptr) 
        << "Should contain gray color (as RGB or gray keyword)";
}
