# JS MVP performance regression fixes

Date: 2026-10-09. Follow-up to [CD native construction](JS_MVP_Lmd_CD_Native_Construction.md).

## Diagnosis

Frozen releases under `temp/mvp_regressions_20261009/` reproduce the two reported
regressions with unchanged JS, release MIR, balanced process order and an
identical-control peer. `object_delete` measures 1.120 versus 1.0765 ms. CD cold
process time measures 3.191 versus 3.146 s (ratio interval 1.0099–1.0190), while
its execution time remains faster.

The previous run isolated the deletion slowdown to the compiler changes, with
identical normalized MIR and unchanged native map set/delete bodies. Its exact
instruction-level attribution remains unproven. The exposed structural cost is
clear: after deletion changes property order, reads repeatedly miss the original
literal shape and call the generic reader. The compiler does not predict the
shared successor layout.

Separate MIR generator diagnostics identify CD's extra compilation work. The
recursive traversal grows from 9,306 to 18,002 finalized instructions and from
39,792 to 81,632 native bytes; its code-generation time rises from 45.91 to
128.04 ms in this diagnostic pair. Each allocating call site expands multiple
same-named class methods, despite its exact callee guard selecting only one.
The per-callee size limit does not bound total caller expansion.

## Changes

- Predict removal and reinsertion layouts from known literal shapes. Reuse
  `plan_shape_field`, the existing four-shape guard budget, `field_read` and
  runtime shape construction. Predictions are fixed before execution; a
  mismatching shape uses the generic reader (**D3.4.3v5**, **D3.4.5**,
  **D8.4.1v2**). Different value types, property orders and private shapes
  remain valid fallbacks.
- The new compiler helper is `plan_deleted_shapes`, disclosed before
  implementation. No new mutable per-site cache is added.
- Inline at most one allocating target per call site, guarded by captured
  callee identity. Other class implementations use the original call path.
  Cap cumulative allocating expansion at three ordinary statement-body
  budgets per caller, while retaining the per-callee and recursion limits
  (**D6.2.1**, **D8.4.1v2**).
- Preserve precise GC roots and destination-owned scalar lifetimes
  (**D5.2–D5.3**). Regression coverage alternates field types and shapes, deletes
  and reinserts properties, checks field order and object identity, retains
  tiny doubles across retyping, and exercises the non-inlined class target.
- Extract `fn_map_set`'s existing compatible conversion into the shared helper
  `map_field_store_int_as_float`, also disclosed before implementation. Both
  Lambda and MVP call it with an already resolved slot and a proven integer.
  This preserves double layouts (**D3.4.5**) without repeating property lookup.
  Return the original JS assignment value and retain normal retyping for
  other values.

The first, narrower total budget fixed startup but gave back some execution
speed; its screen is retained under `first/`. The revised policy addresses
unnecessary polymorphic expansion directly.

## Discarded cache experiment

The first four candidates reused the existing MVP property cache for ordinary
objects, initially promoting `object_face` to share its family check. The broad
run in `pre-numeric-cache/` was stopped to narrow emitted cache lanes. Checking
the normative ruling then exposed a conflict: **D8.4.1v2** bans mutable per-site
caches, while the pre-existing MVP uses them. Clarification was requested; no
exception was assumed. The ordinary-object extension and helper promotion were
removed in favor of static predictions. The pre-existing MVP cache design is
outside this regression patch and still needs reconciliation with that ruling.

The third candidate regressed Bounce by 3.3% over 60 balanced rounds
(0.8505 versus 0.8235 ms; ratio interval 1.0291–1.0364, identical-control
interval 0.9964–1.0036). Its hot functions have identical normalized MIR and
identical helper/root counts to the incoming candidate. In particular, Bounce
calls the class-property helper 12,354 times from its hot method.

The experimental ordinary-object cache checks preceded class dispatch. Release LLVM IR
shows their extra cache/operation predicates on the class path. Moving them
into the non-class branch recovered parity over 60 rounds (0.822 versus
0.824 ms, ratio interval 0.9939–1.0000). The static-prediction version removes
this runtime change altogether. All candidate histories are retained.

## Older Bounce comparison

The first static-prediction candidate (`pre-numeric-store/`) fixed the latest
deletion/startup regressions but retained a 0.6% Bounce slowdown against the
older `f85933d6…` release: 0.822 versus 0.817 ms over 60 rounds, ratio interval
1.0012–1.0098. The validation run was stopped before acceptance.

