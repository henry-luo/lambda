# JS MVP class workload tuning

2026-10-08. Implements [MVP §23](../jube/JS_MVP_Lmd.md#23-class-workload-tuning).
Authority: **S1.11**, **D3.4.3v5–D3.4.5**, **D5.2–D5.3**,
**D6.2.3v2–D6.2.4**. No language ruling changes.

## Changes and limits

- **Property caches:** four exact immutable shapes per site, with round-robin
  replacement. Each entry records its field, physical storage type/offset,
  inherited callable and writability. The existing shared shape classification
  excludes optional/accessor layouts from inline scalar access. Shape equality
  remains mandatory; deletion, retyping and method shadowing invalidate the
  corresponding entry naturally.
- **Scalar access:** ordinary int53, Float and Boolean fields use shared MIR
  representation primitives. Out-of-band integer lanes and other layouts use
  the existing shared reader. Numeric consumers read directly into native
  registers. Native stores check the current shape and exact storage type after
  evaluating the RHS, boxing only on the fallback. Borrowed values still receive
  an owned scalar home before mutation or a safepoint.
- **Pointer access:** ordinary Array, typed-array, Map, Function and String
  fields load/store their pointer lane directly. Null canonicalization and
  legacy raw-header repair retain the shared reader. This removes the extra
  numeric dispatch that initially regressed array-field-heavy workloads.
- **Small method expressions:** extend the existing expression inliner for
  capture-free, parameterless methods. A guard compares the already captured
  callee's entry address with this program's method entry. Arguments, including
  extra arguments, run first. The inlined body receives the original receiver;
  method replacement and unmatched targets use the ordinary call path. Statement
  bodies and lexical `super` are excluded from this optimization.
- **Constructor allocation:** after one successful straight-line constructor,
  reuse its completed shared shape with `mvp_lmd_object_new` /
  `map_alloc_for_type`. The proof admits only ordinary field assignments whose
  values contain literals and receiver-free identifiers/operators.
  Calls, receiver reads/escape, computed fields, branches, explicit returns and
  derived construction keep incremental initialization. Different value types
  still take Lambda's existing shape transitions. GC sees zero-initialized
  storage until each field is installed.
- **Map iteration:** derive the cursor stride once from the captured iterable.
  Reload the current entries and length on each iteration, then read present
  entries directly. Deleted entries retain the existing `mvp_lmd_map_next`
  helper. Active-cursor bookkeeping and live mutation semantics are unchanged.
- **Repeated Map reads:** reuse the existing `last_entry` ordinal cache for
  guarded `get`/`has` calls. Exact Item equality on a present entry avoids a
  runtime call; other representations, canonicalization and SameValueZero
  matching retain `mvp_lmd_map_call`. Recheck bounds, liveness and the key after
  argument evaluation. Borrowed scalar results retain the existing adoption.

The new helpers, disclosed before coding, are `mvp_lmd_cache_property` (internal
cache population) and `constructor_layout_reusable` (compiler proof). Existing
`property_access`, `read_reference`, `write_reference`, `call`, `inline_body` and
`mvp_lmd_class_invoke` were extended. No new JIT import, full LambdaJS runtime
dependency, closure representation, VMap or conservative root path is added.

The initial single-shape scalar optimization alone produced limited gains.
Constructor layout reuse and retaining several shapes address additional
costs: repeated initialization transitions and cache misses when nullable or
numeric fields change a receiver's shape. Intermediate six-pair screens are
diagnostic evidence; acceptance uses the longer comparisons below.

NBody illustrates the interaction: the Sun starts with integer position fields,
while other bodies start with Float positions. Starting later bodies from the
completed constructor shape makes their x/y/z retyping follow the same transition
path that the Sun subsequently takes. This reduces shape diversity as well as
initialization work; merely shortening scalar helper calls missed this cost.

## Measurement provenance

Artifacts: `temp/mvp_callback_tuning_20261008/`. The control was built from
clean HEAD `7268641b3ba644d21fe9a1fe66b481a1fa8d497e` before these edits.
The previous closure release is also retained as `control.exe`; causal
comparisons use the rebuilt `base.exe` against the same shared source tree.

| Binary | SHA-256 |
|---|---|
| `base.exe` | `3fbfaa1e7d57f51ce35c15ebaa3d2b84cd9d212b7928fd6564a53e6da3988b3b` |
| `release.exe` | `c0953ebc4326e6656e6d6e8b24c36c58941fb4cd57ce1b50bb69ff7ff8a2e5c9` |

`release.json` and `release-source/` record the exact changed sources.
The earlier candidate binaries and comparisons retain intermediate measurements.
All comparisons use release binaries, pinned native MIR, fresh processes,
one discarded process per lane and self-reported execution time. Candidate,
control and identical-control peer orders cycle through all six permutations.
Outputs and finalized MIR are checked and archived. Startup and initial MVP
compilation are excluded; Node tiering during execution remains included.

The six-workload runner reuses the previous frozen complete bundles and native
oracles. MVP and Node include class setup and the oracle; Lambda retains its
canonical port timer. Bounce uses parallel arrays in Lambda; CD uses indexed
nodes in Lambda and objects in JS. These are port comparisons, not isolated
compiler comparisons. The five AWFY references are untyped; SHA1's canonical
Lambda port retains explicit integer annotations and is labeled separately.

### Six recent workloads

Eighteen measured processes per lane, milliseconds; all output checks pass.
The intervals are paired bootstrap candidate/control ratios, two-sided 95%.

| Workload | Before | After | Reduction | Ratio interval | Lambda reference | Node |
|---|---:|---:|---:|---:|---:|---:|
| Bounce | 1.063 | 0.819 | 23.0% | 0.7634–0.7740 | 0.068 | 0.723 |
| NBody | 95.512 | 45.635 | 52.2% | 0.4748–0.4804 | 7.437 | 5.380 |
| Richards | 191.898 | 100.799 | 47.5% | 0.5234–0.5272 | 302.754 | 8.233 |
| CD | 635.779 | 380.495 | 40.2% | 0.5940–0.6016 | 618.824 | 35.729 |
| Storage | 0.549 | 0.513 | 6.5% | 0.9000–0.9484 | 0.592 | 0.683 |
| SHA1 | 14.506 | 13.881 | 4.3% | 0.9482–0.9721 | 51.551* | 8.769 |

*SHA1's Lambda reference is annotated, not untyped. Node v22.13.0. Identical
control peer ratios range from 0.9898 to 1.0099. Evidence:
`release-six/comparison.json`, frozen binaries/sources, raw outputs and MIR.
MVP remains 1.13×/8.48×/12.24×/10.65× Node's time on
Bounce/NBody/Richards/CD, respectively. General boxed calls and coercion,
runtime allocation and shape transitions remain tuning opportunities.

## Acceptance

All **60** workloads pass their output checks. Thirty balanced pairs across
the previous 54 workloads show no statistically confirmed regression
(`release-48`, `release-classes`). Sixty pairs of three shared Lambda clients
also show no confirmed slowdown (`release-shared-lambda`). The current release
passes **54/54** MVP tests normally and with
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` (`tests-cache10.log`,
`tests-cache10-gc.log`). New cases cover cached scalar retyping/deletion, signed
zero and subnormal ownership, method override/argument effects, receiver
identity, constructor type variation and observation of partial initialization,
and Map cache invalidation after deletion, clear and argument effects.

The initial broad comparison found `permute` 1.7% and `queens` 2.7% slower:
their array-valued fields paid new scalar checks before the existing reader.
The pointer path fixes that cost. The final thirty-pair comparison measured
reductions of 10.7% and 6.9%, respectively; Towers improved 7.1% and List 34.9%
(`release-classes`). A 0.35% `map_iteration`
signal from the first 48-workload run persisted in longer comparisons; its
finalized MIR differed only in embedded addresses, and its native cause was
not isolated. Removing independently identified repeated iterator work reduced
the final candidate's time from 1.409 to 1.298 ms (7.9%; 120 pairs, ratio interval
0.9205–0.9226).

An intermediate release also showed `map_lookup` 2.3% slower in 120 pairs,
with unchanged finalized MIR apart from addresses (`verified-tail-mir-audit.json`).
Its native cause remains unisolated. The guarded Map read optimization removes
runtime calls on existing exact-key cache hits; the final 120-pair result is
8.378 → 7.921 ms (5.5% less time; ratio interval 0.9375–0.9524).
The same final-binary comparison covers `deriv`, Base64, JSON, Levenshtein and
Spectralnorm, and shows no confirmed slowdown (`cache10-confirm`). Small
differences near timer resolution or the control/control noise remain uncertain.

Final aggregate gates pass **6,459/6,459** combined Lambda/input tests
(including 2,112 input tests), and **40,261/40,261** Test262 tests with zero
partial results, retries or failed batches. Evidence: `lambda-baseline-release.log`,
`test262-baseline-release.log` and `release-gates.json`; `accept.py` records each
command, exit status and timestamp. `acceptance.json` consolidates the final
comparisons, gates and source/binary hash checks. The current `lambda.exe`
matches the measured `release.exe` exactly.

The first intermediate Test262 run needed retries for two slow Unicode
identifier cases; that run was not a clean acceptance gate. Both subsequent
aggregate runs completed cleanly.

No numbered MVP result is replaced by this tuning record.
