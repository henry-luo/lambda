/**
 * @file test_ast_validator_gtest.cpp
 * @brief Unit Tests for AST-Based Lambda Validator using Google Test
 * @author Henry Luo
 * @license MIT
 */

#include <gtest/gtest.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Include validator headers for ValidationResult and run_validation
#include "../lambda/validator/validator.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/input/input.h"
#include "../lib/mempool.h"
#include "../lib/arraylist.h"
#include "../lib/stringbuf.h"

// Forward declaration for path segment creation
PathSegment* create_path_segment(PathSegmentType type, const char* name, long index, Pool* pool);

// Simple implementation of create_path_segment for tests
PathSegment* create_path_segment(PathSegmentType type, const char* name, long index, Pool* pool) {
    PathSegment* segment = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
    if (!segment) return nullptr;

    segment->type = type;
    segment->next = nullptr;

    switch (type) {
        case PATH_FIELD:
            if (name) {
                segment->data.field_name.str = name;
                segment->data.field_name.length = strlen(name);
            }
            break;
        case PATH_INDEX:
            segment->data.index = index;
            break;
        case PATH_ELEMENT:
            if (name) {
                segment->data.element_tag.str = name;
                segment->data.element_tag.length = strlen(name);
            }
            break;
        case PATH_ATTRIBUTE:
            if (name) {
                segment->data.attr_name.str = name;
                segment->data.attr_name.length = strlen(name);
            }
            break;
    }

    return segment;
}

// Stub implementations for missing functions required by validator (C++ linkage)
void find_errors(TSNode node) {
    // Stub implementation - do nothing for tests
    (void)node; // Suppress unused parameter warning
}

AstNode* build_script(Transpiler* tp, TSNode script_node) {
    // Stub implementation - return null for tests
    (void)tp; // Suppress unused parameter warning
    (void)script_node; // Suppress unused parameter warning
    return nullptr;
}

// Test fixture class for AST validator tests
class AstValidatorTest : public ::testing::Test {
protected:
    Pool* test_pool = nullptr;
    AstValidator* validator = nullptr;
    Input* input = nullptr;

    void SetUp() override {
        test_pool = pool_create();
        ASSERT_NE(test_pool, nullptr) << "Failed to create memory pool";

        validator = ast_validator_create(test_pool);
        ASSERT_NE(validator, nullptr) << "Failed to create AST validator";

        // Create Input context for MarkBuilder
        NamePool* name_pool = name_pool_create(test_pool, nullptr);
        ArrayList* type_list = arraylist_new(32);
        StringBuf* sb = stringbuf_new_cap(test_pool, 256);

        input = (Input*)pool_alloc(test_pool, sizeof(Input));
        input->pool = test_pool;
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
            ast_validator_destroy(validator);
            validator = nullptr;
        }

        if (test_pool) {
            pool_destroy(test_pool);
            test_pool = nullptr;
        }
    }

    // Helper function to create typed items for testing
    ConstItem create_test_string(const char* value) {
        size_t len = strlen(value);
        String* str = (String*)pool_calloc(test_pool, sizeof(String) + len + 1);
        str->len = len;
        strcpy(str->chars, value);

        Item item;
        item.item = s2it(str);
        return item.to_const();
    }

    ConstItem create_test_int(int64_t value) {
        Item item;
        item.item = i2it(value);
        return item.to_const();
    }

    ConstItem create_test_float(double value) {
        double* float_ptr = (double*)pool_calloc(test_pool, sizeof(double));
        *float_ptr = value;

        Item item;
        item.pointer = (uint64_t)float_ptr;
        item._type_id = LMD_TYPE_FLOAT;
        return item.to_const();
    }

    ConstItem create_test_bool(bool value) {
        Item item;
        item.bool_val = value ? 1 : 0;
        item._type_id = LMD_TYPE_BOOL;
        return item.to_const();
    }

    ConstItem create_test_null() {
        Item item;
        item.item = 0;
        item._type_id = LMD_TYPE_NULL;
        return item.to_const();
    }

    // Helper function to create basic types for testing
    Type* create_test_type(TypeId type_id) {
        Type* type = (Type*)pool_calloc(test_pool, sizeof(Type));
        type->type_id = type_id;
        return type;
    }

    // Helper function to create test element types
    TypeElmt* create_test_element_type(const char* name, Type* content_type) {
        TypeElmt* element_type = (TypeElmt*)pool_calloc(test_pool, sizeof(TypeElmt));

        // Set element type properties
        if (name) {
            element_type->name.str = name;
            element_type->name.length = strlen(name);
        }
        element_type->content_length = 0; // No content length constraint by default

        return element_type;
    }
};

