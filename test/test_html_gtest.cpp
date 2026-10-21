#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../lambda/input/input.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/input/html5/html5_parser.h"
#include "../lambda/input/html5/html5_tokenizer.h"
#include "../lambda/input/input-parsers.h"
#include "../lambda/format/format.h"
#include "../lib/mime-detect.h"
#include "../lib/arena.h"
#include "../lib/strbuf.h"

extern "C" {
    #include "../lib/mempool.h"
    #include "../lib/stringbuf.h"
    #include "../lib/strview.h"
    #include "../lib/arraylist.h"
    #include "../lib/log.h"
}
#include "../lib/test_utils.h"

// Helper to create Lambda String
String* create_lambda_string(const char* text) {
    if (!text) return NULL;

    size_t len = strlen(text);
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return NULL;

    result->len = len;
    strcpy(result->chars, text);

    return result;
}

// Test fixture for HTML parsing tests
class HtmlParserTest : public ::testing::Test {
protected:
    Pool* pool;
    String* html_type;

    void SetUp() override {
        pool = tu_setup_pool();

        html_type = create_lambda_string("html");
        ASSERT_NE(html_type, nullptr);

        // Re-init log with log.conf to enable category-level filtering for
        // these tests (overrides the default log_init from tu_setup_pool).
        log_parse_config_file("log.conf");
        log_init("");
    }

    void TearDown() override {
        if (html_type) free(html_type);
        tu_teardown_pool(pool);
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

    Input* parse_document_as(const char* source, const char* format = "xhtml", const char* url = nullptr) {
        String* type = format ? create_lambda_string(format) : nullptr;
        Input* input = input_from_source(source, url ? url_parse(url) : nullptr, type, nullptr);
        free(type);
        return input;
    }

    Item parseHtml5Document(const char* html) {
        Input* input = input_from_source("", NULL, html_type, NULL);
        if (!input) {
            return Item{.item = ITEM_NULL};
        }

        Element* doc = html5_parse(input, html);
        if (!doc) {
            return Item{.item = ITEM_NULL};
        }
        return Item{.element = doc};
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
            for (int64_t i = 0; i < elem_list->length; i++) {
                Element* found = findElementByTag(elem_list->items[i], tag_name);
                if (found) return found;
            }
        } else if (get_type_id(item) == LMD_TYPE_ARRAY) {
            List* list = item.array;
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
            List* elem_list = (List*)elem;

            for (int64_t i = 0; i < elem_list->length; i++) {
                result += getTextContent(elem_list->items[i]);
            }
        } else if (get_type_id(item) == LMD_TYPE_ARRAY) {
            List* list = item.array;
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
            shape = typemap_next_field(type, shape);
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
            shape = typemap_next_field(type, shape);
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
            for (int64_t i = 0; i < elem_list->length; i++) {
                count += countElementsByTag(elem_list->items[i], tag_name);
            }
        } else if (get_type_id(item) == LMD_TYPE_ARRAY) {
            List* list = item.array;
            for (int64_t i = 0; i < list->length; i++) {
                count += countElementsByTag(list->items[i], tag_name);
            }
        }

        return count;
    }

    int countDirectChildrenByTag(Element* elem, const char* tag_name) {
        if (!elem || !elem->type) {
            return 0;
        }

        int count = 0;
        List* elem_list = (List*)elem;
        for (int64_t i = 0; i < elem_list->length; i++) {
            Item child = elem_list->items[i];
            if (get_type_id(child) != LMD_TYPE_ELEMENT) {
                continue;
            }
            TypeElmt* child_type = (TypeElmt*)child.element->type;
            if (strview_equal(&child_type->name, tag_name)) {
                count++;
            }
        }
        return count;
    }

    Element* findElementByTagAllSlots(Item item, const char* tag_name) {
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
            for (int64_t i = 0; i < elem_list->length; i++) {
                Element* found = findElementByTagAllSlots(elem_list->items[i], tag_name);
                if (found) return found;
            }
        }

