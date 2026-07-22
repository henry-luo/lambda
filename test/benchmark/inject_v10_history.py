#!/usr/bin/env python3
"""Inject the Result9 -> Result10 comparison blocks into benchmark_results_v10.json.

Keeping the analysis in `_metadata.historical_comparisons` means
gen_overall_result.py can regenerate Overall_Result10.md from the JSON alone.
"""

import json
import math

V9 = "test/benchmark/benchmark_results_v9.json"
V10 = "test/benchmark/benchmark_results_v10.json"
SUITES = ["r7rs", "awfy", "beng", "kostya", "larceny", "jetstream"]
LABELS = {"r7rs": "R7RS", "awfy": "AWFY", "beng": "BENG",
          "kostya": "KOSTYA", "larceny": "LARCENY", "jetstream": "JetStream"}


def num(v):
    return v if isinstance(v, (int, float)) else None


def dedup(data, engine):
    best = {}
    for suite in SUITES:
        for name, bench in data.get(suite, {}).items():
            e, n = num(bench.get(engine)), num(bench.get("nodejs"))
            if e is None or n is None or n <= 0:
                continue
            if name not in best or e < best[name][0]:
                best[name] = (e, n)
    return {k: v[0] / v[1] for k, v in best.items()}


def geo(vals):
    vals = [v for v in vals if v and v > 0]
    return math.exp(sum(math.log(v) for v in vals) / len(vals)) if vals else None


def fr(v):
    if v is None:
        return "n/a"
    return f"{v:.0f}x" if v >= 100 else (f"{v:.1f}x" if v >= 10 else f"{v:.2f}x")


def change(old, new):
    if old is None or new is None or new == 0:
        return "n/a"
    f = old / new
    return f"{f:.2f}x better" if f >= 1.0 else f"{1.0 / f:.2f}x worse"


v9, v10 = json.load(open(V9)), json.load(open(V10))

ljs9, ljs10 = dedup(v9, "lambdajs"), dedup(v10, "lambdajs")
mir9, mir10 = dedup(v9, "mir"), dedup(v10, "mir")
ljs_common = sorted(set(ljs9) & set(ljs10))
mir_common = sorted(set(mir9) & set(mir10))

# Per-benchmark movers, biggest factor first.
def movers(a, b, common, improved, limit=10):
    rows = sorted(common, key=lambda k: (a[k] / b[k]), reverse=improved)
    out = []
    for k in rows[:limit]:
        if improved and b[k] >= a[k]:
            continue
        if not improved and b[k] <= a[k]:
            continue
        out.append([k, fr(a[k]), fr(b[k]), change(a[k], b[k])])
    return out


# MIR failure inventory.
mir_lost = []
for suite in SUITES:
    for name, bench in v10.get(suite, {}).items():
        st10 = bench.get("_status", {}).get("mir")
        b9 = v9.get(suite, {}).get(name, {})
        if st10 != "ok" and b9.get("_status", {}).get("mir") == "ok":
            cause = {
                "exit_-6": "SIGABRT — mir-scalar-invariant",
                "exit_1": "compile rejected",
            }.get(st10, st10)
            mir_lost.append([f"{suite}/{name}", f"{num(b9.get('mir')):.3g} ms",
                             str(st10), cause])

