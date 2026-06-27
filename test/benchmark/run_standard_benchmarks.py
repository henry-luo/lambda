#!/usr/bin/env python3
"""Run the canonical release benchmark snapshot workflow."""

import argparse
import datetime
import os
import re
import subprocess
import sys


PROJECT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
DEFAULT_ENGINES = "mir,lambdajs,quickjs,nodejs"
PROFILE_MARKERS = [
    "JS_EXEC_PROFILE",
    "JS_EXEC_PROFILE_OUT",
    "js_profile_property_set",
    "gc_sweep_walked_objects",
]


def derive_results_output(report_output):
    if not report_output:
        return "test/benchmark/benchmark_results_v_latest.json"
    base_name = os.path.basename(report_output)
    match = re.search(r"Overall_Result(\d+)\.md$", base_name)
    if match:
        return os.path.join("test", "benchmark", f"benchmark_results_v{match.group(1)}.json")
    stem, _ = os.path.splitext(base_name)
    safe_stem = re.sub(r"[^A-Za-z0-9_]+", "_", stem).strip("_").lower() or "benchmark_results"
    return os.path.join("test", "benchmark", f"{safe_stem}.json")


def derive_log_dir(report_output, results_output):
    source = report_output or results_output
    base_name = os.path.basename(source)
    match = re.search(r"(?:Overall_Result|benchmark_results_v)(\d+)", base_name)
    suffix = f"v{match.group(1)}" if match else "latest"
    return os.path.join("temp", f"benchmark_{suffix}")


def run_command(args, env=None, log_path=None):
    print("+ " + " ".join(args), flush=True)
    if not log_path:
        subprocess.run(args, cwd=PROJECT_ROOT, env=env, check=True)
        return
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    with open(log_path, "w") as log_file:
        proc = subprocess.Popen(args, cwd=PROJECT_ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        for line in proc.stdout:
            print(line, end="")
            log_file.write(line)
        rc = proc.wait()
    if rc != 0:
        raise subprocess.CalledProcessError(rc, args)


def check_profile_markers(log_path=None):
    proc = subprocess.run(["strings", "./lambda.exe"], cwd=PROJECT_ROOT, capture_output=True, text=True, check=True)
    matches = [marker for marker in PROFILE_MARKERS if marker in proc.stdout]
    if log_path:
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        with open(log_path, "w") as f:
            f.write(f"checked_at={datetime.datetime.now().isoformat(timespec='seconds')}\n")
            f.write(f"markers={','.join(PROFILE_MARKERS)}\n")
            f.write(f"matches={','.join(matches)}\n")
    if matches:
        joined = ", ".join(matches)
        raise SystemExit(f"release profiling check failed; found marker(s): {joined}")
    print("release profiling check passed: no profiling markers found in lambda.exe")


def main():
    parser = argparse.ArgumentParser(description="Standard Lambda benchmark snapshot workflow")
    parser.add_argument("--engines", default=DEFAULT_ENGINES, help="comma-separated engines to run")
    parser.add_argument("--runs", type=int, default=3, help="runs per benchmark per engine")
    parser.add_argument("--timeout", type=int, default=180, help="timeout per single run in seconds")
    parser.add_argument("--skip-build", action="store_true", help="reuse the existing lambda.exe")
    parser.add_argument("--skip-profile-check", action="store_true", help="skip release binary profiling-symbol check")
    parser.add_argument("--results-output", default=None, help="benchmark JSON output path")
    parser.add_argument("--report-output", default=None, help="optional Overall_ResultN.md output path")
    parser.add_argument("--report-title", default="Lambda Benchmark Results", help="optional report title")
    parser.add_argument("--merge", action="store_true", help="merge into an existing result JSON instead of starting fresh")
    parser.add_argument("--log-dir", default=None, help="directory for build/benchmark/report logs")
    parser.add_argument("--dry-run", action="store_true", help="print the standardized workflow without executing it")
    args = parser.parse_args()
    results_output = args.results_output or derive_results_output(args.report_output)
    log_dir = args.log_dir or derive_log_dir(args.report_output, results_output)

    benchmark_cmd = [
        sys.executable,
        "test/benchmark/run_benchmarks.py",
        "-e",
        args.engines,
        "-n",
        str(args.runs),
        "-t",
        str(args.timeout),
        "--results-output",
        results_output,
    ]
    if not args.merge:
        benchmark_cmd.append("--fresh")
    report_cmd = None
    if args.report_output:
        report_cmd = [
            sys.executable,
            "test/benchmark/gen_overall_result.py",
            "--output",
            args.report_output,
            "--input",
            results_output,
            "--title",
            args.report_title,
            "--engines",
            args.engines,
        ]

    if args.dry_run:
        print(f"results output: {results_output}")
        print(f"log dir       : {log_dir}")
        if args.report_output:
            print(f"report output : {args.report_output}")
        if not args.skip_build:
            print(f"+ make release  [log: {os.path.join(log_dir, 'build_release.log')}]")
        if not args.skip_profile_check:
            print(f"+ strings ./lambda.exe  [profile marker check, log: {os.path.join(log_dir, 'profile_check.log')}]")
        print("+ " + " ".join(benchmark_cmd) + f"  [log: {os.path.join(log_dir, 'benchmark.log')}]")
        if report_cmd:
            print("+ " + " ".join(report_cmd) + f"  [log: {os.path.join(log_dir, 'report.log')}]")
        return

    if not args.skip_build:
        run_command(["make", "release"], log_path=os.path.join(log_dir, "build_release.log"))

    if not args.skip_profile_check:
        check_profile_markers(log_path=os.path.join(log_dir, "profile_check.log"))

    env = os.environ.copy()
    env.pop("JS_EXEC_PROFILE", None)
    env.pop("JS_EXEC_PROFILE_OUT", None)
    env["LAMBDA_BENCH_PROFILE_CHECK"] = "passed" if not args.skip_profile_check else "skipped"
    env["LAMBDA_BENCH_LOG_DIR"] = log_dir
    run_command(benchmark_cmd, env=env, log_path=os.path.join(log_dir, "benchmark.log"))

    if report_cmd:
        run_command(report_cmd, log_path=os.path.join(log_dir, "report.log"))


if __name__ == "__main__":
    main()
