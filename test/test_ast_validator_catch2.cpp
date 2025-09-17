/**
 * @file test_ast_validator_catch2.cpp
 * @brief Unit Tests for AST-Based Lambda Validator using Catch2
 * @author Henry Luo
 * @license MIT
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>

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

void setup_validator_test() {
    MemPoolError err = pool_variable_init(&test_pool, 1024 * 1024, MEM_POOL_NO_BEST_FIT); // 1MB pool
    REQUIRE(err == MEM_POOL_ERR_OK);
    REQUIRE(test_pool != nullptr);
    
    validator = ast_validator_create(test_pool);
    REQUIRE(validator != nullptr);
}

void teardown_validator_test() {
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

TEST_CASE("AST Validator Creation", "[ast_validator][creation]") {
    setup_validator_test();
    
    SECTION("Create validator") {
        REQUIRE(validator != nullptr);
        REQUIRE(validator->pool != nullptr);
        REQUIRE(validator->transpiler != nullptr);
        REQUIRE(validator->type_definitions != nullptr);
    }
    
    SECTION("Create validator with null pool") {
        AstValidator* null_validator = ast_validator_create(nullptr);
        REQUIRE(null_validator == nullptr);
    }
    
    teardown_validator_test();
}

// ==================== Phase 1 Tests: Primitive Type Validation ====================

TEST_CASE("Primitive Type Validation", "[ast_validator][primitive]") {
    setup_validator_test();
    
    SECTION("Validate string success") {
        TypedItem string_item = create_test_string("hello world");
        Type* string_type = create_test_type(LMD_TYPE_STRING);
        
        AstValidationResult* result = ast_validator_validate_type(validator, string_item, string_type);
        
        REQUIRE(result != nullptr);
        REQUIRE(result->valid);
        REQUIRE(result->error_count == 0);
        REQUIRE(result->errors == nullptr);
    }
    
    SECTION("Validate string type mismatch") {
        TypedItem int_item = create_test_int(42);
        Type* string_type = create_test_type(LMD_TYPE_STRING);
        
        AstValidationResult* result = ast_validator_validate_type(validator, int_item, string_type);
        
        REQUIRE(result != nullptr);
        REQUIRE_FALSE(result->valid);
        REQUIRE(result->error_count == 1);
        REQUIRE(result->errors != nullptr);
        REQUIRE(result->errors->code == VALID_ERROR_TYPE_MISMATCH);
    }
    
    SECTION("Validate int success") {
        TypedItem int_item = create_test_int(42);
        Type* int_type = create_test_type(LMD_TYPE_INT);
        
        AstValidationResult* result = ast_validator_validate_type(validator, int_item, int_type);
        
        REQUIRE(result != nullptr);
        REQUIRE(result->valid);
        REQUIRE(result->error_count == 0);
    }
    
    SECTION("Validate float success") {
        TypedItem float_item = create_test_float(3.14);
        Type* float_type = create_test_type(LMD_TYPE_FLOAT);
        
        AstValidationResult* result = ast_validator_validate_type(validator, float_item, float_type);
        
        REQUIRE(result != nullptr);
        REQUIRE(result->valid);
        REQUIRE(result->error_count == 0);
    }
    
    SECTION("Validate bool success") {
        TypedItem bool_item = create_test_bool(true);
        Type* bool_type = create_test_type(LMD_TYPE_BOOL);
        
        AstValidationResult* result = ast_validator_validate_type(validator, bool_item, bool_type);
        
        REQUIRE(result != nullptr);
        REQUIRE(result->valid);
        REQUIRE(result->error_count == 0);
    }
    
    SECTION("Validate null success") {
        TypedItem null_item = create_test_null();
        Type* null_type = create_test_type(LMD_TYPE_NULL);
        
        AstValidationResult* result = ast_validator_validate_type(validator, null_item, null_type);
        
        REQUIRE(result != nullptr);
        REQUIRE(result->valid);
        REQUIRE(result->error_count == 0);
    }
    
    teardown_validator_test();
}

// ==================== Error Handling Tests ====================

TEST_CASE("Error Handling", "[ast_validator][error_handling]") {
    setup_validator_test();
    
    SECTION("Validate with null validator") {
        TypedItem string_item = create_test_string("test");
        Type* string_type = create_test_type(LMD_TYPE_STRING);
        
        AstValidationResult* result = ast_validator_validate_type(nullptr, string_item, string_type);
        
        REQUIRE(result != nullptr);
        REQUIRE_FALSE(result->valid);
        REQUIRE(result->error_count == 1);
        REQUIRE(result->errors->code == VALID_ERROR_PARSE_ERROR);
    }
    
    SECTION("Validate with null type") {
        TypedItem string_item = create_test_string("test");
        
        AstValidationResult* result = ast_validator_validate_type(validator, string_item, nullptr);
        
        REQUIRE(result != nullptr);
        REQUIRE_FALSE(result->valid);
        REQUIRE(result->error_count == 1);
        REQUIRE(result->errors->code == VALID_ERROR_PARSE_ERROR);
    }
    
    SECTION("Create validation error") {
        ValidationError* error = create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Test error message", nullptr, test_pool);
        
        REQUIRE(error != nullptr);
        REQUIRE(error->code == VALID_ERROR_TYPE_MISMATCH);
        REQUIRE(strcmp(error->message->chars, "Test error message") == 0);
        REQUIRE(error->next == nullptr);
    }
    
    teardown_validator_test();
}

// ==================== Utility Function Tests ====================

TEST_CASE("Utility Functions", "[ast_validator][utility]") {
    setup_validator_test();
    
    SECTION("Item compatible with type success") {
        TypedItem string_item = create_test_string("test");
        Type* string_type = create_test_type(LMD_TYPE_STRING);
        
        bool compatible = is_item_compatible_with_type(string_item, string_type);
        REQUIRE(compatible);
    }
    
    SECTION("Item compatible with type failure") {
        TypedItem int_item = create_test_int(42);
        Type* string_type = create_test_type(LMD_TYPE_STRING);
        
        bool compatible = is_item_compatible_with_type(int_item, string_type);
        REQUIRE_FALSE(compatible);
    }
    
    SECTION("Type to string") {
        Type* string_type = create_test_type(LMD_TYPE_STRING);
        Type* int_type = create_test_type(LMD_TYPE_INT);
        Type* float_type = create_test_type(LMD_TYPE_FLOAT);
        Type* bool_type = create_test_type(LMD_TYPE_BOOL);
        Type* null_type = create_test_type(LMD_TYPE_NULL);
        
        REQUIRE(strcmp(type_to_string(string_type), "string") == 0);
        REQUIRE(strcmp(type_to_string(int_type), "int") == 0);
        REQUIRE(strcmp(type_to_string(float_type), "float") == 0);
        REQUIRE(strcmp(type_to_string(bool_type), "bool") == 0);
        REQUIRE(strcmp(type_to_string(null_type), "null") == 0);
        REQUIRE(strcmp(type_to_string(nullptr), "unknown") == 0);
    }
    
    teardown_validator_test();
}

// ==================== Integration Tests ====================

TEST_CASE("Integration Tests", "[ast_validator][integration]") {
    setup_validator_test();
    
    SECTION("Multiple validations") {
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
        
        REQUIRE(result1->valid);
        REQUIRE(result2->valid);
        REQUIRE(result3->valid);
    }
    
    SECTION("Validation depth check") {
        // Test that validation depth is properly managed
        TypedItem string_item = create_test_string("test");
        Type* string_type = create_test_type(LMD_TYPE_STRING);
        
        // Modify validator options to have very low max depth
        validator->default_options.max_depth = 0;
        
        AstValidationResult* result = ast_validator_validate_type(validator, string_item, string_type);
        
        REQUIRE(result != nullptr);
        REQUIRE_FALSE(result->valid);
        REQUIRE(result->errors->code == VALID_ERROR_CONSTRAINT_VIOLATION);
    }
    
    teardown_validator_test();
}
