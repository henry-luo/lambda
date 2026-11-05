#!/usr/bin/env python3
"""Check complete export contents and cancellation against the bundled SQLite sample."""
import csv
import json
from pathlib import Path
import sqlite3
import subprocess

root = Path(__file__).resolve().parents[4]
output = root / "temp/dbench-stream-ui.json"
cancelled = root / "temp/dbench-stream-cancel.csv"
large = root / "temp/dbench-stream-large.csv"
unicode_output = root / "temp/dbench-stream-unicode.json"
empty = root / "temp/dbench-stream-empty.json"
table = root / "temp/dbench-table-export.csv"
empty_table = root / "temp/dbench-table-empty.csv"
cancelled_table = root / "temp/dbench-table-cancel.csv"
for path in (output, cancelled, large, unicode_output, empty, table, empty_table, cancelled_table):
    path.unlink(missing_ok=True)
before = set((root / "temp").glob(".lambda-stage-*"))
try:
    for replay in ("dbench_stream_export.json", "dbench_stream_large.json", "dbench_stream_unicode.json", "dbench_stream_empty.json",
                   "dbench_stream_cancel.json", "dbench_table_export.json", "dbench_table_cancel.json"):
        subprocess.run([str(root / "lambda.exe"), "demo", "dbench", "--headless", "--no-log",
                        "--event-file", "test/ui/" + replay], cwd=root, check=True, timeout=90)
    result = json.loads(output.read_text(encoding="utf-8"))
    with sqlite3.connect(root / "test/demo/dbench/sample.sqlite") as db:
        expected = [list(row) for row in db.execute("SELECT customer_id, name, note FROM customers ORDER BY customer_id")]
        order_rows = [[str(value) for value in row] for row in
                      db.execute("SELECT order_id, customer_id, status FROM orders ORDER BY order_id")]
        filtered_ids = [str(row[0]) for row in
                        db.execute("SELECT order_id FROM orders WHERE status=? ORDER BY order_id DESC", ("new",))]
    assert [column[0] for column in result["columns"]] == ["customer_id", "name", "note"]
    assert len(expected) == 300 and result["rows"] == expected
    with large.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.reader(stream))
    assert rows[0] == ["order_id", "customer_id", "status"]
    assert len(order_rows) == 12000 and rows[1:] == order_rows
    with table.open(newline="", encoding="utf-8") as stream:
        table_rows = list(csv.DictReader(stream))
    assert len(table_rows) == 3000 and [row["order_id"] for row in table_rows] == filtered_ids
    assert all(row["status"] == "new" for row in table_rows)
    with empty_table.open(newline="", encoding="utf-8") as stream:
        empty_table_rows = list(csv.reader(stream))
    assert empty_table_rows == [["order_id", "customer_id", "product_id", "placed_on", "quantity", "status"]]
    assert json.loads(unicode_output.read_text(encoding="utf-8"))["rows"] == [["😀" * 70000]]
    empty_result = json.loads(empty.read_text(encoding="utf-8"))
    assert [column[0] for column in empty_result["columns"]] == ["duplicate", "duplicate"]
    assert empty_result["rows"] == []
    assert not cancelled.exists()
    assert not cancelled_table.exists()
    assert set((root / "temp").glob(".lambda-stage-*")) == before
finally:
    output.unlink(missing_ok=True)
    cancelled.unlink(missing_ok=True)
    large.unlink(missing_ok=True)
    unicode_output.unlink(missing_ok=True)
    empty.unlink(missing_ok=True)
    table.unlink(missing_ok=True)
    empty_table.unlink(missing_ok=True)
    cancelled_table.unlink(missing_ok=True)
