#!/usr/bin/env python3
"""Ratchet the D10.2.1/D9.5.6 editable protocol ownership boundary."""

import os
import re
import sys
from pathlib import Path


RETIRED_NATIVE_SYMBOLS = (
    "SYSPROC_SET_SELECTION",
    "pn_set_selection",
    "lambda_radiant_set_selection",
    "dispatch_set_selection",
)
REGISTRY_OWNER = Path("lmd/package/dom/edit_registry.ls")
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp", ".ls"}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[4]


def production_sources(root: Path):
    radiant = Path(os.environ.get('RADIANT_ROOT', root.parent / 'radiant')).resolve() / 'radiant'
    for source_root in (root / "lambda", root / "lmd/package/dom", root / "lmd/package/editor", radiant):
        for path in source_root.rglob("*"):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                yield path


def main() -> int:
    root = repo_root()
    failures = []
    base_registry_declarations = []

    for path in production_sources(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        if path.is_relative_to(root):
            rel = path.relative_to(root)
        else:
            radiant = Path(os.environ.get('RADIANT_ROOT', root.parent / 'radiant')).resolve()
            rel = path.relative_to(radiant)
        for symbol in RETIRED_NATIVE_SYMBOLS:
            if re.search(rf"\b{re.escape(symbol)}\b", text):
                failures.append(f"{rel}: retired editable ABI symbol {symbol}")
        if re.search(r"\blet\s+base_registry\s*=", text):
            base_registry_declarations.append(rel)
        if rel.parent == Path("lmd/package/editor") and re.search(
            r"\bfn\s+(extension_key|standard_edit_registry)\b", text
        ):
            failures.append(f"{rel}: duplicate editor-side standard registry")

    if base_registry_declarations != [REGISTRY_OWNER]:
        found = ", ".join(str(path) for path in base_registry_declarations) or "none"
        failures.append(
            f"standard editable registry must be owned only by {REGISTRY_OWNER}; found {found}"
        )

    if failures:
        print("dom-editable-architecture: boundary violations:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print(
        "dom-editable-architecture: shared registry has one owner and "
        "the ambient selection ABI remains retired"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
