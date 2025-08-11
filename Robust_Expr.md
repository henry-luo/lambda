# Lambda Script Robust Expression Implementation Plan

## Executive Summary

This document outlines a comprehensive plan to improve the robustness and completeness of let/if/for expression and statement support in the Lambda Script transpiler and JIT compiler. The current implementation has partial support with various gaps in type handling, error management, and edge cases.

## Current State Analysis

### Architecture Overview

Lambda Script uses a sophisticated compilation pipeline:
1. **Tree-sitter Parsing** → **AST Building** → **Transpilation to C** → **MIR JIT Compilation**
2. **Memory Management**: Reference counting + variable memory pools
3. **Type System**: Strong typing with 20+ built-in types (LMD_TYPE_*)

### Current Implementation Status

#### ✅ What Works
- Basic let expressions and statements
- Simple if expressions with conditional evaluation  
- For expressions with range and array iteration
- Type inference for expressions
- Basic transpilation to C code
- Memory allocation via pools

#### ⚠️  Partially Working
- Nested let expressions (some scoping issues)
- If expressions with complex type unions
- For expressions with nested containers
- Error propagation and handling
- Type coercion in expressions

#### ❌ Missing/Broken
- Comprehensive error handling for all edge cases
- Robust type validation and conversion
- Proper null handling in all contexts
- Memory leak prevention in error paths
- Consistent behavior across all lambda types
- Edge case testing and validation

### Key Files Analysis

#### Core AST & Transpilation
- `lambda/build_ast.cpp`: AST node construction (`build_let_expr`, `build_if_expr`, `build_for_expr`)
- `lambda/transpile.cpp`: C code generation (`transpile_let_stam`, `transpile_if_expr`, `transpile_for_expr`)
- `lambda/transpiler.hpp`: AST node type definitions

#### Runtime Support  
- `lambda/lambda-eval.cpp`: Runtime functions (`map_get`, `array_get`, `list_get`, etc.)
- `lambda/lambda.h`: Core type definitions and Item structure
- `lambda/lambda-mem.cpp`: Memory management

#### Testing Infrastructure
- `test/lambda/`: Lambda script tests (`.ls` files)
- Current tests: `expr.ls`, `simple_expr.ls`, `func.ls`, `value.ls`

## Improvement Plan

### Phase 1: Foundation & Analysis (Week 1-2)

#### 1.1 Comprehensive Code Audit
- **Goal**: Map all current expression handling code paths
- **Tasks**:
  - Document all AST node types and their transpilation
  - Identify all error paths and return values
  - Map type conversion functions and their coverage
  - Document memory allocation patterns

#### 1.2 Current Test Analysis
- **Goal**: Understand existing test coverage
- **Tasks**:
  - Analyze all `.ls` test files for expression patterns
  - Document expected vs actual behavior
  - Identify untested edge cases
  - Create test coverage matrix

#### 1.3 Error Handling Audit
- **Goal**: Document current error handling patterns
- **Tasks**:
  - Map all `ItemNull`, `ItemError`, and `NULL` return paths
  - Document error propagation mechanisms
  - Identify silent failure cases
  - Document memory cleanup in error paths

### Phase 2: Let Expression Robustness (Week 2-3)

#### 2.1 Let Expression Core Issues
**Current Problems Identified**:
- Scoping issues in nested let expressions
- Incomplete type inference in complex cases
- Memory leaks in declaration chains
- Missing error handling for invalid assignments

**Fixes Required**:
- Proper name scope management in `build_let_expr`/`build_let_stam`
- Enhanced type inference for let expressions
- Memory cleanup in error paths
- Variable shadowing handling

#### 2.2 Let Expression Test Suite
**Test Categories**:
- **Basic Tests**: Simple variable declarations
- **Scoping Tests**: Nested lets, variable shadowing
- **Type Tests**: All lambda types as let values
- **Error Tests**: Invalid assignments, type mismatches
- **Memory Tests**: Large declaration chains, cleanup verification

### Phase 3: If Expression Robustness (Week 3-4)

#### 3.1 If Expression Core Issues  
**Current Problems Identified**:
- Type union calculation issues in `build_if_expr`
- Incomplete handling of null/error conditions
- Missing type coercion in branches
- Boolean evaluation edge cases

**Fixes Required**:
- Enhanced type union logic for if expressions
- Proper null/error propagation
- Type coercion between then/else branches
- Robust boolean evaluation (`item_true` function improvements)

#### 3.2 If Expression Test Suite
**Test Categories**:
- **Basic Tests**: Simple conditions with all lambda types
- **Type Union Tests**: Different types in then/else branches
- **Null Handling Tests**: Null conditions, null branches
- **Error Tests**: Error conditions, error propagation  
- **Nested Tests**: Deeply nested if expressions

### Phase 4: For Expression Robustness (Week 4-5)

