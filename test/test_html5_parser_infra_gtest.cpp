#include <gtest/gtest.h>
#include "../lambda/input/html5_parser.h"
#include "../lambda/input/input.h"
#include "../lib/mempool.h"

class Html5ParserInfraTest : public ::testing::Test {
protected:
    Pool* pool;
    Input* input;

    void SetUp() override {
        pool = pool_create();  // Create pool
        ASSERT_NE(pool, nullptr);

        input = (Input*)pool_calloc(pool, sizeof(Input));
        ASSERT_NE(input, nullptr);
        input->pool = pool;
        input->sb = stringbuf_new(pool);
        input->type_list = arraylist_new(16);  // Initialize type list
        input->root = {.item = ITEM_NULL};
    }

    void TearDown() override {
        if (pool) {
            pool_destroy(pool);
        }
    }

    Element* createElement(const char* tag_name) {
        return input_create_element(input, tag_name);
    }
};

// ============================================================================
// Stack Tests
// ============================================================================

TEST_F(Html5ParserInfraTest, StackCreate) {
    Html5Stack* stack = html5_stack_create(pool);
    ASSERT_NE(stack, nullptr);
    EXPECT_TRUE(html5_stack_is_empty(stack));
    EXPECT_EQ(html5_stack_size(stack), (size_t)0);
}

TEST_F(Html5ParserInfraTest, StackPushPop) {
    Html5Stack* stack = html5_stack_create(pool);

    Element* div = createElement("div");
    Element* span = createElement("span");
    Element* p = createElement("p");

    html5_stack_push(stack, div);
    EXPECT_EQ(html5_stack_size(stack), (size_t)1);
    EXPECT_EQ(html5_stack_peek(stack), div);

    html5_stack_push(stack, span);
    EXPECT_EQ(html5_stack_size(stack), (size_t)2);
    EXPECT_EQ(html5_stack_peek(stack), span);

    html5_stack_push(stack, p);
    EXPECT_EQ(html5_stack_size(stack), (size_t)3);
    EXPECT_EQ(html5_stack_peek(stack), p);

    // Pop in LIFO order
    EXPECT_EQ(html5_stack_pop(stack), p);
    EXPECT_EQ(html5_stack_size(stack), (size_t)2);

    EXPECT_EQ(html5_stack_pop(stack), span);
    EXPECT_EQ(html5_stack_size(stack), (size_t)1);

    EXPECT_EQ(html5_stack_pop(stack), div);
    EXPECT_EQ(html5_stack_size(stack), (size_t)0);
    EXPECT_TRUE(html5_stack_is_empty(stack));
}

TEST_F(Html5ParserInfraTest, StackPeekAt) {
    Html5Stack* stack = html5_stack_create(pool);

    Element* div = createElement("div");
    Element* span = createElement("span");
    Element* p = createElement("p");

    html5_stack_push(stack, div);
    html5_stack_push(stack, span);
    html5_stack_push(stack, p);

    EXPECT_EQ(html5_stack_peek_at(stack, 0), p);    // top
    EXPECT_EQ(html5_stack_peek_at(stack, 1), span); // middle
    EXPECT_EQ(html5_stack_peek_at(stack, 2), div);  // bottom
    EXPECT_EQ(html5_stack_peek_at(stack, 3), nullptr); // out of range
}

TEST_F(Html5ParserInfraTest, StackContains) {
    Html5Stack* stack = html5_stack_create(pool);

    html5_stack_push(stack, createElement("div"));
    html5_stack_push(stack, createElement("span"));
    html5_stack_push(stack, createElement("p"));

    EXPECT_TRUE(html5_stack_contains(stack, "div"));
    EXPECT_TRUE(html5_stack_contains(stack, "span"));
    EXPECT_TRUE(html5_stack_contains(stack, "p"));
    EXPECT_FALSE(html5_stack_contains(stack, "table"));

    // Test case-insensitive
    EXPECT_TRUE(html5_stack_contains(stack, "DIV"));
    EXPECT_TRUE(html5_stack_contains(stack, "Span"));
}

TEST_F(Html5ParserInfraTest, StackFind) {
    Html5Stack* stack = html5_stack_create(pool);

    Element* div = createElement("div");
    Element* span = createElement("span");

    html5_stack_push(stack, div);
    html5_stack_push(stack, span);

    EXPECT_EQ(html5_stack_find(stack, "div"), div);
    EXPECT_EQ(html5_stack_find(stack, "span"), span);
    EXPECT_EQ(html5_stack_find(stack, "table"), nullptr);
}

