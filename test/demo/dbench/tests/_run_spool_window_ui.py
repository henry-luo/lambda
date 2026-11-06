#!/usr/bin/env python3
"""Verify exports after the query grid evicts and reloads its row cache."""
import csv
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
selection = root / "temp/dbench-spool-selection.csv"
complete = root / "temp/dbench-spool-complete.json"
after = root / "temp/dbench-spool-after.csv"
try:
    subprocess.run([str(root / "test/test_ui_automation_gtest.exe"), "--test", "dbench_spool_window"],
                   cwd=root, check=True, timeout=180)
    with selection.open(newline="") as stream:
        rows = list(csv.reader(stream))
    assert len(rows) == 2561 and rows[1] == ["1", "Row 1"] and rows[-1] == ["2560", "Row 2560"]
    result = json.loads(complete.read_text())
    assert len(result["rows"]) == 3000
    assert result["rows"][0] == [1, "Row 1"] and result["rows"][-1] == [3000, "Row 3000"]
    with after.open(newline="") as stream:
        loaded = list(csv.reader(stream))
    assert len(loaded) == 2817 and loaded[1] == ["1", "Row 1"] and loaded[-1] == ["2816", "Row 2816"]
finally:
    selection.unlink(missing_ok=True)
    complete.unlink(missing_ok=True)
    after.unlink(missing_ok=True)