#### 4.1 For Expression Core Issues
**Current Problems Identified**:
- Limited container type support in `transpile_loop_expr`
- Incomplete nested loop handling
- Type inference issues in loop variables
- Memory management in loop execution

**Fixes Required**:
- Support for all iterable types (List, Array, Map, Element, Range)
- Enhanced nested loop transpilation
- Proper type inference for loop variables
- Memory pool management in loops

#### 4.2 For Expression Test Suite  
**Test Categories**:
- **Container Tests**: For loops over all lambda types
- **Nested Loop Tests**: Multiple nested for expressions
- **Type Tests**: Type preservation in loop variables
- **Performance Tests**: Large container iteration
- **Error Tests**: Invalid iterables, error propagation

### Phase 5: Cross-Type Support (Week 5-6)

#### 5.1 Universal Type Support
**Goal**: Ensure let/if/for work with all 20+ lambda types

**Type Categories to Test**:
```
Scalars: null, bool, int, int64, float, decimal, number, string, symbol, binary, datetime
Containers: list, array, array_int, map, element, range  
Meta: type, function, any, error
```

#### 5.2 Type Conversion & Coercion
- Implement robust `transpile_box_item` improvements
- Add type conversion functions for all combinations
- Handle type coercion in expressions
- Proper error handling for invalid conversions

### Phase 6: Error Handling & Edge Cases (Week 6-7)

#### 6.1 Comprehensive Error Handling
- **Runtime Errors**: Proper error item creation and propagation
- **Compile-time Errors**: Better error reporting in transpilation
- **Memory Errors**: Cleanup in all error paths
- **Type Errors**: Clear error messages for type mismatches

#### 6.2 Edge Case Coverage
- **Boundary Cases**: Empty containers, null values, large numbers
- **Resource Cases**: Memory exhaustion, deep recursion
- **Invalid Cases**: Malformed expressions, type violations
- **Performance Cases**: Very large expressions, deeply nested structures

### Phase 7: Testing Infrastructure (Week 7-8)

#### 7.1 Comprehensive Test Suite Structure

```
test/lambda/expressions/
├── let/
│   ├── basic_let_test.ls           # Simple let expressions
│   ├── nested_let_test.ls          # Nested let expressions  
│   ├── let_scoping_test.ls         # Variable scoping
│   ├── let_types_test.ls           # All types as let values
│   ├── let_errors_test.ls          # Error cases
│   └── let_memory_test.ls          # Memory management
├── if/
│   ├── basic_if_test.ls            # Simple if expressions
│   ├── if_types_test.ls            # All types in conditions
│   ├── if_union_test.ls            # Type unions in branches
│   ├── if_nested_test.ls           # Nested if expressions
│   ├── if_errors_test.ls           # Error cases
│   └── if_null_test.ls             # Null handling
├── for/
│   ├── basic_for_test.ls           # Simple for expressions
│   ├── for_containers_test.ls      # All container types
│   ├── for_nested_test.ls          # Nested for loops
│   ├── for_types_test.ls           # Type preservation
│   ├── for_errors_test.ls          # Error cases
│   └── for_performance_test.ls     # Performance tests
├── integration/
│   ├── expr_combinations_test.ls   # Let+if+for combinations
│   ├── complex_nesting_test.ls     # Deeply nested expressions
│   ├── type_interactions_test.ls   # Type system interactions
│   └── memory_stress_test.ls       # Memory stress testing
└── negative/
    ├── invalid_syntax_test.ls      # Invalid expression syntax
    ├── type_mismatch_test.ls       # Type constraint violations
    ├── resource_limits_test.ls     # Resource exhaustion
    └── edge_cases_test.ls          # Various edge cases
```

#### 7.2 Test Framework Enhancements
- **Test Runner**: Automated test execution and reporting
- **Assertion Library**: Rich assertion functions for lambda types  
- **Memory Checking**: Memory leak detection in tests
- **Performance Benchmarks**: Performance regression detection

#### 7.3 Test Patterns and Conventions

**Positive Test Pattern**:
```lambda
// test/lambda/expressions/let/basic_let_test.ls
pub fn test_basic_let_integers() {
    let a = 42
    let b = a + 8
    assert_eq(b, 50, "Simple integer let expression")
    true
}

pub fn test_basic_let_strings() {
    let greeting = "Hello"
    let full = greeting + " World" 
    assert_eq(full, "Hello World", "Simple string let expression")
    true
}
```

**Negative Test Pattern**:
```lambda  
// test/lambda/expressions/negative/type_mismatch_test.ls
pub fn test_invalid_let_assignment() {
    // This should produce a compile-time or runtime error
    let x:int = "not a number"
    assert_error(x, "Type mismatch in let assignment")
    true
}
```

