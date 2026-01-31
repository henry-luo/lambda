# Proposal: Making Lambda Codebase More Robust

**Date**: January 30, 2026  
**Context**: Lessons learned from fixing large integer truncation and NULL memory corruption bugs

## Executive Summary

Two bugs were discovered and fixed:
1. **Large integers (1.23e10) displayed incorrectly** as -584901888 due to 32-bit truncation in MIR JIT path
2. **Segfaults with complex JSON** due to NULL type writing 8 bytes when only 1 byte allocated

Root causes: divergence between `type_info[].byte_size` and actual read/write code, plus inconsistent Item handling in varargs.

This proposal outlines improvements to prevent similar issues.

---

## 1. Type Safety Improvements

### 1.1 Problem: type_info and actual code diverge silently

The `type_info` array defines byte sizes for each type, but the actual read/write code uses different sizes:

```cpp
// type_info says NULL is 1 byte
type_info[LMD_TYPE_NULL] = {sizeof(bool), "null", ...};

// But map_put() was writing 8 bytes!
case LMD_TYPE_NULL:
    *(void**)field_ptr = NULL;  // WRONG: writes 8 bytes
```

### 1.2 Solution: Compile-time assertions and unified field operations

**Add static assertions in `lambda-data.cpp`:**

```cpp
// After init_type_info(), add compile-time checks
static_assert(sizeof(bool) == 1, "bool size assumption");
static_assert(sizeof(int64_t) == 8, "int64 size assumption");
static_assert(sizeof(void*) == 8, "pointer size assumption");
static_assert(sizeof(TypedItem) == 16, "TypedItem size for ANY type");
```

**Add runtime type validation (debug builds):**

```cpp
// In lambda-data.hpp
#ifdef DEBUG
inline void validate_item_type(Item item, TypeId expected) {
    TypeId actual = get_type_id(item);
    if (actual != expected) {
        log_error("Item type mismatch: expected %s, got %s", 
                  type_info[expected].name, type_info[actual].name);
        assert(false && "Item type mismatch");
    }
}
#else
#define validate_item_type(item, expected) ((void)0)
#endif
```

### 1.3 Solution: Type-safe field write macro

```cpp
// Macro that enforces type_info byte_size
#define MAP_FIELD_WRITE(field_ptr, type_id, value_ptr) do { \
    size_t expected_size = type_info[type_id].byte_size; \
    memcpy(field_ptr, value_ptr, expected_size); \
} while(0)

#define MAP_FIELD_READ(dest_ptr, field_ptr, type_id) do { \
    size_t expected_size = type_info[type_id].byte_size; \
    memcpy(dest_ptr, field_ptr, expected_size); \
} while(0)
```

---

## 2. Modularity Improvements

### 2.1 Problem: Duplicated type-switching logic

`map_put()` and `map_get()` both have large switch statements handling each type. When INT storage changed from 4 to 8 bytes, both needed updating (and we missed one initially).

**Current state:**
- `lambda/input/input.cpp` - `map_put()` with 20+ case switch
- `lambda/lambda-data-runtime.cpp` - `map_get()` with similar switch
- `lambda/lambda-data.cpp` - `set_fields()` with another switch

### 2.2 Solution: Unified FieldOps abstraction

Create a new file `lambda/field_ops.hpp`:

```cpp
#pragma once
#include "lambda-data.hpp"

// Centralized field read/write operations
// Single source of truth for type-specific storage
namespace FieldOps {

// Write an Item value to a field pointer based on type
inline void write(void* field_ptr, TypeId type_id, Item value) {
    switch (type_id) {
    case LMD_TYPE_NULL:
        // NULL doesn't store data, just mark with zero byte
        *(uint8_t*)field_ptr = 0;
        break;
    case LMD_TYPE_BOOL:
        *(bool*)field_ptr = value.bool_val;
        break;
    case LMD_TYPE_INT:
        *(int64_t*)field_ptr = value.get_int56();
        break;
    case LMD_TYPE_INT64:
        *(int64_t*)field_ptr = value.get_int64();
        break;
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = value.get_double();
        break;
    case LMD_TYPE_STRING:
    case LMD_TYPE_SYMBOL:
    case LMD_TYPE_BINARY:
        *(String**)field_ptr = value.get_string();
        break;
    case LMD_TYPE_MAP:
    case LMD_TYPE_LIST:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_ARRAY:
        *(Container**)field_ptr = value.container;
        break;
    // ... other cases
    default:
        log_error("FieldOps::write: unhandled type %d", type_id);
        assert(false && "Unhandled type in FieldOps::write");
    }
}

// Read a field pointer into an Item based on type
inline Item read(void* field_ptr, TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_NULL:
        return ItemNull;
    case LMD_TYPE_BOOL:
        return {.item = b2it(*(bool*)field_ptr)};
    case LMD_TYPE_INT:
        return {.item = i2it(*(int64_t*)field_ptr)};
    // ... other cases
    default:
        log_error("FieldOps::read: unhandled type %d", type_id);
        return ItemNull;
    }
}

// Get storage size for a type (delegates to type_info)
inline size_t size(TypeId type_id) {
    return type_info[type_id].byte_size;
}

} // namespace FieldOps
```

