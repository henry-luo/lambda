#!/usr/bin/env python3
"""Guard the future Jube Node module boundary.

The checker is deliberately useful before the Node sources move: report mode
records the empty boundary, while enforcement rejects a newly moved source
that reaches back into JS engine internals instead of a Jube host service.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
MODULE_ROOT = ROOT / "lambda" / "module"
NODE_MODULE_NAMES = ("node_core", "node_zlib", "node_fs", "node_net",
                     "node_child_process", "node_http", "node_tls", "node_crypto")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}
FORBIDDEN_TOKENS = (
    '"../../js/', '"../js/', '"../../runtime/', '"../runtime/',
    "js_runtime.h", "js_runtime_state.hpp", "js_globals.cpp",
    '"../../lambda.hpp"', '"../../lambda-data.hpp"',
    "LambdaRootFrame", "Rooted<", "heap_register_gc_root", "js_heap_epoch",
    "uv_",
)


def source_files(module_root: Path) -> list[Path]:
    if not module_root.exists():
        return []
    return sorted(path for path in module_root.rglob("*") if path.suffix in SOURCE_SUFFIXES)


def scan_source(path: Path, source: str) -> list[dict]:
    violations = []
    for line_number, line in enumerate(source.splitlines(), start=1):
        for token in FORBIDDEN_TOKENS:
            if token in line:
                violations.append({
                    "path": str(path.relative_to(ROOT)) if path.is_absolute() else str(path),
                    "line": line_number,
                    "token": token,
                })
    return violations


def scan_tree() -> dict:
    modules = []
    violations = []
    for name in NODE_MODULE_NAMES:
        module_root = MODULE_ROOT / name
        files = source_files(module_root)
        modules.append({"name": name, "present": module_root.exists(), "source_count": len(files)})
        for path in files:
            violations.extend(scan_source(path, path.read_text(encoding="utf-8")))
    return {"schema_version": 1, "modules": modules, "violations": violations}


def node_core_objects() -> list[Path]:
    return sorted((ROOT / "build").glob("obj/**/node_*.o"))


def object_violations(symbols: list[str]) -> list[str]:
    # node_path may rely on approved platform/path helpers, but every JS
    # runtime operation must enter through a Jube table stored in the module.
    return sorted(symbol for symbol in symbols
                  if symbol.startswith("_js_") or symbol.startswith("_heap_"))


def check_node_core_objects() -> None:
    object_paths = node_core_objects()
    if not object_paths:
        raise RuntimeError("node-core objects are absent; run make build first")
    for object_path in object_paths:
        result = subprocess.run(["nm", "-u", str(object_path)], check=False,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if result.returncode != 0:
            raise RuntimeError(f"could not inspect {object_path.relative_to(ROOT)}: {result.stderr}")
        # BSD nm prints just the undefined symbol while GNU nm prefixes it with
        # an address/type column; normalize both before applying the boundary.
        symbols = [line.split()[-1] for line in result.stdout.splitlines() if line.split()]
        forbidden = object_violations(symbols)
        if forbidden:
            raise RuntimeError(f"{object_path.name} imports forbidden host symbols: " +
                               ", ".join(forbidden))


def node_module_binaries() -> list[Path]:
    paths: list[Path] = []
    for name in NODE_MODULE_NAMES:
        module_dir = ROOT / "modules" / name.replace("_", "-")
        for suffix in (".dylib", ".so", ".dll"):
            paths.extend(sorted(module_dir.glob(f"*{suffix}")))
    return paths


def check_node_module_binaries() -> None:
    binaries = node_module_binaries()
    if not binaries:
        raise RuntimeError("node module binaries are absent; build a dynamic module first")
    for binary_path in binaries:
        result = subprocess.run(["nm", "-u", str(binary_path)], check=False,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if result.returncode != 0:
            raise RuntimeError(f"could not inspect {binary_path.relative_to(ROOT)}: {result.stderr}")
        symbols = [line.split()[-1] for line in result.stdout.splitlines() if line.split()]
        forbidden = object_violations(symbols)
        if forbidden:
            raise RuntimeError(f"{binary_path.name} imports forbidden host symbols: " +
                               ", ".join(forbidden))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", action="store_true",
                        help="emit JSON without failing for migration debt")
    parser.add_argument("--require-node-core-object", action="store_true",
                        help="also verify the moved path object's undefined symbols")
    parser.add_argument("--require-module-binary", action="store_true",
                        help="verify every built dynamic Node module import table")
    args = parser.parse_args()
    report = scan_tree()
    if args.report:
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    if report["violations"]:
        for violation in report["violations"]:
            print("NODE_MODULE_ARCH: {path}:{line}: forbidden {token}".format(**violation),
                  file=sys.stderr)
        return 1
    if args.require_node_core_object:
        try:
            check_node_core_objects()
        except RuntimeError as error:
            print(f"NODE_MODULE_ARCH: {error}", file=sys.stderr)
            return 1
    if args.require_module_binary:
        try:
            check_node_module_binaries()
        except RuntimeError as error:
            print(f"NODE_MODULE_ARCH: {error}", file=sys.stderr)
            return 1
    print("NODE_MODULE_ARCH: boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
