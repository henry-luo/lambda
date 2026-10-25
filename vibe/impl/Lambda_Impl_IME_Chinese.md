# Chinese IME implementation

> **Status (2026-10-25):** M0 preparation tooling and data gate qualified;
> M0 runtime measurements and held-out coverage remain open. M1–M3 have no
> implementation yet. The first release is not complete.
> **Design:** [Lambda_Pkg_IME_Chinese.md](../Lambda_Pkg_IME_Chinese.md).
> **Spec linkage:** D7.2.4v4 places the package under
> `lmd/package/ime/chinese/`; D7.1.2v2 governs resource IO when the runtime
> loader is added. The engine/IO split will follow S12.1.1v3 and explicit
> dictionary loading will follow D7.2.1/D7.2.3. Sorting uses S6.2.2v3's byte
> order, and word lengths use S2.5.8's code points. No formal rulings changed.

## 1. Implemented scope

`utils/prepare_ime_chinese.py` implements offline acquisition, preparation and
explicit validation of the adopted version-1 pack. `fetch` is the only command
that accesses the network. It downloads only the URLs and SHA-256 identities
in `utils/ime_chinese_sources.json`; existing mismatched inputs fail rather
than being replaced. `prepare` and `validate` check every input against that
lock before using it. Complete upstream license texts and provenance flags
travel in the generated NOTICE and manifest.

The 417-syllable inventory lives in
`lmd/package/ime/chinese/syllables.txt`. Tones are removed and `ü` becomes `v`;
AOSP's umlaut-omitting `nue`/`lue` aliases normalize to `nve`/`lve`.
Compatibility ideographs normalize to NFC before admission. Han membership
comes from the pinned Unicode 18.0.0 `Scripts.txt`; the default character set
comes from Unihan's `kTGHZ2013`. An explicit source reading admits a word;
otherwise every character must have exactly one reading in the full union of
character readings. Invalid alternatives are retained for this ambiguity
check, so filtering an alternative cannot accidentally permit a guess.

The builder compares the AOSP base, phrase + jieba, HanLP, and both
extensions. Frequency scales are calibrated against the AOSP multi-character
overlap using the median frequency ratio, recorded in the manifest. AOSP
keeps its per-reading weights; unweighted readings stay at zero. Decimal
logarithms and explicit rounding make weight preparation independent of
platform `libm`. The original GBK flag is counted, with no GBK filter applied.
The admitted AOSP words all have frequencies of at least 60, so imposing its
old builder's cutoff would remove none of them.

Abbreviations derive from the completed lexicon. The top-64 limit applies
separately to orthographic initial sequences, preserving both `z` and `zh`
groups under the same first-letter key. Validation re-derives the expected
index to check complete top-K membership, as well as the same-reading,
same-weight and initials invariants. Both tables retain their uncompressed
UTF-8 text and XZ preset-6 versions. The manifest records byte/hash identities,
counts by syllable length, drops, source pins, options, generator identity and
NOTICE identity.

The source lock and code are reviewable repository files. Packs remain under
the existing gitignore entry `lmd/package/ime/chinese/data/` per IME-O7.

## 2. Evidence

All commands below ran from the repository root. No engine source changed,
and no engine build or aggregate regression run was needed for this tooling
milestone.

```sh
python3 utils/prepare_ime_chinese.py fetch
python3 utils/prepare_ime_chinese.py prepare --compare
python3 utils/prepare_ime_chinese.py validate
python3 test/ime/test_prepare_ime_chinese.py
python3 utils/prepare_ime_chinese.py prepare \
  --output-dir temp/ime_chinese/rebuild \
  --report temp/ime_chinese/rebuild-report.json
```

The default pack contains **8,105 characters, 10,947 character readings and
202,083 word readings**. All four source configurations cover the entire
character set. The checked-in 32-case fixture is authored smoke coverage; it
is not a held-out quality corpus.

