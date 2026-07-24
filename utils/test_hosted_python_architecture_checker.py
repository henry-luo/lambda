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
static PmCompilerRegister pm_load_side_stack_runtime(
MIR_new_mem_op
static void pm_begin_function_frame(
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: lowering},
                 checker.check_python_import_lowering_uses_module_graph,
                 "raw frame-runtime memory load")
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: '"../runtime/mir_dump.h"'},
                 checker.check_python_mir_dump_uses_hosted_service,
                 "hosted MIR debug service")
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: "heap_create_name("},
                 checker.check_python_mir_lowering_uses_hosted_name_service,
                 "hosted name service")
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: '"../runtime/transpiler.hpp"'},
                 checker.check_python_mir_lowering_uses_hosted_name_service,
                 "hosted name service")
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
static PmCompilerRegister pm_load_side_stack_runtime(
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: label_lowering},
                 checker.check_python_labels_use_hosted_emission_service,
                 "raw label emission")
    i64_lowering = """\
static void pm_emit_i64_operation(
pm_emit_hosted_instruction
MIR_new_insn(
static void pm_emit_item_return_operand(
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: i64_lowering},
                 checker.check_python_i64_operations_use_hosted_instruction_service,
                 "raw integer-operation MIR construction")
    numeric_lowering = """\\
static PmCompilerRegister pm_box_int_reg(
pm_emit_i64_operation
pm_emit_f64_operation
pm_emit_i64_register_move
pm_emit_branch_true
pm_emit_branch_false
pm_emit_jump
MIR_new_insn(
// ============================================================================
// TCO helpers
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: numeric_lowering},
                 checker.check_python_numeric_lowering_uses_hosted_instruction_service,
                 "native numeric lowering retains raw MIR construction")
    call_lowering = """\\
static PmCompilerRegister pm_emit_hosted_runtime_call_operands(
mir_runtime_import_call_emit
em_call_1(
struct PmArgScope
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: call_lowering},
                 checker.check_python_runtime_calls_use_hosted_call_service,
                 "shared raw call emission")
    direct_call_lowering = """\\
static void pm_emit_local_direct_call(
mir_local_direct_call_emit
MIR_new_insn_arr(
static void pm_emit_label(
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: direct_call_lowering},
                 checker.check_python_local_direct_calls_use_hosted_call_service,
                 "raw local-direct-call emission")
    return_lowering = """\\
static void pm_emit_item_return_operand(
mir_item_return_emit
MIR_new_ret_insn(
static void pm_emit_local_direct_call(
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: return_lowering},
                 checker.check_python_item_returns_use_hosted_return_service,
                 "raw Item-return emission")
    raw_instruction_decoder = """\\
static void pm_emit(PyMirTranspiler* mt, MIR_insn_t insn)
em_emit_insn(&mt->em, insn)
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: raw_instruction_decoder},
                 checker.check_python_lowering_has_no_raw_instruction_decoder,
                 "raw MIR instruction decoder")
    frame_lowering = """\\
static void pm_finish_function_frame(
mir_function_frame_finalize
em_finalize_scalar_homes
// Call helpers
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: frame_lowering},
                 checker.check_python_frame_finalization_uses_hosted_service,
                 "raw frame finalization")
    reference_move_lowering = """\\
static void pm_emit_i64_reference_move(
JUBE_COMPILER_INSN_MOVE_I64_REFERENCE
pm_emit_hosted_instruction
MIR_new_ref_op(
static void pm_emit_f64_operation(
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: reference_move_lowering},
                 checker.check_python_reference_moves_use_hosted_instruction_service,
                 "raw opaque-reference move emission")
    root_candidate_lowering = """\\
mir_compiler_cursor_create
pm_root_call_value(
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: root_candidate_lowering},
                 checker.check_python_root_candidates_use_hosted_service,
                 "direct root-candidate callback")
    frame_adapter_lowering = """\\
static void pm_begin_function_frame(
mir_function_frame_begin
mir_function_frame_scalar_return_home_set
mir_function_frame_finalize
mt->em.frame
// ============================================================================
// Call helpers
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: frame_adapter_lowering},
                 checker.check_python_function_frames_use_hosted_services,
                 "direct MirEmitter frame-layout access")
    import_cache_lowering = """\\
mir_compiler_import_cache_init
mir_compiler_import_cache_destroy
mir_local_direct_call_prototype_get_or_create
mt->em.import_cache
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: import_cache_lowering},
                 checker.check_python_import_cache_uses_hosted_services,
                 "direct import-cache ownership")
    function_selection_lowering = """\\
