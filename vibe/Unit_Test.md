# Lambda Unit Test Framework Implementation Plan

## Overview

This document outlines the plan to implement a lightweight, Criterion-compatible unit test framework for the Lambda project. The framework will be implemented under `./lib/unit_test` and provide full API compatibility with existing Criterion-based tests while minimizing external dependencies.

## Current State Analysis

### Criterion Usage in Lambda

The Lambda project currently has **36 test files** with extensive Criterion usage:
- **1,500+ test assertions** across all test files
- Primary macros: `cr_assert`, `cr_expect`, `cr_assert_eq`, `cr_assert_neq`, `cr_expect_eq`, `cr_expect_gt`, etc.
- Test organization: `Test()` and `TestSuite()` macros with setup/teardown functions
- Advanced features: Test filtering (`--filter`), floating-point comparisons, string comparisons
- Memory integration: Tests use Lambda's custom memory pool system

### Key API Requirements

Based on analysis of existing test files, the framework must support:

1. **Test Definition Macros:**
   ```c
   Test(suite_name, test_name, .init = setup, .fini = teardown)
   TestSuite(suite_name, .init = setup, .fini = teardown)
   ```

2. **Assertion Macros:**
   ```c
   cr_assert(condition, message, ...)
   cr_assert_eq(actual, expected, message, ...)
   cr_assert_neq(actual, expected, message, ...)
   cr_assert_gt(actual, expected, message, ...)
   cr_assert_not_null(ptr, message, ...)
   cr_expect_* (non-fatal versions)
   cr_expect_float_eq(actual, expected, epsilon, message, ...)
   ```

3. **Setup/Teardown Functions:**
   ```c
   void setup(void);
   void teardown(void);
   ```

4. **Command Line Interface:**
   ```bash
   ./test_executable --filter="*test_name"
   ```

## Architecture Design

### Directory Structure
```
./lib/unit_test/
├── include/
│   ├── criterion/
│   │   ├── criterion.h          # Main header (Criterion compatibility)
│   │   └── new/
│   │       └── assert.h         # New-style assertions
│   ├── unit_test.h              # Core framework header
│   ├── test_registry.h          # Test registration system
│   ├── test_runner.h            # Test execution engine
│   └── assertions.h             # Assertion implementations
├── src/
│   ├── unit_test.c              # Core framework implementation
│   ├── test_registry.c          # Test registration and discovery
│   ├── test_runner.c            # Test execution and reporting
│   ├── assertions.c             # Assertion implementations
│   └── criterion_compat.c       # Criterion compatibility layer
└── README.md                    # Framework documentation
```

### Core Components

#### 1. Test Registry System (`test_registry.h/c`)

**Purpose:** Automatic test discovery and registration using constructor attributes.

**Key Structures:**
```c
typedef struct {
    const char* suite_name;
    const char* test_name;
    void (*test_func)(void);
    void (*setup_func)(void);
    void (*teardown_func)(void);
    bool enabled;
} TestCase;

typedef struct {
    TestCase* tests;
    size_t count;
    size_t capacity;
} TestRegistry;
```

**Key Functions:**
```c
void test_registry_init(void);
void test_registry_register(const TestCase* test);
TestCase* test_registry_find_tests(const char* filter, size_t* count);
void test_registry_cleanup(void);
```

#### 2. Test Runner Engine (`test_runner.h/c`)

**Purpose:** Execute tests, handle failures, generate reports.

**Key Structures:**
```c
typedef enum {
    TEST_RESULT_PASS,
    TEST_RESULT_FAIL,
    TEST_RESULT_SKIP
} TestResult;

typedef struct {
    TestResult result;
    const char* message;
    const char* file;
    int line;
    double execution_time;
} TestReport;

typedef struct {
    int total_tests;
    int passed_tests;
    int failed_tests;
    int skipped_tests;
    double total_time;
    TestReport* reports;
} TestSummary;
```

**Key Functions:**
```c
TestSummary* test_runner_execute(TestCase* tests, size_t count);
void test_runner_print_summary(const TestSummary* summary);
void test_runner_cleanup(TestSummary* summary);
```

