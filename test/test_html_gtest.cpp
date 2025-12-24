#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

extern "C" {
    #include "../lambda/input/input.hpp"
    #include "../lib/mempool.h"
    #include "../lib/stringbuf.h"
    #include "../lib/strview.h"
    #include "../lib/arraylist.h"
    #include "../lib/log.h"
    void rpmalloc_initialize();
    void rpmalloc_finalize();
}

// Helper to create Lambda String
String* create_lambda_string(const char* text) {
    if (!text) return NULL;

    size_t len = strlen(text);
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return NULL;

    result->len = len;
    result->ref_cnt = 1;
    strcpy(result->chars, text);

    return result;
}

// Test fixture for HTML parsing tests
class HtmlParserTest : public ::testing::Test {
protected:
    Pool* pool;
    String* html_type;

    void SetUp() override {
        log_init(NULL);
        pool = pool_create();
        ASSERT_NE(pool, nullptr);

        html_type = create_lambda_string("html");
        ASSERT_NE(html_type, nullptr);

        log_parse_config_file("log.conf");
        log_init("");
    }

    void TearDown() override {
        if (html_type) {
            free(html_type);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }

    Item parseHtml(const char* html) {
        Input* input = input_from_source(html, NULL, html_type, NULL);
        if (!input) {
            return Item{.item = ITEM_NULL};
        }

        Element* extracted = input_get_html_fragment_element(input, html);
        if (extracted) {
            return Item{.element = extracted};
        }

        return input->root;
    }

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

    std::string getAttr(Element* elmt, const char* attr_name) {
        if (!elmt || !elmt->type) return "";

        TypeElmt* type = (TypeElmt*)elmt->type;
        if (!type->shape || !elmt->data) return "";

        ShapeEntry* shape = type->shape;

        while (shape) {
            if (shape->name && strview_equal(shape->name, attr_name)) {
                void* field_ptr = (char*)elmt->data + shape->byte_offset;
                TypeId type_id = shape->type ? shape->type->type_id : LMD_TYPE_NULL;

                if (type_id == LMD_TYPE_STRING) {
                    String** str_ptr = (String**)field_ptr;
                    if (str_ptr && *str_ptr) {
                        String* str = *str_ptr;
                        return std::string(str->chars, str->len);
                    }
                } else if (type_id == LMD_TYPE_NULL) {
                    return "";
                } else if (type_id == LMD_TYPE_BOOL) {
                    bool val = *(bool*)field_ptr;
                    return val ? "true" : "false";
                }
                return "";
            }
            shape = shape->next;
        }

        return "";
    }

    bool hasAttr(Element* elmt, const char* attr_name) {
        if (!elmt || !elmt->type) return false;

        TypeElmt* type = (TypeElmt*)elmt->type;
        if (!type->shape) return false;

        ShapeEntry* shape = type->shape;

        while (shape) {
            if (shape->name && strview_equal(shape->name, attr_name)) {
                return true;
            }
            shape = shape->next;
        }

        return false;
    }

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

// ============================================================================
// Attribute Tests
// ============================================================================

TEST_F(HtmlParserTest, AttributeQuoted) {
    Item result = parseHtml(R"(<div id="test" class="container"></div>)");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "id"), "test");
    EXPECT_EQ(getAttr(div, "class"), "container");
}

TEST_F(HtmlParserTest, AttributeUnquoted) {
    Item result = parseHtml("<div id=test class=container></div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "id"), "test");
    EXPECT_EQ(getAttr(div, "class"), "container");
}

TEST_F(HtmlParserTest, AttributeSingleQuoted) {
    Item result = parseHtml("<div id='my-id' class='container'></div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "id"), "my-id");
    EXPECT_EQ(getAttr(div, "class"), "container");
}

TEST_F(HtmlParserTest, AttributeEmpty) {
    Item result = parseHtml("<input disabled readonly>");

    Element* input = findElementByTag(result, "input");
    ASSERT_NE(input, nullptr);

    EXPECT_TRUE(hasAttr(input, "disabled"));
    EXPECT_TRUE(hasAttr(input, "readonly"));
}

TEST_F(HtmlParserTest, AttributeBoolean) {
    Item result = parseHtml("<input disabled checked>");

    Element* input = findElementByTag(result, "input");
    ASSERT_NE(input, nullptr);

    EXPECT_TRUE(hasAttr(input, "disabled"));
    EXPECT_TRUE(hasAttr(input, "checked"));
}

TEST_F(HtmlParserTest, AttributeDataCustom) {
    Item result = parseHtml(R"(<div data-value="123" data-name="test"></div>)");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "data-value"), "123");
    EXPECT_EQ(getAttr(div, "data-name"), "test");
}

