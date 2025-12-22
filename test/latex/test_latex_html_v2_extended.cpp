#include <gtest/gtest.h>
#include "fixture_loader.h"
#include "html_comparison.h"
#include "../../lambda/input/input.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/url.h"
#include "../../lib/log.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <set>
#include <map>

// Forward declaration for V2 formatter (C linkage)
extern "C" {
    Item format_latex_html_v2_c(Input* input, int text_mode);
}

class LatexHtmlV2ExtendedTest : public ::testing::Test {
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
        report << "\n=== V2 EXTENDED TEST FAILURE ===\n";
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

// Parameterized test for running all extended fixtures
class LatexHtmlV2ExtendedParameterizedTest : public LatexHtmlV2ExtendedTest,
                                             public ::testing::WithParamInterface<LatexHtmlFixture> {
};

TEST_P(LatexHtmlV2ExtendedParameterizedTest, RunFixture) {
    const LatexHtmlFixture& fixture = GetParam();

    // Skip tests marked with "!" (original mechanism)
    if (fixture.skip_test) {
        GTEST_SKIP() << "Test marked as skipped: " << fixture.header;
    }

    run_fixture_test(fixture);
}

// Load extended fixtures (tests that currently fail but should be fixed)
std::vector<LatexHtmlFixture> load_v2_extended_fixtures() {
    std::vector<LatexHtmlFixture> extended_fixtures;

    std::string fixtures_dir = "test/latex/fixtures";

    // Extended test files - tests that currently fail but are work-in-progress
    std::set<std::string> extended_files = {
        "basic_text.tex",
        "boxes.tex",
        "counters.tex",
        "environments.tex",
        "fonts.tex",
        "groups.tex",
        "label-ref.tex",
        "layout-marginpar.tex",
        "macros.tex",
        "sectioning.tex",
        "symbols.tex",
        "text.tex",
        "whitespace.tex"
    };

    // Specific test IDs to include in extended (failing tests only)
    std::map<std::string, std::set<int>> extended_test_ids = {
        {"basic_text.tex", {4}},  // test 4 expects no ZWS (inconsistent with text.tex); test 6 PASSES (moved to baseline)
        {"boxes.tex", {4}},  // boxes_tex_2, 3, 5 moved to baseline; tex_4 has \noindent issue
        // counters.tex test 2 moved to baseline (PASSES)
        {"environments.tex", {7, 10, 14}},
        {"fonts.tex", {6, 7, 8}},
        {"groups.tex", {2, 3}},
        {"label-ref.tex", {2, 3, 6, 7}},  // test 1 moved to baseline (PASSES)
        {"layout-marginpar.tex", {1, 2, 3}},
        {"macros.tex", {2, 4, 5, 6}},
        {"sectioning.tex", {3}},
        // symbols.tex test 2 PASSES (^^ unicode notation) - moved to baseline
        {"text.tex", {4, 6, 8, 10}},  // test 5, 7 moved to baseline (PASS)
        {"whitespace.tex", {5, 6, 7, 8, 12, 21}}  // test 13, 15 moved to baseline (PASS)
    };

    if (!std::filesystem::exists(fixtures_dir)) {
        std::cerr << "Warning: Fixtures directory not found: " << fixtures_dir << std::endl;
        return extended_fixtures;
    }

    FixtureLoader loader;
    std::vector<FixtureFile> fixture_files = loader.load_fixtures_directory(fixtures_dir);

    for (const auto& file : fixture_files) {
        for (const auto& fixture : file.fixtures) {
            // Check if this fixture is in the extended test list
            auto extended_id_it = extended_test_ids.find(fixture.filename);
            if (extended_id_it != extended_test_ids.end() &&
                extended_id_it->second.find(fixture.id) != extended_id_it->second.end()) {
                extended_fixtures.push_back(fixture);
            }
        }
    }

    std::cout << "Loaded " << extended_fixtures.size() << " V2 extended (failing) fixtures" << std::endl;

    return extended_fixtures;
}

// Generate test names for parameterized tests
std::string generate_v2_extended_test_name(const ::testing::TestParamInfo<LatexHtmlFixture>& info) {
    std::string name = info.param.filename + "_" + std::to_string(info.param.id);

    // Replace invalid characters for test names
    std::replace_if(name.begin(), name.end(), [](char c) {
        return !std::isalnum(c);
    }, '_');

    return name;
}

// Register V2 EXTENDED parameterized tests (tests that currently fail)
INSTANTIATE_TEST_SUITE_P(
    V2ExtendedFixtures,
    LatexHtmlV2ExtendedParameterizedTest,
    ::testing::ValuesIn(load_v2_extended_fixtures()),
    generate_v2_extended_test_name
);

// =============================================================================
// Main Entry Point - Using GTest default main
// =============================================================================
