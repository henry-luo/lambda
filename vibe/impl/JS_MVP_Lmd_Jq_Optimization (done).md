# LambdaJS jq runtime optimization and AUTO generator promotion

**Authority:** D6.1.1v2, D6.1.2–D6.1.4, D6.3.3, D6.3.6, D7.4.1,
D8.4.2, D8.4.4–D8.4.5. User authorized implementation on 2026-11-10. **Status: completed milestone.**

## Current checkpoint

- Updated 2026-11-11. Goal: implement the Result55 jq proposals and eligible
  AUTO generator promotion. Checkout `/Users/henryluo/Projects/lambda`, branch
  `master`, initially clean base `1c1e0ccbb`; uncommitted changes are this task.
- Implementation complete: resident-byte budgeting/batch trimming, image-based
  recovery cleanup, lazy simple-parameter MIR generators, prepared result shapes,
  guarded value-only stepping, direct retained arguments, and synchronous
  generator function/public class instance-method AUTO promotion.
- D6.1.1v2 and RA13v2 replace the old parked-count policy. Exact roots,
  same-address restoration, owned scalars and callable identity are preserved.
  Existing interpreted instances keep their walker. Async/unsupported shapes
  remain interpreted under the closed admission proof (D8.4.5).
- Lambda-owned changes only; no vendor edits, native-stack root scanning,
  mutable inline caches or state-machine lowering.
- Final release candidate: `temp/js_jq_optimization/lambda-candidate`, generated
  `release_native` (O3, NDEBUG, full LTO), SHA-256
  `6f912a185277915b53251307fc601b3e60334769d2c25701f9d63e23b19b07de`.
  Control: unchanged base, SHA-256
  `b7ada2b248e99a408b949ed66e015acccba9b4161796f2a987fac1310e86248f`.
  `provenance-final.json` and `candidate-final-sources.patch` identify the build.
- Final gates: Lambda baseline 7376/7376 (including 855 runtime and 270 script
  tests); 53/53 forced-GC/poison generator/iterator tests; 2/2 forced-GC AUTO
  promotion cases. Activation 14/14 normal and forced-GC checks also pass.
- Review corrections: lazy receivers now own borrowed wide scalars; direct
  stepping preserves a borrowed intrinsic next's error realm and context check.
  Both have deterministic failing-before regressions and passing final cases.
- Final release checksums all pass: MIR records 47.292 s, bf 188.232 s,
  tree 208.918 s, mix 237.080 s; AUTO records 76.534 s. Raw final selections
  are in `release-qualified.json`. Depth has three final alternating pairs.
- Final standard/hybrid Test262: 40261/40261, zero regressions. Final forced
  MIR generator/iterator subset: 3984/4041; all 57 failures exactly match the
  unchanged control. Full earlier forced-MIR sweep: 39955/40261, with all 306
  failures exactly replayed on control. No Test262 harness or baseline edits.
- Final cold-state check: 10000 generators survive collection/resume. Charged
  footprint control 84.73 MiB, candidate 94.27 MiB (peak 103.21 MiB).
- `lambda.exe` now equals the verified release candidate; release marker set,
  host exports pass (284 listed symbols) and the borrowed-next smoke passes.
- Five bounded final profiles are complete (four MIR workloads, AUTO records),
  using a release image with identical __text. Mix still spends 53.3% of its
  sampled window in VM calls; records/tree now show larger property costs.
- No work remains for this milestone. Document links, formal index and diff
  checks pass. Broader pool/dispatch experiments are listed below.
  Timing, profiling, build and test phases were separated.

## Milestones

1. Activation lifecycle: avoid repeated paging in live generator chains while
   bounding genuinely parked resident state; avoid dead-stack restoration when
   durable recovery ownership permits it; assess simple-parameter lazy start.
2. Iterator protocol: reuse the shared iterator operation helpers, fuse guarded
   intrinsic generator value stepping and allocate ordinary results directly
   from a shared prepared shape.
3. AUTO promotion: admit supported generator definitions and route future
   invocations to compiled bodies without changing existing generator walkers,
   callable identity, closure semantics or parameter initialization timing.
4. Qualification: focused lifecycle/protocol/promotion regressions, forced GC
   and poisoning, paired release jq outputs/timings and memory measurements,
   then required JS/Test262 and Lambda milestone gates. Record limitations and
   retain meaningful assertions if a gate fails.

