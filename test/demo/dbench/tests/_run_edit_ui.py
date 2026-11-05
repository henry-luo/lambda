#!/usr/bin/env python3
"""Run the edit replay on a disposable copy and verify the committed value."""
from pathlib import Path
import sqlite3
import subprocess
from _prepare_ui import prepare_database

root = Path(__file__).resolve().parents[4]
database = prepare_database("edit")
try:
    subprocess.run([str(root / "lambda.exe"), "demo", "dbench", "--headless", "--no-log",
                    "--event-file", "test/ui/dbench_edit.json"], cwd=root, check=True, timeout=90)
    with sqlite3.connect(database) as db:
        assert db.execute("SELECT name FROM customers WHERE customer_id=1").fetchone() == ("Edited customer",)
        assert db.execute("SELECT name, city FROM customers WHERE customer_id=301").fetchone() == ("Inserted", "Penang")
        assert db.execute("SELECT id, note FROM empty_keyed").fetchone() == (1, "first")
        assert db.execute("SELECT active, note FROM customers WHERE customer_id=1").fetchone() == (0, "Set note")
        assert db.execute("SELECT picture FROM products WHERE product_id=7").fetchone() == (b"\xaa\xbb",)
finally:
    database.unlink(missing_ok=True)
