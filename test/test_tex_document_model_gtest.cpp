// test_tex_document_model_gtest.cpp - Unit tests for document model layer
//
// Tests the tex_document_model.hpp/cpp implementation which provides
// the intermediate document model for the unified LaTeX pipeline.

#include <gtest/gtest.h>
#include "lambda/tex/tex_document_model.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/strbuf.h"
#include "lib/log.h"

using namespace tex;

class TexDocumentModelTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
    }

    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
    }
};

// ============================================================================
// Document Model Creation Tests
// ============================================================================

TEST_F(TexDocumentModelTest, CreateEmptyDocument) {
    TexDocumentModel* doc = doc_model_create(arena);
    
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->arena, arena);
    EXPECT_STREQ(doc->document_class, "article");
    EXPECT_EQ(doc->title, nullptr);
    EXPECT_EQ(doc->author, nullptr);
    EXPECT_EQ(doc->root, nullptr);
    EXPECT_EQ(doc->label_count, 0);
    EXPECT_EQ(doc->macro_count, 0);
}

// ============================================================================
// Element Allocation Tests
// ============================================================================

TEST_F(TexDocumentModelTest, AllocateElement) {
    DocElement* elem = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->type, DocElemType::PARAGRAPH);
    EXPECT_EQ(elem->flags, 0);
    EXPECT_EQ(elem->first_child, nullptr);
    EXPECT_EQ(elem->last_child, nullptr);
    EXPECT_EQ(elem->next_sibling, nullptr);
    EXPECT_EQ(elem->parent, nullptr);
}

TEST_F(TexDocumentModelTest, AllocateAllElementTypes) {
    // Test that all element types can be allocated
    DocElemType types[] = {
        DocElemType::PARAGRAPH,
        DocElemType::HEADING,
        DocElemType::LIST,
        DocElemType::LIST_ITEM,
        DocElemType::TABLE,
        DocElemType::TABLE_ROW,
        DocElemType::TABLE_CELL,
        DocElemType::FIGURE,
        DocElemType::BLOCKQUOTE,
        DocElemType::CODE_BLOCK,
        DocElemType::MATH_INLINE,
        DocElemType::MATH_DISPLAY,
        DocElemType::TEXT_SPAN,
        DocElemType::TEXT_RUN,
        DocElemType::LINK,
        DocElemType::IMAGE,
        DocElemType::DOCUMENT,
        DocElemType::SECTION,
    };
    
    for (auto type : types) {
        DocElement* elem = doc_alloc_element(arena, type);
        ASSERT_NE(elem, nullptr) << "Failed to allocate " << doc_elem_type_name(type);
        EXPECT_EQ(elem->type, type);
    }
}

// ============================================================================
// Tree Building Tests
// ============================================================================

TEST_F(TexDocumentModelTest, AppendChild) {
    DocElement* parent = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    DocElement* child1 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    DocElement* child2 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    
    doc_append_child(parent, child1);
    
    EXPECT_EQ(parent->first_child, child1);
    EXPECT_EQ(parent->last_child, child1);
    EXPECT_EQ(child1->parent, parent);
    EXPECT_EQ(child1->next_sibling, nullptr);
    
    doc_append_child(parent, child2);
    
    EXPECT_EQ(parent->first_child, child1);
    EXPECT_EQ(parent->last_child, child2);
    EXPECT_EQ(child1->next_sibling, child2);
    EXPECT_EQ(child2->parent, parent);
    EXPECT_EQ(child2->next_sibling, nullptr);
}

TEST_F(TexDocumentModelTest, CreateTextElement) {
    const char* text = "Hello, World!";
    DocTextStyle style = DocTextStyle::plain();
    style.set(DocTextStyle::BOLD);
    
    DocElement* elem = doc_create_text(arena, text, strlen(text), style);
    
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->type, DocElemType::TEXT_RUN);
    EXPECT_EQ(elem->text.text_len, strlen(text));
    EXPECT_STREQ(elem->text.text, text);
    EXPECT_TRUE(elem->text.style.has(DocTextStyle::BOLD));
}

TEST_F(TexDocumentModelTest, CreateTextElementCstr) {
    const char* text = "Test string";
    DocTextStyle style = DocTextStyle::plain();
    
    DocElement* elem = doc_create_text_cstr(arena, text, style);
    
    ASSERT_NE(elem, nullptr);
    EXPECT_STREQ(elem->text.text, text);
    EXPECT_EQ(elem->text.text_len, strlen(text));
}

TEST_F(TexDocumentModelTest, BuildDocumentTree) {
    // Build a simple document structure:
    // DOCUMENT
    //   HEADING
    //   PARAGRAPH
    //     TEXT_RUN
    //     TEXT_SPAN (bold)
    //       TEXT_RUN
    
    DocElement* doc = doc_alloc_element(arena, DocElemType::DOCUMENT);
    
    DocElement* heading = doc_alloc_element(arena, DocElemType::HEADING);
    heading->heading.level = 2;
    heading->heading.title = "Introduction";
    doc_append_child(doc, heading);
    
    DocElement* para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    doc_append_child(doc, para);
    
    DocElement* text1 = doc_create_text_cstr(arena, "This is ", DocTextStyle::plain());
    doc_append_child(para, text1);
    
    DocElement* bold_span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    bold_span->text.style = DocTextStyle::plain();
    bold_span->text.style.set(DocTextStyle::BOLD);
    doc_append_child(para, bold_span);
    
    DocElement* bold_text = doc_create_text_cstr(arena, "important", DocTextStyle::plain());
    doc_append_child(bold_span, bold_text);
    
    // Verify structure
    EXPECT_EQ(doc->first_child, heading);
    EXPECT_EQ(heading->next_sibling, para);
    EXPECT_EQ(para->first_child, text1);
    EXPECT_EQ(text1->next_sibling, bold_span);
    EXPECT_EQ(bold_span->first_child, bold_text);
}

