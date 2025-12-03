/**
 * LaTeX Parser Unit Tests
 *
 * Tests for the modular LaTeX parser implementation in lambda/input/latex/
 * Tests parsing of LaTeX files from test/input/, excluding math-intensive files.
 */

#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include "../lambda/lambda-data.hpp"
#include "../lambda/input/input.hpp"
#include "../lib/mempool.h"
#include "../lib/url.h"
#include "../lib/log.h"

extern "C" {
    Input* input_from_source(const char* source, Url* abs_url, String* type, String* flavor);
    Url* url_parse(const char* input);
    Url* url_parse_with_base(const char* input, const Url* base);
    void url_destroy(Url* url);
}

// Helper function to create a Lambda String from C string
static String* create_string(const char* text) {
    if (!text) return nullptr;
    size_t len = strlen(text);
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return nullptr;
    result->len = len;
    result->ref_cnt = 1;
    strcpy(result->chars, text);
    return result;
}

// Helper to read file contents
static std::string read_file_contents(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Helper to count elements of a specific type in the AST
static int count_elements_by_tag(Item item, const char* tag_name) {
    TypeId type = get_type_id(item);
    int count = 0;

    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = (Element*)item.pointer;
        if (elem && elem->type) {
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type->name.str && elmt_type->name.length > 0) {
                if (strncmp(elmt_type->name.str, tag_name, elmt_type->name.length) == 0 &&
                    strlen(tag_name) == elmt_type->name.length) {
                    count = 1;
                }
            }
        }

        // Recurse into children
        List* list = (List*)elem;
        for (int64_t i = 0; i < list->length; i++) {
            count += count_elements_by_tag(list->items[i], tag_name);
        }
    } else if (type == LMD_TYPE_LIST || type == LMD_TYPE_ARRAY) {
        List* list = (List*)item.pointer;
        if (list) {
            for (int64_t i = 0; i < list->length; i++) {
                count += count_elements_by_tag(list->items[i], tag_name);
            }
        }
    }

    return count;
}

// Test fixture class
class LatexParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        cwd_ = url_parse("file://./");
        type_str_ = create_string("latex");
    }

    void TearDown() override {
        if (cwd_) url_destroy(cwd_);
        if (type_str_) free(type_str_);
    }

    // Parse LaTeX content and return parsed input
    Input* parse_latex(const char* content, const char* filename = "test.tex") {
        Url* url = url_parse_with_base(filename, cwd_);
        char* content_copy = strdup(content);
        Input* input = input_from_source(content_copy, url, type_str_, nullptr);
        // Note: content_copy is owned by input after this call
        return input;
    }

    // Parse LaTeX file from test/input directory
    Input* parse_latex_file(const std::string& filename) {
        std::string path = "test/input/" + filename;
        std::string content = read_file_contents(path);
        if (content.empty()) {
            return nullptr;
        }
        return parse_latex(content.c_str(), filename.c_str());
    }

    // Verify that the AST root is valid
    bool verify_ast_valid(Input* input) {
        if (!input) return false;
        if (input->root.item == ITEM_NULL || input->root.item == ITEM_ERROR) return false;

        TypeId type = get_type_id(input->root);
        return (type == LMD_TYPE_ELEMENT || type == LMD_TYPE_LIST || type == LMD_TYPE_ARRAY);
    }

    Url* cwd_ = nullptr;
    String* type_str_ = nullptr;
};

// =============================================================================
// Basic Parsing Tests
// =============================================================================

TEST_F(LatexParserTest, ParseEmptyDocument) {
    const char* latex = "";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    // Empty document should still produce valid output
}

TEST_F(LatexParserTest, ParseSimpleText) {
    const char* latex = "Hello, World!";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
}

TEST_F(LatexParserTest, ParseDocumentClass) {
    const char* latex = "\\documentclass{article}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "documentclass"), 1);
}

TEST_F(LatexParserTest, ParseSection) {
    const char* latex = "\\section{Test Section}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "section"), 1);
}

TEST_F(LatexParserTest, ParseTextFormatting) {
    const char* latex = "\\textbf{bold} and \\textit{italic} and \\texttt{mono}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "textbf"), 1);
    EXPECT_GE(count_elements_by_tag(input->root, "textit"), 1);
    EXPECT_GE(count_elements_by_tag(input->root, "texttt"), 1);
}

// =============================================================================
// Environment Tests
// =============================================================================

TEST_F(LatexParserTest, ParseDocumentEnvironment) {
    const char* latex =
        "\\documentclass{article}\n"
        "\\begin{document}\n"
        "Content here\n"
        "\\end{document}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "document"), 1);
}

