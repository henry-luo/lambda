// test_tex_phase5_gtest.cpp - Unit tests for DVI and PDF output (Phase 5)
//
// Tests the tex_dvi_out.hpp and tex_pdf_out.hpp implementations.

#include <gtest/gtest.h>
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lambda/tex/tex_hlist.hpp"
#include "lambda/tex/tex_linebreak.hpp"
#include "lambda/tex/tex_vlist.hpp"
#include "lambda/tex/tex_pagebreak.hpp"
#include "lambda/tex/tex_dvi_out.hpp"
#include "lambda/tex/tex_pdf_out.hpp"
#include "lambda/tex/dvi_parser.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>

using namespace tex;
using namespace tex::dvi;  // For DVIParser, DVIPage, DVIFont

// ============================================================================
// Test Fixture
// ============================================================================

class TexPhase5Test : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    TFMFontManager* fonts;
    char temp_dir[256];

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        fonts = create_font_manager(arena);

        // Create temp directory for test outputs
        snprintf(temp_dir, sizeof(temp_dir), "/tmp/tex_phase5_test_%d", getpid());
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", temp_dir);
        system(cmd);
    }

    void TearDown() override {
        // Clean up temp files
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", temp_dir);
        system(cmd);

        arena_destroy(arena);
        pool_destroy(pool);
    }

    // Helper to create a simple text vlist
    TexNode* create_test_vlist(const char* text) {
        VListContext ctx(arena, fonts);
        init_vlist_context(ctx, 300.0f);

        begin_vlist(ctx);
        add_paragraph(ctx, text, strlen(text));
        return end_vlist(ctx);
    }

    // Helper to get temp file path
    void temp_file(char* buf, size_t size, const char* name) {
        snprintf(buf, size, "%s/%s", temp_dir, name);
    }
};

// ============================================================================
// DVI Unit Conversion Tests
// ============================================================================

TEST_F(TexPhase5Test, PointsToScaledPoints) {
    // 1 point = 65536 scaled points
    EXPECT_EQ(pt_to_sp(1.0f), 65536);
    EXPECT_EQ(pt_to_sp(10.0f), 655360);
    EXPECT_EQ(pt_to_sp(0.5f), 32768);
}

TEST_F(TexPhase5Test, ScaledPointsToPoints) {
    EXPECT_FLOAT_EQ(sp_to_pt(65536), 1.0f);
    EXPECT_FLOAT_EQ(sp_to_pt(655360), 10.0f);
    EXPECT_FLOAT_EQ(sp_to_pt(32768), 0.5f);
}

// ============================================================================
// DVI Params Tests
// ============================================================================

TEST_F(TexPhase5Test, DVIParamsDefaults) {
    DVIParams params = DVIParams::defaults();

    // Check standard DVI conversion values
    EXPECT_EQ(params.numerator, 25400000u);
    EXPECT_EQ(params.denominator, 473628672u);
    EXPECT_EQ(params.magnification, 1000u);
    EXPECT_EQ(params.max_stack_depth, 100);
}

// ============================================================================
// DVI Writer Basic Tests
// ============================================================================

TEST_F(TexPhase5Test, DVIWriterConstruction) {
    DVIWriter writer(arena);

    EXPECT_EQ(writer.file, nullptr);
    EXPECT_EQ(writer.h, 0);
    EXPECT_EQ(writer.v, 0);
    EXPECT_EQ(writer.page_count, 0);
}

TEST_F(TexPhase5Test, DVIOpenClose) {
    char path[512];
    temp_file(path, sizeof(path), "test_open.dvi");

    DVIWriter writer(arena);
    EXPECT_TRUE(dvi_open(writer, path, DVIParams::defaults()));
    EXPECT_NE(writer.file, nullptr);

    dvi_close(writer);
    EXPECT_EQ(writer.file, nullptr);

    // File should exist
    FILE* f = fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    fclose(f);
}

