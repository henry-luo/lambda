#!/usr/bin/env python3
"""Report the active ownership work needed for the static-module split.

This is deliberately report-only during P0.  It inventories the current
couplings from source rather than accepting an allowlist as proof of a clean
boundary.  P5 changes the same policy data into a failing gate after the
providers and source groups have actually moved.
"""

from pathlib import Path
import json
import re
import sys


ROOT = Path(__file__).resolve().parent.parent
BUILD_CONFIG = ROOT / "build_lambda_config.json"

# Retired C2MIR is not a module-split participant.  Keeping this list narrow
# makes an accidental widening of the frozen enclave visible in every report.
FROZEN_C2MIR = (
    "lambda/lambda.h",
    "lambda/lambda-embed.h",
    "lambda/transpile.cpp",
)

# These paths preserve source compatibility while the authoritative public
# declarations live in their owning Lambda module. They are not lib providers
# and therefore do not represent a lib-to-Lambda dependency.
COMPATIBILITY_FORWARDERS = {
    "lib/item_tagged.hpp",
    "lib/lambda_typed.hpp",
    "lib/side_stack.h",
}

PUBLIC_HEADERS = {
    "lambda/lambda.h": "frozen-c2mir-compatibility",
    "lambda/lambda.hpp": "mixed-active-core-io-rt",
    "lambda/lambda-data.hpp": "mixed-active-core-rt",
    "lambda/mark_reader.hpp": "core-candidate",
    "lambda/mark_builder.hpp": "io-candidate",
    "lambda/mark_editor.hpp": "io-candidate",
    "lambda/input/input.hpp": "io-candidate",
    "lambda/format/format.h": "io-candidate",
    "lambda/validator/validator.hpp": "rt-candidate",
    "lambda/runtime/runtime-state.h": "rt-provider",
    "radiant/radiant.hpp": "radiant-candidate",
}

RUNTIME_MARKERS = re.compile(
    r"\b(?:Context|EvalContext|RootFrame|Rooted|NoGC|g_dry_run|heap_[A-Za-z0-9_]*|"
    r"gc_[A-Za-z0-9_]*|lambda_root_[A-Za-z0-9_]*|lambda_stack_[A-Za-z0-9_]*)\b")
IO_MARKERS = re.compile(
    r"\b(?:Input|Target|target_[A-Za-z0-9_]*|Url|read_text_file|write_text_file|"
    r"curl_[A-Za-z0-9_]*|resource_[A-Za-z0-9_]*|network_[A-Za-z0-9_]*)\b")
RADIANT_INCLUDE = re.compile(r'^\s*#\s*include\s+["<][^">]*(?:radiant/|\.\./radiant/)', re.MULTILINE)
LAMBDA_INCLUDE = re.compile(r'^\s*#\s*include\s+["<][^">]*(?:lambda/|\.\./lambda/)', re.MULTILINE)
DIRECT_IO = re.compile(r"\b(?:curl_[A-Za-z0-9_]*|uv_[A-Za-z0-9_]*|open|read|write|socket|connect)\s*\(")
UPWARD_EXTERN = re.compile(
    r"^\s*extern\s+(?:\"C\"\s+)?[^;\n]*\b(?:heap_(?:alloc|data_alloc)|"
    r"lambda_(?:root|weak)_[A-Za-z0-9_]*|dispatch_emit|counter_format|"
    r"resolve_symbol(?:_string)?|log_mem_stage)\b[^;\n]*;", re.MULTILINE)
WEAK_PROVIDER = re.compile(
    r"__attribute__\s*\(\(weak\)\)|__attribute__\s*\(\s*\(weak\)\s*\)")
PUBLIC_STATIC_SELECTOR = re.compile(r"^\s*#\s*(?:define|undef)\s+LAMBDA_STATIC\b", re.MULTILINE)

RUNTIME_NATIVE_IO = {
    "lambda/js/js_fs.cpp": "rt-native: Node fs handles and callbacks",
    "lambda/js/js_net.cpp": "rt-native: Node sockets and libuv handles",
    "lambda/js/js_dns.cpp": "rt-native: Node DNS callbacks and libuv requests",
    "lambda/js/js_tls.cpp": "rt-native: Node TLS handles and promises",
    "lambda/js/js_fetch.cpp": "review: JS promise binding stays rt; reusable curl worker is io candidate",
}


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def matches(pattern: re.Pattern, path: str) -> list[dict]:
    text = source(path)
    return [
        {"line": line_number(text, match.start()), "token": match.group(0).strip()}
        for match in pattern.finditer(text)
    ]


