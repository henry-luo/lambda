#!/usr/bin/env python3
"""Check transitive includes of the extracted input and Radiant header surfaces."""

import json
import os
from pathlib import Path
import shlex
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
INPUT_ROOT = ROOT.parent / "lambda-input"
RADIANT_ROOT = ROOT.parent / "radiant"
INPUT_HEADERS = (
    "lib/dom_image.h",
    "lib/language_preferences.h",
    "lambda/input/input.hpp",
    "lambda/input/input-parsers.h",
    "lambda/input/input-context.hpp",
    "lambda/input/css/dom_element.hpp",
    "lambda/input/css/dom_blob_url.hpp",
    "lambda/input/css/dom_xml_serializer.h",
    "lambda/input/css/dom_svg_context.h",
    "lambda/input/html5/html5_parser.h",
    "lambda/io/mark_builder.hpp",
    "lambda/io/mark_editor.hpp",
    "lambda/io/mark_output_builder.hpp",
    "lambda/core/mark_reader.hpp",
    "lambda/core/collection_storage.h",
    "lambda/core/print.h",
    "lambda/format/format.h",
)
RADIANT_HEADERS = (
    "radiant/view.hpp",
    "radiant/layout.hpp",
    "radiant/render.hpp",
    "radiant/event.hpp",
    "radiant/editing_dom_internal.hpp",
    "radiant/editing_dom_host.hpp",
    "radiant/script_loop_host.hpp",
    "radiant/form_controls.hpp",
    "radiant/loader_runtime_host.hpp",
    "radiant/reactive_render_host.hpp",
    "radiant/stylesheet_sync.hpp",
    "radiant/dom_mutation_prepare.hpp",
    "radiant/dom_backed_tree.hpp",
    "radiant/layout_table_mutation.hpp",
    "radiant/dom_mutation_state.hpp",
    "radiant/dom_mutation_record.hpp",
    "radiant/css_mutation_queries.hpp",
    "radiant/radiant.hpp",
    "radiant/gl_core.hpp",
    "radiant/svg_animation.hpp",
    "radiant/document_clock.hpp",
    "radiant/devtools_adapter.hpp",
    "radiant/input_value.hpp",
    "radiant/select_options.hpp",
    "radiant/scroll_observer.hpp",
    "radiant/scroll_request.hpp",
    "radiant/state_store_host.hpp",
    "radiant/source_path.hpp",
    "radiant/source_path_value.hpp",
    "radiant/source_position_host.hpp",
    "radiant/layout_host.hpp",
    "radiant/package_layout_host.hpp",
    "radiant/pdf_export_host.hpp",
    "radiant/speech_event_host.hpp",
    "radiant/export_runtime_host.hpp",
    "radiant/event_sim_script_host.hpp",
    "radiant/speech.hpp",
    "radiant/resource_observer.hpp",
    "radiant/svg_hit.hpp",
    "radiant/svg_geometry_context.hpp",
    "radiant/scene3d/scene3d.hpp",
    "radiant/scene3d/scene3d_animation_internal.hpp",
    "radiant/scene3d/scene3d_audio_internal.hpp",
    "radiant/scene3d/scene3d_particles_internal.hpp",
    "radiant/scene3d/scene3d_physics_internal.hpp",
    "radiant/scene3d/scene3d_navigation_internal.hpp",
    "radiant/scene3d/scene3d_navigation_event_host.hpp",
    "radiant/scene3d/scene3d_control_access.hpp",
    "radiant/scene3d/scene3d_physics_event_host.hpp",
    "radiant/scene3d/scene3d_physics.hpp",
)
RADIANT_SOURCES = ("radiant/cmd_layout.cpp", "radiant/stylesheet_sync.cpp", "radiant/reactive_render_host.cpp", "radiant/loader_runtime_host.cpp", "radiant/form_controls.cpp", "radiant/window.cpp", "radiant/script_loop_host.cpp", "radiant/select_options.cpp", "radiant/dom_backed_tree.cpp", "radiant/layout_table_mutation.cpp", "radiant/css_inheritance.cpp", "radiant/css_mutation_queries.cpp", "radiant/editing_dom_waist.cpp", "radiant/dom_mutation_record.cpp", "radiant/dom_mutation_state.cpp", "radiant/dom_mutation_prepare.cpp", "radiant/editing_dom_host.cpp", "radiant/state_store.cpp", "radiant/scene3d/scene3d_navigation.cpp", "radiant/scene3d/scene3d_navigation_event_host.cpp", "radiant/scene3d/scene3d_physics.cpp", "radiant/scene3d/scene3d_particles.cpp", "radiant/scene3d/scene3d_audio.cpp", "radiant/scene3d/scene3d_animation.cpp", "radiant/svg_animation.cpp", "radiant/css_animation.cpp", "radiant/render_svg_inline.cpp", "radiant/state_store_host.cpp", "radiant/devtools_host.cpp", "radiant/event_sim.cpp", "radiant/input_value.cpp", "radiant/input_state.cpp", "radiant/layout.cpp", "radiant/layout_block.cpp", "radiant/layout_custom.cpp", "radiant/layout_mathml.cpp", "radiant/layout_form.cpp", "radiant/layout_host.cpp", "radiant/package_layout_host.cpp", "radiant/pdf_export_host.cpp", "radiant/render_pdf.cpp", "radiant/speech_event_host.cpp", "radiant/scene3d/scene3d_physics_event_host.cpp", "radiant/export_runtime_host.cpp", "radiant/event_sim_script_host.cpp", "radiant/event_sim_render.cpp", "radiant/render_output.cpp", "radiant/speech.cpp", "radiant/speech_markup.cpp", "radiant/resource_observer.cpp", "radiant/dom_image.cpp", "radiant/svg_hit.cpp", "radiant/svg_geometry_context.cpp", "radiant/render_svg.cpp", "radiant/geomap.cpp", "radiant/render_path_geometry.cpp", "radiant/svg_length_axis.cpp", "radiant/scroll_observer.cpp", "radiant/scroll_geometry.cpp", "radiant/scroll_request.cpp", "radiant/source_path.cpp", "radiant/source_path_value.cpp", "radiant/source_position_host.cpp", "radiant/source_pos_bridge.cpp", "radiant/scroller.cpp", "radiant/surface.cpp", "radiant/rdt_vector_tvg.cpp", "radiant/render_raster_walk.cpp", "radiant/document_clock.cpp", "radiant/gl_core.cpp", "radiant/canvas_2d.cpp", "radiant/layout_box.cpp", "radiant/css_prop_table.cpp", "radiant/page_document.cpp", "radiant/resolve_htm_style.cpp", "radiant/resolve_css_style.cpp", "radiant/css_resolved_style.cpp",
                   "radiant/view_tree_css.cpp", "radiant/view_pool.cpp", "radiant/scene3d/scene3d.cpp", "radiant/scene3d/scene3d_runtime.cpp")
