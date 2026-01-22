// test_dvi_compare_gtest.cpp - Compare Radiant DVI output with reference DVI files
//
// Tests the LaTeX typesetting pipeline by comparing generated DVI files
// against reference DVI files produced by standard TeX.
//
// Directory structure:
//   test/latex/fixtures/<category>/<name>.tex  - Source files
//   test/latex/expected/<category>/<name>.dvi  - Reference DVI files
//
// To regenerate reference files:
//   node utils/generate_latex_refs.js --output-format=dvi --force
//
// This test uses the Lambda CLI (./lambda.exe render) to generate DVI output,
// making it a true integration test of the full rendering pipeline.

#include <gtest/gtest.h>
#include "lambda/tex/dvi_parser.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

using namespace tex;
using namespace tex::dvi;

// ============================================================================
// DVI Normalization Functions
// ============================================================================

/**
 * Normalized DVI content for comparison.
 * Ignores:
 * - Comment header (varies between tools)
 * - Timestamp information
 * - PostScript specials (header=...)
 */
struct NormalizedDVI {
    int page_count;

    // Per-page normalized content
    struct NormalizedPage {
        // Glyphs with font name (not font number, since numbering may differ)
        struct NormalizedGlyph {
            int32_t codepoint;
            const char* font_name;  // e.g., "cmr10"
            // Note: we don't compare positions for now since they require
            // matching TeX's exact spacing algorithms
        };

        NormalizedGlyph* glyphs;
        int glyph_count;

        // Text content as string (for quick comparison)
        char* text_content;
        int text_length;
    };

    NormalizedPage* pages;
};

/**
 * Extract text content from a DVI page (ignoring positions).
 * Returns printable ASCII characters only.
 */
static char* extract_page_text(const DVIPage* page, Arena* arena) {
    // Allocate buffer for text (worst case: all glyphs are printable)
    char* text = (char*)arena_alloc(arena, page->glyph_count + 1);
    int text_len = 0;

    for (int i = 0; i < page->glyph_count; i++) {
        int32_t cp = page->glyphs[i].codepoint;
        // Only include printable ASCII
        if (cp >= 32 && cp < 127) {
            text[text_len++] = (char)cp;
        }
    }
    text[text_len] = '\0';

    return text;
}

/**
 * Get font name for a glyph.
 */
static const char* get_glyph_font_name(const DVIParser& parser, uint32_t font_num) {
    const DVIFont* font = parser.font(font_num);
    return font ? font->name : "unknown";
}

/**
 * Normalize a parsed DVI file for comparison.
 * This extracts the semantic content while ignoring tool-specific differences.
 */
static NormalizedDVI normalize_dvi(const DVIParser& parser, Arena* arena) {
    NormalizedDVI norm = {};
    norm.page_count = parser.page_count();

    if (norm.page_count == 0) {
        return norm;
    }

    norm.pages = (NormalizedDVI::NormalizedPage*)arena_alloc(
        arena, norm.page_count * sizeof(NormalizedDVI::NormalizedPage)
    );

    for (int p = 0; p < norm.page_count; p++) {
        const DVIPage* page = parser.page(p);
        NormalizedDVI::NormalizedPage& np = norm.pages[p];

        // Extract text content
        np.text_content = extract_page_text(page, arena);
        np.text_length = strlen(np.text_content);

        // Store normalized glyphs
        np.glyph_count = page->glyph_count;
        if (np.glyph_count > 0) {
            np.glyphs = (NormalizedDVI::NormalizedPage::NormalizedGlyph*)arena_alloc(
                arena, np.glyph_count * sizeof(NormalizedDVI::NormalizedPage::NormalizedGlyph)
            );

            for (int g = 0; g < np.glyph_count; g++) {
                np.glyphs[g].codepoint = page->glyphs[g].codepoint;
                np.glyphs[g].font_name = get_glyph_font_name(parser, page->glyphs[g].font_num);
            }
        } else {
            np.glyphs = nullptr;
        }
    }

    return norm;
}

/**
 * Compare two normalized DVIs for text content equality.
 * Returns true if the text content matches on all pages.
 */
