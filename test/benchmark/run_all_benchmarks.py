#!/usr/bin/env python3
"""Comprehensive Benchmark Runner — All 5 suites × 5 engines

Engines: MIR Direct, C2MIR, Lambda JS, QuickJS, Node.js
Suites:  r7rs, awfy, beng, kostya, larceny

Measures:
  Set 1: Total wall-clock time (median of N runs)
  Set 2: Self-reported exec time (__TIMING__:NNN in stdout, ms)
"""

import subprocess
import time
import sys
import os
import re
import json

NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3
TIMEOUT_S = 120  # per-run timeout

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

LAMBDA_EXE = "./lambda.exe"
NODE_EXE = "node"
QJS_EXE = "qjs"

TIMING_RE = re.compile(r"__TIMING__:([\d.]+(?:e[+-]?\d+)?)")

# ============================================================
# Benchmark definitions: (name, ls_path, js_path_or_None)
# For r7rs and awfy: use *2.ls (typed versions with __TIMING__)
# ============================================================

R7RS = [
    ("fib",     "test/benchmark/r7rs/fib2.ls",     "test/benchmark/r7rs/fib2.js"),
    ("fibfp",   "test/benchmark/r7rs/fibfp2.ls",   "test/benchmark/r7rs/fibfp2.js"),
    ("tak",     "test/benchmark/r7rs/tak2.ls",     "test/benchmark/r7rs/tak2.js"),
    ("cpstak",  "test/benchmark/r7rs/cpstak2.ls",  "test/benchmark/r7rs/cpstak2.js"),
    ("sum",     "test/benchmark/r7rs/sum2.ls",     "test/benchmark/r7rs/sum2.js"),
    ("sumfp",   "test/benchmark/r7rs/sumfp2.ls",   "test/benchmark/r7rs/sumfp2.js"),
    ("nqueens", "test/benchmark/r7rs/nqueens2.ls",  "test/benchmark/r7rs/nqueens2.js"),
    ("fft",     "test/benchmark/r7rs/fft2.ls",     "test/benchmark/r7rs/fft2.js"),
    ("mbrot",   "test/benchmark/r7rs/mbrot2.ls",   "test/benchmark/r7rs/mbrot2.js"),
    ("ack",     "test/benchmark/r7rs/ack2.ls",     "test/benchmark/r7rs/ack2.js"),
]

AWFY = [
    ("sieve",       "test/benchmark/awfy/sieve2.ls",      "test/benchmark/awfy/sieve2.js"),
    ("permute",     "test/benchmark/awfy/permute2.ls",    "test/benchmark/awfy/permute2.js"),
    ("queens",      "test/benchmark/awfy/queens2.ls",     "test/benchmark/awfy/queens2.js"),
    ("towers",      "test/benchmark/awfy/towers2.ls",     "test/benchmark/awfy/towers2.js"),
    ("bounce",      "test/benchmark/awfy/bounce2.ls",     "test/benchmark/awfy/bounce2.js"),
    ("list",        "test/benchmark/awfy/list2.ls",       "test/benchmark/awfy/list2.js"),
    ("storage",     "test/benchmark/awfy/storage2.ls",    "test/benchmark/awfy/storage2.js"),
    ("mandelbrot",  "test/benchmark/awfy/mandelbrot2.ls", "test/benchmark/awfy/mandelbrot2.js"),
    ("nbody",       "test/benchmark/awfy/nbody2.ls",      "test/benchmark/awfy/nbody2.js"),
    ("richards",    "test/benchmark/awfy/richards2.ls",   "test/benchmark/awfy/richards2.js"),
    ("json",        "test/benchmark/awfy/json2.ls",       "test/benchmark/awfy/json2.js"),
    ("deltablue",   "test/benchmark/awfy/deltablue2.ls",  "test/benchmark/awfy/deltablue2.js"),
    ("havlak",      "test/benchmark/awfy/havlak2.ls",     "test/benchmark/awfy/havlak2.js"),
    ("cd",          "test/benchmark/awfy/cd2.ls",         "test/benchmark/awfy/cd2.js"),
]

