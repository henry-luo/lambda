#!/usr/bin/env python3
"""Check setup ownership and orchestration without installing system packages."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
INPUT = Path(os.environ.get('LAMBDA_INPUT_ROOT', ROOT.parent / 'lambda-input')).resolve()
RADIANT = Path(os.environ.get('RADIANT_ROOT', ROOT.parent / 'radiant')).resolve()


def plan(project, platform):
    env = dict(os.environ, LAMBDA_INPUT_ROOT=str(INPUT), RADIANT_ROOT=str(RADIANT), MSYSTEM='CLANG64')
    return subprocess.check_output(['bash', str(project / f'setup-{platform}-deps.sh'), '--dry-run'],
                                   cwd=ROOT / 'temp', env=env, text=True).splitlines()


class SetupOwnership(unittest.TestCase):
    def test_lambda_runs_each_owner_once(self):
        for platform in ('mac', 'linux', 'windows'):
            with self.subTest(platform=platform):
                lines = plan(ROOT, platform)
                builds = [line for line in lines if ' vendor-deps curl-deps native-deps' in line]
                self.assertEqual(builds, [f'make -C {INPUT} vendor-deps curl-deps native-deps'])
                self.assertEqual(sum('glfw' in line for line in lines), 1)
                self.assertTrue(any('npm install --ignore-scripts' in line for line in lines))
                if platform == 'linux':
                    self.assertEqual(sum('setup_linux_sources.py' in line for line in lines), 1)

    def test_live_orchestration_and_failure_propagation(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'temp', prefix='setup-test-') as directory:
            scratch = Path(directory)
            log = scratch / 'commands.log'
            stubs = {
                'brew': 'if [ "$1" = list ]; then exit 1; fi; echo "brew $*" >> "$SETUP_TEST_LOG"',
                'npm': 'echo "npm $*" >> "$SETUP_TEST_LOG"',
                'make': 'echo "make $*" >> "$SETUP_TEST_LOG"; exit "${SETUP_TEST_MAKE_STATUS:-0}"',
            }
            for name, body in stubs.items():
                path = scratch / name
                path.write_text('#!/bin/sh\n' + body + '\n')
                path.chmod(0o755)
            env = dict(os.environ, LAMBDA_INPUT_ROOT=str(INPUT), RADIANT_ROOT=str(RADIANT),
                       PATH=str(scratch) + os.pathsep + os.environ['PATH'], SETUP_TEST_LOG=str(log))
            env.pop('SETUP_DRY_RUN', None)
            command = ['bash', str(ROOT / 'setup-mac-deps.sh')]
            subprocess.run(command, cwd=ROOT / 'temp', env=env, check=True, capture_output=True)
            commands = log.read_text().splitlines()
            input_build = next(i for i, line in enumerate(commands) if line.startswith('make '))
            self.assertLess(commands.index('brew install mbedtls@3'), input_build)
            self.assertGreater(commands.index('brew install glfw'), input_build)
            self.assertEqual(sum(line.startswith('make ') for line in commands), 1)
            log.unlink()
            env['SETUP_TEST_MAKE_STATUS'] = '7'
            result = subprocess.run(command, cwd=ROOT / 'temp', env=env, capture_output=True)
            self.assertEqual(result.returncode, 7)
            self.assertNotIn('brew install glfw', log.read_text())

    def test_vendor_recipes_belong_to_input(self):
        host = json.loads((ROOT / 'build_deps_config.json').read_text())
        input_config = json.loads((INPUT / 'build_vendor_deps_config.json').read_text())
        for name in ('thorvg', 'brotli', 'utf8proc', 'curl', 'mbedtls', 'nghttp2', 'mpdecimal'):
            self.assertNotIn(name, host['dependency_builds'])
            self.assertIn(name, input_config['dependency_builds'])
        self.assertFalse(any(entry.get('root_path') == 'build_vendor_deps_config.json'
                             for entry in host['config_imports'] if isinstance(entry, dict)))

    def test_owner_setups_are_independent_of_lambda(self):
        for platform in ('mac', 'linux', 'windows'):
            with self.subTest(platform=platform):
                input_lines = plan(INPUT, platform)
                radiant_lines = plan(RADIANT, platform)
                self.assertFalse(any('npm install' in line or 'glfw' in line for line in input_lines))
                self.assertEqual(sum(' vendor-deps curl-deps native-deps' in line for line in radiant_lines), 1)
                self.assertEqual(sum('glfw' in line for line in radiant_lines), 1)
                self.assertFalse(any('npm install' in line for line in radiant_lines))


if __name__ == '__main__':
    unittest.main()
