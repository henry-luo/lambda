# Lambda Script Structured Testing Plan

## Overview

This document outlines a comprehensive, structured testing framework for the Lambda scripting language engine. The plan addresses the current ad-hoc testing approach and proposes a systematic methodology that covers all data types, expressions, and language features with both positive and negative test cases.

## Current State Analysis

### Existing Test Structure
- **Location**: `./test/lambda/` directory
- **Format**: Individual `.ls` test files with corresponding `.txt` expected output files
- **Coverage**: Partial coverage with some comprehensive tests for specific areas
- **Organization**: Ad-hoc, with tests scattered across different files
- **Execution**: Manual execution and comparison

### Identified Gaps
1. **Inconsistent Coverage**: Some data types and operations are well-tested, others are sparse
2. **Missing Negative Tests**: Limited systematic testing of error conditions
3. **No Test Categories**: Tests are not organized by functionality or complexity
4. **Manual Verification**: No automated test runner or assertion framework
5. **Edge Case Coverage**: Incomplete testing of boundary conditions and type interactions

## Proposed Testing Framework

### 1. Test Organization Structure

```
test/lambda/structured/
├── core/                    # Core language features
│   ├── datatypes/          # Data type tests
│   ├── expressions/        # Expression evaluation tests
│   ├── operators/          # Operator tests
│   ├── control_flow/       # Control flow constructs
│   └── functions/          # Function definition and calls
├── integration/            # Integration tests
│   ├── mixed_types/        # Cross-type operations
│   ├── complex_expr/       # Complex nested expressions
│   └── real_world/         # Real-world scenarios
├── negative/               # Error condition tests
│   ├── syntax_errors/      # Syntax error cases
│   ├── type_errors/        # Type mismatch errors
│   ├── runtime_errors/     # Runtime error conditions
│   └── boundary_errors/    # Boundary condition errors
├── performance/            # Performance regression tests
└── utilities/              # Test utilities and helpers
```

### 2. Data Type Test Categories

#### 2.1 Scalar Types
- **Null**: `LMD_TYPE_NULL`
- **Boolean**: `LMD_TYPE_BOOL`
- **Integer**: `LMD_TYPE_INT`, `LMD_TYPE_INT64`
- **Float**: `LMD_TYPE_FLOAT`
- **Decimal**: `LMD_TYPE_DECIMAL`
- **Number**: `LMD_TYPE_NUMBER` (union type)
- **String**: `LMD_TYPE_STRING`
- **Symbol**: `LMD_TYPE_SYMBOL`
- **Binary**: `LMD_TYPE_BINARY`
- **DateTime**: `LMD_TYPE_DTIME`

#### 2.2 Container Types
- **List**: `LMD_TYPE_LIST`
- **Range**: `LMD_TYPE_RANGE`
- **Array**: `LMD_TYPE_ARRAY`, `LMD_TYPE_ARRAY_INT`, `LMD_TYPE_ARRAY_FLOAT`
- **Map**: `LMD_TYPE_MAP`
- **Element**: `LMD_TYPE_ELEMENT`

#### 2.3 Meta Types
- **Type**: `LMD_TYPE_TYPE`
- **Function**: `LMD_TYPE_FUNC`
- **Any**: `LMD_TYPE_ANY`
- **Error**: `LMD_TYPE_ERROR`

### 3. Expression Test Categories

#### 3.1 Arithmetic Operations
- **Addition**: `+` (numeric, string concatenation, container joining)
- **Subtraction**: `-`
- **Multiplication**: `*` (numeric, string repetition)
- **Division**: `/` (float division)
- **Integer Division**: `_/`
- **Modulo**: `%`
- **Power**: `^`
- **Unary Plus**: `+`
- **Unary Minus**: `-`

#### 3.2 Comparison Operations
- **Equality**: `==`, `!=`
- **Relational**: `<`, `>`, `<=`, `>=`
- **Type Checking**: `is`
- **Membership**: `in`

#### 3.3 Logical Operations
- **Logical AND**: `and`
- **Logical OR**: `or`
- **Logical NOT**: `not`

#### 3.4 Access Operations
- **Member Access**: `.`
- **Index Access**: `[]`
- **Range Creation**: `to`

### 4. Test Case Design Principles

