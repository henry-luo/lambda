#!/usr/bin/env python3
"""Compare native C2MIR integer ports with their double-state counterparts."""

import argparse
import pathlib
import statistics
import subprocess
import sys

from run_c2mir_benchmarks import build_command, parse_timing, strip_timing


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
PORT_ROOT = PROJECT_ROOT / "test/benchmark"

# These ports have a meaningful floating-point equivalent.  Loop selectors,
# array indices, bitwise state, and C ABI/status values remain integral in the
# counterparts because those are control/data-representation requirements,
# not benchmark payload arithmetic.
PAIRS = {
    "r7rs": ["ack", "cpstak", "fib", "nqueens", "sum", "tak"],
    "awfy": ["bounce", "list", "permute", "queens", "sieve"],
    "kostya": ["collatz", "json_gen", "levenshtein", "primes"],
    "larceny": ["array1", "diviter", "divrec", "paraffins", "pnpoly", "primes", "puzzle", "quicksort", "triangl"],
}


def run_once(source, timeout):
    """Run one port and return (body_ms, exit_code, output, stderr)."""
    try:
        result = subprocess.run(
            build_command(source), cwd=PROJECT_ROOT,
            capture_output=True, text=True, timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return None, -1, "", "timeout"
    return parse_timing(result.stdout), result.returncode, strip_timing(result.stdout), result.stderr.strip()


def format_ms(value):
    if value is None:
        return "FAIL"
    return f"{value:9.3f} ms"


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, default=7, help="runs per source (default: 7)")
    parser.add_argument("--timeout", type=float, default=120.0, help="per-run timeout in seconds")
    parser.add_argument("--suite", choices=sorted(PAIRS), action="append", help="repeat to select suites")
    return parser.parse_args()


def main():
    args = parse_args()
    if args.runs < 1:
        print("--runs must be positive", file=sys.stderr)
        return 2
    suites = args.suite or list(PAIRS)
    failures = 0
    ratios = []
    print("C2MIR integer vs double-state benchmark comparison")
    print(f"runs per source: {args.runs}")
    print()
    print(f"{'Benchmark':24s} {'int median':>14s} {'double median':>14s} {'double/int':>11s}  output")
    print("-" * 86)

    for suite in suites:
        for name in PAIRS[suite]:
            int_source = PORT_ROOT / suite / "c2mir" / f"{name}.c"
            double_source = PORT_ROOT / suite / "c2mir" / f"{name}_double.c"
            int_times = []
            double_times = []
            int_output = None
            double_output = None
            detail = "PASS"
            for _ in range(args.runs):
                timing, code, output, error = run_once(int_source, args.timeout)
                if code != 0 or timing is None:
                    detail = f"int failed ({error or code})"
                    break
                int_times.append(timing)
                int_output = output
            if detail == "PASS":
                for _ in range(args.runs):
                    timing, code, output, error = run_once(double_source, args.timeout)
                    if code != 0 or timing is None:
                        detail = f"double failed ({error or code})"
                        break
                    double_times.append(timing)
                    double_output = output
            if detail == "PASS" and int_output != double_output:
                detail = "output mismatch"
            if detail != "PASS":
                failures += 1
                print(f"{suite + '/' + name:24s} {'FAIL':>14s} {'FAIL':>14s} {'N/A':>11s}  {detail}")
                continue
            int_median = statistics.median(int_times)
            double_median = statistics.median(double_times)
            ratio = double_median / int_median if int_median > 0.0 else 0.0
            if ratio > 0.0:
                ratios.append(ratio)
            print(f"{suite + '/' + name:24s} {format_ms(int_median)} {format_ms(double_median)} {ratio:10.2f}x  {detail}")

    print()
    if ratios:
        product = 1.0
        for ratio in ratios:
            product *= ratio
        geometric_mean = product ** (1.0 / len(ratios))
        print(f"geometric mean double/int: {geometric_mean:.2f}x")
        print(f"double faster: {sum(1 for ratio in ratios if ratio < 1.0)} / {len(ratios)}")
    print(f"matched pairs passed: {len(ratios)}/{sum(len(PAIRS[suite]) for suite in suites)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
