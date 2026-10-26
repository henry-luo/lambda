#!/usr/bin/env python3
"""Authored SCEL records exercise decoding, loss accounting and pack invariants."""

import argparse
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "utils"))
import prepare_ime_chinese as ime
import prepare_ime_scel as scel


def text(value):
    raw = value.encode("utf-16le")
    return struct.pack("<H", len(raw)) + raw


def fixture(groups=None):
    raw = bytearray(scel.WORD_START)
    raw[:len(scel.SIGNATURE)] = scel.SIGNATURE
    title = "Authored fixture".encode("utf-16le")
    raw[0x130:0x130 + len(title)] = title
    pinyin = struct.pack("<I", 4) + b"".join(
        struct.pack("<H", index) + text(value) for index, value in enumerate(("bei", "jing", "lv", "lue")))
    raw[scel.PINYIN_START:scel.PINYIN_START + len(pinyin)] = pinyin
    for reading, words in groups or [((0, 1), ["北京", "背景"])]:
        indices = b"".join(struct.pack("<H", index) for index in reading)
        raw.extend(struct.pack("<HH", len(words), len(indices)) + indices)
        for index, word in enumerate(words, 1):
            raw.extend(text(word) + struct.pack("<HH", 10, index) + bytes(8))
    return bytes(raw)


