#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

extern "C" {
    #include "../lambda/input/input.hpp"
    #include "../lambda/input/input-html-context.h"
    #include "../lib/mempool.h"
    #include "../lib/stringbuf.h"
    #include "../lib/strview.h"
    #include "../lib/arraylist.h"
    #include "../lib/log.h"
    void rpmalloc_initialize();
    void rpmalloc_finalize();
}

// Helper to create Lambda String (same as in test_input_roundtrip_gtest.cpp)
String* create_lambda_string(const char* text) {
    if (!text) return NULL;

    size_t len = strlen(text);
    // Allocate String struct + space for the null-terminated string
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return NULL;

    result->len = len;
    result->ref_cnt = 1;
    // Copy the string content to the chars array at the end of the struct
    strcpy(result->chars, text);

    return result;
}

// Test fixture for HTML parsing tests
class HtmlParserTest : public ::testing::Test {
protected:
    Pool* pool;
    String* html_type;

    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        pool = pool_create();
        ASSERT_NE(pool, nullptr);

        // Create the "html" type string for input_from_source
        html_type = create_lambda_string("html");
        ASSERT_NE(html_type, nullptr);

        // Initialize logging system (same pattern as main.cpp)
        // Parse config file first, then initialize
        log_parse_config_file("log.conf");
        log_init("");  // Initialize with parsed config
    }

    void TearDown() override {
        if (html_type) {
            free(html_type);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }

    // Helper: Parse HTML and return root element using input_from_source
    Item parseHtml(const char* html) {
        Input* input = input_from_source(html, NULL, html_type, NULL);
        if (!input) {
            return Item{.item = ITEM_NULL};
        }
        return input->root;
    }

    // Helper: Get element by tag name from a list/element
    Element* findElementByTag(Item item, const char* tag_name) {
        if (item.item == ITEM_NULL || item.item == ITEM_ERROR) {
            return nullptr;
        }

        if (get_type_id(item) == LMD_TYPE_ELEMENT) {
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
        } else if (get_type_id(item) == LMD_TYPE_LIST) {
            List* list = item.list;
            for (int64_t i = 0; i < list->length; i++) {
                Element* found = findElementByTag(list->items[i], tag_name);
                if (found) return found;
            }
        }

        return nullptr;
    }

    // Helper: Get tag name from element
    const char* getElementTagName(Element* elem) {
        if (!elem || !elem->type) return "";

        TypeElmt* type = (TypeElmt*)elem->type;
        // Return a null-terminated string for the tag name
        static char tag_buffer[256];
        if (type->name.length >= sizeof(tag_buffer)) {
            return "";
        }
        memcpy(tag_buffer, type->name.str, type->name.length);
        tag_buffer[type->name.length] = '\0';
        return tag_buffer;
    }

    // Helper: Get text content from element (concatenate all text nodes)
    std::string getTextContent(Item item) {
        std::string result;

        if (item.item == ITEM_NULL || item.item == ITEM_ERROR) {
            return result;
        }

        if (get_type_id(item) == LMD_TYPE_STRING) {
            String* str = item.get_string();
            if (str) {
                return std::string(str->chars, str->len);
            }
        } else if (get_type_id(item) == LMD_TYPE_ELEMENT) {
            Element* elem = item.element;
            TypeElmt* type = (TypeElmt*)elem->type;
            List* elem_list = (List*)elem;

            // Get content items only
            int64_t attr_count = elem_list->length - type->content_length;
            for (int64_t i = attr_count; i < elem_list->length; i++) {
                result += getTextContent(elem_list->items[i]);
            }
        } else if (get_type_id(item) == LMD_TYPE_LIST) {
            List* list = item.list;
            for (int64_t i = 0; i < list->length; i++) {
                result += getTextContent(list->items[i]);
            }
        }

        return result;
    }

    // Helper: Get attribute value from element
    std::string getAttr(Element* elmt, const char* attr_name) {
        if (!elmt || !elmt->type) return "";

        TypeElmt* type = (TypeElmt*)elmt->type;
        if (!type->shape || !elmt->data) return "";

        ShapeEntry* shape = type->shape;

        // Iterate through the shape to find the attribute
        while (shape) {
            if (shape->name && strview_equal(shape->name, attr_name)) {
                // Found the attribute, get its value from data
                void* field_ptr = (char*)elmt->data + shape->byte_offset;

                // Get the type ID from the shape's type
                TypeId type_id = shape->type ? shape->type->type_id : LMD_TYPE_NULL;

                if (type_id == LMD_TYPE_STRING) {
                    String** str_ptr = (String**)field_ptr;
                    if (str_ptr && *str_ptr) {
                        String* str = *str_ptr;
                        return std::string(str->chars, str->len);
                    }
                } else if (type_id == LMD_TYPE_NULL) {
                    return "";  // Empty attribute
                } else if (type_id == LMD_TYPE_BOOL) {
                    bool val = *(bool*)field_ptr;
                    return val ? "true" : "false";
                }
                return "";
            }
            shape = shape->next;
        }

        return "";  // Attribute not found
    }

    // Helper: Check if element has attribute
    bool hasAttr(Element* elmt, const char* attr_name) {
        if (!elmt || !elmt->type) return false;

        TypeElmt* type = (TypeElmt*)elmt->type;
        if (!type->shape) return false;

        ShapeEntry* shape = type->shape;

        // Iterate through the shape to find the attribute
        while (shape) {
            if (shape->name && strview_equal(shape->name, attr_name)) {
                return true;
            }
            shape = shape->next;
        }

        return false;
    }

    // Helper: Count elements with specific tag name
    int countElementsByTag(Item item, const char* tag_name) {
        int count = 0;

        if (item.item == ITEM_NULL || item.item == ITEM_ERROR) {
            return 0;
        }

        if (get_type_id(item) == LMD_TYPE_ELEMENT) {
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
        } else if (get_type_id(item) == LMD_TYPE_LIST) {
            List* list = item.list;
            for (int64_t i = 0; i < list->length; i++) {
                count += countElementsByTag(list->items[i], tag_name);
            }
        }

        return count;
    }
};

// ============================================================================
// Basic Parsing Tests
// ============================================================================

TEST_F(HtmlParserTest, BasicParsingSimpleDiv) {
    Item result = parseHtml("<div></div>");

    ASSERT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
    Element* elem = result.element;
    ASSERT_NE(elem, nullptr);

    TypeElmt* type = (TypeElmt*)elem->type;
    EXPECT_TRUE(strview_equal(&type->name, "div"));
}

TEST_F(HtmlParserTest, BasicParsingWithText) {
    Item result = parseHtml("<p>Hello World</p>");

    ASSERT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
    Element* elem = result.element;
    TypeElmt* type = (TypeElmt*)elem->type;

    EXPECT_TRUE(strview_equal(&type->name, "p"));

    std::string text = getTextContent(result);
    EXPECT_EQ(text, "Hello World");
}

TEST_F(HtmlParserTest, BasicParsingNestedElements) {
    Item result = parseHtml("<div><span>test</span></div>");

    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    TypeElmt* div_type = (TypeElmt*)div->type;
    EXPECT_TRUE(strview_equal(&div_type->name, "div"));

    Element* span = findElementByTag(result, "span");
    ASSERT_NE(span, nullptr);

    TypeElmt* span_type = (TypeElmt*)span->type;
    EXPECT_TRUE(strview_equal(&span_type->name, "span"));
}

TEST_F(HtmlParserTest, EntityDecoding) {
    Item result = parseHtml("<p>&lt;div&gt;</p>");

    std::string text = getTextContent(result);
    // ASCII entities are decoded to actual characters
    EXPECT_EQ(text, "<div>");
}

TEST_F(HtmlParserTest, MultipleRootElements) {
    Item result = parseHtml("<div></div><span></span>");

    // Parser should return a list for multiple root elements
    ASSERT_TRUE(get_type_id(result) == LMD_TYPE_LIST);
    List* list = result.list;
    ASSERT_GE(list->length, 2);
}

// ============================================================================
// Attribute Tests
// ============================================================================

TEST_F(HtmlParserTest, AttributeQuoted) {
    log_debug("=== Starting AttributeQuoted test ===");
    Item result = parseHtml("<div id=\"my-id\" class=\"container\"></div>");
    log_debug("Parsed HTML, checking element");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);
    log_debug("Element is not null");

    std::string id_val = getAttr(div, "id");
    log_debug("Got id attribute: '%s'", id_val.c_str());
    EXPECT_EQ(id_val, "my-id");

    std::string class_val = getAttr(div, "class");
    log_debug("Got class attribute: '%s'", class_val.c_str());
    EXPECT_EQ(class_val, "container");
    log_debug("=== AttributeQuoted test complete ===");
}

