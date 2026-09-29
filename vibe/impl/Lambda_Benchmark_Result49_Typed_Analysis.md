# Result49: typed Lambda versus native C2MIR

Analysis date: 2026-09-29. Source inspected: `ebd7ef64e`.
Status: analysis plus bounded implementations in all five proposed areas:
typed text, private-array growth, native-loop facts, typed-path reuse across
adjacent operations and selected pure calls, and local/direct-call aggregate
scalarization. General loop relations, arbitrary call effects, and general
interprocedural aggregate-lifetime analysis remain open.
No semantics ruling changed.

Timing source: [Result49 JSON](../../test/benchmark/benchmark_results_v49.json),
measured 2026-09-24 on Darwin arm64, release commit `537a867839`, three samples
per cell. This analysis uses **Part 1, forced-JIT workload execution**, excluding
startup and compilation. Ratios below are recomputed from unrounded medians.
Derived rows, samples and aggregates are in
`temp/result49_typed_analysis/metrics.json`. The initial analysis took no
fresh CPU profile; earlier diagnostic evidence is labelled historical. The
implementation measurements and their source-identical controls appear below.

## 1. What the results establish

Typed Lambda/C2MIR is **3.1615x geometric mean across all 63 rows**. Typed
Lambda is **1.80x faster than untyped Lambda** on the same population.
There are seven rows faster than C2MIR, 14 more below 2x, 21 at 2–5x,
11 at 5–10x, nine at 10–20x, and one above 20x.

| Suite | Rows | Typed / C2MIR geomean |
|---|---:|---:|
| R7RS | 10 | 1.47x |
| Larceny | 11 | 1.95x |
| BENG | 8 | 2.32x |
| Kostya | 7 | 3.22x |
| AWFY | 14 | 5.04x |
| Text | 7 | 5.25x |
| JetStream | 6 | 7.54x |

The scalar lane is already competitive: `sum` 0.99x, `sumfp` 0.81x,
`ack` 0.91x, `diviter` 0.99x and `matmul` 0.84x. `mandelbrot` is 1.29x
and `collatz` 1.22x. `binarytrees` is 0.77x and `gcbench` 1.06x, so a
universal claim that Lambda allocation/GC is intrinsically an order of
magnitude slower is unsupported.

C2MIR here compiles **independent C ports**, not Lambda source. The retired
Lambda C-text backend stays removed (**D1.6**). The common MIR backend makes
the extra operations, representation and runtime protocols the first place
to investigate; it does not establish identical ownership, algorithms,
storage widths or allocation work. C2MIR is a useful reference, not a strict
semantic lower bound or proof that the backend contributes zero to a gap.

The historical 63-row typed/C ratios were R42 6.53x, R47 3.59x, R48 3.23x
and R49 3.16x. This is descriptive progress, **not a source-identical engine
speedup**: benchmark ports, runtime correctness and host conditions changed.

## 2. Choose both ratio and elapsed-time priorities

| Workload | Typed ms | C2MIR ms | Ratio | Primary investigation |
|---|---:|---:|---:|---|
| microdiff | 58.410 | 2.625 | 22.25x | Dynamic traversal, path/result construction |
| splay | 307.418 | 18.855 | 16.30x | Snapshot copies, tree construction and mutation |
| havlak | 27.438 | 1.861 | 14.74x | Typed nested access, remaining graph/runtime work |
| cube3d | 7.184 | 0.532 | 13.50x | Short-lived vector/matrix results and calls |
| prettier_ast | 536.776 | 41.465 | 12.95x | Document construction, array accumulators, dynamic input |
| deltablue | 14.359 | 1.156 | 12.42x | Handles, nested borrows, checked operations |
| richards | 337.512 | 29.990 | 11.25x | Repeated typed paths and call boundaries |
| puzzle | 13.944 | 1.277 | 10.92x | Plain-parameter snapshot cost versus C mutation |
| cd | 159.543 | 15.018 | 10.62x | Nested container updates and construction |
| base64 | 5.705 | 0.567 | 10.06x | Residual character and append work |
| hashmap | 27.813 | 2.831 | 9.82x | Remaining child-borrow/access boundaries |
| crypto_sha1 | 25.770 | 2.674 | 9.64x | Native-domain proof through bitwise/call chains |

Ranking by time above C2MIR gives a different order:

| Workload | Excess ms | Share of summed positive excess |
|---|---:|---:|
| log_pipeline | 1,404.185 | 30.9% |
| text_search | 1,219.315 | 26.8% |
| prettier_ast | 495.311 | 10.9% |
| richards | 307.522 | 6.8% |
| splay | 288.563 | 6.3% |

Those five account for **81.7%** of positive excess; the first two account
for **57.7%**. Summed typed medians are 8.727 seconds, versus 4.190 seconds
for C2MIR; text accounts for 76.2% of typed time. These sums describe one
instance of this benchmark mix, not an application throughput distribution.
`three_way_merge` is the longest typed row at 2.205 seconds but already
1.032x C2MIR: it is a weak priority for *closing this comparison*.

Typed is slower than untyped by more than 10% on `nqueens`, `bounce`,
`fannkuch`, `fasta`, `microdiff` and `text_search`. The latter is substantial:
1,755 ms typed versus 1,479 ms untyped. All three typed samples exceed all
three untyped samples. This merits matched-source investigation; it does
not prove annotations alone cause the difference. Microdiff also has a
136.8 ms typed outlier around its 58.4 ms median. Three samples are a screen,
not a controlled regression attribution.

## 3. Bottlenecks supported by the sources

### 3.1 A typed signature does not make the whole program static

[microdiff2.ls:4](../../test/benchmark/text/microdiff2.ls) declares results
as open `map` and traverses `any`/open arrays. `diff_map`, `diff_array` and
`diff_value` create paths and concatenate result arrays at every recursive
level. The C port uses `DiffResult` with fixed-capacity change/path storage
([microdiff.c:51](../../test/benchmark/text/c2mir/microdiff.c)). It performs
far fewer general-purpose allocation and collection operations.

Prettier's external JSON AST remains dynamic at `print_node(node: any)`.
Its internal `Doc` is now a **single fixed record with an integer kind**, so
the old diagnosis of six-way Doc-union admission is obsolete for Result49.
It still builds a document tree, uses growing array accumulators and performs
five replacement passes in `json_quote`.

### 3.2 Array construction can retain superlinear copying after typing

`join_docs_at` and `print_nodes_at` append with `acc ++ [...]` before
recursing ([prettier_ast2.ls:59](../../test/benchmark/text/prettier_ast2.ls)).
`fn_join_sequences` allocates destination storage and copies both operands;
the generic/native-lane fallback also constructs a fresh result
([lambda-eval.cpp:546](../../lambda/runtime/lambda-eval.cpp)). Growing a
list one item at a time therefore copies O(n²) accumulated elements per list.
Microdiff has a related repeated-result-concatenation pattern.

`array_concat_inherit_cert` **already preserves eligible typed-array
certificates**. Removing repeated admission and removing repeated prefix
copying are different optimizations. The latter remains a concrete source
of work, but its fraction of the complete formatter/diff timing needs a
current copied-element/allocation census.

### 3.3 Per-character operations still cross native helper boundaries

Log Pipeline has already eliminated per-line record construction. Its hot
`process_logs` scans the same strings character by character; `decimal_span`
computes `ord(line[index])` for every digit
([log_pipeline2.ls:79](../../test/benchmark/text/log_pipeline2.ls)).

`emit_ascii_char_literal_compare` still emits `fn_string_char_eq_ascii`
([transpile-mir.cpp:13503](../../lambda/runtime/transpile-mir.cpp)); the
helper rechecks the string, ASCII flag and index, with a UTF-8 fallback.
The `SYSFUNC_ORD` arm first lowers its string argument, then calls
`fn_ord_str` and converts its absent-result sentinel
([transpile-mir.cpp:28386](../../lambda/runtime/transpile-mir.cpp)). It does
not fuse indexed-character production with ordinal consumption. The C port
reads bytes directly inside the scan. This is a specific remaining boundary
cost, not evidence that ASCII indexing allocates a string every time:
existing ASCII characters are interned.

