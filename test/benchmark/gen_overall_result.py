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

SUITE_ORDER = ["r7rs", "awfy", "beng", "kostya", "larceny", "jetstream", "text"]
SUITE_LABELS = {
    "r7rs": "R7RS",
    "awfy": "AWFY",
    "beng": "BENG",
    "kostya": "KOSTYA",
    "larceny": "LARCENY",
    "jetstream": "JetStream",
    "text": "Text",
}
ENGINE_LABELS = {
    "mir": "MIR (untyped)",
    "mir_typed": "MIR (typed)",
    # Auto-tier columns. The MIR columns above pin LAMBDA_TIER=jit so the
    # series stays comparable back through Result18; these two report what a
    # user actually gets from `lambda.exe run` with no tier override, which
    # since the interpreter-first default is a different execution path.
    "mir_auto": "MIR (untyped, auto)",
    "mir_typed_auto": "MIR (typed, auto)",
    # Set 2 (end-to-end wall clock). Same workloads, different question: how long
    # does it take to RUN the script, with each engine paying its own startup and
    # compilation inside the measured region.
    "mir_auto_e2e": "MIR (untyped, auto)",
    "mir_typed_auto_e2e": "MIR (typed, auto)",
    "c2mir_e2e": "C2MIR",
    "go_e2e": "Go",
    "lambdajs_e2e": "LambdaJS",
    "quickjs_e2e": "QuickJS",
    "nodejs_e2e": "Node.js",
    "c2mir": "C2MIR",
    "go": "Go",
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


def report_engines(data, requested):
    """Expand MIR into untyped and typed columns when typed data is present."""
    has_typed_mir = any(
        "mir_typed" in bench_data
        for suite in SUITE_ORDER
        for bench_data in data.get(suite, {}).values()
    )
    has_auto_mir = any(
        "mir_auto" in bench_data
        for suite in SUITE_ORDER
        for bench_data in data.get(suite, {}).values()
    )
    has_typed_auto_mir = any(
        "mir_typed_auto" in bench_data
        for suite in SUITE_ORDER
        for bench_data in data.get(suite, {}).values()
    )
    expanded = []
    for engine in requested:
        expanded.append(engine)
        if engine == "mir" and has_typed_mir:
            expanded.append("mir_typed")
        # Auto columns ride alongside the pinned-JIT pair rather than replacing
        # it: the comparison that matters is jit-vs-auto on the SAME row.
        if engine == "mir" and has_auto_mir:
            expanded.append("mir_auto")
        if engine == "mir" and has_typed_auto_mir:
            expanded.append("mir_typed_auto")
    return expanded


def display_ms(bench_data, engine):
    """Format a timing and mark typed cells that reuse an untyped result."""
    value = fmt_ms(value_of(bench_data.get(engine)))
    if engine in ("mir_typed", "mir_typed_auto") and \
            status_of(bench_data, engine) == "untyped_fallback":
        return value + "*" if value != "---" else value
    return value


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


STATIC_CEILING_ENGINES = ["c2mir", "go"]


def collect_static_ceiling(data, engines):
    """Per-row distance from typed Lambda to the statically typed native ports.

    C2MIR and Go are not alternative Lambda backends: they are what the same
    workload costs with types all the way down, so MIR/native is the headroom a
    fully typed Lambda could still recover. C2MIR is the sharper of the two
    because it shares MIR's backend — a gap there is Lambda's front end, not a
    difference in code generator.
    """
    present = [e for e in STATIC_CEILING_ENGINES if e in engines]
    if not present:
        return None
    lambda_engine = "mir_typed" if "mir_typed" in engines else "mir"
    rows = []
    for suite in SUITE_ORDER:
        for bench_name, bench_data in data.get(suite, {}).items():
            lambda_ms = value_of(bench_data.get(lambda_engine))
            gaps = {e: ratio(bench_data.get(lambda_engine), bench_data.get(e)) for e in present}
            if all(g is None for g in gaps.values()):
                continue
            rows.append({
                "suite": suite,
                "name": bench_name,
                "lambda_ms": lambda_ms,
                "native": {e: value_of(bench_data.get(e)) for e in present},
                "gaps": gaps,
            })
    if not rows:
        return None
    return {
        "lambda_engine": lambda_engine,
        "engines": present,
        "rows": rows,
        "geo": {e: geo_mean([r["gaps"].get(e) for r in rows]) for e in present},
        "covered": {e: sum(1 for r in rows if r["gaps"].get(e) is not None) for e in present},
    }


def write_static_ceiling(w, ceiling, total_rows):
    if not ceiling:
        return
    lambda_label = ENGINE_LABELS.get(ceiling["lambda_engine"], ceiling["lambda_engine"])
    engines = ceiling["engines"]
    w("---")
    w()
    w("## Distance to the Static Ceiling")
    w()
    w(f"How far {lambda_label} is from the same workload written in a statically typed "
      "language. These columns are a reference bound, not another Lambda execution path: "
      "they say what is still on the table, and C2MIR is the sharper of the two because it "
      "shares MIR's code generator, so a gap there is attributable to Lambda's front end "
      "rather than to the backend.")
    w()
    for engine in engines:
        label = ENGINE_LABELS.get(engine, engine)
        w(f"- **{lambda_label} / {label} geomean:** "
          f"{fmt_ratio(ceiling['geo'].get(engine))} over "
          f"{ceiling['covered'].get(engine, 0)} of {total_rows} rows")
    w()
    ranked = sorted(
        (r for r in ceiling["rows"] if r["gaps"].get(engines[0]) is not None),
        key=lambda r: r["gaps"][engines[0]], reverse=True)
    if not ranked:
        return
    w(f"**Widest gaps vs {ENGINE_LABELS.get(engines[0], engines[0])}**")
    w()
    header = f"| Benchmark | {lambda_label} |"
    header += "".join(f" {ENGINE_LABELS.get(e, e)} |" for e in engines)
    header += "".join(f" {lambda_label}/{ENGINE_LABELS.get(e, e)} |" for e in engines)
    w(header)
    w("|---|" + "---:|" * (1 + 2 * len(engines)))
    for row in ranked[:12]:
        line = f"| {row['suite']}/{row['name']} | {fmt_ms(row['lambda_ms'])} |"
        line += "".join(f" {fmt_ms(row['native'].get(e))} |" for e in engines)
        line += "".join(f" {fmt_ratio(row['gaps'].get(e))} |" for e in engines)
        w(line)
    w()


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


def write_historical_comparisons(w, metadata):
    comparisons = metadata.get("historical_comparisons")
    if not comparisons:
        return

    w("---")
    w()
    w("## Historical Comparison")
    w()
    for comparison in comparisons:
        title = comparison.get("title")
        if title:
            w(f"### {title}")
            w()
        for note in comparison.get("notes", []):
            w(note)
            w()
        for table in comparison.get("tables", []):
            caption = table.get("caption")
            if caption:
                w(f"**{caption}**")
                w()
            columns = table.get("columns", [])
            rows = table.get("rows", [])
            if not columns or not rows:
                continue
            w("| " + " | ".join(columns) + " |")
            w("|" + "|".join("---" for _ in columns) + "|")
            for row in rows:
                w("| " + " | ".join(row) + " |")
            w()


# Set 2's engine list, in the order the report shows it. Built from what the
# JSON actually carries so an older snapshot without e2e columns simply reports
# part 1 alone.
E2E_ENGINES = ["mir_auto_e2e", "mir_typed_auto_e2e", "c2mir_e2e",
               "lambdajs_e2e", "quickjs_e2e", "nodejs_e2e"]


def available_e2e_engines(data):
    present = []
    for engine in E2E_ENGINES:
        if any(engine in bench_data
               for suite in SUITE_ORDER
               for bench_data in data.get(suite, {}).values()):
            present.append(engine)
    # A part-2 table without its Node baseline has nothing to normalize against.
    return present if "nodejs_e2e" in present else []


def emit_summary_table(w, data, engines, node_engine, canonicalized):
    """Suite/overall counts and geomeans for one measurement set."""
    ratio_engines = [e for e in engines if e != node_engine]
    columns = ["Suite", "Total"]
    columns.extend(f"Timed {ENGINE_LABELS.get(e, e)}" for e in engines)
    columns.extend(f"{ENGINE_LABELS.get(e, e)}/Node geo" for e in ratio_engines)
    w("| " + " | ".join(columns) + " |")
    w("|---|---:" + "|---:" * (len(columns) - 2) + "|")

    overall_ratios = {e: [] for e in ratio_engines}
    overall_counts = {e: 0 for e in engines}
    total_rows = 0
    for suite in SUITE_ORDER:
        if suite not in data:
            continue
        benches = data[suite]
        total_rows += len(benches)
        suite_counts = {e: 0 for e in engines}
        suite_ratios = {e: [] for e in ratio_engines}
        for bench_data in benches.values():
            node = value_of(bench_data.get(node_engine))
            for engine in engines:
                value = value_of(bench_data.get(engine))
                if value is not None and value > 0:
                    suite_counts[engine] += 1
                    overall_counts[engine] += 1
                if engine != node_engine:
                    r = ratio(value, node)
                    if r is not None:
                        suite_ratios[engine].append(r)
                        overall_ratios[engine].append(r)
        cells = [SUITE_LABELS.get(suite, suite), str(len(benches))]
        cells.extend(str(suite_counts.get(e, 0)) for e in engines)
        cells.extend(fmt_ratio(geo_mean(suite_ratios.get(e, []))) for e in ratio_engines)
        w("| " + " | ".join(cells) + " |")

    overall_cells = ["**Overall**", str(total_rows)]
    overall_cells.extend(str(overall_counts.get(e, 0)) for e in engines)
    overall_cells.extend(fmt_ratio(geo_mean(overall_ratios.get(e, []))) for e in ratio_engines)
    w("| " + " | ".join(overall_cells) + " |")
    return total_rows


def emit_suite_tables(w, data, engines, node_engine):
    ratio_engines = [e for e in engines if e != node_engine]
    for suite in SUITE_ORDER:
        if suite not in data:
            continue
        w()
        w(f"### {SUITE_LABELS.get(suite, suite)}")
        w()
        header = "| Benchmark | Category |" + "".join(f" {ENGINE_LABELS.get(e, e)} (ms) |" for e in engines)
        header += "".join(f" {ENGINE_LABELS.get(e, e)}/Node |" for e in ratio_engines)
        w(header)
        w("|---|---|" + "---:|" * (len(engines) + len(ratio_engines)))
        for bench_name, bench_data in data[suite].items():
            row = f"| {bench_name} | {bench_data.get('category', '')} |"
            for engine in engines:
                row += f" {display_ms(bench_data, engine)} |"
            node = value_of(bench_data.get(node_engine))
            for engine in ratio_engines:
                row += f" {fmt_ratio(ratio(bench_data.get(engine), node))} |"
            w(row)


def write_report(args, data):
    requested_engines = [e.strip() for e in args.engines.split(",") if e.strip()]
    engines = report_engines(data, requested_engines)
    metadata = data.get("_metadata", {})
    date = args.date or (metadata.get("started_at", "")[:10] if metadata.get("started_at") else None) or datetime.datetime.now().strftime("%Y-%m-%d")
    commit = args.commit or metadata.get("lambda_commit") or read_cmd(["git", "rev-parse", "HEAD"])
    node_version = metadata.get("node_version") or read_cmd(["node", "--version"])
    qjs_help = metadata.get("quickjs_version") or read_cmd(["qjs", "--help"])
    qjs_version = qjs_help.splitlines()[0].replace("QuickJS version ", "") if qjs_help != "unavailable" else "unavailable"
    profile_check = metadata.get("profile_check", "not_recorded")
    test262_baseline = metadata.get("test262_baseline") or {}
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
    lambda_exe = metadata.get("lambda_archive") or metadata.get("lambda_exe")
    exe_size = metadata.get("lambda_archive_size_bytes") or metadata.get("lambda_exe_size_bytes")
    try:
        exe_size = int(exe_size) if exe_size is not None else None
    except (TypeError, ValueError):
        exe_size = None
    if lambda_exe and "/exe/" in lambda_exe:
        # an archived binary from test/benchmark/exe/ — name it, so the reader knows
        # these numbers are re-measurable rather than tied to a since-changed build
        w(f"- **Lambda build:** archived release binary `{lambda_exe}`"
          + (f" ({exe_size:,} bytes)" if exe_size else ""))
    else:
        w("- **Lambda build:** clean release build (`make release`)")
    w(f"- **Instrumentation check:** {profile_check}")
    if test262_baseline.get("status") == "passed":
        duration_s = test262_baseline.get("duration_s")
        duration_text = f" in {duration_s:.2f}s" if isinstance(duration_s, (int, float)) else ""
        fully_passed = test262_baseline.get("fully_passed")
        baseline_tests = test262_baseline.get("baseline_tests")
        pass_text = (f"{fully_passed:,} / {baseline_tests:,} passed"
                     if isinstance(fully_passed, int) and isinstance(baseline_tests, int) else "passed")
        test262_context = ("required pre-benchmark gate" if test262_baseline.get("gate")
                           else "post-snapshot archived-binary verification")
        w(f"- **Test262 baseline:** {pass_text}{duration_text} (harness time; {test262_context})")
        timing_s = test262_baseline.get("timing_s")
        if isinstance(timing_s, dict):
            def phase(name):
                value = timing_s.get(name)
                return f"{value:.1f}s" if isinstance(value, (int, float)) else "n/a"
            w("- **Test262 phases:** "
              f"prep {phase('prep')}; batch {phase('batch')} "
              f"(batched {phase('batched')}: sync {phase('sync')}, async {phase('async')}; "
              f"non-batched {phase('non_batched')}); retry {phase('retry')}; "
              f"partial {phase('partial')}; timing {phase('timing')}; "
              f"memory {phase('memory')}; eval {phase('eval')}")
    elif test262_baseline.get("status") and test262_baseline.get("status") != "not_run":
        w(f"- **Test262 baseline:** {test262_baseline['status']}")
    w(f"- **Node.js:** {node_version}")
    w(f"- **QuickJS:** {qjs_version}")
    timeout_text = f", timeout {timeout_s}s per run" if timeout_s else ""
    cooldown_s = metadata.get("suite_cooldown_seconds")
    # reported from metadata, not hand-written: an earlier revision stated a cooldown
    # in prose that the runner did not actually perform
    if cooldown_s:
        order = metadata.get("suite_order") or SUITE_ORDER
        # keep the join outside the f-string because Python 3.9 parses its nested quotes as syntax
        order_text = " -> ".join(order)
        timeout_text += (f"; suites run in order `{order_text}`"
                         f" with a {cooldown_s}s idle gap between suites")
    w(f"- **Methodology:** {runs} run(s) per benchmark, median of self-reported `__TIMING__` milliseconds{timeout_text}")
    w(f"- **Engines in this report:** {', '.join(ENGINE_LABELS.get(e, e) for e in engines)}")
    w(f"- **Results source:** `{args.input}`")
    # Columns folded in by merge_engine_results.py were measured in a separate
    # session; saying so is the difference between a comparison and a claim.
    for record in metadata.get("merged_engines", []):
        merged_labels = ", ".join(ENGINE_LABELS.get(e, e) for e in record.get("engines", []))
        started = (record.get("source_started_at") or "")[:10]
        when = f" on {started}" if started else ""
        runs_text = f", {record['source_runs']} run(s)" if record.get("source_runs") else ""
        note = f" {record['note']}" if record.get("note") else ""
        w(f"- **Separately measured:** {merged_labels} measured{when}{runs_text} "
          f"from `{record.get('source')}`.{note}")
    if "mir_typed" in engines:
        w("- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists")
    w()
    w("JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` "
      "workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, "
      "richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda "
      "`.ls` port implements exactly one `runIteration()`, so every engine times the same work. "
      "A previous revision hard-coded 8 repeats for every file, which made the JS engines run "
      "8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.")
    if any(e in engines for e in STATIC_CEILING_ENGINES):
        w()
        w("C2MIR and Go are native statically typed ports of the same workloads, present as a "
          "reference bound rather than as Lambda execution paths. The C2MIR column is **not** the "
          "retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend "
          "(`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both "
          "native columns report workload-only `__TIMING__` milliseconds like every other engine — "
          "the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under "
          "`-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go "
          "ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts "
          "the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design "
          "(see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names "
          "whose canonical row lives in another suite.")
    w()
    w("---")
    w()
    e2e_engines = available_e2e_engines(data)
    canonicalized = bool(metadata.get("canonical_duplicate_suites"))

    if e2e_engines:
        w("## Part 1 — Execution time (self-reported)")
        w()
        w("Each engine's own `__TIMING__` figure: the timed workload only, with "
          "startup and compilation outside the measured region. This is the "
          "historical series, comparable back through Result18, and the MIR "
          "columns pin `LAMBDA_TIER=jit`.")
        w()

    w("## Summary" if not e2e_engines else "### Summary")
    w()
    total_rows = emit_summary_table(w, data, engines, NODE_ENGINE, canonicalized)
    w()
    if canonicalized:
        dedup = {"duplicates": []}
        w("> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.")
    else:
        dedup = compute_dedup_summary(data, engines)
    w("> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.")
    w()
    write_historical_comparisons(w, metadata)
    write_static_ceiling(w, collect_static_ceiling(data, engines), total_rows)
    missing, ljs_ratios, ljs_wins = collect_notables(data, engines)
    w("---")
    w()
    w("## Notable Results" if not e2e_engines else "### Notable Results")
    w()
    w(f"- Missing timings: **{len(missing)}** cells")
    if missing:
        by_engine = {}
        for suite, bench, engine, status in missing:
            by_engine.setdefault(engine, []).append(f"{suite}/{bench} ({status})")
        for engine, entries in by_engine.items():
            w(f"- {ENGINE_LABELS.get(engine, engine)} missing: " + ", ".join(entries))
    if ljs_ratios:
        w()
        w("### Largest LambdaJS / Node.js Ratios" if not e2e_engines
          else "#### Largest LambdaJS / Node.js Ratios")
        w()
        w("| Benchmark | LambdaJS | Node.js | Ratio |")
        w("|---|---:|---:|---:|")
        for r, suite, bench, ljs, node in ljs_ratios[:8]:
            w(f"| {suite}/{bench} | {fmt_ms(ljs)} | {fmt_ms(node)} | {fmt_ratio(r)} |")
    if ljs_wins:
        w()
        w("### LambdaJS Faster Than Node.js" if not e2e_engines
          else "#### LambdaJS Faster Than Node.js")
        w()
        w("| Benchmark | LambdaJS | Node.js | Ratio |")
        w("|---|---:|---:|---:|")
        for r, suite, bench, ljs, node in ljs_wins[:8]:
            w(f"| {suite}/{bench} | {fmt_ms(ljs)} | {fmt_ms(node)} | {fmt_ratio(r)} |")

    emit_suite_tables(w, data, engines, NODE_ENGINE)

    if e2e_engines:
        w()
        w("---")
        w()
        w("## Part 2 — End-to-end time (wall clock, auto tier)")
        w()
        w("Wall clock from process invocation to exit, so **every engine pays its "
          "own startup and compilation inside the number**. The MIR columns use "
          "the shipped auto tier -- no `LAMBDA_TIER` override -- which is what "
          "`lambda.exe run script.ls` actually does.")
        w()
        w("This set exists because the two questions are different. Part 1 asks "
          "how fast the compiled workload runs; part 2 asks how long it takes to "
          "run the script. Timing the auto tier under part 1's rules would charge "
          "Lambda for JIT compilation performed *inside* the measured region while "
          "crediting Node.js with a post-warmup figure -- comparing two different "
          "things. Here the accounting is the same for everyone.")
        w()
        w("Same processes, where possible: the reference engines report their wall "
          "and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two "
          "readings of one launch. Only the MIR columns are re-run, because part 1 "
          "pins the JIT and part 2 must use the auto tier.")
        w()
        w("⚠ Short workloads are dominated by fixed process startup here, so a row "
          "whose part-1 time is a fraction of a millisecond says more about "
          "executable launch cost than about the language. Read part 2 by the "
          "longer rows.")
        w()
        w("### Summary")
        w()
        emit_summary_table(w, data, e2e_engines, "nodejs_e2e", canonicalized)
        w()
        w("> Ratio < 1.0 means the engine finished the whole run faster than Node.js.")
        emit_suite_tables(w, data, e2e_engines, "nodejs_e2e")
        w()

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
