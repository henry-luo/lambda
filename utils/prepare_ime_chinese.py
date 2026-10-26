#!/usr/bin/env python3
"""Prepare and qualify the adopted Chinese IME pack v1 (IME6, proposal §6).

fetch downloads only checksum-pinned inputs. prepare and validate are offline;
they do not load a runtime dictionary or import an IME engine. See --help.
"""

import argparse
import base64
from bisect import bisect_right
from collections import Counter, defaultdict
from decimal import Decimal, ROUND_HALF_UP, localcontext
from functools import lru_cache
import json
import lzma
from pathlib import Path
import re
from statistics import median
import unicodedata
import urllib.request
import zipfile

from spelling_vocab_poc import digest, encoded


ROOT = Path(__file__).resolve().parent.parent
LOCK_PATH = ROOT / "utils/ime_chinese_sources.json"
SYLLABLE_PATH = ROOT / "lmd/package/ime/chinese/syllables.txt"
FORMAT = "lambda.ime.chinese.pack"
VERSION = "1.1.0"
MODES = ("aosp", "phrase", "hanlp", "both")
PROFILES = {"zh-Hans": ("Hans", "kTGHZ2013"),
            "zh-Hant": ("Hant", "OpenCC STCharacters/STPhrases of kTGHZ2013")}


def load_syllables(path=SYLLABLE_PATH):
    raw = path.read_bytes()
    words = raw.decode("ascii").splitlines()
    if not raw.endswith(b"\n") or words != sorted(set(words)) or any(
            not re.fullmatch("[a-z]+", word) for word in words):
        raise ValueError("syllable table must be sorted unique lowercase ASCII with final LF")
    return frozenset(words)


def input_source_names(lock, profile):
    return [name for name in lock["sources"] if name != "opencc" or profile == "zh-Hant"]


def read_sources(directory, lock, names=None):
    sources = {}
    for name, source in lock["sources"].items():
        if names is not None and name not in names:
            continue
        sources[name] = {}
        for filename, entry in source["files"].items():
            raw = (directory / name / filename).read_bytes()
            if len(raw) != entry["bytes"] or digest(raw) != entry["sha256"]:
                raise ValueError(f"source checksum mismatch: {name}/{filename}")
            sources[name][filename] = raw
    return sources


def fetch_sources(directory, lock, names=None):
    for name, source in lock["sources"].items():
        if names is not None and name not in names:
            continue
        for filename, entry in source["files"].items():
            path = directory / name / filename
            if path.exists():
                raw = path.read_bytes()
            else:
                with urllib.request.urlopen(entry["url"], timeout=60) as response:
                    raw = response.read()
                if name == "aosp":
                    raw = base64.b64decode(raw, validate=False)
            if len(raw) != entry["bytes"] or digest(raw) != entry["sha256"]:
                raise ValueError(f"source checksum mismatch: {name}/{filename}")
            if not path.exists():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(raw)


def toneless(value):
    # Strip tone marks, retaining the umlaut's distinction before writing ASCII v.
    decomposed = unicodedata.normalize("NFD", value.lower().replace("u:", "v"))
    value = "".join(c for c in decomposed if c not in "\u0300\u0301\u0304\u030c")
    value = re.sub("[1-5]$", "", unicodedata.normalize("NFC", value).replace("ü", "v"))
    # AOSP omits the umlaut in n/l + ue; canonical pack spellings retain it as v.
    return re.sub(r"^([nl])ue$", r"\1ve", value)


def initial(syllable):
    if syllable.startswith(("zh", "ch", "sh")):
        return syllable[:2]
    return syllable[0] if syllable[0] not in "aoe" else ""


class HanScript:
    def __init__(self, raw):
        self.ranges = []
        for line in raw.decode("utf-8").splitlines():
            body = line.split("#", 1)[0].strip()
            if not body:
                continue
            interval, script = (part.strip() for part in body.split(";"))
            if script == "Han":
                points = interval.split("..")
                self.ranges.append((int(points[0], 16), int(points[-1], 16)))
        self.ranges.sort()
        self.starts = [start for start, _ in self.ranges]

    def contains(self, char):
        code = ord(char)
        at = bisect_right(self.starts, code) - 1
        return at >= 0 and code <= self.ranges[at][1]


