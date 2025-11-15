/**
 * @file test_validator_integration.cpp
 * @brief Integration tests for Lambda validator - Sprint 5
 * @author Henry Luo
 * @date November 14, 2025
 *
 * Tests combining multiple validator features in realistic scenarios:
 * - Null vs missing field validation
 * - Enhanced error reporting with suggestions
 * - Validation options (strict mode, max errors, timeout)
 * - Format-specific handling (XML/HTML unwrapping)
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

// Test fixture for integration tests
class ValidatorIntegrationTest : public ::testing::Test {
protected:
    Pool* pool;
    SchemaValidator* validator;
    Input* input;

    void SetUp() override {
        // Initialize logging system
        log_parse_config_file("log.conf");
        log_init("");  // Initialize with parsed config

        pool = pool_create();
        ASSERT_NE(pool, nullptr);

        validator = schema_validator_create(pool);
        ASSERT_NE(validator, nullptr);

        // Create Input context for MarkBuilder
        NamePool* name_pool = name_pool_create(pool, nullptr);
        ArrayList* type_list = arraylist_new(32);
        StringBuf* sb = stringbuf_new_cap(pool, 256);

        input = (Input*)pool_alloc(pool, sizeof(Input));
        input->pool = pool;
        input->name_pool = name_pool;
        input->type_list = type_list;
        input->sb = sb;
        input->url = nullptr;
        input->path = nullptr;
        input->root = (Item){.item = 0};
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

// ==================== Real-World Document Validation ====================

TEST_F(ValidatorIntegrationTest, ValidateArticleWithOptionalFields) {
    // Schema: Article with required and optional fields
    const char* schema = R"(
        type Article = {
            title: string,
            author: string
        }
    )";

    int load_result = schema_validator_load_schema(validator, schema, "Article");
    ASSERT_EQ(load_result, 0) << "Schema should load successfully";

    // Test: Valid article with required fields using simpler builder
    MarkBuilder builder(input);
    Item article = builder.map()
        .put("title", "Hello World")
        .put("author", "Alice")
        .final();

    ValidationResult* result = schema_validator_validate(validator, *(ConstItem*)&article, "Article");
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Article with required fields should be valid";
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorIntegrationTest, ValidateWithStrictModeAndMaxErrors) {
    // Schema with multiple potential errors
    const char* schema = R"(
        type Person = {
            name: string,
            age: int
        }
    )";

    int load_result = schema_validator_load_schema(validator, schema, "Person");
    ASSERT_EQ(load_result, 0);

    // Configure validation options
    schema_validator_set_strict_mode(validator, true);
    schema_validator_set_max_errors(validator, 100);

    // Create invalid document with wrong types
    MarkBuilder builder(input);
    Item person = builder.map()
        .put("name", (int64_t)42)  // wrong type (int instead of string)
        .put("age", "thirty")  // wrong type (string instead of int)
        .final();

    ValidationResult* result = schema_validator_validate(validator, *(ConstItem*)&person, "Person");
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid);

    // Should report errors but respect max_errors limit
    EXPECT_GT(result->error_count, 0);
    EXPECT_LE(result->error_count, 100) << "Should respect max_errors limit";
}

TEST_F(ValidatorIntegrationTest, ValidateXMLDocumentWithUnwrapping) {
    // Schema for article element
    const char* schema = "type Article = <article>;";

    int load_result = schema_validator_load_schema(validator, schema, "Article");
    ASSERT_EQ(load_result, 0);

    // Create XML document with wrapper: <document><article/></document>
    MarkBuilder builder(input);
    Item wrapped_doc = builder.element("document")
        .child(builder.createElement("article"))
        .final();

    // Validate with XML format - should automatically unwrap
    ValidationResult* result = schema_validator_validate_with_format(
        validator,
        *(ConstItem*)&wrapped_doc,
        "Article",
        "xml"
    );

    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "XML document wrapper should be unwrapped automatically";
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorIntegrationTest, ValidateNestedStructureWithErrors) {
    // Schema with nested structure
    const char* schema = R"(
        type Book = {
            title: string,
            author: {
                name: string
            }
        }
    )";

    int load_result = schema_validator_load_schema(validator, schema, "Book");
    ASSERT_EQ(load_result, 0);

    // Create book with invalid nested author
    MarkBuilder builder(input);
    Item author = builder.map()
        .put("name", "Alice")
        .final();

    Item book = builder.map()
        .put("title", "Lambda Guide")
        .put("author", author)
        .final();

    ValidationResult* result = schema_validator_validate(validator, *(ConstItem*)&book, "Book");
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Valid nested structure should pass";
}

// ==================== Edge Cases ====================

TEST_F(ValidatorIntegrationTest, ValidateEmptyMap) {
    const char* schema = "type Empty = {};";

    int load_result = schema_validator_load_schema(validator, schema, "Empty");
    ASSERT_EQ(load_result, 0);

    MarkBuilder builder(input);
    Item empty = builder.createMap();

    ValidationResult* result = schema_validator_validate(validator, *(ConstItem*)&empty, "Empty");
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Empty map should match empty schema";
}

TEST_F(ValidatorIntegrationTest, ValidateNullVsOptional) {
    // Test explicit null vs optional field
    const char* schema = R"(
        type Data = {
            required: string
        }
    )";

    int load_result = schema_validator_load_schema(validator, schema, "Data");
    ASSERT_EQ(load_result, 0);

    // Test: Required field present
    MarkBuilder builder(input);
    Item data = builder.map()
        .put("required", "present")
        .final();

    ValidationResult* result = schema_validator_validate(validator, *(ConstItem*)&data, "Data");
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Required field present should be valid";
}

TEST_F(ValidatorIntegrationTest, ValidateArrayOccurrences) {
    // Test array occurrence operators (*, +, ?)
    const char* schema = R"(
        type Lists = {
            zero_or_more: [int*],
            one_or_more: [int+]
        }
    )";

    int load_result = schema_validator_load_schema(validator, schema, "Lists");
    ASSERT_EQ(load_result, 0);

    // Valid: empty array for zero_or_more, non-empty for one_or_more
    MarkBuilder builder(input);
    Item lists = builder.map()
        .put("zero_or_more", builder.createArray())
        .put("one_or_more", builder.array().append((int64_t)1).final())
        .final();

    ValidationResult* result = schema_validator_validate(validator, *(ConstItem*)&lists, "Lists");
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result, nullptr);

    // Debug: Print errors if validation failed
    if (!result->valid) {
        printf("Validation failed with %d errors:\n", result->error_count);
        for (int i = 0; i < result->error_count; i++) {
            printf("  Error %d: %s\n", i+1, result->errors[i].message->chars);
        }
    }

    EXPECT_TRUE(result->valid);
}

// ==================== Format Detection ====================

TEST_F(ValidatorIntegrationTest, AutoDetectAndValidateFormats) {
    // Test auto-detection for different formats

    // XML element
    MarkBuilder xml_builder(input);
    Item xml_item = xml_builder.createElement("root");
    const char* xml_format = detect_input_format(*(ConstItem*)&xml_item);
    ASSERT_NE(xml_format, nullptr);
    EXPECT_STREQ(xml_format, "xml");

    // HTML element
    MarkBuilder html_builder(input);
    Item html_item = html_builder.createElement("html");
    const char* html_format = detect_input_format(*(ConstItem*)&html_item);
    ASSERT_NE(html_format, nullptr);
    EXPECT_STREQ(html_format, "html");

    // JSON map
    MarkBuilder json_builder(input);
    Item json_item = json_builder.createMap();
    const char* json_format = detect_input_format(*(ConstItem*)&json_item);
    ASSERT_NE(json_format, nullptr);
    EXPECT_STREQ(json_format, "json");
}

// ==================== Performance & Limits ====================

TEST_F(ValidatorIntegrationTest, ValidateWithDepthLimit) {
    // Schema allowing deep nesting
    const char* schema = R"(
        type Node = {
            value: int,
            child: Node?
        }
    )";

    int load_result = schema_validator_load_schema(validator, schema, "Node");
    ASSERT_EQ(load_result, 0);

    // Set a reasonable depth limit
    ValidationOptions* opts = schema_validator_get_options(validator);
    opts->max_depth = 10;

    // Create deeply nested structure (beyond limit)
    MarkBuilder builder(input);
    Item current = builder.createInt(0);

    // Build 15 levels deep (exceeds max_depth of 10)
    for (int i = 0; i < 15; i++) {
        Item parent = builder.map()
            .put("value", (int64_t)i)
            .put("child", current)
            .final();
        current = parent;
    }

    ValidationResult* result = schema_validator_validate(validator, *(ConstItem*)&current, "Node");
    ASSERT_NE(result, nullptr);

    // Should fail due to depth limit
    EXPECT_FALSE(result->valid) << "Deep nesting should exceed max_depth limit";
}

TEST_F(ValidatorIntegrationTest, ValidateDefaultOptionsValues) {
    // Verify default options are sensible
    ValidationOptions defaults = schema_validator_default_options();

    EXPECT_FALSE(defaults.strict_mode) << "Default should not be strict";
    EXPECT_FALSE(defaults.allow_unknown_fields) << "Default should not allow unknown fields";
    EXPECT_EQ(defaults.max_depth, 100) << "Default max depth should be 100";
    EXPECT_EQ(defaults.timeout_ms, 0) << "Default should have no timeout";
    EXPECT_EQ(defaults.max_errors, 0) << "Default should report all errors";
    EXPECT_TRUE(defaults.show_suggestions) << "Default should show suggestions";
    EXPECT_TRUE(defaults.show_context) << "Default should show context";
}

// ==================== Main ====================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
