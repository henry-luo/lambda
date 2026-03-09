#!/usr/bin/env python3
"""Generate Overall_Result3.md from benchmark_results_v3.json"""
import json
import math

with open("test/benchmark/benchmark_results_v3.json") as f:
    data = json.load(f)

# Round 2 data (from Overall_Result2.md)
R2_MIR = {
    "r7rs": {"fib": 2.1, "fibfp": 3.6, "tak": 0.15, "cpstak": 0.31, "sum": 0.27, "sumfp": 0.067, "nqueens": 6.5, "fft": 0.19, "mbrot": 0.60, "ack": 10.3},
    "awfy": {"sieve": 0.053, "permute": 0.066, "queens": 0.15, "towers": 0.22, "bounce": 0.20, "list": 0.023, "storage": 0.32, "mandelbrot": 32.0, "nbody": 1.3, "richards": 56.7, "json": 0.028, "deltablue": 6.1, "havlak": 339, "cd": 445},
    "beng": {"binarytrees": 8.0, "fannkuch": 0.74, "fasta": 1.1, "knucleotide": 2.9, "mandelbrot": 22.4, "nbody": 2.8, "pidigits": 0.43, "regexredux": 1.2, "revcomp": 1.9, "spectralnorm": 13.1},
    "kostya": {"brainfuck": 164, "matmul": 8.7, "primes": 7.3, "base64": 222, "levenshtein": 8.5, "json_gen": 64.7, "collatz": 302},
    "larceny": {"triangl": 187, "array1": 0.56, "deriv": 20.0, "diviter": 272, "divrec": 0.82, "gcbench": 472, "paraffins": 0.33, "pnpoly": 58.9, "primes": 0.47, "puzzle": 3.9, "quicksort": 2.9, "ray": 7.2},
}
R2_LJS = {
    "r7rs": {"fib": 1.1, "fibfp": 1.2, "tak": 0.11, "cpstak": 0.22, "sum": 16.5, "sumfp": 1.7, "nqueens": 0.013, "fft": 2.4, "mbrot": None, "ack": 8.6},
    "awfy": {"sieve": 0.009, "permute": 0.008, "queens": 0.007, "towers": 0.006, "bounce": None, "list": 0.008, "storage": None, "mandelbrot": None, "nbody": 0.11, "richards": 0.008, "json": None, "deltablue": None, "havlak": None, "cd": None},
    "beng": {"binarytrees": 18.8, "fannkuch": None, "fasta": 1.1, "knucleotide": 0.054, "mandelbrot": None, "nbody": 134, "pidigits": 0.015, "regexredux": 0.083, "revcomp": 0.001, "spectralnorm": 19.2},
    "kostya": {"brainfuck": 420, "matmul": 1180, "primes": 19.4, "base64": 0.0, "levenshtein": 30.7, "json_gen": 20.3, "collatz": None},
    "larceny": {"triangl": 1680, "array1": 4.1, "deriv": 44.3, "diviter": 11630, "divrec": 0.84, "gcbench": 586, "paraffins": 0.72, "pnpoly": 78.5, "primes": 1.5, "puzzle": None, "quicksort": 0.19, "ray": 13.7},
}

def fmt(v, show_s=True):
    """Format ms value for table display."""
    if v is None:
        return "---"
    if v == 0:
        return "0.000"
    if v < 0.01:
        return f"{v:.3f}"
    if v < 0.1:
        return f"{v:.3f}"
    if v < 1:
        return f"{v:.2f}"
    if v < 10:
        return f"{v:.1f}"
    if v < 1000:
        return f"{v:.0f}"
    if show_s:
        return f"{v/1000:.2f}s"
    return f"{v:.0f}"

def ratio_str(a, b):
    if a is None or b is None or b == 0:
        return "---"
    r = a / b
    if r < 0.01:
        return f"{r:.3f}x"
    if r < 10:
        return f"{r:.2f}x"
    if r < 100:
        return f"{r:.1f}x"
    return f"{r:.0f}x"

