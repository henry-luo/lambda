#!/usr/bin/env python3
"""Offline pack-contract tests; fixtures are authored, not upstream dictionaries."""

from collections import defaultdict
from decimal import Decimal
import json
import lzma
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "utils"))
import prepare_ime_chinese as ime


class PrepareChineseTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "temp").mkdir(exist_ok=True)
        self.scratch = tempfile.TemporaryDirectory(prefix="ime-contract-", dir=ROOT / "temp")
        self.addCleanup(self.scratch.cleanup)
        self.directory = Path(self.scratch.name)
        self.syllables = ime.load_syllables()
        self.han = ime.HanScript(b"3007 ; Han # zero\n4E00..9FFF ; Han # unified ideographs\n")
        self.entries = {("北京", ("bei", "jing")): 4000, ("背景", ("bei", "jing")): 3900,
                        ("中国", ("zhong", "guo")): 5000, ("资格", ("zi", "ge")): 5100,
                        ("行", ("xing",)): 4500, ("行", ("hang",)): 3500,
                        ("〇", ("ling",)): 0}
        self.chars = set("".join(word for word, _ in self.entries))
        self.scales = {name: {"scale": "1", "overlap_words": 1} for name in ("jieba", "hanlp")}
        self.sources = {name: {"NOTICE" if name == "aosp" else "LICENSE": b"Fixture notice"}
                        for name in ("aosp", "pinyin_data", "unihan")}
        self.lock = {"unicode_version": "18.0.0", "sources": {
            name: {"license": "MIT", "revision": "fixture", "provenance_flags": [], "files": {}}
            for name in self.sources}}
        self.manifest = ime.write_pack(self.directory, self.entries, {}, {}, "aosp", 1,
                                       self.scales, self.chars, self.sources, self.lock)

    def validate(self):
        return ime.validate_pack(self.directory, self.syllables, self.chars, self.han, self.lock)

    def assert_rejected(self, substring=None):
        result = self.validate()
        self.assertFalse(result["ok"], result)
        if substring:
            self.assertIn(substring, "\n".join(result["errors"]))

    def mutate_table(self, name, raw, rehash=True):
        (self.directory / (name + ".txt")).write_bytes(raw)
        compressed = lzma.compress(raw, preset=6)
        (self.directory / (name + ".txt.xz")).write_bytes(compressed)
        if rehash:
            info = self.manifest["files"][name]
            info.update(bytes=len(raw), sha256=ime.digest(raw), xz_bytes=len(compressed), xz_sha256=ime.digest(compressed))
            self.save_manifest()

    def save_manifest(self):
        (self.directory / "manifest.json").write_bytes(ime.encoded(self.manifest))

    def lexicon(self):
        return (self.directory / "lexicon.txt").read_bytes()

    def test_fixture_pack_passes(self):
        self.assertEqual(self.validate(), {"ok": True, "errors": []})

    def test_canonical_serialization_has_independent_expected_bytes(self):
        expected = ("1:hang\t行 3500\n1:ling\t〇 0\n1:xing\t行 4500\n"
                    "2:bei'jing\t北京 4000\t背景 3900\n"
                    "2:zhong'guo\t中国 5000\n2:zi'ge\t资格 5100\n")
        self.assertEqual(self.lexicon(), expected.encode())

    def test_deterministic_rebuild_in_another_directory(self):
        other = self.directory / "rebuilt"
        ime.write_pack(other, dict(reversed(list(self.entries.items()))), {}, {}, "aosp", 1,
                       self.scales, self.chars, self.sources, self.lock)
        for path in other.iterdir():
            self.assertEqual(path.read_bytes(), (self.directory / path.name).read_bytes(), path.name)

    def test_tone_normalization(self):
        for source, target in (("lǜ", "lv"), ("nüè", "nve"), ("LÜE4", "lve"), ("lu:e4", "lve"),
                               ("zhōng", "zhong"), ("ń", "n"), ("ḿ", "m"), ("ê", "ê"),
                               ("lue", "lve"), ("nue4", "nve"), ("juan", "juan"), ("lui", "lui")):
            with self.subTest(source=source):
                self.assertEqual(ime.toneless(source), target)

    def test_syllable_table_and_han_boundaries(self):
        self.assertIn("nv", self.syllables)
        self.assertIn("lve", self.syllables)
        for char, expected in (("〇", True), ("一", True), ("A", False), ("。", False), ("龿", True)):
            self.assertEqual(self.han.contains(char), expected)

    def test_initials_limit_is_per_orthographic_sequence(self):
        _, rows = ime.table_rows(self.entries, 1)
        self.assertEqual(len(rows["2:z'g"]), 2)  # z'g and zh'g retain their own top entry.
        self.assertEqual(rows["2:b'j"], [("北京", "bei'jing", 4000)])
        self.assertEqual(ime.initial("an"), "")
        self.assertEqual(ime.initial("zhong"), "zh")

    def test_weight_zero_and_monotonicity(self):
        self.assertEqual(ime.weight(Decimal(0)), 0)
        self.assertEqual(ime.weight(Decimal(1)), 256)
        self.assertEqual(ime.weight(Decimal(3)), 512)
        values = [ime.weight(Decimal(n)) for n in (1, 10, 100, 1000, 100000)]
        self.assertEqual(values, sorted(set(values)))
        self.assertEqual(ime.weight(Decimal("1e100")), 65535)

    def test_source_digest_mismatch_is_not_repaired(self):
        source = self.directory / "input/fixture"
        source.mkdir(parents=True)
        (source / "data").write_bytes(b"unexpected")
        lock = {"sources": {"fixture": {"files": {"data": {"bytes": 1, "sha256": "0" * 64}}}}}
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            ime.read_sources(source.parent, lock)

    def test_unsorted_lines(self):
        lines = self.lexicon().splitlines(keepends=True)
        self.mutate_table("lexicon", b"".join(reversed(lines)))
        self.assert_rejected("keys not strictly sorted")

    def test_duplicate_key(self):
        self.mutate_table("lexicon", self.lexicon() + self.lexicon().splitlines(keepends=True)[-1])
        self.assert_rejected("keys not strictly sorted")

    def test_truncated_table(self):
        self.mutate_table("lexicon", self.lexicon()[:-1], rehash=False)
        self.assert_rejected("size, path or hash mismatch")

    def test_invalid_encodings_and_line_endings(self):
        original = self.lexicon()
        for raw in (b"\xef\xbb\xbf" + original, original.replace(b"\n", b"\r\n"), original[:-1], b"\xff\n", original + b"\n"):
            with self.subTest(raw=raw[:10]):
                self.mutate_table("lexicon", raw)
                self.assert_rejected()

    def test_malformed_keys_and_postings(self):
        for line in ("0:bei\t北 1\n", "2:bei\t北京 1\n", "1:notpinyin\t北 1\n",
                     "1:bei\t北 01\n", "1:bei\t北 -1\n", "1:bei\t北 65536\n",
                     "1:bei\t北 1 2\n", "1:bei\t北 1\t北 1\n", "1:bei\n",
                     "1:bei\tA 1\n", "1:bei\t。 1\n", "1:bei\t北\u2028 1\n"):
            with self.subTest(line=line):
                self.mutate_table("lexicon", line.encode())
                self.assert_rejected()

    def test_posting_order(self):
        raw = self.lexicon().replace("北京 4000\t背景 3900".encode(), "背景 3900\t北京 4000".encode())
        self.mutate_table("lexicon", raw)
        self.assert_rejected("postings not in")

    def test_wrong_abbreviation_weight(self):
        raw = (self.directory / "abbrev.txt").read_bytes().replace(b"4000", b"3999")
        self.mutate_table("abbrev", raw)
        self.assert_rejected("same-weight")

    def test_wrong_abbreviation_reading(self):
        raw = (self.directory / "abbrev.txt").read_bytes().replace(b"bei'jing", b"bei'guo")
        self.mutate_table("abbrev", raw)
        self.assert_rejected("initials do not match")

    def test_missing_top_initial_group(self):
        raw = (self.directory / "abbrev.txt").read_bytes()
        raw = raw.replace("\t中国 zhong'guo 5000".encode(), b"")
        self.mutate_table("abbrev", raw)
        self.assert_rejected("per-initial top-K")

    def test_wrong_top_k_member(self):
        raw = (self.directory / "abbrev.txt").read_bytes().replace("北京 bei'jing 4000".encode(), "背景 bei'jing 3900".encode())
        self.mutate_table("abbrev", raw)
        self.assert_rejected("per-initial top-K")

    def test_wrong_manifest_fields(self):
        for field, value in (("format", "other"), ("format_version", 2), ("profile", "unknown"),
                             ("syllable_table", "0" * 64), ("abbrev_limit", 0)):
            old = self.manifest[field]
            self.manifest[field] = value
            self.save_manifest()
            self.assert_rejected()
            self.manifest[field] = old

    def test_wrong_manifest_statistics(self):
        self.manifest["files"]["lexicon"]["postings"] += 1
        self.manifest["weights"]["zero_postings"] += 1
        self.save_manifest()
        self.assert_rejected("manifest counts")
        self.assert_rejected("weight statistics")

    def test_manifest_cannot_drop_source_identity(self):
        del self.manifest["sources"]["aosp"]
        self.save_manifest()
        self.assert_rejected("source identity")

    def test_bad_xz_and_notices(self):
        (self.directory / "lexicon.txt.xz").write_bytes(b"bad xz")
        (self.directory / "NOTICE.txt").write_bytes(b"missing notices")
        self.assert_rejected("invalid XZ")
        self.assert_rejected("NOTICE")

    def test_non_nfc_and_outside_profile(self):
        # U+F900 normalizes to a unified ideograph; prepared words must already be NFC.
        self.mutate_table("lexicon", "1:bei\t豈 1\n1:jing\t京 1\n1:zi\t字 1\n".encode())
        self.assert_rejected("invalid word")

    def test_controls_round_trip(self):
        controls = ime.size_controls(self.entries, 1)
        self.assertEqual(set(controls), {"lexicon_front_keys", "abbrev_front_keys", "abbrev_entry_ids"})
        self.assertTrue(all(row["bytes"] > 0 for row in controls.values()))

    def test_no_guessed_polyphonic_readings(self):
        data = {"aosp": {}, "chars": defaultdict(set, {"北": {"bei"}, "京": {"jing"}, "行": {"hang", "xing"}}),
                "phrase": [("行北", ("hang", "bei"))], "hanlp": [],
                "jieba_freq": {"北京": 10, "行京": 20, "行北": 30}, "hanlp_freq": {}}
        self.chars.update("北京行")
        entries, dropped, _ = ime.assemble(data, "phrase", self.syllables, self.chars, self.han, self.scales)
        self.assertIn(("北京", ("bei", "jing")), entries)
        self.assertIn(("行北", ("hang", "bei")), entries)
        self.assertNotIn(("行京", ("hang", "jing")), entries)
        self.assertNotIn(("行京", ("xing", "jing")), entries)
        self.assertEqual(dropped["phrase:ambiguous_or_missing_derived_reading"], 2)

    def test_invalid_alternative_does_not_make_derivation_unambiguous(self):
        data = {"aosp": {}, "chars": {"北": {"bei", "invalid"}, "京": {"jing"}},
                "phrase": [], "hanlp": [], "jieba_freq": {"北京": 10}, "hanlp_freq": {}}
        entries, _, _ = ime.assemble(data, "phrase", self.syllables, self.chars, self.han, self.scales)
        self.assertNotIn(("北京", ("bei", "jing")), entries)


if __name__ == "__main__":
    unittest.main()