INPUT_SOURCES = ("lambda/core/lambda-data.cpp", "lambda/core/lambda-decimal.cpp",
                 "lambda/core/name_pool.cpp", "lambda/core/print.cpp", "lambda/core/utf_string.cpp",
                 "lambda/core/shape_builder.cpp", "lambda/io/collection_io.cpp", "lambda/io/target.cpp",
                 "lambda/input/input_sysinfo.cpp", "lambda/input/input-utils.cpp",
                 "lambda/input/css/dom_element.cpp", "lambda/input/css/dom_xml_serializer.cpp", "lambda/input/css/dom_blob_url.cpp", "lambda/input/css/dom_lifecycle.cpp", "lib/language_preferences.cpp")
INPUT_TEST_SOURCES = (
    "test/test_input_dom_native_gtest.cpp", "test/test_input_blob_url_gtest.cpp", "test/test_input_model_gtest.cpp", "test/test_input_roundtrip_gtest.cpp",
    "test/test_input_sourcepos_gtest.cpp", "test/test_markup_modular_gtest.cpp",
    "test/test_input_rdb_gtest.cpp", "test/test_format_markup_gtest.cpp",
    "test/test_input_test_helpers.cpp", "test/test_input_math_gtest.cpp", "test/test_dir_gtest.cpp",
    "test/wpt/test_wpt_html_parser_gtest.cpp", "test/markup/test_markdown_gtest.cpp", "test/test_yaml_suite_gtest.cpp",
)
RUNTIME_AREAS = (("lambda", "runtime"), ("lambda", "js"),
                 ("lambda", "dom"), ("lambda", "module"))


def check_headers(name: str, headers: tuple[str, ...], forbidden: tuple[tuple[str, ...], ...],
                  defines: tuple[str, ...] = ()) -> bool:
    config = json.loads((ROOT / "build_lambda_config.json").read_text())
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CPPFLAGS", ""))
    include_paths = list(config["includes"])
    include_paths.extend(library["include"] for library in config["libraries"]
                         if isinstance(library.get("include"), str))
    includes = [f"-I{ROOT / path}" for path in dict.fromkeys(include_paths)]
    command = compiler + flags + includes + [f"-D{define}" for define in defines] + \
        ["-std=c++17", "-MM", "-MT", name, "-x", "c++", "-"]
    dependencies = set()
    for header in headers:
        # Separate translation units keep unrelated headers from affecting one another.
        result = subprocess.run(command, input=f'#include "{header}"\n', text=True, cwd=ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if result.returncode:
            print(f"SPLIT_HEADERS: {name} dependency scan failed for {header}:\n{result.stderr}", file=sys.stderr)
            return False
        words = shlex.split(result.stdout.replace("\\\n", " "))
        dependencies.update(words[1:])
    violations = []
    for dependency in dependencies:
        path = (ROOT / dependency).resolve()
        # Classify real owners after following migration links in either direction.
        for project in (ROOT, INPUT_ROOT, RADIANT_ROOT):
            try:
                relative = path.relative_to(project)
            except ValueError:
                continue
            if any(relative.parts[:len(prefix)] == prefix for prefix in forbidden):
                violations.append(f"{project.name}/{relative.as_posix()}")
            break
    for path in sorted(set(violations)):
        print(f"SPLIT_HEADERS: {name} imports forbidden header {path}", file=sys.stderr)
    if violations:
        return False
    print(f"SPLIT_HEADERS: {name} checked {len(headers)} entry files and {len(dependencies)} dependencies")
    return True


def main() -> int:
    input_ok = check_headers("input", INPUT_HEADERS, RUNTIME_AREAS + (("radiant",),))
    data_ok = check_headers("input-data-sources", INPUT_SOURCES, RUNTIME_AREAS + (("radiant",),),
                            ("LAMBDA_IO_STATIC_VALUES",))
    tests_ok = check_headers("input-test-sources", INPUT_TEST_SOURCES,
                             RUNTIME_AREAS + (("radiant",),))
    radiant_ok = check_headers("radiant", RADIANT_HEADERS, RUNTIME_AREAS)
    radiant_sources_ok = check_headers("radiant-native-sources", RADIANT_SOURCES, RUNTIME_AREAS)
    return 0 if input_ok and data_ok and tests_ok and radiant_ok and radiant_sources_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
