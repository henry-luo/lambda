#!/usr/bin/env python3
"""Runs every functional Lambda test script under both tiers and reports the
zero-fallback, output-identical set. Output goes to ./temp/ (CLAUDE rule 2).

Usage: python3 test/interp/tier_sweep.py [--dir test/lambda] [--timeout 20]
"""
import argparse, os, re, subprocess, sys

FALLBACK_RE = re.compile(r"interp: executed=(\d+) fallback=(\d+) excluded=(\d+)")


def run(script, tier, timeout):
    env = dict(os.environ)
    if tier:
        env["LAMBDA_TIER"] = tier
    else:
        env.pop("LAMBDA_TIER", None)
    try:
        proc = subprocess.run(["./lambda.exe", script], env=env, timeout=timeout,
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
    ap.add_argument("--dir", default="test/lambda")
    ap.add_argument("--timeout", type=int, default=20)
    ap.add_argument("--out", default="temp/interp_tier_sweep.tsv")
    args = ap.parse_args()

    scripts = sorted(
        os.path.join(args.dir, n) for n in os.listdir(args.dir)
        if n.endswith(".ls") and os.path.exists(
            os.path.join(args.dir, n[:-3] + ".txt")))

    os.makedirs("temp", exist_ok=True)
    rows, supported, fell_back, mismatched = [], [], [], []
    for script in scripts:
        jit_out, _, jit_status = run(script, None, args.timeout)
        int_out, stats, int_status = run(script, "interp", args.timeout)
        executed, fallback = stats if stats else (0, 0)
        if fallback or not executed:
            verdict = "fallback"
            fell_back.append(script)
        elif jit_out != int_out or jit_status != int_status:
            verdict = "mismatch"
            mismatched.append(script)
        else:
            verdict = "match"
            supported.append(script)
        rows.append((script, verdict, jit_status, int_status, executed, fallback))

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