class TraditionalConverter:
    """IME11: longest phrase first, then every unresolved character alternative."""

    def __init__(self, source):
        self.characters = self.read_table(source["STCharacters.txt"], "STCharacters", single=True)
        self.phrases = self.read_table(source["STPhrases.txt"], "STPhrases")
        self.trie = {}
        for phrase, forms in self.phrases.items():
            node = self.trie
            for char in phrase:
                node = node.setdefault(char, {})
            node[None] = forms

    @staticmethod
    def read_table(raw, label, single=False):
        table, seen = {}, set()
        for number, line in enumerate(raw.decode("utf-8").splitlines(), 1):
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) != 2 or not fields[0] or fields[0] in seen or any(c.isspace() for c in fields[0]):
                raise ValueError(f"{label}:{number}: invalid or duplicate OpenCC key")
            seen.add(fields[0])
            key = unicodedata.normalize("NFC", fields[0])
            forms = fields[1].split(" ")
            if any(not form or any(c.isspace() for c in form) for form in forms):
                raise ValueError(f"{label}:{number}: invalid OpenCC forms")
            forms = {unicodedata.normalize("NFC", form) for form in forms}
            if single and (len(key) != 1 or any(len(form) != 1 for form in forms)):
                raise ValueError(f"{label}:{number}: character mapping must preserve one code point")
            # Compatibility keys can become identical after NFC; retain their union.
            table[key] = tuple(sorted(set(table.get(key, ())) | forms, key=lambda value: value.encode()))
        if not table:
            raise ValueError(f"{label}: empty OpenCC dictionary")
        return table

    @lru_cache(maxsize=4096)
    def forms(self, word):
        at, results = 0, {""}
        while at < len(word):
            node, end, matched = self.trie, at, None
            for cursor in range(at, len(word)):
                node = node.get(word[cursor])
                if node is None:
                    break
                if None in node:
                    end, matched = cursor + 1, node[None]
            if matched is None:
                end, matched = at + 1, self.characters.get(word[at], (word[at],))
            results = {prefix + form for prefix in results for form in matched}
            at = end
        return tuple(sorted(results, key=lambda value: value.encode()))

    def character_set(self, base_chars, han):
        chars = {form for char in base_chars for form in self.forms(char)}
        # Phrase exceptions may introduce glyphs absent from a character's fallback.
        for key, forms in self.phrases.items():
            if len(key) <= 9 and all(char in base_chars for char in key):
                chars.update(char for form in forms if len(form) == len(key)
                             for char in form if han.contains(char))
        if any(len(char) != 1 or not han.contains(char) for char in chars):
            raise ValueError("OpenCC character conversion produced a non-Han character")
        return chars


def traditional_entries(entries, converter, syllables, chars, han):
    converted, dropped, stats = {}, Counter(), Counter()
    max_forms = 0
    for (word, reading), frequency in entries.items():
        forms = converter.forms(word)
        max_forms = max(max_forms, len(forms))
        stats["ambiguous_source_postings"] += len(forms) > 1
        stats["changed_source_postings"] += forms != (word,)
        for form in forms:
            reason = invalid_pair(form, reading, syllables, chars, han)
            if reason:
                dropped[reason] += 1
                continue
            stats["emitted_postings_before_merge"] += 1
            pair = (form, reading)
            stats["merged_postings"] += pair in converted
            # Several Simplified spellings can converge; readings stay separate.
            converted[pair] = max(converted.get(pair, 0), frequency)
    return converted, {
        "source_profile": "zh-Hans", "method": "longest STPhrases match, then STCharacters; retain every alternative",
        "source_lexicon_sha256": digest(table_bytes(table_rows(entries, 1)[0])),
        "source_postings": len(entries), "postings": len(converted), "max_forms_per_source": max_forms,
        "collision_weight": "maximum source weight for identical word/reading",
        "dropped": dict(sorted(dropped.items())), **dict(stats)}


def assemble_profile(data, mode, syllables, base_chars, han, scales, converter=None):
    entries, dropped, accepted = assemble(data, mode, syllables, base_chars, han, scales)
    chars, conversion = base_chars, None
    if converter is not None:
        chars = converter.character_set(base_chars, han)
        entries, conversion = traditional_entries(entries, converter, syllables, chars, han)
        dropped = {**dropped, **{"opencc:" + key: value for key, value in conversion["dropped"].items()}}
    return entries, dropped, accepted, chars, conversion


def read_unihan(raw):
    from io import BytesIO

    chars, readings, frequencies = set(), defaultdict(set), defaultdict(dict)
    with zipfile.ZipFile(BytesIO(raw)) as archive:
        text = archive.read("Unihan_Readings.txt").decode("utf-8")
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        point, prop, value = line.split("\t")
        char = chr(int(point[2:], 16))
        if prop == "kTGHZ2013":
            chars.add(char)
        if prop in ("kMandarin", "kHanyuPinyin", "kXHC1983", "kTGHZ2013"):
            # Indexed reading properties put one or more location codes before ':'.
            for token in value.split():
                readings[char].update(toneless(s) for s in token.rsplit(":", 1)[-1].split(","))
        if prop == "kHanyuPinlu":
            for syllable, count in re.findall(r"([^\s(]+)\((\d+)\)", value):
                frequencies[char][toneless(syllable)] = int(count)
    return chars, readings, frequencies


