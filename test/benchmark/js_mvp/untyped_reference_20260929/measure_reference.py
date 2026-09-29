#!/usr/bin/env python3
"""Bounded, rotating JS/MVP/untyped-Lambda reference survey for the tuning audit."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "test/benchmark"))
import run_benchmarks as bench
import run_paired_benchmarks as paired
from js_benchmark_manifest import TUNE14_V1_PROFILE, build_workloads

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary", required=True)
parser.add_argument("--output", required=True)
parser.add_argument("--runs", type=int, default=3)
args = parser.parse_args()
os.chdir(ROOT)
binary = str(Path(args.binary).resolve())
output = Path(args.output)
output.parent.mkdir(parents=True, exist_ok=True)
bench.check_release_build(exe_path=binary)
bench.require_pinned_node_version(["nodejs"], "time")
os.environ.update(JS_EXECUTION_BACKEND="mir", JS_OPT_TRACE="0", JS_MIR_DUMP="0")
selected = {"fft", "mbrot", "bounce", "mandelbrot", "nbody", "richards",
            "havlak", "cd", "base64", "triangl", "fast_diff", "microdiff",
            "text_search", "three_way_merge", "log_pipeline"}
specs = [s for s in bench.build_benchmark_list(None, None) if s["name"] in selected]
workloads = {w["id"]: w for w in build_workloads(vars(bench), specs, TUNE14_V1_PROFILE)}
artifact = {"metadata": {
    "started_at": datetime.datetime.now().astimezone().isoformat(),
    "binary": binary, "binary_sha256": paired.sha256_file(binary),
    "runner_sha256": paired.sha256_file(__file__),
    "paired_runner_sha256": paired.sha256_file(paired.__file__),
    "git_commit": paired.command_output(["git", "rev-parse", "HEAD"]),
    "power": paired.command_output(["pmset", "-g", "batt"]),
    "node": paired.command_output(["node", "--version"]),
    "runs": args.runs, "js_backend": "mir", "lambda_tier": "jit",
    "scope": "Reference ports, not identical language programs or a causal compiler-speedup measurement.",
    "order": "full_js,mvp,lambda_untyped rotated once per round",
}, "rows": []}

def save():
    output.write_text(json.dumps(artifact, indent=2) + "\n")

for spec in specs:
    name = spec["suite"] + "/" + spec["name"]
    ls, _ = bench.mir_script_variants(spec)
    js = workloads[name]["source"]
    golden = Path(ls).with_suffix(".txt")
    expected_ls = paired.normalized_stdout(golden.read_text())
    reference = subprocess.run(["node", js], capture_output=True, text=True,
                               timeout=180, check=True)
    expected_js = paired.normalized_stdout(reference.stdout)
    assert "FAIL" not in expected_js and bench.parse_timing(reference.stdout) is not None
    expected_hash = {
        "lambda_untyped": hashlib.sha256(expected_ls.encode()).hexdigest(),
        "full_js": hashlib.sha256(expected_js.encode()).hexdigest(),
        "mvp": hashlib.sha256(expected_js.encode()).hexdigest(),
    }
    row = {"id": name, "lambda_source": paired.source_provenance(ls),
           "js_source": paired.source_provenance(js), "lambda_golden": str(golden),
           "lambda_golden_sha256": paired.sha256_file(golden),
           "expected_stdout": {"lambda_untyped": expected_ls, "js": expected_js},
           "samples": []}
    print(name, flush=True)
    engines = ["full_js", "mvp", "lambda_untyped"]
    for round_index in range(args.runs):
        order = engines[round_index % 3:] + engines[:round_index % 3]
        for engine in order:
            sample = paired.run_once(binary, ls if engine == "lambda_untyped" else js,
                180, "lambda" if engine == "lambda_untyped" else "js", "jit",
                js_runtime="mvp" if engine == "mvp" else "legacy")
            sample.update(engine=engine, round=round_index + 1,
                oracle_equal=sample.get("stdout_sha256") == expected_hash[engine])
            row["samples"].append(sample)
            print(" ", round_index + 1, engine, sample["status"],
                  sample.get("exec_ms"), "oracle", sample["oracle_equal"], flush=True)
    row["valid"] = all(s["status"] == "ok" and s["oracle_equal"] and
                       s.get("exec_ms", 0) > 0 for s in row["samples"])
    row["medians_ms"] = {engine: statistics.median(
        s["exec_ms"] for s in row["samples"] if s["engine"] == engine)
        for engine in engines} if row["valid"] else None
    artifact["rows"].append(row)
    save()

artifact["metadata"]["finished_at"] = datetime.datetime.now().astimezone().isoformat()
artifact["metadata"]["binary_unchanged"] = paired.sha256_file(binary) == artifact["metadata"]["binary_sha256"]
artifact["metadata"]["sources_unchanged"] = all(
    paired.sha256_file(row[key]["path"]) == row[key]["sha256"]
    for row in artifact["rows"] for key in ("lambda_source", "js_source"))
save()