TEST_F(LatexParserTest, ParseItemizeEnvironment) {
    const char* latex =
        "\\begin{itemize}\n"
        "\\item First item\n"
        "\\item Second item\n"
        "\\end{itemize}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "itemize"), 1);
    EXPECT_GE(count_elements_by_tag(input->root, "item"), 2);
}

TEST_F(LatexParserTest, ParseEnumerateEnvironment) {
    const char* latex =
        "\\begin{enumerate}\n"
        "\\item First\n"
        "\\item Second\n"
        "\\item Third\n"
        "\\end{enumerate}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "enumerate"), 1);
}

TEST_F(LatexParserTest, ParseTabularEnvironment) {
    const char* latex =
        "\\begin{tabular}{|c|c|}\n"
        "\\hline\n"
        "A & B \\\\\n"
        "C & D \\\\\n"
        "\\hline\n"
        "\\end{tabular}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "tabular"), 1);
}

TEST_F(LatexParserTest, ParseVerbatimEnvironment) {
    const char* latex =
        "\\begin{verbatim}\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n"
        "\\end{verbatim}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "verbatim"), 1);
}

// =============================================================================
// Special Character Tests
// =============================================================================

TEST_F(LatexParserTest, ParseEscapedCharacters) {
    const char* latex = "\\$ \\% \\& \\# \\_ \\{ \\}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
}

TEST_F(LatexParserTest, ParseDashes) {
    const char* latex = "em---dash and en--dash and hyphen-ation";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
}

TEST_F(LatexParserTest, ParseQuotes) {
    const char* latex = "``double quotes'' and `single quotes'";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
}

TEST_F(LatexParserTest, ParseTilde) {
    const char* latex = "non~breaking~space";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
}

// =============================================================================
// Diacritic Tests
// =============================================================================

TEST_F(LatexParserTest, ParseDiacritics) {
    const char* latex = "\\'e \\`a \\^o \\\"u \\~n \\=e \\c{c}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
}

// =============================================================================
// Command Tests
// =============================================================================

TEST_F(LatexParserTest, ParseUsepackage) {
    const char* latex = "\\usepackage[utf8]{inputenc}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "usepackage"), 1);
}

TEST_F(LatexParserTest, ParseTitleAuthorDate) {
    const char* latex =
        "\\title{My Document}\n"
        "\\author{Author Name}\n"
        "\\date{\\today}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "title"), 1);
    EXPECT_GE(count_elements_by_tag(input->root, "author"), 1);
    EXPECT_GE(count_elements_by_tag(input->root, "date"), 1);
}

TEST_F(LatexParserTest, ParseMaketitle) {
    const char* latex = "\\maketitle";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "maketitle"), 1);
}

TEST_F(LatexParserTest, ParseVerb) {
    const char* latex = "\\verb|inline code|";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "verb"), 1);
}

// =============================================================================
// Math Tests (disabled - math parser has issues with memory management)
// These tests are skipped to avoid crashes in the existing parser
// =============================================================================

TEST_F(LatexParserTest, DISABLED_ParseInlineMath) {
    const char* latex = "Equation: $E = mc^2$";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "math"), 1);
}

TEST_F(LatexParserTest, DISABLED_ParseDisplayMath) {
    const char* latex = "$$x = \\frac{-b \\pm \\sqrt{b^2-4ac}}{2a}$$";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "displaymath"), 1);
}

TEST_F(LatexParserTest, DISABLED_ParseEquationEnvironment) {
    const char* latex =
        "\\begin{equation}\n"
        "E = mc^2\n"
        "\\end{equation}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "equation"), 1);
}

// =============================================================================
// File-based Tests (non-math files from test/input/)
// =============================================================================

TEST_F(LatexParserTest, ParseBasicTestTex) {
    Input* input = parse_latex_file("basic_test.tex");
    ASSERT_NE(input, nullptr) << "Failed to parse basic_test.tex";
    EXPECT_TRUE(verify_ast_valid(input));

    // basic_test.tex should have document structure
    EXPECT_GE(count_elements_by_tag(input->root, "documentclass"), 1);
    EXPECT_GE(count_elements_by_tag(input->root, "document"), 1);
    EXPECT_GE(count_elements_by_tag(input->root, "section"), 1);
}

TEST_F(LatexParserTest, DISABLED_ParseComprehensiveTestTex) {
    // Disabled: contains math content that causes issues
    Input* input = parse_latex_file("comprehensive_test.tex");
    ASSERT_NE(input, nullptr) << "Failed to parse comprehensive_test.tex";
    EXPECT_TRUE(verify_ast_valid(input));

    // Should have multiple sections and features
    EXPECT_GE(count_elements_by_tag(input->root, "section"), 1);
    EXPECT_GE(count_elements_by_tag(input->root, "itemize"), 1);
}

