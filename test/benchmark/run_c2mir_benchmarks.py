#!/usr/bin/env python3
"""Compile and execute the native C2MIR benchmark ports.

The Lambda --c2mir execution path is retired.  These C sources exercise the
embedded MIR C frontend directly through mac-deps/mir/c2m instead.

Usage:
  python3 test/benchmark/run_c2mir_benchmarks.py
  python3 test/benchmark/run_c2mir_benchmarks.py --suite r7rs
  python3 test/benchmark/run_c2mir_benchmarks.py --list
"""

import argparse
import pathlib
import re
import subprocess
import sys
import time


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
C2M = PROJECT_ROOT / "mac-deps/mir/c2m"
TIMER_MAIN = PROJECT_ROOT / "test/benchmark/c2mir/bench_timer_main.c"
TIMING_RE = re.compile(r"__TIMING__:([\d.]+(?:e[+-]?\d+)?)")
SUITES = {
    "r7rs": [
        ("ack", "ack: PASS"),
        ("cpstak", "cpstak: PASS"),
        ("fft", "fft: PASS"),
        ("fib", "fib: PASS"),
        ("fibfp", "fibfp: PASS"),
        ("mbrot", "mbrot: PASS"),
        ("nqueens", "nqueens: PASS"),
        ("sum", "sum: PASS"),
        ("sumfp", "sumfp: PASS"),
        ("tak", "tak: PASS"),
    ],
    "awfy": [
        ("bounce", "Bounce: PASS"),
        ("list", "List: PASS"),
        ("mandelbrot", "Mandelbrot: PASS"),
        ("nbody", "NBody: PASS"),
        ("permute", "Permute: PASS"),
        ("queens", "Queens: PASS"),
        ("sieve", "Sieve: PASS"),
        ("storage", "Storage: PASS"),
        ("towers", "Towers: PASS"),
    ],
    "kostya": [
        ("base64", "base64: encoded_len=13336 decoded_len=10000\nbase64: PASS"),
        ("brainfuck", "Hello World!"),
        ("collatz", "collatz: PASS (start=837799)"),
        ("levenshtein", "levenshtein: PASS"),
        ("json_gen", "json_gen: length=64007\njson_gen: PASS"),
        ("matmul", "matmul: sum=7090614\nmatmul: DONE"),
        ("primes", "primes: PASS (78498)"),
    ],
    "larceny": [
        ("array1", "array1: PASS"),
        ("diviter", "diviter: PASS"),
        ("divrec", "divrec: PASS"),
        ("gcbench", "stretch tree of depth 15 check: 65535\n16384 trees of depth 4 check: 507904\n4096 trees of depth 6 check: 520192\n1024 trees of depth 8 check: 523264\n256 trees of depth 10 check: 524032\n64 trees of depth 12 check: 524224\n16 trees of depth 14 check: 524272\nlong lived tree of depth 14 check: 32767"),
        ("pnpoly", "pnpoly: total=100000 inside=29415\npnpoly: DONE"),
        ("paraffins", "paraffins: nb(23) = 5731580\nparaffins: PASS"),
        ("primes", "primes: PASS"),
        ("puzzle", "puzzle: PASS"),
        ("quicksort", "quicksort: PASS"),
        ("ray", "ray: hits=1392\nray: PASS"),
        ("triangl", "triangl: solutions=29760\ntriangl: PASS"),
    ],
    "beng": [
        ("binarytrees", "stretch tree of depth 11\t check: 4095\n1024\t trees of depth 4\t check: 31744\n256\t trees of depth 6\t check: 32512\n64\t trees of depth 8\t check: 32704\n16\t trees of depth 10\t check: 32752\nlong lived tree of depth 10\t check: 2047"),
        ("fannkuch", "228\nPfannkuchen(7) = 16"),
        ("fasta", "@file:test/benchmark/beng/fasta.txt"),
        ("knucleotide", "@file:test/benchmark/beng/knucleotide.txt"),
        ("mandelbrot", "255"),
        ("regexredux", "@file:test/benchmark/beng/regexredux.txt"),
        ("revcomp", "@file:test/benchmark/beng/revcomp.txt"),
        ("spectralnorm", "1.274219991"),
    ],
    "jetstream": [
        ("hashmap", "hash-map: PASS"),
    ],
}


def port_source(suite, name):
    """Path of the native port for one benchmark row, or None when unported."""
    source = PROJECT_ROOT / "test/benchmark" / suite / "c2mir" / f"{name}.c"
    return source if source.is_file() else None


def build_command(source):
    """c2m argv for one native port.

    `-Dmain=c2mir_bench_body` renames the port's entry point so
    bench_timer_main.c can supply the real `main` and bracket the workload with
    a wall-clock measurement. Without it the only measurable number would be
    whole-process wall time, which for c2m includes parsing and JIT-generating
    the C source and so is not comparable with any engine's __TIMING__ value.
    """
    return [str(C2M), "-Dmain=c2mir_bench_body", str(source), str(TIMER_MAIN), "-eg"]


def parse_timing(stdout):
    """Return the self-reported workload milliseconds, or None."""
    match = TIMING_RE.search(stdout)
    return float(match.group(1)) if match else None


def strip_timing(stdout):
    """Drop the __TIMING__ line so output can be diffed against a golden file."""
    return "\n".join(line for line in stdout.splitlines()
                     if not TIMING_RE.search(line)).strip()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=sorted(SUITES), action="append",
                        help="run only this suite; repeat to select multiple suites")
    parser.add_argument("--list", action="store_true", help="list available ports and exit")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="per-benchmark timeout in seconds (default: 120)")
    return parser.parse_args()


def main():
    args = parse_args()
    selected_suites = args.suite or list(SUITES)
    if args.list:
        for suite in selected_suites:
            for name, _ in SUITES[suite]:
                print(f"{suite}/{name}")
        return 0
    if not C2M.is_file():
        print(f"C2MIR driver not found: {C2M}", file=sys.stderr)
        return 2

    failures = 0
    total = 0
    for suite in selected_suites:
        print(f"{suite}:")
        for name, expected in SUITES[suite]:
            source = port_source(suite, name)
            total += 1
            if source is None:
                failures += 1
                print(f"  {name:<12} MISSING SOURCE")
                continue
            started = time.perf_counter()
            try:
                result = subprocess.run(
                    build_command(source), cwd=PROJECT_ROOT,
                    capture_output=True, text=True, timeout=args.timeout,
                )
            except subprocess.TimeoutExpired:
                failures += 1
                print(f"  {name:<12} TIMEOUT")
                continue
            wall_ms = (time.perf_counter() - started) * 1000.0
            # workload-only time when the port reported it; wall time (which
            # includes c2m's own compile) only as a fallback
            body_ms = parse_timing(result.stdout)
            elapsed_ms = body_ms if body_ms is not None else wall_ms
            output = strip_timing(result.stdout)
            if expected.startswith("@file:"):
                golden = PROJECT_ROOT / expected.removeprefix("@file:")
                expected_output = golden.read_text().strip()
            else:
                expected_output = expected
            if result.returncode == 0 and output == expected_output:
                print(f"  {name:<12} PASS  {elapsed_ms:8.2f} ms")
            else:
                failures += 1
                print(f"  {name:<12} FAIL  {elapsed_ms:8.2f} ms")
                if output:
                    print(f"    stdout: {output}")
                if result.stderr.strip():
                    print(f"    stderr: {result.stderr.strip()}")

    print(f"\n{total - failures}/{total} native C2MIR benchmarks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
