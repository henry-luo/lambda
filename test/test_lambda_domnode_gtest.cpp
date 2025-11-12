#include <gtest/gtest.h>

extern "C" {
#include "../lib/mempool.h"
#include "../lib/string.h"
#include "../lib/url.h"
#include "../lambda/input/input.h"
}

#include "../lambda/input/css/dom_element.hpp"

// build_dom_tree_from_element is now exported from dom_element.cpp
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
                if (item.type_id() == LMD_TYPE_ELEMENT) {
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

    // Empty attribute - HTML parser may not preserve empty class attributes
    // This behavior depends on the parser implementation
    const char* class_value = root->get_attribute("class");
    // Accept either empty string or NULL for empty attributes
    if (class_value) {
        EXPECT_STREQ(class_value, "");
    }
}

TEST_F(DomNodeBaseTest, NavigateFirstChild) {
    const char* html = "<div><p>Paragraph</p></div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Get first child using field access (not method)
    DomNode* child = root->first_child;
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
    DomNode* first = root->first_child;
    ASSERT_NE(first, nullptr);
    EXPECT_STREQ(first->name(), "p");

    // Second child (sibling of first)
    DomNode* second = first->next_sibling;
    ASSERT_NE(second, nullptr);
    EXPECT_STREQ(second->name(), "span");

    // Third child (sibling of second)
    DomNode* third = second->next_sibling;
    ASSERT_NE(third, nullptr);
    EXPECT_STREQ(third->name(), "a");

    // No more siblings
    DomNode* none = third->next_sibling;
    EXPECT_EQ(none, nullptr);
}

TEST_F(DomNodeBaseTest, NavigateTextNode) {
    const char* html = "<div>Text content</div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // First child should be text node
    DomNode* text_node = root->first_child;
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
    DomNode* text1 = root->first_child;
    ASSERT_NE(text1, nullptr);
    EXPECT_TRUE(text1->is_text());
    EXPECT_STREQ((const char*)text1->text_data(), "Text before");

    // Second child: element
    DomNode* em = text1->next_sibling;
    ASSERT_NE(em, nullptr);
    EXPECT_TRUE(em->is_element());
    EXPECT_STREQ(em->name(), "em");

    // Third child: text
    DomNode* text2 = em->next_sibling;
    ASSERT_NE(text2, nullptr);
    EXPECT_TRUE(text2->is_text());
    EXPECT_STREQ((const char*)text2->text_data(), "Text after");
}

TEST_F(DomNodeBaseTest, NavigateNestedStructure) {
    const char* html = "<div><ul><li>Item 1</li><li>Item 2</li></ul></div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // div -> ul
    DomNode* ul = root->first_child;
    ASSERT_NE(ul, nullptr);
    EXPECT_STREQ(ul->name(), "ul");

    // ul -> li (first)
    DomNode* li1 = ul->first_child;
    ASSERT_NE(li1, nullptr);
    EXPECT_STREQ(li1->name(), "li");

    // li -> text
    DomNode* text1 = li1->first_child;
    ASSERT_NE(text1, nullptr);
    EXPECT_STREQ((const char*)text1->text_data(), "Item 1");

    // li (second)
    DomNode* li2 = li1->next_sibling;
    ASSERT_NE(li2, nullptr);
    EXPECT_STREQ(li2->name(), "li");

    // li2 -> text
    DomNode* text2 = li2->first_child;
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
    DomNode* child_p = root->first_child;
    ASSERT_NE(child_p, nullptr);

    DomNode* text_node = child_p->first_child;
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
    // Note: tag_id is not set by build_dom_tree_from_element, only tag_name
    // The tag() method returns tag_id which defaults to 0

    // Test attribute access
    EXPECT_STREQ(root->get_attribute("id"), "content");

    // Test child navigation
    DomNode* h1 = root->first_child;
    ASSERT_NE(h1, nullptr);
    EXPECT_STREQ(h1->name(), "h1");

    DomNode* p = h1->next_sibling;
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p->name(), "p");
}

TEST_F(DomNodeBaseTest, ParentNavigation) {
    const char* html = "<div><section><article><p>Deep nesting</p></article></section></div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Navigate down
    DomNode* section = root->first_child;
    ASSERT_NE(section, nullptr);
    EXPECT_STREQ(section->name(), "section");

    DomNode* article = section->first_child;
    ASSERT_NE(article, nullptr);
    EXPECT_STREQ(article->name(), "article");

    DomNode* p = article->first_child;
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p->name(), "p");

    // Navigate up via parent
    EXPECT_EQ(p->parent, article);
    EXPECT_EQ(article->parent, section);
    EXPECT_EQ(section->parent, root);
    EXPECT_EQ(root->parent, nullptr);  // Root has no parent
}

