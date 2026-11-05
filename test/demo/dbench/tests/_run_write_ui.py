#!/usr/bin/env python3
"""Verify reviewed SQL writes and rollback on a disposable SQLite file."""
from pathlib import Path
import sqlite3
import subprocess
from _prepare_ui import prepare_database

root = Path(__file__).resolve().parents[4]
database = root / "temp/dbench-write-ui.sqlite"
cancel_database = root / "temp/dbench-write-cancel.sqlite"
running_database = root / "temp/dbench-write-running.sqlite"
try:
    prepare_database("write")
    subprocess.run([str(root / "lambda.exe"), "demo", "dbench", "--headless", "--no-log",
                    "--event-file", "test/ui/dbench_write.json"], cwd=root, check=True, timeout=90)
    with sqlite3.connect(database) as db:
        assert db.execute("SELECT id, value FROM items").fetchall() == [(1, "O'Reilly")]
        assert db.execute("SELECT name FROM sqlite_schema WHERE type='table' AND name='added'").fetchone() == ("added",)
    prepare_database("write_cancel")
    subprocess.run([str(root / "lambda.exe"), "demo", "dbench", "--headless", "--no-log",
                    "--event-file", "test/ui/dbench_write_cancel.json"], cwd=root, check=True, timeout=90)
    with sqlite3.connect(cancel_database) as db:
        assert db.execute("SELECT COUNT(*) FROM items").fetchone() == (0,)
    prepare_database("write_cancel_running")
    subprocess.run([str(root / "lambda.exe"), "demo", "dbench", "--headless", "--no-log",
                    "--event-file", "test/ui/dbench_write_cancel_running.json"], cwd=root, check=True, timeout=90)
    with sqlite3.connect(running_database) as db:
        assert db.execute("SELECT COUNT(*) FROM items").fetchone() == (0,)
finally:
    database.unlink(missing_ok=True)
    cancel_database.unlink(missing_ok=True)
    running_database.unlink(missing_ok=True)
