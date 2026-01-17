/**
 * test_latex_unified_pipeline.cpp - Tests for unified LaTeX pipeline
 * 
 * Tests the new unified pipeline (doc_model_from_string -> doc_model_to_html)
 * against latex_js fixtures, verifying semantic equivalence with different
 * HTML output format.
 * 
 * Key differences from legacy pipeline:
 * - Uses semantic HTML5 tags (<strong>, <em>, <article>) instead of span classes
 * - Different class naming convention (latex-* prefix)
 * - Different document structure (no <div class="body"> wrapper)
 */

#include <gtest/gtest.h>
#include "fixture_loader.h"
#include "html_comparison.h"
#include "../../lambda/input/input.hpp"
#include "../../lambda/tex/tex_document_model.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/arena.h"
#include "../../lib/log.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <map>
#include <regex>

using namespace tex;

class UnifiedPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
        pool = pool_create();
        arena = arena_create_default(pool);
        ASSERT_NE(pool, nullptr);
        ASSERT_NE(arena, nullptr);
    }

    void TearDown() override {
        if (arena) arena_destroy(arena);
        if (pool) pool_destroy(pool);
    }

    // Render using unified pipeline
    std::string renderUnified(const std::string& latex, bool legacy = false) {
        arena_reset(arena);
        
        TexDocumentModel* doc = doc_model_from_string(
            latex.c_str(), latex.length(), arena, nullptr);
        
        if (!doc || !doc->root) {
            return "";
        }
        
        StrBuf* out = strbuf_new_cap(4096);
        HtmlOutputOptions opts = legacy ? HtmlOutputOptions::legacy() : HtmlOutputOptions::defaults();
        opts.standalone = false;  // Fragment mode, no full HTML document
        opts.pretty_print = false;
        opts.include_css = false;
        
        doc_model_to_html(doc, out, opts);
        
        std::string result(out->str, out->length);
        strbuf_free(out);
        
        return result;
    }
    
    // Render in legacy mode for fixture comparison
    std::string renderLegacy(const std::string& latex) {
        return renderUnified(latex, true);
    }
    
    // Extract text content from HTML (ignoring tags)
    std::string extractText(const std::string& html) {
        std::string result;
        bool in_tag = false;
        for (char c : html) {
            if (c == '<') in_tag = true;
            else if (c == '>') in_tag = false;
            else if (!in_tag) result += c;
        }
        // Normalize whitespace
        std::regex ws_re("\\s+");
        result = std::regex_replace(result, ws_re, " ");
        // Trim
        size_t start = result.find_first_not_of(" \t\n\r");
        size_t end = result.find_last_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        return result.substr(start, end - start + 1);
    }
    
    // Check if HTML contains a specific tag
    bool hasTag(const std::string& html, const std::string& tag) {
        std::string open_tag = "<" + tag;
        return html.find(open_tag) != std::string::npos;
    }
    
    // Check if HTML contains specific text
    bool hasText(const std::string& html, const std::string& text) {
        return extractText(html).find(text) != std::string::npos;
    }
    
    Pool* pool;
    Arena* arena;
};

// ============================================================================
// Basic Text Tests
// ============================================================================

TEST_F(UnifiedPipelineTest, PlainText) {
    std::string html = renderUnified("Hello world");
    EXPECT_TRUE(hasText(html, "Hello world")) << "Output: " << html;
}

TEST_F(UnifiedPipelineTest, BoldText) {
    std::string html = renderUnified("\\textbf{Bold}");
    EXPECT_TRUE(hasTag(html, "strong")) << "Should use <strong>: " << html;
    EXPECT_TRUE(hasText(html, "Bold")) << "Should contain 'Bold': " << html;
}

TEST_F(UnifiedPipelineTest, ItalicText) {
    std::string html = renderUnified("\\textit{Italic}");
    EXPECT_TRUE(hasTag(html, "em")) << "Should use <em>: " << html;
    EXPECT_TRUE(hasText(html, "Italic")) << "Should contain 'Italic': " << html;
}

TEST_F(UnifiedPipelineTest, MonospaceText) {
    std::string html = renderUnified("\\texttt{code}");
    EXPECT_TRUE(hasTag(html, "code")) << "Should use <code>: " << html;
    EXPECT_TRUE(hasText(html, "code")) << "Should contain 'code': " << html;
}

TEST_F(UnifiedPipelineTest, EmphText) {
    std::string html = renderUnified("\\emph{emphasized}");
    EXPECT_TRUE(hasTag(html, "em")) << "Should use <em>: " << html;
    EXPECT_TRUE(hasText(html, "emphasized")) << "Should contain 'emphasized': " << html;
}

