#!/usr/bin/env python3
"""Peak Memory Usage Benchmark Runner — 6 suites × 6 engines

Engines: MIR Direct, C2MIR, Lambda JS, QuickJS, Node.js, Python
Suites:  r7rs, awfy, beng, kostya, larceny, jetstream

Measures peak resident set size (RSS) using /usr/bin/time on macOS/Linux.

Usage: python3 test/benchmark/run_memory_benchmarks.py [num_runs]
"""

import subprocess
import sys
import os
import re
import json
import math
import platform

NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 1
TIMEOUT_S = 180  # per-run timeout

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../.."))

LAMBDA_EXE = "./lambda.exe"
NODE_EXE = "node"
QJS_EXE = "qjs"
PYTHON_EXE = "python3"

IS_MACOS = platform.system() == "Darwin"

# ============================================================
# Benchmark definitions (same as run_all_benchmarks_v3.py)
# ============================================================

R7RS = [
    ("fib",     "recursive", "test/benchmark/r7rs/fib2.ls",     "test/benchmark/r7rs/fib2.js",     "test/benchmark/r7rs/python/fib.py"),
    ("fibfp",   "recursive", "test/benchmark/r7rs/fibfp2.ls",   "test/benchmark/r7rs/fibfp2.js",   "test/benchmark/r7rs/python/fibfp.py"),
    ("tak",     "recursive", "test/benchmark/r7rs/tak2.ls",     "test/benchmark/r7rs/tak2.js",     "test/benchmark/r7rs/python/tak.py"),
    ("cpstak",  "closure",   "test/benchmark/r7rs/cpstak2.ls",  "test/benchmark/r7rs/cpstak2.js",  "test/benchmark/r7rs/python/cpstak.py"),
    ("sum",     "iterative", "test/benchmark/r7rs/sum2.ls",     "test/benchmark/r7rs/sum2.js",     "test/benchmark/r7rs/python/sum.py"),
    ("sumfp",   "iterative", "test/benchmark/r7rs/sumfp2.ls",   "test/benchmark/r7rs/sumfp2.js",   "test/benchmark/r7rs/python/sumfp.py"),
    ("nqueens", "backtrack",  "test/benchmark/r7rs/nqueens2.ls", "test/benchmark/r7rs/nqueens2.js", "test/benchmark/r7rs/python/nqueens.py"),
    ("fft",     "numeric",   "test/benchmark/r7rs/fft2.ls",     "test/benchmark/r7rs/fft2.js",     "test/benchmark/r7rs/python/fft.py"),
    ("mbrot",   "numeric",   "test/benchmark/r7rs/mbrot2.ls",   "test/benchmark/r7rs/mbrot2.js",   "test/benchmark/r7rs/python/mbrot.py"),
    ("ack",     "recursive", "test/benchmark/r7rs/ack2.ls",     "test/benchmark/r7rs/ack2.js",     "test/benchmark/r7rs/python/ack.py"),
]

