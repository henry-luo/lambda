#!/usr/bin/env python3
"""R7RS Benchmark Runner — Lambda Script (untyped & typed) vs Racket vs Guile vs Python

Runs 10 R7RS-derived benchmarks across five configurations and compares
performance using internal timing (Racket/Guile/Python) and wall-clock (Lambda).

Usage: python3 test/benchmark/r7rs/run_bench.py [num_runs]
"""

import subprocess
import time
import sys
import os
import re
import statistics
import math

NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 5

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.join(SCRIPT_DIR, "..", "..", "..")
os.chdir(PROJECT_ROOT)

LAMBDA_EXE = "./lambda.exe"
RACKET_EXE = "racket"
GUILE_EXE = "guile"
PYTHON_EXE = "python3"
PY_DIR = "test/benchmark/r7rs/python"

TIMING_RE = re.compile(r"^__TIMING__:([\d.]+(?:e[+-]?\d+)?)")

# Lambda startup overhead (measured separately) — subtracted for fair comparison
LAMBDA_STARTUP_MS = 10  # ~10ms on M-series Mac

# Benchmark list: (name, category, description)
BENCHMARKS = [
    ("fib",     "recursive",    "Naive recursive Fibonacci, fib(27)"),
    ("fibfp",   "recursive",    "Floating-point Fibonacci, fibfp(27.0)"),
    ("tak",     "recursive",    "Takeuchi function, tak(18,12,6)"),
    ("cpstak",  "closure",      "CPS Takeuchi / tak x2, tak(18,12,6)"),
    ("sum",     "iterative",    "Integer sum 0..10000, 100 iterations"),
    ("sumfp",   "iterative",    "Float sum 0.0..100000.0"),
    ("nqueens", "backtrack",    "N-Queens, n=8"),
    ("fft",     "numeric",      "FFT 4096-element vector"),
    ("mbrot",   "numeric",      "Mandelbrot 75x75 grid"),
    ("ack",     "recursive",    "Ackermann ack(3,8)"),
]


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


def extract_time_ms(stdout):
    """Extract TIME_MS= value from stdout."""
    m = re.search(r'TIME_MS=([0-9.]+)', stdout)
    return float(m.group(1)) if m else None


def run_lambda(cmd, num_runs, timeout_s=300):
    """Run Lambda cmd N times, return list of wall-clock times in ms."""
    times = []
    for i in range(num_runs):
        start = time.perf_counter_ns()
        try:
            r = subprocess.run(
                cmd, shell=True, capture_output=True, text=True, timeout=timeout_s
            )
        except subprocess.TimeoutExpired:
            times.append(timeout_s * 1000)
            print("T", end="", flush=True)
            continue
        end = time.perf_counter_ns()
        elapsed_ms = (end - start) / 1_000_000
        # Subtract startup overhead for pure computation time
        compute_ms = max(0.01, elapsed_ms - LAMBDA_STARTUP_MS)
        times.append(compute_ms)

        if i == 0:
            stdout = r.stdout.strip()
            if "FAIL" in stdout:
                print(f" [FAIL: {stdout[:60]}]", end="", flush=True)
            elif r.returncode != 0:
                stderr_short = (r.stderr[:80].replace("\n", " ") if r.stderr else "")
                print(f" [exit {r.returncode}: {stderr_short}]", end="", flush=True)
        print(".", end="", flush=True)
    return times


def run_with_internal_timing(cmd, num_runs, timeout_s=300):
    """Run cmd N times, extract internal TIME_MS from stdout."""
    times = []
    for i in range(num_runs):
        try:
            r = subprocess.run(
                cmd, shell=True, capture_output=True, text=True, timeout=timeout_s
            )
        except subprocess.TimeoutExpired:
            times.append(timeout_s * 1000)
            print("T", end="", flush=True)
            continue

        internal_ms = extract_time_ms(r.stdout)
        if internal_ms is not None:
            times.append(internal_ms)
        else:
            times.append(0)
            if i == 0:
                print(" [no TIME_MS]", end="", flush=True)

        if i == 0:
            if "FAIL" in r.stdout:
                print(f" [FAIL]", end="", flush=True)
            elif r.returncode != 0:
                print(f" [exit {r.returncode}]", end="", flush=True)
        print(".", end="", flush=True)
    return times


