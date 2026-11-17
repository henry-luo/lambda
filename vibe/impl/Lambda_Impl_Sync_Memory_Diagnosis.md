# Sync release memory diagnosis

**Date:** 2026-11-17  
**Status:** Detailed follow-up complete; proposed implementation plan written; runtime fixes remain open.  
**Authority:** D5.1.3, D5.2.2–D5.2.5, D5.3.1, D8.3.3–D8.3.4.

## Checkpoint

- Profiled the same release executable used for streaming-pack qualification:
  SHA256 `7bcdabf0e4d7706a111748fef15af5d5b884fe02902d5775ef87c3752db61a25`.
- Isolated random fixtures and local S3 only. No user sync directory, profile,
  credentials or Wasabi objects were used.
- Confirmed three independent costs: retained MIR compiler state, binary payload
  bytes invisible to GC pressure, and Input lifetime retention. The detailed follow-up below now separates GC region capacity,
  formatter-owned pool allocations and other pool facades.
- Temporary native probes were removed without producing a replacement host.
  A release rebuild hit unrelated undeclared DOM URL-resolver calls. Attribution
  therefore uses the existing release host, OS allocation traces and its existing
  memory/GC counters. No runtime fix or new performance result is claimed.
- Native sources and original host are unchanged. Investigation artifacts and
  scripts are under `temp/sync-memory-diagnosis/`.

## What owns the memory

A fresh 80 MiB / 128-file tree was published to the isolated local S3 fixture.
The script pauses after publication so the compiler/runtime still exist while
`vmmap` and `heap` inspect them. Release `MEMTRACK_MODE=STATS` is enabled for
attribution; these observations are diagnostic samples, not timing benchmarks.

| Observation | AUTO | Strict interpreter |
|---|---:|---:|
| Sampled process RSS through publication | 1022.5 MiB | 715.8 MiB |
| Live native malloc blocks after publication | 4,859,872 | 247,215 |
| Live native malloc bytes after publication | 541.1 MiB | 178.4 MiB |
| Resident `VM_ALLOCATE` regions after publication | 271.5 MiB | 253.9 MiB |
| Private physical footprint at inspection | 834.5 MiB | 701.7 MiB |
| Private physical footprint peak reported at inspection | 855.1 MiB | 701.7 MiB |
| Tracked `system` bytes after runtime teardown, before InputManager cleanup | 86.2 MiB | 86.2 MiB |

The last row is a **subset** of the native allocation ownership, not an extra
86 MiB to add to the table. The last retained pack-sized malloc allocation in
AUTO is 15.0 MiB. Free allocator capacity and shared/clean pages also affect RSS.
For example, the interpreter malloc-zone report contains 170.4 MiB of dirty/free
fragmentation; live-allocation reduction need not immediately reduce RSS equally.
RSS, private footprint, live bytes and reserved address space are different
metrics. Do not add their values together. The 64 MiB reserved VM region in the
reports has zero resident pages.

Artifacts: `publication-1794908306974390000/{vmmap.txt,heap.txt,rss.json,runtime.log}`
and `publish-interp-1794908662189030000/` under the investigation directory.

### MIR retains compilation state with callable images

AUTO has **362.7 MiB more live native heap and 4,612,657 more allocations** than
the interpreter in the publication comparison. A separate allocation-stack run
attributes 4,607,961 live blocks to MIR call paths. The near-equal block difference
and the tier comparison identify compiler state as the largest avoidable native
heap component. The stack run attributes 449.5 MiB to those blocks, but stack
logging changes malloc sizes and adds overhead: that is **not** the production
byte count and must not be substituted into the uninstrumented table.

The release host is stripped. The investigation matches unique native instruction
sequences against the existing, unchanged `mir.o` / `mir-gen.o`, masking relocated
instructions. All 237 recovered entry addresses lie in the MIR code region.
Traces identify register allocation, live ranges/bitmaps, SSA/definition tables,
register/string tables and MIR instructions. Symbol aliases can share code; the
component attribution is stronger than an exact per-function byte breakdown.
The scripts and mappings are preserved as `symbolize_{mir,relaxed}.py`,
`mir-symbols-relaxed.json`, and `publish-malloc-mir-summary.json`.

