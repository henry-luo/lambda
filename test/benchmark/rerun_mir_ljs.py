#!/usr/bin/env python3
"""Rerun MIR Direct + LambdaJS for all 5 suites with robust timeout handling.
Merges results back into test/benchmark/benchmark_results.json."""

import json, os, re, signal, subprocess, sys, time

NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3
TIMEOUT_S = 90        # Wall-clock timeout per single run
LAMBDA_EXE = "./lambda.exe"
TIMING_RE = re.compile(r"__TIMING__:([\d.]+(?:e[+-]?\d+)?)")
JSON_PATH = "test/benchmark/benchmark_results.json"

# All suites: (suite_name, [(bench_name, ls_path, js_path), ...])
SUITES = {
    "r7rs": [
        ("fib",     "test/benchmark/r7rs/fib2.ls",     "test/benchmark/r7rs/fib2.js"),
        ("fibfp",   "test/benchmark/r7rs/fibfp2.ls",   "test/benchmark/r7rs/fibfp2.js"),
        ("tak",     "test/benchmark/r7rs/tak2.ls",      "test/benchmark/r7rs/tak2.js"),
        ("cpstak",  "test/benchmark/r7rs/cpstak2.ls",   "test/benchmark/r7rs/cpstak2.js"),
        ("sum",     "test/benchmark/r7rs/sum2.ls",      "test/benchmark/r7rs/sum2.js"),
        ("sumfp",   "test/benchmark/r7rs/sumfp2.ls",    "test/benchmark/r7rs/sumfp2.js"),
        ("nqueens", "test/benchmark/r7rs/nqueens2.ls",  "test/benchmark/r7rs/nqueens2.js"),
        ("fft",     "test/benchmark/r7rs/fft2.ls",      "test/benchmark/r7rs/fft2.js"),
        ("mbrot",   "test/benchmark/r7rs/mbrot2.ls",    "test/benchmark/r7rs/mbrot2.js"),
        ("ack",     "test/benchmark/r7rs/ack2.ls",      "test/benchmark/r7rs/ack2.js"),
    ],
    "awfy": [
        ("sieve",       "test/benchmark/awfy/sieve2.ls",       "test/benchmark/awfy/sieve2.js"),
        ("permute",     "test/benchmark/awfy/permute2.ls",     "test/benchmark/awfy/permute2.js"),
        ("queens",      "test/benchmark/awfy/queens2.ls",      "test/benchmark/awfy/queens2.js"),
        ("towers",      "test/benchmark/awfy/towers2.ls",      "test/benchmark/awfy/towers2.js"),
        ("bounce",      "test/benchmark/awfy/bounce2.ls",      "test/benchmark/awfy/bounce2.js"),
        ("list",        "test/benchmark/awfy/list2.ls",        "test/benchmark/awfy/list2.js"),
        ("storage",     "test/benchmark/awfy/storage2.ls",     "test/benchmark/awfy/storage2.js"),
        ("mandelbrot",  "test/benchmark/awfy/mandelbrot2.ls",  "test/benchmark/awfy/mandelbrot2.js"),
        ("nbody",       "test/benchmark/awfy/nbody2.ls",       "test/benchmark/awfy/nbody2.js"),
        ("richards",    "test/benchmark/awfy/richards2.ls",    "test/benchmark/awfy/richards2.js"),
        ("json",        "test/benchmark/awfy/json2.ls",        "test/benchmark/awfy/json2.js"),
        ("deltablue",   "test/benchmark/awfy/deltablue2.ls",   "test/benchmark/awfy/deltablue2.js"),
        ("havlak",      "test/benchmark/awfy/havlak2.ls",      "test/benchmark/awfy/havlak2.js"),
        ("cd",          "test/benchmark/awfy/cd2.ls",          "test/benchmark/awfy/cd2.js"),
    ],
    "beng": [
        ("binarytrees",  "test/benchmark/beng/binarytrees.ls",  "test/benchmark/beng/js/binarytrees.js"),
        ("fannkuch",     "test/benchmark/beng/fannkuch.ls",     "test/benchmark/beng/js/fannkuch.js"),
        ("fasta",        "test/benchmark/beng/fasta.ls",        "test/benchmark/beng/js/fasta.js"),
        ("knucleotide",  "test/benchmark/beng/knucleotide.ls",  "test/benchmark/beng/js/knucleotide.js"),
        ("mandelbrot",   "test/benchmark/beng/mandelbrot.ls",   "test/benchmark/beng/js/mandelbrot.js"),
        ("nbody",        "test/benchmark/beng/nbody.ls",        "test/benchmark/beng/js/nbody.js"),
        ("pidigits",     "test/benchmark/beng/pidigits.ls",     "test/benchmark/beng/js/pidigits.js"),
        ("regexredux",   "test/benchmark/beng/regexredux.ls",   "test/benchmark/beng/js/regexredux.js"),
        ("revcomp",      "test/benchmark/beng/revcomp.ls",      "test/benchmark/beng/js/revcomp.js"),
        ("spectralnorm", "test/benchmark/beng/spectralnorm.ls", "test/benchmark/beng/js/spectralnorm.js"),
    ],
    "kostya": [
        ("brainfuck",   "test/benchmark/kostya/brainfuck.ls",   "test/benchmark/kostya/brainfuck.js"),
        ("matmul",      "test/benchmark/kostya/matmul.ls",      "test/benchmark/kostya/matmul.js"),
        ("primes",      "test/benchmark/kostya/primes.ls",      "test/benchmark/kostya/primes.js"),
        ("base64",      "test/benchmark/kostya/base64.ls",      "test/benchmark/kostya/base64.js"),
        ("levenshtein", "test/benchmark/kostya/levenshtein.ls", "test/benchmark/kostya/levenshtein.js"),
        ("json_gen",    "test/benchmark/kostya/json_gen.ls",    "test/benchmark/kostya/json_gen.js"),
        ("collatz",     "test/benchmark/kostya/collatz.ls",     "test/benchmark/kostya/collatz.js"),
    ],
    "larceny": [
        ("triangl",    "test/benchmark/larceny/triangl.ls",    "test/benchmark/larceny/triangl.js"),
        ("array1",     "test/benchmark/larceny/array1.ls",     "test/benchmark/larceny/array1.js"),
        ("deriv",      "test/benchmark/larceny/deriv.ls",      "test/benchmark/larceny/deriv.js"),
        ("diviter",    "test/benchmark/larceny/diviter.ls",    "test/benchmark/larceny/diviter.js"),
        ("divrec",     "test/benchmark/larceny/divrec.ls",     "test/benchmark/larceny/divrec.js"),
        ("gcbench",    "test/benchmark/larceny/gcbench.ls",    "test/benchmark/larceny/gcbench.js"),
        ("paraffins",  "test/benchmark/larceny/paraffins.ls",  "test/benchmark/larceny/paraffins.js"),
        ("pnpoly",     "test/benchmark/larceny/pnpoly.ls",    "test/benchmark/larceny/pnpoly.js"),
        ("primes",     "test/benchmark/larceny/primes.ls",     "test/benchmark/larceny/primes.js"),
        ("puzzle",     "test/benchmark/larceny/puzzle.ls",     "test/benchmark/larceny/puzzle.js"),
        ("quicksort",  "test/benchmark/larceny/quicksort.ls",  "test/benchmark/larceny/quicksort.js"),
        ("ray",        "test/benchmark/larceny/ray.ls",        "test/benchmark/larceny/ray.js"),
    ],
}

