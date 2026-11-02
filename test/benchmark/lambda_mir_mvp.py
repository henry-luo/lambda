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
SCALAR_CASES = (("r7rs", "fib"), ("r7rs", "sum"), ("r7rs", "sumfp"), ("awfy", "mandelbrot"))
FLOAT_ARRAY_CASES = (("beng", "spectralnorm"), ("awfy", "nbody"))
CORE_ARRAY_CASES = (("larceny", "array1"), ("awfy", "sieve")) + FLOAT_ARRAY_CASES
RECURSION_CASES = (("r7rs", "tak"), ("r7rs", "ack"))
EXPANSION_ARRAY_CASES = (("larceny", "quicksort"), ("kostya", "matmul"))
EXPANSION_CASES = RECURSION_CASES + EXPANSION_ARRAY_CASES + (("kostya", "collatz"),)
EXTENDED_ARRAY_CASES = (("r7rs", "fft"), ("r7rs", "nqueens"), ("awfy", "bounce"),
                        ("awfy", "queens"), ("beng", "fannkuch"), ("kostya", "primes"),
                        ("kostya", "levenshtein"), ("kostya", "brainfuck"),
                        ("larceny", "paraffins"), ("larceny", "pnpoly"), ("larceny", "ray"))
EXTENDED_CASES = (("r7rs", "fibfp"),) + EXTENDED_ARRAY_CASES + (
    ("beng", "mandelbrot"), ("kostya", "json_gen"), ("larceny", "divrec"))
ARRAY_CASES = CORE_ARRAY_CASES + EXPANSION_ARRAY_CASES + EXTENDED_ARRAY_CASES
GC_CASES = ARRAY_CASES + (("kostya", "json_gen"),)
CORE_CASES = SCALAR_CASES + CORE_ARRAY_CASES
CASES = CORE_CASES + EXPANSION_CASES + EXTENDED_CASES
TIMED_OUTPUT_CASES = (("beng", "spectralnorm"), ("beng", "fannkuch"), ("beng", "mandelbrot"),
                      ("kostya", "levenshtein"), ("larceny", "pnpoly"))


