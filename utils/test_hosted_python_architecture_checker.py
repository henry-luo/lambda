#!/usr/bin/env python3
"""Exercise the hosted-Python architecture checker with in-memory regressions."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parent.parent
CHECKER_PATH = ROOT / "utils" / "check_hosted_python_architecture.py"


def fail(message: str) -> None:
    print(f"HOSTED_PY_ARCH_SELFTEST: {message}", file=sys.stderr)
    raise SystemExit(1)


def load_checker():
    spec = importlib.util.spec_from_file_location("hosted_python_arch_checker", CHECKER_PATH)
    if spec is None or spec.loader is None:
        fail("could not load architecture checker")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expect_rejection(checker, check, expected: str) -> None:
    class Rejected(Exception):
        pass

    original_fail = checker.fail
    checker.fail = lambda message: (_ for _ in ()).throw(Rejected(message))
    try:
        check()
    except Rejected as error:
        if expected not in str(error):
            fail(f"rejection was not specific to {expected}: {error}")
    else:
        fail(f"checker accepted regression for {expected}")
    finally:
        checker.fail = original_fail


def with_sources(checker, replacements: dict[Path, str], check, expected: str) -> None:
    original_text = checker.text

    def patched_text(path: Path) -> str:
        return replacements.get(path, original_text(path))

    checker.text = patched_text
    try:
        expect_rejection(checker, check, expected)
    finally:
        checker.text = original_text


def main() -> int:
    checker = load_checker()
    with_sources(checker, {checker.MAIN: "transpile_py_to_mir"},
                 checker.check_main_has_no_python_branch, "transpile_py_to_mir")
    with_sources(checker, {checker.PYTHON_DIR / "py_runtime.cpp": "js_call_value"},
                 checker.check_python_has_no_js_runtime_calls, "js_call_value")
    lowering = """\
pm_loading_module_namespace
pm_load_lambda_module
jube_load_hosted_module
jube_load_language_module
// Load Python module for cross-language import
module_state
module_begin_loading
module_publish
static MIR_reg_t pm_load_side_stack_runtime(
MIR_new_mem_op
static void pm_emit_side_stack_overflow(
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: lowering},
                 checker.check_python_import_lowering_uses_module_graph,
                 "raw frame-runtime memory load")
    identity_lowering = lowering.replace("MIR_new_mem_op\n", "em_new_reg(\n")
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: identity_lowering},
                 checker.check_python_import_lowering_uses_module_graph,
                 "direct compiler identity allocation")
    label_lowering = lowering.replace("MIR_new_mem_op\n", "em_new_label(\n")
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: label_lowering},
                 checker.check_python_import_lowering_uses_module_graph,
                 "direct compiler identity allocation")
    literal_lowering = """\
static void pm_emit_i64_immediate(
pm_emit_hosted_instruction
MIR_new_insn(
static void pm_emit_f64_immediate(
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: literal_lowering},
                 checker.check_python_literal_moves_use_hosted_instruction_service,
                 "raw literal MIR construction")
    branch_lowering = """\
static void pm_emit_branch_true(
pm_emit_hosted_instruction
MIR_new_label_op(
static void pm_emit_branch_false(
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: branch_lowering},
                 checker.check_python_truth_branches_use_hosted_instruction_service,
                 "raw truth-branch MIR construction")
    label_lowering = """\
static void pm_emit_label(
mir_label_emit
em_emit_label(
static MIR_reg_t pm_load_side_stack_runtime(
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: label_lowering},
                 checker.check_python_labels_use_hosted_emission_service,
                 "raw label emission")
    manifest = """{
  \"host_build_id\": \"stale-host\"
}\n"""
    header = '#define JUBE_HOST_BUILD_ID "current-host"\n'
    with_sources(checker, {
        checker.PYTHON_MODULE_DIR / "module.json": manifest,
        checker.JUBE_HEADER: header,
    }, checker.check_dynamic_manifest_matches_host_build, "manifest host_build_id")
    print("HOSTED_PY_ARCH_SELFTEST: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