#### 3. Assertion System (`assertions.h/c`)

**Purpose:** Implement all Criterion assertion macros with detailed failure reporting.

**Key Macros:**
```c
#define cr_assert(condition, ...) \
    do { \
        if (!(condition)) { \
            _test_fail(__FILE__, __LINE__, __VA_ARGS__); \
            return; \
        } \
    } while(0)

#define cr_expect(condition, ...) \
    do { \
        if (!(condition)) { \
            _test_expect_fail(__FILE__, __LINE__, __VA_ARGS__); \
        } \
    } while(0)

#define cr_assert_eq(actual, expected, ...) \
    cr_assert((actual) == (expected), __VA_ARGS__)
```

**Key Functions:**
```c
void _test_fail(const char* file, int line, const char* format, ...);
void _test_expect_fail(const char* file, int line, const char* format, ...);
bool _test_float_eq(double a, double b, double epsilon);
```

#### 4. Criterion Compatibility Layer (`criterion_compat.c`)

**Purpose:** Provide drop-in replacement for Criterion headers.

**Features:**
- Header compatibility: `#include <criterion/criterion.h>`
- Macro compatibility: All existing `cr_*` macros work unchanged
- Setup/teardown compatibility: `.init` and `.fini` attributes
- Command-line compatibility: `--filter` argument support

## Implementation Phases

### Phase 1: Core Framework (Week 1-2)
**Deliverables:**
- Basic test registry system
- Simple test runner with pass/fail reporting
- Core assertion macros (`cr_assert`, `cr_expect`)
- Basic `Test()` macro implementation

**Success Criteria:**
- Can run simple tests with basic assertions
- Test registration works automatically
- Basic pass/fail reporting functional

### Phase 2: Assertion System (Week 3)
**Deliverables:**
- Complete assertion macro set
- Floating-point comparison support
- String comparison assertions
- Null pointer checks
- Detailed failure messages with file/line info

**Success Criteria:**
- All `cr_assert_*` and `cr_expect_*` macros implemented
- Assertion failures provide clear diagnostic information
- Floating-point comparisons work with epsilon tolerance

### Phase 3: Advanced Features (Week 4)
**Deliverables:**
- `TestSuite()` macro support
- Setup/teardown function integration
- Test filtering by name pattern
- Command-line argument parsing
- Performance timing for tests

**Success Criteria:**
- Test suites can have shared setup/teardown
- `--filter="*pattern"` command-line filtering works
- Test execution times are measured and reported

### Phase 4: Criterion Compatibility (Week 5)
**Deliverables:**
- Complete Criterion header compatibility
- Drop-in replacement for `<criterion/criterion.h>`
- All existing Lambda tests compile without changes
- Feature parity with used Criterion functionality

**Success Criteria:**
- All 36 existing test files compile without modification
- All tests pass with identical behavior to Criterion
- No external dependencies beyond standard C library

### Phase 5: Integration & Testing (Week 6)
**Deliverables:**
- Integration with Lambda build system
- Comprehensive test suite for the framework itself
- Performance benchmarking vs Criterion
- Documentation and migration guide

**Success Criteria:**
- Framework passes its own comprehensive test suite
- Performance is comparable to or better than Criterion
- All Lambda tests pass with new framework
- Build system integration complete

## Technical Specifications

### Memory Management
- **Zero external dependencies** - only standard C library
- **Integration with Lambda's memory pools** - support for custom allocators
- **Automatic cleanup** - proper resource management for test reports
- **Minimal memory footprint** - efficient test registration and execution

### Error Handling
- **Graceful failure handling** - tests continue after assertion failures
- **Detailed error reporting** - file, line, and custom message support
- **Stack trace support** - optional backtrace on assertion failures (if available)
- **Signal handling** - catch segfaults and other signals during test execution

