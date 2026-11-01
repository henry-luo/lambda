#!/usr/bin/env python3
"""Run the scalar MVP's admission, ownership, error and emission checks."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from lambda_mir_mvp import CASES, ROOT
from run_c2mir_benchmarks import strip_timing


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

    def test_scalar_benchmarks(self):
        for suite, name in CASES:
            with self.subTest(suite=suite, name=name):
                self.check_golden(ROOT / "test/benchmark" / suite / f"{name}2.ls")

    def test_ownership_and_semantics(self):
        fixtures = [ROOT / "test/lambda/proc" / f"mir_mvp_{name}.ls" for name in ("scalar", "roots")]
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_errors.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_integer.ls")
        fixtures.append(ROOT / "test/lambda/mvp/mir_mvp_native.ls")
        modes = ({}, {"LAMBDA_GC_FORCE_EVERY": "1", "LAMBDA_GC_POISON_FREED": "1"},
                 {"LAMBDA_GC_FORCE_SEED": "1592594996", "LAMBDA_GC_FORCE_ONE_IN": "3", "LAMBDA_GC_POISON_FREED": "1"})
        for source in fixtures:
            for mode in modes:
                with self.subTest(source=source.name, mode=mode):
                    self.check_golden(source, mode)

    def test_admission_precedes_execution(self):
        sources = {
            "array": 'pn main() { print("EXECUTED"); let a = [1, 2]; print(a) }',
            "builtin": 'pn main() { print("EXECUTED"); print(sqrt(2.0)) }',
            "capture": 'pn outer(x: int) { pn inner() int { return x }; return inner() }; pn main() { print("EXECUTED"); print(outer(7)) }',
            "content": '1; 2',
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
        # one recursive expansion leaves four real edges, each retaining the native ABI and stack guard.
        self.assertEqual(sum("\tcall\tmvp_0_fib_dp" in line for line in body.splitlines()), 4)
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
