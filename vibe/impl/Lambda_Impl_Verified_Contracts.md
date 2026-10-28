# Persistent verified record contracts

**Date:** 2026-10-28. **Status:** implemented; qualified with two pre-existing baseline failures.
**Design:** [Type Enforcement TE-19.1](../Lambda_Design_Type_Enforcement.md#te-191--persistent-contract-proofs-in-runtime-descriptors).
**Authority:** S11.4.1v3, S11.4.10, S2.1.4, S11.3.1v2;
D3.2.4v4, D3.2.6, D3.4.3v5, D3.4.5.

## Root cause and scope

The exact trusted descriptor already bypassed validation. Open records lost
that proof: admission required equal field counts, and adding fields cleared
the trusted flag. An ordered two-field contract with one extra field paid 101
reifications and 202 field visits in a 100-call interpreter fixture. Reordered
input with extras also retained the wrong layout: the control printed a corrupt
integer and crashed on the regression fixture.

The change preserves a verified prefix on an immutable extended `TypeMap`.
Admission reorders declared fields by name and retains extras. Compatible
transitions and COW copies keep the proof; invalidating writes lose it. The
runtime and MIR boundary guard recognize the prefix certificate. Existing
statically proven field/array accesses continue to carry their proofs.

Already compatible records also publish the descriptor once. With no extras,
this only replaces the type pointer after COW ownership is established; with
extras, it installs the extended shape. Otherwise the existing early proof
return would keep consulting the shape-relation cache at every boundary.

## Implementation

- `lambda/lambda-data.hpp`: optional `TypeMap::verified_contract`, shared
  storage comparison, and conservative proof preservation.
- `lambda/input/input.cpp`: transition identity, append/retype propagation,
  and certificate-free descriptor detachment for raw mutation.
- `lambda/runtime/type_contract.cpp`: open-field compatibility and constant-time
  certificate recognition.
- `lambda/runtime/lambda-eval.cpp`: canonical declared-prefix layout with extras,
  runtime admission reuse, and same-tag replacement invalidation.
- `lambda/runtime/transpile-mir.cpp`: exact-shape hit first, then verified-prefix
  hit, with the normal checked boundary on a miss.

The existing bounded transition tree shares enriched descriptors. A declined
edge uses a private shape, preserving the same semantics. Container size and
nominal identity are unchanged; a descriptor gains one pointer (8 bytes on the
qualified 64-bit host). This implements one record-prefix proof;
arbitrary refinement sets, predicate-result caching, and new element-content
certificates remain outside scope.

## Qualification

The clean control is commit `d53f1a0437202ad348b58af21bae3f96b07f07f2`, built with `make release`.
The final candidate uses the same release configuration, pinned separately from
`lambda.exe` before the debug regression gate. Qualification is macOS arm64;
Windows and Linux were not measured.

- Control SHA-256: `a777d962b7da4de2becd86564b26156690fb6cca963d4d4574b504b90a002da3`.
- Candidate SHA-256: `58820152992fe1ab4740510f66272e792930e9ffc8132fbf27378f3086a4f162`.
- Artifacts: `temp/contract-proofs/final-pairs.json`, `final-manifest.json`,
  `final-provenance.json`, and `final-output-checks.json`.
- Method: JIT pinned, MIR import cache disabled, one warmup pair and 21
  alternating measured pairs per row. The 95% intervals use the runner's
  paired bootstrap (10,000 resamples). Builds and regression tests did not
  run alongside timed samples.
- Completion: 17/17 rows, 357/357 measured pairs valid; all 748 outputs,
  including warmups, match exact oracles. Ten existing rows also match their
  `.txt` goldens; the four JetStream rows report their internal PASS checks.
  All binary, source, build and native-module stability checks passed.

### Repeated-boundary workloads

Each targeted row makes 100,000 calls through an erased argument into a declared
record binding. `open` has a structural contract plus an extra field, `exact`
has the same declared fields with no extras, and `nominal` extends a nominal
instance before the timed loop. Scripts and goldens are retained in
`temp/contract-proofs/benchmarks/`.

| Workload | Control ms | Candidate ms | Speedup | Candidate/control 95% interval |
|---|---:|---:|---:|---:|
| open | 20.248 | 0.639 | 31.7x | 0.0311–0.0322 |
| exact | 2.334 | 0.642 | 3.6x | 0.2668–0.2835 |
| nominal | 13.752 | 0.647 | 21.3x | 0.0465–0.0492 |

The 100-call counter probes (`profiles-final.json`) explain the gains in both
tiers: open-record field visits fall from 202 to 0 and reifications from 101
to 0; nominal field visits fall from 200 to 0 and reifications from 100 to 0.
Already compatible exact records previously had 100 relation-cache hits; the
candidate has only 1 (JIT) or 2 (interpreter), at initial admission. The new
nominal path performs no runtime admission calls during the loop.

### Existing benchmark peers

Ratios below 1 are faster. These unchanged workload rows do not establish a
broad typed-Lambda speedup. Typed Splay improves 0.8%; AWFY DeltaBlue regresses
1.3% untyped and 1.6% typed, with intervals above 1. Other intervals include 1.
The maximum observed median regression in this set is 1.6%. This qualification
covers these seven workloads and both variants, not the full benchmark matrix.

| Suite/workload | Variant | Control ms | Candidate ms | Ratio | 95% interval |
|---|---|---:|---:|---:|---:|
| awfy/deltablue | untyped | 80.117 | 81.133 | 1.0127 | 1.0092–1.0233 |
| awfy/deltablue | typed | 19.066 | 19.370 | 1.0159 | 1.0081–1.0197 |
| beng/fannkuch | untyped | 0.277 | 0.278 | 1.0036 | 0.9928–1.0109 |
| beng/fannkuch | typed | 0.293 | 0.292 | 0.9966 | 0.9798–1.0104 |
| larceny/pnpoly | untyped | 2.348 | 2.348 | 1.0000 | 0.9940–1.0056 |
| larceny/pnpoly | typed | 2.570 | 2.601 | 1.0121 | 0.9892–1.0236 |
| text/microdiff | untyped | 32.543 | 32.459 | 0.9974 | 0.9940–1.0044 |
| text/microdiff | typed | 30.238 | 30.092 | 0.9952 | 0.9900–1.0014 |
| text/jq_records | untyped | 332.441 | 333.325 | 1.0027 | 0.9932–1.0195 |
| text/jq_records | typed | 387.306 | 386.556 | 0.9981 | 0.9892–1.0092 |
| jetstream/splay | untyped | 188.881 | 189.781 | 1.0048 | 0.9982–1.0075 |
| jetstream/splay | typed | 207.778 | 206.196 | 0.9924 | 0.9893–0.9967 |
| jetstream/deltablue | untyped | 986.505 | 987.371 | 1.0009 | 0.9969–1.0050 |
| jetstream/deltablue | typed | 14.434 | 14.376 | 0.9960 | 0.9856–1.0024 |

### Emission cost

`mir-review.json` records the control/candidate MIR comparison for typed
JetStream DeltaBlue. The new prefix guard adds two instructions after an
exact-shape miss at 28 sites: module 10,401 → 10,457, and
`constraint_choose_method` 1,765 → 1,775. The first exact-shape comparison still
handles the common canonical descriptor. The measured default ratchet budget
records this intentional change; the Windows budget remains unchanged.

### Correctness gates

| Gate | Result |
|---|---|
| `make test262-baseline` | 40,261/40,261; exit 0, zero regressions |
| `make test-lambda-baseline` | 8,372/8,375 in the final full run; details below |
| Optimization suite after the expected profile-count update | 55/55 |
| Transition-tree retype suite | 4/4 |
| Forced-GC regression and fallback fixtures | 4/4 runs, both tiers |
| MIR emission / size ratchet / forced-GC corpus (within the Lambda gate) | 277/277, 20/20, 348/348 |

The full Lambda run had two demo golden failures plus one outdated optimization
profile assertion. The latter expected one packed-field fast hit and three
generic reads. Both admitted records now have the trusted descriptor, so the
correct result is two fast hits and two generic out-of-range reads. Observable
output remains correct; the assertion was updated and the full 55-test
optimization suite rerun successfully. The aggregate was not repeated after
that test-only update. Logs: `lambda-baseline-qualified.log`, `opt-final.log`,
and `test262-final.log` under the artifact directory.

The remaining demo failures reproduce on the unchanged release control:
`tests_greetings_model` produces identical control/candidate output that differs
from its golden; `tests_procart_foliage` is nondeterministic in both builds and
fails its golden in 2/3 control and 3/3 candidate runs. They were not rebaselined
or changed (`known-baseline-failures.json`). The preceding full run, before the
final compatible-descriptor adoption change, was 8,373/8,375 with these same two
failures. No claim of a fully green Lambda baseline is made.

The final Test262 build is byte-for-byte identical to the pinned candidate
used for all final benchmark pairs. Focused checks establish:

- The regression fixture passes in JIT and interpreter modes with
  `LAMBDA_GC_FORCE_EVERY=1` and `LAMBDA_GC_POISON_FREED=1`. It covers reordered,
  already-compatible and extended records, repeated crossings, COW snapshots,
  nested replacement, nominal methods/inheritance, and rejection after a raw
  same-tag incompatible replacement.
- A 24-shape forced-GC fixture passes in both tiers, including eight declined
  external transitions. Private fallback descriptors preserve the same layout
  and values (`fallback-final-*-shapes.tsv`).
- All four `TransitionTreeRetypeTest` tests pass, including the new certificate
  preservation/invalidation and sibling-isolation test.

### Remaining limits

One canonical record prefix is retained. Predicate-bearing nominal contracts,
differing nominal identities, binder-dependent admission and exotic property
metadata retain the general path. This does not add new array or element-content
certificates. Shared roots still use COW; repeatedly discarding the admitted
copy can still pay admission on the original unverified alias. The existing
benchmark results above retain the small observed regressions rather than
claiming universal improvement.

## Follow-up: typed/untyped proof-use audit

**Date:** 2026-10-28. **Status:** analysis complete; the tuning candidates below
are not implemented or speedup-qualified. Authority remains S11.4.10,
D3.2.4v4 and D3.2.6. This audit changes no language ruling.

### Latest recorded result and current reproduction

The latest full snapshot is
[`benchmark_results_v54.json`](../../test/benchmark/benchmark_results_v54.json),
completed 2026-10-24 at 12:17:43. Its raw JIT workload medians contain **14/71**
rows where typed is slower. Earlier prose counted 13: `iteration_pi_sum`
adds a fourteenth by only 0.002 ms. Raw median ordering alone is not evidence
of a repeatable regression.

Reproduced all 14 pairs on commit `b8f17f80efb0efa9408483872bff737544f8f019`,
using the qualified release candidate above for **both** source variants.
This is a current typed/untyped comparison, not a before/after optimization
comparison. JIT pinned, MIR cache disabled, one warmup and 21 alternating
measured pairs per row; no concurrent build or profiling. All 294 measured
pairs completed, all 616 outputs including warmups matched their oracles,
and the runner's binary/source/build/module stability checks passed.

Artifacts are under `temp/proof-usage/`: `provenance.json`,
`result54-slower.json`, `typed-peer-manifest.json`, and `typed-peers.json`.
The executable hash is
`58820152992fe1ab4740510f66272e792930e9ffc8132fbf27378f3086a4f162`.

| Workload | Result54 typed overhead | Current untyped ms | Current typed ms | Typed/untyped | Paired 95% interval |
|---|---:|---:|---:|---:|---:|
| r7rs/fib | +0.62% | 1.224 | 1.227 | 1.0025 | 0.9903–1.0164 |
| r7rs/tak | +0.70% | 0.128 | 0.133 | 1.0391 | 1.0000–1.0859 |
| r7rs/cpstak | +2.47% | 0.271 | 0.265 | 0.9779 | 0.9706–1.0368 |
| awfy/mandelbrot | +0.27% | 35.123 | 35.058 | 0.9981 | 0.9966–1.0011 |
| beng/fannkuch | +5.43% | 0.276 | 0.295 | 1.0688 | 1.0429–1.0761 |
| beng/pidigits | +1.66% | 0.364 | 0.367 | 1.0082 | 0.9918–1.0277 |
| larceny/array1 | +0.37% | 0.803 | 0.806 | 1.0037 | 1.0025–1.0254 |
| larceny/divrec | +2.66% | 1.183 | 1.170 | 0.9890 | 0.9659–1.0171 |
| larceny/pnpoly | +60.87% | 2.338 | 2.591 | 1.1082 | 1.0926–1.1536 |
| larceny/ray | +4.31% | 0.210 | 0.217 | 1.0333 | 1.0188–1.0796 |
| julia/iteration_pi_sum | +0.07% | 2.885 | 2.884 | 0.9997 | 0.9927–1.0137 |
| text/microdiff | +12.99% | 32.242 | 29.811 | 0.9246 | 0.9167–0.9300 |
| text/jq_records | +17.21% | 327.487 | 377.711 | 1.1534 | 1.1483–1.1660 |
| jetstream/splay | +16.41% | 190.277 | 207.359 | 1.0898 | 1.0845–1.0941 |

Six current intervals lie wholly above 1: the four main targets are
`jq_records`, `pnpoly`, `splay`, and `fannkuch`; `array1` and `ray` add small
absolute gaps of 0.003 and 0.007 ms. `microdiff` is now 7.54% faster typed.
The comparison with Result54 spans other intervening changes and host runs;
it does not attribute those improvements to persistent map proofs.

### Which proofs are actually used?

Separate instrumented runs captured caller-attributed execution/COW counts,
checked their outputs, and retained uninstrumented MIR for the five main
pilots. Counts cover the whole invocation, including setup; they are not
percentages of timed execution. See `profiles.json`, `small-profiles.json`,
and the `*.calls.tsv`, `*.sites.tsv`, `*.cow.tsv`, and `*.mir` artifacts.

| Typed workload | `lambda_type_check` calls | Map admissions | Map fields visited | Map reifications |
|---|---:|---:|---:|---:|
| splay | 1 | 1 | 0 | 0 |
| jq_records | 524,881 | 120,015 | 0 | 0 |
| microdiff | 61,500 | 41,000 | 0 | 0 |
| pnpoly | 2 | 0 | 0 | 0 |
| fannkuch | 0 | 0 | 0 | 0 |

Record admission reuse is effective. In particular, Splay's one runtime check
is at initialization; its recursive record crossings are not repeatedly
walking fields. The jq and microdiff map admissions take the storage-compatible
path without visiting fields. Their broad `map` contracts do not declare a
fixed record layout for the new prefix certificate to establish.

At this audit's control revision, persistent proofs were **not fully consumed by the compiler**.
The highest-confidence record-specific gap is nullable-root stores.

### 1. Splay: use the verified nullable record at stores

The root member-store gate in
[`transpile-mir.cpp`](../../lambda/runtime/transpile-mir.cpp)
(`AST_NODE_MEMBER_ASSIGN_STAM`) requires
`mir_unwrap_decl_type(cow_root->full_type)->type_id == LMD_TYPE_MAP`.
That helper does not reach through `SplayNode?`. Consequently the direct
writer's full field-contract and storage-lane proofs are never considered
for this root, even after a null guard. This is the consumer case explicitly
covered by D3.2.4v4's non-null-arm requirement.

A small executable probe, `controls/optional_write.ls` with its `.txt` oracle,
writes 1,000 times through each of `Record` and `Record?`, with an explicit
null return in the latter. Both JIT and interpreter produce the expected
values, including the null case. MIR and execution counters show:

- Required record: direct store, zero `fn_map_set` calls.
- Optional record: 1,000 `fn_map_set` calls and 3,000 root stores.

Typed Splay makes 562,197 `fn_map_set` calls. Of these, 526,200 come from
`rotate_right`, `rotate_left`, and `splay_node`, whose root parameter is
`SplayNode?`. Reuse `lambda_type_nonnull_map_contract` to expose the admitted
layout to the existing writer. A fast store must still establish a non-null
receiver, unique/COW-safe ownership, the incoming **full** field contract,
and the correct physical lane; retain the checked fallback for every miss.
This count is a tuning opportunity, not a claim that every call can be removed.

A smaller read gap remains: `greatest.key` in `run_splay` attempts 3,999 inferred
shape guards and hits zero. `splay_find_greatest_less_than` declares
`SplayNode?`, but the unannotated local loses that return-layout proof at the
field-read consumer. Recover a successful declared return through a stable
definition, using the existing call/definition proof machinery, while
retaining null/error and mutation guards. Do not trust an inferred literal
shape as a declared layout.

Both variants still perform 951,168 COW map copies. A type/layout certificate
does not prove ownership, so dropping these copies is not licensed by
S11.4.10 or D3.2.6.

### 2. jq_records: preserve array producers and simplify broad boundaries

Two additional 21-pair controls separate annotations from source differences.
The existing annotation eraser was applied to the typed main script **and**
its typed helper module; explicit conversions and algorithms were retained.
All 42 measured pairs and all 88 outputs including warmups passed, with stable
inputs. These temporary diagnostic sources do not replace canonical tests.

| Comparison | Control ms | Candidate ms | Ratio | Paired 95% interval |
|---|---:|---:|---:|---:|
| Same typed algorithm: erased → annotated | 329.361 | 377.887 | 1.1473 | 1.1345–1.1551 |
| Canonical untyped → erased typed algorithm | 331.984 | 332.951 | 1.0029 | 0.9923–1.0251 |

Artifacts: `jq-controls-manifest.json`, `jq-controls.json`, and `controls/`.
Annotations add about 14.7% on this input; the source differences alone are
inconclusive. This is not evidence that erasing annotations preserves the
program's contracts on arbitrary inputs, or that one proposed fix recovers
the entire gap.

The 524,881 checks include 80,010 each in `transform_order`, the boxed
`jq_slice_string` wrapper, the boxed `jq_at` wrapper, and `round_digest`;
79,515 in the boxed `collect_numbers` wrapper; and 40,020 each in the boxed
and ordinary `jq_unique_strings` entries. These counts identify consumers to
inspect; a wrapper can check multiple arguments, including nullable bounds.

Two concrete directions follow:

- Let broad `array`/`map` boundaries reuse a proven carrier, or perform an
  exact kind guard with the checked error/conversion path on a miss.
  `emit_checked_boundary` already has scalar and trusted-record guards;
  plain containers currently retain more runtime calls. A zero-tag pointer
  alone is not enough to distinguish container kinds or establish `T[]`.
- Preserve the physical array representation through `sort`/`unique` when
  possible, and propagate the resulting exact element/rank proof to return
  and wrapper consumers. `jq_unique_strings` returns `unique(sort(xs))` as
  `string[]`. In `lambda-vector.cpp`, the `Array*` overload of
  `array_transform_copy_cert` intentionally clears the certificate because
  its result has boxed Item slots. Simply copying a native pointer-lane
  certificate onto that result would violate D3.3.3v3. An implementation must
  preserve/rebuild the lane or carry a separately valid semantic proof.

Typed jq also makes 120,030 extra `fn_string` calls, but the erased-source
control retains those expressions and does not explain the main gap. The
record-prefix certificate alone cannot optimize these array/scalar paths.

### 3. Numeric workloads: other proof consumers

- **pnpoly:** only two checks, both startup array admissions. All 100,000
  dense-loop attempts hit the fast arm in both variants. Typed execution
  nevertheless makes 100,003 stack-check helper calls versus 3, with
  100,023 root stores / 200,002 reloads versus 19 / 2. Its hot function has
  a 128-byte root frame plus a number frame in the emitted MIR. Investigate
  whether existing array/bounds proofs permit a success path without
  allocation/root-frame work, keeping the nullable/out-of-bounds/error path
  correct. Repeated nominal validation is not the cause.
- **fannkuch:** zero runtime type checks. Dense-loop attempts are 32,208 typed
  versus 5,040 untyped, all successful; root traffic is essentially equal.
  Inspect nested loop versioning and repeated guards under stable array and
  bounds proofs. The counts expose different generated work, not a measured
  attribution of the full 6.9% gap.
- **array1 / ray:** the focused profiles show no hot repeated type checks
  (zero / four startup checks), and all recorded dense attempts succeed.
  Their small gaps have lower priority than the four main workloads.
- **microdiff:** now faster typed, although 61,500 broad checks remain.
  Include it as a peer when changing generic container boundary handling.

For the next implementation, start with nullable-record stores and the stable
returned-record read in Splay, then the jq boundary/array-producer work.
Qualify each independently against a pinned release control, retain both
typed and untyped peers, and run the engine correctness gates after changes.
No benchmark sources or engine code were changed for this audit.

## Compiler consumers follow-up (2026-10-28)

This follow-up implements the record and array consumer opportunities above,
under S11.4.10, D3.2.4v4, D3.2.6 and D3.3.3v3. The audit and its timings remain
the historical control evidence.

- **Record reads:** one shared MIR layout guard accepts exact descriptors and
  verified declared prefixes. A declared return supplies a guarded layout
  candidate even when an early AST retained an inferred shape. Invalidated,
  rebound, null and error receivers retain fallback handling.
- **Record writes:** optional declared roots expose the non-null record to the
  existing full field-contract/lane proof. Unique roots use direct stores;
  shared roots reuse the canonical COW setter. Unproven values use checked
  writes. The null branch evaluates the RHS and reports the original failure.
  Runtime/interpreter optional-root dispatch also resolves the declared field
  before mutation, preventing a rejected value from corrupting a native slot.
- **Proof lifetime:** eliminating a check requires an enforced declaration or
  a whole-definition proof. Direct/nested writes and mutable argument paths
  invalidate inferred local proofs; member chains require a live owner proof.
  Immutable explicit-return definitions remain reusable. Cyclic definition
  analysis fails conservatively, and nullable owners cannot prove a required
  field value without a non-null fact.
- **Array boundaries:** rank-one uncounted contracts test certificate rank,
  flags, element identity and lane, plus the live carrier/representation.
  Broad `array`/`map` and optional scalar contracts use exact kind/null guards.
  Misses retain the ordinary diagnostic, conversion and validation path.
- **Array producers:** reverse, slice, sort and unique restore the native lane
  after boxed staging before publishing the source certificate. This shares
  concat's existing lane-rebuild mechanism. Count-changing transforms recheck
  the certificate and clear it when its shape no longer holds.

The compiler still retains full admission for counted/multidimensional and
binder-dependent contracts at these new boundary guards. A certificate proves
neither unique ownership nor arbitrary predicates. The numeric-loop/root-frame
opportunities identified for `pnpoly` and `fannkuch` are separate work.

Two MIR fixtures, `persistent_record_consumers` and
`persistent_array_consumers`, cover the new paths and their checked fallbacks.
They include open-record extras, COW snapshots, null RHS effects, direct/nested
and borrowed mutations, wrong container kinds, nullable scalars, counted arrays,
wrong element/rank, native pointer arrays and certificate invalidation.

Qualification artifacts for this follow-up are under `temp/proof-consumers/`.
The control is the previous persistent-proof implementation at `b8f17f80ef`;
both binaries use release compilation. `final-provenance.json` records binary
and runtime diff hashes; `profiles.json` and the saved `.mir` files capture
mechanisms separately from timed runs. Final qualification results follow below.

### Release benchmark qualification

The final run completed **35/35 rows, 735/735 valid measured pairs, and 1,540/1,540
exact output oracles including warmups**. It used one warmup and 21 alternating
pairs per row, JIT pinned, MIR cache disabled, and no concurrent builds, tests or
profiling. Binary, benchmark-source, runtime-source, build and native-module
stability checks all passed. Intervals below are paired bootstrap two-sided 95%
intervals with 10,000 resamples; a ratio below one is faster.

- Control SHA-256: `58820152992fe1ab4740510f66272e792930e9ffc8132fbf27378f3086a4f162`.
- Candidate SHA-256: `e2f9b029aabaa73ce60274b10f8835b0f65420ac20d46e0da112a944995f1ad2`.
- Evidence: `final-pairs.json`, `final-manifest.json`, `final-output-checks.json`,
  `final-summary.txt`, and `final-provenance.json` in `temp/proof-consumers/`.

| Workload | Variant | Control ms | Candidate ms | Ratio | Paired 95% interval |
|---|---|---:|---:|---:|---:|
| targeted/open | typed | 0.646 | 0.640 | 0.9907 | 0.9370–1.0047 |
| targeted/exact | typed | 0.638 | 0.640 | 1.0031 | 0.9938–1.0063 |
| targeted/nominal | typed | 0.637 | 0.638 | 1.0016 | 0.9984–1.0031 |
| awfy/deltablue | untyped | 80.917 | 81.134 | 1.0027 | 0.9983–1.0075 |
| awfy/deltablue | typed | 19.328 | 18.897 | 0.9777 | 0.9725–0.9859 |
| beng/fannkuch | untyped | 0.278 | 0.280 | 1.0072 | 0.9892–1.0182 |
| beng/fannkuch | typed | 0.289 | 0.293 | 1.0138 | 0.9966–1.0242 |
| larceny/pnpoly | untyped | 2.340 | 2.349 | 1.0038 | 0.9911–1.0151 |
| larceny/pnpoly | typed | 2.572 | 2.576 | 1.0016 | 0.9942–1.0066 |
| text/microdiff | untyped | 32.673 | 32.500 | 0.9947 | 0.9889–0.9986 |
| text/microdiff | typed | 30.059 | 28.392 | 0.9445 | 0.9394–0.9484 |
| text/jq_records | untyped | 332.594 | 330.396 | 0.9934 | 0.9825–1.0077 |
| text/jq_records | typed | 382.099 | 365.627 | 0.9569 | 0.9461–0.9711 |
| jetstream/splay | untyped | 191.847 | 191.574 | 0.9986 | 0.9912–1.0027 |
| jetstream/splay | typed | 208.285 | 185.016 | 0.8883 | 0.8848–0.8907 |
| jetstream/deltablue | untyped | 999.191 | 997.914 | 0.9987 | 0.9872–1.0061 |
| jetstream/deltablue | typed | 14.560 | 14.207 | 0.9758 | 0.9611–0.9902 |
| r7rs/fib | untyped | 1.334 | 1.304 | 0.9775 | 0.9652–0.9909 |
| r7rs/fib | typed | 1.310 | 1.314 | 1.0031 | 0.9916–1.0100 |
| r7rs/tak | untyped | 0.137 | 0.142 | 1.0365 | 1.0073–1.0365 |
| r7rs/tak | typed | 0.138 | 0.141 | 1.0217 | 0.9648–1.0365 |
| r7rs/cpstak | untyped | 0.275 | 0.274 | 0.9964 | 0.9647–1.0183 |
| r7rs/cpstak | typed | 0.272 | 0.276 | 1.0147 | 0.9855–1.0404 |
| awfy/mandelbrot | untyped | 37.577 | 37.534 | 0.9989 | 0.9976–1.0004 |
| awfy/mandelbrot | typed | 37.594 | 37.549 | 0.9988 | 0.9981–0.9993 |
| beng/pidigits | untyped | 0.369 | 0.371 | 1.0054 | 0.9892–1.0136 |
| beng/pidigits | typed | 0.368 | 0.372 | 1.0109 | 1.0027–1.0245 |
| larceny/array1 | untyped | 0.803 | 0.805 | 1.0025 | 0.9988–1.0062 |
| larceny/array1 | typed | 0.806 | 0.809 | 1.0037 | 1.0000–1.0074 |
| larceny/divrec | untyped | 1.201 | 1.202 | 1.0008 | 0.9992–1.0033 |
| larceny/divrec | typed | 1.211 | 1.204 | 0.9942 | 0.9718–1.0008 |
| larceny/ray | untyped | 0.213 | 0.212 | 0.9953 | 0.9814–1.0142 |
| larceny/ray | typed | 0.218 | 0.221 | 1.0138 | 1.0046–1.0278 |
| julia/iteration_pi_sum | untyped | 2.888 | 2.894 | 1.0021 | 0.9986–1.0059 |
| julia/iteration_pi_sum | typed | 2.894 | 2.888 | 0.9979 | 0.9952–1.0035 |

Typed Splay improves **11.2%**, microdiff **5.5%**, jq_records **4.3%**, and the two
DeltaBlue suites **2.2–2.4%**. Their untyped peers remain neutral except microdiff,
which improves 0.5%. The previously optimized repeated-record-boundary probes
remain neutral. `pnpoly` and `fannkuch` do not improve materially.

There are small measured regressions: untyped tak +5 µs (3.6%), typed pidigits
+4 µs (1.1%), and typed ray +3 µs (1.4%), with intervals above one. They remain
reported rather than retried away. Captured tak/pidigits MIR is unchanged after
normalizing process addresses; ray additionally contains the new array boundary
guards. These observations do not establish a causal runtime explanation for
the small regressions.

Candidate medians put typed Splay 3.4% below untyped, microdiff 12.6% below,
jq_records 10.7% above, pnpoly 9.7% above and fannkuch 4.6% above. These are
**descriptive comparisons between separate rows**, not a fresh paired
typed-versus-untyped experiment. Remaining parity gaps are not all repeated
nominal validation.

### Executed mechanisms

Profiles are separate diagnostic runs, not timing attribution:

| Typed workload | Control `lambda_type_check` | Candidate | Other evidence |
|---|---:|---:|---|
| splay | 1 | 1 | Generic `fn_map_set`: 562,197 → 35,997; returned-record field hits: 0/3,999 → 3,999/3,999 |
| jq_records | 524,881 | 85,020 | Broad/nullable guards and retained array certificates remove repeated crossings |
| microdiff | 61,500 | 0 | Broad map/array boundaries use kind guards |

Splay root stores/reloads fall from 10,179,475/6,264,487 to
9,669,275/5,216,088. Required COW behavior remains intact; the proof is not an
ownership license. jq's residual checks are 40,020 boxed `jq_unique_strings`
entries and 15,000 each in `double_qty`, `jq_collect_paths` and `collect_numbers`.
Those producers still need admission when no live certificate proves the input.

### Correctness qualification

The release MIR gate passes **279/279**. Both new fixtures match their goldens in
JIT and interpreter modes, normally and with `LAMBDA_GC_FORCE_EVERY=1` plus
`LAMBDA_GC_POISON_FREED=1` (**8/8 combinations**). This includes failed writes
leaving originals and COW snapshots intact. Aggregate gate results are recorded
below after completion.

### Emission-size review and additional peers

Five instruction ceilings grew because admission fast paths now contain explicit
carrier/certificate checks while retaining their cold validator calls. Saved
control/candidate MIR and `emission-growth-review.json` identify each changed
function. Ordinary array entries add 22 instructions; boxed wrappers add 31.
The existing array witness still bypasses the entire admission path. Additional
record guards and conservative proof lifetime handling account for the larger
record-heavy functions. No root/frame ceilings were raised.

| Probe | Actual control module instructions | Candidate | Previous module ceiling | New ceiling |
|---|---:|---:|---:|---:|
| typed_array_guard | 1,304 | 1,455 | 1,306 | 1,455 |
| tune30_libm_no_gc | 466 | 541 | 467 | 541 |
| cube3d2 | 14,974 | 16,551 | 15,099 | 16,551 |
| deltablue2 | 10,457 | 10,638 | 10,457 | 10,638 |
| prettier_ast2 | 18,379 | 20,920 | 18,423 | 20,920 |

Only the measured default instruction budgets were updated; platform-specific
ceilings remain unchanged. Additional 21-pair release runs qualify the larger
cube3d/prettier changes against the same binaries:

| Workload | Variant | Control ms | Candidate ms | Ratio | Paired 95% interval |
|---|---|---:|---:|---:|---:|
| prettier_ast | untyped | 765.145 | 750.921 | 0.9814 | 0.9754–0.9857 |
| prettier_ast | typed | 346.659 | 308.137 | 0.8889 | 0.8858–0.8933 |
| cube3d | untyped | 8.213 | 7.627 | 0.9286 | 0.9147–0.9352 |
| cube3d | typed | 6.083 | 5.055 | 0.8310 | 0.8221–0.8356 |

All 84 retained pairs and 176 outputs pass, including exact comparison of
prettier's formatted output with `prettier_ast2.formatted.txt`. The first cube
rows in `growth-pairs.json` used an incorrect manifest marker (`cube3d` instead
of `3d-cube`) and are excluded. `cube-pairs.json` contains the valid rerun with
the correct existing oracle. Overall qualification covers **39 rows, 819 valid
pairs and 1,716 exact outputs**. It establishes workload execution improvements;
MIR growth remains a compilation/code-size tradeoff.

### Aggregate gates and residual failures

- `make test-lambda-baseline`: initial combined result **8,366/8,379**. Its 13
  failures were four sandbox-blocked loopback tests, five instruction ratchets,
  two tier-support assertions affected by optional-record support, and two
  previously known demo failures.
- Focused resolution: loopback **4/4** outside the sandbox; optimization suite
  **55/55**; tier-parity fixture group **1/1** (all listed scripts across three
  tiers, now including the new fixtures); reviewed debug ratchets **20/20**.
  Three existing optional-record scripts now exercise interpreter golden parity
  instead of expecting unsupported-node rejection. COW-count assertions remain.
- The aggregate's MIR gate passed **279/279** and forced-GC sweep **350/350**.
- `make test262-baseline`: **40,261/40,261**, zero regressions.
- The two residual Lambda failures are `tests_greetings_model` and
  `tests_procart_foliage`. They also fail with the saved control. This is the
  aggregate result plus focused resolutions, not a second all-green aggregate
  run; the effective unresolved count is **2/8,379**.
- A separate release-mode ratchet run is **19/20**: `cow_nested_store` emits 169
  instructions against its old release cap of 166 on **both** control and
  candidate (only process addresses differ). Its existing debug cap is 169,
  and the debug gate passes. That unrelated release ceiling was left unchanged.

Logs, exact outputs, per-test JSON, the saved release/debug binaries and the
runtime diff are retained in `temp/proof-consumers/`. The Test262 release build
has the same SHA-256 as the timed candidate. No canonical benchmark sources,
semantics rulings, vendor code, or expected program outputs were changed.
