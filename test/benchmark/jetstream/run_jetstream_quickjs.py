#!/usr/bin/env python3
"""JetStream QuickJS Benchmark Runner

Runs all JetStream benchmarks on QuickJS using the reference JS source files.
Creates wrapper scripts that instantiate the Benchmark class and time execution.

Usage:
    python3 test/benchmark/jetstream/run_jetstream_quickjs.py [num_runs]
"""

import subprocess
import sys
import os
import re
import time
import math

NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3
TIMEOUT_S = 120

# Run from project root
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.."))

QJS_EXE = "qjs"
TIMING_RE = re.compile(r"__TIMING__:([\d.]+(?:e[+-]?\d+)?)")

BENCHMARKS = [
    ("nbody",         "ref/JetStream/SunSpider/n-body.js"),
    ("cube3d",        "ref/JetStream/SunSpider/3d-cube.js"),
    ("navier_stokes", "ref/JetStream/Octane/navier-stokes.js"),
    ("richards",      "ref/JetStream/Octane/richards.js"),
    ("splay",         "ref/JetStream/Octane/splay.js"),
    ("deltablue",     "ref/JetStream/Octane/deltablue.js"),
    ("hashmap",       "ref/JetStream/simple/hash-map.js"),
    ("crypto_sha1",   "ref/JetStream/SunSpider/crypto-sha1.js"),
    ("raytrace3d",    "ref/JetStream/SunSpider/3d-raytrace.js"),
]


def parse_timing(stdout):
    for line in stdout.strip().split("\n"):
        m = TIMING_RE.search(line.strip())
        if m:
            return float(m.group(1))
    return None


def median(vals):
    s = sorted(vals)
    n = len(s)
    if n == 0:
        return None
    if n % 2 == 1:
        return s[n // 2]
    return (s[n // 2 - 1] + s[n // 2]) / 2


def make_wrapper(name, js_path):
    """Create a wrapper JS file that loads the benchmark and times it."""
    src = open(js_path, "r").read()
    wrapper = f"""{src}
var b = new Benchmark();
var t0 = performance.now();
b.runIteration();
var t1 = performance.now();
console.log("__TIMING__:" + (t1 - t0).toFixed(3));
"""
    os.makedirs("temp", exist_ok=True)
    tmp_path = f"temp/_qjs_bench_{name}.js"
    with open(tmp_path, "w") as f:
        f.write(wrapper)
    return tmp_path


def run_once(cmd):
    start = time.perf_counter_ns()
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=TIMEOUT_S)
    except subprocess.TimeoutExpired:
        return (TIMEOUT_S * 1000, None, False, "TIMEOUT")
    end = time.perf_counter_ns()
    wall_ms = (end - start) / 1_000_000
    if r.returncode != 0:
        stderr_snip = r.stderr.strip()[:200] if r.stderr else ""
        return (wall_ms, None, False, stderr_snip)
    exec_ms = parse_timing(r.stdout)
    return (wall_ms, exec_ms, True, r.stdout.strip())


def run_benchmark(name, js_path):
    tmp_path = make_wrapper(name, js_path)
    cmd = f'{QJS_EXE} "{tmp_path}"'
    walls = []
    execs = []
    ok = False
    last_output = ""
    for _ in range(NUM_RUNS):
        w, e, success, output = run_once(cmd)
        if success:
            ok = True
            walls.append(w)
            if e is not None:
                execs.append(e)
            last_output = output
        else:
            last_output = output
    if not ok:
        return None, None, False, last_output
    return median(walls), median(execs) if execs else None, True, last_output


def main():
    print(f"JetStream QuickJS Benchmarks — {NUM_RUNS} run(s) each")
    print(f"{'Benchmark':<20} {'Wall (ms)':>12} {'Exec (ms)':>12} {'Status':<10}")
    print("-" * 58)

    results = []
    for name, js_path in BENCHMARKS:
        wall, exec_ms, ok, output = run_benchmark(name, js_path)
        if ok:
            w_str = f"{wall:.1f}" if wall is not None else "—"
            e_str = f"{exec_ms:.1f}" if exec_ms is not None else "—"
            status = "PASS"
            print(f"{name:<20} {w_str:>12} {e_str:>12} {status:<10}")
            results.append({"name": name, "wall_ms": wall, "exec_ms": exec_ms, "status": status})
        else:
            print(f"{name:<20} {'—':>12} {'—':>12} {'FAIL':<10}")
            if output:
                print(f"  Error: {output[:120]}")
            results.append({"name": name, "wall_ms": None, "exec_ms": None, "status": "FAIL"})

    print("-" * 58)
    passed = sum(1 for r in results if r["status"] != "FAIL")
    print(f"Passed: {passed}/{len(results)}")

    exec_times = [r["exec_ms"] for r in results if r["exec_ms"] is not None and r["exec_ms"] > 0]
    if exec_times:
        geo_mean = math.exp(sum(math.log(t) for t in exec_times) / len(exec_times))
        print(f"Geometric mean (exec): {geo_mean:.1f} ms")


if __name__ == "__main__":
    main()
