# Sync memory tuning — implementation plan

**Date:** 2026-11-17  
**Status:** P0–P5 implemented; release qualification recorded below, remaining budgets open.  
**Authority:** D5.1.3–D5.1.5, D5.2.1–D5.2.5, D5.3.1–D5.3.2,
D6.3.2, D8.3.3–D8.3.4, D9.1.1v4, D11.1.2; S2.3.1v2, S12.1.1v3.  
**Evidence:** [release memory diagnosis](Lambda_Impl_Sync_Memory_Diagnosis.md),
[streaming packs](Lambda_Impl_Sync_Streaming_Packs.md).

## Completion checkpoint

- User authorized P0–P5 implementation, then requested wrap-up. Checkout `master`
  at `df3ab3e83` plus the uncommitted changes; last update 2026-11-17.
- P0–P5 and physical-pack download grouping are implemented. Scoped Input
  source/API/test fixes are in the sibling checkout; Radiant ledger only.
- Final fresh-VM change passes the full Lambda baseline, **7,590/7,590**,
  including 368 MIR/GC stress cases and 2,458 runtime cases.
- Three-run release qualification before the last fresh-VM change is preserved
  under `temp/sync-memory-impl/pre-lazy/`. It covers byte-exact 8/80 MiB restore,
  larger single-file/256 MiB cases, repeated sync and binary/formatter reproducers.
- Final normal release build succeeds; 112 release GC/lifetime and 24 Input
  tests pass. The final 80 MiB check is recorded below. No further broad
  qualification was launched after the wrap-up request.
- Initial OS memory budgets are not claimed met. Native retained ownership is
  flat across 5/25 rounds, but normal live-loop footprint remained variable in
  the earlier run. Final fresh-VM warm-loop requalification remains pending.
- Independent failures remain Radiant RI-107 and Input LI-027; neither test was
  masked. Empty-binary solidity is separately logged against S2.2.2v2.
- Local S3 test services are stopped and reaped. Changes remain uncommitted.
- Artifacts: `temp/sync-memory-impl/`; historical diagnosis links below.


The preceding investigation separated allocation lifetime, GC pressure, collector
capacity and allocator residency. It included full-host owner snapshots, controlled GC,
formatter/binary reproducers, MIR requested-byte accounting, repeated unchanged
sync, and an independent 80 MiB restore verified by every file's SHA256.
Those profilers were isolated release relinks with temporary native probes,
not clean source-build performance qualifications. The implementation below
uses a clean release baseline and normal release timings; the new compact
retained-host checkpoint exists only in the native test harness. See the
historical diagnosis for its build and instrumentation limitations.

This plan changes memory ownership and reclamation. Sync policy remains primarily
Lambda Script (S12.1.1v3); native work supplies allocator/IO mechanics and fixes
runtime lifetimes (D11.1.2). File history, CRDT semantics, encrypted immutable packs,
metadata-only SQLite, authentication, recovery and canonical wire bytes keep their
existing contracts. No vendor modification is proposed.

## Evidence and priorities

| Owner or path | Measured problem | Proposed change |
|---|---|---|
| Formatter destination | 64 discarded 1 MiB JSON results retain 80 MiB of pool allocations after GC | Per-invocation result owner, released after the GC copy |
| Binary backing storage | 128 discarded 2 MiB bodies trigger zero collections | Account backing bytes at legal GC allocation boundaries |
| MIR image contexts | 8 MiB publication retains about 313 MiB requested MIR bytes; teardown separates 173.4 MiB generator scratch and 68.3 MiB generated IR | Seal eager images and release compiler scratch before publication |
| GC regions | After repeated 80 MiB sync, 192.8 MiB committed versus about 2.3 MiB occupied after collection | Reclaim empty regions without moving object addresses |
| Parsed Inputs | Three unchanged 80 MiB verification/cloud rounds add roughly 16 MiB of parser arenas; Mark validation dominates parse count | Bounded parse-result ownership and redundant-validation reduction |
| HTTP packs | One bounded pack body plus transient read/copy buffers | File-backed upload after the main runtime fixes |

These are different runs and overlapping allocation domains. Their byte counts
must not be added into a projected peak reduction. In particular, freed malloc
blocks can remain resident. Early bounded lifetimes matter more than a late
process-wide purge.

