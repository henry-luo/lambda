#!/usr/bin/env python3
"""Reconcile CSS registration and source wiring; source references are not conformance evidence."""

import argparse
import csv
import io
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "test/css/support_inventory.tsv"
REGISTRY = "lambda/input/css/css_properties.cpp"
MAPPING = "lambda/input/css/well_known_css_property_mapping.h"
ACCESSORS = "radiant/css_prop_table.cpp"
QUALIFICATION = ROOT / "test/css/support_qualification.json"


def qualification_records():
    document = json.loads(QUALIFICATION.read_text())
    if document.get("version") != 1:
        raise ValueError("unsupported qualification manifest version")
    records = {}
    for family in document["families"]:
        if family["qualification"] not in {"supported", "partial", "parsed-only"}:
            raise ValueError("unknown qualification status")
        if not family["scope"] or not family["evidence"]:
            raise ValueError("qualification needs bounded scope and evidence")
        for reference in family["evidence"]:
            path, _, case = reference.partition("#")
            source = ROOT / path
            if not source.is_file() or (case and case not in source.read_text()):
                raise ValueError(f"missing qualification evidence: {reference}")
        for name in family["properties"]:
            if name in records:
                raise ValueError(f"duplicate qualification: {name}")
            records[name] = family
    return records


def references(paths, pattern):
    found = {}
    for path in sorted(paths):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            for code in re.findall(pattern, line):
                found.setdefault(code, []).append(f"{path.relative_to(ROOT)}:{number}")
    return found


def inventory():
    qualifications = qualification_records()
    source = (ROOT / REGISTRY).read_text()
    definitions = source.split("static CssProperty property_definitions[] = {", 1)[1].split("\n};", 1)[0]
    names = dict(re.findall(r"X\((CSS_PROPERTY_\w+), (MARKUP_NAME_\w+)\)", (ROOT / MAPPING).read_text()))
    accessor_source = (ROOT / ACCESSORS).read_text().split("static const CssPropAccessor CSS_PROP_ROWS[] = {", 1)[1].split("\n};", 1)[0]
    accessors = {}
    for macro, code, arguments in re.findall(r"(DIRECT_ROW|DERIVED_ROW|DECL_ROW)\((CSS_PROPERTY_\w+)([^\n]*)\)", accessor_source):
        accessors[code] = (macro, arguments.lstrip(", "))
    resolver = references([ROOT / "radiant/resolve_css_style.cpp"], r"case (CSS_PROPERTY_\w+):")
    consumers = references(
        [path for path in (ROOT / "radiant").glob("*.cpp")
         if path.name.startswith(("layout_", "render_", "paint_", "css_animation", "animation_"))],
        r"\b(CSS_PROPERTY_\w+)\b")
    tests = references(list((ROOT / "test").glob("test_*gtest.cpp")) + list((ROOT / "test/css").glob("*.cpp")),
        r"\b(CSS_PROPERTY_\w+)\b")
    buffer = io.StringIO()
    writer = csv.writer(buffer, delimiter="\t", lineterminator="\n")
    writer.writerow(["name", "property_code", "generated_name_id", "registered_type", "inherits",
        "initial", "registered_shorthand", "identity_shorthand", "registered_longhands", "registered_validator",
        "computed_accessor", "computed_storage_or_writer", "resolver_cases",
        "layout_paint_motion_references", "native_test_references", "qualification",
        "qualification_scope", "qualification_evidence", "qualification_limits"])
    seen_names, seen_codes = set(), set()
    for line in definitions.splitlines():
        line = line.strip()
        if not line.startswith("{CSS_PROPERTY_"):
            continue
        fields = next(csv.reader([line.removesuffix(",")[1:-1]], skipinitialspace=True))
        if len(fields) not in (11, 13):
            raise ValueError(f"unrecognized registry row: {line}")
        code, name, value_type, inheritance, initial, _, shorthand, longhands, _, validator, _ = fields[:11]
        identity = fields[12] if len(fields) == 13 else "false"
        if name in seen_names or code in seen_codes:
            raise ValueError(f"duplicate registered identity: {name}, {code}")
        if code not in names:
            raise ValueError(f"missing generated name: {code}")
        seen_names.add(name)
        seen_codes.add(code)
        accessor, storage = accessors.get(code, ("absent", ""))
        qualification = qualifications.get(name, {})
        writer.writerow([name, code, names[code], value_type, inheritance, initial, shorthand, identity,
            longhands, validator, accessor, storage, ";".join(resolver.get(code, [])),
            ";".join(consumers.get(code, [])), ";".join(tests.get(code, [])),
            qualification.get("qualification", "unqualified"), qualification.get("scope", ""),
            ";".join(qualification.get("evidence", [])), qualification.get("limits", "")])
    if not seen_codes:
        raise ValueError("no property definitions found")
    unknown = set(accessors) - seen_codes
    if unknown:
        raise ValueError(f"unregistered computed accessors: {sorted(unknown)}")
    if set(qualifications) - seen_names:
        raise ValueError(f"unregistered qualifications: {sorted(set(qualifications) - seen_names)}")
    return buffer.getvalue(), len(seen_codes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail when the versioned inventory is stale")
    args = parser.parse_args()
    text, count = inventory()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != text:
            print("CSS inventory is stale: run python3 utils/generate_css_support_inventory.py", file=sys.stderr)
            return 1
    else:
        OUTPUT.write_text(text)
    print(f"CSS inventory: {count} distinct registered names/IDs; static wiring requires separate consumer qualification")
    return 0


if __name__ == "__main__":
    sys.exit(main())