#### 4.1 Positive Test Cases
- **Valid Operations**: Test all valid combinations of types and operators
- **Edge Cases**: Boundary values, empty containers, zero values
- **Type Coercion**: Implicit type conversions where supported
- **Complex Expressions**: Nested operations with proper precedence

#### 4.2 Negative Test Cases
- **Type Mismatches**: Invalid type combinations for operations
- **Division by Zero**: All division operations with zero divisors
- **Out of Bounds**: Array/list access beyond valid indices
- **Null Operations**: Operations involving null values where invalid
- **Syntax Errors**: Malformed expressions and statements

#### 4.3 Boundary Test Cases
- **Numeric Limits**: Maximum and minimum values for integer and float types
- **String Limits**: Very long strings, empty strings, Unicode edge cases
- **Container Limits**: Empty containers, single-element containers, very large containers
- **Precision Limits**: Floating-point precision edge cases

### 5. Test File Naming Convention

```
[category]_[subcategory]_[test_type].ls

Examples:
- datatypes_int_positive.ls
- datatypes_int_negative.ls
- datatypes_int_boundary.ls
- expressions_arithmetic_mixed_types.ls
- operators_comparison_string_error.ls
- integration_complex_nested_expr.ls
```

### 6. Test File Structure

Each test file should follow this structure:

```lambda
// Test: [Description]
// Category: [Category]
// Type: [Positive/Negative/Boundary]
// Expected: [Success/Error/Specific Output]

"===== [TEST SECTION NAME] ====="

// Test case 1: [Description]
[test expression]

// Test case 2: [Description]  
[test expression]

// ... more test cases

"===== END [TEST SECTION NAME] ====="
```

### 7. Automated Test Runner

#### 7.1 Test Runner Features
- **Batch Execution**: Run all tests or specific categories
- **Output Comparison**: Compare actual vs expected output
- **Error Detection**: Identify tests that should fail but don't
- **Performance Tracking**: Monitor execution time for performance tests
- **Report Generation**: Generate detailed test reports

#### 7.2 Test Runner Implementation
```bash
#!/bin/bash
# test_runner.sh

LAMBDA_EXEC="./lambda"
TEST_DIR="test/lambda/structured"
RESULTS_DIR="test/results"

# Functions for running different test categories
run_positive_tests() { ... }
run_negative_tests() { ... }
run_boundary_tests() { ... }
run_integration_tests() { ... }
run_performance_tests() { ... }
```

### 8. Specific Test Plans

#### 8.1 Data Type Tests

**Integer Tests** (`datatypes_int_*.ls`):
- Positive: Basic arithmetic, type coercion, literals
- Negative: Overflow, invalid operations with non-numeric types
- Boundary: MIN_INT, MAX_INT, zero operations

**Float Tests** (`datatypes_float_*.ls`):
- Positive: Decimal operations, scientific notation, precision
- Negative: Division by zero, invalid type mixing
- Boundary: Infinity, NaN, very small/large numbers

**String Tests** (`datatypes_string_*.ls`):
- Positive: Concatenation, indexing, length, Unicode
- Negative: Invalid index access, type mismatches
- Boundary: Empty strings, very long strings, special characters

**Decimal Tests** (`datatypes_decimal_*.ls`):
- Positive: High-precision arithmetic, financial calculations
- Negative: Invalid operations, type mismatches
- Boundary: Very high precision numbers, rounding edge cases

#### 8.2 Expression Tests

**Arithmetic Expression Tests** (`expressions_arithmetic_*.ls`):
- All operator combinations with all numeric types
- Mixed-type operations and coercion rules
- Operator precedence and associativity
- Complex nested expressions

**Comparison Expression Tests** (`expressions_comparison_*.ls`):
- All comparison operators with compatible types
- Type mismatch error cases
- Chained comparisons
- Comparison with container types

**Logical Expression Tests** (`expressions_logical_*.ls`):
- Boolean logic with all combinations
- Short-circuit evaluation
- Type coercion in logical contexts
- Error cases with invalid types

#### 8.3 Integration Tests

**Mixed Type Operations** (`integration_mixed_types_*.ls`):
- Operations involving multiple different types
- Type coercion chains
- Container operations with mixed element types

**Complex Expressions** (`integration_complex_expr_*.ls`):
- Deeply nested expressions
- Multiple operator precedence levels
- Function calls within expressions
- Conditional expressions with complex conditions

