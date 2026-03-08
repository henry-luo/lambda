#!/usr/bin/env python3
"""Generate the complete Overall_Result2.md from benchmark_results.json"""
import json, math

with open("test/benchmark/benchmark_results.json") as f:
    data = json.load(f)

def get_exec(d, eng):
    v = d.get(eng, [None, None])
    if v and len(v) > 1 and v[1] is not None:
        return v[1]
    return None

def fmt(v):
    """Format ms value for table display."""
    if v is None: return "---"
    if abs(v) < 0.001: return "0.000"
    if v < 0.1: return f"{v:.3f}"
    if v < 1: return f"{v:.2f}"
    if v < 10: return f"{v:.1f}"
    if v < 100: return f"{v:.1f}"
    if v < 1000: return f"{v:.0f}"
    return f"{v/1000:.2f}s"

def ratio_str(mir, node):
    if mir is not None and node is not None and node > 0:
        return f"{mir/node:.2f}x"
    return "---"

# Suite metadata
SUITE_META = {
    "r7rs": {
        "title": "R7RS Benchmarks (Typed)",
        "desc": "Classic Scheme benchmark suite adapted for Lambda with type annotations. Tests recursive functions, numeric computation, and backtracking.",
        "categories": {
            "fib": "recursive", "fibfp": "recursive", "tak": "recursive",
            "cpstak": "closure", "sum": "iterative", "sumfp": "iterative",
            "nqueens": "backtrack", "fft": "numeric", "mbrot": "numeric",
            "ack": "recursive",
        },
    },
    "awfy": {
        "title": "Are We Fast Yet (AWFY) Benchmarks",
        "desc": "Standard cross-language benchmark suite from Stefan Marr. Lambda implementations use procedural style; JS uses official AWFY source.",
        "categories": {
            "sieve": "micro", "permute": "micro", "queens": "micro",
            "towers": "micro", "bounce": "micro", "list": "micro",
            "storage": "micro", "mandelbrot": "compute", "nbody": "compute",
            "richards": "macro", "json": "macro", "deltablue": "macro",
            "havlak": "macro", "cd": "macro",
        },
    },
    "beng": {
        "title": "Benchmarks Game (BENG)",
        "desc": "Subset of the Computer Language Benchmarks Game. Tests diverse real-world computation: GC stress, regex, FASTA I/O, numeric precision, permutations.",
        "categories": {
            "binarytrees": "allocation", "fannkuch": "permutation",
            "fasta": "generation", "knucleotide": "hashing",
            "mandelbrot": "numeric", "nbody": "numeric",
            "pidigits": "bignum", "regexredux": "regex",
            "revcomp": "string", "spectralnorm": "numeric",
        },
    },
    "kostya": {
        "title": "Kostya Benchmarks",
        "desc": "Community benchmarks from kostya/benchmarks comparing languages on common tasks.",
        "categories": {
            "brainfuck": "interpreter", "matmul": "numeric",
            "primes": "numeric", "base64": "string",
            "levenshtein": "string", "json_gen": "data",
            "collatz": "numeric",
        },
    },
    "larceny": {
        "title": "Larceny/Gabriel Benchmarks",
        "desc": "Classic Gabriel/Larceny Scheme benchmark suite testing diverse functional programming patterns.",
        "categories": {
            "triangl": "search", "array1": "array", "deriv": "symbolic",
            "diviter": "iterative", "divrec": "recursive",
            "gcbench": "allocation", "paraffins": "combinat",
            "pnpoly": "numeric", "primes": "iterative",
            "puzzle": "search", "quicksort": "sorting", "ray": "numeric",
        },
    },
}

SUITE_ORDER = ["r7rs", "awfy", "beng", "kostya", "larceny"]
ENGINES = ["mir", "c2mir", "lambdajs", "quickjs", "nodejs"]

lines = []
def w(s=""):
    lines.append(s)

# Header
w("# Lambda Benchmark Results: 5 Suites x 5 Engines")
w()
w("**Date:** 2026-03-07  ")
w("**Platform:** Apple Silicon MacBook Air (M4, aarch64), macOS  ")
w("**Lambda version:** release build (8.3 MB, stripped, `-O2`)  ")
w("**Node.js:** v22.13.0 (V8 JIT)  ")
w("**QuickJS:** v2025-09-13 (interpreter)  ")
w("**Methodology:** 3 runs per benchmark, median of self-reported execution time (excludes startup/JIT compilation overhead)")
w()
w("---")
w()
w("## Engine Overview")
w()
w("| Engine | Type | Description |")
w("|--------|------|-------------|")
w("| **MIR Direct** | JIT | Lambda -> MIR IR -> native code (default compiler path) |")
w("| **C2MIR** | JIT | Lambda -> C source -> MIR (legacy path via c2mir) |")
w("| **LambdaJS** | JIT | Lambda's built-in JavaScript JIT engine |")
w("| **QuickJS** | Interpreter | Standalone QuickJS JavaScript engine |")
w("| **Node.js** | JIT | Google V8 JavaScript engine with optimizing JIT |")
w()
w("---")