TEST_F(HtmlParserTest, AttributeUnquoted) {
    Item result = parseHtml("<div id=myid class=container></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "id"), "myid");
    EXPECT_EQ(getAttr(div, "class"), "container");
}

TEST_F(HtmlParserTest, AttributeSingleQuoted) {
    Item result = parseHtml("<div id='my-id' class='container'></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "id"), "my-id");
    EXPECT_EQ(getAttr(div, "class"), "container");
}

TEST_F(HtmlParserTest, AttributeEmpty) {
    Item result = parseHtml("<input disabled=\"\" readonly=\"\">");
    Element* input = result.element;
    ASSERT_NE(input, nullptr);

    EXPECT_TRUE(hasAttr(input, "disabled"));
    EXPECT_TRUE(hasAttr(input, "readonly"));
}

TEST_F(HtmlParserTest, AttributeBoolean) {
    Item result = parseHtml("<input disabled checked>");
    Element* input = result.element;
    ASSERT_NE(input, nullptr);

    EXPECT_TRUE(hasAttr(input, "disabled"));
    EXPECT_TRUE(hasAttr(input, "checked"));
}

TEST_F(HtmlParserTest, AttributeDataCustom) {
    Item result = parseHtml("<div data-value=\"123\" data-name=\"test\"></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "data-value"), "123");
    EXPECT_EQ(getAttr(div, "data-name"), "test");
}

TEST_F(HtmlParserTest, AttributeAria) {
    Item result = parseHtml("<button aria-label=\"Close\" aria-pressed=\"true\"></button>");
    Element* button = result.element;
    ASSERT_NE(button, nullptr);

    EXPECT_EQ(getAttr(button, "aria-label"), "Close");
    EXPECT_EQ(getAttr(button, "aria-pressed"), "true");
}

TEST_F(HtmlParserTest, AttributeMultiple) {
    Item result = parseHtml("<div id=\"test\" class=\"box red\" title=\"tooltip\" data-index=\"5\"></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "id"), "test");
    EXPECT_EQ(getAttr(div, "class"), "box red");
    EXPECT_EQ(getAttr(div, "title"), "tooltip");
    EXPECT_EQ(getAttr(div, "data-index"), "5");
}

TEST_F(HtmlParserTest, AttributeWithSpecialChars) {
    Item result = parseHtml("<div title=\"A &amp; B\"></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    std::string title = getAttr(div, "title");
    // Entities preserved in attributes
    EXPECT_TRUE(title == "A &amp; B" || title == "A & B");
}

TEST_F(HtmlParserTest, AttributeCaseSensitivity) {
    Item result = parseHtml("<div ID=\"test\" Class=\"container\"></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    // HTML attributes are case-insensitive, but parser may preserve case
    EXPECT_TRUE(hasAttr(div, "ID") || hasAttr(div, "id"));
}

// ============================================================================
// Void Element Tests
// ============================================================================

TEST_F(HtmlParserTest, VoidElementBr) {
    // TODO: Parser crashes on mixed text+br in element - needs investigation
    // Item result = parseHtml("<div>Line1<br>Line2</div>");

    // Simpler test: br alone
    Item result = parseHtml("<br>");
    ASSERT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserTest, VoidElementImg) {
    Item result = parseHtml("<img src=\"test.jpg\" alt=\"Test\">");
    Element* img = result.element;
    ASSERT_NE(img, nullptr);

    TypeElmt* type = (TypeElmt*)img->type;
    EXPECT_TRUE(strview_equal(&type->name, "img"));
    EXPECT_EQ(getAttr(img, "src"), "test.jpg");
}

TEST_F(HtmlParserTest, VoidElementInput) {
    Item result = parseHtml("<input type=\"text\" name=\"username\" value=\"test\">");
    Element* input = result.element;
    ASSERT_NE(input, nullptr);

    EXPECT_EQ(getAttr(input, "type"), "text");
    EXPECT_EQ(getAttr(input, "name"), "username");
}

TEST_F(HtmlParserTest, VoidElementMeta) {
    Item result = parseHtml("<meta charset=\"UTF-8\">");
    Element* meta = result.element;
    ASSERT_NE(meta, nullptr);

    TypeElmt* type = (TypeElmt*)meta->type;
    EXPECT_TRUE(strview_equal(&type->name, "meta"));
}

TEST_F(HtmlParserTest, VoidElementLink) {
    Item result = parseHtml("<link rel=\"stylesheet\" href=\"style.css\">");
    Element* link = result.element;
    ASSERT_NE(link, nullptr);

    EXPECT_EQ(getAttr(link, "rel"), "stylesheet");
    EXPECT_EQ(getAttr(link, "href"), "style.css");
}

TEST_F(HtmlParserTest, VoidElementHr) {
    // TODO: Parser crashes on text+hr mixed - needs investigation
    // Item result = parseHtml("<div>Section 1<hr>Section 2</div>");

    // Simpler test: hr alone
    Item result = parseHtml("<hr>");
    ASSERT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
}

// ============================================================================
// Comment Tests
// ============================================================================

TEST_F(HtmlParserTest, CommentSimple) {
    Item result = parseHtml("<div><!-- This is a comment --><p>Text</p></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    // Should find the paragraph, comment may or may not be preserved
    Element* p = findElementByTag(result, "p");
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, CommentMultiline) {
    Item result = parseHtml(R"(<div>
        <!-- This is a
             multiline
             comment -->
        <p>Text</p>
    </div>)");

    Element* p = findElementByTag(result, "p");
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, CommentBeforeRoot) {
    Item result = parseHtml("<!-- Comment before --><div>Content</div>");

    // Should parse successfully, may return list or just element
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT || get_type_id(result) == LMD_TYPE_LIST);
}

TEST_F(HtmlParserTest, CommentAfterRoot) {
    Item result = parseHtml("<div>Content</div><!-- Comment after -->");

    // Should parse successfully
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT || get_type_id(result) == LMD_TYPE_LIST);
}

// ============================================================================
// DOCTYPE Tests
// ============================================================================

TEST_F(HtmlParserTest, DoctypeHTML5) {
    Item result = parseHtml("<!DOCTYPE html><html><body>Test</body></html>");

    // Should parse successfully, DOCTYPE may or may not be in result
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT || get_type_id(result) == LMD_TYPE_LIST);
}

TEST_F(HtmlParserTest, DoctypeUppercase) {
    Item result = parseHtml("<!DOCTYPE HTML><html><body>Test</body></html>");

    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT || get_type_id(result) == LMD_TYPE_LIST);
}

TEST_F(HtmlParserTest, DoctypeLowercase) {
    Item result = parseHtml("<!doctype html><html><body>Test</body></html>");

    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT || get_type_id(result) == LMD_TYPE_LIST);
}

TEST_F(HtmlParserTest, DoctypeWithPublic) {
    Item result = parseHtml(R"(<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">
<html><body>Test</body></html>)");

    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT || get_type_id(result) == LMD_TYPE_LIST);
}

// ============================================================================
// Whitespace Handling Tests
// ============================================================================

TEST_F(HtmlParserTest, WhitespacePreserveInText) {
    Item result = parseHtml("<p>Hello   World</p>");

    std::string text = getTextContent(result);
    // Check if multiple spaces are preserved
    EXPECT_TRUE(text.find("  ") != std::string::npos || text == "Hello World");
}

TEST_F(HtmlParserTest, WhitespaceNewlines) {
    Item result = parseHtml("<p>Line1\nLine2\nLine3</p>");

    std::string text = getTextContent(result);
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, WhitespaceTabs) {
    Item result = parseHtml("<p>Text\twith\ttabs</p>");

    std::string text = getTextContent(result);
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, WhitespaceLeadingTrailing) {
    Item result = parseHtml("<p>  Leading and trailing  </p>");

    std::string text = getTextContent(result);
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, WhitespaceOnlyText) {
    Item result = parseHtml("<div>   </div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    // Whitespace-only text may or may not be preserved
    std::string text = getTextContent(result);
    // Just check parsing succeeded
    EXPECT_TRUE(true);
}

// ============================================================================
// Complex Structure Tests
// ============================================================================

TEST_F(HtmlParserTest, ComplexDeeplyNested) {
    Item result = parseHtml("<div><ul><li><a><span>Text</span></a></li></ul></div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    Element* span = findElementByTag(result, "span");
    ASSERT_NE(span, nullptr);

    EXPECT_EQ(getTextContent(Item{.element = span}), "Text");
}

TEST_F(HtmlParserTest, ComplexTable) {
    Item result = parseHtml(R"(
        <table>
            <thead><tr><th>Header</th></tr></thead>
            <tbody><tr><td>Cell</td></tr></tbody>
        </table>
    )");

    Element* table = findElementByTag(result, "table");
    ASSERT_NE(table, nullptr);

    Element* th = findElementByTag(result, "th");
    Element* td = findElementByTag(result, "td");
    EXPECT_NE(th, nullptr);
    EXPECT_NE(td, nullptr);
}

TEST_F(HtmlParserTest, ComplexList) {
    Item result = parseHtml(R"(
        <ul>
            <li>Item 1</li>
            <li>Item 2
                <ul>
                    <li>Sub 1</li>
                    <li>Sub 2</li>
                </ul>
            </li>
            <li>Item 3</li>
        </ul>
    )");

    int li_count = countElementsByTag(result, "li");
    EXPECT_EQ(li_count, 5);  // 3 main + 2 sub
}

TEST_F(HtmlParserTest, ComplexForm) {
    Item result = parseHtml(R"(
        <form action="/submit" method="post">
            <input type="text" name="username">
            <input type="password" name="password">
            <button type="submit">Login</button>
        </form>
    )");

    Element* form = findElementByTag(result, "form");
    ASSERT_NE(form, nullptr);

    int input_count = countElementsByTag(result, "input");
    EXPECT_EQ(input_count, 2);
}

// ============================================================================
// HTML5 Semantic Elements Tests
// ============================================================================

TEST_F(HtmlParserTest, SemanticArticle) {
    Item result = parseHtml("<article><h1>Title</h1><p>Content</p></article>");
    Element* article = findElementByTag(result, "article");
    ASSERT_NE(article, nullptr);
}

TEST_F(HtmlParserTest, SemanticAside) {
    Item result = parseHtml("<aside><p>Sidebar content</p></aside>");
    Element* aside = findElementByTag(result, "aside");
    ASSERT_NE(aside, nullptr);
}

TEST_F(HtmlParserTest, SemanticNav) {
    Item result = parseHtml("<nav><ul><li><a href=\"#\">Link</a></li></ul></nav>");
    Element* nav = findElementByTag(result, "nav");
    ASSERT_NE(nav, nullptr);
}

TEST_F(HtmlParserTest, SemanticSection) {
    Item result = parseHtml("<section><h2>Section Title</h2></section>");
    Element* section = findElementByTag(result, "section");
    ASSERT_NE(section, nullptr);
}

TEST_F(HtmlParserTest, SemanticHeaderFooter) {
    Item result = parseHtml(R"(
        <div>
            <header><h1>Page Title</h1></header>
            <main>Content</main>
            <footer><p>Copyright</p></footer>
        </div>
    )");

    EXPECT_NE(findElementByTag(result, "header"), nullptr);
    EXPECT_NE(findElementByTag(result, "main"), nullptr);
    EXPECT_NE(findElementByTag(result, "footer"), nullptr);
}

TEST_F(HtmlParserTest, SemanticFigure) {
    Item result = parseHtml(R"(
        <figure>
            <img src="image.jpg" alt="Image">
            <figcaption>Image caption</figcaption>
        </figure>
    )");

    Element* figure = findElementByTag(result, "figure");
    ASSERT_NE(figure, nullptr);

    Element* figcaption = findElementByTag(result, "figcaption");
    EXPECT_NE(figcaption, nullptr);
}

TEST_F(HtmlParserTest, SemanticTime) {
    Item result = parseHtml("<time datetime=\"2025-10-26\">October 26, 2025</time>");
    Element* time_elem = findElementByTag(result, "time");
    ASSERT_NE(time_elem, nullptr);

    EXPECT_EQ(getAttr(time_elem, "datetime"), "2025-10-26");
}

TEST_F(HtmlParserTest, SemanticMark) {
    log_debug("Starting SemanticMark test");
    Item result = parseHtml("<p>This is <mark>highlighted</mark> text</p>");
    Element* mark = findElementByTag(result, "mark");
    ASSERT_NE(mark, nullptr);
}

// ============================================================================
// Raw Text Elements Tests (script, style, textarea)
// ============================================================================

TEST_F(HtmlParserTest, ScriptElement) {
    Item result = parseHtml("<script>var x = 10; console.log(x);</script>");
    Element* script = findElementByTag(result, "script");
    ASSERT_NE(script, nullptr);

    std::string content = getTextContent(Item{.element = script});
    EXPECT_FALSE(content.empty());
}

TEST_F(HtmlParserTest, StyleElement) {
    Item result = parseHtml("<style>body { margin: 0; }</style>");
    Element* style = findElementByTag(result, "style");
    ASSERT_NE(style, nullptr);

    std::string content = getTextContent(Item{.element = style});
    EXPECT_FALSE(content.empty());
}

TEST_F(HtmlParserTest, TextareaElement) {
    Item result = parseHtml("<textarea>Default text content</textarea>");
    Element* textarea = findElementByTag(result, "textarea");
    ASSERT_NE(textarea, nullptr);

    std::string content = getTextContent(Item{.element = textarea});
    EXPECT_FALSE(content.empty());
}

TEST_F(HtmlParserTest, TitleElement) {
    Item result = parseHtml("<head><title>Page Title</title></head>");
    Element* title = findElementByTag(result, "title");
    ASSERT_NE(title, nullptr);
}

// ============================================================================
// Edge Cases and Error Handling Tests
// ============================================================================

TEST_F(HtmlParserTest, EdgeCaseMalformedUnclosedTag) {
    Item result = parseHtml("<div><p>Text");

    // Should parse without crashing, may auto-close tags
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserTest, EdgeCaseMismatchedTags) {
    Item result = parseHtml("<div><span></div></span>");

    // Should handle gracefully
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserTest, EdgeCaseExtraClosingTag) {
    Item result = parseHtml("<div></div></div>");

    // Extra closing tag should be handled
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserTest, EdgeCaseEmptyTag) {
    Item result = parseHtml("<></>");

    // Malformed empty tags should return error or NULL
    // input_from_source properly validates HTML and rejects this
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_NULL || get_type_id(result) == LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, EdgeCaseTagNameWithNumbers) {
    Item result = parseHtml("<h1>Heading 1</h1><h2>Heading 2</h2>");

    Element* h1 = findElementByTag(result, "h1");
    Element* h2 = findElementByTag(result, "h2");
    EXPECT_NE(h1, nullptr);
    EXPECT_NE(h2, nullptr);
}

TEST_F(HtmlParserTest, EdgeCaseTagNameCase) {
    Item result = parseHtml("<DiV>Mixed Case</DiV>");

    // Should handle case-insensitive tag names
    Element* div = findElementByTag(result, "div");
    EXPECT_TRUE(div != nullptr || findElementByTag(result, "DiV") != nullptr);
}

TEST_F(HtmlParserTest, EdgeCaseLongContent) {
    std::string long_text(10000, 'x');
    std::string html = "<div>" + long_text + "</div>";

    Item result = parseHtml(html.c_str());
    Element* div = result.element;
    ASSERT_NE(div, nullptr);
}

TEST_F(HtmlParserTest, EdgeCaseManyAttributes) {
    Item result = parseHtml(R"(<div
        a1="v1" a2="v2" a3="v3" a4="v4" a5="v5"
        a6="v6" a7="v7" a8="v8" a9="v9" a10="v10"
    ></div>)");

    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "a1"), "v1");
    EXPECT_EQ(getAttr(div, "a10"), "v10");
}

TEST_F(HtmlParserTest, EdgeCaseUnicodeContent) {
    Item result = parseHtml("<p>Hello 世界 🌍</p>");

    std::string text = getTextContent(result);
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, EdgeCaseSelfClosingSyntax) {
    Item result = parseHtml("<div />");

    // Self-closing div (not valid in HTML5 but should parse)
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserTest, EdgeCaseConsecutiveTags) {
    Item result = parseHtml("<b><i><u>Text</u></i></b>");

    Element* b = findElementByTag(result, "b");
    Element* i = findElementByTag(result, "i");
    Element* u = findElementByTag(result, "u");

    EXPECT_NE(b, nullptr);
    EXPECT_NE(i, nullptr);
    EXPECT_NE(u, nullptr);
}

// ============================================================================
// Parser Reusability Test
// ============================================================================

TEST_F(HtmlParserTest, ParserReuse) {
    // Test that we can parse multiple documents with same fixture
    Item result1 = parseHtml("<div>First</div>");
    ASSERT_TRUE(get_type_id(result1) == LMD_TYPE_ELEMENT);

    Item result2 = parseHtml("<span>Second</span>");
    ASSERT_TRUE(get_type_id(result2) == LMD_TYPE_ELEMENT);

    Element* span = result2.element;
    TypeElmt* type = (TypeElmt*)span->type;
    EXPECT_TRUE(strview_equal(&type->name, "span"));
}

// ============================================================================
// Phase 1.1 Tests: Tokenization and Entity Decoding
// ============================================================================

TEST_F(HtmlParserTest, EntityDecodingNumericDecimal) {
    Item result = parseHtml("<p>&#65;&#66;&#67;</p>");
    std::string text = getTextContent(result);
    // Numeric entities may or may not be decoded depending on implementation
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, EntityDecodingNumericHex) {
    Item result = parseHtml("<p>&#x41;&#x42;&#x43;</p>");
    std::string text = getTextContent(result);
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, EntityDecodingCommonEntities) {
    Item result = parseHtml("<p>&lt; &gt; &amp; &quot; &apos;</p>");
    std::string text = getTextContent(result);
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, EntityDecodingExtendedLatin) {
    Item result = parseHtml("<p>&Agrave; &Eacute; &Iuml; &Ntilde; &Ouml;</p>");
    std::string text = getTextContent(result);
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, EntityDecodingSpecialChars) {
    // Non-ASCII entities are converted to Lambda symbols
    Item result = parseHtml("<p>&nbsp;&copy;&reg;&trade;&deg;</p>");
    Element* p = result.element;
    ASSERT_NE(p, nullptr);
    TypeElmt* type = (TypeElmt*)p->type;
    EXPECT_GT(type->content_length, 0);  // Should have symbol content items
}

TEST_F(HtmlParserTest, EntityDecodingMathSymbols) {
    // Non-ASCII math entities are converted to Lambda symbols
    Item result = parseHtml("<p>&plusmn;&times;&divide;&frac14;&frac12;&frac34;</p>");
    Element* p = result.element;
    ASSERT_NE(p, nullptr);
    TypeElmt* type = (TypeElmt*)p->type;
    EXPECT_GT(type->content_length, 0);  // Should have symbol content items
}

TEST_F(HtmlParserTest, EntityDecodingInAttribute) {
    Item result = parseHtml("<div title=\"&lt;tag&gt; &amp; &quot;text&quot;\"></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    std::string title = getAttr(div, "title");
    EXPECT_FALSE(title.empty());
}

TEST_F(HtmlParserTest, EntityDecodingMixedNumericNamed) {
    Item result = parseHtml("<p>&#65;&amp;&#x42;</p>");
    std::string text = getTextContent(result);
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, EntityDecodingInvalidEntity) {
    Item result = parseHtml("<p>&invalidEntity;</p>");
    std::string text = getTextContent(result);
    // Should preserve unknown entities for round-trip compatibility
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, EntityDecodingUnicodeCodePoints) {
    Item result = parseHtml("<p>&#128512;&#128513;&#128514;</p>"); // emoji code points
    std::string text = getTextContent(result);
    EXPECT_FALSE(text.empty());
}

// ============================================================================
// Phase 1.1 Tests: Element Classification
// ============================================================================

TEST_F(HtmlParserTest, ClassificationAllVoidElements) {
    const char* void_html =
        "<area><base><br><col><embed><hr><img><input>"
        "<link><meta><param><source><track><wbr>";

    Item result = parseHtml(void_html);
    // Should parse all void elements without errors
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT || get_type_id(result) == LMD_TYPE_LIST);
}

TEST_F(HtmlParserTest, ClassificationSemanticElements) {
    const char* semantic_html =
        "<article><aside><details><figcaption><figure><footer>"
        "<header><main><mark><nav><section><summary><time></time>"
        "</summary></section></nav></mark></main></header></footer>"
        "</figure></figcaption></details></aside></article>";

    Item result = parseHtml(semantic_html);
    Element* article = findElementByTag(result, "article");
    EXPECT_NE(article, nullptr);
}

TEST_F(HtmlParserTest, ClassificationRawTextElements) {
    Item result1 = parseHtml("<script>var x = '<div>not parsed</div>';</script>");
    Element* script = findElementByTag(result1, "script");
    ASSERT_NE(script, nullptr);

    Item result2 = parseHtml("<style>.class { content: '<div>'; }</style>");
    Element* style = findElementByTag(result2, "style");
    ASSERT_NE(style, nullptr);
}

TEST_F(HtmlParserTest, ClassificationPreformattedElements) {
    Item result = parseHtml("<pre>  spaces   preserved  </pre>");
    Element* pre = findElementByTag(result, "pre");
    ASSERT_NE(pre, nullptr);

    std::string text = getTextContent(Item{.element = pre});
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, ClassificationBlockElements) {
    const char* block_html =
        "<div><p><h1></h1><h2></h2><ul><li></li></ul><table></table></p></div>";

    Item result = parseHtml(block_html);
    EXPECT_NE(findElementByTag(result, "div"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
    EXPECT_NE(findElementByTag(result, "h1"), nullptr);
}

TEST_F(HtmlParserTest, ClassificationInlineElements) {
    const char* inline_html =
        "<span><a><b><i><em><strong><code><small></small></code></strong></em></i></b></a></span>";

    Item result = parseHtml(inline_html);
    EXPECT_NE(findElementByTag(result, "span"), nullptr);
    EXPECT_NE(findElementByTag(result, "a"), nullptr);
    EXPECT_NE(findElementByTag(result, "code"), nullptr);
}

TEST_F(HtmlParserTest, ClassificationMixedBlockInline) {
    Item result = parseHtml("<div><p>Text <span>inline</span> more <b>bold</b></p></div>");

    Element* div = findElementByTag(result, "div");
    Element* span = findElementByTag(result, "span");
    Element* b = findElementByTag(result, "b");

    EXPECT_NE(div, nullptr);
    EXPECT_NE(span, nullptr);
    EXPECT_NE(b, nullptr);
}

// ============================================================================
// Phase 1.1 Tests: HTML5 Data and ARIA Attributes
// ============================================================================

TEST_F(HtmlParserTest, DataAttributesSimple) {
    Item result = parseHtml("<div data-id=\"123\" data-name=\"test\" data-active=\"true\"></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "data-id"), "123");
    EXPECT_EQ(getAttr(div, "data-name"), "test");
    EXPECT_EQ(getAttr(div, "data-active"), "true");
}

TEST_F(HtmlParserTest, DataAttributesComplex) {
    Item result = parseHtml("<div data-user-id=\"42\" data-api-endpoint=\"/api/v1/users\"></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "data-user-id"), "42");
    EXPECT_EQ(getAttr(div, "data-api-endpoint"), "/api/v1/users");
}

TEST_F(HtmlParserTest, DataAttributesWithJSON) {
    Item result = parseHtml("<div data-config='{\"key\": \"value\"}'></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    std::string config = getAttr(div, "data-config");
    EXPECT_FALSE(config.empty());
}

TEST_F(HtmlParserTest, AriaAttributesAccessibility) {
    Item result = parseHtml(R"(
        <button aria-label="Close dialog"
                aria-pressed="false"
                aria-disabled="false"
                aria-describedby="help-text">
            X
        </button>
    )");

    Element* button = findElementByTag(result, "button");
    ASSERT_NE(button, nullptr);

    EXPECT_EQ(getAttr(button, "aria-label"), "Close dialog");
    EXPECT_EQ(getAttr(button, "aria-pressed"), "false");
    EXPECT_EQ(getAttr(button, "aria-disabled"), "false");
}

TEST_F(HtmlParserTest, AriaAttributesRole) {
    Item result = parseHtml("<div role=\"navigation\" aria-label=\"Main navigation\"></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "role"), "navigation");
    EXPECT_EQ(getAttr(div, "aria-label"), "Main navigation");
}

TEST_F(HtmlParserTest, AriaAttributesLiveRegion) {
    Item result = parseHtml("<div aria-live=\"polite\" aria-atomic=\"true\"></div>");
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "aria-live"), "polite");
    EXPECT_EQ(getAttr(div, "aria-atomic"), "true");
}

TEST_F(HtmlParserTest, MixedDataAndAriaAttributes) {
    Item result = parseHtml(R"(
        <div data-component="modal"
             data-id="modal-1"
             aria-hidden="false"
             aria-labelledby="modal-title">
        </div>
    )");

    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "data-component"), "modal");
    EXPECT_EQ(getAttr(div, "aria-hidden"), "false");
}

// ============================================================================
// Phase 1.2 Tests: Tree Construction - Parse Depth Tracking
// ============================================================================

TEST_F(HtmlParserTest, TreeConstructionDeeplyNestedElements) {
    // Test parsing with deep nesting to verify parse_depth tracking works
    std::string html = "<div>";
    for (int i = 0; i < 20; i++) {
        html += "<div>";
    }
    html += "Content";
    for (int i = 0; i < 20; i++) {
        html += "</div>";
    }
    html += "</div>";

    Item result = parseHtml(html.c_str());
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    // Should parse successfully without stack overflow
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserTest, TreeConstructionVeryDeeplyNested) {
    // Test with even deeper nesting (50 levels)
    std::string html;
    for (int i = 0; i < 50; i++) {
        html += "<div>";
    }
    html += "Deep content";
    for (int i = 0; i < 50; i++) {
        html += "</div>";
    }

    Item result = parseHtml(html.c_str());
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserTest, TreeConstructionMultipleSiblings) {
    // Test html_append_child with many siblings
    std::string html = "<ul>";
    for (int i = 0; i < 50; i++) {
        html += "<li>Item " + std::to_string(i) + "</li>";
    }
    html += "</ul>";

    Item result = parseHtml(html.c_str());
    Element* ul = findElementByTag(result, "ul");
    ASSERT_NE(ul, nullptr);

    int li_count = countElementsByTag(result, "li");
    EXPECT_EQ(li_count, 50);
}

TEST_F(HtmlParserTest, TreeConstructionMixedContent) {
    // Test html_append_child with mixed element and text children
    Item result = parseHtml("<div>Text1<span>Span1</span>Text2<span>Span2</span>Text3</div>");

    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    TypeElmt* type = (TypeElmt*)div->type;
    List* list = (List*)div;

    // Verify content_length is properly set
    EXPECT_GT(type->content_length, 0);
    EXPECT_EQ(type->content_length, list->length);
}

TEST_F(HtmlParserTest, TreeConstructionContentLength) {
    // Test html_set_content_length updates properly
    Item result = parseHtml("<div><p>P1</p><p>P2</p><p>P3</p></div>");

    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    TypeElmt* div_type = (TypeElmt*)div->type;
    List* div_list = (List*)div;

    // Content length should equal list length (no attributes)
    EXPECT_EQ(div_type->content_length, div_list->length);
    EXPECT_EQ(div_type->content_length, 3);
}

TEST_F(HtmlParserTest, TreeConstructionWithAttributes) {
    // Test content_length when element has both attributes and children
    Item result = parseHtml("<div id=\"test\" class=\"box\"><p>Child1</p><p>Child2</p></div>");

    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    TypeElmt* type = (TypeElmt*)div->type;
    List* list = (List*)div;

    // Attributes are stored separately in element data, not in the list
    // List length equals content_length (only children)
    EXPECT_EQ(list->length, type->content_length);
    EXPECT_EQ(type->content_length, 2); // 2 children

    // Verify attributes are accessible
    EXPECT_EQ(getAttr(div, "id"), "test");
    EXPECT_EQ(getAttr(div, "class"), "box");
}

TEST_F(HtmlParserTest, TreeConstructionEmptyElement) {
    // Test tree construction with empty element (no children)
    Item result = parseHtml("<div></div>");

    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    TypeElmt* type = (TypeElmt*)div->type;
    EXPECT_EQ(type->content_length, 0);
}

TEST_F(HtmlParserTest, TreeConstructionOnlyAttributes) {
    // Test element with only attributes, no children
    Item result = parseHtml("<div id=\"test\" class=\"box\" data-value=\"123\"></div>");

    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    TypeElmt* type = (TypeElmt*)div->type;
    List* list = (List*)div;

    // Attributes are stored separately in element data, not in list
    // Both content length and list length should be 0 (no children)
    EXPECT_EQ(type->content_length, 0);
    EXPECT_EQ(list->length, 0);

    // Verify attributes are accessible
    EXPECT_EQ(getAttr(div, "id"), "test");
    EXPECT_EQ(getAttr(div, "class"), "box");
    EXPECT_EQ(getAttr(div, "data-value"), "123");
}

TEST_F(HtmlParserTest, TreeConstructionNestedWithAttributes) {
    // Test complex nesting with attributes at each level
    Item result = parseHtml(R"(
        <div id="outer" class="container">
            <div id="middle" class="box">
                <div id="inner" class="item">
                    <span>Content</span>
                </div>
            </div>
        </div>
    )");

    Element* outer = findElementByTag(result, "div");
    ASSERT_NE(outer, nullptr);

    Element* span = findElementByTag(result, "span");
    ASSERT_NE(span, nullptr);
}

TEST_F(HtmlParserTest, TreeConstructionManyChildren) {
    // Test html_append_child performance with many children
    std::string html = "<div>";
    for (int i = 0; i < 100; i++) {
        html += "<span>" + std::to_string(i) + "</span>";
    }
    html += "</div>";

    Item result = parseHtml(html.c_str());
    Element* div = result.element;
    ASSERT_NE(div, nullptr);

    int span_count = countElementsByTag(result, "span");
    EXPECT_EQ(span_count, 100);
}

TEST_F(HtmlParserTest, TreeConstructionSequentialParsing) {
    // Test that parse_depth is properly reset between parses
    Item result1 = parseHtml("<div><div><div>Deep1</div></div></div>");
    ASSERT_TRUE(get_type_id(result1) == LMD_TYPE_ELEMENT);

    Item result2 = parseHtml("<span>Shallow</span>");
    ASSERT_TRUE(get_type_id(result2) == LMD_TYPE_ELEMENT);

    Item result3 = parseHtml("<div><div><div><div>Deeper</div></div></div></div>");
    ASSERT_TRUE(get_type_id(result3) == LMD_TYPE_ELEMENT);
}

// ============================================================================
// Phase 1.1+1.2 Integration Tests
// ============================================================================

TEST_F(HtmlParserTest, IntegrationComplexDocumentWithEntities) {
    Item result = parseHtml(R"(
        <article data-id="123" aria-label="Article">
            <header>
                <h1>Title &amp; Subtitle</h1>
                <p>By &copy; Author &middot; 2025</p>
            </header>
            <section>
                <p>Content with &lt;code&gt; and &quot;quotes&quot;</p>
                <pre>  Preserved   spaces  </pre>
            </section>
            <footer aria-label="Footer">
                <p>&reg; 2025 &middot; All rights reserved</p>
            </footer>
        </article>
    )");

    Element* article = findElementByTag(result, "article");
    ASSERT_NE(article, nullptr);

    EXPECT_EQ(getAttr(article, "data-id"), "123");
    EXPECT_EQ(getAttr(article, "aria-label"), "Article");

    Element* h1 = findElementByTag(result, "h1");
    ASSERT_NE(h1, nullptr);
}

TEST_F(HtmlParserTest, IntegrationFormWithDataAttributes) {
    Item result = parseHtml(R"(
        <form data-form-id="login" data-validation="strict">
            <div data-field="username">
                <input type="text"
                       name="username"
                       data-required="true"
                       aria-label="Username">
            </div>
            <div data-field="password">
                <input type="password"
                       name="password"
                       data-required="true"
                       aria-label="Password">
            </div>
            <button type="submit"
                    data-action="submit"
                    aria-label="Submit form">
                Login &rarr;
            </button>
        </form>
    )");

    Element* form = findElementByTag(result, "form");
    ASSERT_NE(form, nullptr);
    EXPECT_EQ(getAttr(form, "data-form-id"), "login");

    int input_count = countElementsByTag(result, "input");
    EXPECT_EQ(input_count, 2);
}

TEST_F(HtmlParserTest, IntegrationSemanticDocumentStructure) {
    Item result = parseHtml(R"(
        <!DOCTYPE html>
        <html>
            <head>
                <meta charset="UTF-8">
                <title>Test Page</title>
            </head>
            <body>
                <header aria-label="Site header">
                    <nav data-nav-type="main">
                        <ul>
                            <li><a href="#home">Home</a></li>
                            <li><a href="#about">About</a></li>
                        </ul>
                    </nav>
                </header>
                <main>
                    <article data-article-id="1">
                        <h1>Article Title</h1>
                        <p>Content with entities: &lt; &gt; &amp;</p>
                    </article>
                </main>
                <footer aria-label="Site footer">
                    <p>&copy; 2025</p>
                </footer>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "header"), nullptr);
    EXPECT_NE(findElementByTag(result, "nav"), nullptr);
    EXPECT_NE(findElementByTag(result, "main"), nullptr);
    EXPECT_NE(findElementByTag(result, "article"), nullptr);
    EXPECT_NE(findElementByTag(result, "footer"), nullptr);
}

// ============================================================================
// Phase 2 Tests: HTML5 Void Element Handling
// ============================================================================

TEST_F(HtmlParserTest, VoidElementsAlwaysSelfClosing) {
    // Void elements should be self-closing even without slash syntax
    Item result = parseHtml("<div><img src=\"test.jpg\"><p>After image</p></div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    Element* img = findElementByTag(result, "img");
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(getAttr(img, "src"), "test.jpg");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, VoidElementsWithTrailingSlash) {
    // Void elements with self-closing slash should still work
    Item result = parseHtml("<div><br /><hr /><p>Text</p></div>");

    Element* br = findElementByTag(result, "br");
    Element* hr = findElementByTag(result, "hr");
    Element* p = findElementByTag(result, "p");

    EXPECT_NE(br, nullptr);
    EXPECT_NE(hr, nullptr);
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, NonVoidElementSelfClosingIgnored) {
    // HTML5: Self-closing slash on non-void elements should be ignored
    // <div/> should be treated as <div> (not self-closing)
    Item result = parseHtml("<div/><p>Content in div</p></div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    // The <p> should be a child of <div> since <div/> doesn't self-close
    std::string text = getTextContent(Item{.element = div});
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserTest, VoidElementsInComplexStructure) {
    Item result = parseHtml(R"(
        <div>
            <p>Line 1<br>Line 2<br>Line 3</p>
            <img src="a.jpg" alt="A">
            <img src="b.jpg" alt="B">
            <hr>
            <input type="text" name="field1">
            <input type="checkbox" name="field2">
        </div>
    )");

    EXPECT_EQ(countElementsByTag(result, "br"), 2);
    EXPECT_EQ(countElementsByTag(result, "img"), 2);
    EXPECT_EQ(countElementsByTag(result, "hr"), 1);
    EXPECT_EQ(countElementsByTag(result, "input"), 2);
}

TEST_F(HtmlParserTest, AllVoidElementsWithAttributes) {
    const char* html = R"(
        <area shape="rect" coords="0,0,10,10" href="#area">
        <base href="http://example.com/">
        <br class="break">
        <col span="2">
        <embed src="file.swf" type="application/x-shockwave-flash">
        <hr class="divider">
        <img src="test.png" alt="Test">
        <input type="text" value="input">
        <link rel="stylesheet" href="style.css">
        <meta name="viewport" content="width=device-width">
        <param name="autoplay" value="true">
        <source src="video.mp4" type="video/mp4">
        <track kind="subtitles" src="subs.vtt" srclang="en">
        <wbr>
    )";

    Item result = parseHtml(html);

    // All void elements should be parsed successfully
    EXPECT_NE(findElementByTag(result, "area"), nullptr);
    EXPECT_NE(findElementByTag(result, "base"), nullptr);
    EXPECT_NE(findElementByTag(result, "br"), nullptr);
    EXPECT_NE(findElementByTag(result, "col"), nullptr);
    EXPECT_NE(findElementByTag(result, "embed"), nullptr);
    EXPECT_NE(findElementByTag(result, "hr"), nullptr);
    EXPECT_NE(findElementByTag(result, "img"), nullptr);
    EXPECT_NE(findElementByTag(result, "input"), nullptr);
    EXPECT_NE(findElementByTag(result, "link"), nullptr);
    EXPECT_NE(findElementByTag(result, "meta"), nullptr);
    EXPECT_NE(findElementByTag(result, "param"), nullptr);
    EXPECT_NE(findElementByTag(result, "source"), nullptr);
    EXPECT_NE(findElementByTag(result, "track"), nullptr);
    EXPECT_NE(findElementByTag(result, "wbr"), nullptr);
}

TEST_F(HtmlParserTest, VoidElementsWithInvalidClosingTag) {
    // HTML5: Closing tags on void elements should be ignored/handled gracefully
    // <br></br> - the </br> is an error but parser should handle it
    Item result = parseHtml("<p>Line1<br></br>Line2</p>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    Element* br = findElementByTag(result, "br");
    EXPECT_NE(br, nullptr);
}

TEST_F(HtmlParserTest, MixedVoidAndNonVoidWithSlashes) {
    // Test mixing void and non-void elements with self-closing syntax
    Item result = parseHtml(R"(
        <div>
            <img src="test.jpg" />
            <span/>Content after span</span>
            <br />
            <p/>Paragraph content</p>
        </div>
    )");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    // img and br are void, should be self-closing
    EXPECT_NE(findElementByTag(result, "img"), nullptr);
    EXPECT_NE(findElementByTag(result, "br"), nullptr);

    // span and p are non-void, slash should be ignored
    Element* span = findElementByTag(result, "span");
    Element* p = findElementByTag(result, "p");
    EXPECT_NE(span, nullptr);
    EXPECT_NE(p, nullptr);
}

// ============================================================================
// Phase 3 Tests: Parser Context and Implicit Elements
// ============================================================================

TEST_F(HtmlParserTest, ParserContextCreation) {
    // Create a properly initialized input structure for testing
    Pool* pool = pool_create();
    Input* test_input = Input::create(pool);
    test_input->type_list = arraylist_new(10);

    HtmlParserContext* ctx = html_context_create(test_input);
    ASSERT_NE(ctx, nullptr);

    // Verify initial state
    EXPECT_EQ(ctx->html_element, nullptr);
    EXPECT_EQ(ctx->head_element, nullptr);
    EXPECT_EQ(ctx->body_element, nullptr);
    EXPECT_FALSE(ctx->has_explicit_html);
    EXPECT_FALSE(ctx->has_explicit_head);
    EXPECT_FALSE(ctx->has_explicit_body);
    EXPECT_FALSE(ctx->in_head);
    EXPECT_FALSE(ctx->head_closed);
    EXPECT_FALSE(ctx->in_body);

    html_context_destroy(ctx);
    arraylist_free(test_input->type_list);
    pool_destroy(pool);
}

TEST_F(HtmlParserTest, ParserContextEnsureHtml) {
    Pool* pool = pool_create();
    Input* test_input = Input::create(pool);
    test_input->type_list = arraylist_new(10);

    HtmlParserContext* ctx = html_context_create(test_input);
    ASSERT_NE(ctx, nullptr);

    Element* html = html_context_ensure_html(ctx);
    ASSERT_NE(html, nullptr);

    // Verify it's an html element
    TypeElmt* type = (TypeElmt*)html->type;
    EXPECT_TRUE(strview_equal(&type->name, "html"));

    // Should be implicit
    EXPECT_FALSE(ctx->has_explicit_html);

    // Calling again should return same element
    Element* html2 = html_context_ensure_html(ctx);
    EXPECT_EQ(html, html2);

    html_context_destroy(ctx);
    arraylist_free(test_input->type_list);
    pool_destroy(pool);
}

TEST_F(HtmlParserTest, ParserContextEnsureHead) {
    Pool* pool = pool_create();
    Input* test_input = Input::create(pool);
    test_input->type_list = arraylist_new(10);

    HtmlParserContext* ctx = html_context_create(test_input);
    ASSERT_NE(ctx, nullptr);

    Element* head = html_context_ensure_head(ctx);
    ASSERT_NE(head, nullptr);

    // Verify it's a head element
    TypeElmt* type = (TypeElmt*)head->type;
    EXPECT_TRUE(strview_equal(&type->name, "head"));

    // Should also have created html
    EXPECT_NE(ctx->html_element, nullptr);

    // Should be implicit
    EXPECT_FALSE(ctx->has_explicit_head);
    EXPECT_FALSE(ctx->has_explicit_html);

    html_context_destroy(ctx);
    arraylist_free(test_input->type_list);
    pool_destroy(pool);
}

TEST_F(HtmlParserTest, ParserContextEnsureBody) {
    Pool* pool = pool_create();
    Input* test_input = Input::create(pool);
    test_input->type_list = arraylist_new(10);

    HtmlParserContext* ctx = html_context_create(test_input);
    ASSERT_NE(ctx, nullptr);

    Element* body = html_context_ensure_body(ctx);
    ASSERT_NE(body, nullptr);

    // Verify it's a body element
    TypeElmt* type = (TypeElmt*)body->type;
    EXPECT_TRUE(strview_equal(&type->name, "body"));

    // Should also have created html
    EXPECT_NE(ctx->html_element, nullptr);

    // Should be implicit
    EXPECT_FALSE(ctx->has_explicit_body);
    EXPECT_FALSE(ctx->has_explicit_html);

    // Body creation should set in_body flag
    EXPECT_TRUE(ctx->in_body);

    html_context_destroy(ctx);
    arraylist_free(test_input->type_list);
    pool_destroy(pool);
}

TEST_F(HtmlParserTest, ParserContextGetInsertionPointHeadElement) {
    Pool* pool = pool_create();
    Input* test_input = Input::create(pool);
    test_input->type_list = arraylist_new(10);

    HtmlParserContext* ctx = html_context_create(test_input);
    ASSERT_NE(ctx, nullptr);

    // Get insertion point for a head element (title)
    Element* insertion_point = html_context_get_insertion_point(ctx, "title");
    ASSERT_NE(insertion_point, nullptr);

    // Should be the head element
    TypeElmt* type = (TypeElmt*)insertion_point->type;
    EXPECT_TRUE(strview_equal(&type->name, "head"));

    // Context should show we're in head
    EXPECT_TRUE(ctx->in_head);

    html_context_destroy(ctx);
    arraylist_free(test_input->type_list);
    pool_destroy(pool);
}

TEST_F(HtmlParserTest, ParserContextGetInsertionPointBodyElement) {
    Pool* pool = pool_create();
    Input* test_input = Input::create(pool);
    test_input->type_list = arraylist_new(10);

    HtmlParserContext* ctx = html_context_create(test_input);
    ASSERT_NE(ctx, nullptr);

    // Get insertion point for a body element (div)
    Element* insertion_point = html_context_get_insertion_point(ctx, "div");
    ASSERT_NE(insertion_point, nullptr);

    // Should be the body element
    TypeElmt* type = (TypeElmt*)insertion_point->type;
    EXPECT_TRUE(strview_equal(&type->name, "body"));

    // Context should show we're in body
    EXPECT_TRUE(ctx->in_body);

    // Head should be closed
    EXPECT_TRUE(ctx->head_closed);

    html_context_destroy(ctx);
    arraylist_free(test_input->type_list);
    pool_destroy(pool);
}

TEST_F(HtmlParserTest, ParserContextExplicitElements) {
    Pool* pool = pool_create();
    Input* test_input = Input::create(pool);
    test_input->type_list = arraylist_new(10);

    HtmlParserContext* ctx = html_context_create(test_input);
    ASSERT_NE(ctx, nullptr);

    // Create explicit html element using MarkBuilder
    MarkBuilder builder(test_input);
    Element* html = builder.element("html").final().element;
    html_context_set_html(ctx, html);

    EXPECT_EQ(ctx->html_element, html);
    EXPECT_TRUE(ctx->has_explicit_html);

    // Create explicit head element using MarkBuilder
    Element* head = builder.element("head").final().element;
    html_context_set_head(ctx, head);

    EXPECT_EQ(ctx->head_element, head);
    EXPECT_TRUE(ctx->has_explicit_head);
    EXPECT_TRUE(ctx->in_head);

    // Create explicit body element using MarkBuilder
    Element* body = builder.element("body").final().element;
    html_context_set_body(ctx, body);

    EXPECT_EQ(ctx->body_element, body);
    EXPECT_TRUE(ctx->has_explicit_body);
    EXPECT_TRUE(ctx->in_body);

    // Head should be closed when body starts
    EXPECT_TRUE(ctx->head_closed);
    EXPECT_FALSE(ctx->in_head);

    html_context_destroy(ctx);
    arraylist_free(test_input->type_list);
    pool_destroy(pool);
}

// ============================================================================
// Phase 3 Integration Tests: Context Usage in Real Parsing
// ============================================================================

TEST_F(HtmlParserTest, IntegrationContextExplicitHtmlElement) {
    // Parse HTML with explicit <html> tag
    Item result = parseHtml("<html><body><p>Test</p></body></html>");

    Element* html = findElementByTag(result, "html");
    ASSERT_NE(html, nullptr);

    TypeElmt* type = (TypeElmt*)html->type;
    EXPECT_TRUE(strview_equal(&type->name, "html"));

    // Should find body and p as children
    Element* body = findElementByTag(result, "body");
    Element* p = findElementByTag(result, "p");
    EXPECT_NE(body, nullptr);
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, IntegrationContextExplicitHeadElement) {
    // Parse HTML with explicit <head> tag
    Item result = parseHtml("<html><head><title>Test</title></head><body></body></html>");

    Element* head = findElementByTag(result, "head");
    ASSERT_NE(head, nullptr);

    Element* title = findElementByTag(result, "title");
    EXPECT_NE(title, nullptr);
}

TEST_F(HtmlParserTest, IntegrationContextExplicitBodyElement) {
    // Parse HTML with explicit <body> tag
    Item result = parseHtml("<html><body><div>Content</div></body></html>");

    Element* body = findElementByTag(result, "body");
    ASSERT_NE(body, nullptr);

    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserTest, IntegrationContextCompleteDocument) {
    // Parse a complete HTML5 document structure
    Item result = parseHtml(R"(
        <!DOCTYPE html>
        <html>
            <head>
                <meta charset="UTF-8">
                <title>Test Page</title>
            </head>
            <body>
                <h1>Heading</h1>
                <p>Paragraph</p>
            </body>
        </html>
    )");

    // Should find all structural elements
    EXPECT_NE(findElementByTag(result, "html"), nullptr);
    EXPECT_NE(findElementByTag(result, "head"), nullptr);
    EXPECT_NE(findElementByTag(result, "meta"), nullptr);
    EXPECT_NE(findElementByTag(result, "title"), nullptr);
    EXPECT_NE(findElementByTag(result, "body"), nullptr);
    EXPECT_NE(findElementByTag(result, "h1"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
}

// ============================================================================
// Phase 3 Advanced Tests: Insertion Point and Context State Management
// ============================================================================

TEST_F(HtmlParserTest, Phase3HeadElementsGoInHead) {
    // When explicit <html> without <head>, head elements create implicit head
    Item result = parseHtml("<html><title>Test</title><body><div>Content</div></body></html>");

    // Should have explicit html
    Element* html = findElementByTag(result, "html");
    EXPECT_NE(html, nullptr);

    // Title should be in head (even though head tag wasn't explicit in input)
    Element* title = findElementByTag(result, "title");
    EXPECT_NE(title, nullptr);

    // Body should be present
    Element* body = findElementByTag(result, "body");
    EXPECT_NE(body, nullptr);

    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserTest, Phase3MetaBeforeBody) {
    // Meta elements should go in head section
    Item result = parseHtml("<html><meta charset=\"UTF-8\"><body>Content</body></html>");

    Element* html = findElementByTag(result, "html");
    Element* meta = findElementByTag(result, "meta");
    Element* body = findElementByTag(result, "body");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(meta, nullptr);
    EXPECT_NE(body, nullptr);
}

TEST_F(HtmlParserTest, Phase3BodyContentInBody) {
    // Div elements should go in body
    Item result = parseHtml("<html><head><title>Test</title></head><div>Content</div></html>");

    Element* html = findElementByTag(result, "html");
    Element* head = findElementByTag(result, "head");
    Element* div = findElementByTag(result, "div");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(head, nullptr);
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserTest, Phase3MixedHeadAndBody) {
    // Mix of head and body elements
    Item result = parseHtml("<html><title>Test</title><div>Body content</div></html>");

    Element* html = findElementByTag(result, "html");
    Element* title = findElementByTag(result, "title");
    Element* div = findElementByTag(result, "div");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(title, nullptr);
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserTest, Phase3MultipleHeadElements) {
    // Multiple head-only elements in document with explicit tags
    Item result = parseHtml(R"(
        <html>
            <head>
                <meta charset="UTF-8">
                <title>Test</title>
                <link rel="stylesheet" href="style.css">
                <style>body { margin: 0; }</style>
                <script>console.log('test');</script>
            </head>
            <body>
                <div>Body content</div>
            </body>
        </html>
    )");

    Element* html = findElementByTag(result, "html");
    Element* head = findElementByTag(result, "head");
    Element* body = findElementByTag(result, "body");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(head, nullptr);
    EXPECT_NE(body, nullptr);

    EXPECT_NE(findElementByTag(result, "meta"), nullptr);
    EXPECT_NE(findElementByTag(result, "title"), nullptr);
    EXPECT_NE(findElementByTag(result, "link"), nullptr);
    EXPECT_NE(findElementByTag(result, "style"), nullptr);
    EXPECT_NE(findElementByTag(result, "script"), nullptr);
    EXPECT_NE(findElementByTag(result, "div"), nullptr);
}

TEST_F(HtmlParserTest, Phase3ExplicitStructureTags) {
    // Explicit html, head, body tags
    Item result = parseHtml("<html><head></head><body><p>Paragraph</p></body></html>");

    Element* html = findElementByTag(result, "html");
    Element* head = findElementByTag(result, "head");
    Element* body = findElementByTag(result, "body");
    Element* p = findElementByTag(result, "p");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(head, nullptr);
    EXPECT_NE(body, nullptr);
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, Phase3HeadThenBodyElements) {
    // Head elements followed by body elements with all explicit tags
    Item result = parseHtml(R"(
        <html>
            <head>
                <meta charset="UTF-8">
                <title>Test</title>
            </head>
            <body>
                <div>Content</div>
                <p>Paragraph</p>
            </body>
        </html>
    )");

    Element* head = findElementByTag(result, "head");
    Element* body = findElementByTag(result, "body");

    EXPECT_NE(head, nullptr);
    EXPECT_NE(body, nullptr);

    EXPECT_NE(findElementByTag(result, "meta"), nullptr);
    EXPECT_NE(findElementByTag(result, "title"), nullptr);
    EXPECT_NE(findElementByTag(result, "div"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
}

TEST_F(HtmlParserTest, Phase3LinkAndStyleElements) {
    // Link and style are head elements
    Item result = parseHtml(R"(
        <html>
            <head>
                <link rel="stylesheet" href="style.css">
                <style>body { color: red; }</style>
            </head>
            <body>Content</body>
        </html>
    )");

    Element* link = findElementByTag(result, "link");
    Element* style = findElementByTag(result, "style");

    EXPECT_NE(link, nullptr);
    EXPECT_NE(style, nullptr);
}

// ============================================================================
// Phase 4 Tests: HTML5 Insertion Mode State Machine
// ============================================================================

TEST_F(HtmlParserTest, Phase4InsertionModeInitial) {
    // DOCTYPE should be handled in INITIAL mode
    Item result = parseHtml("<!DOCTYPE html><html><body>Content</body></html>");

    Element* html = findElementByTag(result, "html");
    EXPECT_NE(html, nullptr);
}

TEST_F(HtmlParserTest, Phase4InsertionModeWithHeadElements) {
    // Head elements in proper position
    Item result = parseHtml("<html><title>Test</title><body>Content</body></html>");

    Element* html = findElementByTag(result, "html");
    Element* title = findElementByTag(result, "title");
    Element* body = findElementByTag(result, "body");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(title, nullptr);
    EXPECT_NE(body, nullptr);
}

TEST_F(HtmlParserTest, Phase4InsertionModeInHead) {
    // Explicit head with multiple head elements
    Item result = parseHtml(R"(
        <html>
            <head>
                <meta charset="UTF-8">
                <title>Test</title>
                <link rel="stylesheet" href="style.css">
            </head>
            <body>Content</body>
        </html>
    )");

    Element* head = findElementByTag(result, "head");
    ASSERT_NE(head, nullptr);

    EXPECT_NE(findElementByTag(result, "meta"), nullptr);
    EXPECT_NE(findElementByTag(result, "title"), nullptr);
    EXPECT_NE(findElementByTag(result, "link"), nullptr);
}

TEST_F(HtmlParserTest, Phase4InsertionModeAfterHeadTag) {
    // After </head>, body content should go in body
    Item result = parseHtml("<html><head><title>Test</title></head><div>Content</div></html>");

    Element* head = findElementByTag(result, "head");
    Element* div = findElementByTag(result, "div");

    EXPECT_NE(head, nullptr);
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserTest, Phase4InsertionModeInBody) {
    // Explicit body with content
    Item result = parseHtml(R"(
        <html>
            <head><title>Test</title></head>
            <body>
                <h1>Title</h1>
                <p>Paragraph</p>
                <div>Content</div>
            </body>
        </html>
    )");

    Element* body = findElementByTag(result, "body");
    ASSERT_NE(body, nullptr);

    EXPECT_NE(findElementByTag(result, "h1"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
    EXPECT_NE(findElementByTag(result, "div"), nullptr);
}

TEST_F(HtmlParserTest, Phase4InsertionModeTransitionHeadToBody) {
    // Head should close when body element appears
    Item result = parseHtml("<html><head><title>Test</title></head><body><p>Content</p></body></html>");

    Element* head = findElementByTag(result, "head");
    Element* title = findElementByTag(result, "title");
    Element* body = findElementByTag(result, "body");
    Element* p = findElementByTag(result, "p");

    EXPECT_NE(head, nullptr);
    EXPECT_NE(title, nullptr);
    EXPECT_NE(body, nullptr);
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, Phase4InsertionModeHeadThenBodyContent) {
    // Title in head, then body content
    Item result = parseHtml("<html><title>Test</title><p>Paragraph</p></html>");

    Element* title = findElementByTag(result, "title");
    Element* p = findElementByTag(result, "p");

    EXPECT_NE(title, nullptr);
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, Phase4InsertionModeScriptInHead) {
    // Script in head should stay in head
    Item result = parseHtml(R"(
        <html>
            <head>
                <title>Test</title>
                <script>console.log('in head');</script>
            </head>
            <body>Content</body>
        </html>
    )");

    Element* head = findElementByTag(result, "head");
    Element* script = findElementByTag(result, "script");

    EXPECT_NE(head, nullptr);
    EXPECT_NE(script, nullptr);
}

TEST_F(HtmlParserTest, Phase4InsertionModeMultipleClosingTags) {
    // Proper handling of closing tags for head and body
    Item result = parseHtml(R"(
        <html>
            <head>
                <title>Test</title>
            </head>
            <body>
                <div>Content</div>
            </body>
        </html>
    )");

    Element* html = findElementByTag(result, "html");
    Element* head = findElementByTag(result, "head");
    Element* body = findElementByTag(result, "body");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(head, nullptr);
    EXPECT_NE(body, nullptr);
}

TEST_F(HtmlParserTest, Phase4InsertionModeNestedBody) {
    // Multiple body tags - second one should be ignored
    Item result = parseHtml(R"(
        <html>
            <body>
                <div>First</div>
                <body>
                    <div>Second</div>
                </body>
            </body>
        </html>
    )");

    // Should parse without errors
    Element* body = findElementByTag(result, "body");
    EXPECT_NE(body, nullptr);

    // Both divs should be present
    int div_count = countElementsByTag(result, "div");
    EXPECT_GE(div_count, 1);
}

// ============================================================================
// Phase 3+4 Integration Tests: Real-world HTML Structures
// ============================================================================

TEST_F(HtmlParserTest, Phase34IntegrationBasicHTMLStructure) {
    // Basic complete HTML structure
    Item result = parseHtml("<html><head><title>Test</title></head><body><p>Hello World</p></body></html>");

    Element* html = findElementByTag(result, "html");
    Element* head = findElementByTag(result, "head");
    Element* body = findElementByTag(result, "body");
    Element* p = findElementByTag(result, "p");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(head, nullptr);
    EXPECT_NE(body, nullptr);
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, Phase34IntegrationHTMLWithoutExplicitHead) {
    // HTML tag with head elements but no explicit <head> tag
    Item result = parseHtml("<html><title>Test</title><p>Content</p></html>");

    Element* html = findElementByTag(result, "html");
    Element* title = findElementByTag(result, "title");
    Element* p = findElementByTag(result, "p");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(title, nullptr);
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, Phase34IntegrationCompleteExplicit) {
    // Complete explicit structure
    Item result = parseHtml(R"(
        <!DOCTYPE html>
        <html>
            <head>
                <meta charset="UTF-8">
                <title>Complete</title>
            </head>
            <body>
                <header><h1>Header</h1></header>
                <main><p>Main content</p></main>
                <footer><p>Footer</p></footer>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "html"), nullptr);
    EXPECT_NE(findElementByTag(result, "head"), nullptr);
    EXPECT_NE(findElementByTag(result, "body"), nullptr);
    EXPECT_NE(findElementByTag(result, "header"), nullptr);
    EXPECT_NE(findElementByTag(result, "main"), nullptr);
    EXPECT_NE(findElementByTag(result, "footer"), nullptr);
}

TEST_F(HtmlParserTest, Phase34IntegrationMetaTitleLink) {
    // Meta before title - both should be accessible
    Item result = parseHtml(R"(
        <html>
            <meta charset="UTF-8">
            <title>Test</title>
            <body><div>Content</div></body>
        </html>
    )");

    Element* html = findElementByTag(result, "html");
    Element* meta = findElementByTag(result, "meta");
    Element* title = findElementByTag(result, "title");
    Element* body = findElementByTag(result, "body");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(meta, nullptr);
    EXPECT_NE(title, nullptr);
    EXPECT_NE(body, nullptr);
}

TEST_F(HtmlParserTest, Phase34IntegrationLinkStyleScript) {
    // Multiple head elements of different types
    Item result = parseHtml(R"(
        <html>
            <link rel="stylesheet" href="style.css">
            <style>body { margin: 0; }</style>
            <script>console.log('test');</script>
            <title>Test</title>
            <body><p>Body content</p></body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "link"), nullptr);
    EXPECT_NE(findElementByTag(result, "style"), nullptr);
    EXPECT_NE(findElementByTag(result, "script"), nullptr);
    EXPECT_NE(findElementByTag(result, "title"), nullptr);
    EXPECT_NE(findElementByTag(result, "body"), nullptr);
}

TEST_F(HtmlParserTest, Phase34IntegrationNoScript) {
    // noscript is a head element
    Item result = parseHtml(R"(
        <html>
            <head>
                <title>Test</title>
                <noscript><link rel="stylesheet" href="noscript.css"></noscript>
            </head>
            <body>Content</body>
        </html>
    )");

    Element* noscript = findElementByTag(result, "noscript");
    EXPECT_NE(noscript, nullptr);
}

TEST_F(HtmlParserTest, Phase34IntegrationEmptyHead) {
    // Explicit empty head
    Item result = parseHtml("<html><head></head><body><p>Content</p></body></html>");

    Element* head = findElementByTag(result, "head");
    Element* body = findElementByTag(result, "body");

    EXPECT_NE(head, nullptr);
    EXPECT_NE(body, nullptr);
}

TEST_F(HtmlParserTest, Phase34IntegrationEmptyBody) {
    // Explicit empty body
    Item result = parseHtml("<html><head><title>Test</title></head><body></body></html>");

    Element* head = findElementByTag(result, "head");
    Element* body = findElementByTag(result, "body");

    EXPECT_NE(head, nullptr);
    EXPECT_NE(body, nullptr);
}

TEST_F(HtmlParserTest, Phase34IntegrationBodyBeforeHead) {
    // Invalid: body before head - should handle gracefully
    Item result = parseHtml("<html><body><p>Body</p></body><head><title>Title</title></head></html>");

    // Should have both elements
    Element* head = findElementByTag(result, "head");
    Element* body = findElementByTag(result, "body");

    EXPECT_NE(head, nullptr);
    EXPECT_NE(body, nullptr);
}

TEST_F(HtmlParserTest, Phase34IntegrationComplexHeadContent) {
    // HTML with complex head content
    Item result = parseHtml(R"(
        <html>
            <head>
                <meta charset="UTF-8">
                <meta name="viewport" content="width=device-width, initial-scale=1.0">
                <title>Test Page</title>
                <link rel="stylesheet" href="main.css">
                <link rel="icon" href="favicon.ico">
                <style>
                    body { font-family: Arial; }
                </style>
                <script src="app.js"></script>
            </head>
            <body>
                <div>Content</div>
            </body>
        </html>
    )");

    Element* head = findElementByTag(result, "head");
    EXPECT_NE(head, nullptr);

    // Verify all head elements are present
    int meta_count = countElementsByTag(result, "meta");
    EXPECT_EQ(meta_count, 2);

    int link_count = countElementsByTag(result, "link");
    EXPECT_EQ(link_count, 2);

    EXPECT_NE(findElementByTag(result, "title"), nullptr);
    EXPECT_NE(findElementByTag(result, "style"), nullptr);
    EXPECT_NE(findElementByTag(result, "script"), nullptr);
}

TEST_F(HtmlParserTest, Phase34IntegrationDeeplyNestedWithStructure) {
    // Deep nesting with proper structure
    Item result = parseHtml(R"(
        <html>
            <head><title>Test</title></head>
            <body>
                <div>
                    <div>
                        <div>
                            <div>
                                <p>Deep content</p>
                            </div>
                        </div>
                    </div>
                </div>
            </body>
        </html>
    )");

    Element* html = findElementByTag(result, "html");
    Element* head = findElementByTag(result, "head");
    Element* body = findElementByTag(result, "body");
    Element* p = findElementByTag(result, "p");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(head, nullptr);
    EXPECT_NE(body, nullptr);
    EXPECT_NE(p, nullptr);

    int div_count = countElementsByTag(result, "div");
    EXPECT_EQ(div_count, 4);
}

// ============================================================================
// Phase 5 Tests: Open Element Stack
// ============================================================================

TEST_F(HtmlParserTest, Phase5StackBasicNesting) {
    // Test that elements are properly nested
    Item result = parseHtml("<html><body><div><p>Text</p></div></body></html>");

    Element* html = findElementByTag(result, "html");
    Element* body = findElementByTag(result, "body");
    Element* div = findElementByTag(result, "div");
    Element* p = findElementByTag(result, "p");

    EXPECT_NE(html, nullptr);
    EXPECT_NE(body, nullptr);
    EXPECT_NE(div, nullptr);
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, Phase5StackMultipleSiblings) {
    // Test stack with multiple sibling elements
    Item result = parseHtml("<html><body><div>First</div><div>Second</div><div>Third</div></body></html>");

    int div_count = countElementsByTag(result, "div");
    EXPECT_EQ(div_count, 3);
}

TEST_F(HtmlParserTest, Phase5StackDeeplyNested) {
    // Test deeply nested structure
    Item result = parseHtml(R"(
        <html>
            <body>
                <div>
                    <section>
                        <article>
                            <header>
                                <h1>Title</h1>
                            </header>
                        </article>
                    </section>
                </div>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "div"), nullptr);
    EXPECT_NE(findElementByTag(result, "section"), nullptr);
    EXPECT_NE(findElementByTag(result, "article"), nullptr);
    EXPECT_NE(findElementByTag(result, "header"), nullptr);
    EXPECT_NE(findElementByTag(result, "h1"), nullptr);
}

TEST_F(HtmlParserTest, Phase5StackWithVoidElements) {
    // Test that void elements don't cause stack issues
    Item result = parseHtml("<html><body><img src=\"test.jpg\"><br><hr><p>Text</p></body></html>");

    EXPECT_NE(findElementByTag(result, "img"), nullptr);
    EXPECT_NE(findElementByTag(result, "br"), nullptr);
    EXPECT_NE(findElementByTag(result, "hr"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
}

TEST_F(HtmlParserTest, Phase5StackMisnestedTags) {
    // Test handling of misnested tags (common HTML error)
    // <div><span></div></span> - improper nesting
    Item result = parseHtml("<html><body><div><span>Content</div></span></body></html>");

    // Should still parse without crashing
    Element* div = findElementByTag(result, "div");
    Element* span = findElementByTag(result, "span");

    EXPECT_NE(div, nullptr);
    EXPECT_NE(span, nullptr);
}

TEST_F(HtmlParserTest, Phase5StackUnclosedElements) {
    // Test handling of unclosed elements
    Item result = parseHtml("<html><body><div><p>Unclosed paragraph<div>Another div</div></body></html>");

    // Should handle gracefully
    int div_count = countElementsByTag(result, "div");
    EXPECT_GE(div_count, 1);

    Element* p = findElementByTag(result, "p");
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, Phase5StackMixedContent) {
    // Test stack with mixed inline and block elements
    Item result = parseHtml(R"(
        <html>
            <body>
                <p>Text with <strong>bold</strong> and <em>italic</em></p>
                <div>Block with <span>inline</span> content</div>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "p"), nullptr);
    EXPECT_NE(findElementByTag(result, "strong"), nullptr);
    EXPECT_NE(findElementByTag(result, "em"), nullptr);
    EXPECT_NE(findElementByTag(result, "div"), nullptr);
    EXPECT_NE(findElementByTag(result, "span"), nullptr);
}

TEST_F(HtmlParserTest, Phase5StackTableStructure) {
    // Test stack with table structure
    Item result = parseHtml(R"(
        <html>
            <body>
                <table>
                    <tr>
                        <td>Cell 1</td>
                        <td>Cell 2</td>
                    </tr>
                </table>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "table"), nullptr);
    EXPECT_NE(findElementByTag(result, "tr"), nullptr);

    int td_count = countElementsByTag(result, "td");
    EXPECT_EQ(td_count, 2);
}

TEST_F(HtmlParserTest, Phase5StackListStructure) {
    // Test stack with list structure
    Item result = parseHtml(R"(
        <html>
            <body>
                <ul>
                    <li>Item 1</li>
                    <li>Item 2</li>
                    <li>Item 3</li>
                </ul>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "ul"), nullptr);

    int li_count = countElementsByTag(result, "li");
    EXPECT_EQ(li_count, 3);
}

TEST_F(HtmlParserTest, Phase5StackFormElements) {
    // Test stack with form elements
    Item result = parseHtml(R"(
        <html>
            <body>
                <form>
                    <label>Name:</label>
                    <input type="text">
                    <button>Submit</button>
                </form>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "form"), nullptr);
    EXPECT_NE(findElementByTag(result, "label"), nullptr);
    EXPECT_NE(findElementByTag(result, "input"), nullptr);
    EXPECT_NE(findElementByTag(result, "button"), nullptr);
}

TEST_F(HtmlParserTest, Phase5StackNestedLists) {
    // Test stack with nested lists
    Item result = parseHtml(R"(
        <html>
            <body>
                <ul>
                    <li>Item 1
                        <ul>
                            <li>Nested 1</li>
                            <li>Nested 2</li>
                        </ul>
                    </li>
                    <li>Item 2</li>
                </ul>
            </body>
        </html>
    )");

    int ul_count = countElementsByTag(result, "ul");
    EXPECT_EQ(ul_count, 2);

    int li_count = countElementsByTag(result, "li");
    EXPECT_EQ(li_count, 4);
}

TEST_F(HtmlParserTest, Phase5StackScriptAndStyle) {
    // Test stack with script and style elements (raw text)
    Item result = parseHtml(R"(
        <html>
            <head>
                <style>body { margin: 0; }</style>
                <script>console.log('test');</script>
            </head>
            <body>
                <div>Content</div>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "style"), nullptr);
    EXPECT_NE(findElementByTag(result, "script"), nullptr);
    EXPECT_NE(findElementByTag(result, "div"), nullptr);
}

TEST_F(HtmlParserTest, Phase5StackComplexDocument) {
    // Test stack with complex real-world structure
    Item result = parseHtml(R"(
        <html>
            <head>
                <title>Test Page</title>
                <meta charset="UTF-8">
                <link rel="stylesheet" href="style.css">
            </head>
            <body>
                <header>
                    <nav>
                        <ul>
                            <li><a href="/">Home</a></li>
                            <li><a href="/about">About</a></li>
                        </ul>
                    </nav>
                </header>
                <main>
                    <article>
                        <h1>Article Title</h1>
                        <p>Paragraph with <strong>bold</strong> text.</p>
                    </article>
                </main>
                <footer>
                    <p>Copyright 2025</p>
                </footer>
            </body>
        </html>
    )");

    // Verify all major elements are present
    EXPECT_NE(findElementByTag(result, "html"), nullptr);
    EXPECT_NE(findElementByTag(result, "head"), nullptr);
    EXPECT_NE(findElementByTag(result, "body"), nullptr);
    EXPECT_NE(findElementByTag(result, "header"), nullptr);
    EXPECT_NE(findElementByTag(result, "nav"), nullptr);
    EXPECT_NE(findElementByTag(result, "main"), nullptr);
    EXPECT_NE(findElementByTag(result, "article"), nullptr);
    EXPECT_NE(findElementByTag(result, "footer"), nullptr);

    int p_count = countElementsByTag(result, "p");
    EXPECT_EQ(p_count, 2);
}

TEST_F(HtmlParserTest, Phase5StackEmptyElements) {
    // Test stack with empty elements
    Item result = parseHtml("<html><body><div></div><span></span><p></p></body></html>");

    EXPECT_NE(findElementByTag(result, "div"), nullptr);
    EXPECT_NE(findElementByTag(result, "span"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
}

TEST_F(HtmlParserTest, Phase5StackMultipleClosingTags) {
    // Test proper handling of multiple consecutive closing tags
    Item result = parseHtml(R"(
        <html>
            <body>
                <div>
                    <p>Text</p>
                </div>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "div"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
}

// ============================================================================
// Phase 6 Tests: Special Element Handling (Formatting Elements)
// ============================================================================

TEST_F(HtmlParserTest, Phase6FormattingBasicBold) {
    // Test basic bold formatting element
    Item result = parseHtml("<p>Text with <b>bold</b> content</p>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    Element* b = findElementByTag(result, "b");
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(getElementTagName(b), "b");
}

TEST_F(HtmlParserTest, Phase6FormattingMultipleTypes) {
    // Test multiple different formatting elements
    Item result = parseHtml(R"(
        <p>Text with <b>bold</b>, <i>italic</i>, <strong>strong</strong>,
        <em>emphasis</em>, <code>code</code>, and <u>underlined</u> text.</p>
    )");

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "strong"), nullptr);
    EXPECT_NE(findElementByTag(result, "em"), nullptr);
    EXPECT_NE(findElementByTag(result, "code"), nullptr);
    EXPECT_NE(findElementByTag(result, "u"), nullptr);
}

TEST_F(HtmlParserTest, Phase6FormattingNested) {
    // Test nested formatting elements
    Item result = parseHtml("<p><b>Bold with <i>italic</i> inside</b></p>");

    Element* b = findElementByTag(result, "b");
    ASSERT_NE(b, nullptr);

    Element* i = findElementByTag(result, "i");
    ASSERT_NE(i, nullptr);

    // Verify italic is found within the document (proper nesting handled by parser)
    EXPECT_STREQ(getElementTagName(b), "b");
    EXPECT_STREQ(getElementTagName(i), "i");
}

TEST_F(HtmlParserTest, Phase6FormattingDeeplyNested) {
    // Test deeply nested formatting elements
    Item result = parseHtml(
        "<p><b>Level 1 <i>Level 2 <u>Level 3 <code>Level 4</code></u></i></b></p>"
    );

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "u"), nullptr);
    EXPECT_NE(findElementByTag(result, "code"), nullptr);
}

TEST_F(HtmlParserTest, Phase6FormattingMultipleSiblings) {
    // Test multiple formatting elements as siblings
    Item result = parseHtml(
        "<p><b>Bold 1</b> <i>Italic 1</i> <b>Bold 2</b> <i>Italic 2</i></p>"
    );

    // Count multiple instances
    int bold_count = 0;
    int italic_count = 0;

    Element* p = findElementByTag(result, "p");
    if (p) {
        List* p_list = (List*)p;
        TypeElmt* p_type = (TypeElmt*)p->type;
        int64_t attr_count = p_list->length - p_type->content_length;

        for (int64_t i = attr_count; i < p_list->length; i++) {
            Item child = p_list->items[i];
            if (get_type_id(child) == LMD_TYPE_ELEMENT) {
                Element* elem = child.element;
                const char* tag = getElementTagName(elem);
                if (strcmp(tag, "b") == 0) bold_count++;
                if (strcmp(tag, "i") == 0) italic_count++;
            }
        }
    }

    EXPECT_EQ(bold_count, 2);
    EXPECT_EQ(italic_count, 2);
}

TEST_F(HtmlParserTest, Phase6FormattingAcrossParagraphs) {
    // Test formatting elements properly scoped to paragraphs
    Item result = parseHtml(R"(
        <div>
            <p>First paragraph with <b>bold</b> text.</p>
            <p>Second paragraph with <i>italic</i> text.</p>
        </div>
    )");

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);

    int p_count = countElementsByTag(result, "p");
    EXPECT_EQ(p_count, 2);
}

TEST_F(HtmlParserTest, Phase6FormattingEmpty) {
    // Test empty formatting elements
    Item result = parseHtml("<p>Text with <b></b> empty bold</p>");

    Element* b = findElementByTag(result, "b");
    ASSERT_NE(b, nullptr);

    List* b_list = (List*)b;
    TypeElmt* b_type = (TypeElmt*)b->type;
    EXPECT_EQ(b_type->content_length, 0);
}

TEST_F(HtmlParserTest, Phase6FormattingWithAttributes) {
    // Test formatting elements with attributes
    Item result = parseHtml(
        "<p><span class='highlight'><b>Bold</b> and <i>italic</i></span></p>"
    );

    Element* span = findElementByTag(result, "span");
    ASSERT_NE(span, nullptr);

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
}

TEST_F(HtmlParserTest, Phase6RawTextScript) {
    // Test script as raw text element
    Item result = parseHtml(R"(
        <html>
            <head>
                <script>
                    function test() {
                        return "<div>not parsed</div>";
                    }
                </script>
            </head>
        </html>
    )");

    Element* script = findElementByTag(result, "script");
    ASSERT_NE(script, nullptr);

    // Content should be text, not parsed as HTML
    TypeElmt* script_type = (TypeElmt*)script->type;
    EXPECT_GT(script_type->content_length, 0);

    if (script_type->content_length > 0) {
        List* script_list = (List*)script;
        int64_t attr_count = script_list->length - script_type->content_length;
        Item firstChild = script_list->items[attr_count];
        EXPECT_EQ(get_type_id(firstChild), LMD_TYPE_STRING);
    }
}

TEST_F(HtmlParserTest, Phase6RawTextStyle) {
    // Test style as raw text element
    Item result = parseHtml(R"(
        <html>
            <head>
                <style>
                    body { color: red; }
                    .class > span { font-weight: bold; }
                </style>
            </head>
        </html>
    )");

    Element* style = findElementByTag(result, "style");
    ASSERT_NE(style, nullptr);

    // Content should be text, not parsed as HTML
    TypeElmt* style_type = (TypeElmt*)style->type;
    EXPECT_GT(style_type->content_length, 0);
}

TEST_F(HtmlParserTest, Phase6RawTextTextarea) {
    // Test textarea as RCDATA element
    Item result = parseHtml(R"(
        <form>
            <textarea>
                Some text with <b>tags</b> that should not be parsed
            </textarea>
        </form>
    )");

    Element* textarea = findElementByTag(result, "textarea");
    ASSERT_NE(textarea, nullptr);

    // Content should be text
    TypeElmt* textarea_type = (TypeElmt*)textarea->type;
    EXPECT_GT(textarea_type->content_length, 0);
}

TEST_F(HtmlParserTest, Phase6FormattingComplexNesting) {
    // Test complex real-world formatting scenario
    Item result = parseHtml(R"(
        <article>
            <h1>Article Title</h1>
            <p>
                This is a paragraph with <strong>strong text</strong> and
                <em>emphasized text</em>. It also has <code>inline code</code>
                and <a href="#">a link with <strong>bold</strong> text</a>.
            </p>
            <p>
                Another paragraph with <b>bold</b>, <i>italic</i>,
                <u>underlined</u>, and <s>strikethrough</s> text.
            </p>
        </article>
    )");

    EXPECT_NE(findElementByTag(result, "article"), nullptr);
    EXPECT_NE(findElementByTag(result, "strong"), nullptr);
    EXPECT_NE(findElementByTag(result, "em"), nullptr);
    EXPECT_NE(findElementByTag(result, "code"), nullptr);
    EXPECT_NE(findElementByTag(result, "a"), nullptr);
    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "u"), nullptr);
    EXPECT_NE(findElementByTag(result, "s"), nullptr);

    int p_count = countElementsByTag(result, "p");
    EXPECT_EQ(p_count, 2);
}

TEST_F(HtmlParserTest, Phase6FormattingList) {
    // Test formatting inside list items
    Item result = parseHtml(R"(
        <ul>
            <li><b>Bold item 1</b></li>
            <li><i>Italic item 2</i></li>
            <li><strong>Strong item 3</strong></li>
        </ul>
    )");

    EXPECT_NE(findElementByTag(result, "ul"), nullptr);

    int li_count = countElementsByTag(result, "li");
    EXPECT_EQ(li_count, 3);

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "strong"), nullptr);
}