AWFY = [
    ("sieve",       "micro",   "test/benchmark/awfy/sieve2.ls",      "test/benchmark/awfy/sieve2.js",      "test/benchmark/awfy/python/sieve.py"),
    ("permute",     "micro",   "test/benchmark/awfy/permute2.ls",    "test/benchmark/awfy/permute2.js",    "test/benchmark/awfy/python/permute.py"),
    ("queens",      "micro",   "test/benchmark/awfy/queens2.ls",     "test/benchmark/awfy/queens2.js",     "test/benchmark/awfy/python/queens.py"),
    ("towers",      "micro",   "test/benchmark/awfy/towers2.ls",     "test/benchmark/awfy/towers2.js",     "test/benchmark/awfy/python/towers.py"),
    ("bounce",      "micro",   "test/benchmark/awfy/bounce2.ls",     "test/benchmark/awfy/bounce2.js",     "test/benchmark/awfy/python/bounce.py"),
    ("list",        "micro",   "test/benchmark/awfy/list2.ls",       "test/benchmark/awfy/list2.js",       "test/benchmark/awfy/python/list.py"),
    ("storage",     "micro",   "test/benchmark/awfy/storage2.ls",    "test/benchmark/awfy/storage2.js",    "test/benchmark/awfy/python/storage.py"),
    ("mandelbrot",  "compute", "test/benchmark/awfy/mandelbrot2.ls", "test/benchmark/awfy/mandelbrot2.js", "test/benchmark/awfy/python/mandelbrot.py"),
    ("nbody",       "compute", "test/benchmark/awfy/nbody2.ls",      "test/benchmark/awfy/nbody2.js",      "test/benchmark/awfy/python/nbody.py"),
    ("richards",    "macro",   "test/benchmark/awfy/richards2.ls",   "test/benchmark/awfy/richards2.js",   "test/benchmark/awfy/python/richards.py"),
    ("json",        "macro",   "test/benchmark/awfy/json2.ls",       "test/benchmark/awfy/json2.js",       "test/benchmark/awfy/python/json.py"),
    ("deltablue",   "macro",   "test/benchmark/awfy/deltablue2.ls",  "test/benchmark/awfy/deltablue2.js",  "test/benchmark/awfy/python/deltablue.py"),
    ("havlak",      "macro",   "test/benchmark/awfy/havlak2.ls",     "test/benchmark/awfy/havlak2.js",     "test/benchmark/awfy/python/havlak.py"),
    ("cd",          "macro",   "test/benchmark/awfy/cd2.ls",         "test/benchmark/awfy/cd2.js",         "test/benchmark/awfy/python/cd.py"),
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
    ("nbody",         "numeric",     "test/benchmark/jetstream/nbody.ls"),
    ("cube3d",        "3d",          "test/benchmark/jetstream/cube3d.ls"),
    ("navier_stokes", "numeric",     "test/benchmark/jetstream/navier_stokes.ls"),
    ("richards",      "macro",       "test/benchmark/jetstream/richards.ls"),
    ("splay",         "data",        "test/benchmark/jetstream/splay.ls"),
    ("deltablue",     "macro",       "test/benchmark/jetstream/deltablue.ls"),
    ("hashmap",       "data",        "test/benchmark/jetstream/hashmap.ls"),
    ("crypto_sha1",   "crypto",      "test/benchmark/jetstream/crypto_sha1.ls"),
    ("raytrace3d",    "3d",          "test/benchmark/jetstream/raytrace3d.ls"),
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

STANDARD_SUITES = [
    ("r7rs",    R7RS),
    ("awfy",    AWFY),
    ("beng",    BENG),
    ("kostya",  KOSTYA),
    ("larceny", LARCENY),
]

# QuickJS polyfill (same as v3 runner)
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

# AWFY Python harness config
AWFY_PY_CONFIG = {
    "sieve": 3000, "permute": 1500, "queens": 1500, "towers": 600,
    "bounce": 1500, "list": 1500, "storage": 1000,
    "mandelbrot": 1, "nbody": 1, "richards": 1,
    "json": 1, "deltablue": 1, "havlak": 1, "cd": 1,
}

ENGINES = ["mir", "c2mir", "lambdajs", "quickjs", "nodejs", "python"]
ENGINE_LABELS = {
    "mir": "MIR", "c2mir": "C2MIR", "lambdajs": "LambdaJS",
    "quickjs": "QuickJS", "nodejs": "Node.js", "python": "Python"
}


# ============================================================
# Memory measurement helpers
# ============================================================

def parse_peak_rss_bytes(stderr_text):
    """Parse peak RSS from /usr/bin/time output.

    macOS (/usr/bin/time -l): reports bytes, e.g.
        '12345678  maximum resident set size'
    Linux (/usr/bin/time -v): reports kilobytes, e.g.
        'Maximum resident set size (kbytes): 12345'

    Returns peak RSS in bytes, or None on failure.
    """
    if IS_MACOS:
        # macOS: "<number>  maximum resident set size"
        m = re.search(r"(\d+)\s+maximum resident set size", stderr_text)
        if m:
            return int(m.group(1))
    else:
        # Linux: "Maximum resident set size (kbytes): <number>"
        m = re.search(r"Maximum resident set size.*?:\s*(\d+)", stderr_text)
        if m:
            return int(m.group(1)) * 1024  # convert KB to bytes
    return None


def measure_peak_memory(cmd, timeout=TIMEOUT_S):
    """Run a command and measure its peak RSS.

    Returns (peak_rss_bytes, success).
    """
    if IS_MACOS:
        timed_cmd = f"/usr/bin/time -l {cmd}"
    else:
        timed_cmd = f"/usr/bin/time -v {cmd}"

    try:
        r = subprocess.run(
            timed_cmd, shell=True, capture_output=True, text=True, timeout=timeout
        )
    except subprocess.TimeoutExpired:
        return (None, False)

    if r.returncode != 0:
        return (None, False)

    peak = parse_peak_rss_bytes(r.stderr)
    return (peak, peak is not None)


def measure_peak_memory_n(cmd, num_runs):
    """Run N times, return (median_peak_rss_bytes, success)."""
    measurements = []
    for _ in range(num_runs):
        peak, ok = measure_peak_memory(cmd)
        if ok and peak is not None:
            measurements.append(peak)
        print(".", end="", flush=True)

    if not measurements:
        return (None, False)

    measurements.sort()
    median = measurements[len(measurements) // 2]
    return (median, True)


def make_qjs_wrapper(js_path):
    """Create a QuickJS-compatible wrapper for a Node.js script."""
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
    """Create Node.js wrapper for JetStream benchmark."""
    os.makedirs("temp", exist_ok=True)
    wrapper = os.path.join("temp", f"_node_bench_{bench_name}.js")
    with open(js_path) as f:
        code = f.read()
    with open(wrapper, "w") as f:
        f.write(code)
        f.write("\n")
        f.write("const b = new Benchmark();\n")
        f.write("b.runIteration();\n")
    return wrapper


def make_awfy_python_cmd(bench_name):
    """Build the command for AWFY Python harness."""
    inner = AWFY_PY_CONFIG.get(bench_name, 1)
    py_dir = "test/benchmark/awfy/python"
    return f"cd {py_dir} && {PYTHON_EXE} harness.py {bench_name.capitalize()} 1 {inner}"


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
    """Format RSS bytes as MB for CSV/JSON."""
    if rss_bytes is None:
        return None
    return round(rss_bytes / (1024 * 1024), 2)


def geo_mean(values):
    """Geometric mean of a list of positive numbers."""
    vals = [v for v in values if v is not None and v > 0]
    if not vals:
        return None
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


# ============================================================
# Main
# ============================================================
print(f"{'='*100}")
print(f"Peak Memory Usage Benchmark — 6 Suites × 6 Engines")
print(f"{'='*100}")
print(f"  Runs per benchmark : {NUM_RUNS}")
print(f"  Timeout            : {TIMEOUT_S}s")
print(f"  Platform           : {platform.system()} {platform.machine()}")
print(f"  Lambda             : {LAMBDA_EXE}")
print(f"  Node.js            : {NODE_EXE}")
print(f"  QuickJS            : {QJS_EXE}")
print(f"  Python             : {PYTHON_EXE}")
print()

# Verify executables
for exe in [LAMBDA_EXE, NODE_EXE, QJS_EXE, PYTHON_EXE]:
    r = subprocess.run(f"which {exe}", shell=True, capture_output=True)
    if r.returncode != 0 and not os.path.exists(exe):
        print(f"  WARNING: {exe} not found!")

# Get versions
py_ver = subprocess.run(f"{PYTHON_EXE} --version", shell=True, capture_output=True, text=True).stdout.strip()
node_ver = subprocess.run(f"{NODE_EXE} --version", shell=True, capture_output=True, text=True).stdout.strip()
print(f"  Python version     : {py_ver}")
print(f"  Node.js version    : {node_ver}")
print()

# Results storage: results[suite][bench][engine] = peak_rss_bytes
results = {}

# ============================================================
# Run standard suites
# ============================================================
for suite_name, benchmarks in STANDARD_SUITES:
    print(f"\n{'='*100}")
    print(f"  Suite: {suite_name.upper()} ({len(benchmarks)} benchmarks)")
    print(f"{'='*100}")
    results[suite_name] = {}

    for entry in benchmarks:
        bench_name, category, ls_path, js_path, py_path = entry
        results[suite_name][bench_name] = {"category": category}
        print(f"\n  {bench_name} ({category}):")

        if not os.path.exists(ls_path):
            print(f"    SKIPPED (file not found: {ls_path})")
            continue

        # --- MIR Direct ---
        print(f"    MIR      ", end="", flush=True)
        cmd = f"{LAMBDA_EXE} run {ls_path}"
        peak, ok = measure_peak_memory_n(cmd, NUM_RUNS)
        results[suite_name][bench_name]["mir"] = peak
        print(f" peak={fmt_mem(peak)}" if ok else " failed")

        # --- C2MIR ---
        print(f"    C2MIR    ", end="", flush=True)
        cmd = f"{LAMBDA_EXE} run --c2mir {ls_path}"
        peak, ok = measure_peak_memory_n(cmd, NUM_RUNS)
        results[suite_name][bench_name]["c2mir"] = peak
        print(f" peak={fmt_mem(peak)}" if ok else " failed")

        # For AWFY, use bundle files for LambdaJS/QuickJS
        bundle_path = js_path.replace("2.js", "2_bundle.js") if js_path else None
        standalone_js = bundle_path if (suite_name == "awfy" and bundle_path and os.path.exists(bundle_path)) else js_path

        # --- Lambda JS engine ---
        if standalone_js and os.path.exists(standalone_js):
            print(f"    LambdaJS ", end="", flush=True)
            cmd = f"{LAMBDA_EXE} js {standalone_js}"
            peak, ok = measure_peak_memory_n(cmd, NUM_RUNS)
            results[suite_name][bench_name]["lambdajs"] = peak
            print(f" peak={fmt_mem(peak)}" if ok else " failed")
        else:
            results[suite_name][bench_name]["lambdajs"] = None
            print(f"    LambdaJS  ---")

        # --- QuickJS ---
        if standalone_js and os.path.exists(standalone_js):
            print(f"    QuickJS  ", end="", flush=True)
            wrapper = make_qjs_wrapper(standalone_js)
            cmd = f"{QJS_EXE} --std -m {wrapper}"
            peak, ok = measure_peak_memory_n(cmd, NUM_RUNS)
            results[suite_name][bench_name]["quickjs"] = peak
            print(f" peak={fmt_mem(peak)}" if ok else " failed")
        else:
            results[suite_name][bench_name]["quickjs"] = None
            print(f"    QuickJS   ---")

        # --- Node.js ---
        if js_path and os.path.exists(js_path):
            print(f"    Node.js  ", end="", flush=True)
            cmd = f"{NODE_EXE} {js_path}"
            peak, ok = measure_peak_memory_n(cmd, NUM_RUNS)
            results[suite_name][bench_name]["nodejs"] = peak
            print(f" peak={fmt_mem(peak)}" if ok else " failed")
        else:
            results[suite_name][bench_name]["nodejs"] = None
            print(f"    Node.js   ---")

        # --- Python ---
        if suite_name == "awfy":
            print(f"    Python   ", end="", flush=True)
            cmd = make_awfy_python_cmd(bench_name)
            peak, ok = measure_peak_memory_n(cmd, NUM_RUNS)
            results[suite_name][bench_name]["python"] = peak
            print(f" peak={fmt_mem(peak)}" if ok else " failed")
        elif py_path and os.path.exists(py_path):
            print(f"    Python   ", end="", flush=True)
            cmd = f"{PYTHON_EXE} {py_path}"
            peak, ok = measure_peak_memory_n(cmd, NUM_RUNS)
            results[suite_name][bench_name]["python"] = peak
            print(f" peak={fmt_mem(peak)}" if ok else " failed")
        else:
            results[suite_name][bench_name]["python"] = None
            print(f"    Python    ---")


# ============================================================
# Run JetStream suite
# ============================================================
print(f"\n{'='*100}")
print(f"  Suite: JETSTREAM ({len(JETSTREAM_LS)} benchmarks)")
print(f"{'='*100}")
results["jetstream"] = {}

for bench_name, category, ls_path in JETSTREAM_LS:
    results["jetstream"][bench_name] = {"category": category}
    print(f"\n  {bench_name} ({category}):")

    # --- MIR Direct ---
    if os.path.exists(ls_path):
        print(f"    MIR      ", end="", flush=True)
        cmd = f"{LAMBDA_EXE} run {ls_path}"
        peak, ok = measure_peak_memory_n(cmd, NUM_RUNS)
        results["jetstream"][bench_name]["mir"] = peak
        print(f" peak={fmt_mem(peak)}" if ok else " failed")
    else:
        results["jetstream"][bench_name]["mir"] = None

    # --- C2MIR ---
    if os.path.exists(ls_path):
        print(f"    C2MIR    ", end="", flush=True)
        cmd = f"{LAMBDA_EXE} run --c2mir {ls_path}"
        peak, ok = measure_peak_memory_n(cmd, NUM_RUNS)
        results["jetstream"][bench_name]["c2mir"] = peak
        print(f" peak={fmt_mem(peak)}" if ok else " failed")
    else:
        results["jetstream"][bench_name]["c2mir"] = None

    # --- LambdaJS ---
    results["jetstream"][bench_name]["lambdajs"] = None
    print(f"    LambdaJS  ---")

    # --- QuickJS ---
    results["jetstream"][bench_name]["quickjs"] = None
    print(f"    QuickJS   ---")

    # --- Node.js ---
    js_ref = JETSTREAM_NODE.get(bench_name)
    if js_ref and os.path.exists(js_ref):
        print(f"    Node.js  ", end="", flush=True)
        wrapper = make_jetstream_node_wrapper(bench_name, js_ref)
        cmd = f"{NODE_EXE} {wrapper}"
        peak, ok = measure_peak_memory_n(cmd, NUM_RUNS)
        results["jetstream"][bench_name]["nodejs"] = peak
        print(f" peak={fmt_mem(peak)}" if ok else " failed")
    else:
        results["jetstream"][bench_name]["nodejs"] = None
        print(f"    Node.js   ---")

    # --- Python ---
    results["jetstream"][bench_name]["python"] = None
    print(f"    Python    ---")


# ============================================================
# Results summary tables
# ============================================================
ALL_SUITE_NAMES = ["r7rs", "awfy", "beng", "kostya", "larceny", "jetstream"]

print(f"\n\n{'='*130}")
print(f"PEAK MEMORY USAGE (RSS, median of {NUM_RUNS} run{'s' if NUM_RUNS > 1 else ''})")
print(f"{'='*130}")

for suite_name in ALL_SUITE_NAMES:
    if suite_name not in results:
        continue
    print(f"\n--- {suite_name.upper()} ---")
    hdr = f"{'Benchmark':16s} {'Category':12s}"
    for eng in ENGINES:
        hdr += f" {ENGINE_LABELS[eng]:>10s}"
    hdr += f" {'MIR/Node':>10s} {'MIR/Py':>10s}"
    print(hdr)
    print("-" * (16 + 12 + 12 * len(ENGINES) + 22))

    for bench_name in results[suite_name]:
        data = results[suite_name][bench_name]
        cat = data.get("category", "")
        row = f"{bench_name:16s} {cat:12s}"
        for eng in ENGINES:
            val = data.get(eng)
            row += f" {fmt_mem(val):>10s}"
        # Ratios (lower is better for memory, so MIR/Node < 1 means Lambda uses less)
        mir_val = data.get("mir")
        node_val = data.get("nodejs")
        py_val = data.get("python")
        if mir_val and node_val:
            ratio = mir_val / node_val
            row += f" {ratio:>9.2f}x"
        else:
            row += f" {'---':>10s}"
        if mir_val and py_val:
            ratio = mir_val / py_val
            row += f" {ratio:>9.2f}x"
        else:
            row += f" {'---':>10s}"
        print(row)

    # Per-suite geometric mean ratios
    mir_node_ratios = []
    mir_py_ratios = []
    for bench_name in results[suite_name]:
        data = results[suite_name][bench_name]
        mir_val = data.get("mir")
        node_val = data.get("nodejs")
        py_val = data.get("python")
        if mir_val and node_val and mir_val > 0 and node_val > 0:
            mir_node_ratios.append(mir_val / node_val)
        if mir_val and py_val and mir_val > 0 and py_val > 0:
            mir_py_ratios.append(mir_val / py_val)

    if mir_node_ratios:
        gm = geo_mean(mir_node_ratios)
        less = sum(1 for r in mir_node_ratios if r < 1.0)
        print(f"  Geo mean MIR/Node.js: {gm:.2f}x  (Lambda uses less: {less}/{len(mir_node_ratios)})")
    if mir_py_ratios:
        gm = geo_mean(mir_py_ratios)
        less = sum(1 for r in mir_py_ratios if r < 1.0)
        print(f"  Geo mean MIR/Python:  {gm:.2f}x  (Lambda uses less: {less}/{len(mir_py_ratios)})")


# ============================================================
# Per-engine average memory across all suites
# ============================================================
print(f"\n\n{'='*130}")
print(f"PER-ENGINE AVERAGE PEAK MEMORY (across all benchmarks)")
print(f"{'='*130}")

engine_totals = {eng: [] for eng in ENGINES}
for suite_name in ALL_SUITE_NAMES:
    if suite_name not in results:
        continue
    for bench_name in results[suite_name]:
        data = results[suite_name][bench_name]
        for eng in ENGINES:
            val = data.get(eng)
            if val is not None:
                engine_totals[eng].append(val)

print(f"\n{'Engine':12s} {'Count':>6s} {'Average':>12s} {'Median':>12s} {'Min':>12s} {'Max':>12s}")
print("-" * 70)
for eng in ENGINES:
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


# ============================================================
# Overall summary
# ============================================================
print(f"\n\n{'='*130}")
print(f"OVERALL MEMORY SUMMARY")
print(f"{'='*130}")

print(f"\n{'Suite':12s} {'GeoMean MIR/Node':>18s} {'Lambda<Node':>12s} {'Node<Lambda':>12s} {'GeoMean MIR/Py':>16s} {'Lambda<Py':>12s} {'Py<Lambda':>10s} {'Total':>6s}")
print("-" * 100)

all_mir_node = []
all_mir_py = []
total_benchmarks = 0
total_lw_node = 0
total_nw = 0
total_lw_py = 0
total_pw = 0

for suite_name in ALL_SUITE_NAMES:
    if suite_name not in results:
        continue
    mir_node = []
    mir_py = []
    for bench_name in results[suite_name]:
        data = results[suite_name][bench_name]
        mir_val = data.get("mir")
        node_val = data.get("nodejs")
        py_val = data.get("python")
        if mir_val and node_val and mir_val > 0 and node_val > 0:
            mir_node.append(mir_val / node_val)
        if mir_val and py_val and mir_val > 0 and py_val > 0:
            mir_py.append(mir_val / py_val)

    all_mir_node.extend(mir_node)
    all_mir_py.extend(mir_py)

    gm_n = geo_mean(mir_node) if mir_node else None
    lw_n = sum(1 for r in mir_node if r < 1.0)
    nw = sum(1 for r in mir_node if r >= 1.0)
    gm_p = geo_mean(mir_py) if mir_py else None
    lw_p = sum(1 for r in mir_py if r < 1.0)
    pw = sum(1 for r in mir_py if r >= 1.0)
    total = len(results[suite_name])
    total_benchmarks += total
    total_lw_node += lw_n
    total_nw += nw
    total_lw_py += lw_p
    total_pw += pw

    gm_n_str = f"{gm_n:.2f}x" if gm_n else "---"
    gm_p_str = f"{gm_p:.2f}x" if gm_p else "---"
    print(f"{suite_name.upper():12s} {gm_n_str:>18s} {lw_n:>12d} {nw:>12d} {gm_p_str:>16s} {lw_p:>12d} {pw:>10d} {total:>6d}")

overall_gm_n = geo_mean(all_mir_node) if all_mir_node else None
overall_gm_p = geo_mean(all_mir_py) if all_mir_py else None
overall_gm_n_str = f"{overall_gm_n:.2f}x" if overall_gm_n else "---"
overall_gm_p_str = f"{overall_gm_p:.2f}x" if overall_gm_p else "---"
print("-" * 100)
print(f"{'OVERALL':12s} {overall_gm_n_str:>18s} {total_lw_node:>12d} {total_nw:>12d} {overall_gm_p_str:>16s} {total_lw_py:>12d} {total_pw:>10d} {total_benchmarks:>6d}")


# ============================================================
# Save JSON
# ============================================================
json_path = "test/benchmark/memory_results.json"

# Convert to MB for the JSON export
json_results = {}
for suite_name in results:
    json_results[suite_name] = {}
    for bench_name in results[suite_name]:
        data = results[suite_name][bench_name]
        json_results[suite_name][bench_name] = {"category": data.get("category", "")}
        for eng in ENGINES:
            val = data.get(eng)
            json_results[suite_name][bench_name][eng] = fmt_mem_mb(val)
        json_results[suite_name][bench_name]["_raw_bytes"] = {
            eng: data.get(eng) for eng in ENGINES
        }

with open(json_path, "w") as f:
    json.dump(json_results, f, indent=2, default=str)
print(f"\nRaw data saved to {json_path}")
print(f"\nDone! Total benchmarks: {total_benchmarks}")
