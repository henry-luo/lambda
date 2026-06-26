# Benchmark Workaround Patterns

This document catalogs non-idiomatic patterns in the benchmark `.ls` files that exist
as workarounds for current Lambda language limitations. Each section explains the
pattern, why it's needed, and which files are affected.

---

## 1. `make_array(n, val)` — Manual Array Construction

**Pattern**: A `pn make_array(n, val)` helper that builds an array of size `n` via
doubling + concatenation (`arr ++ arr`), used instead of the built-in `fill(n, val)`.

**Why**: Previously, `fill(n, val)` returned `ArrayInt64` for integer values, but
typed `int[]` variables expected `ArrayInt`. This type mismatch caused element
mutations to silently fail. The bug has been fixed:
- `fn_fill()` now returns `ArrayInt` for integer values
- `ensure_typed_array()` handles any typed-array cross-conversion generically
- The transpiler's coercion check now triggers for all typed array expression types

**Status**: **RESOLVED**. All benchmarks (both untyped and typed) now use
`return fill(n, val)` in `make_array`. The `make_array` wrapper is kept for API
consistency but its body is simply `fill()`.

| File | Uses `fill()` | Notes |
|------|:---:|--------|
| `awfy/bounce.ls` | Yes | |
| `awfy/bounce2.ls` | Yes | Was doubling body, now `fill()` |
| `awfy/permute.ls` | Yes | |
| `awfy/permute2.ls` | Yes | Was doubling body, now `fill()` |
| `awfy/queens.ls` | Yes | |
| `awfy/queens2.ls` | Yes | |
| `awfy/sieve.ls` | Yes | |
| `awfy/sieve2.ls` | Yes | |
| `awfy/storage.ls` | Yes | |
| `awfy/storage2.ls` | Yes | |
| `awfy/towers.ls` | Yes | |
| `awfy/towers2.ls` | Yes | Was doubling body, now `fill()` |
| `r7rs/fft.ls` | Yes | |
| `r7rs/fft2.ls` | Yes | Was doubling body, now `fill()` |
| `r7rs/mbrot.ls` | Yes | |
| `r7rs/mbrot2.ls` | Yes | |
| `r7rs/nqueens.ls` | Yes | |
| `r7rs/nqueens2.ls` | Yes | Was doubling body, now `fill()` |

---

## 2. `0 - x` / `0.0 - x` — Missing Unary Negation

**Pattern**: `0 - expr` or `0.0 - expr` instead of `-expr`.

**Why**: Unary `-` on variables/expressions doesn't always work correctly in `pn`
context with the JIT compiler. Using subtraction from zero is a reliable workaround.
Note: unary `-` on literals (e.g. `-1.0`) works.

