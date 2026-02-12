# Proposal: Robust Error Handling in Lambda System Functions

## Status: Phases 1–2 Implemented ✅ | Phase 3–4 Deferred
## Date: 2026-02-12

---

## Implementation Status

### Phase 1: Immediate Safety Fixes — ✅ DONE (16/16 items)

| Fix | Status | Files Modified |
|-----|--------|----------------|
| `fn_string()` → `&STR_ERROR` instead of NULL | ✅ | `lambda-eval.cpp` |
| `is_truthy(error)` → `BOOL_FALSE` (errors are falsy) | ✅ | `lambda-data.cpp` |
| `GUARD_ERROR` on ~74 Item-returning functions | ✅ | `lambda-eval.cpp`, `lambda-eval-num.cpp`, `lambda-vector.cpp` |
| `fn_min2()` copy-paste typo fix | ✅ | `lambda-eval-num.cpp` |
| `fn_index_of` / `fn_last_index_of` → `INT64_ERROR` | ✅ | `lambda-eval.cpp` |
| `it2d()` → `NaN` instead of `0.0` | ✅ | `lambda-data.cpp` |
| `it2b()` → `false` for errors | ✅ | `lambda-data.cpp` |
| `it2i()` → `0` for errors | ✅ | `lambda-data.cpp` |
| `it2s()` → `"<error>"` instead of `nullptr` | ✅ | `lambda-data.cpp` |
| `fn_to_cstr()` → `""` for errors | ✅ | `lambda-data.cpp` |
| `fn_strcat()` → `&STR_ERROR` instead of NULL | ✅ | `lambda-eval.cpp` |
| `DATETIME_ERROR_VALUE` sentinel (all-ones) | ✅ | `lambda.h` |
| `DATETIME_IS_ERROR` / `DATETIME_MAKE_ERROR` macros | ✅ | `lambda.h` |
| `push_k()` error sentinel detection | ✅ | `lambda-mem.cpp` |
| `GUARD_ERROR1/2/3`, `GUARD_BOOL_ERROR1/2`, `GUARD_DATETIME_ERROR1/2/3` macros | ✅ | `lambda.hpp` |
| `fn_format2()` error check → `&STR_ERROR` | ✅ | `lambda-eval.cpp` |

### Phase 2: DateTime Validation — ✅ DONE (5/5 items)

| Fix | Status | Details |
|-----|--------|---------|
| `fn_date3()` month 1–12, day 1–31 validation | ✅ | Returns `DATETIME_ERROR_VALUE` on invalid |
| `fn_time3()` hour 0–23, minute 0–59, second 0–59 validation | ✅ | Returns `DATETIME_ERROR_VALUE` on invalid |
| `fn_datetime1()` error input propagation | ✅ | `DATETIME_MAKE_ERROR` for error Items |
| `fn_date1()` error input propagation | ✅ | `DATETIME_MAKE_ERROR` for error Items |
| `fn_time1()` error input propagation | ✅ | `DATETIME_MAKE_ERROR` for error Items |

### Language Feature: `^expr` Operator & Error Truthiness — ✅ DONE

| Feature | Status | Files Modified |
|---------|--------|----------------|
| `^expr` prefix operator (shorthand for `expr is error`) | ✅ | `grammar.js`, `build_ast.cpp`, `transpile.cpp`, `transpile-mir.cpp` |
| Errors are falsy (enables `err or default` idiom) | ✅ | `lambda-data.cpp` |
| Documentation updated | ✅ | `doc/Lambda_Error_Handling.md` |

### Testing — ✅ 220/220 Lambda, 1972/1972 Radiant

- `test/lambda/error_propagation.ls` — 50 test cases covering `^expr`, error truthiness, arithmetic/numeric/math/string/vector propagation, `?` operator, chained errors

### Phase 3: Result Types — ⏸️ DEFERRED

The `StringResult`, `DateTimeResult`, etc. structs are **not implemented**. Phase 1 fixes (static `STR_ERROR`, `DATETIME_ERROR_VALUE` sentinel, `GUARD_ERROR` macros) eliminated the urgent crash/corruption issues, making Phase 3 a lower priority structural improvement.

### Phase 4: Transpiler Auto-Error-Check — ⏸️ DEFERRED

Marked "Optional, Future" — not implemented.

### Tiered Macros (`RETURN_ERROR`, `FATAL_ERROR`) — ⏸️ NOT DEFINED

The convenience macros are not defined. Equivalent `log_error()` + `return ItemError` patterns are used inline throughout the codebase.

---

## Assessment: Lambda vs. CPython Error Handling (Without Phase 3)

Even without Result types (Phase 3), Lambda's C runtime error handling is **structurally superior** to CPython's. The key difference: CPython errors are invisible and crashable; Lambda errors are in-band and auto-propagating.

### Direct Comparison

| Issue | CPython | Lambda (current) |
|-------|---------|-------------------|
| NULL pointer returns from C builtins | Common — every caller must check | **Eliminated** — `&STR_ERROR` is never NULL |
| Forgetting to check causes crash? | Yes → NULL deref, hundreds of CVEs | No — sentinels are safe, processable values |
| Error side-channel state | Thread-local `PyErr` — must check AND clear | **None** — errors are purely in-band |
| Error silently becomes valid data | `-1` return ambiguous with valid `-1` | NaN auto-propagates; `DATETIME_ERROR_VALUE` caught by `push_k()` |
| C-level compiler enforcement | ❌ None | ❌ None (same limitation) |
| Language-level compiler enforcement | ❌ None | ✅ `T^E`, `?`, `let a^err` — compiler-enforced |
| Error propagation through call chain | Manual — every caller must check + forward `PyErr` | **Automatic** — `GUARD_ERROR` macros, IEEE 754 NaN propagation |
| Error truthiness | N/A (exceptions, not values) | Errors are **falsy** — enables `err or default` idiom |

### Why CPython's Model Is Fundamentally Fragile

CPython requires **every C-extension caller to do two things right**:
1. Check if the return value is NULL / -1
2. Check `PyErr_Occurred()` and either handle or propagate the error

Forget either step → bug. Forget to *clear* `PyErr` → phantom errors in unrelated code. This two-step protocol is the root cause of hundreds of CVEs in CPython and its ecosystem.

### Why Lambda's Model Is Structurally Safe

