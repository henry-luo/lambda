#!/usr/bin/env python3
"""Remove build-local compatibility metadata from Jube module manifests."""

import json
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: update_jube_manifest_integrity.py <module-dir>", file=sys.stderr)
        return 2
    manifest_path = Path(sys.argv[1]) / "module.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"JUBE_MANIFEST: cannot read {manifest_path}: {error}", file=sys.stderr)
        return 1
    manifest.pop("host_build_id", None)
    manifest.pop("sha256_macos", None)
    manifest.pop("sha256_linux", None)
    manifest.pop("sha256_windows", None)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
