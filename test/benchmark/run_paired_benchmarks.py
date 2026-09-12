#!/usr/bin/env python3
"""Interleaved A/B runner for matched Lambda release binaries.

Each pair runs the same script once with the control binary and once
with the candidate binary.  The order alternates by pair so host drift is not
systematically assigned to one binary.  Timing samples and observable stdout
digests are retained in a JSON artifact under ``temp/``.

Both Lambda MIR and LambdaJS use this causal gate; the normal benchmark matrix
remains the publication snapshot runner.
"""

import argparse
import datetime
import hashlib
import json
import math
import os
import platform
import random
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
    make_jetstream_ljs_wrapper,
    mir_script_variants,
    parse_timing,
)


TIMING_LINE = "__TIMING__:"
DEFAULT_BOOTSTRAP_RESAMPLES = 10000
DEFAULT_BOOTSTRAP_SEED = 260026


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


def run_once(binary, script, timeout_s, language="lambda", tier="jit",
             expected_stdout_text=None):
    """Run one script and return a serializable timing/observable record."""
    command = [binary, "js" if language == "js" else "run", script]
    environment = os.environ.copy()
    if language == "lambda":
        environment["LAMBDA_TIER"] = tier
    started = time.perf_counter_ns()
    process = None
    try:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=(os.name != "nt"),
            env=environment,
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
                "stdout_contains_expected": None,
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
            "stdout_contains_expected": None,
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
        "stdout_contains_expected": (
            expected_stdout_text in stable_stdout
            if expected_stdout_text is not None else None
        ),
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


def paired_ratio_bootstrap(valid_pairs, resamples, seed):
    """One-sided 95% paired-bootstrap bound for the ratio of medians."""
    observations = [
        (pair["control"]["exec_ms"], pair["candidate"]["exec_ms"])
        for pair in valid_pairs
    ]
    if not observations:
        return None
    rng = random.Random(seed)
    ratios = []
    count = len(observations)
    for _ in range(resamples):
        sampled = [observations[rng.randrange(count)] for _ in range(count)]
        control_median = median([pair[0] for pair in sampled])
        candidate_median = median([pair[1] for pair in sampled])
        if control_median and candidate_median is not None:
            ratios.append(candidate_median / control_median)
    if not ratios:
        return None
    ratios.sort()
    upper_index = max(0, math.ceil(0.95 * len(ratios)) - 1)
    return {
        "method": "paired_bootstrap_ratio_of_medians",
        "confidence": 0.95,
        "tail": "one_sided_upper",
        "seed": seed,
        "resamples_requested": resamples,
        "resamples_valid": len(ratios),
        "upper_bound": ratios[upper_index],
    }


