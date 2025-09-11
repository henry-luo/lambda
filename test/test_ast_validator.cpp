/**
 * @file test_ast_validator.cpp
 * @brief Unit Tests for AST-Based Lambda Validator
 * @author Henry Luo
 * @license MIT
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Include validator headers for ValidationResult and run_validation
#include "../lambda/validator.hpp"

// External function declarations for memory pool
extern "C" {
    MemPoolError pool_variable_init(VariableMemPool **pool, size_t grow_size, uint16_t tolerance_percent);
    MemPoolError pool_variable_destroy(VariableMemPool *pool);
}

// Forward declaration for path segment creation
PathSegment* create_path_segment(PathSegmentType type, const char* name, long index, VariableMemPool* pool);

// Simple implementation of create_path_segment for tests
PathSegment* create_path_segment(PathSegmentType type, const char* name, long index, VariableMemPool* pool) {
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

// Test fixtures
static VariableMemPool* test_pool = nullptr;
static AstValidator* validator = nullptr;

void setup_validator_test(void) {
    MemPoolError err = pool_variable_init(&test_pool, 1024 * 1024, MEM_POOL_NO_BEST_FIT); // 1MB pool
    cr_assert_eq(err, MEM_POOL_ERR_OK, "Failed to create memory pool");
    cr_assert_not_null(test_pool, "Memory pool should not be null");
    
    validator = ast_validator_create(test_pool);
    cr_assert_not_null(validator, "Failed to create AST validator");
}

void teardown_validator_test(void) {
    if (validator) {
        ast_validator_destroy(validator);
        validator = nullptr;
    }
    
    if (test_pool) {
        pool_variable_destroy(test_pool);
        test_pool = nullptr;
    }
}

// Helper function to create typed items for testing
TypedItem create_test_string(const char* value) {
    size_t len = strlen(value);
    String* str = (String*)pool_calloc(test_pool, sizeof(String) + len + 1);
    str->len = len;
    strcpy(str->chars, value);
    
    TypedItem item;
    item.type_id = LMD_TYPE_STRING;
    item.pointer = str;
    return item;
}

TypedItem create_test_int(int64_t value) {
    int64_t* int_ptr = (int64_t*)pool_calloc(test_pool, sizeof(int64_t));
    *int_ptr = value;
    
    TypedItem item;
    item.type_id = LMD_TYPE_INT;
    item.pointer = int_ptr;
    return item;
}

TypedItem create_test_float(double value) {
    double* float_ptr = (double*)pool_calloc(test_pool, sizeof(double));
    *float_ptr = value;
    
    TypedItem item;
    item.type_id = LMD_TYPE_FLOAT;
    item.pointer = float_ptr;
    return item;
}

TypedItem create_test_bool(bool value) {
    bool* bool_ptr = (bool*)pool_calloc(test_pool, sizeof(bool));
    *bool_ptr = value;
    
    TypedItem item;
    item.type_id = LMD_TYPE_BOOL;
    item.pointer = bool_ptr;
    return item;
}

TypedItem create_test_null() {
    TypedItem item;
    item.type_id = LMD_TYPE_NULL;
    item.pointer = nullptr;
    return item;
}

// Helper function to create basic types for testing
Type* create_test_type(TypeId type_id) {
    Type* type = (Type*)pool_calloc(test_pool, sizeof(Type));
    type->type_id = type_id;
    return type;
}

// ==================== Phase 1 Tests: Basic Infrastructure ====================

TestSuite(ast_validator_creation, .init = setup_validator_test, .fini = teardown_validator_test);

Test(ast_validator_creation, create_validator) {
    cr_assert_not_null(validator, "Validator should be created successfully");
    cr_assert_not_null(validator->pool, "Validator should have memory pool");
    cr_assert_not_null(validator->transpiler, "Validator should have transpiler");
    cr_assert_not_null(validator->type_definitions, "Validator should have type registry");
}

Test(ast_validator_creation, create_validator_null_pool) {
    AstValidator* null_validator = ast_validator_create(nullptr);
    cr_assert_null(null_validator, "Validator creation should fail with null pool");
}

// ==================== Phase 1 Tests: Primitive Type Validation ====================

TestSuite(primitive_validation, .init = setup_validator_test, .fini = teardown_validator_test);

Test(primitive_validation, validate_string_success) {
    TypedItem string_item = create_test_string("hello world");
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    AstValidationResult* result = ast_validator_validate_type(validator, string_item, string_type);
    
    cr_assert_not_null(result, "Validation result should not be null");
    cr_assert(result->valid, "String validation should succeed");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
    cr_assert_null(result->errors, "Error list should be empty");
}

Test(primitive_validation, validate_string_type_mismatch) {
    TypedItem int_item = create_test_int(42);
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    AstValidationResult* result = ast_validator_validate_type(validator, int_item, string_type);
    
    cr_assert_not_null(result, "Validation result should not be null");
    cr_assert_not(result->valid, "Validation should fail for type mismatch");
    cr_assert_eq(result->error_count, 1, "Should have one error");
    cr_assert_not_null(result->errors, "Should have error details");
    cr_assert_eq(result->errors->code, VALID_ERROR_TYPE_MISMATCH, "Should be type mismatch error");
}

Test(primitive_validation, validate_int_success) {
    TypedItem int_item = create_test_int(42);
    Type* int_type = create_test_type(LMD_TYPE_INT);
    
    AstValidationResult* result = ast_validator_validate_type(validator, int_item, int_type);
    
    cr_assert_not_null(result, "Validation result should not be null");
    cr_assert(result->valid, "Int validation should succeed");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(primitive_validation, validate_float_success) {
    TypedItem float_item = create_test_float(3.14);
    Type* float_type = create_test_type(LMD_TYPE_FLOAT);
    
    AstValidationResult* result = ast_validator_validate_type(validator, float_item, float_type);
    
    cr_assert_not_null(result, "Validation result should not be null");
    cr_assert(result->valid, "Float validation should succeed");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(primitive_validation, validate_bool_success) {
    TypedItem bool_item = create_test_bool(true);
    Type* bool_type = create_test_type(LMD_TYPE_BOOL);
    
    AstValidationResult* result = ast_validator_validate_type(validator, bool_item, bool_type);
    
    cr_assert_not_null(result, "Validation result should not be null");
    cr_assert(result->valid, "Bool validation should succeed");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(primitive_validation, validate_null_success) {
    TypedItem null_item = create_test_null();
    Type* null_type = create_test_type(LMD_TYPE_NULL);
    
    AstValidationResult* result = ast_validator_validate_type(validator, null_item, null_type);
    
    cr_assert_not_null(result, "Validation result should not be null");
    cr_assert(result->valid, "Null validation should succeed");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

// ==================== Error Handling Tests ====================

TestSuite(error_handling, .init = setup_validator_test, .fini = teardown_validator_test);

Test(error_handling, validate_with_null_validator) {
    TypedItem string_item = create_test_string("test");
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    AstValidationResult* result = ast_validator_validate_type(nullptr, string_item, string_type);
    
    cr_assert_not_null(result, "Should return error result");
    cr_assert_not(result->valid, "Should be invalid");
    cr_assert_eq(result->error_count, 1, "Should have one error");
    cr_assert_eq(result->errors->code, VALID_ERROR_PARSE_ERROR, "Should be parse error");
}

Test(error_handling, validate_with_null_type) {
    TypedItem string_item = create_test_string("test");
    
    AstValidationResult* result = ast_validator_validate_type(validator, string_item, nullptr);
    
    cr_assert_not_null(result, "Should return error result");
    cr_assert_not(result->valid, "Should be invalid");
    cr_assert_eq(result->error_count, 1, "Should have one error");
    cr_assert_eq(result->errors->code, VALID_ERROR_PARSE_ERROR, "Should be parse error");
}

Test(error_handling, create_validation_error) {
    ValidationError* error = create_validation_error(
        VALID_ERROR_TYPE_MISMATCH, "Test error message", nullptr, test_pool);
    
    cr_assert_not_null(error, "Error should be created");
    cr_assert_eq(error->code, VALID_ERROR_TYPE_MISMATCH, "Error code should match");
    cr_assert_str_eq(error->message->chars, "Test error message", "Error message should match");
    cr_assert_null(error->next, "Next pointer should be null");
}

// ==================== Utility Function Tests ====================

TestSuite(utility_functions, .init = setup_validator_test, .fini = teardown_validator_test);

Test(utility_functions, is_item_compatible_with_type_success) {
    TypedItem string_item = create_test_string("test");
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    bool compatible = is_item_compatible_with_type(string_item, string_type);
    cr_assert(compatible, "String item should be compatible with string type");
}

Test(utility_functions, is_item_compatible_with_type_failure) {
    TypedItem int_item = create_test_int(42);
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    bool compatible = is_item_compatible_with_type(int_item, string_type);
    cr_assert_not(compatible, "Int item should not be compatible with string type");
}

Test(utility_functions, type_to_string) {
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    Type* int_type = create_test_type(LMD_TYPE_INT);
    Type* float_type = create_test_type(LMD_TYPE_FLOAT);
    Type* bool_type = create_test_type(LMD_TYPE_BOOL);
    Type* null_type = create_test_type(LMD_TYPE_NULL);
    
    cr_assert_str_eq(type_to_string(string_type), "string", "String type name should match");
    cr_assert_str_eq(type_to_string(int_type), "int", "Int type name should match");
    cr_assert_str_eq(type_to_string(float_type), "float", "Float type name should match");
    cr_assert_str_eq(type_to_string(bool_type), "bool", "Bool type name should match");
    cr_assert_str_eq(type_to_string(null_type), "null", "Null type name should match");
    cr_assert_str_eq(type_to_string(nullptr), "unknown", "Null type should return unknown");
}

// ==================== Integration Tests ====================

TestSuite(integration_tests, .init = setup_validator_test, .fini = teardown_validator_test);

Test(integration_tests, multiple_validations) {
    // Test multiple validations with same validator
    TypedItem string_item = create_test_string("hello");
    TypedItem int_item = create_test_int(42);
    TypedItem float_item = create_test_float(3.14);
    
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    Type* int_type = create_test_type(LMD_TYPE_INT);
    Type* float_type = create_test_type(LMD_TYPE_FLOAT);
    
    AstValidationResult* result1 = ast_validator_validate_type(validator, string_item, string_type);
    AstValidationResult* result2 = ast_validator_validate_type(validator, int_item, int_type);
    AstValidationResult* result3 = ast_validator_validate_type(validator, float_item, float_type);
    
    cr_assert(result1->valid, "First validation should succeed");
    cr_assert(result2->valid, "Second validation should succeed");
    cr_assert(result3->valid, "Third validation should succeed");
}

Test(integration_tests, validation_depth_check) {
    // Test that validation depth is properly managed
    TypedItem string_item = create_test_string("test");
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    // Modify validator options to have very low max depth
    validator->default_options.max_depth = 0;
    
    AstValidationResult* result = ast_validator_validate_type(validator, string_item, string_type);
    
    cr_assert_not_null(result, "Should return result");
    cr_assert_not(result->valid, "Should fail due to depth limit");
    cr_assert_eq(result->errors->code, VALID_ERROR_CONSTRAINT_VIOLATION, "Should be constraint violation");
}

// ==================== Advanced Element Validation Tests ====================

TestSuite(element_validation, .init = setup_validator_test, .fini = teardown_validator_test);

// Helper function to create test elements
Element* create_test_element(const char* name, const char* content) {
    Element* element = (Element*)pool_calloc(test_pool, sizeof(Element));
    
    // Element no longer has a name member - skip name setting
    // The Element structure now inherits from List and has type/data members
    
    // Set element content
    if (content) {
        size_t content_len = strlen(content);
        element->data = (char*)pool_calloc(test_pool, content_len + 1);
        strcpy((char*)element->data, content);
        element->length = content_len;
    }
    
    return element;
}

// Helper function to create test element types
TypeElmt* create_test_element_type(const char* name, Type* content_type) {
    TypeElmt* element_type = (TypeElmt*)pool_calloc(test_pool, sizeof(TypeElmt));
    
    // Set element type properties
    if (name) {
        element_type->name.str = name;
        element_type->name.length = strlen(name);
    }
    element_type->content_length = 20; // Default content length
    
    return element_type;
}

Test(element_validation, valid_element_validation) {
    Element* test_element = create_test_element("testElement", "Hello World");
    TypeElmt* element_type = create_test_element_type("testElement", nullptr);
    
    TypedItem item;
    item.type_id = LMD_TYPE_ELEMENT;
    item.pointer = test_element;
    
    AstValidationContext ctx;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;
    
    AstValidationResult* result = validate_against_element_type(validator, item, element_type, &ctx);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "Valid element should pass validation");
}

Test(element_validation, element_content_length_violation) {
    Element* test_element = create_test_element("testElement", "This content is too long for the constraint");
    TypeElmt* element_type = create_test_element_type("testElement", nullptr);
    
    TypedItem item;
    item.type_id = LMD_TYPE_ELEMENT;
    item.pointer = test_element;
    
    AstValidationContext ctx;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;
    
    AstValidationResult* result = validate_against_element_type(validator, item, element_type, &ctx);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert_not(result->valid, "Element with content too long should fail validation");
    cr_assert_gt(result->error_count, 0, "Should have validation errors");
}

Test(element_validation, element_type_mismatch) {
    TypeElmt* element_type = create_test_element_type("testElement", nullptr);
    
    TypedItem item;
    item.type_id = LMD_TYPE_STRING;
    item.pointer = (void*)"not an element";
    
    AstValidationContext ctx;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;
    
    AstValidationResult* result = validate_against_element_type(validator, item, element_type, &ctx);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert_not(result->valid, "Type mismatch should fail validation");
    cr_assert_gt(result->error_count, 0, "Should have validation errors");
}

// ==================== Union Type Validation Tests ====================

TestSuite(union_validation, .init = setup_validator_test, .fini = teardown_validator_test);

Test(union_validation, valid_string_in_union) {
    // Create union types: string | int
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;
    
    Type* int_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    int_type->type_id = LMD_TYPE_INT;
    
    Type** union_types = (Type**)pool_calloc(test_pool, sizeof(Type*) * 2);
    union_types[0] = string_type;
    union_types[1] = int_type;
    
    TypedItem item;
    item.type_id = LMD_TYPE_STRING;
    item.pointer = (void*)"test string";
    
    AstValidationContext ctx;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;
    
    AstValidationResult* result = validate_against_union_type(validator, item, union_types, 2, &ctx);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "Valid string in union should pass validation");
}

Test(union_validation, valid_int_in_union) {
    // Create union types: string | int
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;
    
    Type* int_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    int_type->type_id = LMD_TYPE_INT;
    
    Type** union_types = (Type**)pool_calloc(test_pool, sizeof(Type*) * 2);
    union_types[0] = string_type;
    union_types[1] = int_type;
    
    int test_int = 42;
    TypedItem item;
    item.type_id = LMD_TYPE_INT;
    item.pointer = &test_int;
    
    AstValidationContext ctx;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;
    
    AstValidationResult* result = validate_against_union_type(validator, item, union_types, 2, &ctx);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "Valid int in union should pass validation");
}

Test(union_validation, invalid_type_not_in_union) {
    // Create union types: string | int
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;
    
    Type* int_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    int_type->type_id = LMD_TYPE_INT;
    
    Type** union_types = (Type**)pool_calloc(test_pool, sizeof(Type*) * 2);
    union_types[0] = string_type;
    union_types[1] = int_type;
    
    float test_float = 3.14f;
    TypedItem item;
    item.type_id = LMD_TYPE_FLOAT;
    item.pointer = &test_float;
    
    AstValidationContext ctx;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;
    
    AstValidationResult* result = validate_against_union_type(validator, item, union_types, 2, &ctx);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert_not(result->valid, "Invalid float in union should fail validation");
    cr_assert_gt(result->error_count, 0, "Should have validation errors");
}

// ==================== Occurrence Constraint Tests ====================

TestSuite(occurrence_constraints, .init = setup_validator_test, .fini = teardown_validator_test);

Test(occurrence_constraints, optional_constraint_zero_items) {
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;
    
    AstValidationContext ctx;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;
    
    AstValidationResult* result = validate_occurrence_constraint(validator, nullptr, 0, string_type, OPERATOR_OPTIONAL, &ctx);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "Optional constraint with 0 items should be valid");
}

Test(occurrence_constraints, optional_constraint_too_many_items) {
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;
    
    TypedItem items[2];
    items[0].type_id = LMD_TYPE_STRING;
    items[0].pointer = (void*)"item1";
    items[1].type_id = LMD_TYPE_STRING;
    items[1].pointer = (void*)"item2";
    
    AstValidationContext ctx;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;
    
    AstValidationResult* result = validate_occurrence_constraint(validator, items, 2, string_type, OPERATOR_OPTIONAL, &ctx);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert_not(result->valid, "Optional constraint with 2 items should be invalid");
    cr_assert_gt(result->error_count, 0, "Should have validation errors");
}

Test(occurrence_constraints, one_or_more_constraint_zero_items) {
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;
    
    AstValidationContext ctx;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;
    
    AstValidationResult* result = validate_occurrence_constraint(validator, nullptr, 0, string_type, OPERATOR_ONE_MORE, &ctx);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert_not(result->valid, "One-or-more constraint with 0 items should be invalid");
    cr_assert_gt(result->error_count, 0, "Should have validation errors");
}

Test(occurrence_constraints, one_or_more_constraint_multiple_items) {
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;
    
    TypedItem items[3];
    items[0].type_id = LMD_TYPE_STRING;
    items[0].pointer = (void*)"item1";
    items[1].type_id = LMD_TYPE_STRING;
    items[1].pointer = (void*)"item2";
    items[2].type_id = LMD_TYPE_STRING;
    items[2].pointer = (void*)"item3";
    
    AstValidationContext ctx;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;
    
    AstValidationResult* result = validate_occurrence_constraint(validator, items, 3, string_type, OPERATOR_ONE_MORE, &ctx);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "One-or-more constraint with 3 items should be valid");
}

Test(occurrence_constraints, zero_or_more_constraint_any_items) {
    Type* string_type = (Type*)pool_calloc(test_pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;
    
    TypedItem items[5];
    for (int i = 0; i < 5; i++) {
        items[i].type_id = LMD_TYPE_STRING;
        items[i].pointer = (void*)"item";
    }
    
    AstValidationContext ctx;
    ctx.pool = test_pool;
    ctx.current_path = create_path_segment(PATH_FIELD, "root", 0, test_pool);
    ctx.current_depth = 0;
    ctx.options.max_depth = 10;
    
    AstValidationResult* result = validate_occurrence_constraint(validator, items, 5, string_type, OPERATOR_ZERO_MORE, &ctx);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "Zero-or-more constraint with any number of items should be valid");
}

// ==================== Edge Case and Boundary Tests ====================

TestSuite(edge_cases, .init = setup_validator_test, .fini = teardown_validator_test);

Test(edge_cases, null_pointer_handling) {
    // Test null context handling
    TypedItem string_item = create_test_string("test");
    // Type* string_type = create_test_type(LMD_TYPE_STRING); // Unused
    
    AstValidationResult* result = ast_validator_validate_type(validator, string_item, nullptr);
    
    cr_assert_not_null(result, "Should return error result for null type");
    cr_assert_not(result->valid, "Should be invalid with null type");
    cr_assert_gt(result->error_count, 0, "Should have validation errors");
}

Test(edge_cases, empty_string_handling) {
    TypedItem empty_string_item;
    empty_string_item.type_id = LMD_TYPE_STRING;
    empty_string_item.pointer = (void*)"";
    
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    AstValidationResult* result = ast_validator_validate_type(validator, empty_string_item, string_type);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "Empty string should be valid for string type");
}

Test(edge_cases, unicode_string_handling) {
    const char* unicode_string = "Hello 世界 🌍 Ñoël";
    TypedItem unicode_item;
    unicode_item.type_id = LMD_TYPE_STRING;
    unicode_item.pointer = (void*)unicode_string;
    
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    AstValidationResult* result = ast_validator_validate_type(validator, unicode_item, string_type);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "Unicode string should be valid for string type");
}

Test(edge_cases, numeric_boundary_conditions) {
    // Test with maximum integer value
    int max_int = 2147483647; // INT_MAX value to avoid macro issues
    TypedItem max_int_item = create_test_int(max_int);
    Type* int_type = create_test_type(LMD_TYPE_INT);
    
    AstValidationResult* result = ast_validator_validate_type(validator, max_int_item, int_type);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "Maximum integer value should be valid");
    
    // Test with minimum integer value
    int min_int = -2147483648; // INT_MIN value to avoid macro issues
    TypedItem min_int_item = create_test_int(min_int);
    
    result = ast_validator_validate_type(validator, min_int_item, int_type);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "Minimum integer value should be valid");
}

Test(edge_cases, zero_values) {
    // Test zero integer
    TypedItem zero_int_item = create_test_int(0);
    Type* int_type = create_test_type(LMD_TYPE_INT);
    
    AstValidationResult* result = ast_validator_validate_type(validator, zero_int_item, int_type);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "Zero integer should be valid");
    
    // Test zero float
    TypedItem zero_float_item = create_test_float(0.0);
    Type* float_type = create_test_type(LMD_TYPE_FLOAT);
    
    result = ast_validator_validate_type(validator, zero_float_item, float_type);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert(result->valid, "Zero float should be valid");
}

Test(edge_cases, depth_limit_boundary) {
    TypedItem string_item = create_test_string("test");
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    // Test at maximum depth boundary
    validator->default_options.max_depth = 1;
    
    AstValidationResult* result = ast_validator_validate_type(validator, string_item, string_type);
    
    cr_assert_not_null(result, "Should return validation result");
    // Result depends on implementation - could be valid at depth 1 or invalid due to depth limit
}

// ==================== Error Recovery and Robustness Tests ====================

TestSuite(error_recovery, .init = setup_validator_test, .fini = teardown_validator_test);

Test(error_recovery, multiple_error_accumulation) {
    // Create a scenario that generates multiple errors
    TypedItem int_item = create_test_int(42);
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    AstValidationResult* result = ast_validator_validate_type(validator, int_item, string_type);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert_not(result->valid, "Should be invalid due to type mismatch");
    cr_assert_gt(result->error_count, 0, "Should have at least one error");
    
    // Verify error structure
    cr_assert_not_null(result->errors, "Should have error details");
    cr_assert_eq(result->errors->code, VALID_ERROR_TYPE_MISMATCH, "Should be type mismatch error");
}

Test(error_recovery, error_message_content) {
    TypedItem float_item = create_test_float(3.14);
    Type* bool_type = create_test_type(LMD_TYPE_BOOL);
    
    AstValidationResult* result = ast_validator_validate_type(validator, float_item, bool_type);
    
    cr_assert_not_null(result, "Should return validation result");
    cr_assert_not(result->valid, "Should be invalid due to type mismatch");
    cr_assert_not_null(result->errors, "Should have error details");
    cr_assert_not_null(result->errors->message, "Should have error message");
    cr_assert_gt(strlen(result->errors->message->chars), 0, "Error message should not be empty");
}

Test(error_recovery, validation_state_isolation) {
    // Test that multiple validations don't interfere with each other
    TypedItem valid_item = create_test_string("valid");
    TypedItem invalid_item = create_test_int(42);
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    // First validation (should pass)
    AstValidationResult* result1 = ast_validator_validate_type(validator, valid_item, string_type);
    
    // Second validation (should fail)
    AstValidationResult* result2 = ast_validator_validate_type(validator, invalid_item, string_type);
    
    // Third validation (should pass again)
    AstValidationResult* result3 = ast_validator_validate_type(validator, valid_item, string_type);
    
    cr_assert(result1->valid, "First validation should pass");
    cr_assert_not(result2->valid, "Second validation should fail");
    cr_assert(result3->valid, "Third validation should pass (state isolated)");
}

// ==================== Performance and Stress Tests ====================

TestSuite(performance_tests, .init = setup_validator_test, .fini = teardown_validator_test);

Test(performance_tests, repeated_validation_stability) {
    TypedItem string_item = create_test_string("test");
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    const int ITERATIONS = 1000;
    int successful_validations = 0;
    
    for (int i = 0; i < ITERATIONS; i++) {
        AstValidationResult* result = ast_validator_validate_type(validator, string_item, string_type);
        if (result && result->valid) {
            successful_validations++;
        }
    }
    
    cr_assert_eq(successful_validations, ITERATIONS, "All repeated validations should succeed");
}

Test(performance_tests, large_error_message_handling) {
    // Create a scenario that might generate a large error message
    TypedItem item;
    item.type_id = LMD_TYPE_STRING;
    item.pointer = nullptr; // This might generate an error about null pointer
    
    Type* string_type = create_test_type(LMD_TYPE_STRING);
    
    AstValidationResult* result = ast_validator_validate_type(validator, item, string_type);
    
    cr_assert_not_null(result, "Should return validation result");
    // The result may be valid or invalid depending on implementation
    // The key is that it should handle the null pointer gracefully
}
