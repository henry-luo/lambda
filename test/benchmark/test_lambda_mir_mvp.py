#!/usr/bin/env python3
"""Run the MVP's admission, ownership, error and emission checks."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from lambda_mir_mvp import GC_CASES, CASES, ROOT
from run_c2mir_benchmarks import C2M, ensure_c2m, strip_timing


class LambdaMirMvpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = Path(os.environ.get("LAMBDA_TEST_EXE", ROOT / "lambda.exe")).resolve()
        (ROOT / "temp").mkdir(exist_ok=True)
        cls.scratch = tempfile.TemporaryDirectory(prefix="mir_mvp_", dir=ROOT / "temp")

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def run_script(self, source, overrides=None, logging=False):
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
    four1(a, 8)
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
        native = body[:body.index("\tmod\t")]
        self.assertEqual(native.count("\trsh\t"), 4)
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

    def test_guarded_iterations_and_borrow_inlining(self):
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_iterations.ls", "butterfly")
        native = body[:body.index("\tcall\tfn_index_")]
        self.assertIn("\tdadd\t", native)
        self.assertIn("\tdsub\t", native)
        self.assertIn("lambda_array_set_checked_item", body)
        body = self.function_mir(ROOT / "test/lambda/mvp/mir_mvp_borrow_inline.ls", "nested")
        self.assertNotRegex(body, r"\tcall\tmvp_\d+_replace_owner_dp")

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
            "array": 'pn main() { print("EXECUTED"); let a = [1, "two"]; print(a) }',
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

    def test_native_countdown_mir(self):
        body = self.function_mir(ROOT / "test/benchmark/r7rs/sum2.ls", "run")
        # four independent lanes plus a scalar remainder retain the checked sibling for guard misses.
        self.assertRegex(body, r"\tsub\t[^\n]+, 4\n\tbge\t")
        self.assertRegex(body, r"\tadd\t[^\n]+\n\tsub\t[^\n]+\n\tbge\t")
        self.assertIn("lambda_int_lane_add_slow", body)
        self.assertIn("lambda_int_lane_sub_slow", body)

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
