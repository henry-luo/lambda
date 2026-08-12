#!/usr/bin/env python3
"""Deterministic structural census and ratchet for JS Tune4 callable work.

The counts are intentionally source-only.  They identify legacy mechanisms;
semantic interpretation remains in the Tune4 deletion ledger and code review.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
JS_ROOT = ROOT / "lambda" / "js"
SOURCE_ROOTS = (
    JS_ROOT,
    ROOT / "lambda" / "jube",
    ROOT / "lambda" / "module" / "radiant",
    ROOT / "radiant",
)
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp", ".def"}

# C0 post-Tune3 ceilings.  Tune4 phases only lower these values; an increase is
# a regression even while a legacy mechanism still has an explicit allowance.
RATCHET_MAX = {
    "dispatch_declarations": 0,
    "dispatch_references": 0,
    "builtin_semantic_cases": 0,
    "ambiguous_factory_references": 0,
    "raw_native_factory_casts": 0,
    "pending_new_target_references": 0,
    "special_constructor_references": 0,
    "constructor_name_comparisons": 0,
    "receiver_name_dispatch_references": 0,
    "host_name_dispatch_references": 0,
    "property_miss_synthesis_references": 0,
    "call_migration_flag_references": 0,
    "array_receiver_state_references": 0,
    "legacy_invoke_wrapper_references": 0,
    "direct_global_catalog_shortcuts": 0,
    "direct_global_identity_loads": 1,
    "legacy_class_map_bridge_definitions": 1,
    "legacy_class_map_bridge_references": 2,
    "semantic_catalog_id_reads": 0,
    "catalog_validation_errors": 0,
}

PATTERNS = {
    "dispatch_declarations": re.compile(
        r"(?:^|\n)\s*(?:static\s+)?Item\s+js_dispatch_builtin\s*\(", re.MULTILINE
    ),
    "dispatch_references": re.compile(r"\bjs_dispatch_builtin\s*\("),
    "builtin_semantic_cases": re.compile(r"\bcase\s+JS_BUILTIN_[A-Z0-9_]+\s*:"),
    "ambiguous_factory_references": re.compile(r"\bjs_new_function\s*\("),
    "raw_native_factory_casts": re.compile(
        r"\bjs_new_function\s*\(\s*\(void\s*\*\s*\)", re.MULTILINE
    ),
    "pending_new_target_references": re.compile(
        r"\b(?:js_pending_new_target|js_has_pending_new_target)\b"
    ),
    "special_constructor_references": re.compile(r"\bspecial_ctor(?:_kind|_name_id)?\b"),
    "receiver_name_dispatch_references": re.compile(
        r"\b(?:js_string_method|js_number_method|js_map_method|"
        r"js_array_method|js_array_method_direct)\s*\("
    ),
    # D6.2.2v2: host/catalog methods are direct callable properties too; moving
    # an old receiver/name dispatcher out of lambda/js must not evade the gate.
    "host_name_dispatch_references": re.compile(
        r"\b(?:js_dom_element_method|js_document_method|"
        r"js_document_proxy_method|js_css_namespace_method|"
        r"js_canvas_method_dispatch|js_classlist_method|"
        r"js_dom_implementation_method|jube_member_call)\s*\("
    ),
    "property_miss_synthesis_references": re.compile(
        r"\b(?:js_get_or_create_builtin|js_lookup_builtin_method_spec)\s*\("
    ),
    "call_migration_flag_references": re.compile(
        r"\b(?:JS_CALL_STATS|JS_CALL_FORCE_GENERIC|JS_CALL_LANE_CHECK)\b"
    ),
    "array_receiver_state_references": re.compile(r"\bjs_array_method_real_this\b"),
    "legacy_invoke_wrapper_references": re.compile(r"\bjs_invoke_fn\s*\("),
    "legacy_class_map_bridge_definitions": re.compile(
        r"(?:^|\n)\s*static\s+Item\s+js_construct_entry_legacy_class_map\s*\(",
        re.MULTILINE,
    ),
    "legacy_class_map_bridge_references": re.compile(
        r"\bjs_construct_entry_legacy_class_map\s*\("
    ),
    "semantic_catalog_id_reads": re.compile(r"\bfn->catalog_id\b(?!\s*=)"),
}


def macro_rows(text: str, macro: str) -> list[list[str]]:
    """Return top-level arguments for each invocation of one catalog macro."""
    rows: list[list[str]] = []
    marker = f"{macro}("
    offset = 0
    while True:
        start = text.find(marker, offset)
        if start < 0:
            break
        index = start + len(marker)
        depth = 1
        quoted = False
        escaped = False
        while index < len(text) and depth:
            char = text[index]
            if quoted:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == '"':
                    quoted = False
            elif char == '"':
                quoted = True
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            index += 1
        if depth:
            rows.append(["<unterminated>"])
            break
        body = text[start + len(marker):index - 1]
        args: list[str] = []
        arg_start = 0
        arg_depth = 0
        quoted = False
        escaped = False
        for pos, char in enumerate(body):
            if quoted:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == '"':
                    quoted = False
            elif char == '"':
                quoted = True
            elif char == "(":
                arg_depth += 1
            elif char == ")":
                arg_depth -= 1
            elif char == "," and arg_depth == 0:
                args.append(body[arg_start:pos].strip())
                arg_start = pos + 1
        args.append(body[arg_start:].strip())
        rows.append(args)
        offset = index
    return rows


def quoted_text(value: str) -> str | None:
    if len(value) < 2 or value[0] != '"' or value[-1] != '"':
        return None
    try:
        return json.loads(value)
    except json.JSONDecodeError:
        return None


def catalog_validation_errors(text: str) -> list[str]:
    """Validate the callable target/binding matrix without running Lambda."""
    errors: list[str] = []
    owners = {
        row[0] for row in macro_rows(text, "JS_BUILTIN_OWNER") if len(row) == 1
    }
    targets: dict[str, tuple[str, str]] = {}
    for macro, expected in (("JS_BUILTIN_ID", 3),
                            ("JS_BUILTIN_CONSTRUCTOR_TARGET", 4)):
        for row in macro_rows(text, macro):
            if len(row) != expected:
                errors.append(f"{macro}: expected {expected} arguments, got {len(row)}")
                continue
            target_id = row[0]
            call_body = row[1]
            construct_body = row[2] if macro == "JS_BUILTIN_CONSTRUCTOR_TARGET" else "NULL"
            if target_id in targets:
                errors.append(f"duplicate target id {target_id}")
                continue
            if call_body == "NULL" and construct_body == "NULL":
                errors.append(f"target {target_id} has no call or construct body")
            targets[target_id] = (call_body, construct_body)

    seen_bindings: set[tuple[str, str]] = set()
    aliases: dict[str, tuple[str, str, str, str, str]] = {}
    for row in macro_rows(text, "JS_BUILTIN_METHOD"):
        if len(row) != 9:
            errors.append(f"JS_BUILTIN_METHOD: expected 9 arguments, got {len(row)}")
            continue
        owner, name_literal, length, target_id, arity, display_name, prop_kind, flags, alias = row
        name = quoted_text(name_literal)
        if owner not in owners:
            errors.append(f"binding {name_literal} has unknown owner {owner}")
        if name is None:
            errors.append(f"binding has invalid name literal {name_literal}")
            continue
        try:
            if int(length, 0) != len(name.encode("utf-8")):
                errors.append(f"binding {owner}.{name} has mismatched length {length}")
        except ValueError:
            errors.append(f"binding {owner}.{name} has non-integer length {length}")
        key = (owner, name)
        if key in seen_bindings:
            errors.append(f"duplicate owner/property binding {owner}.{name}")
        seen_bindings.add(key)
        if target_id != "JS_BUILTIN_NONE" and target_id not in targets:
            errors.append(f"binding {owner}.{name} references missing target {target_id}")
        if alias != "JS_INTRINSIC_ALIAS_NONE":
            observable_name = name_literal if display_name == "NULL" else display_name
            signature = (target_id, arity, observable_name, prop_kind, flags)
            prior = aliases.get(alias)
            if prior is not None and prior != signature:
                errors.append(f"identity alias {alias} has incompatible bindings")
            aliases[alias] = signature

    seen_global_ids: set[str] = set()
    seen_global_names: set[str] = set()
    for row in macro_rows(text, "JS_BUILTIN_GLOBAL"):
        if len(row) != 8:
            errors.append(f"JS_BUILTIN_GLOBAL: expected 8 arguments, got {len(row)}")
            continue
        global_id, name_literal, length, kind, runtime_id, target_id, _, _ = row
        name = quoted_text(name_literal)
        if name is None:
            errors.append(f"global has invalid name literal {name_literal}")
            continue
        try:
            if int(length, 0) != len(name.encode("utf-8")):
                errors.append(f"global {name} has mismatched length {length}")
        except ValueError:
            errors.append(f"global {name} has non-integer length {length}")
        if global_id in seen_global_ids:
            errors.append(f"duplicate global id {global_id}")
        if name in seen_global_names:
            errors.append(f"duplicate global name {name}")
        seen_global_ids.add(global_id)
        seen_global_names.add(name)
        if kind == "JS_BUILTIN_GLOBAL_NAMESPACE":
            if target_id != "JS_BUILTIN_NONE":
                errors.append(f"namespace {name} unexpectedly has target {target_id}")
            continue
        target = targets.get(target_id)
        if target is None:
            errors.append(f"global {name} references missing target {target_id}")
            continue
        call_body, construct_body = target
        if call_body == "NULL":
            errors.append(f"global {name} has no call body")
        rejecting_constructor = runtime_id in {"JS_CTOR_SYMBOL", "JS_CTOR_BIGINT"}
        if kind == "JS_BUILTIN_GLOBAL_FUNCTION" and construct_body != "NULL":
            errors.append(f"global function {name} unexpectedly has construct body")
        elif kind == "JS_BUILTIN_GLOBAL_CONSTRUCTOR":
            if construct_body == "NULL":
                errors.append(f"constructor binding {name} has no construct body")
            elif rejecting_constructor and construct_body != "js_intrinsic_ctor_forbidden_construct_body":
                # Symbol/BigInt deliberately have [[Construct]] for
                # IsConstructor/extends, but reject before coercing arguments.
                errors.append(f"rejecting constructor binding {name} has wrong construct body")
    return errors


def source_files() -> list[Path]:
    return sorted({
        path
        for source_root in SOURCE_ROOTS
        for path in source_root.rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    })


def extract_function(text: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", text, re.DOTALL)
    if not match:
        return ""
    start = match.start()
    brace = text.find("{", match.start())
    depth = 0
    for index in range(brace, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    return text[start:]


def collect() -> dict[str, object]:
    texts: dict[Path, str] = {
        path: path.read_text(encoding="utf-8", errors="replace")
        for path in source_files()
    }
    counts = {
        name: sum(len(pattern.findall(text)) for text in texts.values())
        for name, pattern in PATTERNS.items()
    }
    runtime_text = texts.get(JS_ROOT / "js_runtime.cpp", "")
    counts["builtin_semantic_cases"] = len(
        PATTERNS["builtin_semantic_cases"].findall(runtime_text)
    )
    constructor = (
        extract_function(runtime_text, "js_construct_legacy_algorithm")
        or extract_function(runtime_text, "js_new_from_class_object")
    )
    counts["constructor_name_comparisons"] = len(
        re.findall(r"\b(?:strncmp|memcmp)\s*\([^,]+,\s*\"", constructor)
    )
    lowering_text = texts.get(JS_ROOT / "js_mir_expression_lowering.cpp", "")
    counts["direct_global_catalog_shortcuts"] = len(
        re.findall(r"\bjs_builtin_global_find\s*\(", lowering_text)
    )
    counts["direct_global_identity_loads"] = len(
        re.findall(r"\bjs_get_global_builtin_fn_by_id\b", lowering_text)
    )
    catalog_errors = catalog_validation_errors(
        texts.get(JS_ROOT / "js_builtin_catalog.def", "")
    )
    counts["catalog_validation_errors"] = len(catalog_errors)

    by_file: dict[str, dict[str, int]] = {}
    for path, text in texts.items():
        file_counts = {
            name: len(pattern.findall(text))
            for name, pattern in PATTERNS.items()
        }
        file_counts = {name: value for name, value in file_counts.items() if value}
        if file_counts:
            by_file[str(path.relative_to(ROOT))] = file_counts

    return {
        "schema": 1,
        "roots": [str(path.relative_to(ROOT)) for path in SOURCE_ROOTS],
        "counts": dict(sorted(counts.items())),
        "ratchet_max": dict(sorted(RATCHET_MAX.items())),
        "by_file": dict(sorted(by_file.items())),
        "catalog_errors": catalog_errors,
    }


def render_text(result: dict[str, object]) -> str:
    counts = result["counts"]
    limits = result["ratchet_max"]
    lines = ["JS Tune4 callable census"]
    for name in sorted(counts):
        lines.append(f"{name}: {counts[name]} (max {limits.get(name, 'unbounded')})")
    for error in result.get("catalog_errors", []):
        lines.append(f"catalog error: {error}")
    return "\n".join(lines)


def main() -> int:
    global ROOT, JS_ROOT, SOURCE_ROOTS
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", action="store_true", help="emit deterministic JSON")
    parser.add_argument("--check", action="store_true", help="fail when a ratchet grows")
    parser.add_argument(
        "--root",
        type=Path,
        help="inspect a source snapshot rooted outside the current checkout",
    )
    args = parser.parse_args()

    if args.root is not None:
        # C0 comparisons must reuse this exact parser; otherwise census drift can
        # masquerade as mechanism deletion when the archived tree is inspected.
        ROOT = args.root.resolve()
        JS_ROOT = ROOT / "lambda" / "js"
        SOURCE_ROOTS = (
            JS_ROOT,
            ROOT / "lambda" / "jube",
            ROOT / "lambda" / "module" / "radiant",
            ROOT / "radiant",
        )

    result = collect()
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(render_text(result))

    if args.check:
        counts = result["counts"]
        failures = [
            f"{name}: {counts.get(name, 0)} > {limit}"
            for name, limit in sorted(RATCHET_MAX.items())
            if counts.get(name, 0) > limit
        ]
        if failures:
            print("callable census ratchet failures:", file=sys.stderr)
            for failure in failures:
                print(f"  {failure}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
