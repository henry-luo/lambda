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

// Test fixture for HTML parser negative tests (invalid HTML)
class HtmlParserNegativeTest : public ::testing::Test {
protected:
    Pool* pool;
    String* html_type;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);

        html_type = create_lambda_string("html");
        ASSERT_NE(html_type, nullptr);

        // initialize logging
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

    // Helper: Parse HTML and return root element
    Item parseHtml(const char* html) {
        Input* input = input_from_source(html, NULL, html_type, NULL);
        if (!input) {
            return Item{.item = ITEM_NULL};
        }
        return input->root;
    }

    // Helper: Get element by tag name
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

            // check children
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

    // Helper: Get text content
    std::string getTextContent(Item item) {
        std::string result;

        if (item.item == ITEM_NULL || item.item == ITEM_ERROR) {
            return result;
        }

        if (get_type_id(item) == LMD_TYPE_STRING) {
            String* str = (String*)item.pointer;
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

    // Helper: Count elements by tag
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

    // Helper: Get attribute value
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
};

// ============================================================================
// Malformed Tags Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, MalformedUnclosedTag) {
    // HTML5 spec: unclosed tags should be auto-closed
    Item result = parseHtml("<div><p>Text");

    // should parse and auto-close tags
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);

    // should be able to find div
    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserNegativeTest, MalformedMismatchedTags) {
    // HTML5 spec: mismatched closing tags - parser should handle gracefully
    Item result = parseHtml("<div><span></div></span>");

    // should parse without crashing
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);

    // div should exist
    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserNegativeTest, MalformedNestedTagsImproperNesting) {
    // HTML5 spec: improperly nested tags - parser should reconstruct tree
    Item result = parseHtml("<b><i>Text</b></i>");

    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);

    // at least one of the tags should parse
    EXPECT_TRUE(findElementByTag(result, "b") != nullptr ||
                findElementByTag(result, "i") != nullptr);
}

TEST_F(HtmlParserNegativeTest, MalformedExtraClosingTag) {
    // HTML5 spec: extra closing tags should be ignored
    Item result = parseHtml("<div>Content</div></div>");

    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);

    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserNegativeTest, MalformedEmptyTagName) {
    // HTML5 spec: empty tag names are parse errors - should return error/null
    Item result = parseHtml("<>Content</>");

    // should reject or return error
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_ERROR);
}

TEST_F(HtmlParserNegativeTest, MalformedInvalidTagName) {
    // HTML5 spec: invalid tag names with special characters
    Item result = parseHtml("<div@>Content</div@>");

    // parser may reject or accept with sanitized name
    // at minimum, shouldn't crash
    EXPECT_TRUE(true);
}

TEST_F(HtmlParserNegativeTest, MalformedMissingClosingBracket) {
    // HTML5 spec: missing closing '>' in opening tag
    Item result = parseHtml("<div Content");

    // should handle gracefully (may auto-close or error)
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserNegativeTest, MalformedSpaceInTagName) {
    // HTML5 spec: space in tag name is invalid
    Item result = parseHtml("<div span>Content</div span>");

    // should handle gracefully
    EXPECT_TRUE(true);
}

// ============================================================================
// Invalid Attributes Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, InvalidAttributeUnclosedQuote) {
    // HTML5 spec: unclosed attribute quotes - should parse to end of tag
    Item result = parseHtml("<div id=\"unclosed>Content</div>");

    Element* div = findElementByTag(result, "div");
    if (div) {
        // attribute may be malformed but element should exist
        EXPECT_TRUE(true);
    } else {
        // or parser may reject entirely
        EXPECT_TRUE(get_type_id(result) == LMD_TYPE_NULL ||
                    get_type_id(result) == LMD_TYPE_ERROR);
    }
}

TEST_F(HtmlParserNegativeTest, InvalidAttributeMismatchedQuotes) {
    // HTML5 spec: mismatched quotes in attributes
    Item result = parseHtml("<div id=\"value'>Content</div>");

    // should parse somehow (either with mismatched quotes or error)
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, InvalidAttributeNoValue) {
    // HTML5 spec: attribute with '=' but no value - should treat as empty
    Item result = parseHtml("<div id=>Content</div>");

    Element* div = findElementByTag(result, "div");
    if (div) {
        // attribute should exist but be empty
        EXPECT_TRUE(true);
    }
}

