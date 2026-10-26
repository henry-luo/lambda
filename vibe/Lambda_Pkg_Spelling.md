# Lambda Spelling Package Proposal

> **Status:** dictionary resource format adopted, 2026-10-22 (USER), §4.3.
> Lambda front-coded text lookup is implemented as a POC. XZ resource loading,
> the full checker and the suggestion engine remain proposed.
> **Package:** `lambda.doc.spelling`.
> **Scope:** offline spelling checks and correction suggestions for prose,
> followed by document and editor integration and qualified language packs.
> **Spec linkage:** D7.2.1/D7.2.3 govern source packages and state;
> D7.2.4v3/D7.2.6 govern the namespace and facade; S2.5.8/S17.4.1 govern text
> positions; S6.2.2v3 governs sort order; S17.7.1/S17.7.2 govern case operations;
> S12.1.1v3 governs effects. The dictionary format in §4.3 is adopted package
> policy; other APIs and defaults remain proposed. No formal language ruling changes.

## 1. Recommendation

Implement the checker in Lambda Script, with an explicit entry module at
`lmd/package/doc/spelling.ls` and helpers and data under `doc/spelling/`.
This gives the requested `import spelling: lambda.doc.spelling` without a
resolver exception, consistent with **D7.2.4v3/D7.2.6**.

Start with **English, prepared dictionaries, and deterministic suggestions**:

- Use **English Speller Database (ESDB, formerly SCOWL)** as the default
  English vocabulary source. Ship separate `en-US` and `en-GB` profiles at
  coverage level 60; offer larger and additional regional profiles separately.
- Store vocabulary in the **adopted front-coded UTF-8 text format** (§4.3):
  `;` for suffix entries, `N,` for a retained prefix length, `|` for a full word,
  and 64 words per line. Use **XZ preset 6** for compressed distribution.
- Use a **SymSpell-style deletion index** to find likely corrections, followed
  by edit-distance verification and ranking. Implement the algorithm in Lambda;
  the upstream implementation is a reference, not a runtime dependency.
- Keep **accepted vocabulary separate from ranking data**. A corpus frequency
  list must never make an otherwise unknown spelling valid.
- Make **Hunspell dictionary compatibility a later, explicit milestone**.
  Reusing dictionary data does not require embedding Hunspell, but full support
  for its rules is a substantial feature in its own right.

