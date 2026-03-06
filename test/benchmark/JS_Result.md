# Lambda JS Engine — Benchmark Results

**Date:** 2025-07-15 (updated)  
**Lambda build:** Release (MIR JIT)  
**Node.js version:** v25.5.0 (V8)  
**Platform:** macOS, Apple M1  
**Benchmark corpus:** 29 JavaScript files across 3 suites

---

## Summary

| Metric | Count |
|--------|-------|
| Total benchmarks | 29 |
| **Correct output** | **21** (72.4%) |
| Wrong output | 4 (13.8%) |
| Out of scope | 4 (13.8%) |

**Improvement:** +8 benchmarks fixed (from 13 to 21) by implementing:
1. Top-level `const`/`let` capture in function declarations
2. `let` block-scoping in `for` loops
3. TypedArray `.fill()` method dispatch
4. GC fix: pool-allocate JsFunction objects (closures unreachable from GC roots were collected)

---

## 1. Correctness Results — All 29 Benchmarks

### Larceny Suite (12 benchmarks)

Self-verifying benchmarks that report PASS/FAIL and `__TIMING__` via `process.hrtime.bigint()`.

| Benchmark | Status | Lambda Output | Notes |
|-----------|--------|---------------|-------|
| array1 | ✅ PASS | PASS, timing=18.2ms | |
| deriv | ✅ PASS | PASS, timing=41.4ms | |
| diviter | ✅ PASS | PASS, timing=12433ms | Correct but very slow |
| divrec | ✅ PASS | PASS, timing=17.0ms | |
| gcbench | ✅ PASS | Correct tree check values | |
| paraffins | ✅ PASS | PASS, nb(23)=5731580 | |
| pnpoly | ✅ PASS | DONE, total=100000 inside=29415 | |
| primes | ✅ PASS | PASS, timing=4.1ms | **NEW** — fixed by let scoping + TypedArray .fill() |
| puzzle | ✅ PASS | PASS, timing=20.5ms | **NEW** — fixed by top-level capture + GC fix |
| quicksort | ✅ PASS | PASS | |
| ray | ✅ PASS | PASS, hits=1392 | |
| triangl | ✅ PASS | solutions=29760, timing=2205ms | **NEW** — fixed by TypedArray .fill() dispatch |

**Larceny: 12/12 passing (100%)** ✅

### Kostya Suite (7 benchmarks)

Self-verifying benchmarks with same timing/verification pattern.

| Benchmark | Status | Lambda Output | Notes |
|-----------|--------|---------------|-------|
| base64 | ✅ PASS | encoded_len=13336, decoded_len=10000 | **NEW** — fixed by capture + GC fix |
| brainfuck | ✅ PASS | "Hello World!" | |
| collatz | ✅ PASS | start=837799 | |
| json_gen | ✅ PASS | length=61626 | |
| matmul | ✅ PASS | sum=-29562, timing=1205ms | **NEW** — fixed by let scoping + capture |
| primes | ✅ PASS | 78498, timing=60.6ms | **NEW** — fixed by let scoping + TypedArray .fill() |
| levenshtein | ❌ FAIL | d=7,6 (expected 3,3) | Destructuring assignment `[a,b]=[b,a]` |

**Kostya: 6/7 passing (86%)**

### Beng / Computer Language Benchmarks Game (10 benchmarks)

Output-based benchmarks using `console.log` and `process.argv`.

| Benchmark | Status | Lambda Output | Node.js Output | Notes |
|-----------|--------|---------------|----------------|-------|
| fasta | ✅ PASS | 171 lines DNA sequence | 171 lines (identical) | **NEW** — fixed by top-level capture |
| mandelbrot | ✅ PASS | "2" | "2" | Mandelbrot area |
| spectralnorm | ✅ PASS | "1.274219991" | "1.274219991" | **NEW** — fixed by let scoping + Float64Array .fill() |
| binarytrees | ❌ FAIL | Wrong interpolation order | Correct tabular output | Escape sequence `\t` in template literals |
| fannkuch | ❌ TIMEOUT | No output after 30s | Completes in ~1s | TypedArray access performance |
| nbody | ❌ FAIL | parse error | "-0.169075164" / "-0.169087605" | Comments in expressions |
| revcomp | ⬜ N/A | — | — | Requires `require('fs')` |
| knucleotide | ⬜ N/A | — | — | Requires `require('fs')`, `Map` |
| regexredux | ⬜ N/A | — | — | Requires `require('fs')`, `RegExp` |
| pidigits | ⬜ N/A | — | — | Requires `BigInt` |

