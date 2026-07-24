#!/usr/bin/env python3
"""Fail fast when a hosted-Python boundary regresses.

The checks start with boundaries already migrated. Additional core-coupling
checks are intentionally added only after their owning migration stage lands,
so this script never masks pre-existing debt with a broad allowlist.
"""

from pathlib import Path
import json
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
PYTHON_DIR = ROOT / "lambda" / "py"
MAIN = ROOT / "lambda" / "main.cpp"
CORE_JIT_CATALOG = ROOT / "lambda" / "runtime" / "sys_func_registry.c"
# Core sources moved under runtime; keep the architecture gate anchored to the
# live files so a source regrouping cannot silently disable boundary checks.
CORE_IMPORTER = ROOT / "lambda" / "runtime" / "build_ast.cpp"
CORE_TRANSPILE_HEADER = ROOT / "lambda" / "runtime" / "transpiler.hpp"
CORE_AST = ROOT / "lambda" / "runtime" / "ast-core.hpp"
BUILD_CONFIG = ROOT / "build_lambda_config.json"
PYTHON_JUBE_ADAPTER = PYTHON_DIR / "python_jube_module.cpp"
PYTHON_MIR_LOWERING = PYTHON_DIR / "transpile_py_mir.cpp"
PYTHON_RUNTIME_IMPORTS = PYTHON_DIR / "python_runtime_imports.cpp"
PYTHON_MODULE_DIR = ROOT / "modules" / "lang-python"
JUBE_HEADER = ROOT / "lambda" / "jube" / "jube.h"
JUBE_REGISTRY = ROOT / "lambda" / "jube" / "jube_registry.cpp"


# The inventory is deliberately produced by the same tool that enforces the
# boundary.  That prevents an unreviewed allowlist from drifting away from the
# actual source coupling it is meant to retire.
INVENTORY_DESTINATIONS = {
    "external_include": "H7A/H7B/H7C",
    "host_layout": "H6/H7B/H7C",
    "raw_mir_symbol": "H7C",
    "js_symbol": "H5",
    "core_python_symbol": "H3/H4/H6",
    "python_preprocessor": "H3/H10",
    "runtime_import": "H4/H7C",
    "module_registry": "H6",
    "build_dependency": "H8/H9",
}


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def fail(message: str) -> None:
    print(f"HOSTED_PY_ARCH: {message}", file=sys.stderr)
    raise SystemExit(1)


def inventory_entry(category: str, path: Path, token: str, line: int) -> dict:
    return {
        "category": category,
        "path": str(path.relative_to(ROOT)),
        "line": line,
        "token": token,
        "outcome": "extract_or_wrap",
        "destination": INVENTORY_DESTINATIONS[category],
    }


def line_number(source: str, offset: int) -> int:
    return source.count("\n", 0, offset) + 1


def append_matches(entries: list, category: str, path: Path, source: str,
                   pattern: re.Pattern) -> None:
    for match in pattern.finditer(source):
        entries.append(inventory_entry(category, path, match.group(0),
                                       line_number(source, match.start())))


def hosted_python_inventory() -> list:
    """Return every known Python/core coupling with an owning migration stage."""
    entries = []
    include_pattern = re.compile(r'^\s*#\s*include\s+["<]([^">]+)[">]', re.MULTILINE)
    host_layout_pattern = re.compile(
        r'\b(?:Runtime|EvalContext|Context|Input|MIR_context_t|MIR_module_t|'
        r'MIR_item_t|MirEmitter|AstNode|NameScope|FnAnalysis)\b')
    raw_host_global_pattern = re.compile(
        r'\b(?:context|py_input|_lambda_rt|lambda_recovery_[A-Za-z0-9_]+)\b')
    raw_mir_symbol_pattern = re.compile(r'\b_?MIR_[A-Za-z0-9_]+\b')
    js_symbol_pattern = re.compile(r'\bjs_[A-Za-z0-9_]+')
    module_registry_pattern = re.compile(
        r'\b(?:module_get|module_is_loading|module_register_[A-Za-z0-9_]*|'
        r'load_script|module_build_lambda_namespace)\b')

    for path in sorted(PYTHON_DIR.rglob('*')):
        if path.suffix not in {'.c', '.cc', '.cpp', '.h', '.hpp'}:
            continue
        source = text(path)
        for match in include_pattern.finditer(source):
            include = match.group(1)
            if include.startswith('../') or include.startswith('../../'):
                entries.append(inventory_entry(
                    'external_include', path, include, line_number(source, match.start(1))))
        append_matches(entries, 'host_layout', path, source, host_layout_pattern)
        append_matches(entries, 'host_layout', path, source, raw_host_global_pattern)
        append_matches(entries, 'raw_mir_symbol', path, source, raw_mir_symbol_pattern)
        append_matches(entries, 'js_symbol', path, source, js_symbol_pattern)
        append_matches(entries, 'module_registry', path, source, module_registry_pattern)

    runtime_import_source = text(PYTHON_RUNTIME_IMPORTS)
    for match in re.finditer(r'\{\s*"(py_[A-Za-z0-9_]+)"', runtime_import_source):
        entries.append(inventory_entry(
            'runtime_import', PYTHON_RUNTIME_IMPORTS, match.group(1),
            line_number(runtime_import_source, match.start(1))))

    for path in (MAIN, CORE_JIT_CATALOG, CORE_IMPORTER, CORE_TRANSPILE_HEADER, CORE_AST):
        source = text(path)
        append_matches(entries, 'core_python_symbol', path, source,
                       re.compile(r'\b(?:py_[A-Za-z0-9_]+|load_py_module|transpile_py_to_mir)\b'))
        append_matches(entries, 'python_preprocessor', path, source,
                       re.compile(r'\bLAMBDA_PYTHON\b'))

    build_source = text(BUILD_CONFIG)
    for match in re.finditer(r'(?:lang-python|lambda-jube\.exe|lambda-jube)', build_source):
        entries.append(inventory_entry(
            'build_dependency', BUILD_CONFIG, match.group(0),
            line_number(build_source, match.start())))

    return sorted(entries, key=lambda entry: (
        entry['category'], entry['path'], entry['line'], entry['token']))


