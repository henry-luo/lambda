"""Check manifest revision and build regeneration behavior for native schemas."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from generate_binary_bindings import generate


ROOT = Path(__file__).resolve().parent.parent


class BinaryBindingGeneratorTest(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads((ROOT / 'test/binary_schema_fixture.json').read_text())

    def test_host_extent_changes_invalidate_the_complete_contract(self):
        changed = copy.deepcopy(self.manifest)
        changed['records'][1]['fields'][0]['capacity'] = 'other_capacity'
        before, after = generate(self.manifest), generate(changed)
        self.assertNotEqual(before['version'], after['version'])
        self.assertNotEqual(before['lambda'], after['lambda'])

    def test_pointer_count_cannot_replace_host_extent(self):
        del self.manifest['records'][1]['fields'][0]['capacity']
        with self.assertRaisesRegex(ValueError, 'host capacity'):
            generate(self.manifest)

    def test_recursive_manifest_targets_are_rejected(self):
        self.manifest['records'][0]['fields'][0].update(type='NativeBatch?', pointer=True)
        with self.assertRaisesRegex(ValueError, 'precede'):
            generate(self.manifest)

    def test_regeneration_is_idempotent_and_check_detects_stale_files(self):
        (ROOT / 'temp').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / 'temp') as directory:
            base = Path(directory)
            for kind in self.manifest['outputs']:
                self.manifest['outputs'][kind] = str(base / Path(self.manifest['outputs'][kind]).name)
            source = base / 'manifest.json'
            source.write_text(json.dumps(self.manifest))
            command = [sys.executable, str(ROOT / 'utils/generate_binary_bindings.py'), str(source)]
            subprocess.run(command, check=True)
            target = Path(self.manifest['outputs']['cpp'])
            stamp = target.stat().st_mtime_ns
            subprocess.run(command, check=True)
            self.assertEqual(target.stat().st_mtime_ns, stamp)
            subprocess.run(command + ['--check'], check=True)
            target.write_text(target.read_text() + '// stale\n')
            result = subprocess.run(command + ['--check'], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('output is stale', result.stderr)
            subprocess.run(command, check=True)
            subprocess.run(command + ['--check'], check=True)


if __name__ == '__main__':
    unittest.main()
