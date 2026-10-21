#!/usr/bin/env python3
"""Build release and measure timeline normalization/layout/updates at fixed sizes."""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "utils"))
from benchmark_process import run_measured

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
        if b"VALIDATOR] validate_against_base_type" in symbols:
            parser.error("a verified release build is required")
        # The shared checkout marker tracks lambda.exe, not a separately captured binary.
        build_description = "captured release executable; debug-trace exclusion verified"
    else:
        with (output / "build.log").open("w") as log:
            subprocess.run(["make", "release"], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        executable = ROOT / "lambda.exe"
        build_description = "make release"
    # Packages load from source at runtime, so the binary hash alone cannot identify a run.
    sources = sorted((ROOT / "lmd/package/chart").rglob("*.ls")) + [
        ROOT / "lmd/package/slide/easing.ls", ROOT / "lmd/package/slide/color.ls",
        ROOT / "test/timeline/_benchmark.ls", pathlib.Path(__file__).resolve(),
        ROOT / "utils/benchmark_process.py"]
    report = {"recorded_at": stamp, "built_with": build_description,
              "git_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "binary_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
              "source_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
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
            with (output / f"{name}.stdout").open("w") as stdout, (output / f"{name}.stderr").open("w") as stderr:
                measured = run_measured(command, cwd=ROOT, env=environment, stdout=stdout,
                                        stderr=stderr, timeout=args.timeout)
            record = {"count": count, "scenario": scenario, "wall_s": measured["wall_seconds"],
                      "status": "timeout" if measured["timed_out"] else "failed",
                      "exit_code": measured["exit_code"]}
            text = (output / f"{name}.stdout").read_text()
            if not measured["timed_out"] and measured["exit_code"] == 0 and "__TIMELINE_BENCH__\n" in text:
                try:
                    metrics = json.loads(text.split("__TIMELINE_BENCH__\n", 1)[1])
                    work = metrics["viewport_work"]
                    if metrics["normalized_count"] == count and len(work) == 8 and all(
                            frame["indexed"] and frame["measured"] > 0 and
                            (scenario != "identical" or frame["candidates"] == count and frame["measured"] == 1)
                            for frame in work):
                        record.update(status="ok", metrics=metrics)
                except (ValueError, KeyError, TypeError):
                    pass
            record["peak_rss_bytes"] = measured["peak_rss_bytes"]
            report["results"].append(record)
            (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
            print(name, record["status"], flush=True)
    print(output / "results.json")
    return 0 if all(row["status"] == "ok" for row in report["results"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