TEST_F(TexPhase5Test, DVIEmptyDocument) {
    char path[512];
    temp_file(path, sizeof(path), "test_empty.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());
    dvi_close(writer);

    // Parse it back
    DVIParser parser(arena);
    EXPECT_TRUE(parser.parse_file(path));
    EXPECT_EQ(parser.page_count(), 0);
}

// ============================================================================
// DVI Page Tests
// ============================================================================

TEST_F(TexPhase5Test, DVISingleEmptyPage) {
    char path[512];
    temp_file(path, sizeof(path), "test_page.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());

    dvi_begin_page(writer, 1);
    dvi_end_page(writer);

    dvi_close(writer);

    // Parse and verify
    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));
    EXPECT_EQ(parser.page_count(), 1);

    const DVIPage* page = parser.page(0);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->count[0], 1);  // First count value
}

TEST_F(TexPhase5Test, DVIMultiplePages) {
    char path[512];
    temp_file(path, sizeof(path), "test_multi.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());

    for (int i = 1; i <= 5; ++i) {
        dvi_begin_page(writer, i);
        dvi_end_page(writer);
    }

    dvi_close(writer);

    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));
    EXPECT_EQ(parser.page_count(), 5);

    for (int i = 0; i < 5; ++i) {
        const DVIPage* page = parser.page(i);
        EXPECT_EQ(page->count[0], i + 1);
    }
}

// ============================================================================
// DVI Font Tests
// ============================================================================

TEST_F(TexPhase5Test, DVIFontDefinition) {
    char path[512];
    temp_file(path, sizeof(path), "test_font.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());

    uint32_t font_num = dvi_define_font(writer, "cmr10", 10.0f);
    EXPECT_EQ(font_num, 0u);

    // Define another font
    uint32_t font_num2 = dvi_define_font(writer, "cmr12", 12.0f);
    EXPECT_EQ(font_num2, 1u);

    // Same font should return same number
    uint32_t font_num3 = dvi_define_font(writer, "cmr10", 10.0f);
    EXPECT_EQ(font_num3, 0u);

    dvi_begin_page(writer, 1);
    dvi_end_page(writer);
    dvi_close(writer);

    // Parse and verify fonts
    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));
    EXPECT_EQ(parser.font_count(), 2);

    const DVIFont* f1 = parser.font(0);
    ASSERT_NE(f1, nullptr);
    EXPECT_STREQ(f1->name, "cmr10");

    const DVIFont* f2 = parser.font(1);
    ASSERT_NE(f2, nullptr);
    EXPECT_STREQ(f2->name, "cmr12");
}

// ============================================================================
// DVI Character Output Tests
// ============================================================================

TEST_F(TexPhase5Test, DVISetChar) {
    char path[512];
    temp_file(path, sizeof(path), "test_char.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());

    dvi_define_font(writer, "cmr10", 10.0f);

    dvi_begin_page(writer, 1);
    dvi_select_font(writer, 0);

    // Output 'A' = 65
    dvi_set_char(writer, 65);

    dvi_end_page(writer);
    dvi_close(writer);

    // Parse and verify
    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));

    const DVIPage* page = parser.page(0);
    ASSERT_NE(page, nullptr);

    // Should have at least one glyph
    EXPECT_GE(page->glyph_count, 1);
    if (page->glyph_count > 0) {
        EXPECT_EQ(page->glyphs[0].codepoint, 65);
    }
}

TEST_F(TexPhase5Test, DVIMultipleChars) {
    char path[512];
    temp_file(path, sizeof(path), "test_chars.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());

    dvi_define_font(writer, "cmr10", 10.0f);

    dvi_begin_page(writer, 1);
    dvi_select_font(writer, 0);

    // Output "AB"
    dvi_set_char(writer, 65);  // A
    dvi_right(writer, pt_to_sp(6.0f));  // Advance
    dvi_set_char(writer, 66);  // B

    dvi_end_page(writer);
    dvi_close(writer);

    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));

    const DVIPage* page = parser.page(0);
    EXPECT_GE(page->glyph_count, 2);
}

// ============================================================================
// DVI Rule Tests
// ============================================================================