Implement P0–P5 and qualify them before considering the memory task complete.
P6 is a subsequent improvement if the remaining pack copies materially affect
the budget. Each phase is a separate reviewable change with focused tests.

## P0 — Establish the reproducible measurement gate

1. Repair the existing standard build failures through their owners: undeclared
   `dom_document_resolve_url` calls in the full host, and undeclared `list_push`
   calls in the runtime-only profile. Preserve the failures as failures until their
   actual API/extraction defects are fixed. No sibling source repair occurred in
   this investigation.
2. Build the normal release host and record executable/source hashes, compiler
   flags, platform, allocator mode, fixture manifest and local-S3 version.
   Use `make build-release-compile`; no debug performance runs.
3. Preserve a compact diagnostic collector for GC capacity/live bytes, binary
   backing capacity, formatter destination growth, parse owners, MIR lifetime,
   RSS and private footprint. Replace the temporary sleep-duration markers with
   a test-only host checkpoint; they must not become a script API or sync policy.
4. Correct `mem_factory_rt.cpp` heap telemetry: `gc->total_allocated` is not
   reserved GC capacity. Attribute slabs, bump blocks and both data zones once,
   with parent/backing edges that prevent double counting (D5.2.1, D5.2.4).
5. Keep allocator tracing and full context dumps in separate attribution runs.
   Measure process memory before reporting allocations. Full JSON snapshots
   themselves created about 20 MiB per checkpoint in the first probe.

**Gate:** compact reports agree with native region sums; unchanged idle
checkpoints do not create false growth. Capture a normal-release three-run median
before any fix. The preserved-object profiler is evidence for diagnosis only.

## P1 — Release formatter output ownership promptly

**Files:** `lambda/runtime/lambda-eval.cpp`; reuse
`lambda/format/format-utils.hpp` from the Input owner.

`fn_format2()` passes `context->heap->pool` to the formatter and copies its result
into a GC string. The original result and its builder storage survive in that
runtime pool. In the 80 MiB publication, measured formatter growth explains
53.9 of the pool's 56.1 MiB of allocations.

Use the existing `ScopedFormatPool` for the formatter destination. Keep the
source/options rooted, construct the result, copy it into its GC-owned return
home while the temporary owner is live, then release that owner on success and
failure. Audit cancellation/recovery boundaries and avoid retaining names or
metadata from the temporary destination (D5.1.5, D5.2.5). Apply the shared
`format_data_with_json_options` boundary once; do not duplicate per-format fixes.

**Tests:** repeated JSON/Mark formatting, canonical codec fixtures, Unicode and
escaping, binary/full-width numeric scalar round trips, option/error paths, and
forced GC during the return copy. Add a memory regression that discards many
formatted results and checks destination-pool live bytes, with its script golden.

**Gate:** the 64-result reproducer no longer leaves an 80 MiB destination owner.
All existing formatter and sync wire goldens stay byte-identical. Runtime pool
usage plateaus once transient formatting stops.

## P2 — Charge binary backing bytes to collection pressure

**Files:** `lambda/runtime/lambda-mem.cpp`, GC external-pressure helpers,
`lambda/runtime/image_raster.cpp`, native byte-producing call sites; build JSON
if a common accounting module is extracted.

Reuse and generalize the existing raster backing-storage accounting pattern.
Represent backing lifetime once with a release lease; slices and ordinary aliases
reuse it. A lease must remain safe if shared storage outlives its first heap.
Clear/disarm the heap association before heap teardown and reclaim the lease
when its backing dies. Define per-heap admission pressure separately from global
physical-storage accounting for cross-owner storage; aliases must not multiply
physical-byte counters.

Preflight at a legal MAY_GC boundary **before** allocating bytes. Record only a
successful allocation/admission. Preserve and chain existing release callbacks,
including wrapped external storage. Update allocation effects and precisely root
all live source/destination owners before preflight (D6.3.2). Raw byte pointers
alone are not roots. Audit `heap_binary_from_bytes`, concat, copy, slice, crypto,
disk reads, HTTP receives and buffer/view construction. Native payload pointers
must remain valid through the preflight and copy.

**Tests:** the zero-collection reproducer; concat/slice/shared aliases; one and
multiple backing owners; native producer failures; cancellation; surviving source
values during forced GC; cross-task and supported cross-owner transfer; heap
teardown before final external release. Verify release counters balance and
payload hashes survive. Reuse GC/byte-storage suites rather than duplicating
raster lifetime code.

