#!/usr/bin/env python3
"""Ensure diagnostic MIR calls do not introduce production safepoints (D6.3.3)."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "utils"))
from analyze_js_mir import parse_frames
from analyze_mir_gap import parse_mir


def check(script, directory, executable=ROOT / "lambda.exe", cow_sites=False):
    outputs, snapshots = [], []
    for enabled in (False, True):
        prefix = directory / (script.stem + ("_profile" if enabled else "_plain"))
        env = {key: value for key, value in os.environ.items()
               if not key.startswith(("LAMBDA_", "JS_EXECUTION_", "COW_EXEC_PROFILE"))}
        env.update(LAMBDA_EXEC_BACKEND="jit", LAMBDA_DISABLE_MIR_CACHE="1",
                   LAMBDA_MIR_DUMP_PATH=str(prefix.with_suffix(".mir")),
                   LAMBDA_LOG_FILE=str(prefix.with_suffix(".log")),
                   LAMBDA_MIR_LOG_FRAME_SLOTS="1", TMPDIR=str(ROOT / "temp"))
        if cow_sites:
            # compare attribution with aggregate accounting held constant.
            env.update(COW_EXEC_PROFILE="1", COW_EXEC_PROFILE_SITES="1" if enabled else "0",
                       COW_EXEC_PROFILE_OUT=str(prefix.with_suffix(".cow.tsv")),
                       COW_EXEC_PROFILE_SITES_OUT=str(prefix.with_suffix(".sites.tsv")))
        if enabled:
            env.update(LAMBDA_EXEC_PROFILE="1",
                       LAMBDA_EXEC_PROFILE_OUT=str(prefix.with_suffix(".tsv")))
        process = subprocess.run([str(executable), "run", str(script)],
                                 env=env, cwd=ROOT, capture_output=True, text=True, timeout=60)
        if process.returncode:
            raise AssertionError(process.stderr)
        outputs.append(process.stdout)
        frames = parse_frames(prefix.with_suffix(".log"))
        if not frames:
            raise AssertionError("missing frame telemetry: " + str(prefix))
        frame_shape = {str(key): {metric: frame[metric] for metric in
                       ("roots", "root_stores", "safepoints")}
                       for key, frame in frames.items()}
        functions = parse_mir(prefix.with_suffix(".mir"))
        memory_shape = [(fn["name"], fn["memory_loads"], fn["memory_stores"])
                        for fn in functions]
        marker_count = sum(fn["call_targets"].get("lambda_exec_profile_note_call", 0)
                           for fn in functions)
        if bool(marker_count) != enabled:
            raise AssertionError("diagnostic marker emission did not follow the pin")
        snapshots.append({"frames": frame_shape, "memory": memory_shape})
    if outputs[0] != outputs[1] or snapshots[0] != snapshots[1]:
        raise AssertionError(str(script) + " profiling changes roots/safepoints/output:\n" +
                             json.dumps(snapshots, indent=2))
    return {"script": str(script), **snapshots[0]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "lambda.exe")
    parser.add_argument("--cow-sites", action="store_true")
    args = parser.parse_args()
    directory = ROOT / "temp/mir_check" / ("profile_parity_" + str(os.getpid()))
    directory.mkdir(parents=True, exist_ok=True)
    scripts = ("array_root_before_gc", "tune26_dense_carried_index",
               "typed_fill_activation_cert")
    if args.cow_sites:
        scripts += ("string_cow_capture_guard",)
    results = [check(ROOT / "test/mir/lambda" / (name + ".ls"), directory,
                     args.exe.resolve(), args.cow_sites)
               for name in scripts]
    (directory / "result.json").write_text(json.dumps(results, indent=2) + "\n")
    print("MIR profile parity: selected fixtures preserve roots, safepoints and memory operations")


if __name__ == "__main__":
    main()