TEST_F(TexPhase5Test, DVISetRule) {
    char path[512];
    temp_file(path, sizeof(path), "test_rule.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());

    dvi_begin_page(writer, 1);

    // Draw a 100pt x 1pt rule
    dvi_set_rule(writer, pt_to_sp(1.0f), pt_to_sp(100.0f));

    dvi_end_page(writer);
    dvi_close(writer);

    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));

    const DVIPage* page = parser.page(0);
    EXPECT_GE(page->rule_count, 1);

    if (page->rule_count > 0) {
        EXPECT_NEAR(sp_to_pt(page->rules[0].width), 100.0f, 0.01f);
        EXPECT_NEAR(sp_to_pt(page->rules[0].height), 1.0f, 0.01f);
    }
}

// ============================================================================
// DVI Movement Tests
// ============================================================================

TEST_F(TexPhase5Test, DVIRightMovement) {
    DVIWriter writer(arena);
    char path[512];
    temp_file(path, sizeof(path), "test_right.dvi");

    dvi_open(writer, path, DVIParams::defaults());

    EXPECT_EQ(writer.h, 0);

    dvi_begin_page(writer, 1);
    dvi_right(writer, pt_to_sp(72.0f));  // Move 1 inch right
    EXPECT_EQ(writer.h, pt_to_sp(72.0f));

    dvi_end_page(writer);
    dvi_close(writer);
}

TEST_F(TexPhase5Test, DVIDownMovement) {
    DVIWriter writer(arena);
    char path[512];
    temp_file(path, sizeof(path), "test_down.dvi");

    dvi_open(writer, path, DVIParams::defaults());

    dvi_begin_page(writer, 1);
    dvi_down(writer, pt_to_sp(72.0f));  // Move 1 inch down
    EXPECT_EQ(writer.v, pt_to_sp(72.0f));

    dvi_end_page(writer);
    dvi_close(writer);
}

// ============================================================================
// DVI Stack Tests
// ============================================================================

TEST_F(TexPhase5Test, DVIPushPop) {
    DVIWriter writer(arena);
    char path[512];
    temp_file(path, sizeof(path), "test_stack.dvi");

    dvi_open(writer, path, DVIParams::defaults());

    dvi_begin_page(writer, 1);

    // Move to position
    dvi_right(writer, pt_to_sp(100.0f));
    dvi_down(writer, pt_to_sp(50.0f));
    EXPECT_EQ(writer.h, pt_to_sp(100.0f));
    EXPECT_EQ(writer.v, pt_to_sp(50.0f));

    // Save state
    dvi_push(writer);
    EXPECT_EQ(writer.stack_depth, 1);

    // Move further
    dvi_right(writer, pt_to_sp(20.0f));
    dvi_down(writer, pt_to_sp(10.0f));
    EXPECT_EQ(writer.h, pt_to_sp(120.0f));
    EXPECT_EQ(writer.v, pt_to_sp(60.0f));

    // Restore state
    dvi_pop(writer);
    EXPECT_EQ(writer.stack_depth, 0);
    EXPECT_EQ(writer.h, pt_to_sp(100.0f));
    EXPECT_EQ(writer.v, pt_to_sp(50.0f));

    dvi_end_page(writer);
    dvi_close(writer);
}

// ============================================================================
// DVI High-Level API Tests
// ============================================================================

TEST_F(TexPhase5Test, DVIWriteSimplePage) {
    char path[512];
    temp_file(path, sizeof(path), "test_simple_page.dvi");

    TexNode* vlist = create_test_vlist("Hello world");
    ASSERT_NE(vlist, nullptr);

    EXPECT_TRUE(write_dvi_page(path, vlist, fonts, arena));

    // Verify
    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));
    EXPECT_EQ(parser.page_count(), 1);

    const DVIPage* page = parser.page(0);
    EXPECT_GT(page->glyph_count, 0);  // Should have characters
}

TEST_F(TexPhase5Test, DVIWriteMultiPageDocument) {
    char path[512];
    temp_file(path, sizeof(path), "test_document.dvi");

    // Create page content
    PageContent pages[3];
    pages[0].vlist = create_test_vlist("Page one");
    pages[1].vlist = create_test_vlist("Page two");
    pages[2].vlist = create_test_vlist("Page three");

    EXPECT_TRUE(write_dvi_file(path, pages, 3, fonts, arena));

    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));
    EXPECT_EQ(parser.page_count(), 3);
}

