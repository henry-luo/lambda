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
    numerator = value_of(numerator)
    denominator = value_of(denominator)
    if numerator is None or denominator is None or denominator <= 0:
        return None
    return numerator / denominator


def value_of(cell):
    if isinstance(cell, dict):
        return cell.get("ms")
    return cell


def status_of(bench_data, engine):
    status = bench_data.get("_status", {}).get(engine)
    if status:
        return status
    return "ok" if value_of(bench_data.get(engine)) is not None else "not_recorded"


def collect_notables(data, engines):
    missing = []
    ratios = []
    wins = []
    for suite in SUITE_ORDER:
        for bench_name, bench_data in data.get(suite, {}).items():
            node = value_of(bench_data.get(NODE_ENGINE))
            for engine in engines:
                value = value_of(bench_data.get(engine))
                if value is None:
                    missing.append((suite, bench_name, engine, status_of(bench_data, engine)))
            ljs_ratio = ratio(bench_data.get("lambdajs"), node)
            if ljs_ratio is not None:
                entry = (ljs_ratio, suite, bench_name, value_of(bench_data.get("lambdajs")), node)
                ratios.append(entry)
                if ljs_ratio < 1.0:
                    wins.append(entry)
    ratios.sort(reverse=True)
    wins.sort()
    return missing, ratios, wins


def compute_dedup_summary(data, engines):
    dedup = {}
    for suite in SUITE_ORDER:
        for bench_name, bench_data in data.get(suite, {}).items():
            entry = dedup.setdefault(bench_name, {"suites": set()})
            entry["suites"].add(suite)
            for engine in engines:
                value = value_of(bench_data.get(engine))
                if value is None or value <= 0:
                    continue
                previous = entry.get(engine)
                if previous is None or value < previous:
                    entry[engine] = value

    ratios = {e: [] for e in engines if e != NODE_ENGINE}
    counts = {e: 0 for e in engines}
    duplicates = []
    for bench_name, entry in dedup.items():
        if len(entry["suites"]) > 1:
            duplicates.append((bench_name, sorted(entry["suites"])))
        node = entry.get(NODE_ENGINE)
        for engine in engines:
            if entry.get(engine) is not None:
                counts[engine] += 1
            if engine == NODE_ENGINE:
                continue
            r = ratio(entry.get(engine), node)
            if r is not None:
                ratios[engine].append(r)

    return {
        "total": len(dedup),
        "counts": counts,
        "ratios": ratios,
        "duplicates": duplicates,
    }


def write_report(args, data):
    engines = [e.strip() for e in args.engines.split(",") if e.strip()]
    metadata = data.get("_metadata", {})
    date = args.date or (metadata.get("started_at", "")[:10] if metadata.get("started_at") else None) or datetime.datetime.now().strftime("%Y-%m-%d")
    commit = args.commit or metadata.get("lambda_commit") or read_cmd(["git", "rev-parse", "HEAD"])
    node_version = metadata.get("node_version") or read_cmd(["node", "--version"])
    qjs_help = metadata.get("quickjs_version") or read_cmd(["qjs", "--help"])
    qjs_version = qjs_help.splitlines()[0].replace("QuickJS version ", "") if qjs_help != "unavailable" else "unavailable"
    profile_check = metadata.get("profile_check", "not_recorded")
    runs = metadata.get("runs", 3)
    timeout_s = metadata.get("timeout_s")

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
    w(f"- **Instrumentation check:** {profile_check}")
    w(f"- **Node.js:** {node_version}")
    w(f"- **QuickJS:** {qjs_version}")
    timeout_text = f", timeout {timeout_s}s per run" if timeout_s else ""
    w(f"- **Methodology:** {runs} run(s) per benchmark, median of self-reported `__TIMING__` milliseconds{timeout_text}")
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
                value = value_of(bench_data.get(engine))
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

    dedup = compute_dedup_summary(data, engines)
    dedup_counts = dedup["counts"]
    dedup_ratios = dedup["ratios"]
    w(
        f"| **Overall dedup** | **{dedup['total']}** | "
        f"**{dedup_counts.get('mir', 0)}** | **{dedup_counts.get('lambdajs', 0)}** | "
        f"**{dedup_counts.get('quickjs', 0)}** | **{dedup_counts.get('nodejs', 0)}** | "
        f"**{fmt_ratio(geo_mean(dedup_ratios.get('mir', [])))}** | "
        f"**{fmt_ratio(geo_mean(dedup_ratios.get('lambdajs', [])))}** | "
        f"**{fmt_ratio(geo_mean(dedup_ratios.get('quickjs', [])))}** |"
    )
    w(
        f"| Overall raw | {total_rows} | "
        f"{overall_counts.get('mir', 0)} | {overall_counts.get('lambdajs', 0)} | "
        f"{overall_counts.get('quickjs', 0)} | {overall_counts.get('nodejs', 0)} | "
        f"{fmt_ratio(geo_mean(overall_ratios.get('mir', [])))} | "
        f"{fmt_ratio(geo_mean(overall_ratios.get('lambdajs', [])))} | "
        f"{fmt_ratio(geo_mean(overall_ratios.get('quickjs', [])))} |"
    )
    w()
    w("> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.")
    w("> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.")
    w()
    missing, ljs_ratios, ljs_wins = collect_notables(data, engines)
    w("---")
    w()
    w("## Notable Results")
    w()
    if missing:
        w(f"- Missing timings: **{len(missing)}** cells")
        by_engine = {}
        for suite, bench_name, engine, status in missing:
            by_engine.setdefault(engine, []).append(f"{suite}/{bench_name} ({status})")
        for engine, entries in by_engine.items():
            shown = ", ".join(entries[:8])
            suffix = f", +{len(entries) - 8} more" if len(entries) > 8 else ""
            w(f"- {ENGINE_LABELS.get(engine, engine)} missing: {shown}{suffix}")
    else:
        w("- Missing timings: none")
    if dedup["duplicates"]:
        duplicate_text = ", ".join(f"{name} ({'/'.join(suites)})" for name, suites in dedup["duplicates"])
        w(f"- Deduplicated benchmark names: {duplicate_text}")
    if ljs_ratios:
        w()
        w("### Largest LambdaJS / Node.js Ratios")
        w()
        w("| Benchmark | LambdaJS | Node.js | Ratio |")
        w("|---|---:|---:|---:|")
        for r, suite, bench_name, ljs, node in ljs_ratios[:8]:
            w(f"| {suite}/{bench_name} | {fmt_ms(ljs)} | {fmt_ms(node)} | {fmt_ratio(r)} |")
    if ljs_wins:
        w()
        w("### LambdaJS Faster Than Node.js")
        w()
        w("| Benchmark | LambdaJS | Node.js | Ratio |")
        w("|---|---:|---:|---:|")
        for r, suite, bench_name, ljs, node in ljs_wins[:8]:
            w(f"| {suite}/{bench_name} | {fmt_ms(ljs)} | {fmt_ms(node)} | {fmt_ratio(r)} |")
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
                row += f" {fmt_ms(value_of(bench_data.get(engine)))} |"
            node = value_of(bench_data.get(NODE_ENGINE))
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