Lambda has **no two-step check**. Error handling is structural at every type boundary:

- **Item functions**: `GUARD_ERROR` at entry → errors auto-propagate without any action by the caller.
- **String functions**: Return `&STR_ERROR` (never NULL) → `s2it()` boxes it → becomes a string containing `"<error>"` at the Item boundary. No crash possible.
- **DateTime functions**: Return `DATETIME_ERROR_VALUE` → `push_k()` detects the sentinel → returns `ItemError`. No corruption possible.
- **Float unboxing**: `it2d()` returns NaN → IEEE 754 propagates NaN through all downstream arithmetic for free.
- **Language level**: `T^E` return types, `?` operator, and `let a^err` destructuring give **compiler-enforced** error handling that CPython and Go lack entirely.

### Where Phase 3 Would Provide Additional Safety

The remaining CPython-like gap: if someone writes a **new** C function that calls `fn_string()` and processes the `String*` result without knowing `&STR_ERROR` is special, they'd treat `"<error>"` as a real string. With `StringResult`, the struct would force the caller to unpack — similar to V8's `MaybeLocal<T>`.

This gap is **minor** in practice because:
1. All existing callers are already fixed
2. `"<error>"` is a distinctive sentinel — hard to miss in output
3. New C functions are rare — the primary development surface is the Lambda language itself
4. The real enforcement happens at the language level (`T^E`), which neither CPython nor Go have

### Positioning

```
CPython (NULL + PyErr)     Lambda (sentinels + GUARD_ERROR)        Rust (Result<T,E>)
─────────────────────── ──→ ─────────────────────────────────── ──→ ──────────────────
  crashable, two-step         safe, auto-propagating                compiler-enforced
  no language enforcement     language-level T^E enforcement        language-level enforcement
```

Lambda occupies a **strong middle ground**: safer than CPython/Go at the C runtime level, with language-level enforcement that matches Rust's philosophy. Phase 3 would close the remaining gap toward Rust-level C-API safety, but the crash and corruption risks that motivated this work are already eliminated.

---

## Table of Contents