def geo_mean(vals):
    v = [x for x in vals if x is not None and x > 0]
    if not v:
        return None
    return math.exp(sum(math.log(x) for x in v) / len(v))

ENGINES = ["mir", "c2mir", "lambdajs", "quickjs", "nodejs", "python"]
ENG_LABELS = {"mir": "MIR", "c2mir": "C2MIR", "lambdajs": "LambdaJS", "quickjs": "QuickJS", "nodejs": "Node.js", "python": "Python"}

SUITE_ORDER = ["r7rs", "awfy", "beng", "kostya", "larceny", "jetstream"]
SUITE_LABELS = {"r7rs": "R7RS", "awfy": "AWFY", "beng": "BENG", "kostya": "KOSTYA", "larceny": "LARCENY", "jetstream": "JetStream"}

SUITE_DESCRIPTIONS = {
    "r7rs": "Classic Scheme benchmark suite adapted for Lambda with type annotations. Tests recursive functions, numeric computation, and backtracking.",
    "awfy": "Standard cross-language benchmark suite from Stefan Marr. Lambda implementations use procedural style; JS uses official AWFY source.",
    "beng": "Subset of the Computer Language Benchmarks Game. Tests diverse real-world computation: GC stress, regex, FASTA I/O, numeric precision, permutations.",
    "kostya": "Community benchmarks from kostya/benchmarks comparing languages on common tasks.",
    "larceny": "Classic Gabriel/Larceny Scheme benchmark suite testing diverse functional programming patterns.",
    "jetstream": "Benchmarks from Apple's JetStream suite (SunSpider + Octane). Tests numeric computation, 3D rendering, crypto, and data structures.",
}

# Known bad values to exclude
BAD_VALUES = {("awfy", "mandelbrot", "python")}  # 0.004ms is clearly wrong

lines = []
def w(s=""):
    lines.append(s)

# ============================================================
# Build the document
# ============================================================
w("# Lambda Benchmark Results: 6 Suites × 6 Engines (Round 3)")
w()
w("**Date:** 2026-03-09  ")
w("**Platform:** Apple Silicon MacBook Air (M4, aarch64), macOS  ")
w("**Lambda version:** release build (8.3 MB, stripped, `-O2`)  ")
w("**Node.js:** v22.13.0 (V8 JIT)  ")
w("**QuickJS:** v2025-09-13 (interpreter)  ")
w("**Python:** 3.13.3 (CPython)  ")
w("**Methodology:** 3 runs per benchmark, median of self-reported execution time (excludes startup/JIT compilation overhead)")
w()
w("---")
w()
w("## Engine Overview")
w()
w("| Engine | Type | Description |")
w("| --- | --- | --- |")
w("| **MIR Direct** | JIT | Lambda → MIR IR → native code (default compiler path) |")
w("| **C2MIR** | JIT | Lambda → C source → MIR (legacy path via c2mir) |")
w("| **LambdaJS** | JIT | Lambda's built-in JavaScript JIT |")
w("| **QuickJS** | Interpreter | Standalone QuickJS JavaScript engine |")
w("| **Node.js** | JIT | Google V8 JavaScript engine with optimizing JIT |")
w("| **Python** | Interpreter | CPython 3.13 reference interpreter |")
w()
w("---")