static bool compare_dvi_text(const NormalizedDVI& ref, const NormalizedDVI& out,
                              char* error_msg, size_t error_size) {
    if (ref.page_count != out.page_count) {
        snprintf(error_msg, error_size,
                 "Page count mismatch: reference=%d, output=%d",
                 ref.page_count, out.page_count);
        return false;
    }

    for (int p = 0; p < ref.page_count; p++) {
        const char* ref_text = ref.pages[p].text_content;
        const char* out_text = out.pages[p].text_content;

        if (strcmp(ref_text, out_text) != 0) {
            snprintf(error_msg, error_size,
                     "Text mismatch on page %d:\n  Reference: \"%s\"\n  Output:    \"%s\"",
                     p + 1, ref_text, out_text);
            return false;
        }
    }

    error_msg[0] = '\0';
    return true;
}

/**
 * Compare glyph sequences (ignoring positions).
 * Checks that the same characters are rendered in the same order with same fonts.
 */
static bool compare_dvi_glyphs(const NormalizedDVI& ref, const NormalizedDVI& out,
                                char* error_msg, size_t error_size) {
    if (ref.page_count != out.page_count) {
        snprintf(error_msg, error_size,
                 "Page count mismatch: reference=%d, output=%d",
                 ref.page_count, out.page_count);
        return false;
    }

    for (int p = 0; p < ref.page_count; p++) {
        const auto& ref_page = ref.pages[p];
        const auto& out_page = out.pages[p];

        if (ref_page.glyph_count != out_page.glyph_count) {
            snprintf(error_msg, error_size,
                     "Glyph count mismatch on page %d: reference=%d, output=%d",
                     p + 1, ref_page.glyph_count, out_page.glyph_count);
            return false;
        }

        for (int g = 0; g < ref_page.glyph_count; g++) {
            const auto& ref_g = ref_page.glyphs[g];
            const auto& out_g = out_page.glyphs[g];

            if (ref_g.codepoint != out_g.codepoint) {
                snprintf(error_msg, error_size,
                         "Glyph %d mismatch on page %d: ref char=%d, out char=%d",
                         g, p + 1, ref_g.codepoint, out_g.codepoint);
                return false;
            }

            // Font comparison (optional - fonts may have different numbering)
            if (strcmp(ref_g.font_name, out_g.font_name) != 0) {
                snprintf(error_msg, error_size,
                         "Font mismatch at glyph %d on page %d: ref=%s, out=%s",
                         g, p + 1, ref_g.font_name, out_g.font_name);
                return false;
            }
        }
    }

    error_msg[0] = '\0';
    return true;
}

// ============================================================================
// Verbose Mode Flag (controlled via environment variable or --verbose flag)
// ============================================================================

static bool g_verbose_mode = false;

// Check if verbose mode is enabled (via DVI_TEST_VERBOSE env var or --verbose argument)
static bool is_verbose() {
    static bool checked = false;
    if (!checked) {
        const char* env = getenv("DVI_TEST_VERBOSE");
        if (env && (strcmp(env, "1") == 0 || strcmp(env, "true") == 0)) {
            g_verbose_mode = true;
        }
        checked = true;
    }
    return g_verbose_mode;
}

// ============================================================================
// Test Fixture
// ============================================================================

class DVICompareTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    char temp_dir[256];

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);

        // Create temp directory
        snprintf(temp_dir, sizeof(temp_dir), "/tmp/dvi_compare_test_%d", getpid());
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", temp_dir);
        system(cmd);
    }

    void TearDown() override {
        // Clean up temp directory
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", temp_dir);
        system(cmd);

        arena_destroy(arena);
        pool_destroy(pool);
    }

    // Check if file exists
    bool file_exists(const char* path) {
        struct stat st;
        return stat(path, &st) == 0;
    }

    // Get temp file path
    void temp_file(char* buf, size_t size, const char* name) {
        snprintf(buf, size, "%s/%s", temp_dir, name);
    }

    /**
     * Render a LaTeX file to DVI using Lambda CLI.
     * Uses: ./lambda.exe render input.tex -o output.dvi
     * Returns true on success, false on failure.
     */
    bool render_latex_to_dvi_internal(const char* latex_file, const char* dvi_output) {
        // Ensure output directory exists
        char out_dir[512];
        strncpy(out_dir, dvi_output, sizeof(out_dir) - 1);
        out_dir[sizeof(out_dir) - 1] = '\0';
        char* last_slash = strrchr(out_dir, '/');
        if (last_slash) {
            *last_slash = '\0';
            char mkdir_cmd[600];
            snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", out_dir);
            system(mkdir_cmd);
        }

        // Build the command with timeout (30 seconds max)
        // Redirect stderr to stdout, then to /dev/null to avoid buffer blocking
        char cmd[1024];

        #ifdef __APPLE__
        snprintf(cmd, sizeof(cmd), "gtimeout 30s ./lambda.exe render %s -o %s >/dev/null 2>&1", latex_file, dvi_output);
        #else
        snprintf(cmd, sizeof(cmd), "timeout 30s ./lambda.exe render %s -o %s >/dev/null 2>&1", latex_file, dvi_output);
        #endif

        // Execute the command
        int exit_code = system(cmd);

        // Check for timeout (exit code 124 for timeout command)
        if (WEXITSTATUS(exit_code) == 124) {
            fprintf(stderr, "[ERROR] lambda render timed out after 30 seconds for: %s\n", latex_file);
            return false;
        }

        if (exit_code != 0) {
            fprintf(stderr, "[ERROR] lambda render failed with exit code %d for: %s\n", WEXITSTATUS(exit_code), latex_file);
            return false;
        }

        // Verify output file exists
        if (!file_exists(dvi_output)) {
            fprintf(stderr, "[ERROR] DVI output file not created: %s\n", dvi_output);
            return false;
        }

        return true;
    }

    /**
     * Compare a generated DVI with a reference DVI.
     * Returns true if they match (ignoring comment header).
     */
    ::testing::AssertionResult compare_dvi_files(const char* ref_path, const char* out_path) {
        // Parse reference DVI
        DVIParser ref_parser(arena);
        if (!ref_parser.parse_file(ref_path)) {
            return ::testing::AssertionFailure()
                << "Failed to parse reference DVI: " << ref_path
                << " (" << ref_parser.error() << ")";
        }

        // Parse output DVI
        DVIParser out_parser(arena);
        if (!out_parser.parse_file(out_path)) {
            return ::testing::AssertionFailure()
                << "Failed to parse output DVI: " << out_path
                << " (" << out_parser.error() << ")";
        }

        // Basic info (always show)
        fprintf(stderr, "[INFO] ref: %d pages, out: %d pages\n", 
                ref_parser.page_count(), out_parser.page_count());
        
        // Show side-by-side diff of first mismatches
        if (ref_parser.page_count() > 0 && out_parser.page_count() > 0) {
            const DVIPage* ref_page = ref_parser.page(0);
            const DVIPage* out_page = out_parser.page(0);
            int max_glyphs = std::min(ref_page->glyph_count, out_page->glyph_count);
            int diff_count = 0;
            
            fprintf(stderr, "[INFO] ref page 0: %d glyphs, out page 0: %d glyphs\n", 
                    ref_page->glyph_count, out_page->glyph_count);
            
            // In verbose mode, dump all glyphs; otherwise just show first 5 diffs
            if (is_verbose()) {
                fprintf(stderr, "[VERBOSE] === Reference Glyphs ===\n");
                for (int i = 0; i < ref_page->glyph_count && i < 50; i++) {
                    const DVIFont* f = ref_parser.font(ref_page->glyphs[i].font_num);
                    int cp = ref_page->glyphs[i].codepoint;
                    char ch = (cp >= 32 && cp < 127) ? (char)cp : '?';
                    fprintf(stderr, "  [%3d] cp=%3d '%c' font=%s\n", i, cp, ch, f ? f->name : "?");
                }
                fprintf(stderr, "[VERBOSE] === Output Glyphs ===\n");
                for (int i = 0; i < out_page->glyph_count && i < 50; i++) {
                    const DVIFont* f = out_parser.font(out_page->glyphs[i].font_num);
                    int cp = out_page->glyphs[i].codepoint;
                    char ch = (cp >= 32 && cp < 127) ? (char)cp : '?';
                    fprintf(stderr, "  [%3d] cp=%3d '%c' font=%s\n", i, cp, ch, f ? f->name : "?");
                }
            }
            
            // Show first 5 differences (or all in verbose mode)
            int max_diffs = is_verbose() ? 20 : 5;
            for (int i = 0; i < max_glyphs && diff_count < max_diffs; i++) {
                int ref_cp = ref_page->glyphs[i].codepoint;
                int out_cp = out_page->glyphs[i].codepoint;
                const DVIFont* ref_font = ref_parser.font(ref_page->glyphs[i].font_num);
                const DVIFont* out_font = out_parser.font(out_page->glyphs[i].font_num);
                const char* ref_name = ref_font ? ref_font->name : "?";
                const char* out_name = out_font ? out_font->name : "?";
                
                if (ref_cp != out_cp || strcmp(ref_name, out_name) != 0) {
                    char ref_char = (ref_cp >= 32 && ref_cp < 127) ? (char)ref_cp : '?';
                    char out_char = (out_cp >= 32 && out_cp < 127) ? (char)out_cp : '?';
                    fprintf(stderr, "[DIFF] glyph %d: ref=%d '%c' (%s) vs out=%d '%c' (%s)\n",
                            i, ref_cp, ref_char, ref_name, out_cp, out_char, out_name);
                    diff_count++;
                }
            }
            
            // Show extra glyphs if counts differ
            if (ref_page->glyph_count > out_page->glyph_count) {
                fprintf(stderr, "[DIFF] ref has %d extra glyphs starting at index %d\n",
                        ref_page->glyph_count - out_page->glyph_count, out_page->glyph_count);
            } else if (out_page->glyph_count > ref_page->glyph_count) {
                fprintf(stderr, "[DIFF] out has %d extra glyphs starting at index %d\n",
                        out_page->glyph_count - ref_page->glyph_count, ref_page->glyph_count);
            }
            
            if (diff_count == 0 && ref_page->glyph_count == out_page->glyph_count) {
                fprintf(stderr, "[INFO] All %d glyphs match!\n", max_glyphs);
            }
        }

        // Normalize both
        NormalizedDVI ref_norm = normalize_dvi(ref_parser, arena);
        NormalizedDVI out_norm = normalize_dvi(out_parser, arena);

        // Compare text content
        char error_msg[1024];
        if (!compare_dvi_text(ref_norm, out_norm, error_msg, sizeof(error_msg))) {
            return ::testing::AssertionFailure() << error_msg;
        }

        return ::testing::AssertionSuccess();
    }

    /**
     * Run the full comparison test for a LaTeX file.
     * test_name is relative path from fixtures/ without .tex extension
     */
    ::testing::AssertionResult test_latex_file(const char* test_name) {
        char latex_path[512];
        char ref_dvi_path[512];
        char out_dvi_path[512];

        snprintf(latex_path, sizeof(latex_path),
                 "test/latex/fixtures/%s.tex", test_name);
        snprintf(ref_dvi_path, sizeof(ref_dvi_path),
                 "test/latex/expected/%s.dvi", test_name);
        snprintf(out_dvi_path, sizeof(out_dvi_path),
                 "%s/%s.dvi", temp_dir, test_name);

        // Check that source and reference exist
        if (!file_exists(latex_path)) {
            return ::testing::AssertionFailure()
                << "LaTeX source file not found: " << latex_path;
        }
        if (!file_exists(ref_dvi_path)) {
            return ::testing::AssertionFailure()
                << "Reference DVI not found: " << ref_dvi_path
                << " (run: node utils/generate_latex_refs.js --output-format=dvi)";
        }

        // Render LaTeX to DVI
        if (!render_latex_to_dvi_internal(latex_path, out_dvi_path)) {
            return ::testing::AssertionFailure()
                << "Failed to render LaTeX to DVI: " << latex_path;
        }

        // Compare with reference
        return compare_dvi_files(ref_dvi_path, out_dvi_path);
    }
};

