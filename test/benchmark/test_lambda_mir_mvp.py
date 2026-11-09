#!/usr/bin/env python3
"""Run the MVP's admission, ownership, error and emission checks."""
import os
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import lambda_mir_mvp
from lambda_mir_mvp import GC_CASES, CASES, ROOT
from benchmark_provenance import sha256_file
from run_c2mir_benchmarks import C2M, ensure_c2m, strip_timing


GC_MODES = ({}, {"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"},
            {"LAMBDA_GC_FORCE_SEED": "1592594996", "LAMBDA_GC_FORCE_ONE_IN": "3",
             "LAMBDA_GC_POISON_FREED": "1"})

class LambdaMirMvpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = Path(os.environ.get("LAMBDA_TEST_EXE", ROOT / "lambda.exe")).resolve()
        cls.benchmark_root = Path(os.environ.get("LAMBDA_TEST_BENCHMARK_ROOT", ROOT / "test/benchmark")).resolve()
        (ROOT / "temp").mkdir(exist_ok=True)
        cls.scratch = tempfile.TemporaryDirectory(prefix="mir_mvp_", dir=ROOT / "temp")

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def script_source(self, source):
        benchmark_root = ROOT / "test/benchmark"
        return self.benchmark_root / source.relative_to(benchmark_root) if source.is_relative_to(benchmark_root) else source

    def run_script(self, source, overrides=None, logging=False):
        source = self.script_source(source)
        env = os.environ.copy()
        for key in tuple(env):
            if key.startswith(("LAMBDA_", "MIR_", "COW_EXEC_")):
                del env[key]
        env.update(LAMBDA_EXEC_BACKEND="jit", LAMBDA_MIR_COMPILER="mvp")
        env.pop("JS_MIR_INTERP", None)
        env.update(overrides or {})
        command = [str(self.exe), "run", str(source)]
        if not logging:
            command.append("--no-log")
        return subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)

    def check_golden(self, source, overrides=None):
        source = self.script_source(source)
        result = self.run_script(source, overrides)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(strip_timing(result.stdout), lambda_mir_mvp.lambda_golden_path(source).read_text().strip())

    def function_mir(self, source, name):
        dump = Path(self.scratch.name) / f"{source.stem}.mir"
        result = self.run_script(source, {"LAMBDA_MIR_DUMP_PATH": str(dump)}, logging=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        match = re.search(rf"mvp_\d+_{re.escape(name)}:\tfunc(.*?)\tendfunc", dump.read_text(), re.S)
        self.assertIsNotNone(match, f"missing MIR function {name}")
        return match.group(1)

    def test_benchmark_goldens(self):
        for suite, name in CASES:
            for compiler in ("mvp", "production"):
                with self.subTest(suite=suite, name=name, compiler=compiler):
                    self.check_golden(ROOT / "test/benchmark" / suite / f"{name}2.ls",
                                      {"LAMBDA_MIR_COMPILER": compiler})

    def test_complete_benchmark_manifest(self):
        self.assertEqual(len(CASES), len(set(CASES)))
        self.assertEqual(set(CASES), set(lambda_mir_mvp.REGISTERED_CASES))
        self.assertTrue(set(lambda_mir_mvp.COVERED_CASES).issubset(CASES))
        for suite, name in CASES:
            for suffix in ("", "2"):
                source = ROOT / "test/benchmark" / suite / f"{name}{suffix}.ls"
                self.assertTrue(source.is_file(), source)
                self.assertTrue(lambda_mir_mvp.lambda_golden_path(source).is_file(), source)

    def check_gc_fixtures(self, fixtures):
        fixtures = tuple(fixtures)
        for compiler in ("mvp", "production"):
            for mode in GC_MODES:
                for source in fixtures:
                    with self.subTest(compiler=compiler, mode=mode, fixture=source.stem):
                        self.check_golden(source, {**mode, "LAMBDA_MIR_COMPILER": compiler})

    def test_ndim_canonical_values(self):
        names = ("typed_array_ndim_index", "typed_array_ndim_literal",
                 "typed_array_ndim_dynamic", "typed_array_construction_exact", "type_counted_rank")
        self.check_gc_fixtures(ROOT / "test/lambda" / f"{name}.ls" for name in names)

    def test_named_declaration_snapshots(self):
        names = ("fn_local_named_snapshots", "fn_local_capture_modes", "fn_local_capture_iterations")
        self.check_gc_fixtures(ROOT / "test/lambda" / f"{name}.ls" for name in names)

    def test_file_output_values(self):
        names = ("proc/proc_pdf_file", "pdf/pdf_file_mvp", "pdf/output_pages", "pdf/output_text")
        self.check_gc_fixtures(ROOT / "test/lambda" / f"{name}.ls" for name in names)

    def test_output_destination_recovery(self):
        # S2.6.6v3: encoder failures rewind a producer before its replacement.
        names = ("pdf/output_streaming", "mvp/mir_mvp_output_recovery")
        self.check_gc_fixtures(ROOT / "test/lambda" / f"{name}.ls" for name in names)

    def test_boxed_ranges_and_live_borrows(self):
        names = ("mvp/mir_mvp_boxed_range_bounds", "mvp/mir_mvp_live_borrow_arguments",
                 "proc/cow_var_borrow", "proc/tune23_map_argument_order")
        self.check_gc_fixtures(ROOT / "test/lambda" / f"{name}.ls" for name in names)

    def test_eps_object_operations(self):
        names = ("machine", "raster", "fonts", "text")
        self.check_gc_fixtures(ROOT / "test/lambda/eps" / f"{name}.ls" for name in names)

    def test_native_view_definitions(self):
        names = ("view_template", "view_state", "view_attr_pattern", "error_view_outcomes",
                 "error_event_outcomes", "edit_bridge", "ui_controls", "ui_workspace")
        self.check_gc_fixtures(ROOT / "test/lambda" / f"{name}.ls" for name in names)

    def test_scalar_var_places(self):
        names = ("place_borrow_scalar_homes", "place_borrow_homes", "cow_var_typed_rebind",
                 "proc_nullable_native_wide_map_storage")
        self.check_gc_fixtures(ROOT / "test/lambda/proc" / f"{name}.ls" for name in names)

    def test_scoped_tasks(self):
        names = ("mvp/mir_mvp_task_values", "conc/start_wait", "conc/start_imported",
                 "conc/scope_join", "conc/scope_return", "conc/scope_loop_exit",
                 "conc/scope_escape", "conc/no_replay", "conc/cancel_parked",
                 "conc/scope_error_cancel", "conc/cancel_nested_cleanup",
                 "conc/proc_handler_after_wait")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_open_transaction_cleanup(self):
        names = ("proc/open_scope_exit", "proc/open_scope_unwind", "reference_crud")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_procedural_fault_handlers(self):
        names = ("proc/proc_local_system_fault", "proc/proc_local_equality_fault",
                 "conc/proc_handler_fault_after_wait", "proc/handler_results")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_boxed_numeric_literals(self):
        names = ("wide_int_native_arith", "complex", "math_science_fft",
                 "math_science_geometry", "numeric_fastpath_edges", "eq_total")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_module_value_ownership(self):
        self.check_gc_fixtures((ROOT / "test/lambda/mvp/mir_mvp_module_values.ls",))

    def test_range_member_domains(self):
        names = ("mir_mvp_range_domains", "mir_mvp_query_sources")
        self.check_gc_fixtures(ROOT / "test/lambda/mvp" / (name + ".ls") for name in names)

    def test_type_values_and_nullable_returns(self):
        # S11.4.12v2/D2.3.5: declarations bind types; optional scalar returns
        # keep their value category across native and public entries.
        names = ("grammar/types", "type_literal_alias", "mvp/mir_mvp_array_domains")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_constrained_type_predicates(self):
        # S11.4.6v4/S11.4.11 cover declaring scopes, recursive predicates,
        # Error answers, base-only annotations and filtered handlers.
        names = ("constrained_type", "constrained_type_base", "constrained_type_hoisted",
                 "constrained_type_predicate", "constrained_type_recursive",
                 "handler_convenience", "proc/handler_control")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_nominal_literal_values(self):
        names = ("object_default", "object_constraint", "object_field_admission",
                 "mvp/mir_mvp_nominal_values")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_nominal_methods(self):
        # S12.3.3v2/S12.3.4v2/S7.4.6v3: bound receiver snapshots, named
        # defaults and field publication agree across both generated ABIs.
        names = ("mvp/mir_mvp_bound_methods", "mvp/mir_mvp_local_type_methods",
                 "mvp/mir_mvp_nominal_method_alias", "mvp/mir_mvp_nested_type_methods",
                 "import_pub_types", "object", "object_nominal",
                 "object_method_receiver", "object_method_value", "object_inherit", "object_content",
                 "object_boxed_field_layout", "object_optional_presence", "object_method_named_args",
                 "object_update", "object_direct_access", "proc/object_method_write",
                 "proc/object_optional_method", "proc/object_mutation", "proc/object_direct_access",
                 "proc/object_boxed_field_write", "proc/object_open_instance",
                 "proc/proc_keyword_object_members", "proc/proc_object_counter")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_native_module_calls_and_scalar_joins(self):
        names = ("mvp/mir_mvp_module_builtins", "mvp/mir_mvp_scalar_joins",
                 "dom_derive_chardata", "dom_derive_traversal", "dom_derive_query", "dom_derive_forms",
                 "dom_derive_urlencode", "dom_edit", "dom_edit_protocol", "dom_edit_plan",
                 "dom_stylesheet", "dom_validate", "dom_local_fonts")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_inferred_record_fields(self):
        names = ("mvp/mir_mvp_inferred_record_fields", "mvp/mir_mvp_error_boundaries", "latex/test_latex_citeproc_errors",
                 "latex/test_latex_citeproc_data", "mathml_format")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_guarded_record_reads(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_guarded_record_reads.ls"
        self.check_gc_fixtures((source,
            ROOT / "test/lambda/proc/inferred_shape_guarded_read.ls",
            ROOT / "test/lambda/proc/guarded_store_null_slot_retag.ls"))
        for name in ("read_value", "read_after", "read_tail", "read_flag"):
            body = self.function_mir(source, name)
            self.assertIn("\tcall\tmap_shape_field_to_item", body)
            self.assertIn("\tcall\tfn_member_by_id", body)

    def test_guarded_scalar_bodies(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_guarded_scalar_bodies.ls"
        self.check_gc_fixtures((source,))
        body = self.function_mir(source, "float_summary")
        self.assertIn("\tdsub\t", body)
        self.assertIn("\tcall\tfn_sub", body)
        body = self.function_mir(source, "text_summary")
        self.assertIn("\tcall\tfn_len_s", body)
        self.assertIn("\tcall\tfn_len_checked", body)
        body = self.function_mir(source, "choose")
        self.assertNotIn("\tcall\tis_truthy", body)
        body = self.function_mir(source, "invalid_store")
        self.assertEqual(body.count("\tcall\tindex_assign_cow") + body.count("\tcall\tmap_set_cow_resolved"), 1)

    def test_open_record_stores(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_open_record_stores.ls"
        self.check_gc_fixtures((source, ROOT / "test/lambda/mvp/mir_mvp_open_write_contracts.ls"))
        body = self.function_mir(source, "bump")
        self.assertIn("\tcall\tmap_set_cow_resolved", body)
        self.assertNotIn("\tcall\tlambda_map_set_checked", body)
        body = self.function_mir(source, "named_copy")
        self.assertNotIn("\tcall\tmap_set_cow_resolved", body)

    def test_boxed_array_witness(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_boxed_array_witness.ls"
        self.check_gc_fixtures((source,))
        body = self.function_mir(source, "peek")
        self.assertEqual(body.count("\tcall\tfn_len_checked"), 1)
        self.assertIn("\tcall\tfn_index", body)
        native_length = body[:body.index("\tcall\tfn_len_checked")]
        self.assertRegex(native_length, r"\tor\t[^\n]+, 360287970189639680\n")

    def test_counted_admission_and_callable_pipe(self):
        # D4.3.3/S10.1.7v2: resizing invalidates counts; nested ~ is not free.
        cases = (("array_count_resized_binding", "error[E201]", "maximum allowed is 3"),
                 ("pipe_body_bound_current", "error[E212]", "pipe target is not callable"))
        for compiler in ("mvp", "production"):
            for mode in GC_MODES:
                for name, code, message in cases:
                    with self.subTest(compiler=compiler, mode=mode, fixture=name):
                        source = ROOT / "test/lambda/negative/runtime" / (name + ".ls")
                        result = self.run_script(source, {**mode, "LAMBDA_MIR_COMPILER": compiler})
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn(code, result.stdout + result.stderr)
                        self.assertIn(message, result.stdout + result.stderr)

    def test_decomposition_values(self):
        names = ("mvp/mir_mvp_decomposition", "mvp/mir_mvp_decomposition_proc",
                 "fn_local_decomposition", "interp_decompose")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_store_containment(self):
        names = ("mvp/mir_mvp_store_containment", "mvp/mir_mvp_copy_loops",
                 "mvp/mir_mvp_iterations", "mvp/mir_mvp_kernel_loops", "proc/ledger_containment")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_confined_capture_homes(self):
        names = ("mvp/mir_mvp_confined_capture_homes", "fn_local_confined_capture",
                 "fn_local_capture_modes", "fn_local_capture_iterations", "fn_local_capture_roots",
                 "fn_local_decomposition_capture", "proc/fn_local_decomposition_snapshot")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_variadic_calls(self):
        # S16.9.7/D7.4.1: direct, imported and boxed entries share rest slots;
        # self-tail calls evaluate new rest operands and preserve their effects.
        names = ("mvp/mir_mvp_variadic_values", "mvp/mir_mvp_variadic_modules",
                 "mvp/mir_mvp_variadic_recursion", "mvp/mir_mvp_variadic_admission",
                 "arrow_variadic", "interp_variadic",
                 "interp_variadic_promoted", "fn_local_capture_flow", "call_dynamic_apply",
                 "type_enforcement_dynamic_call", "func_param", "func_param2",
                 "dynamic_call_scalar_ownership")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_variadic_var_outcomes(self):
        # Full currently loses this var rebind and T0 excludes the call; retain
        # the S9.1.3 oracle and record those controls in the parity checkpoint.
        for mode in GC_MODES:
            with self.subTest(mode=mode):
                self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_variadic_exits.ls", mode)

    def test_binder_calls(self):
        # S11.4.8v2/D4.1.4: site joins, dependent admission, defaults and
        # empty nested selections retain the same contract on every call.
        names = ("type_binder", "type_binder_leading", "type_binder_raw_variants",
                 "proc/type_binder_proc_raw", "proc/type_binder_var_record_raw",
                 "type_param_error_short_circuit", "proc/proc_type_param_error_short_circuit",
                 "proc/generic_array_var_cow_home", "mvp/mir_mvp_binder_calls")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_binder_type_captures(self):
        # retain the declared oracle: Full/T0 already fail the captured type
        # cases under p11; their controls remain in the real parity reports.
        for mode in GC_MODES:
            with self.subTest(mode=mode):
                self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_binder_values.ls", mode)

    def test_binder_error_admission(self):
        # S11.4.3/S11.4.8v2: an admitted Error is data; an incompatible
        # repeated-site selection still rejects the invocation.
        for mode in GC_MODES:
            with self.subTest(mode=mode):
                self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_binder_outcomes.ls", mode)

    def test_admission_outcome_boundaries(self):
        # S7.4.4/S7.7.4v2/S11.4.6v4: diagnostics, implicit returns and
        # borrowed outcomes retain normal conversion and exact Error values.
        names = ("admission_runtime_type", "error_members", "type_none",
                 "proc/admission_borrowed_outcomes", "proc/admission_return_outcomes",
                 "proc/error_union_normal_admission")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_binder_default_order(self):
        # S11.4.8v2/TG18: defaults see admitted predecessors but do not
        # select a binder; the paired Full failure remains in the report.
        for mode in GC_MODES:
            with self.subTest(mode=mode):
                self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_binder_default_order.ls", mode)

    def test_member_contract_representations(self):
        # S11.1.2v4/D4.2.5: member contracts preserve boxed values,
        # independent type values, record reification and COW snapshots.
        names = ("pattern_annotation", "pattern_quoted_classes", "range_type_membership", "string_pattern_syntax",
                 "import_patterns", "type_syntax_edges", "ledger_verified_semantics",
                 "proc/ledger_lane_widening", "proc/cow_element_snapshot",
                 "proc/map_contract_reordered_literal", "number_model_c14c_div_mod",
                 "list_collapse_void", "navigation/paths", "navigation/surfaces")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)
        # Full still skips the known int-to-float conversion after a deferred
        # failed store. MVP and T0 preserve the S11.4.6v4 destination domain.
        for tier in ("jit", "interp"):
            for mode in GC_MODES:
                with self.subTest(tier=tier, mode=mode):
                    self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_member_contracts.ls",
                                      {**mode, "LAMBDA_EXEC_BACKEND": tier,
                                       "LAMBDA_MIR_COMPILER": "mvp" if tier == "jit" else "production"})

    def test_captured_dependent_annotations(self):
        # S11.4.8v2/D4.1.4: captured annotations and same-spelling aliases
        # resolve by declaration, independently of each activation's slots.
        for mode in GC_MODES:
            for name in ("mir_mvp_captured_annotations", "mir_mvp_captured_type_shadowing"):
                with self.subTest(mode=mode, fixture=name):
                    self.check_golden(ROOT / "test/lambda/mvp" / (name + ".ls"), mode)

    def test_method_lexical_environments(self):
        # S9.1.4v2/S12.3.3v2: each activation owns its method captures;
        # saved receivers and writable method places keep their COW boundary.
        for mode in GC_MODES:
            for name in ("mir_mvp_method_captures", "mir_mvp_method_capture_cow",
                         "mir_mvp_method_capture_annotations"):
                with self.subTest(mode=mode, fixture=name):
                    self.check_golden(ROOT / "test/lambda/mvp" / (name + ".ls"), mode)
            source = ROOT / "test/lambda/negative/semantic/fn_local_method_capture_escape.ls"
            with self.subTest(mode=mode, fixture=source.stem):
                result = self.run_script(source, mode)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(source.with_suffix(".txt").read_text().strip(), result.stderr)

    def test_query_join_streams(self):
        names = ("for_join_test", "for_join_s3b_test", "for_join_group_test",
                 "for_group_test", "mvp/mir_mvp_join_group_values")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_traversal_exits(self):
        # D6.1.1: key frames unwind on propagation, multi-source transfers
        # and projected var calls. Full's continue defect remains recorded.
        for mode in GC_MODES:
            with self.subTest(mode=mode):
                self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_traversal_exits.ls", mode)

    def test_functional_loop_results_and_nullable_places(self):
        # S2.5.3v3/S9.1.2: nested functional streams remain values, and a
        # nullable alias uses the ordinary checked COW write path.
        names = ("expr_stam", "proc/ledger_cow_value_boundaries")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_recovery_and_pointer_result_domains(self):
        # S7.6.1v9/D2.3.5: recovery can replace a native domain, and a
        # raw C pointer's zero result must box as absence.
        names = ("handler_current_error", "name_member", "symbol_empty")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_borrowed_outcome_and_coordinate_boundaries(self):
        # S7.2.2/S7.6.3v3/S9.3.1/D4.3.3: joins retain insertion capture,
        # arrays still admit their values, and split propagates its Error arm.
        names = ("proc/rejected_error_any_operand", "proc/ledger_place_insertion",
                 "proc/subscript_last_scope", "proc/tune23_array_consumer_refinement",
                 "proc/fn_local_proc_snapshot", "conc/select_basic",
                 "proc/proc_ndim_index_write", "proc/ndim_row_assign",
                 "mvp/mir_mvp_select_many")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)
        # Full still mutates the prior N-D snapshots; T0 excludes the nominal
        # projected write. Those controls remain in the paired diagnostic.
        for mode in GC_MODES:
            with self.subTest(mode=mode):
                self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_coordinate_places.ls", mode)

    def test_boundary_source_and_entry_roles(self):
        # S7.4.4/S12.1.1v3: admission owns its source point; a pure main
        # is an ordinary definition and inferred schemas add no admission.
        names = ("ledger_boundary_location", "ledger_parameter_result_site",
                 "grammar_reduce5_scanner", "mvp/mir_mvp_named_main_function",
                 "mvp/mir_mvp_inferred_array_contracts")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_native_subsystem_result_domains(self):
        # S8.3.1v3/S2.6.6v3/D4.3.1: virtual content needs semantic length,
        # and constructor/conditional Error outcomes stay boxed across imports.
        names = ("archive_input", "zip_jar", "zip_errors", "proc/zip_output",
                 "proc/element_prepared_parts", "proc/proc_pdf_maps",
                 "pdf/phase37_page_content_stream_data", "proc/archive_output",
                 "music/music_gc_roots", "scene3d/invalid", "procart/wfc_native",
                 "input_markdown_edit", "input_markdown_emoji")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_producer_carrier_transports(self):
        names = ("mvp/mir_mvp_producer_transport", "match_expr", "raise_non_error",
                 "proc/handler_results", "graph_d2_routing", "graph_transform_html",
                 "graph_transform_input", "input_markdown_mermaid", "procart/animal_surfaces")
        self.check_gc_fixtures(ROOT / "test/lambda" / f"{name}.ls" for name in names)

    def test_procedure_and_pointer_result_contracts(self):
        # S2.5.3v3/D2.3.5/D4.3.1: collapsed statements retain demand,
        # admitted unions select a lane, and open aliases may widen.
        names = ("proc/dense_for_range", "proc/tune23_array_consumer",
                 "proc/function_colour_poly", "proc/record_boundary_admission",
                 "proc/verified_contract_prefix", "mvp/mir_mvp_pointer_union_admission")
        self.check_gc_fixtures(ROOT / "test/lambda" / f"{name}.ls" for name in names)

    def test_extended_call_operands(self):
        # D4.2.1/S7.7.3: conversions and named system operands share checked
        # call transport; Error-only edges do not create a native value lane.
        names = ("agg_null_absence", "admission_call_outcomes",
                 "mvp/mir_mvp_numeric_type_calls", "mvp/mir_mvp_empty_normal_domains")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_imported_type_values(self):
        self.check_gc_fixtures((ROOT / "test/lambda/import_type_values.ls",))

    def test_javascript_exports(self):
        # D7.4.1/D6.2.3: retain the defining namespace and caller-owned scalar
        # homes through each physical bridge arity, including stack operands.
        names = ("import_js", "import_js_naming", "binary_js_bridge", "mvp/mir_mvp_js_exports")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_javascript_export_values(self):
        # Full/T0's E229 controls remain in the parity reports; MVP publishes
        # a Core boxed adapter instead of reinterpreting a JsFunction as code.
        for mode in GC_MODES:
            with self.subTest(mode=mode):
                self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_js_export_values.ls", mode)

    def test_ndim_reads_and_images(self):
        # S8.2.4v3/D6.2.3: coordinates and their receiver survive allocating
        # keys; image channels expose the same N-D runtime representation.
        names = ("mvp/mir_mvp_ndim_reads", "mvp/mir_mvp_ndim_effects", "ndim_any_receiver",
                 "image_composition", "image_edit", "image_gif_disposal", "image_import",
                 "image_paint", "image_psd", "image_raster")
        self.check_gc_fixtures(ROOT / "test/lambda" / (name + ".ls") for name in names)

    def test_content_list_and_spread_semantics(self):
        names = ("mir_mvp_content", "mir_mvp_root_content", "mir_mvp_elements",
                             "mir_mvp_type_membership", "mir_mvp_query_context", "mir_mvp_generic_operators",
                             "mir_mvp_patterns", "mir_mvp_query_windows", "mir_mvp_query_sources",
                             "mir_mvp_references", "mir_mvp_query_patterns_paths",
                             "mir_mvp_open_write_contracts", "mir_mvp_navigation", "mir_mvp_last_index",
                             "mir_mvp_closed_parameters", "mir_mvp_open_projected_borrows",
                             "mir_mvp_builtin_bool_outcomes", "mir_mvp_procedure_results",
                             "mir_mvp_provisos", "mir_mvp_scalar_var_lanes",
                             "mir_mvp_variadic_print", "mir_mvp_closed_arrays",
                             "mir_mvp_sequence_pipes", "mir_mvp_aggregate_pipes",
                             "mir_mvp_abstract_numbers", "mir_mvp_named_arguments",
                             "mir_mvp_vmap_places", "mir_mvp_parking_calls")
        self.check_gc_fixtures(ROOT / "test/lambda/mvp" / (name + ".ls") for name in names)

    def test_open_var_outcomes(self):
        # the Full control still mutates a simultaneous plain snapshot; keep the S9.1.3 oracle.
        for mode in GC_MODES:
            with self.subTest(mode=mode):
                self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_open_var.ls", mode)

    def test_query_window_scalar_ownership(self):
        # retain the cold floats when append growth moves their source buffer.
        for compiler in ("mvp", "production"):
            for seed in (1592594996, 17, 31, 42, 77, 101, 239, 1024):
                with self.subTest(compiler=compiler, seed=seed):
                    self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_query_windows.ls",
                                      {"LAMBDA_MIR_COMPILER": compiler,
                                       "LAMBDA_GC_FORCE_SEED": str(seed), "LAMBDA_GC_FORCE_ONE_IN": "3",
                                       "LAMBDA_GC_POISON_FREED": "1"})

    def test_dynamic_function_values(self):
        names = ("mir_mvp_dynamic_calls", "mir_mvp_named_values", "mir_mvp_builtin_values",
                             "mir_mvp_immutable_captures")
        self.check_gc_fixtures(ROOT / "test/lambda/mvp" / (name + ".ls") for name in names)

    def test_benchmark_expansion_semantics(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_benchmark_expansion.ls"
        modes = ({}, {"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"},
                 {"LAMBDA_GC_FORCE_SEED": "1592594996", "LAMBDA_GC_FORCE_ONE_IN": "3", "LAMBDA_GC_POISON_FREED": "1"},
                 {"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1", "COW_EXEC_PROFILE": "1",
                  "COW_EXEC_PROFILE_OUT": str(Path(self.scratch.name) / "capture-profile.tsv")})
        for compiler in ("mvp", "production"):
            for mode in modes:
                for fixture in (source, ROOT / "test/lambda/mvp/mir_mvp_u32.ls",
                                ROOT / "test/lambda/mvp/mir_mvp_index_recurrence.ls"):
                    with self.subTest(compiler=compiler, mode=mode, fixture=fixture.name):
                        self.check_golden(fixture, {**mode, "LAMBDA_MIR_COMPILER": compiler})
        for mode in modes:
            with self.subTest(membership_outcomes=mode):
                self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_membership_outcomes.ls", mode)
            with self.subTest(module_snapshot=mode):
                self.check_golden(ROOT / "test/lambda/mvp/mir_mvp_module_snapshots.ls", mode)

    def test_ownership_and_semantics(self):
        fixtures = [ROOT / "test/lambda/proc" / f"mir_mvp_{name}.ls" for name in ("scalar", "roots")]
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_errors.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_integer.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_native.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_arrays.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_float_arrays.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_shift.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_comparisons.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_tuning.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_extended.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_consumers.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_borrow_inline.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_iterations.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_kernel_loops.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_copy_loops.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_region_loops.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_recursive_arrays.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_string_forward.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_constructor_boundary.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_modular_update.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_flags.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_fn_local.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_records.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_array_domains.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_borrow_kernel.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_proven_domains.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_numeric_cast.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_float_compare.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_record_witness.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_string_arrays.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_predicate_branch.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_indirect_keys.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_scalar_fallback.ls")
        modes = ({}, {"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"},
                 {"LAMBDA_GC_FORCE_SEED": "1592594996", "LAMBDA_GC_FORCE_ONE_IN": "3", "LAMBDA_GC_POISON_FREED": "1"})
        for source in fixtures:
            for mode in modes:
                with self.subTest(source=source.name, mode=mode):
                    self.check_golden(source, mode)

    def test_array_benchmarks_under_gc(self):
        for suite, name in GC_CASES:
            with self.subTest(suite=suite, name=name):
                self.check_golden(ROOT / "test/benchmark" / suite / f"{name}2.ls",
                                  {"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"})

    def test_reference_generator(self):
        self.assertIsNone(ensure_c2m())
        source = Path(self.scratch.name) / "lcg.c"
        header = ROOT / "test/benchmark/c2mir/bench_lcg.h"
        # Check every input used by matmul (quicksort uses the first 5,000).
        source.write_text(f'#include "{header}"\nextern int printf(const char *, ...);\n'
                          'int main(void) { int seed = 42; int i; for (i = 0; i < 80000; i++) { '
                          'seed = bench_lcg_next(seed); printf("%d\\n", seed); } return 0; }\n')
        result = subprocess.run([str(C2M), str(source), "-eg"], cwd=ROOT,
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = []
        seed = 42
        for _ in range(80000):
            seed = (seed * 1664525 + 1013904223) % 1000000
            expected.append(seed)
        self.assertEqual([int(line) for line in result.stdout.splitlines()], expected)

    def test_reference_goldens(self):
        self.assertIsNone(ensure_c2m())
        for suite, name in CASES:
            with self.subTest(suite=suite, name=name):
                source = ROOT / "test/benchmark" / suite / "c2mir" / f"{name}.c"
                if not source.is_file():
                    continue  # Lambda coverage also includes suite aliases with no C port.
                result = subprocess.run([str(C2M), "-DC2MIR_KERNEL_TIMING", str(source), "-eg"],
                                        cwd=ROOT, capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                expected = source.parent.parent / f"{name}2.txt"
                self.assertEqual(strip_timing(result.stdout), expected.read_text().strip())

    def test_nonzero_fft(self):
        # Exercise the actual butterfly and swap code: the timed benchmark's zero input is a weak oracle.
        original = (ROOT / "test/benchmark/r7rs/fft2.ls").read_text()
        source = Path(self.scratch.name) / "fft_nonzero.ls"
        body = original[:original.index("pn benchmark()")].replace("pn four1(data:", "pn four1(var data:")
        source.write_text(body + '''pn main() {
    var a: float[] = [1.0, 0.0, 2.0, 0.0, 3.0, 0.0, 4.0, 0.0]
    let snapshot: float[] = a
    four1(a, 8)^
    var expected: float[] = [10.0, 0.0, -2.0, -2.0, -2.0, 0.0, -2.0, 2.0]
    var ok: bool = true
    for i in 0 to 7 { if (abs(a[i] - expected[i]) <= 0.000000001) { } else { ok = false } }
    print(ok); print(" "); print(snapshot[0]); print(" "); print(snapshot[2]); print("\\n")
}
''')
        source.with_suffix(".txt").write_text("true 1 2\n")
        for compiler in ("mvp", "production"):
            with self.subTest(compiler=compiler):
                self.check_golden(source, {"LAMBDA_MIR_COMPILER": compiler,
                    "LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"})

    def test_immutable_indirect_keys(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_indirect_keys.ls"
        for name in ("table_read", "nested_table"):
            body = self.function_mir(source, name)
            self.assertNotIn("\tcall\tcow_capture_value", body)
            self.assertIn("\tcall\tfn_index", body)  # invalid keys retain checked absence
        for name in ("mutable_table", "effectful_read"):
            self.assertIn("\tcall\tcow_capture_value", self.function_mir(source, name))
        lazy = self.function_mir(source, "lazy_condition")
        key = re.search(r"\tmov\t(%r[0-9a-f]+), arg1\n", lazy).group(1)
        key_read = re.search(r"\tmov\t%r[0-9a-f]+, " + re.escape(key) + r"\n", lazy)
        first_truth = re.search(r"\t(?:eq|beq|bne)\t[^\n]+, 1\n", lazy)
        self.assertIsNotNone(key_read)
        self.assertIsNotNone(first_truth)
        self.assertGreater(key_read.start(), first_truth.end())
        native = lazy[:lazy.index("\tcall\tfn_index")]
        self.assertRegex(native, r"\tb(?:eq|ne)\t[^\n]+, 1\n")
        self.assertNotRegex(native, r"\teq\t[^\n]+, 1\n")

    def test_confined_array_snapshots(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_confined_arrays.ls"
        for mode in ({}, {"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"},
                     {"LAMBDA_GC_FORCE_SEED": "1592594996", "LAMBDA_GC_FORCE_ONE_IN": "3",
                      "LAMBDA_GC_POISON_FREED": "1"}):
            with self.subTest(mode=mode):
                self.check_golden(source, mode)
                self.check_golden(ROOT / "test/benchmark/larceny/puzzle2.ls", mode)
        for name in ("local_sum", "sibling"):
            body = self.function_mir(source, name)
            self.assertIn("\talloca\t", body)
            self.assertIn("\tcall\tlambda_array_scalar_snapshot", body)
        prefix = self.function_mir(source, "sibling").split("\tcall\tlambda_array_scalar_snapshot", 1)[0]
        for argument in ("arg1", "arg2"):
            self.assertRegex(prefix, rf"\tmov\t[^,\n]+, {argument}\n")
        self.assertRegex(prefix, r"\tmov\ti64:\d+\(runtime\), 0\n")
        body = self.function_mir(source, "call_sibling")
        self.assertIn("\tcall\tlambda_array_scalar_snapshot", body)
        recursive = re.search(r"\tcall\tmvp_\d+_sibling_dp", body)
        self.assertIsNotNone(recursive)
        self.assertLess(body.index("\tcall\tlambda_array_scalar_snapshot"), recursive.start())
        for name in ("retained", "keyed_escape"):
            self.assertNotIn("lambda_array_scalar_snapshot", self.function_mir(source, name))
        body = self.function_mir(ROOT / "test/benchmark/larceny/puzzle2.ls", "solve")
        self.assertEqual(body.count("\tcall\tlambda_array_scalar_snapshot"), 3)
        self.assertNotIn("cow_capture_value", body)

    def test_snapshot_loop_lifetime(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_snapshot_loop.ls"
        for mode in ({}, {"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"},
                     {"LAMBDA_GC_FORCE_SEED": "1592594996", "LAMBDA_GC_FORCE_ONE_IN": "3",
                      "LAMBDA_GC_POISON_FREED": "1"}):
            with self.subTest(mode=mode):
                self.check_golden(source, mode)
        body = self.function_mir(source, "repeat")
        self.assertIn("\tcall\tlambda_array_scalar_snapshot", body)
        # every reservation precedes the loop condition and its back edge.
        last_alloca = body.rindex("\talloca\t")
        self.assertLess(last_alloca, body.index("\tcall\tlambda_array_scalar_snapshot"))
        branches = [m for m in re.finditer(r"\tb(?:ge|gt|le|lt)\t", body)]
        self.assertTrue(branches)
        self.assertLess(last_alloca, branches[0].start())

    def test_propagation_outcomes(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_propagation.ls"
        for mode in ({}, {"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"},
                     {"LAMBDA_GC_FORCE_SEED": "1592594996", "LAMBDA_GC_FORCE_ONE_IN": "3",
                      "LAMBDA_GC_POISON_FREED": "1"}):
            with self.subTest(mode=mode):
                self.check_golden(source, mode)
        body = self.function_mir(source, "plus_one")
        self.assertIn("i64, i64, p:runtime", body)
        self.assertNotIn("\tcall\tint2it_lane", body)
        self.assertNotIn("\tcall\tint2it_lane", self.function_mir(source, "fused"))
        native = self.function_mir(source, "float_update").split("\tcall\tfn_index", 1)[0]
        self.assertIn("\tdmul\t", native)
        self.assertNotIn("lambda_type_check", native)
        body = self.function_mir(source, "repeated_float")
        # accurate numeric effects permit the checked leaf to inline as well;
        # the first checked reader still separates it from the guarded loop.
        self.assertNotRegex(body, r"\tcall\tmvp_\d+_float_update_dp")
        self.assertIn("\tcall\tfn_index", body)
        native = body.split("\tcall\tfn_index", 1)[0]
        self.assertIn("\tdmul\t", native)
        self.assertNotIn("lambda_type_check", native)

    def test_string_outcome_appends(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_string_outcomes.ls"
        for mode in ({}, {"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"},
                     {"LAMBDA_GC_FORCE_SEED": "1592594996", "LAMBDA_GC_FORCE_ONE_IN": "3",
                      "LAMBDA_GC_POISON_FREED": "1"}):
            with self.subTest(mode=mode):
                self.check_golden(source, mode)
        body = self.function_mir(source, "repeat_text")
        self.assertIn("\tcall\tfn_strcat3", body)
        self.assertIn("\tcall\tfn_join", body)

    def test_failed_engine_remains_unqualified(self):
        executable = Path(self.scratch.name) / "failed.exe"
        executable.write_text("#!/bin/sh\nexit 17\n")
        executable.chmod(0o755)
        report = Path(self.scratch.name) / "failed.json"
        manifest = Path(self.scratch.name) / "source.json"
        identity = {"fixture": "archived identity"}
        manifest.write_text(json.dumps({"binary_sha256": sha256_file(executable), "compiler_identity": identity}))
        self.assertIsNone(ensure_c2m())
        reference = Path(self.scratch.name) / "reference.exe"
        reference.write_bytes(C2M.read_bytes())
        reference.chmod(0o755)
        corpus = Path(self.scratch.name) / "benchmarks"
        (corpus / "r7rs/c2mir").mkdir(parents=True)
        for name in ("fib2.ls", "fib2.txt", "c2mir/fib.c"):
            shutil.copy2(ROOT / "test/benchmark/r7rs" / name, corpus / "r7rs" / name)
        shutil.copytree(ROOT / "test/benchmark/c2mir", corpus / "c2mir")
        command = [sys.executable, str(ROOT / "test/benchmark/lambda_mir_mvp.py"),
            "--release", "--exe", str(executable), "--record-failures", "--case", "r7rs/fib",
            "--runs", "3", "--warmups", "1", "--output", str(report), "--source-manifest", str(manifest),
            "--c2mir-exe", str(reference), "--benchmark-root", str(corpus)]
        result = subprocess.run(command,
            cwd=ROOT, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        data = json.loads(report.read_text())
        self.assertEqual(data["compiler_identity"], identity)
        self.assertIn("observed_checkout_identity", data)
        self.assertEqual(data["executables"]["c2mir"]["sha256"], sha256_file(reference))
        self.assertEqual(data["benchmark_root"], str(corpus))
        row = data["cases"][0]
        self.assertEqual(row["commands"]["mvp"][2], str(corpus / "r7rs/fib2.ls"))
        self.assertEqual(row["commands"]["c2mir"][0], str(reference))
        self.assertEqual(set(row["engine_failures"]), {"mvp", "production"})
        self.assertIn("exit 17", row["engine_failures"]["production"]["error"])
        self.assertIsNone(row["median_ms"]["mvp"])
        self.assertIsNone(row["performance_gates"]["faster_than_full_mir"])
        self.assertIsNone(row["performance_gates"]["within_c2mir_allowance"])
        self.assertEqual(len(row["samples_ms"]["c2mir"]), 3)
        self.assertTrue(all(order == ["c2mir"] for order in row["execution_order"]))
        manifest.write_text(json.dumps({"binary_sha256": "0" * 64, "compiler_identity": identity}))
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 2)
        self.assertIn("source manifest does not identify", result.stderr)

    def test_batched_reference_executable(self):
        reference = Path(self.scratch.name) / "batch-reference.exe"
        reference.write_text("mock reference executable\n")
        report = Path(self.scratch.name) / "batch-reference.json"

        def invoke(command, env):
            if command[1] == "run":
                source = Path(command[2])
                expected = source.with_suffix(".txt")
            else:
                self.assertEqual(command[0], str(reference))
                source = Path(command[-2])
                expected = source.parent.parent / (source.stem + "2.txt")
            return expected.read_text().strip() + "\n__TIMING__:1.0\n"

        command = ["lambda_mir_mvp.py", "--release", "--exe", str(self.exe),
            "--subset", "float-array", "--runs", "3", "--warmups", "1",
            "--c2mir-exe", str(reference), "--output", str(report)]
        with mock.patch.object(sys, "argv", command), mock.patch.object(lambda_mir_mvp, "invoke", invoke), \
                mock.patch("builtins.print"):
            lambda_mir_mvp.main()
        rows = json.loads(report.read_text())["cases"]
        self.assertEqual(len(rows), 2)
        for row in rows:
            self.assertEqual(row["commands"]["c2mir"][0], str(reference))
            self.assertEqual(len(row["samples_ms"]["c2mir"]), 3)

    def test_unported_reference_retains_lambda_case(self):
        reference = Path(self.scratch.name) / "unported-reference.exe"
        reference.write_bytes(b"fixture reference identity")
        report = Path(self.scratch.name) / "unported.json"
        arguments = ["lambda_mir_mvp.py", "--release", "--exe", str(self.exe), "--c2mir-exe", str(reference),
                     "--case", "jetstream/richards", "--runs", "3", "--warmups", "1", "--output", str(report)]
        with mock.patch.object(sys, "argv", arguments), mock.patch.object(lambda_mir_mvp, "invoke",
                return_value="richards: PASS\n__TIMING__:1.0\n"):
            lambda_mir_mvp.main()
        row = json.loads(report.read_text())["cases"][0]
        self.assertEqual(set(row["commands"]), {"mvp", "production"})
        self.assertEqual(len(row["samples_ms"]["mvp"]), 3)
        self.assertEqual(len(row["samples_ms"]["production"]), 3)
        self.assertIsNone(row["median_ms"]["c2mir"])
        self.assertIsNone(row["performance_gates"]["within_c2mir_allowance"])

    def test_mvp_regression_only_retains_pairs(self):
        reference = Path(self.scratch.name) / "regression-reference"
        output = Path(self.scratch.name) / "mvp-regression.json"
        for mode in ("--regression-only", "--lambda-only"):
            with self.subTest(mode=mode):
                arguments = ["lambda_mir_mvp.py", "--release", mode, "--exe", str(self.exe),
                    "--previous-mvp", str(self.exe), "--c2mir-exe", str(reference), "--case", "r7rs/fib",
                    "--runs", "3", "--warmups", "1", "--output", str(output)]
                with mock.patch.object(sys, "argv", arguments), mock.patch.object(lambda_mir_mvp, "invoke",
                        return_value="fib: PASS\n__TIMING__:1.0\n") as invoke:
                    lambda_mir_mvp.main()
                row = json.loads(output.read_text())["cases"][0]
                commands = {"mvp", "previous_mvp"}
                if mode == "--lambda-only":
                    commands.add("production")
                self.assertEqual(invoke.call_count, 4 * len(commands))
                self.assertEqual(set(row["commands"]), commands)
                for engine in commands:
                    self.assertEqual(len(row["samples_ms"][engine]), 3)
                if mode == "--regression-only":
                    self.assertEqual(row["samples_ms"]["production"], [])
                self.assertEqual(row["samples_ms"]["c2mir"], [])
                self.assertFalse(row["engine_failures"])
                self.assertIn("c2mir", row["engine_unavailable"])

    def test_typed_untyped_variants_use_their_own_oracles(self):
        corpus = Path(self.scratch.name) / "variant-corpus"
        sources = corpus / "r7rs"
        sources.mkdir(parents=True)
        for name, text in (("fib", "untyped stdout"), ("fib2", "typed stdout")):
            (sources / (name + ".ls")).write_text(f'pn main() {{ print("{text}") }}\n')
            (sources / (name + ".txt")).write_text("reference oracle\n")
            (sources / (name + ".formatted.txt")).write_text(text + "\n")
        output = Path(self.scratch.name) / "variants.json"
        seen = set()

        def invoke(command, env):
            self.assertNotIn("COW_EXEC_PROFILE", env)
            source = Path(command[2])
            seen.add(source.name)
            text = "untyped stdout" if source.name == "fib.ls" else "typed stdout"
            elapsed = 1 if env["LAMBDA_MIR_COMPILER"] == "mvp" else 2
            return f"{text}\n__TIMING__:{elapsed}\n"

        arguments = ["lambda_mir_mvp.py", "--release", "--lambda-only", "--exe", str(self.exe),
                     "--variant", "both", "--case", "r7rs/fib", "--runs", "3", "--warmups", "1",
                     "--benchmark-root", str(corpus), "--output", str(output)]
        with mock.patch.object(sys, "argv", arguments), mock.patch.object(lambda_mir_mvp, "invoke", invoke), \
                mock.patch.dict(os.environ, {"COW_EXEC_PROFILE": "1"}), mock.patch("builtins.print"):
            lambda_mir_mvp.main()
        data = json.loads(output.read_text())
        self.assertEqual(seen, {"fib.ls", "fib2.ls"})
        self.assertEqual({row["case_id"] for row in data["cases"]},
                         {"r7rs/fib/typed", "r7rs/fib/untyped"})
        for row in data["cases"]:
            self.assertFalse(row["engine_failures"])
            self.assertTrue(row["performance_gates"]["faster_than_full_mir"])
            self.assertTrue(row["golden_path"].endswith(".formatted.txt"))
            self.assertEqual(len(row["samples_ms"]["mvp"]), 3)
        self.assertIsNone(data["geomean_mvp_over_production"])
        for summary in data["variant_summaries"].values():
            self.assertEqual(summary["geomean_mvp_over_production"], 0.5)

    def test_sumfp_supplements_preserve_untyped_kernel_and_harness(self):
        directory = self.benchmark_root / "r7rs"
        c_source = directory / "c2mir/sumfp.c"
        generated = []
        for variant, name in (("typed", "sumfp2.ls"), ("untyped", "sumfp.ls")):
            source = directory / name
            result, _, metadata = lambda_mir_mvp.sumfp_batch_sources(source, c_source, 3, variant)
            generated.append(result)
            kernel = source.read_text().split("pn main()", 1)[0]
            self.assertTrue(metadata["lambda_source"].startswith(kernel))
            if variant == "untyped":
                self.assertNotRegex(metadata["lambda_source"], r"var (?:total|i)\s*:")
            self.assertTrue(result.with_suffix(".txt").is_file())
        self.assertNotEqual(*generated)

    def test_batch_record_and_decimal_paths(self):
        modes = ({}, {"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"},
            {"LAMBDA_GC_FORCE_SEED": "1592594996", "LAMBDA_GC_FORCE_ONE_IN": "3", "LAMBDA_GC_POISON_FREED": "1"})
        for name in ("batch_records", "decimal_dispatch", "scalar_outcomes"):
            source = ROOT / "test/lambda/mvp" / f"mir_mvp_{name}.ls"
            for mode in modes:
                with self.subTest(fixture=name, mode=mode):
                    self.check_golden(source, mode)
        decimal = ROOT / "test/lambda/mvp/mir_mvp_decimal_dispatch.ls"
        self.assertEqual(self.function_mir(decimal, "decimal_ops").count("\tcall\tlambda_decimal_binary"), 6)
        self.assertIn("\tcall\tfn_add", self.function_mir(decimal, "decimal_generic"))
        compared = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_scalar_outcomes.ls", "scalar_compare")
        self.assertIn("\tdlt\t", compared)
        self.assertIn("\tcall\tfn_lt", compared)

    def test_recursive_constructor_gc(self):
        original = (ROOT / "test/benchmark/larceny/gcbench2.ls").read_text()
        source = Path(self.scratch.name) / "recursive_constructor.ls"
        source.write_text(original[:original.index("pn main()")] + '''pn main() {
    let retained = make_tree(5)
    let temporary = make_tree(4)
    print([check_tree(retained), check_tree(temporary)]); print("\\n")
}
''')
        source.with_suffix(".txt").write_text("[63, 31]\n")
        for mode in ({"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"},
                     {"LAMBDA_GC_FORCE_SEED": "1592594996", "LAMBDA_GC_FORCE_ONE_IN": "3",
                      "LAMBDA_GC_POISON_FREED": "1"}):
            with self.subTest(mode=mode):
                self.check_golden(source, mode)

    def test_idle_test_supervisors_block_benchmarking(self):
        listing = subprocess.CompletedProcess([], 0, stdout="""PID PGID %CPU COMMAND
10 900 0.0 ../p2_discovery_gtest.exe --gtest_filter=AutoDiscovered/*
11 901 0.0 ./lambda.exe test-batch --timeout=60
12 902 0.0 python3 -m unittest test_lambda_mir_mvp.LambdaMirMvpTests
13 777 90.0 ./lambda.exe run benchmark.ls
14 903 0.1 ./lambda.exe view example.html
""")
        with mock.patch.object(lambda_mir_mvp.subprocess, "run", return_value=listing):
            jobs = lambda_mir_mvp.competing_benchmark_jobs(777)
        self.assertEqual({job["pid"] for job in jobs}, {10, 11, 12})

    def test_interrupted_round_is_retried(self):
        report = Path(self.scratch.name) / "interrupted.json"
        expected = (ROOT / "test/benchmark/r7rs/fib2.txt").read_text().strip()
        state = {"calls": 0, "busy": False}

        def invoke(command, env):
            state["calls"] += 1
            # the first measured round overlaps a build after one observation.
            elapsed = 999 if state["calls"] == 4 else 1
            state["busy"] = elapsed == 999
            return expected + f"\n__TIMING__:{elapsed}\n"

        def competitors(group):
            busy = state["busy"]
            state["busy"] = False
            return [{"pid": 123, "program": "make"}] if busy else []

        command = ["lambda_mir_mvp.py", "--release", "--exe", str(self.exe),
            "--pause-for-jobs", "--case", "r7rs/fib", "--runs", "3", "--warmups", "1",
            "--output", str(report)]
        with mock.patch.object(sys, "argv", command), mock.patch.object(lambda_mir_mvp, "invoke", invoke), \
                mock.patch.object(lambda_mir_mvp, "competing_benchmark_jobs", competitors), \
                mock.patch.object(lambda_mir_mvp.time, "sleep"), mock.patch("builtins.print"):
            lambda_mir_mvp.main()
        row = json.loads(report.read_text())["cases"][0]
        self.assertEqual(len(row["execution_order"]), 3)
        self.assertEqual(len(row["discarded_rounds"]), 1)
        self.assertIn(999, row["discarded_rounds"][0]["samples_ms"].values())
        self.assertTrue(all(values == [1, 1, 1] for values in row["samples_ms"].values()))

    def test_static_scalar_fallback(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_scalar_fallback.ls"
        self.check_gc_fixtures([source])
        for name in ("decimal_sum", "decimal_remainder", "decimal_quotient", "decimal_compare", "string_compare"):
            body = self.function_mir(source, name)
            self.assertNotIn("lambda_int_lane_", body)
            before_helper = body[:re.search(r"\tcall\t(?:fn_|lambda_decimal_binary)", body).start()]
            self.assertNotIn("\tubgt\t", before_helper)
        dynamic = self.function_mir(source, "dynamic_sum")
        self.assertIn("\tubgt\t", dynamic)
        self.assertIn("\tcall\tfn_add", dynamic)
        self.assertIn("\tdadd\t", dynamic)
        chain = self.function_mir(source, "decimal_chain")
        self.assertNotRegex(chain, r"\tcall\tmvp_\d+_decimal_quotient_dp")
        self.assertIn("\tcall\tlambda_decimal_binary", chain)
        epilogue = re.search(r"(?m)^(L\d+):\n(?:\t[^\n]*\n)*?\tret\t", chain).group(1)
        # the failed-local completion keeps its cold check; successful arithmetic does not.
        self.assertNotIn("lambda_type_check", chain[:chain.index("\tjmp\t" + epilogue)])
        self.assertIn("lambda_type_check", self.function_mir(source, "loose_decimal"))
        text = self.function_mir(source, "decimal_text")
        self.assertIn("fn_strcat_values", text)
        self.assertNotIn("\tcall\tfn_join", text)

    def test_native_shift_mir(self):
        for function in ("typed", "fixed"):
            body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_shift.ls", function)
            self.assertIn("\trsh\t", body)
            self.assertIn("fn_shr_item", body)
            self.assertNotIn("lambda_type_check", body)

    def test_nullable_comparison_mir(self):
        body = self.function_mir(ROOT / "test/benchmark/larceny/quicksort2.ls", "partition")
        native = body[:body.index("\tcall\tfn_index")]
        self.assertIn("\tle\t", native)
        self.assertNotIn("fn_le", native)
        # the checked sibling retains the pivot's deferred admission outcome (S7.7.2v2).
        self.assertIn("fn_le", body)
        self.assertNotIn("is_truthy", body)

    def test_error_only_return_to_union(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_error_boundaries.ls"
        self.check_gc_fixtures((source,))
        # an admitted Error cannot need validation of the union's normal int arm.
        body = self.function_mir(source, "returned_error")
        self.assertNotIn("lambda_type_check", body)

    def test_guarded_numeric_iterations(self):
        body = self.function_mir(ROOT / "test/benchmark/kostya/collatz2.ls", "collatz_len")
        native = body[:body.index("\tcall\tlambda_int_lane_divmod_slow")]
        self.assertEqual(len(re.findall(r"\trsh\t[^\n]+, %r[0-9a-f]+\n", native)), 4)
        self.assertNotIn("lambda_int_lane_", native)
        self.assertIn("fn_shr_item", body)  # guard misses preserve poison/count errors

    def test_indexed_region_and_call_boundary(self):
        source = ROOT / "test/benchmark/larceny/quicksort2.ls"
        self.check_gc_fixtures((source,))
        body = self.function_mir(source, "partition")
        native = body[:body.index("\tcall\tfn_index")]
        for helper in ("lambda_array_set_checked_item", "lambda_type_check", "lambda_int_lane_add_slow"):
            self.assertNotIn(helper, native)
            self.assertIn(helper, body)
        caller = self.function_mir(source, "quicksort")
        consumed = caller[caller.index("\tcall\tmvp_1_partition_dp"):caller.index("\tcall\tmvp_2_quicksort_dp")]
        self.assertNotIn("lambda_type_check", consumed)
        self.assertNotIn("int2it_lane", caller)
        self.assertNotIn("u8:3(", caller)  # no storage witness for a forwarding-only owner

    def test_exact_binary_fraction(self):
        body = self.function_mir(ROOT / "test/benchmark/beng/spectralnorm2.ls", "eval_A")
        self.assertEqual(body.count("\ti2d\t"), 1)
        self.assertNotIn("\tdadd\t", body)
        self.assertIn("\trsh\t", body)

    def test_array_reduction_mir(self):
        for source, function in ((ROOT / "test/benchmark/larceny/array12.ls", "benchmark"),
                                 (ROOT / "test/lambda/mvp/mir_mvp_arrays.ls", "reduce"),
                                 (ROOT / "test/lambda/mvp/mir_mvp_arrays.ls", "reduce_outcome")):
            with self.subTest(source=source.name, function=function):
                body = self.function_mir(source, function)
                self.assertRegex(body, r"\tadd\t[^\n]+, 4\n")
                self.assertIn("fn_index", body)  # short/negative extents keep the checked sibling

    def test_fallible_inline_and_bool_count(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_consumers.ls"
        body = self.function_mir(source, "inline_checks")
        self.assertNotRegex(body, r"\tcall\tmvp_\d+_small_get_dp")
        self.assertIn("lambda_type_check", body)  # the short-array return still fails its int boundary
        body = self.function_mir(source, "truth_count")
        self.assertRegex(body, r"\tadd\t[^\n]+, 4\n")
        self.assertIn("fn_index", body)
        body = self.function_mir(source, "rotate_arrays")
        # only the saved alias and the later observed scratch require captures.
        self.assertLessEqual(len(re.findall(r"\tcall\tcow_capture_value_", body)), 4)

    def test_guarded_float_body(self):
        body = self.function_mir(ROOT / "test/benchmark/awfy/nbody2.ls", "advance")
        # Both bodies remain; the first sqrt belongs to the noncollecting version.
        self.assertGreaterEqual(len(re.findall(r"\tcall\tsqrt_", body)), 2)
        first = body[:body.index("\tcall\tsqrt_")]
        for helper in ("fn_index", "lambda_array_set_checked_item", "lambda_type_check"):
            self.assertNotIn("\tcall\t" + helper, first)
            self.assertIn("\tcall\t" + helper, body)
    def test_guarded_float_loop(self):
        for suite, entry in (("awfy", "benchmark"), ("beng", "main"),
                             ("jetstream", "run_nbody_system")):
            with self.subTest(suite=suite):
                caller = self.function_mir(ROOT / f"test/benchmark/{suite}/nbody2.ls", entry)
                checked = re.search(r"\tcall\tmvp_\d+_advance_dp", caller)
                self.assertIsNotNone(checked)
                self.assertIn("\tcall\tsqrt_", caller[:checked.start()])

    def test_records_and_readonly_kernel_emission(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_records.ls", "chain")
        self.assertIn("map_alloc_for_type", body)
        self.assertNotIn("map_fill", body)
        self.assertNotIn("cow_capture_value", body)
        caller = self.function_mir(ROOT / "test/benchmark/beng/nbody2.ls", "main")
        native = caller[:caller.index("\tcall\tmvp_1_advance_dp")]
        self.assertIn("\tcall\tsqrt_", native)
        # the ordinary call remains for layout, alias and short-array misses.
        self.assertIn("\tcall\tmvp_1_advance_dp", caller)

    def test_allocating_vector_emission(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_proven_domains.ls"
        for name in ("cell", "transient_cell"):
            constructor = self.function_mir(source, name)
            self.assertIn("map_alloc_for_type", constructor)
            self.assertNotIn("map_fill", constructor)
            self.assertNotIn("cow_capture_value", constructor)
        body = self.function_mir(source, "shifted")
        native = body[:body.index("\tcall\tarray_plain_")]
        self.assertIn("lambda_fill_primitive_bits", native)
        self.assertGreaterEqual(native.count("\tdadd\t"), 3)
        self.assertNotIn("\tcall\tfn_index_", native)
        self.assertNotIn("\tcall\tlambda_type_check_", native)
        # short arrays and other layouts retain the checked construction path.
        self.assertIn("\tcall\tfn_index_", body)
        self.assertIn("\tcall\tlambda_type_check_", body)
        store = self.function_mir(source, "put_erased")
        self.assertIn("lambda_array_set_checked_preplanned", store)

    def test_guarded_iterations_and_borrow_inlining(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_iterations.ls", "butterfly")
        native = body[:body.index("\tcall\tfn_index_")]
        self.assertIn("\tdadd\t", native)
        self.assertIn("\tdsub\t", native)
        self.assertIn("lambda_array_set_checked_item", body)
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_borrow_inline.ls", "nested")
        self.assertNotRegex(body, r"\tcall\tmvp_\d+_replace_owner_dp")

    def test_erased_array_iterations(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_erased_iterations.ls"
        for compiler in ("production", "mvp"):
            for stress in GC_MODES:
                with self.subTest(compiler=compiler, stress=stress):
                    self.check_golden(source, {**stress, "LAMBDA_MIR_COMPILER": compiler})
        body = self.function_mir(source, "scale")
        native = body[:body.index("\tcall\tfn_index_")]
        self.assertIn("\tdmul\t", native)

    def test_untyped_native_ranges(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_untyped_ranges.ls"
        self.check_gc_fixtures([source, ROOT / "test/lambda/mvp/mir_mvp_boxed_range_bounds.ls"])
        body = self.function_mir(source, "total")
        self.assertNotIn("\tcall\tfn_to_", body)
        self.assertNotIn("\tcall\titer_val_at_", body)

    def test_untyped_recursive_snapshots(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_untyped_snapshots.ls"
        self.check_gc_fixtures([source])
        body = self.function_mir(source, "visit")
        self.assertIn("lambda_array_scalar_snapshot", body)

    def test_recursive_array_capture_effects(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_recursive_arrays.ls"
        for name in ("sum_tail", "even_read", "odd_read"):
            body = self.function_mir(source, name)
            self.assertEqual(len(re.findall(r"\tcall\tcow_capture_value_", body)), 0, name)
            self.assertNotRegex(body, r"\tmov\tu8:4\(")
        body = self.function_mir(source, "retain")
        self.assertNotRegex(body, r"\tcall\tcow_capture_value_")
        self.assertRegex(body, r"\tor\t[^\n]+, 1\n")
        self.assertRegex(body, r"\tmov\tu8:4\(")
        body = self.function_mir(source, "plain_write")
        self.assertIn("\tcall\tlambda_array_scalar_snapshot", body)
        self.assertNotRegex(body, r"\tcall\tcow_capture_value_")
        for name in ("bounded_up", "bounded_down", "near_limit"):
            body = self.function_mir(source, name)
            self.assertNotIn("lambda_int_lane_add_slow", body)
            self.assertNotIn("lambda_int_lane_sub_slow", body)
        body = self.function_mir(source, "grows")
        self.assertIn("lambda_int_lane_mul_slow", body)
        body = self.function_mir(source, "search")
        self.assertIn("lambda_type_check", body)  # a missing element still fails the typed declaration

    def test_closed_kernel_loops(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_kernel_loops.ls"
        body = self.function_mir(source, "repeat_add")
        call = re.search(r"\tcall\tmvp_\d+_add_four_dp", body)
        self.assertIsNotNone(call)  # short or shared owners retain the checked call
        self.assertIn("\tdadd\t", body[:call.start()])
        self.assertNotIn("lambda_array_set_checked_item", body[:call.start()])
        replacing = self.function_mir(source, "replacing")
        self.assertNotIn("\tdadd\t", replacing)  # replacement invalidates a loop-wide witness

    def test_module_initialization(self):
        source = Path(self.scratch.name) / "module.ls"
        source.write_text('fn candidate() any { "wrong" }; let bad: int = candidate(); '
                          'pn main() { print([bad is error, bad.code]) }\n')
        # S7.7.2v2 retains a deferred validation outcome and continues into main.
        source.with_suffix(".txt").write_text("[true, 201]\n")
        for compiler in ("mvp", "production"):
            with self.subTest(compiler=compiler):
                self.check_golden(source, {"LAMBDA_MIR_COMPILER": compiler})

    def test_admission_precedes_execution(self):
        source = Path(self.scratch.name) / "static_mismatch.ls"
        source.write_text('pn main() { print("EXECUTED"); let bad: int = "wrong" }\n')
        source.with_suffix(".txt").write_text("")
        for compiler in ("mvp", "production"):
            with self.subTest(compiler=compiler):
                result = self.run_script(source, {"LAMBDA_MIR_COMPILER": compiler})
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout.strip(), "")
                self.assertIn("error[E201]", result.stderr)

    def test_expanded_admission_executes(self):
        sources = {
            "capture": ('pn outer(x: int) { pn inner() int { return x }; return inner() }; pn main() { print("EXECUTED"); print(outer(7)) }', 'EXECUTED7'),
            "content": ('1; 2', '1\n2'),
            "comprehension": ('pn main() { print([for (x in 0 to 2) x]) }', '[0, 1, 2]'),
            "builtin": ('pn main() { print(abs(math.tan(2.0) + 2.185039863261519) < 0.000000001) }', 'true'),
            "module_call": ('let A = 1.5; let B = float(2); pn main() { print(A ++ " " ++ B) }', '1.5 2'),
        }
        for name, (code, expected) in sources.items():
            with self.subTest(name=name):
                source = Path(self.scratch.name) / f"{name}.ls"
                source.write_text(code + "\n")
                source.with_suffix(".txt").write_text(expected + "\n")
                self.check_golden(source)

    def test_selector_is_explicit(self):
        source = ROOT / "test/benchmark/r7rs/fib2.ls"
        for options in ({"LAMBDA_EXEC_BACKEND": "auto"}, {"LAMBDA_EXEC_BACKEND": "interp"},
                        {"LAMBDA_MIR_COMPILER": "unknown"}):
            with self.subTest(options=options):
                result = self.run_script(source, options)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("fib: PASS", result.stdout)

    def test_native_recursive_mir(self):
        body = self.function_mir(ROOT / "test/benchmark/r7rs/fib2.ls", "fib")
        self.assertIn("\tadd\t", body)
        self.assertIn("\tsub\t", body)
        self.assertIn("mvp_0_fib_dp", body)
        # two bounded recursive expansions leave eight real edges, each retaining the native ABI and stack guard.
        self.assertEqual(sum("\tcall\tmvp_0_fib_dp" in line for line in body.splitlines()), 8)
        for helper in ("fn_add", "fn_sub", "int2it_lane", "lambda_type_check", "lambda_side_stack_ensure_for"):
            self.assertNotIn(helper, body)

    def test_stack_exhaustion_uses_recovery(self):
        source = Path(self.scratch.name) / "exhaustion.ls"
        source.write_text('pn deep(n: int) int { return deep(n + 1) + 1 }; pn main() { print(deep(0)) }\n')
        source.with_suffix(".txt").write_text("stack overflow\n")
        result = self.run_script(source)
        self.assertGreaterEqual(result.returncode, 0, "stack overflow terminated by a signal")
        self.assertIn("stack overflow", (result.stdout + result.stderr).lower())

    def test_guarded_array_stack_exhaustion(self):
        source = Path(self.scratch.name) / "array_exhaustion.ls"
        source.write_text('pn deep(var a: int[], n: int) int { a[0] = n; return deep(a, n + 1) }; '
                          'pn main() { var a: int[] = [0]; print(deep(a, 0)) }\n')
        source.with_suffix(".txt").write_text("stack overflow\n")
        result = self.run_script(source)
        self.assertGreaterEqual(result.returncode, 0, "guarded array recursion terminated by a signal")
        self.assertIn("stack overflow", (result.stdout + result.stderr).lower())

    def test_native_countdown_mir(self):
        body = self.function_mir(ROOT / "test/benchmark/r7rs/sum2.ls", "run")
        # four independent lanes plus a scalar remainder retain the checked sibling for guard misses.
        self.assertRegex(body, r"\tsub\t[^\n]+, 4\n\tbge\t")
        self.assertRegex(body, r"\tadd\t[^\n]+\n\tsub\t[^\n]+\n\tbge\t")
        self.assertIn("lambda_int_lane_add_slow", body)
        self.assertIn("lambda_int_lane_sub_slow", body)

    def test_readonly_array_tail(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_recursive_arrays.ls", "collecting_tail")
        self.assertNotRegex(body, r"\tcall\tmvp_\d+_collecting_tail_dp")
        self.assertRegex(body, r"\tcall\t(?:fn_fill|lambda_fill_for_contract)")
        self.assertIn("lambda_type_check", body)

    def test_implicit_tail_and_split_observations(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_benchmark_expansion.ls"
        for name in ("implicit_collect", "implicit_record_rows"):
            body = self.function_mir(source, name)
            self.assertNotRegex(body, rf"\tcall\tmvp_\d+_{name}(?:_|,)")
        body = self.function_mir(source, "split_observation")
        self.assertIn("fn_split2", body)
        self.assertNotIn("lambda_item_adopt_scalar_home", body)
        # a generic field replacement can rebuild a record's physical layout;
        # the subsequent typed call must establish its own prefix.
        body = self.function_mir(source, "updated_box")
        self.assertIn("lambda_map_set_checked", body)
        self.assertIn("\tcall\tlambda_type_check", body)

    def test_guarded_recursive_arrays(self):
        source = ROOT / "test/benchmark/awfy/queens2.ls"
        body = self.function_mir(source, "place_queen")
        first = body[:body.index("\tcall\tmvp_2_place_queen_dp")]
        self.assertGreater(first.count("\tcall\tlambda_stack_is_exhausted"), 0)
        for helper in ("fn_index", "lambda_array_set_checked_item", "lambda_type_check", "cow_capture_value"):
            self.assertEqual(first.count("\tcall\t" + helper), 0, helper)
            if helper in ("fn_index", "lambda_array_set_checked_item"):
                self.assertGreater(body.count("\tcall\t" + helper), 0, helper)
        # the hot self edge has no owner replacement and publishes no borrow cells.
        self.assertNotRegex(first, r"mov\ti64:1(?:60|68|76|84)\(runtime\), %")

    def test_constructor_admission_once(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_constructor_boundary.ls"
        for name in ("freshly", "call_fill"):
            body = self.function_mir(source, name)
            self.assertGreater(body.count("lambda_fill_for_contract_int_lane_resolved"), 0, name)
            # constructor success is already admitted; the error tag is still checked.
            first = body[body.index("\tcall\tlambda_fill_for_contract"):]
            self.assertGreater(first.count("\tursh\t"), 0, name)
            following = first[first.index("\n") + 1:]
            boundary = following[:following.index("\tbne\t")]
            self.assertEqual(boundary.count("lambda_type_check"), 0, name)

    def test_native_constructor_lanes(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_constructor_boundary.ls"
        for name in ("freshly", "float_values", "bool_values"):
            body = self.function_mir(source, name)
            native = body[:body.index("\tcall\tlambda_fill_primitive_bits")]
            self.assertNotRegex(native, r"\tcall\t(?:int2it_lane|push_d|lambda_fill_for_contract)")
            self.assertIn("lambda_fill_for_contract_int_lane_resolved", body)

    def test_readonly_constant_constructor(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_constructor_boundary.ls"
        body = self.function_mir(source, "constant_argument")
        self.assertNotRegex(body, r"\tcall\t(?:lambda_fill_|fn_fill)")
        for name in ("mutable_argument", "retained_argument", "forwarded_argument"):
            body = self.function_mir(source, name)
            self.assertIn("lambda_fill_primitive_bits", body)

    def test_adjacent_copy_loop(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_copy_loops.ls", "adjacent")
        native = body[:body.index("\tcall\tfn_index")]
        self.assertGreater(native.count("\tublt\t"), 0)
        self.assertEqual(native.count("\tcall\tlambda_array_set_checked_item"), 0)

    def test_native_bitwise_tree(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_shift.ls", "packed")
        # both native operations precede the first Item payload mask/boxing.
        native = body[:body.index("\tand\t")]
        self.assertIn("\tlsh\t", native)
        self.assertIn("\tor\t", native)
        self.assertIn("fn_bor_item", body)

    def test_guarded_array_branch(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_iterations.ls", "conditional_swap")
        native = body[:body.index("\tcall\tfn_index")]
        self.assertRegex(native, r"\tdmov\td:\d*\(%")
        self.assertIn("lambda_array_set_checked_item", body)

    def test_singleton_integer_constants(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_flags.ls", "constants")
        self.assertNotIn("mvp_module_scalar", body)

    def test_power_two_divmod(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_modular_update.ls"
        for name in ("remainder", "quotient", "large"):
            body = self.function_mir(source, name)
            self.assertNotIn("\tmod\t", body)
            self.assertNotIn("\tdiv\t", body)
            self.assertIn("lambda_int_lane_divmod_slow", body)

    def test_nullable_numeric_cast_emission(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_numeric_cast.ls"
        for name in ("float_literals", "int_literals"):
            body = self.function_mir(source, name)
            self.assertNotIn("\tcall\tfn_int", body)
            self.assertNotIn("\tcall\tfn_float", body)
        for name, helper in (("as_int", "fn_int"), ("from_float", "fn_int"),
                             ("widen_optional", "fn_float"), ("same_float", "fn_float")):
            body = self.function_mir(source, name)
            self.assertIn("\tcall\t" + helper, body)
            # conversion helpers remain only on the absent arm.
            before_helper = body[:body.index("\tcall\t" + helper)]
            self.assertIn("\tjmp\t", before_helper)
        store = self.function_mir(source, "store_cast")
        self.assertIn("lambda_array_set_checked_item", store)
        self.assertNotIn("lambda_array_set_checked_preplanned", store)

    def test_float_predicate_branch(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_float_compare.ls", "find_prefix")
        native = body[:body.index("\tcall\tfn_index")]
        self.assertIn("\tdlt\t", native)
        self.assertNotRegex(native, r"\tor\t[^\n]+, 216172782113783808\n")

    def test_native_string_array_read(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_string_arrays.ls", "fetch_string")
        native = body[:body.index("\tcall\tfn_index")]
        self.assertNotRegex(native, r"\tand\t[^\n]+, 17\n")
        self.assertRegex(native, r"\tand\t[^\n]+, 72057594037927935\n")

    def test_scalar_record_witness(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_record_witness.ls", "bump")
        native = body[:body.index("\tcall\tlambda_map_set_checked")]
        # the shared COW predicate now combines flags and owner state in one word.
        self.assertEqual(len(re.findall(r"\tand\t[^\n]+, 4294979584\n", native)), 1)
        self.assertIn("\tadd\t", native)

    def test_erased_float_comparison(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_float_compare.ls"
        for name in ("relation", "reversed"):
            body = self.function_mir(source, name)
            native = body[:body.index("\tcall\tfn_index")]
            self.assertIn("\tdlt\t", native)
            self.assertNotIn("\tcall\tfn_lt", native)
            self.assertIn("\tcall\tfn_lt", body)

    def test_modular_array_update(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_modular_update.ls"
        self.check_gc_fixtures([source])
        body = self.function_mir(source, "step")
        first = body[:body.index("\tcall\tfn_index")]
        self.assertRegex(first, r"\tand\t[^\n]+, 15\n")
        self.assertGreater(body.count("lambda_array_set_checked_item"), 0)
        affine = self.function_mir(source, "affine")
        first = affine[:affine.index("\tcall\tfn_index")]
        self.assertRegex(first, r"\tmul\t[^\n]+, 13\n")
        self.assertRegex(first, r"\tmod\t[^\n]+, 31\n")
        self.assertIn("\tcall\tfn_int", affine)
        conditional = self.function_mir(source, "conditional_step")
        native = conditional[:conditional.index("\tcall\tfn_index")]
        self.assertNotIn("\tcall\tfmod_", native)
        self.assertNotIn("\tcall\tlambda_int_lane_add_slow", native)

    def test_single_use_string_constructor(self):
        body = self.function_mir(ROOT / "test/benchmark/kostya/json_gen2.ls", "benchmark")
        # coalesced literal runs and integer lanes join directly into the owner.
        self.assertRegex(body, r"\tcall\tfn_strcat_values_[^\n]+, 1, 8,")
        self.assertNotRegex(body, r"\tcall\tfn_strcat_(?:many|values)_[^\n]+, 0, (?:7|25),")
        self.assertNotRegex(body, r"\tcall\t(?:fn_string_p|mvp_\d+_json_num_)")

    def test_assignment_failure_names_binding(self):
        source = Path(self.scratch.name) / "assignment.ls"
        source.write_text('pn assign(candidate: any) any { var saved: int = 7; saved = candidate; '
                          'print(saved is error); return saved }; pn main() { return assign("bad") }\n')
        source.with_suffix(".txt").write_text("true\n")
        # S7.7.4v2 continues with the retained Error; main then returns that Error.
        for compiler in ("mvp", "production"):
            with self.subTest(compiler=compiler):
                result = self.run_script(source, {"LAMBDA_MIR_COMPILER": compiler})
                self.assertGreater(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(strip_timing(result.stdout), "true")
                self.assertIn("error[E201]", result.stderr)
                self.assertRegex(result.stderr, r"type check at [^\n]*\bsaved\b[^\n]* failed")

    def test_bounded_counter_mir(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_integer.ls", "bounded")
        self.assertIn("\ti2d\t", body)
        self.assertIn("\tdadd\t", body)
        for helper in ("lambda_int_lane_add_slow", "lambda_int_lane_to_double_c", "lambda_type_check"):
            self.assertNotIn(helper, body)

    def test_native_success_consumers(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_native.ls", "shifted_add")
        self.assertIn("\tlsh\t", body)
        self.assertIn("\tadd\t", body)
        self.assertIn("fn_shl_item", body)
        self.assertNotIn("fn_add", body)
        self.assertNotIn("lambda_type_check", body)

    def test_float_constants_precede_loop(self):
        body = self.function_mir(ROOT / "test/benchmark/r7rs/sumfp2.ls", "run")
        constants = list(re.finditer(r"\tdmov\t[^\n,]+, -?\d+\.\d+e[+-]\d+", body))
        self.assertGreaterEqual(len(constants), 2)
        self.assertTrue(all(match.start() < body.index("\tjmp\t") for match in constants))


if __name__ == "__main__":
    unittest.main()
