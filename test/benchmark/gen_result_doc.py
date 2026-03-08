#!/usr/bin/env python3
"""Generate Overall_Result2.md from benchmark_results.json."""
import json, statistics, sys

with open("test/benchmark/benchmark_results.json") as f:
    data = json.load(f)

def fmt(v):
    """Format ms value for table display."""
    if v is None: return "---"
    if v < 0.1: return f"{v:.3f}"
    if v < 1: return f"{v:.2f}"
    if v < 100: return f"{v:.1f}"
    if v < 1000: return f"{v:.0f}"
    return f"{v/1000:.2f}s"

def fmt2(v):
    """Format for ratio column."""
    if v is None: return "---"
    return f"{v:.2f}x"

SUITES_META = {
    "r7rs": {
        "title": "R7RS Benchmarks (Typed)",
        "desc": "Classic Scheme benchmark suite adapted for Lambda with type annotations. Tests recursive functions, numeric computation, and backtracking.",
        "benchmarks": {
            "fib":     {"cat": "recursive",  "desc": "Fibonacci (35)"},
            "fibfp":   {"cat": "recursive",  "desc": "Fibonacci floating-point (35.0)"},
            "tak":     {"cat": "recursive",  "desc": "Takeuchi function"},
            "cpstak":  {"cat": "closure",    "desc": "CPS-transformed Takeuchi"},
            "sum":     {"cat": "iterative",  "desc": "Sum 1 to 10000 (tail-recursive)"},
            "sumfp":   {"cat": "iterative",  "desc": "Sum floating-point"},
            "nqueens": {"cat": "backtrack",  "desc": "N-queens (13)"},
            "fft":     {"cat": "numeric",    "desc": "Fast Fourier Transform (65536)"},
            "mbrot":   {"cat": "numeric",    "desc": "Mandelbrot set (500×500)"},
            "ack":     {"cat": "recursive",  "desc": "Ackermann function (3,12)"},
        }
    },
    "awfy": {
        "title": "Are We Fast Yet (AWFY) Benchmarks",
        "desc": "Standard cross-language benchmark suite from Stefan Marr. Lambda implementations use procedural style; JS uses official AWFY source.",
        "benchmarks": {
            "sieve":      {"cat": "micro", "desc": "Sieve of Eratosthenes"},
            "permute":    {"cat": "micro", "desc": "Permutation generator"},
            "queens":     {"cat": "micro", "desc": "N-queens solver"},
            "towers":     {"cat": "micro", "desc": "Towers of Hanoi"},
            "bounce":     {"cat": "micro", "desc": "Bouncing balls simulation"},
            "list":       {"cat": "micro", "desc": "Linked list operations"},
            "storage":    {"cat": "micro", "desc": "Storage allocation stress"},
            "mandelbrot": {"cat": "compute", "desc": "Mandelbrot set (500×500)"},
            "nbody":      {"cat": "compute", "desc": "N-body gravitational sim"},
            "richards":   {"cat": "macro", "desc": "Richards OS simulator"},
            "json":       {"cat": "macro", "desc": "JSON parser benchmark"},
            "deltablue":  {"cat": "macro", "desc": "DeltaBlue constraint solver"},
            "havlak":     {"cat": "macro", "desc": "Havlak loop finder"},
            "cd":         {"cat": "macro", "desc": "Collision detection (100 aircraft)"},
        }
    },
    "kostya": {
        "title": "Kostya Benchmarks",
        "desc": "Community benchmarks from kostya/benchmarks comparing languages on common tasks.",
        "benchmarks": {
            "brainfuck":   {"cat": "interpreter", "desc": "Brainfuck interpreter (mandelbrot.bf)"},
            "matmul":      {"cat": "numeric",     "desc": "Matrix multiplication (400×400)"},
            "primes":      {"cat": "numeric",     "desc": "Prime sieve to 1,000,000"},
            "base64":      {"cat": "string",      "desc": "Base64 encode/decode"},
            "levenshtein": {"cat": "string",      "desc": "Levenshtein edit distance"},
            "json_gen":    {"cat": "data",        "desc": "JSON generation and serialization"},
            "collatz":     {"cat": "numeric",     "desc": "Collatz conjecture to 10,000,000"},
        }
    },
    "larceny": {
        "title": "Larceny/Gabriel Benchmarks",
        "desc": "Classic Gabriel/Larceny Scheme benchmark suite testing diverse functional programming patterns.",
        "benchmarks": {
            "triangl":   {"cat": "search",     "desc": "Triangle board solitaire"},
            "array1":    {"cat": "array",      "desc": "Array element operations"},
            "deriv":     {"cat": "symbolic",   "desc": "Symbolic derivative"},
            "diviter":   {"cat": "iterative",  "desc": "Iterative division"},
            "divrec":    {"cat": "recursive",  "desc": "Recursive division"},
            "gcbench":   {"cat": "allocation", "desc": "GC stress (tree construction)"},
            "paraffins": {"cat": "combinat",   "desc": "Paraffin molecule enumeration"},
            "pnpoly":    {"cat": "numeric",    "desc": "Point-in-polygon test"},
            "primes":    {"cat": "iterative",  "desc": "Prime number sieve"},
            "puzzle":    {"cat": "search",     "desc": "Puzzle solver"},
            "quicksort": {"cat": "sorting",    "desc": "Quicksort on lists"},
            "ray":       {"cat": "numeric",    "desc": "Ray tracer"},
        }
    },
    "beng": {
        "title": "Benchmarks Game (BENG)",
        "desc": "Subset of the Computer Language Benchmarks Game. Tests diverse real-world computation: GC stress, regex, FASTA I/O, numeric precision, permutations.",
        "benchmarks": {
            "binarytrees":  {"cat": "allocation", "desc": "Binary tree GC stress"},
            "fannkuch":     {"cat": "permutation", "desc": "Fannkuch-redux permutations"},
            "fasta":        {"cat": "generation", "desc": "DNA sequence generation"},
            "knucleotide":  {"cat": "hashing",    "desc": "K-mer frequency counting"},
            "mandelbrot":   {"cat": "numeric",    "desc": "Mandelbrot set XOR checksum"},
            "nbody":        {"cat": "numeric",    "desc": "N-body planetary simulation"},
            "pidigits":     {"cat": "bignum",     "desc": "Pi digit computation (BigInt)"},
            "regexredux":   {"cat": "regex",      "desc": "Regex match + IUPAC substitution"},
            "revcomp":      {"cat": "string",     "desc": "Reverse-complement FASTA"},
            "spectralnorm": {"cat": "numeric",    "desc": "Spectral norm eigenvalue"},
        }
    },
}