**Benefits:**
- Single place to update when storage requirements change
- Consistent handling across all map operations
- Easier to audit for correctness

### 2.3 Problem: transpile_expr vs transpile_box_item confusion

The transpiler has two functions that look similar but produce different code:
- `transpile_expr()` - produces raw C expression
- `transpile_box_item()` - wraps with `i2it()`, `s2it()`, etc.

Easy to call the wrong one (as happened in `transpile_map_expr`).

### 2.4 Solution: Make context explicit

```cpp
// In transpiler.hpp
enum class ExprContext { 
    RAW,        // Raw C value (int, double, char*)
    BOXED_ITEM  // Wrapped in Item (i2it, s2it, etc.)
};

// Single entry point with explicit context
void transpile_expr(Transpiler* tp, Ast* expr, ExprContext ctx = ExprContext::RAW);

// Or use [[nodiscard]] to force handling return type
[[nodiscard]] const char* transpile_expr_raw(Transpiler* tp, Ast* expr);
[[nodiscard]] const char* transpile_expr_boxed(Transpiler* tp, Ast* expr);
```

---

## 3. Encapsulation / Hiding Implementation Details

### 3.1 Problem: Direct field access to internal structures

Code throughout the codebase directly accesses `Map::data`, `Map::data_cap`, bypassing any validation:

```cpp
// Direct access - no bounds checking
void* field_ptr = (char*)mp->data + byte_offset;
```

### 3.2 Solution: Accessor functions with bounds checking

```cpp
// In lambda-data.hpp
class MapAccessor {
public:
    // Get field pointer with bounds checking
    static void* get_field_ptr(Map* mp, size_t offset, size_t field_size) {
        assert(mp != nullptr);
        assert(mp->data != nullptr);
        assert(offset + field_size <= mp->data_cap);
        return (char*)mp->data + offset;
    }
    
    // Ensure capacity, returns true if resize occurred
    static bool ensure_capacity(Map* mp, size_t needed, MemPool* pool) {
        if (needed <= mp->data_cap) return false;
        
        size_t new_cap = mp->data_cap * 2;
        while (new_cap < needed) new_cap *= 2;
        
        void* new_data = pool_calloc(pool, new_cap);
        if (!new_data) {
            log_error("MapAccessor: failed to allocate %zu bytes", new_cap);
            return false;
        }
        
        memcpy(new_data, mp->data, mp->data_cap);
        pool_free(pool, mp->data);
        mp->data = new_data;
        mp->data_cap = new_cap;
        return true;
    }
};
```

### 3.3 Solution: Const-correct type_info access

```cpp
// In lambda-data.hpp
// Make type_info read-only after initialization
class TypeInfoRegistry {
    static TypeInfo info[32];
    static bool initialized;
    
public:
    static void init();  // Called once at startup
    
    static const TypeInfo& get(TypeId id) {
        assert(initialized && "TypeInfo not initialized");
        assert(id < 32 && "Invalid TypeId");
        return info[id];
    }
    
    static size_t byte_size(TypeId id) { return get(id).byte_size; }
    static const char* name(TypeId id) { return get(id).name; }
};
```

---

## 4. Specific Quick Wins

| Issue | Current Code | Suggested Fix |
|-------|--------------|---------------|
| NULL field writes 8 bytes | `*(void**)field_ptr = NULL` | `*(uint8_t*)field_ptr = 0` |
| INT in varargs truncates | `va_arg(args, int)` | `va_arg(args, uint64_t)` for Items |
| Magic capacity number | `byte_cap = 64` | `#define MAP_INITIAL_DATA_CAP 64` |
| Unhandled switch cases | Silent fallthrough | `default: log_error(...); assert(false);` |
| Type size assumptions | Implicit | `static_assert(sizeof(X) == N)` |

### 4.1 Define constants for magic numbers

```cpp
// In lambda.h or lambda-data.hpp
#define MAP_INITIAL_DATA_CAP    64
#define MAP_GROWTH_FACTOR       2
#define INT56_MAX               ((1LL << 55) - 1)
#define INT56_MIN               (-(1LL << 55))
```

### 4.2 Exhaustive switch statements

```cpp
// Add to all type switch statements:
default:
    log_error("Unhandled type %d (%s) in %s", 
              type_id, type_info[type_id].name, __func__);
    assert(false && "Unhandled type");
    break;
```

---

## 5. Testing Infrastructure

### 5.1 Type Consistency Tests

Add a dedicated test file `test/test_type_consistency_gtest.cpp`:

