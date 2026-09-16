#!/usr/bin/env python3
"""Generate a peak-RSS comparison report from the unified memory results."""

import argparse
import json
import math
import statistics


ENGINES = ["mir", "mir_typed", "c2mir", "lambdajs", "quickjs", "nodejs"]
LABELS = {
    "mir": "Lambda-U",
    "mir_typed": "Lambda-T",
    "c2mir": "C2MIR",
    "lambdajs": "LambdaJS",
    "quickjs": "QuickJS",
    "nodejs": "Node.js",
}


def fmt_bytes(value):
    if value is None:
        return "—"
    if value >= 1 << 30:
        return f"{value / (1 << 30):.2f} GiB"
    return f"{value / (1 << 20):.1f} MiB"


def fmt_ratio(value):
    return "—" if value is None else f"{value:.2f}x"


def load_rows(path):
    with open(path) as stream:
        data = json.load(stream)
    rows = []
    for suite, suite_rows in data.items():
        if suite.startswith("_"):
            continue
        for name, row in suite_rows.items():
            raw = row.get("_raw_bytes", {})
            rows.append((suite, name, raw))
    return data.get("_metadata", {}), rows


def matched_geo_ratio(rows, engine):
    ratios = []
    for _, _, raw in rows:
        value = raw.get(engine)
        node = raw.get("nodejs")
        if value is not None and node is not None and node > 0:
            ratios.append(value / node)
    if not ratios:
        return None, 0
    return math.exp(sum(math.log(ratio) for ratio in ratios) / len(ratios)), len(ratios)


def render(metadata, rows, source_path):
    lines = [
        "# Memory Comparison Report — 2026-09-16",
        "",
        "## Result",
        "",
        "This report compares peak resident set size (RSS) for the canonical benchmark matrix. "
        "Each value is the median of three independent process runs.",
        "",
        "| Engine | Rows measured | Arithmetic mean | Median | Minimum | Maximum | Geo. ratio vs Node.js |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]

    for engine in ENGINES:
        values = [raw.get(engine) for _, _, raw in rows if raw.get(engine) is not None]
        geo, matched = matched_geo_ratio(rows, engine)
        if values:
            summary = (
                f"| {LABELS[engine]} | {len(values)} | {fmt_bytes(statistics.mean(values))} | "
                f"{fmt_bytes(statistics.median(values))} | {fmt_bytes(min(values))} | "
                f"{fmt_bytes(max(values))} | {fmt_ratio(geo)} ({matched} matched) |"
            )
        else:
            summary = f"| {LABELS[engine]} | 0 | — | — | — | — | — |"
        lines.append(summary)

    lines += [
        "",
        "The geometric ratio is computed row-by-row against Node.js using only rows measured "
        "by both engines; it is less dominated by the largest allocation-heavy outliers than "
        "the arithmetic mean.",
        "",
        "## Method",
        "",
        f"- Source data: `{source_path}`",
        f"- Command: `{metadata.get('command', 'not recorded')}`",
        f"- Finished: `{metadata.get('finished_at', 'not recorded')}`",
        f"- Platform: `{metadata.get('platform', 'not recorded')}`",
        f"- Lambda binary: `{metadata.get('lambda_exe', 'not recorded')}` "
        f"({metadata.get('lambda_exe_size_bytes', 0):,} bytes)",
        f"- Lambda commit: `{metadata.get('lambda_commit', 'not recorded')}`",
        f"- Node.js: `{metadata.get('node_version', 'not recorded')}`",
        f"- QuickJS: `{metadata.get('quickjs_version', 'not recorded')}`",
        "- Metric: process peak RSS from macOS `/usr/bin/time -l`; this is not heap-only memory.",
        "",
        "C2MIR values include the native C frontend and MIR generator process footprint. "
        "They are useful as a native reference, but are not the same runtime architecture "
        "as Lambda-U or Lambda-T.",
        "",
        "## Per-benchmark peaks",
        "",
        "| Benchmark | Lambda-U | Lambda-T | C2MIR | LambdaJS | QuickJS | Node.js |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]

    for suite, name, raw in rows:
        values = " | ".join(fmt_bytes(raw.get(engine)) for engine in ENGINES)
        lines.append(f"| `{suite}/{name}` | {values} |")

    missing = []
    for suite, name, raw in rows:
        absent = [LABELS[engine] for engine in ENGINES if raw.get(engine) is None]
        if absent:
            missing.append((suite, name, absent))

    lines += ["", "## Coverage notes", ""]
    if missing:
        for suite, name, absent in missing:
            lines.append(f"- `{suite}/{name}`: missing {', '.join(absent)}.")
    else:
        lines.append("- All requested engines measured every row.")
    lines += [
        "- LambdaJS is unavailable for the three BENG file-I/O workloads because its current "
        "runtime does not provide the required filesystem API; `text/prettier_ast` also failed "
        "during LambdaJS execution.",
        "- The memory runner currently does not measure LambdaJS or QuickJS JetStream rows; "
        "their six cells are intentionally unavailable rather than substituted.",
        "- AWFY Node.js memory runs use the checked-in standalone bundles when the optional "
        "`ref/are-we-fast-yet` checkout is absent.",
        "",
        "## Observations",
        "",
        "- The normal startup footprint is approximately 32–40 MiB for Lambda-U, 33–40 MiB "
        "for Lambda-T, 19–28 MiB for C2MIR, 35–70 MiB for LambdaJS, 1.7–5 MiB for QuickJS, "
        "and 42–60 MiB for Node.js.",
        "- Allocation-heavy rows dominate the maxima: LambdaJS reaches 2.65 GiB on `awfy/havlak` "
        "and 1.28 GiB on `text/hyphen`; typed Lambda reaches 284 MiB on `larceny/gcbench`.",
    ]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", default="test/benchmark/memory_results.json")
    parser.add_argument("output", nargs="?", default="test/benchmark/Memory_Comparison_20260916.md")
    args = parser.parse_args()
    metadata, rows = load_rows(args.input)
    with open(args.output, "w") as stream:
        stream.write(render(metadata, rows, args.input))
    print(f"Wrote {args.output} ({len(rows)} rows)")


if __name__ == "__main__":
    main()
