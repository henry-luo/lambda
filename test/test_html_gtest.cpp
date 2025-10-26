#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

extern "C" {
    #include "../lambda/input/input.h"
    #include "../lib/mempool.h"
    #include "../lib/stringbuf.h"
    #include "../lib/strview.h"
    void parse_html(Input* input, const char* html);
    void rpmalloc_initialize();
    void rpmalloc_finalize();
}

// Test fixture for HTML parsing tests
class HtmlParserTest : public ::testing::Test {
protected:
    Pool* pool;
    Input* input;

    void SetUp() override {
        rpmalloc_initialize();
        pool = pool_create(NULL);
        ASSERT_NE(pool, nullptr);

        input = input_new(NULL);
        ASSERT_NE(input, nullptr);
        input->pool = pool;
    }

    void TearDown() override {
        if (pool) {
            pool_destroy(pool);
        }
        rpmalloc_finalize();
    }

    // Helper: Parse HTML and return root element
    Item parseHtml(const char* html) {
        parse_html(input, html);
        return input->root;
    }

    // Helper: Get element by tag name from a list/element
    Element* findElementByTag(Item item, const char* tag_name) {
        if (item.item == ITEM_NULL || item.item == ITEM_ERROR) {
            return nullptr;
        }

        if (item.type_id == LMD_TYPE_ELMT) {
            Element* elem = item.element;
            TypeElmt* type = (TypeElmt*)elem->type;
            if (strview_equal(&type->name, tag_name)) {
                return elem;
            }

            // Check children: skip attributes, check content
            List* elem_list = (List*)elem;
            int64_t attr_count = elem_list->length - type->content_length;
            for (int64_t i = attr_count; i < elem_list->length; i++) {
                Element* found = findElementByTag(elem_list->items[i], tag_name);
                if (found) return found;
            }
        } else if (item.type_id == LMD_TYPE_LIST) {
            List* list = item.list;
            for (int64_t i = 0; i < list->length; i++) {
                Element* found = findElementByTag(list->items[i], tag_name);
                if (found) return found;
            }
        }

        return nullptr;
    }

    // Helper: Get attribute value from element
    const char* getAttr(Element* elem, const char* attr_name) {
        if (!elem) return nullptr;

        TypeElmt* type = (TypeElmt*)elem->type;
        List* elem_list = (List*)elem;

        // Attributes are stored before content
        int64_t attr_count = elem_list->length - type->content_length;
        for (int64_t i = 0; i < attr_count; i++) {
            Item attr_item = elem_list->items[i];
            if (attr_item.type_id == LMD_TYPE_ELMT) {
                Element* attr_elem = attr_item.element;
                TypeElmt* attr_type = (TypeElmt*)attr_elem->type;

                if (strview_equal(&attr_type->name, attr_name)) {
                    // Get attribute value (first content item)
                    List* attr_list = (List*)attr_elem;
                    if (attr_list->length > 0) {
                        Item val_item = attr_list->items[0];
                        if (val_item.type_id == LMD_TYPE_STRING) {
                            return val_item.string->chars;
                        }
                    }
                    return nullptr;
                }
            }
        }

        return nullptr;
    }

    // Helper: Count elements with specific tag name
    int countElementsByTag(Item item, const char* tag_name) {
        int count = 0;

        if (item.item == ITEM_NULL || item.item == ITEM_ERROR) {
            return 0;
        }

        if (item.type_id == LMD_TYPE_ELMT) {
            Element* elem = item.element;
            TypeElmt* type = (TypeElmt*)elem->type;
            if (strview_equal(&type->name, tag_name)) {
                count = 1;
            }

            // Check children
            List* elem_list = (List*)elem;
            int64_t attr_count = elem_list->length - type->content_length;
            for (int64_t i = attr_count; i < elem_list->length; i++) {
                count += countElementsByTag(elem_list->items[i], tag_name);
            }
        } else if (item.type_id == LMD_TYPE_LIST) {
            List* list = item.list;
            for (int64_t i = 0; i < list->length; i++) {
                count += countElementsByTag(list->items[i], tag_name);
            }
        }

        return count;
    }

