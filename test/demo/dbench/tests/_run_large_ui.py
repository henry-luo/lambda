#!/usr/bin/env python3
"""Exercise one million SQLite rows; run only after a release compilation."""
from pathlib import Path
import resource
import sqlite3
import subprocess
import time
from _prepare_ui import prepare_database

root = Path(__file__).resolve().parents[4]
database = prepare_database("million_rows")
try:
    started = time.monotonic()
    subprocess.run([str(root / "lambda.exe"), "demo", "dbench", "--headless", "--no-log",
                    "--event-file", "test/ui/dbench_million_rows.json"], cwd=root, check=True, timeout=120)
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    print(f"Million-row UI replay: {time.monotonic()-started:.2f}s, child maxrss={usage.ru_maxrss} (platform units)")
finally:
    database.unlink(missing_ok=True)