def compare_row(control, candidate, control_script, candidate_script, pairs, timeout_s,
                language="lambda", tier="jit", expected_stdout_text=None,
                bootstrap_resamples=DEFAULT_BOOTSTRAP_RESAMPLES,
                bootstrap_seed=DEFAULT_BOOTSTRAP_SEED):
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
        first_script = control_script if first_label == "control" else candidate_script
        second_script = candidate_script if second_label == "candidate" else control_script
        first = run_once(first_binary, first_script, timeout_s, language, tier,
                         expected_stdout_text)
        second = run_once(second_binary, second_script, timeout_s, language, tier,
                          expected_stdout_text)
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
            "expected_stdout_matches": (
                samples["control"].get("stdout_contains_expected") is not False
                and samples["candidate"].get("stdout_contains_expected") is not False
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
    uncertainty = paired_ratio_bootstrap(valid_pairs, bootstrap_resamples,
                                         bootstrap_seed)
    return {
        "control_script": control_script,
        "candidate_script": candidate_script,
        "pairs_requested": pairs,
        "pairs_valid": len(valid_pairs),
        "timeout_s": timeout_s,
        "control": control_summary,
        "candidate": candidate_summary,
        "candidate_over_control_median_ratio": ratio,
        "candidate_over_control_paired_uncertainty": uncertainty,
        "candidate_wins": candidate_wins,
        "stdout_equal_all": bool(pair_records) and all(pair["stdout_equal"] for pair in pair_records),
        "expected_stdout_matches_all": (
            bool(pair_records) and all(pair["expected_stdout_matches"] for pair in pair_records)
            if expected_stdout_text is not None else None
        ),
        "pairs": pair_records,
    }


def tree_sha256(root):
    """Hash every regular file below root, including its relative path."""
    digest = hashlib.sha256()
    root = os.path.abspath(root)
    for directory, subdirs, filenames in os.walk(root):
        subdirs.sort()
        for filename in sorted(filenames):
            path = os.path.join(directory, filename)
            if not os.path.isfile(path):
                continue
            relative = os.path.relpath(path, root).replace(os.sep, "/")
            digest.update(relative.encode("utf-8"))
            digest.update(b"\0")
            with open(path, "rb") as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(block)
    return digest.hexdigest()


def source_provenance(script, source_root=None):
    """Record entry and transitive-source-tree identities for a timed script."""
    script = os.path.abspath(script)
    source_root = os.path.abspath(source_root or os.path.dirname(script))
    if not os.path.isfile(script):
        raise ValueError(f"source script does not exist: {script}")
    if not os.path.isdir(source_root):
        raise ValueError(f"source root does not exist: {source_root}")
    return {
        "path": script,
        "sha256": sha256_file(script),
        "source_root": source_root,
        "source_tree_sha256": tree_sha256(source_root),
    }


def load_manifest_rows(path):
    """Load explicit source pairs without silently falling back to benchmark discovery."""
    with open(path) as stream:
        manifest = json.load(stream)
    rows = manifest.get("rows")
    if not isinstance(rows, list) or not rows:
        raise ValueError("manifest must contain a non-empty rows array")
    loaded = []
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"manifest row {index + 1} is not an object")
        control_script = row.get("control_script", row.get("script"))
        candidate_script = row.get("candidate_script", row.get("script"))
        if not isinstance(control_script, str) or not isinstance(candidate_script, str):
            raise ValueError(f"manifest row {index + 1} needs script or both source-pair paths")
        loaded.append({
            "suite": row.get("suite", "manifest"),
            "name": row.get("name", f"row_{index + 1}"),
            "variant": row.get("variant", "typed"),
            "control_script": control_script,
            "candidate_script": candidate_script,
            "control_source_root": row.get("control_source_root", row.get("source_root")),
            "candidate_source_root": row.get("candidate_source_root", row.get("source_root")),
            "expected_stdout_text": row.get("expected_stdout_text"),
            "manifest_entry_sha256": row.get("script_sha256"),
        })
    return manifest, loaded


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
    parser.add_argument("--language", choices=["lambda", "js"], default="lambda",
                        help="source language (default: lambda)")
    parser.add_argument("-s", "--suite", default=None, help="comma-separated suite filter")
    parser.add_argument("-b", "--bench", default=None, help="comma-separated benchmark filter")
    parser.add_argument(
        "--variants", choices=["untyped", "typed", "both"], default="both",
        help="Lambda script variants to compare (default: both)",
    )
    parser.add_argument("--tier", choices=["jit", "auto"], default="jit",
                        help="Lambda execution tier for Lambda sources (default: jit)")
    parser.add_argument("--manifest", default=None,
                        help="JSON file containing explicit same-source or source-pair rows")
    parser.add_argument("--source-pair", nargs=2, action="append",
                        metavar=("CONTROL_SCRIPT", "CANDIDATE_SCRIPT"),
                        help="compare two explicit source files; may be given more than once")
    parser.add_argument("-p", "--pairs", type=int, default=41, help="alternating pairs per row")
    parser.add_argument("--bootstrap-resamples", type=int,
                        default=DEFAULT_BOOTSTRAP_RESAMPLES,
                        help="paired bootstrap resamples per row (default: 10000)")
    parser.add_argument("--bootstrap-seed", type=int, default=DEFAULT_BOOTSTRAP_SEED,
                        help="fixed paired bootstrap seed (default: 260026)")
    parser.add_argument("-t", "--timeout", type=int, default=120, help="timeout per process")
    parser.add_argument(
        "-o", "--output", default=None,
        help="JSON artifact (default: temp/paired_benchmarks_<timestamp>.json)",
    )
    args = parser.parse_args()
    if args.pairs < 1:
        parser.error("--pairs must be positive")
    if args.bootstrap_resamples < 1:
        parser.error("--bootstrap-resamples must be positive")

    control = os.path.abspath(args.control)
    candidate = os.path.abspath(args.candidate)
    for label, path in (("control", control), ("candidate", candidate)):
        if not os.path.isfile(path) or not os.access(path, os.X_OK):
            parser.error(f"{label} binary is not executable: {path}")
    if args.manifest and args.source_pair:
        parser.error("--manifest and --source-pair cannot be combined")

    suite_filters = parse_filters(args.suite)
    bench_filters = parse_filters(args.bench)
    manifest = None
    explicit_rows = []
    if args.manifest:
        manifest_path = os.path.abspath(args.manifest)
        try:
            manifest, explicit_rows = load_manifest_rows(manifest_path)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            parser.error(f"invalid manifest: {error}")
    elif args.source_pair:
        for index, pair in enumerate(args.source_pair):
            explicit_rows.append({
                "suite": "source_pair",
                "name": f"pair_{index + 1}",
                "variant": "source_pair",
                "control_script": pair[0],
                "candidate_script": pair[1],
                "control_source_root": None,
                "candidate_source_root": None,
                "expected_stdout_text": None,
                "manifest_entry_sha256": None,
            })
    if explicit_rows and (suite_filters or bench_filters):
        explicit_rows = [
            row for row in explicit_rows
            if (not suite_filters or row["suite"] in suite_filters) and
            (not bench_filters or row["name"] in bench_filters)
        ]
        if not explicit_rows:
            parser.error("no explicit rows matched the supplied filters")
    benchmarks = []
    if not explicit_rows:
        benchmarks = build_benchmark_list(suite_filters, bench_filters)
        if not benchmarks:
            parser.error("no benchmarks matched the supplied filters")

    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    output = args.output or os.path.join("temp", f"paired_benchmarks_{timestamp}.json")
    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
    variants = ["untyped", "typed"] if args.variants == "both" else [args.variants]
    if args.language == "js":
        variants = ["js"]
    if explicit_rows:
        variants = sorted({row["variant"] for row in explicit_rows})
    metadata = {
        "schema_version": 3,
        "started_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "platform": f"{platform.system()} {platform.machine()}",
        "control": {"path": control, "sha256": sha256_file(control)},
        "candidate": {"path": candidate, "sha256": sha256_file(candidate)},
        "git_commit": command_output(["git", "rev-parse", "HEAD"]),
        "power_state": command_output(["pmset", "-g", "batt"]) if platform.system() == "Darwin" else None,
        "suite_filters": suite_filters or [],
        "bench_filters": bench_filters or [],
        "variants": variants,
        "language": args.language,
        "tier": args.tier,
        "pairs": args.pairs,
        "paired_uncertainty": {
            "method": "paired_bootstrap_ratio_of_medians",
            "confidence": 0.95,
            "tail": "one_sided_upper",
            "resamples": args.bootstrap_resamples,
            "seed": args.bootstrap_seed,
        },
        "timeout_s": args.timeout,
        "command": " ".join(sys.argv),
    }
    if args.manifest:
        metadata["manifest"] = {
            "path": manifest_path,
            "sha256": sha256_file(manifest_path),
            "schema_version": manifest.get("schema_version"),
            "metadata": manifest.get("metadata"),
        }
    artifact = {"_metadata": metadata, "rows": []}
    if explicit_rows:
        print(f"Paired A/B: {len(explicit_rows)} explicit row(s), {args.pairs} pair(s)")
        for spec in explicit_rows:
            control_script = os.path.abspath(spec["control_script"])
            candidate_script = os.path.abspath(spec["candidate_script"])
            row = {
                "suite": spec["suite"],
                "name": spec["name"],
                "variant": spec["variant"],
                "control_script": control_script,
                "candidate_script": candidate_script,
                "expected_stdout_text": spec["expected_stdout_text"],
            }
            try:
                row["control_source"] = source_provenance(
                    control_script, spec["control_source_root"])
                row["candidate_source"] = source_provenance(
                    candidate_script, spec["candidate_source_root"])
            except ValueError as error:
                row["status"] = "missing_script"
                row["status_detail"] = str(error)
                artifact["rows"].append(row)
                print(f"\n{spec['suite']}/{spec['name']} missing script")
                continue
            expected_sha256 = spec["manifest_entry_sha256"]
            if expected_sha256 is not None:
                row["manifest_entry_sha256"] = expected_sha256
                row["manifest_entry_hash_matches"] = (
                    row["control_source"]["sha256"] == expected_sha256 and
                    row["candidate_source"]["sha256"] == expected_sha256
                )
                if not row["manifest_entry_hash_matches"]:
                    row["status"] = "source_hash_mismatch"
                    artifact["rows"].append(row)
                    print(f"\n{spec['suite']}/{spec['name']} source hash mismatch")
                    continue
            print(f"\n{spec['suite']}/{spec['name']}[{spec['variant']}] ",
                  end="", flush=True)
            row.update(compare_row(control, candidate, control_script, candidate_script,
                                   args.pairs, args.timeout, args.language, args.tier,
                                   spec["expected_stdout_text"],
                                   args.bootstrap_resamples, args.bootstrap_seed))
            row["status"] = "ok" if row["pairs_valid"] == args.pairs else "partial_ok"
            if row["expected_stdout_matches_all"] is False:
                row["status"] = "wrong_output"
            artifact["rows"].append(row)
            ratio = row["candidate_over_control_median_ratio"]
            ratio_text = "n/a" if ratio is None else f"{ratio:.4f}"
            print(f" ratio={ratio_text} wins={row['candidate_wins']}/{row['pairs_valid']}"
                  f" stdout_equal={row['stdout_equal_all']}")
        artifact["_metadata"]["finished_at"] = datetime.datetime.now().isoformat(
            timespec="seconds")
        with open(output, "w") as stream:
            json.dump(artifact, stream, indent=2)
        print(f"Saved paired artifact to {output}")
        return

    print(f"Paired A/B: {len(benchmarks)} row(s), {args.pairs} pair(s), variants={','.join(variants)}")
    for benchmark in benchmarks:
        untyped, typed = mir_script_variants(benchmark)
        scripts = {"untyped": untyped, "typed": typed}
        if args.language == "js":
            script = benchmark["js_path"]
            if benchmark["is_jetstream"] and benchmark["ref_js"]:
                script = make_jetstream_ljs_wrapper(benchmark["name"], benchmark["ref_js"])
            elif benchmark["suite"] == "awfy" and script:
                bundle = script.replace("2.js", "2_bundle.js")
                if os.path.exists(bundle):
                    script = bundle
            scripts = {"js": script}
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
            row["control_source"] = source_provenance(script)
            row["candidate_source"] = source_provenance(script)
            row.update(compare_row(control, candidate, script, script, args.pairs,
                                   args.timeout, args.language, args.tier, None,
                                   args.bootstrap_resamples, args.bootstrap_seed))
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
