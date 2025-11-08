#include <gtest/gtest.h>

#include "../../../lambda/input/css/dom_element.hpp"

extern "C" {
#include "../../../lib/strbuf.h"
#include "../../../lib/mempool.h"
#include "../../../lib/log.h"
}

// Test for DOM element printing functionality
class DomElementPrintTest : public ::testing::Test {
protected:
    Pool* pool;
    StrBuf* buffer;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        buffer = strbuf_new();
        ASSERT_NE(buffer, nullptr);
    }

    void TearDown() override {
        if (buffer) {
            strbuf_free(buffer);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }
};

TEST_F(DomElementPrintTest, PrintDivWithId) {
    // Create div element with ID
    DomElement* div = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(div, nullptr);

    // Set ID attribute
    dom_element_set_attribute(div, "id", "test-id");

    // Print the element
    div->print(buffer, 0);

    // Check the output contains the ID
    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(strstr(result, "id=\"test-id\"") != nullptr);
}

TEST_F(DomElementPrintTest, PrintDivWithClass) {
    // Create div element with class
    DomElement* div = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(div, nullptr);

    // Add class
    dom_element_add_class(div, "test-class");

    // Print the element
    div->print(buffer, 0);

    // Check the output contains the class
    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(strstr(result, "class=\"test-class\"") != nullptr);
}

TEST_F(DomElementPrintTest, PrintNestedElements) {
    // Create parent div
    DomElement* div = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(div, nullptr);

    // Create child span
    DomElement* span = dom_element_create(pool, "span", nullptr);
    ASSERT_NE(span, nullptr);

    // Append child (only works for DomElement to DomElement)
    ASSERT_TRUE(dom_element_append_child(div, span));

    // Print with indentation
    div->print(buffer, 2);

    // Check output structure
    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(strstr(result, "<div>") != nullptr);
    EXPECT_TRUE(strstr(result, "<span>") != nullptr);
    EXPECT_TRUE(strstr(result, "</span>") != nullptr);
    EXPECT_TRUE(strstr(result, "</div>") != nullptr);
}

TEST_F(DomElementPrintTest, PrintWithIndentation) {
    // Create simple element
    DomElement* p = dom_element_create(pool, "p", nullptr);
    ASSERT_NE(p, nullptr);

    // Print with indentation level 3
    p->print(buffer, 3);

    // Check that indentation is applied
    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);

    // Should start with 3 spaces (indentation level 3 = 3 spaces)
    EXPECT_TRUE(strncmp(result, "   <p>", 6) == 0);
}