TEST_F(Html5ParserInfraTest, StackPopUntil) {
    Html5Stack* stack = html5_stack_create(pool);

    html5_stack_push(stack, createElement("div"));
    html5_stack_push(stack, createElement("span"));
    html5_stack_push(stack, createElement("p"));
    html5_stack_push(stack, createElement("a"));

    EXPECT_EQ(html5_stack_size(stack), (size_t)4);

    html5_stack_pop_until(stack, "span");

    // Should have popped: a, p, span (leaving only div)
    EXPECT_EQ(html5_stack_size(stack), (size_t)1);
    EXPECT_TRUE(html5_stack_contains(stack, "div"));
    EXPECT_FALSE(html5_stack_contains(stack, "span"));
}

TEST_F(Html5ParserInfraTest, StackRemove) {
    Html5Stack* stack = html5_stack_create(pool);

    Element* div = createElement("div");
    Element* span = createElement("span");
    Element* p = createElement("p");

    html5_stack_push(stack, div);
    html5_stack_push(stack, span);
    html5_stack_push(stack, p);

    EXPECT_EQ(html5_stack_size(stack), (size_t)3);

    // Remove middle element
    html5_stack_remove(stack, span);

    EXPECT_EQ(html5_stack_size(stack), (size_t)2);
    EXPECT_TRUE(html5_stack_contains(stack, "div"));
    EXPECT_FALSE(html5_stack_contains(stack, "span"));
    EXPECT_TRUE(html5_stack_contains(stack, "p"));
}

TEST_F(Html5ParserInfraTest, StackClear) {
    Html5Stack* stack = html5_stack_create(pool);

    html5_stack_push(stack, createElement("div"));
    html5_stack_push(stack, createElement("span"));
    html5_stack_push(stack, createElement("p"));

    EXPECT_EQ(html5_stack_size(stack), (size_t)3);

    html5_stack_clear(stack);

    EXPECT_EQ(html5_stack_size(stack), (size_t)0);
    EXPECT_TRUE(html5_stack_is_empty(stack));
}

// ============================================================================
// Formatting List Tests
// ============================================================================

TEST_F(Html5ParserInfraTest, FormattingListCreate) {
    Html5FormattingList* list = html5_formatting_list_create(pool);
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->size, (size_t)0);
}

TEST_F(Html5ParserInfraTest, FormattingListPushPop) {
    Html5FormattingList* list = html5_formatting_list_create(pool);

    Element* b = createElement("b");
    Element* i = createElement("i");
    Element* u = createElement("u");

    html5_formatting_list_push(list, b);
    EXPECT_EQ(list->size, (size_t)1);

    html5_formatting_list_push(list, i);
    EXPECT_EQ(list->size, (size_t)2);

    html5_formatting_list_push(list, u);
    EXPECT_EQ(list->size, (size_t)3);

    // Pop in LIFO order
    EXPECT_EQ(html5_formatting_list_pop(list), u);
    EXPECT_EQ(list->size, (size_t)2);

    EXPECT_EQ(html5_formatting_list_pop(list), i);
    EXPECT_EQ(list->size, (size_t)1);

    EXPECT_EQ(html5_formatting_list_pop(list), b);
    EXPECT_EQ(list->size, (size_t)0);
}

TEST_F(Html5ParserInfraTest, FormattingListMarker) {
    Html5FormattingList* list = html5_formatting_list_create(pool);

    Element* b = createElement("b");
    Element* i = createElement("i");

    html5_formatting_list_push(list, b);
    html5_formatting_list_push_marker(list);
    html5_formatting_list_push(list, i);

    EXPECT_EQ(list->size, (size_t)3);

    // Pop until marker
    html5_formatting_list_clear_to_marker(list);

    // Should have removed i and marker, leaving b
    EXPECT_EQ(list->size, (size_t)1);
    EXPECT_TRUE(html5_formatting_list_contains(list, "b"));
    EXPECT_FALSE(html5_formatting_list_contains(list, "i"));
}

TEST_F(Html5ParserInfraTest, FormattingListContains) {
    Html5FormattingList* list = html5_formatting_list_create(pool);

    html5_formatting_list_push(list, createElement("b"));
    html5_formatting_list_push(list, createElement("i"));
    html5_formatting_list_push(list, createElement("u"));

    EXPECT_TRUE(html5_formatting_list_contains(list, "b"));
    EXPECT_TRUE(html5_formatting_list_contains(list, "i"));
    EXPECT_TRUE(html5_formatting_list_contains(list, "u"));
    EXPECT_FALSE(html5_formatting_list_contains(list, "strong"));
}

TEST_F(Html5ParserInfraTest, FormattingListFind) {
    Html5FormattingList* list = html5_formatting_list_create(pool);

    Element* b = createElement("b");
    Element* i = createElement("i");

    html5_formatting_list_push(list, b);
    html5_formatting_list_push(list, i);

    EXPECT_EQ(html5_formatting_list_find(list, "b"), b);
    EXPECT_EQ(html5_formatting_list_find(list, "i"), i);
    EXPECT_EQ(html5_formatting_list_find(list, "u"), nullptr);
}