TEST_F(UnifiedPipelineTest, Underline) {
    std::string html = renderUnified("\\underline{underlined}");
    EXPECT_TRUE(hasTag(html, "u")) << "Should use <u>: " << html;
    EXPECT_TRUE(hasText(html, "underlined")) << "Should contain 'underlined': " << html;
}

// ============================================================================
// Section Tests
// ============================================================================

TEST_F(UnifiedPipelineTest, Section) {
    std::string html = renderUnified("\\section{Introduction}");
    bool hasHeading = hasTag(html, "h1") || hasTag(html, "h2") || hasTag(html, "h3");
    EXPECT_TRUE(hasHeading) << "Should have heading tag: " << html;
    EXPECT_TRUE(hasText(html, "Introduction")) << "Should contain 'Introduction': " << html;
}

TEST_F(UnifiedPipelineTest, Subsection) {
    std::string html = renderUnified("\\subsection{Details}");
    bool hasHeading = hasTag(html, "h2") || hasTag(html, "h3") || hasTag(html, "h4");
    EXPECT_TRUE(hasHeading) << "Should have heading tag: " << html;
    EXPECT_TRUE(hasText(html, "Details")) << "Should contain 'Details': " << html;
}

TEST_F(UnifiedPipelineTest, Subsubsection) {
    std::string html = renderUnified("\\subsubsection{Fine details}");
    bool hasHeading = hasTag(html, "h3") || hasTag(html, "h4") || hasTag(html, "h5");
    EXPECT_TRUE(hasHeading) << "Should have heading tag: " << html;
    EXPECT_TRUE(hasText(html, "Fine details")) << "Should contain 'Fine details': " << html;
}

TEST_F(UnifiedPipelineTest, MultipleSections) {
    std::string html = renderUnified(
        "\\section{First}\nContent one.\n\\section{Second}\nContent two.");
    EXPECT_TRUE(hasText(html, "First")) << "Should contain 'First': " << html;
    EXPECT_TRUE(hasText(html, "Second")) << "Should contain 'Second': " << html;
    EXPECT_TRUE(hasText(html, "Content one")) << "Should contain 'Content one': " << html;
    EXPECT_TRUE(hasText(html, "Content two")) << "Should contain 'Content two': " << html;
}

// ============================================================================
// List Tests
// ============================================================================

TEST_F(UnifiedPipelineTest, ItemizeList) {
    std::string html = renderUnified(
        "\\begin{itemize}\n\\item First\n\\item Second\n\\end{itemize}");
    EXPECT_TRUE(hasTag(html, "ul")) << "Should have <ul>: " << html;
    EXPECT_TRUE(hasTag(html, "li")) << "Should have <li>: " << html;
    EXPECT_TRUE(hasText(html, "First")) << "Should contain 'First': " << html;
    EXPECT_TRUE(hasText(html, "Second")) << "Should contain 'Second': " << html;
}

TEST_F(UnifiedPipelineTest, EnumerateList) {
    std::string html = renderUnified(
        "\\begin{enumerate}\n\\item First\n\\item Second\n\\end{enumerate}");
    EXPECT_TRUE(hasTag(html, "ol")) << "Should have <ol>: " << html;
    EXPECT_TRUE(hasTag(html, "li")) << "Should have <li>: " << html;
    EXPECT_TRUE(hasText(html, "First")) << "Should contain 'First': " << html;
    EXPECT_TRUE(hasText(html, "Second")) << "Should contain 'Second': " << html;
}

// ============================================================================
// Quote and Verbatim Tests
// ============================================================================

TEST_F(UnifiedPipelineTest, QuoteEnvironment) {
    std::string html = renderUnified("\\begin{quote}\nQuoted text\n\\end{quote}");
    EXPECT_TRUE(hasTag(html, "blockquote")) << "Should have <blockquote>: " << html;
    EXPECT_TRUE(hasText(html, "Quoted text")) << "Should contain 'Quoted text': " << html;
}

TEST_F(UnifiedPipelineTest, VerbatimEnvironment) {
    std::string html = renderUnified("\\begin{verbatim}\ncode here\n\\end{verbatim}");
    EXPECT_TRUE(hasTag(html, "pre")) << "Should have <pre>: " << html;
    EXPECT_TRUE(hasText(html, "code here")) << "Should contain 'code here': " << html;
}

// ============================================================================
// Link and Image Tests
// ============================================================================

