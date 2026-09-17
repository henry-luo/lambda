# Lambda / LambdaJS Memory Tune

- **Date:** 2026-09-17
- **Status:** IN PROGRESS — Phase A.1 counters-only allocation telemetry,
  Phase B.1 ArrayBuffer backing-store pressure, Phase C full-tenured
  evacuation, and the Hyphen static-literal lowering landed on 2026-09-17.
  Full Phase A snapshots, Phase B.2/B.3 adopters, and Phase D remain open.
- **Scope:** Peak-process-memory reduction in the shared Lambda heap and
  LambdaJS, beginning with the Result45 memory census. This is an
  implementation plan; it introduces no language-semantic or formal-design
  ruling.
- **Input:** `test/benchmark/Memory_Comparison_Result45.md` and its linked
  `memory_results.json`. The report is measurement evidence, not an authority
  over the formal specifications.
- **Related:** `vibe/impl/Lambda_Impl_Tune21 (done).md` §T21-4 (the existing
  tenured-data residue), `vibe/impl/JS_Tune13_Impl.md` T13-8 (external dense
  array ownership), `vibe/Lambda_Garbage_Collector2.md` §5.6 (the already
  designed future tenured compaction), and `doc/Doc_Convention.md`.
- **Formal authority:** D4.2.2–D4.2.5v3 (one `MemContext`/VM substrate and
  release-safe tracking), D4.3.1–D4.3.3 (stable object zone and one-fixup data
  compaction), D4.4.1–D4.4.6 (COW cost and correctness), D5.3.2–D5.3.3
  (collection only at a rooted `MAY_GC` boundary), D8.5.1v3 (static recipe
  code image and fresh materialization), and S9.1.2–S9.1.3 (value/COW semantics
  and `var` borrows).

---

## 1. Decision frame

The memory runner reports `/usr/bin/time -l` maximum RSS for a fresh process.
It therefore includes source loading, JS parse/lowering, JIT code and metadata,
execution allocations, allocator retention, and shutdown work. It is neither a
live-heap measurement nor evidence of a leak by itself.

The goal is to lower retained and avoidably delayed memory without changing
observable Lambda or JavaScript behavior, adding per-allocation locking, or
tuning a benchmark by name. All timing and memory comparisons use an archived
`make release` binary, three fresh process samples, the same runner settings,
and the same host. Debug/ASan results may diagnose ownership only; they never
establish a performance result.

### 1.1 Result45 working set

| Row | Reported peak | Primary finding | Tune responsibility |
|---|---:|---|---|
| `awfy/havlak` LambdaJS | 2.65 GiB | allocation-heavy graph/vector churn; adaptive thresholds reached a 256 MiB nursery and about 153 MiB object-pressure trigger | shared GC retention and attribution |
| `text/hyphen` LambdaJS | 1.28 GiB | its 118,479-byte recursive static pattern literal lowered to 284,563 executable MIR instructions; the MIR linker/optimizer then copied and expanded that code image | compact recursive static-literal recipe lowering |
| `kostya/brainfuck` Lambda | 198.8 MiB | 10,000 transient 30,000-element tapes; each survivor copy enters unreclaimed tenured data | tenured-data full collection |
| `kostya/brainfuck` LambdaJS | 286.5 MiB | 10,000 `Uint8Array(30000)` backing stores = 300,000,000 bytes (286.1 MiB), almost the whole result | external-memory pressure accounting |
| `jetstream/splay` Lambda typed | 227.9 MiB | large live value graph plus map/path COW allocation | distinguish live footprint from reclaimable retention |

`jetstream/splay` is deliberately not a LambdaJS memory result: the runner
does not execute the LambdaJS JetStream rows. Its initial 8,000 nodes each
construct a depth-5 payload tree (63 maps), so roughly 504,000 payload maps
are live before the 4,000 insert/delete cycles begin. Its COW rewrites are
governed by S9.1.2 and S9.1.3, not an accidental JavaScript-style pointer-tree
implementation. The existing source comment citing D3.3.1 should eventually
be corrected: D3.3.1 covers inference; S9.1.2 is the applicable value/COW
ruling. That documentation correction is independent of this tune.

