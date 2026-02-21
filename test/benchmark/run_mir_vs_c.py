#!/usr/bin/env python3
"""MIR Direct vs C2MIR Transpiler Benchmark Comparison

Runs all R7RS and AWFY benchmarks through both transpiler paths
and compares execution time.

Usage: python3 test/benchmark/run_mir_vs_c.py [num_runs]
"""

import subprocess
import time
import sys
import os
import statistics
import math

NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.join(SCRIPT_DIR, "..", "..")
os.chdir(PROJECT_ROOT)

LAMBDA_EXE = "./lambda.exe"

# R7RS benchmarks: (name, category)
R7RS_BENCHMARKS = [
    ("fib",     "recursive"),
    ("fibfp",   "recursive"),
    ("tak",     "recursive"),
    ("cpstak",  "closure"),
    ("sum",     "iterative"),
    ("sumfp",   "iterative"),
    ("nqueens", "backtrack"),
    ("fft",     "numeric"),
    ("mbrot",   "numeric"),
    ("ack",     "recursive"),
]

# AWFY benchmarks: (name, category)
AWFY_BENCHMARKS = [
    ("sieve",       "micro"),
    ("permute",     "micro"),
    ("queens",      "micro"),
    ("towers",      "micro"),
    ("bounce",      "micro"),
    ("list",        "micro"),
    ("storage",     "micro"),
    ("mandelbrot",  "micro"),
    ("nbody",       "micro"),
    ("richards",    "macro"),
    ("json",        "macro"),
    ("deltablue",   "macro"),
    ("havlak",      "macro"),
    ("cd",          "macro"),
]

def format_time(us):
    if us <= 0:
        return "N/A"
    if us < 1000:
        return f"{us:.0f} us"
    elif us < 1000000:
        return f"{us/1000:.1f} ms"
    else:
        return f"{us/1000000:.3f} s"

def run_benchmark(cmd, num_runs, timeout_s=300):
    """Run cmd N times externally, return list of times in microseconds."""
    times = []
    for i in range(num_runs):
        start = time.perf_counter_ns()
        try:
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout_s)
        except subprocess.TimeoutExpired:
            times.append(timeout_s * 1_000_000)
            print("T", end="", flush=True)
            continue
        end = time.perf_counter_ns()
        elapsed_us = (end - start) // 1000
        times.append(elapsed_us)
        if r.returncode != 0 and i == 0:
            stderr_short = r.stderr[:100].replace('\n', ' ') if r.stderr else ''
            stdout_short = r.stdout[:100].replace('\n', ' ') if r.stdout else ''
            print(f" [exit {r.returncode}: {stderr_short or stdout_short}]", end="", flush=True)
            return []  # Failed
        # Check for FAIL in output
        if i == 0 and "FAIL" in (r.stdout or ""):
            print(f" [FAIL: {r.stdout.strip()[:60]}]", end="", flush=True)
            return []
        print(".", end="", flush=True)
    return times

def med(times):
    if not times:
        return 0
    return int(statistics.median(sorted(times)))

def geo_mean(values):
    if not values:
        return 0
    product = 1.0
    for v in values:
        product *= v
    return product ** (1.0 / len(values))

# ============================================================
print(f"\033[1mMIR Direct vs C2MIR Transpiler Benchmark Comparison\033[0m")
print("=" * 80)
print(f"  Runs per benchmark: {NUM_RUNS}")
print(f"  Timing: wall-clock (includes startup + JIT compile + execute)")
print()

# ============================================================
# R7RS BENCHMARKS
# ============================================================
print(f"\033[1m{'='*80}\033[0m")
print(f"\033[1m{'R7RS BENCHMARKS':^80s}\033[0m")
print(f"\033[1m{'='*80}\033[0m")
print()

# Phase 1: R7RS untyped - C2MIR
print(f"\033[1mR7RS Untyped - C2MIR ({NUM_RUNS} runs)\033[0m")
print("-" * 80)
r7rs_c_untyped = {}
for name, cat in R7RS_BENCHMARKS:
    script = f"test/benchmark/r7rs/{name}.ls"
    print(f"  {name:12s}", end="", flush=True)
    times = run_benchmark(f"{LAMBDA_EXE} run {script}", NUM_RUNS)
    m = med(times)
    r7rs_c_untyped[name] = m
    print(f"  median={format_time(m):>10s}")
print()

