#!/usr/bin/env python3
"""Compare release rendering work; all fixtures and receipts stay under ./temp/."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import time


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ("vector", "deep", "mixed", "pressure-128", "pressure-512")


def svg_shapes(count, pressure=False):
    path = "M1 1 C18 0 0 18 19 19 L1 19 Z"
    if pressure:
        path = "M0 0 " + " ".join(
            f"C{j % 18} {j % 17} {(j + 4) % 18} {(j + 6) % 17} "
            f"{(j + 9) % 18} {(j + 3) % 17}" for j in range(24)) + "Z"
    rows = []
    for start in range(0, count, 32):
        shapes = []
        for index in range(start, min(start + 32, count)):
            color = f"rgb({index % 256},{index // 2 % 256},{index // 3 % 256})"
            shapes.append(f'<path d="{path}" transform="translate({index % 32 * 25} '
                          f'{index // 32 * 25})" fill="{color if pressure else "url(#g)"}" '
                          f'stroke="{color}" stroke-width="{1 + index * .001}" '
                          'stroke-dasharray="2 3"/>')
        rows.append('<g clip-path="url(#c)">' + "".join(shapes) + "</g>")
    return ('<svg width="800" height="600"><defs><clipPath id="c">'
            '<rect width="800" height="600" rx="8"/></clipPath>'
            '<linearGradient id="g"><stop stop-color="#e83245"/>'
            '<stop offset="1" stop-color="#3278e8"/></linearGradient></defs>'
            + "".join(rows) + "</svg>")


def fixture_source(name):
    if name.startswith("pressure-"):
        content = svg_shapes(int(name.split("-")[1]), True)
    elif name == "deep":
        content = ('<div style="margin:1px;padding:1px;border:1px solid #4278ab">' * 48
                   + svg_shapes(128) + "</div>" * 48)
    else:
        content = svg_shapes(768)
        if name == "mixed":
            content += "".join(f'<p style="position:absolute;left:8px;top:{i * 24}px;'
                               'margin:0">Shared geometry and live text 0123456789</p>'
                               for i in range(24))
    font = (ROOT / "test/layout/data/font/LiberationSans-Regular.ttf").as_uri()
    return ('<!doctype html><html><head><style>@font-face{font-family:RenderingBench;'
            f'src:url("{font}")' + '}</style></head><body style="margin:0;background:white;'
            'font:14px RenderingBench">' + content + "</body></html>")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def profile_frames(output):
    frames, current = [], {}
    for line in output.splitlines():
        stage = re.search(r"\[RENDER_METRICS\] stage=(\w+) ms=([\d.]+)", line)
        if stage:
            if stage[1] == "setup_clear":
                if current:
                    frames.append(current)
                current = {}
            current[stage[1]] = float(stage[2])
        counter = re.search(r"\[RENDER_METRICS\] (\w+)=(\d+)$", line)
        if counter:
            current[counter[1]] = int(counter[2])
    if current:
        frames.append(current)
    return frames


def run_render(args, directory, name, scale, variant, binary, profiled=False):
    label = f"{name}-{scale}-{variant}"
    image = directory / (label + ".png")
    result = directory / (label + "-events-result.json")
    for stale in (image, result):
        stale.unlink(missing_ok=True)
    source = directory / (name + ".html")
    options = []
    if not profiled:
        options.append("--no-log")
    if args.mode == "warm":
        events = directory / (label + "-events.json")
        events.write_text(json.dumps(dict(viewport=dict(width=800, height=600),
            device_scale=scale, events=[dict(type="render", file=str(image))
                                        for _ in range(args.frames)])) + "\n")
        command = [str(binary), "view", str(source), "--headless", "--event-file", str(events),
                   "--event-result", str(result), *options]
    else:
        command = [str(binary), "render", str(source), "-o", str(image), "-vw", "800",
                   "-vh", "600", "-s", str(scale), *options]
    env = dict(os.environ, TMPDIR=str(ROOT / "temp"), RADIANT_RENDER_THREADS=str(args.threads))
    # normal samples exclude logging; separate profiles identify the work eliminated.
    env.pop("RADIANT_RENDER_METRICS", None)
    if args.mode == "warm":
        env["RADIANT_RECORDING_CACHE"] = variant
    if profiled:
        env.update(RADIANT_RENDER_METRICS="1", LAMBDA_LOG_FILE=str(directory / (label + ".log")))
    start = time.perf_counter()
    process = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=120)
    elapsed = time.perf_counter() - start
    output = (process.stdout + process.stderr).decode(errors="replace")
    (directory / (label + ".stdout")).write_text(output)
    if process.returncode or not image.exists():
        raise RuntimeError(f"{label}: rendering failed ({process.returncode}); see {label}.stdout")
    if args.mode == "warm":
        receipt = json.loads(result.read_text())
        if receipt.get("result") != "PASS" or receipt.get("events") != args.frames:
            raise RuntimeError(f"{label}: incomplete event replay")
    row = dict(fixture=name, scale=scale, variant=variant, wall_seconds=elapsed, png=digest(image))
    if profiled:
        row["frames"] = profile_frames(output)
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=ROOT / "lambda.exe")
    parser.add_argument("--control", type=Path, help="release control binary for cold comparisons")
    parser.add_argument("--mode", choices=("warm", "cold"), default="warm")
    parser.add_argument("--fixtures", nargs="+", choices=FIXTURES, default=FIXTURES)
    parser.add_argument("--scales", nargs="+", type=int, choices=(1, 2), default=(1, 2))
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--frames", type=int, default=12)
    parser.add_argument("--threads", type=int, default=1, help="renderer threads (default: serial)")
    parser.add_argument("--profile", action="store_true", help="collect separate opt-in metric runs")
    parser.add_argument("--out", type=Path, default=ROOT / "temp/rendering-benchmark")
    args = parser.parse_args()
    directory = args.out.resolve()
    if ROOT / "temp" not in directory.parents:
        parser.error("--out must be below this checkout's ./temp/")
    if args.repeats < 1 or args.frames < 3 or args.threads < 0:
        parser.error("use positive --repeats, at least three --frames and nonnegative --threads")
    if args.mode == "warm" and args.control:
        parser.error("--control applies to --mode cold; warm compares the same binary on/off")
    executable = args.executable.resolve()
    variants = {"off": executable, "on": executable} if args.mode == "warm" else {"candidate": executable}
    if args.control:
        variants["control"] = args.control.resolve()
    for binary in set(variants.values()):
        # this is the project's documented debug marker; always build a release binary first.
        if b"VALIDATOR] validate_against_base_type" in binary.read_bytes():
            parser.error(f"{binary} contains the debug marker; run make build-release-compile")
    directory.mkdir(parents=True, exist_ok=True)
    for name in args.fixtures:
        (directory / (name + ".html")).write_text(fixture_source(name))
    report = dict(protocol="alternating-order fresh processes; 800x600; wall includes startup/PNG encoding; "
                  "OS caches uncontrolled; separate profiles include logging; exclude first two frames when analyzing warm profiles",
                  mode=args.mode, frames=args.frames, threads=args.threads,
                  platform=platform.platform(), cpus=os.cpu_count(),
                  environment={key: os.environ.get(key) for key in ("RADIANT_SVG_LAYER", "MEMTRACK_MODE")},
                  binaries={name: dict(path=str(binary), sha256=digest(binary)) for name, binary in variants.items()},
                  fixtures={name: digest(directory / (name + ".html")) for name in args.fixtures}, samples=[], profiles=[])
    receipt = directory / "results.json"
    for name in args.fixtures:
        for scale in args.scales:
            for repeat in range(args.repeats):
                order = list(variants.items())
                if repeat % 2:
                    order.reverse()
                for variant, binary in order:
                    row = run_render(args, directory, name, scale, variant, binary)
                    report["samples"].append(dict(row, repeat=repeat))
                    receipt.write_text(json.dumps(report, indent=2) + "\n")
            rows = [r for r in report["samples"] if r["fixture"] == name and r["scale"] == scale]
            parity = len({r["png"] for r in rows}) == 1
            medians = {variant: round(statistics.median(r["wall_seconds"] for r in rows
                                                       if r["variant"] == variant) * 1000, 3) for variant in variants}
            print(name, scale, "median_ms=", medians, "identical_png=", parity, flush=True)
            if not parity:
                raise RuntimeError(f"{name}/{scale}: PNGs differ; see {receipt}")
            if args.profile:
                for variant, binary in variants.items():
                    profile = run_render(args, directory, name, scale, variant, binary, True)
                    if profile["png"] != rows[0]["png"] or not profile["frames"]:
                        raise RuntimeError(f"{name}/{scale}/{variant}: profile changed pixels or emitted no metrics")
                    report["profiles"].append(profile)
    for variant, binary in variants.items():
        if digest(binary) != report["binaries"][variant]["sha256"]:
            raise RuntimeError(f"{binary} changed during measurement; rerun with a frozen release executable")
    receipt.write_text(json.dumps(report, indent=2) + "\n")
    print(receipt)


if __name__ == "__main__":
    main()
