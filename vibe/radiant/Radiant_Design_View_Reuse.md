# Radiant Design: View Prop Sharing & Storage Reuse

Status: **IMPLEMENTED** — decisions VR1–VR14 and phases R0–R7 completed 2026-07-18. The measured adoption set is retained reset/scratch separation, canonical `InlineProp` sharing, DOM liveness and arena retirement, tree-wide exact Inline interning, and canonical style epochs. Evidence-gated Font sharing and a custom `TextRect` cache were not adopted.
Scope: DOM node storage, view-prop lifecycle, layout/render scratch, canonical prop sharing, and per-element/canonical `specified_style` storage.
Extends: `vibe/radiant/Radiant_Design_Dom_View_Struct.md` (DV1–DV16 complete).
Related: `Radiant_Design_Memory.md`, `Radiant_Design_Robustness.md` (T7 stale pointers), `vibe/Lambda_Design_Memory_Model.md`, and the completed DOM/view impl plan's alias ledger and memory profile.

---

## 0. Allocation Domains and Generations

This campaign uses six logical allocation domains: three pools for independently managed or bulk-pool objects, and three arenas for fast locality-oriented allocation. An arena is backed by a pool, but it has a separate allocation policy and lifetime boundary.

### 0.1 Six pool/arena categories

| Allocation domain | Kind and backing | Stores | Lifetime and reclamation |
|---|---|---|---|
| **1. `dom.document.pool`** | Pool; document-owned | Element-specific/owned `StyleTree`, owned declarations and COW snapshots, document CSS/resources, allocation-domain managers | Individual `pool_free` where supported; destroyed with the document |
| **2. `dom.node.arena`** | Arena backed by `dom.document.pool` | `DomElement`, `DomText`, `DomComment`, and explicitly registered node-owned arena payloads | Stable while live; retired blocks return through the existing arena recycler; all chunks released at document teardown |
| **3. `view_tree.prop_pool`** | Pool; view-owned | Mutable element-owned `*Prop` groups and their independently managed payloads | Survives retained reflow; owned blocks reset in place, COW/freed individually, destroyed with the ViewTree |
| **4. `view_tree.canonical_prop_arena`** | Arena backed by `view_tree.prop_pool` | Immutable canonical shared prop instances, canonical payload snapshots, and canonical-entry metadata | Survives ordinary `layout_generation`s; append-only/capped for the ViewTree lifetime; external resources released before bulk arena destruction |
| **5. `view_tree.scratch_arena`** | Arena backed by `view_tree.prop_pool` | Layout/render scratch, temporary paint lists, and non-retained display-list construction data | Reset/cleared only at a safe pass boundary after previous temporary consumers finish; never stores retained props or canonical values |
| **6. `style.canonical.epoch.pool`** | Independent pool under the document memory context; one per live style epoch | Canonical shared `StyleTree`s, immutable canonical declaration/value snapshots, exact cascade recipes, and recipe-table entries | Survives reflows; old and new epochs may coexist; destroyed wholesale when no element references that style epoch |

UI reconstruction has one compatibility lane: a fat Lambda `Element` that also
serves as a `DomElement` can physically reside in its retained `Input::arena`.
The lifecycle registry records that actual primary owner and returns the block to
that arena at retirement. This is not a seventh allocation category; ordinary
HTML/page nodes and the six-domain page profile use `dom.node.arena`.

The pre-campaign generic fields mapped to the implemented names as follows:

| Current field | Target role/name |
|---|---|
| `DomDocument::pool` | `dom.document.pool` / `document_pool` |
| `DomDocument::arena` | `dom.node.arena` / `node_arena` |
| `ViewTree::pool` | `view_tree.prop_pool` / `prop_pool` |
| `ViewTree::arena` | `view_tree.scratch_arena` / `scratch_arena` |

`canonical_prop_arena` and `StyleEpoch::pool` are new. Other subsystem allocators, such as StateStore arenas and retained-display-list storage, remain outside this campaign.

The backing relationships are:

```text
document memory context
├── dom.document.pool
│   └── dom.node.arena
├── style.canonical.epoch.pool (epoch N)
└── style.canonical.epoch.pool (epoch N-1, while still referenced)

view tree
└── view_tree.prop_pool
    ├── view_tree.canonical_prop_arena
    └── view_tree.scratch_arena
```

Pool and child-arena byte counters are not additive physical totals: arena chunks are obtained through their backing pool. Metrics therefore distinguish physical backing bytes from logical per-domain usage (VR12).

### 0.2 Style epoch