```cpp
#include <gtest/gtest.h>
#include "lambda-data.hpp"

class TypeConsistencyTest : public ::testing::Test {
protected:
    MemPool* pool;
    Input* input;
    
    void SetUp() override {
        pool = pool_create(4096);
        input = input_create(pool);
    }
    
    void TearDown() override {
        pool_destroy(pool);
    }
};

TEST_F(TypeConsistencyTest, NullFieldDoesNotCorruptNextField) {
    // Create map with NULL followed by other fields
    Map* mp = create_empty_map(input);
    
    map_put(mp, "before", {.item = i2it(42)}, input);
    map_put(mp, "null_field", ItemNull, input);
    map_put(mp, "after", {.item = i2it(123)}, input);
    
    // Verify no corruption
    EXPECT_EQ(map_get(mp, "before").get_int56(), 42);
    EXPECT_TRUE(is_null(map_get(mp, "null_field")));
    EXPECT_EQ(map_get(mp, "after").get_int56(), 123);
}

TEST_F(TypeConsistencyTest, LargeIntegerPreservation) {
    int64_t test_values[] = {
        INT56_MAX,
        INT56_MIN,
        12300000000LL,      // 1.23e10
        9007199254740991LL, // JS MAX_SAFE_INTEGER
        -9007199254740991LL
    };
    
    for (int64_t val : test_values) {
        Map* mp = create_empty_map(input);
        Item item = {.item = i2it(val)};
        map_put(mp, "test", item, input);
        
        Item read_back = map_get(mp, "test");
        EXPECT_EQ(read_back.get_int56(), val) 
            << "Failed for value: " << val;
    }
}

TEST_F(TypeConsistencyTest, ByteSizesMatchActualUsage) {
    // Verify type_info byte_size matches actual struct sizes
    EXPECT_EQ(type_info[LMD_TYPE_NULL].byte_size, 1);
    EXPECT_EQ(type_info[LMD_TYPE_BOOL].byte_size, sizeof(bool));
    EXPECT_EQ(type_info[LMD_TYPE_INT].byte_size, sizeof(int64_t));
    EXPECT_EQ(type_info[LMD_TYPE_INT64].byte_size, sizeof(int64_t));
    EXPECT_EQ(type_info[LMD_TYPE_FLOAT].byte_size, sizeof(double));
    EXPECT_EQ(type_info[LMD_TYPE_STRING].byte_size, sizeof(void*));
    EXPECT_EQ(type_info[LMD_TYPE_MAP].byte_size, sizeof(void*));
}

TEST_F(TypeConsistencyTest, MapResizePreservesData) {
    Map* mp = create_empty_map(input);
    
    // Add enough fields to trigger resize (initial cap = 64 bytes)
    // With INT at 8 bytes, 9 fields = 72 bytes > 64
    for (int i = 0; i < 20; i++) {
        char key[16];
        snprintf(key, sizeof(key), "field_%d", i);
        map_put(mp, key, {.item = i2it(i * 1000000000LL)}, input);
    }
    
    // Verify all values preserved after resize
    for (int i = 0; i < 20; i++) {
        char key[16];
        snprintf(key, sizeof(key), "field_%d", i);
        Item val = map_get(mp, key);
        EXPECT_EQ(val.get_int56(), i * 1000000000LL)
            << "Corruption at field " << i;
    }
}
```

### 5.2 Fuzzing Tests for Map Operations

```cpp
TEST_F(TypeConsistencyTest, FuzzMapWithMixedTypes) {
    Map* mp = create_empty_map(input);
    
    // Random sequence of different types
    TypeId types[] = {
        LMD_TYPE_NULL, LMD_TYPE_BOOL, LMD_TYPE_INT,
        LMD_TYPE_FLOAT, LMD_TYPE_STRING
    };
    
    for (int i = 0; i < 100; i++) {
        char key[16];
        snprintf(key, sizeof(key), "k%d", i);
        
        TypeId type = types[i % 5];
        Item value;
        
        switch (type) {
        case LMD_TYPE_NULL:
            value = ItemNull;
            break;
        case LMD_TYPE_BOOL:
            value = {.item = b2it(i % 2)};
            break;
        case LMD_TYPE_INT:
            value = {.item = i2it(i * 123456789LL)};
            break;
        // ... etc
        }
        
        map_put(mp, key, value, input);
    }
    
    // Verify integrity after all insertions
    for (int i = 0; i < 100; i++) {
        char key[16];
        snprintf(key, sizeof(key), "k%d", i);
        Item val = map_get(mp, key);
        EXPECT_NE(get_type_id(val), LMD_TYPE_ERROR)
            << "Corruption at key " << key;
    }
}
```

---

## 6. Implementation Priority

### High Priority (Prevents bugs)
1. Fix NULL field write to use 1 byte ✅ (already done)
2. Add `default:` with assert to all type switches
3. Create FieldOps abstraction for map read/write
4. Add type consistency tests

### Medium Priority (Improves maintainability)
5. Define constants for magic numbers
6. Add static_assert for type sizes
7. Create MapAccessor with bounds checking

### Low Priority (Nice to have)
8. TypeInfoRegistry for const-correct access
9. ExprContext enum for transpiler
10. Debug-mode type validation

---

## 7. Migration Path

1. **Phase 1**: Add tests for current behavior (regression prevention)
2. **Phase 2**: Introduce FieldOps alongside existing code
3. **Phase 3**: Migrate map_put/map_get to use FieldOps
4. **Phase 4**: Remove old switch statements
5. **Phase 5**: Add accessor classes for Map internals

Each phase should be a separate PR with passing tests before and after.