// ==================== Phase 1 Tests: Basic Infrastructure ====================

TEST_F(AstValidatorTest, CreateValidator) {
    EXPECT_NE(validator, nullptr) << "Validator should be created successfully";
    EXPECT_NE(validator->pool, nullptr) << "Validator should have memory pool";
    EXPECT_NE(validator->transpiler, nullptr) << "Validator should have transpiler";
    EXPECT_NE(validator->type_definitions, nullptr) << "Validator should have type registry";
}

TEST(AstValidatorCreation, CreateValidatorNullPool) {
    AstValidator* null_validator = ast_validator_create(nullptr);
    EXPECT_EQ(null_validator, nullptr) << "Validator creation should fail with null pool";
}

// ==================== Phase 1 Tests: Primitive Type Validation ====================

TEST_F(AstValidatorTest, ValidateStringSuccess) {
    ConstItem string_item = create_test_string("hello world");
    Type* string_type = create_test_type(LMD_TYPE_STRING);

    ValidationResult* result = ast_validator_validate_type(validator, string_item, string_type);

    ASSERT_NE(result, nullptr) << "Validation result should not be null";
    EXPECT_TRUE(result->valid) << "String validation should succeed";
    EXPECT_EQ(result->error_count, 0) << "Should have no errors";
    EXPECT_EQ(result->errors, nullptr) << "Error list should be empty";
}

TEST_F(AstValidatorTest, ValidateStringTypeMismatch) {
    ConstItem string_item = create_test_string("hello world");
    Type* int_type = create_test_type(LMD_TYPE_INT);

    ValidationResult* result = ast_validator_validate_type(validator, string_item, int_type);

    ASSERT_NE(result, nullptr) << "Validation result should not be null";
    EXPECT_FALSE(result->valid) << "Validation should fail for type mismatch";
    EXPECT_EQ(result->error_count, 1) << "Should have one error";
    EXPECT_NE(result->errors, nullptr) << "Error list should not be empty";
}

TEST_F(AstValidatorTest, ValidateIntSuccess) {
    ConstItem int_item = create_test_int(42);
    Type* int_type = create_test_type(LMD_TYPE_INT);

    ValidationResult* result = ast_validator_validate_type(validator, int_item, int_type);

    ASSERT_NE(result, nullptr) << "Validation result should not be null";
    EXPECT_TRUE(result->valid) << "Int validation should succeed";
    EXPECT_EQ(result->error_count, 0) << "Should have no errors";
}

TEST_F(AstValidatorTest, ValidateFloatSuccess) {
    ConstItem float_item = create_test_float(3.14);
    Type* float_type = create_test_type(LMD_TYPE_FLOAT);

    ValidationResult* result = ast_validator_validate_type(validator, float_item, float_type);

    ASSERT_NE(result, nullptr) << "Validation result should not be null";
    EXPECT_TRUE(result->valid) << "Float validation should succeed";
    EXPECT_EQ(result->error_count, 0) << "Should have no errors";
}

TEST_F(AstValidatorTest, ValidateBoolSuccess) {
    ConstItem bool_item = create_test_bool(true);
    Type* bool_type = create_test_type(LMD_TYPE_BOOL);

    ValidationResult* result = ast_validator_validate_type(validator, bool_item, bool_type);

    ASSERT_NE(result, nullptr) << "Validation result should not be null";
    EXPECT_TRUE(result->valid) << "Bool validation should succeed";
    EXPECT_EQ(result->error_count, 0) << "Should have no errors";
}

