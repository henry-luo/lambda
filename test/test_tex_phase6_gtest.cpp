// test_tex_phase6_gtest.cpp - Unit tests for SVG and PNG output (Phase 6)
//
// Tests the tex_svg_out.hpp and tex_png_out.hpp implementations.

#include <gtest/gtest.h>
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lambda/tex/tex_hlist.hpp"
#include "lambda/tex/tex_linebreak.hpp"
#include "lambda/tex/tex_vlist.hpp"
#include "lambda/tex/tex_pagebreak.hpp"
#include "lambda/tex/tex_svg_out.hpp"
#include "lambda/tex/tex_png_out.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class TexPhase6Test : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    TFMFontManager* fonts;
    FT_Library ft_lib;
    char temp_dir[256];

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        fonts = create_font_manager(arena);

        // Initialize FreeType for PNG tests
        FT_Init_FreeType(&ft_lib);

        // Create temp directory for test outputs
        snprintf(temp_dir, sizeof(temp_dir), "/tmp/tex_phase6_test_%d", getpid());
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", temp_dir);
        system(cmd);
    }

    void TearDown() override {
        // Clean up temp files
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", temp_dir);
        system(cmd);

        FT_Done_FreeType(ft_lib);
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

    // Helper to create a simple hlist with characters
    TexNode* create_test_hlist(const char* text) {
        HListContext ctx(arena, fonts);
        ctx.current_font = FontSpec("cmr10", 10.0f, nullptr, 0);
        ctx.current_tfm = fonts->get_font("cmr10");
        return text_to_hlist(text, strlen(text), ctx);
    }

    // Helper to get temp file path
    void temp_file(char* buf, size_t size, const char* name) {
        snprintf(buf, size, "%s/%s", temp_dir, name);
    }
};

// ============================================================================
// SVG Params Tests
// ============================================================================

TEST_F(TexPhase6Test, SVGParamsDefaults) {
    SVGParams params = SVGParams::defaults();

    EXPECT_EQ(params.viewport_width, 0);  // Auto-calculated
    EXPECT_EQ(params.viewport_height, 0);
    EXPECT_FLOAT_EQ(params.scale, 1.0f);
    EXPECT_TRUE(params.indent);
    EXPECT_TRUE(params.include_metadata);
}

// ============================================================================
// SVG Helper Function Tests
// ============================================================================

TEST_F(TexPhase6Test, SVGFontFamily) {
    // Test font family mapping - returns CSS font stacks
    // Just check that each returns non-null and contains expected font name
    const char* cmr = svg_font_family("cmr10");
    ASSERT_NE(cmr, nullptr);
    EXPECT_NE(strstr(cmr, "CMU Serif"), nullptr);

    const char* cmtt = svg_font_family("cmtt10");
    ASSERT_NE(cmtt, nullptr);
    EXPECT_NE(strstr(cmtt, "Typewriter"), nullptr);

    const char* cmmi = svg_font_family("cmmi10");
    ASSERT_NE(cmmi, nullptr);
    EXPECT_NE(strstr(cmmi, "Italic"), nullptr);

    // Unknown font defaults to serif
    const char* unknown = svg_font_family("unknown");
    ASSERT_NE(unknown, nullptr);
    EXPECT_NE(strstr(unknown, "serif"), nullptr);
}

TEST_F(TexPhase6Test, SVGColorString) {
    char buf[16];

    // 0 = transparent
    svg_color_string(0x00000000, buf, sizeof(buf));
    EXPECT_STREQ(buf, "transparent");

    // Non-zero colors produce uppercase hex
    svg_color_string(0xFF0000FF, buf, sizeof(buf));
    EXPECT_STREQ(buf, "#FF0000");

    svg_color_string(0x00FF00FF, buf, sizeof(buf));
    EXPECT_STREQ(buf, "#00FF00");

    svg_color_string(0x0000FFFF, buf, sizeof(buf));
    EXPECT_STREQ(buf, "#0000FF");
}