def records(sources):
    """Retain source readings before filtering, including polyphonic alternatives."""
    chars = defaultdict(set)
    for line in sources["pinyin_data"]["pinyin.txt"].decode("utf-8").splitlines():
        body = line.split("#", 1)[0].strip()
        if body:
            point, readings = body.split(":", 1)
            char = unicodedata.normalize("NFC", chr(int(point[2:], 16)))
            chars[char].update(toneless(s.strip()) for s in readings.split(","))
    aosp, flags = {}, Counter()
    for line in sources["aosp"]["rawdict_utf16_65105_freq.txt"].decode("utf-16").splitlines():
        word, frequency, flag, *reading = line.split()
        word = unicodedata.normalize("NFC", word)
        pair = (word, tuple(toneless(s) for s in reading))
        aosp[pair] = max(aosp.get(pair, Decimal(0)), Decimal(frequency))
        flags[flag] += 1
        if len(word) == 1:
            chars[word].update(pair[1])
    phrase = []
    for line in sources["phrase_pinyin_data"]["pinyin.txt"].decode("utf-8").splitlines():
        body = line.split("#", 1)[0].strip()
        if body:
            word, reading = body.split(":", 1)
            phrase.append((unicodedata.normalize("NFC", word), tuple(toneless(s) for s in reading.split())))
    hanlp = []
    for line in sources["hanlp"]["pinyin.txt"].decode("utf-8-sig").splitlines():
        word, reading = line.split("=", 1)
        hanlp.append((unicodedata.normalize("NFC", word), tuple(toneless(s) for s in reading.split(","))))
    jieba_freq, hanlp_freq = {}, {}
    for line in sources["jieba"]["dict.txt"].decode("utf-8").splitlines():
        word, frequency, _ = line.split()
        word = unicodedata.normalize("NFC", word)
        jieba_freq[word] = max(jieba_freq.get(word, 0), int(frequency))
    for line in sources["hanlp"]["CoreNatureDictionary.txt"].decode("utf-8-sig").splitlines():
        word, *fields = line.split("\t")
        word = unicodedata.normalize("NFC", word)
        frequency = sum(int(count) for count in fields[1::2])
        hanlp_freq[word] = max(hanlp_freq.get(word, 0), frequency)
    return {"chars": chars, "aosp": aosp, "phrase": phrase, "hanlp": hanlp,
            "jieba_freq": jieba_freq, "hanlp_freq": hanlp_freq, "gbk_flags": dict(flags)}


def invalid_pair(word, reading, syllables, chars, han):
    if not word or any(not han.contains(c) for c in word):
        return "non_han"
    if len(word) > 9:
        return "over_nine_syllables"
    if len(reading) != len(word):
        return "syllable_count"
    if any(c not in chars for c in word):
        return "outside_character_set"
    if any(s not in syllables for s in reading):
        return "invalid_syllable"
    return None


def frequency_scales(data, syllables, chars, han):
    aosp_words = {}
    for (word, reading), freq in data["aosp"].items():
        if len(word) > 1 and not invalid_pair(word, reading, syllables, chars, han):
            aosp_words[word] = max(aosp_words.get(word, Decimal(0)), freq)
    scales = {}
    with localcontext() as ctx:
        ctx.prec = 40
        for source in ("jieba", "hanlp"):
            frequencies = data[source + "_freq"]
            ratios = [freq / Decimal(frequencies[word]) for word, freq in aosp_words.items()
                      if freq > 0 and frequencies.get(word, 0) > 0]
            if not ratios:
                raise ValueError(f"no common positive-frequency words for {source} calibration")
            scales[source] = {"scale": str(median(ratios)), "overlap_words": len(ratios)}
    return scales


@lru_cache(maxsize=None)
def weight(frequency):
    if frequency <= 0:
        return 0
    # Decimal logarithms give the same rounded weights across platform libm versions.
    with localcontext() as ctx:
        ctx.prec = 40
        value = (Decimal(256) * (1 + frequency).ln() / Decimal(2).ln()).to_integral_value(rounding=ROUND_HALF_UP)
    return max(1, min(65535, int(value)))