A **style epoch** is a generation during which the global inputs to selector matching and canonical specified-style construction remain valid. It is not a reflow.

A new style epoch starts when a global input changes, including:

- stylesheet add/remove/reparse;
- mutation of a stylesheet rule or declaration through CSSOM;
- a viewport/media environment change that changes rule matching; or
- another document-wide mode input included in the cascade recipe.

Element-local changes do not automatically create a global style epoch:

- `element.style` or a presentational attribute COWs that element to an owned tree;
- `:hover`/`:focus` rematches the affected element and binds the exact recipe within the current global epoch; and
- a pure content/geometry reflow leaves the style epoch unchanged.

Each live style epoch owns a `style.canonical.epoch.pool`. Elements bound to canonical trees retain the epoch. A new epoch may coexist with the previous epoch while incremental restyling proceeds; the old pool is destroyed only after its element reference count reaches zero.

### 0.3 Layout generation

A **layout generation** increments for every layout/reflow pass. It invalidates pass-local scratch, used-value caches, measurement generations, and other layout results. It does not by itself:

- destroy `view_tree.prop_pool`;
- clear `view_tree.canonical_prop_arena`;
- start a new style epoch; or
- destroy a canonical style pool.

At a safe boundary before a new pass, `view_tree.scratch_arena` may be reset/cleared after all layout/render temporaries from the prior pass have been released. Retained props and canonical props must never point into this scratch arena.

### 0.4 Canonical prop lifetime

Canonical props normally live for the ViewTree lifetime, across many layout generations and style epochs. Incremental layout may leave clean elements pointing at old canonical entries, so ordinary reflow cannot clear `canonical_prop_arena`. Full ViewTree destruction performs a canonical external-resource teardown pass, destroys both view arenas, then destroys `prop_pool`.

---

## 1. Goals & Non-Goals

**Goals**

1. Share identical fully resolved view-property groups without allowing one element to mutate storage observed by another.
2. Reduce retained-relayout allocator churn by resetting element-owned props in `view_tree.prop_pool` in place.
3. Allocate immutable shared props compactly in `view_tree.canonical_prop_arena` and keep them valid across ordinary reflows.
4. Bound fresh `dom.node.arena` growth under sustained DOM churn after complete liveness proof.
5. Share rule-identical `specified_style` trees within exact `style.canonical.epoch.pool` generations.
6. Measure all six domains without double-counting parent-pool and child-arena bytes.

**Non-Goals**

- Moving or compacting live nodes.
- Skipping selector matching across elements; R6 shares resulting storage, not matching work.
- Cross-document sharing.
- DOM-level undo/redo. Actual Range/Selection/observer/event holders still participate in liveness.
- Free-list trimming beyond the existing arena allocator. A future segmented allocator may return empty slabs to the OS.
- Replacing unrelated StateStore, retained-display-list, font, or renderer allocation domains.

---

## 2. Historical Pre-Implementation Findings

**VF1 — The target six domains refine four current fields.** `DomDocument` currently has one pool plus one child arena; `ViewTree` has one pool plus one child arena. The canonical-prop arena and per-style-epoch pool do not exist yet.

**VF2 — View props are pool-allocated and individually freeable.** Current retained teardown frees groups from `ViewTree::pool`; the next pass commonly allocates the same sizes again. This is churn, not permanent prop leakage.

**VF3 — Current retained ViewTree reset destroys both view allocators.** `ViewTree::reset_retained()` destroys/recreates `pool` and `arena`, which conflicts with owned-prop reset-in-place. R1 must keep `prop_pool` alive and clear only scratch at a proven safe boundary.

**VF4 — Whole prop groups do not follow one inheritance rule.** `InlineProp` mixes inherited fields (`color`, `visibility`, `cursor`) with non-inherited fields (`opacity`, `vertical-align`, `mix-blend-mode`). `FontProp` combines computed declarations, propagated decoration state, derived metrics, a retained `FontHandle`, and payload pointers. “No declaration touched this group” does not prove parent equality.

**VF5 — Candidate groups receive post-cascade writes.** Animation mutates inline/transform values, while `setup_font()` writes metrics and handle state. Sharing requires a COW/finalization boundary.

**VF6 — DOM nodes are not retired today, although the arena already supports recycling.** `dom.node.arena` has built-in `arena_free()` size bins, splitting, coalescing, and bump-back reclamation. R4 should extend this allocator with node retirement metadata/counters rather than build a second generic free-list system.

**VF7 — Detached-node liveness is not centrally observable.** `element_dom_map` is a Lambda backing→DOM map, not a JS-wrapper registry. The wrapper cache strongly roots VMaps, and raw-node holders include mutation records, Range/Selection, observers, event/task queues, live collections, DocState, and reconciliation state.

