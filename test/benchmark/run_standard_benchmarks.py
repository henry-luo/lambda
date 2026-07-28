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
# Emitted by the debug banner in lambda/main.cpp; absent from release builds.
DEBUG_BUILD_MARKER = "Running DEBUG build"


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


def detect_ac_power():
    """Return (on_ac, detail). on_ac is True/False, or None when undetectable.

    Battery power makes timings meaningless: macOS and Linux both throttle CPU
    frequency and disable turbo on battery, which shifts results by far more
    than any tuning phase this suite is used to measure.
    """
    if sys.platform == "darwin":
        try:
            out = subprocess.run(["pmset", "-g", "batt"], capture_output=True,
                                 text=True, timeout=10).stdout
        except (OSError, subprocess.SubprocessError) as exc:
            return None, f"pmset unavailable ({exc})"
        if "AC Power" in out:
            return True, "pmset: drawing from AC Power"
        if "Battery Power" in out:
            return False, "pmset: drawing from Battery Power"
        return None, f"pmset output not recognized: {out.splitlines()[:1]}"

    if sys.platform.startswith("linux"):
        import glob
        # Mains adapters expose type=Mains with online=1 when plugged in.
        for supply in sorted(glob.glob("/sys/class/power_supply/*")):
            try:
                with open(os.path.join(supply, "type")) as f:
                    if f.read().strip() != "Mains":
                        continue
                with open(os.path.join(supply, "online")) as f:
                    online = f.read().strip()
            except OSError:
                continue
            name = os.path.basename(supply)
            if online == "1":
                return True, f"{name}/online=1"
            return False, f"{name}/online=0"
        return None, "no Mains power supply found under /sys/class/power_supply"

    return None, f"AC detection not implemented for platform {sys.platform!r}"


def check_ac_power(log_path=None):
    on_ac, detail = detect_ac_power()
    if log_path:
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        with open(log_path, "w") as f:
            f.write(f"checked_at={datetime.datetime.now().isoformat(timespec='seconds')}\n")
            f.write(f"platform={sys.platform}\n")
            f.write(f"on_ac={on_ac}\n")
            f.write(f"detail={detail}\n")
    if on_ac is False:
        raise SystemExit(
            "benchmark aborted: machine is on BATTERY power.\n"
            f"  {detail}\n"
            "  CPU frequency scaling on battery invalidates the timings.\n"
            "  Plug in and re-run, or pass --skip-power-check to override."
        )
    if on_ac is None:
        print(f"warning: could not determine AC power state ({detail}); continuing")
        return "unknown"
    print(f"AC power check passed: {detail}")
    return "passed"


def check_release_build(log_path=None):
    """Reject a debug lambda.exe: -Og plus assertions make timings meaningless."""
    proc = subprocess.run(["strings", "./lambda.exe"], cwd=PROJECT_ROOT,
                          capture_output=True, text=True, check=True)
    is_debug = DEBUG_BUILD_MARKER in proc.stdout
    if log_path:
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        with open(log_path, "w") as f:
            f.write(f"checked_at={datetime.datetime.now().isoformat(timespec='seconds')}\n")
            f.write(f"marker={DEBUG_BUILD_MARKER}\n")
            f.write(f"is_debug={is_debug}\n")
    if is_debug:
        raise SystemExit(
            "benchmark aborted: ./lambda.exe is a DEBUG build.\n"
            f"  found marker {DEBUG_BUILD_MARKER!r} in the binary.\n"
            "  Run `make release` (the default workflow does this unless "
            "--skip-build is passed)."
        )
    print("release build check passed: lambda.exe is not a debug build")


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
    parser.add_argument("--skip-power-check", action="store_true",
                        help="run even when the machine is on battery (timings will be unreliable)")
    parser.add_argument("--results-output", default=None, help="benchmark JSON output path")
    parser.add_argument("--report-output", default=None, help="optional Overall_ResultN.md output path")
    parser.add_argument("--report-title", default="Lambda Benchmark Results", help="optional report title")
    parser.add_argument("--merge", action="store_true", help="merge into an existing result JSON instead of starting fresh")
    parser.add_argument("--typed", action="store_true",
                        help="run both untyped and typed MIR variants in time mode")
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
    if args.typed:
        benchmark_cmd.append("--typed")
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
        if not args.skip_power_check:
            print(f"+ AC power check  [log: {os.path.join(log_dir, 'power_check.log')}]")
        if not args.skip_build:
            print(f"+ make release  [log: {os.path.join(log_dir, 'build_release.log')}]")
        print(f"+ strings ./lambda.exe  [debug-build check, log: {os.path.join(log_dir, 'release_check.log')}]")
        if not args.skip_profile_check:
            print(f"+ strings ./lambda.exe  [profile marker check, log: {os.path.join(log_dir, 'profile_check.log')}]")
        print("+ " + " ".join(benchmark_cmd) + f"  [log: {os.path.join(log_dir, 'benchmark.log')}]")
        if report_cmd:
            print("+ " + " ".join(report_cmd) + f"  [log: {os.path.join(log_dir, 'report.log')}]")
        return

    # Power first: it is the cheapest check and failing it invalidates the run,
    # so there is no point spending a full release build to find out.
    power_state = "skipped"
    if not args.skip_power_check:
        power_state = check_ac_power(log_path=os.path.join(log_dir, "power_check.log"))

    if not args.skip_build:
        run_command(["make", "release"], log_path=os.path.join(log_dir, "build_release.log"))

    # Always checked, including under --skip-build, where a stale debug
    # lambda.exe left over from a test cycle is exactly the likely mistake.
    check_release_build(log_path=os.path.join(log_dir, "release_check.log"))

    if not args.skip_profile_check:
        check_profile_markers(log_path=os.path.join(log_dir, "profile_check.log"))

    env = os.environ.copy()
    env.pop("JS_EXEC_PROFILE", None)
    env.pop("JS_EXEC_PROFILE_OUT", None)
    env["LAMBDA_BENCH_PROFILE_CHECK"] = "passed" if not args.skip_profile_check else "skipped"
    env["LAMBDA_BENCH_POWER_CHECK"] = power_state
    env["LAMBDA_BENCH_LOG_DIR"] = log_dir
    run_command(benchmark_cmd, env=env, log_path=os.path.join(log_dir, "benchmark.log"))

    if report_cmd:
        run_command(report_cmd, log_path=os.path.join(log_dir, "report.log"))


if __name__ == "__main__":
    main()