def write_inventory() -> None:
    payload = {
        'schema_version': 1,
        'root': str(ROOT),
        'entries': hosted_python_inventory(),
    }
    print(json.dumps(payload, indent=2, sort_keys=True))


def check_python_has_no_js_runtime_calls() -> None:
    pattern = re.compile(r"\bjs_[A-Za-z0-9_]+")
    forbidden = ("load_js_module", "heap_calloc_js_env", '"../js/', '"../js\\')
    for path in sorted(PYTHON_DIR.rglob("*")):
        if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
            continue
        source = text(path)
        match = pattern.search(source)
        if match:
            fail(f"{path.relative_to(ROOT)} references JavaScript runtime symbol {match.group(0)}")
        for token in forbidden:
            if token in source:
                fail(f"{path.relative_to(ROOT)} references JavaScript implementation token {token}")


def check_python_uses_neutral_data_membrane() -> None:
    # H5 has retired the only runtime Input bridge.  Keep this narrow so the
    # compiler's separately tracked AST/session migration is not hidden behind
    # a premature broad ban on all core names.
    runtime_files = (
        PYTHON_DIR / "py_runtime.cpp",
        PYTHON_DIR / "py_async.cpp",
        PYTHON_DIR / "py_class.cpp",
        PYTHON_DIR / "py_builtins.cpp",
        PYTHON_DIR / "py_stdlib.cpp",
    )
    for path in runtime_files:
        source = text(path)
        for token in ("py_input", "map_put(", "->name_pool", "->pool",
                      "heap_calloc_closure_env", "owned_item_slot_store(",
                      "owned_item_slot_read(", "gc_is_managed(", "gc_get_header(",
                      "heap_calloc_class(", "heap_calloc(sizeof(Function)"):
            if token in source:
                fail(f"{path.relative_to(ROOT)} bypasses JubeHostDataAPI via {token}")
        for token in ("heap_register_gc_root(",):
            if token in source:
                fail(f"{path.relative_to(ROOT)} bypasses hosted root/data service via {token}")
    source = text(PYTHON_JUBE_ADAPTER)
    if "py_set_hosted_data_api" not in source or "host->data" not in source:
        fail("lambda/py/python_jube_module.cpp does not negotiate JubeHostDataAPI")


def check_python_uses_opaque_hosted_roots() -> None:
    # H5's root-frame projection must not quietly fall back to Lambda's C++
    # helper or raw C frame representation in a newly added Python helper.
    for path in sorted(PYTHON_DIR.rglob("*")):
        if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
            continue
        source = text(path)
        for token in ("LambdaRootFrame", "Rooted<", "lambda_root_frame_"):
            if token in source:
                fail(f"{path.relative_to(ROOT)} bypasses opaque Jube root API via {token}")
        if re.search(r"\bRootFrame\s+[A-Za-z_]", source):
            fail(f"{path.relative_to(ROOT)} constructs a legacy RootFrame directly")
    adapter = text(PYTHON_JUBE_ADAPTER)
    for token in ("hosted_language->roots", "py_set_hosted_root_api"):
        if token not in adapter:
            fail("lambda/py/python_jube_module.cpp does not negotiate JubeHostRootAPI")


def check_main_has_no_python_branch() -> None:
    source = text(MAIN)
    forbidden = ("py/py_transpiler", "transpile_py_to_mir", "LAMBDA_PYTHON")
    for token in forbidden:
        if token in source:
            fail(f"lambda/main.cpp retains Python-specific dispatch token {token}")


def check_core_jit_catalog_has_no_python_ownership() -> None:
    source = text(CORE_JIT_CATALOG)
    forbidden = ("py/py_", '"py_')
    for token in forbidden:
        if token in source:
            fail(f"lambda/runtime/sys_func_registry.c retains Python-owned JIT import token {token}")


def check_core_importer_has_no_python_loader() -> None:
    for path in (CORE_IMPORTER, CORE_TRANSPILE_HEADER):
        source = text(path)
        for token in ("load_py_module", "LAMBDA_PYTHON"):
            if token in source:
                fail(f"{path.relative_to(ROOT)} retains Python-specific module-loader token {token}")


def check_shared_ast_has_no_python_profile() -> None:
    source = text(CORE_AST)
    for token in ("LAMBDA_PYTHON", "py_profile", '"python"'):
        if token in source:
            fail(f"lambda/runtime/ast-core.hpp retains Python-specific profile token {token}")


def check_python_goldens_are_complete() -> None:
    scripts = {path.stem for path in (ROOT / "test" / "py").glob("test_py_*.py")}
    goldens = {path.stem for path in (ROOT / "test" / "py").glob("test_py_*.txt")}
    missing = sorted(scripts - goldens)
    orphaned = sorted(goldens - scripts)
    if missing:
        fail(f"Python scripts without expected output: {', '.join(missing)}")
    if orphaned:
            fail(f"Python expected output without script: {', '.join(orphaned)}")


def check_no_second_jube_runtime_target() -> None:
    source = text(BUILD_CONFIG)
    if '"output": "lambda-jube.exe"' in source:
        fail("build configuration still defines an independently compiled lambda-jube.exe")