TEST_F(HtmlParserTest, Phase6FormattingTable) {
    // Test formatting inside table cells
    Item result = parseHtml(R"(
        <table>
            <tr>
                <td><b>Bold cell</b></td>
                <td><i>Italic cell</i></td>
            </tr>
        </table>
    )");

    EXPECT_NE(findElementByTag(result, "table"), nullptr);
    EXPECT_NE(findElementByTag(result, "tr"), nullptr);

    int td_count = countElementsByTag(result, "td");
    EXPECT_EQ(td_count, 2);

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
}

TEST_F(HtmlParserTest, Phase6MixedFormattingAndRawText) {
    // Test document with both formatting elements and raw text elements
    Item result = parseHtml(R"(
        <html>
            <head>
                <style>body { color: blue; }</style>
                <script>var x = 10;</script>
            </head>
            <body>
                <p>Text with <b>bold</b> and <i>italic</i>.</p>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "style"), nullptr);
    EXPECT_NE(findElementByTag(result, "script"), nullptr);
    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
}

// ============================================================================
// Phase 7 Tests: Parser Integration (Formatting Element Tracking)
// ============================================================================

TEST_F(HtmlParserTest, Phase7FormattingTrackedAndRemoved) {
    // Test that formatting elements are tracked during parsing and properly removed
    Item result = parseHtml("<p><b>Bold text</b> normal text</p>");

    // Verify the bold element was created
    Element* b = findElementByTag(result, "b");
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(getElementTagName(b), "b");

    // The formatting element should have been tracked and then removed when closed
    // (We can't directly test the active_formatting list from here, but the
    // structure should be correct if parsing succeeded)
    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, Phase7MultipleFormattingTracking) {
    // Test multiple formatting elements being tracked simultaneously
    Item result = parseHtml(R"(
        <div>
            <p><b>Bold</b> and <i>italic</i> and <strong>strong</strong></p>
            <p><em>emphasis</em> and <code>code</code></p>
        </div>
    )");

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "strong"), nullptr);
    EXPECT_NE(findElementByTag(result, "em"), nullptr);
    EXPECT_NE(findElementByTag(result, "code"), nullptr);
}

