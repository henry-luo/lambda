# Transpile_Js16: Crypto Performance & Closure Mutation Bug Fix

## 1. Executive Summary

crypto_spec is the last remaining PDF.js spec with failures.
Analysis reveals two independent root causes:

| Issue | Tests Affected | Root Cause |
|-------|:-:|---|
| Closure mutation not visible across nesting levels | 26 (all failures) | Transpiler creates independent scope_envs per nesting level — grandchild closures snapshot stale values |
| AES/SHA hot loop 285× slower than V8 | 4 (PDF20Algorithm) | Unoptimized typed array ops, per-iteration object allocation, interpreter-level overhead |

| Metric | Before v16 | After v16 Phase 1 | After v16 Phase 2 | Target | Node.js |
|--------|:-:|:-:|:-:|:-:|:-:|
| crypto_spec pass rate | 33/59 (56%) | 40/59 (68%) | **54/59 (92%)** | 59/59 (100%) | 59/59 |
| Non-crypto pdfjs tests | 3,680/3,680 | 3,680/3,680 | **3,680/3,680** | No regressions | — |
| Lambda baseline | 735/752 | 735/752 | **735/752** | No regressions | — |
| Total runtime | 142.5s | TBD | TBD | < 10s | 0.5s |

**v16 Phase 1 results (closure fix): +7 tests passing, 0 regressions.**
**v16 Phase 2 results (Phase 1.7b ordering fix): +14 tests passing, 0 regressions.**

Remaining 5 failures:
- 5 CipherTransformFactory > Encrypt and decrypt: `Error: Invalid argument for bytesToString`

### Impact Beyond crypto_spec

The closure mutation bug affects ANY code pattern where:
- A variable is declared in scope N
- Assigned by a closure at depth N+1 (e.g., `beforeAll`, event handler, callback)
- Read by a closure at depth N+2 (e.g., `it()` inside nested `describe()`)

This is a common pattern in test frameworks (Jasmine, Mocha, Jest), state machines,
and event-driven architectures. Fixing it improves correctness across all JS workloads.

---

## 2. Bug Analysis: Closure Mutation Across Nesting Levels

### 2.1 The Bug

```js
function outer() {
    var x;                          // scope level N
    function setter() { x = 42; }  // depth N+1: captures x, writes to outer.scope_env[0]
    function middle() {             // depth N+1: captures x (transitively, for reader)
        function reader() {         // depth N+2: captures x from middle.scope_env[0]
            return x;
        }
        return reader;
    }
    var reader = middle();
    setter();           // x = 42 → writes to outer.scope_env[0] ✅
    console.log(x);     // 42 ✅ (direct read from outer's register, reloaded from scope_env)
    reader();           // undefined ❌ (reads from middle.scope_env[0], which was snapshot at middle() entry)
}
```

**Expected:** `reader()` returns `42`  
**Actual:** `reader()` returns `undefined`

### 2.2 Transpiler Root Cause

The bug is in Phase 1.7 scope_env construction and the function entry env-to-register loading.

**Phase 1.6** (transitive capture propagation, line ~15282) correctly adds `x` to `middle()`'s
captures because `reader()` needs it. This is correct.

**Phase 1.7** (scope_env construction, line ~15353) then creates:
- `outer.scope_env = [x]` (slot 0) — because both `setter` and `middle` capture `x`
- `middle.scope_env = [x]` (slot 0) — because `reader` captures `x`

These are **two independent arrays**. At runtime:

1. `outer()` entry: allocates `outer.scope_env`, stores initial `x` (undefined) in slot 0
2. `setter()` created: gets `outer.scope_env` as its env (shared pointer) ✅
3. `middle()` created: gets `outer.scope_env` as its env (shared pointer) ✅
4. `middle()` called: loads `x` from `env[0]` into **local register `cap_x`** (line ~13955)
5. `middle()` then allocates its **own** `middle.scope_env`, writes `cap_x` into slot 0
6. `reader()` created inside middle: gets `middle.scope_env` as its env
7. `setter()` called: writes 42 to `outer.scope_env[0]` ✅
8. `reader()` called: reads from `middle.scope_env[0]` → still `undefined` ❌