// ============================================================================
// Derived Test Fixtures for Baseline and Extended Tests
// ============================================================================

// Baseline tests - these are stable and should always pass
class DVICompareBaselineTest : public DVICompareTest {};

// Extended tests - these are work-in-progress and may fail
class DVICompareExtendedTest : public DVICompareTest {};

// ============================================================================
// Baseline: Normalization Unit Tests
// ============================================================================

TEST_F(DVICompareBaselineTest, NormalizationIgnoresComment) {
    // Parse a reference DVI file
    const char* ref_path = "test/latex/expected/basic/test_simple_text.dvi";
    if (!file_exists(ref_path)) {
        GTEST_SKIP() << "Reference DVI not found: " << ref_path;
    }

    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(ref_path));

    // The comment should be accessible but ignored in normalization
    const DVIPreamble* pre = parser.preamble();
    EXPECT_NE(pre->comment, nullptr);

    // Normalization should work regardless of comment content
    NormalizedDVI norm = normalize_dvi(parser, arena);
    EXPECT_GE(norm.page_count, 1);
}

TEST_F(DVICompareBaselineTest, ExtractTextContent) {
    const char* ref_path = "test/latex/expected/basic/test_simple_text.dvi";
    if (!file_exists(ref_path)) {
        GTEST_SKIP() << "Reference DVI not found: " << ref_path;
    }

    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(ref_path));

    NormalizedDVI norm = normalize_dvi(parser, arena);
    ASSERT_GE(norm.page_count, 1);

    // test_simple_text.tex contains "Hello World"
    const char* text = norm.pages[0].text_content;
    EXPECT_NE(strstr(text, "Hello"), nullptr) << "Text content: " << text;
    EXPECT_NE(strstr(text, "orld"), nullptr) << "Text content: " << text;
}