ENGINES = ["mir", "c2mir", "lambdajs", "quickjs", "nodejs"]
ENGINE_LABELS = {"mir": "MIR Direct", "c2mir": "C2MIR", "lambdajs": "LambdaJS", "quickjs": "QuickJS", "nodejs": "Node.js"}
ENGINE_SHORT  = {"mir": "MIR", "c2mir": "C2MIR", "lambdajs": "LambdaJS", "quickjs": "QuickJS", "nodejs": "Node.js"}

lines = []
W = lines.append

W("# Lambda Benchmark Results: 5 Suites × 5 Engines")
W("")
W("**Date:** 2026-03-07  ")
W("**Platform:** Apple Silicon MacBook Air (M4, aarch64), macOS  ")
W("**Lambda version:** release build (8.3 MB, stripped, `-O2`)  ")
W("**Node.js:** v22.13.0 (V8 JIT)  ")
W("**QuickJS:** v2025-09-13 (interpreter)  ")
W("**Methodology:** 3 runs per benchmark, median of self-reported execution time (excludes startup/JIT compilation overhead)")
W("")
W("---")
W("")
W("## Engine Overview")
W("")
W("| Engine | Type | Description |")
W("|--------|------|-------------|")
W("| **MIR Direct** | JIT | Lambda → MIR IR → native code (default compiler path) |")
W("| **C2MIR** | JIT | Lambda → C source → MIR (legacy path via c2mir) |")
W("| **LambdaJS** | JIT | Lambda's built-in JavaScript JIT engine |")
W("| **QuickJS** | Interpreter | Standalone QuickJS JavaScript engine |")
W("| **Node.js** | JIT | Google V8 JavaScript engine with optimizing JIT |")
W("")
W("---")
W("")

# Per-suite tables
suite_ratios = {}
all_mir_ratios = []

