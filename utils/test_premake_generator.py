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
        for provider in ('lambda-rt-cpp', 'radiant'):
            expected = f'lib{provider}.a'
            if expected not in generated:
                fail(f"{platform_name} runtime host omitted static provider {provider}")
        if option not in generated:
            fail(f"{platform_name} runtime test host omitted the D9.4.6 export allowlist")
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
    # D9.4.6: a Jube-capable host exports only the allowlist, in its platform's
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
    providers = []
    for name in ('archive-readers', 'image-codecs', 're2'):
        library = generator.external_libraries[name]
        target = next(t for t in generator.config['targets'] if t['name'] == library['build_target'])
        if '%{cfg.buildcfg}' not in library['lib']:
            fail(f'{name} archive has no profile isolation')
        # Build the synthetic provider only in the test directory.
        target = dict(target, target_dir='build/lib/native/%{cfg.buildcfg}')
        providers.append((name, target, f"build/lib/native/%{{cfg.buildcfg}}/lib{target['name']}.a"))
    utility = next(t for t in generator.config['targets'] if t['name'] == 'lambda-lib')
    utility_archive = generator._internal_static_archive('lambda-lib-cpp')
    emitted = generate_validation_host(module, 'linux', ['lambda-lib'])
    if utility_archive not in emitted:
        fail('native host linker groups ignore the configured utility archive directory')
    generator._generate_complex_library(utility)
    emitted = '\n'.join(generator.premake_content)
    if emitted.count('targetdir "' + utility['target_dir'] + '"') != 2:
        fail('the utility implementation and wrapper do not share profile isolation')
    providers.append(('lambda-lib', dict(utility, name='lambda-lib-cpp',
                      target_dir='build/lib/native/%{cfg.buildcfg}'),
                      'build/lib/native/%{cfg.buildcfg}/liblambda-lib-cpp.a'))
    for name, target, archive_path in providers:
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
                '    linkoptions { "../../' + archive_path + '" }',
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
                archive = work / archive_path.replace('%{cfg.platform}', 'native').replace('%{cfg.buildcfg}', profile)
                snapshots.setdefault(profile, (archive, archive.read_bytes()))
                # switching profiles must preserve the previously built archive's contents.
                for previous, (path, contents) in snapshots.items():
                    if path.read_bytes() != contents:
                        fail(f'{name} {profile} build overwrote the {previous} archive')


def expect_re2_source_build(module) -> None:
    for platform_name in ('linux', 'macos', 'windows'):
        generator = (module.PremakeGenerator(str(ROOT / 'build_lambda_config.json'), 'windows')
                     if platform_name == 'windows' else linux_validation_generator(module))
        generator.use_linux_config = platform_name == 'linux'
        generator.use_macos_config = platform_name == 'macos'
        library = generator.external_libraries['re2']
        target = next(t for t in generator.config['targets'] if t['name'] == library['build_target'])
        sources = target['source_files']
        input_root = module.sibling_roots()[0]
        vendored_sources = {Path(os.path.relpath(path, ROOT)).as_posix()
                            for path in (input_root / 'lib/re2').rglob('*.cc')}
        if set(sources) != vendored_sources or len(sources) != len(set(sources)):
            fail(f'{platform_name} RE2 target omits or duplicates vendored library sources')
        generator._generate_complex_library(target)
        emitted = '\n'.join(generator.premake_content)
        if 'language "C++"' not in emitted or '"-std=c++17"' not in emitted or \
                '"-fPIC"' not in emitted:
            fail(f'{platform_name} RE2 .cc sources lost their C++17/PIC compilation policy')
        if platform_name == 'windows' and '"NOMINMAX"' not in emitted:
            fail('Windows RE2 source build lost its Windows-header compatibility defines')
        # Check direct test links as well as dependencies reached through Lambda archives.
        for dependencies, libraries in (([], ['re2']), (['lambda-data'], [])):
            generator.premake_content = []
            generator._generate_single_test('test_re2_provider', 'lib/test_grep_gtest.cpp',
                                            dependencies, '', '', ['gtest', 'gtest_main', *libraries])
            emitted = '\n'.join(generator.premake_content)
            if 'libre2.a' not in emitted or 'build/lib/native/%{cfg.buildcfg}' not in emitted:
                fail(f'{platform_name} test lost its input-owned profile RE2 archive')
            if re.search(r'dependson \{[^}]*"re2"', emitted):
                fail(f'{platform_name} test rebuilds RE2 from sibling sources')
        generator.premake_content = []
        generator.generate_main_program()
        emitted = '\n'.join(generator.premake_content)
        if library['lib'] not in emitted or re.search(r'dependson \{[^}]*"re2"', emitted):
            fail(f'{platform_name} host lost the input-owned RE2 archive dependency')


