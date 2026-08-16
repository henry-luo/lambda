#!/usr/bin/env python3
"""Interleaved A/B runner for matched Lambda release binaries.

Each pair runs the same Lambda script once with the control binary and once
with the candidate binary.  The order alternates by pair so host drift is not
systematically assigned to one binary.  Timing samples and observable stdout
digests are retained in a JSON artifact under ``temp/``.

This runner deliberately measures only the MIR Lambda scripts.  It is the
causal gate for Result30 follow-up work; the normal benchmark matrix remains
the publication snapshot runner.
"""

import argparse
import datetime
import hashlib
import json
import os
import platform
import signal
import subprocess
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.join(SCRIPT_DIR, "..", "..")
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)
os.chdir(PROJECT_ROOT)

from run_benchmarks import (  # noqa: E402
    build_benchmark_list,
    mir_script_variants,
    parse_timing,
)


TIMING_LINE = "__TIMING__:"


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def command_output(command):
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False)
    except OSError:
        return None
    output = (result.stdout or result.stderr or "").strip()
    return output or None


def normalized_stdout(stdout):
    """Remove only the nondeterministic timing marker before hashing output."""
    lines = []
    for line in stdout.splitlines():
        if TIMING_LINE not in line:
            lines.append(line.rstrip())
    return "\n".join(lines).strip()


