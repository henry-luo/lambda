# Lambda MIR Direct Transpiler — Enhancement Proposal

## 1. Overview

This proposal describes the next phase of the **direct AST→MIR transpiler** (`transpile-mir.cpp`). The current MIR transpiler passes 85 baseline tests and demonstrates **2.60x faster JIT compilation** than the C2MIR path, but suffers from **4 crashing benchmarks** and **2–19x slower execution** on compute-intensive workloads due to missing optimizations.

### Current Performance Profile

| Metric | C2MIR Path | MIR Direct | Delta |
|--------|-----------|------------|-------|
| Avg JIT overhead | 6.503 ms | 2.497 ms | **2.60x faster** |
| JIT bottleneck | C2MIR parse (43.7%) + MIR Gen (46.0%) | Link+Gen (73.6%) + AST→MIR (22.7%) | |
| Execution (matmul) | 333 ms | 1,606 ms | **4.82x slower** |
| Execution (triangl) | 1,723 ms | 32,583 ms | **18.9x slower** |
| Failing benchmarks | 0 | 4 (collatz, diviter, pnpoly, ray) | |

### Goals

1. **Fix all 4 crashing/failing benchmarks** (pnpoly, ray, collatz, diviter)
2. **Close the execution performance gap** with type-aware native code generation
3. **Implement tail call optimization** (TCO) for recursive functions
4. **Implement direct map/struct field access** via the Shape system

### Priority Order

| Phase | Area | Impact | Effort |
|-------|------|--------|--------|
| Phase 1 | Bug fixes (3 distinct bugs) | Correctness — 4 benchmarks | Low |
| Phase 2 | Type narrowing for untyped params | 2–19x speedup on compute benchmarks | Medium |
| Phase 3 | Direct map/struct field access | O(1) vs O(n) field lookup | Medium |
| Phase 4 | Tail call optimization | Stack overflow safety for recursion | Medium |

---

## 2. Phase 1 — Fix Crashing Issues (COMPLETED)

Three distinct bugs cause 4 benchmark failures. All have identified root causes and fix locations.

### Bug 1: Bool/Item Confusion in Comparison Operators

**Affected benchmarks**: `pnpoly`
**Symptom**: SIGSEGV crash during execution
**Root cause**: Comparison operators (`==`, `!=`, `<`, etc.) return `Bool` (uint8_t `0` or `1`) as a raw MIR `I64` value, but the result is later used as a boxed `Item`. When the raw value `0x1` is treated as a pointer (by `fn_ne`, `fn_eq`, etc.), it dereferences invalid memory → SIGSEGV.

**Code path**:
1. `transpile_binary()` at line ~1850–2030: comparison ops with typed operands emit native MIR comparison instructions (e.g., `MIR_EQ`, `MIR_LT`) returning `I64` values `0` or `1`
2. `get_effective_type()` at line ~1576: returns `LMD_TYPE_BOOL` for comparison results
3. When this Bool result feeds into another binary operation (e.g., `if result != 0`), the caller does `transpile_box_item()` which should box it, but if the caller instead passes the raw value directly as `I64` to a runtime function expecting a boxed `Item`, the runtime tries to extract type bits from `0x1` → crash

**Fix**:

```
Location: transpile-mir.cpp, transpile_binary()
```

Ensure comparison operator results are properly tracked as `LMD_TYPE_BOOL` type through the register's type metadata. The `emit_box()` path for `LMD_TYPE_BOOL` must be invoked before passing comparison results to any runtime function that expects boxed Items. Specifically:

1. After emitting native comparison instructions (MIR_EQ/NE/LT/LE/GT/GE), verify the result register is recorded with `MIR_T_I64` + `LMD_TYPE_BOOL` in the variable scope
2. In `transpile_box_item()`, verify that when `get_effective_type()` returns `LMD_TYPE_BOOL`, the value is properly boxed via `emit_box_bool()` (tag the high bits with `LMD_TYPE_BOOL << 56`)
3. Review all call sites where a Bool-typed expression feeds into a runtime function — ensure boxing happens before the call

**Test**: Run `pnpoly` benchmark end-to-end without crash.

### Bug 2: Dead Code Type Mismatch After Return

**Affected benchmarks**: `ray`
**Symptom**: MIR compilation error — "Got 'int', expected 'double'" during code generation
**Root cause**: When a function body has a `return 1.0` followed by dead code, the transpiler emits:
1. The `return` statement → `MIR_RET` instruction with float result (correct)
2. Dead code after the return → tries to box the result of the dead expression. A dummy `I64` register is used, but the dead code calls `emit_box_float()` with this `I64` register → MIR type validation rejects the register type mismatch.

**Code path**:
1. `transpile_return()` at line ~5806: emits `MIR_RET` instruction
2. Execution continues transpiling the next AST node (dead code)
3. The dead code path attempts `emit_box_float()` on a register that was declared as `MIR_T_I64`