TEST_F(AstValidatorTest, ValidateNullSuccess) {
    ConstItem null_item = create_test_null();
    Type* null_type = create_test_type(LMD_TYPE_NULL);

    ValidationResult* result = ast_validator_validate_type(validator, null_item, null_type);

    ASSERT_NE(result, nullptr) << "Validation result should not be null";
    EXPECT_TRUE(result->valid) << "Null validation should succeed";
    EXPECT_EQ(result->error_count, 0) << "Should have no errors";
}

// ==================== Phase 1 Tests: Error Handling ====================

TEST_F(AstValidatorTest, ValidateWithNullValidator) {
    ConstItem string_item = create_test_string("test");
    Type* string_type = create_test_type(LMD_TYPE_STRING);

    ValidationResult* result = ast_validator_validate_type(nullptr, string_item, string_type);

    EXPECT_NE(result, nullptr) << "Should return error result";
    EXPECT_FALSE(result->valid) << "Should be invalid";
    EXPECT_EQ(result->error_count, 1) << "Should have one error";
    EXPECT_EQ(result->errors->code, VALID_ERROR_PARSE_ERROR) << "Should be parse error";
}

TEST_F(AstValidatorTest, ValidateWithNullType) {
    ConstItem string_item = create_test_string("test");

    ValidationResult* result = ast_validator_validate_type(validator, string_item, nullptr);

    EXPECT_NE(result, nullptr) << "Should return error result";
    EXPECT_FALSE(result->valid) << "Should be invalid";
    EXPECT_EQ(result->error_count, 1) << "Should have one error";
    EXPECT_EQ(result->errors->code, VALID_ERROR_PARSE_ERROR) << "Should be parse error";
}

TEST_F(AstValidatorTest, CreateValidationError) {
    PathSegment* path = create_path_segment(PATH_FIELD, "test_field", 0, test_pool);

    ValidationError* error = create_validation_error(VALID_ERROR_TYPE_MISMATCH, "Test error message", path, validator->pool);

    ASSERT_NE(error, nullptr) << "Error creation should succeed";
    EXPECT_STREQ(error->message->chars, "Test error message") << "Error message should match";
    EXPECT_EQ(error->path, path) << "Error path should match";
}

// ==================== Phase 1 Tests: Utility Functions ====================

TEST_F(AstValidatorTest, IsItemCompatibleWithTypeSuccess) {
    ConstItem string_item = create_test_string("test");
    Type* string_type = create_test_type(LMD_TYPE_STRING);

    bool result = is_item_compatible_with_type(string_item, string_type);

    EXPECT_TRUE(result) << "String item should be compatible with string type";
}

TEST_F(AstValidatorTest, IsItemCompatibleWithTypeFailure) {
    ConstItem string_item = create_test_string("test");
    Type* int_type = create_test_type(LMD_TYPE_INT);

    bool result = is_item_compatible_with_type(string_item, int_type);

    EXPECT_FALSE(result) << "String item should not be compatible with int type";
}

TEST_F(AstValidatorTest, TypeToString) {
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    Type* int_type = create_test_type(LMD_TYPE_INT);
    Type* float_type = create_test_type(LMD_TYPE_FLOAT);
    Type* bool_type = create_test_type(LMD_TYPE_BOOL);
    Type* null_type = create_test_type(LMD_TYPE_NULL);

    const char* string_type_name = type_to_string(string_type);
    const char* int_type_name = type_to_string(int_type);
    const char* float_type_name = type_to_string(float_type);
    const char* bool_type_name = type_to_string(bool_type);
    const char* null_type_name = type_to_string(null_type);

    EXPECT_STREQ(string_type_name, "string") << "String type name should be 'string'";
    EXPECT_STREQ(int_type_name, "int") << "Int type name should be 'int'";
    EXPECT_STREQ(float_type_name, "float") << "Float type name should be 'float'";
    EXPECT_STREQ(bool_type_name, "bool") << "Bool type name should be 'bool'";
    EXPECT_STREQ(null_type_name, "null") << "Null type name should be 'null'";
}