**The fundamental problem:** Each nesting level creates its own scope_env, copying captured
values at function entry time rather than passing through the parent's scope_env.

### 2.3 Affected Test Pattern

```js
describe("CipherTransformFactory", function() {
    let dict1, fileId1;                    // scope level N
    beforeAll(function() {                 // depth N+1
        dict1 = buildDict({...});          // writes to parent's scope_env ✅
        fileId1 = unescape("...");
    });
    describe("#ctor", function() {         // depth N+1 (intermediate function)
        it("should accept user password", function() {  // depth N+2
            ensurePasswordCorrect(dict1, fileId1, "123456");
            // dict1 is undefined here ❌
        });
    });
});
```

All 26 CipherTransformFactory failures stem from this single bug. `dict1`, `fileId1`,
`aes256Dict`, etc. are all `undefined` inside the nested `describe/it` closures even
though `beforeAll` correctly assigned them.

### 2.4 Proposed Fix

**Approach: Pass-through scope_env for transitively captured variables**

When `middle()` captures `x` only because its child `reader()` needs it (transitive capture),
`middle()` should NOT create a separate scope_env slot for `x`. Instead, it should pass
`outer.scope_env` (or the relevant slot) directly to `reader()`'s env.

#### Option A: Env Chaining (Recommended)

Instead of snapshot-copying captured vars into a new scope_env, have the child closure's
env contain a **pointer to the parent's scope_env** plus any locally-originated vars:

```
reader.env = [&outer.scope_env, local_var_1, local_var_2]
                    ↑
                    slot 0 is a pointer to parent env, not a value
```

When `reader()` reads `x`:
1. Check if `x` maps to a "parent env indirect" slot
2. Load from `outer.scope_env[x_slot]` at runtime (two-level dereference)

**Pros:** Zero snapshot overhead, mutations always visible, handles arbitrary nesting depth.  
**Cons:** Requires a new env slot type (indirect) and runtime double-dereference.

#### Option B: Flat Env with Direct Parent Pointer

When `middle()` is called and creates `reader()`, the env builder for `reader()` should
store values from `middle()`'s env (which IS `outer.scope_env`) rather than from `middle()`'s
local registers:

Currently (line ~9065):
```cpp
// BUG: reads from middle()'s register (stale snapshot)
MIR_append_insn(ctx, cur_func,
    MIR_new_insn(ctx, MIR_MOV, MIR_new_mem_op(ctx, MIR_T_I64, slot * 8, env_reg, 0, 1),
                 MIR_new_reg_op(ctx, cap_entry.reg)));
```

Fix: when the capture originated from a parent's scope_env (not locally declared):
```cpp
// FIX: reads from middle()'s env (which points to outer.scope_env)
MIR_append_insn(ctx, cur_func,
    MIR_new_insn(ctx, MIR_MOV, tmp_reg,
                 MIR_new_mem_op(ctx, MIR_T_I64, parent_slot * 8, cap_entry.env_reg, 0, 1)));
MIR_append_insn(ctx, cur_func,
    MIR_new_insn(ctx, MIR_MOV, MIR_new_mem_op(ctx, MIR_T_I64, slot * 8, env_reg, 0, 1),
                 tmp_reg));
```

**Pros:** Simpler change, no new runtime concept.  
**Cons:** Still a snapshot (but taken at child-creation time, not parent-entry time). Fails if
the variable is mutated AFTER the child closure is created but BEFORE it's called. However,
this covers the `beforeAll`/`describe`/`it` pattern since `beforeAll` runs before child tests.

#### Option C: Single Shared Scope Env (Simplest)

