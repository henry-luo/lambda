#!/usr/bin/env python3
"""Run the canonical release benchmark snapshot workflow."""

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time


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
CACHE_DIR = os.path.join("test", "benchmark", "exe")
TEST262_BASELINE_CMD = [
    "./test/test_js_test262_gtest.exe",
    "--baseline-only",
    "--batch-only",
    "--run-async",
    "--async-list=test/js262/test262_baseline.txt",
    "--gtest_brief=1",
]
TEST262_SUMMARY_RE = re.compile(
    r"^\[test262\] All (?P<completed_tests>\d+) tests completed in "
    r"(?P<duration_s>[\d.]+)s \((?P<components>.*)\)$", re.MULTILINE)
TEST262_FULLY_PASSED_RE = re.compile(r"Fully passed:\s*(?P<fully_passed>\d+)\s*/\s*(?P<baseline_tests>\d+)")
TEST262_REGRESSIONS_RE = re.compile(r"Regressions:\s*(?P<regressions>\d+)")
TEST262_COMPONENT_RES = {
    "prep": re.compile(r"\bprep (?P<value>[\d.]+)s"),
    "batch": re.compile(r"\bbatch (?P<value>[\d.]+)s \["),
    "batched": re.compile(r"\bbatched (?P<value>[\d.]+)s: sync"),
    "sync": re.compile(r"\bsync (?P<value>[\d.]+)s"),
    "async": re.compile(r"\basync (?P<value>[\d.]+)s"),
    "non_batched": re.compile(r"\bnon-batched (?P<value>[\d.]+)s"),
    "retry": re.compile(r"\bretry (?P<value>[\d.]+)s"),
    "partial": re.compile(r"\bpartial (?P<value>[\d.]+)s"),
    "timing": re.compile(r"\btiming (?P<value>[\d.]+)s"),
    "memory": re.compile(r"\bmemory (?P<value>[\d.]+)s"),
    "eval": re.compile(r"\beval (?P<value>[\d.]+)s"),
}


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


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def derive_cache_path(results_output, commit):
    """Name a snapshot archive without a .exe suffix so make clean preserves it."""
    match = re.search(r"benchmark_results_v(\d+)\.json$", os.path.basename(results_output))
    prefix = f"lambda-v{match.group(1)}" if match else "lambda"
    return os.path.join(CACHE_DIR, f"{prefix}-{commit[:10]}")


def cache_lambda_exe(results_output):
    """Preserve the just-checked release binary and reject ambiguous cache collisions."""
    source = os.path.join(PROJECT_ROOT, "lambda.exe")
    if not os.path.isfile(source):
        raise SystemExit("benchmark aborted: release binary ./lambda.exe is missing before cache")
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=PROJECT_ROOT,
                            capture_output=True, text=True, check=True).stdout.strip()
    if not commit:
        raise SystemExit("benchmark aborted: unable to determine the release binary commit")
    cache_path = os.path.join(PROJECT_ROOT, derive_cache_path(results_output, commit))
    source_hash = sha256_file(source)
    if os.path.exists(cache_path):
        cache_hash = sha256_file(cache_path)
        if cache_hash != source_hash:
            # A result name and commit must identify one exact artifact; otherwise a later
            # benchmark report could claim reproducibility with the wrong executable.
            raise SystemExit(
                "benchmark aborted: cached binary collision at "
                f"{os.path.relpath(cache_path, PROJECT_ROOT)}; contents differ from ./lambda.exe")
        print(f"release cache hit: {os.path.relpath(cache_path, PROJECT_ROOT)}")
    else:
        os.makedirs(os.path.dirname(cache_path), exist_ok=True)
        shutil.copy2(source, cache_path)
        if sha256_file(cache_path) != source_hash:
            raise SystemExit(
                "benchmark aborted: copied release binary did not match ./lambda.exe at "
                f"{os.path.relpath(cache_path, PROJECT_ROOT)}")
        print(f"release cached: {os.path.relpath(cache_path, PROJECT_ROOT)}")
    return {
        "path": os.path.relpath(cache_path, PROJECT_ROOT),
        "size_bytes": os.path.getsize(cache_path),
        "sha256": source_hash,
    }


