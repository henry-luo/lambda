# Lambda Benchmark Guide

This document describes how to prepare, run, and report Lambda benchmarks across 6 suites and 6 engines.

**Canonical snapshot workflow:** use `python3 test/benchmark/run_standard_benchmarks.py` from the project root. It rebuilds a clean release binary, verifies that JS execution profiling markers are absent from `lambda.exe`, runs the standardized benchmark matrix, writes a matching `benchmark_results_vN.json`, and can generate an `Overall_ResultN.md` report from that JSON.

---

## 1. Benchmark Suites

| Suite | Dir | Count | Source | Focus |
|-------|-----|------:|--------|-------|
| **R7RS** | `r7rs/` | 10 | [r7rs-benchmarks](https://ecraven.github.io/r7rs-benchmarks/) | Scheme classics: recursion, numeric, closures |
| **AWFY** | `awfy/` | 14 | [Are We Fast Yet](https://github.com/smarr/are-we-fast-yet) | Cross-language OOP/micro/macro benchmarks |
| **BENG** | `beng/` | 10 | [Benchmarks Game](https://benchmarksgame-team.pages.debian.net/) | Real-world: GC, regex, FASTA, BigInt, N-body |
| **Kostya** | `kostya/` | 7 | [kostya/benchmarks](https://github.com/kostya/benchmarks) | Community: brainfuck, matmul, base64, JSON |
| **Larceny** | `larceny/` | 12 | [Larceny/Gabriel](https://www.larcenists.org/) | Gabriel suite: search, symbolic, allocation |
| **JetStream** | `jetstream/` | 9 | [JetStream](https://browserbench.org/JetStream/) | SunSpider/Octane classics: n-body, deltablue, richards, splay |

**Total: 62 benchmarks**

Each benchmark has a Lambda script (`.ls`), a JavaScript equivalent (`.js`), and where available a Python equivalent (`.py`).

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
| **QuickJS**    | `quickjs`  | Interpreter | `qjs --std -m wrapper.js`            |
| **Node.js**    | `nodejs`   | JIT (V8)    | `node script.js`                     |
| **Python**     | `python`   | Interpreter | `python3 script.py`                  |

- **MIR Direct**: Lambda → MIR IR → native. Default compiler path, lowest startup.
- **C2MIR**: Lambda → C source → MIR. Legacy path, sometimes better optimized.
- **LambdaJS**: Lambda's built-in JS JIT engine. No `require()` or `fs`.
- **Node.js**: Google V8 with optimizing JIT. Full Node.js API.
- **QuickJS**: Lightweight interpreter. Needs a polyfill wrapper (auto-generated).
- **Python**: CPython interpreter. AWFY benchmarks use the official AWFY Python harness.

### QuickJS wrapper

QuickJS lacks `process.hrtime.bigint()` and `console.log`. The runner auto-generates wrappers under `temp/` with:

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

### JetStream standardized wrappers

JetStream reference JS files expose a `Benchmark` class, but the per-file `runIteration()` count is not uniform. The standardized benchmark path does **not** time `new Benchmark().runIteration()` directly. For LambdaJS, QuickJS, and Node.js, `run_benchmarks.py` detects the underlying benchmark function from `runIteration()` and emits an explicit x8 timing wrapper:

```javascript
var _t0 = performance.now();
for (var _i = 0; _i < 8; _i++) { runBenchmarkFunction(); }
var _t1 = performance.now();
console.log("__TIMING__:" + (_t1 - _t0).toFixed(3));
```

This keeps the JS-engine JetStream workload comparable across LambdaJS, QuickJS, and Node.js and matches the older Result7-style workload. Non-JetStream suites use the checked-in timing loop/load in each benchmark file; do not change those ad hoc for a report run.

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

### 4.4 Python equivalent

1. Translate the same algorithm to Python.
2. Add timing and print `__TIMING__:<ms>\n`.
3. Save in the suite's `python/` subdirectory.

### 4.5 Register in the runner

Add the benchmark tuple to the appropriate list in `run_benchmarks.py`:

```python
R7RS = [
    ...
    ("name", "category", "test/benchmark/r7rs/name2.ls", "test/benchmark/r7rs/name2.js", "test/benchmark/r7rs/python/name.py"),
]
```

---

## 5. Running Benchmarks

All commands run from the **project root** (`/Users/henryluo/Projects/Lambda`).
The single unified runner is `test/benchmark/run_benchmarks.py`.

### Standard snapshot run

For checked-in result reports, use the standard workflow script instead of manually assembling commands:

```bash
python3 test/benchmark/run_standard_benchmarks.py --report-output test/benchmark/Overall_Result9.md --report-title "Lambda Benchmark Results: Round 9"
```

Default behavior:

- Runs `make release` first. Never benchmark a debug build.
- Checks `lambda.exe` with `strings` and rejects binaries containing profiling markers such as `JS_EXEC_PROFILE`.
- Clears `JS_EXEC_PROFILE` and `JS_EXEC_PROFILE_OUT` from the benchmark environment.
- Runs `test/benchmark/run_benchmarks.py -e mir,lambdajs,quickjs,nodejs -n 3 -t 180 --results-output test/benchmark/benchmark_results_v9.json`.
- Derives the matching JSON path from `--report-output`; for example, `Overall_Result9.md` pairs with `benchmark_results_v9.json`.
- If `--report-output` is provided, generates the report from the JSON via `test/benchmark/gen_overall_result.py`.

Use `--skip-build` only for a quick local recheck when you already know `lambda.exe` is a fresh release binary. Use `run_benchmarks.py` directly only for exploratory filters or one-off diagnosis.

To verify the exact command sequence and output pairing without running benchmarks:

```bash
python3 test/benchmark/run_standard_benchmarks.py --report-output test/benchmark/Overall_Result9.md --dry-run
```

### Modes

The runner supports three benchmark modes via `-m/--mode`:

| Mode | Description | Default runs | Output file |
|------|-------------|:------------:|-------------|
| **time** (default) | Execution time across all engines | 3 | `benchmark_results_v3.json` by default; snapshot runs use `benchmark_results_vN.json` |
| **memory** | Peak resident set size (RSS) via `/usr/bin/time` | 1 | `memory_results.json` |
| **mir-vs-c** | MIR Direct vs C2MIR transpiler wall-clock comparison | 3 | `temp/mir_vs_c_bench.csv` |

### Exploratory full run (all 6 suites × 6 engines)

```bash
python3 test/benchmark/run_benchmarks.py
```

- Default: 3 runs per benchmark per engine, reports median.
- Timeout: 120 seconds per run.
- Results merged into `test/benchmark/benchmark_results_v3.json`.
- This direct command is useful for exploration, but checked-in result reports should use `run_standard_benchmarks.py` so build mode, profiling checks, engines, timeout, and report generation are consistent.
- Direct runs can also write a chosen JSON with `--results-output test/benchmark/benchmark_results_vN.json`.

### Filtering by suite, benchmark, or engine

```bash
# Run only JetStream suite
python3 test/benchmark/run_benchmarks.py -s jetstream

# Run AWFY + BENG suites
python3 test/benchmark/run_benchmarks.py -s awfy,beng

# Run nbody & richards across all suites that have them
python3 test/benchmark/run_benchmarks.py -b nbody,richards

# Combine: AWFY mandelbrot + nbody only
python3 test/benchmark/run_benchmarks.py -s awfy -b mandelbrot,nbody

# Only MIR and Node.js engines
python3 test/benchmark/run_benchmarks.py -e mir,nodejs

# 5 runs per engine instead of default 3
python3 test/benchmark/run_benchmarks.py -b deltablue -n 5
```

Filters use **case-insensitive substring matching**: `-b nbody` matches `nbody` in all suites.

### Memory profiling

```bash
# Peak RSS for all benchmarks (default 1 run)
python3 test/benchmark/run_benchmarks.py -m memory

# Peak RSS for AWFY only, 3 runs
python3 test/benchmark/run_benchmarks.py -m memory -s awfy -n 3
```

Uses `/usr/bin/time -l` (macOS) or `/usr/bin/time -v` (Linux) to measure peak RSS.

### MIR Direct vs C2MIR comparison

```bash
# Compare MIR vs C2MIR for all suites (untyped only)
python3 test/benchmark/run_benchmarks.py -m mir-vs-c

# Include typed R7RS variants
python3 test/benchmark/run_benchmarks.py -m mir-vs-c --typed

# Only R7RS suite
python3 test/benchmark/run_benchmarks.py -m mir-vs-c -s r7rs --typed
```

Measures wall-clock time in microseconds. Only uses MIR and C2MIR engines.

### Listing and dry-run

```bash
# List all 62 benchmarks across 6 suites
python3 test/benchmark/run_benchmarks.py --list

# Preview what would run without executing
python3 test/benchmark/run_benchmarks.py --dry-run -b nbody -e mir,nodejs

# Run without saving results to JSON
python3 test/benchmark/run_benchmarks.py -b fib -s r7rs --no-save
```

### CLI reference

| Flag | Description |
|------|-------------|
| `-m, --mode` | `time` (default), `memory`, or `mir-vs-c` |
| `-s, --suite` | Comma-separated suite filter (substring match) |
| `-b, --bench` | Comma-separated benchmark filter (substring match) |
| `-e, --engines` | Comma-separated engine filter: `mir,c2mir,lambdajs,quickjs,nodejs,python` |
| `-n, --runs` | Number of runs per engine (default: 3 for time, 1 for memory) |
| `-t, --timeout` | Timeout per run in seconds (default: 120) |
| `--typed` | Include typed R7RS variants (mir-vs-c mode only) |
| `--list` | List all available benchmarks and exit |
| `--dry-run` | Show what would run without executing |
| `--no-save` | Don't write results to JSON/CSV |

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

# Python
python3 test/benchmark/r7rs/python/fib.py
```

### Prerequisites

| Dependency | Install |
|------------|---------|
| `./lambda.exe` | `make release` (must use release build for benchmarking) |
| `node` | [nodejs.org](https://nodejs.org/) |
| `qjs` | `brew install quickjs` (macOS) |
| `python3` | Pre-installed on macOS/Linux |
| AWFY source | `ref/are-we-fast-yet/` (git submodule or clone) |
| JetStream source | `ref/JetStream/` (for Node.js JetStream benchmarks) |

> **IMPORTANT**: Never benchmark with a debug build. Always use `make release`.

---

## 6. Results and Reporting

### Data files

| File | Mode | Format |
|------|------|--------|
| `benchmark_results_v3.json` | time | Exec time (ms) per engine per benchmark |
| `memory_results.json` | memory | Peak RSS (MB) + raw bytes per engine |
| `temp/mir_vs_c_bench.csv` | mir-vs-c | C2MIR vs MIR Direct (μs) + ratio |

`benchmark_results_v3.json` structure:

```json
{
  "r7rs": {
    "fib": {
      "mir": 1.96,
      "c2mir": 2.22,
      "lambdajs": 10.8,
      "quickjs": 17.4,
      "nodejs": 1.64,
      "python": 12.5
    }
  }
}
```

Each engine value is the **self-reported exec time in milliseconds** (median of N runs). A value of `null` means the benchmark failed or timed out. The runner operates in **merge mode** — only the benchmarks you run are overwritten; existing results are preserved.

### Generate the report

```bash
python3 test/benchmark/gen_overall_result.py --output test/benchmark/Overall_Result9.md --title "Lambda Benchmark Results: Round 9"
```

Reads the selected benchmark JSON and writes a chosen `Overall_ResultN.md` with:
- Run metadata: current date, platform, Lambda commit hash, Node.js version, and QuickJS version
- Per-suite tables for the selected engines
- Geometric mean ratios against Node.js per suite and overall
- The standardized JetStream x8 workload note

`gen_result3.py` and `gen_result_doc.py` are historical generators for older report formats. Do not use them for new checked-in result snapshots.

### Pipeline summary

```
prepare scripts  →  run benchmarks  →  report
 (.ls + .js + .py)   run_standard_benchmarks.py   gen_overall_result.py
                          ↓                    ↓
              benchmark_results_vN.json   Overall_ResultN.md
              memory_results.json
              temp/mir_vs_c_bench.csv
```

---

## 7. Methodology Notes

- **Median of N runs** (default N=3 for time, N=1 for memory). The median filters outliers from GC pauses or system load.
- **Self-reported exec time** is the primary comparison metric. It isolates algorithmic performance from process startup overhead (~15–40 ms for Lambda/Node).
- **Wall-clock time** is also measured internally but exec time is preferred when available.
- **Merge mode**: The runner only overwrites results for benchmarks you actually run. Existing results for other benchmarks are preserved in the JSON.
- **Process-group kill**: The runner uses `os.setsid` + `os.killpg` for reliable timeout handling, ensuring child processes don't leak.
- **Timeout**: 120 seconds per run (configurable via `-t`). MIR-vs-C mode auto-raises to 300s.
- **QuickJS wrappers** are auto-generated in `temp/` and not committed.
- **AWFY Python harness**: AWFY Python benchmarks run via the official AWFY `harness.py` with per-benchmark class name mapping and iteration counts.
- **JetStream JS wrappers**: Auto-generated in `temp/` for LambdaJS, QuickJS, and Node.js. The standardized report path runs the detected benchmark function x8 instead of using per-file `Benchmark.runIteration()` counts.
- **Known engine limitations**:
  - LambdaJS: No `require()`, no `fs`, limited ES6 class support (fails some AWFY).
  - QuickJS: No `fs` module (fails BENG file-reading benchmarks), stack overflow on deep recursion (fails `ack`).
  - MIR: Some `.ls` benchmarks fail due to runtime issues (recorded as `---` in results).
  - Python: Not all suites have Python ports. JetStream has Python for deltablue, richards, nbody only.

---

## 8. Transpile Profiling

The transpile profiler measures **compilation overhead** — the time Lambda spends parsing, building the AST, transpiling to MIR, and generating native code — before any user code executes. This is distinct from the execution benchmarks above, which measure runtime performance.

### What it measures

The profiler breaks each script's compilation into 4 phases:

| Phase | What happens |
|-------|-------------|
| **Tree-sitter Parse** | Source text → concrete syntax tree (CST) |
| **AST Build** | CST → typed abstract syntax tree (includes import resolution) |
| **MIR Transpile** | AST → MIR intermediate representation (`transpile_mir_ast` + `MIR_link`) |
| **JIT Codegen** | MIR → native machine code (`jit_init` + `jit_gen_func`) |

Timing is captured via `LAMBDA_PROFILE=1`, which enables instrumentation in the Lambda runtime. Profile data is written to `temp/phase_profile.txt` as a TSV file, then parsed by the Python script.

### Script discovery

The profiler discovers scripts from two source categories:

| Category | Directories | Filter |
|----------|------------|--------|
| **Benchmark suites** | `test/benchmark/{awfy,r7rs,beng,kostya,larceny}` | `.ls` files with matching `.txt` expected output |
| **Gtest scripts** | `test/lambda/`, `test/lambda/{chart,latex,math,proc}` | `.ls` files with matching `.txt` expected output |

Scripts that require features not supported by MIR Direct are automatically skipped (e.g., object-related tests).

### Running the profiler

```bash
# Profile all scripts (benchmark + gtest)
python3 test/benchmark/profile_transpile.py

# Only benchmark suite scripts
python3 test/benchmark/profile_transpile.py -s benchmark

# Only gtest scripts
python3 test/benchmark/profile_transpile.py -s gtest

# Filter by script name (substring match)
python3 test/benchmark/profile_transpile.py -b fib,nbody

# 3 iterations per script (uses median)
python3 test/benchmark/profile_transpile.py -n 3

# Sort by transpile phase time
python3 test/benchmark/profile_transpile.py --sort transpile

# Show only top 20 slowest scripts
python3 test/benchmark/profile_transpile.py --top 20

# Also produce CSV output
python3 test/benchmark/profile_transpile.py --csv

# List all discovered scripts without running
python3 test/benchmark/profile_transpile.py --list
```

### CLI reference

| Flag | Description |
|------|-------------|
| `-s, --suite` | Comma-separated suite filter: `benchmark`, `gtest`, `awfy`, `r7rs`, `beng`, `kostya`, `larceny`, `lambda`, `chart`, `latex`, `math`, `proc` |
| `-b, --benchmarks` | Comma-separated script name filter (substring match) |
| `-n, --iterations` | Number of iterations per script (default: 1, uses median) |
| `--sort` | Sort by: `total` (default), `parse`, `ast`, `transpile`, `jit`, `name` |
| `--top` | Show only top N slowest scripts |
| `--csv` | Also produce CSV output alongside the Markdown report |
| `--timeout` | Timeout per script in seconds (default: 60) |
| `--list` | List all discovered scripts and exit |
| `--exe` | Path to lambda executable (default: `./lambda.exe`) |

### Report format

The report splits results into two sets:

- **Set 1: Standalone scripts** (modules == 1) — single-module timings that directly reflect each compilation phase. MIR Transpile typically dominates (~70%).
- **Set 2: Scripts with imports** (modules > 1) — multi-module timings where AST Build dominates (~80%) due to recursive import resolution. A "Modules" column shows how many modules were compiled.

Each set includes:
- Phase summary table (total, average, percentage)
- Per-suite breakdown table
- Per-script breakdown sorted by total time (descending)

### Output files

| File | Description |
|------|-------------|
| `temp/transpiling_result.md` | Full Markdown report (auto-generated) |
| `temp/transpiling_result.csv` | CSV export (when `--csv` is used) |
| `Transpile_Result.md` | Published report (copy from `temp/`) |
| `temp/phase_profile.txt` | Raw TSV profile data (transient, per-script) |

---

## 9. File Reference

| File | Purpose |
|------|---------|
| `run_standard_benchmarks.py` | **Canonical snapshot workflow**: clean release build, profiling-marker check, standardized engine matrix, optional report generation |
| `run_benchmarks.py` | **Unified runner**: 6 suites × 6 engines, time/memory/mir-vs-c modes |
| `gen_overall_result.py` | Generates current `Overall_ResultN.md` reports from a matching benchmark JSON |
| `profile_transpile.py` | **Transpile profiler**: compilation phase timing for all Lambda scripts |
| `gen_result3.py` | Historical generator for `Overall_Result3.md` |
| `summarize_benchmarks.py` | Prints summary tables to stdout |
| `benchmark_results_vN.json` | Versioned execution time results (ms per engine per benchmark) for checked-in snapshots |
| `benchmark_results_v3.json` | Historical/default execution time result file used by older direct runs |
| `memory_results.json` | Peak RSS memory results (MB + raw bytes) |
| `Overall_ResultN.md` | Current generated result reports, created from matching `benchmark_results_vN.json` files |
| `Overall_Result3.md` | Historical Round 3 report |
| `Transpile_Result.md` | Transpile profiling report (standalone vs with-imports) |
| `awfy/awfy_helper.js` | AWFY timing wrapper for Node.js |
| `awfy/*_bundle.js` | Standalone AWFY bundles for LambdaJS/QuickJS |
| `_old/` | Archived previous runner scripts (for reference) |
