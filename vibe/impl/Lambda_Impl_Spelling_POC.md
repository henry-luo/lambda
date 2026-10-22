# Spelling Vocabulary Storage POC

> **Date:** 2026-10-22.
> **Scope:** vocabulary storage experiments for `lambda.doc.spelling`, from the
> positional JSON lookup POC to the adopted semicolon/offset text resource (§7).
> **Status:** text dictionary format adopted 2026-10-22 (USER); a Lambda text
> lookup POC is implemented (§8). In-process standalone XZ loading is pending.
> **Design:** [Lambda_Pkg_Spelling.md](../Lambda_Pkg_Spelling.md), §4.3.
> **Spec linkage:** D7.2.4v3/D7.2.6 for source-module paths; S12.1.1v3 for
> procedural loading; S2.5.8/S17.4.1 for text; S6.2.2v3 for ordering. The general
> binary-Mark design remains deferred under K29c.

## 1. Implemented scope

The following list describes the original JSON milestone. The adopted text
reader and paragraph benchmark are recorded separately in §8.

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

The original JSON milestone contemplated migration to Lambda/Mark binary
packing; that resource requirement is superseded by the adopted text format in
§7 and [design §4.3](../Lambda_Pkg_Spelling.md#43-adopted-dictionary-resource-format).
The general binary-Mark encoding remains deferred under
[K29c](../Lambda_Design_Concurrency.md), and no binary-Mark size is claimed.
The original package POC loader still consumes uncompressed JSON. The storage
probes in §§5–7 measured alternatives; the subsequent Lambda text reader is in
§8. No standalone XZ reader is implemented by this POC.

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

**Smallest measured download at this comparison stage:** front coding. Store
each sorted spelling as
the number of shared prefix bytes plus the new suffix bytes. A 64-word restart
index is included in the 160.5 KiB figure; it allows a reader to binary-search
blocks and reconstruct at most 64 words within the selected block. The 146.8 KiB
sequential control has no useful intermediate restart points. Whole-file Brotli
or XZ compression must be decoded before arbitrary block access; the figures
do not include an independently compressed-block distribution experiment.

**Suggestion-engine research direction:** evaluate a compact minimal
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

At this comparison stage, a future Mark-binary resource carrying compact arrays
or binary payloads was considered. Generic serialization of the original
duplicated word arrays would not provide prefix/suffix sharing. The vocabulary
storage choice is now adopted in §7; suggestion-engine choices remain proposed.
These earlier comparisons did not replace the implemented JSON reader. JSON
rows remain available for inspection and interchange.

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

## 6. Comma/pipe text format, 64 words per line

The user's next storage proposal uses a dedicated text format, outside Mark
notation. This experiment interprets it as follows:

```text
cat,s|dog,s
```

This expands to `cat`, `cats`, `dog`, `dogs`. The first token on a line and each
token after `|` is a complete dictionary word and becomes the current anchor.
Each token after `,` is a suffix appended to that anchor; it does not change the
anchor. For example, `cat,ch,s` means `cat`, `catch`, `cats`. Partial reuse of an
anchor, such as replacing its final letter, needs a new complete word after `|`.
There is no implied inflection rule or generation of words absent from the source.

Spellings remain sorted and case-preserving UTF-8. Each line contains **64
expanded words**, except the final shorter line; its first word always resets
the anchor. Output uses LF, including a final LF, with no optional spaces.
Comma and pipe are reserved delimiters: none of these four pinned wordlists
contains them, so this experiment needs no escape syntax. Case-policy exceptions,
frequency data, restrictions, headers, notices and indexes are outside the text
payload measured here.

| Profile | Words | Text bytes | Gzip 9 bytes | XZ 6 bytes | Zstd 19 bytes | Brotli 11 bytes |
|---|---:|---:|---:|---:|---:|---:|
| US, level 60 | 109,902 | 632,498 | 220,400 | 181,736 | 187,004 | 185,434 |
| British `-ise`, level 60 | 109,550 | 630,981 | 220,001 | 181,104 | 186,273 | 184,691 |
| US, level 70 | 168,211 | 1,000,693 | 347,498 | 285,444 | 292,908 | 290,503 |
| British, level 70 | 171,326 | 1,026,263 | 354,183 | 290,236 | 298,197 | 295,508 |

For the standard US dictionary this is **617.7 KiB raw**, **215.2 KiB gzip**,
**177.5 KiB XZ**, or **181.1 KiB Brotli**. Raw size is 38.8% below the original
1,033,485-byte wordlist. It has 1,718 lines: 31,012 full-word anchors and 78,890
comma suffixes; the last line has 14 words.

The earlier binary front-coded 64-word format can share an arbitrary prefix of
the previous word by recording its length. It is smaller: 494,314 bytes raw or
164,488 bytes with XZ, compared with 632,498 and 181,736 for this text format.
The latter remains directly readable and needs only delimiter-based expansion.
No parse/lookup latency comparison was performed.

An optional 32-bit line-offset table adds **6,872 bytes (6.7 KiB)** for the US
dictionary. A loader can instead build that table by scanning newlines, keeping
the distributed file at the measured size. The first complete word on each line
provides a block search key without another duplicated word array. As with §5,
the measured whole-file compression must be decoded before arbitrary line
access; independently compressed lines were not measured.

Reproduce with `python3 temp/spelling_vocab_poc/delimited_compare.py`. It reuses
the existing codec-measurement helper and verifies the pinned input hashes.
Artifacts under `temp/spelling_vocab_poc/compact/` are:

- `<profile>.prefix-lines-64.txt`: the complete custom text dictionary.
- `<profile>.prefix-lines-64.offsets.u32`: optional little-endian line offsets.
- `<profile>.prefix-lines-64.{raw,gzip9,xz6,zstd19,brotli11}`: measured payloads.
- `delimited_measurements.json`: counts, byte sizes, hashes and validation results.

All four profiles decode to exactly their original word sequences. Every line
is also decoded independently and checked against its corresponding source
slice: **8,736 line checks** across **558,989 spellings**. All **20 compressed
or raw payload variants** recover the exact text bytes and match recorded hashes.
The Lambda package loader still consumes the positional JSON format; this is
a storage measurement, with no new Lambda reader or engine changes.

## 7. Semicolon text format with prefix offsets

**Adopted 2026-10-22 (USER)** as Lambda's spelling vocabulary resource format.
The current format definition is
[design §4.3](../Lambda_Pkg_Spelling.md#43-adopted-dictionary-resource-format),
including canonical preparation, reserved characters, manifests, entry IDs and
reader validation. This section records the storage proof and measured sizes;
the subsequent Lambda text reader is in §8. Standalone XZ loading remains pending.

The adopted format adds explicit prefix lengths:

```text
catalog;ue;5,yst|dog;s
```

For this measurement, `;suffix` appends to the **previous decoded word**;
`;N,suffix` keeps its first `N` Unicode code points and appends the suffix;
`|word` replaces it with a complete word. The example decodes to `catalog`,
`catalogue`, `catalyst`, `dog`, `dogs`. Offsets count code points rather than
UTF-8 bytes, consistent with the text-position model in **S2.5.8/S17.4.1**.
Every line starts with a complete word and contains 64 decoded words except the
last line. Semicolon, comma and pipe are reserved; none occurs inside the source
spellings. No spaces, quotes or Mark wrapper are added.

The adopted encoder shares the longest common prefix with the previous word.
It omits the offset when that prefix is the complete previous word, and uses
`|word` when there is no common prefix. The unchanged 109,902-entry US level-60
dictionary gives:

| Storage | Bytes | KiB |
|---|---:|---:|
| UTF-8 text | 525,993 | 513.7 |
| Gzip 9 | 167,205 | 163.3 |
| XZ / LZMA2 preset 6 (adopted distribution compression) | **145,056** | **141.7** |
| Zstd 19 | 147,536 | 144.1 |
| Brotli 11 | 144,685 | 141.3 |

With XZ 6 this is **20.2% smaller** than the previous comma/pipe text format's
181,736 bytes. It is also below the earlier binary front-code file's 164,488
bytes with XZ. These measurements show that a binary encoding is not by itself
a guarantee of the smallest compressed file. The binary file includes its
restart-offset table; both text formats omit a stored table and can construct
one by scanning newlines. An optional US text line-offset table remains 6,872
bytes before compression. Metadata, headers, notices and suggestions remain
outside these vocabulary measurements.

The text contains 1,769 complete words, including the 1,718 line starts;
36,295 appended suffixes; and 71,838 suffixes with explicit prefix lengths.
A control encoder chooses a full word instead whenever that individual token
is shorter. Its raw file falls slightly to 524,937 bytes, but its XZ size grows
to 145,780 bytes. Thus shortening individual tokens did not improve whole-file
compression in this comparison.

`python3 temp/spelling_vocab_poc/offset_text_compare.py` reproduces both variants,
reusing the existing prefix-length and codec helpers. Artifacts under
`temp/spelling_vocab_poc/compact/` are:

- `en-US.offset-lines-64.max-prefix.txt`: the primary custom text dictionary.
- `en-US.offset-lines-64.shortest-token.txt`: the shorter-token control.
- Matching `.offsets.u32` files and `.raw`, `.gzip9`, `.xz6`, `.zstd19`,
  `.brotli11` representations.
- `offset_text_measurements.json`: exact sizes, hashes, token counts and checks.

Both encodings decode to the exact original 109,902-word sequence, including
case and accents. Each of the 1,718 lines also decodes independently to its
corresponding source slice. All ten raw/compressed variants recover the exact
text bytes and match recorded hashes. The example and a non-ASCII prefix fixture
also decode correctly. No Lambda package reader or engine code changed.

## 8. Lambda front-coded lookup and paragraph timing

**Initial implementation, 2026-10-22:** `lambda.doc.spelling.front_coded` reads the adopted
uncompressed text resource and retains its binary bytes plus a compact `u32[]`
line-offset index. It binary-searches restart keys by comparing bytes in place,
then decodes only the selected line until a match or a greater word is found.
Lookup returns the zero-based entry ID, or null for an absent word. Query copies
are normalized to NFC; case remains exact. Prefix lengths follow
**S2.5.8/S17.4.1** and byte comparisons follow **S6.2.2v3**.
The lookup path described here is superseded by direct byte comparison in §9;
these timings and regression records describe the initial implementation.

Implemented entry points in `lmd/package/doc/spelling/front_coded.ls`:

| Entry point | Contract |
|---|---|
| `from_bytes(source, expected_count?)` | Validate/index the resource, retaining bytes and offsets, with no full decoded-word array |
| `read(path, expected_count?)` | Procedurally read uncompressed bytes and construct the dictionary (**S12.1.1v3**) |
| `lookup(dictionary, word)` | Exact NFC lookup, returning an entry ID or null; at most one line is decoded |
| `verify(dictionary, source_words)` | Qualification-only streaming comparison against an independent complete word sequence |

`utils/spelling_front_poc.py` checks the pinned US source hash, prepares the
canonical text and XZ files, and writes a separate source/negative-word oracle
for qualification. The generated text is byte-for-byte identical to §7's
525,993-byte measurement. No dictionary-specific C/C++ code was added.

The existing `doc.diff.common::lower_bound` helper now accepts an optional key
projection, so the reader can search restart keys without duplicating the search
algorithm or allocating a parallel key array. UTF-8 scalar decoding/validation
was extracted from `io.terminal.protocol` into shared `lambda.io.utf8` helpers.
The token decoder constructs GC-managed strings rather than creating an entire
document-parser `Input` for each token. ASCII tokens have a direct character-join
path. Validation and lookup share the word decoder.

### 8.1 Paragraph and method

`test/input/spelling/paragraph_100.txt` is a fixed, independently counted
**100-word paragraph**. It contains **97 known words** and three intentional
unknown spellings: `teh`, `recieve`, `speling`. The paragraph helper removes its
ASCII punctuation and lowercases the text before exact lookup. This is a
fixture-specific token policy, not a qualified Unicode prose tokenizer or a
full spelling-case policy.

`test/lambda/spelling/_front_bench.ls` uses Lambda's monotonic `clock()` and
measures file reading, construction/full validation, and paragraph checks
separately. After the first check, it performs five warmup paragraphs and nine
timed batches of three paragraphs each. Every token is looked up on every pass:
there is no word-result or decoded-line cache. The result list and a checksum
are checked against the golden. Timed checks include tokenization and lookups;
they exclude dictionary loading, manifest/paragraph reads and standalone XZ
decompression. The oracle is never loaded by the benchmark.

The qualified final-source timing is one process with nine batch means, using
MIR Direct with `LAMBDA_EXEC_BACKEND=jit`, on macOS 26.5.2 / arm64, eight logical
CPUs. The executable was built with `make build-release-compile JOBS=4`, checked
for the absence of the debug validator marker, and copied privately before any
regression build could replace `lambda.exe`:

`temp/spelling_vocab_poc/lambda-front-release.exe`, SHA-256
`02a4444635d86948c8bb4895eafe88d69ebec260e45f08530a33ed54b6decc6a`.

### 8.2 Observed performance and memory limits

| Phase | Measured wall time |
|---|---:|
| Read the uncompressed dictionary | 1.213 ms |
| Build index and fully validate all decoded words | 24,223.890 ms |
| Total dictionary load/validation | **24.225 s** |
| First 100-word check after loading | **1.023 s** |
| Median warmed 100-word check, nine batch means | **1.152 s** |
| Range of warmed batch means per paragraph | 1.054–1.345 s |

This POC is correct on the checks below but remains slow. The host was busy:
the one-minute load average was approximately 9.39 before and 9.14 after the
measurement on eight logical CPUs. These are observed wall times on that host,
not latency guarantees or an A/B comparison with an expanded-word array. The
machine-readable record includes individual samples, process CPU time, host
load, executable hash and source/fixture hashes.

Logical retained resource payload is **532,865 bytes (520.4 KiB)**: 525,993 text
bytes plus 6,872 bytes of used line offsets. This excludes object headers,
capacity slack, allocator state, runtime/JIT memory and temporary allocations.
**The timed process peaked at 912.0 MiB RSS.** Thus the compact representation
does not yet demonstrate an overall resident-memory improvement: allocation
behavior during validation/checking still needs profiling and reduction. Do
not report the logical dictionary payload as the process memory footprint.

### 8.3 Correctness and remaining scope

- The focused golden gate passes **8/8 fixtures in each of interpreter and JIT**,
  including the original JSON lookup, diff search, terminal protocol/rendering,
  the new reader's 40 checks and the UTF-8 decoder's 22 checks. The reader fixture
  also exercises all 130 entries across three blocks, code-point prefix lengths,
  NFC queries, exact case, missing words and malformed packs.
- The complete **109,902-word decoded sequence** matches the independently
  prepared pinned-source oracle in Lambda. This streams words during
  qualification; the checker does not retain the oracle or decoded-word array.
- **3,436 positive lookups** cover every block's first and last word, including
  the final partial block. **405 synthetic negative lookups** also pass. This is
  a block-boundary lookup sweep, not an exhaustive lookup invocation per word.
- The timed paragraph returns exactly the three expected misspellings and
  passes its checksum; the recorded executable/source/resource hashes match.
- Aggregate `make test-lambda-baseline JOBS=4 TEST_JOBS=3` exited 2: input
  parsers passed **2,112/2,112**; the runtime runner reported **3,172/5,005**.
  Four subprocess suites lost access to the shared `lambda.exe` and reported
  `ENOENT`/spawn failures: the main golden suite, structured tests, Test262
  preflight and math corpus. The executable later reappeared as a release build,
  consistent with a concurrent shared-build replacement. Original failure logs
  are preserved under `initial-*.log`; this is not recorded as a clean baseline.
  Private host/test-executable snapshots and separate scratch space recovered
  the affected suites: main goldens **1,689/1,691** initially, structured tests
  **141/141**, Test262 preflight **1/1**, and math corpus **921/921**. The main
  suite's two residual cases then passed: `input_markdown_emoji` needed the
  isolated workspace's missing `lib/` fixture link; `latex_test_latex_phase3_corpus`
  exceeded the 60-second limit under load but passed alone in 44.170 seconds
  under the unchanged limit. No test expectations or timeouts were relaxed.
  All **8,037 cases** were eventually covered successfully across the original
  run and these recoveries (**2,112 input + 5,925 runtime**). This combines
  debug native suites and private release-host subprocess reruns, and remains
  distinct from a clean single invocation of the aggregate command.

The POC does not load standalone `.xz` in-process; the build-time utility writes
the corresponding `.dict` for Lambda to read. XZ/manifest-policy integration,
runtime artifact-hash verification, complete prose tokenization, case admission,
suggestions, allocation tuning and latency qualification remain separate work.

### 8.4 Reproduction and artifacts

From the repository root, with the pinned source files from §3 available:

```bash
python3 utils/spelling_front_poc.py
make build-release-compile JOBS=4
cp lambda.exe temp/spelling_vocab_poc/lambda-front-release.exe
LAMBDA_EXEC_BACKEND=jit temp/spelling_vocab_poc/lambda-front-release.exe --no-log run test/lambda/proc/spelling_front_coded.ls
LAMBDA_EXEC_BACKEND=jit temp/spelling_vocab_poc/lambda-front-release.exe --no-log run test/lambda/spelling/_front_full.ls
LAMBDA_EXEC_BACKEND=jit temp/spelling_vocab_poc/lambda-front-release.exe --no-log run test/lambda/spelling/_front_bench.ls
```

Generated resources and records under `temp/spelling_vocab_poc/front/`:

- `manifest.json`, `en-US.dict`, `en-US.dict.xz`: resource identity and bytes.
- `oracle.json`: qualification input, excluded from the benchmark/checker state.
- `benchmark.json`: Lambda timing output from the most recent paragraph run.
- `benchmark-results.json`: qualified final-source samples and process/host provenance.
- `full-validation.json` and `.log`: full-decoder and boundary/negative results.
- `focused-interp.log`, `focused-jit-final.log`: focused golden gates.
- `lambda-baseline.log`: aggregate regression output.
- `initial-baseline-failures.json`, `initial-*.log`: preserved spawn-failure evidence.
- `regression-workspace.json`: isolated host/test-executable paths and hashes.
- `regression-retries.json`, `retry-*.json`, `retry-*.log`: affected-suite reruns.
- `prelim-correct.json` and `.log`: the bounded `--prelim` runner-contract gate.
- `baseline-result.json`: original failure, isolated recoveries and combined coverage.

## 9. Direct byte comparison during lookup

**Implemented 2026-10-22 (USER):** lookup compares the target directly with the
front-coded UTF-8 stream. It creates neither intermediate dictionary-word
strings nor a selected-line copy. The dictionary resource, entry IDs, restart
search, exact case behavior and NFC query normalization are unchanged. This is
Lambda Script code; the native release executable is unchanged.

### 9.1 Comparison state

After selecting a line, `compare_word` advances an absolute dictionary byte
position and a target byte position together while their bytes match. It stops
with an entry ID if both words end together, or with null if the dictionary word
is greater. When a word is smaller, the matched target-byte count is sufficient
to interpret following entries without reconstructing that word:

| Following token | Action |
|---|---|
| `;suffix`, preceding word ended as a complete target prefix | Compare the suffix starting at the matched target position |
| `;suffix`, preceding word differed from the target | Skip the suffix; the inherited mismatch is still smaller |
| `;N,suffix` | Find target byte boundary `p` after `N` code points; compare the suffix there if `p` is within the matched prefix, otherwise skip it |
| `|word` | Compare the complete literal starting at target position zero |

The matched-byte count may end *inside* a UTF-8 scalar: for example, `è` and `é`
share their first encoded byte. Explicit prefix counts still refer to complete
code points (**S2.5.8/S17.4.1**). Walking only the target to the requested
code-point boundary ensures that retaining a differing scalar cannot be mistaken
for retaining a matching prefix. Byte comparisons follow the dictionary's
UTF-8 order (**S6.2.2v3**). An offset beyond the target's length is treated as a
retained mismatch. Numeric suffixes with no comma remain ordinary text, even
when their digit sequences are too large to represent as an integer.

Load-time validation and source-oracle verification still reconstruct words
using the original decoder. This change removes word construction from lookup;
it does not optimize full validation or add token/line caches.

### 9.2 Qualification and timing method

`test/lambda/proc/spelling_front_coded.ls` now has 48 golden checks, including
prefix retreat after skipped appends, mismatches within two- and four-byte
scalars, offsets beyond a short target, multi-digit offsets, numeric suffixes,
and an 80-digit appended suffix. `test/lambda/spelling/_front_full.ls` now checks
lookup of **every source word**, replacing its earlier block-boundary sweep,
and checks every generated negative word.

The final-source focused gate passes **8/8 fixtures in each of interpreter and
JIT**. The complete source sequence verifies, all **109,902 positive lookups**
return their exact entry IDs, and all **6,465 synthetic negative lookups** return
null under JIT. The independent UTF-8 fixtures and malformed-resource checks
remain in the focused gate. No C/C++ runtime source changed, so this update uses
the focused package gates rather than repeating the engine aggregate baseline.

`utils/spelling_lookup_bench.py` compares the committed reader at `9a2f257b4`
with the current reader. It snapshots separate package trees, the identical
100-word paragraph harness/fixture, and the same raw dictionary and manifest.
The only differing package source is `doc/spelling/front_coded.ls`. Both lanes
use the same privately copied, release-qualified executable and forced JIT.
Each process uses the unchanged five warmups, nine samples and three paragraphs
per sample from §8.1, with no result or decoded-line cache.

Three process pairs run in **A/B, B/A, A/B order**. The reported comparison uses
the median of the three process medians for each lane; individual samples,
pair ratios, host loads, peak RSS, output checks and source/input/executable
hashes are retained. A failed or timed-out measurement aborts without retry.
Dictionary loading/full validation is measured separately and excluded from
paragraph samples. This is a fresh source A/B comparison on one executable,
distinct from comparing against §8's historical timing.

### 9.3 Observed results

All six measured processes passed the paragraph golden, found exactly `teh`,
`recieve`, `speling`, and produced checksum 96. No process failed, timed out or
needed a retry. Snapshot source/input hashes and the executable hash verified
after the last run. The executable is §8's pinned release host, SHA-256
`02a4444635d86948c8bb4895eafe88d69ebec260e45f08530a33ed54b6decc6a`;
the candidate module is SHA-256
`6bda6eb10e31a65fdf21568cf2cb3606435ec95aad99d51b498ac40ff59ae79d`.

| Process pair | Original decoder, median ms/paragraph | Direct comparison, median ms/paragraph | Speedup |
|---|---:|---:|---:|
| 0, A/B | 3,839.543 | 9.733 | 394.50× |
| 1, B/A | 1,392.569 | 9.051 | 153.85× |
| 2, A/B | 3,080.983 | 15.043 | 204.81× |
| Median of process medians | **3,080.983** | **9.733** | **316.56×** |

The ratio of the lane medians is **316.56×**, a **99.684%** reduction in observed
warm paragraph time. Individual pair ratios range from **153.85× to 394.50×**.
The host was heavily loaded; recorded one-minute load averages ranged from
approximately 21.5 to 36.3 on eight logical CPUs. Individual batch means ranged
from 1,285.064–8,941.268 ms for the original reader and 7.283–69.403 ms for direct
comparison. The absolute timings remain host-dependent; the benefit is large
in every pair. These are wall-time measurements, not a claim about isolated CPU
cycles or a comparison with a native spelling engine.

| Other process measurements, median across three runs | Original decoder | Direct comparison |
|---|---:|---:|
| First paragraph after loading | 1,506.717 ms | 8.829 ms |
| Dictionary read/index/full validation | 28.479 s | 31.611 s |
| Whole-process peak RSS | 894.2 MiB | 700.3 MiB |

Cold checks remain dominated by loading/full validation, whose implementation
was unchanged and was not tuned here. The different observed load medians are
not evidence of a lookup-induced loading improvement. Peak RSS includes that
phase, JIT/runtime state and temporary allocations; it is not dictionary payload
size or steady-state lookup memory. Both representations retain the same
532,865-byte logical dictionary/index payload from §8.2.

### 9.4 Reproduction and artifacts

With the pinned resources prepared and a release executable available (§8.4):

```bash
LAMBDA_EXEC_BACKEND=jit temp/spelling_vocab_poc/lambda-front-release.exe --no-log run test/lambda/spelling/_front_full.ls
python3 utils/spelling_lookup_bench.py --executable temp/spelling_vocab_poc/lambda-front-release.exe --control-ref 9a2f257b4 --pairs 3
```

The A/B utility creates its own directory below `temp/`, snapshots the executable
and package sources, and records the hashes for that run. This qualification's
artifacts are under `temp/spelling-pointer-ab-33i7sqmh/`:

- `results.json`: all six process measurements, samples, output checks and summary.
- `provenance.json`: executable, benchmark utility, input and package-source hashes.
- `pair-<n>-<variant>.log`: stable paragraph golden output for each process.
- `control/`, `candidate/`: private package/fixture/resource snapshots and each
  lane's most recent raw Lambda timing output.
- `lambda-release.exe`: the identical host used for both lanes.

Focused and exhaustive qualification records are under `temp/spelling_pointer/`:
`focused-{interp,jit}.json` and `.log`, `focused-workspace.json`, and
`full-final-jit.json` and `.log`. The full result pins the same candidate source
hash as the timing comparison. The implementation remains a raw UTF-8-resource
POC; XZ loading, complete prose tokenization, case admission, suggestions and
load/allocation tuning remain pending.
