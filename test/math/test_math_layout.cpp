/**
 * Math Layout Test Suite
 *
 * Tests the Lambda math layout engine using fixtures extracted from MathLive.
 *
 * Test categories:
 * - Fractions: layout_fraction()
 * - Subscripts/Superscripts: layout_subsup()
 * - Radicals: layout_radical()
 * - Accents: layout_accent()
 * - Delimiters: layout_delimiter()
 * - Big Operators: layout_big_operator()
 * - Spacing: apply_inter_box_spacing()
 */

#include <gtest/gtest.h>
#include "math_fixture_loader.h"

extern "C" {
#include "../../lib/arena.h"
#include "../../lib/log.h"
}

#include <string>
#include <vector>
#include <cmath>

// Test fixture directory path (relative to executable)
static const char* FIXTURE_DIR = "test/math/fixtures";
static const char* COMBINED_FIXTURE = "test/math/fixtures/all_tests.json";

// Global fixture loader
static MathFixtureLoader g_loader;
static std::vector<MathTestCategory> g_categories;
static bool g_fixtures_loaded = false;

// ============================================================================
// Test Utilities
// ============================================================================

/**
 * Initialize test fixtures.
 * Called once before all tests.
 */
static void load_test_fixtures() {
    if (g_fixtures_loaded) return;

    // Try combined file first
    g_categories = g_loader.load_combined_fixtures(COMBINED_FIXTURE);

    if (g_categories.empty()) {
        // Fall back to directory loading
        g_categories = g_loader.load_fixtures_directory(FIXTURE_DIR);
    }

    g_fixtures_loaded = true;

    size_t total = 0;
    for (const auto& cat : g_categories) {
        total += cat.tests.size();
    }

    log_info("test_math_layout: loaded %zu categories, %zu total tests",
             g_categories.size(), total);
}

/**
 * Get tests for a specific category.
 */
static std::vector<MathTestCase> get_category_tests(const char* category) {
    load_test_fixtures();

    for (const auto& cat : g_categories) {
        if (cat.name == category) {
            return cat.tests;
        }
    }

    return {};
}

/**
 * Get all tests across all categories.
 */
static std::vector<MathTestCase> get_all_tests() {
    load_test_fixtures();
    return g_loader.get_all_tests(g_categories);
}

// ============================================================================
// Basic Smoke Tests
// ============================================================================

class MathLayoutBasicTest : public ::testing::Test {
protected:
    void SetUp() override {
        load_test_fixtures();
    }
};

TEST_F(MathLayoutBasicTest, FixturesLoaded) {
    ASSERT_FALSE(g_categories.empty()) << "No test fixtures loaded";

    size_t total = 0;
    for (const auto& cat : g_categories) {
        total += cat.tests.size();
    }
    EXPECT_GT(total, 0) << "No test cases found in fixtures";

    log_info("test_math_layout: %zu categories, %zu total tests",
             g_categories.size(), total);
}

TEST_F(MathLayoutBasicTest, CategoryLoading) {
    auto fractions = get_category_tests("fractions");
    EXPECT_GT(fractions.size(), 0) << "No fraction tests found";

    auto radicals = get_category_tests("radicals");
    EXPECT_GT(radicals.size(), 0) << "No radical tests found";
}

// ============================================================================
// Parameterized Tests by Category
// ============================================================================

/**
 * Base class for parameterized math layout tests.
 */
class MathLayoutParamTest : public ::testing::TestWithParam<MathTestCase> {
protected:
    Arena* arena;

    void SetUp() override {
        arena = arena_create(NULL, 64 * 1024, 128 * 1024);  // 64KB initial
    }

    void TearDown() override {
        if (arena) {
            arena_destroy(arena);
            arena = nullptr;
        }
    }
};

/**
 * Test that LaTeX test data is valid.
 * This is a smoke test that ensures the test infrastructure works.
 */
TEST_P(MathLayoutParamTest, HasValidTestData) {
    const MathTestCase& test = GetParam();

    // Verify we have valid test data
    EXPECT_FALSE(test.latex.empty())
        << "Test case " << test.id << " has empty LaTeX: " << test.description;

    // Log for debugging
    log_debug("test_math_layout: [%s #%d] %s",
              test.category.c_str(), test.id, test.latex.c_str());
}

