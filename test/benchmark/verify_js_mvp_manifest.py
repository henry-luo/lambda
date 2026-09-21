#!/usr/bin/env python3
"""Verify frozen source identities and wrapper contracts for JS benchmarks."""

import argparse
import json
import os
import runpy
import sys

from js_benchmark_manifest import (
    MVP_V1_PROFILE,
    TUNE14_V1_PROFILE,
    build_workloads,
)


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
MVP_MANIFEST_PATH = os.path.join(SCRIPT_DIR, "js_mvp_manifest_v1.json")
TUNE14_MANIFEST_PATH = os.path.join(SCRIPT_DIR, "js_tune14_manifest_v1.json")


def expected_profile(manifest, path):
    profile = manifest.get("profile", MVP_V1_PROFILE)
    if profile not in {MVP_V1_PROFILE, TUNE14_V1_PROFILE}:
        raise ValueError("unknown JS benchmark manifest profile: " + str(profile))
    if profile == MVP_V1_PROFILE and os.path.abspath(path) != MVP_MANIFEST_PATH:
        raise ValueError("a non-MVP manifest must declare its profile")
    if profile == TUNE14_V1_PROFILE:
        contract = manifest.get("contract")
        if not isinstance(contract, dict) or contract.get("base_manifest") != "test/benchmark/js_mvp_manifest_v1.json":
            raise ValueError("Tune14 manifest must name the immutable MVP v1 base contract")
    return profile


def main():
    parser = argparse.ArgumentParser(description="Verify a frozen JS benchmark contract")
    parser.add_argument("--manifest", default=MVP_MANIFEST_PATH)
    args = parser.parse_args()

    os.chdir(PROJECT_ROOT)
    manifest_path = os.path.abspath(args.manifest)
    with open(manifest_path, encoding="utf-8") as stream:
        manifest = json.load(stream)
    try:
        profile = expected_profile(manifest, manifest_path)
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 1
    runner = runpy.run_path(os.path.join(SCRIPT_DIR, "run_benchmarks.py"),
                            run_name="js_benchmark_manifest_verify")
    rows = runner["build_benchmark_list"](None, None, include_text=True)
    workloads = manifest.get("workloads", [])
    if manifest.get("schema") != 2 or len(rows) != 63 or len(workloads) != 63:
        print("JS benchmark manifest must contain the 63 standard workloads", file=sys.stderr)
        return 1
    expected_workloads = build_workloads(runner, rows, profile)
    for entry, expected in zip(workloads, expected_workloads):
        if entry != expected:
            print("JS benchmark manifest mismatch: " + expected["id"], file=sys.stderr)
            return 1
    label = "JS MVP" if profile == MVP_V1_PROFILE else "JS Tune14"
    print(label + " benchmark manifest verified: 63 workloads")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
