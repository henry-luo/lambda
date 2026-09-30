#!/usr/bin/env python3
"""Compile and execute the native C2MIR benchmark ports.

The Lambda --c2mir execution path is retired.  These C sources exercise the
embedded MIR C frontend directly through the driver built in lambda/mir.

Usage:
  python3 test/benchmark/run_c2mir_benchmarks.py
  python3 test/benchmark/run_c2mir_benchmarks.py --suite r7rs
  python3 test/benchmark/run_c2mir_benchmarks.py --list
"""

import argparse
import datetime
import hashlib
import json
import platform
import pathlib
import re
import subprocess
import sys
import time

from benchmark_provenance import c2mir_source_corpus, command_output, sha256_file


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
# Native reference results must use the driver built from Lambda's pinned MIR
# sources so their backend revision matches the MIR Direct implementation.
C2M = PROJECT_ROOT / "lambda/mir" / ("c2m.exe" if sys.platform == "win32" else "c2m")
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
        ("cd", "collisions=4305\nCD: PASS"),
        ("deltablue", "DeltaBlue: PASS"),
        ("havlak", "Havlak: PASS"),
        ("json", "Json: PASS"),
        ("list", "List: PASS"),
        ("mandelbrot", "Mandelbrot: PASS"),
        ("nbody", "NBody: PASS"),
        ("permute", "Permute: PASS"),
        ("queens", "Queens: PASS"),
        ("richards", "Richards: PASS"),
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
        ("deriv", "deriv: PASS"),
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
        ("pidigits", "3141592653\t:10\n5897932384\t:20\n6264338327\t:30\npidigits: PASS"),
        ("regexredux", "@file:test/benchmark/beng/regexredux.txt"),
        ("revcomp", "@file:test/benchmark/beng/revcomp.txt"),
        ("spectralnorm", "1.274219991"),
    ],
    "jetstream": [
        ("crypto_sha1", "crypto-sha1: PASS"),
        ("cube3d", "3d-cube: PASS"),
        ("hashmap", "hash-map: PASS"),
        ("navier_stokes", "navier-stokes: PASS (checksum=77)"),
        ("raytrace3d", "3d-raytrace: PASS (pixels=7200)"),
        ("splay", "splay: PASS (nodes=8000)"),
    ],
    "text": [
        ("fast_diff", "fast_diff: CHECKSUM:748544"),
        ("microdiff", "microdiff: CHECKSUM:3278848"),
        ("hyphen", "hyphen: CHECKSUM:1183296"),
        ("prettier_ast", "prettier_ast: CHECKSUM:56483873"),
        ("text_search", "text_search: CHECKSUM:91395120"),
        ("three_way_merge", "three_way_merge: CHECKSUM:342313356"),
        ("log_pipeline", "log_pipeline: CHECKSUM:292634526"),
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


def ensure_c2m():
    """Build the pinned native C2MIR driver when a clean build omitted it."""
    if C2M.is_file():
        return None
    result = subprocess.run(
        ["make", "c2mir-driver"], cwd=PROJECT_ROOT, capture_output=True, text=True,
    )
    if result.returncode == 0 and C2M.is_file():
        return None
    detail = result.stderr.strip() or result.stdout.strip() or "no build output"
    return f"could not build native C2MIR driver: {detail}"


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
    parser.add_argument("--repeat", type=int, default=1,
                        help="independent timed runs per C port (default: 1)")
    parser.add_argument("--output", type=pathlib.Path,
                        help="write source-pinned raw results as JSON, under ./temp")
    return parser.parse_args()


def main():
    args = parse_args()
    if args.repeat < 1:
        raise ValueError("--repeat must be positive")
    selected_suites = args.suite or list(SUITES)
    if args.list:
        for suite in selected_suites:
            for name, _ in SUITES[suite]:
                print(f"{suite}/{name}")
        return 0
    driver_error = ensure_c2m()
    if driver_error is not None:
        print(driver_error, file=sys.stderr)
        return 2

    artifact = {
        "_metadata": {
            "schema_version": 1,
            "started_at": datetime.datetime.now().isoformat(timespec="seconds"),
            "platform": platform.platform(),
            "git_commit": command_output(["git", "rev-parse", "HEAD"]),
            "c2m_sha256": sha256_file(C2M),
            "mir_revision": command_output(["git", "-C", "lambda/mir", "rev-parse", "HEAD"]),
            "timer_sha256": sha256_file(TIMER_MAIN),
            "source_corpus": c2mir_source_corpus(),
            "repeat": args.repeat,
            "timeout_s": args.timeout,
        },
        "rows": [],
    }
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
            row = {
                "suite": suite,
                "name": name,
                "source": str(source.relative_to(PROJECT_ROOT)),
                "source_sha256": sha256_file(source),
                "command": build_command(source),
                "samples": [],
            }
            if expected.startswith("@file:"):
                golden = PROJECT_ROOT / expected.removeprefix("@file:")
                expected_output = golden.read_text().strip()
                row["expected_file"] = str(golden.relative_to(PROJECT_ROOT))
                row["expected_sha256"] = sha256_file(golden)
            else:
                expected_output = expected
            for _ in range(args.repeat):
                started = time.perf_counter()
                try:
                    result = subprocess.run(
                        build_command(source), cwd=PROJECT_ROOT,
                        capture_output=True, text=True, timeout=args.timeout,
                    )
                except subprocess.TimeoutExpired:
                    row["samples"].append({"status": "timeout", "exec_ms": None})
                    continue
                wall_ms = (time.perf_counter() - started) * 1000.0
                body_ms = parse_timing(result.stdout)
                output = strip_timing(result.stdout)
                passed = result.returncode == 0 and output == expected_output
                row["samples"].append({
                    "status": "ok" if passed and body_ms is not None
                        else "wall_fallback" if passed else "wrong_output",
                    "exec_ms": body_ms,
                    "wall_ms": wall_ms,
                    "returncode": result.returncode,
                    "stdout_sha256": hashlib.sha256(output.encode()).hexdigest(),
                    "expected_stdout_matches": passed,
                    "stderr": result.stderr.strip() if not passed else "",
                })
            values = sorted(sample["exec_ms"] for sample in row["samples"]
                            if sample["status"] == "ok")
            row["median_exec_ms"] = values[len(values) // 2] if values else None
            row["valid_samples"] = len(values)
            row["status"] = "ok" if len(values) == args.repeat else "failed"
            artifact["rows"].append(row)
            if row["status"] == "ok":
                print(f"  {name:<12} PASS  {row['median_exec_ms']:8.2f} ms"
                      f" ({len(values)} sample(s))")
            else:
                failures += 1
                print(f"  {name:<12} FAIL  {len(values)}/{args.repeat} timed samples")
                for sample in row["samples"]:
                    if sample["status"] != "ok" and sample.get("stderr"):
                        print(f"    stderr: {sample['stderr']}")

    artifact["_metadata"]["finished_at"] = datetime.datetime.now().isoformat(timespec="seconds")
    artifact["_metadata"]["c2m_final_sha256"] = sha256_file(C2M)
    artifact["_metadata"]["source_corpus_final_sha256"] = c2mir_source_corpus()["sha256"]
    artifact["_metadata"]["binary_and_sources_stable"] = (
        artifact["_metadata"]["c2m_sha256"] == artifact["_metadata"]["c2m_final_sha256"]
        and artifact["_metadata"]["source_corpus"]["sha256"]
            == artifact["_metadata"]["source_corpus_final_sha256"]
    )
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(artifact, indent=2) + "\n")
        print(f"Raw C2MIR results saved to {args.output}")
    print(f"\n{total - failures}/{total} native C2MIR benchmarks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