def run_python_timing(cmd, num_runs, timeout_s=300):
    """Run Python benchmark, extract __TIMING__:xxx.xxx from stdout."""
    times = []
    for i in range(num_runs):
        try:
            r = subprocess.run(
                cmd, shell=True, capture_output=True, text=True, timeout=timeout_s
            )
        except subprocess.TimeoutExpired:
            times.append(timeout_s * 1000)
            print("T", end="", flush=True)
            continue

        internal_ms = None
        for line in r.stdout.strip().split("\n"):
            m = TIMING_RE.match(line.strip())
            if m:
                internal_ms = float(m.group(1))
                break

        if internal_ms is not None:
            times.append(internal_ms)
        else:
            times.append(0)
            if i == 0:
                print(" [no __TIMING__]", end="", flush=True)

        if i == 0:
            if "FAIL" in r.stdout:
                print(f" [FAIL]", end="", flush=True)
            elif r.returncode != 0:
                print(f" [exit {r.returncode}]", end="", flush=True)
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
racket_ver = subprocess.run(
    f"{RACKET_EXE} --version", shell=True, capture_output=True, text=True
).stdout.strip()
guile_ver = subprocess.run(
    f"{GUILE_EXE} --version | head -1", shell=True, capture_output=True, text=True
).stdout.strip()
py_ver = subprocess.run(
    f"{PYTHON_EXE} --version", shell=True, capture_output=True, text=True
).stdout.strip()

print(f"\033[1mR7RS Benchmark Suite — Lambda (untyped & typed) vs Racket vs Guile vs Python\033[0m")
print("=" * 82)
print(f"  Runs per benchmark : {NUM_RUNS}")
print(f"  Lambda             : Lambda Script v1.0 (MIR JIT)")
print(f"  Racket             : {racket_ver}")
print(f"  Guile              : {guile_ver}")
print(f"  Python             : {py_ver}")
print(f"  Timing method      : Internal (Racket/Guile/Python) / Wall-clock minus startup (Lambda)")
print()

# ============================================================
# Phase 1: Lambda Untyped (wall-clock minus startup)
# ============================================================
print(f"\033[1mPhase 1: Lambda Script — untyped ({NUM_RUNS} runs each)\033[0m")
print("-" * 82)

lambda_times = {}
for name, cat, desc in BENCHMARKS:
    script = f"test/benchmark/r7rs/{name}.ls"
    print(f"  {name:10s}", end="", flush=True)
    times = run_lambda(f"{LAMBDA_EXE} run {script}", NUM_RUNS)
    m = med(times)
    lambda_times[name] = m
    print(f"  median = {format_time_ms(m):>10s}")

print()

# ============================================================
# Phase 2: Lambda Typed (wall-clock minus startup)
# ============================================================
print(f"\033[1mPhase 2: Lambda Script — typed ({NUM_RUNS} runs each)\033[0m")
print("-" * 82)

lambda_typed_times = {}
for name, cat, desc in BENCHMARKS:
    script = f"test/benchmark/r7rs/{name}2.ls"
    print(f"  {name:10s}", end="", flush=True)
    times = run_lambda(f"{LAMBDA_EXE} run {script}", NUM_RUNS)
    m = med(times)
    lambda_typed_times[name] = m
    print(f"  median = {format_time_ms(m):>10s}")

print()

# ============================================================
# Phase 3: Racket (internal timing)
# ============================================================
print(f"\033[1mPhase 3: Racket — internal timing ({NUM_RUNS} runs each)\033[0m")
print("-" * 82)

racket_times = {}
for name, cat, desc in BENCHMARKS:
    script = f"test/benchmark/r7rs/{name}.rkt"
    print(f"  {name:10s}", end="", flush=True)
    times = run_with_internal_timing(f"{RACKET_EXE} {script}", NUM_RUNS)
    m = med(times)
    racket_times[name] = m
    print(f"  median = {format_time_ms(m):>10s}")

print()

# ============================================================
# Phase 4: Guile (internal timing)
# ============================================================
print(f"\033[1mPhase 4: Guile — internal timing ({NUM_RUNS} runs each)\033[0m")
print("-" * 82)

guile_times = {}
for name, cat, desc in BENCHMARKS:
    script = f"test/benchmark/r7rs/{name}.scm"
    print(f"  {name:10s}", end="", flush=True)
    times = run_with_internal_timing(f"{GUILE_EXE} {script}", NUM_RUNS)
    m = med(times)
    guile_times[name] = m
    print(f"  median = {format_time_ms(m):>10s}")

print()

# ============================================================
# Phase 5: Python (internal timing via __TIMING__)
# ============================================================
print(f"\033[1mPhase 5: Python — internal timing ({NUM_RUNS} runs each)\033[0m")
print("-" * 82)

python_times = {}
for name, cat, desc in BENCHMARKS:
    script = f"{PY_DIR}/{name}.py"
    print(f"  {name:10s}", end="", flush=True)
    times = run_python_timing(f"{PYTHON_EXE} {script}", NUM_RUNS)
    m = med(times)
    python_times[name] = m
    print(f"  median = {format_time_ms(m):>10s}")

print()

# ============================================================
# Results Summary
# ============================================================
SEP = "=" * 100
print(f"\033[1m{SEP}\033[0m")
print(f"\033[1m{'RESULTS SUMMARY':^100s}\033[0m")
print(f"\033[1m{SEP}\033[0m")
print()

