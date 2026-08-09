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
  2. D8.4.3 conformance, in two tiers:
     tier A -- a helper whose C return type is neither `Item` nor `void`, with
       a non-PRESERVES row.  Its result IS published into the merged Item lane
       and tag-tested; the test is provably dead, because a raw 0/1 or
       comparison result can never carry the ERROR tag.  This is a live
       emission defect and fails the lint.
     tier B -- a `void` helper with a non-PRESERVES row.  Nothing is published
       (jm_call_void_* clears the result register), so there is no dead test;
       but the row still claims the helper may raise, while the emitter folds
       "no result therefore clean".  Reported, not failed: it is a contract
       accuracy gap, not an emission defect.

Exit status is 1 when any tier A violation is found, so this can run as a lint.
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

ROW_RE = re.compile(r'\{"([A-Za-z_0-9]+)",\s*FPTR\(')
EFFECTS = ("PRESERVES", "CLEARS", "SETS")
DEFAULT_EFFECT = "MAY_SET(default)"


def parse_registry(root: pathlib.Path):
    """name -> (exception_effect, row_text). Rows are delimited by the next row."""
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
        rows[m.group(1)] = (effect, body)
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
        counts = collections.Counter(eff for eff, _ in selected.values())
        scope = args.prefix or "all"
        print(f"exception_effect census ({scope} rows in {REGISTRY})")
        print(f"  total rows            : {len(selected)}")
        for eff in (DEFAULT_EFFECT,) + EFFECTS:
            print(f"  {eff:<22}: {counts.get(eff, 0)}")
        print("\nrows with an explicit non-default effect:")
        for name, (eff, _) in sorted(selected.items()):
            if eff != DEFAULT_EFFECT:
                print(f"  {eff:<10} {name}")

    # D8.4.3: a raw-scalar return is only legal under a PRESERVES contract.
    # Scoped by --prefix: the Lambda-side fn_* helpers are lowered by the
    # Lambda transpiler, which does not publish into the JS merged Item lane.
    #
    # Tier A -- value-returning raw scalars.  jm_publish_call_result gates on
    # MIR_reg_type == MIR_T_I64, which cannot tell a boxed Item from a raw
    # i64/bool, so these results ARE published and tag-tested.  The test is
    # provably dead (a 0/1 or comparison result never carries tag 27).
    #
    # Tier B -- void returns.  jm_call_void_* clears last_call_result_reg, so
    # nothing is published; but MAY_SET still drives the lane to UNKNOWN, and
    # jm_emit_error_lane_test then answers "no result therefore clean".  That
    # is only sound because no void helper may be fallible -- an invariant the
    # emitter documents but the catalog does not currently back.
    tier_a, tier_b = [], []
    for name, (eff, _) in sorted(selected.items()):
        ret = types.get(name)
        if ret is None or ret == "Item" or eff == "PRESERVES":
            continue
        (tier_b if ret == "void" else tier_a).append((name, ret, eff))

    print(f"\nD8.4.3 tier A -- raw-scalar result published into the Item lane:"
          f" {len(tier_a)}")
    if tier_a:
        print(f"  {'helper':<44}{'C return':<12}{'catalog effect'}")
        for name, ret, eff in tier_a:
            print(f"  {name:<44}{ret:<12}{eff}")
        print("\n  Either prove the helper infallible and mark the row")
        print("  PRESERVES, or stop publishing its result into the lane.")

    print(f"\nD8.4.3 tier B -- void helper whose row still claims MAY_SET:"
          f" {len(tier_b)}")
    if tier_b and args.show_void:
        for name, _, _ in tier_b:
            print(f"  {name}")
    elif tier_b:
        print("  (use --show-void to list)")
    print("  A void helper cannot deliver an error value; its row should say")
    print("  PRESERVES so the emitter's 'no result therefore clean' folding is")
    print("  catalog-backed rather than assumed.")

    # only tier A is a live emission defect, so only it fails the lint
    return 1 if tier_a else 0


if __name__ == "__main__":
    sys.exit(main())