### 1.2 Established mechanisms

1. **Uncharged GC-owned native payloads (resolved for ArrayBuffer storage).**
   `js_typed_array_new()` formerly allocated an ArrayBuffer `ByteStorage`
   before the GC saw pressure from its bytes. Phase B.1 now preflights at a
   rooted allocation boundary, records the successful physical storage, and
   releases its charge only when the refcounted storage actually dies. A
   `Binary` snapshot can therefore retain the charge after its ArrayBuffer is
   detached or swept; a dead 30 KiB tape instead becomes collectible at the
   next external-pressure boundary.

2. **High-water data and object extents.** A nursery block is fully committed;
   `gc_data_zone_reset()` rewinds it but retains every block. Object-zone sweep
   returns slots to size-class free lists, but releases no slab until heap
   destruction. This reuse is valid and fast under D4.3.2v2, but process RSS
   records the high-water mark.

3. **Append-only tenured residue (resolved by Phase C).** The former path
   copied nursery survivors into `tenured_data`, then could only return old
   tenured extents at heap destruction. Phase C now evacuates both source zones
   into a preallocated fresh tenured zone once mature tenured storage reaches
   its generic threshold. Each marked buffer keeps D4.3.1's one owner-pointer
   fixup; the evacuated old tenured VM extents are released in the collection.

4. **Recursive static source data was being compiled as executable work.**
   Hyphen's pattern table is a large nested literal with no dynamic values.
   Earlier fast paths recognized only flat numeric arrays and flat primitive
   maps, so recursive arrays/maps expanded into 284,563 MIR instructions.
   `MIR_link` then dominated the process peak while copying and optimizing that
   image. The new recipe represents the immutable literal shape in the input
   pool and materializes a fresh JS graph on each evaluation, as D8.5.1v3
   requires. It emits 29,814 instructions for Hyphen without sharing mutable
   arrays or objects between evaluations.

5. **Workload allocation is real.** Havlak creates a large temporary graph and
   Splay retains a large graph by construction. A memory tune must not claim
   that all reported RSS is reclaimable, nor weaken COW's unobservable sharing
   requirement (S9.1.2, D4.4.1–D4.4.6).

---

## 2. Non-goals and invariants

- Do not lower global thresholds as a substitute for ownership accounting or
  reclamation. Tune21 established that fixed 3 MiB nursery collection made
  Havlak spend substantial time marking its growing graph; its adaptive pacing
  is retained until a measured replacement proves better.
- Do not special-case any benchmark source, workload name, allocation size, or
  iteration count.
- Do not turn `memtrack` into a release-build per-allocation registry or a
  lifetime/reclamation authority. D4.2.5v3 requires every non-`lib/` raw
  allocation to use its corresponding `memtrack` API, while release telemetry
  remains counters-only and has no per-allocation global lock on hot paths.
- Do not move GC object structs, use a conservative stack scan, or introduce a
  write barrier. D4.3.1 fixes stable `Item` pointers and one owning-pointer
  fixup per surviving variable-size buffer.
- Do not change JS ArrayBuffer/TypedArray ownership, resizable-buffer,
  detachment, or view semantics. A buffer is charged once to its owning
  ArrayBuffer; views do not acquire a second charge.
- Do not change COW eligibility merely to reduce memory. A false unique result
  is a semantic error under S9.1.2 and D4.4.1.

---

## 3. Phase A — release-safe memory attribution

### 3.1 Counters

Add a `LAMBDA_MEMORY_STATS=1` counters-only report to the shared heap and
associated `MemContext` owners. The disabled path is one predictable branch;
the enabled path uses local integer increments only. It must not reuse the
debug allocation registry.