def median(values):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[len(ordered) // 2]


def run_once(binary, script, timeout_s):
    """Run one script and return a serializable timing/observable record."""
    command = [binary, "run", script]
    started = time.perf_counter_ns()
    process = None
    try:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=(os.name != "nt"),
        )
        try:
            stdout, stderr = process.communicate(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            if os.name != "nt":
                os.killpg(os.getpgid(process.pid), signal.SIGKILL)
            else:
                process.kill()
            process.wait()
            return {
                "status": "timeout",
                "wall_ms": float(timeout_s * 1000),
                "exec_ms": None,
                "returncode": process.returncode,
                "stdout_sha256": None,
                "stderr_sha256": None,
            }
    except OSError as error:
        return {
            "status": "launch_error",
            "wall_ms": None,
            "exec_ms": None,
            "returncode": None,
            "error": str(error),
            "stdout_sha256": None,
            "stderr_sha256": None,
        }

    wall_ms = (time.perf_counter_ns() - started) / 1_000_000.0
    stable_stdout = normalized_stdout(stdout)
    stdout_hash = hashlib.sha256(stable_stdout.encode("utf-8")).hexdigest()
    stderr_hash = hashlib.sha256((stderr or "").encode("utf-8")).hexdigest()
    if process.returncode != 0:
        status = f"exit_{process.returncode}"
    else:
        exec_ms = parse_timing(stdout)
        status = "ok" if exec_ms is not None else "wall_fallback"
    return {
        "status": status,
        "wall_ms": wall_ms,
        "exec_ms": parse_timing(stdout) if process.returncode == 0 else None,
        "returncode": process.returncode,
        "stdout_sha256": stdout_hash,
        "stderr_sha256": stderr_hash,
    }


def status_counts(samples):
    counts = {}
    for sample in samples:
        status = sample["status"]
        counts[status] = counts.get(status, 0) + 1
    return counts


def summarize_side(samples):
    values = [sample["exec_ms"] for sample in samples if sample["exec_ms"] is not None]
    return {
        "median_exec_ms": median(values),
        "status_counts": status_counts(samples),
        "successful_samples": len(values),
    }


def compare_row(control, candidate, script, pairs, timeout_s):
    control_samples = []
    candidate_samples = []
    pair_records = []
    for pair_index in range(pairs):
        control_first = pair_index % 2 == 0
        order = "control,candidate" if control_first else "candidate,control"
        first_binary = control if control_first else candidate
        second_binary = candidate if control_first else control
        first_label = "control" if control_first else "candidate"
        second_label = "candidate" if control_first else "control"
        first = run_once(first_binary, script, timeout_s)
        second = run_once(second_binary, script, timeout_s)
        samples = {first_label: first, second_label: second}
        control_samples.append(samples["control"])
        candidate_samples.append(samples["candidate"])
        pair_records.append({
            "pair_index": pair_index + 1,
            "order": order,
            "control": samples["control"],
            "candidate": samples["candidate"],
            "stdout_equal": (
                samples["control"]["stdout_sha256"] is not None
                and samples["control"]["stdout_sha256"] == samples["candidate"]["stdout_sha256"]
            ),
        })
        print(".", end="", flush=True)

    control_summary = summarize_side(control_samples)
    candidate_summary = summarize_side(candidate_samples)
    control_median = control_summary["median_exec_ms"]
    candidate_median = candidate_summary["median_exec_ms"]
    ratio = None
    if control_median and candidate_median:
        ratio = candidate_median / control_median
    valid_pairs = [
        pair for pair in pair_records
        if pair["control"]["exec_ms"] is not None and pair["candidate"]["exec_ms"] is not None
    ]
    candidate_wins = sum(
        pair["candidate"]["exec_ms"] < pair["control"]["exec_ms"]
        for pair in valid_pairs
    )
    return {
        "script": script,
        "pairs_requested": pairs,
        "pairs_valid": len(valid_pairs),
        "timeout_s": timeout_s,
        "control": control_summary,
        "candidate": candidate_summary,
        "candidate_over_control_median_ratio": ratio,
        "candidate_wins": candidate_wins,
        "stdout_equal_all": bool(pair_records) and all(pair["stdout_equal"] for pair in pair_records),
        "pairs": pair_records,
    }


def parse_filters(value):
    if not value:
        return None
    return [part.strip() for part in value.split(",") if part.strip()]


def main():
    parser = argparse.ArgumentParser(
        description="Interleaved A/B timing for two Lambda release binaries"
    )
    parser.add_argument("--control", required=True, help="control release binary")
    parser.add_argument("--candidate", required=True, help="candidate release binary")
    parser.add_argument("-s", "--suite", default=None, help="comma-separated suite filter")
    parser.add_argument("-b", "--bench", default=None, help="comma-separated benchmark filter")
    parser.add_argument(
        "--variants", choices=["untyped", "typed", "both"], default="both",
        help="Lambda script variants to compare (default: both)",
    )
    parser.add_argument("-p", "--pairs", type=int, default=41, help="alternating pairs per row")
    parser.add_argument("-t", "--timeout", type=int, default=120, help="timeout per process")
    parser.add_argument(
        "-o", "--output", default=None,
        help="JSON artifact (default: temp/paired_benchmarks_<timestamp>.json)",
    )
    args = parser.parse_args()
    if args.pairs < 1:
        parser.error("--pairs must be positive")

    control = os.path.abspath(args.control)
    candidate = os.path.abspath(args.candidate)
    for label, path in (("control", control), ("candidate", candidate)):
        if not os.path.isfile(path) or not os.access(path, os.X_OK):
            parser.error(f"{label} binary is not executable: {path}")

    suite_filters = parse_filters(args.suite)
    bench_filters = parse_filters(args.bench)
    benchmarks = build_benchmark_list(suite_filters, bench_filters)
    if not benchmarks:
        parser.error("no benchmarks matched the supplied filters")

    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    output = args.output or os.path.join("temp", f"paired_benchmarks_{timestamp}.json")
    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
    variants = ["untyped", "typed"] if args.variants == "both" else [args.variants]
    metadata = {
        "schema_version": 1,
        "started_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "platform": f"{platform.system()} {platform.machine()}",
        "control": {"path": control, "sha256": sha256_file(control)},
        "candidate": {"path": candidate, "sha256": sha256_file(candidate)},
        "git_commit": command_output(["git", "rev-parse", "HEAD"]),
        "power_state": command_output(["pmset", "-g", "batt"]) if platform.system() == "Darwin" else None,
        "suite_filters": suite_filters or [],
        "bench_filters": bench_filters or [],
        "variants": variants,
        "pairs": args.pairs,
        "timeout_s": args.timeout,
        "command": " ".join(sys.argv),
    }
    artifact = {"_metadata": metadata, "rows": []}
    print(f"Paired A/B: {len(benchmarks)} row(s), {args.pairs} pair(s), variants={','.join(variants)}")
    for benchmark in benchmarks:
        untyped, typed = mir_script_variants(benchmark)
        scripts = {"untyped": untyped, "typed": typed}
        for variant in variants:
            script = scripts[variant]
            row = {
                "suite": benchmark["suite"],
                "name": benchmark["name"],
                "variant": variant,
                "script": script,
            }
            if script is None or not os.path.exists(script):
                row["status"] = "missing_script"
                artifact["rows"].append(row)
                print(f"\n{benchmark['suite']}/{benchmark['name']}[{variant}] missing script")
                continue
            print(f"\n{benchmark['suite']}/{benchmark['name']}[{variant}] ", end="", flush=True)
            row.update(compare_row(control, candidate, script, args.pairs, args.timeout))
            row["status"] = "ok" if row["pairs_valid"] == args.pairs else "partial_ok"
            artifact["rows"].append(row)
            ratio = row["candidate_over_control_median_ratio"]
            ratio_text = "n/a" if ratio is None else f"{ratio:.4f}"
            print(f" ratio={ratio_text} wins={row['candidate_wins']}/{row['pairs_valid']} stdout_equal={row['stdout_equal_all']}")

    artifact["_metadata"]["finished_at"] = datetime.datetime.now().isoformat(timespec="seconds")
    with open(output, "w") as stream:
        json.dump(artifact, stream, indent=2)
    print(f"Saved paired artifact to {output}")


if __name__ == "__main__":
    main()
