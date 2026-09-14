# JS Tune11 — Result44 Recovery Through Shared Physical Plans

**Date:** 2026-09-15

**Status:** **IMPLEMENTED — CLOSEOUT COMPLETE.** T11-0 through T11-5 are in
the working tree, and T11-6 closed with the generated 63-row Result45 snapshot
and an interleaved exact-archive A/B. The directional whole-suite goals missed
their targets; §2.4 and T11-6 record that residual explicitly rather than
claiming an aggregate recovery from focused wins.

**Measured snapshot:** [Result45](../../test/benchmark/Overall_Result45.md),
generated from release archive `lambda-v45-714d448fb2`, SHA-256
`be0abf995ec22252c9fa70253a1f4dc5d89ecd7e81c6e71a3677772c400232ed`.
Result44 remains the diagnostic baseline in §2.1.

**Live-source audit:** the implementation worktree is based on local `master`
`714d448fb2` on 2026-09-15. Result45 records the exact release archive rather
than treating the dirty implementation tree as an already committed source
snapshot.

**Predecessors:** [JS Tune10](JS_Tune10_Fast_Paths.md),
[JS/Lambda unification P7](../Lambda_Proposal_JS_Unify_P7.md),
[JS2 guarded access](../impl/Lambda_Impl_JS2_Guarded_Access.md), and
[Tune27](<../impl/Lambda_Impl_Tune27 (done).md>) §12–§13.

**Scope:** workload and end-to-end LambdaJS performance, shared MIR planning,
numeric/index/field/string hot paths, lowering simplification, and their
correctness/performance gates. No JavaScript or Lambda semantics change, no
benchmark-source rewrite, no Test262-harness suppression, no inline cache, and
no vendored dependency change.

**Formal linkage:** **S3.1** and **S9.1.4** (semantic differences that stay
language-local), **D1.3** (reuse below the semantic boundary),
**D2.4.1–D2.4.3** (explicit representation facts), **D3.3.2v2–D3.3.3v3**
(inference selects an implementation, not a source contract), **D4.1.5**
(string-builder ownership), **D5.3.4**
(rooting is emitter-owned), **D5.4.2–D5.4.4** (construction-time registration,
no repeated-path coherence tax), **D6.2.2v2** (JS call/construct dispatch),
**D8.2.1–D8.2.3** (shared catalog and extract-after-convergence),
**D8.4.1v2–D8.4.3v2** (guarded specialization, direct ABI, explicit
completion), and **D8.6.1–D8.6.4v2** (emission and performance gates).
This proposal changes no formal ruling.

---

## 0. Implementation checkpoint — 2026-09-15

Tune11 restores profitable physical work without restoring the removed
JS-specific fast-path subsystem:

- `MirNumericOpPlan` in `lambda/runtime/mir_emitter_shared.hpp` is the shared
  physical operation table. Lambda and JavaScript consume the same numeric MIR
  plan while JavaScript retains Number admission, coercion, and fallback
  semantics (**D1.3**, **D2.4.3**, **D8.2.3**).
- Proven Number `+`, `-`, `*`, `/`, `%`, equality, and relational expressions
  retain native F64/I64 carriers; an incomplete proof takes the existing
  `js_*` semantic helper. In particular, `+` never admits a String, object,
  BigInt, or unknown value.
- `JsMirReference` carries a computed native Number key to a selected physical
  hit or one generic semantic fallback. A packed tagged literal-array read
  checks its precise receiver/layout/bounds invariants, then uses the shared
  element-address emitter and one tagged load. Holes, prototype visibility,
  noncanonical keys, and changed receivers take the cold kernel exactly once
  (**D5.3.4**, **D8.4.1v2**).
- Object-literal and eligible constructor recipes create immutable TypeMap
  plans. Exact ordinary fields use a guarded direct slot read/store; a shape
  transition, descriptor/exotic case, or incompatible value falls back to the
  current NameId kernel. This is a compile prediction, not a feedback cache.
- ASCII string leaves now expose search, split, slice, character-access, and
  concatenation trace events. The rooted 1,024-entry ASCII substring cache is
  epoch-guarded and only caches eligible 2–32 byte substrings; Unicode retains
  the existing semantic path.
- Property lowering now has a compact reference/get/put flow, and known-code
  function source setup avoids a GC-capable setter on the common MIR path.
  The module-unit index is an `ArrayList`, recovering the no-GC property of
  that path without weakening rooting rules (**D5.3.4**, **D8.4.3v2**).

Validation and audit:

- `make build-test`, `make release`, `test_js_opt_gtest` (24/24),
  `test_js_mir_emission_gtest` (27/27), and `test_mir_ratchet_gtest` (19/19)
  passed. The new Number, index, and field-plan fixtures pin both direct MIR
  and the single semantic miss.
- `make test-mir-gc-stress` and `make test-gc-rooting-core` each passed
  184/184; `python3 utils/check_gc_effects.py` verified 68 no-GC imports and
  135 project call-graph nodes.
- The independently reproduced `beng_revcomp2` aggregate-baseline residual
  was a byte-lane borrow defect in `lib/str.c`, not a Tune11 exception. The
  corrected per-byte case transform and `StrCaseTest.MixedBytesAcrossWordBoundaries`
  passed; `make test-lambda-baseline` is now 5,486/5,486.
- The release Test262 baseline is 40,261/40,261 fully passing, with zero
  regressions. Result45 is a full guarded standard-runner snapshot; §2.4 gives
  its archive, cells, and the causal A/B record.

---

## 1. Decision

Result44's remaining LambdaJS gap should be attacked by restoring **native
numeric, index, and predicted-field execution as shared physical plans**, not
by restoring the deleted JS-specific fast-path implementation and not by
forcing JavaScript through Lambda operators.

The unit of unification is:

```text
language analysis and semantic admission
        -> immutable MIR operation plan
        -> shared representation/address/guard emitter
        -> direct physical hit
        -> one language-semantic kernel on a miss
```

Lambda and JavaScript retain separate semantic walkers. They share the plan
facts and physical emission below them. This is the boundary required by
**D1.3**: guests reuse contracts below the semantic boundary and do not inherit
another language's truthiness, coercion, capture, object, or mutation model.
Lambda truthiness alone differs under **S3.1**, and Lambda closure captures are
snapshots under **S9.1.4**; neither may leak into JavaScript as an optimization.

The generated common case must contain only the proof required for that case:

- A proven numeric operation stays in a native carrier and does not call a
  boxed operator helper.
- A proven dense read checks the read invariant and bounds, then loads. It does
  not pay store/COW/descriptor/prototype-add policy.
- A predicted own-field hit checks the exact physical shape and slot validity,
  then loads or stores. It does not run host, Proxy, accessor, or dictionary
  logic on the hit.
- Every miss reaches the existing JavaScript semantic kernel exactly once.
- An unknown site remains one compact generic call; it does not receive a
  large inline type dispatcher.

This makes code simplification and performance compatible. The compiler owns a
small number of plan emitters; the runtime owns one semantic fallback per
operation family; uncommon behavior stays cold.

---