**Fix**:

```
Location: transpile-mir.cpp, function body transpilation
```

Add a `block_returned` flag to `MirTranspiler` struct. When `transpile_return()` or an unconditional `MIR_RET` is emitted:
1. Set `mt->block_returned = true`
2. In `transpile_stam()` / statement list processing, skip transpilation of subsequent statements when `block_returned == true`
3. Reset `block_returned = false` at branch merge points (after if/else labels, loop continues, match arms)

This mirrors how the C transpiler avoids generating unreachable code after `return` statements.

**Test**: Run `ray` benchmark end-to-end without compilation error.

### Bug 3: Untyped Parameters Force Boxed Runtime Fallback

**Affected benchmarks**: `collatz`, `diviter`
**Symptom**: Execution appears to hang (runs 10–100x slower than expected)
**Root cause**: When function parameters lack type annotations, they are stored as `LMD_TYPE_ANY` at line ~5726. This forces ALL arithmetic operations on those parameters through the boxed runtime fallback path (`fn_add`, `fn_sub`, `fn_mul`, etc.) instead of native MIR arithmetic instructions.

For tight computational loops like `collatz` (millions of iterations with `+`, `-`, `*`, `/`, `%`), the boxed fallback is 10–100x slower than native int arithmetic. The benchmark doesn't actually crash — it just takes so long it appears hung.

**Code path**:
1. `transpile_user_func()` at line ~5726: untyped params set as `set_var(mt, pname, preg, MIR_T_I64, LMD_TYPE_ANY)`
2. `transpile_binary()`: checks `get_effective_type()` for both operands. When either is `LMD_TYPE_ANY`, skips native path → falls through to boxed `fn_add`/`fn_sub` calls

**Fix**:

This is addressed in Phase 2 (Type Narrowing) below, which provides a comprehensive solution. As an interim fix for these specific benchmarks:

1. At function entry, after parameter binding, check if the parameter name appears in binary expressions within the function body with known-type counterparts
2. If a parameter is only ever used in integer arithmetic contexts, narrow it to `LMD_TYPE_INT` via `emit_unbox(mt, preg, LMD_TYPE_INT)` speculatively
3. Add a guard: if the runtime type doesn't match, fall back to boxed path

**Test**: Run `collatz` and `diviter` benchmarks — should complete in <3 seconds (matching C2MIR path).

### Phase 1 — Implementation Status (COMPLETED)

All three bugs have been fixed with additional correctness improvements discovered during implementation.

#### Bug 1 Fix: Comparison Type in `get_effective_type()`

**Changes in `transpile-mir.cpp`:**

1. **`get_effective_type()` (~line 1580)**: Moved BOOL return for comparison operators **before** the `if (lt == ANY || rt == ANY) return ANY` early exit. Previously, comparisons between ANY-typed operands (e.g., untyped function params) returned ANY, causing the raw uint8_t 0/1 from `fn_eq`/`fn_lt`/etc. to be treated as a boxed Item pointer.

2. **`transpile_binary()` boxed fallback (~line 2050)**: Added `MIR_AND` mask with `0xFF` after calling comparison runtime functions (`fn_eq`/`fn_ne`/`fn_lt`/`fn_gt`/`fn_le`/`fn_ge`). These functions return `Bool` (uint8_t) but are declared as returning `MIR_T_I64` — on some ABIs the upper register bytes may contain garbage. The mask ensures clean 0/1 values.

#### Bug 2 Fix: Terminal Branch Detection with `block_returned` Flag

**Changes in `transpile-mir.cpp`:**

1. **`MirTranspiler` struct (~line 157)**: Added `bool block_returned;` flag to track when a terminal instruction (return/break) has been emitted in the current code path.

2. **`transpile_return()` (~line 4510)**: Changed from `ret_node->value->type->type_id` to `get_effective_type(mt, ret_node->value)` which correctly identifies the variable's actual storage type. Also sets `mt->block_returned = true` after emitting MIR_RET.

3. **`transpile_if()` (~line 2195)**: After transpiling then/else branches, checks `block_returned`. When set, emits a dummy `MOV result, 0` instead of calling `emit_box()` on the branch result — which could cause type mismatches like `emit_box_float(I64_register)` for dead code after a return.

4. **`transpile_if()` CONTENT branch boxing (~line 2200)**: When `need_boxing` is true and the branch is a CONTENT or LIST node, uses `LMD_TYPE_ANY` for boxing instead of the AST-inferred type (`then_tid`/`else_tid`). This prevents type mismatches because `transpile_content()` always returns I64 (boxed Items from `list_end()` or `transpile_box_item()`), but the AST type may incorrectly say FLOAT/INT.

5. **`transpile_user_func()` (~line 5790)**: Saves and restores `block_returned` around function body compilation to prevent the flag from leaking between sequentially-compiled functions.

#### Bug 3: Confirmed Not a Crash

