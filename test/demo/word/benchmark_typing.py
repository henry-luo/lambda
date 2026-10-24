#!/usr/bin/env python3
"""Measure individual native Word keystrokes with a release build."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[3]
ARTIFACTS = ROOT / "temp/word/performance"


def fixture(sample, text, paint=False):
    paragraph = "#edit-surface .word-page > p:nth-of-type(2)" if sample else "#edit-surface p"
    setup = [] if sample else [{"type": "click", "target": {"selector": "#new"}}]
    # A collapsed native selection makes the result independent of hit-test coordinates.
    setup.append({"type": "set_editing_selection", "target": {"selector": paragraph}, "start": 0, "end": 0})
    events = list(setup)
    for char in text:
        events.append({"type": "type_physical", "text": char})
        if paint:
            # Include native raster painting and require a nonempty opaque surface.
            events.append({"type": "assert_pixel", "x": 1, "y": 1,
                           "min_a": 255, "force_render": True})
    events.append({"type": "assert_text", "target": {"selector": paragraph}, "contains": text})
    return {"html": "test/demo/word/word.ls", "viewport": {"width": 1440, "height": 900},
            "profile_events": True, "event_interval_ms": 0, "events": events}, len(setup)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lambda-exe", default=str(ROOT / "lambda.exe"))
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--characters", type=int, default=120)
    parser.add_argument("--paint", action="store_true",
                        help="Include a native raster repaint after each physical key")
    parser.add_argument("--label", default="typing")
    args = parser.parse_args()
    if args.repeat < 1 or args.characters < 1:
        parser.error("repeat and characters must be positive")
    if not args.label or any(not (char.isalnum() or char in "-_") for char in args.label):
        parser.error("label must contain only letters, numbers, hyphens, or underscores")
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    text = ("Quick typing stays responsive. " * (args.characters // 31 + 1))[:args.characters]
    report = {}
    for sample in (False, True):
        name = "sample" if sample else "blank"
        spec, start = fixture(sample, text, args.paint)
        input_path = ARTIFACTS / f"{args.label}-{name}-events.json"
        input_path.write_text(json.dumps(spec, indent=2) + "\n")
        durations = []
        for repeat in range(args.repeat):
            prefix = ARTIFACTS / f"{args.label}-{name}-{repeat + 1}"
            result_path = prefix.with_suffix(".json")
            result_path.unlink(missing_ok=True)
            with prefix.with_suffix(".stdout").open("w") as out, prefix.with_suffix(".stderr").open("w") as err:
                result = subprocess.run([str(Path(args.lambda_exe).resolve()), "--no-log", "view",
                    spec["html"], "--headless", "--event-file", str(input_path),
                    "--event-result", str(result_path)], cwd=ROOT, stdout=out, stderr=err, timeout=180)
            data = json.loads(result_path.read_text()) if result_path.exists() else {}
            if result.returncode or data.get("result") != "PASS" or "timings_ms" not in data:
                raise SystemExit(f"{name}: correctness/profiling failed; use a release build and inspect {prefix}.stderr")
            stride = 2 if args.paint else 1
            durations.extend(sum(data["timings_ms"][index:index + stride])
                             for index in range(start, start + len(text) * stride, stride))
        ordered = sorted(durations)
        report[name] = {"keys": len(ordered), "median_ms": round(statistics.median(ordered), 3),
                        "p95_ms": ordered[(len(ordered) * 95 - 1) // 100], "max_ms": max(ordered)}
        print(f"{name}: {json.dumps(report[name])}", flush=True)
    (ARTIFACTS / f"{args.label}-summary.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
