# Lambda Heap Memory Management Design

**Status**: Architecture implemented (rpmalloc removed); §9 remediation and MP-15..MP-17 implementation pending
**Version**: 2.5.0
**Date**: 2026-08-10
**Scope**: **Native heap-based memory management only** — how heap bytes are obtained, owned, shared, and released across Lambda and Radiant. Stack-based memory management (the native C stack, the runtime's side/number stacks, rooting frames, and stack-disciplined value storage) is **out of scope**: see [Lambda_Design_Stack_Frame.md](./Lambda_Design_Stack_Frame.md), [Lambda_Design_Stack_Rooting.md](./Lambda_Design_Stack_Rooting.md), and [Formal Design D5](../doc/Lambda_Formal_Design.md#d5-execution-state-stacks-and-rooting). (ScratchArena, §4.2, remains in scope: its *discipline* is stack-like but its storage is heap-owned arena blocks.)
**Related**: [Formal Design D4](../doc/Lambda_Formal_Design.md#d4-memory-management), [Memory Context](./Memory_Context.md), [Memory Model](./Lambda_Design_Memory_Model.md), [GC2](./Lambda_Garbage_Collector2.md)

**Revision 1.0** (2026-08-08): implemented the rpmalloc exit — hardened `memtrack` as the
sole system-heap substrate, opaque VM-region identity, explicit allocation edge
contracts, multi-slab GC extents.
**Revision 2.0** (2026-08-10): restructured around the governing four-mechanism
model; ratified the arena/pool semantic split (sequential+batch vs
individual+individual), mandatory context binding, and allocator-level
ref-counting for shared arenas/pools (MP-12, MP-15..MP-17); recorded the
post-landing conformance audit and remediation plan (§9).
**Revision 2.1** (2026-08-10): renamed from `Lambda_Design_Mem_Pool.md`; scope
restricted explicitly to heap-based management; absorbed the surviving design
content of `Memory_Pooling.md` (ScratchArena design → §4.2, decision matrix →
§1.5, individual-free census → §5.1, rpmalloc-era history → Appendix A) and
retired that document.
**Revision 2.2** (2026-08-10): arena batch free ruled to have exactly two
variants — whole-arena (reset/destroy) and tail-region (mark/rewind, the
sidecar-stack pattern) — MP-15 updated; rewind added to the §8 edge contract
and §11.3 gates; ScratchArena repositioned as the per-allocation refinement
of tail-region free.
**Revision 2.5** (2026-08-10): R4 restated — the runtime validation path must
allocate **nothing** (fast predicate mode); the two-mode validator redesign is
out of scope and moved to `Lambda_Schema_Validator.md`. V7 reframed around the
allocation itself rather than its mechanism.
**Revision 2.4** (2026-08-10): §5.7 call-site census added (54 pools, ~46 are
arena candidates); MP-19 string-builder two-case model (§2.3) and MP-20
NamePool/ShapePool are arena users (§4.3); R4b census-driven reclassification.
**Revision 2.3** (2026-08-10): raw-surface roadmap ruled (MP-18, §2.5):
function-scoped temporaries migrate to `stack_alloc`/`stack_free`
(heap-backed, thread-confined, LIFO-checked); remaining memtracked
`malloc`/`calloc`/`free` becomes internal to the memory context/manager —
no free-floating call sites, enforced by source audit.

---

## 1. Governing Model

### 1.1 The four mechanisms

Heap data is kept in exactly four ways. No fifth mechanism may be introduced
without a new design ruling.

1. **Tracked raw allocation** — `malloc`/`calloc`/`free` through the hardened
   `memtrack` API, for singular, explicitly owned buffers (§2). Roadmap
   (MP-18, §2.5): this surface splits into `stack_alloc`/`stack_free` for
   function-scoped temporaries plus manager-internal use — no free-floating
   raw call sites remain.
2. **GC heap** — the script runtimes' collector-owned storage (§3;
   D4.3.1–D4.3.2).
3. **Arena** — **sequential allocation, batch free**, in two variants:
   whole-arena free (reset/destroy) and **tail-region free** (mark/rewind —
   alloc, free the tail region, alloc, free the tail region, …, free the
   arena; the sidecar-stack pattern). No individual/non-tail free. Sharing:
   read-only, multi-threaded readers allowed; writing: single thread (§4).
4. **Pool** — **individual allocation, individual free** (plus owner-wide
   teardown as the safety net). Sharing: read-only, multi-threaded readers
   allowed; writing: single thread (§5).

This is a *mechanism* taxonomy, formalized as **D4.1.4**. Formal **D4.1.1v2**
enumerates a *content* taxonomy (GC heap / Input arena / AST-const pool /
NamePool) on a different axis: a content tier states lifetime and collector
visibility; a mechanism states how bytes are obtained and released. Every
content tier maps onto one mechanism (Input arena → arena; NamePool →
arena-backed append-only store; AST/const → arena; runtime values → GC
heap).

### 1.2 Mechanism contracts

| | Alloc | Free | Sharing | Writing | Backing | Context-bound | Ref-counted |
|---|---|---|---|---|---|---|---|
| **raw memtrack** | individual | individual | one explicit owner object | owner | system heap | attributed to owner node | no — ownership is singular |
| **GC heap** | slab pop / bump per GC policy | trace + sweep | runtime values, collector-visible | runtime's mutator thread | VM regions owned by GC | yes — the runtime/eval context | n/a — collector-owned |
| **arena** | sequential (bump), grow by linking blocks | **batch only**: whole-arena (reset/destroy) or tail-region (mark/rewind) | read-only, multi-threaded readers | single thread | chunk-granularity memtrack blocks; VM for oversized | **required** (§1.3) | **required when shared** (§1.4) |
| **pool** | individual | **individual** (+ reset/destroy teardown) | read-only, multi-threaded readers | single thread | one memtrack block per allocation, single inline record | **required** (§1.3) | **required when shared** (§1.4) |

Consequences worth stating once:

- An arena never exposes individual `arena_free()`/`arena_realloc()`; freeing
  the *tail region* via rewind is batch free, freeing anything else is not — a
  call site that needs non-tail free is a pool (or raw memtrack) site by
  definition.
- A pool is chosen *only* when individual free is genuinely exercised;
  "allocations die together" is an arena lifetime even if the code currently
  holds a `Pool*` (§1.5).
- Both arena and pool hot paths are lock-free: single-writer discipline makes
  synchronization unnecessary, and no global registry may be consulted per
  allocation (MP-14).

### 1.3 Context binding

Every arena and pool is created **against a context** — a named `MemContext`
owner node recording role, label, parent, and thread mode at creation
(D4.2.1). There are no free-floating arenas or pools. Canonical contexts:

- **document/input context** — the Input arena of a parsed document
  (D4.1.2–D4.1.3);
- **parse context** — parser/AST scratch and compilation-lifetime storage;
- **eval context** — per-runtime pools/arenas for evaluation-transient
  structures (one per script isolate);
- **validation call** — the validator's scratch arena (§9 R4);
- **layout/render context** — Radiant document/view regions (D4.5.1);
- **JIT/module context** — MIR module storage and caches;
- **session/application context** — the root that ultimately owns the above.

Binding rules:

- The context is the allocator's parent in the `MemContext` graph; teardown of
  the context releases — or drops its reference to (§1.4) — every allocator it
  owns, in reverse dependency order.
- A context proves its allocator child list empty at destroy; a leaked arena or
  pool is a context bug, not an allocator bug.
- APIs accept an owner whose lifetime they actually require; a `Pool*` or
  `Arena*` parameter is never passed merely to select where bytes come from.

### 1.4 Sharing and ref-counting

The sharing model for arena and pool is **single writer, multiple readers**:
after an allocation is published, any thread may read it; all mutating
operations — alloc, free, reset, destroy — execute on the single writer
thread.

When an arena or pool is shared across more than one owning context (e.g. an
Input arena referenced by a document *and* by eval contexts that hold views
into it), its lifetime is governed by an **allocator-level `ref_count`**:

- Creation sets `ref_count = 1`, held by the creating context.
- `arena_acquire()`/`pool_acquire()` and `arena_release()`/`pool_release()`
  adjust it. The count is **atomic** — it is the *only* allocator field mutable
  from non-writer threads.
- The last `release` destroys the allocator. In v1 the last release must occur
  on the owner thread or be deferred to the owning context's teardown; there is
  no cross-thread destroy marshaling (consistent with the no-owner-transfer
  rule in §5.4).
- **Reset requires exclusivity**: `arena_reset()` and pool-wide reset are
  batch invalidations and are permitted only at `ref_count == 1`. Debug builds
  check this; violating it hands dangling pointers to readers. Tail-region
  rewind (§4.1 variant 2) of writer-local scratch is always permitted — the
  sidecar-stack pattern never publishes its tail to readers; rewinding a
  region whose allocations *were* published follows the same exclusivity rule
  as reset.
- Accounting counts bytes once, at the creating context's node; additional
  holders record non-owning diagnostic edges only (§7.1's count-once rule).
- **Immortality is the limiting case**: static Mark data "born shared and
  immortal" (D4.1.2) is a permanent reference — the count never reaches zero.
  Ref-counting generalizes the existing immortal-storage discipline to
  shareable-but-finite lifetimes (e.g. session navigation replacing a
  document while older eval contexts still read it — the known
  `DomDocument` leak class).

### 1.5 Classification rule

Every allocation site is classified by lifetime, using the least powerful
mechanism that satisfies it:

1. If all allocations die together → **arena**.
2. If one object owns one resizable buffer → **raw memtrack**.
3. If multiple variable-size allocations require individual free *and*
   owner-wide teardown → **pool**.
4. If allocation is GC-traced → **GC heap**, never a residual pool.
5. If a uniform fixed-size type demonstrates measured hot reuse → a
   specialized type-stable slab pool may be considered (MP-8, D4.5.1) — a
   specialized owner, not a general allocator.

| Lifetime and behavior | Mechanism | Examples |
|---|---|---|
| Traced runtime object/data | GC heap | Lambda runtime containers, dynamic scalar homes |
| Bulk lifetime; reset/destroy together | arena | Input/Mark data, AST/const data, document regions |
| Nested LIFO temporary lifetime | ScratchArena (mark/rewind over an arena) | parser, layout, rendering, validation scratch |
| One explicit owner; independent resize/free | raw memtrack | `StrBuf`/`StringBuf` backing, isolated byte buffers |
| C library allocator callback | memtrack adapter preserving the callback ABI | FreeType-style alloc/realloc/free |
| Individual free plus owner-wide teardown | pool | selectively released document/view records |
| Uniform fixed-size objects, measured churn | type-stable slab pool | eligible Radiant types under D4.5.1 |
| Interned/append-only semantic storage | owning arena / append-only store | NamePool/ShapePool entries (MP-20) |
| String built into a pool destination | pool-backed builder, `realloc` growth (buffer may move) | `StringBuf` general case (§2.3) |
| String built into an arena destination | arena-backed builder, **tail growth** (buffer stays put) | formatters producing output; parsers building Input/Mark strings (§2.3) |
| String built for an unknown/local destination | standalone builder on raw memtrack | `StrBuf` (§2.3) |

No call site chooses a pool merely because it currently accepts a `Pool*`
parameter. Raw system allocation remains private to `memtrack`, the VM layer,
and third-party ABI boundaries; application code has no raw-allocation escape
hatch.

**Decision matrix for new code** (absorbed from the retired
`Memory_Pooling.md`, updated to the current contracts):

| Question | → Pool | → Arena |
|---|---|---|
| Need to free individual objects? | ✅ | ❌ — reclassify the site |
| Need `realloc` to grow buffers? | ✅ (allocate-copy-free, §5.3) | ❌ — use raw memtrack (§2) |
| All objects freed together at end? | ⚠️ works but wasteful | ✅ |
| High allocation rate, small objects? | ❌ per-alloc record overhead | ✅ O(1) bump |
| Objects survive the parent scope? | ✅ own release point | ❌ |
| Frame/pass-based reset pattern? | ❌ | ✅ `arena_reset()` / ScratchArena |
| LIFO scoped lifetime (sidecar stack)? | ❌ | ✅ mark/rewind (§4.1 variant 2) |
| Scoped temporaries with mid-scope free? | ⚠️ | ✅ ScratchArena LIFO free (§4.2) |
| External C allocator callbacks (FreeType-style)? | ❌ | ❌ — memtrack adapter (§2.3) |

---

## 2. Mechanism 1 — Tracked Raw Allocation (`memtrack`)

Direct `memtrack` allocation is used when one object has unambiguous ownership
of one independently resized or released buffer. `StrBuf`/`StringBuf` is the
model case: the buffer object owns its character allocation, growth is
explicit, destruction has one release point, and no owner-wide operation is
needed to discover or reclaim the buffer. These sites use
`mem_alloc`/`mem_calloc`/`mem_realloc`/`mem_free`.

There is **one** system-allocation stack: `memtrack` is the public Lambda
allocation API, improved in place. No parallel family of "checked system
allocation" wrappers may be added (MP-5).

### 2.1 Tracked-mode policy (MP-13)

`memtrack` has three modes. Their contracts:

- **OFF** — raw system-heap fast path. No headers beyond what a consuming
  mechanism requires, no registry, no lock. **Release-build default.**
- **STATS** — counters only: per-category/per-owner byte and count accounting
  via the inline `MemAllocHeader` (validated by `MEMTRACK_STATS_MAGIC`,
  best-effort). **No allocation-level registry.** Debug-build default.
- **DEBUG** — full diagnostics: the live-allocation registry (consulted
  *before* any inline metadata is read, so invalid and repeated frees never
  touch freed storage), guard bytes, poisoning, wrong-owner/double-free/
  thread-confinement detection.

`MEMTRACK_MODE=OFF|STATS|DEBUG` overrides the default in any build.
Allocation-level records exist **only** in DEBUG mode; a tracked mode may
never impose a per-allocation global lock on release hot paths. (This restates
the rev 0.2 resolution that the landed implementation violated — §9 V3/V4.)

The mode is selected **once at `memtrack_init`** and the allocation
representation (raw vs header-backed) is a lifetime invariant of every block:
`memtrack_set_mode` and the per-thread `memtrack_thread_enable` toggle both
currently have zero runtime callers, and each carries a dormant
representation-mixing hazard that must be resolved before either gains a
caller — see §15 Q12–Q13.

### 2.2 Hardening requirements

- checked `count * size`, `header + payload`, guard-byte, and
  reallocation-size arithmetic before calling the system allocator;
- `size_t`-width metadata with no truncation of large sizes;
- transactional reallocation in all modes (failure preserves the old
  allocation, bytes, linkage, and accounting);
- deterministic zero-size behavior shared by all modes (§8);
- owner/role attribution that does not require a second allocation header for
  ordinary direct users;
- deterministic, thread-scoped fault injection at the system-allocation call;
- payload alignment through `max_align_t`.

The `MemCategory` taxonomy is the per-allocation diagnostic tag; each
subsystem owner has a `MemRole` with one canonical role → default-category
mapping. Owner-local counters roll up into the owning `MemContext` node
without one heavyweight node per allocation.

Under **D4.2.2**, `MemContext` is the sole pressure/OOM escalation
coordinator. Legacy `memtrack` threshold callbacks are compatibility adapters
that register reclaimers with `MemContext`; they do not run a second pressure
ladder inside `mem_alloc` (MP-10).

### 2.3 String builders: standalone vs owner-backed (two deliberate cases)

A string builder is not a single classification — it has **two cases**, and
both are correct:

**Case 1 — standalone** (`StrBuf`, `lib/strbuf.h`). No owner; the buffer is
raw `memtrack` storage grown by `realloc`, released by `strbuf_free`. This is
the §1.5 rule-2 model case of §2. Use it when the result is consumed and
discarded locally, or when it must be copied into a destination whose
lifetime is unknown at build time. Under MP-18 (§2.5) a function-scoped
`StrBuf` is a `stack_alloc` client.

**Case 2 — owner-backed** (`StringBuf`, `lib/stringbuf.h`). The builder is
constructed against a pool or arena and **the string itself is allocated from
that owner**, so the finished string is *already in the destination lifetime*.
`stringbuf_to_string()` is a pure ownership handoff — it finalizes `len`/
`is_ascii`, detaches (`sb->str = NULL`), and returns the `String*` in place.

The point of case 2 is the copy it removes. The alternative — build in a
standalone buffer, then copy the bytes into the pool/arena — costs an extra
full-length copy and a second allocation on **every** produced string, on
paths (formatters, serializers, Input parsing, CSS/DOM text) that produce
them by the million. Owner-backed building is therefore the default whenever
the destination lifetime is known at build time; standalone is the fallback
when it is not.

Case 2 splits by owner, and the two halves grow by different mechanisms —
this pairing is the substance of the case:

**Pool-backed → `realloc` growth.** The shape that exists today: the buffer
grows via `pool_realloc` (allocate-copy-free, §5.3) and the builder shell is
itself a pool allocation. The buffer **may move** on growth, which callers
must tolerate — they hold the `StringBuf*`, not the bytes. This is the
general-purpose case: it imposes no ordering discipline, so the owner may
serve interleaved allocations while a string is being built. `StringBuf` is
therefore a legitimate §1.5 rule-3 user — its individual free is real, not a
misclassification.

**Arena-backed → tail growth.** Batch-free-only (§4.1) forbids `realloc`, so
the buffer instead **stays put and extends** by bumping the arena while it is
the arena's most recent allocation — the §4.1 variant-2 machinery. Nothing
moves, so pointers taken during the build stay valid, and the finished
`String` is already in the destination lifetime.

Two consumer classes, in increasing order of care required:

- **Formatters — the cleanest fit.** A formatter *consumes* a data structure
  and *produces* bytes: one output string, appended monotonically from start
  to finish, with no data-structure allocation competing for the tail. The
  existing code already has the right shape — `format_json(Pool* pool, …)`
  builds `stringbuf_new(pool)` into the caller's destination and returns
  `stringbuf_to_string(sb)`, while its working state lives on a *separate*
  `ScopedFormatPool`; `format-json.cpp` and `format-html.cpp` make zero
  `pool_alloc`/`pool_calloc`/`pool_strdup` calls against the destination
  during the format walk. Swapping the destination pool for an arena is
  therefore a mechanism change with no restructuring, and it removes the
  per-output `realloc` chain on the hottest output path in the system.
- **Parsers — the same win, with one hazard.** A parser also appends to one
  string at a time, but unlike a formatter it is simultaneously *building*
  the Mark/AST nodes that go into the very same Input arena. Any node
  allocated while a string is open steals the tail. So a parser either opens
  strings only between node allocations, or gives strings their own arena /
  ScratchArena, or uses a fallback below.

The discipline is the invariant to state once: **nothing else may allocate
from that arena while the string is open**, or the buffer stops being the
tail. Where that cannot be guaranteed, fall back to:

- **grow-and-abandon** — allocate the larger buffer, copy, leave the old one
  dead in the arena; geometric growth bounds the waste under 2x of the final
  size, reclaimed at the arena's own reset/rewind;
- **size-then-build** — compute or estimate the exact length, make one arena
  allocation, fill it once; wastes nothing and copies nothing, and is
  preferred wherever the length is cheap to compute.

A ScratchArena (§4.2) builder that hands its result to a longer-lived arena
is case 1 in disguise and pays the copy — choose it only when the destination
is genuinely unknown while building.

Ruling: **MP-19**. Neither case is deprecated; a call site picks by whether
the destination lifetime is known at build time. What is *not* acceptable is
routing an owner-backed build through a standalone buffer and copying, purely
because the owner's API is inconvenient.

### 2.4 Callback adapters

C library allocator callbacks (FreeType-style) receive a dedicated adapter
over `memtrack` with matching C semantics — never an arena (whose free is
batch-only) nor a pool whose free the library cannot see. Where the ABI cannot
carry owner metadata, the adapter owns an explicit context object or a
documented fixed role.

### 2.5 Roadmap: splitting the raw surface (MP-18)

The raw memtracked `malloc`/`calloc`/`free` call surface is scheduled to split
into two cases, after which mechanism 1 has **no free-floating call sites** in
application code:

1. **Stack-oriented temporaries → `stack_alloc`/`stack_free`.** A large class
   of raw sites is function-scoped: allocate a temporary within a function,
   free it before the function returns (the same pattern the pre-ScratchArena
   survey found 25+ times in Radiant — Appendix A). These migrate to a
   dedicated API — `stack_alloc`/`stack_calloc`/`stack_free` — whose *name*
   states the discipline: LIFO, function-scoped, freed before return. The
   storage remains heap-based and in scope for this document: the natural
   backing is a thread-confined sidecar stack (§4.1 variant 2 /
   ScratchArena), not the native C stack. Debug builds check the discipline —
   LIFO order and no allocation surviving its function (leak-on-return
   diagnostic).
2. **The remainder becomes manager-internal.** Once stack-oriented sites are
   migrated, memtracked `malloc`/`calloc`/`free` is called only from within
   the memory data context/manager layer — the mechanism implementations
   (GC/arena/pool backends, VM metadata, §2.3 adapters) and the audited
   buffer-owner types of §1.5 rule 2, whose public API is the owner object
   (`StrBuf` etc.), not the allocator. No free-floating
   `malloc`/`calloc`/`free` remains in application code, enforced by the same
   source-audit gate class that already rejects rpmalloc references and
   unregistered allocator creation (§16).

End state for mechanism 1: the mechanism itself is unchanged (system heap
under memtrack), but its *public surface* shrinks to `stack_alloc`/
`stack_free` plus owner-object APIs; everything else reaches the system heap
only through the manager.

---

## 3. Mechanism 2 — GC Heap

Under **D4.3.1–D4.3.2** the collector owns allocation policy *and* backing.
The GC obtains VM regions from the platform layer (§6) and owns all object
slabs, data-zone blocks, and large-object mappings directly. It never calls a
pool or arena for object, data-zone, slab, or large-object storage.

- **Object zone** — per-size-class slab lists; slabs live in GC-owned VM
  extents (an extent may hold multiple slabs; mapping granularity is measured
  GC policy). Allocation pops a free slot or bumps in the current slab; sweep
  returns dead slots to the slab free list; empty slabs/extents follow a
  deterministic, stats-visible retention policy. Addresses never move
  (D4.3.1).
- **Data zone** — nursery and tenured zones own page-aligned segments;
  allocation is bump-based; collection resets or replaces segments under the
  existing dual-zone algorithm with its one-fixup-per-surviving-object
  invariant.
- **Large objects** — one object per dedicated VM region, tracked by a GC
  region record (base, mapped size, object size, kind, mark state, linkage);
  individual reclamation and heap teardown both release each mapping exactly
  once via that record.
- **Lifecycle** — the `GCHeap` control structure is an explicit memtrack
  allocation registered on the GC's `MemContext` node; `gc_heap_destroy()`
  releases all regions, proves the region tables empty, and unregisters once.
- **Stats** — object live bytes, slab committed/reserved bytes, data-zone
  bytes, large-object bytes, and retained empty capacity are reported
  separately.

The GC heap is bound to its runtime's **eval context** (§1.3); one collector
per script isolate.

---

## 4. Mechanism 3 — Arena

### 4.1 Contract: sequential allocation, batch free

An arena directly owns a linked block list:

```text
Arena (ref_count, owner context, writer thread)
  current --------------+
  blocks -> Block -> Block -> Block
             |        |        |
           used/capacity + aligned payload
```

- `arena_alloc()`/`arena_calloc()` bump within the current block and grow by
  linking a new block. Default alignment 16 bytes;
  `arena_alloc_aligned()` accepts power-of-two alignments through 256 bytes in
  v1.
- **Batch free comes in exactly two variants:**
  1. **Whole-arena free** — `arena_reset()` invalidates all allocations at
     once (bounded warm-block retention); `arena_destroy()` releases every
     block exactly once.
  2. **Tail-region free (mark/rewind)** — `arena_mark()` captures the current
     position; `arena_rewind(mark)` frees the entire tail region allocated
     after the mark in O(1): the bump pointer rewinds, and whole blocks
     linked after the mark are released or retained per the warm-block
     policy. The canonical usage is the **sidecar stack**: a scratch region
     riding alongside a computation — alloc, free the tail region, alloc,
     free the tail region, …, free the arena. Marks nest LIFO; rewinding to a
     mark older than one already rewound past is a debug diagnostic.
     *Implementation status*: today mark/rewind exists only at the
     ScratchArena level (`scratch_mark`/`scratch_restore`); promoting
     `arena_mark()`/`arena_rewind()` to the arena API proper is part of R6
     (§9.3), after which ScratchArena rides on it.
- **There is no individual/non-tail free.** A site that needs to free an
  allocation *not* at the tail is reclassified (§1.5) — general free-list
  complexity is never restored to arena.
- Blocks come from `memtrack` at **chunk granularity** (geometric growth
  within explicit bounds; oversized requests get dedicated blocks). Very large
  blocks may use the VM layer (§6) after a measured threshold; the choice is
  internal and invisible to callers.
- `arena_owns()` remains available for storage-class checks (range list/index,
  not a backing-pool inference).
- Stats separate used payload, committed block bytes, retained bytes, reset
  count, and peak bytes.

### 4.2 ScratchArena

ScratchArena is the per-allocation refinement of the §4.1 tail-region
variant: where plain mark/rewind frees an anonymous tail region, ScratchArena
tracks each allocation with a header so callers may *request* frees in any
order — but reclamation still happens only at the tail, so it never violates
the batch-free contract. It is a lightweight stack-*disciplined* allocator
whose storage is heap-owned arena blocks (in scope for this doc; only its
usage pattern resembles a stack). Implemented in `lib/scratch_arena.h`/`.c`
with 21 unit tests (`test/test_scratch_arena_gtest.cpp`); design absorbed
from the retired `Memory_Pooling.md` §9.

- **Layout**: each allocation carries a 16-byte `ScratchHeader`
  (`prev` backward link, `size`, `flags`), so payloads stay 16-byte aligned
  and the live allocations form a backward-linked stack with `head` at the
  top.
- **Alloc**: bump `sizeof(ScratchHeader) + aligned_size` from the backing
  arena; link; O(1).
- **Free (LIFO fast path)**: freeing the head rewinds the arena bump pointer,
  then walks backward reclaiming any *consecutive already-freed holes* behind
  it — so out-of-order frees are not leaked, just deferred.
- **Free (non-LIFO)**: the block is marked as a hole (`flags |= FREED`) and
  reclaimed by the next LIFO free's backward walk, or at latest by
  `scratch_reset()`.
- **Mark/rewind** (`scratch_mark`/`scratch_restore`) is the secondary API for
  pure scoped patterns (e.g. `parse_grid_template_areas()`'s 8k+ nested
  allocations restored in one call).
- **Embedding**: a ScratchArena is a by-value field of its owning context —
  `LayoutContext.scratch` and `RenderContext.scratch`, both backed by
  `view_tree->arena` — which is the §1.3 context-binding rule applied one
  level down. Per-allocation overhead (16 B) is negligible for the target
  sites (64 B table arrays to multi-MB pixel buffers); allocations under
  ~32 B should use the plain arena.

Radiant migration state (2026-08): 12 of 15 audited scoped-malloc sites use
ScratchArena (table metadata arrays, blur/shadow/clip/blend pixel buffers,
grid positioning arrays); 3 deferred for plumbing cost (`graph_dagre.cpp`,
SVG/PDF `text_content`, `layout_counters.cpp` temp array).

### 4.3 Arena-backed semantic tiers

The D4.1.1 content tiers stay distinct even where they share the arena
implementation: Input/Mark arenas remain collector-invisible (D4.1.3);
AST/const storage uses a compilation-lifetime arena; NamePool/ShapePool remain
semantic owners whose append-only entries live in owned arena blocks; Radiant
document/view regions continue to act as cycle collectors under D4.5.1.
Sharing a block implementation does not merge ownership tiers or let pointers
outlive their tier.

**NamePool and ShapePool are arena users (MP-20).** Despite the name, neither
is a §5 pool: interning is *append-only* — an entry is created once, deduped
by lookup thereafter, and lives until its owner dies. There is no eviction,
so there is no individual free, and the §5.7 census confirms zero
`pool_free`/`pool_realloc` in `name_pool.cpp` and `shape_pool.cpp`. Both
therefore take an **arena** for entry storage; each remains its own semantic
owner and `MemContext` node (§1.3) — the change is the backing mechanism, not
the ownership model. Two consequences worth having explicitly:

- The interning *index* (hash buckets) is a separate classification: a table
  that is rebuilt or resized wholesale is arena storage too, but one that
  needs incremental release of superseded bucket arrays is either a pool
  allocation or, better, a table sized to be rebuilt on grow and abandoned in
  the arena (the same grow-and-abandon trade as §2.3).
- Today `js_scope`/`py_scope`/`rb_scope` create an `ast_pool` *only* to back
  `name_pool_create()`, which forces the hand-ordered
  "release name_pool before destroying ast_pool" teardown noted in §5.7.
  Moving NamePool to an arena lets those pools disappear entirely and the
  ordering hazard with them — R4b covers this.

### 4.4 Context and sharing

Every arena is bound to a context at creation (§1.3). A shared arena carries
the atomic `ref_count` of §1.4; `arena_reset()` demands `ref_count == 1`
(debug-checked). The dominant shared case is a document's Input arena kept
alive by eval contexts that still read its static Mark data after navigation;
acquire/release replaces both leaks and premature teardown in that class.

---

## 5. Mechanism 4 — Pool

### 5.1 Contract: individual allocation, individual free

A pool serves multiple variable-size allocations that need **both** individual
release and owner-wide teardown (`pool_drain`/`pool_destroy` as the safety
net). Public API: the existing `pool_alloc`/`pool_calloc`/`pool_free`/
`pool_realloc`/`pool_create`/`pool_destroy` family.

Implementation-wise the pool is the `memtrack` **owner group** of MP-6: an
intrusive ownership layer over tracked raw allocation — *not* a general
allocator. `memtrack` and the system heap handle variable-size allocation and
fragmentation; the pool records which allocations share teardown. Required
behavior:

- O(1) allocation insertion; O(1) individual free and unlink; O(n)
  reset/destroy over allocations still owned.
- Correct `calloc` overflow checking and zeroing; transactional realloc
  (§5.3).
- Exact requested live bytes, allocation count, peak bytes, operation counts;
  aggregate system-heap overhead stays a separate memtrack diagnostic.
- Debug detection of wrong-owner free, double free, corrupted header, and
  use-after-reset — the DEBUG path consults memtrack's live registry before
  reading inline metadata (§2.1); it never inspects a header behind an
  arbitrary invalid pointer.
- No global/TLS initialization beyond the memtrack process lifecycle.
- Thread confinement by default, stated at creation, checked in debug builds
  (MP-7).

**Canonical individual-free sites** (the census evidence that these pools are
pools; sites without individual free were reclassified to arena in the
rpmalloc exit, Appendix A):

- `lambda/input/input.cpp` — `pool_free()` of old map `.data` buffers during
  ShapeEntry/TypeMap resize;
- `radiant/view_pool.cpp` — selective view-component cleanup (10 distinct
  `pool_free()` sites);
- `radiant/cmd_layout.cpp` — `pool_realloc()` of stylesheet arrays;
- FreeType-style allocator callbacks formerly served by `pool_realloc` now
  belong to the §2.3 memtrack adapter, not to a pool.

### 5.2 Allocation layout — one record, registry-free hot path (MP-14)

Each pooled allocation carries **one** inline metadata record extending
memtrack's aligned private header — owner, requested size, intrusive
prev/next links, and debug fields (magic, generation, poison state):

```text
+--------------------------------------------------+-------------------+
| owner | requested_size | prev | next | debug ...  | aligned payload   |
+--------------------------------------------------+-------------------+
```

- The record preserves `max_align_t` payload alignment and checked
  `header + payload` arithmetic, and is private to the allocator.
- Grouped allocation must **not** prepend a second tracker header, and must
  **not** maintain a separate growable index (array or hash map) that could
  fail after the system allocation succeeded. The ownership list is
  intrusive; the whole record-plus-payload block is allocated and released as
  one memtrack unit.
- `pool_free` resolves the record by **header arithmetic** (fixed offset from
  the user pointer) validated by magic + owner fields; in DEBUG mode the
  memtrack registry is consulted first. No mode consults a registry on the
  release-build hot path.
- In OFF mode a pooled allocation is: one system malloc + minimal inline
  record + list link. Nothing else.

(The landed 1.0 implementation violated both prohibitions — §9 V1/V2. **R3a**
closed the separate-index prohibition and shrank the record to 48 B; the
record is still prepended to memtrack's header rather than merged into it —
**R3b**, sequenced with R2.)

### 5.3 Reallocation

`pool_realloc` uses allocate-copy-free: allocate and link the replacement,
copy `min(old, new)`, unlink and release the old block; on failure the old
allocation is untouched. In-place optimization requires profiling plus
dedicated list-integrity tests. (Source use is rare — 6 call lines at the
2026-08-08 census.)

### 5.4 Threading

One writer thread per pool; intrusive list operations are lock-free
(D4.2.1's hot-path intent without TLS heaps). Genuinely shared pools use the
§1.4 ref-count for *lifetime* — mutation remains single-writer. Remedies for
cross-thread use remain: per-worker pools merged by copying into the
destination owner; the subsystem's existing lock above the pool; or direct
memtrack allocations whose container already synchronizes. A hidden mutex in
every pool is not the default; an unavoidable shared-mutation pool must make
its synchronization mode explicit in constructor, accounting, and tests. v1
has no owner-transfer operation.

### 5.5 Context and sharing

Every pool is bound to a context at creation (§1.3) and carries the §1.4
`ref_count` when shared. Pool-wide reset requires `ref_count == 1`
(debug-checked). Individual `pool_free` of a *published* allocation that
another thread may still read is a writer-contract violation, same as any
other mutation.

### 5.6 What a pool must not become

No size classes, per-thread caches, remote-free queues, span maps, huge-page
policy, or custom coalescing. Those are the signs of rebuilding the general
allocator Lambda removed (MP-1). The transitional `Pool` API's 1 GiB
single-allocation limit is retained; changing it requires a call-site audit.

### 5.7 Call-site census (2026-08-10)

The §1.5 classification applied to every live pool. Source scan over `lambda/`,
`radiant/`, `lib/`: **54 application pool-creation sites**; individual
`pool_free`/`pool_realloc` appears in **~110 call lines concentrated in 8
owners**. Pool identity is approximate where a `Pool*` is passed across
subsystems; the owner column names the pool as its users see it.

**Correctly classified as pool** (individual free genuinely exercised —
§1.5 rule 3):

| Owner | Individual-free evidence |
|---|---|
| `doc->document_pool` (CSS/DOM) | the heaviest user: `dom_element.cpp` (class lists, tag/id strings, `ext`, text nodes), `css_style_node.cpp` (~40 sites freeing value/declaration/AVL trees), `dom_lifecycle.cpp`, `style_epoch.cpp`, `js_dom.cpp`, `dom_range.cpp` |
| `input->pool` | map/element `.data` buffers freed on shape resize; `attr_names`/`attr_types` (`input.cpp` 6 sites); `mark_editor.cpp` mirrors the same resize discipline |
| `tree->prop_pool` (Radiant views) | `view_pool.cpp` selective component release; `view_reuse.cpp` canonical-index churn incl. bucket regrow |
| `epoch->pool` (style epochs) | epoch/manager/builder release + the one surviving `pool_realloc` for recipe entries |
| font pool (`font.global.pool`) | `font_tables.c` frees every parsed table individually; `font_context.c` frees handles |
| `scheduler->pool` (animation) | per-animation free on completion |
| runtime heap pool | `lambda-eval.cpp` frees superseded container storage |
| `StringBuf` (caller's pool) | grow via `pool_realloc`, release via `pool_free` — see the caveat below |

**Arena candidates** — no individual free anywhere; allocations die together
with one bulk `pool_destroy`. Roughly **46 of the 54 sites**, grouped by
leverage:

| Group | Sites | Note |
|---|---|---|
| Validator | `doc_validator`, `validator.ast` | see **R4**: the fix is an allocation-free fast mode, not reclassification |
| Per-script result/AST | `script.result` ×6 (`transpile-mir.cpp` ×4, `runner.cpp` ×2), `script.pool`, `module_registry` | runs on **every** script execution |
| Guest-language ASTs | `js.ast`, `py.ast`, `rb.ast`, `bash.ast` | py/rb already allocate AST nodes from a sibling arena; the pool now backs only `NamePool` |
| Temp format/convert | `main.fmt` ×3, `convert.temp`, `proc.output.format`, `emit-parse.pool`, `jube.hosted.result` | textbook scoped temporaries |
| Radiant per-pass | `cmd_layout` ×6, `render.svg_inline` ×2, `window`, `state.dump.scratch`, `rdt.vector.*` ×2, `radiant.document` ×2, `script_runner` | `cmd_layout`'s former `pool_realloc` users are gone — it is now free-less |
| Input/module/misc | `input.sysinfo` ×2, npm ×5, `css.dom_node.format`, `js.cssom`, `pdf.document.pool` | |

Two structural observations from the census:

- **NamePool/ShapePool never free individually** (`name_pool.cpp`,
  `shape_pool.cpp`: zero `pool_free`/`pool_realloc`) — they are append-only
  interning stores, so **their backing should be an arena, not a pool**
  (MP-20, §4.3). Every entry lives until the owner dies; interning has no
  eviction, so nothing in either store can ever exercise the individual free
  it currently pays for. This resolves the mechanism half of §15 Q5, leaving
  only refcounted-owner teardown open.
- **The I2 ordering hazard survives in hand-written form.** `js_scope.cpp:451`,
  `py_scope.cpp:250`, and `rb_scope.cpp:195` each carry the comment "Release
  name_pool BEFORE destroying ast_pool" — manual reverse-dependency ordering
  that D4.2.1 cascade teardown is supposed to own (§7.2). Converting these
  pools to context-bound arenas (MP-16) removes the hand-ordering, not just
  the allocation cost.

**`StringBuf` is correctly classified** — it is the owner-backed string
builder of §2.3 (MP-19), not a rule-2 misclassification. It takes a caller
`Pool*` precisely so the produced `String` is allocated *in the destination
lifetime* and `stringbuf_to_string()` can hand it over with no copy; its
individual `pool_realloc`/`pool_free` are the real growth and shell-release
operations of that design. The standalone counterpart `StrBuf` remains the
rule-2 direct-`memtrack` case. Open item: if a call site's destination is an
**arena**, it needs the §2.3 arena-backed growth strategy rather than a
pool.

---

## 6. Platform VM/Page Layer

The VM layer supplies raw virtual-memory regions — no size classes, free
lists, object headers, or owner semantics:

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

Guarantees: page-aligned sizes/addresses with checked rounding; each region
records mapping base, usable base, reserved/committed bytes, alignment, owner,
role, and state; release takes the `MemVmRegion` identity (callers never
reconstruct bases); over-alignment keeps both bases recorded; failure reports
upward into `MemContext` pressure handling without silent abort or recursive
reclaim; debug builds consult a live-region registry before reading opaque
records; Linux/macOS use `mmap`, Windows `VirtualAlloc`, under identical
contracts; executable JIT mappings remain a distinct role with explicit W^X
handling.

The owning subsystem (GC heap or arena) is the sole release authority.
Optional `MEM_KIND_VM_REGION` records are non-owning diagnostics that never
double-count bytes or unmap during cascade teardown (D4.2.1). The VM layer is
the future Stage 2 page-allocation choke point (D4.2.2) but stays a thin
portability/accounting layer; reclamation policy lives above it in
`MemContext`.

---

## 7. Memory Context Integration

`MemContext` is the allocator factory, ownership graph, accounting surface,
and teardown coordinator (D4.2.1). Context binding (§1.3) makes it the home of
every arena and pool.

### 7.1 Node model

Nodes exist for at least: `MEM_KIND_GC_HEAP`, `MEM_KIND_ARENA`,
`MEM_KIND_SCRATCH`, `MEM_KIND_MEMTRACK_OWNER` (pool), `MEM_KIND_TYPE_POOL`,
optional `MEM_KIND_VM_REGION` diagnostics, and the existing
JIT/cache/external owners. GC and arena have no parent edge to any backing
pool; their node is parented by the owning context. Shared allocators (§1.4)
keep their node at the creating context; additional holders appear as
non-owning reference edges. Aggregate and detail views count each byte exactly
once.

### 7.2 Lifecycle and teardown

- Factory creation records role, label, parent context, thread mode, and
  backend before returning the allocator.
- Subsystem destruction releases implementation resources, proves the child
  list empty, then unregisters once; cascade teardown runs in reverse
  dependency order; the teardown reentrancy guard remains.
- For a shared allocator, context teardown performs a `release`; destruction
  happens at ref-count zero (§1.4).
- Direct memtrack allocations are attributed to owner node/role/category, not
  promoted to context nodes.

### 7.3 Allocation failure and Stage 2

VM and memtrack failures report upward through one non-recursive entry point;
`MemContext` runs the D4.2.2 escalation (drop caches, trim arenas, collect GC,
retry, configured fail/park/abort). Reclaimers must not allocate through the
failing path, re-enter the failing allocator, or take locks in deadlock-prone
order; deterministic fault injection exists so these paths are tested, not
inferred. `MemContext` destroy callbacks run under its non-recursive registry
lock and must not register/unregister/query context state.

---

## 8. Allocation Edge Contract

Ratified in Phase 0; implemented identically by every mechanism surface.

| Operation | Required behavior |
|---|---|
| `alloc(0)` | Return `NULL`; no allocation, no counter change. |
| `calloc(0, n)` / `calloc(n, 0)` | Return `NULL`; checked multiplication first. |
| `free(NULL)` | No-op. |
| `realloc(NULL, n)` | Equivalent to `alloc(n)`. |
| `realloc(ptr, 0)` | Release `ptr`, return `NULL`. |
| failed `realloc(ptr, n)` | Return `NULL`; preserve `ptr`, bytes, linkage, accounting. |
| allocation failure | Return `NULL` after the configured D4.2.2 retry path; no silent abort. |
| reset/destroy | Invalidate all owned allocations exactly once; repeated destroy unsupported. |
| rewind(mark) | Invalidate exactly the tail region allocated after the mark (arena only); marks nest LIFO; rewinding to a mark already rewound past is a debug diagnostic. |

Alignment: memtrack payloads through `max_align_t`; arena default 16 B with
`arena_alloc_aligned()` power-of-two through 256 B in v1; VM usable bases at
least page-aligned; callback adapters document stricter ABI alignment.

In DEBUG mode, wrong-owner free, repeated free, stale generation, corrupted
header, ref-count misuse (§1.4), and thread-confinement violations are
deterministic diagnostics, with the live registry consulted before inline
metadata. In OFF/STATS these remain programmer-contract violations; code may
log and refuse only when it can do so without reading invalid storage.

Fault injection operates at two independent boundaries — memtrack system
allocation and VM reserve/commit — thread-scoped, filterable by owner, role,
operation, and fail-after count.

---

## 9. Conformance Review (2026-08-10) and Remediation

Review of the landed 1.0 implementation (commit `61d3d4083`) against this
document. Verdict per mechanism: GC heap and arena **conform** (VM-region
backing at `gc_data_zone.c:27`; chunk-granularity arena blocks at
`arena.c:160`; gcbench/binarytrees flat across v26/v27/HEAD). Tracked raw
allocation conforms mechanically but had a **policy defect** (V3/V4). Pool
**did not conform** — it collapsed into per-allocation tracked raw allocation
with duplicated bookkeeping (V1/V2).

### 9.1 Measured regressions

Release builds, archived reference binaries, 3-run medians of workload-only
`__TIMING__` (ms). Result27 measured commit `0f65f9d6df`, which **predates**
the allocator landing, so the benchmark round never saw these rows and the
MP-9 release-evidence gate never ran on the landing commit.

| Row | v27 | HEAD | Factor |
|---|---:|---:|---:|
| kostya/primes2 | 26 | ~750 | 29x |
| awfy/sieve2 | 0.16 | 3.6 | 23x |
| jetstream/deltablue2 | 70 | 515 | 7.4x |
| jetstream/splay2 | 257 | 1710 | 6.7x |
| awfy/storage2 | 1.05 | 4.5 | 4.3x |
| awfy/list2 | 0.90 | 3.4 | 3.8x |
| r7rs/nqueens2 | 1.7 | 5.3 | 3.1x |
| jetstream/cube3d2 | 12 | 31 | 2.6x |
| jetstream/raytrace3d2 | 63 | 165 | 2.6x |
| awfy/cd2 | 560 | 920 | 1.6x |

Profile attribution (primes2, `/usr/bin/sample`): with `MEMTRACK_MODE=OFF`,
96% of execution sits inside `pool_alloc`, of which only ~25% is the system
malloc; the default STATS mode adds a further ~5x (825 → 174 ms after R1a).
Dominant caller: `lambda_type_check` → validator allocating a
`ValidationResult` per element — including on success (V7). Teardown replays a
global-locked registry delete per live block, dominating wall-clock outside
`__TIMING__`.

### 9.2 Conformance audit

| ID | Status | Finding | Violates |
|---|---|---|---|
| **V1** | **CLOSED** (R3a, 2026-08-10) | `PoolBlock` (64 B) prepended in addition to memtrack's header. *Fixed:* the record is the only pool-side header and is down to **48 B** — `base` (always the record address) and `allocated` (always `requested + block_header_size()`) were removed, and a `_Static_assert` holds the budget. The second-header half of §5.2 (merging into memtrack's own header) stays open as **R3b**, sequenced with R2 | §5.2 one-record rule |
| **V2** | **CLOSED** (R3a, 2026-08-10) | `pool_lookup` side hash table; insert per alloc (amortized rehash), delete per free; the insert can fail after the system allocation succeeded (`lib/mempool.c:325`). *Fixed:* the table and all `pool_lookup_*` helpers are deleted; `pool_find_block` is fixed header arithmetic validated by magic + owner, and the ownership list is the intrusive one. `pool_alloc` now has no failure mode after the system allocation succeeds | §5.2 no-separate-index rule; intrusive-links requirement |
| **V3** | open | STATS mode keeps the per-allocation registry authoritative (`lib/memtrack.c:684`) | §2.1 (registry is DEBUG-only); falsifies `memtrack.h` "STATS (minimal overhead)" |
| **V4** | closed (R1a) | `MEMTRACK_MODE_STATS` release default (`lambda/main.cpp:1962`) — V3's global-mutex hashmap on every release alloc/free; the OFF fast path never engages | §2.1; MP-9 intent |
| **V5** | open | No release-build performance evidence accompanied the landing | **MP-9** |
| **V6** | open | No thread-confinement debug check in `lib/mempool.c` | §5.1; MP-7 |
| **V7** | open (R4) | The runtime type-check path allocates a `ValidationResult` **per element** — on success as well as failure — because it reuses the reporting-oriented `lambda validate` machinery for what is semantically a predicate. The defect is the allocation itself, not its mechanism: no allocator choice makes O(n) discarded records acceptable on a hot boundary | §1.1 (mechanism must fit the need); R4 |
| **V8** | **CLOSED** (R3a, 2026-08-10) | `pool_lookup` insert/find/remove terminate only at a NULL slot; tombstones never count toward the resize trigger and removal never restores NULLs, so a table whose NULLs are exhausted spins forever. Exact-algorithm simulation: with varied addresses, **247 alloc/free churn cycles** reach 0 NULLs / 44 tombstones on the 64-slot table and the next insert never terminates. Not reproduced in-process (2M cycles) — system-malloc address recycling bounds the landing-slot set — a **latent** unbounded loop whose probability grows with session length and address diversity (2026-08-10 audit addendum). *Fixed:* the whole class is removed with the table — header arithmetic is O(1) by construction, with no probe loop to fail to terminate. Guarded by `temp/pool_hang_repro.c` | correctness; §5.2 |

### 9.3 Remediation

Each slice retains the §11 gates and lands with its measured release-build
delta (MP-9). R1–R3 are independent.

- **R1 — tracked-mode defaults** (§2.1). *Slice R1a (release default OFF) is
  implemented in `lambda/main.cpp` and measured: primes2 825 → 155 ms;
  storage2 and list2 fully recover to v27.*
- **R2 — STATS becomes counters-only**; allocation-level registry becomes
  DEBUG-exclusive; STATS `mem_free` validates via inline
  `MEMTRACK_STATS_MAGIC`.
- **R3 — pool layout conformance** (§5.2): fold owner/links into the single
  memtrack record, delete `pool_lookup`, resolve frees by header arithmetic;
  DEBUG consults the registry first. (Supersedes a reviewed-then-reverted
  interim edit that removed the table but kept the second header.) Deleting
  the table also removes the **V8** latent-hang class wholesale — R3 is now a
  correctness fix, not only a performance one. Split into two slices:

  - **R3a — table deletion and record slimming: IMPLEMENTED 2026-08-10**
    (`lib/mempool.c`). `PoolLookupEntry`, the three `Pool` lookup fields and
    all seven `pool_lookup_*` helpers are gone; `pool_find_block` is
    `(PoolBlock*)((uint8_t*)ptr - block_header_size())` validated by
    `magic == POOL_BLOCK_MAGIC && owner == pool`; `pool_release_block` unlinks
    in O(1) and **clears `magic` before freeing**, so a double free or a stale
    pointer is refused rather than resolving to a freed record (the safety
    the table used to provide by construction). The record dropped `base` and
    `allocated` and is now **48 B, 16-aligned** (was 64 B + ~16 B lookup slot
    ⇒ ~80 B/allocation), with a `_Static_assert` holding the budget.
    `pool_allocation_size` stays header-inclusive — it must keep matching what
    `pool_release_block` subtracts from `live_bytes`, which
    `radiant/view_reuse.cpp` relies on for symmetric `index_bytes` accounting.
    Closes **V1** (per-allocation size half), **V2**, and **V8**.

    *Measured*, release builds, same machine and moment, 3-run medians of
    workload-only `__TIMING__` (ms). `HEAD` here is post-R1a, so it is already
    far below the §9.1 `HEAD` column:

    | Row | v27 | HEAD (post-R1a) | **R3a** | R3a speedup | R3a ÷ v27 |
    |---|---:|---:|---:|---:|---:|
    | kostya/primes2 | 25.3 | 145.1 | **39.5** | 3.67x | 1.56x |
    | awfy/sieve2 | 0.156 | 0.485 | **0.231** | 2.10x | 1.48x |
    | jetstream/deltablue2 | 71.3 | 171.8 | **87.7** | 1.96x | 1.23x |
    | jetstream/splay2 | 260.3 | 579.6 | **305.2** | 1.90x | 1.17x |
    | awfy/storage2 | 1.033 | 0.835 | **0.720** | 1.16x | **0.70x** |
    | awfy/list2 | 0.914 | 1.083 | **0.973** | 1.11x | 1.06x |

    Every row improves and none regresses; storage2 is now faster than v27.
    The residual over v27 on the top four rows is the **V7/R4** validator
    allocation volume, which R3a makes cheaper per allocation but does not
    remove. Standalone microbench (`temp/pool_perf.c`, OFF mode, arm64): bulk
    pool 16 B **59.3 → 23.9 ns/op** median (min 22.8), i.e. 3.3x raw malloc →
    1.3x; churn pool 16 B 24.9 → 22.5. Gates: `make test-lambda-baseline`
    (Input 2104/2104, Lambda Runtime 1568/1568 — the two `test_js_gtest`
    failures under parallel load pass standalone, 327/327, the known
    heavy-load flake), `make test262-baseline` 40261/40261,
    `test_mir_gc_stress_gtest` 59/59, `test_mempool_gtest` 40/40, library
    suite clean, Radiant baseline clean.

  - **R3b — merge the pool record into memtrack's private header** (the
    remaining half of the §5.2 one-record rule, plus "DEBUG consults the
    registry first"). Deferred: it edits the same header layout as **R2**, so
    it is sequenced with R2 rather than landed against a header R2 is about
    to change.
- **R4 — allocation-free fast validation.** `validate_against_type` must
  support a **fast mode that allocates nothing** and answers only
  true/false. Runtime type checking (`lambda_type_check` at declared
  boundaries) is a predicate; it currently reuses the machinery built for
  `lambda validate`, which constructs a detailed `ValidationResult` per
  element in order to report *why* something failed — a report the runtime
  path discards on the overwhelmingly common success. Making that allocation
  cheaper is the wrong fix; the allocation should not happen. Reporting is
  recovered by re-running validation in full mode **only after** a failure
  is known, which pays the detailed cost once, on the cold path.
  The rest of that redesign — the two-mode entry contract and duplicate-error
  suppression within a collection — is **validator design, out of scope
  here**: see [Lambda_Schema_Validator.md §Validation Modes](./Lambda_Schema_Validator.md).
  Mem_Heap owns only this consequence: **no allocation on the runtime
  validation path**. Independent long-term fix tracked under Tune17/TE-17:
  representation-level validation of packed arrays (lane-descriptor check
  instead of element walk) removes the element loop entirely.
- **R4b — census-driven reclassification** (§5.7, §1.5 rule 1): convert the
  remaining ~46 free-less pools to context-bound arenas, highest leverage
  first — the per-script `script.result`/`script.pool`/`module_registry`
  family (allocated on **every** script run), then the guest-language AST
  pools — where MP-20 moves `NamePool` onto an arena, letting `ast_pool`
  disappear entirely along with the hand-ordered
  `name_pool`-before-`ast_pool` teardown — then temp format/convert and the
  Radiant per-pass pools. Each conversion is independently landable and
  independently measurable.
- **R5 — thread-confinement debug check** (§5.1; closes V6).
- **R6 — MP-15..MP-17 implementation**: context-binding audit (every
  `pool_create`/`arena_create` names its owner context), `acquire`/`release`
  ref-count API, reset-exclusivity debug check, the shared-allocator
  census (§15 Q7), and promotion of `arena_mark()`/`arena_rewind()` to the
  arena API proper (§4.1 variant 2; ScratchArena then layers on it).

Acceptance: every §9.1 row ≤1.1x its v27 value on a verified stripped release
binary; `make test-lambda-baseline` and `make test262-baseline` green;
teardown no longer dominated by per-block registry deletes.

### 9.4 Pool micro-costs (2026-08-10, standalone bench, OFF mode, arm64)

ns per operation; *churn* = alloc/free one slot repeatedly (address recycled);
*bulk* = 200k allocations then owner teardown — the validator/engine shape.
"R3 preview" was a temp build with the lookup table removed
(header-arithmetic find), i.e. §5.2 minus the record merge; **"pool (R3a)"**
is that shape as landed in `lib/mempool.c`, re-measured against the pre-R3a
source on the same machine at the same moment (`temp/pool_perf.c`, 6 runs,
medians).

| ns/op | 16 B | 64 B | 1 KB |
|---|---:|---:|---:|
| churn raw malloc+free | 18.0 | 19.1 | 23.8 |
| churn pool (pre-R3a) | 25.7 | 25.6 | 24.6 |
| churn pool (R3 preview) | 23.0 | 22.8 | 21.8 |
| **churn pool (R3a, landed)** | **22.5** | 22.8 | 21.1 |
| bulk raw malloc | 18.9 | 22.6 | 101 |
| bulk pool (pre-R3a) | **66.4** | 76.9 | 264 |
| bulk pool (R3 preview) | **26.1** | 48.9 | 218 |
| **bulk pool (R3a, landed)** | **23.9** | 49.3 | 219 |
| bulk arena (§4) | **11.6** | 11.1 | 21.5 |

Readings: churn was already benign pre-R3a (~1.4x raw — recycled addresses
land on their own tombstones). The engine-relevant **bulk** shape was 3.5x raw
at 16 B, driven by lookup inserts with rehash cascades, the 64 B second header,
and the teardown walk's per-entry probe/remove; **R3a as landed recovers it to
1.3x raw** (23.9 vs 18.9), matching the preview. Arena is still 2x faster than
the post-R3a pool at 16 B and 10x at 1 KB — the measured case for R4-style
reclassification of bulk lifetimes stands, since it is about *how many*
allocations happen, not how fast each one is. Space: per-allocation overhead
was ~80 B (64 B `PoolBlock` + ~16 B lookup slot) — 5x the payload for 16 B
objects — and is now **48 B**, with R3b to fold that into memtrack's own
header. The rpmalloc-era pool's TLS fast path beat even raw malloc, so
by design (MP-1) the pool's ceiling is now ~1.2–1.4x raw; consumers that need
the old speed on bulk lifetimes are reclassified to arena, not re-optimized
in pool.

---

## 10. Migration Record — the rpmalloc Exit

Compressed record of revision 1.0; the full phase plan lives in git history.

**Why rpmalloc was removed.** A reproduced upstream huge-span defect (released
huge span left linked from `span_used`; `rpmalloc_heap_free_all()` could
follow a dangling span) plus independent Lambda ownership/teardown defects;
limited value to the dominant paths (GC and arena already owned their
policies); global/TLS init, vendor drift, patch state, and sanitizer
complexity. The migration was architectural — no replacement general-purpose
allocator was adopted (MP-1), and no rpmalloc patch is retained.

**Census (2026-08-08).** `pool_alloc` 192 call lines, `pool_calloc` 522,
`pool_free` 125, `pool_realloc` 6, `pool_create` 108, `mem_pool_create` 62,
`arena_alloc` 136, `arena_calloc` 39. Active `arena_free()` outside
`lib/arena.c` was limited to ScratchArena and one DOM lifecycle path —
the evidence behind the §4 batch-free contract. `pool_create_mmap()` was
rejected as a blanket replacement (eager mapping, no-op free, full
retention).

**Phase status.** Phases 0–4 and 6 complete: contracts ratified, VM layer and
hardened memtrack landed, GC and arena detached from Pool
(`gc_heap_create_with_pool()` removed), residual users migrated, rpmalloc
sources/build entries deleted. The 2026-08-08 acceptance run passed the
allocator/GC ownership suites, Lambda baseline (3,661/3,661), JS (327/327),
page-load (105/105), Test262 (40,261/40,261), release build, lint, and source
audits (validator DSO runner blocked by its pre-existing `_ItemNull` link
defect, unrelated). **Phase 5's release-performance gate is retroactively
reopened** by the §9 audit: it was recorded as complete without the release
benchmark comparison it required, and §9.3 is its completion.

**Compatibility rules that remain in force.** A `free` may not silently
become a no-op unless the API is explicitly region-based; `realloc` failure
preserves the old allocation; APIs accept an owner whose lifetime they
require; a direct-allocation object releases itself even if an adjacent arena
or context is destroyed; temporary compatibility aliases carry a removal
deadline.

---

## 11. Verification and Acceptance Gates

### 11.1 Allocator contract tests

Common contract suite against memtrack OFF/STATS/DEBUG and the pool surface:
alignment through `max_align_t` (plus supported over-alignment); checked
arithmetic and near-`SIZE_MAX` rejection; zero-size behavior; `calloc`
zeroing; realloc grow/shrink/preserve/failure; reset and repeated reset; empty
and non-empty destroy; wrong-owner and double-free diagnostics
(debug/sanitizer); thread-confinement diagnostics; ref-count
acquire/release/destroy-at-zero and reset-exclusivity diagnostics (§1.4);
owner/role/category accounting without double counting; deterministic
thread-scoped fault injection including reclaimer recursion exclusion.

### 11.2 VM and GC tests

Reserve/commit/decommit/release with page-boundary cases; over-aligned
regions; exact large-region accounting; repeated create/destroy; object-slab
fill/sweep/reuse with multiple slabs per extent and empty-extent trimming;
data-zone reset/compaction with forced GC at every allocation point;
large-object churn without double release; pointer stability and precise
rooting under D4.3.1; empty `MemContext` graph and zero live VM regions at
shutdown; non-owning diagnostic nodes neither double-count nor release.

### 11.3 Arena and pool tests

Geometric growth, oversized dedicated blocks, reset retention cap,
`arena_owns()` boundaries; arena tail-region rewind across block boundaries,
warm retention on rewind, and the stale-mark diagnostic; scratch nested
mark/rewind and misuse diagnostics;
pool alloc/free/reset/destroy list integrity under randomized sequences;
shared arena/pool acquire/release lifecycle including
release-while-reading and reset-at-refcount>1 rejection; `StrBuf` and
callback-adapter growth/failure through memtrack; per-document Radiant
teardown and repeated navigation without retained growth.

### 11.4 Project suites and tooling

`make test`, Lambda/Radiant/Test262 baselines on Linux and macOS; Windows VM,
allocator-contract, and GC/arena lifecycle suites; ASan/UBSan (TSan for
shared-context lifecycle tests); **release-build benchmarks on a verified
stripped binary** for representative evaluation, parsing, layout, rendering,
and Test262 workloads — a debug build is never used for performance decisions,
and a binary is verified release by size and absence of debug format strings;
long-running churn tests tracking RSS, committed VM bytes, retained arena
capacity, and GC slab occupancy; clean-build audit proving no rpmalloc symbol.

Performance acceptance uses workload thresholds against archived reference
binaries (`test/benchmark/exe/`). A regression is not automatically rejected,
but it must be attributed to a policy and either corrected or explicitly
accepted with data — and the comparison must run **on the landing commit**
(the §9 V5 lesson).

---

## 12. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| memtrack/system-heap overhead at formerly pooled hot sites | Move true bulk lifetimes to arena (§1.5); profile release builds; type-stable pool only for measured fixed-size churn. |
| A second checked-allocation or pressure layer diverges | Improve memtrack in place; `MemContext` sole D4.2.2 coordinator; source checks reject parallel wrappers. |
| Tracking tax returns to release hot paths | §2.1 mode policy is normative; registry is DEBUG-only; benchmark gate on landing commits (§11.4). |
| Pool re-grows duplicate bookkeeping | §5.2 one-record rule; emission of a second header or side index is a review-blocking violation. |
| Arena retains peak block chain | Bounded warm retention; release excess on reset and pressure trim. |
| Ref-count misuse (leak or premature destroy) | Debug diagnostics for reset-at-refcount>1, release-after-zero, destroy-with-holders; TSan lifecycle tests; context teardown performs release, not destroy. |
| Shared-allocator accounting double-counts | Bytes counted once at creating node; holders are non-owning edges (§7.1). |
| One OS mapping per small GC slab | Multi-slab extents; large objects keep dedicated mappings. |
| GC region bookkeeping leaks/double-unmaps | One authoritative record per mapping; debug registry; teardown and fault-injection tests. |
| Moving realloc corrupts intrusive links | Allocate-copy-free transaction; optimize only with integrity tests. |
| Hidden cross-thread pool use | Audit creator/free threads; split ownership or explicit synchronization; confinement check (R5). |
| Stage 2 OOM callbacks re-enter allocators | Non-allocating reclaimers, lock order, recursion guard, fault injection. |
| Pool grows into a custom allocator | §5.6 prohibitions; allocator-policy expansion requires a new ruling. |

---

## 13. Formal-Spec Reconciliation

Completed for revision 1.0: D4.2.1 and D4.5.1 revised in place (`v2`) so hot
allocator operations stay free of the `MemContext` registry lock, memtrack is
the sole system-allocation substrate, and "one substrate" means VM primitives
+ hardened memtrack + Memory Context; related memory docs updated. Completed
for revision 2.1: `Memory_Pooling.md` retired — surviving content absorbed
here (Appendix A holds its historical record and issue IDs).

Completed for revisions 2.0–2.3 (formal spec **1.8.0**, 2026-08-10):

1. **D4.1.1v2** — mechanism-axis note added: content tiers map onto the four
   mechanisms.
2. **D4.1.4*** (new) — the four-mechanism taxonomy and the arena/pool
   semantic contracts, including arena's two batch-free variants (MP-12,
   MP-15).
3. **D4.2.3*** (new) — context binding (MP-16); **D4.2.4*** (new) —
   allocator-level ref-counting for shared arenas/pools, generalizing
   D4.1.2's born-shared-immortal discipline (MP-17); **D4.2.5*** (new) —
   tracking-is-a-diagnostic policy and the raw-surface roadmap (MP-13,
   MP-14, MP-18).
4. **D4.5.1v3** — arena's Radiant-seam wording tightened to batch-only
   lifetime; the "audited compatibility users" individual-free carve-out
   removed.
5. **D4.1.3**, **D4.3.1**, **D4.3.2** semantically unchanged; Appendix A
   footnotes record implementation status for every `*`-marked ruling;
   Appendix C maps D4.1/D4.2 to this document.

No formal language-semantics ruling changes.

---

## 14. Decision Ledger

MP-1..MP-11 are the accepted revision-1.0 decisions; MP-12..MP-18 are the
revision-2.x rulings (MP-12 and MP-15..MP-18 stated by the design owner
2026-08-10; MP-13/MP-14 from the §9 review, implementing prior resolutions).
All are formalized in Formal Design 1.8.0 (D4.1.1v2, D4.1.4, D4.2.3–D4.2.5,
D4.5.1v3).

| ID | Decision | Formal basis |
|---|---|---|
| **MP-1** | Retire rpmalloc without adopting another general-purpose allocator. | D4.1.1, D4.2.1 |
| **MP-2** | The VM layer provides opaque region identities and accounting only; the owning subsystem is the sole release authority. | D4.2.1, D4.2.2 |
| **MP-3** | GC owns its VM extents, object slabs, data blocks, and large-object records directly; an extent may hold multiple slabs. | D4.3.1–D4.3.2 |
| **MP-4** | Ordinary arena owns blocks directly and exposes region lifetime, not arbitrary individual free. | D4.1.1, D4.1.3, D4.5.1 |
| **MP-5** | Singular resizable buffers and allocator callbacks use the hardened memtrack API; no parallel system-allocation wrapper. | D4.2.1 |
| **MP-6** | Pool = the existing `memtrack_pool_*` surface implemented as intrusive memtrack owner groups; no separate tracked-pool allocator. | D4.2.1 |
| **MP-7** | Pools are thread-confined by default; shared ownership must be explicit. | D4.2.1 |
| **MP-8** | Specialized type-stable pools only for audited fixed-size workloads. | D4.5.1 |
| **MP-9** | Correctness migration precedes optimization; new allocator machinery requires release-build evidence **measured on the landing commit**. | D4.2.1 |
| **MP-10** | `MemContext` is the sole memory-pressure/OOM coordinator; legacy memtrack callbacks are adapters. | D4.2.2 |
| **MP-11** | All backends obey the common edge contract (§8). | D4.2.1–D4.2.2 |
| **MP-12** | The four-mechanism model (§1.1: raw-tracked / GC / arena / pool) is the governing memory-mechanism taxonomy. | D4.1.1v2, D4.1.4 |
| **MP-13** | Tracked-mode policy: release default OFF; STATS is counters-only; allocation-level records exist only in DEBUG. | D4.2.5 |
| **MP-14** | The pool hot path is registry-free: one inline metadata record with intrusive links; the memtrack registry is a DEBUG diagnostic, never the ownership mechanism. | D4.2.5; §5.2 |
| **MP-15** | Semantic split: arena = sequential allocation + batch free only, where batch free has exactly two variants — whole-arena (reset/destroy) and tail-region (mark/rewind, the sidecar-stack pattern); pool = individual allocation + individual free. A non-tail-free need reclassifies the site, never adds free lists to arena. | D4.1.4, D4.5.1v3; §1.2, §4.1 |
| **MP-16** | Every arena and pool is bound at creation to a `MemContext` owner context (document/input, parse, eval, validation, layout/render, JIT, session); no free-floating allocators. | D4.2.3 |
| **MP-17** | Shared arenas/pools carry an allocator-level atomic `ref_count` (acquire/release; destroy at zero; reset requires exclusivity). Mutation remains single-writer; the count is the only cross-thread-mutable allocator field. | D4.2.4, D4.1.2 |
| **MP-18** | Roadmap: the raw memtrack surface splits into `stack_alloc`/`stack_free` (function-scoped LIFO temporaries, heap-backed by a thread-confined sidecar stack) and manager-internal use; no free-floating `malloc`/`calloc`/`free` remains in application code, enforced by source audit. | D4.2.5; §2.5 |
| **MP-19** | String builders have two legitimate cases: standalone (`StrBuf`, raw memtrack) and owner-backed (`StringBuf`, allocated from the destination pool/arena so the finished string needs no copy). Owner-backed splits by growth mechanism: **pool-backed grows by `realloc`** (buffer may move; no ordering discipline), **arena-backed grows by tail extension** (buffer stays put; requires the string to own the arena tail — formatters fit cleanly, parsers must avoid interleaving node allocation), with grow-and-abandon or size-then-build as the fallbacks when it cannot. Never build standalone and copy into an owner merely for API convenience. | D4.1.4; §2.3 |
| **MP-20** | NamePool and ShapePool are append-only interning stores with no eviction: their entry storage is an **arena**, not a pool. They remain distinct semantic owners and `MemContext` nodes; only the backing mechanism changes. | D4.1.1v2, D4.1.4; §4.3 |

---

## 15. Open Questions

Carried and new; each must be resolved before the affected slice lands:

1. What arena block sizes and reset-retention caps best fit Input, AST, and
   Radiant workloads?
2. At what measured size should arena use VM regions instead of memtrack for
   oversized blocks?
3. Which existing pools are genuinely shared across threads, and can ownership
   be split rather than synchronized?
4. Which Radiant types justify a dedicated type-stable slab pool?
5. ~~Can NamePool and ShapePool become entirely append-only arena-backed
   stores?~~ **Mechanism resolved by MP-20** (§4.3): yes — the census found
   zero individual free in either. Still open: whether refcounted owner
   teardown needs any change, and whether the interning *index* (bucket
   arrays on grow) is arena grow-and-abandon or a pool allocation.
6. How large should GC VM extents be, and how aggressively should the GC
   release versus decommit-and-retain empty extents?
7. **Shared-allocator census (MP-17)**: which arenas/pools are actually shared
   across contexts today (Input arenas across navigation, NamePool across
   runtimes, Radiant doc regions across sessions), and which of those are
   immortal-by-policy versus ref-counted-finite?
8. **Last-release thread policy**: is deferred-to-context-teardown sufficient
   for all v1 sharers, or does any subsystem need marshaled cross-thread
   destroy?
9. Should the validation scratch arena (R4) be per-validator instance or
   per-validation-call?
10. **Stack-oriented census (MP-18)**: which raw memtrack sites are
    function-scoped temporaries eligible for `stack_alloc`/`stack_free`, and
    is the backing one per-thread sidecar stack or one per major context
    (eval/layout/render)?
11. **Owner-object surface (MP-18)**: after the split, does the §1.5 rule-2
    direct API (`mem_alloc`/`mem_free`) stay public for audited buffer owners
    like standalone `StrBuf`, or do those owners move onto a manager-issued
    handle so the raw API can go fully private? (Owner-backed `StringBuf` is
    unaffected — it allocates from its destination owner, MP-19.)
12. **Dormant hazard — OFF→tracked mode switch with live raw allocations.**
    `memtrack_set_mode` refuses a mode change while the registry holds live
    allocations, but the guard counts only the *registry*: switching
    OFF→STATS/DEBUG proceeds when raw (OFF-mode, headerless) allocations are
    still live, because OFF never registers them. A later `mem_free` of such
    a pointer in the tracked mode hits a registry miss — reported as an
    "invalid free" and **leaked** (the registry-first rule prevents
    corruption, not the leak). Dormant today: `memtrack_set_mode` has zero
    runtime callers and the mode is fixed at `memtrack_init`. Resolution
    before the API gains a caller: either track a process-lifetime
    raw-allocation counter so the guard can refuse OFF→tracked too, or
    document one-shot mode selection as the contract and assert on any
    post-init switch.
13. **Dormant hazard — `memtrack_thread_enable` representation mixing.** The
    per-thread TLS toggle has the same
    alloc-in-one-representation/free-in-another shape at thread granularity:
    a block allocated while tracking is disabled (raw, headerless) and freed
    while enabled misses the registry and leaks; the reverse order would
    route a header-bearing block through the raw `free` path with a shifted
    base pointer — heap corruption, not just a leak. Dormant today: zero
    callers. Resolution before first use: define the legal toggle window
    (e.g. only while the thread owns no live memtrack allocations,
    debug-checked), or remove the API in favor of thread-confined owner
    groups, which already express the intended semantics.

Planning defaults: memtrack-backed arena blocks, conservative bounded
retention, thread-confined pools with no v1 owner transfer,
allocate-copy-free realloc, multi-slab GC extents, no new type-stable
allocator until release profiling supports one.

---

## 16. Definition of Done

Revision 1.0 (rpmalloc exit) — complete except where noted:

- GC owns all VM pages/slabs; arena owns its block list; no `Pool*` backing in
  either. ✅
- Every former Pool user classified per §1.5. ✅ (validator reclassification
  reopened as R4)
- memtrack is the sole system-allocation API and passes the §8 contract in all
  modes. ✅
- `MemContext` owns every allocator node; empty graph at clean shutdown. ✅
- No platform builds, downloads, initializes, patches, or links rpmalloc. ✅
- Release-performance gate on the landing commit. ❌ — reopened; closed by
  §9.3 acceptance.

Revision 2.0 — done when:

- §9.1 rows restored to ≤1.1x their v27 values on a verified stripped release
  binary, with baselines and Test262 green (R1–R4).
- §2.1 mode policy implemented and asserted by tests (R1/R2).
- §5.2 single-record layout implemented; source check rejects a second header
  or side index (R3). *Side index gone and the record slimmed to 48 B with a
  `_Static_assert` budget (R3a, 2026-08-10); merge into memtrack's header
  outstanding (R3b).*
- Thread-confinement and ref-count diagnostics exist in debug builds (R5,
  §11.1).
- Context-binding audit complete: every `arena_create`/`pool_create` names its
  owner context (MP-16), and the shared-allocator census (§15 Q7) classifies
  every sharer as immortal or ref-counted (MP-17).
- Formal D4 reconciliation merged (spec 1.8.0, 2026-08-10): D4.1.1v2, D4.1.4, D4.2.3–D4.2.5, D4.5.1v3. ✅

At that point Lambda owns only the policies unique to Lambda: GC allocation,
region allocation, semantic owner lifetimes, memtrack instrumentation, and
thin ownership tracking. General-purpose allocation remains the operating
system and C runtime's responsibility.

---

## Appendix A — History and Retirement of the rpmalloc-Based Pool

Historical record, absorbed from the retired `Memory_Pooling.md` survey
(dated 2026-04-12; full text in git history). Its issue IDs (**P1–P4**,
**A1–A4**, **I1–I2**) remain cited by [Memory Context](./Memory_Context.md)
and resolve here.

### A.1 The rpmalloc-era architecture

Until 2026-08, the codebase used a three-tier layering **Arena → Pool →
rpmalloc → OS**:

- **Pool** (`lib/mempool.c`) wrapped rpmalloc *first-class heaps* — compiled
  with `RPMALLOC_FIRST_CLASS_HEAPS=1` for per-pool heap isolation and
  `ENABLE_OVERRIDE=0` so system malloc stayed untouched
  (`librpmalloc_no_override.a`). Individual free went through
  `rpmalloc_heap_free()`; bulk teardown through `rpmalloc_heap_free_all()` +
  `rpmalloc_heap_release()`. A separate `pool_create_mmap()` mode was a
  bump-over-mmap variant whose `free` was a no-op.
- **Arena** (`lib/arena.c`) allocated its chunks *from* a Pool and carried an
  individual `arena_free()` with free-list recycling — semantics the current
  design has removed (MP-4/MP-15; the LIFO use case moved to ScratchArena,
  §4.2).
- **Raw malloc** covered pool metadata and one-off utilities.

The integration was assessed as correct (proper heap isolation, lazy
per-thread init, bulk-free-first teardown), with known weak points: no
per-thread `rpmalloc_thread_finalize()` for long-running workers, and
platform-specific heap-pointer validation.

### A.2 Pre-retirement hardening record

Issue IDs preserved for external citations:

| ID | Summary | Outcome |
|---|---|---|
| **P1** | `mmap_pool_grow` left cursor/limit dangling on `MAP_FAILED` (OOB write) | Fixed: null cursor/limit; alloc paths check |
| **P2** | `mempool_cleanup()` never called; thread caches never finalized | Fixed at program exit; per-worker-thread finalize remained open and is now moot |
| **P3** | mmap-mode `pool_realloc` read past the old allocation | Fixed: 16-byte size header before each mmap allocation |
| **P4** | Heap-pointer validation `< 0x10000` incomplete | Superseded by retirement |
| **A1–A4** | Arena free-list defects: small-block exclusion, missing ownership check, alignment-blind free-list search, no bump-back coalescing | All fixed in the free-list era; the free-list itself was later removed (MP-15) |
| **I1** | Arena not thread-safe | Now the §1.4 single-writer contract, documented and debug-checked (R5) |
| **I2** | `pool_drain()` freed chunks under a live Arena backed by that Pool | Structurally eliminated: arena owns its blocks directly (MP-4); the ordering hazard class is enforced by `MemContext` child counting |

The same survey drove the Pool→Arena reclassification wave (Ruby/Python AST
builders, PDF view construction, and 12 of 15 Radiant scoped-malloc sites via
ScratchArena) and, in its final recommendation (§8.3 item 9), anticipated the
retirement: after reclassification, only ~3 files genuinely needed individual
free, so rpmalloc's value no longer justified its TLS lifecycle, vendor
drift, and sanitizer friction.

### A.3 Why retired, and what replaced it

The decision record is §10: a reproduced upstream huge-span defect (a
released huge span stayed linked from `span_used`, letting
`rpmalloc_heap_free_all()` follow a dangling span), independent Lambda
ownership/teardown defects, and the architectural fact that GC and arena
already owned their allocation policies. `pool_create_mmap()` was explicitly
rejected as a blanket replacement (eager mapping, no-op free, full
retention). Pool became the memtrack owner group of §5; arena took direct
block ownership (§4); the GC took direct VM-region ownership (§3). The §9
review then found and remediated the performance non-conformances of the
first owner-group implementation.

`Memory_Pooling.md` was retired on 2026-08-10 with this revision; its
surviving design content lives in §1.5, §4.2, §5.1, and this appendix.