# Phase 2: R7RS untyped - MIR Direct
print(f"\033[1mR7RS Untyped - MIR Direct ({NUM_RUNS} runs)\033[0m")
print("-" * 80)
r7rs_mir_untyped = {}
for name, cat in R7RS_BENCHMARKS:
    script = f"test/benchmark/r7rs/{name}.ls"
    print(f"  {name:12s}", end="", flush=True)
    times = run_benchmark(f"{LAMBDA_EXE} run --mir {script}", NUM_RUNS)
    m = med(times)
    r7rs_mir_untyped[name] = m
    print(f"  median={format_time(m):>10s}")
print()

# Phase 3: R7RS typed - C2MIR
print(f"\033[1mR7RS Typed - C2MIR ({NUM_RUNS} runs)\033[0m")
print("-" * 80)
r7rs_c_typed = {}
for name, cat in R7RS_BENCHMARKS:
    script = f"test/benchmark/r7rs/{name}2.ls"
    print(f"  {name:12s}", end="", flush=True)
    times = run_benchmark(f"{LAMBDA_EXE} run {script}", NUM_RUNS)
    m = med(times)
    r7rs_c_typed[name] = m
    print(f"  median={format_time(m):>10s}")
print()

# Phase 4: R7RS typed - MIR Direct
print(f"\033[1mR7RS Typed - MIR Direct ({NUM_RUNS} runs)\033[0m")
print("-" * 80)
r7rs_mir_typed = {}
for name, cat in R7RS_BENCHMARKS:
    script = f"test/benchmark/r7rs/{name}2.ls"
    print(f"  {name:12s}", end="", flush=True)
    times = run_benchmark(f"{LAMBDA_EXE} run --mir {script}", NUM_RUNS)
    m = med(times)
    r7rs_mir_typed[name] = m
    print(f"  median={format_time(m):>10s}")
print()

# R7RS Summary
print(f"\033[1m{'R7RS RESULTS':^80s}\033[0m")
print()
print(f"\033[36mUntyped: C2MIR vs MIR Direct\033[0m")
print(f"{'Benchmark':12s} {'Category':10s} {'C2MIR':>12s} {'MIR Direct':>12s} {'MIR/C':>8s}")
print(f"{'-'*12} {'-'*10} {'-'*12} {'-'*12} {'-'*8}")

r7rs_untyped_ratios = []
for name, cat in R7RS_BENCHMARKS:
    c = r7rs_c_untyped.get(name, 0)
    m = r7rs_mir_untyped.get(name, 0)
    if c > 0 and m > 0:
        ratio = m / c
        rs = f"{ratio:.2f}x"
        r7rs_untyped_ratios.append(ratio)
    else:
        rs = "N/A"
    print(f"{name:12s} {cat:10s} {format_time(c):>12s} {format_time(m):>12s} {rs:>8s}")

if r7rs_untyped_ratios:
    gm = geo_mean(r7rs_untyped_ratios)
    print(f"\n  Geometric mean (MIR/C): {gm:.2f}x  {'(MIR faster)' if gm < 1 else '(C faster)'}")
print()

print(f"\033[36mTyped: C2MIR vs MIR Direct\033[0m")
print(f"{'Benchmark':12s} {'Category':10s} {'C2MIR':>12s} {'MIR Direct':>12s} {'MIR/C':>8s}")
print(f"{'-'*12} {'-'*10} {'-'*12} {'-'*12} {'-'*8}")

r7rs_typed_ratios = []
for name, cat in R7RS_BENCHMARKS:
    c = r7rs_c_typed.get(name, 0)
    m = r7rs_mir_typed.get(name, 0)
    if c > 0 and m > 0:
        ratio = m / c
        rs = f"{ratio:.2f}x"
        r7rs_typed_ratios.append(ratio)
    else:
        rs = "N/A"
    print(f"{name:12s} {cat:10s} {format_time(c):>12s} {format_time(m):>12s} {rs:>8s}")

if r7rs_typed_ratios:
    gm = geo_mean(r7rs_typed_ratios)
    print(f"\n  Geometric mean (MIR/C): {gm:.2f}x  {'(MIR faster)' if gm < 1 else '(C faster)'}")
print()

# ============================================================
# AWFY BENCHMARKS
# ============================================================
print(f"\033[1m{'='*80}\033[0m")
print(f"\033[1m{'AWFY BENCHMARKS':^80s}\033[0m")
print(f"\033[1m{'='*80}\033[0m")
print()

