#!/usr/bin/env python3
"""Recompute the reference table and static MIR/counter evidence."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
reference = json.loads((HERE / "reference.json").read_text())
diagnostics = json.loads((HERE / "diagnostics.json").read_text())
previous_path = HERE.parent / "tune_more_20260929/overall-before-after-summary.json"
previous = json.loads(previous_path.read_text())
paired_path = previous_path.with_name("overall-before-after-paired.json")
paired_rows = [r for r in json.loads(paired_path.read_text())["rows"] if r["name"] != "navier_stokes"]
largest = sorted(paired_rows, key=lambda r: r["candidate"]["median_exec_ms"], reverse=True)[:5]
paired = previous["direct_old_vs_tuned"]
search = previous["highlights"]["text/text_search"]
matched = {r["id"]: r for r in diagnostics["work_matched"]}
summary = {
    "inputs_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
        for p in [HERE / "reference.json", HERE / "diagnostics.json", previous_path, paired_path]},
    "round_analysis": {
        "top_five_workloads": [r["suite"] + "/" + r["name"] for r in largest],
        "top_five_share_of_tuned_ms": sum(r["candidate"]["median_exec_ms"] for r in largest) / paired["tuned_sum_ms"],
        "search_share_of_saved_ms": (search["old_ms"] - search["tuned_ms"]) /
            (paired["old_sum_ms"] - paired["tuned_sum_ms"]),
        "tuned_over_old_sum_excluding_search":
            (paired["tuned_sum_ms"] - search["tuned_ms"]) /
            (paired["old_sum_ms"] - search["old_ms"]),
    }, "rows": [], "diagnostics": []}
lines = ["# JS, MVP, and untyped Lambda reference survey", "",
    "See the [analysis and next proposal](../../../../vibe/impl/JS_MVP_Untyped_Lambda_Tuning_20260929.md).",
    "", "These are port timings, not an attribution of compiler speedup. Three rotating rounds use the same archived O3/LTO/NDEBUG release-profile host, explicit JS MIR and Lambda JIT. JS and MVP are checked against Node 22.13.0 output; Lambda against each source's golden file. Instrumented runs are separate.",
    "", "| Workload | Full JS ms | MVP ms | Lambda untyped ms | JS/MVP | JS/Lambda port |",
    "|---|---:|---:|---:|---:|---:|"]
for row in reference["rows"]:
    assert row["valid"]
    m = row["medians_ms"]
    record = {"id": row["id"], **m, "js_over_mvp": m["full_js"] / m["mvp"],
              "js_over_lambda_port": m["full_js"] / m["lambda_untyped"]}
    if row["id"] in matched:
        probe = matched[row["id"]]
        assert probe["valid"]
        record.update(lambda_matched_ms=probe["median_ms"],
                      js_over_lambda_matched=m["full_js"] / probe["median_ms"])
    summary["rows"].append(record)
    lines.append(f'| {row["id"]} | {m["full_js"]:.3f} | {m["mvp"]:.3f} | {m["lambda_untyped"]:.3f} | {record["js_over_mvp"]:.2f} | {record["js_over_lambda_port"]:.2f} |')
lines += ["", "## Audited work differences", "",
    "Havlak's canonical Lambda port repeats loop finding once, versus 50 times in bundled JS. A diagnostic copy changes only that argument. Text search's Lambda bad-character table omits the final pattern character; its diagnostic copy includes it as JS does. Neither probe changes canonical benchmark sources. These probes correct named differences, not every possible port difference.", ""]
for row in summary["rows"]:
    if "lambda_matched_ms" in row:
        lines.append(f'- {row["id"]}: corrected Lambda **{row["lambda_matched_ms"]:.3f} ms**, JS/Lambda **{row["js_over_lambda_matched"]:.2f}×**.')
lines += ["", "NBody and Bounce use flat numeric arrays in Lambda and objects/methods in JS. CD uses different tree/node layouts. Triangl uses Boolean/numeric Lambda arrays versus Uint8Array/Int32Array in JS. The registry's untyped text-search file still declares three outer scalar locals as `int`; its search functions are unannotated. No aggregate JS/Lambda compiler-speedup ratio is computed.",
    "", "## Reproduction", "", "Run from the repository root, serially:", "", "```sh",
    "python3 test/benchmark/js_mvp/untyped_reference_20260929/measure_reference.py --binary temp/js-tune-20260929/overall-after-profile.exe --output test/benchmark/js_mvp/untyped_reference_20260929/reference.json",
    "python3 test/benchmark/js_mvp/untyped_reference_20260929/diagnose_reference.py --reference test/benchmark/js_mvp/untyped_reference_20260929/reference.json --output test/benchmark/js_mvp/untyped_reference_20260929/diagnostics.json",
    "python3 test/benchmark/js_mvp/untyped_reference_20260929/summarize_reference.py", "```", "",
    "`reference.json` contains all 135 timing samples and provenance. `diagnostics.json` contains six work-corrected timings, eight instrumented runs, exact source edits, MIR hashes and raw trace text. MIR/probe files are under `temp/js-untyped-reference-20260929/`; the diagnostic harness regenerates them. `summary.json` is derived by the script above. Static MIR counts include wrappers and cold fallback bodies; trace counters count events rather than CPU time.", "",
    "An initial survey used raw AWFY module files instead of the canonical bundles. It was stopped, excluded, and retained only under `temp/js-tune-20260929/untyped-reference-raw-awfy-rejected.*`. The recorded survey uses `js_benchmark_manifest` sources."]
for row in diagnostics["instrumented"]:
    assert row["sample"]["status"] == "ok" and row["sample"]["oracle_equal"] and row["mir_exists"]
    source = Path(row["mir"]).read_text()
    opcodes = Counter(re.findall(r"^\t([a-z][a-z0-9_]*)\t", source, re.M))
    calls = Counter(re.findall(r"^\t(?:inline|j)?call\t[^,]+,\s*([^,\s]+)", source, re.M))
    record = {"id": row["id"], "language": row["language"],
        "mir_lines": len(source.splitlines()), "opcodes": dict(opcodes), "calls": dict(calls)}
    if "trace_text" in row:
        events = row["trace_text"].split(" events=", 1)[1].split(" reasons=", 1)[0]
        record["events"] = {k: dict(zip(["attempts", "taken", "fallback", "invalidated"], map(int, v.split("/"))))
            for k, v in (event.split("=", 1) for event in events.split(","))}
    summary["diagnostics"].append(record)
assert reference["metadata"]["binary_unchanged"] and reference["metadata"]["sources_unchanged"]
assert diagnostics["binary_unchanged"]
(HERE / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
(HERE / "README.md").write_text("\n".join(lines) + "\n")
