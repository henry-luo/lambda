#!/usr/bin/env python3
"""
Unified Benchmark Runner — time, memory, and MIR-vs-C comparison.

Consolidates: run_all_benchmarks.py, run_all_benchmarks_v3.py, run_js_benchmarks.py,
              run_beng_benchmarks.py, run_memory_benchmarks.py, run_mir_vs_c.py,
              rerun_mir_ljs.py, temp/rerun_synced_benchmarks.py

Modes:
  time      Measure execution time (default)
  memory    Measure peak resident set size (RSS)
  mir-vs-c  Compare MIR Direct vs C2MIR transpiler

Usage examples:
  python3 test/benchmark/run_benchmarks.py                          # Run ALL time benchmarks
  python3 test/benchmark/run_benchmarks.py -s jetstream             # JetStream suite only
  python3 test/benchmark/run_benchmarks.py -s awfy,beng             # AWFY + BENG
  python3 test/benchmark/run_benchmarks.py -b nbody,richards        # nbody & richards across all suites
  python3 test/benchmark/run_benchmarks.py -s awfy -b mandelbrot    # AWFY mandelbrot only
  python3 test/benchmark/run_benchmarks.py -b deltablue -n 5        # 5 runs per engine
  python3 test/benchmark/run_benchmarks.py -e mir,nodejs            # MIR + Node.js only
  python3 test/benchmark/run_benchmarks.py -m memory                # Peak RSS measurement
  python3 test/benchmark/run_benchmarks.py -m mir-vs-c              # MIR Direct vs C2MIR
  python3 test/benchmark/run_benchmarks.py -m mir-vs-c --typed      # Include typed R7RS variants
  python3 test/benchmark/run_benchmarks.py --list                   # List all benchmarks
  python3 test/benchmark/run_benchmarks.py --dry-run -b nbody       # Show what would run
"""

import argparse
import json
import math
import os
import platform
import re
import signal
import subprocess
import sys
import time

# ============================================================
# Ensure we run from project root regardless of where script is invoked
# ============================================================
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.join(SCRIPT_DIR, "..", "..")
os.chdir(PROJECT_ROOT)

# ============================================================
# Configuration
# ============================================================
DEFAULT_TIMEOUT_S = 120
LAMBDA_EXE = "./lambda.exe"
NODE_EXE = "node"
PYTHON_EXE = "python3"
QJS_EXE = "qjs"
TIMING_RE = re.compile(r"__TIMING__:([\d.]+(?:e[+-]?\d+)?)")

TIME_JSON_PATH = "test/benchmark/benchmark_results_v3.json"
MEMORY_JSON_PATH = "test/benchmark/memory_results.json"
MIR_VS_C_CSV_PATH = "temp/mir_vs_c_bench.csv"

IS_MACOS = platform.system() == "Darwin"

ALL_ENGINES = ["mir", "c2mir", "lambdajs", "quickjs", "nodejs", "python"]
ENGINE_LABELS = {
    "mir": "MIR", "c2mir": "C2MIR", "lambdajs": "LambdaJS",
    "quickjs": "QuickJS", "nodejs": "Node.js", "python": "Python",
}

# ============================================================
# Benchmark registry — (name, category, ls_path, js_path, py_path)
# JetStream uses 3-tuples: (name, category, ls_path)
# ============================================================
R7RS = [
    ("fib",     "recursive", "test/benchmark/r7rs/fib2.ls",     "test/benchmark/r7rs/fib2.js",     "test/benchmark/r7rs/python/fib.py"),
    ("fibfp",   "recursive", "test/benchmark/r7rs/fibfp2.ls",   "test/benchmark/r7rs/fibfp2.js",   "test/benchmark/r7rs/python/fibfp.py"),
    ("tak",     "recursive", "test/benchmark/r7rs/tak2.ls",     "test/benchmark/r7rs/tak2.js",     "test/benchmark/r7rs/python/tak.py"),
    ("cpstak",  "closure",   "test/benchmark/r7rs/cpstak2.ls",  "test/benchmark/r7rs/cpstak2.js",  "test/benchmark/r7rs/python/cpstak.py"),
    ("sum",     "iterative", "test/benchmark/r7rs/sum2.ls",     "test/benchmark/r7rs/sum2.js",     "test/benchmark/r7rs/python/sum.py"),
    ("sumfp",   "iterative", "test/benchmark/r7rs/sumfp2.ls",   "test/benchmark/r7rs/sumfp2.js",   "test/benchmark/r7rs/python/sumfp.py"),
    ("nqueens", "backtrack", "test/benchmark/r7rs/nqueens2.ls", "test/benchmark/r7rs/nqueens2.js", "test/benchmark/r7rs/python/nqueens.py"),
    ("fft",     "numeric",   "test/benchmark/r7rs/fft2.ls",     "test/benchmark/r7rs/fft2.js",     "test/benchmark/r7rs/python/fft.py"),
    ("mbrot",   "numeric",   "test/benchmark/r7rs/mbrot2.ls",   "test/benchmark/r7rs/mbrot2.js",   "test/benchmark/r7rs/python/mbrot.py"),
    ("ack",     "recursive", "test/benchmark/r7rs/ack2.ls",     "test/benchmark/r7rs/ack2.js",     "test/benchmark/r7rs/python/ack.py"),
]

AWFY = [
    ("sieve",      "micro",   "test/benchmark/awfy/sieve2.ls",      "test/benchmark/awfy/sieve2.js",      "test/benchmark/awfy/python/sieve.py"),
    ("permute",    "micro",   "test/benchmark/awfy/permute2.ls",    "test/benchmark/awfy/permute2.js",    "test/benchmark/awfy/python/permute.py"),
    ("queens",     "micro",   "test/benchmark/awfy/queens2.ls",     "test/benchmark/awfy/queens2.js",     "test/benchmark/awfy/python/queens.py"),
    ("towers",     "micro",   "test/benchmark/awfy/towers2.ls",     "test/benchmark/awfy/towers2.js",     "test/benchmark/awfy/python/towers.py"),
    ("bounce",     "micro",   "test/benchmark/awfy/bounce2.ls",     "test/benchmark/awfy/bounce2.js",     "test/benchmark/awfy/python/bounce.py"),
    ("list",       "micro",   "test/benchmark/awfy/list2.ls",       "test/benchmark/awfy/list2.js",       "test/benchmark/awfy/python/list.py"),
    ("storage",    "micro",   "test/benchmark/awfy/storage2.ls",    "test/benchmark/awfy/storage2.js",    "test/benchmark/awfy/python/storage.py"),
    ("mandelbrot", "compute", "test/benchmark/awfy/mandelbrot2.ls", "test/benchmark/awfy/mandelbrot2.js", "test/benchmark/awfy/python/mandelbrot.py"),
    ("nbody",      "compute", "test/benchmark/awfy/nbody2.ls",      "test/benchmark/awfy/nbody2.js",      "test/benchmark/awfy/python/nbody.py"),
    ("richards",   "macro",   "test/benchmark/awfy/richards2.ls",   "test/benchmark/awfy/richards2.js",   "test/benchmark/awfy/python/richards.py"),
    ("json",       "macro",   "test/benchmark/awfy/json2.ls",       "test/benchmark/awfy/json2.js",       "test/benchmark/awfy/python/json.py"),
    ("deltablue",  "macro",   "test/benchmark/awfy/deltablue2.ls",  "test/benchmark/awfy/deltablue2.js",  "test/benchmark/awfy/python/deltablue.py"),
    ("havlak",     "macro",   "test/benchmark/awfy/havlak2.ls",     "test/benchmark/awfy/havlak2.js",     "test/benchmark/awfy/python/havlak.py"),
    ("cd",         "macro",   "test/benchmark/awfy/cd2.ls",         "test/benchmark/awfy/cd2.js",         "test/benchmark/awfy/python/cd.py"),
]

