#include <gtest/gtest.h>
#include "../lambda/format/format-latex-html.h"
#include "../lib/stringbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"

class LatexHtmlTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize memory pool
        pool = variable_mem_pool_create();
        ASSERT_NE(pool, nullptr);

        // Create string buffers with the memory pool
        html_buf = stringbuf_new(pool);
        css_buf = stringbuf_new(pool);
        ASSERT_NE(html_buf, nullptr);
        ASSERT_NE(css_buf, nullptr);
    }

    void TearDown() override {
        // Cleanup string buffers
        if (html_buf) {
            stringbuf_free(html_buf);
        }
        if (css_buf) {
            stringbuf_free(css_buf);
        }

        // Cleanup memory pool
        if (pool) {
            variable_mem_pool_destroy(pool);
        }
    }

    VariableMemPool* pool;
    StringBuf* html_buf;
    StringBuf* css_buf;
};

TEST_F(LatexHtmlTest, BasicDocumentStructure) {
    // Create a simple LaTeX AST for testing
    // This would normally come from the LaTeX parser

    // Create document class element
    Element* doc_class = (Element*)pool_calloc(pool, sizeof(Element));
    doc_class->name = string_pooled(pool, "documentclass", 13);
    doc_class->children = array_pooled(pool);

    String* article_str = string_pooled(pool, "article", 7);
    Item article_item = {.item = s2it(article_str)};
    array_append(doc_class->children, article_item, pool);

    Item doc_class_item = {.item = e2it(doc_class)};

    // Test the HTML generator
        // Test formatting
    LatexHtmlGenerator* generator = latex_html_generator_create(html_buf, css_buf, pool);
    ASSERT_NE(generator, nullptr);

    process_document_class(generator, doc_class_item);
    EXPECT_EQ(generator->doc_class, DOCUMENT_CLASS_ARTICLE);

    latex_html_generator_destroy(generator);
}

TEST_F(LatexHtmlTest, TextFormatting) {
    // Create a textbf element
    Element* textbf = (Element*)pool_calloc(pool, sizeof(Element));
    textbf->name = string_pooled(pool, "textbf", 6);
    textbf->children = array_pooled(pool);

    String* bold_text = string_pooled(pool, "Bold text", 9);
    Item text_item = {.item = s2it(bold_text)};
    array_append(textbf->children, text_item, pool);

    Item textbf_item = {.item = e2it(textbf)};

    // Test the HTML generator
    LatexHtmlGenerator* generator = latex_html_generator_create(&html_buf, &css_buf, pool);
    ASSERT_NE(generator, nullptr);

    process_text_formatting(generator, textbf_item);

    String* html_result = stringbuf_to_string(html_buf);
    EXPECT_TRUE(strstr(html_result->chars, "<strong class=\"textbf\">"));
    EXPECT_TRUE(strstr(html_result->chars, "Bold text"));
    EXPECT_TRUE(strstr(html_result->chars, "</strong>"));

    latex_html_generator_destroy(generator);
}

TEST_F(LatexHtmlTest, SectionProcessing) {
    // Create a section element
    Element* section = (Element*)pool_calloc(pool, sizeof(Element));
    section->name = string_pooled(pool, "section", 7);
    section->children = array_pooled(pool);

    String* section_title = string_pooled(pool, "Test Section", 12);
    Item title_item = {.item = s2it(section_title)};
    array_append(section->children, title_item, pool);

    Item section_item = {.item = e2it(section)};

    // Test the HTML generator
    LatexHtmlGenerator* generator = latex_html_generator_create(html_buf, css_buf, pool);
    ASSERT_NE(generator, nullptr);

    process_sectioning(generator, section_item);

    String* html_result = stringbuf_to_string(html_buf);
    EXPECT_TRUE(strstr(html_result->chars, "<h2 class=\"section\">"));
    EXPECT_TRUE(strstr(html_result->chars, "Test Section"));
    EXPECT_TRUE(strstr(html_result->chars, "</h2>"));

    latex_html_generator_destroy(generator);
}

TEST_F(LatexHtmlTest, CSSGeneration) {
    LatexCssGenerator* css_gen = latex_css_generator_create(&css_buf, pool);
    ASSERT_NE(css_gen, nullptr);

    generate_base_css(css_gen);
    generate_typography_css(css_gen);

    String* css_result = stringbuf_to_string(&css_buf);
    EXPECT_TRUE(strstr(css_result->chars, ".latex-document"));
    EXPECT_TRUE(strstr(css_result->chars, "--latex-font-size"));
    EXPECT_TRUE(strstr(css_result->chars, ".textbf"));
    EXPECT_TRUE(strstr(css_result->chars, "font-weight: bold"));

    latex_css_generator_destroy(css_gen);
}

TEST_F(LatexHtmlTest, ReferenceManager) {
    ReferenceManager* ref_manager = reference_manager_create(pool);
    ASSERT_NE(ref_manager, nullptr);

    String* label = string_pooled(pool, "sec:intro", 9);
    String* value = string_pooled(pool, "1", 1);

    register_label(ref_manager, label, value);

    String* resolved = resolve_reference(ref_manager, label);
    ASSERT_NE(resolved, nullptr);
    EXPECT_STREQ(resolved->chars, "1");

    // Test counter functionality
    String* section_counter = string_pooled(pool, "section", 7);
    increment_counter(ref_manager, section_counter);

    int counter_value = get_counter_value(ref_manager, section_counter);
    EXPECT_EQ(counter_value, 1);

    reference_manager_destroy(ref_manager);
}

TEST_F(LatexHtmlTest, ListEnvironment) {
    // Create an itemize environment
    Element* itemize = (Element*)pool_calloc(pool, sizeof(Element));
    itemize->name = string_pooled(pool, "itemize", 7);
    itemize->children = array_pooled(pool);

    // Create list items
    Element* item1 = (Element*)pool_calloc(pool, sizeof(Element));
    item1->name = string_pooled(pool, "item", 4);
    item1->children = array_pooled(pool);

    String* item1_text = string_pooled(pool, "First item", 10);
    Item item1_text_item = {.item = s2it(item1_text)};
    array_append(item1->children, item1_text_item, pool);

    Item item1_item = {.item = e2it(item1)};
    array_append(itemize->children, item1_item, pool);

    Item itemize_item = {.item = e2it(itemize)};

    // Test the HTML generator
    LatexHtmlGenerator* generator = latex_html_generator_create(html_buf, css_buf, pool);
    ASSERT_NE(generator, nullptr);

    process_environment(generator, itemize_item);

    String* html_result = stringbuf_to_string(html_buf);
    EXPECT_TRUE(strstr(html_result->chars, "<ul class=\"itemize\">"));
    EXPECT_TRUE(strstr(html_result->chars, "<li>"));
    EXPECT_TRUE(strstr(html_result->chars, "First item"));
    EXPECT_TRUE(strstr(html_result->chars, "</li>"));
    EXPECT_TRUE(strstr(html_result->chars, "</ul>"));

    latex_html_generator_destroy(generator);
}
