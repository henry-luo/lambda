#include <gtest/gtest.h>

extern "C" {
#include "../lib/mempool.h"
#include "../lib/string.h"
#include "../lib/url.h"
#include "../lambda/input/input.h"
}

#include "../lambda/input/css/dom_element.hpp"

// Forward declaration of helper function (defined in radiant/cmd_layout.cpp)
DomElement* build_dom_tree_from_element(Element* elem, Pool* pool, DomElement* parent);

/**
 * Test DomNodeBase Integration (C++ Inheritance Model)
 *
 * Tests the C++ inheritance-based DOM system with DomNodeBase, DomElement, and DomText.
 * Tests navigation, attribute access, and type checking using the new simplified API.
 */

class DomNodeBaseTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
    }

    void TearDown() override {
        if (pool) {
            pool_destroy(pool);
        }
    }

    // Helper: Create Lambda string
    String* create_lambda_string(const char* text) {
        if (!text) return nullptr;

        size_t len = strlen(text);
        String* result = (String*)malloc(sizeof(String) + len + 1);
        result->len = len;
        memcpy(result->chars, text, len + 1);
        return result;
    }

    // Helper: Parse HTML string using Lambda parser and build DomElement tree
    DomElement* parse_html_and_build_dom(const char* html_content) {
        String* type_str = create_lambda_string("html");
        Url* url = url_parse("file://test.html");

        char* content_copy = strdup(html_content);
        Input* input = input_from_source(content_copy, url, type_str, nullptr);
        free(content_copy);

        if (!input) return nullptr;

        // Get root element from Lambda parser
        Element* lambda_root = get_html_root_element(input);
        if (!lambda_root) return nullptr;

        // Build DomElement tree from Lambda Element tree
        return build_dom_tree_from_element(lambda_root, pool, nullptr);
    }

    // Helper: Get HTML root element (skip DOCTYPE)
    Element* get_html_root_element(Input* input) {
        void* root_ptr = (void*)input->root.pointer;
        List* root_list = (List*)root_ptr;

        if (root_list->type_id == LMD_TYPE_LIST) {
            for (int64_t i = 0; i < root_list->length; i++) {
                Item item = root_list->items[i];
                if (item.type_id == LMD_TYPE_ELEMENT) {
                    Element* elem = (Element*)item.pointer;
                    TypeElmt* type = (TypeElmt*)elem->type;

                    // Skip DOCTYPE and comments
                    if (strcmp(type->name.str, "!DOCTYPE") != 0 &&
                        strcmp(type->name.str, "!--") != 0) {
                        return elem;
                    }
                }
            }
        } else if (root_list->type_id == LMD_TYPE_ELEMENT) {
            return (Element*)root_ptr;
        }

        return nullptr;
    }
};

TEST_F(DomNodeBaseTest, CreateDomElement) {
    const char* html = "<div id=\"test\">Hello</div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Test DomNodeBase API
    EXPECT_EQ(root->type(), DOM_NODE_ELEMENT);
    EXPECT_TRUE(root->is_element());
    EXPECT_FALSE(root->is_text());

    // Test element-specific access
    DomElement* elem = root->as_element();
    ASSERT_NE(elem, nullptr);
    EXPECT_STREQ(elem->tag_name, "div");
}

TEST_F(DomNodeBaseTest, CreateDomText) {
    DomText* text = dom_text_create(pool, "Hello World");
    ASSERT_NE(text, nullptr);

    // Test DomNodeBase API
    EXPECT_EQ(text->type(), DOM_NODE_TEXT);
    EXPECT_TRUE(text->is_text());
    EXPECT_FALSE(text->is_element());

    // Test name() method
    EXPECT_STREQ(text->name(), "#text");

    // Test text data
    EXPECT_STREQ(text->text, "Hello World");
    EXPECT_EQ(text->length, 11);
}

TEST_F(DomNodeBaseTest, GetTagName) {
    const char* html = "<div>Content</div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Test simplified API
    const char* tag_name = root->name();
    ASSERT_NE(tag_name, nullptr);
    EXPECT_STREQ(tag_name, "div");
}

TEST_F(DomNodeBaseTest, GetAttribute) {
    const char* html = "<div id=\"main\" class=\"container\">Content</div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Test simplified get_attribute() API
    const char* id_value = root->get_attribute("id");
    ASSERT_NE(id_value, nullptr);
    EXPECT_STREQ(id_value, "main");

    const char* class_value = root->get_attribute("class");
    ASSERT_NE(class_value, nullptr);
    EXPECT_STREQ(class_value, "container");

    // Get non-existent attribute
    const char* missing = root->get_attribute("missing");
    EXPECT_EQ(missing, nullptr);
}

TEST_F(DomNodeBaseTest, GetBooleanAttribute) {
    const char* html = "<input disabled checked=\"checked\">";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Boolean attribute (no value) - stored as "true"
    const char* disabled = root->get_attribute("disabled");
    ASSERT_NE(disabled, nullptr);
    // Note: Implementation may store boolean differently

    // Boolean attribute with value
    const char* checked = root->get_attribute("checked");
    ASSERT_NE(checked, nullptr);
    EXPECT_STREQ(checked, "checked");
}

TEST_F(DomNodeBaseTest, GetEmptyAttribute) {
    const char* html = "<div class=\"\">Content</div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Empty attribute
    const char* class_value = root->get_attribute("class");
    ASSERT_NE(class_value, nullptr);
    EXPECT_STREQ(class_value, "");
}

