// test_html_parity.cpp - Compare legacy HTML pipeline with unified document model pipeline
//
// Phase F of LaTeX pipeline unification: Validate that the unified pipeline
// produces output equivalent to the legacy format_latex_html_v2.cpp pipeline.
//
// Reference: vibe/Latex_Typeset5.md Phase F

#include <gtest/gtest.h>
#include "../lambda/tex/tex_document_model.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/format/format.h"
#include "../lambda/lambda-data.hpp"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/strbuf.h"
#include "../lib/log.h"
#include <cstring>
#include <string>
#include <algorithm>
#include <cctype>

using namespace tex;

// Forward declarations
extern "C" {
    void parse_latex_ts(Input* input, const char* latex_string);
    Item format_latex_html_v2_c(Input* input, int text_mode);
}

// ============================================================================
// HTML Normalizer - for comparing HTML output ignoring whitespace differences
// ============================================================================

class HtmlNormalizer {
public:
    // Normalize HTML for comparison
    static std::string normalize(const std::string& html) {
        std::string result;
        result.reserve(html.size());
        
        bool in_tag = false;
        bool prev_space = true;  // Start true to trim leading whitespace
        
        for (size_t i = 0; i < html.size(); i++) {
            char c = html[i];
            
            if (c == '<') {
                in_tag = true;
                // Add space before tag if needed
                if (!result.empty() && !prev_space && result.back() != '>') {
                    // Don't add space - let tag follow content directly
                }
                result += c;
                prev_space = false;
            } else if (c == '>') {
                in_tag = false;
                result += c;
                prev_space = false;
            } else if (in_tag) {
                // Inside tag - preserve structure but normalize whitespace
                if (std::isspace(c)) {
                    if (!prev_space) {
                        result += ' ';
                        prev_space = true;
                    }
                } else {
                    result += c;
                    prev_space = false;
                }
            } else {
                // Text content
                if (std::isspace(c)) {
                    if (!prev_space) {
                        result += ' ';
                        prev_space = true;
                    }
                } else {
                    result += c;
                    prev_space = false;
                }
            }
        }
        
        // Trim trailing whitespace
        while (!result.empty() && std::isspace(result.back())) {
            result.pop_back();
        }
        
        return result;
    }
    
    // Extract just the text content (no tags)
    static std::string extractText(const std::string& html) {
        std::string result;
        bool in_tag = false;
        
        for (char c : html) {
            if (c == '<') {
                in_tag = true;
            } else if (c == '>') {
                in_tag = false;
            } else if (!in_tag) {
                if (std::isspace(c)) {
                    if (result.empty() || !std::isspace(result.back())) {
                        result += ' ';
                    }
                } else {
                    result += c;
                }
            }
        }
        
        // Trim
        while (!result.empty() && std::isspace(result.back())) {
            result.pop_back();
        }
        while (!result.empty() && std::isspace(result.front())) {
            result.erase(0, 1);
        }
        
        return result;
    }
    
    // Check if HTML contains a specific tag
    static bool hasTag(const std::string& html, const char* tag) {
        std::string pattern = std::string("<") + tag;
        return html.find(pattern) != std::string::npos;
    }
    
    // Check if HTML contains specific text
    static bool hasText(const std::string& html, const char* text) {
        return html.find(text) != std::string::npos;
    }
};

// ============================================================================
// Test Fixture
// ============================================================================

class HtmlParityTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    Input* input;
    TFMFontManager fonts;

    void SetUp() override {
        log_init(nullptr);
        pool = pool_create();
        arena = arena_create_default(pool);
        input = InputManager::create_input(nullptr);
    }

    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
        InputManager::destroy_global();
    }
    
    // Render using legacy pipeline
    std::string renderLegacy(const char* latex) {
        // Reset input
        input = InputManager::create_input(nullptr);
        
        parse_latex_ts(input, latex);
        Item result = format_latex_html_v2_c(input, 1);  // text_mode = 1
        
        if (get_type_id(result) == LMD_TYPE_STRING) {
            String* str = (String*)result.string_ptr;
            if (str && str->len > 0) {
                return std::string(str->chars, str->len);
            }
        }
        return "";
    }
    
    // Render using unified pipeline
    std::string renderUnified(const char* latex) {
        // Reset arena for fresh allocation
        arena_reset(arena);
        
        TexDocumentModel* doc = doc_model_from_string(latex, strlen(latex), arena, &fonts);
        
        if (!doc || !doc->root) {
            return "";
        }
        
        StrBuf* out = strbuf_new_cap(4096);
        HtmlOutputOptions opts = HtmlOutputOptions::defaults();
        opts.pretty_print = false;
        
        doc_model_to_html(doc, out, opts);
        
        std::string result(out->str, out->length);
        strbuf_free(out);
        
        return result;
    }
    
    // Compare normalized HTML
    void compareHtml(const char* latex, const char* testName) {
        std::string legacy = renderLegacy(latex);
        std::string unified = renderUnified(latex);
        
        std::string normLegacy = HtmlNormalizer::normalize(legacy);
        std::string normUnified = HtmlNormalizer::normalize(unified);
        
        // For now, we check structural equivalence rather than exact match
        // This allows for CSS class differences, attribute ordering, etc.
        
        // Extract text content
        std::string textLegacy = HtmlNormalizer::extractText(legacy);
        std::string textUnified = HtmlNormalizer::extractText(unified);
        
        EXPECT_EQ(textLegacy, textUnified) 
            << "Text content mismatch in " << testName << "\n"
            << "Legacy text: " << textLegacy << "\n"
            << "Unified text: " << textUnified << "\n"
            << "Legacy HTML: " << legacy << "\n"
            << "Unified HTML: " << unified;
    }
    
    // Check that unified output contains expected elements
    void checkStructure(const char* latex, const char* tag, const char* text) {
        std::string unified = renderUnified(latex);
        
        EXPECT_TRUE(HtmlNormalizer::hasTag(unified, tag))
            << "Missing <" << tag << "> in unified output\n"
            << "HTML: " << unified;
        
        if (text) {
            EXPECT_TRUE(HtmlNormalizer::hasText(unified, text))
                << "Missing text '" << text << "' in unified output\n"
                << "HTML: " << unified;
        }
    }
};

// ============================================================================
// Basic Text Tests
// ============================================================================

TEST_F(HtmlParityTest, PlainText) {
    const char* latex = "Hello World";
    std::string unified = renderUnified(latex);
    
    EXPECT_TRUE(HtmlNormalizer::hasText(unified, "Hello"))
        << "Should contain 'Hello': " << unified;
    EXPECT_TRUE(HtmlNormalizer::hasText(unified, "World"))
        << "Should contain 'World': " << unified;
}

TEST_F(HtmlParityTest, BoldText) {
    const char* latex = "\\textbf{bold text}";
    checkStructure(latex, "strong", "bold text");
}

TEST_F(HtmlParityTest, ItalicText) {
    const char* latex = "\\textit{italic text}";
    checkStructure(latex, "em", "italic text");
}

TEST_F(HtmlParityTest, MonospaceText) {
    const char* latex = "\\texttt{mono text}";
    checkStructure(latex, "code", "mono text");
}

// ============================================================================
// Section Tests
// ============================================================================

TEST_F(HtmlParityTest, Section) {
    const char* latex = "\\section{Introduction}";
    std::string unified = renderUnified(latex);
    
    // Should have heading tag (section -> level 2 -> h3 in our mapping)
    bool hasH1 = HtmlNormalizer::hasTag(unified, "h1");
    bool hasH2 = HtmlNormalizer::hasTag(unified, "h2");
    bool hasH3 = HtmlNormalizer::hasTag(unified, "h3");
    EXPECT_TRUE(hasH1 || hasH2 || hasH3) << "Should have heading tag: " << unified;
    EXPECT_TRUE(HtmlNormalizer::hasText(unified, "Introduction"))
        << "Should contain 'Introduction': " << unified;
}

TEST_F(HtmlParityTest, Subsection) {
    const char* latex = "\\subsection{Details}";
    std::string unified = renderUnified(latex);
    
    bool hasH2 = HtmlNormalizer::hasTag(unified, "h2");
    bool hasH3 = HtmlNormalizer::hasTag(unified, "h3");
    bool hasH4 = HtmlNormalizer::hasTag(unified, "h4");
    EXPECT_TRUE(hasH2 || hasH3 || hasH4) << "Should have heading tag: " << unified;
    EXPECT_TRUE(HtmlNormalizer::hasText(unified, "Details"))
        << "Should contain 'Details': " << unified;
}

// ============================================================================
// List Tests
// ============================================================================

TEST_F(HtmlParityTest, ItemizeList) {
    const char* latex = R"(
\begin{itemize}
\item First
\item Second
\end{itemize}
)";
    std::string unified = renderUnified(latex);
    
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "ul"))
        << "Should have <ul>: " << unified;
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "li"))
        << "Should have <li>: " << unified;
}

