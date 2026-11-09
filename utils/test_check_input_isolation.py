#!/usr/bin/env python3
"""Check that isolation audits the current target rather than stale objects."""

from pathlib import Path
import os
import tempfile
import unittest

from check_input_isolation import ROOT, stage_file, target_dependency_files
from check_input_archive_headers import input_dependencies


class InputIsolationManifestTest(unittest.TestCase):
    def test_stages_each_sibling_without_lambda_directory_links(self):
        (ROOT / "temp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="input-owners-", dir=ROOT / "temp") as directory:
            parent = Path(directory)
            stage = parent / 'Lambda'
            stage.mkdir()
            for relative in ('lib/log.h', 'radiant/view.hpp', 'radiant/view_prop_defaults.cpp',
                             'radiant/test/test_layout_custom_gtest.cpp', 'lambda/core/data.hpp'):
                stage_file(stage, Path(relative))
            self.assertTrue((parent / 'lambda-input/lib/log.h').is_file())
            self.assertTrue((parent / 'radiant/radiant/view.hpp').is_file())
            self.assertTrue((parent / 'radiant/radiant/view_prop_defaults.cpp').is_file())
            self.assertTrue((parent / 'radiant/test/test_layout_custom_gtest.cpp').is_file())
            self.assertTrue((parent / 'lambda-input/lambda/core/data.hpp').is_file())
            self.assertTrue((stage / 'lambda/core/data.hpp').is_file())
            self.assertFalse((stage / 'lib').exists())
            self.assertFalse((stage / 'radiant').exists())

    def test_generated_dependency_paths_use_their_compiler_working_directory(self):
        (ROOT / 'temp').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='input-depfiles-', dir=ROOT / 'temp') as directory:
            depfile = Path(directory) / 'test.d'
            cwd = ROOT / 'build/premake'
            source = ROOT / 'test/test_dom_range_gtest.cpp'
            depfile.write_text('test.o: ' + os.path.relpath(source, cwd) + '\n')
            self.assertIn(Path('test/test_dom_range_gtest.cpp'), list(input_dependencies(depfile, cwd)))

    def test_current_objects_exclude_stale_dependencies_and_require_each_member(self):
        (ROOT / "temp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="input-manifest-", dir=ROOT / "temp") as directory:
            root = Path(directory)
            makefile = root / "target.make"
            makefile.write_text("OBJECTS += $(OBJDIR)/current.o\n")
            (root / "current.d").write_text("current.o: input.hpp\n")
            (root / "retired.d").write_text("retired.o: lambda/runtime/old.hpp\n")
            self.assertEqual(target_dependency_files(makefile, root), [root / "current.d"])
            makefile.write_text("OBJECTS += $(OBJDIR)/current.o\nOBJECTS += $(OBJDIR)/missing.o\n")
            with self.assertRaisesRegex(RuntimeError, "missing compiler dependency"):
                target_dependency_files(makefile, root)


if __name__ == "__main__":
    unittest.main()
