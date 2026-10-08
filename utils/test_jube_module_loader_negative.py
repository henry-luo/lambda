#!/usr/bin/env python3
"""Exercise negative native-module loader cases against copied node-fs bundles.

Every case copies the subject module (node-fs, with its node-core dependency)
into an isolated bundle, breaks one loader contract (manifest, digest, ABI,
descriptor, requirements, initialization, dependency rollback), and expects
`require('fs')` to fail with MODULE_NOT_FOUND instead of loading it (D7.3.2).
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import hashlib
import argparse


ROOT = Path(__file__).resolve().parent.parent
SUBJECT_DIR = ROOT / "modules" / "node-fs"
SUBJECT_SPECIFIER = "fs"
TEST_ROOT = ROOT / "temp" / "jube-loader-negative"
HOST = Path(os.environ.get("LAMBDA_JUBE_HOST_EXE", ROOT / "lambda.exe")).resolve()
INIT_FAILURE_SOURCE = ROOT / "test" / "jube" / "jube_init_failure_module.cpp"
FIXTURE_NAME = "jube-test-subject"
DEPENDENCY_NAME = "jube-test-dependency"


def fail(message: str) -> None:
    print(f"JUBE_LOADER_NEGATIVE: {message}", file=sys.stderr)
    raise SystemExit(1)


def library_key() -> str:
    if sys.platform == "darwin":
        return "library_macos"
    if sys.platform.startswith("linux"):
        return "library_linux"
    if sys.platform == "win32":
        return "library_windows"
    fail(f"unsupported platform {sys.platform}")
    raise AssertionError("unreachable")


def library_path(manifest: dict, module_dir: Path) -> Path:
    name = manifest.get(library_key())
    if not isinstance(name, str) or not name:
        fail("module manifest has no library for this platform")
    path = module_dir / name
    if not path.is_file():
        fail(f"native module is missing: {path.relative_to(ROOT)}")
    return path


def integrity_key() -> str:
    return {"library_macos": "sha256_macos", "library_linux": "sha256_linux",
            "library_windows": "sha256_windows"}[library_key()]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def manifest_bytes_of(manifest: dict) -> bytes:
    return (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()


def build_fixture_library(fixture_name: str, module_name: str,
                          compiler_defines: list[str] | None = None) -> Path:
    if not INIT_FAILURE_SOURCE.is_file():
        fail(f"init-failure fixture source is missing: {INIT_FAILURE_SOURCE.relative_to(ROOT)}")
    suffix = ".dylib" if sys.platform == "darwin" else ".so"
    output = TEST_ROOT / f"jube-test-{fixture_name}{suffix}"
    compiler = os.environ.get("CXX", "clang++")
    # jube.h exposes the complete public Item representation, whose public
    # parser declarations include Tree-sitter's installed-style include root.
    command = [compiler, "-std=c++17", "-I", str(ROOT), "-I",
               str(ROOT / "lambda" / "tree-sitter" / "lib" / "include"),
               f'-DJUBE_TEST_MODULE_NAME="{module_name}"']
    command.extend(f"-D{define}" for define in compiler_defines or [])
    command.extend(["-dynamiclib"] if sys.platform == "darwin" else ["-shared", "-fPIC"])
    command.extend([str(INIT_FAILURE_SOURCE), "-o", str(output)])
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, check=False)
    if completed.returncode != 0 or not output.is_file():
        fail(f"could not build {fixture_name} fixture: {completed.stderr.strip()}")
    return output


def write_module(bundle_root: Path, directory_name: str, manifest_bytes: bytes | None,
                 copy_library: bool, library: Path) -> None:
    bundle = bundle_root / directory_name
    bundle.mkdir(parents=True, exist_ok=True)
    if manifest_bytes is not None:
        (bundle / "module.json").write_bytes(manifest_bytes)
    if copy_library:
        shutil.copy2(library, bundle / library.name)


def copy_runtime_module(bundle_root: Path, module_dir: Path, copy_library: bool) -> tuple[dict, Path]:
    try:
        manifest_bytes = (module_dir / "module.json").read_bytes()
        manifest = json.loads(manifest_bytes)
    except (OSError, json.JSONDecodeError) as error:
        fail(f"could not read runtime module manifest: {error}")
    library = library_path(manifest, module_dir)
    module_name = manifest.get("name")
    if not isinstance(module_name, str) or not module_name:
        fail("runtime module manifest has no name")
    write_module(bundle_root, module_name, manifest_bytes, copy_library, library)
    return manifest, library


def copy_dependencies(bundle_root: Path, dependencies: list,
                      module_root: Path = ROOT / "modules") -> None:
    for dependency_name in dependencies:
        if not isinstance(dependency_name, str) or not dependency_name:
            fail("runtime module dependency name is invalid")
        copy_runtime_module(bundle_root, module_root / dependency_name, True)


def run_require(bundle_root: Path, isolated_host: Path, specifier: str,
                extra_environment: dict | None = None,
                working_directory: Path = TEST_ROOT) -> subprocess.CompletedProcess:
    environment = dict(os.environ)
    environment["JUBE_MODULE_PATH"] = str(bundle_root)
    environment.update(extra_environment or {})
    return subprocess.run(
        [str(isolated_host), "js", "-e",
         f"try {{ require('{specifier}'); console.log('unexpected-success'); }} "
         "catch (error) { console.log(error.code); }"],
        # Run outside the repository root so the normal development bundle
        # cannot mask a rejection from this deliberately isolated bundle.
        cwd=working_directory,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def expect_rejection(case_name: str, bundle_root: Path, isolated_host: Path,
                     specifier: str = SUBJECT_SPECIFIER, module_name: str | None = None,
                     extra_environment: dict | None = None) -> None:
    completed = run_require(bundle_root, isolated_host, specifier, extra_environment)
    if completed.returncode != 0 or "unexpected-success" in completed.stdout:
        fail(f"{case_name} bundle was accepted")
    if "MODULE_NOT_FOUND" not in completed.stdout:
        fail(f"{case_name} rejection did not report MODULE_NOT_FOUND")
    if module_name and f"module '{module_name}'" not in completed.stdout + completed.stderr:
        fail(f"{case_name} rejection did not identify {module_name} in the host log")


def test_search_priority(module_dir: Path, specifier: str) -> None:
    # D7.3.4: catalog metadata selects the image before descriptor validation.
    # An incompatible lower-priority copy must never displace that selection.
    for case_name, locations, accepted in (
            ("configured-first", ("configured", "second"), True),
            ("configured-invalid-first", ("configured", "second"), False),
            ("configured-over-bundled", ("configured", "bundled"), True),
            ("bundled-over-working", ("bundled", "working"), True),
            ("conflicting-provider", ("configured", "second"), False)):
        case_root = TEST_ROOT / case_name
        host_root = case_root / "host"
        working_directory = case_root / "working"
        host_root.mkdir(parents=True)
        working_directory.mkdir(parents=True)
        isolated_host = host_root / "lambda.exe"
        shutil.copy2(HOST, isolated_host)
        roots = {"configured": case_root / "configured", "second": case_root / "second",
                 "bundled": host_root / "modules", "working": working_directory / "modules"}
        for index, location in enumerate(locations):
            manifest, _ = copy_runtime_module(roots[location], module_dir, True)
            copy_dependencies(roots[location], manifest.get("dependencies", []), module_dir.parent)
            if case_name == "conflicting-provider" and index == 1:
                original_name = manifest["name"]
                manifest["name"] = "jube-test-conflicting-provider"
                (roots[location] / original_name / "module.json").write_bytes(manifest_bytes_of(manifest))
            elif case_name != "conflicting-provider" and (index == 0) != accepted:
                manifest["base_abi_version"] += 1
                (roots[location] / manifest["name"] / "module.json").write_bytes(
                    manifest_bytes_of(manifest))
        configured = os.pathsep.join(str(roots[name]) for name in locations
                                     if name in ("configured", "second"))
        completed = run_require(roots[locations[0]], isolated_host, specifier,
                                {"JUBE_MODULE_PATH": configured}, working_directory)
        successful = "unexpected-success" in completed.stdout
        if completed.returncode != 0 or successful != accepted:
            fail(f"{case_name} selected the wrong module: {completed.stdout} {completed.stderr}")
        if not accepted and "MODULE_NOT_FOUND" not in completed.stdout:
            fail(f"{case_name} did not report the selected module's rejection")
        if case_name == "conflicting-provider" and "duplicate provider" not in completed.stderr:
            fail("conflicting providers were not rejected during catalog discovery")
    print("JUBE_LOADER_NEGATIVE: search priority passed")


def run_runtime_module_negative(module_dir: Path, specifier: str) -> int:
    if not HOST.is_file():
        fail("lambda.exe is missing; build the host first")
    if TEST_ROOT.exists():
        shutil.rmtree(TEST_ROOT)
    TEST_ROOT.mkdir(parents=True)
    isolated_host = TEST_ROOT / "lambda.exe"
    shutil.copy2(HOST, isolated_host)
    test_search_priority(module_dir, specifier)

    manifest, library = copy_runtime_module(TEST_ROOT / "missing-library", module_dir, False)
    module_name = manifest["name"]
    dependencies = manifest.get("dependencies", [])
    if not isinstance(dependencies, list):
        fail("runtime module dependencies must be an array")
    copy_dependencies(TEST_ROOT / "missing-library", dependencies, module_dir.parent)
    expect_rejection("missing-library", TEST_ROOT / "missing-library", isolated_host,
                     specifier, module_name)

    tampered_root = TEST_ROOT / "checksum-mismatch"
    copy_runtime_module(tampered_root, module_dir, True)
    copy_dependencies(tampered_root, dependencies, module_dir.parent)
    checksum_library = tampered_root / module_name / library.name
    with checksum_library.open("r+b") as file:
        first_byte = file.read(1)
        if not first_byte:
            fail("native runtime module is empty")
        file.seek(0)
        file.write(bytes([first_byte[0] ^ 0x01]))
    expect_rejection("checksum-mismatch", tampered_root, isolated_host, specifier, module_name)

    wrong_abi_root = TEST_ROOT / "wrong-base-abi"
    manifest, library = copy_runtime_module(wrong_abi_root, module_dir, True)
    copy_dependencies(wrong_abi_root, dependencies, module_dir.parent)
    wrong_abi = dict(manifest)
    wrong_abi["base_abi_version"] = int(manifest["base_abi_version"]) + 1
    (wrong_abi_root / module_name / "module.json").write_bytes(manifest_bytes_of(wrong_abi))
    expect_rejection("wrong-base-abi", wrong_abi_root, isolated_host, specifier, module_name)

    print(f"JUBE_LOADER_NEGATIVE: runtime module {module_name} passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime-module-dir", type=Path)
    parser.add_argument("--runtime-specifier")
    arguments = parser.parse_args()
    if arguments.runtime_module_dir or arguments.runtime_specifier:
        if not arguments.runtime_module_dir or not arguments.runtime_specifier:
            fail("runtime negative tests require both --runtime-module-dir and --runtime-specifier")
        return run_runtime_module_negative(arguments.runtime_module_dir.resolve(),
                                           arguments.runtime_specifier)
    if not HOST.is_file():
        fail("lambda.exe is missing; build the host first")
    try:
        manifest = json.loads((SUBJECT_DIR / "module.json").read_bytes())
    except (OSError, json.JSONDecodeError) as error:
        fail(f"could not read development manifest: {error}")
    subject_name = manifest["name"]
    dependencies = manifest.get("dependencies", [])
    library = library_path(manifest, SUBJECT_DIR)
    # Integrity is build-local; negative fixtures add it to exercise digest checks.
    manifest[integrity_key()] = sha256_file(library)

    if TEST_ROOT.exists():
        shutil.rmtree(TEST_ROOT)
    TEST_ROOT.mkdir(parents=True)
    isolated_host = TEST_ROOT / "lambda.exe"
    shutil.copy2(HOST, isolated_host)
    test_search_priority(SUBJECT_DIR, SUBJECT_SPECIFIER)

    def write_bundle(case_name: str, bundle_manifest: dict | bytes | None, copy_library: bool,
                     bundle_library: Path, directory_name: str = subject_name) -> Path:
        bundle_root = TEST_ROOT / case_name
        data = bundle_manifest if bundle_manifest is None or isinstance(bundle_manifest, bytes) \
            else manifest_bytes_of(bundle_manifest)
        write_module(bundle_root, directory_name, data, copy_library, bundle_library)
        copy_dependencies(bundle_root, dependencies)
        return bundle_root

    def variant(**changes) -> dict:
        changed = dict(manifest)
        changed.update(changes)
        return changed

    expect_rejection("missing-library", write_bundle("missing-library", manifest, False, library),
                     isolated_host)
    expect_rejection("wrong-base-abi", write_bundle(
        "wrong-base-abi", variant(base_abi_version=int(manifest["base_abi_version"]) + 1),
        True, library), isolated_host)
    expect_rejection("wrong-hosted-api", write_bundle(
        "wrong-hosted-api", variant(hosted_api_version=int(manifest["hosted_api_version"]) + 1),
        True, library), isolated_host)
    expect_rejection("unsafe-resource", write_bundle(
        "unsafe-resource", variant(resources=["../outside-the-module"]), True, library),
        isolated_host)
    checksum_root = write_bundle("checksum-mismatch", manifest, True, library)
    with (checksum_root / subject_name / library.name).open("r+b") as file:
        first_byte = file.read(1)
        if not first_byte:
            fail("native module is empty")
        file.seek(0)
        file.write(bytes([first_byte[0] ^ 0x01]))
    expect_rejection("checksum-mismatch", checksum_root, isolated_host)
    expect_rejection("wrong-entry-symbol", write_bundle(
        "wrong-entry-symbol", variant(entry_symbol="missing_jube_module_entry"), True, library),
        isolated_host)
    expect_rejection("missing-dependency", write_bundle(
        "missing-dependency", variant(dependencies=["missing-test-dependency"]), True, library),
        isolated_host)

    if sys.platform != "win32":
        # Fixture cases impersonate a synthetic module that provides only its own
        # name, so the catalog can attest it and activation reaches the descriptor.
        def fixture_bundle(case_name: str, defines: list[str], **changes) -> Path:
            fixture = build_fixture_library(case_name, FIXTURE_NAME, defines)
            fixture_manifest = variant(name=FIXTURE_NAME, provides=[FIXTURE_NAME], **changes)
            fixture_manifest[library_key()] = fixture.name
            fixture_manifest[integrity_key()] = sha256_file(fixture)
            return write_bundle(case_name, fixture_manifest, True, fixture, FIXTURE_NAME)

        def expect_fixture_rejection(case_name: str, bundle_root: Path, environment: dict) -> None:
            expect_rejection(case_name, bundle_root, isolated_host, FIXTURE_NAME,
                             extra_environment=environment)

        marker = TEST_ROOT / "failed-init-shutdown.marker"
        expect_fixture_rejection("failed-initialization", fixture_bundle("failed-initialization", []),
                                 {"JUBE_INIT_FAILURE_MARKER": str(marker)})
        if not marker.is_file() or marker.read_text() != "shutdown\n":
            fail("failed initializer did not receive shutdown rollback")

        # Each descriptor/requirements defect must be rejected before init runs.
        for case_name, define in (
                ("unsupported-descriptor-abi", "JUBE_TEST_UNSUPPORTED_ABI"),
                ("undersized-descriptor", "JUBE_TEST_UNDERSIZED_DESCRIPTOR"),
                ("unsupported-requirements", "JUBE_TEST_REQUIRES_UNSUPPORTED_REQUIREMENTS"),
                ("unsupported-node-version", "JUBE_TEST_REQUIRES_UNSUPPORTED_NODE_VERSION"),
                ("undersized-node-api", "JUBE_TEST_REQUIRES_UNDERSIZED_NODE_API"),
                ("undersized-value-api", "JUBE_TEST_REQUIRES_UNDERSIZED_VALUE_API")):
            init_marker = TEST_ROOT / f"{case_name}-init.marker"
            expect_fixture_rejection(case_name, fixture_bundle(case_name, [define]),
                                     {"JUBE_DESCRIPTOR_INIT_MARKER": str(init_marker)})
            if init_marker.exists():
                fail(f"{case_name} reached the module initializer")

        # A dependency activated for a dependent that then fails must be shut down.
        dependency = build_fixture_library("dependency-success", DEPENDENCY_NAME,
                                           ["JUBE_TEST_SUCCESS_INIT"])
        dependency_manifest = variant(name=DEPENDENCY_NAME, provides=[DEPENDENCY_NAME],
                                      dependencies=[])
        dependency_manifest[library_key()] = dependency.name
        dependency_manifest[integrity_key()] = sha256_file(dependency)
        rollback_root = fixture_bundle("dependent-init-failure", [],
                                       dependencies=[DEPENDENCY_NAME])
        write_module(rollback_root, DEPENDENCY_NAME, manifest_bytes_of(dependency_manifest),
                     True, dependency)
        init_marker = TEST_ROOT / "dependency-init.marker"
        shutdown_marker = TEST_ROOT / "dependency-shutdown.marker"
        expect_fixture_rejection("dependency-rollback", rollback_root, {
            "JUBE_DEPENDENCY_INIT_MARKER": str(init_marker),
            "JUBE_DEPENDENCY_SHUTDOWN_MARKER": str(shutdown_marker)})
        if not init_marker.is_file() or init_marker.read_text() != "init\n":
            fail("dependency did not initialize before its dependent failed")
        if not shutdown_marker.is_file() or shutdown_marker.read_text() != "shutdown\n":
            fail("dependency was not shut down during dependent rollback")

    expect_rejection("corrupt-manifest", write_bundle(
        "corrupt-manifest", b'{"name":"node-fs","provides":["fs"], broken', False, library),
        isolated_host)

    print("JUBE_LOADER_NEGATIVE: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