`collatz` and `diviter` don't crash — they timeout because untyped parameters force ALL arithmetic through boxed runtime calls (`fn_add`, `fn_sub`, etc.), which is ~100x slower per operation. Deferred to Phase 2 (Type Narrowing).

#### Benchmark Results After Phase 1

| Benchmark | Before | After | Status |
|-----------|--------|-------|--------|
| **pnpoly** | SIGSEGV at 0x1 | **PASS** (36s) | **FIXED** |
| **ray** | MIR type error | **PASS** (6.1s) | **FIXED** |
| collatz | Timeout | Timeout | Deferred (Phase 2) |
| diviter | Timeout | Timeout | Deferred (Phase 2) |
| quicksort | 2.39x slower | **PASS** (4.0s) | No regression |
| array1 | PASS | PASS | No regression |
| deriv | PASS | PASS | No regression |
| puzzle | PASS | PASS | No regression |
| paraffins | PASS | PASS | No regression |
| gcbench | PASS | PASS (43s) | No regression |
| divrec | N/A | Correct output, MIR cleanup crash | Pre-existing MIR issue |
| primes | N/A | Correct output, MIR cleanup crash | Pre-existing MIR issue |
| triangl | N/A | Timeout (>120s) | Deferred (Phase 2) |

**Baseline tests**: 445/451 pass (5 failures in non-MIR structured tests — pre-existing, unrelated to transpile-mir.cpp changes). MIR-specific tests: 27/155 pass (maintained from pre-fix level).

---

## 3. Phase 2 — Execution Performance Tuning (COMPLETED)

### Problem Analysis

The MIR direct transpiler's execution slowdown stems from one fundamental issue: **insufficient type information causes excessive boxing/unboxing and runtime function dispatch**.

Benchmark execution comparison (median of 3 runs):

| Benchmark | C2MIR (ms) | MIR Direct (ms) | Ratio | Root Cause |
|-----------|-----------|-----------------|-------|------------|
| triangl | 1,723 | 32,583 | 18.9x | Untyped recursive params |
| primes | 11.6 | 89.0 | 7.7x | Untyped loop counters |
| matmul | 334 | 1,606 | 4.8x | Untyped array indexing + arithmetic |
| array1 | 11.2 | 38.6 | 3.5x | Array access overhead |
| brainfuck | 345 | 1,022 | 3.0x | Untyped loop operations |
| quicksort | 6.6 | 15.8 | 2.4x | Untyped recursive params |
| puzzle | 13.2 | 22.6 | 1.7x | Mixed typed/untyped |
| levenshtein | 23.5 | 34.6 | 1.5x | Array operations |

Three benchmarks are already near parity:

| Benchmark | C2MIR (ms) | MIR Direct (ms) | Ratio | Root Cause |
|-----------|-----------|-----------------|-------|------------|
| base64 | 1,083 | 971 | 0.90x | I/O-bound (OK) |
| gcbench | 2,732 | 2,561 | 0.94x | GC-bound (OK) |
| json_gen | 68 | 66 | 0.97x | String-bound (OK) |

### 2.1 Type Narrowing for Untyped Parameters

**Impact**: Fixes 10+ benchmarks with 2–19x slowdowns
**Approach**: Infer parameter types from usage context at transpile time

The C2MIR path benefits from C's type system — the C compiler (C2MIR) performs type checking and optimization on the generated C code. The direct MIR path must replicate this with AST-level type inference.

**Implementation**:

1. **Pre-pass type inference** — Before transpiling a function body, scan all expressions involving each parameter:
   - If a param appears only in `+`, `-`, `*`, `/`, `%` with int literals or other int-typed values → infer `INT`
   - If a param appears only in float arithmetic → infer `FLOAT`
   - If a param is compared with `==`, `<`, etc. against typed values → infer from comparand
   - If a param is used as array index → infer `INT`
   - If a param feeds into `fn_member` → keep as `ANY` (boxed Item)

2. **Speculative unboxing with guard** — At function entry, after binding the parameter:
   ```
   // Pseudo-MIR for param 'n' inferred as INT:
   type_check = call item_type_id(n)
   if type_check != LMD_TYPE_INT goto slow_path
   n_native = n >> 8  // inline unbox
   // ... fast path using native int ops ...
   // slow_path: use boxed fn_add etc.
   ```

3. **Scope variable type update** — After speculative unboxing, update `set_var()` with the narrowed type so that `get_effective_type()` returns the precise type throughout the function body.

**Key location**: `transpile_user_func()` at line ~5700–5730 where params are bound.

### 2.2 Integer Arithmetic Optimization

**Current state**: When both operands are `LMD_TYPE_INT`, the MIR transpiler already emits native `MIR_ADD`, `MIR_SUB`, `MIR_MUL` instructions (see `transpile_binary()` ~line 1650–1800). The issue is that `get_effective_type()` returns `LMD_TYPE_ANY` for untyped params, bypassing this fast path.

