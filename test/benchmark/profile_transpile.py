#!/usr/bin/env python3
"""
Transpile Profiling Script — measure tree-sitter parse, AST build,
MIR transpile, and JIT codegen time for Lambda scripts.

Discovers scripts from test/benchmark suites and test_lambda_gtest directories,
runs each with LAMBDA_PROFILE=1 to capture phase-level timing, and produces
a transpiling_result report.

Usage:
  python3 test/benchmark/profile_transpile.py                     # All scripts
  python3 test/benchmark/profile_transpile.py -s benchmark        # Benchmark suites only
  python3 test/benchmark/profile_transpile.py -s gtest            # Gtest scripts only
  python3 test/benchmark/profile_transpile.py -b fib,nbody        # Specific scripts by name
  python3 test/benchmark/profile_transpile.py -n 3                # 3 iterations (median)
  python3 test/benchmark/profile_transpile.py --sort transpile    # Sort by transpile time
  python3 test/benchmark/profile_transpile.py --csv               # Also produce CSV output
  python3 test/benchmark/profile_transpile.py --list              # List all discovered scripts
  python3 test/benchmark/profile_transpile.py --top 20            # Show top N slowest
"""

import argparse
import os
import re
import subprocess
import sys
import statistics
from pathlib import Path

# ============================================================
# Ensure we run from project root
# ============================================================
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.join(SCRIPT_DIR, "..", "..")
os.chdir(PROJECT_ROOT)

LAMBDA_EXE = "./lambda.exe"
PROFILE_FILE = "temp/phase_profile.txt"
REPORT_DIR = "temp"
REPORT_MD = os.path.join(REPORT_DIR, "transpiling_result.md")
REPORT_CSV = os.path.join(REPORT_DIR, "transpiling_result.csv")
DEFAULT_TIMEOUT = 60  # seconds per script

# ============================================================
# Script discovery
# ============================================================

# Functional test directories (run with: ./lambda.exe <script>)
FUNCTIONAL_DIRS = [
    "test/lambda",
    "test/lambda/chart",
    "test/lambda/latex",
    "test/lambda/math",
]

# Procedural test directories (run with: ./lambda.exe run <script>)
PROCEDURAL_DIRS = [
    "test/lambda/proc",
    "test/benchmark/awfy",
    "test/benchmark/r7rs",
    "test/benchmark/beng",
    "test/benchmark/kostya",
    "test/benchmark/larceny",
]

# Benchmark-only directories
BENCHMARK_DIRS = [
    "test/benchmark/awfy",
    "test/benchmark/r7rs",
    "test/benchmark/beng",
    "test/benchmark/kostya",
    "test/benchmark/larceny",
]

# Tests that don't work with MIR Direct
MIR_SKIP_TESTS = {
    "object", "object_inherit", "object_default", "object_update",
    "object_mutation", "object_pattern", "object_constraint",
    "object_direct_access", "typed_param_direct_access",
    "map_object_robustness",
    "awfy_json", "awfy_json2", "awfy_list2",
    "beng_fasta", "beng_pidigits", "beng_revcomp",
}


def discover_scripts_in_dir(dir_path, is_procedural):
    """Discover .ls scripts that have matching .txt expected output files."""
    scripts = []
    if not os.path.isdir(dir_path):
        return scripts
    for fname in sorted(os.listdir(dir_path)):
        if not fname.endswith(".ls"):
            continue
        script_path = os.path.join(dir_path, fname)
        base = fname[:-3]  # strip .ls
        expected_path = os.path.join(dir_path, base + ".txt")
        if not os.path.isfile(expected_path):
            continue
        # Build a test name from directory + base name
        dir_name = os.path.basename(dir_path)
        if dir_path in ("test/lambda",):
            test_name = base
        else:
            test_name = f"{dir_name}_{base}"
        scripts.append({
            "name": test_name,
            "path": script_path,
            "is_procedural": is_procedural,
            "dir": dir_name,
            "suite": categorize_suite(dir_path),
        })
    return scripts