def compiler_identity():
    """Identify the experiment's sources alongside the measured executable hashes."""
    paths = list((ROOT / "lambda/runtime/mvp").glob("*.hpp"))
    paths += list((ROOT / "lambda/runtime/mvp").glob("*.cpp"))
    paths += [ROOT / path for path in (
        "build_lambda_config.json", "lib/math_checked.hpp", "lambda/runtime/runner.cpp",
        "lambda/lambda.h", "lambda/lambda-data.hpp", "lambda/runtime/lambda-data-runtime.cpp",
        "lambda/core/print.h", "lambda/core/print.cpp",
        "lambda/runtime/heap_api.h", "lambda/runtime/lambda-mem.cpp",
        "lambda/runtime/gc/gc_object_zone.h", "lambda/runtime/gc/gc_object_zone.c",
        "lambda/runtime/gc/gc_heap.c", "lambda/runtime/lambda-root-frame.hpp",
        "lambda/io/mark_builder.hpp", "lambda/io/mark_builder.cpp",
        "lambda/runtime/ast-core.cpp", "lambda/runtime/ast-core.hpp", "lambda/runtime/build_ast.cpp",
        "lambda/runtime/transpile-mir.cpp", "lambda/runtime/mir_emitter_shared.hpp",
        "lambda/runtime/mir_loop_analysis.hpp", "lambda/runtime/mir_bitwise.hpp",
        "lambda/runtime/mir_container_access.hpp",
        "lambda/runtime/lambda-vector.cpp", "lambda/runtime/lambda-eval.cpp",
        "lambda/runtime/sys_func_registry.c", "lambda/runtime/sys_func_registry.h",
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


def sumfp_batch_sources(source, c_source, count):
    """Retain both original kernels; consume every result in the same timed batch."""
    scratch = ROOT / "temp/lambda_mir_mvp" / f"sumfp_batch_{count}"
    scratch.mkdir(parents=True, exist_ok=True)
    lambda_text = source.read_text().split("pn main()", 1)[0] + f'''pn main() {{
    let t0 = clock()
    var total: float = 0.0
    var i: int = 0
    while (i < {count}) {{ total = total + benchmark(); i = i + 1 }}
    let elapsed = (clock() - t0) * 1000.0
    if (abs(total - 5000050000.0 * {count}.0) < 1.0) {{ print("sumfp: PASS\\n") }}
    else {{ print("sumfp: FAIL\\n") }}
    print("__TIMING__:" ++ elapsed ++ "\\n")
}}
'''
    header = ROOT / "test/benchmark/c2mir/bench_kernel_timer.h"
    c_text = c_source.read_text().split("int main(void)", 1)[0].replace(
        '"../../c2mir/bench_kernel_timer.h"', f'"{header}"') + f'''int main(void) {{
    BENCH_KERNEL_BEGIN();
    double total = 0.0;
    long i;
    for (i = 0; i < {count}; i++) total += run_sumfp(100000.0);
    BENCH_KERNEL_END();
    double diff = total - 5000050000.0 * {count}.0;
    if (diff < 0.0) diff = -diff;
    printf(diff < 1.0 ? "sumfp: PASS\\n" : "sumfp: FAIL\\n");
    BENCH_KERNEL_REPORT();
    return diff >= 1.0;
}}
'''
    derived_lambda, derived_c = scratch / "sumfp.ls", scratch / "sumfp.c"
    derived_lambda.write_text(lambda_text)
    derived_lambda.with_suffix(".txt").write_text(source.with_suffix(".txt").read_text())
    derived_c.write_text(c_text)
    metadata = {"count": count, "lambda_sha256": sha256_file(derived_lambda),
                "c_sha256": sha256_file(derived_c), "lambda_source": lambda_text, "c_source": c_text,
                "timer_scope": "repeated original kernels plus loop and consumed-result accumulation; reporting excluded"}
    return derived_lambda, derived_c, metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "lambda.exe")
    parser.add_argument("--control", type=Path, help="archived production release; defaults to --exe in production mode")
    parser.add_argument("--previous-mvp", type=Path, help="optional prior MVP release for an interleaved before/after comparison")
    parser.add_argument("--legacy-timer", action="store_true", help="use the historical C adapter that also times output")
    parser.add_argument("--runs", type=int, default=15)
    parser.add_argument("--c2mir-allowance-percent", type=float, default=20.0,
                        help="maximum slowdown against C2MIR, using the one-sided 95%% upper ratio bound")
    subsets = {"scalar": SCALAR_CASES, "array": ARRAY_CASES, "float-array": FLOAT_ARRAY_CASES,
               "core": CORE_CASES, "expansion": EXPANSION_CASES, "extended": EXTENDED_CASES, "all": CASES}
    parser.add_argument("--subset", choices=tuple(subsets), default="all")
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--case", choices=[f"{suite}/{name}" for suite, name in CASES],
                        help="select one case within the subset")
    parser.add_argument("--sumfp-batch", type=int, default=1,
                        help="supplemental matched Sumfp batch for sub-resolution single-run timings")
    parser.add_argument("--output", type=Path, default=ROOT / "temp/lambda_mir_mvp/results.json")
    parser.add_argument("--release", action="store_true", required=True, help="confirm both Lambda binaries were built in release mode")
    args = parser.parse_args()
    if args.runs < 3 or args.warmups < 1:
        parser.error("use at least three measured runs and one warmup")
    if not math.isfinite(args.c2mir_allowance_percent) or args.c2mir_allowance_percent < 0:
        parser.error("C2MIR allowance must be a finite nonnegative percentage")
    if args.sumfp_batch < 1 or args.sumfp_batch > 100000:
        parser.error("use a Sumfp batch count from 1 to 100000")
    if args.sumfp_batch > 1 and (args.case != "r7rs/sumfp" or args.legacy_timer):
        parser.error("batched timing requires --case r7rs/sumfp and matched kernel timers")
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
        "c2mir_allowance_percent": args.c2mir_allowance_percent,
        "compiler_identity": compiler_identity(),
        "executables": {name: {"path": str(path), "sha256": sha256_file(path)}
                        for name, path in binaries.items()},
        "timer_sha256": sha256_file(TIMER_MAIN),
        "timer_sources": hashed_file_corpus([TIMER_MAIN, ROOT / "test/benchmark/c2mir/bench_clock.h",
                                               ROOT / "test/benchmark/c2mir/bench_kernel_timer.h"], ROOT),
        "reference_helpers": hashed_file_corpus([ROOT / "test/benchmark/c2mir/bench_lcg.h"], ROOT),
        "runner_sources": hashed_file_corpus([Path(__file__), ROOT / "test/benchmark/run_paired_benchmarks.py",
                                                ROOT / "test/benchmark/run_c2mir_benchmarks.py",
                                                ROOT / "test/benchmark/benchmark_provenance.py"], ROOT),
        "timer_scope": "historical C main including validation/output" if args.legacy_timer else
                       "matched per-row execution boundaries; compilation excluded; see each row for timed output",
        "runs": args.runs, "warmups": args.warmups, "cases": [],
    }
    rng = random.Random(1101)
    cases = subsets[args.subset]
    if args.case:
        cases = tuple(case for case in cases if "/".join(case) == args.case)
        if not cases:
            parser.error("selected case is outside the subset")
    for suite, name in cases:
        source = ROOT / "test/benchmark" / suite / f"{name}2.ls"
        c_source = source.parent / "c2mir" / f"{name}.c"
        expected = source.with_suffix(".txt").read_text().strip()
        run_source, run_c_source, batch = source, c_source, None
        if args.sumfp_batch > 1:
            run_source, run_c_source, batch = sumfp_batch_sources(source, c_source, args.sumfp_batch)
        commands = {
            "mvp": [str(executable), "run", str(run_source), "--no-log"],
            "production": [str(control), "run", str(run_source), "--no-log"],
            "c2mir": build_command(run_c_source) if args.legacy_timer else
                     [str(C2M), "-DC2MIR_KERNEL_TIMING", str(run_c_source), "-eg"],
        }
        if args.previous_mvp:
            commands["previous_mvp"] = [str(binaries["previous_mvp"]), "run", str(run_source), "--no-log"]
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
                if elapsed is None or elapsed < 0 or not math.isfinite(elapsed):
                    raise RuntimeError(f"{suite}/{name} {engine}: invalid timer {elapsed}")
                if iteration >= 0:
                    samples[engine].append(elapsed)
        medians = {engine: statistics.median(values) for engine, values in samples.items()}
        row = {"name": f"{suite}/{name}", "lambda_sha256": sha256_file(source),
               "timer_scope": "C main including final reporting; Lambda uses its script clock pair" if args.legacy_timer else
                   "workload plus formatting/output (pnpoly excludes final DONE line)" if (suite, name) in TIMED_OUTPUT_CASES else
                   "setup, sort and sortedness check; final reporting excluded" if name == "quicksort" else
                   "workload including setup; final reporting excluded",
               "c_sha256": sha256_file(c_source), "golden_sha256": sha256_file(source.with_suffix('.txt')),
               "commands": commands, "samples_ms": samples, "median_ms": medians,
               "execution_order": orders,
               "mvp_over_c2mir": medians["mvp"] / medians["c2mir"] if medians["c2mir"] else None,
               "mvp_over_production": medians["mvp"] / medians["production"] if medians["production"] else None,
               "timer_resolution_limited": {engine: any(value == 0 for value in values)
                                            for engine, values in samples.items()}}
        row["uncertainty"] = {}
        if batch:
            row["derived_workload"] = batch
            row["timer_scope"] = batch["timer_scope"]
        for engine in samples:
            if engine == "mvp":
                continue
            pairs = [{"control": {"exec_ms": a}, "candidate": {"exec_ms": b}}
                     for a, b in zip(samples[engine], samples["mvp"])]
            # A zero is a valid clock observation, not proof of zero runtime.
            # Keep it in the raw samples, but require longer timing before qualification.
            limited = row["timer_resolution_limited"]
            row["uncertainty"][f"mvp_over_{engine}"] = (
                None if limited[engine] or limited["mvp"] else paired_ratio_bootstrap(pairs, 10000, 1101))
        if args.previous_mvp:
            row["mvp_over_previous"] = medians["mvp"] / medians["previous_mvp"] if medians["previous_mvp"] else None
        full = row["uncertainty"]["mvp_over_production"]
        reference = row["uncertainty"]["mvp_over_c2mir"]
        row["performance_gates"] = {
            "faster_than_full_mir": full["upper_bound"] < 1.0 if full else None,
            "within_c2mir_allowance": reference["upper_bound"] <= 1.0 + args.c2mir_allowance_percent / 100.0 if reference else None,
            "within_10_percent_of_c2mir": reference["upper_bound"] <= 1.10 if reference else None}
        report["cases"].append(row)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        ratio = row["mvp_over_c2mir"]
        ratio_text = f"{ratio:.2f}x" if ratio is not None else "unresolved"
        print(f"{row['name']}: MVP={medians['mvp']:.3f}ms, production={medians['production']:.3f}ms, "
              f"C2MIR={medians['c2mir']:.3f}ms, ratio={ratio_text}", flush=True)
    for engine in binaries:
        if engine != "mvp":
            resolved = all(not row["timer_resolution_limited"]["mvp"] and
                           not row["timer_resolution_limited"][engine] for row in report["cases"])
            report[f"geomean_mvp_over_{engine}"] = statistics.geometric_mean(
                row["median_ms"]["mvp"] / row["median_ms"][engine] for row in report["cases"]) if resolved else None
    report["qualification"] = f"{args.subset} subset; coverage and per-row performance qualification are separate" + (
        "; historical timer scopes differ" if args.legacy_timer else "; matched workload boundaries, paired bootstrap per row")
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