TEST_F(DomElementPrintTest, PrintComplexHTMLDocument) {
    // Create a structure similar to the background-001.html test case
    DomElement* html = dom_element_create(pool, "html", nullptr);
    ASSERT_NE(html, nullptr);

    DomElement* head = dom_element_create(pool, "head", nullptr);
    ASSERT_NE(head, nullptr);

    DomElement* title = dom_element_create(pool, "title", nullptr);
    ASSERT_NE(title, nullptr);

    DomElement* link1 = dom_element_create(pool, "link", nullptr);
    ASSERT_NE(link1, nullptr);
    dom_element_set_attribute(link1, "rel", "author");
    dom_element_set_attribute(link1, "title", "Microsoft");
    dom_element_set_attribute(link1, "href", "http://www.microsoft.com/");

    DomElement* link2 = dom_element_create(pool, "link", nullptr);
    ASSERT_NE(link2, nullptr);
    dom_element_set_attribute(link2, "rel", "help");
    dom_element_set_attribute(link2, "href", "http://www.w3.org/TR/CSS21/colors.html#propdef-background");

    DomElement* meta1 = dom_element_create(pool, "meta", nullptr);
    ASSERT_NE(meta1, nullptr);
    dom_element_set_attribute(meta1, "name", "flags");
    dom_element_set_attribute(meta1, "content", "");

    DomElement* meta2 = dom_element_create(pool, "meta", nullptr);
    ASSERT_NE(meta2, nullptr);
    dom_element_set_attribute(meta2, "name", "assert");
    dom_element_set_attribute(meta2, "content", "Background with color only sets the background of the element to the color specified.");

    DomElement* style = dom_element_create(pool, "style", nullptr);
    ASSERT_NE(style, nullptr);
    dom_element_set_attribute(style, "type", "text/css");

    DomElement* body = dom_element_create(pool, "body", nullptr);
    ASSERT_NE(body, nullptr);

    DomElement* p = dom_element_create(pool, "p", nullptr);
    ASSERT_NE(p, nullptr);

    DomElement* div = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(div, nullptr);

    // Build the structure
    ASSERT_TRUE(dom_element_append_child(head, title));
    ASSERT_TRUE(dom_element_append_child(head, link1));
    ASSERT_TRUE(dom_element_append_child(head, link2));
    ASSERT_TRUE(dom_element_append_child(head, meta1));
    ASSERT_TRUE(dom_element_append_child(head, meta2));
    ASSERT_TRUE(dom_element_append_child(head, style));

    ASSERT_TRUE(dom_element_append_child(body, p));
    ASSERT_TRUE(dom_element_append_child(body, div));

    ASSERT_TRUE(dom_element_append_child(html, head));
    ASSERT_TRUE(dom_element_append_child(html, body));

    // Print the entire document
    html->print(buffer, 0);

    // Check output structure
    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);

    // Verify main structure
    EXPECT_TRUE(strstr(result, "<html>") != nullptr);
    EXPECT_TRUE(strstr(result, "<head>") != nullptr);
    EXPECT_TRUE(strstr(result, "<title>") != nullptr);
    EXPECT_TRUE(strstr(result, "<link") != nullptr);
    EXPECT_TRUE(strstr(result, "rel=\"author\"") != nullptr);
    EXPECT_TRUE(strstr(result, "rel=\"help\"") != nullptr);
    EXPECT_TRUE(strstr(result, "<meta") != nullptr);
    EXPECT_TRUE(strstr(result, "name=\"flags\"") != nullptr);
    EXPECT_TRUE(strstr(result, "name=\"assert\"") != nullptr);
    EXPECT_TRUE(strstr(result, "<style") != nullptr);
    EXPECT_TRUE(strstr(result, "type=\"text/css\"") != nullptr);
    EXPECT_TRUE(strstr(result, "<body>") != nullptr);
    EXPECT_TRUE(strstr(result, "<p>") != nullptr);
    EXPECT_TRUE(strstr(result, "<div>") != nullptr);

    // Verify closing tags
    EXPECT_TRUE(strstr(result, "</html>") != nullptr);
    EXPECT_TRUE(strstr(result, "</head>") != nullptr);
    EXPECT_TRUE(strstr(result, "</body>") != nullptr);
}

TEST_F(DomElementPrintTest, PrintElementWithMultipleAttributes) {
    // Create an element with many attributes like a real HTML element
    DomElement* form = dom_element_create(pool, "form", nullptr);
    ASSERT_NE(form, nullptr);

    dom_element_set_attribute(form, "id", "contact-form");
    dom_element_add_class(form, "form-horizontal");
    dom_element_add_class(form, "validation-enabled");
    dom_element_set_attribute(form, "method", "POST");
    dom_element_set_attribute(form, "action", "/submit");
    dom_element_set_attribute(form, "enctype", "multipart/form-data");
    dom_element_set_attribute(form, "novalidate", "true");
    dom_element_set_attribute(form, "data-submit-url", "/api/contact");
    dom_element_set_attribute(form, "data-success-message", "Thank you for your message!");

    form->print(buffer, 0);

    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);

    // Check all attributes are present
    EXPECT_TRUE(strstr(result, "id=\"contact-form\"") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"form-horizontal validation-enabled\"") != nullptr);
    EXPECT_TRUE(strstr(result, "method=\"POST\"") != nullptr);
    EXPECT_TRUE(strstr(result, "action=\"/submit\"") != nullptr);
    EXPECT_TRUE(strstr(result, "enctype=\"multipart/form-data\"") != nullptr);
    EXPECT_TRUE(strstr(result, "novalidate=\"true\"") != nullptr);
    EXPECT_TRUE(strstr(result, "data-submit-url=\"/api/contact\"") != nullptr);
    EXPECT_TRUE(strstr(result, "data-success-message=\"Thank you for your message!\"") != nullptr);
}

