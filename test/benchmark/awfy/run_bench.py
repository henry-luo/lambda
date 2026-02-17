#!/usr/bin/env python3
"""AWFY Benchmark Runner - Lambda Script vs Node.js (Fair Comparison)

Runs all 14 AWFY benchmarks with matched workload parameters.
Uses external wall-clock timing and Node.js internal timing.

Usage: python3 test/benchmark/awfy/run_bench.py [num_runs]
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

JS_DIR = os.path.join("are-we-fast-yet", "benchmarks", "JavaScript")
LAMBDA_EXE = "./lambda.exe"

# Benchmark configs: name, JS innerIterations (to match Lambda workload), category
# JS innerIterations meaning varies per benchmark:
#   Default (Sieve, Permute, etc.): runs benchmark() N times
#   Mandelbrot: grid size (500 = 500x500)
#   NBody: number of advance() steps (1000)
#   DeltaBlue: n parameter for chain/projection test (100)
#   CD: number of aircraft (100)
#   Havlak: numDummyLoops (1, but JS uses findLoopIterations=50 vs Lambda's 1)
BENCHMARKS = [
    ("Sieve",         1,      "micro"),
    ("Permute",       1,      "micro"),
    ("Queens",        1,      "micro"),
    ("Towers",        1,      "micro"),
    ("Bounce",        1,      "micro"),
    ("List",          1,      "micro"),
    ("Storage",       1,      "micro"),
    ("Mandelbrot",  500,      "micro"),
    ("NBody",      1000,      "micro"),
    ("Richards",      1,      "macro"),
    ("Json",          1,      "macro"),
    ("DeltaBlue",   100,      "macro"),
    ("Havlak",        1,      "macro"),
    ("CD",          100,      "macro"),
]

def format_time(us):
    if us <= 0:
        return "N/A"
    if us < 1000:
        return f"{us} us"
    elif us < 1000000:
        return f"{us/1000:.1f} ms"
    else:
        return f"{us/1000000:.3f} s"

def run_external(cmd, num_runs, label=""):
    """Run cmd N times externally, return list of times in microseconds."""
    times = []
    for i in range(num_runs):
        start = time.perf_counter_ns()
        try:
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=300)
        except subprocess.TimeoutExpired:
            times.append(300_000_000)  # 300s timeout marker
            print("T", end="", flush=True)
            continue
        end = time.perf_counter_ns()
        elapsed_us = (end - start) // 1000
        times.append(elapsed_us)
        if r.returncode != 0 and i == 0:
            stderr_short = r.stderr[:120].replace('\n', ' ') if r.stderr else ''
            print(f" [exit {r.returncode}]", end="")
        print(".", end="", flush=True)
    return times

def run_nodejs_internal(bench, inner_iter, num_outer):
    """Run Node.js with internal timing. Returns avg us per iteration."""
    cmd = f"cd {JS_DIR} && node harness.js {bench} {num_outer} {inner_iter}"
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return None
    if r.returncode != 0:
        return None
    m = re.search(r'average:\s*(\d+)us', r.stdout)
    return int(m.group(1)) if m else None

def med(times):
    return int(statistics.median(sorted(times)))

# ============================================================
node_ver = subprocess.run("node --version", shell=True, capture_output=True, text=True).stdout.strip()
print(f"\033[1mAWFY Benchmark Suite — Lambda Script vs Node.js\033[0m")
print("=" * 70)
print(f"Runs per benchmark: {NUM_RUNS}")
print(f"Node.js: {node_ver}")
print()

# ============================================================
# Phase 1: Node.js Internal Timing
# ============================================================
print(f"\033[1mPhase 1: Node.js Internal Timing (matched workload)\033[0m")
print("-" * 70)

js_int = {}
for name, inner, cat in BENCHMARKS:
    avg = run_nodejs_internal(name, inner, max(NUM_RUNS, 3))
    js_int[name] = avg if avg else 0
    s = format_time(avg) if avg else "FAILED"
    print(f"  {name:14s} (inner={inner:<5d})  {s:>14s}")

print()

# ============================================================
# Phase 2: Node.js External
# ============================================================
print(f"\033[1mPhase 2: Node.js External ({NUM_RUNS} runs)\033[0m")
print("-" * 70)

js_ext = {}
for name, inner, cat in BENCHMARKS:
    print(f"  {name:14s}", end="", flush=True)
    times = run_external(f"cd {JS_DIR} && node harness.js {name} 1 {inner}", NUM_RUNS, f"Node {name}")
    m = med(times)
    js_ext[name] = m
    print(f"  median={format_time(m):>10s}")

print()

# ============================================================
# Phase 3: Lambda External
# ============================================================
print(f"\033[1mPhase 3: Lambda External ({NUM_RUNS} runs)\033[0m")
print("-" * 70)

lm_ext = {}
for name, inner, cat in BENCHMARKS:
    script = f"test/benchmark/awfy/{name.lower()}.ls"
    print(f"  {name:14s}", end="", flush=True)
    times = run_external(f"{LAMBDA_EXE} run {script}", NUM_RUNS, f"Lambda {name}")
    m = med(times)
    lm_ext[name] = m
    print(f"  median={format_time(m):>10s}")

print()

# ============================================================
# Summary
# ============================================================
SEP = "=" * 78
print(f"\033[1m{SEP}\033[0m")
print(f"\033[1m{'RESULTS SUMMARY':^78s}\033[0m")
print(f"\033[1m{SEP}\033[0m")
print()

print(f"\033[36mTable 1: Wall-Clock (median) — includes startup + JIT + execution\033[0m")
hdr = f"{'Benchmark':14s} {'Lambda':>12s} {'Node.js':>12s} {'Ratio':>8s}  {''}:>0s"
print(f"{'Benchmark':14s} {'Lambda':>12s} {'Node.js':>12s} {'Ratio':>8s}")
print(f"{'-'*14} {'-'*12} {'-'*12} {'-'*8}")

ratios = []
lw = jw = 0
for name, inner, cat in BENCHMARKS:
    lm = lm_ext.get(name, 0)
    js = js_ext.get(name, 0)
    if lm > 0 and js > 0:
        r = lm / js
        rs = f"{r:.2f}x"
        ratios.append(r)
        if r <= 1.0:
            lw += 1
        else:
            jw += 1
    else:
        rs = "N/A"
    print(f"{name:14s} {format_time(lm):>12s} {format_time(js):>12s} {rs:>8s}")

if ratios:
    gm = 1
    for r in ratios:
        gm *= r
    gm = gm ** (1.0 / len(ratios))
    print()
    print(f"  Geometric mean: {gm:.2f}x  |  Lambda wins: {lw}  |  Node.js wins: {jw}")

print()

print(f"\033[36mTable 2: Lambda Total vs Node.js Internal (benchmark-only)\033[0m")
print(f"{'Benchmark':14s} {'Lambda':>12s} {'Node(int)':>12s} {'Ratio':>10s}")
print(f"{'-'*14} {'-'*12} {'-'*12} {'-'*10}")

for name, inner, cat in BENCHMARKS:
    lm = lm_ext.get(name, 0)
    js = js_int.get(name, 0)
    if lm > 0 and js > 0:
        r = lm / js
        rs = f"{r:.1f}x"
    else:
        rs = "N/A"
    print(f"{name:14s} {format_time(lm):>12s} {format_time(js):>12s} {rs:>10s}")

print()
print("Legend:")
print("  Ratio = Lambda / Node.js  (< 1.0 = Lambda faster, > 1.0 = Node.js faster)")
print("  Lambda: total wall-clock (startup + parse + JIT compile + execute)")
print("  Node.js ext: total wall-clock (startup ~30ms + V8 JIT + execute)")
print("  Node.js int: pure benchmark execution (measured inside process)")
print()

# Save
csv = "temp/bench_results.csv"
with open(csv, "w") as f:
    f.write(f"# AWFY Results {time.strftime('%Y-%m-%d %H:%M:%S')}, runs={NUM_RUNS}\n")
    f.write("benchmark,category,js_inner_iter,lambda_med_us,nodejs_ext_med_us,nodejs_int_us\n")
    for name, inner, cat in BENCHMARKS:
        lm = lm_ext.get(name, 0)
        je = js_ext.get(name, 0)
        ji = js_int.get(name, 0)
        f.write(f"{name},{cat},{inner},{lm},{je},{ji}\n")
print(f"Data saved to {csv}")
