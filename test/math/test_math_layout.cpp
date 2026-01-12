/**
 * Math Layout Test Suite
 *
 * Tests the TeX math pipeline by:
 * 1. Loading LaTeX math test cases from fixtures
 * 2. Parsing and typesetting using typeset_latex_math()
 * 3. Generating DVI output
 * 4. Comparing with reference DVI files (glyph sequence comparison)
 *
 * Reference files are in test/math/reference/ and can be generated
 * by running with --generate-references flag.
 */

#include <gtest/gtest.h>
#include "math_fixture_loader.h"

#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lambda/tex/tex_math_bridge.hpp"
#include "lambda/tex/tex_dvi_out.hpp"
#include "lambda/tex/dvi_parser.hpp"

extern "C" {
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include "lib/file.h"
}

#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

using namespace tex;
using namespace tex::dvi;

// ============================================================================
// Configuration
// ============================================================================

// Test fixture directory path (relative to executable)
static const char* FIXTURE_DIR = "test/math/fixtures";
static const char* COMBINED_FIXTURE = "test/math/fixtures/all_tests.json";
static const char* REFERENCE_DIR = "test/math/reference";

// Whether to generate reference files instead of comparing
static bool g_generate_references = false;

// Default font size for math typesetting (in points)
static const float DEFAULT_FONT_SIZE = 10.0f;

// ============================================================================
// Global Fixture State
// ============================================================================

static MathFixtureLoader g_loader;
static std::vector<MathTestCategory> g_categories;
static bool g_fixtures_loaded = false;

/**
 * Initialize test fixtures.
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
            // Copy tests and populate index field
            std::vector<MathTestCase> result = cat.tests;
            for (size_t i = 0; i < result.size(); i++) {
                result[i].index = (int)i;
            }
            return result;
        }
    }

    return {};
}

// ============================================================================
// DVI Utilities
// ============================================================================

/**
 * Extract glyph sequence from a DVI page.
 * Returns a string of codepoints for comparison.
 */
static std::string extract_glyph_sequence(const DVIPage* page) {
    std::string seq;
    for (int i = 0; i < page->glyph_count; i++) {
        int32_t cp = page->glyphs[i].codepoint;
        if (cp > 0 && cp < 256) {
            seq += (char)cp;
        } else {
            // Encode high codepoints as [XXXX]
            char buf[16];
            snprintf(buf, sizeof(buf), "[%04X]", cp);
            seq += buf;
        }
    }
    return seq;
}

/**
 * Generate a unique filename for a test case's reference DVI.
 * Uses index (0-based) to match test names.
 */
static std::string get_reference_path(const MathTestCase& test) {
    // Create filename: category_index.dvi
    char buf[256];
    snprintf(buf, sizeof(buf), "%s/%s_%d.dvi",
             REFERENCE_DIR, test.category.c_str(), test.index);
    return buf;
}

/**
 * Check if a file exists.
 */
static bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// ============================================================================
// Math Typesetting to DVI
// ============================================================================

/**
 * Typeset a LaTeX math string to a DVI file.
 *
 * Creates a minimal DVI with a single page containing the math.
 * The math is centered on a standard page for consistency.
 */
static bool typeset_math_to_dvi(const char* latex_math, const char* output_path,
                                 Arena* arena, TFMFontManager* fonts) {
    // Unescape the LaTeX string (fixtures have double backslashes)
    std::string unescaped;
    const char* p = latex_math;
    while (*p) {
        if (p[0] == '\\' && p[1] == '\\') {
            unescaped += '\\';
            p += 2;
        } else {
            unescaped += *p;
            p++;
        }
    }

    log_debug("test_math: typesetting '%s'", unescaped.c_str());

    // Create math context
    MathContext ctx = MathContext::create(arena, fonts, DEFAULT_FONT_SIZE);
    ctx.style = MathStyle::Display;  // Use display style for clearer output

    // Parse and typeset the math
    TexNode* math_hbox = typeset_latex_math(unescaped.c_str(), unescaped.size(), ctx);
    if (!math_hbox) {
        log_error("test_math: failed to typeset: %s", unescaped.c_str());
        return false;
    }

    log_debug("test_math: result width=%.2f height=%.2f depth=%.2f",
              math_hbox->width, math_hbox->height, math_hbox->depth);

    // Wrap math in a VBox for the page
    // Create a simple page structure: [glue][math_hbox][glue]
    TexNode* page_vlist = make_vlist(arena);

    // Add top space (1 inch = 72pt)
    TexNode* top_glue = make_glue(arena, Glue::fixed(72.0f));
    page_vlist->append_child(top_glue);

    // Create an HBox to center the math horizontally
    // Page width 468pt (6.5" text width), center the math
    float page_width = 468.0f;
    float left_margin = (page_width - math_hbox->width) / 2.0f;
    if (left_margin < 0) left_margin = 0;

    TexNode* hbox = make_hbox(arena);
    TexNode* left_glue = make_glue(arena, Glue::fixed(left_margin));
    hbox->append_child(left_glue);
    hbox->append_child(math_hbox);
    hbox->width = page_width;
    hbox->height = math_hbox->height;
    hbox->depth = math_hbox->depth;

    page_vlist->append_child(hbox);

    // Set page dimensions
    page_vlist->width = page_width;
    page_vlist->height = 72.0f + math_hbox->height + math_hbox->depth;
    page_vlist->depth = 0;

    // Write DVI
    DVIParams params = DVIParams::defaults();
    params.comment = "Lambda Math Test";

    return write_dvi_page(output_path, page_vlist, fonts, arena, params);
}

