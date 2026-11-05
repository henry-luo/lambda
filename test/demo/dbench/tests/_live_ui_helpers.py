"""Run a live replay with a private, short-lived event fixture."""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]


def run_events(name, events, timeout=120):
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=ROOT / "temp",
                                     prefix="dbench-live-events-", suffix=".json", delete=False) as fixture:
        path = Path(fixture.name)
        json.dump({"name": name, "html": "test/demo/dbench/dbench.ls",
                   "viewport": {"width": 1280, "height": 800}, "input_turn_ms": 0, "events": events}, fixture)
    try:
        subprocess.run([str(ROOT / "lambda.exe"), "demo", "dbench", "--headless", "--no-log",
                        "--event-file", str(path)], cwd=ROOT, check=True, timeout=timeout)
    finally:
        path.unlink(missing_ok=True)
