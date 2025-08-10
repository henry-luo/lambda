/**
 * @file test_validator_basic.c
 * @brief Basic Schema Validator Test Suite
 * @author Henry Luo
 * @license MIT
 * 
 * Comprehensive tests for the Lambda schema validator starting with simple schemas
 * and progressively testing more complex validation scenarios.
 */

#include "../validator.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

// Test results tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test utility macros
#define TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf("✓ PASS: %s\n", message); \
        } else { \
            tests_failed++; \
            printf("✗ FAIL: %s\n", message); \
        } \
    } while(0)

#define TEST_SECTION(section_name) \
    printf("\n=== %s ===\n", section_name)

// Test fixture
typedef struct TestValidator {
    SchemaValidator* validator;
    VariableMemPool* pool;
} TestValidator;

TestValidator* setup_test_validator() {
    VariableMemPool* pool = NULL;
    if (pool_variable_init(&pool, 8192, 50) != MEM_POOL_ERR_OK) {
        return NULL;
    }
    
    TestValidator* test_validator = (TestValidator*)pool_calloc(pool, sizeof(TestValidator));
    test_validator->pool = pool;
    test_validator->validator = schema_validator_create(pool);
    
    return test_validator;
}

void teardown_test_validator(TestValidator* test_validator) {
    if (test_validator && test_validator->validator) {
        schema_validator_destroy(test_validator->validator);
    }
    if (test_validator && test_validator->pool) {
        pool_variable_destroy(&test_validator->pool);
    }
}

// Create test items for validation
Item create_string_item(const char* str, VariableMemPool* pool) {
    String* string_obj = string_from_strview(strview_from_cstr(str), pool);
    Item item = {.item = LMD_TYPE_STRING, .pointer = string_obj};
    return item;
}

Item create_int_item(long value, VariableMemPool* pool) {
    Integer* int_obj = (Integer*)pool_calloc(pool, sizeof(Integer));
    int_obj->value = value;
    Item item = {.item = LMD_TYPE_INT, .pointer = int_obj};
    return item;
}

Item create_bool_item(bool value, VariableMemPool* pool) {
    Bool* bool_obj = (Bool*)pool_calloc(pool, sizeof(Bool));
    bool_obj->value = value;
    Item item = {.item = LMD_TYPE_BOOL, .pointer = bool_obj};
    return item;
}

Item create_null_item() {
    Item item = {.item = ITEM_NULL, .pointer = NULL};
    return item;
}

// Test primitive type validation
void test_primitive_validation() {
    TEST_SECTION("Primitive Type Validation");
    
    TestValidator* test = setup_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Load simple schemas
    const char* simple_schema = "type StringType = string\ntype IntType = int\ntype BoolType = bool";
    int result = schema_validator_load_schema(test->validator, simple_schema, "simple_test");
    TEST_ASSERT(result == 0, "Load simple primitive schemas");
    
    // Test string validation
    TypeSchema* string_schema = create_primitive_schema(LMD_TYPE_STRING, test->pool);
    Item string_item = create_string_item("test string", test->pool);
    ValidationResult* string_result = validate_item(test->validator, string_item, string_schema, test->validator->context);
    TEST_ASSERT(string_result != NULL && string_result->valid, "String validates against string schema");
    
    // Test type mismatch
    Item int_item = create_int_item(42, test->pool);
    ValidationResult* mismatch_result = validate_item(test->validator, int_item, string_schema, test->validator->context);
    TEST_ASSERT(mismatch_result != NULL && !mismatch_result->valid, "Int fails against string schema");
    TEST_ASSERT(mismatch_result->error_count > 0, "Type mismatch generates error");
    
    teardown_test_validator(test);
}