// ============================================================================
// Label and Reference Tests
// ============================================================================

TEST_F(TexDocumentModelTest, AddAndResolveLabels) {
    TexDocumentModel* doc = doc_model_create(arena);
    
    doc->add_label("sec:intro", "1.1", 1);
    doc->add_label("eq:formula", "2", 3);
    doc->add_label("fig:diagram", "Figure 1", 5);
    
    EXPECT_EQ(doc->label_count, 3);
    EXPECT_STREQ(doc->resolve_ref("sec:intro"), "1.1");
    EXPECT_STREQ(doc->resolve_ref("eq:formula"), "2");
    EXPECT_STREQ(doc->resolve_ref("fig:diagram"), "Figure 1");
    EXPECT_STREQ(doc->resolve_ref("nonexistent"), "??");
}

TEST_F(TexDocumentModelTest, AddManyLabels) {
    TexDocumentModel* doc = doc_model_create(arena);
    
    // Add more labels than initial capacity to test reallocation
    for (int i = 0; i < 50; i++) {
        char label[32];
        char ref[32];
        snprintf(label, sizeof(label), "label%d", i);
        snprintf(ref, sizeof(ref), "ref%d", i);
        doc->add_label(label, ref, i);
    }
    
    EXPECT_EQ(doc->label_count, 50);
}

// ============================================================================
// Macro Tests
// ============================================================================

TEST_F(TexDocumentModelTest, AddAndFindMacros) {
    TexDocumentModel* doc = doc_model_create(arena);
    
    doc->add_macro("\\R", 0, "\\mathbb{R}");
    doc->add_macro("\\norm", 1, "\\|#1\\|");
    doc->add_macro("\\inner", 2, "\\langle#1,#2\\rangle");
    
    EXPECT_EQ(doc->macro_count, 3);
    
    auto* macro1 = doc->find_macro("\\R");
    ASSERT_NE(macro1, nullptr);
    EXPECT_STREQ(macro1->replacement, "\\mathbb{R}");
    EXPECT_EQ(macro1->num_args, 0);
    
    auto* macro2 = doc->find_macro("\\norm");
    ASSERT_NE(macro2, nullptr);
    EXPECT_EQ(macro2->num_args, 1);
    
    auto* macro3 = doc->find_macro("\\nonexistent");
    EXPECT_EQ(macro3, nullptr);
}

// ============================================================================
// Bibliography Tests
// ============================================================================

TEST_F(TexDocumentModelTest, AddAndResolveCitations) {
    TexDocumentModel* doc = doc_model_create(arena);
    
    doc->add_bib_entry("knuth1984", "[1]");
    doc->add_bib_entry("lamport1994", "[2]");
    
    EXPECT_EQ(doc->bib_count, 2);
    EXPECT_STREQ(doc->resolve_cite("knuth1984"), "[1]");
    EXPECT_STREQ(doc->resolve_cite("lamport1994"), "[2]");
    EXPECT_STREQ(doc->resolve_cite("unknown"), "[?]");
}

// ============================================================================
// HTML Output Tests
// ============================================================================