Phase A.1 implements the common allocation half of this work: with
`MEMTRACK_MODE=STATS`, `LAMBDA_MEMORY_STATS=1` reports current and peak bytes
by `memtrack` category at process completion. STATS stores a small size/category
header adjacent to each allocation and updates atomics directly; it has no
allocation pointer registry or global registry lock. DEBUG retains the guarded
registry for diagnostics. This is the release-safe distinction required by
D4.2.5v3, but it is not yet the lifecycle snapshots and per-owner attribution
specified below.

Report current, peak, and capacity/reserved bytes where meaningful:

| Family | Required counters |
|---|---|
| Object heap | live object bytes/count, large-object bytes/count, slab reserved bytes, free-slot bytes |
| Nursery data | used bytes, committed capacity, block count, high-water capacity, collections caused by data pressure |
| Tenured data | allocated capacity, marked-live bytes at a full scan, reclaimable bytes, full-compaction count |
| External GC-owned data | current/peak/bytes-since-collection by owner kind: ArrayBuffer, JS dense external array, Lambda Binary, and future adopted kinds |
| Execution ownership | source/AST/MIR cache bytes, parse/JIT context peak, final cleanup bytes |

Emit snapshots at `parse complete`, `compile complete`, `execution complete`,
`post-final-collection`, and `heap destroy`. The normal benchmark output stays
unchanged; the opt-in report is a diagnostic side channel. At the process
level, the runner continues to record RSS separately.

### 3.2 First acceptance gate

Before changing policy, capture a current release baseline for the five rows in
§1.1 plus representative short-lived and long-lived workloads. The counters
must reconcile at every snapshot:

- `current <= peak` for every counter;
- ArrayBuffer charge changes exactly once per backing-store ownership change;
- external current bytes return to zero after realm/heap destruction;
- post-full-collection tenured live bytes equal the data reachable through
  marked owners, within explicit alignment padding;
- a stats-enabled release run has no material timing regression against the
  same binary with statistics disabled.

This phase is required before assigning exact byte shares to Havlak and other
remaining high-water workloads. Hyphen's prior peak has a separate root cause:
the static literal's MIR code-image expansion, established by instruction
counts and linker allocation stacks rather than allocation attribution.

---

## 4. Phase B — external payload pressure for LambdaJS and Lambda

### 4.1 Shared contract

Introduce a small GC-facing external-payload account, owned by `gc_heap_t`,
not a second allocator:

```
preflight at MAY_GC -> native allocation -> record successful physical payload
last physical-storage release -> release charge
```

The account carries `live_bytes`, `peak_bytes`, `bytes_since_collection`, and
an owner-kind counter. It has no pointer registry and does not determine object
reachability. The mark/sweep graph remains the only liveness authority.

`gc_external_preflight(heap, bytes, kind)` is called before allocating a
GC-owned native payload. It may collect only at its caller's existing
`MAY_GC` boundary; otherwise the caller must preflight earlier, before it owns
an unrooted GC value. This is required by D5.3.2 and D5.3.3. The successful
allocation calls `gc_external_record_alloc()`; failed allocation and wrapper
publication therefore create no phantom charge. `gc_external_record_release()`
never starts a collection and runs from the physical `ByteStorage` release
callback, rather than assuming that the ArrayBuffer wrapper is the last owner.
Resize and transfer install a separately charged replacement store; shared
views charge zero.

Phase B.1 uses bytes allocated since the last completed collection as its
generic trigger and reports live external bytes separately. Later policy work
may combine the two only after Phase A establishes the needed release data. It
uses the same collector-time budget vocabulary as the existing data/object
pacing and is never encoded for 30 KiB Brainfuck tapes.

### 4.2 Adoption order

1. **ArrayBuffer / TypedArray / DataView / Node Buffer.** Charge exactly the
   `ByteStorage` owned by `JsArrayBuffer`. Typed-array and DataView wrappers
   are views and charge zero. Cover fixed, resizable, detached, shared-storage,
   and Node Buffer paths. This is the direct Brainfuck fix.

2. **Existing JS external dense arrays.** Keep T13-8's intrusive ownership
   model. Route its owned-buffer allocation/release through the same accounting
   API rather than a JS-private pressure mechanism.

