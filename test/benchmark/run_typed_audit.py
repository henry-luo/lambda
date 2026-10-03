#!/usr/bin/env python3
"""Pinned-tier typed/untyped or same-source release comparison.

Reuse the paired runner's timing/output gate. Interpreter eligibility is checked
in a separate instrumented process, outside every timed sample (D8.1.1v15).
"""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "test/interp"))
from lambda_process import run_lambda_process
from run_benchmarks import build_benchmark_list, check_release_build, mir_script_variants
from run_paired_benchmarks import (compare_row, finalize_provenance, lambda_build_manifest,
    normalized_stdout, run_once, source_provenance)
from benchmark_provenance import command_output, sha256_file


ANNOTATION_CASES = {
    "bounce": "awfy/bounce2.ls", "fannkuch": "beng/fannkuch2.ls",
    "nqueens": "r7rs/nqueens2.ls", "Queens": "awfy/queens2.ls",
    "fasta": "beng/fasta2.ls", "deriv": "larceny/deriv2.ls",
    "towers": "awfy/towers2.ls", "sieve": "awfy/sieve2.ls",
    "primes": "larceny/primes2.ls", "quicksort": "larceny/quicksort2.ls",
    "binarytrees": "beng/binarytrees2.ls", "gcbench": "larceny/gcbench2.ls",
    "Richards": "awfy/richards2.ls", "prettier_ast": "text/prettier_ast2.ls",
}


def erase_annotations(source):
    """Erase the selected corpus's annotations, preserving strings and type definitions."""
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',
        lambda match: re.sub(r'[^\n]', ' ', match.group()), source)
    names = ["int", "float", "bool", "string", "any", "array", "list", "map"]
    names += re.findall(r'^\s*type\s+(\w+)\s*=', masked, re.M)
    contract = r'(?:' + '|'.join(names) + r')\b(?:\[\]|\?)*'
    ranges = []
    for declaration in re.finditer(r'\b(?:let|var)\s+\w+\s*(:\s*' + contract + ')', masked):
        ranges.append(declaration.span(1))
    for signature in re.finditer(r'\b(?:fn|pn)\s+\w+\s*\(', masked):
        depth = 1
        position = signature.end()
        while position < len(masked) and depth:
            token = masked[position]
            if token in '([{':
                depth += 1
            elif token in ')]}':
                depth -= 1
            elif token == ':' and depth == 1:
                annotation = re.match(r':\s*' + contract, masked[position:])
                if annotation:
                    ranges.append((position, position + annotation.end()))
            position += 1
        result = re.match(r'\s+' + contract + r'(?=\s*(?:\{|=>))', masked[position:])
        if result:
            ranges.append((position, position + result.end()))
    # A borrowed parameter is also a `var` declaration; erase each span once.
    ranges = set(ranges)
    for start, end in sorted(ranges, reverse=True):
        source = source[:start] + source[end:]
    return source


def repeat_annotation_case(key, source, repeats):
    if key in ("bounce", "nqueens", "Queens", "deriv"):
        needle = "    let result = benchmark()"
        if source.count(needle) != 1:
            raise ValueError("missing benchmark invocation: " + key)
        source = source.replace(needle, "    var result = 0\n    for repetition in 1 to " +
            str(repeats) + " { result = benchmark() }")
        expected = (ROOT / "test/benchmark" / ANNOTATION_CASES[key]).with_suffix(".txt").read_text()
    elif key == "fannkuch":
        source = source.replace("pn main() {\n    var __t0 = clock()", "pn benchmark() {")
        source = source[:source.index('    print(checksum ++ "\\n")')]
        source += "    return [checksum, max_flips]\n}\n\n"
        source += ("pn main() {\n    let start = clock()\n"
            "    var result = [0, 0]\n    for repetition in 1 to " + str(repeats) +
            " { result = benchmark() }\n    let elapsed = (clock() - start) * 1000.0\n"
            '    print(result)\n    print("\\n__TIMING__:" ++ elapsed ++ "\\n")\n}\n')
        expected = "[228, 16]\n"
    else:
        source = source.replace("pn main() {", "pn benchmark_main() {")
        source = re.sub(r'^    var __t[01] = clock\(\)\n|^    print\("__TIMING__:[^\n]*\n',
            '', source, flags=re.M)
        source += ("\npn main() {\n    let start = clock()\n    for repetition in 1 to " +
            str(repeats) + " { benchmark_main() }\n"
            '    print("__TIMING__:" ++ ((clock() - start) * 1000.0) ++ "\\n")\n}\n')
        oracle = ROOT / "test/benchmark" / ANNOTATION_CASES[key]
        oracle = oracle.with_suffix(".formatted.txt" if key == "prettier_ast" else ".txt")
        expected = (oracle.read_text().rstrip() + "\n") * repeats
    return source, expected


