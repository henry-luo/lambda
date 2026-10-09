#!/usr/bin/env python3
"""Author native input replays from the real, seeded fixed-step simulation."""
import argparse
import json
import math
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
DEMO = ROOT / "test/demo/doom"
TEMP = ROOT / "temp/doom"


def generate(kind, executable):
    route = json.loads((DEMO / f"reference/e1m1_{kind}_route.json").read_text())
    TEMP.mkdir(parents=True, exist_ok=True)
    (TEMP / "route-request.json").write_text(json.dumps(route))
    probe = subprocess.run([executable, "test/demo/doom/tools/_route_probe.ls"],
        cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    # Lambda's display formatter surrounds the JSON string with literal quotes.
    displayed = probe.stdout.strip()
    result = json.loads(displayed[1:-1])
    owner = {"selector": "#doom"}
    events = []

    def event(action, **fields):
        events.append({"type": action, **fields})

    def expect(attribute, value):
        event("assert_attribute", target=owner, attribute="data-" + attribute, equals=str(value))

    def boundary(row, mode):
        expect("mode", mode)
        for field in ["ticks", "health", "kills", "pickups", "floor"]:
            expect(field, row[field])
        for field in ["x", "y"]:
            expect(field, math.floor(row[field] + .5))
        expect("ammo", row["ammo"]["bullets"])

    event("advance_time", ms=40, steps=2)
    event("snapshot_state_store", target={"selector": "#scene"}, name="scene")
    event("click", target={"selector": "#start"})
    event("advance_time", ms=1, steps=1)
    elapsed_ticks = previous_ms = 0
    checkpoints = {14, 20, 25} if kind == "exit" else set()
    for index, segment in enumerate(route["segments"]):
        for key in segment["keys"]:
            event("key_down", key=key)
        elapsed_ticks += segment["ticks"]
        target_ms = math.ceil(elapsed_ticks * 1000 / 35)
        event("advance_time", ms=target_ms - previous_ms, steps=segment["ticks"])
        previous_ms = target_ms
        for key in segment["keys"]:
            event("key_up", key=key)
        if index in checkpoints:
            event("key_press", key="P")
            boundary(result["rows"][index], "paused")
            event("key_press", key="P")
            event("advance_time", ms=1, steps=1)
            elapsed_ticks = previous_ms = 0
    if kind == "exit":
        assert result["game"]["transition"]["map"] == "E1M2"
        event("key_press", key="P")
        boundary(result["rows"][-1], "paused")
        event("assert_state_store_snapshot", target={"selector": "#scene"}, name="scene")
        event("key_press", key="P")
        event("advance_time", ms=1, steps=1)
        event("advance_time", ms=1029, steps=36)
        expect("map", "E1M2")
        expect("generation", 2)
    else:
        assert result["game"]["mode"] == "dead"
        boundary(result["rows"][-1], "dead")
        event("key_press", key="R")
        for field, value in {"mode": "playing", "ticks": 0, "health": 100, "generation": 2}.items():
            expect(field, value)
        event("assert_state_store_snapshot", target={"selector": "#scene"}, name="scene")
    event("window_close")
    replay = {"name": f"E1M1 native {kind} route derived from fixed-step simulation",
        "html": "test/demo/doom/doom.ls", "viewport": {"width": 660, "height": 500},
        "default_timeout": 1, "input_turn_ms": 0, "events": events}
    (DEMO / f"replay/e1m1_{kind}.json").write_text(json.dumps(replay, indent=2) + "\n")
    print(f"{kind}: {len(events)} native events; {result['game']['ticks']} gameplay ticks")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=["exit", "death"])
    parser.add_argument("--executable", default=str(ROOT / "lambda.exe"))
    arguments = parser.parse_args()
    generate(arguments.kind, arguments.executable)
