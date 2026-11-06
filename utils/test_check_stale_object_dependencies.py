import tempfile
import unittest
from pathlib import Path

from check_stale_object_dependencies import retire_stale_objects


class StaleObjectDependenciesTest(unittest.TestCase):
    def test_only_changed_source_is_retired(self):
        base = Path(__file__).resolve().parent.parent / "temp"
        base.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as directory:
            root = Path(directory)
            makefile = root / "project.make"
            makefile.write_text("$(OBJDIR)/moved.o: new/source.cpp\n"
                                "$(OBJDIR)/same.o: current/source.cpp\n")
            objects = root / "obj/native/release"
            objects.mkdir(parents=True)
            for name, source in (("moved", "old/source.cpp"), ("same", "current/source.cpp"),
                                 ("unused", "old/unused.cpp")):
                (objects / f"{name}.o").write_text("object")
                (objects / f"{name}.d").write_text(f"{name}.o: \\\n  {source} header.h\n")
            self.assertEqual(retire_stale_objects(makefile, root / "obj"), [objects / "moved.o"])
            self.assertFalse((objects / "moved.d").exists())
            self.assertFalse((objects / "moved.o").exists())
            self.assertTrue((objects / "same.o").exists())
            self.assertTrue((objects / "unused.d").exists())

    def test_rejects_ambiguous_active_rules(self):
        base = Path(__file__).resolve().parent.parent / "temp"
        base.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as directory:
            root = Path(directory)
            makefile = root / "project.make"
            makefile.write_text("$(OBJDIR)/x.o: one.cpp\n$(OBJDIR)/x.o: two.cpp\n")
            with self.assertRaises(RuntimeError):
                retire_stale_objects(makefile, root / "obj")

    def test_malformed_dependency_does_not_delete_object(self):
        base = Path(__file__).resolve().parent.parent / "temp"
        base.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as directory:
            root = Path(directory)
            makefile = root / "project.make"
            makefile.write_text("$(OBJDIR)/x.o: source.cpp\n")
            objects = root / "obj/native/debug"
            objects.mkdir(parents=True)
            (objects / "x.o").write_text("object")
            (objects / "x.d").write_text("x.o:\n")
            with self.assertRaises(RuntimeError):
                retire_stale_objects(makefile, root / "obj")
            self.assertTrue((objects / "x.o").exists())
            self.assertTrue((objects / "x.d").exists())


if __name__ == "__main__":
    unittest.main()