**After type narrowing**: Once parameters are narrowed, the existing native arithmetic path will be used automatically. No additional changes needed for basic int/float ops.

### 2.3 Array/List Access Optimization

**Current state**: Array indexing goes through `fn_index(boxed_array, boxed_idx)` runtime call.

**Optimization**: For typed arrays with int index, emit inline access:
```
// Pseudo-MIR for arr[i] where arr: [int], i: int
base = load arr->data          // Container.data pointer
elem = load [base + i * 8]    // direct memory access
```

This requires:
1. Checking array element type from `TypeList.item_type`
2. Emitting bounds check: `if i >= arr->count goto error`
3. Loading `Container.data` pointer and computing byte offset

**Key struct** (`lambda-data.hpp`): `Container { TypeId type; int count; Item* data; }`

### 2.4 Inline String/Symbol Comparison

**Current state**: String equality uses `fn_eq(boxed_str1, boxed_str2)` — boxes both operands, dispatches through runtime.

**Optimization**: For symbol comparisons (common in pattern matching and map key checks), compare String* pointers directly since the name pool guarantees unique pointers for each symbol.

### Phase 2 — Implementation Status (COMPLETED)

Type narrowing for untyped parameters has been implemented, along with extensive correctness fixes discovered during testing. All 155 MIR baseline tests pass (up from 27 at end of Phase 1). Compute-heavy benchmarks show **up to 205x speedup** over C2MIR on integer-arithmetic workloads.

#### 2.5 Type Inference System

Implemented a two-pass type inference system in `transpile-mir.cpp`:

**Data structures:**
- `InferCtx` — tracks up to 8 names (parameter + aliases) and accumulated evidence flags
- Evidence flags: `INFER_INT=1`, `INFER_FLOAT=2`, `INFER_STOP=4`, `INFER_NUMERIC_USE=8`, `INFER_FLOAT_CONTEXT=16`

**Pass 1 — `find_aliases()`**: Scans function body for `let x = param` or `var x = param` patterns, transitively collecting aliases. This ensures that if a parameter is renamed via local binding, evidence gathered on the alias still counts.

**Pass 2 — `gather_evidence()`**: Walks the AST collecting type evidence for all tracked names:
- **Strong INT evidence** (`INFER_INT`): parameter used as array index (`arr[param]`), or in binary expression with an int literal (`param + 1`)
- **Strong FLOAT evidence** (`INFER_FLOAT`): parameter in binary expression with a float literal
- **Stop evidence** (`INFER_STOP`): parameter used in string concatenation, member access, or passed to function calls — indicates polymorphic use, keep as ANY
- **Numeric use** (`INFER_NUMERIC_USE`): parameter used in arithmetic/comparison operators but without literal type info (weak evidence)
- **Float context** (`INFER_FLOAT_CONTEXT`): function body contains float literals anywhere — guards against NUMERIC_USE→INT inference in mixed float/int functions
- **Unary negation**: `-param` sets `INFER_NUMERIC_USE` (not `INFER_INT`) because negation applies to both int and float

**`infer_param_type()` decision logic:**
1. If `INFER_STOP` → return `LMD_TYPE_ANY` (polymorphic use detected)
2. If `INFER_INT` only → return `LMD_TYPE_INT`
3. If `INFER_FLOAT` (possibly mixed with INT) → return `LMD_TYPE_FLOAT`
4. If `INFER_NUMERIC_USE` AND `is_proc` AND no `INFER_FLOAT_CONTEXT` → return `LMD_TYPE_INT` (procedural functions with pure integer arithmetic patterns)
5. Otherwise → return `LMD_TYPE_ANY`

The `is_proc` guard (rule 4) is critical: `pn` functions have predictable numeric usage patterns, while `fn` functions are often intentionally polymorphic and should not have types forced.

#### 2.6 Effective Type Migration (`get_effective_type` Audit)

Discovered and fixed a systematic bug pattern across **25+ locations** in the transpiler. The pattern:

```cpp
// BEFORE (broken): uses raw AST type, ignores inference
TypeId tid = node->type ? node->type->type_id : LMD_TYPE_ANY;
MIR_reg_t boxed = emit_box(mt, val, tid);
```

When type inference narrows a parameter to native `INT` (stored as raw `int64_t`), but code uses the AST type (`ANY`), `emit_box(val, ANY)` treats the native `int64_t` as an already-boxed `Item` → NULL pointer dereference at very low addresses (0x1, 0x2, 0x5).

```cpp
// AFTER (correct): uses effective type which respects inference
TypeId tid = get_effective_type(mt, node);
MIR_reg_t boxed = emit_box(mt, val, tid);
```

