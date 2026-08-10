#!/usr/bin/env python3
"""Exception-effect catalog census and D8.4.3 conformance lint (static).

D8.4.3: "Fallible JS/Jube helpers use the merged Item error ABI ... Raw scalar
helpers are permitted only when their catalog contract is infallible
(PRESERVES)."

This reads `lambda/runtime/sys_func_registry.c` and the helper definitions in
`lambda/js` + `lambda/runtime`, and reports:

  1. the exception_effect distribution over registry rows (the P5 worklist --
     a row with no explicit effect defaults to JIT_EXCEPTION_MAY_SET, so the
     lane re-enters UNKNOWN after the call and a tag test is emitted),
  2. D8.4.3 conformance, in three tiers:
     tier A -- a helper whose C return type is neither `Item` nor `void`, but
       whose catalog does not declare both its raw scalar transport and an
       infallible PRESERVES effect. D8.4.3 forbids this: raw 0/1, comparison,
       and double results cannot carry an ERROR Item. This fails the lint.
     tier B -- a `void` helper with a non-PRESERVES row. It cannot publish a
       replacement error Item, so the emitter must retain the preceding carrier
       only when the catalog explicitly says PRESERVES. This is a contract
       violation.
     tier C -- an `Item` helper explicitly cataloged as a raw scalar. The MIR
       publication gate would suppress its real error carrier.

Exit status is 1 when any tier violates the contract, so this can run as a lint.
Polarity rule (D6.1.3): a row becomes PRESERVES only by mechanical
verification, never by absence of evidence -- this tool reports candidates,
it never certifies one.

Usage:
    utils/js_exception_catalog_census.py                # census + lint
    utils/js_exception_catalog_census.py --violations   # lint output only
    utils/js_exception_catalog_census.py --prefix js_   # restrict the census
"""

from __future__ import annotations

import argparse
import collections
import glob
import pathlib
import re
import sys

REGISTRY = "lambda/runtime/sys_func_registry.c"
HELPER_GLOBS = ("lambda/js/*.cpp", "lambda/js/*.c",
                "lambda/runtime/*.cpp", "lambda/runtime/*.c")

ROW_RE = re.compile(r'\{"([A-Za-z_0-9]+)",\s*FPTR\(([A-Za-z_0-9]+)\)')
EFFECTS = ("PRESERVES", "CLEARS", "SETS")
DEFAULT_EFFECT = "MAY_SET(default)"
BOXED_ITEM = "JIT_VALUE_BOXED_ITEM"
NON_GC_SCALAR = "JIT_VALUE_NON_GC_SCALAR"


def parse_registry(root: pathlib.Path):
    """name -> (target, effect, return class, row text)."""
    src = (root / REGISTRY).read_text(errors="replace")
    matches = list(ROW_RE.finditer(src))
    rows = {}
    for i, m in enumerate(matches):
        end = matches[i + 1].start() if i + 1 < len(matches) else len(src)
        body = src[m.start():end]
        effect = DEFAULT_EFFECT
        for e in EFFECTS:
            if "JIT_EXCEPTION_" + e in body:
                effect = e
                break
        metadata = re.search(
            r'FPTR\([A-Za-z_0-9]+\)\s*,\s*\{\s*[^,]+,\s*[^,]+,\s*'
            r'(JIT_VALUE_[A-Z_]+)', body)
        ret_class = metadata.group(1) if metadata else "JIT_VALUE_UNKNOWN"
        if "JIT_IMPORT_RAW_SCALAR_PRESERVES" in body:
            ret_class = NON_GC_SCALAR
            effect = "PRESERVES"
        if "JIT_IMPORT_VOID_PRESERVES" in body:
            effect = "PRESERVES"
        rows[m.group(1)] = (m.group(2), effect, ret_class, body)
    return rows


