#!/usr/bin/env python3
"""Replay native brush previews with an optimized host; instrument disposable copies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "test/demo/photos"


def prepare(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    for path in source.iterdir():
        if path.suffix in (".ls", ".css"):
            shutil.copyfile(path, destination / path.name)
    shutil.copyfile(ROOT / "test/demo/mod_resources.ls", destination.parent / "mod_resources.ls")
    path = destination / "photos.ls"
    script = path.read_text()
    start, end = script.index("on photos_frame(evt) {"), script.index("\non click(evt)")
    handler = script[start:end]
    measured = next(line for line in handler.splitlines() if "interaction.render(next)" in line)
    handler = handler.replace("    var next =", "    let measured = clock()\n    var next =", 1)
    handler = handler.replace(measured, measured + "\n    let edited = clock()", 1)
    marker = "    return 'handled'"
    at = handler.rindex(marker)
    handler = handler[:at] + ('    print("PHOTOS_FRAME " ++ string(evt.timeStamp) ++ " " ++ string((edited-measured)*1000) ++ " " ++ '
                              'string((clock()-edited)*1000) ++ "\\n")\n') + handler[at:]
    path.write_text(script[:start] + handler + script[end:])
    return path


def distribution(samples):
    return {"median_ms": statistics.median(samples),
            "p95_ms": sorted(samples)[min(len(samples)-1, int(len(samples)*0.95))],
            "max_ms": max(samples)}


def fingerprint(directory, suffixes):
    digest = hashlib.sha256()
    for path in sorted(directory.rglob("*")):
        if path.suffix in suffixes and path.is_file():
            digest.update(path.relative_to(directory).as_posix().encode())
            digest.update(path.read_bytes())
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "lambda.exe")
    parser.add_argument("--reference-dir", type=Path, help="saved Photos demo scripts")
    parser.add_argument("--reference-home", type=Path, help="saved lmd package directory")
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--layers", nargs="+", type=int, default=[1, 8])
    parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=768)
    parser.add_argument("--samples", type=int, default=40)
    parser.add_argument("--paint", action="store_true", help="force and verify every preview paint; collect native frame/paint profiles")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "temp/photos/performance/brush")
    args = parser.parse_args()
    if min(args.repetitions, args.width, args.height, args.samples, *args.layers) < 1:
        parser.error("counts and dimensions must be positive")
    if bool(args.reference_dir) != bool(args.reference_home):
        parser.error("pair reference-dir with reference-home")
    binary = args.binary.resolve()
    if b"VALIDATOR] validate_against_base_type" in binary.read_bytes():
        parser.error("use an optimized host: make build-release-compile in a worktree")
    work = args.output_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    versions = [("current", SOURCE, ROOT / "lmd")]
    if args.reference_dir:
        versions.insert(0, ("reference", args.reference_dir.resolve(), args.reference_home.resolve()))
    env = dict(os.environ, TMPDIR=str(ROOT / "temp"), LAMBDA_LOG_LEVEL="ERROR")
    records = []
    for layers in args.layers:
        alpha = 1.0 if layers == 1 else 0.7
        case = work / f"{layers}-layers"
        case.mkdir(exist_ok=True)
        source = case / "source.photos"
        generator = case / "_source.ls"
        generator.write_text(f'''import img: lambda.image
import project: lambda.image.project
pn main() {{
    let raster = img.blank({{width:{args.width},height:{args.height},color:[0.3,0.5,0.7,{alpha}]}})^
    let doc = img.document(img.stack([for (i in 1 to {layers})
        img.layer(raster,{{id:"layer-" ++ string(i),name:"Layer " ++ string(i)}})]),
        {{width:{args.width},height:{args.height}}})
    project.write(doc,{json.dumps(source.as_posix())})^
    return null
}}
''')
        with (case / "source.log").open("w") as log:
            subprocess.run([str(binary), "run", str(generator)], cwd=ROOT,
                           env=dict(env, LAMBDA_HOME=str(ROOT / "lmd")), stdout=log, stderr=log, check=True)
        events = [{"type": "advance_time", "ms": 50},
                  {"type": "click", "target": {"selector": "#tool-brush"}},
                  {"type": "mouse_down", "target": {"selector": "#preview", "offset_x": 100, "offset_y": 120}}]
        for i in range(args.samples):
            target = {"selector": "#preview", "offset_x": 110 + i % 40 * 6, "offset_y": 120 + i % 6 * 5}
            events.append({"type": "mouse_move", "target": target})
            if args.paint:
                events.append({"type": "assert_pixel", "target": target,
                               "min_r": 24, "max_r": 28, "min_g": 87, "max_g": 91, "min_b": 240, "max_b": 244})
        events += [{"type": "mouse_up", "target": {"selector": "#preview", "offset_x": 350, "offset_y": 140}},
                   {"type": "assert_attribute", "target": {"selector": "#photos"}, "attribute": "data-error", "equals": ""},
                   {"type": "assert_attribute", "target": {"selector": "#photos"}, "attribute": "data-history", "equals": "1"}]
        fixture = case / "events.json"
        fixture.write_text(json.dumps({"name": "brush latency", "viewport": {"width": 1280, "height": 820}, "events": events}))
        prepared = [(name, prepare(directory, case / name / "app"), home) for name, directory, home in versions]
        for repetition in range(args.repetitions):
            for name, page, home in prepared[::1 if repetition % 2 == 0 else -1]:
                tag = case / f"{name}-{repetition}"
                with Path(str(tag) + ".log").open("w") as log:
                    run = subprocess.Popen([str(binary), "view", str(page), "--headless", "--event-file", str(fixture),
                                          "--event-result", str(tag) + ".json", *(["--event-log"] if args.paint else [])], cwd=ROOT,
                        env=dict(env, PHOTOS_SOURCE=str(source), LAMBDA_HOME=str(home),
                                 LAMBDA_LOG_LEVEL="NOTICE" if args.paint else "ERROR",
                                 RADIANT_FRAME_PROFILE="1" if args.paint else "0", LAMBDA_LOG_FILE=str(tag) + "-native.log"),
                        stdout=log, stderr=log)
                    try:
                        run.wait(timeout=300)
                    except subprocess.TimeoutExpired:
                        run.kill()
                        run.wait()
                        raise SystemExit(f"Photos replay timed out: {tag}.log")
                result = json.loads(Path(str(tag) + ".json").read_text())
                rows = [tuple(map(float, row)) for row in re.findall(
                    r"PHOTOS_FRAME ([0-9.e+-]+) ([0-9.e+-]+) ([0-9.e+-]+)", Path(str(tag) + ".log").read_text())]
                samples = [(edit, publish) for _, edit, publish in rows]
                if run.returncode or result.get("result") != "PASS" or len(samples) != args.samples:
                    raise SystemExit(f"Photos replay failed or missed frames: {tag}.log")
                record = {"layers": layers, "source_alpha": alpha, "version": name, "repetition": repetition,
                          "edit": distribution([x for x, _ in samples]),
                          "publish": distribution([y for _, y in samples]),
                          "handler": distribution([x+y for x, y in samples]), "samples_ms": samples}
                if args.paint:
                    journal = ROOT / "temp" / f"events_{run.pid}_photos.ls.jsonl"
                    shutil.move(journal, str(tag) + ".events.jsonl")
                    paints, active, pending, event = [], False, False, None
                    for line in Path(str(tag) + ".events.jsonl").read_text().splitlines():
                        entry = json.loads(line)
                        if entry["type"] == "input.raw":
                            event = entry["data"]["event"]
                            pending |= active and event == "mouse_move"
                            if event == "mouse_up":
                                active = False
                        elif entry["type"] == "hit.target" and event == "mouse_down":
                            active = entry["data"]["target"].get("author_id") == "preview"
                        elif entry["type"] == "render.stats" and pending:
                            paints.append(entry["data"]["total_ms"])
                            pending = False
                    profiles = [dict(re.findall(r"(\w+)=([0-9.]+)", line)) for line in
                                Path(str(tag) + "-native.log").read_text().splitlines() if "[FRAME_PROFILE]" in line]
                    by_time = {round(float(p["document_timestamp_ms"]), 3): p for p in profiles}
                    frames = [by_time[round(timestamp, 3)] for timestamp, _, _ in rows]
                    if len(paints) != args.samples:
                        raise SystemExit(f"Missing per-preview paints: {tag}.events.jsonl")
                    record.update(frame=distribution([float(p["total_ms"]) for p in frames]),
                                  layout=distribution([float(p["layout_ms"]) for p in frames]),
                                  paint=distribution(paints),
                                  cpu_frame_and_paint=distribution([float(p["total_ms"])+paint for p, paint in zip(frames, paints)]),
                                  frame_mutations=sum(int(p["mutations"]) for p in frames))
                records.append(record)
                print(json.dumps({k: v for k, v in record.items() if k != "samples_ms"}), flush=True)
    report = {"profile": "release", "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
              "platform": platform.platform(), "machine": platform.machine(), "cpu_count": os.cpu_count(),
              "revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "versions": [{"name": name, "demo_sha256": fingerprint(directory, (".ls", ".css")),
                            "package_sha256": fingerprint(home / "package", (".ls",))} for name, directory, home in versions],
              "execution_backend": env.get("LAMBDA_EXEC_BACKEND", "auto"),
              "runtime_environment": {key: env[key] for key in ("LAMBDA_GC_FORCE_EVERY", "LAMBDA_GC_POISON_FREED", "LAMBDA_DISABLE_MIR_CACHE") if key in env},
              "scenario": vars(args), "records": records,
              "notes": ["Alternating reference/current processes use the same optimized host.",
                        "Samples measure preview editing and publication, including canvas upload.",
                        "Handler samples exclude event dispatch, later layout/paint, display refresh and input queuing.",
                        "With --paint, frame and paint profiles additionally measure native reconciliation and CPU rendering; matched callback timestamps exclude activation/input-drain turns.",
                        "These are synchronous automation replays, not hardware input-to-display measurements."]}
    (work / "results.json").write_text(json.dumps(report, indent=2, default=str) + "\n")


if __name__ == "__main__":
    main()
