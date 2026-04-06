# Proposal: Sized Numeric Types for Lambda

## Summary

Introduce sized numeric types (signed/unsigned integers and sized floats) packed inline within the 64-bit `Item` representation. This adds low-level numeric control without heap allocation, enabling efficient interop with binary formats, hardware interfaces, and numerical computing.

## Motivation

Lambda currently supports three numeric representations:

| Type | Storage | Range |
|------|---------|-------|
| `int` (int56) | Inline in Item | ±36 quadrillion |
| `int64` | Heap-allocated pointer | Full signed 64-bit |
| `float` | Heap-allocated pointer (double) | IEEE 754 64-bit |
| `decimal` | Heap-allocated mpdecimal | Arbitrary precision |

**Gaps:**
- No unsigned integers — needed for binary protocol parsing, bitwise operations, hash values
- No sub-64-bit sized types — wasteful when processing packed binary data (images, audio, network packets)
- No float32/float16 — needed for ML model weights, GPU data, and memory-efficient numeric arrays
- `int64` and `float` require heap allocation even for single values

## Design

### 1. `LMD_TYPE_NUM_SIZED` — Inline Sized Numerics

A single new TypeId that packs small numeric values directly into the 64-bit Item, using a sub-type discriminator:

```
Item layout (64 bits):
┌──────────┬──────────┬──────────┬──────────────────────────────────┐
│ type_id  │ num_type │ (unused) │           value                  │
│  8 bits  │  8 bits  │  16 bits │          32 bits                 │
│ [63:56]  │ [55:48]  │ [47:32]  │          [31:0]                  │
└──────────┴──────────┴──────────┴──────────────────────────────────┘
```

All sized numerics fit in 32 bits or less, so the value is stored inline — **zero heap allocation**.

#### NumType Enum

```c
enum NumSizedType {
    NUM_INT8 = 0,     // signed 8-bit   [-128, 127]
    NUM_INT16,        // signed 16-bit  [-32768, 32767]
    NUM_INT32,        // signed 32-bit  [-2^31, 2^31-1]
    NUM_UINT8,        // unsigned 8-bit  [0, 255]
    NUM_UINT16,       // unsigned 16-bit [0, 65535]
    NUM_UINT32,       // unsigned 32-bit [0, 2^32-1]
    NUM_FLOAT16,      // IEEE 754 half-precision (16-bit)
    NUM_FLOAT32,      // IEEE 754 single-precision (32-bit)
    NUM_SIZED_COUNT
};
typedef uint8_t NumSizedType;
```

#### Packing / Unpacking

```c
// Pack: value stored in low 32 bits, num_type in bits [55:48], type_id in [63:56]
#define NUM_SIZED_PACK(num_type, val32) \
    (((uint64_t)LMD_TYPE_NUM_SIZED << 56) | ((uint64_t)(num_type) << 48) | ((uint32_t)(val32)))

// Unpack
#define NUM_SIZED_SUBTYPE(item)  ((uint8_t)(((uint64_t)(item) >> 48) & 0xFF))
#define NUM_SIZED_RAW32(item)    ((uint32_t)((uint64_t)(item) & 0xFFFFFFFF))
```

**C++ Item struct additions:**

```cpp
// Inside struct Item:
struct {
    uint32_t num_value;         // [31:0]  — raw 32-bit value
    uint16_t _num_pad;          // [47:32] — unused
    uint8_t  num_type;          // [55:48] — NumSizedType
    uint8_t  _type_id_num;      // [63:56] — LMD_TYPE_NUM_SIZED
};
```

**Value encoding per sub-type:**

