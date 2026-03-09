#!/usr/bin/env python3
"""Run JetStream benchmarks on LambdaJS engine.

Creates wrapper JS files that bypass the ES6 class pattern (which LambdaJS
may not support) and call the benchmark function directly with timing.
"""

import subprocess
import time
import os
import re
import sys

NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3
TIMEOUT_S = 120

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.."))

LAMBDA_EXE = "./lambda.exe"
TIMING_RE = re.compile(r"__TIMING__:([\d.]+(?:e[+-]?\d+)?)")

# Each benchmark: (name, js_source, run_expression)
# run_expression is what to call to execute the benchmark once
BENCHMARKS = [
    ("nbody",         "ref/JetStream/SunSpider/n-body.js",       "run()"),
    ("cube3d",        "ref/JetStream/SunSpider/3d-cube.js",      "run()"),
    ("navier_stokes", "ref/JetStream/Octane/navier-stokes.js",   None),  # need to check
    ("richards",      "ref/JetStream/Octane/richards.js",        None),  # need to check
    ("splay",         "ref/JetStream/Octane/splay.js",           None),  # need to check
    ("deltablue",     "ref/JetStream/Octane/deltablue.js",       None),  # need to check
    ("hashmap",       "ref/JetStream/simple/hash-map.js",        None),  # need to check
    ("crypto_sha1",   "ref/JetStream/SunSpider/crypto-sha1.js",  "run()"),
    ("crypto_aes",    "ref/JetStream/SunSpider/crypto-aes.js",   "run()"),
    ("crypto_md5",    "ref/JetStream/SunSpider/crypto-md5.js",   None),  # check
    ("raytrace3d",    "ref/JetStream/SunSpider/3d-raytrace.js",  "run()"),
    ("base64",        "ref/JetStream/SunSpider/base64.js",       None),  # check
    ("regex_dna",     "ref/JetStream/SunSpider/regex-dna.js",    None),  # check
]


def detect_run_function(js_path):
    """Try to detect the benchmark's run function from the Benchmark class."""
    with open(js_path) as f:
        code = f.read()
    
    # Look for runIteration() body
    m = re.search(r'runIteration\(\)\s*\{([^}]+)\}', code)
    if m:
        body = m.group(1).strip()
        # Extract the function call (e.g., "runRichards()" or "run()")
        calls = re.findall(r'(\w+)\(\)', body)
        if calls:
            # Get the last unique function call (not 'let', 'var', etc.)
            for call in calls:
                if call not in ('let', 'var', 'const', 'for', 'if'):
                    return f"{call}()"
    return None


def make_lambdajs_wrapper(name, js_path, run_expr):
    """Create a LambdaJS-friendly wrapper that bypasses ES6 class."""
    os.makedirs("temp", exist_ok=True)
    with open(js_path) as f:
        code = f.read()
    
    # Strip the class Benchmark { ... } block at the end
    code = re.sub(r'\nclass Benchmark \{[^}]+\}\s*$', '', code, flags=re.DOTALL)
    
    # Also strip any 'use strict'
    code = code.replace("'use strict';", "")
    code = code.replace('"use strict";', "")
    
    # Create wrapper with timing
    wrapper_path = f"temp/ljs_jetstream_{name}.js"
    with open(wrapper_path, "w") as f:
        f.write(code)
        f.write("\n")
        f.write("// Timing wrapper\n")
        f.write("var _t0 = performance.now();\n")
        if run_expr:
            # Run the same number of iterations as Benchmark class would
            f.write(f"for (var _i = 0; _i < 8; _i++) {{ {run_expr}; }}\n")
        f.write("var _t1 = performance.now();\n")
        f.write('console.log("__TIMING__:" + (_t1 - _t0).toFixed(3));\n')
    
    return wrapper_path


def parse_timing(stdout):
    for line in stdout.strip().split("\n"):
        m = TIMING_RE.search(line.strip())
        if m:
            return float(m.group(1))
    return None


