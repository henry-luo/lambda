#!/usr/bin/env python3
"""Verify persisted profiles contain no password and can be reopened."""
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
network = root / "temp/dbench-profile-network.json"
sqlite = root / "temp/dbench-profile-sqlite.json"
for path in (network, sqlite):
    path.unlink(missing_ok=True)
try:
    subprocess.run([str(root / "lambda.exe"), "demo", "dbench", "--headless", "--no-log",
                    "--event-file", "test/ui/dbench_profiles.json"], cwd=root, check=True, timeout=90)
    network_text = network.read_text(encoding="utf-8")
    assert "never-save-this" not in network_text
    assert json.loads(network_text) == {
        "version": 1, "name": "Remote sample", "engine": "postgresql", "host": "db.example",
        "port": 5432, "database": "sales", "user": "reader", "tls": "verify-full", "ca": "certs/root.pem",
    }
    assert json.loads(sqlite.read_text(encoding="utf-8")) == {
        "version": 1, "name": "Local sample", "engine": "sqlite", "path": "test/demo/dbench/sample.sqlite",
    }
finally:
    network.unlink(missing_ok=True)
    sqlite.unlink(missing_ok=True)