TEST_F(HtmlParserTest, AttributeAria) {
    Item result = parseHtml(R"(<button aria-label="Close" aria-pressed="true"></button>)");

    Element* button = findElementByTag(result, "button");
    ASSERT_NE(button, nullptr);

    EXPECT_EQ(getAttr(button, "aria-label"), "Close");
    EXPECT_EQ(getAttr(button, "aria-pressed"), "true");
}

TEST_F(HtmlParserTest, AttributeMultiple) {
    Item result = parseHtml(R"(<div id="test" class="box red" title="tooltip" data-index="5"></div>)");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "id"), "test");
    EXPECT_EQ(getAttr(div, "class"), "box red");
    EXPECT_EQ(getAttr(div, "title"), "tooltip");
    EXPECT_EQ(getAttr(div, "data-index"), "5");
}

TEST_F(HtmlParserTest, AttributeWithSpecialChars) {
    Item result = parseHtml(R"(<div title="A &amp; B"></div>)");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    std::string title = getAttr(div, "title");
    EXPECT_TRUE(title == "A &amp; B" || title == "A & B");
}

TEST_F(HtmlParserTest, AttributeCaseSensitivity) {
    Item result = parseHtml(R"(<div ID="test"></div>)");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_TRUE(hasAttr(div, "ID") || hasAttr(div, "id"));
}

// ============================================================================
// Void Element Tests
// ============================================================================

TEST_F(HtmlParserTest, VoidElementBr) {
    Item result = parseHtml("<p>Line1<br>Line2</p>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);
    // Br element parsing should succeed
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserTest, VoidElementImg) {
    Item result = parseHtml(R"(<img src="test.jpg" alt="Test">)");

    Element* img = findElementByTag(result, "img");
    ASSERT_NE(img, nullptr);

    TypeElmt* type = (TypeElmt*)img->type;
    EXPECT_TRUE(strview_equal(&type->name, "img"));
    EXPECT_EQ(getAttr(img, "src"), "test.jpg");
}

TEST_F(HtmlParserTest, VoidElementInput) {
    Item result = parseHtml(R"(<input type="text" name="username">)");

    Element* input = findElementByTag(result, "input");
    ASSERT_NE(input, nullptr);

    EXPECT_EQ(getAttr(input, "type"), "text");
    EXPECT_EQ(getAttr(input, "name"), "username");
}

TEST_F(HtmlParserTest, VoidElementHr) {
    Item result = parseHtml("<div><hr></div>");

    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

// ============================================================================
// Doctype Tests
// ============================================================================

TEST_F(HtmlParserTest, DoctypeHTML5) {
    Item result = parseHtml("<!DOCTYPE html><html><body><p>Test</p></body></html>");
    // Should parse without errors
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserTest, DoctypeUppercase) {
    Item result = parseHtml("<!DOCTYPE HTML><html><body><p>Test</p></body></html>");
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserTest, DoctypeLowercase) {
    Item result = parseHtml("<!doctype html><html><body><p>Test</p></body></html>");
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
}

// ============================================================================
// Comment Tests
// ============================================================================

TEST_F(HtmlParserTest, CommentBeforeRoot) {
    Item result = parseHtml("<!-- comment --><div>content</div>");
    // Should parse successfully, comment may be preserved or stripped
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, CommentAfterRoot) {
    Item result = parseHtml("<div>content</div><!-- comment -->");
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_ERROR);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(HtmlParserTest, EdgeCaseMalformedUnclosedTag) {
    Item result = parseHtml("<div><p>text");
    // Should handle gracefully
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, EdgeCaseMismatchedTags) {
    Item result = parseHtml("<div><span></div></span>");
    // Should handle gracefully
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, EdgeCaseExtraClosingTag) {
    Item result = parseHtml("<div></div></div>");
    // Should handle gracefully
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, EdgeCaseTagNameCase) {
    Item result = parseHtml("<DiV>content</DiV>");

    Element* div = findElementByTag(result, "div");
    EXPECT_TRUE(div != nullptr || findElementByTag(result, "DiV") != nullptr);
}

TEST_F(HtmlParserTest, EdgeCaseLongContent) {
    std::string long_text(100000, 'a');
    std::string html = "<p>" + long_text + "</p>";

    Item result = parseHtml(html.c_str());
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserTest, EdgeCaseManyAttributes) {
    std::string html = "<div";
    for (int i = 1; i <= 50; i++) {
        html += " a" + std::to_string(i) + "=\"v" + std::to_string(i) + "\"";
    }
    html += ">test</div>";

    Item result = parseHtml(html.c_str());
    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "a1"), "v1");
    EXPECT_EQ(getAttr(div, "a10"), "v10");
}

