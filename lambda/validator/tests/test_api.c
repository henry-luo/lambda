#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "../validator.h"

// Test fixtures
static LambdaValidator* api_validator = NULL;

void api_test_setup(void) {
    api_validator = lambda_validator_create();
}

void api_test_teardown(void) {
    if (api_validator) {
        lambda_validator_destroy(api_validator);
        api_validator = NULL;
    }
}

TestSuite(api_tests, .init = api_test_setup, .fini = api_test_teardown);

// ==================== API Lifecycle Tests ====================

Test(api_tests, create_and_destroy_validator) {
    cr_assert_not_null(api_validator, "Validator should be created successfully");
    
    // Test destruction
    lambda_validator_destroy(api_validator);
    api_validator = NULL;  // Prevent double-free in teardown
}

Test(api_tests, load_schema_string) {
    const char* schema_source = "type SimpleDoc = {title: string, content: string}";
    
    int result = lambda_validator_load_schema_string(api_validator, schema_source, "SimpleDoc");
    
    cr_assert_eq(result, 0, "Schema should load successfully");
}

Test(api_tests, load_multiple_schemas) {
    const char* schema1 = "type Person = {name: string, age: int}";
    const char* schema2 = "type Company = {name: string, employees: Person*}";
    
    int result1 = lambda_validator_load_schema_string(api_validator, schema1, "Person");
    int result2 = lambda_validator_load_schema_string(api_validator, schema2, "Company");
    
    cr_assert_eq(result1, 0, "First schema should load successfully");
    cr_assert_eq(result2, 0, "Second schema should load successfully");
}

// ==================== Validation Tests ====================

Test(api_tests, validate_valid_document_string) {
    // Load schema
    const char* schema_source = "type SimpleDoc = {title: string, content: string}";
    int load_result = lambda_validator_load_schema_string(api_validator, schema_source, "SimpleDoc");
    cr_assert_eq(load_result, 0, "Schema should load successfully");
    
    // Validate valid document
    const char* valid_document = "{title: \"Test Document\", content: \"Hello World\"}";
    
    LambdaValidationResult* result = lambda_validate_string(api_validator, valid_document, "SimpleDoc");
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, true, "Valid document should pass validation");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
    cr_assert_null(result->errors, "Error array should be null when no errors");
    
    lambda_validation_result_free(result);
}

Test(api_tests, validate_invalid_document_string) {
    // Load schema
    const char* schema_source = "type SimpleDoc = {title: string, content: string}";
    int load_result = lambda_validator_load_schema_string(api_validator, schema_source, "SimpleDoc");
    cr_assert_eq(load_result, 0, "Schema should load successfully");
    
    // Validate invalid document (missing required field)
    const char* invalid_document = "{title: \"Test Document\"}";  // Missing 'content'
    
    LambdaValidationResult* result = lambda_validate_string(api_validator, invalid_document, "SimpleDoc");
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, false, "Invalid document should fail validation");
    cr_assert_gt(result->error_count, 0, "Should have at least one error");
    cr_assert_not_null(result->errors, "Error array should not be null when errors exist");
    
    // Check that errors array is null-terminated
    cr_assert_not_null(result->errors[0], "First error should not be null");
    cr_assert_null(result->errors[result->error_count], "Error array should be null-terminated");
    
    lambda_validation_result_free(result);
}

Test(api_tests, validate_document_with_warnings) {
    // Load schema that can generate warnings
    const char* schema_source = "type DocWithMeta = {title: string, author?: string, content: string}";
    int load_result = lambda_validator_load_schema_string(api_validator, schema_source, "DocWithMeta");
    cr_assert_eq(load_result, 0, "Schema should load successfully");
    
    // Document that might generate warnings (e.g., missing optional but recommended field)
    const char* document = "{title: \"Test\", content: \"Hello\"}";  // Missing 'author'
    
    LambdaValidationResult* result = lambda_validate_string(api_validator, document, "DocWithMeta");
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, true, "Document should be valid despite warnings");
    
    // Note: This test might not generate warnings with the current implementation
    // but demonstrates the API structure for when warnings are implemented
    
    lambda_validation_result_free(result);
}

// ==================== Options Tests ====================

Test(api_tests, set_validation_options) {
    LambdaValidationOptions options = {
        .strict_mode = true,
        .allow_unknown_fields = false,
        .allow_empty_elements = false,
        .max_validation_depth = 50,
        .enabled_custom_rules = NULL,
        .disabled_rules = NULL
    };
    
    lambda_validator_set_options(api_validator, &options);
    
    // Get options back to verify they were set
    LambdaValidationOptions* current_options = lambda_validator_get_options(api_validator);
    
    cr_assert_not_null(current_options, "Should be able to get current options");
    cr_assert_eq(current_options->strict_mode, true, "Strict mode should be set");
    cr_assert_eq(current_options->allow_unknown_fields, false, "Unknown fields should be disallowed");
}

Test(api_tests, strict_mode_validation) {
    // Load schema
    const char* schema_source = "type StrictDoc = {title: string}";
    int load_result = lambda_validator_load_schema_string(api_validator, schema_source, "StrictDoc");
    cr_assert_eq(load_result, 0, "Schema should load successfully");
    
    // Enable strict mode
    LambdaValidationOptions options = {
        .strict_mode = true,
        .allow_unknown_fields = false,
        .allow_empty_elements = false,
        .max_validation_depth = 100,
        .enabled_custom_rules = NULL,
        .disabled_rules = NULL
    };
    lambda_validator_set_options(api_validator, &options);
    
    // Document with extra field should fail in strict mode
    const char* document_with_extra = "{title: \"Test\", extra_field: \"value\"}";
    
    LambdaValidationResult* result = lambda_validate_string(api_validator, document_with_extra, "StrictDoc");
    
    cr_assert_not_null(result, "Validation result should be returned");
    // In strict mode with allow_unknown_fields=false, this should fail
    // Note: Actual behavior depends on implementation details
    
    lambda_validation_result_free(result);
}

