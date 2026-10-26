# Chinese IME implementation

> **Status (2026-10-26):** M1 engine and M2 session reducer implemented;
> qualification recorded below. M4 Traditional preparation/profile selection
> and its conversion/quality gates are implemented (§7). M3 transport, native
> popup and body adapter are
> implemented and focused-qualified; the aggregate gates retain two failures
> reproduced on the preserved pre-M3 host. M0 tooling and authored
> held-out coverage are qualified; retained-heap accounting and compressed-only loading remain open.
> The first release remains unqualified against the provisional performance targets.
> **Design:** [Lambda_Pkg_IME_Chinese.md](../Lambda_Pkg_IME_Chinese.md).
> **Spec linkage:** D7.2.4v4 places the package under
> `lmd/package/ime/chinese/`; D7.1.2v2 governs resource IO when the runtime
> loader is added. The engine/IO split follows S12.1.1v3 and explicit
> dictionary loading follows D7.2.1/D7.2.3. Sorting uses S6.2.2v3's byte
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

## 3. M1 engine

The entry `lmd/package/ime/chinese.ls` exports `load`, `from_bytes`,
`candidates` and `fuzzy_pairs`. Its `syllables`, `lattice`, `pack`, `lookup`
and `rank` modules implement M1. See the
[API reference](../../doc/Lambda_IME_Chinese.md) for options and data shapes.

Resource acquisition happens only in `pn load`; import is inert. Both spelling
and IME use `lambda.io.line_index`, promoted from spelling without copying its
builder. The IME loader checks format/version/profile and byte sizes, then
retains the bytes plus u32 offsets. Dictionary validation remains the explicit
Python preparation operation; there is no hidden full validation at load.

The lattice retains every admitted full-syllable, fuzzy, initial and trailing
prefix edge. A suffix-reachability pass excludes a leading path that cannot
be completed: otherwise `x` in `xi'an` would spuriously admit 先 while leaving
an unsegmentable `i'an`. Traversal and lookup have explicit resource budgets;
every discarded route, scan or candidate sets `truncated`. Postings decode
lazily and are bounded by `max_postings`; only one final ranking is performed
per query. Normal lookup never constructs a map/string for every pack entry.

`consumed` records the raw code-point offset, including an immediately
following apostrophe; `coverage` records syllables. For `beijingdaxue`, 北京
consumes seven input characters and covers two syllables. This lets M2 consume
the exact selected route without guessing its segmentation (S2.5.8/S17.4.1).

All 11 fuzzy pairs are symmetric, operate on whole initials/finals, compose,
and discard expansions outside the 417-syllable inventory. Settings names and
query option names are checked under S17.8.1. Orthographic initials retain
`zh`/`ch`/`sh`; 中国 is `zhg`, while `zg` requires `z=zh` as specified in §6.4.

Ranking uses one lexicographic sort key for match quality, negative weight and
pack position. It uses negative numeric keys instead of `desc`, because
S6.2.3 defines `desc` as full reversal, which would reverse equal-key pack
ties. Rare longer words are placed after the leading entries of the next
available shorter group, skipping empty coverage levels. Text reached by
several paths is retained once at its best position.

### 3.1 Binary-slice prerequisite

Full-pack qualification exposed a runtime defect in `fn_slice`: the generic
`VECTOR_TEXT_ITEMS` dispatch materialized every byte before the already
implemented `heap_binary_slice` branch could run. A 13-byte posting from the
5,077,740-byte lexicon cost about 85 ms; 64 repeated reads cost 5,579 ms.
The recorded sample showed repeated byte materialization and a peak footprint
near 975 MB. The original whole-corpus probe was terminated and is not
acceptance evidence.

Removing that dispatch lets binaries use the existing storage-retaining slice
path. Byte semantics remain S2.5.8's; storage-sharing behavior is the existing
PL5 / Binary2 Phase-2 contract. `EvalContextTest.BinarySliceBuiltinRetainsStorageWithoutMaterializingBytes`
asserts shared storage, flattened nested offsets, embedded-NUL contents and no
new payload allocation/copy. It failed against the original builtin before
the fix. This runtime change requires the Lambda and Test262 aggregate gates.

### 3.2 Frozen baseline and calibration

`test/ime/heldout.json` freezes 64 repository-authored MIT query cases, separate
from the M0 preparation/smoke fixture. `test/ime/run_engine.py` retains the
executable, engine-source, fixture and pack identities, row-level ranks,
resource counts, timings and process peak RSS. No external representative
corpus or broad quality guarantee is claimed.

The initial untuned baseline was recorded before setting ranking parameters.
Its expected-word weight's 10th percentile, **2703**, becomes `coverage_floor`.
The selected `group_leaders` is **1**, its median full-input rank. The
75th-percentile choice of two leaders regressed initials first-page coverage
from 50/64 to 49/64 and was rejected; one leader preserves that floor. Both
options remain overridable. The initial first-letter
control is retained separately: first-letter `s`/`z` must not be confused with
orthographic initials `sh`/`zh` when fuzzy pairs are off.

`test/ime/baseline.json` freezes the untuned coverage/rank counts as minimum
acceptance floors. The runner fails on a changed fixture identity or any
regressed floor, including the two-leader counterexample. Final defaults
preserve or exceed all of these minima:

| Query mode | Found | Top 1 floor | Top 3 floor | Top 9 floor |
|---|---:|---:|---:|---:|
| Full pinyin | 64/64 | 44/64 | 56/64 | 63/64 |
| Orthographic initials | 64/64 | 20/64 | 32/64 | 50/64 |
| All fuzzy pairs | 64/64 | 37/64 | 47/64 | 58/64 |

The qualified default run on the pinned release host produced:

| Query mode | Found | Top 1 | Top 3 | Top 9 | Truncated queries |
|---|---:|---:|---:|---:|---:|
| Full pinyin | 64/64 | 54/64 | 63/64 | 64/64 | 43/64 |
| Orthographic initials | 64/64 | 23/64 | 35/64 | 50/64 | 64/64 |
| Full pinyin, all fuzzy pairs | 64/64 | 53/64 | 61/64 | 64/64 | 52/64 |

Truncation is reported even if the expected word is present. These rates
describe this frozen set and these budgets, including their coverage limits.
The simulated keystrokes per character are respectively **3.6454**, **1.8298**
and **3.6454**, using typed letters, one selection and one page advance per
nine candidates. No missing words are excluded in the qualified run.

Interpreter and JIT returned identical rank, leading candidate, expected-word
weight, truncation and scan counts for **192/192 queries**. The full interpreter
run reported `executed=1 fallback=0 excluded=0`.

### 3.3 Release measurements

Host: macOS 26.5.2, arm64. The pinned release executable is
`temp/ime_chinese/m1/lambda-m1-fixed.exe`, SHA-256
`b02e095267d133b39259fc129900fab3247b2c79229a6fb88cec5b44003241dd`.
Each query's sample is the median of three executions; percentiles below span
the 64 per-query medians. The explicit load timer includes resource reads and
index/inventory construction, not process startup.

| Tier | Load ms | Full pinyin p50/p95 ms | Initials p50/p95 ms | All-pairs p50/p95 ms | Process peak RSS bytes |
|---|---:|---:|---:|---:|---:|
| JIT | 358.05 | 4.53 / 22.74 | 7.92 / 14.31 | 8.60 / 41.27 | 206,045,184 |
| Interpreter | 686.13 | 15.83 / 71.99 | 23.87 / 44.21 | 26.59 / 128.92 | 164,904,960 |

Peak RSS includes compilation, the runtime and transient query work; it is not
the engine's retained-heap measurement. Pack bytes plus the offset payload
remain **10,217,536 bytes**, excluding headers and inventory metadata.
The provisional **200 ms load / 5 ms JIT p95** targets are **not met**.

The same-host untuned control reduced JIT p95 from 60.96/44.00/109.01 ms to
19.38/12.70/34.94 ms by ranking once per query, with unchanged reported rank
counts on that control. This is separate from ranking calibration; the report
retains both sets of source identities and row-level results.

### 3.4 Verification and artifacts

