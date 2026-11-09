#!/usr/bin/env python3
"""Release-only typed/untyped Lambda MIR comparison; retain every sample."""

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
import time

from benchmark_provenance import command_output, hashed_file_corpus, sha256_file
from run_c2mir_benchmarks import C2M, TIMER_MAIN, build_command, ensure_c2m, parse_timing, strip_timing
from run_paired_benchmarks import paired_ratio_bootstrap
from run_benchmarks import STANDARD_SUITES, JETSTREAM_LS, TEXT
from utils.split_paths import project_source_path

ROOT = Path(__file__).resolve().parents[2]


def lambda_golden_path(source):
    """Prefer the complete stdout oracle when a benchmark also prints its result."""
    formatted = source.with_suffix(".formatted.txt")
    return formatted if formatted.is_file() else source.with_suffix(".txt")


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
ORIGINAL_CASES = CORE_CASES + EXPANSION_CASES + EXTENDED_CASES
ADDITIONAL_CASES = (("r7rs", "cpstak"), ("larceny", "diviter"), ("larceny", "primes"),
                    ("larceny", "puzzle"), ("larceny", "triangl"), ("larceny", "gcbench"),
                    ("larceny", "deriv"), ("beng", "nbody"), ("beng", "binarytrees"),
                    ("beng", "fasta"), ("beng", "pidigits"), ("awfy", "list"),
                    ("awfy", "permute"), ("awfy", "storage"), ("awfy", "towers"),
                    ("kostya", "base64"), ("jetstream", "nbody"), ("jetstream", "navier_stokes"),
                    ("jetstream", "cube3d"), ("jetstream", "raytrace3d"))
COVERED_CASES = ORIGINAL_CASES + ADDITIONAL_CASES
REGISTERED_CASES = tuple((suite, entry[0]) for suite, entries in
    STANDARD_SUITES + [("jetstream", JETSTREAM_LS), ("text", TEXT)] for entry in entries)
COMPLETION_CASES = tuple(case for case in REGISTERED_CASES if case not in COVERED_CASES)
CASES = COVERED_CASES + COMPLETION_CASES
TIMED_OUTPUT_CASES = (("beng", "spectralnorm"), ("beng", "fannkuch"), ("beng", "mandelbrot"),
                      ("kostya", "levenshtein"), ("larceny", "pnpoly"),
                      ("beng", "nbody"), ("beng", "binarytrees"), ("beng", "fasta"),
                      ("beng", "pidigits"), ("larceny", "gcbench"))


