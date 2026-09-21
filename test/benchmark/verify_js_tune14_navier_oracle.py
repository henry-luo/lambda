#!/usr/bin/env python3
"""Run Tune14's Navier semantic control outside its canonical timed region."""

import argparse
import hashlib
import json
import os
import runpy
import subprocess
import sys

from js_benchmark_manifest import NAVIER_STOKES_ID, TUNE14_V1_PROFILE, build_workloads


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
MANIFEST_PATH = os.path.join(SCRIPT_DIR, "js_tune14_manifest_v1.json")


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_checked(command, expected_marker):
    result = subprocess.run(command, cwd=PROJECT_ROOT, capture_output=True, text=True, check=False)
    output = result.stdout + result.stderr
    status = "ok"
    if result.returncode != 0:
        status = "exit_" + str(result.returncode)
    elif output.count("__TIMING__:") != 1:
        status = "timing_marker_count"
    elif expected_marker not in output:
        status = "missing_density_marker"
    return {
        "command": command,
        "status": status,
        "returncode": result.returncode,
        "timing_marker_count": output.count("__TIMING__:"),
        "density_marker_present": expected_marker in output,
        "stdout_sha256": hashlib.sha256(result.stdout.encode()).hexdigest(),
        "stderr_sha256": hashlib.sha256(result.stderr.encode()).hexdigest(),
    }


def navier_entry(manifest):
    for entry in manifest["workloads"]:
        if entry["id"] == NAVIER_STOKES_ID:
            return entry
    raise RuntimeError("Tune14 manifest is missing jetstream/navier_stokes")


def main():
    parser = argparse.ArgumentParser(description="Verify the Tune14 Navier semantic control")
    parser.add_argument("--lambda", dest="lambda_exe", default="./lambda.exe")
    parser.add_argument("--node", default="node")
    parser.add_argument("--manifest", default=MANIFEST_PATH)
    parser.add_argument("--output", default=None,
                        help="optional JSON artifact recording both engine results")
    args = parser.parse_args()

    os.chdir(PROJECT_ROOT)
    manifest_path = os.path.abspath(args.manifest)
    with open(manifest_path, encoding="utf-8") as stream:
        manifest = json.load(stream)
    if manifest.get("profile") != TUNE14_V1_PROFILE:
        raise SystemExit("Navier oracle requires the Tune14 v1 manifest")
    runner = runpy.run_path(os.path.join(SCRIPT_DIR, "run_benchmarks.py"),
                            run_name="js_tune14_navier_oracle")
    rows = runner["build_benchmark_list"](None, None, include_text=True)
    expected = build_workloads(runner, rows, TUNE14_V1_PROFILE)
    if manifest.get("workloads") != expected:
        raise SystemExit("Tune14 manifest does not match the current wrapper contract")
    entry = navier_entry(manifest)
    oracle = entry["wrapper"].get("post_timing_oracle")
    if not isinstance(oracle, dict):
        raise SystemExit("Tune14 Navier oracle descriptor is missing")
    source = entry["source"]
    oracle_id = oracle["id"]
    expected_marker = oracle["expected_stdout_text"]
    node_wrapper = runner["make_jetstream_node_wrapper"]("navier_stokes", source, oracle_id)
    ljs_wrapper = runner["make_jetstream_ljs_wrapper"]("navier_stokes", source, oracle_id)
    if node_wrapper is None or ljs_wrapper is None:
        raise SystemExit("unable to construct the canonical Navier wrapper")
    artifact = {
        "schema_version": 1,
        "manifest": {"path": args.manifest, "sha256": sha256_file(manifest_path)},
        "source": {"path": source, "sha256": sha256_file(source)},
        "lambda": {"path": args.lambda_exe, "sha256": sha256_file(args.lambda_exe)},
        "expected_density_marker": expected_marker,
        "node": run_checked([args.node, node_wrapper], expected_marker),
        "lambdajs": run_checked([args.lambda_exe, "js", ljs_wrapper], expected_marker),
    }
    if args.output:
        output_path = os.path.abspath(args.output)
        os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
        with open(output_path, "w", encoding="utf-8") as stream:
            json.dump(artifact, stream, indent=2)
            stream.write("\n")
    if artifact["node"]["status"] != "ok" or artifact["lambdajs"]["status"] != "ok":
        print("Tune14 Navier oracle failed: Node=" + artifact["node"]["status"] +
              " LambdaJS=" + artifact["lambdajs"]["status"], file=sys.stderr)
        return 1
    print("Tune14 Navier oracle verified: Node and LambdaJS density digest " + expected_marker)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
