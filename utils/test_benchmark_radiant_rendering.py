"""Check the measurement contracts that make rendering comparisons attributable."""

import os
import contextlib
import io
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import benchmark_process
import benchmark_radiant_rendering as rendering


class ProcessMeasurementTest(unittest.TestCase):
    def setUp(self):
        (rendering.ROOT / "temp").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=rendering.ROOT / "temp")
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name) / "output.log"

    def run_child(self, script, timeout=5):
        with self.output.open("wb") as output:
            result = benchmark_process.run_measured([sys.executable, "-c", script],
                stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
        return result

    def test_failure_preserves_output_and_reaps_the_child(self):
        result = self.run_child("import sys; print('child evidence',flush=True); sys.exit(7)")
        self.assertEqual(result["exit_code"], 7)
        self.assertFalse(result["timed_out"])
        self.assertIn("child evidence", self.output.read_text())
        if os.name == "posix":
            with self.assertRaises(ChildProcessError):
                os.waitpid(result["pid"], os.WNOHANG)

    @unittest.skipUnless(callable(getattr(os, "wait4", None)), "per-child RSS requires wait4")
    def test_small_child_does_not_inherit_the_previous_large_child_peak(self):
        large = self.run_child("data=bytearray(64*1024*1024); print(len(data),flush=True)")
        small = self.run_child("print('small',flush=True)")
        self.assertEqual(large["resource_accounting"], "wait4")
        self.assertGreater(large["peak_rss_bytes"] - small["peak_rss_bytes"], 32 * 1024 * 1024)
        self.assertGreaterEqual(small["user_seconds"], 0)

    def test_timeout_reaps_the_child(self):
        result = self.run_child("import time; time.sleep(30)", timeout=.1)
        self.assertTrue(result["timed_out"])
        self.assertNotEqual(result["exit_code"], 0)
        if os.name == "posix":
            with self.assertRaises(ChildProcessError):
                os.waitpid(result["pid"], os.WNOHANG)

    @unittest.skipUnless(os.name == "posix", "process groups require POSIX")
    def test_timeout_closes_output_inherited_by_a_descendant(self):
        reader, writer = os.pipe()
        script = ("import subprocess,sys,time; child=subprocess.Popen([sys.executable,'-c',"
                  "'import time; time.sleep(30)']); print('ready:'+str(child.pid),flush=True); time.sleep(30)")
        with os.fdopen(writer, "wb") as output:
            result = benchmark_process.run_measured([sys.executable, "-c", script],
                stdout=output, stderr=subprocess.STDOUT, timeout=1)
        evidence = b""
        closed = False
        deadline = time.monotonic() + 2
        with os.fdopen(reader, "rb", buffering=0) as source:
            while time.monotonic() < deadline:
                if not select.select([source], [], [], max(0, deadline - time.monotonic()))[0]:
                    break
                chunk = source.read(4096)
                if not chunk:
                    closed = True
                    break
                evidence += chunk
        if not closed and b"ready:" in evidence:
            os.kill(int(evidence.split(b"ready:")[1].splitlines()[0]), signal.SIGKILL)
        self.assertTrue(result["timed_out"])
        self.assertIn(b"ready:", evidence)
        self.assertTrue(closed, "a descendant retained the pipe after timeout cleanup")

    def test_missing_wait4_reports_unknown_resources(self):
        with mock.patch.object(benchmark_process.os, "wait4", None, create=True):
            result = self.run_child("pass")
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(result["resource_accounting"], "unavailable")
        self.assertIsNone(result["peak_rss_bytes"])

    def test_invalid_timeout_does_not_start_a_process(self):
        with mock.patch.object(benchmark_process.subprocess, "Popen") as launch:
            for timeout in (0, -1, float("nan"), float("inf")):
                with self.assertRaises(ValueError):
                    benchmark_process.run_measured([sys.executable], timeout=timeout)
            launch.assert_not_called()


def profile_text():
    return "\n".join(f"[RENDER_METRICS] stage={stage} ms=1.000" for stage in rendering.STAGES) + "\n" + \
        "[RENDER_METRICS] tracked_start_bytes=100 tracked_end_bytes=200 tracked_process_peak_bytes=300\n" + \
        "[RENDER_METRICS] scope_total_ms=8.010 native_internal_bytes=untracked\n" + \
        "[RENDER_METRICS] full_frame_reuses=1\n"


class RenderingReceiptTest(unittest.TestCase):
    def test_existing_output_is_rejected_before_any_evidence_changes(self):
        with tempfile.TemporaryDirectory(dir=rendering.ROOT / "temp") as directory:
            directory = Path(directory)
            evidence = directory / "evidence.txt"
            evidence.write_bytes(b"original receipt")
            binary = directory / "release-marker-control"
            binary.write_bytes(b"no debug validator trace")
            argv = ["benchmark", "--executable", str(binary), "--out", str(directory)]
            with mock.patch.object(sys, "argv", argv), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as stopped:
                    rendering.main()
            self.assertEqual(stopped.exception.code, 2)
            self.assertEqual(evidence.read_bytes(), b"original receipt")
            self.assertEqual({p.name for p in directory.iterdir()}, {"evidence.txt", "release-marker-control"})

    def test_profile_preserves_memory_fields_and_excludes_encoding_from_frame_work(self):
        frames = rendering.profile_frames(profile_text() * 3)
        summary = rendering.summarize_profile(frames, 2)
        self.assertEqual(summary["measured_frames"], 1)
        self.assertEqual(summary["frame_work_ms"]["median"], 7)
        self.assertEqual(summary["scope_total_ms"]["median"], 8.010)
        self.assertEqual(summary["tracked_end_bytes"]["median"], 200)
        self.assertEqual(summary["tracked_process_peak_bytes"], 300)
        self.assertEqual(summary["full_frame_reuses"], 1)

    def test_truncated_or_repeated_stage_is_rejected(self):
        for bad in (profile_text().replace("stage=teardown", "stage=unknown"),
                    profile_text().replace("[RENDER_METRICS] scope_total_ms", "[OTHER] scope_total_ms"),
                    profile_text() + "[RENDER_METRICS] stage=replay ms=1.000\n"):
            with self.assertRaises(ValueError):
                rendering.profile_frames(bad)
        with self.assertRaises(ValueError):
            rendering.summarize_profile(rendering.profile_frames(profile_text()), 1)

    def test_overlapping_stage_times_cannot_pass_as_a_complete_scope(self):
        with self.assertRaises(ValueError):
            rendering.profile_frames(profile_text().replace("scope_total_ms=8.010", "scope_total_ms=1.000"))

    def test_missing_allocator_tracking_cannot_pass_as_a_profile(self):
        with self.assertRaises(ValueError):
            rendering.profile_frames(profile_text().replace("tracked_end_bytes=200", "tracked_end_bytes=0"))

    def rows(self):
        return [dict(variant=variant, repeat=i, png="same", wall_seconds=value,
                     peak_rss_bytes=100, user_seconds=1, system_seconds=1)
                for variant, values in (("off", (1, 2, 100)), ("on", (3, 20, 110)))
                for i, value in enumerate(values)]

    def test_ratios_are_paired_by_repeat(self):
        summary = rendering.summarize_samples(self.rows(), ("off", "on"), 3)
        self.assertEqual(summary["paired_ratios"]["wall_ms"]["median"], 3)
        self.assertEqual(summary["ratio_numerator"], "on")

    def test_pixel_mismatch_missing_or_duplicate_repeat_is_rejected(self):
        rows = self.rows()
        for bad in (rows[:-1], rows[:-1] + [rows[0]], rows[:-1] + [dict(rows[-1], png="different")]):
            with self.assertRaises(ValueError):
                rendering.summarize_samples(bad, ("off", "on"), 3)


if __name__ == "__main__":
    unittest.main()
