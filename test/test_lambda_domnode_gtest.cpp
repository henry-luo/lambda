#include <gtest/gtest.h>

extern "C" {
#include "../lib/mempool.h"
#include "../lib/string.h"
#include "../lib/url.h"
#include "../lambda/input/input.h"
}

#include "../radiant/dom.hpp"

/**
 * Test Lambda DomNode Integration
 *
 * Tests that DomNode correctly wraps Lambda Element structures
 * and provides proper navigation and attribute access.
 *
 * NOTE: These tests are currently disabled because they test unimplemented functionality.
 * DomNode::create_mark_element() expects DomElement*, not Lambda Element*.
 * DomNode::create_mark_text() expects DomText*, not Lambda String*.
 * A conversion layer would need to be implemented to enable these tests.
 */

#if 0  // Disabled - requires conversion layer from Lambda Element* to DomElement*

class LambdaDomNodeTest : public ::testing::Test {
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

    // Helper: Parse HTML string using Lambda parser
    Input* parse_html_source(const char* html_content) {
        String* type_str = create_lambda_string("html");
        Url* url = url_parse("file://test.html");

        char* content_copy = strdup(html_content);
        Input* input = input_from_source(content_copy, url, type_str, nullptr);
        free(content_copy);

        return input;
    }

    // Helper: Get root HTML element (skip DOCTYPE)
    Element* get_root_element(Input* input) {
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

TEST_F(LambdaDomNodeTest, CreateMarkElement) {
    const char* html = "<div id=\"test\">Hello</div>";

    Input* input = parse_html_source(html);
    ASSERT_NE(input, nullptr);

    Element* root = get_root_element(input);
    ASSERT_NE(root, nullptr);

    // Create DomNode from Lambda Element
    DomNode* node = DomNode::create_mark_element(root);
    ASSERT_NE(node, nullptr);

    EXPECT_EQ(node->type, MARK_ELEMENT);
    EXPECT_EQ(node->mark_element, root);
}

TEST_F(LambdaDomNodeTest, CreateMarkText) {
    String* text = create_lambda_string("Hello World");

    DomNode* node = DomNode::create_mark_text(text);
    ASSERT_NE(node, nullptr);

    EXPECT_EQ(node->type, MARK_TEXT);
    EXPECT_EQ(node->mark_text, text);
}

TEST_F(LambdaDomNodeTest, GetTagName) {
    const char* html = "<div>Content</div>";

    Input* input = parse_html_source(html);
    Element* root = get_root_element(input);
    DomNode* node = DomNode::create_mark_element(root);

    char* tag_name = node->name();
    ASSERT_NE(tag_name, nullptr);
    EXPECT_STREQ(tag_name, "div");
}

TEST_F(LambdaDomNodeTest, GetAttribute) {
    const char* html = "<div id=\"main\" class=\"container\">Content</div>";

    Input* input = parse_html_source(html);
    Element* root = get_root_element(input);
    DomNode* node = DomNode::create_mark_element(root);

    // Get id attribute
    size_t id_len;
    const lxb_char_t* id_value = node->get_attribute("id", &id_len);
    ASSERT_NE(id_value, nullptr);
    EXPECT_STREQ((const char*)id_value, "main");
    EXPECT_EQ(id_len, 4);

    // Get class attribute
    size_t class_len;
    const lxb_char_t* class_value = node->get_attribute("class", &class_len);
    ASSERT_NE(class_value, nullptr);
    EXPECT_STREQ((const char*)class_value, "container");
    EXPECT_EQ(class_len, 9);

    // Get non-existent attribute
    const lxb_char_t* missing = node->get_attribute("missing", nullptr);
    EXPECT_EQ(missing, nullptr);
}

TEST_F(LambdaDomNodeTest, GetBooleanAttribute) {
    const char* html = "<input disabled checked=\"checked\">";

    Input* input = parse_html_source(html);
    Element* root = get_root_element(input);
    DomNode* node = DomNode::create_mark_element(root);

    // Boolean attribute (no value)
    const lxb_char_t* disabled = node->get_attribute("disabled", nullptr);
    ASSERT_NE(disabled, nullptr);
    EXPECT_STREQ((const char*)disabled, "true");

    // Boolean attribute with value
    const lxb_char_t* checked = node->get_attribute("checked", nullptr);
    ASSERT_NE(checked, nullptr);
    EXPECT_STREQ((const char*)checked, "checked");
}

TEST_F(LambdaDomNodeTest, GetEmptyAttribute) {
    const char* html = "<div class=\"\">Content</div>";

    Input* input = parse_html_source(html);
    Element* root = get_root_element(input);
    DomNode* node = DomNode::create_mark_element(root);

    // Empty attribute
    size_t len;
    const lxb_char_t* class_value = node->get_attribute("class", &len);
    ASSERT_NE(class_value, nullptr);
    EXPECT_EQ(len, 0);
    EXPECT_STREQ((const char*)class_value, "");
}

TEST_F(LambdaDomNodeTest, NavigateFirstChild) {
    const char* html = "<div><p>Paragraph</p></div>";

    Input* input = parse_html_source(html);
    Element* root = get_root_element(input);
    DomNode* div_node = DomNode::create_mark_element(root);

    // Get first child
    DomNode* child = div_node->first_child();
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->type, MARK_ELEMENT);
    EXPECT_EQ(child->parent, div_node);

    char* child_tag = child->name();
    EXPECT_STREQ(child_tag, "p");
}

TEST_F(LambdaDomNodeTest, NavigateMultipleChildren) {
    const char* html = "<div><p>First</p><span>Second</span><a>Third</a></div>";

    Input* input = parse_html_source(html);
    Element* root = get_root_element(input);
    DomNode* div_node = DomNode::create_mark_element(root);

    // First child
    DomNode* first = div_node->first_child();
    ASSERT_NE(first, nullptr);
    EXPECT_STREQ(first->name(), "p");

    // Second child (sibling of first)
    DomNode* second = first->next_sibling();
    ASSERT_NE(second, nullptr);
    EXPECT_STREQ(second->name(), "span");

    // Third child (sibling of second)
    DomNode* third = second->next_sibling();
    ASSERT_NE(third, nullptr);
    EXPECT_STREQ(third->name(), "a");

    // No more siblings
    DomNode* none = third->next_sibling();
    EXPECT_EQ(none, nullptr);
}

TEST_F(LambdaDomNodeTest, NavigateTextNode) {
    const char* html = "<div>Text content</div>";

    Input* input = parse_html_source(html);
    Element* root = get_root_element(input);
    DomNode* div_node = DomNode::create_mark_element(root);

    // First child should be text node
    DomNode* text_node = div_node->first_child();
    ASSERT_NE(text_node, nullptr);
    EXPECT_EQ(text_node->type, MARK_TEXT);
    EXPECT_TRUE(text_node->is_text());

    // Get text data
    unsigned char* text_data = text_node->text_data();
    ASSERT_NE(text_data, nullptr);
    EXPECT_STREQ((const char*)text_data, "Text content");
}

TEST_F(LambdaDomNodeTest, NavigateMixedContent) {
    const char* html = "<div>Text before<em>emphasized</em>Text after</div>";

    Input* input = parse_html_source(html);
    Element* root = get_root_element(input);
    DomNode* div_node = DomNode::create_mark_element(root);

    // First child: text
    DomNode* text1 = div_node->first_child();
    ASSERT_NE(text1, nullptr);
    EXPECT_EQ(text1->type, MARK_TEXT);
    EXPECT_STREQ((const char*)text1->text_data(), "Text before");

    // Second child: element
    DomNode* em = text1->next_sibling();
    ASSERT_NE(em, nullptr);
    EXPECT_EQ(em->type, MARK_ELEMENT);
    EXPECT_STREQ(em->name(), "em");

    // Third child: text
    DomNode* text2 = em->next_sibling();
    ASSERT_NE(text2, nullptr);
    EXPECT_EQ(text2->type, MARK_TEXT);
    EXPECT_STREQ((const char*)text2->text_data(), "Text after");
}

TEST_F(LambdaDomNodeTest, NavigateNestedStructure) {
    const char* html = "<div><ul><li>Item 1</li><li>Item 2</li></ul></div>";

    Input* input = parse_html_source(html);
    Element* root = get_root_element(input);
    DomNode* div_node = DomNode::create_mark_element(root);

    // div -> ul
    DomNode* ul = div_node->first_child();
    ASSERT_NE(ul, nullptr);
    EXPECT_STREQ(ul->name(), "ul");

    // ul -> li (first)
    DomNode* li1 = ul->first_child();
    ASSERT_NE(li1, nullptr);
    EXPECT_STREQ(li1->name(), "li");

    // li -> text
    DomNode* text1 = li1->first_child();
    ASSERT_NE(text1, nullptr);
    EXPECT_STREQ((const char*)text1->text_data(), "Item 1");

    // li (second)
    DomNode* li2 = li1->next_sibling();
    ASSERT_NE(li2, nullptr);
    EXPECT_STREQ(li2->name(), "li");

    // li2 -> text
    DomNode* text2 = li2->first_child();
    ASSERT_NE(text2, nullptr);
    EXPECT_STREQ((const char*)text2->text_data(), "Item 2");
}

TEST_F(LambdaDomNodeTest, CachedNavigation) {
    const char* html = "<div><p>Child</p></div>";

    Input* input = parse_html_source(html);
    Element* root = get_root_element(input);
    DomNode* div_node = DomNode::create_mark_element(root);

    // First access creates cache
    DomNode* child1 = div_node->first_child();
    ASSERT_NE(child1, nullptr);

    // Second access should return cached
    DomNode* child2 = div_node->first_child();
    EXPECT_EQ(child1, child2);  // Same pointer
}

#endif  // End of disabled tests

// Run all tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
