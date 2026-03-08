#!/usr/bin/env python3
"""AWFY Benchmark Runner - Lambda Script vs Node.js vs Python

Runs all 14 AWFY benchmarks with matched workload parameters.
Uses external wall-clock timing and Node.js/Python internal timing.

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
PY_DIR = "test/benchmark/awfy/python"
LAMBDA_EXE = "./lambda.exe"
PYTHON_EXE = "python3"

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

def run_python_internal(bench, inner_iter, num_outer):
    """Run Python AWFY harness with internal timing. Returns avg us per iteration."""
    cmd = f"cd {PY_DIR} && {PYTHON_EXE} harness.py {bench} {num_outer} {inner_iter}"
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=300)
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
py_ver = subprocess.run(f"{PYTHON_EXE} --version", shell=True, capture_output=True, text=True).stdout.strip()
print(f"\033[1mAWFY Benchmark Suite — Lambda Script vs Node.js vs Python\033[0m")
print("=" * 78)
print(f"Runs per benchmark: {NUM_RUNS}")
print(f"Node.js: {node_ver}")
print(f"Python:  {py_ver}")
print()

# ============================================================
# Phase 1: Node.js Internal Timing
# ============================================================
print(f"\033[1mPhase 1: Node.js Internal Timing (matched workload)\033[0m")
print("-" * 78)

js_int = {}
for name, inner, cat in BENCHMARKS:
    avg = run_nodejs_internal(name, inner, max(NUM_RUNS, 3))
    js_int[name] = avg if avg else 0
    s = format_time(avg) if avg else "FAILED"
    print(f"  {name:14s} (inner={inner:<5d})  {s:>14s}")

print()

# ============================================================
# Phase 1b: Python Internal Timing
# ============================================================
print(f"\033[1mPhase 1b: Python Internal Timing (matched workload)\033[0m")
print("-" * 78)

py_int = {}
for name, inner, cat in BENCHMARKS:
    avg = run_python_internal(name, inner, max(NUM_RUNS, 3))
    py_int[name] = avg if avg else 0
    s = format_time(avg) if avg else "FAILED"
    print(f"  {name:14s} (inner={inner:<5d})  {s:>14s}")

print()

# ============================================================
# Phase 2: Node.js External
# ============================================================
print(f"\033[1mPhase 2: Node.js External ({NUM_RUNS} runs)\033[0m")
print("-" * 78)

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
print("-" * 78)

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
# Phase 4: Python External
# ============================================================
print(f"\033[1mPhase 4: Python External ({NUM_RUNS} runs)\033[0m")
print("-" * 78)

py_ext = {}
for name, inner, cat in BENCHMARKS:
    print(f"  {name:14s}", end="", flush=True)
    times = run_external(f"cd {PY_DIR} && {PYTHON_EXE} harness.py {name} 1 {inner}", NUM_RUNS, f"Py {name}")
    m = med(times)
    py_ext[name] = m
    print(f"  median={format_time(m):>10s}")

print()

# ============================================================
# Summary
# ============================================================
SEP = "=" * 90
print(f"\033[1m{SEP}\033[0m")
print(f"\033[1m{'RESULTS SUMMARY':^90s}\033[0m")
print(f"\033[1m{SEP}\033[0m")
print()

print(f"\033[36mTable 1: Wall-Clock (median) — includes startup + JIT + execution\033[0m")
print(f"{'Benchmark':14s} {'Lambda':>12s} {'Node.js':>12s} {'Python':>12s} {'Lm/JS':>8s} {'Lm/Py':>8s}")
print(f"{'-'*14} {'-'*12} {'-'*12} {'-'*12} {'-'*8} {'-'*8}")

ratios_js = []
ratios_py = []
lw_js = jw_js = lw_py = pw_py = 0
for name, inner, cat in BENCHMARKS:
    lm = lm_ext.get(name, 0)
    js = js_ext.get(name, 0)
    py = py_ext.get(name, 0)
    if lm > 0 and js > 0:
        r = lm / js
        rs_js = f"{r:.2f}x"
        ratios_js.append(r)
        if r <= 1.0: lw_js += 1
        else: jw_js += 1
    else:
        rs_js = "N/A"
    if lm > 0 and py > 0:
        r2 = lm / py
        rs_py = f"{r2:.2f}x"
        ratios_py.append(r2)
        if r2 <= 1.0: lw_py += 1
        else: pw_py += 1
    else:
        rs_py = "N/A"
    print(f"{name:14s} {format_time(lm):>12s} {format_time(js):>12s} {format_time(py):>12s} {rs_js:>8s} {rs_py:>8s}")

if ratios_js:
    gm_js = 1
    for r in ratios_js: gm_js *= r
    gm_js = gm_js ** (1.0 / len(ratios_js))
    print(f"  Geo mean Lambda/Node.js: {gm_js:.2f}x  |  Lambda wins: {lw_js}  |  Node.js wins: {jw_js}")
if ratios_py:
    gm_py = 1
    for r in ratios_py: gm_py *= r
    gm_py = gm_py ** (1.0 / len(ratios_py))
    print(f"  Geo mean Lambda/Python:  {gm_py:.2f}x  |  Lambda wins: {lw_py}  |  Python wins: {pw_py}")

print()

print(f"\033[36mTable 2: Internal Timing (Lambda wall-clock vs JS/Python harness)\033[0m")
print(f"{'Benchmark':14s} {'Lambda':>12s} {'Node(int)':>12s} {'Py(int)':>12s} {'Lm/JS':>8s} {'Lm/Py':>8s}")
print(f"{'-'*14} {'-'*12} {'-'*12} {'-'*12} {'-'*8} {'-'*8}")

for name, inner, cat in BENCHMARKS:
    lm = lm_ext.get(name, 0)
    ji = js_int.get(name, 0)
    pi = py_int.get(name, 0)
    rs_js = f"{lm / ji:.1f}x" if lm > 0 and ji > 0 else "N/A"
    rs_py = f"{lm / pi:.1f}x" if lm > 0 and pi > 0 else "N/A"
    print(f"{name:14s} {format_time(lm):>12s} {format_time(ji):>12s} {format_time(pi):>12s} {rs_js:>8s} {rs_py:>8s}")

print()
print("Legend:")
print("  Ratio = Lambda / other  (< 1.0 = Lambda faster, > 1.0 = other faster)")
print("  Lambda: total wall-clock (startup + parse + JIT compile + execute)")
print("  Node.js ext: total wall-clock (startup ~30ms + V8 JIT + execute)")
print("  Node.js int: pure benchmark execution (measured inside process)")
print("  Python ext:  total wall-clock (startup ~20ms + execute)")
print("  Python int:  pure benchmark execution (measured inside process)")
print()

# Save
csv = "temp/bench_results.csv"
with open(csv, "w") as f:
    f.write(f"# AWFY Results {time.strftime('%Y-%m-%d %H:%M:%S')}, runs={NUM_RUNS}\n")
    f.write("benchmark,category,js_inner_iter,lambda_med_us,nodejs_ext_med_us,nodejs_int_us,python_ext_med_us,python_int_us\n")
    for name, inner, cat in BENCHMARKS:
        lm = lm_ext.get(name, 0)
        je = js_ext.get(name, 0)
        ji = js_int.get(name, 0)
        pe = py_ext.get(name, 0)
        pi = py_int.get(name, 0)
        f.write(f"{name},{cat},{inner},{lm},{je},{ji},{pe},{pi}\n")
print(f"Data saved to {csv}")