3. **Lambda Binary and other GC-finalized native buffers.** Adopt only after
   auditing each constructor, clone/share path, and finalizer. Input arenas,
   immutable source storage, and non-GC `MemContext` owners are explicitly not
   GC external payloads. The generic API permits their own context accounting
   without making the GC reclaim what it does not own.

### 4.3 Phase B.1 implementation record

The shared collector now owns `gc_external_stats_t` and reports total live,
peak, since-collection, and external-pressure collection counts through the
existing opt-in `LAMBDA_GC_STATS=1` teardown record. `gc_external_preflight()`
uses the generic `GC_EXTERNAL_PRESSURE_THRESHOLD`; it is not keyed to a
benchmark allocation size. The counter is reset only after a completed
collection.

`JsArrayBuffer` construction, resizable grow/shrink, transfer, detach, COW
write preparation, and final destruction route through this account. The
charge belongs to the `ByteStorage` release callback so an alias retained by
`Binary` remains charged until its final reference disappears. The implementation
has no allocation registry, lock, or raw libc allocation; it uses the
corresponding `memtrack` APIs required by D4.2.5v3. The focused GC regression
`GCHeapTest.ExternalPayloadPressureTracksPhysicalLifetime` covers accounting,
the exact threshold edge, collection reset, and release balance.

### 4.4 Correctness gates

- Add focused JS tests for repeated `Uint8Array`, shared ArrayBuffer views,
  `DataView`, Buffer aliases, resize/detach, construction failure, and realm
  destruction. Verify charge balance and no double finalization.
- Add Lambda Binary ownership tests when that adopter lands.
- Run Test262, Lambda baseline, and forced-GC/poison sweeps with collection at
  every legal allocation boundary. Failed allocation and wrapper publication
  must leave no external charge because recording occurs only after storage
  allocation succeeds.
- A release Brainfuck run must show bounded external peak relative to the
  selected policy and released dead tape storage before process exit. Its time
  trade-off is reported, not hidden.

---

## 5. Phase C — full tenured-data collection

### 5.1 Implemented algorithm

At an ordinary completed mark, the current generic policy admits a full-data
collection after `GC_TENURED_FULL_COMPACT_THRESHOLD` (two data-zone blocks) of
tenured allocation. It then:

1. Preallocates a fresh tenured data zone sized to the current nursery plus old
   tenured allocation. Reservation failure leaves both source zones unchanged
   and falls back to ordinary nursery promotion.
2. Reuses the existing compactor once with the nursery as source and once with
   old tenured data as source. Both passes write directly to fresh tenured
   storage, so no surviving allocation is copied through an intermediate zone.
   Existing view rebinding, embedded scalar-tail fixups, and the specialized
   JS-function environment compactor remain the single shared owner walk.
3. Restores the nursery source, releases the old tenured VM extents, then
   sweeps dead object owners and resets the nursery as usual.

This retains D4.3.1: object structs and `Item` pointers never move, and each
surviving data owner receives at most one pointer fixup. It does not require a
remembered set or a write barrier. It implements the full compaction described
in `Lambda_Garbage_Collector2.md` §5.6 by reusing the existing compacting owner
walk rather than duplicating it.

### 5.2 Shared-walker audit

Generalize the existing nursery-only `gc_compact_data()` ownership walk; do not
copy its cases into a second full-GC walker. The shared walk must cover maps,
elements, arrays, numeric array shapes/tails, packed data, list items,
functions/closure environments, and every registered VMap/native compaction
callback. The audit must identify every raw pointer that may address data-zone
storage and prove it is either an owner slot fixed by the walk or absent at the
`MAY_GC` boundary.

The initial policy uses one generic threshold, with a measured floor:

- do not compact a tenured zone below two mature nursery blocks;
- allocate the temporary fresh zone before modifying an owner pointer;
- report full-compaction count and released old-tenured bytes through
  `LAMBDA_GC_STATS=1`;
- tune hysteresis from a release matrix before adding any more elaborate
  reclaimability heuristic.

