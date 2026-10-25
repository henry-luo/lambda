#!/usr/bin/env python3
"""Qualify the full local pack on a pinned release host and retain its provenance.

Requires prepare_ime_chinese.py prepare first. This runner does not prepare
resources, build executables, change dictionaries, or update acceptance rates.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import resource
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "temp/ime_chinese/m1"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def percentile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)]


def summarize(rows, mode):
    queries = [row[mode] for row in rows]
    covered = [(row, query) for row, query in zip(rows, queries) if query["rank"] is not None]
    strokes = sum(len(query["input"]) + (query["rank"] - 1) // 9 + 1 for _, query in covered)
    characters = sum(len(row["word"]) for row, _ in covered)
    return {"total": len(rows), "found": len(covered),
            **{f"top_{count}": sum(q["rank"] is not None and q["rank"] <= count for q in queries) for count in (1, 3, 9)},
            "truncated": sum(q["truncated"] for q in queries),
            "latency_ms": {"p50": percentile([q["ms"] for q in queries], .5),
                           "p95": percentile([q["ms"] for q in queries], .95)},
            "max_keys": max(q["keys"] for q in queries), "max_postings": max(q["postings"] for q in queries),
            "keystrokes_per_character_found_only": strokes / characters if characters else None,
            "keystroke_model": "typed letters + one selection + one PageDown per nine candidates; missing words excluded",
            "missing": [row["word"] for row, query in zip(rows, queries) if query["rank"] is None]}


def check_baseline(report, baseline):
    failures = []
    if report["fixture_sha256"] != baseline["fixture_sha256"]:
        failures.append("frozen fixture hash changed")
    for mode, minimum in baseline["minimum"].items():
        for metric, floor in minimum.items():
            actual = report["summary"][mode][metric]
            if actual < floor:
                failures.append(f"{mode}.{metric}: {actual} < {floor}")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=ROOT / "lambda.exe")
    parser.add_argument("--tier", choices=("jit", "interp"), default="jit")
    parser.add_argument("--options", type=json.loads, default={})
    parser.add_argument("--report", type=Path, default=WORK / "qualification.json")
    parser.add_argument("--baseline", type=Path, default=ROOT / "test/ime/baseline.json")
    args = parser.parse_args()
    executable = args.executable.resolve()
    raw = executable.read_bytes()
    if b"VALIDATOR] validate_against_base_type" in raw:
        parser.error("performance qualification requires a release executable")
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / "eval-options.json").write_text(json.dumps(args.options) + "\n")
    environment = {**os.environ, "LAMBDA_EXEC_BACKEND": args.tier}
    command = [str(executable), "--no-log", "run", "test/ime/engine_eval.ls"]
    (WORK / "evaluation.json").unlink(missing_ok=True)
    result = subprocess.run(command, cwd=ROOT, env=environment, text=True, capture_output=True, timeout=300)
    log = args.report.with_suffix(".log")
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(result.stdout + result.stderr)
    if result.returncode or result.stdout.strip().splitlines()[-1:] != ["[true]"]:
        parser.exit(1, f"IME full-pack qualification failed ({result.returncode}); see {log}\n")
    measured = json.loads((WORK / "evaluation.json").read_bytes())
    # This process launches one child; its RUSAGE_CHILDREN maximum is that run's peak.
    peak_rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    if sys.platform != "darwin":
        peak_rss *= 1024
    sources = [*sorted((ROOT / "lmd/package/ime").rglob("*.ls")),
               ROOT / "lmd/package/io/line_index.ls", ROOT / "lambda/runtime/lambda-vector.cpp",
               ROOT / "lmd/package/ime/chinese/syllables.txt",
               ROOT / "test/ime/engine_eval.ls", Path(__file__).resolve()]
    report = {"format_version": 1, "tier": args.tier, "host": platform.platform(),
              "executable": {"path": str(executable), "sha256": sha(executable)},
              "fixture_sha256": sha(ROOT / "test/ime/heldout.json"),
              "pack_manifest_sha256": sha(ROOT / "lmd/package/ime/chinese/data/zh-Hans/manifest.json"),
              "source_sha256": {str(p.relative_to(ROOT)): sha(p) for p in sources},
              "peak_rss_bytes": int(peak_rss),
              "scope": "repository-authored frozen query set; baseline, not a representative quality claim",
              "summary": {mode: summarize(measured["rows"], mode) for mode in ("full", "initials", "fuzzy")},
              **measured}
    report["acceptance"] = {"baseline_sha256": sha(args.baseline),
        "failures": check_baseline(report, json.loads(args.baseline.read_bytes()))}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"load_ms": report["load_ms"], "peak_rss_bytes": report["peak_rss_bytes"],
                      "summary": report["summary"], "acceptance": report["acceptance"],
                      "report": str(args.report)}, ensure_ascii=False, indent=2))
    return 1 if report["acceptance"]["failures"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
