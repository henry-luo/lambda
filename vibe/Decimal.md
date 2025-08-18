# Lambda Runtime: libmpdec Decimal Support - Migration Complete ✅

## Migration Status: **COMPLETED** 🎉

**Date Completed**: August 19, 2025  
**Migration**: Successfully migrated from GMP to libmpdec for decimal arithmetic

### ✅ **Migration Summary**
Lambda has successfully transitioned from GMP (GNU Multiple Precision Arithmetic Library) to **libmpdec** (the Python decimal library) for high-precision decimal arithmetic. This provides better precision, IEEE 754-2008 compliance, and improved cross-platform support.

## Current State Analysis

### ✅ **Successfully Implemented**
1. **Library Migration**: Replaced GMP with libmpdec in build system and dependencies
2. **Data Structure**: Updated `Decimal` struct to use `mpd_t*` (libmpdec type)
3. **Memory Management**: Heap-allocated, reference-counted decimal values using libmpdec
4. **Parsing**: `build_lit_decimal()` uses `mpd_qset_string()` for decimal literal parsing
5. **Printing**: `print_decimal()` uses `mpd_to_sci()` for scientific notation output
6. **Runtime Integration**: Full integration with Lambda's JIT compilation and execution
7. **Type System**: `LMD_TYPE_DECIMAL` fully integrated with boxing/unboxing macros

### ✅ **Verified Working Features**
- **Basic Literals**: `12.34n` → `12.34` ✅
- **Variable Assignment**: `let x = 123.456n; x` → `123.456` ✅  
- **Array Support**: `[1.5n, 2.75n, 3.14159n]` → `[1.5, 2.75, 3.14159]` ✅
- **Constant Pool**: Decimal literals stored and retrieved correctly ✅
- **Memory Management**: Reference counting and cleanup working ✅

### 🔄 **Current Limitations** (Future Enhancement Opportunities)
1. **Arithmetic Operations**: All arithmetic functions (`fn_add`, `fn_sub`, etc.) still need libmpdec implementation
2. **Unary Operations**: `fn_pos`/`fn_neg` decimal branches return errors  
3. **Comparisons**: Relational operators don't handle decimal types
4. **Type Conversions**: Missing decimal ↔ other type conversions
5. **Precision Control**: No default precision configuration exposed to users
6. **Cross-Platform**: Windows build configuration not yet updated

### Architecture Decision: libmpdec Benefits

**Chosen Solution**: `mpd_t*` (libmpdec Multi-precision Decimal)
- ✅ **Pros**: IEEE 754-2008 standard compliance, exact decimal arithmetic, optimized performance
- ✅ **Pros**: Better cross-platform support, memory-efficient implementation
- ✅ **Pros**: Standard decimal behavior familiar to users from Python/JavaScript
- ✅ **Pros**: No precision loss in decimal operations, proper rounding controls

**Migration from GMP Benefits**:
- Better precision control and standards compliance
- Reduced memory overhead for decimal-specific operations  
- Improved cross-platform compatibility
- More intuitive decimal behavior for end users

---

## Implementation Plan: Incremental Phases

## Phase 1: Core Arithmetic Operations 🎯

### 1.1 Memory Management Infrastructure
**Files**: `lambda/lambda-eval.cpp`, `lambda/lambda-mem.cpp`

**Implement**:
```cpp
// Decimal memory management functions
mpf_t* alloc_decimal(double precision = 256);  // Default 256-bit precision
void free_decimal(mpf_t* dec);
mpf_t* copy_decimal(const mpf_t* src);
Item push_decimal(const mpf_t* dec);    // Push decimal to runtime stack
Item create_decimal_from_double(double val);
Item create_decimal_from_string(const char* str);
```

**Key Features**:
- Integration with existing heap allocation system
- Reference counting for decimal values
- Configurable default precision (256-bit recommended)
- Memory pool integration for performance

### 1.2 Enhanced Arithmetic Functions
**Files**: `lambda/lambda-eval.cpp`

**Enhance existing functions** with decimal support:

```cpp
Item fn_add(Item item_a, Item item_b) {
    // ... existing code ...
    
    // Add decimal support
    else if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        mpf_t result;
        mpf_init(result);
        
        mpf_t* a_dec = convert_to_decimal(item_a);  // Convert any numeric type
        mpf_t* b_dec = convert_to_decimal(item_b);
        
        mpf_add(result, *a_dec, *b_dec);
        
        cleanup_temp_decimal(a_dec, item_a.type_id);  // Only free if temporary
        cleanup_temp_decimal(b_dec, item_b.type_id);
        
        return push_decimal(&result);
    }
    // ... rest of function
}
```

**Functions to enhance**:
- ✅ `fn_add` - Addition with decimal precision
- ✅ `fn_sub` - Subtraction with decimal precision  
- ✅ `fn_mul` - Multiplication with decimal precision
- ✅ `fn_div` - Division with decimal precision (no integer overflow)
- ✅ `fn_pow` - Power operations using `mpf_pow_ui`/`mpf_sqrt` for common cases
- ✅ `fn_mod` - Modulo using `mpf_fmod` equivalent (implement custom if needed)

### 1.3 Helper Functions
**Files**: `lambda/lambda-eval.cpp`

```cpp
// Type conversion functions
mpf_t* convert_to_decimal(Item item);           // Convert any numeric type to decimal
double decimal_to_double(const mpf_t* dec);    // Convert decimal to double
long decimal_to_long(const mpf_t* dec);        // Convert decimal to integer
bool is_decimal_integer(const mpf_t* dec);     // Check if decimal represents integer

// Utility functions  
void set_decimal_precision(unsigned int bits); // Set global precision
int compare_decimals(const mpf_t* a, const mpf_t* b);  // -1, 0, 1 comparison
bool decimal_is_zero(const mpf_t* dec);        // Zero check for division
```

**Timeline**: 2-3 days
**Testing**: Extend `test/lambda/numeric_expr.ls` with decimal operations

---

## Phase 2: Unary Operations & Comparisons 🎯

### 2.1 Enhanced Unary Operations
**Files**: `lambda/lambda-eval.cpp`

```cpp
Item fn_pos(Item item) {
    // ... existing code ...
    else if (item.type_id == LMD_TYPE_DECIMAL) {
        // Return copy of decimal (unary + keeps same value)
        mpf_t* dec = (mpf_t*)item.pointer;
        return push_decimal(dec);  // Creates copy automatically
    }
}

Item fn_neg(Item item) {
    // ... existing code ...
    else if (item.type_id == LMD_TYPE_DECIMAL) {
        mpf_t result;
        mpf_init(result);
        mpf_neg(result, *(mpf_t*)item.pointer);
        return push_decimal(&result);
    }
}
```

### 2.2 Comparison Operations
**Files**: `lambda/lambda-eval.cpp`

**Enhance comparison functions**:

```cpp
Item fn_eq(Item a_item, Item b_item) {
    // ... existing fast paths ...
    
    // Add decimal comparison support
    if (a_item.type_id == LMD_TYPE_DECIMAL || b_item.type_id == LMD_TYPE_DECIMAL) {
        mpf_t* a_dec = convert_to_decimal(a_item);
        mpf_t* b_dec = convert_to_decimal(b_item);
        
        int cmp_result = mpf_cmp(*a_dec, *b_dec);
        bool equal = (cmp_result == 0);
        
        cleanup_temp_decimal(a_dec, a_item.type_id);
        cleanup_temp_decimal(b_dec, b_item.type_id);
        
        return {.item = b2it(equal)};
    }
}
```

**Functions to enhance**:
- ✅ `fn_eq` / `fn_ne` - Equality with decimal precision
- ✅ `fn_lt` / `fn_le` - Less-than with decimal precision
- ✅ `fn_gt` / `fn_ge` - Greater-than with decimal precision

**Timeline**: 1-2 days  
**Testing**: Add decimal comparison tests to `test/lambda/numeric_expr.ls`

---

## Phase 3: System Functions & Type Conversions 🎯

### 3.1 Enhanced System Functions
**Files**: `lambda/lambda-eval.cpp`

**Add decimal support to system functions**:

```cpp
// In sys_func_* implementations
case SYSFUNC_NUMBER:
    if (input_type == LMD_TYPE_DECIMAL) {
        return item;  // Already a decimal, return as-is
    }
    // Convert other types to decimal
    return create_decimal_from_item(item);

case SYSFUNC_INT:
    if (input_type == LMD_TYPE_DECIMAL) {
        mpf_t* dec = (mpf_t*)item.pointer;
        long int_val = mpf_get_si(*dec);  // Convert to signed integer
        return (Item){.item = i2it(int_val)};
    }

case SYSFUNC_FLOAT:
    if (input_type == LMD_TYPE_DECIMAL) {
        mpf_t* dec = (mpf_t*)item.pointer;
        double float_val = mpf_get_d(*dec);
        return push_d(float_val);
    }
```

**New system function**:
```cpp
case SYSFUNC_DECIMAL:  // New: decimal() conversion function
    return create_decimal_from_item(item);
```

### 3.2 Mathematical Functions
**Files**: `lambda/lambda-eval.cpp`

**Enhance mathematical system functions**:

```cpp
// Functions to enhance with decimal support
Item fn_abs(Item item);    // mpf_abs
Item fn_round(Item item);  // mpf_round (implement custom rounding)
Item fn_floor(Item item);  // mpf_floor  
Item fn_ceil(Item item);   // mpf_ceil
Item fn_sqrt(Item item);   // mpf_sqrt
```

**Timeline**: 2-3 days
**Testing**: Add comprehensive system function tests

---

## Phase 4: String Integration & I/O 🎯

### 4.1 Enhanced String Operations
**Files**: `lambda/lambda-eval.cpp`

**String parsing enhancements**:
```cpp
Item fn_pos(Item item) {
    // ... existing string parsing code ...
    
    // Enhanced decimal parsing for strings
    else if (item.type_id == LMD_TYPE_STRING || item.type_id == LMD_TYPE_SYMBOL) {
        String* str = (String*)item.pointer;
        
        // Try parsing as decimal first (higher precision)
        mpf_t dec_val;
        mpf_init(dec_val);
        int parse_result = mpf_set_str(dec_val, str->chars, 10);
        
        if (parse_result == 0) {  // Successfully parsed
            return push_decimal(&dec_val);
        }
        
        mpf_clear(dec_val);
        // Fall back to existing integer/float parsing
        // ... existing code ...
    }
}
```

### 4.2 Enhanced String Formatting  
**Files**: `lambda/print.cpp`

**Improve decimal printing**:
```cpp
// In print_item() for LMD_TYPE_DECIMAL case
case LMD_TYPE_DECIMAL: {
    mpf_t *num = (mpf_t*)item.pointer;
    
    // Configurable precision output
    char format_str[32];
    snprintf(format_str, sizeof(format_str), "%%.%dFf", get_decimal_display_precision());
    
    if (HAS_GMP_IO()) {
        gmp_sprintf(buf, format_str, *num);
    } else {
        // Fallback with precision warning
        double approximation = mpf_get_d(*num);
        snprintf(buf, sizeof(buf), "%.15g", approximation);
        // Consider adding precision loss warning
    }
    
    strbuf_append_str(buffer, buf);
    break;
}
```

**Timeline**: 1-2 days
**Testing**: String conversion and formatting tests

---

## Phase 5: Memory Management & Performance 🎯

### 5.1 GMP Memory Integration
**Files**: `lambda/lambda-mem.cpp` (new), `lambda/lambda-eval.cpp`

**Custom GMP memory functions**:
```cpp
// GMP memory integration with Lambda's pool system
void* gmp_malloc_func(size_t size);
void* gmp_realloc_func(void* oldptr, size_t old_size, size_t new_size);  
void gmp_free_func(void* ptr, size_t size);

// Initialize GMP memory integration
void init_gmp_memory_integration() {
    mp_set_memory_functions(gmp_malloc_func, gmp_realloc_func, gmp_free_func);
}
```

### 5.2 Decimal Cleanup Integration
**Files**: `lambda/lambda-eval.cpp`

**Reference counting for decimals**:
```cpp
// Integrate decimal cleanup with existing ref counting
void cleanup_item(Item item) {
    if (item.type_id == LMD_TYPE_DECIMAL) {
        mpf_t* dec = (mpf_t*)item.pointer;
        mpf_clear(*dec);
        heap_free(dec);  // Use existing heap management
    }
    // ... existing cleanup code ...
}
```