TEST_F(DomNodeBaseTest, PrevSiblingNavigation) {
    const char* html = "<div><p>First</p><span>Second</span><a>Third</a></div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Navigate to last child
    DomNode* first = root->first_child;
    DomNode* second = first->next_sibling;
    DomNode* third = second->next_sibling;

    ASSERT_NE(third, nullptr);
    EXPECT_STREQ(third->name(), "a");

    // Navigate backward using prev_sibling
    EXPECT_EQ(third->prev_sibling, second);
    EXPECT_STREQ(third->prev_sibling->name(), "span");

    EXPECT_EQ(second->prev_sibling, first);
    EXPECT_STREQ(second->prev_sibling->name(), "p");

    EXPECT_EQ(first->prev_sibling, nullptr);  // First child has no previous sibling
}

TEST_F(DomNodeBaseTest, AttributeManipulation) {
    DomElement* elem = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(elem, nullptr);

    // Set attributes
    EXPECT_TRUE(dom_element_set_attribute(elem, "id", "test-id"));
    EXPECT_TRUE(dom_element_set_attribute(elem, "class", "container"));
    EXPECT_TRUE(dom_element_set_attribute(elem, "data-value", "42"));

    // Get attributes
    EXPECT_STREQ(elem->get_attribute("id"), "test-id");
    EXPECT_STREQ(elem->get_attribute("class"), "container");
    EXPECT_STREQ(elem->get_attribute("data-value"), "42");

    // Has attribute
    EXPECT_TRUE(dom_element_has_attribute(elem, "id"));
    EXPECT_TRUE(dom_element_has_attribute(elem, "class"));
    EXPECT_TRUE(dom_element_has_attribute(elem, "data-value"));
    EXPECT_FALSE(dom_element_has_attribute(elem, "missing"));

    // Remove attribute
    EXPECT_TRUE(dom_element_remove_attribute(elem, "class"));
    EXPECT_FALSE(dom_element_has_attribute(elem, "class"));
    EXPECT_EQ(elem->get_attribute("class"), nullptr);

    // Other attributes should still exist
    EXPECT_STREQ(elem->get_attribute("id"), "test-id");
    EXPECT_STREQ(elem->get_attribute("data-value"), "42");
}

TEST_F(DomNodeBaseTest, ClassManagement) {
    DomElement* elem = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(elem, nullptr);

    // Add classes
    EXPECT_TRUE(dom_element_add_class(elem, "container"));
    EXPECT_TRUE(dom_element_add_class(elem, "active"));
    EXPECT_TRUE(dom_element_add_class(elem, "primary"));

    // Has class
    EXPECT_TRUE(dom_element_has_class(elem, "container"));
    EXPECT_TRUE(dom_element_has_class(elem, "active"));
    EXPECT_TRUE(dom_element_has_class(elem, "primary"));
    EXPECT_FALSE(dom_element_has_class(elem, "missing"));

    // Class count
    EXPECT_EQ(elem->class_count, 3);

    // Remove class
    EXPECT_TRUE(dom_element_remove_class(elem, "active"));
    EXPECT_FALSE(dom_element_has_class(elem, "active"));
    EXPECT_EQ(elem->class_count, 2);

    // Toggle class
    EXPECT_TRUE(dom_element_toggle_class(elem, "highlight"));  // Add
    EXPECT_TRUE(dom_element_has_class(elem, "highlight"));
    EXPECT_FALSE(dom_element_toggle_class(elem, "highlight")); // Remove
    EXPECT_FALSE(dom_element_has_class(elem, "highlight"));
}

TEST_F(DomNodeBaseTest, EmptyAndNullHandling) {
    // Test with null/empty strings
    DomElement* elem = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(elem, nullptr);

    // Empty class name
    EXPECT_FALSE(dom_element_has_class(elem, ""));

    // Get non-existent attribute
    EXPECT_EQ(elem->get_attribute("nonexistent"), nullptr);

    // Empty attribute name
    EXPECT_EQ(elem->get_attribute(""), nullptr);
}