def write_annotation_sources(key, scripts, typed):
    """Keep imported benchmark kernels inside the matched annotation pair."""
    original_import = "import ~~.richards2_core"
    core = (ROOT / "test/benchmark/richards2_core.ls").read_text() if key == "Richards" else None
    for script, source in zip(scripts, (erase_annotations(typed), typed)):
        if core is not None:
            if source.count(original_import) != 1:
                raise ValueError("missing Richards kernel import")
            module = Path(script).with_name(Path(script).stem + "_core.ls")
            module.write_text(erase_annotations(core) if "_erased" in module.stem else core)
            module.with_suffix(".txt").write_text("")
            source = source.replace(original_import, "import ." + module.stem)
        Path(script).write_text(source)


def annotation_controls(directory, repeats, cases=None, target_ms=0, binary=None,
                        tier="jit", timeout=60):
    """Only annotations differ; identical repetition makes tiny loops measurable."""
    directory.mkdir(parents=True, exist_ok=True)
    rows = []
    for key, path in ANNOTATION_CASES.items():
        if cases and key not in cases:
            continue
        original = (ROOT / "test/benchmark" / path).read_text()
        scripts = [str(directory / (key + "_" + suffix + ".ls"))
                   for suffix in ("erased", "typed")]
        case_repeats = 1 if target_ms else repeats
        for calibration in range(2 if target_ms else 1):
            typed, expected = repeat_annotation_case(key, original, case_repeats)
            erased = erase_annotations(typed)
            if erased == typed and key != "Richards":
                raise ValueError("annotation erasure failed: " + path)
            write_annotation_sources(key, scripts, typed)
            for script in scripts:
                Path(script).with_suffix(".txt").write_text(expected)
            if target_ms and calibration == 0:
                samples = [run_once(binary, script, timeout, tier=tier) for script in scripts]
                if any(sample["status"] != "ok" for sample in samples):
                    raise ValueError("annotation calibration failed: " + key)
                longest = max(sample["exec_ms"] for sample in samples)
                case_repeats = max(1, min(10000, int(target_ms / max(longest, 0.001)) + 1))
                print("calibration", key, "repeats", case_repeats, flush=True)
        rows.append(("annotation/" + key, *scripts))
    return rows


def fasta_cast_controls(directory, repeats, **calibration):
    """Vary the PRNG cast independently at each fixed annotation setting."""
    annotation = annotation_controls(directory, repeats, cases={"fasta"}, **calibration)[0]
    rows = []
    cast = "int((seed_arr[0] * IA + IC) % IM)"
    for suffix, script in zip(("erased", "typed"), annotation[1:]):
        source = Path(script).read_text()
        if source.count(cast) != 1:
            raise ValueError("missing unique fasta PRNG cast")
        without_cast = directory / ("fasta_" + suffix + "_no_cast.ls")
        without_cast.write_text(source.replace(cast, cast[3:]))
        without_cast.with_suffix(".txt").write_text(Path(script).with_suffix(".txt").read_text())
        rows.append(("cast/fasta_" + suffix, str(without_cast), script))
    return rows