## Qualification method

The release comparison uses a private unchanged host at `1c1e0ccbb` and a
private candidate built with the generated `release_native` profile (O3,
NDEBUG, full LTO). These are a new paired comparison, not a rerun of the
missing Result55 executable. `temp/js_jq_optimization/provenance-final.json` records
binary and fixture hashes. Both binaries execute identical bundled jqjs
sources with canonical input sizes; workload timers exclude input/filter
preparation and include compilation triggered during execution. Every run
must produce its canonical checksum. No builds or tests overlap timings.

The depth probe has three alternating pairs and reports the median second
pass. Each canonical jq configuration has one complete run, with the
control/candidate order alternated between workloads; those times establish
local effects, not a confidence interval or a replacement benchmark suite.

The canonical standard Test262 gate routes its 40261 tests through the
existing hybrid selector. The separate forced-MIR sweep passed 39955 and
failed 306; exact replay of all failures on the unchanged control produced
identical status, kind and message. The receiver ownership and borrowed-next realm corrections were then checked
by focused normal/forced-GC tests, the final standard full gate and the
4041-case forced-MIR generator/iterator subset (3984 pass, 57 identical control
failures). No Test262 harness or baseline was edited to obtain these results.

The 10000-parked-generator diagnostic measures macOS charged footprint,
not RSS, after collection and verifies resumed state. The 32 MiB warm budget
does not bound total process memory: native mappings, exact roots, owned
images and activations too large to compact also consume memory (D6.1.1v2).

## Final release results

Workload seconds on the unchanged control and final candidate:

| Workload / backend | Control | Candidate | Speedup |
|---|---:|---:|---:|
| records / MIR | 61.275 | 47.292 | 1.30x |
| bf / MIR | 282.276 | 188.232 | 1.50x |
| tree / MIR | 485.485 | 208.918 | 2.32x |
| mix / MIR | 287.227 | 237.080 | 1.21x |
| records / AUTO | 105.469 | 76.534 | 1.38x |

All outputs match canonical checksums: records `878885883`, bf `478890292`,
tree `313746104`, mix `98172625`. The bundle's AUTO gain does not isolate the
benefit of generator promotion from the shared runtime changes. Other AUTO
workloads were not timed in this finite qualification.

The final depth probe's median second-pass milliseconds (three alternating
pairs, 10000 values per pass, checksum `49995000` throughout):

| Wrappers | Control | Candidate |
|---|---:|---:|
| 8 | 15.46 | 16.17 |
| 15 | 30.88 | 31.13 |
| 16 | 54.61 | 32.94 |
| 17 | 80.65 | 34.35 |
| 24 | 256.75 | 46.64 |
| 32 | 429.50 | 63.29 |

At 32 wrappers the improvement is 6.79x; shallow depths are essentially flat.
This is a mechanism probe, not a canonical jq score.

## Ownership and protocol regressions

The lazy receiver regression passes a strict generator a borrowed native
wide-float receiver, overwrites that caller-owned home, collects, then starts
the body. The old carrier yielded the overwritten `42.75`; its owned scalar
tail now preserves `5e-324` (D6.1.3, D6.3.3).

The borrowed-next regression reenters a busy generator through another
realm's intrinsic `next`. Direct stepping must retain that cached callable's
validation-error realm and context checks, as the shared invoke kernel does
(D8.4.4). The unchanged control passes, the initial shortcut fails, and the
final candidate passes. Custom next/getters, delegated result identity,
destructuring elisions, defaults, return/throw/finally and existing interpreted
instances also have focused coverage. Required result objects preserve own
descriptors, key order, ordinary prototype and wide scalar payloads.

## Residual profiles and next experiments

The final symbol-retaining release image has the same `__text` address and
SHA-256 as the timed candidate. Each profile samples the worker thread every
5 ms for 12 seconds, starting 18 seconds after launch. These bounded runs are
terminated after sampling; canonical checksum qualification is separate above.
There are 2110–2136 worker samples per window. Shares describe the sampled
phase, not the whole workload or isolated savings from an individual change.