// ============================================================================
// Test Fixture Base Class
// ============================================================================

class MathDVITest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    TFMFontManager* fonts;
    char temp_dir[256];

    void SetUp() override {
        load_test_fixtures();

        pool = pool_create();
        arena = arena_create_default(pool);
        fonts = create_font_manager(arena);

        // Create temp directory for generated DVI files
        snprintf(temp_dir, sizeof(temp_dir), "/tmp/lambda_math_test_%d", getpid());
        mkdir(temp_dir, 0755);
    }

    void TearDown() override {
        if (arena) {
            arena_destroy(arena);
            arena = nullptr;
        }
        if (pool) {
            pool_destroy(pool);
            pool = nullptr;
        }
        fonts = nullptr;

        // Clean up temp directory
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s 2>/dev/null", temp_dir);
        system(cmd);
    }

    /**
     * Get path to a temp file.
     */
    std::string temp_file(const char* name) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s/%s", temp_dir, name);
        return buf;
    }

    /**
     * Test a single math expression.
     * Either generates reference or compares with existing reference.
     */
    ::testing::AssertionResult test_math_case(const MathTestCase& test) {
        std::string ref_path = get_reference_path(test);
        std::string out_path = temp_file((test.category + "_" + std::to_string(test.id) + ".dvi").c_str());

        // Typeset the math
        if (!typeset_math_to_dvi(test.latex.c_str(), out_path.c_str(), arena, fonts)) {
            return ::testing::AssertionFailure()
                << "Failed to typeset: " << test.latex
                << " (" << test.description << ")";
        }

        if (g_generate_references) {
            // Copy generated DVI to reference location
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", out_path.c_str(), ref_path.c_str());
            if (system(cmd) != 0) {
                return ::testing::AssertionFailure()
                    << "Failed to copy to reference: " << ref_path;
            }
            log_info("Generated reference: %s", ref_path.c_str());
            return ::testing::AssertionSuccess();
        }

        // Check if reference exists
        if (!file_exists(ref_path.c_str())) {
            return ::testing::AssertionFailure()
                << "Reference DVI not found: " << ref_path
                << "\nRun with --generate-references to create it";
        }

        // Parse both DVIs
        DVIParser ref_parser(arena);
        if (!ref_parser.parse_file(ref_path.c_str())) {
            return ::testing::AssertionFailure()
                << "Failed to parse reference DVI: " << ref_path
                << " (" << ref_parser.error() << ")";
        }

        DVIParser out_parser(arena);
        if (!out_parser.parse_file(out_path.c_str())) {
            return ::testing::AssertionFailure()
                << "Failed to parse output DVI: " << out_path
                << " (" << out_parser.error() << ")";
        }

        // Compare glyph sequences
        if (ref_parser.page_count() != out_parser.page_count()) {
            return ::testing::AssertionFailure()
                << "Page count mismatch: ref=" << ref_parser.page_count()
                << " out=" << out_parser.page_count();
        }

        for (int p = 0; p < ref_parser.page_count(); p++) {
            std::string ref_seq = extract_glyph_sequence(ref_parser.page(p));
            std::string out_seq = extract_glyph_sequence(out_parser.page(p));

            if (ref_seq != out_seq) {
                return ::testing::AssertionFailure()
                    << "Glyph sequence mismatch on page " << p << "\n"
                    << "  LaTeX: " << test.latex << "\n"
                    << "  Expected: " << ref_seq << "\n"
                    << "  Got: " << out_seq;
            }
        }

        return ::testing::AssertionSuccess();
    }
};

// ============================================================================
// Basic Smoke Tests
// ============================================================================

TEST_F(MathDVITest, FixturesLoaded) {
    ASSERT_FALSE(g_categories.empty()) << "No test fixtures loaded";

    size_t total = 0;
    for (const auto& cat : g_categories) {
        total += cat.tests.size();
    }
    EXPECT_GT(total, 0) << "No test cases found in fixtures";

    log_info("test_math_layout: %zu categories, %zu total tests",
             g_categories.size(), total);
}