## 2. Evidence quality and benchmark provenance

### 2.1 What Result44 contains

Result44 is a complete 63-row guarded matrix with MIR untyped, MIR typed,
C2MIR, LambdaJS, QuickJS, and Node.js. It records three samples per row, a
180-second timeout, no missing timing cell, and the required Test262 gate:

- 40,258 / 40,261 fully passed.
- Zero Test262 regressions.
- 126 LambdaJS/LambdaJS-E2E cells were refreshed separately from
  `temp/benchmark_v44_ljsfix.json`.
- The refreshed release archive is
  `test/benchmark/exe/lambda-v44-acd1e88d1b-ljsfix`, SHA-256
  `3b43e87bb518f9fd6ed71aa78b93561f949a6b8fcc3ec09c02053cf28b98dbbe`.
- That archive is the Result44 source tree plus the two then-uncommitted
  post-Result44 fixes described by Tune27 §12.

The refreshed LambdaJS column is the only valid Result44 LambdaJS baseline.
The pre-refresh column includes a root-reservation defect that is already
fixed and must not be used to prioritize residual work.

### 2.2 Confirmed Result44 root regression

The pre-refresh binary repeatedly ran
`js_runtime_state_prepare_root_vectors()` and the full realm-slot reservation
loop from `js_realm_intrinsic_slots_ensure_roots()` on every prototype lookup.
A release `sample` put 94% of the affected run in that routine. The current
`JsRealmSlots::suffix_reserved` check performs the work once per store lifetime
and returns immediately thereafter, consistent with **D5.4.2**, **D5.4.4**,
and the fixed maximum-root-span precedent of **D5.3.5**.

Across the 63 rows, pre-fix LambdaJS divided by the fixed column is 2.616x by
geomean and 1.877x by median. The largest repaired rows were:

| Row | Pre-fix (ms) | Fixed (ms) | Pre/fixed |
|---|---:|---:|---:|
| `larceny/puzzle` | 1,369.2 | 61.5 | 22.25x |
| `awfy/sieve` | 72.0 | 3.30 | 21.82x |
| `kostya/base64` | 10,887.9 | 561.7 | 19.39x |
| `jetstream/navier_stokes` | 6,630.4 | 386.7 | 17.15x |
| `awfy/permute` | 132.7 | 8.38 | 15.83x |
| `jetstream/crypto_sha1` | 5,181.1 | 386.8 | 13.39x |
| `text/three_way_merge` | 162,918.0 | 15,015.5 | 10.85x |

This is a confirmed special-case tax: lifecycle preparation was placed in a
repeated common path. It is also the design pattern Tune11 must follow in the
other direction—prepare facts once and keep repeated execution minimal.

### 2.3 What the comparisons can and cannot prove

Result44 is suitable for ranking current costs. It is not, by itself, a causal
A/B experiment:

- Its LambdaJS column has different binary provenance from the other columns.
- Result43→Result44 timings include host variation; QuickJS and Node improved
  more than LambdaJS.
- Result41→Result44 spans compiler/runtime changes that both improve and
  regress different workloads.
- Three samples per row are adequate for a guarded snapshot but not for a
  narrow tuning attribution near the noise floor.

A focused interleaved replay was attempted with the Result42 archive and the
Result44 fixed archive for `fft`, `sieve`, `primes`, `spectralnorm`,
`quicksort`, and `array1`. The Result42 control exited on every current bundle
with `Cannot destructure 'undefined' as it is undefined`, leaving zero valid
pairs. No causal claim in this document relies on that failed replay.

Evidence below is labeled as one of:

1. **Confirmed root cause:** profiler/source/repaired-binary evidence agrees.
2. **Matched A/B:** identical workload and interleaved control/candidate
   archives with matching output.
3. **Strong regression signature:** unchanged controls and a source change
   align, but no fresh matched A/B exists.
4. **Hypothesis:** the row and source shape identify a candidate that still
   requires counters or a profile.

### 2.4 Result45 and exact-archive closeout

[Result45](../../test/benchmark/Overall_Result45.md) is the required full
standard-runner snapshot, not a cell merge. It used the release archive
`test/benchmark/exe/lambda-v45-714d448fb2`, SHA-256
`be0abf995ec22252c9fa70253a1f4dc5d89ecd7e81c6e71a3677772c400232ed`.
The guarded run passed on AC power after a clean release build and a
40,261/40,261 Test262 gate. It generated all 63 rows with three fresh medians,
zero missing timing cells, and no timeout/output mismatch. The current
standard runner's engine population is MIR, LambdaJS, QuickJS, and Node.js;
unlike Result44, Result45 therefore does not include a C2MIR reference column.

Result45's workload geomeans are LambdaJS/QuickJS **2.588x**,
LambdaJS/MIR-U **14.020x**, and LambdaJS/Node **18.728x**. The sum of the 63
LambdaJS workload medians is **228.399 s**. The fresh sequential Result45 /
Result44 LambdaJS geomean is **0.883x**, but that observation is explicitly
not used for attribution: the snapshots span source and host variation.

For causal evidence, a clean `714d448fb2` control worktree was built from the
same source basis as the implementation tree, using the host's already-built
ignored dependency artifacts only to make the control link. Its release binary
SHA-256 is `7a05610eb275cd3d054ed224855cbe33eef69c29c871f0038b65112ae806cd97`.
The control and Result45 candidate ran an interleaved, output-equality-checked
11-pair JIT corpus on Darwin/arm64, with 10,000 paired-bootstrap one-sided 95%
upper bounds. The generated artifact is
`temp/t11_paired_primary.json`, SHA-256
`b702a4ded0a570668d406492d168dc49830d29e6259e7032a3b9d68cb79c688c`.

| Workload | Candidate / control | 95% upper bound | Candidate wins / 11 | Classification |
|---|---:|---:|---:|---|
| `awfy/havlak` | 0.9984x | 0.9994x | 10 | neutral control |
| `beng/spectralnorm` | 0.5048x | 0.5074x | 11 | numeric/index win |
| `kostya/matmul` | 0.5546x | 0.5567x | 11 | numeric/index win |
| `larceny/array1` | 0.5785x | 0.5832x | 11 | numeric/index win |
| `text/three_way_merge` | 1.0090x | 1.0114x | 0 | neutral control; +0.9% |
| `text/log_pipeline` | 0.9865x | 0.9894x | 11 | string/field win |

All six workloads produced exactly equal output on every pair. No primary or
neutral control has a confirmed regression above 3%. The four material wins
demonstrate the selected plans; the directional aggregate targets remain
missed (QuickJS target 2.0x, MIR-U target 10x, median-sum target 160 s), so
the remaining gap is retained as performance debt rather than disguised as a
Tune11 completion failure (**D8.6.1–D8.6.4v2**).

---

## 3. Result44 performance analysis

### 3.1 Headline gap

All ratios below are workload-only medians aggregated by geomean. LambdaJS /
QuickJS and LambdaJS / MIR-U are derived from the same Result44 cells.

