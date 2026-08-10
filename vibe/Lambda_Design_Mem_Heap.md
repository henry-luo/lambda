# Lambda Memory Pool Redesign — rpmalloc Exit Proposal

**Status**: Implemented
**Version**: 1.0.0
**Date**: 2026-08-08
**Scope**: Lambda and Radiant allocator backends, ownership, migration, and verification
**Related**: [Formal Design D4](../doc/Lambda_Formal_Design.md#d4-memory-management), [Memory Context](./Memory_Context.md), [Memory Pooling](./Memory_Pooling.md), [Memory Model](./Lambda_Design_Memory_Model.md), [GC2](./Lambda_Garbage_Collector2.md)

**Revision 1.0**: implemented the approved design by retaining and hardening the existing `memtrack` allocation API as
the sole system-heap substrate; defines opaque VM-region identity and teardown
authority; makes allocation edge contracts explicit; permits multiple GC slabs
per VM extent; and moves Windows VM verification into the primitive-backend
phase.

---

## 1. Decision Summary

Lambda should migrate completely out of rpmalloc. It should **not** replace rpmalloc with another general-purpose allocator and should **not** build a general-purpose malloc implementation.

The target has three allocation paths:

1. **GC heap — GC-owned VM pages and slabs.** The collector obtains virtual-memory regions from a small platform layer and owns all object slabs, data-zone blocks, and large-object mappings itself.
2. **Arena — arena-owned blocks.** An arena obtains and links its own blocks directly, without a backing `Pool`. Ordinary arenas provide region allocation, reset, and destruction rather than pretending to support general individual frees.
3. **Residual individually managed allocations.** A call site uses the existing,
   hardened `memtrack` API when ownership is singular and explicit, as with
   `StrBuf`/`StringBuf`, or uses a `memtrack` owner group when it needs both
   individual release and owner-wide teardown.

`MemContext` remains the mandatory allocator factory, ownership graph, accounting surface, and teardown coordinator under **D4.2.1**. This proposal changes allocator backends and subsystem ownership; it does not weaken Memory Context ownership.

The result is a small set of explicit policies rather than a new universal allocator:

```text
                              MemContext
                                  |
             +--------------------+--------------------+
             |                    |                    |
          GCHeap                Arena             MemtrackOwner
             |                    |                    |
       VM extents/slabs      owned block list      memtrack allocations
             |                    |              + intrusive ownership
             +--------------------+--------------------+
                                  |
                       platform VM / system heap

        Explicit single-owner buffers ----------------> memtrack
```

This is an implementation proposal. Acceptance must include the formal-spec reconciliation in [§16](#16-formal-spec-reconciliation); this document does not silently revise an accepted D4 ruling.

There is one system-allocation stack, not two. `memtrack` remains the public
Lambda allocation API and is improved in place. The migration must not add a
parallel family of “checked system allocation” wrappers around raw
`malloc`/`calloc`/`realloc`/`free`.

---

## 2. Why Leave rpmalloc

The reason is not that every observed memory failure was an rpmalloc defect. The audit found both kinds of problem:

- A first-class rpmalloc huge-span bookkeeping defect was reproduced using rpmalloc alone, outside Lambda, on Linux and macOS. The failing sequence combined a large allocation, individual heap free, and heap-wide teardown. The root cause was recorded during the pre-retirement audit: a released huge span remained linked from `span_used`, so later `rpmalloc_heap_free_all()` could follow a dangling span. The equivalent fix was already present as an uncommitted delta in the older macOS dependency tree, which also explains why platform results diverged. No rpmalloc patch is retained in the implementation tree.
- Independent Lambda ownership and teardown defects also existed, including incorrect GC-backing registration/lifetime and repeated finalization paths. Those defects must remain fixed regardless of allocator choice.

The migration is therefore architectural, not an attempt to hide Lambda misuse behind a different allocator. rpmalloc currently adds limited value to the dominant paths:

- The GC already implements its own size classes, object free lists, bump-allocated data zones, collection policy, and large-object bookkeeping. rpmalloc is mostly a lower-level source of coarse backing memory.
- Arenas already implement region ownership and should not need a general allocator beneath every block.
- The remaining `Pool` users are a mixed set of ownership patterns. A universal backend obscures whether a call site needs region lifetime, individual ownership, callback-compatible allocation, or a true fixed-size pool.

rpmalloc also introduces global/TLS initialization and finalization, vendor-version drift, platform-specific patch state, and sanitizer/debugging complexity. Removing it eliminates that class of integration risk, but it does **not** eliminate the need to audit allocator ownership and API use.

The replacement mempool tests cover large allocations, selective release,
owner-wide teardown, and deterministic fault injection without depending on
the retired backend.

### 2.1 Current source snapshot

A 2026-08-08 source scan under `lib/`, `lambda/`, and `radiant/` found the following call-line counts. Counts include declarations and allocator implementations, so they measure migration surface rather than runtime frequency.

| Pattern | Call lines |
|---|---:|
| `pool_alloc(` | 192 |
| `pool_calloc(` | 522 |
| `pool_free(` | 125 |
| `pool_realloc(` | 6 |
| `pool_create(` | 108 |
| `mem_pool_create(` | 62 |
| `arena_alloc(` | 136 |
| `arena_calloc(` | 39 |

The important asymmetry is that active `arena_free()` use outside `lib/arena.c` is limited to `ScratchArena` and one DOM lifecycle path, while no ordinary external call site currently calls `arena_realloc()`. This is strong evidence that ordinary Arena should be a region allocator and that the exceptional users should receive an API matching their actual lifetime.

The existing `pool_create_mmap()` mode is not a safe blanket replacement. It eagerly maps a large first chunk, treats individual free as a no-op, and retains allocations until pool destruction. Applying it to every residual pool would trade the rpmalloc dependency for excessive reservation/retention and incorrect expectations around selective release.

---

## 3. Formal Alignment

This proposal preserves the following accepted design rulings:

- **D4.1.1** — the semantic tiers remain GC heap, Input arena, AST/const storage, and NamePool. A tier describes lifetime and visibility to the collector; it does not mandate rpmalloc as its backend.
- **D4.1.3** — Input and format allocations remain outside GC rooting. Arena-owned blocks make this boundary more explicit.
- **D4.2.1** — every allocator is created in the `MemContext` graph, with lifecycle and backing relationships recorded at creation and dependency-ordered teardown.
- **D4.2.2** — the page provider and accounting in this proposal are the substrate for the designed Stage 2 memory-pressure coordinator; this proposal does not by itself implement the complete OOM escalation ladder.
- **D4.3.1** — the GC remains non-moving and pointer-stable.
- **D4.3.2** — the object zone remains size-class/slab based and the data zone remains bump/reset based. The collector simply owns the backing directly.
- **D4.5.1** — Lambda and Radiant retain different memory policies. Radiant's region and type-stable ownership must not be replaced with GC tracing.

Two wording updates are likely required if this proposal is accepted: **D4.2.1** currently names `pool_alloc`/`arena_alloc` as unchanged hot paths, and **D4.5.1** describes the shared substrate as “mempool/arena + Memory Context.” The invariants remain valid, but the concrete substrate and API names will change. See [§16](#16-formal-spec-reconciliation).

---

## 4. Goals and Non-Goals

### 4.1 Goals

- Remove rpmalloc source, archives, headers, build configuration, patches, initialization, finalization, and runtime dependency on every platform.
- Give the GC direct and auditable ownership of every VM region it uses.
- Make ordinary Arena a clear region allocator that owns its block chain directly.
- Match every residual allocation site to its real lifetime instead of routing all sites through one generic pool abstraction.
- Preserve `MemContext` ownership, labels, parent relationships, snapshots, leak reports, and dependency-ordered teardown.
- Preserve alignment, zero-initialization, overflow handling, allocation-failure behavior, and pointer stability required by callers.
- Reuse and harden `memtrack` as the sole checked system-allocation API,
  including overflow checks, owner/role attribution, debug diagnostics, and
  deterministic fault injection.
- Keep hot allocation paths free from the global `MemContext` lock. In grouped `memtrack` ownership, this is achieved through explicit thread confinement rather than hidden TLS state.
- Make Linux, macOS, and Windows behavior derive from the same contracts and tests.

### 4.2 Non-goals

- Implement a replacement general-purpose malloc.
- Patch or fork another vendor allocator.
- Introduce a second system-allocation wrapper or a second memory-pressure
  coordinator beside `memtrack` and `MemContext`.
- Change Lambda value semantics, GC reachability, rooting, or container representation.
- Reintroduce conservative stack scanning; precise `RootFrame`/`Rooted` ownership remains mandatory.
- Force all allocations through the VM API. Small, individually owned buffers should use the system heap.
- Preserve misleading APIs such as arbitrary `arena_free()` merely to minimize mechanical edits.
- Optimize every residual allocation before measurement. Correct ownership and complete removal of rpmalloc come first.

---

## 5. Target Allocation Policies

Every allocation site must be classified by lifetime before migration:

| Lifetime and behavior | Target policy | Examples |
|---|---|---|
| Traced runtime object/data | `GCHeap` VM regions and slabs | Lambda runtime containers, dynamic scalar homes |
| Bulk lifetime; reset/destroy together | `Arena` owning its own blocks | Input/Mark data, AST/const data, document regions |
| Nested LIFO temporary lifetime | `ScratchArena` mark/rewind over an Arena | parser, layout, and rendering scratch |
| One explicit owner; independent resize/free | hardened `memtrack` allocation | `StrBuf`/`StringBuf` backing buffer, isolated byte buffers |
| C library allocator callback | dedicated `memtrack` adapter preserving the callback ABI | FreeType-style alloc/realloc/free callbacks |
| Individual free plus owner-wide teardown | `memtrack` owner group | selectively released document/view records where a region is insufficient |
| Uniform fixed-size objects with measured churn | specialized type-stable slab/free-list pool | eligible Radiant types under **D4.5.1** |
| Interned/append-only semantic storage | owning Arena or specialized append-only store | NamePool/ShapePool entries after call-site audit |

The last two policies are specialized owners, not general allocator replacements. A type-stable pool may be implemented only for a measured fixed-size workload; it must not grow into a variable-size malloc implementation.

### 5.1 Classification rule

Use the least powerful policy that satisfies the lifetime:

1. If all allocations die together, use Arena.
2. If one object owns one resizable buffer, use `memtrack` directly.
3. If multiple variable-size allocations require both individual free and owner-wide teardown, use a `memtrack` owner group.
4. If allocation is GC-traced, it belongs to `GCHeap`, never to a residual pool.
5. If a uniform type demonstrates hot reuse, consider a type-stable pool after profiling.

No call site should choose grouped ownership merely because it currently accepts a `Pool*` parameter.

Raw system allocation remains private to `memtrack`, the VM portability
implementation, and third-party ABI boundaries that cannot carry Lambda
metadata. Application code does not gain a new raw-allocation escape hatch.

---

## 6. Platform VM/Page Layer

The VM layer supplies raw virtual-memory regions. It deliberately has no size classes, free lists, object headers, arenas, or bulk-owner semantics.

Conceptual operations are:

```c
typedef struct MemVmRegion MemVmRegion;

size_t mem_vm_page_size(void);
MemVmRegion* mem_vm_region_reserve(MemContext* context, MemNode* owner,
                                   MemRole role, size_t size, size_t alignment);
bool mem_vm_region_commit(MemVmRegion* region, size_t offset, size_t size);
bool mem_vm_region_decommit(MemVmRegion* region, size_t offset, size_t size);
void mem_vm_region_release(MemVmRegion* region);
void* mem_vm_region_base(const MemVmRegion* region);
```

`MemVmRegion` is an opaque identity record allocated by the VM layer through
the hardened `memtrack` substrate before the OS mapping is created. Metadata
allocation failure is reported without creating a mapping. The record retains
the actual mapping base, usable aligned base, reserved and committed byte
counts, requested alignment, owner, role, and lifecycle state. A GC heap or
Arena links the record into its own authoritative region/block list and is
responsible for calling `mem_vm_region_release()` exactly once; VM metadata is
never allocated from the region it describes.

The final API may combine reserve and commit where the platform distinction provides no benefit, but it must preserve these guarantees:

- Sizes and addresses are page-aligned with checked rounding and overflow detection.
- A successful region records its exact mapping base, usable base, reserved bytes, committed bytes, owner, role, and state.
- Release receives the `MemVmRegion` identity; callers do not reconstruct mapping bases or sizes from object data.
- Over-alignment may require a mapping base different from the usable base; both
  remain recorded until release.
- Failure is reported to the caller and integrated with `MemContext` pressure handling. The primitive does not silently abort or recursively invoke arbitrary reclaimers.
- Debug builds consult a live-region registry before the opaque record is read,
  detecting double release, partial-release mismatch, and owner mismatch without
  dereferencing released metadata.
- Linux/macOS use `mmap`/`munmap` and the appropriate commit/decommit advice or protection. Windows uses `VirtualAlloc`/`VirtualFree` with equivalent contracts.
- Executable JIT mappings remain a distinct role with explicit W^X handling. This proposal does not route executable pages through a data-page shortcut.

The subsystem that owns the `MemVmRegion` record is the sole release authority.
`MemContext` reports aggregate region bytes through that subsystem's allocator
node. Optional `MEM_KIND_VM_REGION` records are diagnostic, non-owning child
records with no destroy callback; they must not cause bytes to be counted a
second time and must never unmap a region during cascade teardown. This preserves
the reverse-dependency lifecycle of **D4.2.1** without creating two teardown
paths.

The VM layer is the future Stage 2 page-allocation choke point described by **D4.2.2**, but its first implementation should remain a thin portability and accounting layer. Cache reclamation, forced GC, parking, and fatal policy stay above it in `MemContext`.

---

## 7. GC-Owned VM Pages and Slabs

Under **D4.3.1** and **D4.3.2**, the GC already owns allocation policy. The redesign removes `Pool` as its backing allocator and makes that ownership physical as well as logical.

### 7.1 Object zone

- Each size class owns a list of GC slabs.
- Slabs are backed by GC-owned VM extents. An extent may contain one or more
  slabs; each slab records class size, slot count, allocation bitmap/free list,
  mark state, and its owning extent. Mapping granularity is a measured GC policy,
  not a one-mapping-per-slab invariant.
- Allocation pops a free slot or bumps within the current slab.
- Sweep returns dead slots to that slab's free list.
- Entirely empty slabs may be retained up to a measured warm limit. An extent
  may be decommitted or released only when no live slab in that extent needs its
  pages. The policy must be deterministic and visible in stats.
- Object addresses never move. Returning or releasing an entirely empty slab therefore preserves **D4.3.1**.

### 7.2 Data zone

- Nursery and tenured data zones own page-aligned VM segments.
- Allocation remains bump based.
- Collection resets or replaces data segments according to the existing dual-zone algorithm.
- The existing one-fixup-per-surviving-object invariant remains unchanged.

### 7.3 Large objects

- Objects above the slab threshold receive dedicated VM regions; unlike ordinary
  slabs, large objects are intentionally one object per region.
- A GC-owned region record stores base, mapped size, object size, kind, mark state, and list linkage.
- Individual large-object reclamation releases the exact mapping once the object is unreachable.
- Heap teardown walks the same region records and releases each live mapping exactly once.

This explicit record is the invariant that prevents “individually freed, then bulk-freed again” ambiguity. The GC is the sole owner of the region list.

### 7.4 Metadata and lifecycle

- The small `GCHeap` control structure may use an explicit `memtrack` allocation registered as part of the GC node; it must not depend on the heap it is creating.
- `mem_gc_heap_create()` registers the heap and its VM accounting in `MemContext`.
- `gc_heap_destroy()` releases all GC-owned regions, verifies that the region tables are empty, and unregisters once.
- Transitional `gc_heap_create_with_pool()` is removed after all callers migrate.
- GC stats report object live bytes, slab committed/reserved bytes, data-zone bytes, large-object bytes, and retained empty capacity separately.

The collector must never call a residual `memtrack` owner group for object, data-zone, slab, or large-object storage.

---

## 8. Arena-Owned Blocks

An ordinary Arena becomes a direct owner of a linked block list:

```text
Arena
  current --------------+
  blocks -> Block -> Block -> Block
             |       |       |
           used/capacity + aligned payload
```

The initial implementation should obtain ordinary blocks through `memtrack`.
Very large blocks may use the VM layer only after a measured threshold justifies
it; the choice is internal to Arena and does not change caller semantics.

### 8.1 Ordinary Arena contract

- `arena_alloc()` and `arena_calloc()` allocate aligned storage from the current block and grow by linking a new block.
- `arena_reset()` invalidates all allocations, resets the retained warm block(s), and releases excess blocks according to a bounded retention policy.
- `arena_destroy()` releases every block exactly once.
- `arena_owns()` remains available for storage-class checks. Its implementation may use a range list or range index; it must not infer ownership from a removed backing Pool.
- Block growth is geometric within explicit minimum and maximum bounds. Oversized requests receive dedicated blocks rather than permanently inflating normal block size.
- Stats separate used payload, committed block bytes, retained bytes, reset count, and peak bytes.

### 8.2 Remove arbitrary free from ordinary Arena

Ordinary region allocation should not expose arbitrary `arena_free()` or `arena_realloc()` semantics. The current exceptional uses should migrate deliberately:

- `ScratchArena` should use mark/rewind or a scratch-specific block recycler. LIFO scratch release does not require a general coalescing allocator.
- The DOM lifecycle path that releases one arena allocation should either defer that storage to document-region teardown or move the record to a type-stable/`memtrack` grouped owner if early reclamation is materially required.
- Any newly discovered selective-free user is reclassified; it is not accommodated by restoring general free-list complexity to Arena.

This removes coalescing and arbitrary-realloc behavior from the hottest region abstraction and makes misuse visible at compile time.

### 8.3 Arena-backed semantic tiers

The tiers in **D4.1.1** remain distinct even when they share an Arena implementation:

- Input/Mark arenas remain collector-invisible under **D4.1.3**.
- AST/const storage should use a compilation-lifetime Arena when its objects die together.
- NamePool and ShapePool remain semantic owners and `MemContext` nodes. Append-only entries should use owned Arena blocks; any selectively released auxiliary index must be classified independently.
- Radiant document/view regions continue to act as cycle collectors under **D4.5.1**.

Sharing a block implementation does not merge ownership tiers or permit pointers to outlive their tier.

---

## 9. Residual `memtrack` Allocation

Direct `memtrack` allocation is preferred when one object has unambiguous ownership of one independently resized or released buffer.

`StrBuf`/`StringBuf` is the model case:

- the buffer object owns its character allocation,
- growth is explicit,
- destruction has one release point,
- no pool-wide operation is needed to discover or reclaim the buffer.

These sites continue to use `mem_alloc`/`mem_calloc`/`mem_realloc`/`mem_free`.
Phase 1 improves those functions in place; it does not introduce another
allocation family. The hardened API provides checked addition and
multiplication, the edge semantics in [§13.1](#131-allocation-edge-contract),
failure reporting, optional debug poisoning, role/category accounting, and
deterministic fault injection. It does not add bulk ownership tracking.

The existing `MemCategory` taxonomy is retained as the per-allocation diagnostic
tag. Each allocator or subsystem owner also has a `MemRole`; Phase 0 defines one
canonical `MemRole` → default `MemCategory` mapping and documents the few sites
that require a more specific category. Owner-local counters roll up direct
allocations into the owning `MemContext` node without creating one heavyweight
node per allocation.

`memtrack` currently contains its own threshold-triggered pressure callbacks.
Under **D4.2.2**, `MemContext` becomes the sole escalation coordinator. The
existing callback registration surface may remain temporarily as a compatibility
adapter, but it must register reclaimers with `MemContext`; it must not run a
second independent pressure ladder from inside `mem_alloc`. Allocation failure
flows upward once, excludes the failing allocator from re-entry, and retries
according to [§11.3](#113-allocation-failure-and-stage-2).

Hardening `memtrack` includes:

- checked `count * size`, `header + payload`, guard-byte, and reallocation-size
  arithmetic before calling the system allocator;
- `size_t`-width allocation metadata with no truncation of large sizes;
- transactional reallocation in OFF, STATS, and DEBUG modes;
- deterministic zero-size behavior shared by all modes;
- owner/role attribution that does not require a second allocation header in
  ordinary direct-allocation users;
- deterministic, thread-scoped fault injection at the system-allocation call;
- a debug live-allocation registry used before inspecting inline metadata, so
  invalid and repeated frees do not read already-freed storage.

Library callback APIs should receive a dedicated adapter over `memtrack` with
matching C semantics. For example, an allocator callback that may call `realloc`
on an individually owned block should not be backed by an Arena or by a pool
whose `free` may be a no-op. Where a callback ABI cannot carry owner metadata,
the adapter owns an explicit context object or uses a documented fixed role; it
does not bypass `memtrack` with scattered raw calls.

Direct ungrouped `memtrack` allocation is not appropriate when teardown must recover a set of allocations that callers may not individually release. That ownership pattern belongs to a `memtrack` owner group.

---

## 10. `memtrack` Owner Groups

A `memtrack` owner group is a lifecycle feature of `memtrack`, not a separate
allocator or wrapper layer. Phase 1 evolves the existing
`memtrack_pool_create`/`memtrack_pool_alloc`/`memtrack_pool_destroy` integration
in `lib/memtrack.*`; it does not add `TrackedPool`, `tracked_pool_*`, or a new
allocator module. `memtrack` and the system allocator continue to handle
variable-size allocation, instrumentation, and fragmentation while the owner
group records which allocations share teardown.

The current `memtrack_pool_*` functions are shallow wrappers over `Pool` plus
lifecycle counters. Their implementation is replaced in place: the owner group
becomes independent of rpmalloc, and the existing family gains the
calloc/free/realloc/reset operations required by audited callers. Transitional
`Pool*` compatibility remains behind the factory switch only until call sites
use the `memtrack` ownership surface directly.

### 10.1 Required behavior

- O(1) allocation insertion.
- O(1) individual free and unlink.
- O(n) reset/destroy over allocations still owned by the group.
- Correct `calloc` overflow checking and zero initialization.
- Transactional reallocation: on failure, the old allocation and ownership list remain unchanged.
- Exact requested live bytes, allocation count, peak requested bytes, and
  operation counts; aggregate system-heap overhead remains a separate
  `memtrack` diagnostic.
- Debug detection of wrong-owner free, double free, corrupted header, and use after reset where practical. The debug path first consults `memtrack`'s live-allocation registry; it never relies on reading an inline header after the system allocation was released.
- No additional global/TLS allocator initialization or finalization beyond the
  existing `memtrack` process lifecycle; individual owner groups have none.
- Default thread confinement, stated at owner-group creation and checked in debug builds.

### 10.2 Allocation layout

Each grouped allocation extends `memtrack`'s aligned private metadata before the caller payload:

```text
+------------------------------------------------+-------------------+
| owner | requested_size | prev | next | debug   | aligned payload   |
+------------------------------------------------+-------------------+
```

The metadata must preserve `max_align_t` alignment, use checked `header + payload` arithmetic, and remain private to `memtrack`. Debug fields may include a magic value, generation, allocation sequence, or released-state poison. Owner, requested size, and intrusive links are part of the one `memtrack` metadata record; grouped allocation must not prepend a second tracker header.

In OFF mode, ungrouped direct `memtrack` allocation retains its raw system-heap
fast path. Grouped allocation retains only the minimal owner, requested-size,
and intrusive-link fields because they are required for semantics; STATS and
DEBUG instrumentation is layered into the same record. The unavoidable grouped
metadata cost is measured separately in Phase 0/Phase 5.

The owner list is intrusive so reset and destroy require no secondary allocation. An owner group does not store pointers in a separate growable array that could fail while recording a successful allocation. The entire metadata-plus-payload block is allocated and released by `memtrack`.

### 10.3 Reallocation

Because source use of `pool_realloc()` is rare, the robust initial implementation should use allocate-copy-free rather than trying to repair intrusive neighbors after a moving system `realloc`:

1. Allocate and link the replacement.
2. Copy `min(old_size, new_size)` bytes.
3. Unlink and release the old block.
4. If step 1 fails, leave the old block untouched.

An in-place optimization may be added only after profiling and dedicated list-integrity tests.

### 10.4 Threading

The default contract is one owner thread per `memtrack` owner group. Intrusive list operations are then lock-free and satisfy the hot-path intent of **D4.2.1** without rpmalloc TLS heaps.

The migration audit must identify genuinely shared pools. Preferred remedies are:

- give each worker its own owner/pool and merge results by copying/moving values
  into the destination owner's lifetime,
- protect the higher-level shared object with its existing subsystem lock,
- use direct `memtrack` allocations whose container already provides synchronization.

A hidden mutex in every owner group is not the default. If an unavoidable shared group remains, its synchronization mode must be explicit in its constructor, accounting, and tests.

The v1 thread-confined API has no allocation- or whole-group owner-transfer
operation. Create, allocate, free, reset, and destroy occur on the declared owner
thread. If Phase 0 finds a required cross-thread pool, it must use an explicitly
synchronized mode or be reclassified before the compatibility backend is
enabled; an implicit handoff is not accepted.

### 10.5 What owner groups must not become

The `memtrack` owner-group feature must not acquire size classes, per-thread caches, remote-free queues, span maps, huge-page policy, or custom coalescing. Those are signs that Lambda is rebuilding the general allocator it intended to remove.

---

## 11. Memory Context Integration

`MemContext` remains the source of allocator identity and lifecycle under **D4.2.1**.

### 11.1 Node model

The context should expose nodes for at least:

- `MEM_KIND_GC_HEAP`
- `MEM_KIND_ARENA`
- `MEM_KIND_SCRATCH`
- `MEM_KIND_MEMTRACK_OWNER`
- `MEM_KIND_TYPE_POOL`
- `MEM_KIND_VM_REGION` where region-level diagnostics are enabled
- existing JIT/cache/external owners

GC and Arena no longer have a parent edge to a generic backing Pool. Their allocator node is parented by its owning context. GC extents and VM-backed Arena blocks remain in the subsystem's authoritative private list. Optional VM-region nodes are non-owning diagnostic children as specified in [§6](#6-platform-vmpage-layer); parent aggregate and child detail views must use snapshot flags/roll-up rules that count each byte exactly once.

### 11.2 Lifecycle and teardown

- Factory creation records role, label, parent context, thread mode, and backend before returning the allocator.
- Subsystem destruction releases implementation resources, proves the child list empty, then unregisters once.
- Context cascade teardown still runs in reverse dependency order.
- Direct single-owner `memtrack` allocations are attributed to their owner node,
  `MemRole`, and `MemCategory`; they are not each promoted to heavyweight
  context nodes.
- The existing teardown reentrancy guard remains necessary while release hooks unregister allocator subtrees.

### 11.3 Allocation failure and Stage 2

The VM backend and `memtrack` report failure upward through one non-recursive
entry point. `MemContext` may then run the **D4.2.2** escalation policy: drop
caches, trim arenas, collect GC, retry, and finally follow the configured
fail/park/abort disposition. Legacy `memtrack` pressure callbacks become
`MemContext` reclaimer registrations or compatibility adapters; they do not
invoke reclamation independently from the allocation hot path.

Reclaimers must not allocate through the failing path, re-enter the same allocator, or acquire locks in an order that can deadlock. The VM-region API and `memtrack` need deterministic fault injection so these paths can be tested rather than inferred. `MemContext` destroy callbacks continue to run under its non-recursive registry lock and therefore must not register, unregister, or query context state; allocator release hooks use the existing teardown guard.

---

## 12. Migration Plan

Migration should be incremental and dual-testable. At no phase may a passing test be obtained by weakening ownership checks or by leaving a platform-specific hidden backend.

### Phase 0 — Audit contracts and freeze a baseline

- Accept the proposal through the formal reconciliation in [§16](#16-formal-spec-reconciliation) before backend implementation begins.
- Inventory every factory and raw pool creation, label it by role, owner, lifetime, thread mode, individual-free use, and bulk-destroy behavior.
- Commit the inventory as a reviewable census with source location, owner,
  `MemRole`, `MemCategory`, current API behavior, target policy, and migration
  phase. A generated source check reports uncatalogued application allocator
  creation.
- Reconcile `memtrack`, `MemContext`, and the checked-allocation helpers into the
  single layering specified by [§9](#9-residual-memtrack-allocation); identify
  compatibility APIs that will be retired rather than copied.
- Ratify the allocation edge contract in [§13.1](#131-allocation-edge-contract).
- Record Linux and macOS debug/sanitizer baselines and release CPU/RSS/peak-memory baselines.
- Add the standalone rpmalloc revision/flags reproducer and focused regression
  tests for the confirmed large-allocation/individual-free/bulk-teardown
  sequence and for the separate Lambda teardown bugs.
- Mark every current `pool_realloc`, cross-thread free, allocator callback, and no-op-free dependency for explicit review.

**Gate**: formal reconciliation is accepted; every pool creation has a proposed
destination from [§5](#5-target-allocation-policies); every direct allocation
path has an owner/role/category; the edge contract is ratified; and no
application allocator creation remains uncatalogued. Any unclassified site
blocks Phase 1.

### Phase 1 — Add primitive backends behind existing factories

- Harden the existing `memtrack` API according to [§9](#9-residual-memtrack-allocation). Do not add a parallel checked-allocation API.
- Route the existing checked-allocation helpers through the hardened substrate
  or retire redundant helpers after their callers migrate.
- Implement the VM portability layer over the hardened metadata substrate and
  add its region/accounting tests.
- Replace the backend of the existing `memtrack_pool_*` integration with intrusive
  `memtrack` owner groups behind a temporary switch in the existing `Pool` API
  so identical call sites can be A/B tested. Do not add a parallel tracked-pool
  type or function family.
- Keep the switch in `build_lambda_config.json`-generated configuration; do not hand-edit generated Lua build files.

**Gate**: allocator contract tests pass with both backends and in `memtrack` OFF,
STATS, and DEBUG modes, including fault injection and sanitizer runs. VM
reserve/commit/decommit/release tests pass on Linux, macOS, and Windows; where a
Windows CI runner is unavailable, compile plus a documented focused runtime
smoke run is required before Phase 2. This phase is compatibility scaffolding,
not the final architecture.

### Phase 2 — Detach GC from Pool

- Replace GC slab-extent, data-zone, and large-object backing with GC-owned VM region records.
- Preserve non-moving addresses and the object/data-zone policies required by **D4.3.1–D4.3.2**.
- Route GC accounting through its own `MemContext` node.
- Remove `gc_heap_create_with_pool()` after all callers migrate.

**Gate**: forced-collection/rooting stress, large-object churn, repeated heap
create/destroy, and full runtime baselines pass on Linux and macOS with no Pool
allocation inside GC. The focused GC lifecycle suite also passes on Windows
before Arena migration begins.

### Phase 3 — Detach Arena from Pool

- Give Arena a directly owned block list.
- Preserve allocation/alignment/reset/destroy/owns behavior.
- Convert ScratchArena to mark/rewind or its own recycler.
- Reclassify the DOM selective-free path.
- Remove arbitrary free/realloc from ordinary Arena after the last caller migrates.

**Gate**: parser, Input, AST, layout, render, and scratch stress tests pass; repeated reset has bounded retained memory; no Arena has a backing `Pool*`.

### Phase 4 — Migrate residual Pool users

For each owner:

- Convert bulk-lifetime allocations to Arena.
- Convert singular resizable buffers such as `StrBuf` to direct `memtrack` ownership.
- Give external callbacks a contract-compatible `memtrack` adapter.
- Use a `memtrack` owner group only where individual release and bulk teardown are both required.
- Use a specialized type-stable pool only for audited fixed-size hot types under **D4.5.1**.
- Re-home append-only NamePool/ShapePool storage without changing semantic identity.

Retain the existing `memtrack_pool_*` family as the grouped-ownership surface,
expanded in place as described in §10. Temporary generic `Pool*` compatibility
aliases must have a removal issue and deadline; the final API must make clear
that it is a `memtrack` owner group, not a universal allocator.

**Gate**: source audit finds no generic Pool used as backing for GC or Arena and no call site classified only as “legacy.”

### Phase 5 — Flip the default and soak

- Make the non-rpmalloc path the only default in Linux, macOS, and Windows CI.
- Run the old backend only as a short-lived comparison lane while resolving regressions.
- Compare release-build CPU time, allocation rate, peak RSS, retained bytes after reset, and GC pause/throughput.
- Run cross-platform soak and release-performance comparisons before final deletion.

**Gate**: correctness gates in [§14](#14-verification-and-acceptance-gates) pass and measured regressions are understood. Do not use a debug build for performance decisions.

### Phase 6 — Delete rpmalloc

- Remove vendor sources/binaries/headers, build entries, setup/download scripts, include paths, initialization/finalization, compatibility flags, and patches.
- Remove obsolete mmap-pool code and any temporary dual-backend branch.
- Update allocator docs and architecture diagrams.
- Add a source/build gate that rejects new rpmalloc references and unauthorized raw allocator creation.

**Gate**: a clean checkout builds and runs all required suites on each supported platform without obtaining or linking rpmalloc.

---

## 13. Compatibility and API Strategy

A big-bang call-site rename would mix architecture changes with hundreds of mechanical edits. The transition should preserve reviewability:

1. Introduce new implementations behind factories and narrowly scoped compatibility APIs.
2. Detach GC and Arena first, so their ownership can be reviewed independently.
3. Migrate residual owners one subsystem at a time.
4. Move residual grouped ownership onto the existing `memtrack_pool_*` surface only after its user list is small and intentional.
5. Delete aliases and backend switches before declaring migration complete.

Compatibility must not preserve ambiguous behavior:

- A `free` operation may not silently become a no-op unless the API is explicitly region-based and the caller cannot observe early reclamation.
- `realloc` failure must preserve the old allocation.
- A pool pointer may not be accepted merely to select where bytes come from; APIs should accept an owner whose lifetime they actually require.
- A direct-allocation object must release itself even if an adjacent Arena or context is destroyed.

### 13.1 Allocation edge contract

This contract is ratified in Phase 0 and implemented identically by both
temporary Pool backends. It protects the hot-path and ownership intent of
**D4.2.1** while removing platform-dependent allocator behavior.

| Operation | Required behavior |
|---|---|
| `alloc(0)` | Return `NULL`; create no allocation and do not change counters. |
| `calloc(0, n)` / `calloc(n, 0)` | Return `NULL`; perform checked multiplication before allocation. |
| `free(NULL)` | No-op. |
| `realloc(NULL, n)` | Equivalent to `alloc(n)`. |
| `realloc(ptr, 0)` | Release `ptr` and return `NULL`. |
| failed `realloc(ptr, n)` | Return `NULL`; preserve `ptr`, its bytes, ownership linkage, and accounting. |
| allocation failure | Return `NULL` after the configured **D4.2.2** retry path; primitive code does not silently abort. |
| reset/destroy | Invalidate all allocations owned by that allocator exactly once; repeated destroy is not a supported operation. |

Alignment contracts are explicit:

- direct and owner-grouped `memtrack` payloads are aligned through `max_align_t`.
- ordinary `arena_alloc()` preserves its existing 16-byte default alignment;
  `arena_alloc_aligned()` accepts power-of-two alignments through 256 bytes in
  v1 and rejects zero, non-power-of-two, and larger requests until a separately
  tested over-aligned API is accepted;
- VM usable bases are at least page-aligned and honor any larger supported
  power-of-two alignment recorded in `MemVmRegion`;
- callback adapters document any stricter alignment required by their C ABI.

The transitional `Pool` compatibility API retains its current 1 GiB
single-allocation limit under both backends. `memtrack` itself has no arbitrary
global size cap beyond checked `size_t` arithmetic, configured `MemContext`
budgets, and subsystem-specific limits. Removing or changing the compatibility
cap requires a separately reviewed call-site audit.

In DEBUG mode, wrong-owner free, repeated free, stale generation, corrupted
header, and thread-confinement violations are deterministic diagnostics. The
debug live-allocation registry is consulted before private inline metadata is
read. In release/STATS modes these conditions remain programmer-contract
violations; code may log and refuse an operation only when it can do so without
reading invalid storage.

Fault injection is available at two independent boundaries: `memtrack` system
allocation and VM reserve/commit. It is thread-scoped and can filter by owner,
role, operation, and fail-after count, so tests do not perturb unrelated
allocator bookkeeping or the reclaimer itself.

---

## 14. Verification and Acceptance Gates

### 14.1 Allocator contract tests

- Run the common contract suite against `memtrack` OFF/STATS/DEBUG and the
  final memtrack owner-group Pool surface.
- Alignment through `max_align_t` and any explicitly supported over-alignment.
- Checked addition/multiplication and near-`SIZE_MAX` rejection.
- Defined zero-size allocation and reallocation behavior.
- `calloc` zeroing.
- Realloc grow/shrink/preserve/failure behavior.
- Reset and repeated reset.
- Empty and non-empty destroy.
- Wrong-owner free and double-free diagnostics in debug/sanitizer builds.
- Thread-confinement violation diagnostics.
- Owner/role/category accounting without double counting.
- Deterministic, thread-scoped allocation and VM failure injection, including
  reclaimer recursion exclusion.

### 14.2 VM and GC tests

- Reserve/commit/decommit/release with page-boundary cases.
- Over-aligned regions whose mapping base differs from the usable base.
- Exact large-region accounting and repeated create/destroy.
- Object-slab fill/sweep/reuse, multiple slabs per extent, mixed-live extent
  retention, and completely empty extent trimming.
- Data-zone reset/compaction with forced GC at every allocation point.
- Large-object allocation, individual collection, and heap teardown without double release.
- Pointer stability and precise rooting stress under **D4.3.1**.
- Empty `MemContext` graph and zero live VM regions at shutdown.
- Non-owning VM diagnostic nodes neither double-count nor release their parent's regions.

### 14.3 Arena and residual-owner tests

- Geometric growth, oversized dedicated blocks, reset retention cap, and `arena_owns()` boundaries.
- Scratch nested mark/rewind and out-of-order misuse diagnostics.
- `memtrack` owner-group alloc/free/reset/destroy list integrity under randomized operation sequences.
- `StrBuf` and callback-adapter growth/failure tests through `memtrack`, with no
  parallel raw-allocation wrapper.
- Per-document Radiant teardown and repeated navigation without retained growth.

### 14.4 Project suites and tooling

- `make test`, Lambda baseline, Radiant baseline, and Test262 baseline on Linux.
- The corresponding macOS suites, including the previously platform-divergent cases.
- Windows VM, allocator-contract, and focused GC/Arena lifecycle suites from
  Phase 1 onward; full supported Windows suites before the default flips.
- ASan and UBSan on Linux and macOS; TSan for shared-context and allocator-lifecycle tests where supported.
- Release-build benchmarks for representative Lambda evaluation, parsing, layout, rendering, and Test262 workloads.
- Long-running churn tests tracking RSS, committed VM bytes, retained Arena capacity, and GC slab occupancy.
- Clean-build and dependency audit proving that no rpmalloc object or symbol is linked.

The 2026-08-08 implementation acceptance run passed the focused allocator/GC
and ownership suites, Lambda input/runtime baseline (3,661/3,661), JS suite
(327/327), page-load suite (105/105), Test262 baseline (40,261/40,261),
extended and DOM-node suites, release build, lint, and source audits. The
validator DSO runner remains blocked by its pre-existing `_ItemNull` link
defect; broad UI/WPT fixture failures remain unrelated to allocator ownership
and are not used to mask this migration.

Performance acceptance should use workload-specific thresholds established in Phase 0. A regression is not automatically rejected, but it must be attributed to a policy and either corrected or explicitly accepted with data.

---

## 15. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| `memtrack`/system-heap overhead at formerly pooled hot sites | Move true bulk lifetimes to Arena; keep release instrumentation measurable; profile release builds; add a type-stable pool only for measured fixed-size churn. |
| A second checked-allocation or pressure layer diverges from `memtrack` | Improve `memtrack` in place; route existing helpers through it; make `MemContext` the sole **D4.2.2** coordinator; reject parallel wrapper APIs in source checks. |
| Arena retains a peak block chain forever | Bound warm retention; release oversized/excess blocks on reset and memory-pressure trim. |
| One OS mapping per small GC slab creates syscall/VMA overhead | Allocate one or more slabs from measured GC-owned VM extents; keep large objects as dedicated mappings. |
| GC region bookkeeping causes leaks or double unmap | One authoritative region record per mapping, exact-size release, debug registry, repeated teardown and failure-injection tests. |
| Grouped-owner metadata breaks alignment or overflows size arithmetic | One `memtrack` metadata record, `max_align_t` payload alignment, checked arithmetic, near-`SIZE_MAX` tests, private conversion helpers. |
| Moving realloc corrupts intrusive links | Start with allocate-copy-free transaction; optimize only with dedicated integrity tests. |
| Existing code relies on no-op free | Classify every free site and make region semantics explicit; no universal mmap-pool substitution. |
| Hidden cross-thread Pool use violates owner-group confinement | Audit creator and free thread; split ownership or add explicit synchronization at the owning subsystem. |
| Direct allocations bypass or double-count `MemContext` reports | Attribute `memtrack` bytes to owner/role/category counters once; context nodes remain at owner granularity and optional VM detail nodes are non-owning. |
| Stage 2 OOM callbacks re-enter allocators | Non-allocating reclaimers, explicit lock order, recursion guard, deterministic fault injection. |
| Migration temporarily carries two implementations | Time-box the compatibility backend and require Phase 6 deletion before completion. |
| `memtrack` owner groups grow into another custom allocator | Enforce the non-goals in §10.5 and require a new design ruling for allocator-policy expansion. |

---

## 16. Formal-Spec Reconciliation

Before implementation is treated as the accepted architecture:

1. Keep **D4.1.1**, **D4.1.3**, **D4.3.1**, and **D4.3.2** semantically unchanged.
2. Revise **D4.2.1** in place with a `v2` suffix so it states that hot
   allocator operations remain free of the `MemContext` registry lock, the
   existing `memtrack` API is the sole system-allocation substrate, and the
   concrete Pool/Arena APIs may evolve.
3. Revise **D4.5.1** in place with a `v2` suffix so “one substrate” means the
   VM primitives, hardened `memtrack`, and Memory Context, with GC, Arena,
   grouped ownership, and type-stable policies layered above them.
4. Bump the formal design document semver as required by repository policy.
5. Update [Memory Context](./Memory_Context.md) Stage 2 to remove rpmalloc as a
   page-source option, retain/map `MemCategory` rather than creating a parallel
   allocation API, and make `MemContext` the sole pressure/OOM coordinator
   described by §9 and §11.
6. Mark [Memory Pooling](./Memory_Pooling.md) as historical/superseded where its Pool→rpmalloc and Arena→Pool diagrams no longer describe the implementation.
7. Update the memory-tracking design documentation so `memtrack`'s retained
   role, owner/category accounting, fault injection, and compatibility pressure
   adapter match [§9](#9-residual-memtrack-allocation).

No formal language-semantics ruling is changed by this proposal.

---

## 17. Accepted Decision Ledger

These IDs capture backend choices not fully specified by D4. The formal
reconciliation is complete; the implementation and verification record above
is the acceptance status for this ledger.

| ID | Decision | Formal basis |
|---|---|---|
| **MP-1** | Retire rpmalloc without adopting another general-purpose allocator. | D4.1.1, D4.2.1 |
| **MP-2** | The platform VM layer provides opaque region identities and accounting only; the owning subsystem is the sole release authority. | D4.2.1, D4.2.2 |
| **MP-3** | GC owns its VM extents, object slabs, data blocks, and large-object records directly; an extent may hold multiple ordinary slabs. | D4.3.1–D4.3.2 |
| **MP-4** | Ordinary Arena owns blocks directly and exposes region lifetime, not arbitrary individual free. | D4.1.1, D4.1.3, D4.5.1 |
| **MP-5** | Singular resizable buffers and allocator callbacks use the existing, hardened `memtrack` API; no parallel system-allocation wrapper is introduced. | D4.2.1; backend choice otherwise uncovered |
| **MP-6** | Residual variable-size bulk ownership extends the existing `memtrack_pool_*` integration with intrusive owner groups; no separate tracked-pool allocator or API is introduced. | D4.2.1; backend choice otherwise uncovered |
| **MP-7** | The default `memtrack` owner group is thread-confined; shared ownership must be explicit. | D4.2.1 |
| **MP-8** | Specialized type-stable pools are allowed only for audited fixed-size workloads, not as a universal allocator. | D4.5.1 |
| **MP-9** | Correctness migration precedes optimization; new allocator machinery requires release-build evidence. | D4.2.1; backend choice otherwise uncovered |
| **MP-10** | `MemContext` is the sole memory-pressure/OOM escalation coordinator; legacy `memtrack` callbacks are compatibility adapters only. | D4.2.2 |
| **MP-11** | All allocator backends obey the common zero-size, alignment, reallocation, failure, and diagnostic contract in §13.1. | D4.2.1–D4.2.2 |

---

## 18. Open Questions

These questions do not block acceptance of the direction, but they must be resolved before the affected phase lands:

1. What ordinary Arena block sizes and reset-retention cap best fit Input, AST, and Radiant workloads?
2. At what measured size, if any, should Arena use VM regions instead of ordinary `memtrack` allocation for oversized blocks?
3. Which existing pools are genuinely shared across threads, and can ownership be split rather than synchronized?
4. Which Radiant types justify a dedicated type-stable slab pool after rpmalloc removal?
5. Can NamePool and ShapePool become entirely append-only Arena-backed stores without changing refcounted owner teardown?
6. How large should GC VM extents be, and how aggressively should the GC release
   completely empty extents versus decommit and retain them?

Revision 0.2 resolves two earlier questions. Direct allocation accounting uses
owner-local `MemContext` counters plus `MemCategory` totals, with allocation-level
records only in `memtrack` DEBUG mode. Before the first non-rpmalloc default,
the **D4.2.2** path must provide one failure entry point, a recursion/stampede
guard, failing-allocator exclusion, deterministic fault injection, retry, and a
configured final fail disposition. Cache ordering, parking, and the full
cost-based reclaimer ladder may land later under the existing Stage 2 design.

Defaults for implementation planning are: `memtrack`-backed Arena blocks,
conservative bounded retention, thread-confined `memtrack` owner groups with no
v1 owner transfer, allocate-copy-free reallocation, GC extents that may hold
multiple slabs, and no new type-stable allocator until release profiling
supports one.

---

## 19. Definition of Done

The rpmalloc migration is complete only when all of the following are true:

- GC owns all of its VM pages/slabs and has no `Pool*` backing.
- Arena owns its block list and has no `Pool*` backing.
- Every former Pool user is classified as Arena, direct `memtrack` owner,
  `memtrack` callback adapter, `memtrack` owner group, or an audited type-stable
  owner.
- `memtrack` is the sole system-allocation API, passes the common edge contract
  in OFF/STATS/DEBUG modes, and has no independent pressure ladder beside
  `MemContext`.
- `MemContext` still owns every allocator node and reports an empty graph at clean shutdown.
- VM regions have one authoritative subsystem owner; optional diagnostic nodes
  are non-owning and accounting counts each byte exactly once.
- Linux, macOS, and Windows builds do not compile, download, initialize, finalize, patch, or link rpmalloc.
- Required baselines, Test262, sanitizer, fault-injection, teardown, and release-performance gates pass or have explicitly accepted measured deltas.
- Formal Design D4 and the related working documents are reconciled as described in §16.
- Source/build checks prevent rpmalloc, unregistered allocator creation,
  parallel system-allocation wrappers, and unauthorized raw allocator calls from
  returning unnoticed.

At that point Lambda owns only the policies unique to Lambda: GC allocation,
region allocation, semantic owner lifetimes, `memtrack` instrumentation, and
thin ownership tracking. General-purpose allocation remains the operating
system and C runtime's responsibility.