TEST_F(UnifiedPipelineTest, HrefLink) {
    std::string html = renderUnified("\\href{http://example.com}{Click here}");
    EXPECT_TRUE(hasTag(html, "a")) << "Should have <a>: " << html;
    EXPECT_TRUE(hasText(html, "Click here")) << "Should contain 'Click here': " << html;
    EXPECT_TRUE(html.find("http://example.com") != std::string::npos) 
        << "Should contain URL: " << html;
}

TEST_F(UnifiedPipelineTest, UrlCommand) {
    std::string html = renderUnified("\\url{http://example.com}");
    EXPECT_TRUE(hasTag(html, "a")) << "Should have <a>: " << html;
    EXPECT_TRUE(html.find("http://example.com") != std::string::npos) 
        << "Should contain URL: " << html;
}

TEST_F(UnifiedPipelineTest, Includegraphics) {
    std::string html = renderUnified("\\includegraphics{image.png}");
    EXPECT_TRUE(hasTag(html, "img")) << "Should have <img>: " << html;
    EXPECT_TRUE(html.find("image.png") != std::string::npos) 
        << "Should contain image path: " << html;
}

// ============================================================================
// Math Tests
// ============================================================================

TEST_F(UnifiedPipelineTest, InlineMath) {
    std::string html = renderUnified("$x^2$");
    // Math can be rendered as SVG, MathML, or fallback span
    bool hasMath = hasTag(html, "svg") || hasTag(html, "math") || 
                   html.find("x") != std::string::npos;
    EXPECT_TRUE(hasMath) << "Should have math content: " << html;
}

TEST_F(UnifiedPipelineTest, DisplayMath) {
    std::string html = renderUnified("$$E = mc^2$$");
    // Display math should have some container
    bool hasMath = hasTag(html, "svg") || hasTag(html, "math") || 
                   hasTag(html, "div") || hasTag(html, "span");
    EXPECT_TRUE(hasMath) << "Should have math container: " << html;
}

// ============================================================================
// Table Tests  
// ============================================================================

TEST_F(UnifiedPipelineTest, SimpleTable) {
    std::string html = renderUnified(
        "\\begin{tabular}{cc}\na & b \\\\\nc & d\n\\end{tabular}");
    EXPECT_TRUE(hasTag(html, "table")) << "Should have <table>: " << html;
    EXPECT_TRUE(hasTag(html, "tr")) << "Should have <tr>: " << html;
    EXPECT_TRUE(hasTag(html, "td")) << "Should have <td>: " << html;
}

// ============================================================================
// Combined Document Tests
// ============================================================================

TEST_F(UnifiedPipelineTest, DocumentWithSections) {
    std::string latex = R"(
\section{Introduction}
This is the intro.

\section{Methods}
These are methods.
)";
    std::string html = renderUnified(latex);
    EXPECT_TRUE(hasText(html, "Introduction")) << "Should have Introduction: " << html;
    EXPECT_TRUE(hasText(html, "Methods")) << "Should have Methods: " << html;
    EXPECT_TRUE(hasText(html, "This is the intro")) << "Should have intro text: " << html;
    EXPECT_TRUE(hasText(html, "These are methods")) << "Should have methods text: " << html;
}

TEST_F(UnifiedPipelineTest, DocumentWithList) {
    std::string latex = R"(
\section{Items}
\begin{itemize}
\item First item
\item Second item
\end{itemize}
)";
    std::string html = renderUnified(latex);
    EXPECT_TRUE(hasTag(html, "ul")) << "Should have <ul>: " << html;
    EXPECT_TRUE(hasText(html, "First item")) << "Should have 'First item': " << html;
    EXPECT_TRUE(hasText(html, "Second item")) << "Should have 'Second item': " << html;
}

TEST_F(UnifiedPipelineTest, MixedFormatting) {
    std::string latex = R"(\textbf{Bold} and \textit{italic} and \texttt{mono})";
    std::string html = renderUnified(latex);
    EXPECT_TRUE(hasTag(html, "strong")) << "Should have <strong>: " << html;
    EXPECT_TRUE(hasTag(html, "em")) << "Should have <em>: " << html;
    EXPECT_TRUE(hasTag(html, "code")) << "Should have <code>: " << html;
    EXPECT_TRUE(hasText(html, "Bold")) << "Should have 'Bold': " << html;
    EXPECT_TRUE(hasText(html, "italic")) << "Should have 'italic': " << html;
    EXPECT_TRUE(hasText(html, "mono")) << "Should have 'mono': " << html;
}

