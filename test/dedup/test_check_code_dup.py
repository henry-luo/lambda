#!/usr/bin/env python3
"""Unit tests for the duplicate-code scan wrapper."""

import importlib.util
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).with_name("check_code_dup.py")
SPEC = importlib.util.spec_from_file_location("check_code_dup", SCRIPT_PATH)
CHECK_CODE_DUP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK_CODE_DUP)


class BuildLizardCommandTest(unittest.TestCase):
    def test_forces_cpp_language_before_scan_paths(self):
        command = CHECK_CODE_DUP.build_lizard_command(
            "/usr/local/bin/lizard",
            ["lambda/generated.c"],
            ("lib", "lambda"),
        )

        self.assertEqual(
            command,
            [
                "/usr/local/bin/lizard",
                "-Eduplicate",
                "-l",
                "cpp",
                "-x",
                "lambda/generated.c",
                "lib",
                "lambda",
            ],
        )


if __name__ == "__main__":
    unittest.main()
