#!/usr/bin/env python3
"""Rebuild the input archives and a native test in an isolated macOS tree."""

import argparse
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

from check_input_archive_headers import (ARCHIVES, FORBIDDEN, ROOT, check_archive,
                                         check_archive_ownership, input_dependencies)
from check_radiant_archive_ownership import makefile_objects

TEST_TARGETS = ("test_input_dom_native_gtest", "test_input_blob_url_gtest", "test_input_model_gtest", "test_input_roundtrip_gtest",
                "test_markup_modular_gtest", "test_format_markup_gtest", "test_input_math_gtest", "test_dir_gtest",
                "test_wpt_html_parser_gtest", "test_markdown_gtest", "test_yaml_suite_gtest")
RADIANT_TEST = "test_radiant_input_value_gtest"
RADIANT_SVG_TEST = "test_radiant_svg_hit_gtest"
RADIANT_LAYOUT_TEST = "test_layout_custom_gtest"
RADIANT_SOURCE_PATH_TEST = "test_radiant_source_path_gtest"
RADIANT_STATE_STORE_TEST = "test_state_store_gtest"
RADIANT_FILES = {Path("radiant/input_value.cpp"), Path("radiant/input_value.hpp"),
                 Path("radiant/input_state.cpp"), Path("radiant/select_options.cpp"),
                 Path("radiant/select_options.hpp"), Path("radiant/form_controls.cpp"),
                 Path("radiant/form_controls.hpp"), Path("radiant/loader_runtime_host.cpp"),
                 Path("radiant/loader_runtime_host.hpp"), Path("radiant/reactive_render_host.cpp"),
                 Path("radiant/reactive_render_host.hpp"), Path("radiant/stylesheet_sync.cpp"),
                 Path("radiant/stylesheet_sync.hpp")}
RADIANT_SOURCE_PATH_FILES = {Path("radiant/source_path.cpp"), Path("radiant/source_path.hpp"),
                             Path("radiant/source_path_value.cpp"), Path("radiant/source_path_value.hpp"),
                             Path("radiant/source_position_host.cpp"), Path("radiant/source_position_host.hpp")}
FIXTURES = ("test/demo/scene3d/assets/loading", "test/scene3d/assets", "test/html/wpt",
            "test/markup/md", "test/yaml")
BASELINE_MODE_TARGETS = {"test_wpt_html_parser_gtest", "test_markdown_gtest"}


def stage_file(stage: Path, relative: Path) -> None:
    source = ROOT / relative
    if not source.is_file():
        raise FileNotFoundError(source)
    destination = stage / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def run_logged(command: list[str], cwd: Path, log: Path, environment: dict[str, str]) -> None:
    with log.open("w") as output:
        result = subprocess.run(command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT,
                                env=environment, check=False)
    if result.returncode:
        raise RuntimeError(f"{' '.join(command)} exited {result.returncode}; see {log}")