**Beng: 3/10 passing (3 correct, 3 wrong, 1 timeout, 3 out of scope)**

---

## 2. Performance Comparison — Passing Benchmarks

### Wall Time (median of 3 runs)

Wall time includes Lambda startup and JIT compilation overhead.

| Benchmark | Lambda (ms) | Node.js (ms) | Ratio | Comment |
|-----------|-------------|--------------|-------|---------|
| fasta | 14 | 46 | **0.30x** ✅ | Lambda faster |
| paraffins | 15 | 47 | **0.31x** ✅ | Lambda faster |
| quicksort | 21 | 61 | **0.34x** ✅ | Lambda faster |
| ray | 23 | 46 | **0.50x** ✅ | Lambda faster |
| puzzle | 33 | 57 | **0.58x** ✅ | Lambda faster (NEW) |
| divrec | 30 | 52 | **0.57x** ✅ | Lambda faster |
| array1 | 30 | 44 | **0.68x** ✅ | Lambda faster |
| json_gen | 34 | 50 | **0.68x** ✅ | Lambda faster |
| spectralnorm | 51 | 44 | 1.16x | Comparable (NEW) |
| kostya/primes | 65 | 53 | 1.23x | Comparable (NEW) |
| mandelbrot | 104 | 48 | 2.16x | Lambda slower |
| pnpoly | 107 | 48 | 2.22x | Lambda slower |
| deriv | 55 | 45 | 1.22x | Comparable |
| base64 | 396 | 66 | 6.00x | Lambda slower (NEW) |
| gcbench | 511 | 64 | 7.98x | Lambda slower (GC heavy) |
| brainfuck | 641 | 80 | 8.01x | Lambda slower |
| kostya/matmul | 1397 | 72 | 19.4x | Lambda much slower (NEW) |
| triangl | 2283 | ~180 | 12.7x | Lambda much slower (NEW) |
| collatz | 6027 | 1337 | 4.50x | Lambda slower |
| diviter | 12487 | 384 | 32.51x | Lambda much slower |

**8 of 21 benchmarks: Lambda wall time is faster than Node.js** (startup amortized).

### Self-Reported Timing (computation only, no startup)

These `__TIMING__` values are measured inside the script using `process.hrtime.bigint()`, excluding startup overhead. More representative of raw computation speed.

| Benchmark | Lambda (ms) | Node.js (ms) | Ratio | Comment |
|-----------|-------------|--------------|-------|---------|
| larceny/primes | 4.1 | 0.68 | 6.0x | NEW |
| divrec | 17.0 | 7.6 | 2.2x | Close |
| paraffins | 1.6 | 0.5 | 3.0x | |
| puzzle | 20.5 | 2.1 | 9.8x | NEW |
| kostya/primes | 60.6 | 4.9 | 12.4x | NEW |
| collatz | 6105 | 1294 | 4.7x | |
| json_gen | 21.6 | 3.4 | 6.4x | |
| ray | 9.9 | 1.5 | 6.7x | |
| quicksort | 8.7 | 1.0 | 8.9x | |
| array1 | 18.2 | 1.5 | 12.1x | |
| base64 | ~350 | 13.9 | ~25x | NEW |
| kostya/matmul | 1205 | 13.2 | 91.3x | NEW — heavy array ops |
| triangl | 2205 | 61.1 | 36.1x | NEW — TypedArray loops |
| brainfuck | 625 | 33.8 | 18.5x | |
| deriv | 41.4 | 2.2 | 18.6x | |
| pnpoly | 93.9 | 4.4 | 21.2x | |
| gcbench | 492 | 22.8 | 21.6x | GC-intensive |
| diviter | 12433 | 349 | 35.6x | Tight integer loop |

**Median self-time ratio: ~12x slower than Node.js V8 JIT.**

---

## 3. Root Cause Analysis — Remaining Failures

4 benchmarks still fail, clustering into 2 residual issues:

### Issue 1: AST builder gaps (3 benchmarks)

| Gap | Benchmark | Example |
|-----|-----------|---------|
| Escape sequences in template literals | binarytrees | `` `${n}\t${check}` `` — `\t` not cooked |
| Comments inside expressions | nbody | `[{ x: 1.0, /* comment */ y: 2.0 }]` |
| Destructuring assignment (not declaration) | levenshtein | `[prev, curr] = [curr, prev]` |

### Issue 2: TypedArray access performance (1 benchmark)

**Affected:** fannkuch

Each bracket access on typed arrays involves boxing/unboxing through `js_property_access` → `js_typed_array_get`, causing catastrophic slowdown in tight combinatorial loops.

### Previously Fixed Issues (this session)