**VF8 — A stale reference cannot validate by reading recycled object bytes.** The generic node arena may reuse a retired block for another allocation shape. Deferred references therefore validate `(address, expected node id)` against an external document registry, not by dereferencing the possibly repurposed block.

**VF9 — `specified_style` currently owns per-element declaration clones.** Existing `style_tree_clone()` is shallow and has a source-pool lifetime requirement. Canonical style storage needs an explicit epoch pool and a dedicated COW clone contract.

**VF10 — Current allocator snapshots can double-count backing memory.** Arena structs/chunks are allocated by `pool_alloc`, while both the parent pool and child arena report bytes and snapshot totals sum all nodes. R0 must provide physical totals and logical attribution separately.

---

## 3. Decisions

### Part A — View-property sharing

### VR1 — Shareability uses the complete resolved value and write ledger

A group is eligible only when its value is fully resolved from initial values, per-property inheritance, UA/presentational adjustments, and declarations; semantic equality covers every field/payload; every post-commit writer COWs or moves before freeze; and no pass-local used value or scratch lives in the shared object.

Layout-mutated groups remain owned. `InlineProp` is the first candidate. Font sharing is gated independently on VR4 finalization.

### VR2 — Parent equality never aliases parent-owned storage

Resolve the child completely, then compare semantically with the parent. Equal values point at an immutable canonical instance in `view_tree.canonical_prop_arena`. If the parent was owned, promotion copies/transfers the value into canonical storage, changes the parent to shared, and returns the old owned block to `view_tree.prop_pool` before the child points at the canonical entry.

No shared prop instance is element-owned. Optional tree-wide interning extends the same canonical arena/index rather than introducing another owner.

### VR3 — Prop ownership has an explicit state machine

| From | Event | To | Storage action |
|---|---|---|---|
| absent | first write | owned | allocate from defaults in `prop_pool` |
| shared | first write | owned | COW from canonical arena into `prop_pool` |
| owned | equal-value commit | shared | promote to canonical arena, return old pool block |
| owned | retained reset | owned | release payload, reset pool block in place |
| shared | element reset/teardown | absent | clear pointer only; canonical arena remains |
| owned | full teardown | absent | release payload and pool-free block |

Pointer stability applies only to owned→owned retained reset. Explicit high `elmt_flags` bits encode selected prop ownership plus future `specified_style` ownership, with compile-time overlap/capacity assertions.

### VR4 — COW is the only post-commit write path; Font must finalize before freeze

`ensure_G()` always returns mutable element-owned storage. Animation, transition, restyle, HTML/SVG adjustments, layout helpers, and font setup are audited.

Font sharing requires either a pre-promotion finalization that resolves handle/metrics and makes later setup read-only, or a split between shareable computed descriptors and mutable retained font resources. Canonical font resources are recorded in the canonical-arena external-resource ledger and released exactly once before arena destruction.

### VR5 — Canonical props are immutable, arena-owned, and ViewTree-lifetime

Canonical entries, equality metadata, and copied payload graphs allocate from `canonical_prop_arena`. A separate destruction ledger holds external resources that arena destruction alone cannot release. The arena survives ordinary layout generations and retained/incremental reflow. It is capped; cap overflow falls back to owned `prop_pool` storage.

### VR6 — Equality and hashing are field-aware

Hashes accelerate lookup; exact semantic equality decides sharing. Explicit `hash_G`/`equal_G` functions ignore padding and normalize equivalent encodings. Pointer-bearing groups compare immutable payload content or remain excluded. Debug checksums detect mutation but are not equality proof.

### Part B — DOM recycling and relayout storage

### VR7 — Complete liveness infrastructure precedes node reuse

The wrapper cache gains true GC weak slots. A document registry tracks current node generations and retainer counts by `(address, node id, reason)`. Ranges, observers, queues, collections, DocState, reconciliation, and Lambda backing participate. A dedicated detached-root candidate queue is independent of the capped mutation-record array.

`DomNodeRef { address, expected_id }` validation queries the external registry without dereferencing the address. The registry removes the live-node mapping before a block enters the generic arena recycler and installs a fresh mapping only when a node factory reuses/creates that address.

### VR8 — Node retirement reuses the existing `dom.node.arena` recycler

After liveness passes, bottom-up retirement tears down a fully unpinned detached subtree and calls `arena_free(primary_owner_arena, address, exact_size)`. The registry normally records `dom.node.arena`; UI fat Lambda backing records its retained `Input::arena`. `arena_calloc()` already searches free bins and zeroes reused blocks, so `create()` remains the one allocation path. `DomText` records or reliably derives its exact co-allocation size.