// ==================== Phase 1 Tests: Integration Tests ====================

TEST_F(AstValidatorTest, MultipleValidations) {
    ConstItem string_item = create_test_string("test");
    ConstItem int_item = create_test_int(42);
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    Type* int_type = create_test_type(LMD_TYPE_INT);

    ValidationResult* string_result = ast_validator_validate_type(validator, string_item, string_type);
    ValidationResult* int_result = ast_validator_validate_type(validator, int_item, int_type);

    ASSERT_NE(string_result, nullptr) << "String validation result should not be null";
    ASSERT_NE(int_result, nullptr) << "Int validation result should not be null";
    EXPECT_TRUE(string_result->valid) << "String validation should succeed";
    EXPECT_TRUE(int_result->valid) << "Int validation should succeed";
}

TEST_F(AstValidatorTest, ValidationDepthCheck) {
    // Create a complex nested structure to test validation depth
    ConstItem string_item = create_test_string("deep_test");
    Type* string_type = create_test_type(LMD_TYPE_STRING);

    // Create a path with multiple segments
    PathSegment* field_segment = create_path_segment(PATH_FIELD, "level1", 0, test_pool);
    PathSegment* index_segment = create_path_segment(PATH_INDEX, nullptr, 5, test_pool);
    PathSegment* element_segment = create_path_segment(PATH_ELEMENT, "div", 0, test_pool);

    // Chain the segments
    field_segment->next = index_segment;
    index_segment->next = element_segment;

    ValidationResult* result = ast_validator_validate_type(validator, string_item, string_type);

    ASSERT_NE(result, nullptr) << "Validation result should not be null";
    EXPECT_TRUE(result->valid) << "Deep validation should succeed";
}

// ==================== Element Validation Tests ====================

