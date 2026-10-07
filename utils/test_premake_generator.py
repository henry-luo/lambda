#!/usr/bin/env python3
"""Verify native test-host dependency expansion and link dependencies in the Premake generator."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
GENERATOR_PATH = ROOT / "utils" / "generate_premake.py"
TEST_MULTIARCH_TRIPLET = "x86_64-linux-gnu"


def fail(message: str) -> None:
    print(f"PREMAKE_GENERATOR_SELFTEST: {message}", file=sys.stderr)
    raise SystemExit(1)


def load_generator_module():
    spec = importlib.util.spec_from_file_location("premake_generator", GENERATOR_PATH)
    if spec is None or spec.loader is None:
        fail("could not load generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def linux_validation_generator(module):
    # Supply the Linux toolchain facts instead of probing the host gcc/pkg-config,
    # so the Linux config validates on any host; these checks don't depend on their values.
    return module.PremakeGenerator(str(ROOT / "build_lambda_config.json"), "linux",
                                   linux_multiarch_triplet=TEST_MULTIARCH_TRIPLET,
                                   linux_pkg_config_includes=[])


def generate_validation_host(module, platform_name: str) -> str:
    # Construct under Linux first so this pure generation test never refreshes
    # macOS archive artifacts, then select the requested output policy.
    generator = linux_validation_generator(module)
    generator.use_linux_config = platform_name == "linux"
    generator.use_macos_config = platform_name == "macos"
    generator.use_windows_config = False
    generator._generate_single_test(
        "test_runtime_full_trace_provider",
        "test/test_validator_integration.cpp",
        ["lambda-runtime-full"],
        "",
        "",
        ["gtest", "gtest_main"],
        target_name="test_runtime_full_trace_provider.exe",
    )
    return "\n".join(generator.premake_content)


def expect_node_core_provider(module, platform_name: str) -> None:
    generated = generate_validation_host(module, platform_name)
    if '"node-core",' not in generated:
        fail(f"{platform_name} runtime-full test omitted the node-core trace provider")


def lddeps_of(makefile: Path) -> set[str]:
    return {dep for line in re.findall(r"^\s*LDDEPS \+=(.*)$", makefile.read_text(), re.M)
            for dep in line.split()}


def expect_archive_link_deps(module) -> None:
    # Run the emitted hook through the installed premake: its gmake internals,
    # not this generator, decide whether a rebuilt archive relinks a binary.
    premake = os.environ.get("PREMAKE5_BIN") or shutil.which("premake5")
    if not premake:
        fail("premake5 not found")
    generator = linux_validation_generator(module)
    generator.generate_archive_link_deps()
    (ROOT / "temp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / "temp") as work:
        script = Path(work) / "premake5.lua"
        script.write_text("\n".join([
            *generator.premake_content,
            'workspace "probe"',
            '    configurations { "debug", "release" }',
            '    location "build"',
            '    language "C"',
            'project "app"',
            '    kind "ConsoleApp"',
            '    files { "app.c" }',
            '    linkoptions { "-Wl,-force_load,../lib/libgrammar.a", "../lib/libplain.a",',
            '                  "-Wl,--start-group,../lib/libgroup.a,--end-group", "-lm",',
            '                  "-Wl,--dynamic-list=../lib/host_exports.sym" }',
            'project "archive"',
            '    kind "StaticLib"',
            '    files { "archive.c" }',
            '    linkoptions { "../lib/libgrammar.a" }',
            '',
        ]))
        result = subprocess.run([premake, f"--file={script}", "gmake"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.returncode != 0:
            fail(f"premake5 gmake failed:\n{result.stdout}")
        app_deps = lddeps_of(Path(work) / "build" / "app.make")
        archive_deps = lddeps_of(Path(work) / "build" / "archive.make")
    if app_deps != {"../lib/libgrammar.a", "../lib/libplain.a", "../lib/libgroup.a",
                    "../lib/host_exports.sym"}:
        fail(f"executable LDDEPS omit its linkoptions archives: {sorted(app_deps)}")
    if archive_deps:
        fail(f"static library LDDEPS gained archives it never reads: {sorted(archive_deps)}")


def expect_module_only_libraries(module) -> None:
    generator = linux_validation_generator(module)
    generator.generate_main_program()
    generated = "\n".join(generator.premake_content)
    configured = {lib['name']: lib for lib in generator.config['libraries']
                  if isinstance(lib, dict) and lib.get('module_only')}
    for name, library in configured.items():
        if f'"{library["lib"]}"' in generated:
            fail(f"static host links module-only archive {name}")
    module_target = next(target for target in generator.config['targets']
                         if target['name'] == 'rdb-drivers')
    closure = generator._executable_external_dependencies(module_target)
    if not configured.keys() <= set(closure):
        fail("RDB module lost its explicit external dependencies")


def expect_host_export_list(module) -> None:
    # D7.3.6: a Jube-capable host exports only the allowlist, in its platform's
    # linker format; a host without the Jube loader exports nothing.
    listed = module.read_host_export_list()
    if len(listed) != len(set(listed)):
        fail("host export list repeats a symbol")
    generator = linux_validation_generator(module)
    generator.generate_main_program()
    generated = "\n".join(generator.premake_content)
    if "--export-dynamic" in generated or \
            '"-Wl,--dynamic-list=lambda_host_exports.linux.sym"' not in generated:
        fail("Linux host does not link its dynamic exports through the allowlist")
    path, text = generator.host_export_file
    if not path.endswith("lambda_host_exports.linux.sym") or \
            text.count(";\n") != len(listed) + 1:
        fail("Linux dynamic list does not carry every allowlisted symbol")
    generator.use_linux_config, generator.use_macos_config = False, True
    options = "\n".join(generator._host_export_link_options())
    if '"-Wl,-exported_symbols_list,lambda_host_exports.macos.sym"' not in options or \
            generator.host_export_file[1].splitlines() != ["_" + name for name in listed]:
        fail("macOS host does not link its exports through the allowlist")
    generator.config['defines'] = generator.config.get('defines', []) + ['LAMBDA_NO_JUBE']
    if generator._host_export_link_options() or generator.host_export_file:
        fail("a host without the Jube loader still exports the allowlist")


def main() -> int:
    module = load_generator_module()
    expect_node_core_provider(module, "macos")
    expect_node_core_provider(module, "linux")
    expect_archive_link_deps(module)
    expect_module_only_libraries(module)
    expect_host_export_list(module)
    print("PREMAKE_GENERATOR_SELFTEST: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