| NumType | Stored as | Notes |
|---------|-----------|-------|
| `int8` | sign-extended to int32, stored as uint32 | Extract: `(int8_t)(raw & 0xFF)` |
| `int16` | sign-extended to int32, stored as uint32 | Extract: `(int16_t)(raw & 0xFFFF)` |
| `int32` | direct uint32 bit cast | Extract: `(int32_t)raw` |
| `uint8` | zero-extended uint32 | Extract: `(uint8_t)(raw & 0xFF)` |
| `uint16` | zero-extended uint32 | Extract: `(uint16_t)(raw & 0xFFFF)` |
| `uint32` | direct uint32 | Extract: `raw` |
| `float16` | IEEE 754 binary16 in low 16 bits | Convert to/from float via half-float routines |
| `float32` | IEEE 754 binary32 as uint32 bit pattern | Extract: `memcpy(&f, &raw, 4)` or union cast |

### 2. `LMD_TYPE_UINT64` — Unsigned 64-bit Integer

Like `LMD_TYPE_INT64`, this stores a heap-allocated pointer to a `uint64_t`:

```c
// In EnumTypeId, add after LMD_TYPE_PATH:
LMD_TYPE_UINT64,   // unsigned 64-bit integer (heap-allocated)
```

```
Item layout:
┌──────────┬──────────────────────────────────────────────────────┐
│ type_id  │              pointer to uint64_t                     │
│  8 bits  │                    56 bits                           │
│ [63:56]  │                   [55:0]                             │
└──────────┴──────────────────────────────────────────────────────┘
```

**Packing macro:**

```c
#define u2it(uint64_ptr)  ((uint64_ptr) ? ((((uint64_t)LMD_TYPE_UINT64)<<56) | (uint64_t)(uint64_ptr)) : ITEM_NULL)
```

**Range:** `[0, 2^64 - 1]` — full unsigned 64-bit.

### 3. Literal Syntax

Postfix type suffixes on numeric literals:

#### Integer Suffixes

| Suffix | Type | Example | Value |
|--------|------|---------|-------|
| `i8` | int8 | `127i8` | signed 8-bit |
| `i16` | int16 | `1000i16` | signed 16-bit |
| `i32` | int32 | `42i32` | signed 32-bit |
| `i64` | int64 | `100i64` | signed 64-bit (existing `int64`, explicit) |
| `u8` | uint8 | `255u8` | unsigned 8-bit |
| `u16` | uint16 | `60000u16` | unsigned 16-bit |
| `u32` | uint32 | `3000000000u32` | unsigned 32-bit |
| `u64` | uint64 | `18446744073709551615u64` | unsigned 64-bit |

#### Float Suffixes

| Suffix | Type | Example | Value |
|--------|------|---------|-------|
| `f16` | float16 | `0.5f16` | IEEE 754 half-precision |
| `f32` | float32 | `3.14f32` | IEEE 754 single-precision |

#### Negative Literals

Negative sized literals use the unary minus operator: `-42i8`, `-3.14f32`.

#### Grammar Changes

In `grammar.js`, extend the numeric literal rules:

```javascript
// Sized integer: [0-9]+ followed by type suffix
sized_integer: _ => token(seq(
    choice('0', seq(/[1-9]/, optional(/\d+/))),
    choice('i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64')
)),

// Sized float: float literal followed by f16/f32
sized_float: _ => token(seq(
    choice(
        seq(choice('0', seq(/[1-9]/, optional(/\d+/))), '.', /\d+/),
        seq('.', /\d+/)
    ),
    choice('f16', 'f32')
)),

// Update _number to include sized variants
_number: $ => choice($.integer, $.float, $.decimal, $.sized_integer, $.sized_float),
```

### 4. Built-in Type Names

Add the following as built-in type identifiers, usable in type annotations and `is` checks:

