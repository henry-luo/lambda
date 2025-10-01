#include <gtest/gtest.h>
#include "fixture_loader.h"
#include "html_comparison.h"
#include "../../lambda/format/format-latex-html.h"
#include "../../lambda/input/input-latex.h"
#include "../../lib/stringbuf.h"
#include "../../lib/mem-pool/include/mem_pool.h"
#include <filesystem>
#include <iostream>

class LatexHtmlFixtureTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize memory pool
        pool = variable_mem_pool_create();
        ASSERT_NE(pool, nullptr);
        
        // Initialize string buffers
        stringbuf_init(&html_buf);
        stringbuf_init(&css_buf);
        
        // Initialize HTML comparator
        comparator.set_ignore_whitespace(true);
        comparator.set_normalize_attributes(true);
        comparator.set_case_sensitive(false);
    }
    
    void TearDown() override {
        // Cleanup string buffers
        stringbuf_destroy(&html_buf);
        stringbuf_destroy(&css_buf);
        
        // Cleanup memory pool
        if (pool) {
            variable_mem_pool_destroy(pool);
        }
    }
    
    // Helper function to run a single fixture test
    void run_fixture_test(const LatexHtmlFixture& fixture) {
        // Clear buffers
        stringbuf_clear(&html_buf);
        stringbuf_clear(&css_buf);
        
        try {
            // Parse LaTeX source using Lambda's parser
            Item latex_ast = parse_latex_string(fixture.latex_source.c_str(), pool);
            
            // Generate HTML using Lambda's formatter
            format_latex_to_html(&html_buf, &css_buf, latex_ast, pool);
            
            // Get generated HTML
            String* html_result = stringbuf_to_string(&html_buf);
            std::string actual_html(html_result->chars, html_result->length);
            
            // Compare with expected output
            std::vector<HtmlDifference> differences;
            bool matches = comparator.compare_html_detailed(fixture.expected_html, actual_html, differences);
            
            if (!matches) {
                // Generate detailed failure report
                std::string report = generate_failure_report(fixture, actual_html, differences);
                FAIL() << report;
            }
            
        } catch (const std::exception& e) {
            FAIL() << "Exception in fixture '" << fixture.header << "': " << e.what();
        }
    }
    
    // Generate detailed failure report
    std::string generate_failure_report(const LatexHtmlFixture& fixture, 
                                       const std::string& actual_html,
                                       const std::vector<HtmlDifference>& differences) {
        std::stringstream report;
        report << "\n=== FIXTURE TEST FAILURE ===\n";
        report << "File: " << fixture.filename << "\n";
        report << "Test: " << fixture.header << " (ID: " << fixture.id << ")\n\n";
        
        report << "LaTeX Source:\n";
        report << "-------------\n";
        report << fixture.latex_source << "\n\n";
        
        report << "Expected HTML:\n";
        report << "--------------\n";
        report << fixture.expected_html << "\n\n";
        
        report << "Actual HTML:\n";
        report << "------------\n";
        report << actual_html << "\n\n";
        
        report << "Differences:\n";
        report << "------------\n";
        report << comparator.get_comparison_report() << "\n";
        
        return report.str();
    }
    
    VariableMemPool* pool;
    StringBuf html_buf;
    StringBuf css_buf;
    HtmlComparator comparator;
};

// Test fixture loading functionality
TEST_F(LatexHtmlFixtureTest, FixtureLoaderBasic) {
    FixtureLoader loader;
    
    // Test with a simple fixture string
    std::string test_content = R"(
** simple test
.
Hello world
.
<div class="latex-document"><p>Hello world</p></div>
.
)";
    
    std::vector<LatexHtmlFixture> fixtures = loader.parse_fixtures(test_content, "test.tex");
    
    ASSERT_EQ(fixtures.size(), 1);
    EXPECT_EQ(fixtures[0].header, "simple test");
    EXPECT_EQ(fixtures[0].latex_source, "Hello world");
    EXPECT_TRUE(fixtures[0].expected_html.find("<p>Hello world</p>") != std::string::npos);
}

