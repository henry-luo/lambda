#!/usr/bin/env python3
"""Summarize benchmark results from JSON."""
import json

with open("test/benchmark/benchmark_results.json") as f:
    results = json.load(f)

engines = ["mir", "c2mir", "lambdajs", "quickjs", "nodejs"]
labels = {"mir": "MIR", "c2mir": "C2MIR", "lambdajs": "LambdaJS", "quickjs": "QuickJS", "nodejs": "Node.js"}

def get_val(suite, bench, eng, idx):
    """Get wall (idx=0) or exec (idx=1) time."""
    entry = results.get(suite, {}).get(bench, {}).get(eng)
    if entry is None:
        return None
    if isinstance(entry, list) and len(entry) > idx:
        return entry[idx]
    return None

# SET 1: SUITE TOTALS — WALL-CLOCK TIME
print("=" * 100)
print("SET 1: SUITE TOTALS — WALL-CLOCK TIME (ms, median of 3 runs)")
print("=" * 100)
hdr = f"{'Suite':16s}"
for e in engines:
    hdr += f"  {labels[e]:>10s}"
print(hdr)
print("-" * 70)

grand_wall = {e: 0.0 for e in engines}
grand_wall_count = {e: 0 for e in engines}

for suite in results:
    row = f"{suite:16s}"
    for e in engines:
        total = 0.0
        count = 0
        for bench in results[suite]:
            w = get_val(suite, bench, e, 0)
            if w is not None:
                total += w
                count += 1
        if count > 0:
            row += f"  {total:>10.0f}"
            grand_wall[e] += total
            grand_wall_count[e] += count
        else:
            row += f"  {'---':>10s}"
    print(row)

print("-" * 70)
row = f"{'GRAND TOTAL':16s}"
for e in engines:
    if grand_wall_count[e] > 0:
        row += f"  {grand_wall[e]:>10.0f}"
    else:
        row += f"  {'---':>10s}"
print(row)

row = f"{'  (# succeeded)':16s}"
for e in engines:
    row += f"  {grand_wall_count[e]:>10d}"
print(row)

# SET 2: SUITE TOTALS — SELF-REPORTED EXEC TIME
print()
print("=" * 100)
print("SET 2: SUITE TOTALS — SELF-REPORTED EXEC TIME (ms, median of 3 runs)")
print("=" * 100)
hdr = f"{'Suite':16s}"
for e in engines:
    hdr += f"  {labels[e]:>10s}"
print(hdr)
print("-" * 70)

grand_exec = {e: 0.0 for e in engines}
grand_exec_count = {e: 0 for e in engines}

for suite in results:
    row = f"{suite:16s}"
    for e in engines:
        total = 0.0
        count = 0
        for bench in results[suite]:
            ex = get_val(suite, bench, e, 1)
            if ex is not None:
                total += ex
                count += 1
        if count > 0:
            row += f"  {total:>10.1f}"
            grand_exec[e] += total
            grand_exec_count[e] += count
        else:
            row += f"  {'---':>10s}"
    print(row)

print("-" * 70)
row = f"{'GRAND TOTAL':16s}"
for e in engines:
    if grand_exec_count[e] > 0:
        row += f"  {grand_exec[e]:>10.1f}"
    else:
        row += f"  {'---':>10s}"
print(row)

row = f"{'  (# succeeded)':16s}"
for e in engines:
    row += f"  {grand_exec_count[e]:>10d}"
print(row)

# Compute "common benchmarks only" totals (only where ALL engines succeeded)
print()
print("=" * 100)
print("COMMON-ONLY TOTALS (benchmarks where ALL 5 engines reported exec time)")
print("=" * 100)

common_total = {e: 0.0 for e in engines}
common_count = 0
common_names = []

for suite in results:
    for bench in results[suite]:
        vals = {}
        all_ok = True
        for e in engines:
            ex = get_val(suite, bench, e, 1)
            if ex is not None:
                vals[e] = ex
            else:
                all_ok = False
                break
        if all_ok:
            common_count += 1
            common_names.append(f"{suite}/{bench}")
            for e in engines:
                common_total[e] += vals[e]

print(f"Common benchmarks ({common_count}): {', '.join(common_names)}")
print()
hdr = f"{'':16s}"
for e in engines:
    hdr += f"  {labels[e]:>10s}"
print(hdr)
print("-" * 70)
row = f"{'Exec Total (ms)':16s}"
for e in engines:
    row += f"  {common_total[e]:>10.1f}"
print(row)

# Node.js relative speed
if common_total["nodejs"] > 0:
    row = f"{'vs Node.js':16s}"
    for e in engines:
        ratio = common_total[e] / common_total["nodejs"]
        row += f"  {ratio:>10.2f}x"
    print(row)
