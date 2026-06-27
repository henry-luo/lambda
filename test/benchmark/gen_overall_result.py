#!/usr/bin/env python3
"""Generate an Overall_ResultN.md report from a benchmark result JSON file."""

import argparse
import datetime
import json
import math
import os
import platform
import subprocess


PROJECT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
os.chdir(PROJECT_ROOT)

DEFAULT_JSON = "test/benchmark/benchmark_results_v3.json"
DEFAULT_ENGINES = "mir,lambdajs,quickjs,nodejs"
NODE_ENGINE = "nodejs"

SUITE_ORDER = ["r7rs", "awfy", "beng", "kostya", "larceny", "jetstream"]
SUITE_LABELS = {
    "r7rs": "R7RS",
    "awfy": "AWFY",
    "beng": "BENG",
    "kostya": "KOSTYA",
    "larceny": "LARCENY",
    "jetstream": "JetStream",
}
ENGINE_LABELS = {
    "mir": "MIR",
    "c2mir": "C2MIR",
    "lambdajs": "LambdaJS",
    "quickjs": "QuickJS",
    "nodejs": "Node.js",
    "python": "Python",
}


def read_cmd(args):
    try:
        proc = subprocess.run(args, capture_output=True, text=True, check=False)
    except OSError:
        return "unavailable"
    return proc.stdout.strip() or proc.stderr.strip() or "unavailable"


def fmt_ms(value):
    if value is None:
        return "---"
    if value < 1:
        return f"{value:.3f}"
    if value < 10:
        return f"{value:.2f}"
    if value < 1000:
        return f"{value:.1f}"
    return f"{value / 1000:.2f}s"


def fmt_ratio(value):
    if value is None:
        return "---"
    if value < 0.01:
        return f"{value:.3f}x"
    if value < 10:
        return f"{value:.2f}x"
    if value < 100:
        return f"{value:.1f}x"
    return f"{value:.0f}x"


def geo_mean(values):
    vals = [v for v in values if v is not None and v > 0]
    if not vals:
        return None
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


def ratio(numerator, denominator):
    if numerator is None or denominator is None or denominator <= 0:
        return None
    return numerator / denominator


def write_report(args, data):
    engines = [e.strip() for e in args.engines.split(",") if e.strip()]
    date = args.date or datetime.datetime.now().strftime("%Y-%m-%d")
    commit = args.commit or read_cmd(["git", "rev-parse", "HEAD"])
    node_version = read_cmd(["node", "--version"])
    qjs_help = read_cmd(["qjs", "--help"])
    qjs_version = qjs_help.splitlines()[0].replace("QuickJS version ", "") if qjs_help != "unavailable" else "unavailable"

    lines = []

    def w(text=""):
        lines.append(text)

    title = args.title or "Lambda Benchmark Results"
    w(f"# {title}")
    w()
    w(f"- **Date:** {date}")
    w(f"- **Platform:** {platform.system()} {platform.machine()}")
    w(f"- **Lambda commit:** `{commit}`")
    w("- **Lambda build:** clean release build (`make release`)")
    w(f"- **Node.js:** {node_version}")
    w(f"- **QuickJS:** {qjs_version}")
    w("- **Methodology:** 3 runs per benchmark by default, median of self-reported `__TIMING__` milliseconds")
    w(f"- **Engines in this report:** {', '.join(ENGINE_LABELS.get(e, e) for e in engines)}")
    w(f"- **Results source:** `{args.input}`")
    w()
    w("JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.")
    w()
    w("---")
    w()
    w("## Summary")
    w()
    w("| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |")
    w("|---|---:|---:|---:|---:|---:|---:|---:|---:|")

    overall_ratios = {e: [] for e in engines if e != NODE_ENGINE}
    overall_counts = {e: 0 for e in engines}
    total_rows = 0

    for suite in SUITE_ORDER:
        if suite not in data:
            continue
        benches = data[suite]
        total_rows += len(benches)
        suite_counts = {e: 0 for e in engines}
        suite_ratios = {e: [] for e in engines if e != NODE_ENGINE}
        for bench_data in benches.values():
            node = bench_data.get(NODE_ENGINE)
            for engine in engines:
                value = bench_data.get(engine)
                if value is not None and value > 0:
                    suite_counts[engine] += 1
                    overall_counts[engine] += 1
                if engine != NODE_ENGINE:
                    r = ratio(value, node)
                    if r is not None:
                        suite_ratios[engine].append(r)
                        overall_ratios[engine].append(r)

        w(
            f"| {SUITE_LABELS.get(suite, suite)} | {len(benches)} | "
            f"{suite_counts.get('mir', 0)} | {suite_counts.get('lambdajs', 0)} | "
            f"{suite_counts.get('quickjs', 0)} | {suite_counts.get('nodejs', 0)} | "
            f"{fmt_ratio(geo_mean(suite_ratios.get('mir', [])))} | "
            f"{fmt_ratio(geo_mean(suite_ratios.get('lambdajs', [])))} | "
            f"{fmt_ratio(geo_mean(suite_ratios.get('quickjs', [])))} |"
        )

    w(
        f"| **Overall timed** | **{total_rows}** | "
        f"**{overall_counts.get('mir', 0)}** | **{overall_counts.get('lambdajs', 0)}** | "
        f"**{overall_counts.get('quickjs', 0)}** | **{overall_counts.get('nodejs', 0)}** | "
        f"**{fmt_ratio(geo_mean(overall_ratios.get('mir', [])))}** | "
        f"**{fmt_ratio(geo_mean(overall_ratios.get('lambdajs', [])))}** | "
        f"**{fmt_ratio(geo_mean(overall_ratios.get('quickjs', [])))}** |"
    )
    w()
    w("> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.")
    w()
    w("---")

    for suite in SUITE_ORDER:
        if suite not in data:
            continue
        w()
        w(f"## {SUITE_LABELS.get(suite, suite)}")
        w()
        header = "| Benchmark | Category |" + "".join(f" {ENGINE_LABELS.get(e, e)} (ms) |" for e in engines)
        ratio_header = "".join(f" {ENGINE_LABELS.get(e, e)}/Node |" for e in engines if e != NODE_ENGINE)
        w(header + ratio_header)
        w("|---|---|" + "---:|" * len(engines) + "---:|" * (len(engines) - 1))
        for bench_name, bench_data in data[suite].items():
            row = f"| {bench_name} | {bench_data.get('category', '')} |"
            for engine in engines:
                row += f" {fmt_ms(bench_data.get(engine))} |"
            node = bench_data.get(NODE_ENGINE)
            for engine in engines:
                if engine == NODE_ENGINE:
                    continue
                row += f" {fmt_ratio(ratio(bench_data.get(engine), node))} |"
            w(row)

    with open(args.output, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Generated {args.output} ({len(lines)} lines)")


def main():
    parser = argparse.ArgumentParser(description="Generate benchmark result markdown from a benchmark result JSON file")
    parser.add_argument("--input", default=DEFAULT_JSON, help="input JSON path")
    parser.add_argument("--output", required=True, help="output markdown path")
    parser.add_argument("--title", default=None, help="report title")
    parser.add_argument("--engines", default=DEFAULT_ENGINES, help="comma-separated engines to include")
    parser.add_argument("--date", default=None, help="report date override")
    parser.add_argument("--commit", default=None, help="git commit override")
    args = parser.parse_args()

    with open(args.input) as f:
        data = json.load(f)
    write_report(args, data)


if __name__ == "__main__":
    main()