TEST_F(Html5ParserInfraTest, FormattingListRemove) {
    Html5FormattingList* list = html5_formatting_list_create(pool);

    Element* b = createElement("b");
    Element* i = createElement("i");
    Element* u = createElement("u");

    html5_formatting_list_push(list, b);
    html5_formatting_list_push(list, i);
    html5_formatting_list_push(list, u);

    EXPECT_EQ(list->size, (size_t)3);

    html5_formatting_list_remove(list, i);

    EXPECT_EQ(list->size, (size_t)2);
    EXPECT_TRUE(html5_formatting_list_contains(list, "b"));
    EXPECT_FALSE(html5_formatting_list_contains(list, "i"));
    EXPECT_TRUE(html5_formatting_list_contains(list, "u"));
}

TEST_F(Html5ParserInfraTest, FormattingListReplace) {
    Html5FormattingList* list = html5_formatting_list_create(pool);

    Element* b = createElement("b");
    Element* i = createElement("i");
    Element* strong = createElement("strong");

    html5_formatting_list_push(list, b);
    html5_formatting_list_push(list, i);

    html5_formatting_list_replace(list, b, strong);

    EXPECT_FALSE(html5_formatting_list_contains(list, "b"));
    EXPECT_TRUE(html5_formatting_list_contains(list, "strong"));
    EXPECT_TRUE(html5_formatting_list_contains(list, "i"));
    EXPECT_EQ(html5_formatting_list_find(list, "strong"), strong);
}

// ============================================================================
// Parser Tests
// ============================================================================

TEST_F(Html5ParserInfraTest, ParserCreate) {
    const char* html = "<html><body><p>Hello</p></body></html>";
    Html5Parser* parser = html5_parser_create(input, html, pool);

    ASSERT_NE(parser, nullptr);
    EXPECT_EQ(parser->insertion_mode, HTML5_MODE_INITIAL);
    EXPECT_EQ(parser->quirks_mode, QUIRKS_MODE_NO_QUIRKS);
    EXPECT_TRUE(parser->scripting_enabled);
    EXPECT_FALSE(parser->foster_parenting);
    EXPECT_TRUE(parser->frameset_ok);
    EXPECT_EQ(parser->error_count, (size_t)0);

    ASSERT_NE(parser->open_elements, nullptr);
    ASSERT_NE(parser->active_formatting_elements, nullptr);
    ASSERT_NE(parser->template_insertion_modes, nullptr);
}

TEST_F(Html5ParserInfraTest, ParserSetMode) {
    const char* html = "<html></html>";
    Html5Parser* parser = html5_parser_create(input, html, pool);

    EXPECT_EQ(parser->insertion_mode, HTML5_MODE_INITIAL);

    html5_parser_set_mode(parser, HTML5_MODE_BEFORE_HTML);
    EXPECT_EQ(parser->insertion_mode, HTML5_MODE_BEFORE_HTML);

    html5_parser_set_mode(parser, HTML5_MODE_IN_BODY);
    EXPECT_EQ(parser->insertion_mode, HTML5_MODE_IN_BODY);
}

TEST_F(Html5ParserInfraTest, ParserModeName) {
    EXPECT_STREQ(html5_mode_name(HTML5_MODE_INITIAL), "initial");
    EXPECT_STREQ(html5_mode_name(HTML5_MODE_BEFORE_HTML), "before html");
    EXPECT_STREQ(html5_mode_name(HTML5_MODE_IN_BODY), "in body");
    EXPECT_STREQ(html5_mode_name(HTML5_MODE_IN_TABLE), "in table");
    EXPECT_STREQ(html5_mode_name(HTML5_MODE_AFTER_AFTER_BODY), "after after body");
}

TEST_F(Html5ParserInfraTest, ParserError) {
    const char* html = "<html></html>";
    Html5Parser* parser = html5_parser_create(input, html, pool);

    EXPECT_EQ(parser->error_count, (size_t)0);

    html5_parser_error(parser, "unexpected-token", "Unexpected token found");
    EXPECT_EQ(parser->error_count, (size_t)1);

    html5_parser_error(parser, "eof-in-tag", "EOF in tag");
    EXPECT_EQ(parser->error_count, (size_t)2);

    // Check error details
    ASSERT_NE(parser->errors, nullptr);
    EXPECT_STREQ(parser->errors->error_code, "eof-in-tag");
    EXPECT_STREQ(parser->errors->message, "EOF in tag");
}

// ============================================================================
// Scope Tests
// ============================================================================