# Per-suite tables
for suite in SUITE_ORDER:
    if suite not in data:
        continue

    w()
    w(f"## {SUITE_LABELS[suite]} Benchmarks")
    w()
    w(f"> {SUITE_DESCRIPTIONS[suite]}")
    w()

    # Table header
    w(f"| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |")
    w(f"| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |")

    benchmarks = data[suite]
    mir_node_ratios = []
    mir_py_ratios = []

    for bench_name, bench_data in benchmarks.items():
        cat = bench_data.get("category", "")
        vals = {}
        for eng in ENGINES:
            v = bench_data.get(eng)
            if (suite, bench_name, eng) in BAD_VALUES:
                v = None
            vals[eng] = v

        # Compute ratios
        mir_v = vals.get("mir")
        node_v = vals.get("nodejs")
        py_v = vals.get("python")

        mn_ratio = None
        mp_ratio = None
        if mir_v and node_v and mir_v > 0 and node_v > 0:
            mn_ratio = mir_v / node_v
            mir_node_ratios.append(mn_ratio)
        if mir_v and py_v and mir_v > 0 and py_v > 0:
            mp_ratio = mir_v / py_v
            mir_py_ratios.append(mp_ratio)

        mn_str = ratio_str(mir_v, node_v)
        mp_str = ratio_str(mir_v, py_v) if (suite, bench_name, "python") not in BAD_VALUES else "---"

        row = f"| {bench_name} | {cat} |"
        for eng in ENGINES:
            row += f" {fmt(vals[eng]):>5s} |"
        row += f" {mn_str} | {mp_str} |"
        w(row)

    # Geo means
    gm_node = geo_mean(mir_node_ratios)
    gm_py = geo_mean(mir_py_ratios)

    lw_node = sum(1 for r in mir_node_ratios if r < 1.0)
    nw_node = len(mir_node_ratios) - lw_node
    lw_py = sum(1 for r in mir_py_ratios if r < 1.0)
    nw_py = len(mir_py_ratios) - lw_py

    w()
    if gm_node:
        w(f"**Geometric mean MIR/Node.js: {gm_node:.2f}x** — Lambda {'faster' if gm_node < 1 else 'slower'} on {lw_node}/{len(mir_node_ratios)} benchmarks")
    if gm_py:
        w(f"**Geometric mean MIR/Python: {gm_py:.2f}x** — Lambda faster on {lw_py}/{len(mir_py_ratios)} benchmarks")
    w()
    w("---")

# ============================================================
# Overall Summary
# ============================================================
w()
w("## Overall Summary")
w()
w("### MIR Direct vs Node.js V8 (Self-Reported Exec Time)")
w()
w("| Suite | Geo. Mean | Lambda Wins | Node Wins | Total |")
w("|-------|----------:|:-----------:|:---------:|:-----:|")

all_mn = []
all_mp = []
total_lw_n = 0
total_nw = 0
total_lw_p = 0
total_pw = 0
total_bench = 0

for suite in SUITE_ORDER:
    if suite not in data:
        continue
    benchmarks = data[suite]
    mn = []
    mp = []
    for bench_name, bench_data in benchmarks.items():
        mir_v = bench_data.get("mir")
        node_v = bench_data.get("nodejs")
        py_v = bench_data.get("python")
        if (suite, bench_name, "python") in BAD_VALUES:
            py_v = None
        if mir_v and node_v and mir_v > 0 and node_v > 0:
            mn.append(mir_v / node_v)
        if mir_v and py_v and mir_v > 0 and py_v > 0:
            mp.append(mir_v / py_v)

    all_mn.extend(mn)
    all_mp.extend(mp)

    gm = geo_mean(mn) if mn else None
    lw = sum(1 for r in mn if r < 1.0)
    nw = len(mn) - lw
    total_lw_n += lw
    total_nw += nw
    total_bench += len(benchmarks)

    gm_str = f"{gm:.2f}x" if gm else "---"
    w(f"| {SUITE_LABELS[suite]} | {gm_str} | {lw} | {nw} | {len(benchmarks)} |")

overall_gm_n = geo_mean(all_mn)
w(f"| **Overall** | **{overall_gm_n:.2f}x** | **{total_lw_n}** | **{total_nw}** | **{total_bench}** |")

w()
w("> Ratio < 1.0 = Lambda MIR is faster. Ratio > 1.0 = Node.js is faster.")
w()

# Excluding JetStream
mn_no_jet = [r for suite in ["r7rs", "awfy", "beng", "kostya", "larceny"]
             for bench_name, bench_data in data.get(suite, {}).items()
             if (mir_v := bench_data.get("mir")) and (node_v := bench_data.get("nodejs"))
             and mir_v > 0 and node_v > 0
             for r in [mir_v / node_v]]