### 3.4 Native loops retain semantic checks beyond element types

Text Search already produces `int[]` at `to_codes`; adding that annotation
again cannot recover the earlier untyped speedup. The relevant loop relation
is `position <= n - m`, `offset < m`, then `text[position + offset]`
([text_search2.ls:34](../../test/benchmark/text/text_search2.ls)). C uses
32-bit integers, raw array loads and stack work tables. Lambda's `int`
includes poison values, and invalid reads yield null (**S4.1.1–S4.1.2,
S7.1.1v3**). Array typing alone proves neither that relation nor finite
element values (**D3.3.4**).

Historical Tune31 samples put roughly 83% of typed Text Search in generated
code, with little type-check time. That supports native-loop investigation,
not a fresh Result49 attribution to bounds checks or spills. Its attempted
simple bound elimination barely helped; a useful next experiment must cover
the combined relational bounds, numeric-domain checks and generated loop.

### 3.5 Container access and ownership are separate costs

Richards uses a single `World` with typed task/packet stores and an open
heterogeneous `datas` store, addressed through integer handles
([richards2_core.ls:46](../../lambda/benchmark/richards2_core.ls)). Its repeated
`w.tasks[id].field` and `w.pkts[id].field` accesses need valid index, current
carrier, field-layout and ownership facts, not just a parameter annotation.

Direct typed paths **already exist**: `mir_emit_typed_path_store`,
`mir_emit_cow_typed_map_field_borrow` and key-span fallback setters. The
single-field borrow specialization requires a trusted fixed shape and calls
a helper that rechecks carrier identity. The opportunity is to retain valid
facts across several accesses/calls, and extend eligible coverage, rather
than implement direct access from scratch. **D4.4.4v4** requires invalidation
on conflicting writes/sharing and reloading storage after relevant calls.

Splay is different: rotations bind child values and reattach them while
other owners are reachable. Prior diagnostics establish heavy real copying,
not just cheap unique-owner tests. Its C port mutates pointers with a
different rotation organization. Puzzle similarly snapshots plain array
parameters where C mutates and restores the same arrays. **S9.1.2–S9.1.3**
forbid deleting observable snapshots to obtain C timings.

### 3.6 Small aggregate lifetime is a missing proof, not missing packed arrays

Cube3d already uses packed `float[]`. `mat4_mul`, `vmulti`, `vmulti2` and
`calc_normal` return 3/4/16-element arrays
([cube3d2.ls:16](../../test/benchmark/jetstream/cube3d2.ls)); C commonly uses
caller-owned buffers. These returned values cannot simply share one scratch
buffer: subsequent calls and retained results make their lifetimes observable.

The compiler already has bounded leaf inlining and scalar-record destination
plans. `mir_inline_callee_ok` rejects aggregate-result functions, while
`mir_record_result_plan` covers a narrow fixed-record class, not general
numeric-array results ([transpile-mir.cpp:27599](../../lambda/runtime/transpile-mir.cpp),
`:4425`). That is a concrete limit for interprocedural scalar replacement.

## 4. Substantial improvement proposals

These are hypotheses with mechanism gates, not promised speedups. Reuse
the shared analysis/emitter and existing storage helpers; carry proofs in
the existing contract/representation machinery (**D2.4.1–D2.4.3,
D8.2.5v3–D8.2.6**).

| Priority | Proposed change | First pilots | Evidence required before expansion |
|---|---|---|---|
| 1 | Eliminate repeated array-prefix materialization for private append accumulators | prettier_ast, microdiff | Copied-element growth becomes linear in an isolated eligible chain; allocations fall; unchanged outputs and snapshots |
| 2 | Lower an entire proven ASCII scan to byte operations; fuse `ord(s[i])` | log_pipeline, brainfuck, base64 | No per-character runtime calls on the guarded path; UTF-8/absent fallback retained; kernel speedup |
| 3 | Carry relational bounds and finite-value facts through native loop regions and small calls | text_search, fast_diff, crypto_sha1 | Fewer executed checks/instructions in the actual hot loop, without more spills or code-size regressions |
| 4 | Reuse typed path proofs over a valid region, including call effect summaries | richards, havlak, deltablue, hashmap, cd | Fewer executed navigation/admission/borrow calls with unchanged necessary detach events |
| 5 | Extend aggregate destination/scalar replacement only across proved producer-consumer lifetimes | cube3d, formatter helper records | Fewer actual temporary allocations; escaping results still materialize and remain rooted |

**1: remove work, not just boundary names.** Recognize an append accumulator
whose previous versions are not observed, then use the existing growable
array storage/admission machinery and publish the final value. Across a
tail-recursive call, first prove the caller/closures/containers cannot retain
the old accumulator. Otherwise retain ordinary immutable concatenation.
Typing and a clear local shared bit alone are insufficient. Any broader
consuming-call convention needs its own design review under **D4.4.4v4–D4.4.6**;
this analysis does not ratify one. A separate source-level builder experiment
can measure the opportunity, but its gain is not an engine gain.

**2: amortize guards across the scan.** Check the actual string's ASCII
state and usable extent before entering a fast scan, then reuse the pointer,
length and proven index relations. Preserve original UTF-8 code-point and
null behavior on the fallback (**S7.1.1v3**). Fuse the producer/consumer
without materializing an intermediate character value. This is distinct
from the already implemented character-pair comparator. Reuse the existing
string builder's capacity-fit path for appends; do not add another builder.

**3: prove complete operations.** For Text Search, prove nested-index
relations, stable lengths, counter ranges, and element domains separately.
If needed, use a bounded guarded loop variant; no assumption that `int[]`
contains bytes. Any guard must run before affected writes, with a fallback
that neither replays effects nor alters failure order. Compare typed and
untyped generated paths to explain the current 19% reversal. A compiler
fact is useful only if lowering consumes it and native code gets cheaper.

**4: make typed paths cheaper across a region.** Extend the existing place
plan with read/write/retain effects and current-carrier provenance. Avoid
re-navigating a stable owner/path for adjacent accesses, and inline eligible
small consumers so the same facts reach their operations. Reload/recheck
after invalidating mutation or allocation; transport replacement homes on
`var` calls. Use static guards and the boxed fallback, never mutable inline
caches (**D8.3.1v2–D8.3.3, D8.4.1v2**).

**5: lifetime analysis before storage substitution.** Begin with a bounded
producer-consumer chain whose result is consumed locally after inlining;
scalarize its fields/elements or construct directly into a proved destination.
Existing scalar-record/destination plans are the starting point. Array results
that escape or remain live across reuse still need independent storage.
Do not globally reuse Cube3d scratch arrays or replace every plain parameter
with `var` (**D4.1.4v4, D5.2, S9.1.2–S9.1.3**).

For copy-heavy graphs, a larger gain may require a separately designed
ownership/escape analysis or an explicit source representation experiment
using the existing handle-store idiom. First attribute which copies are
required; improve their construction/access cost without suppressing live
observers. Havlak is already flat in Result49. It is not evidence for
proposing the same flattening again.

Root traffic amplifies helper costs, but it is not independently optional.
Use the existing dirty-live-root protocol and mechanically proven `NO_GC`
effects (**D5.3.1–D5.3.4**). Eliminating a boundary can eliminate its root
traffic; classifying an allocating path as `NO_GC` cannot. No conservative
native-stack scanning or vendor changes are proposed (**D1.5v2, D1.6**).

## 5. Lessons that prevent repeating completed or failed work

[Tune31 §9.10–9.11](Lambda_Impl_Tune31.md) records fixed-source engine wins
of 0.693x time for Base64 fixed-arity append, 0.721x for Hashmap typed field
borrow, and 0.692x for an archived string-based Fast Diff comparator. These
mechanisms shipped. Its full typed-suite gain was only **2.17%**, missing
the 10% objective.