        return nullptr;
    }

    int countDirectElementChildrenByTagAllSlots(Element* elem, const char* tag_name) {
        if (!elem || !elem->type) {
            return 0;
        }

        int count = 0;
        List* elem_list = (List*)elem;
        for (int64_t i = 0; i < elem_list->length; i++) {
            Item child = elem_list->items[i];
            if (get_type_id(child) != LMD_TYPE_ELEMENT) {
                continue;
            }
            TypeElmt* child_type = (TypeElmt*)child.element->type;
            if (strview_equal(&child_type->name, tag_name)) {
                count++;
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

TEST_F(HtmlParserTest, SourceNewlinesNormalizeBeforeCharacterReferences) {
    struct Case { const char* source; const char* tag; const char* expected; } cases[] = {
        {"<p>A\rB\r\nC\nD</p>", "p", "A\nB\nC\nD"},
        {"<textarea>A\rB\r\nC\nD</textarea>", "textarea", "A\nB\nC\nD"},
        {"<title>A\rB\r\nC\nD</title>", "title", "A\nB\nC\nD"},
        {"<style>A\rB\r\nC\nD</style>", "style", "A\nB\nC\nD"},
        {"<script>A\rB\r\nC\nD</script>", "script", "A\nB\nC\nD"},
        {"<xmp>A\rB\r\nC\nD</xmp>", "xmp", "A\nB\nC\nD"},
        {"<plaintext>A\rB\r\nC\nD", "plaintext", "A\nB\nC\nD"},
        {"<svg><![CDATA[A\rB\r\nC\nD]]></svg>", "svg", "A\nB\nC\nD"},
        {"<p>中文\r左右\r\n↔</p>", "p", "中文\n左右\n↔"},
        {"<p>A&#13;B&#13;&#10;C</p>", "p", "A\rB\r\nC"},
        {"<textarea>A&#13;B&#13;&#10;C</textarea>", "textarea", "A\rB\r\nC"},
    };
    for (const Case& entry : cases) {
        SCOPED_TRACE(entry.tag);
        Input* input = input_from_source(entry.source, nullptr, html_type, nullptr);
        ASSERT_NE(input, nullptr);
        Element* element = findElementByTagAllSlots(input->root, entry.tag);
        ASSERT_NE(element, nullptr);
        StringBuf* text = stringbuf_new(pool);
        ElementReader(element).textContent(text);
        EXPECT_STREQ(text->str->chars, entry.expected);
    }
    Item parsed = parseHtml("<div title=\"A\rB\r\nC\" data-cr=\"A&#13;B\"></div>");
    Element* element = findElementByTagAllSlots(parsed, "div");
    ASSERT_NE(element, nullptr);
    // input preprocessing normalizes literal bytes, while a decoded reference stays in the DOM.
    EXPECT_STREQ(ElementReader(element).get_attr_string("title"), "A\nB\nC");
    EXPECT_STREQ(ElementReader(element).get_attr_string("data-cr"), "A\rB");
    parsed = parseHtml("<div><!--A\rB\r\nC--></div>");
    Element* comment = findElementByTagAllSlots(parsed, "#comment");
    ASSERT_NE(comment, nullptr);
    EXPECT_STREQ(ElementReader(comment).get_attr_string("data"), "A\nB\nC");
}

TEST_F(HtmlParserTest, ExplicitLengthPreservesScriptsAndMarkupAfterNullBytes) {
    const char source[] = "<!doctype html><script>const route='a\0b';</script>"
        "<p>tail</p><script>console.log('after')</script>";
    Input* input = input_from_source_n(source, sizeof(source) - 1, NULL, html_type, NULL);
    ASSERT_NE(input, nullptr);
    Element* script = findElementByTagAllSlots(input->root, "script");
    ASSERT_NE(script, nullptr);
    ElementReader reader(script);
    ASSERT_EQ(reader.childCount(), 1);
    String* text = reader.childAt(0).asString();
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text->chars, "const route='a\xEF\xBF\xBD" "b';");
    ASSERT_NE(findElementByTagAllSlots(input->root, "p"), nullptr);
    Element* head = findElementByTagAllSlots(input->root, "head");
    ASSERT_NE(head, nullptr);
    Element* body = findElementByTagAllSlots(input->root, "body");
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(countDirectElementChildrenByTagAllSlots(head, "script"), 1);
    EXPECT_EQ(countDirectElementChildrenByTagAllSlots(body, "script"), 1);
}

TEST_F(HtmlParserTest, FinalNullInTextIsReplacedBeforeEndOfInput) {
    const char source[] = "<!doctype html><textarea>x\0";
    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    Html5ParseOptions options = {.track_source_lines = true};
    Element* document = html5_parse_n(input, source, sizeof(source) - 1, &options);
    ASSERT_NE(document, nullptr);
    Element* textarea = findElementByTagAllSlots((Item){.element = document}, "textarea");
    ASSERT_NE(textarea, nullptr);
    ElementReader reader(textarea);
    ASSERT_EQ(reader.childCount(), 1);
    String* text = reader.childAt(0).asString();
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text->chars, "x\xEF\xBF\xBD");
}

TEST_F(HtmlParserTest, UiModeKeepsDenseRegistryMarkupStorageBounded) {
    const int registry_rows = 768;
    const size_t max_parser_pool_bytes = 64u * 1024u * 1024u;
    StringBuf* source = stringbuf_new(pool);
    ASSERT_NE(source, nullptr);
    stringbuf_append_str(source, "<html><body><table>");
    for (int row = 0; row < registry_rows; row++) {
        stringbuf_append_str(source,
            "<tr><td><div><span><a href=\"/registry/entry\">entry</a></span></div>"
            "<br><a href=\"/registry/reference\">reference</a></td></tr>");
    }
    stringbuf_append_str(source, "</table></body></html>");

    PoolStats before = {};
    pool_get_detailed_stats(pool, &before);
    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    input->ui_mode = true;
    Element* document = html5_parse(input, source->str->chars);
    ASSERT_NE(document, nullptr);

    ArenaStats arena = {};
    arena_get_stats(input->arena, &arena);
    PoolStats after = {};
    pool_get_detailed_stats(pool, &after);
    EXPECT_LT(arena.committed_bytes, max_parser_pool_bytes);
    EXPECT_LT(after.live_bytes - before.live_bytes, max_parser_pool_bytes);
}

TEST_F(HtmlParserTest, TableWhitespaceDoesNotQuadraticallyGrowInputArena) {
    const int table_rows = 1024;
    const size_t max_parser_arena_bytes = 64u * 1024u * 1024u;
    StringBuf* source = stringbuf_new(pool);
    ASSERT_NE(source, nullptr);
    stringbuf_append_str(source, "<html><body><table>\n");
    for (int row = 0; row < table_rows; row++) {
        stringbuf_append_str(source, "<tr><td>registry entry</td></tr>\n");
    }
    stringbuf_append_str(source, "</table></body></html>");

    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    input->ui_mode = true;
    ASSERT_NE(html5_parse(input, source->str->chars), nullptr);

    ArenaStats arena = {};
    arena_get_stats(input->arena, &arena);
    EXPECT_LT(arena.committed_bytes, max_parser_arena_bytes);
}

TEST_F(HtmlParserTest, MismatchedHeadingEndTagClosesOpenHeading) {
    Item result = parseHtml5Document("<div><h2>Why B-School?</h1><p>Intro</p></div>");

    Element* div = findElementByTagAllSlots(result, "div");
    ASSERT_NE(div, nullptr);
    Element* h2 = findElementByTagAllSlots(result, "h2");
    ASSERT_NE(h2, nullptr);

    EXPECT_EQ(countDirectElementChildrenByTagAllSlots(div, "h2"), 1);
    EXPECT_EQ(countDirectElementChildrenByTagAllSlots(div, "p"), 1);
    EXPECT_EQ(countDirectElementChildrenByTagAllSlots(h2, "p"), 0);
}

TEST_F(HtmlParserTest, AfterAfterFramesetPreservesWhitespaceCommentsAndNoframes) {
    Item result = parseHtml5Document("<!doctype html><html><head></head>"
        "<frameset><frame src='about:blank'></frameset></html>\n"
        "<!--tail--><html data-tail='kept'><noframes>fallback</noframes><p>ignored</p>");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ELEMENT);
    Element* document = result.element;
    ASSERT_EQ(countDirectElementChildrenByTagAllSlots(document, "#comment"), 1);
    Element* html = findElementByTagAllSlots(result, "html");
    ASSERT_NE(html, nullptr);
    EXPECT_STREQ(ElementReader(html).get_attr_string("data-tail"), "kept");
    EXPECT_EQ(countDirectElementChildrenByTagAllSlots(html, "#comment"), 0);
    ASSERT_GT(html->length, 2);
    ASSERT_EQ(get_type_id(html->items[2]), LMD_TYPE_STRING);
    EXPECT_STREQ(html->items[2].get_string()->chars, "\n");
    Element* noframes = findElementByTagAllSlots(result, "noframes");
    ASSERT_NE(noframes, nullptr);
    ASSERT_EQ(noframes->length, 1);
    ASSERT_EQ(get_type_id(noframes->items[0]), LMD_TYPE_STRING);
    EXPECT_STREQ(noframes->items[0].get_string()->chars, "fallback");
    EXPECT_EQ(findElementByTagAllSlots(result, "p"), nullptr);
}