BENG = [
    ("binarytrees",  "allocation",  "test/benchmark/beng/binarytrees.ls",  "test/benchmark/beng/js/binarytrees.js",  "test/benchmark/beng/python/binarytrees.py"),
    ("fannkuch",     "permutation", "test/benchmark/beng/fannkuch.ls",     "test/benchmark/beng/js/fannkuch.js",     "test/benchmark/beng/python/fannkuch.py"),
    ("fasta",        "generation",  "test/benchmark/beng/fasta.ls",        "test/benchmark/beng/js/fasta.js",        "test/benchmark/beng/python/fasta.py"),
    ("knucleotide",  "hashing",     "test/benchmark/beng/knucleotide.ls",  "test/benchmark/beng/js/knucleotide.js",  "test/benchmark/beng/python/knucleotide.py"),
    ("mandelbrot",   "numeric",     "test/benchmark/beng/mandelbrot.ls",   "test/benchmark/beng/js/mandelbrot.js",   "test/benchmark/beng/python/mandelbrot.py"),
    ("nbody",        "numeric",     "test/benchmark/beng/nbody.ls",        "test/benchmark/beng/js/nbody.js",        "test/benchmark/beng/python/nbody.py"),
    ("pidigits",     "bignum",      "test/benchmark/beng/pidigits.ls",     "test/benchmark/beng/js/pidigits.js",     "test/benchmark/beng/python/pidigits.py"),
    ("regexredux",   "regex",       "test/benchmark/beng/regexredux.ls",   "test/benchmark/beng/js/regexredux.js",   "test/benchmark/beng/python/regexredux.py"),
    ("revcomp",      "string",      "test/benchmark/beng/revcomp.ls",      "test/benchmark/beng/js/revcomp.js",      "test/benchmark/beng/python/revcomp.py"),
    ("spectralnorm", "numeric",     "test/benchmark/beng/spectralnorm.ls", "test/benchmark/beng/js/spectralnorm.js", "test/benchmark/beng/python/spectralnorm.py"),
]

KOSTYA = [
    ("brainfuck",   "interpreter", "test/benchmark/kostya/brainfuck.ls",   "test/benchmark/kostya/brainfuck.js",   "test/benchmark/kostya/python/brainfuck.py"),
    ("matmul",      "numeric",     "test/benchmark/kostya/matmul.ls",      "test/benchmark/kostya/matmul.js",      "test/benchmark/kostya/python/matmul.py"),
    ("primes",      "numeric",     "test/benchmark/kostya/primes.ls",      "test/benchmark/kostya/primes.js",      "test/benchmark/kostya/python/primes.py"),
    ("base64",      "string",      "test/benchmark/kostya/base64.ls",      "test/benchmark/kostya/base64.js",      "test/benchmark/kostya/python/base64.py"),
    ("levenshtein", "string",      "test/benchmark/kostya/levenshtein.ls", "test/benchmark/kostya/levenshtein.js", "test/benchmark/kostya/python/levenshtein.py"),
    ("json_gen",    "data",        "test/benchmark/kostya/json_gen.ls",    "test/benchmark/kostya/json_gen.js",    "test/benchmark/kostya/python/json_gen.py"),
    ("collatz",     "numeric",     "test/benchmark/kostya/collatz.ls",     "test/benchmark/kostya/collatz.js",     "test/benchmark/kostya/python/collatz.py"),
]

LARCENY = [
    ("triangl",    "search",     "test/benchmark/larceny/triangl.ls",    "test/benchmark/larceny/triangl.js",    "test/benchmark/larceny/python/triangl.py"),
    ("array1",     "array",      "test/benchmark/larceny/array1.ls",     "test/benchmark/larceny/array1.js",     "test/benchmark/larceny/python/array1.py"),
    ("deriv",      "symbolic",   "test/benchmark/larceny/deriv.ls",      "test/benchmark/larceny/deriv.js",      "test/benchmark/larceny/python/deriv.py"),
    ("diviter",    "iterative",  "test/benchmark/larceny/diviter.ls",    "test/benchmark/larceny/diviter.js",    "test/benchmark/larceny/python/diviter.py"),
    ("divrec",     "recursive",  "test/benchmark/larceny/divrec.ls",     "test/benchmark/larceny/divrec.js",     "test/benchmark/larceny/python/divrec.py"),
    ("gcbench",    "allocation", "test/benchmark/larceny/gcbench.ls",    "test/benchmark/larceny/gcbench.js",    "test/benchmark/larceny/python/gcbench.py"),
    ("paraffins",  "combinat",   "test/benchmark/larceny/paraffins.ls",  "test/benchmark/larceny/paraffins.js",  "test/benchmark/larceny/python/paraffins.py"),
    ("pnpoly",     "numeric",    "test/benchmark/larceny/pnpoly.ls",    "test/benchmark/larceny/pnpoly.js",     "test/benchmark/larceny/python/pnpoly.py"),
    ("primes",     "iterative",  "test/benchmark/larceny/primes.ls",    "test/benchmark/larceny/primes.js",     "test/benchmark/larceny/python/primes.py"),
    ("puzzle",     "search",     "test/benchmark/larceny/puzzle.ls",    "test/benchmark/larceny/puzzle.js",     "test/benchmark/larceny/python/puzzle.py"),
    ("quicksort",  "sorting",    "test/benchmark/larceny/quicksort.ls", "test/benchmark/larceny/quicksort.js",  "test/benchmark/larceny/python/quicksort.py"),
    ("ray",        "numeric",    "test/benchmark/larceny/ray.ls",       "test/benchmark/larceny/ray.js",        "test/benchmark/larceny/python/ray.py"),
]

JETSTREAM_LS = [
    ("nbody",         "numeric", "test/benchmark/jetstream/nbody.ls"),
    ("cube3d",        "3d",      "test/benchmark/jetstream/cube3d.ls"),
    ("navier_stokes", "numeric", "test/benchmark/jetstream/navier_stokes.ls"),
    ("richards",      "macro",   "test/benchmark/jetstream/richards.ls"),
    ("splay",         "data",    "test/benchmark/jetstream/splay.ls"),
    ("deltablue",     "macro",   "test/benchmark/jetstream/deltablue.ls"),
    ("hashmap",       "data",    "test/benchmark/jetstream/hashmap.ls"),
    ("crypto_sha1",   "crypto",  "test/benchmark/jetstream/crypto_sha1.ls"),
    ("raytrace3d",    "3d",      "test/benchmark/jetstream/raytrace3d.ls"),
]

JETSTREAM_NODE = {
    "nbody":         "ref/JetStream/SunSpider/n-body.js",
    "cube3d":        "ref/JetStream/SunSpider/3d-cube.js",
    "navier_stokes": "ref/JetStream/Octane/navier-stokes.js",
    "richards":      "ref/JetStream/Octane/richards.js",
    "splay":         "ref/JetStream/Octane/splay.js",
    "deltablue":     "ref/JetStream/Octane/deltablue.js",
    "hashmap":       "ref/JetStream/simple/hash-map.js",
    "crypto_sha1":   "ref/JetStream/SunSpider/crypto-sha1.js",
    "raytrace3d":    "ref/JetStream/SunSpider/3d-raytrace.js",
}