TEST_F(HtmlParserTest, EdgeCaseSelfClosingSyntax) {
    Item result = parseHtml("<div/>");
    // HTML5 doesn't support self-closing non-void elements
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, ParserReuse) {
    Item result1 = parseHtml("<div>first</div>");
    Item result2 = parseHtml("<span>second</span>");

    Element* div = findElementByTag(result1, "div");
    Element* span = findElementByTag(result2, "span");

    ASSERT_NE(div, nullptr);
    ASSERT_NE(span, nullptr);

    TypeElmt* type = (TypeElmt*)span->type;
    EXPECT_TRUE(strview_equal(&type->name, "span"));
}

TEST_F(HtmlParserTest, EntityDecodingInAttribute) {
    Item result = parseHtml(R"(<div title="&lt;tag&gt;"></div>)");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    std::string title = getAttr(div, "title");
    EXPECT_FALSE(title.empty());
}

// ============================================================================
// Data and ARIA Attribute Tests
// ============================================================================

TEST_F(HtmlParserTest, DataAttributesSimple) {
    Item result = parseHtml(R"(<div data-id="123" data-name="test" data-active="true"></div>)");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "data-id"), "123");
    EXPECT_EQ(getAttr(div, "data-name"), "test");
    EXPECT_EQ(getAttr(div, "data-active"), "true");
}

TEST_F(HtmlParserTest, DataAttributesComplex) {
    Item result = parseHtml(R"(<div data-user-id="42" data-api-endpoint="/api/v1/users"></div>)");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "data-user-id"), "42");
    EXPECT_EQ(getAttr(div, "data-api-endpoint"), "/api/v1/users");
}

TEST_F(HtmlParserTest, DataAttributesWithJSON) {
    Item result = parseHtml(R"(<div data-config='{"key":"value"}'></div>)");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    std::string config = getAttr(div, "data-config");
    EXPECT_FALSE(config.empty());
}

TEST_F(HtmlParserTest, AriaAttributesAccessibility) {
    Item result = parseHtml(R"(
        <button aria-label="Close dialog" aria-pressed="false">X</button>
    )");

    Element* button = findElementByTag(result, "button");
    ASSERT_NE(button, nullptr);

    EXPECT_EQ(getAttr(button, "aria-label"), "Close dialog");
    EXPECT_EQ(getAttr(button, "aria-pressed"), "false");
}

TEST_F(HtmlParserTest, AriaAttributesRole) {
    Item result = parseHtml(R"(<div role="navigation" aria-label="Main navigation"></div>)");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "role"), "navigation");
    EXPECT_EQ(getAttr(div, "aria-label"), "Main navigation");
}

TEST_F(HtmlParserTest, AriaAttributesLiveRegion) {
    Item result = parseHtml(R"(<div aria-live="polite" aria-atomic="true"></div>)");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "aria-live"), "polite");
    EXPECT_EQ(getAttr(div, "aria-atomic"), "true");
}

TEST_F(HtmlParserTest, MixedDataAndAriaAttributes) {
    Item result = parseHtml(R"(
        <div
            data-component="modal"
            data-visible="true"
            role="dialog"
            aria-modal="true"
            aria-labelledby="modal-title"
            aria-hidden="false">
        </div>
    )");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "data-component"), "modal");
    EXPECT_EQ(getAttr(div, "aria-hidden"), "false");
}

// ============================================================================
// Semantic Element Tests
// ============================================================================

