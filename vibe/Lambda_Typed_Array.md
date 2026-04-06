# Proposal: Enhanced Typed Arrays for Lambda

## Summary

Unify Lambda's three typed array types (`LMD_TYPE_ARRAY_INT`, `LMD_TYPE_ARRAY_INT64`, `LMD_TYPE_ARRAY_FLOAT`) into a single `LMD_TYPE_ARRAY_NUM` with an `elem_type` discriminator that supports all numeric element types — from the existing `int` and `float` through compact sized types (i8–u64, f16–f64). Add NumPy/JS-inspired APIs for typed array construction, manipulation, and conversion. Extend vectorized arithmetic to operate natively on all element widths.

## Motivation

### Current State

Lambda has three typed array types with fixed element widths:

| Type | Element | Item Size | Storage |
|------|---------|-----------|---------|
| `LMD_TYPE_ARRAY_INT` | int56 (as int64) | 8 bytes | `int64_t*` |
| `LMD_TYPE_ARRAY_INT64` | int64 | 8 bytes | `int64_t*` |
| `LMD_TYPE_ARRAY_FLOAT` | float64 | 8 bytes | `double*` |

All three use 8 bytes per element. There are no compact arrays for smaller types.

### Gaps

1. **No compact storage for sized types** — A `u8[]` array of 1000 bytes should use ~1KB, not 8KB. Current arrays waste 7× memory for byte-sized data.
2. **No unsigned array support** — Binary data (images, audio, network packets) needs `u8[]`, `u16[]`, `u32[]`.
3. **No float32/float16 arrays** — ML model weights, GPU interop, and sensor data typically use `f32[]` or `f16[]`.
4. **~420 triplicated dispatch points** — Every array operation has 3 nearly-identical `case` branches across 16 files. Adding a new element type means adding a 4th case everywhere.
5. **Vector arithmetic ignores sized scalars** — `NUM_SIZED` and `UINT64` values don't participate in vectorized operations.
6. **No typed array construction APIs** — Users can't explicitly create typed arrays; they rely on compiler type inference.

### Why Merge All Three

| Metric | `ARRAY_INT` | `ARRAY_INT64` | `ARRAY_FLOAT` |
|--------|:-----------:|:-------------:|:--------------:|
| Total references | 139 | 137 | 141 |
| case/if dispatch branches | 57 | 99 | 94 |
| Files with dispatch points | 16 | 15 | 15 |

The three types share identical struct layouts (`type_id`, `flags`, `items*`, `length`, `extra`, `capacity`) and appear in the same `switch` statements. `array_float_new` alone has 38 call sites (21 in lambda-vector.cpp, 14 in lambda-eval-num.cpp). A unified `array_num_new(elem, len)` replaces all three factory functions and collapses ~420 case branches into ~140.

**Performance cost: negligible.** The JIT transpiler tracks `elem_type` statically and emits the same direct memory loads it does today. The interpreter adds one predicted branch (`switch(elem_type)` inside `case ARRAY_NUM:`). Vector loop bodies are identical — dispatched by `elem_type` once before the loop.

### Design Goals

- **Code simplification** — One type ID, one struct, one set of dispatch logic for all numeric arrays
- **Memory efficiency** — Pack elements at their natural width (1/2/4/8 bytes)
- **Zero-copy interop** — Typed arrays can serve as views over binary data
- **Transparent vectorization** — Arithmetic on typed arrays operates at native width
- **API parity** — Functional equivalents of JS TypedArray and NumPy operations
- **Backward compatibility** — Existing `int[]` and `float[]` syntax and semantics preserved via `ELEM_INT` / `ELEM_FLOAT`

---

## Design

### 1. Replace Three Type IDs with `LMD_TYPE_ARRAY_NUM`

Merge the three existing typed array type IDs into a single `LMD_TYPE_ARRAY_NUM` that carries a sub-type discriminator:

```c
// Before:
LMD_TYPE_ARRAY_INT,     // int56 array (8 bytes/element)
LMD_TYPE_ARRAY_INT64,   // int64 array (8 bytes/element)
LMD_TYPE_ARRAY_FLOAT,   // float64 array (8 bytes/element)

// After:
LMD_TYPE_ARRAY_NUM,     // unified numeric array (element width varies by elem_type)
```

The `EnumTypeId` becomes:

```c
enum EnumTypeId {
    // ... existing types ...
    LMD_TYPE_RANGE,
    LMD_TYPE_ARRAY_NUM,     // all numeric typed arrays (elem_type determines element kind)
    LMD_TYPE_ARRAY,         // generic Item array — unchanged
    // ...
};
```

This frees two enum slots and reduces `LMD_TYPE_COUNT` by 2.

### 2. `ArrayNum` Struct