| Issue | Fix | Benchmarks Unblocked |
|-------|-----|---------------------|
| Top-level `const`/`let` not captured in fn declarations | Capture analysis now includes function declarations; closures created when captures > 0 | puzzle, base64, fasta, nbody*, matmul* |
| `let` in `for` loops not block-scoped | Added `js_scope_push`/`js_scope_pop` around for-statement body | primes×2, matmul, spectralnorm |
| TypedArray `.fill()` method dispatch | Added `js_is_typed_array` check in MAP method dispatch branch | primes×2, triangl, spectralnorm |
| GC collecting JsFunction closures | Changed `heap_alloc` → `pool_calloc` for JsFunction (closures in pool-allocated env arrays are unreachable from GC roots) | base64 (and any closure-heavy benchmark with enough iterations to trigger GC) |

*partially fixed — nbody still blocked by comments-in-expressions

---

## 4. Remaining Feature Gaps

Features required by the 4 still-failing benchmarks:

| Feature | Benchmarks Blocked | Priority |
|---------|--------------------|----------|
| Escape sequences in template literals | 1 (binarytrees) | Medium |
| Comments inside expressions | 1 (nbody) | Medium |
| Destructuring assignment (not declaration) | 1 (levenshtein) | Medium |
| TypedArray access performance | 1 (fannkuch) | Low |
| `require('fs')` / file I/O | 3 (out of scope) | Out of scope |
| `BigInt` | 1 (out of scope) | Out of scope |
| `Map` built-in | 1 (out of scope) | Out of scope |

---

## 5. Key Observations

1. **Correctness rate: 21/29 (72.4%)** — up from 13/29 after fixing 3 systemic issues + GC bug. The Larceny suite now passes 12/12 (100%). Kostya passes 6/7 (86%).

2. **Wall time vs Node.js**: Lambda is **faster in wall time** for 8/21 benchmarks (startup + JIT included). Lambda's fast startup (~15ms) gives it an advantage over Node.js (~45-60ms) on short benchmarks.

3. **Self-reported time ratio**: Median **~12x slower** than V8. The range spans 2.2x (divrec) to 91.3x (matmul). V8's tiered JIT compilation with speculative optimization gives it a large edge on tight loops, especially array-heavy and GC-heavy code.

4. **Remaining failures** are all AST builder gaps (3 benchmarks: comments in expressions, template literal escapes, destructuring assignment) and TypedArray performance (1 benchmark). These are localized issues, not systemic.

5. **Out-of-scope failures** (4 benchmarks) require `require('fs')`, `BigInt`, `Map`, or `RegExp` — major runtime features not part of the current JS engine scope.

---

## Appendix: Benchmark File Locations

```
test/benchmark/
├── larceny/           # 12 files — Larceny project benchmarks (12/12 ✅)
│   ├── array1.js      ✅
│   ├── deriv.js       ✅
│   ├── diviter.js     ✅
│   ├── divrec.js      ✅
│   ├── gcbench.js     ✅
│   ├── paraffins.js   ✅
│   ├── pnpoly.js      ✅
│   ├── primes.js      ✅ (NEW)
│   ├── puzzle.js      ✅ (NEW)
│   ├── quicksort.js   ✅
│   ├── ray.js         ✅
│   └── triangl.js     ✅ (NEW)
├── kostya/            # 7 files — Kostya benchmarks (6/7 ✅)
│   ├── base64.js      ✅ (NEW)
│   ├── brainfuck.js   ✅
│   ├── collatz.js     ✅
│   ├── json_gen.js    ✅
│   ├── matmul.js      ✅ (NEW)
│   ├── primes.js      ✅ (NEW)
│   └── levenshtein.js ❌
└── beng/js/           # 10 files — Computer Language Benchmarks Game (3/10 ✅)
    ├── fasta.js       ✅ (NEW)
    ├── mandelbrot.js  ✅
    ├── spectralnorm.js✅ (NEW)
    ├── binarytrees.js ❌
    ├── fannkuch.js    ❌ (timeout)
    ├── nbody.js       ❌
    ├── revcomp.js     ⬜ (require fs)
    ├── knucleotide.js ⬜ (require fs)
    ├── regexredux.js  ⬜ (require fs)
    └── pidigits.js    ⬜ (BigInt)
```

## Appendix: Test Environment

```
Lambda:  release build, MIR JIT, 8.3MB binary
Node.js: v25.5.0 (V8 engine with tiered JIT + TurboFan)
OS:      macOS (Apple Silicon M1)
Method:  3 runs per benchmark, median wall time reported
         Self-time measured via process.hrtime.bigint() inside script
```
