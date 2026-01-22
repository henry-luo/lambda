# Proposal: Change LMD_TYPE_INT from int32 to int56

## Executive Summary

This document proposes changing the underlying representation of `LMD_TYPE_INT` from `int32` to `int56` (56-bit signed integer packed inline). The motivation is to:
1. **Greatly expanded range**: ±36 quadrillion vs ±2 billion
2. **Consistent error handling**: Use `ITEM_ERROR` for division-by-zero, matching other Item error conventions
3. **True integer semantics**: Exact arithmetic for `+`, `-`, `*`, `%` operations
4. **Graceful division**: `/` promotes to double before dividing, returning float result

---

## Options Comparison

### Option 1: Keep int32 (Current)

| Aspect | Description |
|--------|-------------|
| **Storage** | 32 bits packed in Item, 24 bits unused |
| **Range** | ±2,147,483,647 (~2.1×10⁹) |
| **Overflow** | Promotes to double (via `push_d`) |
| **Division by zero** | Returns `ItemError` |

**Pros:**
- ✅ Simple, well-tested implementation
- ✅ Exact integer arithmetic
- ✅ Fast operations on all CPUs
- ✅ Works with bitwise operations

**Cons:**
- ❌ Limited range (only ~2 billion)
- ❌ Wastes 24 bits of Item storage
- ❌ Overflow to double is awkward (changes type unexpectedly)

---

### Option 2: float32

| Aspect | Description |
|--------|-------------|
| **Storage** | 32 bits packed in Item |
| **Range** | ±3.4×10³⁸ (but precision loss beyond 2²⁴) |
| **Overflow** | Becomes `±inf` naturally |
| **Division by zero** | Returns `±inf` or `NaN` (IEEE 754) |

**Pros:**
- ✅ Built-in IEEE 754 divide-by-zero handling
- ✅ No integer overflow wrap-around
- ✅ Huge representable range

**Cons:**
- ❌ **Precision loss**: Integers > 16,777,216 lose precision
  - `16777217.0f == 16777216.0f` is **true**!
- ❌ Modulo (`%`) semantics change to `fmodf`
- ❌ Cannot do bitwise operations
- ❌ Comparison edge cases (NaN ≠ NaN)
- ❌ Users expect `int` to be exact

---

### Option 3: int56 (Recommended) ✅

| Aspect | Description |
|--------|-------------|
| **Storage** | 56 bits packed in Item (uses full available space) |
| **Range** | ±36,028,797,018,963,967 (~3.6×10¹⁶) |
| **Overflow** | Returns `ITEM_ERROR` |
| **Division by zero** | Returns `ITEM_ERROR` |

**Pros:**
- ✅ **17× more bits** than int32 (56 vs 32)
- ✅ **16 million× larger range** than int32
- ✅ Exact integer arithmetic (no precision loss)
- ✅ Consistent error handling via `ITEM_ERROR`
- ✅ Unbox to int64 for safe computation
- ✅ Division promotes to double (existing behavior)
- ✅ Works with bitwise operations
- ✅ Maximizes use of Item's 64-bit storage

**Cons:**
- ⚠️ Still has finite range (but huge: ±36 quadrillion)
- ⚠️ Slightly more complex bit manipulation

---

## Decision: int56

We choose **int56** because it:
1. Maximizes the integer range within Item's storage constraints
2. Preserves exact integer semantics that users expect
3. Uses `ITEM_ERROR` consistently for error cases
4. Leverages existing double-promotion for division

---

## Current Implementation Analysis

### How LMD_TYPE_INT Works Today

