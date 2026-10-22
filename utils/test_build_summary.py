#!/usr/bin/env python3
"""Exercise the Makefile's build summary with isolated sub-make output."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BuildSummaryTest(unittest.TestCase):
    def setUp(self):
        (ROOT / 'temp').mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=ROOT / 'temp')
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        (self.work / 'build/premake').mkdir(parents=True)
        self.fixture = self.work / 'fixture.mk'
        self.fixture.write_text(
            f'include {ROOT}/Makefile\n'
            '.PHONY: summary-check\n'
            'summary-check:\n'
            '\t$(call run_make_with_error_summary,summary-test)\n')
        self.submake = self.work / 'submake.sh'
        self.submake.write_text(
            '#!/bin/sh\n'
            'cat "$BUILD_SUMMARY_INPUT"\n'
            'exit "$BUILD_SUMMARY_EXIT"\n')

    def summarize(self, log, exit_code=0):
        source = self.work / 'input.log'
        source.write_text(log)
        result = subprocess.run(
            ['make', '--no-print-directory', '-f', str(self.fixture), 'summary-check',
             f'MAKE=sh "{self.submake}"', 'JOBS=1'],
            cwd=self.work, env=dict(os.environ, BUILD_SUMMARY_INPUT=str(source),
                                    BUILD_SUMMARY_EXIT=str(exit_code)),
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual((self.work / 'temp/build_summary-test.log').read_text(), log)
        self.assertEqual(result.returncode == 0, exit_code == 0, result.stdout)
        return result.stdout

    def test_macro_warning_excerpts_are_not_errors(self):
        log = ''.join(
            f'../../lambda/dom/dom_api.def:{line}:1: warning: incompatible cast\n'
            f'  {line} | DOM_OP(CORE, {name}, messaging, 2, "fn(n: any, error: bool) -> any")\n'
            '      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n'
            "../../lambda/dom/dom_module.cpp:80:5: note: expanded from macro 'DOM_OP'\n"
            for line, name in [(808, 'broadcast_handler'), (809, 'broadcast_set_handler')] * 4)
        output = self.summarize(log)
        self.assertIn('Errors: 0\nWarnings: 8\n', output)
        self.assertNotIn('ERRORS FOUND', output)

    def test_diagnostic_severity_does_not_come_from_message_or_notes(self):
        output = self.summarize(
            'fixture.cpp:3:2: warning: string contains "error: bool"\n'
            'fixture.cpp:4:2: note: error: is part of the signature\n'
            'fixture.cpp:5:2: note: warning: is part of the signature\n'
            '    4 | const char* message = "error: warning:";\n'
            '      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n')
        self.assertIn('Errors: 0\nWarnings: 1\n', output)

    def test_real_errors_are_reported_even_when_message_mentions_warning(self):
        output = self.summarize(
            'fixture.cpp:7:2: error: unknown type name warning:\n'
            'fixture.h:8: fatal error: missing include\n'
            'clang++: error: linker command failed with exit code 1\n'
            'ld: warning: duplicate library\n'
            'ERROR: build step failed\n', exit_code=2)
        self.assertIn('Errors: 4\nWarnings: 1\n', output)
        self.assertIn('ERRORS FOUND', output)
        self.assertIn(f'file://{self.work}/build/premake/fixture.cpp:7:2', output)
        self.assertIn('unknown type name warning:', output)
        self.assertIn('clang++: error: linker command failed', output)

    def test_failure_without_matching_diagnostic_still_fails(self):
        output = self.summarize('make[1]: *** [lambda] Error 2\n', exit_code=2)
        self.assertIn('Build failed for target:', output)
        self.assertIn('Errors: 0\nWarnings: 0\n', output)

    def test_incremental_build_without_diagnostics(self):
        output = self.summarize('==== Building lambda (debug_native) ====\n')
        self.assertIn('Build completed successfully', output)
        self.assertIn('Errors: 0\nWarnings: 0\n', output)


if __name__ == '__main__':
    unittest.main()
