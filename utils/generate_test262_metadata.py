#!/usr/bin/env python3
"""Generate test262 metadata cache for the GTest runner.

Scans all .js test files under ref/test262/test/, extracts YAML frontmatter
metadata, and writes a TSV cache to temp/test262_metadata.tsv.

The GTest runner (test_js_test262_gtest.cpp) loads this cache to skip
per-file I/O during Phase 1 (metadata parsing).

Usage: python3 utils/generate_test262_metadata.py
"""

import os
import re
import sys

TEST262_ROOT = "ref/test262"
OUTPUT_FILE = "temp/test262_metadata.tsv"


def parse_metadata(source):
    """Parse YAML frontmatter from a test262 test file.

    Mirrors the parse_metadata() function in test_js_test262_gtest.cpp.
    Returns a dict with: includes, features, flags bitmask, negative info.
    """
    meta = {
        "includes": [],
        "features": [],
        "is_async": False,
        "is_module": False,
        "is_raw": False,
        "is_strict": False,
        "is_nostrict": False,
        "is_negative": False,
        "neg_phase": "",
        "neg_type": "",
    }

    start = source.find("/*---")
    end = source.find("---*/")
    if start == -1 or end == -1:
        return meta

    yaml = source[start + 5 : end]

    # parse includes: [file1.js, file2.js] or includes:\n  - file.js
    inc_match = re.search(r"includes:\s*\[([^\]]*)\]", yaml)
    if inc_match:
        for item in inc_match.group(1).split(","):
            item = item.strip()
            if item:
                meta["includes"].append(item)
    else:
        inc_pos = yaml.find("includes:")
        if inc_pos != -1:
            lines = yaml[inc_pos:].split("\n")[1:]
            for line in lines:
                stripped = line.strip()
                if not stripped.startswith("-"):
                    break
                val = stripped[1:].strip()
                if val:
                    meta["includes"].append(val)

    # parse flags: [onlyStrict, async, module, raw, noStrict]
    flags_match = re.search(r"flags:\s*\[([^\]]*)\]", yaml)
    if flags_match:
        for item in flags_match.group(1).split(","):
            item = item.strip()
            if item == "async":
                meta["is_async"] = True
            elif item == "module":
                meta["is_module"] = True
            elif item == "raw":
                meta["is_raw"] = True
            elif item == "onlyStrict":
                meta["is_strict"] = True
            elif item == "noStrict":
                meta["is_nostrict"] = True

    # parse features: [feat1, feat2] or features:\n  - feat
    feat_match = re.search(r"features:\s*\[([^\]]*)\]", yaml)
    if feat_match:
        for item in feat_match.group(1).split(","):
            item = item.strip()
            if item:
                meta["features"].append(item)
    else:
        feat_pos = yaml.find("features:")
        if feat_pos != -1:
            lines = yaml[feat_pos:].split("\n")[1:]
            for line in lines:
                stripped = line.strip()
                if not stripped.startswith("-"):
                    break
                val = stripped[1:].strip()
                if val:
                    meta["features"].append(val)

    # parse negative:
    if "negative:" in yaml:
        meta["is_negative"] = True
        neg_section = yaml[yaml.find("negative:") :]
        phase_match = re.search(r"phase:\s*(\S+)", neg_section)
        type_match = re.search(r"type:\s*(\S+)", neg_section)
        if phase_match:
            meta["neg_phase"] = phase_match.group(1).strip()
        if type_match:
            meta["neg_type"] = type_match.group(1).strip()

    return meta


def main():
    test_dir = os.path.join(TEST262_ROOT, "test")
    if not os.path.isdir(test_dir):
        print(f"Error: {test_dir} not found. Run from project root.", file=sys.stderr)
        sys.exit(1)

    # walk all .js files (sorted for deterministic output)
    entries = []
    for root, dirs, files in os.walk(test_dir):
        dirs.sort()
        for fname in sorted(files):
            if not fname.endswith(".js"):
                continue
            path = os.path.join(root, fname)
            entries.append(path)

    print(f"[metadata] Scanning {len(entries)} test files...", file=sys.stderr)

    # parse metadata for each file
    results = []
    for i, path in enumerate(entries):
        try:
            with open(path, "r", errors="replace") as f:
                source = f.read()
        except Exception:
            source = ""

        meta = parse_metadata(source)

        # flags bitmask: bit0=async, bit1=module, bit2=raw,
        #                bit3=strict, bit4=nostrict, bit5=negative
        flags = 0
        if meta["is_async"]:
            flags |= 1
        if meta["is_module"]:
            flags |= 2
        if meta["is_raw"]:
            flags |= 4
        if meta["is_strict"]:
            flags |= 8
        if meta["is_nostrict"]:
            flags |= 16
        if meta["is_negative"]:
            flags |= 32

        includes = ";".join(meta["includes"])
        features = ";".join(meta["features"])

        # native harness eligibility: no includes, not negative, no Test262Error in source
        native = 0
        if not meta["includes"] and not meta["is_negative"]:
            if "Test262Error" not in source:
                native = 1

        results.append((path, flags, meta["neg_phase"], meta["neg_type"], includes, features, native))

        if (i + 1) % 10000 == 0:
            print(f"[metadata]   parsed {i + 1}/{len(entries)}...", file=sys.stderr)

    # write TSV cache
    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
    with open(OUTPUT_FILE, "w") as f:
        f.write(f"V2\t{len(results)}\n")
        for path, flags, neg_phase, neg_type, includes, features, native in results:
            # path\tflags\tneg_phase\tneg_type\tincludes\tfeatures\tnative
            f.write(f"{path}\t{flags}\t{neg_phase}\t{neg_type}\t{includes}\t{features}\t{native}\n")

    file_size = os.path.getsize(OUTPUT_FILE)
    print(
        f"[metadata] Generated {OUTPUT_FILE}: {len(results)} entries, {file_size / 1024:.0f} KB",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