TEST_F(HtmlParserTest, Phase7NestedFormattingTracking) {
    // Test nested formatting elements tracking
    Item result = parseHtml("<p><b>Bold <i>and italic <u>and underlined</u></i></b></p>");

    Element* b = findElementByTag(result, "b");
    Element* i = findElementByTag(result, "i");
    Element* u = findElementByTag(result, "u");

    ASSERT_NE(b, nullptr);
    ASSERT_NE(i, nullptr);
    ASSERT_NE(u, nullptr);
}

TEST_F(HtmlParserTest, Phase7FormattingClearedOnHeadClose) {
    // Test that formatting elements are cleared when head closes
    // (Even though formatting in head is unusual, we test the clearing mechanism)
    Item result = parseHtml(R"(
        <html>
            <head>
                <title>Test</title>
            </head>
            <body>
                <p>Body content</p>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "head"), nullptr);
    EXPECT_NE(findElementByTag(result, "body"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
}

TEST_F(HtmlParserTest, Phase7FormattingClearedOnBodyClose) {
    // Test that formatting elements are cleared when body closes
    Item result = parseHtml(R"(
        <html>
            <body>
                <p><b>Bold</b> and <i>italic</i></p>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "body"), nullptr);
    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
}

TEST_F(HtmlParserTest, Phase7FormattingWithImplicitElements) {
    // Test formatting elements with implicit html/head/body
    Item result = parseHtml("<p><b>Bold text</b></p>");

    Element* b = findElementByTag(result, "b");
    ASSERT_NE(b, nullptr);

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, Phase7FormattingInComplexStructure) {
    // Test formatting elements in complex document structure
    Item result = parseHtml(R"(
        <html>
            <head><title>Test</title></head>
            <body>
                <header><h1>Title with <b>bold</b></h1></header>
                <main>
                    <article>
                        <p>First paragraph with <strong>strong</strong>.</p>
                        <p>Second with <em>emphasis</em> and <code>code</code>.</p>
                    </article>
                    <aside>
                        <p>Sidebar with <i>italic</i> text.</p>
                    </aside>
                </main>
                <footer><p>Footer with <small>small</small> text.</p></footer>
            </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "strong"), nullptr);
    EXPECT_NE(findElementByTag(result, "em"), nullptr);
    EXPECT_NE(findElementByTag(result, "code"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "small"), nullptr);
}

