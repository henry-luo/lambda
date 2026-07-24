#!/usr/bin/env python3
"""Exercise negative native-module loader cases against copied Python bundles."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import hashlib


ROOT = Path(__file__).resolve().parent.parent
MODULE_DIR = ROOT / "modules" / "lang-python"
TEST_ROOT = ROOT / "temp" / "jube-loader-negative"
HOST = Path(os.environ.get("LAMBDA_JUBE_HOST_EXE", ROOT / "lambda.exe")).resolve()
SCRIPT = ROOT / "test" / "py" / "test_py_basic.py"
EXPECTED_ERROR = "Hosted language module for 'py' is unavailable or incompatible."
INIT_FAILURE_SOURCE = ROOT / "test" / "jube" / "jube_init_failure_module.cpp"


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


def fixture_library_name(fixture_name: str) -> str:
    if sys.platform == "darwin":
        return f"lang-python-{fixture_name}.dylib"
    if sys.platform.startswith("linux"):
        return f"lang-python-{fixture_name}.so"
    fail("the init-failure fixture currently requires a POSIX C++ compiler")
    raise AssertionError("unreachable")


def build_fixture_library(fixture_name: str,
                          compiler_defines: list[str] | None = None) -> Path:
    if not INIT_FAILURE_SOURCE.is_file():
        fail(f"init-failure fixture source is missing: {INIT_FAILURE_SOURCE.relative_to(ROOT)}")
    output = TEST_ROOT / fixture_library_name(fixture_name)
    compiler = os.environ.get("CXX", "clang++")
    command = [compiler, "-std=c++17", "-I", str(ROOT)]
    for compiler_define in compiler_defines or []:
        command.append(f"-D{compiler_define}")
    if sys.platform == "darwin":
        command.append("-dynamiclib")
    else:
        command.extend(["-shared", "-fPIC"])
    command.extend([str(INIT_FAILURE_SOURCE), "-o", str(output)])
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, check=False)
    if completed.returncode != 0 or not output.is_file():
        fail(f"could not build {fixture_name} fixture: {completed.stderr.strip()}")
    return output


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_module(bundle_root: Path, directory_name: str, manifest_bytes: bytes | None,
                 copy_library: bool, library: Path) -> None:
    bundle = bundle_root / directory_name
    bundle.mkdir(parents=True, exist_ok=True)
    if manifest_bytes is not None:
        (bundle / "module.json").write_bytes(manifest_bytes)
    if copy_library:
        shutil.copy2(library, bundle / library.name)


def write_bundle(case_name: str, manifest_bytes: bytes | None, copy_library: bool,
                 library: Path) -> Path:
    bundle_root = TEST_ROOT / case_name
    write_module(bundle_root, "lang-python", manifest_bytes, copy_library, library)
    return bundle_root


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


def expect_init_failure_rejection(bundle_root: Path, isolated_host: Path) -> None:
    marker = TEST_ROOT / "failed-init-shutdown.marker"
    environment = dict(os.environ)
    environment["JUBE_MODULE_PATH"] = str(bundle_root)
    environment["JUBE_INIT_FAILURE_MARKER"] = str(marker)
    completed = subprocess.run(
        [str(isolated_host), "py", str(SCRIPT), "--no-log"],
        cwd=TEST_ROOT,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode == 0:
        fail("failed-initialization bundle was accepted")
    if EXPECTED_ERROR not in completed.stderr:
        fail("failed-initialization rejection did not report the stable unavailable/incompatible error")
    if not marker.is_file() or marker.read_text() != "shutdown\n":
        fail("failed initializer did not receive shutdown rollback")


def expect_stale_cursor_rejection(bundle_root: Path, isolated_host: Path) -> None:
    marker = TEST_ROOT / "stale-cursor.marker"
    environment = dict(os.environ)
    environment["JUBE_MODULE_PATH"] = str(bundle_root)
    environment["JUBE_STALE_CURSOR_MARKER"] = str(marker)
    completed = subprocess.run(
        [str(isolated_host), "py", str(SCRIPT), "--no-log"],
        cwd=TEST_ROOT,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode == 0:
        fail("stale-cursor bundle was accepted")
    if EXPECTED_ERROR not in completed.stderr:
        fail("stale-cursor rejection did not report the stable unavailable/incompatible error")
    if not marker.is_file() or marker.read_text() != "rejected\n":
        fail("host accepted a stale or manufactured compiler cursor")


def expect_wrong_owner_rejection(bundle_root: Path, isolated_host: Path) -> None:
    marker = TEST_ROOT / "wrong-owner.marker"
    environment = dict(os.environ)
    environment["JUBE_MODULE_PATH"] = str(bundle_root)
    environment["JUBE_WRONG_OWNER_MARKER"] = str(marker)
    completed = subprocess.run(
        [str(isolated_host), "py", str(SCRIPT), "--no-log"],
        cwd=TEST_ROOT,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode == 0:
        fail("wrong-owner bundle was accepted")
    if EXPECTED_ERROR not in completed.stderr:
        fail("wrong-owner rejection did not report the stable unavailable/incompatible error")
    if not marker.is_file() or marker.read_text() != "rejected\n":
        fail("host accepted a compiler function handle from another cursor")


def expect_missing_capability_rejection(bundle_root: Path, isolated_host: Path) -> None:
    marker = TEST_ROOT / "missing-capability-init.marker"
    environment = dict(os.environ)
    environment["JUBE_MODULE_PATH"] = str(bundle_root)
    environment["JUBE_CAPABILITY_INIT_MARKER"] = str(marker)
    completed = subprocess.run(
        [str(isolated_host), "py", str(SCRIPT), "--no-log"],
        cwd=TEST_ROOT,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode == 0:
        fail("missing-capability bundle was accepted")
    if EXPECTED_ERROR not in completed.stderr:
        fail("missing-capability rejection did not report the stable unavailable/incompatible error")
    if marker.exists():
        fail("missing capability reached the module initializer")


def expect_descriptor_rejection(case_name: str, bundle_root: Path, isolated_host: Path) -> None:
    marker = TEST_ROOT / f"{case_name}-init.marker"
    environment = dict(os.environ)
    environment["JUBE_MODULE_PATH"] = str(bundle_root)
    environment["JUBE_DESCRIPTOR_INIT_MARKER"] = str(marker)
    completed = subprocess.run(
        [str(isolated_host), "py", str(SCRIPT), "--no-log"],
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
    if marker.exists():
        fail(f"{case_name} reached the module initializer")


def expect_dependency_rollback(bundle_root: Path, isolated_host: Path) -> None:
    init_marker = TEST_ROOT / "dependency-init.marker"
    shutdown_marker = TEST_ROOT / "dependency-shutdown.marker"
    environment = dict(os.environ)
    environment["JUBE_MODULE_PATH"] = str(bundle_root)
    environment["JUBE_DEPENDENCY_INIT_MARKER"] = str(init_marker)
    environment["JUBE_DEPENDENCY_SHUTDOWN_MARKER"] = str(shutdown_marker)
    completed = subprocess.run(
        [str(isolated_host), "py", str(SCRIPT), "--no-log"],
        cwd=TEST_ROOT,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode == 0:
        fail("dependency rollback bundle was accepted")
    if EXPECTED_ERROR not in completed.stderr:
        fail("dependency rollback did not report the stable unavailable/incompatible error")
    if not init_marker.is_file() or init_marker.read_text() != "init\n":
        fail("dependency did not initialize before its dependent failed")
    if not shutdown_marker.is_file() or shutdown_marker.read_text() != "shutdown\n":
        fail("dependency was not shut down during dependent rollback")


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

    unsafe_resource = dict(manifest)
    unsafe_resource["resources"] = ["../outside-the-module"]
    unsafe_resource_bytes = (json.dumps(unsafe_resource, indent=2, sort_keys=True) + "\n").encode()
    unsafe_resource_root = write_bundle("unsafe-resource", unsafe_resource_bytes, True, library)
    expect_rejection("unsafe-resource", unsafe_resource_root, isolated_host)

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

    if sys.platform != "win32":
        init_failure_library = build_fixture_library("init-failure")
        failed_init = dict(manifest)
        fixture_name = init_failure_library.name
        if sys.platform == "darwin":
            failed_init["library_macos"] = fixture_name
        else:
            failed_init["library_linux"] = fixture_name
        failed_init[integrity_key()] = sha256_file(init_failure_library)
        failed_init_bytes = (json.dumps(failed_init, indent=2, sort_keys=True) + "\n").encode()
        failed_init_root = write_bundle("failed-initialization", failed_init_bytes, True,
                                        init_failure_library)
        expect_init_failure_rejection(failed_init_root, isolated_host)

        stale_cursor_library = build_fixture_library(
            "stale-cursor", ["JUBE_TEST_STALE_CURSOR"])
        stale_cursor = dict(manifest)
        fixture_name = stale_cursor_library.name
        if sys.platform == "darwin":
            stale_cursor["library_macos"] = fixture_name
        else:
            stale_cursor["library_linux"] = fixture_name
        stale_cursor[integrity_key()] = sha256_file(stale_cursor_library)
        stale_cursor_bytes = (
            json.dumps(stale_cursor, indent=2, sort_keys=True) + "\n").encode()
        stale_cursor_root = write_bundle("stale-cursor", stale_cursor_bytes, True,
                                         stale_cursor_library)
        expect_stale_cursor_rejection(stale_cursor_root, isolated_host)

        wrong_owner_library = build_fixture_library(
            "wrong-owner", ["JUBE_TEST_WRONG_OWNER"])
        wrong_owner = dict(manifest)
        fixture_name = wrong_owner_library.name
        if sys.platform == "darwin":
            wrong_owner["library_macos"] = fixture_name
        else:
            wrong_owner["library_linux"] = fixture_name
        wrong_owner[integrity_key()] = sha256_file(wrong_owner_library)
        wrong_owner_bytes = (
            json.dumps(wrong_owner, indent=2, sort_keys=True) + "\n").encode()
        wrong_owner_root = write_bundle("wrong-owner", wrong_owner_bytes, True,
                                        wrong_owner_library)
        expect_wrong_owner_rejection(wrong_owner_root, isolated_host)

        missing_capability_library = build_fixture_library(
            "missing-capability", ["JUBE_TEST_REQUIRES_MISSING_CAPABILITY"])
        missing_capability = dict(manifest)
        fixture_name = missing_capability_library.name
        if sys.platform == "darwin":
            missing_capability["library_macos"] = fixture_name
        else:
            missing_capability["library_linux"] = fixture_name
        missing_capability[integrity_key()] = sha256_file(missing_capability_library)
        missing_capability_bytes = (
            json.dumps(missing_capability, indent=2, sort_keys=True) + "\n").encode()
        missing_capability_root = write_bundle("missing-capability", missing_capability_bytes,
                                               True, missing_capability_library)
        expect_missing_capability_rejection(missing_capability_root, isolated_host)

        unsupported_abi_library = build_fixture_library(
            "unsupported-descriptor-abi", ["JUBE_TEST_UNSUPPORTED_ABI"])
        unsupported_abi = dict(manifest)
        fixture_name = unsupported_abi_library.name
        if sys.platform == "darwin":
            unsupported_abi["library_macos"] = fixture_name
        else:
            unsupported_abi["library_linux"] = fixture_name
        unsupported_abi[integrity_key()] = sha256_file(unsupported_abi_library)
        unsupported_abi_bytes = (
            json.dumps(unsupported_abi, indent=2, sort_keys=True) + "\n").encode()
        unsupported_abi_root = write_bundle("unsupported-descriptor-abi",
                                            unsupported_abi_bytes, True,
                                            unsupported_abi_library)
        expect_descriptor_rejection("unsupported-descriptor-abi", unsupported_abi_root,
                                    isolated_host)

        undersized_library = build_fixture_library(
            "undersized-descriptor", ["JUBE_TEST_UNDERSIZED_DESCRIPTOR"])
        undersized = dict(manifest)
        fixture_name = undersized_library.name
        if sys.platform == "darwin":
            undersized["library_macos"] = fixture_name
        else:
            undersized["library_linux"] = fixture_name
        undersized[integrity_key()] = sha256_file(undersized_library)
        undersized_bytes = (json.dumps(undersized, indent=2, sort_keys=True) + "\n").encode()
        undersized_root = write_bundle("undersized-descriptor", undersized_bytes, True,
                                      undersized_library)
        expect_descriptor_rejection("undersized-descriptor", undersized_root, isolated_host)

        missing_dependency = dict(manifest)
        missing_dependency["dependencies"] = ["missing-test-dependency"]
        missing_dependency_bytes = (
            json.dumps(missing_dependency, indent=2, sort_keys=True) + "\n").encode()
        missing_dependency_root = write_bundle("missing-dependency", missing_dependency_bytes,
                                               True, library)
        expect_rejection("missing-dependency", missing_dependency_root, isolated_host)

        dependency_library = build_fixture_library(
            "dependency-success", ["JUBE_TEST_SUCCESS_INIT",
                                   'JUBE_TEST_MODULE_NAME="lang-python-dependency"'])
        dependency_manifest = dict(manifest)
        dependency_manifest["name"] = "lang-python-dependency"
        dependency_manifest["language"] = "test-dependency"
        dependency_manifest["aliases"] = ["test-dependency"]
        dependency_manifest["extensions"] = [".test-dependency"]
        if sys.platform == "darwin":
            dependency_manifest["library_macos"] = dependency_library.name
        else:
            dependency_manifest["library_linux"] = dependency_library.name
        dependency_manifest[integrity_key()] = sha256_file(dependency_library)
        failed_dependent_library = build_fixture_library("dependent-init-failure")
        failed_dependent = dict(manifest)
        failed_dependent["dependencies"] = ["lang-python-dependency"]
        if sys.platform == "darwin":
            failed_dependent["library_macos"] = failed_dependent_library.name
        else:
            failed_dependent["library_linux"] = failed_dependent_library.name
        failed_dependent[integrity_key()] = sha256_file(failed_dependent_library)
        failed_dependent_bytes = (
            json.dumps(failed_dependent, indent=2, sort_keys=True) + "\n").encode()
        dependency_rollback_root = write_bundle("dependency-rollback", failed_dependent_bytes,
                                                True, failed_dependent_library)
        dependency_bytes = (
            json.dumps(dependency_manifest, indent=2, sort_keys=True) + "\n").encode()
        write_module(dependency_rollback_root, "lang-python-dependency", dependency_bytes,
                     True, dependency_library)
        expect_dependency_rollback(dependency_rollback_root, isolated_host)

    corrupt_manifest_root = write_bundle(
        "corrupt-manifest", b'{"language":"python","aliases":["py"], broken', False, library)
    expect_rejection("corrupt-manifest", corrupt_manifest_root, isolated_host)

    print("JUBE_LOADER_NEGATIVE: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
