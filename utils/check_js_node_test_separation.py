#!/usr/bin/env python3
"""Reject Node compatibility coverage placed in the JavaScript runtime suite."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
JS_TEST_DIR = ROOT / "test" / "js"
NODE_SPECIFIER = (
    r"assert|buffer|child_process|cluster|console|constants|diagnostics_channel|"
    r"domain|events|fs|module|net|os|path|perf_hooks|process|punycode|"
    r"querystring|readline|repl|stream|string_decoder|timers|tty|url|util|"
    r"v8|vm|worker_threads|zlib"
)
NODE_IMPORT = re.compile(
    rf"(?:from|require\()\s*['\"](?:node:)?(?:{NODE_SPECIFIER})['\"]"
)
NODE_GLOBAL = re.compile(r"\b(?:Buffer\.|process\.nextTick|__dirname|__filename|module\.exports)")
BROWSER_LIBRARY_DRIVERS = {
    # These are DOM tests; fs only loads the committed browser-library fixture.
    "dom2_library_probe.js",
    "dom_bootstrap.js",
    "dom_jquery_fx.js",
}


def is_vendored_fixture(path: Path) -> bool:
    name = path.name
    # These are third-party parser/runtime corpus inputs, not authored tests.
    return (name.startswith("lib_") or name.endswith(".min.js") or "_src" in name or
            name in {"dom_jquery_lib.js", "hljs_highlight.js", "underscore_lib.js"})


def main() -> int:
    failures: list[str] = []
    for path in sorted(JS_TEST_DIR.rglob("*.js")):
        if is_vendored_fixture(path) or path.name in BROWSER_LIBRARY_DRIVERS:
            continue
        source = path.read_text(encoding="utf-8")
        relative = path.relative_to(ROOT)
        if path.name.startswith("jube_"):
            failures.append(f"{relative}: Jube tests exercise Node modules and belong in test/node")
        if NODE_IMPORT.search(source) or NODE_GLOBAL.search(source):
            failures.append(f"{relative}: Node API coverage belongs in test/node")

    if failures:
        print("JS_NODE_TEST_SEPARATION: failed", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("JS_NODE_TEST_SEPARATION: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
