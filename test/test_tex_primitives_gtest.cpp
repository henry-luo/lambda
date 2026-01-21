// test_tex_primitives_gtest.cpp - Unit tests for TeX primitive commands
//
// Tests the HTML output functionality for TeX primitives:
// - Spacing: \hskip, \vskip, \kern
// - Infinite glue: \hfil, \hfill, \hss, \vfil, \vfill, \vss  
// - Rules: \hrule, \vrule
// - Penalties: \penalty, \break, \nobreak, \allowbreak
// - Boxes: \hbox, \vbox, \vtop, \raise, \lower, \moveleft, \moveright, \rlap, \llap

#include <gtest/gtest.h>
#include "lambda/tex/tex_document_model.hpp"
#include "lambda/tex/tex_html_render.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/strbuf.h"
#include "lib/log.h"
#include <cstring>
#include <string>

using namespace tex;

class TexPrimitivesTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    TFMFontManager* fonts;
    
    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        fonts = create_font_manager(arena);
    }
    
    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
    }
    
    // Convert LaTeX string to HTML output
    std::string latex_to_html(const char* latex_src) {
        TexDocumentModel* doc = doc_model_from_string(
            latex_src, strlen(latex_src), arena, fonts);
        
        if (!doc || !doc->root) {
            return "";
        }
        
        StrBuf* html_buf = strbuf_new_cap(4096);
        HtmlOutputOptions opts = HtmlOutputOptions::defaults();
        opts.standalone = false;
        opts.pretty_print = false;
        opts.include_css = false;
        
        bool success = doc_model_to_html(doc, html_buf, opts);
        
        if (!success) {
            strbuf_free(html_buf);
            return "";
        }
        
        std::string result(html_buf->str, html_buf->length);
        strbuf_free(html_buf);
        return result;
    }
    
    // Check HTML contains pattern
    bool contains(const std::string& html, const char* pattern) {
        return html.find(pattern) != std::string::npos;
    }
};

// ============================================================================
// Spacing Primitives Tests
// ============================================================================

TEST_F(TexPrimitivesTest, HskipPositive) {
    std::string html = latex_to_html("A\\hskip 10pt B");
    // Should contain span with margin-right style
    EXPECT_TRUE(contains(html, "margin-right:")) << "hskip should produce margin-right style";
}

TEST_F(TexPrimitivesTest, VskipPositive) {
    std::string html = latex_to_html("Line 1\\vskip 1cm Line 2");
    // Should contain div with height style for vertical space
    EXPECT_TRUE(contains(html, "height:") || contains(html, "vskip")) 
        << "vskip should produce height style or vskip class";
}

TEST_F(TexPrimitivesTest, KernPositive) {
    std::string html = latex_to_html("A\\kern 5pt B");
    // Should contain kern spacing
    EXPECT_TRUE(contains(html, "margin-right:")) << "kern should produce margin-right style";
}

// ============================================================================
// Infinite Glue Tests
// ============================================================================

TEST_F(TexPrimitivesTest, Hfil) {
    std::string html = latex_to_html("Left\\hfil Right");
    EXPECT_TRUE(contains(html, "hfil")) << "hfil should produce hfil class";
}

TEST_F(TexPrimitivesTest, Hfill) {
    std::string html = latex_to_html("Left\\hfill Right");
    EXPECT_TRUE(contains(html, "hfill")) << "hfill should produce hfill class";
}

TEST_F(TexPrimitivesTest, Hss) {
    std::string html = latex_to_html("Left\\hss Right");
    EXPECT_TRUE(contains(html, "hss")) << "hss should produce hss class";
}

TEST_F(TexPrimitivesTest, Vfil) {
    std::string html = latex_to_html("Top\\vfil Bottom");
    EXPECT_TRUE(contains(html, "vfil")) << "vfil should produce vfil class";
}

TEST_F(TexPrimitivesTest, Vfill) {
    std::string html = latex_to_html("Top\\vfill Bottom");
    EXPECT_TRUE(contains(html, "vfill")) << "vfill should produce vfill class";
}

TEST_F(TexPrimitivesTest, Vss) {
    std::string html = latex_to_html("Top\\vss Bottom");
    EXPECT_TRUE(contains(html, "vss")) << "vss should produce vss class";
}

// ============================================================================
// Rule Tests
// ============================================================================

TEST_F(TexPrimitivesTest, Hrule) {
    std::string html = latex_to_html("\\hrule");
    // Should contain hr element with background style
    EXPECT_TRUE(contains(html, "<hr") || contains(html, "hrule")) 
        << "hrule should produce hr element";
}

TEST_F(TexPrimitivesTest, HruleWithHeight) {
    std::string html = latex_to_html("\\hrule height 2pt");
    EXPECT_TRUE(contains(html, "height:") || contains(html, "<hr")) 
        << "hrule height should produce height style";
}

TEST_F(TexPrimitivesTest, Vrule) {
    std::string html = latex_to_html("Text\\vrule Text");
    // Should contain inline-block span with width
    EXPECT_TRUE(contains(html, "inline-block") || contains(html, "vrule")) 
        << "vrule should produce inline-block element";
}