For transitively captured vars, skip creating `middle.scope_env` entries entirely. Instead,
when `reader()` is created inside `middle()`, build its env using values from `middle()`'s
own env (the parent's scope_env) instead of `middle()`'s registers:

In `jm_create_func_or_closure()` / `jm_transpile_func_expr()`, when populating the closure
env and the current function's variable is `from_env`:
```cpp
if (v.from_env) {
    // Read LIVE from parent's scope_env instead of local register
    MIR_append_insn(ctx, cur_func,
        MIR_new_insn(ctx, MIR_MOV, env_slot,
                     MIR_new_mem_op(ctx, MIR_T_I64, v.env_slot * 8, v.env_reg, 0, 1)));
} else {
    MIR_append_insn(ctx, cur_func, MIR_new_insn(ctx, MIR_MOV, env_slot, v.reg));
}
```

**This is the recommended minimal fix.** When the variable `x` in `middle()` was loaded from
its parent env (i.e., `from_env == true`), building `reader()`'s env should re-read from
`middle()`'s env pointer (which IS `outer.scope_env`) at closure-creation time, not from
`middle()`'s local register.

Combined with the `use_scope_env` path (which passes `mt->scope_env_reg` directly instead of
copying), this ensures all mutations are visible.

### 2.5 Implementation Plan

See §2.6 for actual implementation.

### 2.6 Implementation (Completed)

The initial Option C (re-read from parent env) was insufficient. The fix evolved into a
**3-part approach** after discovering that Phase 2 emits innermost functions first:

#### Part 1: Phase 1.7b — Parent Env Reuse Detection (pre-Phase 2)

Added a new pass after Phase 1.7 that detects when ALL of a function's scope_env variables
are also in its own captures (transitive captures from grandparent). When detected:
- Set `fc->reuse_parent_env = true` and `fc->reuse_env_slot_count`
- Remap children's `captures[k].scope_env_slot` to grandparent's slot positions

**Critical insight:** This MUST happen in Phase 1.7b (before Phase 2), because Phase 2
emits functions in `func_entries[]` order — innermost first. If remapping happens during
body emission (as initially attempted), child bodies are already compiled with stale slots.

#### Part 2: Body Emission — Reuse Parent Env

When `fc->reuse_parent_env` is set during body emission:
- Set `mt->scope_env_reg = env_reg` (parent's env), skip `js_alloc_env()`
- Set `mt->scope_env_slot_count = fc->reuse_env_slot_count`
- Mark vars for write-back using remapped slot positions
- Fixed `jm_scope_env_mark_and_writeback()` to use `var->env_slot` when `reuse_parent_env`

#### Part 3: Fallback — Live Reads for Mixed Scope Envs

When not all vars are transitive (mixed local + captured), still allocate a new scope_env
but use live reads from parent env for `from_env` variables:
```cpp
if (svar->from_env) {
    val = jm_new_reg(mt, "senv_live", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, val,
        MIR_new_mem_op(mt->ctx, MIR_T_I64, svar->env_slot * sizeof(uint64_t), svar->env_reg, 0, 1)));
}
```

Same live-read fix applied to `jm_create_func_or_closure()` and `jm_transpile_func_expr()`
fallback (per-closure env) paths.

#### Struct Changes

Added to `JsFuncCollected`:
```cpp
bool reuse_parent_env;       // v16: true if scope_env reuses parent env
int reuse_env_slot_count;    // v16: slot count when reusing parent env
```

#### Results

| Test Suite | Before | After | Delta |
|-----------|:-:|:-:|:-:|
| crypto_spec | 33/59 | **40/59** | **+7** |
| Non-crypto pdfjs | 3,680/3,680 | 3,680/3,680 | 0 |
| Lambda baseline | 735/752 | 735/752 | 0 |

Test patterns verified:
- Pattern A: Post-assign (outer > setter + directReader) ✅
- Pattern B: Direct nested closure read ✅
- Pattern C: Callback registration + nested closure ✅
- Pattern D: Object property mutation ✅

### 2.7 Phase 2: Phase 1.7b Iteration Order Fix

**Bug**: Phase 1.7b iterated `func_entries[]` from index 0 to max. In the Lambda JS
transpiler, inner closures have **lower** indices than their parents (depth-first
collection). This caused Phase 1.7b to process children BEFORE parents, reading stale
`scope_env_slot` values that hadn't been remapped yet.

**Example**: With 3-level nesting (Outer > level2 > level3a > it_callback):
```
func_entries[4] = it_d3a (depth 3, innermost)
func_entries[5] = level3a (depth 2)
func_entries[11] = level2 (depth 1)
func_entries[12] = Outer (depth 0)
```

Phase 1.7b at fi=5 (level3a) reads `level3a->captures[c].scope_env_slot` to get
"grandparent" slots for remapping it_d3a's captures. But at fi=5, level2's Phase 1.7b
(fi=11) hasn't run yet, so level3a's captures still hold level2's *local* scope_env
positions instead of Outer's *root* positions.

Result: all slot indices mapped to the intermediate function's ordering, not the root
env's ordering. For 10 variables, this manifested as a systematic scramble (e.g., v6
reading slot 5 instead of 4, fn0 reading slot 1 instead of 9).

**Fix**: Reverse the Phase 1.7b iteration order:
```cpp
// BEFORE (broken): inner closures processed first
for (int fi = 0; fi < mt->func_count; fi++) { ... }

// AFTER (fixed): outer closures processed first
for (int fi = mt->func_count - 1; fi >= 0; fi--) { ... }
```

This ensures parents are always processed before children, so children read correctly
remapped grandparent slots.

**Reproduction test**: `temp/test_closure_depth.js` — 6 functions + 9 let vars in outer
scope, deferred callback execution at depths 1-4. Before fix: depth 3+ showed scrambled
values. After fix: all depths read correctly.

#### Results

| Test Suite | Before | After | Delta |
|-----------|:-:|:-:|:-:|
| crypto_spec | 40/59 | **54/59** | **+14** |
| Non-crypto pdfjs | 3,680/3,680 | 3,680/3,680 | 0 |
| JS baseline | 67/72 | 67/72 | 0 |

---

## 3. Performance Analysis: 285× Slowdown

### 3.1 Timing Breakdown

| Test | Lambda (s) | % of Total |
|------|:-:|:-:|
| PDF20 > check user key | 27.3 | 19% |
| PDF20 > check owner key | 38.5 | 27% |
| PDF20 > gen file key (user) | 29.4 | 21% |
| PDF20 > gen file key (owner) | 47.4 | 33% |
| **All other 55 tests** | **< 0.1** | **~0%** |
| **Total** | **142.6** | 100% |

Node.js completes all 59 tests (including these 4) in **0.5s**.

### 3.2 Hot Path: `PDF20._hash()`

```js
_hash(password, concatBytes, userBytes) {
    let k = calculateSHA256(new Uint8Array([...password, ...concatBytes]));
    let e;
    let i = 0;
    while (true) {
        // Inner loop: 64+ iterations
        const combinedLength = password.length + k.length + userBytes.length;
        const combinedArray = new Uint8Array(combinedLength);
        combinedArray.set(password, 0);
        combinedArray.set(k, password.length);
        combinedArray.set(userBytes, password.length + k.length);

        // Create a large buffer: combined × 64
        const k1 = new Uint8Array(combinedLength * 64);
        for (let j = 0; j < 64; j++) {
            k1.set(combinedArray, j * combinedLength);
        }

        // AES-128-CBC encrypt the entire k1 buffer
        const cipher = new AES128Cipher(k.subarray(0, 16));
        e = cipher.encrypt(k1, k.subarray(16, 32));

        // SHA hash the result
        const remainder = ...;
        if (remainder === 0) k = calculateSHA256(e);
        else if (remainder === 1) k = calculateSHA384(e);
        else k = calculateSHA512(e);

        if (i >= 63 && e.at(-1) <= i - 32) break;
        i++;
    }
}
```

**Per iteration (minimum 64 iterations):**
1. `new Uint8Array(combinedLength)` + 3× `.set()` — allocate + copy ~80 bytes
2. `new Uint8Array(combinedLength * 64)` — allocate ~5120 bytes
3. 64× `.set(combinedArray, offset)` — copy 80 bytes at varying offsets
4. `new AES128Cipher(k.subarray(0, 16))` — construct cipher, expand key (10 rounds × 4 words)
5. `cipher.encrypt(k1, iv)` — CBC-encrypt 320 blocks × 10 AES rounds each
6. `calculateSHA256/384/512(e)` — hash 5120 bytes

### 3.3 Performance Bottleneck Analysis

| Operation | Estimated % | V8 Advantage |
|-----------|:-:|---|
| AES128._encrypt() (S-box + MixColumns) | ~50% | JIT-optimizes tight loops, SIMD potential |
| Uint8Array allocation + .set() | ~20% | Generational GC, fast bump allocation, memcpy optimization |
| AES128 key expansion (constructor) | ~10% | Inline-caches constructor, avoids re-expanding |
| SHA256/384/512 computation | ~10% | Tight integer math loop optimization |
| Object/closure overhead | ~10% | Escape analysis, hidden class transitions |

### 3.4 Optimization Proposals

#### P1: Typed Array `.set()` Fast Path (High Impact, Low Effort)

Currently, `Uint8Array.set(source, offset)` likely goes through generic property access.
Implement a runtime fast path that:
- Detects both source and target are typed arrays
- Calls `memcpy(&target.buffer[offset], source.buffer, source.length)`

**Expected speedup:** 3–5× on the copy-heavy `k1` construction loop.

#### P2: Avoid Re-Creating AES128Cipher Inside Loop (Medium Impact)

Each iteration of `_hash()` creates `new AES128Cipher(k.subarray(0, 16))` which runs
key expansion. The key changes each iteration (it's derived from the hash), so caching
the cipher isn't possible. However, the key expansion itself can be optimized:

- Pre-allocate the `_key` Uint8Array in the class (reuse across calls)
- Avoid `new Uint8Array()` inside `_expandKey()`
- Use pre-computed byte-level operations instead of dynamic indexing

**Expected speedup:** 1.2–1.5× on AES operations.

#### P3: AES S-Box Lookup Optimization (High Impact, Medium Effort)

`AESBaseCipher._encrypt()` performs ~3,200 S-box lookups per 16-byte block (10 rounds ×
16 bytes × SubBytes + MixColumns). These are indexed reads from `Uint8Array` S-box tables.

Optimization: Store S-box tables as **regular JS arrays** (which Lambda can optimize to
native int arrays) or as **C-level uint8_t arrays** accessed through a specialized runtime
function, avoiding the typed array indirection overhead.

Alternatively, add a **"known constant typed array" optimization** in the transpiler:
detect when a typed array is initialized once and never modified, and compile indexed reads
as direct memory loads without bounds checking.

**Expected speedup:** 2–3× on AES core operations (the dominant cost).

#### P4: Loop-Invariant Typed Array Optimization (Medium Impact)

Inside `cipher.encrypt()`, the inner loop body accesses `this._s`, `this._key`, and
temporary typed arrays repeatedly. If the transpiler can prove these typed arrays don't
escape or alias, it can:
- Hoist the buffer pointer out of the loop
- Compile indexed access as `base[i]` without re-fetching the buffer each iteration

**Expected speedup:** 1.5–2× on the encrypt loop.

#### P5: SHA Hash Native Implementation (High Impact, Low Effort)

Replace the JS-level `calculateSHA256/384/512` with a C-level runtime function:
```c
extern "C" Item js_sha256(Item data_array);
extern "C" Item js_sha384(Item data_array);
extern "C" Item js_sha512(Item data_array);
```

Using mbedTLS (already linked) or a lightweight C implementation. This turns the SHA
computation from interpreted/JIT'd JS into optimized native code.

**Expected speedup:** 10–20× on SHA operations alone (~10% of total → ~1–2% of total).

#### P6: Batch `new Uint8Array` + `.set()` Pattern Recognition

Detect the pattern:
```js
const buf = new Uint8Array(size);
buf.set(a, 0);
buf.set(b, a.length);
buf.set(c, a.length + b.length);
```
And compile it as a single `concat_typed_arrays(a, b, c)` runtime call that avoids
intermediate allocations.

**Expected speedup:** Small overall, but reduces GC pressure.

### 3.5 Optimization Priority

| Priority | Optimization | Effort | Impact | Dependencies |
|:--------:|-------------|:------:|:------:|:------------:|
| 1 | P1: TypedArray.set() memcpy fast path | Low | High | None |
| 2 | P3: S-box lookup optimization | Medium | High | None |
| 3 | P5: SHA native implementation | Low | Medium | mbedTLS |
| 4 | P4: Loop-invariant typed array opt. | Medium | Medium | Transpiler analysis |
| 5 | P2: Key expansion optimization | Low | Low | None |
| 6 | P6: Batch allocation recognition | High | Low | Pattern detection |

**Realistic target with P1+P3+P5:** 10–15× speedup → ~10s total (from 142.5s).  
**Stretch target with all optimizations:** 20–30× → ~5–7s total.  
**V8 parity (0.5s)** requires fundamental JIT improvements beyond crypto-specific opts.

---

## 4. Additional Findings

### 4.1 `typeof unescape` Returns `undefined`

`unescape()` works correctly but `typeof unescape` returns `undefined`, suggesting it's
implemented as a special form rather than a proper function binding. This doesn't affect
crypto_spec but is a minor spec compliance issue.

### 4.2 `constructor` Property Not Set on Class Instances

```js
var Name = class _Name { ... };
var n = new Name();
n.constructor === Name;  // false (should be true)
```

The class expression `class _Name` creates the constructor, but the `constructor` property
on instances doesn't point to the outer binding `Name`. This is a pre-existing issue
documented elsewhere and doesn't affect crypto_spec (which uses `instanceof`, not
`.constructor`).

### 4.3 `Uint8Array.at(-1)` 

The PDF20._hash loop uses `e.at(-1)` to read the last byte. Verify this is implemented
correctly (should be equivalent to `e[e.length - 1]`). If not, it would cause an infinite
loop or wrong hash selection.

---

## 5. Implementation Phases

### Phase 1: Closure Fix ✅ COMPLETED
1. ✅ Added Phase 1.7b parent env reuse detection + child capture slot remapping
2. ✅ Body emission: reuse parent env when `reuse_parent_env` is set
3. ✅ Fallback: live reads from parent env for `from_env` vars in mixed scope_envs
4. ✅ Fixed `jm_scope_env_mark_and_writeback()` for reuse path
5. ✅ Verified: crypto_spec 33→40/59, 0 regressions across 3,680 non-crypto tests
6. Remaining: 19 crypto_spec failures (14 unnamed + 5 bytesToString errors) — likely
   separate bugs unrelated to closure mutation

### Phase 2: TypedArray.set() Fast Path
1. Add `memcpy`-based fast path in `js_typed_array_set()` runtime function
2. Detect same-type source/target, valid bounds, call `memcpy`
3. Benchmark PDF20 tests — expect ~3× speedup

### Phase 3: S-Box and SHA Optimization
1. Implement native SHA256/384/512 using mbedTLS or standalone C
2. Optimize AES S-box table access (native array or direct memory)
3. Benchmark — expect additional 3–5× speedup

### Phase 4: Advanced Typed Array Optimizations
1. Loop-invariant buffer pointer hoisting
2. Bounds check elimination for known-safe indices
3. Batch allocation pattern recognition

---

## 6. Test Plan

| Test | Expected Result | Actual Result |
|------|----------------|---------------|
| `temp/test_closure_depth.js` | var mutation visible at depth N+2 | ✅ PASS |
| `temp/test_closure_patterns.js` | All 4 patterns (A,B,C,D) pass | ✅ PASS |
| `temp/test_closure_c.js` | Callback + nested closure pattern | ✅ PASS |
| `crypto_spec` full run | 59/59 passing | 40/59 (19 remaining) |
| `crypto_spec` timing | < 10s (after Phase 2+3) | TBD |
| All other pdfjs specs | No regressions (3,680/3,680) | ✅ 3,680/3,680 |
| Lambda baseline tests | No regressions | ✅ 735/752 |

---

## 7. Files to Modify

| File | Changes | Status |
|------|---------|:------:|
| `lambda/js/transpile_js_mir.cpp` | Phase 1.7b reuse detection, body emission reuse, live reads, scope_env writeback fix | ✅ Done |
| `lambda/js/js_runtime.cpp` | TypedArray.set() fast path; native SHA256/384/512 | Pending |
| `lambda/js/js_runtime.h` | New runtime function declarations | Pending |
| `temp/test_closure_depth.js` | Test: 3-level closure mutation | ✅ Done |
| `temp/test_closure_patterns.js` | Test: 4 closure patterns (A,B,C,D) | ✅ Done |
| `temp/test_closure_c.js` | Test: callback + nested closure | ✅ Done |
