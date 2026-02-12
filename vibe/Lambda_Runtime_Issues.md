# Lambda Runtime: Structural Issues Audit

Date: 2026-02-12

## Priority Summary

| Priority | Items | Effort | Impact |
|----------|-------|--------|--------|
| **Fix now** (bugs) | #1–5 | Small — point fixes | Correctness & crash prevention |
| **Fix soon** (time bombs) | #6–9 | Small–medium | Prevents future corruption |
| **Plan for** (architecture) | #10–12 | Medium–large | Maintainability & performance |
| **Opportunistic** | #13–16 | Varies | Code quality |

---

## 🔴 Actual Bugs (Should Fix)

### Issue #1: NamePool `ref_cnt` not incremented for parent pool hits — ✅ FIXED

**File:** `lambda/name_pool.cpp` — `name_pool_create_strview()`
**Severity:** 🔴 Critical — potential use-after-free

`name_pool_create_strview()` returns a parent pool's string without incrementing `ref_cnt`. The comment claims "ref_cnt already incremented by lookup" — but the header explicitly states lookup does **not** increment `ref_cnt`.

**Consequence:** Parent-pool strings have fewer references than they should. During `heap_cleanup`, strings may be freed while still referenced from child-pool data structures → use-after-free / dangling pointer.

**Fix:** Add `parent_result->ref_cnt++` before returning the parent hit:
```cpp
if (pool->parent) {
    String* parent_result = name_pool_lookup_strview(pool->parent, name);
    if (parent_result) {
        parent_result->ref_cnt++;  // ← FIX: increment for this reference
        return parent_result;
    }
}
```

---

### Issue #2: NamePool `lookup` skips current pool when parent exists — ✅ FIXED

**File:** `lambda/name_pool.cpp` — `name_pool_lookup_strview()`
**Severity:** 🔴 Critical — silent data loss

When a NamePool has a parent, `lookup` immediately recurses into the parent and **never searches the current pool**. Strings added to child pools are invisible to `lookup`, `name_pool_lookup`, and all external callers.

Contrast with `name_pool_create_strview()` which correctly searches parent first, then current pool. The asymmetry is a bug.

**Fix:** Search current pool first (or after parent):
```cpp
String* name_pool_lookup_strview(NamePool* pool, StrView name) {
    if (!pool) return nullptr;
    String* result = find_string_by_content(pool, name.str, name.length);
    if (result) return result;
    if (pool->parent) return name_pool_lookup_strview(pool->parent, name);
    return nullptr;
}
```

---

### Issue #3: `list_push` decimal branch — NULL deref — ✅ FIXED

**File:** `lambda/lambda-data.cpp` — `list_push()`
**Severity:** 🔴 High — crash

The decimal handling branch checks `if (dval && dval->dec_val)` but then unconditionally executes `dval->ref_cnt++` **outside** the guard block:

```cpp
case LMD_TYPE_DECIMAL: {
    Decimal *dval = item.get_decimal();
    if (dval && dval->dec_val) {
        // ... debug logging ...
    } else {
        log_debug("pushed null decimal value");
    }
    dval->ref_cnt++;  // ← BUG: dereferences dval even when NULL
    break;
}
```

**Fix:** Move `ref_cnt++` inside the `if (dval)` guard.

---

### Issue #4: `cleanup_temp_decimal` — TypeId passed as bool — ✅ FIXED

**File:** `lambda/lambda-eval-num.cpp` — `fn_add`, `fn_sub`, `fn_mul`, `fn_div`, etc.
**Severity:** 🔴 High — memory leak

In arithmetic functions, the first call to `cleanup_temp_decimal` passes the raw `TypeId` integer as the `is_original` bool parameter:

```cpp
cleanup_temp_decimal(a_dec, type_a);  // ← BUG: type_a is TypeId, not bool
cleanup_temp_decimal(b_dec, type_b == LMD_TYPE_DECIMAL);  // ← correct
```

Any non-zero TypeId is treated as `true` ("is original decimal, don't free"), causing **temporary decimals to never be freed**. This pattern is repeated in every binary arithmetic function.

**Fix:** Change to `cleanup_temp_decimal(a_dec, type_a == LMD_TYPE_DECIMAL)`.

---

### Issue #5: `type_info[LMD_TYPE_RANGE]` has wrong display name

**File:** `lambda/lambda-data.cpp` — `type_info` array
**Severity:** 🟡 Low — misleading debug output

