#!/usr/bin/env python3
"""Larceny/Gambit Benchmark Runner — Lambda Script

Runs extended Larceny/Gambit benchmark suite in Lambda Script.
These benchmarks complement the R7RS suite with additional classic
Scheme/Lisp benchmarks.

Usage: python3 test/benchmark/larceny/run_bench.py [num_runs]
"""

import subprocess
import time
import sys
import os
import statistics

NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.join(SCRIPT_DIR, "..", "..", "..")
os.chdir(PROJECT_ROOT)

LAMBDA_EXE = "./lambda.exe"
LS_DIR = "test/benchmark/larceny"

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

# Lambda startup overhead (measured separately)
LAMBDA_STARTUP_MS = 10


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


def run_external(cmd, num_runs):
    """Run cmd N times externally, return list of times in ms."""
    times = []
    for i in range(num_runs):
        start = time.perf_counter_ns()
        try:
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=300)
        except subprocess.TimeoutExpired:
            times.append(300_000)  # 300s timeout marker
            print("T", end="", flush=True)
            continue
        end = time.perf_counter_ns()
        elapsed_ms = (end - start) / 1_000_000
        compute_ms = max(0.01, elapsed_ms - LAMBDA_STARTUP_MS)
        times.append(compute_ms)
        if r.returncode != 0 and i == 0:
            stderr_short = r.stderr[:120].replace('\n', ' ') if r.stderr else ''
            print(f" [exit {r.returncode}: {stderr_short}]", end="", flush=True)
        elif i == 0 and "FAIL" in r.stdout:
            print(f" [FAIL: {r.stdout.strip()[:60]}]", end="", flush=True)
        print(".", end="", flush=True)
    return times


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
print(f"\033[1mLarceny/Gambit Benchmark Suite — Lambda Script\033[0m")
print("=" * 70)
print(f"  Runs per benchmark : {NUM_RUNS}")
print(f"  Lambda             : Lambda Script v1.0 (C2MIR JIT)")
print(f"  Timing method      : Wall-clock minus ~{LAMBDA_STARTUP_MS}ms startup")
print()

# ============================================================
# Run benchmarks
# ============================================================
print(f"\033[1mRunning benchmarks ({NUM_RUNS} runs each)\033[0m")
print("-" * 70)

results = {}
for name, cat, desc in BENCHMARKS:
    script = f"{LS_DIR}/{name}.ls"
    if not os.path.exists(script):
        print(f"  {name:12s} SKIPPED (file not found)")
        continue
    print(f"  {name:12s}", end="", flush=True)
    times = run_external(f"{LAMBDA_EXE} run {script}", NUM_RUNS)
    m = med(times)
    results[name] = m
    print(f"  median = {format_time_ms(m):>10s} ({desc})")

print()

# ============================================================
# Results Summary
# ============================================================
SEP = "=" * 70
print(f"\033[1m{SEP}\033[0m")
print(f"\033[1m{'RESULTS SUMMARY':^70s}\033[0m")
print(f"\033[1m{SEP}\033[0m")
print()

hdr = f"{'Benchmark':12s} {'Category':10s} {'Time':>12s} {'Description'}"
print(hdr)
print(f"{'-'*12} {'-'*10} {'-'*12} {'-'*40}")

for name, cat, desc in BENCHMARKS:
    if name in results:
        t = results[name]
        print(f"{name:12s} {cat:10s} {format_time_ms(t):>12s} {desc}")

if results:
    times_list = [v for v in results.values() if v > 0]
    if times_list:
        print()
        total = sum(times_list)
        gm = geo_mean(times_list)
        print(f"  Total time     : {format_time_ms(total)}")
        print(f"  Geometric mean : {format_time_ms(gm)}")

print()

# ============================================================
# Save CSV
# ============================================================
csv_path = f"{LS_DIR}/bench_results.csv"
with open(csv_path, "w") as f:
    f.write(f"# Larceny/Gambit Benchmark Results {time.strftime('%Y-%m-%d %H:%M:%S')}, runs={NUM_RUNS}\n")
    f.write("benchmark,category,lambda_ms\n")
    for name, cat, desc in BENCHMARKS:
        t = results.get(name, 0)
        f.write(f"{name},{cat},{t:.3f}\n")
print(f"Raw data saved to {csv_path}")
