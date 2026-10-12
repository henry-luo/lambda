#!/usr/bin/env python3
"""Build release and measure timeline normalization/layout/updates at fixed sizes."""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import platform
import re
import signal
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENARIOS = ["points", "identical", "groups", "rich", "large_years"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--counts", nargs="+", type=int, default=[100, 1000, 10000, 100000])
    parser.add_argument("--scenarios", nargs="+", choices=SCENARIOS, default=SCENARIOS)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--release-executable", type=pathlib.Path,
                        help="use a captured release executable instead of rebuilding the shared checkout")
    args = parser.parse_args()
    if not (ROOT / ".git").is_dir():
        parser.error("run in the main checkout; release builds are forbidden in worktrees")
    if any(count < 100 or count > 100000 for count in args.counts) or args.timeout <= 0:
        parser.error("counts must be 100..100000 and timeout must be positive")
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = ROOT / "temp" / "timeline-benchmarks" / stamp
    output.mkdir(parents=True)
    if args.release_executable:
        executable = args.release_executable.resolve()
        # The debug validator trace is compiled out of release binaries (Developer Guide §7.3).
        symbols = subprocess.run(["strings", str(executable)], capture_output=True, check=True).stdout
        if not (ROOT / ".lambda_release_build").exists() or b"VALIDATOR] validate_against_base_type" in symbols:
            parser.error("a verified release build is required")
        build_description = "captured release executable; release marker and debug-trace exclusion verified"
    else:
        with (output / "build.log").open("w") as log:
            subprocess.run(["make", "release"], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        executable = ROOT / "lambda.exe"
        build_description = "make release"
    report = {"built_with": build_description, "binary_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
              "platform": platform.platform(), "backend": "jit", "results": []}
    environment = {**os.environ, "LAMBDA_EXEC_BACKEND": "jit"}
    module = pathlib.Path(os.path.relpath(ROOT / "test/timeline/_benchmark", output))
    module_import = ".".join("~~" if part == ".." else part for part in module.parts)
    for count in args.counts:
        for scenario in args.scenarios:
            name = f"{scenario}-{count}"
            driver = output / f"{name}.ls"
            # Module paths use Lambda's relative ~~ syntax (S16.9.8).
            driver.write_text("import benchmark: " + module_import +
                              "\npn main() {\n let result=benchmark.run(" + str(count) + "," + json.dumps(scenario) +
                              ");\n print(\"__TIMELINE_BENCH__\\n\",format(result,'json'),\"\\n\")\n}\n")
            command = [str(executable), "run", str(driver), "--no-log"]
            started = time.monotonic()
            with (output / f"{name}.stdout").open("w") as stdout, (output / f"{name}.stderr").open("w") as stderr:
                process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=stdout, stderr=stderr, start_new_session=True)
                timed_out = False
                usage = None
                if hasattr(os, "wait4"):
                    # wait4 measures this child without macOS time(1)'s restricted sysctl.
                    while True:
                        pid, status, child_usage = os.wait4(process.pid, os.WNOHANG)
                        if pid:
                            process.returncode = os.waitstatus_to_exitcode(status)
                            usage = child_usage
                            break
                        if time.monotonic() - started >= args.timeout:
                            timed_out = True
                            os.killpg(process.pid, signal.SIGKILL)
                            _, status, usage = os.wait4(process.pid, 0)
                            process.returncode = os.waitstatus_to_exitcode(status)
                            break
                        time.sleep(0.05)
                else:
                    try:
                        process.wait(timeout=args.timeout)
                    except subprocess.TimeoutExpired:
                        timed_out = True
                        process.kill()
                        process.wait()
            record = {"count": count, "scenario": scenario, "wall_s": time.monotonic() - started,
                      "status": "timeout" if timed_out else "failed", "exit_code": process.returncode}
            text = (output / f"{name}.stdout").read_text()
            if not timed_out and process.returncode == 0 and "__TIMELINE_BENCH__\n" in text:
                try:
                    metrics = json.loads(text.split("__TIMELINE_BENCH__\n", 1)[1])
                    if metrics["normalized_count"] == count:
                        record.update(status="ok", metrics=metrics)
                except (ValueError, KeyError, TypeError):
                    pass
            if usage is not None:
                record["peak_rss_bytes"] = usage.ru_maxrss * (1 if platform.system() == "Darwin" else 1024)
            report["results"].append(record)
            (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
            print(name, record["status"], flush=True)
    print(output / "results.json")
    return 0 if all(row["status"] == "ok" for row in report["results"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