### 5.3 Performance Optimizations
**Strategies**:
- **Precision Caching**: Cache common precisions to avoid repeated `mpf_init2` calls
- **Small Number Fast Path**: Use native arithmetic for small decimals that fit in double
- **Operation Chaining**: Minimize temporary allocations in expression chains
- **Lazy Conversion**: Only convert to decimal when mixed-type operations require it

**Timeline**: 2-3 days
**Testing**: Memory leak tests, performance benchmarks

---

## Phase 6: Cross-Platform & Edge Cases 🎯

### 6.1 Windows Support Enhancement
**Files**: `windows-deps/src/gmp_stub.c`, `windows-deps/include/gmp.h`

**Enhance Windows stub**:
```c
// Add missing mpf_t functions to Windows stub
void mpf_init(mpf_t x);
void mpf_clear(mpf_t x);
void mpf_add(mpf_t rop, mpf_srcptr op1, mpf_srcptr op2);
void mpf_sub(mpf_t rop, mpf_srcptr op1, mpf_srcptr op2);
void mpf_mul(mpf_t rop, mpf_srcptr op1, mpf_srcptr op2);
void mpf_div(mpf_t rop, mpf_srcptr op1, mpf_srcptr op2);
int mpf_cmp(mpf_srcptr op1, mpf_srcptr op2);
double mpf_get_d(mpf_srcptr op);
// ... other essential functions
```

**Alternative**: Link against full GMP for Windows builds

### 6.2 Edge Case Handling
**Files**: `lambda/lambda-eval.cpp`

**Comprehensive error handling**:
```cpp
// Division by zero for decimals
Item fn_div_decimal(mpf_t* a, mpf_t* b) {
    if (mpf_cmp_ui(*b, 0) == 0) {
        printf("decimal division by zero error\n");
        return ItemError;
    }
    
    mpf_t result;
    mpf_init(result);
    mpf_div(result, *a, *b);
    return push_decimal(&result);
}

// Overflow/underflow detection
bool check_decimal_overflow(const mpf_t* dec) {
    // Check if decimal exceeds reasonable bounds
    return mpf_cmp_d(*dec, 1e308) > 0 || mpf_cmp_d(*dec, -1e308) < 0;
}
```

**Edge cases to handle**:
- ✅ Division by zero
- ✅ Very large numbers (overflow detection)  
- ✅ Very small numbers (underflow to zero)
- ✅ Invalid string conversions
- ✅ Memory allocation failures
- ✅ Precision loss warnings

**Timeline**: 2-3 days
**Testing**: Comprehensive edge case test suite

---

## Phase 7: Configuration & Documentation 🎯

### 7.1 Decimal Configuration
**Files**: `lambda/lambda-eval.cpp`, build configs

**Configuration options**:
```cpp
// Global decimal configuration
typedef struct {
    unsigned int default_precision;     // Default: 256 bits
    unsigned int display_precision;     // Default: 15 decimal places
    bool round_on_display;             // Default: true
    bool warn_on_precision_loss;       // Default: false
} DecimalConfig;

DecimalConfig decimal_config = {
    .default_precision = 256,
    .display_precision = 15,
    .round_on_display = true,
    .warn_on_precision_loss = false
};
```

### 7.2 Build System Integration
**Files**: `Makefile`, `build_lambda_config.json`

**GMP build detection**:
```makefile
# Enhanced GMP detection
GMP_AVAILABLE := $(shell pkg-config --exists gmp && echo yes || echo no)
ifeq ($(GMP_AVAILABLE),yes)
    GMP_CFLAGS := $(shell pkg-config --cflags gmp)
    GMP_LIBS := $(shell pkg-config --libs gmp)
    DECIMAL_FLAGS := -DLAMBDA_DECIMAL_FULL_GMP
else
    DECIMAL_FLAGS := -DLAMBDA_DECIMAL_STUB
    $(warning "GMP not found, using stub implementation")
endif
```

### 7.3 Documentation & Examples
**Files**: `doc/decimals.md`, `test/lambda/decimal_examples.ls`

**Example Lambda scripts**:
```lambda
// Financial calculations with exact precision
let price = 19.99n
let tax_rate = 0.08n  
let total = price * (1.0n + tax_rate)

// High-precision mathematical calculations
let pi_approx = 3.14159265358979323846n
let circumference = 2.0n * pi_approx * radius

// Decimal comparisons
if (total > 20.0n) {
    print("Expensive item")
}
```

