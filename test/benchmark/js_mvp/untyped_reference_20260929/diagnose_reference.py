#!/usr/bin/env python3
"""Work-matched Lambda probes and separate instrumented compiler diagnostics."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "test/benchmark"))
import run_paired_benchmarks as paired

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--reference", required=True)
parser.add_argument("--output", required=True)
args = parser.parse_args()
os.chdir(ROOT)
reference = json.loads(Path(args.reference).read_text())
binary = reference["metadata"]["binary"]
assert paired.sha256_file(binary) == reference["metadata"]["binary_sha256"]
scratch = ROOT / "temp/js-untyped-reference-20260929"
scratch.mkdir(parents=True, exist_ok=True)
rows = {row["id"]: row for row in reference["rows"]}
result = {"binary_sha256": paired.sha256_file(binary),
          "runner_sha256": paired.sha256_file(__file__),
          "reference_sha256": paired.sha256_file(args.reference),
          "work_matched": [], "instrumented": []}
os.environ.update(JS_EXECUTION_BACKEND="mir", JS_OPT_TRACE="0", JS_MIR_DUMP="0")
os.environ.pop("LAMBDA_MIR_DUMP_PATH", None)

def save():
    Path(args.output).write_text(json.dumps(result, indent=2) + "\n")

# Change only the audited work difference; leave canonical benchmark files intact.
for name, before, after in [
    ("awfy/havlak", "lta_main(1, 1, 10, 10, 5)", "lta_main(1, 50, 10, 10, 5)"),
    ("text/text_search", "while (index < len(pattern) - 1)", "while (index < len(pattern))"),
]:
    row = rows[name]
    source = Path(row["lambda_source"]["path"]).read_text()
    assert source.count(before) == 1
    probe = scratch / (name.replace("/", "-") + "-matched.ls")
    probe.write_text(source.replace(before, after))
    # Each added Lambda script has its corresponding exact expected result.
    probe.with_suffix(".txt").write_text(Path(row["lambda_golden"]).read_text())
    expected = hashlib.sha256(row["expected_stdout"]["lambda_untyped"].encode()).hexdigest()
    samples = []
    for _ in range(3):
        sample = paired.run_once(binary, str(probe), 180)
        sample["oracle_equal"] = sample.get("stdout_sha256") == expected
        samples.append(sample)
    valid = all(s["status"] == "ok" and s["oracle_equal"] for s in samples)
    result["work_matched"].append({"id": name, "before": before, "after": after,
        "original_source": row["lambda_source"], "probe": str(probe),
        "probe_sha256": paired.sha256_file(probe), "samples": samples,
        "valid": valid, "median_ms": statistics.median(s["exec_ms"] for s in samples) if valid else None})
    print("work-matched", name, valid, result["work_matched"][-1]["median_ms"], flush=True)
    save()

# Instrumentation has its own runs and never contributes performance medians.
for name in ["text/text_search", "larceny/triangl", "awfy/nbody", "awfy/bounce"]:
    row = rows[name]
    for language in ["js", "lambda"]:
        stem = name.replace("/", "-") + "-" + language
        mir = scratch / (stem + ".mir")
        trace = scratch / (stem + ".trace.tsv")
        os.environ.update(LAMBDA_MIR_DUMP_PATH=str(mir),
            JS_OPT_TRACE="1" if language == "js" else "0", JS_OPT_TRACE_OUT=str(trace))
        source = row["js_source" if language == "js" else "lambda_source"]["path"]
        expected = row["expected_stdout"]["js" if language == "js" else "lambda_untyped"]
        sample = paired.run_once(binary, source, 240, language=language)
        sample["oracle_equal"] = sample.get("stdout_sha256") == hashlib.sha256(expected.encode()).hexdigest()
        entry = {"id": name, "language": language, "sample": sample,
                 "mir": str(mir), "mir_exists": mir.exists()}
        if mir.exists():
            entry.update(mir_sha256=paired.sha256_file(mir), mir_bytes=mir.stat().st_size)
        if trace.exists():
            entry.update(trace=str(trace), trace_text=trace.read_text(), trace_sha256=paired.sha256_file(trace))
        result["instrumented"].append(entry)
        print("instrumented", name, language, sample["status"], sample["oracle_equal"], entry["mir_exists"], flush=True)
        save()

result["binary_unchanged"] = paired.sha256_file(binary) == result["binary_sha256"]
save()