The same record rejected certificate-publication changes at 1.020x,
fill/admission helper fusion at 1.015x, and simple loop-bound elimination
at 0.990x Text Search / 1.030x Fast Diff. These small trials are not universal
proofs against certificates, fusion or bounds analysis; they show that fewer
MIR calls/checks alone is not an adequate performance argument.

The [September 22 follow-up in Tune27 §10.11](<Lambda_Impl_Tune27 (done).md>)
subsequently added inline exact-record admission, key-span paths, geometric
typed boxed-array growth, concat certificate inheritance, and source changes
to Havlak/Prettier/Deltablue. In particular, historical Havlak counts of
65,876 map and 44,999 array copies, and Prettier's 6.6 million union
admissions, **must not be presented as Result49 counts**.

## 6. Measurement and realistic targets

Start with fresh release profiles of the five highest excess-time rows,
plus Microdiff/Cube3d for structural mechanisms. Record exact binaries,
transitive source/data hashes, forced-JIT tier and outputs. Collect CPU
samples and allocation/copy/helper counters separately from timing runs.
For Richards, capture the imported core, not just its thin entry module.

For each candidate, use alternating fixed-source release A/B pairs with
raw samples and uncertainty, plus an A/A stability control if short rows
move. Count executed operations or copied bytes; inspect native instructions
before attributing a gap to spills. Validate semantic fallbacks, alias and
snapshot cases, errors, and forced precise GC. Then run the Lambda baseline,
relevant MIR/optimization gates, all 63 typed rows and untyped guards.
Measure auto/end-to-end separately. Source-port improvements get a separate
comparison on one immutable binary.

A first broad objective of **3.16x → 2.5x C2MIR geomean** means roughly
21% less geometric-mean execution time with the references held fixed;
2.0x requires about 37% less. These are planning targets, not forecasts.
Halving ten of the 63 rows only reaches 2.83x; halving twenty reaches 2.54x.
This explains why isolated 30% wins cannot deliver a dramatic suite change.

Halving just Log Pipeline and Text Search would reduce the sum of typed
medians by 21.7% (about 1.28x speedup for this mix), yet move the suite
geomean only from 3.16x to 3.09x. Report both views. The highest-value program
is therefore a combination of high-time text-loop work and broadly reused
construction/path optimizations, with ownership-heavy graph gains treated
as a separate, proof-intensive track.

## 7. Implemented: typed indexed-character fast path

MIR Direct now fuses `ord(s[i])` when the String carrier, integer index and
error-free source/index expressions are proved. Its ASCII arm checks the
actual String flag and byte extent, loads the byte directly, and boxes that
known finite value. A shared byte emitter also replaces the per-character
helper on eligible `s[i] == "x"` and `a[i] == b[j]` comparisons. Unicode, absent indices and
non-String defensive cases use the existing behavior through fallbacks.
`fn_string_ord_at` decodes Unicode directly without materializing a character
String. The source remains rooted across index evaluation, which may allocate.
This follows **S7.1.1v3**, **S4.1.1–S4.1.2**, **D2.8.3** and
**D5.3.1–D5.3.4**; it does not change the language's indexing or `ord` result.

In release, seven source-identical alternating pairs gave Log Pipeline
0.9532x candidate/control for the fused helper alone (6/7 wins), then
0.6778x inline/helper for the guarded byte path (7/7 wins). Against the
original release binary, Brainfuck was 0.7920x (7/7), Base64 0.9840x
(5/7), and Text Search 0.9842x (7/7). These are row-level results, not a
recomputed 63-row geomean. The clean final release versus the original was
0.6495x on Log Pipeline in nine direct pairs (9/9 wins, equal output). A
three-pair sweep of all 63 typed rows gave a candidate/control geometric
mean of 0.9868x. That short sweep is a broad regression screen, not a
high-confidence estimate for its millisecond-scale rows. Raw samples,
source hashes and binary hashes are
in `temp/result49_typed_impl/paired_ord_log.json`,
`paired_inline_log.json`, `paired_inline_other.json`,
`paired_final_log.json` and `paired_final_typed_suite.json`. The 173-case Lambda
MIR emission corpus passes, including the new Unicode/absence fixture, as
does its forced-GC run. The static GC effects audit still fails on pre-existing
JS paths; it reports no path from the new `fn_string_ord_at` helper after
following its scalar boxing routine.

The two-string character extension gives typed Levenshtein 0.6280x time
relative to the indexed-ordinal release in nine direct pairs (9/9 wins, equal
output); its raw record is `temp/result49_typed_impl/paired_char_pair_levenshtein.json`.
In the 63-row typed sweep against that release, the extension measured
0.9876x geometric-mean time, including Knucleotide at 0.7535x (three
pairs per row, all output hashes equal). A direct five-pair A/B of all 63
typed rows against the original release measured 0.9742x geometric-mean
time, with all rows valid and equal output hashes. Levenshtein was 0.6321x,
Log Pipeline 0.6568x, Knucleotide 0.7168x and Brainfuck 0.8016x. Holding
the Result49 C2MIR medians fixed, the 3.1615x baseline gap would become
about 3.08x. Raw data are in
`temp/result49_typed_impl/paired_final_direct_suite.json`.
It shares the ASCII byte emitter and the original Unicode/absence fallback,
and roots the left source across evaluation of the right. Prettier's fused
`ord(formatted[index])` grows the module MIR ratchet by three instructions,
from 17,305 to 17,308; `test/mir/mir_budgets.json` records this reviewed
increase. The first Lambda baseline run passed 6,004/6,005 tests, with only
that ratchet failing before the budget adjustment. The rerun passes all
6,005 baseline cases, including 173 MIR emission and 224 forced-GC cases.

Fresh `COW_EXEC_PROFILE` runs on the pair-candidate release separate
snapshot work from other container costs. Splay records 951,168 Map shared
copies (61.8 MB by the one-level estimator), versus only 1,000 Map copies
in Richards. Prettier records 1,325,056 map admission calls and 768,258
string append calls; Microdiff records 20,528 Array share marks but zero
COW array copies. These counters do **not** count `fn_join_sequences`' prefix
copies, so Microdiff's zero COW-copy count does not refute the O(n²) concat
mechanism. Artifacts are `temp/result49_typed_impl/cow_{microdiff,prettier,richards,splay}.tsv`.
This evidence keeps snapshot removal constrained by **S9.1.2–S9.1.3** and
points Richards toward the path-fact work under **D4.4.4v4**.

## 8. Continued implementation and gates

The byte emitter now omits the repeated tag/null checks only for a direct
binding with a checked non-null `string` contract. Dynamic, nullable, and
computed sources retain both guards and the Unicode/absent fallback
(**S7.1.1v3, D3.3.3v3**). Nine source-identical release pairs against the
preceding candidate measured 0.9479x Log Pipeline and 0.9565x Levenshtein,
with matching output. A three-pair 63-row screen measured 0.9956x geometric
mean time and equal output on every row; short rows remain noisy. Samples are
in `temp/result49_typed_impl/paired_string_contract.json` and
`paired_string_contract_suite.json`.

A private local `array` self-rebinding may now consume its old open Array
after evaluating the RHS. The runtime detaches shared snapshots before
appending, reserves geometrically, and copies wide scalar items into owned
storage. The compiler keeps the returned value's ownership fact so a later
alias binding marks the old header; dropping that fact initially made a
captured snapshot observe a later append, and the new fixture caught it.
Selection requires an owned local and a RHS with no indirect reference to
that binding (or the direct self-RHS handled by detachment). Lists, views,
native lanes, representation certificates, and attribute-bearing arrays use
the existing join. This follows **S9.1.2, S10.6.1, D4.4.4v4** and is not yet
the proposed cross-call accumulator ownership proof.

