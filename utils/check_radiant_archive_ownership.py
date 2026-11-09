#!/usr/bin/env python3
"""Verify that native Radiant and Lambda adapter archives have distinct owners."""

import glob
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
from check_stale_object_dependencies import main as retire_stale_objects
from split_paths import sibling_roots

ROOT = Path(__file__).resolve().parent.parent


def members(archive: Path) -> set[str]:
    command = shlex.split(os.environ.get("AR", "ar")) + ["-t", str(archive)]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(f"cannot list {archive}: {result.stderr.strip()}")
    objects = [item for item in result.stdout.splitlines() if item.endswith(".o")]
    if not objects or len(set(objects)) != len(objects):
        raise RuntimeError(f"empty archive or duplicate object names: {archive}")
    return set(objects)


def makefile_objects(makefile: Path) -> set[str]:
    objects = set(re.findall(r"^OBJECTS \+= \$\(OBJDIR\)/([^\s]+\.o)$",
                             makefile.read_text(), re.MULTILINE))
    if not objects:
        raise RuntimeError(f"cannot determine object inventory from {makefile}")
    return objects


def prepare_archives() -> int:
    retire_stale_objects()
    # ar replaces named members but does not remove objects dropped from a
    # regenerated project. Retire only stale build artifacts before make runs.
    for target in ("radiant-cpp", "lambda-radiant-adapter-cpp", "lambda-data-cpp", "lambda-lib-cpp"):
        makefile = ROOT / "build/premake" / f"{target}.make"
        if not makefile.is_file():
            continue
        expected = makefile_objects(makefile)
        root = ROOT / "build/lib"
        archives = [root / f"lib{target}.a"] + list(root.glob(f"native/*/lib{target}.a"))
        for archive in archives:
            if archive.is_file() and members(archive) != expected:
                archive.unlink()
                print(f"BUILD_ARCHIVES: removed stale {archive} after source-list change")
    return 0


def main() -> int:
    try:
        if sys.argv[1:] == ["--prepare"]:
            return prepare_archives()
        if sys.argv[1:]:
            raise RuntimeError("usage: check_radiant_archive_ownership.py [--prepare]")
        config = json.loads((ROOT / "build_lambda_config.json").read_text())
        adapter = next(target for target in config["targets"] if target["name"] == "lambda-radiant-adapter")
        sources = [Path(source) for pattern in adapter["source_patterns"]
                   for source in glob.glob(str(ROOT / pattern))]
        expected = {source.stem + ".o" for source in sources}
        if not sources or len(sources) != len(expected):
            raise RuntimeError("adapter source list is empty or contains colliding object names")
        native = members(sibling_roots()[1] / "build/lib/debug/libradiant.a")
        actual = members(ROOT / "build/lib/native/debug/liblambda-radiant-adapter-cpp.a")
        if actual != expected:
            raise RuntimeError(f"adapter archive members differ: missing={sorted(expected - actual)}, "
                               f"extra={sorted(actual - expected)}")
        if overlap := native & expected:
            raise RuntimeError(f"native Radiant archive contains adapter objects: {sorted(overlap)}")
        print(f"RADIANT_ARCHIVES: {len(native)} native objects and {len(actual)} adapter objects have distinct owners")
        return 0
    except (OSError, ValueError, KeyError, StopIteration, RuntimeError) as exc:
        print(f"RADIANT_ARCHIVES: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