### Performance Requirements
- **Fast test discovery** - sub-millisecond test registration
- **Efficient execution** - minimal overhead per test
- **Parallel execution support** - foundation for future parallel test running
- **Memory efficiency** - minimal memory usage during test execution

### Compatibility Requirements
- **C99 standard compliance** - works with Lambda's existing C codebase
- **GCC/Clang compatibility** - works with Lambda's build toolchain
- **Cross-platform support** - macOS, Linux, Windows (future)
- **No external dependencies** - self-contained implementation

## API Specification

### Test Definition
```c
// Basic test definition
Test(suite_name, test_name) {
    cr_assert_eq(1 + 1, 2, "Basic math should work");
}

// Test with setup/teardown
Test(suite_name, test_name, .init = setup, .fini = teardown) {
    cr_expect_not_null(global_resource, "Resource should be initialized");
}

// Test suite definition
TestSuite(suite_name, .init = suite_setup, .fini = suite_teardown);
```

### Assertion Macros
```c
// Basic assertions
cr_assert(condition, message, ...);
cr_expect(condition, message, ...);

// Equality assertions
cr_assert_eq(actual, expected, message, ...);
cr_assert_neq(actual, expected, message, ...);
cr_expect_eq(actual, expected, message, ...);

// Comparison assertions
cr_assert_gt(actual, expected, message, ...);
cr_assert_lt(actual, expected, message, ...);
cr_assert_geq(actual, expected, message, ...);
cr_assert_leq(actual, expected, message, ...);

// Pointer assertions
cr_assert_null(ptr, message, ...);
cr_assert_not_null(ptr, message, ...);

// Floating-point assertions
cr_expect_float_eq(actual, expected, epsilon, message, ...);

// String assertions
cr_assert_str_eq(actual, expected, message, ...);
cr_assert_str_neq(actual, expected, message, ...);
```

### Command Line Interface
```bash
# Run all tests
./test_executable

# Run tests matching pattern
./test_executable --filter="*math*"

# Run specific test
./test_executable --filter="*test_addition"

# Verbose output
./test_executable --verbose

# List available tests
./test_executable --list-tests
```

## Build System Integration

### Makefile Integration
```makefile
# Add unit test library to build
UNIT_TEST_DIR = lib/unit_test
UNIT_TEST_SOURCES = $(wildcard $(UNIT_TEST_DIR)/src/*.c)
UNIT_TEST_OBJECTS = $(UNIT_TEST_SOURCES:.c=.o)

# Include unit test headers
CFLAGS += -I$(UNIT_TEST_DIR)/include

# Build unit test library
$(UNIT_TEST_DIR)/libunit_test.a: $(UNIT_TEST_OBJECTS)
	ar rcs $@ $^

# Link tests with unit test library
test_%: test/test_%.c $(UNIT_TEST_DIR)/libunit_test.a
	$(CC) $(CFLAGS) -o $@ $< -L$(UNIT_TEST_DIR) -lunit_test
```

### Premake Integration
```lua
-- Add unit test library project
project "unit_test"
    kind "StaticLib"
    language "C"
    files { "lib/unit_test/src/*.c" }
    includedirs { "lib/unit_test/include" }

-- Update test projects to use unit test library
filter "configurations:Test"
    links { "unit_test" }
    includedirs { "lib/unit_test/include" }
```

## Migration Strategy

### Step 1: Parallel Implementation
- Implement unit test framework alongside existing Criterion usage
- Create compatibility headers that can switch between implementations
- Test framework with subset of existing tests

### Step 2: Gradual Migration
- Convert test files one by one to use new framework
- Maintain both implementations during transition period
- Validate identical behavior between frameworks

### Step 3: Complete Replacement
- Remove Criterion dependency from build system
- Update all test files to use new framework
- Remove compatibility shims and old headers

### Compatibility Shim Example
```c
// criterion_shim.h - temporary compatibility layer
#ifdef USE_LAMBDA_UNIT_TEST
    #include "unit_test/criterion/criterion.h"
#else
    #include <criterion/criterion.h>
#endif
```

## Testing Strategy

