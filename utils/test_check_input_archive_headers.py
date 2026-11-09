#!/usr/bin/env python3
"""Check dependency ownership after the sibling source links are removed."""

from pathlib import Path
import tempfile
import unittest

from check_input_archive_headers import (INPUT_ROOT, RADIANT_ROOT, ROOT,
                                         dependency_file, input_dependencies)


class InputArchiveDependencyTest(unittest.TestCase):
    def test_sibling_dependencies_keep_their_logical_owners(self):
        (ROOT / 'temp').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / 'temp') as directory:
            depfile = Path(directory) / 'object.d'
            dependencies = [INPUT_ROOT / 'lib/log.h', RADIANT_ROOT / 'radiant/view.hpp',
                            ROOT / 'lambda/runtime/runtime-state.h']
            depfile.write_text('object.o: ' + ' '.join(map(str, dependencies)) + '\n')
            logical = list(input_dependencies(depfile))
        self.assertEqual(logical, [Path('lib/log.h'), Path('radiant/view.hpp'),
                                   Path('lambda/runtime/runtime-state.h')])
        self.assertEqual(dependency_file(logical[0]), dependencies[0])
        self.assertEqual(dependency_file(logical[1]), dependencies[1])


if __name__ == '__main__':
    unittest.main()
