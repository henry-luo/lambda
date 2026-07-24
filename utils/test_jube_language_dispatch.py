#!/usr/bin/env python3
"""Verify generic hosted-language alias, command, and extension dispatch."""

from __future__ import annotations

from pathlib import Path
import json
import os
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
HOST = Path(os.environ.get("LAMBDA_JUBE_HOST_EXE", ROOT / "lambda.exe")).resolve()
MODULE_DIR = ROOT / "modules" / "lang-python"
SOURCE = ROOT / "test" / "py" / "test_py_basic.py"
GOLDEN = ROOT / "test" / "py" / "test_py_basic.txt"
TEST_ROOT = ROOT / "temp" / "jube-language-dispatch"


def fail(message: str) -> None:
    print(f"JUBE_LANGUAGE_DISPATCH: {message}", file=sys.stderr)
    raise SystemExit(1)


def run_case(name: str, arguments: list[str], expected: str,
             extra_environment: dict[str, str] | None = None) -> None:
    environment = dict(os.environ)
    if extra_environment:
        environment.update(extra_environment)
    completed = subprocess.run(
        [str(TEST_ROOT / "lambda.exe"), *arguments],
        cwd=TEST_ROOT,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        fail(f"{name} exited {completed.returncode}: {completed.stderr.strip()}")
    if completed.stdout != expected:
        fail(f"{name} produced unexpected stdout")
    if completed.stderr:
        fail(f"{name} wrote unexpected stderr: {completed.stderr.strip()}")


def main() -> int:
    if not HOST.is_file() or not SOURCE.is_file() or not GOLDEN.is_file():
        fail("host or Python dispatch fixtures are missing")
    if not MODULE_DIR.is_dir():
        fail("lang-python module directory is missing")
    if TEST_ROOT.exists():
        shutil.rmtree(TEST_ROOT)
    (TEST_ROOT / "modules").mkdir(parents=True)
    shutil.copy2(HOST, TEST_ROOT / "lambda.exe")
    shutil.copytree(MODULE_DIR, TEST_ROOT / "modules" / "lang-python")
    upper_extension_source = TEST_ROOT / "test_py_basic.PY"
    shutil.copy2(SOURCE, upper_extension_source)
    expected = GOLDEN.read_text(encoding="utf-8")

    duplicate_root = TEST_ROOT / "duplicate-modules"
    shutil.copytree(MODULE_DIR, duplicate_root / "a-python")
    shutil.copytree(MODULE_DIR, duplicate_root / "z-python")
    rejected_manifest_path = duplicate_root / "z-python" / "module.json"
    rejected_manifest = json.loads(rejected_manifest_path.read_text(encoding="utf-8"))
    rejected_manifest["host_build_id"] = "incompatible-duplicate-bundle"
    rejected_manifest_path.write_text(
        json.dumps(rejected_manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    run_case("alias", ["py", str(SOURCE), "--no-log"], expected)
    run_case("uppercase-alias", ["PY", str(SOURCE), "--no-log"], expected)
    run_case("run-lang", ["run", "--lang", "python", str(SOURCE), "--no-log"], expected)
    run_case("extension", [str(SOURCE), "--no-log"], expected)
    run_case("uppercase-extension", [str(upper_extension_source), "--no-log"], expected)
    run_case("duplicate-bundle-order", ["PY", str(SOURCE), "--no-log"], expected,
             {"JUBE_MODULE_PATH": str(duplicate_root)})
    run_case("help", ["py", "--help"], "Lambda hosted Python\n\nUsage: lambda.exe py [file.py]\n")

    print("JUBE_LANGUAGE_DISPATCH: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