**Files affected**:
- `awfy/bounce.ls`, `awfy/bounce2.ls` — `bxv[j] = 0 - axv`
- `awfy/nbody.ls`, `awfy/nbody2.ls` — `0.0 - (px / SOLAR_MASS)` (intentional: see also #3)
- `awfy/cd.ls` — `0 - radius * radius`

**Fixed** (now use `-expr`):
- `awfy/cd.ls` — `(-b - sq)`, `(-b + sq)` in `find_intersection`
- `awfy/cd2.ls` — same
- `larceny/ray.ls` — `(-b - math.sqrt(disc))`, `(-b + math.sqrt(disc))`
- `beng/nbody.ls` — `-v`, `-(px/SOLAR_MASS)` etc.
- `beng/spectralnorm.ls` — `-v`

---

## 3. `bx[0] - bx[0]` — Item-Typed Zero

**Pattern**: `var e = bx[0] - bx[0]` to produce a zero that has the same runtime
type as array element reads.

**Why (historical)**: Writing `var e = 0.0` was believed to cause a JIT type
mismatch with subsequent accumulation of `Item`-typed array reads, so the
self-subtraction produced an `Item`-typed zero instead.

**Status**: **RESOLVED**. All sites now use `var e = 0.0` / `var px = 0.0`.

Investigation showed the workaround was obsolete *and* masked a deeper engine
bug. The real fault was in procedural (`pn`) untyped-parameter type inference
(`transpile-mir.cpp`, `infer_param_type`): a param used only in true division
`dt / (d2 * dist)` — with no float literal elsewhere in the body — was
speculatively inferred as `int`, so the call site inserted a truncating `it2i`
coercion (`0.01 → 0`). In `nbody.advance()` this zeroed `mag`, killing every
velocity update and producing a wrong energy (`1690750` vs the correct
`1690876`). The fix: a tracked param participating in `/` now marks
`INFER_FLOAT_CONTEXT` (Lambda's `/` always yields float, e.g. `4/2 → 2.0`),
suppressing the unsound `int` narrowing. Untyped `nbody.ls` now passes; lambda
baseline 3232/3232, no regressions.

**Files affected**:
- `awfy/nbody.ls` — was `var e = bx[0] - bx[0]`, `var px/py/pz = bvx[0] - bvx[0]`
- `awfy/nbody2.ls` — same (typed `float[]` variant)
- `lambda/transpile-mir.cpp` — engine fix (param-inference float-division guard)

---

## 4. `[null, null, ...]` — Pre-Allocated Fixed-Size Arrays

**Pattern**: Literal arrays of nulls like `[null,null,null,...,null]` for 2, 4, 6, 16,
or 32 elements.

**Why (historical)**: These serve as chunk buffers for hand-rolled growable arrays
(Vec/Arr abstractions). `fill(16, null)` was reported to cause runtime hangs in
hot-path functions like `null16()`/`null32()`, so literal null arrays were used.

**Status**: **MOSTLY RESOLVED**. `fill(n, null)` no longer hangs and is now used in
11 of the 13 affected files, each verified byte-for-byte identical to the literal
version (passing benchmarks stay passing; pre-existing failures are unchanged).

**Exception — `awfy/cd.ls` / `awfy/cd2.ls` keep the literals.** Converting CD to
`fill(n, null)` triggers a **heap-use-after-free** (ASan abort) — but the fault is
*not* in `fill`. An all-null literal is never promoted (`try_promote_scalars_to_1d`
bails on the non-numeric `null`), so `fill` and the literal build the same generic
`Array`. The crash is in an *unrelated* numeric array-literal promotion
(`array_end → try_promote_scalars_to_1d → array_num_new → gc_collect → gc_sweep`)
that frees a JIT local still live in a pending `ck == null` comparison (`fn_eq`).
`fill`'s different allocation timing merely exposes this **latent JIT GC-rooting
gap** (locals not rooted across an `array_end` that can GC). It is not minimally
reproducible and the fix belongs in the JIT root-frame machinery — tracked
separately. CD therefore retains literal null arrays until that GC bug is fixed.

**Files converted to `fill(n, null)`**:
- `awfy/havlak.ls`, `awfy/havlak2.ls` — `null16()`, `null32()`
- `awfy/json.ls`, `awfy/json2.ls` — `chunks: fill(16, null)` in `vec_new`/`vec_add`
- `awfy/deltablue2.ls` — chunked `Vec` buffers
- `awfy/storage.ls`, `awfy/storage2.ls` — `fill(4, null)`
- `awfy/richards.ls`, `awfy/richards2.ls` — `task_table = fill(6, null)`
- `jetstream/richards.ls`, `jetstream/richards2.ls` — `blocks = fill(6, null)`

**Kept as literals (GC-bug exception)**:
- `awfy/cd.ls`, `awfy/cd2.ls` — `null16()`, `null32()`, `vxy = [null, null]`

---

## 5. `.sz` Field — Manual Size Tracking

**Pattern**: Maps with a `.sz` field to track logical array length:
```lambda
var vec = { chunks: [null16()], sz: 0 }
```

**Why (historical)**: Lambda had no built-in growable array, so benchmarks implemented
a chunked vector where `.sz` tracked the logical element count separate from physical
chunk capacity.

**Status**: **ENGINE ENHANCED + converted (6 of 8 files).** Added a built-in
`push(arr, val)` that appends to a growable array **in place** (amortized O(1) via the
runtime's existing `array_push` → `expand_list` doubling, which is GC-aware). `[]`
makes an empty growable array; `len(arr)` gives the logical size; `arr[i]` indexes
directly. This replaces the whole chunked-vector + `.sz` abstraction.

- Engine: `SYSPROC_PUSH` in `lambda.h`; registry entry in `sys_func_registry.c`;
  `pn_push` in `lambda-data.cpp`. Regression test: `test/lambda/proc/proc_push.ls`.
- **Performance: `push` is ~9.6× faster** than the chunked vector (77 ms vs 742 ms on
  a 10000×200 append+scan workload), with the same result — direct O(1) indexing vs
  the chunked double-indirection + per-add chunk management. Far exceeds "comparable".

**Converted (verified PASS / no ASan, growable `[]`+`push`+`len`+`[i]`):**
- `awfy/json.ls`, `json2.ls` — `vec_*` wrappers.
- `awfy/havlak.ls`, `havlak2.ls` — `vec` (list) **and** `bvec` (queue: `{data: [], first}`,
  `push(v.data, …)`, `len(v.data)-first`). `arr`/`iarr` stay chunked (sparse
  absolute-index stores — not sequential growable vectors, so not `push`-convertible).
- `awfy/cd.ls`, `cd2.ls` — `vec` (cd2 also changes `type Vec = {chunks,sz}` → `type Vec = any`).

These were **unblocked by the BUG-001 fix** (see `Lambda_Bug.md`, now FIXED): `fn_fill`
generic arrays were created with `capacity = 0`, and `gc_trace_object` marks only
`items[0 … min(length, capacity))`, so every child of a `fill(n, null)` chunk was
skipped by the collector and freed while still live. `push`'s more frequent GC made it
deterministic (havlak + `push` was the repro). Setting `capacity = n` in `fn_fill`
fixed it; the same fix also resolved the pre-existing `proc_array_type_convert` and
`proc_view_mutable` failures (garbage read from swept arrays).

**Not converted — `deltablue.ls`, `deltablue2.ls`** (both PASS as-is after the BUG-001
fix). Their vec is a fixed `fill(256, null)` + `.sz` that supports **remove-by-shift**
(`vec_remove_first`, `vec_remove_cid`) — a `push`-only growable array can't express
element removal without a separate `pop`/`remove`/`splice` builtin. Left on the
workaround pending such a builtin.

---

## 6. `int()` — Explicit Type Casts from Array Reads

**Pattern**: `int(arr[i])` or `int(floor(x))` to extract a typed integer.

**Why**: Reads from untyped arrays return `Item` (a tagged runtime value). Arithmetic
and comparison operations require explicit `int()` casts to unwrap the integer. Typed
arrays (`int[]`) avoid this, but untyped arrays always need the cast.

**Files affected**:
- `awfy/cd.ls`, `awfy/cd2.ls` — `int(x)`, `int(px / GOOD_VOXEL_SIZE)`
- `beng/fannkuch.ls` — `int(perm[0])`, `int(count[r])`
- `beng/knucleotide.ls` — `int(entries[i][1])`
- `beng/spectralnorm.ls`, `beng/nbody.ls` — `int(floor(v))`
- `kostya/matmul.ls` — `int(floor(total))`

---

## 7. `while` Loops — No `for` Range in `pn`

**Pattern**: Counter-based `while` loops instead of `for i in 0 to n-1 { ... }`.

**Why (historical)**: `for i in start to end { ... }` statement loops in `pn` failed —
but with a MIR codegen error, not a generic "runtime error".

**Status**: **FIXED (engine) + partially converted.** Two engine bugs were found and
fixed; `for i in start to end { ... }` statement loops now work in `pn`, including
loops whose body assigns through the loop variable as an array index.

Root causes (both in the JIT, exposed only by *statement-form* for-loops with an
array-index-assignment body — `for i in a to b { arr[i] = v }`):
1. **`transpile_for` boxed the body result unconditionally.** A pure-statement body
   (e.g. `arr[i] = v`) produces no value — transpile returns reg 0 (the invalid-reg
   sentinel) — and `emit_box(reg 0)` raised MIR's *"undeclared reg 0"* error. Fix:
   substitute a null Item when the body result is reg 0 (`transpile-mir.cpp`,
   `transpile_for`).
2. **`fn_index_assign` rejected generic arrays.** A range loop variable is statically
   typed ANY, so `arr[i] = v` routes through `fn_index_assign`, which only accepted
   typed numeric (`ArrayNum`) targets and errored *"masked assignment requires a typed
   numeric array target"* for a generic `[null,...]` array. Fix: handle a generic
   `Array` + plain-int index as an ordinary element write via `fn_array_set`
   (`lambda-vector.cpp`).

Regression test: `test/lambda/proc/proc_for_range.ls`.

**Converted (verified identical PASS output):** `awfy/sieve.ls`, `sieve2.ls`,
`bounce.ls`, `bounce2.ls`, `queens.ls`, `queens2.ls`, `storage.ls`, `storage2.ls` —
the clean `var i = 0; while (i < n) { … i = i + 1 }` counter loops became
`for i in 0 to n - 1 { … }`. `storage` exercises fix #2 (generic chunk array).

**Left as `while` (not simple unit-step ranges — convert case-by-case):** loops with a
non-unit step (`k = k + i` in the sieve inner loop), decrementing counters
(`while (i >= 0) { … i = i - 1 }` in `permute`/`towers`), and the macro-benchmarks with
break/continue and nested mutable counter state (`cd`, `havlak`, `deltablue`, and the
`r7rs`/`beng` numeric kernels).

Example conversion:
```lambda
var i = 0
while (i < n) { /* ... */ i = i + 1 }   // before
for i in 0 to n - 1 { /* ... */ }       // after
```

---

## 8. `min_f` / `max_f` — Function Wrappers

**Pattern**: Manual min/max functions instead of built-in `min()`/`max()`.

**Why**: Calling the built-in `min()`/`max()` with `fn` syntax inside `pn` functions
caused runtime hangs during testing. The `pn` wrapper with explicit `if` comparison
works reliably.

**Files affected**:
- `awfy/cd.ls` — `pn min_f(a, b)`, `pn max_f(a, b)`
- `awfy/cd2.ls` — `pn min_f(a: float, b: float)`, `pn max_f(a: float, b: float)`

---

## 9. `format9()` / `format3()` — Manual Float Formatting

**Pattern**: ~20-line functions to format a float to a fixed number of decimal places.

**Why**: Lambda lacks `sprintf`-style formatted output or a `toFixed(n)` function.
Each benchmark requiring specific decimal precision must implement its own formatter
that decomposes a float into integer + fraction parts, scales, pads with zeros, and
concatenates strings.

**Files affected**:
- `beng/nbody.ls` — `pn format9(x)` (9 decimal places)
- `beng/spectralnorm.ls` — `pn format9(x)`
- `beng/knucleotide.ls` — `pn format3(x)` (3 decimal places)

---

## 10. Single-Element Array/Map for Pass-by-Reference

**Pattern**: `var seed_arr = [74755]` or `let state = {count: 0}` to wrap a scalar
that needs to be mutated across function calls.

**Why**: Lambda passes scalars by value. To simulate pass-by-reference (needed for
PRNG state, counters, accumulators), the value is wrapped in a `[val]` array or
`{field: val}` map. The container is passed by reference, allowing mutation of the
inner value.

**Files affected**:
- `awfy/bounce.ls`, `bounce2.ls` — `seed_arr = [74755]`
- `awfy/storage.ls`, `storage2.ls` — `seed_arr = [74755]`, `state = {count: 0}`
- `awfy/permute.ls`, `permute2.ls` — `state = {count: 0}`
- `awfy/towers.ls`, `towers2.ls` — `state = {moves: 0}`
- `beng/fasta.ls` — `seed_arr = [42]`

---

## 11. Manual Sort

**Pattern**: Hand-written bubble sort.

**Why**: Lambda has no built-in sort function for arrays, nor support for custom
comparators in `pn` context.

**Files affected**:
- `beng/knucleotide.ls` — `pn sort_entries(entries)` (~25 lines)

---

## 12. `string()` — Explicit String Conversion

**Pattern**: `string(value)` in string concatenation expressions.

**Why**: The `++` operator doesn't auto-coerce non-string types. This is **by design**
in Lambda, not a bug. Noted here for completeness. A future string interpolation
syntax (e.g. `$"value={x}"`) could reduce verbosity.

**Files affected**: All benchmark files that produce output.

---

## Summary of Root Causes

| Root Cause | Workaround # | Fix Complexity |
|------------|:---:|---|
| `fill()` + typed `int[]` mutation bug | 1 | Engine fix (fill return type) |
| Unary `-` unreliable in pn JIT | 2 | Engine fix (transpile-mir) |
| Untyped array reads return Item | 3, 6 | By design / type inference |
| No growable array type | 4, 5 | New built-in type |
| `fill(n, null)` hangs in hot paths | 4 | Engine bug |
| `for` range broken in pn | 7 | Engine fix (transpile) |
| `fn`/built-in calls hang in pn | 8 | Engine fix (JIT interop) |
| No float format specifier | 9 | New built-in function |
| Scalars are pass-by-value only | 10 | By design / add ref params |
| No built-in sort | 11 | New built-in function |
| No string interpolation | 12 | Syntax sugar |