TEST_F(MathDVITest, SimpleExpression) {
    // Test a basic expression directly
    MathTestCase test;
    test.id = 0;
    test.latex = "a+b";
    test.category = "basic";
    test.description = "Simple addition";

    std::string out_path = temp_file("simple_test.dvi");
    ASSERT_TRUE(typeset_math_to_dvi(test.latex.c_str(), out_path.c_str(), arena, fonts))
        << "Failed to typeset simple expression";

    // Parse and verify we got output
    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(out_path.c_str()))
        << "Failed to parse generated DVI: " << parser.error();

    EXPECT_EQ(parser.page_count(), 1);
    if (parser.page_count() > 0) {
        std::string seq = extract_glyph_sequence(parser.page(0));
        log_info("Simple expression glyphs: %s", seq.c_str());
        EXPECT_FALSE(seq.empty()) << "No glyphs in output";
    }
}

TEST_F(MathDVITest, FractionExpression) {
    MathTestCase test;
    test.id = 0;
    test.latex = "\\frac{a}{b}";
    test.category = "fractions";
    test.description = "Simple fraction";

    std::string out_path = temp_file("fraction_test.dvi");
    ASSERT_TRUE(typeset_math_to_dvi(test.latex.c_str(), out_path.c_str(), arena, fonts))
        << "Failed to typeset fraction";

    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(out_path.c_str()))
        << "Failed to parse generated DVI";

    EXPECT_EQ(parser.page_count(), 1);
    if (parser.page_count() > 0) {
        std::string seq = extract_glyph_sequence(parser.page(0));
        log_info("Fraction glyphs: %s", seq.c_str());
        // Should contain 'a' and 'b' at minimum
        EXPECT_NE(seq.find('a'), std::string::npos) << "Missing 'a' in output";
        EXPECT_NE(seq.find('b'), std::string::npos) << "Missing 'b' in output";
    }
}

// ============================================================================
// Parameterized Tests by Category
// ============================================================================

class MathLayoutParamTest : public MathDVITest,
                            public ::testing::WithParamInterface<MathTestCase> {
};

TEST_P(MathLayoutParamTest, TypesetsCorrectly) {
    const MathTestCase& test = GetParam();
    EXPECT_TRUE(test_math_case(test));
}

// Custom test name generator
struct MathTestNameGenerator {
    std::string operator()(const ::testing::TestParamInfo<MathTestCase>& info) const {
        // Use index to ensure uniqueness (category + index)
        std::string name = info.param.category + "_" + std::to_string(info.index);
        // Replace invalid characters
        for (char& c : name) {
            if (!isalnum(c)) c = '_';
        }
        return name;
    }
};

// Generate test parameters for each category
static std::vector<MathTestCase> GetOperatorTests() {
    return get_category_tests("operators");
}

static std::vector<MathTestCase> GetFractionTests() {
    return get_category_tests("fractions");
}

static std::vector<MathTestCase> GetRadicalTests() {
    return get_category_tests("radicals");
}

static std::vector<MathTestCase> GetAccentTests() {
    return get_category_tests("accents");
}

static std::vector<MathTestCase> GetDelimiterTests() {
    return get_category_tests("left_right");
}

static std::vector<MathTestCase> GetSpacingTests() {
    return get_category_tests("spacing");
}

static std::vector<MathTestCase> GetOverUnderTests() {
    return get_category_tests("overunder");
}

// Instantiate parameterized tests for each category

INSTANTIATE_TEST_SUITE_P(
    Operators,
    MathLayoutParamTest,
    ::testing::ValuesIn(GetOperatorTests()),
    MathTestNameGenerator()
);

INSTANTIATE_TEST_SUITE_P(
    Fractions,
    MathLayoutParamTest,
    ::testing::ValuesIn(GetFractionTests()),
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
    Spacing,
    MathLayoutParamTest,
    ::testing::ValuesIn(GetSpacingTests()),
    MathTestNameGenerator()
);

INSTANTIATE_TEST_SUITE_P(
    OverUnder,
    MathLayoutParamTest,
    ::testing::ValuesIn(GetOverUnderTests()),
    MathTestNameGenerator()
);

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    // Initialize logging
    log_init("log.conf");

    // Check for --generate-references flag
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--generate-references") == 0) {
            g_generate_references = true;
            log_info("Running in reference generation mode");
        }
    }

    ::testing::InitGoogleTest(&argc, argv);

    // Pre-load fixtures to report count
    load_test_fixtures();

    return RUN_ALL_TESTS();
}
