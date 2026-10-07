#!/usr/bin/env python3
"""Regenerate the TeX engine conformance expectations from a real TeX.

Each fixture is an IniTeX file whose \\T{...} lines write `LT:` records to
the terminal. This script runs `pdftex -ini -etex` on every fixture, keeps
the LT: lines of its log, and writes the expected output of
test/lambda/latex/test_tex_engine_conformance.ls, so the engine is checked
against TeX itself rather than against its own earlier output.

Reference toolchain: TeX Live 2025 (pdfTeX 1.40.27, e-TeX 2.6).
Usage (from the repository root):
    python3 test/latex/fixtures/tex_engine/make_reference.py
"""
import glob
import os
import shutil
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
WORK = os.path.join(ROOT, "temp", "tex_engine_reference")
FIXTURES = ["expansion", "conditionals", "registers", "etex", "tokens", "pdfstrings"]
EXPECTED = os.path.join(ROOT, "test", "lambda", "latex", "test_tex_engine_conformance.txt")


def reference_lines(name):
    os.makedirs(WORK, exist_ok=True)
    shutil.copy(os.path.join(HERE, name + ".tex"), WORK)
    env = dict(os.environ, max_print_line="100000")
    subprocess.run(["pdftex", "-ini", "-etex", "-interaction=nonstopmode", name + ".tex"],
                   cwd=WORK, env=env, capture_output=True)
    with open(os.path.join(WORK, name + ".log"), encoding="latin-1") as log:
        return [line.rstrip("\n") for line in log if line.startswith("LT:")]


def main():
    out = []
    for name in FIXTURES:
        out.append("== " + name + " ==")
        out.extend(reference_lines(name))
    # the test prints one top-level string: quoted, newlines and backslashes raw
    with open(EXPECTED, "w") as f:
        f.write('"' + "\n".join(out) + '"\n')


if __name__ == "__main__":
    main()