def check_python_adapter_uses_hosted_services() -> None:
    source = text(PYTHON_JUBE_ADAPTER)
    forbidden_includes = (
        '"../format/',
        '"../ast.hpp"',
        '"../lambda-stack.h"',
        '"../../lib/file.h"',
        '"../../lib/mem_factory.h"',
        '"../../lib/strbuf.h"',
    )
    for token in forbidden_includes:
        if token in source:
            fail(f"lambda/py/python_jube_module.cpp bypasses hosted service {token}")
    required = (
        "source_read",
        "source_release",
        "write_result",
        "session_alloc",
        "session_free",
        "execution_create",
        "execution_destroy",
        "execution_link_module",
        "mir_context_create",
        "mir_context_destroy",
        "mir_module_create",
        "mir_module_finalize_and_load",
        "mir_function_lookup",
        "mir_function_finish",
        "mir_item_function_create",
        "mir_function_forward_create",
        "mir_item_function_proto_create",
        "mir_function_register_lookup",
        "mir_function_frame_runtime_load",
        "mir_function_register_create",
        "mir_label_create",
        "mir_instruction_emit",
        "mir_label_emit",
        "mir_function_frame_finalize",
        "execution_activate",
        "execution_activate_import",
        "execution_run_main",
        "execution_finish_guest",
    )
    for token in required:
        if token not in source:
            fail(f"lambda/py/python_jube_module.cpp no longer uses hosted service {token}")
    for token in ("Runtime runtime", "(Runtime*)", "runtime_init(", "runtime_cleanup("):
        if token in source:
            fail(f"lambda/py/python_jube_module.cpp retains direct runtime lifecycle token {token}")
    for token in ("runtime_catalog", "register_imports"):
        if token not in source:
            fail(f"lambda/py/python_jube_module.cpp no longer negotiates Jube runtime catalog {token}")


def check_python_runtime_catalog_uses_jube_api() -> None:
    source = text(PYTHON_RUNTIME_IMPORTS)
    for token in ('"../sys_func_registry.h"', "JitImport", "jit_register_module_imports"):
        if token in source:
            fail(f"lambda/py/python_runtime_imports.cpp bypasses Jube runtime catalog via {token}")
    for token in ("JubeRuntimeImport", "JubeRuntimeCatalogAPI", "register_imports"):
        if token not in source:
            fail(f"lambda/py/python_runtime_imports.cpp no longer uses Jube runtime catalog {token}")


def check_python_frontend_has_no_monolithic_transpiler_header() -> None:
    source = text(PYTHON_DIR / "py_transpiler.hpp")
    if '"../transpiler.hpp"' in source:
        fail("lambda/py/py_transpiler.hpp retains the monolithic core transpiler header")


def check_python_mir_dump_uses_hosted_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ('"../runtime/mir_dump.h"', "mir_dump_instrumentation_enabled(",
                  "mir_dump_write_context("):
        if token in source:
            fail(f"lambda/py/transpile_py_mir.cpp bypasses hosted MIR debug service via {token}")
    if "mir_debug_dump_if_enabled" not in source:
        fail("lambda/py/transpile_py_mir.cpp does not use hosted MIR debug service")


def check_python_mir_lowering_uses_hosted_name_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ('"../runtime/heap_api.h"', '"../runtime/transpiler.hpp"',
                  "<mir-gen.h>", "heap_create_name("):
        if token in source:
            fail(f"lambda/py/transpile_py_mir.cpp bypasses hosted name service via {token}")
    for token in ("pm_hosted_name_from_utf8_n", "py_data_name_from_utf8_n"):
        if token not in source:
            fail(f"lambda/py/transpile_py_mir.cpp does not use length-aware hosted name service {token}")


def check_python_import_lowering_uses_module_graph() -> None:
    source = text(PYTHON_MIR_LOWERING)
    legacy_loader = "// Load Python module for cross-language import"
    if legacy_loader not in source:
        fail("lambda/py/transpile_py_mir.cpp lost its isolated legacy loader marker")
    lowering, legacy = source.split(legacy_loader, 1)
    forbidden = (
        "module_is_loading",
        "module_get(",
        "load_script(",
        "module_build_lambda_namespace",
        "load_py_module(",
    )
    for token in forbidden:
        if token in lowering:
            fail(f"Python import lowering bypasses the hosted module graph via {token}")
    required = (
        "pm_loading_module_namespace",
        "pm_load_lambda_module",
        "jube_load_hosted_module",
        "jube_load_language_module",
    )
    for token in required:
        if token not in lowering:
            fail(f"Python import lowering no longer uses hosted module graph service {token}")
    for token in ("(Context*)context", "context->"):
        if token in lowering:
            fail(f"normal Python compilation retains raw host activation access via {token}")
    for token in ("../module_registry.h", "module_get(", "module_is_loading",
                  "module_register_", "module_build_lambda_namespace", "load_script(",
                  "read_text_file", "import_resolver", "jit_init(", "MIR_finish(",
                  "MIR_finish_module(", "MIR_load_module(", "find_func(",
                  "MIR_new_func_arr(", "MIR_new_forward(", "MIR_new_proto_arr(", "MIR_reg(",
                  "mir_guest_finish_context("):
        if token in source:
            fail(f"Python compiler bypasses hosted graph/source service via {token}")
    for token in ("module_state", "module_begin_loading", "module_publish"):
        if token not in legacy:
            fail(f"Python legacy loader no longer uses hosted module graph service {token}")
    for token in ("pm_try_load_module(Runtime*", "mt->runtime"):
        if token in source:
            fail(f"Python import lowering retains a Runtime-shaped graph token {token}")
    for token in ("(Runtime*)", "EvalContext", "Input::create", "heap_init(",
                  "mir_guest_finish_context(", "lambda_recovery_"):
        if token in source:
            fail(f"Python compiler retains raw host execution internals via {token}")
    if '"_lambda_rt"' in source:
        fail("Python compiler retains direct _lambda_rt storage import")
    runtime_loader_start = source.find("static PmCompilerRegister pm_load_side_stack_runtime(")
    runtime_loader_end = source.find("static void pm_begin_function_frame(",
                                     runtime_loader_start)
    if runtime_loader_start < 0 or runtime_loader_end < 0 or "MIR_new_mem_op" in source[
            runtime_loader_start:runtime_loader_end]:
        fail("Python compiler retains a raw frame-runtime memory load")
    for token in ("em_new_reg(", "em_new_label("):
        if token in source:
            fail(f"Python compiler retains direct compiler identity allocation via {token}")