**Locations fixed (all in `transpile-mir.cpp`):**
- `transpile_index()` — changed to use `transpile_box_item()` for both object and field expressions
- `AST_NODE_INDEX_ASSIGN_STAM` — `obj_tid` and `idx_tid` for subscript assignment (critical for `placed[i] = row` in nqueens)
- `transpile_array()` — push pattern `val_tid`
- `transpile_match()` — pattern and scrutinee boxing
- `transpile_for()` — collection boxing
- `transpile_for_comprehension()` — where clause, body, key, offset (×2), limit (×2)
- `transpile_let_declarations()` — decompose source
- System function calls — `vmap_from_array`, `vmap_set` arguments
- Dynamic call — function expression boxing
- Pipe expressions — left and right operands
- Raise statement — value boxing
- Query expression — object and type arguments
- Path index segment
- Parent expression — object boxing
- Field access dispatch — `obj_tid` for Path type check
- `transpile_unary()` — operand_tid dispatch

#### 2.7 Proc Content Block Optimization

When a `pn` function's body is a content block (declarations + statements + trailing expression), the transpiler now avoids constructing a temporary list. Instead, it evaluates declarations and statements for side effects, and only boxes the final value expression as the block result. This eliminates unnecessary `list()`/`list_end()` runtime calls in procedural code.

#### Benchmark Results After Phase 2

Full comparison of MIR Direct vs C2MIR execution time (debug build, macOS arm64, 60s timeout):

| Benchmark | MIR Direct (s) | C2MIR (s) | Speedup | Status |
|-----------|----------------|-----------|---------|--------|
| **sum** | 0.02 | 4.09 | **205x** | MIR dramatically faster |
| **ack** | 0.05 | 5.77 | **115x** | MIR dramatically faster |
| **diviter** | 0.31 | 26.14 | **84x** | MIR dramatically faster |
| **sumfp** | 0.02 | 1.05 | **53x** | MIR dramatically faster |
| **divrec** | 0.04 | 2.03 | **51x** | MIR dramatically faster |
| **collatz** | 0.57 | T/O | **>100x** | C2MIR times out |
| **quicksort** | 1.33 | 3.11 | **2.3x** | MIR faster |
| **fibfp** | 3.89 | 5.09 | **1.3x** | MIR faster |
| fib | 1.35 | 1.29 | ~1.0x | Comparable |
| tak | 0.02 | 0.02 | ~1.0x | Comparable |
| cpstak2 | 0.03 | 0.02 | ~1.0x | Comparable |
| nqueens | 1.06 | 1.01 | ~1.0x | Comparable |
| mbrot | 7.66 | 7.38 | ~1.0x | Comparable |
| array1 | 6.18 | 5.97 | ~1.0x | Comparable |
| pnpoly | 31.60 | 31.48 | ~1.0x | Comparable |
| larceny/primes | 0.68 | 0.64 | ~1.0x | Comparable |
| kostya/primes | 8.91 | 9.46 | ~1.0x | Comparable |
| deriv | 3.55 | 2.88 | 0.8x | MIR slightly slower |
| brainfuck | T/O | 40.56 | <1.0x | MIR regression (see below) |
| triangl | T/O | T/O | — | Both timeout |
| matmul | T/O | T/O | — | Both timeout (debug build) |

**Summary:**
- **8 benchmarks with significant MIR speedups** (1.3x–205x)
- **9 benchmarks at parity** (~1.0x)
- **1 slight regression** (deriv at 0.8x — symbolic expression manipulation, overhead from boxing in polymorphic paths)
- **1 meaningful regression** (brainfuck — see below)
- **2 timeouts in both modes** (triangl, matmul — debug build overhead)

**Baseline tests**: 605/605 pass (155/155 MIR tests).

#### Known Regression: brainfuck

The brainfuck benchmark (BF interpreter with heavy array mutation in tight loops) is slower in MIR direct. Root cause: type inference correctly narrows loop variables to native `INT`, but every array read/write (`tape[dp]`, `jumps[ip]`) still goes through the boxed `fn_index`/`fn_array_set` runtime calls, which require boxing the native int index back to a boxed `Item` each time. The net effect is that the boxing/unboxing overhead for array access exceeds the arithmetic speedup from native int operations.

This is an inherent tradeoff in Phase 2's approach: native arithmetic dramatically speeds up compute-heavy loops, but array-access-heavy loops pay a per-access boxing tax. Phase 2.3 (inline array access) would address this but is not yet implemented.

#### Analysis: Where the Speedups Come From

The massive speedups (50–200x) on sum, ack, diviter, divrec, sumfp, collatz all share the same pattern: **tight recursive or iterative loops with pure arithmetic on untyped parameters**. Before Phase 2, every `+`, `-`, `*`, `/`, `%`, `<`, `>` operation went through a boxed runtime function call (~20–50ns each). After inference narrows parameters to native INT/FLOAT, these become single MIR instructions (~1ns each).

The C2MIR path is slower on these benchmarks because the generated C code still uses boxed `Item` representations and calls `fn_add`/`fn_sub` — the C compiler cannot see through the runtime boxing to optimize the arithmetic. The MIR direct transpiler, having full AST visibility, can bypass boxing entirely.

