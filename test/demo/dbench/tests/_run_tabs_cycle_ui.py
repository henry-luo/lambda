#!/usr/bin/env python3
"""Exercise repeated tab creation and cancellation without retaining their views."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
fixture = root / "temp/dbench-tabs-cycle.json"
events = [{"type": "assert_attribute", "target": {"selector": "#dbench"},
           "attribute": "data-rows", "equals": "256", "assert_timeout": 30000}]
for tab in range(2, 12):
    events.extend([
        {"type": "click", "target": {"selector": "#tab-new"}},
        {"type": "assert_attribute", "target": {"selector": "#dbench"},
         "attribute": "data-active-tab", "equals": str(tab)},
        {"type": "assert_attribute", "target": {"selector": "#dbench"},
         "attribute": "data-connected", "equals": "true", "assert_timeout": 30000},
        {"type": "click", "target": {"selector": "#tab-1"}},
        {"type": "click", "target": {"selector": f"[data-tab-close='{tab}']"}},
        {"type": "assert_count", "target": {"selector": "[role='tab']"}, "equals": 1},
    ])
events.append({"type": "assert_script", "js": "document.querySelectorAll('#result-grid td[data-row]').length<500"})
fixture.write_text(json.dumps({"name": "DBench repeated tab lifecycle", "html": "test/demo/dbench/dbench.ls",
                               "viewport": {"width": 960, "height": 700}, "input_turn_ms": 0,
                               "events": events}), encoding="utf-8")
try:
    env = os.environ.copy()
    env["TMPDIR"] = str(root / "temp")
    result = subprocess.run([str(root / "lambda.exe"), "demo", "dbench", "--headless", "--no-log",
                             "--event-file", str(fixture)], cwd=root, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120, check=True)
    assert "memtrack: LEAK" not in result.stdout, result.stdout
    print("Repeated tab creation and cancellation: PASS")
finally:
    fixture.unlink(missing_ok=True)