def assemble(data, mode, syllables, chars, han, scales):
    admitted, dropped, accepted = {}, Counter(), Counter()

    def add(word, reading, frequency, source):
        reason = invalid_pair(word, reading, syllables, chars, han)
        if reason:
            dropped[source + ":" + reason] += 1
            return
        pair = (word, reading)
        admitted[pair] = max(admitted.get(pair, 0), weight(frequency))
        accepted[source] += 1

    for (word, reading), frequency in data["aosp"].items():
        add(word, reading, frequency, "aosp")
    for char, readings in data["chars"].items():
        for reading in sorted(readings):
            add(char, (reading,), Decimal(0), "pinyin_data_union")
    extensions = []
    if mode in ("phrase", "both"):
        extensions.append(("phrase", data["phrase"], "jieba"))
    if mode in ("hanlp", "both"):
        extensions.append(("hanlp", data["hanlp"], "hanlp"))
    for source, pairs, frequency_source in extensions:
        scale = Decimal(scales[frequency_source]["scale"])
        frequencies = data[frequency_source + "_freq"]
        for word, reading in pairs:
            if len(word) <= 1:
                continue  # Character weights remain per-reading AOSP evidence.
            with localcontext() as ctx:
                ctx.prec = 40
                frequency = Decimal(frequencies.get(word, 0)) * scale
            # AOSP owns the weight of its attested reading, not a word-level count.
            if (word, reading) in data["aosp"]:
                frequency = data["aosp"][(word, reading)]
            add(word, reading, frequency, source)
        for word, frequency in sorted(frequencies.items()):
            if len(word) <= 1:
                continue
            readings = [data["chars"].get(char, set()) for char in word]
            if any(len(options) != 1 for options in readings):
                dropped[source + ":ambiguous_or_missing_derived_reading"] += 1
                continue
            reading = tuple(next(iter(options)) for options in readings)
            if (word, reading) not in admitted:
                with localcontext() as ctx:
                    ctx.prec = 40
                    normalized = Decimal(frequency) * scale
                add(word, reading, normalized, source + "_unambiguous")
    return admitted, dict(sorted(dropped.items())), dict(accepted)


def table_rows(entries, abbrev_limit):
    lexicon, groups = defaultdict(list), defaultdict(list)
    for (word, reading), frequency in entries.items():
        key = f"{len(reading)}:" + "'".join(reading)
        lexicon[key].append((word, frequency))
        if len(reading) >= 2:
            key = f"{len(reading)}:" + "'".join(s[0] for s in reading)
            initials = tuple(initial(s) for s in reading)
            groups[(key, initials)].append((word, "'".join(reading), frequency))
    abbrev = defaultdict(list)
    for (key, _), postings in groups.items():
        abbrev[key].extend(sorted(postings, key=lambda p: (-p[2], p[0].encode(), p[1].encode()))[:abbrev_limit])
    for postings in lexicon.values():
        postings.sort(key=lambda p: (-p[1], p[0].encode()))
    for postings in abbrev.values():
        postings.sort(key=lambda p: (-p[2], p[0].encode(), p[1].encode()))
    return dict(sorted(lexicon.items())), dict(sorted(abbrev.items()))


def table_bytes(rows):
    return "".join(key + "\t" + "\t".join(" ".join(str(v) for v in p) for p in postings) + "\n"
                   for key, postings in rows.items()).encode("utf-8")


def row_counts(rows):
    counts = {}
    for key, postings in rows.items():
        count = counts.setdefault(key[0], {"lines": 0, "postings": 0})
        count["lines"] += 1
        count["postings"] += len(postings)
    return counts


def emit_table(directory, filename, rows):
    raw = table_bytes(rows)
    if not raw or len(raw) >= 2 ** 32:
        raise ValueError(f"empty or over-u32 table: {filename}")
    compressed = lzma.compress(raw, preset=6)
    (directory / filename).write_bytes(raw)
    (directory / (filename + ".xz")).write_bytes(compressed)
    return {"path": filename, "bytes": len(raw), "sha256": digest(raw),
            "xz_path": filename + ".xz", "xz_bytes": len(compressed), "xz_sha256": digest(compressed),
            "lines": len(rows), "postings": sum(len(p) for p in rows.values())}