    // Helper: Get text content from element (concatenate all text nodes)
    std::string getTextContent(Item item) {
        std::string result;

        if (item.item == ITEM_NULL || item.item == ITEM_ERROR) {
            return result;
        }

        if (item.type_id == LMD_TYPE_STRING) {
            return std::string(item.string->chars);
        } else if (item.type_id == LMD_TYPE_ELMT) {
            Element* elem = item.element;
            TypeElmt* type = (TypeElmt*)elem->type;
            List* elem_list = (List*)elem;

            // Get content items only
            int64_t attr_count = elem_list->length - type->content_length;
            for (int64_t i = attr_count; i < elem_list->length; i++) {
                result += getTextContent(elem_list->items[i]);
            }
        } else if (item.type_id == LMD_TYPE_LIST) {
            List* list = item.list;
            for (int64_t i = 0; i < list->length; i++) {
                result += getTextContent(list->items[i]);
            }
        }

        return result;
    }

    // Helper: Get attribute value from element
    String* getAttr(Element* elem, const char* attr_name) {
        if (!elem) return nullptr;
        TypeElmt* type = (TypeElmt*)elem->type;

        for (size_t i = 0; i < type->field_count; i++) {
            if (strcmp(type->field_names[i], attr_name) == 0) {
                Item attr_val = ((List*)elem)->items[i];
                if (attr_val.type_id == LMD_TYPE_STRING) {
                    return attr_val.string;
                }
                return nullptr;
            }
        }
        return nullptr;
    }

    // Helper: Count elements with tag name
    int countElementsByTag(Item item, const char* tag_name) {
        int count = 0;
        if (item.type_id == LMD_TYPE_ELEMENT) {
            Element* elem = item.element;
            TypeElmt* type = (TypeElmt*)elem->type;
            if (strcmp(type->name, tag_name) == 0) {
                count++;
            }
            // Count in children
            for (size_t i = 0; i < ((List*)elem)->length - type->content_length; i++) {
                count += countElementsByTag(((List*)elem)->items[i + type->content_length], tag_name);
            }
        } else if (item.type_id == LMD_TYPE_LIST) {
            List* list = item.list;
            for (size_t i = 0; i < list->length; i++) {
                count += countElementsByTag(list->items[i], tag_name);
            }
        }
        return count;
    }

    // Helper: Get text content from element
    const char* getTextContent(Element* elem) {
        if (!elem) return nullptr;
        TypeElmt* type = (TypeElmt*)elem->type;

        if (type->content_length > 0) {
            Item first_child = ((List*)elem)->items[type->field_count];
            if (first_child.type_id == LMD_TYPE_STRING) {
                return first_child.string->chars;
            }
        }
        return nullptr;
    }
};

// ===== BASIC PARSING TESTS =====

TEST_F(HtmlParserTest, EmptyDocument) {
    Item root = parseHtml("");
    EXPECT_EQ(root.type_id, LMD_TYPE_NULL);
}

TEST_F(HtmlParserTest, SimpleElement) {
    Item root = parseHtml("<div></div>");
    ASSERT_EQ(root.type_id, LMD_TYPE_ELEMENT);

    Element* div = root.element;
    TypeElmt* type = (TypeElmt*)div->type;
    EXPECT_STREQ(type->name, "div");
}

TEST_F(HtmlParserTest, ElementWithText) {
    Item root = parseHtml("<p>Hello World</p>");
    ASSERT_EQ(root.type_id, LMD_TYPE_ELEMENT);

    Element* p = root.element;
    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "Hello World");
}

TEST_F(HtmlParserTest, NestedElements) {
    Item root = parseHtml("<div><p>Text</p></div>");
    ASSERT_EQ(root.type_id, LMD_TYPE_ELEMENT);

    Element* div = root.element;
    TypeElmt* type = (TypeElmt*)div->type;
    EXPECT_STREQ(type->name, "div");

    Element* p = findElementByTag(root, "p");
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(getTextContent(p), "Text");
}

TEST_F(HtmlParserTest, MultipleSiblings) {
    Item root = parseHtml("<div><p>First</p><p>Second</p><p>Third</p></div>");
    EXPECT_EQ(countElementsByTag(root, "p"), 3);
}