The registered goldens cover inert import, loading, exact/prefix/abbreviation
lookup, every inventory syllable and every fuzzy pair in both directions,
all-pairs validity, separators, `v`/`ü`, ambiguity, partial consumption,
deduplication, rare-word demotion, stable ties, strict options and all budgets.
The focused GTest filter includes four IME behavior fixtures, the inert module
fixture and spelling's compatibility fixture. Both tiers pass 6/6. The M0
Python contracts pass 26/26. The candidate, pack and ranking fixtures also
pass 3/3 with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` on JIT.

```sh
LAMBDA_EXEC_BACKEND=interp ./test/test_lambda_gtest.exe \
  --gtest_filter='AutoDiscovered/*/ime_*:AutoDiscovered/*/proc_spelling_front_coded'
LAMBDA_EXEC_BACKEND=jit ./test/test_lambda_gtest.exe \
  --gtest_filter='AutoDiscovered/*/ime_*:AutoDiscovered/*/proc_spelling_front_coded'
python3 test/ime/run_engine.py --executable <release-host> \
  --tier jit --report temp/ime_chinese/m1/qualification-jit.json
python3 test/ime/run_engine.py --executable <release-host> \
  --tier interp --report temp/ime_chinese/m1/qualification-interp.json
```

Local artifacts include `baseline-untuned.json`, `aggregate-rank-control.json`,
`baseline-canonical.json`,
`qualification-jit.json`, `qualification-interp.json`, `tier-parity.json`,
`slice-before.log` and the release build log under `temp/ime_chinese/m1/`.
Aggregate gates completed successfully:

- `make test-lambda-baseline`: **8,238/8,238** (Input **2,112/2,112**;
  Lambda Runtime **6,126/6,126**). The storage/GC regression is included in
  `test_lambda_errors_gtest`, **284/284**.
- `make test262-baseline`: **40,261/40,261**, zero regressions, failed tests,
  or non-fully-passing/unstable batches. The separate excluded set is 2,652.
- Final one-leader calibration: focused goldens **6/6 in each tier**; full
  frozen-set qualification and threshold checks run on the final defaults.

Gate logs are `lambda-baseline.log` and `test262-baseline.log` under the same
artifact directory. `slice-after.json` retains the successful native case and
its binary/source identities. The two-leader controls are retained as
`qualification-{jit,interp}-leaders2.json`; they are not the accepted defaults.

## 4. M2 session and learning

`session.ls` implements the pure key reducer, settings replacement and overlay
attachment. `user.ls` validates, indexes, matches and learns user words;
`query.ls` is the shared M1/M2 query path. The public entry exports `session`,
`key`, `configure`, `settings`, `with_user`, `load_user` and `save_user`.
Effects remain explicit procedures (S12.1.1v3, D7.1.2v2); importing the package
does not read a dictionary or user file (D7.2.1/D7.2.3).

The reducer handles partial selection, exact-prefix undo, digits/Space,
paging and highlight movement, punctuation and paired quotes, Shift-tap mode
switching, modifier pass-through, cancellation and the 32-letter bound.
Preedit carets count code points (S2.5.8/S17.4.1). Switching to English flushes
an existing preedit; Shift chords/repeats do not toggle. `configure` validates
only `fuzzy` and `page_size` (S17.8.1), refreshes an open composition without
changing chosen/raw text and leaves the learned overlay intact (IME13).

Selections stay pending until the composition commits; undo/cancel never
trains the overlay (IME8). Learning uses deterministic selection counters,
then ranks matching user words by recency and count before base candidates.
It can add a word absent from the pack. Both paths reuse the same syllable
lattice, orthographic-initial filter and text deduplication. The overlay builds
first-letter/syllable-count buckets on attachment or commit; scanning uses
the existing posting budget and reports truncation. It never rebuilds the
base dictionary or scans every learned entry on each keystroke.

The version-1 JSON user file contains only format/version/profile, next_seq
and `[word, reading, count, last_seq]` entries. Validation checks unique pairs,
NFC Han words of 1–9 code points, syllable membership/count and compact-int
counter bounds/order. An exhausted counter returns an error. Saving validates
before atomic replacement and does not serialize its private index or host
preferences. `utils/generate_ime_han.py` verifies/generates the Han matcher
from M0's SHA-pinned Unicode 18 Scripts.txt and embeds its complete license.
Python remains development tooling; user-file handling runs entirely in Lambda.

### 4.1 Quoted-name compiler prerequisite

The backslash punctuation case exposed a name-decoding defect. Symbol values
already decoded escapes, but `direct_key_text` only stripped quotes. Thus
`{'\\':1}["\\"]` returned null. This violates S8.2.2v6, which identifies names
after escape decoding. `build_ast.cpp` now shares its existing quoted-literal
decoder with quoted name construction, including Unicode escapes and
surrogate pairs. The regression failed before this fix and passes afterward
for both map and element keys. No parser/vendor code or formal ruling changed.

### 4.2 Verification and artifacts

The focused harness passes **16/16 in each execution tier**, including the M1
fixtures, spelling compatibility, eight M2 behavior fixtures, the inert
session-helper module and the quoted-name regression. Exact action-list
goldens cover compose/candidates/commit/cancel ordering. Two successive word
selections commit together and train both entries; overlay scans also exercise
the posting cap. Persistence covers an
explicit round trip, invalid-save preservation, malformed/missing files,
independently saved settings and cleanup. Overlay validation checks 26 invalid
shapes/words/readings/counters, astral Han and counter exhaustion.

The partial-selection, learning, persistence and quoted-key fixtures pass
**4/4** on JIT with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
Preparation contracts pass **26/26** and the generated Han matcher verifies
all **25** pinned ranges. The full-pack runner passes the frozen rank floors
in both tiers, with identical ranks/leading words/weights/truncation/scan counts
on **192/192** queries and the same counts as the qualified M1 defaults.
These timing samples overlapped other validation and do not qualify a
performance change; the provisional load/p95 targets remain open.

Validation used private debug/release executables under `temp/ime_chinese/m2/`
to preserve another task's shared Radiant executable. Their SHA-256 identities:

- debug: `e85a5e36264422cc3ae427e498c8f8d4d55e8020b3ec558f038f2ffa53124e1f`.
- release: `a22c0c8427d03494e24121d07785147d7ee54123f77e857fc727bbdf9d19dc6b`.

Both used the generated build flags, recompiled `build_ast.cpp` and linked the
existing objects into private targets. Native baseline test executables and
their runtime archives were rebuilt separately. Input/Lambda execution uses
the same baseline recipes in an isolated workspace with the private debug
host; Test262 uses `LAMBDA_TEST262_EXE` to pin the private release host.

Local evidence: `focused-{interp,jit}.log`, `gc-stress.log`,
`qualification-{interp,jit}.json`, `tier-parity.json`, `prepare-tests.log`,
`input-baseline.log`, `lambda-baseline.log`, `test262-baseline.log` and build
logs under `temp/ime_chinese/m2/`. Test262 passes **40,261/40,261**, with no
regressions or unstable batches. The Input/Lambda aggregate passes
**8,248/8,248** (Input **2,112/2,112**, Lambda **6,136/6,136**), with final
process exits of zero. The full-pack all-fuzzy session smoke also commits
北京, learns its selection and promotes it on the next composition. The API
document's two runnable blocks compile with no marker violations.

## 5. Remaining work

M1's functional engine and frozen baseline are implemented. The provisional
performance targets remain open; this is not the M3 first release. M0 still
needs retained-heap attribution and standalone exact/prefix/abbreviation
microbenchmarks distinct from the measured whole queries. A compressed-only
resource loader and the proposed explicit Lambda `validate` API also remain
open; full preparation validation is available in Python now.

M2's session reducer, settings and explicit user overlay are implemented.
M3 implements queued composition transport, the native candidate popup, settings
and the DOM adapter, including OS-composition stand-down. M4 adds the
Traditional profile and document selection; its evidence follows in §7.


## 6. M3 Radiant integration

**Scope and acceptance (2026-10-26):** IME-E1 deferred composition through the
existing controller, IME-E2 native numbered popup/page indicator with click
selection, and IME-E3 Chinese/English indicator plus window opt-in and document
override (IME-O1/IME-O2). Input policy stays in `lambda.dom.ime` (D7.2.5),
with a document-owned exact root for the opaque adapter value (D5.3.3).

### 6.1 Implementation

`lambda.dom.form` routes keydown/keyup, focus and native popup actions through
`lambda.dom.ime`. The adapter loads the dictionary on first eligible focus,
reuses it across the document's controls, applies live fuzzy/page preferences,
and retains mode and learning across focus changes. The engine value belongs
to its document runtime; it does not cross GC heaps (D5.4.1). Document
`data-ime="chinese"` / `"off"` overrides the opt-in window setting exposed by
`radiant.set_chinese_ime`. Native window state survives navigation; document
sessions are released with their owning script runtime.

`radiant/input_method.cpp` queues copied UTF-8 composition requests with
validated `DomNodeRef` targets. Delivery begins after the requesting handler
unwinds, with a bounded queue boundary for listener-enqueued work. Native
code owns popup geometry, at most nine copied labels and hit testing; Lambda
owns candidate selection, settings, mode and learning. Popup presses keep
editing focus. An unprevented outside press cancels preedit, while an author
toolbar can preserve focus through ordinary `mousedown.preventDefault()`.
Target removal and document teardown release pending native requests/labels.
An OS composition cancels any scripted provisional range before taking over;
package keys pass through until that OS composition ends (IME10).

The shared caret rectangle now converts block-local metrics to viewport
coordinates for both native IME and popup placement. Shared keyboard dispatch
retains printable keydown cancellation for the later character callback on
text controls as well as rich surfaces, preventing duplicate Latin input.

Rich composition exposed an existing undo defect: provisional updates became
individual history entries. The package history now groups text/retained
structural deltas, commits one undo entry, and replays cancellation to restore
the original selection/content. The generic `dom.edit_sync_selection` waist
copies an explicitly restored DOM Selection into the live edit capability so
cached mutation coordinates cannot overwrite it. History policy remains in
Lambda (D7.2.5); native code only replays opaque deltas and selection state.

Forced GC then exposed a separate native event-construction defect: allocating
a property name could collect the event/value or initialization record, so
the first keyboard event sometimes had an empty type. A native regression
failed before the fix. Event property writes and keyboard/composition/input
construction now use precise roots across allocations (D5.3.3), and that
regression passes alongside the existing focus/mouse lifetime regressions.
No conservative stack scanning or vendor changes were introduced.

### 6.2 Verification

The verification budget uses focused causal UI fixtures and neighboring
editing cases during implementation, then the Radiant baseline plus the
Lambda/Input and Test262 gates for changed native DOM APIs. Private debug and
release executables under `temp/ime_chinese/m3/` preserve the shared host used
by concurrent work. The runner workspace points at that pinned debug host;
native test archives/runners are rebuilt independently.

The nine UI fixtures pass **65/65 assertions in each of interpreter and JIT**. They cover physical
input/textarea/contenteditable typing, provisional text, commit, one-step
undo/redo, selected-text cancellation, maxlength, OS stand-down and takeover,
numbered popup selection, English/Chinese mode, live fuzzy/page settings,
window/document precedence, focus changes and detached-target cleanup.
Seven neighboring editing/IME UI fixtures pass. Popup, rich history and
lifetime fixtures pass **21/21 assertions** with forced GC and freed-memory
poisoning. The pure engine/session harness passes **16/16 in each tier**.
The rendered preedit popup was inspected below the caret with numbered rows,
a highlight and page indicator. The installed-pack demo is
`./lambda.exe view test/demo/ime/chinese.ls`.

The Radiant aggregate exits **1**: **6,188 passed, 141 partial, 2 skipped,
1 failed**, from 6,332 results. All **692/692 UI cases** pass. The failure is
`CssCascadeMemory.PageLoadAndRecascadeStayWithinBaseline`: jqueryui retains
382,866 CSS-role bytes against a 373,940-byte budget; linuxmint retains
668,675 against 667,901. The preserved pre-M3 M2 host reproduces the same
initial/recascade counts with the same current harness. No baseline or
threshold changed. Layout reports 3,157 full passes and 137 accepted partial
results, with zero failures; visual rendering has four expected failures.
The first sandboxed attempt was stopped after macOS application-service denial
stalled native graphics; the recorded complete run used permitted host access.

The static-module source inventory passes its scaffold checks. The legacy
`check_dom_editable_architecture.py` audit still searches the retired
`lambda/dom/edit_registry.ls` path instead of `lmd/package/dom/edit_registry.ls`
and reports no owner. This unrelated checker was not changed; no five-DSO
linker qualification is claimed.

Input passes **2,112/2,112**. Lambda exits **1** with **6,136 passed,
1 failed** (6,137 total), so their combined result is **8,248 passed,
1 failed**. The failure is `JavaScriptTests/JsFileTest.Run/dom_media_playback`:
the assertion `media.paused && media.currentTime > 0` after a 100 ms timer
intermittently returns false. An alternating isolated run reproduces the same
assertion on the preserved pre-M3 host. These are reported aggregate failures;
later passing isolated runs do not replace the failed qualification. The
native event regression passes as part of the **251/251** JS script tests.

Test262 passes **40,261/40,261**, with no regressions or unstable batches,
and exits zero. Alternating media controls pass 10/12 and fail 2/12 on each
host; the aggregate remains reported as failed. The native transport-lifetime
regression and the three native event-GC cases pass. Lint for Radiant integer
casts and `git diff --check` pass. API document blocks have zero violations.
The installed-pack demo passes its five preedit/commit/undo assertions; its
rendered popup was inspected separately from the synthetic fixture.

The complete local [validation manifest](../../temp/ime_chinese/m3/validation.json)
records source/artifact hashes, executable identities, exact focused/aggregate
counts, existing failures and excluded environment/early-link attempts. Logs,
`focused-ui-final.json`, `gc-ui-final.json`, `radiant-summary.json`,
`lambda-summary.json`, `media-paired.json`, `demo-preedit.png` and
`preedit-final.png` are alongside it. The pinned debug executable SHA-256 is
`9a8f9e27eae86ec6ee090b24cc710eff6ba39dc04f80fc80290c47d2d8bd0960`;
release is `11e528e8bdc6d7dfd358b0a2a8b865921924abe1fa5823251cea937684a07d1f`.

Run the currently qualified private host from the repository root:

```sh
./temp/ime_chinese/m3/lambda.exe view test/demo/ime/chinese.ls
```

M3's functional implementation is delivered. Its required aggregate gates are
not fully green because of the two control-reproduced failures above; no
whole-release or performance qualification is claimed.

### 6.3 Remaining limits

M3's native popup is qualified for top-level documents on macOS. Nested-frame
popup placement, Windows/Linux platform runs and assistive-technology exposure
are unqualified. Dictionary preparation remains explicit. Loading is reused
per document runtime, so separate documents load their own GC-owned value.
M0 retained-heap/compressed-only loading work, the provisional performance
targets and future sentence conversion remain open. M4 Traditional is delivered
in §7. M3 functional
delivery does not establish completion of those broader release gates.


## 7. M4 Traditional profile

**Scope (2026-10-26):** IME11 / proposal §5.6. M4 adds a preparation-time
Traditional profile to the existing pure engine and M3 adapter; no native
engine code, pack-format version or formal ruling changes. Effects remain
explicit procedures (S12.1.1v3, D7.1.2v2), word lengths remain code points
(S2.5.8), and deterministic posting order uses S6.2.2v3.

### 7.1 Conversion and installation

The source lock pins OpenCC ver.1.4.2 at commit
`025f371dc76b598d77384fbdab90c937471844d8`, with SHA-256 identities for
STCharacters, STPhrases and its complete Apache-2.0 license. Only explicit
`fetch --profile zh-Hant` acquires inputs. Preparation and validation are
offline; the default Hans commands do not require OpenCC inputs.

`TraditionalConverter` normalizes the tables to NFC, applies the longest
phrase at each position, then uses every unresolved character alternative.
Outputs are not converted recursively. Reading/weight pairs are inherited
from the admitted Hans source; identical converted word/readings merge by
maximum source weight. It never chooses a new pronunciation. Traditional
character membership includes converted base characters and phrase-exception
glyphs. Existing table writing, abbreviation top-K derivation, byte ordering,
compression and full validation are shared by both profiles.

The v1 manifest records conversion policy, source lexicon identity, expansion,
merges, drops and exact OpenCC provenance. Full Traditional validation
reconstructs the source conversion and checks exact forms/readings/weights,
in addition to the existing syntax, counts, hashes and abbreviation invariants.
A rehashed lexicon cannot silently lose an alternative or alter its reading or
weight. This is Python preparation validation; the proposed explicit Lambda
`pn validate(profile)` remains separate unfinished work. Normal loading still
reads bytes/builds indexes without full validation.

The installed pack has **234,399** lexicon postings: **11,351** character
readings and **223,048** word readings. Conversion expands **17,164** ambiguous
source postings, emits **234,585** postings before merging **186** collisions,
and drops **zero** converted forms. Maximum expansion is **48** forms for one
source posting. The profile character set contains **8,363** Han characters.
The two table payloads total **10,151,251** bytes; XZ totals **3,026,368**.
Bytes plus u32 offsets total **10,939,259**, excluding runtime/allocator overhead.

`ime.load("zh-Hant")` discovers the installed profile alongside Hans. A host
can choose `<body data-ime="chinese" data-ime-profile="zh-Hant">`; the demo
has 简体/繁體 buttons. Changing profile/directory first loads the requested
engine, cancels any old provisional range, then creates a fresh session.
Committed document text stays as authored. Learned overlays remain explicitly
saved, profile-specific values; switching never implicitly converts them.
The native root remains opaque (D7.2.5 / D5.3.3).

### 7.2 Quality report

The checked-in [Traditional report](../../test/ime/quality_zh_hant.md) contains
all 64 authored expected spellings/readings, per-case ranks, scope, source and
pack identities, timings and reproducible commands. The first untuned profile
run freezes `test/ime/baseline_zh_hant.json`; `--record-baseline` refuses an
existing file. No profile-specific ranking parameters were changed.

| Mode | Found | Top 1 | Top 3 | Top 9 | Truncated |
|---|---:|---:|---:|---:|---:|
| Full pinyin | 64/64 | 52/64 | 63/64 | 64/64 | 44/64 |
| Orthographic initials | 64/64 | 22/64 | 35/64 | 50/64 | 64/64 |
| All fuzzy pairs | 64/64 | 51/64 | 61/64 | 64/64 | 53/64 |

Interpreter and JIT agree on **192/192** ranks, first words, weights,
truncation and scan counts. The fresh Hans control agrees with the M2 reference
on **192/192** corresponding results. Truncation is reported when a resource
budget discards work, even when the expected word is present. These authored
fixtures establish regression floors, not representative corpus quality.

The pinned release host from M3 is unchanged. Its current samples are
**343.37 ms** JIT load and **19.67 / 13.15 / 34.29 ms** query p95 for
full/initials/all-fuzzy; interpreter load is **688.14 ms**. Process peak RSS
is **209,616,896 / 172,343,296 bytes** for JIT/interpreter. These are query
samples, not end-to-end key latency or retained-heap attribution. The
provisional performance targets remain unmet; no cross-run improvement is claimed.

### 7.3 Verification

The finite verification budget covers offline conversion/provenance rejection,
full-pack validation/rebuild, frozen ranks in both tiers, package/session
goldens, Traditional UI/profile changes and M3 neighbors. Native code is
unchanged; M3's pinned debug/release hosts and current native test runners are
reused. Aggregate recipes run in the existing private workspace, preserving
the shared root executable and unrelated dirty work.

- **38/38** offline contracts pass; the original 26 remain covered once each.
- The **32/32** independent authored conversion cases match, and **38/38**
  expected target word/readings are present in the full pack.
- Explicit full validation passes for both profiles. Independent Traditional
  preparation is **6/6 byte-identical**. Hans tables remain **4/4 unchanged**;
  its NOTICE also remains unchanged.
- The focused package harness passes **17/17 in each tier**. New synthetic
  Traditional goldens check exact/prefix/initials candidates, both 發/髮,
  commit/learning and profile mismatch rejection without an installed full pack.
- All ten IME UI fixtures pass **83/83 assertions per tier**, including
  input/textarea/contenteditable, candidate clicks, undo and a profile change
  while composing. Traditional UI passes **18/18** with forced GC plus freed
  memory poisoning; the Traditional package golden also passes that mode.
- The full installed-pack demo passes **8/8** profile-switch/preedit/commit/undo
  assertions. Its Traditional popup rendering was inspected.
- API document blocks and `git diff --check` pass.

Fresh aggregate execution finishes with:

| Gate | Passed | Partial | Skipped | Failed | Exit |
|---|---:|---:|---:|---:|---:|
| Radiant | 6,189 | 141 | 2 | 1 | 1 |
| Input | 2,112 | 0 | 0 | 0 | 0 |
| Lambda | 6,137 | 0 | 0 | 1 | 1 |

Radiant passes all **693/693 UI cases**. The failures are the same CSS-memory
budget and intermittent `dom_media_playback` 100 ms clock assertion recorded
in §6.2, with the same actual/expected values. The pre-M3 control evidence is
retained there; no test, baseline or threshold was changed to hide them.
Input/Lambda combined records **8,249 passed, 1 failed**. Native host hashes
remain exactly those qualified in M3; Test262's **40,261/40,261** evidence
belongs to that unchanged native host and was not rerun for package/data work.

The local [M4 validation manifest](../../temp/ime_chinese/m4/validation.json)
records source/artifact/executable identities, all focused/aggregate counts,
pack hashes, reproducibility and remaining limits. Detailed artifacts include
`prepare-report.json`, `reproducibility.json`, `qualification-hant-jit-final.json`,
`qualification-hant-interp.json`, `tier-parity.json`, `focused-ui.json`,
`gc-ui.json`, aggregate summaries/logs and `traditional-demo.png`.

The full installed pack is ready locally. Run
`./temp/ime_chinese/m3/lambda.exe view test/demo/ime/chinese.ls` and select 繁體;
no native rebuild is needed for this package/data milestone. M4's specified
conversion fixtures and quality report are delivered; the broader release
qualification remains limited by the aggregate failures and performance work.

### 7.4 Remaining scope

M4's Traditional pack and runtime selection are implemented. Regional variants
(IME-O8), optional domain/scheme extensions, popup accessibility and sentence
conversion remain deferred. M0 retained-heap attribution, compressed-only
loading, explicit Lambda validation and performance targets remain open.
Prepared full packs stay locally generated and gitignored per IME-O7.


## 8. Horizontal candidates and seven-option default

**Scope (USER, 2026-10-26):** replace the vertical popup with a horizontal
strip, default to seven candidates, retain explicit page sizes 1–9, and let
the demo inherit the default. IME-E2 owns native presentation/hit testing;
IME13 / S17.8.1 keep settings in the Lambda package.

Acceptance: seven numbered options share a line at desktop widths; measured
label widths and shared bounds keep painting/clicks aligned; narrow windows
wrap without losing selectable options. Paging, highlight, mode switching and
editing focus remain functional. Verify the user-visible before/after output,
a focused default/paging golden in both tiers, IME UI neighbors, pointer
selection after resize, Radiant integer-cast lint and the Radiant aggregate.
The native strip now measures labels through the shared font-layout helper,
records per-option viewport rectangles, and uses those rectangles for both
painting and hit testing. A rounded blue highlight marks the selected option;
the mode/page button follows the candidates. Whole options wrap on narrow
viewports, and popup padding consumes clicks without moving editing focus.
The reducer owns the default of seven; the DOM adapter omits an absent page
size so it inherits that value. The demo removes its forced size of two and
preserves explicit page-size settings when toggling fuzzy matching. The older
controls fixture explicitly requests two to retain its paging coverage.

Focused verification passes **18/18 package goldens and 92/92 UI assertions
in each tier**. The new fixture covers option seven, paging to the eighth
option, an explicit size-nine override, desktop alignment, 2x raster scaling,
narrow wrapping and pointer selection after resize. The full installed-pack
demo shows seven options for `xuexi` and commits 学习. Both the desktop and
narrow screenshots were inspected. Radiant integer-cast lint and API document
blocks pass.

The Radiant baseline exits **1** with **6,213 passed, 141 accepted partial,
2 skipped and 1 failed**. All **713/713 UI cases pass**. The sole failing
case is `CssCascadeMemory.PageLoadAndRecascadeStayWithinBaseline`, the CSS
memory-budget debt already recorded in §6.2. A fresh M3-host control in the
same workspace reproduces all six assertions with identical actual/budget
values. No baseline or threshold changed.

At the horizontal-strip checkpoint, the root `./lambda.exe` was rebuilt with
the qualified private host's SHA-256,
`cda5230d8bd7b939dd79e14b1c57548aa62a3685f8712fc0f3707941edcad8f0`.
Its full-pack demo passes **2/2 assertions** for seven horizontal candidates
and committing 学习. Run `./lambda.exe view test/demo/ime/chinese.ls`; historical
M3/M4 artifacts retain their original native presentation. The local
[validation manifest](../../temp/ime_chinese/horizontal/validation.json)
records the focused and aggregate results. Visual evidence includes
[desktop](../../temp/ime_chinese/horizontal/after.png) and
[narrow 2x](../../temp/ime_chinese/horizontal/fixture-narrow.png) captures.

### 8.1 Page-font follow-up

**Scope (USER, 2026-10-26):** candidate labels must match the page's sans-serif
text. The popup previously measured and painted with `UiContext::default_font`,
which selected the default serif face despite the editor's computed font.
Popup placement now borrows the active editor's selected-tree font for both
measurement and painting (D4.5.1v4), including its used size. It resolves the
font on each placement instead of retaining a pointer across reflows.

Acceptance is the supplied `da` textarea case with sans-serif numerals and
Chinese glyphs, matching geometry and pointer selection after narrow resize.
Verification covers before/after screenshots, all IME UI neighbors, integer-cast
lint and the required Radiant baseline after rebuilding affected native tests.

The `da` capture now shows sans-serif numerals and Chinese glyphs matching the
editor. All **92/92 focused IME UI assertions pass**, including 2x raster scale,
narrow wrapping and pointer selection after resize. The rebuilt root executable
matches the private host and passes **3/3** full-pack layout, click-selection
and focus assertions. Integer-cast lint, API document blocks and diff checks pass.
The fresh Radiant baseline records **6,213 passed, 141 accepted partial,
2 skipped and 1 failed**, with **713/713 UI cases passing**. The sole failure
remains `CssCascadeMemory.PageLoadAndRecascadeStayWithinBaseline`.

Run `./lambda.exe view test/demo/ime/chinese.ls`. The local
[validation manifest](../../temp/ime_chinese/sans/validation.json) records this
font follow-up separately from the earlier horizontal-strip checkpoint;
[before](../../temp/ime_chinese/sans/before.png),
[after](../../temp/ime_chinese/sans/after.png) and
[narrow 2x](../../temp/ime_chinese/sans/fixture-narrow.png) captures were inspected.

### 8.2 Pinyin caret editing

**Scope (USER, 2026-10-26):** Left/Right move within the remaining pinyin,
while Up/Down retain candidate navigation. Typing, Backspace and Delete use
that cursor; candidate selection maps it past the consumed prefix. Positions
remain code points (S2.5.8/S17.4.1), and policy stays in Lambda (D7.2.5).
Acceptance covers both bounds, insertion/deletion in the middle, separators,
selected Chinese prefixes, selection/undo and all three editing surfaces.

The reducer adds a raw-input caret and emits a compose-only action when it
moves. One splice helper handles insertion and both deletion directions,
including empty separators. The existing restore-last-selection behavior
remains available after raw input becomes empty. UI assertions now verify the
composition's code-point caret as well as its text. This exposed a rich-text
transport defect: the native composition event supplied its payload with a
zero caret despite applying the edit at the correct position. Passing the
caret directly through the shared composition dispatcher fixes that loss and
removes the form-only temporary hint.

The new reducer golden and native UI assertion both failed before the repair.
Focused qualification passes **19/19 goldens and 139/139 IME UI assertions in
each tier**, including the new golden's **36 checks** and the native caret
fixture's **47 assertions**. The full-pack demo passes **9/9 assertions** with
existing Chinese text before the pinyin, an insertion/deletion in the middle,
raw commit and one-step undo preserving that Chinese prefix. Its
[caret capture](../../temp/ime_chinese/caret/demo-caret.png) was inspected.

The fresh Radiant baseline exits **1** with **6,214 passed, 141 accepted partial,
2 skipped and 1 failed**, including **714/714 UI cases passing**. The sole
failure remains `CssCascadeMemory.PageLoadAndRecascadeStayWithinBaseline`.
Integer-cast lint, API document blocks and diff checks pass. The rebuilt root
`./lambda.exe` matches the qualified private host; run
`./lambda.exe view test/demo/ime/chinese.ls`. The local
[validation manifest](../../temp/ime_chinese/caret/validation.json) retains
the source and host identities, before-failure evidence and qualification logs.

### 8.3 Compact typography, syllable steps and expanded candidates

**Scope (USER, 2026-10-26):** reduce candidate typography, move Left to the
previous syllable boundary while Right advances one letter, and use Down to
open a complete candidate grid where all four arrows navigate options.
Syllable boundaries reuse the engine's ordered segmentation lattice; positions
remain code points (S2.5.8/S17.4.1). Lambda owns expansion and selection policy
(D7.2.5), while native layout reports responsive grid columns.

Acceptance covers ambiguous/explicit syllables, incomplete input, compact
versus expanded arrow behavior, all returned candidates beyond the old native
nine-option limit, row navigation, scrolling, pointer selection, Escape back
to the strip, and editing/commit after selection. Candidate text keeps the
editor's family with a reduced size capped at 16 CSS pixels before zoom.
Verification uses reducer goldens in both tiers, native UI cases, full-pack
desktop/narrow captures, ownership cleanup, lint and the Radiant baseline.

The engine now exposes cached `caret_stops` from the same ordered viable
lattice used for lookup. Expanded frames carry the complete returned option
set and an absolute highlight; native layout reports the actual columns to
Lambda, which applies row/column navigation. Escape collapses before canceling,
and pointer actions carry an index instead of encoding it as a digit key.
The body adapter explicitly reads getter-backed keyboard fields before adding
geometry; spreading the native event had dropped its key payload.

Native frames own dynamically sized labels/cells and a reduced-font cache
alias. The shared font-release helper is inline beside `font_prop_copy`, so
state-only owners use the real cleanup without linking the layout font setup.
Painting and hit testing share the same grid, clipping and scroll position;
keyboard selection scrolls into view and wheel input stays inside the menu.
Fractional-font alignment assertions use the same 0.01 CSS-pixel tolerance on
both axes; the observed discrepancy was 0.000008 pixels after viewport offset.

Focused verification passes **20/20 goldens and 163/163 IME UI assertions in
each tier**. The new menu fixture has **22 passing assertions**, covering all
64 synthetic options, selection of option 50, 2x rendering, three-column
navigation after resize, wheel scrolling, collapse and caret editing. The
state-store ownership test passes, as do integer-cast lint, API document blocks
and diff checks. The installed-pack `dajia` demo passes **6/6 assertions** and
shows all **128 returned candidates** in its scrollable grid; compact, expanded,
selected and narrow captures were inspected.

The fresh Radiant baseline exits **1**, recording **6,215 passed, 141 accepted
partial, 2 skipped and 1 failed**. All **715/715 UI cases pass**. The sole
failure remains `CssCascadeMemory.PageLoadAndRecascadeStayWithinBaseline`;
no budget or baseline was changed. The rebuilt root `./lambda.exe` matches the
qualified private host. Run `./lambda.exe view test/demo/ime/chinese.ls`.
The local [validation manifest](../../temp/ime_chinese/menu/validation.json)
retains source/host identities and focused/aggregate evidence, with
[compact](../../temp/ime_chinese/menu/compact.png),
[expanded](../../temp/ime_chinese/menu/expanded.png) and
[narrow 2x](../../temp/ime_chinese/menu/interp/grid-narrow.png) captures.

### 8.4 Demo button behavior and feedback

**Scope (USER, 2026-10-26):** make the demo's settings controls work visibly,
including before a field has focus. The reproducer confirms spelling/profile
changes, but Window off leaves composition enabled under the initial document
override. IME-O2 still gives document overrides priority; the demo now selects
window scope when a window button is chosen. Use window setting must inherit
the existing value instead of forcing it on. Button pressed states and a live
status report effective scope, profile and spelling. Cold configuration must
not send a null target to the native popup.

Acceptance covers every button, cold spelling/profile changes, switching both
character profiles, window on/off and document overrides, returning to an off
window default, and preserving editing focus. Verify real typing/results as
well as the visible feedback in both tiers, then run the Radiant baseline.

The before-repair cold-click reproducer records **8 passed, 1 failed**: Window
off did not stop the document-forced input method. It also logged invalid
popup-target calls during cold spelling configuration. The demo now clears
its override when choosing window scope, while Use window setting preserves
the existing window value. Pressed states and a live status initialize on load
and update after every command. Handler errors are surfaced in the status;
the spelling options use an explicit empty map value in procedure context
(S16.4.2v2). The adapter retains settings but skips native popup presentation
until an editor target exists.

The new button fixture verifies all nine controls, cold configuration and both
profiles, actual fuzzy/exact conversion behavior, direct window on/off,
document overrides, inheriting an off window default and retained focus.
Existing controls coverage explicitly enables the window before testing its
live disable action, rather than relying on the old inherit-forces-on behavior.

Focused qualification passes **18/18 IME goldens and 218/218 IME UI assertions in
each tier**. The new button fixture passes **53 assertions**; the original
cold-click reproducer now passes **9/9** without invalid popup-target errors.
The [Traditional selection](../../temp/ime_chinese/buttons/traditional-selected.png)
and [window-off status](../../temp/ime_chinese/buttons/window-off.png) captures
were inspected. API document blocks and diff checks pass. The unchanged native
host was pinned for the fresh Radiant baseline, which exits **1** with
**6,216 passed, 141 accepted partial, 2 skipped and 1 failed**. All **716/716 UI
cases pass**. The sole failure remains
`CssCascadeMemory.PageLoadAndRecascadeStayWithinBaseline`; no budget or baseline
was changed. The local
[validation manifest](../../temp/ime_chinese/buttons/validation.json) retains
source and executable identities, the reproducer and qualification evidence.
Restart with `./lambda.exe view test/demo/ime/chinese.ls` to load the updated
Lambda demo and adapter; the native executable is unchanged from §8.3.

### 8.5 Fuzzy master toggle and pair configuration

**Scope (USER, 2026-10-26):** replace the single z/zh action with a fuzzy
pinyin on/off toggle and adjacent `v` configuration button. The panel lists
the engine's supported pairs (IME13), remembers selections while off, and
applies checkbox changes immediately while on without disturbing preedit.
All pairs start selected in the panel; the master remains off initially.
An empty selection switches matching off and disables the master until a
pair is selected. Engine defaults and explicit settings contracts remain
unchanged (S17.8.1).

Acceptance includes every pair, toggle off/on retention, editing options while
off, live s/sh and z/zh word matches, page-size preservation, focus/preedit
retention, Escape/outside dismissal and a bounded narrow-viewport panel.
Reuse the native popover and checkbox operations, derive options from public
metadata, and verify the UI in both tiers, visually and in the Radiant baseline.

The demo now uses a split control: **Fuzzy pinyin on/off** plus `v`. Native
popover element methods and checkbox state supply the panel mechanics;
`chinese.fuzzy_pairs` supplies every name and example. The remembered selection
is separate from the active engine settings. Commands preserve an explicit
page size and editing focus, and changing a pair while on refreshes the live
composition. Done and Escape retain preedit; outside dismissal follows the
ordinary pointer/focus policy. The panel is viewport-bounded and scrollable.

Focused qualification passes **18/18 IME package goldens and 268/268 UI
assertions in each tier**. The new fixture passes **52 assertions**, including
all 11 choices, all/none, on/off retention, s/sh and z/zh word conversion,
preedit/focus and page-size preservation, dismissal and narrow 2x scrolling.
The [desktop panel](../../temp/ime_chinese/fuzzy_options/interp/pairs.png),
[active composition](../../temp/ime_chinese/fuzzy_options/interp/composing.png),
and [scrolled narrow panel](../../temp/ime_chinese/fuzzy_options/interp/narrow-scrolled.png)
were inspected. The installed full pack also returns 是非 for `sifei`: rank
17 with all pairs, rank 10 with only s/sh. API document blocks and diff checks
pass.

The unchanged native host was pinned for the fresh Radiant baseline:
**6,217 passed, 141 accepted partial, 2 skipped, 1 failed**, including
**717/717 UI cases passing**. The sole failure is the previously reproduced
`CssCascadeMemory.PageLoadAndRecascadeStayWithinBaseline`; no budget was
changed. `make run-radiant-baseline` exits 2 for the runner's exit 1. The
[validation manifest](../../temp/ime_chinese/fuzzy_options/validation.json)
records the executable identity, focused and aggregate results, source hashes,
and local artifacts. Restart `./lambda.exe view test/demo/ime/chinese.ls` to
load these demo changes; no native rebuild is needed.

### 8.6 Complete-reading ranking before frequency

**Scope (USER, 2026-10-26):** improve the ranking after `sifei` put 是非 behind
是否. The fresh installed-pack reproducer reports 是非 at rank 17: its full
fuzzy reading consumes five letters, but its weight 2581 is below the 2703
floor. 是否 at rank 2 consumes only `sif`, leaving `ei`. The old coverage
bucket demotes the better reading before comparing its match quality.

IME7v2 now groups complete exact readings, complete fuzzy readings, then the
remaining routes. The old coverage/frequency algorithm runs within each group,
and deduplication retains the best route after grouping. Query passes the
actual normalized raw length rather than inferring completeness from a bounded
path search. Learned selections keep IME8 priority; budgets, dictionary weights
and frequency defaults stay unchanged. No sentence-context model is added.

The authored regression uses a weight-1 是非 against weight-60000 是否 and
weight-65000 时候, proving the behavior does not depend on a word-specific
boost or adjusting the threshold. It fails against the old implementation and
passes after the change. The expected order for complete `beijingdaxue` now
keeps both complete homophones before partial 北京; the separate synthetic
coverage test still checks rare-word demotion within a group.

Fresh before/after qualification uses the same pinned release host, pack
manifests and unchanged frozen query fixtures. All four after reports pass
their existing acceptance floors. The two tiers agree on all **192/192**
rank/leading-word/weight/truncation/scan-count results per profile. No row's
rank regresses: Simplified improves 10 full-input and 11 all-fuzzy rows;
Traditional improves 11 and 12 respectively. All initials rows are unchanged.

| Profile | Mode | Before top 1 / 3 / 9 | After top 1 / 3 / 9 | Found |
|---|---|---|---|---|
| zh-Hans | full pinyin | 54 / 63 / 64 | 63 / 64 / 64 | 64 / 64 |
| zh-Hans | initials | 23 / 35 / 50 | 23 / 35 / 50 | 64 / 64 |
| zh-Hans | all fuzzy | 53 / 61 / 64 | 63 / 64 / 64 | 64 / 64 |
| zh-Hant | full pinyin | 52 / 63 / 64 | 61 / 64 / 64 | 64 / 64 |
| zh-Hant | initials | 22 / 35 / 50 | 22 / 35 / 50 | 64 / 64 |
| zh-Hant | all fuzzy | 51 / 61 / 64 | 61 / 64 / 64 | 64 / 64 |

These are the repository-authored regression corpora, not a representative
external quality estimate. Scan counts are unchanged; the work/path/candidate
limits and truncation behavior remain bounded. No performance improvement is
claimed. The installed-pack `sifei` result now leads with 私费, 是非, 施肥,
试飞, 时候, 是否: 是非 moves from rank 17 to 2, and 是否 from 2 to 6.
The local [row comparison](../../temp/ime_chinese/ranking/comparison.json)
retains every improved/unchanged/regressed outcome.

Focused verification runs **19/19 scoped IME golden cases** (18 existing plus
the new completeness regression) in both tiers. The earlier §8.4/8.5 scoped
counts have been corrected against their retained GTest logs; the broad
`*ime_*` name filter had also selected unrelated runtime cases.

All **268/268 IME UI assertions in each tier** pass across the 15 fixtures,
including selecting the weight-1 full fuzzy candidate while settings change
live. The installed-pack replay passes **5/5** assertions and commits
`大家好是非` by clicking the second candidate. The
[strip](../../temp/ime_chinese/ranking/full-pack-strip.png) and
[expanded grid](../../temp/ime_chinese/ranking/full-pack-grid.png) were
inspected. API document blocks and diff checks pass.

`make test-lambda-baseline` completes with **6,160 passed, 1 failed**, exit 2.
The sole failure is `JavaScriptTests/JsFileTest.Run/dom_media_playback`: its
`media.paused && media.currentTime > 0` assertion after a 100 ms wait fails
intermittently. Three isolated runs record **one failure and two passes**.
The unchanged executable is byte-identical to the pinned pre-change host;
no media code, timer, test expectation or baseline was altered. This aggregate
gate is reported as failed, separately from the passing ranking qualification.
No native Radiant code changed for this ranking update.

The [validation manifest](../../temp/ime_chinese/ranking/validation.json)
retains the before/after source and executable identities, frozen-profile
reports, row comparison, focused UI checks and aggregate failure evidence.
Restart `./lambda.exe view test/demo/ime/chinese.ls` to load the package changes.

### 8.7 Fuzzy enabled at demo startup

**Scope (USER, 2026-10-26):** make fuzzy pinyin on by default in the demo.
The root body selects `data-ime-fuzzy="all"`, so acquisition uses all supported
pairs before any toolbar action. The initial button label and selected state
report on; the existing toggle and pair configuration retain their behavior
(IME13). This is an explicit demo policy, leaving the engine's omitted query
settings contract unchanged (S17.8.1).

The existing fuzzy UI fixture now converts `sifei` to 是非 immediately on first
focus, before clicking any toolbar button. Button coverage checks the initial
selected state and cold off/on transitions; live-configuration coverage starts
from an explicitly selected off state.

Focused verification passes **272/272 UI assertions in each tier** across all
15 IME fixtures. First-use conversion, the selected button, all pair options,
cold toggling and active-composition configuration pass. The startup-on
[panel capture](../../temp/ime_chinese/fuzzy_default/interp/pairs.png) was
inspected. API document blocks and diff checks pass. The native host is
unchanged; this demo default requires no engine build or broad engine rerun.
The local [validation manifest](../../temp/ime_chinese/fuzzy_default/validation.json)
retains the host/source identities and all focused UI results. Restart
`./lambda.exe view test/demo/ime/chinese.ls` to apply the startup default.

### 8.8 Up from the first candidate row collapses the grid

**Scope (USER, 2026-10-26):** Up from the first logical row of the expanded
candidate menu returns to the compact strip. The old navigation path clamped
Up to candidate zero and kept the grid open. The reducer now uses the existing
rendered-column count to detect that boundary, keeps the absolute selection,
and emits only the collapsed candidate frame. Preedit and its code-point caret
are unchanged (S2.5.8/S17.4.1); policy remains in Lambda (D7.2.5, IME-E2).
Up from lower rows continues row navigation, repeated Up in the strip is a
no-op, and Down reopens at the retained candidate.

The existing session-menu golden now covers first/other columns, the three-
column boundary, reopening, repeated Up and selection after collapse. Its
new assertions fail with the previous reducer. Native UI coverage exercises
wide and narrow 2x layouts, scrolling back to the first row, preserved pinyin
and caret, the selected column and reopening. No native code changes are needed.

Focused qualification passes **19/19 IME golden cases and 281/281 UI
assertions in each tier**. The menu fixture has **31 passing assertions**;
its [wide collapse](../../temp/ime_chinese/up_collapse/interp/collapsed-wide.png)
and [narrow 2x collapse](../../temp/ime_chinese/up_collapse/interp/collapsed-narrow.png)
captures were inspected. They retain the highlighted candidate and pinyin
caret, and Down reopens at that selection. API document blocks and diff checks
pass. Query/ranking source identities match the previous frozen-corpus
qualification; no new dictionary or ranking change is involved.

`make test-lambda-baseline` completes with **6,160 passed, 1 failed**, exit 2.
Its sole failure is the same `dom_media_playback` assertion at the 100 ms
playback wait seen in §8.6, with the identical output mismatch. No media code,
test expectation or baseline was changed. The native executable remains
byte-identical to the pinned host; the aggregate failure is retained separately
from the passing focused checks.

The [validation manifest](../../temp/ime_chinese/up_collapse/validation.json)
records source/host identities, the failing pre-change regression and current
focused/aggregate results. Restart `./lambda.exe view test/demo/ime/chinese.ls`
to load the reducer and updated keyboard help.

### 8.9 Suspend the native overlay during fuzzy configuration

**Scope (USER, 2026-10-26):** the native 中 indicator must not paint above the
fuzzy-options dropdown. The popover deliberately preserves editor focus, so
the adapter's idle indicator remains active; native overlays paint separately
from the author DOM (IME-E2). The focused pre-change reproducer records
**4 passed, 1 failed**, with the indicator still visible while options are open.

The DOM adapter now exposes `suspend_popup(body, suspended)`. Every normal
presentation path honors the document's transient flag. Resuming snapshots the
existing reducer frame rather than reconfiguring or requerying it, retaining
mode, selection, expansion, preedit and caret (D7.2.5, S2.5.8/S17.4.1). The
reducer's existing frame helper is shared with the adapter instead of copying
its slicing/highlight rules. Resume checks focused-target eligibility and OS
composition priority (IME10). No native painting or global popup order changes.

The demo routes open, Done, arrow-toggle, Escape and outside dismissal through
one helper. Settings changes while the panel is open update the engine while
keeping its overlay hidden. Tests cover the idle indicator, active preedit,
restoring an expanded menu at a nonzero highlight and all existing dismissal
paths. No composition cancellation or mode switching is used to hide the UI.

The idle reproducer now passes **5/5** assertions. An extended replay passes
**14/14**, covering cold open/close without an editor, programmatic focus while
the panel is already open, a caret anchor beneath the panel, and English-mode
restoration. The focused-editor check uses `element.focus()` plus an explicit
focus assertion: the event harness's ordinary focus action clicks the target,
which instead hits an overlying popover.

All **19/19 IME golden cases and 290/290 UI assertions in each tier** pass;
the fuzzy panel fixture has **64 passing assertions**. The
[idle indicator](../../temp/ime_chinese/popup_pause/idle.png) and
[active preedit](../../temp/ime_chinese/popup_pause/interp/composing.png)
captures were inspected: no native indicator or candidates cover the options.
API document blocks and diff checks pass. The unchanged native executable is
byte-identical to the pinned host; no native rebuild is needed.

`make test-lambda-baseline` completes with **6,160 passed, 1 failed**, exit 2.
The only failure remains `JavaScriptTests/JsFileTest.Run/dom_media_playback`,
the same intermittent 100 ms playback assertion recorded in §8.6/8.8. No media
code, expectation or baseline was changed; this failed aggregate gate remains
separate from the passing focused checks. The
[validation manifest](../../temp/ime_chinese/popup_pause/validation.json)
retains pre-change sources, the reproducer, host identities and focused and
aggregate evidence. Restart `./lambda.exe view test/demo/ime/chinese.ls` to
load the adapter and demo policy changes.

### 8.10 Local Bible dictionary import

**Scope (USER, 2026-10-26):** download Sogou dictionaries 35457 and 54542,
combine them into Lambda's existing pack format, and keep every data file
gitignored (IME6/IME-O7). `utils/prepare_ime_scel.py` provides pinned fetch,
offline preparation and explicit validation. It shares serialization and
table-contract validation with the base preparer. No runtime change is needed.
The small source lock records URLs, sizes, hashes and provenance; dictionary
bytes remain beneath ignored `temp/ime_sources/` and
`lmd/package/ime/chinese/data/zh-Hans-bible/` directories.

The downloads decode to **28,721 phrase records and 391 names/places**.
The combined pack admits **21,332 unique word/readings**, retaining all 391
names/places. The existing nine-character limit excludes **7,732** records;
the supported character set excludes another **48**. Every omission appears
in the pack's `rejected.jsonl`; original SCEL bytes remain available locally.
The source extension prefixes are permutations of 1 through each dictionary's
record count, so they are not assumed to be corpus frequencies. Imported
weights are zero without documented frequency evidence. Original syllable
sequences are preserved and canonicalized without guessing polyphonic readings.

Validation passes **38 base-pack contracts and 8 authored SCEL contracts**,
including malformed binary rejection, deduplication, loss accounting and source
identity checks. All eight generated files rebuild byte-for-byte. Full imported-
pack and existing Simplified-pack validation pass. Eleven real exact, initials,
mixed and fuzzy queries find their targets in both interpreter and JIT, with
identical results; three long-phrase queries also report normal budget
truncation while retaining the expected first candidate. Git verifies every
downloaded/generated file is ignored and none is tracked. Evidence is in
`temp/ime_chinese/bible/validation.json`. API document blocks and diff checks
pass. No runtime build or broad engine gate is required for preparation tooling.

### 8.11 Default demo uses standard and Bible dictionaries together

**Scope (USER, 2026-10-26):** the interactive test page must load both
dictionaries by default. Preparation now accepts `--with-base` and `--profile`.
It reuses the standard source assembly, merges word/readings by maximum weight,
then rebuilds both lookup tables from the union (IME6). OpenCC conversion
precedes merging for Traditional, retaining unresolved alternatives (IME11).
All source notices, calibrated frequency scales and merge counts accompany
the ignored local packs. The runtime query/session code is unchanged.

The Simplified union contains **234,335 postings**, adding **21,305** Bible
word/readings after 27 overlaps. Traditional contains **261,794 postings**,
adding **27,395** converted Bible word/readings after 27 overlaps. Independent
comparison against the installed standard packs verifies that every original
posting and its exact weight survive. All generated files are ignored and none
is tracked. Both preparations pass full source/table validation.

The demo's initial directory selects `zh-Hans-bible-combined`. Profile buttons
switch to the corresponding `zh-Hant-bible-combined` or Simplified directory,
while retaining explicit fixture/host directory overrides. Acquisition still
occurs on enable/focus (D7.2.4v4/S12.1.1v3). The previous page fails the focused
Bible conversion reproducer; the updated page converts both standard words
and Bible names/phrases on input, textarea and contenteditable surfaces.

All **38 standard and 10 SCEL preparation contracts** pass. The 15 existing
UI fixtures pass **295 assertions per tier**, including five new cold-start
directory/profile checks. A local full-pack replay adds **21 passing assertions
per tier** for both dictionaries and both script profiles: **316 total per
tier**. Its Simplified and Traditional captures were inspected. The initial
replay used the input-only `set_editing_value` action on contenteditable;
removing that invalid test action gives a clean replay without a product change.
Standard 诺亚 correctly retains its frequency-based priority over unweighted
挪亚; imported terms receive no word-specific ranking boosts.

Evidence is in `temp/ime_chinese/combined_default/validation.json` and
`pack-audit.json`. API document blocks and diff checks pass. Only preparation
tooling, demo policy, focused tests and documentation changed; no engine build
or broad engine gate is required.

### 8.12 Demo dictionary customization dialog

**Scope (USER, 2026-10-26):** browse, add and delete words from the test page's
active combined dictionary. The page now owns a native modal with a paged word/
reading list, indexed pinyin-prefix search, add form, row deletion, validation
feedback, Close/Escape and restored toolbar focus. It scrolls on narrow/short
windows. Pinyin search stays literal; Chinese additions use pasted text or an OS
input method. The native IME overlay is suspended while the modal owns focus.

`test/demo/ime/mod_dictionary.ls` reuses the pack's binary search/posting
decoder, line index and existing user-word validation. It creates new lexicon
and abbreviation byte/index values without modifying installed resources
(IME6/IME8, D7.2.4v4/S12.1.1v3). Browsing decodes one page, not the whole pack.
Explicit edits reindex both affected tables; this is a small personal edit
list, not a bulk dictionary preparation path. New pairs carry zero weight;
re-added built-in pairs retain their original weight. Deletion also removes the
matching learned promotion. Other learned choices retain their normal priority.

`mod_dictionary_ui.ls` saves final pair edits as versioned JSON, atomically,
before publishing the new engine value. The default paths are gitignored
`temp/ime_chinese/custom-zh-Hans.json` and `custom-zh-Hant.json`. Each profile
restores edits on acquisition and toolbar profile changes, including when an
editor keeps focus. The original engine value is retained so live edits and
restart replay use the same base weights. Explicit host/fixture dictionary
directories default to document-only edits unless `data-ime-custom-file` is
supplied. Malformed local files open the dialog with a visible error; a failed
save preserves the live dictionary and existing files. Temp cleanup removes
these local preferences. No pack format, runtime API or native engine changed.

**Verification:** both interpreter and JIT pass **21 procedural IME goldens**,
including the new 30-check model and nine-check persistence fixtures. The new
registered UI fixture passes **25 assertions per tier**, covering real modal
focus, literal pinyin typing, paging, deletion after learning, validation,
immediate full-pinyin conversion, re-addition, Escape and inherited/explicit
policy restoration. The 15 existing UI fixtures retain **295 passing assertions
per tier**, and the unchanged full-pack replay adds 21. Fresh process pairs
verify addition/deletion persistence against both real combined packs in both
tiers; additional cases check profile changes without refocusing, malformed
files and failed-save rollback. Full and narrow screenshots were inspected.
Original lexicon/abbreviation hashes still match all four prepared manifest
entries; all data and custom files are ignored and untracked.

Evidence: `temp/ime_chinese/dictionary_dialog/validation.json`, `focused-ui.json`,
`focused.json`, `final-ui.json`, `persistence.json` and `pack-integrity.json`.
Document-block and diff checks pass. Demo/model-only changes require no native
rebuild or new broad runtime/Radiant baseline qualification.

### 8.13 Two input-scope toggles

**USER, 2026-10-26:** replace the four Window/Document on/off buttons with
one toggle per scope. Window flips the window setting and clears the document
override; Document flips the effective document setting and sets an override.
Use window setting retains its existing inheritance action. Both toggle labels
and `aria-pressed` reflect current on/off values, including when the window is
on beneath a document-off override. Policy remains in the page (D7.2.5).
The updated buttons/controls and dictionary-modal fixtures pass **111 assertions
per tier** in interpreter and JIT. Cold toggling, focused composition cancellation,
actual conversion, inheritance and modal restoration are covered. The native
capture was inspected; document-block and diff checks pass. Evidence:
`temp/ime_chinese/toggles/results.json`. No engine or build changes were needed.

### 8.14 Clear stale results after a rejected dictionary search

The reported `da` → `大` sequence rejected non-pinyin input before updating
the table, leaving the previous rows, cursor/history and paging buttons active.
Search now publishes an empty first page on validation failure and displays
the pinyin-only prompt (S17.8.1). Valid searches replace that page and clear
earlier messages, including when no words match. Add/delete error handling is
unchanged. The shared page presenter serves successful and rejected searches.

The new authored `ime_chinese_dictionary_search.json` reproduces **eight failed
assertions before the fix** and passes **22/22 after it** in both interpreter
and JIT. It covers invalid Chinese/punctuation after paging, disabled paging,
Enter recovery, valid misses and empty-query recovery. Existing dictionary
editing adds 25 passing assertions per tier; the exact reported sequence on the
real combined pack adds three, for **50/50 per tier**. The empty-result capture
was inspected. Evidence: `temp/ime_chinese/search_reset/before.json` and
`results.json`. Document-block and diff checks pass; this page-only fix requires
no native rebuild or new broad engine qualification.

### 8.15 Search dictionary entries by Chinese word or pinyin

**USER, 2026-10-26:** the dialog now accepts either Chinese word substrings
(`北京`, `京`) or the existing toneless pinyin prefixes (`bei jing`). The
label and placeholder describe both. Word matches retain distinct readings,
use the active script's actual entries, and participate in the same pagination,
add/delete and rejected-search reset behavior. Mixed Chinese/pinyin or other
unsupported input still reports a validation error (S17.8.1).

The demo model scans native-decoded lines for a new word query, then reuses the
shared posting/page walker to filter only matching words. Its cursor retains
matching line positions for later pages; pinyin keeps indexed key lookup.
Search reads the current in-memory bytes, so edits are visible immediately.
Acquisition and pack formats are unchanged (IME6, D7.2.4v4/S12.1.1v3).

Both interpreter and JIT pass **21 IME goldens**, including the expanded
45-check dictionary model: whole-word/substrings, multiple readings, pagination,
missing words, current edits, Traditional and supplementary-plane Han. The
registered search fixture passes 29 UI assertions and the existing editing
fixture passes 25. A full-pack replay adds 40 assertions for word/pinyin
switching, `da` → `大`, page navigation and substring-only results in both
scripts: **94/94 UI assertions per tier**. Native captures were inspected.
A single release-host smoke pass measured 48–116 ms for initial word searches
and 0.17–3.47 ms for subsequent nonempty pages; these are diagnostic samples,
not repeated benchmark qualification. Evidence:
`temp/ime_chinese/word_search/validation.json`, `results.json` and `timing.json`.
Document-block and diff checks pass. No runtime/native source or build changed.