def inspect_table(raw, abbreviation, syllables, chars, han, label, errors):
    rows = {}
    if not raw or len(raw) >= 2 ** 32 or not raw.endswith(b"\n") or b"\r" in raw or raw.startswith(b"\xef\xbb\xbf"):
        errors.append(f"{label}: invalid file size, BOM or line endings")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        errors.append(f"{label}: invalid UTF-8")
        return rows
    previous = b""
    # Split only LF: Unicode line separators are invalid word characters, not rows.
    for number, line in enumerate(text.removesuffix("\n").split("\n"), 1):
        context = f"{label}:{number}"
        fields = line.split("\t")
        key = fields[0]
        key_bytes = key.encode("utf-8")
        if key_bytes <= previous:
            errors.append(f"{context}: keys not strictly sorted and unique")
        previous = key_bytes
        if not re.fullmatch(r"[1-9]:[a-z]+(?:'[a-z]+)*", key):
            errors.append(f"{context}: invalid key")
            continue
        count, parts = int(key[0]), key[2:].split("'")
        if count != len(parts) or (abbreviation and (count < 2 or any(len(p) != 1 for p in parts))) or (
                not abbreviation and any(p not in syllables for p in parts)):
            errors.append(f"{context}: invalid key syllables or count")
        postings, seen = [], set()
        for field in fields[1:]:
            tokens = field.split(" ")
            if len(tokens) != (3 if abbreviation else 2) or not re.fullmatch(r"0|[1-9][0-9]{0,4}", tokens[-1]):
                errors.append(f"{context}: invalid posting syntax")
                continue
            word, frequency = tokens[0], int(tokens[-1])
            reading = tuple(tokens[1].split("'")) if abbreviation else tuple(parts)
            if frequency > 65535:
                errors.append(f"{context}: weight outside 0..65535")
            if unicodedata.normalize("NFC", word) != word or len(word) != count or invalid_pair(word, reading, syllables, chars, han):
                errors.append(f"{context}: invalid word or reading")
            if abbreviation and (len(reading) != count or [s[:1] for s in reading] != parts):
                errors.append(f"{context}: abbreviation initials do not match reading")
            identity = (word, reading) if abbreviation else word
            if identity in seen:
                errors.append(f"{context}: duplicate posting")
            seen.add(identity)
            postings.append((word, "'".join(reading), frequency) if abbreviation else (word, frequency))
        if not postings:
            errors.append(f"{context}: no postings")
        ordered = sorted(postings, key=lambda p: (-p[-1], p[0].encode(), p[1].encode() if abbreviation else b""))
        if postings != ordered:
            errors.append(f"{context}: postings not in weight/word/reading order")
        rows[key] = postings
    return rows


def pack_source_names(mode, profile):
    names = ["aosp", "pinyin_data", "unihan"]
    if mode in ("phrase", "both"):
        names += ["phrase_pinyin_data", "jieba"]
    if mode in ("hanlp", "both"):
        names += ["hanlp"]
    if profile == "zh-Hant":
        names += ["opencc"]
    return names


def character_set_identity(profile, chars):
    return {"rule": PROFILES[profile][1], "size": len(chars),
            "sha256": digest("".join(sorted(chars)).encode())}


def validate_pack(directory, syllables, chars, han, lock=None, expected_entries=None, expected_conversion=None):
    """Full preparation check; deliberately separate from normal runtime loading."""
    errors = []
    try:
        manifest = json.loads((directory / "manifest.json").read_bytes())
        profile = manifest["profile"]
        if manifest["format"] != FORMAT or type(manifest["format_version"]) is not int or manifest["format_version"] != 1 or profile not in PROFILES or manifest["script"] != PROFILES[profile][0]:
            errors.append("manifest: unsupported format, version or profile")
        if manifest["char_set"] != character_set_identity(profile, chars):
            errors.append("manifest: character-set identity mismatch")
        if manifest["syllable_table"] != digest(SYLLABLE_PATH.read_bytes()):
            errors.append("manifest: syllable-table hash mismatch")
        mode = manifest["generator"]["options"]["extension"]
        expected_sources = set(pack_source_names(mode, profile))
        if mode not in MODES or set(manifest["sources"]) != expected_sources or (
                lock is not None and (manifest["unicode_version"] != lock["unicode_version"] or any(
                    source != lock["sources"].get(name) for name, source in manifest["sources"].items()))):
            errors.append("manifest: source identity mismatch")
        limit = manifest["abbrev_limit"]
        if type(limit) is not int or limit < 1:
            errors.append("manifest: invalid abbrev_limit")
            limit = 1
        if manifest["generator"]["options"]["abbrev_limit"] != limit:
            errors.append("manifest: generator abbrev_limit mismatch")
        tables = {}
        for label in ("lexicon", "abbrev"):
            filename = label + ".txt"
            info = manifest["files"][label]
            raw = (directory / filename).read_bytes()
            compressed = (directory / (filename + ".xz")).read_bytes()
            if info["path"] != filename or info["xz_path"] != filename + ".xz" or (
                    len(raw), digest(raw), len(compressed), digest(compressed)) != (
                    info["bytes"], info["sha256"], info["xz_bytes"], info["xz_sha256"]):
                errors.append(f"{label}: size, path or hash mismatch")
            try:
                if lzma.decompress(compressed) != raw:
                    errors.append(f"{label}: XZ differs from uncompressed table")
            except lzma.LZMAError:
                errors.append(f"{label}: invalid XZ")
            rows = inspect_table(raw, label == "abbrev", syllables, chars, han, label, errors)
            tables[label] = rows
            if info["lines"] != len(rows) or info["postings"] != sum(len(p) for p in rows.values()) or manifest["counts"][label] != row_counts(rows):
                errors.append(f"{label}: manifest counts mismatch")
        entries = {(word, tuple(key[2:].split("'"))): frequency
                   for key, posts in tables["lexicon"].items() for word, frequency in posts}
        if profile == "zh-Hant" and (expected_entries is None or expected_conversion is None):
            errors.append("manifest: Traditional source conversion must be validated explicitly")
        if expected_entries is not None and entries != expected_entries:
            errors.append("lexicon: differs from source conversion (forms, readings or weights)")
        if manifest.get("conversion") != expected_conversion:
            errors.append("manifest: conversion provenance mismatch")
        _, expected = table_rows(entries, limit)
        # Re-derivation checks exact top-K membership as well as the four invariants.
        if tables["abbrev"] != expected:
            errors.append("abbrev: lexicon membership, same-weight or per-initial top-K invariant failed")
        weights = list(entries.values())
        if weights and any(manifest["weights"][field] != value for field, value in (
                ("minimum", min(weights)), ("maximum", max(weights)), ("zero_postings", weights.count(0)))):
            errors.append("manifest: weight statistics mismatch")
        notice = (directory / "NOTICE.txt").read_bytes()
        if manifest["notice"] != {"path": "NOTICE.txt", "bytes": len(notice), "sha256": digest(notice)}:
            errors.append("manifest: NOTICE size or hash mismatch")
    except (OSError, ValueError, KeyError, TypeError, AttributeError, IndexError) as error:
        errors.append(f"pack: {error}")
    return {"ok": not errors, "errors": errors}