JETSTREAM_PY = {
    "deltablue": "test/benchmark/jetstream/deltablue.py",
    "richards":  "test/benchmark/jetstream/richards.py",
    "nbody":     "test/benchmark/jetstream/nbody.py",
}

STANDARD_SUITES = [
    ("r7rs",    R7RS),
    ("awfy",    AWFY),
    ("beng",    BENG),
    ("kostya",  KOSTYA),
    ("larceny", LARCENY),
]

ALL_SUITE_NAMES = ["r7rs", "awfy", "beng", "kostya", "larceny", "jetstream"]

# QuickJS polyfill
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

# AWFY Python harness config: int = inner_iterations, tuple = (num_iterations, inner_iterations)
AWFY_PY_CONFIG = {
    "sieve": 3000, "permute": 1500, "queens": 1500, "towers": 600,
    "bounce": 1500, "list": 1500, "storage": 1000,
    "mandelbrot": 500, "nbody": 36000, "richards": 50,
    "json": 1, "deltablue": (20, 100), "havlak": 1, "cd": 1,
}

# AWFY Python class names (for benchmarks where capitalize() doesn't produce the right name)
AWFY_PY_CLASSNAME = {
    "nbody": "NBody",
    "deltablue": "DeltaBlue",
    "cd": "CD",
}


# ============================================================
# Utility helpers
# ============================================================

def parse_timing(stdout):
    """Extract __TIMING__:NNN from stdout, return ms or None."""
    for line in stdout.strip().split("\n"):
        m = TIMING_RE.search(line.strip())
        if m:
            return float(m.group(1))
    return None


def geo_mean(values):
    """Geometric mean of positive numbers."""
    vals = [v for v in values if v is not None and v > 0]
    if not vals:
        return None
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


def match_filter(name, filters):
    """Check if name matches any of the filter strings (case-insensitive substring)."""
    if not filters:
        return True
    name_lower = name.lower()
    return any(f.lower() in name_lower for f in filters)


def fmt_ms(ms):
    """Format milliseconds for display."""
    if ms is None:
        return "---"
    if ms < 1:
        return f"{ms:.3f}"
    if ms < 10:
        return f"{ms:.2f}"
    if ms < 100:
        return f"{ms:.1f}"
    if ms < 1000:
        return f"{ms:.0f}"
    return f"{ms / 1000:.2f}s"


def fmt_us(us):
    """Format microseconds for display."""
    if us is None or us <= 0:
        return "N/A"
    if us < 1000:
        return f"{us:.0f} us"
    elif us < 1000000:
        return f"{us / 1000:.1f} ms"
    else:
        return f"{us / 1000000:.3f} s"


def fmt_mem(rss_bytes):
    """Format RSS bytes to human-readable string."""
    if rss_bytes is None:
        return "---"
    mb = rss_bytes / (1024 * 1024)
    if mb < 1.0:
        kb = rss_bytes / 1024
        return f"{kb:.0f}K"
    elif mb < 10:
        return f"{mb:.2f}M"
    elif mb < 100:
        return f"{mb:.1f}M"
    elif mb < 1024:
        return f"{mb:.0f}M"
    else:
        gb = mb / 1024
        return f"{gb:.2f}G"


def fmt_mem_mb(rss_bytes):
    """Format RSS bytes as MB for JSON export."""
    if rss_bytes is None:
        return None
    return round(rss_bytes / (1024 * 1024), 2)


def make_qjs_wrapper(js_path):
    """Create a QuickJS-compatible wrapper for a Node.js benchmark script."""
    os.makedirs("temp", exist_ok=True)
    wrapper = os.path.join("temp", "qjs_" + os.path.basename(js_path))
    with open(js_path) as f:
        code = f.read()
    with open(wrapper, "w") as f:
        f.write("import * as std from 'std';\n")
        f.write(QJS_POLYFILL)
        code = code.replace("'use strict';", "")
        code = code.replace('"use strict";', "")
        f.write(code)
    return wrapper


def make_jetstream_node_wrapper(bench_name, js_path):
    """Create Node.js wrapper for JetStream benchmark (adds runIteration + timing)."""
    os.makedirs("temp", exist_ok=True)
    wrapper = os.path.join("temp", f"_node_bench_{bench_name}.js")
    with open(js_path) as f:
        code = f.read()
    with open(wrapper, "w") as f:
        f.write(code)
        f.write("\nconst b = new Benchmark();\n")
        f.write("const t0 = performance.now();\n")
        f.write("b.runIteration();\n")
        f.write("const t1 = performance.now();\n")
        f.write('console.log("__TIMING__:" + (t1 - t0).toFixed(3));\n')
    return wrapper


# ============================================================
# Build the unified benchmark list
# ============================================================

def build_benchmark_list(suite_filters, bench_filters):
    """
    Build flat list of benchmarks to run based on suite/bench filters.
    Returns list of dicts with keys:
        suite, name, category, ls_path, js_path, py_path, is_jetstream, ref_js
    """
    benchmarks = []

    for suite_name, suite_benchmarks in STANDARD_SUITES:
        if not match_filter(suite_name, suite_filters):
            continue
        for entry in suite_benchmarks:
            bench_name, category, ls_path, js_path, py_path = entry
            if not match_filter(bench_name, bench_filters):
                continue
            benchmarks.append({
                "suite": suite_name,
                "name": bench_name,
                "category": category,
                "ls_path": ls_path,
                "js_path": js_path,
                "py_path": py_path,
                "is_jetstream": False,
                "ref_js": None,
            })

    if match_filter("jetstream", suite_filters):
        for entry in JETSTREAM_LS:
            bench_name, category, ls_path = entry
            if not match_filter(bench_name, bench_filters):
                continue
            benchmarks.append({
                "suite": "jetstream",
                "name": bench_name,
                "category": category,
                "ls_path": ls_path,
                "js_path": None,
                "py_path": JETSTREAM_PY.get(bench_name),
                "is_jetstream": True,
                "ref_js": JETSTREAM_NODE.get(bench_name),
            })

    return benchmarks


def list_benchmarks():
    """Print all available benchmarks grouped by suite."""
    all_b = build_benchmark_list(None, None)
    current_suite = None
    for b in all_b:
        if b["suite"] != current_suite:
            current_suite = b["suite"]
            print(f"\n  {current_suite.upper()} ({sum(1 for x in all_b if x['suite'] == current_suite)} benchmarks)")
            print(f"  {'Name':20s} {'Category':12s} {'LS':4s} {'JS':4s} {'PY':4s}")
            print(f"  {'-' * 46}")
        has_ls = "yes" if b["ls_path"] and os.path.exists(b["ls_path"]) else "no"
        has_js = "yes" if (b["js_path"] and os.path.exists(b["js_path"])) or (b["ref_js"] and os.path.exists(b["ref_js"])) else "no"
        has_py = "yes" if b["py_path"] and os.path.exists(b["py_path"]) else "no"
        print(f"  {b['name']:20s} {b['category']:12s} {has_ls:4s} {has_js:4s} {has_py:4s}")
    total = len(all_b)
    print(f"\n  Total: {total} benchmarks across {len(ALL_SUITE_NAMES)} suites")


# ============================================================
# TIME mode — execution time measurement
# ============================================================