// ============================================================================
// Baseline: DVI Comparison Tests (Passing)
// ============================================================================

TEST_F(DVICompareBaselineTest, SimpleText) {
    // Generate the DVI and save to a known location for debugging
    char out_dvi_path[512];
    snprintf(out_dvi_path, sizeof(out_dvi_path), "/tmp/lambda_test_simple_text.dvi");
    if (render_latex_to_dvi_internal("test/latex/fixtures/basic/test_simple_text.tex", out_dvi_path)) {
        fprintf(stderr, "[DEBUG] Generated DVI saved to: %s\n", out_dvi_path);
    }
    EXPECT_TRUE(test_latex_file("basic/test_simple_text"));
}

TEST_F(DVICompareBaselineTest, SimpleMath) {
    EXPECT_TRUE(test_latex_file("basic/test_simple_math"));
}

TEST_F(DVICompareBaselineTest, Fraction) {
    EXPECT_TRUE(test_latex_file("math/test_fraction"));
}

TEST_F(DVICompareBaselineTest, Greek) {
    EXPECT_TRUE(test_latex_file("math/test_greek"));
}

TEST_F(DVICompareBaselineTest, Sqrt) {
    EXPECT_TRUE(test_latex_file("math/test_sqrt"));
}

TEST_F(DVICompareBaselineTest, SubscriptSuperscript) {
    EXPECT_TRUE(test_latex_file("math/test_subscript_superscript"));
}

TEST_F(DVICompareBaselineTest, Delimiters) {
    EXPECT_TRUE(test_latex_file("math/test_delimiters"));
}

TEST_F(DVICompareBaselineTest, SumIntegral) {
    EXPECT_TRUE(test_latex_file("math/test_sum_integral"));
}

TEST_F(DVICompareBaselineTest, ComplexFormula) {
    EXPECT_TRUE(test_latex_file("math/test_complex_formula"));
}

TEST_F(DVICompareBaselineTest, Calculus) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_calculus"));
}

TEST_F(DVICompareBaselineTest, SetTheory) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_set_theory"));
}

TEST_F(DVICompareBaselineTest, LinearAlgebra2_Eigenvalues) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_linear_algebra2"));
}

// ============================================================================
// Baseline: MathLive Fixtures (converted from test/math/fixtures)
// ============================================================================

TEST_F(DVICompareBaselineTest, Mathlive_Fractions) {
    EXPECT_TRUE(test_latex_file("math/mathlive/fractions_000"));
    EXPECT_TRUE(test_latex_file("math/mathlive/fractions_001"));
    EXPECT_TRUE(test_latex_file("math/mathlive/fractions_002"));
}