def write_pack(directory, entries, dropped, accepted, mode, limit, scales, chars, sources, lock,
               profile="zh-Hans", conversion=None):
    directory.mkdir(parents=True, exist_ok=True)
    lexicon, abbrev = table_rows(entries, limit)
    selected = pack_source_names(mode, profile)
    notice = [f"Lambda Chinese IME {profile} pack, format version 1\n",
              "Prepared from modified upstream data: normalized readings, filtered entries,\n"
              "rescaled frequencies and derived abbreviation postings. No sentence model.\n"]
    if conversion is not None:
        notice.append("OpenCC phrase-then-character conversion; every unresolved form retains\n"
                      "its source reading and weight; identical word/readings merge by maximum weight.\n")
    for name in selected:
        source = lock["sources"][name]
        notice.append(f"\n--- {name} ({source['license']}, {source['revision']}) ---\n")
        notice.extend(flag + "\n" for flag in source["provenance_flags"])
        notice.append(sources[name]["NOTICE" if name == "aosp" else "LICENSE"].decode("utf-8"))
        if name == "hanlp":
            notice.append("\nHanLP: https://github.com/hankcs/HanLP (Apache-2.0)\n")
    raw_notice = "\n".join(notice).encode("utf-8")
    (directory / "NOTICE.txt").write_bytes(raw_notice)
    weights = list(entries.values())
    manifest = {
        "format": FORMAT, "format_version": 1, "profile": profile, "script": PROFILES[profile][0],
        "unicode_version": lock["unicode_version"],
        "char_set": character_set_identity(profile, chars),
        "syllable_table": digest(SYLLABLE_PATH.read_bytes()),
        "files": {"lexicon": emit_table(directory, "lexicon.txt", lexicon), "abbrev": emit_table(directory, "abbrev.txt", abbrev)},
        "notice": {"path": "NOTICE.txt", "bytes": len(raw_notice), "sha256": digest(raw_notice)},
        "counts": {"lexicon": row_counts(lexicon), "abbrev": row_counts(abbrev)},
        "weights": {"method": "round_half_up(256 * log2(1 + normalized_frequency)), clipped to 1..65535; zero without evidence",
                    "calibration": "median AOSP/frequency ratio over shared admissible multi-character words; AOSP reading weights retained",
                    "scales": scales, "minimum": min(weights), "maximum": max(weights), "zero_postings": weights.count(0)},
        "abbrev_limit": limit, "dropped": dropped, "accepted_source_records": accepted,
        "sources": {name: lock["sources"][name] for name in selected},
        "generator": {"tool": "utils/prepare_ime_chinese.py", "version": VERSION,
                      "sha256": digest(Path(__file__).read_bytes()),
                      "options": {"extension": mode, "abbrev_limit": limit, "profile": profile}},
    }
    if conversion is not None:
        manifest["conversion"] = conversion
    (directory / "manifest.json").write_bytes(encoded(manifest))
    return manifest