// Test optional type validation
void test_optional_validation() {
    TEST_SECTION("Optional Type Validation");
    
    TestValidator* test = setup_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Create optional string schema
    TypeSchema* base_string_schema = create_primitive_schema(LMD_TYPE_STRING, test->pool);
    TypeSchema* optional_schema = create_occurrence_schema(base_string_schema, 0, 1, test->pool);
    
    // Test valid string
    Item string_item = create_string_item("valid", test->pool);
    ValidationResult* valid_result = validate_item(test->validator, string_item, optional_schema, test->validator->context);
    TEST_ASSERT(valid_result != NULL && valid_result->valid, "String validates against optional string");
    
    // Test null (should be valid for optional)
    Item null_item = create_null_item();
    ValidationResult* null_result = validate_item(test->validator, null_item, optional_schema, test->validator->context);
    TEST_ASSERT(null_result != NULL && null_result->valid, "Null validates against optional string");
    
    teardown_test_validator(test);
}

// Test array validation
void test_array_validation() {
    TEST_SECTION("Array Validation");
    
    TestValidator* test = setup_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Create array schema [string*]
    TypeSchema* string_schema = create_primitive_schema(LMD_TYPE_STRING, test->pool);
    TypeSchema* array_schema = create_array_schema(string_schema, 0, -1, test->pool);
    
    // Create test array
    List* test_list = list_new(test->pool);
    list_add(test_list, create_string_item("item1", test->pool));
    list_add(test_list, create_string_item("item2", test->pool));
    
    Item array_item = {.item = LMD_TYPE_LIST, .pointer = test_list};
    ValidationResult* array_result = validate_item(test->validator, array_item, array_schema, test->validator->context);
    TEST_ASSERT(array_result != NULL && array_result->valid, "String array validates correctly");
    
    // Test mixed type array (should fail)
    List* mixed_list = list_new(test->pool);
    list_add(mixed_list, create_string_item("string", test->pool));
    list_add(mixed_list, create_int_item(42, test->pool));
    
    Item mixed_array_item = {.item = LMD_TYPE_LIST, .pointer = mixed_list};
    ValidationResult* mixed_result = validate_item(test->validator, mixed_array_item, array_schema, test->validator->context);
    TEST_ASSERT(mixed_result != NULL && !mixed_result->valid, "Mixed type array fails validation");
    
    teardown_test_validator(test);
}

// Test simple map validation
void test_map_validation() {
    TEST_SECTION("Map Validation");
    
    TestValidator* test = setup_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Create simple map schema {name: string, age: int}
    TypeSchema* map_schema = create_map_schema(NULL, NULL, test->pool);
    
    // Create test map
    Map* test_map = map_new(test->pool);
    String* name_key = string_from_strview(strview_from_cstr("name"), test->pool);
    String* age_key = string_from_strview(strview_from_cstr("age"), test->pool);
    
    map_set(test_map, (Item){.item = LMD_TYPE_STRING, .pointer = name_key}, 
            create_string_item("John Doe", test->pool));
    map_set(test_map, (Item){.item = LMD_TYPE_STRING, .pointer = age_key},
            create_int_item(30, test->pool));
    
    Item map_item = {.item = LMD_TYPE_MAP, .pointer = test_map};
    ValidationResult* map_result = validate_item(test->validator, map_item, map_schema, test->validator->context);
    TEST_ASSERT(map_result != NULL, "Map validation produces result");
    
    teardown_test_validator(test);
}