static void pm_select_hosted_function(
mir_function_state_suspend
mir_function_select
mir_function_state_restore
mt->em.func_item =
static bool pm_require_capacity(
mir_function_register_lookup_current
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: function_selection_lowering},
                 checker.check_python_function_selection_uses_hosted_services,
                 "direct function-state ownership")
    current_lowering = """\\
static PmCompilerRegister pm_new_reg(
mir_function_register_create_current
mir_label_create_current
mir_instruction_emit_current
mir_label_emit_current
mir_function_frame_runtime_load_current
mt->em.ctx
static void pm_begin_function_frame(
mir_function_finish_current
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: current_lowering},
                 checker.check_python_current_lowering_uses_hosted_cursor_services,
                 "direct current compiler state")
    cursor_construction_lowering = """\\
mir_item_function_create_typed_current
mir_function_forward_create_current
mir_module_finalize_and_load_current
mir_function_lookup_current
pm_create_hosted_item_function_typed(mt->em.ctx
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: cursor_construction_lowering},
                 checker.check_python_cursor_construction_uses_hosted_services,
                 "direct cursor construction")
    scalar_home_lowering = """\\
mir_scalar_home_create_current
mir_scalar_home_bind_current
em_scalar_home_new(&mt->em)
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: scalar_home_lowering},
                 checker.check_python_scalar_homes_use_hosted_services,
                 "direct scalar-home ownership")
    opaque_cursor_lowering = """\\
mir_compiler_cursor_create
mir_compiler_cursor_destroy
compiler_cursor
MirEmitter em
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: opaque_cursor_lowering},
                 checker.check_python_compiler_cursor_is_opaque,
                 "concrete compiler cursor state")
    opaque_artifact_lowering = """\\
void* compiler_context
void* module
MIR_context_t
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: opaque_artifact_lowering},
                 checker.check_python_compilation_artifacts_are_opaque,
                 "concrete compilation artifact")
    private_mir_type_lowering = """\\
PmCompilerRegister
PmCompilerLabel
PmCompilerFunctionItem
PmCompilerFunction
MIR_reg_t
"""
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: private_mir_type_lowering},
                 checker.check_python_lowering_has_no_private_mir_types,
                 "private MIR type dependency")
    stale_cursor_registry = """\\
