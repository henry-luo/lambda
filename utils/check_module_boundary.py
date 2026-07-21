#!/usr/bin/env python3
"""Verify the linker-level static-module boundary images.

lib, core, io, and radiant are strict SharedLib links.  The active runtime
image is also built, but SM10's approved Class-F deferment leaves its JS DOM
and UI bindings as dynamic imports.  That set is ratcheted: additions fail,
while removals are reported as progress.
"""

from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
RT_DSO = ROOT / "build/lib/liblambda-boundary-rt.dylib"
RT_BASELINE = ROOT / "utils/static_module_rt_class_f_baseline.txt"

CLASS_F_SYMBOL = re.compile(
    r"^_(?:_?Z.*(?:DocState|Dom|UiContext|Editing|InputIntent|AnimationScheduler|DirtyTracker).*"
    r"|(?:radiant_|state_|focus_|tc_|editing_|form_control_|selection_|dom_|css_prop_"
    r"|caret_|view_|clipboard_store_).*|fn_pdf_register_svg_image_resolver)$")


def read_baseline() -> set[str]:
    return {
        line.strip()
        for line in RT_BASELINE.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.startswith("#")
    }


def runtime_class_f_imports() -> set[str]:
    if not RT_DSO.exists():
        raise RuntimeError(f"missing runtime boundary DSO: {RT_DSO}")
    output = subprocess.check_output(["nm", "-u", str(RT_DSO)], text=True)
    return {
        line.strip()
        for line in output.splitlines()
        if CLASS_F_SYMBOL.fullmatch(line.strip())
    }


def main() -> int:
    expected = read_baseline()
    actual = runtime_class_f_imports()
    unexpected = sorted(actual - expected)
    resolved = sorted(expected - actual)
    print("MODULE_BOUNDARY: strict DSOs=lib,core,io,radiant")
    print(f"MODULE_BOUNDARY: deferred_class_f_imports={len(actual)} baseline={len(expected)}")
    if unexpected:
        print("MODULE_BOUNDARY: new deferred Class-F imports are not allowed:", file=sys.stderr)
        for symbol in unexpected:
            print(f"  {symbol}", file=sys.stderr)
        return 1
    if resolved:
        print(f"MODULE_BOUNDARY: resolved_class_f_imports={len(resolved)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
