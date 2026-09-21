#!/usr/bin/env python3
"""Generate frozen JS benchmark contracts from the standard runner."""

import argparse
import json
import os
import runpy

from js_benchmark_manifest import MVP_V1_PROFILE, TUNE14_V1_PROFILE, build_workloads


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
MVP_MANIFEST_PATH = os.path.join(SCRIPT_DIR, "js_mvp_manifest_v1.json")
TUNE14_MANIFEST_PATH = os.path.join(SCRIPT_DIR, "js_tune14_manifest_v1.json")


def manifest_metadata(profile):
    if profile == MVP_V1_PROFILE:
        return {"schema": 2}
    return {
        "schema": 2,
        "profile": profile,
        "contract": {
            "base_manifest": "test/benchmark/js_mvp_manifest_v1.json",
            "purpose": ("Tune14 fixed 63-row JS population with the canonical Navier "
                        "single-frame timer and a post-timing density oracle."),
        },
    }


def main():
    parser = argparse.ArgumentParser(description="Generate a frozen JS benchmark contract")
    parser.add_argument("--profile", choices=[MVP_V1_PROFILE, TUNE14_V1_PROFILE],
                        default=MVP_V1_PROFILE)
    parser.add_argument("--output", default=None)
    args = parser.parse_args()
    output = args.output
    if output is None:
        output = MVP_MANIFEST_PATH if args.profile == MVP_V1_PROFILE else TUNE14_MANIFEST_PATH

    os.chdir(PROJECT_ROOT)
    runner = runpy.run_path(os.path.join(SCRIPT_DIR, "run_benchmarks.py"),
                            run_name="js_benchmark_manifest_generate")
    rows = runner["build_benchmark_list"](None, None, include_text=True)
    workloads = build_workloads(runner, rows, args.profile)
    if len(workloads) != 63:
        raise RuntimeError("expected 63 standard workloads")
    manifest = manifest_metadata(args.profile)
    manifest["workloads"] = workloads
    with open(output, "w", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2)
        stream.write("\n")
    print("Wrote JS benchmark manifest: " + output + " (63 workloads)")


if __name__ == "__main__":
    main()
