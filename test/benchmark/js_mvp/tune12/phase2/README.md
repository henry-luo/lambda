# LambdaJS Tune12 Phase 2 evidence

This directory is the durable evidence for Phase 2. It follows **S1.11**:
release-only timing, fixed source/workload identities, raw samples retained,
and no performance claim based on a debug host or a survivor-only population.

## Release identities

| Role | Archive | SHA-256 |
|---|---|---|
| Phase 2 control | test/benchmark/exe/lambda-tune12-p2-control | 4f158287b8f342ef703b946a47507d18dcfedf89748eaaf50cc9c9bc30aef972 |
| Paired-run candidate | test/benchmark/exe/lambda-tune12-p2-final-release | f6d61644742644d792ff7fd83cb5bdaac7d57ada3ca903740638b297ec739e5c |
| Standard-workflow/final release | test/benchmark/exe/lambda-tune12-p2-final-standard | 467920b3a9180623c2512dfab99d8327098c2864287dcfed934af9fea25fb957 |

The control is the preserved Phase 2 starting-tree release at commit
763269c4fbc6df4ccecd5e7a0991dcebc20f0a4a. The final standard release was
built cleanly by the guarded workflow after the same runtime source changes.
The paired candidate predates only documentation/test/build-rule changes; its
runtime source is the same. All three archives are release hosts, without
LAMBDA_JS_EXEC_PROFILE.

The initial attempted final paired candidate was discarded: the shared
lambda.exe target let a newer debug host satisfy the release target. The
release recipe now removes that shared target before the release relink, so a
debug gate cannot silently contaminate a performance run.

## Final causal result

[p2-final-full-paired-release.json](p2-final-full-paired-release.json)
contains all 63 canonical JavaScript rows, 11 alternating pairs per row,
AC-power metadata, source-tree hashes, raw samples, output digests and 10,000
resample paired-bootstrap intervals.

| Measure | Result |
|---|---:|
| Valid/equal-output rows | 63 / 63 |
| Valid pairs | 693 / 693 |
| Candidate/control geometric mean | **0.562698x** |
| Sum of row medians, control → candidate | 211,935.173 → 140,040.846 ms |
| Sum-of-medians ratio | **0.660772x** |
| Material slowest residual | jetstream/crypto_sha1, 1.0704x |

Selected causal rows: FFT 0.2337x (upper 0.2379), primes 0.0565x
(0.0596), Havlak 0.7933x (0.7951), text_search 0.5118x (0.5127),
three_way_merge 0.8634x (0.8681), and log_pipeline 0.9331x (0.9439).
hyphen is an explicit unresolved 1.0562x residual rather than a claimed
improvement.

## Publication snapshot

[final_phase2.json](final_phase2.json) and
[Overall_Phase2.md](Overall_Phase2.md) are the guarded three-sample
LambdaJS/QuickJS matrix. The workflow rebuilt release, rejected profiling
symbols, ran the Test262 baseline (40,261/40,261), and retained all 63 timing
cells. On that matrix LambdaJS/QuickJS is **2.936473x** geometric mean
(140,572.512 / 42,451.320 ms total medians); LambdaJS wins 11 rows.

This improves the Phase 1 3.481106x full-matrix ratio but does **not** meet the
Tune12 0.80x milestone. The main remaining gaps are text/Unicode and
string-heavy work (hyphen, microdiff, revcomp), plus dynamic
object/control-flow workloads such as nqueens, towers, richards and deltablue.

## Package checkpoints and residual census

- [p2-3-numeric-r1.json](p2-3-numeric-r1.json) records the first numeric
  local/boxed-return checkpoint.
- [p2-4-fft-r2.json](p2-4-fft-r2.json) and
  [p2-4-load-consumer-r3.json](p2-4-load-consumer-r3.json) record guarded
  typed-store and typed-load-to-consumer checkpoints.
- [p2-5-loop-kind-r4.json](p2-5-loop-kind-r4.json) records the narrow
  immutable typed-element-kind loop hoist. Length and data-pointer observations
  intentionally remain per access because detachment/resizing may change them.
- [p2-7-text-memory-final.json](p2-7-text-memory-final.json) records final
  release peak RSS: microdiff 151 MiB, hyphen 1.45 GiB and prettier_ast 1.36
  GiB. It matches the earlier [p2-7-text-memory-r4.json](p2-7-text-memory-r4.json)
  census; no ownership diagnosis supports speculative GC-threshold tuning.

The diagnostic LAMBDA_GC_STATS=1 hyphen run observed one 0.767 ms mark
collection and only 118,507 bytes of retained script source. It does not
identify the 1.45 GiB peak as compiler-cache retention or GC CPU. The field/
call resample likewise found mutable constructor-owned fields and dynamic
method capability in Havlak, not a safe immutable shape/call plan. Those
paths intentionally retain their generic fallback under **D8.4.1v2** and
**D8.4.2v2**.

## Correctness record

The final source passed:

- make test-lambda-baseline: 5,593 / 5,593;
- forced-GC poisoning/MIR corpus: 189 / 189;
- JS optimizer contracts: 31 / 31;
- MIR ratchet: 19 / 19;
- full Test262: 40,263 / 40,263 fully passing, zero baseline regressions;
- Test262 baseline in the guarded publication workflow: 40,261 / 40,261.

The release-build fix and all guarded fast paths retain the one semantic
fallback and precise rooting required by **D2.4**, **D5.3**, and
**D8.4.1v2–D8.4.3v2**.
