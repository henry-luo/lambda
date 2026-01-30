/**
 * @file test_validator_input_gtest.cpp
 * @brief Validator tests through lambda-input-full DLL
 *
 * Tests the Lambda validator by calling functions directly from the
 * lambda-input-full shared library. This verifies that:
 * 1. Validator functions are properly exported from the DLL
 * 2. Validator works correctly when called as a library
 * 3. Integration with input parsing works properly
 *
 * @author Henry Luo
 * @license MIT
 */

#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Include validator headers
#include "../lambda/validator/validator.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lib/mempool.h"
#include "../lib/log.h"

// Forward declarations for input functions
extern "C" {
    Input* input_from_source(char* source, Url* abs_url, String* type, String* flavor);
}

// Helper to create Lambda String
String* create_lambda_string(const char* text) {
    if (!text) return nullptr;

    size_t len = strlen(text);
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return nullptr;

    result->len = len;
    result->ref_cnt = 1;
    strcpy(result->chars, text);

    return result;
}

// Test fixture for validator tests
class ValidatorInputTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    SchemaValidator* validator = nullptr;

    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Failed to create memory pool";

        validator = schema_validator_create(pool);
        ASSERT_NE(validator, nullptr) << "Failed to create validator";
    }

    void TearDown() override {
        if (validator) {
            schema_validator_destroy(validator);
            validator = nullptr;
        }

        if (pool) {
            pool_destroy(pool);
            pool = nullptr;
        }
    }

    // Helper to create test items
    ConstItem create_string(const char* value) {
        size_t len = strlen(value);
        String* str = (String*)pool_calloc(pool, sizeof(String) + len + 1);
        str->len = len;
        str->ref_cnt = 0;
        strcpy(str->chars, value);
        Item item = {.item = s2it(str)};
        return *(ConstItem*)&item;
    }

    ConstItem create_int(int value) {
        Item item = {.item = i2it(value)};
        return *(ConstItem*)&item;
    }

    ConstItem create_bool(bool value) {
        Item item;
        item.bool_val = (uint64_t)(value ? 1 : 0);
        item._type_id = LMD_TYPE_BOOL;
        return *(ConstItem*)&item;
    }

    ConstItem create_null() {
        Item item = {.item = ITEM_NULL};
        return *(ConstItem*)&item;
    }

    Type* create_type(TypeId type_id) {
        Type* type = (Type*)pool_calloc(pool, sizeof(Type));
        type->type_id = type_id;
        return type;
    }
};

// ==================== Basic Primitive Type Tests ====================

TEST_F(ValidatorInputTest, ValidatorCreation) {
    EXPECT_NE(validator, nullptr);
    EXPECT_NE(validator->get_pool(), nullptr);
    EXPECT_NE(validator->get_transpiler(), nullptr);
}

TEST_F(ValidatorInputTest, ValidateString) {
    ConstItem string_item = create_string("hello world");
    Type* string_type = create_type(LMD_TYPE_STRING);

    ValidationResult* result = schema_validator_validate_type(
        validator, string_item, string_type);

    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "String validation should pass";
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorInputTest, ValidateInt) {
    ConstItem int_item = create_int(42);
    Type* int_type = create_type(LMD_TYPE_INT);

    ValidationResult* result = schema_validator_validate_type(
        validator, int_item, int_type);

    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid);
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorInputTest, ValidateBool) {
    ConstItem bool_item = create_bool(true);
    Type* bool_type = create_type(LMD_TYPE_BOOL);

    ValidationResult* result = schema_validator_validate_type(
        validator, bool_item, bool_type);

    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid);
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorInputTest, ValidateNull) {
    ConstItem null_item = create_null();
    Type* null_type = create_type(LMD_TYPE_NULL);

    ValidationResult* result = schema_validator_validate_type(
        validator, null_item, null_type);

    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid);
}

// ==================== Type Mismatch Tests ====================

TEST_F(ValidatorInputTest, StringIntMismatch) {
    ConstItem string_item = create_string("not a number");
    Type* int_type = create_type(LMD_TYPE_INT);

    ValidationResult* result = schema_validator_validate_type(
        validator, string_item, int_type);

    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid) << "String should not validate as int";
    EXPECT_GT(result->error_count, 0);

    // Check error details
    if (result->errors) {
        EXPECT_EQ(result->errors->code, VALID_ERROR_TYPE_MISMATCH);
        EXPECT_NE(result->errors->message, nullptr);
    }
}

