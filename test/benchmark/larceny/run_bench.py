#!/usr/bin/env python3
"""Larceny/Gambit Benchmark Runner — Lambda Script vs Python

Runs extended Larceny/Gambit benchmark suite in Lambda Script and Python.
These benchmarks complement the R7RS suite with additional classic
Scheme/Lisp benchmarks.

Usage: python3 test/benchmark/larceny/run_bench.py [num_runs]
"""

import subprocess
import time
import sys
import os
import re
import statistics

NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.join(SCRIPT_DIR, "..", "..", "..")
os.chdir(PROJECT_ROOT)

LAMBDA_EXE = "./lambda.exe"
PYTHON_EXE = "python3"
LS_DIR = "test/benchmark/larceny"
PY_DIR = "test/benchmark/larceny/python"

# Benchmark configs: (name, category, description)
BENCHMARKS = [
    ("deriv",     "alloc",       "Symbolic differentiation (5000 iters)"),
    ("primes",    "array",       "Sieve of Eratosthenes to 8000 (10 iters)"),
    ("pnpoly",    "numeric",     "Point-in-polygon classification (100K pts)"),
    ("diviter",   "iterative",   "Iterative division (1000 iters)"),
    ("divrec",    "recursive",   "Recursive division (1000 iters)"),
    ("array1",    "array",       "Array fill & sum (10000 elems, 100 iters)"),
    ("gcbench",   "gc",          "GC stress: binary trees depth 4-14"),
    ("quicksort", "sort",        "Quicksort 5000 elements (10 iters)"),
    ("triangl",   "backtrack",   "Triangle solitaire solutions"),
    ("puzzle",    "backtrack",   "N-Queens n=10 all solutions"),
    ("ray",       "numeric",     "Ray tracer 100x100 (10 iters)"),
    ("paraffins", "recursive",   "Paraffin isomer counting nb(1..23) x10"),
]

# Language startup overheads
LAMBDA_STARTUP_MS = 10
PYTHON_STARTUP_MS = 15

TIMING_RE = re.compile(r"^__TIMING__:([\d.]+(?:e[+-]?\d+)?)")


def format_time_ms(ms):
    """Format milliseconds to human-readable string."""
    if ms <= 0:
        return "< 0.1 ms"
    if ms < 1.0:
        return f"{ms:.3f} ms"
    elif ms < 1000:
        return f"{ms:.1f} ms"
    else:
        return f"{ms / 1000:.3f} s"


def parse_timing(stdout):
    """Extract __TIMING__:NNN.NNN from stdout, return ms or None."""
    for line in stdout.strip().split("\n"):
        m = TIMING_RE.match(line.strip())
        if m:
            return float(m.group(1))
    return None


def run_external(cmd, num_runs, startup_ms):
    """Run cmd N times externally, return (wall_times, exec_times) in ms."""
    wall_times = []
    exec_times = []
    for i in range(num_runs):
        start = time.perf_counter_ns()
        try:
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=300)
        except subprocess.TimeoutExpired:
            wall_times.append(300_000)
            exec_times.append(300_000)
            print("T", end="", flush=True)
            continue
        end = time.perf_counter_ns()
        elapsed_ms = (end - start) / 1_000_000
        wall_ms = max(0.01, elapsed_ms - startup_ms)
        wall_times.append(wall_ms)

        et = parse_timing(r.stdout)
        exec_times.append(et if et is not None else wall_ms)

        if r.returncode != 0 and i == 0:
            stderr_short = r.stderr[:120].replace('\n', ' ') if r.stderr else ''
            print(f" [exit {r.returncode}: {stderr_short}]", end="", flush=True)
        elif i == 0 and "FAIL" in r.stdout:
            print(f" [FAIL: {r.stdout.strip()[:60]}]", end="", flush=True)
        print(".", end="", flush=True)
    return wall_times, exec_times


def med(times):
    """Return median of times (ms)."""
    return statistics.median(sorted(times))


def geo_mean(values):
    """Geometric mean of a list of positive values."""
    if not values:
        return 0
    product = 1.0
    for v in values:
        product *= v
    return product ** (1.0 / len(values))


# ============================================================
# Header
# ============================================================
py_ver = subprocess.run(f"{PYTHON_EXE} --version", shell=True, capture_output=True, text=True).stdout.strip()
print(f"\033[1mLarceny/Gambit Benchmark Suite — Lambda Script vs Python\033[0m")
print("=" * 78)
print(f"  Runs per benchmark : {NUM_RUNS}")
print(f"  Lambda             : Lambda Script v1.0 (MIR JIT)")
print(f"  Python             : {py_ver}")
print(f"  Timing method      : Wall-clock + in-script __TIMING__")
print()

# ============================================================
# Phase 1: Lambda benchmarks
# ============================================================
print(f"\033[1mPhase 1: Lambda Script ({NUM_RUNS} runs each)\033[0m")
print("-" * 78)

