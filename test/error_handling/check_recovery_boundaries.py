#!/usr/bin/env python3
"""Keep the native-fault carve-out separate from language completions.

This is intentionally a small source gate rather than a parser for C++: the
runtime recovery API is the only approved native-fault boundary, and ordinary
language implementations must not acquire a direct recovery call by accident.
The detailed semantic proof remains in the runtime tests and formal design.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}
VENDORED_PARTS = {"mir", "tree-sitter", "tree-sitter-lambda",
                  "tree-sitter-javascript", "tree-sitter-typescript"}

# These files contain the closed S7.11 execution/transaction boundary or the
# generated synchronous fault-only wrapper. No language frontend belongs here.
RECOVERY_CALL_ALLOWLIST = {
    "lambda/runtime/concurrency.cpp",
    "lambda/runtime/interp.cpp",
    "lambda/runtime/lambda-eval.cpp",
    "lambda/runtime/lambda-stack.cpp",
    "lambda/runtime/recovery_frame.c",
    "lambda/runtime/recovery_frame.h",
    "lambda/runtime/runner.cpp",
    "lambda/runtime/sys_func_registry.c",
    "lambda/runtime/transpile-mir.cpp",
    "lambda/js/js_mir_entrypoints_require.cpp",
    "lambda/jube/jube_registry.cpp",
    "lambda/main.cpp",
}


def source_files():
    for path in (ROOT / "lambda").rglob("*"):
        if path.suffix not in SOURCE_SUFFIXES:
            continue
        if any(part in VENDORED_PARTS for part in path.parts):
            continue
        yield path


def main():
    failures = []
    direct_recovery = re.compile(
        r"lambda_recovery_frame_raise_(?:local_)?fault\s*\(")
    recovery_boundary = re.compile(
        r"(?:lambda_recovery_frame_begin_for|"
        r"LAMBDA_RECOVERY_FRAME_SETJMP|lambda_recovery_frame_arm\s*\(|"
        r"lambda_recovery_frame_end\s*\()")
    for path in source_files():
        rel = path.relative_to(ROOT).as_posix()
        text = path.read_text(errors="replace")
        if direct_recovery.search(text) and rel not in RECOVERY_CALL_ALLOWLIST:
            failures.append(
                f"{rel}: direct recovery call is outside the native-fault allowlist")
        if recovery_boundary.search(text) and rel not in RECOVERY_CALL_ALLOWLIST:
            failures.append(
                f"{rel}: recovery boundary is outside the native-fault allowlist")
        if "LAMBDA_FAULT_EQUALITY_DEPTH_EXHAUSTION" in text:
            failures.append(
                f"{rel}: equality-depth failure must use an ordinary completion")
    if failures:
        for failure in failures:
            print(f"error-recovery-gate: {failure}")
        return 1
    print("error-recovery-gate: native recovery call sites are allowlisted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
