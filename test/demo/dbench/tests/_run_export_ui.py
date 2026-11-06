#!/usr/bin/env python3
"""Verify selection export and create-only publication on a disposable file."""
import csv
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
output = root / "temp/dbench-export-ui.csv"
script = root / "temp/dbench-script-ui.sql"
output.parent.mkdir(exist_ok=True)
output.unlink(missing_ok=True)
script.unlink(missing_ok=True)
try:
    subprocess.run([str(root / "lambda.exe"), "demo", "dbench", "--headless", "--no-log",
                    "--event-file", "test/ui/dbench_export.json"], cwd=root, check=True, timeout=90)
    with output.open(newline="", encoding="utf-8") as stream:
        assert list(csv.reader(stream)) == [["name"], ["Customer 001"], ["Customer 002"]]
    assert script.read_text(encoding="utf-8") == "SELECT * FROM customers"
finally:
    output.unlink(missing_ok=True)
    script.unlink(missing_ok=True)