def run_command(args, env=None, log_path=None):
    print("+ " + " ".join(args), flush=True)
    started_at = time.monotonic()
    if not log_path:
        subprocess.run(args, cwd=PROJECT_ROOT, env=env, check=True)
        return time.monotonic() - started_at
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    with open(log_path, "w") as log_file:
        proc = subprocess.Popen(args, cwd=PROJECT_ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        for line in proc.stdout:
            print(line, end="")
            log_file.write(line)
        rc = proc.wait()
    elapsed_s = time.monotonic() - started_at
    if rc != 0:
        error = subprocess.CalledProcessError(rc, args)
        error.elapsed_s = elapsed_s
        raise error
    return elapsed_s


def parse_test262_baseline_report(log_path):
    """Read the harness timing breakdown instead of conflating it with setup time."""
    with open(log_path) as f:
        output = f.read()
    summary = TEST262_SUMMARY_RE.search(output)
    passed = TEST262_FULLY_PASSED_RE.search(output)
    regressions = TEST262_REGRESSIONS_RE.search(output)
    if not summary or not passed or not regressions:
        raise ValueError("missing Test262 completion, pass-count, or regression summary")
    timing_s = {}
    component_text = summary.group("components")
    for name, pattern in TEST262_COMPONENT_RES.items():
        match = pattern.search(component_text)
        if not match:
            raise ValueError(f"missing Test262 timing component {name!r}")
        timing_s[name] = float(match.group("value"))
    return {
        "status": "passed",
        "duration_s": float(summary.group("duration_s")),
        "completed_tests": int(summary.group("completed_tests")),
        "baseline_tests": int(passed.group("baseline_tests")),
        "fully_passed": int(passed.group("fully_passed")),
        "regressions": int(regressions.group("regressions")),
        "timing_s": timing_s,
    }


def run_test262_baseline(log_dir):
    """Gate snapshots on Test262 and time only the release-runtime test pass."""
    prepare_log = os.path.join(log_dir, "test262_prepare.log")
    result_log = os.path.join(log_dir, "test262_baseline.log")
    try:
        run_command(["make", "ensure-test262-gtest"], log_path=prepare_log)
    except subprocess.CalledProcessError:
        raise SystemExit(
            "benchmark aborted: could not prepare the Test262 baseline runner; "
            f"see {prepare_log}") from None
    try:
        baseline_env = os.environ.copy()
        baseline_env.pop("JS_EXEC_PROFILE", None)
        baseline_env.pop("JS_EXEC_PROFILE_OUT", None)
        elapsed_s = run_command(TEST262_BASELINE_CMD, env=baseline_env, log_path=result_log)
    except subprocess.CalledProcessError as error:
        elapsed_s = getattr(error, "elapsed_s", 0.0)
        raise SystemExit(
            "benchmark aborted: Test262 baseline failed "
            f"after {elapsed_s:.2f}s; benchmark results were not produced. "
            f"See {result_log}") from None
    try:
        result = parse_test262_baseline_report(result_log)
    except ValueError as error:
        raise SystemExit(
            "benchmark aborted: Test262 baseline passed but its timing summary could not be parsed; "
            f"benchmark results were not produced. {error}. See {result_log}") from None
    result["wall_duration_s"] = elapsed_s
    result["command"] = " ".join(TEST262_BASELINE_CMD)
    result["gate"] = True
    print(f"Test262 baseline passed: {result['fully_passed']} / {result['baseline_tests']} "
          f"in {result['duration_s']:.2f}s (harness); {elapsed_s:.2f}s wall")
    return result


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
    parser.add_argument("--suite", default=None,
                        help="comma-separated suite name(s) to pass to the benchmark runner")
    parser.add_argument("--runs", type=int, default=3, help="runs per benchmark per engine")
    parser.add_argument("--timeout", type=int, default=180, help="timeout per single run in seconds")
    parser.add_argument("--cooldown", type=int, default=10,
                        help="idle seconds between suites (default: 10; 0 disables)")
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
    parser.add_argument("--legacy", action="store_true",
                        help="legacy single-column run: time ONLY the typed *2.ls and report it "
                             "under MIR-U. Kept for comparability with historical result files")
    parser.add_argument("--log-dir", default=None, help="directory for build/benchmark/report logs")
    parser.add_argument("--dry-run", action="store_true", help="print the standardized workflow without executing it")
    args = parser.parse_args()

    # Forwarded to run_benchmarks.py, which requires an explicit variant selection.
    # Gate it here too so the failure names the workflow flag the user actually typed.
    if args.typed and args.legacy:
        print("error: --typed and --legacy are mutually exclusive.", file=sys.stderr)
        sys.exit(2)
    if not args.typed and not args.legacy:
        print(
            "error: a variant selection is required.\n"
            "\n"
            "  --typed   run BOTH Lambda variants (recommended)\n"
            "              MIR-U = untyped <bench>.ls, MIR-T = typed <bench>2.ls\n"
            "  --legacy  legacy single-column run: ONLY the typed <bench>2.ls,\n"
            "            reported under the MIR-U heading\n"
            "\n"
            "This used to default to --legacy, which reported a typed measurement\n"
            "as untyped.\n",
            file=sys.stderr)
        sys.exit(2)

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
        "--cooldown",
        str(args.cooldown),
    ]
    if args.suite:
        # named snapshots must be able to include opt-in suites without losing the guarded workflow
        benchmark_cmd.extend(["-s", args.suite])
    if not args.merge:
        benchmark_cmd.append("--fresh")
    if args.typed:
        benchmark_cmd.append("--typed")
    if args.legacy:
        benchmark_cmd.append("--legacy")
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
        print(f"+ make ensure-test262-gtest  [log: {os.path.join(log_dir, 'test262_prepare.log')}]")
        print("+ " + " ".join(TEST262_BASELINE_CMD)
              + f"  [gate and timing log: {os.path.join(log_dir, 'test262_baseline.log')}]")
        cache_commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=PROJECT_ROOT,
                                      capture_output=True, text=True, check=True).stdout.strip()
        print(f"+ cache ./lambda.exe -> {derive_cache_path(results_output, cache_commit)}")
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

    test262_result = run_test262_baseline(log_dir)
    cache_info = cache_lambda_exe(results_output)

    env = os.environ.copy()
    env.pop("JS_EXEC_PROFILE", None)
    env.pop("JS_EXEC_PROFILE_OUT", None)
    env["LAMBDA_BENCH_PROFILE_CHECK"] = "passed" if not args.skip_profile_check else "skipped"
    env["LAMBDA_BENCH_POWER_CHECK"] = power_state
    env["LAMBDA_BENCH_LOG_DIR"] = log_dir
    env["LAMBDA_BENCH_ARCHIVE"] = cache_info["path"]
    env["LAMBDA_BENCH_ARCHIVE_SIZE_BYTES"] = str(cache_info["size_bytes"])
    env["LAMBDA_BENCH_ARCHIVE_SHA256"] = cache_info["sha256"]
    env["LAMBDA_BENCH_TEST262_RESULT"] = json.dumps(test262_result, separators=(",", ":"))
    run_command(benchmark_cmd, env=env, log_path=os.path.join(log_dir, "benchmark.log"))

    if report_cmd:
        run_command(report_cmd, log_path=os.path.join(log_dir, "report.log"))


if __name__ == "__main__":
    main()
