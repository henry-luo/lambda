# Spelling Vocabulary Storage POC

> **Date:** 2026-10-22.
> **Scope:** the positional vocabulary format requested for `lambda.doc.spelling`;
> dictionary preparation, actual file sizes, and Lambda load/lookup validation.
> **Design:** [Lambda_Pkg_Spelling.md](../Lambda_Pkg_Spelling.md), §4.3.
> **Spec linkage:** D7.2.4v3/D7.2.6 for source-module paths; S12.1.1v3 for
> procedural loading; S2.5.8/S17.4.1 for text; K29c for deferred binary Mark.

## 1. Implemented scope

- `utils/spelling_vocab_poc.py` verifies pinned source hashes, prepares four
  English profiles, serializes three comparable JSON shapes plus a plain-wordlist
  control, measures actual bytes, and verifies JSON round trips.
- `lmd/package/doc/spelling/vocabulary.ls` reads positional JSON, rejects malformed
  or misaligned entries, and performs exact NFC binary-search lookup. It reuses
  the existing `doc.diff.common::lower_bound` helper instead of duplicating it;
  extracting a neutral shared module can follow when the full package is built.
- `test/lambda/proc/spelling_vocab_array.ls` and its golden check the storage
  contract, including NFC lookup, boundaries, optional fields and invalid packs.
- `test/lambda/spelling/_vocab_file.ls` and its golden check all four generated
  dictionaries using the default measurement report. It is a manual helper,
  excluded from ordinary discovery because its full-size inputs are generated.

No new native implementation was added. This POC does not implement spelling
case admission, tokenization, restrictions, suggestions, or editor integration.
All measured rows use `[word, "ordinary"]` to match the storage experiment;
the original spellings and capitalization are retained. That label assignment
is not a claim of fidelity to upstream case rules.

The logical row schema is `[word, case_policy, commonness?, flags?]`. Two fields
are sufficient for this experiment. Optional commonness and restriction flags
are exercised in the small fixture, not included in the measured dictionaries.
Both keys and rows remain positional Lambda arrays, consistent with D7.2.4v3's
ordinary source-package mechanism; no runtime data representation change occurs.

## 2. Measured vocabulary sizes