def size_controls(entries, limit):
    """Round-trip controls measure alternatives without changing adopted v1."""
    lexicon, abbrev = table_rows(entries, limit)
    controls = {}
    for label, rows in (("lexicon", lexicon), ("abbrev", abbrev)):
        previous, lines, restored = "", [], []
        for key, postings in rows.items():
            common = 0
            while common < min(len(previous), len(key)) and previous[common] == key[common]:
                common += 1
            suffix = key[common:]
            payload = "\t".join(" ".join(str(v) for v in p) for p in postings)
            lines.append(f"{common},{suffix}\t{payload}\n")
            previous = key
        raw = "".join(lines).encode()
        previous = ""
        for line in raw.decode().splitlines():
            delta, payload = line.split("\t", 1)
            common, suffix = delta.split(",", 1)
            previous = previous[:int(common)] + suffix
            restored.append(previous + "\t" + payload + "\n")
        if "".join(restored).encode() != table_bytes(rows):
            raise ValueError("front-key control round-trip failed")
        controls[label + "_front_keys"] = {"bytes": len(raw), "xz_bytes": len(lzma.compress(raw, preset=6))}
    identities = sorted(entries, key=lambda pair: (pair[0].encode(), "'".join(pair[1]).encode()))
    ids = {pair: i for i, pair in enumerate(identities)}
    lines = []
    for key, postings in abbrev.items():
        values = []
        for word, reading, frequency in postings:
            pair = (word, tuple(reading.split("'")))
            entry_id = ids[pair]
            if identities[entry_id] != pair or entries[pair] != frequency:
                raise ValueError("entry-ID control round-trip failed")
            values.append(f"{entry_id} {frequency}")
        lines.append(key + "\t" + "\t".join(values) + "\n")
    raw = "".join(lines).encode()
    controls["abbrev_entry_ids"] = {"bytes": len(raw), "xz_bytes": len(lzma.compress(raw, preset=6)),
                                     "identity": "zero-based (word, reading) byte order in the same lexicon build"}
    return controls


def coverage(entries, fixture):
    authored = json.loads(fixture.read_bytes())["cases"]
    cases = [{"word":word,"reading":case["reading"]} for case in authored
             for word in (case["forms"] if "forms" in case else [case["word"]])]
    missing = [case for case in cases if (case["word"], tuple(case["reading"].split("'"))) not in entries]
    return {"fixture_sha256": digest(fixture.read_bytes()), "total": len(cases),
            "covered": len(cases) - len(missing), "missing": missing,
            "scope": "authored smoke coverage only; held-out quality/ranking baseline remains M1"}