| Workload / backend | VM syscalls | Activation management | Property dispatch | Iterator result | GC/finalization |
|---|---:|---:|---:|---:|---:|
| records / MIR | 12.6% | 18.2% | 35.5% | 1.6% | 12.4% |
| bf / MIR | 29.6% | 37.8% | 21.9% | 0.9% | 14.1% |
| tree / MIR | 15.5% | 19.5% | 33.6% | 0.9% | 16.4% |
| mix / MIR | 53.3% | 69.9% | 6.6% | 0.8% | 5.3% |
| records / AUTO | 7.0% | 10.5% | 21.8% | 1.2% | 11.6% |

VM overlaps the other columns; the remaining categories use the same disjoint
precedence as the original investigation. `mix` still hits budget-pressure
compaction and restoration: 579 VM samples occur under parked trimming and
506 under restoration in `activation_resume`. The 32 MiB policy fixes the
small-depth cliff, but it does not eliminate paging for larger working sets.
`bf`, `tree` and `records` retain map/unmap work during creation/finalization.
Property dispatch is now the largest classified category for MIR records/tree.

In AUTO records, inclusive `js_interp_eval` is 38.2% and
`mvp_lmd_dynamic_call_native` is 53.9%; these overlap. Some helpers, unsupported
generator shapes and existing instances still execute through AST/native
bridges. The admission extension does not turn the whole program into MIR.

The next measured experiments are working-set-aware pressure handling for
long recursive chains, committed-byte-aware idle-pool sizing, and propagation
of proved receiver/method facts through jq-node fields and closures. AUTO can
then grow its checked ordinary helper/class-method slice. These broader
dispatch and pool changes are not implemented in this milestone. Each needs
its own ownership/identity proof and paired memory/performance measurements;
per-site mutable caches remain forbidden (D7.4.1, D8.4.5).

## Evidence and commands

Artifacts live under `temp/js_jq_optimization/`:

- `release-qualified.json`, `release-final-depth.json`: final timings and hashes.
  `release-benchmarks.json` retains earlier candidates and the planned scheduler
  stop; those superseded candidate times are excluded from the final table.
- `provenance-final.json`, `candidate-final-sources.patch`: release identity and
  source snapshot; `lambda-control`, `lambda-candidate`: reproducible hosts.
- `lambda-baseline-realm-final.log`, `test262-realm-hybrid.log`: final aggregate
  7376/7376 and standard Test262 40261/40261 gates.
- `test262-mir.log`, `test262-control-comparison.json`: earlier full forced-MIR
  qualification and exact control replay of all 306 failures.
- `test262-generator-final.log`, `test262-generator-final-comparison.json`:
  final 3984/4041 forced-MIR subset; all 57 failures match control exactly.
- `realm-forced-generators.log`, `realm-forced-auto.log`: final forced-GC/poison
  53/53 protocol and 2/2 promotion cases.
- `cold-{control,candidate}.json`, `cold-status.json`: final charged footprints
  and passing resume checks; candidate steady 94.27 MiB, peak 103.21 MiB versus
  control steady 84.73 MiB. The measured steady tradeoff is 9.55 MiB.
- `*-steady.sample.txt`, `sample-summary.json`, `symbols-text-comparison.json`:
  fresh raw profiles, aggregation and identical release code-section check.

Commands use generated release configuration and explicit private hosts:

```sh
JS_EXEC_BACKEND=mir temp/js_jq_optimization/lambda-candidate js temp/js_jq_optimization/jq_records.js
JS_EXEC_BACKEND=auto temp/js_jq_optimization/lambda-candidate js temp/js_jq_optimization/jq_records.js
make test-lambda-baseline
LAMBDA_TEST262_EXE="$PWD/temp/js_jq_optimization/lambda-candidate" ./test/test_js_test262_gtest.exe --baseline-only --batch-only --run-async --async-list=test/js262/test262_baseline.txt --jobs=4 --gtest_brief=1
LAMBDA_TEST262_EXE="$PWD/temp/js_jq_optimization/lambda-candidate" ./test/test_js_test262_gtest.exe --baseline-only --batch-only --batch-file=temp/js_jq_optimization/test262-generator-final-list.txt --run-async --async-list=test/js262/test262_baseline.txt --mvp-lmd --jobs=3 --gtest_brief=1
```

Test262's standard selector is hybrid; its full forced-MIR gaps remain known
limitations. No Test262 harness, baseline, vendor or sibling source was changed.
