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
            if key.startswith(("LAMBDA_", "MIR_")):
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
        self.assertEqual(strip_timing(result.stdout), source.with_suffix(".txt").read_text().strip())

    def function_mir(self, source, name):
        dump = Path(self.scratch.name) / f"{source.stem}.mir"
        result = self.run_script(source, {"LAMBDA_MIR_DUMP_PATH": str(dump)}, logging=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        match = re.search(rf"mvp_\d+_{re.escape(name)}:\tfunc(.*?)\tendfunc", dump.read_text(), re.S)
        self.assertIsNotNone(match, f"missing MIR function {name}")
        return match.group(1)

    def test_benchmark_goldens(self):
        for suite, name in CASES:
            with self.subTest(suite=suite, name=name):
                self.check_golden(ROOT / "test/benchmark" / suite / f"{name}2.ls")

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
        call = re.search(r"\tcall\tmvp_\d+_float_update_dp", body)
        self.assertIsNotNone(call)
        self.assertIn("\tdmul\t", body[:call.start()])
        self.assertNotIn("lambda_type_check", body[:call.start()])

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
        for name in ("decimal_sum", "decimal_remainder", "decimal_quotient", "decimal_compare", "string_compare"):
            body = self.function_mir(source, name)
            self.assertNotIn("lambda_int_lane_", body)
            before_helper = body[:re.search(r"\tcall\t(?:fn_|lambda_decimal_binary)", body).start()]
            self.assertNotIn("\tubgt\t", before_helper)
        dynamic = self.function_mir(source, "dynamic_sum")
        self.assertIn("\tubgt\t", dynamic)
        self.assertIn("\tcall\tfn_add", dynamic)
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
        self.assertIn("\tle\t", body)
        self.assertNotIn("fn_le", body)
        self.assertNotIn("is_truthy", body)

    def test_guarded_numeric_iterations(self):
        body = self.function_mir(ROOT / "test/benchmark/kostya/collatz2.ls", "collatz_len")
        native = body[:body.index("\tcall\tlambda_int_lane_divmod_slow")]
        self.assertEqual(len(re.findall(r"\trsh\t[^\n]+, %r[0-9a-f]+\n", native)), 4)
        self.assertNotIn("lambda_int_lane_", native)
        self.assertIn("fn_shr_item", body)  # guard misses preserve poison/count errors

    def test_indexed_region_and_call_boundary(self):
        source = ROOT / "test/benchmark/larceny/quicksort2.ls"
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
                                 (ROOT / "test/lambda/mvp/mir_mvp_arrays.ls", "reduce")):
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

    def test_recursive_array_capture_effects(self):
        source = ROOT / "test/lambda/mvp/mir_mvp_recursive_arrays.ls"
        for name in ("sum_tail", "even_read", "odd_read"):
            body = self.function_mir(source, name)
            self.assertEqual(len(re.findall(r"\tcall\tcow_capture_value_", body)), 0, name)
        body = self.function_mir(source, "retain")
        self.assertGreater(len(re.findall(r"\tcall\tcow_capture_value_", body)), 0)
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
                          'pn main() { print("UNEXPECTED MAIN") }\n')
        source.with_suffix(".txt").write_text("type check at bad failed\n")
        result = self.run_script(source)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("UNEXPECTED MAIN", result.stdout)
        self.assertIn("type check at bad failed", result.stdout + result.stderr)

    def test_admission_precedes_execution(self):
        sources = {
            "comprehension": 'pn main() { print("EXECUTED"); let a = [for (x in 0 to 2) x]; print(a) }',
            "builtin": 'pn main() { print("EXECUTED"); print(math.tan(2.0)) }',
            "capture": 'pn outer(x: int) { pn inner() int { return x }; return inner() }; pn main() { print("EXECUTED"); print(outer(7)) }',
            "content": '1; 2',
            "module_call": 'let A = 1.5; let B = float(2); pn main() { print(A) }',
        }
        for name, code in sources.items():
            with self.subTest(name=name):
                source = Path(self.scratch.name) / f"{name}.ls"
                source.write_text(code + "\n")
                source.with_suffix(".txt").write_text("")
                result = self.run_script(source)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout.strip(), "")
                self.assertIn("Lambda MIR MVP", result.stderr)

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
        self.assertEqual(native.count("u8:1("), 1)
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
        body = self.function_mir(source, "step")
        first = body[:body.index("\tcall\tfn_index")]
        self.assertRegex(first, r"\tand\t[^\n]+, 15\n")
        self.assertGreater(body.count("lambda_array_set_checked_item"), 0)
        affine = self.function_mir(source, "affine")
        first = affine[:affine.index("\tcall\tfn_index")]
        self.assertRegex(first, r"\tmul\t[^\n]+, 13\n")
        self.assertRegex(first, r"\tmod\t[^\n]+, 31\n")
        self.assertIn("\tcall\tfn_int", affine)

    def test_single_use_string_constructor(self):
        body = self.function_mir(ROOT / "test/benchmark/kostya/json_gen2.ls", "benchmark")
        # coalesced literal runs and integer lanes join directly into the owner.
        self.assertRegex(body, r"\tcall\tfn_strcat_values_[^\n]+, 1, 8,")
        self.assertNotRegex(body, r"\tcall\tfn_strcat_(?:many|values)_[^\n]+, 0, (?:7|25),")
        self.assertNotRegex(body, r"\tcall\t(?:fn_string_p|mvp_\d+_json_num_)")

    def test_assignment_failure_names_binding(self):
        source = Path(self.scratch.name) / "assignment.ls"
        source.write_text('pn assign(candidate: any) any { var saved: int = 7; saved = candidate; '
                          'print("UNEXPECTED"); return saved }; pn main() { return assign("bad") }\n')
        source.with_suffix(".txt").write_text("type check at saved failed\n")
        result = self.run_script(source)
        self.assertGreater(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("UNEXPECTED", result.stdout)
        self.assertIn("type check at saved failed", result.stdout + result.stderr)

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