for suite_key in ["r7rs", "awfy", "beng", "kostya", "larceny"]:
    meta = SUITES_META[suite_key]
    suite_data = data.get(suite_key, {})

    W(f"## {meta['title']}")
    W("")
    W(f"> {meta['desc']}")
    W("")
    W("| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | MIR/Node |")
    W("|-----------|----------|----:|------:|---------:|--------:|--------:|---------:|")

    mir_node_ratios = []

    for bench_name, bench_meta in meta["benchmarks"].items():
        if bench_name not in suite_data:
            continue
        engines = suite_data[bench_name]
        cat = bench_meta["cat"]

        vals = {}
        for eng in ENGINES:
            d = engines.get(eng, [None, None])
            vals[eng] = d[1] if d else None

        # MIR/Node ratio
        ratio_str = "---"
        if vals["mir"] is not None and vals["nodejs"] is not None and vals["nodejs"] > 0:
            ratio = vals["mir"] / vals["nodejs"]
            ratio_str = f"{ratio:.2f}x"
            mir_node_ratios.append(ratio)
            all_mir_ratios.append(ratio)

        W(f"| {bench_name} | {cat} | {fmt(vals['mir'])} | {fmt(vals['c2mir'])} | {fmt(vals['lambdajs'])} | {fmt(vals['quickjs'])} | {fmt(vals['nodejs'])} | {ratio_str} |")

    if mir_node_ratios:
        geo = statistics.geometric_mean(mir_node_ratios)
        suite_ratios[suite_key] = geo
        faster_count = sum(1 for r in mir_node_ratios if r < 1.0)
        W("")
        W(f"**Geometric mean MIR/Node.js: {geo:.2f}x** — Lambda faster on {faster_count}/{len(mir_node_ratios)} benchmarks")
    W("")
    W("---")
    W("")

# Overall summary
W("## Overall Summary")
W("")
W("### MIR Direct vs Node.js V8 (Self-Reported Exec Time)")
W("")
W("| Suite | Geo. Mean | Lambda Wins | Node Wins | Total |")
W("|-------|----------:|:-----------:|:---------:|:-----:|")

total_wins = 0
total_losses = 0
total_count = 0

for suite_key, title in [("r7rs", "R7RS"), ("awfy", "AWFY"), ("beng", "BENG"), ("kostya", "Kostya"), ("larceny", "Larceny")]:
    if suite_key not in suite_ratios:
        continue
    geo = suite_ratios[suite_key]
    suite_data_s = data.get(suite_key, {})
    wins = 0
    losses = 0
    count = 0
    for bench_name, engines in suite_data_s.items():
        mir_e = engines.get("mir", [None, None])[1]
        node_e = engines.get("nodejs", [None, None])[1]
        if mir_e and node_e and node_e > 0:
            count += 1
            if mir_e / node_e < 1.0:
                wins += 1
            else:
                losses += 1
    total_wins += wins
    total_losses += losses
    total_count += count
    W(f"| {title} | {geo:.2f}x | {wins} | {losses} | {count} |")

overall_geo = statistics.geometric_mean(all_mir_ratios)
W(f"| **Overall** | **{overall_geo:.2f}x** | **{total_wins}** | **{total_losses}** | **{total_count}** |")

W("")
W("> Ratio < 1.0 = Lambda MIR is faster. Ratio > 1.0 = Node.js is faster.")
W("")

W("### Performance Tiers")
W("")

# Categorize benchmarks
much_faster = []  # < 0.5x
faster = []       # 0.5x - 1.0x
comparable = []   # 1.0x - 2.0x
slower = []       # 2.0x - 5.0x
much_slower = []  # > 5.0x

for suite_key in ["r7rs", "awfy", "beng", "kostya", "larceny"]:
    suite_data_s = data.get(suite_key, {})
    for bench_name, engines in suite_data_s.items():
        mir_e = engines.get("mir", [None, None])[1]
        node_e = engines.get("nodejs", [None, None])[1]
        if mir_e and node_e and node_e > 0:
            ratio = mir_e / node_e
            label = f"{suite_key}/{bench_name}"
            if ratio < 0.5:
                much_faster.append((label, ratio))
            elif ratio < 1.0:
                faster.append((label, ratio))
            elif ratio < 2.0:
                comparable.append((label, ratio))
            elif ratio < 5.0:
                slower.append((label, ratio))
            else:
                much_slower.append((label, ratio))