TEST_F(DomElementPrintTest, PrintElementWithPseudoStates) {
    // Create an input element with various pseudo-states
    DomElement* input = dom_element_create(pool, "input", nullptr);
    ASSERT_NE(input, nullptr);

    dom_element_set_attribute(input, "type", "text");
    dom_element_set_attribute(input, "id", "username");
    dom_element_add_class(input, "form-control");

    // Set multiple pseudo-states (simulating user interaction)
    input->pseudo_state = PSEUDO_STATE_FOCUS | PSEUDO_STATE_HOVER | PSEUDO_STATE_DISABLED;

    input->print(buffer, 0);

    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);

    // Check that pseudo-states are printed
    EXPECT_TRUE(strstr(result, "[pseudo:") != nullptr);
    EXPECT_TRUE(strstr(result, " focus") != nullptr);
    EXPECT_TRUE(strstr(result, " hover") != nullptr);
    EXPECT_TRUE(strstr(result, " disabled") != nullptr);
    EXPECT_TRUE(strstr(result, "]>") != nullptr);
}

TEST_F(DomElementPrintTest, PrintDeeplyNestedStructure) {
    // Create a deeply nested structure to test indentation
    DomElement* container = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(container, nullptr);
    dom_element_add_class(container, "container");

    DomElement* row = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(row, nullptr);
    dom_element_add_class(row, "row");

    DomElement* col = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(col, nullptr);
    dom_element_add_class(col, "col-md-6");

    DomElement* card = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(card, nullptr);
    dom_element_add_class(card, "card");

    DomElement* card_header = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(card_header, nullptr);
    dom_element_add_class(card_header, "card-header");

    DomElement* card_title = dom_element_create(pool, "h3", nullptr);
    ASSERT_NE(card_title, nullptr);
    dom_element_add_class(card_title, "card-title");

    DomElement* card_body = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(card_body, nullptr);
    dom_element_add_class(card_body, "card-body");

    DomElement* list = dom_element_create(pool, "ul", nullptr);
    ASSERT_NE(list, nullptr);
    dom_element_add_class(list, "list-group");

    DomElement* item1 = dom_element_create(pool, "li", nullptr);
    ASSERT_NE(item1, nullptr);
    dom_element_add_class(item1, "list-group-item");

    DomElement* item2 = dom_element_create(pool, "li", nullptr);
    ASSERT_NE(item2, nullptr);
    dom_element_add_class(item2, "list-group-item");

    // Build the nested structure
    ASSERT_TRUE(dom_element_append_child(list, item1));
    ASSERT_TRUE(dom_element_append_child(list, item2));
    ASSERT_TRUE(dom_element_append_child(card_body, list));
    ASSERT_TRUE(dom_element_append_child(card_header, card_title));
    ASSERT_TRUE(dom_element_append_child(card, card_header));
    ASSERT_TRUE(dom_element_append_child(card, card_body));
    ASSERT_TRUE(dom_element_append_child(col, card));
    ASSERT_TRUE(dom_element_append_child(row, col));
    ASSERT_TRUE(dom_element_append_child(container, row));

    // Print with indentation
    container->print(buffer, 0);

    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);

    // Verify nested structure
    EXPECT_TRUE(strstr(result, "class=\"container\"") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"row\"") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"col-md-6\"") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"card\"") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"card-header\"") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"card-title\"") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"card-body\"") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"list-group\"") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"list-group-item\"") != nullptr);

    // Check that all elements are properly closed
    EXPECT_TRUE(strstr(result, "</ul>") != nullptr);
    EXPECT_TRUE(strstr(result, "</div>") != nullptr);
    EXPECT_TRUE(strstr(result, "</h3>") != nullptr);
    EXPECT_TRUE(strstr(result, "</li>") != nullptr);
}