Brainfuck is the primary correctness/performance case: most old tenured tape
copies are dead, so a full-data collection should collapse them to the current
live tape rather than accumulate one copy per minor collection. Havlak and
Hyphen validate that compaction does not turn a high allocation rate into an
unacceptable pause.

### 5.3 Verification and measured result

- `GCHeapTest.FullTenuredCompactionReleasesDeadPromotedBuffers` repeatedly
  promotes a 4 MiB list backing buffer, drops it, then proves that the full
  collection returns the old >=8 MiB tenured allocation while preserving the
  rooted survivor.
- Extend the existing forced-GC + poison corpus so a collection can occur
  between every owner copy/fixup step. Run all three Lambda execution tiers.
- Verify VMap and hosted-language native callbacks remain allocation-free while
  tracing/finalizing, per D4.3.3.
- Run `make test-lambda-baseline`, `make test262-baseline`, and the targeted
  JS buffer suites before a release memory comparison.

One fresh 2026-09-17 release sample measured Lambda-U Brainfuck at **49.1 MiB**
and Lambda-T at **49.3 MiB** peak RSS, compared with Result45's 199.1 MiB and
198.8 MiB respectively. Both ran 714 collections and 20 full evacuations,
releasing 168,016,960 tenured bytes. These are targeted validation samples,
not the three-run acceptance matrix.

---

## 6. Hyphen — recursive static-literal recipes

### 6.1 Root cause and implementation

Hyphen's large static hyphenation table was semantically constant source data,
but the previous lowering emitted the construction of every nested array and
object as executable MIR. The resulting 284,563 executable instructions were
not a live JS heap graph or a GC pacing failure: stack sampling placed the
dominant allocation inside `MIR_link` while its linker/optimizer duplicated the
oversized code image. MIR is a vendored dependency, so the fix is on the
LambdaJS lowering side, before it reaches the linker.

`JsStaticLiteralRecipe` now records a recursively side-effect-free literal
shape in the parser input pool. It admits only immediate values, strings,
array holes, arrays, and ordinary data-property maps; it declines computed
keys, spreads, accessors, methods, `__proto__`, shorthand, and any dynamic
child. At execution, `js_static_literal_from_recipe()` roots its receiver and
children across allocation, materializes the shape in source order, preserves
array holes and duplicate-key last-writer behavior, and returns a new mutable
array/object graph for every evaluation. The original flat literal fast paths
remain for their smaller representations. This maintains D8.5.1v3's rule that
the recipe is code-image metadata, not a shared mutable runtime literal.

### 6.2 Correctness and result

`JsOpt.StaticCompositeLiteralRecipePreservesFreshNestedValues` checks nested
freshness after mutation, holes, duplicate keys, and property order; it also
exercises the recursively lowered form. A direct release MIR inspection shows
the `js_static_literal_from_recipe` call. The existing flat numeric-array and
primitive-object recipe tests continue to pass.

A fresh release Hyphen run preserved checksum `1183296`, reduced the code image
to **29,814** executable MIR instructions, and recorded **116.3 MiB** maximum
RSS (121,995,264 bytes) versus Result45's 1.28 GiB. Its compile-and-link time
was 213.2 ms and total measured time was 330.9 ms. This is one targeted sample,
not the final three-sample acceptance matrix.

---

## 7. Phase D — return safely reusable high-water capacity

This phase follows Phase C because it is a capacity policy, not a substitute
for recovering dead tenured data.

1. **Nursery tails.** After all live nursery data has moved, release or
   decommit only unused tail regions beyond a hysteresis floor. No live owner
   can point into the reset nursery, so this is a VM-region policy change under
   D4.2.2 and D4.3.2v2. Preserve enough blocks for the measured steady state.

2. **Object-zone regions.** Consider returning only a whole region for which
   every slab is empty, all free-list/range metadata is updated atomically, and
   no stable live object is affected. Do not release individual slabs merely
   because some slots are free. This remains a later slice because D4.3.1
   requires stable live object addresses and the current ownership index has to
   be audited first.

