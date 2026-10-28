"""The coherent-header ratchet distinguishes relocation from header growth."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "radiant_header_lint",
    REPO / "utils/lint/rules/structural/no_new_per_file_header.py",
)
LINT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LINT)


class HeaderRelocationTests(unittest.TestCase):
    def setUp(self):
        scratch = REPO / "temp/scene3d-gaming"
        scratch.mkdir(parents=True, exist_ok=True)
        self.scratch = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)

    def header(self, name, tracked=False):
        path = self.root / "radiant" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#pragma once\nvoid scene_operation();\n")
        if tracked:
            subprocess.run(["git", "add", str(path)], cwd=self.root, check=True)
        return path

    def relocations(self):
        return LINT.relocated_headers(
            self.root, LINT.radiant_headers(self.root), LINT.git_status_paths(self.root)
        )

    def test_unstaged_move_preserves_existing_header(self):
        old = self.header("scene.hpp", tracked=True)
        destination = self.root / "radiant/scene/scene.hpp"
        destination.parent.mkdir()
        old.rename(destination)
        self.assertEqual(self.relocations(), {"radiant/scene/scene.hpp"})

    def test_untracked_directory_exposes_each_new_header(self):
        self.header("new/new.hpp")
        self.assertEqual(LINT.git_status_paths(self.root)["radiant/new/new.hpp"], "??")
        self.assertEqual(self.relocations(), set())

    def test_copy_does_not_count_as_relocation(self):
        self.header("scene.hpp", tracked=True)
        self.header("scene/scene.hpp")
        self.assertEqual(self.relocations(), set())

    def test_ambiguous_originals_are_not_grandfathered(self):
        self.header("a/scene.hpp", tracked=True).unlink()
        self.header("b/scene.hpp", tracked=True).unlink()
        self.header("scene/scene.hpp")
        self.assertEqual(self.relocations(), set())


if __name__ == "__main__":
    unittest.main()
