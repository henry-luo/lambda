#!/usr/bin/env python3
"""Focused release-only Lambda MIR MVP/C2MIR comparison; retain every sample."""

import argparse
import datetime
import json
import math
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess

from benchmark_provenance import command_output, hashed_file_corpus, sha256_file
from run_c2mir_benchmarks import C2M, TIMER_MAIN, build_command, ensure_c2m, parse_timing, strip_timing
from run_paired_benchmarks import paired_ratio_bootstrap

ROOT = Path(__file__).resolve().parents[2]
CASES = (("r7rs", "fib"), ("r7rs", "sum"), ("r7rs", "sumfp"), ("awfy", "mandelbrot"))


def compiler_identity():
    """Identify the experiment's sources alongside the measured executable hashes."""
    paths = list((ROOT / "lambda/runtime/mvp").glob("*.hpp"))
    paths += list((ROOT / "lambda/runtime/mvp").glob("*.cpp"))
    paths += [ROOT / path for path in (
        "build_lambda_config.json", "lambda/runtime/runner.cpp",
        "lambda/runtime/transpile-mir.cpp", "lambda/runtime/mir_emitter_shared.hpp",
        "lambda/runtime/mir_loop_analysis.hpp", "lambda/runtime/mir_bitwise.hpp",
        "lambda/runtime/type_contract.cpp", "lambda/runtime/type_contract.hpp",
        "lambda/mir/VENDOR.md")]
    return {"experiment_sources": hashed_file_corpus(paths, ROOT),
            "compiler": command_output(["c++", "--version"]),
            "cpu_probe": command_output(["sysctl", "-n", "machdep.cpu.brand_string"])
                   if platform.system() == "Darwin" else platform.processor()}


def invoke(command, env):
    result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(f"{command}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "lambda.exe")
    parser.add_argument("--control", type=Path, help="archived production release; defaults to --exe in production mode")
    parser.add_argument("--previous-mvp", type=Path, help="optional prior MVP release for an interleaved before/after comparison")
    parser.add_argument("--legacy-timer", action="store_true", help="use the historical C adapter that also times output")
    parser.add_argument("--runs", type=int, default=15)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--output", type=Path, default=ROOT / "temp/lambda_mir_mvp/results.json")
    parser.add_argument("--release", action="store_true", required=True, help="confirm both Lambda binaries were built in release mode")
    args = parser.parse_args()
    if args.runs < 3 or args.warmups < 1:
        parser.error("use at least three measured runs and one warmup")
    error = ensure_c2m()
    if error:
        raise RuntimeError(error)
    executable = args.exe.resolve()
    control = (args.control or executable).resolve()
    binaries = {"mvp": executable, "production": control, "c2mir": C2M}
    if args.previous_mvp:
        binaries["previous_mvp"] = args.previous_mvp.resolve()
    base = os.environ.copy()
    for key in tuple(base):
        if key.startswith(("LAMBDA_", "MIR_")):
            del base[key]
    base["LAMBDA_EXEC_BACKEND"] = "jit"
    base.pop("JS_MIR_INTERP", None)
    report = {
        "schema": 2, "date": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": platform.platform(), "machine": platform.machine(),
        "revision": command_output(["git", "rev-parse", "HEAD"]),
        "working_tree": command_output(["git", "status", "--short"]),
        "release_confirmed": args.release,
        "compiler_identity": compiler_identity(),
        "executables": {name: {"path": str(path), "sha256": sha256_file(path)}
                        for name, path in binaries.items()},
        "timer_sha256": sha256_file(TIMER_MAIN),
        "timer_sources": hashed_file_corpus([TIMER_MAIN, ROOT / "test/benchmark/c2mir/bench_clock.h",
                                               ROOT / "test/benchmark/c2mir/bench_kernel_timer.h"], ROOT),
        "runner_sources": hashed_file_corpus([Path(__file__), ROOT / "test/benchmark/run_paired_benchmarks.py",
                                                ROOT / "test/benchmark/benchmark_provenance.py"], ROOT),
        "timer_scope": "historical C main including validation/output" if args.legacy_timer else
                       "kernel execution only for all engines; excludes result validation/output and compilation",
        "runs": args.runs, "warmups": args.warmups, "cases": [],
    }
    rng = random.Random(1101)
    for suite, name in CASES:
        source = ROOT / "test/benchmark" / suite / f"{name}2.ls"
        c_source = source.parent / "c2mir" / f"{name}.c"
        expected = source.with_suffix(".txt").read_text().strip()
        commands = {
            "mvp": [str(executable), "run", str(source), "--no-log"],
            "production": [str(control), "run", str(source), "--no-log"],
            "c2mir": build_command(c_source) if args.legacy_timer else
                     [str(C2M), "-DC2MIR_KERNEL_TIMING", str(c_source), "-eg"],
        }
        if args.previous_mvp:
            commands["previous_mvp"] = [str(binaries["previous_mvp"]), "run", str(source), "--no-log"]
        samples = {engine: [] for engine in commands}
        orders = []
        for iteration in range(-args.warmups, args.runs):
            order = list(commands)
            rng.shuffle(order)
            if iteration >= 0:
                orders.append(order)
            for engine in order:
                env = {**base, "LAMBDA_MIR_COMPILER": "mvp" if engine in ("mvp", "previous_mvp") else "production"}
                stdout = invoke(commands[engine], env)
                if strip_timing(stdout) != expected:
                    raise RuntimeError(f"{suite}/{name} {engine}: output differs from {source.with_suffix('.txt')}: {stdout!r}")
                elapsed = parse_timing(stdout)
                if elapsed is None or elapsed <= 0 or not math.isfinite(elapsed):
                    raise RuntimeError(f"{suite}/{name} {engine}: invalid timer {elapsed}")
                if iteration >= 0:
                    samples[engine].append(elapsed)
        medians = {engine: statistics.median(values) for engine, values in samples.items()}
        row = {"name": f"{suite}/{name}", "lambda_sha256": sha256_file(source),
               "c_sha256": sha256_file(c_source), "golden_sha256": sha256_file(source.with_suffix('.txt')),
               "commands": commands, "samples_ms": samples, "median_ms": medians,
               "execution_order": orders,
               "mvp_over_c2mir": medians["mvp"] / medians["c2mir"],
               "mvp_over_production": medians["mvp"] / medians["production"]}
        row["uncertainty"] = {}
        for engine in samples:
            if engine == "mvp":
                continue
            pairs = [{"control": {"exec_ms": a}, "candidate": {"exec_ms": b}}
                     for a, b in zip(samples[engine], samples["mvp"])]
            row["uncertainty"][f"mvp_over_{engine}"] = paired_ratio_bootstrap(pairs, 10000, 1101)
        if args.previous_mvp:
            row["mvp_over_previous"] = medians["mvp"] / medians["previous_mvp"]
        report["cases"].append(row)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        print(f"{row['name']}: MVP={medians['mvp']:.3f}ms, production={medians['production']:.3f}ms, "
              f"C2MIR={medians['c2mir']:.3f}ms, ratio={row['mvp_over_c2mir']:.2f}x", flush=True)
    for engine in binaries:
        if engine != "mvp":
            report[f"geomean_mvp_over_{engine}"] = statistics.geometric_mean(
                row["median_ms"]["mvp"] / row["median_ms"][engine] for row in report["cases"])
    report["qualification"] = "scalar subset only; eight-row milestone remains incomplete" + (
        "; historical timer scopes differ" if args.legacy_timer else "; matched kernel boundaries, paired bootstrap per row")
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