Nine release pairs measured 0.9867x Microdiff and 1.0043x Prettier against
the text-only candidate, both with equal output. The three-pair 63-row
screen measured 0.9979x geometric mean with all outputs equal. The typed
`Doc[]` tail-recursive accumulator remains on the fallback, explaining why
this safe local step does not substantially improve Prettier. Artifacts are
`paired_array_microdiff_prettier.json` and `paired_array_typed_suite.json`.
The full Lambda baseline passed 6,007/6,007 cases after the array change,
including 174 MIR emission and 225 forced-GC cases. The static native-root
hazard audit passed as well. The later non-null String guard passed another
full 6,007/6,007 baseline sweep, including forced GC.

A general typed `int[]` paired-index runtime leaf was trialled for Text
Search and removed. It matched only two checked comparison sites, not the
hot `position + offset` relation; nine release pairs measured 1.2980x time
(0/9 wins, equal output). Its call/root overhead increased work while the
loop's defect and bound checks remained. The raw artifact is
`paired_int_pair_text_search.json`. A useful native-loop change therefore
needs the relational index proof and a cheaper generated loop, not another
call boundary (**S4.1.1–S4.1.2, S7.1.3v2, D3.3.3v3**).

The current retained release, with the text paths, private open-array append,
and non-null String guard, measured 0.9671x geometric-mean time against the
original release over five alternating pairs of all 63 typed rows. Every row
had valid, matching output. Levenshtein was 0.6002x, Log Pipeline 0.6557x,
and Knucleotide 0.6673x. Holding Result49 C2MIR medians fixed, this moves
the aggregate comparison from 3.1615x to about 3.06x. The full raw record is
`temp/result49_typed_impl/paired_current_direct_suite.json`. The remaining
gap needs broader loop, path, and aggregate work; these local changes do not
meet the 2.5x planning target.

A MIR inline fast arm for the existing one-field typed COW borrow was also
trialled and removed. Nine release pairs measured 1.0045x Richards, 1.0235x
Hashmap, and 1.0094x Deltablue, all output-equal. Richards' hot
`w.tasks[id].field` writes already lower through the direct typed-path store,
so this borrow path did not remove their navigation; the extra guards made
Hashmap slower. Raw data are in `paired_typed_borrow.json`. Further path work
must target repeated direct-store/read path facts and their invalidation,
rather than inlining an unrelated borrow (**D4.4.4v4**).

The shared Map/Element copy path now allocates its packed data without a
redundant zero-fill: the clone immediately overwrites all `data_cap` bytes
with `memcpy` before publishing or collecting. This removes one full memory
write per COW copy while preserving the allocation contract (**D4.3.1,
D5.3.1–D5.3.4**). Nine release pairs measured Splay 0.9881x (9/9 wins,
equal output) and Richards 1.0049x. A three-pair 63-row screen measured
1.0004x geometric mean with all outputs equal, so this is a specific Splay
allocation improvement rather than a suite-wide gain. The 225-case forced-GC
MIR sweep passed. Raw measurements are `paired_map_clone.json` and
`paired_map_clone_suite.json`.

A rank-one certified pointer-lane `T[]` join now builds its result in that
lane directly. The old native-lane fallback boxed the entire left prefix into
a generic `Array`, then the next typed boundary rebuilt it as a pointer lane.
The direct path checks the left certificate, proves the right elements (or
accepts its same exact certificate), reserves the target once, and retains the
certificate only while its representation and fixed-length rules still hold.
In nine fixed-source release pairs, Prettier measured 0.8944x time (9/9 wins,
equal output) against the map-clone candidate. The three-pair 63-row typed
screen measured 0.9983x geometric mean with all rows valid and output-equal;
the Prettier row was 0.8932x. Artifacts are
`paired_native_lane_join_final.json` and `paired_native_lane_join_suite.json`.
This removes a repeated representation round-trip, but the fresh result still
copies the prefix and the recursive accumulator remains O(n²) in its length.

A new typed-array mutation probe exposed a pre-existing ownership error in
both tiers: `let extended: T[] = base ++ items; var changed: T[] = extended;
changed[0].field = 9` also changed `extended`. The join is a fresh sequence
when both operands are non-null arrays, but a `T[]` AST type is wrapped as
`LMD_TYPE_TYPE`; the old binding classifier recognized neither the join's
fresh ownership nor the wrapper as a possible container. The shared AST
classifier now recognizes this exact fresh-join shape and typed-array
contracts, so both tiers mark the later alias before mutation. The regression
fixture checks that `base` and `extended` remain unchanged under JIT and T0,
as required by **S9.1.2–S9.1.3** and **D3.3.3v3**. The native root audit passes.

The compiler now recognizes a bounded pure tail-recursive `T[]` accumulator:
its final parameter has a rank-one certified pointer lane; exactly one tail
branch appends it either directly in the recursive argument or through one
local `let` whose conditional branches both append; no other recursive
argument or append RHS reads the accumulator. The incoming parameter is marked
shared once, so the first consuming join detaches any caller snapshot. Later
backedges reuse the private lane with geometric growth, while incompatible
operands fall back before a visible write. The generic typed argument boundary
still checks the backedge, and the boxed non-TCO lane retains ordinary joins.
This is the bounded ownership proof called for by **D4.4.4v4** and
**S9.1.2–S9.1.3**, rather than a new consuming convention for arbitrary calls.

The new `result49_typed_tail_accumulator` fixture checks both structural
forms, the untouched initial array, and a later alias mutation; its MIR check
requires `fn_join_consume_cert_array` in both native bodies. An isolated
96-chain × 384-append release pilot recorded 36,768 unique Array mutations
and 96 shared copies (one initial detach per chain). Nine source-identical
release pairs measured 0.6360x time (8/9 wins, equal output) against the
previous candidate. Prettier measured 0.9906x in nine pairs (9/9), suggesting
its short accumulator chains leave other document and string work dominant.
The three-pair 63-row typed screen measured 0.9951x geomean with every row
valid and output-equal. Raw records are `paired_tail_array_scale.json`,
`tail_array_scale_cow.tsv`, `paired_tail_accumulator.json`, and
`paired_tail_accumulator_suite.json` under `temp/result49_typed_impl/`.

A guarded comparison now handles two reads from certified `int[]` bindings
whose indices are pure native-int expressions. It checks each index against
the cached length, loads the native lane directly, and uses raw equality when
both values are in the ordinary int band. The original lowering handles
absent reads, overflow/poison values, and other carriers, preserving
**S4.1.2, S5.1.2, S7.1.1v3, and D3.3.3v3**. This eliminates the repeated
read-null and numeric-conversion branches in Text Search's inner comparison;
it does not yet prove the enclosing `position + offset` relation across the
whole loop. The `result49_int_array_pair_compare` fixture covers equal,
unequal, and absent pairs. Against the previous release, nine source-identical
Text Search pairs measured 0.9062x (9/9 wins); the 63-row typed screen
measured 0.9941x geomean in three pairs, with all outputs equal. Fast Diff
measured 0.9495x in that screen. The records are
`paired_int_array_pair_text_search.json` and
`paired_int_array_pair_suite.json` under `temp/result49_typed_impl/`.
The full Lambda/input baseline passes 6011/6011, including 227 forced-GC
MIR cases and the new edge-case and emission fixtures; the static native-root
audit also passes.

A broader read-only place-handle planner for declared record arrays and
non-writing record roots was trialled and removed. It reused navigation for
stable `nodes[i].field` sequences and invalidated the handle at a write, but
nine release pairs measured Richards 1.0225x and Havlak 1.0062x time
(both output-equal); CD and Hashmap were flat. This suggests that merely
expanding handle coverage adds bind/Item overhead where the existing direct
path was already cheap. The measured artifact is `paired_record_array_handle.json`.
Further path work must remove repeated checks in a measured hot region while
preserving **D4.4.4v4** invalidation and **D5.2** rooting.