def parse_return_types(root: pathlib.Path):
    """name -> declared C return type, from `extern "C" <type> <name>(`."""
    decl = re.compile(r'extern\s+"C"\s+([A-Za-z_][A-Za-z_0-9]*)\s+'
                      r'([A-Za-z_][A-Za-z_0-9]*)\s*\(')
    types = {}
    for pattern in HELPER_GLOBS:
        for path in glob.glob(str(root / pattern)):
            text = pathlib.Path(path).read_text(errors="replace")
            for m in decl.finditer(text):
                # a definition wins over a forward declaration of the same name
                types.setdefault(m.group(2), m.group(1))
    return types


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=".")
    ap.add_argument("--prefix", default=None,
                    help="only census rows with this name prefix (e.g. js_)")
    ap.add_argument("--show-void", action="store_true",
                    help="list the tier B void rows")
    ap.add_argument("--violations", action="store_true",
                    help="print only the D8.4.3 violation list")
    args = ap.parse_args()

    root = pathlib.Path(args.root).resolve()
    rows = parse_registry(root)
    types = parse_return_types(root)

    selected = {n: v for n, v in rows.items()
                if args.prefix is None or n.startswith(args.prefix)}

    if not args.violations:
        counts = collections.Counter(effect for _, effect, _, _ in selected.values())
        scope = args.prefix or "all"
        print(f"exception_effect census ({scope} rows in {REGISTRY})")
        print(f"  total rows            : {len(selected)}")
        for eff in (DEFAULT_EFFECT,) + EFFECTS:
            print(f"  {eff:<22}: {counts.get(eff, 0)}")
        print("\nrows with an explicit non-default effect:")
        for name, (_, eff, _, _) in sorted(selected.items()):
            if eff != DEFAULT_EFFECT:
                print(f"  {eff:<10} {name}")

    # D8.4.3: raw scalar helpers need both an explicit raw transport and an
    # infallible catalog effect; only boxed Items enter the merged error lane.
    # Scoped by --prefix: the Lambda-side fn_* helpers are lowered by the
    # Lambda transpiler, which does not publish into the JS merged Item lane.
    #
    # Tier A -- value-returning raw scalars lacking their required contract.
    # The JIT gate reads `ret_class`; without NON_GC_SCALAR it must conservatively
    # publish an I64 register, while a non-PRESERVES effect violates the ABI
    # even if the result is correctly excluded from publication.
    #
    # Tier B -- void returns cannot supply a new error carrier. A preserving
    # call retains the preceding carrier; any other effect clears it
    # fail-closed, so a MAY_SET void row contradicts the merged Item ABI.
    tier_a, tier_b, tier_c = [], [], []
    for name, (target, eff, ret_class, _) in sorted(selected.items()):
        ret = types.get(target)
        if ret is None:
            continue
        if ret == "Item":
            if ret_class == NON_GC_SCALAR:
                tier_c.append((name, target, ret, ret_class))
            continue
        if ret == "void":
            if eff != "PRESERVES":
                tier_b.append((name, target, ret, eff))
        elif ret_class != NON_GC_SCALAR or eff != "PRESERVES":
            tier_a.append((name, target, ret, eff))

    print(f"\nD8.4.3 tier A -- raw-scalar helper lacking a PRESERVES contract:"
          f" {len(tier_a)}")
    if tier_a:
        print(f"  {'registry name':<44}{'target':<40}{'C return':<12}{'catalog effect'}")
        for name, target, ret, eff in tier_a:
            print(f"  {name:<44}{target:<40}{ret:<12}{eff}")
        print("\n  Declare NON_GC_SCALAR plus PRESERVES, or return a boxed Item.")

    print(f"\nD8.4.3 tier B -- void helper whose row still claims MAY_SET:"
          f" {len(tier_b)}")
    if tier_b and args.show_void:
        for name, _, _, _ in tier_b:
            print(f"  {name}")
    elif tier_b:
        print("  (use --show-void to list)")
    print("  A void helper cannot deliver a replacement error Item; its row")
    print("  must say PRESERVES so the emitter retains the preceding carrier")
    print("  instead of clearing it fail-closed.")

    print(f"\nD8.4.3 tier C -- boxed-Item helper cataloged as a raw scalar:"
          f" {len(tier_c)}")
    if tier_c:
        print(f"  {'registry name':<44}{'target':<40}{'C return':<12}{'catalog class'}")
        for name, target, ret, ret_class in tier_c:
            print(f"  {name:<44}{target:<40}{ret:<12}{ret_class}")
        print("\n  An Item-returning helper must not be declared NON_GC_SCALAR.")

    return 1 if tier_a or tier_b or tier_c else 0


if __name__ == "__main__":
    sys.exit(main())