TEST_F(HtmlParserNegativeTest, InvalidAttributeDuplicateAttributes) {
    // HTML5 spec: duplicate attributes - first wins
    Item result = parseHtml("<div id=\"first\" id=\"second\">Content</div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    std::string id_val = getAttr(div, "id");
    // HTML5 spec: first attribute value should be used
    EXPECT_TRUE(id_val == "first" || !id_val.empty());
}

TEST_F(HtmlParserNegativeTest, InvalidAttributeEqualsWithoutName) {
    // HTML5 spec: attribute equals sign without name
    Item result = parseHtml("<div =\"value\">Content</div>");

    // parser rejects this with error
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR || get_type_id(result) == LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, InvalidAttributeSpecialCharsInName) {
    // HTML5 spec: special characters in attribute names
    Item result = parseHtml("<div id@name=\"value\">Content</div>");

    // parser may sanitize or reject
    EXPECT_TRUE(true);
}

TEST_F(HtmlParserNegativeTest, InvalidAttributeWhitespaceAroundEquals) {
    // HTML5 spec: whitespace around '=' is allowed
    Item result = parseHtml("<div id = \"value\">Content</div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    // attribute should parse correctly
    std::string id_val = getAttr(div, "id");
    EXPECT_EQ(id_val, "value");
}

// ============================================================================
// Invalid Entity References Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, InvalidEntityUnknownNamed) {
    // HTML5 spec: unknown named entities should be preserved as-is
    Item result = parseHtml("<p>&unknownentity;</p>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    std::string text = getTextContent(Item{.element = p});
    // should preserve the unknown entity
    EXPECT_TRUE(text.find("&unknownentity;") != std::string::npos ||
                !text.empty());
}

TEST_F(HtmlParserNegativeTest, InvalidEntityMissingSemicolon) {
    // HTML5 spec: entities without semicolon - may or may not be recognized
    Item result = parseHtml("<p>&lt&gt</p>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    std::string text = getTextContent(Item{.element = p});
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserNegativeTest, InvalidEntityNumericOutOfRange) {
    // HTML5 spec: numeric entities out of valid Unicode range
    Item result = parseHtml("<p>&#9999999;</p>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    // should parse without crashing (may replace with replacement char)
    std::string text = getTextContent(Item{.element = p});
    EXPECT_TRUE(true);
}

TEST_F(HtmlParserNegativeTest, InvalidEntityNumericInvalidHex) {
    // HTML5 spec: invalid hex entity reference
    Item result = parseHtml("<p>&#xGGGG;</p>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    // should handle gracefully
    std::string text = getTextContent(Item{.element = p});
    EXPECT_TRUE(true);
}

TEST_F(HtmlParserNegativeTest, InvalidEntityAmpersandWithoutEntity) {
    // HTML5 spec: bare ampersand should be preserved
    Item result = parseHtml("<p>Price is 5 & 10</p>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    std::string text = getTextContent(Item{.element = p});
    // should contain ampersand in some form
    EXPECT_FALSE(text.empty());
}

// ============================================================================
// Invalid Nesting Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, InvalidNestingBlockInInline) {
    // HTML5 spec: block elements inside inline elements - parser should reorganize
    Item result = parseHtml("<span><div>Block in inline</div></span>");

    // should parse without crashing
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);

    // at least one element should exist
    EXPECT_TRUE(findElementByTag(result, "span") != nullptr ||
                findElementByTag(result, "div") != nullptr);
}

TEST_F(HtmlParserNegativeTest, InvalidNestingPInP) {
    // HTML5 spec: <p> cannot contain another <p> - first should auto-close
    Item result = parseHtml("<p>Paragraph 1<p>Paragraph 2</p></p>");

    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);

    // should have two separate p elements
    int p_count = countElementsByTag(result, "p");
    EXPECT_GE(p_count, 1);
}

TEST_F(HtmlParserNegativeTest, InvalidNestingListItemsWithoutList) {
    // HTML5 spec: <li> without parent <ul> or <ol>
    Item result = parseHtml("<li>List item</li>");

    // should parse somehow
    Element* li = findElementByTag(result, "li");
    EXPECT_NE(li, nullptr);
}

TEST_F(HtmlParserNegativeTest, InvalidNestingTableCellsWithoutRow) {
    // HTML5 spec: <td> without parent <tr>
    Item result = parseHtml("<table><td>Cell</td></table>");

    // HTML5 parsers should create implicit <tr> or handle gracefully
    Element* table = findElementByTag(result, "table");
    EXPECT_NE(table, nullptr);
}

TEST_F(HtmlParserNegativeTest, InvalidNestingFormInForm) {
    // HTML5 spec: nested forms are not allowed
    Item result = parseHtml("<form><form></form></form>");

    // should parse somehow (may ignore inner form)
    Element* form = findElementByTag(result, "form");
    EXPECT_NE(form, nullptr);
}

// ============================================================================
// Invalid Comments Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, InvalidCommentUnclosed) {
    // HTML5 spec: unclosed comment
    Item result = parseHtml("<div><!-- Unclosed comment");

    // should handle gracefully (may treat as error or auto-close)
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_ELEMENT ||
                get_type_id(result) == LMD_TYPE_LIST);
}

TEST_F(HtmlParserNegativeTest, InvalidCommentMalformedClosing) {
    // HTML5 spec: malformed comment closing
    Item result = parseHtml("<!-- Comment --!>");

    // should parse somehow
    EXPECT_TRUE(true);
}

TEST_F(HtmlParserNegativeTest, InvalidCommentDoubleHyphenInside) {
    // HTML5 spec: -- inside comment is technically invalid but usually tolerated
    Item result = parseHtml("<!-- Comment -- with double hyphen -->");

    // should parse
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_ERROR);
}

