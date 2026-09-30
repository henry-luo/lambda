#!/usr/bin/env python3
"""Report fixed-family and all-row paired release results for typed Lambda."""

import argparse
import hashlib
import json
import math
import random
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_FAMILIES = ROOT / "test/benchmark/typed_common_overhead_families.json"


def upper_median(values):
    ordered = sorted(values)
    return ordered[len(ordered) // 2]


def geomean(values):
    return math.exp(sum(math.log(value) for value in values) / len(values))


def paired_samples(row):
    return [
        (pair["control"]["exec_ms"], pair["candidate"]["exec_ms"])
        for pair in row["pairs"]
        if pair["control"]["exec_ms"] is not None
        and pair["candidate"]["exec_ms"] is not None
    ]


def summarize(rows, resamples, seed):
    """Bootstrap paired timing noise conditional on these fixed workload rows."""
    point = geomean([row["candidate_over_control_median_ratio"] for row in rows])
    observations = [paired_samples(row) for row in rows]
    if any(not samples for samples in observations):
        raise ValueError("a selected row has no valid paired timing samples")
    rng = random.Random(seed)
    draws = []
    for _ in range(resamples):
        ratios = []
        for samples in observations:
            picked = [samples[rng.randrange(len(samples))]
                      for _ in range(len(samples))]
            ratios.append(upper_median([item[1] for item in picked]) /
                          upper_median([item[0] for item in picked]))
        draws.append(geomean(ratios))
    draws.sort()
    lower = draws[max(0, math.floor(0.025 * len(draws)))]
    upper = draws[min(len(draws) - 1, math.ceil(0.975 * len(draws)) - 1)]
    return {
        "rows": len(rows),
        "geometric_mean_ratio": point,
        "paired_bootstrap_95_percent_interval": [lower, upper],
        "interval_scope": "timing noise conditional on fixed workload rows",
        "control_sum_of_medians_ms": sum(row["control"]["median_exec_ms"] for row in rows),
        "candidate_sum_of_medians_ms": sum(row["candidate"]["median_exec_ms"] for row in rows),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path, help="full typed paired-run JSON")
    parser.add_argument("--families", type=Path, default=DEFAULT_FAMILIES)
    parser.add_argument("--c2mir", type=Path,
                        help="contemporaneous repeated C2MIR reference artifact")
    parser.add_argument("--expected-rows", type=int, default=63)
    parser.add_argument("--resamples", type=int, default=10000)
    parser.add_argument("--seed", type=int, default=260026)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "temp/typed_common_overhead_report.json")
    args = parser.parse_args()
    if args.resamples < 1:
        parser.error("--resamples must be positive")

    artifact = json.loads(args.artifact.read_text())
    families = json.loads(args.families.read_text())
    rows = [row for row in artifact["rows"] if row["variant"] == "typed"]
    by_name = {f"{row['suite']}/{row['name']}": row for row in rows}
    if len(rows) != args.expected_rows or len(by_name) != len(rows):
        raise ValueError(f"expected {args.expected_rows} unique typed rows, got {len(rows)}")
    for name, row in by_name.items():
        if (row["status"] != "ok" or not row["stdout_equal_all"] or
                row["expected_stdout_matches_all"] is False or not row["pairs_valid"]):
            raise ValueError(f"row {name} lacks valid, equal-output paired samples")
        if not row["candidate_over_control_median_ratio"] or not paired_samples(row):
            raise ValueError(f"row {name} lacks a positive execution-time ratio")

    family_rows = {}
    assigned = set()
    for family, members in families["families"].items():
        if not members:
            raise ValueError(f"family {family} is empty")
        if len(set(members)) != len(members):
            raise ValueError(f"family {family} repeats a row")
        missing = set(members) - by_name.keys()
        repeated = set(members) & assigned
        if missing or repeated:
            raise ValueError(f"family {family}: missing={sorted(missing)}, repeated={sorted(repeated)}")
        assigned.update(members)
        family_rows[family] = [by_name[name] for name in members]

    summary = {
        "schema_version": 1,
        "paired_artifact": str(args.artifact),
        "paired_artifact_sha256": hashlib.sha256(args.artifact.read_bytes()).hexdigest(),
        "families_file": str(args.families),
        "families_sha256": hashlib.sha256(args.families.read_bytes()).hexdigest(),
        "control_sha256": artifact["_metadata"]["control"]["sha256"],
        "candidate_sha256": artifact["_metadata"]["candidate"]["sha256"],
        "bootstrap_resamples": args.resamples,
        "bootstrap_seed": args.seed,
        "overall": summarize(rows, args.resamples, args.seed),
        "families": {},
        "unassigned_rows": sorted(by_name.keys() - assigned),
        "row_ratios": {
            name: row["candidate_over_control_median_ratio"]
            for name, row in sorted(by_name.items())
        },
    }
    for family, members in family_rows.items():
        report = summarize(members, args.resamples, args.seed)
        report["members"] = families["families"][family]
        report["meets_5_percent_and_ci_target"] = (
            family != "regression_controls"
            and report["geometric_mean_ratio"] <= 0.95
            and report["paired_bootstrap_95_percent_interval"][1] < 1.0
        )
        summary["families"][family] = report
    summary["independent_families_meeting_target"] = sum(
        report["meets_5_percent_and_ci_target"]
        for report in summary["families"].values()
    )
    if args.c2mir:
        c2mir_artifact = json.loads(args.c2mir.read_text())
        c2mir_rows = {
            f"{row['suite']}/{row['name']}": row
            for row in c2mir_artifact["rows"]
        }
        if len(c2mir_rows) != len(c2mir_artifact["rows"]):
            raise ValueError("C2MIR reference has duplicate rows")
        missing = by_name.keys() - c2mir_rows.keys()
        if missing:
            raise ValueError(f"C2MIR reference lacks typed rows: {sorted(missing)}")
        lambda_over_c = {}
        control_over_c = {}
        for name, row in sorted(by_name.items()):
            reference = c2mir_rows[name]
            if (reference["status"] != "ok" or reference["valid_samples"] < 1 or
                    reference["median_exec_ms"] <= 0 or
                    any(sample["status"] != "ok" or
                        sample["expected_stdout_matches"] is False
                        for sample in reference["samples"])):
                raise ValueError(f"C2MIR reference row {name} is invalid")
            lambda_over_c[name] = (row["candidate"]["median_exec_ms"] /
                                   reference["median_exec_ms"])
            control_over_c[name] = (row["control"]["median_exec_ms"] /
                                    reference["median_exec_ms"])
        summary["c2mir_reference"] = {
            "artifact": str(args.c2mir),
            "artifact_sha256": hashlib.sha256(args.c2mir.read_bytes()).hexdigest(),
            "matched_rows": len(lambda_over_c),
            "comparison_scope": "cross-language source-relative medians, not paired samples",
            "candidate_over_c2mir_geometric_mean": geomean(lambda_over_c.values()),
            "control_over_c2mir_geometric_mean": geomean(control_over_c.values()),
            "candidate_over_c2mir_rows": lambda_over_c,
        }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print(f"overall: {summary['overall']['geometric_mean_ratio']:.4f}x "
          f"({len(rows)} typed rows)")
    for family, report in summary["families"].items():
        print(f"{family}: {report['geometric_mean_ratio']:.4f}x "
              f"CI [{report['paired_bootstrap_95_percent_interval'][0]:.4f}, "
              f"{report['paired_bootstrap_95_percent_interval'][1]:.4f}]")
    if args.c2mir:
        reference = summary["c2mir_reference"]
        print(f"candidate/C2MIR: "
              f"{reference['candidate_over_c2mir_geometric_mean']:.4f}x "
              f"({reference['matched_rows']} source-relative rows)")
    print(f"saved {args.output}")


if __name__ == "__main__":
    main()