Benchmarks at parity (fib, tak, nqueens, mbrot) already had typed parameters in their original scripts, so inference provided no additional benefit.

---

## 4. Phase 3 — Direct Map/Struct Field Access

### Problem

Every `map.field` and `obj.field` access in the MIR transpiler goes through a runtime hash-lookup call:

```cpp
// Current: transpile_member() at line 3484
MIR_reg_t result = emit_call_2(mt, "fn_member", MIR_T_I64,
    MIR_T_I64, boxed_obj, MIR_T_I64, boxed_field);
```

The C transpiler, by contrast, uses **direct byte-offset loads** for maps with known shapes — O(1) pointer arithmetic vs O(n) hash lookup.

### C Transpiler's Approach

The C transpiler's `emit_direct_field_read()` (transpile.cpp:4794) generates:
```c
// For point.x where x is int at byte_offset=0:
*(int64_t*)((char*)(point)->data + 0)
```

**Eligibility check** (`has_fixed_shape()` at transpile.cpp:4682):
- Map must have a `struct_name` (from a named type definition: `type Point = {x: int, y: float}`)
- All fields must be named (no spread entries)
- All `byte_offset` values must be 8-byte aligned

### MIR Implementation Plan

#### 4.1 Shape Eligibility Check

Add a new function `mir_has_fixed_shape()` that replicates the C transpiler's logic:

```cpp
static bool mir_has_fixed_shape(Type* type) {
    if (!type) return false;
    TypeId tid = type->type_id;
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_OBJECT && tid != LMD_TYPE_ELEMENT) return false;
    TypeMap* map_type = (TypeMap*)type;
    if (!map_type->struct_name) return false;  // anonymous map literal
    ShapeEntry* field = map_type->shape;
    while (field) {
        if (!field->name) return false;  // spread entry
        if (field->byte_offset % sizeof(void*) != 0) return false;  // misaligned
        field = field->next;
    }
    return true;
}
```

#### 4.2 Direct Field Read via MIR Memory Operations

When `transpile_member()` encounters a field access on a fixed-shape map/object, emit inline MIR instructions instead of calling `fn_member`:

```
// Pseudo-MIR for point.x where x: int, byte_offset=0
//
// Step 1: Get Container* from boxed Item (strip tag bits)
container_ptr = obj_item & 0x00FFFFFFFFFFFFFF   // mask off TypeId tag
//
// Step 2: Load data pointer from Container struct
// Container layout: { TypeId type; int count; Item* data; }
data_ptr = load [container_ptr + offsetof(Container, data)]
//
// Step 3: Load typed value at field's byte_offset
value = load_i64 [data_ptr + 0]    // byte_offset=0, type=int64_t
```

In MIR API terms:

```cpp
// Untag the Item to get Container*
MIR_reg_t ptr = new_reg(mt, "cptr", MIR_T_I64);
emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND,
    MIR_new_reg_op(mt->ctx, ptr),
    MIR_new_reg_op(mt->ctx, boxed_obj),
    MIR_new_uint_op(mt->ctx, 0x00FFFFFFFFFFFFFFULL)));

// Load Container.data pointer (offset depends on struct layout)
MIR_reg_t data = new_reg(mt, "data", MIR_T_P);
emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
    MIR_new_reg_op(mt->ctx, data),
    MIR_new_mem_op(mt->ctx, MIR_T_P, offsetof(Container, data), ptr, 0, 1)));

// Load field value at byte_offset
MIR_type_t field_mir_type = type_to_mir(field->type->type_id);  // MIR_T_I64, MIR_T_D, etc.
MIR_reg_t value = new_reg(mt, "fval", field_mir_type);
emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
    MIR_new_reg_op(mt->ctx, value),
    MIR_new_mem_op(mt->ctx, field_mir_type, field->byte_offset, data, 0, 1)));
```

#### 4.3 Direct Field Write

For assignment (`obj.x = value`), emit the reverse:

```
// Pseudo-MIR for obj.x = value (int field at byte_offset=0)
container_ptr = obj_item & MASK
data_ptr = load [container_ptr + offsetof(Container, data)]
store_i64 [data_ptr + 0] = value
```

When the value being assigned is already a native type matching the field type, store directly. When it's a boxed Item, unbox first.

#### 4.4 Method Body Field Loading

**Current** (transpile-mir.cpp:5753–5787): Each field loaded via `fn_member()` + `heap_create_symbol()` — two function calls per field.

**Optimized**: Load `self->data` once, then read each field by byte offset:

```
// Method prologue: load all fields from self via direct offset
self_container = self_item & MASK
self_data = load [self_container + offsetof(Container, data)]
field_x = load_i64 [self_data + 0]    // x: int at offset 0
field_y = load_f64 [self_data + 8]    // y: float at offset 8
```

