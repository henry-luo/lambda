#!/usr/bin/env python3
"""Generate the frozen JS MVP benchmark contract from the standard runner."""

import hashlib
import json
import os
import runpy


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
MANIFEST_PATH = os.path.join(SCRIPT_DIR, "js_mvp_manifest_v1.json")
FASTA_INPUT = "test/benchmark/beng/input/fasta_1000.txt"
PRETTIER_INPUT = "test/benchmark/text/prettier_ast.json"


def digest(path):
    with open(path, "rb") as stream:
        return hashlib.sha256(stream.read()).hexdigest()


def selected_source(row):
    source = row["js_path"] or row["ref_js"]
    if row["suite"] == "awfy":
        bundle = source.replace("2.js", "2_bundle.js")
        if os.path.exists(bundle):
            return bundle
    return source


def inputs_for(row):
    identifier = row["suite"] + "/" + row["name"]
    if identifier in {"beng/knucleotide", "beng/regexredux", "beng/revcomp"}:
        return [FASTA_INPUT]
    if identifier == "text/prettier_ast":
        return [PRETTIER_INPUT]
    return []


def wrapper_for(runner, row, source):
    if not row["is_jetstream"]:
        return {
            "kind": "direct_script",
            "strict_mode": "source_preserved",
            "timed_region": "source-defined __TIMING__ payload",
        }
    detected = runner["_detect_jetstream_run_function"](source)
    if detected is None:
        raise RuntimeError("JetStream wrapper detection failed for " + source)
    run_expression, repeats = detected
    canonical = "jetstream-runIteration-v1\n" + source + "\n" + run_expression + "\n" + str(repeats)
    return {
        "kind": "jetstream_run_iteration",
        "strict_mode": "source_preserved",
        "run_expression": run_expression,
        "repeat_count": repeats,
        "timed_region": "source-defined runIteration payload",
        "canonical_sha256": hashlib.sha256(canonical.encode()).hexdigest(),
    }


def main():
    os.chdir(PROJECT_ROOT)
    runner = runpy.run_path(os.path.join(SCRIPT_DIR, "run_benchmarks.py"),
                            run_name="js_mvp_manifest_generate")
    rows = runner["build_benchmark_list"](None, None, include_text=True)
    workloads = []
    for row in rows:
        source = selected_source(row)
        inputs = inputs_for(row)
        workloads.append({
            "id": row["suite"] + "/" + row["name"],
            "source": source,
            "sha256": digest(source),
            "inputs": [{"path": path, "sha256": digest(path)} for path in inputs],
            "arguments": [],
            "working_directory": ".",
            "wrapper": wrapper_for(runner, row, source),
            "correctness_oracle": "source PASS/FAIL marker and zero exit status",
        })
    if len(workloads) != 63:
        raise RuntimeError("expected 63 standard workloads")
    with open(MANIFEST_PATH, "w", encoding="utf-8") as stream:
        json.dump({"schema": 2, "workloads": workloads}, stream, indent=2)
        stream.write("\n")
    print("Wrote JS MVP benchmark manifest: 63 workloads")


if __name__ == "__main__":
    main()