| Type Name | Resolves To |
|-----------|-------------|
| `i8` | `LMD_TYPE_NUM_SIZED` + `NUM_INT8` |
| `i16` | `LMD_TYPE_NUM_SIZED` + `NUM_INT16` |
| `i32` | `LMD_TYPE_NUM_SIZED` + `NUM_INT32` |
| `i64` | `LMD_TYPE_INT64` (existing type, alias) |
| `u8` | `LMD_TYPE_NUM_SIZED` + `NUM_UINT8` |
| `u16` | `LMD_TYPE_NUM_SIZED` + `NUM_UINT16` |
| `u32` | `LMD_TYPE_NUM_SIZED` + `NUM_UINT32` |
| `u64` | `LMD_TYPE_UINT64` |
| `f16` | `LMD_TYPE_NUM_SIZED` + `NUM_FLOAT16` |
| `f32` | `LMD_TYPE_NUM_SIZED` + `NUM_FLOAT32` |

These participate in the type system the same way `int`, `float`, and `int64` do:

```lambda
let x: i8 = 42i8
let y = 255u8
type(y)          // => u8
y is u8          // => true
y is int         // => true (sized ints widen to int)
```

#### `get_type_name()` Extension

```c
case LMD_TYPE_NUM_SIZED: {
    NumSizedType st = NUM_SIZED_SUBTYPE(item);
    switch (st) {
        case NUM_INT8:    return "i8";
        case NUM_INT16:   return "i16";
        case NUM_INT32:   return "i32";
        case NUM_UINT8:   return "u8";
        case NUM_UINT16:  return "u16";
        case NUM_UINT32:  return "u32";
        case NUM_FLOAT16: return "f16";
        case NUM_FLOAT32: return "f32";
    }
}
case LMD_TYPE_UINT64: return "u64";
```

Note: `get_type_name()` currently takes only `TypeId`. For `LMD_TYPE_NUM_SIZED`, we need either:
- A new `get_type_name_item(Item)` that can inspect sub-type, or
- `get_type_name(TypeId)` returns `"num_sized"` and a separate `get_num_sized_type_name(NumSizedType)` for the specific name.

### 5. Type Coercion & Arithmetic

#### Implicit Widening (lossless, automatic)

```
i8  → i16 → i32 → int (int56) → int64 → float
u8  → u16 → u32 → int (int56) → int64 → float  (if value fits)
u8  → u16 → u32 → u64                            (unsigned chain)
f16 → f32 → float
```

#### Mixed Arithmetic Rules

When two sized numerics appear in an arithmetic expression:

1. **Same signedness, different widths:** widen to larger type.
2. **Signed + unsigned, same width:** promote to next wider signed type.
   - e.g., `i8 + u8` → `i16`; `i32 + u32` → `int` (int56)
3. **Sized + regular `int`/`float`:** promote the sized type to `int`/`float`.
4. **Integer + float sized:** promote integer to float type.
   - e.g., `i32 + f32` → `f32`; `u16 + f16` → `f16`

**Overflow behavior:** Sized arithmetic wraps by default (modular arithmetic), matching hardware behavior:
```lambda
let x = 127i8 + 1i8    // => -128i8 (wraps)
let y = 255u8 + 1u8    // => 0u8 (wraps)
```

#### Explicit Conversion Functions

```lambda
i8(val)    // convert to i8 (truncate or error on out-of-range)
u32(val)   // convert to u32
f16(val)   // convert to f16 (lossy precision reduction)
```

These follow the same pattern as existing `int()`, `float()`, `decimal()` conversion functions.

### 6. Comparison with Existing Types

| Property | `int` (int56) | `int64` | New sized ints | New `u64` |
|----------|---------------|---------|----------------|-----------|
| Storage | Inline | Heap ptr | Inline | Heap ptr |
| Allocation | None | `heap_calloc` | None | `heap_calloc` |
| Signed | Yes | Yes | Both | No |
| Bit width | 56 | 64 | 8/16/32 | 64 |
| Arithmetic | Full | Full | Wrapping | Wrapping |

### 7. MIR JIT Considerations

The JIT transpiler (`transpile-mir.cpp`) needs to:

1. **Recognize sized literal nodes** from the AST and emit inline packing.
2. **Emit type-appropriate arithmetic** — call wrapping versions of add/sub/mul for sized types.
3. **Handle coercion at call boundaries** — when passing sized values to functions expecting `Item`, they're already valid Items (just a different type tag).
4. **Unbox for native math** — when both operands are the same sized type, operate on raw values and re-pack.

Since sized ints are packed inline (no heap pointer), MIR can operate on them efficiently — extract the 32-bit value, do native arithmetic, pack result. No GC interaction needed.

### 8. EnumTypeId Changes

```c
enum EnumTypeId {
    // ... existing types unchanged ...

    LMD_TYPE_PATH,           // 26

    LMD_TYPE_NUM_SIZED,      // 27 — inline sized numerics (i8..u32, f16, f32)
    LMD_TYPE_UINT64,         // 28 — unsigned 64-bit integer (heap pointer)

    LMD_TYPE_COUNT,          // 29 — must remain last
    LMD_CONTAINER_HEAP_START,
};
```

**Important:** `LMD_TYPE_COUNT` is used for array sizing. Adding two new types before it shifts its value, which is safe as long as all `TypeId`-indexed arrays are sized by `LMD_TYPE_COUNT`.

### 9. Future Phase: Typed Arrays

In a subsequent phase, add specialized typed arrays for each sized numeric type:

```c
LMD_TYPE_ARRAY_INT8,
LMD_TYPE_ARRAY_INT16,
LMD_TYPE_ARRAY_INT32,
LMD_TYPE_ARRAY_UINT8,
LMD_TYPE_ARRAY_UINT16,
LMD_TYPE_ARRAY_UINT32,
LMD_TYPE_ARRAY_UINT64,
LMD_TYPE_ARRAY_FLOAT16,
LMD_TYPE_ARRAY_FLOAT32,
```

These would store values in native-width packed buffers (e.g., `uint8_t*` for `ARRAY_UINT8`), enabling:
- Compact memory representation (8x savings for `u8[]` vs `Array`)
- SIMD-friendly memory layout
- Direct interop with binary file formats and C libraries
- Zero-copy views over binary data

Syntax:
```lambda
let pixels: u8[] = [255u8, 128u8, 0u8]
let weights: f32[] = [0.5f32, 0.3f32, 0.2f32]
```

---

## Open Questions

### Q1: Should sized arithmetic wrap or error?

The proposal specifies **wrapping** (modular arithmetic) for sized types, matching C/Rust behavior and hardware semantics. Alternative: **saturating** arithmetic (clamp to min/max), or **error on overflow** (safer, more functional style). Lambda is a functional language — silent wrapping may be surprising.

**Suggestion:** Wrap by default (performance, hardware alignment), but provide a `checked_add(a, b)` family or an annotation `@checked` for opt-in overflow detection.

### Q2: `i64` alias vs. separate literal path

The proposal makes `i64` an alias for the existing `LMD_TYPE_INT64`. But `123i64` would still parse as a distinct AST node (`sized_integer`) and then lower to the existing `push_l()` path. This works but means two parsing paths produce the same runtime type.

**Suggestion:** This is fine — `i64` is syntactic sugar. The parser detects the `i64` suffix and emits the same AST node as a regular integer that exceeds int56 range. Keep `LMD_TYPE_INT64` as-is.

### Q3: `f64` suffix for symmetry?

Should we add `f64` as an alias for the existing `float` (64-bit double)? It would complete the set: `f16`, `f32`, `f64`. Without it, users must write `3.14` (no suffix) for float64 but `3.14f32` for float32 — asymmetric.

**Suggestion:** Yes, add `f64` as an alias for `float`. No new runtime type needed — just a grammar alias.

### Q4: `get_type_name()` signature

Currently `get_type_name(TypeId)` takes only the top-level type ID. For `LMD_TYPE_NUM_SIZED`, the sub-type lives inside the Item, not in the TypeId alone. Options:
- (a) Add `get_type_name_item(Item)` that dispatches for `NUM_SIZED`.
- (b) Return generic `"num_sized"` from `get_type_name(TypeId)` and add `get_num_sized_name(NumSizedType)`.
- (c) Change `type()` system function to return the specific sub-type name for sized numerics.