| Suite | Rows | LambdaJS / Node | LambdaJS / QuickJS | LambdaJS / MIR-U |
|---|---:|---:|---:|---:|
| R7RS | 10 | 7.89x | 1.22x | 22.54x |
| AWFY | 14 | 22.44x | 4.33x | 15.24x |
| BENG | 8 | 9.48x | 5.62x | 25.41x |
| KOSTYA | 7 | 42.49x | 3.56x | 27.56x |
| LARCENY | 11 | 22.13x | 1.66x | 14.86x |
| JetStream | 6 | 37.97x | 3.14x | 7.76x |
| Text | 7 | 51.57x | 4.02x | 7.41x |
| **Overall** | **63** | **21.05x** | **2.91x** | **15.93x** |

End-to-end Result44 reports LambdaJS / Node at 4.92x and QuickJS / Node at
1.27x, hence LambdaJS / QuickJS is 3.88x end-to-end. The smaller LambdaJS /
Node E2E ratio does not mean startup is solved; it reflects Node's process and
JIT startup being included while the workload-only section excludes startup
for every engine.

R7RS is already close to QuickJS as a suite. Calls and recursion therefore are
not the first Tune11 target. AWFY, BENG, KOSTYA, JetStream, and Text identify
numeric, indexed, field, and string work as the larger residuals.

### 3.2 Absolute time concentration

The 63 LambdaJS workload medians sum to 233,224.7 ms. A geomean treats every
row equally; engineering payoff does not. The first five rows account for
79.95% of all measured LambdaJS time and the first eight for 88.68%.

| Rank | Row | LambdaJS (ms) | Share | Cumulative | LJS/QJS | LJS/MIR-U |
|---:|---|---:|---:|---:|---:|---:|
| 1 | `text/text_search` | 117,912.8 | 50.56% | 50.56% | 3.02x | 7.74x |
| 2 | `awfy/havlak` | 21,290.2 | 9.13% | 59.69% | 6.40x | 292.07x |
| 3 | `text/log_pipeline` | 17,217.2 | 7.38% | 67.07% | 1.83x | 2.90x |
| 4 | `larceny/diviter` | 15,027.3 | 6.44% | 73.51% | 0.56x | 56.37x |
| 5 | `text/three_way_merge` | 15,015.5 | 6.44% | 79.95% | 2.43x | 4.44x |
| 6 | `larceny/triangl` | 7,608.5 | 3.26% | 83.21% | 3.49x | 27.46x |
| 7 | `kostya/collatz` | 7,401.6 | 3.17% | 86.39% | 1.19x | 23.38x |
| 8 | `awfy/cd` | 5,355.2 | 2.30% | 88.68% | 5.58x | 9.28x |
| 9 | `text/prettier_ast` | 5,017.5 | 2.15% | 90.83% | 3.53x | 5.44x |
| 10 | `kostya/brainfuck` | 3,076.1 | 1.32% | 92.15% | 3.47x | 14.02x |

The largest ratio is not necessarily the largest payoff. `primes` is 399x
Node but only 0.75% of total LambdaJS time. It is still important because it
is a sharp regression reproducer for the lost numeric/index lane, not because
it dominates the total.

### 3.3 Result43→Result44: controls improved more

Ratios below are Result44 / Result43; below 1 is faster. The normalized column
divides the LambdaJS change by the QuickJS change for the same suite.

| Suite | LambdaJS | QuickJS | Node | LJS/QJS normalized |
|---|---:|---:|---:|---:|
| R7RS | 0.764x | 0.753x | 0.674x | 1.015x |
| AWFY | 0.908x | 0.853x | 0.825x | 1.064x |
| BENG | 0.997x | 0.999x | 0.995x | 0.997x |
| KOSTYA | 0.999x | 0.999x | 0.992x | 0.999x |
| LARCENY | 1.009x | 0.986x | 0.991x | 1.023x |
| JetStream | 1.084x | 0.999x | 0.991x | 1.086x |
| Text | 0.995x | 0.980x | 0.985x | 1.015x |
| **Overall** | **0.945x** | **0.918x** | **0.895x** | **1.030x** |

LambdaJS is nominally 5.5% faster overall, but normalized to QuickJS it is
3.0% slower and normalized to Node it is 5.6% slower. JetStream is the clearest
suite-level residual. The largest LambdaJS row increases are `revcomp` 1.386x,
`cd` 1.336x, `splay` 1.209x, `regexredux` 1.193x, `havlak` 1.162x, and
`microdiff` 1.155x. These are replay/profile candidates, not confirmed defects.

### 3.4 Result41→Result44: de-specialization signature

Commit `85dcc897e` (`JS code reduction`) is not an ancestor of Result41
(`cbd220d923`) but is an ancestor of Result43 and Result44. Across all 63 rows,
Result44 / Result41 is:

| Engine | Geomean |
|---|---:|
| MIR untyped | 0.953x |
| MIR typed | 0.699x |
| C2MIR | 1.127x |
| LambdaJS | 1.029x |
| QuickJS | 0.999x |
| Node.js | 0.999x |

The aggregate LambdaJS result hides opposing changes. Call/completion and
allocation repairs improved some rows, while the removed numeric/index lanes
regressed their focused workloads:

| Row | Result41 LJS (ms) | Result44 LJS (ms) | R44/R41 |
|---|---:|---:|---:|
| `kostya/primes` | 135.5 | 1,754.8 | 12.96x |
| `beng/spectralnorm` | 38.7 | 245.4 | 6.33x |
| `awfy/sieve` | 1.26 | 3.30 | 2.63x |
| `larceny/array1` | 35.9 | 87.1 | 2.43x |
| `kostya/matmul` | 661.5 | 1,400.7 | 2.12x |
| `awfy/cd` | 4,103.9 | 5,355.2 | 1.30x |
| `awfy/havlak` | 18,392.9 | 21,290.2 | 1.16x |

QuickJS and Node being flat strengthens the signal. The comparison remains
sequential, so Tune11 treats it as a **strong regression signature**, not a
confirmed attribution.

### 3.5 Existing matched A/B evidence

The earlier JS2 guarded-access candidate did provide same-workload A/B evidence:

| Row | Control (ms) | Candidate (ms) | Speedup | Result |
|---|---:|---:|---:|---|
| `kostya/matmul` | 658.3 | 326.8 | 2.014x | candidate won 5/5 |
| `larceny/array1` | 33.8 | 17.0 | 1.987x | candidate won 5/5 |
| `beng/fannkuch` | 27.0 | 23.4 | 1.154x | candidate won 5/5 |
| `larceny/quicksort` | 146.2 | 154.3 | 0.947x | candidate won 4/11 |

The candidate also increased the wall-minus-workload remainder on some rows,
consistent with more emitted guards and compilation work. This proves two
things simultaneously:

1. Native numeric access has real payoff.
2. Restoring the former large handwritten dispatcher verbatim is not the right
   design; specialization must become smaller, selected earlier, and shared.

---

## 4. Live-code diagnosis

### 4.1 Numeric expressions are boxed again