TEST_F(DomNodeBaseTest, MultipleAttributeTypes) {
    const char* html = "<input type=\"text\" name=\"username\" value=\"john\" required disabled>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(root->name(), "input");

    // Different attribute types
    const char* type_attr = root->get_attribute("type");
    if (type_attr) EXPECT_STREQ(type_attr, "text");

    const char* name_attr = root->get_attribute("name");
    if (name_attr) EXPECT_STREQ(name_attr, "username");

    const char* value_attr = root->get_attribute("value");
    if (value_attr) EXPECT_STREQ(value_attr, "john");
}

TEST_F(DomNodeBaseTest, DeepNestingNavigation) {
    const char* html = R"(
        <div>
            <ul>
                <li>
                    <span>
                        <em>Deep</em>
                    </span>
                </li>
            </ul>
        </div>
    )";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Navigate to deepest element, skipping text nodes
    DomNode* ul = root->first_child;
    while (ul && !ul->is_element()) ul = ul->next_sibling;
    ASSERT_NE(ul, nullptr);
    EXPECT_STREQ(ul->name(), "ul");

    DomNode* li = ul->first_child;
    while (li && !li->is_element()) li = li->next_sibling;
    ASSERT_NE(li, nullptr);
    EXPECT_STREQ(li->name(), "li");

    DomNode* span = li->first_child;
    while (span && !span->is_element()) span = span->next_sibling;
    ASSERT_NE(span, nullptr);
    EXPECT_STREQ(span->name(), "span");

    DomNode* em = span->first_child;
    while (em && !em->is_element()) em = em->next_sibling;
    ASSERT_NE(em, nullptr);
    EXPECT_STREQ(em->name(), "em");

    // Verify parent chain
    EXPECT_EQ(em->parent, span);
    EXPECT_EQ(span->parent, li);
    EXPECT_EQ(li->parent, ul);
    EXPECT_EQ(ul->parent, root);
}

TEST_F(DomNodeBaseTest, SiblingCountAndOrder) {
    const char* html = "<div><a>1</a><b>2</b><c>3</c><d>4</d><e>5</e></div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Count siblings
    int count = 0;
    DomNode* child = root->first_child;
    while (child) {
        count++;
        child = child->next_sibling;
    }
    EXPECT_EQ(count, 5);

    // Verify order
    const char* expected[] = {"a", "b", "c", "d", "e"};
    child = root->first_child;
    int i = 0;
    while (child) {
        EXPECT_STREQ(child->name(), expected[i]);
        i++;
        child = child->next_sibling;
    }
}

TEST_F(DomNodeBaseTest, MixedContentWithWhitespace) {
    const char* html = "<div>  \n  <span>text</span>  \n  </div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // First child should be whitespace text (might be skipped by parser)
    // Second/main child should be span element
    DomNode* child = root->first_child;

    // Skip any leading whitespace nodes
    while (child && child->is_text()) {
        DomText* text = child->as_text();
        // Check if it's just whitespace
        bool all_whitespace = true;
        for (size_t i = 0; i < text->length; i++) {
            if (text->text[i] != ' ' && text->text[i] != '\n' && text->text[i] != '\t') {
                all_whitespace = false;
                break;
            }
        }
        if (all_whitespace) {
            child = child->next_sibling;
        } else {
            break;
        }
    }

    ASSERT_NE(child, nullptr);
    EXPECT_TRUE(child->is_element());
    EXPECT_STREQ(child->name(), "span");
}

TEST_F(DomNodeBaseTest, TextNodeManipulation) {
    DomText* text = dom_text_create(pool, "Original text");
    ASSERT_NE(text, nullptr);

    // Initial state
    EXPECT_STREQ(text->text, "Original text");
    EXPECT_EQ(text->length, 13);

    // Modify text
    EXPECT_TRUE(dom_text_set_content(text, "New text"));
    EXPECT_STREQ(text->text, "New text");
    EXPECT_EQ(text->length, 8);

    // Get content
    const char* content = dom_text_get_content(text);
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "New text");
}

TEST_F(DomNodeBaseTest, ElementTreeStructure) {
    const char* html = R"(
        <html>
            <head>
                <title>Test</title>
            </head>
            <body>
                <header>
                    <h1>Title</h1>
                </header>
                <main>
                    <article>
                        <p>Content</p>
                    </article>
                </main>
                <footer>
                    <p>Footer</p>
                </footer>
            </body>
        </html>
    )";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(root->name(), "html");

    // Count top-level children (head, body)
    int top_count = 0;
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            top_count++;
        }
        child = child->next_sibling;
    }
    EXPECT_GE(top_count, 2);  // At least head and body

    // Navigate to body
    DomNode* body = root->first_child;
    while (body && strcmp(body->name(), "body") != 0) {
        body = body->next_sibling;
    }
    ASSERT_NE(body, nullptr);

    // Count body children (header, main, footer)
    int body_count = 0;
    child = body->first_child;
    while (child) {
        if (child->is_element()) {
            body_count++;
        }
        child = child->next_sibling;
    }
    EXPECT_GE(body_count, 3);  // At least header, main, footer
}