TEST_F(HtmlParserTest, AfterAfterBodyKeepsDocumentCommentsAndReprocessesContent) {
    Item result = parseHtml5Document("<!doctype html><html><body>first</body></html>"
        "<!--tail--><p>second</p>");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ELEMENT);
    EXPECT_EQ(countDirectElementChildrenByTagAllSlots(result.element, "#comment"), 1);
    Element* html = findElementByTagAllSlots(result, "html");
    ASSERT_NE(html, nullptr);
    EXPECT_EQ(countDirectElementChildrenByTagAllSlots(html, "#comment"), 0);
    Element* body = findElementByTagAllSlots(result, "body");
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(countDirectElementChildrenByTagAllSlots(body, "p"), 1);
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

TEST_F(HtmlParserTest, EmptyAttributeValuesRemainStrings) {
    Element* element = findElementByTag(parseHtml5Document(
        "<!doctype html><span lang='' data-empty=\"\" hidden></span>"), "span");
    ASSERT_NE(element, nullptr);
    ElementReader reader(element);
    const char* attributes[] = {"lang", "data-empty", "hidden"};
    for (const char* name : attributes) {
        ASSERT_TRUE(reader.has_attr(name));
        ItemReader value = reader.get_attr(name);
        ASSERT_TRUE(value.isString()) << name;
        ASSERT_NE(value.asString(), nullptr);
        EXPECT_EQ(value.asString()->len, 0u);
        EXPECT_STREQ(reader.get_attr_string(name), "");
    }
    EXPECT_FALSE(reader.has_attr("missing"));
    EXPECT_TRUE(reader.get_attr("missing").isNull());
    EXPECT_EQ(element->length, 0);
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

TEST_F(HtmlParserTest, EndTagAttributesTokenizeWithoutChangingTheClosedElement) {
    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    Html5Parser* parser = html5_parser_create(pool, input->arena, input);
    ASSERT_NE(parser, nullptr);
    Html5ParserScope scope(parser);
    const char* source = "</DiV data-note='first' DATA-NOTE='duplicate' disabled>";
    parser->html = source;
    parser->length = strlen(source);
    Html5Token* token = html5_tokenize_next(parser);
    ASSERT_NE(token, nullptr);
    EXPECT_EQ(token->type, HTML5_TOKEN_END_TAG);
    EXPECT_STREQ(token->tag_name->chars, "div");
    ASSERT_EQ(token->attr_count, 2u);
    EXPECT_STREQ(token->attrs[0].name->chars, "data-note");
    ASSERT_EQ(get_type_id(token->attrs[0].value), LMD_TYPE_STRING);
    EXPECT_STREQ(it2s(token->attrs[0].value)->chars, "first");
    EXPECT_STREQ(token->attrs[1].name->chars, "disabled");
    EXPECT_EQ(html5_tokenize_next(parser)->type, HTML5_TOKEN_EOF);

    Item result = parseHtml5Document("<div id='kept'>a</div id='wrong' data-note='discarded'>"
        "<p id='next'>b</p>");
    Element* div = findElementByTag(result, "div");
    Element* next = findElementByTag(result, "p");
    ASSERT_NE(div, nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_STREQ(ElementReader(div).get_attr_string("id"), "kept");
    EXPECT_EQ(ElementReader(div).get_attr_string("data-note"), nullptr);
    EXPECT_STREQ(ElementReader(next).get_attr_string("id"), "next");
}

TEST_F(HtmlParserTest, FragmentTokenScratchPreservesPublishedDataAndContinuation) {
    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    Html5Parser* parser = html5_fragment_parser_create(input->pool, input->arena, input);
    ASSERT_NE(parser, nullptr);
    StrBuf* markup = strbuf_new();
    ASSERT_NE(markup, nullptr);
    for (size_t i = 0; i < 1000; i++)
        strbuf_append_str(markup, "<span data-key=\"kept\">text</span>");
    size_t before = arena_total_used(input->arena);
    ASSERT_TRUE(html5_fragment_parse(parser, markup->str));
    strbuf_free(markup);
    // token records and attribute buffers must not accumulate with published nodes.
    EXPECT_LT(arena_total_used(input->arena) - before, 384000u);
    EXPECT_EQ(parser->token_arena, parser->arena);
    Element* body = html5_fragment_get_body(parser);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->length, 1000);
    Element* first = body->items[0].element;
    EXPECT_EQ(getAttr(first, "data-key"), "kept");
    ASSERT_TRUE(html5_fragment_parse(parser, "<p id=\"later\">continued</p>"));
    EXPECT_EQ(body->length, 1001);
    EXPECT_EQ(getAttr(first, "data-key"), "kept");
    EXPECT_EQ(getAttr(body->items[1000].element, "id"), "later");
    html5_parser_destroy(parser);
    EXPECT_EQ(body->length, 1001);
    EXPECT_EQ(getAttr(first, "data-key"), "kept");
    EXPECT_EQ(getAttr(body->items[1000].element, "id"), "later");
}

TEST_F(HtmlParserTest, ParserWorkDoesNotAccumulateInPublishedOwner) {
    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    MarkBuilder builder(input);
    Element* published = builder.element("span").attr("data-key", "kept").final().element;
    ASSERT_NE(published, nullptr);
    size_t arena_before = arena_total_used(input->arena);
    PoolStats before = {};
    pool_get_detailed_stats(pool, &before);
    for (size_t i = 0; i < 64; i++) {
        Html5Parser* parser = html5_parser_create(pool, input->arena, input);
        ASSERT_NE(parser, nullptr);
        for (size_t depth = 0; depth < 128; depth++) {
            html5_push_element(parser, published);
            html5_push_active_formatting_element(parser, published, nullptr);
        }
        html5_parser_destroy(parser);
    }
    PoolStats after = {};
    pool_get_detailed_stats(pool, &after);
    EXPECT_EQ(arena_total_used(input->arena), arena_before);
    EXPECT_EQ(after.live_bytes, before.live_bytes);
    EXPECT_STREQ(ElementReader(published).get_attr_string("data-key"), "kept");
}

TEST_F(HtmlParserTest, FragmentWorkGrowthPreservesRawTextAndAttributesAfterDestroy) {
    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    Html5Parser* parser = html5_fragment_parser_create(pool, input->arena, input);
    ASSERT_NE(parser, nullptr);
    StrBuf* markup = strbuf_new();
    ASSERT_NE(markup, nullptr);
    strbuf_append_str(markup, "<textarea data-long=\"");
    for (size_t i = 0; i < 9000; i++) strbuf_append_char(markup, 'x');
    strbuf_append_str(markup, "\">");
    for (size_t i = 0; i < 9000; i++) strbuf_append_char(markup, 'y');
    strbuf_append_str(markup, "</wrong></textarea>");
    ASSERT_TRUE(html5_fragment_parse(parser, markup->str));
    strbuf_free(markup);
    Element* body = html5_fragment_get_body(parser);
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->length, 1);
    Element* text = body->items[0].element;
    html5_parser_destroy(parser);
    ElementReader reader(text);
    String* attribute = reader.get_string_attr("data-long");
    ASSERT_NE(attribute, nullptr);
    EXPECT_EQ(attribute->len, 9000u);
    ASSERT_EQ(reader.childCount(), 1);
    String* content = reader.childAt(0).asString();
    ASSERT_NE(content, nullptr);
    EXPECT_EQ(content->len, 9008u);
    EXPECT_STREQ(content->chars + 9000, "</wrong>");
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


TEST_F(HtmlParserTest, ExternalSvgSharesHtmlForeignContentModel) {
    // compare the complete subtree, including recovery and HTML integration points (D2.11.2).
    const char* sources[] = {
        "<SVG viewbox='0 0 40 20'><lineargradient id='paint'><stop offset=0/></lineargradient>"
        "<rect width=40 height=20 fill=red FILL=blue/></SVG>",
        "<svg><g><path d='M0 0h10v10z'></svg>",
        "<svg><foreignObject><div/><p>label</p></foreignObject></svg>",
        "<svg xmlns='urn:foreign' xmlns:s='http://www.w3.org/2000/svg'>"
        "<s:rect width='10'/><circle r='5'/></svg>",
        "<svg><text> a <![CDATA[<b>&]]> z </text></svg>"
    };
    for (const char* source : sources) {
        SCOPED_TRACE(source);
        Input* external = Input::create(pool);
        Input* embedded = Input::create(pool);
        ASSERT_NE(external, nullptr);
        ASSERT_NE(embedded, nullptr);
        Element* actual = parse_svg_document(external, source);
        ASSERT_NE(actual, nullptr);
        Element* document = html5_parse(embedded, source);
        ASSERT_NE(document, nullptr);
        Element* expected = findElementByTag(Item{.element = document}, "svg");
        ASSERT_NE(expected, nullptr);
        String* actual_xml = format_xml(pool, Item{.element = actual});
        String* expected_xml = format_xml(pool, Item{.element = expected});
        ASSERT_NE(actual_xml, nullptr);
        ASSERT_NE(expected_xml, nullptr);
        EXPECT_STREQ(actual_xml->chars, expected_xml->chars);
    }
}

TEST_F(HtmlParserTest, ExternalSvgUsesHtmlNamesAndDocumentOwner) {
    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    StrBuf* source = strbuf_new();
    ASSERT_NE(source, nullptr);
    strbuf_append_str(source, "\xEF\xBB\xBF<?xml version='1.0'?>"
        "<SVG viewbox='0 0 20 10'><foreignobject><div/><p>kept</p></foreignobject></SVG>");
    Element* svg = parse_svg_document(input, source->str);
    strbuf_free(source);
    ASSERT_NE(svg, nullptr);
    EXPECT_TRUE(ElementReader(input->root).hasTag("#document"));
    EXPECT_STREQ(ElementReader(svg).get_attr_string("viewBox"), "0 0 20 10");
    Element* foreign = findElementByTag(Item{.element = svg}, "foreignObject");
    ASSERT_NE(foreign, nullptr);
    Element* div = findElementByTag(Item{.element = foreign}, "div");
    ASSERT_NE(div, nullptr);
    ASSERT_EQ(div->length, 1);
    EXPECT_TRUE(ElementReader(div->items[0]).hasTag("p"));
    EXPECT_STREQ(div->items[0].element->items[0].get_string()->chars, "kept");
}

TEST_F(HtmlParserTest, SvgHtmlIntegrationEndTagsDoNotCaseFoldForeignAncestors) {
    Item document = parseHtml5Document("<svg><foreignObject><h:div/></foreignObject>"
        "<foreignObject><div class='second'/></foreignObject></svg>");
    Element* svg = findElementByTag(document, "svg");
    ASSERT_NE(svg, nullptr);
    ASSERT_EQ(svg->length, 1);
    ElementReader foreign(svg->items[0]);
    EXPECT_TRUE(foreign.hasTag("foreignObject"));
    ElementReader literal = foreign.findChildElement("h:div");
    ASSERT_TRUE(literal.isValid());
    ElementReader html_foreign = literal.findChildElement("foreignobject");
    ASSERT_TRUE(html_foreign.isValid());
    EXPECT_TRUE(html_foreign.findChildElement("div").isValid());
    document = parseHtml5Document("<svg><foreignObject><div></div></foreignObject><circle r='5'/></svg>");
    svg = findElementByTag(document, "svg");
    ASSERT_NE(svg, nullptr);
    ASSERT_EQ(svg->length, 2);
    EXPECT_TRUE(ElementReader(svg->items[1]).hasTag("circle"));
}

TEST_F(HtmlParserTest, XhtmlUsesHtmlDocumentAndInternedNames) {
    const char* source = "<?xml version=\"1.0\"?><!DOCTYPE h:html>"
        "<!--note--><h:html xmlns:h=\"http://www.w3.org/1999/xhtml\">"
        "<h:head/><h:body><h:div id=\"box\" class=\"card\" style=\"width:80px\">text</h:div>"
        "</h:body></h:html>";
    Input* input = parse_document_as(source);
    ASSERT_NE(input, nullptr);
    ASSERT_FALSE(input->parse_failed);
    EXPECT_TRUE(ElementReader(input->root).hasTag("#document"));
    EXPECT_EQ(findElementByTag(input->root, "?xml"), nullptr);
    Element* doctype = findElementByTag(input->root, "#doctype");
    ASSERT_NE(doctype, nullptr);
    EXPECT_STREQ(doctype->get_attr("name").string()->chars, "html");
    Element* comment = findElementByTag(input->root, "#comment");
    ASSERT_NE(comment, nullptr);
    EXPECT_STREQ(comment->get_attr("data").string()->chars, "note");
    Element* div = findElementByTag(input->root, "div");
    ASSERT_NE(div, nullptr);
    Input* html = parse_document_as("<!doctype html><html><head></head><body>"
        "<div id=\"box\" class=\"card\" style=\"width:80px\">text</div></body></html>", "html");
    ASSERT_NE(html, nullptr);
    Element* html_div = findElementByTag(html->root, "div");
    ASSERT_NE(html_div, nullptr);
    EXPECT_EQ(ElementReader(div).tagId(), ElementReader(html_div).tagId());
    const char* attributes[] = {"id", "class", "style"};
    for (const char* key : attributes) {
        EXPECT_STREQ(div->get_attr(key).string()->chars, html_div->get_attr(key).string()->chars);
    }
    EXPECT_EQ(findElementByTag(input->root, "h:div"), nullptr);
}

TEST_F(HtmlParserTest, XhtmlPreservesXmlContentAndForeignNamespaces) {
    Input* input = parse_document_as("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body>"
        "<div/><p> a <![CDATA[<b>&]]> z </p><pre>  a\n b  </pre>"
        "<s:svg xmlns:s=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 20 30\">"
        "<s:foreignObject><div>inside</div></s:foreignObject></s:svg>"
        "<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><mi>x</mi></math>"
        "<div xmlns=\"\">foreign</div><DIV>case-sensitive</DIV></body></html>");
    ASSERT_NE(input, nullptr);
    ASSERT_FALSE(input->parse_failed);
    Element* body = findElementByTag(input->root, "body");
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->length, 7);
    EXPECT_EQ(body->items[0].element->length, 0);
    Element* paragraph = body->items[1].element;
    ASSERT_EQ(paragraph->length, 1);
    EXPECT_STREQ(paragraph->items[0].get_string()->chars, " a <b>& z ");
    Element* pre = findElementByTag(input->root, "pre");
    ASSERT_NE(pre, nullptr);
    EXPECT_STREQ(pre->items[0].get_string()->chars, "  a\n b  ");
    Element* svg = findElementByTag(input->root, "svg");
    ASSERT_NE(svg, nullptr);
    EXPECT_STREQ(svg->get_attr("viewBox").string()->chars, "0 0 20 30");
    EXPECT_STREQ(svg->get_attr("__lambda_ns_uri").string()->chars, "http://www.w3.org/2000/svg");
    Element* object = findElementByTag(input->root, "foreignObject");
    ASSERT_NE(object, nullptr);
    EXPECT_STREQ(object->items[0].element->get_attr("__lambda_ns_uri").string()->chars,
        "http://www.w3.org/1999/xhtml");
    EXPECT_STREQ(body->items[5].element->get_attr("__lambda_ns_uri").string()->chars, "");
    EXPECT_NE(findElementByTag(input->root, "DIV"), nullptr);
}