`lambda/js/js_mir_expression_lowering.cpp::jm_emit_binary_expression` retains
string concatenation, short-circuit semantics, and a few focused cases, but its
general arithmetic/comparison switch maps directly to `js_add`,
`js_subtract`, `js_multiply`, `js_divide`, `js_modulo`, `js_compare`, and the
other boxed runtime helpers. Both operands are produced through
`jm_transpile_box_item` before the call.

`jm_transpile_condition` recognizes a statically numeric comparison, but it
obtains the already-boxed binary expression and then extracts truthiness. The
comment claiming that the numeric boundary already returns a raw bit is stale
for the current general binary path.

Consequences in a hot numeric loop include:

- Repeated Item boxing/publication.
- Runtime helper call/return overhead.
- Repeated tag classification and coercion logic after inference already
  established a Number-only operation.
- Loss of an unboxed induction/comparison chain across loop edges.
- Reduced opportunity for MIR to combine address arithmetic and eliminate
  conversions.

The loss is visible in `primes`, `spectralnorm`, `sieve`, `matmul`, `array1`,
`mandelbrot`, `collatz`, and `diviter`.

### 4.2 Computed property access discards the native key

The reference path currently performs:

```text
evaluate key as Item
    -> js_to_property_key when not already canonical
    -> js_property_lane_for_canonical_key
    -> js_get / js_set
```

The direct member-value path separately performs either `js_get_name_id` for a
static name or `js_get_reference` for a computed key. This duplicates the
post-evaluation access choice between `jm_emit_reference` +
`jm_emit_get_value`/`jm_emit_put_value` and `jm_emit_member_value`.

`js_elements_get_int`, `js_elements_set_int`,
`js_elements_set_int_completion`, and `js_string_get_int` still exist as
native-index semantic entries in `lambda/js/js_runtime.cpp`, but the JS MIR
lowering has no call site for the element entries. The current compiler thus
boxes a key even where it already owns an I64/F64 carrier, and the runtime must
recover index identity later.

This is particularly expensive for `text_search`: its timed body searches
arrays of numeric character codes with repeated `text[position + offset]`,
`pattern[offset]`, prefix-table reads/writes, arithmetic, and comparisons.
The row is not primarily a Unicode string-index workload; it is the largest
numeric-array/index workload in the matrix.

### 4.3 Named access has a good generic head but no predicted hit

`lambda/js/js_runtime.cpp::js_get_name_id` resolves the `NameId`, checks a host
dynamic property, probes `js_named_fast_lookup`, and calls
`js_get_reference` on a miss. `js_set_name_id` similarly attempts a same-slot
write and then continues through ordinary-add or generic set semantics.

That is the correct stateless Tier-B kernel under **D8.4.1v2**. It is not the
complete policy. The same ruling explicitly sanctions compile-predicted
literal/constructor shapes with an inline guard and the shared kernel on a
miss. Current JavaScript lowering does not consume
`lambda/runtime/mir_shape_candidates.hpp::mir_shape_candidate`, even though
Lambda does and the header already describes JS module-recipe ownership.

Object-heavy rows therefore pay NameId resolution and receiver/shape
classification at every site, even when a literal, constructor, direct
argument, or known return provides a stable candidate.

### 4.4 The code-reduction commit removed mechanisms, not only duplication

Commit `85dcc897e` changed 23 files by +119/-1,897 and removed 1,404 lines from
`js_mir_expression_lowering.cpp`. The deleted code included:

- `jm_emit_numeric_index_access` and native key carriers.
- `jm_emit_predicted_shape_access`.
- Native arithmetic/comparison tables and paths.
- Native update expressions.
- JS-local constant folding.
- The JS consumer of the shared shape-candidate walk.

The same commit revised `test/mir/js/shared_access_guards.mir-check` to require
`js_get_name_id`, `js_get_reference`, and `js_set`, while forbidding the prior
numeric ABI entries. That check accurately pins the post-removal
implementation, but its current generic-only expectation is not architectural
authority. When Tune11 lands, it must instead pin the **D8.4.1v2** contract:
direct predicted hit, immutable guard, and one semantic miss path.

### 4.5 Documentation drift

`doc/dev/js/JS_04_MIR_Lowering.md` and `doc/dev/js/JS_15_Performance.md` still
describe general native arithmetic and constant folding as live mechanisms.
The live source contradicts those informative documents. Formal design and
the source audit above govern the proposal. Tune11 must update the detailed
documents after the mechanism stabilizes; it must not use their stale status
as implementation evidence.

---

## 5. Unification boundary with untyped Lambda

### 5.1 Share representation facts, not source meaning

Under **D2.4.1–D2.4.3**, semantic contract, planned representation, emitted
representation/provenance, and MIR register class are distinct facts. Tune11
must carry every decision through `MirValue` or a named plan. It must never
infer meaning from `MIR_T_I64`, because a boxed Item, Lambda int lane, machine
quantity, and pointer can share that register class.

Under **D3.3.2v2**, JavaScript inference may select a native implementation but
does not create a source contract. If a value at runtime may be String,
BigInt, Symbol, object-with-coercion, or another non-Number case, the operation
cannot enter a Number-only plan merely because a local heuristic prefers it.
The admission proof must dominate the operation or the site must retain a
guard and semantic fallback.

Under **D3.3.3v3**, a binding-local inferred element type is not a reusable
container certificate. Tune11 may use it to select a guarded plan at that
binding; it may not attach Lambda's typed-array representation authority to a
JavaScript object.

### 5.2 Reusable Lambda mechanisms

The current Lambda lowering already contains the mechanisms worth promoting:

- `mir_emit_native_index_expr`: preserves and composes native index
  expressions.
- `mir_emit_dense_index_expr`: recognizes dense index arithmetic.
- `mir_prepare_dense_loop_guard`: constructs loop-dominating extent facts.
- `typed_array_dense_store_guard`: separates write admission from read extent.
- `mir_expr_candidate_shape` and `mir_module_unique_shape_for_field`: consume
  the shared `mir_shape_candidate` walk.
- The `em_*` address, guard, load/store, boxing, demand, and root-store helpers
  in `mir_emitter_shared.hpp`.

Not all current Lambda policy is reusable. Lambda COW behavior, saturating int
semantics, nullable lane sentinels, error admission, and typed-container
certificates remain Lambda-specific. JavaScript arrays, Number arithmetic,
prototypes, descriptors, typed-array detach/resizability, and abrupt completion
remain JavaScript-specific.

The extraction rule is **D8.2.3**: promote the smallest already-working Lambda
mechanism while adding JavaScript as the second client in the same coherent
slice. Do not build a universal callback framework in advance. P7 already
withdrew the universal shared-walker proposal after showing that the shared
skeleton was thin and the residual semantics thick.

### 5.3 Planned structure

Tune11 introduces or completes four plan families:

