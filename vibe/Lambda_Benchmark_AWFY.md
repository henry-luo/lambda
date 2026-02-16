# Lambda × AWFY Benchmark Proposal

## 1. Overview

The [Are We Fast Yet (AWFY)](https://github.com/smarr/are-we-fast-yet) project is an academic benchmark suite designed to compare language implementations using a common set of abstractions: **objects, closures, arrays, and strings**. It currently covers 14 benchmarks ported to 10 languages (C++, Crystal, Java, JavaScript, Lua, Python, Ruby, SOM, SOMns, Smalltalk).

This proposal assesses the feasibility of porting the AWFY benchmark suite to Lambda Script, identifies required language extensions, lays out an implementation plan, and describes how to run formal benchmarks and report results.

---

## 2. AWFY Benchmark Summary

### 2.1 Micro Benchmarks (9 benchmarks)

| # | Benchmark    | Description                                    | Lines | Key Features Used                        | Expected Result      |
|---|-------------|------------------------------------------------|-------|------------------------------------------|----------------------|
| 1 | **Bounce**   | Ball bouncing in a box (100 balls, 50 frames)  | ~90   | Mutable objects, arrays, PRNG            | `1331`               |
| 2 | **List**     | Recursive linked-list traversal (Tak function) | ~88   | Recursion, linked lists, cons cells      | `10`                 |
| 3 | **Mandelbrot** | Mandelbrot set fractal computation           | ~110  | Float math, bitwise ops, nested loops    | `191` (size=500)     |
| 4 | **NBody**    | 5-body gravitational simulation                | ~195  | Float math, sqrt, mutable object arrays  | `-0.1690859889909308`|
| 5 | **Permute**  | Permutation generation via recursive swap      | ~63   | Recursion, array swap                    | `8660`               |
| 6 | **Queens**   | N-Queens backtracking solver                   | ~87   | Backtracking, boolean arrays, recursion  | `true`               |
| 7 | **Sieve**    | Sieve of Eratosthenes (primes up to 5000)      | ~50   | Boolean array mutation, tight loops      | `669`                |
| 8 | **Storage**  | Recursive tree of arrays (allocation stress)   | ~57   | Recursive construction, PRNG             | `5461`               |
| 9 | **Towers**   | Towers of Hanoi (13 disks, linked-list stacks) | ~96   | Recursion, linked-list push/pop          | `8191`               |

### 2.2 Macro Benchmarks (5 benchmarks)

| #  | Benchmark     | Description                                     | Lines | Key Features Used                                  | Expected Result           |
|----|--------------|--------------------------------------------------|-------|----------------------------------------------------|---------------------------|
| 10 | **Richards**  | OS task scheduler simulation                     | ~430  | Deep OOP hierarchy, closures with mutable capture, state machines | `true`                    |
| 11 | **CD**        | Airplane collision detection                     | ~855  | Red-black tree, 3D vector math, polymorphism       | `14484` (1000 aircraft)   |
| 12 | **DeltaBlue** | Incremental constraint solver                    | ~660  | Deep inheritance (5 levels), polymorphic dispatch   | `true`                    |
| 13 | **Havlak**    | Loop detection in control flow graphs            | ~664  | Union-find, DFS, heavy collection use              | `[1647, 5213]`            |
| 14 | **Json**      | Recursive descent JSON parser                    | ~564  | String char-by-char parsing, recursive descent      | `156` (operations count)  |

### 2.3 Core Abstractions Required by AWFY

From the AWFY guidelines, the "core language" subset is:
- **Objects** with fields (can be records, structs, or maps)
- **Polymorphic methods** on user-defined types
- **Closures** with read/write access to lexical scope
- **Fixed-size arrays** with indexed access
- **Strings** with character access

Explicitly excluded (must be reimplemented within the benchmark):
- Built-in hash tables, dictionaries, stacks, queues
- `break`/`continue` (except for collection iterators)
- Single-character types

---

## 3. Lambda Language Feature Assessment

### 3.1 Lambda Feature Matrix vs AWFY Requirements

| AWFY Requirement                        | Lambda Status        | Detail                                                                                     |
|-----------------------------------------|----------------------|-------------------------------------------------------------------------------------------|
| Objects with mutable fields             | **Partial**          | Maps `{a: 1, b: 2}` are immutable. `pn` functions allow `var` for mutable local scalars. Map updates require spread: `{...old, a: newVal}` creating a new map. |
| Polymorphic methods                     | **Achievable**       | No class inheritance, but `match` expressions + structural typing + function maps enable dispatch patterns. |
| Closures (mutable capture)              | **Not supported**    | Closures capture values only (immutable). No closure-over-mutable-variable pattern.        |
| Fixed-size arrays (indexed read)        | **Supported**        | `arr[i]` read access works.                                                                |
| Fixed-size arrays (indexed write)       | **Not supported**    | No `arr[i] = val` syntax. Internal `array_set` exists but is not exposed to Lambda code.  |
| Strings (character access)              | **Partial**          | `slice(s, i, i+1)` extracts one character. No `s[i]` char indexing.                       |
| While loops with mutation               | **Supported**        | `pn` functions: `var i = 0; while (i < n) { i = i + 1 }`.                                 |
| For loops with indexed iteration        | **Supported**        | `for i in 0 to n { ... }` with mutation of `var`s in body.                                 |
| Recursion                               | **Supported**        | Both `fn` and `pn` support recursion.                                                      |
| Float math (sqrt, sin, cos)             | **Supported**        | `sqrt`, `sin`, `cos`, `tan`, `log`, `exp`, `abs`, `floor`, `ceil`, `round` all available.  |
| Bitwise operations (AND, OR, XOR, shift)| **Not supported**    | No bitwise operators in Lambda's grammar. No `<<`, `>>`, `&` (bitwise), `\|`, `^` (XOR).  |
| Integer division / modulo               | **Supported**        | `div` (integer division), `%` (modulo).                                                    |
| String concatenation                    | **Supported**        | `++` operator.                                                                             |
| Comparison operators                    | **Supported**        | `==`, `!=`, `<`, `<=`, `>`, `>=`.                                                          |
| Logical operators                       | **Supported**        | `and`, `or`, `not`.                                                                        |
| Class inheritance                       | **Not supported**    | No class system. Maps + closures + match patterns serve as alternatives.                   |
| Module/import                           | **Supported**        | `import module;`, `pub fn/pn` exports.                                                     |

### 3.2 Critical Gaps — Language Features Needed

The following language features are **required** to faithfully implement the AWFY suite in Lambda:

#### Gap 1: Array Indexed Assignment — `arr[i] = val` (CRITICAL)

**Impact**: Blocks **11 of 14** benchmarks (Bounce, Mandelbrot, NBody, Permute, Queens, Sieve, Storage, Towers, Richards, CD, DeltaBlue).

**Current state**: `array_set` exists internally in the C++ runtime (`mark_editor.cpp`) but is not exposed as a Lambda operator or system function.

**Recommendation**: Expose array indexed assignment in `pn` functions only. Two options:
- **Option A (syntax)**: Allow `arr[i] = val` assignment syntax in `pn` functions (grammar change).
- **Option B (system function)**: Expose `array_set(arr, index, value)` as a system function callable from `pn`.

Option A is more idiomatic and aligns with other languages. This enables all benchmarks that need array mutation.

#### Gap 2: Bitwise Operations (IMPORTANT)

**Impact**: Required by **Mandelbrot** (bit packing: `<<`, `^`), **Richards** (XOR), **CD** (integer truncation via `| 0`), **Json** (hash table masking: `&`, `^`, `>>>`). Also used heavily in the `som.js` shared collection library (Dictionary hashing).

**Recommendation**: Add bitwise operators or system functions:
- `bit_and(a, b)` or `a band b`
- `bit_or(a, b)` or `a bor b`
- `bit_xor(a, b)` or `a bxor b`
- `bit_not(a)` or `bnot a`
- `bit_shl(a, n)` or `a shl n` (left shift)
- `bit_shr(a, n)` or `a shr n` (arithmetic right shift)
- `bit_ushr(a, n)` or `a ushr n` (unsigned/logical right shift)

**Alternative**: System functions (`band`, `bor`, `bxor`, `bnot`, `shl`, `shr`) are simpler to implement and avoid grammar changes.

#### Gap 3: Map Field Mutation — `obj.field = val` (IMPORTANT)

**Impact**: Required by **NBody** (updating body positions/velocities), **Richards** (task state), **CD** (tree nodes), **DeltaBlue** (constraint state), **Havlak** (node properties), **Bounce** (ball position/velocity).

**Current state**: Maps are immutable. `{...old, key: val}` creates a new map. The C++ runtime has `MarkEditor::map_update` internally.

**Recommendation**: Two options:
- **Option A (syntax)**: Allow `obj.field = val` in `pn` functions (grammar change — assignment target = field access expression).
- **Option B (system function)**: Expose `map_set(obj, "field", val)` returning a new map (or mutating in place if refcount == 1).

Option A is strongly preferred for clarity and benchmark faithfulness. In-place mutation matters for performance — creating new maps on every field update would invalidate benchmark results.

#### Gap 4: Mutable Closure Capture (MODERATE)

**Impact**: **Richards** (closure callbacks that read/write scheduler state), **DeltaBlue** (constraint callbacks). Can be worked around.

**Current state**: Closures capture immutable values only.

**Workaround**: Instead of closures capturing mutable vars, pass state explicitly through function arguments or use an array as a mutable cell (AWFY Java approach). With array indexed assignment (Gap 1), a single-element array can serve as a mutable box:
```lambda
var cell = [0]
let inc = () => cell[0] = cell[0] + 1   // if arr[i] = val is available
```

**Recommendation**: Not strictly required if Gap 1 (array mutation) is implemented. The array-as-mutable-box pattern is an accepted AWFY approach (documented as the Java convention). More naturally, if Gap 3 (map field mutation) is also implemented, map fields can serve as shared mutable state.

### 3.3 Nice-to-Have Features (Non-blocking)

| Feature | Impact | Workaround |
|---------|--------|------------|
| `+=`, `-=`, `*=` compound assignment | Code brevity | Use `x = x + 1` |
| String character indexing `s[i]` | Json benchmark clarity | Use `slice(s, i, i+1)` |
| CLI `--time` flag for benchmarking | Convenience | Use external `time` or `hyperfine` |
| Mutable closure captures | Richards, DeltaBlue | Array-as-mutable-cell pattern |

---

## 4. Benchmark-by-Benchmark Feasibility

### 4.1 Assessment Table

Assumes **all critical gaps (1–3) are implemented**. Without them, the "Feasibility" column shows the current status.

| #  | Benchmark     | Currently Feasible? | With Gaps Fixed? | Lambda Approach                                            | Difficulty |
|----|--------------|---------------------|------------------|------------------------------------------------------------|------------|
| 1  | **Bounce**    | No (needs arr mutation, mutable objects) | **Yes** | `pn` with array of maps, field/array mutation, PRNG via formula | Easy |
| 2  | **List**      | **Yes**             | Yes              | Recursive maps `{val: int, next: Node\|null}`, `fn` recursion | Easy |
| 3  | **Mandelbrot**| No (needs bitwise ops) | **Yes**       | `pn` with nested while loops, float math, bitwise bit-packing | Easy |
| 4  | **NBody**     | No (needs arr+map mutation) | **Yes**   | `pn` with array of body maps, field mutation per timestep  | Medium |
| 5  | **Permute**   | No (needs arr mutation) | **Yes**      | `pn` with array swap via indexed assignment                | Easy |
| 6  | **Queens**    | No (needs arr mutation) | **Yes**      | `pn` with boolean arrays for constraints, backtracking     | Easy |
| 7  | **Sieve**     | No (needs arr mutation) | **Yes**      | `pn` with boolean array, while loop, indexed toggle        | Easy |
| 8  | **Storage**   | **Yes** (minor)     | Yes              | `pn` with recursive array/tree construction, PRNG          | Easy |
| 9  | **Towers**    | **Partial** (needs map mutation for stack pointers) | **Yes** | `pn` with linked-list maps for disk stacks    | Easy |
| 10 | **Richards**  | No (needs all gaps) | **Yes**         | `pn` with maps as objects, match for polymorphism, closures | Hard |
| 11 | **CD**        | No (needs all gaps) | **Yes**         | `pn` with red-black tree as maps, vector math, match dispatch | Hard |
| 12 | **DeltaBlue** | No (needs all gaps) | **Yes**         | `pn` with constraint maps, match for virtual dispatch      | Hard |
| 13 | **Havlak**    | No (needs all gaps) | **Yes**         | `pn` with graph maps, union-find via array mutation        | Hard |
| 14 | **Json**      | No (needs bitwise + arr mutation) | **Yes** | `pn` with recursive descent, char-by-char string parsing | Medium |

### 4.2 Lambda Idiom Mapping

The AWFY guidelines require "identical" code using the language idiomatically. The mapping from OOP patterns to Lambda idioms:

| OOP Pattern                | Lambda Idiom                                                    |
|----------------------------|-----------------------------------------------------------------|
| Class with fields          | Map `{field1: val1, field2: val2}`                              |
| Constructor                | Factory function `fn make_ball(x, y) => {x: x, y: y, vx: 0, vy: 0}` |
| Method call `obj.method()` | Function call `method(obj)` or `obj \| method`                  |
| Inheritance hierarchy      | Tagged maps `{type: 'stay, ...}` + `match` dispatch            |
| Virtual method dispatch    | `match obj.type { 'stay => ..., 'edit => ..., 'scale => ... }` |
| Static method              | Module-level `fn` or `pn`                                       |
| `this` / `self`            | Explicit first parameter `self` or map passed to functions      |
| Interface / abstract class | Type union: `type Constraint = StayConstraint \| EditConstraint \| ...` |
| `new ClassName(args)`      | `make_classname(args)` factory function                         |
| Collection library (som.js)| Lambda `som.ls` module with `Vector`, `Set`, `Dictionary` as maps + functions |

### 4.3 The SOM Collection Library in Lambda

The AWFY benchmarks use a shared `som` library providing `Vector`, `Set`, `Dictionary`, `Random`. In Lambda, this would be implemented as a `som.ls` module:

```lambda
// som.ls — SOM collection library for AWFY benchmarks

// Vector: resizable array wrapper
pub fn make_vector() => {storage: [], size: 0}

pub pn vector_append(v, elem) {
    v.storage[v.size] = elem
    v.size = v.size + 1
}

pub pn vector_at(v, idx) {
    v.storage[idx]
}

pub pn vector_for_each(v, callback) {
    var i = 0
    while (i < v.size) {
        callback(v.storage[i])
        i = i + 1
    }
}

// Random: deterministic PRNG
pub pn random_next(r) {
    r.seed = band(r.seed * 1309 + 13849, 65535)
    r.seed
}

// Dictionary: hash map with separate chaining
// ... (requires bitwise ops for hashing)
```

---

## 5. Implementation Plan

### Phase 0: Language Extensions (Prerequisites)

Before benchmark implementation, the following language features must be added:

| Priority | Feature                  | Scope                    | Estimated Effort |
|----------|--------------------------|--------------------------|------------------|
| P0       | Array indexed assignment | Grammar + eval + MIR JIT | 3–5 days         |
| P0       | Bitwise operations       | System functions or ops  | 2–3 days         |
| P1       | Map field mutation       | Grammar + eval + MIR JIT | 3–5 days         |
| P2       | String char indexing     | System function          | 1 day            |

**Total estimated effort for extensions**: 9–14 days

### Phase 1: Infrastructure & Micro Benchmarks

**Directory structure**:
```
test/benchmark/awfy/
├── harness.ls          # Benchmark harness (timing, iteration, reporting)
├── run.ls              # Benchmark runner (selects + executes benchmarks)
├── som.ls              # SOM collection library (Vector, Set, Dictionary, Random)
├── bounce.ls           # Bounce benchmark
├── list.ls             # List benchmark
├── mandelbrot.ls       # Mandelbrot benchmark
├── nbody.ls            # NBody benchmark
├── permute.ls          # Permute benchmark
├── queens.ls           # Queens benchmark
├── sieve.ls            # Sieve benchmark
├── storage.ls          # Storage benchmark
├── towers.ls           # Towers of Hanoi benchmark
├── richards.ls         # Richards benchmark
├── cd.ls               # CD (Collision Detection) benchmark
├── deltablue.ls        # DeltaBlue benchmark
├── havlak.ls           # Havlak benchmark
├── json.ls             # JSON parser benchmark
└── results/            # Benchmark result outputs
    ├── lambda.csv       # Lambda results
    └── report.md        # Comparison report
```

**Implementation order** (by difficulty, building up incrementally):

| Step | Benchmark      | Dependencies          | Estimated Time |
|------|---------------|----------------------|----------------|
| 1.1  | Harness + Run  | None                 | 1 day          |
| 1.2  | Sieve          | Array mut             | 0.5 day        |
| 1.3  | Permute        | Array mut             | 0.5 day        |
| 1.4  | Queens         | Array mut             | 0.5 day        |
| 1.5  | Towers         | Map mut               | 1 day          |
| 1.6  | Bounce         | Array+Map mut, PRNG   | 1 day          |
| 1.7  | List           | Recursive maps        | 0.5 day        |
| 1.8  | Storage        | Array mut, PRNG       | 0.5 day        |
| 1.9  | Mandelbrot     | Bitwise ops           | 1 day          |
| 1.10 | NBody          | Map mut, sqrt         | 1 day          |

**Phase 1 total**: ~7.5 days

### Phase 2: SOM Library & Macro Benchmarks

| Step | Benchmark      | Dependencies                        | Estimated Time |
|------|---------------|--------------------------------------|----------------|
| 2.1  | som.ls (Vector, Set, Dictionary, Random) | Array+Map mut, Bitwise | 2 days  |
| 2.2  | Json           | String ops, recursive descent        | 2 days         |
| 2.3  | Richards       | som.ls, closures, match dispatch     | 3 days         |
| 2.4  | CD             | som.ls, red-black tree, vector math  | 3 days         |
| 2.5  | DeltaBlue      | som.ls, deep dispatch, constraint graph | 3 days      |
| 2.6  | Havlak         | som.ls, union-find, graph algorithms | 3 days         |

**Phase 2 total**: ~16 days

### Phase 3: Verification & Correctness

| Step | Task                                              | Time   |
|------|---------------------------------------------------|--------|
| 3.1  | Verify all 14 benchmarks produce correct results  | 2 days |
| 3.2  | Cross-check against JavaScript reference outputs   | 1 day  |
| 3.3  | Add expected-result `.txt` files for test suite    | 0.5 day|
| 3.4  | Add Makefile targets for benchmark execution       | 0.5 day|

**Phase 3 total**: ~4 days

---

## 6. Benchmark Execution & Performance Comparison

### 6.1 Harness Design

The benchmark harness follows the AWFY convention:

```
./lambda.exe run test/benchmark/awfy/harness.ls <Benchmark> <NumIterations> <InnerIterations>
```

Example:
```bash
./lambda.exe run test/benchmark/awfy/harness.ls Bounce 100 10
# Output: Bounce: iterations=100 average: 1234us total: 123400us
```

Each iteration:
1. Run the benchmark's `inner_benchmark_loop(inner_iterations)`
2. Verify the result matches the expected value
3. Record elapsed time (microseconds)
4. Print per-iteration timing

### 6.2 Timing Methodology

Following AWFY methodology:
- **Warmup**: First N iterations are warmup (JIT compilation, cache warming). Report separately.
- **Measurement**: Remaining iterations are measured. Report geometric mean.
- **External timing**: Use `hyperfine` for wall-clock comparison.

```bash
# Per-benchmark internal timing
./lambda.exe run test/benchmark/awfy/harness.ls Sieve 1000 1

# Wall-clock comparison across languages
hyperfine \
  './lambda.exe run test/benchmark/awfy/harness.ls Sieve 100 1' \
  'node are-we-fast-yet/benchmarks/JavaScript/harness.js Sieve 100 1' \
  'python3 are-we-fast-yet/benchmarks/Python/harness.py Sieve 100 1' \
  --warmup 3
```

### 6.3 Comparison Languages

Primary comparison targets (all available in AWFY):

| Language     | Implementation | Category            |
|-------------|----------------|---------------------|
| **C++**     | clang++ -O2    | Native compiled     |
| **Java**    | OpenJDK HotSpot| JIT compiled        |
| **JavaScript** | Node.js (V8) | JIT compiled       |
| **Lua**     | LuaJIT         | JIT compiled        |
| **Python**  | CPython        | Interpreted         |
| **Ruby**    | CRuby (MRI)    | Interpreted         |
| **Lambda**  | C2MIR JIT      | JIT compiled        |

Lambda with MIR JIT occupies the same category as JavaScript/V8 and LuaJIT — JIT-compiled dynamic language. Expected positioning:
- **Target**: Within 2–5× of C++ (native)
- **Competitive with**: JavaScript (V8), LuaJIT
- **Faster than**: CPython, CRuby

### 6.4 Metrics & Report Format

For each benchmark, report:

| Metric              | Description                                      |
|---------------------|--------------------------------------------------|
| **Total time (ms)** | Wall-clock for all iterations                    |
| **Average (μs)**    | Mean time per iteration (after warmup)           |
| **Median (μs)**     | Median time per iteration                        |
| **Std dev (%)**     | Coefficient of variation                         |
| **Relative to C++** | Ratio vs C++ baseline (1.0 = same speed)         |
| **Relative to JS**  | Ratio vs Node.js                                 |

### 6.5 Final Report Structure

```
test/benchmark/awfy/results/report.md
```

Contents:
1. **Executive Summary** — Lambda's overall position vs other languages
2. **Methodology** — Machine specs, OS, compiler versions, iteration counts
3. **Results Table** — All 14 benchmarks × all languages, with relative performance
4. **Performance Chart** — Geometric mean across all benchmarks (bar chart)
5. **Per-Benchmark Analysis** — Where Lambda excels/struggles and why
6. **Optimization Opportunities** — JIT compilation hotspots, potential improvements

---

## 7. Timeline Summary

| Phase  | Description                           | Duration   | Cumulative |
|--------|---------------------------------------|------------|------------|
| **0**  | Language extensions (arr mut, bitwise, map mut) | 9–14 days | 9–14 days |
| **1**  | Infrastructure + 9 micro benchmarks   | 7.5 days   | 16.5–21.5 days |
| **2**  | SOM library + 5 macro benchmarks      | 16 days    | 32.5–37.5 days |
| **3**  | Verification & correctness            | 4 days     | 36.5–41.5 days |
| **4**  | Formal benchmarking & report          | 3 days     | 39.5–44.5 days |

**Total estimated effort: 8–9 weeks**

### Milestone Checkpoints

| Milestone | Criteria                                          | Target     |
|-----------|---------------------------------------------------|------------|
| M0        | Language extensions merged, basic tests passing    | Week 2     |
| M1        | All 9 micro benchmarks producing correct results  | Week 4     |
| M2        | SOM library complete, Json passing                 | Week 5     |
| M3        | All 14 benchmarks producing correct results        | Week 7     |
| M4        | Formal benchmark run, report published             | Week 9     |

---

## 8. Alternative Approaches

### 8.1 Functional-First Port (without language extensions)

If language extensions are delayed, a **subset** of benchmarks can be ported using current Lambda features:

| Benchmark   | Feasibility Without Extensions | Approach                                    |
|-------------|-------------------------------|----------------------------------------------|
| **List**    | **Fully feasible**            | Pure `fn` recursion with immutable map nodes |
| **Storage** | **Mostly feasible**           | `pn` with fresh array construction per node  |
| **Towers**  | **Feasible with workaround**  | Use arrays as stacks with full replacement   |

This gives 2–3 working benchmarks immediately as proof-of-concept.

### 8.2 Phased Extension Rollout

If implementing all extensions at once is too costly, prioritize:

1. **Array indexed assignment** (P0) — unblocks 11 benchmarks
2. **Bitwise system functions** (P0) — unblocks Mandelbrot, Json, Dictionary hashing
3. **Map field mutation** (P1) — needed for faithful OOP simulation in macro benchmarks

With just #1 and #2, all micro benchmarks become feasible, and some macro benchmarks can work with map-spread workarounds (at a performance cost).

### 8.3 Benchmark Adaptation Options

For benchmarks heavily reliant on OOP (Richards, DeltaBlue, Havlak, CD), two adaptation strategies exist:

**Strategy A — Tagged Dispatch**: Use maps with a `type` field and `match` expressions for polymorphism. This is idiomatic Lambda but structurally differs from OOP ports.

```lambda
// DeltaBlue constraint hierarchy via tagged maps
fn make_stay_constraint(v, strength) => {
    type: 'stay, variable: v, strength: strength, satisfied: false
}

fn make_edit_constraint(v, strength) => {
    type: 'edit, variable: v, strength: strength, satisfied: false
}

pn execute(c) {
    match c.type {
        'stay => c.variable.value = c.variable.value   // no-op
        'edit => null                                    // externally set
        'scale => {
            c.output.value = c.input.value * c.scale_factor + c.offset
        }
    }
}
```

**Strategy B — Function-in-Map (Vtable Pattern)**: Store method functions as map fields, simulating virtual dispatch.

```lambda
fn make_stay_constraint(v, strength) => {
    variable: v,
    strength: strength,
    execute: (self) => self.variable.value,
    inputs_do: (self, callback) => null,
}

// Dispatch via field call
pn run_constraint(c) {
    c.execute(c)
}
```

**Recommendation**: Use **Strategy A** (tagged dispatch) for clarity and maintainability. It provides deterministic dispatch with `match`, which is idiomatic Lambda and easier to optimize in the JIT.

---

## 9. Risks & Mitigations

| Risk                                      | Impact | Mitigation                                           |
|-------------------------------------------|--------|------------------------------------------------------|
| Array/map mutation changes affect immutability guarantees | High | Restrict mutations to `pn` functions only; `fn` remains pure |
| JIT not optimizing map-based dispatch well | Medium | Profile after implementation; add JIT hints if needed |
| Benchmark results not comparable due to different idioms | Medium | Follow AWFY guidelines strictly; document deviations |
| Stack overflow on deep recursion (List, Havlak) | Low | Test with default stack size; add TCO if needed      |
| Floating-point precision differences       | Low    | Verify against JS reference with epsilon comparison  |

---

## 10. Success Criteria

1. **Correctness**: All 14 benchmarks produce the correct expected results.
2. **Performance**: Lambda with MIR JIT is within 3× of Node.js (V8) on geometric mean across all benchmarks.
3. **Reproducibility**: Results are reproducible with documented methodology.
4. **Report**: Published comparison with at least 4 other languages (C++, Java, JavaScript, Python).
5. **Integration**: Benchmarks are runnable via Makefile targets and included in CI.
