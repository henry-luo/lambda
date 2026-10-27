#!/usr/bin/env python3
"""Stage a local or Developer ID signed InputMethodKit bundle."""

import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "lambda/host/macos_ime"
APP = ROOT / "temp/ime_chinese/m5/LambdaChineseIME.app"
SIGNED_APP = ROOT / "temp/ime_chinese/m5/distribution/LambdaChineseIME.app"
SIGNED_BUNDLE_ID = "org.lambda-script.inputmethod.chinese"


def checked_copy(source: Path, destination: Path, entry: dict) -> None:
    raw = source.read_bytes()
    if len(raw) != entry["bytes"] or hashlib.sha256(raw).hexdigest() != entry["sha256"]:
        raise ValueError(f"IME pack file failed manifest validation: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(raw)


def distribution_info(info: dict) -> dict:
    # keep the first notarized source distinct from the rejected development ID.
    info["CFBundleIdentifier"] = SIGNED_BUNDLE_ID
    info["InputMethodConnectionName"] = SIGNED_BUNDLE_ID + "_Connection"
    modes = info["ComponentInputModeDict"]
    properties = next(iter(modes["tsInputModeListKey"].values()))
    source_id = SIGNED_BUNDLE_ID + ".Simplified"
    properties["TISInputSourceID"] = source_id
    modes["tsInputModeListKey"] = {source_id: properties}
    modes["tsVisibleInputModeOrderedArrayKey"] = [source_id]
    return info


def sign_and_notarize(app: Path, identity: str, profile: str) -> None:
    subprocess.run(["codesign", "--force", "--sign", identity, "--options", "runtime",
                    "--entitlements", str(HOST / "entitlements.plist"), str(app)], check=True)
    subprocess.run(["codesign", "--verify", "--strict", str(app)], check=True)
    archive = ROOT / "temp/ime_chinese/m5/distribution/LambdaChineseIME.zip"
    archive.unlink(missing_ok=True)
    subprocess.run(["ditto", "-c", "-k", "--keepParent", str(app), str(archive)], check=True)
    submitted = subprocess.run(["xcrun", "notarytool", "submit", str(archive),
                                "--keychain-profile", profile, "--wait",
                                "--output-format", "json"], check=True, capture_output=True, text=True)
    result = json.loads(submitted.stdout)
    if result.get("status") != "Accepted":
        raise RuntimeError(f"Apple notarization did not accept the bundle: {result}")
    subprocess.run(["xcrun", "stapler", "staple", str(app)], check=True)
    subprocess.run(["xcrun", "stapler", "validate", str(app)], check=True)
    subprocess.run(["codesign", "--verify", "--strict", str(app)], check=True)
    subprocess.run(["spctl", "--assess", "--type", "execute", str(app)], check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sign-identity", help="Developer ID Application identity")
    parser.add_argument("--notary-profile", help="notarytool Keychain profile")
    args = parser.parse_args()
    if bool(args.sign_identity) != bool(args.notary_profile):
        parser.error("--sign-identity and --notary-profile must be supplied together")
    app = SIGNED_APP if args.sign_identity else APP
    executable = ROOT / "LambdaChineseIME"
    if not executable.is_file():
        raise FileNotFoundError(f"build macos-ime before staging: {executable}")
    if app.exists():
        shutil.rmtree(app)
    contents = app / "Contents"
    resources = contents / "Resources"
    lmd = resources / "lmd/package"
    (contents / "MacOS").mkdir(parents=True)
    (lmd / "ime").mkdir(parents=True)
    shutil.copy2(executable, contents / "MacOS/LambdaChineseIME")
    info = plistlib.loads((HOST / "Info.plist").read_bytes())
    if args.sign_identity:
        info = distribution_info(info)
    (contents / "Info.plist").write_bytes(plistlib.dumps(info))
    source_id = info["ComponentInputModeDict"]["tsVisibleInputModeOrderedArrayKey"][0]
    localization = resources / "en.lproj"
    localization.mkdir(parents=True)
    (localization / "InfoPlist.strings").write_text(
        f'"CFBundleName" = "Lambda Chinese";\n"{source_id}" = "Lambda Chinese";\n')
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
    # MIR allocates MAP_JIT pages in either signing profile.
    if args.sign_identity:
        sign_and_notarize(app, args.sign_identity, args.notary_profile)
    else:
        subprocess.run(["codesign", "--force", "--sign", "-", "--options", "runtime",
                        "--entitlements", str(HOST / "entitlements.plist"), str(app)], check=True)
    print(app)


if __name__ == "__main__":
    main()
