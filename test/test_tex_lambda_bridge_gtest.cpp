// test_tex_lambda_bridge_gtest.cpp - Tests for Lambda-TeX document integration
//
// Tests the bridge between Lambda document representation and TeX typesetter

#include <gtest/gtest.h>
#include "lambda/tex/tex_lambda_bridge.hpp"
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lambda/tex/tex_hlist.hpp"
#include "lambda/mark_builder.hpp"
#include "lambda/mark_reader.hpp"
#include "lambda/input/input.hpp"
#include "lib/mempool.h"
#include "lib/arena.h"
#include "lib/arraylist.h"
#include "lib/log.h"

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class TexLambdaBridgeTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    Input* input;
    TFMFontManager* fonts;
    DocumentContext* ctx;

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);

        // Create Input context for MarkBuilder
        input = Input::create(pool, nullptr);

        // Create font manager
        fonts = create_font_manager(arena);

        // Create document context
        ctx = (DocumentContext*)arena_alloc(arena, sizeof(DocumentContext));
        *ctx = DocumentContext::create(arena, fonts);
    }

    void TearDown() override {
        // ctx and fonts are arena-allocated, no delete needed
        if (input) {
            arraylist_free(input->type_list);
        }
        arena_destroy(arena);
        pool_destroy(pool);
    }

    // Helper to create a simple document element
    Item create_doc(std::initializer_list<Item> children) {
        MarkBuilder builder(input);
        ElementBuilder doc = builder.element("doc");
        for (const Item& child : children) {
            doc.child(child);
        }
        return doc.final();
    }

    // Helper to create a paragraph element
    Item create_paragraph(const char* text) {
        MarkBuilder builder(input);
        ElementBuilder p = builder.element("p");
        p.text(text);
        return p.final();
    }

    // Helper to create a heading element
    Item create_heading(int level, const char* text) {
        MarkBuilder builder(input);
        char tag[3] = {'h', (char)('0' + level), '\0'};
        ElementBuilder h = builder.element(tag);
        h.text(text);
        return h.final();
    }

    // Helper to count nodes of a specific class
    int count_nodes(TexNode* root, NodeClass nc) {
        if (!root) return 0;
        int count = (root->node_class == nc) ? 1 : 0;
        for (TexNode* child = root->first_child; child; child = child->next_sibling) {
            count += count_nodes(child, nc);
        }
        return count;
    }

    // Helper to check if VList is non-empty
    bool has_content(TexNode* node) {
        return node && node->first_child;
    }
};

// ============================================================================
// DocumentContext Tests
// ============================================================================

TEST_F(TexLambdaBridgeTest, CreateDefaultContext) {
    // Default context should have reasonable values
    EXPECT_GT(ctx->page_width, 0);
    EXPECT_GT(ctx->page_height, 0);
    EXPECT_GT(ctx->text_width, 0);
    EXPECT_GT(ctx->text_height, 0);
    EXPECT_GT(ctx->base_size_pt, 0);

    // Check that fonts are initialized
    EXPECT_NE(ctx->roman_tfm, nullptr);
}

TEST_F(TexLambdaBridgeTest, CreateCustomContext) {
    // A4 page size
    float a4_width = 595.0f;
    float a4_height = 842.0f;
    float margin = 50.0f;

    DocumentContext custom = DocumentContext::create(arena, fonts,
                                                      a4_width, a4_height,
                                                      margin, margin);

    EXPECT_FLOAT_EQ(custom.page_width, a4_width);
    EXPECT_FLOAT_EQ(custom.page_height, a4_height);
    EXPECT_FLOAT_EQ(custom.text_width, a4_width - 2 * margin);
    EXPECT_FLOAT_EQ(custom.text_height, a4_height - 2 * margin);
}

TEST_F(TexLambdaBridgeTest, LineBreakParams) {
    LineBreakParams params = ctx->line_break_params();
    EXPECT_FLOAT_EQ(params.hsize, ctx->text_width);
    EXPECT_GT(params.tolerance, 0);
}

TEST_F(TexLambdaBridgeTest, BaselineSkip) {
    float skip = ctx->baseline_skip();
    EXPECT_FLOAT_EQ(skip, ctx->base_size_pt * ctx->leading);
}

// ============================================================================
// Basic Document Conversion Tests
// ============================================================================

TEST_F(TexLambdaBridgeTest, ConvertEmptyDocument) {
    Item doc = create_doc({});
    TexNode* result = convert_document(doc, *ctx);

    // Empty document should produce something (possibly empty VList)
    // Just verify it doesn't crash
    EXPECT_NE(result, nullptr);
}