A single struct covers all numeric element types. The element sub-type is stored in `elem_type`, reusing `Container.flags` byte position (since typed arrays don't use the existing flag bits):

```c
// Element type enum — covers Lambda's existing types + all sized types
enum EnumArrayNumElemType {
    // Lambda's standard numeric types (8 bytes/element each):
    ELEM_INT = 0,        // 8 bytes  — int56-as-int64, current ARRAY_INT behavior
    ELEM_FLOAT,          // 8 bytes  — double, current ARRAY_FLOAT behavior

    // Sized integer types (compact storage):
    ELEM_INT8,           // 1 byte   — maps to NUM_INT8
    ELEM_INT16,          // 2 bytes  — maps to NUM_INT16
    ELEM_INT32,          // 4 bytes  — maps to NUM_INT32
    ELEM_UINT8,          // 1 byte   — maps to NUM_UINT8
    ELEM_UINT16,         // 2 bytes  — maps to NUM_UINT16
    ELEM_UINT32,         // 4 bytes  — maps to NUM_UINT32

    // Sized float types (compact storage):
    ELEM_FLOAT16,        // 2 bytes  — maps to NUM_FLOAT16
    ELEM_FLOAT32,        // 4 bytes  — maps to NUM_FLOAT32

    // Explicit 64-bit types (8 bytes/element):
    ELEM_INT64,          // 8 bytes  — current ARRAY_INT64 behavior
    ELEM_UINT64,         // 8 bytes
    ELEM_FLOAT64,        // 8 bytes  — explicit f64 (same storage as ELEM_FLOAT, distinct type)

    ELEM_NUM_COUNT
};
typedef uint8_t ArrayNumElemType;
```

```c
// C struct (for MIR JIT)
struct ArrayNum {
    TypeId type_id;         // LMD_TYPE_ARRAY_NUM
    uint8_t elem_type;     // ArrayNumElemType — element sub-type
    //---------------------
    void* items;            // raw buffer (cast to int8_t*, uint16_t*, float*, int64_t*, double*, etc.)
    int64_t length;         // number of elements
    int64_t extra;          // extra elements at end
    int64_t capacity;       // allocated capacity (in elements)
};
```

```cpp
// C++ struct
struct ArrayNum : Container {
    uint8_t elem_type;     // ArrayNumElemType
    void* items;
    int64_t length;
    int64_t extra;
    int64_t capacity;
};
```

The `elem_type` field occupies the same offset as `Container.flags` (byte 1). This is safe because typed arrays don't use the `is_content`/`is_spreadable` flag bits — those are only relevant for generic `Array` and `Element`. The struct remains naturally aligned.

### 3. Element Width Table

```c
// Bytes per element, indexed by ArrayNumElemType
static const uint8_t ELEM_TYPE_SIZE[ELEM_NUM_COUNT] = {
    8, // ELEM_INT     — int64_t (int56 stored as int64)
    8, // ELEM_FLOAT   — double
    1, // ELEM_INT8
    2, // ELEM_INT16
    4, // ELEM_INT32
    1, // ELEM_UINT8
    2, // ELEM_UINT16
    4, // ELEM_UINT32
    2, // ELEM_FLOAT16
    4, // ELEM_FLOAT32
    8, // ELEM_INT64
    8, // ELEM_UINT64
    8, // ELEM_FLOAT64
};
```

Memory usage: `length × ELEM_TYPE_SIZE[elem_type]` bytes.

| Array Type | Element Size | 1000 elements | vs. generic `Array` |
|-----------|-------------|--------------|---------------------|
| `u8[]` / `i8[]` | 1 byte | 1 KB | 8× smaller |
| `i16[]` / `u16[]` / `f16[]` | 2 bytes | 2 KB | 4× smaller |
| `i32[]` / `u32[]` / `f32[]` | 4 bytes | 4 KB | 2× smaller |
| `int[]` / `float[]` / `i64[]` / `u64[]` / `f64[]` | 8 bytes | 8 KB | same |
| Generic `Array` (Item) | 8 bytes | 8 KB | baseline |

### 4. Backward Compatibility: Migration Strategy

All three existing typed arrays map cleanly to `ArrayNum` with specific `elem_type` values:

| Old Type | New Representation | `elem_type` | Storage |
|----------|-------------------|-------------|---------|
| `LMD_TYPE_ARRAY_INT` | `LMD_TYPE_ARRAY_NUM` | `ELEM_INT` | `int64_t*` (8 bytes) |
| `LMD_TYPE_ARRAY_INT64` | `LMD_TYPE_ARRAY_NUM` | `ELEM_INT64` | `int64_t*` (8 bytes) |
| `LMD_TYPE_ARRAY_FLOAT` | `LMD_TYPE_ARRAY_NUM` | `ELEM_FLOAT` | `double*` (8 bytes) |

**Phase 1 shims** — Old names alias to new:

```c
// Type aliases for incremental migration
#define ArrayInt    ArrayNum   // ELEM_INT
#define ArrayInt64  ArrayNum   // ELEM_INT64
#define ArrayFloat  ArrayNum   // ELEM_FLOAT

// Factory shims — delegate to unified constructor
ArrayNum* array_int_new(int64_t len)    { return array_num_new(ELEM_INT, len); }
ArrayNum* array_int64_new(int64_t len)  { return array_num_new(ELEM_INT64, len); }
ArrayNum* array_float_new(int64_t len)  { return array_num_new(ELEM_FLOAT, len); }

// Accessor shims — delegate to unified getter
Item    array_int_get(ArrayNum* a, int64_t i)    { return array_num_get(a, i); }
Item    array_int64_get(ArrayNum* a, int64_t i)  { return array_num_get(a, i); }
Item    array_float_get(ArrayNum* a, int64_t i)  { return array_num_get(a, i); }
int64_t array_int_get_raw(ArrayNum* a, int64_t i)   { return ((int64_t*)a->items)[i]; }
int64_t array_int64_get_raw(ArrayNum* a, int64_t i) { return ((int64_t*)a->items)[i]; }
double  array_float_get_value(ArrayNum* a, int64_t i){ return ((double*)a->items)[i]; }
```

**Phase 2** — Collapse `case` branches file by file:

```c
// Before (3 cases in every switch):
case LMD_TYPE_ARRAY_INT:   return array_int_get(arr, i);
case LMD_TYPE_ARRAY_INT64: return array_int64_get(arr, i);
case LMD_TYPE_ARRAY_FLOAT: return array_float_get(arr, i);

// After (1 case):
case LMD_TYPE_ARRAY_NUM:   return array_num_get(arr, i);
```

**Element get behavior per elem_type** — `array_num_get()` returns the same Item types as the old accessors:
- `ELEM_INT` → returns `LMD_TYPE_INT` Item (int56, same as old `array_int_get`)
- `ELEM_FLOAT` → returns `LMD_TYPE_FLOAT` Item (boxed double, same as old `array_float_get`)
- `ELEM_INT64` → returns `LMD_TYPE_INT64` Item (same as old `array_int64_get`)
- `ELEM_UINT8` → returns `LMD_TYPE_NUM_SIZED` Item (u8 packed inline)
- etc.

### 5. Literal Syntax & Type Annotations

```lambda
// Standard typed arrays (same as today, now routed through ARRAY_NUM)
let nums = [1, 2, 3]                   // int[] (ELEM_INT) — inferred from int literals
let vals = [1.0, 2.0, 3.0]             // float[] (ELEM_FLOAT) — inferred from float literals

// Sized typed array literals (element type inferred from suffix)
let bytes = [255u8, 128u8, 0u8]       // u8[] — 3 bytes storage
let weights = [0.5f32, 0.3f32, 0.2f32] // f32[] — 12 bytes storage
let coords = [100i16, 200i16, -50i16]  // i16[] — 6 bytes storage
let ids = [1000u64, 2000u64]           // u64[] — 16 bytes storage

// Explicit type annotation
let data: u8[] = [1, 2, 3, 4]          // coerce int literals to u8
let matrix: f32[] = [1.0, 2.0, 3.0]    // coerce float literals to f32
var buffer: i32[] = fill(1024, 0)       // fill returns i32[]

// Type syntax — all resolve to ARRAY_NUM with different elem_type
int[]  float[]                          // ELEM_INT, ELEM_FLOAT (Lambda standard)
i8[]  i16[]  i32[]  i64[]              // ELEM_INT8..ELEM_INT64
u8[]  u16[]  u32[]  u64[]              // ELEM_UINT8..ELEM_UINT64
f16[]  f32[]  f64[]                    // ELEM_FLOAT16..ELEM_FLOAT64
```

**Inference rules for array literals:**
1. If all elements are plain int literals → `int[]` (ELEM_INT, same as today)
2. If all elements are plain float literals → `float[]` (ELEM_FLOAT, same as today)
3. If all elements share the same sized suffix → typed array of that size (e.g., `[1u8, 2u8]` → `u8[]`)
4. If annotation present → coerce elements to annotated type (e.g., `let x: i16[] = [1, 2]`)
5. Mixed sizes or standard types → fall back to generic `Array` (Items)

---

## API Design

### 6. Construction Functions

Inspired by JS `TypedArray.of()`, `TypedArray.from()`, and NumPy `zeros()`, `ones()`, `arange()`, `linspace()`.

#### 6.1 Fill-based Construction

```lambda
// fill(count, value) — already exists, extend to infer typed array from value
fill(1000, 0u8)         // u8[] of 1000 zeros
fill(256, 0.0f32)       // f32[] of 256 zeros
fill(100, 0i16)         // i16[] of 100 zeros

// zeros(count, type) — explicit type, zero-filled
zeros(1000, u8)         // u8[] of 1000 zeros
zeros(256, f32)         // f32[] of 256 zeros

// ones(count, type) — explicit type, filled with 1
ones(100, i32)          // i32[] of 100 ones
ones(50, f16)           // f16[] of 50 ones
```

**Implementation note:** `zeros()` and `ones()` are thin wrappers around `fill()` with explicit type coercion. The typed `fill()` path calls `heap_data_calloc()` for the compact buffer.

#### 6.2 Range-based Construction

```lambda
// arange(start, stop, step, type?) — NumPy-style evenly spaced values
arange(0, 256, 1, u8)          // u8[]: [0, 1, 2, ..., 255]
arange(0.0, 1.0, 0.1, f32)     // f32[]: [0.0, 0.1, 0.2, ..., 0.9]
arange(0, 100)                  // int[]: [0, 1, ..., 99] (default: int array)

// linspace(start, stop, count, type?) — evenly spaced over interval
linspace(0.0, 1.0, 11, f32)    // f32[]: [0.0, 0.1, ..., 1.0]
linspace(-1.0, 1.0, 5)          // float[]: [-1.0, -0.5, 0.0, 0.5, 1.0]
```

#### 6.3 Conversion / Casting

```lambda
// Convert between typed arrays (like JS TypedArray constructor and NumPy astype)
let floats: f32[] = [1.1f32, 2.5f32, 3.9f32]
let ints = i32(floats)          // i32[]: [1, 2, 3] (truncate)
let bytes = u8(floats)          // u8[]: [1, 2, 3] (truncate + clamp)

// From generic array
let arr = [10, 20, 30]
let typed = i16(arr)            // i16[]: [10, 20, 30]

// From binary data (like JS DataView / NumPy frombuffer)
let raw = b'\x01\x02\x03\x04'
let view = u8(raw)              // u8[]: [1, 2, 3, 4] — zero-copy view
let shorts = i16(raw)           // i16[]: [0x0201, 0x0403] — native endian

// To binary
let buf = binary(typed)         // binary: raw bytes of the typed array
```

**Coercion rules when existing conversion functions receive typed arrays:**

| Function | Input | Output |
|----------|-------|--------|
| `i8(arr)` | any array | `i8[]` (truncate + wrap) |
| `u8(arr)` | any array | `u8[]` (truncate + wrap) |
| `i16(arr)` | any array | `i16[]` |
| `f32(arr)` | any array | `f32[]` |
| `int(arr)` | any array | `int[]` (ELEM_INT) |
| `float(arr)` | any array | `float[]` (ELEM_FLOAT) |

### 7. Access & Slicing

Typed arrays support the same indexing and slicing syntax as regular arrays:

```lambda
let arr: u8[] = [10u8, 20u8, 30u8, 40u8, 50u8]

// Single element access — returns sized scalar
arr[0]              // 10u8 (returns u8, not boxed Item)
arr[-1]             // 50u8

// Range slicing — returns new typed array of same type
arr[1 to 3]         // u8[]: [20, 30, 40]

// Length
len(arr)            // 5
```

**JIT fast-path:** When the compiler knows `arr` is `ARRAY_NUM` with a compile-time known `elem_type`, element access compiles to a single native memory load at the correct width — no boxing, no function call.

### 8. Collection Operations (Functional)

All existing Lambda collection operations extend to typed arrays. Results preserve the typed array type when possible:

```lambda
let data: f32[] = [1.0f32, 2.5f32, 3.7f32, 0.5f32, 4.2f32]

// Pipe map — result is typed array when transform preserves type
data | ~ * 2.0f32        // f32[]: [2.0, 5.0, 7.4, 1.0, 8.4]

// Filter with that — result is typed array
data that (~ > 2.0)      // f32[]: [2.5, 3.7, 4.2]

// Aggregation (returns scalar)
sum(data)                 // 11.9 (float)
avg(data)                 // 2.38 (float)
min(data)                 // 0.5 (float)
max(data)                 // 4.2 (float)

// Sorting — returns new typed array
sort(data)                // f32[]: [0.5, 1.0, 2.5, 3.7, 4.2]

// Reverse
reverse(data)             // f32[]: [4.2, 0.5, 3.7, 2.5, 1.0]

// Slice/take/drop
take(data, 3)             // f32[]: [1.0, 2.5, 3.7]
drop(data, 2)             // f32[]: [3.7, 0.5, 4.2]
slice(data, 1, 3)         // f32[]: [2.5, 3.7]

// Concatenation
data ++ [5.0f32, 6.0f32]  // f32[]: [1.0, 2.5, 3.7, 0.5, 4.2, 5.0, 6.0]

// For expressions
(for (x in data) x * x)   // f32[]: [1.0, 6.25, 13.69, 0.25, 17.64]
```

**Type preservation rules:**
- Operations that produce elements of the same type → same typed array
- Operations that widen (e.g., `u8 * u8` can exceed u8 range) → promote to wider type
- Mixed-type operations → fall back to generic Array or wider typed array

### 9. Properties

Typed arrays expose metadata through member properties:

```lambda
let arr: u8[] = zeros(100, u8)

len(arr)               // 100 — number of elements
type(arr)              // u8[] 
arr.bytes              // 100 — byte size of data buffer
arr.elem_type          // 'u8' — element type as symbol
```

---

## Vectorized Arithmetic Enhancements

### 10. Typed Array Arithmetic

Extend `vec_add`, `vec_sub`, `vec_mul`, `vec_div`, `vec_mod`, `vec_pow` to handle `ARRAY_NUM` with all elem_types:

```lambda
// Element-wise operations between typed arrays
let a: i16[] = [100i16, 200i16, 300i16]
let b: i16[] = [10i16, 20i16, 30i16]

a + b      // i16[]: [110, 220, 330]
a - b      // i16[]: [90, 180, 270]
a * b      // i32[]: [1000, 4000, 9000]  — promoted to avoid overflow
a / b      // f32[]: [10.0, 10.0, 10.0]  — division always produces float

// Scalar broadcast with typed arrays
let data: f32[] = [1.0f32, 2.0f32, 3.0f32]
data * 2.0f32    // f32[]: [2.0, 4.0, 6.0]
data + 1.0f32    // f32[]: [2.0, 3.0, 4.0]
10i16 - b        // i16[]: [0, -10, -20]

// Mixed width
let x: i8[] = [1i8, 2i8, 3i8]
let y: i16[] = [100i16, 200i16, 300i16]
x + y            // i16[]: [101, 202, 303]  — widened to i16
```

#### Arithmetic Promotion Rules (Array Context)

When two typed arrays are combined, the result element type follows these rules:

| Operation | Same type | Different width (same sign) | Signed + Unsigned | Any + float |
|-----------|-----------|---------------------------|-------------------|-------------|
| `+` `-` | Same type (wrapping) | Wider type | Next wider signed | Float type |
| `*` | Next wider type | Wider of the two widened | Next wider signed | Float type |
| `/` | `f32` or `f64` | `f32` or `f64` | `f32` or `f64` | Float type |
| `%` | Same type | Wider type | Next wider signed | Float type |
| `**` | `f64` | `f64` | `f64` | `f64` |

**Specific widening table for `*` (to prevent overflow):**

| Operand Types | Result Type | Rationale |
|---------------|-------------|-----------|
| `i8 * i8` | `i16` | 8×8 fits in 16 bits |
| `i16 * i16` | `i32` | 16×16 fits in 32 bits |
| `i32 * i32` | `i64` | 32×32 fits in 64 bits |
| `u8 * u8` | `u16` | unsigned variant |
| `f32 * f32` | `f32` | float mul stays same width |

### 11. Sized Scalar Support in Vector Ops

Currently `is_scalar_numeric()` only recognizes `INT`, `INT64`, `FLOAT`, `DECIMAL`. Extend to include `NUM_SIZED` and `UINT64`:

```c
static inline bool is_scalar_numeric(TypeId type) {
    return type == LMD_TYPE_INT || type == LMD_TYPE_INT64 ||
           type == LMD_TYPE_FLOAT || type == LMD_TYPE_DECIMAL ||
           type == LMD_TYPE_NUM_SIZED || type == LMD_TYPE_UINT64;
}
```

This enables:

```lambda
let arr: f32[] = [1.0f32, 2.0f32, 3.0f32]
arr * 2.0f32       // f32[]: scalar broadcast
arr + 100i16       // f32[]: sized scalar promotes to f32

let ints: i16[] = [10i16, 20i16, 30i16]
ints + 5u8         // i16[]: [15, 25, 35] — u8 scalar widens to i16
```

### 12. `item_to_double` Extension

Currently `item_to_double` handles `INT`, `INT64`, `FLOAT`. Extend for sized types:

```c
static double item_to_double(Item item) {
    TypeId type = get_type_id(item);
    switch (type) {
        case LMD_TYPE_INT:       return (double)item.get_int56();
        case LMD_TYPE_INT64:     return (double)item.get_int64();
        case LMD_TYPE_FLOAT:     return item.get_double();
        case LMD_TYPE_UINT64:    return (double)item.get_uint64();
        case LMD_TYPE_NUM_SIZED: {
            NumSizedType st = NUM_SIZED_SUBTYPE(item.item);
            switch (st) {
                case NUM_INT8:    return (double)item_to_i8(item.item);
                case NUM_INT16:   return (double)item_to_i16(item.item);
                case NUM_INT32:   return (double)item_to_i32(item.item);
                case NUM_UINT8:   return (double)item_to_u8(item.item);
                case NUM_UINT16:  return (double)item_to_u16(item.item);
                case NUM_UINT32:  return (double)item_to_u32(item.item);
                case NUM_FLOAT16: return (double)item_to_f16(item.item);
                case NUM_FLOAT32: return (double)item_to_f32(item.item);
            }
        }
        default: return NAN;
    }
}
```

### 13. Native-Width Fast Paths

For same-type operations on compact arrays, the runtime operates directly on the native buffer without converting to `double`:

```c
// Example: u8[] + u8[] → u8[] (wrapping)
static Item vec_add_u8(ArrayNum* a, ArrayNum* b) {
    int64_t len = a->length;
    ArrayNum* result = array_num_new(ELEM_UINT8, len);
    uint8_t* pa = (uint8_t*)a->items;
    uint8_t* pb = (uint8_t*)b->items;
    uint8_t* pr = (uint8_t*)result->items;
    for (int64_t i = 0; i < len; i++) {
        pr[i] = pa[i] + pb[i];  // native u8 wrapping add
    }
    return { .container = (Container*)result };
}
```

**SIMD opportunity:** These tight loops with fixed-width elements are ideal candidates for auto-vectorization by the C compiler (via `-O2 -ftree-vectorize`). No explicit SIMD intrinsics needed initially — the compiler can generate SSE/NEON instructions automatically for aligned buffers.

### 14. Math Module Extensions for Typed Arrays

NumPy-style element-wise math functions should return typed arrays when given typed array input:

```lambda
let data: f32[] = [1.0f32, 4.0f32, 9.0f32, 16.0f32]

math.sqrt(data)     // f32[]: [1.0, 2.0, 3.0, 4.0]
math.log(data)      // f32[]: [0.0, 1.386, 2.197, 2.773]
math.sin(data)      // f32[]: [0.841, -0.757, 0.412, -0.288]
math.exp(data)      // f32[]: [2.718, 54.598, 8103.08, ...]

// Statistical operations
math.mean(data)      // 7.5 (scalar)
math.variance(data)  // 31.25 (scalar)
math.deviation(data) // 5.59 (scalar)
math.cumsum(data)    // f32[]: [1.0, 5.0, 14.0, 30.0]
math.cumprod(data)   // f32[]: [1.0, 4.0, 36.0, 576.0]

// Vector operations
let a: f32[] = [1.0f32, 0.0f32, 0.0f32]
let b: f32[] = [0.0f32, 1.0f32, 0.0f32]
math.dot(a, b)       // 0.0 (scalar)
math.norm(a)         // 1.0 (scalar)
```

---

## Runtime Implementation

### 15. Factory Functions

```c
// Primary constructor
ArrayNum* array_num_new(ArrayNumElemType elem_type, int64_t length);

// Compatibility wrappers (Phase 1 — delegate to array_num_new)
ArrayNum* array_int_new(int64_t length);    // → array_num_new(ELEM_INT, length)
ArrayNum* array_int64_new(int64_t length);  // → array_num_new(ELEM_INT64, length)
ArrayNum* array_float_new(int64_t length);  // → array_num_new(ELEM_FLOAT, length)

// Element access (boxed — returns Item)
Item array_num_get(ArrayNum* arr, int64_t index);

// Element access (raw — returns native type, skip boxing)
int64_t array_num_get_int(ArrayNum* arr, int64_t index);
double  array_num_get_float(ArrayNum* arr, int64_t index);

// Element write
void array_num_set_int(ArrayNum* arr, int64_t index, int64_t value);
void array_num_set_float(ArrayNum* arr, int64_t index, double value);

// Coercion
ArrayNum* array_num_from_array(Array* arr, ArrayNumElemType target_type);
ArrayNum* array_num_cast(ArrayNum* src, ArrayNumElemType target_type);
```

### 16. `array_num_get` Implementation

```c
Item array_num_get(ArrayNum* arr, int64_t index) {
    if (!arr || index < 0 || index >= arr->length) return ItemNull;
    
    switch (arr->elem_type) {
        case ELEM_INT:     return i2it(((int64_t*)arr->items)[index]);  // int56 → LMD_TYPE_INT
        case ELEM_FLOAT:   return push_d(((double*)arr->items)[index]); // double → LMD_TYPE_FLOAT
        case ELEM_INT8:    return { .item = i8_to_item(((int8_t*)arr->items)[index]) };
        case ELEM_INT16:   return { .item = i16_to_item(((int16_t*)arr->items)[index]) };
        case ELEM_INT32:   return { .item = i32_to_item(((int32_t*)arr->items)[index]) };
        case ELEM_UINT8:   return { .item = u8_to_item(((uint8_t*)arr->items)[index]) };
        case ELEM_UINT16:  return { .item = u16_to_item(((uint16_t*)arr->items)[index]) };
        case ELEM_UINT32:  return { .item = u32_to_item(((uint32_t*)arr->items)[index]) };
        case ELEM_FLOAT16: return { .item = f16_to_item(((uint16_t*)arr->items)[index]) };
        case ELEM_FLOAT32: return { .item = f32_to_item(((float*)arr->items)[index]) };
        case ELEM_INT64:   return push_l(((int64_t*)arr->items)[index]);
        case ELEM_UINT64:  { uint64_t* p = (uint64_t*)heap_calloc(8, LMD_TYPE_UINT64);
                             *p = ((uint64_t*)arr->items)[index];
                             return { .item = u64_to_item(p) }; }
        case ELEM_FLOAT64: return push_d(((double*)arr->items)[index]);
        default:           return ItemNull;
    }
}
```

### 17. `ensure_typed_array` Extension

Extend the existing `ensure_typed_array()` to handle `ARRAY_NUM` coercion:

```c
void* ensure_typed_array(Item item, TypeId element_type_id) {
    TypeId item_tid = get_type_id(item);
    
    // All typed array annotations now resolve to ARRAY_NUM
    // Determine target elem_type from the annotation
    ArrayNumElemType target = annotation_to_elem_type(element_type_id, sub_type);
    
    if (item_tid == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* src = (ArrayNum*)item.container;
        if (src->elem_type == target) return src;  // already correct
        return array_num_cast(src, target);         // cross-cast (e.g., int[] → f32[])
    }
    // Convert from generic Array
    if (item_tid == LMD_TYPE_ARRAY) {
        return array_num_from_array((Array*)item.container, target);
    }
    return NULL;  // incompatible type
}
```

---

## JIT Compilation

### 18. Transpiler Changes

#### Literal Detection

When the transpiler encounters an array literal where all elements are sized literals of the same type:

```c
// In transpile_array():
if (all_elements_same_sized_type(mt, node)) {
    ArrayNumElemType elem = get_common_elem_type(mt, node);
    int64_t count = count_elements(node);
    
    // Emit: ArrayNum* arr = array_num_new(elem, count)
    MIR_reg_t arr = emit_call_2(mt, "array_num_new", MIR_T_P,
        MIR_T_I64, MIR_new_int_op(mt->ctx, elem),
        MIR_T_I64, MIR_new_int_op(mt->ctx, count));
    
    // Emit element writes at native width
    // ...
    return arr;  // with var_tid = LMD_TYPE_ARRAY_NUM
}
```

Note: Existing int-literal and float-literal array paths now also emit `array_num_new(ELEM_INT, count)` and `array_num_new(ELEM_FLOAT, count)` respectively, replacing the old `array_int_new()` / `array_float_new()` calls.

#### Inline Element Access

When `obj_tid == LMD_TYPE_ARRAY_NUM` and `elem_type` is known at compile time:

```c
// Direct memory load at native width (no function call)
// For ELEM_UINT8:
//   uint8_t val = ((uint8_t*)arr->items)[index]
//   Item result = u8_to_item(val)
// → 2 MIR instructions (load + pack), zero overhead
//
// For ELEM_INT (same as old ARRAY_INT fast path):
//   int64_t val = ((int64_t*)arr->items)[index]
//   Item result = i2it(val)
// → identical codegen to current ARRAY_INT path
//
// For ELEM_FLOAT (same as old ARRAY_FLOAT fast path):
//   double val = ((double*)arr->items)[index]
//   Item result = push_d(val)
// → identical codegen to current ARRAY_FLOAT path
```

#### Vector Operation Dispatch

```c
// In transpile_binary_expr when both sides are ARRAY_NUM:
if (left_tid == LMD_TYPE_ARRAY_NUM && right_tid == LMD_TYPE_ARRAY_NUM) {
    // If both elem_types are known and same → emit type-specialized native loop
    // Otherwise → emit call to vec_add etc. (dispatches by elem_type at runtime)
}
```

---

## Type System Integration

### 19. Type Annotations

```lambda
// Variable annotations
let a: i8[] = [1, 2, 3]
let b: u32[] = fill(100, 0)
var c: f32[] = zeros(256, f32)

// Function parameter types
fn dot_product(a: f32[], b: f32[]) f32 {
    sum(a * b)
}

// Return types
fn normalize(v: f32[]) f32[] {
    v / math.norm(v)
}
```

### 20. Type Checking

```lambda
let arr: u8[] = [1u8, 2u8, 3u8]

type(arr)            // u8[]
arr is u8[]          // true
arr is array         // true  (typed arrays are arrays)
arr is i8[]          // false (different element type)
arr is u16[]         // false (different element width)
```

### 21. `is` and Pattern Matching

```lambda
match data {
    case u8[]:    "byte array"
    case i32[]:   "int32 array"
    case f32[]:   "float32 array"
    case int[]:   "standard int array"
    case float[]: "standard float array"
    case array:   "generic array"
    default:      "not an array"
}
```

Note: All typed array `case` arms check `type_id == LMD_TYPE_ARRAY_NUM` then match on `elem_type`. The `case array:` arm matches `LMD_TYPE_ARRAY` (generic Item array) only.

---

## Prior Art

### JavaScript TypedArray

Lambda's typed arrays draw from JS TypedArray for:
- **Type-specific constructors** — JS has `Int8Array`, `Uint8Array`, etc. Lambda uses `i8[]`, `u8[]` syntax.
- **Underlying buffer** — JS uses `ArrayBuffer`; Lambda uses raw `void*` buffer with `elem_type` discriminator.
- **Element access** — Both support `arr[i]` indexing.
- **Conversion** — JS `TypedArray.from()` maps to Lambda's casting functions `u8(arr)`, `i16(arr)`.

**Differences from JS:**
- Lambda typed arrays are **immutable by default** (functional paradigm). Mutation requires `pn` context.
- No `ArrayBuffer`/`DataView` abstraction — Lambda typed arrays own their buffer directly.
- No `byteOffset` views — Lambda creates new arrays rather than buffer views (may add in future).

### NumPy ndarray

Lambda's typed arrays draw from NumPy for:
- **`dtype` parameter** — NumPy `np.zeros(100, dtype=np.uint8)` maps to Lambda `zeros(100, u8)`.
- **Element-wise arithmetic** — NumPy `a + b` broadcasts; Lambda `a + b` does the same.
- **Mathematical functions** — NumPy `np.sqrt(arr)` maps to Lambda `math.sqrt(arr)`.
- **Statistical functions** — NumPy `np.mean(arr)` maps to Lambda `math.mean(arr)`.

**Differences from NumPy:**
- Lambda is 1-dimensional only (no ndarray shape/strides). Multi-dimensional data uses nested arrays or maps.
- No broadcasting rules beyond scalar-vector — Lambda requires explicit reshaping.
- No in-place operations in functional context.

### Rust `Vec<T>` and Slices

Lambda's compact storage model is similar to Rust's `Vec<u8>`, `Vec<f32>`, etc. — contiguous memory at native element width. The key difference is Lambda's typed arrays are GC-managed, not owner-based.

---

## Open Questions

### Q1: Wrapping vs. clamping vs. error on overflow in array operations?

When `u8[] + u8[]` produces a value > 255, should it:
- (a) Wrap (modular arithmetic) — consistent with scalar sized type behavior
- (b) Clamp to 255 — like JS `Uint8ClampedArray`
- (c) Promote to wider type — like the `*` promotion table above

**Suggestion:** Default to wrapping for `+`, `-`, `%` (matching scalar behavior) and promote for `*` (to prevent silent data loss from multiplication overflow). Provide explicit `saturating_add()` etc. as opt-in.

### Q2: Zero-copy views over binary data?

Should `u8(binary_data)` create a zero-copy view (sharing the binary buffer) or always copy?

**Suggestion:** Zero-copy when the binary is immutable (which it always is in Lambda). The `ArrayNum.items` pointer points directly into the Binary's char buffer. This enables efficient binary protocol parsing without allocation.

### Q3: SIMD acceleration?

Should the proposal include explicit SIMD intrinsics or rely on compiler auto-vectorization?

**Suggestion:** Phase 1 relies on auto-vectorization (`-O2`). The tight loops over aligned `uint8_t*` / `float*` buffers are ideal for auto-vectorization. Phase 2 can add explicit NEON/SSE intrinsics for critical paths (e.g., dot product, element-wise multiply-accumulate).

### Q4: Multi-dimensional arrays (future)?

NumPy's power comes from multi-dimensional arrays with shape/strides. Should Lambda eventually support `f32[3][4]` or `f32[,]` (2D) syntax?

**Suggestion:** Out of scope for this proposal. Lambda's data model is tree-structured (arrays of arrays), not tensor-structured. If needed, a separate `Tensor` type with shape metadata could be added in the future without changing the 1D typed array design.

---

## Implementation Phases

### Phase 1: Core `ArrayNum` Infrastructure
- Define `ArrayNumElemType` enum and `ArrayNum` struct
- Replace `LMD_TYPE_ARRAY_INT`, `LMD_TYPE_ARRAY_INT64`, `LMD_TYPE_ARRAY_FLOAT` → `LMD_TYPE_ARRAY_NUM`
- Implement `array_num_new()`, `array_num_get()`, `array_num_set_*()`
- Add compatibility shims: `array_int_new()` → `array_num_new(ELEM_INT, ...)`, etc.
- Migrate existing `ArrayInt`/`ArrayInt64`/`ArrayFloat` code to `ArrayNum` with `ELEM_INT`/`ELEM_INT64`/`ELEM_FLOAT`
- Collapse triplicated `case` branches across 16 files into single `case LMD_TYPE_ARRAY_NUM:`
- Update `get_type_name()` to return `"int[]"`, `"float[]"`, `"i8[]"`, `"u8[]"`, etc. based on `elem_type`
- Register new functions in `sys_func_registry.c`
- Update GC heap (`gc_heap.c`) to handle single `ARRAY_NUM` type

### Phase 2: Literal Parsing & Type Annotations
- Teach `transpile_array()` to emit `array_num_new(ELEM_INT, ...)` for int literals (replaces `array_int_new`)
- Teach `transpile_array()` to emit `array_num_new(ELEM_FLOAT, ...)` for float literals (replaces `array_float_new`)
- Detect homogeneous sized literals → `array_num_new(ELEM_*, ...)`
- Extend `ensure_typed_array()` for `ARRAY_NUM` coercion
- Support `int[]`, `float[]`, `i8[]`, `u8[]`, `f32[]` etc. in type annotations
- Extend `build_base_type()` to recognize all numeric array types

### Phase 3: Vectorized Arithmetic
- Extend `is_scalar_numeric()` and `is_vector_type()` for `NUM_SIZED`, `UINT64`, `ARRAY_NUM`
- Extend `item_to_double()` and `vector_get()` for `ARRAY_NUM`
- Refactor `vec_scalar_op` / `vec_vec_op` to dispatch on `elem_type` instead of `type_id`
- Native-width loops for same-type operations (no double conversion)
- Arithmetic promotion table implementation

### Phase 4: Construction APIs
- `zeros(count, type)`, `ones(count, type)` — new system functions
- `arange(start, stop, step, type?)` — new system function
- `linspace(start, stop, count, type?)` — new system function
- Extend `fill()` to infer typed array from sized fill value

### Phase 5: Conversion & Binary Interop
- Extend `i8()`, `u8()`, ... conversion functions to accept arrays → typed array result
- `u8(binary)` → zero-copy view over binary data
- `binary(typed_array)` → raw bytes
- Cross-type array casting: `array_num_cast()`

### Phase 6: JIT Optimization
- Compile-time `elem_type` tracking through the transpiler
- Inline element access at native width (no function call)
- Type-specialized vector operation emission
- Loop optimization for typed array iteration

### Phase 7: Type System & Pattern Matching
- `is u8[]`, `is f32[]`, `is int[]`, `is float[]` type checks
- Pattern matching `case u8[]:` / `case int[]:` arms
- `type()` returns specific typed array type
- Typed array subtype relations: `u8[] <: array`, `int[] <: array`
