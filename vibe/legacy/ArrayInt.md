# ArrayInt and ArrayLong Refactoring Plan

## Overview

This document outlines the comprehensive refactoring plan to enhance Lambda's type system with proper int32/int64 distinction and introduce ArrayInt/ArrayLong built-in types.

## Current State Analysis

### Existing Type System
- **LMD_TYPE_INT**: Currently maps to 32-bit signed integers, stored inline in Item union
- **LMD_TYPE_INT64**: Maps to 64-bit signed integers, stored as heap pointers
- **LMD_TYPE_ARRAY_INT**: Already exists but maps to ArrayLong (64-bit integers)
- **LMD_TYPE_ARRAY_FLOAT**: Exists for double arrays

### Current Issues
1. Inconsistent naming: `LMD_TYPE_ARRAY_INT` actually stores `long*` (64-bit) values
2. Missing dedicated 32-bit integer array type
3. Grammar only supports `int` type, no `int64` type
4. Type system conflates 32-bit and 64-bit integer handling

## Proposed Changes

### 1. Type System Refactoring

#### Revised Type Mappings
- **LMD_TYPE_INT** → int32 + **LMD_TYPE_ARRAY_INT** (32-bit integer arrays)
- **LMD_TYPE_INT64** → int64 + **LMD_TYPE_ARRAY_INT64** (64-bit integer arrays)

#### Struct Definitions
```c
// Repurpose existing ArrayLong as ArrayInt for 32-bit integers
struct ArrayInt : Container {
    int32_t* items;      // 32-bit integer array
    long length;
    long extra;
    long capacity;
};

// New 64-bit integer array type
struct ArrayInt64 : Container {
    int64_t* items;      // 64-bit integer array  
    long length;
    long extra;
    long capacity;
};
```

### 2. Grammar Extensions

#### Add int64 Type Support
In `grammar.js`, update `built_in_types()`:
```javascript
function built_in_types(include_null) { 
  let types = [
    'any', 'error', 'bool',
    'int',      // 32-bit integers
    'int64',    // 64-bit integers (NEW)
    'float', 'decimal', 'number',
    // ... rest unchanged
  ];
  return include_null ? choice('null', ...types) : choice(...types);
}
```

### 3. Implementation Plan - Incremental Phases

#### Phase 1: Core Type Infrastructure
**Files to modify:**
- `lambda/lambda.h`
- `lambda/lambda-data.hpp`
- `lambda/lambda-data.cpp`

**Changes:**
1. Add `LMD_TYPE_ARRAY_INT64` enum after existing `LMD_TYPE_ARRAY_FLOAT`
2. Add `ArrayInt64` struct definition (rename existing `ArrayLong` to `ArrayInt`)
3. Update `Item` union to include `ArrayInt64* array_int64`
4. Update type_info array in lambda-data.cpp with new entry
5. Repurpose existing `ArrayLong` as `ArrayInt` for 32-bit integers

#### Phase 2: Grammar and Parser Updates
**Files to modify:**
- `lambda/tree-sitter-lambda/grammar.js`

**Changes:**
1. Add `int64` to built-in types
2. Update parser to distinguish between `int` and `int64` literals
3. Regenerate parser using `generate-parser.sh`

#### Phase 3: AST Building and Type Inference
**Files to modify:**
- `lambda/build_ast.cpp`

**Changes:**
1. Update `build_primary_expr()` to handle int64 literals
2. Add type inference for ArrayInt vs ArrayLong
3. Update system function handling for int/int64 distinction
4. Modify binary expression type promotion rules

#### Phase 4: Runtime Evaluation
**Files to modify:**
- `lambda/lambda-eval.cpp`

**Changes:**
1. Convert existing `ArrayLong` functions to work with 32-bit `ArrayInt`:
   - Modify `array_long_new()` → `array_int_new()` (use `int32_t`)
   - Modify `array_long_new_item()` → `array_int_new_item()`
2. Implement new `ArrayInt64` creation functions:
   - `array_int64_new(int count, ...)`
   - `array_int64_new_item(int count, ...)`
3. Update arithmetic operations to handle both ArrayInt and ArrayInt64
4. Add proper type promotion between 32-bit and 64-bit arrays

#### Phase 5: System Functions and Built-ins
**Files to modify:**
- `lambda/build_ast.cpp` (system function calls)
- `lambda/lambda-eval.cpp` (system function implementations)

**Changes:**
1. Add `int64()` system function for explicit casting
2. Update `len()`, `min()`, `max()`, `sum()`, `avg()` for ArrayInt
3. Add type conversion functions between ArrayInt/ArrayLong

#### Phase 6: Memory Management and Utilities
**Files to modify:**
- `lambda/lambda-eval.cpp`
- `lambda/lambda.h`

**Changes:**
1. Implement proper heap allocation for ArrayInt
2. Add reference counting support
3. Update garbage collection if applicable
4. Add utility functions for ArrayInt manipulation

### 4. Detailed Function Specifications

#### New ArrayInt and ArrayInt64 Functions
```c
// ArrayInt functions (32-bit integers)
ArrayInt* array_int_new(int count, ...);
Item array_int_new_item(int count, ...);
int32_t array_int_get(ArrayInt* array, int index);
void array_int_set(ArrayInt* array, int index, int32_t value);
void array_int_push(ArrayInt* array, int32_t value);

// ArrayInt64 functions (64-bit integers)
ArrayInt64* array_int64_new(int count, ...);
Item array_int64_new_item(int count, ...);
int64_t array_int64_get(ArrayInt64* array, int index);
void array_int64_set(ArrayInt64* array, int index, int64_t value);
void array_int64_push(ArrayInt64* array, int64_t value);

// Conversion functions
ArrayInt64* array_int_to_int64(ArrayInt* arr);
ArrayInt* array_int64_to_int(ArrayInt64* arr);  // with overflow checking
```