TEST_F(UnifiedPipelineTest, NestedFormatting) {
    std::string latex = R"(\textbf{\textit{Bold italic}})";
    std::string html = renderUnified(latex);
    EXPECT_TRUE(hasTag(html, "strong")) << "Should have <strong>: " << html;
    EXPECT_TRUE(hasTag(html, "em")) << "Should have <em>: " << html;
    EXPECT_TRUE(hasText(html, "Bold italic")) << "Should have text: " << html;
}

// ============================================================================
// Parameterized Tests for latex_js Fixtures
// Separated into BASELINE (passing) and EXTENDED (failing/in-progress) sets
// ============================================================================

// Baseline tests - these must all pass (23 fixture tests currently passing)
// Format: "filename_id" where filename is without .tex extension
static const std::set<std::string> BASELINE_FIXTURES = {
    // basic_test.tex - all passing
    "basic_test_1",
    "basic_test_2",
    // formatting.tex - all passing
    "formatting_1",
    "formatting_2",
    "formatting_3",
    "formatting_4",
    "formatting_5",
    "formatting_6",
    // symbols.tex - all passing
    "symbols_1",
    "symbols_2",
    "symbols_3",
    "symbols_4",  // predefined symbols
    // sectioning.tex - partial
    "sectioning_1",
    "sectioning_2",
    // text.tex - partial
    "text_1",
    "text_2",
    "text_3",    // noindent paragraphs
    "text_4",    // UTF-8, punctuation, TeX symbols
    "text_5",    // empty diacritics
    "text_6",
    "text_7",    // ligatures and ligature prevention
    "text_8",    // verbatim text
    "text_9",    // French quotes
    // environments.tex - partial
    "environments_1",
    "environments_2",  // itemize with parbreaks in items
    "environments_3",  // empty and consecutive itemize environments
    "environments_4",  // nested itemize
    "environments_5",  // nested itemize II
    "environments_6",  // custom item labels \item[label]
    "environments_7",  // enumerate with custom labels
    "environments_8",  // description lists
    "environments_9",  // quote, quotation, verse
    "environments_11",
    // whitespace.tex - partial
    "whitespace_1",   // \par command paragraph breaks
    "whitespace_2",   // \par and ~ handling
    "whitespace_3",
    "whitespace_4",   // linebreaks \unskip trailing space
    "whitespace_9",   // multiple newlines collapsing
    "whitespace_10",  // spaces between environments
    "whitespace_11",  // comment line joining (Supercal%ifragilist)
    "whitespace_12",  // comments preventing paragraph breaks
    "whitespace_14",
    "whitespace_15",
    "whitespace_16",
    "whitespace_18",
    "whitespace_19",
};

// Helper to generate fixture key for lookup
static std::string get_fixture_key(const LatexHtmlFixture& fixture) {
    std::string filename = fixture.filename;
    // Remove .tex extension if present
    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".tex") {
        filename = filename.substr(0, filename.size() - 4);
    }
    return filename + "_" + std::to_string(fixture.id);
}

// Check if fixture is in baseline set
static bool is_baseline_fixture(const LatexHtmlFixture& fixture) {
    return BASELINE_FIXTURES.count(get_fixture_key(fixture)) > 0;
}

// Normalize HTML for comparison
static std::string normalize_html(const std::string& s) {
    std::string result;
    bool in_whitespace = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!in_whitespace && !result.empty()) {
                result += ' ';
                in_whitespace = true;
            }
        } else {
            result += c;
            in_whitespace = false;
        }
    }
    // Trim trailing whitespace
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    // Remove whitespace between > and < (between tags)
    std::string final_result;
    for (size_t i = 0; i < result.size(); i++) {
        if (result[i] == ' ' && i > 0 && i + 1 < result.size() &&
            result[i - 1] == '>' && result[i + 1] == '<') {
            // skip this space
            continue;
        }
        final_result += result[i];
    }
    return final_result;
}

// ============================================================================
// BASELINE Test Suite - Must all pass, run as part of make test-latex-baseline
// ============================================================================

class BaselineFixtureTest : public UnifiedPipelineTest,
                            public ::testing::WithParamInterface<LatexHtmlFixture> {
};

TEST_P(BaselineFixtureTest, FixtureContent) {
    const LatexHtmlFixture& fixture = GetParam();
    
    if (fixture.skip_test) {
        GTEST_SKIP() << "Skipped: " << fixture.header;
    }
    
    // Use legacy mode for fixture comparison
    std::string html = renderLegacy(fixture.latex_source);
    
    std::string expected_normalized = normalize_html(fixture.expected_html);
    std::string actual_normalized = normalize_html(html);
    
    EXPECT_EQ(expected_normalized, actual_normalized)
        << "\n=== BASELINE Fixture: " << fixture.header << " ==="
        << "\n=== LaTeX Input ===\n" << fixture.latex_source
        << "\n=== Expected HTML ===\n" << fixture.expected_html
        << "\n=== Actual HTML ===\n" << html;
}