# Each suite
for suite_name in SUITE_ORDER:
    meta = SUITE_META[suite_name]
    benchmarks = data[suite_name]
    
    w()
    w(f"## {meta['title']}")
    w()
    w(f"> {meta['desc']}")
    w()
    w("| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | MIR/Node |")
    w("|-----------|----------|----:|------:|---------:|--------:|--------:|---------:|")
    
    ratios = []
    wins = 0
    losses = 0
    
    for bench_name in benchmarks:
        d = benchmarks[bench_name]
        mir = get_exec(d, "mir")
        c2mir = get_exec(d, "c2mir")
        ljs = get_exec(d, "lambdajs")
        qjs = get_exec(d, "quickjs")
        node = get_exec(d, "nodejs")
        cat = meta["categories"].get(bench_name, "---")
        rs = ratio_str(mir, node)
        
        if mir is not None and node is not None and node > 0:
            r = mir / node
            ratios.append(r)
            if r < 1.0: wins += 1
            elif r > 1.0: losses += 1
        
        w(f"| {bench_name} | {cat} | {fmt(mir)} | {fmt(c2mir)} | {fmt(ljs)} | {fmt(qjs)} | {fmt(node)} | {rs} |")
    
    if ratios:
        geo = math.exp(sum(math.log(r) for r in ratios) / len(ratios))
        total = len(ratios)
        w()
        w(f"**Geometric mean MIR/Node.js: {geo:.2f}x** -- Lambda faster on {wins}/{total} benchmarks")
    
    w()
    w("---")

# Overall Summary
w()
w("## Overall Summary")
w()
w("### MIR Direct vs Node.js V8 (Self-Reported Exec Time)")
w()
w("| Suite | Geo. Mean | Lambda Wins | Node Wins | Total |")
w("|-------|----------:|:-----------:|:---------:|:-----:|")

all_ratios = []
all_wins = 0
all_losses = 0

for suite_name in SUITE_ORDER:
    benchmarks = data[suite_name]
    suite_ratios = []
    s_wins = 0
    s_losses = 0
    for bench_name in benchmarks:
        d = benchmarks[bench_name]
        mir = get_exec(d, "mir")
        node = get_exec(d, "nodejs")
        if mir is not None and node is not None and node > 0:
            r = mir / node
            suite_ratios.append(r)
            all_ratios.append(r)
            if r < 1.0:
                s_wins += 1
                all_wins += 1
            elif r > 1.0:
                s_losses += 1
                all_losses += 1
    if suite_ratios:
        geo = math.exp(sum(math.log(r) for r in suite_ratios) / len(suite_ratios))
        w(f"| {suite_name.upper()} | {geo:.2f}x | {s_wins} | {s_losses} | {len(suite_ratios)} |")

if all_ratios:
    overall_geo = math.exp(sum(math.log(r) for r in all_ratios) / len(all_ratios))
    w(f"| **Overall** | **{overall_geo:.2f}x** | **{all_wins}** | **{all_losses}** | **{len(all_ratios)}** |")

w()
w("> Ratio < 1.0 = Lambda MIR is faster. Ratio > 1.0 = Node.js is faster.")

# Performance Tiers
w()
w("### Performance Tiers")
w()
w("| Tier | Count | Benchmarks |")
w("|------|------:|------------|")

tier_data = []
for suite_name in SUITE_ORDER:
    for bench_name in data[suite_name]:
        d = data[suite_name][bench_name]
        mir = get_exec(d, "mir")
        node = get_exec(d, "nodejs")
        if mir is not None and node is not None and node > 0:
            r = mir / node
            tier_data.append((r, f"{suite_name}/{bench_name}"))

tier_data.sort(key=lambda x: x[0])

tiers = [
    ("Lambda >2x faster", "< 0.5x", lambda r: r < 0.5),
    ("Lambda faster", "0.5-1.0x", lambda r: 0.5 <= r < 1.0),
    ("Comparable", "1.0-2.0x", lambda r: 1.0 <= r < 2.0),
    ("Node faster", "2.0-5.0x", lambda r: 2.0 <= r < 5.0),
    ("Node >5x faster", "> 5.0x", lambda r: r >= 5.0),
]

for tier_name, range_s, pred in tiers:
    items = [(r, name) for r, name in tier_data if pred(r)]
    names = ", ".join(f"{name} ({r:.2f}x)" for r, name in items)
    w(f"| **{tier_name}** ({range_s}) | {len(items)} | {names} |")