| Plan | Owner | Shared content | Language-local content |
|---|---|---|---|
| Numeric operation plan | frontend analysis | operand/result reps, native MIR opcode shape, range facts, demand | JS Number/BigInt/string-coercion admission; Lambda numeric semantics |
| Index access plan | frontend analysis | native index expression, bounds/extent fact, element address, result rep | JS array/typed-array/string validity and fallback; Lambda COW/certificate policy |
| Field access plan | existing `mir_shape_candidates.hpp` | candidate source walk, construction/field/offset plan, exact-layout guard primitives | JS ordinary-object/descriptor/host admission; Lambda map contract admission |
| Call/result plan | existing shared substrate | argument carriers, direct ABI, result/completion shape, root placement | JavaScript `Get`/`[[Call]]`/`[[Construct]]`; Lambda function/error semantics |

The call/result family is already substantially shared and is not the first
performance phase. It remains in the table to keep the architecture coherent
and prevent property tuning from bypassing JavaScript's observable Get-before-
Call behavior under **D6.2.2v2**.

---

## 6. Common-case-first rules

These are implementation constraints, not optional style preferences.

### 6.1 Select before emitting

Analysis chooses exactly one primary plan for a site. Generated code must not
run a large ordered list of receiver formats on every access. For example:

- A site predicted as packed `ArrayNum` emits the `ArrayNum` guard and address,
  not tagged-array plus every typed-array class plus String checks.
- A site predicted as a plain object field emits the exact shape/slot guard,
  not host/Proxy/DOM/array/string classification.
- A site with no useful prediction emits the generic kernel directly.

Multi-versioning, if later justified by a census, remains an immutable guard
chain under **D8.4.1v2** and needs its own emission/performance gate. Tune11's
initial plans are monomorphic predictions plus one miss.

### 6.2 Read proofs and write policy are different products

A read asks whether a value is present at a valid address. A write additionally
issues a semantic mutation and may require ownership, descriptor, prototype,
extensibility, coercion, observation, and strict-mode behavior.

Tune27 §12 demonstrated the failure mode: a write/COW guard was ANDed into the
loop register used by unrelated dense reads, making all reads take the checked
arm and leaving typed `matmul` 3x slower than untyped. Tune11 therefore carries
separate plan fields and registers for:

- `read_extent_valid`
- `read_slot_valid`
- `write_existing_valid`
- `write_create_or_transition`

A store guard must never contaminate a read-only access. A failed store guard
falls directly to the JS set kernel; it cannot poison a surrounding loop's
read plan.

### 6.3 Special cases stay in the semantic miss

The direct hit never implements a second copy of:

- `ToPrimitive`, `ToNumeric`, `ToPropertyKey`, or BigInt mixing errors.
- Proxy traps, accessors, inherited setters, exotic host objects, DOM live
  collections, or dictionary-mode storage.
- Array holes that require prototype lookup, non-writable length, sparse
  storage, or property attributes.
- Detached, shared, resizable, or length-tracking typed-array behavior unless
  the selected plan explicitly proves the required invariant.
- Private names, `super`, optional-chain short-circuiting, or exception
  completion.

Those cases remain correct by falling through to the one existing kernel. A
fast-path miss must be observationally identical to starting in the kernel, as
required by **D8.4.1v2**.

### 6.4 Do not trade source LOC for repeated runtime work

The governed objective is total system simplicity:

- One plan definition.
- One emitter per physical family.
- One semantic fallback per language operation.
- Small emitted hot code.
- No duplicated semantic implementation.

Deleting a compiler proof because the fallback is functionally correct can be
a performance regression. Conversely, copying 1,400 lines back into the JS
lowering would be an architectural regression. Tune11 reduces LOC only through
shared plan/emitter extraction after the profitable path is measured.

---

## 7. Tune11 phases

### T11-0 — Freeze a clean baseline and add the census

**Purpose:** remove provenance ambiguity before tuning.

Actions:

1. Build and archive one release binary from the exact current candidate tree.
2. Run the full Test262 baseline before performance measurement.
3. Run the complete 63-row matrix with the standard generated JSON/Markdown
   workflow; do not merge selected cells into this baseline.
4. Extend the existing `JS_OPT_TRACE` schema, in profiling builds only, with:
   - native numeric candidate/admitted/fallback counts by operator family;
   - native-index candidate/admitted/fallback counts;
   - index miss reasons: non-numeric key, fractional/range, unknown receiver,
     kind mismatch, hole/sparse, bounds, attributes, typed-array state;
   - predicted-field candidate/hit/miss counts;
   - field miss reasons: receiver, shape, slot, flags, reserved/deleted, value
     representation, host/exotic;
   - boxes, unboxes, root stores, and generic fallback calls already present in
     the schema.
5. Add focused contract fixtures proving trace-on and trace-off produce the
   same output and finalized MIR shape, following the existing
   `test_js_opt_gtest` discipline.
6. Profile at least `text_search`, `havlak`, `log_pipeline`, `diviter`,
   `three_way_merge`, `triangl`, `collatz`, `cd`, and `prettier_ast` in release
   or a release-equivalent symbolized build.

**Implemented result:** numeric and index admission/fallback counters, field
plan contracts, and the ASCII string-leaf event families are present. The
compiler-time counters record selected plans rather than loop iterations, so
trace-on/off retains the same generated MIR and the release hot path has no
trace call. The two profile-led text workloads show the intended split:
`log_pipeline` made 16.56M ASCII searches, 2.16M splits, 28.8M slices, and
273,004 concats; `three_way_merge` made 1.551M ASCII splits, 11,000 character
accesses, and 2,057 concats. The former's substring cache recorded 24,663,929
hits and 4,121,131 misses (85.7% hits); the latter does not call substring.
Result45 and the release archive close the baseline requirement in §2.4.

Exit:

- One single-provenance baseline with all 63 rows and archived SHA-256.
- Zero new Test262 regressions.
- Every new counter has a focused nonzero fixture and a zero/expected miss
  fixture.
- Instrumentation is compiled out of the ordinary release path.
- Each top-nine row has a dominant-operation census; later phases may reorder
  only if this evidence contradicts §3's hypothesis.

### T11-1 — Shared numeric-operation plan

**Purpose:** keep proven JavaScript Number operations native while sharing
Lambda's representation/range machinery.

Admission:

- Both operands have a dominating Number-only proof or arrive in a native
  numeric `MirValue` carrier whose provenance excludes non-Number values.
- `+` is admitted only when string/object coercion and BigInt are excluded.
- Relational and equality plans are admitted only where the JS Number result
  is identical to the direct IEEE operation, including NaN and signed zero.
- Integer MIR arithmetic is used only where interval facts prove the complete
  JS Number result. Otherwise use F64 or the semantic helper.
- Bitwise operations use explicit JS ToInt32/ToUint32 legalization; they do not
  reuse Lambda int semantics.
- No plan is inferred from `MIR_reg_type()`; **D2.4.1–D2.4.3** apply.

Hit shape:

```text
native operands -> one MIR numeric/compare sequence -> native MirValue result
```

There is no tag check or helper call when the proof dominates the operation.
When only a guarded proof exists, one type/provenance guard branches to the
boxed JS operator helper.

Keep native results through:

- loop induction and test edges;
- arithmetic chains;
- address arithmetic consumed by T11-2;
- compound assignment to an admitted local;
- prefix/postfix updates, with the correct old/new result carrier.