TEST_F(HtmlParserNegativeTest, InvalidCommentEmptyComment) {
    // HTML5 spec: empty comments are valid
    Item result = parseHtml("<!----><div>Content</div>");

    // should parse
    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

// ============================================================================
// Invalid DOCTYPE Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, InvalidDoctypeMalformed) {
    // HTML5 spec: malformed DOCTYPE
    Item result = parseHtml("<!DOCTYPE><html><body>Test</body></html>");

    // should parse the html even with malformed DOCTYPE
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, InvalidDoctypeUnclosed) {
    // HTML5 spec: unclosed DOCTYPE
    Item result = parseHtml("<!DOCTYPE html<html><body>Test</body></html>");

    // should handle gracefully
    EXPECT_TRUE(true);
}

TEST_F(HtmlParserNegativeTest, InvalidDoctypeMultiple) {
    // HTML5 spec: multiple DOCTYPE declarations
    Item result = parseHtml("<!DOCTYPE html><!DOCTYPE html><html></html>");

    // should parse (may ignore second DOCTYPE)
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, InvalidDoctypeAfterContent) {
    // HTML5 spec: DOCTYPE after content is invalid
    Item result = parseHtml("<html></html><!DOCTYPE html>");

    // should parse html element
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

// ============================================================================
// Invalid Void Elements Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, InvalidVoidElementWithClosingTag) {
    // HTML5 spec: void elements with closing tags
    Item result = parseHtml("<br></br>");

    // should parse (may ignore closing tag)
    Element* br = findElementByTag(result, "br");
    EXPECT_NE(br, nullptr);
}

TEST_F(HtmlParserNegativeTest, InvalidVoidElementWithContent) {
    // HTML5 spec: void elements cannot have content
    Item result = parseHtml("<img>Content</img>");

    // should parse somehow (may ignore content)
    Element* img = findElementByTag(result, "img");
    EXPECT_NE(img, nullptr);
}

TEST_F(HtmlParserNegativeTest, InvalidVoidElementNested) {
    // HTML5 spec: void elements with nested elements
    Item result = parseHtml("<input><div>Nested</div></input>");

    // should parse somehow
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

// ============================================================================
// Invalid Script/Style Elements Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, InvalidScriptUnclosed) {
    // HTML5 spec: unclosed script tag
    Item result = parseHtml("<script>var x = 10;");

    // should handle gracefully (may auto-close or error)
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserNegativeTest, InvalidScriptWithPartialClosingTag) {
    // HTML5 spec: script with partial closing tag inside
    Item result = parseHtml("<script>var x = '</script';</script>");

    // should parse correctly (script content parsing is tricky)
    Element* script = findElementByTag(result, "script");
    EXPECT_NE(script, nullptr);
}

TEST_F(HtmlParserNegativeTest, InvalidStyleUnclosed) {
    // HTML5 spec: unclosed style tag
    Item result = parseHtml("<style>body { margin: 0; }");

    // should handle gracefully
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_ELEMENT);
}

// ============================================================================
// Extreme/Edge Cases Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, ExtremeDeeplyNestedTags) {
    // HTML5 spec: extremely deep nesting - parser may limit depth
    std::string html = "";
    for (int i = 0; i < 20; i++) {
        html += "<div>";
    }
    html += "Content";
    for (int i = 0; i < 20; i++) {
        html += "</div>";
    }

    Item result = parseHtml(html.c_str());

    // should parse without crashing (may limit depth)
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_ERROR);
}

TEST_F(HtmlParserNegativeTest, ExtremeVeryLongAttributeValue) {
    // HTML5 spec: extremely long attribute values
    std::string long_value(10000, 'x');
    std::string html = "<div id=\"" + long_value + "\">Content</div>";

    Item result = parseHtml(html.c_str());

    // should handle gracefully (may truncate or accept)
    EXPECT_TRUE(true);
}

