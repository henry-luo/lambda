#!/usr/bin/env python3
"""Merge engine columns from one benchmark result JSON into another.

Adding a late-measured engine to a published snapshot must not silently rewrite
that snapshot's provenance: `run_benchmarks.py` overwrites `_metadata` wholesale
on every run, which would drop the archived-binary name, the Test262 gate, and
the engine list the original numbers were taken under. This tool copies only the
requested engine cells and records where they came from, so the report can say
that a column was measured separately.

Usage:
  python3 test/benchmark/merge_engine_results.py \
      --into test/benchmark/benchmark_results_v18.json \
      --source test/benchmark/benchmark_results_v18_native.json \
      --engines c2mir,go
"""

import argparse
import datetime
import json
import pathlib
import sys


SUITE_KEYS_TO_SKIP = {"_metadata"}


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--into", required=True, type=pathlib.Path,
                        help="target result JSON, updated in place")
    parser.add_argument("--source", required=True, type=pathlib.Path,
                        help="result JSON to take engine columns from")
    parser.add_argument("--engines", required=True,
                        help="comma-separated engine keys to copy")
    parser.add_argument("--note", default=None,
                        help="free-text provenance note stored with the merge record")
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would change without writing")
    return parser.parse_args()


def merge(target, source, engines):
    """Copy engine cells for rows the target already has. Returns (copied, skipped)."""
    copied = []
    skipped = []
    for suite, source_benches in source.items():
        if suite in SUITE_KEYS_TO_SKIP or not isinstance(source_benches, dict):
            continue
        target_benches = target.get(suite)
        for bench_name, source_row in source_benches.items():
            if not isinstance(source_row, dict):
                continue
            # The target snapshot defines the report population — a row that only
            # exists in the source would add a benchmark the other engines never ran.
            if target_benches is None or bench_name not in target_benches:
                skipped.append(f"{suite}/{bench_name}")
                continue
            target_row = target_benches[bench_name]
            for engine in engines:
                if engine not in source_row:
                    continue
                target_row[engine] = source_row[engine]
                for key in ("_status", "_status_detail"):
                    value = source_row.get(key, {}).get(engine)
                    if value is not None:
                        target_row.setdefault(key, {})[engine] = value
                copied.append(f"{suite}/{bench_name}:{engine}")
    return copied, skipped


def main():
    args = parse_args()
    engines = [e.strip() for e in args.engines.split(",") if e.strip()]
    if not engines:
        print("no engines requested", file=sys.stderr)
        return 2

    target = json.loads(args.into.read_text())
    source = json.loads(args.source.read_text())
    copied, skipped = merge(target, source, engines)

    source_meta = source.get("_metadata", {})
    record = {
        "engines": engines,
        "source": str(args.source),
        "source_started_at": source_meta.get("started_at"),
        "source_finished_at": source_meta.get("finished_at"),
        "source_runs": source_meta.get("runs"),
        "source_platform": source_meta.get("platform"),
        "merged_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "cells": len(copied),
    }
    if args.note:
        record["note"] = args.note
    target.setdefault("_metadata", {}).setdefault("merged_engines", []).append(record)

    print(f"copied {len(copied)} cells for {', '.join(engines)}")
    if skipped:
        print(f"skipped {len(skipped)} source rows absent from the target: "
              + ", ".join(sorted(set(skipped))))
    if args.dry_run:
        print("--dry-run: target not written")
        return 0
    args.into.write_text(json.dumps(target, indent=2, default=str) + "\n")
    print(f"wrote {args.into}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
