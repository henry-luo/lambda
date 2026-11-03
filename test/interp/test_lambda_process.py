"""Checks bounded benchmark output and timeout cleanup without a Lambda build."""
import sys
import unittest

from lambda_process import run_process


class BoundedProcessTests(unittest.TestCase):
    def test_success_keeps_unicode_output(self):
        result = run_process([sys.executable, '-c', 'print("hello λ")'], 5,
                             output_limit=128)
        self.assertEqual(result.status, 'ok')
        self.assertEqual(result.stdout, 'hello λ\n')

    def test_flood_fails_with_bounded_diagnostics(self):
        result = run_process([sys.executable, '-c',
                              'import sys; sys.stderr.write("x" * 100000)'], 5,
                             output_limit=128)
        self.assertEqual(result.status, 'output-limit')
        self.assertEqual(result.stderr, 'x' * 128)
        self.assertFalse(result.timed_out)

    def test_timeout_keeps_partial_output(self):
        result = run_process([sys.executable, '-c',
                              'import time; print("started", flush=True); time.sleep(60)'],
                             1, output_limit=128)
        self.assertEqual(result.status, 'timeout')
        self.assertEqual(result.stdout, 'started\n')
        self.assertIsNone(result.return_code)


if __name__ == '__main__':
    unittest.main()