// Test HTML comparison functionality
TEST_F(LatexHtmlFixtureTest, HtmlComparatorBasic) {
    HtmlComparator comp;
    
    // Test exact match
    EXPECT_TRUE(comp.compare_html("<p>Hello</p>", "<p>Hello</p>"));
    
    // Test whitespace normalization
    EXPECT_TRUE(comp.compare_html("<p>Hello</p>", "<p> Hello </p>"));
    EXPECT_TRUE(comp.compare_html("<p>Hello</p>", "<p>\n  Hello\n</p>"));
    
    // Test case insensitivity
    EXPECT_TRUE(comp.compare_html("<P>Hello</P>", "<p>hello</p>"));
    
    // Test mismatch
    EXPECT_FALSE(comp.compare_html("<p>Hello</p>", "<p>World</p>"));
}

// Parameterized test for running all fixtures from a directory
class LatexHtmlFixtureParameterizedTest : public LatexHtmlFixtureTest,
                                         public ::testing::WithParamInterface<LatexHtmlFixture> {
};

TEST_P(LatexHtmlFixtureParameterizedTest, RunFixture) {
    const LatexHtmlFixture& fixture = GetParam();
    
    // Skip tests marked with "!"
    if (fixture.skip_test) {
        GTEST_SKIP() << "Test marked as skipped: " << fixture.header;
    }
    
    run_fixture_test(fixture);
}

// Load and register all fixtures
std::vector<LatexHtmlFixture> load_all_fixtures() {
    std::vector<LatexHtmlFixture> all_fixtures;
    
    std::string fixtures_dir = "test/latex_html/fixtures";
    
    // Check if fixtures directory exists
    if (!std::filesystem::exists(fixtures_dir)) {
        std::cerr << "Warning: Fixtures directory not found: " << fixtures_dir << std::endl;
        return all_fixtures;
    }
    
    FixtureLoader loader;
    std::vector<FixtureFile> fixture_files = loader.load_fixtures_directory(fixtures_dir);
    
    for (const auto& file : fixture_files) {
        for (const auto& fixture : file.fixtures) {
            all_fixtures.push_back(fixture);
        }
    }
    
    std::cout << "Loaded " << all_fixtures.size() << " fixtures from " 
              << fixture_files.size() << " files" << std::endl;
    
    return all_fixtures;
}

// Generate test names for parameterized tests
std::string generate_test_name(const ::testing::TestParamInfo<LatexHtmlFixture>& info) {
    std::string name = info.param.filename + "_" + std::to_string(info.param.id);
    
    // Replace invalid characters for test names
    std::replace_if(name.begin(), name.end(), [](char c) {
        return !std::isalnum(c);
    }, '_');
    
    return name;
}

// Register parameterized tests
INSTANTIATE_TEST_SUITE_P(
    AllFixtures,
    LatexHtmlFixtureParameterizedTest,
    ::testing::ValuesIn(load_all_fixtures()),
    generate_test_name
);

// Individual test suites for specific functionality
TEST_F(LatexHtmlFixtureTest, BasicTextFormatting) {
    LatexHtmlFixture fixture;
    fixture.id = 1;
    fixture.header = "basic text formatting";
    fixture.latex_source = R"(\textbf{Bold text} and \textit{italic text})";
    fixture.expected_html = R"(<div class="latex-document"><p><span class="latex-textbf">Bold text</span> and <span class="latex-textit">italic text</span></p></div>)";
    fixture.skip_test = false;
    
    run_fixture_test(fixture);
}

TEST_F(LatexHtmlFixtureTest, SectioningCommands) {
    LatexHtmlFixture fixture;
    fixture.id = 2;
    fixture.header = "sectioning commands";
    fixture.latex_source = R"(\section{Introduction}
This is the introduction.
\subsection{Background}
This is background information.)";
    fixture.expected_html = R"(<div class="latex-document">
<div class="latex-section">Introduction</div>
<p>This is the introduction.</p>
<div class="latex-subsection">Background</div>
<p>This is background information.</p>
</div>)";
    fixture.skip_test = false;
    
    run_fixture_test(fixture);
}

TEST_F(LatexHtmlFixtureTest, ListEnvironments) {
    LatexHtmlFixture fixture;
    fixture.id = 3;
    fixture.header = "list environments";
    fixture.latex_source = R"(\begin{itemize}
\item First item
\item Second item
\end{itemize})";
    fixture.expected_html = R"(<div class="latex-document">
<ul class="latex-itemize">
<li>First item</li>
<li>Second item</li>
</ul>
</div>)";
    fixture.skip_test = false;
    
    run_fixture_test(fixture);
}