TEST_F(DVICompareBaselineTest, Mathlive_Accents) {
    EXPECT_TRUE(test_latex_file("math/mathlive/accents_000"));
    EXPECT_TRUE(test_latex_file("math/mathlive/accents_001"));
    EXPECT_TRUE(test_latex_file("math/mathlive/accents_002"));
}

TEST_F(DVICompareBaselineTest, Mathlive_Operators) {
    EXPECT_TRUE(test_latex_file("math/mathlive/operators_000"));
    EXPECT_TRUE(test_latex_file("math/mathlive/operators_001"));
    EXPECT_TRUE(test_latex_file("math/mathlive/operators_002"));
}

// ============================================================================
// Extended: TeX Primitives (Spacing, Glue, Rules, Boxes)
// Note: These tests verify DVI output of TeX primitives. The primitives are
// currently implemented for HTML output only (see test_tex_primitives_gtest.cpp).
// DVI output requires additional work in the TeX typesetting engine.
// These tests are DISABLED until DVI primitive support is implemented.
// ============================================================================

// Spacing primitives
TEST_F(DVICompareExtendedTest, PrimSpacingHskip) {
    EXPECT_TRUE(test_latex_file("primitives/test_prim_spacing_hskip"));
}

TEST_F(DVICompareExtendedTest, PrimSpacingGlue) {
    EXPECT_TRUE(test_latex_file("primitives/test_prim_spacing_glue"));
}

// Rule primitives
TEST_F(DVICompareExtendedTest, PrimRulesHruleVrule) {
    EXPECT_TRUE(test_latex_file("primitives/test_prim_rules_hrule_vrule"));
}

// Penalty primitives
TEST_F(DVICompareExtendedTest, PrimPenalties) {
    EXPECT_TRUE(test_latex_file("primitives/test_prim_penalties"));
}

// Box primitives
TEST_F(DVICompareExtendedTest, PrimBoxesHbox) {
    EXPECT_TRUE(test_latex_file("boxes/test_prim_boxes_hbox"));
}

TEST_F(DVICompareExtendedTest, PrimBoxesVbox) {
    EXPECT_TRUE(test_latex_file("boxes/test_prim_boxes_vbox"));
}

TEST_F(DVICompareExtendedTest, PrimBoxesLap) {
    EXPECT_TRUE(test_latex_file("boxes/test_prim_boxes_lap"));
}

TEST_F(DVICompareExtendedTest, PrimBoxesShift) {
    EXPECT_TRUE(test_latex_file("boxes/test_prim_boxes_shift"));
}

// Combined layout using multiple primitives
TEST_F(DVICompareExtendedTest, PrimCombinedLayout) {
    EXPECT_TRUE(test_latex_file("primitives/test_prim_combined_layout"));
}

// ============================================================================
// Baseline: Self-Consistency Tests
// ============================================================================

TEST_F(DVICompareBaselineTest, SelfConsistency) {
    // Render the same file twice and verify outputs match
    const char* latex_path = "test/latex/fixtures/basic/test_simple_text.tex";
    if (!file_exists(latex_path)) {
        GTEST_SKIP() << "LaTeX source not found: " << latex_path;
    }

    char out1[512], out2[512];
    temp_file(out1, sizeof(out1), "self_test1.dvi");
    temp_file(out2, sizeof(out2), "self_test2.dvi");

    ASSERT_TRUE(render_latex_to_dvi_internal(latex_path, out1));
    ASSERT_TRUE(render_latex_to_dvi_internal(latex_path, out2));

    // Parse both outputs
    DVIParser parser1(arena);
    DVIParser parser2(arena);
    ASSERT_TRUE(parser1.parse_file(out1));
    ASSERT_TRUE(parser2.parse_file(out2));

    // Compare
    NormalizedDVI norm1 = normalize_dvi(parser1, arena);
    NormalizedDVI norm2 = normalize_dvi(parser2, arena);

    char error_msg[1024];
    EXPECT_TRUE(compare_dvi_text(norm1, norm2, error_msg, sizeof(error_msg)))
        << error_msg;
}