A bounded scalar replacement now handles a private `float[]` literal of one
to four values when its immutable local binding has only fixed, in-range
index reads in the same function. The emitter evaluates each admissible,
defect-free element once into a float lane and substitutes those lanes at
the reads. Identity never escapes, so no array allocation or materialization
is needed; escape and absent-index cases take the ordinary path. This follows
**S9.1.2, D5.2, and D8.2.5v3**. The proof counts the literal's linked items:
its AST `length` field is optional metadata and can be zero for a populated
literal. The `result49_small_array_scalar` fixture checks both paths and MIR
emission. A 120,000-call isolated release pilot ran at 0.3110x the previous
candidate's time in 9/9 pairs, with equal output. Cube3d remained at 1.0028x
in nine pairs: its hot arrays have more than four elements or escape across
calls, and its release COW profile reported 116,760 direct numeric stores with
zero shared copies. The 63-row typed screen was 1.0018x geomean, with all
rows valid and output-equal. One three-pair Knucleotide outlier (1.2428x)
resolved to 1.0000x in a nine-pair rerun. Artifacts are
`paired_small_array_scale_firing.json`, `paired_small_array_cube3d_firing.json`,
`paired_small_array_suite.json`, `paired_small_array_knucleotide.json`, and
`cube3d_cow.tsv` under `temp/result49_typed_impl/`.
The full Lambda/input baseline passes 6013/6013, including 228 forced-GC
MIR cases and 177 MIR emission fixtures. The native-root audit checked 17,188
migrated functions without finding automatic-local roots or transient guards.

A first whole-scan ASCII proof now recognizes a `while (index < stop)` loop
with one `index = index + 1` update, an unchanged typed String source and
bound, and only `ord(source[index])` calls in its straight-line body. A single
entry guard establishes the actual String carrier, ASCII flag, nonnegative
counter, and `counter <= stop <= byte length` within the compact-int band.
The fast copy retains the rooted non-moving String pointer and reads bytes
without per-character tag, ASCII, or extent checks; the unchanged generic
copy handles UTF-8, negative and absent indices, poison, and failed guards.
The `ord` result is still boxed at its nullable `Item` boundary, as required
by **S7.1.1v3, D5.3.1, and D8.3.1v2**. The new
`result49_ascii_scan` fixture covers those fallback cases and a wide numeric
fold that can collect during the fast loop. Its focused MIR emission and
forced-GC checks pass. Nine source-identical release pairs measured Log
Pipeline at 0.9873x (5/9 wins, equal output) and an isolated decimal-scan
pilot at 0.9738x (8/9 wins, equal output); the three-pair 63-row typed screen
was 0.9969x geomean with all rows valid and output-equal. A nine-pair
Knucleotide rerun measured 1.0276x on a roughly 0.36 ms row, so the
cross-row aggregate should be read with that timer resolution in mind.
Artifacts are `paired_ascii_scan_log_pipeline.json`,
`paired_ascii_scan_scale.json`, `paired_ascii_scan_suite.json`, and
`paired_ascii_scan_knucleotide.json` under `temp/result49_typed_impl/`.
The remaining text-scan opportunity is the nested character-comparison loops
in `process_logs`; this first proof did not cover their compound conditions.

The same proof now also accepts a short-circuit condition of the form
`index < stop and source[index] ==/!= <ASCII character>` with the same single
counter update. It guards the unchanged source and bound once, then lets the
fast copy compare bytes directly in the condition. The generic copy retains
Unicode, negative-index, and past-end behavior under **S7.1.1v3**. The
`result49_ascii_scan` fixture now covers both equality and inequality scans,
Unicode, negative indices, a bound longer than the String, and a source
reassignment that must stay on the generic loop; its MIR check
shows the ASCII flag load at entry and in the generic copy, with only one
generic character helper call. Nine source-identical release Log Pipeline
pairs measured 0.7812x time (9/9 wins, equal output) relative to the ordinal
scan candidate. The 63-row three-pair screen was 1.0030x geomean, with all
rows valid and output-equal; its four >5% slow rows lasted 0.032–1.304 ms
and nine-pair reruns were 0.9778–1.0070x. These are timing resolution effects,
not evidence that unrelated loop bodies changed. The records are
`paired_ascii_char_scan_log_pipeline.json`,
`paired_ascii_char_scan_suite.json`, and
`paired_ascii_char_scan_outliers.json` under `temp/result49_typed_impl/`.
The ASCII scan candidate passed the full 6015/6015 Lambda/input baseline, including 229
forced-GC MIR cases and 178 MIR emission fixtures; the extended fixture's
focused emission and forced-GC cases also pass. The native-root audit checked
17,194 functions and found no automatic-local roots or transient guards.

### Adjacent packed-record stores

A forced-JIT Richards COW profile counted 1,346,050 unique map mutations but
only 1,000 shared map copies (64,900 bytes copied). Its typed `World` setters
write several boolean fields through `w.tasks[tid]` consecutively. The prior
`has_fixed_shape` gate rejected `TaskControlBlock`: boolean slots are packed
one byte apart, while that gate requires every field offset to be word-aligned.
The direct typed-path store also used an eight-byte write for boolean leaves,
so merely relaxing the gate would corrupt adjacent fields.

The terminal record now needs a trusted named layout, and only a proved
non-null, non-defective boolean may take the packed fast arm. That arm writes
one byte; other leaf types still require the aligned-shape proof. A MIR-local
batch recognizes adjacent literal-boolean stores to the same stable place,
binds one writing handle, and reuses it until the last store. The first bind
retains the COW, null, certificate and bounds guards, while each store keeps
the checked cold arm (**D4.4.4v4**, **D3.2.4v4**, **S9.3.1**). A changed index
or intervening statement ends the batch. The `result49_first_write_handle`
fixture checks one certificate proof and three byte stores, a changed index,
a COW snapshot, neighboring int fields and a null child on both tiers and
under forced GC. Its mixed bool/any/bool case exposed another invalidation:
the checked `any` sibling store may reify the terminal record layout without
changing the outer array certificate. Each packed leaf therefore checks the
terminal record's exact shape before its byte write and takes the checked
arm on a mismatch. This restores JIT/interpreter agreement after reification.

Nine source-identical release pairs measure Richards at 0.7725x time (9/9
wins, equal output). Brainfuck is 1.0095x and Deltablue is 0.9958x, both
with equal output. The 63-row three-pair screen has a 0.9917x geomean, with
all rows output-equal; four >5% slow rows reran at 0.9670–1.0000x over nine
pairs. An earlier AST-wide first-write plan achieved the Richards gain but
slowed Brainfuck to 1.50x despite identical emitted Brainfuck MIR; that trial
was removed in favor of the bounded MIR run. The Deltablue MIR image grew
67 instructions for its guarded fast path and terminal shape checks; its
reviewed ratchet budget is 9,859. The release-wide ratchet has unrelated
pre-existing default-profile mismatches; the Deltablue probe passes in
isolation, and the debug baseline ratchet passes 20/20. Artifacts are
`richards_cow_jit.tsv`,
`paired_mir_batch_guarded_richards.json`,
`paired_mir_batch_guarded_brainfuck.json`,
`paired_mir_batch_guarded_deltablue.json`,
`paired_mir_batch_guarded_suite.json`, and
`paired_mir_batch_guarded_outliers_{r7rs,kostya}.json` under
`temp/result49_typed_impl/`. The full Lambda/input baseline passes
6,017/6,017, including 230 forced-GC MIR cases and 179 MIR emission fixtures;
the native-root audit checks 17,198 functions cleanly.

For the next native-loop pass, a current nine-pair, source-paired run of
`text_search.ls` against `text_search2.ls` on the guarded release binary puts
typed at 1.0376x untyped time (0/9 typed wins, equal output), rather than the
1.19x typed/untyped ratio in Result49. The remaining proof work should target
actual inner-loop bounds and finite-value checks, with a current MIR/profile
comparison before adding another loop version. Artifact:
`text_search_typed_vs_untyped_current.json` under `temp/result49_typed_impl/`.