def target_dependency_files(makefile: Path, objects: Path) -> list[Path]:
    # Removed sources leave .d files behind; only current target members belong in the audit.
    depfiles = [objects / (Path(name).stem + ".d") for name in sorted(makefile_objects(makefile))]
    missing = [depfile for depfile in depfiles if not depfile.is_file()]
    if missing:
        raise RuntimeError(f"missing compiler dependency files for {makefile.stem}: {missing}")
    return depfiles


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--radiant-input-value", action="store_true",
                        help="also build the native Radiant input-value test")
    parser.add_argument("--radiant-svg-hit", action="store_true",
                        help="also build the native Radiant SVG hit test")
    parser.add_argument("--radiant-package-layout", action="store_true",
                        help="also build the native custom-layout host test")
    parser.add_argument("--radiant-source-path", action="store_true",
                        help="also build the native source-path lifetime test")
    parser.add_argument("--radiant-state-store", action="store_true",
                        help="also build the native StateStore tests")
    args = parser.parse_args()
    test_targets = TEST_TARGETS + ((RADIANT_TEST,) if args.radiant_input_value else ())
    test_targets += ((RADIANT_SVG_TEST,) if args.radiant_svg_hit else ())
    test_targets += ((RADIANT_LAYOUT_TEST,) if args.radiant_package_layout else ())
    test_targets += ((RADIANT_SOURCE_PATH_TEST,) if args.radiant_source_path else ())
    test_targets += ((RADIANT_STATE_STORE_TEST,) if args.radiant_state_store else ())
    targets = ("lambda-lib-cpp", "lambda-data-cpp")
    targets += (("radiant-properties",) if args.radiant_package_layout or args.radiant_state_store else ())
    targets += test_targets
    if platform.system() != "Darwin":
        print("INPUT_ISOLATION: this debug build staging check currently supports macOS only", file=sys.stderr)
        return 1
    if not all([check_archive(archive, objects) for archive, objects in ARCHIVES]) or not check_archive_ownership():
        return 1
    temp = ROOT / "temp"
    temp.mkdir(exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="input-isolation-", dir=temp))
    try:
        dependencies = set()
        for target in targets:
            objects = ROOT / "build/obj" / target / "native/debug"
            makefile = ROOT / "build/premake" / (target + ".make")
            depfiles = target_dependency_files(makefile, objects)
            target_dependencies = set()
            for depfile in depfiles:
                target_dependencies.update(input_dependencies(depfile))
            violations = sorted(str(dep) for dep in target_dependencies if any(
                dep.parts[:len(prefix)] == prefix for prefix in FORBIDDEN) and not
                (target == RADIANT_TEST and dep in RADIANT_FILES) and not
                (target == RADIANT_SOURCE_PATH_TEST and dep in RADIANT_SOURCE_PATH_FILES) and not
                (target in (RADIANT_SVG_TEST, RADIANT_LAYOUT_TEST, RADIANT_STATE_STORE_TEST,
                            "radiant-properties") and dep.parts[0] == "radiant"))
            if violations:
                raise RuntimeError(f"{target} imports forbidden headers: {', '.join(violations)}")
            dependencies.update(target_dependencies)
        for relative in sorted(dependencies):
            stage_file(stage, relative)
        for target in targets:
            stage_file(stage, Path("build/premake") / (target + ".make"))
        for fixture in FIXTURES:
            destination = stage / fixture
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(ROOT / fixture, destination)

        # The experiment rebuilds first-party archives; vendor archives are
        # copied from the selected build and remain an explicit packaging dependency.
        archives = set()
        for target in test_targets:
            makefile = (ROOT / "build/premake" / (target + ".make")).read_text()
            debug = makefile.split("ifeq ($(config),debug_native)", 1)[1].split("else ifeq", 1)[0]
            archives.update(re.findall(r"\.\./\.\./[^\s]+\.a", debug))
        if args.radiant_package_layout or args.radiant_state_store:
            # The native property archive is rebuilt from its staged sources.
            archives = {archive for archive in archives if
                Path(os.path.normpath(Path("build/premake") / archive)) !=
                Path("build/lib/libradiant-properties.a")}
        for archive in sorted(archives):
            relative = Path(os.path.normpath(Path("build/premake") / archive))
            stage_file(stage, relative)
        environment = os.environ.copy()
        for name in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
            environment.pop(name, None)
        build = stage / "build/premake"
        workers = str(min(8, os.cpu_count() or 1))
        print(f"INPUT_ISOLATION: stage {stage}; {len(dependencies)} source/header files, "
              f"{len(archives)} prebuilt vendor archives", flush=True)
        for target in targets:
            run_logged(["make", "-f", f"{target}.make", "config=debug_native", f"-j{workers}"],
                       build, stage / f"{target}.log", environment)
            print(f"INPUT_ISOLATION: rebuilt {target}", flush=True)
        # Pull every first-party member into one client to expose unused-provider
        # and duplicate-symbol dependencies that an ordinary static link omits.
        whole_target = "test_input_blob_url_whole_gtest"
        link_command = ('LINKCMD=$(CXX) -o "$@" $(OBJECTS) $(RESOURCES) '
                        '-Wl,-force_load,../../build/lib/liblambda-data-cpp.a '
                        '-Wl,-force_load,../../build/lib/native/debug/liblambda-lib-cpp.a '
                        '$(ALL_LDFLAGS) $(LIBS)')
        run_logged(["make", "-f", "test_input_blob_url_gtest.make", "config=debug_native",
                    f"TARGET=../../test/{whole_target}.exe", link_command],
                   build, stage / f"{whole_target}.log", environment)
        run_logged([f"./test/{whole_target}.exe"], stage,
                   stage / f"{whole_target}-run.log", environment)
        print("INPUT_ISOLATION: whole-archive client linked and passed", flush=True)
        for target in test_targets:
            log = stage / f"{target}-run.log"
            arguments = ["--baseline"] if target in BASELINE_MODE_TARGETS else []
            run_logged([f"./test/{target}.exe", *arguments], stage, log, environment)
            print(f"INPUT_ISOLATION: {target} passed; log {log}", flush=True)
        return 0
    except (OSError, ValueError, IndexError, RuntimeError) as exc:
        print(f"INPUT_ISOLATION: {exc}; stage retained at {stage}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