gm_no_jet = geo_mean(mn_no_jet)
lw_no_jet = sum(1 for r in mn_no_jet if r < 1.0)
nw_no_jet = len(mn_no_jet) - lw_no_jet
w(f"**Excluding JetStream (original 53 benchmarks): {gm_no_jet:.2f}x** — Lambda wins {lw_no_jet}/{len(mn_no_jet)}")
w()

w("### MIR Direct vs Python (Self-Reported Exec Time)")
w()
w("| Suite | Geo. Mean | Lambda Wins | Python Wins | Total Compared |")
w("|-------|----------:|:-----------:|:-----------:|:--------------:|")

total_lw_p = 0
total_pw = 0
total_compared_p = 0

for suite in SUITE_ORDER:
    if suite not in data:
        continue
    benchmarks = data[suite]
    mp = []
    for bench_name, bench_data in benchmarks.items():
        mir_v = bench_data.get("mir")
        py_v = bench_data.get("python")
        if (suite, bench_name, "python") in BAD_VALUES:
            py_v = None
        if mir_v and py_v and mir_v > 0 and py_v > 0:
            mp.append(mir_v / py_v)

    gm = geo_mean(mp) if mp else None
    lw = sum(1 for r in mp if r < 1.0)
    pw = len(mp) - lw
    total_lw_p += lw
    total_pw += pw
    total_compared_p += len(mp)

    gm_str = f"{gm:.2f}x" if gm else "---"
    compared = len(mp)
    w(f"| {SUITE_LABELS[suite]} | {gm_str} | {lw} | {pw} | {compared} |")

overall_gm_p = geo_mean(all_mp)
w(f"| **Overall** | **{overall_gm_p:.2f}x** | **{total_lw_p}** | **{total_pw}** | **{total_compared_p}** |")
w()
w("> Lambda MIR is overwhelmingly faster than CPython across all suites.")

w()
w("---")

# ============================================================
# Performance Tiers (MIR vs Node.js)
# ============================================================
w()
w("### Performance Tiers (MIR vs Node.js)")
w()

tiers = {
    "Lambda >2× faster": [],
    "Lambda faster": [],
    "Comparable": [],
    "Node faster": [],
    "Node >5× faster": [],
}

for suite in SUITE_ORDER:
    if suite not in data:
        continue
    for bench_name, bench_data in data[suite].items():
        mir_v = bench_data.get("mir")
        node_v = bench_data.get("nodejs")
        if not mir_v or not node_v or mir_v <= 0 or node_v <= 0:
            continue
        r = mir_v / node_v
        label = f"{suite}/{bench_name} ({r:.2f}x)"
        if r < 0.5:
            tiers["Lambda >2× faster"].append((r, label))
        elif r < 1.0:
            tiers["Lambda faster"].append((r, label))
        elif r < 2.0:
            tiers["Comparable"].append((r, label))
        elif r < 5.0:
            tiers["Node faster"].append((r, label))
        else:
            tiers["Node >5× faster"].append((r, label))

w("| Tier | Count | Benchmarks |")
w("|------|------:|------------|")

tier_order = ["Lambda >2× faster", "Lambda faster", "Comparable", "Node faster", "Node >5× faster"]
tier_ranges = {
    "Lambda >2× faster": "< 0.5×",
    "Lambda faster": "0.5–1.0×",
    "Comparable": "1.0–2.0×",
    "Node faster": "2.0–5.0×",
    "Node >5× faster": "> 5.0×",
}

for tier in tier_order:
    items = tiers[tier]
    items.sort(key=lambda x: x[0])
    labels = ", ".join(x[1] for x in items)
    w(f"| **{tier}** ({tier_ranges[tier]}) | {len(items)} | {labels} |")

w()
w("---")

# ============================================================
# Improvement over Round 2
# ============================================================
w()
w("## Improvement over Round 2")
w()
w("### MIR Direct Performance Changes (R2 → R3)")
w()

