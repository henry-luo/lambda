#!/usr/bin/env python3
"""Rank opt-in COW sites and map JIT return PCs using MIR address notices."""

import argparse
import bisect
import csv
import json
import re
from pathlib import Path


ADDRESS = re.compile(r"mir-code-addr: (\S+) (0x[0-9a-fA-F]+)")


def read_jit_addresses(path: Path):
    starts = {}
    for line in path.read_text(errors="replace").splitlines():
        match = ADDRESS.search(line)
        if match:
            starts[int(match.group(2), 16)] = match.group(1)
    addresses = sorted(starts)
    return addresses, starts


def read_sites(path: Path, addresses, names, max_span):
    rows = []
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            if row["event"] == "overflow":
                if int(row["count"]):
                    raise ValueError(f"COW site table overflowed by {row['count']} events")
                continue
            pc = int(row["caller_pc"], 16)
            count = int(row["count"])
            if not count:
                continue
            symbol = row["native_symbol"]
            offset = int(row["symbol_offset"], 16)
            if symbol:
                site = f"{symbol}+0x{offset:x}"
                kind = "native"
            else:
                index = bisect.bisect_right(addresses, pc) - 1
                start = addresses[index] if index >= 0 else None
                if start is not None and pc - start <= max_span:
                    site = f"{names[start]}+0x{pc - start:x}"
                    kind = "jit"
                else:
                    site = f"unresolved:{row['caller_pc']}"
                    kind = "unresolved"
            rows.append({
                "event": row["event"], "type": row["type"],
                "site": site, "kind": kind, "count": count,
                "bytes": int(row["bytes"]),
            })
    return sorted(rows, key=lambda row: (-row["count"], row["site"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sites", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--max-jit-span", type=int, default=65536)
    args = parser.parse_args()
    addresses, names = read_jit_addresses(args.log)
    rows = read_sites(args.sites, addresses, names, args.max_jit_span)
    totals = {}
    for row in rows:
        key = f"{row['event']}:{row['type']}"
        entry = totals.setdefault(key, {"count": 0, "bytes": 0})
        entry["count"] += row["count"]
        entry["bytes"] += row["bytes"]
    report = {"site_count": len(rows), "totals": totals, "sites": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    for row in rows[:25]:
        print(f"{row['event']:12} {row['type']:8} {row['count']:>9} "
              f"{row['bytes']:>10} {row['site']}")


if __name__ == "__main__":
    main()