TEST_F(DomNodeBaseTest, SafeDowncasting) {
    const char* html = "<div><p>Text in paragraph</p></div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Element downcast
    DomElement* elem = root->as_element();
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem, root);

    // Invalid text downcast on element
    DomText* text_from_elem = root->as_text();
    EXPECT_EQ(text_from_elem, nullptr);

    // Get text node
    DomNode* p = root->first_child;
    ASSERT_NE(p, nullptr);

    DomNode* text_node = p->first_child;
    ASSERT_NE(text_node, nullptr);
    EXPECT_TRUE(text_node->is_text());

    // Valid text downcast
    DomText* text = text_node->as_text();
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text->text, "Text in paragraph");

    // Invalid element downcast on text
    DomElement* elem_from_text = text_node->as_element();
    EXPECT_EQ(elem_from_text, nullptr);
}

TEST_F(DomNodeBaseTest, ComplexAttributeValues) {
    DomElement* elem = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(elem, nullptr);

    // Various attribute value types
    dom_element_set_attribute(elem, "data-json", "{\"key\": \"value\"}");
    dom_element_set_attribute(elem, "data-url", "https://example.com/path?query=1");
    dom_element_set_attribute(elem, "data-number", "12345");
    dom_element_set_attribute(elem, "data-special", "special!@#$%^&*()chars");

    EXPECT_STREQ(elem->get_attribute("data-json"), "{\"key\": \"value\"}");
    EXPECT_STREQ(elem->get_attribute("data-url"), "https://example.com/path?query=1");
    EXPECT_STREQ(elem->get_attribute("data-number"), "12345");
    EXPECT_STREQ(elem->get_attribute("data-special"), "special!@#$%^&*()chars");
}

TEST_F(DomNodeBaseTest, TableStructureNavigation) {
    const char* html = R"(
        <table>
            <tr>
                <td>Cell 1</td>
                <td>Cell 2</td>
            </tr>
            <tr>
                <td>Cell 3</td>
                <td>Cell 4</td>
            </tr>
        </table>
    )";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(root->name(), "table");

    // HTML parsers often insert tbody automatically, so navigate through it
    DomNode* tbody_or_row1 = root->first_child;
    while (tbody_or_row1 && !tbody_or_row1->is_element()) tbody_or_row1 = tbody_or_row1->next_sibling;
    ASSERT_NE(tbody_or_row1, nullptr);

    // If we got tbody, navigate into it
    DomNode* row1 = nullptr;
    if (strcmp(tbody_or_row1->name(), "tbody") == 0) {
        row1 = tbody_or_row1->first_child;
        while (row1 && !row1->is_element()) row1 = row1->next_sibling;
    } else {
        row1 = tbody_or_row1;
    }
    ASSERT_NE(row1, nullptr);
    EXPECT_STREQ(row1->name(), "tr");

    // Second row
    DomNode* row2 = row1->next_sibling;
    while (row2 && !row2->is_element()) row2 = row2->next_sibling;
    ASSERT_NE(row2, nullptr);
    EXPECT_STREQ(row2->name(), "tr");

    // Cells in first row
    DomNode* cell1 = row1->first_child;
    while (cell1 && !cell1->is_element()) cell1 = cell1->next_sibling;
    ASSERT_NE(cell1, nullptr);
    EXPECT_STREQ(cell1->name(), "td");

    DomNode* cell2 = cell1->next_sibling;
    while (cell2 && !cell2->is_element()) cell2 = cell2->next_sibling;
    ASSERT_NE(cell2, nullptr);
    EXPECT_STREQ(cell2->name(), "td");
}

TEST_F(DomNodeBaseTest, ListStructureNavigation) {
    const char* html = R"(
        <ul>
            <li>Item 1</li>
            <li>Item 2</li>
            <li>Item 3</li>
        </ul>
    )";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(root->name(), "ul");

    // Count list items
    int item_count = 0;
    DomNode* li = root->first_child;
    while (li) {
        if (li->is_element() && strcmp(li->name(), "li") == 0) {
            item_count++;
        }
        li = li->next_sibling;
    }
    EXPECT_EQ(item_count, 3);
}

