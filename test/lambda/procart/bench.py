#!/usr/bin/env python3
"""Time the procart benchmarks against their budgets (Lambda_Pkg_Proc_Art.md §7, PG7.1).

Each test/lambda/procart/_bench_<name>.ls is run --runs times on one tier and the
best wall time is reported. Only a release build counts as PG7 evidence
(AGENTS rule 10): pass its path with --exe.

    python3 test/lambda/procart/bench.py --exe ./lambda.exe --tier auto
"""
import argparse
import os
import pathlib
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
# seconds; a family is a native-port candidate only when it misses its budget by 5x
BUDGETS = {"noise": 1.0, "voronoi": 2.0, "wfc": 5.0, "erosion": 5.0}


def run_once(exe, script, tier):
    env = dict(os.environ, LAMBDA_EXEC_BACKEND=tier)
    start = time.perf_counter()
    proc = subprocess.run([exe, "--no-log", str(script)], env=env, capture_output=True, text=True)
    elapsed = time.perf_counter() - start
    if proc.returncode != 0:
        sys.exit(f"bench: {script.name} failed on {tier}:\n{proc.stderr[-2000:]}")
    return elapsed, proc.stdout.strip().splitlines()[-1]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", default="./lambda.exe")
    parser.add_argument("--tier", default="auto", choices=["auto", "jit", "interp"])
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--only", help="run one benchmark by name")
    args = parser.parse_args()

    scripts = sorted(HERE.glob("_bench_*.ls"))
    if args.only:
        scripts = [s for s in scripts if s.stem == "_bench_" + args.only]
    print(f"{'benchmark':<12} {'best s':>8} {'budget s':>9} {'ratio':>6}  checksum")
    for script in scripts:
        name = script.stem[len("_bench_"):]
        runs = [run_once(args.exe, script, args.tier) for _ in range(args.runs)]
        best = min(r[0] for r in runs)
        budget = BUDGETS.get(name)
        ratio = f"{best / budget:.2f}" if budget else "-"
        print(f"{name:<12} {best:>8.3f} {budget if budget else '-':>9} {ratio:>6}  {runs[0][1]}")


if __name__ == "__main__":
    main()
