#!/usr/bin/env python3
"""Turnaround/memory measurement for the T0 interpreter (§6 of the impl plan).

Runs the §6.1 corpus under both tiers on the same binary, selected by
LAMBDA_TIER, and writes temp/interp_bench.tsv plus a Markdown table ready to
paste into vibe/Lambda_Impl_Ast_Interp.md §6.2.

Protocol (U33): release build, one warm-up plus N measured runs, median.
Peak RSS comes from the child's own ru_maxrss via resource.getrusage on the
child wait, matching the `peak_rss_mb` the CLI prints for itself.
"""
import argparse
import os
import re
import statistics
import subprocess
import time

SUMMARY_RE = re.compile(
    r"interp: executed=(\d+) fallback=(\d+) excluded=(\d+) nodes=(\d+) peak_rss_mb=([\d.]+)")


def run_once(argv, tier):
    env = dict(os.environ)
    if tier:
        env["LAMBDA_TIER"] = tier
    else:
        env.pop("LAMBDA_TIER", None)
    # The child reports its own ru_maxrss: RUSAGE_CHILDREN is a running maximum
    # over every child this process ever reaped, so it cannot attribute a peak
    # to one run.
    env["LAMBDA_RSS_REPORT"] = "1"
    start = time.perf_counter()
    proc = subprocess.run(argv, env=env, capture_output=True, text=True, errors="replace")
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    stats = SUMMARY_RE.search(proc.stderr or "")
    peak_mb = float(stats.group(5)) if stats else 0.0
    return {
        "ms": elapsed_ms,
        "peak_rss_mb": peak_mb,
        "fallback": int(stats.group(2)) if stats else 0,
        "executed": int(stats.group(1)) if stats else 0,
        "nodes": int(stats.group(4)) if stats else 0,
        "ok": proc.returncode == 0,
    }


def measure(argv, tier, repeats):
    run_once(argv, tier)                       # warm-up, discarded
    runs = [run_once(argv, tier) for _ in range(repeats)]
    return {
        "ms": statistics.median(r["ms"] for r in runs),
        "peak_rss_mb": statistics.median(r["peak_rss_mb"] for r in runs),
        "fallback": runs[-1]["fallback"],
        "executed": runs[-1]["executed"],
        "nodes": runs[-1]["nodes"],
        "ok": all(r["ok"] for r in runs),
    }


def corpus(subset_path, gen_dir):
    """C1 suite (subset), C2 synthetic scale, C4 validate fixture."""
    items = []
    scripts = [l.strip() for l in open(subset_path)
               if l.strip() and not l.startswith("#")]
    items.append(("C1", "test/lambda subset", scripts))
    for size in (1000, 5000, 20000):
        path = os.path.join(gen_dir, f"gen_{size}.ls")
        if os.path.exists(path):
            items.append(("C2", f"synthetic {size}-line", [path]))
    return items


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repeats", type=int, default=5)
    ap.add_argument("--subset", default="test/lambda/interp_p0_subset.txt")
    ap.add_argument("--gen-dir", default="temp/interp_bench")
    ap.add_argument("--out", default="temp/interp_bench.tsv")
    args = ap.parse_args()

    os.makedirs("temp", exist_ok=True)
    rows = []
    for item_id, label, scripts in corpus(args.subset, args.gen_dir):
        for tier in ("jit", "interp"):
            total_ms = 0.0
            peak = 0.0
            fallback = 0
            nodes = 0
            failures = 0
            for script in scripts:
                result = measure(["./lambda.exe", script], tier, args.repeats)
                total_ms += result["ms"]
                peak = max(peak, result["peak_rss_mb"])
                fallback += result["fallback"]
                nodes += result["nodes"]
                if not result["ok"]:
                    failures += 1
            rows.append((item_id, label, tier, len(scripts), total_ms, peak,
                         fallback, nodes, failures))
            print(f"{item_id:3s} {label:22s} {tier:6s} "
                  f"total={total_ms:9.1f} ms  peak_rss={peak:7.2f} MB  "
                  f"fallback={fallback}  failures={failures}")

    with open(args.out, "w") as f:
        f.write("# corpus\tlabel\ttier\tscripts\ttotal_ms\tpeak_rss_mb\tfallback\tnodes\tfailures\n")
        for row in rows:
            f.write("\t".join(str(c) for c in row) + "\n")
    print(f"wrote {args.out}")

    print("\n| # | Corpus | tier | scripts | total ms | peak RSS MB | fallback |")
    print("|---|---|---|---|---|---|---|")
    for item_id, label, tier, count, total_ms, peak, fallback, _nodes, _f in rows:
        print(f"| {item_id} | {label} | {tier} | {count} | {total_ms:.1f} | "
              f"{peak:.2f} | {fallback} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
