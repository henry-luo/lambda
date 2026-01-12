// test_dvi_compare_gtest.cpp - Compare Radiant DVI output with reference DVI files
//
// Tests the LaTeX typesetting pipeline by comparing generated DVI files
// against reference DVI files produced by standard TeX.

#include <gtest/gtest.h>
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lambda/tex/tex_pagebreak.hpp"
#include "lambda/tex/tex_dvi_out.hpp"
#include "lambda/tex/tex_latex_bridge.hpp"
#include "lambda/tex/dvi_parser.hpp"
#include "lambda/input/input.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include "lib/file.h"
#include "lib/url.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>

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
     * Render a LaTeX file to DVI using Radiant's pipeline.
     * Returns true on success, false on failure.
     */
    bool render_latex_to_dvi_internal(const char* latex_file, const char* dvi_output) {
        // Read LaTeX file
        char* latex_content = read_text_file(latex_file);
        if (!latex_content) {
            log_error("Failed to read LaTeX file: %s", latex_file);
            return false;
        }

        // Parse LaTeX URL
        Url* cwd = get_current_dir();
        Url* latex_url = url_parse_with_base(latex_file, cwd);
        url_destroy(cwd);

        // Create type string
        String* type_str = (String*)pool_alloc(pool, sizeof(String) + 6);
        type_str->len = 5;
        strcpy(type_str->chars, "latex");

        // Parse LaTeX
        Input* latex_input = input_from_source(latex_content, latex_url, type_str, nullptr);
        free(latex_content);

        if (!latex_input || !latex_input->root.item) {
            log_error("Failed to parse LaTeX file: %s", latex_file);
            return false;
        }

        // Set up TeX context
        TFMFontManager* fonts = create_font_manager(arena);
        LaTeXContext ctx = LaTeXContext::create(arena, fonts, "article");

        // Standard page layout
        ctx.doc_ctx.page_width = 612.0f;
        ctx.doc_ctx.page_height = 792.0f;
        ctx.doc_ctx.margin_left = 72.0f;
        ctx.doc_ctx.margin_right = 72.0f;
        ctx.doc_ctx.margin_top = 72.0f;
        ctx.doc_ctx.margin_bottom = 72.0f;
        ctx.doc_ctx.text_width = ctx.doc_ctx.page_width - ctx.doc_ctx.margin_left - ctx.doc_ctx.margin_right;
        ctx.doc_ctx.text_height = ctx.doc_ctx.page_height - ctx.doc_ctx.margin_top - ctx.doc_ctx.margin_bottom;

        // Typeset
        TexNode* document = typeset_latex_document(latex_input->root, ctx);
        if (!document) {
            log_error("Failed to typeset document");
            return false;
        }

        // Break into pages
        PageList pages = break_latex_into_pages(document, ctx);
        if (pages.page_count == 0) {
            log_error("No pages generated");
            return false;
        }

        // Convert to PageContent array
        PageContent* page_contents = (PageContent*)arena_alloc(
            arena, pages.page_count * sizeof(PageContent)
        );
        for (int i = 0; i < pages.page_count; i++) {
            page_contents[i].vlist = pages.pages[i];
            page_contents[i].height = 0.0f;
            page_contents[i].depth = 0.0f;
            page_contents[i].break_penalty = 0;
            page_contents[i].marks_first = nullptr;
            page_contents[i].marks_top = nullptr;
            page_contents[i].marks_bot = nullptr;
            page_contents[i].inserts = nullptr;
        }

        // Write DVI
        DVIParams params = DVIParams::defaults();
        params.comment = "Lambda Script TeX Output";

        return write_dvi_file(dvi_output, page_contents, pages.page_count, fonts, arena, params);
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

        // Debug: show glyph counts
        fprintf(stderr, "[DEBUG] ref_parser: %d pages\n", ref_parser.page_count());
        fprintf(stderr, "[DEBUG] out_parser: %d pages\n", out_parser.page_count());
        if (ref_parser.page_count() > 0) {
            const DVIPage* p = ref_parser.page(0);
            fprintf(stderr, "[DEBUG] ref page 0: %d glyphs\n", p->glyph_count);
        }
        if (out_parser.page_count() > 0) {
            const DVIPage* p = out_parser.page(0);
            fprintf(stderr, "[DEBUG] out page 0: %d glyphs\n", p->glyph_count);
            for (int i = 0; i < p->glyph_count && i < 20; i++) {
                fprintf(stderr, "[DEBUG]   glyph %d: codepoint=%d '%c'\n",
                        i, p->glyphs[i].codepoint,
                        (p->glyphs[i].codepoint >= 32 && p->glyphs[i].codepoint < 127)
                            ? (char)p->glyphs[i].codepoint : '?');
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
     */
    ::testing::AssertionResult test_latex_file(const char* test_name) {
        char latex_path[512];
        char ref_dvi_path[512];
        char out_dvi_path[512];

        snprintf(latex_path, sizeof(latex_path),
                 "test/latex/%s.tex", test_name);
        snprintf(ref_dvi_path, sizeof(ref_dvi_path),
                 "test/latex/reference/%s.dvi", test_name);
        snprintf(out_dvi_path, sizeof(out_dvi_path),
                 "%s/%s.dvi", temp_dir, test_name);

        // Check that source and reference exist
        if (!file_exists(latex_path)) {
            return ::testing::AssertionFailure()
                << "LaTeX source file not found: " << latex_path;
        }
        if (!file_exists(ref_dvi_path)) {
            return ::testing::AssertionFailure()
                << "Reference DVI not found: " << ref_dvi_path;
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
// Normalization Unit Tests
// ============================================================================

TEST_F(DVICompareTest, NormalizationIgnoresComment) {
    // Parse a reference DVI file
    const char* ref_path = "test/latex/reference/test_simple_text.dvi";
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

TEST_F(DVICompareTest, ExtractTextContent) {
    const char* ref_path = "test/latex/reference/test_simple_text.dvi";
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
// DVI Comparison Tests
// ============================================================================

TEST_F(DVICompareTest, SimpleText) {
    // Generate the DVI and save to a known location for debugging
    char out_dvi_path[512];
    snprintf(out_dvi_path, sizeof(out_dvi_path), "/tmp/lambda_test_simple_text.dvi");
    if (render_latex_to_dvi_internal("test/latex/test_simple_text.tex", out_dvi_path)) {
        fprintf(stderr, "[DEBUG] Generated DVI saved to: %s\n", out_dvi_path);
    }
    EXPECT_TRUE(test_latex_file("test_simple_text"));
}

// Additional test cases - these may be skipped if reference files don't exist
// or if the typesetter doesn't yet support all features

TEST_F(DVICompareTest, SimpleMath) {
    // Math typesetting comparison
    EXPECT_TRUE(test_latex_file("test_simple_math"));
}

TEST_F(DVICompareTest, DISABLED_Fraction) {
    EXPECT_TRUE(test_latex_file("test_fraction"));
}

TEST_F(DVICompareTest, Greek) {
    EXPECT_TRUE(test_latex_file("test_greek"));
}

TEST_F(DVICompareTest, DISABLED_Sqrt) {
    EXPECT_TRUE(test_latex_file("test_sqrt"));
}

TEST_F(DVICompareTest, DISABLED_SubscriptSuperscript) {
    EXPECT_TRUE(test_latex_file("test_subscript_superscript"));
}

TEST_F(DVICompareTest, DISABLED_Delimiters) {
    EXPECT_TRUE(test_latex_file("test_delimiters"));
}

TEST_F(DVICompareTest, DISABLED_SumIntegral) {
    EXPECT_TRUE(test_latex_file("test_sum_integral"));
}

TEST_F(DVICompareTest, DISABLED_Matrix) {
    EXPECT_TRUE(test_latex_file("test_matrix"));
}

TEST_F(DVICompareTest, DISABLED_ComplexFormula) {
    EXPECT_TRUE(test_latex_file("test_complex_formula"));
}

// ============================================================================
// Self-Consistency Tests
// ============================================================================

TEST_F(DVICompareTest, SelfConsistency) {
    // Render the same file twice and verify outputs match
    const char* latex_path = "test/latex/test_simple_text.tex";
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