Bounce repeatedly assigns integer limits to floating position fields. The MVP
shape setter treats that as a type change; Lambda's general setter already
widens the integer in place. Reusing that existing conversion removes this
source of shape churn. Focused checks cover shared layout identity, the largest
exact JS integer, tiny doubles retained across writes, signed zero, deletion
and nonnumeric retyping.

## Numeric-store refinement

Calling the full `fn_map_set` fixed Bounce but regressed Richards by 1.5% in
the 15-round matrix (76.334 versus 75.205 ms; ratio interval 1.0054–1.0198).
That run is retained under `pre-richards-refinement/` and was stopped before
acceptance. Instrumented runs identify the duplicated work: Richards enters
the new numeric path 24,800 times for `control` and 23,300 times for `identity`.
Class-property calls barely change (59,319 to 59,313), so the extra full
property dispatch is almost pure overhead. Bounce, by contrast, uses the path
1,233 times and cuts class-property calls from 14,178 to 7,482.

The shared store extraction retains the existing conversion and lets MVP use
its resolved field address. It adds no lookup, allocation or scalar home;
callers retain the existing guards and precise roots. The original Lambda
setter uses the same extracted operation.

## Measurement protocol and interruptions

Final artifacts are under `temp/mvp_regressions_20261009/accepted/`. The release
candidate and sources are frozen by `manifest.json`. The seventh candidate's
complete record is archived under `seventh-validation/`; its SHA-256 is
`8652d0f02773c49cb8eeedf5dbf9824f6a1365064f599ce1633c5ce784024b41`.
The superseded numeric
store candidate is `4aadafdf38f216b4ae3b6de7d0f16c37d82f09131b1930ac4e65b62ba23c68c5`.
The primary control is
the pre-regression `2756f62f…` release. `cd-incoming` compares the immediately
previous optimized `2b6cb21f…` release; `bounce-historical` uses the older
`f85933d6…` release that originally exposed the small Bounce regression.

Runs use unchanged workload sources and output oracles, pinned native MIR,
release binaries, fresh processes and balanced six-permutation ordering with
an identical-control peer. Workload times are self-reported; process wall time
includes startup, compilation and execution. Diagnostic counters are collected
separately under **D5.4.4**, outside performance measurements.

User interruptions stopped one partial run, retained under
`pre-richards-refinement/interrupted/`. That superseded candidate's initial
CD/Bounce confirmations also span a host-state shift: both controls became
about 1.7 times slower. Its validation manifests and host-state note record
the boundary; those records are not the final candidate's acceptance run.

The seventh candidate's short CD screen overlapped a baseline run in the
separate `Lambda-opus` checkout and heavy indexing. It is excluded from
acceptance evidence (`seventh/MEASUREMENT_LIMIT.md`). The seventh validation
started after that baseline exited, with no concurrent agent-owned build,
test, benchmark or profiling process. Its results follow.

A later Radiant baseline began in the other checkout during the seventh
candidate's Bounce confirmations (`seventh-validation/host-state-during-bounce.txt`). Identical-control
variation is reported with the results; these runs do not establish quiet-host
absolute latency. The first Bounce control/peer interval excludes parity, so
its exact percentage improvement should not be treated as precise.

## Seventh candidate measurements

Times below are milliseconds. Intervals are paired 95% bootstrap intervals for
candidate/control; lower is faster. The main matrix uses 15 rounds; targeted
checks use 30–90 rounds. All 60 MVP workload output oracles pass.

| Workload / comparison | Control | Candidate | Ratio interval |
|---|---:|---:|---:|
| `object_delete`, 90 rounds | 1.1375 | 0.849 | 0.7434–0.7553 |
| CD, pre-regression control | 199.577 | 174.418 | 0.8572–0.9521 |
| Richards, pre-regression control | 77.025 | 75.579 | 0.9770–0.9891 |
| Bounce, pre-regression control | 0.827 | 0.599 | 0.7196–0.7337 |
| CD, incoming optimized release, 30 rounds | 178.0905 | 176.047 | 0.9528–1.0036 |
| CD total process, incoming release, 30 rounds | 3455.417 | 3369.400 | 0.9505–0.9902 |

CD is **4.80× Node** (36.310 ms) and **0.951× typed Lambda** (183.327 ms)
in the same matrix. Richards is 8.89× Node and 0.926× typed Lambda. Typed
Lambda ports use different native data structures and clock their kernel;
MVP/Node include class setup and verification. These ratios do not isolate
typing or compiler quality.