// ============================================================================
// SVG Writer Tests
// ============================================================================

TEST_F(TexPhase6Test, SVGWriterInit) {
    SVGWriter writer;
    EXPECT_TRUE(svg_init(writer, arena, SVGParams::defaults()));
    EXPECT_NE(writer.output, nullptr);
    EXPECT_NE(writer.arena, nullptr);
}

TEST_F(TexPhase6Test, SVGWriteDocument) {
    TexNode* hlist = create_test_hlist("Hello");
    ASSERT_NE(hlist, nullptr);

    SVGWriter writer;
    svg_init(writer, arena, SVGParams::defaults());

    EXPECT_TRUE(svg_write_document(writer, hlist));

    const char* output = svg_get_output(writer);
    ASSERT_NE(output, nullptr);

    // Should have basic SVG structure
    EXPECT_NE(strstr(output, "<svg"), nullptr);
    EXPECT_NE(strstr(output, "</svg>"), nullptr);
    EXPECT_NE(strstr(output, "xmlns=\"http://www.w3.org/2000/svg\""), nullptr);
}

// ============================================================================
// SVG File Output Tests
// ============================================================================

TEST_F(TexPhase6Test, SVGWriteToFile) {
    char path[512];
    temp_file(path, sizeof(path), "test_output.svg");

    TexNode* hlist = create_test_hlist("Test");
    ASSERT_NE(hlist, nullptr);

    SVGWriter writer;
    svg_init(writer, arena, SVGParams::defaults());
    svg_write_document(writer, hlist);

    EXPECT_TRUE(svg_write_to_file(writer, path));

    // Verify file exists and has content
    FILE* f = fopen(path, "r");
    ASSERT_NE(f, nullptr);

    char buf[1024];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    buf[len] = '\0';
    fclose(f);

    EXPECT_GT(len, 0u);
    EXPECT_NE(strstr(buf, "<svg"), nullptr);
}

TEST_F(TexPhase6Test, SVGWriteToInvalidPath) {
    TexNode* hlist = create_test_hlist("X");
    ASSERT_NE(hlist, nullptr);

    SVGWriter writer;
    svg_init(writer, arena, SVGParams::defaults());
    svg_write_document(writer, hlist);

    // Should fail gracefully
    EXPECT_FALSE(svg_write_to_file(writer, "/nonexistent/path/test.svg"));
}

// ============================================================================
// SVG Convenience API Tests
// ============================================================================

TEST_F(TexPhase6Test, SVGRenderToString) {
    TexNode* hlist = create_test_hlist("Hi");
    ASSERT_NE(hlist, nullptr);

    const char* svg = svg_render_to_string(hlist, nullptr, arena);
    ASSERT_NE(svg, nullptr);

    EXPECT_NE(strstr(svg, "<svg"), nullptr);
    EXPECT_NE(strstr(svg, "</svg>"), nullptr);
}

TEST_F(TexPhase6Test, SVGRenderToFile) {
    char path[512];
    temp_file(path, sizeof(path), "test_render.svg");

    TexNode* hlist = create_test_hlist("Test");
    ASSERT_NE(hlist, nullptr);

    EXPECT_TRUE(svg_render_to_file(hlist, path, nullptr, arena));

    FILE* f = fopen(path, "r");
    ASSERT_NE(f, nullptr);
    fclose(f);
}

// ============================================================================
// PNG Params Tests
// ============================================================================

TEST_F(TexPhase6Test, PNGParamsDefaults) {
    PNGParams params = PNGParams::defaults();

    EXPECT_FLOAT_EQ(params.dpi, 150.0f);
    EXPECT_EQ(params.background, 0xFFFFFFFF);  // White
    EXPECT_EQ(params.text_color, 0x000000FF);  // Black
    EXPECT_TRUE(params.antialias);
    EXPECT_FLOAT_EQ(params.margin_px, 10.0f);
}