TEST_F(TexLambdaBridgeTest, ConvertSingleParagraph) {
    Item p = create_paragraph("Hello, world!");
    Item doc = create_doc({p});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);

    // Should have some content
    EXPECT_TRUE(has_content(result));
}

TEST_F(TexLambdaBridgeTest, ConvertMultipleParagraphs) {
    Item p1 = create_paragraph("First paragraph.");
    Item p2 = create_paragraph("Second paragraph.");
    Item p3 = create_paragraph("Third paragraph.");
    Item doc = create_doc({p1, p2, p3});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

TEST_F(TexLambdaBridgeTest, ConvertHeading) {
    Item h1 = create_heading(1, "Chapter One");
    Item doc = create_doc({h1});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

TEST_F(TexLambdaBridgeTest, ConvertHeadingAndParagraph) {
    Item h1 = create_heading(1, "Introduction");
    Item p = create_paragraph("This is the introduction.");
    Item doc = create_doc({h1, p});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

// ============================================================================
// Inline Formatting Tests
// ============================================================================

TEST_F(TexLambdaBridgeTest, ConvertEmphasis) {
    MarkBuilder builder(input);

    // Create: <p>This is <em>emphasized</em> text.</p>
    ElementBuilder em = builder.element("em");
    em.text("emphasized");
    Item em_item = em.final();

    ElementBuilder p = builder.element("p");
    p.text("This is ");
    p.child(em_item);
    p.text(" text.");
    Item p_item = p.final();

    Item doc = create_doc({p_item});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

TEST_F(TexLambdaBridgeTest, ConvertStrong) {
    MarkBuilder builder(input);

    // Create: <p>This is <strong>bold</strong> text.</p>
    ElementBuilder strong = builder.element("strong");
    strong.text("bold");
    Item strong_item = strong.final();

    ElementBuilder p = builder.element("p");
    p.text("This is ");
    p.child(strong_item);
    p.text(" text.");
    Item p_item = p.final();

    Item doc = create_doc({p_item});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

TEST_F(TexLambdaBridgeTest, ConvertInlineCode) {
    MarkBuilder builder(input);

    // Create: <p>Use the <code>printf</code> function.</p>
    ElementBuilder code = builder.element("code");
    code.text("printf");
    Item code_item = code.final();

    ElementBuilder p = builder.element("p");
    p.text("Use the ");
    p.child(code_item);
    p.text(" function.");
    Item p_item = p.final();

    Item doc = create_doc({p_item});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

// ============================================================================
// List Tests
// ============================================================================

TEST_F(TexLambdaBridgeTest, ConvertUnorderedList) {
    MarkBuilder builder(input);

    // Create: <ul><li>First</li><li>Second</li></ul>
    ElementBuilder li1 = builder.element("li");
    li1.text("First");
    Item li1_item = li1.final();

    ElementBuilder li2 = builder.element("li");
    li2.text("Second");
    Item li2_item = li2.final();

    ElementBuilder ul = builder.element("ul");
    ul.child(li1_item);
    ul.child(li2_item);
    Item ul_item = ul.final();

    Item doc = create_doc({ul_item});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

TEST_F(TexLambdaBridgeTest, ConvertOrderedList) {
    MarkBuilder builder(input);

    // Create: <ol><li>First</li><li>Second</li></ol>
    ElementBuilder li1 = builder.element("li");
    li1.text("First");
    Item li1_item = li1.final();

    ElementBuilder li2 = builder.element("li");
    li2.text("Second");
    Item li2_item = li2.final();

    ElementBuilder ol = builder.element("ol");
    ol.child(li1_item);
    ol.child(li2_item);
    Item ol_item = ol.final();

    Item doc = create_doc({ol_item});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

// ============================================================================
// Block Element Tests
// ============================================================================

TEST_F(TexLambdaBridgeTest, ConvertBlockquote) {
    MarkBuilder builder(input);

    // Create: <blockquote><p>A wise quote.</p></blockquote>
    ElementBuilder p = builder.element("p");
    p.text("A wise quote.");
    Item p_item = p.final();

    ElementBuilder bq = builder.element("blockquote");
    bq.child(p_item);
    Item bq_item = bq.final();

    Item doc = create_doc({bq_item});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

TEST_F(TexLambdaBridgeTest, ConvertCodeBlock) {
    MarkBuilder builder(input);

    // Create: <pre>int main() {\n    return 0;\n}</pre>
    ElementBuilder pre = builder.element("pre");
    pre.text("int main() {\n    return 0;\n}");
    Item pre_item = pre.final();

    Item doc = create_doc({pre_item});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

TEST_F(TexLambdaBridgeTest, ConvertHorizontalRule) {
    MarkBuilder builder(input);

    // Create: <hr/>
    ElementBuilder hr = builder.element("hr");
    Item hr_item = hr.final();

    Item p = create_paragraph("Before the rule.");
    Item p2 = create_paragraph("After the rule.");

    Item doc = create_doc({p, hr_item, p2});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));

    // Should have a Rule node somewhere
    int rule_count = count_nodes(result, NodeClass::Rule);
    EXPECT_GT(rule_count, 0);
}

// ============================================================================
// Math Tests
// ============================================================================

TEST_F(TexLambdaBridgeTest, ConvertInlineMathInText) {
    // Test text with $...$ inline math
    MarkBuilder builder(input);

    ElementBuilder p = builder.element("p");
    p.text("The equation $E = mc^2$ is famous.");
    Item p_item = p.final();

    Item doc = create_doc({p_item});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

TEST_F(TexLambdaBridgeTest, ConvertMathElement) {
    MarkBuilder builder(input);

    // Create: <math display="block">a^2 + b^2 = c^2</math>
    ElementBuilder math = builder.element("math");
    math.attr("display", "block");
    math.text("a^2 + b^2 = c^2");
    Item math_item = math.final();

    Item doc = create_doc({math_item});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
}

// ============================================================================
// Complex Document Tests
// ============================================================================

TEST_F(TexLambdaBridgeTest, ConvertArticle) {
    MarkBuilder builder(input);

    // Create a more complex document
    Item h1 = create_heading(1, "Introduction");
    Item p1 = create_paragraph("This is the introduction to our paper.");
    Item p2 = create_paragraph("We present an important result.");

    Item h2 = create_heading(2, "Methods");
    Item p3 = create_paragraph("We used the following methods.");

    // Create list
    ElementBuilder li1 = builder.element("li");
    li1.text("Method one");
    Item li1_item = li1.final();

    ElementBuilder li2 = builder.element("li");
    li2.text("Method two");
    Item li2_item = li2.final();

    ElementBuilder ul = builder.element("ul");
    ul.child(li1_item);
    ul.child(li2_item);
    Item ul_item = ul.final();

    Item h3 = create_heading(2, "Results");
    Item p4 = create_paragraph("Our results show significant improvement.");

    Item doc = create_doc({h1, p1, p2, h2, p3, ul_item, h3, p4});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

TEST_F(TexLambdaBridgeTest, ConvertNestedStructure) {
    MarkBuilder builder(input);

    // Create nested sections
    Item p = create_paragraph("Inner content.");

    ElementBuilder section = builder.element("section");
    section.child(p);
    Item section_item = section.final();

    ElementBuilder article = builder.element("article");
    article.child(section_item);
    Item article_item = article.final();

    Item doc = create_doc({article_item});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

// ============================================================================
// High-Level API Tests
// ============================================================================

TEST_F(TexLambdaBridgeTest, TypesetDocumentVList) {
    Item h1 = create_heading(1, "Title");
    Item p1 = create_paragraph("First paragraph of the document.");
    Item p2 = create_paragraph("Second paragraph with more text to create some length.");
    Item doc = create_doc({h1, p1, p2});

    TexNode* vlist = typeset_document_vlist(doc, *ctx);
    EXPECT_NE(vlist, nullptr);
    EXPECT_TRUE(has_content(vlist));
}

TEST_F(TexLambdaBridgeTest, TypesetDocument) {
    Item h1 = create_heading(1, "Title");
    Item p1 = create_paragraph("First paragraph of the document.");
    Item doc = create_doc({h1, p1});

    TexNode* result = typeset_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

TEST_F(TexLambdaBridgeTest, BreakIntoPages) {
    // Create a longer document
    MarkBuilder builder(input);

    ElementBuilder doc_builder = builder.element("doc");

    Item h1 = create_heading(1, "A Longer Document");
    doc_builder.child(h1);

    // Add many paragraphs to potentially span pages
    for (int i = 0; i < 20; i++) {
        char text[128];
        snprintf(text, sizeof(text),
                 "This is paragraph number %d. It contains some text to fill the page.", i + 1);
        Item p = create_paragraph(text);
        doc_builder.child(p);
    }

    Item doc = doc_builder.final();

    // First typeset the document
    TexNode* vlist = typeset_document(doc, *ctx);
    EXPECT_NE(vlist, nullptr);

    // Then break into pages
    PageList pages = break_into_pages(vlist, *ctx);

    // Should have at least one page
    EXPECT_GE(pages.page_count, 1);

    // Each page should exist
    for (int i = 0; i < pages.page_count; i++) {
        EXPECT_NE(pages.pages[i], nullptr);
    }
}

// ============================================================================
// Text Processing Tests
// ============================================================================

TEST_F(TexLambdaBridgeTest, HListCreation) {
    // Test creating an HList using HListContext
    HListContext hctx(arena, fonts);
    hctx.current_tfm = ctx->roman_tfm;
    hctx.current_font = ctx->roman_font;

    const char* text = "Hello world";
    TexNode* hlist = text_to_hlist(text, strlen(text), hctx);

    EXPECT_NE(hlist, nullptr);
    EXPECT_TRUE(has_content(hlist));

    // Count character nodes
    int char_count = count_nodes(hlist, NodeClass::Char);
    EXPECT_GT(char_count, 0);
}

TEST_F(TexLambdaBridgeTest, TextWithLigatures) {
    // "fi" and "fl" should produce ligatures
    HListContext hctx(arena, fonts);
    hctx.current_tfm = ctx->roman_tfm;
    hctx.current_font = ctx->roman_font;

    const char* text = "office floor";
    TexNode* hlist = text_to_hlist(text, strlen(text), hctx);

    EXPECT_NE(hlist, nullptr);
    EXPECT_TRUE(has_content(hlist));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(TexLambdaBridgeTest, NullDocument) {
    Item null_doc = ItemNull;
    TexNode* result = convert_document(null_doc, *ctx);
    // Should return an empty VList, not crash
    EXPECT_NE(result, nullptr);
}

TEST_F(TexLambdaBridgeTest, EmptyParagraph) {
    MarkBuilder builder(input);

    ElementBuilder p = builder.element("p");
    Item p_item = p.final();  // Empty paragraph

    Item doc = create_doc({p_item});

    TexNode* result = convert_document(doc, *ctx);
    // Should handle gracefully
    EXPECT_NE(result, nullptr);
}

TEST_F(TexLambdaBridgeTest, WhitespaceOnlyParagraph) {
    MarkBuilder builder(input);

    ElementBuilder p = builder.element("p");
    p.text("   \n\t  ");  // Whitespace only
    Item p_item = p.final();

    Item doc = create_doc({p_item});

    TexNode* result = convert_document(doc, *ctx);
    // Should handle gracefully
    EXPECT_NE(result, nullptr);
}

TEST_F(TexLambdaBridgeTest, DeeplyNestedContent) {
    MarkBuilder builder(input);

    // Create deeply nested emphasis
    // <p><em><strong><em>text</em></strong></em></p>
    ElementBuilder inner_em = builder.element("em");
    inner_em.text("deeply nested");
    Item inner_em_item = inner_em.final();

    ElementBuilder strong = builder.element("strong");
    strong.child(inner_em_item);
    Item strong_item = strong.final();

    ElementBuilder outer_em = builder.element("em");
    outer_em.child(strong_item);
    Item outer_em_item = outer_em.final();

    ElementBuilder p = builder.element("p");
    p.child(outer_em_item);
    Item p_item = p.final();

    Item doc = create_doc({p_item});

    TexNode* result = convert_document(doc, *ctx);
    EXPECT_NE(result, nullptr);
    EXPECT_TRUE(has_content(result));
}

// ============================================================================
// Section Numbering Tests
// ============================================================================

TEST_F(TexLambdaBridgeTest, SectionNumbering) {
    // After converting headings, section state should be updated
    SectionState state;

    EXPECT_EQ(state.chapter, 0);
    EXPECT_EQ(state.section, 0);

    state.increment(1);
    EXPECT_EQ(state.chapter, 1);

    state.increment(2);
    EXPECT_EQ(state.section, 1);

    state.increment(2);
    EXPECT_EQ(state.section, 2);

    // New chapter should reset section
    state.increment(1);
    EXPECT_EQ(state.chapter, 2);
    EXPECT_EQ(state.section, 0);
}

// ============================================================================
// Format State Tests
// ============================================================================

TEST_F(TexLambdaBridgeTest, FormatState) {
    FormatState state;

    EXPECT_EQ(state.style, TextStyle::Roman);
    EXPECT_EQ(state.list_depth, 0);
    EXPECT_FALSE(state.in_math);

    state.style = TextStyle::Bold;
    EXPECT_EQ(state.style, TextStyle::Bold);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