Separate instrumented release probes confirm the mechanism (**D5.4.4**):

| Diagnostic, incoming → fixed | Before | After |
|---|---:|---:|
| CD traversal `f93`, finalized MIR instructions | 18,002 | 13,176 |
| CD traversal native bytes | 81,632 | 58,256 |
| CD traversal generator time, diagnostic only | 126.43 ms | 76.73 ms |
| CD executed JIT helper calls | 4,118,148 | 4,118,148 |
| Deletion JIT helper calls | 80,002 | 40,002 |
| Deletion root reloads | 200,000 | 100,000 |
| Bounce JIT helper calls | 21,376 | 14,680 |
| Bounce root reloads | 40,759 | 25,973 |

CD's removed expansions were not executed; the bounded policy reduces
compilation without giving back its executed fast paths. Deletion and Bounce
reduce repeated runtime work. These counters and generator times come from
separate diagnostic binaries, not the timed acceptance candidate.

The older Bounce control also improves over 60 rounds (0.6335 versus
0.8905 ms, interval 0.6734–0.7626), although its absolute timing is noisy.
The 30-round Richards confirmation is 76.062 versus 78.0345 ms, interval
0.9476–0.9943. Shared Lambda Fibonacci, GC allocation and JSON generation
checks show no confirmed slowdown over 30 rounds.

The matrix flags Map lookup (+3.9%, with a +1.8% identical-control shift)
and SHA-1 (+0.94%) for longer confirmation. Their generated MIR is identical
after address normalization; no new generated operation explains those flags.
CD total process time against the older pre-regression control is inconclusive
in this noisy matrix (interval 0.9401–1.2392), despite the significant reduction
against the incoming build. The longer confirmations and baseline gates below
resolve the execution flags and retain the cold-process uncertainty.

## Seventh candidate functional validation

The frozen release passes all **67** focused MVP tests in both normal and
forced-GC modes. The first aggregate Lambda baseline passes **6,486/6,487**;
`latex_test_latex_phase3_corpus` reaches its 60-second script timeout. An
isolated unchanged replay passes in **54.766 seconds**. This is consistent
with the load sensitivity documented in Developer Guide §7.4, but does not
establish its cause. No test, timeout or expected output was changed.
`lambda-gate.log`, `lambda-gate-timeout-results.json` and
`latex-corpus-replay.log` retain that failure and replay. The complete baseline
rerun passes **6,487/6,487**, with the corpus taking about 58 seconds; its narrow
timeout margin remains a validation limitation. The rerun log and JSON retain
the successful attempt. The continuation uses `validation-after-timeout.json`.

Test262 passes **40,261/40,261**, with zero failures, non-fully-passing cases
or retries (169 batches; 2,652 excluded cases remain skipped). The restored
main executable matches the frozen measured SHA-256, and all implementation
source hashes match the manifest.

## Remaining Map regression

The longer check confirms Map lookup at **7.958 versus 7.782 ms**, a **2.26%**
regression over 90 rounds (interval 1.0125–1.0284). The identical-control median
is also 7.782 ms. `args_ctl` returns to parity. SHA-1's 60-round interval spans
parity (0.9999–1.0079), so its initial flag is not confirmed.

The Map helper remains 2,772 native bytes and moves 6,624 bytes in the executable.
Its 39 changed instruction words are call/page relocations; generated MIR is
unchanged. This points to native layout sensitivity, but the specific cache or
predictor mechanism has not been isolated. No padding or linker ordering is
added to the production build.

The existing helper reserves a 240-byte frame even for read/immediate-update
hits because canonicalization and allocating updates contain precise-root
setup. The eighth candidate factors those owned operations into `map_call_owned`,
disclosed before implementation. It reuses `canonical_string`, `store`,
`compact_entries`, `entry_find`, shared array reservation and scalar ownership
instead of duplicating their implementations (**D5.2–D5.3**). This also combines
the two root-setup blocks. The native diagnostic audit confirms **2,772 → 1,244
bytes** for the hot helper and **240 → 112 bytes** for its stack frame.

The eighth release is
`5f0ce0ed8d41059e76a65ac9f972f95c0daed9d42ccedb1b419c0c779d10ce16`.
Its 67 normal/forced-GC checks pass. A 30-round Map screen returns to parity
(7.758 versus 7.7845 ms, interval 0.9900–1.0088). The 90-round repeat is noisier
(10.0085 versus 9.9585 ms, interval 0.9919–1.0233); it does not exclude a small
remaining slowdown. Map iteration improves in both checks.

