#!/usr/bin/env python3
"""BENG Benchmark Runner - Lambda Script vs Node.js vs Python

Runs all 10 Benchmarks Game benchmarks with configurable N values.
Uses external wall-clock timing for fair comparison.

Usage: python3 test/benchmark/beng/run_bench.py [num_runs]
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
PYTHON_EXE = "python3"
JS_DIR = "test/benchmark/beng/js"
LS_DIR = "test/benchmark/beng"
PY_DIR = "test/benchmark/beng/python"
INPUT_DIR = "test/benchmark/beng/input"

# Benchmark configs: (name, N_param, type)
# type: "n" = passed as CLI arg for JS, set in script for Lambda
#       "file" = uses file input (knucleotide, revcomp)
#
# N values: small for baseline testing, larger for real benchmarking
# These N values are tuned for ~0.1-5s execution times
BENCHMARKS = [
    # (name,          N,     type,     category)
    ("binarytrees",   10,    "n",      "gc"),
    ("fannkuch",      7,     "n",      "combinatorial"),
    ("fasta",         1000,  "n",      "string"),
    ("knucleotide",   None,  "file",   "hashmap"),
    ("mandelbrot",    200,   "n",      "numeric"),
    ("nbody",         1000,  "n",      "numeric"),
    ("pidigits",      30,    "n",      "bigint"),
    ("regexredux",    None,  "file",   "regex"),
    ("revcomp",       None,  "file",   "string"),
    ("spectralnorm",  100,   "n",      "numeric"),
]


def format_time(us):
    """Format microseconds to human-readable string."""
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
            print(f" [exit {r.returncode}: {stderr_short}]", end="")
        print(".", end="", flush=True)
    return times


def med(times):
    return int(statistics.median(sorted(times)))


# ============================================================
# Header
# ============================================================
node_ver = subprocess.run("node --version", shell=True, capture_output=True, text=True).stdout.strip()
py_ver = subprocess.run(f"{PYTHON_EXE} --version", shell=True, capture_output=True, text=True).stdout.strip()
print(f"\033[1mBENG Benchmark Suite — Lambda Script vs Node.js vs Python\033[0m")
print("=" * 78)
print(f"Runs per benchmark: {NUM_RUNS}")
print(f"Node.js: {node_ver}")
print(f"Python:  {py_ver}")
print()

# ============================================================
# Phase 1: Node.js External Timing
# ============================================================
print(f"\033[1mPhase 1: Node.js External ({NUM_RUNS} runs)\033[0m")
print("-" * 78)

js_ext = {}
for name, n_val, btype, cat in BENCHMARKS:
    js_file = os.path.join(JS_DIR, f"{name}.js")
    print(f"  {name:16s}", end="", flush=True)

    if btype == "n":
        cmd = f"node {js_file} {n_val}"
    else:
        # file-based: pass input path as arg
        input_file = os.path.join(INPUT_DIR, "fasta_1000.txt")
        cmd = f"node {js_file} {input_file}"

    times = run_external(cmd, NUM_RUNS, f"Node {name}")
    m = med(times)
    js_ext[name] = m
    print(f"  median={format_time(m):>10s}")

print()

# ============================================================
# Phase 2: Lambda External Timing
# ============================================================
print(f"\033[1mPhase 2: Lambda External ({NUM_RUNS} runs)\033[0m")
print("-" * 78)

lm_ext = {}
for name, n_val, btype, cat in BENCHMARKS:
    ls_file = os.path.join(LS_DIR, f"{name}.ls")
    print(f"  {name:16s}", end="", flush=True)
    cmd = f"{LAMBDA_EXE} run {ls_file}"
    times = run_external(cmd, NUM_RUNS, f"Lambda {name}")
    m = med(times)
    lm_ext[name] = m
    print(f"  median={format_time(m):>10s}")

print()

# ============================================================
# Phase 3: Python External Timing
# ============================================================
print(f"\033[1mPhase 3: Python External ({NUM_RUNS} runs)\033[0m")
print("-" * 78)

py_ext = {}
for name, n_val, btype, cat in BENCHMARKS:
    py_file = os.path.join(PY_DIR, f"{name}.py")
    print(f"  {name:16s}", end="", flush=True)
    if btype == "n":
        cmd = f"{PYTHON_EXE} {py_file} {n_val}"
    else:
        input_file = os.path.join(INPUT_DIR, "fasta_1000.txt")
        cmd = f"{PYTHON_EXE} {py_file} {input_file}"
    times = run_external(cmd, NUM_RUNS, f"Py {name}")
    m = med(times)
    py_ext[name] = m
    print(f"  median={format_time(m):>10s}")

print()

# ============================================================
# Summary
# ============================================================
SEP = "=" * 86
print(f"\033[1m{SEP}\033[0m")
print(f"\033[1m{'RESULTS SUMMARY':^86s}\033[0m")
print(f"\033[1m{SEP}\033[0m")
print()

print(f"{'Benchmark':16s} {'Category':>12s} {'Lambda':>12s} {'Node.js':>12s} {'Python':>12s} {'Lm/JS':>7s} {'Lm/Py':>7s}")
print(f"{'-'*16} {'-'*12} {'-'*12} {'-'*12} {'-'*12} {'-'*7} {'-'*7}")

ratios_js = []
ratios_py = []
lw_js = jw = lw_py = pw = 0
for name, n_val, btype, cat in BENCHMARKS:
    lm = lm_ext.get(name, 0)
    js = js_ext.get(name, 0)
    py = py_ext.get(name, 0)
    if lm > 0 and js > 0:
        r = lm / js
        rs_js = f"{r:.2f}x"
        ratios_js.append(r)
        if r <= 1.0: lw_js += 1
        else: jw += 1
    else:
        rs_js = "N/A"
    if lm > 0 and py > 0:
        r2 = lm / py
        rs_py = f"{r2:.2f}x"
        ratios_py.append(r2)
        if r2 <= 1.0: lw_py += 1
        else: pw += 1
    else:
        rs_py = "N/A"
    print(f"{name:16s} {cat:>12s} {format_time(lm):>12s} {format_time(js):>12s} {format_time(py):>12s} {rs_js:>7s} {rs_py:>7s}")

gm_js = gm_py = None
if ratios_js:
    gm_js = 1
    for r in ratios_js: gm_js *= r
    gm_js = gm_js ** (1.0 / len(ratios_js))
if ratios_py:
    gm_py = 1
    for r in ratios_py: gm_py *= r
    gm_py = gm_py ** (1.0 / len(ratios_py))

print()
if gm_js:
    print(f"  Geo mean Lambda/Node.js: {gm_js:.2f}x  |  Lambda wins: {lw_js}  |  Node.js wins: {jw}")
if gm_py:
    print(f"  Geo mean Lambda/Python:  {gm_py:.2f}x  |  Lambda wins: {lw_py}  |  Python wins: {pw}")

print()
print("Legend:")
print("  Ratio = Lambda / other (< 1.0 = Lambda faster, > 1.0 = other faster)")
print("  All times are wall-clock (startup + compile/JIT + execution)")
print()

# ============================================================
# Save CSV
# ============================================================
csv_path = "temp/beng_results.csv"
with open(csv_path, "w") as f:
    f.write(f"# BENG Results {time.strftime('%Y-%m-%d %H:%M:%S')}, runs={NUM_RUNS}\n")
    f.write("benchmark,category,n_value,lambda_med_us,nodejs_ext_med_us,python_ext_med_us\n")
    for name, n_val, btype, cat in BENCHMARKS:
        lm = lm_ext.get(name, 0)
        je = js_ext.get(name, 0)
        pe = py_ext.get(name, 0)
        nv = n_val if n_val else "file"
        f.write(f"{name},{cat},{nv},{lm},{je},{pe}\n")

print(f"Data saved to {csv_path}")

# ============================================================
# Generate Markdown Result Table
# Preserves content after "## Notes" if Result.md already exists
# ============================================================
md_path = os.path.join(LS_DIR, "Result.md")

# Read existing content after "## Notes" to preserve manually-written sections
preserved_notes = None
if os.path.exists(md_path):
    with open(md_path, "r") as f:
        existing = f.read()
    marker = "## Notes"
    idx = existing.find(marker)
    if idx >= 0:
        preserved_notes = existing[idx:]

with open(md_path, "w") as f:
    f.write("# BENG Benchmark Results\n\n")
    f.write("**Benchmarks Game** — Lambda Script vs Node.js\n\n")
    f.write(f"- Runs per benchmark: {NUM_RUNS}\n")
    f.write(f"- Node.js version: {node_ver}\n")
    f.write(f"- Date: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    f.write(f"- Timing: wall-clock (startup + compile/JIT + execution)\n\n")

    f.write("## Results\n\n")
    f.write("| Benchmark | Category | N | Lambda | Node.js | Python | Lm/JS | Lm/Py |\n")
    f.write("|-----------|----------|---|--------|---------|--------|-------|-------|\n")

    for name, n_val, btype, cat in BENCHMARKS:
        lm = lm_ext.get(name, 0)
        js = js_ext.get(name, 0)
        py = py_ext.get(name, 0)
        nv = str(n_val) if n_val else "file"
        rs_js = f"{lm/js:.2f}x" if lm > 0 and js > 0 else "N/A"
        rs_py = f"{lm/py:.2f}x" if lm > 0 and py > 0 else "N/A"
        f.write(f"| {name} | {cat} | {nv} | {format_time(lm)} | {format_time(js)} | {format_time(py)} | {rs_js} | {rs_py} |\n")

    f.write("\n")
    if gm_js:
        f.write(f"**Geo mean Lambda/Node.js: {gm_js:.2f}x** (Lambda wins: {lw_js}, Node.js wins: {jw})\n\n")
    if gm_py:
        f.write(f"**Geo mean Lambda/Python: {gm_py:.2f}x** (Lambda wins: {lw_py}, Python wins: {pw})\n\n")

    if preserved_notes:
        f.write(preserved_notes)
    else:
        f.write("## Notes\n\n")
        f.write("- Ratio < 1.0 means Lambda is faster; > 1.0 means the other language is faster\n")
        f.write("- Lambda uses MIR JIT compilation\n")
        f.write("- Node.js uses V8 JIT compilation\n")
        f.write("- N values are kept small for baseline testing; increase for production benchmarks\n")
        f.write("- File-based benchmarks (knucleotide, revcomp, regexredux) use `input/fasta_1000.txt`\n")

print(f"Result.md saved to {md_path}")