### 9. Test Execution and Verification

#### 9.1 Expected Output Files
Each test file should have a corresponding `.expected` file containing:
- Expected successful output
- Expected error messages for negative tests
- Performance benchmarks for performance tests

#### 9.2 Verification Process
1. **Execute Test**: Run lambda script with test file
2. **Capture Output**: Capture both stdout and stderr
3. **Compare Results**: Compare with expected output
4. **Report Differences**: Highlight any discrepancies
5. **Update Baselines**: Process for updating expected results

### 10. Maintenance and Evolution

#### 10.1 Test Maintenance
- **Regular Review**: Quarterly review of test coverage
- **New Feature Tests**: Add tests for new language features
- **Regression Tests**: Add tests for discovered bugs
- **Performance Baselines**: Update performance expectations

#### 10.2 Coverage Metrics
- **Type Coverage**: Percentage of type combinations tested
- **Operator Coverage**: Percentage of operator/type combinations tested
- **Error Coverage**: Percentage of error conditions tested
- **Feature Coverage**: Percentage of language features tested

## 8. Custom Lambda Test Framework Design

### 8.1 Architecture Overview

The Custom Lambda Test Framework consists of:

1. **Test Runner** (`lambda_test_runner.cpp`) - C++ application that executes Lambda scripts
2. **Test Metadata System** - Comment-based metadata in Lambda test files
3. **Output Formats** - JSON and TAP (Test Anything Protocol) support
4. **Directory Structure** - Organized test categories under `test/std/`

### 8.2 Test File Format

Lambda test files use structured comments for metadata:

```lambda
// Test: Integer Data Type - Positive Cases
// Category: core/datatypes
// Type: positive
// Expected: Various integer operations should work correctly

// Test content here
42
10 + 5
```

### 8.3 Test Runner Features

#### Core Functionality
- **Automatic Discovery**: Recursively finds all `.ls` files in test directories
- **Metadata Parsing**: Extracts test information from structured comments
- **Runtime Integration**: Uses Lambda runtime directly for script execution
- **Performance Tracking**: Measures execution time for each test
- **Error Handling**: Distinguishes between expected failures and unexpected errors

#### Output Formats

**JSON Report** (`test_results.json`):
```json
{
  "summary": {
    "total": 50,
    "passed": 48,
    "failed": 2,
    "execution_time_ms": 1234.56
  },
  "categories": {
    "core/datatypes": 20,
    "core/expressions": 15
  },
  "tests": [
    {
      "name": "Integer Data Type - Positive Cases",
      "category": "core/datatypes",
      "type": "positive",
      "passed": true,
      "execution_time_ms": 12.34
    }
  ]
}
```

**TAP Report** (`test_results.tap`):
```tap
TAP version 13
1..50
ok 1 - Integer Data Type - Positive Cases # category:core/datatypes type:positive time:12.34ms
not ok 2 - String Invalid Operations # category:core/datatypes type:negative error:Type mismatch
  ---
  message: "Type mismatch"
  severity: fail
  data:
    got: "ERROR: Type mismatch"
    expect: "success"
    file: "test/std/core/datatypes/string_negative.ls"
  ...
```

### 8.4 Command Line Interface

```bash
# Basic usage
./lambda_test_runner

# Advanced options
./lambda_test_runner --test-dir test/std \
                    --format both \
                    --json-output results.json \
                    --tap-output results.tap \
                    --verbose

# Help
./lambda_test_runner --help
```

### 8.5 Integration with Build System

**Build Configuration** (`build_lambda_config.json`):
```json
{
  "suite": "lambda-std",
  "name": "🧪 Lambda Standard Tests",
  "sources": ["test/lambda_test_runner.cpp"],
  "library_dependencies": [["lambda-test-runtime"]],
  "binaries": ["test/lambda_test_runner.exe"],
  "type": "lambda-std",
  "special_flags": "-std=c++17 -lstdc++",
  "test_runner": {
    "type": "custom",
    "executable": "test/lambda_test_runner.exe",
    "test_dir": "test/std",
    "output_formats": ["json", "tap"],
    "command_args": ["--test-dir", "test/std", "--format", "both"]
  }
}
```