#### Updated System Functions
```c
// Enhanced system functions
int64_t fn_int64(Item item);     // Cast to int64
Item fn_int(Item item);       // Cast to int32 (with overflow check)
```

### 5. Type Promotion Rules

#### Arithmetic Operations
1. **int32 + int32** → int32 (with overflow detection)
2. **int64 + int64** → int64
3. **int32 + int64** → int64 (promote int32)
4. **ArrayInt + ArrayInt** → ArrayInt (element-wise, 32-bit)
5. **ArrayInt64 + ArrayInt64** → ArrayInt64 (element-wise, 64-bit)
6. **ArrayInt + ArrayInt64** → ArrayInt64 (promote ArrayInt to 64-bit)

#### Overflow Handling
- Detect int32 overflow and promote to int64 or decimal
- Provide explicit casting functions for controlled conversions
- Add runtime warnings for potential data loss

### 6. Testing Strategy

#### Unit Tests Location
`./test/lambda/` directory structure:
```
test/lambda/
├── array_int_basic.ls          # Basic ArrayInt operations
├── array_long_basic.ls         # Basic ArrayLong operations  
├── int_int64_literals.ls       # Literal parsing tests
├── type_promotion.ls           # Arithmetic promotion tests
├── array_conversion.ls         # ArrayInt/ArrayLong conversion
├── system_functions.ls         # int(), int64(), len() etc.
├── overflow_detection.ls       # Overflow handling tests
└── mixed_operations.ls         # Mixed int32/int64 operations
```

#### Test Categories
1. **Literal Tests**: Verify `123` vs `123L` parsing
2. **Array Creation**: Test ArrayInt/ArrayLong construction
3. **Arithmetic**: All binary operations with type promotion
4. **System Functions**: len(), min(), max(), sum() etc.
5. **Type Conversion**: Explicit and implicit conversions
6. **Edge Cases**: Overflow, underflow, empty arrays
7. **Performance**: Large array operations

### 7. Migration Strategy

#### Backward Compatibility
- Existing `int` literals remain 32-bit by default
- `LMD_TYPE_ARRAY_INT` behavior preserved (maps to ArrayLong)
- Add deprecation warnings for ambiguous usage
- Provide migration guide for existing code

#### Breaking Changes
- Grammar now distinguishes `int` vs `int64`
- Some implicit conversions may change behavior
- Array type inference may produce different results

### 8. Implementation Checklist

#### Phase 1: Core Infrastructure ✓
- [ ] Add ArrayInt struct to lambda.h
- [ ] Update Item union with array_int field  
- [ ] Add LMD_TYPE_ARRAY_INT32 enum
- [ ] Update type_info array
- [ ] Add ArrayInt type constants

#### Phase 2: Grammar Updates ✓
- [ ] Add int64 to grammar.js built_in_types
- [ ] Update parser generation
- [ ] Test grammar parsing int vs int64

#### Phase 3: AST Building ✓
- [ ] Update build_primary_expr for int64
- [ ] Add ArrayInt type inference
- [ ] Update system function parsing
- [ ] Modify binary expression type rules

#### Phase 4: Runtime Evaluation ✓
- [ ] Implement array_int_new functions
- [ ] Add ArrayInt arithmetic operations
- [ ] Update fn_add, fn_sub, fn_mul, fn_div
- [ ] Add ArrayInt indexing support

#### Phase 5: System Functions ✓
- [ ] Implement int64() system function
- [ ] Update len() for ArrayInt
- [ ] Update min/max/sum/avg for ArrayInt
- [ ] Add type conversion utilities

#### Phase 6: Memory Management ✓
- [ ] ArrayInt heap allocation
- [ ] Reference counting
- [ ] Garbage collection updates
- [ ] Memory leak prevention

#### Phase 7: Testing ✓
- [ ] Write comprehensive unit tests
- [ ] Test all arithmetic operations
- [ ] Test type promotion rules
- [ ] Test system functions
- [ ] Performance benchmarking

#### Phase 8: Documentation ✓
- [ ] Update Lambda language documentation
- [ ] Add migration guide
- [ ] Update API documentation
- [ ] Add examples and tutorials

### 9. Risk Assessment

#### High Risk Areas
1. **Type Promotion Complexity**: Ensuring consistent behavior across all operations
2. **Memory Management**: Proper allocation/deallocation of ArrayInt
3. **Backward Compatibility**: Existing code behavior changes
4. **Performance Impact**: Additional type checking overhead

#### Mitigation Strategies
1. Comprehensive unit testing at each phase
2. Incremental implementation with rollback capability
3. Performance benchmarking before/after changes
4. Code review for memory safety

### 10. Timeline Estimate

- **Phase 1-2**: 2-3 days (Core infrastructure + Grammar)
- **Phase 3-4**: 3-4 days (AST + Runtime evaluation)  
- **Phase 5-6**: 2-3 days (System functions + Memory)
- **Phase 7**: 2-3 days (Comprehensive testing)
- **Phase 8**: 1-2 days (Documentation)

**Total Estimated Time**: 10-15 days

### 11. Success Criteria

1. All existing tests pass without modification
2. New ArrayInt/ArrayLong functionality works correctly
3. Type promotion follows documented rules
4. No memory leaks or crashes
5. Performance impact < 5% for existing code
6. Comprehensive test coverage > 90%

---

This refactoring will significantly enhance Lambda's type system while maintaining backward compatibility and providing a solid foundation for future numeric type enhancements.
