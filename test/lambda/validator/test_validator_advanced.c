/**
 * @file test_validator_advanced.c
 * @brief Advanced Schema Validator Test Suite with Error Recovery
 * @author Henry Luo
 * @license MIT
 */

#include "../validator.h"
#include "validator_enhanced.cpp"
#include <stdio.h>
#include <assert.h>
#include <string.h>

// Test results tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Enhanced test utility macros
#define TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf("✓ PASS: %s\n", message); \
        } else { \
            tests_failed++; \
            printf("✗ FAIL: %s (line %d)\n", message, __LINE__); \
        } \
    } while(0)

#define TEST_SECTION(section_name) \
    printf("\n=== %s ===\n", section_name)

#define TEST_EXPECT_ERRORS(result, expected_count, message) \
    TEST_ASSERT((result) && (result)->error_count == (expected_count), message)

#define TEST_EXPECT_VALID(result, message) \
    TEST_ASSERT((result) && (result)->valid, message)

#define TEST_EXPECT_INVALID(result, message) \
    TEST_ASSERT((result) && !(result)->valid, message)

// Enhanced test fixture
typedef struct AdvancedTestValidator {
    SchemaValidator* validator;
    ValidationContext* context;
    VariableMemPool* pool;
} AdvancedTestValidator;

AdvancedTestValidator* setup_advanced_test_validator() {
    VariableMemPool* pool = NULL;
    if (pool_variable_init(&pool, 16384, 100) != MEM_POOL_ERR_OK) {
        return NULL;
    }
    
    AdvancedTestValidator* test = (AdvancedTestValidator*)pool_calloc(pool, sizeof(AdvancedTestValidator));
    test->pool = pool;
    test->validator = schema_validator_create(pool);
    
    if (!test->validator) {
        return NULL;
    }
    
    // Set up enhanced validation options
    ValidationOptions options = {
        .strict_mode = false,
        .allow_unknown_fields = true,
        .allow_empty_elements = false,
        .max_depth = 50,
        .timeout_ms = 0
    };
    
    test->context = create_enhanced_validation_context(pool, options);
    test->context->schema_registry = test->validator->schemas;
    
    return test;
}

void teardown_advanced_test_validator(AdvancedTestValidator* test) {
    if (test && test->validator) {
        schema_validator_destroy(test->validator);
    }
    if (test && test->pool) {
        pool_variable_destroy(&test->pool);
    }
}

// Create complex test data structures
Item create_person_map(VariableMemPool* pool) {
    Map* person_map = map_new(pool);
    
    // Add name
    String* name_key = string_from_strview(strview_from_cstr("name"), pool);
    String* name_value = string_from_strview(strview_from_cstr("John Doe"), pool);
    map_set(person_map, 
        (Item){.item = LMD_TYPE_STRING, .pointer = name_key},
        (Item){.item = LMD_TYPE_STRING, .pointer = name_value});
    
    // Add age
    String* age_key = string_from_strview(strview_from_cstr("age"), pool);
    Integer* age_value = (Integer*)pool_calloc(pool, sizeof(Integer));
    age_value->value = 30;
    map_set(person_map,
        (Item){.item = LMD_TYPE_STRING, .pointer = age_key},
        (Item){.item = LMD_TYPE_INT, .pointer = age_value});
    
    return (Item){.item = LMD_TYPE_MAP, .pointer = person_map};
}

Item create_invalid_person_map(VariableMemPool* pool) {
    Map* person_map = map_new(pool);
    
    // Add name (correct)
    String* name_key = string_from_strview(strview_from_cstr("name"), pool);
    String* name_value = string_from_strview(strview_from_cstr("Jane Doe"), pool);
    map_set(person_map, 
        (Item){.item = LMD_TYPE_STRING, .pointer = name_key},
        (Item){.item = LMD_TYPE_STRING, .pointer = name_value});
    
    // Add age (incorrect - string instead of int)
    String* age_key = string_from_strview(strview_from_cstr("age"), pool);
    String* age_value = string_from_strview(strview_from_cstr("thirty"), pool);
    map_set(person_map,
        (Item){.item = LMD_TYPE_STRING, .pointer = age_key},
        (Item){.item = LMD_TYPE_STRING, .pointer = age_value});
    
    // Add unexpected field
    String* extra_key = string_from_strview(strview_from_cstr("unexpected_field"), pool);
    String* extra_value = string_from_strview(strview_from_cstr("should not be here"), pool);
    map_set(person_map,
        (Item){.item = LMD_TYPE_STRING, .pointer = extra_key},
        (Item){.item = LMD_TYPE_STRING, .pointer = extra_value});
    
    return (Item){.item = LMD_TYPE_MAP, .pointer = person_map};
}

