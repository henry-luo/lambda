#!/usr/bin/env python3
"""Exercise negative native-module loader cases against copied Python bundles."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
MODULE_DIR = ROOT / "modules" / "lang-python"
TEST_ROOT = ROOT / "temp" / "jube-loader-negative"
HOST = Path(os.environ.get("LAMBDA_JUBE_HOST_EXE", ROOT / "lambda.exe")).resolve()
SCRIPT = ROOT / "test" / "py" / "test_py_basic.py"
EXPECTED_ERROR = "Hosted language module for 'py' is unavailable or incompatible."


def fail(message: str) -> None:
    print(f"JUBE_LOADER_NEGATIVE: {message}", file=sys.stderr)
    raise SystemExit(1)


def library_path(manifest: dict) -> Path:
    if sys.platform == "darwin":
        name = manifest.get("library_macos")
    elif sys.platform.startswith("linux"):
        name = manifest.get("library_linux")
    elif sys.platform == "win32":
        name = manifest.get("library_windows")
    else:
        fail(f"unsupported platform {sys.platform}")
    if not isinstance(name, str) or not name:
        fail("module manifest has no library for this platform")
    path = MODULE_DIR / name
    if not path.is_file():
        fail(f"native module is missing: {path.relative_to(ROOT)}")
    return path


def integrity_key() -> str:
    if sys.platform == "darwin":
        return "sha256_macos"
    if sys.platform.startswith("linux"):
        return "sha256_linux"
    if sys.platform == "win32":
        return "sha256_windows"
    fail(f"unsupported platform {sys.platform}")
    raise AssertionError("unreachable")


def write_bundle(case_name: str, manifest_bytes: bytes | None, copy_library: bool,
                 library: Path) -> Path:
    bundle = TEST_ROOT / case_name / "lang-python"
    bundle.mkdir(parents=True, exist_ok=True)
    if manifest_bytes is not None:
        (bundle / "module.json").write_bytes(manifest_bytes)
    if copy_library:
        shutil.copy2(library, bundle / library.name)
    return bundle.parent


def expect_rejection(case_name: str, bundle_root: Path, isolated_host: Path) -> None:
    environment = dict(os.environ)
    environment["JUBE_MODULE_PATH"] = str(bundle_root)
    completed = subprocess.run(
        [str(isolated_host), "py", str(SCRIPT), "--no-log"],
        # Run outside the repository root so the normal development bundle
        # cannot mask a rejection from this deliberately isolated bundle.
        cwd=TEST_ROOT,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode == 0:
        fail(f"{case_name} bundle was accepted")
    if EXPECTED_ERROR not in completed.stderr:
        fail(f"{case_name} rejection did not report the stable unavailable/incompatible error")


def main() -> int:
    if not HOST.is_file():
        fail("lambda.exe is missing; build the host first")
    try:
        manifest_bytes = (MODULE_DIR / "module.json").read_bytes()
        manifest = json.loads(manifest_bytes)
    except (OSError, json.JSONDecodeError) as error:
        fail(f"could not read development manifest: {error}")
    library = library_path(manifest)

    if TEST_ROOT.exists():
        shutil.rmtree(TEST_ROOT)
    TEST_ROOT.mkdir(parents=True)
    isolated_host = TEST_ROOT / "lambda.exe"
    shutil.copy2(HOST, isolated_host)

    missing_library = write_bundle("missing-library", manifest_bytes, False, library)
    expect_rejection("missing-library", missing_library, isolated_host)

    incomplete = dict(manifest)
    del incomplete["host_build_id"]
    incomplete_bytes = (json.dumps(incomplete, indent=2, sort_keys=True) + "\n").encode()
    incomplete_manifest = write_bundle("incomplete-manifest", incomplete_bytes, False, library)
    expect_rejection("incomplete-manifest", incomplete_manifest, isolated_host)

    wrong_build = dict(manifest)
    wrong_build["host_build_id"] = "incompatible-host-build"
    wrong_build_bytes = (json.dumps(wrong_build, indent=2, sort_keys=True) + "\n").encode()
    wrong_host_build = write_bundle("wrong-host-build", wrong_build_bytes, True, library)
    expect_rejection("wrong-host-build", wrong_host_build, isolated_host)

    wrong_base_abi = dict(manifest)
    wrong_base_abi["base_abi_version"] = int(manifest["base_abi_version"]) + 1
    wrong_base_abi_bytes = (json.dumps(wrong_base_abi, indent=2, sort_keys=True) + "\n").encode()
    wrong_base_abi_root = write_bundle("wrong-base-abi", wrong_base_abi_bytes, False, library)
    expect_rejection("wrong-base-abi", wrong_base_abi_root, isolated_host)

    wrong_hosted_abi = dict(manifest)
    wrong_hosted_abi["hosted_api_version"] = int(manifest["hosted_api_version"]) + 1
    wrong_hosted_abi_bytes = (json.dumps(wrong_hosted_abi, indent=2, sort_keys=True) + "\n").encode()
    wrong_hosted_abi_root = write_bundle("wrong-hosted-abi", wrong_hosted_abi_bytes,
                                         False, library)
    expect_rejection("wrong-hosted-abi", wrong_hosted_abi_root, isolated_host)

    missing_checksum = dict(manifest)
    del missing_checksum[integrity_key()]
    missing_checksum_bytes = (json.dumps(missing_checksum, indent=2, sort_keys=True) + "\n").encode()
    missing_checksum_root = write_bundle("missing-checksum", missing_checksum_bytes,
                                         False, library)
    expect_rejection("missing-checksum", missing_checksum_root, isolated_host)

    checksum_mismatch_root = write_bundle("checksum-mismatch", manifest_bytes, True, library)
    checksum_library = checksum_mismatch_root / "lang-python" / library.name
    with checksum_library.open("r+b") as file:
        first_byte = file.read(1)
        if not first_byte:
            fail("native module is empty")
        file.seek(0)
        file.write(bytes([first_byte[0] ^ 0x01]))
    expect_rejection("checksum-mismatch", checksum_mismatch_root, isolated_host)

    wrong_entry = dict(manifest)
    wrong_entry["entry_symbol"] = "missing_jube_module_entry"
    wrong_entry_bytes = (json.dumps(wrong_entry, indent=2, sort_keys=True) + "\n").encode()
    wrong_entry_root = write_bundle("wrong-entry-symbol", wrong_entry_bytes, True, library)
    expect_rejection("wrong-entry-symbol", wrong_entry_root, isolated_host)

    corrupt_manifest_root = write_bundle(
        "corrupt-manifest", b'{"language":"python","aliases":["py"], broken', False, library)
    expect_rejection("corrupt-manifest", corrupt_manifest_root, isolated_host)

    print("JUBE_LOADER_NEGATIVE: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