Primary rows: `primes`, `spectralnorm`, `sieve`, `fft`, `mandelbrot`,
`collatz`, `diviter`, `matmul`, `array1`, `triangl`.

Non-goals: constant folding, arbitrary speculative number guards, BigInt
optimization, and cross-language operator semantics.

**Implemented result:** `MirNumericOpPlan` is consumed by Lambda F64
arithmetic/comparisons and JavaScript Number-proven arithmetic, modulo, and
comparisons. The contract fixture proves a native local accumulator through a
loop and a boxed fallback for a partial proof. Bitwise conversion remains a
semantic JS path by design; it was not falsely admitted as a Lambda integer
operation. The paired `spectralnorm` result in §2.4 is a 0.5048x candidate /
control ratio with a 0.5074x 95% upper bound.

Exit:

- Focused MIR checks show no `js_add`/`js_subtract`/`js_multiply`/
  `js_compare` call inside admitted numeric loop bodies.
- Mixed Number/String, object coercion, NaN, infinities, signed zero, division,
  modulo, bitwise conversion, BigInt mixing, and abrupt-completion fixtures
  remain exact.
- At least two regression reproducers show a stable material win; the targeted
  fixed-population geomean must improve beyond noise.
- No target or neutral-control row regresses by more than 3% in an interleaved
  11-pair follow-up unless a larger full-population benefit and root cause are
  documented before landing.

### T11-2 — Shared native-index and dense-read plan

**Purpose:** preserve numeric keys, reuse Lambda's index/extent reasoning, and
make dense reads pay only dense-read policy.

Analysis produces a native index plus one predicted receiver family. The first
families are:

1. Packed tagged Array, present own element.
2. Packed `ArrayNum` Float64 storage.
3. Fixed attached non-shared, non-resizable typed array, specialized by element
   kind.
4. ASCII String indexed character.
5. Unknown receiver with a native key, using `js_elements_get_int` or the
   corresponding native-key set completion rather than stringifying the key.

I64 keys must be in `0..2^32-2`. F64 keys must be finite, nonnegative,
integral, round-trip exactly to the machine index, and treat negative zero as
index zero. Fractional, negative, NaN, infinite, oversized, Symbol, and object
keys use normal JavaScript `ToPropertyKey` semantics.

Dense read hit:

```text
receiver/layout guard -> bounds -> own-presence/hole guard -> address -> load
```

Dense existing-element store hit:

```text
receiver/layout guard -> bounds -> own writable/default-data proof
    -> value representation admission -> address -> store
```

Append, hole creation, inherited setters, descriptors, sparse transition,
non-writable length, typed-array coercion not already proven, and observation
hooks stay in the set kernel. A tagged-array read checks a hole before loading;
a hole misses because JavaScript may find a prototype property.

The plan uses `em_element_address`/`em_array_element_address` and emitter-owned
root/final-store primitives under **D5.3.4**. JavaScript never emits Lambda's
`_cow` wrappers.

Primary rows: `text_search`, `matmul`, `array1`, `primes`, `spectralnorm`,
`triangl`, `hashmap`, `quicksort`, `fannkuch`, `nbody`.

Exit:

- Proven `a[i]` MIR contains neither `js_to_property_key` nor
  `js_property_lane_for_canonical_key` on the hit.
- The hit has one cold semantic miss, and the miss is dynamically exercised by
  holes, prototype accessors, fractional keys, sparse arrays, and detached or
  resizable typed-array fixtures.
- Read-only loops contain no write/COW/descriptor guard.
- `matmul` and `array1` reproduce the direction of the earlier matched A/B;
  `quicksort` is no more than 3% slower in an 11-pair follow-up.
- `text_search` must be measured separately because it controls half of total
  time; a change that cannot improve it needs census evidence explaining why.

**Implemented result:** `JsMirReference` retains an INT/FLOAT computed key
through one post-evaluation selector. A proven packed tagged literal read has
the direct receiver/bounds/address/load hit; noncanonical keys and every
unproven receiver family call the existing native-key or generic kernel once.
The fixtures exercise ordinary/typed/string semantic paths, prototype-visible
holes, `-0`, fractional, NaN, infinite, and oversized keys. The causal
`matmul` and `array1` rows are 0.5546x and 0.5785x, respectively (§2.4), with
no read-side store/COW/descriptor policy.

### T11-3 — Predicted exact-field plan

**Purpose:** let ordinary field hits use the same physical shape/slot route as
untyped Lambda while retaining stateless JS kernels on misses.

Candidate sources, in priority order:

1. Object-literal construction recipe.
2. Constructor `this` prefix with a stable ordinary layout.
3. Direct-call argument propagated from a candidate construction.
4. Direct-call return propagated from a candidate construction.
5. Module-unique eligible construction for the requested field.

The source walk is the existing `mir_shape_candidate` profile. A candidate is
a prediction, never proof. The emitted read guard validates the receiver kind,
exact shape identity or resolved recipe, slot bounds, field identity/offset,
ordinary data flags, and non-reserved/non-deleted state. A direct store also
validates that the value can inhabit the current physical slot without a shape
transition and preserves global/DOM observer hooks where applicable.

On a miss:

- static names call `js_get_name_id` or `js_set_name_id`;
- computed/private/super cases retain their current semantic paths;
- no receiver feedback, mutable site cache, patched MIR, or feedback vector is
  introduced, per **D8.4.1v2**.

Primary rows: `havlak`, `cd`, `splay`, `prettier_ast`, `richards`,
`deltablue`, `log_pipeline`, `three_way_merge`, `nbody`, `json`.

Exit:

- Focused MIR shows exact-shape guard + direct slot load/store + one NameId
  fallback.
- Shape transition, deleted fields, accessors, host properties, private names,
  Proxy behavior, constructor escape, and incompatible store values match the
  generic path.
- The candidate walk is shared; no JS copy of binding/argument/return traversal
  is added.
- At least two of `havlak`, `cd`, `splay`, `prettier_ast`, and `log_pipeline`
  improve materially with no greater than 3% regression in the target set.

**Implemented result:** literal and constructor candidates materialize an
immutable TypeMap recipe. A guarded ordinary field takes its direct slot
load/store only when the receiver/layout/value assumptions still hold;
otherwise it makes the one NameId semantic call. `native_field_plan` fixes the
direct and miss forms in MIR and output tests. The exact A/B gives
`log_pipeline` a 0.9865x ratio (0.9894x upper bound), while the `havlak`
neutral control is 0.9984x (0.9994x upper bound).

### T11-4 — ASCII string and builder leaves

**Purpose:** optimize the text rows only after T11-0 identifies their actual
post-index/post-field residue.

Result44's Text suite is 4.02x QuickJS and holds three of the five largest
absolute rows. Their timed work is different:

- `text_search` is numeric-code arrays and belongs primarily to T11-1/T11-2.
- `log_pipeline` repeatedly uses `split`, `indexOf`, `slice`, Number
  conversion, record field reads/writes, and aggregation.
- `three_way_merge` repeatedly splits ASCII lines into words, compares arrays,
  appends output words, joins strings, reads length, and uses `charCodeAt`.

