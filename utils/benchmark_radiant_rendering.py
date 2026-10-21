#!/usr/bin/env python3
"""Compare release rendering work; all fixtures and receipts stay under ./temp/."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess

from benchmark_process import run_measured


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ("vector", "deep", "mixed", "pressure-128", "pressure-512")
FONT = ROOT / "test/layout/data/font/LiberationSans-Regular.ttf"
STAGES = ("setup_clear", "record_lower", "overlays_validate", "retained_capture",
          "replay", "media_trace", "encode_write", "teardown")


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
    font = FONT.as_uri()
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
            if stage[1] in current:
                raise ValueError(f"duplicate frame stage: {stage[1]}")
            position = sum(key in STAGES for key in current)
            if position >= len(STAGES) or stage[1] != STAGES[position]:
                raise ValueError(f"unexpected frame stage: {stage[1]}")
            current[stage[1]] = float(stage[2])
        elif "[RENDER_METRICS]" in line:
            for key, value in re.findall(r"(\w+)=(\d+(?:\.\d+)?)(?=\s|$)", line):
                current[key] = float(value) if "." in value else int(value)
    if current:
        frames.append(current)
    required = (*STAGES, "scope_total_ms", "tracked_start_bytes", "tracked_end_bytes",
                "tracked_process_peak_bytes", "full_frame_reuses")
    for frame in frames:
        if any(key not in frame for key in required):
            raise ValueError("incomplete rendering profile frame")
        # checkpoints round independently; their sum must still fit the outer scope.
        if sum(frame[key] for key in STAGES) > frame["scope_total_ms"] + .001 * len(STAGES):
            raise ValueError("frame stages exceed their measured scope")
        if frame["tracked_end_bytes"] <= 0 or frame["tracked_process_peak_bytes"] < \
                max(frame["tracked_start_bytes"], frame["tracked_end_bytes"]):
            raise ValueError("profile allocation tracking is absent or inconsistent")
    return frames


def distribution(values):
    values = sorted(values)
    if not values or any(not math.isfinite(value) for value in values):
        raise ValueError("expected finite nonempty measurement samples")
    return dict(count=len(values), median=statistics.median(values), minimum=values[0],
                p95=values[math.ceil(len(values) * .95) - 1], maximum=values[-1])


def summarize_profile(frames, warmup):
    steady = frames[warmup:]
    if not steady:
        raise ValueError("warm-up discards all profiled frames")
    return dict(total_frames=len(frames), discarded_frames=warmup, measured_frames=len(steady),
                stages_ms={key: distribution(frame[key] for frame in steady) for key in STAGES},
                frame_work_ms=distribution(sum(frame[key] for key in STAGES if key != "encode_write")
                                           for frame in steady),
                scope_total_ms=distribution(frame["scope_total_ms"] for frame in steady),
                tracked_end_bytes=distribution(frame["tracked_end_bytes"] for frame in steady),
                tracked_process_peak_bytes=max(frame["tracked_process_peak_bytes"] for frame in steady),
                full_frame_reuses=sum(frame["full_frame_reuses"] for frame in steady))


def source_identity():
    changed = subprocess.check_output(["git", "diff", "--name-only", "HEAD"], cwd=ROOT, text=True).splitlines()
    changed += subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard"], cwd=ROOT, text=True).splitlines()
    suffixes = {".c", ".cpp", ".h", ".hpp", ".py", ".js", ".cjs", ".ls", ".json", ".css", ".html"}
    return dict(revision=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                dirty_sources={name: digest(ROOT / name) if (ROOT / name).is_file() else None
                               for name in sorted(set(changed)) if Path(name).suffix in suffixes},
                measurement_sources={str(path.relative_to(ROOT)): digest(path) for path in
                    (Path(__file__).resolve(), ROOT / "utils/benchmark_process.py", FONT)})


def run_render(args, directory, name, scale, variant, binary, repeat=None, profiled=False):
    label = f"profile-{variant}" if profiled else f"run-{repeat:03d}-{variant}"
    run = directory / f"{name}-{scale}" / label
    run.mkdir(parents=True)  # never replace the evidence from an earlier sample
    image = run / "output.png"
    result = run / "events-result.json"
    source = directory / (name + ".html")
    options = []
    if not profiled:
        options.append("--no-log")
    if args.mode == "warm":
        events = run / "events.json"
        events.write_text(json.dumps(dict(viewport=dict(width=800, height=600),
            device_scale=scale, events=[dict(type="render", file=str(image))
                                        for _ in range(args.frames)])) + "\n")
        command = [str(binary), "view", str(source), "--headless", "--event-file", str(events),
                   "--event-result", str(result), *options]
    else:
        command = [str(binary), "render", str(source), "-o", str(image), "-vw", "800",
                   "-vh", "600", "-s", str(scale), *options]
    env = dict(os.environ, TMPDIR=str(ROOT / "temp"), RADIANT_RENDER_THREADS=str(args.threads),
               MEMTRACK_MODE="STATS" if profiled else "OFF")
    # normal samples exclude logging; separate profiles identify the work eliminated.
    for key in ("RADIANT_RENDER_METRICS", "RADIANT_RECORDING_CACHE", "RADIANT_VIEW_TIMING_PATH"):
        env.pop(key, None)
    if args.mode == "warm":
        env["RADIANT_RECORDING_CACHE"] = variant
    if profiled:
        env.update(RADIANT_RENDER_METRICS="1", LAMBDA_LOG_FILE=str(run / "native.log"))
    (run / "command.json").write_text(json.dumps(dict(argv=command,
        environment={key: env.get(key) for key in ("TMPDIR", "RADIANT_RENDER_THREADS", "RADIANT_SVG_LAYER",
            "RADIANT_RECORDING_CACHE", "RADIANT_RENDER_METRICS", "MEMTRACK_MODE", "LAMBDA_LOG_FILE")}), indent=2) + "\n")
    with (run / "stdout.log").open("wb") as output:
        measured = run_measured(command, cwd=ROOT, env=env, stdout=output,
                                stderr=subprocess.STDOUT, timeout=args.timeout)
    row = dict(fixture=name, scale=scale, variant=variant, repeat=repeat,
               artifacts=str(run.relative_to(directory)), **measured)
    (run / "measurement.json").write_text(json.dumps(row, indent=2) + "\n")
    if measured["timed_out"] or measured["exit_code"] or not image.exists():
        raise RuntimeError(f"{run}: rendering failed; see measurement.json and stdout.log")
    if args.mode == "warm":
        receipt = json.loads(result.read_text())
        if receipt.get("result") != "PASS" or receipt.get("events") != args.frames:
            raise RuntimeError(f"{run}: incomplete event replay")
    row["png"] = digest(image)
    if profiled:
        row["frames"] = profile_frames((run / "stdout.log").read_text(errors="replace"))
        expected = args.frames + 1 if args.mode == "warm" else 1
        if len(row["frames"]) != expected:
            raise ValueError(f"{run}: expected {expected} complete profile frames, found {len(row['frames'])}")
        row["summary"] = summarize_profile(row["frames"], args.warmup_frames if args.mode == "warm" else 0)
    (run / "measurement.json").write_text(json.dumps(row, indent=2) + "\n")
    return row


def summarize_samples(rows, variants, repeats):
    if len({row["png"] for row in rows}) != 1:
        raise ValueError("sample PNGs differ")
    summary = dict(pixel_parity=True, variants={}, paired_ratios={})
    metrics = dict(wall_ms=lambda row: row["wall_seconds"] * 1000,
                   peak_rss_bytes=lambda row: row["peak_rss_bytes"],
                   cpu_ms=lambda row: None if row["user_seconds"] is None else
                       (row["user_seconds"] + row["system_seconds"]) * 1000)
    indexed = {}
    for variant in variants:
        selected = [row for row in rows if row["variant"] == variant]
        if len(selected) != repeats or {row["repeat"] for row in selected} != set(range(repeats)):
            raise ValueError("missing or duplicate repeat samples")
        indexed[variant] = {row["repeat"]: row for row in selected}
        summary["variants"][variant] = {key: None if any(getter(row) is None for row in selected)
            else distribution(getter(row) for row in selected) for key, getter in metrics.items()}
    numerator, denominator = ("on", "off") if "on" in indexed else ("candidate", "control")
    if denominator in indexed:
        summary.update(ratio_numerator=numerator, ratio_denominator=denominator)
        for key, getter in metrics.items():
            pairs = [(getter(indexed[numerator][i]), getter(indexed[denominator][i])) for i in range(repeats)]
            summary["paired_ratios"][key] = None if any(a is None or b is None or b <= 0 for a, b in pairs) \
                else distribution(a / b for a, b in pairs)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=ROOT / "lambda.exe")
    parser.add_argument("--control", type=Path, help="release control binary for cold comparisons")
    parser.add_argument("--mode", choices=("warm", "cold"), default="warm")
    parser.add_argument("--fixtures", nargs="+", choices=FIXTURES, default=FIXTURES)
    parser.add_argument("--scales", nargs="+", type=int, choices=(1, 2), default=(1, 2))
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--frames", type=int, default=12)
    parser.add_argument("--warmup-frames", type=int, default=2, help="prefix discarded from warm profiles, including initial presentation")
    parser.add_argument("--timeout", type=float, default=120, help="per-process timeout in seconds")
    parser.add_argument("--threads", type=int, default=1, help="renderer threads (default: serial)")
    parser.add_argument("--profile", action="store_true", help="collect separate opt-in metric runs")
    parser.add_argument("--out", type=Path, default=ROOT / "temp/rendering-benchmark")
    args = parser.parse_args()
    directory = args.out.resolve()
    if ROOT / "temp" not in directory.parents:
        parser.error("--out must be below this checkout's ./temp/")
    if args.repeats < 1 or args.frames < 3 or args.threads < 0 or args.timeout <= 0 or \
        not math.isfinite(args.timeout) or args.warmup_frames < 0 or args.warmup_frames > args.frames:
        parser.error("use positive repeats/timeout, at least three frames, nonnegative threads and a warm-up shorter than the profile")
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
    if any(directory.iterdir()):
        parser.error("--out must be a new or empty directory; earlier receipts are never overwritten")
    for name in args.fixtures:
        (directory / (name + ".html")).write_text(fixture_source(name))
    report = dict(version=2, status="running", recorded_at=datetime.now(timezone.utc).isoformat(),
                  protocol="alternating-order fresh processes; 800x600; wall includes startup/warm-up/PNG encoding; "
                  "exit observation polls at 1 ms; OS caches uncontrolled; separate profiles include logging/allocation tracking",
                  mode=args.mode, frames=args.frames, threads=args.threads,
                  warmup_frames=args.warmup_frames if args.mode == "warm" else 0,
                  normal_memtrack_mode="OFF", profile_memtrack_mode="STATS",
                  statistics="median/minimum/nearest-rank p95/maximum; ratios pair matching repeat indices",
                  source=source_identity(),
                  measurement_limits=["peak RSS is a per-process high-water mark, not steady retained or native-only memory",
                    "profile frame work sums outer checkpoint intervals except encode/write; logging/allocation tracking affect timing",
                    "tracked process peak covers only enabled Lambda allocators; ThorVG allocation attribution is unobserved",
                    "these headless samples do not measure input-to-display latency"],
                  platform=platform.platform(), cpus=os.cpu_count(),
                  environment={key: os.environ.get(key) for key in ("RADIANT_SVG_LAYER", "MEMTRACK_MODE")},
                  binaries={name: dict(path=str(binary), sha256=digest(binary)) for name, binary in variants.items()},
                  fixtures={name: digest(directory / (name + ".html")) for name in args.fixtures},
                  samples=[], profiles=[], summaries=[])
    receipt = directory / "results.json"
    for name in args.fixtures:
        for scale in args.scales:
            for repeat in range(args.repeats):
                order = list(variants.items())
                if repeat % 2:
                    order.reverse()
                for variant, binary in order:
                    row = run_render(args, directory, name, scale, variant, binary, repeat)
                    report["samples"].append(row)
                    receipt.write_text(json.dumps(report, indent=2) + "\n")
            rows = [r for r in report["samples"] if r["fixture"] == name and r["scale"] == scale]
            summary = dict(fixture=name, scale=scale, **summarize_samples(rows, variants, args.repeats))
            print(name, scale, "median_wall_ms=", {variant: round(value["wall_ms"]["median"], 3)
                  for variant, value in summary["variants"].items()}, "identical_png=True", flush=True)
            if args.profile:
                for variant, binary in variants.items():
                    profile = run_render(args, directory, name, scale, variant, binary, profiled=True)
                    if profile["png"] != rows[0]["png"] or not profile["frames"]:
                        raise RuntimeError(f"{name}/{scale}/{variant}: profile changed pixels or emitted no metrics")
                    report["profiles"].append(profile)
            report["summaries"].append(summary)
            receipt.write_text(json.dumps(report, indent=2) + "\n")
    for variant, binary in variants.items():
        if digest(binary) != report["binaries"][variant]["sha256"]:
            raise RuntimeError(f"{binary} changed during measurement; rerun with a frozen release executable")
    if source_identity() != report["source"]:
        raise RuntimeError("source identity changed during measurement; repeat against frozen inputs")
    if any(digest(directory / (name + ".html")) != sha for name, sha in report["fixtures"].items()):
        raise RuntimeError("benchmark fixtures changed during measurement")
    report["status"] = "complete"
    receipt.write_text(json.dumps(report, indent=2) + "\n")
    print(receipt)


if __name__ == "__main__":
    main()
