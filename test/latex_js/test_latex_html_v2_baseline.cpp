#include <gtest/gtest.h>
#include "fixture_loader.h"
#include "html_comparison.h"
#include "../../lambda/input/input.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/url.h"
#include "../../lib/log.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <set>
#include <map>

// Forward declaration for V2 formatter (C linkage)
extern "C" {
    Item format_latex_html_v2_c(Input* input, int text_mode);
}

class LatexHtmlV2FixtureTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging
        log_init(NULL);

        // Initialize memory pool
        pool = pool_create();
        ASSERT_NE(pool, nullptr);

        // Initialize HTML comparator
        comparator.set_ignore_whitespace(true);
        comparator.set_normalize_attributes(true);
        comparator.set_case_sensitive(false);
    }

    void TearDown() override {
        // Cleanup memory pool
        if (pool) {
            pool_destroy(pool);
        }
    }

    // Helper function to run a single fixture test with V2 formatter
    void run_fixture_test(const LatexHtmlFixture& fixture) {
        try {
            // Create URL for the source (can be null since it's just source text)
            Url* url = nullptr;

            // Create String for type specification
            String* type_str = nullptr;
            {
                const char* type_cstr = "latex-ts";  // Use Tree-sitter parser
                type_str = (String*)malloc(sizeof(String) + strlen(type_cstr) + 1);
                type_str->len = strlen(type_cstr);
                type_str->ref_cnt = 0;
                strcpy(type_str->chars, type_cstr);
            }

            // Create String for flavor (can be null)
            String* flavor_str = nullptr;

            // Parse LaTeX source using Tree-sitter parser
            Input* input = input_from_source(fixture.latex_source.c_str(), url, type_str, flavor_str);
            ASSERT_NE(input, nullptr) << "Input creation should succeed for fixture '" << fixture.header << "'";

            // Clean up type string
            if (type_str) free(type_str);

            // Generate HTML using V2 formatter (text_mode=1 for HTML string)
            Item result_item = format_latex_html_v2_c(input, 1);  // 1 = text mode → HTML string

            ASSERT_EQ(get_type_id(result_item), LMD_TYPE_STRING)
                << "V2 formatter should return string in text mode for fixture '" << fixture.header << "'";

            String* html_result = (String*)result_item.string_ptr;
            if (!html_result || html_result->len == 0) {
                FAIL() << "V2 formatter produced no result for fixture '" << fixture.header << "'";
                return;
            }

            std::string actual_html(html_result->chars, html_result->len);

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
        } catch (...) {
            FAIL() << "Unknown exception in fixture '" << fixture.header << "'";
        }
    }

    // Generate detailed failure report
    std::string generate_failure_report(const LatexHtmlFixture& fixture,
                                       const std::string& actual_html,
                                       const std::vector<HtmlDifference>& differences) {
        std::stringstream report;
        report << "\n=== V2 FIXTURE TEST FAILURE ===\n";
        report << "File: " << fixture.filename << "\n";
        report << "Test: " << fixture.header << " (ID: " << fixture.id << ")\n\n";

        report << "LaTeX Source:\n";
        report << "-------------\n";
        report << fixture.latex_source << "\n\n";

        report << "Expected HTML:\n";
        report << "--------------\n";
        report << fixture.expected_html << "\n\n";

        report << "Actual HTML (V2):\n";
        report << "-----------------\n";
        report << actual_html << "\n\n";

        report << "Differences:\n";
        report << "------------\n";
        report << comparator.get_comparison_report() << "\n";

        return report.str();
    }

    Pool* pool;
    HtmlComparator comparator;
};

// Test fixture loading functionality
TEST_F(LatexHtmlV2FixtureTest, FixtureLoaderBasic) {
    FixtureLoader loader;

    // Test with a simple fixture string
    std::string test_content = R"(
** simple test
.
Hello world
.
<div class="body"><p>Hello world</p></div>
.
)";

    std::vector<LatexHtmlFixture> fixtures = loader.parse_fixtures(test_content, "test.tex");

    ASSERT_EQ(fixtures.size(), 1);
    EXPECT_EQ(fixtures[0].header, "simple test");
    EXPECT_EQ(fixtures[0].latex_source, "Hello world");
    EXPECT_TRUE(fixtures[0].expected_html.find("<p>Hello world</p>") != std::string::npos);
}

// Parameterized test for running all fixtures from a directory with V2
class LatexHtmlV2FixtureParameterizedTest : public LatexHtmlV2FixtureTest,
                                           public ::testing::WithParamInterface<LatexHtmlFixture> {
};

TEST_P(LatexHtmlV2FixtureParameterizedTest, RunFixture) {
    const LatexHtmlFixture& fixture = GetParam();

    // Skip tests marked with "!" (original mechanism)
    if (fixture.skip_test) {
        GTEST_SKIP() << "Test marked as skipped: " << fixture.header;
    }

    run_fixture_test(fixture);
}