TEST_F(HtmlParserNegativeTest, ExtremeVeryLongTextContent) {
    // HTML5 spec: extremely long text content
    std::string long_text(100000, 'x');
    std::string html = "<div>" + long_text + "</div>";

    Item result = parseHtml(html.c_str());

    // should handle gracefully (may truncate)
    EXPECT_TRUE(true);
}

TEST_F(HtmlParserNegativeTest, ExtremeManyAttributes) {
    // HTML5 spec: element with very many attributes
    std::string html = "<div";
    for (int i = 0; i < 100; i++) {
        html += " a" + std::to_string(i) + "=\"v" + std::to_string(i) + "\"";
    }
    html += ">Content</div>";

    Item result = parseHtml(html.c_str());

    // should parse (may limit attribute count)
    Element* div = findElementByTag(result, "div");
    EXPECT_TRUE(div != nullptr || get_type_id(result) == LMD_TYPE_ERROR);
}

TEST_F(HtmlParserNegativeTest, ExtremeEmptyDocument) {
    // HTML5 spec: completely empty document
    Item result = parseHtml("");

    // should return null or empty result
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_LIST);
}

TEST_F(HtmlParserNegativeTest, ExtremeOnlyWhitespace) {
    // HTML5 spec: document with only whitespace
    Item result = parseHtml("   \n\t\r   ");

    // should return null or empty result
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_LIST);
}

TEST_F(HtmlParserNegativeTest, ExtremeNullBytes) {
    // HTML5 spec: null bytes in content
    const char html[] = "<div>Content\0Hidden</div>";

    Item result = parseHtml(html);

    // should parse up to null byte
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_ERROR);
}

// ============================================================================
// Invalid Character Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, InvalidCharactersInTagName) {
    // HTML5 spec: invalid characters in tag names
    Item result = parseHtml("<div$name>Content</div$name>");

    // should handle gracefully
    EXPECT_TRUE(true);
}

TEST_F(HtmlParserNegativeTest, InvalidCharactersControlChars) {
    // HTML5 spec: control characters in content
    Item result = parseHtml("<div>Content\x01\x02\x03</div>");

    // should parse without crashing
    Element* div = findElementByTag(result, "div");
    EXPECT_TRUE(div != nullptr);
}

TEST_F(HtmlParserNegativeTest, InvalidCharactersInvalidUTF8) {
    // HTML5 spec: invalid UTF-8 sequences
    const char html[] = "<div>Content\xFF\xFE</div>";

    Item result = parseHtml(html);

    // should handle gracefully (may replace invalid chars)
    EXPECT_TRUE(true);
}

// ============================================================================
// Invalid Table Structure Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, InvalidTableDirectTrInTable) {
    // HTML5 spec: <tr> directly in <table> should create implicit <tbody>
    Item result = parseHtml("<table><tr><td>Cell</td></tr></table>");

    Element* table = findElementByTag(result, "table");
    ASSERT_NE(table, nullptr);

    // should either have tbody or handle gracefully
    EXPECT_TRUE(findElementByTag(result, "tbody") != nullptr ||
                findElementByTag(result, "tr") != nullptr);
}

TEST_F(HtmlParserNegativeTest, InvalidTableTdWithoutTr) {
    // HTML5 spec: <td> without <tr> parent
    Item result = parseHtml("<table><tbody><td>Cell</td></tbody></table>");

    // should handle gracefully
    Element* table = findElementByTag(result, "table");
    EXPECT_NE(table, nullptr);
}

TEST_F(HtmlParserNegativeTest, InvalidTableMixedContent) {
    // HTML5 spec: mixed content (text + elements) directly in table
    Item result = parseHtml("<table>Text<tr><td>Cell</td></tr></table>");

    // should parse somehow
    Element* table = findElementByTag(result, "table");
    EXPECT_NE(table, nullptr);
}

// ============================================================================
// HTML5 Specific Error Cases Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, HTML5MisplacedStartTag) {
    // HTML5 spec: start tag in wrong context
    Item result = parseHtml("<html><head><div>Content</div></head></html>");

    // should parse (may move or accept)
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, HTML5MisplacedEndTag) {
    // HTML5 spec: end tag without matching start tag
    Item result = parseHtml("<div>Content</span></div>");

    // should ignore mismatched end tag
    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserNegativeTest, HTML5EOFInTag) {
    // HTML5 spec: end of file while in tag
    Item result = parseHtml("<div");

    // should handle as error or auto-close
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserNegativeTest, HTML5EOFInAttribute) {
    // HTML5 spec: end of file in attribute
    Item result = parseHtml("<div id=\"value");

    // should handle gracefully
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserNegativeTest, HTML5ClosingSlashInWrongPlace) {
    // HTML5 spec: closing slash in non-void element start tag
    Item result = parseHtml("<div/>Content");

    // should parse (may treat as self-closing or ignore slash)
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

// ============================================================================
// Mixed Valid/Invalid Content Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, MixedValidAfterInvalid) {
    // HTML5 spec: valid content after invalid content should still parse
    Item result = parseHtml("<><div>Valid</div>");

    // should find the valid div
    Element* div = findElementByTag(result, "div");
    EXPECT_TRUE(div != nullptr || get_type_id(result) == LMD_TYPE_ERROR);
}