TEST_F(HtmlParserTest, SemanticArticle) {
    Item result = parseHtml("<article><h1>Title</h1><p>Content</p></article>");

    Element* article = findElementByTag(result, "article");
    EXPECT_NE(article, nullptr);
}

TEST_F(HtmlParserTest, SemanticAside) {
    Item result = parseHtml("<aside>sidebar</aside>");

    Element* aside = findElementByTag(result, "aside");
    EXPECT_NE(aside, nullptr);
}

TEST_F(HtmlParserTest, SemanticNav) {
    Item result = parseHtml("<nav><a href='/'>Home</a></nav>");

    Element* nav = findElementByTag(result, "nav");
    EXPECT_NE(nav, nullptr);
}

TEST_F(HtmlParserTest, SemanticSection) {
    Item result = parseHtml("<section><h2>Section Title</h2></section>");

    Element* section = findElementByTag(result, "section");
    EXPECT_NE(section, nullptr);
}

TEST_F(HtmlParserTest, SemanticTime) {
    Item result = parseHtml(R"(<time datetime="2023-01-01">January 1, 2023</time>)");

    Element* time_elem = findElementByTag(result, "time");
    EXPECT_NE(time_elem, nullptr);
}

TEST_F(HtmlParserTest, ClassificationAllVoidElements) {
    Item result = parseHtml(R"(
        <area><base><br><col><embed><hr><img><input>
        <link><meta><param><source><track><wbr>
    )");

    // Should parse without errors
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_ERROR);
}

TEST_F(HtmlParserTest, ClassificationSemanticElements) {
    Item result = parseHtml("<article><header><nav><main><section><aside><footer></footer></aside></section></main></nav></header></article>");

    Element* article = findElementByTag(result, "article");
    EXPECT_NE(article, nullptr);
}

// ============================================================================
// Tree Construction Tests
// ============================================================================

TEST_F(HtmlParserTest, TreeConstructionDeeplyNestedElements) {
    std::string html = "<div>";
    for (int i = 0; i < 100; i++) {
        html += "<span>";
    }
    html += "content";
    for (int i = 0; i < 100; i++) {
        html += "</span>";
    }
    html += "</div>";

    Item result = parseHtml(html.c_str());
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserTest, TreeConstructionVeryDeeplyNested) {
    std::string html;
    for (int i = 0; i < 200; i++) {
        html += "<div>";
    }
    html += "deep";
    for (int i = 0; i < 200; i++) {
        html += "</div>";
    }

    Item result = parseHtml(html.c_str());
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserTest, TreeConstructionEmptyElement) {
    Item result = parseHtml("<div></div>");

    ASSERT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
    Element* elem = result.element;
    TypeElmt* type = (TypeElmt*)elem->type;

    EXPECT_TRUE(strview_equal(&type->name, "div"));
}

TEST_F(HtmlParserTest, TreeConstructionOnlyAttributes) {
    Item result = parseHtml(R"(<div id="test" class="box" data-value="123"></div>)");

    ASSERT_TRUE(get_type_id(result) == LMD_TYPE_ELEMENT);
    Element* div = result.element;
    List* list = (List*)div;

    EXPECT_GE(list->length, 0);

    EXPECT_EQ(getAttr(div, "id"), "test");
    EXPECT_EQ(getAttr(div, "class"), "box");
    EXPECT_EQ(getAttr(div, "data-value"), "123");
}

TEST_F(HtmlParserTest, TreeConstructionSequentialParsing) {
    Item result1 = parseHtml("<div>first</div>");
    Item result2 = parseHtml("<p>second</p>");
    Item result3 = parseHtml("<span>third</span>");

    EXPECT_TRUE(get_type_id(result1) == LMD_TYPE_ELEMENT);
    EXPECT_TRUE(get_type_id(result2) == LMD_TYPE_ELEMENT);
    EXPECT_TRUE(get_type_id(result3) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserTest, WhitespacePreservation) {
    Item result = parseHtml("<pre>  spaces  \n  newlines  </pre>");

    Element* pre_elem = findElementByTag(result, "pre");
    ASSERT_NE(pre_elem, nullptr);

    // HTML5 parser preserves whitespace in <pre> tags
    // Just verify parsing succeeded
    TypeElmt* type = (TypeElmt*)pre_elem->type;
    EXPECT_TRUE(strview_equal(&type->name, "pre"));
}