BENG = [
    ("binarytrees",  "test/benchmark/beng/binarytrees.ls",  "test/benchmark/beng/js/binarytrees.js"),
    ("fannkuch",     "test/benchmark/beng/fannkuch.ls",     "test/benchmark/beng/js/fannkuch.js"),
    ("fasta",        "test/benchmark/beng/fasta.ls",        "test/benchmark/beng/js/fasta.js"),
    ("knucleotide",  "test/benchmark/beng/knucleotide.ls",  "test/benchmark/beng/js/knucleotide.js"),
    ("mandelbrot",   "test/benchmark/beng/mandelbrot.ls",   "test/benchmark/beng/js/mandelbrot.js"),
    ("nbody",        "test/benchmark/beng/nbody.ls",        "test/benchmark/beng/js/nbody.js"),
    ("pidigits",     "test/benchmark/beng/pidigits.ls",     "test/benchmark/beng/js/pidigits.js"),
    ("regexredux",   "test/benchmark/beng/regexredux.ls",   "test/benchmark/beng/js/regexredux.js"),
    ("revcomp",      "test/benchmark/beng/revcomp.ls",      "test/benchmark/beng/js/revcomp.js"),
    ("spectralnorm", "test/benchmark/beng/spectralnorm.ls",  "test/benchmark/beng/js/spectralnorm.js"),
]

KOSTYA = [
    ("brainfuck",   "test/benchmark/kostya/brainfuck.ls",   "test/benchmark/kostya/brainfuck.js"),
    ("matmul",      "test/benchmark/kostya/matmul.ls",      "test/benchmark/kostya/matmul.js"),
    ("primes",      "test/benchmark/kostya/primes.ls",      "test/benchmark/kostya/primes.js"),
    ("base64",      "test/benchmark/kostya/base64.ls",      "test/benchmark/kostya/base64.js"),
    ("levenshtein", "test/benchmark/kostya/levenshtein.ls",  "test/benchmark/kostya/levenshtein.js"),
    ("json_gen",    "test/benchmark/kostya/json_gen.ls",     "test/benchmark/kostya/json_gen.js"),
    ("collatz",     "test/benchmark/kostya/collatz.ls",      "test/benchmark/kostya/collatz.js"),
]

LARCENY = [
    ("triangl",    "test/benchmark/larceny/triangl.ls",    "test/benchmark/larceny/triangl.js"),
    ("array1",     "test/benchmark/larceny/array1.ls",     "test/benchmark/larceny/array1.js"),
    ("deriv",      "test/benchmark/larceny/deriv.ls",      "test/benchmark/larceny/deriv.js"),
    ("diviter",    "test/benchmark/larceny/diviter.ls",     "test/benchmark/larceny/diviter.js"),
    ("divrec",     "test/benchmark/larceny/divrec.ls",     "test/benchmark/larceny/divrec.js"),
    ("gcbench",    "test/benchmark/larceny/gcbench.ls",     "test/benchmark/larceny/gcbench.js"),
    ("paraffins",  "test/benchmark/larceny/paraffins.ls",   "test/benchmark/larceny/paraffins.js"),
    ("pnpoly",     "test/benchmark/larceny/pnpoly.ls",     "test/benchmark/larceny/pnpoly.js"),
    ("primes",     "test/benchmark/larceny/primes.ls",     "test/benchmark/larceny/primes.js"),
    ("puzzle",     "test/benchmark/larceny/puzzle.ls",     "test/benchmark/larceny/puzzle.js"),
    ("quicksort",  "test/benchmark/larceny/quicksort.ls",   "test/benchmark/larceny/quicksort.js"),
    ("ray",        "test/benchmark/larceny/ray.ls",        "test/benchmark/larceny/ray.js"),
]

ALL_SUITES = [
    ("r7rs",    R7RS),
    ("awfy",    AWFY),
    ("beng",    BENG),
    ("kostya",  KOSTYA),
    ("larceny", LARCENY),
]

# QuickJS polyfill: replace process.hrtime.bigint() with performance.now()
QJS_POLYFILL = """
if (typeof process === 'undefined') {
    globalThis.process = {
        stdout: { write: function(s) { std.out.puts(s); std.out.flush(); } },
        argv: ['-', '-'],
        hrtime: { bigint: function() { return BigInt(Math.round(performance.now() * 1e6)); } },
        exit: function(code) { std.exit(code); }
    };
}
if (typeof console === 'undefined') {
    globalThis.console = { log: function() { std.out.puts(Array.prototype.join.call(arguments, ' ') + '\\n'); std.out.flush(); } };
}
"""


def parse_timing(stdout):
    """Extract __TIMING__:NNN from stdout, return ms or None."""
    for line in stdout.strip().split("\n"):
        m = TIMING_RE.search(line.strip())
        if m:
            return float(m.group(1))
    return None


