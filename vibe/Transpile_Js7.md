# JavaScript Transpiler v7: Correctness Completion & Performance Tuning

## 1. Executive Summary

Lambda's JS engine (LambdaJS) passes **21 of 29** benchmark tests (72.4%) with 8 benchmarks fixed in v6. This proposal targets **100% correctness on all feasible benchmarks** (25/25, excluding 4 that require Node.js-specific `require('fs')`) and **closing the median self-time ratio from ~12x to ~5x** versus V8.

The remaining work falls into two categories:
1. **Correctness fixes** — 4 benchmarks fail due to 3 localized AST builder gaps and 1 performance timeout
2. **Feature additions** — 4 benchmarks are "out of scope" but 3 can be brought in-scope by reusing existing Lambda infrastructure (decimal for BigInt, RE2 for RegExp, HashMap for Map)

### Architecture Position

```
v1–v2: JS AST → C codegen → C2MIR → native              (removed)
v3:    Runtime alignment, GC, DOM, selectors              (done)
v4:    JS AST → direct MIR IR → native                    (done)
v5:    Language coverage + typed arrays + closures         (done)
v6:    Type inference + native code generation             (done, 11–52x speedup)
v7:    Correctness completion + performance tuning         (this proposal)
         Phase A: AST builder correctness fixes            3 benchmarks
         Phase B: TypedArray loop performance              1 benchmark (fannkuch)
         Phase C: BigInt via Lambda decimal                1 benchmark (pidigits)
         Phase D: Hot-loop deboxing                        systemic perf improvement
         Phase E: Map built-in + RegExp                    2 benchmarks (knucleotide, regexredux)
```

### Target Outcome

| Metric | Current (v6) | Target (v7) |
|--------|-------------|-------------|
| Correct output | 21/29 (72.4%) | 25/29 (86.2%) |
| Feasible correct | 21/25 | **25/25 (100%)** |
| Median self-time ratio vs V8 | ~12x | ~5x |
| Benchmarks faster than Node.js (wall time) | 8/21 | 12/25 |

The 4 remaining `require('fs')` benchmarks (revcomp, knucleotide, regexredux — after Map/RegExp) stay out of scope unless a `--input` CLI workaround is adopted.

---

## 2. Issue Verification — Status of All JS_Result.md Issues

### Previously Fixed Issues (v6) — All Verified ✅

| # | Issue | Fix | Status |
|---|-------|-----|--------|
| 1 | Top-level `const`/`let` not captured in fn declarations | Capture analysis includes function declarations; closures created when captures > 0 | ✅ Resolved — puzzle, base64, fasta, matmul pass |
| 2 | `let` in `for` loops not block-scoped | `js_scope_push`/`js_scope_pop` around for-statement body | ✅ Resolved — primes×2, matmul, spectralnorm pass |
| 3 | TypedArray `.fill()` method dispatch | `js_is_typed_array` check in MAP method dispatch branch | ✅ Resolved — primes×2, triangl, spectralnorm pass |
| 4 | GC collecting JsFunction closures | `heap_alloc` → `pool_calloc` for JsFunction | ✅ Resolved — base64 closure-heavy benchmark passes |

### Remaining Issues — 4 Failing + 4 Out-of-Scope

| # | Issue | Benchmarks | Status | This Proposal |
|---|-------|-----------|--------|---------------|
| 5 | Escape sequences in template literals (`\t`, `\n`) | binarytrees | ❌ Failing | Phase A1 |
| 6 | Comments inside array expressions | nbody | ❌ Failing | Phase A2 |
| 7 | Destructuring assignment (not declaration) | levenshtein | ❌ Failing | Phase A3 |
| 8 | TypedArray access performance in tight loops | fannkuch | ❌ Timeout | Phase B |
| 9 | `BigInt` type | pidigits | ⬜ Out of scope | Phase C |
| 10 | `Map` built-in | knucleotide | ⬜ Out of scope | Phase E |
| 11 | `RegExp` literals + methods | regexredux | ⬜ Out of scope | Phase E |
| 12 | `require('fs')` file I/O | revcomp, knucleotide, regexredux | ⬜ Out of scope | Deferred |