The Lambda-side ownership chain explains the retention:

1. `mvp_compile_function_image()` creates a private MIR context for a promoted
   image, as required by D8.3.4.
2. The image retains that context and its compiler snapshot with the callable
   entries. Scratch tables and allocator state remain reachable through it.
3. This image path does not call the existing `jit_release_generated_ir()` helper.
   That helper currently runs on the ordinary whole-module path.
4. `MIR_gen_finish()` runs from `jit_cleanup_mode()` at image destruction. Until
   then, generation scratch such as register-allocation bitmap arrays and live
   range free lists can remain allocated alongside code.

Generated machine code itself is small: the full VM report in the stack-profiled
publication has only **3.84 MiB resident executable VM**; about **266.4 MiB** is
writable/non-executable VM. Those figures are from a separate run. Large compiler
working-state retention must not be described as unavoidable machine-code size.

### Binary payload allocations do not trigger GC pressure

`heap_binary_from_bytes()` allocates a native `ByteStorage` body, then a small
GC-managed `Binary` descriptor. `heap_binary_from_storage()` also handles shared
storage views. Neither allocation path charges the payload to GC external-byte
pressure. The binary finalizer releases the storage, but only when a collection
actually discovers that the descriptor is dead. Descriptor pressure alone can
therefore remain tiny while unreachable payloads consume hundreds of MiB.

Minimal release reproducer: load one 1 MiB binary, then repeatedly concatenate
it with itself and hash the resulting 2 MiB binary. Only the final 32-byte digest
is retained by the script.

| Iterations | Peak RSS | Tracked container allocation peak | GC collections |
|---|---:|---:|---:|
| 1 | 31.7 MiB | 2.1 MiB | 0 |
| 128 | 289.8 MiB | 256.1 MiB | 0 |

Both runs report `external_live=0`, `external_peak=0`, and no external-pressure
collections. This is deferred reclamation from missing pressure accounting, not
a failure of the binary finalizer to free memory at runtime teardown. Streaming
file reads are already bounded to 1 MiB blocks; that bound does not limit the
sum of dead blocks waiting for GC. Pack and crypto temporaries take the same path.

Reproducer: `temp/sync-memory-diagnosis/_binary_pressure.ls`, driver
`run_binary_pressure.py`, results `binary-pressure.json`, and `binary-{1,128}.log`.

### Parser lifetime and VM pool retention are additional costs

As already recorded in the Input retention issue, `fn_parse2()` returns an
Input-owned tree. `InputManager` retains each Input until its pool is reset or
torn down (D5.1.3, D5.2.5). Sync codec decoding invokes this path. Both publication
runs retain exactly 90,352,749 tracked `system` bytes before InputManager cleanup,
versus about 1.95 MiB for an empty scan. This category includes arena and owner
bookkeeping; **86.2 MiB is not an exact isolated parser-tree payload count**.
It nevertheless rules out assigning the entire GiB to parser retention.

The VM report is also a capacity measure, not a live object census. The collector
retains object slabs and bump blocks until heap teardown; nursery reset rewinds
cursors and keeps its blocks. Tenured full compaction can release some data
storage. Code inspection explains why swept objects need not return resident
pages immediately (D5.3.1). Pool extents share this VM category. The near-equal
interpreter VM residency shows that the 250+ MiB of VM is not primarily native
machine code. The detailed follow-up below divides GC capacity from the other pool owners
and measures occupancy after collection.

## File count versus byte count

Separate scan-only runs include first capture, inventory and one unchanged
verification rescan. They use independent random fixtures and the same release
host/counters. No cloud transfer is involved.

