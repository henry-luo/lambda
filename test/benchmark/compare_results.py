#!/usr/bin/env python3
"""Compare two benchmark result JSONs (e.g. v9 vs v10).

Emits per-benchmark engine deltas and the aggregate geo-mean movement, and can
write a `historical_comparisons` block into the newer JSON's `_metadata` so
gen_overall_result.py renders it.

Usage:
  python3 test/benchmark/compare_results.py --old ...v9.json --new ...v10.json
  python3 test/benchmark/compare_results.py --old ... --new ... --inject \
      --old-label Result9 --new-label Result10
"""

import argparse
import json
import math

SUITE_ORDER = ["r7rs", "awfy", "beng", "kostya", "larceny", "jetstream"]
SUITE_LABELS = {
    "r7rs": "R7RS", "awfy": "AWFY", "beng": "BENG",
    "kostya": "KOSTYA", "larceny": "LARCENY", "jetstream": "JetStream",
}
ENGINES = ["mir", "lambdajs", "quickjs", "nodejs"]
NODE = "nodejs"


def load(path):
    with open(path) as f:
        return json.load(f)


def value_of(cell):
    return cell if isinstance(cell, (int, float)) else None


def geo_mean(values):
    values = [v for v in values if v and v > 0]
    if not values:
        return None
    return math.exp(sum(math.log(v) for v in values) / len(values))


def dedup_ratios(data, engine):
    """Best (lowest) timed value per benchmark name, ratio vs Node, deduped."""
    best = {}
    for suite in SUITE_ORDER:
        for name, bench in data.get(suite, {}).items():
            e = value_of(bench.get(engine))
            n = value_of(bench.get(NODE))
            if e is None or n is None:
                continue
            prev = best.get(name)
            if prev is None or e < prev[0]:
                best[name] = (e, n)
    return {name: e / n for name, (e, n) in best.items()}


def suite_geo(data, engine, suite):
    vals = []
    for name, bench in data.get(suite, {}).items():
        e = value_of(bench.get(engine))
        n = value_of(bench.get(NODE))
        if e is not None and n is not None and n > 0:
            vals.append(e / n)
    return geo_mean(vals), len(vals), len(data.get(suite, {}))


def fmt_ratio(v):
    if v is None:
        return "n/a"
    if v >= 100:
        return f"{v:.0f}x"
    if v >= 10:
        return f"{v:.1f}x"
    return f"{v:.2f}x"


def fmt_change(old, new):
    """Improvement factor: >1 means the new run is faster relative to Node."""
    if old is None or new is None or new == 0:
        return "n/a"
    f = old / new
    if f >= 1.0:
        return f"{f:.2f}x better"
    return f"{1.0 / f:.2f}x worse"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old", required=True)
    ap.add_argument("--new", required=True)
    ap.add_argument("--old-label", default="old")
    ap.add_argument("--new-label", default="new")
    ap.add_argument("--engine", default="lambdajs,mir")
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--inject", action="store_true",
                    help="write historical_comparisons into the new JSON metadata")
    args = ap.parse_args()

    old, new = load(args.old), load(args.new)
    engines = [e.strip() for e in args.engine.split(",") if e.strip()]

    comparisons = []
    for engine in engines:
        o_r, n_r = dedup_ratios(old, engine), dedup_ratios(new, engine)
        o_geo, n_geo = geo_mean(list(o_r.values())), geo_mean(list(n_r.values()))

        print(f"\n{'=' * 78}\n{engine.upper()} — {args.old_label} vs {args.new_label}\n{'=' * 78}")
        print(f"dedup geo mean vs Node: {fmt_ratio(o_geo)} -> {fmt_ratio(n_geo)}  "
              f"({fmt_change(o_geo, n_geo)})   coverage {len(o_r)} -> {len(n_r)}")

        suite_rows = []
        print(f"\n{'suite':<12}{args.old_label:>12}{args.new_label:>12}  change")
        for suite in SUITE_ORDER:
            og, oc, ot = suite_geo(old, engine, suite)
            ng, nc, nt = suite_geo(new, engine, suite)
            print(f"{SUITE_LABELS[suite]:<12}{fmt_ratio(og):>12}{fmt_ratio(ng):>12}  "
                  f"{fmt_change(og, ng)}   ({oc}/{ot} -> {nc}/{nt} timed)")
            suite_rows.append([SUITE_LABELS[suite], fmt_ratio(og), fmt_ratio(ng),
                               fmt_change(og, ng)])

        common = sorted(set(o_r) & set(n_r), key=lambda k: o_r[k] / n_r[k])
        print(f"\nper-benchmark ratio vs Node (worst regressions first, then best gains):")
        print(f"{'bench':<18}{args.old_label:>10}{args.new_label:>10}  change")
        for name in common[:args.top]:
            print(f"{name:<18}{fmt_ratio(o_r[name]):>10}{fmt_ratio(n_r[name]):>10}  "
                  f"{fmt_change(o_r[name], n_r[name])}")
        if len(common) > args.top:
            print("   ...")
            for name in common[-args.top:]:
                print(f"{name:<18}{fmt_ratio(o_r[name]):>10}{fmt_ratio(n_r[name]):>10}  "
                      f"{fmt_change(o_r[name], n_r[name])}")

        only_old = sorted(set(o_r) - set(n_r))
        only_new = sorted(set(n_r) - set(o_r))
        if only_old:
            print(f"\ntimed in {args.old_label} but not {args.new_label}: {', '.join(only_old)}")
        if only_new:
            print(f"newly timed in {args.new_label}: {', '.join(only_new)}")

        comparisons.append({
            "engine": engine,
            "overall": [fmt_ratio(o_geo), fmt_ratio(n_geo), fmt_change(o_geo, n_geo),
                        len(o_r), len(n_r)],
            "suite_rows": suite_rows,
        })

    if args.inject:
        blocks = []
        for c in comparisons:
            label = {"lambdajs": "LambdaJS", "mir": "Lambda/MIR"}.get(c["engine"], c["engine"])
            o_geo, n_geo, change, o_cov, n_cov = c["overall"]
            blocks.append({
                "title": f"{label} vs {args.old_label}",
                "notes": [
                    f"Same host, same Node.js/QuickJS versions, same 3-run median "
                    f"`__TIMING__` protocol as {args.old_label}. Ratios are "
                    f"{label}/Node.js deduplicated geometric means; lower is better.",
                ],
                "tables": [
                    {
                        "caption": f"{label} overall comparison (dedup)",
                        "columns": ["Baseline", f"{args.old_label} {label}/Node",
                                    f"{args.new_label} {label}/Node", "Coverage", "Change"],
                        "rows": [[args.old_label, o_geo, n_geo,
                                  f"{o_cov} -> {n_cov} timed", change]],
                    },
                    {
                        "caption": f"{label} suite comparison, {args.old_label} vs {args.new_label} (raw per-suite geo)",
                        "columns": ["Suite", args.old_label, args.new_label, "Change"],
                        "rows": c["suite_rows"],
                    },
                ],
            })
        new.setdefault("_metadata", {})["historical_comparisons"] = blocks
        with open(args.new, "w") as f:
            json.dump(new, f, indent=1)
        print(f"\ninjected historical_comparisons ({len(blocks)} block(s)) into {args.new}")


if __name__ == "__main__":
    main()
