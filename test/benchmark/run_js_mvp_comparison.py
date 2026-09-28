#!/usr/bin/env python3
"""Compare full MIR and MVP in one optimized host against Node output oracles."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import time

import run_benchmarks as bench
import run_paired_benchmarks as paired
from js_benchmark_manifest import TUNE14_V1_PROFILE, build_workloads


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="./lambda-profile.exe")
    parser.add_argument("--output", required=True)
    parser.add_argument("--pairs", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    if args.pairs < 1 or args.timeout < 1:
        parser.error("pairs and timeout must be positive")
    binary = str(Path(args.binary).resolve())
    bench.check_release_build(exe_path=binary)
    bench.require_pinned_node_version(["nodejs"], "time")
    # Profile instrumentation stays dormant during timing; the full engine's
    # default is AUTO, so explicitly select the compiled benchmark lane.
    os.environ["JS_EXECUTION_BACKEND"] = "mir"
    os.environ["JS_OPT_TRACE"] = "0"
    os.environ["JS_MIR_DUMP"] = "0"
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    rows = bench.build_benchmark_list(None, None, include_text=True)
    workloads = build_workloads(vars(bench), rows, TUNE14_V1_PROFILE)
    artifact = {
        "metadata": {
            "started_at": datetime.datetime.now().astimezone().isoformat(),
            "git_commit": paired.command_output(["git", "rev-parse", "HEAD"]),
            "git_diff": paired.command_output(["git", "diff"]),
            "binary": binary, "binary_sha256": paired.sha256_file(binary),
            "runner_sha256": paired.sha256_file(__file__),
            "paired_runner_sha256": paired.sha256_file(paired.__file__),
            "node": shutil.which("node"),
            "node_version": paired.command_output(["node", "--version"]),
            "platform": platform.platform(),
            "power": paired.command_output(["pmset", "-g", "batt"]),
            "environment": {k: v for k, v in os.environ.items()
                            if k.startswith(("JS_", "LAMBDA_"))},
            "pairs": args.pairs, "timeout_s": args.timeout,
            "control": "full LambdaJS MIR", "candidate": "MVP MIR",
            "correctness": "Node normalized stdout equality, zero exit, timing, no FAIL; Navier post-timing density oracle",
        },
        "rows": [],
    }

    def save():
        output.write_text(json.dumps(artifact, indent=2) + "\n")

    for index, (spec, workload) in enumerate(zip(rows, workloads)):
        script = workload["source"]
        oracle = None
        if spec["is_jetstream"]:
            if spec["name"] == "navier_stokes":
                oracle = bench.NAVIER_STOKES_DENSITY_ORACLE_V1
            # All three engines execute the same strict-source-preserving wrapper.
            script = bench.make_jetstream_node_wrapper(spec["name"], script, oracle)
        expected = bench.jetstream_post_timing_oracle_marker(oracle)
        row = {"workload": workload, "script": script,
               "script_sha256": paired.sha256_file(script)}
        print(f"{index + 1}/{len(rows)} {workload['id']}", end=" ", flush=True)
        started = time.perf_counter()
        try:
            reference = subprocess.run(["node", script], capture_output=True, text=True,
                                       timeout=args.timeout)
            stdout = paired.normalized_stdout(reference.stdout)
            row["node"] = {
                "returncode": reference.returncode, "stdout": stdout,
                "stderr": reference.stderr,
                "wall_ms": (time.perf_counter() - started) * 1000,
                "exec_ms": bench.parse_timing(reference.stdout),
                "stdout_sha256": hashlib.sha256(stdout.encode()).hexdigest(),
            }
            node_ok = (reference.returncode == 0 and row["node"]["exec_ms"] is not None
                       and not re.search(r"\bFAIL\b", stdout)
                       and (expected is None or expected in stdout))
        except subprocess.TimeoutExpired:
            row["node"] = {"status": "timeout"}
            node_ok = False
        row["preflight"] = {
            runtime: paired.run_once(binary, script, args.timeout, "js", "jit", expected, runtime)
            for runtime in ("legacy", "mvp")
        }
        output_ok = node_ok and all(
            sample["status"] == "ok" and sample["exec_ms"] > 0 and
            sample["stdout_sha256"] == row["node"]["stdout_sha256"] and
            sample.get("stdout_contains_expected") is not False
            for sample in row["preflight"].values())
        if output_ok:
            row["paired"] = paired.compare_row(
                binary, binary, script, script, args.pairs, args.timeout, language="js",
                expected_stdout_text=expected, candidate_js_runtime="mvp")
            samples = [pair[side] for pair in row["paired"]["pairs"]
                       for side in ("control", "candidate")]
            valid = all(sample["status"] == "ok" and sample["exec_ms"] > 0 and
                        sample["stdout_sha256"] == row["node"]["stdout_sha256"] and
                        sample.get("stdout_contains_expected") is not False
                        for sample in samples)
            row["status"] = "validated" if valid else "sample_failure"
            # A later sample can fail after preflight; retain it without
            # publishing a partial ratio or aborting the remaining matrix.
            row["full_over_mvp"] = (
                1 / row["paired"]["candidate_over_control_median_ratio"] if valid else None)
            row["wall_medians_ms"] = {
                side: statistics.median(pair[side]["wall_ms"] for pair in row["paired"]["pairs"])
                for side in ("control", "candidate")} if valid else None
            ratio = f"{row['full_over_mvp']:.3f}" if valid else "n/a"
            print(f" {row['status']} full/MVP={ratio}", flush=True)
        else:
            row["status"] = "preflight_failure"
            print("preflight_failure (timing comparison withheld)", flush=True)
        artifact["rows"].append(row)
        save()
    artifact["metadata"]["finished_at"] = datetime.datetime.now().astimezone().isoformat()
    artifact["metadata"]["binary_unchanged"] = paired.sha256_file(binary) == artifact["metadata"]["binary_sha256"]
    artifact["metadata"]["sources_unchanged"] = workloads == build_workloads(vars(bench), rows, TUNE14_V1_PROFILE)
    save()


if __name__ == "__main__":
    main()
