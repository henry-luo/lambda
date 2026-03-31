/**
 * @file test_serve_phase2_gtest.cpp
 * @brief GTest unit tests for lambda/serve/ Phase 2 components
 *
 * Tests: schema validation (standalone, no server dependency).
 * REST registry, OpenAPI, and Express compat tests require server.cpp
 * which has open API compatibility issues tracked separately in Phase 1.
 */

#include <gtest/gtest.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "../lambda/serve/serve_types.hpp"
#include "../lambda/serve/schema_validator.hpp"

// ============================================================================
// Schema validation — string type
// ============================================================================

class SchemaValidationTest : public ::testing::Test {};

TEST_F(SchemaValidationTest, ValidJsonMatchingStringSchema) {
    const char* schema = "{\"type\":\"string\",\"minLength\":2,\"maxLength\":10}";
    const char* value = "\"hello\"";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 1);
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.error_count, 0);
}

TEST_F(SchemaValidationTest, StringTooShort) {
    const char* schema = "{\"type\":\"string\",\"minLength\":5}";
    const char* value = "\"hi\"";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(result.valid);
    EXPECT_GT(result.error_count, 0);
}

TEST_F(SchemaValidationTest, StringTooLong) {
    const char* schema = "{\"type\":\"string\",\"maxLength\":3}";
    const char* value = "\"toolong\"";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(result.valid);
    EXPECT_GT(result.error_count, 0);
}

// ============================================================================
// Schema validation — numeric types
// ============================================================================

TEST_F(SchemaValidationTest, ValidIntegerValue) {
    const char* schema = "{\"type\":\"integer\",\"minimum\":1,\"maximum\":100}";
    const char* value = "42";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 1);
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.error_count, 0);
}

TEST_F(SchemaValidationTest, IntegerBelowMinimum) {
    const char* schema = "{\"type\":\"integer\",\"minimum\":10}";
    const char* value = "5";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(result.valid);
    EXPECT_GT(result.error_count, 0);
}

TEST_F(SchemaValidationTest, IntegerAboveMaximum) {
    const char* schema = "{\"type\":\"integer\",\"maximum\":50}";
    const char* value = "100";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(result.valid);
    EXPECT_GT(result.error_count, 0);
}

TEST_F(SchemaValidationTest, FloatNotMatchingIntegerType) {
    const char* schema = "{\"type\":\"integer\"}";
    const char* value = "3.14";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(result.valid);
}

TEST_F(SchemaValidationTest, ValidNumberValue) {
    const char* schema = "{\"type\":\"number\",\"minimum\":0.0}";
    const char* value = "3.14";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 1);
    EXPECT_TRUE(result.valid);
}

// ============================================================================
// Schema validation — primitive types
// ============================================================================

TEST_F(SchemaValidationTest, NullTypeMatch) {
    const char* schema = "{\"type\":\"null\"}";
    const char* value = "null";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 1);
    EXPECT_TRUE(result.valid);
}

TEST_F(SchemaValidationTest, BooleanTrue) {
    const char* schema = "{\"type\":\"boolean\"}";

    ValidationResult result;
    schema_validate("true", schema, &result);
    EXPECT_TRUE(result.valid);
}

TEST_F(SchemaValidationTest, BooleanFalse) {
    const char* schema = "{\"type\":\"boolean\"}";

    ValidationResult result;
    schema_validate("false", schema, &result);
    EXPECT_TRUE(result.valid);
}

TEST_F(SchemaValidationTest, TypeMismatch) {
    const char* schema = "{\"type\":\"string\"}";
    const char* value = "42";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(result.valid);
    EXPECT_GT(result.error_count, 0);
}

// ============================================================================
// Schema validation — arrays
// ============================================================================

TEST_F(SchemaValidationTest, ValidEmptyArray) {
    const char* schema = "{\"type\":\"array\"}";
    const char* value = "[]";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 1);
    EXPECT_TRUE(result.valid);
}

TEST_F(SchemaValidationTest, ArrayMinItems) {
    const char* schema = "{\"type\":\"array\",\"minItems\":2}";
    const char* value = "[1]";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(result.valid);
}

TEST_F(SchemaValidationTest, ArrayMaxItems) {
    const char* schema = "{\"type\":\"array\",\"maxItems\":1}";
    const char* value = "[1,2,3]";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(result.valid);
}

// ============================================================================
// Schema validation — objects
// ============================================================================

TEST_F(SchemaValidationTest, ValidObjectWithRequiredField) {
    const char* schema = "{\"type\":\"object\",\"required\":[\"name\"]}";
    const char* value = "{\"name\":\"Alice\"}";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 1);
    EXPECT_TRUE(result.valid);
}

TEST_F(SchemaValidationTest, ObjectMissingRequiredField) {
    const char* schema = "{\"type\":\"object\",\"required\":[\"name\"]}";
    const char* value = "{\"age\":30}";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(result.valid);
    EXPECT_GT(result.error_count, 0);
}

// ============================================================================
// Schema validation — enum
// ============================================================================

TEST_F(SchemaValidationTest, EnumValidMatch) {
    const char* schema = "{\"enum\":[\"red\",\"green\",\"blue\"]}";
    const char* value = "\"red\"";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 1);
    EXPECT_TRUE(result.valid);
}

TEST_F(SchemaValidationTest, EnumInvalidValue) {
    const char* schema = "{\"enum\":[\"red\",\"green\",\"blue\"]}";
    const char* value = "\"purple\"";

    ValidationResult result;
    int r = schema_validate(value, schema, &result);
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(result.valid);
}

// ============================================================================
// Validation result JSON formatting
// ============================================================================

TEST_F(SchemaValidationTest, ErrorJsonFormat) {
    const char* schema = "{\"type\":\"integer\",\"minimum\":100}";
    const char* value = "5";

    ValidationResult result;
    schema_validate(value, schema, &result);
    EXPECT_FALSE(result.valid);

    char* err_json = schema_validation_error_json(&result);
    ASSERT_NE(err_json, nullptr);
    EXPECT_NE(strstr(err_json, "details"), nullptr);
    free(err_json);
}

TEST(ValidationResultJson, SingleError) {
    ValidationResult result;
    memset(&result, 0, sizeof(result));
    result.valid = false;
    result.errors[0].path = "#";
    result.errors[0].message = "type mismatch";
    result.error_count = 1;

    char* buf = schema_validation_error_json(&result);
    ASSERT_NE(buf, nullptr);
    EXPECT_NE(strstr(buf, "details"), nullptr);
    EXPECT_NE(strstr(buf, "type mismatch"), nullptr);
    free(buf);
}

TEST(ValidationResultJson, EmptyErrors) {
    ValidationResult result;
    memset(&result, 0, sizeof(result));
    result.valid = true;
    result.error_count = 0;

    char* buf = schema_validation_error_json(&result);
    ASSERT_NE(buf, nullptr);
    EXPECT_NE(strstr(buf, "details"), nullptr);
    free(buf);
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