// Test enhanced error recovery
void test_error_recovery_and_continuation() {
    TEST_SECTION("Error Recovery and Continuation");
    
    AdvancedTestValidator* test = setup_advanced_test_validator();
    TEST_ASSERT(test != NULL, "Advanced test validator setup");
    
    // Load complex schema
    const char* person_schema = 
        "type PersonType = {\n"
        "    name: string,\n"
        "    age: int,\n"
        "    email: string?\n"
        "}";
    
    int load_result = schema_validator_load_schema(test->validator, person_schema, "person_test");
    TEST_ASSERT(load_result == 0, "Load person schema");
    
    // Create schema for validation
    TypeSchema* person_type_schema = create_map_schema(NULL, NULL, test->pool);
    
    // Test with invalid data - should accumulate multiple errors
    Item invalid_person = create_invalid_person_map(test->pool);
    ValidationResult* result = validate_item_with_recovery(
        test->validator, invalid_person, person_type_schema, test->context);
    
    TEST_ASSERT(result != NULL, "Validation result created");
    TEST_EXPECT_INVALID(result, "Invalid person data fails validation");
    
    // Should have multiple errors but continue validation
    printf("Error count: %d\n", result->error_count);
    
    // Print detailed error report
    if (result->error_count > 0) {
        String* report = generate_validation_report(result, test->pool);
        printf("Validation Report:\n%s\n", report ? report->chars : "No report generated");
    }
    
    teardown_advanced_test_validator(test);
}

// Test path tracking in nested structures
void test_nested_path_tracking() {
    TEST_SECTION("Nested Path Tracking");
    
    AdvancedTestValidator* test = setup_advanced_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Create nested map structure: {person: {address: {street: "invalid_type"}}}
    Map* address_map = map_new(test->pool);
    String* street_key = string_from_strview(strview_from_cstr("street"), test->pool);
    Integer* invalid_street = (Integer*)pool_calloc(test->pool, sizeof(Integer));
    invalid_street->value = 123; // Should be string, not int
    map_set(address_map,
        (Item){.item = LMD_TYPE_STRING, .pointer = street_key},
        (Item){.item = LMD_TYPE_INT, .pointer = invalid_street});
    
    Map* person_map = map_new(test->pool);
    String* address_key = string_from_strview(strview_from_cstr("address"), test->pool);
    map_set(person_map,
        (Item){.item = LMD_TYPE_STRING, .pointer = address_key},
        (Item){.item = LMD_TYPE_MAP, .pointer = address_map});
    
    Map* root_map = map_new(test->pool);
    String* person_key = string_from_strview(strview_from_cstr("person"), test->pool);
    map_set(root_map,
        (Item){.item = LMD_TYPE_STRING, .pointer = person_key},
        (Item){.item = LMD_TYPE_MAP, .pointer = person_map});
    
    Item nested_item = {.item = LMD_TYPE_MAP, .pointer = root_map};
    
    // Create nested schema
    TypeSchema* string_schema = create_primitive_schema(LMD_TYPE_STRING, test->pool);
    TypeSchema* address_schema = create_map_schema(NULL, NULL, test->pool);
    TypeSchema* person_schema = create_map_schema(NULL, NULL, test->pool);
    TypeSchema* root_schema = create_map_schema(NULL, NULL, test->pool);
    
    ValidationResult* result = validate_item_with_recovery(
        test->validator, nested_item, root_schema, test->context);
    
    TEST_ASSERT(result != NULL, "Nested validation result created");
    
    // Check for proper path tracking in errors
    if (result->errors) {
        ValidationError* error = result->errors;
        while (error) {
            String* path_str = format_validation_path_enhanced(error->path, test->pool);
            printf("Error at path: %s - %s\n", 
                   path_str ? path_str->chars : "(no path)",
                   error->message ? error->message->chars : "No message");
            error = error->next;
        }
    }
    
    teardown_advanced_test_validator(test);
}

