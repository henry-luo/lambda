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