// ===== ATTRIBUTE TESTS =====

TEST_F(HtmlParserTest, SingleAttribute) {
    Item root = parseHtml("<div id=\"test\"></div>");
    Element* div = root.element;

    String* id = getAttr(div, "id");
    ASSERT_NE(id, nullptr);
    EXPECT_STREQ(id->chars, "test");
}

TEST_F(HtmlParserTest, MultipleAttributes) {
    Item root = parseHtml("<div id=\"main\" class=\"container\" data-value=\"123\"></div>");
    Element* div = root.element;

    String* id = getAttr(div, "id");
    String* cls = getAttr(div, "class");
    String* data = getAttr(div, "data-value");

    ASSERT_NE(id, nullptr);
    ASSERT_NE(cls, nullptr);
    ASSERT_NE(data, nullptr);

    EXPECT_STREQ(id->chars, "main");
    EXPECT_STREQ(cls->chars, "container");
    EXPECT_STREQ(data->chars, "123");
}

TEST_F(HtmlParserTest, AttributeWithSingleQuotes) {
    Item root = parseHtml("<div id='test'></div>");
    Element* div = root.element;

    String* id = getAttr(div, "id");
    ASSERT_NE(id, nullptr);
    EXPECT_STREQ(id->chars, "test");
}

TEST_F(HtmlParserTest, AttributeUnquoted) {
    Item root = parseHtml("<div id=test></div>");
    Element* div = root.element;

    String* id = getAttr(div, "id");
    ASSERT_NE(id, nullptr);
    EXPECT_STREQ(id->chars, "test");
}

TEST_F(HtmlParserTest, EmptyAttribute) {
    Item root = parseHtml("<div id=\"\"></div>");
    Element* div = root.element;

    String* id = getAttr(div, "id");
    // Empty attribute should return NULL or empty string
    EXPECT_TRUE(id == nullptr || id->len == 0);
}

TEST_F(HtmlParserTest, BooleanAttribute) {
    Item root = parseHtml("<input disabled>");
    Element* input = root.element;

    TypeElmt* type = (TypeElmt*)input->type;
    bool found_disabled = false;

    for (size_t i = 0; i < type->field_count; i++) {
        if (strcmp(type->field_names[i], "disabled") == 0) {
            Item val = ((List*)input)->items[i];
            EXPECT_EQ(val.type_id, LMD_TYPE_BOOL);
            EXPECT_TRUE(val.bool_val);
            found_disabled = true;
            break;
        }
    }

    EXPECT_TRUE(found_disabled);
}

TEST_F(HtmlParserTest, AttributeWithSpaces) {
    Item root = parseHtml("<div id = \"test\" ></div>");
    Element* div = root.element;

    String* id = getAttr(div, "id");
    ASSERT_NE(id, nullptr);
    EXPECT_STREQ(id->chars, "test");
}

TEST_F(HtmlParserTest, DataAttributes) {
    Item root = parseHtml("<div data-id=\"123\" data-name=\"test\"></div>");
    Element* div = root.element;

    String* data_id = getAttr(div, "data-id");
    String* data_name = getAttr(div, "data-name");

    ASSERT_NE(data_id, nullptr);
    ASSERT_NE(data_name, nullptr);

    EXPECT_STREQ(data_id->chars, "123");
    EXPECT_STREQ(data_name->chars, "test");
}

TEST_F(HtmlParserTest, AriaAttributes) {
    Item root = parseHtml("<button aria-label=\"Close\" aria-hidden=\"true\"></button>");
    Element* btn = root.element;

    String* label = getAttr(btn, "aria-label");
    String* hidden = getAttr(btn, "aria-hidden");

    ASSERT_NE(label, nullptr);
    ASSERT_NE(hidden, nullptr);

    EXPECT_STREQ(label->chars, "Close");
    EXPECT_STREQ(hidden->chars, "true");
}

// ===== VOID ELEMENTS TESTS =====

TEST_F(HtmlParserTest, VoidElementBr) {
    Item root = parseHtml("<div>Line 1<br>Line 2</div>");
    Element* br = findElementByTag(root, "br");
    EXPECT_NE(br, nullptr);
}