| Sources | Word readings | Authored smoke coverage | Combined XZ table bytes |
|---|---:|---:|---:|
| AOSP | 48,065 | 31/32 | 745,512 |
| Phrase + jieba | 187,495 | 32/32 | 2,719,040 |
| HanLP | 100,376 | 31/32 | 1,420,836 |
| Both | 202,083 | 32/32 | 2,887,128 |

**Both extensions are selected:** HanLP adds 14,588 word readings over
phrase + jieba for 168,088 compressed bytes, about 7.8% more word readings for
6.2% more compressed table storage. This settles the engineering part of
IME-O4; provenance acceptance was already the user's ruling. It does not
establish candidate quality.

| Default table | Lines | Postings | UTF-8 bytes | XZ bytes | u32 offset payload bytes |
|---|---:|---:|---:|---:|---:|
| Lexicon | 153,659 | 213,030 | 5,077,740 | 1,525,804 | 614,636 |
| Abbreviation | 43,343 | 143,234 | 4,351,788 | 1,361,324 | 173,372 |

Bytes plus the proposed u32 offset arrays total **10,217,536 bytes**. This is
payload accounting, not measured retained memory; allocator/container overhead
and engine metadata are additional. There is no Lambda loader yet, so no load
time or lookup latency is reported.

The front-key control preserves postings and encodes each key as a shared
prefix length plus a suffix; decoding must reproduce every original byte.
The entry-ID control replaces abbreviation words/readings with lexicon IDs
and weights, checking every ID's word/reading/weight against the lexicon.
These are measured controls, not changes to the user-adopted pack format.

| Control | Uncompressed bytes | XZ bytes |
|---|---:|---:|
| Lexicon with front-coded keys | 4,086,988 | 1,446,088 |
| Abbreviations with front-coded keys | 4,134,442 | 1,322,212 |
| Abbreviations using entry IDs | 1,983,371 | 608,832 |

Unihan cross-checking flags **32** retained character readings not recorded
by the four checked reading properties. **15** characters have a leading
weighted reading different from `kHanyuPinlu`; measured frequency sources can
disagree. These discrepancies are reported for quality review; no attested
curated reading is silently changed or invented to resolve them.

Validation passed on the full generated pack. The **26/26** offline contract
tests passed, including independent expected table bytes, source checksums,
normalization, polyphone admission, per-initial top-K preservation, corrupt
encodings, missing LF, unsorted/duplicate keys, invalid words/weights, broken
abbreviation membership, XZ corruption and manifest/count mismatches.

An independent preparation into another directory produced **6/6
byte-identical files**, including both XZ tables, manifest and complete
notices. Generated evidence stays local:

- `temp/ime_chinese/prepare-report.json`: counts, drops, comparisons and controls.
- `temp/ime_chinese/validate.json`: full-pack validation result.
- `temp/ime_chinese/reproducibility.json`: per-file equality and SHA-256 values.
- `temp/ime_chinese/rebuild-report.json`: independent rebuild results.

## 3. Remaining gates and next implementation

M0 remains partial until a permissively licensed held-out fixture is chosen and
covered, and load time, retained memory and exact/prefix/abbreviation lookup
latency are measured using the Lambda loader on a release executable. The
authored smoke fixture and storage accounting do not replace those gates.

M1 adds the pure Lambda package, the lattice, bounded lookup, configurable
fuzzy pairs and ranking. First promote the spelling line-offset index builder
to a shared IO package and use it from both packages (IME6; no copied builder).
Implement normal loading as bytes plus offsets with only format/profile and
byte-size checks; keep the full validator explicit. The shared compressed
resource reader specified in §6.4 does not exist in this checkout and must be
added before compressed-only installation is qualified. Record the held-out
candidate ranking baseline without tuning against the smoke fixture.

M2 adds session reduction, partial selection, paging, punctuation, Shift-tap
mode switching, settings and the explicit user overlay. Package fixtures need
both interpreter and JIT runs. M3 adds queued composition transport, the native
candidate popup, settings and the DOM adapter, then the Radiant UI and
aggregate gates. The OS-composition stand-down behavior remains required.