### Short-circuit length facts and non-null int-array equality

The MIR Text Search image showed `offset < len(pattern)` immediately followed
by `pattern[offset]`, yet the indexed read checked the same upper bound again.
A bounded loop fact now recognizes a zero-origin counter with one tail `+1`
update, a stable rank-one array length, and a read-only right arm of `and`.
That right arm and the loop body may read the locally cached, certified array
without another bounds branch (**S7.1.1v3**, **D2.5.3**). The proof is shared
by ordinary indexed reads and certified `int[]` pair comparisons. It does not
assert a 32-bit counter range: array length is stored as `int64_t`, so
arithmetic retains its poison/overflow path under **S4.1.2**. Negative starts
and side-effecting conditions stay checked.

Certified `int[]` pair equality also used to branch to generic numeric
comparison for every poison element. The existing non-null IntLane equality
rule handles those values with raw equality plus one nan test, so the pair
path now uses it directly; only an invalid index takes the generic fallback
(**S4.1.2**, **D2.6.2**). `result49_length_bound_read` checks equal prefixes,
an empty array, a shorter left array, nan elements and a negative start on
JIT and interpreter, including forced GC. Its MIR check expects one bounds
guard for the two-read prefix and none for the separately proved read.

Nine source-identical release Text Search pairs measure 0.9550x time (9/9
wins, equal output) against the guarded packed-record build. The 63-row
three-pair screen is 0.9989x geomean with all outputs equal; its three >5%
slow rows last 0.123–0.806 ms and rerun at 0.9603–1.0062x over nine pairs.
Artifacts: `paired_length_bound_pair_stable_text_search.json`,
`paired_length_bound_pair_stable_suite.json`, and
`paired_length_bound_pair_stable_outliers_{beng,larceny}.json` under
`temp/result49_typed_impl/`. The complete Lambda/input baseline passes
6,019/6,019, including 231 forced-GC MIR cases and 180 MIR emission fixtures;
the native-root audit checks 17,202 functions cleanly.
On this final release binary, nine source-paired Text Search runs put typed
at 0.9956x untyped time (equal output), down from 1.0376x before this loop
and pair work; see `text_search_typed_vs_untyped_stable.json`.

### Bounded direct-call array result scalarization

A fixed-read caller of a direct local `pn`/`fn` returning a one-to-four-member
`float[]` literal can now evaluate its pure arithmetic members into scalar
lanes at the call site. Every source read must be a constant index into a
distinct, caller-certified `float[]` argument, and the result binding may only
be read at fixed in-range indices in the same function. The caller checks the
required length of each source once, transfers its cached typed-array layout
to the callee parameter's temporary binding, and keeps the proven index facts
only for this read-only body. Escaping, rebound, or nonmatching results still
use the ordinary call and allocation. This extends the private-lifetime case
under **D5.2, D8.2.5v3, and S9.1.2** without claiming a general call-effect
summary.

If a source is too short, the fast body performs no read and calls the
original callee on the cold edge. Its `float[]` return boundary rejects the
absent member, preserving the original diagnostic under **S7.1.1v3** and
**S11.4.1v3**. `result49_small_array_call` checks successful and escaping
results, the short-input error, and a rebound `var` result on JIT and T0; its
MIR check confirms scalar arithmetic and no hot-path array allocation. The
focused forced-GC fixture, all 232 forced-GC MIR cases, 181 emission fixtures,
20 MIR ratchets, and the full Lambda/input baseline pass **6,021/6,021**.
The native-root audit checked 17,205 functions cleanly. Cube3d's reviewed
module budget rises by 20 instructions, with no `run_cube` budget growth.

Nine source-identical release pairs of a 120,000-call fixed-result pilot
measure **0.3821x** prior time (9/9 wins, equal output). Cube3d is flat:
1.0089x over nine pairs, and 0.9932x in the three-pair suite screen. The
63-row typed screen is 1.0030x geomean with all outputs equal. Its five
>5% slow rows run in 0.029–1.083 ms on the control; nine-pair reruns span
0.9605–1.0286x. Artifacts: `paired_small_array_call_cached_scale.json`,
`paired_small_array_call_cached_cube3d.json`,
`paired_small_array_call_cached_suite.json`, and
`paired_small_array_call_cached_outliers.json` under
`temp/result49_typed_impl/`. The remaining Cube3d cost is in its larger
matrix/vector results and stores, which this bounded scalar result does not
replace.

### Admitted array fields in direct record construction

The current Prettier release profile records 735,232 exact map admissions,
100,608 shared Array copies (7.24 MB), and 768,258 string appends. A `Doc`
literal such as `text_doc` also rechecked the same module-level `Doc[]`
binding at every `parts` field and took `map_with_type_tl` plus `map_fill`.
Its declaration had already established the invariant array contract, but
the literal's shape proof looked only at its open AST initializer type.

Named record literals may now use their direct field-store constructor when
an otherwise unproven field reads a binding admitted to the exact rank-one
pointer-array contract. The emitted store checks for the live Array carrier;
on a miss it runs the original field boundary and diagnostic before storing.
The static defect scan still treats that field as fallible. Capture happens
before the check and direct store, preserving **S9.3.1** and the binding's
invariant representation under **D3.3.3v3**. Dynamic arrays still take
`map_fill` and full admission. `result49_map_field_binding` checks all three
paths, an invalid dynamic value, MIR shape, T0/JIT parity and forced GC.

Nine source-identical release Prettier pairs measured 0.9906x time (7/9
wins, equal output). The three-pair 63-row typed screen was 1.0020x
geomean with all outputs equal; its two >5% slow rows lasted only 0.102 and
0.313 ms on the control. The new direct constructor removes `map_fill` from
`text_doc`, but the 735,232 map-admission count is unchanged: the larger
remaining cost is elsewhere, and this change alone is not a substantial
Prettier gain. The reviewed Prettier MIR module budget rises by 96 instructions.
The complete Lambda/input baseline passes **6,023/6,023**, including 233
forced-GC MIR cases and 182 emission fixtures; the native-root audit checks
17,207 functions cleanly. Raw records:
`prettier_current_cow.tsv`, `prettier_map_field_direct_cow.tsv`,
`paired_map_field_direct_prettier.json`, and
`paired_map_field_direct_suite.json` under `temp/result49_typed_impl/`.

### Small literal appends in private typed tail accumulators

The proved `T[]` tail accumulator still built `acc ++ [item]` as a temporary
generic Array, then admitted and consumed it. `join_docs_at` in Prettier also
uses `acc ++ [separator, part]` on its common branch. Both shapes now pass
their already evaluated Items directly to the same guarded consuming helper.
The emitter captures each item before evaluating the next, and the helper
validates the destination's rank-one pointer lane and every item before
detaching or writing. A list item, failed element contract, changed carrier,
or reserve failure reconstructs the source array literal and uses the
ordinary `++` path. This preserves list splicing, snapshots, and errors under
**S9.3.1, S9.1.2, S10.6.1, and D4.4.4v4**.

Against the preceding admitted-record release, nine paired runs of a
96 × 384 singleton-append pilot measured **0.5987x** time (9/9 wins); typed
Prettier measured **0.9838x** (7/9), both output-equal. Extending the same
helper to two ordinary items measured **0.5930x** on the corresponding pair
pilot (9/9) and **0.9937x** on Prettier (6/9) against the singleton release.
The final three-pair 63-row typed screen is **1.0039x** geometric-mean time
against the admitted-record release with valid, equal output on every row.
Its three >5% slow rows run in 0.117–0.359 ms on the control; nine-pair
reruns measure 1.0068–1.0261x. These short-row shifts do not show a
material broader regression, while the isolated accumulator now avoids the
repeated temporary-array allocation.

