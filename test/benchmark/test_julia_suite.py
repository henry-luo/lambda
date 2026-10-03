"""Contract, language coverage and strict result validation for the new suite."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import unittest

import run_benchmarks as registry
import run_c2mir_benchmarks as c2mir
import run_go_benchmarks as go
import run_julia_benchmarks as julia
import verify_julia_suite as verifier
from js_benchmark_manifest import build_workloads, TUNE14_V1_PROFILE


class JuliaMicroSuiteTests(unittest.TestCase):
    def test_independent_oracles(self):
        expected = json.loads((verifier.SUITE / "expected.json").read_text())
        self.assertEqual(expected, verifier.contract_oracle())

    def test_manifest_and_all_language_entries(self):
        expected = json.loads((verifier.SUITE / "expected.json").read_text())
        entries = registry.build_benchmark_list(["julia"], [], include_text=False)
        self.assertEqual(set(expected), {entry["name"] for entry in entries})
        for name, _, ls_path, js_path, py_path in registry.JULIA_MICRO:
            with self.subTest(name=name):
                self.assertTrue(all(Path(path).is_file() for path in (ls_path, js_path, py_path)))
                self.assertTrue(go.has_port("julia", name))
                self.assertIsNotNone(julia.port_source("julia", name))
                source = c2mir.port_source("julia", name)
                self.assertIsNotNone(source)
                self.assertNotIn(str(c2mir.TIMER_MAIN), c2mir.build_command(source))
                golden = name + ": PASS " + " ".join(map(str, expected[name])) + "\n"
                for suffix in ("", "2"):
                    self.assertEqual(golden, (verifier.SUITE / f"{name}{suffix}.txt").read_text())
                    self.assertTrue((verifier.SUITE / f"{name}{suffix}.ls").is_file())

    def test_typed_and_untyped_algorithms_are_identical(self):
        typed = (verifier.SUITE / "micro_common2.ls").read_text()
        stripped = re.sub(r": (?:int|float|string|bool)(?:\[\])?", "", typed)
        stripped = re.sub(r"\) (?:int|float|string|bool)(?:\[\])? \{", ") {", stripped)
        self.assertEqual(stripped.replace("Shared typed scalar", "Shared untyped scalar"),
                         (verifier.SUITE / "micro_common.ls").read_text())

    def test_shared_js_bundle_preserves_workload(self):
        common = (verifier.SUITE / "micro_common.js").read_text()
        for name in verifier.NAMES:
            path = verifier.SUITE / f"{name}.js"
            expanded = Path(registry.expand_benchmark_js(str(path))).read_text()
            self.assertEqual(common + f'runJuliaMicro("{name}");\n', expanded)
        rows = registry.build_benchmark_list(["julia"], [], include_text=False)
        frozen = build_workloads(vars(registry), rows, TUNE14_V1_PROFILE)
        for workload in frozen:
            self.assertTrue(Path(workload["source"]).read_text().startswith(common))

    @unittest.skipUnless(shutil.which(registry.QJS_EXE), "QuickJS is not installed")
    def test_quickjs_synchronous_output_adapter_writes_and_reports_errors(self):
        directory = verifier.ROOT / "temp/julia-suite-tests"
        directory.mkdir(parents=True, exist_ok=True)
        output = str(directory / "output.txt")
        missing = str(directory / "absent-parent" / "output.txt")
        wrapper = str(directory / "output_adapter.js")
        code = ("var fs = require('fs');\n"
                f"fs.writeFileSync({json.dumps(output)}, '12 13\\n');\n"
                f"if (fs.readFileSync({json.dumps(output)}, 'utf8') !== '12 13\\n') throw new Error('wrong bytes');\n"
                "var failed = false;\n"
                f"try {{ fs.writeFileSync({json.dumps(missing)}, 'x'); }} catch (error) {{ failed = true; }}\n"
                "if (!failed) throw new Error('write error was ignored');\n")
        registry.write_qjs_script_wrapper(wrapper, code)
        result = subprocess.run([registry.QJS_EXE, "--std", "-m", wrapper], capture_output=True,
                                text=True, timeout=15)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual("12 13\n", Path(output).read_text())

    def test_partial_failed_or_multiple_timer_output_is_rejected(self):
        values = [1177795, 584298900, 391, 100000]
        line = "formatted_output: PASS " + " ".join(map(str, values)) + "\n"
        good = subprocess.CompletedProcess([], 0, line + "__TIMING__:1.25\n", "")
        self.assertEqual(1.25, verifier.checked_output(good, "formatted_output", values))
        for returncode, output in [
            (1, good.stdout), (0, ""), (0, "__TIMING__:1\n"),
            (0, line.replace("391", "390") + "__TIMING__:1\n"),
            (0, line + "__TIMING__:1\n__TIMING__:2\n"),
            (0, line + "__TIMING__:0\n"), (0, line + "__TIMING__:1e999\n"),
            (0, line + "__TIMING__:1junk\n"),
        ]:
            with self.subTest(output=output):
                failed = subprocess.CompletedProcess([], returncode, output, "")
                self.assertIsNone(verifier.checked_output(failed, "formatted_output", values))


if __name__ == "__main__":
    unittest.main()