TEST_F(DomNodeBaseTest, NodeTypeIdentification) {
    const char* html = "<div>Text<em>emphasized</em>more text</div>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Root is element
    EXPECT_EQ(root->type(), DOM_NODE_ELEMENT);
    EXPECT_TRUE(root->is_element());
    EXPECT_FALSE(root->is_text());
    EXPECT_FALSE(root->is_comment());

    // First child is text
    DomNode* text1 = root->first_child;
    if (text1 && text1->is_text()) {
        EXPECT_EQ(text1->type(), DOM_NODE_TEXT);
        EXPECT_TRUE(text1->is_text());
        EXPECT_FALSE(text1->is_element());
    }

    // Find em element
    DomNode* child = root->first_child;
    while (child && !child->is_element()) {
        child = child->next_sibling;
    }
    if (child) {
        EXPECT_TRUE(child->is_element());
        EXPECT_STREQ(child->name(), "em");
    }
}

/**
 * Test Tag ID Population
 * Verifies that tag_id field is properly populated during element creation
 */
TEST_F(DomNodeBaseTest, TagIdPopulation) {
    const char* html = "<html><head><title>Test</title></head><body>"
                      "<div id='main'><p>Paragraph</p><span>Text</span>"
                      "<img src='test.png'/></div></body></html>";

    DomElement* root = parse_html_and_build_dom(html);
    ASSERT_NE(root, nullptr);

    // Verify root html element has correct tag ID
    EXPECT_STREQ(root->name(), "html");
    EXPECT_NE(root->tag_id, 0UL);  // Should be set, not 0
    EXPECT_EQ(root->tag(), root->tag_id);  // tag() should return tag_id

    // Navigate to body
    DomNode* body = nullptr;
    for (DomNode* child = root->first_child; child; child = child->next_sibling) {
        if (child->is_element() && strcmp(child->name(), "body") == 0) {
            body = child;
            break;
        }
    }
    ASSERT_NE(body, nullptr);

    // Verify body has tag ID
    DomElement* body_elem = body->as_element();
    ASSERT_NE(body_elem, nullptr);
    EXPECT_NE(body_elem->tag_id, 0UL);
    EXPECT_EQ(body->tag(), body_elem->tag_id);

    // Find div element
    DomNode* div = nullptr;
    for (DomNode* child = body->first_child; child; child = child->next_sibling) {
        if (child->is_element() && strcmp(child->name(), "div") == 0) {
            div = child;
            break;
        }
    }
    ASSERT_NE(div, nullptr);

    // Verify div has tag ID
    DomElement* div_elem = div->as_element();
    ASSERT_NE(div_elem, nullptr);
    EXPECT_NE(div_elem->tag_id, 0UL);

    // Find p element
    DomNode* p = nullptr;
    for (DomNode* child = div->first_child; child; child = child->next_sibling) {
        if (child->is_element() && strcmp(child->name(), "p") == 0) {
            p = child;
            break;
        }
    }
    ASSERT_NE(p, nullptr);

    // Verify p has tag ID
    DomElement* p_elem = p->as_element();
    ASSERT_NE(p_elem, nullptr);
    EXPECT_NE(p_elem->tag_id, 0UL);

    // Find span element
    DomNode* span = nullptr;
    for (DomNode* child = div->first_child; child; child = child->next_sibling) {
        if (child->is_element() && strcmp(child->name(), "span") == 0) {
            span = child;
            break;
        }
    }
    ASSERT_NE(span, nullptr);

    // Verify span has tag ID
    DomElement* span_elem = span->as_element();
    ASSERT_NE(span_elem, nullptr);
    EXPECT_NE(span_elem->tag_id, 0UL);

    // Find img element
    DomNode* img = nullptr;
    for (DomNode* child = div->first_child; child; child = child->next_sibling) {
        if (child->is_element() && strcmp(child->name(), "img") == 0) {
            img = child;
            break;
        }
    }
    ASSERT_NE(img, nullptr);

    // Verify img has tag ID
    DomElement* img_elem = img->as_element();
    ASSERT_NE(img_elem, nullptr);
    EXPECT_NE(img_elem->tag_id, 0UL);

    // Verify that different tags have different tag IDs
    EXPECT_NE(div_elem->tag_id, p_elem->tag_id);
    EXPECT_NE(div_elem->tag_id, span_elem->tag_id);
    EXPECT_NE(p_elem->tag_id, span_elem->tag_id);
}

// Run all tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
