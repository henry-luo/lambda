#!/usr/bin/env python3
"""Run JS engines (LambdaJS, Node.js, QuickJS) for R7RS and AWFY suites only,
then merge results back into benchmark_results.json."""

import json, os, re, subprocess, sys, time

NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3
TIMEOUT_S = 120
LAMBDA_EXE = "./lambda.exe"
NODE_EXE = "node"
QJS_EXE = "qjs"
TIMING_RE = re.compile(r"__TIMING__:([\d.]+)")

R7RS = [
    ("fib",     "test/benchmark/r7rs/fib2.ls",     "test/benchmark/r7rs/fib2.js"),
    ("fibfp",   "test/benchmark/r7rs/fibfp2.ls",   "test/benchmark/r7rs/fibfp2.js"),
    ("tak",     "test/benchmark/r7rs/tak2.ls",     "test/benchmark/r7rs/tak2.js"),
    ("cpstak",  "test/benchmark/r7rs/cpstak2.ls",  "test/benchmark/r7rs/cpstak2.js"),
    ("sum",     "test/benchmark/r7rs/sum2.ls",     "test/benchmark/r7rs/sum2.js"),
    ("sumfp",   "test/benchmark/r7rs/sumfp2.ls",   "test/benchmark/r7rs/sumfp2.js"),
    ("nqueens", "test/benchmark/r7rs/nqueens2.ls",  "test/benchmark/r7rs/nqueens2.js"),
    ("fft",     "test/benchmark/r7rs/fft2.ls",     "test/benchmark/r7rs/fft2.js"),
    ("mbrot",   "test/benchmark/r7rs/mbrot2.ls",   "test/benchmark/r7rs/mbrot2.js"),
    ("ack",     "test/benchmark/r7rs/ack2.ls",     "test/benchmark/r7rs/ack2.js"),
]

AWFY = [
    ("sieve",       "test/benchmark/awfy/sieve2.ls",      "test/benchmark/awfy/sieve2.js"),
    ("permute",     "test/benchmark/awfy/permute2.ls",    "test/benchmark/awfy/permute2.js"),
    ("queens",      "test/benchmark/awfy/queens2.ls",     "test/benchmark/awfy/queens2.js"),
    ("towers",      "test/benchmark/awfy/towers2.ls",     "test/benchmark/awfy/towers2.js"),
    ("bounce",      "test/benchmark/awfy/bounce2.ls",     "test/benchmark/awfy/bounce2.js"),
    ("list",        "test/benchmark/awfy/list2.ls",       "test/benchmark/awfy/list2.js"),
    ("storage",     "test/benchmark/awfy/storage2.ls",    "test/benchmark/awfy/storage2.js"),
    ("mandelbrot",  "test/benchmark/awfy/mandelbrot2.ls", "test/benchmark/awfy/mandelbrot2.js"),
    ("nbody",       "test/benchmark/awfy/nbody2.ls",      "test/benchmark/awfy/nbody2.js"),
    ("richards",    "test/benchmark/awfy/richards2.ls",   "test/benchmark/awfy/richards2.js"),
    ("json",        "test/benchmark/awfy/json2.ls",       "test/benchmark/awfy/json2.js"),
    ("deltablue",   "test/benchmark/awfy/deltablue2.ls",  "test/benchmark/awfy/deltablue2.js"),
    ("havlak",      "test/benchmark/awfy/havlak2.ls",     "test/benchmark/awfy/havlak2.js"),
    ("cd",          "test/benchmark/awfy/cd2.ls",         "test/benchmark/awfy/cd2.js"),
]

SUITES = [("r7rs", R7RS), ("awfy", AWFY)]

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


def parse_timing(stdout):
    for line in stdout.strip().split("\n"):
        m = TIMING_RE.search(line.strip())
        if m:
            return float(m.group(1))
    return None


def run_once(cmd, timeout=TIMEOUT_S):
    start = time.perf_counter_ns()
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return (timeout * 1000, None, False)
    end = time.perf_counter_ns()
    wall_ms = (end - start) / 1_000_000
    if r.returncode != 0:
        return (wall_ms, None, False)
    exec_ms = parse_timing(r.stdout)
    return (wall_ms, exec_ms, True)