TEST_F(TexDocumentModelTest, HtmlEscaping) {
    StrBuf* out = strbuf_new_cap(256);
    
    html_escape_append(out, "<script>alert('xss')</script>", 30);
    
    EXPECT_STREQ(out->str, "&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;");
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, HtmlIndentation) {
    StrBuf* out = strbuf_new_cap(256);
    
    html_indent(out, 0);
    EXPECT_EQ(out->length, 0u);
    
    html_indent(out, 3);
    EXPECT_STREQ(out->str, "      ");  // 3 * 2 spaces
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderSimpleParagraph) {
    DocElement* para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    DocElement* text = doc_create_text_cstr(arena, "Hello, World!", DocTextStyle::plain());
    doc_append_child(para, text);
    
    StrBuf* out = strbuf_new_cap(256);
    
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(para, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<p class=\"latex-paragraph\">"), nullptr);
    EXPECT_NE(strstr(out->str, "Hello, World!"), nullptr);
    EXPECT_NE(strstr(out->str, "</p>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderTextSpanWithStyling) {
    DocElement* span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    span->text.style = DocTextStyle::plain();
    span->text.style.set(DocTextStyle::BOLD);
    span->text.style.set(DocTextStyle::ITALIC);
    span->text.text = "styled text";
    span->text.text_len = 11;
    
    StrBuf* out = strbuf_new_cap(256);
    
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(span, out, opts, 0);
    
    // Should have bold and italic tags
    EXPECT_NE(strstr(out->str, "<strong>"), nullptr);
    EXPECT_NE(strstr(out->str, "<em>"), nullptr);
    EXPECT_NE(strstr(out->str, "</em>"), nullptr);
    EXPECT_NE(strstr(out->str, "</strong>"), nullptr);
    EXPECT_NE(strstr(out->str, "styled text"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderHeading) {
    DocElement* heading = doc_alloc_element(arena, DocElemType::HEADING);
    heading->heading.level = 2;  // section level
    heading->heading.title = "Test Section";
    heading->heading.number = "1.2";
    heading->flags = DocElement::FLAG_NUMBERED;
    
    StrBuf* out = strbuf_new_cap(256);
    
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(heading, out, opts, 0);
    
    // level 2 should map to h3
    EXPECT_NE(strstr(out->str, "<h3"), nullptr);
    EXPECT_NE(strstr(out->str, "section-number"), nullptr);
    EXPECT_NE(strstr(out->str, "1.2"), nullptr);
    EXPECT_NE(strstr(out->str, "Test Section"), nullptr);
    EXPECT_NE(strstr(out->str, "</h3>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderList) {
    DocElement* list = doc_alloc_element(arena, DocElemType::LIST);
    list->list.list_type = ListType::ITEMIZE;
    
    DocElement* item1 = doc_alloc_element(arena, DocElemType::LIST_ITEM);
    DocElement* text1 = doc_create_text_cstr(arena, "First item", DocTextStyle::plain());
    doc_append_child(item1, text1);
    doc_append_child(list, item1);
    
    DocElement* item2 = doc_alloc_element(arena, DocElemType::LIST_ITEM);
    DocElement* text2 = doc_create_text_cstr(arena, "Second item", DocTextStyle::plain());
    doc_append_child(item2, text2);
    doc_append_child(list, item2);
    
    StrBuf* out = strbuf_new_cap(512);
    
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(list, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<ul"), nullptr);
    EXPECT_NE(strstr(out->str, "<li>"), nullptr);
    EXPECT_NE(strstr(out->str, "First item"), nullptr);
    EXPECT_NE(strstr(out->str, "Second item"), nullptr);
    EXPECT_NE(strstr(out->str, "</li>"), nullptr);
    EXPECT_NE(strstr(out->str, "</ul>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderFullDocument) {
    TexDocumentModel* doc = doc_model_create(arena);
    doc->title = "Test Document";
    doc->author = "Test Author";
    doc->date = "2026";
    
    doc->root = doc_alloc_element(arena, DocElemType::DOCUMENT);
    
    DocElement* para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    DocElement* text = doc_create_text_cstr(arena, "Document content.", DocTextStyle::plain());
    doc_append_child(para, text);
    doc_append_child(doc->root, para);
    
    StrBuf* out = strbuf_new_cap(4096);
    
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.standalone = true;
    opts.include_css = true;
    
    bool result = doc_model_to_html(doc, out, opts);
    
    EXPECT_TRUE(result);
    EXPECT_NE(strstr(out->str, "<!DOCTYPE html>"), nullptr);
    EXPECT_NE(strstr(out->str, "<title>Test Document</title>"), nullptr);
    EXPECT_NE(strstr(out->str, "Test Author"), nullptr);
    EXPECT_NE(strstr(out->str, "Document content."), nullptr);
    EXPECT_NE(strstr(out->str, "</html>"), nullptr);
    
    strbuf_free(out);
}

// ============================================================================
// Debug Dump Tests
// ============================================================================

TEST_F(TexDocumentModelTest, DumpElement) {
    DocElement* para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    DocElement* text = doc_create_text_cstr(arena, "Test content", DocTextStyle::plain());
    doc_append_child(para, text);
    
    StrBuf* out = strbuf_new_cap(512);
    
    doc_element_dump(para, out, 0);
    
    EXPECT_NE(strstr(out->str, "[PARAGRAPH]"), nullptr);
    EXPECT_NE(strstr(out->str, "[TEXT_RUN]"), nullptr);
    EXPECT_NE(strstr(out->str, "Test content"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, DumpDocument) {
    TexDocumentModel* doc = doc_model_create(arena);
    doc->title = "Test";
    doc->root = doc_alloc_element(arena, DocElemType::DOCUMENT);
    
    StrBuf* out = strbuf_new_cap(512);
    
    doc_model_dump(doc, out);
    
    EXPECT_NE(strstr(out->str, "=== Document Model ==="), nullptr);
    EXPECT_NE(strstr(out->str, "Title: Test"), nullptr);
    EXPECT_NE(strstr(out->str, "[DOCUMENT]"), nullptr);
    
    strbuf_free(out);
}

// ============================================================================
// Element Type Name Tests
// ============================================================================

TEST_F(TexDocumentModelTest, ElementTypeNames) {
    EXPECT_STREQ(doc_elem_type_name(DocElemType::PARAGRAPH), "PARAGRAPH");
    EXPECT_STREQ(doc_elem_type_name(DocElemType::HEADING), "HEADING");
    EXPECT_STREQ(doc_elem_type_name(DocElemType::LIST), "LIST");
    EXPECT_STREQ(doc_elem_type_name(DocElemType::MATH_INLINE), "MATH_INLINE");
    EXPECT_STREQ(doc_elem_type_name(DocElemType::TEXT_SPAN), "TEXT_SPAN");
}

// ============================================================================
// DocTextStyle Tests
// ============================================================================

TEST_F(TexDocumentModelTest, TextStyleFlags) {
    DocTextStyle style = DocTextStyle::plain();
    
    EXPECT_FALSE(style.has(DocTextStyle::BOLD));
    EXPECT_FALSE(style.has(DocTextStyle::ITALIC));
    
    style.set(DocTextStyle::BOLD);
    EXPECT_TRUE(style.has(DocTextStyle::BOLD));
    EXPECT_FALSE(style.has(DocTextStyle::ITALIC));
    
    style.set(DocTextStyle::ITALIC);
    EXPECT_TRUE(style.has(DocTextStyle::BOLD));
    EXPECT_TRUE(style.has(DocTextStyle::ITALIC));
    
    style.clear(DocTextStyle::BOLD);
    EXPECT_FALSE(style.has(DocTextStyle::BOLD));
    EXPECT_TRUE(style.has(DocTextStyle::ITALIC));
}

// ============================================================================
// Tree Manipulation Tests
// ============================================================================

TEST_F(TexDocumentModelTest, InsertBefore) {
    DocElement* parent = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    DocElement* child1 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    DocElement* child2 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    DocElement* child3 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    
    doc_append_child(parent, child1);
    doc_append_child(parent, child3);
    
    // Insert child2 between child1 and child3
    doc_insert_before(parent, child3, child2);
    
    EXPECT_EQ(parent->first_child, child1);
    EXPECT_EQ(child1->next_sibling, child2);
    EXPECT_EQ(child2->next_sibling, child3);
    EXPECT_EQ(parent->last_child, child3);
    EXPECT_EQ(child2->parent, parent);
}

TEST_F(TexDocumentModelTest, InsertAtBeginning) {
    DocElement* parent = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    DocElement* child1 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    DocElement* child2 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    
    doc_append_child(parent, child2);
    doc_insert_before(parent, child2, child1);
    
    EXPECT_EQ(parent->first_child, child1);
    EXPECT_EQ(child1->next_sibling, child2);
    EXPECT_EQ(parent->last_child, child2);
}

TEST_F(TexDocumentModelTest, RemoveChild) {
    DocElement* parent = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    DocElement* child1 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    DocElement* child2 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    DocElement* child3 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    
    doc_append_child(parent, child1);
    doc_append_child(parent, child2);
    doc_append_child(parent, child3);
    
    // Remove middle child
    doc_remove_child(parent, child2);
    
    EXPECT_EQ(parent->first_child, child1);
    EXPECT_EQ(child1->next_sibling, child3);
    EXPECT_EQ(parent->last_child, child3);
    EXPECT_EQ(child2->parent, nullptr);
    EXPECT_EQ(child2->next_sibling, nullptr);
}

TEST_F(TexDocumentModelTest, RemoveFirstChild) {
    DocElement* parent = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    DocElement* child1 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    DocElement* child2 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    
    doc_append_child(parent, child1);
    doc_append_child(parent, child2);
    
    doc_remove_child(parent, child1);
    
    EXPECT_EQ(parent->first_child, child2);
    EXPECT_EQ(parent->last_child, child2);
}

TEST_F(TexDocumentModelTest, RemoveLastChild) {
    DocElement* parent = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    DocElement* child1 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    DocElement* child2 = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    
    doc_append_child(parent, child1);
    doc_append_child(parent, child2);
    
    doc_remove_child(parent, child2);
    
    EXPECT_EQ(parent->first_child, child1);
    EXPECT_EQ(parent->last_child, child1);
    EXPECT_EQ(child1->next_sibling, nullptr);
}

// ============================================================================
// Math SVG Rendering Tests (Phase B)
// ============================================================================

TEST_F(TexDocumentModelTest, RenderInlineMathFallback) {
    // Test inline math rendering without TexNode (fallback path)
    DocElement* math = doc_alloc_element(arena, DocElemType::MATH_INLINE);
    math->math.latex_src = "x^2 + y^2";
    math->math.node = nullptr;  // No pre-rendered node
    
    StrBuf* out = strbuf_new_cap(512);
    
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    opts.math_as_svg = true;  // Request SVG, but will fall back since node is null
    
    doc_element_to_html(math, out, opts, 0);
    
    // Should produce fallback span with math content (with prefix)
    EXPECT_NE(strstr(out->str, "math-inline"), nullptr);
    EXPECT_NE(strstr(out->str, "x^2 + y^2"), nullptr);
    EXPECT_NE(strstr(out->str, "</span>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderDisplayMathFallback) {
    // Test display math rendering without TexNode (fallback path)
    DocElement* math = doc_alloc_element(arena, DocElemType::MATH_DISPLAY);
    math->math.latex_src = "\\int_0^\\infty e^{-x} dx";
    math->math.node = nullptr;
    math->math.number = "1.1";
    
    StrBuf* out = strbuf_new_cap(512);
    
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    opts.math_as_svg = true;
    
    doc_element_to_html(math, out, opts, 0);
    
    // Should produce display math container with equation number
    EXPECT_NE(strstr(out->str, "math-display"), nullptr);
    EXPECT_NE(strstr(out->str, "eq-number"), nullptr);
    EXPECT_NE(strstr(out->str, "(1.1)"), nullptr);
    EXPECT_NE(strstr(out->str, "</div>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderMathWithoutSvgOption) {
    // Test math rendering with SVG disabled
    DocElement* math = doc_alloc_element(arena, DocElemType::MATH_INLINE);
    math->math.latex_src = "a + b = c";
    math->math.node = nullptr;
    
    StrBuf* out = strbuf_new_cap(512);
    
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    opts.math_as_svg = false;  // SVG disabled
    
    doc_element_to_html(math, out, opts, 0);
    
    // Should still produce math span
    EXPECT_NE(strstr(out->str, "math-inline"), nullptr);
    EXPECT_NE(strstr(out->str, "a + b = c"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, MathElementEquationLabel) {
    // Test equation labels in math elements
    DocElement* math = doc_alloc_element(arena, DocElemType::MATH_DISPLAY);
    math->math.latex_src = "E = mc^2";
    math->math.number = "2";
    math->math.label = "eq:einstein";
    
    EXPECT_STREQ(math->math.latex_src, "E = mc^2");
    EXPECT_STREQ(math->math.number, "2");
    EXPECT_STREQ(math->math.label, "eq:einstein");
}

TEST_F(TexDocumentModelTest, HtmlOutputOptionsDefaults) {
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    
    // Check default values (as defined in tex_document_model.hpp)
    EXPECT_TRUE(opts.include_css);
    EXPECT_TRUE(opts.standalone);
    EXPECT_TRUE(opts.pretty_print);
    EXPECT_TRUE(opts.math_as_svg);
    EXPECT_STREQ(opts.css_class_prefix, "latex-");
    EXPECT_STREQ(opts.lang, "en");
}

TEST_F(TexDocumentModelTest, HtmlOutputOptionsMathSvg) {
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.math_as_svg = false;  // Disable
    
    EXPECT_FALSE(opts.math_as_svg);
    
    opts.math_as_svg = true;   // Re-enable
    EXPECT_TRUE(opts.math_as_svg);
}

// ============================================================================
// Phase C: Text Formatting and Section Tests
// ============================================================================

TEST_F(TexDocumentModelTest, BuildBoldTextSpan) {
    // Test that bold text span has correct style flags
    DocElement* span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    span->text.style = DocTextStyle::plain();
    span->text.style.flags |= DocTextStyle::BOLD;
    
    DocElement* text = doc_create_text_cstr(arena, "Bold text", DocTextStyle::plain());
    doc_append_child(span, text);
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(span, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<strong>"), nullptr);
    EXPECT_NE(strstr(out->str, "Bold text"), nullptr);
    EXPECT_NE(strstr(out->str, "</strong>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, BuildItalicTextSpan) {
    DocElement* span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    span->text.style = DocTextStyle::plain();
    span->text.style.flags |= DocTextStyle::ITALIC;
    
    DocElement* text = doc_create_text_cstr(arena, "Italic text", DocTextStyle::plain());
    doc_append_child(span, text);
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(span, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<em>"), nullptr);
    EXPECT_NE(strstr(out->str, "Italic text"), nullptr);
    EXPECT_NE(strstr(out->str, "</em>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, BuildMonospaceTextSpan) {
    DocElement* span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    span->text.style = DocTextStyle::plain();
    span->text.style.flags |= DocTextStyle::MONOSPACE;
    
    DocElement* text = doc_create_text_cstr(arena, "Code text", DocTextStyle::plain());
    doc_append_child(span, text);
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(span, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<code>"), nullptr);
    EXPECT_NE(strstr(out->str, "Code text"), nullptr);
    EXPECT_NE(strstr(out->str, "</code>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, BuildNestedTextStyles) {
    // Bold containing italic
    DocElement* bold = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    bold->text.style = DocTextStyle::plain();
    bold->text.style.flags |= DocTextStyle::BOLD;
    
    DocElement* italic = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    italic->text.style = DocTextStyle::plain();
    italic->text.style.flags |= DocTextStyle::ITALIC;
    
    DocElement* text = doc_create_text_cstr(arena, "Bold and italic", DocTextStyle::plain());
    doc_append_child(italic, text);
    doc_append_child(bold, italic);
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(bold, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<strong>"), nullptr);
    EXPECT_NE(strstr(out->str, "<em>"), nullptr);
    EXPECT_NE(strstr(out->str, "Bold and italic"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, BuildSectionHeading) {
    DocElement* heading = doc_alloc_element(arena, DocElemType::HEADING);
    heading->heading.level = 2;  // section
    heading->heading.title = "Introduction";
    heading->heading.number = "1";
    heading->flags = DocElement::FLAG_NUMBERED;
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(heading, out, opts, 0);
    
    // Section (level 2) should produce <h3>
    EXPECT_NE(strstr(out->str, "<h3"), nullptr);
    EXPECT_NE(strstr(out->str, "Introduction"), nullptr);
    EXPECT_NE(strstr(out->str, "</h3>"), nullptr);
    EXPECT_NE(strstr(out->str, "1"), nullptr);  // section number
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, BuildStarredSection) {
    // Starred section should not have number
    DocElement* heading = doc_alloc_element(arena, DocElemType::HEADING);
    heading->heading.level = 2;
    heading->heading.title = "Appendix";
    heading->heading.number = nullptr;
    heading->flags = DocElement::FLAG_STARRED;
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(heading, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<h3"), nullptr);
    EXPECT_NE(strstr(out->str, "Appendix"), nullptr);
    // Should NOT have section-number span
    EXPECT_EQ(strstr(out->str, "section-number"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, BuildChapterHeading) {
    DocElement* heading = doc_alloc_element(arena, DocElemType::HEADING);
    heading->heading.level = 1;  // chapter
    heading->heading.title = "Getting Started";
    heading->heading.number = "1";
    heading->flags = DocElement::FLAG_NUMBERED;
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(heading, out, opts, 0);
    
    // Chapter (level 1) should produce <h2>
    EXPECT_NE(strstr(out->str, "<h2"), nullptr);
    EXPECT_NE(strstr(out->str, "Getting Started"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, BuildSubsectionHeading) {
    DocElement* heading = doc_alloc_element(arena, DocElemType::HEADING);
    heading->heading.level = 3;  // subsection
    heading->heading.title = "Details";
    heading->heading.number = "1.2";
    heading->flags = DocElement::FLAG_NUMBERED;
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(heading, out, opts, 0);
    
    // Subsection (level 3) should produce <h4>
    EXPECT_NE(strstr(out->str, "<h4"), nullptr);
    EXPECT_NE(strstr(out->str, "Details"), nullptr);
    EXPECT_NE(strstr(out->str, "1.2"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, TextStyleCombinations) {
    // Test combined styles: bold + italic
    DocElement* span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    span->text.style = DocTextStyle::plain();
    span->text.style.flags = DocTextStyle::BOLD | DocTextStyle::ITALIC;
    
    DocElement* text = doc_create_text_cstr(arena, "Combined", DocTextStyle::plain());
    doc_append_child(span, text);
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(span, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<strong>"), nullptr);
    EXPECT_NE(strstr(out->str, "<em>"), nullptr);
    EXPECT_NE(strstr(out->str, "Combined"), nullptr);
    EXPECT_NE(strstr(out->str, "</em>"), nullptr);
    EXPECT_NE(strstr(out->str, "</strong>"), nullptr);
    
    strbuf_free(out);
}
// ============================================================================
// Phase D: List, Table, Blockquote, Code Block Tests
// ============================================================================

TEST_F(TexDocumentModelTest, RenderUnorderedList) {
    // Create itemize list
    DocElement* list = doc_alloc_element(arena, DocElemType::LIST);
    list->list.list_type = ListType::ITEMIZE;
    
    // Add items
    DocElement* item1 = doc_alloc_element(arena, DocElemType::LIST_ITEM);
    DocElement* text1 = doc_create_text_cstr(arena, "First item", DocTextStyle::plain());
    doc_append_child(item1, text1);
    doc_append_child(list, item1);
    
    DocElement* item2 = doc_alloc_element(arena, DocElemType::LIST_ITEM);
    DocElement* text2 = doc_create_text_cstr(arena, "Second item", DocTextStyle::plain());
    doc_append_child(item2, text2);
    doc_append_child(list, item2);
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(list, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<ul"), nullptr);
    EXPECT_NE(strstr(out->str, "<li>"), nullptr);
    EXPECT_NE(strstr(out->str, "First item"), nullptr);
    EXPECT_NE(strstr(out->str, "Second item"), nullptr);
    EXPECT_NE(strstr(out->str, "</ul>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderOrderedList) {
    // Create enumerate list
    DocElement* list = doc_alloc_element(arena, DocElemType::LIST);
    list->list.list_type = ListType::ENUMERATE;
    list->list.start_num = 1;
    
    DocElement* item1 = doc_alloc_element(arena, DocElemType::LIST_ITEM);
    DocElement* text1 = doc_create_text_cstr(arena, "Step one", DocTextStyle::plain());
    doc_append_child(item1, text1);
    doc_append_child(list, item1);
    
    DocElement* item2 = doc_alloc_element(arena, DocElemType::LIST_ITEM);
    DocElement* text2 = doc_create_text_cstr(arena, "Step two", DocTextStyle::plain());
    doc_append_child(item2, text2);
    doc_append_child(list, item2);
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(list, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<ol"), nullptr);
    EXPECT_NE(strstr(out->str, "<li>"), nullptr);
    EXPECT_NE(strstr(out->str, "Step one"), nullptr);
    EXPECT_NE(strstr(out->str, "Step two"), nullptr);
    EXPECT_NE(strstr(out->str, "</ol>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderDescriptionList) {
    // Create description list
    DocElement* list = doc_alloc_element(arena, DocElemType::LIST);
    list->list.list_type = ListType::DESCRIPTION;
    
    DocElement* item1 = doc_alloc_element(arena, DocElemType::LIST_ITEM);
    item1->list_item.label = "Term1";
    DocElement* text1 = doc_create_text_cstr(arena, "Definition one", DocTextStyle::plain());
    doc_append_child(item1, text1);
    doc_append_child(list, item1);
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(list, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<dl"), nullptr);
    EXPECT_NE(strstr(out->str, "<dt>"), nullptr);
    EXPECT_NE(strstr(out->str, "Term1"), nullptr);
    EXPECT_NE(strstr(out->str, "<dd>"), nullptr);
    // Note: "fi" in "Definition" gets transformed to the fi-ligature (U+FB01) = "ﬁ"
    EXPECT_NE(strstr(out->str, "De\xEF\xAC\x81nition one"), nullptr);
    EXPECT_NE(strstr(out->str, "</dl>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderSimpleTable) {
    // Create table
    DocElement* table = doc_alloc_element(arena, DocElemType::TABLE);
    table->table.column_spec = "lc";
    table->table.num_columns = 2;
    
    // Row 1
    DocElement* row1 = doc_alloc_element(arena, DocElemType::TABLE_ROW);
    DocElement* cell11 = doc_alloc_element(arena, DocElemType::TABLE_CELL);
    doc_append_child(cell11, doc_create_text_cstr(arena, "A1", DocTextStyle::plain()));
    doc_append_child(row1, cell11);
    DocElement* cell12 = doc_alloc_element(arena, DocElemType::TABLE_CELL);
    doc_append_child(cell12, doc_create_text_cstr(arena, "B1", DocTextStyle::plain()));
    doc_append_child(row1, cell12);
    doc_append_child(table, row1);
    
    // Row 2
    DocElement* row2 = doc_alloc_element(arena, DocElemType::TABLE_ROW);
    DocElement* cell21 = doc_alloc_element(arena, DocElemType::TABLE_CELL);
    doc_append_child(cell21, doc_create_text_cstr(arena, "A2", DocTextStyle::plain()));
    doc_append_child(row2, cell21);
    DocElement* cell22 = doc_alloc_element(arena, DocElemType::TABLE_CELL);
    doc_append_child(cell22, doc_create_text_cstr(arena, "B2", DocTextStyle::plain()));
    doc_append_child(row2, cell22);
    doc_append_child(table, row2);
    
    StrBuf* out = strbuf_new_cap(512);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(table, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<table"), nullptr);
    EXPECT_NE(strstr(out->str, "<tr>"), nullptr);
    EXPECT_NE(strstr(out->str, "<td"), nullptr);
    EXPECT_NE(strstr(out->str, "A1"), nullptr);
    EXPECT_NE(strstr(out->str, "B1"), nullptr);
    EXPECT_NE(strstr(out->str, "A2"), nullptr);
    EXPECT_NE(strstr(out->str, "B2"), nullptr);
    EXPECT_NE(strstr(out->str, "</table>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderBlockquote) {
    DocElement* quote = doc_alloc_element(arena, DocElemType::BLOCKQUOTE);
    DocElement* para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    doc_append_child(para, doc_create_text_cstr(arena, "This is a quote.", DocTextStyle::plain()));
    doc_append_child(quote, para);
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(quote, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<blockquote"), nullptr);
    EXPECT_NE(strstr(out->str, "This is a quote."), nullptr);
    EXPECT_NE(strstr(out->str, "</blockquote>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderCodeBlock) {
    DocElement* code = doc_alloc_element(arena, DocElemType::CODE_BLOCK);
    code->text.text = "int main() {\n    return 0;\n}";
    code->text.text_len = strlen(code->text.text);
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(code, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<pre"), nullptr);
    EXPECT_NE(strstr(out->str, "<code>"), nullptr);
    EXPECT_NE(strstr(out->str, "int main()"), nullptr);
    EXPECT_NE(strstr(out->str, "return 0;"), nullptr);
    EXPECT_NE(strstr(out->str, "</code></pre>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderNestedList) {
    // Create outer list
    DocElement* outer = doc_alloc_element(arena, DocElemType::LIST);
    outer->list.list_type = ListType::ITEMIZE;
    
    // Add first item with nested list
    DocElement* item1 = doc_alloc_element(arena, DocElemType::LIST_ITEM);
    doc_append_child(item1, doc_create_text_cstr(arena, "Outer item", DocTextStyle::plain()));
    
    // Create inner list
    DocElement* inner = doc_alloc_element(arena, DocElemType::LIST);
    inner->list.list_type = ListType::ENUMERATE;
    
    DocElement* inner_item = doc_alloc_element(arena, DocElemType::LIST_ITEM);
    doc_append_child(inner_item, doc_create_text_cstr(arena, "Inner item", DocTextStyle::plain()));
    doc_append_child(inner, inner_item);
    
    doc_append_child(item1, inner);
    doc_append_child(outer, item1);
    
    StrBuf* out = strbuf_new_cap(512);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(outer, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<ul"), nullptr);
    EXPECT_NE(strstr(out->str, "<ol"), nullptr);
    EXPECT_NE(strstr(out->str, "Outer item"), nullptr);
    EXPECT_NE(strstr(out->str, "Inner item"), nullptr);
    EXPECT_NE(strstr(out->str, "</ol>"), nullptr);
    EXPECT_NE(strstr(out->str, "</ul>"), nullptr);
    
    strbuf_free(out);
}

// ============================================================================
// Phase E Tests: Images, Links, Figures, Cross-References
// ============================================================================

TEST_F(TexDocumentModelTest, RenderImage) {
    DocElement* img = doc_alloc_element(arena, DocElemType::IMAGE);
    img->image.src = "diagram.png";
    img->image.width = 300.0f;
    img->image.height = 200.0f;
    img->image.alt = "A diagram";
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(img, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<img"), nullptr);
    EXPECT_NE(strstr(out->str, "src=\"diagram.png\""), nullptr);
    EXPECT_NE(strstr(out->str, "width=\"300"), nullptr);
    EXPECT_NE(strstr(out->str, "height=\"200"), nullptr);
    EXPECT_NE(strstr(out->str, "alt=\"A diagram\""), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderImageNoAlt) {
    DocElement* img = doc_alloc_element(arena, DocElemType::IMAGE);
    img->image.src = "photo.jpg";
    img->image.width = 0.0f;  // No width specified
    img->image.height = 0.0f; // No height specified
    img->image.alt = nullptr;
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(img, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<img"), nullptr);
    EXPECT_NE(strstr(out->str, "src=\"photo.jpg\""), nullptr);
    // Should not have width/height attributes when zero
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderLink) {
    DocElement* link = doc_alloc_element(arena, DocElemType::LINK);
    link->link.href = "https://example.com";
    link->link.link_text = "Example Site";
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(link, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<a href=\"https://example.com\""), nullptr);
    EXPECT_NE(strstr(out->str, "Example Site"), nullptr);
    EXPECT_NE(strstr(out->str, "</a>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderUrlLink) {
    // URL link where text equals the URL
    DocElement* link = doc_alloc_element(arena, DocElemType::LINK);
    link->link.href = "https://www.latex-project.org";
    link->link.link_text = "https://www.latex-project.org";
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(link, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<a href=\"https://www.latex-project.org\""), nullptr);
    EXPECT_NE(strstr(out->str, "https://www.latex-project.org</a>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderFigure) {
    DocElement* fig = doc_alloc_element(arena, DocElemType::FIGURE);
    
    // Add an image inside the figure
    DocElement* img = doc_alloc_element(arena, DocElemType::IMAGE);
    img->image.src = "graph.png";
    img->image.width = 400.0f;
    img->image.height = 300.0f;
    img->image.alt = "A graph";
    doc_append_child(fig, img);
    
    // Add a caption
    DocElement* caption = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    caption->text.text = "Figure 1: A sample graph";
    caption->text.text_len = strlen(caption->text.text);
    caption->text.style = DocTextStyle::plain();
    doc_append_child(fig, caption);
    
    StrBuf* out = strbuf_new_cap(512);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(fig, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<figure"), nullptr);
    EXPECT_NE(strstr(out->str, "<img"), nullptr);
    EXPECT_NE(strstr(out->str, "graph.png"), nullptr);
    EXPECT_NE(strstr(out->str, "A sample graph"), nullptr);
    EXPECT_NE(strstr(out->str, "</figure>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderCrossRef) {
    DocElement* ref = doc_alloc_element(arena, DocElemType::CROSS_REF);
    ref->ref.ref_label = "fig:intro";
    ref->ref.ref_text = "1";  // Resolved reference number
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(ref, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<a href=\"#fig:intro\""), nullptr);
    EXPECT_NE(strstr(out->str, ">1</a>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderUnresolvedRef) {
    DocElement* ref = doc_alloc_element(arena, DocElemType::CROSS_REF);
    ref->ref.ref_label = "eq:undefined";
    ref->ref.ref_text = "??";  // Unresolved reference
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(ref, out, opts, 0);
    
    // Should still render as a link with ?? text
    EXPECT_NE(strstr(out->str, "<a href=\"#eq:undefined\""), nullptr);
    EXPECT_NE(strstr(out->str, "??</a>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderFootnote) {
    DocElement* fn = doc_alloc_element(arena, DocElemType::FOOTNOTE);
    fn->footnote.footnote_number = 1;
    
    // Add footnote content
    DocElement* text = doc_create_text_cstr(arena, "This is footnote text.", DocTextStyle::plain());
    doc_append_child(fn, text);
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(fn, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<sup"), nullptr);
    EXPECT_NE(strstr(out->str, "1"), nullptr);
    // Footnote marker should link to footnote content
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, RenderCitation) {
    DocElement* cite = doc_alloc_element(arena, DocElemType::CITATION);
    cite->citation.key = "knuth1984";
    cite->citation.cite_text = "[1]";  // Resolved citation
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(cite, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<cite"), nullptr);
    EXPECT_NE(strstr(out->str, "[1]"), nullptr);
    EXPECT_NE(strstr(out->str, "</cite>"), nullptr);
    
    strbuf_free(out);
}

TEST_F(TexDocumentModelTest, LabelResolution) {
    TexDocumentModel* doc = doc_model_create(arena);
    
    // Add a label
    doc->add_label("fig:test", "3", 5);
    
    // Resolve it
    const char* ref_text = doc->resolve_ref("fig:test");
    ASSERT_NE(ref_text, nullptr);
    EXPECT_STREQ(ref_text, "3");
    
    // Try to resolve unknown label - returns placeholder, not nullptr
    const char* unknown = doc->resolve_ref("nonexistent");
    EXPECT_STREQ(unknown, "??");  // Unresolved marker
}

TEST_F(TexDocumentModelTest, CitationResolution) {
    TexDocumentModel* doc = doc_model_create(arena);
    
    // Add a bibliography entry
    doc->add_bib_entry("lamport1986", "[Lamport, 1986]");
    
    // Resolve it
    const char* cite_text = doc->resolve_cite("lamport1986");
    ASSERT_NE(cite_text, nullptr);
    EXPECT_STREQ(cite_text, "[Lamport, 1986]");
    
    // Try to resolve unknown citation - returns placeholder, not nullptr
    const char* unknown = doc->resolve_cite("nonexistent");
    EXPECT_STREQ(unknown, "[?]");  // Unresolved marker
}

TEST_F(TexDocumentModelTest, HeadingWithLabel) {
    DocElement* heading = doc_alloc_element(arena, DocElemType::HEADING);
    heading->heading.level = 0;  // level 0 -> h1
    heading->heading.title = "Introduction";
    heading->heading.label = "sec:intro";
    heading->flags |= DocElement::FLAG_NUMBERED;
    
    StrBuf* out = strbuf_new_cap(256);
    HtmlOutputOptions opts = HtmlOutputOptions::defaults();
    opts.pretty_print = false;
    
    doc_element_to_html(heading, out, opts, 0);
    
    EXPECT_NE(strstr(out->str, "<h1"), nullptr);
    EXPECT_NE(strstr(out->str, "id=\"sec:intro\""), nullptr);
    EXPECT_NE(strstr(out->str, "Introduction"), nullptr);
    EXPECT_NE(strstr(out->str, "</h1>"), nullptr);
    
    strbuf_free(out);
}