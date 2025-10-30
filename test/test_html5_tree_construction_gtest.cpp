#include <gtest/gtest.h>
extern "C" {
#include "lambda/input/html5_parser.h"
#include "lambda/input/html5_tokenizer.h"
#include "lambda/input/input.h"
#include "lib/mempool.h"
#include "lib/stringbuf.h"
#include "lib/arraylist.h"
}

class Html5TreeConstructionTest : public ::testing::Test {
protected:
    Pool* pool;
    Input* input;

    void SetUp() override {
        pool = pool_create();

        // Initialize Input structure
        input = (Input*)pool_calloc(pool, sizeof(Input));
        input->pool = pool;
        input->sb = stringbuf_new(pool);
        input->type_list = arraylist_new(16);
        input->root = {.item = ITEM_NULL};
    }

    void TearDown() override {
        if (input && input->type_list) {
            arraylist_free(input->type_list);
        }
        if (input && input->sb) {
            stringbuf_free(input->sb);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }    // Helper to get tag name from element
    std::string getTagName(Element* element) {
        if (!element || !element->type) return "";
        TypeElmt* type = (TypeElmt*)element->type;
        return std::string(type->name.str);
    }

    // Helper to count children
    size_t countChildren(Element* element) {
        if (!element) return 0;
        return (size_t)element->length;
    }

    // Helper to get nth child
    Element* getChild(Element* element, size_t index) {
        if (!element) return nullptr;
        if (index >= (size_t)element->length) return nullptr;
        Item item = element->items[index];
        // Use get_type_id() which reads from the Container's type_id field
        if (get_type_id(item) == LMD_TYPE_ELEMENT) {
            return item.element;
        }
        return nullptr;
    }
};

// ============================================================================
// Basic Document Structure Tests
// ============================================================================

TEST_F(Html5TreeConstructionTest, EmptyDocument) {
    const char* html = "";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    // Should create implicit html, head, and body elements
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(getTagName(doc), "html");
}

TEST_F(Html5TreeConstructionTest, SimpleDocument) {
    const char* html = "<html><head></head><body></body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(getTagName(doc), "html");

    // Should have 2 children: head and body
    EXPECT_EQ(countChildren(doc), (size_t)2);

    Element* head = getChild(doc, 0);
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(getTagName(head), "head");

    Element* body = getChild(doc, 1);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(getTagName(body), "body");
}

TEST_F(Html5TreeConstructionTest, ImplicitElements) {
    const char* html = "<div>Hello</div>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(getTagName(doc), "html");

    // Should have implicitly created html, head, and body
    EXPECT_GE(countChildren(doc), (size_t)1);
}

TEST_F(Html5TreeConstructionTest, SingleDiv) {
    const char* html = "<html><body><div></div></body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    Element* body = getChild(doc, 1);  // Second child after head
    ASSERT_NE(body, nullptr);

    EXPECT_EQ(countChildren(body), (size_t)1);
    Element* div = getChild(body, 0);
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(getTagName(div), "div");
}

TEST_F(Html5TreeConstructionTest, NestedDivs) {
    const char* html = "<html><body><div><div><div></div></div></div></body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    Element* body = getChild(doc, 1);
    ASSERT_NE(body, nullptr);

    Element* div1 = getChild(body, 0);
    ASSERT_NE(div1, nullptr);
    EXPECT_EQ(getTagName(div1), "div");

    Element* div2 = getChild(div1, 0);
    ASSERT_NE(div2, nullptr);
    EXPECT_EQ(getTagName(div2), "div");

    Element* div3 = getChild(div2, 0);
    ASSERT_NE(div3, nullptr);
    EXPECT_EQ(getTagName(div3), "div");
}

// ============================================================================
// Text Content Tests
// ============================================================================

TEST_F(Html5TreeConstructionTest, SimpleText) {
    const char* html = "<html><body>Hello</body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    Element* body = getChild(doc, 1);
    ASSERT_NE(body, nullptr);

    // Should have text nodes as children
    EXPECT_GE(countChildren(body), (size_t)1);
}

TEST_F(Html5TreeConstructionTest, TextInDiv) {
    const char* html = "<html><body><div>Hello World</div></body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    Element* body = getChild(doc, 1);
    ASSERT_NE(body, nullptr);

    Element* div = getChild(body, 0);
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(getTagName(div), "div");

    // Div should have text content
    EXPECT_GE(countChildren(div), (size_t)1);
}

// ============================================================================
// Multiple Element Tests
// ============================================================================

TEST_F(Html5TreeConstructionTest, MultipleSiblings) {
    const char* html = "<html><body><div></div><div></div><div></div></body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    Element* body = getChild(doc, 1);
    ASSERT_NE(body, nullptr);

    EXPECT_EQ(countChildren(body), (size_t)3);

    for (size_t i = 0; i < 3; i++) {
        Element* div = getChild(body, i);
        ASSERT_NE(div, nullptr);
        EXPECT_EQ(getTagName(div), "div");
    }
}

TEST_F(Html5TreeConstructionTest, MixedElements) {
    const char* html = "<html><body><div></div><p></p><span></span></body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    Element* body = getChild(doc, 1);
    ASSERT_NE(body, nullptr);

    EXPECT_EQ(countChildren(body), (size_t)3);

    Element* div = getChild(body, 0);
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(getTagName(div), "div");

    Element* p = getChild(body, 1);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(getTagName(p), "p");

    Element* span = getChild(body, 2);
    ASSERT_NE(span, nullptr);
    EXPECT_EQ(getTagName(span), "span");
}

// ============================================================================
// Heading Tests
// ============================================================================

TEST_F(Html5TreeConstructionTest, Headings) {
    const char* html = "<html><body><h1></h1><h2></h2><h3></h3></body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    Element* body = getChild(doc, 1);
    ASSERT_NE(body, nullptr);

    EXPECT_EQ(countChildren(body), (size_t)3);

    EXPECT_EQ(getTagName(getChild(body, 0)), "h1");
    EXPECT_EQ(getTagName(getChild(body, 1)), "h2");
    EXPECT_EQ(getTagName(getChild(body, 2)), "h3");
}

// ============================================================================
// List Tests
// ============================================================================

TEST_F(Html5TreeConstructionTest, UnorderedList) {
    const char* html = "<html><body><ul><li></li><li></li></ul></body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    Element* body = getChild(doc, 1);
    ASSERT_NE(body, nullptr);

    Element* ul = getChild(body, 0);
    ASSERT_NE(ul, nullptr);
    EXPECT_EQ(getTagName(ul), "ul");

    EXPECT_EQ(countChildren(ul), (size_t)2);
    EXPECT_EQ(getTagName(getChild(ul, 0)), "li");
    EXPECT_EQ(getTagName(getChild(ul, 1)), "li");
}

// ============================================================================
// Void Element Tests
// ============================================================================

TEST_F(Html5TreeConstructionTest, VoidElements) {
    const char* html = "<html><body><br><hr><img></body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    Element* body = getChild(doc, 1);
    ASSERT_NE(body, nullptr);

    // Should have 3 void elements
    EXPECT_EQ(countChildren(body), (size_t)3);

    EXPECT_EQ(getTagName(getChild(body, 0)), "br");
    EXPECT_EQ(getTagName(getChild(body, 1)), "hr");
    EXPECT_EQ(getTagName(getChild(body, 2)), "img");

    // Void elements should have no children
    for (size_t i = 0; i < 3; i++) {
        Element* elem = getChild(body, i);
        EXPECT_EQ(countChildren(elem), (size_t)0);
    }
}

// ============================================================================
// Semantic HTML5 Elements
// ============================================================================

TEST_F(Html5TreeConstructionTest, SemanticElements) {
    const char* html = "<html><body><header></header><nav></nav><main></main><footer></footer></body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    ASSERT_NE(doc, nullptr);
    Element* body = getChild(doc, 1);
    ASSERT_NE(body, nullptr);

    EXPECT_EQ(countChildren(body), (size_t)4);

    EXPECT_EQ(getTagName(getChild(body, 0)), "header");
    EXPECT_EQ(getTagName(getChild(body, 1)), "nav");
    EXPECT_EQ(getTagName(getChild(body, 2)), "main");
    EXPECT_EQ(getTagName(getChild(body, 3)), "footer");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
