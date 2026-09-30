#!/usr/bin/env python3
"""Summarize opt-in JIT call counters without treating call counts as CPU time."""

import argparse
import csv
import json
from pathlib import Path


def summarize(path):
    calls = {}
    categories = {}
    overflow = None
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames not in (["kind", "name", "calls"],
                                     ["kind", "name", "count"]):
            raise ValueError(f"{path}: unexpected profile header")
        count_column = reader.fieldnames[2]
        for row in reader:
            count = int(row[count_column])
            if count < 0:
                raise ValueError(f"{path}: negative call count")
            if row["kind"] == "overflow":
                overflow = count
                continue
            key = (row["kind"], row["name"])
            calls[key] = calls.get(key, 0) + count
            categories[row["kind"]] = categories.get(row["kind"], 0) + count
    if overflow is None:
        raise ValueError(f"{path}: missing overflow row")
    ranked = sorted(calls.items(), key=lambda entry: (-entry[1], entry[0]))
    return {
        "profile": str(path),
        "total_classified_calls": sum(
            categories.get(kind, 0)
            for kind in ("helper", "raw", "boxed", "indirect", "unknown")
        ),
        "total_classified_events": sum(categories.values()),
        "overflow_calls": overflow,
        "categories": categories,
        "ranked_calls": [
            {"kind": kind, "name": name, "calls": count}
            for (kind, name), count in ranked
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profiles", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = {path.stem: summarize(path) for path in args.profiles}
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