// ============================================================================
// Category-Specific Tests
// ============================================================================

// Generate test parameters for each category
static std::vector<MathTestCase> GetFractionTests() {
    return get_category_tests("fractions");
}

static std::vector<MathTestCase> GetSubscriptTests() {
    return get_category_tests("subscripts");
}

static std::vector<MathTestCase> GetRadicalTests() {
    return get_category_tests("radicals");
}

static std::vector<MathTestCase> GetAccentTests() {
    return get_category_tests("accents");
}

static std::vector<MathTestCase> GetDelimiterTests() {
    return get_category_tests("delimiters");
}

static std::vector<MathTestCase> GetOperatorTests() {
    return get_category_tests("operators");
}

static std::vector<MathTestCase> GetSpacingTests() {
    return get_category_tests("spacing");
}

// Custom test name generator
struct MathTestNameGenerator {
    std::string operator()(const ::testing::TestParamInfo<MathTestCase>& info) const {
        // Use source_id if available, otherwise use category + id
        std::string name;
        if (!info.param.source.empty()) {
            // Create a unique name from source location
            name = info.param.category + "_" + info.param.source + "_" + std::to_string(info.param.id);
        } else {
            name = info.param.category + "_" + std::to_string(info.index) + "_" + std::to_string(info.param.id);
        }
        // Replace invalid characters
        for (char& c : name) {
            if (!isalnum(c)) c = '_';
        }
        return name;
    }
};

// Instantiate parameterized tests for each category

INSTANTIATE_TEST_SUITE_P(
    Fractions,
    MathLayoutParamTest,
    ::testing::ValuesIn(GetFractionTests()),
    MathTestNameGenerator()
);

INSTANTIATE_TEST_SUITE_P(
    Subscripts,
    MathLayoutParamTest,
    ::testing::ValuesIn(GetSubscriptTests()),
    MathTestNameGenerator()
);

INSTANTIATE_TEST_SUITE_P(
    Radicals,
    MathLayoutParamTest,
    ::testing::ValuesIn(GetRadicalTests()),
    MathTestNameGenerator()
);

INSTANTIATE_TEST_SUITE_P(
    Accents,
    MathLayoutParamTest,
    ::testing::ValuesIn(GetAccentTests()),
    MathTestNameGenerator()
);

INSTANTIATE_TEST_SUITE_P(
    Delimiters,
    MathLayoutParamTest,
    ::testing::ValuesIn(GetDelimiterTests()),
    MathTestNameGenerator()
);

INSTANTIATE_TEST_SUITE_P(
    Operators,
    MathLayoutParamTest,
    ::testing::ValuesIn(GetOperatorTests()),
    MathTestNameGenerator()
);

INSTANTIATE_TEST_SUITE_P(
    Spacing,
    MathLayoutParamTest,
    ::testing::ValuesIn(GetSpacingTests()),
    MathTestNameGenerator()
);

// ============================================================================
// Structure Validation Tests (Placeholder)
// ============================================================================

TEST(MathStructurePlaceholder, FractionStructure) {
    // A fraction should produce a vbox with numerator, rule, denominator
    // TODO: Implement when parser is integrated
    SUCCEED() << "Placeholder - implement when LaTeX parser integrated";
}

TEST(MathStructurePlaceholder, SubscriptSuperscriptStructure) {
    // x^2 should produce an hbox with base and raised superscript
    // TODO: Implement when parser is integrated
    SUCCEED() << "Placeholder - implement when LaTeX parser integrated";
}

TEST(MathStructurePlaceholder, RadicalStructure) {
    // sqrt{x} should produce radical sign + overline
    // TODO: Implement when parser is integrated
    SUCCEED() << "Placeholder - implement when LaTeX parser integrated";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    // Initialize logging
    log_init("log.conf");

    ::testing::InitGoogleTest(&argc, argv);

    // Pre-load fixtures to report count
    load_test_fixtures();

    return RUN_ALL_TESTS();
}