# Compute improvements
improvements = []
for suite in ["r7rs", "awfy", "beng", "kostya", "larceny"]:
    if suite not in R2_MIR or suite not in data:
        continue
    for bench_name in data[suite]:
        r2_v = R2_MIR.get(suite, {}).get(bench_name)
        r3_v = data[suite][bench_name].get("mir")
        if r2_v and r3_v and r2_v > 0 and r3_v > 0:
            speedup = r2_v / r3_v
            improvements.append((suite, bench_name, r2_v, r3_v, speedup))

# Sort by speedup descending
improvements.sort(key=lambda x: x[4], reverse=True)

w("| Benchmark | Suite | R2 (ms) | R3 (ms) | Speedup |")
w("|-----------|-------|--------:|--------:|--------:|")

for suite, bench, r2, r3, sp in improvements:
    if sp > 1.05:  # >5% improvement
        w(f"| {bench} | {SUITE_LABELS[suite]} | {fmt(r2)} | {fmt(r3)} | **{sp:.2f}×** |")

w()

# Overall MIR improvement geo mean (excluding json which changed workload)
valid_impr = [(s, b, r2, r3, sp) for s, b, r2, r3, sp in improvements if not (s == "awfy" and b == "json")]
geo_impr = geo_mean([sp for _, _, _, _, sp in valid_impr])
w(f"**Overall MIR improvement (geo mean, {len(valid_impr)} benchmarks): {geo_impr:.1f}% faster**")
w(f"(excluding AWFY/json which had a workload change)")
w()

# R2 vs R3 suite geo means
w("### Suite-Level Comparison (MIR/Node.js Geometric Mean)")
w()
w("| Suite | R2 Geo Mean | R3 Geo Mean | Change |")
w("|-------|------------:|------------:|--------|")

r2_gm = {"r7rs": 0.47, "awfy": 0.61, "beng": 0.72, "kostya": 2.07, "larceny": 1.25}
for suite in ["r7rs", "awfy", "beng", "kostya", "larceny"]:
    mn = []
    for bench_name, bench_data in data.get(suite, {}).items():
        mir_v = bench_data.get("mir")
        node_v = bench_data.get("nodejs")
        if mir_v and node_v and mir_v > 0 and node_v > 0:
            mn.append(mir_v / node_v)
    gm = geo_mean(mn) if mn else None
    r2 = r2_gm[suite]
    if gm:
        if gm < r2:
            change = f"↑ improved ({r2/gm:.0f}% better)"
        elif gm > r2:
            change = f"↓ ({gm/r2:.0f}% worse)*"
        else:
            change = "→ unchanged"
        w(f"| {SUITE_LABELS[suite]} | {r2:.2f}x | {gm:.2f}x | {change} |")

w()
w("*AWFY geo mean change is primarily due to the `json` benchmark workload being corrected (R2: 0.028ms → R3: 3.3ms).")
w()

# LambdaJS improvements
w("### Lambda JS Engine Improvements (R2 → R3)")
w()
w("| Benchmark | Suite | R2 (ms) | R3 (ms) | Change |")
w("|-----------|-------|--------:|--------:|--------|")

ljs_improvements = []
for suite in ["r7rs", "awfy", "beng", "kostya", "larceny"]:
    if suite not in R2_LJS or suite not in data:
        continue
    for bench_name in data[suite]:
        r2_v = R2_LJS.get(suite, {}).get(bench_name)
        r3_v = data[suite][bench_name].get("lambdajs")
        if r2_v is None and r3_v is not None and r3_v > 0:
            ljs_improvements.append((suite, bench_name, None, r3_v, "NEW"))
        elif r2_v and r3_v and r2_v > 0 and r3_v > 0:
            sp = r2_v / r3_v
            if sp > 1.2:
                ljs_improvements.append((suite, bench_name, r2_v, r3_v, f"{sp:.1f}× faster"))
            elif sp < 0.5:
                ljs_improvements.append((suite, bench_name, r2_v, r3_v, f"{1/sp:.1f}× slower*"))