| Input | Peak sampled RSS | Tracked container peak | Retained tracked system bytes |
|---|---:|---:|---:|
| Empty | 143.8 MiB | 0.3 MiB | 1.9 MiB |
| 8 MiB / 128 files | 569.4 MiB | 18.4 MiB | 55.3 MiB |
| 80 MiB / 128 files | 659.8 MiB | 126.4 MiB | 61.0 MiB |
| 80 MiB / 1 file | 691.2 MiB | 443.3 MiB | 2.5 MiB |

Both dimensions matter. Many files produce many metadata/codec objects and hot
functions. A large streamed file produces much more native binary allocation
pressure while allocating relatively few GC descriptors; the one-file run has
only one collection. It already uses streaming, not a whole-file read. This is
consistent with the independently reproduced binary-pressure defect.

An 8 MiB / 128-file scan/rescan tier comparison gives 301.7 MiB interpreter,
569.4 MiB AUTO and 668.0 MiB eager JIT. Inputs are independent samples; scheduling,
GC thresholds and allocator reuse differ. Tier selection is a diagnostic, not a
proposed sync workaround.

## Initial fix-order outline

The detailed follow-up and [proposed plan](Lambda_Impl_Sync_Memory_Tuning.md)
below supersede this initial outline.

1. **Account binary storage pressure.** Preflight at legal precisely rooted
   allocation boundaries; charge physical backing storage once, not each slice.
   Match release to storage lifetime, including views/shared storage, cancellation
   and cross-owner cases. Re-run the zero-collection reproducer and large-file
   capture; no conservative native-stack scanning (D5.3.1).
2. **Separate compiler scratch lifetime from executable image lifetime.** Use
   Lambda's existing MIR APIs to release generated IR and finish generator scratch
   once every retained entry is eagerly generated and sealed. Audit lazy thunks,
   pending image publication, failure/cancellation and destruction flags first.
   Preserve MIR-interpreter behavior and D8.3.3–D8.3.4. No vendor patch is proposed.
3. **Measure and contract excess GC-owned VM capacity.** Attribute live versus
   reusable bytes before changing slab/bump/data-zone reclamation. Keep stable
   object pointers, precise ownership and the non-moving contract (D5.3.1).
4. **Bound parse ownership.** Decode into an appropriate reclaimable owner or
   copy validated results into GC-owned storage before releasing an Input. Never
   destroy an Input while its returned tree can remain reachable. Codec/schema
   canonical validation must remain intact.
5. Re-run matched release publication/restore checks, large single-file capture,
   many-file capture and repeated unchanged sync rounds. Track live bytes, peak
   RSS and post-idle footprint independently. File-backed HTTP upload remains a
   later bounded-buffer improvement; it does not address the main compiler cost.

## Build limitation

`make build-release-compile` failed in `lambda/dom/dom.cpp` at the two existing
calls to undeclared `dom_document_resolve_url` (lines 1769 and 6140 in this tree).
The exact source/build error is in `temp/build_split-runtime.log`; the investigation
build wrapper log is `temp/sync-memory-diagnosis/build.log`. No DOM workaround or
third-party edit was made. The original release host hash remained unchanged.

## Detailed follow-up: lifetime, capacity and residency

The [proposed implementation plan](Lambda_Impl_Sync_Memory_Tuning.md) supersedes
the initial fix-order outline above. This follow-up uses an isolated full-host
release profiler, built from the preserved release runtime archive with three
instrumented objects: concurrency checkpoints, parse/format counters, and a
public-API MIR allocator adapter. The full-host build failure remained open; the
supported CLI build also failed on undeclared `list_push` calls in Input markup
sources. No DOM, Input or vendor repair was substituted into these measurements.

The original benchmark executable is unchanged. The profiler is a preserved-object
release relink, **not a clean source-build qualification**. Version two SHA256:
`1c7d69da2f1bc27ddb5b32c2b867c69b5fc0d1c4992954c18c7a1495cc0becd1`.
Its scripts, source probe copies, link recipe, logs and snapshots are under
`temp/sync-memory-detail/`. The first profiler wrote full context snapshots; its
roughly 20 MiB JSON report allocated enough temporary memory to distort RSS.
Version two aggregates context owners compactly and samples OS memory before
report generation. Use v1 for logical-owner evidence, not idle RSS comparisons.
MIR accounting adds allocation headers: requested-byte counters are useful, but
its OS memory is not an uninstrumented performance baseline (D5.2.4).