TEST_F(DomElementPrintTest, PrintFormWithInputElements) {
    // Create a realistic form structure
    DomElement* form = dom_element_create(pool, "form", nullptr);
    ASSERT_NE(form, nullptr);
    dom_element_set_attribute(form, "id", "signup-form");
    dom_element_add_class(form, "needs-validation");

    DomElement* fieldset = dom_element_create(pool, "fieldset", nullptr);
    ASSERT_NE(fieldset, nullptr);

    DomElement* legend = dom_element_create(pool, "legend", nullptr);
    ASSERT_NE(legend, nullptr);

    DomElement* email_input = dom_element_create(pool, "input", nullptr);
    ASSERT_NE(email_input, nullptr);
    dom_element_set_attribute(email_input, "type", "email");
    dom_element_set_attribute(email_input, "id", "email");
    dom_element_set_attribute(email_input, "name", "email");
    dom_element_set_attribute(email_input, "required", "true");
    dom_element_set_attribute(email_input, "placeholder", "Enter your email");
    dom_element_add_class(email_input, "form-control");

    DomElement* password_input = dom_element_create(pool, "input", nullptr);
    ASSERT_NE(password_input, nullptr);
    dom_element_set_attribute(password_input, "type", "password");
    dom_element_set_attribute(password_input, "id", "password");
    dom_element_set_attribute(password_input, "name", "password");
    dom_element_set_attribute(password_input, "required", "true");
    dom_element_set_attribute(password_input, "minlength", "8");
    dom_element_add_class(password_input, "form-control");

    DomElement* checkbox = dom_element_create(pool, "input", nullptr);
    ASSERT_NE(checkbox, nullptr);
    dom_element_set_attribute(checkbox, "type", "checkbox");
    dom_element_set_attribute(checkbox, "id", "agree");
    dom_element_set_attribute(checkbox, "name", "agree");
    dom_element_set_attribute(checkbox, "value", "yes");
    checkbox->pseudo_state = PSEUDO_STATE_CHECKED | PSEUDO_STATE_DISABLED;

    DomElement* submit_btn = dom_element_create(pool, "button", nullptr);
    ASSERT_NE(submit_btn, nullptr);
    dom_element_set_attribute(submit_btn, "type", "submit");
    dom_element_add_class(submit_btn, "btn");
    dom_element_add_class(submit_btn, "btn-primary");
    submit_btn->pseudo_state = PSEUDO_STATE_HOVER | PSEUDO_STATE_ACTIVE;

    // Build form structure
    ASSERT_TRUE(dom_element_append_child(fieldset, legend));
    ASSERT_TRUE(dom_element_append_child(fieldset, email_input));
    ASSERT_TRUE(dom_element_append_child(fieldset, password_input));
    ASSERT_TRUE(dom_element_append_child(fieldset, checkbox));
    ASSERT_TRUE(dom_element_append_child(fieldset, submit_btn));
    ASSERT_TRUE(dom_element_append_child(form, fieldset));

    form->print(buffer, 0);

    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);

    // Verify form structure and attributes
    EXPECT_TRUE(strstr(result, "<form") != nullptr);
    EXPECT_TRUE(strstr(result, "id=\"signup-form\"") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"needs-validation\"") != nullptr);
    EXPECT_TRUE(strstr(result, "<fieldset>") != nullptr);
    EXPECT_TRUE(strstr(result, "<legend>") != nullptr);

    // Check input elements
    EXPECT_TRUE(strstr(result, "type=\"email\"") != nullptr);
    EXPECT_TRUE(strstr(result, "type=\"password\"") != nullptr);
    EXPECT_TRUE(strstr(result, "type=\"checkbox\"") != nullptr);
    EXPECT_TRUE(strstr(result, "type=\"submit\"") != nullptr);

    // Check specific attributes
    EXPECT_TRUE(strstr(result, "placeholder=\"Enter your email\"") != nullptr);
    EXPECT_TRUE(strstr(result, "minlength=\"8\"") != nullptr);
    EXPECT_TRUE(strstr(result, "value=\"yes\"") != nullptr);

    // Check pseudo-states on checkbox and button
    EXPECT_TRUE(strstr(result, "[pseudo: checked disabled]") != nullptr);
    EXPECT_TRUE(strstr(result, "[pseudo: hover active]") != nullptr);

    // Check CSS classes
    EXPECT_TRUE(strstr(result, "class=\"form-control\"") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"btn btn-primary\"") != nullptr);
}

