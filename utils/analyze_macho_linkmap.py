#!/usr/bin/env python3
"""Summarize live symbols in an Apple ld link map by archive and section.

Example: python3 utils/analyze_macho_linkmap.py temp/release-size-baseline/lambda.linkmap \
    --output-dir temp/release-size-baseline/analysis

The totals describe linker attribution, not removable feature sizes. LTO may
assign optimized code to a synthetic object. File-backed object totals exclude
zero-fill sections; symbols.tsv and groups.tsv include them.
"""

import argparse
import bisect
from collections import Counter
from pathlib import Path
import re
import sys

OBJECT = re.compile(r"\[\s*(\d+)\]\s+(.*)")
SECTION = re.compile(r"(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S+)\s+(\S+)")
SYMBOL = re.compile(r"(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+\[\s*(\d+)\]\s+(.*)")
ARCHIVE = re.compile(r"([^/()]+\.a)\(")
ZERO_FILL = {"__bss", "__thread_bss", "__common"}


def read_map(path):
    objects, sections, symbols = {}, [], []
    mode = None
    with path.open(encoding="utf-8", errors="replace") as source:
        for line in source:
            if line.startswith("# Object files:"):
                mode = "objects"
            elif line.startswith("# Sections:"):
                mode = "sections"
            elif line.startswith("# Symbols:"):
                mode = "symbols"
            elif line.startswith("# Dead Stripped Symbols:"):
                break
            elif line.startswith("#"):
                continue
            elif mode == "objects":
                match = OBJECT.match(line)
                if match:
                    objects[int(match[1])] = match[2].strip()
            elif mode == "sections":
                match = SECTION.match(line)
                if match:
                    sections.append((int(match[1], 16), int(match[2], 16), match[3], match[4]))
            elif mode == "symbols":
                match = SYMBOL.match(line)
                if match:
                    symbols.append((int(match[1], 16), int(match[2], 16),
                                    int(match[3]), match[4].strip()))
    if not objects or not sections or not symbols:
        raise ValueError("link map lacks object, section, or live-symbol records")
    return objects, sorted(sections), symbols


def analyze(path, output_dir):
    objects, sections, symbols = read_map(path)
    starts = [section[0] for section in sections]
    all_objects, file_objects = Counter(), Counter()
    by_section, by_group = Counter(), Counter()
    attributed_symbols = []
    for address, size, index, name in symbols:
        if not size:
            continue
        pos = bisect.bisect_right(starts, address) - 1
        if pos < 0 or address + size > sections[pos][0] + sections[pos][1]:
            raise ValueError(f"symbol outside its section: {address:#x} {name}")
        if index not in objects:
            raise ValueError(f"unknown object index {index}: {name}")
        _, _, segment, section = sections[pos]
        obj = objects[index]
        all_objects[obj] += size
        file_objects[obj] += size if section not in ZERO_FILL else 0
        by_section[(segment, section)] += size
        archive = ARCHIVE.search(obj)
        group = archive[1] if archive else ("linker synthesized" if index == 0 else obj)
        by_group[(group, segment, section)] += size
        attributed_symbols.append((size, group, segment, section, name))

    output_dir.mkdir(parents=True, exist_ok=True)
    with (output_dir / "groups.tsv").open("w") as out:
        print("group\tsegment\tsection\tbytes", file=out)
        for (group, segment, section), size in sorted(by_group.items(), key=lambda item: -item[1]):
            print(group, segment, section, size, sep="\t", file=out)
    for filename, counts in (("objects.tsv", all_objects), ("file-backed-objects.tsv", file_objects)):
        with (output_dir / filename).open("w") as out:
            print("bytes\tobject", file=out)
            for obj, size in counts.most_common():
                print(size, obj, sep="\t", file=out)
    with (output_dir / "symbols.tsv").open("w") as out:
        print("bytes\tgroup\tsegment\tsection\tname", file=out)
        for row in sorted(attributed_symbols, reverse=True):
            print(*row, sep="\t", file=out)
    with (output_dir / "sections.tsv").open("w") as out:
        print("segment\tsection\tsection_bytes\tmapped_symbol_bytes", file=out)
        for _, size, segment, section in sections:
            print(segment, section, size, by_section[(segment, section)], sep="\t", file=out)
    print(f"objects {len(objects)} symbols {len(symbols)} symbol bytes {sum(by_section.values())}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("linkmap", type=Path, help="Apple ld link map")
    parser.add_argument("--output-dir", type=Path, required=True, help="directory for TSV reports")
    args = parser.parse_args()
    try:
        analyze(args.linkmap, args.output_dir)
    except (OSError, ValueError) as error:
        print(f"link map analysis: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