TEST_F(HtmlParserTest, VoidElementImg) {
    Item root = parseHtml("<img src=\"test.jpg\" alt=\"Test\">");
    ASSERT_EQ(root.type_id, LMD_TYPE_ELEMENT);

    Element* img = root.element;
    TypeElmt* type = (TypeElmt*)img->type;
    EXPECT_STREQ(type->name, "img");

    String* src = getAttr(img, "src");
    String* alt = getAttr(img, "alt");

    ASSERT_NE(src, nullptr);
    ASSERT_NE(alt, nullptr);
    EXPECT_STREQ(src->chars, "test.jpg");
    EXPECT_STREQ(alt->chars, "Test");
}

TEST_F(HtmlParserTest, VoidElementInput) {
    Item root = parseHtml("<input type=\"text\" name=\"username\">");
    Element* input = root.element;

    String* type = getAttr(input, "type");
    String* name = getAttr(input, "name");

    ASSERT_NE(type, nullptr);
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(type->chars, "text");
    EXPECT_STREQ(name->chars, "username");
}

TEST_F(HtmlParserTest, VoidElementMeta) {
    Item root = parseHtml("<meta charset=\"UTF-8\">");
    Element* meta = root.element;

    String* charset = getAttr(meta, "charset");
    ASSERT_NE(charset, nullptr);
    EXPECT_STREQ(charset->chars, "UTF-8");
}

TEST_F(HtmlParserTest, VoidElementLink) {
    Item root = parseHtml("<link rel=\"stylesheet\" href=\"style.css\">");
    Element* link = root.element;

    String* rel = getAttr(link, "rel");
    String* href = getAttr(link, "href");

    ASSERT_NE(rel, nullptr);
    ASSERT_NE(href, nullptr);
    EXPECT_STREQ(rel->chars, "stylesheet");
    EXPECT_STREQ(href->chars, "style.css");
}

TEST_F(HtmlParserTest, SelfClosingTag) {
    Item root = parseHtml("<div />");
    ASSERT_EQ(root.type_id, LMD_TYPE_ELEMENT);
    Element* div = root.element;
    TypeElmt* type = (TypeElmt*)div->type;
    EXPECT_STREQ(type->name, "div");
}

// ===== HTML ENTITIES TESTS =====

TEST_F(HtmlParserTest, BasicEntities) {
    Item root = parseHtml("<p>&lt;tag&gt; &amp; &quot; &apos;</p>");
    Element* p = root.element;
    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<tag> & \" '");
}

TEST_F(HtmlParserTest, NumericEntities) {
    Item root = parseHtml("<p>&#65;&#66;&#67;</p>");
    Element* p = root.element;
    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "ABC");
}

TEST_F(HtmlParserTest, HexEntities) {
    Item root = parseHtml("<p>&#x41;&#x42;&#x43;</p>");
    Element* p = root.element;
    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "ABC");
}

TEST_F(HtmlParserTest, SpecialEntities) {
    Item root = parseHtml("<p>&copy; &reg; &trade; &euro; &pound;</p>");
    Element* p = root.element;
    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "© ® ™ € £");
}

TEST_F(HtmlParserTest, MathEntities) {
    Item root = parseHtml("<p>&times; &divide; &plusmn; &frac12;</p>");
    Element* p = root.element;
    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "× ÷ ± ½");
}

TEST_F(HtmlParserTest, GreekLetters) {
    Item root = parseHtml("<p>&alpha; &beta; &gamma; &delta; &pi;</p>");
    Element* p = root.element;
    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "α β γ δ π");
}

TEST_F(HtmlParserTest, EntityInAttribute) {
    Item root = parseHtml("<a title=\"&lt;Example&gt;\">Link</a>");
    Element* a = root.element;

    String* title = getAttr(a, "title");
    ASSERT_NE(title, nullptr);
    EXPECT_STREQ(title->chars, "<Example>");
}

// ===== COMMENT TESTS =====