**Timeline**: 1-2 days

---

## Testing Strategy

### Unit Tests
**File**: `test/lambda/decimal_comprehensive.ls`

```lambda
// Arithmetic operations
let a = 123.456n
let b = 789.012n
assert(a + b == 912.468n)
assert(a - b == -665.556n)
assert(a * b == 97408.047872n)

// High precision
let precise = 1.23456789012345678901234567890n
assert(precise > 1.23456789012345678901234567889n)

// Type conversions
assert(int(123.999n) == 123)
assert(float(123.456n) == 123.456)
assert(string(123.456n) == "123.456")

// Edge cases
assert(0.0n / 1.0n == 0.0n)
assert(error(1.0n / 0.0n))  // Division by zero
```

### Performance Tests
**File**: `test/performance/decimal_perf.ls`

```lambda
// Large number arithmetic
let large_a = 999999999999999999999999999999.999999999999999n
let large_b = 111111111111111111111111111111.111111111111111n
time_operation(large_a * large_b)

// Precision stress test
let precision_test = 1.0n
for i in (1 to 1000) {
    precision_test = precision_test * 1.0001n
}
```

### Integration Tests
- ✅ Mixed arithmetic (decimal + int + float)
- ✅ System function integration
- ✅ String parsing and formatting
- ✅ Memory management under load
- ✅ Cross-platform compatibility

---

## Risk Assessment & Mitigation

### High-Risk Areas
1. **Memory Management**: GMP allocations not integrated with Lambda's pool system
   - **Mitigation**: Custom GMP memory functions, thorough leak testing
   
2. **Performance Impact**: Decimal operations significantly slower than native arithmetic
   - **Mitigation**: Fast paths for common cases, optional decimal mode
   
3. **Cross-Platform Issues**: GMP availability varies across platforms
   - **Mitigation**: Robust stub implementation, build-time detection

4. **Precision Expectations**: Users may expect unlimited precision
   - **Mitigation**: Clear documentation, configurable precision limits

### Medium-Risk Areas
1. **API Consistency**: Decimal behavior differs from int/float
   - **Mitigation**: Comprehensive testing, clear type conversion rules

2. **Build Complexity**: GMP dependency adds build complications
   - **Mitigation**: Optional compilation, fallback mechanisms

---

## Success Metrics

### Phase Completion Criteria
- ✅ All arithmetic operations support decimal operands
- ✅ All comparison operations handle decimal types
- ✅ System functions seamlessly convert to/from decimals
- ✅ No memory leaks in decimal operations
- ✅ Performance within 10x of native operations for common cases
- ✅ Cross-platform build success (macOS, Linux, Windows)

### Quality Gates
- ✅ 100% test coverage for decimal operations
- ✅ All existing tests continue to pass
- ✅ Performance benchmarks meet targets
- ✅ Memory usage analysis shows no excessive overhead

---

## Timeline Summary

| Phase | Duration | Deliverable |
|-------|----------|-------------|
| Phase 1 | 2-3 days | Core arithmetic with decimals |
| Phase 2 | 1-2 days | Unary ops & comparisons |
| Phase 3 | 2-3 days | System functions & conversions |
| Phase 4 | 1-2 days | String integration & I/O |
| Phase 5 | 2-3 days | Memory management & performance |
| Phase 6 | 2-3 days | Cross-platform & edge cases |
| Phase 7 | 1-2 days | Configuration & documentation |

**Total Estimated Time**: 11-18 days (2-3 weeks)

---

## Long-Term Enhancements (Future Phases)

### Advanced Features
- **Rational Numbers**: Add `mpq_t` support as `LMD_TYPE_RATIONAL`
- **Complex Numbers**: Support complex decimals for mathematical applications
- **Decimal Arrays**: Optimized storage for arrays of decimals
- **Serialization**: Binary serialization of decimal values
- **Mathematical Constants**: Built-in high-precision constants (π, e, etc.)

### Performance Optimizations
- **SIMD Operations**: Vectorized decimal operations where supported
- **Compile-Time Precision**: Template-like precision specialization
- **Caching**: Frequently used decimal values in cache
- **Native Code**: JIT compilation of decimal expressions

This incremental plan ensures Lambda gets robust, production-ready decimal support while maintaining system stability and performance.