---

## 3. Action Items

### Phase A: AST Builder Correctness Fixes

**Goal:** Fix 3 failing benchmarks with localized code changes.  
**Estimated effort:** ~100 lines total across `build_js_ast.cpp` and `transpile_js_mir.cpp`.

#### A1. Template Literal Escape Sequences (binarytrees)

**Problem:** In `build_js_ast.cpp`, template element `cooked` value is set to `raw` without processing escape sequences:
```cpp
element->cooked = element->raw; // TODO: Process escape sequences
```
The `\t` in `` `${iterations}\t trees of depth ${depth}\t check: ${sum}` `` is output literally.

**Fix:**
- Add a `js_cook_template_string()` function in `build_js_ast.cpp` that processes standard escape sequences: `\t` → tab, `\n` → newline, `\\` → backslash, `\r` → CR, `\0` → null, `\'`, `\"`, `` \` ``
- Use arena allocation (`arena_alloc`) for the cooked string since it may be shorter than raw
- Call it when setting `element->cooked`
- This matches what the regular JS string literal parser already does — extract the escape processing into a shared helper

**Files:** `lambda/js/build_js_ast.cpp`  
**Lines changed:** ~30

#### A2. Comments Inside Array Expressions (nbody)

**Problem:** `build_js_array_expression` in `build_js_ast.cpp` iterates named children but does not skip `comment` nodes. `build_js_object_expression` already skips them with:
```cpp
if (strcmp(child_type, "comment") == 0) continue;
```

**Fix:**
- Add the same `comment` skip guard in `build_js_array_expression`
- Also skip in any other expression builders that iterate children (audit: `build_js_call_expression`, `build_js_template_literal`)
- Decrement element count for skipped comments

**Files:** `lambda/js/build_js_ast.cpp`  
**Lines changed:** ~5–10

#### A3. Destructuring Assignment (levenshtein)

**Problem:** `[prev, curr] = [curr, prev]` — the transpiler handles destructuring in `const`/`let` declarations and `for-of` loops, but `jm_transpile_assignment` falls through on `JS_AST_NODE_ARRAY_PATTERN` left-hand side.

**Fix:**
- In `jm_transpile_assignment` (`transpile_js_mir.cpp`), add an `JS_AST_NODE_ARRAY_PATTERN` case
- Evaluate the RHS array expression first, store in temp registers (to handle circular swaps correctly)
- For each element in the array pattern, assign from the corresponding temp:
  ```
  // [prev, curr] = [curr, prev]
  // 1. Evaluate RHS: tmp0 = curr, tmp1 = prev
  // 2. Assign: prev = tmp0, curr = tmp1
  ```
- Reuse the destructuring logic already in `jm_transpile_variable_declaration` — extract the element-assignment loop into a shared helper `jm_destructure_array(mt, pattern, rhs_reg)`
- Support rest elements (`...rest`) using the existing `js_array_slice_from` runtime function

**Files:** `lambda/js/transpile_js_mir.cpp`  
**Lines changed:** ~60

---

### Phase B: TypedArray Loop Performance (fannkuch)

**Goal:** Make `fannkuch(12)` complete within 30s (currently times out). Target: ~2s (Node.js: ~1s).  
**Estimated effort:** ~150 lines in `transpile_js_mir.cpp`.

**Problem:** The `fannkuch` benchmark has 4 nested loops doing ~10^9 TypedArray bracket accesses. Even with Phase 9 direct memory access, the current path still:
1. Extracts `JsTypedArray*` from `Map.data` on every access (redundant when the variable is loop-invariant)
2. Performs bounds checking on every access
3. Boxes/unboxes integer results between typed array reads and arithmetic

**Action items:**

#### B1. TypedArray Pointer Hoisting

When a TypedArray variable (`perm`, `perm1`, `count`) is not reassigned within a loop, hoist the `JsTypedArray*` pointer load and `data` pointer load out of the loop:
```mir
// Before loop:
ta_ptr = LOAD Map.data        // JsTypedArray*
data_ptr = LOAD ta_ptr->data  // int32_t*
ta_len = LOAD ta_ptr->length  // for bounds check
// Inside loop:
val = MEM[data_ptr + idx * 4] // direct load, no Map indirection
```

This requires a pre-scan of loops to identify TypedArray variables that are loop-invariant.

#### B2. Bounds Check Elimination in Counted Loops

In `for (let i = 0; i < n; i++)` where `n <= arr.length`, the bounds check `if (idx >= ta_len) goto slow_path` is provably safe and can be eliminated. Detect this pattern:
- Loop variable `i` starts at 0 (or known non-negative constant)
- Loop bound is `< arr.length` or `< n` where `n` was set from `arr.length`
- Loop step is +1

This is a common V8 optimization and critical for inner loops.

#### B3. Native Register Allocation for TypedArray Temporaries

In the swap pattern:
```javascript
const tmp = perm[lo]; perm[lo] = perm[hi]; perm[hi] = tmp;
```
Ensure `tmp` stays as a native `int32_t` register — no boxing to `Item` and back. The type inference already identifies `tmp` as INT from the Int32Array typed-array-type propagation. Verify the codegen path keeps it native through the entire read-swap-write sequence.

#### B4. Inlined TypedArray Copy Loops

Pattern `for (let i = 0; i < n; i++) perm[i] = perm1[i]` can be recognized and lowered to a `memcpy` call when both arrays are the same typed-array type. This is a secondary optimization.

**Files:** `lambda/js/transpile_js_mir.cpp`  
**Lines changed:** ~150

---

### Phase C: BigInt via Lambda Decimal Library (pidigits)

**Goal:** Pass `pidigits.js` by implementing JS `BigInt` backed by Lambda's `mpdecimal` unlimited-precision library.  
**Estimated effort:** ~300 lines across AST, transpiler, and runtime.

**Design principle:** Reuse Lambda's existing `lambda-decimal.hpp` infrastructure. Lambda already has:
- `decimal_add`, `decimal_sub`, `decimal_mul`, `decimal_div`, `decimal_mod`, `decimal_pow` — all arbitrary-precision
- `decimal_cmp_items` — comparison returning -1/0/1
- `decimal_from_int64`, `decimal_to_int64` — conversion
- `decimal_to_string` — BigInt's `.toString()` method
- Unlimited precision mode (`N` suffix in Lambda) — exactly what BigInt needs

**Action items:**

#### C1. BigInt Literal Parsing

In `build_js_ast.cpp`, detect the `n` suffix on numeric literals (`1n`, `0n`, `42n`):
- Add `JS_LITERAL_BIGINT` to `JsLiteralType` enum
- Parse `1n` as a decimal unlimited value via `decimal_from_string()`
- Store as `Item` with decimal type in `JsLiteralNode`

#### C2. BigInt Runtime Functions

Add to `js_runtime.cpp`:
```c
extern "C" Item js_bigint_add(Item a, Item b);  // → decimal_add
extern "C" Item js_bigint_sub(Item a, Item b);  // → decimal_sub
extern "C" Item js_bigint_mul(Item a, Item b);  // → decimal_mul
extern "C" Item js_bigint_div(Item a, Item b);  // → decimal_div (floor division)
extern "C" Item js_bigint_mod(Item a, Item b);  // → decimal_mod
extern "C" Item js_bigint_cmp(Item a, Item b);  // → decimal_cmp_items
extern "C" Item js_bigint_to_string(Item a);     // → decimal_to_string
extern "C" Item js_bigint_from_int(int64_t v);   // → decimal_from_int64
```

Key semantic difference: JS BigInt division truncates toward zero (like C integer division), while Lambda decimal division is exact. Use `decimal_trunc(decimal_div(a, b))` for BigInt `/` operator.

The `pidigits.js` benchmark implements its own `idiv()` function adjusting for floor-vs-truncate semantics, so the basic truncating division will suffice.

#### C3. BigInt Transpiler Support

In `transpile_js_mir.cpp`:
- Add `LMD_TYPE_BIGINT` (or reuse `LMD_TYPE_DECIMAL`) to type inference for BigInt literals
- Route `+`, `-`, `*`, `/` operators to `js_bigint_*` functions when either operand is BigInt
- Handle `===` comparison via `js_bigint_cmp`
- Handle `.toString()` method via `js_bigint_to_string`
- Handle mixed BigInt-Number operations as TypeError (JS semantics)

#### C4. Type Tag for BigInt

JS BigInt maps directly to Lambda's decimal type (`LMD_TYPE_DECIMAL`). No new type tag needed — use the existing unlimited-precision decimal representation. The `typeof` operator returns `"bigint"` when `get_type_id(item) == LMD_TYPE_DECIMAL`.

**Files:** `lambda/js/js_ast.hpp`, `lambda/js/build_js_ast.cpp`, `lambda/js/js_runtime.cpp`, `lambda/js/transpile_js_mir.cpp`  
**Lines changed:** ~300

---

### Phase D: Hot-Loop Deboxing (Systemic Performance)

**Goal:** Reduce median self-time ratio from ~12x to ~5x across all benchmarks.  
**Estimated effort:** ~200 lines in `transpile_js_mir.cpp`.

**Analysis of remaining bottlenecks** (from self-time ratios):

| Ratio | Benchmarks | Root Cause |
|-------|-----------|------------|
| 2–7x | divrec, larceny/primes, collatz, json_gen, ray | Near-native, minor overhead |
| 10–20x | quicksort, array1, pnpoly, deriv, brainfuck, gcbench | Property access + method dispatch |
| 25–91x | base64, diviter, triangl, matmul | Inner-loop boxing residue |

#### D1. Compound Assignment Type Inference Fix

The `jm_get_effective_type` function returns `LMD_TYPE_ANY` for compound assignments (`+=`, `-=`, etc.):
```cpp
case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
    if (asgn->op == JS_OP_ASSIGN) return jm_get_effective_type(mt, asgn->right);
    return LMD_TYPE_ANY;  // ← THIS should infer from both sides
}
```
Fix: For compound assignments where both sides are numeric, return the numeric type. This prevents downstream code from treating the assignment result as ANY-typed, forcing boxing.

#### D2. Top-Level Function Body Native Emission

Currently, only pre-collected named functions get dual native+boxed versions. The top-level `main()` body (or the implicit program-level code) always runs fully boxed because it contains mixed-type operations like `process.hrtime.bigint()`.

Fix: Introduce **selective deboxing** within top-level code — when a local variable is initialized from a numeric literal or numeric function result, track it as native within the scope even if the function overall can't be fully nativized. This is a lightweight version of Phase 4 applied to the program-level scope.

#### D3. Native String Method Inlining

The `charCodeAt` and `String.fromCharCode` methods show up in brainfuck's inner loop (18.5x ratio). Currently dispatched through `js_string_method` with string comparison:
```cpp
if (strcmp(method_chars, "charCodeAt") == 0) { ... }
```

Fix: In the transpiler, detect `str.charCodeAt(i)` at compile time (the method name is a static identifier) and emit a direct call to the underlying character extraction (a byte load from `String.chars + index`), returning a native INT. Similarly, `String.fromCharCode(n)` can be inlined to a single-character string allocation.

#### D4. Property Access Caching for Known Shapes

For benchmarks like `nbody` where objects have fixed shapes (`{x, y, z, vx, vy, vz, mass}`), the `js_property_get` call does a shape-entry walk on every access. This can be optimized:
- At the point where `body.x` is accessed, if the object was created with a known literal shape, resolve the field offset at compile time
- Emit a direct memory load from the Map data at the known offset
- This is a compile-time specialization, not speculative — only applies when the shape is provably known

This is a larger optimization mainly beneficial for `nbody` and similar physics simulations.

**Files:** `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_runtime.cpp`  
**Lines changed:** ~200

---

### Phase E: Map Built-in + RegExp (knucleotide, regexredux)

**Goal:** Bring 2 more benchmarks from "out of scope" to passing.  
**Estimated effort:** ~500 lines total.

#### E1. JS `Map` Built-in (knucleotide)

**Lambda infrastructure:** Lambda's `lib/hashmap.h` provides a generic C hash map with `hashmap_new`, `hashmap_get`, `hashmap_set`, `hashmap_remove`, `hashmap_count`, and iteration. This is the ideal backing store for JS `Map`.

**Implementation:**
- Create `JsMap` struct wrapping a `HashMap*` with string-keyed entries storing `Item` values
- Wrap in a Lambda Map object with a `js_map_type_marker` sentinel (same pattern as TypedArray and DOM wrapping)
- Runtime functions:
  ```c
  extern "C" Item js_map_new();                        // HashMap* creation
  extern "C" Item js_map_set(Item map, Item key, Item val);
  extern "C" Item js_map_get(Item map, Item key);
  extern "C" Item js_map_has(Item map, Item key);
  extern "C" Item js_map_delete(Item map, Item key);
  extern "C" Item js_map_size(Item map);
  extern "C" Item js_map_entries(Item map);            // → Lambda Array of [k,v] pairs
  extern "C" Item js_map_keys(Item map);
  extern "C" Item js_map_values(Item map);
  extern "C" Item js_map_for_each(Item map, Item callback);
  ```
- In transpiler, detect `new Map()` constructor and route to `js_map_new`
- Route `.set()`, `.get()`, `.has()`, `.delete()`, `.size` method calls to corresponding runtime functions
- Support `[...map.entries()]` spread via `js_map_entries` → Array conversion

**Files:** New file `lambda/js/js_map.cpp`, `lambda/js/js_map.h`, + `transpile_js_mir.cpp` dispatch  
**Lines changed:** ~250

#### E2. JS `RegExp` via Lambda RE2 (regexredux)

**Lambda infrastructure:** Lambda's `re2_wrapper.hpp` provides:
- `pattern_full_match` / `pattern_partial_match` — matching
- `pattern_find_all` — global search returning all matches with indices
- `pattern_replace_all` — global substitution
- `pattern_split` — split by pattern
- Built-in caching of compiled RE2 patterns

**Implementation:**
- In `build_js_ast.cpp`, parse regex literals (`/pattern/flags`) as a new `JS_LITERAL_REGEX` type
  - `g` flag → global mode, `i` flag → case-insensitive, `m` flag → multiline
  - Store the pattern string and flags
- Runtime: Create `JsRegExp` struct storing the compiled RE2 pattern + flags
- Map key methods to RE2 wrapper calls:

  | JS Method | Lambda RE2 Function |
  |-----------|-------------------|
  | `regex.test(str)` | `pattern_partial_match` |
  | `str.match(regex)` | `pattern_find_all` (with `g` flag) or `pattern_partial_match` |
  | `str.replace(regex, replacement)` | `pattern_replace_all` |
  | `str.split(regex)` | `pattern_split` |
  | `regex.exec(str)` | `pattern_partial_match` + capture groups |

- RE2 supports the regex subset used in `regexredux.js` (alternation, character classes, anchors)
- RE2 does NOT support backreferences or lookaheads, but the Benchmarks Game regex benchmarks use only basic patterns compatible with RE2

**Files:** New file `lambda/js/js_regexp.cpp`, `lambda/js/js_regexp.h`, + `build_js_ast.cpp`, `transpile_js_mir.cpp`  
**Lines changed:** ~250

---

### Phase F: `require('fs')` Shim (Optional, Deferred)

**Goal:** Enable 3 benchmarks that read input from stdin/files.  
**Estimated effort:** ~80 lines.

Rather than implementing a full Node.js module system, provide a minimal shim:
- Detect `require('fs')` in the transpiler and return a built-in `fs` object
- Implement only `fs.readFileSync(path, encoding)` using Lambda's existing `read_text_file()` from `lib/file_utils.h`
- Alternatively, support `--input <file>` CLI flag in `lambda.exe js` mode that pre-loads file content into a global variable, allowing benchmarks to be adapted to read from a pre-loaded buffer

**Rationale for deferral:** All 3 `require('fs')` benchmarks also need Map or RegExp (covered in Phase E). Once Phase E is done, only the file I/O shim remains. This phase is low-priority since the benchmarks can also be modified to use process.stdin or hardcoded inputs.

**Files:** `lambda/js/js_globals.cpp`, `lambda/js/transpile_js_mir.cpp`  
**Lines changed:** ~80

---

## 4. Implementation Priority & Dependencies

```
Phase A (AST fixes)         ──→ +3 benchmarks   [LOW effort, HIGH value]
  ├── A1 template escapes   (independent)
  ├── A2 comment skipping   (independent)
  └── A3 destructuring asgn (independent)

