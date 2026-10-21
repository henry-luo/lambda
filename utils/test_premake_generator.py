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


def linux_validation_generator(module, architecture=None):
    # Supply the Linux toolchain facts instead of probing the host gcc/pkg-config,
    # so the Linux config validates on any host; these checks don't depend on their values.
    return module.PremakeGenerator(str(ROOT / "build_lambda_config.json"), "linux",
                                   linux_multiarch_triplet=TEST_MULTIARCH_TRIPLET,
                                   linux_pkg_config_includes=[],
                                   target_architecture=architecture)


def generate_validation_host(module, platform_name: str, dependencies=None) -> str:
    # Construct under Linux first so this pure generation test never refreshes
    # macOS archive artifacts, then select the requested output policy.
    generator = linux_validation_generator(module)
    generator.use_linux_config = platform_name == "linux"
    generator.use_macos_config = platform_name == "macos"
    generator.use_windows_config = False
    generator._generate_single_test(
        "test_runtime_full_trace_provider",
        "test/test_validator_integration.cpp",
        dependencies if dependencies is not None else ["lambda-runtime-full"],
        "",
        "",
        ["gtest", "gtest_main"],
        target_name="test_runtime_full_trace_provider.exe",
    )
    return "\n".join(generator.premake_content)


def expect_static_runtime_providers(module, platform_name: str) -> None:
    option = '-exported_symbols_list,lambda_host_exports.macos.sym' if platform_name == 'macos' \
        else '--dynamic-list=lambda_host_exports.linux.sym'
    for dependencies in (["lambda-runtime-full"], ["lambda-rt", "radiant"]):
        generated = generate_validation_host(module, platform_name, dependencies)
        if '"node-core",' in generated:
            fail(f"{platform_name} static runtime host linked a replaceable Node module")
        for provider in ('lambda-rt-cpp', 'radiant-cpp'):
            if f'"{provider}",' not in generated:
                fail(f"{platform_name} runtime host omitted static provider {provider}")
        if option not in generated:
            fail(f"{platform_name} runtime test host omitted the D7.3.6 export allowlist")
    for dependencies in (["lambda-data"], ["lambda-rt"]):
        static_host = generate_validation_host(module, platform_name, dependencies)
        if 'lambda_host_exports.' in static_host:
            fail(f"{platform_name} minimal static test gained unrelated host exports")


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


