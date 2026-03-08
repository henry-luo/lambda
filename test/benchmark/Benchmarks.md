# Lambda Benchmark Guide

This document describes how to prepare, run, and report Lambda benchmarks across 5 suites and 5 engines.

---

## 1. Benchmark Suites

| Suite | Dir | Count | Source | Focus |
|-------|-----|------:|--------|-------|
| **R7RS** | `r7rs/` | 10 | [r7rs-benchmarks](https://ecraven.github.io/r7rs-benchmarks/) | Scheme classics: recursion, numeric, closures |
| **AWFY** | `awfy/` | 14 | [Are We Fast Yet](https://github.com/smarr/are-we-fast-yet) | Cross-language OOP/micro/macro benchmarks |
| **BENG** | `beng/` | 10 | [Benchmarks Game](https://benchmarksgame-team.pages.debian.net/) | Real-world: GC, regex, FASTA, BigInt, N-body |
| **Kostya** | `kostya/` | 7 | [kostya/benchmarks](https://github.com/kostya/benchmarks) | Community: brainfuck, matmul, base64, JSON |
| **Larceny** | `larceny/` | 12 | [Larceny/Gabriel](https://www.larcenists.org/) | Gabriel suite: search, symbolic, allocation |

**Total: 53 benchmarks**

Each benchmark has a Lambda script (`.ls`) and a JavaScript equivalent (`.js`).

### File naming

| Pattern | Purpose |
|---------|---------|
| `fib.ls` | Original Lambda implementation (unit test) |
| `fib2.ls` | **Benchmark version** with timing + type annotations |
| `fib2.js` | **JS benchmark** with `__TIMING__` output |
| `fib2_bundle.js` | **Standalone JS bundle** (AWFY only, no `require()`) |

For the BENG suite, the convention is simpler: `binarytrees.ls` and `js/binarytrees.js`.

---

## 2. Engines

| Engine         | Key        | Type        | Command                              |
| -------------- | ---------- | ----------- | ------------------------------------ |
| **MIR Direct** | `mir`      | JIT         | `./lambda.exe run script.ls`         |
| **C2MIR**      | `c2mir`    | JIT         | `./lambda.exe run --c2mir script.ls` |
| **LambdaJS**   | `lambdajs` | JIT         | `./lambda.exe js script.js`          |
| **Node.js**    | `nodejs`   | JIT (V8)    | `node script.js`                     |
| **QuickJS**    | `quickjs`  | Interpreter | `qjs --std -m wrapper.js`            |

- **MIR Direct**: Lambda → MIR IR → native. Default compiler path, lowest startup.
- **C2MIR**: Lambda → C source → MIR. Legacy path, sometimes better optimized.
- **LambdaJS**: Lambda's built-in JS JIT engine. No `require()` or `fs`.
- **Node.js**: Google V8 with optimizing JIT. Full Node.js API.
- **QuickJS**: Lightweight interpreter. Needs a polyfill wrapper (auto-generated).

### QuickJS wrapper

QuickJS lacks `process.hrtime.bigint()` and `console.log`. The runner auto-generates `temp/qjs_<name>.js` with:

```javascript
import * as std from 'std';
if (typeof process === 'undefined') {
    globalThis.process = {
        stdout: { write: function(s) { std.out.puts(s); std.out.flush(); } },
        argv: ['-', '-'],
        hrtime: { bigint: function() {
            return BigInt(Math.round(performance.now() * 1e6));
        } },
        exit: function(code) { std.exit(code); }
    };
}
// ... original JS code follows
```

### AWFY bundle files

AWFY JS benchmarks use `require()` to load official source from `ref/are-we-fast-yet/`. Since LambdaJS and QuickJS don't support `require()`, standalone `*_bundle.js` files inline all dependencies. The runner automatically uses bundles for these two engines and the original `require()`-based file for Node.js.

---

## 3. Timing Protocol

Every benchmark must emit a `__TIMING__` line to stdout. The runner parses it via regex:

```
__TIMING__:<milliseconds>
```

This measures **self-reported execution time** — only the computation, excluding process startup, JIT warmup, and file I/O. The runner also measures **wall-clock time** externally.

### Lambda script pattern (`.ls`)

```javascript
pn main() {
    let t0 = clock()          // start timer

    // ... benchmark computation ...

    let elapsed = (clock() - t0) * 1000.0   // ms
    print("__TIMING__:" ++ string(elapsed) ++ "\n")
}
```

`clock()` returns seconds as a float. Multiply by 1000 for milliseconds.

### JavaScript pattern (`.js`)

```javascript
const __t0 = process.hrtime.bigint();

// ... benchmark computation ...

const __t1 = process.hrtime.bigint();
process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
```

`process.hrtime.bigint()` returns nanoseconds. Divide by 1e6 for milliseconds.

### Timing placement rules

| Case | Strategy |
|------|----------|
| Pure computation | Wrap entire computation |
| File I/O (BENG: knucleotide, regexredux, revcomp) | Start timer **after** `fs.readFileSync()` |
| AWFY (via helper) | `awfy_helper.js` wraps `innerBenchmarkLoop()` |
| Correctness check | Include verification in timed section or not — be consistent |

---

## 4. Preparing a New Benchmark

### 4.1 Lambda script

1. Write the benchmark as a procedural `pn main() { ... }` script.
2. Add type annotations where possible (helps MIR JIT).
3. Wrap the computation with `clock()` timing.
4. Print `__TIMING__:<ms>\n` to stdout.
5. Print a PASS/FAIL line for correctness verification.
6. Save as `<name>2.ls` (or `<name>.ls` for BENG/Kostya/Larceny).

### 4.2 JavaScript equivalent

1. Translate the same algorithm to idiomatic JavaScript.
2. Add `process.hrtime.bigint()` timing around the computation.
3. Print `__TIMING__:<ms>\n` via `process.stdout.write()`.
4. Save as `<name>2.js` (or `js/<name>.js` for BENG).

### 4.3 AWFY-specific

AWFY benchmarks reuse official source from `ref/are-we-fast-yet/benchmarks/JavaScript/`:

1. Create `<name>2.js` using the helper:
   ```javascript
   'use strict';
   const { runAWFY } = require('./awfy_helper');
   runAWFY('Name', require('../../../ref/are-we-fast-yet/benchmarks/JavaScript/<name>'));
   ```
2. Create `<name>2_bundle.js` — a standalone file that inlines all AWFY dependencies plus the `__TIMING__` wrapper. This is needed for LambdaJS and QuickJS.

### 4.4 Register in the runner

Add the benchmark tuple to the appropriate list in `run_all_benchmarks.py`:

```python
R7RS = [
    ...
    ("name", "test/benchmark/r7rs/name2.ls", "test/benchmark/r7rs/name2.js"),
]
```

---

## 5. Running Benchmarks

All commands run from the **project root** (`/Users/henryluo/Projects/Lambda`).

### Full run (all 5 suites × 5 engines)

```bash
python3 test/benchmark/run_all_benchmarks.py [NUM_RUNS]
```

- Default: 3 runs per benchmark, reports median.
- Timeout: 120 seconds per run.
- Results saved to `test/benchmark/benchmark_results.json`.
- Prints wall-clock and exec-time tables to stdout.

### Single-suite runners

```bash
# BENG suite only (all 5 engines)
python3 test/benchmark/run_beng_benchmarks.py [NUM_RUNS]

# JS engines only for R7RS + AWFY (LambdaJS, Node.js, QuickJS)
python3 test/benchmark/run_js_benchmarks.py [NUM_RUNS]
```

These merge results into the same `benchmark_results.json`.

### Manual single-benchmark run

```bash
# Lambda MIR Direct
./lambda.exe run test/benchmark/r7rs/fib2.ls

# Lambda C2MIR
./lambda.exe run --c2mir test/benchmark/r7rs/fib2.ls

# LambdaJS
./lambda.exe js test/benchmark/r7rs/fib2.js

# Node.js
node test/benchmark/r7rs/fib2.js

# QuickJS (needs wrapper — or use the auto-generated one in temp/)
qjs --std -m temp/qjs_fib2.js
```

### Prerequisites

| Dependency | Install |
|------------|---------|
| `./lambda.exe` | `make release` (must use release build for benchmarking) |
| `node` | [nodejs.org](https://nodejs.org/) |
| `qjs` | `brew install quickjs` (macOS) |
| AWFY source | `ref/are-we-fast-yet/` (git submodule or clone) |

> **IMPORTANT**: Never benchmark with a debug build. Always use `make release`.

---

## 6. Results and Reporting

### Data file

`benchmark_results.json` stores all results:

```json
{
  "r7rs": {
    "fib": {
      "mir":      [19.1, 1.96],
      "c2mir":    [19.1, 2.22],
      "lambdajs": [20.8, 10.8],
      "nodejs":   [37.8, 1.64],
      "quickjs":  [22.0, 17.4]
    }
  }
}
```

Each engine value is `[wall_clock_ms, exec_time_ms]`. A value of `null` means the benchmark failed or timed out.

### Tabulate results

```bash
python3 test/benchmark/summarize_benchmarks.py
```

Prints summary tables: suite totals (wall-clock and exec-time), per-engine comparison, and common-only totals (benchmarks where all 5 engines succeeded).

### Generate the report

```bash
python3 test/benchmark/gen_result_doc.py
```

Reads `benchmark_results.json` and writes `Overall_Result2.md` with:
- Per-suite tables (all 5 engines + MIR/Node ratio column)
- Geometric mean MIR/Node.js ratios per suite
- Overall summary table
- Performance tier breakdown (Lambda >2x faster, comparable, Node faster, etc.)
- Key findings narrative

### Pipeline summary

```
prepare scripts  →  run benchmarks  →  tabulate  →  report
   (.ls + .js)        run_all_        summarize_    gen_result_
                    benchmarks.py   benchmarks.py    doc.py
                         ↓                              ↓
               benchmark_results.json        Overall_Result2.md
```

---

## 7. Methodology Notes

- **Median of N runs** (default N=3). The median filters outliers from GC pauses or system load.
- **Self-reported exec time** is the primary comparison metric. It isolates algorithmic performance from process startup overhead (~15–40 ms for Lambda/Node).
- **Wall-clock time** is also recorded for total cost analysis.
- **Timeout**: 120 seconds per run. Benchmarks exceeding this are marked as failed.
- **QuickJS wrappers** are auto-generated in `temp/` and not committed.
- **Known engine limitations**:
  - LambdaJS: No `require()`, no `fs`, limited ES6 class support (fails some AWFY).
  - QuickJS: No `fs` module (fails BENG file-reading benchmarks), stack overflow on deep recursion (fails `ack`).
  - MIR: Some `.ls` benchmarks fail due to runtime issues (recorded as `---` in results).

---

## 8. File Reference

| File | Purpose |
|------|---------|
| `run_all_benchmarks.py` | Main runner: 5 suites × 5 engines |
| `run_beng_benchmarks.py` | BENG-only runner (all 5 engines) |
| `run_js_benchmarks.py` | JS-only runner (R7RS + AWFY, 3 engines) |
| `gen_result_doc.py` | Generates `Overall_Result2.md` from JSON |
| `summarize_benchmarks.py` | Prints summary tables to stdout |
| `benchmark_results.json` | Raw results data (wall + exec times) |
| `Overall_Result2.md` | Generated report with tables and analysis |
| `awfy/awfy_helper.js` | AWFY timing wrapper for Node.js |
| `awfy/*_bundle.js` | Standalone AWFY bundles for LambdaJS/QuickJS |