3. **Context attribution.** Parse/JIT/module contexts must release their own
   completed-workspace capacity through their `MemContext`; the GC must not be
   made responsible for compiler memory. Phase A establishes whether this is a
   material Hyphen contributor.

Each region-return change is separately measured. A fast repeated workload may
prefer retained capacity; the policy must use a stated floor/hysteresis rule
rather than a benchmark-specific branch.

---

## 8. Measurement and acceptance plan

### 7.1 Order of work

| Order | Deliverable | Exit criterion |
|---:|---|---|
| A | counters and phase snapshots | A.1 category counters landed; remaining byte accounting reconciles and no material release-time effect with stats disabled |
| B | ArrayBuffer external pressure | Brainfuck external storage is charged/released exactly once; JS semantic suites and stress runs pass |
| C | full tenured-data collection | implemented; focused promotion/release regression and targeted release Brainfuck samples pass |
| D | nursery/object/context capacity return | only measured high-water residue is returned; no allocation-thrash regression |
| E | result report | fresh release RSS/time matrix, counter evidence, commit/binary hashes, and explicit regressions |

### 7.2 Benchmark matrix

Every phase runs fresh release samples for:

- Lambda and LambdaJS Brainfuck;
- LambdaJS Havlak and Hyphen;
- typed and untyped Lambda Splay, reported separately from any JS result;
- a short-lived allocation workload, a long-lived document/value workload, and
  at least one Test262 buffer-heavy case;
- the full Lambda and Test262 baselines plus forced-collection stress.

Report median and all samples for wall time and peak RSS. A claimed memory win
must include Phase A counters that identify released/avoided bytes. A memory
decrease paired with an unbounded pause or a semantic/test regression is not a
win.

### 7.3 Expected, but not pre-claimed, outcomes

- LambdaJS Brainfuck no longer retains nearly all 10,000 dead typed-array
  backing stores until shutdown: a targeted release sample measured 53.7 MiB
  peak RSS, 96 external-pressure collections, a 3.15 MiB external peak, and
  zero external live bytes at teardown (Result45: 286.5 MiB).
- Lambda Brainfuck no longer retains each dead promoted tape after a full-data
  collection; the release samples are recorded in §5.3.
- Hyphen no longer sends a recursively static source table through per-node MIR
  lowering; the targeted result is recorded in §6.2.
- Havlak should expose its true split among live graph, compiler contexts,
  external payload, and allocator retention; only the latter three are
  candidates for reclamation. One targeted release sample is 623.2 MiB versus
  Result45's 2.65 GiB; its exact retained-byte attribution remains open.
- Splay retains its required live graph and COW semantics. The tune may recover
  stale high-water data but does not promise to erase the graph's inherent
  footprint. A targeted typed release sample is 259.8 MiB, compared with
  Result45's 227.9 MiB, so this change makes no memory-improvement claim for
  Splay. COW copy-count reduction remains the separate D4.4/Tune28 track.

No row receives a numerical RSS target until Phase A establishes an exact,
archived-release baseline and the counters explain which bytes are avoidable.

---

## 9. Completion criteria

This proposal is complete only when:

1. release-safe accounting distinguishes GC object/data, tenured capacity,
   external payloads, and parse/JIT contexts;
2. external GC-owned payloads have one safe preflight/commit/release lifecycle;
3. at least ArrayBuffer storage and Lambda Binary (after its audit) use that
   lifecycle without double counting or finalization hazards;
4. a full tenured-data collection returns dead promoted capacity while retaining
   D4.3.1's stable-object guarantee;
5. every change passes the required Lambda, JavaScript, and forced-GC gates;
6. an updated memory report records both RSS and internal counter evidence,
   including any time/RSS trade-off; and
7. no formal ruling has been changed silently. If implementation reveals that
   data-owner relocation, hosted payload ownership, or a pressure policy needs
   a new semantic/design ruling, stop and escalate under `Doc_Convention.md`
   §2 before landing it.