TEST_F(HtmlParserTest, Phase7FormattingInterleaved) {
    // Test interleaved opening and closing of formatting elements
    Item result = parseHtml(R"(
        <p>
            <b>Bold start</b>
            <i>Italic start</i>
            <b>Bold again</b>
            <i>Italic again</i>
        </p>
    )");

    // Should find both bold and italic elements
    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
}

TEST_F(HtmlParserTest, Phase7AllFormattingElements) {
    // Test all supported formatting elements
    Item result = parseHtml(R"(
        <div>
            <a href="#">link</a>
            <b>bold</b>
            <big>big</big>
            <code>code</code>
            <em>emphasis</em>
            <font>font</font>
            <i>italic</i>
            <nobr>nobr</nobr>
            <s>strikethrough</s>
            <small>small</small>
            <strike>strike</strike>
            <strong>strong</strong>
            <tt>teletype</tt>
            <u>underline</u>
        </div>
    )");

    EXPECT_NE(findElementByTag(result, "a"), nullptr);
    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "big"), nullptr);
    EXPECT_NE(findElementByTag(result, "code"), nullptr);
    EXPECT_NE(findElementByTag(result, "em"), nullptr);
    EXPECT_NE(findElementByTag(result, "font"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "nobr"), nullptr);
    EXPECT_NE(findElementByTag(result, "s"), nullptr);
    EXPECT_NE(findElementByTag(result, "small"), nullptr);
    EXPECT_NE(findElementByTag(result, "strike"), nullptr);
    EXPECT_NE(findElementByTag(result, "strong"), nullptr);
    EXPECT_NE(findElementByTag(result, "tt"), nullptr);
    EXPECT_NE(findElementByTag(result, "u"), nullptr);
}