**Gate:** 128 and 1,024 discarded 2 MiB bodies produce pressure collections and
bounded pending payload, rather than linear growth. After a full collection,
backing live bytes match actual survivors. Proposed microbenchmark peak budget:
64 MiB RSS on the qualification machine, subject to P0's measured fixed baseline.

## P3 — Seal eager MIR images and retire compilation scratch

**Files:** `lambda/runtime/mir.c`, `lambda/runtime/mvp/mvp_driver.cpp`,
image lifecycle fields and `runner.cpp` teardown/anchor transfers.

Add one Lambda-side sealing operation. After an eager image has linked every
entry, populated its BSS/layout, and copied required debug/property metadata,
prove that every retained function has machine code. Reuse
`jit_release_generated_ir()` and finish its generator scratch through the public
MIR API. Retain the MIR context's code/data/module ownership required by callable
entries. Preserve D8.3.3–D8.3.4; a shared mutable compilation context is not needed.

Make the compiler lifecycle explicit: building, eagerly generated, sealed,
published, destroyed. Keep `image->mir_gen_initialized` and the Script anchor's
mirrored flag consistent. Failure/cancellation paths finish each initialized
resource once; published code remains callable until image destruction. Audit
background publication, synchronous anchors, stale jobs and queued destruction.
MIR-interpreter and genuinely lazy-generation paths retain their required IR and
generator state. Respect the existing keep-IR debugging option.

The measured teardown split establishes an opportunity, not proof that every
resource can be dropped earlier. Qualify eager sealing with calls made **after**
sealing and after subsequent images compile. About 79 MiB of context data remained
after scratch/IR release in the probe; preserve it until its reachability is
understood. Reconcile the small 0.11 MiB post-finish allocator residue before
claiming that context allocations fully balance. Decoupling whole executable images from MIR contexts is a separate,
larger project if that residue exceeds the final budget.

**Tests:** eager, AUTO synchronous/background and MIR-interpreter paths; clusters,
multiple entries, BSS/constants, recursive/tail calls, imported calls, property
metadata, cancellation and stale-image rejection, shutdown and keep-IR mode.
Run MIR GC stress and tier parity fixtures, then sync publication/restore.

**Gate:** generator scratch and generated IR do not accumulate across sealed
images. Pending scratch follows bounded active compilation, not the number of
already-published functions. Repeated hot-function calls and shutdown remain
correct; no double generator finish or late lazy call into retired state.

## P4 — Return empty GC regions after collection

**Files:** `lambda/runtime/gc/gc_heap.*`, `gc_data_zone.*`,
`gc_object_zone.*`; use the existing VM provider.

Reclaim in order of simplicity:

1. Release surplus empty nursery blocks after successful promotion/compaction;
   retain a small measured warm capacity. Do not reset or release a pinned source
   region still needed by the current collection.
2. Compact tenured data according to survivor/retained-capacity measurements.
   Let the existing one-owner data-pointer fixup release excess old data storage.
3. Identify wholly dead object slabs and bump regions using the collection's
   existing live walk and range index. Before unmapping a region, unlink **all**
   its slots from every size-class free list, including mixed bump/slab lists;
   unregister its ranges/pages/foreign owner; clear relevant mark/ownership
   caches and release allocation-start bitmaps. Repair current/fresh allocation
   cursors. Keep partial live regions at their existing addresses.

This preserves non-moving object identity and precise roots (D5.3.1). Avoid an
extra quadratic per-slot scan or unconditional expensive trim on every small
collection. Reclaim from measured empty capacity at GC/safe idle boundaries;
allocator-wide pressure-relief calls are not the reclamation mechanism.

**Tests:** mixed size classes, surviving objects in old regions, free-list reuse,
empty and partially live bump blocks, repeated grow/collect/reuse, exact ownership
lookup after unmapping, OOM while collecting, forced GC and pointer stability.
Run address/UAF checks separately from release performance qualification.

**Gate:** compact collection leaves capacity proportional to survivors plus a
bounded warm reserve. On the 80 MiB fixture, post-collection occupancy is about
2.3 MiB; it must not permanently require 192.8 MiB of GC regions. Set the first
collector-capacity gate to at most 32 MiB when this survivor set fits, and measure
allocation throughput to protect reuse performance.

## P5 — Bound parser ownership and reduce redundant codec validation

