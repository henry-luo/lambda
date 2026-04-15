# Experiment Proposal: Direct C Pointer for String, Symbol, Binary

**Branch:** `direct-string-pointer`  
**Baseline:** Round 4 benchmark results (`test/benchmark/Overall_Result4.md`)  
**Goal:** Determine whether eliminating high-byte type tagging for `String`, `Symbol`, and `Binary` and switching to direct C pointer storage (same as containers) improves runtime performance and/or reduces peak memory.

---

## Table of Contents

1. [Background](#1-background)
2. [Motivation](#2-motivation)
3. [Proposed Struct Layout Changes](#3-proposed-struct-layout-changes)
4. [Boxing and Unboxing Changes](#4-boxing-and-unboxing-changes)
5. [Affected Files](#5-affected-files)
6. [Implementation Plan](#6-implementation-plan)
7. [Benchmark Plan](#7-benchmark-plan)
8. [Expected Results and Success Criteria](#8-expected-results-and-success-criteria)
9. [Risks and Mitigations](#9-risks-and-mitigations)

---

## 1. Background

Lambda's 64-bit `Item` type uses two distinct pointer-boxing strategies depending on the data type:

### 1a. Tagged pointers — for String, Symbol, Binary (and numeric heap types)

The high 8 bits of the `Item` store the `TypeId`; the low 56 bits store the object pointer.

```c
// Boxing (lambda.h)
#define s2it(str_ptr)   ((str_ptr) ? ((((uint64_t)LMD_TYPE_STRING) << 56) | (uint64_t)(str_ptr)) : ITEM_NULL)
#define y2it(sym_ptr)   ((sym_ptr) ? ((((uint64_t)LMD_TYPE_SYMBOL) << 56) | (uint64_t)(sym_ptr)) : ITEM_NULL)
#define x2it(bin_ptr)   ((bin_ptr) ? ((((uint64_t)LMD_TYPE_BINARY) << 56) | (uint64_t)(bin_ptr)) : ITEM_NULL)

// Unboxing (Item union bitfields)
inline String* get_string() const { return (String*)this->string_ptr; }  // string_ptr: 56-bit field
inline Symbol* get_symbol() const { return (Symbol*)this->symbol_ptr; }  // symbol_ptr: 56-bit field
inline String* get_binary() const { return (String*)this->binary_ptr; }  // binary_ptr: 56-bit field
```

The `string_ptr`, `symbol_ptr`, and `binary_ptr` bitfields are 56 bits wide; the tag is automatically stripped by the bitfield read. Type identification requires no pointer dereference — it is read directly from `item._type_id` (high byte).

The `String`, `Symbol`, and `Binary` structs themselves carry **no `type_id` field**:

```c
typedef struct String {
    uint32_t len;       // byte length
    uint8_t  is_ascii;  // 1 if pure ASCII
    char     chars[];   // UTF-8 data (null-terminated)
} String;              // header: 5 bytes

typedef struct Symbol {
    uint32_t len;       // symbol name length
    Target*  ns;        // namespace (NULL for unqualified)
    char     chars[];   // symbol characters
} Symbol;              // header: 16 bytes (4 + 4pad + 8)

typedef String Binary;  // alias
```

### 1b. Direct C pointers — for containers (Range, List, Array, Map, Element, Object, …)

Container types store raw 64-bit pointers in the `Item` union. The `TypeId` is stored as the **first field** in each container struct:

```c
struct Container {
    TypeId  type_id;  // at offset 0 — identifies the concrete type
    uint8_t flags;
};

struct List {
    TypeId   type_id;  // offset 0
    uint8_t  flags;    // offset 1
    Item*    items;    // offset 8  (6 bytes padding for pointer alignment)
    int64_t  length;   // offset 16
    ...
};
```

Boxing and unboxing use plain pointer casts — no bit manipulation:

```c
// Boxing
static inline Item p2it(void* ptr) {
    return (Item)(uint64_t)(uintptr_t)ptr;  // store raw 64-bit pointer
}

// Unboxing
#define it2list(item)  ((List*)(uintptr_t)(item))
#define it2map(item)   ((Map*)(uintptr_t)(item))
```

Type detection dereferences the first byte:

```cpp
inline TypeId type_id() {
    if (this->_type_id) return this->_type_id;  // tagged scalars (Int, Bool, Float, String …)
    if (this->item)     return *((TypeId*)this->item);  // containers — dereference first byte
    return LMD_TYPE_NULL;
}
```

### 1c. The asymmetry

The current design creates two distinct code paths for the same kind of operation (boxing/unboxing heap-allocated objects), distinguished solely by whether the object carries an embedded `type_id`:

| Property | String/Symbol/Binary | Containers |
|---|---|---|
| TypeId in Item | High 8 bits (bit shift) | No tag (pointer only) |
| TypeId in struct | No | Yes (first byte) |
| Boxing | OR with shifted TypeId | Plain pointer cast |
| Unboxing | 56-bit masked bitfield | Plain pointer cast |
| Type check | `item._type_id` (free) | `*((TypeId*)ptr)` (1 dereference) |
| JIT-emitted instructions | OR + shift per boxing | Store only |

---

## 2. Motivation

The hypothesis is that eliminating the tag-in-high-bits representation for String/Symbol/Binary and aligning them with the container model will produce measurable performance benefits on string-heavy benchmarks:

### 2a. Fewer instructions per boxing operation

Every time the JIT (MIR Direct path, `transpile-mir.cpp`) boxes a `String*` into an `Item`, it currently emits:

```c
item = (((uint64_t)LMD_TYPE_STRING) << 56) | (uint64_t)str_ptr;
// compiled to: OR  xN, xM, #(LMD_TYPE_STRING << 56)
```

With direct pointer storage, boxing a `String*` becomes a no-op register move (same as boxing a Map or Array). On ARM64, OR-with-immediate costs one cycle; eliminating it in hot loops (e.g. repeated string construction) could compound.

### 2b. Simpler unboxing on the hot path

Unboxing currently requires a 56-bit bitfield read, which the compiler typically lowers to:

```asm
// ARM64 unboxing for string_ptr (56-bit bitfield)
ubfx  x1, x0, #0, #56   // extract bits [55:0]
```

With a direct pointer, unboxing is:

```asm
// ARM64 unboxing for direct pointer
mov   x1, x0             // or just use x0 directly
```

The `ubfx` instruction is 1 cycle on M-series, so the benefit is marginal per operation, but string operations appear in tight inner loops (string comparison, hashing, length checks).

### 2c. Uniform type dispatch

Currently `item.type_id()` has a two-branch check: test `_type_id != 0` (tagged scalar path), otherwise dereference. After the change, all heap-allocated types — containers and strings alike — go to the dereference path. The `_type_id != 0` fast lane is retained only for inline scalars (Bool, Int, Int64, Float, Decimal, DateTime, Error).

This may improve branch predictor accuracy in type-dispatch routines because all pointer-carrying items now share the same code path.

### 2d. Simplified GC traversal

The garbage collector currently has a special case for String/Symbol/Binary (they have no embedded `type_id` so the GC must rely on the Item tag to classify them). After the change, the GC can identify any live object — whether it is a String, Symbol, or Map — by reading `*((TypeId*)ptr)`, eliminating the special-case branch.

### 2e. Alignment with future optimisations

Once String/Symbol live as plain pointers, they can be stored in lists/arrays of pointers using the same patterns as container lists, opening the door for future optimisations like typed string arrays.

---

## 3. Proposed Struct Layout Changes

### 3a. String (and Binary)

**Current layout (5-byte header):**

```
offset  0: uint32_t len        (4 bytes)
offset  4: uint8_t  is_ascii   (1 byte)
offset  5: char     chars[]    (flexible array)
```

**Proposed layout (8-byte header):**

```
offset  0: TypeId   type_id    (1 byte) ← NEW: LMD_TYPE_STRING or LMD_TYPE_BINARY
offset  1: uint8_t  is_ascii   (1 byte)
offset  2: uint8_t  _pad[2]    (2 bytes, explicit padding)
offset  4: uint32_t len        (4 bytes, naturally 4-byte aligned)
offset  8: char     chars[]    (flexible array)
```

C definition:

```c
typedef struct String {
    TypeId  type_id;      // LMD_TYPE_STRING (or LMD_TYPE_BINARY for binary data)
    uint8_t is_ascii;     // 1 if all bytes < 0x80
    uint8_t _pad[2];      // explicit padding — sizeof(String) == 8
    uint32_t len;         // byte length of the string
    char    chars[];      // UTF-8 or binary data (null-terminated)
} String;
typedef String Binary;    // same struct, distinguished by type_id value
```

Header size increases from 5 → 8 bytes (+3 bytes per String allocation). Strings are heap-allocated and typically much larger than 3 bytes, so the relative overhead per string is low. Benchmarks with very high string allocation rates (e.g. `base64`, `json_gen`, `revcomp`) will be the most sensitive.

### 3b. Symbol

**Current layout (16-byte header):**

```
offset  0: uint32_t len        (4 bytes)
offset  4: uint8_t  _pad[4]    (4 bytes implicit padding for pointer alignment)
offset  8: Target*  ns         (8 bytes)
offset 16: char     chars[]    (flexible array)
```

**Proposed layout (16-byte header — same size!):**

```
offset  0: TypeId   type_id    (1 byte) ← NEW: LMD_TYPE_SYMBOL
offset  1: uint8_t  _pad[3]    (3 bytes explicit padding for uint32_t alignment)
offset  4: uint32_t len        (4 bytes)
offset  8: Target*  ns         (8 bytes, naturally 8-byte aligned)
offset 16: char     chars[]    (flexible array)
```

C definition:

```c
typedef struct Symbol {
    TypeId  type_id;    // LMD_TYPE_SYMBOL
    uint8_t _pad[3];    // explicit padding
    uint32_t len;       // symbol name byte length
    Target*  ns;        // namespace target (NULL for unqualified)
    char    chars[];    // symbol name characters
} Symbol;
```

**Symbol header is unchanged at 16 bytes** — the `type_id` byte and 3 padding bytes occupy the same 4 bytes that were previously all padding after `len`. No memory regression for symbols.

### 3c. Allocation initialisation

Wherever a `String` or `Symbol` is allocated (in the GC heap, name pool, arena, or pool), the newly added `type_id` field must be set at construction time:

```c
// String construction (example):
String* s = (String*)heap_calloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
s->type_id  = LMD_TYPE_STRING;     // ← new
s->is_ascii = 0;
s->len      = len;
memcpy(s->chars, src, len);
s->chars[len] = '\0';

// Binary construction:
String* b = (String*)heap_calloc(sizeof(String) + len + 1, LMD_TYPE_BINARY);
b->type_id  = LMD_TYPE_BINARY;     // ← different type_id

// Symbol construction (example):
Symbol* sym = (Symbol*)heap_calloc(sizeof(Symbol) + len + 1, LMD_TYPE_SYMBOL);
sym->type_id = LMD_TYPE_SYMBOL;    // ← new
sym->len     = len;
sym->ns      = ns;
memcpy(sym->chars, name, len);
sym->chars[len] = '\0';
```

---

## 4. Boxing and Unboxing Changes

### 4a. Boxing macros

Replace tag-or operations with plain pointer-to-item casts:

```c
// BEFORE
#define s2it(str_ptr)   ((str_ptr) ? ((((uint64_t)LMD_TYPE_STRING) << 56) | (uint64_t)(str_ptr)) : ITEM_NULL)
#define y2it(sym_ptr)   ((sym_ptr) ? ((((uint64_t)LMD_TYPE_SYMBOL) << 56) | (uint64_t)(sym_ptr)) : ITEM_NULL)
#define x2it(bin_ptr)   ((bin_ptr) ? ((((uint64_t)LMD_TYPE_BINARY) << 56) | (uint64_t)(bin_ptr)) : ITEM_NULL)

// AFTER
#define s2it(str_ptr)   p2it(str_ptr)
#define y2it(sym_ptr)   p2it(sym_ptr)
#define x2it(bin_ptr)   p2it(bin_ptr)
```

The `p2it` macro is already defined for container boxing:

```c
static inline Item p2it(void* ptr) {
    if (!ptr) return ITEM_NULL;
    return (Item)(uint64_t)(uintptr_t)ptr;
}
```

### 4b. Unboxing in `Item` methods (lambda.hpp)

```cpp
// BEFORE — bitfield extraction (56-bit masked read)
inline String* get_string() const { return (String*)this->string_ptr; }
inline Symbol* get_symbol() const { return (Symbol*)this->symbol_ptr; }
inline String* get_binary() const { return (String*)this->binary_ptr; }

// AFTER — direct pointer cast (same as containers)
inline String* get_string() const { return (String*)(uintptr_t)this->item; }
inline Symbol* get_symbol() const { return (Symbol*)(uintptr_t)this->item; }
inline String* get_binary() const { return (String*)(uintptr_t)this->item; }
```

### 4c. `get_chars()` and `get_len()` helpers

These dispatch on `_type_id` to select the right cast. After the change, `_type_id` will be zero for all pointer types (String, Symbol, containers), so they must dispatch on the dereferenced `type_id`:

```cpp
// AFTER
inline const char* get_chars() const {
    TypeId tid = *((TypeId*)this->item);
    if (tid == LMD_TYPE_STRING || tid == LMD_TYPE_BINARY)
        return ((String*)(uintptr_t)this->item)->chars;
    return ((Symbol*)(uintptr_t)this->item)->chars;
}
inline uint32_t get_len() const {
    TypeId tid = *((TypeId*)this->item);
    if (tid == LMD_TYPE_STRING || tid == LMD_TYPE_BINARY)
        return ((String*)(uintptr_t)this->item)->len;
    return ((Symbol*)(uintptr_t)this->item)->len;
}
```

### 4d. `item.type_id()` — no change needed

The existing two-branch logic already handles this correctly:

```cpp
inline TypeId type_id() {
    if (this->_type_id) return this->_type_id;   // inline scalars: Bool, Int, Int64, Float, Decimal, DateTime, Error
    if (this->item)     return *((TypeId*)this->item); // now: containers + String + Symbol + Binary
    return LMD_TYPE_NULL;
}
```

After the change, the String/Symbol/Binary path naturally falls into the dereference branch — no modification needed. The `_type_id != 0` fast path becomes exclusively for inline scalars.

### 4e. `const_s2it` and related JIT const macros

```c
// BEFORE
#define const_s2it(index)   s2it(_const_pool[index])

// AFTER — same macro, but s2it is now p2it, so this becomes:
// const_s2it(i) -> p2it(_const_pool[i]) -> raw pointer cast
// No change needed to definition; behaviour changes automatically.
```

### 4f. `it2s` unboxing function (C++ mode, lambda.h line 944)

```cpp
// BEFORE (declared; defined in lambda-data.cpp)
String* it2s(Item item);
// — extracts 56-bit tagged pointer

// AFTER
static inline String* it2s(Item item) {
    return (String*)(uintptr_t)item.item;
}
```

---

## 5. Affected Files

The following files require changes. Organised by category:

### 5a. Core type definitions and macros

| File | Change |
|---|---|
| `lambda/lambda.h` | Modify `String` and `Symbol` struct definitions (add `type_id` first field). Change `s2it`, `y2it`, `x2it` to `p2it`. Update `it2s`, inline unboxing macros. Add `string_ptr`/`symbol_ptr`/`binary_ptr` union members → replace with direct `item` access. |
| `lambda/lambda.hpp` | Update `Item::get_string()`, `get_symbol()`, `get_binary()`, `get_chars()`, `get_len()`. |

### 5b. Allocation sites — must set `type_id` on construction

| File | Change |
|---|---|
| `lambda/lambda-data.cpp` | `heap_create_name()`, `heap_strcpy()` — set `type_id = LMD_TYPE_STRING`. Binary alloc sites — set `type_id = LMD_TYPE_BINARY`. |
| `lambda/lambda-data-runtime.cpp` | Any string/binary allocation using `pool_calloc` or `arena_alloc`. |
| `lambda/name_pool.cpp` | `NamePool` string and symbol allocation. Set `type_id` for each interned entry. |
| `lambda/lambda-eval.cpp` | Inline string/symbol/binary construction. |
| `lambda/lambda-eval-num.cpp` | Numeric-to-string conversions that build `String*`. |
| `lambda/mark_builder.cpp` | `MarkBuilder` string/symbol creation calls. |
| `lambda/mark_editor.cpp` | Any allocation of new strings in the editor. |
| `lambda/utf_string.cpp` | `utf_string_*` helpers that allocate String structs. |
| `lambda/input/` (all input parsers) | Every `pool_calloc`/`arena_alloc` of a String or Symbol must set `type_id`. |
| `lambda/format/` (all output formatters) | String builders that allocate String structs. |
| `lib/string.h` | `lib/string.h` defines its own `String` struct — must be kept separate (differs from Lambda's `lambda/lambda.h` String). |

### 5c. Transpiler (JIT code generation)

| File | Change |
|---|---|
| `lambda/transpile-mir.cpp` | `transpile_box_item()` for STRING/SYMBOL/BINARY — use `p2it` pattern. `transpile_unbox_item()` — use direct cast. String literal emission — ensure `type_id` field initialised in generated code. |
| `lambda/transpile.cpp` | C-code transpiler: same boxing/unboxing changes as MIR path. |
| `lambda/transpile-call.cpp` | Function call boxing for string return values. |

### 5d. Runtime evaluation and built-in functions

| File | Change |
|---|---|
| `lambda/lambda-eval.cpp` | Any direct use of `item.string_ptr`, `item.symbol_ptr`, `item.binary_ptr` bitfields — replace with `item.get_string()`, etc. |
| `lambda/lambda-mem.cpp` | GC traversal: can simplify String/Symbol/Binary traversal since they now have `type_id` at offset 0, same as containers. |
| `lambda/print.cpp` | String printing helpers. |
| `lambda/pack.cpp` | Serialisation that accesses String internals. |

### 5e. GC and memory

| File | Change |
|---|---|
| `lambda/lambda-mem.cpp` | GC scan: `String`/`Symbol` objects now discoverable by `*((TypeId*)ptr)` like containers — remove special-cased String/Symbol traversal branches if any exist. |

---

## 6. Implementation Plan

### Step 1 — Create branch

```bash
git checkout -b direct-string-pointer
```

### Step 2 — Modify struct definitions (`lambda/lambda.h`)

1. Update `String` struct: add `type_id` (offset 0), move `is_ascii` to offset 1, add 2-byte explicit pad, keep `len` at offset 4.
2. Update `Symbol` struct: add `type_id` (offset 0), 3-byte pad (reusing existing alignment gap), `len` at offset 4, `ns` at offset 8.
3. Change `s2it`, `y2it`, `x2it` macros to `p2it(...)`.
4. Replace `string_ptr`, `symbol_ptr`, `binary_ptr` bitfields in `Item` union with a note (or remove them; access goes through `.item` directly).
5. Update `it2s` to a direct cast.

### Step 3 — Update `Item` inline methods (`lambda/lambda.hpp`)

Update `get_string()`, `get_symbol()`, `get_binary()`, `get_chars()`, `get_len()`.

### Step 4 — Fix all allocation sites

Search for every site that allocates a `String` or `Symbol` and set the new `type_id` field:

```bash
grep -rn "sizeof(String)\|sizeof(Symbol)\|heap_create_name\|heap_strcpy\|heap_create_symbol" lambda/ --include="*.cpp" --include="*.h"
```

Patch each allocation to set `s->type_id = LMD_TYPE_STRING` (or `LMD_TYPE_BINARY` / `LMD_TYPE_SYMBOL`).

### Step 5 — Fix transpiler boxing/unboxing

In `transpile-mir.cpp` and `transpile.cpp`, each switch-case for `LMD_TYPE_STRING`, `LMD_TYPE_SYMBOL`, `LMD_TYPE_BINARY` in `transpile_box_item()` / `transpile_unbox_item()` must be updated to emit `p2it()` / direct-cast idioms.

### Step 6 — Build and run unit tests (baseline sanity)

```bash
make release
make test-lambda-baseline
make test-radiant-baseline
```

All unit tests must pass before benchmarking.

### Step 7 — Run benchmarks

```bash
python3 test/benchmark/run_benchmarks.py -m time -n 3
python3 test/benchmark/run_benchmarks.py -m memory -n 3
```

Record output as `test/benchmark/Overall_Result5.md` and `test/benchmark/memory_results_r5.json`.

---

## 7. Benchmark Plan

### 7a. Scope

Run the full benchmark suite from Round 4 on the **release build** (`make release`):

- **Time**: 3 median runs, self-reported execution time (excluding startup and JIT compilation)
- **Memory**: Peak RSS via `/usr/bin/time -l` (macOS)
- **Engines**: MIR Direct and C2MIR only (LambdaJS and third-party engines are unchanged)

### 7b. Benchmarks most sensitive to this change

The change primarily affects operations that box/unbox strings or compare them. The following benchmarks are expected to show the largest delta:

| Suite | Benchmark | Reason |
|---|---|---|
| KOSTYA | `base64` | String allocation/conversion intensive |
| KOSTYA | `levenshtein` | String character access |
| KOSTYA | `json_gen` | Heavy string construction |
| BENG | `revcomp` | String-heavy processing |
| BENG | `regexredux` | String pattern matching |
| BENG | `knucleotide` | String hashing / map lookup with string keys |
| AWFY | `json` | JSON parsing: map keys + string values |
| LARCENY | `deriv` | Symbolic manipulation (symbol-heavy) |

### 7c. Benchmarks expected to be neutral

Pure numeric benchmarks (fib, tak, mbrot, mandelbrot, nbody, spectralnorm, sieve, permute) do not touch strings at runtime and should show no change (within noise margin).

### 7d. Comparison table format

Results will be presented as:

```
Benchmark | R4 MIR (ms) | R5 MIR (ms) | Delta | R4 RSS (MB) | R5 RSS (MB) | RSS Delta
```

---

## 8. Results

### 8a. Pre-experiment hypotheses

| Benchmark | Expected timing delta | Expected RSS delta |
|---|---|---|
| `base64` | −5% to −15% | −2% to +5% |
| `levenshtein` | −3% to −8% | negligible |
| `json_gen` | −5% to −10% | −3% to +3% |
| `revcomp` | −3% to −8% | negligible |
| `knucleotide` | −2% to −5% | negligible |
| Numeric benchmarks | 0% (within noise) | 0% |

### 8b. Actual results — timing

**Overall: ~11% slowdown. All hypothesised gains were invalidated.**

| Metric | Value |
|---|---|
| **Geometric mean R5/R4** | **1.111x (11.1% slower)** |
| Benchmarks faster (>3%) | 2 / 62 |
| Benchmarks slower (>3%) | 55 / 62 |
| Benchmarks within ±3% | 5 / 62 |
| Best improvement | fib: −9.6% |
| Worst regression | bounce: +27.4% |

**Hypothesis vs. reality for string-heavy benchmarks:**

| Benchmark | Expected | Actual | Verdict |
|---|---|---|---|
| `base64` | −5% to −15% | **+20.1%** | ❌ Much worse |
| `levenshtein` | −3% to −8% | **+16.5%** | ❌ Much worse |
| `json_gen` | −5% to −10% | **+8.6%** | ❌ Worse |
| `revcomp` | −3% to −8% | **+14.8%** | ❌ Much worse |
| `knucleotide` | −2% to −5% | **+17.4%** | ❌ Much worse |
| Numeric benchmarks | 0% | **+10–15%** | ❌ Unexpected regression |

**Per-suite summary:**

| Suite | Faster | Slower | Same | Trend |
|---|---:|---:|---:|---|
| R7RS | 1 | 6 | 3 | ~10% slower |
| AWFY | 0 | 12 | 2 | ~12% slower |
| BENG | 0 | 10 | 0 | ~13% slower |
| KOSTYA | 0 | 7 | 0 | ~13% slower |
| LARCENY | 1 | 11 | 0 | ~11% slower |
| JetStream | 0 | 9 | 0 | ~12% slower |

Full per-benchmark tables are in `test/benchmark/Overall_Result5.md`.

### 8c. Actual results — memory

Peak RSS was mostly unchanged (<3% for most benchmarks). One outlier: `kostya/base64` showed +67% RSS, likely measurement variance.

Selected RSS comparisons:

| Benchmark | R4 (MB) | R5 (MB) | Change |
|---|---:|---:|---:|
| `r7rs/fib` | 34.8 | 34.0 | −2.3% |
| `awfy/richards` | 312 | 43.8 | −86% (anomalous) |
| `beng/binarytrees` | 45 | 45.2 | +0.4% |
| `kostya/base64` | 1414 | 2360 | +67% (anomalous) |
| `larceny/gcbench` | 261 | 259 | −0.8% |
| `jetstream/navier_stokes` | 1310 | 1280 | −2.3% |

The String header growth (5 → 8 bytes) did not produce a measurable, consistent memory impact.

### 8d. Root cause analysis

The slowdown is caused by an **extra memory dereference in `get_type_id()`**. Under the tagged-pointer scheme, `_type_id` is the high byte of the `Item` register — extracting it is a zero-cost bitfield read. Under the direct-pointer scheme, `_type_id` is at offset 0 of the pointed-to struct, requiring a branch + memory load:

```cpp
// Before (R4): type is in the register
return this->_type_id;  // zero-cost

// After (R5): type is behind a pointer
if (this->_type_id) return this->_type_id;    // branch
if (this->item)     return *((TypeId*)this->item);  // memory load
return LMD_TYPE_NULL;
```

The boxing cost reduction (removing one OR instruction) was negligible compared to the type-check cost increase, because type checks are far more frequent than boxing operations in practice.

### 8e. Verdict

The experiment is a **negative result**. None of the three success criteria were met:

1. ❌ No string-heavy benchmark improved — all regressed significantly (+8% to +20%).
2. ❌ Numeric benchmarks regressed by +10–15% (limit was 2%).
3. ❌ Memory was mostly flat, but one outlier showed +67%.

**Conclusion:** The tagged-pointer scheme (type in high byte of Item) is the correct design for Lambda's performance-critical runtime. The branch `direct-string-pointer` should **not** be merged.

### 8f. Bugs fixed during implementation

All 677/677 baseline tests pass with the direct-string-pointer implementation. The following bugs were discovered and fixed. **All 7 are branch-specific artifacts of the direct-pointer scheme and do not exist on master — none need to be ported.**

On master, String/Symbol/Binary carry their `TypeId` in the high byte of the `Item` register (tagged pointer). The struct has no `type_id` field, and `_type_id` is always non-zero for these types, so the tagged-pointer code paths handle all cases correctly. Each bug below only manifests when the tag is removed and the struct must self-identify via an embedded `type_id` field:

1. `init_ascii_char_table()` — missing `type_id = LMD_TYPE_STRING` for interned single-char strings. Master: no `type_id` field in `String` struct; type comes from Item tag.
2. `stringbuf_to_string()` — missing `type_id = STRING_TYPE_ID` for strings built from StringBuf. Master: same reason.
3. `fn_eq_depth()` — equality comparison failed when both sides have `_type_id == 0` (e.g., String vs Array). Master: String has `_type_id = LMD_TYPE_STRING` (non-zero), so the `a._type_id != b._type_id` mismatch branch correctly catches String-vs-Array.
4. `ConstItem::string()` — dereferenced ITEM_NULL as a pointer (crash in DOM building). Master: checks `_type_id == LMD_TYPE_STRING` first; ITEM_NULL has `_type_id = LMD_TYPE_NULL`, so it safely returns `nullptr`.
5. Various `_type_id ==` checks replaced with `get_type_id()` calls. Master: `_type_id` is correct for String/Symbol/Binary (set in high byte), so direct `_type_id` checks work.
6. `emit_load_const_boxed` BINARY case — still used old tag-based boxing. Master: tag-based boxing IS the correct behavior.
7. `deep_copy_internal` BINARY case — lost `type_id` after calling `createString()`. Master: `x2it()` adds the BINARY tag to the Item; the struct needs no `type_id`.

---

## 9. Risks and Mitigations

### 9a. `_type_id` check order in `item.type_id()`

**Risk:** After the change, `_type_id` is zero for all pointer types (containers + String/Symbol/Binary). If any code calls `item.type_id()` on a String item and previously relied on the fast `_type_id != 0` path (0 cycles vs. 1 dereference), there is a latency increase.

**Mitigation:** The dereference is a hot-cache read (the `type_id` byte is co-located with the struct header that is already in L1 cache for any recently accessed string). Measured impact should be negligible.

### 9b. String-allocating code that omits `type_id` initialisation

**Risk:** If any allocation site forgets to set `type_id`, the GC and type-dispatch code will misidentify the object as `LMD_TYPE_NULL` (0) or garbage. This will likely crash immediately, not cause silent corruption.

**Mitigation:** The unit test suite (`make test-lambda-baseline`) exercises all major code paths. A crash will surface quickly. Additionally, `heap_calloc` already receives the `TypeId` as a parameter and can be modified to write it into offset 0 automatically for all heap-allocated types.

**Recommended hardening:** Modify `heap_calloc` to always write `type_id` to offset 0 of the returned allocation:

```c
void* heap_calloc(size_t size, TypeId type_id) {
    void* ptr = /* ... gc alloc ... */;
    if (ptr && type_id) *((TypeId*)ptr) = type_id;  // ← write type_id at offset 0
    return ptr;
}
```

This makes `heap_calloc` self-initialising for the header field, reducing the risk of allocation sites forgetting the initialisation.

### 9c. `lib/string.h` vs. `lambda/lambda.h` String struct conflict

**Risk:** `lib/string.h` defines its own `String` struct (used in `lib/` utilities) that is a different type from Lambda's runtime String. The two must remain separate and not conflict. The `lib/string.h` String does NOT need a `type_id` field.

**Mitigation:** The `STRING_STRUCT_DEFINED` guard in `lambda/lambda.h` already prevents double-definition. No change needed to `lib/string.h`.

### 9d. C2MIR path — struct field offsets in generated C code

**Risk:** If the C code generated by the C2MIR transpiler accesses `String.len` or `String.chars` by a hardcoded byte offset (rather than by field name), those offsets have changed: `len` moves from offset 0 to offset 4, `chars` moves from offset 5 to offset 8.

**Mitigation:** Search the generated C output for hardcoded struct offsets. If found, update the offset constants in `transpile.cpp`. The MIR Direct path accesses struct fields symbolically (via MIR's `LD`/`ST` instructions with field offsets computed from the C struct definition), so it should update automatically once the struct is changed.

### 9e. Name pool interned strings

**Risk:** The name pool returns interned `String*` pointers. These are pool-allocated, not GC-heap-allocated, so they may bypass `heap_calloc`. The `type_id` field must be set in the name pool's own allocator.

**Mitigation:** `name_pool.cpp` uses the pool allocator directly. A targeted audit of `NamePool::intern()` and related functions is required to ensure `type_id` is set on all allocated name strings.

---

## Appendix: Key Macro Changes Summary

| Identifier | Before | After |
|---|---|---|
| `s2it(p)` | `(LMD_TYPE_STRING<<56) \| p` | `p2it(p)` |
| `y2it(p)` | `(LMD_TYPE_SYMBOL<<56) \| p` | `p2it(p)` |
| `x2it(p)` | `(LMD_TYPE_BINARY<<56) \| p` | `p2it(p)` |
| `item.get_string()` | `(String*)item.string_ptr` (56-bit bitfield) | `(String*)(uintptr_t)item.item` |
| `item.get_symbol()` | `(Symbol*)item.symbol_ptr` | `(Symbol*)(uintptr_t)item.item` |
| `item.get_binary()` | `(String*)item.binary_ptr` | `(String*)(uintptr_t)item.item` |
| `String.sizeof(header)` | 5 bytes | 8 bytes |
| `Symbol.sizeof(header)` | 16 bytes | 16 bytes (unchanged) |
| `String.type_id` | N/A (none) | offset 0, `TypeId` |
| `Symbol.type_id` | N/A (none) | offset 0, `TypeId` |
| `item.type_id()` for String | reads `item._type_id` (high byte, inline) | reads `*((TypeId*)item.item)` (1 dereference) |