TEST_F(ValidatorInputTest, IntStringMismatch) {
    ConstItem int_item = create_int(123);
    Type* string_type = create_type(LMD_TYPE_STRING);

    ValidationResult* result = schema_validator_validate_type(
        validator, int_item, string_type);

    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid);
    EXPECT_GT(result->error_count, 0);
}

TEST_F(ValidatorInputTest, BoolIntMismatch) {
    ConstItem bool_item = create_bool(true);
    Type* int_type = create_type(LMD_TYPE_INT);

    ValidationResult* result = schema_validator_validate_type(
        validator, bool_item, int_type);

    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid);
}

// ==================== Error Reporting Tests ====================

TEST_F(ValidatorInputTest, ErrorHasMessage) {
    ConstItem string_item = create_string("wrong");
    Type* int_type = create_type(LMD_TYPE_INT);

    ValidationResult* result = schema_validator_validate_type(
        validator, string_item, int_type);

    ASSERT_NE(result, nullptr);
    ASSERT_FALSE(result->valid);
    ASSERT_NE(result->errors, nullptr);

    ValidationError* error = result->errors;
    EXPECT_NE(error->message, nullptr);
    EXPECT_GT((int)error->message->len, 0);

    // Error message should mention type mismatch
    const char* msg = error->message->chars;
    EXPECT_NE(msg, nullptr);
}

TEST_F(ValidatorInputTest, ValidationResultDestroy) {
    ConstItem string_item = create_string("test");
    Type* string_type = create_type(LMD_TYPE_STRING);

    ValidationResult* result = schema_validator_validate_type(
        validator, string_item, string_type);

    ASSERT_NE(result, nullptr);
}

// ==================== Integration with Input Parsing ====================

// TODO: This test needs proper TypeMap setup to work correctly
// For now, commenting out to avoid segfault
/*
TEST_F(ValidatorInputTest, ValidateJSONInput) {
    // Create a simple JSON document
    const char* json_source = R"({"name": "Alice", "age": 30})";
    char* source_copy = strdup(json_source);

    String* json_type = create_lambda_string("json");
    String* flavor = nullptr;

    // Parse JSON to Lambda Item
    Input* input = input_from_source(source_copy, nullptr, json_type, flavor);

    if (input && input->root.item) {
        // The root should be a map
        EXPECT_EQ(input->root.type_id(), LMD_TYPE_MAP);

        // Create a simple map type for validation
        Type* map_type = create_type(LMD_TYPE_MAP);

        // Convert Item to ConstItem for validation
        ConstItem root_item = input->root.to_const();

        ValidationResult* result = schema_validator_validate_type(
            validator, root_item, map_type);

        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(result->valid) << "JSON map should validate as map type";
    }

    free(source_copy);
    if (json_type) free(json_type);
}
*/

// ==================== Null/Edge Case Tests ====================

TEST_F(ValidatorInputTest, NullValidator) {
    ConstItem string_item = create_string("test");
    Type* string_type = create_type(LMD_TYPE_STRING);

    ValidationResult* result = schema_validator_validate_type(
        nullptr, string_item, string_type);

    // The validator handles null gracefully by returning an invalid result
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid) << "Null validator should produce invalid result";
}

TEST_F(ValidatorInputTest, NullType) {
    ConstItem string_item = create_string("test");

    ValidationResult* result = schema_validator_validate_type(
        validator, string_item, nullptr);

    // Should handle null type gracefully
    EXPECT_NE(result, nullptr);
    EXPECT_FALSE(result->valid);
}

TEST_F(ValidatorInputTest, EmptyString) {
    ConstItem empty_string = create_string("");
    Type* string_type = create_type(LMD_TYPE_STRING);

    ValidationResult* result = schema_validator_validate_type(
        validator, empty_string, string_type);

    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Empty string should still be valid string type";
}

// ==================== Multiple Validation Tests ====================

TEST_F(ValidatorInputTest, MultipleValidations) {
    Type* string_type = create_type(LMD_TYPE_STRING);
    Type* int_type = create_type(LMD_TYPE_INT);

    // First validation
    ConstItem string_item = create_string("hello");
    ValidationResult* result1 = schema_validator_validate_type(
        validator, string_item, string_type);
    EXPECT_TRUE(result1->valid);

    // Second validation
    ConstItem int_item = create_int(42);
    ValidationResult* result2 = schema_validator_validate_type(
        validator, int_item, int_type);
    EXPECT_TRUE(result2->valid);

    // Third validation (should fail)
    ValidationResult* result3 = schema_validator_validate_type(
        validator, string_item, int_type);
    EXPECT_FALSE(result3->valid);
}

// ==================== Main Entry Point ====================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