**Suggestion:** Option (a) — add `get_type_name_item(Item)`, which calls through to `get_type_name(TypeId)` for all types except `NUM_SIZED`, where it inspects the sub-type byte. The `type()` system function already receives the full Item, so it can call this directly.

### Q5: Interaction with `decimal` type

Should sized floats (`f16`, `f32`) be convertible to/from `decimal`? Decimal is arbitrary precision — converting `f16` → `decimal` is lossless, but `decimal` → `f16` is extremely lossy.

**Suggestion:** Allow it with explicit conversion only: `f16(some_decimal)` truncates; `decimal(some_f16)` is lossless.

### Q6: Pattern matching on sized types

Lambda supports type patterns. Should `match` arms distinguish sized types?

```lambda
match value
    x: i8  => "8-bit signed"
    x: u32 => "32-bit unsigned"
    x: int => "regular int"
```

**Suggestion:** Yes — the sub-type byte is available at runtime, so pattern matching can discriminate. Sized types should be matchable independently. A plain `int` pattern should NOT match sized ints (they are distinct types). Use `number` to match all numeric types.

### Q7: Literal range validation at parse time vs. runtime

Should `300i8` (exceeds int8 max of 127) be a compile-time error or runtime wrap?

**Suggestion:** Compile-time error for constant literals that provably overflow. The parser/AST builder can validate ranges for literal values. Runtime wrapping applies only to computed results.

### Q8: Binary literal support

While adding sized types, consider also supporting binary and hex literals with size suffixes: `0xFFu8`, `0b1010i8`.

**Suggestion:** Yes — binary/hex literals + size suffixes are natural and commonly needed for the same use cases (protocol parsing, bitwise ops). Implement in the same grammar change.

### Q9: Float16 implementation

IEEE 754 binary16 (half precision) is not natively supported on most CPUs. Operations on `f16` values require:
- Convert f16 → f32 for arithmetic
- Convert f32 → f16 for storage

This implies `f16` arithmetic is slower than `f32`. For compute-heavy code, users should promote to `f32` or `float`.

**Suggestion:** Use the standard software conversion routines (bit manipulation, ~10 instructions each way). `f16` is primarily a storage/interchange format, not a compute format. Arithmetic on `f16` operands should auto-promote to at least `f32`, then truncate back to `f16` only on assignment.

## Prior Art

Lambda's sized numeric literal syntax is directly modeled on **Rust**, which is the closest match. Other languages offer varying degrees of numeric suffix support.

| Language | Syntax Style | Integer Suffixes | Float Suffixes | Case |
|----------|-------------|-----------------|----------------|------|
| **Rust** | `{value}{type}` | `42i8`, `255u8`, `1000i16`, `100u64` | `3.14f32`, `2.7f64` | Lowercase only (`42I8` is invalid) |
| **F#** | Single-letter | `42y` (i8), `42uy` (u8), `42s` (i16), `42us` (u16), `42l` (i32), `42L` (i64) | `3.14f` (f32) | Mixed (`L` for int64) |
| **Nim** | Apostrophe sep. | `42'i8`, `255'u8`, `1000'i16` | `3.14'f32` | Lowercase only |
| **Crystal** | Underscore sep. | `42_i8`, `255_u8`, `1000_i16` | `3.14_f32` | Lowercase only |
| **OCaml** | Letter suffixes | `42l` (i32), `42L` (i64), `42n` (nativeint) | — | Mixed |
| **C/C++** | Class suffixes | `42U`, `42L`, `42UL`, `42LL`, `42ULL` | `3.14f`, `3.14L` | Case-insensitive |
| **C#** | Class suffixes | `42L`, `42UL` | `3.14f`, `3.14d`, `3.14m` (decimal) | Case-insensitive |
| **Java/Kotlin** | Limited | `42L` (long) | `3.14f`, `3.14d` | Case-insensitive |
| **Zig** | Coercion-based | No literal suffixes — types inferred from context or explicit `@as(u8, 42)` | Same | N/A |
| **Swift** | No suffixes | No literal suffixes — types inferred from annotation: `let x: UInt8 = 42` | Same | N/A |

