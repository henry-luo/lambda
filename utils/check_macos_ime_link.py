#!/usr/bin/env python3
"""Invalidate an IME executable when its generated Premake profile changes."""

import hashlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "temp/ime_chinese/m5"


def main() -> None:
    if len(sys.argv) != 2 or sys.argv[1] not in ("LambdaChineseIME", "LambdaChineseIME-debug"):
        raise SystemExit("usage: check_macos_ime_link.py LambdaChineseIME[-debug]")
    name = sys.argv[1]
    digest = hashlib.sha256((ROOT / "premake5.ime.macos.lua").read_bytes()).hexdigest()
    WORK.mkdir(parents=True, exist_ok=True)
    stamp = WORK / f"{name}.profile.sha256"
    if not stamp.exists() or stamp.read_text().strip() != digest:
        # Premake's generated target does not depend on the profile file itself.
        (ROOT / name).unlink(missing_ok=True)
        stamp.write_text(digest + "\n")


if __name__ == "__main__":
    main()
