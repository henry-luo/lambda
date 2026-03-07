#!/usr/bin/env python3
"""Run all 5 engines for BENG suite only, then merge results."""
import json, os, re, subprocess, sys, time

NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3
TIMEOUT_S = 30
LAMBDA_EXE = "./lambda.exe"
NODE_EXE = "node"
QJS_EXE = "qjs"
TIMING_RE = re.compile(r"__TIMING__:([\d.]+)")

BENG = [
    ("binarytrees",  "test/benchmark/beng/binarytrees.ls",  "test/benchmark/beng/js/binarytrees.js"),
    ("fannkuch",     "test/benchmark/beng/fannkuch.ls",     "test/benchmark/beng/js/fannkuch.js"),
    ("fasta",        "test/benchmark/beng/fasta.ls",        "test/benchmark/beng/js/fasta.js"),
    ("knucleotide",  "test/benchmark/beng/knucleotide.ls",  "test/benchmark/beng/js/knucleotide.js"),
    ("mandelbrot",   "test/benchmark/beng/mandelbrot.ls",   "test/benchmark/beng/js/mandelbrot.js"),
    ("nbody",        "test/benchmark/beng/nbody.ls",        "test/benchmark/beng/js/nbody.js"),
    ("pidigits",     "test/benchmark/beng/pidigits.ls",     "test/benchmark/beng/js/pidigits.js"),
    ("regexredux",   "test/benchmark/beng/regexredux.ls",   "test/benchmark/beng/js/regexredux.js"),
    ("revcomp",      "test/benchmark/beng/revcomp.ls",      "test/benchmark/beng/js/revcomp.js"),
    ("spectralnorm", "test/benchmark/beng/spectralnorm.ls",  "test/benchmark/beng/js/spectralnorm.js"),
]

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
    walls, execs = [], []
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
    median_e = execs[len(execs) // 2] if execs else None
    return (median_w, median_e, True)

def make_qjs_wrapper(js_path):
    wrapper = os.path.join("temp", "qjs_" + os.path.basename(js_path))  # temp/ is fine for transient wrapper files
    with open(js_path) as f:
        code = f.read()
    with open(wrapper, "w") as f:
        f.write("import * as std from 'std';\n")
        f.write(QJS_POLYFILL)
        code = code.replace("'use strict';", "").replace('"use strict";', "")
        f.write(code)
    return wrapper

def fmt_ms(ms):
    if ms is None: return "failed"
    if ms < 1: return f"{ms:.3f} ms"
    elif ms < 1000: return f"{ms:.1f} ms"
    else: return f"{ms/1000:.3f} s"

def fmt_ms_short(ms):
    if ms is None: return "---"
    if ms < 1: return f"{ms:.2f}"
    elif ms < 1000: return f"{ms:.0f}"
    else: return f"{ms:.0f}"

# Load existing results
json_path = "test/benchmark/benchmark_results.json"
with open(json_path) as f:
    results = json.load(f)

print(f"{'='*80}")
print(f"BENG Benchmark Suite — All 5 Engines (re-run)")
print(f"{'='*80}")
print(f"  Runs: {NUM_RUNS}  Timeout: {TIMEOUT_S}s\n")

suite_name = "beng"
if suite_name not in results:
    results[suite_name] = {}

def save_results():
    with open(json_path, "w") as f:
        json.dump(results, f, indent=2, default=str)

for bench_name, ls_path, js_path in BENG:
    if bench_name not in results[suite_name]:
        results[suite_name][bench_name] = {}
    print(f"\n  {bench_name}:")

    # MIR Direct
    if os.path.exists(ls_path):
        print(f"    MIR      ", end="", flush=True)
        cmd = f"{LAMBDA_EXE} run {ls_path}"
        w, e, ok = run_benchmark(cmd, NUM_RUNS)
        results[suite_name][bench_name]["mir"] = [w, e] if ok else [None, None]
        print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")

    # C2MIR
    if os.path.exists(ls_path):
        print(f"    C2MIR    ", end="", flush=True)
        cmd = f"{LAMBDA_EXE} run --c2mir {ls_path}"
        w, e, ok = run_benchmark(cmd, NUM_RUNS)
        results[suite_name][bench_name]["c2mir"] = [w, e] if ok else [None, None]
        print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")

    # LambdaJS (shorter timeout for JS engines in Lambda runtime)
    if js_path and os.path.exists(js_path):
        print(f"    LambdaJS ", end="", flush=True)
        cmd = f"{LAMBDA_EXE} js {js_path}"
        w, e, ok = run_benchmark(cmd, NUM_RUNS)
        results[suite_name][bench_name]["lambdajs"] = [w, e] if ok else [None, None]
        print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")

    # Node.js
    if js_path and os.path.exists(js_path):
        print(f"    Node.js  ", end="", flush=True)
        cmd = f"{NODE_EXE} {js_path}"
        w, e, ok = run_benchmark(cmd, NUM_RUNS)
        results[suite_name][bench_name]["nodejs"] = [w, e] if ok else [None, None]
        print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")

    # QuickJS
    if js_path and os.path.exists(js_path):
        print(f"    QuickJS  ", end="", flush=True)
        wrapper = make_qjs_wrapper(js_path)
        cmd = f"{QJS_EXE} --std -m {wrapper}"
        w, e, ok = run_benchmark(cmd, NUM_RUNS)
        results[suite_name][bench_name]["quickjs"] = [w, e] if ok else [None, None]
        print(f" wall={fmt_ms(w)}, exec={fmt_ms(e)}")

    # Save after each benchmark
    save_results()
    print(f"    [saved]")

print(f"\nResults merged into {json_path}")

# Summary table
ENGINES = ["mir", "c2mir", "lambdajs", "quickjs", "nodejs"]
ENGINE_LABELS = {"mir": "MIR", "c2mir": "C2MIR", "lambdajs": "LambdaJS", "quickjs": "QuickJS", "nodejs": "Node.js"}
for label, idx in [("WALL-CLOCK", 0), ("SELF-REPORTED EXEC", 1)]:
    print(f"\n{'='*100}")
    print(f"BENG — {label} TIME (ms, median of {NUM_RUNS} runs)")
    print(f"{'='*100}")
    hdr = f"{'Benchmark':16s}"
    for eng in ENGINES:
        hdr += f" {ENGINE_LABELS[eng]:>12s}"
    print(hdr)
    print("-" * (16 + 13 * len(ENGINES)))
    for bench_name, _, _ in BENG:
        row = f"{bench_name:16s}"
        for eng in ENGINES:
            data = results[suite_name][bench_name].get(eng, [None, None])
            val = data[idx] if data else None
            row += f" {fmt_ms_short(val):>12s}"
        print(row)