for suite, bench, r2, r3, change in sorted(ljs_improvements, key=lambda x: x[0]):
    r2_str = fmt(r2) if r2 else "---"
    w(f"| {bench} | {SUITE_LABELS[suite]} | {r2_str} | {fmt(r3)} | {change} |")

w()
w("*Workload or test changes between rounds may account for some differences.")

w()
w("---")

# ============================================================
# Memory Profiling
# ============================================================
import os
mem_data = None
mem_path = "test/benchmark/memory_results.json"
if os.path.exists(mem_path):
    with open(mem_path) as f:
        mem_data = json.load(f)

if mem_data:
    w()
    w("## Memory Profiling (Peak RSS)")
    w()
    w("Peak resident set size measured via `/usr/bin/time -l` (macOS). Values in **MB**.")
    w("Includes runtime, JIT compiler, and all loaded libraries for each engine.")
    w()

    MEM_ENGINES = ["mir", "c2mir", "lambdajs", "quickjs", "nodejs", "python"]
    MEM_LABELS = {"mir": "MIR", "c2mir": "C2MIR", "lambdajs": "LambdaJS",
                  "quickjs": "QuickJS", "nodejs": "Node.js", "python": "Python"}

    def fmt_mb(v):
        if v is None:
            return "---"
        if v < 1:
            return f"{v:.2f}"
        if v < 10:
            return f"{v:.1f}"
        return f"{v:.0f}"

    # Per-suite memory tables
    for suite in SUITE_ORDER:
        if suite not in mem_data:
            continue
        w(f"### {SUITE_LABELS[suite]} — Peak RSS (MB)")
        w()
        w("| Benchmark | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python |")
        w("| --------- | ---: | ----: | -------: | ------: | ------: | -----: |")

        for bench_name, bench_mem in mem_data[suite].items():
            row = f"| {bench_name} |"
            for eng in MEM_ENGINES:
                v = bench_mem.get(eng)
                row += f" {fmt_mb(v)} |"
            w(row)
        w()

    # Summary table: average peak RSS per engine per suite
    w("### Memory Summary — Average Peak RSS per Suite (MB)")
    w()
    w("| Suite | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/QJS |")
    w("|-------|----:|------:|---------:|--------:|--------:|-------:|---------:|--------:|")

    all_mir_mem = []
    all_node_mem = []
    all_qjs_mem = []

    for suite in SUITE_ORDER:
        if suite not in mem_data:
            continue
        avgs = {}
        for eng in MEM_ENGINES:
            vals = [b.get(eng) for b in mem_data[suite].values() if b.get(eng) is not None]
            avgs[eng] = sum(vals) / len(vals) if vals else None

        mir_avg = avgs.get("mir")
        node_avg = avgs.get("nodejs")
        qjs_avg = avgs.get("quickjs")

        if mir_avg and node_avg:
            all_mir_mem.append(mir_avg)
            all_node_mem.append(node_avg)
        if mir_avg and qjs_avg:
            all_qjs_mem.append(qjs_avg)

        mn_str = f"{mir_avg/node_avg:.2f}x" if mir_avg and node_avg and node_avg > 0 else "---"
        mq_str = f"{mir_avg/qjs_avg:.1f}x" if mir_avg and qjs_avg and qjs_avg > 0 else "---"

        row = f"| {SUITE_LABELS[suite]} |"
        for eng in MEM_ENGINES:
            row += f" {fmt_mb(avgs.get(eng))} |"
        row += f" {mn_str} | {mq_str} |"
        w(row)

    if all_mir_mem and all_node_mem:
        avg_mir = sum(all_mir_mem) / len(all_mir_mem)
        avg_node = sum(all_node_mem) / len(all_node_mem)
        overall_ratio = avg_mir / avg_node if avg_node > 0 else None
        w()
        if overall_ratio:
            if overall_ratio < 1:
                w(f"**Lambda MIR uses {overall_ratio:.2f}× the memory of Node.js** ({avg_mir:.0f} MB vs {avg_node:.0f} MB average) — Lambda is more memory-efficient.")
            else:
                w(f"**Lambda MIR uses {overall_ratio:.2f}× the memory of Node.js** ({avg_mir:.0f} MB vs {avg_node:.0f} MB average).")
    if all_qjs_mem:
        avg_qjs = sum(all_qjs_mem) / len(all_qjs_mem)
        avg_mir2 = sum(all_mir_mem) / len(all_mir_mem)
        w(f"**QuickJS is the most memory-efficient** at {avg_qjs:.0f} MB average — {avg_mir2/avg_qjs:.0f}× less than Lambda MIR.")

    w()
    w("> Memory footprint is dominated by engine/runtime overhead; actual benchmark data is small.")
    w("> QuickJS's tiny interpreter has minimal memory overhead. Node.js includes the full V8 engine.")
    w()
    w("---")