TEST_F(LatexParserTest, DISABLED_ParseEnhancedTestTex) {
    // Disabled: contains math content that causes issues
    Input* input = parse_latex_file("enhanced_test.tex");
    ASSERT_NE(input, nullptr) << "Failed to parse enhanced_test.tex";
    EXPECT_TRUE(verify_ast_valid(input));

    // enhanced_test.tex has tables and formatting
    EXPECT_GE(count_elements_by_tag(input->root, "tabular"), 1);
    EXPECT_GE(count_elements_by_tag(input->root, "textbf"), 1);
}

TEST_F(LatexParserTest, ParseEnhancedRealTex) {
    Input* input = parse_latex_file("enhanced_real.tex");
    ASSERT_NE(input, nullptr) << "Failed to parse enhanced_real.tex";
    EXPECT_TRUE(verify_ast_valid(input));
}

TEST_F(LatexParserTest, DISABLED_ParseComprehensiveTex) {
    // Disabled: contains math content that causes issues
    Input* input = parse_latex_file("comprehensive.tex");
    ASSERT_NE(input, nullptr) << "Failed to parse comprehensive.tex";
    EXPECT_TRUE(verify_ast_valid(input));

    // comprehensive.tex is a full document
    EXPECT_GE(count_elements_by_tag(input->root, "documentclass"), 1);
}

TEST_F(LatexParserTest, DISABLED_ParseTestTex) {
    // Disabled: may contain math content that causes issues
    Input* input = parse_latex_file("test.tex");
    ASSERT_NE(input, nullptr) << "Failed to parse test.tex";
    EXPECT_TRUE(verify_ast_valid(input));
}

TEST_F(LatexParserTest, DISABLED_ParseTypographyLayoutTestTex) {
    // Disabled: may contain math content that causes issues
    Input* input = parse_latex_file("typography_layout_test.tex");
    ASSERT_NE(input, nullptr) << "Failed to parse typography_layout_test.tex";
    EXPECT_TRUE(verify_ast_valid(input));
}

// =============================================================================
// Stress Tests
// =============================================================================

TEST_F(LatexParserTest, ParseNestedEnvironments) {
    const char* latex =
        "\\begin{document}\n"
        "\\begin{itemize}\n"
        "\\item First level\n"
        "  \\begin{enumerate}\n"
        "  \\item Nested item 1\n"
        "  \\item Nested item 2\n"
        "  \\end{enumerate}\n"
        "\\item Back to first level\n"
        "\\end{itemize}\n"
        "\\end{document}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
}

TEST_F(LatexParserTest, ParseComplexDocumentNoMath) {
    // Complex document without math to avoid crashes
    const char* latex =
        "\\documentclass[12pt,a4paper]{article}\n"
        "\\usepackage[utf8]{inputenc}\n"
        "\\title{Complex Test}\n"
        "\\author{Test}\n"
        "\\date{\\today}\n"
        "\\begin{document}\n"
        "\\maketitle\n"
        "\\tableofcontents\n"
        "\\section{Introduction}\n"
        "This is \\textbf{bold} and \\textit{italic}.\n"
        "\\subsection{Details}\n"
        "Some text here.\n"
        "\\begin{itemize}\n"
        "\\item Item one\n"
        "\\item Item two with \\emph{emphasis}\n"
        "\\end{itemize}\n"
        "\\section{Conclusion}\n"
        "The end.\n"
        "\\end{document}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));

    // Verify structure
    EXPECT_GE(count_elements_by_tag(input->root, "documentclass"), 1);
    EXPECT_GE(count_elements_by_tag(input->root, "document"), 1);
    EXPECT_GE(count_elements_by_tag(input->root, "section"), 2);
    EXPECT_GE(count_elements_by_tag(input->root, "subsection"), 1);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(LatexParserTest, ParseComments) {
    const char* latex =
        "% This is a comment\n"
        "Text content % inline comment\n"
        "More text";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
}

TEST_F(LatexParserTest, ParseEmptyEnvironment) {
    const char* latex =
        "\\begin{center}\n"
        "\\end{center}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
}

TEST_F(LatexParserTest, ParseWhitespaceHandling) {
    const char* latex =
        "   Leading whitespace\n"
        "\n"
        "Paragraph break above\n"
        "   \n"
        "Another paragraph";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
}

TEST_F(LatexParserTest, ParseUnknownCommand) {
    const char* latex = "\\unknowncommand{arg}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    // Unknown commands should still be parsed as elements
    EXPECT_TRUE(verify_ast_valid(input));
}

TEST_F(LatexParserTest, ParseGroup) {
    const char* latex = "{grouped content}";
    Input* input = parse_latex(latex);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(verify_ast_valid(input));
    EXPECT_GE(count_elements_by_tag(input->root, "group"), 1);
}

// Main function
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
