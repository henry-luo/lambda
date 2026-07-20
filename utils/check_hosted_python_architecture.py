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
CORE_JIT_CATALOG = ROOT / "lambda" / "sys_func_registry.c"
CORE_IMPORTER = ROOT / "lambda" / "build_ast.cpp"
CORE_TRANSPILE_HEADER = ROOT / "lambda" / "transpiler.hpp"
CORE_AST = ROOT / "lambda" / "ast-core.hpp"
BUILD_CONFIG = ROOT / "build_lambda_config.json"
PYTHON_JUBE_ADAPTER = PYTHON_DIR / "python_jube_module.cpp"
PYTHON_MIR_LOWERING = PYTHON_DIR / "transpile_py_mir.cpp"
PYTHON_RUNTIME_IMPORTS = PYTHON_DIR / "python_runtime_imports.cpp"
PYTHON_MODULE_DIR = ROOT / "modules" / "lang-python"
JUBE_HEADER = ROOT / "lambda" / "jube" / "jube.h"


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
            fail(f"lambda/sys_func_registry.c retains Python-owned JIT import token {token}")


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
            fail(f"lambda/ast-core.hpp retains Python-specific profile token {token}")


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
        "_find_func",
        "_mir_guest_finish_context",
        "_jit_import_get_metadata",
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
    check_main_has_no_python_branch()
    check_core_jit_catalog_has_no_python_ownership()
    check_core_importer_has_no_python_loader()
    check_shared_ast_has_no_python_profile()
    check_python_goldens_are_complete()
    check_no_second_jube_runtime_target()
    check_python_adapter_uses_hosted_services()
    check_python_runtime_catalog_uses_jube_api()
    check_python_import_lowering_uses_module_graph()
    check_dynamic_module_has_no_retired_host_imports()
    check_dynamic_manifest_matches_host_build()
    print("HOSTED_PY_ARCH: passed")


if __name__ == "__main__":
    main()