TEST_F(TexPhase6Test, PNGParamsTransparent) {
    PNGParams params = PNGParams::transparent();

    EXPECT_EQ(params.background, 0x00000000);  // Transparent
}

TEST_F(TexPhase6Test, PNGParamsHighRes) {
    PNGParams params = PNGParams::highres();

    EXPECT_FLOAT_EQ(params.dpi, 300.0f);
}

// ============================================================================
// PNG Image Tests
// ============================================================================

TEST_F(TexPhase6Test, PNGCreateImage) {
    PNGImage* img = png_create_image(arena, 100, 50);
    ASSERT_NE(img, nullptr);

    EXPECT_EQ(img->width, 100);
    EXPECT_EQ(img->height, 50);
    EXPECT_NE(img->pixels, nullptr);
    EXPECT_EQ(img->stride, 400);  // 100 * 4 bytes per pixel
}

TEST_F(TexPhase6Test, PNGClear) {
    PNGImage* img = png_create_image(arena, 10, 10);
    ASSERT_NE(img, nullptr);

    png_clear(img, 0xFF0000FF);  // Red

    // Check first pixel
    EXPECT_EQ(img->pixels[0], 0xFF);  // R
    EXPECT_EQ(img->pixels[1], 0x00);  // G
    EXPECT_EQ(img->pixels[2], 0x00);  // B
    EXPECT_EQ(img->pixels[3], 0xFF);  // A
}

// ============================================================================
// PNG Writer Tests
// ============================================================================

TEST_F(TexPhase6Test, PNGWriterInit) {
    PNGWriter writer;
    EXPECT_TRUE(png_init(writer, arena, ft_lib, PNGParams::defaults()));
    EXPECT_NE(writer.arena, nullptr);
}

TEST_F(TexPhase6Test, PNGRender) {
    TexNode* hlist = create_test_hlist("ABC");
    ASSERT_NE(hlist, nullptr);

    PNGWriter writer;
    png_init(writer, arena, ft_lib, PNGParams::defaults());

    PNGImage* image = png_render(writer, hlist);
    ASSERT_NE(image, nullptr);
    EXPECT_GT(image->width, 0);
    EXPECT_GT(image->height, 0);
}

// ============================================================================
// PNG File Output Tests
// ============================================================================

TEST_F(TexPhase6Test, PNGWriteToFile) {
    char path[512];
    temp_file(path, sizeof(path), "test_output.png");

    TexNode* hlist = create_test_hlist("Test");
    ASSERT_NE(hlist, nullptr);

    PNGWriter writer;
    png_init(writer, arena, ft_lib, PNGParams::defaults());

    PNGImage* image = png_render(writer, hlist);
    ASSERT_NE(image, nullptr);

    EXPECT_TRUE(png_write_to_file(image, path));

    // Verify file exists and has PNG signature
    FILE* f = fopen(path, "rb");
    ASSERT_NE(f, nullptr);

    unsigned char sig[8];
    EXPECT_EQ(fread(sig, 1, 8, f), 8u);
    fclose(f);

    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    EXPECT_EQ(sig[0], 0x89);
    EXPECT_EQ(sig[1], 'P');
    EXPECT_EQ(sig[2], 'N');
    EXPECT_EQ(sig[3], 'G');
}

TEST_F(TexPhase6Test, PNGWriteToInvalidPath) {
    TexNode* hlist = create_test_hlist("X");
    ASSERT_NE(hlist, nullptr);

    PNGWriter writer;
    png_init(writer, arena, ft_lib, PNGParams::defaults());

    PNGImage* image = png_render(writer, hlist);
    ASSERT_NE(image, nullptr);

    // Should fail gracefully
    EXPECT_FALSE(png_write_to_file(image, "/nonexistent/path/test.png"));
}

// ============================================================================
// PNG Convenience API Tests
// ============================================================================