lm_wall = {}
lm_exec = {}
for name, cat, desc in BENCHMARKS:
    script = f"{LS_DIR}/{name}.ls"
    if not os.path.exists(script):
        print(f"  {name:12s} SKIPPED (file not found)")
        continue
    print(f"  {name:12s}", end="", flush=True)
    wt, et = run_external(f"{LAMBDA_EXE} run {script}", NUM_RUNS, LAMBDA_STARTUP_MS)
    mw = med(wt)
    me = med(et)
    lm_wall[name] = mw
    lm_exec[name] = me
    jit_ms = max(0, mw - me)
    print(f"  wall={format_time_ms(mw):>10s}  exec={format_time_ms(me):>10s}  jit={format_time_ms(jit_ms):>8s}  ({desc})")

print()

# ============================================================
# Phase 2: Python benchmarks
# ============================================================
print(f"\033[1mPhase 2: Python ({NUM_RUNS} runs each)\033[0m")
print("-" * 78)

py_wall = {}
py_exec = {}
for name, cat, desc in BENCHMARKS:
    script = f"{PY_DIR}/{name}.py"
    if not os.path.exists(script):
        print(f"  {name:12s} SKIPPED (file not found)")
        continue
    print(f"  {name:12s}", end="", flush=True)
    wt, et = run_external(f"{PYTHON_EXE} {script}", NUM_RUNS, PYTHON_STARTUP_MS)
    mw = med(wt)
    me = med(et)
    py_wall[name] = mw
    py_exec[name] = me
    print(f"  wall={format_time_ms(mw):>10s}  exec={format_time_ms(me):>10s}")

print()

# ============================================================
# Results Summary
# ============================================================
SEP = "=" * 86
print(f"\033[1m{SEP}\033[0m")
print(f"\033[1m{'RESULTS SUMMARY':^86s}\033[0m")
print(f"\033[1m{SEP}\033[0m")
print()

print(f"\033[36mExecution time comparison (exec = internal __TIMING__, wall = startup-subtracted)\033[0m")
print(f"{'Benchmark':12s} {'Lm(exec)':>10s} {'Py(exec)':>10s} {'Lm(wall)':>10s} {'Py(wall)':>10s} {'Lm/Py(exec)':>12s}")
print(f"{'-'*12} {'-'*10} {'-'*10} {'-'*10} {'-'*10} {'-'*12}")

ratios = []
lw = pw = 0
for name, cat, desc in BENCHMARKS:
    le = lm_exec.get(name, 0)
    pe = py_exec.get(name, 0)
    lw_t = lm_wall.get(name, 0)
    pw_t = py_wall.get(name, 0)
    if le > 0 and pe > 0:
        r = le / pe
        rs = f"{r:.2f}x"
        ratios.append(r)
        if r <= 1.0: lw += 1
        else: pw += 1
    else:
        rs = "N/A"
    print(f"{name:12s} {format_time_ms(le):>10s} {format_time_ms(pe):>10s} {format_time_ms(lw_t):>10s} {format_time_ms(pw_t):>10s} {rs:>12s}")

if ratios:
    gm = geo_mean(ratios)
    print()
    print(f"  Geo mean Lambda/Python (exec): {gm:.2f}x")
    print(f"  Lambda wins: {lw}  |  Python wins: {pw}")
    le_list = [v for v in lm_exec.values() if v > 0]
    pe_list = [v for v in py_exec.values() if v > 0]
    if le_list and pe_list:
        print(f"  Total Lambda exec : {format_time_ms(sum(le_list))}")
        print(f"  Total Python exec : {format_time_ms(sum(pe_list))}")

print()
print("Legend:")
print("  exec = pure benchmark execution time from __TIMING__ (no startup)")
print("  wall = total wall-clock minus language startup overhead")
print("  Ratio < 1.0 = Lambda faster; > 1.0 = Python faster")
print()

# ============================================================
# Save CSV
# ============================================================
csv_path = f"{LS_DIR}/bench_results.csv"
with open(csv_path, "w") as f:
    f.write(f"# Larceny/Gambit Benchmark Results {time.strftime('%Y-%m-%d %H:%M:%S')}, runs={NUM_RUNS}\n")
    f.write("benchmark,category,lm_wall_ms,lm_exec_ms,py_wall_ms,py_exec_ms,lm_py_ratio\n")
    for name, cat, desc in BENCHMARKS:
        lw_t = lm_wall.get(name, 0)
        le = lm_exec.get(name, 0)
        pw_t = py_wall.get(name, 0)
        pe = py_exec.get(name, 0)
        ratio = f"{le/pe:.3f}" if le > 0 and pe > 0 else "N/A"
        f.write(f"{name},{cat},{lw_t:.3f},{le:.3f},{pw_t:.3f},{pe:.3f},{ratio}\n")
print(f"Raw data saved to {csv_path}")