def prepare(args, lock):
    if args.output_dir.resolve().is_relative_to(args.source_dir.resolve()):
        raise ValueError("output-dir must not be inside source-dir")
    sources = read_sources(args.source_dir, lock, input_source_names(lock, args.profile))
    converter = TraditionalConverter(sources["opencc"]) if args.profile == "zh-Hant" else None
    syllables = load_syllables()
    chars, unihan_readings, pinlu = read_unihan(sources["unihan"]["Unihan.zip"])
    han = HanScript(sources["unihan"]["Scripts.txt"])
    data = records(sources)
    scales = frequency_scales(data, syllables, chars, han)
    comparisons = {}
    chosen = None
    for mode in MODES if args.compare else (args.extension,):
        entries, dropped, accepted, profile_chars, conversion = assemble_profile(
            data, mode, syllables, chars, han, scales, converter)
        lexicon, abbrev = table_rows(entries, args.abbrev_limit)
        tables = {"lexicon": lexicon, "abbrev": abbrev}
        comparisons[mode] = {
            "word_readings": sum(len(word) > 1 for word, _ in entries),
            "character_readings": sum(len(word) == 1 for word, _ in entries),
            "characters": len({word for word, _ in entries if len(word) == 1}),
            "files": {name: {"lines": len(rows), "postings": sum(len(p) for p in rows.values()),
                             "bytes": len(table_bytes(rows)), "xz_bytes": len(lzma.compress(table_bytes(rows), preset=6)),
                             "offset_payload_bytes": len(rows) * 4, "counts": row_counts(rows)} for name, rows in tables.items()},
            "dropped": dropped, "coverage": coverage(entries, args.coverage),
        }
        if mode == args.extension:
            chosen = entries, dropped, accepted, profile_chars, conversion
    entries, dropped, accepted, profile_chars, conversion = chosen
    manifest = write_pack(args.output_dir, entries, dropped, accepted, args.extension,
                          args.abbrev_limit, scales, profile_chars, sources, lock, args.profile, conversion)
    validation = validate_pack(args.output_dir, syllables, profile_chars, han, lock,
        entries if converter else None, conversion)
    if not validation["ok"]:
        raise ValueError("; ".join(validation["errors"]))
    crosscheck = [(char, reading[0]) for char, reading in entries if len(char) == 1
                  and reading[0] not in unihan_readings.get(char, set())]
    pinlu_disagreements = []
    for char, frequencies in pinlu.items():
        ranked = [(count, syllable, entries[(char, (syllable,))]) for syllable, count in frequencies.items()
                  if (char, (syllable,)) in entries and entries[(char, (syllable,))] > 0]
        if len(ranked) > 1 and max(ranked)[1] != max(ranked, key=lambda p: (p[2], p[1]))[1]:
            pinlu_disagreements.append(char)
    report = {"format_version": 1, "profile": args.profile, "selected_extension": args.extension,
              "conversion": conversion, "comparisons": comparisons,
              "controls": size_controls(entries, args.abbrev_limit), "validation": validation,
              "aosp_review": {"gbk_flags": data["gbk_flags"], "gbk_filter_applied": False,
                              "frequency_60_cutoff_applied": False,
                              "admitted_words_under_60": sum(len(word) > 1 and freq < 60 and (word, reading) in entries
                                                            for (word, reading), freq in data["aosp"].items())},
              "unihan_crosscheck": {"readings_not_recorded": len(crosscheck), "examples": crosscheck[:20],
                                    "pinlu_ranking_disagreements": pinlu_disagreements,
                                    "policy": "curated readings retained; measured frequency sources may disagree"},
              "manifest_sha256": digest(encoded(manifest)),
              "runtime_measurements": {"status": "separate release qualification: test/ime/run_engine.py",
                                       "index_payload_bytes": sum(info["bytes"] + 4 * info["lines"] for info in manifest["files"].values()),
                                       "note": "payload accounting excludes allocator/container overhead; no load or per-key timing claimed"}}
    if converter is not None:
        cases = [case for case in json.loads(args.coverage.read_bytes())["cases"] if "forms" in case]
        mismatches = [{"source":case["source"],"expected":case["forms"],
                       "actual":list(converter.forms(case["source"]))} for case in cases
                      if set(converter.forms(case["source"])) != set(case["forms"])]
        report["conversion_fixtures"] = {"ok":not mismatches,"total":len(cases),"passed":len(cases)-len(mismatches),
                                         "mismatches":mismatches}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_bytes(encoded(report))
    if converter is not None and not report["conversion_fixtures"]["ok"]:
        raise ValueError(f"Traditional conversion fixtures failed; see {args.report}")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("fetch", "prepare", "validate"))
    parser.add_argument("--source-dir", type=Path, default=ROOT / "temp/ime_sources")
    parser.add_argument("--profile", choices=PROFILES, default="zh-Hans")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--extension", choices=MODES, default="both")
    parser.add_argument("--abbrev-limit", type=int, default=64)
    parser.add_argument("--compare", action="store_true", help="measure all four extension combinations")
    parser.add_argument("--coverage", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    args.output_dir = args.output_dir or ROOT / "lmd/package/ime/chinese/data" / args.profile
    args.coverage = args.coverage or ROOT / "test/ime" / (
        "chinese_traditional_coverage.json" if args.profile == "zh-Hant" else "chinese_coverage.json")
    args.report = args.report or ROOT / "temp/ime_chinese" / (
        "zh-Hant-prepare-report.json" if args.profile == "zh-Hant" else "prepare-report.json")
    if args.abbrev_limit < 1:
        parser.error("abbrev-limit must be positive")
    lock = json.loads(LOCK_PATH.read_bytes())
    source_names = input_source_names(lock, args.profile)
    try:
        if args.command == "fetch":
            fetch_sources(args.source_dir, lock, source_names)
            result = {"ok": True, "sources": len(lock["sources"]) if source_names is None else len(source_names)}
        elif args.command == "prepare":
            report = prepare(args, lock)
            result = {"ok": True, "report": str(args.report), "output_dir": str(args.output_dir),
                      "files": report["comparisons"][args.extension]["files"]}
        else:
            sources = read_sources(args.source_dir, lock, source_names)
            chars, _, _ = read_unihan(sources["unihan"]["Unihan.zip"])
            syllables, han = load_syllables(), HanScript(sources["unihan"]["Scripts.txt"])
            entries, conversion = None, None
            if args.profile == "zh-Hant":
                mode = json.loads((args.output_dir / "manifest.json").read_bytes())["generator"]["options"]["extension"]
                data = records(sources)
                scales = frequency_scales(data, syllables, chars, han)
                entries, _, _, chars, conversion = assemble_profile(data, mode, syllables, chars, han, scales,
                    TraditionalConverter(sources["opencc"]))
            result = validate_pack(args.output_dir, syllables, chars, han, lock, entries, conversion)
            if json.loads((args.output_dir / "manifest.json").read_bytes())["profile"] != args.profile:
                result["errors"].append("manifest: profile differs from requested profile")
                result["ok"] = False
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        parser.exit(1, f"ime preparation: {error}\n")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
