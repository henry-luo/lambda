#!/usr/bin/env python3
"""Summarize real native frame records during the held-turn replay interval."""
import argparse
import hashlib
import json
import statistics
from pathlib import Path
from profile_fields import profile_records


def percentile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("event_log", type=Path)
    parser.add_argument("--binary", type=Path, default=Path("lambda.exe"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--engine-log", type=Path)
    parser.add_argument("--application-log", type=Path)
    args = parser.parse_args()
    records = [json.loads(line) for line in args.event_log.open()]
    keys = [r for r in records if r["type"] == "input.raw"
            and r["data"].get("key") == 262]
    begin = next(r["time"]["mono_ms"] for r in keys if r["data"]["event"] == "key_down")
    end = next(r["time"]["mono_ms"] for r in keys if r["data"]["event"] == "key_up")
    frames = [r for r in records if r["type"] == "render.stats"
              and begin < r["time"]["mono_ms"] < end]
    if len(frames) < 2:
        raise ValueError("Need at least two real native frames in the turn interval")
    timestamps = [r["time"]["mono_ms"] for r in frames]
    intervals = [b - a for a, b in zip(timestamps, timestamps[1:])]
    surfaces = {(r["data"]["surface_width"], r["data"]["surface_height"]) for r in frames}
    if len(surfaces) != 1:
        raise ValueError("Device surface changed during the measured interval")
    result = {
        "event_log": str(args.event_log), "binary": str(args.binary),
        "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        "session": records[0], "sample_count": len(frames),
        "sample_duration_ms": timestamps[-1] - timestamps[0],
        "warmup_duration_ms": begin, "held_turn_duration_ms": end - begin,
        "presented_fps": (len(frames) - 1) * 1000 / (timestamps[-1] - timestamps[0]),
        "frame_interval_ms": {"median": statistics.median(intervals), "p95": percentile(intervals, .95)},
        "render": {},
        "surface": {key: frames[0]["data"][key] for key in ("surface_width", "surface_height")},
        "measurement": "Real native monotonic clock; held ArrowRight after replay warmup. No virtual waits.",
    }
    for field in ("record_ms", "replay_ms", "total_ms", "css3d_compose_ms",
                  "css3d_planes", "css3d_fragments", "display_list_items"):
        values = [r["data"][field] for r in frames]
        result["render"][field] = {"median": statistics.median(values), "p95": percentile(values, .95)}
    if args.engine_log:
        # Raw input uses the native clock; script event stamps are document-relative.
        native_begin = next(r["data"]["timestamp"] * 1000 for r in keys if r["data"]["event"] == "key_down")
        native_end = next(r["data"]["timestamp"] * 1000 for r in keys if r["data"]["event"] == "key_up")
        groups = {"DOOM_PROFILE": [], "FRAME_PROFILE": []}
        for marker, values in profile_records(args.engine_log, args.application_log):
            if marker in groups and native_begin < values["timestamp_ms"] < native_end:
                groups[marker].append(values)
        result["engine_log"] = str(args.engine_log)
        result["application_log"] = str(args.application_log) if args.application_log else None
        for marker, fields in (("DOOM_PROFILE", ("simulation_ms", "presentation_ms", "visible_planes", "total_planes")),
                               ("FRAME_PROFILE", ("total_ms", "cascade_ms", "layout_ms", "dom_bytes", "view_bytes", "layout_before", "layout_after"))):
            samples = groups[marker]
            if not samples:
                raise ValueError(f"No {marker} samples inside the native input interval")
            result[marker.lower()] = {"sample_count": len(samples)}
            for field in fields:
                values = [sample[field] for sample in samples]
                result[marker.lower()][field] = {"median": statistics.median(values), "p95": percentile(values, .95),
                                                "min": min(values), "max": max(values)}
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
