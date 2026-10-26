#!/usr/bin/env python3
"""Stage a local InputMethodKit bundle from the macos-ime executable and packs."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "lambda/host/macos_ime"
APP = ROOT / "temp/ime_chinese/m5/LambdaChineseIME.app"


def checked_copy(source: Path, destination: Path, entry: dict) -> None:
    raw = source.read_bytes()
    if len(raw) != entry["bytes"] or hashlib.sha256(raw).hexdigest() != entry["sha256"]:
        raise ValueError(f"IME pack file failed manifest validation: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(raw)


def main() -> None:
    executable = ROOT / "LambdaChineseIME"
    if not executable.is_file():
        raise FileNotFoundError(f"build macos-ime before staging: {executable}")
    if APP.exists():
        shutil.rmtree(APP)
    contents = APP / "Contents"
    resources = contents / "Resources"
    lmd = resources / "lmd/package"
    (contents / "MacOS").mkdir(parents=True)
    (lmd / "ime").mkdir(parents=True)
    shutil.copy2(executable, contents / "MacOS/LambdaChineseIME")
    shutil.copy2(HOST / "Info.plist", contents / "Info.plist")
    shutil.copy2(ROOT / "lmd/package/ime/chinese.ls", lmd / "ime/chinese.ls")
    source_modules = ROOT / "lmd/package/ime/chinese"
    for source in source_modules.glob("*.ls"):
        target = lmd / "ime/chinese" / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    shutil.copy2(source_modules / "syllables.txt", lmd / "ime/chinese/syllables.txt")
    for name in ("line_index.ls", "utf8.ls", "search.ls"):
        target = lmd / "io" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "lmd/package/io" / name, target)
    for profile in ("zh-Hans", "zh-Hant"):
        source = source_modules / "data" / profile
        manifest = json.loads((source / "manifest.json").read_text())
        if manifest["profile"] != profile or manifest["format"] != "lambda.ime.chinese.pack":
            raise ValueError(f"invalid IME profile manifest: {profile}")
        if "sogou" in manifest.get("sources", {}):
            raise ValueError(f"private Sogou data cannot enter the default bundle: {profile}")
        target = lmd / "ime/chinese/data" / profile
        target.mkdir(parents=True, exist_ok=True)
        for entry in (manifest["files"]["lexicon"], manifest["files"]["abbrev"],
                      manifest["notice"]):
            checked_copy(source / entry["path"], target / entry["path"], entry)
        shutil.copy2(source / "manifest.json", target / "manifest.json")
    icon = resources / "LambdaChineseIME.png"
    subprocess.run(["rsvg-convert", "-o", str(icon), str(HOST / "icon.svg")], check=True)
    subprocess.run(["sips", "-s", "format", "tiff", str(icon), "--out",
                    str(resources / "LambdaChineseIME.tiff")], check=True,
                   stdout=subprocess.DEVNULL)
    icon.unlink()
    # MIR allocates MAP_JIT pages; validate the local bundle under the same runtime policy.
    subprocess.run(["codesign", "--force", "--sign", "-", "--options", "runtime",
                    "--entitlements", str(HOST / "entitlements.plist"), str(APP)], check=True)
    print(APP)


if __name__ == "__main__":
    main()