def categorize_suite(dir_path):
    """Categorize a directory into a suite name for reporting."""
    if "benchmark/awfy" in dir_path:
        return "awfy"
    if "benchmark/r7rs" in dir_path:
        return "r7rs"
    if "benchmark/beng" in dir_path:
        return "beng"
    if "benchmark/kostya" in dir_path:
        return "kostya"
    if "benchmark/larceny" in dir_path:
        return "larceny"
    if "lambda/chart" in dir_path:
        return "chart"
    if "lambda/latex" in dir_path:
        return "latex"
    if "lambda/math" in dir_path:
        return "math"
    if "lambda/proc" in dir_path:
        return "proc"
    return "lambda"


def discover_all_scripts(suite_filter=None, name_filter=None):
    """Discover all eligible scripts based on filters."""
    scripts = []

    benchmark_only = suite_filter and "benchmark" in suite_filter
    gtest_only = suite_filter and "gtest" in suite_filter

    if not benchmark_only:
        for d in FUNCTIONAL_DIRS:
            scripts.extend(discover_scripts_in_dir(d, is_procedural=False))
        # Procedural gtest dirs (excluding benchmark dirs already covered)
        scripts.extend(discover_scripts_in_dir("test/lambda/proc", is_procedural=True))

    if not gtest_only:
        for d in BENCHMARK_DIRS:
            scripts.extend(discover_scripts_in_dir(d, is_procedural=True))
    elif not benchmark_only:
        # When gtest_only, also include benchmark dirs that are part of gtest
        for d in BENCHMARK_DIRS:
            scripts.extend(discover_scripts_in_dir(d, is_procedural=True))

    # Filter out MIR-unsupported tests
    scripts = [s for s in scripts if s["name"] not in MIR_SKIP_TESTS]

    # Apply suite filter (specific suites like "awfy", "r7rs", etc.)
    if suite_filter:
        allowed = set(suite_filter)
        # "benchmark" matches all benchmark suites; "gtest" matches all non-benchmark
        gtest_suites = {"lambda", "chart", "latex", "math", "proc"}
        bench_suites = {"awfy", "r7rs", "beng", "kostya", "larceny"}
        expanded = set()
        for s in allowed:
            if s == "benchmark":
                expanded |= bench_suites
            elif s == "gtest":
                expanded |= gtest_suites
            else:
                expanded.add(s)
        scripts = [s for s in scripts if s["suite"] in expanded]

    # Apply name filter
    if name_filter:
        patterns = [n.strip() for n in name_filter]
        scripts = [s for s in scripts
                   if any(p in s["name"] or p in os.path.basename(s["path"])
                          for p in patterns)]

    # Deduplicate by path
    seen = set()
    unique = []
    for s in scripts:
        if s["path"] not in seen:
            seen.add(s["path"])
            unique.append(s)
    return unique


# ============================================================
# Profiling execution
# ============================================================

