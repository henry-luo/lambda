# Lambda Validator Test Suite

This directory contains comprehensive tests for the Lambda validator implementation as described in `lambda/validator/validator.md`.

## Test Structure

The test suite is organized into type-specific test pairs, with each pair consisting of:
- **Test Data File** (`test_*.ls`) - Lambda script containing data to validate
- **Schema File** (`schema_*.ls`) - Lambda script defining the validation schema

## Type Definition Categories

### 1. Primitive Types (`test_primitive.ls` + `schema_primitive.ls`)
Tests all basic Lambda primitive types:
- `int`, `float`, `string`, `bool`, `null`
- `char`, `symbol`, `datetime`, `decimal`, `binary`

### 2. Union Types (`test_union.ls` + `schema_union.ls`)
Tests binary type expressions with `|` operator:
- `string | int`
- `int | bool`
- `string | null` (nullable types)
- Multi-type unions: `int | float | string`

### 3. Occurrence Types (`test_occurrence.ls` + `schema_occurrence.ls`)
Tests occurrence modifiers:
- `?` (optional, zero or one)
- `+` (one or more)
- `*` (zero or more)

### 4. Array Types (`test_array.ls` + `schema_array.ls`)
Tests array type syntax:
- `[int*]` (array of integers)
- `[string*]` (array of strings)
- `[[int*]*]` (nested arrays)
- `[int | string | bool | null*]` (mixed type arrays)

### 5. Map Types (`test_map.ls` + `schema_map.ls`)
Tests object/map structures:
- Simple field definitions
- Nested object structures
- Type references in maps
- Optional fields in maps

### 6. Element Types (`test_element.ls` + `schema_element.ls`)
Tests HTML-like element syntax:
- `<header level: int, text: string>`
- `<p class: string?, content: string>`
- `<a href: string, target: string?, text: string>`
- `<img src: string, alt: string, width: int?, height: int?>`

### 7. Reference Types (`test_reference.ls` + `schema_reference.ls`)
Tests forward references and type relationships:
- Type definitions referencing other types
- Complex nested type hierarchies
- Circular reference handling

### 8. Function Types (`test_function.ls` + `schema_function.ls`)
Tests function type syntax:
- `(int, int) => int`
- `(string, any) => string`
- `(any) => void`

### 9. Complex Types (`test_complex.ls` + `schema_complex.ls`)
Tests combinations of all type features:
- Deeply nested structures
- Multiple type modifiers
- Complex type relationships
- Real-world-like document structures

### 10. Edge Cases (`test_edge_cases.ls` + `schema_edge_cases.ls`)
Tests boundary conditions:
- Empty strings and arrays
- Zero and negative numbers
- Unicode content
- Special characters
- Null values in various contexts

### 11. Invalid Cases (`test_invalid.ls` + `schema_invalid.ls`)
Tests error conditions (should fail validation):
- Type mismatches
- Missing required fields
- Invalid array types
- Schema violations

## Running Tests

### Quick Test Run
```bash
./test/test_validator.sh
```

### Manual CLI Testing
```bash
# Test primitive types
./lambda.exe validate test/lambda/validator/test_primitive.ls -s test/lambda/validator/schema_primitive.ls

# Test union types
./lambda.exe validate test/lambda/validator/test_union.ls -s test/lambda/validator/schema_union.ls

# Test complex nested structures
./lambda.exe validate test/lambda/validator/test_complex.ls -s test/lambda/validator/schema_complex.ls
```

### Individual Schema Testing
```bash
# Test schema parsing only
./lambda.exe validate --schema-only test/lambda/validator/schema_primitive.ls
```

## Test Implementation

### Test Runner (`test/test_validator.c`)
- **Schema Parsing Tests**: Verify schemas parse correctly using Tree-sitter grammar
- **Data Validation Tests**: Validate test data against schemas
- **Memory Management**: Uses Lambda's VariableMemPool API correctly
- **Error Handling**: Tests both success and failure cases
- **Integration Testing**: Tests full CLI integration

### Shell Script (`test/test_validator.sh`)
- **Build Verification**: Ensures lambda.exe is built and ready
- **File Checking**: Verifies all test files are present
- **CLI Testing**: Tests actual CLI validation commands
- **Result Reporting**: Comprehensive pass/fail reporting
- **Error Recovery**: Fallback to CLI-only testing if compilation fails

## Coverage

This test suite covers:
- ✅ **All Tree-sitter Symbols**: Tests all 50+ symbols from `ts-enum.h`
- ✅ **All Field IDs**: Tests all 19 field IDs for AST navigation
- ✅ **All Type Constructs**: Primitive, complex, and advanced types
- ✅ **Memory Pool Integration**: Tests VariableMemPool API usage
- ✅ **CLI Integration**: Tests `lambda validate` subcommand
- ✅ **Error Handling**: Tests both valid and invalid cases
- ✅ **Real-world Scenarios**: Complex document-like structures

## Expected Results

When the validator implementation matches the specification in `validator.md`:
- **Schema Parsing**: All schema files should parse successfully
- **Type Recognition**: All type definitions should be recognized correctly
- **Validation Logic**: Valid data should pass, invalid data should fail
- **Memory Management**: No memory leaks or allocation errors
- **CLI Integration**: Seamless integration with lambda.exe

## Integration with validator.md

This test suite directly validates the implementation described in:
- **Section 2.2**: Enhanced Lambda Grammar Integration
- **Section 3**: CLI Integration and Memory Pool usage
- **Section 4**: Build and Test procedures
- **Section 5**: Complete integration status

The tests ensure that the "✅ Complete and Integrated" status in validator.md is accurate and verifiable.