**Build Commands**:
```bash
# Build all tests (including custom runner)
./test/test_build.sh all

# Run all tests
./test/test_run.sh

# Run only Lambda Standard Tests
./test/test_run.sh --target=lambda-std

# Raw output for debugging
./test/test_run.sh --target=lambda-std --raw
```

### 8.6 CI/CD Integration

The TAP format enables integration with various CI systems:

- **GitHub Actions**: Use `tap-junit` to convert TAP to JUnit XML
- **Jenkins**: Native TAP support via TAP plugin
- **GitLab CI**: TAP parsing for test result visualization

### 8.7 Integration Results

**Seamless Build System Integration**:
- Custom test runner automatically discovered by `test/test_run.sh`
- Compatible JSON output format with existing Criterion tests
- Unified test reporting in `test_output/test_summary.json`
- Full support for parallel and sequential execution modes

**Test Suite Categories**:
```
📚 Library Tests        (Criterion-based C tests)
📄 Input Processing     (Criterion-based C++ tests)  
⚡ MIR JIT Tests        (Criterion-based C tests)
🐑 Lambda Runtime       (Criterion-based C tests)
🧪 Lambda Standard      (Custom test runner)
🔍 Validator Tests      (Criterion-based C++ tests)
```

### 8.8 Advantages Over Criterion Framework

1. **Native Lambda Integration**: Direct runtime usage without C wrapper overhead
2. **Rich Metadata**: Category, type, and expectation tracking
3. **Multiple Output Formats**: JSON for tooling, TAP for CI/CD
4. **Performance Metrics**: Built-in execution time tracking
5. **Flexible Test Organization**: Hierarchical directory structure
6. **Better Error Reporting**: Detailed failure diagnostics
7. **Build System Compatibility**: Seamless integration with existing infrastructure

### 11. Implementation Phases

#### Phase 1: Foundation (Week 1-2) ✅ COMPLETED
- ✅ Set up test directory structure (`test/std/`)
- ✅ Create Custom Lambda Test Runner (`lambda_test_runner.cpp`)
- ✅ Implement JSON and TAP output formats
- ✅ Create sample test files with metadata system
- ✅ Establish naming conventions and file structure
- ✅ **Integrate with existing build system** (`build_lambda_config.json`)
- ✅ **Update test runner scripts** (`test/test_run.sh`, `test/test_build.sh`)
- ✅ **Add `lambda-std` test suite** with full compatibility

#### Phase 2: Core Coverage (Week 3-4)
- Complete all data type tests (int, float, string, bool, etc.)
- Add expression and operator tests
- Implement comprehensive negative test cases
- Add boundary condition tests
- Create expected result files for validation

#### Phase 3: Integration & Advanced (Week 5-6)
- Multi-file test scenarios
- Performance benchmarks
- Error handling tests
- Documentation and examples
- Build system integration

#### Phase 4: Automation & CI (Week 7-8)
- Integrate with existing Makefile
- Set up continuous integration workflows
- Add regression test detection
- Performance monitoring and reporting

### 12. Success Metrics

- **Coverage**: 95%+ coverage of all type/operator combinations
- **Automation**: 100% automated test execution
- **Reliability**: <1% false positive/negative rate
- **Performance**: Test suite completes in <5 minutes
- **Maintainability**: Easy addition of new tests for new features

### 13. Best Practices from Similar Projects

#### Inspiration from Language Testing Frameworks
- **Python**: Comprehensive unittest framework with fixtures and assertions
- **Rust**: Property-based testing with quickcheck-style generators
- **JavaScript**: Jest framework with snapshot testing and mocking
- **Go**: Table-driven tests with clear input/output specifications

#### Adopted Patterns
- **Table-Driven Tests**: Systematic coverage of input/output combinations
- **Property-Based Testing**: Generate test cases for edge conditions
- **Snapshot Testing**: Capture and compare complex output structures
- **Regression Testing**: Maintain tests for all discovered bugs

### 14. Conclusion

This structured testing plan transforms the current ad-hoc testing approach into a comprehensive, maintainable, and automated testing framework. The systematic coverage of all data types, operators, and expressions ensures robust validation of the Lambda scripting engine while providing clear guidelines for future test development.

The phased implementation approach allows for gradual adoption while immediately improving test coverage and reliability. The framework is designed to scale with the language's evolution and provides the foundation for confident development and deployment of Lambda script features.