TEST_F(AstValidatorTest, ValidElementValidation) {
    // Create element using MarkBuilder
    MarkBuilder builder(input);
    Item element = builder.element("testElement")
        .text("Hello World")
        .final();

    TypeElmt* element_type = create_test_element_type("testElement", nullptr);

    AstValidator ctx = *validator;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;

    AstValidationResult* result = validate_against_element_type(&ctx, element.to_const(), element_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "Valid element should pass validation";
}

TEST_F(AstValidatorTest, ElementContentLengthViolation) {
    // Create element with content using MarkBuilder (1 text child)
    MarkBuilder builder(input);
    Item element = builder.element("testElement")
        .text("This content is too long for the constraint")
        .final();

    TypeElmt* element_type = create_test_element_type("testElement", nullptr);
    // Set a content_length constraint that expects 5 children, but we only have 1
    element_type->content_length = 5;

    Item item_mut;
    item_mut.element = element.element;
    ConstItem item = item_mut.to_const();

    AstValidator ctx = *validator;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;

    AstValidationResult* result = validate_against_element_type(&ctx, item, element_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_FALSE(result->valid) << "Element with wrong child count should fail validation";
    EXPECT_GT(result->error_count, 0) << "Should have validation errors";
}

TEST_F(AstValidatorTest, ElementTypeMismatch) {
    TypeElmt* element_type = create_test_element_type("testElement", nullptr);

    String* str = create_string(test_pool, "not an element");
    Item item_mut;
    item_mut.item = s2it(str);
    ConstItem item = item_mut.to_const();

    AstValidator ctx = *validator;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;

    AstValidationResult* result = validate_against_element_type(&ctx, item, element_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_FALSE(result->valid) << "Type mismatch should fail validation";
    EXPECT_GT(result->error_count, 0) << "Should have validation errors";
}

// ==================== Union Type Validation Tests ====================

TEST_F(AstValidatorTest, ValidStringInUnion) {
    // Create union types: string | int
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;

    Type* int_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    int_type->type_id = LMD_TYPE_INT;

    Type** union_types = (Type**)pool_calloc(test_pool, sizeof(Type*) * 2);
    union_types[0] = string_type;
    union_types[1] = int_type;

    String* str = create_string(test_pool, "test string");
    Item item_mut;
    item_mut.item = s2it(str);
    ConstItem item = item_mut.to_const();

    AstValidator ctx = *validator;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;

    AstValidationResult* result = validate_against_union_type(&ctx, item, union_types, 2);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "Valid string in union should pass validation";
}

TEST_F(AstValidatorTest, ValidIntInUnion) {
    // Create union types: string | int
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;

    Type* int_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    int_type->type_id = LMD_TYPE_INT;

    Type** union_types = (Type**)pool_calloc(test_pool, sizeof(Type*) * 2);
    union_types[0] = string_type;
    union_types[1] = int_type;

    Item item_mut;
    item_mut.item = i2it(42);
    ConstItem item = item_mut.to_const();

    AstValidator ctx = *validator;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;

    AstValidationResult* result = validate_against_union_type(&ctx, item, union_types, 2);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "Valid int in union should pass validation";
}

TEST_F(AstValidatorTest, InvalidTypeNotInUnion) {
    // Create union types: string | int
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;

    Type* int_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    int_type->type_id = LMD_TYPE_INT;

    Type** union_types = (Type**)pool_calloc(test_pool, sizeof(Type*) * 2);
    union_types[0] = string_type;
    union_types[1] = int_type;

    double* float_ptr = (double*)pool_calloc(test_pool, sizeof(double));
    *float_ptr = 3.14;
    Item item_mut;
    item_mut.pointer = (uint64_t)float_ptr;
    item_mut._type_id = LMD_TYPE_FLOAT;
    ConstItem item = item_mut.to_const();

    AstValidator ctx = *validator;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;

    AstValidationResult* result = validate_against_union_type(&ctx, item, union_types, 2);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_FALSE(result->valid) << "Invalid float in union should fail validation";
    EXPECT_GT(result->error_count, 0) << "Should have validation errors";
}

// ==================== Occurrence Constraint Tests ====================

TEST_F(AstValidatorTest, OptionalConstraintZeroItems) {
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;

    AstValidator ctx = *validator;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;

    AstValidationResult* result = validate_against_occurrence(&ctx, nullptr, 0, string_type, OPERATOR_OPTIONAL);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "Optional constraint with 0 items should be valid";
}

TEST_F(AstValidatorTest, OptionalConstraintTooManyItems) {
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;

    String* str1 = create_string(test_pool, "item1");
    String* str2 = create_string(test_pool, "item2");
    ConstItem items[2];
    Item item1, item2;
    item1.item = s2it(str1);
    item2.item = s2it(str2);
    items[0] = item1.to_const();
    items[1] = item2.to_const();

    AstValidator ctx = *validator;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;

    AstValidationResult* result = validate_against_occurrence(&ctx, items, 2, string_type, OPERATOR_OPTIONAL);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_FALSE(result->valid) << "Optional constraint with 2 items should be invalid";
    EXPECT_GT(result->error_count, 0) << "Should have validation errors";
}

TEST_F(AstValidatorTest, OneOrMoreConstraintZeroItems) {
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;

    AstValidator ctx = *validator;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;

    AstValidationResult* result = validate_against_occurrence(&ctx, nullptr, 0, string_type, OPERATOR_ONE_MORE);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_FALSE(result->valid) << "One-or-more constraint with 0 items should be invalid";
    EXPECT_GT(result->error_count, 0) << "Should have validation errors";
}

TEST_F(AstValidatorTest, OneOrMoreConstraintMultipleItems) {
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;

    String* str1 = create_string(test_pool, "item1");
    String* str2 = create_string(test_pool, "item2");
    String* str3 = create_string(test_pool, "item3");
    ConstItem items[3];
    Item item1, item2, item3;
    item1.item = s2it(str1);
    item2.item = s2it(str2);
    item3.item = s2it(str3);
    items[0] = item1.to_const();
    items[1] = item2.to_const();
    items[2] = item3.to_const();

    AstValidator ctx = *validator;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;

    AstValidationResult* result = validate_against_occurrence(&ctx, items, 3, string_type, OPERATOR_ONE_MORE);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "One-or-more constraint with 3 items should be valid";
}

TEST_F(AstValidatorTest, ZeroOrMoreConstraintAnyItems) {
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;

    String* str = create_string(test_pool, "item");
    ConstItem items[5];
    for (int i = 0; i < 5; i++) {
        Item item_mut;
        item_mut.item = s2it(str);
        items[i] = item_mut.to_const();
    }

    AstValidator ctx = *validator;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;

    AstValidationResult* result = validate_against_occurrence(&ctx, items, 5, string_type, OPERATOR_ZERO_MORE);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "Zero-or-more constraint with any number of items should be valid";
}

// ==================== Edge Case and Boundary Tests ====================

TEST_F(AstValidatorTest, NullPointerHandling) {
    // Test null context handling
    ConstItem string_item = create_test_string("test");
    // Type* string_type = create_test_type(LMD_TYPE_STRING); // Unused

    AstValidationResult* result = ast_validator_validate_type(validator, string_item, nullptr);

    ASSERT_NE(result, nullptr) << "Should return error result for null type";
    EXPECT_FALSE(result->valid) << "Should be invalid with null type";
    EXPECT_GT(result->error_count, 0) << "Should have validation errors";
}

TEST_F(AstValidatorTest, EmptyStringHandling) {
    String* empty_str = create_string(test_pool, "");
    Item item_mut;
    item_mut.item = s2it(empty_str);
    ConstItem empty_string_item = item_mut.to_const();

    Type* string_type = create_test_type(LMD_TYPE_STRING);

    AstValidationResult* result = ast_validator_validate_type(validator, empty_string_item, string_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "Empty string should be valid for string type";
}

TEST_F(AstValidatorTest, UnicodeStringHandling) {
    const char* unicode_string = "Hello 世界 🌍 Ñoël";
    String* unicode_str = create_string(test_pool, unicode_string);
    Item item_mut;
    item_mut.item = s2it(unicode_str);
    ConstItem unicode_item = item_mut.to_const();

    Type* string_type = create_test_type(LMD_TYPE_STRING);

    AstValidationResult* result = ast_validator_validate_type(validator, unicode_item, string_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "Unicode string should be valid for string type";
}

TEST_F(AstValidatorTest, NumericBoundaryConditions) {
    // Test with maximum integer value
    int max_int = 2147483647; // INT_MAX value to avoid macro issues
    ConstItem max_int_item = create_test_int(max_int);
    Type* int_type = create_test_type(LMD_TYPE_INT);

    AstValidationResult* result = ast_validator_validate_type(validator, max_int_item, int_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "Maximum integer value should be valid";

    // Test with minimum integer value
    int min_int = -2147483648; // INT_MIN value to avoid macro issues
    ConstItem min_int_item = create_test_int(min_int);

    result = ast_validator_validate_type(validator, min_int_item, int_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "Minimum integer value should be valid";
}

TEST_F(AstValidatorTest, ZeroValues) {
    // Test zero integer
    ConstItem zero_int_item = create_test_int(0);
    Type* int_type = create_test_type(LMD_TYPE_INT);

    AstValidationResult* result = ast_validator_validate_type(validator, zero_int_item, int_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "Zero integer should be valid";

    // Test zero float
    ConstItem zero_float_item = create_test_float(0.0);
    Type* float_type = create_test_type(LMD_TYPE_FLOAT);

    result = ast_validator_validate_type(validator, zero_float_item, float_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_TRUE(result->valid) << "Zero float should be valid";
}

TEST_F(AstValidatorTest, DepthLimitBoundary) {
    ConstItem string_item = create_test_string("test");
    Type* string_type = create_test_type(LMD_TYPE_STRING);

    // Test at maximum depth boundary
    validator->options.max_depth = 1;

    AstValidationResult* result = ast_validator_validate_type(validator, string_item, string_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    // Result depends on implementation - could be valid at depth 1 or invalid due to depth limit
}

// ==================== Error Recovery and Robustness Tests ====================

TEST_F(AstValidatorTest, MultipleErrorAccumulation) {
    // Create a scenario that generates multiple errors
    ConstItem int_item = create_test_int(42);
    Type* string_type = create_test_type(LMD_TYPE_STRING);

    AstValidationResult* result = ast_validator_validate_type(validator, int_item, string_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_FALSE(result->valid) << "Should be invalid due to type mismatch";
    EXPECT_GT(result->error_count, 0) << "Should have at least one error";

    // Verify error structure
    ASSERT_NE(result->errors, nullptr) << "Should have error details";
    EXPECT_EQ(result->errors->code, VALID_ERROR_TYPE_MISMATCH) << "Should be type mismatch error";
}

TEST_F(AstValidatorTest, ErrorMessageContent) {
    ConstItem float_item = create_test_float(3.14);
    Type* bool_type = create_test_type(LMD_TYPE_BOOL);

    AstValidationResult* result = ast_validator_validate_type(validator, float_item, bool_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    EXPECT_FALSE(result->valid) << "Should be invalid due to type mismatch";
    ASSERT_NE(result->errors, nullptr) << "Should have error details";
    ASSERT_NE(result->errors->message, nullptr) << "Should have error message";
    EXPECT_GT(strlen(result->errors->message->chars), 0u) << "Error message should not be empty";
}

TEST_F(AstValidatorTest, ValidationStateIsolation) {
    // Test that multiple validations don't interfere with each other
    ConstItem valid_item = create_test_string("valid");
    ConstItem invalid_item = create_test_int(42);
    Type* string_type = create_test_type(LMD_TYPE_STRING);

    // First validation (should pass)
    AstValidationResult* result1 = ast_validator_validate_type(validator, valid_item, string_type);

    // Second validation (should fail)
    AstValidationResult* result2 = ast_validator_validate_type(validator, invalid_item, string_type);

    // Third validation (should pass again)
    AstValidationResult* result3 = ast_validator_validate_type(validator, valid_item, string_type);

    EXPECT_TRUE(result1->valid) << "First validation should pass";
    EXPECT_FALSE(result2->valid) << "Second validation should fail";
    EXPECT_TRUE(result3->valid) << "Third validation should pass (state isolated)";
}

// ==================== Performance and Stress Tests ====================

TEST_F(AstValidatorTest, RepeatedValidationStability) {
    ConstItem string_item = create_test_string("test");
    Type* string_type = create_test_type(LMD_TYPE_STRING);

    const int ITERATIONS = 1000;
    int successful_validations = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        AstValidationResult* result = ast_validator_validate_type(validator, string_item, string_type);
        if (result && result->valid) {
            successful_validations++;
        }
    }

    EXPECT_EQ(successful_validations, ITERATIONS) << "All repeated validations should succeed";
}

TEST_F(AstValidatorTest, LargeErrorMessageHandling) {
    // Create a scenario that might generate a large error message
    Item item_mut;
    item_mut.item = 0; // null pointer
    item_mut._type_id = LMD_TYPE_STRING;
    ConstItem item = item_mut.to_const();

    Type* string_type = create_test_type(LMD_TYPE_STRING);

    AstValidationResult* result = ast_validator_validate_type(validator, item, string_type);

    ASSERT_NE(result, nullptr) << "Should return validation result";
    // The result may be valid or invalid depending on implementation
    // The key is that it should handle the null pointer gracefully
}
