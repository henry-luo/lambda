#!/usr/bin/env python3
"""Prepare the adopted front-coded English dictionary and independent lookup oracle.

Requires the pinned en_US.txt acquired for spelling_vocab_poc.py. This is
build-time tooling only; dictionary indexing and lookup run in Lambda Script.
"""

import argparse
import json
import lzma
import unicodedata
from pathlib import Path

from spelling_vocab_poc import SOURCE_FILES, SOURCE_REVISION, digest, encoded


def pack_words(words):
    lines = []
    for start in range(0, len(words), 64):
        previous = words[start]
        tokens = [previous]
        for word in words[start + 1:start + 64]:
            common = 0
            while common < min(len(previous), len(word)) and previous[common] == word[common]:
                common += 1
            suffix = word[common:]
            if common == len(previous):
                tokens.append(";" + suffix)
            elif common:
                tokens.append(";" + str(common) + "," + suffix)
            else:
                tokens.append("|" + word)
            previous = word
        lines.append("".join(tokens))
    return ("\n".join(lines) + "\n").encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("temp/spelling_vocab_poc/source/en_US.txt"))
    parser.add_argument("--output-dir", type=Path, default=Path("temp/spelling_vocab_poc/front"))
    args = parser.parse_args()
    source = args.source.read_bytes()
    if digest(source) != SOURCE_FILES["en_US.txt"][2]:
        parser.error("pinned English source checksum mismatch")
    spellings = source.decode("utf-8").splitlines()
    words = sorted(unicodedata.normalize("NFC", word) for word in spellings)
    if len(set(words)) != len(words) or any(not word or any(c in word for c in ";,|\r\n\x00") for word in words):
        parser.error("colliding, empty, or unencodable word")
    known = set(words)
    negatives = sorted({word + "zz" for word in words[::17]} - known)
    payload = pack_words(words)
    compressed = lzma.compress(payload, preset=6)
    assert lzma.decompress(compressed) == payload
    args.output_dir.mkdir(parents=True, exist_ok=True)
    dictionary = args.output_dir / "en-US.dict"
    archive = args.output_dir / "en-US.dict.xz"
    oracle = args.output_dir / "oracle.json"
    dictionary.write_bytes(payload)
    archive.write_bytes(compressed)
    oracle.write_bytes(encoded({"words": words, "negatives": negatives}))
    manifest = {
        "format_version": 1, "encoding": "semicolon-prefix-text", "block_words": 64,
        "language": "en-US", "source_revision": SOURCE_REVISION,
        "source_sha256": digest(source), "generator_sha256": digest(Path(__file__).read_bytes()),
        "entries": len(words), "dictionary": str(dictionary), "bytes": len(payload),
        "sha256": digest(payload), "xz_file": str(archive), "xz_bytes": len(compressed),
        "xz_sha256": digest(compressed), "compression": "XZ/LZMA2 preset 6",
        "lines": (len(words) + 63) // 64, "offset_payload_bytes": ((len(words) + 63) // 64) * 4,
        "oracle": str(oracle), "oracle_sha256": digest(oracle.read_bytes()),
        "negative_checks": len(negatives),
        "notices": "retain source/ESDB notices from the earlier vocabulary builder; sizes exclude notices",
    }
    (args.output_dir / "manifest.json").write_bytes(encoded(manifest))
    print(json.dumps({"entries": len(words), "bytes": len(payload), "xz_bytes": len(compressed),
                      "manifest": str(args.output_dir / "manifest.json")}, indent=2))


if __name__ == "__main__":
    main()