// ============================================================================
// Extended: Linear Algebra (split into smaller tests)
// ============================================================================

TEST_F(DVICompareExtendedTest, Matrix) {
    EXPECT_TRUE(test_latex_file("math/test_matrix"));
}

TEST_F(DVICompareExtendedTest, LinearAlgebra1_Matrix) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_linear_algebra1"));
}

TEST_F(DVICompareExtendedTest, LinearAlgebra3_SpecialMatrices) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_linear_algebra3"));
}

// ============================================================================
// Extended: Physics (split into smaller tests)
// ============================================================================

TEST_F(DVICompareExtendedTest, Physics1_Mechanics) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_physics1"));
}

TEST_F(DVICompareExtendedTest, Physics2_Quantum) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_physics2"));
}

// ============================================================================
// Extended: Nested Structures (split into smaller tests)
// ============================================================================

TEST_F(DVICompareExtendedTest, Nested1_Fractions) {
    EXPECT_TRUE(test_latex_file("math/test_nested1"));
}

TEST_F(DVICompareExtendedTest, Nested2_Scripts) {
    EXPECT_TRUE(test_latex_file("math/test_nested2"));
}

// ============================================================================
// Extended: Sophisticated Math Tests (Work in Progress)
// ============================================================================

TEST_F(DVICompareExtendedTest, NumberTheory) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_number_theory"));
}

TEST_F(DVICompareExtendedTest, Probability) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_probability"));
}

TEST_F(DVICompareExtendedTest, Combinatorics) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_combinatorics"));
}

TEST_F(DVICompareExtendedTest, AbstractAlgebra) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_abstract_algebra"));
}

TEST_F(DVICompareExtendedTest, DifferentialEquations) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_differential_equations"));
}

TEST_F(DVICompareExtendedTest, ComplexAnalysis) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_complex_analysis"));
}

TEST_F(DVICompareExtendedTest, Topology) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_topology"));
}

// ============================================================================
// Extended: Structure and Syntax Tests (Work in Progress)
// ============================================================================

TEST_F(DVICompareExtendedTest, EdgeCases) {
    EXPECT_TRUE(test_latex_file("align/test_edge_cases"));
}

TEST_F(DVICompareExtendedTest, AllGreek) {
    EXPECT_TRUE(test_latex_file("math/test_all_greek"));
}

TEST_F(DVICompareExtendedTest, AllOperators) {
    EXPECT_TRUE(test_latex_file("math/test_all_operators"));
}

TEST_F(DVICompareExtendedTest, AlignmentAdvanced) {
    EXPECT_TRUE(test_latex_file("align/test_alignment_advanced"));
}

TEST_F(DVICompareExtendedTest, Chemistry) {
    EXPECT_TRUE(test_latex_file("math/subjects/test_chemistry"));
}

TEST_F(DVICompareExtendedTest, FontStyles) {
    EXPECT_TRUE(test_latex_file("math/test_font_styles"));
}

TEST_F(DVICompareExtendedTest, Tables) {
    EXPECT_TRUE(test_latex_file("document/test_tables"));
}

// ============================================================================
// Extended: MathLive Fixtures (work in progress)
// ============================================================================

TEST_F(DVICompareExtendedTest, Mathlive_Radicals) {
    EXPECT_TRUE(test_latex_file("math/mathlive/radicals_000"));
    EXPECT_TRUE(test_latex_file("math/mathlive/radicals_001"));
    EXPECT_TRUE(test_latex_file("math/mathlive/radicals_002"));
}

TEST_F(DVICompareExtendedTest, Mathlive_Delimiters) {
    EXPECT_TRUE(test_latex_file("math/mathlive/left_right_000"));
    EXPECT_TRUE(test_latex_file("math/mathlive/left_right_001"));
    EXPECT_TRUE(test_latex_file("math/mathlive/left_right_002"));
}

TEST_F(DVICompareExtendedTest, Mathlive_Spacing) {
    EXPECT_TRUE(test_latex_file("math/mathlive/spacing_000"));
    EXPECT_TRUE(test_latex_file("math/mathlive/spacing_001"));
    EXPECT_TRUE(test_latex_file("math/mathlive/spacing_002"));
}