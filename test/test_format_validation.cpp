/**
 * @file test_format_validation.cpp
 * @brief Tests for format-specific validation (Sprint 4)
 * @author Henry Luo
 * @date November 14, 2025
 */

#include <gtest/gtest.h>
#include "../lambda/validator/validator.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include "../lib/arraylist.h"
#include "../lib/strbuf.h"
#include "../lambda/name_pool.h"

// test fixture for format validation tests
class FormatValidationTest : public ::testing::Test {
protected:
    Pool* pool;
    SchemaValidator* validator;
    Input* input;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);

        validator = schema_validator_create(pool);
        ASSERT_NE(validator, nullptr);

        // Use Input::create to properly initialize all fields including arena
        input = Input::create(pool, nullptr);
        ASSERT_NE(input, nullptr);
    }

    void TearDown() override {
        if (input) {
            arraylist_free(input->type_list);
        }
        if (validator) {
            schema_validator_destroy(validator);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }
};

// ==================== Format Detection Tests ====================

TEST_F(FormatValidationTest, DetectXMLFormat) {
    // create an XML element
    MarkBuilder builder(input);
    Item item = builder.createElement("document");
    ConstItem const_item = *(ConstItem*)&item;

    const char* format = detect_input_format(const_item);
    ASSERT_NE(format, nullptr);
    EXPECT_STREQ(format, "xml");
}

TEST_F(FormatValidationTest, DetectHTMLFormat) {
    // create an HTML element
    MarkBuilder builder(input);
    Item item = builder.createElement("html");
    ConstItem const_item = *(ConstItem*)&item;

    const char* format = detect_input_format(const_item);
    ASSERT_NE(format, nullptr);
    EXPECT_STREQ(format, "html");
}

TEST_F(FormatValidationTest, DetectJSONFromMap) {
    // create a map (typical JSON structure)
    MarkBuilder builder(input);
    Item map_item = builder.createMap();

    ConstItem const_item = *(ConstItem*)&map_item;

    const char* format = detect_input_format(const_item);
    ASSERT_NE(format, nullptr);
    EXPECT_STREQ(format, "json");
}

TEST_F(FormatValidationTest, DetectJSONFromList) {
    // create a list (JSON array)
    MarkBuilder builder(input);
    Item list_item = builder.createArray();

    ConstItem const_item = *(ConstItem*)&list_item;

    const char* format = detect_input_format(const_item);
    ASSERT_NE(format, nullptr);
    EXPECT_STREQ(format, "json");
}

// ==================== XML Document Unwrapping Tests ====================

TEST_F(FormatValidationTest, UnwrapXMLDocumentWrapper) {
    // create <document><article>content</article></document>
    MarkBuilder builder(input);
    Item nested = builder.element("document")
        .child(builder.createElement("article"))
        .final();
    ConstItem const_item = *(ConstItem*)&nested;

    // unwrap the document wrapper
    ConstItem unwrapped = unwrap_xml_document(const_item, pool);

    // verify we got an element (the article)
    EXPECT_TRUE(unwrapped.type_id() == LMD_TYPE_ELEMENT);
}

TEST_F(FormatValidationTest, UnwrapXMLPreservesNonDocumentElements) {
    // create <article>content</article> (no wrapper)
    MarkBuilder builder(input);
    Item item = builder.createElement("article");
    ConstItem const_item = *(ConstItem*)&item;

    // unwrap should return the same item
    ConstItem unwrapped = unwrap_xml_document(const_item, pool);

    // verify we still have an element
    EXPECT_TRUE(unwrapped.type_id() == LMD_TYPE_ELEMENT);
}

TEST_F(FormatValidationTest, UnwrapXMLHandlesNonElements) {
    // create a map (not an element)
    MarkBuilder builder(input);
    Item item = builder.createMap();
    ConstItem const_item = *(ConstItem*)&item;

    // unwrap should return the same item
    ConstItem unwrapped = unwrap_xml_document(const_item, pool);

    // verify we still have a map (types should match)
    EXPECT_EQ(unwrapped.type_id(), const_item.type_id());
}

// ==================== HTML Document Unwrapping Tests ====================

TEST_F(FormatValidationTest, UnwrapHTMLFindsBody) {
    // create <html><head>...</head><body>content</body></html>
    MarkBuilder builder(input);
    Item item = builder.element("html")
        .child(builder.createElement("head"))
        .child(builder.createElement("body"))
        .final();
    ConstItem const_item = *(ConstItem*)&item;

    // unwrap should return the body element
    ConstItem unwrapped = unwrap_html_document(const_item, pool);

    // verify we got an element
    EXPECT_TRUE(unwrapped.type_id() == LMD_TYPE_ELEMENT);
}

TEST_F(FormatValidationTest, UnwrapHTMLPreservesNonHTMLElements) {
    // create <div>content</div> (not an html root)
    MarkBuilder builder(input);
    Item item = builder.createElement("div");
    ConstItem const_item = *(ConstItem*)&item;

    // unwrap should return the same item
    ConstItem unwrapped = unwrap_html_document(const_item, pool);

    // verify we still have an element
    EXPECT_TRUE(unwrapped.type_id() == LMD_TYPE_ELEMENT);
}

TEST_F(FormatValidationTest, UnwrapHTMLHandlesHTMLWithoutBody) {
    // create <html><head>...</head></html> (no body)
    MarkBuilder builder(input);
    Item item = builder.element("html")
        .child(builder.createElement("head"))
        .final();
    ConstItem const_item = *(ConstItem*)&item;

    // unwrap should return original (no body found)
    ConstItem unwrapped = unwrap_html_document(const_item, pool);

    // verify we still have an element (the html root)
    EXPECT_TRUE(unwrapped.type_id() == LMD_TYPE_ELEMENT);
}

// ==================== Format-Aware Validation API Tests ====================

TEST_F(FormatValidationTest, ValidateWithXMLFormat) {
    // load a simple schema
    const char* schema = "type Article = <article>;";
    int load_result = schema_validator_load_schema(validator, schema, "Article");
    ASSERT_EQ(load_result, 0);

    // create <document><article/></document>
    MarkBuilder builder(input);
    Item nested = builder.element("document")
        .child(builder.createElement("article"))
        .final();
    ConstItem const_item = *(ConstItem*)&nested;

    // validate with XML format (should unwrap document)
    ValidationResult* result = schema_validator_validate_with_format(
        validator, const_item, "Article", "xml"
    );

    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "XML document wrapper should be unwrapped";
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(FormatValidationTest, ValidateWithAutoDetectedFormat) {
    // load a simple schema
    const char* schema = "type Doc = <document>;";
    int load_result = schema_validator_load_schema(validator, schema, "Doc");
    ASSERT_EQ(load_result, 0);

    // create <document/> element
    MarkBuilder builder(input);
    Item item = builder.createElement("document");
    ConstItem const_item = *(ConstItem*)&item;

    // validate without format hint (should auto-detect XML)
    ValidationResult* result = schema_validator_validate_with_format(
        validator, const_item, "Doc", nullptr
    );

    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid);
    EXPECT_EQ(result->error_count, 0);
}

// ==================== Main ====================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
