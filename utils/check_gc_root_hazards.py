#!/usr/bin/env python3
"""Reject unsafe exact-root patterns in migrated native runtime code.

This is the native-side ratchet paired with check_gc_effects.py. It is not a
C++ type checker; it deliberately targets the failure patterns that violate
the exact-root contract: registering addresses of automatic locals,
balanced register/unregister pairs used as transient guards, and one-time
process booleans guarding heap-local root registries.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from check_gc_effects import ROOT, SOURCE_SUFFIXES, function_bodies, relative


MIGRATED_ROOTS = (ROOT / "lambda",)
SKIP_PARTS = {
    "bash", "format", "input", "package", "py", "rb", "tree-sitter",
    "tree-sitter-bash", "tree-sitter-javascript", "tree-sitter-lambda",
    "tree-sitter-latex", "tree-sitter-latex-math", "tree-sitter-python",
    "tree-sitter-ruby", "tree-sitter-typescript",
}

# These functions replace or destroy persistent backing storage. They must
# unregister the old stable address and register the new one in one operation.
STABLE_RANGE_HANDOFFS = {
    "async_frame_reserve",
    "lambda_task_start_function_scoped",
    "js_object_get_own_property_descriptors",
    "lambda_task_set_frame_roots",
}

# These functions allocate stable backing storage which escapes the activation;
# the registered address is not the automatic pointer variable itself.
ESCAPING_STABLE_STORAGE = {
    "js_alloc_module_vars",
    "js_object_define_properties",
}

RESETTABLE_ROOT_FLAGS = {
    "s_hostobj_demo_roots_registered",
}

LOCAL_DECL = re.compile(
    r"\b(?:Item|String|Symbol|Array|List|Map|VMap|Function|Container|"
    r"JsFunction|JsArrayBuffer|JsTypedArray|LambdaHandle)\s*\*?\s*"
    r"([A-Za-z_]\w*)\b"
)
REGISTER_OPERAND = re.compile(
    r"heap_register_gc_root(?:_range)?\s*\(\s*"
    r"(?:\([^)]*\)\s*)?&?\s*([A-Za-z_]\w*)"
)
STATIC_LOCAL_DECL = re.compile(
    r"\bstatic\s+(?:Item|String|Symbol|Array|List|Map|VMap|Function|Container|"
    r"JsFunction|JsArrayBuffer|JsTypedArray|LambdaHandle)\s*\*?\s*"
    r"([A-Za-z_]\w*)\b"
)


def main() -> int:
    errors: list[str] = []
    functions_checked = 0

    for source_root in MIGRATED_ROOTS:
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            if any(part in SKIP_PARTS for part in path.relative_to(ROOT).parts):
                continue

            source = path.read_text(encoding="utf-8", errors="replace")
            # A static process flag survives heap replacement, but the root
            # registry does not. Epoch gates or idempotent registration are
            # required for persistent roots.
            for match in re.finditer(
                r"static\s+bool\s+([A-Za-z_]\w*(?:rooted|root_registered|roots_registered))\s*(?:=|;)",
                source,
            ):
                name = match.group(1)
                if name in RESETTABLE_ROOT_FLAGS:
                    continue
                line = source.count("\n", 0, match.start()) + 1
                errors.append(
                    f"process-lifetime GC root gate {name} at {relative(path)}:{line}"
                )

            for function in function_bodies(path):
                functions_checked += 1
                body = function.body
                has_register = "heap_register_gc_root" in body
                has_unregister = "heap_unregister_gc_root" in body
                if has_register and has_unregister and function.name not in STABLE_RANGE_HANDOFFS:
                    errors.append(
                        f"transient register/unregister pair in {function.name} at "
                        f"{relative(path)}:{function.line}; use RootFrame/Rooted"
                    )

                if not has_register:
                    continue
                if function.name in ESCAPING_STABLE_STORAGE:
                    continue
                locals_declared = set(LOCAL_DECL.findall(body))
                locals_declared -= set(STATIC_LOCAL_DECL.findall(body))
                for operand in REGISTER_OPERAND.findall(body):
                    if operand in locals_declared:
                        errors.append(
                            f"automatic local {operand} registered in {function.name} at "
                            f"{relative(path)}:{function.line}; use RootFrame/Rooted"
                        )

    if errors:
        print("gc-root-hazards: exact-root structural audit failed", file=sys.stderr)
        for error in sorted(set(errors)):
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(
        f"gc-root-hazards: {functions_checked} migrated native functions checked; "
        "no automatic-local roots or transient registration guards"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