### 1. Formatting retains a second result outside the GC

`fn_format2()` calls `format_data_with_json_options()` with
`context->heap->pool`, then copies the returned String into the GC heap.
The original String and its builder allocation remain in `eval.runtime.pool`.
The JSON formatter's `stringbuf_to_string()` transfers the builder's allocated
capacity, not only its used text length. The runtime pool dies at heap teardown.

The isolated formatter test repeatedly formats a 1 MiB ASCII string as JSON,
hashes each result and discards it. Its final hash is checked independently
against the quoted source string.

| Iterations | Returned JSON bytes, cumulative | Formatter-attributed pool growth | Pool reserved capacity | Growth after explicit GC |
|---|---:|---:|---:|---:|
| 1 | 1.00 MiB | 1.25 MiB | 1.28 MiB | 1.25 MiB |
| 64 | 64.00 MiB | 80.00 MiB | 160.75 MiB | 80.00 MiB |

The 64-iteration run already had eight automatic collections before the explicit
one. More GC cannot release the pool-owned copies. This is a distinct lifetime
bug under D5.1.5, not just missing binary pressure.

In the fresh 80 MiB publication, formatter counters account for **53.91 MiB of
56.11 MiB** of runtime-pool live allocations. After three unchanged sync rounds,
those figures become **68.88 MiB of 71.09 MiB**. The runtime pool has 128 MiB
reserved at publication. The remaining pool allocation is much smaller than
the formatter contribution and needs separate attribution only if it keeps growing.

Artifacts: `micro-format-{1,64}-*/`, `_format_pressure.ls`, `micro-results.json`.

### 2. Mark validation dominates retained parser owners

The native parse counters now distinguish JSON and Mark and measure each parsed
Input arena's committed and active bytes on return. At 8 MiB first capture there
are **142 JSON parses and 7,492 Mark parses**, not thousands of JSON decodes.
`codec.validate_scalars()` performs a Mark round trip for selected values, and
`codec.decoded_scalars()` parses their literal representations. Binary identifiers
are among the repeatedly processed scalar values. General Mark parsing is used
on this path even when only a portability check is needed.

Each unchanged round in this experiment explicitly runs
`local.scan(handle, null, true)` (verification), then cloud pull/publication.
All three report **zero uploaded objects**. These are repeated verification-and-
cloud rounds, not passive idle or a claim that every ordinary polling tick has
this cost. Idle checkpoints leave parse/format ownership counters unchanged.

| 80 MiB / 128-file stage | JSON parses | Mark parses | JSON arena capacity | Mark arena capacity | Formatter pool growth |
|---|---:|---:|---:|---:|---:|
| First capture | 270 | 8,644 | 5.20 MiB | 47.15 MiB | 36.65 MiB |
| Cloud publication | 610 | 12,578 | 9.21 MiB | 68.62 MiB | 53.91 MiB |
| Unchanged round 1 | 618 | 13,406 | 9.29 MiB | 74.79 MiB | 60.46 MiB |
| Unchanged round 2 | 626 | 14,228 | 9.37 MiB | 79.56 MiB | 64.67 MiB |
| Unchanged round 3 | 634 | 15,050 | 9.45 MiB | 84.34 MiB | 68.88 MiB |

After the first round's additional warmup, each unchanged verification-and-cloud round adds about
**4.77 MiB of Mark arenas plus 4.21 MiB of formatter allocations**, before Input
bookkeeping. This is a steady ownership-growth path even when no file changed.
An 8 MiB scan-only unchanged round adds 396 Mark parses, about 2.34 MiB of Mark
arenas, and 2.07 MiB of formatter allocations. These logical counters are distinct
from GC and allocator residency; idle time does not release their owners.

