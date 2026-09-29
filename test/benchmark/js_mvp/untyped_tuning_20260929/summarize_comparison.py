#!/usr/bin/env python3
"""Summarize the fixed-population full-JS/MVP output and timing screen."""

import argparse
import hashlib
import json
from pathlib import Path
import statistics


def file_sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fixed_rows(artifact):
    return {
        row["workload"]["id"]: row
        for row in artifact["rows"]
        if row["status"] == "validated"
        and row["workload"]["id"] != "jetstream/navier_stokes"
    }


def measures(rows):
    values = list(rows.values())
    full_exec = [row["paired"]["control"]["median_exec_ms"] for row in values]
    mvp_exec = [row["paired"]["candidate"]["median_exec_ms"] for row in values]
    full_wall = [row["wall_medians_ms"]["control"] for row in values]
    mvp_wall = [row["wall_medians_ms"]["candidate"] for row in values]
    return {
        "rows": len(values),
        "full_over_mvp_exec_geomean": statistics.geometric_mean(
            row["full_over_mvp"] for row in values),
        "full_over_mvp_exec_sum_ratio": sum(full_exec) / sum(mvp_exec),
        "full_over_mvp_wall_geomean": statistics.geometric_mean(
            full / mvp for full, mvp in zip(full_wall, mvp_wall)),
        "full_wins": sum(full < mvp for full, mvp in zip(full_exec, mvp_exec)),
        "mvp_wins": sum(mvp < full for full, mvp in zip(full_exec, mvp_exec)),
        "full_exec_median_sum_ms": sum(full_exec),
        "mvp_exec_median_sum_ms": sum(mvp_exec),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    before = json.loads(args.before.read_text())
    after = json.loads(args.after.read_text())
    before_rows = fixed_rows(before)
    after_rows = fixed_rows(after)
    common = sorted(before_rows.keys() & after_rows.keys())
    same_sources = all(
        before_rows[name]["script_sha256"] == after_rows[name]["script_sha256"]
        for name in common)
    checks = {
        "before_rows": len(before["rows"]),
        "after_rows": len(after["rows"]),
        "before_validated": len(before_rows),
        "after_validated": len(after_rows),
        "common_validated": len(common),
        "same_source_hashes": same_sources,
        "after_all_but_navier_validated": len(after_rows) == len(after["rows"]) - 1,
        "after_binary_unchanged": after["metadata"].get("binary_unchanged"),
        "after_sources_unchanged": after["metadata"].get("sources_unchanged"),
    }
    summary = {
        "method": "Fixed common validated JS sources. Ratios from source-defined execution medians; separate-day snapshots are descriptive, not a causal replay.",
        "before": {
            "artifact": str(args.before),
            "sha256": file_sha256(args.before),
            "binary_sha256": before["metadata"]["binary_sha256"],
            **measures({name: before_rows[name] for name in common}),
        },
        "after": {
            "artifact": str(args.after),
            "sha256": file_sha256(args.after),
            "binary_sha256": after["metadata"]["binary_sha256"],
            **measures({name: after_rows[name] for name in common}),
        },
        "checks": checks,
    }
    if len(after["rows"]) != 63 or len(common) != 62 or not same_sources:
        raise SystemExit("expected 63 rows and 62 same-source validated comparisons")
    if not all(checks[key] for key in (
            "after_all_but_navier_validated", "after_binary_unchanged",
            "after_sources_unchanged")):
        raise SystemExit("comparison output or provenance did not validate")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