```cpp
type_info[LMD_TYPE_RANGE] = {sizeof(void*), "array", &TYPE_RANGE, ...};
//                                           ^^^^^
//                                           should be "range"
```

Any error message or `get_type_name()` call for range values displays "array" instead of "range".

**Fix:** Change `"array"` to `"range"`.

---

## 🟠 Ticking Time Bombs (Should Address Soon)

### Issue #6: String/Symbol `ref_cnt` is only 10 bits (max 1023)

**File:** `lambda/lambda.h` — `struct String`, `struct Symbol`
**Severity:** 🟠 High — silent corruption in large documents

```c
typedef struct String {
    uint32_t len:22;      // up to 4MB
    uint32_t ref_cnt:10;  // up to 1023 refs — TOO SMALL
    char chars[];
} String;
```

Name-pooled strings are shared across all maps, elements, and attributes in an entire document hierarchy. A heavily-used key like `"class"` or `"id"` in an HTML document could easily exceed 1023 references. At 1024, `ref_cnt` silently wraps to 0, making the string appear freeable while still in use → use-after-free.

**Options:**
- A) Widen `ref_cnt` to 16 bits (reduce `len` to 16 bits / 64KB max, or expand to 64-bit header)
- B) Saturate at max: once `ref_cnt` reaches 1023, never decrement — treat as permanent (zero cost, simple fix)

---

### Issue #7: `type_info` array has only 32 slots, nearly full — ✅ FIXED

**File:** `lambda/lambda-data.cpp`
**Severity:** 🟠 High — will overflow with 2 more types

```cpp
TypeInfo type_info[32];  // ~30 type IDs already used
```

Adding 2 more types will overflow the array. Additionally, `LMD_TYPE_PATTERN` has **no entry** — any code calling `get_type_name(LMD_TYPE_PATTERN)` reads zeroed memory, yielding empty string and NULL type pointer.

**Fix:** Derive array size from a `LMD_TYPE_COUNT` sentinel in the enum, and add the missing PATTERN entry.

---

### Issue #8: Function objects allocated with raw `calloc`, never freed

**File:** `lambda/lambda-eval.cpp` — `create_fn_*` functions
**Severity:** 🟠 High — systematic memory leak

All 5 function/closure creation functions (`create_fn_lambda`, `create_fn_closure`, etc.) use bare `calloc` — NOT `pool_calloc`, NOT tracked by the heap system. These allocations are invisible to `heap_cleanup` and are **never freed**.

Every lambda/closure creation leaks ~64 bytes. In a long-running script that creates many closures (e.g., in a loop), this adds up.

**Fix:** Allocate via `pool_calloc(context->heap->pool, sizeof(Function))` and track in heap entries, OR use arena allocation for function objects with the same lifetime as their containing scope.

---

### Issue #9: ShapePool signature compare doesn't verify field names

**File:** `lambda/shape_pool.cpp` — `shape_signature_compare()`
**Severity:** 🟠 High — silent data corruption on hash collision

```cpp
static int shape_signature_compare(const void *a, const void *b, void *udata) {
    // Only compares: hash, length, byte_size
    // Does NOT compare actual field names or types
}
```

Two different shapes with the same hash + field count + byte size (a hash collision) would be treated as identical. One map's shape would be reused for a completely different map → **fields read from wrong offsets, silent data corruption**.

**Fix:** On hash match, perform deep comparison of actual field names and types (similar to `shapes_equal` which already does this).

---

## 🟡 Architecture Issues (Improve Over Time)

### Issue #10: O(N²) type-pair dispatch in arithmetic

**File:** `lambda/lambda-eval-num.cpp` — all binary arithmetic functions
**Severity:** 🟡 Medium — maintainability + missed optimization

Every binary operator (`fn_add`, `fn_sub`, `fn_mul`, `fn_div`, `fn_mod`, `fn_idiv`, `fn_pow`) repeats a ~120-line cascade of `if/else if` branches for every pair of (INT, INT64, FLOAT, DECIMAL) × (INT, INT64, FLOAT, DECIMAL) — 15+ branches per operator, all structurally identical except the operator itself.

The decimal cleanup boilerplate alone is duplicated 6+ times. Adding one numeric type means editing **every** operator function.