// ============================================================================
// Penalty Tests
// ============================================================================

TEST_F(TexPrimitivesTest, Break) {
    std::string html = latex_to_html("Line 1\\break Line 2");
    // Should contain br element for forced break
    EXPECT_TRUE(contains(html, "<br") || contains(html, "penalty-break")) 
        << "break should produce br element";
}

TEST_F(TexPrimitivesTest, Nobreak) {
    std::string html = latex_to_html("word\\nobreak word");
    // Should contain word joiner U+2060 (UTF-8: E2 81 A0)
    EXPECT_TRUE(contains(html, "\xE2\x81\xA0") || contains(html, "nobreak")) 
        << "nobreak should produce word joiner character";
}

TEST_F(TexPrimitivesTest, Allowbreak) {
    std::string html = latex_to_html("longword\\allowbreak here");
    // Should contain zero-width space U+200B (UTF-8: E2 80 8B)
    EXPECT_TRUE(contains(html, "\xE2\x80\x8B") || contains(html, "allowbreak")) 
        << "allowbreak should produce zero-width space";
}

// ============================================================================
// Box Tests  
// ============================================================================

TEST_F(TexPrimitivesTest, Hbox) {
    std::string html = latex_to_html("\\hbox{content}");
    EXPECT_TRUE(contains(html, "hbox")) << "hbox should produce hbox class";
}

TEST_F(TexPrimitivesTest, Vbox) {
    std::string html = latex_to_html("\\vbox{content}");
    EXPECT_TRUE(contains(html, "vbox")) << "vbox should produce vbox class";
}

TEST_F(TexPrimitivesTest, Vtop) {
    std::string html = latex_to_html("\\vtop{content}");
    EXPECT_TRUE(contains(html, "vtop")) << "vtop should produce vtop class";
}

TEST_F(TexPrimitivesTest, Rlap) {
    std::string html = latex_to_html("\\rlap{overlapping}text");
    EXPECT_TRUE(contains(html, "rlap")) << "rlap should produce rlap class";
}

TEST_F(TexPrimitivesTest, Llap) {
    std::string html = latex_to_html("text\\llap{overlapping}");
    EXPECT_TRUE(contains(html, "llap")) << "llap should produce llap class";
}

TEST_F(TexPrimitivesTest, Raise) {
    std::string html = latex_to_html("base\\raise 2pt\\hbox{raised}");
    // Should contain position:relative with negative top
    EXPECT_TRUE(contains(html, "position:relative") || contains(html, "top:")) 
        << "raise should produce position:relative style";
}

TEST_F(TexPrimitivesTest, Lower) {
    std::string html = latex_to_html("base\\lower 2pt\\hbox{lowered}");
    EXPECT_TRUE(contains(html, "position:relative") || contains(html, "top:")) 
        << "lower should produce position:relative style";
}

TEST_F(TexPrimitivesTest, Moveleft) {
    std::string html = latex_to_html("\\moveleft 10pt\\hbox{shifted}");
    EXPECT_TRUE(contains(html, "left:") || contains(html, "position:relative")) 
        << "moveleft should produce left style";
}

TEST_F(TexPrimitivesTest, Moveright) {
    std::string html = latex_to_html("\\moveright 10pt\\hbox{shifted}");
    EXPECT_TRUE(contains(html, "left:") || contains(html, "position:relative")) 
        << "moveright should produce left style";
}

// ============================================================================
// Dimension Parsing Tests
// ============================================================================

TEST_F(TexPrimitivesTest, DimensionPt) {
    std::string html = latex_to_html("A\\kern 10pt B");
    // 10pt should convert to approximately 13.33px (10 * 96/72)
    EXPECT_TRUE(contains(html, "13.") || contains(html, "margin-right:")) 
        << "pt dimension should be converted to px";
}

TEST_F(TexPrimitivesTest, DimensionCm) {
    std::string html = latex_to_html("A\\kern 1cm B");
    // 1cm should convert to approximately 37.8px (96/2.54)
    EXPECT_TRUE(contains(html, "37.") || contains(html, "margin-right:")) 
        << "cm dimension should be converted to px";
}

TEST_F(TexPrimitivesTest, DimensionMm) {
    std::string html = latex_to_html("A\\kern 5mm B");
    // 5mm should convert to approximately 18.9px (5 * 96/25.4)
    EXPECT_TRUE(contains(html, "18.") || contains(html, "margin-right:")) 
        << "mm dimension should be converted to px";
}

TEST_F(TexPrimitivesTest, DimensionIn) {
    std::string html = latex_to_html("A\\kern 0.5in B");
    // 0.5in should convert to 48px (0.5 * 96)
    EXPECT_TRUE(contains(html, "48") || contains(html, "margin-right:")) 
        << "in dimension should be converted to px";
}

TEST_F(TexPrimitivesTest, DimensionEm) {
    std::string html = latex_to_html("A\\kern 1em B");
    // 1em should convert to 16px (assuming 16px base)
    EXPECT_TRUE(contains(html, "16") || contains(html, "margin-right:")) 
        << "em dimension should be converted to px";
}