TEST_F(Html5ParserInfraTest, ScopeBasic) {
    const char* html = "";
    Html5Parser* parser = html5_parser_create(input, html, pool);

    html5_stack_push(parser->open_elements, createElement("html"));
    html5_stack_push(parser->open_elements, createElement("body"));
    html5_stack_push(parser->open_elements, createElement("div"));
    html5_stack_push(parser->open_elements, createElement("p"));

    EXPECT_TRUE(html5_has_element_in_scope(parser, "p"));
    EXPECT_TRUE(html5_has_element_in_scope(parser, "div"));
    EXPECT_TRUE(html5_has_element_in_scope(parser, "body"));
    EXPECT_FALSE(html5_has_element_in_scope(parser, "span"));
}

TEST_F(Html5ParserInfraTest, ScopeBoundary) {
    const char* html = "";
    Html5Parser* parser = html5_parser_create(input, html, pool);

    html5_stack_push(parser->open_elements, createElement("html"));
    html5_stack_push(parser->open_elements, createElement("body"));
    html5_stack_push(parser->open_elements, createElement("p"));
    html5_stack_push(parser->open_elements, createElement("table")); // scope boundary
    html5_stack_push(parser->open_elements, createElement("tr"));
    html5_stack_push(parser->open_elements, createElement("td"));

    // td is in scope
    EXPECT_TRUE(html5_has_element_in_scope(parser, "td"));
    // tr is in scope
    EXPECT_TRUE(html5_has_element_in_scope(parser, "tr"));
    // table is in scope (it's a boundary itself)
    EXPECT_TRUE(html5_has_element_in_scope(parser, "table"));
    // p is NOT in scope (blocked by table)
    EXPECT_FALSE(html5_has_element_in_scope(parser, "p"));
    // body is NOT in scope (blocked by table)
    EXPECT_FALSE(html5_has_element_in_scope(parser, "body"));
}

TEST_F(Html5ParserInfraTest, ButtonScope) {
    const char* html = "";
    Html5Parser* parser = html5_parser_create(input, html, pool);

    html5_stack_push(parser->open_elements, createElement("html"));
    html5_stack_push(parser->open_elements, createElement("body"));
    html5_stack_push(parser->open_elements, createElement("p"));
    html5_stack_push(parser->open_elements, createElement("button")); // button scope boundary
    html5_stack_push(parser->open_elements, createElement("span"));

    // span is in button scope
    EXPECT_TRUE(html5_has_element_in_button_scope(parser, "span"));
    // button is in button scope
    EXPECT_TRUE(html5_has_element_in_button_scope(parser, "button"));
    // p is NOT in button scope (blocked by button)
    EXPECT_FALSE(html5_has_element_in_button_scope(parser, "p"));
}

TEST_F(Html5ParserInfraTest, TableScope) {
    const char* html = "";
    Html5Parser* parser = html5_parser_create(input, html, pool);

    html5_stack_push(parser->open_elements, createElement("html"));
    html5_stack_push(parser->open_elements, createElement("body"));
    html5_stack_push(parser->open_elements, createElement("table"));
    html5_stack_push(parser->open_elements, createElement("tbody"));
    html5_stack_push(parser->open_elements, createElement("tr"));
    html5_stack_push(parser->open_elements, createElement("td"));

    // In table scope, only html, table, and template are boundaries
    EXPECT_TRUE(html5_has_element_in_table_scope(parser, "td"));
    EXPECT_TRUE(html5_has_element_in_table_scope(parser, "tr"));
    EXPECT_TRUE(html5_has_element_in_table_scope(parser, "tbody"));
    EXPECT_TRUE(html5_has_element_in_table_scope(parser, "table"));
    // body is NOT in table scope (blocked by table)
    EXPECT_FALSE(html5_has_element_in_table_scope(parser, "body"));
}

TEST_F(Html5ParserInfraTest, SelectScope) {
    const char* html = "";
    Html5Parser* parser = html5_parser_create(input, html, pool);

    html5_stack_push(parser->open_elements, createElement("html"));
    html5_stack_push(parser->open_elements, createElement("body"));
    html5_stack_push(parser->open_elements, createElement("select"));
    html5_stack_push(parser->open_elements, createElement("optgroup"));
    html5_stack_push(parser->open_elements, createElement("option"));

    // In select scope, everything except optgroup and option are boundaries
    EXPECT_TRUE(html5_has_element_in_select_scope(parser, "option"));
    EXPECT_TRUE(html5_has_element_in_select_scope(parser, "optgroup"));
    EXPECT_TRUE(html5_has_element_in_select_scope(parser, "select"));
    // body is NOT in select scope (it's a boundary)
    EXPECT_FALSE(html5_has_element_in_select_scope(parser, "body"));
}