def run_benchmark(cmd, num_runs):
    walls = []
    execs = []
    ok = False
    for _ in range(num_runs):
        w, e, success = run_once(cmd)
        if success:
            ok = True
            walls.append(w)
            if e is not None:
                execs.append(e)
        print(".", end="", flush=True)
    if not ok:
        return (None, None, False)
    walls.sort()
    median_w = walls[len(walls) // 2]
    median_e = None
    if execs:
        execs.sort()
        median_e = execs[len(execs) // 2]
    return (median_w, median_e, True)


def make_qjs_wrapper(js_path):
    wrapper = os.path.join("temp", "qjs_" + os.path.basename(js_path))  # temp/ is fine for transient wrapper files
    with open(js_path) as f:
        code = f.read()
    with open(wrapper, "w") as f:
        f.write("import * as std from 'std';\n")
        f.write(QJS_POLYFILL)
        code = code.replace("'use strict';", "")
        code = code.replace('"use strict";', "")
        f.write(code)
    return wrapper


def fmt_ms(ms):
    if ms is None:
        return "failed"
    if ms < 1:
        return f"{ms:.3f} ms"
    elif ms < 1000:
        return f"{ms:.1f} ms"
    else:
        return f"{ms/1000:.3f} s"


def fmt_ms_short(ms):
    if ms is None:
        return "---"
    if ms < 1:
        return f"{ms:.2f}"
    elif ms < 1000:
        return f"{ms:.0f}"
    else:
        return f"{ms:.0f}"


# Load existing results
json_path = "test/benchmark/benchmark_results.json"
if os.path.exists(json_path):
    with open(json_path) as f:
        results = json.load(f)
else:
    results = {}

print(f"{'='*80}")
print(f"JS Engine Benchmarks — R7RS + AWFY (supplement run)")
print(f"{'='*80}")
print(f"  Runs per benchmark : {NUM_RUNS}")
print(f"  Lambda             : {LAMBDA_EXE}")
print(f"  Node.js            : {NODE_EXE}")
print(f"  QuickJS            : {QJS_EXE}")
print()

for suite_name, benchmarks in SUITES:
    print(f"\n{'='*80}")
    print(f"  Suite: {suite_name.upper()} ({len(benchmarks)} benchmarks)")
    print(f"{'='*80}")
    if suite_name not in results:
        results[suite_name] = {}

    for bench_name, ls_path, js_path in benchmarks:
        if bench_name not in results[suite_name]:
            results[suite_name][bench_name] = {}
        print(f"\n  {bench_name}:")

        if not js_path or not os.path.exists(js_path):
            print(f"    SKIPPED (no JS file)")
            continue

        # For AWFY, use bundle files for LambdaJS/QuickJS (no require())
        bundle_path = js_path.replace("2.js", "2_bundle.js")
        standalone_js = bundle_path if (suite_name == "awfy" and os.path.exists(bundle_path)) else js_path

        # --- LambdaJS ---
        print(f"    LambdaJS ", end="", flush=True)
        cmd = f"{LAMBDA_EXE} js {standalone_js}"
        w, e, ok = run_benchmark(cmd, NUM_RUNS)
        results[suite_name][bench_name]["lambdajs"] = [w, e] if ok else [None, None]
        print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")

        # --- Node.js (can handle require(), use original js_path) ---
        print(f"    Node.js  ", end="", flush=True)
        cmd = f"{NODE_EXE} {js_path}"
        w, e, ok = run_benchmark(cmd, NUM_RUNS)
        results[suite_name][bench_name]["nodejs"] = [w, e] if ok else [None, None]
        print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")

        # --- QuickJS (use standalone bundle, wrapped with polyfill) ---
        print(f"    QuickJS  ", end="", flush=True)
        wrapper = make_qjs_wrapper(standalone_js)
        cmd = f"{QJS_EXE} --std -m {wrapper}"
        w, e, ok = run_benchmark(cmd, NUM_RUNS)
        results[suite_name][bench_name]["quickjs"] = [w, e] if ok else [None, None]
        print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")

# Save merged results
with open(json_path, "w") as f:
    json.dump(results, f, indent=2, default=str)
print(f"\nResults merged into {json_path}")

# Print summary tables
ENGINES = ["mir", "c2mir", "lambdajs", "quickjs", "nodejs"]
ENGINE_LABELS = {"mir": "MIR", "c2mir": "C2MIR", "lambdajs": "LambdaJS", "quickjs": "QuickJS", "nodejs": "Node.js"}

for label, metric_idx in [("WALL-CLOCK", 0), ("SELF-REPORTED EXEC", 1)]:
    print(f"\n{'='*100}")
    print(f"{label} TIME (ms, median of {NUM_RUNS} runs)")
    print(f"{'='*100}")
    for suite_name, benchmarks in SUITES:
        print(f"\n--- {suite_name.upper()} ---")
        hdr = f"{'Benchmark':16s}"
        for eng in ENGINES:
            hdr += f" {ENGINE_LABELS[eng]:>12s}"
        print(hdr)
        print("-" * (16 + 13 * len(ENGINES)))
        for bench_name, _, _ in benchmarks:
            if bench_name not in results.get(suite_name, {}):
                continue
            row = f"{bench_name:16s}"
            for eng in ENGINES:
                data = results[suite_name][bench_name].get(eng, [None, None])
                val = data[metric_idx] if data else None
                row += f" {fmt_ms_short(val):>12s}"
            print(row)