### Framework Self-Testing
The unit test framework will include comprehensive tests for itself:

1. **Test Registry Tests:**
   - Test registration and discovery
   - Test filtering and pattern matching
   - Memory management during registration

2. **Assertion Tests:**
   - All assertion macros with various inputs
   - Failure message generation
   - Floating-point comparison edge cases

3. **Test Runner Tests:**
   - Test execution and result collection
   - Setup/teardown function execution
   - Error handling and recovery

4. **Integration Tests:**
   - End-to-end test execution
   - Command-line argument processing
   - Report generation and formatting

### Validation Against Existing Tests
- Run all 36 existing Lambda test files with new framework
- Compare results with Criterion-based execution
- Validate identical pass/fail behavior
- Performance benchmarking against Criterion

## Success Metrics

### Functional Requirements
- ✅ **100% API compatibility** - All existing tests compile without changes
- ✅ **Identical behavior** - Same pass/fail results as Criterion
- ✅ **Complete feature coverage** - All used Criterion features implemented
- ✅ **Zero external dependencies** - Self-contained implementation

### Performance Requirements
- ✅ **Test execution overhead < 1ms per test** - Minimal performance impact
- ✅ **Memory usage < 1MB total** - Efficient memory utilization
- ✅ **Startup time < 10ms** - Fast test discovery and initialization
- ✅ **Comparable to Criterion performance** - No significant slowdown

### Quality Requirements
- ✅ **Framework test coverage > 95%** - Comprehensive self-testing
- ✅ **Zero memory leaks** - Proper resource management
- ✅ **Clean compilation** - No warnings with strict compiler flags
- ✅ **Cross-platform compatibility** - Works on Lambda's target platforms

## Risk Assessment

### Technical Risks
1. **Macro complexity** - Criterion's macro system is sophisticated
   - **Mitigation:** Implement incrementally, test thoroughly
2. **Setup/teardown timing** - Complex execution order requirements
   - **Mitigation:** Study Criterion behavior, implement carefully
3. **Floating-point comparisons** - Edge cases in epsilon comparisons
   - **Mitigation:** Use proven algorithms, extensive testing

### Integration Risks
1. **Build system complexity** - Integration with existing build system
   - **Mitigation:** Gradual integration, maintain parallel builds
2. **Test behavior differences** - Subtle differences from Criterion
   - **Mitigation:** Extensive validation, side-by-side testing
3. **Memory pool integration** - Lambda's custom memory management
   - **Mitigation:** Design for pluggable allocators from start

### Timeline Risks
1. **Underestimated complexity** - Framework may be more complex than anticipated
   - **Mitigation:** Conservative estimates, incremental delivery
2. **Integration challenges** - Unexpected issues with existing tests
   - **Mitigation:** Early integration testing, parallel development

## Future Enhancements

### Phase 6+: Advanced Features
- **Parallel test execution** - Run tests concurrently for speed
- **Test fixtures** - Shared test data and resources
- **Parameterized tests** - Data-driven test execution
- **Benchmark support** - Performance testing capabilities
- **Coverage integration** - Code coverage reporting
- **XML/JSON output** - Machine-readable test reports
- **Test discovery** - Automatic test file discovery
- **Mock framework** - Function mocking and stubbing support

### Integration Opportunities
- **Lambda Script integration** - Run Lambda scripts as tests
- **Math expression testing** - Specialized assertions for mathematical expressions
- **Memory pool testing** - Assertions for memory management
- **Performance regression testing** - Automated performance validation

## Conclusion

This plan provides a comprehensive roadmap for implementing a Criterion-compatible unit test framework for the Lambda project. The framework will eliminate external dependencies while maintaining full compatibility with existing tests. The phased approach ensures incremental progress and validation, minimizing risk while delivering a robust, efficient testing solution.

The implementation will leverage Lambda's existing infrastructure (memory pools, build system) while providing a clean, maintainable codebase that can evolve with the project's testing needs. The result will be a lightweight, fast, and fully-featured unit testing framework tailored specifically for the Lambda project's requirements.
