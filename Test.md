# Lambda Project Testing Guide

This document provides a comprehensive overview of the Lambda project's testing infrastructure, including unit tests, integration tests, performance benchmarks, memory leak detection, and more.

## 📋 Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Test Types](#test-types)
- [Test Architecture](#test-architecture)
- [Running Tests](#running-tests)
- [Continuous Integration](#continuous-integration)
- [Test Results](#test-results)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)

## 🔍 Overview

The Lambda project features a comprehensive, multi-layered testing infrastructure designed to ensure code quality, performance, and reliability. Our testing suite includes:

- **Unit Tests**: Comprehensive Criterion-based C unit tests
- **Integration Tests**: End-to-end workflow testing
- **Memory Tests**: AddressSanitizer and Valgrind leak detection
- **Performance Tests**: Benchmark tracking and regression detection
- **Fuzzing Tests**: Robustness testing with random inputs
- **Code Coverage**: Line and branch coverage analysis

### Key Features

✅ **Parallel Execution**: All test suites run in parallel for maximum speed  
✅ **Dynamic CPU Detection**: Automatically uses optimal CPU core count  
✅ **Comprehensive Coverage**: Tests all major components (Library, MIR JIT, Validator)  
✅ **Memory Safety**: Built-in leak detection and AddressSanitizer integration  
✅ **Performance Tracking**: Benchmark results stored for regression analysis  
✅ **CI Ready**: Optimized test targets for continuous integration  

## 🚀 Quick Start

```bash
# Install dependencies
brew install criterion jq valgrind lcov

# Run basic test suite
make test

# Run all tests (comprehensive)
make test-all

# Run CI test suite
make test-ci
```

## 🧪 Test Types

### 1. Unit Tests (`make test`)

Comprehensive Criterion-based unit tests covering all major components:

**Library Tests** (5 test suites, run in parallel):
- `test_strbuf.c` - String buffer operations and memory management
- `test_strview.c` - String view utilities and parsing
- `test_variable_pool.c` - Memory pool allocation and reallocation (18 tests)
- `test_num_stack.c` - Numeric stack operations
- `test_mime_detect.c` - MIME type detection algorithms

**MIR JIT Tests** (`test_mir.c`):
- JIT context initialization and cleanup
- C code compilation to MIR intermediate representation
- Native code generation and execution
- Function pointer management and execution
- Error handling for compilation failures

**Validator Tests** (`test_validator.c`):
- Schema parsing for all Lambda type constructs
- Validation logic for primitive, union, array, map types
- Element types, reference types, and function types
- Complex nested structures and edge cases
- CLI integration testing

### 2. Memory Tests (`make test-memory`)

**AddressSanitizer Integration**:
```bash
# Compiles all tests with -fsanitize=address
# Detects: buffer overflows, use-after-free, memory leaks
# Options: abort_on_error=1, halt_on_error=1, detect_leaks=1
```

**Valgrind Integration**:
```bash
# Full memory check with leak detection
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all
```

### 3. Performance Tests (`make test-benchmark`)

Tracks performance across multiple scenarios:

- **Full Test Suite Execution Time**
- **Individual Component Benchmarks**
- **Build Time Tracking**
- **Memory Allocation Performance**

Results stored in `test/benchmark_results.txt` with timestamps for trend analysis.

### 4. Integration Tests (`make test-integration`)

End-to-end workflow testing:

- **Build Pipeline Testing**: Clean, debug, release, cross-compilation
- **CLI Workflow Testing**: Basic execution, validation workflows
- **Component Interaction Testing**: Parser → Validator chains
- **Error Handling**: Invalid syntax, missing files, malformed schemas
- **Performance Under Load**: Large file processing, concurrent operations

### 5. Fuzzing Tests (`make test-fuzz`)

Robustness testing with random inputs:

- **Random Input Generation**: 50+ auto-generated test cases
- **Edge Case Testing**: Empty files, large files, special characters
- **Binary Data Testing**: Random binary content handling
- **Crash Detection**: Monitors for segfaults and unexpected exits
- **Timeout Handling**: 5-second timeout per test case

### 6. Code Coverage (`make test-coverage`)

Line and branch coverage analysis:

```bash
# Uses gcov and lcov for comprehensive coverage reporting
# Generates HTML reports in coverage-report/
# Tracks: line coverage, branch coverage, function coverage
```

## 🏗️ Test Architecture

### Parallel Execution Design

```
Main Test Runner (test_all.sh)
├── CPU Detection (detect_cpu_cores)
├── Criterion Discovery (find_criterion)
└── Parallel Suite Execution
    ├── Library Tests (Background Process 1)
    │   ├── test_strbuf.exe (Parallel)
    │   ├── test_strview.exe (Parallel)
    │   ├── test_variable_pool.exe (Parallel)
    │   ├── test_num_stack.exe (Parallel)
    │   └── test_mime_detect.exe (Parallel)
    ├── MIR JIT Tests (Background Process 2)
    │   └── test_mir.exe (--jobs=CPU_CORES)
    └── Validator Tests (Background Process 3)
        └── test_validator.exe (--jobs=CPU_CORES)
```

### Test Result Collection

Each test suite writes results to temporary files:
```
/tmp/suite_results/
├── library_results.txt
├── mir_results.txt
└── validator_results.txt
```

Results are collected, parsed, and aggregated for final reporting.

### Error Handling and Timeouts

- **5-minute timeout** per test suite to prevent hangs
- **Robust error collection** with fallback mechanisms
- **Process monitoring** with background timeout watchers
- **Graceful cleanup** of temporary files and processes

## 🔧 Running Tests

### Basic Commands

```bash
# Run unit tests only
make test

# Run with memory leak detection
make test-memory

# Run performance benchmarks
make test-benchmark

# Run fuzzing tests
make test-fuzz

# Run integration tests
make test-integration

# Run everything
make test-all

# CI-optimized suite
make test-ci
```

### Advanced Usage

```bash
# Run with specific CPU core count
CPU_CORES=4 ./test/test_all.sh

# Run specific test suite
cd test && ./test_strbuf.exe --verbose

# Run with TAP output for CI
cd test && ./test_validator.exe --tap

# Run memory tests with Valgrind only
cd test && ./test_memory.sh --valgrind-only

# Generate coverage report
make test-coverage && open coverage-report/index.html
```

### Test Configuration

Environment variables:
```bash
export CPU_CORES=8              # Override CPU detection
export ASAN_OPTIONS="..."       # AddressSanitizer options
export TEST_TIMEOUT=300         # Test timeout in seconds
export CRITERION_FLAGS="..."    # Additional Criterion flags
```

## 🔄 Continuous Integration

### CI Test Suite (`make test-ci`)

Optimized for CI environments:
1. **Unit Tests** - Core functionality verification
2. **Memory Tests** - Memory safety with AddressSanitizer
3. **Integration Tests** - End-to-end workflow verification

### GitHub Actions Integration

```yaml
name: Lambda Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libcriterion-dev jq valgrind lcov
      - name: Run CI tests
        run: make test-ci
      - name: Upload coverage
        uses: codecov/codecov-action@v1
        with:
          file: ./coverage.info
```

### Performance Regression Detection

Benchmark results are stored with timestamps:
```csv
timestamp,test_name,avg_time,min_time,max_time
2025-01-15 10:30:00,Full Test Suite,12.345,11.234,13.456
```

CI can compare against previous runs to detect regressions.

## 📊 Test Results

### Output Format

**Console Output**:
```
================================================
     Lambda Comprehensive Test Suite Runner    
================================================

🚀 Starting comprehensive test suite...
Detected 8 CPU cores using sysctl (macOS)
Found Criterion via Homebrew (Apple Silicon)

🚀 Starting all test suites in parallel...

================================================
        STARTING LIBRARY TESTS (PARALLEL)      
================================================
📚 Library Tests - Starting...
🔧 Compiling and running Library tests...
✅ Compiled test/test_strbuf.c successfully
✅ Starting test_strbuf.exe in parallel...
[... more output ...]

📊 Library Test Results Summary:
   Total Tests: 63
   Passed: 63
   Failed: 0

================================================
              FINAL TEST SUMMARY               
================================================
🎉 ALL TESTS PASSED!
✨ Lambda project is ready for production use!
```

**TAP Output** (for CI):
```
1..63
ok 1 - strbuf_new_creates_valid_buffer
ok 2 - strbuf_append_increases_length
ok 3 - variable_pool_init_basic_initialization
[... more tests ...]
```

### Coverage Reports

HTML coverage reports include:
- **Line Coverage**: Percentage of code lines executed
- **Branch Coverage**: Percentage of code branches taken
- **Function Coverage**: Percentage of functions called
- **File-by-file breakdown** with color-coded visualization

### Benchmark Reports

Performance tracking in `test/benchmark_results.txt`:
```
2025-01-15 10:30:00,Full Test Suite,12.345,11.234,13.456
2025-01-15 10:35:00,Library Tests Only,2.123,1.987,2.456
2025-01-15 10:40:00,MIR JIT Tests,5.678,5.234,6.123
```

## 🐛 Troubleshooting

### Common Issues

**1. Criterion Not Found**
```bash
Error: Criterion testing framework not found!
Solution: brew install criterion
```

**2. Tests Hanging**
```bash
Issue: Test execution appears stuck
Solution: Check for infinite loops, increase timeout, or run with --verbose
Debug: ps aux | grep test  # Check for hanging processes
```

**3. Memory Test Failures**
```bash
Issue: AddressSanitizer reports memory errors
Solution: Review the specific error output, check for:
- Buffer overflows
- Use-after-free
- Memory leaks
- Double-free errors
```

**4. Performance Regression**
```bash
Issue: Benchmark tests show performance degradation
Solution: 
1. Compare with previous benchmark results
2. Profile the code to identify bottlenecks
3. Check for algorithmic changes
4. Verify system load during testing
```

### Debug Mode

Run tests with debug output:
```bash
# Verbose output
./test/test_all.sh --verbose

# Debug specific test
cd test && ./test_strbuf.exe --debug

# Memory debugging
ASAN_OPTIONS="abort_on_error=0" ./test/test_memory.sh
```

### Log Files

Generated log files:
- `valgrind_*.log` - Valgrind memory analysis
- `test/benchmark_results.txt` - Performance tracking
- `coverage.info` - Coverage data
- `test/fuzz_inputs/` - Generated fuzz test cases

## 🤝 Contributing

### Adding New Tests

**1. Unit Tests**:
```c
// In appropriate test_*.c file
Test(test_suite_name, test_case_name) {
    // Setup
    // Execute
    // Assert with cr_assert_*
}
```

**2. Integration Tests**:
```bash
# Add to test/test_integration.sh
test_new_feature() {
    print_status "Testing new feature..."
    # Test implementation
    return 0  # or 1 for failure
}
```

**3. Memory Tests**:
- Tests are automatically included if they follow naming convention
- Add compilation dependencies to `test_memory.sh`

### Test Guidelines

- **Use descriptive test names** that explain what is being tested
- **Follow AAA pattern**: Arrange, Act, Assert
- **Test edge cases** and error conditions
- **Keep tests independent** - no shared state between tests
- **Add documentation** for complex test scenarios

### Debugging Tests

```bash
# Run single test with full output
cd test && ./test_strbuf.exe --verbose --no-early-exit

# Debug memory issues
cd test && gdb ./test_strbuf.exe

# Check for compilation issues
cd test && gcc -Wall -Wextra -g test_strbuf.c ../lib/strbuf.c -lcriterion
```

## 📈 Performance Metrics

### Current Benchmarks (8-core macOS)

| Test Suite | Average Time | Tests Count | Parallel |
|------------|--------------|-------------|----------|
| Library Tests | ~2.5s | 63 tests | ✅ |
| MIR JIT Tests | ~3.2s | 8 tests | ✅ |
| Validator Tests | ~4.1s | 22 tests | ✅ |
| **Total Suite** | **~10s** | **93 tests** | ✅ |

### Memory Usage

- **Peak Memory**: ~150MB during parallel execution
- **Memory Leaks**: 0 detected (AddressSanitizer + Valgrind)
- **Memory Pool Efficiency**: 95%+ allocation success rate

### Coverage Metrics

- **Line Coverage**: 85%+ target
- **Branch Coverage**: 80%+ target
- **Function Coverage**: 90%+ target

---

## 🎯 Summary

The Lambda project testing infrastructure provides comprehensive quality assurance through:

- **93 unit tests** across all major components
- **Parallel execution** utilizing all available CPU cores
- **Memory safety verification** with AddressSanitizer and Valgrind
- **Performance tracking** to detect regressions
- **Fuzzing tests** for robustness validation
- **Integration tests** for end-to-end workflow verification
- **Code coverage analysis** for completeness assessment

This multi-layered approach ensures the Lambda project maintains high quality, performance, and reliability standards throughout development.

For questions or issues, please refer to the troubleshooting section or open an issue in the project repository.
