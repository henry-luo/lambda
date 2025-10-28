#include <gtest/gtest.h>

extern "C" {
#include "../../../lambda/input/css/dom_element.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/mempool.h"
#include "../../../lib/log.h"
}

// Simple test for DOM element printing functionality
class DomElementSimpleTest : public ::testing::Test {
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

TEST_F(DomElementSimpleTest, PrintEmptyDiv) {
    // Create a simple div element using proper API
    DomElement* div = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(div, nullptr);

    // Print the element
    dom_element_print(div, buffer, 0);

    // Check the output
    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "<div></div>\n");
}

TEST_F(DomElementSimpleTest, PrintDivWithId) {
    // Create div element with ID
    DomElement* div = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(div, nullptr);
    
    // Set ID attribute
    dom_element_set_attribute(div, "id", "test-id");
    
    // Print the element
    dom_element_print(div, buffer, 0);

    // Check the output contains the ID
    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(strstr(result, "id=\"test-id\"") != nullptr);
}

TEST_F(DomElementSimpleTest, PrintDivWithClass) {
    // Create div element with class
    DomElement* div = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(div, nullptr);
    
    // Add class
    dom_element_add_class(div, "test-class");
    
    // Print the element
    dom_element_print(div, buffer, 0);

    // Check the output contains the class
    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(strstr(result, "class=\"test-class\"") != nullptr);
}

TEST_F(DomElementSimpleTest, PrintNestedElements) {
    // Create parent div
    DomElement* div = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(div, nullptr);
    
    // Create child span
    DomElement* span = dom_element_create(pool, "span", nullptr);
    ASSERT_NE(span, nullptr);
    
    // Append child (only works for DomElement to DomElement)
    ASSERT_TRUE(dom_element_append_child(div, span));
    
    // Print with indentation
    dom_element_print(div, buffer, 2);
    
    // Check output structure
    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(strstr(result, "<div>") != nullptr);
    EXPECT_TRUE(strstr(result, "<span>") != nullptr);
    EXPECT_TRUE(strstr(result, "</span>") != nullptr);
    EXPECT_TRUE(strstr(result, "</div>") != nullptr);
}

TEST_F(DomElementSimpleTest, PrintWithIndentation) {
    // Create simple element
    DomElement* p = dom_element_create(pool, "p", nullptr);
    ASSERT_NE(p, nullptr);
    
    // Print with indentation level 3
    dom_element_print(p, buffer, 3);
    
    // Check that indentation is applied
    const char* result = buffer->str;
    ASSERT_NE(result, nullptr);
    
    // Should start with 3 spaces (indentation level 3 = 3 spaces)
    EXPECT_TRUE(strncmp(result, "   <p>", 6) == 0);
}