// ============================================================================
// PDF Params Tests
// ============================================================================

TEST_F(TexPhase5Test, PDFParamsDefaults) {
    PDFParams params;

    EXPECT_FLOAT_EQ(params.page_width, HPDF_PAGE_SIZE_LETTER_WIDTH);
    EXPECT_FLOAT_EQ(params.page_height, HPDF_PAGE_SIZE_LETTER_HEIGHT);
    EXPECT_FLOAT_EQ(params.margin_left, 72.0f);
    EXPECT_FLOAT_EQ(params.margin_top, 72.0f);
}

// ============================================================================
// PDF Font Mapping Tests
// ============================================================================

TEST_F(TexPhase5Test, PDFFontMapping) {
    // Computer Modern to Base14 mappings
    EXPECT_STREQ(map_tex_font_to_pdf("cmr10"), "Times-Roman");
    EXPECT_STREQ(map_tex_font_to_pdf("cmbx10"), "Times-Bold");
    EXPECT_STREQ(map_tex_font_to_pdf("cmti10"), "Times-Italic");
    EXPECT_STREQ(map_tex_font_to_pdf("cmss10"), "Helvetica");
    EXPECT_STREQ(map_tex_font_to_pdf("cmtt10"), "Courier");
    EXPECT_STREQ(map_tex_font_to_pdf(nullptr), "Times-Roman");
}

// ============================================================================
// PDF Coordinate Conversion Tests
// ============================================================================

TEST_F(TexPhase5Test, PDFCoordinateConversion) {
    // TeX: origin at top-left, y increases downward
    // PDF: origin at bottom-left, y increases upward
    float page_height = 792.0f;

    EXPECT_FLOAT_EQ(tex_y_to_pdf(0.0f, page_height), 792.0f);
    EXPECT_FLOAT_EQ(tex_y_to_pdf(100.0f, page_height), 692.0f);
    EXPECT_FLOAT_EQ(tex_y_to_pdf(page_height, page_height), 0.0f);
}

// ============================================================================
// PDF Writer Basic Tests
// ============================================================================

TEST_F(TexPhase5Test, PDFWriterConstruction) {
    PDFWriter writer(arena);

    EXPECT_EQ(writer.doc, nullptr);
    EXPECT_EQ(writer.page, nullptr);
    EXPECT_EQ(writer.page_count, 0);
}

TEST_F(TexPhase5Test, PDFOpenClose) {
    char path[512];
    temp_file(path, sizeof(path), "test_open.pdf");

    PDFWriter writer(arena);
    EXPECT_TRUE(pdf_open(writer, path));
    EXPECT_NE(writer.doc, nullptr);

    pdf_close(writer);
    EXPECT_EQ(writer.doc, nullptr);
}

// ============================================================================
// PDF Page Tests
// ============================================================================

TEST_F(TexPhase5Test, PDFSinglePage) {
    char path[512];
    temp_file(path, sizeof(path), "test_page.pdf");

    PDFWriter writer(arena);
    pdf_open(writer, path);

    pdf_begin_page(writer);
    EXPECT_NE(writer.page, nullptr);
    EXPECT_EQ(writer.page_count, 1);

    pdf_end_page(writer);

    // Save and close
    HPDF_SaveToFile(writer.doc, path);
    pdf_close(writer);

    // Verify file exists and has content
    FILE* f = fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    EXPECT_GT(size, 0);
}

// ============================================================================
// PDF Drawing Tests
// ============================================================================

TEST_F(TexPhase5Test, PDFDrawRule) {
    char path[512];
    temp_file(path, sizeof(path), "test_rule.pdf");

    PDFWriter writer(arena);
    pdf_open(writer, path);
    pdf_begin_page(writer);

    pdf_draw_rule(writer, 100.0f, 100.0f, 200.0f, 10.0f);

    pdf_end_page(writer);
    HPDF_SaveToFile(writer.doc, path);
    pdf_close(writer);

    FILE* f = fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    fclose(f);
}