**Files:** `lambda/runtime/lambda-eval.cpp`, a shared owned-result transfer
helper if needed, `lmd/package/sync/codec.ls`; a scoped Input dispatch API belongs
to `lambda-input` if the existing public dispatch cannot accept an explicit owner.

Use the existing grammar parser's private-owner/runtime-result pattern for
runtime JSON/Mark parsing. Reuse the parser dispatcher; do not implement another
JSON/Mark parser. Parse in an invocation-owned Input, transfer the resulting tree
into correctly owned runtime values, then destroy the temporary owner. Transfer
all escaping strings, names, symbols, shapes and out-of-band numeric/decimal/date
storage, not only array/map shells. Preserve map order, value types, sharing/COW
behavior, diagnostics and source-position options (S2.3.1v2, D5.1.3, D5.2.5).
Reuse existing constructors and conversion machinery; isolate any missing common
transfer shape once. No global InputManager reset is safe while other documents
or editor values can still refer to Inputs.

Separately, use the existing exact scalar handling in the codec to avoid Mark
round trips that add no portability check, especially native binary identifiers.
Prove the binary wire spelling/round trip over all byte values, leading zeros,
empty values, NULs and maximum protocol sizes. Keep validation for scalars that
need it and keep canonical re-encoding checks for received data. Cache validation
only for authenticated immutable identities, with a byte-bounded GC-owned cache
whose keys include encoding/schema version. Reuse the existing repository owner
cache before adding another one.

The existing codec can continue to call the same parse interface if ownership
is fixed there. Any proposed public API or observable semantics change needs its
formal ruling reviewed first; this plan requires no new wire format.

**Tests:** repeated valid/invalid parses, every supported codec scalar and nested
container, errors referencing parsed source, surviving values across GC/task
turns, aliases/COW, external editor changes, source positions and independent
open documents. Add owner-count regressions and negative canonical-wire cases.

**Gate:** parsed-document owners do not grow with completed sync rounds. The
current unchanged 80 MiB verification/cloud round adds about 4.8 MiB Mark arenas plus about 4.2 MiB
formatter allocations; both slopes must disappear. Retained source documents
remain valid. If a sibling API is needed, keep its ownership change separately
reviewable under D9.1.1v4.

## P6 — Remove the remaining bounded pack upload copies

**Files:** sync cloud adapter and native HTTP/file capability boundary.

If P1–P5 leave pack copies as a material cost, add file-backed PUT from an
immutable authenticated local pack through the existing HTTP client. Keep the
file/revision capability alive until completion; stream bounded reads and hash
verification without passing ciphertext through SQLite. Preserve cancellation,
retry, incomplete-transfer recovery and cloud head/catalog publication ordering.
For downloaded packs, keep one bounded pack/slice owner and release it before
reading unrelated packs. Do not retain an ever-growing array of decoded records.

**Gate:** payload buffering follows pack/chunk concurrency rather than total
repository bytes. Repeat existing transfer-drop/retry and exact-restore checks.

## Final qualification and proposed budgets

Use a clean normal release build with profiling disabled. Run before/after arms
against identical fixture bytes, fresh repositories and fresh remote namespaces.
Record three independent samples and report medians plus ranges; cold capture,
publication, restore and unchanged warm rounds are separate observations.

| Workload | First peak RSS gate | Post-idle private-footprint gate |
|---|---:|---:|
| 8 MiB / 128 files, publication | 256 MiB | 128 MiB |
| 80 MiB / 128 files, publication | 384 MiB | 192 MiB |
| 80 MiB / 128 files, independent restore | 384 MiB | 192 MiB |

These are proposed first qualification budgets, not measured outcomes or
implementation caps. Also qualify 80 MiB / 1 file and a larger streaming fixture
with explicit capture limits. Peak buffering should not scale linearly with total
file bytes. Use mixed text, image and random/compressible/deduplicated binary data.

Qualify passive polling and explicit unchanged verification/cloud rounds
separately; the measured 9 MiB slope comes from the latter with zero uploads.
After hot functions stabilize, twenty unchanged rounds in each case should add at most
2 MiB of private live ownership and at most 8 MiB post-idle footprint; document
any bounded cache warmup separately. Watch native live bytes, GC committed bytes,
parser owner count, pool live bytes and OS resident/physical metrics independently.
Do not use a late forced collection as the normal sync behavior to meet the gate.
Publication/restore time should stay within 10% of the paired P0 release median;
report any tradeoff explicitly instead of changing fixture contents or chunking.

