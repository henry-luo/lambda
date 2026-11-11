# MVP region allocation and profiling assessment

Date: 2026-11-10

## Current checkpoint

- Scope: the user now explicitly requests removal of region allocation and
  related code, superseding the assessment's retention recommendation below.
- Main checkout, `master`, starting HEAD `fd07a893f`; prior helper-retirement
  changes remain uncommitted. User explicitly approved the scoped input-owned
  `lambda/lambda.h` edit; the prepared patch is applied without other owner
  source/header/build edits (D9.1.1v4, D9.1.3v2).
- Removed region types, allocation/block/cache helpers, heap cache fields and
  teardown, begin/end and region-map wrappers, and their JIT registrations.
- `map_alloc_for_type(TypeMap*, int64_t)` is GC-only. Migrated all 19 native
  calls in 12 files, MVP's emitted call, and JIT argument classifications.
  Runtime container layouts and precise-root policy remain unchanged
  (D2.1.1, D5.3.1, D6.3.3–D6.3.4).
- Full split release and native-test rebuilds succeed. All selected checks pass:
  nine native/profiling GTests, seven focused Python methods, six JS map-growth/
  GC cases, four typed/untyped tree benchmark goldens, and four diagnostic
  root/memory parity cases. LI-006 is resolved.
- Source and compiled-symbol audits find no retired region references. The
  applied owner-header diff matches the approved patch exactly. Evidence:
  `temp/mvp_region_retirement/final-summary.json` and its linked receipts.
- The verified release is `temp/mvp_region_retirement/lambda-release.exe`;
  root `lambda.exe` is unchanged. Source changes in Lambda and the approved
  input header remain uncommitted. No baseline or new performance campaign ran.
- Region retirement is complete; no jobs remain running. Earlier profiling
  timing limits remain historical and are not requalified by these checks.

## Region retirement implementation

The user explicitly approved the prepared input-owned `lambda/lambda.h` patch.
Its applied diff is checked against the reviewed patch; no other sibling source
or header is edited. The owner issue is LI-006 (D9.1.1v4, D9.1.3v2).

Removed seven allocation-region functions: `lambda_region_begin`,
`lambda_region_end`, `lambda_region_calloc`, private `lambda_region_take_block`
and `lambda_region_destroy_caches`, and `map_with_region_tl` /
`map_with_region_type_tl`. The two private structs, 64 KiB block constant, both
heap cache fields, teardown call, declarations and four JIT import rows are gone.
A tracked-source audit across Lambda, input and Radiant finds no remaining
references to the retired allocation-region names. Compiled object symbols
confirm their absence too.

The retained allocator is `map_alloc_for_type(TypeMap*, int64_t minimum_capacity)`.
It always performs the existing combined GC allocation of map header and inline
data; capacity checks and public container layouts are unchanged (D2.1.1,
D5.3.1). All 19 existing native calls in 12 Lambda-owned files now use two
arguments, including JS shape-storage capacity. MVP emits the two-argument
call, and its import metadata classifies the shape as a raw non-GC pointer and
capacity as a non-GC scalar (D6.3.2–D6.3.4). There is no ignored compatibility
argument or alternate allocator. The stale Full-region benchmark comment is
removed without changing its workload or golden.

The new `GcMapAllocatorHonorsMinimumCapacity` native test checks GC ownership,
inline storage, capacities below/above the shape size, payload survival across
collection and invalid requests. Its shape is pool-owned through heap teardown;
the map uses a precise root (D5.2.1, D6.3.3).

Verification receipts under `temp/mvp_region_retirement/`:
`native-units-final.log` (7/7), `profile-units.log` (2/2), `python-units.log`
(7 methods), `shared-allocator-goldens.json` (10/10),
`profile-root-parity.log` (four cases), `retired-source-references.json` and
`symbol-audit.json`. The release CLI and native GTest executable are rebuilt
from the two-argument allocator ABI. The shared root executable is not replaced;
the earlier profiling performance discrepancy is outside this retirement's
correctness qualification.

## Prior assessment checkpoint (superseded by the retirement request)

- MVP execution-call profiling already works. COW site attribution now uses
  existing audited diagnostic wrappers. Disabled profiling keeps
  the ordinary helper names and native capture path (D6.3.2–D6.3.4).
- The COW census regression now passes. Four focused Python unit methods,
  36 normal/forced/seeded-GC diagnostic cases, and seven profiler root/memory
  comparisons pass. Ordinary MIR, instruction metrics and frame telemetry
  match the control for both variants of Binary Trees and GCBench.
