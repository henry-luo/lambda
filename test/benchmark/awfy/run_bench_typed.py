#!/usr/bin/env python3
"""Compare Lambda untyped vs typed AWFY benchmarks.

Usage: python3 test/benchmark/awfy/run_bench_typed.py [num_runs]
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

JS_DIR = os.path.join("are-we-fast-yet", "benchmarks", "JavaScript")

BENCHMARKS = [
    ("Sieve",       "sieve",       1, "micro"),
    ("Permute",     "permute",     1, "micro"),
    ("Queens",      "queens",      1, "micro"),
    ("Towers",      "towers",      1, "micro"),
    ("Bounce",      "bounce",      1, "micro"),
    ("List",        "list",        1, "micro"),
    ("Storage",     "storage",     1, "micro"),
    ("Mandelbrot",  "mandelbrot",500, "micro"),
    ("NBody",       "nbody",    1000, "micro"),
    ("Richards",    "richards",    1, "macro"),
    ("Json",        "json",        1, "macro"),
    ("DeltaBlue",   "deltablue", 100, "macro"),
    ("Havlak",      "havlak",      1, "macro"),
    ("CD",          "cd",        100, "macro"),
]

SLOW_BENCHMARKS = {"CD", "Havlak", "Mandelbrot", "Richards"}

def get_runs(name):
    """Fewer runs for very slow benchmarks."""
    if name in SLOW_BENCHMARKS:
        return max(1, NUM_RUNS // 3)
    return NUM_RUNS

def format_time(us):
    if us <= 0:
        return "N/A"
    if us < 1000:
        return f"{us} us"
    elif us < 1000000:
        return f"{us/1000:.1f} ms"
    else:
        return f"{us/1000000:.3f} s"

def run_timed(cmd, num_runs):
    """Run cmd N times, return list of times in microseconds."""
    times = []
    for i in range(num_runs):
        start = time.perf_counter_ns()
        try:
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=600)
        except subprocess.TimeoutExpired:
            times.append(600_000_000)
            print("T", end="", flush=True)
            continue
        end = time.perf_counter_ns()
        elapsed_us = (end - start) // 1000
        times.append(elapsed_us)
        if r.returncode != 0 and i == 0:
            stdout_short = r.stdout[:80].replace('\n', ' ') if r.stdout else ''
            print(f" [{stdout_short}]", end="")
        print(".", end="", flush=True)
    return times

def med(times):
    return int(statistics.median(sorted(times)))

print(f"\033[1mAWFY Benchmark: Untyped vs Typed Lambda Script\033[0m")
print("=" * 78)
print(f"Runs per benchmark: {NUM_RUNS}")
print()

# Phase 1: Original (untyped)
print(f"\033[1mPhase 1: Lambda Untyped (original)\033[0m")
print("-" * 60)
orig = {}
for name, base, inner, cat in BENCHMARKS:
    script = f"test/benchmark/awfy/{base}.ls"
    runs = get_runs(name)
    print(f"  {name:14s} ({runs}x)", end="", flush=True)
    times = run_timed(f"{LAMBDA_EXE} run {script}", runs)
    m = med(times)
    orig[name] = m
    print(f"  median={format_time(m):>10s}")
print()

# Phase 2: Typed
print(f"\033[1mPhase 2: Lambda Typed (annotated)\033[0m")
print("-" * 60)
typed = {}
for name, base, inner, cat in BENCHMARKS:
    script = f"test/benchmark/awfy/{base}2.ls"
    runs = get_runs(name)
    print(f"  {name:14s} ({runs}x)", end="", flush=True)
    times = run_timed(f"{LAMBDA_EXE} run {script}", runs)
    m = med(times)
    typed[name] = m
    print(f"  median={format_time(m):>10s}")
print()

# Phase 3: Node.js (for reference)
print(f"\033[1mPhase 3: Node.js (reference)\033[0m")
print("-" * 60)
nodejs = {}
for name, base, inner, cat in BENCHMARKS:
    runs = get_runs(name)
    print(f"  {name:14s} ({runs}x)", end="", flush=True)
    times = run_timed(f"cd {JS_DIR} && node harness.js {name} 1 {inner}", runs)
    m = med(times)
    nodejs[name] = m
    print(f"  median={format_time(m):>10s}")
print()

# Summary
SEP = "=" * 90
print(f"\033[1m{SEP}\033[0m")
print(f"\033[1m{'RESULTS: Untyped vs Typed Lambda (with Node.js reference)':^90s}\033[0m")
print(f"\033[1m{SEP}\033[0m")
print()

print(f"{'Benchmark':14s} {'Untyped':>12s} {'Typed':>12s} {'Speedup':>8s} {'Node.js':>12s} {'T/N Ratio':>10s}")
print(f"{'-'*14} {'-'*12} {'-'*12} {'-'*8} {'-'*12} {'-'*10}")

speedups = []
typed_ratios = []
for name, base, inner, cat in BENCHMARKS:
    o = orig.get(name, 0)
    t = typed.get(name, 0)
    n = nodejs.get(name, 0)

    if o > 0 and t > 0:
        sp = o / t
        sps = f"{sp:.2f}x"
        speedups.append(sp)
    else:
        sps = "N/A"

    if t > 0 and n > 0:
        tr = t / n
        trs = f"{tr:.1f}x"
        typed_ratios.append(tr)
    else:
        trs = "N/A"

    print(f"{name:14s} {format_time(o):>12s} {format_time(t):>12s} {sps:>8s} {format_time(n):>12s} {trs:>10s}")

if speedups:
    gm_sp = 1
    for s in speedups:
        gm_sp *= s
    gm_sp = gm_sp ** (1.0 / len(speedups))
    print()
    print(f"  Typing speedup (geo mean): {gm_sp:.2f}x")

if typed_ratios:
    gm_tr = 1
    for r in typed_ratios:
        gm_tr *= r
    gm_tr = gm_tr ** (1.0 / len(typed_ratios))
    print(f"  Typed vs Node.js (geo mean): {gm_tr:.2f}x")

print()
print("Legend:")
print("  Speedup = Untyped / Typed (> 1.0 = typed is faster)")
print("  T/N Ratio = Typed / Node.js (> 1.0 = Node.js faster)")
print()

# Save CSV
csv = "temp/bench_typed_results.csv"
with open(csv, "w") as f:
    f.write(f"# Typed vs Untyped AWFY {time.strftime('%Y-%m-%d %H:%M:%S')}, runs={NUM_RUNS}\n")
    f.write("benchmark,category,untyped_us,typed_us,nodejs_us\n")
    for name, base, inner, cat in BENCHMARKS:
        o = orig.get(name, 0)
        t = typed.get(name, 0)
        n = nodejs.get(name, 0)
        f.write(f"{name},{cat},{o},{t},{n}\n")
print(f"Data saved to {csv}")
