# Four-second debug test tuning

**Status:** wrapped up at user request, 2026-11-10; timing target partially met. **Scope:** the nine requested Lambda and
MIR GC tests, each below four seconds with the current debug build.
Authority: **D6.3.1–D6.3.3, D8.3.1–D8.3.4, D8.5.1, S1.6, S2.3.1v2,
S4.1.5 and S10.2.1**. No formal ruling changes.

## Wrap-up

- Tree: `/Users/henryluo/Projects/lambda`, `master`, base `f6a13fc04`;
  changes remain uncommitted. Existing debug host flags are retained.
- `make test-lambda-baseline`: **7,370/7,370 pass**, including **2,283/2,283**
  main Lambda cases, **590/590** MIR emission checks and **366/366** GC cases.
  Evidence: `temp/under4-tests/full-baseline.log` and `baseline-*.json`.
- Eight requested cases finish below four seconds in that full baseline.
  Keyed noise takes **4.161 s**. The earlier scheduled focused run has all nine
  below four seconds, but does not establish the full-suite noise target.
- Scheduler contracts: **86/86 pass**. New shuffle and periodic-row regressions
  pass on interpreter/AUTO/JIT. The independent procart oracle passes.
- Original texture PNG and mineral model hashes match at their recorded
  verification points. The shuffle regression separately proves the later
  batched-rank change agrees with the scalar reference on all three tiers.
- Final focused replay: noise **3.926 s**, other requested durations below four
  seconds (`temp/under4-tests/wrapup-focus/`). The restored strict chart check
  initially used a newline-trimming golden reader; switching to the existing
  raw text reader fixes that harness mismatch. Its separate final replay passes
  all three tiers in **3.160 s** (`chart-strict-final.json`). The full baseline
  predates this assertion-preserving reader correction.
- The attempted Wilson traversal loop is reverted. The retained grid changes
  are RNG batching, first-hit search and equivalent last-visit map construction.
- No sibling source, vendor source, host build flags or `log.conf` changes.
  Every existing fixture, tier, golden, diagnostic assertion and GC profile stays.
- Unrelated untracked `vibe/jube/JS_Jq_Result55_Analysis.md` is preserved.
- Remaining work, if resumed: give keyed noise enough margin below four seconds
  in the full suite. Current verified baseline is the starting point; do not
  reuse the reverted traversal experiment or claim all nine are qualified.

## Causes and changes

### Cold imported native code

The chart imports 99 modules. The original lowering generated 1,583 functions
and 2,258,357 MIR instructions. Sampling showed native generation and register
allocation dominated this cold work. Repeated scalar ownership, conversion,
indexing, truth, equality and speculative record-shape graphs enlarged many
functions whose exports were never invoked.

Large eager imported debug units now call the existing checked leaves for
those erased operations. Proven scalar and array lanes retain direct lowering.
The scalar-adoption leaf retains the same colored home and NO_GC contract
(**D6.3.1–D6.3.3**). Main scripts keep their emission contracts, and AUTO hot
satellites retain specialization (**D8.3.1–D8.3.4**). This is a debug latency
policy; long-running eager-native throughput is not qualified here.

### Repeated package work

- Plant attachment sampling shares one centerline-frame array per parent or
  leaf-bearing branch. The public sample wrapper and returned models retain
  their behavior (**S1.6**).
- Grid steps, shuffle ranks and texture grain reuse the existing native keyed
  RNG batch, preserving counter indexes, rank order and the SplitMix mapping
  (**S4.1.5**; procart PG2/PG3). Wilson walks stop hit searching at the first tree
  cell. Their last-visit map writes indexes in traversal order instead of
  grouping every visit and taking a maximum; later writes select the same final
  index (**S2.3.1v2**).
- Periodic unwarped fields share wrapped coordinates and fade weights per row.
  They use the existing octave sampler, retain the point sampler's four-corner
  and octave reduction order, and broadcast shaping/weights over numeric rows
  (**S10.2.1**). Warped fields retain point sampling. Exact values and signed
  zeros are checked against point evaluation (**S1.6**; procart PG2/PG5).
- Mineral instances share local edge geometry and compute each placement matrix
  once. Eight complete serialized model hashes match the original. All 18
  texture catalog/colorway PNG hashes also match the original.

### Nested scheduling

The GC suite previously received one scheduler slot, serializing its four
independent pure-script profiles. Lambda case workers reserved three slots,
leaving one unused under the ordinary heavy-load ceiling. Both now reserve four
slots; the outer scheduler continues to cap total reservations. Pure tier jobs
interleave tiers, start demos first and order larger fixture batches before the
short tail. Scripts retain fresh runtime state (**D8.5.1**), and all GC profiles
retain distinct processes and precise roots (**D6.3.1–D6.3.3**).

## Timing evidence

Seconds, ordinary debug host, four-slot heavy-load scheduler. The workshop
duration is its actual script elapsed time, rather than its golden assertion.

| Requested case | Initial focused | Focused scheduled | Full baseline |
|---|---:|---:|---:|
| Surface bindings/coordinates | 7.655 | 3.684 | 3.793 |
| Chart paint fallback | 7.050 | 3.247 | 3.220 |
| Keyed noise | 7.151 | 3.860 | **4.161** |
| Plant architecture/development | 8.128 | 3.476 | 3.653 |
| Music GC roots | 3.132 | 1.682 | 1.674 |
| Procart workshops | 6.331 | 3.796 | 3.912 |
| Music performance GC | 2.358 | 1.149 | 1.154 |
| Solar plants/wind | 5.720 | 1.639 | 1.656 |
| Animal specimens/surfaces | 5.717 | 3.000 | 2.926 |

The initial focused music measurements already used three slots, unlike the
old scheduled allocation of one. Their initial numbers therefore understate
the scheduler fix's effect relative to the user's six/seven-second report.

Detailed samples, discarded experiment logs, exact-output hashes and test
reports live under `temp/under4-tests/`.
