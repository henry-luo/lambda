#!/usr/bin/env python3
"""Verify the frozen source identities for the JS MVP benchmark contract."""

import hashlib
import json
import os
import runpy
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
MANIFEST_PATH = os.path.join(SCRIPT_DIR, "js_mvp_manifest_v1.json")
FASTA_INPUT = "test/benchmark/beng/input/fasta_1000.txt"
PRETTIER_INPUT = "test/benchmark/text/prettier_ast.json"


def source_hash(path):
    with open(path, "rb") as stream:
        return hashlib.sha256(stream.read()).hexdigest()


def selected_source(row):
    source = row["js_path"] or row["ref_js"]
    if row["suite"] == "awfy":
        bundle = source.replace("2.js", "2_bundle.js")
        if os.path.exists(bundle):
            return bundle
    return source


def expected_inputs(row):
    identifier = row["suite"] + "/" + row["name"]
    if identifier in {"beng/knucleotide", "beng/regexredux", "beng/revcomp"}:
        return [FASTA_INPUT]
    if identifier == "text/prettier_ast":
        return [PRETTIER_INPUT]
    return []


def main():
    os.chdir(PROJECT_ROOT)
    with open(MANIFEST_PATH, encoding="utf-8") as stream:
        manifest = json.load(stream)
    runner = runpy.run_path(os.path.join(SCRIPT_DIR, "run_benchmarks.py"),
                            run_name="js_mvp_manifest_verify")
    rows = runner["build_benchmark_list"](None, None, include_text=True)
    workloads = manifest.get("workloads", [])
    if manifest.get("schema") != 2 or len(rows) != 63 or len(workloads) != 63:
        print("JS MVP manifest must contain the 63 standard workloads", file=sys.stderr)
        return 1
    for entry, row in zip(workloads, rows):
        source = selected_source(row)
        identifier = row["suite"] + "/" + row["name"]
        if (entry.get("id") != identifier or entry.get("source") != source or
                entry.get("sha256") != source_hash(source)):
            print("JS MVP manifest mismatch: " + source, file=sys.stderr)
            return 1
        expected = [{"path": path, "sha256": source_hash(path)}
                    for path in expected_inputs(row)]
        if entry.get("inputs") != expected:
            print("JS MVP manifest input mismatch: " + identifier, file=sys.stderr)
            return 1
        wrapper = entry.get("wrapper")
        if not isinstance(wrapper, dict) or not wrapper.get("kind") or \
                entry.get("correctness_oracle") != "source PASS/FAIL marker and zero exit status":
            print("JS MVP manifest wrapper mismatch: " + identifier, file=sys.stderr)
            return 1
    print("JS MVP benchmark manifest verified: 63 workloads")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