TEST_F(HtmlParserTest, SimpleComment) {
    Item root = parseHtml("<!-- This is a comment --><div></div>");

    // Check if root is a list containing comment and div
    if (root.type_id == LMD_TYPE_LIST) {
        List* list = root.list;
        EXPECT_GE(list->length, 1);

        // Find the comment element
        bool found_comment = false;
        for (size_t i = 0; i < list->length; i++) {
            if (list->items[i].type_id == LMD_TYPE_ELEMENT) {
                Element* elem = list->items[i].element;
                TypeElmt* type = (TypeElmt*)elem->type;
                if (strcmp(type->name, "!--") == 0) {
                    found_comment = true;
                    break;
                }
            }
        }
        EXPECT_TRUE(found_comment);
    }
}

TEST_F(HtmlParserTest, CommentWithDashes) {
    Item root = parseHtml("<!-- Comment -- with -- dashes -->");
    EXPECT_NE(root.type_id, LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, NestedComment) {
    Item root = parseHtml("<div><!-- Comment --><p>Text</p></div>");
    Element* div = findElementByTag(root, "div");
    ASSERT_NE(div, nullptr);

    Element* p = findElementByTag(root, "p");
    ASSERT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, MultilineComment) {
    Item root = parseHtml("<!-- This is\n"
                         "a multiline\n"
                         "comment --><div></div>");
    EXPECT_NE(root.type_id, LMD_TYPE_ERROR);
}

// ===== DOCTYPE TESTS =====

TEST_F(HtmlParserTest, DoctypeHTML5) {
    Item root = parseHtml("<!DOCTYPE html><html></html>");

    // Check if root is a list containing DOCTYPE and html
    if (root.type_id == LMD_TYPE_LIST) {
        List* list = root.list;
        EXPECT_GE(list->length, 1);

        // Find DOCTYPE
        bool found_doctype = false;
        for (size_t i = 0; i < list->length; i++) {
            if (list->items[i].type_id == LMD_TYPE_ELEMENT) {
                Element* elem = list->items[i].element;
                TypeElmt* type = (TypeElmt*)elem->type;
                if (strncmp(type->name, "!DOCTYPE", 8) == 0 ||
                    strncmp(type->name, "!doctype", 8) == 0) {
                    found_doctype = true;
                    break;
                }
            }
        }
        EXPECT_TRUE(found_doctype);
    }
}

TEST_F(HtmlParserTest, DoctypeUppercase) {
    Item root = parseHtml("<!DOCTYPE HTML><html></html>");
    EXPECT_NE(root.type_id, LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, DoctypeLowercase) {
    Item root = parseHtml("<!doctype html><html></html>");
    EXPECT_NE(root.type_id, LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, DoctypeWithPublicId) {
    Item root = parseHtml("<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\">"
                         "<html></html>");
    EXPECT_NE(root.type_id, LMD_TYPE_ERROR);
}

// ===== WHITESPACE HANDLING TESTS =====

TEST_F(HtmlParserTest, LeadingWhitespace) {
    Item root = parseHtml("   <div></div>");
    Element* div = findElementByTag(root, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserTest, TrailingWhitespace) {
    Item root = parseHtml("<div></div>   ");
    Element* div = findElementByTag(root, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserTest, WhitespaceBetweenElements) {
    Item root = parseHtml("<div>  <p>Text</p>  </div>");
    Element* div = findElementByTag(root, "div");
    Element* p = findElementByTag(root, "p");
    EXPECT_NE(div, nullptr);
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, NewlinesInContent) {
    Item root = parseHtml("<p>Line 1\nLine 2\nLine 3</p>");
    Element* p = root.element;
    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    EXPECT_NE(strstr(text, "\n"), nullptr);
}

TEST_F(HtmlParserTest, TabsInContent) {
    Item root = parseHtml("<p>Tab\there</p>");
    Element* p = root.element;
    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    EXPECT_NE(strstr(text, "\t"), nullptr);
}

// ===== COMPLEX STRUCTURE TESTS =====

TEST_F(HtmlParserTest, DeeplyNestedElements) {
    Item root = parseHtml("<div><div><div><div><div><p>Deep</p></div></div></div></div></div>");
    Element* p = findElementByTag(root, "p");
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(getTextContent(p), "Deep");
}

TEST_F(HtmlParserTest, ComplexHtmlDocument) {
    const char* html = "<!DOCTYPE html>"
                      "<html>"
                      "<head>"
                      "<meta charset=\"UTF-8\">"
                      "<title>Test</title>"
                      "</head>"
                      "<body>"
                      "<div id=\"main\">"
                      "<h1>Title</h1>"
                      "<p>Paragraph</p>"
                      "</div>"
                      "</body>"
                      "</html>";

    Item root = parseHtml(html);
    EXPECT_NE(root.type_id, LMD_TYPE_ERROR);

    Element* html_elem = findElementByTag(root, "html");
    Element* head = findElementByTag(root, "head");
    Element* body = findElementByTag(root, "body");
    Element* div = findElementByTag(root, "div");
    Element* h1 = findElementByTag(root, "h1");
    Element* p = findElementByTag(root, "p");

    EXPECT_NE(html_elem, nullptr);
    EXPECT_NE(head, nullptr);
    EXPECT_NE(body, nullptr);
    EXPECT_NE(div, nullptr);
    EXPECT_NE(h1, nullptr);
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, TableStructure) {
    const char* html = "<table>"
                      "<tr><td>Cell 1</td><td>Cell 2</td></tr>"
                      "<tr><td>Cell 3</td><td>Cell 4</td></tr>"
                      "</table>";

    Item root = parseHtml(html);
    EXPECT_EQ(countElementsByTag(root, "tr"), 2);
    EXPECT_EQ(countElementsByTag(root, "td"), 4);
}

TEST_F(HtmlParserTest, ListStructure) {
    const char* html = "<ul>"
                      "<li>Item 1</li>"
                      "<li>Item 2"
                      "  <ul>"
                      "    <li>Nested 1</li>"
                      "    <li>Nested 2</li>"
                      "  </ul>"
                      "</li>"
                      "<li>Item 3</li>"
                      "</ul>";

    Item root = parseHtml(html);
    EXPECT_EQ(countElementsByTag(root, "ul"), 2);
    EXPECT_EQ(countElementsByTag(root, "li"), 5);
}

TEST_F(HtmlParserTest, FormElements) {
    const char* html = "<form action=\"/submit\" method=\"post\">"
                      "<input type=\"text\" name=\"username\">"
                      "<input type=\"password\" name=\"password\">"
                      "<button type=\"submit\">Login</button>"
                      "</form>";

    Item root = parseHtml(html);
    Element* form = findElementByTag(root, "form");
    ASSERT_NE(form, nullptr);

    String* action = getAttr(form, "action");
    String* method = getAttr(form, "method");

    ASSERT_NE(action, nullptr);
    ASSERT_NE(method, nullptr);
    EXPECT_STREQ(action->chars, "/submit");
    EXPECT_STREQ(method->chars, "post");
}

// ===== HTML5 SEMANTIC ELEMENTS TESTS =====

TEST_F(HtmlParserTest, SemanticArticle) {
    Item root = parseHtml("<article><h2>Title</h2><p>Content</p></article>");
    Element* article = findElementByTag(root, "article");
    EXPECT_NE(article, nullptr);
}

TEST_F(HtmlParserTest, SemanticAside) {
    Item root = parseHtml("<aside>Sidebar content</aside>");
    Element* aside = findElementByTag(root, "aside");
    EXPECT_NE(aside, nullptr);
}

TEST_F(HtmlParserTest, SemanticNav) {
    Item root = parseHtml("<nav><a href=\"/\">Home</a><a href=\"/about\">About</a></nav>");
    Element* nav = findElementByTag(root, "nav");
    EXPECT_NE(nav, nullptr);
    EXPECT_EQ(countElementsByTag(root, "a"), 2);
}

TEST_F(HtmlParserTest, SemanticSection) {
    Item root = parseHtml("<section><h1>Section Title</h1><p>Content</p></section>");
    Element* section = findElementByTag(root, "section");
    EXPECT_NE(section, nullptr);
}

TEST_F(HtmlParserTest, SemanticHeaderFooter) {
    Item root = parseHtml("<header><h1>Site Title</h1></header>"
                         "<main><p>Content</p></main>"
                         "<footer><p>Copyright 2025</p></footer>");

    Element* header = findElementByTag(root, "header");
    Element* main = findElementByTag(root, "main");
    Element* footer = findElementByTag(root, "footer");

    EXPECT_NE(header, nullptr);
    EXPECT_NE(main, nullptr);
    EXPECT_NE(footer, nullptr);
}

TEST_F(HtmlParserTest, SemanticFigure) {
    Item root = parseHtml("<figure>"
                         "<img src=\"image.jpg\" alt=\"Test\">"
                         "<figcaption>Image caption</figcaption>"
                         "</figure>");

    Element* figure = findElementByTag(root, "figure");
    Element* img = findElementByTag(root, "img");
    Element* figcaption = findElementByTag(root, "figcaption");

    EXPECT_NE(figure, nullptr);
    EXPECT_NE(img, nullptr);
    EXPECT_NE(figcaption, nullptr);
}

TEST_F(HtmlParserTest, SemanticTime) {
    Item root = parseHtml("<time datetime=\"2025-01-01\">January 1, 2025</time>");
    Element* time = findElementByTag(root, "time");
    ASSERT_NE(time, nullptr);

    String* datetime = getAttr(time, "datetime");
    ASSERT_NE(datetime, nullptr);
    EXPECT_STREQ(datetime->chars, "2025-01-01");
}

TEST_F(HtmlParserTest, SemanticMark) {
    Item root = parseHtml("<p>This is <mark>highlighted</mark> text</p>");
    Element* mark = findElementByTag(root, "mark");
    ASSERT_NE(mark, nullptr);
    EXPECT_STREQ(getTextContent(mark), "highlighted");
}

// ===== RAW TEXT ELEMENTS TESTS =====

TEST_F(HtmlParserTest, ScriptElement) {
    Item root = parseHtml("<script>var x = 5; if (x < 10) { alert('Hello'); }</script>");
    Element* script = findElementByTag(root, "script");
    ASSERT_NE(script, nullptr);

    const char* content = getTextContent(script);
    ASSERT_NE(content, nullptr);
    EXPECT_NE(strstr(content, "var x = 5"), nullptr);
}

TEST_F(HtmlParserTest, StyleElement) {
    Item root = parseHtml("<style>body { margin: 0; } div > p { color: red; }</style>");
    Element* style = findElementByTag(root, "style");
    ASSERT_NE(style, nullptr);

    const char* content = getTextContent(style);
    ASSERT_NE(content, nullptr);
    EXPECT_NE(strstr(content, "margin: 0"), nullptr);
}

TEST_F(HtmlParserTest, TextareaElement) {
    Item root = parseHtml("<textarea>Some <b>text</b> content</textarea>");
    Element* textarea = findElementByTag(root, "textarea");
    ASSERT_NE(textarea, nullptr);

    const char* content = getTextContent(textarea);
    ASSERT_NE(content, nullptr);
    // Textarea should preserve raw content including tags
    EXPECT_NE(strstr(content, "<b>"), nullptr);
}

TEST_F(HtmlParserTest, ScriptWithClosingTag) {
    Item root = parseHtml("<script>var msg = '</script>'; alert(msg);</script>");
    Element* script = findElementByTag(root, "script");
    ASSERT_NE(script, nullptr);
}

// ===== EDGE CASES AND ERROR HANDLING =====

TEST_F(HtmlParserTest, MalformedTag) {
    Item root = parseHtml("<div<p>Test</p>");
    // Should handle malformed HTML gracefully
    EXPECT_NE(root.type_id, LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, UnclosedTag) {
    Item root = parseHtml("<div><p>Text</div>");
    // Should handle unclosed tags
    EXPECT_NE(root.type_id, LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, ExtraClosingTag) {
    Item root = parseHtml("<div></div></div>");
    Element* div = findElementByTag(root, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserTest, EmptyTagName) {
    Item root = parseHtml("<>Content</>");
    // Should handle empty tag names
    EXPECT_NE(root.type_id, LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, VeryLongAttributeValue) {
    // Create a very long attribute value
    char html[2048];
    char long_value[1000];
    memset(long_value, 'a', 999);
    long_value[999] = '\0';

    snprintf(html, sizeof(html), "<div data-long=\"%s\">Content</div>", long_value);
    Item root = parseHtml(html);

    Element* div = findElementByTag(root, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserTest, VeryLongTextContent) {
    // Create very long text content
    char html[2048];
    char long_text[1000];
    memset(long_text, 'X', 999);
    long_text[999] = '\0';

    snprintf(html, sizeof(html), "<p>%s</p>", long_text);
    Item root = parseHtml(html);

    Element* p = root.element;
    ASSERT_NE(p, nullptr);

    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(strlen(text), 999);
}

TEST_F(HtmlParserTest, ManyAttributes) {
    const char* html = "<div a1=\"1\" a2=\"2\" a3=\"3\" a4=\"4\" a5=\"5\" "
                      "a6=\"6\" a7=\"7\" a8=\"8\" a9=\"9\" a10=\"10\"></div>";

    Item root = parseHtml(html);
    Element* div = root.element;
    ASSERT_NE(div, nullptr);

    String* a1 = getAttr(div, "a1");
    String* a10 = getAttr(div, "a10");

    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a10, nullptr);
    EXPECT_STREQ(a1->chars, "1");
    EXPECT_STREQ(a10->chars, "10");
}

TEST_F(HtmlParserTest, CaseSensitivityTags) {
    Item root1 = parseHtml("<DIV></DIV>");
    Item root2 = parseHtml("<div></div>");
    Item root3 = parseHtml("<DiV></DiV>");

    // All should parse as 'div' (lowercase)
    Element* div1 = findElementByTag(root1, "div");
    Element* div2 = findElementByTag(root2, "div");
    Element* div3 = findElementByTag(root3, "div");

    EXPECT_NE(div1, nullptr);
    EXPECT_NE(div2, nullptr);
    EXPECT_NE(div3, nullptr);
}

TEST_F(HtmlParserTest, CaseSensitivityAttributes) {
    Item root = parseHtml("<div ID=\"test\" Class=\"main\"></div>");
    Element* div = root.element;

    // Attributes should be lowercase
    String* id = getAttr(div, "id");
    String* cls = getAttr(div, "class");

    ASSERT_NE(id, nullptr);
    ASSERT_NE(cls, nullptr);
}

TEST_F(HtmlParserTest, SpecialCharactersInText) {
    Item root = parseHtml("<p>Special: !@#$%^&*()_+-={}[]|\\:;\"'<>,.?/</p>");
    Element* p = root.element;
    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    // Should preserve special characters (except those that are entity-decoded)
}

TEST_F(HtmlParserTest, UnicodeCharacters) {
    Item root = parseHtml("<p>Unicode: こんにちは 你好 Привет مرحبا</p>");
    Element* p = root.element;
    const char* text = getTextContent(p);
    ASSERT_NE(text, nullptr);
    EXPECT_NE(strstr(text, "こんにちは"), nullptr);
}

TEST_F(HtmlParserTest, MultipleRootElements) {
    Item root = parseHtml("<div>First</div><div>Second</div><div>Third</div>");

    // Should return a list of elements
    if (root.type_id == LMD_TYPE_LIST) {
        List* list = root.list;
        EXPECT_GE(list->length, 3);
    } else {
        // Or find all div elements
        EXPECT_EQ(countElementsByTag(root, "div"), 3);
    }
}

TEST_F(HtmlParserTest, RootLevelTextNodes) {
    Item root = parseHtml("Text before<div>Content</div>Text after");
    EXPECT_NE(root.type_id, LMD_TYPE_ERROR);
}

// ===== REUSABILITY TESTS =====

TEST_F(HtmlParserTest, ReuseInputMultipleTimes) {
    // Parse first document
    parse_html(input, "<div>First</div>");
    Element* div1 = findElementByTag(input->root, "div");
    EXPECT_NE(div1, nullptr);

    // Parse second document (should handle cleanup properly)
    parse_html(input, "<p>Second</p>");
    Element* p = findElementByTag(input->root, "p");
    EXPECT_NE(p, nullptr);

    // Parse third document
    parse_html(input, "<span>Third</span>");
    Element* span = findElementByTag(input->root, "span");
    EXPECT_NE(span, nullptr);
}

// Main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