def interp_diagnostic(binary, script, directory, label, timeout):
    profile = directory / (label + ".exec.tsv")
    profile.unlink(missing_ok=True)
    result = run_lambda_process(binary, script, "interp", timeout,
        procedural=True, extra_env={"LAMBDA_EXEC_PROFILE": "1",
            "LAMBDA_EXEC_PROFILE_OUT": str(profile)})
    stats = re.search(r"interp: executed=(\d+) fallback=(\d+) excluded=(\d+)",
                      result.stderr)
    # A T0 process cannot enter any profiled MIR function, including satellites.
    executed_mir = False
    if profile.exists():
        executed_mir = any(line.startswith("frame_entry\t") and
            int(line.rsplit("\t", 1)[1]) > 0
            for line in profile.read_text().splitlines())
    accepted = result.status == "ok" and stats is not None and (
        tuple(map(int, stats.groups())) == (1, 0, 0)) and not executed_mir
    return {"accepted": accepted, "status": result.status,
        "stats": list(map(int, stats.groups())) if stats else None,
        "executed_mir": executed_mir, "stdout_sha256": hashlib.sha256(
            result.stdout.encode()).hexdigest(), "stderr": result.stderr}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", default="./lambda.exe")
    parser.add_argument("--control", help="compare the same typed source with this release")
    parser.add_argument("--source", choices=("typed", "untyped"), default="typed",
                        help="same-source old/new lane when --control is supplied")
    parser.add_argument("--tier", choices=("jit", "interp"), default="jit")
    parser.add_argument("--only", default="", help="comma-separated suite/name keys")
    parser.add_argument("--pairs", type=int, default=15)
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--annotation-controls", action="store_true")
    parser.add_argument("--fasta-cast-controls", action="store_true")
    parser.add_argument("--repeats", type=int, default=1000)
    parser.add_argument("--annotation-target-ms", type=float, default=0,
                        help="calibrate identical repetitions per pair before the campaign")
    parser.add_argument("--output", default="temp/typed_tuning/audit.json")
    args = parser.parse_args()
    if args.pairs < 1 or args.repeats < 1 or args.annotation_target_ms < 0:
        parser.error("pairs and repeats must be positive")
    os.chdir(ROOT)
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    for key in list(os.environ):
        if key.startswith(("LAMBDA_", "JS_EXECUTION_", "COW_EXEC_PROFILE")):
            os.environ.pop(key)
    os.environ.update(LAMBDA_NO_LOG="1", LAMBDA_RSS_REPORT="1", TMPDIR=str(ROOT / "temp"))
    candidate = str(Path(args.candidate).resolve())
    control = str(Path(args.control).resolve()) if args.control else candidate
    for binary in set((candidate, control)):
        check_release_build(exe_path=binary)
    selected = set(args.only.split(",")) if args.only else None
    rows = []
    for benchmark in build_benchmark_list([], []):
        key = benchmark["suite"] + "/" + benchmark["name"]
        if selected and key not in selected:
            continue
        untyped, typed = mir_script_variants(benchmark)
        if typed:
            rows.append((key, untyped, typed))
    if args.annotation_controls:
        cases = {key[len("annotation/"):] for key in selected
                 if key.startswith("annotation/")} if selected else None
        rows += annotation_controls(output.parent / (output.stem + "_controls"), args.repeats,
            cases=cases, target_ms=args.annotation_target_ms, binary=candidate,
            tier=args.tier, timeout=args.timeout)
    if args.fasta_cast_controls:
        rows += fasta_cast_controls(output.parent / (output.stem + "_cast_controls"),
            args.repeats, target_ms=args.annotation_target_ms, binary=candidate,
            tier=args.tier, timeout=args.timeout)
        if selected:
            rows = [row for row in rows if row[0] in selected]
    if not rows:
        parser.error("no typed rows selected")
    if selected and selected - {row[0] for row in rows}:
        parser.error("unknown keys: " + ", ".join(sorted(selected - {row[0] for row in rows})))
    result = {"metadata": {"started_at": datetime.datetime.now().isoformat(),
        "platform": platform.platform(), "commit": command_output(["git", "rev-parse", "HEAD"]),
        "status": command_output(["git", "status", "--short"]),
        "candidate": {"path": candidate, "sha256": sha256_file(candidate)},
        "control": {"path": control, "sha256": sha256_file(control)},
        "lambda_build_manifest": lambda_build_manifest(),
        "comparison": "same " + args.source + " source" if args.control else "untyped/typed ports",
        "tier": args.tier, "pairs": args.pairs, "warmups": 1,
        "annotation_repeats": args.repeats, "load_start": os.getloadavg(),
        "environment": {k: v for k, v in os.environ.items() if k.startswith("LAMBDA_")}}, "rows": []}
    failed = False
    for key, untyped, typed in rows:
        if args.control and args.source == "untyped":
            typed = untyped
        first = typed if args.control else untyped
        oracle = Path(typed).with_suffix(".txt").read_text().strip() if key.startswith(("annotation/", "cast/")) else None
        print(args.tier, key, flush=True)
        row = {"key": key, "source_sha256": [sha256_file(first), sha256_file(typed)],
            "source_provenance": [source_provenance(script) for script in (first, typed)]}
        expected_hash = hashlib.sha256(normalized_stdout(oracle).encode()).hexdigest() if oracle else None
        if args.tier == "interp":
            row["diagnostics"] = [interp_diagnostic(binary, script, output.parent,
                key.replace("/", "_") + "_" + label, args.timeout)
                for binary, script, label in ((control, first, "control"),
                                              (candidate, typed, "candidate"))]
        row["warmups"] = [run_once(binary, script, args.timeout, tier=args.tier,
                                  expected_stdout_text=oracle)
            for binary, script in ((control, first), (candidate, typed))]
        eligible = all(sample["status"] == "ok" and
            sample.get("stdout_contains_expected") is not False and
            (not expected_hash or sample["stdout_sha256"] == expected_hash)
            for sample in row["warmups"])
        eligible &= all(d["accepted"] for d in row.get("diagnostics", []))
        if eligible:
            row.update(compare_row(control, candidate, first, typed, args.pairs,
                args.timeout, tier=args.tier, expected_stdout_text=oracle))
            failed |= (not row["stdout_equal_all"] or row["pairs_valid"] != args.pairs or
                       row.get("expected_stdout_matches_all") is False)
            row["golden_equal_all"] = all(sample["stdout_sha256"] == expected_hash
                for pair in row["pairs"] for sample in (pair["control"], pair["candidate"])) if oracle else None
            failed |= row["golden_equal_all"] is False
            print(" ", row["candidate_over_control_median_ratio"],
                  "equal:", row["stdout_equal_all"], flush=True)
        else:
            row["excluded"] = True
            print(" excluded (execution/output gate)", flush=True)
        row["load"] = os.getloadavg()
        result["rows"].append(row)
        output.write_text(json.dumps(result, indent=2) + "\n")
    result["metadata"]["load_end"] = os.getloadavg()
    finalize_provenance(result["metadata"], control, candidate)
    result["metadata"]["source_inputs_stable"] = all(
        provenance == source_provenance(provenance["path"], provenance["source_root"])
        for row in result["rows"] for provenance in row["source_provenance"])
    failed |= not result["metadata"]["source_inputs_stable"]
    failed |= not result["metadata"]["binary_inputs_stable"]
    failed |= not result["metadata"]["lambda_build_manifest_stable"]
    output.write_text(json.dumps(result, indent=2) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