TEST_F(TexPhase6Test, PNGRenderToFile) {
    char path[512];
    temp_file(path, sizeof(path), "test_render.png");

    TexNode* hlist = create_test_hlist("Hello");
    ASSERT_NE(hlist, nullptr);

    EXPECT_TRUE(png_render_to_file(hlist, path, nullptr, arena, ft_lib));

    FILE* f = fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    fclose(f);
}

TEST_F(TexPhase6Test, PNGEncode) {
    TexNode* hlist = create_test_hlist("Enc");
    ASSERT_NE(hlist, nullptr);

    PNGWriter writer;
    png_init(writer, arena, ft_lib, PNGParams::defaults());

    PNGImage* image = png_render(writer, hlist);
    ASSERT_NE(image, nullptr);

    size_t size = 0;
    uint8_t* data = png_encode(image, &size, arena);

    ASSERT_NE(data, nullptr);
    EXPECT_GT(size, 0u);

    // Verify PNG signature
    EXPECT_EQ(data[0], 0x89);
    EXPECT_EQ(data[1], 'P');
    EXPECT_EQ(data[2], 'N');
    EXPECT_EQ(data[3], 'G');
}

// ============================================================================
// DPI Scaling Tests
// ============================================================================

TEST_F(TexPhase6Test, PNGDPIScaling) {
    TexNode* hlist = create_test_hlist("DPI");
    ASSERT_NE(hlist, nullptr);

    // Create two images at different DPI
    PNGWriter writer1, writer2;

    PNGParams params1 = PNGParams::defaults();
    params1.dpi = 72.0f;
    png_init(writer1, arena, ft_lib, params1);

    PNGParams params2 = PNGParams::defaults();
    params2.dpi = 144.0f;
    png_init(writer2, arena, ft_lib, params2);

    PNGImage* img1 = png_render(writer1, hlist);
    PNGImage* img2 = png_render(writer2, hlist);

    ASSERT_NE(img1, nullptr);
    ASSERT_NE(img2, nullptr);

    // Higher DPI should produce larger image
    EXPECT_GT(img2->width, img1->width);
    EXPECT_GT(img2->height, img1->height);
}

// ============================================================================
// Integration Tests - SVG and PNG from same source
// ============================================================================

TEST_F(TexPhase6Test, SVGAndPNGFromSameSource) {
    char svg_path[512], png_path[512];
    temp_file(svg_path, sizeof(svg_path), "integrated.svg");
    temp_file(png_path, sizeof(png_path), "integrated.png");

    TexNode* hlist = create_test_hlist("Math");
    ASSERT_NE(hlist, nullptr);

    // Generate both formats
    EXPECT_TRUE(svg_render_to_file(hlist, svg_path, nullptr, arena));
    EXPECT_TRUE(png_render_to_file(hlist, png_path, nullptr, arena, ft_lib));

    // Both files should exist
    FILE* svg_f = fopen(svg_path, "r");
    FILE* png_f = fopen(png_path, "rb");

    ASSERT_NE(svg_f, nullptr);
    ASSERT_NE(png_f, nullptr);

    fclose(svg_f);
    fclose(png_f);
}

TEST_F(TexPhase6Test, VlistToSVG) {
    TexNode* vlist = create_test_vlist("A full paragraph of text.");
    ASSERT_NE(vlist, nullptr);

    const char* svg = svg_render_to_string(vlist, nullptr, arena);
    ASSERT_NE(svg, nullptr);
    EXPECT_NE(strstr(svg, "<svg"), nullptr);
}

TEST_F(TexPhase6Test, VlistToPNG) {
    char path[512];
    temp_file(path, sizeof(path), "vlist.png");

    TexNode* vlist = create_test_vlist("Another paragraph here.");
    ASSERT_NE(vlist, nullptr);

    EXPECT_TRUE(png_render_to_file(vlist, path, nullptr, arena, ft_lib));

    FILE* f = fopen(path, "rb");
    ASSERT_NE(f, nullptr);
    fclose(f);
}