The context census confirms the parse counters: after the three 8 MiB rescans,
8,825 arenas have `parse://inline/` ownership, matching 145 JSON plus 8,680 Mark
calls. Other Inputs and their arena overhead explain the small difference between
summed parse-arena capacity and all Input arena capacity. The Input thread pool
also retains structures, names and bookkeeping outside the arenas (D5.1.3,
D5.2.5). Both resources need bounded ownership; freeing only arena data is not a
complete scoped parse implementation.

### 3. GC capacity greatly exceeds surviving data

The native census walks the actual GC-owned regions, separately from pool
facades. It deduplicates physical `ByteStorage` pointers for binary capacity.
A pre-collection header census includes objects not yet found dead; only the
post-collection census is a survivor count.

| Compact-checkpoint workload | Committed GC regions after GC | Small live object bytes + used data zones | GC-reachable binary backing |
|---|---:|---:|---:|
| 8 MiB capture + three unchanged scans | 116.4 MiB | 2.03 MiB | 0.03 MiB |
| 80 MiB publication + three unchanged rounds | 192.8 MiB | 2.28 MiB | 0.05 MiB |
| 80 MiB independent restore | 76.5 MiB | Small survivor set; see raw census | 0.05 MiB |

For the 80 MiB repeated-sync case the regions are **60 MiB of object bump
blocks, 37.1 MiB of object slabs, 44 MiB of nursery, and 51.6 MiB of tenured
capacity** (rounded; raw `detail-8.json` is authoritative). The exact mix varies
with GC/tier scheduling; an earlier probe had 148.8 MiB total. The repeatable
finding is the large gap between committed capacity and live occupancy.

Current object bump/slab regions are retained until heap destruction. Nursery
reset retains overflow blocks. Tenured compaction may release old data, but its
current policy does not consistently reduce capacity to this survivor set.
`eval.heap` in the existing context report reports `gc->total_allocated` for
both reserved and used bytes. That misses region capacity and can misleadingly
show only kilobytes while over 100 MiB remains committed. Fixing this telemetry
is part of P0, not a new allocation mechanism (D5.2.1, D5.2.4, D5.3.1).

The other notable publication pools are separate owners: about 128 MiB reserved
runtime formatter pool, 32 MiB Input thread pool, 30 MiB parser/AST pools across
65 modules, and 4.4 MiB satellite snapshot pools. Not all reserved pool pages are
resident. Compiler snapshot pools are much smaller than MIR's raw allocations;
the entire VM category must not be called compiler working memory.

### 4. MIR requested bytes can be split by teardown operation

A public `MIR_init2()` allocator adapter counts requested bytes and blocks by
context. It does not patch MIR. At the end of an 8 MiB publication, there are
about **313 MiB of live requested MIR allocation** across **214 initialized
contexts**. A diagnostic-only change performs the existing generated-IR cleanup
at normal context teardown, between generator cleanup and context destruction.
It changes no executing image or publication lifetime.

| Teardown component, summed over these contexts | Requested bytes |
|---|---:|
| Generator scratch released by `MIR_gen_finish()` | 173.35 MiB |
| Generated IR released by `jit_release_generated_ir()` | 68.33 MiB |
| Remaining context data released by `MIR_finish()` | 78.98 MiB |
| Small post-finish accounting residue to reconcile | 0.11 MiB |

The sum before teardown is 320.77 MiB, slightly larger than the earlier live
checkpoint because additional work completes before shutdown and individual
lifetimes differ. These are operation-specific counters, not OS resident bytes.
The small post-finish residue needs allocator/teardown reconciliation before
claiming all MIR allocations balance; it is not the explanation for the GiB peak.
The data supports early eager-image scratch reclamation, while actual calls after
sealing, lazy mode and cancellation still require P3's qualification. About
79 MiB belongs to a different lifetime and must not be freed with the generator.

Artifacts: `mir-account-1794911141106640000/runtime.log`,
`mir-teardown-v2.json`. No early-sealing implementation was shipped or qualified.

### 5. Reclamation and OS residency are separate

