#!/usr/bin/env python3
"""Check shipped-script naming and prevent dependencies on checkout demos."""

from pathlib import Path
import sys

from check_ui_boundaries import IMPORT, IMPORT_ITEM, PACKAGES, ROOT, resolve


def main():
    problems = []
    scripts = sorted(PACKAGES.rglob("*.ls"))
    for source in scripts:
        if source.name.startswith("mod_"):
            problems.append(f"shipped script uses mod_ prefix: {source.relative_to(ROOT)}")
        for line in source.read_text().splitlines():
            match = IMPORT.match(line)
            if not match:
                continue
            for item in match.group(1).split(","):
                part = IMPORT_ITEM.match(item)
                target = resolve(source, part.group(1)) if part else None
                if target and not target.is_relative_to(PACKAGES):
                    problems.append(f"import leaves package tree: {source.relative_to(ROOT)} -> {target}")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    print(f"Package structure passed: {len(scripts)} shipped scripts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