This eliminates N `fn_member` calls and N `heap_create_symbol` calls per method invocation.

#### 4.5 Fallback

When `mir_has_fixed_shape()` returns false (anonymous maps, computed keys, spread entries), fall through to the existing `fn_member()` call path. No behavior change for dynamic maps.

---

## 5. Phase 4 — Tail Call Optimization (TCO)

### Problem

Recursive functions like `factorial`, `fibonacci`, tree traversals, and `collatz`-style loops will overflow the stack for deep recursion. The C transpiler already implements TCO via `goto`-based loop transformation. The MIR transpiler has TCO struct fields (`tco_func`, `tco_label`) but they are never used.

### C Transpiler's TCO Pattern

The C transpiler transforms tail-recursive calls into goto-based loops:

```c
// Original Lambda:  fn factorial(n: int, acc: int): int = if n <= 1 then acc else factorial(n-1, n*acc)
//
// Generated C:
int64_t factorial(int64_t n, int64_t acc) {
    int tco_count = 0;
  tco_start:;
    if (++tco_count > 1000000) { lambda_stack_overflow_error("factorial"); return 0; }
    if (n <= 1) return acc;
    { int64_t tmp0 = n - 1; int64_t tmp1 = n * acc;
      n = tmp0; acc = tmp1;
      goto tco_start;
    }
}
```

### TCO Eligibility

Reuse the existing `should_use_tco()` from `safety_analyzer.cpp` (line 183):
- Function must be **named** (not anonymous)
- Function must **not be a closure** (no captured environment)
- Function body contains at least one **tail-recursive call** (detected by `has_tail_call()`)

### MIR Implementation Plan

#### 5.1 Detection and Setup

In `transpile_user_func()`, after creating the MIR function but before transpiling the body:

```cpp
bool use_tco = should_use_tco(fn_node);
if (use_tco) {
    mt->tco_func = fn_node;
    mt->tco_label = new_label(mt);

    // Emit iteration counter as local variable
    MIR_reg_t tco_count = new_reg(mt, "tco_count", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, tco_count),
        MIR_new_int_op(mt->ctx, 0)));

    // Emit label
    emit_label(mt, mt->tco_label);

    // Emit guard: if ++tco_count > LAMBDA_TCO_MAX_ITERATIONS goto overflow
    // ... increment, compare, branch to error call ...
}
```

#### 5.2 Tail Position Tracking

Add `in_tail_position` flag to `MirTranspiler`:

```cpp
bool in_tail_position;  // current expression is in tail position
```