blocks = [
    {
        "title": "R0 baseline status — read this before using the numbers above",
        "notes": [
            "Result10 re-measures current master on the same host, Node.js v22.13.0 and QuickJS 2025-09-13, "
            "with the same clean release build, 3-run median `__TIMING__` protocol and 180 s timeout as Result9. "
            "It is the R0 deliverable of `vibe/Lambda_Tuning_Proposal.md`.",
            "**The Lambda/MIR column is not a usable baseline.** 12 benchmarks that ran in Result9 no longer run: "
            "10 abort at transpile time with `mir-scalar-invariant: unresolved call retains scalar home` "
            "(`lambda/mir_emitter_shared.hpp`, `em_emit_unknown_call`), and 2 are rejected by the front end. "
            "The abort was introduced by commit `e30dc677b` (\"impl scalar GC invariant\"), which replaced the previous "
            "`em_heap_rehome_item_arg()` fallback for unresolved calls with a hard `abort()`. Because the failing rows "
            "drop out of the geometric mean, the MIR/Node figures in this report compare different benchmark "
            "populations and must not be quoted as a like-for-like movement.",
            "**The LambdaJS column is usable but bimodal**, and its headline moved for two separable reasons: a real "
            "broad regression on the small/mid cluster, and a coverage change (`cd` and `hashmap` are timed for the "
            "first time in Result10, both above 2000x, which raises the mean by construction). The like-for-like "
            "figures below hold the benchmark population fixed.",
        ],
        "tables": [{
            "caption": "Headline, all-timed vs like-for-like (deduplicated, engine/Node geometric mean)",
            "columns": ["Engine", "Result9 all-timed", "Result10 all-timed", "Result9 like-for-like",
                        "Result10 like-for-like", "Common rows", "Like-for-like change"],
            "rows": [
                ["LambdaJS", f"{fr(geo(list(ljs9.values())))} (n={len(ljs9)})",
                 f"{fr(geo(list(ljs10.values())))} (n={len(ljs10)})",
                 fr(geo([ljs9[k] for k in ljs_common])), fr(geo([ljs10[k] for k in ljs_common])),
                 str(len(ljs_common)),
                 change(geo([ljs9[k] for k in ljs_common]), geo([ljs10[k] for k in ljs_common]))],
                ["Lambda/MIR", f"{fr(geo(list(mir9.values())))} (n={len(mir9)})",
                 f"{fr(geo(list(mir10.values())))} (n={len(mir10)})",
                 fr(geo([mir9[k] for k in mir_common])), fr(geo([mir10[k] for k in mir_common])),
                 str(len(mir_common)),
                 change(geo([mir9[k] for k in mir_common]), geo([mir10[k] for k in mir_common]))],
            ],
        }],
    },
    {
        "title": "Lambda/MIR — benchmarks lost since Result9",
        "notes": [
            "All 10 SIGABRT rows share one message and one origin. Reproduce with "
            "`./lambda.exe run test/benchmark/jetstream/deltablue2.ls` (exit 134). "
            "`r7rs/fft` is rejected by the type checker (`cannot assign float value to var 'm' of type int`) and "
            "`beng/pidigits` by the parser (`Unexpected syntax near '1'`) — three distinct regression classes, not one.",
        ],
        "tables": [{
            "caption": "Regressed from timed to failing",
            "columns": ["Benchmark", "Result9 MIR", "Result10 status", "Cause"],
            "rows": mir_lost,
        }],
    },
    {
        "title": "LambdaJS vs Result9 — where it moved",
        "notes": [
            "The disaster tail that defined Result9's geometric mean has largely collapsed, which is the expected "
            "signature of the landed inline self-tagged doubles, side-number-stack scalars and safepoint rooting. "
            "The offsetting regression is concentrated in the previously-fast small and mid benchmarks. "
            "These are not measurement artifacts: Node.js's own times for the same rows moved by under 10% between "
            "the two rounds, so the host is consistent and the movement is in the engine. In absolute terms "
            "`awfy/sieve` went 0.49 ms to 50.1 ms (102x slower), `larceny/puzzle` 22.7 ms to 769 ms (34x), "
            "`larceny/array1` 3.40 ms to 28.2 ms (8.3x) and `primes` 15.9 ms to 105 ms (6.6x). The shape — "
            "previously-near-parity numeric/array loops losing an order of magnitude while the boxing-bound tail "
            "improves — is consistent with lost native specialization on these functions, and is the first thing "
            "to investigate before any further tuning work is ranked.",
            f"Across the {len(ljs_common)} benchmarks timed in both rounds, {sum(1 for k in ljs_common if ljs10[k] < ljs9[k])} improved "
            f"and {sum(1 for k in ljs_common if ljs10[k] > ljs9[k])} regressed.",
        ],
        "tables": [
            {
                "caption": "Largest improvements (LambdaJS/Node ratio)",
                "columns": ["Benchmark", "Result9", "Result10", "Change"],
                "rows": movers(ljs9, ljs10, ljs_common, improved=True, limit=10),
            },
            {
                "caption": "Largest regressions (LambdaJS/Node ratio)",
                "columns": ["Benchmark", "Result9", "Result10", "Change"],
                "rows": movers(ljs9, ljs10, ljs_common, improved=False, limit=10),
            },
            {
                "caption": "Per-suite LambdaJS/Node geometric mean",
                "columns": ["Suite", "Result9", "Result10", "Change"],
                "rows": [
                    [LABELS[s],
                     fr(geo([num(b.get("lambdajs")) / num(b.get("nodejs"))
                             for b in v9.get(s, {}).values()
                             if num(b.get("lambdajs")) and num(b.get("nodejs"))])),
                     fr(geo([num(b.get("lambdajs")) / num(b.get("nodejs"))
                             for b in v10.get(s, {}).values()
                             if num(b.get("lambdajs")) and num(b.get("nodejs"))])),
                     change(geo([num(b.get("lambdajs")) / num(b.get("nodejs"))
                                 for b in v9.get(s, {}).values()
                                 if num(b.get("lambdajs")) and num(b.get("nodejs"))]),
                            geo([num(b.get("lambdajs")) / num(b.get("nodejs"))
                                 for b in v10.get(s, {}).values()
                                 if num(b.get("lambdajs")) and num(b.get("nodejs"))]))]
                    for s in SUITES
                ],
            },
        ],
    },
]

v10.setdefault("_metadata", {})["historical_comparisons"] = blocks
with open(V10, "w") as f:
    json.dump(v10, f, indent=1)
print(f"injected {len(blocks)} comparison blocks; MIR lost rows: {len(mir_lost)}")
