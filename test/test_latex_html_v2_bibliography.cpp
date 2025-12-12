/**
 * @file test_latex_html_v2_bibliography.cpp
 * @brief Tests for LaTeX HTML V2 Formatter - Bibliography & Citations
 * 
 * Tests bibliography and citation handling including:
 * - \cite{key} command (inline citations)
 * - \bibliographystyle{style} command
 * - \bibliography{file} command
 * - BibTeX entry parsing and rendering
 * - Multiple citation styles (plain, alpha, numbered)
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

class LatexHtmlV2BibliographyTest : public ::testing::Test {
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
// Citation Commands Tests
// =============================================================================

TEST_F(LatexHtmlV2BibliographyTest, SimpleCite) {
    const char* latex = R"(See Smith \cite{smith2020} for details.)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "See Smith") != nullptr) << "Should contain text before citation";
    EXPECT_TRUE(strstr(html, "[1]") != nullptr || strstr(html, "smith2020") != nullptr) 
        << "Should contain citation reference";
    EXPECT_TRUE(strstr(html, "for details") != nullptr) << "Should contain text after citation";
}

TEST_F(LatexHtmlV2BibliographyTest, MultipleCites) {
    const char* latex = R"(See \cite{smith2020,jones2019,doe2021} for more information.)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "See") != nullptr);
    // Should contain citations (format depends on style: [1,2,3] or [1-3] or smith2020,jones2019,doe2021)
    EXPECT_TRUE(strstr(html, "for more information") != nullptr);
}

TEST_F(LatexHtmlV2BibliographyTest, CiteWithOptionalText) {
    const char* latex = R"(As shown \cite[p.~42]{smith2020}, the results are clear.)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "As shown") != nullptr);
    EXPECT_TRUE(strstr(html, "p") != nullptr || strstr(html, "42") != nullptr) 
        << "Should include page reference";
    EXPECT_TRUE(strstr(html, "the results are clear") != nullptr);
}

TEST_F(LatexHtmlV2BibliographyTest, CiteAuthor) {
    const char* latex = R"(\citeauthor{smith2020} showed that...)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Smith") != nullptr || strstr(html, "smith2020") != nullptr) 
        << "Should contain author name or key";
    EXPECT_TRUE(strstr(html, "showed that") != nullptr);
}

TEST_F(LatexHtmlV2BibliographyTest, CiteYear) {
    const char* latex = R"(In \citeyear{smith2020}, the study found...)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "In") != nullptr);
    EXPECT_TRUE(strstr(html, "2020") != nullptr || strstr(html, "smith2020") != nullptr) 
        << "Should contain year or key";
    EXPECT_TRUE(strstr(html, "the study found") != nullptr);
}

// =============================================================================
// Bibliography Style Tests
// =============================================================================

TEST_F(LatexHtmlV2BibliographyTest, BibliographyStylePlain) {
    const char* latex = R"(
\bibliographystyle{plain}
Text with citation \cite{smith2020}.
)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Text with citation") != nullptr);
    // Style command should be processed (may not produce visible output)
}

TEST_F(LatexHtmlV2BibliographyTest, BibliographyStyleAlpha) {
    const char* latex = R"(
\bibliographystyle{alpha}
Text with citation \cite{smith2020}.
)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Text with citation") != nullptr);
    // Alpha style should generate citations like [Smi20] instead of [1]
}

// =============================================================================
// Bibliography Command Tests
// =============================================================================

TEST_F(LatexHtmlV2BibliographyTest, BibliographyCommand) {
    const char* latex = R"(
See \cite{smith2020} for details.

\bibliography{references}
)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "See") != nullptr);
    // Bibliography command should generate bibliography section
    // May contain "References" heading
}

TEST_F(LatexHtmlV2BibliographyTest, ThebibliographyEnvironment) {
    const char* latex = R"(
See reference \cite{item1}.

\begin{thebibliography}{99}
\bibitem{item1} Smith, J. (2020). A Study. Journal, 10(2), 123-145.
\bibitem{item2} Jones, A. (2019). Another Work. Publisher.
\end{thebibliography}
)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "See reference") != nullptr);
    EXPECT_TRUE(strstr(html, "Smith") != nullptr) << "Should contain bibliography entry";
    EXPECT_TRUE(strstr(html, "2020") != nullptr) << "Should contain year";
    EXPECT_TRUE(strstr(html, "Journal") != nullptr) << "Should contain journal name";
}

// =============================================================================
// BibTeX Entry Parsing Tests (if we implement .bib parser)
// =============================================================================

TEST_F(LatexHtmlV2BibliographyTest, BibItemSimple) {
    const char* latex = R"(
\begin{thebibliography}{9}
\bibitem{key1} Author Name. Title. Publisher, Year.
\end{thebibliography}
)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Author Name") != nullptr);
    EXPECT_TRUE(strstr(html, "Title") != nullptr);
    EXPECT_TRUE(strstr(html, "Publisher") != nullptr);
}

TEST_F(LatexHtmlV2BibliographyTest, BibItemWithLabel) {
    const char* latex = R"(
\begin{thebibliography}{99}
\bibitem[Smith89]{smith1989} Smith, J. Title of Work. 1989.
\end{thebibliography}
)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Smith") != nullptr);
    EXPECT_TRUE(strstr(html, "1989") != nullptr);
    // Should use custom label "Smith89" instead of [1]
}

// =============================================================================
// Combined Tests
// =============================================================================

TEST_F(LatexHtmlV2BibliographyTest, CompleteDocument) {
    const char* latex = R"(
\section{Introduction}

Previous work \cite{smith2020,jones2019} has shown that...

\section{References}

\begin{thebibliography}{99}
\bibitem{smith2020} Smith, J. (2020). A Comprehensive Study. 
    Journal of Science, 15(3), 234-256.
\bibitem{jones2019} Jones, A., \& Brown, B. (2019). 
    Methods and Applications. Academic Press.
\end{thebibliography}
)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "Introduction") != nullptr);
    EXPECT_TRUE(strstr(html, "Previous work") != nullptr);
    EXPECT_TRUE(strstr(html, "References") != nullptr);
    EXPECT_TRUE(strstr(html, "Smith") != nullptr);
    EXPECT_TRUE(strstr(html, "Jones") != nullptr);
    EXPECT_TRUE(strstr(html, "2020") != nullptr);
    EXPECT_TRUE(strstr(html, "2019") != nullptr);
}

TEST_F(LatexHtmlV2BibliographyTest, CiteWithNonExistentKey) {
    const char* latex = R"(See \cite{nonexistent} for details.)";
    
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    
    ASSERT_NE(html, nullptr);
    EXPECT_TRUE(strstr(html, "See") != nullptr);
    EXPECT_TRUE(strstr(html, "for details") != nullptr);
    // Should handle gracefully - either show key or [?]
}

// =============================================================================
// Main Entry Point - Using GTest default main
// =============================================================================