- Selected profile GTests pass 2/5, versus 1/5 on the control. Three unchanged
  Full-MIR-specific profile oracles still fail on both (LR07-59).
- Timing: Binary Trees is flat; GCBench flags +2.4% typed and +1.0% untyped
  in a 30-pair confirmation. Cause is unconfirmed despite identical ordinary
  MIR. No zero-regression or whole-MVP speedup qualification is claimed.
- Evidence: `temp/mvp_regions_profile/`. Control SHA256:
  `0e17c8c569764b0619088296bdb93fc5bfadc2f6cfde2f433b4fa55ea536ee75`.
- Private release candidate SHA256:
  `5408e9088a02c32510a8306ad9af18ec751d6784c6278dc650a0c4b958875bbe`.
  Root `lambda.exe` remains the control. Source changes are uncommitted.
- Assessment complete; next implementation milestone is the scoped region
  proof below. Before activating the profiling candidate, investigate the
  measured GCBench delta. No builds/tests remain running.

## Historical region assessment (retention recommendation withdrawn)

Full MIR passed a private region capability through recursive map producers
only when an immediate reader returned a scalar and could not retain the graph.
Allocation used reusable 64 KiB blocks with ordinary adjacent object headers;
the graph was released after the consuming call. These objects were absent from
the GC object registry. Their proof therefore excluded outgoing ordinary-GC
edges. This was not a general replacement for the GC allocator.

Current MVP plans exact field layouts, direct stores, scalar representations,
inlining and guarded accesses (D4.3.3, D7.3.1, D7.4.2). Its map-literal emitter
still passes a null region to `map_alloc_for_type`. Its numeric/indexed loop
regions are compiler proof scopes, not allocations in `LambdaRegion`.

Selected execution profiles from the current release show:

| Benchmark | Variant | Map allocations | Generic `set_field_value` calls |
|---|---|---:|---:|
| BENG Binary Trees | untyped | 134,492 | 268,984 |
| BENG Binary Trees | typed | 134,492 | 0 |
| Larceny GCBench | untyped | 3,200,348 | 6,400,696 |
| Larceny GCBench | typed | 3,200,348 | 0 |

No region begin/end calls are emitted. Thus direct field stores have removed
one cost in typed code, while graph allocation remains. The untyped setter
counts are a separate tuning lead; call counts alone do not establish which
cost dominates elapsed time. Profile-enabled durations are not benchmark results.

The native allocator probe holds the map shape, construction order, precise
`RootFrame`/`Rooted` ownership and traversal constant. It compares normal GC
allocation with the existing region allocator for 20 depth-14 trees per sample
(655,340 nodes). It clears the result root before ending each region. All graph
edges point to the same region or null; null-shaped lanes follow D5.3.3.

| Isolated campaign | Pairs | GC median, ms | Region median, ms | Median paired region/GC ratio |
|---|---:|---:|---:|---:|
| 1 | 15 | 8.457 | 7.509 | 0.889 |
| 2 | 15 | 8.323 | 7.564 | 0.910 |
| 3 | 15 | 8.385 | 7.574 | 0.900 |

The release probe uses `-O3`, full LTO and `-march=native`; order alternates,
with two warmups per allocator. A setup campaign overlapped a build and is
excluded. Raw samples, source, commands and hashes are in `allocator-isolated-*.tsv`,
`allocator_probe.cpp`, `probe-commands.json` and `allocator-summary.json`.
The assessment initially recommended retaining the allocator for a scoped MVP
extension; the user subsequently directed its removal. The measurements do
not predict a 9–11% improvement in complete Lambda execution or prove
that the same ratio holds on other map sizes, hosts or workloads.

## Cancelled region integration proposal

The user requested retirement instead of implementing this proposal. The
requirements below are historical analysis, not outstanding implementation work.

1. Retain immutable frontend facts and build a private producer/consumer summary.
   Prove that the fresh graph cannot escape the consuming call, including via
   errors, captures, aliases, module state, callbacks or asynchronous work
   (S9.1.2–S9.1.3, D6.3, D7.4.3).
2. Admit only graph layouts whose fields contain immediate/native scalars or
   maps in that region. Pointer scalars, number-home lifetimes and error-bearing
   constructor outcomes need explicit handling; ordinary GC cannot discover
   heap objects reachable only through an unregistered region graph
   (D5.3.1–D5.3.3, D6.2.2, D6.3.3).
3. Pass the capability through private direct entries without changing the
   public boxed ABI. Recursive edges preserve it; ordinary calls retain GC
   allocation. Treat the capability as a raw non-GC pointer (D6.3.4, D7.4.2).