1. [Industry Prior Art](#industry-prior-art)
2. [Problem Statement](#problem-statement)
3. [Audit: Current Error Handling Issues](#audit-current-error-handling-issues)
   - [Category A: Functions Returning Native Types (No Error Channel)](#category-a-functions-returning-native-types-no-error-channel)
   - [Category B: Functions Returning NULL Instead of Error](#category-b-functions-returning-null-instead-of-error)
   - [Category C: Silent Error Swallowing in Arithmetic/String Functions](#category-c-silent-error-swallowing)
   - [Category D: Unboxing Functions That Mask Errors](#category-d-unboxing-functions-that-mask-errors)
   - [Category E: Transpiler Boxing Converts NULL to Undefined](#category-e-transpiler-boxing-converts-null-to-undefined)
4. [Root Cause Analysis](#root-cause-analysis)
5. [Proposed Solution: Result Types for Native Returns](#proposed-solution-result-types-for-native-returns)
6. [Proposed Solution: Error-Early-Return Guards for Item Functions](#proposed-solution-error-early-return-guards)
7. [Proposed Solution: Static Error Strings](#proposed-solution-static-error-strings)
8. [Proposed Solution: Poison NaN for Float Errors](#proposed-solution-poison-nan-for-float-errors)
9. [Proposed Solution: DateTime Error Sentinel](#proposed-solution-datetime-error-sentinel)
10. [Proposed Solution: Tiered Error Severity](#proposed-solution-tiered-error-severity)
11. [Implementation Plan](#implementation-plan)
12. [Migration Strategy](#migration-strategy)
13. [Alternatives Considered](#alternatives-considered)

---

## Industry Prior Art

How other language runtimes handle errors in their C/C++ built-in functions — the exact layer where Lambda currently has gaps.

### Comparison Matrix

| Aspect | **CPython** | **V8 (JS)** | **Lua** | **Rust std** | **Lambda (current)** | **Lambda (proposed)** |
|--------|------------|-------------|---------|-------------|---------------------|----------------------|
| Error signal (pointer returns) | Return `NULL` + set thread-local `PyErr` | Return `MaybeLocal<T>` (empty = error) | Return error code + `lua_error()` | `Result<T, E>` enum | Return `NULL` (no side-channel) | `StringResult { value, error }` |
| Error signal (scalar returns) | Return `-1` + set thread-local `PyErr` | `Maybe<T>` with `.IsNothing()` | Return error code | `Result<T, E>` enum | Ad-hoc (0, INT64_MAX, all-zeros) | `DateTimeResult { value, error }` |
| Error context preserved? | ✅ Full traceback in `PyErr` | ✅ Exception object on isolate | ❌ Just an error string | ✅ Full `E` type | ❌ Lost (NULL carries no info) | ✅ Original error `Item` preserved |
| Forgetting to check? | 🐛 Common source of bugs | Compiler warns (`.ToLocalChecked()` crashes in debug) | Runtime crash | Compiler error (`#[must_use]`) | 🐛 Silent corruption/crash | Struct forces unpacking |

### V8's `MaybeLocal<T>` — Validates the Result Type Approach

V8 (the JavaScript engine) faced the **exact same problem** Lambda has. Their solution was to wrap every fallible return in `MaybeLocal<T>`:

```cpp
// V8 pattern — can't use result without checking
MaybeLocal<String> maybe_str = obj->ToString(context);
Local<String> str;
if (!maybe_str.ToLocal(&str)) {
    return;  // error pending on isolate
}
// safe to use str here
```

This is structurally identical to the proposed `StringResult`. The key insight: **the type system forces callers to acknowledge the possibility of failure**. V8 also provides `.ToLocalChecked()` which crashes in debug builds if empty — useful for catching missed checks during development.

**Lesson adopted:** Add a debug-mode assertion macro (see [Result Types](#proposed-solution-result-types-for-native-returns)):
```c
#define STRING_UNWRAP_CHECKED(r) \
    (assert(!RESULT_IS_ERROR(r) && "unchecked error result"), (r).value)
```

### CPython's `NULL + PyErr_SetString()` — The Cautionary Tale

CPython uses exactly what Lambda currently does: return NULL for errors, with error details in a side channel. This is **widely acknowledged as CPython's biggest source of C-extension bugs**:

- Forgetting to check `PyErr_Occurred()` after a NULL return → use-after-null
- Forgetting to **clear** the error flag → phantom errors in unrelated code
- Return -1 for int functions is indistinguishable from legitimate -1 values

CPython has had hundreds of CVEs from this pattern. The CPython core team has repeatedly discussed migrating to a Result-like pattern but the API surface is too large.

**Lesson adopted:** Lambda's codebase is still small enough to fix this structurally. Don't accumulate the technical debt CPython can't escape.

### Lua's Protected Calls — Defense in Depth

Lua takes a different approach: C functions signal errors by calling `lua_error()`, which performs a `longjmp` back to the nearest protected call point. This means **errors cannot be ignored** — they forcefully unwind.

Lambda can't use longjmp (it would skip C++ destructors and ref-count cleanup), but the principle of **making error ignoring impossible** is valuable. The Result struct achieves this: you can't access `.value` without dealing with the struct.

### Rust's Zero-Cost `Result<T, E>` — The Gold Standard

Rust's `Result<T, E>` is the cleanest solution: the compiler refuses to let you use the inner value without matching on the enum. Lambda's language-level `T^` already mirrors this. The gap is that the **C runtime** doesn't enforce it.

**Lesson adopted:** The proposed `RESULT_IS_ERROR` check before accessing `.value` is the C equivalent of Rust's `match` or `.unwrap()`.

### Go's `(value, err)` Tuple — What NOT to Do

Go's `(value, err)` tuple is easy to ignore with `value, _ := f()`. Lambda's compiler already enforces error handling at the language level (unlike Go). But at the C runtime level, Lambda currently has Go's exact problem: callers can silently ignore NULL returns.

### JavaScript's `NaN` Propagation — Adopted for Floats

JavaScript uses `NaN` as a poison value that propagates through arithmetic: `NaN + 5 = NaN`, `NaN > 0 = false`. This auto-propagation means errors can never silently become valid numbers. Lambda's `it2d()` currently returns `0.0` for errors — **worse** than NaN because it silently becomes a valid number.

**Lesson adopted:** See [Poison NaN for Float Errors](#proposed-solution-poison-nan-for-float-errors).

### POSIX `time_t` / `mktime()` — Adopted for DateTime

POSIX uses `(time_t)-1` as an error sentinel for `time()`, `mktime()`, etc. Lambda's DateTime (64-bit bitfield) currently uses all-zeros as an error value, which encodes to year=-4000, month=0 — a technically valid bitpattern that slips through undetected.

**Lesson adopted:** See [DateTime Error Sentinel](#proposed-solution-datetime-error-sentinel).

### Node.js Error Classification — Adopted for Tiered Severity

Node.js classifies errors into three tiers with different handling strategies:
- **Programmer errors** (wrong arg types): throw immediately
- **Operational errors** (file not found): return via callback/Promise error
- **Fatal errors** (out of memory): abort

**Lesson adopted:** See [Tiered Error Severity](#proposed-solution-tiered-error-severity).

---

## Problem Statement

Lambda's language-level error handling (`T^`, `raise`, `?`, `let a^err`) is well-designed and compiler-enforced. However, the **C/C++ runtime implementation** has systematic gaps where errors are silently lost, converted to garbage values, or cause NULL pointer dereferences.

The core tension is:

> **Functions returning `Item` can encode errors via `ITEM_ERROR`. Functions returning native C types (`String*`, `DateTime`, `int64_t`, `Bool`) have no uniform error channel.**

This creates two classes of bugs:
1. **Silent data corruption**: Error datetime values become valid-looking `DTIME` Items with year=-4000
2. **NULL dereference crashes**: `fn_string()` returns NULL for error Items, and callers don't check

---

## Audit: Current Error Handling Issues

### Category A: Functions Returning Native Types (No Error Channel)

These functions return C types that have no in-band error representation:

| Function | Return Type | Error Behavior | Severity | File |
|----------|-------------|----------------|----------|------|
| `fn_string()` | `String*` | Returns `NULL` for `LMD_TYPE_ERROR` | 🔴 CRASH | `lambda-eval.cpp:1176` |
| `fn_format1/2()` | `String*` | Returns `NULL` when input is error | 🔴 CRASH | `lambda-eval.cpp` |
| `fn_strcat()` | `String*` | Returns `NULL` if either arg is NULL | 🟠 HIGH | `lambda-eval.cpp` |
| `fn_symbol1()` | `Symbol*` | Returns `NULL` for error items | 🔴 CRASH | `lambda-data-runtime.cpp` |
| `fn_datetime1()` | `DateTime` | Returns all-zeros struct (year=-4000) | 🔴 SILENT CORRUPTION | `lambda-eval.cpp:2564` |
| `fn_date3()` | `DateTime` | No input validation; invalid dates pass through | 🔴 SILENT CORRUPTION | `lambda-eval.cpp:2645` |
| `fn_date1()` | `DateTime` | Returns all-zeros struct on failure | 🟠 HIGH | `lambda-eval.cpp:2616` |
| `fn_time3()` | `DateTime` | No input validation; invalid times pass through | 🟠 HIGH | `lambda-eval.cpp` |
| `fn_time1()` | `DateTime` | Returns all-zeros struct on failure | 🟠 HIGH | `lambda-eval.cpp` |
| `it2s()` | `String*` | Returns `nullptr` for non-string types | 🔴 CRASH | `lambda-data-runtime.cpp` |
| `it2d()` | `double` | Returns `0.0` for error items | 🟡 MEDIUM | `lambda-data-runtime.cpp` |
| `it2l()` | `int64_t` | Returns `INT64_ERROR` sentinel | 🟢 OK-ish | `lambda-data-runtime.cpp` |
| `is_truthy()` | `Bool` | Error items evaluate as **truthy** | 🟠 HIGH | `lambda-data-runtime.cpp` |
| `fn_index_of()` | `int64_t` | Returns -1 for errors (same as "not found") | 🟠 HIGH | `lambda-data-runtime.cpp` |
| `fn_last_index_of()` | `int64_t` | Returns -1 for errors (same as "not found") | 🟠 HIGH | `lambda-data-runtime.cpp` |

### Category B: Functions Returning NULL Instead of Error

The `fn_string()` issue cascades through the entire runtime:

**Crash chain example — the `++` (string concat) operator:**
```
user writes:   error("bad") ++ "hello"
runtime:       fn_strcat(fn_string(error_item), fn_string(str_item))
               fn_strcat(NULL, "hello")
               → logs error, returns NULL
               → s2it(NULL) → returns 0 (ITEM_UNDEFINED)
```

The error is silently converted to `ITEM_UNDEFINED` (0) — neither `ITEM_NULL` nor `ITEM_ERROR`. The user's error value **vanishes**.

**Crash chain example — symbol concatenation:**
```
user writes:   'prefix ++ error("bad")
runtime:       fn_strcat(fn_string(symbol), fn_string(error_item))
               fn_strcat(String*, NULL)
               → logs error, returns NULL
               → s2it(NULL) → returns 0 (ITEM_UNDEFINED)
```

### Category C: Silent Error Swallowing in Arithmetic/String Functions {#category-c-silent-error-swallowing}

**None** of the following functions check for error Items at entry. They "accidentally" produce `ItemError` by falling through to the default `else` branch with **misleading error messages**:

| Function Group | Functions | Error Behavior | Log Message |
|---------------|-----------|----------------|-------------|
| Arithmetic | `fn_add`, `fn_sub`, `fn_mul`, `fn_div`, `fn_mod`, `fn_idiv`, `fn_pow` | Falls to default → `ItemError` | "unknown add type: 23" |
| Unary math | `fn_abs`, `fn_round`, `fn_floor`, `fn_ceil`, `fn_neg`, `fn_pos` | Falls to default → `ItemError` | "not supported for type" |
| Min/Max | `fn_min1`, `fn_min2`, `fn_max1`, `fn_max2` | Falls to default → `ItemError` | "unsupported type" |
| Aggregation | `fn_sum`, `fn_avg` | Falls to default → `ItemError` | "not supported for type" |
| String ops | `fn_trim`, `fn_lower`, `fn_upper`, `fn_split`, `fn_replace` | Falls to default → `ItemError` | "argument must be a string" |
| String tests | `fn_contains`, `fn_starts_with`, `fn_ends_with` | Falls to default → `BOOL_ERROR` | "arguments must be strings" |

While the **result** is accidentally correct (`ItemError`), the log messages are misleading and make debugging difficult. An explicit error-input check would produce clearer diagnostics and be resilient to future refactoring.

**Additional bug found during audit:**
- `fn_min2()`: When checking the second argument for decimal type, the code checks `item_a._type_id` instead of `item_b._type_id` — a copy-paste typo.

### Category D: Unboxing Functions That Mask Errors

| Function | Input: Error Item | Returns | Problem |
|----------|------------------|---------|---------|
| `it2d()` | `LMD_TYPE_ERROR` | `0.0` | Error silently becomes zero |
| `it2i()` | `LMD_TYPE_ERROR` | `0` | Error silently becomes zero |
| `it2l()` | `LMD_TYPE_ERROR` | `INT64_ERROR` | Correct sentinel, but callers may not check |
| `it2b()` / `is_truthy()` | `LMD_TYPE_ERROR` | `true` | **Error items are truthy** — masking errors in `if` conditions |
| `it2s()` | `LMD_TYPE_ERROR` | `nullptr` | Callers crash on NULL deref |
| `fn_to_cstr()` | `LMD_TYPE_ERROR` | `""` | Error becomes empty string in paths |

The `is_truthy` issue is particularly dangerous: `if (potentially_error_value)` will take the truthy branch, completely masking the error.

### Category E: Transpiler Boxing Converts NULL to Undefined

When the transpiler emits code calling `String*`-returning functions, it boxes with `s2it()`:

```c
#define s2it(str_ptr) ((str_ptr)? ((((uint64_t)LMD_TYPE_STRING)<<56) | (uint64_t)(str_ptr)): null)
```

When `str_ptr` is NULL (error case), `s2it` returns `0` which is `ITEM_UNDEFINED` — **not** `ITEM_NULL` and **not** `ITEM_ERROR`. The error is converted to an undefined value that doesn't match any type check.

Similarly, `k2it()` only checks for NULL pointer, but `push_k()` always allocates on the num_stack, so the pointer is never NULL. An error DateTime (all-zeros struct) passes through as a valid `DTIME` Item.

---

## Root Cause Analysis

The fundamental issue is an **impedance mismatch** between two layers:

| Layer | Error Mechanism | Works? |
|-------|----------------|--------|
| Lambda language | `T^E` return type, `raise`, `?`, `let a^err` | ✅ Well-designed |
| `Item`-returning C functions | Return `ITEM_ERROR` / `ItemError` | ✅ Works |
| **Native-type-returning C functions** | **Ad-hoc**: NULL, all-zeros, sentinels, or nothing | ❌ **Broken** |

Functions that return `Item` have a uniform error channel. Functions that return `String*`, `DateTime`, `Symbol*`, `Bool`, `int64_t`, or `double` do **not**.

The existing ad-hoc approaches:
- **NULL pointer** (`String*`, `Symbol*`): Callers often don't check → crash
- **All-zeros struct** (`DateTime`): Indistinguishable from valid data → silent corruption
- **Sentinel values** (`INT64_ERROR`, `BOOL_ERROR`): Work but inconsistent across types
- **Nothing** (`it2d` returning 0.0): Silent data loss

---

## Proposed Solution: Result Types for Native Returns

### Core Idea

Introduce lightweight result structs for each native return type, pairing the value with an error `Item`:

```c
// Result types for system functions returning native C types
typedef struct {
    String* value;
    Item error;     // ITEM_NULL if success, ITEM_ERROR or error Item if failure
} StringResult;

typedef struct {
    DateTime value;
    Item error;
} DateTimeResult;

typedef struct {
    Symbol* value;
    Item error;
} SymbolResult;

typedef struct {
    int64_t value;
    Item error;
} Int64Result;

typedef struct {
    double value;
    Item error;
} DoubleResult;

typedef struct {
    Bool value;
    Item error;
} BoolResult;
```

### Convenience Macros

```c
// Success constructors
#define STRING_OK(s)     ((StringResult){.value = (s), .error = {.item = ITEM_NULL}})
#define DATETIME_OK(dt)  ((DateTimeResult){.value = (dt), .error = {.item = ITEM_NULL}})
#define INT64_OK(v)      ((Int64Result){.value = (v), .error = {.item = ITEM_NULL}})

// Error constructors
#define STRING_ERR(e)    ((StringResult){.value = NULL, .error = (e)})
#define DATETIME_ERR(e)  ((DateTimeResult){.value = {0}, .error = (e)})
#define INT64_ERR(e)     ((Int64Result){.value = INT64_ERROR, .error = (e)})

// Check macros
#define RESULT_IS_ERROR(r)  (get_type_id((r).error) == LMD_TYPE_ERROR)
#define RESULT_IS_OK(r)     (get_type_id((r).error) != LMD_TYPE_ERROR)
```

### Applied to Key Functions

**Before (current):**
```c
String* fn_string(Item itm) {
    // ...
    case LMD_TYPE_ERROR:
        return NULL;  // 💣 time bomb
}

DateTime fn_date3(Item y, Item m, Item d) {
    // ...no validation...
    return dt;  // 💣 may be invalid
}
```

**After (proposed):**
```c
StringResult fn_string(Item itm) {
    // ...
    case LMD_TYPE_ERROR:
        return STRING_ERR(itm);  // ✅ error preserved and propagated
}

DateTimeResult fn_date3(Item y, Item m, Item d) {
    // validate inputs
    if (get_type_id(y) == LMD_TYPE_ERROR) return DATETIME_ERR(y);
    if (get_type_id(m) == LMD_TYPE_ERROR) return DATETIME_ERR(m);
    if (get_type_id(d) == LMD_TYPE_ERROR) return DATETIME_ERR(d);

    int year = ...; int month = ...; int day_val = ...;

    // validate ranges
    if (month < 1 || month > 12)
        return DATETIME_ERR(make_error("date: month must be 1-12, got %d", month));
    if (day_val < 1 || day_val > 31)
        return DATETIME_ERR(make_error("date: day must be 1-31, got %d", day_val));

    DateTime dt = ...;
    return DATETIME_OK(dt);  // ✅ validated
}
```

### Transpiler Integration

The transpiler must be updated to unwrap result types:

**For `fn_string()` calls (e.g., in `++` operator):**
```c
// Before:
s2it(fn_string(expr))

// After:
({StringResult _sr = fn_string(expr);
  RESULT_IS_ERROR(_sr) ? _sr.error : s2it(_sr.value);})
```

**For datetime function calls:**
```c
// Before:
push_k(fn_date3(y, m, d))

// After:
({DateTimeResult _dr = fn_date3(y, m, d);
  RESULT_IS_ERROR(_dr) ? _dr.error : push_k(_dr.value);})
```

### MIR JIT Compatibility

Result types are simple C structs and should work with MIR's C2MIR compiler. The structs are returned by value (128 bits = two registers on x86-64/ARM64). If MIR has issues with struct returns:

**Fallback option**: Use a thread-local error slot:
```c
// Thread-local last-error slot
extern __thread Item _last_error;
#define HAS_LAST_ERROR() (get_type_id(_last_error) == LMD_TYPE_ERROR)
#define CLEAR_LAST_ERROR() (_last_error.item = ITEM_NULL)

// Functions still return native type, but set _last_error on failure
String* fn_string(Item itm) {
    case LMD_TYPE_ERROR:
        _last_error = itm;
        return &STR_ERROR;  // static sentinel, never NULL
}
```

However, the struct-based approach is **preferred** because:
1. Error state is explicit, not ambient
2. Cannot forget to check (compiler can enforce struct unpacking)
3. No global mutable state / thread-safety issues

---

## Proposed Solution: Error-Early-Return Guards for Item Functions {#proposed-solution-error-early-return-guards}

For functions that already return `Item`, add a simple guard macro at the top:

```c
// Guard macro: if any argument is an error Item, propagate it immediately
#define GUARD_ERROR1(a) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return (a)
#define GUARD_ERROR2(a, b) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return (a); \
    if (get_type_id(b) == LMD_TYPE_ERROR) return (b)
#define GUARD_ERROR3(a, b, c) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return (a); \
    if (get_type_id(b) == LMD_TYPE_ERROR) return (b); \
    if (get_type_id(c) == LMD_TYPE_ERROR) return (c)
```

**Applied to arithmetic functions:**
```c
Item fn_add(Item a, Item b) {
    GUARD_ERROR2(a, b);  // ✅ clear error propagation
    TypeId type_a = get_type_id(a);
    TypeId type_b = get_type_id(b);
    // ... existing logic
}

Item fn_trim(Item str) {
    GUARD_ERROR1(str);  // ✅ clear error propagation
    // ... existing logic
}
```

This gives us:
1. **Explicit** error propagation (not accidental fallthrough)
2. **Correct** error messages (the original error is returned, not "unknown type 23")
3. **Efficient** (single type check at entry, no overhead on happy path)

### Functions to Add Guards

All `Item`-returning system functions (approximately 80+):

| Category | Functions | Guard Type |
|----------|-----------|------------|
| Arithmetic (binary) | `fn_add`, `fn_sub`, `fn_mul`, `fn_div`, `fn_mod`, `fn_idiv`, `fn_pow` | `GUARD_ERROR2` |
| Arithmetic (unary) | `fn_abs`, `fn_round`, `fn_floor`, `fn_ceil`, `fn_neg`, `fn_pos` | `GUARD_ERROR1` |
| Min/Max | `fn_min2`, `fn_max2` | `GUARD_ERROR2` |
| Min/Max (agg) | `fn_min1`, `fn_max1`, `fn_sum`, `fn_avg` | `GUARD_ERROR1` |
| Vector binary | `vec_add`, `vec_sub`, `vec_mul`, `vec_div`, `vec_mod`, `vec_pow` | `GUARD_ERROR2` |
| Vector unary | `fn_reverse`, `fn_sort1`, `fn_unique`, `fn_prod`, `fn_cumsum`, `fn_cumprod` | `GUARD_ERROR1` |
| Vector binary | `fn_concat`, `fn_zip`, `fn_dot` | `GUARD_ERROR2` |
| Vector slice | `fn_take`, `fn_drop`, `fn_slice` | `GUARD_ERROR2` / `GUARD_ERROR3` |
| String ops | `fn_trim`, `fn_lower`, `fn_upper`, `fn_split`, `fn_replace` | `GUARD_ERROR1` / `GUARD_ERROR2`+ |
| String join | `fn_join`, `fn_str_join` | `GUARD_ERROR2` |
| String search | `fn_substring` | `GUARD_ERROR3` |
| Conversion | `fn_int`, `fn_float`, `fn_decimal`, `fn_binary` | `GUARD_ERROR1` |
| Normalize | `fn_normalize`, `fn_normalize1` | `GUARD_ERROR1` |

### Bool-Returning Functions

For functions returning `Bool`, error propagation needs the `BOOL_ERROR` sentinel:

```c
#define GUARD_BOOL_ERROR1(a) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return BOOL_ERROR
#define GUARD_BOOL_ERROR2(a, b) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return BOOL_ERROR; \
    if (get_type_id(b) == LMD_TYPE_ERROR) return BOOL_ERROR

Bool fn_contains(Item str, Item substr) {
    GUARD_BOOL_ERROR2(str, substr);
    // ... existing logic
}
```

---

## Proposed Solution: Static Error Strings {#proposed-solution-static-error-strings}

As an **immediate** fix (before the full Result type migration), introduce a static error string that `fn_string()` returns instead of NULL:

```c
// Static error indicator string — never NULL, never deallocated
static String STR_ERROR_VALUE = {.len = 7, .ref_cnt = 1023};  // "<error>"
// (chars[] would contain "<error>")

// Or simpler: use a pre-allocated name pool entry
static String* STR_ERROR = heap_create_name("<error>");
```

This prevents NULL dereference crashes in `fn_strcat()` and all callers, at the cost of error items appearing as the string `"<error>"` when stringified. This is acceptable behavior — similar to how `null` stringifies to `"null"`.

### `is_truthy` Fix

Error items should be **falsy**, not truthy:

```c
Bool is_truthy(Item item) {
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_ERROR) return BOOL_FALSE;  // ✅ errors are falsy
    // ... existing logic
}
```

---

## Proposed Solution: Poison NaN for Float Errors {#proposed-solution-poison-nan-for-float-errors}

*Inspired by: JavaScript/IEEE 754 NaN propagation*

### Problem

`it2d()` returns `0.0` for error Items. This silently converts an error into a valid number:

```lambda
let x = might_fail()    // returns error
let y = x + 5.0         // it2d(error) → 0.0, result is 5.0 — error vanished
```

### Solution

Return `NaN` instead of `0.0` for unrecognized/error types in float unboxing:

```c
#include <math.h>  // for NAN

double it2d(Item item) {
    TypeId type_id = get_type_id(item);
    switch (type_id) {
    case LMD_TYPE_FLOAT:  return item.get_double();
    case LMD_TYPE_INT:    return (double)item.get_int56();
    case LMD_TYPE_INT64:  return (double)item.get_int64();
    case LMD_TYPE_DECIMAL: /* ... */
    case LMD_TYPE_BOOL:   return item.bool_val ? 1.0 : 0.0;
    case LMD_TYPE_ERROR:  return NAN;  // ✅ poison NaN — auto-propagates through arithmetic
    default:
        log_debug("it2d: cannot convert type %s to double", get_type_name(type_id));
        return NAN;  // ✅ was 0.0 — now NaN
    }
}
```

### Why NaN Works

NaN has a unique property defined by IEEE 754: **it propagates through all arithmetic operations**:

| Expression | Result | Explanation |
|-----------|--------|-------------|
| `NaN + 5` | `NaN` | Error propagates through addition |
| `NaN * 100` | `NaN` | Error propagates through multiplication |
| `NaN > 0` | `false` | All comparisons with NaN are false |
| `NaN == NaN` | `false` | NaN is not equal to itself |
| `isnan(x)` | `true` | Detectable with standard function |

This means if an error leaks into a float computation pipeline, **every downstream result will be NaN** — a clear signal that something went wrong. With `0.0`, the error silently becomes a valid number and corrupts results undetectably.

### Impact on `fn_*_u` Unboxed Functions

The unboxed math functions (`fn_pow_u`, `fn_min2_u`, `fn_max2_u`, etc.) operate on `double` values. With NaN inputs, they will naturally produce NaN outputs (IEEE 754 semantics), so **no changes needed** to those functions — NaN propagation is free.

### Compatibility Note

NaN is a valid `double` value — no special memory representation needed. `push_d(NaN)` will correctly store it on the num_stack and box it as a `FLOAT` Item. The Lambda `float` type already supports NaN implicitly via IEEE 754.

---

## Proposed Solution: DateTime Error Sentinel {#proposed-solution-datetime-error-sentinel}

*Inspired by: POSIX `(time_t)-1` for `mktime()` and `time()` error returns*

### Problem

DateTime functions return an all-zeros struct on failure:

```c
DateTime err;
memset(&err, 0, sizeof(DateTime));
return err;  // year=-4000, month=0, day=0 — looks like a valid DTIME Item
```

This passes through `push_k()` and `k2it()` undetected because:
- `push_k()` allocates on num_stack (pointer is never NULL)
- `k2it()` only checks for NULL pointer
- The all-zeros DateTime encodes a technically valid (but nonsensical) date

### Solution

Define an explicit error sentinel value using all-ones (0xFFFFFFFFFFFFFFFF):

```c
// DateTime error sentinel — all bits set = clearly invalid
// year_month=131071 → year=4191, month=15 (invalid month, max possible)
// day=31, hour=31, minute=63, second=63, millisecond=1023
// tz_offset_biased=2047, precision=3, format_hint=3
// Every field is at its maximum — an impossible combination
#define DATETIME_ERROR_VALUE  0xFFFFFFFFFFFFFFFFULL
#define DATETIME_IS_ERROR(dt) ((dt).int64_val == DATETIME_ERROR_VALUE)
```

### Why All-Ones

| Criterion | All-zeros (`memset 0`) | All-ones (`0xFFFF...`) |
|-----------|----------------------|----------------------|
| Month field | 0 (ambiguous: could mean "unset") | 15 (impossible: months are 1-12) |
| Day field | 0 (ambiguous) | 31 (could be valid in some months) |
| Detectable? | ❌ Looks like year -4000 | ✅ Month=15 is always invalid |
| Used currently? | ✅ Yes — collision with error | ❌ No — safe to claim as sentinel |
| Distinct from valid dates? | ❌ No | ✅ Yes (month=15 is impossible) |

### Applied to DateTime Functions

```c
DateTime fn_datetime1(Item arg) {
    TypeId arg_type = get_type_id(arg);

    // error input propagation
    if (arg_type == LMD_TYPE_ERROR) {
        DateTime err;
        err.int64_val = DATETIME_ERROR_VALUE;
        return err;
    }

    if (arg_type == LMD_TYPE_STRING || arg_type == LMD_TYPE_SYMBOL) {
        // ... parse logic ...
        if (!parsed) {
            log_error("datetime: failed to parse string '%.*s'", (int)len, chars);
            DateTime err;
            err.int64_val = DATETIME_ERROR_VALUE;
            return err;
        }
    }
    // ...
}
```

### Applied to `push_k()`

```c
Item push_k(DateTime val) {
    if (DATETIME_IS_ERROR(val)) return ItemError;  // ✅ error detected, return ITEM_ERROR
    // ... existing logic
    DateTime *dtptr = num_stack_push_datetime(context->num_stack, val);
    return {.item = k2it(dtptr)};
}
```

Now the chain `fn_date3(bad_input)` → `push_k(DATETIME_ERROR)` → `ItemError` works end-to-end. The error DateTime never leaks into the Lambda type system as a valid `DTIME` Item.

### Applied to `fn_date3()` with Range Validation

```c
DateTime fn_date3(Item y, Item m, Item d) {
    // error input propagation
    if (get_type_id(y) == LMD_TYPE_ERROR || get_type_id(m) == LMD_TYPE_ERROR
        || get_type_id(d) == LMD_TYPE_ERROR) {
        DateTime err; err.int64_val = DATETIME_ERROR_VALUE; return err;
    }

    int year = ...;
    int month = ...;
    int day_val = ...;

    // range validation
    if (month < 1 || month > 12) {
        log_error("date: month must be 1-12, got %d", month);
        DateTime err; err.int64_val = DATETIME_ERROR_VALUE; return err;
    }
    if (day_val < 1 || day_val > 31) {
        log_error("date: day must be 1-31, got %d", day_val);
        DateTime err; err.int64_val = DATETIME_ERROR_VALUE; return err;
    }

    DateTime dt;
    memset(&dt, 0, sizeof(DateTime));
    DATETIME_SET_YEAR_MONTH(&dt, year, month);
    dt.day = day_val;
    dt.precision = DATETIME_PRECISION_DATE_ONLY;
    return dt;  // ✅ validated
}
```

---

## Proposed Solution: Tiered Error Severity {#proposed-solution-tiered-error-severity}

*Inspired by: Node.js error classification (programmer errors vs. operational errors vs. fatal errors)*

### Problem

All runtime errors are currently handled uniformly — a missing file and a NULL context pointer get the same `log_error` + return `ItemError` treatment. This makes it hard to distinguish "expected failures that users should handle" from "internal bugs that indicate runtime corruption."

### Solution: Three Error Tiers

| Tier | Description | C Runtime Action | Lambda Surface |
|------|-------------|-----------------|----------------|
| **Tier 1: Propagated errors** | Error Items flowing through sys functions | `GUARD_ERROR` macro → return original error | User handles with `?` or `let a^err` |
| **Tier 2: Operational errors** | Validation failures, parse errors, I/O failures | Create new error Item with context → return | User handles with `?` or `let a^err` |
| **Tier 3: Fatal errors** | NULL context, pool corruption, stack overflow | `log_error` + abort (debug) or return error (release) | Runtime terminates or returns error |

### Tier 1: Error Propagation (GUARD_ERROR)

Already described in [Error-Early-Return Guards](#proposed-solution-error-early-return-guards). These are not new errors — they are **existing error Items passing through** a function that doesn't know what to do with them. The correct behavior is to return them unchanged:

```c
Item fn_add(Item a, Item b) {
    GUARD_ERROR2(a, b);  // tier 1: propagate existing errors
    // ... normal logic
}
```

### Tier 2: Operational Errors (New Error Creation)

These are **new errors created by system functions** when they encounter invalid inputs or external failures:

```c
DateTime fn_date3(Item y, Item m, Item d) {
    // tier 1: propagate
    if (get_type_id(y) == LMD_TYPE_ERROR) { DateTime e; e.int64_val = DATETIME_ERROR_VALUE; return e; }

    int month = ...;
    if (month < 1 || month > 12) {
        // tier 2: new operational error
        log_error("date: month must be 1-12, got %d", month);
        DateTime e; e.int64_val = DATETIME_ERROR_VALUE; return e;
    }
    // ...
}
```

### Tier 3: Fatal Errors (Runtime Integrity)

These indicate the runtime itself is in a bad state. They should **never happen** in correct code:

```c
Item push_k(DateTime val) {
    if (!context->num_stack) {
        // tier 3: fatal — runtime is broken
        log_error("push_k: FATAL — num_stack is NULL, context corrupted");
        #ifdef LAMBDA_DEBUG
        abort();  // crash immediately in debug for stack trace
        #endif
        return ItemError;
    }
    // ...
}
```

### Corresponding Macros

```c
// Tier 1: propagate existing error (zero overhead on happy path)
#define GUARD_ERROR1(a) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return (a)

// Tier 2: create operational error (includes context for debugging)
#define RETURN_ERROR(msg) \
    do { log_error(msg); return ItemError; } while(0)
#define RETURN_ERROR_FMT(fmt, ...) \
    do { log_error(fmt, __VA_ARGS__); return ItemError; } while(0)

// Tier 3: fatal runtime error (debug-mode abort)
#define FATAL_ERROR(msg) \
    do { \
        log_error("FATAL: " msg); \
        assert(false && "FATAL: " msg); \
        return ItemError; \
    } while(0)
```

### Benefits of Tiering

1. **Log filtering**: Tier 1 errors don't log at all (they're expected flow). Tier 2 logs at `error` level. Tier 3 logs at `error` level with "FATAL" prefix.
2. **Debug productivity**: When you see "FATAL" in the log, you know it's a runtime bug, not a user error.
3. **Performance**: Tier 1 guards are a single type check. No logging, no allocation on the hot path.

---

## Implementation Plan

### Phase 1: Immediate Safety Fixes (Low Risk, High Impact)

These changes prevent crashes without altering function signatures:

1. **`fn_string()`: Return static error string instead of NULL**
   - Change `case LMD_TYPE_ERROR: return NULL;` → `return &STR_ERROR;`
   - All callers immediately stop crashing

2. **`is_truthy()`: Error items return `BOOL_FALSE`**
   - Prevents errors from being silently truthy

3. **Add `GUARD_ERROR` to all Item-returning functions**
   - ~80 one-line additions
   - Zero risk: same result as before but with correct error messages

4. **Fix `fn_min2()` typo**: `item_a._type_id` → `item_b._type_id`

5. **`fn_index_of` / `fn_last_index_of`**: Return `INT64_ERROR` for error inputs instead of `-1`

6. **`it2d()`: Return `NAN` instead of `0.0` for error/unrecognized types**
   - Poison NaN auto-propagates through all downstream float arithmetic
   - Zero risk: no valid computation should depend on error→0.0 coercion

7. **Define `DATETIME_ERROR_VALUE` (all-ones sentinel)**
   - Replace all `memset(&err, 0, ...)` in datetime functions with `err.int64_val = DATETIME_ERROR_VALUE`
   - Add `DATETIME_IS_ERROR()` check in `push_k()`
   - Error datetime values now correctly become `ItemError` instead of garbage `DTIME` Items

8. **Define tiered error macros** (`GUARD_ERROR`, `RETURN_ERROR`, `FATAL_ERROR`)
   - Adopt consistently across runtime

### Phase 2: DateTime Validation (Medium Risk)

1. **Add range validation to `fn_date3()`**: month 1-12, day 1-31
2. **Add range validation to `fn_time3()`**: hour 0-23, minute 0-59, second 0-59
3. **Add error Item check to `fn_datetime1()`, `fn_date1()`, `fn_time1()`** at entry
4. **Add `datetime_is_valid()` call** before returning from all datetime constructors

### Phase 3: Result Types (Higher Risk, Full Solution)

1. **Define Result types** in `lambda.h`
2. **Migrate `fn_string()`** to return `StringResult`
3. **Update all callers** of `fn_string()` (evaluator, transpiler)
4. **Migrate datetime constructors** to return `DateTimeResult`
5. **Update transpiler** to emit result-unwrapping code
6. **Migrate `fn_symbol1()`** to return `SymbolResult`
7. **Test MIR JIT compatibility** with struct returns
8. **Add debug-mode assertion** `STRING_UNWRAP_CHECKED` / `DATETIME_UNWRAP_CHECKED` (V8-inspired)

### Phase 4: Transpiler Auto-Error-Check (Optional, Future)

Consider having the transpiler automatically emit error checks for system function calls even without explicit `?`, at least for functions known to be failable. This would provide defense-in-depth beyond the compiler enforcement.

---

## Migration Strategy

### Backward Compatibility

- **Phase 1** changes are purely internal — no Lambda language semantics change
- **Phase 2** may cause previously "silent" invalid dates to become errors — this is **desired** (better to fail loudly than silently corrupt)
- **Phase 3** changes function signatures but all callers are internal C/C++ code

### Testing

Each phase should run the full test suite:
```bash
make test-lambda-baseline   # Must pass 100%
make test-radiant-baseline  # Must pass 100%
make test                   # Extended tests
```

Add new test cases for:
- `string(error("test"))` → should produce error, not crash
- `error("test") ++ "hello"` → should produce error, not undefined
- `date(2024, 13, 1)` → should produce error (month out of range)
- `date(2024, 1, 32)` → should produce error (day out of range)
- `time(25, 0, 0)` → should produce error (hour out of range)
- `datetime("not-a-date")` → should produce error
- `if (error("x")) "yes" else "no"` → should evaluate to `"no"` (error is falsy)
- `[1, 2, error("x")] |> sum` → should produce error

---

## Alternatives Considered

### Alternative 1: All Functions Return `Item`

Change all system functions to return `Item` instead of native types.

**Pros**: Uniform error channel, no new types needed.
**Cons**: Breaks MIR JIT calling convention for functions like `fn_date3()`. Performance regression from boxing/unboxing overhead. Major refactoring of transpiler.

**Verdict**: Too invasive. The Result type approach is more surgical.

### Alternative 2: Thread-Local Error Slot

Functions still return native types but set a thread-local error flag on failure.

**Pros**: No signature changes. Compatible with MIR.
**Cons**: Easy to forget to check. Global mutable state. Not compositional (nested calls overwrite error). Similar problems to C's `errno`.

**Verdict**: Acceptable as a fallback if MIR can't handle struct returns, but Result types are preferred.

### Alternative 3: Error Callback

Register an error callback that's invoked when a system function encounters an error.

**Pros**: Decoupled error handling.
**Cons**: Changes control flow in hard-to-predict ways. Not compatible with Lambda's error-as-value philosophy. Essentially reinventing exceptions.

**Verdict**: Rejected. Doesn't fit Lambda's design.

### Alternative 4: Sentinel Values Only (Status Quo+)

Define proper sentinel values for each type: `STR_ERROR` for strings, `DATETIME_ERROR` for datetime, etc.

**Pros**: Minimal changes. No new types.
**Cons**: Can't carry error details (message, code, source location). Different sentinel for each type. Callers must know each sentinel. Errors lose context.

**Verdict**: Acceptable for Phase 1 (immediate safety) but insufficient long-term. The Result type carries the original error Item with full context.

---

## Summary of Issue Counts

| Severity | Count | Examples |
|----------|-------|---------|
| 🔴 CRASH (NULL deref) | 4 | `fn_string`, `it2s`, `fn_symbol1`, `fn_format1/2` |
| 🔴 SILENT CORRUPTION | 3 | `fn_datetime1`, `fn_date3`, `s2it(NULL)→UNDEFINED` |
| 🟠 HIGH (wrong behavior) | 8 | `is_truthy(error)=true`, `fn_index_of(error)=-1`, datetime no-validation |
| 🟡 MEDIUM (accidental fallthrough) | ~25 | All arithmetic/string functions without `GUARD_ERROR` |
| 🟢 LOW (latent issues) | 5 | `fn_to_cstr`→empty, `fn_neg` decimal, `fn_mod_i` div-by-zero |

**Total: ~45 issues identified across the runtime.**

The proposed three-phase approach addresses all of them:
- **Phase 1** (immediate): Eliminates all crashes and silent corruption with minimal risk
- **Phase 2** (validation): Adds input validation to datetime constructors
- **Phase 3** (structural): Result types provide a permanent, compositional error channel for native-type-returning functions
