#!/usr/bin/env python3
"""Download, combine and validate the local Sogou Bible IME pack (IME6/IME-O7).

fetch checks pinned downloads; prepare and validate are offline. Dictionary bytes
stay in ignored temp/ and lmd/package/ime/chinese/data/ directories.
--with-base builds the demo's standard + Bible union; --profile selects its script.
SCEL layout reference: https://github.com/lewangdev/scel2txt
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import struct
import unicodedata

import prepare_ime_chinese as ime

LOCK_PATH = ime.ROOT / "utils/ime_chinese_bible_sources.json"
SIGNATURE = b"\x40\x15\x00\x00DCS\x01\x01\x00\x00\x00"
PINYIN_START, WORD_START = 0x1540, 0x2628


class Reader:
    def __init__(self, raw, start, end):
        self.raw, self.at, self.end = raw, start, end

    def take(self, length):
        if self.at + length > self.end:
            raise ValueError(f"truncated SCEL field at byte {self.at}")
        result = self.raw[self.at:self.at + length]
        self.at += length
        return result

    def u16(self):
        return struct.unpack("<H", self.take(2))[0]

    def text(self):
        length = self.u16()
        if length == 0 or length % 2:
            raise ValueError("invalid SCEL UTF-16 string length")
        return self.take(length).decode("utf-16le")


def read_scel(raw):
    # Accept the observed DCS1 layout explicitly; unknown layouts must not be guessed.
    if len(raw) <= WORD_START or not raw.startswith(SIGNATURE):
        raise ValueError("unsupported SCEL header or empty dictionary")
    py = Reader(raw, PINYIN_START, WORD_START)
    count = struct.unpack("<I", py.take(4))[0]
    inventory = {}
    for _ in range(count):
        index, value = py.u16(), py.text()
        if index in inventory or not re.fullmatch("[a-z]+", value):
            raise ValueError("invalid or duplicate SCEL pinyin index")
        inventory[index] = value
    if not inventory or any(py.take(py.end - py.at)):
        raise ValueError("invalid SCEL pinyin table padding")
    words, records, prefixes = Reader(raw, WORD_START, len(raw)), [], []
    while words.at < words.end:
        homonyms, size = words.u16(), words.u16()
        if homonyms == 0 or size == 0 or size % 2:
            raise ValueError("invalid SCEL homonym group")
        indices = struct.iter_unpack("<H", words.take(size))
        try:
            reading = tuple(ime.toneless(inventory[index]) for index, in indices)
        except KeyError as error:
            raise ValueError(f"unknown SCEL pinyin index: {error}") from error
        for _ in range(homonyms):
            word = unicodedata.normalize("NFC", words.text())
            extension = words.take(words.u16())
            if len(extension) < 2:
                raise ValueError("invalid SCEL extension length")
            prefixes.append(struct.unpack_from("<H", extension)[0])
            records.append((word, reading))
    title = raw[0x130:0x338].decode("utf-16le").split("\0", 1)[0]
    return records, {"title": title, "records": len(records),
                     "extension_prefix_is_ordinal_permutation":
                         sorted(prefixes) == list(range(1, len(records) + 1))}


def collect(sources, syllables, chars, han):
    entries, reports, rejected = {}, {}, []
    for name, files in sources.items():
        records, details = read_scel(next(iter(files.values())))
        dropped, duplicates = Counter(), 0
        for word, reading in records:
            reason = ime.invalid_pair(word, reading, syllables, chars, han)
            if reason:
                dropped[reason] += 1
                rejected.append({"source": name, "word": word, "reading": "'".join(reading), "reason": reason})
            else:
                duplicates += (word, reading) in entries
                # SCEL ordinals are not frequency evidence (IME6); unweighted entries use zero.
                entries[word, reading] = 0
        reports[name] = {**details, "accepted": len(records) - sum(dropped.values()),
                         "duplicates": duplicates, "dropped": dict(sorted(dropped.items()))}
    if not entries:
        raise ValueError("no admissible entries in the source dictionaries")
    return entries, reports, rejected


def combine(base, extra):
    # Keep existing per-reading frequency evidence when an imported entry overlaps.
    result = dict(base)
    for pair, weight in extra.items():
        result[pair] = max(result.get(pair, 0), weight)
    return result, {"base_postings": len(base), "bible_postings": len(extra),
                    "overlapping_postings": len(base.keys() & extra.keys()),
                    "added_postings": len(extra.keys() - base.keys())}


def prepare(args, lock, entries, reports, rejected, chars, conversion=None, base=None, sources=None):
    notice = ["Local Sogou Bible dictionaries, converted to Lambda Chinese IME pack v1.\n",
              "Readings come from SCEL; NFC words, canonical toneless pinyin, Han-only, 1-9 characters.\n",
              "Identical word/readings merge by maximum weight; imported entries have zero weight without frequency evidence.\n",
              "See import-report.json and rejected.jsonl for complete admission accounting.\n"]
    for name, source in lock["sources"].items():
        notice.append(f"\n--- {name}: {source.get('title', name)} ({source['license']}) ---\n")
        notice.extend(flag + "\n" for flag in source["provenance_flags"])
        if "page" in source:
            notice.append(source["page"] + "\n")
        if sources and name in sources:
            files = sources[name]
            notice.append(files.get("NOTICE", files.get("LICENSE", b"")).decode("utf-8") + "\n")
    weights = list(entries.values())
    manifest = {
        "format": ime.FORMAT, "format_version": 1, "profile": args.profile, "script": ime.PROFILES[args.profile][0],
        "unicode_version": lock["unicode_version"],
        "char_set": ime.character_set_identity(args.profile, chars),
        "syllable_table": ime.digest(ime.SYLLABLE_PATH.read_bytes()),
        **ime.write_pack_payload(args.output_dir, entries, args.abbrev_limit, "".join(notice).encode()),
        "weights": {"method": "maximum per word/reading; standard weights retained, zero for SCEL entries without frequency evidence",
                    "minimum": min(weights), "maximum": max(weights), "zero_postings": weights.count(0)},
        "sources": lock["sources"],
        "accepted_source_records": {name: report["accepted"] for name, report in reports.items()},
        "dropped": {name: report["dropped"] for name, report in reports.items()},
        "generator": {"tool": "utils/prepare_ime_scel.py", "version": "1.1.0",
                      "sha256": ime.digest(Path(__file__).read_bytes()),
                      "options": {"extension": "scel", "abbrev_limit": args.abbrev_limit,
                                  "profile": args.profile, "with_base": args.with_base}},
    }
    if conversion is not None:
        manifest["conversion"] = conversion
    if base is not None:
        manifest["combined"] = base
    (args.output_dir / "manifest.json").write_bytes(ime.encoded(manifest))
    (args.output_dir / "rejected.jsonl").write_text(
        "".join(json.dumps(row, ensure_ascii=False) + "\n" for row in rejected), encoding="utf-8")
    report = {"sources": reports, "source_records": sum(r["records"] for r in reports.values()),
              "unique_postings": len(entries), "unique_words": len({word for word, _ in entries}),
              "rejected_records": len(rejected), "merged_duplicates": sum(r["duplicates"] for r in reports.values()),
              "files": manifest["files"], "manifest_sha256": ime.digest(ime.encoded(manifest))}
    if base is not None:
        report["combined"] = base
    if conversion is not None:
        report["conversion"] = conversion
    (args.output_dir / "import-report.json").write_bytes(ime.encoded(report))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("fetch", "prepare", "validate"))
    parser.add_argument("--source-dir", type=Path, default=ime.ROOT / "temp/ime_sources")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--profile", choices=ime.PROFILES, default="zh-Hans")
    parser.add_argument("--with-base", action="store_true", help="combine standard and Bible entries for the demo")
    parser.add_argument("--abbrev-limit", type=int, default=64)
    args = parser.parse_args()
    args.output_dir = args.output_dir or ime.ROOT / "lmd/package/ime/chinese/data" / (
        args.profile + "-bible" + ("-combined" if args.with_base else ""))
    if args.abbrev_limit < 1:
        parser.error("abbrev-limit must be positive")
    if args.output_dir.resolve().is_relative_to(args.source_dir.resolve()):
        parser.error("output-dir must not be inside source-dir")
    lock = json.loads(LOCK_PATH.read_bytes())
    base = json.loads(ime.LOCK_PATH.read_bytes())
    names = ime.input_source_names(base, args.profile) if args.with_base else (
        ["unihan", "opencc"] if args.profile == "zh-Hant" else ["unihan"])
    try:
        if args.command == "fetch":
            ime.fetch_sources(args.source_dir, lock)
            ime.fetch_sources(args.source_dir, base, names)
            print(json.dumps({"ok": True, "sources": list(lock["sources"])}))
            return 0
        sources = ime.read_sources(args.source_dir, lock)
        base_sources = ime.read_sources(args.source_dir, base, names)
        unicode_source = base_sources["unihan"]
        chars, _, _ = ime.read_unihan(unicode_source["Unihan.zip"])
        han, syllables = ime.HanScript(unicode_source["Scripts.txt"]), ime.load_syllables()
        entries, reports, rejected = collect(sources, syllables, chars, han)
        converter = ime.TraditionalConverter(base_sources["opencc"]) if args.profile == "zh-Hant" else None
        profile_chars = converter.character_set(chars, han) if converter else chars
        conversion, combined_stats = None, None
        if converter:
            entries, converted = ime.traditional_entries(entries, converter, syllables, profile_chars, han)
            conversion = {"bible": converted}
        if args.with_base:
            data = ime.records(base_sources)
            scales = ime.frequency_scales(data, syllables, chars, han)
            base_entries, _, _, _, base_conversion = ime.assemble_profile(
                data, "both", syllables, chars, han, scales, converter)
            entries, combined_stats = combine(base_entries, entries)
            combined_stats.update(standard_extension="both", frequency_scales=scales)
            if converter:
                conversion["base"] = base_conversion
        combined = {"unicode_version": base["unicode_version"],
                    "sources": {**lock["sources"], **{name: base["sources"][name] for name in names}}}
        report = prepare(args, combined, entries, reports, rejected, profile_chars, conversion, combined_stats,
                         {**sources, **base_sources}) if args.command == "prepare" else None
        result = ime.validate_pack(args.output_dir, syllables, profile_chars, han, combined, entries, conversion,
                                   source_names=combined["sources"])
        print(json.dumps({**result, "output_dir": str(args.output_dir),
                          "import": report}, ensure_ascii=False, indent=2))
        return 0 if result["ok"] else 1
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"SCEL preparation: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