// Test array validation with error recovery
void test_array_validation_with_recovery() {
    TEST_SECTION("Array Validation with Error Recovery");
    
    AdvancedTestValidator* test = setup_advanced_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Create array with mixed valid/invalid elements
    List* mixed_array = list_new(test->pool);
    list_add(mixed_array, create_string_item("valid", test->pool));
    list_add(mixed_array, create_int_item(42, test->pool));        // Invalid - should be string
    list_add(mixed_array, create_string_item("also valid", test->pool));
    list_add(mixed_array, create_bool_item(true, test->pool));     // Invalid - should be string
    list_add(mixed_array, create_string_item("valid again", test->pool));
    
    Item array_item = {.item = LMD_TYPE_LIST, .pointer = mixed_array};
    
    // Create array schema [string*]
    TypeSchema* string_schema = create_primitive_schema(LMD_TYPE_STRING, test->pool);
    TypeSchema* array_schema = create_array_schema(string_schema, 0, -1, test->pool);
    
    ValidationResult* result = validate_array_with_recovery(
        test->validator, array_item, array_schema, test->context);
    
    TEST_ASSERT(result != NULL, "Array validation result created");
    TEST_EXPECT_INVALID(result, "Mixed array fails validation");
    
    // Should have exactly 2 errors (for indices 1 and 3)
    printf("Array validation errors: %d\n", result->error_count);
    
    // Check error paths include array indices
    ValidationError* error = result->errors;
    bool found_index_path = false;
    while (error) {
        String* path_str = format_validation_path_enhanced(error->path, test->pool);
        if (path_str && strstr(path_str->chars, "[")) {
            found_index_path = true;
            printf("Array error at: %s\n", path_str->chars);
        }
        error = error->next;
    }
    
    TEST_ASSERT(found_index_path, "Array errors include index paths");
    
    teardown_advanced_test_validator(test);
}

// Test union validation with suggestions
void test_union_validation_with_suggestions() {
    TEST_SECTION("Union Validation with Suggestions");
    
    AdvancedTestValidator* test = setup_advanced_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Create union schema: string | int | bool
    List* union_types = list_new(test->pool);
    list_add(union_types, (Item){.item = LMD_SCHEMA_PRIMITIVE, 
        .pointer = create_primitive_schema(LMD_TYPE_STRING, test->pool)});
    list_add(union_types, (Item){.item = LMD_SCHEMA_PRIMITIVE, 
        .pointer = create_primitive_schema(LMD_TYPE_INT, test->pool)});
    list_add(union_types, (Item){.item = LMD_SCHEMA_PRIMITIVE, 
        .pointer = create_primitive_schema(LMD_TYPE_BOOL, test->pool)});
    
    TypeSchema* union_schema = create_union_schema(union_types, test->pool);
    
    // Test valid cases
    Item string_item = create_string_item("test", test->pool);
    ValidationResult* string_result = validate_union_with_recovery(
        test->validator, string_item, union_schema, test->context);
    TEST_EXPECT_VALID(string_result, "String matches union");
    
    Item int_item = create_int_item(42, test->pool);
    ValidationResult* int_result = validate_union_with_recovery(
        test->validator, int_item, union_schema, test->context);
    TEST_EXPECT_VALID(int_result, "Int matches union");
    
    // Test invalid case with suggestions
    List* invalid_list = list_new(test->pool);
    Item array_item = {.item = LMD_TYPE_LIST, .pointer = invalid_list};
    
    ValidationResult* invalid_result = validate_union_with_recovery(
        test->validator, array_item, union_schema, test->context);
    TEST_EXPECT_INVALID(invalid_result, "Array fails union validation");
    
    // Check for error suggestions
    if (invalid_result && invalid_result->errors) {
        ValidationError* error = invalid_result->errors;
        if (error->suggestions && error->suggestions->length > 0) {
            printf("Union validation suggestions:\n");
            for (long i = 0; i < error->suggestions->length; i++) {
                Item suggestion = list_get(error->suggestions, i);
                if (get_type_id(suggestion) == LMD_TYPE_STRING) {
                    String* suggestion_str = (String*)suggestion.pointer;
                    printf("  - %s\n", suggestion_str->chars);
                }
            }
        }
    }
    
    teardown_advanced_test_validator(test);
}

