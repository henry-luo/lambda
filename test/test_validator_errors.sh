#!/bin/bash
# Comprehensive Validator Error Reporting Tests

cd /Users/henryluo/Projects/Jubily

echo "========================================================================"
echo "Lambda Validator - Comprehensive Error Reporting Demonstration"
echo "========================================================================"
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 1: Empty Lambda File (Should detect empty content)"
echo "------------------------------------------------------------------------"
./lambda.exe validate test/input/negative/empty.ls
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 2: Non-Lambda Syntax (Should fail syntax check)"
echo "------------------------------------------------------------------------"
./lambda.exe validate test/input/negative/not_lambda.ls
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 3: Syntax Error with Missing Braces"
echo "------------------------------------------------------------------------"
./lambda.exe validate test/input/negative/syntax_error.ls
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 4: Type Mismatch Errors (name should be string, not int)"
echo "------------------------------------------------------------------------"
./lambda.exe validate -s test/input/negative/person_schema.ls -f json test/input/negative/type_mismatch.json
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 5: Missing Required Fields (age and active missing)"
echo "------------------------------------------------------------------------"
./lambda.exe validate -s test/input/negative/person_schema.ls -f json test/input/negative/missing_required.json
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 6: Unknown Fields (with strict mode disabled - should pass)"
echo "------------------------------------------------------------------------"
./lambda.exe validate -s test/input/negative/person_schema.ls -f json test/input/negative/unknown_fields.json
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 7: Unknown Fields (with --allow-unknown flag)"
echo "------------------------------------------------------------------------"
./lambda.exe validate -s test/input/negative/person_schema.ls -f json --allow-unknown test/input/negative/unknown_fields.json
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 8: Nested Structure Errors (multiple levels of type errors)"
echo "------------------------------------------------------------------------"
./lambda.exe validate -s test/input/negative/nested_schema.ls -f json test/input/negative/nested_errors.json
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 9: Invalid XML (mismatched tags)"
echo "------------------------------------------------------------------------"
./lambda.exe validate -s test/input/negative/article_schema.ls -f xml test/input/negative/invalid_xml.xml
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 10: Max Errors Limit (stop after 2 errors)"
echo "------------------------------------------------------------------------"
./lambda.exe validate -s test/input/negative/nested_schema.ls -f json --max-errors 2 test/input/negative/nested_errors.json
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 11: Strict Mode (requires explicit null)"
echo "------------------------------------------------------------------------"
./lambda.exe validate -s test/input/negative/person_schema.ls -f json --strict test/input/negative/unknown_fields.json
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 12: Nested Object Path Reporting (3-4 levels deep)"
echo "------------------------------------------------------------------------"
echo "Testing: .employee.contact.address.city path reporting"
./lambda.exe validate -s test/input/negative/company_named_types.ls -f json test/input/negative/company_nested_errors.json
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 13: Array Index Path Reporting"
echo "------------------------------------------------------------------------"
echo "Testing: .employees[N].contacts[M].address.city path reporting"
./lambda.exe validate -s test/input/negative/company_with_arrays.ls -f json test/input/negative/company_array_errors.json
echo ""

echo "------------------------------------------------------------------------"
echo "TEST 14: Enhanced Error Messages (Human-Readable Types)"
echo "------------------------------------------------------------------------"
echo "Verifying error messages use 'string', 'int', 'bool' instead of type IDs"
./lambda.exe validate -s test/input/negative/person_schema.ls -f json test/input/negative/type_mismatch.json | grep -E "(Expected type|TYPE_MISMATCH)"
echo ""

echo "========================================================================"
echo "Error Reporting Demonstration Complete"
echo "========================================================================"