**Memory Test Pattern**:
```lambda
// test/lambda/expressions/let/let_memory_test.ls  
pub fn test_let_memory_cleanup() {
    let large_data = []
    for i in 1 to 10000 {
        large_data = large_data + [i]
    }
    let result = len(large_data)
    // Memory should be cleaned up properly
    assert_eq(result, 10000, "Large let assignment")
    true
}
```

### Phase 8: Implementation Execution (Week 8-12)

#### 8.1 Sequential Implementation Strategy
1. **Start with Let Expressions** (most foundational)
2. **Move to If Expressions** (builds on let)  
3. **Finish with For Expressions** (most complex)
4. **Implement comprehensive testing throughout**

#### 8.2 Code Quality Standards
- **Error Handling**: Every function must handle error cases
- **Memory Management**: All allocations must have cleanup paths
- **Type Safety**: All type conversions must be explicit and safe
- **Documentation**: All functions must be documented
- **Testing**: All changes must include tests

#### 8.3 Implementation Checkpoints
- **Daily**: Code review and testing of changes
- **Weekly**: Integration testing and performance benchmarks
- **Phase End**: Complete test suite run and documentation update

## Success Criteria

### Functional Requirements
- ✅ All let/if/for expressions work correctly with all 20+ lambda types
- ✅ Proper error handling and reporting in all cases
- ✅ Memory safety with no leaks or corruption
- ✅ Type safety with proper coercion and validation
- ✅ Performance meets or exceeds current implementation

### Testing Requirements  
- ✅ >95% test coverage for expression handling code
- ✅ Comprehensive positive and negative test cases
- ✅ Performance regression tests
- ✅ Memory leak testing
- ✅ Integration testing with full lambda runtime

### Quality Requirements
- ✅ Zero known bugs in expression handling
- ✅ Consistent behavior across all platforms
- ✅ Clear error messages for user-facing errors
- ✅ Comprehensive documentation
- ✅ Code maintainability and readability

## Risk Mitigation

### Technical Risks
- **Complexity Risk**: Break down into small, testable increments
- **Regression Risk**: Comprehensive test suite with CI integration
- **Performance Risk**: Continuous benchmarking throughout development
- **Memory Risk**: Extensive memory testing and validation

### Schedule Risks
- **Scope Creep**: Strict adherence to defined phases
- **Dependencies**: Early identification and resolution of blockers
- **Resource**: Parallel development where possible

## Deliverables

1. **Enhanced AST Building** (`build_ast.cpp` improvements)
2. **Robust Transpilation** (`transpile.cpp` improvements)  
3. **Enhanced Runtime Support** (`lambda-eval.cpp` improvements)
4. **Comprehensive Test Suite** (200+ test cases)
5. **Documentation Updates** (API and implementation docs)
6. **Performance Benchmarks** (baseline and regression tests)

## Critical Development Guidelines ⚠️

### Safety Measures (MANDATORY)

1. **Address Sanitizer**: Always build with `-fsanitize=address` to detect memory issues
   - Modified `compile.sh` to include AddressSanitizer by default
   - Essential for catching memory corruption, leaks, and buffer overflows

2. **Incremental Development**: Fix ONE issue at a time
   - Make minimal changes per iteration
   - Test and verify each fix completely before moving to next issue
   - Document what was changed and why
   - Commit frequently with clear messages

3. **Timeout Protection**: Always run `lambda.exe` with 5-second timeout
   - Code may hang due to infinite loops or memory corruption
   - Use: `timeout 5s ./lambda.exe test_file.ls`
   - If timeout occurs, indicates serious issue requiring immediate investigation

### Development Workflow

```bash
# 1. Build with sanitizer
./compile.sh  # now includes -fsanitize=address by default

# 2. Test with timeout protection  
timeout 5s ./lambda.exe test/lambda/simple.ls

# 3. Check for sanitizer output
# Look for memory errors, leaks, or corruption reports

# 4. Fix ONE issue at a time
# 5. Repeat until all issues resolved
```

### Code Stability Warning

⚠️ **FRAGILE CODEBASE ALERT** ⚠️
- Current implementation is partially working and unstable
- Memory corruption and crashes are likely
- Infinite loops and hangs are possible
- Always use safety measures above
- Never run without timeout in production

## Questions for Clarification

1. **Priority**: Should we focus on correctness first or performance optimization?
2. **Breaking Changes**: Are breaking changes acceptable for robustness improvements?
3. **Testing Strategy**: Should we use an existing test framework or build custom assertions?
4. **Memory Management**: Are there specific memory pool optimizations we should consider?
5. **Error Handling**: What's the preferred error reporting mechanism (exceptions, error returns, etc.)?
6. **Type System**: Are there plans to extend the type system that would affect this work?

This plan provides a structured approach to systematically improve the robustness of Lambda Script's expression handling while maintaining compatibility and performance.
