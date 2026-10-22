#!/usr/bin/env python3
"""Measure positional English vocabulary rows using pinned ESDB word lists.

Download SOURCE_URL/<filename> into --source-dir, including Copyright and
README.md, then run this utility. No downloads or Lambda engine builds occur.
"""

import argparse
import gzip
import hashlib
import json
import platform
import shutil
import unicodedata
from pathlib import Path


SOURCE_REVISION = "71d7dd07676edb60ade43552e10b41314b7e9287"
SOURCE_URL = f"https://raw.githubusercontent.com/en-wl/wordlist-diff/{SOURCE_REVISION}"
SOURCE_FILES = {
    "en_US.txt": ("en-US", 60, "4ff7e0b6d86763e1e042ffd746e94cdf4432618702deac303a1669e2a838db04"),
    "en_GB-ise.txt": ("en-GB", 60, "82b22843c9958a1efac291e0ea7dbef22ce227dd4e5ca0fd84043cbc62e0a4f1"),
    "en_US-large.txt": ("en-US-large", 70, "fa1f9a1382df724be887d3a5d2d743095e6f33fc0949ebb22415c1554bb42fa7"),
    "en_GB-large.txt": ("en-GB-large", 70, "cde5031d8719181cb03c045c4af1e7c20052e9afe09118c5a5e616beed95ea61"),
    "Copyright": (None, None, "090575a131b4260926c7a6b30a90aca0f5db5fbb5c46778e0c5855227bf6ebc3"),
    "README.md": (None, None, "12887e9866b6ea088d6d7b5b86d8ffdf341feeb3cee645cfc340ea66eb06243f"),
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")


def emit(output, name, data):
    path = output / name
    path.write_bytes(data)
    compressed = gzip.compress(data, compresslevel=9, mtime=0)
    path.with_name(name + ".gz").write_bytes(compressed)
    return {"file": name, "bytes": len(data), "gzip_bytes": len(compressed),
            "sha256": digest(data), "gzip_sha256": digest(compressed)}


def measure(output, profile, level, source):
    source_words = source.decode("utf-8").splitlines()
    if not source_words or any(not word or "\x00" in word for word in source_words):
        raise ValueError(f"invalid source words: {profile}")
    # Preserve full spellings and case; normalization changes only lookup encoding.
    words = sorted({unicodedata.normalize("NFC", word) for word in source_words})
    if len(words) != len(source_words):
        raise ValueError(f"duplicate or normalization-colliding source words: {profile}")
    header = {"format_version": 1, "language": profile.removesuffix("-large")}
    rows = [[word, "ordinary"] for word in words]
    controls = {
        "rows": {**header, "keys": words, "entries": rows},
        "objects": {**header, "keys": words,
                    "entries": [{"word": word, "case": case} for word, case in rows]},
        "rows-only": {**header, "entries": rows},
    }
    formats = {"wordlist": emit(output, profile + ".words.txt",
                                 ("\n".join(words) + "\n").encode("utf-8"))}
    for label, value in controls.items():
        payload = encoded(value)
        decoded = json.loads(payload)
        if label == "objects":
            recovered = [[entry["word"], entry["case"]] for entry in decoded["entries"]]
        else:
            recovered = decoded["entries"]
        if recovered != rows or (label != "rows-only" and decoded["keys"] != words):
            raise ValueError(f"JSON round-trip mismatch: {profile}/{label}")
        formats[label] = emit(output, profile + "." + label + ".json", payload)
    return {"profile": profile, "coverage_level": level, "entries": len(words),
            "source_bytes": len(source), "source_sha256": digest(source), "formats": formats,
            "row_savings_bytes": formats["objects"]["bytes"] - formats["rows"]["bytes"],
            "duplicated_keys_bytes": formats["rows"]["bytes"] - formats["rows-only"]["bytes"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=Path("temp/spelling_vocab_poc/source"))
    parser.add_argument("--output-dir", type=Path, default=Path("temp/spelling_vocab_poc/out"))
    args = parser.parse_args()
    # Refuse an output in the input tree: the measurement must not rewrite provenance.
    if args.output_dir.resolve().is_relative_to(args.source_dir.resolve()):
        parser.error("output-dir must not be inside source-dir")
    sources = {}
    for name, (_, _, expected) in SOURCE_FILES.items():
        data = (args.source_dir / name).read_bytes()
        if digest(data) != expected:
            parser.error(f"source checksum mismatch: {name}")
        sources[name] = data
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name in ("Copyright", "README.md"):
        shutil.copyfile(args.source_dir / name, args.output_dir / ("ESDB-" + name))
    report = {
        "format_version": 1, "source_release": "2026.02.25", "source_revision": SOURCE_REVISION,
        "source_url": SOURCE_URL, "generator_sha256": digest(Path(__file__).read_bytes()),
        "python": platform.python_version(), "unicode_version": unicodedata.unidata_version,
        "serialization": "UTF-8 JSON, ensure_ascii=false, no optional whitespace, final LF",
        "case_policy": "all rows use ordinary for the requested storage experiment; no case-rule fidelity claim",
        "includes": "keys plus two-column [word, case] rows",
        "excludes": "suggestion index, frequencies, restrictions and other optional row fields; notices are separate",
        "compression": "gzip level 9, mtime=0; comparison only, not a Lambda loader format",
        "profiles": [measure(args.output_dir, profile, level, sources[name])
                     for name, (profile, level, _) in SOURCE_FILES.items() if profile],
    }
    (args.output_dir / "measurements.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Profile          Entries    Rows bytes    Rows gzip   Objects bytes   Rows-only bytes")
    for item in report["profiles"]:
        formats = item["formats"]
        print(f"{item['profile']:<16} {item['entries']:>7,} {formats['rows']['bytes']:>13,}"
              f" {formats['rows']['gzip_bytes']:>12,} {formats['objects']['bytes']:>15,}"
              f" {formats['rows-only']['bytes']:>17,}")
    print(f"Verified measurements: {args.output_dir / 'measurements.json'}")


if __name__ == "__main__":
    main()
