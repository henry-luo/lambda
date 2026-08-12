#!/usr/bin/env python3
"""Deterministic structural census for the JS Tune5 property migration.

This tool is intentionally source-only.  It reports mechanism ownership and
ratchet counts; semantic decisions remain governed by the Tune5 ledger and the
formal design rulings cited there.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (
    ROOT / "lambda" / "js",
    ROOT / "lambda" / "runtime",
    ROOT / "lambda" / "module",
    ROOT / "modules",
    ROOT / "radiant",
)
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}

# These are the P9 ceilings.  During the migration --check is expected to
# report the remaining debt; the ratchets become a release gate at P9.
RATCHET_MAX = {
    "semantic_operation_definitions": 8,
    "semantic_operation_families": 8,
    "transitional_exotic_adapters": 1,
    "ambient_strict_reads_in_core": 0,
    "core_ic_signature_references": 0,
    "prototype_numeric_scan_helpers": 1,
    "proxy_receiver_references": 0,
    "scoped_proxy_receiver_references": 0,
    "skip_accessor_dispatch_references": 0,
    "legacy_property_semantic_definitions": 0,
    "legacy_array_semantic_definitions": 0,
    "public_map_fast_semantic_exports": 0,
    "ordinary_array_index_classifiers": 1,
    "numeric_key_formatting_in_js": 0,
    "typemap_plausibility_recovery_branches": 0,
    "automatic_tagged_to_numeric_sites": 0,
    "js_private_gc_header_writes": 0,
    "array_identity_side_tables": 0,
}

PATTERNS = {
    "proxy_receiver_references": re.compile(r"\bjs_proxy_receiver\b"),
    "scoped_proxy_receiver_references": re.compile(r"\bScopedProxyReceiver\b"),
    "skip_accessor_dispatch_references": re.compile(
        r"\b(?:js_skip_accessor_dispatch|ScopedSkipAccessorDispatch)\b"
    ),
    "legacy_property_semantic_definitions": re.compile(
        r"(?:^|\n)\s*(?:extern \"C\"\s+)?(?:Item|bool|void)\s+js_property_"
        r"(?:get|set|access|set_strict|set_v|access_name_id|set_name_id)\s*\([^;]*\)\s*\{",
        re.MULTILINE | re.DOTALL,
    ),
    "legacy_array_semantic_definitions": re.compile(
        r"(?:^|\n)\s*(?:extern \"C\"\s+)?(?:Item|int64_t|bool|void)\s+js_array_"
        r"(?:get|set)[A-Za-z0-9_]*\s*\([^;]*\)\s*\{",
        re.MULTILINE | re.DOTALL,
    ),
    "public_map_fast_semantic_exports": re.compile(
        r"(?:^|\n)\s*(?:extern \"C\"\s+)?(?:Item|bool|void)\s+js_map_get_fast"
        r"(?:_ext)?\s*\([^;]*\)\s*\{",
        re.MULTILINE | re.DOTALL,
    ),
    # Count independent decimal-index implementations, not thin typed
    # wrappers that delegate to the canonical NameClassification seam.
    "ordinary_array_index_classifiers": re.compile(
        r"(?:for\s*\([^\n]*\)|while\s*\([^\n]*\))[^\n]*"
        r"(?:array_index|idx|digit|chars\[)", re.IGNORECASE
    ),
    "numeric_key_formatting_in_js": re.compile(
        r"\bsnprintf\s*\([^\n]*\"(?:%d|%lld|%llu)\"[^\n]*\b(?:index|idx|i)\b",
        re.IGNORECASE,
    ),
    "typemap_plausibility_recovery_branches": re.compile(
        r"(?:\b(?:typemap_ptr_is_plausible|type_map_is_plausible|map_type_is_valid)\b"
        r"[^\n]*(?:return|fallback|miss|error)|->type\s*&&\s*!?[^\n]*"
        r"(?:return|fallback|miss|error))",
        re.IGNORECASE,
    ),
    "automatic_tagged_to_numeric_sites": re.compile(
        r"(?:HOLEY_TAGGED|PACKED_TAGGED)[^\n]*(?:PACKED_NUMERIC|NUMERIC)"
        r"|(?:PACKED_NUMERIC)[^\n]*(?:PACKED_TAGGED|HOLEY_TAGGED)",
        re.IGNORECASE,
    ),
    "js_private_gc_header_writes": re.compile(
        r"\b(?:header|gc_header)->(?:type_tag|alloc_size|object_size|flags)\s*=(?!=)",
        re.IGNORECASE,
    ),
    "array_identity_side_tables": re.compile(
        r"\b(?:array_identity|array_forward|array_wrapper|array_side_table)\b",
        re.IGNORECASE,
    ),
}

SEMANTIC_OPERATIONS = (
    "js_get",
    "js_set",
    "js_define_own",
    "js_delete",
    "js_has_property",
    "js_has_own",
    "js_get_own_property_descriptor_lane",
    "js_own_keys",
)


def source_files() -> list[Path]:
    return sorted(
        {
            path
            for source_root in SOURCE_ROOTS
            if source_root.exists()
            for path in source_root.rglob("*")
            if path.is_file() and path.suffix in SOURCE_SUFFIXES
        }
    )


def function_definitions(text: str, name: str) -> int:
    pattern = rf"(?:^|\n)\s*(?:extern \"C\"\s+)?(?:[A-Za-z_][A-Za-z0-9_:<>*& ]*\s+)?{re.escape(name)}\s*\([^;]*?\)\s*\{{"
    return len(re.findall(pattern, text, re.MULTILINE | re.DOTALL))


def collect(configuration: str) -> dict[str, object]:
    texts = {
        path: path.read_text(encoding="utf-8", errors="replace")
        for path in source_files()
    }
    counts = {
        name: sum(len(pattern.findall(text)) for text in texts.values())
        for name, pattern in PATTERNS.items()
    }
    # The JS property layer never owns a GC header.  Runtime GC writes live in
    # the collector support root and are not a JS-private retagging surface.
    counts["js_private_gc_header_writes"] = sum(
        len(PATTERNS["js_private_gc_header_writes"].findall(text))
        for path, text in texts.items()
        if path.is_relative_to(ROOT / "lambda" / "js")
    )

    def is_typemap_recovery(path: Path, line: str) -> bool:
        if "assert(" in line:
            return False
        # These are explicit external-input and cache-publication validators;
        # the ratchet is only for ordinary semantic fallback/miss recovery.
        if path.name in {"js_class.h", "lambda-eval.cpp"}:
            return False
        if any(token in line for token in (
                "js_new_object_with_typemap", "cached_shape", "g_regex",
                "JS_LOAD_IC_SITE_MISS_BAD_TYPEMAP", "JS_STORE_IC_SITE_MISS_BAD_TYPEMAP")):
            return False
        return True

    counts["typemap_plausibility_recovery_branches"] = sum(
        sum(is_typemap_recovery(path, line) for line in text.splitlines()
            if "typemap_ptr_is_plausible" in line or
            "type_map_is_plausible" in line or "map_type_is_valid" in line)
        for path, text in texts.items()
        if path.name != "js_runtime.cpp"
    )
    props_text = "\n".join(
        text for path, text in texts.items() if path.name in {"js_props.h", "js_props.cpp"}
    )
    runtime_text = "\n".join(
        text for path, text in texts.items() if path.name == "js_runtime.cpp"
    )
    # Tune5's semantic Set core is the contiguous runtime lane between the
    # explicit primitive setter and the public core wrapper. Counting only
    # js_props.cpp would miss a regression where an inner Set branch consults
    # the ambient strict-mode cell.
    core_start = runtime_text.find("static Item js_set_on_primitive_base")
    core_end = runtime_text.find("extern \"C\" Item js_set_key_cstr")
    set_core_text = runtime_text[core_start:core_end] if core_start >= 0 and core_end > core_start else ""
    semantic_core_text = props_text + set_core_text
    counts["semantic_operation_definitions"] = sum(
        function_definitions(props_text, name) for name in SEMANTIC_OPERATIONS
    )
    counts["ordinary_array_index_classifiers"] = sum(
        function_definitions(text, "js_property_name_to_array_index")
        for text in texts.values()
    )
    counts["semantic_operation_families"] = sum(
        len(re.findall(rf"\b{re.escape(name)}\b", props_text)) > 0
        for name in SEMANTIC_OPERATIONS
    )
    counts["transitional_exotic_adapters"] = sum(
        function_definitions(text, "js_property_exotic_adapter")
        for text in texts.values()
    )
    counts["ambient_strict_reads_in_core"] = len(
        re.findall(r"\bjs_strict_mode\b", semantic_core_text)
    )
    counts["core_ic_signature_references"] = len(
        re.findall(r"\b(?:JsLoadIC|JsStoreIC|FeedbackSlot)\b", props_text)
    )
    counts["elements_state_reads_writes"] = sum(
        len(re.findall(r"\b(?:container_js_elements_kind|JS_ELEMENTS_[A-Z_]+)\b", text))
        for text in texts.values()
    )
    counts["sparse_map_references"] = sum(
        len(re.findall(r"\b(?:SparseArrayMap|MAP_KIND_ARRAY_SPARSE|sparse_indices)\b", text))
        for text in texts.values()
    )
    counts["prototype_epoch_mutation_references"] = sum(
        len(re.findall(r"\b(?:mutation_versions|mutation_serial|note_property_mutation)\b", text))
        for text in texts.values()
    )
    counts["prototype_numeric_scan_helpers"] = sum(
        function_definitions(text, "js_array_proto_scan_indexed_properties")
        for text in texts.values()
    )
    counts["host_property_callback_references"] = sum(
        len(re.findall(r"\b(?:property_set_intercept|property_get_intercept|js_dom_.*property)\b", text))
        for text in texts.values()
    )

    by_file: dict[str, dict[str, int]] = {}
    for path, text in texts.items():
        file_counts = {
            name: len(pattern.findall(text))
            for name, pattern in PATTERNS.items()
        }
        if not path.is_relative_to(ROOT / "lambda" / "js"):
            file_counts["js_private_gc_header_writes"] = 0
        file_counts["typemap_plausibility_recovery_branches"] = 0 if path.name == "js_runtime.cpp" else sum(
            is_typemap_recovery(path, line) for line in text.splitlines()
            if "typemap_ptr_is_plausible" in line or
            "type_map_is_plausible" in line or "map_type_is_valid" in line
        )
        if path.name in {"js_props.h", "js_props.cpp"}:
            file_counts["ordinary_array_index_classifiers"] = function_definitions(
                text, "js_property_name_to_array_index")
        if path.name in {"js_props.h", "js_props.cpp"}:
            file_counts["semantic_operation_definitions"] = sum(
                function_definitions(text, name) for name in SEMANTIC_OPERATIONS
            )
        file_counts = {name: value for name, value in file_counts.items() if value}
        if file_counts:
            by_file[str(path.relative_to(ROOT))] = file_counts

    return {
        "schema": 1,
        "configuration": configuration,
        "roots": [str(path.relative_to(ROOT)) for path in SOURCE_ROOTS if path.exists()],
        "counts": dict(sorted(counts.items())),
        "ratchet_max": dict(sorted(RATCHET_MAX.items())),
        "semantic_operations": list(SEMANTIC_OPERATIONS),
        "by_file": dict(sorted(by_file.items())),
    }


def render_text(result: dict[str, object]) -> str:
    lines = ["JS Tune5 property census"]
    counts = result["counts"]
    limits = result["ratchet_max"]
    for name in sorted(counts):
        lines.append(f"{name}: {counts[name]} (max {limits.get(name, 'unbounded')})")
    return "\n".join(lines)


def main() -> int:
    global ROOT, SOURCE_ROOTS
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", action="store_true", help="emit deterministic JSON")
    parser.add_argument("--check", action="store_true", help="fail when a final ratchet grows")
    parser.add_argument("--configuration", default="debug", help="label recorded in the census")
    parser.add_argument("--root", type=Path, help="inspect a source snapshot outside this checkout")
    args = parser.parse_args()

    if args.root is not None:
        ROOT = args.root.resolve()
        SOURCE_ROOTS = tuple(
            ROOT / relative
            for relative in (
                Path("lambda/js"),
                Path("lambda/runtime"),
                Path("lambda/module"),
                Path("modules"),
                Path("radiant"),
            )
        )

    result = collect(args.configuration)
    print(json.dumps(result, indent=2, sort_keys=True) if args.json else render_text(result))

    if args.check:
        counts = result["counts"]
        failures = [
            f"{name}: {counts.get(name, 0)} > {limit}"
            for name, limit in sorted(RATCHET_MAX.items())
            if counts.get(name, 0) > limit
        ]
        if failures:
            print("property census ratchet failures:", file=sys.stderr)
            for failure in failures:
                print(f"  {failure}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