def expect_image_source_build(module) -> None:
    for architecture in ('arm64', 'x86_64'):
        generator = linux_validation_generator(module, architecture)
        codec = next(t for t in generator.config['targets'] if t['name'] == 'lambda-image-codecs')
        sources = codec['source_files']
        if any('/mux/' in f or '/demux/' in f or f.endswith('ratectrl_rtc.cc')
               for f in sources):
            fail('image source target includes unused WebP animation or standalone tool providers')
        # animated GIF output uses both the encoder and its compression hash table.
        gif_sources = {Path(os.path.relpath(module.sibling_roots()[0] / path, ROOT)).as_posix()
                       for path in ('lib/image/giflib/egif_lib.c', 'lib/image/giflib/gif_hash.c')}
        if not gif_sources <= set(sources):
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
        if 'liblambda-image-codecs.a' not in emitted or re.search(r'dependson \{[^}]*"lambda-image-codecs"', emitted):
            fail('host lost the input-owned image archive dependency')
        if 'build_temp/image-codecs' in emitted:
            fail('host still consumes the retired CMake codec prefix')
        generator.premake_content = []
        generator._generate_single_test('test_image_provider', 'test/test_image_io_gtest.cpp',
                                        ['lambda-data'], '', '', ['gtest', 'gtest_main'])
        emitted = '\n'.join(generator.premake_content)
        if 'liblambda-image-codecs.a' not in emitted or re.search(r'dependson \{[^}]*"lambda-image-codecs"', emitted):
            fail('image tests lost their input-owned archive dependency')
    generator = module.PremakeGenerator(str(ROOT / 'build_lambda_config.json'), 'windows',
                                        target_architecture='x86_64')
    codec = next(t for t in generator.config['targets'] if t['name'] == 'lambda-image-codecs')
    if not any(f.endswith('tif_win32.c') for f in codec['source_files']) or \
            any(f.endswith('tif_unix.c') for f in codec['source_files']):
        fail('Windows image target selects POSIX TIFF resource helpers')


def expect_macos_ime_profile(module) -> None:
    generator = module.PremakeGenerator(str(ROOT / 'build_lambda_config.json'), 'linux',
                                        variant='macos-ime',
                                        linux_multiarch_triplet=TEST_MULTIARCH_TRIPLET,
                                        linux_pkg_config_includes=[])
    files = set(generator.config['source_files'])
    if 'lambda/host/macos_ime/main.mm' not in files or \
            'lambda/core/js_well_known_names.c' in files or \
            'lambda' in generator.config['source_dirs']:
        fail('macos-ime source inheritance leaked the CLI or JS entry point')
    if generator.variant_config.get('target_extension') != '' or \
            'AppKit' in set(generator.variant_config['exclude_macos_libraries']) - \
            set(generator.variant_config['restore_macos_libraries']):
        fail('macos-ime lost extensionless output or AppKit restore')
    generator.use_linux_config = False
    generator.use_macos_config = True
    generator.premake_content = []
    generator.generate_main_program()
    emitted = '\n'.join(generator.premake_content)
    if 'targetextension ""' not in emitted or \
            '"lambda/host/macos_ime/main.mm"' not in emitted or \
            '"lambda/runtime/document_host.cpp"' not in emitted or \
            '"lambda/core/js_well_known_names.c"' in emitted or \
            '"lambda/main.cpp"' in emitted or \
            '"-framework WebKit"' in emitted:
        fail('macos-ime generated host contains CLI/JS/WebKit inputs')