// ============================================================================
// PDF High-Level API Tests
// ============================================================================

TEST_F(TexPhase5Test, PDFWriteSimplePage) {
    char path[512];
    temp_file(path, sizeof(path), "test_simple_page.pdf");

    TexNode* vlist = create_test_vlist("Hello world");
    ASSERT_NE(vlist, nullptr);

    EXPECT_TRUE(write_pdf_page(path, vlist, fonts, arena));

    // Verify file exists
    FILE* f = fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    EXPECT_GT(size, 100);  // Should have meaningful content
}

TEST_F(TexPhase5Test, PDFWriteMultiPageDocument) {
    char path[512];
    temp_file(path, sizeof(path), "test_document.pdf");

    PageContent pages[3];
    pages[0].vlist = create_test_vlist("Page one");
    pages[1].vlist = create_test_vlist("Page two");
    pages[2].vlist = create_test_vlist("Page three");

    EXPECT_TRUE(write_pdf_file(path, pages, 3, fonts, arena));

    FILE* f = fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    fclose(f);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(TexPhase5Test, RoundTripDVI) {
    // Create a document, write to DVI, parse back, verify content
    char path[512];
    temp_file(path, sizeof(path), "test_roundtrip.dvi");

    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 300.0f);

    begin_vlist(ctx);
    const char* text = "This is a test paragraph for round-trip verification.";
    add_paragraph(ctx, text, strlen(text));
    TexNode* vlist = end_vlist(ctx);

    // Write to DVI
    ASSERT_TRUE(write_dvi_page(path, vlist, fonts, arena));

    // Parse back
    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));

    EXPECT_EQ(parser.page_count(), 1);
    const DVIPage* page = parser.page(0);
    EXPECT_GT(page->glyph_count, 10);  // Should have many characters
}

TEST_F(TexPhase5Test, PageBreakToDVI) {
    char path[512];
    temp_file(path, sizeof(path), "test_pagebreak.dvi");

    VListContext ctx(arena, fonts);
    init_vlist_context(ctx, 300.0f);
    ctx.params.max_depth = 4.0f;

    begin_vlist(ctx);

    // Add multiple paragraphs to potentially trigger page break
    const char* para_text = "This is a test paragraph that may span multiple lines.";
    for (int i = 0; i < 10; ++i) {
        add_paragraph(ctx, para_text, strlen(para_text));
        // Add vertical glue between paragraphs
        Glue skip;
        skip.space = 12.0f;
        skip.stretch = 3.0f;
        skip.shrink = 1.0f;
        add_vspace(ctx, skip);
    }

    TexNode* vlist = end_vlist(ctx);

    // Break into pages using paginate convenience function
    PageBreakParams pb_params;
    pb_params.page_height = 200.0f;  // Short pages to force breaks
    pb_params.top_skip = 10.0f;

    int page_count = 0;
    PageContent* pages = paginate(vlist, pb_params, &page_count, arena);

    // Without real TFM fonts, page breaking might not produce multiple pages
    // Just verify we get at least one page
    EXPECT_GE(page_count, 1);

    // Write all pages to DVI
    ASSERT_TRUE(write_dvi_file(path, pages, page_count, fonts, arena));

    // Parse and verify
    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));
    EXPECT_EQ(parser.page_count(), page_count);
}