### Key Differences

**Rust** (closest to Lambda):
- Identical `{value}{type}` postfix syntax: `42i8`, `255u8`, `3.14f32`
- Strictly lowercase — `42I8` is a compile error
- Additional types not in Lambda: `i128`, `u128`, `isize`, `usize`
- Sized arithmetic **panics on overflow** in debug builds, **wraps** in release builds. Lambda always wraps.

**C-family** (C, C++, C#, Java):
- Only distinguish broad categories (`L` for long, `U` for unsigned, `f` for float) — no per-width suffixes like `i8` or `u16`
- Case-insensitive: `42L` and `42l` are equivalent
- Cannot express specific bit widths in literals; must use casts: `(int8_t)42`

**Nim and Crystal**:
- Use a separator character (`'` or `_`) between value and suffix, making the suffix visually distinct
- Otherwise semantically identical to Rust/Lambda

**Zig and Swift**:
- No numeric suffixes at all — rely on type inference and explicit type annotations
- More verbose but avoids suffix proliferation

### Design Rationale

Lambda follows Rust's convention because:
1. **Readable** — `42i8` clearly states both value and type in a single token
2. **Unambiguous** — no separator needed; the letter after digits starts the suffix
3. **Compact** — shorter than cast syntax (`i8(42)`) or annotation-only (`let x: i8 = 42`)
4. **Familiar** — Rust has popularized this syntax in the systems programming community

Lambda keeps suffixes **lowercase only** (matching Rust, Nim, Crystal) to avoid ambiguity with identifiers and maintain visual consistency.

## Implementation Phases

### Phase 1: Core Infrastructure
- Add `LMD_TYPE_NUM_SIZED` and `LMD_TYPE_UINT64` to `EnumTypeId`
- Define `NumSizedType` enum and packing/unpacking macros
- Add Item struct union members for sized numeric access
- Add `u2it()` macro for uint64
- Extend `get_type_name()` / add `get_type_name_item()`
- Add `item_type_id()` handling for the new types

### Phase 2: Literal Parsing
- Extend `grammar.js` with `sized_integer` and `sized_float` rules
- Regenerate parser (`make generate-grammar`)
- Update `build_ast.cpp` to produce AST nodes for sized literals
- Validate literal ranges at parse time

### Phase 3: Runtime Operations
- Implement packing functions: `int8_to_item()`, `uint32_to_item()`, etc.
- Implement unpacking: `item_to_int8()`, `item_to_uint32()`, etc.
- Implement wrapping arithmetic operators for sized types
- Add conversion system functions: `i8()`, `u32()`, `f16()`, etc.
- Update `fn_eq`, `fn_lt`, `fn_gt`, etc. for sized type comparisons
- Handle mixed-type promotion in `fn_add`, `fn_sub`, etc.

### Phase 4: JIT Compilation
- Update `transpile-mir.cpp` to emit inline packing for sized literals
- Generate type-specialized arithmetic when both operands are sized
- Handle coercion at function call boundaries

### Phase 5: Type System Integration
- Register `i8`, `i16`, `i32`, `u8`, `u16`, `u32`, `u64`, `f16`, `f32` as built-in type names
- Support sized types in type annotations, `is` checks, and pattern matching
- Update `fn_type()` to return specific sized type names
- Add to type hierarchy (subtype relationships for widening)

### Phase 6 (Future): Typed Arrays
- Add `LMD_TYPE_ARRAY_*` variants for each sized type
- Implement packed native-width array storage
- Array construction from sized literals
- SIMD optimization opportunities