SUITE_ORDER = ["r7rs", "awfy", "beng", "kostya", "larceny"]

def parse_timing(stdout):
    for line in stdout.strip().split("\n"):
        m = TIMING_RE.search(line.strip())
        if m:
            return float(m.group(1))
    return None

def run_once(cmd, timeout=TIMEOUT_S):
    """Run a command with robust timeout using process group kill."""
    start = time.perf_counter_ns()
    try:
        proc = subprocess.Popen(
            cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, preexec_fn=os.setsid  # Create new process group
        )
        try:
            stdout, stderr = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            # Kill entire process group
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            proc.wait()
            return (timeout * 1000, None, False)
        
        end = time.perf_counter_ns()
        wall_ms = (end - start) / 1_000_000
        
        if proc.returncode != 0:
            return (wall_ms, None, False)
        
        exec_ms = parse_timing(stdout)
        return (wall_ms, exec_ms, True)
    except Exception as e:
        return (0, None, False)

def run_benchmark(cmd, num_runs):
    """Run benchmark multiple times, return median wall/exec times."""
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
        return (None, None)
    walls.sort()
    median_w = walls[len(walls) // 2]
    median_e = None
    if execs:
        execs.sort()
        median_e = execs[len(execs) // 2]
    return (median_w, median_e)

def fmt(ms):
    if ms is None: return "---"
    if ms < 1: return f"{ms:.3f}"
    if ms < 100: return f"{ms:.1f}"
    if ms < 1000: return f"{ms:.0f}"
    return f"{ms/1000:.2f}s"

# Load existing results
if os.path.exists(JSON_PATH):
    with open(JSON_PATH) as f:
        results = json.load(f)
else:
    results = {}

total = sum(len(SUITES[s]) for s in SUITE_ORDER)
print(f"Rerunning MIR + LambdaJS: {total} benchmarks × {NUM_RUNS} runs (timeout {TIMEOUT_S}s)")
print(f"Lambda: {LAMBDA_EXE}")
print()

count = 0
for suite_name in SUITE_ORDER:
    benchmarks = SUITES[suite_name]
    print(f"\n=== {suite_name.upper()} ({len(benchmarks)} benchmarks) ===")
    
    if suite_name not in results:
        results[suite_name] = {}

    for bench_name, ls_path, js_path in benchmarks:
        count += 1
        if bench_name not in results[suite_name]:
            results[suite_name][bench_name] = {}
        
        old_mir = results[suite_name][bench_name].get("mir", [None, None])
        old_ljs = results[suite_name][bench_name].get("lambdajs", [None, None])
        
        # --- MIR Direct ---
        print(f"  [{count}/{total}] {bench_name} MIR", end="", flush=True)
        cmd = f"{LAMBDA_EXE} run {ls_path}"
        w, e = run_benchmark(cmd, NUM_RUNS)
        results[suite_name][bench_name]["mir"] = [w, e]
        old_e = old_mir[1] if old_mir and len(old_mir) > 1 else None
        delta = ""
        if e is not None and old_e is not None and old_e > 0:
            pct = (e - old_e) / old_e * 100
            delta = f" ({pct:+.1f}%)"
        print(f" {fmt(e)}{delta}")

        # --- Lambda JS ---
        # For AWFY suite, prefer bundle files (no require())
        actual_js = js_path
        if suite_name == "awfy":
            bundle = js_path.replace("2.js", "2_bundle.js")
            if os.path.exists(bundle):
                actual_js = bundle

        if actual_js and os.path.exists(actual_js):
            print(f"  [{count}/{total}] {bench_name} LJS", end="", flush=True)
            cmd = f"{LAMBDA_EXE} js {actual_js}"
            w, e = run_benchmark(cmd, NUM_RUNS)
            results[suite_name][bench_name]["lambdajs"] = [w, e]
            old_e = old_ljs[1] if old_ljs and len(old_ljs) > 1 else None
            delta = ""
            if e is not None and old_e is not None and old_e > 0:
                pct = (e - old_e) / old_e * 100
                delta = f" ({pct:+.1f}%)"
            print(f" {fmt(e)}{delta}")
        else:
            results[suite_name][bench_name]["lambdajs"] = [None, None]
            print(f"  [{count}/{total}] {bench_name} LJS --- (no JS file)")

    # Save after each suite completes (intermediate checkpoint)
    with open(JSON_PATH, "w") as f:
        json.dump(results, f, indent=2, default=str)
    print(f"  [checkpoint saved]")

# Final summary
print(f"\n{'='*70}")
print(f"RESULTS SUMMARY (exec_ms)")
print(f"{'='*70}")
for suite_name in SUITE_ORDER:
    print(f"\n--- {suite_name.upper()} ---")
    print(f"  {'Bench':16s} {'MIR':>8s} {'LambdaJS':>10s}")
    for bench_name, _, _ in SUITES[suite_name]:
        d = results.get(suite_name, {}).get(bench_name, {})
        mir_e = d.get("mir", [None, None])[1] if d.get("mir") else None
        ljs_e = d.get("lambdajs", [None, None])[1] if d.get("lambdajs") else None
        print(f"  {bench_name:16s} {fmt(mir_e):>8s} {fmt(ljs_e):>10s}")

print(f"\nDone. Results saved to {JSON_PATH}")