Run focused tests per phase. At the final core-runtime milestone, run the full
Lambda and Radiant baselines, sync's complete local-S3/negative/recovery suite,
precise-root forced-GC and tier checks. Every restored file must match SHA256
with the sender stopped. Add expected `.txt` results for every new golden script.
No debug or allocation-stack-logging run establishes the performance budget.

## Implementation and qualification — 2026-11-17

P0–P5 are implemented. No wire/schema change, conservative GC roots, vendor edit
or ciphertext in SQLite was introduced. The changes are uncommitted on
`df3ab3e83`, with scoped Input source/build/test changes in the sibling checkout.

| Phase | Result and ownership boundary |
|---|---|
| P0 | Standard full/CLI release builds repaired through the Input resolver API and scoped build declarations; shared core source expansion repaired |
| P1 | `fn_format2` owns staging output per invocation and copies it into the rooted runtime result before releasing the pool |
| P2 | ByteStorage has a shared release lease, once-per-heap backing charge, precisely rooted preflight, and safe cross-thread/late release |
| P3 | Eager MIR images retire generated IR/generator scratch before publication; executable images stay owned; lazy/interpreter/keep-IR contexts retain required state |
| P4 | Whole dead object regions and unused data capacity are reclaimed with ownership indexes/free lists repaired; live object addresses do not move |
| P5 | Explicit JSON/Mark parses use a scoped Input and existing parser/clone machinery, transferring all escaping value/name/shape/scalar ownership |
| Pack downloads | Script policy groups imports by physical pack, bounded to 128 records, avoiding several complete pack bodies in a logical-ID batch |

P4 also stops clearing complete fresh VM extents. The common VM provider supplies
zero pages on POSIX/Windows and explicitly zeros WASM commitments. Per-slot reuse
and calloc initialization remain. This avoids physically faulting unused bump
capacity and worst-case evacuation reservations. Reserved/committed/occupied and
external backing telemetry remain distinct (D5.2.1, D5.2.4, D5.3.1).

P6 file-backed HTTP upload is not implemented. The HTTP adapter still buffers one
bounded pack; source parsing in automatic/other-format modes keeps its previous
Input lifetime. Remaining MIR context accounting is separate from retired eager
scratch. These limits remain visible rather than claiming the entire process
footprint equals the GC live set.

### Qualification method

Clean normal release, AUTO backend, profiling disabled, isolated loopback S3,
fixed identical 8/80 MiB fixture bytes, fresh repository/remote namespace per run.
Three publication/independent-restore samples per size; every restored file is
checked by SHA256 after its sender process exits. One 8 MiB baseline publication
has an 8.824 s connection-stage outlier; the full 3.021–12.520 s range is retained.
Larger 80 MiB/single-file and 256 MiB/128-file cases are separate single samples.
No debug/instrumented timing establishes the performance result.

### Three-run release results before fresh-VM residency fix

These qualify P0–P5 plus pack grouping before the final redundant-clear removal.
Baseline and tuned arms use the same fixture bytes. Values are medians; ranges
are retained to show variability. MiB denotes peak RSS, not registered live bytes.

| Data / operation | Before time | Tuned time (range) | Before RSS | Tuned RSS (range) |
|---|---:|---:|---:|---:|
| 8 MiB / 128 publish | 3.127 s | 3.258 s (3.246–4.303) | 813.56 MiB | 367.64 MiB (367.44–374.13) |
| 8 MiB / 128 restore | 6.951 s | 7.198 s (6.739–7.360) | 728.92 MiB | 357.31 MiB (322.61–357.48) |
| 80 MiB / 128 publish | 8.513 s | 8.878 s (8.786–9.577) | 1032.06 MiB | 430.97 MiB (414.64–478.88) |
| 80 MiB / 128 restore | 13.712 s | 13.658 s (13.420–13.890) | 1499.83 MiB | 441.34 MiB (440.80–441.69) |

All timing medians are within the proposed 10% regression budget. The original
peak RSS gates remain unmet in this arm. Baseline ranges and individual stages
are preserved in `baseline-results.json`; no outlier is removed.

Single samples: 80 MiB / 1 file publishes/restores in 9.514/10.157 s,
368.55/432.45 MiB RSS. The 256 MiB / 128-file case takes 28.566/36.060 s,
440.62/615.94 MiB RSS. Every restore is SHA256 exact. These samples do not establish
a constant memory bound at arbitrary sizes.

