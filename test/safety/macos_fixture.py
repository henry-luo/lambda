"""Shared signing fixture; all bundles and entitlement files stay under temp/."""

from pathlib import Path
import plistlib
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run(command: list[str]) -> str:
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(f"{command[0]} exited {result.returncode}: {result.stderr.strip()}")
    return result.stdout


def stage_bundle(directory: Path, name: str, binary: Path, identity: str, sandboxed: bool,
                 identifier: str | None = None, extra_entitlements: dict | None = None) -> Path:
    app = directory / (name + ".app")
    contents = app / "Contents"
    executable = contents / "MacOS/worker"
    executable.parent.mkdir(parents=True)
    shutil.copy2(binary, executable)
    info = {"CFBundleIdentifier": identifier or "org.lambda-script.safety-qualification." + name.lower(),
            "CFBundleExecutable": "worker", "CFBundleName": "Lambda Safety Qualification",
            "CFBundlePackageType": "APPL", "CFBundleVersion": "1"}
    (contents / "Info.plist").write_bytes(plistlib.dumps(info))
    entitlements = directory / (name + ".entitlements.plist")
    entitlement_values = {"com.apple.security.app-sandbox": True} if sandboxed else {}
    entitlement_values.update(extra_entitlements or {})
    entitlements.write_bytes(plistlib.dumps(entitlement_values))
    run(["codesign", "--force", "--sign", identity, "--options", "runtime",
         "--entitlements", str(entitlements), str(app)])
    run(["codesign", "--verify", "--strict", str(app)])
    return executable