No second `DomElement`/`DomText` free-list implementation is introduced. If type-stable lanes later prove necessary, they are added as a reusable arena allocation-class feature, not duplicated per DOM type.

### VR9 — Reuse safety combines registry generations, poison, and assertions

Every node creation receives a fresh monotonic ID and registers it externally. Retirement unregisters the current generation, poisons bytes before `arena_free` (allowing allocator headers to overwrite the freed block), and rejects nonzero pins. Direct tree pointers never outlive structural unlink; deferred holders use pins and/or validated references.

### VR10 — Retained reflow preserves props and separates scratch reset

`VIEW_PROP_TEARDOWN` RESET mode resets owned prop-pool blocks in place and clears shared element pointers without freeing canonical storage. `ViewTree::reset_retained()` no longer destroys `prop_pool` merely to begin another layout generation.

At a safe pass boundary it releases prior scratch users and resets/clears `scratch_arena`. Full ViewTree destruction orders cleanup as: detach element pointers and release owned props → release canonical external resources → destroy canonical arena → destroy scratch arena → destroy prop pool.

### VR11 — `TextRect` recycling is evidence-gated and belongs to `prop_pool`

Because the rpmalloc-backed prop pool already reuses size classes, a Radiant `TextRect` cache is adopted only if release measurements show a material allocator/time win. It does not move into scratch, because rects can remain attached to text views beyond one local scratch scope.

### VR12 — Metrics expose six logical domains without double-counting physical bytes

Two reports are required:

1. **Physical backing:** count top-level pools once; do not add child-arena bytes again.
2. **Logical attribution:** report direct pool allocations and each child arena's committed, active, recyclable/free, waste, hit/miss, and fresh-growth values.

For `dom.node.arena`, interior `arena_free` blocks must be separated from active bytes; final `arena_total_used` alone is insufficient. Node-recycling success means committed/fresh growth plateaus after warm-up, not that already reserved bytes disappear.

For `style.canonical.epoch.pool`, report bytes by epoch and distinguish current vs retired-but-referenced epochs. Style sharing never counts as DOM-arena reduction.

### Part C — Specified-style sharing

### VR13 — Canonical specified styles are owned by versioned style-epoch pools

Matching accumulates an exact ordered cascade recipe containing rule identity/version inputs, specificity, origin/source order, global style epoch, and relevant mode inputs. Hash-table lookup always performs exact recipe equality.

On a miss, one canonical `StyleTree`, immutable declaration/value snapshots, recipe, and entry allocate from the current `style.canonical.epoch.pool`; on a hit, the element binds without per-element declaration construction. Each bound element retains the epoch. Stylesheet/global CSSOM or style-environment change creates a new epoch pool; the old pool remains until its bound-element count reaches zero.

Element-local mutation COWs into `dom.document.pool` and releases the canonical epoch reference. The COW tree owns independent nodes and mutable declaration records. It may borrow a `CssValue` only when the value's immutable lifetime outlives the owned tree; otherwise it copies a snapshot. Existing `style_tree_clone()` is not assumed to meet this contract.

Pseudo-state rematching stays within the current style epoch unless a global style input changed. Semantic verification compares sorted property winners, never tree bytes.

### VR14 — Allocation names and lifecycle paths are singular

Target code names make the six domains visible: `document_pool`, `node_arena`, `prop_pool`, `canonical_prop_arena`, `scratch_arena`, and `StyleEpoch::pool`. Migration may be mechanical and phased, but no ambiguous second owner remains.

Sharing/recycling stay behind `ensure_*`, `create()`, `VIEW_PROP_TEARDOWN`, the canonical store, and one retirement sweep. Node reuse extends the existing arena recycler. Style epoch creation/destruction has one manager. LOC is reported as a guardrail, never traded against correctness infrastructure.

---

## 4. Revised Implementation Outline

