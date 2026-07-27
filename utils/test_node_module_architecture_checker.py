#!/usr/bin/env python3
"""Self-test the Node module boundary checker without creating temp files."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parent.parent
CHECKER_PATH = ROOT / "utils" / "check_node_module_architecture.py"


def fail(message: str) -> None:
    print(f"NODE_MODULE_ARCH_SELFTEST: {message}", file=sys.stderr)
    raise SystemExit(1)


def load_checker():
    spec = importlib.util.spec_from_file_location("node_module_arch_checker", CHECKER_PATH)
    if spec is None or spec.loader is None:
        fail("could not load checker")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expect_token(checker, source: str, token: str) -> None:
    findings = checker.scan_source(Path("lambda/module/node_core/probe.cpp"), source)
    if not any(finding["token"] == token for finding in findings):
        fail(f"checker accepted forbidden token {token}")


def main() -> int:
    checker = load_checker()
    expect_token(checker, '#include "../../js/js_runtime.h"', '"../../js/')
    expect_token(checker, '#include "../../lambda.hpp"', '"../../lambda.hpp"')
    expect_token(checker, "uv_tcp_init(loop, &socket);", "uv_")
    expect_token(checker, "heap_register_gc_root(&item.item);", "heap_register_gc_root")
    node_fs_probe = Path("lambda/module/node_fs/node_fs_module.cpp")
    findings = checker.scan_source(node_fs_probe, '#include <unistd.h>\n')
    if not any(finding["token"] == "<unistd.h>" for finding in findings):
        fail("checker accepted a node-fs platform I/O header")
    findings = checker.scan_source(node_fs_probe, 'NODE_FS_OPEN(path, flags, mode);\n')
    if not any(finding["token"] == "NODE_FS_OPEN(" for finding in findings):
        fail("checker accepted a node-fs raw descriptor macro")
    if checker.object_violations(["_file_getcwd", "_js_property_get", "_heap_create_name"]) != [
            "_heap_create_name", "_js_property_get"]:
        fail("object symbol checker did not distinguish Jube and platform dependencies")
    if checker.scan_source(Path("lambda/module/node_core/clean.cpp"),
                           '#include "../../jube/jube.h"\n'):
        fail("checker rejected a Jube-only source")
    if checker.object_violations(["_jube_module", "_js_property_get"]) != ["_js_property_get"]:
        fail("binary import checker did not retain the Jube-only boundary")
    if checker.object_violations(["__Z13node_url_initPK11JubeHostAPI"]) != [
            "__Z13node_url_initPK11JubeHostAPI"]:
        fail("binary import checker accepted a shared Node primitive import")
    print("NODE_MODULE_ARCH_SELFTEST: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