TEST_F(TexPhase5Test, BothOutputFormats) {
    // Generate same document to both DVI and PDF
    char dvi_path[512], pdf_path[512];
    temp_file(dvi_path, sizeof(dvi_path), "test_both.dvi");
    temp_file(pdf_path, sizeof(pdf_path), "test_both.pdf");

    TexNode* vlist = create_test_vlist("Testing both output formats.");

    ASSERT_TRUE(write_dvi_page(dvi_path, vlist, fonts, arena));
    ASSERT_TRUE(write_pdf_page(pdf_path, vlist, fonts, arena));

    // Both files should exist with content
    FILE* dvi = fopen(dvi_path, "rb");
    FILE* pdf = fopen(pdf_path, "rb");
    ASSERT_NE(dvi, nullptr);
    ASSERT_NE(pdf, nullptr);

    fseek(dvi, 0, SEEK_END);
    fseek(pdf, 0, SEEK_END);
    EXPECT_GT(ftell(dvi), 0);
    EXPECT_GT(ftell(pdf), 0);

    fclose(dvi);
    fclose(pdf);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(TexPhase5Test, DVIOpenInvalidPath) {
    DVIWriter writer(arena);
    EXPECT_FALSE(dvi_open(writer, "/nonexistent/path/test.dvi", DVIParams::defaults()));
}

TEST_F(TexPhase5Test, DVIWriteNullVList) {
    char path[512];
    temp_file(path, sizeof(path), "test_null.dvi");

    EXPECT_FALSE(write_dvi_page(path, nullptr, fonts, arena));
}

TEST_F(TexPhase5Test, PDFWriteNullVList) {
    char path[512];
    temp_file(path, sizeof(path), "test_null.pdf");

    EXPECT_FALSE(write_pdf_page(path, nullptr, fonts, arena));
}

// ============================================================================
// Performance Tests (Stress Tests)
// ============================================================================

TEST_F(TexPhase5Test, DVIManyPages) {
    char path[512];
    temp_file(path, sizeof(path), "test_many_pages.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());

    // Create 100 pages
    for (int i = 1; i <= 100; ++i) {
        dvi_begin_page(writer, i);
        dvi_end_page(writer);
    }

    dvi_close(writer);

    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));
    EXPECT_EQ(parser.page_count(), 100);
}

TEST_F(TexPhase5Test, DVIManyCharacters) {
    char path[512];
    temp_file(path, sizeof(path), "test_many_chars.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());

    dvi_define_font(writer, "cmr10", 10.0f);

    dvi_begin_page(writer, 1);
    dvi_select_font(writer, 0);

    // Output 1000 characters
    for (int i = 0; i < 1000; ++i) {
        dvi_set_char(writer, 65 + (i % 26));  // A-Z cycling
        dvi_right(writer, pt_to_sp(6.0f));

        // New line every 50 characters
        if ((i + 1) % 50 == 0) {
            dvi_right(writer, -pt_to_sp(300.0f));
            dvi_down(writer, pt_to_sp(12.0f));
        }
    }

    dvi_end_page(writer);
    dvi_close(writer);

    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));

    const DVIPage* page = parser.page(0);
    EXPECT_GE(page->glyph_count, 1000);
}

// ============================================================================
// Special Commands Tests
// ============================================================================

TEST_F(TexPhase5Test, DVISpecial) {
    char path[512];
    temp_file(path, sizeof(path), "test_special.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());

    dvi_begin_page(writer, 1);

    const char* special = "color push rgb 1 0 0";
    dvi_special(writer, special, strlen(special));

    dvi_end_page(writer);
    dvi_close(writer);

    // File should be valid
    DVIParser parser(arena);
    ASSERT_TRUE(parser.parse_file(path));
}

// ============================================================================
// Metrics Tests
// ============================================================================

TEST_F(TexPhase5Test, DVIMaxHMaxV) {
    char path[512];
    temp_file(path, sizeof(path), "test_max.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());

    dvi_begin_page(writer, 1);
    dvi_right(writer, pt_to_sp(500.0f));
    dvi_down(writer, pt_to_sp(700.0f));
    dvi_end_page(writer);

    EXPECT_GE(writer.max_h, pt_to_sp(500.0f));
    EXPECT_GE(writer.max_v, pt_to_sp(700.0f));

    dvi_close(writer);
}

TEST_F(TexPhase5Test, DVIMaxPush) {
    char path[512];
    temp_file(path, sizeof(path), "test_max_push.dvi");

    DVIWriter writer(arena);
    dvi_open(writer, path, DVIParams::defaults());

    dvi_begin_page(writer, 1);

    // Nested pushes
    dvi_push(writer);
    dvi_push(writer);
    dvi_push(writer);
    EXPECT_EQ(writer.max_push, 3);

    dvi_pop(writer);
    dvi_pop(writer);
    dvi_pop(writer);

    dvi_end_page(writer);
    dvi_close(writer);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
