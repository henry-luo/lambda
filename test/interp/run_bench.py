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

# The shipped default path is not uniformly native codegen: modules over
# MIR_LARGE_MODULE_INSN_THRESHOLD (100k insns) link through
# MIR_set_interp_interface instead. A jit-tier row that does not say which mode
# ran is not interpretable, so the mode is read back out of the run log.
LARGE_INTERP_RE = re.compile(r"lambda-mir: .*-> MIR interpreter")


def jit_mode_of_last_run(log_path="log.txt"):
    try:
        with open(log_path, errors="replace") as f:
            return "MIR-interp" if LARGE_INTERP_RE.search(f.read()) else "native"
    except OSError:
        return "unknown"


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
        "jit_mode": jit_mode_of_last_run() if tier != "interp" else "-",
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
        # If any run of a corpus item took the escape hatch, the item's
        # baseline is MIR-interp — report the pessimistic answer.
        "jit_mode": ("MIR-interp" if any(r["jit_mode"] == "MIR-interp" for r in runs)
                     else runs[-1]["jit_mode"]),
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
            jit_mode = "-"
            for script in scripts:
                result = measure(["./lambda.exe", script], tier, args.repeats)
                total_ms += result["ms"]
                peak = max(peak, result["peak_rss_mb"])
                fallback += result["fallback"]
                nodes += result["nodes"]
                if result["jit_mode"] == "MIR-interp":
                    jit_mode = "MIR-interp"
                elif jit_mode == "-":
                    jit_mode = result["jit_mode"]
                if not result["ok"]:
                    failures += 1
            rows.append((item_id, label, tier, jit_mode, len(scripts), total_ms, peak,
                         fallback, nodes, failures))
            print(f"{item_id:3s} {label:22s} {tier:6s} mode={jit_mode:10s} "
                  f"total={total_ms:9.1f} ms  peak_rss={peak:7.2f} MB  "
                  f"fallback={fallback}  failures={failures}")

    with open(args.out, "w") as f:
        f.write("# corpus\tlabel\ttier\tjit_mode\tscripts\ttotal_ms\tpeak_rss_mb\tfallback\tnodes\tfailures\n")
        for row in rows:
            f.write("\t".join(str(c) for c in row) + "\n")
    print(f"wrote {args.out}")

    print("\n| # | Corpus | tier | jit mode | scripts | total ms | peak RSS MB | fallback |")
    print("|---|---|---|---|---|---|---|---|")
    for item_id, label, tier, mode, count, total_ms, peak, fallback, _n, _f in rows:
        print(f"| {item_id} | {label} | {tier} | {mode} | {count} | {total_ms:.1f} | "
              f"{peak:.2f} | {fallback} |")
    if any(r[3] == "MIR-interp" for r in rows):
        print("\nNOTE: rows marked MIR-interp took the >100k-insn escape hatch, so "
              "their baseline skipped codegen. Re-run with LAMBDA_JS_LARGE_INTERP=0 "
              "for the forced-native (--jit-all) comparison.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