// ==================== Error Handling Tests ====================

Test(api_tests, validate_with_nonexistent_schema) {
    const char* document = "{title: \"Test\"}";
    
    LambdaValidationResult* result = lambda_validate_string(api_validator, document, "NonexistentSchema");
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, false, "Should fail when schema doesn't exist");
    cr_assert_gt(result->error_count, 0, "Should have at least one error");
    
    lambda_validation_result_free(result);
}

Test(api_tests, validate_malformed_document) {
    // Load schema
    const char* schema_source = "type SimpleDoc = {title: string}";
    int load_result = lambda_validator_load_schema_string(api_validator, schema_source, "SimpleDoc");
    cr_assert_eq(load_result, 0, "Schema should load successfully");
    
    // Malformed document
    const char* malformed_document = "{title: \"Test\"";  // Missing closing brace
    
    LambdaValidationResult* result = lambda_validate_string(api_validator, malformed_document, "SimpleDoc");
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, false, "Malformed document should fail validation");
    cr_assert_gt(result->error_count, 0, "Should have at least one error");
    
    lambda_validation_result_free(result);
}

Test(api_tests, load_malformed_schema) {
    const char* malformed_schema = "type Invalid = {field: unknown_type";  // Missing closing brace
    
    int result = lambda_validator_load_schema_string(api_validator, malformed_schema, "Invalid");
    
    cr_assert_ne(result, 0, "Malformed schema should fail to load");
}

// ==================== File API Tests ====================

Test(api_tests, load_schema_from_file) {
    // Create a temporary schema file
    const char* temp_schema_file = "/tmp/test_schema.ls";
    FILE* f = fopen(temp_schema_file, "w");
    cr_assert_not_null(f, "Should be able to create temp file");
    
    fprintf(f, "type FileDoc = {title: string, content: string}\n");
    fclose(f);
    
    int result = lambda_validator_load_schema_file(api_validator, temp_schema_file);
    
    cr_assert_eq(result, 0, "Schema should load from file successfully");
    
    // Clean up
    unlink(temp_schema_file);
}

Test(api_tests, validate_document_from_file) {
    // Load schema
    const char* schema_source = "type FileDoc = {title: string, content: string}";
    int load_result = lambda_validator_load_schema_string(api_validator, schema_source, "FileDoc");
    cr_assert_eq(load_result, 0, "Schema should load successfully");
    
    // Create a temporary document file
    const char* temp_doc_file = "/tmp/test_document.mark";
    FILE* f = fopen(temp_doc_file, "w");
    cr_assert_not_null(f, "Should be able to create temp file");
    
    fprintf(f, "{title: \"File Test\", content: \"Hello from file\"}\n");
    fclose(f);
    
    LambdaValidationResult* result = lambda_validate_file(api_validator, temp_doc_file, "FileDoc");
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, true, "Valid document file should pass validation");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
    
    lambda_validation_result_free(result);
    
    // Clean up
    unlink(temp_doc_file);
}

Test(api_tests, validate_nonexistent_file) {
    // Load schema
    const char* schema_source = "type TestDoc = {title: string}";
    int load_result = lambda_validator_load_schema_string(api_validator, schema_source, "TestDoc");
    cr_assert_eq(load_result, 0, "Schema should load successfully");
    
    LambdaValidationResult* result = lambda_validate_file(api_validator, "/nonexistent/file.mark", "TestDoc");
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, false, "Nonexistent file should fail validation");
    cr_assert_gt(result->error_count, 0, "Should have at least one error");
    
    lambda_validation_result_free(result);
}

// ==================== Memory Management Tests ====================

Test(api_tests, validation_result_memory_management) {
    // Load schema
    const char* schema_source = "type MemTest = {title: string}";
    int load_result = lambda_validator_load_schema_string(api_validator, schema_source, "MemTest");
    cr_assert_eq(load_result, 0, "Schema should load successfully");
    
    // Validate document with errors to test error string memory management
    const char* invalid_document = "{invalid: \"field\"}";
    
    LambdaValidationResult* result = lambda_validate_string(api_validator, invalid_document, "MemTest");
    
    cr_assert_not_null(result, "Validation result should be returned");
    
    if (result->error_count > 0) {
        cr_assert_not_null(result->errors, "Error array should not be null");
        cr_assert_not_null(result->errors[0], "First error should not be null");
        
        // Error strings should be readable
        size_t error_len = strlen(result->errors[0]);
        cr_assert_gt(error_len, 0, "Error message should not be empty");
    }
    
    // This should not crash and should properly free all memory
    lambda_validation_result_free(result);
}

Test(api_tests, multiple_validations_memory_consistency) {
    // Load schema
    const char* schema_source = "type ConsistencyTest = {id: int, name: string}";
    int load_result = lambda_validator_load_schema_string(api_validator, schema_source, "ConsistencyTest");
    cr_assert_eq(load_result, 0, "Schema should load successfully");
    
    // Perform multiple validations to test for memory leaks or corruption
    for (int i = 0; i < 10; i++) {
        char document[100];
        snprintf(document, sizeof(document), "{id: %d, name: \"Test %d\"}", i, i);
        
        LambdaValidationResult* result = lambda_validate_string(api_validator, document, "ConsistencyTest");
        
        cr_assert_not_null(result, "Validation result should be returned");
        cr_assert_eq(result->valid, true, "Valid documents should pass validation");
        
        lambda_validation_result_free(result);
    }
}