| Phase | Content | Allocation-domain result | Gate |
|---|---|---|---|
| R0 | Naming seam, six-domain metrics, canonical probes, churn fixture | Current four fields mapped; physical/logical accounting corrected | report before scope freeze |
| R1 | RESET-in-place and retained-reset lifecycle split; optional `TextRect` cache | `prop_pool` survives; `scratch_arena` resets safely | pointer/payload/scratch lifetime tests |
| R2 | Ownership state machine, `canonical_prop_arena`, resolved-value Inline; optional finalized Font | owned props in pool, shared props in arena | animation/restyle COW; canonical teardown |
| R3 | Weak wrappers, external generation/retainer registry, detached queue; no reuse | liveness metadata in document-owned storage | every holder-class test; zero reuse |
| R4 | Bottom-up retirement through existing arena free/reuse path | `node_arena` growth plateaus | churn/reinsert/ASAN tests |
| R5 | Optional global prop interning | reuses canonical prop arena and index | measured-positive groups only |
| R6 | Exact style epochs, canonical epoch pools, owned COW | canonical styles isolated from document pool | epoch/semantic/CSS/DOM/UI gates |
| R7 | Fresh-process 46-page before/after profile | all six logical domains plus exclusive physical total | identical manifest/sample point/layout semantics |

---

## 5. Risk Register

- **Retained reset still recreates `prop_pool`** → R1 reuse is illusory. Mitigation: explicit allocator-lifetime test and field identity assertion.
- **Scratch arena cleared while a temporary consumer survives** → dangling render/layout data. Mitigation: one safe boundary and retained-display-list audit.
- **Canonical prop stored in scratch** → reflow UAF. Mitigation: allocation-domain assertions and arena ownership tests.
- **Canonical external resources skipped before arena destruction** → handle/resource leak. Mitigation: typed destruction ledger and release-once tests.
- **Incomplete node-retainer ledger** → use-after-reuse. Mitigation: no R4 until R3 closes every raw holder.
- **Generic arena block validated by dereference** → type-confused stale check. Mitigation: external address/ID registry.
- **Second DOM free-list duplicates `arena_free`** → conflicting allocation paths. Mitigation: R4 uses/extends the existing arena allocator only.
- **Style epoch destroyed while an element or COW value borrows it** → dangling tree/value. Mitigation: element epoch refs and owned snapshots for insufficient lifetimes.
- **Pool + arena metrics summed as physical bytes** → double-counted memory claims. Mitigation: physical and logical reports remain separate.
- **Custom `TextRect` cache duplicates rpmalloc without benefit** → complexity without savings. Mitigation: release-build adoption gate.

---

## 6. Resolved Questions

- **OQ-A:** no DOM edit-history retainer exists; actual native/JS holders still participate in VR7.
- **OQ-B:** parent equality is exploited, but shared prop storage is canonical-arena-owned, never parent-owned.
- **OQ-C:** `specified_style` sharing remains in scope as R6 and uses style-epoch pools.
- **OQ-D:** allocator trimming beyond existing arena behavior is deferred.
- **OQ-E:** ordinary reflow advances `layout_generation`, not `style_epoch`; retained props/canonical styles survive when their inputs remain valid.
- **OQ-F:** the campaign uses the six allocation domains defined in §0.1; other subsystem allocators are unchanged.

---

## 7. Implemented Outcome

| Phase | Outcome |
|---|---|
| R0 | Adopted: singular allocator names, exclusive physical/logical telemetry, probes, and churn fixtures. |
| R1 | Adopted: `prop_pool` survives retained reset, owned props reset in place, and scoped scratch resets at each `layout_generation`. A custom `TextRect` cache was not adopted. |
| R2 | Adopted for fully resolved `InlineProp`, including immutable arena ownership, parent promotion, exact equality, COW, a cap, and teardown. Font sharing was not adopted because mutable handle/metric state did not clear its evidence gate. |
| R3 | Adopted: weak GC slots, wrapper weak cache, external node generation/pin registry, validated references, and a detached-root candidate queue. |
| R4 | Adopted: bottom-up retirement through the existing arena recycler, including actual-owner tracking for UI fat Lambda nodes. Churn growth plateaus after warm-up. |
| R5 | Adopted only for exact tree-wide `InlineProp` interning; no other property group justified another canonical contract. |
| R6 | Adopted: exact recipe lookup, immutable epoch snapshots, element binding counts, global epoch invalidation, local owned COW, and wholesale old-pool release. |
| R7 | Completed: 46/46 fresh-process post-layout outputs are byte-semantically equal to baseline after timestamp normalization. Physical live memory is −42,176 B; logical DOM active memory is −18.62%; node-arena active memory is −22.20%; logical view-prop active memory is −3.72%. |

The R6 gate was measured directly. Disabling production style epochs raised the
46-page physical-live sum to 37,028,896 B, 3,436,640 B above the adopted result.
A duplicate-only-within-one-cascade prototype was also 245,168 B worse because
it missed reuse across later cascade batches. Therefore the implemented
all-exact-recipe epoch index is the measured-positive policy. Full tables and
reproducibility artifacts are in `Radiant_Impl_View_Reuse.md` §9.
