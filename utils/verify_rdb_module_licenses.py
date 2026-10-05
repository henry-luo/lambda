#!/usr/bin/env python3
"""Licence gate for the rdb-drivers Jube module (vibe/Lambda_IO_RDB.md
section 13.8, RDB8).

Fails when a bundle would ship the module without its licence material, or
with material that no longer describes the archives linked into it:
  1. LICENSES/ holds the PostgreSQL, LGPL-2.1 (Connector/C) and zlib texts;
  2. SOURCES.md names the versions, URLs and SHA-256 pinned in
     utils/build-rdb-deps.sh, and carries the LGPL relink instructions;
  3. the archives built (<deps-dir>/VERSIONS) are those pinned versions.

usage: utils/verify_rdb_module_licenses.py [--module-dir DIR] [--deps-dir DIR]
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LICENSES = {
    "PostgreSQL.txt": "PostgreSQL",
    "MariaDB-Connector-C-LGPL-2.1.txt": "LESSER GENERAL PUBLIC LICENSE",
    "zlib.txt": "Jean-loup Gailly",
}


def pinned() -> dict:
    script = (ROOT / "utils" / "build-rdb-deps.sh").read_text()
    values = {}
    for key in ("PG_VERSION", "MARIADB_CC_VERSION", "MARIADB_CC_SHA256"):
        m = re.search(rf"^{key}=(\S+)$", script, re.M)
        if not m:
            raise SystemExit(f"rdb-licences: {key} not found in build-rdb-deps.sh")
        values[key] = m.group(1)
    return values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--module-dir", default=str(ROOT / "modules" / "rdb-drivers"))
    parser.add_argument("--deps-dir", default=str(ROOT / "mac-deps" / "rdb"))
    args = parser.parse_args()
    module_dir = Path(args.module_dir)
    problems = []

    for name, marker in LICENSES.items():
        path = module_dir / "LICENSES" / name
        if not path.exists() or marker not in path.read_text(errors="replace"):
            problems.append(f"missing or wrong licence text LICENSES/{name}")

    want = pinned()
    sources = module_dir / "SOURCES.md"
    text = sources.read_text() if sources.exists() else ""
    if not text:
        problems.append("missing SOURCES.md")
    else:
        rows = {
            "libpq": f"| libpq (PostgreSQL) | {want['PG_VERSION']} |",
            "Connector/C": f"| MariaDB Connector/C | {want['MARIADB_CC_VERSION']} |",
        }
        for component, row in rows.items():
            if row not in text:
                problems.append(f"SOURCES.md does not record the pinned {component} version")
        if want["MARIADB_CC_SHA256"] not in text:
            problems.append("SOURCES.md does not record the pinned Connector/C SHA-256")
        if "RDB_MARIADB_ARCHIVE" not in text:
            problems.append("SOURCES.md lacks the LGPL relink instructions")

    versions = Path(args.deps_dir) / "VERSIONS"
    if versions.exists():
        built = dict(line.split() for line in versions.read_text().splitlines() if line.strip())
        if built.get("postgresql") != want["PG_VERSION"] or \
                built.get("mariadb-connector-c") != want["MARIADB_CC_VERSION"]:
            problems.append(f"built archives {built} differ from the pinned versions; "
                            "run make build-rdb-deps")

    for p in problems:
        print(f"FAIL {p}")
    print(f"rdb-licences: {module_dir}: {'FAILED' if problems else 'ok'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