def parse_profile_file():
    """Parse temp/phase_profile.txt and return list of profile entries."""
    entries = []
    if not os.path.isfile(PROFILE_FILE):
        return entries
    with open(PROFILE_FILE, "r", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 10:
                continue
            try:
                entries.append({
                    "script": parts[0],
                    "parse_ms": float(parts[1]),
                    "ast_ms": float(parts[2]),
                    "transpile_ms": float(parts[3]),
                    "jit_init_ms": float(parts[4]),
                    "file_write_ms": float(parts[5]),
                    "c2mir_ms": float(parts[6]),
                    "mir_gen_ms": float(parts[7]),
                    "total_ms": float(parts[8]),
                    "code_len": int(parts[9]),
                })
            except (ValueError, IndexError):
                continue
    return entries


def aggregate_profile(entries):
    """
    Aggregate profile entries into the 4 user-facing phases.
    For scripts with imports, sum all entries into a single result.
    Returns dict with: parse_ms, ast_ms, transpile_ms, jit_ms, total_ms
    """
    parse_ms = sum(e["parse_ms"] for e in entries)
    ast_ms = sum(e["ast_ms"] for e in entries)
    # transpile_ms covers: MIR Direct → transpile_mir_ast + MIR_link
    #                      C2MIR     → C code gen + C→MIR compilation
    transpile_ms = sum(e["transpile_ms"] + e["c2mir_ms"] for e in entries)
    # jit_ms covers: jit_init + jit_gen_func (native code generation)
    jit_ms = sum(e["jit_init_ms"] + e["mir_gen_ms"] for e in entries)
    total_ms = parse_ms + ast_ms + transpile_ms + jit_ms
    return {
        "parse_ms": parse_ms,
        "ast_ms": ast_ms,
        "transpile_ms": transpile_ms,
        "jit_ms": jit_ms,
        "total_ms": total_ms,
        "modules": len(entries),
    }


def run_script_with_profiling(script_info, timeout=DEFAULT_TIMEOUT, exe=LAMBDA_EXE):
    """Run a single script with LAMBDA_PROFILE=1 and return aggregated timing."""
    # Remove stale profile file
    if os.path.isfile(PROFILE_FILE):
        os.remove(PROFILE_FILE)

    cmd = [exe]
    if script_info["is_procedural"]:
        cmd.append("run")
    cmd.extend(["--no-log", script_info["path"]])

    env = os.environ.copy()
    env["LAMBDA_PROFILE"] = "1"

    try:
        result = subprocess.run(
            cmd, env=env, timeout=timeout,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    except subprocess.TimeoutExpired:
        return None, "timeout"
    except OSError as e:
        return None, str(e)

    if result.returncode != 0:
        # Non-zero exit is acceptable for profiling — script may have runtime errors
        # but transpilation data is still captured
        pass

    entries = parse_profile_file()
    if not entries:
        return None, "no profile data"

    return aggregate_profile(entries), None


def run_profiling(scripts, iterations=1, timeout=DEFAULT_TIMEOUT, exe=LAMBDA_EXE):
    """Run profiling for all scripts, optionally with multiple iterations."""
    results = []
    total = len(scripts)

    for i, script in enumerate(scripts, 1):
        name = script["name"]
        suite = script["suite"]
        label = f"[{i}/{total}] {suite}/{name}"

        iter_results = []
        error = None
        for _ in range(iterations):
            timing, err = run_script_with_profiling(script, timeout, exe)
            if err:
                error = err
                break
            iter_results.append(timing)

        if error:
            print(f"  {label}: SKIP ({error})")
            results.append({
                "name": name,
                "suite": suite,
                "path": script["path"],
                "error": error,
            })
            continue

        # Use median across iterations for each phase
        if iterations > 1:
            timing = {
                "parse_ms": statistics.median([r["parse_ms"] for r in iter_results]),
                "ast_ms": statistics.median([r["ast_ms"] for r in iter_results]),
                "transpile_ms": statistics.median([r["transpile_ms"] for r in iter_results]),
                "jit_ms": statistics.median([r["jit_ms"] for r in iter_results]),
                "total_ms": statistics.median([r["total_ms"] for r in iter_results]),
                "modules": iter_results[0]["modules"],
            }
        else:
            timing = iter_results[0]

        print(f"  {label}: {timing['total_ms']:.2f} ms "
              f"(parse={timing['parse_ms']:.2f} ast={timing['ast_ms']:.2f} "
              f"transpile={timing['transpile_ms']:.2f} jit={timing['jit_ms']:.2f})")

        results.append({
            "name": name,
            "suite": suite,
            "path": script["path"],
            "timing": timing,
        })

    return results


# ============================================================
# Report generation
# ============================================================

def _sort_results(results, sort_key):
    """Sort results list in place by the given sort key."""
    sort_map = {
        "total": "total_ms",
        "parse": "parse_ms",
        "ast": "ast_ms",
        "transpile": "transpile_ms",
        "jit": "jit_ms",
        "name": None,
    }
    sort_field = sort_map.get(sort_key, "total_ms")
    if sort_field:
        results.sort(key=lambda r: r["timing"][sort_field], reverse=True)
    else:
        results.sort(key=lambda r: r["name"])


def _summary_table(lines, results, heading):
    """Append a summary table for a set of results."""
    if not results:
        return
    parse_sum = sum(r["timing"]["parse_ms"] for r in results)
    ast_sum = sum(r["timing"]["ast_ms"] for r in results)
    transpile_sum = sum(r["timing"]["transpile_ms"] for r in results)
    jit_sum = sum(r["timing"]["jit_ms"] for r in results)
    grand_total = parse_sum + ast_sum + transpile_sum + jit_sum
    n = len(results)

    lines.append(f"### {heading}\n")
    lines.append("| Phase | Total (ms) | Avg (ms) | % of Total |")
    lines.append("|-------|-----------|----------|------------|")
    for label, val in [("Tree-sitter Parse", parse_sum),
                       ("AST Build", ast_sum),
                       ("MIR Transpile", transpile_sum),
                       ("JIT Codegen", jit_sum)]:
        pct = (val / grand_total * 100) if grand_total > 0 else 0
        lines.append(f"| {label} | {val:.2f} | {val/n:.2f} | {pct:.1f}% |")
    lines.append(f"| **Total** | **{grand_total:.2f}** | **{grand_total/n:.2f}** | 100% |")
    lines.append("")


def _suite_table(lines, results):
    """Append a per-suite breakdown table."""
    suites = {}
    for r in results:
        suites.setdefault(r["suite"], []).append(r)
    if len(suites) <= 1:
        return
    lines.append("| Suite | Scripts | Parse (ms) | AST (ms) | Transpile (ms) | JIT (ms) | Total (ms) | Avg (ms) |")
    lines.append("|-------|---------|-----------|----------|---------------|----------|-----------|----------|")
    for suite_name in sorted(suites.keys()):
        sr = suites[suite_name]
        sp = sum(r["timing"]["parse_ms"] for r in sr)
        sa = sum(r["timing"]["ast_ms"] for r in sr)
        st = sum(r["timing"]["transpile_ms"] for r in sr)
        sj = sum(r["timing"]["jit_ms"] for r in sr)
        stot = sp + sa + st + sj
        lines.append(f"| {suite_name} | {len(sr)} | {sp:.2f} | {sa:.2f} | {st:.2f} | {sj:.2f} | {stot:.2f} | {stot/len(sr):.2f} |")
    lines.append("")


def _script_table(lines, results, show_modules=False):
    """Append a per-script breakdown table."""
    if show_modules:
        lines.append("| # | Suite | Script | Parse (ms) | AST (ms) | Transpile (ms) | JIT (ms) | Total (ms) | Modules |")
        lines.append("|---|-------|--------|-----------|----------|---------------|----------|-----------|---------|")
    else:
        lines.append("| # | Suite | Script | Parse (ms) | AST (ms) | Transpile (ms) | JIT (ms) | Total (ms) |")
        lines.append("|---|-------|--------|-----------|----------|---------------|----------|-----------|")
    for i, r in enumerate(results, 1):
        t = r["timing"]
        row = (f"| {i} | {r['suite']} | {r['name']} | "
               f"{t['parse_ms']:.2f} | {t['ast_ms']:.2f} | "
               f"{t['transpile_ms']:.2f} | {t['jit_ms']:.2f} | "
               f"{t['total_ms']:.2f}")
        if show_modules:
            row += f" | {t['modules']}"
        row += " |"
        lines.append(row)
    lines.append("")


def generate_report(results, sort_key="total", top_n=None, output_csv=False):
    """Generate markdown (and optionally CSV) report."""
    ok_results = [r for r in results if "timing" in r]
    err_results = [r for r in results if "error" in r]

    # Split into no-import (single module) vs with-import (multiple modules)
    no_import = [r for r in ok_results if r["timing"]["modules"] == 1]
    with_import = [r for r in ok_results if r["timing"]["modules"] > 1]

    _sort_results(no_import, sort_key)
    _sort_results(with_import, sort_key)

    if top_n:
        no_import = no_import[:top_n]
        with_import = with_import[:top_n]

    os.makedirs(REPORT_DIR, exist_ok=True)

    lines = []
    lines.append("# Transpiling Time Profile Report\n")
    lines.append(f"Scripts profiled: {len(ok_results)} "
                 f"({len(no_import)} standalone, {len(with_import)} with imports, "
                 f"{len(err_results)} skipped)\n")
    lines.append("")

    # ── Set 1: Scripts without imports ──
    lines.append("---\n")
    lines.append(f"## Set 1: Standalone Scripts (no imports) — {len(no_import)} scripts\n")
    lines.append("These timings reflect a single module and are the most direct "
                 "measure of each compilation phase.\n")
    lines.append("")

    _summary_table(lines, no_import, "Phase Summary (standalone)")
    _suite_table(lines, no_import)

    lines.append("### Per-Script Breakdown (standalone)\n")
    _script_table(lines, no_import, show_modules=False)

    # ── Set 2: Scripts with imports ──
    lines.append("---\n")
    lines.append(f"## Set 2: Scripts with Imports — {len(with_import)} scripts\n")
    lines.append("Timings include all imported modules. Module count shown in "
                 "last column.\n")
    lines.append("")

    _summary_table(lines, with_import, "Phase Summary (with imports)")
    _suite_table(lines, with_import)

    lines.append("### Per-Script Breakdown (with imports)\n")
    _script_table(lines, with_import, show_modules=True)

    # ── Skipped ──
    if err_results:
        lines.append("---\n")
        lines.append("## Skipped Scripts\n")
        lines.append("| Script | Reason |")
        lines.append("|--------|--------|")
        for r in err_results:
            lines.append(f"| {r['name']} | {r['error']} |")
        lines.append("")

    report = "\n".join(lines)
    with open(REPORT_MD, "w") as f:
        f.write(report)
    print(f"\nReport written to: {REPORT_MD}")

    # CSV output
    if output_csv:
        with open(REPORT_CSV, "w") as f:
            f.write("suite,name,path,parse_ms,ast_ms,transpile_ms,jit_ms,total_ms,modules\n")
            for r in ok_results:
                t = r["timing"]
                f.write(f"{r['suite']},{r['name']},{r['path']},"
                        f"{t['parse_ms']:.3f},{t['ast_ms']:.3f},"
                        f"{t['transpile_ms']:.3f},{t['jit_ms']:.3f},"
                        f"{t['total_ms']:.3f},{t['modules']}\n")
        print(f"CSV written to:    {REPORT_CSV}")

    return report


# ============================================================
# Main
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        description="Profile Lambda transpiling time across test scripts.")
    parser.add_argument("-s", "--suite", type=str, default=None,
                        help="Comma-separated suite filter: benchmark, gtest, awfy, r7rs, "
                             "beng, kostya, larceny, lambda, chart, latex, math, proc")
    parser.add_argument("-b", "--benchmarks", type=str, default=None,
                        help="Comma-separated script name filter (substring match)")
    parser.add_argument("-n", "--iterations", type=int, default=1,
                        help="Number of iterations per script (uses median)")
    parser.add_argument("--sort", type=str, default="total",
                        choices=["total", "parse", "ast", "transpile", "jit", "name"],
                        help="Sort results by this phase (default: total)")
    parser.add_argument("--top", type=int, default=None,
                        help="Show only top N slowest scripts")
    parser.add_argument("--csv", action="store_true",
                        help="Also produce CSV output")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT,
                        help=f"Timeout per script in seconds (default: {DEFAULT_TIMEOUT})")
    parser.add_argument("--list", action="store_true",
                        help="List all discovered scripts and exit")
    parser.add_argument("--exe", type=str, default=LAMBDA_EXE,
                        help=f"Path to lambda executable (default: {LAMBDA_EXE})")
    args = parser.parse_args()

    if args.exe != LAMBDA_EXE:
        exe = args.exe
    else:
        exe = LAMBDA_EXE

    # Check executable exists
    if not os.path.isfile(exe):
        print(f"Error: Lambda executable not found: {exe}")
        print("Run 'make build' first.")
        sys.exit(1)

    # Discover scripts
    suite_filter = args.suite.split(",") if args.suite else None
    name_filter = args.benchmarks.split(",") if args.benchmarks else None
    scripts = discover_all_scripts(suite_filter, name_filter)

    if not scripts:
        print("No scripts found matching the given filters.")
        sys.exit(1)

    if args.list:
        print(f"Discovered {len(scripts)} scripts:\n")
        by_suite = {}
        for s in scripts:
            by_suite.setdefault(s["suite"], []).append(s)
        for suite in sorted(by_suite.keys()):
            print(f"  [{suite}] ({len(by_suite[suite])} scripts)")
            for s in by_suite[suite]:
                mode = "run" if s["is_procedural"] else "eval"
                print(f"    {s['name']:40s} {s['path']:50s} ({mode})")
        return

    print(f"Profiling {len(scripts)} scripts "
          f"({args.iterations} iteration{'s' if args.iterations > 1 else ''} each)...\n")

    results = run_profiling(scripts, args.iterations, args.timeout, exe)
    generate_report(results, args.sort, args.top, args.csv)


if __name__ == "__main__":
    main()