An external parallel build then drives CD process times above 25 seconds,
compared with roughly 3 seconds in earlier runs. The matrix is stopped and
retained under `accepted/contaminated-matrix/`, excluded from acceptance.
The external session subsequently starts a baseline run; the immediate restart
is stopped as well (`baseline-overlap-restart/`). Independent diagnostics and
source checks are preserved; the completed reruns are reported below.

The eighth candidate's aggregate Lambda run passes **6,486/6,487**, again
timing out only `latex_test_latex_phase3_corpus`. Its companion expl3 corpus
takes 27 seconds versus about 9 seconds in the earlier successful run. All
67 MVP tests pass within this aggregate run. This failure is retained in
`accepted/lambda-gate.log` and `lambda-gate-timeout-results.json`; the unchanged
timeout is not relaxed. Test262 passes **40,261/40,261**, with zero retries,
failed cases or unstable batches. The restored executable and source hashes
match the eighth candidate's manifest. The final results and limits follow.

## Eighth candidate results

The repeated final matrix completes all **60 workloads** with matching output
oracles and no statistically confirmed slowdown. This remains qualified paired
evidence: another Test262 run was active at the start, and CD's matrix interval
is wide. `accepted/PERFORMANCE_ENVIRONMENT.md` records that limitation.

The final 90-round confirmation measures Map lookup at **7.7875 versus
7.7625 ms**, interval **0.9959–1.0116**, with a 7.802 ms identical-control median.
This no longer supports the seventh candidate's 2.26% regression. Deletion is
**0.8405 versus 1.134 ms**, interval **0.7366–0.7489**. Object fields are at
parity; Map iteration's median is 1.16% lower with an interval spanning parity.
These records are under `accepted/map-confirm-final/`.

| Final confirmation | Control ms | Candidate ms | Candidate/control 95% interval |
|---|---:|---:|---:|
| Deletion, 90 rounds | 1.134 | 0.8405 | 0.7366–0.7489 |
| Map lookup, 90 rounds | 7.7625 | 7.7875 | 0.9959–1.0116 |
| CD execution, 30 rounds | 204.163 | 178.1025 | 0.8264–0.8925 |
| CD total process, same rounds | 3345.540 | 3313.980 | 0.9698–1.0411 |
| Bounce, older control, 30 rounds | 0.8295 | 0.6155 | 0.7368–0.7458 |
| Richards, 15-round matrix | 77.559 | 75.843 | 0.9478–0.9962 |

The CD execution gain is confirmed. Its cold-process median is 0.94% lower,
but the interval cannot exclude a small startup regression; the original
1.1% cold-process difference is below this run's resolution. The seventh
candidate's comparison against the incoming optimized build remains separate
historical evidence, not a substitute for this final comparison.

The final matrix observes CD at 4.69× Node and 0.914× typed Lambda, and Richards
at 8.66× Node and 0.936× typed Lambda. CD's matrix interval is particularly
wide under host variation; these are observed medians, not precise quiet-host
cross-engine ratios. The unchanged-workload and timing-boundary qualifications
above still apply.

All **63** measured rows (60 MVP workloads plus three shared Lambda checks)
have no statistically confirmed slowdown. This does not prove zero regression
below the measurement uncertainty. The final full Lambda baseline rerun passes
**6,487/6,487**, with the LaTeX corpus again taking about 58 seconds. Earlier
timeouts remain recorded; its narrow timeout margin is not claimed fixed.
The eighth diagnostic generator timings were collected during host
contention and are not comparative timing evidence; instruction/byte counts,
deterministic counters, and the measured executable's Map frame audit are retained.

Closeout: `validation-performance-final.json`, `validation-test262.json` and
`validation-lambda-final.json` record the completed final runs.
`lambda-gate-final-results.json` preserves the aggregate rerun. The release
restored after that gate has SHA-256
`5f0ce0ed8d41059e76a65ac9f972f95c0daed9d42ccedb1b419c0c779d10ce16`,
identical to the measured candidate; all frozen implementation source hashes
also match (`closeout-hashes.json`).

Published as [MVP_Result8](../../test/benchmark/js_mvp_lmd/MVP_Result8.md),
with all 60 matrix rows, longer confirmations and validation provenance.
