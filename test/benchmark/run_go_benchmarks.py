#!/usr/bin/env python3
"""Build and execute each native Go benchmark in an isolated process.

Usage:
  python3 test/benchmark/run_go_benchmarks.py
  python3 test/benchmark/run_go_benchmarks.py --suite awfy
  python3 test/benchmark/run_go_benchmarks.py --list
"""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import time

TIMING_RE = re.compile(r"__TIMING__:([\d.]+(?:e[+-]?\d+)?)")


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
GO_ROOT = PROJECT_ROOT / "test" / "benchmark" / "go"
DEFAULT_BUILD_DIR = PROJECT_ROOT / "temp" / "go-benchmarks"
SUITES = {
    "r7rs": ["ack", "cpstak", "fft", "fib", "fibfp", "mbrot", "nqueens", "sum", "sumfp", "tak"],
    "awfy": ["bounce", "cd", "deltablue", "havlak", "json", "list", "mandelbrot", "nbody", "permute", "queens", "richards", "sieve", "storage", "towers"],
    "kostya": ["base64", "brainfuck", "collatz", "json_gen", "levenshtein", "matmul", "primes"],
    "larceny": ["array1", "deriv", "diviter", "divrec", "gcbench", "paraffins", "pnpoly", "primes", "puzzle", "quicksort", "ray", "triangl"],
    "beng": ["binarytrees", "fannkuch", "fasta", "knucleotide", "mandelbrot", "nbody", "pidigits", "regexredux", "revcomp", "spectralnorm"],
    "jetstream": ["base64", "bigdenary", "crypto_aes", "crypto_md5", "crypto_rsa", "crypto_sha1", "cube3d", "deltablue", "hashmap", "navier_stokes", "nbody", "raytrace3d", "regex_dna", "richards", "splay"],
    "standalone": ["cow_document_edit"],
}


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=sorted(SUITES), action="append",
                        help="run only this suite; repeat to select multiple suites")
    parser.add_argument("--list", action="store_true", help="list benchmark programs and exit")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="per-benchmark execution timeout in seconds (default: 120)")
    parser.add_argument("--build-dir", type=pathlib.Path, default=DEFAULT_BUILD_DIR,
                        help="directory for isolated executable artifacts")
    return parser.parse_args()


def go_executable():
    go = shutil.which("go")
    if go:
        return go
    homebrew_go = pathlib.Path("/opt/homebrew/opt/go/bin/go")
    return str(homebrew_go) if homebrew_go.is_file() else None


def command_package(suite, name):
    if suite == "standalone":
        return f"./cmd/{name}"
    return f"./cmd/{suite}/{name}"


def has_port(suite, name):
    """Whether a Go port exists for one benchmark row."""
    return name in SUITES.get(suite, ())


def build_binary(go, suite, name, build_dir):
    """Build one port. Returns (executable_path, error_text_or_None)."""
    executable = build_dir / f"{suite}_{name}"
    build = subprocess.run(
        [go, "-C", str(GO_ROOT), "build", "-o", str(executable), command_package(suite, name)],
        cwd=PROJECT_ROOT, capture_output=True, text=True,
    )
    if build.returncode != 0:
        return None, build.stderr.strip() or f"go build exited {build.returncode}"
    return executable, None


def parse_timing(stdout):
    """Return the self-reported workload milliseconds, or None."""
    match = TIMING_RE.search(stdout)
    return float(match.group(1)) if match else None


def main():
    args = parse_args()
    selected_suites = args.suite or list(SUITES)
    if args.list:
        for suite in selected_suites:
            for name in SUITES[suite]:
                print(name if suite == "standalone" else f"{suite}/{name}")
        return 0
    go = go_executable()
    if not go:
        print("Go toolchain not found in PATH or /opt/homebrew/opt/go/bin/go", file=sys.stderr)
        return 2
    args.build_dir.mkdir(parents=True, exist_ok=True)

    failures = 0
    total = 0
    for suite in selected_suites:
        print(f"{suite}:")
        for name in SUITES[suite]:
            executable, build_error = build_binary(go, suite, name, args.build_dir)
            total += 1
            if build_error is not None:
                failures += 1
                print(f"  {name:<16} BUILD FAIL")
                print(f"    stderr: {build_error}")
                continue
            started = time.perf_counter()
            try:
                result = subprocess.run(
                    [str(executable)], cwd=PROJECT_ROOT, capture_output=True, text=True, timeout=args.timeout,
                )
            except subprocess.TimeoutExpired:
                failures += 1
                print(f"  {name:<16} TIMEOUT")
                continue
            wall_ms = (time.perf_counter() - started) * 1000.0
            # workload-only time when the port reported it. Process wall time is
            # not usable here: a freshly built binary pays a few hundred ms of
            # first-launch verification on macOS, which dwarfs most workloads.
            body_ms = parse_timing(result.stdout)
            elapsed_ms = body_ms if body_ms is not None else wall_ms
            if result.returncode == 0:
                print(f"  {name:<16} PASS  {elapsed_ms:8.2f} ms")
            else:
                failures += 1
                print(f"  {name:<16} FAIL  {elapsed_ms:8.2f} ms")
                if result.stdout.strip():
                    print(f"    stdout: {result.stdout.strip()}")
                if result.stderr.strip():
                    print(f"    stderr: {result.stderr.strip()}")
    print(f"\n{total - failures}/{total} isolated Go benchmarks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