A three-item extension was trialled and removed. It measured 0.6915x time
on an isolated 96 × 384 append pilot (9/9 wins), but **1.0098x** on typed
Prettier (2/9 wins) against the two-item release. The shared helper and MIR
image grew for a rare branch, so the intended pilot offered no gain. The
retained emitter also rejects nested numeric literals from this direct path:
those literals lower to N-D numeric storage, not an ordinary Array. Records
are `paired_tail_triple_scale.json` and `paired_tail_triple_prettier.json`.

`result49_typed_tail_accumulator` covers direct, conditional, and two-item
recursive appends, caller snapshots, absent elements, and a list-splice
fallback on JIT and interpreter. Its MIR check requires the direct helpers
without array-literal construction. The full Lambda/input baseline passed
**6,023/6,023** on the final source, including 233 forced-GC cases,
182 emission fixtures, and 20 ratchets; the list fallback also matched
its expected output under forced GC in both tiers. The native-root audit
checked 17,211 functions cleanly. Raw records:
`paired_singleton_tail_scale.json`, `paired_singleton_tail_prettier.json`,
`paired_singleton_tail_typed_suite.json`, `paired_tail_pair_scale.json`,
`paired_tail_pair_prettier.json`, `paired_small_literal_tail_suite.json`, and
`paired_small_literal_tail_outliers.json` under `temp/result49_typed_impl/`.

### Fixed-source comparison through small-literal tail append

The retained release was paired against the archived original Result49
release on the same current source for all 63 typed rows, five alternating
pairs per row. Every row completed with matching output. Geometric-mean time
is **0.9548x** the original, and the sum of row medians falls from 7,382.5 ms
to 6,017.3 ms (**18.5%**). Log Pipeline measures 0.5011x, Levenshtein
0.5833x, Knucleotide 0.6643x, Richards 0.7573x, Prettier 0.8305x, and
Text Search 0.8624x. Three >5% slow screen rows last 0.029–0.312 ms on the
control; nine-pair reruns measure 0.9667–1.0303x. The exact record is
`temp/result49_typed_impl/paired_final_guard_vs_original.json`, with those
reruns in `paired_final_guard_outliers.json`.

Holding the Result49 native C2MIR medians fixed, the 3.1615x typed/C2MIR
geomean becomes about **3.02x**. This is meaningful elapsed-time progress on
the expensive text rows but does not meet the 2.5x planning target in §6.
The bounded implementations now cover private array consumption, whole-scan
ASCII/ordinal lowering, loop length/index facts, adjacent typed-record path
reuse, and scalar replacement across a small direct `float[]` call. They keep
the cold semantic paths and precise roots required by **S7.1.1v3, S9.1.2,
S9.3.1, D3.3.3v3, D4.4.4v4, and D5.2–D5.3.4**.

At that point the measured limits were concrete: Cube3d was 0.9926x original time
and still allocated its escaping 3/4/16-member numeric results; Microdiff was
0.9919x and its dynamic traversal dominated; Splay was 0.9827x with necessary
shared snapshots; CD was near flat, where broader path/call effect facts are
still needed. Any next implementation should prove the lifetime or region
before removing those operations, then measure executed work rather than
counting omitted MIR checks alone (**D4.4.4v4, D5.2, D8.3.1v2**).

### Fresh Array plus one item

Microdiff repeatedly computes `prefix ++ [index]` while retaining `prefix`
for sibling paths. Consuming the prefix would violate **S9.1.2**; the old
lowering also constructed a one-item RHS Array before allocating the fresh
joined Array. A `fn_join_fresh_array_item` path now copies a plain left Array
directly into the final owner and appends the captured item. It reserves room
for wide-scalar tail homes before that copy. Lists, the empty-list marker,
N-D numeric children, certified/native lanes, views, and changed carriers
reconstruct the original RHS literal and call `fn_join`. The emitter keeps
the existing consuming-local and certified-tail selections ahead of this
fresh-result case (**S9.3.1, S10.6.1, D4.4.4v4**).

Nine release pairs against the preceding build measure **0.8996x** typed
Microdiff time (9/9 wins) and **1.0005x** typed Prettier (6/9), with matching
output. The three-pair 63-row typed screen is 1.0046x geomean with all
outputs equal; its largest slow rows are Sieve and Permute (0.029 and
0.069 ms control medians), whose MIR images do not call the helper.
Levenshtein's >5% three-pair shift reruns at 0.9635x over nine pairs.
`result49_fresh_array_item` checks direct MIR emission, list and N-D
fallbacks, numeric left carriers, COW snapshots, and owned wide floats in
both tiers under forced GC. Artifacts: `paired_fresh_array_text.json`,
`paired_fresh_array_suite.json`, and `paired_fresh_array_outliers.json`
under `temp/result49_typed_impl/`.

The full Lambda/input baseline passes **6,025/6,025** with 234 forced-GC
cases, 183 emission fixtures, and 20 MIR ratchets; the static root audit
checks 17,212 functions cleanly. The three-pair 63-row untyped screen is
1.0045x geomean with all outputs equal. Its sole >5% slow row, untyped Fib,
reruns at 0.9899x over nine pairs and does not emit the new helper. Records:
`paired_fresh_array_untyped_suite.json` and
`paired_fresh_array_untyped_fib.json` under `temp/result49_typed_impl/`.

### Numeric copy census and remaining Cube3d lifetime

CD's current release `COW_EXEC_PROFILE` records 80,162 shared ArrayNum
copies (36.1 MB), 176,660 shared open-Array copies (53.0 MB), and 42,495
shared Map copies (2.0 MB). Its three-level `arr_set` writes each copied
child back into its parent; removing a detach requires an ownership proof
for that level under **S9.1.2** and **D4.4.4v4**. A trial that skipped
zero-filling complete ArrayNum clones passed a focused 1-D/2-D forced-GC
case, but CD's nine source-identical release pairs measured 1.0015x prior
time, with equal output. The trial was reverted; the large copy volume alone
does not make zero-fill a useful performance target. The profile and paired
artifact are `cd_fresh_cow.tsv` and `paired_numeric_clone_cd.json` under
`temp/result49_typed_impl/`.

Cube3d's `calc_cross` result already takes the bounded scalar direct-call
path on the hot edge. Its caller `calc_normal` still allocates two private
three-element inputs, `a` and `b`, before that call. Their elements read
`float[]` parameters at constant indexes and can be absent under
**S7.1.1v3**. Eliminating those arrays needs a guard over the required
source lengths, a cold path that evaluates the original declaration and
reports its exact error, and a producer-consumer proof that the callee only
reads the temporary lanes (**D5.2, D8.2.5v3, S11.4.1v3**). The later
source-level ceiling experiment below makes this edge a lower priority.

### Literal-stride matrix extent

Cube3d's `mat4_mul` uses `i * 4 + j`, `i * 4 + 0..3`, and `0..3 * 4 + j`
inside nested loops bounded by the literal `4`. The dense-loop scanner
previously recognized only a named stride, so these indexes retained
per-read null branches. The checked destination store was already cold:
release execution profiles report zero such calls for both builds.
The same guard now proves a literal row-major stride and its fixed row or
column axes, provided the source array has at least `extent²` elements.
Only a strictly bounded nested counter joins this proof: `j <= extent`
can reach the first index past the guarded region. The guard-failing sibling
keeps the original checked reads and writes (**S7.1.1v3, S7.1.3v2,
D2.5.3, D3.3.3v3**).