Phase B (TypedArray perf)   ──→ +1 benchmark    [MEDIUM effort, HIGH value]
  ├── B1 pointer hoisting   (independent)
  ├── B2 bounds check elim  (depends on B1)
  └── B3 native registers   (independent)

Phase C (BigInt)            ──→ +1 benchmark    [MEDIUM effort, MEDIUM value]
  └── Reuses lambda-decimal.hpp

Phase D (Hot-loop deboxing) ──→ systemic perf   [MEDIUM effort, HIGH value]
  ├── D1 compound assign type (independent)
  ├── D2 top-level native   (independent)
  ├── D3 string method inline (independent)
  └── D4 shape caching      (independent, larger)

Phase E (Map + RegExp)      ──→ +2 benchmarks   [HIGH effort, MEDIUM value]
  ├── E1 Map built-in       (independent)
  └── E2 RegExp via RE2     (independent)

Phase F (require fs shim)   ──→ enables E tests  [LOW effort, LOW value]
  └── Depends on Phase E
```

**Recommended execution order:**
1. **Phase A** — Quick wins, 3 benchmarks fixed with minimal code changes
2. **Phase D1** — Compound assignment type fix, unblocks better codegen everywhere
3. **Phase B** — Makes fannkuch pass, proves TypedArray competitiveness
4. **Phase C** — BigInt via decimal, adds pidigits
5. **Phase D2–D4** — Progressive performance tuning
6. **Phase E** — Map + RegExp for remaining benchmarks
7. **Phase F** — `require('fs')` shim if needed

---

## 5. Performance Projection

### After Phase A + B (Correctness)

| Benchmark | Status | Expected |
|-----------|--------|----------|
| binarytrees | ❌ → ✅ | Template escape fix, correct output |
| nbody | ❌ → ✅ | Comment skip fix, correct output |
| levenshtein | ❌ → ✅ | Destructuring assignment fix |
| fannkuch | ❌ → ✅ | TypedArray hoisting, completes in ~2-5s |

### After Phase C + E (Feature Additions)

| Benchmark | Status | Expected |
|-----------|--------|----------|
| pidigits | ⬜ → ✅ | BigInt via decimal lib |
| knucleotide | ⬜ → ✅ | Map built-in (still needs fs shim or adapted input) |
| regexredux | ⬜ → ✅ | RegExp via RE2 (still needs fs shim or adapted input) |

### After Phase D (Performance Tuning)

Expected self-time ratio improvements on key benchmarks:

| Benchmark | Current Ratio | Expected After D | Technique |
|-----------|--------------|-------------------|-----------|
| diviter | 35.6x | ~5x | D1 (compound assign type) + D2 (top-level native) |
| matmul | 91.3x | ~10x | B1 (pointer hoisting) + D2 |
| triangl | 36.1x | ~8x | B1 + B2 (bounds check elimination) |
| base64 | ~25x | ~8x | B1 + D2 |
| brainfuck | 18.5x | ~8x | D3 (charCodeAt inlining) |
| deriv | 18.6x | ~10x | D4 (shape caching for cons cells) |
| gcbench | 21.6x | ~15x | Limited by GC overhead (architectural) |

**Projected median self-time ratio: ~6–8x** (down from ~12x).

---

## 6. Design Principles

1. **Reuse Lambda runtime infrastructure** — BigInt → `lambda-decimal.hpp`, RegExp → `re2_wrapper.hpp`, Map → `lib/hashmap.h`, file I/O → `lib/file_utils.h`. No new external dependencies.

2. **Same type system** — All JS values remain Lambda `Item` (64-bit tagged values). BigInt uses `LMD_TYPE_DECIMAL`. Map/RegExp use the sentinel-marker-in-Map wrapping pattern (same as TypedArray and DOM).

3. **Progressive optimization** — Each phase independently improves correctness or performance. No phase creates regressions in passing benchmarks.

4. **Native path first** — Extend type inference to cover more cases (compound assignments, top-level code, string methods) so the native MIR emission path triggers more often, avoiding boxing overhead.

5. **No speculation without fallback** — Unlike V8's speculative optimization with deoptimization bailouts, Lambda's JS engine uses **static type inference** — only emit native code when types are provably known at compile time. This is simpler and avoids deopt complexity, at the cost of not optimizing fully untyped code.

---

## 7. Test Plan

### Correctness Verification

After each phase, run:
```bash
# Run all 29 benchmarks, compare outputs with Node.js
cd test/benchmark
python3 run_js_benchmarks.py