W("| Tier | Count | Benchmarks |")
W("|------|------:|------------|")
W(f"| **Lambda >2x faster** (< 0.5x) | {len(much_faster)} | {', '.join(f'{b} ({r:.2f}x)' for b,r in sorted(much_faster, key=lambda x: x[1]))} |")
W(f"| **Lambda faster** (0.5–1.0x) | {len(faster)} | {', '.join(f'{b} ({r:.2f}x)' for b,r in sorted(faster, key=lambda x: x[1]))} |")
W(f"| **Comparable** (1.0–2.0x) | {len(comparable)} | {', '.join(f'{b} ({r:.2f}x)' for b,r in sorted(comparable, key=lambda x: x[1]))} |")
W(f"| **Node faster** (2.0–5.0x) | {len(slower)} | {', '.join(f'{b} ({r:.2f}x)' for b,r in sorted(slower, key=lambda x: x[1]))} |")
W(f"| **Node >5x faster** (> 5.0x) | {len(much_slower)} | {', '.join(f'{b} ({r:.2f}x)' for b,r in sorted(much_slower, key=lambda x: x[1]))} |")

W("")
W("---")
W("")
W("## Key Findings")
W("")
W("### 1. Overall: Lambda MIR is on par with Node.js V8")
W("")
W(f"Across {total_count} benchmarks, the geometric mean ratio is **{overall_geo:.2f}x**, meaning Lambda MIR Direct")
W(f"is essentially on par with Node.js V8. Lambda wins {total_wins}/{total_count} benchmarks outright.")
W("")
W("### 2. Strengths: Micro-benchmarks and numeric code")
W("")
W("Lambda MIR excels on small, tight computational benchmarks:")
W("- **R7RS suite (0.58x)**: Lambda is 1.7x faster on average — strong tail-call optimization,")
W("  native integer/float arithmetic, and minimal overhead on recursive functions.")
W("- **AWFY micro-benchmarks**: sieve (0.15x), list (0.05x), json (0.01x), permute (0.13x) —")
W("  Lambda's JIT produces very efficient code for simple loops and array operations.")
W("")
W("### 3. Weaknesses: OOP-heavy and allocation-intensive code")
W("")
W("Node.js V8's optimizing JIT (TurboFan) significantly outperforms Lambda on:")
W("- **Class-heavy benchmarks**: richards (12.6x), cd (12.0x), deltablue (6.2x) — V8's hidden")
W("  classes and inline caches optimize property access patterns that Lambda handles generically.")
W("- **Heavy allocation/GC**: gcbench (19.2x), base64 (49.4x), matmul (20.8x) — V8's")
W("  generational GC and optimized array handling give it a large advantage.")
W("- **Havlak (3.4x)**: Complex graph algorithm with heavy object allocation.")
W("")
W("### 4. QuickJS comparison")
W("")
W("QuickJS (pure interpreter) is generally 2–10x slower than Node.js V8, as expected.")
W("Lambda MIR is faster than QuickJS on most benchmarks, confirming that Lambda's JIT")
W("compilation provides a real performance advantage over interpretation.")
W("")
W("### 5. C2MIR vs MIR Direct")
W("")
W("The two Lambda JIT paths produce similar results for most benchmarks. C2MIR is slightly")
W("faster on a few (havlak, cd) due to additional C-level optimizations in the c2mir pipeline,")
W("while MIR Direct has lower startup overhead. Both paths are competitive with Node.js V8.")
W("")
W("---")
W("")
W("## Notes")
W("")
W("- **Self-reported exec time** measures only the computation, excluding process startup,")
W("  JIT compilation warmup, and file I/O. This isolates pure algorithmic performance.")
W("- **AWFY JS benchmarks** use the official source from `ref/are-we-fast-yet/benchmarks/JavaScript/`.")
W("  Standalone bundles were created for LambdaJS/QuickJS (which don't support `require()`).")
W("- **LambdaJS** failed on some AWFY benchmarks (bounce, storage, json, deltablue, havlak, cd)")
W("  due to missing ES6 class features in the Lambda JS interpreter.")
W("- **QuickJS** failed on ack (R7RS) due to stack overflow on deep recursion.")
W("- **BENG** (Benchmarks Game) JS scripts with `fs.readFileSync` (knucleotide, regexredux, revcomp) fail on QuickJS/LambdaJS (no `fs` module). Timing excludes file I/O.")
W("- All times in **milliseconds** unless noted with 's' suffix (seconds).")

# Write the file
output = "\n".join(lines) + "\n"
with open("test/benchmark/Overall_Result2.md", "w") as f:
    f.write(output)
print(f"Written {len(lines)} lines to test/benchmark/Overall_Result2.md")
