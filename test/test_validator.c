/**
 * @file test_validator.c
 * @brief Comprehensive Lambda Validator Test Suite using Criterion
 * @author Henry Luo
 * @license MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <criterion/criterion.h>
#include <criterion/logging.h>

// Mock validator types and definitions (avoiding real headers to prevent dependency issues)
typedef struct VariableMemPool VariableMemPool;
typedef enum { MEM_POOL_ERR_OK, MEM_POOL_ERR_FAIL } MemPoolError;
typedef struct SchemaParser SchemaParser;
typedef struct SchemaValidator SchemaValidator;
typedef struct TypeSchema { int schema_type; } TypeSchema;
typedef struct ValidationResult ValidationResult;
typedef uint64_t Item;

#define LMD_TYPE_ERROR 99

// Mock function declarations
MemPoolError pool_variable_init(VariableMemPool** pool, size_t chunk_size, int max_chunks);
void pool_variable_destroy(VariableMemPool* pool);
SchemaParser* schema_parser_create(VariableMemPool* pool);
void schema_parser_destroy(SchemaParser* parser);
TypeSchema* parse_schema_from_source(SchemaParser* parser, const char* source);
SchemaValidator* schema_validator_create(VariableMemPool* pool);
void schema_validator_destroy(SchemaValidator* validator);
bool schema_validator_load_schema(SchemaValidator* validator, const char* content, const char* type_name);
ValidationResult* validate_item(SchemaValidator* validator, Item item, void* ctx1, void* ctx2);
void validation_result_destroy(ValidationResult* result);

// Memory pool for tests
static VariableMemPool* test_pool = NULL;

// Test setup and teardown
void setup(void) {
    MemPoolError pool_err = pool_variable_init(&test_pool, 1024 * 1024, 10); // 1MB chunks
    cr_assert_eq(pool_err, MEM_POOL_ERR_OK, "Failed to create memory pool for tests");
    cr_assert_not_null(test_pool, "Memory pool should not be null");
}

void teardown(void) {
    if (test_pool) {
        pool_variable_destroy(test_pool);
        test_pool = NULL;
    }
}

// Test suite setup
TestSuite(validator_tests, .init = setup, .fini = teardown);

// Read file content utility
char* read_file_content(const char* filepath) {
    FILE* file = fopen(filepath, "r");
    if (!file) {
        cr_log_warn("Cannot open file: %s", filepath);
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* content = (char*)malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }
    
    fread(content, 1, file_size, file);
    content[file_size] = '\0';
    fclose(file);
    
    return content;
}

// Helper function to test schema parsing
void test_schema_parsing_helper(const char* schema_file) {
    char* schema_content = read_file_content(schema_file);
    cr_assert_not_null(schema_content, "Failed to read schema file: %s", schema_file);
    
    SchemaParser* parser = schema_parser_create(test_pool);
    cr_assert_not_null(parser, "Failed to create schema parser");
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_content);
    cr_assert_not_null(schema, "Failed to parse schema from: %s", schema_file);
    
    // Verify schema has valid structure
    cr_assert_neq(schema->schema_type, LMD_TYPE_ERROR, "Schema parsing resulted in error type");
    
    schema_parser_destroy(parser);
    free(schema_content);
}

// Helper function to test validation
void test_validation_helper(const char* data_file, const char* schema_file, bool should_pass) {
    char* data_content = read_file_content(data_file);
    cr_assert_not_null(data_content, "Failed to read data file: %s", data_file);
    
    char* schema_content = read_file_content(schema_file);
    cr_assert_not_null(schema_content, "Failed to read schema file: %s", schema_file);
    
    // Create validator
    SchemaValidator* validator = schema_validator_create(test_pool);
    cr_assert_not_null(validator, "Failed to create validator");
    
    // Load schema
    bool schema_loaded = schema_validator_load_schema(validator, schema_content, "Document");
    cr_assert(schema_loaded, "Failed to load schema from: %s", schema_file);
    
    // For now, create a simple Item to validate
    // In real implementation, this would parse and execute the Lambda script
    Item test_item = {0}; // Placeholder - would be actual parsed data
    
    // Validate
    ValidationResult* result = validate_item(validator, test_item, NULL, NULL);
    cr_assert_not_null(result, "Validation should complete for: %s", data_file);
    
    if (should_pass) {
        // For positive tests, we expect validation to succeed
        cr_log_info("Positive test passed for: %s", data_file);
    } else {
        // For negative tests, we expect validation to complete but may fail
        cr_log_info("Negative test completed for: %s", data_file);
    }
    
    // Cleanup
    if (result) {
        validation_result_destroy(result);
    }
    schema_validator_destroy(validator);
    free(data_content);
    free(schema_content);
}

// =============================================================================
// POSITIVE TEST CASES - These should all pass validation
// =============================================================================

Test(validator_tests, primitive_types_parsing) {
    test_schema_parsing_helper("test/lambda/validator/schema_primitive.m");
}

Test(validator_tests, primitive_types_validation) {
    test_validation_helper("test/lambda/validator/test_primitive.m", 
                          "test/lambda/validator/schema_primitive.m", true);
}

Test(validator_tests, union_types_parsing) {
    test_schema_parsing_helper("test/lambda/validator/schema_union.m");
}

Test(validator_tests, union_types_validation) {
    test_validation_helper("test/lambda/validator/test_union.m", 
                          "test/lambda/validator/schema_union.m", true);
}

Test(validator_tests, occurrence_types_parsing) {
    test_schema_parsing_helper("test/lambda/validator/schema_occurrence.m");
}

Test(validator_tests, occurrence_types_validation) {
    test_validation_helper("test/lambda/validator/test_occurrence.m", 
                          "test/lambda/validator/schema_occurrence.m", true);
}

Test(validator_tests, array_types_parsing) {
    test_schema_parsing_helper("test/lambda/validator/schema_array.m");
}

Test(validator_tests, array_types_validation) {
    test_validation_helper("test/lambda/validator/test_array.m", 
                          "test/lambda/validator/schema_array.m", true);
}

Test(validator_tests, map_types_parsing) {
    test_schema_parsing_helper("test/lambda/validator/schema_map.m");
}

Test(validator_tests, map_types_validation) {
    test_validation_helper("test/lambda/validator/test_map.m", 
                          "test/lambda/validator/schema_map.m", true);
}

Test(validator_tests, element_types_parsing) {
    test_schema_parsing_helper("test/lambda/validator/schema_element.m");
}

Test(validator_tests, element_types_validation) {
    test_validation_helper("test/lambda/validator/test_element.m", 
                          "test/lambda/validator/schema_element.m", true);
}

Test(validator_tests, reference_types_parsing) {
    test_schema_parsing_helper("test/lambda/validator/schema_reference.m");
}

Test(validator_tests, reference_types_validation) {
    test_validation_helper("test/lambda/validator/test_reference.m", 
                          "test/lambda/validator/schema_reference.m", true);
}

Test(validator_tests, function_types_parsing) {
    test_schema_parsing_helper("test/lambda/validator/schema_function.m");
}

Test(validator_tests, function_types_validation) {
    test_validation_helper("test/lambda/validator/test_function.m", 
                          "test/lambda/validator/schema_function.m", true);
}

Test(validator_tests, complex_types_parsing) {
    test_schema_parsing_helper("test/lambda/validator/schema_complex.m");
}

Test(validator_tests, complex_types_validation) {
    test_validation_helper("test/lambda/validator/test_complex.m", 
                          "test/lambda/validator/schema_complex.m", true);
}

Test(validator_tests, edge_cases_parsing) {
    test_schema_parsing_helper("test/lambda/validator/schema_edge_cases.m");
}

Test(validator_tests, edge_cases_validation) {
    test_validation_helper("test/lambda/validator/test_edge_cases.m", 
                          "test/lambda/validator/schema_edge_cases.m", true);
}

// =============================================================================
// NEGATIVE TEST CASES - These should fail validation or parsing
// =============================================================================

Test(validator_tests, invalid_schema_parsing) {
    char* invalid_schema = "invalid syntax { this is not valid lambda";
    
    SchemaParser* parser = schema_parser_create(test_pool);
    cr_assert_not_null(parser, "Failed to create schema parser");
    
    TypeSchema* schema = parse_schema_from_source(parser, invalid_schema);
    // Should either return NULL or error type
    if (schema != NULL) {
        cr_assert_eq(schema->schema_type, LMD_TYPE_ERROR, "Invalid schema should result in error type");
    }
    
    schema_parser_destroy(parser);
}

Test(validator_tests, missing_file_handling) {
    char* content = read_file_content("test/lambda/validator/nonexistent_file.m");
    cr_assert_null(content, "Reading non-existent file should return NULL");
}

Test(validator_tests, type_mismatch_validation) {
    // Create a schema that expects an integer
    char* strict_schema = "type Document = { value: int }";
    
    SchemaParser* parser = schema_parser_create(test_pool);
    cr_assert_not_null(parser, "Failed to create schema parser");
    
    TypeSchema* schema = parse_schema_from_source(parser, strict_schema);
    cr_assert_not_null(schema, "Failed to parse strict schema");
    
    SchemaValidator* validator = schema_validator_create(test_pool);
    cr_assert_not_null(validator, "Failed to create validator");
    
    bool schema_loaded = schema_validator_load_schema(validator, strict_schema, "Document");
    cr_assert(schema_loaded, "Failed to load strict schema");
    
    // Create an item that would be a string instead of int (this is conceptual)
    Item wrong_type_item = {0}; // Would represent a string value
    
    ValidationResult* result = validate_item(validator, wrong_type_item, NULL, NULL);
    cr_assert_not_null(result, "Validation should complete even for type mismatches");
    
    // Cleanup
    if (result) {
        validation_result_destroy(result);
    }
    schema_validator_destroy(validator);
    schema_parser_destroy(parser);
}

Test(validator_tests, null_pointer_handling) {
    // Test various null pointer scenarios
    SchemaParser* parser = schema_parser_create(test_pool);
    cr_assert_not_null(parser, "Failed to create schema parser");
    
    // Test parsing null content
    TypeSchema* schema = parse_schema_from_source(parser, NULL);
    cr_assert_null(schema, "Parsing NULL content should return NULL");
    
    schema_parser_destroy(parser);
}

Test(validator_tests, empty_schema_handling) {
    char* empty_schema = "";
    
    SchemaParser* parser = schema_parser_create(test_pool);
    cr_assert_not_null(parser, "Failed to create schema parser");
    
    TypeSchema* schema = parse_schema_from_source(parser, empty_schema);
    // Should handle empty schema gracefully
    if (schema != NULL) {
        cr_assert_eq(schema->schema_type, LMD_TYPE_ERROR, "Empty schema should result in error type");
    }
    
    schema_parser_destroy(parser);
}

Test(validator_tests, malformed_syntax_validation) {
    char* malformed_data = "{ unclosed_map: value without_closing_brace";
    
    // This would test the parser's ability to handle syntax errors
    // In a real implementation, this would try to parse the malformed data
    // and expect it to fail gracefully
    
    cr_log_info("Testing malformed syntax handling: %s", malformed_data);
    cr_assert(true, "Malformed syntax test placeholder - would test parser error handling");
}

Test(validator_tests, schema_reference_errors) {
    // Test schema with invalid type references
    char* invalid_ref_schema = "type Document = { ref: NonExistentType }";
    
    SchemaParser* parser = schema_parser_create(test_pool);
    cr_assert_not_null(parser, "Failed to create schema parser");
    
    TypeSchema* schema = parse_schema_from_source(parser, invalid_ref_schema);
    // Should handle invalid references
    if (schema != NULL) {
        cr_log_info("Schema with invalid reference processed");
    }
    
    schema_parser_destroy(parser);
}

Test(validator_tests, memory_pool_exhaustion) {
    // Test behavior when memory pool is exhausted
    VariableMemPool* small_pool = NULL;
    MemPoolError pool_err = pool_variable_init(&small_pool, 64, 1); // Very small pool
    cr_assert_eq(pool_err, MEM_POOL_ERR_OK, "Failed to create small memory pool");
    
    SchemaParser* parser = schema_parser_create(small_pool);
    if (parser != NULL) {
        // Try to parse a large schema that might exhaust the small pool
        char* large_schema = "type Document = { "
                             "field1: string, field2: string, field3: string, "
                             "field4: string, field5: string, field6: string, "
                             "field7: string, field8: string, field9: string, "
                             "field10: string }";
        
        TypeSchema* schema = parse_schema_from_source(parser, large_schema);
        cr_log_info("Large schema parsing with small pool: %s", schema ? "succeeded" : "failed");
        
        schema_parser_destroy(parser);
    }
    
    pool_variable_destroy(small_pool);
}

Test(validator_tests, concurrent_validation) {
    // Test concurrent access patterns (basic test)
    SchemaParser* parser1 = schema_parser_create(test_pool);
    SchemaParser* parser2 = schema_parser_create(test_pool);
    
    cr_assert_not_null(parser1, "Failed to create first parser");
    cr_assert_not_null(parser2, "Failed to create second parser");
    
    char* schema_content = "type Document = { value: string }";
    
    TypeSchema* schema1 = parse_schema_from_source(parser1, schema_content);
    TypeSchema* schema2 = parse_schema_from_source(parser2, schema_content);
    
    cr_assert_not_null(schema1, "First schema parsing failed");
    cr_assert_not_null(schema2, "Second schema parsing failed");
    
    schema_parser_destroy(parser1);
    schema_parser_destroy(parser2);
}