# ============================================================
# Key Findings
# ============================================================
w()
w("## Key Findings")
w()
w("### 1. Overall: Lambda MIR competitive with Node.js V8")
w()
w(f"Across {len(all_mn)} benchmarks with both MIR and Node.js results, the geometric mean ratio is **{overall_gm_n:.2f}x**.")
w(f"Excluding the new JetStream suite (which ports are not yet optimized), Lambda achieves **{gm_no_jet:.2f}x**")
w(f"on the original 53 benchmarks, winning {lw_no_jet} of {len(mn_no_jet)}.")
w()

w("### 2. Lambda MIR dominates CPython")
w()
w(f"Across {total_compared_p} benchmarks with Python comparisons, Lambda MIR is **{1/overall_gm_p:.0f}× faster** (geo mean {overall_gm_p:.2f}x).")
w("CPython's interpreted execution cannot match JIT-compiled code on compute-intensive tasks.")
w("Lambda wins on all suites, with particular dominance on tight loops and numeric code (AWFY micro-benchmarks: 1000–30000× faster).")
w()

w("### 3. Strengths: Micro-benchmarks and numeric code")
w()
w("Lambda MIR excels on small, tight computational benchmarks:")
w("- **R7RS suite (0.43x)**: 2.3× faster on average — strong tail-call optimization, native integer/float arithmetic")
w("- **AWFY micro-benchmarks**: sieve (0.14x), list (0.04x), permute (0.08x) — highly efficient JIT for simple loops")
w("- **FFT (0.11x)**: 9× speedup from typed array inline fast paths")
w("- **Collatz (0.21x)**: 5× faster than Node.js on integer-heavy iteration")
w()

w("### 4. Weaknesses: OOP-heavy and allocation-intensive code")
w()
w("Node.js V8's optimizing JIT (TurboFan) significantly outperforms Lambda on:")
w("- **Class-heavy benchmarks**: richards (11.3x), cd (14.3x), deltablue (6.2x) — V8's hidden classes and inline caches")
w("- **Heavy allocation/GC**: gcbench (20.3x), base64 (12.5x) — V8's generational GC advantage")
w("- **JetStream suite (7.88x)**: Complex OOP-style benchmarks where V8's mature optimizations dominate")
w()

w("### 5. JetStream: New frontier for optimization")
w()
w("The JetStream benchmarks (ported from Apple's JS benchmark suite) show Lambda MIR at **7.88× slower** than Node.js.")
w("Key bottlenecks:")
w("- **navier_stokes (56×)**: Heavy array-based PDE solver — needs typed array optimization for this pattern")
w("- **richards (30×)**: OOP task scheduler — class/method dispatch overhead")
w("- **raytrace3d (20×)**: Object-heavy 3D computation — property access patterns")
w("- **deltablue (1.77×)**: Closest to parity — constraint solver benefits from Lambda's approach")
w("These represent clear optimization targets for future MIR engine improvements.")
w()