def compiler_identity():
    """Identify the experiment's sources alongside the measured executable hashes."""
    paths = list((ROOT / "lambda/runtime/mvp").glob("*.hpp"))
    paths += list((ROOT / "lambda/runtime/mvp").glob("*.cpp"))
    # source ownership moved to sibling projects; hash their real sources
    # rather than the retired include-prefix paths (D9.1.1v4).
    paths += [project_source_path(path, ROOT) for path in (
        "build_lambda_config.json", "utils/split_paths.py", "lib/math_checked.hpp", "lambda/runtime/runner.cpp",
        "lambda/lambda.h", "lambda/lambda-data.hpp", "lambda/runtime/lambda-data-runtime.cpp",
        "lambda/runtime/collection_runtime.cpp", "lambda/runtime/vmap.cpp",
        "lambda/runtime/write_set.hpp", "lambda/runtime/write_set.cpp",
        "lambda/runtime/recovery_frame.h", "lambda/runtime/recovery_frame.c",
        "lambda/core/print.h", "lambda/core/print.cpp",
        "lambda/runtime/heap_api.h", "lambda/runtime/lambda-mem.cpp",
        "lambda/runtime/gc/gc_object_zone.h", "lambda/runtime/gc/gc_object_zone.c",
        "lambda/runtime/gc/gc_heap.c", "lambda/runtime/lambda-root-frame.hpp",
        "lambda/io/mark_builder.hpp", "lambda/io/mark_builder.cpp",
        "lambda/runtime/ast-core.cpp", "lambda/runtime/ast-core.hpp", "lambda/runtime/ast.hpp",
        "lambda/runtime/interp.cpp", "lambda/runtime/interp_plan.cpp", "lambda/runtime/build_ast.cpp",
        "lambda/runtime/const_materialize.cpp", "lambda/runtime/mir_emitter_shared.hpp",
        "lambda/runtime/mir_loop_analysis.hpp", "lambda/runtime/mir_cow_analysis.hpp",
        "lambda/runtime/mir_shape_candidates.hpp",
        "lambda/runtime/mir_bitwise.hpp",
        "lambda/runtime/mir_container_access.hpp",
        "lambda/runtime/mir_path.hpp", "lambda/runtime/mir_function_value.hpp",
        "lambda/runtime/lambda-vector.cpp", "lambda/runtime/lambda-eval.cpp",
        "lambda/runtime/lambda-eval-num.cpp", "lambda/runtime/re2_wrapper.cpp",
        "lambda/runtime/re2_wrapper.hpp", "lambda/core/lambda-decimal.cpp",
        "lambda/core/lambda-decimal.hpp",
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


def competing_benchmark_jobs(group=None):
    """Exclude our group; test supervisors remain busy between child bursts."""
    listing = subprocess.run(["ps", "-axo", "pid,pgid,pcpu,args"],
                             capture_output=True, text=True, check=True)
    jobs = []
    for line in listing.stdout.splitlines()[1:]:
        parts = line.strip().split(None, 3)
        if len(parts) != 4:
            continue
        pid, pgid, cpu, command = parts
        if int(pgid) == group or int(pid) == os.getpid():
            continue
        program = Path(command.split()[0]).name
        build = program in ("make", "gmake", "clang", "clang++", "gcc", "g++", "ld")
        suite = any(name in command for name in ("test/test_run.js", "test/interp/tier_sweep.py",
                                                "test/benchmark/test_lambda_mir_mvp.py",
                                                "-m unittest ", "-m pytest "))
        # private release harnesses have different prefixes and can be idle
        # while their next worker starts. CPU thresholds miss those windows.
        suite |= program.endswith("_gtest.exe") or program in ("pytest", "py.test")
        suite |= "test-batch" in command and program.endswith(".exe")
        desktop = (" view " in command or " serve " in command) and float(cpu) < 20.0
        active = program.endswith(".exe") and float(cpu) >= 5.0 and not desktop
        if build or suite or active:
            jobs.append({"pid": int(pid), "cpu": float(cpu), "program": program})
    return jobs


def wait_for_benchmark_idle():
    quiet = 0
    while quiet < 2:
        quiet = 0 if competing_benchmark_jobs(os.getpgrp()) else quiet + 1
        if quiet < 2:
            time.sleep(1)


def sumfp_batch_sources(source, c_source, count, variant="typed"):
    """Retain both original kernels; consume every result in the same timed batch."""
    scratch = ROOT / "temp/lambda_mir_mvp" / f"sumfp_batch_{count}" / variant
    scratch.mkdir(parents=True, exist_ok=True)
    float_annotation, int_annotation = (": float", ": int") if variant == "typed" else ("", "")
    lambda_text = source.read_text().split("pn main()", 1)[0] + f'''pn main() {{
    let t0 = clock()
    var total{float_annotation} = 0.0
    var i{int_annotation} = 0
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
    parser.add_argument("--control", type=Path, help="archived Full MIR release (required unless --regression-only)")
    parser.add_argument("--previous-mvp", type=Path, help="optional prior MVP release for an interleaved before/after comparison")
    parser.add_argument("--c2mir-exe", type=Path, help="archived C2MIR driver; defaults to the shared native driver")
    parser.add_argument("--source-manifest", type=Path,
                        help="archived candidate binary SHA and compiler_identity; verify before using its source identity")
    parser.add_argument("--legacy-timer", action="store_true", help="use the historical C adapter that also times output")
    parser.add_argument("--runs", type=int, default=15)
    parser.add_argument("--c2mir-allowance-percent", type=float, default=20.0,
                        help="maximum slowdown against C2MIR, using the one-sided 95%% upper ratio bound")
    subsets = {"scalar": SCALAR_CASES, "array": ARRAY_CASES, "float-array": FLOAT_ARRAY_CASES,
               "core": CORE_CASES, "expansion": EXPANSION_CASES, "extended": EXTENDED_CASES,
               "original": ORIGINAL_CASES, "additional": ADDITIONAL_CASES, "covered": COVERED_CASES,
               "completion": COMPLETION_CASES, "all": CASES}
    parser.add_argument("--subset", choices=tuple(subsets), default="all")
    parser.add_argument("--variant", choices=("typed", "untyped", "both"), default="typed",
                        help="run canonical name2.ls, name.ls, or both without changing either workload")
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--benchmark-root", type=Path, default=ROOT / "test/benchmark",
                        help="archived benchmark corpus; keeps paired workloads stable during shared edits")
    parser.add_argument("--case", choices=[f"{suite}/{name}" for suite, name in CASES],
                        help="select one case within the subset")
    parser.add_argument("--sumfp-batch", type=int, default=1,
                        help="supplemental matched Sumfp batch for sub-resolution single-run timings")
    parser.add_argument("--output", type=Path, default=ROOT / "temp/lambda_mir_mvp/results.json")
    parser.add_argument("--release", action="store_true", required=True, help="confirm both Lambda binaries were built in release mode")
    parser.add_argument("--record-failures", action="store_true",
                        help="retain failed engines as unqualified rows, continue other engines, and exit nonzero")
    engines = parser.add_mutually_exclusive_group()
    engines.add_argument("--regression-only", action="store_true",
                        help="measure only candidate and --previous-mvp; retain other engines as unmeasured")
    engines.add_argument("--lambda-only", action="store_true",
                        help="measure Lambda engines without requiring or running the C2MIR reference")
    parser.add_argument("--pause-for-jobs", action="store_true",
                        help="wait for competing builds/tests and retry interrupted paired rounds")
    args = parser.parse_args()
    if args.runs < 3 or args.warmups < 1:
        parser.error("use at least three measured runs and one warmup")
    if args.regression_only and not args.previous_mvp:
        parser.error("--regression-only requires --previous-mvp")
    if not args.regression_only and not args.control:
        parser.error("Full MIR is retired; comparisons require an archived --control executable")
    if not math.isfinite(args.c2mir_allowance_percent) or args.c2mir_allowance_percent < 0:
        parser.error("C2MIR allowance must be a finite nonnegative percentage")
    if args.sumfp_batch < 1 or args.sumfp_batch > 100000:
        parser.error("use a Sumfp batch count from 1 to 100000")
    if args.sumfp_batch > 1 and (args.case != "r7rs/sumfp" or args.legacy_timer):
        parser.error("batched timing requires --case r7rs/sumfp and matched kernel timers")
    measure_c2mir = not (args.regression_only or args.lambda_only)
    if not args.c2mir_exe and measure_c2mir:
        error = ensure_c2m()
        if error:
            raise RuntimeError(error)
    reference = (args.c2mir_exe or C2M).resolve()
    if measure_c2mir and not reference.is_file():
        parser.error("C2MIR executable does not exist")
    executable = args.exe.resolve()
    control = (args.control or executable).resolve()
    binaries = {"mvp": executable, "production": control, "c2mir": reference}
    if args.previous_mvp:
        binaries["previous_mvp"] = args.previous_mvp.resolve()
    identity = compiler_identity()
    archived_identity = None
    if args.source_manifest:
        archived_identity = json.loads(args.source_manifest.read_text())
        if archived_identity["binary_sha256"] != sha256_file(executable):
            parser.error("source manifest does not identify the candidate executable")
    base = os.environ.copy()
    for key in tuple(base):
        if key.startswith(("LAMBDA_", "MIR_", "COW_EXEC_")):
            del base[key]
    base["LAMBDA_EXEC_BACKEND"] = "jit"
    base.pop("JS_MIR_INTERP", None)
    report = {
        "schema": 2, "date": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": platform.platform(), "machine": platform.machine(),
        "revision": command_output(["git", "rev-parse", "HEAD"]),
        "working_tree": command_output(["git", "status", "--short"]),
        "release_confirmed": args.release,
        "benchmark_root": str(args.benchmark_root.resolve()),
        "c2mir_allowance_percent": args.c2mir_allowance_percent,
        "compiler_identity": archived_identity["compiler_identity"] if archived_identity else identity,
        "executables": {name: {"path": str(path), "sha256": sha256_file(path) if path.is_file() else None}
                        for name, path in binaries.items()},
        "timer_sha256": sha256_file(TIMER_MAIN),
        "timer_sources": hashed_file_corpus([TIMER_MAIN, ROOT / "test/benchmark/c2mir/bench_clock.h",
                                               ROOT / "test/benchmark/c2mir/bench_kernel_timer.h"], ROOT),
        "reference_helpers": hashed_file_corpus([ROOT / "test/benchmark/c2mir" / name
            for name in ("bench_lcg.h", "nbody_kernel.h", "tree_benchmark.h")], ROOT),
        "runner_sources": hashed_file_corpus([Path(__file__), ROOT / "test/benchmark/run_paired_benchmarks.py",
                                                ROOT / "test/benchmark/run_c2mir_benchmarks.py",
                                                ROOT / "test/benchmark/benchmark_provenance.py"], ROOT),
        "timer_scope": "historical C main including validation/output" if args.legacy_timer else
                       "matched per-row execution boundaries; compilation excluded; see each row for timed output",
        "runs": args.runs, "warmups": args.warmups, "variant": args.variant, "cases": [],
        "pause_for_jobs": args.pause_for_jobs,
    }
    if archived_identity:
        report["observed_checkout_identity"] = identity
        report["candidate_source_manifest"] = {
            "path": str(args.source_manifest.resolve()), "sha256": sha256_file(args.source_manifest)}
    rng = random.Random(1101)
    cases = subsets[args.subset]
    if args.case:
        cases = tuple(case for case in cases if "/".join(case) == args.case)
        if not cases:
            parser.error("selected case is outside the subset")
    variants = ("typed", "untyped") if args.variant == "both" else (args.variant,)
    for suite, name, variant in ((suite, name, variant) for suite, name in cases for variant in variants):
        source = args.benchmark_root.resolve() / suite / f"{name}{'2' if variant == 'typed' else ''}.ls"
        c_source = source.parent / "c2mir" / f"{name}.c"
        golden = lambda_golden_path(source)
        expected = golden.read_text().strip()
        c_expected = source.with_suffix(".txt").read_text().strip()
        run_source, run_c_source, batch = source, c_source, None
        if args.sumfp_batch > 1:
            run_source, run_c_source, batch = sumfp_batch_sources(source, c_source, args.sumfp_batch, variant)
        commands = {
            "mvp": [str(executable), "run", str(run_source), "--no-log"],
            "production": [str(control), "run", str(run_source), "--no-log"],
            "c2mir": build_command(run_c_source) if args.legacy_timer else
                     [str(reference), "-DC2MIR_KERNEL_TIMING", str(run_c_source), "-eg"],
        }
        commands["c2mir"][0] = str(reference)
        unavailable = {}
        if not c_source.is_file():
            del commands["c2mir"]
            unavailable["c2mir"] = "no standalone C port for this suite entry"
        if args.regression_only:
            for engine in ("production", "c2mir"):
                commands.pop(engine, None)
                unavailable[engine] = "not measured in the MVP regression-only campaign"
        elif args.lambda_only:
            commands.pop("c2mir", None)
            unavailable["c2mir"] = "not measured in the Lambda-only campaign"
        if args.previous_mvp:
            commands["previous_mvp"] = [str(binaries["previous_mvp"]), "run", str(run_source), "--no-log"]
        samples = {engine: [] for engine in binaries}
        failures = {}
        orders = []
        discarded = []
        iteration = -args.warmups
        needs_idle = args.pause_for_jobs
        while iteration < args.runs:
            if needs_idle:
                wait_for_benchmark_idle()
                needs_idle = False
            order = [engine for engine in commands if engine not in failures]
            rng.shuffle(order)
            round_samples = {}
            busy = []
            for engine in order:
                env = {**base, "LAMBDA_MIR_COMPILER": "mvp" if engine in ("mvp", "previous_mvp") else "production"}
                try:
                    stdout = invoke(commands[engine], env)
                    oracle = c_expected if engine == "c2mir" else expected
                    if strip_timing(stdout) != oracle:
                        raise RuntimeError(f"output differs from {source.with_suffix('.txt') if engine == 'c2mir' else golden}: {stdout!r}")
                    elapsed = parse_timing(stdout)
                    if elapsed is None or elapsed < 0 or not math.isfinite(elapsed):
                        raise RuntimeError(f"invalid timer {elapsed}")
                except (RuntimeError, subprocess.TimeoutExpired) as error:
                    detail = f"{suite}/{name}/{variant} {engine}: {error}"
                    if not args.record_failures:
                        raise RuntimeError(detail) from error
                    failures[engine] = {"iteration": iteration, "error": detail}
                    continue
                round_samples[engine] = elapsed
                busy = competing_benchmark_jobs(os.getpgrp()) if args.pause_for_jobs else []
                if busy:
                    break
            if busy:
                # discard the whole pair, never just the slower observation.
                discarded.append({"iteration": iteration, "execution_order": order,
                                  "samples_ms": round_samples, "competing_jobs": busy})
                needs_idle = True
                continue
            if iteration >= 0:
                orders.append(order)
                for engine, elapsed in round_samples.items():
                    samples[engine].append(elapsed)
            iteration += 1
        medians = {engine: statistics.median(values) if values and engine not in failures else None
                   for engine, values in samples.items()}
        row = {"name": f"{suite}/{name}", "variant": variant,
               "case_id": f"{suite}/{name}/{variant}", "lambda_sha256": sha256_file(source),
               "timer_scope": "C main including final reporting; Lambda uses its script clock pair" if args.legacy_timer else
                   "setup and workload with intermediate output; final long-lived tree check/output excluded" if name == "gcbench" else
                   "workload plus formatting/output; final DONE line excluded" if (suite, name) == ("larceny", "pnpoly") else
                   "workload plus formatting/output" if (suite, name) in TIMED_OUTPUT_CASES else
                   "setup, sort and sortedness check; final reporting excluded" if name == "quicksort" else
                   "workload including setup; final reporting excluded",
               "c_sha256": sha256_file(c_source) if c_source.is_file() else None,
               "golden_sha256": sha256_file(golden), "golden_path": str(golden),
               "c_golden_sha256": sha256_file(source.with_suffix('.txt')), "engine_unavailable": unavailable,
               "commands": commands, "samples_ms": samples, "median_ms": medians,
               "execution_order": orders, "engine_failures": failures, "discarded_rounds": discarded,
               "mvp_over_c2mir": medians["mvp"] / medians["c2mir"] if medians["mvp"] is not None and medians["c2mir"] else None,
               "mvp_over_production": medians["mvp"] / medians["production"] if medians["mvp"] is not None and medians["production"] else None,
               "timer_resolution_limited": {engine: not values or any(value == 0 for value in values)
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
                None if engine in failures or "mvp" in failures or limited[engine] or limited["mvp"] else
                paired_ratio_bootstrap(pairs, 10000, 1101))
        if args.previous_mvp:
            row["mvp_over_previous"] = medians["mvp"] / medians["previous_mvp"] if medians["mvp"] is not None and medians["previous_mvp"] else None
            previous = row["uncertainty"]["mvp_over_previous_mvp"]
            row["previous_mvp_comparison"] = {
                "statistically_faster": previous["upper_bound"] < 1.0 if previous else None,
                "statistically_slower": previous["two_sided_lower_bound"] > 1.0 if previous else None,
            }
        full = row["uncertainty"]["mvp_over_production"]
        reference_bounds = row["uncertainty"]["mvp_over_c2mir"]
        row["performance_gates"] = {
            "faster_than_full_mir": full["upper_bound"] < 1.0 if full else None,
            "within_c2mir_allowance": reference_bounds["upper_bound"] <= 1.0 + args.c2mir_allowance_percent / 100.0 if reference_bounds else None,
            "within_10_percent_of_c2mir": reference_bounds["upper_bound"] <= 1.10 if reference_bounds else None}
        report["cases"].append(row)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        ratio = row["mvp_over_c2mir"]
        ratio_text = f"{ratio:.2f}x" if ratio is not None else "unresolved"
        timings = ", ".join(f"{engine}={elapsed:.3f}ms" if elapsed is not None else
                            f"{engine}=UNMEASURED" if not measure_c2mir and engine in unavailable else
                            f"{engine}=UNAVAILABLE" if engine in unavailable else f"{engine}=FAILED"
                            for engine, elapsed in medians.items())
        print(f"{row['case_id']}: {timings}, ratio={ratio_text}", flush=True)
    def aggregate(rows):
        result = {}
        for engine in binaries:
            if engine == "mvp":
                continue
            resolved = all("mvp" not in row["engine_failures"] and engine not in row["engine_failures"] and
                           not row["timer_resolution_limited"]["mvp"] and
                           not row["timer_resolution_limited"][engine] for row in rows)
            result[f"geomean_mvp_over_{engine}"] = statistics.geometric_mean(
                row["median_ms"]["mvp"] / row["median_ms"][engine] for row in rows) if rows and resolved else None
        return result
    report["variant_summaries"] = {variant: aggregate([row for row in report["cases"] if row["variant"] == variant])
                                   for variant in variants}
    # keep the legacy typed aggregate, but never mix typed/untyped ratios into one claim.
    report.update(aggregate(report["cases"]) if len(variants) == 1 else
                  {f"geomean_mvp_over_{engine}": None for engine in binaries if engine != "mvp"})
    report["qualification"] = f"{args.subset} subset; coverage and per-row performance qualification are separate" + (
        "; historical timer scopes differ" if args.legacy_timer else "; matched workload boundaries, paired bootstrap per row")
    if args.regression_only:
        report["qualification"] += "; MVP regression comparison only; Full MIR and C2MIR unmeasured"
    elif args.lambda_only:
        report["qualification"] += "; Lambda engines only; C2MIR unmeasured"
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    if any(row["engine_failures"] for row in report["cases"]):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
