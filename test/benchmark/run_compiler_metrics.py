#!/usr/bin/env python3
"""Collect opt-in JIT compilation cost for a fixed paired benchmark corpus."""

import argparse
import hashlib
import json
import math
import os
import re
import subprocess
from pathlib import Path

from benchmark_provenance import sha256_file
from run_paired_benchmarks import normalized_stdout


ROOT = Path(__file__).resolve().parents[2]
METRICS = ("build_transpile_us", "compile_peak_rss_bytes", "insns", "functions")
CODE_SIZE_RECORD = re.compile(
    r"^\s*Code generation for .* \(addr=[^,]+, len=(\d+)\)", re.MULTILINE)


def median(values):
    ordered = sorted(values)
    return ordered[len(ordered) // 2]


def parse_record(line, marker):
    if not line.startswith("\x01" + marker + " "):
        return None
    fields = {}
    for token in line.split()[1:]:
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def run_once(binary, script, expected_hash, timeout):
    env = os.environ.copy()
    env["LAMBDA_EXEC_BACKEND"] = "jit"
    env["LAMBDA_COMPILER_TIMING"] = "1"
    env["LAMBDA_DISABLE_MIR_CACHE"] = "1"
    result = subprocess.run(
        [str(binary), "run", script], cwd=ROOT, env=env,
        capture_output=True, text=True, timeout=timeout, check=False,
    )
    timing = None
    volume = None
    observable_lines = []
    for line in result.stdout.splitlines():
        if (record := parse_record(line, "COMPILER_TIMING")) is not None:
            timing = record
        elif (record := parse_record(line, "MIR_VOLUME")) is not None:
            volume = record
        else:
            observable_lines.append(line)
    normalized = normalized_stdout("\n".join(observable_lines))
    actual_hash = hashlib.sha256(normalized.encode()).hexdigest()
    if result.returncode != 0 or not timing or not volume or actual_hash != expected_hash:
        raise ValueError(f"compiler metric run failed: {binary} {script}; "
                         f"exit={result.returncode}, timing={bool(timing)}, "
                         f"volume={bool(volume)}, output_equal={actual_hash == expected_hash}; "
                         f"stderr={result.stderr[-500:]}")
    sample = {name: int(timing[name]) for name in METRICS if name in timing}
    sample["insns"] = int(volume["insns"])
    sample["functions"] = int(volume["functions"])
    if any(name not in sample for name in METRICS):
        raise ValueError(f"compiler metric record is incomplete for {script}: {sample}")
    return sample


def run_exact_code_size(binary, script, expected_hash, timeout):
    # MIR's existing level-0 generator log records each published function's
    # actual byte length. A separate run keeps its I/O out of compile timings.
    debug_path = ROOT / "temp/mir_gen_debug.txt"
    debug_path.unlink(missing_ok=True)
    env = os.environ.copy()
    env.update(LAMBDA_EXEC_BACKEND="jit", LAMBDA_COMPILER_TIMING="0",
               LAMBDA_DISABLE_MIR_CACHE="1", LAMBDA_MIR_GEN_DEBUG="0",
               LAMBDA_MIR_GEN_DEBUG_APPEND="1")
    result = subprocess.run(
        [str(binary), "run", script], cwd=ROOT, env=env,
        capture_output=True, text=True, timeout=timeout, check=False,
    )
    actual_hash = hashlib.sha256(normalized_stdout(result.stdout).encode()).hexdigest()
    if result.returncode != 0 or actual_hash != expected_hash or not debug_path.exists():
        raise ValueError(f"code-size run failed: {binary} {script}; "
                         f"exit={result.returncode}, "
                         f"output_equal={actual_hash == expected_hash}")
    lengths = [int(match) for match in CODE_SIZE_RECORD.findall(
        debug_path.read_text())]
    if not lengths:
        raise ValueError(f"MIR generator emitted no function lengths: {script}")
    return sum(lengths), len(lengths)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paired_artifact", type=Path)
    parser.add_argument("--control", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--suite", help="comma-separated pilot suite names")
    parser.add_argument("--bench", help="comma-separated pilot benchmark names")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error("--repeat must be positive")
    artifact = json.loads(args.paired_artifact.read_text())
    suites = set(args.suite.split(",")) if args.suite else None
    benches = set(args.bench.split(",")) if args.bench else None
    source_rows = [row for row in artifact["rows"] if row["variant"] == "typed"
                   and (suites is None or row["suite"] in suites)
                   and (benches is None or row["name"] in benches)]
    if not source_rows:
        raise ValueError("no typed source rows selected")
    binaries = {"control": args.control.resolve(),
                "candidate": args.candidate.resolve()}
    rows = []
    for row in source_rows:
        name = f"{row['suite']}/{row['name']}"
        if row["control_script"] != row["candidate_script"]:
            raise ValueError(f"{name} is a source-pair comparison")
        expected_hash = row["pairs"][0]["control"]["stdout_sha256"]
        samples = {"control": [], "candidate": []}
        for repetition in range(args.repeat):
            order = ("control", "candidate") if repetition % 2 == 0 else (
                "candidate", "control")
            for role in order:
                samples[role].append(run_once(binaries[role], row["script"],
                                              expected_hash, args.timeout))
        control = {metric: median([sample[metric] for sample in samples["control"]])
                   for metric in METRICS}
        candidate = {metric: median([sample[metric] for sample in samples["candidate"]])
                     for metric in METRICS}
        code_functions = {}
        for role in ("control", "candidate"):
            code_bytes, code_functions[role] = run_exact_code_size(
                binaries[role], row["script"], expected_hash, args.timeout)
            (control if role == "control" else candidate)["generated_code_bytes"] = code_bytes
        rows.append({"suite": row["suite"], "name": row["name"],
                     "script": row["script"], "samples": samples,
                     "generated_functions": code_functions,
                     "control_median": control, "candidate_median": candidate,
                     "ratios": {metric: candidate[metric] / control[metric]
                                if control[metric] else None
                                for metric in (*METRICS, "generated_code_bytes")}})
        print(f"{name}: compile {rows[-1]['ratios']['build_transpile_us']:.4f}x, "
              f"RSS {rows[-1]['ratios']['compile_peak_rss_bytes']:.4f}x, "
              f"code {rows[-1]['ratios']['generated_code_bytes']:.4f}x", flush=True)
    aggregate = {}
    for metric in (*METRICS, "generated_code_bytes"):
        values = [row["ratios"][metric] for row in rows if row["ratios"][metric]]
        aggregate[metric] = math.exp(sum(math.log(value) for value in values) /
                                     len(values)) if values else None
    output = {"_metadata": {
        "schema_version": 1,
        "paired_artifact": str(args.paired_artifact),
        "paired_artifact_sha256": sha256_file(args.paired_artifact),
        "control_sha256": sha256_file(args.control),
        "candidate_sha256": sha256_file(args.candidate),
        "repeat": args.repeat,
        "environment": {"LAMBDA_EXEC_BACKEND": "jit", "LAMBDA_COMPILER_TIMING": "1",
                        "LAMBDA_DISABLE_MIR_CACHE": "1"},
        "metric_scope": "fresh-process entry-module timing; generated_code_bytes "
                        "sums all MIR contexts' per-function code lengths in a separate diagnostic run; "
                        "code-size diagnostics are excluded from compile timings",
    }, "aggregate_geometric_mean_ratios": aggregate, "rows": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(f"saved {args.output}")


if __name__ == "__main__":
    main()