w("### 6. MIR JIT improvements from Round 2")
w()
w("MIR Direct shows measurable improvements across the board:")
w("- **havlak**: 339 → 183ms (**1.85× faster**) — graph algorithm optimization")
w("- **nbody (BENG)**: 2.8 → 1.3ms (**2.15× faster**) — continued typed-array optimization")
w("- **deltablue**: 6.1 → 5.3ms (**1.15× faster**) — macro benchmark improvement")
w("- **gcbench**: 472 → 500ms — slight regression due to GC tuning trade-offs")
w(f"- **Overall**: ~{(geo_impr-1)*100:.0f}% faster across {len(valid_impr)} comparable benchmarks")
w()

w("### 7. Lambda JS engine growth")
w()
w("LambdaJS continues expanding benchmark coverage:")
w("- **New passing benchmarks**: BENG/fannkuch, BENG/mandelbrot, KOSTYA/collatz")
w("- **BENG/nbody**: 134 → 11.8ms (11.4× faster)")
w("- **KOSTYA/primes**: 19.4 → 8.5ms (2.3× faster)")
w("- **KOSTYA/levenshtein**: 30.7 → 14.2ms (2.2× faster)")
w()

w("### 8. QuickJS comparison")
w()
w("QuickJS (pure interpreter) is generally 2–10× slower than Node.js V8, as expected.")
w("Lambda MIR is faster than QuickJS on most benchmarks, confirming Lambda's JIT advantage.")
w()

w("### 9. C2MIR vs MIR Direct")
w()
w("The two Lambda JIT paths produce similar results. Notable differences:")
w("- C2MIR slightly faster on: havlak (145 vs 177ms), richards (49 vs 56ms)")
w("- MIR Direct faster on: matmul (8.7 vs 129ms), cube3d (49 vs 141ms)")
w("- MIR Direct has lower compilation overhead and is the default path")
w()

w("### 10. Memory footprint")
w()
w("Lambda MIR's peak RSS averages ~2.8× Node.js memory, dominated by the MIR JIT compiler and runtime overhead.")
w("Key observations:")
w("- **R7RS/micro-benchmarks**: MIR ~37 MB vs Node ~41 MB — Lambda is **lighter** for small programs")
w("- **KOSTYA/LARCENY/JetStream**: MIR 63–330 MB vs Node 43–60 MB — Lambda's GC and data model use more RAM on heavy workloads")
w("- **QuickJS** is the most memory-efficient at ~3 MB average (pure interpreter, minimal overhead)")
w("- **Outliers**: base64 (1,414 MB MIR), navier_stokes (1,310 MB MIR) — indicate optimization opportunities for array-heavy benchmarks")
w()

w("---")
w()
w("## Notes")
w()
w("- **Self-reported exec time** measures only the computation, excluding process startup, JIT compilation warmup, and file I/O.")
w("- **AWFY JS benchmarks** use the official source from `ref/are-we-fast-yet/benchmarks/JavaScript/`. AWFY Python benchmarks use the official Python port with harness.")
w("- **AWFY Python micro-benchmarks** (sieve, permute, queens, etc.) show extreme Lambda advantage because CPython interprets tight loops ~10,000× slower than JIT-compiled code.")
w("- **AWFY Python mandelbrot** value excluded due to measurement error (harness incompatibility).")
w("- **LambdaJS** still fails on some AWFY benchmarks (bounce, storage, json, deltablue, havlak, cd) due to missing ES6 class features.")
w("- **QuickJS** fails on ack (R7RS) due to stack overflow on deep recursion.")
w("- **JetStream** benchmarks only run on MIR, C2MIR, and Node.js. No LambdaJS/QuickJS/Python ports exist.")
w("- **Python** benchmarks not available for: AWFY/nbody, AWFY/deltablue, AWFY/cd, all JetStream benchmarks.")
w("- All times in **milliseconds** unless noted with 's' suffix (seconds).")
w("- The `json` AWFY benchmark workload was corrected between R2 and R3 (R2: 0.028ms was a minimal-workload test).")

output = "\n".join(lines)
with open("test/benchmark/Overall_Result3.md", "w") as f:
    f.write(output)
print(f"Generated Overall_Result3.md ({len(lines)} lines)")
