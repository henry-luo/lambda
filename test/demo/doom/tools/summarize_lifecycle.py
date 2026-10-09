#!/usr/bin/env python3
"""Record per-map ownership plateaus across the native three-episode replay."""
import argparse
import hashlib
import json
from pathlib import Path
from profile_fields import profile_records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine_log", type=Path)
    parser.add_argument("event_result", type=Path)
    parser.add_argument("--binary", type=Path, default=Path("lambda.exe"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--application-log", type=Path)
    args = parser.parse_args()
    text = args.engine_log.read_text()
    if args.application_log:
        text += args.application_log.read_text()
    event_result = json.loads(args.event_result.read_text())
    if event_result["result"] != "PASS" or "[MEMTRACK_LIVE] bytes=0 count=0" not in text:
        raise ValueError("Replay or tracked close ownership failed")
    generations = {}
    generation = None
    for marker, fields in profile_records(args.engine_log, args.application_log):
        if marker == "DOOM_PROFILE":
            generation = int(fields["generation"])
            entry = generations.setdefault(generation, {"map": fields["map"], "frames": [], "allocators": {}})
            if entry["map"] != fields["map"]:
                raise ValueError("Generation reused across maps")
        elif generation is not None and marker == "FRAME_PROFILE":
            generations[generation]["frames"].append(fields)
        elif generation is not None and marker == "FRAME_MEMORY":
            generations[generation]["allocators"].setdefault(fields["allocator"], []).append(
                {key: int(fields[key]) for key in ("reserved", "live", "peak")})
    expected = list(range(2, 29))
    if sorted(generations) != expected:
        raise ValueError(f"Expected 27 replaced map generations, got {sorted(generations)}")
    cycles = []
    for cycle in range(3):
        maps = []
        allocators = {}
        for index in range(9):
            generation = 2 + cycle * 9 + index
            entry = generations[generation]
            if entry["map"] != f"E1M{index + 1}" or not entry["frames"]:
                raise ValueError(f"Missing expected map/frame samples in generation {generation}")
            frames = entry["frames"]
            maps.append({"map": entry["map"], "generation": generation, "sample_count": len(frames),
                         "dom_bytes": {"last": int(frames[-1]["dom_bytes"]), "max": int(max(frame["dom_bytes"] for frame in frames))},
                         "view_bytes": {"last": int(frames[-1]["view_bytes"]), "max": int(max(frame["view_bytes"] for frame in frames))}})
            for label, samples in entry["allocators"].items():
                peak = allocators.setdefault(label, {"reserved": 0, "live": 0, "peak": 0, "sample_count": 0})
                for sample in samples:
                    for field in ("reserved", "live", "peak"):
                        peak[field] = max(peak[field], sample[field])
                    peak["sample_count"] += 1
        cycles.append({"cycle": cycle + 1, "maps": maps, "allocators": allocators})
    growth = [{"map": third["map"], **{field: third[field]["last"] - second[field]["last"]
               for field in ("dom_bytes", "view_bytes")}}
              for second, third in zip(cycles[1]["maps"], cycles[2]["maps"])]
    result = {"binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              "engine_log": str(args.engine_log), "event_result": event_result,
              "application_log": str(args.application_log) if args.application_log else None,
              "forced_gc_every": 5000, "poison_freed": True,
              "closed_tracked_live_bytes": 0, "closed_tracked_allocation_count": 0,
              "cycles": cycles, "last_cycle_growth_by_map": growth,
              "measurement": "Generation-aligned native ownership samples; compare identical maps after the first complete warmup episode."}
    output = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()