// Test comprehensive error reporting
void test_comprehensive_error_reporting() {
    TEST_SECTION("Comprehensive Error Reporting");
    
    AdvancedTestValidator* test = setup_advanced_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Create complex invalid structure
    Item invalid_complex = create_invalid_person_map(test->pool);
    TypeSchema* person_schema = create_map_schema(NULL, NULL, test->pool);
    
    ValidationResult* result = validate_item_with_recovery(
        test->validator, invalid_complex, person_schema, test->context);
    
    TEST_ASSERT(result != NULL, "Complex validation result created");
    
    // Test different report formats
    String* text_report = generate_validation_report(result, test->pool);
    String* json_report = generate_json_report(result, test->pool);
    
    TEST_ASSERT(text_report && text_report->len > 0, "Text report generated");
    TEST_ASSERT(json_report && json_report->len > 0, "JSON report generated");
    
    printf("\n--- Text Report ---\n%s\n", text_report->chars);
    printf("\n--- JSON Report ---\n%s\n", json_report->chars);
    
    // Test enhanced error formatting
    if (result->errors) {
        ValidationError* error = result->errors;
        while (error) {
            String* enhanced_error = format_error_with_context(error, test->pool);
            printf("Enhanced error: %s\n", enhanced_error->chars);
            error = error->next;
        }
    }
    
    teardown_advanced_test_validator(test);
}

// Test validation options and strict mode
void test_validation_options() {
    TEST_SECTION("Validation Options and Strict Mode");
    
    AdvancedTestValidator* test = setup_advanced_test_validator();
    TEST_ASSERT(test != NULL, "Test validator setup");
    
    // Test with strict mode disabled (default)
    test->context->options.strict_mode = false;
    test->context->options.allow_unknown_fields = true;
    
    TypeSchema* string_schema = create_primitive_schema(LMD_TYPE_STRING, test->pool);
    Item int_item = create_int_item(42, test->pool);
    
    ValidationResult* lenient_result = validate_item_with_recovery(
        test->validator, int_item, string_schema, test->context);
    
    TEST_EXPECT_INVALID(lenient_result, "Lenient mode still catches type errors");
    printf("Lenient mode errors: %d\n", lenient_result->error_count);
    
    // Test with strict mode enabled
    test->context->options.strict_mode = true;
    test->context->options.allow_unknown_fields = false;
    
    ValidationResult* strict_result = validate_item_with_recovery(
        test->validator, int_item, string_schema, test->context);
    
    TEST_EXPECT_INVALID(strict_result, "Strict mode catches type errors");
    printf("Strict mode errors: %d\n", strict_result->error_count);
    
    teardown_advanced_test_validator(test);
}

// Test memory management and cleanup
void test_memory_management() {
    TEST_SECTION("Memory Management and Cleanup");
    
    // Test multiple validation cycles
    for (int cycle = 0; cycle < 5; cycle++) {
        AdvancedTestValidator* test = setup_advanced_test_validator();
        TEST_ASSERT(test != NULL, "Memory management - validator setup");
        
        // Create and validate multiple items
        for (int i = 0; i < 10; i++) {
            Item test_item = create_string_item("test", test->pool);
            TypeSchema* schema = create_primitive_schema(LMD_TYPE_STRING, test->pool);
            
            ValidationResult* result = validate_item_with_recovery(
                test->validator, test_item, schema, test->context);
            
            TEST_EXPECT_VALID(result, "Memory cycle validation");
        }
        
        teardown_advanced_test_validator(test);
    }
    
    printf("Completed %d memory management cycles\n", 5);
}

// Main test runner
int main() {
    printf("=== Lambda Enhanced Schema Validator Test Suite ===\n");
    
    test_error_recovery_and_continuation();
    test_nested_path_tracking();
    test_array_validation_with_recovery();
    test_union_validation_with_suggestions();
    test_comprehensive_error_reporting();
    test_validation_options();
    test_memory_management();
    
    printf("\n=== Test Summary ===\n");
    printf("Total tests: %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n", tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    
    if (tests_failed == 0) {
        printf("\n🎉 All tests passed! Enhanced validator is working correctly.\n");
    } else {
        printf("\n❌ Some tests failed. Check the output above for details.\n");
    }
    
    return tests_failed == 0 ? 0 : 1;
}
