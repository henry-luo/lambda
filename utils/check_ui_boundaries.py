#!/usr/bin/env python3
"""Check that neutral UI and desktop workspace imports do not load themes."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = ROOT / "lmd/package"
UI = PACKAGES / "ui"
IMPORT = re.compile(r"^\s*import\s+(.+)$")
IMPORT_ITEM = re.compile(r"^\s*(?:\w+\s*:\s*)?([.\w~]+)")
VISUAL = {"dtna", "bold", "wired", "clay", "blade"}


def resolve(source, name):
    if name.startswith("lambda."):
        path = PACKAGES.joinpath(*name.split(".")[1:])
    elif name.startswith("."):
        path = source.parent.joinpath(*name[1:].split("."))
    elif name.startswith("~~."):
        parts = name.split(".")
        parent = source.parent
        while parts and parts[0] == "~~":
            parent = parent.parent
            parts.pop(0)
        path = parent.joinpath(*parts)
    else:
        return None
    path = path.with_suffix(".ls")
    return path.resolve() if path.is_file() else None


def imports(source):
    for line in source.read_text().splitlines():
        match = IMPORT.match(line)
        if match:
            for item in match.group(1).split(","):
                part = IMPORT_ITEM.match(item)
                name = part.group(1) if part else ""
                if name.startswith("lambda.ui.") and name.split(".")[2] in VISUAL:
                    raise ValueError(f"visual-family import {name} in {source.relative_to(ROOT)}")
                target = resolve(source, name) if name else None
                if target:
                    yield target


def visual_family(source):
    try:
        parts = source.relative_to(UI).parts
    except ValueError:
        return None
    family = parts[0].removesuffix(".ls")
    return family if family in VISUAL else None


def check(entry, done, active):
    if entry in active:
        raise ValueError("cyclic import: " + " -> ".join(str(p.relative_to(ROOT)) for p in [*active, entry]))
    if entry in done:
        return
    if visual_family(entry):
        raise ValueError("visual-family dependency: " + " -> ".join(str(p.relative_to(ROOT)) for p in [*active, entry]))
    active.append(entry)
    for dependency in imports(entry):
        check(dependency, done, active)
    active.pop()
    done.add(entry)


def main():
    entries = [*sorted((UI / "core").glob("*.ls")), UI / "workspace.ls", *sorted((UI / "workspace").glob("*.ls"))]
    seen = set()
    try:
        for entry in entries:
            check(entry.resolve(), seen, [])
    except ValueError as error:
        print(f"UI package boundary failed: {error}", file=sys.stderr)
        return 1
    print(f"UI package boundaries passed: {len(entries)} entries, {len(seen)} dependencies")
    return 0


if __name__ == "__main__":
    sys.exit(main())
