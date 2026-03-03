#!/usr/bin/env python3
"""Kostya Benchmark Runner — Lambda Script

Runs benchmarks adapted from github.com/kostya/benchmarks.
These are common cross-language benchmarks used for comparing
performance across many programming languages.

Usage: python3 test/benchmark/kostya/run_bench.py [num_runs]
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
LS_DIR = "test/benchmark/kostya"

# Benchmark configs: (name, category, description)
BENCHMARKS = [
    ("brainfuck",   "interp",    "Brainfuck interpreter (10000 runs)"),
    ("matmul",      "numeric",   "Matrix multiplication 200x200"),
    ("primes",      "array",     "Sieve of Eratosthenes to 1M"),
    ("base64",      "string",    "Base64 encode 10KB (100 iters)"),
    ("levenshtein", "dp",        "Levenshtein edit distance (500 chars)"),
    ("json_gen",    "string",    "JSON generation (1000 objects, 10 iters)"),
    ("collatz",     "numeric",   "Longest Collatz sequence under 1M"),
]

# Lambda startup overhead (measured separately)
LAMBDA_STARTUP_MS = 10

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


def run_external(cmd, num_runs):
    """Run cmd N times externally, return (wall_times, exec_times) in ms."""
    wall_times = []
    exec_times = []
    for i in range(num_runs):
        start = time.perf_counter_ns()
        try:
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=600)
        except subprocess.TimeoutExpired:
            wall_times.append(600_000)
            exec_times.append(600_000)
            print("T", end="", flush=True)
            continue
        end = time.perf_counter_ns()
        elapsed_ms = (end - start) / 1_000_000
        wall_ms = max(0.01, elapsed_ms - LAMBDA_STARTUP_MS)
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
print(f"\033[1mKostya Benchmark Suite — Lambda Script\033[0m")
print("=" * 70)
print(f"  Runs per benchmark : {NUM_RUNS}")
print(f"  Lambda             : Lambda Script v1.0 (C2MIR JIT)")
print(f"  Timing method      : Wall-clock + in-script clock()")
print()

# ============================================================
# Run benchmarks
# ============================================================
print(f"\033[1mRunning benchmarks ({NUM_RUNS} runs each)\033[0m")
print("-" * 70)

results_wall = {}
results_exec = {}
for name, cat, desc in BENCHMARKS:
    script = f"{LS_DIR}/{name}.ls"
    if not os.path.exists(script):
        print(f"  {name:14s} SKIPPED (file not found)")
        continue
    print(f"  {name:14s}", end="", flush=True)
    wall_times, exec_times = run_external(f"{LAMBDA_EXE} run {script}", NUM_RUNS)
    mw = med(wall_times)
    me = med(exec_times)
    results_wall[name] = mw
    results_exec[name] = me
    jit_ms = max(0, mw - me)
    print(f"  total={format_time_ms(mw):>10s}  exec={format_time_ms(me):>10s}  jit={format_time_ms(jit_ms):>8s}  ({desc})")

print()

# ============================================================
# Results Summary
# ============================================================
SEP = "=" * 70
print(f"\033[1m{SEP}\033[0m")
print(f"\033[1m{'RESULTS SUMMARY':^70s}\033[0m")
print(f"\033[1m{SEP}\033[0m")
print()

hdr = f"{'Benchmark':14s} {'Category':8s} {'Total':>10s} {'Exec':>10s} {'JIT':>10s}"
print(hdr)
print(f"{'-'*14} {'-'*8} {'-'*10} {'-'*10} {'-'*10}")

for name, cat, desc in BENCHMARKS:
    if name in results_wall:
        tw = results_wall[name]
        te = results_exec[name]
        tj = max(0, tw - te)
        print(f"{name:14s} {cat:8s} {format_time_ms(tw):>10s} {format_time_ms(te):>10s} {format_time_ms(tj):>10s}")

if results_wall:
    wall_list = [v for v in results_wall.values() if v > 0]
    exec_list = [v for v in results_exec.values() if v > 0]
    if wall_list:
        print()
        print(f"  Total (wall)     : {format_time_ms(sum(wall_list))}")
        print(f"  Total (exec)     : {format_time_ms(sum(exec_list))}")
        print(f"  Geo mean (wall)  : {format_time_ms(geo_mean(wall_list))}")
        print(f"  Geo mean (exec)  : {format_time_ms(geo_mean(exec_list))}")

print()

# ============================================================
# Save CSV
# ============================================================
csv_path = f"{LS_DIR}/bench_results.csv"
with open(csv_path, "w") as f:
    f.write(f"# Kostya Benchmark Results {time.strftime('%Y-%m-%d %H:%M:%S')}, runs={NUM_RUNS}\n")
    f.write("benchmark,category,wall_ms,exec_ms,jit_ms\n")
    for name, cat, desc in BENCHMARKS:
        tw = results_wall.get(name, 0)
        te = results_exec.get(name, 0)
        tj = max(0, tw - te)
        f.write(f"{name},{cat},{tw:.3f},{te:.3f},{tj:.3f}\n")
print(f"Raw data saved to {csv_path}")