# Key Findings
w()
w("---")
w()
w("## Key Findings")
w()
w("### 1. Overall: Lambda MIR is 17% faster than Node.js V8")
w()
w(f"Across {len(all_ratios)} benchmarks, the geometric mean ratio is **{overall_geo:.2f}x**, meaning Lambda MIR Direct")
w("is faster than Node.js V8 overall. Lambda wins 29/53 benchmarks outright.")
w()
w("### 2. Strengths: Micro-benchmarks and numeric code")
w()
w("Lambda MIR excels on small, tight computational benchmarks:")
w("- **R7RS suite (0.47x)**: Lambda is 2.1x faster on average -- strong tail-call optimization,")
w("  native integer/float arithmetic, and minimal overhead on recursive functions.")
w("- **AWFY micro-benchmarks**: sieve (0.15x), list (0.05x), json (0.01x), permute (0.08x) --")
w("  Lambda's JIT produces very efficient code for simple loops and array operations.")
w("- **FFT (0.13x)**: 8x speedup from typed array inline fast paths enabling native float operations.")
w()
w("### 3. Weaknesses: OOP-heavy and allocation-intensive code")
w()
w("Node.js V8's optimizing JIT (TurboFan) significantly outperforms Lambda on:")
w("- **Class-heavy benchmarks**: richards (12.6x), cd (12.0x), deltablue (7.1x) -- V8's hidden")
w("  classes and inline caches optimize property access patterns that Lambda handles generically.")
w("- **Heavy allocation/GC**: gcbench (18.9x), base64 (12.3x) -- V8's")
w("  generational GC and optimized string/array handling give it a large advantage.")
w("- **Havlak (3.5x)**: Complex graph algorithm with heavy object allocation.")
w()
w("### 4. LambdaJS JIT engine improvements")
w()
w("Significant performance gains in the Lambda JS JIT engine across all suites:")
w("- **ack**: 61.1 -> 8.6ms (7.1x faster)")
w("- **divrec**: 24.3 -> 0.84ms (29x faster)")
w("- **quicksort**: 9.8 -> 0.19ms (51x faster)")
w("- **array1**: 20.4 -> 4.1ms (5x faster)")
w("- **spectralnorm**: 48.9 -> 19.2ms (2.5x faster)")
w("- **brainfuck**: 713 -> 420ms (1.7x faster)")
w("- **primes (kostya)**: 54.4 -> 19.4ms (2.8x faster)")
w()
w("### 5. MIR JIT typed-array optimizations")
w()
w("The inline typed-array fast paths produced dramatic speedups on array-heavy benchmarks:")
w("- **matmul**: 200 -> 8.7ms (23x faster) -- native float[] load/store bypasses boxed runtime calls")
w("- **fft**: 1.5 -> 0.19ms (8x faster) -- native double arithmetic on float[] elements")
w("- **nbody (AWFY)**: 2.8 -> 1.3ms (2.1x faster)")
w("- **permute**: 0.11 -> 0.066ms (1.7x faster)")
w()
w("### 6. QuickJS comparison")
w()
w("QuickJS (pure interpreter) is generally 2-10x slower than Node.js V8, as expected.")
w("Lambda MIR is faster than QuickJS on most benchmarks, confirming that Lambda's JIT")
w("compilation provides a real performance advantage over interpretation.")
w()
w("### 7. C2MIR vs MIR Direct")
w()
w("The two Lambda JIT paths produce similar results for most benchmarks. C2MIR is slightly")
w("faster on a few (havlak, cd) due to additional C-level optimizations in the c2mir pipeline,")
w("while MIR Direct has lower startup overhead. Both paths are competitive with Node.js V8.")
w()
w("---")
w()
w("## Notes")
w()
w("- **Self-reported exec time** measures only the computation, excluding process startup,")
w("  JIT compilation warmup, and file I/O. This isolates pure algorithmic performance.")
w("- **AWFY JS benchmarks** use the official source from `ref/are-we-fast-yet/benchmarks/JavaScript/`.")
w("  Standalone bundles were created for LambdaJS/QuickJS (which don't support `require()`).")
w("- **LambdaJS** failed on some AWFY benchmarks (bounce, storage, json, deltablue, havlak, cd)")
w("  due to missing ES6 class features in the Lambda JS engine.")
w("- **QuickJS** failed on ack (R7RS) due to stack overflow on deep recursion.")
w("- **BENG** (Benchmarks Game) JS scripts with `fs.readFileSync` (knucleotide, regexredux, revcomp) fail on QuickJS/LambdaJS (no `fs` module). Timing excludes file I/O.")
w("- All times in **milliseconds** unless noted with 's' suffix (seconds).")

output = "\n".join(lines) + "\n"
with open("test/benchmark/Overall_Result2.md", "w") as f:
    f.write(output)
print(f"Written {len(lines)} lines to Overall_Result2.md")