// Test union type validation
void test_union_validation() {
    TEST_SECTION("Union Type Validation");
    
    TestValidator* test = setup_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Create union schema string | int
    List* union_types = list_new(test->pool);
    list_add(union_types, (Item){.item = LMD_SCHEMA_PRIMITIVE, .pointer = create_primitive_schema(LMD_TYPE_STRING, test->pool)});
    list_add(union_types, (Item){.item = LMD_SCHEMA_PRIMITIVE, .pointer = create_primitive_schema(LMD_TYPE_INT, test->pool)});
    
    TypeSchema* union_schema = create_union_schema(union_types, test->pool);
    
    // Test string matches union
    Item string_item = create_string_item("test", test->pool);
    ValidationResult* string_result = validate_item(test->validator, string_item, union_schema, test->validator->context);
    TEST_ASSERT(string_result != NULL && string_result->valid, "String validates against string|int union");
    
    // Test int matches union  
    Item int_item = create_int_item(42, test->pool);
    ValidationResult* int_result = validate_item(test->validator, int_item, union_schema, test->validator->context);
    TEST_ASSERT(int_result != NULL && int_result->valid, "Int validates against string|int union");
    
    // Test bool fails union
    Item bool_item = create_bool_item(true, test->pool);
    ValidationResult* bool_result = validate_item(test->validator, bool_item, union_schema, test->validator->context);
    TEST_ASSERT(bool_result != NULL && !bool_result->valid, "Bool fails against string|int union");
    
    teardown_test_validator(test);
}

// Test error path tracking
void test_error_path_tracking() {
    TEST_SECTION("Error Path Tracking");
    
    TestValidator* test = setup_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Create nested structure for path testing
    ValidationContext* ctx = test->validator->context;
    
    // Push field path
    PathSegment* field_path = create_field_path("person", test->pool);
    PathSegment* old_path = push_path_segment(ctx, field_path);
    
    // Push index path
    PathSegment* index_path = create_index_path(2, test->pool);
    push_path_segment(ctx, index_path);
    
    // Create error with path
    ValidationError* error = create_validation_error(
        VALID_ERROR_TYPE_MISMATCH, 
        "Type mismatch in nested structure", 
        ctx->path, 
        test->pool
    );
    
    TEST_ASSERT(error != NULL, "Error with path created");
    TEST_ASSERT(error->path != NULL, "Error has path information");
    
    // Format path and check
    String* path_str = format_validation_path(error->path, test->pool);
    TEST_ASSERT(path_str != NULL, "Path formatting succeeds");
    TEST_ASSERT(strstr(path_str->chars, "[2]") != NULL, "Path contains array index");
    TEST_ASSERT(strstr(path_str->chars, "person") != NULL, "Path contains field name");
    
    teardown_test_validator(test);
}

// Test error recovery and continuation
void test_error_recovery() {
    TEST_SECTION("Error Recovery and Continuation");
    
    TestValidator* test = setup_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Create validation result for multiple errors
    ValidationResult* result = create_validation_result(test->pool);
    
    // Add multiple errors to test accumulation
    ValidationError* error1 = create_validation_error(
        VALID_ERROR_TYPE_MISMATCH, "First error", NULL, test->pool);
    ValidationError* error2 = create_validation_error(
        VALID_ERROR_MISSING_FIELD, "Second error", NULL, test->pool);
    ValidationError* error3 = create_validation_error(
        VALID_ERROR_UNEXPECTED_FIELD, "Third error", NULL, test->pool);
    
    add_validation_error(result, error1);
    add_validation_error(result, error2);
    add_validation_error(result, error3);
    
    TEST_ASSERT(result->error_count == 3, "Multiple errors accumulated");
    TEST_ASSERT(!result->valid, "Result marked as invalid");
    
    // Test error report generation
    String* report = generate_validation_report(result, test->pool);
    TEST_ASSERT(report != NULL, "Validation report generated");
    TEST_ASSERT(report->len > 0, "Report contains content");
    
    printf("Generated report:\n%s\n", report->chars);
    
    teardown_test_validator(test);
}

// Main test runner
int main() {
    printf("=== Lambda Schema Validator Test Suite ===\n");
    
    test_primitive_validation();
    test_optional_validation();
    test_array_validation();
    test_map_validation();
    test_union_validation();
    test_error_path_tracking();
    test_error_recovery();
    
    printf("\n=== Test Summary ===\n");
    printf("Total tests: %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n", tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    
    return tests_failed == 0 ? 0 : 1;
}
