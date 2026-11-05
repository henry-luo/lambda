#!/usr/bin/env python3
"""Verify staged changes remain attached to their tab on a disposable database."""
from pathlib import Path
import sqlite3
import subprocess
from _prepare_ui import prepare_database

root = Path(__file__).resolve().parents[4]
database = prepare_database("tabs_edit")
try:
    subprocess.run([str(root / "lambda.exe"), "demo", "dbench", "--headless", "--no-log",
                    "--event-file", "test/ui/dbench_tabs_edit.json"], cwd=root, check=True, timeout=90)
    with sqlite3.connect(database) as db:
        assert db.execute("SELECT name FROM customers WHERE customer_id=1").fetchone() == ("Tab one edit",)
finally:
    database.unlink(missing_ok=True)