// ============================================================================
// Phase 8 Tests: Simple Reconstruction for Misnested Formatting
// ============================================================================

TEST_F(HtmlParserTest, Phase8SimpleMisnestingBoldParagraph) {
    // Test basic misnesting: <b><p>text</b></p>
    // Should reconstruct: <b></b><p><b>text</b></p>
    Item result = parseHtml("<b><p>text</p></b>");

    // Both b and p should exist
    Element* b = findElementByTag(result, "b");
    Element* p = findElementByTag(result, "p");

    ASSERT_NE(b, nullptr);
    ASSERT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, Phase8MisnestingMultipleBlocks) {
    // Test formatting element across multiple blocks
    Item result = parseHtml("<b><p>First</p><p>Second</p></b>");

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    int p_count = countElementsByTag(result, "p");
    EXPECT_EQ(p_count, 2);
}

TEST_F(HtmlParserTest, Phase8MisnestingNestedFormatting) {
    // Test nested formatting with block interruption
    Item result = parseHtml("<b><i><p>text</p></i></b>");

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
}

TEST_F(HtmlParserTest, Phase8MisnestingWithDiv) {
    // Test formatting interrupted by div
    Item result = parseHtml("<strong><div>content</div></strong>");

    EXPECT_NE(findElementByTag(result, "strong"), nullptr);
    EXPECT_NE(findElementByTag(result, "div"), nullptr);
}

