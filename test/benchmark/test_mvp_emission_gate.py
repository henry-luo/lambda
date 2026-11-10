"""The baseline adapter must preserve errors and count failed methods once."""
import io
import unittest

from run_mvp_emission_units import GateResult, report, suite


class GateContracts(unittest.TestCase):
    def run_cases(self, case):
        return report(unittest.TextTestRunner(stream=io.StringIO(), resultclass=GateResult).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(case)))

    def test_subtest_failures_count_the_parent_once(self):
        class Cases(unittest.TestCase):
            def test_values(self):
                for value in (1, 2):
                    with self.subTest(value=value):
                        self.assertEqual(value, 0)
        data = self.run_cases(Cases)
        self.assertEqual(data["summary"], {"passed": 0, "failed": 1, "total": 1})
        self.assertEqual(len(data["results"][0]["errors"]), 2)

    def test_errors_skips_and_successes_are_distinct(self):
        class Cases(unittest.TestCase):
            def test_error(self):
                raise RuntimeError("broken fixture")
            def test_skip(self):
                self.skipTest("missing required probe")
            def test_pass(self):
                self.assertTrue(True)
        data = self.run_cases(Cases)
        self.assertEqual(data["summary"], {"passed": 1, "failed": 2, "total": 3})

    def test_empty_report_and_invalid_selection_fail_closed(self):
        self.assertEqual(report(GateResult(io.StringIO(), True, 1))["summary"]["failed"], 1)
        for names in ([], ["test_missing"], ["test_native_recursive_mir"] * 2):
            with self.subTest(names=names), self.assertRaises(ValueError):
                suite(names)

    def test_class_setup_failure_is_reported(self):
        class Cases(unittest.TestCase):
            @classmethod
            def setUpClass(cls):
                raise RuntimeError("engine unavailable")
            def test_value(self):
                self.fail("setup should stop this method")
        data = self.run_cases(Cases)
        self.assertEqual(data["summary"], {"passed": 0, "failed": 1, "total": 1})
        self.assertIn("engine unavailable", data["results"][0]["errors"][0])

    def test_normal_selection_retains_optimization_controls(self):
        selected = {case._testMethodName for case in suite()}
        self.assertGreaterEqual(len(selected), 56)
        self.assertTrue({"test_native_recursive_mir", "test_guarded_float_body",
                         "test_recursive_array_capture_effects", "test_modular_array_update"} <= selected)
