#!/usr/bin/env python3
"""Verify the linker-level static-module boundary images.

lib, core and io are strict SharedLib links. The transitional Radiant image
still links the runtime and permits dynamic lookup, so it does not prove native
closure. The runtime image's approved Class-F deferment remains ratcheted.
DOM adapter imports from the native Radiant owner are checked separately
against D9.1.1v3.
"""

from collections import Counter, defaultdict
from pathlib import Path
import re
import subprocess
import sys

from check_input_archive_headers import input_dependencies


ROOT = Path(__file__).resolve().parent.parent
# the boundary target follows the host shared-library suffix; hard-coding the
# macOS suffix made the Linux linker gate stop after successfully linking.
RT_SUFFIX = ".dylib" if sys.platform == "darwin" else ".dll" if sys.platform.startswith("win") else ".so"
RT_DSO = ROOT / "build/lib" / f"liblambda-boundary-rt{RT_SUFFIX}"
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


def adapter_native_imports(unexpected: set[str]) -> set[str]:
    """Admit only DOM adapter imports backed by exactly one Radiant provider."""
    if not unexpected:
        return set()
    providers = Counter()
    for archive in (ROOT / "build/lib/libradiant-cpp.a", ROOT / "build/lib/libradiant-properties.a"):
        output = subprocess.check_output(["nm", "-gU", "-j", str(archive)], text=True, stderr=subprocess.DEVNULL)
        providers.update(symbol for symbol in output.splitlines() if symbol in unexpected)
    importers = defaultdict(set)
    object_dir = ROOT / "build/obj/lambda-boundary-rt/native/debug"
    for obj in object_dir.glob("*.o"):
        output = subprocess.check_output(["nm", "-u", "-j", str(obj)], text=True, stderr=subprocess.DEVNULL)
        matched = unexpected.intersection(output.splitlines())
        if not matched:
            continue
        sources = list(input_dependencies(obj.with_suffix(".d")))
        if not sources:
            raise RuntimeError(f"missing source provenance for {obj}")
        for symbol in matched:
            importers[symbol].add(sources[0].as_posix())
    catalog = (ROOT / "lambda/dom/dom_api.def").read_text(encoding="utf-8")
    return classify_adapter_imports(unexpected, importers, providers, catalog)


def classify_adapter_imports(unexpected, importers, providers, catalog):
    accepted = set()
    for symbol in unexpected:
        sources = importers.get(symbol, set())
        if providers.get(symbol, 0) != 1 or not sources:
            continue
        # The Jube registry exposes only native operations declared by the DOM catalog.
        catalog_name = symbol.lstrip("_")
        catalogued = any(line.startswith("DOM_RAW(") and "DOM_F_NATIVE" in line and
                          re.search(r"\b" + re.escape(catalog_name) + r"\b", line)
                          for line in catalog.splitlines())
        if all(source.startswith("lambda/dom/") or
               (source == "lambda/jube/jube_registry.cpp" and catalogued)
               for source in sources):
            accepted.add(symbol)
    return accepted


def main() -> int:
    expected = read_baseline()
    actual = runtime_class_f_imports()
    additions = actual - expected
    try:
        adapter_imports = adapter_native_imports(additions)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"MODULE_BOUNDARY: cannot verify adapter ownership: {exc}", file=sys.stderr)
        return 1
    unexpected = sorted(additions - adapter_imports)
    resolved = sorted(expected - actual)
    print("MODULE_BOUNDARY: strict DSOs=lib,core,io; Radiant is transitional and runtime-linked")
    print(f"MODULE_BOUNDARY: deferred_class_f_imports={len(actual & expected)} baseline={len(expected)}")
    print(f"MODULE_BOUNDARY: verified_dom_adapter_native_imports={len(adapter_imports)}")
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
