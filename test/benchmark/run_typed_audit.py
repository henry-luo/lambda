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
sys.path.insert(0, str(ROOT / "test/interp"))
from lambda_process import run_lambda_process
from run_benchmarks import build_benchmark_list, check_release_build, mir_script_variants
from run_paired_benchmarks import compare_row, finalize_provenance, run_once
from benchmark_provenance import command_output, sha256_file


def annotation_controls(directory, repeats):
    """Only annotations differ; identical repetition makes tiny loops measurable."""
    directory.mkdir(parents=True, exist_ok=True)
    rows = []
    for key, path in (("bounce", "awfy/bounce2.ls"),
                      ("fannkuch", "beng/fannkuch2.ls")):
        typed = (ROOT / "test/benchmark" / path).read_text()
        if key == "bounce":
            typed = typed.replace("    let result = benchmark()",
                "    var result = 0\n    for repetition in 1 to " + str(repeats) +
                " { result = benchmark() }")
            expected = "Bounce: PASS\n"
        else:
            typed = typed.replace("pn main() {\n    var __t0 = clock()",
                                  "pn benchmark() {")
            typed = typed[:typed.index('    print(checksum ++ "\\n")')]
            typed += "    return [checksum, max_flips]\n}\n\n"
            typed += ("pn main() {\n    let start = clock()\n"
                      "    var result = [0, 0]\n    for repetition in 1 to " +
                      str(repeats) + " { result = benchmark() }\n"
                      "    let elapsed = (clock() - start) * 1000.0\n"
                      "    print(result)\n    print(\"\\n__TIMING__:\" ++ elapsed ++ \"\\n\")\n}\n")
            expected = "[228, 16]\n"
        erased = re.sub(r":\s*int(?:\[\])?", "", typed)
        erased = re.sub(r"\)\s+int\s*\{", ") {", erased)
        if erased == typed or re.search(r":\s*int|\)\s+int\s*\{", erased):
            raise ValueError("annotation erasure failed: " + path)
        scripts = []
        for suffix, source in (("erased", erased), ("typed", typed)):
            script = directory / (key + "_" + suffix + ".ls")
            script.write_text(source)
            script.with_suffix(".txt").write_text(expected)
            scripts.append(str(script))
        rows.append(("annotation/" + key, *scripts))
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
    parser.add_argument("--tier", choices=("jit", "interp"), default="jit")
    parser.add_argument("--only", default="", help="comma-separated suite/name keys")
    parser.add_argument("--pairs", type=int, default=15)
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--annotation-controls", action="store_true")
    parser.add_argument("--repeats", type=int, default=1000)
    parser.add_argument("--output", default="temp/typed_tuning/audit.json")
    args = parser.parse_args()
    if args.pairs < 1 or args.repeats < 1:
        parser.error("pairs and repeats must be positive")
    os.chdir(ROOT)
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    for key in list(os.environ):
        if key.startswith(("LAMBDA_", "JS_EXECUTION_")):
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
        rows += annotation_controls(output.parent / (output.stem + "_controls"), args.repeats)
    if not rows:
        parser.error("no typed rows selected")
    if selected and selected - {row[0] for row in rows}:
        parser.error("unknown keys: " + ", ".join(sorted(selected - {row[0] for row in rows})))
    result = {"metadata": {"started_at": datetime.datetime.now().isoformat(),
        "platform": platform.platform(), "commit": command_output(["git", "rev-parse", "HEAD"]),
        "status": command_output(["git", "status", "--short"]),
        "candidate": {"path": candidate, "sha256": sha256_file(candidate)},
        "control": {"path": control, "sha256": sha256_file(control)},
        "comparison": "same typed source" if args.control else "untyped/typed ports",
        "tier": args.tier, "pairs": args.pairs, "warmups": 1,
        "annotation_repeats": args.repeats, "load_start": os.getloadavg(),
        "environment": {k: v for k, v in os.environ.items() if k.startswith("LAMBDA_")}}, "rows": []}
    failed = False
    for key, untyped, typed in rows:
        first = typed if args.control else untyped
        oracle = Path(typed).with_suffix(".txt").read_text().strip() if key.startswith("annotation/") else None
        print(args.tier, key, flush=True)
        row = {"key": key, "source_sha256": [sha256_file(first), sha256_file(typed)]}
        if args.tier == "interp":
            row["diagnostics"] = [interp_diagnostic(binary, script, output.parent,
                key.replace("/", "_") + "_" + label, args.timeout)
                for binary, script, label in ((control, first, "control"),
                                              (candidate, typed, "candidate"))]
        row["warmups"] = [run_once(binary, script, args.timeout, tier=args.tier,
                                  expected_stdout_text=oracle)
            for binary, script in ((control, first), (candidate, typed))]
        eligible = all(sample["status"] == "ok" and
            sample.get("stdout_contains_expected") is not False for sample in row["warmups"])
        eligible &= all(d["accepted"] for d in row.get("diagnostics", []))
        if eligible:
            row.update(compare_row(control, candidate, first, typed, args.pairs,
                args.timeout, tier=args.tier, expected_stdout_text=oracle))
            failed |= (not row["stdout_equal_all"] or row["pairs_valid"] != args.pairs or
                       row.get("expected_stdout_matches_all") is False)
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
    failed |= not result["metadata"]["binary_inputs_stable"]
    output.write_text(json.dumps(result, indent=2) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
