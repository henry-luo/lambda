#!/usr/bin/env python3
"""Verify document variants and child exit reporting in the input-events layout runner."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipIf(os.name == "nt", "the synthetic child uses POSIX signals")
class InputLayoutRunnerTest(unittest.TestCase):
    def setUp(self):
        (ROOT / "temp").mkdir(exist_ok=True)
        self.workspace = Path(tempfile.mkdtemp(prefix="wpt-layout-runner-", dir=ROOT / "temp"))
        (self.workspace / "temp").mkdir()
        for name in ("test", "ref"):
            (self.workspace / name).symlink_to(ROOT / name, target_is_directory=True)

    def tearDown(self):
        shutil.rmtree(self.workspace)

    def run_child(self, ending):
        child = self.workspace / "lambda.exe"
        child.write_text('#!/bin/sh\necho "WPT_RESULT: 1/1 passed"\n' + ending + '\n')
        child.chmod(0o755)
        return self.run_wpt("*textarea_insertfrompaste_type_inputevent_data_html")

    def run_wpt(self, case_filter):
        return subprocess.run(
            [str(ROOT / "test/test_wpt_input_events_gtest.exe"),
             f"--gtest_filter={case_filter}"],
            cwd=self.workspace, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=20, check=False,
            env={**os.environ, "LAMBDA_HOME": str(ROOT / "lmd")},
        )

    def test_successful_child_is_reported(self):
        result = self.run_child("exit 0")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("[  PASSED  ] 1 test", result.stdout)

    def test_nonzero_child_cannot_pass_after_printing_summary(self):
        result = self.run_child("exit 42")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("Layout child failed", result.stdout)

    def test_signalled_child_cannot_pass_after_printing_summary(self):
        result = self.run_child("kill -TERM $$")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("Layout child failed", result.stdout)

    def test_variants_reach_the_real_document_url(self):
        (self.workspace / "ref").unlink()
        fixtures = self.workspace / "ref/wpt/input-events"
        fixtures.mkdir(parents=True)
        (self.workspace / "lambda.exe").symlink_to(ROOT / "lambda.exe")
        (fixtures / "variant-location.html").write_text("""<!doctype html>
<meta name="variant" content="?file=$LAMBDA_WPT_QUERY.md">
<meta name="variant" content="?file=example.html#example.svg">
<script>
test(() => {
  const suffix = location.search + location.hash;
  assert_in_array(suffix, ["?file=$LAMBDA_WPT_QUERY.md", "?file=example.html#example.svg"]);
  assert_true(document.URL.endsWith(suffix));
  assert_equals(location, document.location);
}, "The variant initializes Location");
</script>
""")
        result = self.run_wpt("*variant_location*")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("[  PASSED  ] 2 tests.", result.stdout)

    def test_redirect_releases_layout_created_during_script_load(self):
        (self.workspace / "source.html").write_text("""<!doctype html>
<div id="box">Text</div>
<script>
document.getElementById("box").getBoundingClientRect();
location = "missing.html";
</script>
""")
        result = subprocess.run(
            [str(ROOT / "lambda.exe"), "layout", "source.html", "--auto-close",
             "--no-log", "--view-output", "temp/source.json"],
            cwd=self.workspace, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=20, check=False,
            env={**os.environ, "MEMTRACK_MODE": "DEBUG", "LAMBDA_HOME": str(ROOT / "lmd")},
        )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Completed layout command: 0 success, 1 failed", result.stdout)
        self.assertNotIn("memtrack-console: ERR!", result.stdout)

    def test_concurrent_suites_use_distinct_documents(self):
        child = self.workspace / "lambda.exe"
        child.write_text('#!/bin/sh\necho "$2" >> documents\necho "WPT_RESULT: 1/1 passed"\n')
        child.chmod(0o755)
        args = [str(ROOT / "test/test_wpt_input_events_gtest.exe"),
                "--gtest_filter=*textarea_insertfrompaste_type_inputevent_data_html"]
        processes = [subprocess.Popen(args, cwd=self.workspace, stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, text=True) for _ in range(2)]
        for process in processes:
            output, _ = process.communicate(timeout=20)
            self.assertEqual(process.returncode, 0, output)
        documents = (self.workspace / "documents").read_text().splitlines()
        self.assertEqual(len(documents), 2)
        self.assertNotEqual(documents[0], documents[1])


if __name__ == "__main__":
    unittest.main()