4. Reuse MVP's cleanup chain so normal results, propagated errors, constructor
   failures and other completion paths end the region exactly once. Preserve
   precise roots for values outside the region (D6.3.3, D7.4.3).
5. Qualify positive and rejected escape cases under forced/poisoned GC. Measure
   matched typed/untyped Binary Trees and GCBench with profiling off; retain
   raw samples, goldens and source identities. Do not enable a blanket region
   switch on the strength of the allocator probe.

No formal semantics/design ruling changes are proposed. If a scoped lowering
does not justify its cost after this qualification, retire the allocator and
its shared `map_alloc_for_type` ABI parameter with the input owner, rather than
silently ignoring a non-null capability (D9.1.1v4, D9.1.3v2).

## Profiling follows the current MVP paths

`LAMBDA_EXEC_PROFILE=1` already records executed helpers, direct/boxed calls,
frame entries, roots and fast/fallback paths. `LAMBDA_EXEC_PROFILE_CALLERS=1`
adds caller names. Aggregate `COW_EXEC_PROFILE=1` counters already work too.
Neither facility is feedback-directed optimization; use the results to select
and validate compiler changes, then disable profiling for performance timing.

The missing path is `COW_EXEC_PROFILE_SITES=1`: ordinary COW helpers report
aggregate events without the generated caller PC. MVP now selects the existing
`*_profiled` capture, prepare-write, resolved-map-write and member-write wrappers
when site attribution is requested. Selection happens while compiling; no
runtime switch is added to ordinary MIR. The wrappers preserve their existing
effect metadata and GC ownership rules (D5.4.1–D5.4.3, D6.3.2–D6.3.4).

This is attribution of retained helper events, not a census of every native
store or an execution-time improvement. Native fast paths can bypass helpers.
No runtime data-layout change, new profiler, profile-guided dispatch, baseline
suite or broad benchmark campaign is part of this assessment.

## Verification and performance limits

The release runtime and private CLI rebuild successfully. The existing
`CowSiteCensusMatchesAggregateAndIsOptIn` test now passes, including aggregate
versus site-count equality, a positive JIT caller count, and no TSV when the
diagnostic options are disabled. `ProfileDisabledWritesNoTsv` also passes.
Three other selected execution-profile tests retain Full MIR names/call shapes;
both binaries fail the same assertions. Their oracles are unchanged and tracked
as LR07-59, not counted as passes.

Four focused Python methods pass: member contracts, content/spread semantics,
projection call snapshots and external-call arguments. A separate 36-case gate
covers four COW snapshot/borrow fixtures across diagnostics off/aggregate/sites
and normal/forced/seeded poisoned GC. The execution profiler's three fixtures
and the COW site's four fixtures retain equal roots, root stores, safepoints and
memory operations with diagnostic instrumentation enabled.

Both binaries match all four selected benchmark goldens. With all profiling
disabled, their normalized MIR is identical, as are instruction metrics and
frame telemetry. Normalization removes address-based varargs prototype suffixes
and positive pointer-range operands; no arithmetic constant in these workloads
falls in the normalized range.

The 15-pair Binary Trees campaign shows no statistical slowdown. GCBench flags
small slowdowns in both its first 15-pair campaign and a 30-pair confirmation:

| Case | Control median, ms | Candidate median, ms | Candidate/control |
|---|---:|---:|---:|
| Binary Trees typed | 1.985 | 1.986 | 1.0005 |
| Binary Trees untyped | 2.982 | 2.938 | 0.9852 |
| GCBench typed, confirmation | 58.734 | 60.159 | 1.0243 |
| GCBench untyped, confirmation | 80.700 | 81.501 | 1.0099 |

These are the existing harness's self-reported workload timings, excluding
compilation, with its per-case output boundaries preserved. They compare the
profiling change with the previous MVP, not with Full MIR or C2MIR. They do not
measure a new region path. Identical MIR does not explain away the timing flags;
native layout/build effects remain an unconfirmed hypothesis. The candidate is
kept private and the root executable is unchanged pending investigation.

Receipts under `temp/mvp_regions_profile/`: `profile-units-{control,candidate}.log`,
`focused-python.log`, `cow-stress.json`, `ordinary-mir-parity.json`,
`exec-root-parity.log`, `cow-root-parity.log`, `binarytrees-paired.json`,
`gcbench-paired.json`, and `gcbench-confirmation.json`. Frame/memory snapshots
are in `temp/mir_check/profile_parity_{27209,27208}/result.json`.
`final-summary.json` records source and binary hashes. No Lambda or Radiant
baseline, Full MIR comparison, or all-benchmark performance claim was made.