class ScelTests(unittest.TestCase):
    def test_decodes_explicit_readings_and_homonyms(self):
        records, details = scel.read_scel(fixture())
        self.assertEqual(records, [("北京", ("bei", "jing")), ("背景", ("bei", "jing"))])
        self.assertEqual(details, {"title": "Authored fixture", "records": 2,
                                  "extension_prefix_is_ordinal_permutation": True})

    def test_canonical_umlaut_and_nfc(self):
        records, _ = scel.read_scel(fixture([((2, 3), ["旅略"])]))
        self.assertEqual(records, [("旅略", ("lv", "lve"))])

    def test_bad_headers_truncation_and_trailing_garbage_are_rejected(self):
        raw = fixture()
        for bad in (b"<html>download error</html>", b"?" + raw[1:], raw[:-1], raw + b"\x00"):
            with self.subTest(length=len(bad)), self.assertRaises(ValueError):
                scel.read_scel(bad)

    def test_unknown_pinyin_reference_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unknown SCEL pinyin index"):
            scel.read_scel(fixture([((999,), ["北"])]))

    def test_odd_utf16_and_index_lengths_are_rejected(self):
        for offset in (scel.PINYIN_START + 6, scel.WORD_START + 2, scel.WORD_START + 8):
            raw = bytearray(fixture())
            struct.pack_into("<H", raw, offset, 1)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                scel.read_scel(bytes(raw))

    def test_duplicate_pinyin_id_is_rejected(self):
        raw = bytearray(fixture())
        struct.pack_into("<H", raw, scel.PINYIN_START + 14, 0)
        with self.assertRaisesRegex(ValueError, "duplicate SCEL pinyin index"):
            scel.read_scel(bytes(raw))

    def test_merge_preserves_readings_and_accounts_for_all_losses(self):
        sources = {"first": {"a.scel": fixture()},
                   "second": {"b.scel": fixture([((0, 1), ["北京", "北A", "北北", "北", "龘北"]),
                                                 ((0,) * 10, ["北" * 10])])}}
        han = ime.HanScript(b"4E00..9FFF ; Han\n")
        entries, report, rejected = scel.collect(sources, ime.load_syllables(), set("北京背景"), han)
        self.assertEqual(entries, {("北京", ("bei", "jing")): 0,
                                   ("背景", ("bei", "jing")): 0,
                                   ("北北", ("bei", "jing")): 0})
        self.assertEqual(report["second"]["duplicates"], 1)
        self.assertEqual(report["second"]["dropped"], {"non_han": 1, "syllable_count": 1,
                                                        "outside_character_set": 1, "over_nine_syllables": 1})
        self.assertEqual(len(entries) + len(rejected) + 1, sum(r["records"] for r in report.values()))

    def test_supplemental_pack_uses_common_validation_and_rejects_wrong_sources(self):
        (ROOT / "temp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="scel-contract-", dir=ROOT / "temp") as scratch:
            directory = Path(scratch)
            (directory / "unihan").mkdir()
            (directory / "unihan/LICENSE").write_text("Fixture notice\n")
            args = argparse.Namespace(source_dir=directory, output_dir=directory / "pack", abbrev_limit=64,
                                      profile="zh-Hans", with_base=False)
            entries = {("北京", ("bei", "jing")): 0, ("背景", ("bei", "jing")): 0}
            chars = set("北京背景")
            han = ime.HanScript(b"4E00..9FFF ; Han\n")
            lock = {"unicode_version": "18.0.0", "sources": {"authored": {
                "license": "MIT", "revision": "fixture", "provenance_flags": []}}}
            report = {"authored": {"accepted": 2, "records": 2, "duplicates": 0, "dropped": {}}}
            notices = {"authored": {"LICENSE": b"Fixture notice\n"}}
            scel.prepare(args, lock, entries, report, [], chars, sources=notices)

            def validate():
                return ime.validate_pack(args.output_dir, ime.load_syllables(), chars, han, lock, entries,
                                         source_names=lock["sources"])

            self.assertEqual(validate(), {"ok": True, "errors": []})
            self.assertFalse(ime.validate_pack(args.output_dir, ime.load_syllables(), chars, han, lock)["ok"])
            original = {p.name: p.read_bytes() for p in args.output_dir.iterdir()}
            scel.prepare(args, lock, dict(reversed(list(entries.items()))), report, [], chars, sources=notices)
            self.assertEqual(original, {p.name: p.read_bytes() for p in args.output_dir.iterdir()})
            path = args.output_dir / "manifest.json"
            manifest = json.loads(path.read_bytes())
            manifest["sources"] = {}
            path.write_bytes(ime.encoded(manifest))
            self.assertIn("manifest: source identity mismatch", validate()["errors"])

    def test_combine_keeps_base_weights_and_polyphonic_readings(self):
        base = {("行", ("xing",)): 4000, ("行", ("hang",)): 2000, ("北京", ("bei", "jing")): 3000}
        extra = {("行", ("xing",)): 0, ("北行", ("bei", "hang")): 0}
        entries, stats = scel.combine(base, extra)
        self.assertEqual(entries, {**base, ("北行", ("bei", "hang")): 0})
        self.assertEqual(stats, {"base_postings": 3, "bible_postings": 2,
                                 "overlapping_postings": 1, "added_postings": 1})
        self.assertEqual(base[("行", ("xing",))], 4000)

    def test_traditional_conversion_precedes_combining_and_rebuilds_abbreviations(self):
        converter = ime.TraditionalConverter({"STCharacters.txt": "马\t馬\n国\t國\n".encode(),
                                              "STPhrases.txt": "马国\t馬國\n".encode()})
        han = ime.HanScript(b"4E00..9FFF ; Han\n")
        chars = set("馬國北")
        extra, conversion = ime.traditional_entries({("马国", ("ma", "guo")): 0}, converter,
                                                     ime.load_syllables(), chars, han)
        entries, stats = scel.combine({("北國", ("bei", "guo")): 4000}, extra)
        self.assertEqual(entries, {("馬國", ("ma", "guo")): 0, ("北國", ("bei", "guo")): 4000})
        lexicon, abbrev = ime.table_rows(entries, 64)
        self.assertEqual(lexicon["2:ma'guo"], [("馬國", 0)])
        self.assertEqual(abbrev["2:m'g"], [("馬國", "ma'guo", 0)])
        self.assertEqual(conversion["dropped"], {})


if __name__ == "__main__":
    unittest.main()