The formatter microbenchmark drops from 110 to 45.67 MiB RSS; 64 discarded
1 MiB results add zero retained formatter-pool bytes. Binary pressure drops from
288.38 MiB (128 bodies) to 33.45 MiB for both 128 and 1,024 discarded 2 MiB bodies.

After 5 versus 25 unchanged rounds in the native retained-host checkpoint:
committed GC capacity is 16.23 MiB both times, external backing is zero after
collection, allocator usage is 26,734,941 versus 26,731,757 bytes, and Input arena
counts/bytes are identical. Physical footprint is 251.6 versus 254.0 MiB. This
post-procedure, worker-stopped, forced-collection checkpoint meets the live-owner
and GC-capacity gates; it is not the normal CLI idle measurement.

In the normal 25-round CLI run, every unchanged round uploads zero objects, but
private footprint varies and jumps from 275 to 339 MiB at round 25. VM maps put
the 64 MiB step in VM_ALLOCATE rather than malloc. The final fresh-VM change fixes
the identified redundant whole-extent clearing; a new 20-round idle/passive-poll
qualification is pending at wrap-up. Idle-footprint budgets are not claimed met.


### Final fresh-VM wrap-up check

Normal release SHA256
`130e5cb53f176d02d5ac1d22c752118a90a08062fa9a22bf1362aced8565f9d6`.
One fresh 80 MiB / 128-file local-S3 run publishes in **10.022 s**,
**384.08 MiB RSS**, and independently restores in
**13.884 s**, **409.23 MiB RSS**.
Every file matches SHA256. Capture is 5.890 s; the cloud publication stage is
2.930 s. Build, GC and Input native tests pass on this final tree.

This single sample does not replace the earlier three-run median qualification.
Its publication time is higher than the earlier medians; repeat timing and
normal idle/passive-poll measurements are pending after the user's wrap-up
request. The proposed 384 MiB peak gate is narrowly missed by publication and
still missed by restore. No claim is made that all original budgets are met.
Evidence: `final-results.json`, `wrapup-80.log`, `gc-release-lazy.log`,
`input-release-lazy.log`, `lambda-baseline-lazy.log` under the task artifact dir.

### Regression coverage and known independent failures

The full Lambda baseline passes 7,590/7,590 after the last fresh-VM change.
Final release native coverage includes 112 GC/lifetime and 24 Input tests.
Before the final redundant-clear removal, 23 focused MIR image/cache/publication/
cancellation cases passed. Native tests exercise backing aliases, two heap admissions,
release after heap destruction/on another thread, forced precise GC, free-list
reuse after unmapping and stable live addresses. New goldens cover all 256 byte
values, maximum codec size and over-limit rejection, nested parsed values and
empty/NUL/Unicode names.

Before the final redundant-clear removal, local S3 protocol/negative/recovery
checks and the mixed-format demo pass,
including interrupted upload/retry, offline restore, snapshots, rename/edit,
checksum tampering, lost commit response, revoked membership, periodic fallback
and killed-client resumption. Earlier CLI release parser/codec smoke checks pass.
Generator regressions pass.

The Radiant baseline has 8,170 passes, 297 partial, 20 skips and one failure:
`BaseReplacementPreservesTheLoadersUrlCarrier`. The loader mutates its URL before
the new Input resolver runs; this is recorded in the owning Radiant ledger as
RI-107. No Radiant source/reference/test was modified.

The optional full concurrency suite reproduces the existing Input LI-027
libuv teardown spin at `ModuleStateSlabsArePrivateToEachEvalContext`, after 97
checks. The sampled process was stopped; that suite is not a pass. Focused
compiler ownership/cancellation checks pass separately. The existing empty-text
binary solidity defect (S2.2.2v2) is recorded separately in the central ledger;
its wider IO boundary migration is outside this ownership change.

Evidence: `temp/sync-memory-impl/`; `pre-lazy/` preserves the prior qualification,
final release SHA256 is `130e5cb53f176d02d5ac1d22c752118a90a08062fa9a22bf1362aced8565f9d6`.
The source diagnosis remains in the linked diagnosis record. Native retained-host
post-procedure collection checkpoints are distinguished from normal live-loop
idle measurements; they are not substituted to satisfy idle budgets.