Source: ESDB **2026.02.25**, generated-output repository revision
`71d7dd07676edb60ade43552e10b41314b7e9287`, tag `rel-2026.02.25`.
The publisher offers normal level-60 and large level-70 dictionaries; the large
British profile includes both `-ise` and `-ize` spellings.
[Publisher](https://wordlist.aspell.net/dicts/),
[pinned generated output](https://github.com/en-wl/wordlist-diff/tree/71d7dd07676edb60ade43552e10b41314b7e9287).

| Profile | Entries | Requested array JSON, bytes | MiB | Gzip, bytes | Equivalent object JSON, bytes |
|---|---:|---:|---:|---:|---:|
| US, level 60 | 109,902 | 3,935,365 | 3.75 | 616,484 | 5,473,993 |
| British `-ise`, level 60 | 109,550 | 3,923,825 | 3.74 | 614,852 | 5,457,525 |
| US, level 70 | 168,211 | 6,125,214 | 5.84 | 951,097 | 8,480,168 |
| British, level 70 | 171,326 | 6,260,797 | 5.97 | 967,756 | 8,659,361 |

JSON uses UTF-8, no optional whitespace, `ensure_ascii=false`, and one final LF.
The small header contains `format_version` and `language`; `keys` repeats the
word in each corresponding entry, exactly as requested. Gzip uses level 9 with
`mtime=0`. MiB means 1,048,576 bytes. No runtime latency benchmark was performed.

For standard US English:

| Representation | Bytes | Gzip bytes |
|---|---:|---:|
| Plain complete-form wordlist | 1,033,485 | 288,940 |
| Keys plus object entries `{word, case}` | 5,473,993 | 626,011 |
| **Keys plus array entries `[word, case]`** | **3,935,365** | **616,484** |
| Array entries only, omitting the duplicate keys array | 2,682,067 | 319,331 |

Replacing entry maps with arrays saves **1,538,628 bytes (28.11%)** before
compression in the requested US profile. Gzip already compresses repeated field
names well, so the compressed saving is only 9,527 bytes (1.52%).

The duplicate US keys array accounts for **1,253,298 bytes**. The rows-only
control is measured as a further design comparison; the implemented loader
retains the requested separate keys array. It would need a different lookup
projection to consume that control directly.

These totals exclude the suggestion deletion index, frequency/commonness data,
optional restrictions, and the separate source/license manifest. The copied
upstream notices and README add 4,863 and 1,705 bytes respectively. The measurement
manifest has its own generated size. JSON file size is not loaded-memory size.

The final distribution must migrate to **Lambda/Mark binary packing when
supported**, preserving the logical array schema and IDs. The current design
defers the encoding under [K29c](../Lambda_Design_Concurrency.md); no binary-Mark
size is claimed. The original package POC loader consumes uncompressed JSON.
The later storage experiments in §5 are independent measurement prototypes,
not shipping substitute formats or a binary-Mark implementation.

## 3. Reproduction and artifacts

From the repository root, acquire the six pinned public source files:

```bash
mkdir -p temp/spelling_vocab_poc/source
for name in en_US.txt en_GB-ise.txt en_US-large.txt en_GB-large.txt Copyright README.md; do
  curl --fail --location --silent --show-error \
    "https://raw.githubusercontent.com/en-wl/wordlist-diff/71d7dd07676edb60ade43552e10b41314b7e9287/$name" \
    -o "temp/spelling_vocab_poc/source/$name"
done
python3 utils/spelling_vocab_poc.py
```

The utility verifies every input against an embedded SHA-256 and rejects changes.
It requires Python 3.9 or newer, with no third-party Python dependencies. Its
report records the Python/Unicode versions, source revision, generator hash and
generated artifact hashes. Full upstream notices are retained beside the output.

Default artifacts:

- `temp/spelling_vocab_poc/out/en-US.rows.json`: requested format for US level 60.
- `temp/spelling_vocab_poc/out/*.objects.json`: equivalent object-entry controls.
- `temp/spelling_vocab_poc/out/*.rows-only.json`: duplicate-key-free comparisons.
- `temp/spelling_vocab_poc/out/measurements.json`: exact counts, byte sizes and hashes.
- `temp/spelling_vocab_poc/out/ESDB-Copyright`: original distribution notices.

Run the functional checks in both execution tiers:

```bash
LAMBDA_EXEC_BACKEND=interp ./lambda.exe --no-log run test/lambda/proc/spelling_vocab_array.ls
LAMBDA_EXEC_BACKEND=jit ./lambda.exe --no-log run test/lambda/proc/spelling_vocab_array.ls
LAMBDA_EXEC_BACKEND=interp ./lambda.exe --no-log run test/lambda/spelling/_vocab_file.ls
LAMBDA_EXEC_BACKEND=jit ./lambda.exe --no-log run test/lambda/spelling/_vocab_file.ls
```

The full-size helper reads `measurements.json` from the default output directory;
it does not accept trailing script arguments. The CLI currently treats a second
positional file as a second script.

## 4. Verification record

Validation uses a private copy of the available executable at
`temp/spelling_vocab_poc/lambda-poc.exe`, SHA-256
`1ea100e485af7736b93fa98146aeb29a775fe368e92d790cbbb86e838a757d68`.
The generated machine record is `temp/spelling_vocab_poc/validation.json`.
The size report records generator SHA-256
`a3f2f395833397707c99d957d273831f4dfc6cbe2c31850ea5336263a188e65d`.

Focused results:

- Four profiles / 16 uncompressed representations round-trip successfully.
  Every recorded file size and SHA-256 matches the generated bytes.
- All 16 gzip artifacts match their recorded sizes/hashes and decompress to
  exactly the corresponding uncompressed bytes.
- The small fixture passes **16/16 assertions per tier**. The four full-size
  dictionaries pass **20/20 assertions per tier**, including entry counts,
  known/missing words and first/last lookup boundaries.
- Total: **4/4 golden-matched invocations, 72/72 assertions**, across interpreter
  and JIT. Documents, JSON examples and the generator syntax also pass checks.

Aggregate result: **`make test-lambda-baseline` passed, 8,035/8,035 tests**:
2,112/2,112 input-parser tests and 5,923/5,923 Lambda/runtime tests. The main
golden suite passed 1,689/1,689, including `proc_spelling_vocab_array`.
The command exited 0; its log is
`temp/spelling_vocab_poc/lambda-baseline.log`. This is correctness qualification,
not release-performance evidence; the runner reported substantial host load.

## 5. Compact dictionary comparison, 2026-10-22

Follow-up: the user considers roughly 600 KiB compressed too large. Compare
data structures as well as codecs, preserving the **same 109,902 spellings**.
No smaller vocabulary or probabilistic acceptance filter is used.

| Representation | Compression | Actual bytes | KiB |
|---|---|---:|---:|
| Original keys plus two-column JSON entries | Gzip 9 | 616,484 | 602.0 |
| Same original JSON | XZ / LZMA2 preset 6 | 465,920 | 455.0 |
| Plain sorted wordlist | XZ / LZMA2 preset 6 | 229,476 | 224.1 |
| Front coding, one sequential block | Brotli 11 | 150,310 | 146.8 |
| Front coding, 64-word restart blocks and index | Brotli 11 | 164,349 | 160.5 |
| Minimal DAFSA, columnar arrays and varint state IDs | XZ / LZMA2 preset 6 | 186,668 | 182.3 |
| MARISA succinct trie, tiny cache / three levels | Brotli 11 | 200,290 | 195.6 |

These are vocabulary-only storage measurements; per-word exceptions, ranking
data and suggestion indexes are extra. Default `ordinary` metadata can be stored
once rather than repeated for every entry. Codec levels differ explicitly in the
table; these are measured storage alternatives, not a runtime speed comparison.

**Smallest measured download:** front coding. Store each sorted spelling as
the number of shared prefix bytes plus the new suffix bytes. A 64-word restart
index is included in the 160.5 KiB figure; it allows a reader to binary-search
blocks and reconstruct at most 64 words within the selected block. The 146.8 KiB
sequential control has no useful intermediate restart points. Whole-file Brotli
or XZ compression must be decoded before arbitrary block access; the figures
do not include an independently compressed-block distribution experiment.

**Recommended direction for a full checker:** evaluate a compact minimal
acyclic word graph (DAWG / DAFSA), or an FST when word IDs or weights are needed.
Prefixes and equivalent suffix continuations are shared. Near-word search can
traverse the same graph with edit-distance state, avoiding a separately expanded
SymSpell deletion index. That changes the proposed suggestion design and needs
Unicode-aware correctness, quality and latency qualification before adoption.
[FST set/map and fuzzy-search reference](https://github.com/BurntSushi/fst)

The custom graph experiment has 41,031 states and 88,588 arcs, reduced from a
257,234-node byte trie. Its columnar varint representation is 319,667 bytes before
XZ compression. A loaded implementation would need compact numeric indexes;
reconstructing one Lambda map per graph node would discard much of the memory
benefit. Membership and full enumeration are validated; word-to-entry-ID payloads,
spelling case admission and approximate search are not implemented by this probe.

MARISA is an established static compact trie with lookup, key IDs, reverse
lookup and prefix search. The reference probe used the Python `marisa-trie`
1.4.1 wheel installed only under `temp/spelling_vocab_poc/marisa_tool/`.
It is a comparison tool, not a new Lambda package dependency. Its smallest
tested raw file is 287,784 bytes. A native dependency or a compatible reader
would be needed to consume that specific format.
[MARISA implementation](https://github.com/s-yata/marisa-trie),
[key-ID and storage interface](https://marisa-trie.readthedocs.io/en/latest/tutorial.html)

The ultimate Mark-binary resource should carry the chosen compact structure as
typed arrays/binary data plus small metadata. A generic binary encoding of the
original duplicated word arrays alone does not provide dictionary-specific
prefix/suffix sharing. JSON rows remain useful as an inspectable source form.
The production storage/suggestion choice remains open; this comparison does
not replace the implemented JSON reader or ratify a private wire format.

All experimental code and results are under `temp/spelling_vocab_poc/`:

- `compact_compare.py`: prefix coding, simple minimal DAFSA, codec measurements.
- `automaton_columns.py`: alternative columnar graph representations.
- `marisa_compare.py`: reference trie comparison, three/five/ten levels.
- `compact/`: `measurements.json`, `column_measurements.json`,
  `marisa_measurements.json`, `json_measurements.json` and `validation.json`
  contain sizes, hashes and checks.

Validation enumerates and compares the complete accepted language of each
experimental automaton with the source wordlist. Every front-coded spelling
round-trips and every restart-table offset is checked. All three MARISA variants
enumerate the same complete word set and pass 109,902 ID/reverse-lookup checks
each. No native Lambda code or package implementation changed for these tests;
the aggregate result in §4 belongs to the earlier JSON-loader milestone.