Only encoding-neutral physical work is shared with Lambda:

- byte-span search under an ASCII proof;
- owner-backed or standalone `StrBuf` growth selected by lifetime, consistent
  with **D4.1.5**;
- exact-size split/join planning where a first pass is already required;
- byte-offset indexing when `String::is_ascii` proves byte index = UTF-16 code
  unit index;
- cached/canonical single-byte result strings if an existing shared cache is
  suitable and precisely rooted.

Unicode and observable JS behavior remain in the JS kernels: UTF-16 code-unit
length, surrogate pairs, normalization, regexp dispatch, coercion, and
allocation identity where observable. A non-ASCII string takes one cold
Unicode path; the ASCII loop does not branch per byte on every special case.

Primary rows: `log_pipeline`, `three_way_merge`, then `revcomp`, `regexredux`,
`fast_diff`, and `microdiff` according to the T11-0 profile.

Exit:

- ASCII and Unicode/surrogate fixtures are byte-for-byte correct against the
  current JS output and applicable Test262 cases.
- Profiles show the targeted string leaf actually dominates before it is
  changed.
- `log_pipeline` and/or `three_way_merge` improve beyond noise; no speculative
  String layout expansion lands without a measured payoff and a GC/layout
  audit.

**Implemented result:** the ASCII leaves and rooted substring cache above are
implemented without a String layout expansion. The cache is only an internal
reuse of immutable eligible substrings; Unicode stays on the semantic path.
`log_pipeline` is a measured A/B win. `three_way_merge` is a deliberate neutral
control at 1.0090x (1.0114x upper bound), safely below the 3% regression gate.

### T11-5 — Consolidate JS access flow after the plans work

**Purpose:** reduce compiler duplication without deleting profitable lanes.

Do this after T11-2/T11-3 have two live consumers, consistent with
**D8.2.3**. Preserve JavaScript evaluation-order and reference semantics in
their existing owners. Consolidate only the post-evaluation decision:

```text
(base MirValue, key plan, access mode, value/demand)
    -> direct selected plan or one JS semantic fallback
```

`jm_emit_reference`, `jm_emit_get_value`, `jm_emit_put_value`, and
`jm_emit_member_value` may keep separate setup for optional chaining, `super`,
private fields, suspension spills, and assignment completion. They should call
one shared JS property-access emitter after those semantics have produced the
base and key.

Likewise, binary lowering should route through one numeric-plan-or-boxed-
semantic decision instead of duplicating native-path predicates in consumers.

Cleanup rules:

- Promote an existing `static` helper to the module header when reused; never
  copy it.
- Delete a wrapper only after all clients use the plan replacement.
- Keep runtime kernels as semantic authorities even when benchmarks hit the
  direct path.
- Do not reduce LOC by removing comments, blank lines, tests, or profitable
  mechanisms.
- Every MIR-size increase is explicit in `test/mir/mir_budgets.json` under
  **D8.6.1**; decreases auto-tighten.

Exit:

- Fewer independent access-selection branches in JS lowering.
- No duplicated shape/index semantic implementation.
- No performance loss versus the pre-cleanup T11-2/T11-4 candidate.
- `git diff --check`, compiler timing, finalized MIR counts, Test262, Lambda
  baseline, and forced-GC gates pass.

**Implemented result:** reference construction remains the owner of
evaluation order, optional/super/private setup, and roots, while the selected
get/put decision is shared. Known-code function source setup takes the no-GC
setter, the module-unit index keeps that contract, and the former 130 related
safepoints/root stores are absent. The GC, MIR-budget, Test262, and Lambda
baseline evidence is recorded in §0 and §8.

### T11-6 — Full closeout and Result45 snapshot

The first full guarded snapshot after Tune11 is Result45, generated by the
standard runner rather than a hand-edited Result44 or sparse cell merge. It
uses one archived release binary for every Lambda/LambdaJS cell; no separate
cell refresh was used.

Closeout reporting includes:

- 63-row fixed population and any new rows reported separately.
- Workload-only and end-to-end geomeans.
- Sum of LambdaJS workload medians and top-row shares.
- LambdaJS/QuickJS, LambdaJS/Node, and LambdaJS/MIR-U.
- Per-phase primary-row A/B tables and neutral controls.
- Source manifest, archive SHA-256, Test262 actual regressions, missing timings,
  timeouts, and output mismatches.
- Remaining target misses and measurement limitations.

The historical Result44 values are diagnostics, not absolute timing gates,
because host load changes. Interleaved candidate/control ratios are the landing
gates. Directional program goals after T11-4 are:

- LambdaJS / QuickJS workload geomean: 2.91x -> at most 2.0x.
- LambdaJS / MIR-U workload geomean: 15.93x -> at most 10x.
- Sum of 63 LambdaJS medians: 233.2 s -> at most 160 s.
- No primary or neutral-control row with a confirmed regression above 3%.

If those goals are missed, the closeout states the residual; it does not relabel
a focused success as overall completion.

**Implemented result:** Result45 supplies the generated full matrix and
archive; the 11-pair archive A/B supplies the landing evidence. The QuickJS,
MIR-U, and total-median directional targets are missed, while the explicit
per-row no-regression gate passes. This proposal is therefore implementation-
and correctness-complete with quantified follow-on performance debt, not a
claim that Result44's aggregate gap is solved.

---

## 8. Validation matrix

| Gate | T11-0 | T11-1 | T11-2 | T11-3 | T11-4 | T11-5/6 |
|---|---|---|---|---|---|---|
| Focused output/golden | pass | `native_number_plan` | `native_index_plan` | `native_field_plan` | ASCII/Unicode contracts | consolidated flow |
| Focused MIR contract | trace-neutral | native arithmetic | packed read + miss | exact slot + miss | ASCII leaf branches | 27/27 emission |
| `JS_OPT_TRACE` census | release-neutral | numeric | native index | field plan | string profile | recorded |
| Forced-GC/self-baseline | pass | native carriers | read/rooting | field/rooting | cache rooting | 184/184 twice |
| Test262 baseline | — | — | — | — | — | 40,261/40,261 |
| Lambda baseline | — | — | — | — | — | 5,486/5,486 |
| Release A/B | archived Result45 | 0.5048x row | 0.5546x, 0.5785x | 0.9865x | 1.0090x neutral | full 63-row matrix |
| MIR emission budget | — | hard | hard | hard | hard | 19/19 ratchet |
| Compiler/profile diagnostic | text census | paired | paired | paired | paired | full closeout |

Additional invariants:

- All performance measurements use release builds.
- Instrumented profiles are not substituted for release timing.
- Test262 failures are fixed in the runtime; the harness is never changed to
  suppress them.
- A missed timing is a failure, not a speedup.
- Benchmark sources, loop counts, and expected outputs remain unchanged during
  an engine A/B.
- QuickJS and Node remain controls; their movement is reported alongside
  LambdaJS.
- C2MIR is a static reference and not a LambdaJS execution path.

---

## 9. Risks and rejected directions

### 9.1 Rejected: restore the pre-`85dcc897e` file wholesale

Why rejected:

- It restores a large JS-only semantic/guard dispatcher.
- Earlier A/B exposed a persistent `quicksort` and code-volume cost.
- It duplicates rules already owned by JS runtime kernels.
- It conflicts with the shared-plan direction of **D1.3** and
  extract-after-convergence in **D8.2.3**.

The old code remains useful as evidence and as a list of once-covered cases,
not as the patch to apply.

### 9.2 Rejected: put every case in one faster C helper

A native-key helper is a useful fallback for an unknown receiver, but it does
not let MIR combine loop induction, bounds, address arithmetic, and the load.
It also makes ordinary arrays pay the branch chain for strings and every typed
array class. Known common cases need compile-selected direct plans.

### 9.3 Rejected: inline caches or feedback vectors

**D8.4.1v2** explicitly bans mutable per-site caches and patched code in both
lanes. Tune11 uses immutable compile predictions, guards, and semantic misses.

### 9.4 Rejected: force JS through Lambda operators or containers

Lambda and JavaScript differ in truthiness, numeric behavior, captures,
mutation/COW, arrays, prototypes, descriptors, and errors. Sharing semantics
would violate **D1.3**, **S3.1**, **S9.1.4**, and the JavaScript guest contract.
Only physical plans and lower-level mechanisms are shared.

### 9.5 Rejected: optimize call dispatch first

R7RS is 1.22x QuickJS while Text, BENG, and AWFY are 4.02x, 5.62x, and 4.33x.
Call/result and explicit-completion work has already delivered improvements.
Method calls still benefit from T11-3 because their observable Get becomes
cheaper; Tune11 does not bypass Get or cache the callee by spelling.

### 9.6 Rejected: claim success from sequential ResultN movement

Result44/43 controls moved by 8–11%, and Result44 has a separately measured
LambdaJS column. Sequential snapshots rank suspects but cannot prove a tuning
patch. Every landing attribution uses identical sources and interleaved exact
archives.

---

## 10. Completion record

Tune11 is complete. Each original completion condition has evidence:

1. **Archived baseline:** Result45 is a generated, single-release-archive
   63-row snapshot with the SHA-256 in §2.4.
2. **Census:** numeric/index/field plan contracts, GC/root audits, and the
   string workload events are recorded in §0 and the focused contracts.
3. **Native numeric carriers:** `native_number_plan` proves direct arithmetic
   and boxed fallback; the interleaved `spectralnorm` row is a material win.
4. **Native keys and shared addressing:** `native_index_plan` proves the
   computed-key route and packed direct read; the `matmul`/`array1` A/Bs are
   material wins.
5. **Read-only hit:** its direct packed-read path contains no store, COW, or
   descriptor policy; exceptional cases call one semantic kernel.
6. **Exact fields:** `native_field_plan` proves guarded direct slots and the
   NameId miss, using immutable recipes rather than a per-site cache.
7. **Correctness/rooting:** Test262 is 40,261/40,261; both 184-test GC gates
   and the 5,486-test Lambda baseline pass.
8. **Consolidated flow:** `JsMirReference` supplies shared post-evaluation
   get/put selection without altering language-local reference semantics.
9. **Documentation:** [JS_04 — MIR Lowering](../../doc/dev/js/JS_04_MIR_Lowering.md)
   and [JS_15 — Performance](../../doc/dev/js/JS_15_Performance.md) record the
   implementation and measurement boundary.
10. **Generated reporting and residuals:** Result45 plus the exact A/B table
    reports every missing target and measurement limitation. The aggregate
    target miss is follow-on performance work, not an unreported gate failure.

---

## Appendix A — Implemented code ownership

This appendix records the landed ownership boundary, not a new ruling.

| Area | Implemented ownership |
|---|---|
| Shared representation, guards, addresses, demand, root/final store | `lambda/runtime/mir_emitter_shared.hpp` |
| Shared construction/field candidate and plan | `lambda/runtime/mir_shape_candidates.hpp` |
| Shared numeric plan facts promoted from Lambda | `lambda/runtime/mir_emitter_shared.hpp`; no JS semantics |
| Lambda plan admission and COW/certificate policy | `lambda/runtime/transpile-mir.cpp` |
| JS numeric/access admission and evaluation semantics | `lambda/js/js_mir_expression_lowering.cpp` and existing JS MIR modules |
| Stateless semantic fallback | existing `js_get_reference`, `js_get_name_id`, `js_set`, `js_set_name_id`, element/string helpers |
| Profiling-only census | `lambda/js/js_exec_profile.h/.cpp` plus focused `test_js_opt_gtest` fixtures |
| MIR contracts | `test/mir/js/*.mir-check` and `test/mir/mir_budgets.json` |

Before adding any new helper, search for an existing `em_*`, native-index,
shape-candidate, array address, or runtime semantic helper. At the first second
client, promote the existing static helper; never copy it. No vendored MIR or
Tree-sitter source is modified.

## Appendix B — Implemented MIR contracts

The exact register names and raw immediates are not contracts under
**D8.6.2**. Sidecars should assert stable call/instruction shapes.

### B.1 Numeric admission

For a proven Number loop:

- expect native arithmetic/compare instructions;
- forbid boxed `js_add`/`js_multiply`/`js_compare` in the loop function;
- allow boxed helpers in the cold miss function/block only when admission is
  guarded rather than statically proven.

### B.2 Index admission

For a proven packed numeric array read:

- expect receiver/layout guard, bounds branch, element address, and load;
- forbid `js_to_property_key` and property-lane classification on the hit;
- expect exactly one `js_get_reference` or native-key semantic fallback on the
  miss;
- separately assert that a read-only function lacks the store guard.

### B.3 Field admission

For a predicted ordinary own field:

- expect exact shape/layout guard and direct slot access;
- expect `js_get_name_id`/`js_set_name_id` on the miss;
- forbid mutable cache cells, feedback-vector writes, and patched targets.

### B.4 Instrumentation neutrality

Trace-on and trace-off executions must:

- print identical normalized output;
- produce the same finalized non-probe MIR contract;
- write no trace artifact when tracing is disabled;
- compile profiling calls out of release timing builds.

## Appendix C — Implementation order completed

The completed sequence was intentionally narrow:

1. T11-0 trace schema and clean release baseline.
2. Numeric compare/update plan on `primes` and `spectralnorm` only.
3. Shared native-index expression plus a packed tagged-array read on
   `spectralnorm`, `matmul`, and `array1`; other receiver families retain their
   existing native-key semantic helpers.
4. Keep existing-element stores in their existing JS semantic kernel. The new
   read plan deliberately does not pay write/COW/descriptor policy.
5. Reconnect JavaScript to `mir_shape_candidate` for read-only literal fields;
   add stores only after read gates pass.
6. Profile-driven string leaf work for `log_pipeline` and
   `three_way_merge`.
7. Consolidate access-selection flow and update detailed design docs.
8. Full guarded ResultN closeout.

Each step kept one strong reproducer, one aggregate slice, and explicit neutral
controls. The final Test262, Lambda, forced-GC, emission, and full benchmark
gates are recorded in §0, §2.4, and §8.