`result49_literal_matrix_extent` exercises valid multiplication, a short
source, and the inclusive nested-loop boundary in JIT, interpreter, and
forced-GC modes. Its MIR ratchet confirms the guarded raw copy and checked
sibling. Nine source-identical Cube3d release pairs on the final source
measure **0.9499x** time (6.430 to 6.108 ms medians, 9/9 wins, equal output).
The final-source 63-row typed three-pair screen measures **0.9956x**
geomean with all outputs equal. Its only >5% slow row, Sieve (0.032 ms
control median), reruns at 1.0000x over nine pairs. The corresponding
untyped screen measures **1.0080x** geomean with all outputs equal. Its five
>5% slow rows (Sum, Ack, Sieve, Queens, Pidigits) rerun between 0.9942x
and 1.0044x over nine pairs. Artifacts:
`paired_literal_matrix_final_cube3d.json`,
`paired_literal_matrix_final_typed_suite.json`,
`paired_literal_matrix_final_typed_outlier.json`,
`paired_literal_matrix_final_untyped_suite.json`, and
`paired_literal_matrix_final_untyped_outliers.json` under
`temp/result49_typed_impl/`.
The control and candidate store-count profiles are
`cube3d_literal_matrix_control_cow.tsv` and
`cube3d_literal_matrix_candidate_cow.tsv`; the gain is attributable to the
guarded read region, not to fewer executed checked stores.

The full Lambda/input baseline passes **6,027/6,027**, including 235
forced-GC cases, 184 MIR emission fixtures and 20 MIR ratchets. The static
native-root audit checks 17,213 functions cleanly.

### Guarded short-array lifetime

A fixed-read local `float[]` literal could previously stay in scalar lanes
only if every initializer element was statically non-null. An index of an
admitted `float[]` parameter is nullable under **S7.1.1v3**, even when a
particular call supplies enough elements. The local scalar plan now collects
the largest fixed index read from each parameter and guards their lengths
before evaluating any element. On a failed guard it evaluates the original
literal and crosses the declaration's checked `float[]` boundary, preserving
its exact error site. The successful arm stores only the numeric lanes;
escaping locals still materialize an independent ArrayNum (**D5.2,
S11.4.1v3**). The direct-call short-array path uses the same length-guard
emitter.

`result49_guarded_small_array` covers two source parameters, both missing
positions, a valid fixed-read local, and an escaping local in JIT, interpreter,
and forced-GC modes. A release kernel making one million calls to a
fixed-read two-element producer measures **0.3561x** the prior time
(70.052 to 24.945 ms medians; 9/9 paired wins, equal output). This isolates
the aggregate-lifetime opportunity; it is not a Cube3d gain. The final-source
63-row typed and untyped screens are output-equal and measure **1.0019x**
and **1.0007x** geomean respectively. Typed Sum, Mbrot, Ack, Base64 and
Levenshtein screen outliers rerun between 0.9646x and 1.0111x over nine
pairs. Untyped Fannkuch and Pidigits rerun at 0.9853x and 1.0173x.
Artifacts: `guarded_small_array_bench.ls`,
`paired_guarded_small_array_kernel.json`,
`paired_guarded_small_array_typed_suite.json`,
`paired_guarded_small_array_typed_outliers.json`,
`paired_guarded_small_array_untyped_suite.json`, and
`paired_guarded_small_array_untyped_outliers.json` under
`temp/result49_typed_impl/`.

The full Lambda/input baseline passes **6,029/6,029**, including 236
forced-GC cases, 185 MIR emission fixtures and 20 MIR ratchets. The static
native-root audit checks 17,214 functions cleanly. Cube3d's `a` and `b`
still cross the `calc_cross` call, so they remain materialized. A temporary
source-level diagnostic removed both inputs and the call, replacing the
whole producer-consumer chain with scalar locals. Nine same-engine release
pairs measure only **0.9886x** Cube3d time (5.894 to 5.827 ms, 6/9 wins,
equal output; one-sided 95% ratio upper bound 1.0102). This rewrites more
than a narrow compiler scalarization would and sets a low measured ceiling
for that specific edge. Artifacts: `cube3d_scalar_source_pilot.ls` and
`paired_cube3d_scalar_source_pilot.json` under `temp/result49_typed_impl/`.
A trial to cache a repeated typed `w.tasks[id]` read in Richards emitted no
new reuse: the existing place-handle lowering already navigates it once.
The trial was removed; path work should target regions where the generated
MIR actually repeats navigation.

### Read-place facts across a proven pure call

The place-handle scan now preserves a read-only typed record place across a
direct, immutable, capture-free `fn` call when each involved owner/key is a
plain unwritten parameter and every other argument is an effect-free leaf.
The call may retain the record and introduce sharing, so this proof cannot
license a later write through the handle: the first write ends it. A handle
that crosses the call lives in a precise root slot and is reloaded afterward,
including when the callee allocates and GC runs (**D4.4.4v4, D5.3.3,
S9.1.2**). General callees, `var` parameters, expressions with effects and
mutating calls still invalidate the fact.

`result49_read_handle_call` covers owner and key arguments, an allocating
callee under forced GC, a mutating call, and a retained alias followed by a
COW write. The MIR sidecar sees one indexed navigation across each admitted
read call and a reload after the mutating call; JIT, interpreter and forced-GC
outputs agree. A release kernel makes one million such calls at **0.9515x**
the prior time (14.038 to 13.357 ms medians, 7/9 wins, equal output; one-sided
95% ratio upper bound 0.9595). The final-source 63-row screens are
output-equal and measure **1.0022x** typed and **1.0005x** untyped geomean.
Typed Sieve's 0.030 ms screen control reruns at 1.0606x over nine pairs,
about 0.002 ms. Untyped Quicksort's >5% screen movement becomes 1.0117x
over 30 pairs. Untyped Pnpoly remains 1.0571x over 30 pairs, although its
finalized MIR differs between binaries only in relocated addresses; the new
typed-path plan emits no extra work in that script. A 30-pair final-binary
A/A control measures 1.0000x typed Sieve and 0.9908x untyped Pnpoly; the
Pnpoly B/A movement is recorded as a release-layout-sensitive residual
(inference), not assigned to added MIR operations. Artifacts:
`paired_read_handle_call_final_kernel.json`,
`paired_read_handle_call_final_typed_suite.json`,
`paired_read_handle_call_final_typed_outlier.json`,
`paired_read_handle_call_final_untyped_suite.json`,
`paired_read_handle_call_final_untyped_outliers_30.json`,
`paired_read_handle_call_aa_short_rows.json`, and
`pnpoly_control.mir` / `pnpoly_candidate.mir` under
`temp/result49_typed_impl/`.

The full Lambda/input baseline passes **6,031/6,031**, including 237
forced-GC cases, 186 MIR emission fixtures and 20 MIR ratchets. The static
native-root audit checks 17,215 functions cleanly. The alias/COW addition to
the fixture then passed its focused emission, forced-GC and all-tier checks.

### Final-source comparison with the archived Result49 release

The final release, including the pure-call read-place proof, was paired
against the archived original Result49 binary on the same current source for
all 63 typed rows, five alternating pairs per row. Every row completed with
equal output. Geometric-mean execution time is **0.9586x** the original; the
sum of row medians falls from 7,492.306 to 6,070.803 ms (**19.0%**). Log
Pipeline measures 0.4791x, Levenshtein 0.5821x, Richards 0.7723x,
Knucleotide 0.7448x, Prettier 0.8316x, Text Search 0.8766x, Microdiff
0.8999x and Cube3d 0.9632x. The two >5% slow screen rows rerun over 30
pairs: 1.0625x Sieve (0.032 ms control median, about 0.002 ms difference)
and 1.0449x Pidigits (0.32 ms control median). Source/binary hashes, raw
samples and uncertainty are in `paired_result49_final_with_call_vs_original.json`
and `paired_result49_final_with_call_outliers.json` under
`temp/result49_typed_impl/`.

Holding the Result49 C2MIR medians fixed, the 3.1615x typed/C2MIR
geomean becomes about **3.03x**. This is an inference across measurement
runs, not a fresh simultaneous C2MIR comparison. The planned 2.5x target
is not reached. The bounded implementations cover all five proposed
mechanisms, but a general interprocedural path/call-effect proof and
aggregate destination analysis remain open. The Cube3d scalar-source
ceiling and already-reused Richards read path argue against expanding those
two specific pilots without a new executed-work census (**D4.4.4v4, D5.2,
D8.3.1v2**).
