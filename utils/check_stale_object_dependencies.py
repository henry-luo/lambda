#!/usr/bin/env python3
"""Retire objects whose generated rule now names a different source file."""

from pathlib import Path
import re
import shlex

ROOT = Path(__file__).resolve().parent.parent
RULE = re.compile(r"^\$\(OBJDIR\)/([^:\n]+\.o):\s*(.+)$", re.MULTILINE)


def retire_stale_objects(makefile: Path, object_root: Path) -> list[Path]:
    sources = {}
    for name, raw_source in RULE.findall(makefile.read_text()):
        tokens = shlex.split(raw_source)
        if len(tokens) != 1 or "$" in tokens[0]:
            continue
        source = (makefile.parent / tokens[0]).resolve()
        if name in sources and sources[name] != source:
            raise RuntimeError(f"ambiguous source rule for {name} in {makefile}")
        sources[name] = source

    removed = []
    for dependency in object_root.glob("*/*/*.d"):
        name = dependency.with_suffix(".o").name
        if name not in sources:
            continue
        # The compiler lists the translation unit first, before its headers.
        with dependency.open() as stream:
            prefix = stream.read(4096).replace("\\\n", " ")
        colon = prefix.find(":")
        token = re.match(r"\s*((?:\\.|[^\s\\])+)", prefix[colon + 1:]) if colon >= 0 else None
        recorded = shlex.split(token.group(1))[0] if token else None
        if not recorded:
            raise RuntimeError(f"cannot read source from {dependency}")
        if (makefile.parent / recorded).resolve() == sources[name]:
            continue
        obj = dependency.with_suffix(".o")
        dependency.unlink()
        obj.unlink(missing_ok=True)
        removed.append(obj)
    return removed


def main() -> None:
    for makefile in (ROOT / "build/premake").glob("*.make"):
        object_root = ROOT / "build/obj" / makefile.stem
        if not object_root.is_dir():
            continue
        for obj in retire_stale_objects(makefile, object_root):
            print(f"BUILD_OBJECTS: removed {obj} after source ownership changed")


if __name__ == "__main__":
    main()