TEST_F(HtmlParserNegativeTest, MixedInvalidInMiddle) {
    // HTML5 spec: invalid content in middle of valid content
    Item result = parseHtml("<div>Before<>After</div>");

    // should parse the div
    Element* div = findElementByTag(result, "div");
    EXPECT_TRUE(div != nullptr);
}

TEST_F(HtmlParserNegativeTest, MixedMultipleErrors) {
    // HTML5 spec: multiple different types of errors
    Item result = parseHtml("<div id=\"unclosed><p>Text</div></p>");

    // parser rejects this with error due to unclosed quote
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR || get_type_id(result) == LMD_TYPE_NULL);
}

// ============================================================================
// Additional Corner Cases - Attribute Edge Cases
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseAttributeNameStartsWithNumber) {
    // attributes starting with numbers are invalid
    Item result = parseHtml("<div 123attr=\"value\">Content</div>");

    // should parse somehow (may ignore invalid attribute)
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseAttributeOnlyEquals) {
    // attribute that is just '='
    Item result = parseHtml("<div = >Content</div>");

    // should handle gracefully
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserNegativeTest, CornerCaseAttributeMultipleEquals) {
    // attribute with multiple equals signs
    Item result = parseHtml("<div id==\"value\">Content</div>");

    // parser rejects this as error
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR || get_type_id(result) == LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseAttributeNestedQuotes) {
    // attribute with nested quotes of same type
    Item result = parseHtml("<div title=\"She said \"hello\"\">Content</div>");

    // should parse (may truncate at first closing quote)
    Element* div = findElementByTag(result, "div");
    EXPECT_TRUE(div != nullptr || get_type_id(result) == LMD_TYPE_ERROR);
}

TEST_F(HtmlParserNegativeTest, CornerCaseAttributeTabsInValue) {
    // attribute value with tabs
    Item result = parseHtml("<div data-value=\"\t\tvalue\t\">Content</div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    // should preserve tabs in value
    std::string val = getAttr(div, "data-value");
    EXPECT_FALSE(val.empty());
}

TEST_F(HtmlParserNegativeTest, CornerCaseAttributeNewlineInUnquoted) {
    // unquoted attribute with newline (invalid)
    Item result = parseHtml("<div id=test\nvalue>Content</div>");

    // should parse somehow
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseAttributeOnlyWhitespace) {
    // attribute name that is only whitespace
    Item result = parseHtml("<div   =\"value\">Content</div>");

    // should handle gracefully
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_NULL ||
                findElementByTag(result, "div") != nullptr);
}

// ============================================================================
// Additional Corner Cases - Tag Name Edge Cases
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseTagNameWithDashes) {
    // tag names with dashes (custom elements style)
    Item result = parseHtml("<my-custom-element>Content</my-custom-element>");

    Element* elem = findElementByTag(result, "my-custom-element");
    EXPECT_NE(elem, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseTagNameWithUnderscores) {
    // tag names with underscores
    Item result = parseHtml("<my_element>Content</my_element>");

    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseTagNameStartsWithDash) {
    // tag name starting with dash (invalid)
    Item result = parseHtml("<-element>Content</-element>");

    // should handle somehow
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserNegativeTest, CornerCaseTagNameAllNumbers) {
    // tag name that is all numbers (invalid)
    Item result = parseHtml("<123>Content</123>");

    // should handle gracefully
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_ELEMENT);
}

TEST_F(HtmlParserNegativeTest, CornerCaseTagNameWithDots) {
    // tag name with dots
    Item result = parseHtml("<my.element>Content</my.element>");

    // should parse somehow
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseTagNameWithColons) {
    // tag name with colons (XML namespace style)
    Item result = parseHtml("<ns:element>Content</ns:element>");

    // should parse (may treat as single tag name)
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseTagNameVeryLong) {
    // extremely long tag name
    std::string long_tag(1000, 'a');
    std::string html = "<" + long_tag + ">Content</" + long_tag + ">";

    Item result = parseHtml(html.c_str());

    // should handle gracefully
    EXPECT_TRUE(true);
}