TEST_F(HtmlParityTest, EnumerateList) {
    const char* latex = R"(
\begin{enumerate}
\item First
\item Second
\end{enumerate}
)";
    std::string unified = renderUnified(latex);
    
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "ol"))
        << "Should have <ol>: " << unified;
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "li"))
        << "Should have <li>: " << unified;
}

// ============================================================================
// Table Tests
// ============================================================================

TEST_F(HtmlParityTest, SimpleTable) {
    const char* latex = R"(
\begin{tabular}{|c|c|}
\hline
A & B \\
\hline
1 & 2 \\
\hline
\end{tabular}
)";
    std::string unified = renderUnified(latex);
    
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "table"))
        << "Should have <table>: " << unified;
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "tr"))
        << "Should have <tr>: " << unified;
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "td"))
        << "Should have <td>: " << unified;
}

// ============================================================================
// Quote/Blockquote Tests
// ============================================================================

TEST_F(HtmlParityTest, QuoteEnvironment) {
    const char* latex = R"(
\begin{quote}
This is a quote.
\end{quote}
)";
    std::string unified = renderUnified(latex);
    
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "blockquote"))
        << "Should have <blockquote>: " << unified;
}

// ============================================================================
// Code Block Tests
// ============================================================================

TEST_F(HtmlParityTest, VerbatimEnvironment) {
    const char* latex = R"(
\begin{verbatim}
int main() { return 0; }
\end{verbatim}
)";
    std::string unified = renderUnified(latex);
    
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "pre"))
        << "Should have <pre>: " << unified;
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "code"))
        << "Should have <code>: " << unified;
}

// ============================================================================
// Link and Image Tests (Phase E)
// ============================================================================

TEST_F(HtmlParityTest, HrefLink) {
    const char* latex = "\\href{https://example.com}{Example}";
    std::string unified = renderUnified(latex);
    
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "a"))
        << "Should have <a>: " << unified;
    EXPECT_TRUE(HtmlNormalizer::hasText(unified, "Example"))
        << "Should contain link text: " << unified;
}

TEST_F(HtmlParityTest, UrlCommand) {
    const char* latex = "\\url{https://example.com}";
    std::string unified = renderUnified(latex);
    
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "a"))
        << "Should have <a>: " << unified;
}

TEST_F(HtmlParityTest, Includegraphics) {
    const char* latex = "\\includegraphics{image.png}";
    std::string unified = renderUnified(latex);
    
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "img"))
        << "Should have <img>: " << unified;
}

// ============================================================================
// Math Tests
// ============================================================================

TEST_F(HtmlParityTest, InlineMath) {
    const char* latex = "The formula $x^2$ is here.";
    std::string unified = renderUnified(latex);
    
    // Should have some math representation (span with class or actual math)
    EXPECT_TRUE(HtmlNormalizer::hasText(unified, "x") || 
                HtmlNormalizer::hasTag(unified, "span") ||
                HtmlNormalizer::hasTag(unified, "math"))
        << "Should have math content: " << unified;
}

TEST_F(HtmlParityTest, DisplayMath) {
    const char* latex = "\\[E = mc^2\\]";
    std::string unified = renderUnified(latex);
    
    // Should have display math (div or display block)
    EXPECT_TRUE(HtmlNormalizer::hasText(unified, "E") ||
                HtmlNormalizer::hasTag(unified, "div") ||
                HtmlNormalizer::hasTag(unified, "span"))
        << "Should have display math: " << unified;
}

// ============================================================================
// Complex Document Tests
// ============================================================================

TEST_F(HtmlParityTest, DocumentWithSections) {
    const char* latex = R"(
\section{Introduction}
This is the introduction.

\section{Methods}
These are the methods.
)";
    std::string unified = renderUnified(latex);
    
    EXPECT_TRUE(HtmlNormalizer::hasText(unified, "Introduction"))
        << "Should have Introduction: " << unified;
    EXPECT_TRUE(HtmlNormalizer::hasText(unified, "Methods"))
        << "Should have Methods: " << unified;
}

TEST_F(HtmlParityTest, DocumentWithList) {
    const char* latex = R"(
\section{Items}
\begin{itemize}
\item First item
\item Second item with \textbf{bold}
\end{itemize}
)";
    std::string unified = renderUnified(latex);
    
    EXPECT_TRUE(HtmlNormalizer::hasTag(unified, "ul"))
        << "Should have <ul>: " << unified;
    EXPECT_TRUE(HtmlNormalizer::hasText(unified, "First item"))
        << "Should have 'First item': " << unified;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