def paths_under(directory: str) -> list[Path]:
    root = ROOT / directory
    return sorted(path for path in root.rglob("*")
                  if path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp"})


def include_inventory() -> dict:
    lib_to_lambda = []
    for path in paths_under("lib"):
        relative = path.relative_to(ROOT).as_posix()
        # These files are already classified rt for P1b, not lib violations.
        if (relative.startswith("lib/gc/") or relative == "lib/side_stack.c" or
                relative in COMPATIBILITY_FORWARDERS):
            continue
        text = path.read_text(encoding="utf-8")
        for match in LAMBDA_INCLUDE.finditer(text):
            lib_to_lambda.append({
                "path": relative,
                "line": line_number(text, match.start()),
                "token": match.group(0).strip(),
                "resolution": "move caller upward or replace with lib-neutral API",
            })

    upper_to_radiant = []
    for directory in ("lambda/input", "lambda/network", "lambda/js", "lambda/module"):
        for path in paths_under(directory):
            text = path.read_text(encoding="utf-8")
            for match in RADIANT_INCLUDE.finditer(text):
                upper_to_radiant.append({
                    "path": path.relative_to(ROOT).as_posix(),
                    "line": line_number(text, match.start()),
                    "token": match.group(0).strip(),
                    "resolution": "P3/P4 move, split, or lower-owned hook",
                })
    return {"lib_to_lambda": lib_to_lambda, "upper_to_radiant": upper_to_radiant}


def header_inventory() -> list[dict]:
    entries = []
    for path, planned_owner in PUBLIC_HEADERS.items():
        text = source(path)
        runtime = matches(RUNTIME_MARKERS, path)
        io = matches(IO_MARKERS, path)
        entries.append({
            "path": path,
            "planned_owner": planned_owner,
            "frozen": path in FROZEN_C2MIR,
            "runtime_markers": runtime,
            "io_markers": io,
            "outcome": (
                "exclude under SM14" if path in FROZEN_C2MIR else
                "split by provider before enforcement" if runtime and io else
                "candidate for provider probe after relocation"
            ),
        })
    return entries


def io_inventory() -> list[dict]:
    entries = []
    for path, classification in RUNTIME_NATIVE_IO.items():
        if not (ROOT / path).exists():
            continue
        entries.append({
            "path": path,
            "classification": classification,
            "direct_io_calls": matches(DIRECT_IO, path),
        })
    return entries


def validation_scaffold() -> dict:
    config = json.loads(BUILD_CONFIG.read_text(encoding="utf-8"))
    scaffold = config.get("static_module_validation", {})
    expected = {"lib", "core", "io", "rt", "radiant"}
    declared = {target.get("module") for target in scaffold.get("dsos", [])}
    return {
        "declared": sorted(declared),
        "missing": sorted(expected - declared),
        "unexpected": sorted(declared - expected),
        "mode": scaffold.get("mode"),
    }


def boundary_failure_baseline() -> dict:
    upward_externs = []
    weak_providers = []
    for directory in ("lambda", "radiant"):
        for path in paths_under(directory):
            text = path.read_text(encoding="utf-8")
            relative = path.relative_to(ROOT).as_posix()
            for match in UPWARD_EXTERN.finditer(text):
                upward_externs.append({
                    "path": relative,
                    "line": line_number(text, match.start()),
                    "declaration": match.group(0).strip(),
                    "resolution": "replace with lower-owned provider or registered hook",
                })
            for match in WEAK_PROVIDER.finditer(text):
                weak_providers.append({
                    "path": relative,
                    "line": line_number(text, match.start()),
                    "token": match.group(0),
                    "resolution": "P1b removes weak runtime fallbacks before DSO enforcement",
                })
    return {"upward_externs": upward_externs, "weak_providers": weak_providers}


def macro_hygiene_inventory() -> list[dict]:
    entries = []
    for path in PUBLIC_HEADERS:
        if path in FROZEN_C2MIR:
            continue
        text = source(path)
        for match in PUBLIC_STATIC_SELECTOR.finditer(text):
            entries.append({
                "path": path,
                "line": line_number(text, match.start()),
                "token": match.group(0).strip(),
                "resolution": "remove public-header ABI selector in P1a",
            })
    return entries


def inventory() -> dict:
    return {
        "schema_version": 1,
        "mode": "report-only",
        "frozen_c2mir": list(FROZEN_C2MIR),
        "public_header_providers": header_inventory(),
        "include_edges": include_inventory(),
        "runtime_native_io": io_inventory(),
        "validation_dso_scaffold": validation_scaffold(),
        "boundary_failure_baseline": boundary_failure_baseline(),
        "public_header_macro_hygiene": macro_hygiene_inventory(),
    }


def main() -> None:
    payload = inventory()
    if "--json" in sys.argv:
        print(json.dumps(payload, indent=2, sort_keys=True))
        return
    if "--boundary-baseline" in sys.argv:
        print(json.dumps(payload["boundary_failure_baseline"], indent=2, sort_keys=True))
        return
    headers = payload["public_header_providers"]
    includes = payload["include_edges"]
    scaffold = payload["validation_dso_scaffold"]
    print("STATIC_MODULE_ARCH: report-only P0 inventory")
    print(f"STATIC_MODULE_ARCH: headers={len(headers)} frozen={sum(entry['frozen'] for entry in headers)}")
    print("STATIC_MODULE_ARCH: "
          f"lib_to_lambda={len(includes['lib_to_lambda'])} "
          f"upper_to_radiant={len(includes['upper_to_radiant'])} "
          f"runtime_native_io={len(payload['runtime_native_io'])}")
    baseline = payload["boundary_failure_baseline"]
    print("STATIC_MODULE_ARCH: "
          f"predicted_dso_externs={len(baseline['upward_externs'])} "
          f"weak_providers={len(baseline['weak_providers'])}")
    macro_selectors = payload["public_header_macro_hygiene"]
    print(f"STATIC_MODULE_ARCH: public_lambda_static_selectors={len(macro_selectors)}")
    valid_modes = {"report-only", "enforced-with-class-f-defer"}
    if scaffold["missing"] or scaffold["unexpected"] or scaffold["mode"] not in valid_modes:
        print("STATIC_MODULE_ARCH: validation DSO scaffold needs repair", file=sys.stderr)
        raise SystemExit(1)
    if scaffold["mode"] == "enforced-with-class-f-defer":
        print("STATIC_MODULE_ARCH: five-DSO harness active; Class-F stays ratcheted by module-boundary-link")
    else:
        print("STATIC_MODULE_ARCH: five-DSO scaffold declared; enforcement deferred to P5")


if __name__ == "__main__":
    main()