**Improvement:** A type-promotion table + double-dispatch matrix:
```cpp
// Promote both args to the widest common type, then dispatch once
NumericType common = promote(type_a, type_b);  // INT < INT64 < FLOAT < DECIMAL
switch (common) {
    case FLOAT:   return push_d(to_double(a) + to_double(b));
    case DECIMAL: return decimal_op(a, b, mpd_add);
    // ...
}
```

---

### Issue #11: Monolithic source files

**Files affected:**

| File | Lines | Scope |
|------|-------|-------|
| `lambda/transpile.cpp` | ~5,443 | C code generation for entire language |
| `lambda/build_ast.cpp` | ~5,396 | AST construction for all grammar productions |
| `lambda/lambda-eval.cpp` | ~2,871 | String ops, datetime, comparison, formatting, I/O, regex |
| `lambda/lambda-eval-num.cpp` | ~2,578 | Arithmetic, aggregation, type conversion |

**Suggested splits:**
- `lambda-eval.cpp` → `lambda-eval-string.cpp`, `lambda-eval-datetime.cpp`, `lambda-eval-compare.cpp`
- `transpile.cpp` → `transpile-expr.cpp`, `transpile-func.cpp`, `transpile-loop.cpp`
- `build_ast.cpp` → `build_ast-expr.cpp`, `build_ast-func.cpp`, `build_ast-stmt.cpp`

---

### Issue #12: Linear scan for system function lookup

**File:** `lambda/build_ast.cpp` — `find_sys_func()`
**Severity:** 🟡 Medium — performance on every function call

The `sys_func` table has ~130 entries. `find_sys_func()` does a linear scan on **every** function call expression during AST building. For method-style calls, a second linear scan happens in the method lookup path.

**Fix:** A hashmap or perfect hash (the set of system function names is known at compile time) would make this O(1).

---

## 🟢 Opportunistic Fixes

### Issue #13: `current_vargs` thread-local unsafe for nesting

**File:** `lambda/lambda-eval.cpp`
**Severity:** 🟢 Low-medium — only matters for nested variadic calls

`__thread List* current_vargs = NULL;` is overwritten unconditionally when entering a variadic function. If a variadic function's argument expression calls another variadic function, the outer's vargs are lost.

**Fix:** Stack-based save/restore:
```cpp
List* saved_vargs = current_vargs;
current_vargs = new_vargs;
// ... call body ...
current_vargs = saved_vargs;
```

---

### Issue #14: `heap_strcpy` sets `ref_cnt = 0`

**File:** `lambda/lambda-mem.cpp` — `heap_strcpy()`
**Severity:** 🟢 Medium — premature free risk

The newly allocated string has `ref_cnt = 0`, making it immediately eligible for freeing by `heap_cleanup`. If the caller doesn't store it somewhere that increments `ref_cnt` before the next GC sweep, it's freed prematurely. Compare with `name_pool_create` which sets `ref_cnt = 1`.

**Fix:** Initialize `ref_cnt = 1` (caller is the first reference), or document that callers must immediately increment.

---

### Issue #15: `transpile-mir.cpp` is dead code

**File:** `lambda/transpile-mir.cpp` (496 lines)
**Severity:** 🟢 Low — confusion risk

The direct AST→MIR transpiler is a non-functional stub:
- Integer literals hardcoded to `42`
- Float literals hardcoded to `3.14`
- Identifiers return `10`
- Only 4 AST node types handled (primary, binary, unary, ident)
- No functions, closures, loops, strings, or data structures

The actual JIT path is Lambda → C code (via `transpile.cpp`) → MIR (via C2MIR). This file is dead code.

**Fix:** Remove, or add a prominent comment marking it as experimental/unused.

---

### Issue #16: `TypedItem` `pack(1)` causes unaligned 8-byte access

**File:** `lambda/lambda-data.hpp` — `struct TypedItem`
**Severity:** 🟢 Low — performance penalty on ARM64

```cpp
#pragma pack(push, 1)
typedef struct TypedItem {
    TypeId type_id;       // 1 byte
    union {               // starts at offset 1 — UNALIGNED
        int64_t long_val;
        double double_val;
        // ...
    };
} TypedItem;
#pragma pack(pop)
```

The 8-byte union members start at byte offset 1. On x86/ARM64 this works but incurs a performance penalty. On strict-alignment architectures it would cause SIGBUS.

**Fix:** Pad to 16 bytes (1 byte TypeId + 7 padding + 8 byte union), or accept the 1-byte overhead and remove `pack(1)`.
