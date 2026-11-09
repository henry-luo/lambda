#!/usr/bin/env python3
"""Check that build manifests resolve owned paths without renaming targets."""

import os
import json
from pathlib import Path
import unittest
from unittest.mock import patch

from split_paths import logical_project_path, resolve_split_paths


class SplitPathsTest(unittest.TestCase):
    def test_decimal_runtime_provider_is_selected_by_each_host_profile(self):
        root = Path(__file__).resolve().parent.parent
        config = json.loads((root / 'build_lambda_config.json').read_text())
        provider = 'lambda/runtime/lambda-decimal-runtime.cpp'
        self.assertNotIn(provider, config['exclude_source_files'])
        self.assertNotIn(provider, config['platforms']['cli']['exclude_source_files'])
        self.assertIn(provider, config['platforms']['lambda-wasm']['source_files'])
        self.assertNotIn('lambda-decimal.cpp', (root / provider).read_text())

    def test_roots_and_target_names(self):
        root = Path(__file__).resolve().parent.parent
        with patch.dict(os.environ, {'LAMBDA_INPUT_ROOT': str(root / 'temp/input-root'),
                                     'RADIANT_ROOT': str(root / 'temp/radiant-root')}):
            resolved = resolve_split_paths({
                'source_dirs': ['lib', 'radiant/scene3d', 'lambda'],
                'includes': ['.'],
                'targets': [{'name': 'radiant', 'libraries': ['radiant', 'lambda-lib'],
                             'source_patterns': ['radiant/*.cpp', 'lib/*.c']}],
                'test': {'source': 'lib/test_example.cpp'},
            })
        self.assertEqual(resolved['source_dirs'],
                         ['temp/input-root/lib', 'temp/radiant-root/radiant/scene3d', 'lambda'])
        self.assertEqual(resolved['targets'][0]['name'], 'radiant')
        self.assertEqual(resolved['targets'][0]['libraries'], ['radiant', 'lambda-lib'])
        self.assertEqual(resolved['targets'][0]['source_patterns'],
                         ['temp/radiant-root/radiant/*.cpp', 'temp/input-root/lib/*.c'])
        self.assertEqual(resolved['test']['source'], 'lib/test_example.cpp')
        self.assertIn('temp/input-root', resolved['includes'])
        self.assertIn('temp/radiant-root', resolved['includes'])

    def test_audit_names_preserve_sibling_ownership(self):
        root = Path(__file__).resolve().parent.parent
        input_root = root.parent / 'input-example'
        radiant_root = root.parent / 'radiant-example'
        with patch.dict(os.environ, {'LAMBDA_INPUT_ROOT': str(input_root),
                                     'RADIANT_ROOT': str(radiant_root)}):
            self.assertEqual(logical_project_path(input_root / 'lib/log.h', root), Path('lib/log.h'))
            self.assertEqual(logical_project_path(radiant_root / 'radiant/view.hpp', root),
                             Path('radiant/view.hpp'))


if __name__ == '__main__':
    unittest.main()