The binary reproducer now includes an explicit precise-root collection and an
OS heap inspection. Before GC, the native heap has **271,170,000 bytes** allocated;
after GC it has **4,848,208 bytes**. The GC-reachable binary backing falls from
about 256 MiB to about 2 MiB. Finalization therefore works: the dead storage has
been freed. Nevertheless `vmmap` reports **252 MiB resident in empty malloc
regions**, and the private footprint remains about 277 MiB.

This closes the earlier uncertainty between a backing-storage leak and delayed
collection/allocator residency. Missing binary pressure allows a large allocation
burst; GC later frees it, while the allocator can keep the pages. Pressure-driven
collection early in the burst bounds both peak allocation and the amount of
capacity that can become cached. The observed allocator-relief calls did not
produce an immediate useful drop and are not the proposed fix.

In the 80 MiB single-file streaming case the first capture has about **213 MiB
of GC-reachable backing capacity**, with only 14.5 MiB of GC regions. This is
another direct demonstration that a bounded input block size does not bound the
sum of native buffers waiting for GC.

Artifacts: `micro-binary-128-*/`, `binary-inspect-1794911422684617000/`.

### 6. Restore amplifies the same paths and allocator high water

Two independent 80 MiB restores complete with every file matching the source
SHA256. The sender is offline. The traced restore has roughly **311 MiB requested
MIR data**, **68.6 MiB parsed arena capacity**, **42.1 MiB runtime-pool live data**
(36.7 MiB attributed directly to formatting), and about **77–81 MiB GC capacity**.
At the materialization checkpoint, only about **2.3 MiB** of backing remains
reachable from GC binary descriptors; the larger transient transfer/crypto
allocation burst has already occurred.

The before/after-GC OS inspection further separates live from cached storage.
After GC the heap still has **556.5 MiB allocated**, including MIR counter headers
and the live owners above. `vmmap` also shows **457.4 MiB in empty large-malloc
regions and 161.7 MiB in empty small-malloc regions**, plus **177.7 MiB resident
VM allocations**. These categories explain most of the approximately 1.3 GiB
private footprint at that checkpoint. The malloc-zone fragmentation summary
partly overlaps those empty-region figures and must not be added again.

Some residency falls only later: in the held inspection run, a later checkpoint
drops from about 1,363 to 1,078 MiB without a comparable change in live owner
counters. It is not valid to attribute that delayed OS drop to a particular
application trim call. The useful fix is to prevent accumulation across pack,
format, compiler and parse lifetimes in the first place.

The counter-instrumented restore peaks at about 1.58 GiB sampled RSS; this is
not a new production performance number. It agrees qualitatively with the earlier
unmodified host's approximately 1.5 GiB restore problem. The plan requires a clean,
uninstrumented final release gate.

Artifacts: `restore-results.json`, `restore-inspect-results.json`,
`restore-80-1794911209209752000/`,
`restore-inspect-80-1794911507711114000/`.

### Follow-up conclusion and remaining limits

The main ownership paths are now separated with independent reproducers:
formatter lifetime, binary pressure, MIR generator/IR lifetime, parser ownership,
and GC region retention. OS snapshots explain why a late GC does not undo the
high-water footprint. The proposed plan fixes those causes in separate phases
and sets gates for repeated unchanged sync and independent restore.
Exact native live bytes still differ from OS resident/dirty pages and may include
allocator metadata. Snapshot overhead, instrumentation headers, background
compilation and GC timing prevent a perfectly additive cross-run memory pie.
A clean release rebuild, early-seal correctness and the proposed final budgets
remain implementation/qualification work. No runtime tuning is claimed complete.


## Implementation follow-up — 2026-11-17

This record preserves the pre-change diagnosis and its instrumented build limits.
P0–P5 are implemented in the [memory tuning qualification](Lambda_Impl_Sync_Memory_Tuning.md#implementation-and-qualification--2026-11-17), together with physical-pack download grouping. That record contains clean normal-release results and remaining budget gaps; diagnostic owner totals here are not the final product RSS (D5.2.4, D8.3.4).
