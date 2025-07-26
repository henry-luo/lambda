#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mem_pool/mem_pool.h>
#include "../validator.h"

// Simple test to verify basic compilation and API
TestSuite(simple_tests);

Test(simple_tests, validator_lifecycle) {
    // Test creating and destroying the validator
    LambdaValidator* validator = lambda_validator_create(); 
    cr_assert_not_null(validator, "Validator should be created successfully");
    
    lambda_validator_destroy(validator);
    // Test passes if we get here without crashing
}

Test(simple_tests, basic_validation_types) {
    // Test basic validation constants exist
    cr_assert_eq(VALID_ERROR_TYPE_MISMATCH, VALID_ERROR_TYPE_MISMATCH);
    cr_assert_eq(VALID_ERROR_CONSTRAINT_VIOLATION, VALID_ERROR_CONSTRAINT_VIOLATION);
    cr_assert_eq(VALID_ERROR_MISSING_FIELD, VALID_ERROR_MISSING_FIELD);
    cr_assert_eq(VALID_ERROR_UNEXPECTED_FIELD, VALID_ERROR_UNEXPECTED_FIELD);
}

Test(simple_tests, schema_types_defined) {
    // Test that schema types are defined
    cr_assert_eq(LMD_SCHEMA_PRIMITIVE, LMD_SCHEMA_PRIMITIVE);
    cr_assert_eq(LMD_SCHEMA_UNION, LMD_SCHEMA_UNION);
    cr_assert_eq(LMD_SCHEMA_ARRAY, LMD_SCHEMA_ARRAY);
    cr_assert_eq(LMD_SCHEMA_MAP, LMD_SCHEMA_MAP);
    cr_assert_eq(LMD_SCHEMA_ELEMENT, LMD_SCHEMA_ELEMENT);
}