def expect_safety_early_profiles(module) -> None:
    for variant, name, entry in [('safety-runtime', 'LambdaSafetyRuntime', 'worker_module.cpp'),
                                  ('update-runtime', 'LambdaUpdateRuntime', 'update_verifier_worker.cpp')]:
        generator = module.PremakeGenerator(str(ROOT / 'build_lambda_config.json'), 'mac', variant=variant)
        generator.premake_content = []
        generator.generate_main_program()
        emitted = '\n'.join(generator.premake_content)
        if 'kind "SharedLib"' not in emitted or f'targetname "{name}"' not in emitted or \
                'targetextension ".dylib"' not in emitted or 'targetprefix ""' not in emitted or \
                f'"lambda/host/safety/{entry}"' not in emitted or \
                '"lambda/host/safety/worker_main.cpp"' in emitted:
            fail(f'{variant} lost its module entry or inherited an executable main')
        if variant == 'update-runtime':
            for excluded in ['worker.cpp', 'worker_module.cpp', 'update_controls.cpp', 'update_metadata.cpp',
                             'update_refresh.cpp', 'update_stage.cpp', 'update_process.cpp', 'storage_tree.cpp',
                             'grant_store.cpp', 'host_main.cpp']:
                if f'"lambda/host/safety/{excluded}"' in emitted:
                    fail(f'offline verifier inherited host/app authority: {excluded}')
    target = next(t for t in generator.config['targets'] if t['name'] == 'lambda-safety-bootstrap')
    generator.premake_content = []
    generator._generate_complex_library(target)
    emitted = '\n'.join(generator.premake_content)
    if '-framework ' in emitted or '"c++"' in emitted or target['libraries']:
        fail('minimal safety bootstrap acquired framework or C++ startup dependencies')


def expect_cli_sibling_roots(module) -> None:
    generator = module.PremakeGenerator(str(ROOT / 'build_lambda_config.json'), 'mac', variant='cli')
    includes = {(ROOT / path).resolve() for path in generator.config['includes']}
    if not set(module.sibling_roots()).issubset(includes):
        fail('CLI include overlay lost a sibling project root')


def expect_owned_test_source_resolution(module) -> None:
    input_root, radiant_root = module.sibling_roots()
    cases = [
        ('lib/test_atomic_gtest.cpp', input_root / 'test/lib/test_atomic_gtest.cpp'),
        ('test_input_sourcepos_gtest.cpp', input_root / 'test/test_input_sourcepos_gtest.cpp'),
        ('test/css/test_css_parser_gtest.cpp', input_root / 'test/css/test_css_parser_gtest.cpp'),
        ('test_animation_gtest.cpp', radiant_root / 'test/test_animation_gtest.cpp'),
        ('test/css/test_css_dom_crud.cpp', radiant_root / 'test/css/test_css_dom_crud.cpp'),
    ]
    for source, expected in cases:
        actual = module.resolve_split_paths({'source': source})['source']
        if (ROOT / actual).resolve() != expected.resolve():
            fail(f'test source {source} did not resolve to its owner: {actual}')


