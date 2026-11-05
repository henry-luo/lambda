#!/usr/bin/env python3
"""Prepare isolated DBench fixtures for both the UI suite and standalone runners."""
from pathlib import Path
import shutil
import sqlite3
import sys

ROOT = Path(__file__).resolve().parents[4]
DB_PATHS = {
    "edit": "dbench-edit-ui.sqlite",
    "tabs_edit": "dbench-tabs-edit.sqlite",
    "million_rows": "dbench-million.sqlite",
    "write": "dbench-write-ui.sqlite",
    "write_cancel": "dbench-write-cancel.sqlite",
    "write_cancel_running": "dbench-write-running.sqlite",
}
OUTPUT_PATHS = {
    "export": ("dbench-export-ui.csv", "dbench-script-ui.sql"),
    "stream_export": ("dbench-stream-ui.json",),
    "stream_large": ("dbench-stream-large.csv",),
    "stream_unicode": ("dbench-stream-unicode.json",),
    "stream_empty": ("dbench-stream-empty.json",),
    "stream_cancel": ("dbench-stream-cancel.csv",),
    "spool_window": ("dbench-spool-selection.csv", "dbench-spool-complete.json", "dbench-spool-after.csv"),
    "table_export": ("dbench-table-export.csv", "dbench-table-empty.csv"),
    "table_cancel": ("dbench-table-cancel.csv",),
    "profiles": ("dbench-profile-network.json", "dbench-profile-sqlite.json"),
}


def prepare_database(variant):
    database = ROOT / "temp" / DB_PATHS[variant]
    database.parent.mkdir(exist_ok=True)
    database.unlink(missing_ok=True)
    if variant in ("edit", "tabs_edit"):
        shutil.copyfile(ROOT / "test/demo/dbench/sample.sqlite", database)
    with sqlite3.connect(database) as db:
        if variant == "edit":
            db.execute("CREATE TABLE unkeyed (message TEXT)")
            db.execute("INSERT INTO unkeyed VALUES ('unchanged')")
            db.execute("CREATE TABLE empty_keyed (id INTEGER PRIMARY KEY, note TEXT)")
        elif variant == "million_rows":
            db.execute("CREATE TABLE records (id INTEGER PRIMARY KEY, payload TEXT NOT NULL)")
            db.executemany("INSERT INTO records VALUES (?, ?)",
                           ((i, f"Row {i}") for i in range(1, 1_000_001)))
        elif variant.startswith("write"):
            db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, value TEXT)")
    return database


def prepare_fixture(variant):
    if variant in DB_PATHS:
        prepare_database(variant)
    elif variant in OUTPUT_PATHS:
        for name in OUTPUT_PATHS[variant]:
            (ROOT / "temp" / name).unlink(missing_ok=True)
    else:
        raise ValueError(f"Unknown DBench fixture: {variant}")
    # The UI harness requires prepared documents under temp; keep module and
    # resource paths intact without changing the checked-in demo.
    parent = ROOT / "temp/dbench-prepared" / variant
    demo = parent / "dbench"
    demo.mkdir(parents=True, exist_ok=True)
    for source in (ROOT / "test/demo").glob("mod_*.ls"):
        shutil.copyfile(source, parent / source.name)
    for source in (ROOT / "test/demo/dbench").iterdir():
        if source.is_file() and source.suffix in (".ls", ".css", ".sqlite"):
            shutil.copyfile(source, demo / source.name)


if __name__ == "__main__":
    prepare_fixture(sys.argv[1])
