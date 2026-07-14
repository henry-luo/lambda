#!/usr/bin/env python3
"""Unit tests for the duplicate-code scan wrapper."""

import importlib.util
import io
import unittest
from contextlib import redirect_stdout
from pathlib import Path


SCRIPT_PATH = Path(__file__).with_name("check_code_dup.py")
SPEC = importlib.util.spec_from_file_location("check_code_dup", SCRIPT_PATH)
CHECK_CODE_DUP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK_CODE_DUP)


def location(file_name, start, end):
    return {
        "text": "%s:%d ~ %d" % (file_name, start, end),
        "file": file_name,
        "start": start,
        "end": end,
    }


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


class FileExclusionConfigTest(unittest.TestCase):
    def test_lambda_excludes_every_vendored_tree_sitter_tree(self):
        config = CHECK_CODE_DUP.load_config(SCRIPT_PATH.with_name("exclude.json"))

        self.assertEqual(
            CHECK_CODE_DUP.active_file_exclusions(config, ("lambda",)),
            ["lambda/lambda-embed.h", "lambda/tree-sitter*"],
        )

    def test_lambda_css_table_is_the_only_reviewed_block_exclusion(self):
        config = CHECK_CODE_DUP.load_config(SCRIPT_PATH.with_name("exclude.json"))
        root = SCRIPT_PATH.resolve().parents[2]

        rules = CHECK_CODE_DUP.active_block_exclusions(config, ("lambda",), root)

        self.assertEqual([rule["id"] for rule in rules], ["declarative_css_property_rows"])
        self.assertTrue(rules[0]["allow_within_region"])
        region = rules[0]["regions"][0]
        inside = [[location(region["file"], region["start"] + 1, region["end"] - 1)]]
        outside = [[location(region["file"], region["end"] + 1, region["end"] + 2)]]
        self.assertEqual(CHECK_CODE_DUP.exclusion_for_block(inside[0], rules), rules[0])
        self.assertIsNone(CHECK_CODE_DUP.exclusion_for_block(outside[0], rules))

    def test_lambda_ratchet_baseline_is_configured(self):
        baselines = CHECK_CODE_DUP.load_baselines(SCRIPT_PATH.with_name("baseline.json"))

        self.assertEqual(
            baselines["lambda"],
            {
                "family_count": 1385,
                "union_duplicate_lines": 56478,
                "diagnostic_raw_blocks": 3520,
                "diagnostic_remaining_blocks": 3340,
            },
        )


class CloneFamilyTest(unittest.TestCase):
    def setUp(self):
        self.blocks = [
            [location("a.cpp", 1, 4), location("b.cpp", 10, 13)],
            [location("a.cpp", 3, 6), location("b.cpp", 20, 23)],
            [location("a.cpp", 30, 33), location("b.cpp", 40, 43)],
            [location("a.cpp", 2, 5), location("c.cpp", 50, 53)],
        ]

    def test_merges_overlaps_only_within_the_same_file_set(self):
        families = CHECK_CODE_DUP.cluster_duplicate_families(self.blocks)

        self.assertEqual(len(families), 3)
        self.assertEqual(
            sorted(len(family["blocks"]) for family in families),
            [1, 1, 2],
        )

    def test_reports_union_lines_and_family_kinds(self):
        families = CHECK_CODE_DUP.cluster_duplicate_families(self.blocks)

        metrics = CHECK_CODE_DUP.duplicate_metrics(5, self.blocks, families)

        self.assertEqual(metrics["raw_block_count"], 5)
        self.assertEqual(metrics["remaining_block_count"], 4)
        self.assertEqual(metrics["family_count"], 3)
        self.assertEqual(metrics["same_file_family_count"], 0)
        self.assertEqual(metrics["cross_file_family_count"], 3)
        self.assertEqual(metrics["union_lines_by_file"]["a.cpp"], 10)
        self.assertEqual(metrics["union_duplicate_lines"], 26)


class ReportAndRatchetTest(unittest.TestCase):
    def setUp(self):
        self.blocks = [[location("a.cpp", 1, 4), location("b.cpp", 10, 13)]]
        families = CHECK_CODE_DUP.cluster_duplicate_families(self.blocks)
        self.metrics = CHECK_CODE_DUP.duplicate_metrics(1, self.blocks, families)
        self.baseline = {"family_count": 1, "union_duplicate_lines": 8}

    def render(self, full):
        output = io.StringIO()
        with redirect_stdout(output):
            CHECK_CODE_DUP.print_report(
                self.metrics,
                self.blocks,
                {},
                ("lambda",),
                2,
                self.baseline,
                full,
            )
        return output.getvalue()

    def test_default_report_is_summary_only(self):
        output = self.render(False)

        self.assertIn("First-party clone families: 1", output)
        self.assertIn("Ratchet: PASS", output)
        self.assertNotIn("Duplicate block:", output)

    def test_full_report_preserves_every_location(self):
        output = self.render(True)

        self.assertIn("Duplicate block:", output)
        self.assertIn("a.cpp:1 ~ 4", output)
        self.assertIn("b.cpp:10 ~ 13", output)

    def test_ratchet_fails_each_growth_dimension(self):
        self.assertEqual(
            CHECK_CODE_DUP.ratchet_failures(
                {"family_count": 2, "union_duplicate_lines": 9},
                self.baseline,
            ),
            [
                "family_count grew from 1 to 2",
                "union_duplicate_lines grew from 8 to 9",
            ],
        )


if __name__ == "__main__":
    unittest.main()