static void* jube_host_mir_cursor_register(
static MirEmitter* jube_host_mir_cursor_emitter(
static void jube_host_mir_cursor_unregister(
static void jube_host_mir_compiler_cursor_destroy(
jube_host_mir_cursor_unregister(compiler_cursor);
jube_host_mir_cursor_dispose(emitter);
static int jube_host_mir_next_service(
MirEmitter* emitter = (MirEmitter*)compiler_cursor;
"""
    with_sources(checker, {checker.JUBE_REGISTRY: stale_cursor_registry},
                 checker.check_hosted_compiler_cursor_rejects_stale_handles,
                 "raw compiler cursor pointer")
    release_first_registry = stale_cursor_registry.replace(
        "jube_host_mir_cursor_unregister(compiler_cursor);\njube_host_mir_cursor_dispose(emitter);",
        "jube_host_mir_cursor_dispose(emitter);\njube_host_mir_cursor_unregister(compiler_cursor);").replace(
        "MirEmitter* emitter = (MirEmitter*)compiler_cursor;\n", "")
    with_sources(checker, {checker.JUBE_REGISTRY: release_first_registry},
                 checker.check_hosted_compiler_cursor_rejects_stale_handles,
                 "invalidate compiler cursor")
    context_first_registry = """\\
static void jube_host_mir_cursor_invalidate_context(
static void jube_host_mir_context_destroy(
MIR_finish(
jube_host_mir_cursor_invalidate_context(mir_context);
static void* jube_host_mir_module_create(
"""
    with_sources(checker, {checker.JUBE_REGISTRY: context_first_registry},
                 checker.check_hosted_compiler_context_invalidates_cursors,
                 "invalidate compiler cursors before MIR context release")
    unowned_function_registry = """\\
static bool jube_host_mir_cursor_track_function(
static bool jube_host_mir_cursor_owns_function(
static int jube_host_mir_function_select(
MIR_get_item_func(
jube_host_mir_cursor_owns_function(
static int jube_host_mir_function_state_restore(
static int jube_host_mir_item_function_create_typed_current(
static int jube_host_mir_function_forward_create_current(
"""
    with_sources(checker, {checker.JUBE_REGISTRY: unowned_function_registry},
                 checker.check_hosted_compiler_function_handles_are_owner_checked,
                 "function ownership before MIR lookup")
    raw_state_registry = """\\
struct JubeMirFunctionStateToken {};
static void* jube_host_mir_state_token_register(
static JubeMirStateTokenEntry* jube_host_mir_state_token_find(
static void jube_host_mir_state_tokens_discard_slot(
static int jube_host_mir_function_state_suspend(
jube_host_mir_state_token_register(compiler_cursor, state)
static int jube_host_mir_function_select(
static int jube_host_mir_function_state_restore(
JubeMirFunctionStateToken* state = (JubeMirFunctionStateToken*)state_token;
static int jube_host_mir_function_register_lookup_current(
static void jube_host_mir_compiler_cursor_destroy(
jube_host_mir_state_tokens_discard_slot(
static void jube_host_mir_debug_dump_if_enabled(
"""
    with_sources(checker, {checker.JUBE_REGISTRY: raw_state_registry},
                 checker.check_hosted_compiler_state_tokens_are_opaque,
                 "raw compiler state token")
    unowned_label_registry = """\\
static bool jube_host_mir_cursor_track_label(
static bool jube_host_mir_cursor_owns_label(
static int jube_host_mir_label_create_current(
jube_host_mir_cursor_track_label(
static int jube_host_mir_instruction_emit_current(
static int jube_host_mir_label_emit_current(
jube_host_mir_label_emit(
jube_host_mir_cursor_owns_label(
static int jube_host_mir_function_frame_runtime_load_current(
"""
    with_sources(checker, {checker.JUBE_REGISTRY: unowned_label_registry},
                 checker.check_hosted_compiler_labels_are_owner_checked,
                 "label ownership before emission")
    unowned_direct_call_registry = """\\
static bool jube_host_mir_cursor_track_prototype(
static bool jube_host_mir_cursor_owns_prototype(
static bool jube_host_mir_cursor_owns_function_item(
static int jube_host_mir_local_direct_call_emit(
MIR_new_ref_op(
jube_host_mir_cursor_owns_prototype(
jube_host_mir_cursor_owns_function_item(
static int jube_host_mir_item_return_emit(
static int jube_host_mir_local_direct_call_prototype_get_or_create(
jube_host_mir_cursor_track_prototype(
static int jube_host_mir_function_state_suspend(
"""
    with_sources(checker, {checker.JUBE_REGISTRY: unowned_direct_call_registry},
                 checker.check_hosted_compiler_direct_call_handles_are_owner_checked,
                 "direct-call ownership before MIR operands")
    raw_module_registry = """\\
static void* jube_host_mir_module_register(
static MIR_module_t jube_host_mir_module_from_handle(
static void jube_host_mir_module_unregister(
static void jube_host_mir_modules_invalidate_context(
static void* jube_host_mir_module_create(
MIR_new_module(
static int jube_host_mir_module_finalize_and_load(
MIR_finish_module(
static void* jube_host_mir_function_lookup(
"""
    with_sources(checker, {checker.JUBE_REGISTRY: raw_module_registry},
                 checker.check_hosted_compiler_module_handles_are_owner_checked,
                 "register opaque compiler module handles")
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: "MIR_new_mem_op("},
                 checker.check_python_lowering_has_no_raw_memory_or_call_construction,
                 "raw MIR memory/call construction")
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: "MIR_new_insn("},
                 checker.check_python_lowering_has_no_raw_instruction_construction,
                 "raw MIR instruction construction")
    with_sources(checker, {checker.PYTHON_MIR_LOWERING: "MIR_new_reg_op("},
                 checker.check_python_lowering_has_no_raw_call_operand_construction,
                 "raw MIR call operand construction")
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