Propagation rules (matching C transpiler's logic):
- **Function body**: `in_tail_position = true` (body result is the return value)
- **If-expression**: condition → `false`; then/else branches → inherit parent
- **Match-expression**: each arm body → inherit parent; patterns → `false`
- **List/block**: last item → inherit parent; all others → `false`
- **Call arguments**: always `false`
- **Return statement (proc)**: return value → `true`
- **Binary/unary operands**: always `false`

#### 5.3 Tail Call Transformation

In `transpile_call()`, when the callee is a self-recursive call and `mt->in_tail_position == true`:

```cpp
if (mt->tco_func && mt->in_tail_position && is_recursive_call(call_node, mt->tco_func)) {
    // 1. Evaluate all arguments into temporaries
    MIR_reg_t temps[MAX_PARAMS];
    for (int i = 0; i < arg_count; i++) {
        temps[i] = transpile_expr(mt, args[i]);
    }

    // 2. Assign temporaries to parameter registers (two-pass prevents aliasing)
    for (int i = 0; i < arg_count; i++) {
        MIR_reg_t param_reg = lookup_param_reg(mt, fn_node->param, i);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, param_reg),
            MIR_new_reg_op(mt->ctx, temps[i])));
    }

    // 3. Jump back to tco_label
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, mt->tco_label)));

    // 4. Return a dummy register (unreachable, but MIR needs a value for the expression)
    return new_reg(mt, "tco_unreachable", MIR_T_I64);
}
```

#### 5.4 Parameter Aliasing Prevention

Critical: When a tail call passes `factorial(b, a)` (swapping parameters), simple sequential assignment would corrupt values. The two-pass approach (evaluate all args to temps, then assign all temps to params) prevents this.

For typed parameters, handle boxing/unboxing:
- If param expects native `INT` but arg produces boxed `Item` → unbox to temp
- If param expects boxed `Item` but arg produces native `INT` → box to temp

#### 5.5 Cleanup

After transpiling the function body, reset TCO state:
```cpp
mt->tco_func = NULL;
mt->in_tail_position = false;
```

---

## 6. Implementation Roadmap

### Phase 1: Bug Fixes (Estimated: 1–2 days)

| Task | File | Lines | Description |
|------|------|-------|-------------|
| 1a | transpile-mir.cpp | ~2030 | Fix Bool result boxing for comparison operators |
| 1b | transpile-mir.cpp | ~5806 | Add `block_returned` flag, skip dead code after return |
| 1c | transpile-mir.cpp | ~5726 | Interim type narrowing for all-integer-context params |

**Validation**: All 4 failing benchmarks (collatz, diviter, pnpoly, ray) pass. No regression in existing 85 baseline tests.

### Phase 2: Type Narrowing (Estimated: 2–3 days)

| Task | File | Description |
|------|------|-------------|
| 2a | transpile-mir.cpp | Add pre-pass function body scanner for param type inference |
| 2b | transpile-mir.cpp | Implement speculative unboxing with type guard at function entry |
| 2c | transpile-mir.cpp | Propagate narrowed types through `get_effective_type()` |
| 2d | transpile-mir.cpp | Optimize array index access for typed arrays |

**Validation**: Execution time ratios should drop below 2.0x for all benchmarks vs C2MIR path.

### Phase 3: Direct Map Access (Estimated: 2–3 days)

| Task | File | Description |
|------|------|-------------|
| 3a | transpile-mir.cpp | Add `mir_has_fixed_shape()` eligibility check |
| 3b | transpile-mir.cpp | Implement `emit_direct_field_read()` with MIR memory ops |
| 3c | transpile-mir.cpp | Implement `emit_direct_field_write()` |
| 3d | transpile-mir.cpp | Optimize method body field loading (eliminate `fn_member` + `heap_create_symbol`) |

**Validation**: Run struct-heavy benchmarks. Field access should be O(1). No regression in dynamic map tests.

### Phase 4: Tail Call Optimization (Estimated: 2–3 days)

| Task | File | Description |
|------|------|-------------|
| 4a | transpile-mir.cpp | Add `in_tail_position` flag and propagation logic |
| 4b | transpile-mir.cpp | Integrate `should_use_tco()` into `transpile_user_func()` |
| 4c | transpile-mir.cpp | Implement tail call transformation with temp-based parameter swap |
| 4d | transpile-mir.cpp | Add iteration counter guard (`LAMBDA_TCO_MAX_ITERATIONS`) |

**Validation**: Tail-recursive factorial(1000000) completes without stack overflow. TCO-specific unit tests pass.

---

## 7. Key Code Locations Reference

| Area | File | Lines | Function/Struct |
|------|------|-------|-----------------|
| MIR transpiler struct | transpile-mir.cpp | 100–160 | `MirTranspiler` |
| TCO fields (unused) | transpile-mir.cpp | 140–142 | `tco_func`, `tco_label` |
| Binary expression dispatch | transpile-mir.cpp | 1650–2035 | `transpile_binary()` |
| Effective type inference | transpile-mir.cpp | 1530–1610 | `get_effective_type()` |
| Member access (runtime) | transpile-mir.cpp | 3484–3541 | `transpile_member()` |
| User function transpilation | transpile-mir.cpp | 5600–5800 | `transpile_user_func()` |
| Method field loading | transpile-mir.cpp | 5753–5787 | within `transpile_user_func()` |
| Entry point | transpile-mir.cpp | 6495–6730 | `run_script_mir()` |
| TCO eligibility (reuse) | safety_analyzer.cpp | 183–213 | `should_use_tco()` |
| Tail position detection (reuse) | safety_analyzer.cpp | 97–167 | `has_tail_call()` |
| Shape system | lambda-data.hpp | 185–202 | `ShapeEntry`, `TypeMap` |
| C direct field read (reference) | transpile.cpp | 4794–4856 | `emit_direct_field_read()` |
| C direct field write (reference) | transpile.cpp | 4895–4980 | `emit_direct_field_write()` |
| C TCO prologue (reference) | transpile.cpp | 5424–5486 | `define_func()` |
| C tail call xform (reference) | transpile-call.cpp | 289–410 | `transpile_tail_call()` |
| Container struct layout | lambda-data.hpp | ~150 | `Container { TypeId type; int count; Item* data; }` |

---

## 8. Testing Strategy

### Unit Tests

Each phase should produce targeted test scripts:

```
test/mir/test_mir_bool_boxing.ls      — Phase 1a: comparison result used in expressions
test/mir/test_mir_dead_code.ls        — Phase 1b: return followed by dead code
test/mir/test_mir_untyped_arith.ls    — Phase 1c/2: untyped params in arithmetic
test/mir/test_mir_struct_access.ls    — Phase 3: named type field read/write
test/mir/test_mir_tco.ls             — Phase 4: tail-recursive functions
```

### Benchmark Regression

After each phase, re-run the full 18-benchmark suite with:
```bash
LAMBDA_PROFILE=1 ./lambda.exe run --mir <benchmark>.ls
```

Track JIT overhead (should remain ≤3ms avg) and execution time (should converge toward C2MIR numbers).

### Baseline Tests

All 85 existing MIR baseline tests must continue passing after each phase. Run:
```bash
make test-lambda-baseline  # (with --mir flag)
```

