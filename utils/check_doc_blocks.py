#!/usr/bin/env python3
"""Compile every Lambda unit carried by `doc/**/*.md` and check its contract.

Pairs with extract_doc_blocks.py, which turns the doc set into runnable `.ls`
files. This is the gate over them (Doc_Convention.md 8.1):

  * an unmarked block, and an `expr` / `type` unit, must compile clean;
  * an `error=E###` block must FAIL, and carry that diagnostic code -- so a
    reworded or renumbered error surfaces here instead of rotting;
  * a `no-run` block is skipped, and only counted.

A violated marker is reported apart from a plain failure: it means the doc
asserts something the compiler contradicts, which is worse than a block that
is merely known-broken.

Exit status is 0 only when nothing failed and no marker was violated. The
`--baseline` flag suppresses failures already recorded in a baseline file, so
the gate can ride a known-broken corpus down without going red on day one.

    python3 utils/check_doc_blocks.py                     # check everything
    python3 utils/check_doc_blocks.py --filter Lambda_Type
    python3 utils/check_doc_blocks.py --write-baseline temp/doc_baseline.json
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extract_doc_blocks as extractor  # noqa: E402

ROOT = extractor.ROOT
LAMBDA = ROOT / "lambda.exe"
CODE = re.compile(r"error\[(E\d+)\]")


def compile_unit(rel_path):
    """Return the set of diagnostic codes the compiler reports for one unit."""
    proc = subprocess.run(
        [str(LAMBDA), "--dry-run", "--max-errors", "0", str(ROOT / rel_path)],
        capture_output=True, text=True, errors="replace", cwd=ROOT,
    )
    return set(CODE.findall(proc.stdout + proc.stderr))


def key(entry):
    return f"{entry['file']}:{entry['line']}"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--filter", metavar="SUBSTR",
                    help="only check units whose document path contains SUBSTR")
    ap.add_argument("--baseline", metavar="FILE",
                    help="suppress failures listed in FILE (marker violations still fail)")
    ap.add_argument("--write-baseline", metavar="FILE",
                    help="record current failures to FILE and exit 0")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="summary only; do not list failing units")
    args = ap.parse_args()

    if not LAMBDA.exists():
        sys.exit(f"lambda.exe not found at {LAMBDA} -- run `make build` first")

    extractor.main()
    index = json.loads(extractor.INDEX.read_text(encoding="utf-8"))
    if args.filter:
        index = [e for e in index if args.filter in e["file"]]

    baseline = set()
    if args.baseline and Path(args.baseline).exists():
        baseline = set(json.loads(Path(args.baseline).read_text(encoding="utf-8")))

    tally = Counter()
    failures, violations = [], []
    for entry in index:
        if entry["path"] is None:            # no-run: indexed, never compiled
            tally["no-run skipped"] += 1
            continue
        codes = compile_unit(entry["path"])
        want = entry["expect_error"]
        kind = entry["kind"] or "plain"
        if want:
            if want in codes:
                tally["error= honoured"] += 1
            else:
                tally["error= VIOLATED"] += 1
                violations.append((entry, want, codes))
        elif codes:
            tally[f"{kind} failing"] += 1
            failures.append((entry, codes))
        else:
            tally[f"{kind} clean"] += 1

    if args.write_baseline:
        Path(args.write_baseline).write_text(
            json.dumps(sorted(key(e) for e, _ in failures), indent=1), encoding="utf-8")
        print(f"baseline written: {args.write_baseline} ({len(failures)} entries)")
        return 0

    new_failures = [(e, c) for e, c in failures if key(e) not in baseline]
    fixed = baseline - {key(e) for e, _ in failures}

    print()
    for label, count in sorted(tally.items()):
        print(f"  {count:>4}  {label}")

    if violations:
        print("\n  MARKER VIOLATIONS (the doc asserts what the compiler denies):")
        for entry, want, codes in violations:
            got = ", ".join(sorted(codes)) or "no error at all"
            print(f"    {key(entry):<44} declares {want}, got {got}")

    if new_failures and not args.quiet:
        header = "NEW failures" if baseline else "failing units"
        print(f"\n  {header}:")
        by_doc = defaultdict(list)
        for entry, codes in new_failures:
            by_doc[entry["file"]].append((entry, codes))
        for doc in sorted(by_doc):
            print(f"    {doc}")
            for entry, codes in by_doc[doc]:
                kind = entry["kind"] or "plain"
                print(f"      :{entry['line']:<6} {kind:<5} {', '.join(sorted(codes))}")

    if fixed:
        print(f"\n  {len(fixed)} baseline entr{'y' if len(fixed) == 1 else 'ies'} "
              f"now passing -- refresh with --write-baseline")

    bad = len(violations) + len(new_failures)
    print(f"\n  {'FAIL' if bad else 'OK'}: {len(violations)} violation(s), "
          f"{len(new_failures)} unexpected failure(s)"
          f"{f', {len(failures) - len(new_failures)} baselined' if baseline else ''}\n")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