TEST_F(DomElementPrintTest, PrintTableStructure) {
    // Create a table structure to test complex nesting
    DomElement* table = dom_element_create(pool, "table", nullptr);
    ASSERT_NE(table, nullptr);
    dom_element_add_class(table, "table");
    dom_element_add_class(table, "table-striped");

    DomElement* thead = dom_element_create(pool, "thead", nullptr);
    ASSERT_NE(thead, nullptr);

    DomElement* header_row = dom_element_create(pool, "tr", nullptr);
    ASSERT_NE(header_row, nullptr);

    DomElement* th1 = dom_element_create(pool, "th", nullptr);
    ASSERT_NE(th1, nullptr);
    dom_element_set_attribute(th1, "scope", "col");

    DomElement* th2 = dom_element_create(pool, "th", nullptr);
    ASSERT_NE(th2, nullptr);
    dom_element_set_attribute(th2, "scope", "col");

    DomElement* th3 = dom_element_create(pool, "th", nullptr);
    ASSERT_NE(th3, nullptr);
    dom_element_set_attribute(th3, "scope", "col");

    DomElement* tbody = dom_element_create(pool, "tbody", nullptr);
    ASSERT_NE(tbody, nullptr);

    DomElement* row1 = dom_element_create(pool, "tr", nullptr);
    ASSERT_NE(row1, nullptr);

    DomElement* td1 = dom_element_create(pool, "td", nullptr);
    ASSERT_NE(td1, nullptr);

    DomElement* td2 = dom_element_create(pool, "td", nullptr);
    ASSERT_NE(td2, nullptr);

    DomElement* td3 = dom_element_create(pool, "td", nullptr);
    ASSERT_NE(td3, nullptr);

    // Build table structure
    ASSERT_TRUE(dom_element_append_child(header_row, th1));
    ASSERT_TRUE(dom_element_append_child(header_row, th2));
    ASSERT_TRUE(dom_element_append_child(header_row, th3));
    ASSERT_TRUE(dom_element_append_child(thead, header_row));

    ASSERT_TRUE(dom_element_append_child(row1, td1));
    ASSERT_TRUE(dom_element_append_child(row1, td2));
    ASSERT_TRUE(dom_element_append_child(row1, td3));
    ASSERT_TRUE(dom_element_append_child(tbody, row1));

    ASSERT_TRUE(dom_element_append_child(table, thead));
    ASSERT_TRUE(dom_element_append_child(table, tbody));

    table->print(buffer, 0);

    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);

    // Verify table structure
    EXPECT_TRUE(strstr(result, "<table") != nullptr);
    EXPECT_TRUE(strstr(result, "class=\"table table-striped\"") != nullptr);
    EXPECT_TRUE(strstr(result, "<thead>") != nullptr);
    EXPECT_TRUE(strstr(result, "<tbody>") != nullptr);
    EXPECT_TRUE(strstr(result, "<tr>") != nullptr);
    EXPECT_TRUE(strstr(result, "<th") != nullptr);
    EXPECT_TRUE(strstr(result, "<td>") != nullptr);
    EXPECT_TRUE(strstr(result, "scope=\"col\"") != nullptr);

    // Verify closing tags
    EXPECT_TRUE(strstr(result, "</table>") != nullptr);
    EXPECT_TRUE(strstr(result, "</thead>") != nullptr);
    EXPECT_TRUE(strstr(result, "</tbody>") != nullptr);
    EXPECT_TRUE(strstr(result, "</tr>") != nullptr);
    EXPECT_TRUE(strstr(result, "</th>") != nullptr);
    EXPECT_TRUE(strstr(result, "</td>") != nullptr);
}