def check_python_literal_moves_use_hosted_instruction_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static void pm_emit_i64_immediate(")
    end = source.find("static void pm_emit_f64_immediate(", start)
    if start < 0 or end < 0:
        fail("Python compiler lost the hosted integer-immediate move adapter")
    adapter = source[start:end]
    if "pm_emit_hosted_instruction" not in adapter:
        fail("Python compiler bypasses hosted instruction emission for literals")
    for token in ("MIR_new_insn(", "MIR_new_reg_op(", "MIR_new_int_op("):
        if token in adapter:
            fail(f"Python compiler retains raw literal MIR construction via {token}")


def check_python_truth_branches_use_hosted_instruction_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static void pm_emit_branch_true(")
    end = source.find("static void pm_emit_branch_false(", start)
    if start < 0 or end < 0:
        fail("Python compiler lost the hosted truth-branch adapter")
    adapter = source[start:end]
    if "pm_emit_hosted_instruction" not in adapter:
        fail("Python compiler bypasses hosted instruction emission for truth branches")
    for token in ("MIR_new_insn(", "MIR_new_label_op(", "MIR_new_reg_op("):
        if token in adapter:
            fail(f"Python compiler retains raw truth-branch MIR construction via {token}")


def check_python_labels_use_hosted_emission_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static void pm_emit_label(")
    end = source.find("static PmCompilerRegister pm_load_side_stack_runtime(", start)
    if start < 0 or end < 0:
        fail("Python compiler lost the hosted label-emission adapter")
    adapter = source[start:end]
    if "mir_label_emit" not in adapter:
        fail("Python compiler bypasses hosted label emission")
    if "em_emit_label(" in adapter or "MIR_append_insn(" in adapter:
        fail("Python compiler retains raw label emission")


def check_python_i64_operations_use_hosted_instruction_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static void pm_emit_i64_operation(")
    end = source.find("static void pm_emit_item_return_operand(", start)
    if start < 0 or end < 0:
        fail("Python compiler lost the hosted integer-operation adapter")
    adapter = source[start:end]
    if "pm_emit_hosted_instruction" not in adapter:
        fail("Python compiler bypasses hosted instruction emission for integer operations")
    for token in ("MIR_new_insn(", "MIR_new_reg_op(", "MIR_new_int_op("):
        if token in adapter:
            fail(f"Python compiler retains raw integer-operation MIR construction via {token}")


def check_python_numeric_lowering_uses_hosted_instruction_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static PmCompilerRegister pm_box_int_reg(")
    end = source.find("// ============================================================================\n// TCO helpers", start)
    if start < 0 or end < 0:
        fail("Python compiler lost the native numeric lowering boundary")
    lowering = source[start:end]
    for token in ("MIR_new_insn(", "MIR_new_label_op(", "MIR_new_ref_op("):
        if token in lowering:
            fail(f"Python native numeric lowering retains raw MIR construction via {token}")
    for token in ("pm_emit_i64_operation", "pm_emit_f64_operation",
                  "pm_emit_i64_register_move", "pm_emit_branch_true",
                  "pm_emit_branch_false", "pm_emit_jump"):
        if token not in lowering:
            fail(f"Python native numeric lowering no longer uses hosted instruction service {token}")


def check_python_runtime_calls_use_hosted_call_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static PmCompilerRegister pm_emit_hosted_runtime_call_operands(")
    end = source.find("struct PmArgScope", start)
    if start < 0 or end < 0:
        fail("Python compiler is missing the hosted runtime-call adapter")
    adapter = source[start:end]
    if "mir_runtime_import_call_emit" not in adapter:
        fail("Python compiler does not use the hosted runtime-call service")
    for token in ("em_call_", "em_call_void_"):
        if token in adapter:
            fail(f"Python compiler retains shared raw call emission via {token}")


def check_python_local_direct_calls_use_hosted_call_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static void pm_emit_local_direct_call(")
    end = source.find("static void pm_emit_label(", start)
    if start < 0 or end < 0:
        fail("Python compiler is missing the hosted local-direct-call adapter")
    adapter = source[start:end]
    if "mir_local_direct_call_emit" not in adapter:
        fail("Python compiler does not use the hosted local-direct-call service")
    for token in ("em_emit_borrowed_call(", "MIR_new_insn_arr("):
        if token in adapter:
            fail(f"Python compiler retains raw local-direct-call emission via {token}")


def check_python_item_returns_use_hosted_return_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static void pm_emit_item_return_operand(")
    end = source.find("static void pm_emit_local_direct_call(", start)
    if start < 0 or end < 0:
        fail("Python compiler is missing the hosted Item-return adapter")
    adapter = source[start:end]
    if "mir_item_return_emit" not in adapter:
        fail("Python compiler does not use the hosted Item-return service")
    for token in ("MIR_new_insn(", "MIR_new_ret_insn(", "MIR_JMP"):
        if token in adapter:
            fail(f"Python compiler retains raw Item-return emission via {token}")


def check_python_lowering_has_no_raw_instruction_decoder() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ("static void pm_emit(PyMirTranspiler* mt, MIR_insn_t insn)",
                  "em_emit_insn(&mt->em, insn)", "_MIR_free_insn(mt->em.ctx, insn)"):
        if token in source:
            fail(f"Python compiler retains raw MIR instruction decoder via {token}")


def check_python_frame_finalization_uses_hosted_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static void pm_finish_function_frame(")
    end = source.find("// Call helpers", start)
    if start < 0 or end < 0:
        fail("Python compiler is missing the hosted frame-finalization adapter")
    adapter = source[start:end]
    if "mir_function_frame_finalize" not in adapter:
        fail("Python compiler does not use the hosted frame-finalization service")
    for token in ("em_finalize_semantic_root_write_back", "em_finalize_scalar_homes",
                  "em_finalize_frame_prologue", "em_finalize_function_metadata",
                  "em_adopt_scalar_item", "em_store_frame_top", "MIR_new_ret_insn("):
        if token in adapter:
            fail(f"Python compiler retains raw frame finalization via {token}")


def check_python_reference_moves_use_hosted_instruction_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static void pm_emit_i64_reference_move(")
    end = source.find("static void pm_emit_f64_operation(", start)
    if start < 0 or end < 0:
        fail("Python compiler is missing the hosted opaque-reference move adapter")
    adapter = source[start:end]
    if "JUBE_COMPILER_INSN_MOVE_I64_REFERENCE" not in adapter or \
            "pm_emit_hosted_instruction" not in adapter:
        fail("Python compiler does not use the hosted opaque-reference move service")
    if "MIR_new_ref_op(" in adapter:
        fail("Python compiler retains raw opaque-reference move emission")


def check_python_root_candidates_use_hosted_service() -> None:
    source = text(PYTHON_MIR_LOWERING)
    if "mir_compiler_cursor_create" not in source:
        fail("Python compiler does not use the host-owned root-candidate cursor")
    for token in ("pm_root_call_value(", "mir_frame_root_candidate_note"):
        if token in source:
            fail(f"Python compiler retains direct root-candidate callback via {token}")


def check_python_function_frames_use_hosted_services() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static void pm_begin_function_frame(")
    end = source.find("// ============================================================================\n// Call helpers", start)
    if start < 0 or end < 0:
        fail("Python compiler is missing the hosted frame adapters")
    adapters = source[start:end]
    for token in ("mir_function_frame_begin", "mir_function_frame_scalar_return_home_set",
                  "mir_function_frame_finalize"):
        if token not in adapters:
            fail(f"Python compiler does not use hosted function-frame service {token}")
    if "mt->em.frame" in source:
        fail("Python compiler retains direct MirEmitter frame-layout access")


def check_python_import_cache_uses_hosted_services() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ("mir_compiler_import_cache_init",
                  "mir_compiler_import_cache_destroy",
                  "mir_local_direct_call_prototype_get_or_create"):
        if token not in source:
            fail(f"Python compiler does not use hosted import-cache service {token}")
    for token in ("mt->em.import_cache", "MirImportCacheEntry"):
        if token in source:
            fail(f"Python compiler retains direct import-cache ownership via {token}")


def check_python_function_selection_uses_hosted_services() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static void pm_select_hosted_function(")
    end = source.find("static bool pm_require_capacity(", start)
    if start < 0 or end < 0:
        fail("Python compiler is missing the hosted function-selection adapters")
    adapters = source[start:end]
    for token in ("mir_function_select", "mir_function_state_restore"):
        if token not in adapters:
            fail(f"Python compiler does not use hosted function-state service {token}")
    if "mir_function_state_suspend" not in source:
        fail("Python compiler does not use hosted function-state suspension")
    for token in ("mt->em.func_item =", "mt->em.func =", "em_function_arguments_"):
        if token in source:
            fail(f"Python compiler retains direct function-state ownership via {token}")
    if "mir_function_register_lookup_current" not in source:
        fail("Python compiler does not use hosted current-function register lookup")


def check_python_current_lowering_uses_hosted_cursor_services() -> None:
    source = text(PYTHON_MIR_LOWERING)
    start = source.find("static PmCompilerRegister pm_new_reg(")
    end = source.find("static void pm_begin_function_frame(", start)
    if start < 0 or end < 0:
        fail("Python compiler is missing the hosted current-lowering adapters")
    adapters = source[start:end]
    for token in ("mir_function_register_create_current", "mir_label_create_current",
                  "mir_instruction_emit_current", "mir_label_emit_current",
                  "mir_function_frame_runtime_load_current"):
        if token not in adapters:
            fail(f"Python compiler does not use hosted cursor service {token}")
    for token in ("mt->em.ctx", "mt->em.func", "mt->em.func_item", "mt->em.reg_counter"):
        if token in adapters:
            fail(f"Python lowering retains direct current compiler state via {token}")
    if "mir_function_finish_current" not in source:
        fail("Python compiler does not use hosted current-function finalization")


def check_python_cursor_construction_uses_hosted_services() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ("mir_item_function_create_typed_current",
                  "mir_function_forward_create_current",
                  "mir_module_finalize_and_load_current",
                  "mir_function_lookup_current"):
        if token not in source:
            fail(f"Python compiler does not use hosted cursor construction service {token}")
    for token in ("pm_create_hosted_item_function_typed(mt->em.ctx",
                  "pm_create_hosted_function_forward(mt->em.ctx",
                  "pm_finalize_and_load_hosted_mir_module(mt->em.ctx",
                  "pm_find_hosted_mir_function(ctx,"):
        if token in source:
            fail(f"Python compiler retains direct cursor construction via {token}")


def check_python_scalar_homes_use_hosted_services() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ("mir_scalar_home_create_current", "mir_scalar_home_bind_current"):
        if token not in source:
            fail(f"Python compiler does not use hosted scalar-home service {token}")
    for token in ("em_scalar_home_new(&mt->em)",
                  "em_scalar_home_bind(&mt->em)",
                  "em_scalar_home_ref(&mt->em)",
                  "em_materialize_frame_ref(&mt->em)"):
        if token in source:
            fail(f"Python compiler retains direct scalar-home ownership via {token}")


def check_python_compiler_cursor_is_opaque() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ("mir_compiler_cursor_create", "mir_compiler_cursor_destroy",
                  "compiler_cursor"):
        if token not in source:
            fail(f"Python compiler does not use opaque compiler cursor service {token}")
    for token in ("MirEmitter em", "mt->em", "emitter->ctx =",
                  "lookup_import_metadata = pm_"):
        if token in source:
            fail(f"Python compiler retains concrete compiler cursor state via {token}")


def check_python_compilation_artifacts_are_opaque() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ("void* compiler_context", "void* module"):
        if token not in source:
            fail(f"Python compiler does not retain opaque compilation artifact {token}")
    for token in ("MIR_context_t", "MIR_module_t"):
        if token in source:
            fail(f"Python compiler retains concrete compilation artifact via {token}")


def check_python_lowering_has_no_private_mir_types() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ("#include <mir.h>", "mir_emitter_shared.hpp",
                  "MIR_reg_t", "MIR_label_t", "MIR_item_t", "MIR_func_t",
                  "MIR_type_t", "MIR_T_"):
        if token in source:
            fail(f"Python compiler retains private MIR type dependency via {token}")
    for token in ("PmCompilerRegister", "PmCompilerLabel",
                  "PmCompilerFunctionItem", "PmCompilerFunction"):
        if token not in source:
            fail(f"Python compiler does not use semantic compiler identity {token}")


def check_hosted_compiler_cursor_rejects_stale_handles() -> None:
    source = text(JUBE_REGISTRY)
    for token in ("jube_host_mir_cursor_register", "jube_host_mir_cursor_emitter",
                  "jube_host_mir_cursor_unregister"):
        if token not in source:
            fail(f"Jube host does not implement opaque compiler cursor service {token}")
    if "MirEmitter* emitter = (MirEmitter*)compiler_cursor;" in source:
        fail("Jube host accepts a raw compiler cursor pointer")
    destroy_start = source.find("static void jube_host_mir_compiler_cursor_destroy(")
    if destroy_start < 0:
        fail("Jube host is missing compiler cursor destruction")
    destroy_end = source.find("static int jube_host_mir_", destroy_start + 1)
    if destroy_end < 0:
        fail("Jube host compiler cursor destruction has no following service")
    destroy = source[destroy_start:destroy_end]
    invalidate = destroy.find("jube_host_mir_cursor_unregister(compiler_cursor);")
    release = destroy.find("jube_host_mir_cursor_dispose(emitter);")
    if invalidate < 0 or release < 0 or invalidate > release:
        fail("Jube host does not invalidate compiler cursor before emitter release")


def check_hosted_compiler_context_invalidates_cursors() -> None:
    source = text(JUBE_REGISTRY)
    if "jube_host_mir_cursor_invalidate_context" not in source:
        fail("Jube host does not invalidate compiler cursors for a finished context")
    destroy_start = source.find("static void jube_host_mir_context_destroy(")
    destroy_end = source.find("static void* jube_host_mir_module_create(", destroy_start)
    if destroy_start < 0 or destroy_end < 0:
        fail("Jube host is missing opaque MIR context destruction")
    destroy = source[destroy_start:destroy_end]
    invalidate = destroy.find("jube_host_mir_cursor_invalidate_context(mir_context);")
    finish = destroy.find("MIR_finish(")
    if invalidate < 0 or finish < 0 or invalidate > finish:
        fail("Jube host does not invalidate compiler cursors before MIR context release")


def check_hosted_compiler_function_handles_are_owner_checked() -> None:
    source = text(JUBE_REGISTRY)
    for token in ("jube_host_mir_cursor_track_function",
                  "jube_host_mir_cursor_owns_function"):
        if token not in source:
            fail(f"Jube host does not track compiler function ownership via {token}")
    select_start = source.find("static int jube_host_mir_function_select(")
    select_end = source.find("static int jube_host_mir_function_state_restore(", select_start)
    if select_start < 0 or select_end < 0:
        fail("Jube host is missing compiler function selection")
    select = source[select_start:select_end]
    ownership = select.find("jube_host_mir_cursor_owns_function(")
    mir_lookup = select.find("MIR_get_item_func(")
    if ownership < 0 or mir_lookup < 0 or ownership > mir_lookup:
        fail("Jube host does not validate compiler function ownership before MIR lookup")
    create_start = source.find("static int jube_host_mir_item_function_create_typed_current(")
    create_end = source.find("static int jube_host_mir_function_forward_create_current(",
                             create_start)
    if create_start < 0 or create_end < 0 or \
            "jube_host_mir_cursor_track_function(" not in source[create_start:create_end]:
        fail("Jube host does not record current compiler function ownership")


def check_hosted_compiler_state_tokens_are_opaque() -> None:
    source = text(JUBE_REGISTRY)
    for token in ("jube_host_mir_state_token_register",
                  "jube_host_mir_state_token_find",
                  "jube_host_mir_state_tokens_discard_slot"):
        if token not in source:
            fail(f"Jube host does not manage opaque compiler state tokens via {token}")
    if "JubeMirFunctionStateToken* state = (JubeMirFunctionStateToken*)state_token;" in source:
        fail("Jube host dereferences a raw compiler state token")
    suspend_start = source.find("static int jube_host_mir_function_state_suspend(")
    suspend_end = source.find("static int jube_host_mir_function_select(", suspend_start)
    if suspend_start < 0 or suspend_end < 0 or \
            "jube_host_mir_state_token_register(compiler_cursor, state)" not in \
            source[suspend_start:suspend_end]:
        fail("Jube host does not register an opaque compiler state token")
    restore_start = source.find("static int jube_host_mir_function_state_restore(")
    restore_end = source.find("static int jube_host_mir_function_register_lookup_current(",
                              restore_start)
    restore = source[restore_start:restore_end]
    lookup = restore.find("jube_host_mir_state_token_find(state_token,")
    state_access = restore.find("entry->state")
    if restore_start < 0 or restore_end < 0 or lookup < 0 or state_access < 0 or \
            lookup > state_access:
        fail("Jube host does not validate an opaque compiler state token before use")
    destroy_start = source.find("static void jube_host_mir_compiler_cursor_destroy(")
    destroy_end = source.find("static void jube_host_mir_debug_dump_if_enabled(", destroy_start)
    if destroy_start < 0 or destroy_end < 0 or \
            "jube_host_mir_state_tokens_discard_slot(" not in source[destroy_start:destroy_end]:
        fail("Jube host does not discard compiler state tokens with their cursor")


def check_hosted_compiler_labels_are_owner_checked() -> None:
    source = text(JUBE_REGISTRY)
    for token in ("jube_host_mir_cursor_track_label",
                  "jube_host_mir_cursor_owns_label"):
        if token not in source:
            fail(f"Jube host does not track compiler label ownership via {token}")
    create_start = source.find("static int jube_host_mir_label_create_current(")
    create_end = source.find("static int jube_host_mir_instruction_emit_current(",
                             create_start)
    if create_start < 0 or create_end < 0 or \
            "jube_host_mir_cursor_track_label(" not in source[create_start:create_end]:
        fail("Jube host does not record current compiler label ownership")
    emit_start = source.find("static int jube_host_mir_label_emit_current(")
    emit_end = source.find("static int jube_host_mir_function_frame_runtime_load_current(",
                           emit_start)
    if emit_start < 0 or emit_end < 0:
        fail("Jube host is missing current compiler label emission")
    emit = source[emit_start:emit_end]
    ownership = emit.find("jube_host_mir_cursor_owns_label(")
    host_emit = emit.find("jube_host_mir_label_emit(")
    if ownership < 0 or host_emit < 0 or ownership > host_emit:
        fail("Jube host does not validate compiler label ownership before emission")


def check_hosted_compiler_direct_call_handles_are_owner_checked() -> None:
    source = text(JUBE_REGISTRY)
    for token in ("jube_host_mir_cursor_track_prototype",
                  "jube_host_mir_cursor_owns_prototype",
                  "jube_host_mir_cursor_owns_function_item"):
        if token not in source:
            fail(f"Jube host does not track compiler direct-call ownership via {token}")
    call_start = source.find("static int jube_host_mir_local_direct_call_emit(")
    call_end = source.find("static int jube_host_mir_item_return_emit(", call_start)
    if call_start < 0 or call_end < 0:
        fail("Jube host is missing local direct-call emission")
    call = source[call_start:call_end]
    prototype_check = call.find("jube_host_mir_cursor_owns_prototype(")
    target_check = call.find("jube_host_mir_cursor_owns_function_item(")
    raw_operand = call.find("MIR_new_ref_op(")
    if prototype_check < 0 or target_check < 0 or raw_operand < 0 or \
            prototype_check > raw_operand or target_check > raw_operand:
        fail("Jube host does not validate direct-call ownership before MIR operands")
    cache_start = source.find("static int jube_host_mir_local_direct_call_prototype_get_or_create(")
    cache_end = source.find("static int jube_host_mir_function_state_suspend(", cache_start)
    if cache_start < 0 or cache_end < 0 or \
            "jube_host_mir_cursor_track_prototype(" not in source[cache_start:cache_end]:
        fail("Jube host does not record local direct-call prototype ownership")


def check_hosted_compiler_module_handles_are_owner_checked() -> None:
    source = text(JUBE_REGISTRY)
    for token in ("jube_host_mir_module_register",
                  "jube_host_mir_module_from_handle",
                  "jube_host_mir_module_unregister",
                  "jube_host_mir_modules_invalidate_context"):
        if token not in source:
            fail(f"Jube host does not manage opaque compiler modules via {token}")
    create_start = source.find("static void* jube_host_mir_module_create(")
    create_end = source.find("static int jube_host_mir_module_finalize_and_load(",
                             create_start)
    if create_start < 0 or create_end < 0 or \
            "jube_host_mir_module_register(" not in source[create_start:create_end]:
        fail("Jube host does not register opaque compiler module handles")
    finalize_start = source.find("static int jube_host_mir_module_finalize_and_load(")
    finalize_end = source.find("static void* jube_host_mir_function_lookup(", finalize_start)
    finalize = source[finalize_start:finalize_end]
    lookup = finalize.find("jube_host_mir_module_from_handle(")
    finish = finalize.find("MIR_finish_module(")
    if finalize_start < 0 or finalize_end < 0 or lookup < 0 or finish < 0 or lookup > finish:
        fail("Jube host does not validate compiler module ownership before finalization")
    if "jube_host_mir_module_unregister(mir_module);" not in finalize:
        fail("Jube host does not consume finalized compiler module handles")


def check_python_lowering_has_no_raw_memory_or_call_construction() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ("MIR_new_mem_op(", "MIR_new_call_insn(", "MIR_new_insn_arr("):
        if token in source:
            fail(f"Python compiler retains raw MIR memory/call construction via {token}")


def check_python_lowering_has_no_raw_instruction_construction() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ("MIR_new_insn(", "MIR_new_ret_insn(", "MIR_new_label("):
        if token in source:
            fail(f"Python compiler retains raw MIR instruction construction via {token}")


def check_python_lowering_has_no_raw_call_operand_construction() -> None:
    source = text(PYTHON_MIR_LOWERING)
    for token in ("MIR_new_reg_op(", "MIR_new_int_op(", "MIR_new_double_op("):
        if token in source:
            fail(f"Python compiler retains raw MIR call operand construction via {token}")


def check_dynamic_module_has_no_retired_host_imports() -> None:
    candidates = (
        PYTHON_MODULE_DIR / "lang-python.dylib",
        PYTHON_MODULE_DIR / "lang-python.so",
        PYTHON_MODULE_DIR / "lang-python.dll",
    )
    module = next((path for path in candidates if path.exists()), None)
    if module is None:
        if "--require-module-binary" in sys.argv:
            fail("lang-python module binary is missing")
        return
    try:
        output = subprocess.check_output(["nm", "-u", str(module)], text=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        fail(f"could not inspect lang-python module imports: {exc}")
    forbidden = (
        "_module_get",
        "_module_is_loading",
        "_module_register",
        "_module_build_lambda_namespace",
        "_load_script",
        "_read_text_file",
        "_import_resolver",
        "_jit_init",
        "_MIR_finish",
        "_MIR_finish_module",
        "_MIR_finish_func",
        "_MIR_load_module",
        "_MIR_new_module",
        "_MIR_new_func_arr",
        "_MIR_new_forward",
        "_MIR_reg",
        "_find_func",
        "_mir_guest_finish_context",
        "_jit_import_get_metadata",
        "_lambda_rt",
        "_lambda_root_frame_begin",
        "_lambda_root_frame_end",
        "_lambda_root_frame_take_slot",
        "_lambda_root_frame_overflow_error",
        "_context",
        "_heap_calloc_closure_env",
        "_owned_item_slot_store",
        "_owned_item_slot_read",
        "_gc_is_managed",
        "_gc_get_header",
    )
    for symbol in forbidden:
        if re.search(rf"(?:^|\s){re.escape(symbol)}$", output, re.MULTILINE):
            fail(f"lang-python module retains retired host import {symbol}")
    for line in output.splitlines():
        symbol = line.split()[-1] if line.split() else ""
        if symbol.startswith("_js_"):
            fail(f"lang-python module retains JavaScript runtime import {symbol}")


def check_dynamic_manifest_matches_host_build() -> None:
    manifest_path = PYTHON_MODULE_DIR / "module.json"
    if not manifest_path.exists():
        return
    match = re.search(r'^#define\s+JUBE_HOST_BUILD_ID\s+"([^"]+)"$', text(JUBE_HEADER), re.MULTILINE)
    if not match:
        fail("lambda/jube/jube.h does not define JUBE_HOST_BUILD_ID")
    try:
        import json
        manifest = json.loads(text(manifest_path))
    except (OSError, ValueError) as exc:
        fail(f"could not parse modules/lang-python/module.json: {exc}")
    if manifest.get("host_build_id") != match.group(1):
        fail("lang-python manifest host_build_id is stale relative to Jube host ABI")


def main() -> None:
    if "--inventory" in sys.argv:
        write_inventory()
        return
    check_python_has_no_js_runtime_calls()
    check_python_uses_neutral_data_membrane()
    check_python_uses_opaque_hosted_roots()
    check_main_has_no_python_branch()
    check_core_jit_catalog_has_no_python_ownership()
    check_core_importer_has_no_python_loader()
    check_shared_ast_has_no_python_profile()
    check_python_goldens_are_complete()
    check_no_second_jube_runtime_target()
    check_python_adapter_uses_hosted_services()
    check_python_frontend_has_no_monolithic_transpiler_header()
    check_python_mir_dump_uses_hosted_service()
    check_python_mir_lowering_uses_hosted_name_service()
    check_python_runtime_catalog_uses_jube_api()
    check_python_import_lowering_uses_module_graph()
    check_python_literal_moves_use_hosted_instruction_service()
    check_python_truth_branches_use_hosted_instruction_service()
    check_python_labels_use_hosted_emission_service()
    check_python_i64_operations_use_hosted_instruction_service()
    check_python_numeric_lowering_uses_hosted_instruction_service()
    check_python_runtime_calls_use_hosted_call_service()
    check_python_local_direct_calls_use_hosted_call_service()
    check_python_item_returns_use_hosted_return_service()
    check_python_lowering_has_no_raw_instruction_decoder()
    check_python_frame_finalization_uses_hosted_service()
    check_python_reference_moves_use_hosted_instruction_service()
    check_python_root_candidates_use_hosted_service()
    check_python_function_frames_use_hosted_services()
    check_python_import_cache_uses_hosted_services()
    check_python_function_selection_uses_hosted_services()
    check_python_current_lowering_uses_hosted_cursor_services()
    check_python_cursor_construction_uses_hosted_services()
    check_python_scalar_homes_use_hosted_services()
    check_python_compiler_cursor_is_opaque()
    check_python_compilation_artifacts_are_opaque()
    check_python_lowering_has_no_private_mir_types()
    check_hosted_compiler_cursor_rejects_stale_handles()
    check_hosted_compiler_context_invalidates_cursors()
    check_hosted_compiler_function_handles_are_owner_checked()
    check_hosted_compiler_state_tokens_are_opaque()
    check_hosted_compiler_labels_are_owner_checked()
    check_hosted_compiler_direct_call_handles_are_owner_checked()
    check_hosted_compiler_module_handles_are_owner_checked()
    check_python_lowering_has_no_raw_memory_or_call_construction()
    check_python_lowering_has_no_raw_instruction_construction()
    check_python_lowering_has_no_raw_call_operand_construction()
    check_dynamic_module_has_no_retired_host_imports()
    check_dynamic_manifest_matches_host_build()
    print("HOSTED_PY_ARCH: passed")


if __name__ == "__main__":
    main()
