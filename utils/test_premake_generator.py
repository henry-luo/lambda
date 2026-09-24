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


def generate_validation_host(module, platform_name: str) -> str:
    # Construct under Linux first so this pure generation test never refreshes
    # macOS archive artifacts, then select the requested output policy.
    generator = module.PremakeGenerator(str(ROOT / "build_lambda_config.json"), "linux")
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
    generator = module.PremakeGenerator(str(ROOT / "build_lambda_config.json"), "linux")
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
            '                  "-Wl,--start-group,../lib/libgroup.a,--end-group", "-lm" }',
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
    if app_deps != {"../lib/libgrammar.a", "../lib/libplain.a", "../lib/libgroup.a"}:
        fail(f"executable LDDEPS omit its linkoptions archives: {sorted(app_deps)}")
    if archive_deps:
        fail(f"static library LDDEPS gained archives it never reads: {sorted(archive_deps)}")


def main() -> int:
    module = load_generator_module()
    expect_node_core_provider(module, "macos")
    expect_node_core_provider(module, "linux")
    expect_archive_link_deps(module)
    print("PREMAKE_GENERATOR_SELFTEST: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