// Load baseline fixtures only (must pass 100%) - Same as V1 baseline
std::vector<LatexHtmlFixture> load_v2_baseline_fixtures() {
    std::vector<LatexHtmlFixture> baseline_fixtures;

    std::string fixtures_dir = "test/latex/fixtures";

    // Baseline test files that must always pass 100%
    // Using original latex-js fixtures
    std::set<std::string> baseline_files = {
        "basic_test.tex",
        "text.tex",
        "environments.tex",
        "sectioning.tex",
        "whitespace.tex",
        // New baseline files
        "counters.tex",
        "formatting.tex",
        "preamble.tex",
        "spacing.tex",
        "symbols.tex",
        "macros.tex",
        "fonts.tex",
        // Additional files - full test suite
        "boxes.tex",
        "groups.tex",
        "label-ref.tex",
        "layout-marginpar.tex"
    };

    // Tests to exclude from V2 baseline (moved to extended test suite)
    // These are tests that currently fail and need work
    std::map<std::string, std::set<int>> excluded_test_ids = {
        // boxes.tex: All tests pass! tex_4 fixed with lazy paragraph opening
        // counters.tex: All tests pass! Test 1 (counter arithmetic), Test 2 (moved from extended)
        // environments.tex: All tests pass! test 14 (comment env) fixed with special handling
        // fonts.tex: All tests pass! Tests 7, 8 pass with font class wrapper for breakspace
        // groups.tex: All tests pass! Tests 2, 3 fixed (error brack_group + paragraph_break in groups)
        // label-ref.tex: All tests pass! Test 7 moved from extended (list item paragraph fix)
        // layout-marginpar.tex: All tests pass! (marginpar implementation complete)
        // macros.tex: Tests 4, 5, 6 pass! (sibling lookahead + parbreak->br + cstring() fix)
        // sectioning.tex: All tests pass! test 3 fixed with inStyledSpan() check
        {"spacing.tex", {1}}  // fixture needs Unicode thin space update (U+2009 vs ASCII space)
        // symbols.tex test 2 PASSES (^^ unicode notation) - removed from exclusions
        // text.tex test 10 now passes (paragraph alignment buffering fix)
        // whitespace.tex: test 5 passes (mbox ZWS at start), tests 7-8 skipped (match latex-js)
        // All whitespace tests now in baseline (7-8 have ! prefix in fixture - aspirational tests)
    };

    if (!std::filesystem::exists(fixtures_dir)) {
        std::cerr << "Warning: Fixtures directory not found: " << fixtures_dir << std::endl;
        return baseline_fixtures;
    }

    FixtureLoader loader;
    std::vector<FixtureFile> fixture_files = loader.load_fixtures_directory(fixtures_dir);

    for (const auto& file : fixture_files) {
        for (const auto& fixture : file.fixtures) {
            // Check if this fixture belongs to a baseline file
            if (baseline_files.find(fixture.filename) != baseline_files.end()) {
                // Skip tests that are in the excluded list for this file
                bool exclude = false;

                // Check ID-based exclusion
                auto excluded_id_it = excluded_test_ids.find(fixture.filename);
                if (excluded_id_it != excluded_test_ids.end() &&
                    excluded_id_it->second.find(fixture.id) != excluded_id_it->second.end()) {
                    exclude = true;
                }

                if (!exclude) {
                    baseline_fixtures.push_back(fixture);
                }
            }
        }
    }

    std::cout << "Loaded " << baseline_fixtures.size() << " V2 baseline fixtures from "
              << baseline_files.size() << " files" << std::endl;

    return baseline_fixtures;
}

// Generate test names for parameterized tests
std::string generate_v2_test_name(const ::testing::TestParamInfo<LatexHtmlFixture>& info) {
    std::string name = info.param.filename + "_" + std::to_string(info.param.id);

    // Replace invalid characters for test names
    std::replace_if(name.begin(), name.end(), [](char c) {
        return !std::isalnum(c);
    }, '_');

    return name;
}

// Register V2 BASELINE parameterized tests (must always pass 100%)
INSTANTIATE_TEST_SUITE_P(
    V2BaselineFixtures,
    LatexHtmlV2FixtureParameterizedTest,
    ::testing::ValuesIn(load_v2_baseline_fixtures()),
    generate_v2_test_name
);

// Individual test suites for specific functionality with V2
TEST_F(LatexHtmlV2FixtureTest, BasicTextFormatting) {
    LatexHtmlFixture fixture;
    fixture.id = 1;
    fixture.header = "basic text formatting";
    fixture.latex_source = R"(\textbf{Bold text} and \textit{italic text})";
    fixture.expected_html = R"(<div class="body"><p><span class="bf">Bold text</span> and <span class="it">italic text</span></p></div>)";
    fixture.skip_test = false;

    run_fixture_test(fixture);
}

TEST_F(LatexHtmlV2FixtureTest, SectioningCommands) {
    LatexHtmlFixture fixture;
    fixture.id = 2;
    fixture.header = "sectioning commands";
    fixture.latex_source = R"(\section{Introduction}
This is the introduction.
\subsection{Background}
This is background information.)";
    fixture.expected_html = "<div class=\"body\">\n"
        "<h2 id=\"sec-1\">1\xE2\x80\x83Introduction</h2>\n"
        "<p>This is the introduction.</p>\n"
        "<h3 id=\"sec-2\">1.1\xE2""\x80""\x83""Background</h3>\n"
        "<p>This is background information.</p>\n"
        "</div>";
    fixture.skip_test = false;

    run_fixture_test(fixture);
}

TEST_F(LatexHtmlV2FixtureTest, ListEnvironments) {
    LatexHtmlFixture fixture;
    fixture.id = 3;
    fixture.header = "list environments";
    fixture.latex_source = R"(\begin{itemize}
\item First item
\item Second item
\end{itemize})";
    fixture.expected_html = R"(<div class="body">
<ul class="list">
<li><span class="itemlabel"><span class="hbox llap">•</span></span><p>First item</p></li>
<li><span class="itemlabel"><span class="hbox llap">•</span></span><p>Second item</p></li>
</ul>
</div>)";
    fixture.skip_test = false;

    run_fixture_test(fixture);
}

// =============================================================================
// Main Entry Point - Using GTest default main
// =============================================================================