def expect_profile_isolated_archives(module) -> None:
    premake = os.environ.get('PREMAKE5_BIN') or shutil.which('premake5')
    if not premake:
        fail('premake5 not found')
    generator = linux_validation_generator(module)
    for name in ('archive-readers', 'image-codecs'):
        library = generator.external_libraries[name]
        target = next(t for t in generator.config['targets'] if t['name'] == library['build_target'])
        with tempfile.TemporaryDirectory(dir=ROOT / 'temp') as directory:
            work = Path(directory)
            (work / 'mode.c').write_text('int archive_mode(void) { return PROFILE_VALUE; }\n')
            (work / 'app.c').write_text(
                'int archive_mode(void); int main(void) { return archive_mode() != PROFILE_VALUE; }\n')
            script = work / 'premake5.lua'
            script.write_text('\n'.join([
                'workspace "profile_switch"',
                '    configurations { "debug", "release" }',
                '    platforms { "native" }',
                '    location "build/premake"',
                '    language "C"',
                '    objdir "build/obj/%{prj.name}/%{cfg.platform}/%{cfg.buildcfg}"',
                '    filter "configurations:debug"',
                '        defines { "PROFILE_VALUE=11" }',
                '    filter "configurations:release"',
                '        defines { "PROFILE_VALUE=29" }',
                '    filter {}',
                'project "' + target['name'] + '"',
                '    kind "StaticLib"',
                '    targetdir "' + target.get('target_dir', 'build/lib') + '"',
                '    files { "mode.c" }',
                *generator._static_lib_rebuild_lines('StaticLib'),
                'project "app"',
                '    kind "ConsoleApp"',
                '    targetdir "bin/%{cfg.buildcfg}"',
                '    targetextension ".exe"',
                '    files { "app.c" }',
                '    dependson { "' + target['name'] + '" }',
                '    linkoptions { "../../' + library['lib'] + '" }',
                '',
            ]))
            commands = [[premake, '--file=' + str(script), 'gmake']]
            commands += [['make', '-C', str(work / 'build/premake'),
                          'config=' + profile + '_native', 'app', 'CC=gcc', 'CXX=g++']
                         for profile in ('debug', 'release', 'debug', 'release')]
            snapshots = {}
            for index, command in enumerate(commands):
                result = subprocess.run(command, cwd=work, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                if result.returncode:
                    fail(f'{name} profile-switch build failed:\n{result.stdout}')
                if not index:
                    continue
                profile = ('debug', 'release')[(index - 1) % 2]
                executable = work / 'bin' / profile / 'app.exe'
                if subprocess.run([str(executable)], cwd=work).returncode:
                    fail(f'{name} {profile} executable linked the other profile archive')
                archive = work / library['lib'].replace('%{cfg.platform}', 'native').replace('%{cfg.buildcfg}', profile)
                snapshots.setdefault(profile, (archive, archive.read_bytes()))
                # switching profiles must preserve the previously built archive's contents.
                for previous, (path, contents) in snapshots.items():
                    if path.read_bytes() != contents:
                        fail(f'{name} {profile} build overwrote the {previous} archive')


def expect_image_source_build(module) -> None:
    for architecture in ('arm64', 'x86_64'):
        generator = linux_validation_generator(module, architecture)
        codec = next(t for t in generator.config['targets'] if t['name'] == 'lambda-image-codecs')
        sources = codec['source_files']
        if any('/mux/' in f or '/demux/' in f or f.endswith('ratectrl_rtc.cc')
               for f in sources):
            fail('image source target includes unused WebP animation or standalone tool providers')
        # animated GIF output uses both the encoder and its compression hash table.
        if not {'lib/image/giflib/egif_lib.c', 'lib/image/giflib/gif_hash.c'} <= set(sources):
            fail(f'{architecture} image target omits required GIF encoding providers')
        if not all((ROOT / f).is_file() for f in sources):
            fail(f'{architecture} image target has missing sources')
        arm_files = any('/arm/' in f or 'aarch64_cpudetect.c' in f for f in sources)
        if arm_files != (architecture == 'arm64'):
            fail(f'{architecture} image target selects the wrong SIMD source family')
        generator._generate_complex_library(codec)
        emitted = '\n'.join(generator.premake_content)
        if architecture == 'arm64' and '-march=armv8.2-a+dotprod' not in emitted:
            fail('AOM intrinsic sources lost their per-file ISA flags')
        generator.premake_content = []
        generator.generate_main_program()
        emitted = '\n'.join(generator.premake_content)
        if not re.search(r'dependson \{[^}]*"lambda-image-codecs"', emitted):
            fail('host can link the image archive before its source target builds')
        if 'build_temp/image-codecs' in emitted:
            fail('host still consumes the retired CMake codec prefix')
        generator.premake_content = []
        generator._generate_single_test('test_image_provider', 'test/test_image_io_gtest.cpp',
                                        ['lambda-data'], '', '', ['gtest', 'gtest_main'])
        if not re.search(r'dependson \{[^}]*"lambda-image-codecs"', '\n'.join(generator.premake_content)):
            fail('image tests lost their transitive source-build dependency')
    generator = module.PremakeGenerator(str(ROOT / 'build_lambda_config.json'), 'windows',
                                        target_architecture='x86_64')
    codec = next(t for t in generator.config['targets'] if t['name'] == 'lambda-image-codecs')
    if not any(f.endswith('tif_win32.c') for f in codec['source_files']) or \
            any(f.endswith('tif_unix.c') for f in codec['source_files']):
        fail('Windows image target selects POSIX TIFF resource helpers')


def main() -> int:
    module = load_generator_module()
    generator = linux_validation_generator(module)
    closure = generator._executable_external_dependencies({'libraries': ['archive-readers']})
    if not {'archive-readers', 'archive-iconv', 'lzma', 'zstd', 'bzip2'}.issubset(closure):
        fail('source-built archive consumers lost their codec/charset link requirements')
    expect_image_source_build(module)
    expect_profile_isolated_archives(module)
    expect_static_runtime_providers(module, "macos")
    expect_static_runtime_providers(module, "linux")
    expect_archive_link_deps(module)
    expect_module_only_libraries(module)
    expect_host_export_list(module)
    print("PREMAKE_GENERATOR_SELFTEST: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