TEST_F(HtmlParserTest, XhtmlRejectsMalformedXmlAndWrongRoots) {
    const char* invalid[] = {
        "", "<html/>", "<html xmlns=\"urn:wrong\"/>",
        "<body xmlns=\"http://www.w3.org/1999/xhtml\"/>",
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"><p></P></html>",
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"><div></html>",
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"><h:div/></html>",
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" id=\"a\" id=\"b\"/>",
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:a=\"urn:a\" xmlns:b=\"urn:a\" a:id=\"a\" b:id=\"b\"/>",
        "<html xmlns=\"http://www.w3.org/1999/xhtml\">&nbsp;</html>",
        "<!DOCTYPE html [<!ENTITY e 'x'>]><html xmlns=\"http://www.w3.org/1999/xhtml\"/>",
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"/><html xmlns=\"http://www.w3.org/1999/xhtml\"/>"
    };
    for (const char* source : invalid) {
        SCOPED_TRACE(source);
        Input* input = parse_document_as(source);
        ASSERT_NE(input, nullptr);
        EXPECT_TRUE(input->parse_failed);
    }
}

TEST_F(HtmlParserTest, XhtmlDetectsExtensionsAndMime) {
    const char* urls[] = {"file:///page.xhtml", "file:///page.XHT"};
    for (const char* url : urls) {
        Input* input = parse_document_as("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head/><body/></html>", nullptr, url);
        ASSERT_NE(input, nullptr);
        ASSERT_FALSE(input->parse_failed);
        EXPECT_TRUE(ElementReader(input->root).hasTag("#document"));
    }
    EXPECT_STREQ(mime_extension_from_content_type("application/xhtml+xml; charset=UTF-8"), ".xhtml");
}

TEST_F(HtmlParserTest, XhtmlRejectsTransportNulAndTracksSourceLines) {
    Input* input = parse_document_as("<html xmlns=\"http://www.w3.org/1999/xhtml\"/>");
    ASSERT_NE(input, nullptr);
    const char* source = "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n<head/>\n<body><p/></body></html>";
    ASSERT_NE(parse_xhtml_document(input, source, strlen(source), true), nullptr);
    Element* paragraph = findElementByTag(input->root, "p");
    ASSERT_NE(paragraph, nullptr);
    EXPECT_EQ(ElementReader(paragraph).get_attr("__source_line").asInt(), 3);
    const char truncated[] = "<html xmlns=\"http://www.w3.org/1999/xhtml\"/>\0<broken>";
    EXPECT_EQ(parse_xhtml_document(input, truncated, sizeof(truncated) - 1), nullptr);
    EXPECT_TRUE(input->parse_failed);
}