**Item Union Structure** ([lambda.hpp#L7-L15](lambda/lambda.hpp#L7-L15)):
```cpp
typedef struct Item {
    union {
        struct {
            int int_val: 32;        // <-- only 32 bits used
            uint32_t _24: 24;       // <-- 24 bits WASTED
            uint32_t _type_id: 8;   // LMD_TYPE_INT tag in high byte
        };
        uint64_t item;
        // ... other union members
    };
} Item;
```

**Key Insight**: The current design wastes 24 bits. We can use all 56 bits (64 - 8 for type tag) for the integer value.

### Current Conversion Macros ([lambda.h#L208-L210](lambda/lambda.h#L208-L210))

```cpp
#define ITEM_INT  ((uint64_t)LMD_TYPE_INT << 56)
#define i2it(int_val)  ((int_val) <= INT32_MAX && (int_val) >= INT32_MIN \
    ? (ITEM_INT | ((int64_t)(int_val) & 0x00FFFFFFFFFFFFFF)) \
    : push_d(int_val))
```

Note: The mask `0x00FFFFFFFFFFFFFF` already extracts 56 bits! The code was designed for this.

---

## Proposed Changes

### Design Principles

1. **Pack**: Store signed 56-bit integer in low 56 bits of Item
2. **Unpack**: Sign-extend to int64 for all computations
3. **Arithmetic**: Use int64 for `+`, `-`, `*` to detect overflow
4. **Division**: Promote to double, return double result
5. **Errors**: Return `ITEM_ERROR` for overflow and division-by-zero

### 1. Item Union Structure Changes

**File**: [lambda/lambda.hpp](lambda/lambda.hpp#L7-L15)

```cpp
// BEFORE
struct {
    int int_val: 32;
    uint32_t _24: 24;
    uint32_t _type_id: 8;
};

// AFTER - conceptually (actual access via bit manipulation)
struct {
    int64_t int56_val: 56;  // signed 56-bit integer
    uint8_t _type_id: 8;
};
```

**Note**: C bitfields for signed integers across byte boundaries are tricky. Better to use explicit bit manipulation.

### 2. Constants and Limits

**File**: [lambda/lambda.h](lambda/lambda.h)

```cpp
// NEW: int56 limits
#define INT56_MAX  ((int64_t)0x007FFFFFFFFFFFFF)   // +36,028,797,018,963,967
#define INT56_MIN  ((int64_t)0xFF80000000000000)   // -36,028,797,018,963,968

#define ITEM_INT   ((uint64_t)LMD_TYPE_INT << 56)
```

### 3. Conversion Macro Changes

**File**: [lambda/lambda.h](lambda/lambda.h#L208-L210)

```cpp
// BEFORE
#define i2it(int_val)  ((int_val) <= INT32_MAX && (int_val) >= INT32_MIN \
    ? (ITEM_INT | ((int64_t)(int_val) & 0x00FFFFFFFFFFFFFF)) \
    : push_d(int_val))

// AFTER - check 56-bit range, return ITEM_ERROR on overflow
#ifndef __cplusplus
#define i2it(val) \
    (((int64_t)(val) <= INT56_MAX && (int64_t)(val) >= INT56_MIN) \
        ? (ITEM_INT | ((uint64_t)(val) & 0x00FFFFFFFFFFFFFF)) \
        : ITEM_ERROR)
#else
#define i2it(val) \
    (((int64_t)(val) <= INT56_MAX && (int64_t)(val) >= INT56_MIN) \
        ? (ITEM_INT | ((uint64_t)(val) & 0x00FFFFFFFFFFFFFF)) \
        : ITEM_ERROR)
#endif
```

### 4. Extraction Function Changes

**File**: [lambda/lambda-data.cpp](lambda/lambda-data.cpp#L231-L244)

```cpp
// BEFORE
int it2i(Item itm) {
    if (itm._type_id == LMD_TYPE_INT) {
        return itm.int_val;
    }
    // ...
    return INT_ERROR;
}

// AFTER - sign-extend 56-bit to 64-bit
int64_t it2i(Item itm) {
    if (itm._type_id == LMD_TYPE_INT) {
        // Extract low 56 bits and sign-extend
        int64_t val = itm.item & 0x00FFFFFFFFFFFFFF;
        // Sign extension: if bit 55 is set, extend the sign
        if (val & 0x0080000000000000) {
            val |= 0xFF00000000000000;  // set high 8 bits
        }
        return val;
    }
    else if (itm._type_id == LMD_TYPE_INT64) {
        return itm.get_int64();
    }
    else if (itm._type_id == LMD_TYPE_FLOAT) {
        return (int64_t)itm.get_double();
    }
    return INT64_MAX;  // error sentinel
}
```

**Alternative with helper function**:
```cpp
// Helper: extract signed 56-bit integer from Item
inline int64_t item_get_int56(Item itm) {
    uint64_t raw = itm.item & 0x00FFFFFFFFFFFFFF;
    // Sign extend from bit 55
    if (raw & 0x0080000000000000ULL) {
        return (int64_t)(raw | 0xFF00000000000000ULL);
    }
    return (int64_t)raw;
}
```

### 5. Item Union Access

**File**: [lambda/lambda.hpp](lambda/lambda.hpp)

Add helper method to Item struct:

```cpp
typedef struct Item {
    // ... existing union members ...
    
    // Helper to get int56 value as int64
    inline int64_t get_int56() const {
        uint64_t raw = item & 0x00FFFFFFFFFFFFFFULL;
        if (raw & 0x0080000000000000ULL) {
            return (int64_t)(raw | 0xFF00000000000000ULL);
        }
        return (int64_t)raw;
    }
} Item;
```

### 6. Arithmetic Operations Changes

**File**: [lambda/lambda-eval-num.cpp](lambda/lambda-eval-num.cpp)

All arithmetic uses int64, then checks for overflow before packing:

```cpp
Item fn_add(Item item_a, Item item_b) {
    TypeId type_a = get_type_id(item_a);
    TypeId type_b = get_type_id(item_b);
    
    if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_INT) {
        int64_t a = item_a.get_int56();
        int64_t b = item_b.get_int56();
        int64_t result = a + b;
        
        // Check for overflow (result outside int56 range)
        if (result > INT56_MAX || result < INT56_MIN) {
            log_error("integer overflow in addition");
            return ItemError;
        }
        return { .item = i2it(result) };
    }
    // ... rest of function unchanged for other type combinations
}

Item fn_sub(Item item_a, Item item_b) {
    if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_INT) {
        int64_t a = item_a.get_int56();
        int64_t b = item_b.get_int56();
        int64_t result = a - b;
        
        if (result > INT56_MAX || result < INT56_MIN) {
            log_error("integer overflow in subtraction");
            return ItemError;
        }
        return { .item = i2it(result) };
    }
    // ...
}

Item fn_mul(Item item_a, Item item_b) {
    if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_INT) {
        int64_t a = item_a.get_int56();
        int64_t b = item_b.get_int56();
        
        // Use __builtin_mul_overflow for efficient overflow detection
        // Falls back to __int128 on platforms without builtin support
#if defined(__GNUC__) || defined(__clang__)
        int64_t result;
        if (__builtin_mul_overflow(a, b, &result)) {
            log_error("integer overflow in multiplication");
            return ItemError;
        }
        // Also check int56 range (result fits in int64 but may exceed int56)
        if (result > INT56_MAX || result < INT56_MIN) {
            log_error("integer overflow in multiplication");
            return ItemError;
        }
#else
        // Fallback: use __int128 for precise overflow detection
        __int128 wide_result = (__int128)a * (__int128)b;
        if (wide_result > INT56_MAX || wide_result < INT56_MIN) {
            log_error("integer overflow in multiplication");
            return ItemError;
        }
        int64_t result = (int64_t)wide_result;
#endif
        return { .item = i2it(result) };
    }
    // ...
}
```

### 7. Division - Promote to Double

**File**: [lambda/lambda-eval-num.cpp](lambda/lambda-eval-num.cpp)

```cpp
Item fn_div(Item item_a, Item item_b) {
    TypeId type_a = get_type_id(item_a);
    TypeId type_b = get_type_id(item_b);
    
    if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_INT) {
        int64_t a = item_a.get_int56();
        int64_t b = item_b.get_int56();
        
        if (b == 0) {
            log_error("division by zero");
            return ItemError;
        }
        
        // Promote to double for division (always returns float)
        return push_d((double)a / (double)b);
    }
    // ... rest unchanged
}
```

### 8. Integer Division and Modulo

```cpp
Item fn_idiv(Item item_a, Item item_b) {
    if (item_a._type_id == LMD_TYPE_INT && item_b._type_id == LMD_TYPE_INT) {
        int64_t a = item_a.get_int56();
        int64_t b = item_b.get_int56();
        
        if (b == 0) {
            log_error("integer division by zero");
            return ItemError;
        }
        
        int64_t result = a / b;  // truncates toward zero
        return { .item = i2it(result) };
    }
    // ...
}

Item fn_mod(Item item_a, Item item_b) {
    if (item_a._type_id == LMD_TYPE_INT && item_b._type_id == LMD_TYPE_INT) {
        int64_t a = item_a.get_int56();
        int64_t b = item_b.get_int56();
        
        if (b == 0) {
            log_error("modulo by zero");
            return ItemError;
        }
        
        int64_t result = a % b;  // exact integer modulo
        return { .item = i2it(result) };
    }
    // ...
}
```

### 9. Comparison Operations

**File**: [lambda/lambda-eval.cpp](lambda/lambda-eval.cpp)

```cpp
Bool fn_eq(Item a_item, Item b_item) {
    // ...
    if (a_item._type_id == LMD_TYPE_INT) {
        return (a_item.get_int56() == b_item.get_int56()) ? BOOL_TRUE : BOOL_FALSE;
    }
    // ...
}

Bool fn_lt(Item a_item, Item b_item) {
    // ...
    if (a_item._type_id == LMD_TYPE_INT) {
        return (a_item.get_int56() < b_item.get_int56()) ? BOOL_TRUE : BOOL_FALSE;
    }
    // ...
}
```

### 10. Type Printing

**File**: [lambda/print.cpp](lambda/print.cpp#L77-79)

```cpp
// Keep as int64_t since that's what we unbox to
case LMD_TYPE_INT:
    strbuf_append_str(code_buf, "int64_t");
    break;
```

### 11. Function Signature Changes

**File**: [lambda/lambda.h](lambda/lambda.h#L291)

```cpp
// BEFORE
int it2i(Item item);

// AFTER - returns int64 (unboxed from int56)
int64_t it2i(Item item);
```

---

## Files Requiring Changes

### Core Runtime (Must Change)
| File | Changes |
|------|---------|
| [lambda/lambda.hpp](lambda/lambda.hpp) | Add `get_int56()` helper method |
| [lambda/lambda.h](lambda/lambda.h) | `INT56_MAX/MIN` constants, `i2it` macro, `it2i` return type |
| [lambda/lambda-data.cpp](lambda/lambda-data.cpp) | `it2i`, `it2l`, `it2d` - use `get_int56()` |
| [lambda/lambda-eval-num.cpp](lambda/lambda-eval-num.cpp) | All arithmetic: use `get_int56()`, check overflow |
| [lambda/lambda-eval.cpp](lambda/lambda-eval.cpp) | Comparisons: use `get_int56()` |

### Input/Output
| File | Changes |
|------|---------|
| [lambda/print.cpp](lambda/print.cpp) | Value formatting with int64 |
| [lambda/format/format.cpp](lambda/format/format.cpp) | Number output |
| [lambda/mark_builder.cpp](lambda/mark_builder.cpp) | `createInt()` - accept int64 |
| [lambda/mark_editor.cpp](lambda/mark_editor.cpp) | Field get/set |
| [lambda/mark_reader.cpp](lambda/mark_reader.cpp) | `as_int()` return int64 |

### Transpiler
| File | Changes |
|------|---------|
| [lambda/transpile.cpp](lambda/transpile.cpp) | Use int64_t for LMD_TYPE_INT |
| [lambda/transpile-mir.cpp](lambda/transpile-mir.cpp) | MIR code generation |

### Minimal Changes Needed
| File | Changes |
|------|---------|
| [lambda/input/input-json.cpp](lambda/input/input-json.cpp) | Already uses int64 internally |
| [lambda/input/input-yaml.cpp](lambda/input/input-yaml.cpp) | Already uses int64 internally |

---

## Behavioral Changes

### Semantic Comparison

| Operation | Current (int32) | Proposed (int56) |
|-----------|-----------------|------------------|
| `5 / 2` | 2.5 (double) | 2.5 (double) - same |
| `5 _/ 2` | 2 | 2 - same |
| `5 % 2` | 1 | 1 - same |
| `1 / 0` | Error | Error - same |
| `0 / 0` | Error | Error - same |
| `2147483647 + 1` | Overflow → double | 2147483648 (exact int56) ✅ |
| `16777217` | 16777217 | 16777217 (exact) ✅ |
| Max value | ~2.1×10⁹ | ~3.6×10¹⁶ (16M× larger) ✅ |

### Key Improvements
1. **No silent type changes**: Overflow returns `ITEM_ERROR` instead of changing to double
2. **Much larger range**: 36 quadrillion vs 2 billion
3. **Exact semantics**: All integer operations remain exact
4. **Consistent errors**: Division-by-zero returns `ITEM_ERROR`

---

## Literal Parsing and Auto-Promotion

### Design Decision: Large Literals Promote to Float

When parsing integer literals in Lambda scripts, if the literal value exceeds the int56 range, it is **automatically promoted to float (double)** rather than producing a parse error.

**Rationale:**
1. **Graceful handling**: Users writing large numbers shouldn't get cryptic parse errors
2. **Predictable behavior**: Large constants become floats, which can represent them (with potential precision loss for very large values)
3. **Distinction from runtime**: Literals are known at parse time; runtime overflow returns `ITEM_ERROR`

### Implementation

**File**: [lambda/build_ast.cpp](lambda/build_ast.cpp#L687-L696)

```cpp
// Check if the value fits in 56-bit signed integer range
if (INT56_MIN <= value && value <= INT56_MAX) {
    log_debug("Using LIT_INT for value %lld", value);
    i_node->type = &LIT_INT;
}
else { // promote to float for values outside int56 range
    log_debug("Using float for value %lld (outside int56 range)", value);
    i_node->type = build_lit_float(tp, expr_node);
}
```

### Examples

```lambda
// Within int56 range - stays as int
let a = 36028797018963967   // INT56_MAX - type: int
type(a)  // → "int"

// Exceeds int56 range - promoted to float
let b = 36028797018963968   // INT56_MAX + 1 - type: float
type(b)  // → "float"

// Large negative - promoted to float
let c = -36028797018963969  // < INT56_MIN - type: float
type(c)  // → "float"

// Very large literal - promoted to float
let d = 99999999999999999999  // type: float
type(d)  // → "float"
```

### Comparison: Parsing vs Runtime

| Scenario | Parsing (Literals) | Runtime (Operations) |
|----------|-------------------|---------------------|
| Value > INT56_MAX | Promotes to float | Returns `ITEM_ERROR` |
| Value < INT56_MIN | Promotes to float | Returns `ITEM_ERROR` |
| Detection | At compile time | At runtime |
| Behavior | Silent type change | Explicit error |

**Why the difference?**
- **Literals**: Known at compile time, can be represented as float with no further consequences
- **Runtime overflow**: Result of computation, should signal error so caller can handle it

---

## Migration Strategy

### Phase 1: Add Infrastructure
1. Add `INT56_MAX`, `INT56_MIN` constants
2. Add `get_int56()` helper method to Item
3. Add `item_get_int56()` C helper function

### Phase 2: Update Core
1. Update `i2it` macro for 56-bit range check
2. Update `it2i` to return int64 with sign extension
3. Update arithmetic functions to use `get_int56()`

### Phase 3: Update Peripheral Code
1. Update print/format functions
2. Update MarkBuilder/MarkReader
3. Update transpiler

### Phase 4: Testing
1. Run `make test-lambda-baseline` - must pass 100%
2. Add tests for large integers (> 2³¹)
3. Add overflow detection tests
4. Test edge cases around INT56_MAX/MIN

---

## Summary

The int56 approach is the optimal choice because:

| Criterion | int32 | float32 | int56 ✅ |
|-----------|-------|---------|----------|
| Range | 2×10⁹ | 3×10³⁸* | 3×10¹⁶ |
| Precision | Exact | Lossy >2²⁴ | Exact |
| Overflow | Changes type | inf | Error |
| Div-by-zero | Error | inf/NaN | Error |
| Modulo | Exact | fmodf | Exact |
| Bitwise ops | ✅ | ❌ | ✅ |
| Storage efficiency | 32/64 bits | 32/64 bits | 56/64 bits |

*float32 range comes with precision tradeoffs

**Recommendation**: Proceed with int56 implementation.