# Phase 5: AWFY - C2MIR
print(f"\033[1mAWFY - C2MIR ({NUM_RUNS} runs)\033[0m")
print("-" * 80)
awfy_c = {}
for name, cat in AWFY_BENCHMARKS:
    script = f"test/benchmark/awfy/{name}.ls"
    print(f"  {name:14s}", end="", flush=True)
    times = run_benchmark(f"{LAMBDA_EXE} run {script}", NUM_RUNS)
    m = med(times)
    awfy_c[name] = m
    print(f"  median={format_time(m):>10s}")
print()

# Phase 6: AWFY - MIR Direct
print(f"\033[1mAWFY - MIR Direct ({NUM_RUNS} runs)\033[0m")
print("-" * 80)
awfy_mir = {}
for name, cat in AWFY_BENCHMARKS:
    script = f"test/benchmark/awfy/{name}.ls"
    print(f"  {name:14s}", end="", flush=True)
    times = run_benchmark(f"{LAMBDA_EXE} run --mir {script}", NUM_RUNS)
    m = med(times)
    awfy_mir[name] = m
    print(f"  median={format_time(m):>10s}")
print()

# AWFY Summary
print(f"\033[1m{'AWFY RESULTS':^80s}\033[0m")
print()
print(f"{'Benchmark':14s} {'Category':8s} {'C2MIR':>12s} {'MIR Direct':>12s} {'MIR/C':>8s}")
print(f"{'-'*14} {'-'*8} {'-'*12} {'-'*12} {'-'*8}")

awfy_ratios = []
for name, cat in AWFY_BENCHMARKS:
    c = awfy_c.get(name, 0)
    m = awfy_mir.get(name, 0)
    if c > 0 and m > 0:
        ratio = m / c
        rs = f"{ratio:.2f}x"
        awfy_ratios.append(ratio)
    else:
        rs = "N/A"
    print(f"{name:14s} {cat:8s} {format_time(c):>12s} {format_time(m):>12s} {rs:>8s}")

if awfy_ratios:
    gm = geo_mean(awfy_ratios)
    print(f"\n  Geometric mean (MIR/C): {gm:.2f}x  {'(MIR faster)' if gm < 1 else '(C faster)'}")
print()

# ============================================================
# OVERALL SUMMARY
# ============================================================
print(f"\033[1m{'='*80}\033[0m")
print(f"\033[1m{'OVERALL SUMMARY':^80s}\033[0m")
print(f"\033[1m{'='*80}\033[0m")
print()

all_ratios = r7rs_untyped_ratios + r7rs_typed_ratios + awfy_ratios
if r7rs_untyped_ratios:
    print(f"  R7RS untyped  MIR/C geomean: {geo_mean(r7rs_untyped_ratios):.2f}x")
if r7rs_typed_ratios:
    print(f"  R7RS typed    MIR/C geomean: {geo_mean(r7rs_typed_ratios):.2f}x")
if awfy_ratios:
    print(f"  AWFY          MIR/C geomean: {geo_mean(awfy_ratios):.2f}x")
if all_ratios:
    print(f"  Overall       MIR/C geomean: {geo_mean(all_ratios):.2f}x")
    print()
    print(f"  Ratio < 1.0 = MIR Direct is faster")
    print(f"  Ratio > 1.0 = C2MIR is faster")

# Save CSV
csv_path = "temp/mir_vs_c_bench.csv"
os.makedirs("temp", exist_ok=True)
with open(csv_path, "w") as f:
    f.write(f"# MIR vs C Benchmark Results {time.strftime('%Y-%m-%d %H:%M:%S')}, runs={NUM_RUNS}\n")
    f.write("suite,benchmark,category,c2mir_us,mir_direct_us,ratio\n")
    for name, cat in R7RS_BENCHMARKS:
        c = r7rs_c_untyped.get(name, 0)
        m = r7rs_mir_untyped.get(name, 0)
        ratio = m / c if c > 0 and m > 0 else 0
        f.write(f"r7rs_untyped,{name},{cat},{c},{m},{ratio:.4f}\n")
    for name, cat in R7RS_BENCHMARKS:
        c = r7rs_c_typed.get(name, 0)
        m = r7rs_mir_typed.get(name, 0)
        ratio = m / c if c > 0 and m > 0 else 0
        f.write(f"r7rs_typed,{name},{cat},{c},{m},{ratio:.4f}\n")
    for name, cat in AWFY_BENCHMARKS:
        c = awfy_c.get(name, 0)
        m = awfy_mir.get(name, 0)
        ratio = m / c if c > 0 and m > 0 else 0
        f.write(f"awfy,{name},{cat},{c},{m},{ratio:.4f}\n")
print(f"\nData saved to {csv_path}")