ESDB provides customizable English spelling data and published plain-wordlist
artifacts. Its database export interface is still evolving, so a release must
pin an upstream artifact or source revision. [ESDB overview](https://wordlist.aspell.net/)

The first useful release should check text, suggest fixes, honor regional and
personal dictionaries, and report precise ranges. Interactive underlines and
many-language morphology can follow without delaying this core.

## 2. Features and release boundaries

| Capability | First release | Later milestone |
|---|---|---|
| Word lookup | Accepted, unknown, forbidden, and wrong-case results; distinguish ignored tokens | Language-specific morphological explanations |
| Text checking | Ordered issues, original spelling and source ranges, coverage statistics | Structured-document adapters |
| Suggestions | Insertion, deletion, substitution and adjacent-transposition errors; deterministic top results | Phonetic rules, word splits/joins, optional frequency ranking |
| English variants | Separate US and British dictionaries; explicit selection | Canadian, Australian, and selectable Oxford spelling |
| Personal vocabulary | Immutable user/project additions, forbidden words, preferred replacements, ignore lists | Persistence through the host application's existing settings |
| Token handling | Unicode letters and marks, contractions, possessives, hyphenated words; URL/email exclusions | Language-specific segmentation and compounds |
| Technical documents | Caller exclusions and optional identifier handling | Selected CSpell domain packs; parser-based code/math exclusions |
| Editor use | Stable, revision-associated issue data | Decorations, context menu, incremental rechecks, undoable replacements |
| Other languages | Explicit unsupported-language result | Individually qualified dictionary/rule packs |

### 2.1 Core behavior

1. Check a single word without scanning a document or generating suggestions.
2. Check a UTF-8 string and return every issue in source order, within explicit
   limits. Repeated misspellings retain every occurrence.
3. Generate suggestions separately, so batch checks can skip this cost and an
   editor can request suggestions only for the selected issue.
4. Respect dictionary spelling and case policy. Ordinary sentence capitalization
   must work without accepting arbitrary mixed-case forms or lowercasing names.
5. Recognize inflected forms present in the prepared vocabulary. Do not guess
   validity by stripping `s`, `ed`, or `ing` from an unknown word.
6. Exclude URLs, email addresses, and caller-supplied ranges. Numeric-only
   tokens are skipped; skipping acronyms or identifiers is configurable.
7. Work offline with packaged data. Checking never downloads a dictionary or
   sends document text to a service.

### 2.2 Scope limits

The first release does not detect grammar mistakes, contextual real-word errors
such as `form` for `from`, or semantic misuse. It does not provide definitions,
translation, a thesaurus, neural rewriting, automatic language detection, or
automatic correction while typing. Valid words remain valid even when rare.

Chinese, Japanese, Thai, and other languages requiring additional segmentation
are future language projects. A Unicode tokenizer alone does not make a checker
for these languages. The Unicode segmentation specification explicitly notes
the need for language-dependent mechanisms. [UAX #29](https://www.unicode.org/reports/tr29/)

## 3. Available dictionaries and related resources

“Dictionary” can mean an acceptance vocabulary, a morphology ruleset, a domain
overlay, or a frequency table. Choose these independently.

| Resource | What it supplies | Proposed use and tradeoff |
|---|---|---|
| **ESDB / historical SCOWL** | Curated English spellings, dialects, variants, and coverage levels | Best starting point for a controllable English package; export complete word forms. [Project](https://github.com/en-wl/wordlist) |
| **LibreOffice Hunspell dictionaries** | Many languages, generally as paired `.dic` vocabulary and `.aff` rules | Best general source family for a multilingual extension; qualify each language and its license separately. [Collection](https://github.com/LibreOffice/dictionaries), [format](https://raw.githubusercontent.com/hunspell/hunspell/master/man/hunspell.5) |
| **Marco A.G.Pinto British English dictionary** | A separately maintained British Hunspell dictionary | Strong candidate for broad British coverage; assess alongside ESDB British profiles. Its lineage and licensing differ from ESDB's. [Maintainer information](https://wordlist.aspell.net/dicts/), [dictionary README](https://github.com/LibreOffice/dictionaries/blob/master/en/README_en_GB.txt) |
| **wooorm/dictionaries** | Normalized distributions of existing Hunspell dictionaries | Convenient acquisition/packaging alternative; normalization does not replace upstream maintenance or the original data licenses. [Project](https://github.com/wooorm/dictionaries) |
| **CSpell dictionaries** | Natural-language and specialist vocabularies, including programming and technical terms | Best fit for optional domain overlays; avoid enabling all domain terms for ordinary prose. [Collection](https://github.com/streetsidesoftware/cspell-dicts) |
| **SymSpell frequency dictionaries** | Word/count tables accompanying a correction algorithm | Useful ranking experiment; not an authoritative vocabulary. The English data combines corpus counts with a spelling list. [Data description](https://github.com/wolfgarbe/SymSpell#frequency-dictionary) |
| **wordfreq** | Multilingual usage-frequency estimates | Optional ranking research. Code and data have different licensing, and the usage data is a snapshot through about 2021. [Project and data notes](https://github.com/rspeer/wordfreq) |

Hunspell is an **engine and dictionary format**, while ESDB is a **data source**;
they are complementary choices. Hunspell supports morphology, compounds and
language-specific suggestions, rather than merely consulting a word list.
[Hunspell features](https://hunspell.github.io/)

### 3.1 Which English dictionaries should Lambda use?

**Default: ESDB level 60, one regional profile at a time.** Upstream recommends
the normal coverage tier for general checking; the larger tier accepts more
uncommon words and can therefore miss plausible typos. The publisher's listed
speller release at research time is **2026.02.25**, with plain lists available.
Pin the chosen release and actual checksums during implementation; this proposal
does not assert that an artifact has already been imported.
[Published dictionaries and coverage guidance](https://wordlist.aspell.net/dicts/)

Use `en-US` as the fixed fallback when no language is supplied, independent of
the machine's locale. Let the caller or document select `en-GB`. Do not combine
US and British vocabularies by default: the union hides regional inconsistencies.

For the initial British pack, recommend **ESDB British `-ise`, level 60**, for
consistent export and distribution with the US pack. Label it accurately;
British `-ize` is also legitimate. Add an Oxford profile rather than presenting
all `-ize` spellings as inherently wrong.

For users prioritizing broad British coverage, evaluate **Marco A.G.Pinto's
dictionary as an optional profile**. ESDB's own publisher points to it as the
official British Hunspell dictionary and notes its broader coverage and stronger
exclusion of Americanisms. That makes it a quality candidate, not evidence that
it wins every precision/recall comparison. Measure both on British prose and
typo fixtures before choosing a broader default.
[British dictionary comparison](https://wordlist.aspell.net/dicts/)

Do not add a giant general word dump merely to suppress unknown-word reports.
Use personal and domain vocabularies for names and specialist terminology.
Use frequencies to rank already valid candidates, never to validate raw tokens.

### 3.2 Distribution and provenance

Each pack needs a manifest containing its language, profile, source URL,
upstream version/commit, downloaded and generated SHA-256 hashes, generator
version/options, entry counts, case policy, and original license notices.
Prepared indexes inherit the relevant data obligations; changing format does
not change the source license.

ESDB permits distribution with its notices; some exported subsets require
additional notices, including Australian-derived data. Preserve the notices
applicable to the actual export. [ESDB Copyright](https://github.com/en-wl/wordlist/blob/v2/Copyright)

Do not label every LibreOffice dictionary with one repository-wide license.
For example, the British dictionary README describes LGPL-licensed word and
affix data. Likewise, wooorm's packaging license does not replace each original
dictionary license. [British README](https://github.com/LibreOffice/dictionaries/blob/master/en/README_en_GB.txt),
[wooorm license distinction](https://github.com/wooorm/dictionaries#install)

Do not bundle wordfreq by default: its data licensing and attribution need a
separate distribution decision. A compact ESDB coverage/commonness tier is
enough for an initial deterministic ranking; it must be labeled as a coarse
priority, not an observed word frequency.
[wordfreq licensing](https://github.com/rspeer/wordfreq#license)

## 4. Architecture

```text
upstream lexical data + notices
  -> reproducible build-time preparation
  -> versioned vocabulary, metadata, and suggestion index

explicit dictionary load + personal/project vocabulary
  -> immutable checker value
  -> original-text tokens and ranges
  -> dictionary lookup
  -> ordered issues
  -> optional suggestion lookup and ranking
```

### 4.1 Lambda owns checking policy

Tokenization, dictionary precedence, lookup, suggestion ranking, document
traversal, and diagnostics belong in the package. Core checks are pure `fn`
transformations over an explicit checker and input text. Dictionary loading and
personal-dictionary file writes are `pn` operations, following **S12.1.1v3**.
The host can load once and reuse the immutable checker across checks.

Package imports must not read user dictionaries or build a large suggestion
index at module initialization. **D7.2.1** requires immutable module bindings;
**D7.2.3** caches imported packages, not an implicitly shared user session.
Any later runtime cache must be context-owned and keyed by resource identity.
Route resource reads through the existing IO boundary (**D7.1.2v2/D7.5.2**).

An offline Python preparation utility is acceptable as development tooling.
Released checks must not require Python, Node.js, a shell subprocess, or a
system spell-checking service.

### 4.2 Existing facilities to reuse

The current checkout already provides:

- `normalize(text, "nfc")`, `ord`, `chr`, code-point indexing, `slice`, `find`,
  `lower`, and `upper`; normalization is implemented by
  `lambda/runtime/lambda-eval.cpp::fn_normalize`.
- `lambda.io.unicode_category` for Unicode letter/mark classification. Current
  string-pattern `\w`/`\a` classes are ASCII, so they cannot alone tokenize prose.
- `lmd/package/doc/diff/common.ls::lower_bound`, an existing sorted-array search
  helper; evaluate a shared helper location rather than copying its algorithm.
- Original-to-prepared span mapping in `chart/timeline/search.ls` as an existing
  pattern. Share a suitably general helper if needed; do not make document
  spelling depend on a chart implementation.
- `editor/mod_decorations.ls` for derived overlays and
  `editor/mod_source_pos.ls` for editor positions. These provide integration
  mechanisms, not an existing spelling implementation.

The full-text tokenizer in `lib/fts/fts_token.cpp` is also relevant prior art,
but its token policy is different: it separates apostrophes and emits CJK
unigrams. Reuse lower-level Unicode mechanisms where appropriate without
changing full-text search semantics to suit spelling.

### 4.3 Adopted dictionary resource format

**Adopted 2026-10-22 (USER): front-coded UTF-8 text, version 1, with 64 decoded
words per line and XZ preset 6 for compressed distribution.** This is a dedicated
spelling dictionary resource format, outside Mark notation.

For English, consume ESDB's complete surface-form wordlists rather than
implementing a Hunspell affix parser first. Preserve authored spellings,
case distinctions, regional identity, and any available entry restrictions.
Do not turn every capitalized name into an accepted lowercase word.

#### 4.3.1 Stream and decoding

```text
catalog;ue;5,yst|dog;s
```

This line decodes to `catalog`, `catalogue`, `catalyst`, `dog`, `dogs`.
Each token produces exactly one word:

| Token | Decoding rule |
|---|---|
| First token on a line | A complete word; start fresh without the preceding line's state |
| `;suffix` | Append the suffix to the previous decoded word |
| `;N,suffix` | Keep the first `N` code points of the previous decoded word, then append the suffix |
| `|word` | Use the complete word as written |

After each token, the result becomes the previous word for the next token.
There is no persistent root-word anchor: offsets always refer to the immediately
previous decoded word. `N` is a retained prefix length, not a count to remove.
It counts **Unicode code points**, following **S2.5.8/S17.4.1**, and must be an
ASCII decimal integer from 1 through the previous word's code-point length,
with no sign or leading zeros. Byte offsets are only an internal indexing detail.

Use UTF-8 without a BOM, LF line endings, and a final LF. Every line contains
**64 decoded words**, except the final line, which may contain 1–64. Full words
and suffixes are nonempty. There are no blank lines, comments, header lines,
string quoting, or optional spaces. The illustrative line above is a short final line.

Semicolon, comma, pipe, CR, LF and NUL are reserved inside words; version 1 has
no escape syntax. Preparation must reject a source spelling containing one of
these characters rather than modify or silently discard it. Other supported
characters, including accents, apostrophes and hyphens, are preserved literally.

#### 4.3.2 Canonical preparation

Normalize complete spellings to NFC, preserve their case, reject normalized
collisions, and sort the words strictly by case-sensitive UTF-8 byte order
(**S6.2.2v3**). Do not apply locale collation or case folding to the stored stream.
Split the sorted sequence into consecutive blocks of 64 words:

1. Write the first word of each block in full.
2. For each following word, find its longest common prefix with the previous
   decoded word, measured in code points.
3. If the prefix is the entire previous word, emit `;suffix`.
4. Otherwise, if the prefix is nonempty, emit `;N,suffix`.
5. If there is no shared prefix, emit `|word`.
6. Terminate the block with LF and reset decoding state for the next block.

Use the longest prefix even when a literal token would be slightly shorter.
That canonical encoder produced the adopted measurement; the shorter-token
control compressed less well. This encodes only complete forms present in the
source vocabulary and does not introduce an affix or morphology rule engine.

#### 4.3.3 Identity, metadata and packaging

A word's **entry ID is its zero-based ordinal in the fully decoded sequence**,
including across line boundaries. IDs are local to a particular dictionary
artifact/hash; rebuilding the vocabulary can change them. Keep any policy
exceptions, commonness, restrictions and suggestion postings in separate
resources keyed by that artifact's entry IDs. Do not repeat `ordinary` or a
second complete `keys` array in the vocabulary stream. The metadata sidecar's
precise representation remains package implementation work.

The pack manifest records the resource format/version, language and profile,
word count, 64-word block size, compression, source provenance, artifact hashes
and original notices (§3.2). They are not embedded as word-stream headers.
Distribute the resource with **XZ / LZMA2 preset 6**; retain the corresponding
uncompressed text for inspection and conversion. XZ changes the outer bytes,
not the dictionary's decoding rules. Preparation tooling may use Python;
released loading/checking must use the package and shared runtime IO facilities.

The earlier positional JSON is an interchange/debugging representation and the
existing lookup POC's input, not the adopted dictionary resource encoding.
The former requirement to replace the vocabulary encoding with binary Mark is
superseded by this decision; see Appendix S. A future Mark pack may contain this
resource without changing its internal format. The general binary-Mark design
remains deferred under [K29c](Lambda_Design_Concurrency.md).

#### 4.3.4 Loading and lookup

Build or load a compact line-offset table and binary-search each line's first
complete word to select a block. Compare the target directly against at most
64 encoded entries in that block, without constructing intermediate dictionary
words. Track the matched target-byte prefix: an inherited mismatch remains
smaller, while a shorter retained prefix permits suffix comparison again.
Convert token prefix counts to target UTF-8 byte boundaries (**S2.5.8/S17.4.1**);
compare bytes in the dictionary's sort order (**S6.2.2v3**). Keep the vocabulary
compact instead of expanding it into one map per word or duplicating the full
spelling list in suggestion resources.

Offsets into the UTF-8 file are byte positions; prefix lengths inside tokens
remain code-point counts. A 32-bit line-offset table for the measured US pack
requires 6,872 bytes and can be rebuilt by scanning LF boundaries, so storing
it is optional. Whole-file XZ must be decompressed before arbitrary line access;
independently compressed blocks are not part of version 1.

**Adopted loading policy, 2026-10-22 (USER):** fully validate dictionaries during
preparation. Check the declared version, UTF-8, token syntax, prefix bounds,
nonempty words/suffixes, line word counts, NFC, strict global order/uniqueness,
entry count, hashes and metadata/posting bounds there. Reject malformed or
truncated packs as preparation errors. For spelling checks, read the prepared
UTF-8 bytes and build the LF line-offset index; full word reconstruction and
validation are explicit preparation operations. A supplied entry count is
prepared metadata, not recomputed by the normal reader. Keep personal overlays
separate so adding a word does not require rebuilding the base resource.

#### 4.3.5 Size and implementation status

The pinned ESDB 2026.02.25 US level-60 vocabulary contains **109,902 words**:

| Adopted resource | Bytes | KiB |
|---|---:|---:|
| Uncompressed UTF-8 text | 525,993 | 513.7 |
| XZ / LZMA2 preset 6 | **145,056** | **141.7** |

There are 1,718 lines, with 14 words in the last line. These are vocabulary-only
sizes; notices, manifests, metadata, optional indexes and suggestion resources
are additional. File size is not a claim about loaded memory or lookup speed.

The [storage POC, §7](impl/Lambda_Impl_Spelling_POC.md#7-semicolon-text-format-with-prefix-offsets)
records the encoder, complete word-for-word and independent-line round trips,
hashes and compression measurements. The
[lookup POC, §8](impl/Lambda_Impl_Spelling_POC.md#8-lambda-front-coded-lookup-and-paragraph-timing)
implements `lambda.doc.spelling.front_coded`: retained UTF-8 bytes, a `u32[]`
line-offset index, and binary search of restart keys. The
[pointer comparison update, §9](impl/Lambda_Impl_Spelling_POC.md#9-direct-byte-comparison-during-lookup)
compares the target against the selected line directly. Lookup creates no
dictionary-word strings or line slice and returns a zero-based word ID or null.
The [loading update, §10](impl/Lambda_Impl_Spelling_POC.md#10-read-and-index-loading-with-explicit-preparation-validation)
moves full reconstruction/checking into explicit `validate()` for preparation;
normal `read()`/`from_bytes()` only retain bytes and build offsets. The earlier
JSON reader remains available for its original fixture.

The POC reads the uncompressed `.dict` produced by the preparation utility;
in-process standalone XZ loading and a complete manifest/policy loader remain
implementation work. Its 100-word paragraph harness has a deliberately limited
ASCII punctuation/lowercase policy; it is not the full prose tokenizer or case
admission engine. Current timing records separate file reading, index building
and checks against the loaded dictionary; preparation validation is excluded.

The shipped `lambda.doc.spelling` now loads the prepared en-US level-60 resource
and reports source-mapped Latin prose issues. `spelling/document.ls` joins inline
leaves and excludes code, math, deleted text, `spellcheck=false` and non-English
regions; its ranges follow **S2.5.8** and **S17.4.1**. Word and Slides use
`lambda.edit.spelling` for non-document underlines. The initial check starts when
the document loads; later checks wait for a typing pause before invoking the
checker in an isolated process so dictionary loading and lookup
do not run in the editing event loop (**S13.1.3v2**, **S13.3.1v2**). Edits cancel
pending checks; a generation number rejects late results. The previous underlines
remain visible during a pending check and are replaced by the latest accepted
result. Source paths resolve
to rendered text ranges, and native text controls resolve code-point offsets to
their painted rectangles. Displayed slide text uses a snapshot of visible DOM
text leaves with native range geometry, because the compiled slide canvas has no
editor source map. It honors `spellcheck=false` and inherited language on those
leaves. The resulting wavy overlays leave source data,
formatting, selection and undo untouched (**D7.2.5**). Suggestions, broader language profiles and
complete document-adapter coverage remain separate milestones.

### 4.4 Correction algorithm

Use the symmetric-delete approach as a candidate-generation reference:
precompute deletion variants of dictionary keys, generate the corresponding
query variants, and join their postings. Upstream describes this separation of
precomputation and lookup. Its published speed comparisons are not Lambda
performance claims. [SymSpell algorithm](https://github.com/wolfgarbe/SymSpell)

Proposed first-release parameters and processing:

1. Normalize lookup copies to NFC. Generate up to two deletions from a bounded
   prefix, initially seven code points; include the undeleted key.
2. Deduplicate candidate entry IDs and reject impossible length differences.
3. Verify the full query and candidate with bounded **optimal string alignment
   distance**: insertion, deletion, substitution, and adjacent transposition.
   This restricted metric is not unrestricted Damerau–Levenshtein distance.
4. Remove forbidden and no-suggest entries; enforce dictionary case rules.
5. Rank by explicit preferred replacement, verified distance, case fit, optional
   domain priority, coarse commonness tier, and finally a stable lexical key.
   Preferred replacements must themselves pass the configured acceptance policy.
6. Project normal sentence/title/uppercase casing where the entry allows it;
   preserve required case for names and case-sensitive vocabulary.

Compare index results against exhaustive distance checks on small dictionaries,
including short words, transpositions and prefix-boundary edits. Prefix indexes
trade space against candidate volume; completeness within the declared metric
and distance needs evidence, not an assumption copied from another engine.

Cap candidate verification and token length to bound interactive work. Report
when a limit truncates suggestions; an empty, truncated result does not mean
there are no possible corrections. Exact lookup still works for long words.

## 5. Text, language, and dictionary policy

### 5.1 Positions and Unicode

All public text positions use zero-based **Unicode code-point offsets** with
half-open `[start, end)` ranges, following **S2.5.8, S7.2.1 and S17.4.1**.
Always retain the original token and range. Normalize token copies after
tokenization; normalization and case conversion may change their lengths.
Never derive an issue's end from the normalized spelling.

Canonical normalization is enabled; stripping accents or applying compatibility
normalization is not. These transformations can erase spelling distinctions.
Use the existing case operations according to **S17.7.1/S17.7.2**: simple
case-insensitive matching is distinct from full `lower`/`upper` mappings.
An English-specific candidate key may use a documented case mapping followed
by NFC, but it must still retain entry case rules and verify candidates.
Other languages need their own qualified case policy.

The English tokenizer groups letters and following combining marks, retaining
internal apostrophes and hyphens when surrounded by letters. English lookup
may equate straight and curly apostrophes while retaining the source text.
Try an explicitly listed hyphenated form first; otherwise check constituent
parts and report a component's own range. Do not silently strip possessives or
accept any combination of recognized word parts as a valid compound.

Unknown tokens in supported English text are spelling issues. Unsupported
scripts, excluded spans, and oversized tokens contribute to skipped coverage;
they are not silently counted as correctly spelled.
Declare supported scripts in each profile; any generated script-classification
tables must pin their Unicode version rather than infer script from letter case.

### 5.2 Selection and overlays

Normalize accepted locale spellings such as `en_US` to the registry key `en-US`.
Do not silently reinterpret an unavailable `en-CA` pack as US English. A pack
selection failure is an error with the available profiles listed.

Resolve overlays in this order:

1. Explicit forbidden terms produce an issue, even if another layer accepts them.
2. Per-check ignored words suppress ordinary unknown/wrong-case reports and are
   counted as ignored; they do not override explicit forbidden terms.
3. Personal/project accepted words precede optional domain and base vocabulary.
4. A preferred replacement guides a reported issue or explicit suggestion query;
   it does not by itself make an invalid replacement acceptable.

Store case policy with custom entries. Personal words do not implicitly gain
every inflection. A checker value is immutable; adding vocabulary returns a new
value. Loading and saving personal vocabulary belongs to the caller's procedural
workflow, with no hidden home-directory writes.

## 6. Proposed public API

All names are provisional. A `checker` is an ordinary immutable Lambda map
containing loaded vocabulary, indexes, profile metadata, and overlay settings.
Fallible operations use the normal `T^` result contract (**S11.4.2**).

| Function | Contract |
|---|---|
| `pn load(locale = "en-US", options = null)` | Load one installed, qualified profile and return a checker; no network fallback |
| `fn from_words(words, options = null)` | Build a small checker from caller-supplied words; useful for tests and custom domains |
| `fn with_words(checker, words, options = null)` | Return a new checker with personal/project additions and entry policies |
| `fn contains(checker, word)` | Boolean acceptance query; excludes forbidden words and ignores no token ranges |
| `fn inspect(checker, word)` | Detailed word status and matching dictionary; no suggestion computation |
| `fn suggest(checker, word, options = null)` | Return `{items, truncated}` with ranked candidates and verified distances |
| `fn check(checker, text, options = null)` | Return issues, token/coverage statistics, profile identity, and completion state |
| `fn check_runs(checker, runs, options = null)` | Document milestone: check caller-supplied prose runs with source mappings |

```lambda no-run
// no-run: proposed package; the implementation does not exist yet.
import spelling: lambda.doc.spelling

pn main() {
    let base = spelling.load("en-US")^
    let checker = spelling.with_words(base, ["Lambda", "Radiant"], {case: "exact"})^
    let report = spelling.check(checker, "This is teh Lambda document.")^
    let fixes = spelling.suggest(checker, "teh", {limit: 5})^
    print(report)
    print(fixes)
}
```

Illustrative issue; the particular suggestion order is a testable quality target,
not a measured result:

```json
{
  "kind": "unknown_word",
  "word": "teh",
  "start": 8,
  "end": 11,
  "language": "en-US"
}
```

Reports include `issues`, `checked_tokens`, `ignored_tokens`, `skipped_tokens`,
`complete`, `truncated`, `next_offset`, and profile/resource identity. If an issue
limit stops the scan, `complete` is false and `next_offset` identifies where to
resume. Completion means the scan finished; coverage counters separately show
what was skipped. Suggestions can be requested on demand or included explicitly.

Initial options should cover excluded ranges, ignored words, optional
acronym/identifier skipping, suggestion inclusion and count, maximum issues,
maximum suggestion distance, candidate work, and token length. Validate option
names and values rather than silently ignoring mistakes. Choose numeric work
limits from measured fixtures; distance two and five displayed suggestions are
starting defaults, not performance guarantees.

An unknown word is ordinary report data. Missing/corrupt dictionaries, invalid
options, invalid encodings, or unsupported profiles are errors. Do not convert
a dictionary load failure into an empty successful report.

## 7. Documents and interactive editing

The next milestone accepts semantic document trees or explicit prose runs.
Use existing HTML/Markdown/DOCX/LaTeX parsing and document adapters; do not
strip raw markup with a regex. Check text and selected prose attributes such
as image descriptions, while excluding code, scripts, styles, math, destinations,
and content marked `spellcheck=false`. Preserve declared language boundaries.

A prose run has text, language, a document revision, and a mapping from its
code-point intervals to source segments. Join text across inline formatting so
`mis<strong>spell</strong>ed` is checked as one word. Block boundaries insert
separators; code/math exclusions also prevent accidental word concatenation.
Report multiple mapped ranges when one token crosses several text leaves.
Rendered text offsets are not raw HTML/XML/Markdown source-file offsets.

For the editor, reuse decorations and transactions. The current
`editor/mod_source_pos.ls` documents **UTF-8 byte offsets** inside text leaves,
whereas spelling's public string ranges use code points. Convert explicitly at
the adapter boundary using each original leaf; do not pass these offsets through
unchanged. DOM UTF-16 offsets, if needed, likewise require an explicit adapter.

Recheck changed prose runs with neighboring token context, invalidate mapped
issues touched by edits, and discard results whose revision no longer matches.
Defer checks during IME composition. Host scheduling, cancellation and debounce
are integration concerns; the core checker remains a deterministic transform.

Show underlines as non-document decorations. Apply a chosen suggestion through
the existing edit transaction so undo and selection mapping remain correct.
The spelling package must not introduce a second editing path or native editing
policy, consistent with **D7.2.5**.

## 8. Multilingual and engine alternatives

Full Hunspell support cannot be obtained by taking the text before `/` on every
`.dic` line. Validity also depends on `.aff` flags, affixes, case restrictions,
forbidden forms, and compounds. A stem can require an affix before it is valid.
These are acceptance semantics, not optional suggestion refinements.
[Hunspell dictionary specification](https://raw.githubusercontent.com/hunspell/hunspell/master/man/hunspell.5)

Two later implementation routes are reasonable:

| Route | Benefits | Costs and boundary |
|---|---|---|
| Implement qualified rules in Lambda | Consistent package ownership and portability; no new engine dependency | Substantial morphology work; publish supported directives and languages; reject unsupported acceptance-affecting rules |
| Add a narrow native engine bridge | Reuse mature morphology and compounding | New packaging/ABI/lifetime obligations; keep document and editor policy in Lambda and original vendor code unchanged |

For broad multilingual fidelity, **Hunspell** is the first native candidate.
**Nuspell** is another Hunspell-format engine with ICU-backed Unicode and
LGPL-3.0-or-later licensing, but its dependency footprint needs evaluation.
Neither is a dictionary supplier by itself. [Hunspell](https://hunspell.github.io/),
[Nuspell requirements](https://github.com/nuspell/nuspell)

Recommend shipping the English package first. Then select one additional
language with actual user demand and qualify its tokenizer, case policy,
dictionary, rule coverage, suggestions, and distribution together. If general
multilingual support becomes the immediate goal, prefer a mature native engine
over claiming that finite word-list expansion handles arbitrary productive
compounds. No universal `.aff` importer is part of the first release.

## 9. Acceptance and evaluation

Keep dictionary correctness, suggestion usefulness, document mapping, and
performance as separate gates.

| Gate | Required evidence |
|---|---|
| Lexical acceptance | Every admitted prepared entry round-trips under its declared case policy; negatives include wrong case, invalid inflections, regional differences, and explicit forbidden forms |
| Candidate correctness | Small exhaustive comparisons cover all candidates within the configured distance, except explicitly reported work-limit truncation |
| Correction quality | Frozen natural typo fixtures and separate synthetic edits; report top-1/top-5 recall, no-suggestion rate, and clean-text false alarms for each profile |
| Unicode/ranges | Accents, decomposed marks, curly quotes, emoji before a typo, supplementary letters, CRLF, empty text, and long tokens retain correct original spans |
| Policy | Personal/domain precedence, exact-case additions, ignore/exclusion handling, explicit locale failures, corrupt packs and unknown options |
| Document/editor | Inline-split words, block boundaries, code/math exclusions, language changes, byte/code-point conversion, stale revisions, IME and undo |
| Resources | Reproducible generation, source and artifact hashes, complete applicable notices, bounded artifact parsing and index validation |

Use upstream ESDB exports as the vocabulary reference for ESDB packs. For a
Hunspell-derived language, compare acceptance to a pinned upstream engine and
the exact same dictionary. Do not require identical suggestion ordering across
different algorithms, and distinguish deliberate profile differences from bugs.

Before tuning, freeze a held-out typo corpus and clean prose set. Publish their
source/license and counts; exclude generated “typos” that are valid words or
intentional alternate spellings. Establish numeric quality thresholds from the
baseline and record them before further tuning. No quality score is claimed here.

Measure dictionary load/decode, first lookup, warm checking throughput,
suggestion p50/p95, and peak resident memory on a named host and pinned executable.
Include both regional packs, a larger optional pack, repeated and all-unique
misspellings, and short-word candidate collisions. Separate cold process/import
time from warm queries. A provisional interactive goal is p95 suggestions within
50 ms after loading; qualify or revise it explicitly from release-build evidence.
Follow `doc/dev/Developer_Guide.md` §7; never benchmark a debug executable or run
`make release` inside a worktree.

## Appendix A. Proposed implementation increments

Detailed progress should be recorded in a later `vibe/impl/Lambda_Impl_Spelling.md`.
The following increments define the intended completion boundaries.

| Increment | Locations and deliverable | Exit gate |
|---|---|---|
| 0. Data qualification | `utils/prepare_spelling.py`; adopted front-coded text/XZ resources, pinned manifests and notices under `doc/spelling/data/` | Reproducible US/British exports, exact round trips, verified licenses, artifact/heap size and decode measurements |
| 1. Lexical core | `doc/spelling.ls`; `spelling/{dictionary,text,tokenize,options}.ls`; adopted text/XZ reader and line index | Word/text checks, resource validation, exact ranges, overlays and failures; matching `.ls`/`.txt` goldens |
| 2. Suggestions; first release | `spelling/{index,distance,suggest}.ls`; prepared deletion indexes | Exhaustive candidate gate, frozen typo/clean-text reports, bounded-work behavior and release measurements |
| 3. Documents/editor | `spelling/document.ls` and existing document/editor adapters | Source-mapped runs, explicit offset conversions, incremental decorations and undoable replacement |
| 4. Language expansion | Additional qualified packs; decide Lambda rules versus native bridge | One language at a time, with acceptance-oracle and suggestion-quality evidence |

All package paths in this table are relative to `lmd/package/` unless prefixed
with `utils/`. Keep fixtures under `test/lambda/spelling/`, with every golden-driven
`.ls` paired with its expected `.txt`; helper scripts use the repository's helper
naming convention. Register the focused runner in `test/README.md` during
implementation and update `doc/Lambda_Packages.md` only when the package exists.

For package-only milestones, run the new focused fixtures in interpreter and JIT
tiers and the applicable package regression gate. If Lambda runtime mechanisms
change, also run `make test-lambda-baseline` and `make test262-baseline`. If
Radiant engine behavior changes, read `radiant/README.md` and run its required
focused and aggregate gates. This proposal itself requires document checks only.

## Appendix B. Decisions to retain when implementation starts

- English-first, offline, explicit profiles, immutable checker values.
- ESDB level 60 for the bundled US and British baseline; broader vocabulary is
  optional and quality-tested, with accurate profile and license labels.
- Adopted vocabulary resource: semicolon/offset UTF-8 front coding, 64 words
  per line, with XZ preset 6 distribution (§4.3).
- Vocabulary validity and suggestion priority remain separate.
- Prepared English forms avoid an early commitment to full Hunspell emulation.
- Original source ranges survive normalization, inline formatting and editor
  offset conversion.
- Publish measured quality, memory and latency before claiming readiness;
  record multilingual and document/editor coverage as separate milestones.

## Appendix S. Superseded dictionary storage decisions

**Superseded 2026-10-22 (USER) by §4.3's adopted semicolon/offset text format.**
The following wording records the earlier JSON-array POC and intended binary-Mark
migration. It is historical; the implemented JSON POC and its measurements remain
valid, while its distribution requirements no longer govern the word resource.

~~Use a sorted array of lookup keys with parallel **positional entry arrays** and
binary search. Per-word maps are replaced by rows to avoid repeating field names
in every entry (USER, 2026-10-22). Keep the small descriptive header as a map:~~

```json
{
  "format_version": 1,
  "language": "en-US",
  "keys": ["cat", "cats", "dog", "dogs"],
  "entries": [
    ["cat", "ordinary"],
    ["cats", "ordinary"],
    ["dog", "ordinary"],
    ["dogs", "ordinary"]
  ]
}
```

The entry ID is its array index, and `keys[i]` identifies `entries[i]`. The row
schema is `[word, case_policy, commonness?, flags?]`. Omit unused trailing fields;
use `null` for a missing commonness value when flags follow. The POC recognizes
`ordinary` and `exact` case labels, nonnegative integer commonness, and flags
`1 = forbidden`, `2 = no_suggest` (a bit mask). These are storage fields; this
POC implements exact NFC lookup, not full case admission or restriction policy.
The measured English files use two-column `[word, "ordinary"]` rows throughout
to isolate the requested representation; that does not establish case fidelity.

Store suggestions as deletion keys mapped to entry IDs using
sorted keys, posting offsets, and a flat ID array. This avoids thousands of
generated Lambda declarations and a mandatory mutable hash-table build at load.
~~Minified UTF-8 JSON is the initial portable artifact encoding; loaded memory and
decode time must be measured before accepting it for distribution.~~ Retaining the
word in both `keys` and each row duplicates vocabulary bytes; measure a rows-only
alternative for comparison while preserving the requested format for this POC.

~~**The eventual distribution encoding must be Lambda/Mark binary packing when
that is supported** (USER, 2026-10-22). Preserve the logical positional schema
and entry IDs across encodings, and keep JSON for inspection and interchange.~~
The current binary-Mark design is deferred under
[K29c](Lambda_Design_Concurrency.md); this POC does not introduce a private
binary format or claim an implemented binary loader or measured binary size.
Sharding remains a separate decision based on actual measurements.
Do not assume a compressed file implies a small runtime heap, or duplicate the
same full vocabulary inside both the lookup and suggestion indexes.

Validate format versions, sorted-key order, posting bounds, entry IDs and declared
sizes when loading a pack. Keep small personal overlays separate from the base
index, so adding a name does not rebuild the entire dictionary; search those
overlay entries when producing suggestions as well as during exact lookup.

The [2026-10-22 storage POC](impl/Lambda_Impl_Spelling_POC.md) measures the
requested two-column format against pinned ESDB 2026.02.25 exports: `en-US`
has **109,902 entries, 3,935,365 bytes of JSON (3.75 MiB), or 616,484 bytes
with gzip**. Its equivalent object-entry control is 5,473,993 bytes. These are
vocabulary-only measurements; suggestion indexes, optional fields and separately
stored provenance/notices are additional. Gzip is a size comparison, not the
future binary-Mark encoding or a loader feature.
