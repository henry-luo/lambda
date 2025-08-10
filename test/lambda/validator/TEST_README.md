# Lambda Schema Validator Test Suite

This directory contains the comprehensive test suite for the Lambda Schema Validator, including enhanced error recovery, detailed reporting, and full schema validation support.

## 📁 Directory Structure

- **Scripts**: Demo and test execution scripts
  - `demo.sh` - Interactive demonstration of validator features
  - `quick_test.sh` - Quick validation test
  - `run_tests.sh` - Comprehensive test suite runner

- **Schema Files**: Test schemas for various validation scenarios
  - `simple_schemas.ls` - Basic validation schemas
  - `strict_schemas.ls` - Strict validation with required fields
  - `very_strict.ls` - Most restrictive validation
  - `complex_schemas.ls` - Complex nested structures

- **Test Data**: JSON and other test data files
  - `test_data_valid.json` - Valid test data
  - `test_data_invalid.json` - Invalid test data for error testing
  - `test_data_strings.json` - String-only test data

- **Build System**: Makefile and build configuration
  - `Makefile` - Test suite compilation
  - `test_validator_basic.c` - Basic validator tests
  - `test_validator_advanced.c` - Advanced validator tests

## 🚀 Quick Start

### Run Demo
```bash
cd test/lambda/validator
./demo.sh
```

### Run Quick Test
```bash
cd test/lambda/validator
./quick_test.sh
```

### Run Full Test Suite
```bash
cd test/lambda/validator
./run_tests.sh
```

## ✅ Features Tested

- **Error Recovery**: Validates all elements, continues after errors
- **Enhanced Error Reporting**: Detailed messages with path tracking
- **Schema Validation**: Full support for all Lambda schema types
- **Multiple Input Formats**: JSON, XML, HTML, Markdown, YAML, etc.
- **Memory Safety**: Proper cleanup and resource management

## 📊 Test Coverage

The test suite includes:
- ✅ Primitive type validation
- ✅ Map and array validation
- ✅ Nested structure validation
- ✅ Union type validation
- ✅ Error recovery and reporting
- ✅ CLI integration testing
- ✅ Memory leak detection

For detailed implementation notes, see `ENHANCEMENT_SUMMARY.md`.
