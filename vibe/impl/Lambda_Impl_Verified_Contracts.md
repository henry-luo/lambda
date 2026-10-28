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
