"""Measure connector pointer-handler latency using an optimized release host."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "test/demo/draw"
WORK = ROOT / "temp/draw/performance"


def prepare(source, destination, dense):
    destination.mkdir(parents=True, exist_ok=True)
    for path in source.iterdir():
        if path.suffix in (".ls", ".css"):
            shutil.copyfile(path, destination / path.name)
    shutil.copyfile(SOURCE.parent / "mod_resources.ls", destination.parent / "mod_resources.ls")
    path = destination / "draw.ls"
    script = path.read_text()
    start, end = script.index("on pointermove(evt) {"), script.index("on pointerup(evt) {")
    handler = script[start:end]
    marker = "    return 'prevent-default'"
    if handler.count(marker) != 1:
        raise ValueError("unsupported Draw pointer handler")
    # Instrument a copy; clocks and output never enter the application's drag handler.
    handler = handler.replace("on pointermove(evt) {", "on pointermove(evt) {\n    let measured = clock()")
    handler = handler.replace(marker, '    print("DRAW_MOVE " ++ string((clock()-measured)*1000) ++ "\\n")\n' + marker)
    path.write_text(script[:start] + handler + script[end:])
    if dense:
        path = destination / "mod_model.ls"
        script = path.read_text()
        start, end = script.index("pub fn starter()"), script.index("pub fn showcase()")
        starter = '''pub fn starter() => drawing([
    *[for (i in 0 to 63) with_attrs(make_shape("n" ++ string(i+1),"rect",80+(i%8)*125,60+floor(i/8)*85,"Node " ++ string(i+1)),{width:80,height:40})],
    *[for (i in 1 to 63) edge("e" ++ string(i),"n" ++ string(i),"n" ++ string(i+1))]])
'''
        path.write_text(script[:start] + starter + script[end:])
    return destination / "draw.ls"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("lambda.exe"))
    parser.add_argument("--reference-dir", type=Path, help="saved Draw demo scripts")
    parser.add_argument("--reference-home", type=Path, help="saved lmd directory for those scripts")
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--output-dir", type=Path, default=WORK)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if b"VALIDATOR] validate_against_base_type" in binary.read_bytes():
        parser.error("performance measurements require a release host")
    if args.repetitions < 1 or (args.reference_home and not args.reference_dir):
        parser.error("use positive repetitions and pair reference-home with reference-dir")
    work = args.output_dir.resolve()
    records = []
    for dense in (False, True):
        case = "127-objects" if dense else "9-objects"
        cases = [("current", SOURCE, ROOT / "lmd")]
        if args.reference_dir:
            cases.insert(0, ("reference", args.reference_dir.resolve(),
                             args.reference_home.resolve() if args.reference_home else ROOT / "lmd"))
        prepared = [(name, prepare(source, work / case / name, dense), home) for name, source, home in cases]
        fixture = work / case / "events.json"
        events = [{"type": "advance_time", "ms": 50},
                  {"type": "select_option", "target": {"selector": "#object-list"}, "value": "e1"},
                  {"type": "mouse_down", "target": {"selector": "#paper", "offset_x": 142 if dense else 243,
                                                     "offset_y": 55 if dense else 236}}]
        events += [{"type": "mouse_move", "target": {"selector": "#paper", "offset_x": 380 + i % 8 * 4,
                                                     "offset_y": 110 + i % 6 * 4}} for i in range(24)]
        events += [{"type": "mouse_up", "target": {"selector": "#paper", "offset_x": 400, "offset_y": 130}},
                   {"type": "assert_attribute", "target": {"selector": "#draw"}, "attribute": "data-history", "equals": "1"},
                   {"type": "window_close"}]
        fixture.write_text(json.dumps({"name": case, "viewport": {"width": 1280, "height": 820}, "events": events}))
        for iteration in range(args.repetitions):
            for name, page, home in prepared[::1 if iteration % 2 == 0 else -1]:
                tag = work / case / f"{name}-{iteration}"
                env = dict(os.environ, LAMBDA_HOME=str(home), LAMBDA_LOG_LEVEL="ERROR",
                           LAMBDA_LOG_FILE=str(tag) + "-native.log", RADIANT_VIEW_TIMING_PATH=str(tag) + "-phases.tsv")
                with Path(str(tag) + ".log").open("w") as log:
                    run = subprocess.run([str(binary), "view", str(page), "--headless", "--event-file", str(fixture),
                                          "--event-result", str(tag) + ".json"], cwd=ROOT, env=env,
                                         stdout=log, stderr=subprocess.STDOUT, timeout=120)
                outcome = json.loads(Path(str(tag) + ".json").read_text())
                if run.returncode or outcome.get("result") != "PASS":
                    raise SystemExit(f"Draw replay failed: {tag}.log")
                samples = [float(v) for v in re.findall(r"DRAW_MOVE ([0-9.e+-]+)", Path(str(tag) + ".log").read_text())]
                if len(samples) < 24:
                    raise SystemExit(f"missing pointer samples: {tag}.log")
                record = {"case": case, "version": name, "iteration": iteration,
                          "median_ms": statistics.median(samples), "p95_ms": sorted(samples)[int(len(samples)*0.95)],
                          "samples_ms": samples}
                records.append(record)
                print(json.dumps({k: v for k, v in record.items() if k != "samples_ms"}), flush=True)
    (work / "results.json").write_text(json.dumps({"binary": str(binary), "repetitions": args.repetitions,
        "reference_dir": str(args.reference_dir) if args.reference_dir else None,
        "reference_home": str(args.reference_home) if args.reference_home else None, "records": records}, indent=2))


if __name__ == "__main__":
    main()