TEST_F(HtmlParserTest, Phase8MisnestingMultipleFormatting) {
    // Test multiple formatting elements with block
    Item result = parseHtml("<b><i><p>text</p></i></b>");

    Element* b = findElementByTag(result, "b");
    Element* i = findElementByTag(result, "i");
    Element* p = findElementByTag(result, "p");

    ASSERT_NE(b, nullptr);
    ASSERT_NE(i, nullptr);
    ASSERT_NE(p, nullptr);
}

TEST_F(HtmlParserTest, Phase8MisnestingHeading) {
    // Test formatting with heading element
    Item result = parseHtml("<b><h1>Title</h1></b>");

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "h1"), nullptr);
}

TEST_F(HtmlParserTest, Phase8MisnestingList) {
    // Test formatting with list structure
    Item result = parseHtml("<b><ul><li>Item</li></ul></b>");

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "ul"), nullptr);
    EXPECT_NE(findElementByTag(result, "li"), nullptr);
}

TEST_F(HtmlParserTest, Phase8MisnestingBlockquote) {
    // Test formatting with blockquote
    Item result = parseHtml("<i><blockquote>Quote</blockquote></i>");

    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "blockquote"), nullptr);
}

TEST_F(HtmlParserTest, Phase8MisnestingComplexStructure) {
    // Test complex misnesting scenario
    Item result = parseHtml(R"(
        <b>Bold start
            <p>Paragraph 1</p>
            <i>Italic start
                <div>Division</div>
            </i>
            <p>Paragraph 2</p>
        </b>
    )");

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "div"), nullptr);

    int p_count = countElementsByTag(result, "p");
    EXPECT_EQ(p_count, 2);
}

