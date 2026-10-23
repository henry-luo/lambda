"""Keep the frame-15 checksum inside the complete Navier timed workload."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import unittest
from unittest.mock import patch

import run_benchmarks as registry


SOURCE = Path(registry.JETSTREAM_NODE['navier_stokes'])


class NavierWorkloadTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'), 'Node is not installed')
    def test_js_timer_and_density_oracle_observe_the_same_completed_state(self):
        expression, repeats = registry._detect_jetstream_run_function(SOURCE)
        trailer = registry.jetstream_timing_trailer(expression, repeats)
        oracle = registry.jetstream_post_timing_oracle(registry.NAVIER_STOKES_DENSITY_ORACLE_V1)
        # Observe real simulation progress at both clock calls, not just the loop spelling.
        script = '''const vm = require('vm');
const input = JSON.parse(require('fs').readFileSync(0, 'utf8'));
const clocks = [], output = [];
const context = vm.createContext({console: {log: value => output.push(value)},
    performance: {now: () => {clocks.push(context.nsFrameCounter); return clocks.length;}}});
vm.runInContext(input.source + input.trailer, context);
if (JSON.stringify(clocks) !== '[0,15]' || context.result !== 77)
    throw Error('timer did not enclose 15 checksum-verified frames');
vm.runInContext(input.oracle, context);
if (context.nsFrameCounter !== 15 || !output.includes('__NAVIER_DENSITY_DIGEST__:-257786486'))
    throw Error('oracle advanced or failed to validate the measured state');
'''
        proc = subprocess.run(['node', '-e', script], input=json.dumps({
            'source': SOURCE.read_text(), 'trailer': trailer, 'oracle': oracle}),
            text=True, capture_output=True, timeout=30)
        self.assertEqual(0, proc.returncode, proc.stdout + proc.stderr)

    @unittest.skipUnless(shutil.which('node'), 'Node is not installed')
    def test_js_checksum_failure_rejects_the_timed_workload(self):
        expression, repeats = registry._detect_jetstream_run_function(SOURCE)
        source = SOURCE.read_text().replace('this.result!=77', 'this.result!=78')
        self.assertNotEqual(source, SOURCE.read_text())
        proc = subprocess.run(['node'], input=source + registry.jetstream_timing_trailer(expression, repeats),
                              text=True, capture_output=True, timeout=30)
        self.assertNotEqual(0, proc.returncode)
        self.assertIn('checksum failed', proc.stderr)
        self.assertNotIn('__TIMING__:', proc.stdout)

    def test_python_timer_ends_after_all_frames_and_checksum(self):
        spec = importlib.util.spec_from_file_location('navier_python',
            SOURCE.with_name('navier_stokes.py'))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        frames = 0
        clocks = []
        velocity_step = module.vel_step

        def step(*args):
            nonlocal frames
            velocity_step(*args)
            frames += 1

        def clock():
            clocks.append(frames)
            return len(clocks) * 1_000_000

        with patch.object(module, 'vel_step', step), \
                patch.object(module.time, 'perf_counter_ns', clock):
            checksum, elapsed = module.run_navier_stokes()
        self.assertEqual([0, 15], clocks)
        self.assertEqual(77, checksum)
        self.assertEqual(1.0, elapsed)


if __name__ == '__main__':
    unittest.main()