def run_once(cmd):
    start = time.perf_counter_ns()
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=TIMEOUT_S)
    except subprocess.TimeoutExpired:
        return None, None, False, "TIMEOUT"
    end = time.perf_counter_ns()
    wall_ms = (end - start) / 1_000_000
    if r.returncode != 0:
        stderr = r.stderr.strip()[:300] if r.stderr else ""
        return wall_ms, None, False, stderr
    exec_ms = parse_timing(r.stdout)
    return wall_ms, exec_ms, True, r.stdout.strip()[:200]


def run_benchmark(cmd, num_runs):
    walls = []
    execs = []
    ok = False
    last_output = ""
    for _ in range(num_runs):
        w, e, success, output = run_once(cmd)
        if success:
            ok = True
            walls.append(w)
            if e is not None:
                execs.append(e)
            last_output = output
        else:
            last_output = output
        print(".", end="", flush=True)
    if not ok:
        return None, None, False, last_output
    walls.sort()
    execs.sort()
    med_w = walls[len(walls) // 2]
    med_e = execs[len(execs) // 2] if execs else None
    return med_w, med_e, True, last_output


print(f"JetStream LambdaJS Benchmarks — {NUM_RUNS} runs each")
print(f"{'Benchmark':<20} {'Wall (ms)':>12} {'Exec (ms)':>12} {'Status':<10}")
print("-" * 58)

results = []

for name, js_path, run_expr in BENCHMARKS:
    if not os.path.exists(js_path):
        print(f"{name:<20} {'—':>12} {'—':>12} {'NO FILE':<10}")
        results.append({"name": name, "wall_ms": None, "exec_ms": None, "status": "NO FILE"})
        continue
    
    # Auto-detect run function if not specified
    if run_expr is None:
        run_expr = detect_run_function(js_path)
        if run_expr is None:
            print(f"{name:<20} {'—':>12} {'—':>12} {'NO RUN FN':<10}")
            results.append({"name": name, "wall_ms": None, "exec_ms": None, "status": "NO RUN FN"})
            continue
    
    wrapper = make_lambdajs_wrapper(name, js_path, run_expr)
    cmd = f"{LAMBDA_EXE} js {wrapper}"
    
    print(f"{name:<20} ", end="", flush=True)
    wall, exec_ms, ok, output = run_benchmark(cmd, NUM_RUNS)
    
    if ok:
        w_str = f"{wall:.1f}" if wall else "—"
        e_str = f"{exec_ms:.1f}" if exec_ms else f"{wall:.1f}*"
        # Use wall time as exec proxy since LambdaJS doesn't support internal timing
        actual_ms = exec_ms if exec_ms else wall
        print(f"{w_str:>12} {e_str:>12} {'PASS':<10}")
        results.append({"name": name, "wall_ms": wall, "exec_ms": actual_ms, "status": "PASS"})
    else:
        print(f"{'—':>12} {'—':>12} {'FAIL':<10}")
        if output:
            # Show a snippet of the error
            err_lines = output.strip().split("\n")
            for line in err_lines[:2]:
                print(f"  {line[:120]}")
        results.append({"name": name, "wall_ms": None, "exec_ms": None, "status": "FAIL"})

print("-" * 58)
passed = sum(1 for r in results if r["status"] == "PASS")
print(f"Passed: {passed}/{len(results)}")

if passed > 0:
    import math
    exec_times = [r["exec_ms"] for r in results if r["exec_ms"] is not None and r["exec_ms"] > 0]
    if exec_times:
        geo = math.exp(sum(math.log(t) for t in exec_times) / len(exec_times))
        print(f"Geometric mean (exec): {geo:.1f} ms")

# Print summary for easy copy
print("\nResults for Overall_Result3.md:")
for r in results:
    if r["status"] == "PASS":
        print(f"  {r['name']}: exec={r['exec_ms']:.1f}ms")
