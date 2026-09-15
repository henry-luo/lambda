#!/usr/bin/env python3
"""Reject legacy JS execution dependencies from the independent MVP source tree."""

import os
import re
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
MVP_DIR = os.path.join(PROJECT_ROOT, "lambda", "js", "mvp")

# The parser/AST API is the only permitted LambdaJS API surface.  The host MIR
# library and generic Lambda substrate are separately documented in the design.
ALLOWED_JS_INCLUDES = {"../js_transpiler.hpp", "../js_ast.hpp"}
FORBIDDEN_TERMS = (
    "JsRuntimeState", "JsContext", "JsFunction", "JsCallableCode",
    "js_interp", "js_eval", "js_execute", "js_runtime_", "js_mir_",
    "js_to_", "js_from_", "js_op_", "js_gc_", "js_value_",
)
ALLOWED_JS_SYMBOL_PREFIXES = ("js_transpiler_", "js_ast_")


def source_paths():
    names = sorted(os.listdir(MVP_DIR))
    return [os.path.join(MVP_DIR, name) for name in names
            if name.endswith((".cpp", ".h", ".hpp"))]


def main():
    failures = []
    finite_formatter_calls = 0
    for path in source_paths():
        relative = os.path.relpath(path, PROJECT_ROOT)
        with open(path, encoding="utf-8") as stream:
            text = stream.read()
        for include in re.findall(r'^\s*#include\s+"([^"]+)"', text, re.MULTILINE):
            if "/js/" in include or include.startswith("../js_"):
                if include not in ALLOWED_JS_INCLUDES:
                    failures.append(relative + ": legacy JS include " + include)
        for term in FORBIDDEN_TERMS:
            if term in text:
                failures.append(relative + ": forbidden legacy term " + term)
        for symbol in re.findall(r'\b(js_[A-Za-z0-9_]+)\s*\(', text):
            if not symbol.startswith(ALLOWED_JS_SYMBOL_PREFIXES):
                failures.append(relative + ": forbidden JS call " + symbol)
        finite_formatter_calls += len(re.findall(r'\blambda_finite_double_to_shortest\s*\(', text))
    if finite_formatter_calls != 2:
        failures.append("expected exactly two finite-number formatting boundary calls, found " +
                        str(finite_formatter_calls))
    if failures:
        print("JS MVP isolation audit failed:", file=sys.stderr)
        for failure in failures:
            print("  " + failure, file=sys.stderr)
        return 1
    print("JS MVP isolation audit verified: parser/AST only; private execution helpers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
