#!/usr/bin/env python3
"""Audit compiler-recorded dependencies for every member of the input archives."""

import os
from pathlib import Path
import shlex
import subprocess
import sys
from split_paths import sibling_roots

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build" / "premake"
INPUT_ROOT, RADIANT_ROOT = sibling_roots()
FORBIDDEN = (("lambda", "runtime"), ("lambda", "js"), ("lambda", "dom"),
             ("lambda", "module"), ("radiant",))
ARCHIVES = (
    (ROOT / "build/lib/liblambda-data-cpp.a", ROOT / "build/obj/lambda-data-cpp/native/debug"),
    (ROOT / "build/lib/native/debug/liblambda-lib-cpp.a", ROOT / "build/obj/lambda-lib-cpp/native/debug"),
)


def input_dependencies(depfile: Path):
    # -MD emits a continued primary rule followed by -MP phony header rules.
    primary = depfile.read_text().replace("\\\n", " ").splitlines()[0]
    _, separator, prerequisites = primary.partition(": ")
    if not separator:
        raise ValueError(f"invalid dependency rule: {depfile}")
    for token in shlex.split(prerequisites):
        if not token.startswith("..") and not Path(token).is_absolute():
            continue
        source = Path(os.path.normpath(BUILD_DIR / token))
        for project, prefix in ((INPUT_ROOT, Path()),
                                (RADIANT_ROOT / 'radiant', Path('radiant')),
                                (ROOT, Path())):
            try:
                yield prefix / source.relative_to(project)
                break
            except ValueError:
                continue


def dependency_file(relative: Path) -> Path:
    """Locate a logical dependency in its owning sibling or Lambda."""
    if relative.parts[0] == 'lib':
        return INPUT_ROOT / relative
    if relative.parts[0] == 'radiant':
        return RADIANT_ROOT / relative
    local = ROOT / relative
    return local if local.exists() else INPUT_ROOT / relative


def archive_members(archive: Path) -> list[str]:
    command = shlex.split(os.environ.get("AR", "ar")) + ["-t", str(archive)]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(f"cannot list {archive}: {result.stderr.strip()}")
    return [name for name in result.stdout.splitlines() if name.endswith(".o")]


def check_archive(archive: Path, object_dir: Path) -> bool:
    try:
        objects = archive_members(archive)
    except RuntimeError as exc:
        print(f"INPUT_ARCHIVE_HEADERS: {exc}", file=sys.stderr)
        return False
    if not objects or len(set(objects)) != len(objects):
        print(f"INPUT_ARCHIVE_HEADERS: empty archive or duplicate object names: {archive}", file=sys.stderr)
        return False
    errors = []
    for name in objects:
        obj = object_dir / name
        depfile = object_dir / (Path(name).stem + ".d")
        if not obj.is_file() or not depfile.is_file():
            errors.append(f"{name}: object or compiler dependency file missing")
            continue
        try:
            dependencies = list(input_dependencies(depfile))
        except (ValueError, OSError) as exc:
            errors.append(str(exc))
            continue
        if not dependencies:
            errors.append(f"{name}: no workspace dependencies recorded")
            continue
        for rel in dependencies:
            if any(rel.parts[:len(prefix)] == prefix for prefix in FORBIDDEN):
                errors.append(f"{name}: forbidden dependency {rel}")
            file = dependency_file(rel)
            if not file.is_file() or file.stat().st_mtime_ns > obj.stat().st_mtime_ns:
                errors.append(f"{name}: missing or newer dependency {rel}")
    if errors:
        for error in sorted(set(errors)):
            print(f"INPUT_ARCHIVE_HEADERS: {error}", file=sys.stderr)
        return False
    print(f"INPUT_ARCHIVE_HEADERS: {archive.name}: {len(objects)} compiler dependency sets checked")
    return True


def check_archive_ownership() -> bool:
    owners = {}
    try:
        for archive, object_dir in ARCHIVES:
            for member in archive_members(archive):
                depfile = object_dir / (Path(member).stem + ".d")
                source = next(input_dependencies(depfile))
                previous = owners.setdefault(source, archive.name)
                if previous != archive.name:
                    print(f"INPUT_ARCHIVE_HEADERS: {source} belongs to both {previous} and {archive.name}",
                          file=sys.stderr)
                    return False
    except (OSError, ValueError, RuntimeError, StopIteration) as exc:
        print(f"INPUT_ARCHIVE_HEADERS: cannot verify ownership: {exc}", file=sys.stderr)
        return False
    print(f"INPUT_ARCHIVE_HEADERS: {len(owners)} source files have distinct archive owners")
    return True


def main() -> int:
    archives_ok = all([check_archive(archive, objects) for archive, objects in ARCHIVES])
    return 0 if archives_ok and check_archive_ownership() else 1


if __name__ == "__main__":
    raise SystemExit(main())
