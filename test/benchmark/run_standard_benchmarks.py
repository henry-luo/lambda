#!/usr/bin/env python3
"""Run the canonical release benchmark snapshot workflow."""

import argparse
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


def run_command(args, env=None):
    print("+ " + " ".join(args), flush=True)
    subprocess.run(args, cwd=PROJECT_ROOT, env=env, check=True)


def check_profile_markers():
    proc = subprocess.run(["strings", "./lambda.exe"], cwd=PROJECT_ROOT, capture_output=True, text=True, check=True)
    matches = [marker for marker in PROFILE_MARKERS if marker in proc.stdout]
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
    parser.add_argument("--dry-run", action="store_true", help="print the standardized workflow without executing it")
    args = parser.parse_args()
    results_output = args.results_output or derive_results_output(args.report_output)

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
        if args.report_output:
            print(f"report output : {args.report_output}")
        if not args.skip_build:
            print("+ make release")
        if not args.skip_profile_check:
            print("+ strings ./lambda.exe  [profile marker check]")
        print("+ " + " ".join(benchmark_cmd))
        if report_cmd:
            print("+ " + " ".join(report_cmd))
        return

    if not args.skip_build:
        run_command(["make", "release"])

    if not args.skip_profile_check:
        check_profile_markers()

    env = os.environ.copy()
    env.pop("JS_EXEC_PROFILE", None)
    env.pop("JS_EXEC_PROFILE_OUT", None)
    run_command(benchmark_cmd, env=env)

    if report_cmd:
        run_command(report_cmd)


if __name__ == "__main__":
    main()
