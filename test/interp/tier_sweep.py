#!/usr/bin/env python3
"""Runs every functional Lambda test script under both tiers and reports the
zero-fallback, output-identical set. Output goes to ./temp/ (CLAUDE rule 2).

Usage: python3 test/interp/tier_sweep.py [--dir test/lambda] [--timeout 20]
"""
import argparse, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

FALLBACK_RE = re.compile(r"interp: executed=(\d+) fallback=(\d+) excluded=(\d+)")


# Mirrors test_lambda_gtest.cpp's discovery: functional scripts run directly,
# procedural ones through `lambda.exe run`.
FUNCTIONAL_DIRS = (
    "test/lambda", "test/lambda/chart", "test/lambda/latex", "test/lambda/math",
    "test/lambda/editor", "test/lambda/editing", "test/lambda/graph/mermaid",
    "test/lambda/graph/graphviz", "test/lambda/graph/structurizr",
)
PROCEDURAL_DIRS = ("test/lambda/proc", "test/lambda/conc", "test/lambda/pdf")


def run(script, tier, timeout, procedural=False):
    env = dict(os.environ)
    if tier:
        env["LAMBDA_TIER"] = tier
    else:
        env.pop("LAMBDA_TIER", None)
    argv = ["./lambda.exe", "run", script] if procedural else ["./lambda.exe", script]
    try:
        proc = subprocess.run(argv, env=env, timeout=timeout,
                              capture_output=True, text=True, errors="replace")
    except subprocess.TimeoutExpired:
        return None, None, "timeout"
    stats = FALLBACK_RE.search(proc.stderr or "")
    fallback = int(stats.group(2)) if stats else 0
    executed = int(stats.group(1)) if stats else 0
    status = "ok" if proc.returncode == 0 else f"exit{proc.returncode}"
    return proc.stdout, (executed, fallback), status


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=None,
                    help="scan one directory instead of the whole baseline corpus")
    ap.add_argument("--timeout", type=int, default=20)
    ap.add_argument("--out", default="temp/interp_tier_sweep.tsv")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 1))
    args = ap.parse_args()

    def discover(directory, procedural):
        if not os.path.isdir(directory):
            return []
        return sorted(
            (os.path.join(directory, n), procedural)
            for n in os.listdir(directory)
            if n.endswith(".ls") and os.path.exists(
                os.path.join(directory, n[:-3] + ".txt")))

    if args.dir:
        scripts = discover(args.dir, args.dir in PROCEDURAL_DIRS)
    else:
        scripts = []
        for directory in FUNCTIONAL_DIRS:
            scripts += discover(directory, False)
        for directory in PROCEDURAL_DIRS:
            scripts += discover(directory, True)

    os.makedirs("temp", exist_ok=True)

    # Each script is an independent pair of subprocess runs, so the sweep
    # parallelizes cleanly. It was serial back when almost everything fell back
    # in milliseconds; every slice that lands converts a rejection into a full
    # interpreted run, so the serial cost grew with coverage. These runs read
    # only stderr — nothing here depends on the shared log.txt, which is why
    # refresh_lists.py (which does) must stay serial.
    def verdict_for(entry):
        script, procedural = entry
        jit_out, _, jit_status = run(script, None, args.timeout, procedural)
        int_out, stats, int_status = run(script, "interp", args.timeout, procedural)
        executed, fallback = stats if stats else (0, 0)
        if fallback or not executed:
            verdict = "fallback"
        elif jit_out != int_out or jit_status != int_status:
            verdict = "mismatch"
        else:
            verdict = "match"
        return (script, verdict, jit_status, int_status, executed, fallback)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        rows = list(pool.map(verdict_for, scripts))

    supported = [r[0] for r in rows if r[1] == "match"]
    fell_back = [r[0] for r in rows if r[1] == "fallback"]
    mismatched = [r[0] for r in rows if r[1] == "mismatch"]

    with open(args.out, "w") as f:
        f.write("# script\tverdict\tjit_status\tinterp_status\texecuted\tfallback\n")
        for row in rows:
            f.write("\t".join(str(c) for c in row) + "\n")

    print(f"scripts={len(scripts)} match={len(supported)} "
          f"fallback={len(fell_back)} mismatch={len(mismatched)}")
    print(f"wrote {args.out}")
    if mismatched:
        print("mismatched:")
        for s in mismatched:
            print("  " + s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
