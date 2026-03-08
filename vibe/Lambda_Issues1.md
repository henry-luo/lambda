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

**Why**: Writing `var e = 0.0` gives a float literal, but subsequent accumulation
with array elements (which return `Item` type in untyped arrays) causes a JIT type
mismatch. Using `bx[0] - bx[0]` ensures `e` is Item-typed zero, compatible with
later arithmetic on array-read results.

**Files affected**:
- `awfy/nbody.ls` — `var e = bx[0] - bx[0]`
- `awfy/nbody2.ls` — same

---

## 4. `[null, null, ...]` — Pre-Allocated Fixed-Size Arrays

**Pattern**: Literal arrays of nulls like `[null,null,null,...,null]` for 2, 4, 6, 16,
or 32 elements.

**Why**: These serve as chunk buffers for hand-rolled growable arrays (Vec/Arr
abstractions). `fill(16, null)` would be more concise but caused runtime hangs in
hot-path functions like `null16()`/`null32()` during testing. Literal null arrays work
reliably.

**Files affected**:
- `awfy/cd.ls`, `awfy/cd2.ls` — `null16()`, `null32()`, `vxy = [null, null]`
- `awfy/havlak.ls`, `awfy/havlak2.ls` — `null16()`, `null32()`
- `awfy/json.ls`, `awfy/json2.ls` — `chunks: [null,...,null]` in map literals
- `awfy/deltablue.ls`, `awfy/deltablue2.ls` — same chunked pattern
- `awfy/storage.ls`, `awfy/storage2.ls` — `[null, null, null, null]`
- `awfy/richards.ls`, `awfy/richards2.ls` — `[null, null, null, null, null, null]`

---

## 5. `.sz` Field — Manual Size Tracking

**Pattern**: Maps with a `.sz` field to track logical array length:
```lambda
var vec = { chunks: [null16()], sz: 0 }
```

**Why**: Lambda has no built-in growable/resizable array type. These benchmarks
implement a chunked vector abstraction where `.sz` tracks the logical element count,
separate from the physical chunk capacity. `len()` returns the array's physical size,
not the user's logical size.

**Files affected**: `awfy/cd.ls`, `cd2.ls`, `havlak.ls`, `havlak2.ls`, `deltablue.ls`,
`deltablue2.ls`, `json.ls`, `json2.ls`

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

**Why**: `for i in start to end { ... }` in `pn` (procedural) functions causes runtime
errors. All benchmarks are procedural (`pn`), so they must use `while` loops.

**Files affected**: All benchmark files (~30+). Example:
```lambda
var i = 0
while (i < n) {
    // ...
    i = i + 1
}
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