// ============================================================================
// EXTENDED Test Suite - In-progress tests, failures expected
// ============================================================================

class ExtendedFixtureTest : public UnifiedPipelineTest,
                            public ::testing::WithParamInterface<LatexHtmlFixture> {
};

TEST_P(ExtendedFixtureTest, FixtureContent) {
    const LatexHtmlFixture& fixture = GetParam();
    
    if (fixture.skip_test) {
        GTEST_SKIP() << "Skipped: " << fixture.header;
    }
    
    // Use legacy mode for fixture comparison
    std::string html = renderLegacy(fixture.latex_source);
    
    std::string expected_normalized = normalize_html(fixture.expected_html);
    std::string actual_normalized = normalize_html(html);
    
    EXPECT_EQ(expected_normalized, actual_normalized)
        << "\n=== EXTENDED Fixture: " << fixture.header << " ==="
        << "\n=== LaTeX Input ===\n" << fixture.latex_source
        << "\n=== Expected HTML ===\n" << fixture.expected_html
        << "\n=== Actual HTML ===\n" << html;
}

// ============================================================================
// Fixture Loaders
// ============================================================================

// Load all fixtures from directory
std::vector<LatexHtmlFixture> load_all_fixtures() {
    std::vector<LatexHtmlFixture> fixtures;
    std::string fixtures_dir = "test/latex_js/fixtures";
    
    // Include all fixture files
    std::set<std::string> test_files = {
        "basic_test.tex",
        "text.tex",
        "environments.tex",
        "sectioning.tex",
        "whitespace.tex",
        "formatting.tex",
        "symbols.tex"
    };
    
    if (!std::filesystem::exists(fixtures_dir)) {
        std::cerr << "Warning: Fixtures directory not found: " << fixtures_dir << std::endl;
        return fixtures;
    }
    
    FixtureLoader loader;
    std::vector<FixtureFile> fixture_files = loader.load_fixtures_directory(fixtures_dir);
    
    for (const auto& file : fixture_files) {
        if (test_files.find(file.filepath.substr(file.filepath.rfind('/') + 1)) != test_files.end()) {
            for (const auto& fixture : file.fixtures) {
                fixtures.push_back(fixture);
            }
        }
    }
    
    return fixtures;
}

// Load only baseline fixtures
std::vector<LatexHtmlFixture> load_baseline_fixtures() {
    std::vector<LatexHtmlFixture> all = load_all_fixtures();
    std::vector<LatexHtmlFixture> baseline;
    for (const auto& f : all) {
        if (is_baseline_fixture(f)) {
            baseline.push_back(f);
        }
    }
    std::cout << "Loaded " << baseline.size() << " BASELINE fixtures" << std::endl;
    return baseline;
}

// Load only extended (non-baseline) fixtures
std::vector<LatexHtmlFixture> load_extended_fixtures() {
    std::vector<LatexHtmlFixture> all = load_all_fixtures();
    std::vector<LatexHtmlFixture> extended;
    for (const auto& f : all) {
        if (!is_baseline_fixture(f)) {
            extended.push_back(f);
        }
    }
    std::cout << "Loaded " << extended.size() << " EXTENDED fixtures" << std::endl;
    return extended;
}

std::string generate_baseline_test_name(const ::testing::TestParamInfo<LatexHtmlFixture>& info) {
    std::string name = info.param.filename + "_" + std::to_string(info.param.id);
    std::replace_if(name.begin(), name.end(), [](char c) {
        return !std::isalnum(c);
    }, '_');
    return name;
}

std::string generate_extended_test_name(const ::testing::TestParamInfo<LatexHtmlFixture>& info) {
    std::string name = info.param.filename + "_" + std::to_string(info.param.id);
    std::replace_if(name.begin(), name.end(), [](char c) {
        return !std::isalnum(c);
    }, '_');
    return name;
}

// Register BASELINE test suite - these must all pass
INSTANTIATE_TEST_SUITE_P(
    LatexBaseline,
    BaselineFixtureTest,
    ::testing::ValuesIn(load_baseline_fixtures()),
    generate_baseline_test_name
);

// Register EXTENDED test suite - in-progress, failures expected
INSTANTIATE_TEST_SUITE_P(
    LatexExtended,
    ExtendedFixtureTest,
    ::testing::ValuesIn(load_extended_fixtures()),
    generate_extended_test_name
);