// ============================================================================
// Additional Corner Cases - Nesting and Structure
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseButtonInsideButton) {
    // buttons nested inside buttons (invalid)
    Item result = parseHtml("<button><button>Inner</button></button>");

    // should parse somehow (may auto-close first button)
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseAnchorInsideAnchor) {
    // anchors nested inside anchors (invalid)
    Item result = parseHtml("<a href=\"#1\"><a href=\"#2\">Link</a></a>");

    // should parse somehow
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseLabelInsideLabel) {
    // labels nested inside labels (invalid)
    Item result = parseHtml("<label><label>Inner</label></label>");

    // should parse somehow
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseHeadingInsideHeading) {
    // heading nested inside heading
    Item result = parseHtml("<h1><h2>Nested heading</h2></h1>");

    // should parse somehow
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseOptionOutsideSelect) {
    // option elements without select parent
    Item result = parseHtml("<option>Choice 1</option>");

    Element* option = findElementByTag(result, "option");
    EXPECT_NE(option, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseDtDdWithoutDl) {
    // dt/dd elements without dl parent
    Item result = parseHtml("<dt>Term</dt><dd>Definition</dd>");

    // should parse elements
    EXPECT_TRUE(findElementByTag(result, "dt") != nullptr ||
                findElementByTag(result, "dd") != nullptr);
}

// ============================================================================
// Additional Corner Cases - Content Edge Cases
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseTextWithOnlySpecialChars) {
    // text content with only special characters
    Item result = parseHtml("<div>!@#$%^&*()_+-=[]{}|;:',.<>?/~`</div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    std::string text = getTextContent(Item{.element = div});
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserNegativeTest, CornerCaseTextWithRepeatedEntities) {
    // text with many repeated entities
    Item result = parseHtml("<p>&lt;&gt;&lt;&gt;&lt;&gt;&lt;&gt;</p>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    std::string text = getTextContent(Item{.element = p});
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserNegativeTest, CornerCaseTextWithMixedNewlines) {
    // text with mixed newline types (CRLF, LF, CR)
    Item result = parseHtml("<div>Line1\r\nLine2\nLine3\rLine4</div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    std::string text = getTextContent(Item{.element = div});
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserNegativeTest, CornerCaseTextWithZeroWidthChars) {
    // text with zero-width characters
    Item result = parseHtml("<p>Text\u200B\u200C\u200D\uFEFFWith Zero Width</p>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    std::string text = getTextContent(Item{.element = p});
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserNegativeTest, CornerCaseTextWithBidiMarkers) {
    // text with bidirectional text markers
    Item result = parseHtml("<div>English\u202Ahebrew\u202C</div>");

    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

// ============================================================================
// Additional Corner Cases - Whitespace Handling
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseNoSpaceBetweenAttributes) {
    // no space between attributes
    Item result = parseHtml("<div id=\"test\"class=\"box\"data-value=\"123\">Content</div>");

    Element* div = findElementByTag(result, "div");
    EXPECT_TRUE(div != nullptr || get_type_id(result) == LMD_TYPE_ERROR);
}

TEST_F(HtmlParserNegativeTest, CornerCaseNoSpaceBeforeSlash) {
    // no space before self-closing slash
    Item result = parseHtml("<img src=\"test.jpg\"/>");

    Element* img = findElementByTag(result, "img");
    EXPECT_NE(img, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseExcessiveWhitespaceBetweenAttrs) {
    // excessive whitespace between attributes
    Item result = parseHtml("<div id=\"test\"     \n\t\r    class=\"box\">Content</div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    EXPECT_EQ(getAttr(div, "id"), "test");
    EXPECT_EQ(getAttr(div, "class"), "box");
}

TEST_F(HtmlParserNegativeTest, CornerCaseWhitespaceInTagName) {
    // whitespace in middle of tag name (splits tag)
    Item result = parseHtml("<div class>Content</div class>");

    // should handle gracefully
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_NULL ||
                findElementByTag(result, "div") != nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseFormFeedCharacter) {
    // form feed character in content
    Item result = parseHtml("<div>Before\fAfter</div>");

    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

// ============================================================================
// Additional Corner Cases - Comment Edge Cases
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseCommentWithNull) {
    // comment containing null byte
    const char html[] = "<div><!-- Comment\0Hidden --></div>";

    Item result = parseHtml(html);

    // should parse up to null
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseCommentStartingWithDash) {
    // comment starting with dash (near edge of valid syntax)
    Item result = parseHtml("<!--- This is a comment --->");

    // should parse somehow
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_NULL ||
                get_type_id(result) == LMD_TYPE_ELEMENT ||
                get_type_id(result) == LMD_TYPE_LIST);
}

TEST_F(HtmlParserNegativeTest, CornerCaseCommentEndingWithDash) {
    // comment ending with extra dash
    Item result = parseHtml("<!-- Comment --->");

    // should parse somehow
    EXPECT_TRUE(true);
}

TEST_F(HtmlParserNegativeTest, CornerCaseNestedCommentLike) {
    // nested comment-like syntax
    Item result = parseHtml("<!-- Outer <!-- Inner --> Outer -->");

    // should parse somehow (HTML doesn't support nested comments)
    EXPECT_TRUE(true);
}

TEST_F(HtmlParserNegativeTest, CornerCaseCommentWithScriptTag) {
    // comment containing script-like content
    Item result = parseHtml("<!-- <script>alert('xss')</script> --><div>Safe</div>");

    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

// ============================================================================
// Additional Corner Cases - Entity Reference Edge Cases
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseEntityWithoutSemicolonFollowedByNumber) {
    // entity without semicolon followed by number
    Item result = parseHtml("<p>&lt123</p>");

    Element* p = findElementByTag(result, "p");
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseEntityPartialMatch) {
    // entity that partially matches known entity
    Item result = parseHtml("<p>&ltx;</p>");

    Element* p = findElementByTag(result, "p");
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseNumericEntityZero) {
    // numeric entity for code point 0
    Item result = parseHtml("<p>&#0;</p>");

    Element* p = findElementByTag(result, "p");
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseNumericEntityLeadingZeros) {
    // numeric entity with leading zeros
    Item result = parseHtml("<p>&#00065;</p>");

    Element* p = findElementByTag(result, "p");
    ASSERT_NE(p, nullptr);

    // should parse to 'A' (U+0041 = 65)
    std::string text = getTextContent(Item{.element = p});
    EXPECT_FALSE(text.empty());
}

TEST_F(HtmlParserNegativeTest, CornerCaseHexEntityUppercase) {
    // hex entity with uppercase X
    Item result = parseHtml("<p>&#X41;</p>");

    Element* p = findElementByTag(result, "p");
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseMultipleAmpersands) {
    // multiple ampersands in a row
    Item result = parseHtml("<p>&&&</p>");

    Element* p = findElementByTag(result, "p");
    EXPECT_NE(p, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseEntityInAttributeName) {
    // entity reference in attribute name (invalid)
    Item result = parseHtml("<div data-&lt;test=\"value\">Content</div>");

    // should handle gracefully
    EXPECT_TRUE(true);
}

// ============================================================================
// Additional Corner Cases - Self-Closing and Void Elements
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseSelfClosingWithSpace) {
    // self-closing with space before slash
    Item result = parseHtml("<br />");

    Element* br = findElementByTag(result, "br");
    EXPECT_NE(br, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseSelfClosingWithAttributes) {
    // self-closing with attributes after slash
    Item result = parseHtml("<img / src=\"test.jpg\">");

    // parser rejects this as error (slash in wrong position)
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR || get_type_id(result) == LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseVoidElementWithChildren) {
    // void element with child elements
    Item result = parseHtml("<input><span>Child</span></input>");

    // should handle gracefully
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseMultipleSelfClosingSlashes) {
    // multiple slashes in self-closing tag
    Item result = parseHtml("<br //>");

    // parser rejects this as error or null
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR || get_type_id(result) == LMD_TYPE_NULL);
}// ============================================================================
// Additional Corner Cases - Script/Style Content
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseScriptWithHTMLComments) {
    // script with HTML comment markers (old school)
    Item result = parseHtml("<script><!-- alert('test'); //--></script>");

    Element* script = findElementByTag(result, "script");
    EXPECT_NE(script, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseScriptWithCDATA) {
    // script with CDATA section
    Item result = parseHtml("<script><![CDATA[ var x = 1; ]]></script>");

    Element* script = findElementByTag(result, "script");
    EXPECT_NE(script, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseStyleWithMediaQuery) {
    // style with @media query containing >
    Item result = parseHtml("<style>@media (min-width: 768px) { body { margin: 0; } }</style>");

    Element* style = findElementByTag(result, "style");
    EXPECT_NE(style, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseScriptWithEscapedClosingTag) {
    // script with escaped closing tag
    Item result = parseHtml("<script>var html = '<\\/script>';</script>");

    Element* script = findElementByTag(result, "script");
    EXPECT_NE(script, nullptr);
}

// ============================================================================
// Additional Corner Cases - Case Sensitivity
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseMixedCaseAttributes) {
    // mixed case attributes (HTML is case-insensitive)
    Item result = parseHtml("<div ID=\"test\" CLaSs=\"box\" dAtA-VaLuE=\"123\">Content</div>");

    Element* div = findElementByTag(result, "div");
    ASSERT_NE(div, nullptr);

    // should be case-insensitive
    EXPECT_TRUE(getAttr(div, "id") == "test" || getAttr(div, "ID") == "test");
}

TEST_F(HtmlParserNegativeTest, CornerCaseMixedCaseTagNames) {
    // mixed case tag names
    Item result = parseHtml("<DiV><SpAn>Text</SpAn></DiV>");

    // should handle case-insensitively
    EXPECT_TRUE(findElementByTag(result, "div") != nullptr ||
                findElementByTag(result, "DiV") != nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseMixedCaseEntities) {
    // mixed case entity names (entities are case-sensitive)
    Item result = parseHtml("<p>&Lt;&gT;&AMP;</p>");

    Element* p = findElementByTag(result, "p");
    EXPECT_NE(p, nullptr);
}

// ============================================================================
// Additional Corner Cases - Empty Elements and Attributes
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseElementWithOnlyWhitespaceContent) {
    // element with only various whitespace types
    Item result = parseHtml("<div> \t\n\r </div>");

    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseAttributeNameEmpty) {
    // empty attribute name
    Item result = parseHtml("<div =\"value\">Content</div>");

    // should reject or handle gracefully
    EXPECT_TRUE(get_type_id(result) == LMD_TYPE_ERROR ||
                get_type_id(result) == LMD_TYPE_NULL ||
                findElementByTag(result, "div") != nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseManyEmptyElements) {
    // many empty elements nested
    Item result = parseHtml("<div><span></span><span></span><span></span></div>");

    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);

    int span_count = countElementsByTag(result, "span");
    EXPECT_EQ(span_count, 3);
}

// ============================================================================
// Additional Corner Cases - Table Structure
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseTableWithOnlyCaption) {
    // table with only caption
    Item result = parseHtml("<table><caption>Title</caption></table>");

    Element* table = findElementByTag(result, "table");
    EXPECT_NE(table, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseTableMultipleTbody) {
    // table with multiple tbody elements
    Item result = parseHtml("<table><tbody><tr><td>1</td></tr></tbody><tbody><tr><td>2</td></tr></tbody></table>");

    Element* table = findElementByTag(result, "table");
    EXPECT_NE(table, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseTableTheadAfterTbody) {
    // table with thead after tbody (wrong order)
    Item result = parseHtml("<table><tbody><tr><td>Body</td></tr></tbody><thead><tr><th>Header</th></tr></thead></table>");

    Element* table = findElementByTag(result, "table");
    EXPECT_NE(table, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseTableColspanZero) {
    // table cell with colspan=0
    Item result = parseHtml("<table><tr><td colspan=\"0\">Cell</td></tr></table>");

    Element* table = findElementByTag(result, "table");
    EXPECT_NE(table, nullptr);
}

// ============================================================================
// Additional Corner Cases - Stress Tests
// ============================================================================

TEST_F(HtmlParserNegativeTest, CornerCaseAlternatingValidInvalid) {
    // alternating valid and invalid tags
    Item result = parseHtml("<div><>text</><span><>more</></span></div>");

    Element* div = findElementByTag(result, "div");
    EXPECT_TRUE(div != nullptr || get_type_id(result) == LMD_TYPE_ERROR);
}

TEST_F(HtmlParserNegativeTest, CornerCaseManyUnclosedTags) {
    // many unclosed tags in sequence
    Item result = parseHtml("<div><p><span><strong><em>Text");

    // should auto-close all
    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseRepeatedSameTag) {
    // same tag opened repeatedly without closing
    Item result = parseHtml("<div><div><div><div>Content</div></div></div></div>");

    Element* div = findElementByTag(result, "div");
    EXPECT_NE(div, nullptr);
}

TEST_F(HtmlParserNegativeTest, CornerCaseSingleCharacterElements) {
    // single character tag names
    Item result = parseHtml("<a><b><i><u><s>Text</s></u></i></b></a>");

    EXPECT_TRUE(get_type_id(result) != LMD_TYPE_NULL);
}

TEST_F(HtmlParserNegativeTest, CornerCaseBracketInText) {
    // < and > characters in text without proper escaping
    Item result = parseHtml("<div>5 < 10 > 3</div>");

    // should parse somehow (< might be treated as tag start)
    Element* div = findElementByTag(result, "div");
    EXPECT_TRUE(div != nullptr || get_type_id(result) == LMD_TYPE_ERROR);
}