TEST_F(DomNodeBaseTest, NavigateFirstChild) {
    const char* html = "<div><p>Paragraph</p></div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Get first child using field access (not method)
    DomNodeBase* child = root->first_child;
    ASSERT_NE(child, nullptr);
    EXPECT_TRUE(child->is_element());
    EXPECT_EQ(child->parent, root);

    const char* child_tag = child->name();
    EXPECT_STREQ(child_tag, "p");
}

TEST_F(DomNodeBaseTest, NavigateMultipleChildren) {
    const char* html = "<div><p>First</p><span>Second</span><a>Third</a></div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // First child
    DomNodeBase* first = root->first_child;
    ASSERT_NE(first, nullptr);
    EXPECT_STREQ(first->name(), "p");

    // Second child (sibling of first)
    DomNodeBase* second = first->next_sibling;
    ASSERT_NE(second, nullptr);
    EXPECT_STREQ(second->name(), "span");

    // Third child (sibling of second)
    DomNodeBase* third = second->next_sibling;
    ASSERT_NE(third, nullptr);
    EXPECT_STREQ(third->name(), "a");

    // No more siblings
    DomNodeBase* none = third->next_sibling;
    EXPECT_EQ(none, nullptr);
}

TEST_F(DomNodeBaseTest, NavigateTextNode) {
    const char* html = "<div>Text content</div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // First child should be text node
    DomNodeBase* text_node = root->first_child;
    ASSERT_NE(text_node, nullptr);
    EXPECT_TRUE(text_node->is_text());
    EXPECT_EQ(text_node->type(), DOM_NODE_TEXT);

    // Get text data using simplified API
    unsigned char* text_data = text_node->text_data();
    ASSERT_NE(text_data, nullptr);
    EXPECT_STREQ((const char*)text_data, "Text content");

    // Test safe downcasting
    DomText* dom_text = text_node->as_text();
    ASSERT_NE(dom_text, nullptr);
    EXPECT_STREQ(dom_text->text, "Text content");
}

TEST_F(DomNodeBaseTest, NavigateMixedContent) {
    const char* html = "<div>Text before<em>emphasized</em>Text after</div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // First child: text
    DomNodeBase* text1 = root->first_child;
    ASSERT_NE(text1, nullptr);
    EXPECT_TRUE(text1->is_text());
    EXPECT_STREQ((const char*)text1->text_data(), "Text before");

    // Second child: element
    DomNodeBase* em = text1->next_sibling;
    ASSERT_NE(em, nullptr);
    EXPECT_TRUE(em->is_element());
    EXPECT_STREQ(em->name(), "em");

    // Third child: text
    DomNodeBase* text2 = em->next_sibling;
    ASSERT_NE(text2, nullptr);
    EXPECT_TRUE(text2->is_text());
    EXPECT_STREQ((const char*)text2->text_data(), "Text after");
}

TEST_F(DomNodeBaseTest, NavigateNestedStructure) {
    const char* html = "<div><ul><li>Item 1</li><li>Item 2</li></ul></div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // div -> ul
    DomNodeBase* ul = root->first_child;
    ASSERT_NE(ul, nullptr);
    EXPECT_STREQ(ul->name(), "ul");

    // ul -> li (first)
    DomNodeBase* li1 = ul->first_child;
    ASSERT_NE(li1, nullptr);
    EXPECT_STREQ(li1->name(), "li");

    // li -> text
    DomNodeBase* text1 = li1->first_child;
    ASSERT_NE(text1, nullptr);
    EXPECT_STREQ((const char*)text1->text_data(), "Item 1");

    // li (second)
    DomNodeBase* li2 = li1->next_sibling;
    ASSERT_NE(li2, nullptr);
    EXPECT_STREQ(li2->name(), "li");

    // li2 -> text
    DomNodeBase* text2 = li2->first_child;
    ASSERT_NE(text2, nullptr);
    EXPECT_STREQ((const char*)text2->text_data(), "Item 2");
}

TEST_F(DomNodeBaseTest, TypeChecking) {
    const char* html = "<div><p>Paragraph</p></div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Test element type checking
    EXPECT_TRUE(root->is_element());
    EXPECT_FALSE(root->is_text());
    EXPECT_FALSE(root->is_comment());

    // Test safe downcasting
    DomElement* elem = root->as_element();
    EXPECT_EQ(elem, root);

    DomText* text = root->as_text();
    EXPECT_EQ(text, nullptr);  // Should fail for element

    // Test child (should be text)
    DomNodeBase* child_p = root->first_child;
    ASSERT_NE(child_p, nullptr);

    DomNodeBase* text_node = child_p->first_child;
    ASSERT_NE(text_node, nullptr);

    EXPECT_TRUE(text_node->is_text());
    EXPECT_FALSE(text_node->is_element());

    DomText* text_ptr = text_node->as_text();
    ASSERT_NE(text_ptr, nullptr);
    EXPECT_STREQ(text_ptr->text, "Paragraph");
}

TEST_F(DomNodeBaseTest, SimplifiedAPIConsistency) {
    const char* html = "<section id=\"content\"><h1>Title</h1><p>Text</p></section>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Test consistent API across nodes
    EXPECT_STREQ(root->name(), "section");
    EXPECT_EQ(root->type(), DOM_NODE_ELEMENT);
    EXPECT_NE(root->tag(), 0);  // Should have tag ID

    // Test attribute access
    EXPECT_STREQ(root->get_attribute("id"), "content");

    // Test child navigation
    DomNodeBase* h1 = root->first_child;
    ASSERT_NE(h1, nullptr);
    EXPECT_STREQ(h1->name(), "h1");

    DomNodeBase* p = h1->next_sibling;
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p->name(), "p");
}

// Run all tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