def time_run_once(cmd, timeout_s):
    """Run cmd once using process groups for reliable timeout kill.
    Returns (wall_ms, exec_ms_or_None, success)."""
    start = time.perf_counter_ns()
    try:
        proc = subprocess.Popen(
            cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, preexec_fn=os.setsid)
        try:
            stdout, stderr = proc.communicate(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            proc.wait()
            return (timeout_s * 1000, None, False)
        end = time.perf_counter_ns()
        wall_ms = (end - start) / 1_000_000
        if proc.returncode != 0:
            return (wall_ms, None, False)
        exec_ms = parse_timing(stdout)
        return (wall_ms, exec_ms, True)
    except Exception:
        return (0, None, False)


def time_run_benchmark(cmd, num_runs, timeout_s):
    """Run cmd N times, return (median_wall_ms, median_exec_ms_or_None, success)."""
    walls, execs = [], []
    ok = False
    for _ in range(num_runs):
        w, e, success = time_run_once(cmd, timeout_s)
        if success:
            ok = True
            walls.append(w)
            if e is not None:
                execs.append(e)
        print(".", end="", flush=True)
    if not ok:
        return (None, None, False)
    walls.sort()
    execs.sort()
    return (walls[len(walls) // 2],
            execs[len(execs) // 2] if execs else None,
            True)


def time_run_awfy_python(bench_name, num_runs, timeout_s):
    """Run AWFY Python benchmark via harness. Returns (wall_ms, exec_ms, success)."""
    cfg = AWFY_PY_CONFIG.get(bench_name, 1)
    if isinstance(cfg, tuple):
        num_iter, inner = cfg
    else:
        num_iter, inner = 1, cfg
    py_dir = "test/benchmark/awfy/python"
    class_name = AWFY_PY_CLASSNAME.get(bench_name, bench_name.capitalize())
    cmd = f"cd {py_dir} && {PYTHON_EXE} harness.py {class_name} {num_iter} {inner}"
    walls, execs = [], []
    ok = False
    for _ in range(num_runs):
        start = time.perf_counter_ns()
        try:
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout_s)
        except subprocess.TimeoutExpired:
            print("T", end="", flush=True)
            continue
        end = time.perf_counter_ns()
        wall_ms = (end - start) / 1_000_000
        if r.returncode != 0:
            print("X", end="", flush=True)
            continue
        ok = True
        walls.append(wall_ms)
        run_total_us = 0
        for line in r.stdout.strip().split("\n"):
            m2 = re.search(r"runtime:\s*(\d+)us", line)
            if m2:
                run_total_us += float(m2.group(1))
        if run_total_us > 0:
            execs.append(run_total_us / 1000.0)
        t = parse_timing(r.stdout)
        if t is not None and not execs:
            execs.append(t)
        print(".", end="", flush=True)
    if not ok:
        return (None, None, False)
    walls.sort()
    execs.sort()
    return (walls[len(walls) // 2],
            execs[len(execs) // 2] if execs else None,
            True)


def time_run_single(b, engines, num_runs, timeout_s, results):
    """Run one benchmark across selected engines in TIME mode. Updates results dict."""
    suite = b["suite"]
    name = b["name"]
    is_js = b["is_jetstream"]
    ls_path = b["ls_path"]
    js_path = b["js_path"]
    py_path = b["py_path"]
    ref_js = b["ref_js"]

    if suite not in results:
        results[suite] = {}
    if name not in results[suite]:
        results[suite][name] = {}

    row = {}

    # --- MIR Direct ---
    if "mir" in engines:
        print(f"  MIR      ", end="", flush=True)
        w, e, ok = time_run_benchmark(f"{LAMBDA_EXE} run {ls_path}", num_runs, timeout_s)
        val = e if ok and e is not None else (w if ok else None)
        results[suite][name]["mir"] = val
        row["mir"] = val
        print(f" {fmt_ms(e if e is not None else w)}")

    # --- C2MIR ---
    if "c2mir" in engines:
        print(f"  C2MIR    ", end="", flush=True)
        w, e, ok = time_run_benchmark(f"{LAMBDA_EXE} run --c2mir {ls_path}", num_runs, timeout_s)
        val = e if ok and e is not None else (w if ok else None)
        results[suite][name]["c2mir"] = val
        row["c2mir"] = val
        print(f" {fmt_ms(e if e is not None else w)}")

    if not is_js:
        bundle_path = js_path.replace("2.js", "2_bundle.js") if js_path else None
        standalone_js = bundle_path if (suite == "awfy" and bundle_path and os.path.exists(bundle_path)) else js_path

        # --- LambdaJS ---
        if "lambdajs" in engines:
            if standalone_js and os.path.exists(standalone_js):
                print(f"  LambdaJS ", end="", flush=True)
                w, e, ok = time_run_benchmark(f"{LAMBDA_EXE} js {standalone_js}", num_runs, timeout_s)
                val = e if ok and e is not None else (w if ok else None)
                results[suite][name]["lambdajs"] = val
                row["lambdajs"] = val
                print(f" {fmt_ms(e if e is not None else w)}")
            else:
                results[suite][name]["lambdajs"] = None
                row["lambdajs"] = None
                print(f"  LambdaJS  ---")

        # --- QuickJS ---
        if "quickjs" in engines:
            if standalone_js and os.path.exists(standalone_js):
                print(f"  QuickJS  ", end="", flush=True)
                wrapper = make_qjs_wrapper(standalone_js)
                w, e, ok = time_run_benchmark(f"{QJS_EXE} --std -m {wrapper}", num_runs, timeout_s)
                val = e if ok and e is not None else (w if ok else None)
                results[suite][name]["quickjs"] = val
                row["quickjs"] = val
                print(f" {fmt_ms(e if e is not None else w)}")
            else:
                results[suite][name]["quickjs"] = None
                row["quickjs"] = None
                print(f"  QuickJS   ---")

        # --- Node.js ---
        if "nodejs" in engines:
            if js_path and os.path.exists(js_path):
                print(f"  Node.js  ", end="", flush=True)
                w, e, ok = time_run_benchmark(f"{NODE_EXE} {js_path}", num_runs, timeout_s)
                val = e if ok and e is not None else (w if ok else None)
                results[suite][name]["nodejs"] = val
                row["nodejs"] = val
                print(f" {fmt_ms(e if e is not None else w)}")
            else:
                results[suite][name]["nodejs"] = None
                row["nodejs"] = None
                print(f"  Node.js   ---")

        # --- Python ---
        if "python" in engines:
            if suite == "awfy":
                print(f"  Python   ", end="", flush=True)
                w, e, ok = time_run_awfy_python(name, num_runs, timeout_s)
                val = e if ok and e is not None else (w if ok else None)
                results[suite][name]["python"] = val
                row["python"] = val
                print(f" {fmt_ms(e if e is not None else w)}")
            elif py_path and os.path.exists(py_path):
                print(f"  Python   ", end="", flush=True)
                w, e, ok = time_run_benchmark(f"{PYTHON_EXE} {py_path}", num_runs, timeout_s)
                val = e if ok and e is not None else (w if ok else None)
                results[suite][name]["python"] = val
                row["python"] = val
                print(f" {fmt_ms(e if e is not None else w)}")
            else:
                results[suite][name]["python"] = None
                row["python"] = None
                print(f"  Python    ---")
    else:
        # JetStream suite: no LambdaJS/QuickJS
        if "lambdajs" in engines:
            results[suite][name]["lambdajs"] = None
            row["lambdajs"] = None
            print(f"  LambdaJS  ---")

        if "quickjs" in engines:
            results[suite][name]["quickjs"] = None
            row["quickjs"] = None
            print(f"  QuickJS   ---")

        if "nodejs" in engines:
            if ref_js and os.path.exists(ref_js):
                print(f"  Node.js  ", end="", flush=True)
                wrapper = make_jetstream_node_wrapper(name, ref_js)
                w, e, ok = time_run_benchmark(f"{NODE_EXE} {wrapper}", num_runs, timeout_s)
                val = e if ok and e is not None else (w if ok else None)
                results[suite][name]["nodejs"] = val
                row["nodejs"] = val
                print(f" {fmt_ms(e if e is not None else w)}")
            else:
                results[suite][name]["nodejs"] = None
                row["nodejs"] = None
                print(f"  Node.js   ---")

        if "python" in engines:
            if py_path and os.path.exists(py_path):
                print(f"  Python   ", end="", flush=True)
                w, e, ok = time_run_benchmark(f"{PYTHON_EXE} {py_path}", num_runs, timeout_s)
                val = e if ok and e is not None else (w if ok else None)
                results[suite][name]["python"] = val
                row["python"] = val
                print(f" {fmt_ms(e if e is not None else w)}")
            else:
                results[suite][name]["python"] = None
                row["python"] = None
                print(f"  Python    ---")

    return row


def run_time_mode(benchmarks, engines, num_runs, timeout_s, no_save):
    """Execute TIME mode: measure execution time across engines."""
    # Load existing results (merge mode)
    if os.path.exists(TIME_JSON_PATH):
        with open(TIME_JSON_PATH) as f:
            results = json.load(f)
    else:
        results = {}

    summary = []
    current_suite = None
    for b in benchmarks:
        if b["suite"] != current_suite:
            current_suite = b["suite"]
            count = sum(1 for x in benchmarks if x["suite"] == current_suite)
            print(f"\n{'=' * 70}")
            print(f"  Suite: {current_suite.upper()} ({count} benchmarks)")
            print(f"{'=' * 70}")

        print(f"\n  {b['name']} ({b['category']}):")
        if not os.path.exists(b["ls_path"]):
            print(f"    SKIPPED (file not found: {b['ls_path']})")
            continue

        row = time_run_single(b, engines, num_runs, timeout_s, results)
        summary.append((b["suite"], b["name"], row))

    # Save
    if not no_save:
        with open(TIME_JSON_PATH, "w") as f:
            json.dump(results, f, indent=2, default=str)
        print(f"\nResults saved to {TIME_JSON_PATH}")
    else:
        print(f"\n--no-save: results NOT written to JSON")

    # Summary table
    print(f"\n{'=' * 80}")
    print(f"TIME SUMMARY (exec_ms, median of {num_runs} runs)")
    print(f"{'=' * 80}")
    hdr = f"  {'Suite/Bench':24s}"
    for eng in engines:
        hdr += f" {ENGINE_LABELS[eng]:>8s}"
    print(hdr)
    print(f"  {'-' * (24 + 9 * len(engines))}")

    for suite, name, row in summary:
        line = f"  {suite + '/' + name:24s}"
        for eng in engines:
            val = row.get(eng)
            if val is None:
                val = results.get(suite, {}).get(name, {}).get(eng)
            line += f" {fmt_ms(val):>8s}"
        print(line)

    print(f"\nDone! {len(summary)} benchmarks completed.")


# ============================================================
# MEMORY mode — peak RSS measurement
# ============================================================

def parse_peak_rss_bytes(stderr_text):
    """Parse peak RSS from /usr/bin/time output. Returns bytes or None."""
    if IS_MACOS:
        m = re.search(r"(\d+)\s+maximum resident set size", stderr_text)
        if m:
            return int(m.group(1))
    else:
        m = re.search(r"Maximum resident set size.*?:\s*(\d+)", stderr_text)
        if m:
            return int(m.group(1)) * 1024
    return None


def mem_measure_once(cmd, timeout_s):
    """Run a command and measure its peak RSS. Returns (peak_rss_bytes, success)."""
    timed_cmd = f"/usr/bin/time -l {cmd}" if IS_MACOS else f"/usr/bin/time -v {cmd}"
    try:
        r = subprocess.run(timed_cmd, shell=True, capture_output=True, text=True, timeout=timeout_s)
    except subprocess.TimeoutExpired:
        return (None, False)
    if r.returncode != 0:
        return (None, False)
    peak = parse_peak_rss_bytes(r.stderr)
    return (peak, peak is not None)


def mem_measure_n(cmd, num_runs, timeout_s):
    """Run N times, return (median_peak_rss_bytes, success)."""
    measurements = []
    for _ in range(num_runs):
        peak, ok = mem_measure_once(cmd, timeout_s)
        if ok and peak is not None:
            measurements.append(peak)
        print(".", end="", flush=True)
    if not measurements:
        return (None, False)
    measurements.sort()
    return (measurements[len(measurements) // 2], True)


def mem_make_awfy_python_cmd(bench_name):
    """Build the command for AWFY Python harness (memory mode uses 1 iter for large benchmarks)."""
    cfg = AWFY_PY_CONFIG.get(bench_name, 1)
    if isinstance(cfg, tuple):
        num_iter, inner = cfg
    else:
        num_iter, inner = 1, cfg
    py_dir = "test/benchmark/awfy/python"
    class_name = AWFY_PY_CLASSNAME.get(bench_name, bench_name.capitalize())
    return f"cd {py_dir} && {PYTHON_EXE} harness.py {class_name} {num_iter} {inner}"


def mem_run_single(b, engines, num_runs, timeout_s, results):
    """Run one benchmark across selected engines in MEMORY mode. Updates results dict."""
    suite = b["suite"]
    name = b["name"]
    is_js = b["is_jetstream"]
    ls_path = b["ls_path"]
    js_path = b["js_path"]
    py_path = b["py_path"]
    ref_js = b["ref_js"]

    if suite not in results:
        results[suite] = {}
    if name not in results[suite]:
        results[suite][name] = {"category": b["category"]}

    row = {}

    # --- MIR Direct ---
    if "mir" in engines:
        print(f"  MIR      ", end="", flush=True)
        peak, ok = mem_measure_n(f"{LAMBDA_EXE} run {ls_path}", num_runs, timeout_s)
        results[suite][name]["mir"] = peak
        row["mir"] = peak
        print(f" peak={fmt_mem(peak)}" if ok else " failed")

    # --- C2MIR ---
    if "c2mir" in engines:
        print(f"  C2MIR    ", end="", flush=True)
        peak, ok = mem_measure_n(f"{LAMBDA_EXE} run --c2mir {ls_path}", num_runs, timeout_s)
        results[suite][name]["c2mir"] = peak
        row["c2mir"] = peak
        print(f" peak={fmt_mem(peak)}" if ok else " failed")

    if not is_js:
        bundle_path = js_path.replace("2.js", "2_bundle.js") if js_path else None
        standalone_js = bundle_path if (suite == "awfy" and bundle_path and os.path.exists(bundle_path)) else js_path

        # --- LambdaJS ---
        if "lambdajs" in engines:
            if standalone_js and os.path.exists(standalone_js):
                print(f"  LambdaJS ", end="", flush=True)
                peak, ok = mem_measure_n(f"{LAMBDA_EXE} js {standalone_js}", num_runs, timeout_s)
                results[suite][name]["lambdajs"] = peak
                row["lambdajs"] = peak
                print(f" peak={fmt_mem(peak)}" if ok else " failed")
            else:
                results[suite][name]["lambdajs"] = None
                row["lambdajs"] = None
                print(f"  LambdaJS  ---")

        # --- QuickJS ---
        if "quickjs" in engines:
            if standalone_js and os.path.exists(standalone_js):
                print(f"  QuickJS  ", end="", flush=True)
                wrapper = make_qjs_wrapper(standalone_js)
                peak, ok = mem_measure_n(f"{QJS_EXE} --std -m {wrapper}", num_runs, timeout_s)
                results[suite][name]["quickjs"] = peak
                row["quickjs"] = peak
                print(f" peak={fmt_mem(peak)}" if ok else " failed")
            else:
                results[suite][name]["quickjs"] = None
                row["quickjs"] = None
                print(f"  QuickJS   ---")

        # --- Node.js ---
        if "nodejs" in engines:
            if js_path and os.path.exists(js_path):
                print(f"  Node.js  ", end="", flush=True)
                peak, ok = mem_measure_n(f"{NODE_EXE} {js_path}", num_runs, timeout_s)
                results[suite][name]["nodejs"] = peak
                row["nodejs"] = peak
                print(f" peak={fmt_mem(peak)}" if ok else " failed")
            else:
                results[suite][name]["nodejs"] = None
                row["nodejs"] = None
                print(f"  Node.js   ---")

        # --- Python ---
        if "python" in engines:
            if suite == "awfy":
                print(f"  Python   ", end="", flush=True)
                cmd = mem_make_awfy_python_cmd(name)
                peak, ok = mem_measure_n(cmd, num_runs, timeout_s)
                results[suite][name]["python"] = peak
                row["python"] = peak
                print(f" peak={fmt_mem(peak)}" if ok else " failed")
            elif py_path and os.path.exists(py_path):
                print(f"  Python   ", end="", flush=True)
                peak, ok = mem_measure_n(f"{PYTHON_EXE} {py_path}", num_runs, timeout_s)
                results[suite][name]["python"] = peak
                row["python"] = peak
                print(f" peak={fmt_mem(peak)}" if ok else " failed")
            else:
                results[suite][name]["python"] = None
                row["python"] = None
                print(f"  Python    ---")
    else:
        # JetStream suite
        if "lambdajs" in engines:
            results[suite][name]["lambdajs"] = None
            row["lambdajs"] = None
            print(f"  LambdaJS  ---")

        if "quickjs" in engines:
            results[suite][name]["quickjs"] = None
            row["quickjs"] = None
            print(f"  QuickJS   ---")

        if "nodejs" in engines:
            if ref_js and os.path.exists(ref_js):
                print(f"  Node.js  ", end="", flush=True)
                wrapper = make_jetstream_node_wrapper(name, ref_js)
                peak, ok = mem_measure_n(f"{NODE_EXE} {wrapper}", num_runs, timeout_s)
                results[suite][name]["nodejs"] = peak
                row["nodejs"] = peak
                print(f" peak={fmt_mem(peak)}" if ok else " failed")
            else:
                results[suite][name]["nodejs"] = None
                row["nodejs"] = None
                print(f"  Node.js   ---")

        if "python" in engines:
            if py_path and os.path.exists(py_path):
                print(f"  Python   ", end="", flush=True)
                peak, ok = mem_measure_n(f"{PYTHON_EXE} {py_path}", num_runs, timeout_s)
                results[suite][name]["python"] = peak
                row["python"] = peak
                print(f" peak={fmt_mem(peak)}" if ok else " failed")
            else:
                results[suite][name]["python"] = None
                row["python"] = None
                print(f"  Python    ---")

    return row


def run_memory_mode(benchmarks, engines, num_runs, timeout_s, no_save):
    """Execute MEMORY mode: measure peak RSS across engines."""
    results = {}

    current_suite = None
    summary = []
    for b in benchmarks:
        if b["suite"] != current_suite:
            current_suite = b["suite"]
            count = sum(1 for x in benchmarks if x["suite"] == current_suite)
            print(f"\n{'=' * 70}")
            print(f"  Suite: {current_suite.upper()} ({count} benchmarks)")
            print(f"{'=' * 70}")

        print(f"\n  {b['name']} ({b['category']}):")
        if not os.path.exists(b["ls_path"]):
            print(f"    SKIPPED (file not found: {b['ls_path']})")
            continue

        row = mem_run_single(b, engines, num_runs, timeout_s, results)
        summary.append((b["suite"], b["name"], row))

    # Summary table
    print(f"\n{'=' * 100}")
    print(f"MEMORY SUMMARY (peak RSS, median of {num_runs} run{'s' if num_runs > 1 else ''})")
    print(f"{'=' * 100}")

    hdr = f"  {'Suite/Bench':24s}"
    for eng in engines:
        hdr += f" {ENGINE_LABELS[eng]:>10s}"
    hdr += f" {'MIR/Node':>10s} {'MIR/Py':>10s}"
    print(hdr)
    print(f"  {'-' * (24 + 12 * len(engines) + 22)}")

    all_mir_node, all_mir_py = [], []
    for suite, name, row in summary:
        line = f"  {suite + '/' + name:24s}"
        for eng in engines:
            val = row.get(eng)
            line += f" {fmt_mem(val):>10s}"
        mir_val = row.get("mir")
        node_val = row.get("nodejs")
        py_val = row.get("python")
        if mir_val and node_val:
            r = mir_val / node_val
            all_mir_node.append(r)
            line += f" {r:>9.2f}x"
        else:
            line += f" {'---':>10s}"
        if mir_val and py_val:
            r = mir_val / py_val
            all_mir_py.append(r)
            line += f" {r:>9.2f}x"
        else:
            line += f" {'---':>10s}"
        print(line)

    if all_mir_node:
        gm = geo_mean(all_mir_node)
        less = sum(1 for r in all_mir_node if r < 1.0)
        print(f"\n  Geo mean MIR/Node.js: {gm:.2f}x  (Lambda uses less: {less}/{len(all_mir_node)})")
    if all_mir_py:
        gm = geo_mean(all_mir_py)
        less = sum(1 for r in all_mir_py if r < 1.0)
        print(f"  Geo mean MIR/Python:  {gm:.2f}x  (Lambda uses less: {less}/{len(all_mir_py)})")

    # Per-engine average
    print(f"\n{'=' * 80}")
    print(f"PER-ENGINE AVERAGE PEAK MEMORY (across measured benchmarks)")
    print(f"{'=' * 80}")

    engine_totals = {eng: [] for eng in engines}
    for _, _, row in summary:
        for eng in engines:
            val = row.get(eng)
            if val is not None:
                engine_totals[eng].append(val)

    print(f"\n{'Engine':12s} {'Count':>6s} {'Average':>12s} {'Median':>12s} {'Min':>12s} {'Max':>12s}")
    print("-" * 70)
    for eng in engines:
        vals = engine_totals[eng]
        if not vals:
            print(f"{ENGINE_LABELS[eng]:12s} {'0':>6s} {'---':>12s} {'---':>12s} {'---':>12s} {'---':>12s}")
            continue
        vals_sorted = sorted(vals)
        avg = sum(vals) / len(vals)
        median = vals_sorted[len(vals_sorted) // 2]
        mn = vals_sorted[0]
        mx = vals_sorted[-1]
        print(f"{ENGINE_LABELS[eng]:12s} {len(vals):>6d} {fmt_mem(int(avg)):>12s} {fmt_mem(median):>12s} {fmt_mem(mn):>12s} {fmt_mem(mx):>12s}")

    # Save JSON
    if not no_save:
        json_results = {}
        for suite_name in results:
            json_results[suite_name] = {}
            for bench_name in results[suite_name]:
                data = results[suite_name][bench_name]
                json_results[suite_name][bench_name] = {"category": data.get("category", "")}
                for eng in ALL_ENGINES:
                    val = data.get(eng)
                    json_results[suite_name][bench_name][eng] = fmt_mem_mb(val)
                json_results[suite_name][bench_name]["_raw_bytes"] = {
                    eng: data.get(eng) for eng in ALL_ENGINES
                }
        with open(MEMORY_JSON_PATH, "w") as f:
            json.dump(json_results, f, indent=2, default=str)
        print(f"\nResults saved to {MEMORY_JSON_PATH}")
    else:
        print(f"\n--no-save: results NOT written to JSON")

    print(f"\nDone! {len(summary)} benchmarks measured.")


# ============================================================
# MIR-VS-C mode — MIR Direct vs C2MIR transpiler comparison
# ============================================================

def mirc_run_benchmark(cmd, num_runs, timeout_s):
    """Run cmd N times, return list of wall-clock times in microseconds."""
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
            return []
        if i == 0 and "FAIL" in (r.stdout or ""):
            print(f" [FAIL: {r.stdout.strip()[:60]}]", end="", flush=True)
            return []
        print(".", end="", flush=True)
    return times


def med_us(times):
    """Median of a list of microsecond times."""
    if not times:
        return 0
    s = sorted(times)
    return int(s[len(s) // 2])


def run_mirc_mode(benchmarks, num_runs, timeout_s, no_save, include_typed):
    """Execute MIR-VS-C mode: compare MIR Direct vs C2MIR transpiler."""
    timeout_s = max(timeout_s, 300)  # MIR-vs-C can be slow

    # Collect benchmark phases: list of (label, suite, name, category, script)
    phases_c2mir = []
    phases_mir = []

    for b in benchmarks:
        suite = b["suite"]
        name = b["name"]
        category = b["category"]
        ls_path = b["ls_path"]

        # For R7RS, include both untyped (.ls) and typed (2.ls), for AWFY use untyped (.ls)
        if suite == "r7rs":
            # Untyped: fib.ls, typed: fib2.ls
            untyped = ls_path.replace("2.ls", ".ls")  # fib2.ls -> fib.ls
            typed = ls_path  # fib2.ls
            phases_c2mir.append((f"{suite}_untyped", suite, name, category, untyped))
            phases_mir.append((f"{suite}_untyped", suite, name, category, untyped))
            if include_typed:
                phases_c2mir.append((f"{suite}_typed", suite, name, category, typed))
                phases_mir.append((f"{suite}_typed", suite, name, category, typed))
        elif suite in ("awfy", "beng", "kostya", "larceny"):
            # Use untyped (.ls) for AWFY, and whatever ls_path for others
            if suite == "awfy":
                untyped = ls_path.replace("2.ls", ".ls")
            else:
                untyped = ls_path
            phases_c2mir.append((suite, suite, name, category, untyped))
            phases_mir.append((suite, suite, name, category, untyped))
        elif suite == "jetstream":
            phases_c2mir.append((suite, suite, name, category, ls_path))
            phases_mir.append((suite, suite, name, category, ls_path))

    if not phases_c2mir:
        print("No benchmarks to run in MIR-vs-C mode.")
        return

    print(f"\033[1mMIR Direct vs C2MIR Transpiler Benchmark Comparison\033[0m")
    print("=" * 80)
    print(f"  Benchmarks : {len(phases_c2mir)}")
    print(f"  Runs each  : {num_runs}")
    print(f"  Timeout    : {timeout_s}s")
    print(f"  Timing     : wall-clock (includes startup + JIT compile + execute)")
    print()

    # Group by label for display
    from collections import OrderedDict
    label_groups = OrderedDict()
    for label, suite, name, category, script in phases_c2mir:
        if label not in label_groups:
            label_groups[label] = []
        label_groups[label].append((suite, name, category, script))

    # Run C2MIR phase
    c2mir_results = {}  # key = (label, name) -> median_us
    for label, items in label_groups.items():
        print(f"\033[1m{label.upper()} — C2MIR ({num_runs} runs)\033[0m")
        print("-" * 80)
        for suite, name, category, script in items:
            if not os.path.exists(script):
                print(f"  {name:14s} SKIPPED (not found: {script})")
                continue
            print(f"  {name:14s}", end="", flush=True)
            times = mirc_run_benchmark(f"{LAMBDA_EXE} run --c2mir {script}", num_runs, timeout_s)
            m = med_us(times)
            c2mir_results[(label, name)] = m
            print(f"  median={fmt_us(m):>10s}")
        print()

    # Run MIR Direct phase
    mir_results = {}
    for label, items in label_groups.items():
        print(f"\033[1m{label.upper()} — MIR Direct ({num_runs} runs)\033[0m")
        print("-" * 80)
        for suite, name, category, script in items:
            if not os.path.exists(script):
                print(f"  {name:14s} SKIPPED")
                continue
            print(f"  {name:14s}", end="", flush=True)
            times = mirc_run_benchmark(f"{LAMBDA_EXE} run {script}", num_runs, timeout_s)
            m = med_us(times)
            mir_results[(label, name)] = m
            print(f"  median={fmt_us(m):>10s}")
        print()

    # Summary tables per label group
    csv_rows = []
    all_ratios = []

    for label, items in label_groups.items():
        print(f"\033[1m{label.upper()} RESULTS\033[0m")
        print(f"{'Benchmark':14s} {'Category':10s} {'C2MIR':>12s} {'MIR Direct':>12s} {'MIR/C':>8s}")
        print(f"{'-' * 14} {'-' * 10} {'-' * 12} {'-' * 12} {'-' * 8}")

        group_ratios = []
        for suite, name, category, script in items:
            c = c2mir_results.get((label, name), 0)
            m = mir_results.get((label, name), 0)
            if c > 0 and m > 0:
                ratio = m / c
                rs = f"{ratio:.2f}x"
                group_ratios.append(ratio)
                all_ratios.append(ratio)
            else:
                ratio = 0
                rs = "N/A"
            print(f"{name:14s} {category:10s} {fmt_us(c):>12s} {fmt_us(m):>12s} {rs:>8s}")
            csv_rows.append((label, name, category, c, m, ratio))

        if group_ratios:
            gm = geo_mean(group_ratios)
            print(f"\n  Geometric mean (MIR/C): {gm:.2f}x  {'(MIR faster)' if gm < 1 else '(C faster)'}")
        print()

    # Overall
    print(f"\033[1m{'=' * 80}\033[0m")
    print(f"\033[1m{'OVERALL SUMMARY':^80s}\033[0m")
    print(f"\033[1m{'=' * 80}\033[0m\n")

    for label in label_groups:
        group_ratios = [r for (lbl, n), r in zip(
            [(l, n) for l, _, n, _, _ in phases_c2mir],
            [mir_results.get((l, n), 0) / c2mir_results.get((l, n), 1)
             for l, _, n, _, _ in phases_c2mir if c2mir_results.get((l, n), 0) > 0 and mir_results.get((l, n), 0) > 0]
        ) if lbl == label]
        # Recompute cleanly
        ratios = []
        for suite, name, category, script in label_groups[label]:
            c = c2mir_results.get((label, name), 0)
            m = mir_results.get((label, name), 0)
            if c > 0 and m > 0:
                ratios.append(m / c)
        if ratios:
            gm = geo_mean(ratios)
            print(f"  {label:20s} MIR/C geomean: {gm:.2f}x")

    if all_ratios:
        gm = geo_mean(all_ratios)
        print(f"  {'Overall':20s} MIR/C geomean: {gm:.2f}x")
        print(f"\n  Ratio < 1.0 = MIR Direct is faster")
        print(f"  Ratio > 1.0 = C2MIR is faster")

    # Save CSV
    if not no_save:
        os.makedirs("temp", exist_ok=True)
        with open(MIR_VS_C_CSV_PATH, "w") as f:
            f.write(f"# MIR vs C Benchmark Results {time.strftime('%Y-%m-%d %H:%M:%S')}, runs={num_runs}\n")
            f.write("suite,benchmark,category,c2mir_us,mir_direct_us,ratio\n")
            for label, name, category, c, m, ratio in csv_rows:
                f.write(f"{label},{name},{category},{c},{m},{ratio:.4f}\n")
        print(f"\nData saved to {MIR_VS_C_CSV_PATH}")
    else:
        print(f"\n--no-save: CSV NOT written")

    print(f"\nDone! {len(phases_c2mir)} benchmark configurations compared.")


# ============================================================
# Main entry point
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        description="Unified benchmark runner — time, memory, and MIR-vs-C comparison",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Modes:
  time      Measure execution time across all engines (default)
  memory    Measure peak resident set size (RSS) via /usr/bin/time
  mir-vs-c  Compare MIR Direct vs C2MIR transpiler (wall-clock μs)

Examples:
  %(prog)s                                 Run ALL time benchmarks
  %(prog)s -m memory                       Measure peak RSS for all
  %(prog)s -m mir-vs-c                     MIR Direct vs C2MIR comparison
  %(prog)s -m mir-vs-c --typed             Include typed R7RS variants
  %(prog)s -s jetstream                    Run all JetStream benchmarks
  %(prog)s -s awfy,beng                    Run AWFY + BENG suites
  %(prog)s -b nbody,richards              Run nbody & richards in all suites
  %(prog)s -s awfy -b mandelbrot,nbody     AWFY mandelbrot + nbody only
  %(prog)s -b deltablue -n 5               5 runs per engine (default: 3)
  %(prog)s -e mir,nodejs                   Only MIR and Node.js engines
  %(prog)s --list                          List all available benchmarks
  %(prog)s --dry-run -b nbody              Show what would run without running
""")
    parser.add_argument("-m", "--mode", type=str, default="time",
                        choices=["time", "memory", "mir-vs-c"],
                        help="Benchmark mode: time (default), memory, mir-vs-c")
    parser.add_argument("-s", "--suite", type=str, default=None,
                        help="Comma-separated suite name(s) to filter (substring match)")
    parser.add_argument("-b", "--bench", type=str, default=None,
                        help="Comma-separated benchmark name(s) to filter (substring match)")
    parser.add_argument("-n", "--runs", type=int, default=None,
                        help="Number of runs per benchmark per engine (default: 3 for time, 1 for memory)")
    parser.add_argument("-e", "--engines", type=str, default=None,
                        help=f"Comma-separated engine(s): {','.join(ALL_ENGINES)} (default: all)")
    parser.add_argument("-t", "--timeout", type=int, default=DEFAULT_TIMEOUT_S,
                        help=f"Timeout per single run in seconds (default: {DEFAULT_TIMEOUT_S})")
    parser.add_argument("--typed", action="store_true",
                        help="Include typed R7RS variants (mir-vs-c mode only)")
    parser.add_argument("--list", action="store_true",
                        help="List all available benchmarks and exit")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show what would be run without actually running")
    parser.add_argument("--no-save", action="store_true",
                        help="Don't save results to JSON/CSV")

    args = parser.parse_args()

    # --- List mode ---
    if args.list:
        list_benchmarks()
        return

    # Parse filters
    suite_filters = [s.strip() for s in args.suite.split(",")] if args.suite else None
    bench_filters = [b.strip() for b in args.bench.split(",")] if args.bench else None

    mode = args.mode
    timeout_s = args.timeout

    # Default runs depends on mode
    if args.runs is not None:
        num_runs = args.runs
    else:
        num_runs = 1 if mode == "memory" else 3

    # Engine handling
    if mode == "mir-vs-c":
        engines = ["mir", "c2mir"]  # Only these two are relevant
    elif args.engines:
        engines = [e.strip().lower() for e in args.engines.split(",")]
        for eng in engines:
            if eng not in ALL_ENGINES:
                print(f"Unknown engine: {eng}. Available: {', '.join(ALL_ENGINES)}")
                sys.exit(1)
    else:
        engines = ALL_ENGINES

    # Build benchmark list
    benchmarks = build_benchmark_list(suite_filters, bench_filters)

    if not benchmarks:
        print("No benchmarks match the given filters.")
        if suite_filters:
            print(f"  Suite filter: {suite_filters}")
        if bench_filters:
            print(f"  Bench filter: {bench_filters}")
        print("\nUse --list to see all available benchmarks.")
        sys.exit(1)

    # --- Dry-run mode ---
    if args.dry_run:
        mode_label = {"time": "TIME", "memory": "MEMORY", "mir-vs-c": "MIR-vs-C"}[mode]
        eng_count = len(engines) if mode != "mir-vs-c" else 2
        print(f"DRY RUN [{mode_label}] — {len(benchmarks)} benchmark(s) x {eng_count} engine(s) x {num_runs} run(s)\n")
        print(f"  Mode    : {mode_label}")
        print(f"  Engines : {', '.join(ENGINE_LABELS.get(e, e) for e in engines)}")
        print(f"  Timeout : {timeout_s}s per run")
        if mode == "mir-vs-c" and args.typed:
            print(f"  Typed   : yes (R7RS typed variants included)")
        if mode == "time":
            print(f"  Output  : {TIME_JSON_PATH}")
        elif mode == "memory":
            print(f"  Output  : {MEMORY_JSON_PATH}")
        elif mode == "mir-vs-c":
            print(f"  Output  : {MIR_VS_C_CSV_PATH}")

        current_suite = None
        for b in benchmarks:
            if b["suite"] != current_suite:
                current_suite = b["suite"]
                print(f"\n  {current_suite.upper()}:")
            note = " (JetStream)" if b["is_jetstream"] else ""
            print(f"    {b['name']:20s} {b['category']:12s}{note}")
        print(f"\n  Total: {len(benchmarks)} benchmarks")
        return

    # --- Print header ---
    suite_desc = f"suites={args.suite}" if args.suite else "all suites"
    bench_desc = f"benchmarks={args.bench}" if args.bench else "all benchmarks"
    mode_label = {"time": "TIME", "memory": "MEMORY", "mir-vs-c": "MIR-vs-C"}[mode]
    print(f"{'=' * 70}")
    print(f"Benchmark Runner [{mode_label}] — {len(benchmarks)} benchmark(s)")
    print(f"{'=' * 70}")
    print(f"  Mode      : {mode_label}")
    print(f"  Filter    : {suite_desc}, {bench_desc}")
    print(f"  Engines   : {', '.join(ENGINE_LABELS.get(e, e) for e in engines)}")
    print(f"  Runs      : {num_runs}")
    print(f"  Timeout   : {timeout_s}s")
    print(f"  Platform  : {platform.system()} {platform.machine()}")
    print(f"  Lambda    : {LAMBDA_EXE}")

    # Get versions for non-mir-vs-c modes
    if mode != "mir-vs-c":
        py_ver = subprocess.run(f"{PYTHON_EXE} --version", shell=True, capture_output=True, text=True).stdout.strip()
        node_ver = subprocess.run(f"{NODE_EXE} --version", shell=True, capture_output=True, text=True).stdout.strip()
        print(f"  Node.js   : {node_ver}")
        print(f"  Python    : {py_ver}")
    print()

    # --- Dispatch to mode ---
    if mode == "time":
        run_time_mode(benchmarks, engines, num_runs, timeout_s, args.no_save)
    elif mode == "memory":
        run_memory_mode(benchmarks, engines, num_runs, timeout_s, args.no_save)
    elif mode == "mir-vs-c":
        run_mirc_mode(benchmarks, num_runs, timeout_s, args.no_save, args.typed)


if __name__ == "__main__":
    main()