# Run individual benchmarks
./lambda.exe js test/benchmark/beng/js/binarytrees.js 10
./lambda.exe js test/benchmark/beng/js/nbody.js 1000
./lambda.exe js test/benchmark/kostya/levenshtein.js
./lambda.exe js test/benchmark/beng/js/fannkuch.js 7
./lambda.exe js test/benchmark/beng/js/pidigits.js 30
```

### Regression Testing

```bash
make test-lambda-baseline    # Ensure Lambda core tests still pass
```

### Performance Benchmarking

Compare median of 3 runs, **release build only** (`make release`):
```bash
# Wall time comparison
time ./lambda.exe js test/benchmark/beng/js/fannkuch.js 12
time node test/benchmark/beng/js/fannkuch.js 12

# Self-reported timing (from __TIMING__ output)
# Compare Lambda vs Node.js self-time ratios before/after each phase
```

---

## 8. Summary

| Phase | Benchmarks Fixed | Effort | Priority |
|-------|-----------------|--------|----------|
| **A: AST correctness** | +3 (binarytrees, nbody, levenshtein) | ~100 LOC | **P0 — do first** |
| **B: TypedArray perf** | +1 (fannkuch) | ~150 LOC | **P0** |
| **C: BigInt** | +1 (pidigits) | ~300 LOC | **P1** |
| **D: Deboxing** | systemic perf, ~12x→~6x | ~200 LOC | **P1** |
| **E: Map + RegExp** | +2 (knucleotide, regexredux) | ~500 LOC | **P2** |
| **F: require('fs')** | enables E tests | ~80 LOC | **P3** |
| **Total** | **25/29 correct (100% feasible)** | **~1330 LOC** | |