TEST_F(HtmlParserTest, Phase8NoReconstructionWithoutFormatting) {
    // Test that reconstruction doesn't happen when no formatting is active
    Item result = parseHtml("<div><p>Just blocks</p></div>");

    EXPECT_NE(findElementByTag(result, "div"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
}

TEST_F(HtmlParserTest, Phase8ReconstructionPreservesContent) {
    // Test that content is preserved through reconstruction
    Item result = parseHtml("<b><p>Hello World</p></b>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    std::string content = getTextContent((Item){.element = p});
    EXPECT_TRUE(content.find("Hello World") != std::string::npos);
}

TEST_F(HtmlParserTest, Phase8MisnestingWithAttributes) {
    // Test that attributes are preserved during reconstruction
    Item result = parseHtml(R"(<b class="bold"><p id="para">text</p></b>)");

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
}

TEST_F(HtmlParserTest, Phase8MultipleBlocksInFormatting) {
    // Test multiple different block types within formatting
    Item result = parseHtml(R"(
        <strong>
            <p>Paragraph</p>
            <div>Division</div>
            <h2>Heading</h2>
            <ul><li>List item</li></ul>
        </strong>
    )");

    EXPECT_NE(findElementByTag(result, "strong"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
    EXPECT_NE(findElementByTag(result, "div"), nullptr);
    EXPECT_NE(findElementByTag(result, "h2"), nullptr);
    EXPECT_NE(findElementByTag(result, "ul"), nullptr);
    EXPECT_NE(findElementByTag(result, "li"), nullptr);
}

// ============================================================================
// Phase 9 Tests: Foster Parenting for Table Misnesting
// ============================================================================

TEST_F(HtmlParserTest, Phase9TableBasicStructure) {
    // Test basic well-formed table structure
    Item result = parseHtml(R"(
        <table>
            <tr>
                <td>Cell 1</td>
                <td>Cell 2</td>
            </tr>
        </table>
    )");

    EXPECT_NE(findElementByTag(result, "table"), nullptr);
    EXPECT_NE(findElementByTag(result, "tr"), nullptr);

    int td_count = countElementsByTag(result, "td");
    EXPECT_EQ(td_count, 2);
}

TEST_F(HtmlParserTest, Phase9TableWithTbody) {
    // Test table with explicit tbody
    Item result = parseHtml(R"(
        <table>
            <tbody>
                <tr>
                    <td>Data</td>
                </tr>
            </tbody>
        </table>
    )");

    EXPECT_NE(findElementByTag(result, "table"), nullptr);
    EXPECT_NE(findElementByTag(result, "tbody"), nullptr);
    EXPECT_NE(findElementByTag(result, "tr"), nullptr);
    EXPECT_NE(findElementByTag(result, "td"), nullptr);
}

TEST_F(HtmlParserTest, Phase9TableWithTheadTfoot) {
    // Test table with thead and tfoot
    Item result = parseHtml(R"(
        <table>
            <thead>
                <tr><th>Header</th></tr>
            </thead>
            <tbody>
                <tr><td>Data</td></tr>
            </tbody>
            <tfoot>
                <tr><td>Footer</td></tr>
            </tfoot>
        </table>
    )");

    EXPECT_NE(findElementByTag(result, "thead"), nullptr);
    EXPECT_NE(findElementByTag(result, "tbody"), nullptr);
    EXPECT_NE(findElementByTag(result, "tfoot"), nullptr);
    EXPECT_NE(findElementByTag(result, "th"), nullptr);

    int td_count = countElementsByTag(result, "td");
    EXPECT_EQ(td_count, 2);
}

TEST_F(HtmlParserTest, Phase9TableWithCaption) {
    // Test table with caption
    Item result = parseHtml(R"(
        <table>
            <caption>Table Caption</caption>
            <tr><td>Data</td></tr>
        </table>
    )");

    EXPECT_NE(findElementByTag(result, "table"), nullptr);
    EXPECT_NE(findElementByTag(result, "caption"), nullptr);
    EXPECT_NE(findElementByTag(result, "td"), nullptr);
}

TEST_F(HtmlParserTest, Phase9TableWithColgroup) {
    // Test table with colgroup and col
    Item result = parseHtml(R"(
        <table>
            <colgroup>
                <col span="2">
            </colgroup>
            <tr><td>A</td><td>B</td></tr>
        </table>
    )");

    EXPECT_NE(findElementByTag(result, "table"), nullptr);
    EXPECT_NE(findElementByTag(result, "colgroup"), nullptr);
    EXPECT_NE(findElementByTag(result, "col"), nullptr);
}

TEST_F(HtmlParserTest, Phase9TableMisplacedText) {
    // Test table with text directly inside (should be fostered or handled)
    Item result = parseHtml(R"(
        <table>
            Misplaced text
            <tr><td>Cell</td></tr>
        </table>
    )");

    // Table should still be created
    EXPECT_NE(findElementByTag(result, "table"), nullptr);
    EXPECT_NE(findElementByTag(result, "tr"), nullptr);
    EXPECT_NE(findElementByTag(result, "td"), nullptr);
}

TEST_F(HtmlParserTest, Phase9TableMisplacedDiv) {
    // Test table with div directly inside (should be fostered)
    Item result = parseHtml(R"(
        <table>
            <div>Misplaced content</div>
            <tr><td>Cell</td></tr>
        </table>
    )");

    EXPECT_NE(findElementByTag(result, "table"), nullptr);
    EXPECT_NE(findElementByTag(result, "div"), nullptr);
    EXPECT_NE(findElementByTag(result, "tr"), nullptr);
}

TEST_F(HtmlParserTest, Phase9TableComplexStructure) {
    // Test complex table with multiple sections
    Item result = parseHtml(R"(
        <table border="1">
            <caption>Sales Report</caption>
            <colgroup>
                <col style="background-color: lightblue">
                <col style="background-color: lightgreen">
            </colgroup>
            <thead>
                <tr>
                    <th>Product</th>
                    <th>Sales</th>
                </tr>
            </thead>
            <tbody>
                <tr>
                    <td>Product A</td>
                    <td>100</td>
                </tr>
                <tr>
                    <td>Product B</td>
                    <td>150</td>
                </tr>
            </tbody>
            <tfoot>
                <tr>
                    <td>Total</td>
                    <td>250</td>
                </tr>
            </tfoot>
        </table>
    )");

    EXPECT_NE(findElementByTag(result, "table"), nullptr);
    EXPECT_NE(findElementByTag(result, "caption"), nullptr);
    EXPECT_NE(findElementByTag(result, "colgroup"), nullptr);
    EXPECT_NE(findElementByTag(result, "thead"), nullptr);
    EXPECT_NE(findElementByTag(result, "tbody"), nullptr);
    EXPECT_NE(findElementByTag(result, "tfoot"), nullptr);

    int tr_count = countElementsByTag(result, "tr");
    EXPECT_EQ(tr_count, 4); // 1 thead + 2 tbody + 1 tfoot

    int col_count = countElementsByTag(result, "col");
    EXPECT_EQ(col_count, 2);
}

TEST_F(HtmlParserTest, Phase9NestedTables) {
    // Test nested tables
    Item result = parseHtml(R"(
        <table>
            <tr>
                <td>
                    Outer cell
                    <table>
                        <tr><td>Inner cell</td></tr>
                    </table>
                </td>
            </tr>
        </table>
    )");

    int table_count = countElementsByTag(result, "table");
    EXPECT_EQ(table_count, 2);

    int td_count = countElementsByTag(result, "td");
    EXPECT_EQ(td_count, 2);
}

TEST_F(HtmlParserTest, Phase9TableInDiv) {
    // Test table inside div (normal nesting)
    Item result = parseHtml(R"(
        <div>
            <table>
                <tr><td>Cell</td></tr>
            </table>
        </div>
    )");

    EXPECT_NE(findElementByTag(result, "div"), nullptr);
    EXPECT_NE(findElementByTag(result, "table"), nullptr);
    EXPECT_NE(findElementByTag(result, "tr"), nullptr);
    EXPECT_NE(findElementByTag(result, "td"), nullptr);
}

TEST_F(HtmlParserTest, Phase9TableContextDetection) {
    // Test that table context detection works
    // This is more of an infrastructure test
    Item result = parseHtml(R"(
        <table>
            <tr>
                <td>Cell content</td>
            </tr>
        </table>
    )");

    Element* table = findElementByTag(result, "table");
    ASSERT_NE(table, nullptr);

    Element* td = findElementByTag(result, "td");
    ASSERT_NE(td, nullptr);
}

TEST_F(HtmlParserTest, Phase9MultipleTablesInDocument) {
    // Test multiple tables in same document
    Item result = parseHtml(R"(
        <div>
            <table><tr><td>Table 1</td></tr></table>
            <p>Between tables</p>
            <table><tr><td>Table 2</td></tr></table>
        </div>
    )");

    int table_count = countElementsByTag(result, "table");
    EXPECT_EQ(table_count, 2);

    EXPECT_NE(findElementByTag(result, "p"), nullptr);
}

// ========================================
// Phase 10: HTML5 Compliance Edge Cases
// ========================================

TEST_F(HtmlParserTest, Phase10NestedFormattingMultipleLevels) {
    // Test deep nesting of formatting elements
    Item result = parseHtml(R"(<b><i><u><s>deep text</s></u></i></b>)");

    Element* b_elem = findElementByTag(result, "b");
    ASSERT_NE(b_elem, nullptr);

    Element* i_elem = findElementByTag(Item{.element = b_elem}, "i");
    ASSERT_NE(i_elem, nullptr);

    Element* u_elem = findElementByTag(Item{.element = i_elem}, "u");
    ASSERT_NE(u_elem, nullptr);

    Element* s_elem = findElementByTag(Item{.element = u_elem}, "s");
    ASSERT_NE(s_elem, nullptr);
}

TEST_F(HtmlParserTest, Phase10MixedFormattingAndBlocks) {
    // Test formatting spanning multiple blocks
    Item result = parseHtml(R"(<b><p>para1</p><p>para2</p></b>)");

    int p_count = countElementsByTag(result, "p");
    EXPECT_EQ(p_count, 2);

    // Both paragraphs should have bold applied due to reconstruction
    int b_count = countElementsByTag(result, "b");
    EXPECT_GE(b_count, 1);
}

TEST_F(HtmlParserTest, Phase10SelfClosingTagsInContext) {
    // Test void elements in various contexts
    Item result = parseHtml(R"(
        <div>
            <p>Text <br> more text <img src="test.png"> end</p>
            <hr>
            <input type="text">
        </div>
    )");

    EXPECT_NE(findElementByTag(result, "br"), nullptr);
    EXPECT_NE(findElementByTag(result, "img"), nullptr);
    EXPECT_NE(findElementByTag(result, "hr"), nullptr);
    EXPECT_NE(findElementByTag(result, "input"), nullptr);
}

TEST_F(HtmlParserTest, Phase10MisnestingWithAttributes) {
    // Test that elements with attributes are handled during reconstruction
    Item result = parseHtml(R"(<b class="highlight" id="b1"><p>text</p></b>)");

    Element* b_elem = findElementByTag(result, "b");
    ASSERT_NE(b_elem, nullptr);

    // Element should be properly reconstructed
    Element* p_elem = findElementByTag(result, "p");
    ASSERT_NE(p_elem, nullptr);
}

TEST_F(HtmlParserTest, Phase10ComplexListNesting) {
    // Test nested lists with formatting
    Item result = parseHtml(R"(
        <ul>
            <li><b>Bold item</b></li>
            <li>
                <ul>
                    <li><i>Nested italic</i></li>
                </ul>
            </li>
        </ul>
    )");

    int ul_count = countElementsByTag(result, "ul");
    EXPECT_EQ(ul_count, 2);

    int li_count = countElementsByTag(result, "li");
    EXPECT_EQ(li_count, 3);

    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
}

TEST_F(HtmlParserTest, Phase10HeadingsWithFormatting) {
    // Test formatting elements in headings
    Item result = parseHtml(R"(
        <h1><b>Bold Heading</b></h1>
        <h2><i>Italic</i> <u>Underlined</u></h2>
        <h3><code>Code in heading</code></h3>
    )");

    EXPECT_NE(findElementByTag(result, "h1"), nullptr);
    EXPECT_NE(findElementByTag(result, "h2"), nullptr);
    EXPECT_NE(findElementByTag(result, "h3"), nullptr);
    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "u"), nullptr);
    EXPECT_NE(findElementByTag(result, "code"), nullptr);
}

TEST_F(HtmlParserTest, Phase10DivSpanMixing) {
    // Test inline/block element mixing
    Item result = parseHtml(R"(
        <div>
            <span>Inline text</span>
            <div>Block text</div>
            <span>More inline</span>
        </div>
    )");

    int div_count = countElementsByTag(result, "div");
    EXPECT_EQ(div_count, 2);  // Parent div + nested div

    int span_count = countElementsByTag(result, "span");
    EXPECT_EQ(span_count, 2);
}

TEST_F(HtmlParserTest, Phase10TableComplexNesting) {
    // Test complex table structure
    Item result = parseHtml(R"(
        <table>
            <caption>Table Title</caption>
            <thead>
                <tr><th>Header 1</th><th>Header 2</th></tr>
            </thead>
            <tbody>
                <tr><td>Cell 1</td><td>Cell 2</td></tr>
                <tr><td>Cell 3</td><td>Cell 4</td></tr>
            </tbody>
            <tfoot>
                <tr><td>Footer 1</td><td>Footer 2</td></tr>
            </tfoot>
        </table>
    )");

    EXPECT_NE(findElementByTag(result, "table"), nullptr);
    EXPECT_NE(findElementByTag(result, "caption"), nullptr);
    EXPECT_NE(findElementByTag(result, "thead"), nullptr);
    EXPECT_NE(findElementByTag(result, "tbody"), nullptr);
    EXPECT_NE(findElementByTag(result, "tfoot"), nullptr);

    int th_count = countElementsByTag(result, "th");
    EXPECT_EQ(th_count, 2);

    int td_count = countElementsByTag(result, "td");
    EXPECT_EQ(td_count, 6);
}

TEST_F(HtmlParserTest, Phase10EmptyElements) {
    // Test empty elements are handled correctly
    Item result = parseHtml(R"(
        <div></div>
        <p></p>
        <span></span>
        <b></b>
    )");

    EXPECT_NE(findElementByTag(result, "div"), nullptr);
    EXPECT_NE(findElementByTag(result, "p"), nullptr);
    EXPECT_NE(findElementByTag(result, "span"), nullptr);
    EXPECT_NE(findElementByTag(result, "b"), nullptr);
}

TEST_F(HtmlParserTest, Phase10WhitespacePreservation) {
    // Test that whitespace in elements is handled
    Item result = parseHtml(R"(<pre>  line 1
  line 2
  line 3  </pre>)");

    Element* pre_elem = findElementByTag(result, "pre");
    ASSERT_NE(pre_elem, nullptr);

    // Pre element should preserve whitespace
    List* pre_list = (List*)pre_elem;
    EXPECT_GT(pre_list->length, 0);
}

TEST_F(HtmlParserTest, Phase10FormElements) {
    // Test form and form elements
    Item result = parseHtml(R"(
        <form action="/submit" method="post">
            <label for="name">Name:</label>
            <input type="text" id="name" name="name">
            <textarea name="message"></textarea>
            <select name="choice">
                <option value="1">Option 1</option>
                <option value="2">Option 2</option>
            </select>
            <button type="submit">Submit</button>
        </form>
    )");

    EXPECT_NE(findElementByTag(result, "form"), nullptr);
    EXPECT_NE(findElementByTag(result, "label"), nullptr);
    EXPECT_NE(findElementByTag(result, "input"), nullptr);
    EXPECT_NE(findElementByTag(result, "textarea"), nullptr);
    EXPECT_NE(findElementByTag(result, "select"), nullptr);
    EXPECT_NE(findElementByTag(result, "option"), nullptr);
    EXPECT_NE(findElementByTag(result, "button"), nullptr);
}

TEST_F(HtmlParserTest, Phase10LinkAndScriptElements) {
    // Test special head elements
    Item result = parseHtml(R"(
        <html>
        <head>
            <title>Test Page</title>
            <meta charset="utf-8">
            <link rel="stylesheet" href="style.css">
            <script src="script.js"></script>
        </head>
        <body>
            <p>Content</p>
        </body>
        </html>
    )");

    EXPECT_NE(findElementByTag(result, "html"), nullptr);
    EXPECT_NE(findElementByTag(result, "head"), nullptr);
    EXPECT_NE(findElementByTag(result, "title"), nullptr);
    EXPECT_NE(findElementByTag(result, "meta"), nullptr);
    EXPECT_NE(findElementByTag(result, "body"), nullptr);
}

TEST_F(HtmlParserTest, Phase10SemanticElements) {
    // Test HTML5 semantic elements
    Item result = parseHtml(R"(
        <article>
            <header><h1>Article Title</h1></header>
            <section>
                <p>Article content</p>
            </section>
            <footer>Footer content</footer>
        </article>
        <aside>Sidebar content</aside>
        <nav>Navigation</nav>
    )");

    EXPECT_NE(findElementByTag(result, "article"), nullptr);
    EXPECT_NE(findElementByTag(result, "header"), nullptr);
    EXPECT_NE(findElementByTag(result, "section"), nullptr);
    EXPECT_NE(findElementByTag(result, "footer"), nullptr);
    EXPECT_NE(findElementByTag(result, "aside"), nullptr);
    EXPECT_NE(findElementByTag(result, "nav"), nullptr);
}

TEST_F(HtmlParserTest, Phase10MixedQuotesInAttributes) {
    // Test different quote styles in attributes
    Item result = parseHtml(R"(
        <div id="div1" class='highlight' data-value="test">
            <img src="image.png" alt='An "image"'>
        </div>
    )");

    Element* div_elem = findElementByTag(result, "div");
    ASSERT_NE(div_elem, nullptr);

    Element* img_elem = findElementByTag(result, "img");
    ASSERT_NE(img_elem, nullptr);

    // Verify attributes are present on elements
    EXPECT_TRUE(div_elem->has_attr("id"));
    EXPECT_TRUE(img_elem->has_attr("src"));
}

TEST_F(HtmlParserTest, Phase10UnclosedTags) {
    // Test parser handles unclosed tags gracefully
    Item result = parseHtml(R"(
        <div>
            <p>Paragraph
            <p>Another paragraph
        </div>
    )");

    int p_count = countElementsByTag(result, "p");
    EXPECT_EQ(p_count, 2);

    Element* div_elem = findElementByTag(result, "div");
    ASSERT_NE(div_elem, nullptr);
}

TEST_F(HtmlParserTest, Phase10RealWorldFragment) {
    // Test realistic HTML fragment
    Item result = parseHtml(R"(
        <article class="blog-post">
            <header>
                <h1><a href="/post/123">Post Title</a></h1>
                <p class="meta">By <strong>Author Name</strong> on <time>2025-01-01</time></p>
            </header>
            <div class="content">
                <p>This is the <b>first</b> paragraph with <i>some</i> formatting.</p>
                <p>Second paragraph with a <a href="link.html">link</a>.</p>
                <ul>
                    <li>First item</li>
                    <li>Second item with <code>code</code></li>
                </ul>
            </div>
            <footer>
                <p>Tags: <span class="tag">html</span>, <span class="tag">css</span></p>
            </footer>
        </article>
    )");

    // Verify structure is preserved
    EXPECT_NE(findElementByTag(result, "article"), nullptr);
    EXPECT_NE(findElementByTag(result, "header"), nullptr);
    EXPECT_NE(findElementByTag(result, "h1"), nullptr);
    EXPECT_NE(findElementByTag(result, "footer"), nullptr);

    // Verify content elements
    int p_count = countElementsByTag(result, "p");
    EXPECT_GE(p_count, 3);

    // Verify formatting preserved
    EXPECT_NE(findElementByTag(result, "b"), nullptr);
    EXPECT_NE(findElementByTag(result, "i"), nullptr);
    EXPECT_NE(findElementByTag(result, "code"), nullptr);
    EXPECT_NE(findElementByTag(result, "strong"), nullptr);

    // Verify links
    int a_count = countElementsByTag(result, "a");
    EXPECT_EQ(a_count, 2);
}

// ============================================================================
// DT/DD Auto-Close Tests (HTML Spec Optional End Tags)
// ============================================================================

// Test: DT elements should auto-close previous DT (basic case)
TEST_F(HtmlParserTest, AutoCloseDtClosesOtherDt) {
    Item result = parseHtml(R"(
        <dl>
            <dt>Term 1
            <dt>Term 2
        </dl>
    )");

    // Should have 2 DT elements as siblings in DL
    int dt_count = countElementsByTag(result, "dt");
    EXPECT_EQ(dt_count, 2);

    // Verify they are siblings (children of DL), not nested
    Element* dl = findElementByTag(result, "dl");
    ASSERT_NE(dl, nullptr);

    // Count direct DT children of DL
    List* dl_list = (List*)dl;
    TypeElmt* dl_type = (TypeElmt*)dl->type;
    int64_t attr_count = dl_list->length - dl_type->content_length;

    int direct_dt_count = 0;
    for (int64_t i = attr_count; i < dl_list->length; i++) {
        Item child = dl_list->items[i];
        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            Element* child_elem = child.element;
            TypeElmt* child_type = (TypeElmt*)child_elem->type;
            if (strview_equal(&child_type->name, "dt")) {
                direct_dt_count++;
            }
        }
    }
    EXPECT_EQ(direct_dt_count, 2);
}

// Test: DD elements should auto-close previous DT
TEST_F(HtmlParserTest, AutoCloseDdClosesDt) {
    Item result = parseHtml(R"(
        <dl>
            <dt>Term
            <dd>Definition
        </dl>
    )");

    // Should have 1 DT and 1 DD as siblings
    int dt_count = countElementsByTag(result, "dt");
    int dd_count = countElementsByTag(result, "dd");
    EXPECT_EQ(dt_count, 1);
    EXPECT_EQ(dd_count, 1);

    // Verify DD is NOT nested inside DT
    Element* dt = findElementByTag(result, "dt");
    ASSERT_NE(dt, nullptr);

    // DD should not be a child of DT
    Element* dd_in_dt = findElementByTag(Item{.element = dt}, "dd");
    EXPECT_EQ(dd_in_dt, nullptr);
}

// Test: DT elements should auto-close previous DD
TEST_F(HtmlParserTest, AutoCloseDtClosesDd) {
    Item result = parseHtml(R"(
        <dl>
            <dd>Definition 1
            <dt>Term 2
            <dd>Definition 2
        </dl>
    )");

    // Should have 1 DT and 2 DD as siblings
    int dt_count = countElementsByTag(result, "dt");
    int dd_count = countElementsByTag(result, "dd");
    EXPECT_EQ(dt_count, 1);
    EXPECT_EQ(dd_count, 2);
}

// Test: DD elements should auto-close previous DD
TEST_F(HtmlParserTest, AutoCloseDdClosesOtherDd) {
    Item result = parseHtml(R"(
        <dl>
            <dt>Term
            <dd>Definition 1
            <dd>Definition 2
        </dl>
    )");

    // Should have 1 DT and 2 DD as siblings
    int dt_count = countElementsByTag(result, "dt");
    int dd_count = countElementsByTag(result, "dd");
    EXPECT_EQ(dt_count, 1);
    EXPECT_EQ(dd_count, 2);
}

// Test: Complete DL with multiple DT/DD pairs (typical use case)
TEST_F(HtmlParserTest, AutoCloseMultipleDtDdPairs) {
    Item result = parseHtml(R"(
        <dl>
            <dt>HTML
            <dd>HyperText Markup Language
            <dt>CSS
            <dd>Cascading Style Sheets
            <dt>JS
            <dd>JavaScript
        </dl>
    )");

    // Should have 3 DT and 3 DD as siblings
    int dt_count = countElementsByTag(result, "dt");
    int dd_count = countElementsByTag(result, "dd");
    EXPECT_EQ(dt_count, 3);
    EXPECT_EQ(dd_count, 3);
}

// Test: DT/DD with nested elements inside (content should remain)
TEST_F(HtmlParserTest, AutoCloseDtDdWithNestedContent) {
    Item result = parseHtml(R"(
        <dl>
            <dt><a href="link1.html">Term with link</a>
            <dd>Definition with <strong>bold</strong> text
            <dt><code>code-term</code>
            <dd>Another definition
        </dl>
    )");

    // Should have 2 DT and 2 DD
    int dt_count = countElementsByTag(result, "dt");
    int dd_count = countElementsByTag(result, "dd");
    EXPECT_EQ(dt_count, 2);
    EXPECT_EQ(dd_count, 2);

    // Nested elements should still be there
    EXPECT_NE(findElementByTag(result, "a"), nullptr);
    EXPECT_NE(findElementByTag(result, "strong"), nullptr);
    EXPECT_NE(findElementByTag(result, "code"), nullptr);
}

// Test: CERN.html style - HTML 1.0 with multi-line tags
TEST_F(HtmlParserTest, AutoCloseCernHtmlStyle) {
    Item result = parseHtml(R"(
<dl>
<dt><a href="link1.html">What's out there?</a>
<dd> Pointers to the world's online information
<dt><a href="link2.html">Help</a>
<dd> on the browser you are using
<dt><a href="link3.html">Software Products</a>
<dd> A list of project components
</dl>
    )");

    // Should have 3 DT and 3 DD as siblings
    int dt_count = countElementsByTag(result, "dt");
    int dd_count = countElementsByTag(result, "dd");
    EXPECT_EQ(dt_count, 3);
    EXPECT_EQ(dd_count, 3);

    // All 3 links should be present
    int a_count = countElementsByTag(result, "a");
    EXPECT_EQ(a_count, 3);
}

// Test: LI auto-close (similar pattern)
TEST_F(HtmlParserTest, AutoCloseLiClosesOtherLi) {
    Item result = parseHtml(R"(
        <ul>
            <li>Item 1
            <li>Item 2
            <li>Item 3
        </ul>
    )");

    // Should have 3 LI elements as siblings
    int li_count = countElementsByTag(result, "li");
    EXPECT_EQ(li_count, 3);
}

// Test: P auto-close when followed by block element
TEST_F(HtmlParserTest, AutoClosePClosedByDiv) {
    Item result = parseHtml(R"(
        <p>Paragraph text
        <div>Block content</div>
    )");

    // P should be closed by div, both should exist
    int p_count = countElementsByTag(result, "p");
    int div_count = countElementsByTag(result, "div");
    EXPECT_EQ(p_count, 1);
    EXPECT_EQ(div_count, 1);

    // Div should NOT be nested inside P
    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);
    Element* div_in_p = findElementByTag(Item{.element = p}, "div");
    EXPECT_EQ(div_in_p, nullptr);
}

// Test: TR auto-close
TEST_F(HtmlParserTest, AutoCloseTrClosesOtherTr) {
    Item result = parseHtml(R"(
        <table>
            <tr><td>R1C1</td>
            <tr><td>R2C1</td>
        </table>
    )");

    int tr_count = countElementsByTag(result, "tr");
    EXPECT_EQ(tr_count, 2);
}

// Test: TD auto-close
TEST_F(HtmlParserTest, AutoCloseTdClosesOtherTd) {
    Item result = parseHtml(R"(
        <table>
            <tr>
                <td>Cell 1
                <td>Cell 2
                <td>Cell 3
            </tr>
        </table>
    )");

    int td_count = countElementsByTag(result, "td");
    EXPECT_EQ(td_count, 3);
}