def expect_static_module_archive_profiles() -> None:
    spec = importlib.util.spec_from_file_location(
        "static_module_patcher", ROOT / "utils" / "patch_static_module_makefile.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    template = "build/lib/%{cfg.platform}/%{cfg.buildcfg}/libre2.a"
    expression = module.configured_archive_path(template, str(ROOT))
    if "%{cfg." in expression:
        fail("static-module patcher left a Premake token in its Make expression")
    with tempfile.TemporaryDirectory(dir=ROOT / "temp") as directory:
        makefile = Path(directory) / "Makefile"
        for profile in ("debug", "debug_asan", "release", "release_profile", "coverage"):
            makefile.write_text(
                f"OBJDIR = ../obj/lambda-exe/native/{profile}\n"
                f"ARCHIVE = {expression}\n"
                "all:\n\t@echo $(ARCHIVE)\n")
            actual = subprocess.check_output(["make", "-s", "-f", str(makefile)],
                                             text=True).strip()
            expected = str(ROOT / "build" / "lib" / "native" / profile / "libre2.a")
            if actual != expected:
                fail(f"static-module archive profile {profile}: {actual} != {expected}")

        # Both linker inputs and prerequisites must select each owner's profile.
        sandbox = Path(directory)
        root = sandbox / 'Lambda'
        profiles = ('debug', 'debug_asan', 'release', 'release_profile', 'coverage')
        common = '../lib/liblambda-data-cpp.a ../lib/libradiant-cpp.a'
        source = ''.join(f'ifeq ($(config),{profile}_native)\n'
                         f'LIBS += {common} ../lib/native/{profile}/liblambda-lib-cpp.a\n'
                         f'LDDEPS += {common} ../lib/native/{profile}/liblambda-lib-cpp.a\nendif\n'
                         for profile in profiles)
        previous = {key: os.environ.pop(key, None) for key in ('LAMBDA_INPUT_ROOT', 'RADIANT_ROOT')}
        try:
            result = module.use_sibling_archives(source, str(root))
        finally:
            for key, value in previous.items():
                if value is not None:
                    os.environ[key] = value
        if any(token in result for token in common.split()):
            fail('split link retained generated sibling source archives')
        for profile in profiles:
            if result.count(f'lambda-input/build/lib/{profile}/liblambda-lib.a') != 2:
                fail(f'split link lost {profile} input archive')
        if result.count('$(notdir $(OBJDIR))/liblambda-input.a') != len(profiles) * 2:
            fail('split link lost profile-specific input archive')


def expect_radiant_property_ownership(module) -> None:
    generator = linux_validation_generator(module)
    targets = {target['name']: target for target in generator.config['targets']}
    checker_path = ROOT.parent / 'radiant/scripts/check_input_facade.py'
    spec = importlib.util.spec_from_file_location('radiant_input_facade', checker_path)
    if spec is None or spec.loader is None:
        fail('could not load the Radiant input facade check')
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)
    violations = checker.check_facade(ROOT.parent / 'radiant')
    if violations:
        fail('Radiant input facade: ' + '; '.join(violations))
    (ROOT / 'temp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / 'temp') as work:
        probe = Path(work)
        facade = probe / 'lib'
        facade.mkdir()
        (facade / 'allowed.hpp').write_text('#pragma once\n')
        header = probe / 'input_header.hpp'
        header.write_text('#pragma once\n')
        try:
            (facade / 'linked.hpp').symlink_to(header)
        except OSError:
            pass  # creating file symlinks can require privileges on Windows
        else:
            hidden_source = probe / 'input_source.cpp'
            hidden_source.write_text('')
            disguised = facade / 'disguised.hpp'
            disguised.symlink_to(hidden_source)
            if not checker.check_facade(probe):
                fail('Radiant input facade accepted a header link to a C++ source')
            disguised.unlink()
        if checker.check_facade(probe):
            fail('Radiant input facade rejected a header')
        source = facade / 'forbidden.cpp'
        source.write_text('')
        if not checker.check_facade(probe):
            fail('Radiant input facade accepted a C++ source')
        source.unlink()
        try:
            (facade / 'directory_link').symlink_to(probe, target_is_directory=True)
        except OSError:
            pass  # creating directory symlinks can require privileges on Windows
        else:
            if not checker.check_facade(probe):
                fail('Radiant input facade accepted a directory link')
            (facade / 'directory_link').unlink()
        native = probe / 'radiant'
        native.mkdir()
        (native / 'probe.cpp').write_text('#include "lib/implementation.cpp"\n')
        if not checker.check_facade(probe):
            fail('Radiant input facade accepted an input C++ source include')
    if 'lambda-rt' in targets['radiant'].get('libraries', []):
        fail('Radiant must not depend on the Lambda runtime archive')
    for name in ('event.cpp', 'editing_controller.cpp', 'editing_intent.cpp'):
        if not (ROOT / 'lambda/module/radiant' / name).is_file() or \
                (ROOT.parent / 'radiant/radiant' / name).exists():
            fail(f'event controller source has the wrong owner: {name}')
    for name in ('dom', 'js', 'module', 'runtime', 'mir', 'lambda-data.hpp'):
        path = ROOT.parent / 'radiant/lambda' / name
        if path.exists() or path.is_symlink():
            fail(f'Radiant has a reverse Lambda source link: {name}')
    providers = {'radiant/view_prop_defaults.cpp', 'radiant/view_prop_ensure.cpp'}
    native_sources = subprocess.check_output(
        [sys.executable, str(ROOT.parent / 'lambda-input/scripts/build_sources.py'),
         'radiant', str(ROOT.parent / 'radiant')], text=True).split()
    if providers.intersection(targets['lambda-data']['source_files']):
        fail('input archive owns a Radiant property provider')
    if 'radiant-properties' in targets or any(native_sources.count(path) != 1 for path in providers) or \
            'radiant' not in targets['lambda-exe']['libraries']:
        fail('Radiant must contain each property provider exactly once')
    adapter = targets['lambda-radiant-adapter']
    radiant_prefix = Path(os.path.relpath(module.sibling_roots()[1] / 'radiant', ROOT)).as_posix() + '/'
    if any(pattern.startswith('lambda/module/radiant/') for pattern in
           targets['radiant'].get('source_patterns', [])) or \
            'lambda/module/radiant/*.cpp' not in adapter.get('source_patterns', []) or \
            'lambda-radiant-adapter' not in targets['lambda-exe']['libraries']:
        fail('Lambda Radiant adapters are not owned by their own integration archive')
    for group in generator.config['test']['test_suites']:
        for test in group.get('tests', []):
            dependencies = test.get('dependencies', [])
            native_sources = test.get('additional_sources', [])
            native_independent = test.get('radiant_native_sources_independent', False)
            has_native_sources = any(path.startswith(radiant_prefix) for path in native_sources)
            if 'radiant' in dependencies and 'lambda-radiant-adapter' not in dependencies and not has_native_sources:
                fail(f"{test.get('binary', test.get('name'))} omits the Lambda Radiant adapter")
            if native_independent and ('radiant' in dependencies or not has_native_sources):
                fail(f"{test.get('binary', test.get('name'))} has an invalid independent-native declaration")
            if not native_independent and has_native_sources and 'radiant' not in dependencies:
                fail(f"{test.get('binary', test.get('name'))} omits the Radiant archive")


def expect_radiant_media_platform_selection(module) -> None:
    wrappers = {'radiant/rdt_video_mac_static.cpp', 'radiant/rdt_speech_mac_static.cpp'}
    stubs = {'radiant/rdt_video_stub.cpp', 'radiant/rdt_speech_stub.cpp'}
    input_script = ROOT / '../lambda-input/scripts/build_sources.py'
    radiant_root = ROOT / '../radiant'
    for system, output in [('macos', 'mac'), ('linux', 'linux'), ('windows', 'windows')]:
        kwargs = {'target_architecture': 'arm64' if system == 'macos' else 'x86_64'}
        if system == 'linux':
            kwargs.update(linux_multiarch_triplet=TEST_MULTIARCH_TRIPLET, linux_pkg_config_includes=[])
        generator = module.PremakeGenerator(str(ROOT / 'build_lambda_config.json'), output, **kwargs)
        target = next(t for t in generator.config['targets'] if t['name'] == 'radiant')
        excluded = set(target.get('exclude_patterns', []))
        objcxx = set(target.get('objcxx_source_files', []))
        sources = set(subprocess.check_output(
            [sys.executable, str(input_script), 'radiant', str(radiant_root), '--system', system,
             '--architecture', kwargs['target_architecture']],
            text=True).split())
        mapped_wrappers = set(module.resolve_split_paths({'source_files': list(wrappers)})['source_files'])
        mapped_stubs = set(module.resolve_split_paths({'source_files': list(stubs)})['source_files'])
        if system == 'macos':
            if not mapped_stubs.issubset(excluded) or not mapped_wrappers.issubset(objcxx) or \
                    not wrappers.issubset(sources) or stubs.intersection(sources):
                fail('macOS Radiant must select both AVFoundation providers')
            generator.premake_content = []
            generator.generate_main_program()
            main_sources = '\n'.join(generator.premake_content)
            if any(f'"{source}",' in main_sources for source in mapped_wrappers):
                fail('the regular Lambda target also compiled a static media wrapper')
        elif not mapped_wrappers.issubset(excluded) or not stubs.issubset(sources) or \
                wrappers.intersection(sources) or objcxx:
            fail(f'{system} Radiant must select the native media stubs without Objective-C++')


def main() -> int:
    module = load_generator_module()
    generator = linux_validation_generator(module)
    closure = generator._executable_external_dependencies({'libraries': ['archive-readers']})
    if not {'archive-readers', 'archive-iconv', 'lzma', 'zstd', 'bzip2'}.issubset(closure):
        fail('source-built archive consumers lost their codec/charset link requirements')
    expect_re2_source_build(module)
    expect_image_source_build(module)
    expect_macos_ime_profile(module)
    expect_safety_early_profiles(module)
    expect_cli_sibling_roots(module)
    expect_owned_test_source_resolution(module)
    expect_profile_isolated_archives(module)
    expect_static_runtime_providers(module, "macos")
    expect_static_runtime_providers(module, "linux")
    expect_archive_link_deps(module)
    expect_static_module_archive_profiles()
    expect_radiant_property_ownership(module)
    expect_radiant_media_platform_selection(module)
    expect_module_only_libraries(module)
    expect_host_export_list(module)
    print("PREMAKE_GENERATOR_SELFTEST: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
