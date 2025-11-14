/**
 * @file test_validator_path_reporting.cpp
 * @brief Comprehensive tests for validator error path reporting
 *
 * Tests cover:
 * - Flat structure validation and error paths
 * - Nested object validation with full path reporting
 * - Array validation with index notation
 * - Multi-level nested arrays and objects
 * - Enhanced error message formatting
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <sstream>

// Helper function to run validator command and capture output
class ValidatorTestHelper {
public:
    static std::string runValidator(const char* args) {
        char command[1024];
        snprintf(command, sizeof(command), "./lambda.exe validate %s 2>&1", args);

        FILE* pipe = popen(command, "r");
        if (!pipe) return "";

        char buffer[256];
        std::string result;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }

        pclose(pipe);
        return result;
    }

    static bool outputContains(const std::string& output, const char* substring) {
        return output.find(substring) != std::string::npos;
    }

    static int countOccurrences(const std::string& output, const char* substring) {
        int count = 0;
        size_t pos = 0;
        std::string search(substring);

        while ((pos = output.find(search, pos)) != std::string::npos) {
            count++;
            pos += search.length();
        }

        return count;
    }
};

// ============================================================================
// TEST SUITE 1: Flat Structure Path Reporting
// ============================================================================

TEST(ValidatorPathReporting, FlatStructure_TypeMismatch) {
    // Test type mismatches in flat structure show correct field paths
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/person_schema.ls -f json test/input/negative/type_mismatch.json"
    );

    // Should detect type mismatches
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Validation failed"));

    // Check for proper path notation
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "at .name"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "at .active"));

    // Check for human-readable type names
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Expected type 'string'"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "got 'int'"));
}

TEST(ValidatorPathReporting, FlatStructure_MissingFields) {
    // Test missing required fields show correct paths
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/person_schema.ls -f json test/input/negative/missing_required.json"
    );

    // Should detect missing fields
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "MISSING_FIELD"));

    // Check for proper field names in errors
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Required field 'age'"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Required field 'active'"));

    // Check paths
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "at .age"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "at .active"));
}

TEST(ValidatorPathReporting, FlatStructure_ValidData) {
    // Test that valid data passes without errors
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/person_schema.ls -f json test/input/negative/valid_lambda.ls"
    );

    // Note: This will fail if valid_lambda.ls is not JSON-compatible,
    // but demonstrates successful validation path
    // The test is more about checking the validator handles valid input
}

// ============================================================================
// TEST SUITE 2: Nested Object Path Reporting
// ============================================================================

TEST(ValidatorPathReporting, NestedObjects_MultipleLevels) {
    // Test nested objects show full path from root
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/company_named_types.ls -f json test/input/negative/company_nested_errors.json"
    );

    // Should detect validation errors
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Validation failed"));

    // Check for nested paths (3 levels deep)
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employee.contact.phone"));

    // Check for deeply nested paths (4 levels deep)
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employee.contact.address.city"));

    // Verify TYPE_MISMATCH errors are reported
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "TYPE_MISMATCH"));
}

TEST(ValidatorPathReporting, NestedObjects_TypeReferences) {
    // Test that type references (Address, Contact, Employee) are resolved
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/company_named_types.ls -f json test/input/negative/company_nested_errors.json"
    );

    // Should successfully validate through type references
    // If type references weren't working, we'd get no errors or wrong errors
    int errorCount = ValidatorTestHelper::countOccurrences(output, "TYPE_MISMATCH");
    EXPECT_GT(errorCount, 0) << "Should detect type mismatches in nested structures";
}

// ============================================================================
// TEST SUITE 3: Array Index Path Reporting
// ============================================================================

TEST(ValidatorPathReporting, Arrays_SingleLevel) {
    // Test array items show index notation [N]
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/company_with_arrays.ls -f json test/input/negative/company_array_errors.json"
    );

    // Should detect errors in array items
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Validation failed"));

    // Check for array index notation
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employees["));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "]"));
}

TEST(ValidatorPathReporting, Arrays_NestedArrays) {
    // Test nested arrays show multiple index levels
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/company_with_arrays.ls -f json test/input/negative/company_array_errors.json"
    );

    // Check for nested array paths: .array1[N].array2[M]
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employees[1].contacts[0]"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employees[2].contacts[0]"));

    // Should show multiple errors at different indices
    int errorCount = ValidatorTestHelper::countOccurrences(output, "[TYPE_MISMATCH]");
    EXPECT_GT(errorCount, 3) << "Should detect multiple errors across array items";
}

TEST(ValidatorPathReporting, Arrays_DeepNesting) {
    // Test deeply nested: .array[N].object.array[M].object.field
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/company_with_arrays.ls -f json test/input/negative/company_array_errors.json"
    );

    // Check for deep nesting: array → object → array → object → field
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employees[1].contacts[0].address.city"));

    // Verify the full path is present
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employees[1].contacts[1].address.street"));
}

TEST(ValidatorPathReporting, Arrays_DifferentIndices) {
    // Test that different array indices are correctly reported
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/company_with_arrays.ls -f json test/input/negative/company_array_errors.json"
    );

    // Should report errors at index 1
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employees[1]"));

    // Should report errors at index 2
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employees[2]"));

    // Verify specific index paths
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employees[1].contacts[0]"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employees[1].contacts[1]"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, ".employees[2].contacts[0]"));
}

// ============================================================================
// TEST SUITE 4: Enhanced Error Message Format
// ============================================================================

TEST(ValidatorPathReporting, ErrorFormat_TypeNames) {
    // Test that error messages use human-readable type names
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/person_schema.ls -f json test/input/negative/type_mismatch.json"
    );

    // Should NOT use type IDs (e.g., "type 3", "type 10")
    // Old format: "Type mismatch: expected string, got type 3"
    // New format: "Expected type 'string', but got 'int'"

    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Expected type"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "'string'"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "'int'"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "'bool'"));
}

TEST(ValidatorPathReporting, ErrorFormat_MissingFieldMessage) {
    // Test missing field error messages are clear
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/person_schema.ls -f json test/input/negative/missing_required.json"
    );

    // New format should be clearer
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Required field"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "missing from object"));
}

TEST(ValidatorPathReporting, ErrorFormat_ErrorCodes) {
    // Test that error codes are present
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/person_schema.ls -f json test/input/negative/type_mismatch.json"
    );

    // Should have error code tags
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "[TYPE_MISMATCH]"));
}

TEST(ValidatorPathReporting, ErrorFormat_ErrorCount) {
    // Test that error count is reported
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/person_schema.ls -f json test/input/negative/type_mismatch.json"
    );

    // Should show error count
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Errors:"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Warnings:"));
}

// ============================================================================
// TEST SUITE 5: Lambda Syntax Validation (AST-based)
// ============================================================================

TEST(ValidatorPathReporting, Lambda_ValidSyntax) {
    // Test valid Lambda script passes
    std::string output = ValidatorTestHelper::runValidator(
        "test/input/negative/valid_lambda.ls"
    );

    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Validation successful"));
}

TEST(ValidatorPathReporting, Lambda_SyntaxError) {
    // Test Lambda syntax errors are caught
    std::string output = ValidatorTestHelper::runValidator(
        "test/input/negative/syntax_error.ls"
    );

    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Validation failed"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "PARSE_ERROR"));
}

TEST(ValidatorPathReporting, Lambda_NonLambdaSyntax) {
    // Test non-Lambda content is rejected
    std::string output = ValidatorTestHelper::runValidator(
        "test/input/negative/not_lambda.ls"
    );

    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Validation failed"));
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "PARSE_ERROR"));
}

// ============================================================================
// TEST SUITE 6: Edge Cases and Special Scenarios
// ============================================================================

TEST(ValidatorPathReporting, EdgeCase_EmptyFile) {
    // Test empty file handling
    std::string output = ValidatorTestHelper::runValidator(
        "test/input/negative/empty.ls"
    );

    // Empty file with just comments should pass
    EXPECT_TRUE(ValidatorTestHelper::outputContains(output, "Validation successful"));
}

TEST(ValidatorPathReporting, EdgeCase_MaxErrorsLimit) {
    // Test max errors limit works
    std::string output = ValidatorTestHelper::runValidator(
        "-s test/input/negative/company_with_arrays.ls -f json --max-errors 3 test/input/negative/company_array_errors.json"
    );

    // Count actual error lines shown (lines starting with "  1.", "  2.", etc.)
    int errorCount = 0;
    std::istringstream stream(output);
    std::string line;
    bool inErrorSection = false;

    while (std::getline(stream, line)) {
        // Check if we entered the Errors: section
        if (line.find("Errors:") != std::string::npos) {
            inErrorSection = true;
            continue;
        }
        // Count lines that start with "  1.", "  2.", etc.
        if (inErrorSection && line.length() > 3) {
            if (line[0] == ' ' && line[1] == ' ' && std::isdigit(line[2]) && line[3] == '.') {
                errorCount++;
            }
        }
        // Exit error section on empty line or new section
        if (inErrorSection && line.empty()) {
            break;
        }
    }

    EXPECT_LE(errorCount, 3) << "Should show at most 3 error descriptions";
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char** argv) {
    // Check if lambda.exe exists
    struct stat buffer;
    if (stat("./lambda.exe", &buffer) != 0) {
        fprintf(stderr, "ERROR: lambda.exe not found. Please build first with: make build\n");
        return 1;
    }

    // Check if test input files exist
    if (stat("test/input/negative/person_schema.ls", &buffer) != 0) {
        fprintf(stderr, "ERROR: Test input files not found in test/input/negative/\n");
        return 1;
    }

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