def run_once(cmd, timeout=TIMEOUT_S):
    """Run cmd once, return (wall_ms, exec_ms_or_None, success)."""
    start = time.perf_counter_ns()
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return (timeout * 1000, None, False)
    end = time.perf_counter_ns()
    wall_ms = (end - start) / 1_000_000
    if r.returncode != 0:
        return (wall_ms, None, False)
    exec_ms = parse_timing(r.stdout)
    return (wall_ms, exec_ms, True)


def run_benchmark(cmd, num_runs):
    """Run cmd N times, return (median_wall_ms, median_exec_ms_or_None, success)."""
    walls = []
    execs = []
    ok = False
    for _ in range(num_runs):
        w, e, success = run_once(cmd)
        if success:
            ok = True
            walls.append(w)
            if e is not None:
                execs.append(e)
        print(".", end="", flush=True)
    if not ok:
        return (None, None, False)
    walls.sort()
    median_w = walls[len(walls) // 2]
    median_e = None
    if execs:
        execs.sort()
        median_e = execs[len(execs) // 2]
    return (median_w, median_e, True)


def make_qjs_wrapper(js_path):
    """Create a QuickJS-compatible wrapper for a Node.js script."""
    wrapper = os.path.join("temp", "qjs_" + os.path.basename(js_path))  # temp/ is fine for transient wrapper files
    with open(js_path) as f:
        code = f.read()
    # Replace process.hrtime.bigint() timing with performance.now() based timing
    # We apply the polyfill before the script code
    with open(wrapper, "w") as f:
        f.write("import * as std from 'std';\n")
        f.write(QJS_POLYFILL)
        # Transform the code: remove 'use strict' (not compatible as module)
        code = code.replace("'use strict';", "")
        code = code.replace('"use strict";', "")
        f.write(code)
    return wrapper


def fmt_ms(ms):
    """Format milliseconds for display."""
    if ms is None:
        return "failed"
    if ms < 1:
        return f"{ms:.3f} ms"
    elif ms < 1000:
        return f"{ms:.1f} ms"
    else:
        return f"{ms/1000:.3f} s"


def fmt_ms_short(ms):
    """Short format for table."""
    if ms is None:
        return "---"
    if ms < 1:
        return f"{ms:.2f}"
    elif ms < 1000:
        return f"{ms:.0f}"
    else:
        return f"{ms:.0f}"


# ============================================================
# Main
# ============================================================
print(f"{'='*80}")
print(f"Comprehensive Benchmark Suite — 5 Suites × 5 Engines")
print(f"{'='*80}")
print(f"  Runs per benchmark : {NUM_RUNS}")
print(f"  Timeout            : {TIMEOUT_S}s")
print(f"  Lambda             : {LAMBDA_EXE}")
print(f"  Node.js            : {NODE_EXE}")
print(f"  QuickJS            : {QJS_EXE}")
print()

# Verify executables
for exe in [LAMBDA_EXE, NODE_EXE, QJS_EXE]:
    r = subprocess.run(f"which {exe}", shell=True, capture_output=True)
    if r.returncode != 0 and not os.path.exists(exe):
        print(f"  WARNING: {exe} not found!")

# Results storage: results[suite][bench][engine] = (wall_ms, exec_ms)
results = {}

for suite_name, benchmarks in ALL_SUITES:
    print(f"\n{'='*80}")
    print(f"  Suite: {suite_name.upper()} ({len(benchmarks)} benchmarks)")
    print(f"{'='*80}")
    results[suite_name] = {}

    for bench_name, ls_path, js_path in benchmarks:
        results[suite_name][bench_name] = {}
        print(f"\n  {bench_name}:")

        # Skip if ls file doesn't exist
        if not os.path.exists(ls_path):
            print(f"    SKIPPED (file not found: {ls_path})")
            continue

        # --- MIR Direct ---
        print(f"    MIR      ", end="", flush=True)
        cmd = f"{LAMBDA_EXE} run {ls_path}"
        w, e, ok = run_benchmark(cmd, NUM_RUNS)
        results[suite_name][bench_name]["mir"] = (w, e) if ok else (None, None)
        print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")

        # --- C2MIR ---
        print(f"    C2MIR    ", end="", flush=True)
        cmd = f"{LAMBDA_EXE} run --c2mir {ls_path}"
        w, e, ok = run_benchmark(cmd, NUM_RUNS)
        results[suite_name][bench_name]["c2mir"] = (w, e) if ok else (None, None)
        print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")

        # For AWFY, use bundle files for LambdaJS/QuickJS (no require() needed)
        bundle_path = js_path.replace("2.js", "2_bundle.js") if js_path else None
        standalone_js = bundle_path if (suite_name == "awfy" and bundle_path and os.path.exists(bundle_path)) else js_path

        # --- Lambda JS engine ---
        if standalone_js and os.path.exists(standalone_js):
            print(f"    LambdaJS ", end="", flush=True)
            cmd = f"{LAMBDA_EXE} js {standalone_js}"
            w, e, ok = run_benchmark(cmd, NUM_RUNS)
            results[suite_name][bench_name]["lambdajs"] = (w, e) if ok else (None, None)
            print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")
        else:
            results[suite_name][bench_name]["lambdajs"] = (None, None)
            print(f"    LambdaJS  no JS file")

        # --- Node.js (can handle require(), use original js_path) ---
        if js_path and os.path.exists(js_path):
            print(f"    Node.js  ", end="", flush=True)
            cmd = f"{NODE_EXE} {js_path}"
            w, e, ok = run_benchmark(cmd, NUM_RUNS)
            results[suite_name][bench_name]["nodejs"] = (w, e) if ok else (None, None)
            print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")
        else:
            results[suite_name][bench_name]["nodejs"] = (None, None)
            print(f"    Node.js   no JS file")

        # --- QuickJS (use standalone bundle, wrapped with polyfill) ---
        if standalone_js and os.path.exists(standalone_js):
            print(f"    QuickJS  ", end="", flush=True)
            wrapper = make_qjs_wrapper(standalone_js)
            cmd = f"{QJS_EXE} --std -m {wrapper}"
            w, e, ok = run_benchmark(cmd, NUM_RUNS)
            results[suite_name][bench_name]["quickjs"] = (w, e) if ok else (None, None)
            print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")
        else:
            results[suite_name][bench_name]["quickjs"] = (None, None)
            print(f"    QuickJS   no JS file")


# ============================================================
# Results tables
# ============================================================
ENGINES = ["mir", "c2mir", "lambdajs", "quickjs", "nodejs"]
ENGINE_LABELS = {"mir": "MIR", "c2mir": "C2MIR", "lambdajs": "LambdaJS", "quickjs": "QuickJS", "nodejs": "Node.js"}

print(f"\n\n{'='*120}")
print(f"SET 1: TOTAL WALL-CLOCK TIME (ms, median of {NUM_RUNS} runs)")
print(f"{'='*120}")

for suite_name, benchmarks in ALL_SUITES:
    print(f"\n--- {suite_name.upper()} ---")
    hdr = f"{'Benchmark':16s}"
    for eng in ENGINES:
        hdr += f" {ENGINE_LABELS[eng]:>12s}"
    print(hdr)
    print("-" * (16 + 13 * len(ENGINES)))

    for bench_name, _, _ in benchmarks:
        if bench_name not in results[suite_name]:
            continue
        row = f"{bench_name:16s}"
        for eng in ENGINES:
            w, e = results[suite_name][bench_name].get(eng, (None, None))
            row += f" {fmt_ms_short(w):>12s}"
        print(row)

print(f"\n\n{'='*120}")
print(f"SET 2: SELF-REPORTED EXEC TIME (ms, median of {NUM_RUNS} runs)")
print(f"{'='*120}")

for suite_name, benchmarks in ALL_SUITES:
    print(f"\n--- {suite_name.upper()} ---")
    hdr = f"{'Benchmark':16s}"
    for eng in ENGINES:
        hdr += f" {ENGINE_LABELS[eng]:>12s}"
    print(hdr)
    print("-" * (16 + 13 * len(ENGINES)))

    for bench_name, _, _ in benchmarks:
        if bench_name not in results[suite_name]:
            continue
        row = f"{bench_name:16s}"
        for eng in ENGINES:
            w, e = results[suite_name][bench_name].get(eng, (None, None))
            row += f" {fmt_ms_short(e):>12s}"
        print(row)

# Save JSON
json_path = "test/benchmark/benchmark_results.json"
with open(json_path, "w") as f:
    json.dump(results, f, indent=2, default=str)
print(f"\nRaw data saved to {json_path}")
