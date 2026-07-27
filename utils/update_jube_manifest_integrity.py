#!/usr/bin/env python3
"""Write the platform library digest into a generated Jube module manifest."""

import hashlib
import json
import platform
import re
import sys
from pathlib import Path


def library_name() -> tuple[str, str]:
    system = platform.system()
    if system == "Darwin":
        return "library_macos", "sha256_macos"
    if system == "Windows":
        return "library_windows", "sha256_windows"
    return "library_linux", "sha256_linux"


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def host_build_id() -> str:
    header = Path(__file__).resolve().parent.parent / "lambda" / "jube" / "jube.h"
    try:
        source = header.read_text(encoding="utf-8")
    except OSError as error:
        raise RuntimeError(f"cannot read {header}: {error}") from error
    match = re.search(r'^#define\s+JUBE_HOST_BUILD_ID\s+"([^"]+)"$', source, re.MULTILINE)
    if not match:
        raise RuntimeError(f"JUBE_HOST_BUILD_ID is missing from {header}")
    return match.group(1)


def main() -> int:
    force = False
    arguments = sys.argv[1:]
    if arguments and arguments[0] == "--force":
        force = True
        arguments = arguments[1:]
    if len(arguments) != 1:
        print("usage: update_jube_manifest_integrity.py [--force] <module-dir>", file=sys.stderr)
        return 2
    module_dir = Path(arguments[0])
    manifest_path = module_dir / "module.json"
    library_key, digest_key = library_name()
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        library = module_dir / manifest[library_key]
    except (OSError, KeyError, json.JSONDecodeError) as error:
        print(f"JUBE_MANIFEST: cannot read {manifest_path}: {error}", file=sys.stderr)
        return 1
    # Hosted-language descriptors are source-controlled compatibility metadata.
    # Their binary digest and host build identity vary per local build, so only
    # an explicit disposable-fixture update is allowed to stamp those fields.
    if manifest.get("language") and not force:
        manifest.pop("host_build_id", None)
        manifest.pop("sha256_macos", None)
        manifest.pop("sha256_linux", None)
        manifest.pop("sha256_windows", None)
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return 0
    if not library.is_file():
        print(f"JUBE_MANIFEST: native library is missing: {library}", file=sys.stderr)
        return 1
    try:
        manifest["host_build_id"] = host_build_id()
    except RuntimeError as error:
        print(f"JUBE_MANIFEST: {error}", file=sys.stderr)
        return 1
    manifest[digest_key] = digest(library)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