print(f"\033[36mComputation Time (median of {NUM_RUNS} runs) — pure benchmark execution\033[0m")
print(f"  Lambda: wall-clock minus ~{LAMBDA_STARTUP_MS}ms startup")
print(f"  Racket/Guile/Python: internal timing")
print()
hdr = f"{'Benchmark':10s} {'Lambda':>10s} {'Typed':>10s} {'Racket':>10s} {'Guile':>10s} {'Python':>10s} {'T/U':>7s} {'L/R':>7s} {'L/G':>7s} {'L/Py':>7s}"
print(hdr)
print(f"{'-'*10} {'-'*10} {'-'*10} {'-'*10} {'-'*10} {'-'*10} {'-'*7} {'-'*7} {'-'*7} {'-'*7}")

lr_ratios = []
lg_ratios = []
lpy_ratios = []
tu_ratios = []
tr_ratios = []

for name, cat, desc in BENCHMARKS:
    lm = lambda_times.get(name, 0)
    lt = lambda_typed_times.get(name, 0)
    rk = racket_times.get(name, 0)
    gl = guile_times.get(name, 0)
    py = python_times.get(name, 0)

    # Typed / Untyped ratio
    if lt > 0 and lm > 0:
        tu = lt / lm
        tu_s = f"{tu:.2f}x"
        tu_ratios.append(tu)
    else:
        tu_s = "N/A"

    if lm > 0 and rk > 0.001:
        lr = lm / rk
        lr_s = f"{lr:.0f}x"
        lr_ratios.append(lr)
    else:
        lr_s = "N/A"

    if lm > 0 and gl > 0.001:
        lg = lm / gl
        lg_s = f"{lg:.0f}x"
        lg_ratios.append(lg)
    else:
        lg_s = "N/A"

    if lm > 0 and py > 0.001:
        lpy = lm / py
        lpy_s = f"{lpy:.2f}x"
        lpy_ratios.append(lpy)
    else:
        lpy_s = "N/A"

    if lt > 0 and rk > 0.001:
        tr = lt / rk
        tr_ratios.append(tr)

    print(f"{name:10s} {format_time_ms(lm):>10s} {format_time_ms(lt):>10s} {format_time_ms(rk):>10s} {format_time_ms(gl):>10s} {format_time_ms(py):>10s} {tu_s:>7s} {lr_s:>7s} {lg_s:>7s} {lpy_s:>7s}")

print()
if lr_ratios:
    lr_gm = geo_mean(lr_ratios)
    lg_gm = geo_mean(lg_ratios) if lg_ratios else 0
    lpy_gm = geo_mean(lpy_ratios) if lpy_ratios else 0
    tu_gm = geo_mean(tu_ratios) if tu_ratios else 0
    tr_gm = geo_mean(tr_ratios) if tr_ratios else 0
    print(f"  Geometric mean ratios:")
    print(f"    Lambda (untyped) / Racket : {lr_gm:.2f}x")
    print(f"    Lambda (untyped) / Guile  : {lg_gm:.2f}x")
    if lpy_gm > 0:
        print(f"    Lambda (untyped) / Python : {lpy_gm:.2f}x")
    if tu_gm > 0:
        pct = (1.0 - tu_gm) * 100
        print(f"    Lambda typed / untyped    : {tu_gm:.2f}x  ({'%.0f%% faster' % pct if pct > 0 else '%.0f%% slower' % (-pct)})")
    if tr_gm > 0:
        print(f"    Lambda (typed) / Racket   : {tr_gm:.2f}x")

print()
print("Legend:")
print("  L/R, L/G, L/Py = Lambda(untyped) / other  (< 1.0 = Lambda faster, > 1.0 = other faster)")
print("  T/U = typed / untyped  (< 1.0 = typed faster)")
print()

# ============================================================
# Save CSV
# ============================================================
csv_path = "test/benchmark/r7rs/bench_results.csv"
with open(csv_path, "w") as f:
    f.write(f"# R7RS Benchmark Results {time.strftime('%Y-%m-%d %H:%M:%S')}, runs={NUM_RUNS}\n")
    f.write("benchmark,category,lambda_ms,lambda_typed_ms,racket_ms,guile_ms,python_ms\n")
    for name, cat, desc in BENCHMARKS:
        lm = lambda_times.get(name, 0)
        lt = lambda_typed_times.get(name, 0)
        rk = racket_times.get(name, 0)
        gl = guile_times.get(name, 0)
        py = python_times.get(name, 0)
        f.write(f"{name},{cat},{lm:.3f},{lt:.3f},{rk:.3f},{gl:.3f},{py:.3f}\n")
print(f"Raw data saved to {csv_path}")
