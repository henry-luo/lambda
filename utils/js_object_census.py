#!/usr/bin/env python3
"""Deterministic Tune6 object-mechanism census and structural ratchet.

The report deliberately distinguishes forbidden semantic mechanisms from the
small physical ``map_kind`` allowlist.  It is source-based: the rows are an
audit manifest for review, while ``--check`` only accepts the named physical
owners below and the explicit JR7 Promise index exception.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCOPES = (
    ROOT / "lambda" / "js",
    ROOT / "lambda" / "runtime",
    ROOT / "modules",
    ROOT / "radiant",
    ROOT / "test",
)

FORBIDDEN_PATTERNS = {
    "property_adapter": r"js_property_exotic_adapter",
    "property_adapter_type": r"JsPropertyExoticOperation",
    "legacy_exotic_helpers": r"js_try_exotic_[A-Za-z0-9_]+",
    "mutable_class_field": r"(?:->|\.)js_class\b|\bjs_class\s*=",
    "class_stamp": r"js_class_stamp|js_object_set_class|js_class_get",
    "implicit_proto": r"js_get_implicit_proto",
    "instance_proto_sentinel": r"__instance_proto__",
    "json_proto_sentinel": r"__json_own_proto__",
    "native_backing_sentinels": r"__(?:ta|ab|dv)__",
    "tune6_native_sentinels": r"__(?:rd|gen_idx)__?",
}

PROMISE_ALLOWLIST = re.compile(r"__promise_idx")
MAP_KIND_RE = re.compile(r"\b(?:map_kind|MAP_KIND_[A-Z0-9_]+)\b")


def source_files() -> list[Path]:
    files: list[Path] = []
    for scope in SCOPES:
        if scope.exists():
            files.extend(
                p for p in scope.rglob("*")
                if p.suffix in {".c", ".cc", ".cpp", ".h", ".hpp", ".py"}
            )
    return sorted(set(files))


def is_comment(line: str) -> bool:
    stripped = line.lstrip()
    return stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*")


def map_kind_disposition(path: Path, line: str) -> tuple[str, str]:
    """Classify one physical map-kind source row by its owning subsystem."""
    rel = str(path.relative_to(ROOT))
    code = line.split("//", 1)[0]
    if "MAP_KIND_ARRAY" in code or "map_kind_is_array_props" in code:
        return "physical_array_companion", "array companion allocation/shape/GC"
    if rel.endswith("lambda/js/js_typed_array.cpp"):
        return "physical_typed_payload", "checked typed-array/buffer/view payload access"
    if "MAP_KIND_REGEXP" in code:
        return "physical_typed_payload", "checked RegExp carrier payload"
    if "MAP_KIND_COLLECTION" in code:
        return "physical_typed_payload", "checked collection payload"
    if "MAP_KIND_ITERATOR" in code:
        return "physical_iterator_payload", "iterator carrier allocation/trace/finalize"
    if "MAP_KIND_PROXY" in code:
        return "physical_proxy_payload", "Proxy carrier allocation/trace/finalize"
    if rel.endswith("lambda/runtime/lambda-mem.cpp"):
        return "physical_gc", "native payload finalization/tracing"
    if rel.endswith("lambda/runtime/gc/gc_heap.c"):
        return "physical_gc", "container GC dispatch"
    if rel.endswith("lambda/js/js_runtime.cpp") and (
        "js_map_promote_descriptor_kind" in code or "MAP_KIND_PLAIN" in code
    ):
        return "physical_shape", "descriptor/shape storage mutation"
    if rel.endswith("lambda/js/js_runtime.cpp"):
        return "physical_runtime_carrier", "runtime carrier allocation/trace/payload"
    if rel.endswith("lambda/js/js_runtime_state.cpp"):
        return "physical_array_companion", "runtime array companion allocation"
    if rel.endswith("lambda/runtime/transpile-mir.cpp"):
        return "physical_input", "Input/compiled Map construction"
    if rel.endswith("lambda/runtime/lambda-eval.cpp"):
        return "physical_copy", "container copy preserves physical layout"
    if rel.endswith("lambda/runtime/lambda-data-runtime.cpp"):
        return "physical_array", "ArrayNum element lane storage"
    if rel.endswith("lambda/runtime/vmap.cpp"):
        return "physical_copy", "VMap/Map conversion preserves physical layout"
    if rel.startswith("test/"):
        return "physical_test", "layout/representation test fixture"
    if rel.endswith("lambda/lambda.h") or rel.endswith("lambda-data.hpp"):
        return "physical_layout", "container ABI or MapKind declaration"
    if rel.startswith("lambda/js/") and (
        "map_kind_is_array_props" in code or "MAP_KIND_ARRAY_PROPS" in code
    ):
        return "physical_array_companion", "array companion storage"
    return "semantic_forbidden", "no Tune6 semantic map_kind owner"


def census() -> dict:
    totals = {name: 0 for name in FORBIDDEN_PATTERNS}
    rows: list[dict] = []
    map_totals: dict[str, int] = {}
    map_rows: list[dict] = []
    for path in source_files():
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        rel = str(path.relative_to(ROOT))
        for name, pattern in FORBIDDEN_PATTERNS.items():
            count = len(re.findall(pattern, text))
            if count:
                totals[name] += count
                rows.append({"file": rel, "symbol": name, "count": count})
        for number, line in enumerate(text.splitlines(), 1):
            if not MAP_KIND_RE.search(line) or is_comment(line):
                continue
            disposition, owner = map_kind_disposition(path, line)
            map_totals[disposition] = map_totals.get(disposition, 0) + 1
            map_rows.append({
                "file": rel,
                "line": number,
                "disposition": disposition,
                "owner": owner,
                "text": line.strip(),
            })
    forbidden_map_rows = [r for r in map_rows if r["disposition"] == "semantic_forbidden"]
    return {
        "root": str(ROOT),
        "forbidden_totals": totals,
        "forbidden_rows": rows,
        "map_kind_totals": map_totals,
        "map_kind_semantic_forbidden": len(forbidden_map_rows),
        "map_kind_rows": map_rows,
        "promise_allowlist": "__promise_idx (JR7 only)",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", type=Path)
    parser.add_argument("--text", type=Path)
    parser.add_argument("--configuration", default="debug")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    result = census()

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.text:
        args.text.parent.mkdir(parents=True, exist_ok=True)
        lines = [f"configuration: {args.configuration}"]
        lines.extend(f"forbidden.{key}: {result['forbidden_totals'][key]}"
                     for key in sorted(result["forbidden_totals"]))
        lines.extend(f"map_kind.{key}: {result['map_kind_totals'][key]}"
                     for key in sorted(result["map_kind_totals"]))
        lines.append(f"map_kind.semantic_forbidden: {result['map_kind_semantic_forbidden']}")
        args.text.write_text("\n".join(lines) + "\n", encoding="utf-8")
    if not args.json and not args.text:
        print(json.dumps(result, indent=2, sort_keys=True))

    if args.check:
        failures = {
            key: value for key, value in result["forbidden_totals"].items()
            if value
        }
        if result["map_kind_semantic_forbidden"]:
            failures["semantic_map_kind"] = result["map_kind_semantic_forbidden"]
        if failures:
            print(json.dumps({"ratchet_failures": failures}, sort_keys=True))
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
