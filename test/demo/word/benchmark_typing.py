#!/usr/bin/env python3
"""Measure native Word typing, formatting and paragraph breaks with a release build."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[3]
ARTIFACTS = ROOT / "temp/word/performance"


def fixture(sample, text, paint=False, workload="typing", entry="test/demo/word/word.ls"):
    paragraph = "#edit-surface .word-page > p:nth-of-type(2)" if sample else "#edit-surface p"
    setup = [] if sample else [{"type": "click", "target": {"selector": "#new"}}]
    # A collapsed native selection makes the result independent of hit-test coordinates.
    setup.append({"type": "set_editing_selection", "target": {"selector": paragraph}, "start": 0, "end": 0})
    if workload == "editing":
        setup.append({"type": "script", "js":
            "window.wordPerfBefore=document.getElementById('edit-surface').textContent.replace(/\u200b/g,'');"
            "window.wordPerfParagraphs=document.querySelectorAll('#edit-surface p').length;"
            "window.wordPerfPoint=function(selector,offset){"
            "var w=document.createTreeWalker(document.querySelector(selector),4),n=w.nextNode();"
            "while(n&&offset>n.textContent.length){offset-=n.textContent.length;n=w.nextNode();}"
            "return [n,offset];};window.wordPerfSelect=function(selector,start,end){"
            "var a=wordPerfPoint(selector,start),b=wordPerfPoint(selector,end);"
            "document.getSelection().setBaseAndExtent(a[0],a[1],b[0],b[1]);};"
            "window.wordPerfBold=function(selector,offset){var n=wordPerfPoint(selector,offset)[0];"
            "return !!(n&&n.parentNode.closest('strong,b'));};"})
    events = list(setup)
    measurements = []

    def append(event, kind):
        measurements.append((len(events), 2 if paint else 1, kind))
        events.append(event)
        if paint:
            # Include native raster painting and require a nonempty opaque surface.
            events.append({"type": "assert_pixel", "x": 1, "y": 1,
                           "min_a": 255, "force_render": True})
    breaks = 0
    for index, char in enumerate(text, 1):
        append({"type": "type_physical", "text": char}, "typing")
        if workload == "editing" and index % 6 == 0:
            current = (2 if sample else 1) + breaks
            end = index - breaks * 12
            selector = json.dumps(f"#edit-surface .word-page > p:nth-of-type({current})")
            # Native fixture offsets address one text node; formatted paragraphs contain several.
            events.append({"type": "script", "js":
                f"wordPerfSelect({selector},{end - 6},{end});"
                f"window.wordPerfWasBold=wordPerfBold({selector},{end - 5})"})
            append({"type": "key_combo", "key": "b", "mods_str": "ctrl"}, "formatting")
            events.append({"type": "assert_script", "js":
                f"wordPerfBold({selector},{end - 5})!==wordPerfWasBold&&"
                f"wordPerfBold({selector},{end - 1})!==wordPerfWasBold"})
            events.append({"type": "script", "js": f"wordPerfSelect({selector},{end},{end})"})
        if workload == "editing" and index % 12 == 0:
            append({"type": "key_press", "key": "Enter"}, "paragraph_break")
            breaks += 1
    if workload == "editing":
        # Paragraph splitting and mark toggles must preserve every original and inserted character.
        events.append({"type": "assert_script", "js":
            "document.getElementById('edit-surface').textContent.replace(/\u200b/g,'').replace(" + json.dumps(text) +
            ", '')===wordPerfBefore&&document.querySelectorAll('#edit-surface p').length==="
            f"wordPerfParagraphs+{breaks}"})
        events.append({"type": "assert_count", "target": {"selector": "#edit-surface strong"},
                       "min": 1})
    else:
        events.append({"type": "assert_text", "target": {"selector": paragraph}, "contains": text})
    return {"html": entry, "viewport": {"width": 1440, "height": 900},
            "profile_events": True, "event_interval_ms": 0, "events": events}, measurements


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lambda-exe", default=str(ROOT / "lambda.exe"))
    parser.add_argument("--entry", default="test/demo/word/word.ls",
                        help="Word entry script, including a saved application snapshot")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--characters", type=int, default=120)
    parser.add_argument("--paint", action="store_true",
                        help="Include a native raster repaint after each physical key")
    parser.add_argument("--workload", choices=("typing", "editing"), default="typing",
                        help="Editing also measures bold toggles and Enter between physical keys")
    parser.add_argument("--label", default="typing")
    args = parser.parse_args()
    if args.repeat < 1 or args.characters < 1:
        parser.error("repeat and characters must be positive")
    if args.workload == "editing" and args.characters < 12:
        parser.error("editing requires at least 12 characters")
    if not args.label or any(not (char.isalnum() or char in "-_") for char in args.label):
        parser.error("label must contain only letters, numbers, hyphens, or underscores")
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    text = ("Quick typing stays responsive. " * (args.characters // 31 + 1))[:args.characters]
    report = {}
    for sample in (False, True):
        name = "sample" if sample else "blank"
        spec, measurements = fixture(sample, text, args.paint, args.workload, args.entry)
        input_path = ARTIFACTS / f"{args.label}-{name}-events.json"
        input_path.write_text(json.dumps(spec, indent=2) + "\n")
        durations = {}
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
            for index, stride, kind in measurements:
                durations.setdefault(kind, []).append(sum(data["timings_ms"][index:index + stride]))
        if args.workload == "editing":
            durations["all_inputs"] = [value for values in durations.values() for value in values]
        summaries = {}
        for kind, values in durations.items():
            ordered = sorted(values)
            summaries[kind] = {"keys": len(ordered), "median_ms": round(statistics.median(ordered), 3),
                               "mean_ms": round(statistics.mean(ordered), 3), "total_ms": round(sum(ordered), 3),
                               "p95_ms": ordered[(len(ordered) * 95 - 1) // 100], "max_ms": max(ordered)}
        report[name] = summaries["typing"] if args.workload == "typing" else summaries
        print(f"{name}: {json.dumps(report[name])}", flush=True)
    (ARTIFACTS / f"{args.label}-summary.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
