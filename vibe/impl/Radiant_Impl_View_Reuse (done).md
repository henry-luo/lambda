# Radiant View Prop Sharing & Storage Reuse — Implementation Plan

**Date:** 2026-07-18
**Baseline commit:** `a267387db`
**Parent design:** `vibe/radiant/Radiant_Design_View_Reuse.md` — allocation domains §0, decisions VR1–VR14
**Related:** completed DOM/view structure campaign, `Radiant_Design_Robustness.md` T7, and the DOM/view memory profile

**Campaign status: complete.** R0–R7 are implemented and release-profiled. Adopted: retained reset/scratch separation, canonical and tree-wide `InlineProp` reuse, external DOM liveness with existing-arena retirement, and canonical style epochs. Evidence-gated Font sharing and a custom `TextRect` cache were not adopted. Final validation and profile evidence are recorded in §9.

---

## 0. Ground Rules and Allocation Contract

### 0.1 Ground rules

1. **Per-PR gates:** `make build`, `make test-radiant-baseline`, `make layout suite=baseline`, and `make lint`. R3/R4 additionally run JS DOM, UI automation, editor, and ASAN churn tests. R6 additionally runs CSS and DOM-CRUD suites.
2. **Release-only performance evidence:** allocator/timing decisions use `make release`.
3. **One lifecycle path:** extend `create()`, `ensure_*`, `VIEW_PROP_TEARDOWN`, the existing arena recycler, one canonical-prop store, one style-epoch manager, and one retirement sweep. No shadow paths.
4. **Fail toward owned/pinned:** equality uncertainty produces owned prop/style storage; liveness uncertainty leaves a subtree unretired.
5. **No element-owned shared storage:** canonical props belong to `canonical_prop_arena`; canonical styles belong to a `StyleEpoch::pool`.
6. **Hash is not proof:** exact semantic equality follows every hash hit.
7. **No node reuse before R3 closes liveness.** R3 changes references/metadata only; R4 enables `arena_free` retirement.
8. **Root-cause comments:** allocation-domain crossings, COW, epoch changes, pin/unpin, retirement rejection, and teardown order receive invariant comments.
9. **Sharing and recycling stay in separate PRs.** A bisect must implicate one mechanism.
10. **LOC is reported, not gamed:** run `verify_loc_reduction.sh --ref a267387db` at phase boundaries.

### 0.2 Six-domain implementation contract

| Domain | Target owner/member | Allocation API | Must survive |
|---|---|---|---|
| `dom.document.pool` | `DomDocument::document_pool` | `pool_*` | all layout generations; document lifetime |
| `dom.node.arena` | `DomDocument::node_arena` | `arena_*` | all layout generations; live-node lifetime |
| `view_tree.prop_pool` | `ViewTree::prop_pool` | `pool_*` | retained/incremental reflow |
| `view_tree.canonical_prop_arena` | `ViewTree::canonical_prop_arena` | `arena_*` | retained/incremental reflow and ordinary style epochs |
| `view_tree.scratch_arena` | `ViewTree::scratch_arena` | `arena_*`/scratch wrapper | only the active safe layout/render scope |
| `style.canonical.epoch.pool` | `StyleEpoch::pool` | `pool_*` | all reflows until epoch refcount reaches zero |

UI fat Lambda elements are the compatibility exception to the usual physical
`dom.node.arena` owner: their primary block remains in the retained
`Input::arena`. The external lifecycle registry records the actual owner arena
and returns the block there. This is not an additional logical category; the
six-domain `page` profile contains ordinary nodes in `dom.node.arena`.

The target destruction order is:

```text
ViewTree full destroy:
  detach/reset element prop pointers
  release owned prop resources
  destroy canonical indexes and release canonical external-resource ledger
  destroy canonical_prop_arena
  assert/release scratch scopes; destroy scratch_arena
  destroy prop_pool

DomDocument full destroy:
  quiesce layout/events and invalidate wrappers
  destroy ViewTree/StateStore dependents, detaching view props
  release node-held owned styles and canonical style bindings
  destroy style-epoch manager/pools
  destroy node_arena
  destroy document_pool
```

Ordinary reflow increments `layout_generation`; it does not destroy `prop_pool`, `canonical_prop_arena`, or any still-valid style epoch. A global stylesheet/CSSOM/media-matching change starts `style_epoch + 1`; a pure reflow does not.

### 0.3 Starting measurements

| Metric (46-page corpus, release, post-layout) | Baseline |
|---|---|
| DOM node-arena used, sum | 3,193,344 B |
| Current ViewTree arena used, sum | 490,544 B |
| Current document + ViewTree pools cumulative allocations | 28,325,328 B |
| Median combined arena per page | 28,192 B |
| DOM churn committed/fresh growth | linear |

These historical totals are not six-domain-exclusive. R0 replaces them with physical backing and logical attribution reports.

---

## R0 — Allocation Seams, Canonical Probes, Metrics, and Fixtures

### R0.1 — Allocation-domain naming seam  **[VR14]**

- In one mechanical PR, rename or provide a single migration seam for:
  - `DomDocument::pool` → `document_pool`;
  - `DomDocument::arena` → `node_arena`;
  - `ViewTree::pool` → `prop_pool`; and
  - `ViewTree::arena` → `scratch_arena`.
- Update allocator labels to the six names from §0.2. Do not create `canonical_prop_arena` or style epoch pools in this naming-only PR.
- Audit constructor/destructor order and every raw field use with `rg`; no compatibility duplicate field remains.
- Add comments on each field stating its contents and reset boundary.

### R0.2 — Exclusive physical/logical memory accounting  **[VR12]**

- Extend arena stats to report exact backing bytes requested from the parent pool, active bump bytes, interior recyclable/free bytes, chunk-header overhead, allocation reuse hits/misses, and fresh chunk growth.
- Physical totals count top-level pools once. Child arena samples remain visible but are not added again to physical totals.
- Logical direct-pool attribution excludes exact child-allocator backing. Assert rather than silently clamp if child backing exceeds the parent sample.
- Domain-specific cumulative counters count prop/style/node operations directly; do not infer all logical work from the backing pool's aggregate allocation count.
- Update `--mem-dump` schema/output with `physical_total` and `logical_domains`; document that they answer different questions.

### R0.3 — Prop equality/duplicate probe  **[VR1, VR6, VR12]**

- Add temporary post-layout traversal using explicit field hashing/equality. Zero scratch; do not use raw struct bytes as the semantic contract.
- Pointer groups compare immutable payload content or report “not safely comparable.”
- Report per group: instances, exact distinct values, fully-resolved equal-to-parent count, non-parent duplicates, owned struct/payload bytes, theoretical canonical-arena bytes, and compare time.
- Record every post-commit writer and whether it already crosses `ensure_*`/finalization.

### R0.4 — Exact cascade-recipe probe  **[VR13]**

- Collect ordered diagnostic recipe entries containing rule identity/version input, specificity, origin/source order, style epoch, and relevant mode inputs.
- Mark inline/presentational/direct mutation element-specific until represented canonically.
- Group by hash then exact recipe equality. Verify sampled equal recipes by sorted semantic `StyleTree` comparison, not bytes.
- Measure current tree header, AVL node, per-element declaration clone, and value-payload bytes with tagged counters.
- Predict canonical epoch-pool bytes, recipe-table bytes, and number of concurrently live epochs for scripted stylesheet/media changes.

### R0.5 — DOM arena allocation/ownership probe  **[VR7, VR8]**

- Inventory every `node_arena` allocation and classify it:
  1. primary node/co-allocation block with exact size;
  2. node-owned auxiliary allocation that retirement must return with exact size; or
  3. intentional document-lifetime allocation.
- Record missing owner/size metadata as an R4 blocker.
- Confirm current `arena_alloc/calloc` free-bin reuse, split, coalesce, and bump-back behavior under node/text sizes. This report decides whether any reusable allocation-class extension is needed; a second DOM free-list is not the default.

### R0.6 — Churn fixture

- Add deterministic create/append/remove/reinsert/subtree replacement/text/class/style churn for at least 10k cycles with reconcile and reflow.
- Variants retain nodes through wrappers, Range/Selection, observers, queues, live collections, DocState, and reconciliation/Lambda backing.
- Include more than `DOM_JS_MUTATION_RECORD_CAP` removals between reconciles.
- R0 records non-gating growth; R4 adds plateau assertions.

**R0 exit:** naming build/tests clean; six-domain report recorded in §9; R2/R5/R6 adoption scope frozen; node-arena owner/size ledger created.

---

## R1 — Retained Reflow and Scratch-Lifetime Split

### R1.1 — Preserve `prop_pool` across retained reset  **[VR10]**

- Change `ViewTree::reset_retained()` so it no longer destroys/recreates `prop_pool` for an ordinary retained reflow.
- Add `VIEW_TEARDOWN_RESET_IN_PLACE` to the existing table. At R1 all present prop groups are owned: release external resources/payloads once, copy typed defaults into the same block, retain pointer ownership.
- Keep FREE behavior for full ViewTree destruction. Union roles clear consistently; `layout_cache` uses explicit generation invalidation.
- Add typed size/default table metadata and compile-time size assertions. No parallel per-group reset functions.

### R1.2 — Safe `scratch_arena` reset

- Inventory every layout/render scratch, `PaintList`, display-list, and temporary consumer backed by `scratch_arena`.
- Add an active-scope counter/assertion. Reset/clear scratch only after the prior layout/render temporary scopes are closed and before the next layout generation begins.
- Retained display-list/cache storage must remain in its existing dedicated allocator; no retained pointer may target `scratch_arena`.
- Select `arena_reset` versus `arena_clear` from release high-water measurements: reset retains chunks for speed; clear returns excess chunks to `prop_pool`.

### R1.3 — `TextRect` decision  **[VR11]**

- Keep `TextRect` in `prop_pool`, not scratch.
- Compare rpmalloc reuse with a local cache prototype under repeated relayout. Adopt one shared push-chain/pop-zero helper only for a material release-build allocator/time win; otherwise remove the prototype and record “not adopted.”

### R1.4 — Acceptance

- Tests: owned block/font/inline pointer stability, default restoration, payload/handle release once, union/cache reset, and scratch pointer rejection after generation advance.
- Assert `prop_pool` identity is unchanged across retained reflow while `layout_generation` increments.
- Record direct prop-pool allocations, scratch committed/active/high-water bytes, reset/clear time, and release timing.

**R1 exit:** owned props genuinely survive retained reset; scratch has a proven safe clearing boundary.

---

## R2 — Canonical Prop Arena and Immutable Sharing

### R2.1 — Ownership bits and transition helpers  **[VR3]**

- Assign named high `elmt_flags` bits for selected shareable groups and reserve the R6 `specified_style` ownership bit. Assert no overlap with bits 0–16 and no overflow of the 15-bit reserve.
- Add one set of helpers used by `ensure_*`, resolver commit, and teardown: state query, mark owned/shared, COW, promote, return-owned, and clear-shared.
- Before sharing, every shareable `ensure_*` allocation sets owned state; teardown frees only owned prop-pool blocks. This PR is semantically neutral.
- Unit-test every VR3 transition, especially owned→shared and shared→owned.

### R2.2 — Create `canonical_prop_arena` and teardown ledger  **[VR5]**

- Create `canonical_prop_arena` from `prop_pool`; do not reuse `scratch_arena`.
- Allocate canonical structs and copied immutable payload graphs with `arena_calloc/alloc`.
- Keep resizable indexes and the typed external-resource destruction ledger in `prop_pool`, because they must be traversed/destroyed before the canonical arena disappears.
- Add a canonical-byte cap. Arena cap overflow falls back to owned prop-pool storage.
- Full teardown order: destroy index → release each ledger resource exactly once → destroy canonical arena. Ordinary layout generation does none of these.

### R2.3 — Fully resolved `InlineProp` parent-value sharing  **[VR1, VR2, VR6]**

- Define explicit `inline_prop_hash/equal` over every field.
- At resolver commit, compare the child's normalized full value with the parent:
  - equal canonical parent → return child temporary owned block, bind canonical;
  - equal owned parent → allocate canonical copy, change parent to shared, return both superseded owned storage as applicable, bind child;
  - unequal/uncertain → keep child owned.
- Group-touched flags remain diagnostics only.

### R2.4 — COW and post-commit writer audit  **[VR4]**

- `ensure_inline()` COWs from canonical arena into a mutable `prop_pool` block.
- Audit animation/transition, hover/focus restyle, HTML/SVG defaults, table anonymous wrappers, layout helpers, and tests.
- A writer with only `InlineProp*` must move before freeze or receive the element/ensure gate.
- Count COWs by call site; unexpected hot COW blocks expansion.

### R2.5 — Optional finalized Font sharing

- Add `font_prop_finalize()` before a Font can become canonical. Make later `setup_font()` read-only for finalized canonical values or split mutable resource state.
- Define semantic family/shadow/descriptor/metric/resource equality.
- Canonical Font handle/payload ownership registers in the canonical teardown ledger; discarded owned candidates release their own resources.
- Font-context invalidation COWs/re-resolves without mutating canonical entries. Enable only if R0 predicts sufficient savings.

### R2.6 — Acceptance

- Test mixed inheritance (`opacity`, `vertical-align`, `color`, `visibility`), parent/child animation, independent restyle, promotion, COW, retained reset, arena cap fallback, and full canonical teardown.
- If Font enabled: finalize/read-only setup, invalidation, handle release once, and ASAN subtree tests.
- Assert canonical addresses belong to `canonical_prop_arena`, owned addresses belong directly to `prop_pool`, and neither belongs to `scratch_arena`.
- Record prop-pool live/cumulative bytes, canonical arena committed/active/waste, promotions, COWs, and release time.

**R2 exit:** all shared props are immutable canonical-arena objects and survive ordinary layout generations.

---

## R3 — Node Liveness Infrastructure (No Recycling Yet)

### R3.1 — Complete raw-node holder ledger  **[VR7]**

Close a checked ledger containing owner, field, acquisition, release, pin reason, and retirement behavior for wrappers, Range/Selection, observers/transient roots, mutation/reconcile records, event/task queues, live collections, DocState, Lambda backing/reconcile maps, and pseudo/generated ownership.

New raw/deferred `DomNode*` fields added during the campaign join the ledger before merge.

### R3.2 — GC weak slots and wrapper cache conversion

- Add weak-slot/weak-handle support plus post-collection cleared-slot processing to `lib/gc` and the Jube host API; strong-root registration is insufficient.
- Convert wrapper identity entries to weak slots while preserving identity while live.
- Weak clear removes the cache index and releases the wrapper pin without dereferencing recycled node bytes. Document teardown invalidates surviving wrappers.
- “Ever wrapped means never retire” is not accepted.

### R3.3 — External generation and retainer registry

- Allocate a document-owned registry from `document_pool`, keyed by node address with current ID, node type, exact primary allocation size, state, and per-reason pin counts.
- `DomNodeRef` contains address + expected ID and validates only through this registry. It never reads the target bytes before validation.
- Node creation registers a fresh ID/size/type. Structural retirement first verifies pins/current ID, then removes the live mapping before `arena_free`.
- Pin underflow, wrong-ID unpin, duplicate live registration, or retirement with pins is a `RADIANT_CHECK` failure.

### R3.4 — Detached-root queue

- Add one `document_pool`-owned candidate set/list of address + expected ID. Coalesce descendants beneath the highest detached root.
- Reinsertion cancels the root/ancestor candidate before linkage becomes visible.
- Candidate tracking is independent of mutation records. Last-pin release schedules eligibility for a later safe sweep; it never retires inline.

### R3.5 — Acceptance

- Run R0 churn variants and assert pins, validated refs, candidate state, wrapper weak behavior, rewrap identity, reinsertion cancellation, and >64 mutation overflow.
- Assert no call to `arena_free(node_arena, ...)` occurs for nodes and no recycled node allocation occurs.
- Run ASAN and negative stale-ID tests.

**R3 exit:** external registry and all holder integrations pass; zero node reuse.

---

## R4 — DOM Retirement Through the Existing Node Arena Recycler

### R4.1 — Exact node and auxiliary allocation ownership  **[VR8]**

- Fixed primary sizes use `sizeof(DomElement)`/`sizeof(DomComment)`; co-allocated `DomText` registers its exact aligned allocation size at creation.
- For every R0 class-2 node-owned auxiliary arena allocation, either:
  - register address + exact size under the node for bottom-up return; or
  - move truly independently mutable storage to `document_pool` with explicit ownership.
- Unknown owner/size remains an R4 blocker; do not guess size or leave churn payloads abandoned.

### R4.2 — Unified bottom-up `retire_node()`

- Replace `dom_element_destroy` semantics with one typed retirement visitor.
- Before retirement assert: detached candidate ID current, not document/pseudo root, entire subtree unpinned, no Lambda/reconcile reference, and side-state detach complete.
- If any descendant fails, retain the whole subtree and record its rejection reason.
- Retire bottom-up: release owned view props and style binding, release retained resources, return registered auxiliary blocks with `arena_free`, poison primary bytes, unregister the live generation, then `arena_free(registered_primary_owner_arena, primary, exact_size)`. The owner is normally `node_arena`; UI fat Lambda backing retains its `Input::arena` owner.

### R4.3 — Existing arena reuse path

- Keep node factories on `arena_calloc`; it already searches free bins and zeroes reused storage.
- Register a fresh monotonic node ID after allocation. Generic cross-type block reuse is safe because stale validation consults the external registry.
- Add arena hit/miss/split/coalesce/fresh-chunk counters needed by R0.2. Do not add document-level type free lists.
- If measurements require type-stable lanes, implement a reusable arena allocation-class facility and re-probe; do not duplicate per-node free-list code.

### R4.4 — Safe sweep checkpoints and acceptance

- One `dom_retire_sweep()` runs after reconcile, GC weak processing, and event/observer/task delivery checkpoints where pins can reach zero. Never retire inline in removal/finalization.
- Convert R0 churn to gating assertions: node-arena committed/fresh growth plateaus after warm-up for unheld and hold/release variants.
- Test pinned descendant, reinsertion, wrapper/Range/observer/event release, mutation overflow, variable text sizes, auxiliary payload return, generic cross-type reuse, fresh IDs, and stale registry rejection.
- Run full Radiant/layout/JS DOM/UI/editor/ASAN and release timing gates.

**R4 exit:** bounded `dom.node.arena` growth without a second free-list mechanism.

---

## R5 — Optional Tree-Wide Prop Interning

### R5.1 — Canonical index  **[VR5, VR6]**

- Adopt only groups whose R0 non-parent savings exceed canonical arena, prop-pool index, hash/equality, and payload costs.
- Reuse R2 canonical entries/arena and ownership transitions. The resizable capped index lives in `prop_pool`; canonical values live in `canonical_prop_arena`.
- Resolve normalized value, hash, exact-compare, then share or stay owned. Start with pointer-free groups.
- Table/canonical cap fallback remains owned; style-thrash must not append a canonical value per frame without bound.

### R5.2 — Acceptance

- Realized live-byte savings within 20% of R0 prediction or discrepancy explained before retention.
- Record lookup time, collisions/exact comparisons, prop-pool index bytes, canonical arena bytes, COW, and cap fallback.
- Phase may be recorded as skipped.

---

## R6 — Versioned Canonical Style Epoch Pools

### R6.1 — `StyleEpoch` manager and triggers  **[VR13]**

- Add a `document_pool`-owned manager with current/retired epoch list. Each `StyleEpoch` has monotonic ID, independent `Pool*`, exact recipe table, bound-element refcount, and current/retired state.
- Create the epoch pool directly under the document memory context with the `style.canonical.epoch.pool` label; it is not the DOM node arena and is not backed by `document_pool`.
- Start a new global epoch on stylesheet add/remove/reparse, global CSSOM rule/declaration mutation, or media/style-environment change that affects matching.
- Do not start a new global epoch for pure reflow, element-local inline mutation, or element-local pseudo-state rematching.

### R6.2 — Production exact cascade recipe

- Accumulate compact ordered recipe entries before constructing a shareable tree. Include epoch, rule identity/version inputs, specificity, origin/source order, and mode inputs.
- Hash indexes; exact length/entry equality accepts sharing.
- Temporary recipe construction may use a scoped scratch allocator, but stored recipes copy into the current epoch pool before scratch reset.

### R6.3 — Canonical construction and element binding

- On recipe hit, bind the element directly without per-element declaration clones.
- On miss, allocate canonical `StyleTree`, immutable declaration/value snapshots, stored recipe, and `StyleCanonicalEntry` from `StyleEpoch::pool`; existing pool-based AVL APIs remain usable.
- Store explicit absent/shared/owned style state using the reserved ownership bit. A canonical tree/header points back to its `StyleCanonicalEntry`/epoch so unbind decrements entry diagnostics and epoch refcount exactly once without adding an element-sized epoch pointer.
- A retired epoch pool is destroyed wholesale only when bound-element refcount reaches zero. Current zero-ref epoch remains until superseded/document teardown.

### R6.4 — Owned mutation/COW into `document_pool`

- Add a purpose-specific clone API. Existing `style_tree_clone()` remains documented shallow and is not called “deep.”
- COW produces independent tree nodes and mutable declaration records in `document_pool`.
- Borrow `CssValue` only when its immutable lifetime provably exceeds the owned tree; otherwise snapshot it into document-owned storage. A COW tree may not silently retain a destroyable epoch pool.
- Only after successful clone does the element unbind/decrement its style epoch and apply inline/presentational/CSSOM mutation.

### R6.5 — Verification and acceptance

- Debug sampling compares sorted property IDs and winning declaration semantics; never tree bytes.
- Test forced hash collision, same rule pointer/new epoch, old/new epoch coexistence, incremental rebinding and wholesale old-pool destruction, media change, pseudo-state within epoch, inline COW, property removal, and COW value lifetime.
- Run CSS, DOM-CRUD, Radiant, layout, UI automation, and mutation suites.
- Record pool bytes per epoch, current vs retired referenced epochs, recipe/index bytes, hits, COWs, epoch lifetime, and release timing. Report no DOM node-arena reduction.

**R6 exit:** canonical styles are isolated in exact style-epoch pools; owned mutations are document-pool-independent of epoch destruction.

---

## R7 — `page` Suite Six-Domain Memory Profile

### R7.1 — Capture comparable before/after snapshots  **[VR12]**

- Profile all 46 HTML fixtures in `test/layout/data/page/` with release builds. Run each page in a fresh process and sample immediately after `layout_html_doc()` completes, before rendering, so renderer surfaces, JIT memory, and unrelated framework RSS remain outside the result.
- Capture the **before** snapshot at baseline commit `a267387db` with an instrumentation-only version of the R0 metrics hook, before R1–R6 behavior changes. Capture the **after** snapshot at the completed campaign revision with the identical page manifest, sampling point, domain definitions, and aggregation script.
- Keep the four pre-campaign fields mapped to their §0.2 target domains. Record `canonical_prop_arena` and `style.canonical.epoch.pool` as “not present (0 B)” before the change rather than folding their future bytes into a backing pool.
- For every page, record all six logical domains:
  - direct live and cumulative bytes for `dom.document.pool` and `view_tree.prop_pool`;
  - committed, active, recyclable/free, waste, and high-water bytes for `dom.node.arena`, `view_tree.canonical_prop_arena`, and `view_tree.scratch_arena`; and
  - current plus retired-but-referenced bytes, recipes, canonical trees/declarations, and bound-element refs for `style.canonical.epoch.pool`.
- Also record the exclusive physical total, counting each independent top-level pool once. Never add a child arena's committed bytes to its backing pool a second time.
- Store raw per-page results in `temp/view_reuse_page_memory_comparison.csv` and the aggregate report in `temp/view_reuse_page_memory_summary.json`. The committed implementation plan receives the final result table so evidence does not depend on temporary artifacts.

### R7.2 — Report deltas and validate comparability

- Report before, after, byte delta, percentage delta, and improved/unchanged/regressed page counts for each domain and for the exclusive physical total. Include sum, median, and p95 per-page values; report both active/live and committed/reserved memory because arena reuse may retain chunks while reducing fresh growth.
- Add focused composites for DOM storage (`document_pool` direct + `node_arena` logical active), view-prop storage (`prop_pool` direct + both view arenas), and canonical-style storage. Label these as logical attribution, not additional physical totals.
- Run `make layout suite=page` at both revisions and require the same 46-page manifest with no missing/crashing page. Record any layout-result difference next to its memory delta; do not claim savings from a page that failed or skipped post-layout capture.
- Attribute changes by completed phase where possible: R1 scratch/prop churn, R2/R5 canonical view props, R4 node-arena recycling, and R6 style-epoch pools. Explain regressions and retained-capacity effects instead of reporting only the aggregate net.

**R7 exit:** reproducible release-build before/after evidence is documented for every page and all six domains, with exclusive physical totals and no missing page result.

---

## 8. Test Matrix

| Test | Phase | Invariant |
|---|---|---|
| allocator naming/destruction order | R0 | six domains have one owner and correct backing |
| physical vs logical memory snapshot | R0 | child arenas are not double-counted |
| retained prop/scratch reset | R1 | prop pool survives; scratch resets safely |
| owned reset/default/payload lifetime | R1 | reset releases once and preserves owned block |
| mixed inheritance/full-value comparison | R2 | no group-touched correctness proxy |
| canonical arena ownership and teardown | R2 | shared props never enter pool/scratch owner paths |
| parent/child animation/restyle COW | R2 | canonical bytes remain immutable |
| Font finalize/invalidate/handle release | R2 optional | canonical resource ledger is correct |
| weak wrapper identity/death/rewrap | R3 | wrapper cache does not pin dead wrappers |
| Range/observer/event/collection pins | R3 | every holder blocks retirement until release |
| external stale-ID validation | R3/R4 | validation never dereferences repurposed storage |
| >64 detach records and reinsertion | R3/R4 | candidate queue is independent and cancellable |
| primary/auxiliary node block return | R4 | all recyclable node-arena bytes have exact ownership |
| generic arena cross-type reuse | R4 | registry generations preserve safety |
| intern collision/cap/style thrash | R5 optional | equality and growth bounds hold |
| style epoch triggers/coexistence/destruction | R6 | epoch pools follow global style inputs, not reflows |
| style recipe collision/pseudo/source order | R6 | recipe equality is sufficient |
| owned style COW/value lifetime | R6 | epoch destruction cannot dangle owned styles |
| 46-page six-domain before/after profile | R7 | identical post-layout sampling and no physical double-counting |

---

## 9. Six-Domain Metrics Tracking

Physical totals count independent top-level pools once. Logical rows below are attribution and must not be summed with their backing pool as additional physical memory.

### 9.1 Completed phase/adoption matrix

| Phase | Final result |
|---|---|
| R0 | Implemented singular allocator names and labels, direct-vs-backing accounting, arena recycler counters, canonical probes, and deterministic churn fixtures. |
| R1 | Adopted retained `prop_pool`, table-driven owned reset-in-place, `layout_generation`, scope-checked scratch reset, and allocation-domain assertions. The `TextRect` cache prototype was not adopted because the existing pool allocator already supplied the needed reuse. |
| R2 | Adopted immutable canonical `InlineProp` values, exact field hash/equality, parent promotion, ownership flags, COW, cap fallback, and ordered teardown. Font sharing was not adopted: mutable handle/metrics state did not justify another resource-sharing contract. |
| R3 | Implemented GC weak slots/Jube weak wrappers, external address+generation validation, per-reason pins, holder integration, and detached-root candidates independent of mutation-record capacity. No recycling was enabled until this ledger passed. |
| R4 | Adopted bottom-up retirement through the existing arena bins. Nodes register exact size and actual primary owner; UI fat Lambda elements return to `Input::arena`. Unheld, held-then-released, overflow, and variable-size churn all plateau after warm-up. |
| R5 | Adopted exact tree-wide interning only for `InlineProp`, reusing the R2 arena/index. Other groups were left owned. |
| R6 | Adopted exact style recipes, independent epoch pools, immutable deep snapshots, shared/owned state, global invalidation, local COW, and refcounted old-pool destruction. |
| R7 | Completed the fresh-process release A/B for all 46 pages and all six domains; 46/46 normalized layout JSON hashes are identical. |

### 9.2 Reproducibility contract and artifacts

- Baseline source: exact commit `a267387dbf373a1a142dd30fd5bd9bf193f0c739` plus the instrumentation-only R0 profile hook, built as `temp/view-reuse-baseline/lambda.exe`.
- After source: completed campaign release build, `lambda.exe`.
- Harness: `python3 utils/profile_view_reuse_memory.py --capture after --after-exe ./lambda.exe`; use `--capture both --baseline-exe temp/view-reuse-baseline/lambda.exe` to recapture both sides.
- Corpus/sample: the same sorted 46 files under `test/layout/data/page/`; one fresh release process per page; 1200×800 viewport; sample at `post_layout_pre_output`.
- Raw profiles/layout hashes/manifests: `temp/view_reuse_page_memory/`.
- Per-page comparison: `temp/view_reuse_page_memory_comparison.csv`.
- Aggregate machine-readable result: `temp/view_reuse_page_memory_summary.json`.

The harness rejects a missing domain, attribution error, manifest mismatch, wrong
sample point, failed page, or changed normalized layout hash. Wall-clock capture
timestamps are removed before hashing; layout state is otherwise unchanged.

### 9.3 Final 46-page memory result

Page counts are `improved / unchanged / regressed`, where lower memory is
“improved.” Medians and p95 values are per-page bytes (`before → after`).

| Metric | Before sum | After sum | Delta | Delta % | Pages I/U/R | Median B→A | p95 B→A |
|---|---:|---:|---:|---:|---:|---:|---:|
| Exclusive physical live | 33,634,432 | 33,592,256 | −42,176 | −0.125% | 19/0/27 | 312,040→294,256 | 2,045,872→2,204,560 |
| Exclusive physical reserved | 33,634,432 | 34,801,968 | +1,167,536 | +3.471% | 19/0/27 | 312,040→297,376 | 2,045,872→2,250,384 |
| Logical DOM active composite | 20,878,880 | 16,991,440 | −3,887,440 | −18.619% | 30/0/16 | 184,096→77,488 | 1,750,784→1,482,192 |
| `dom.document.pool` direct live | 17,570,704 | 14,417,648 | −3,153,056 | −17.945% | 29/0/17 | 143,800→51,688 | 1,544,864→1,349,936 |
| `dom.node.arena` active | 3,308,176 | 2,573,792 | −734,384 | −22.199% | 46/0/0 | 27,168→21,512 | 180,112→144,160 |
| `dom.node.arena` committed | 4,145,152 | 3,276,800 | −868,352 | −20.949% | 18/28/0 | 28,672→28,672 | 192,512→192,512 |
| Logical view-prop active composite | 8,399,600 | 8,087,216 | −312,384 | −3.719% | 30/12/4 | 68,552→67,856 | 465,296→443,248 |
| `view_tree.prop_pool` direct live | 8,399,600 | 8,082,032 | −317,568 | −3.781% | 30/12/4 | 68,552→67,792 | 465,296→443,056 |
| `view_tree.canonical_prop_arena` active | 0 | 5,184 | +5,184 | n/a | 0/13/33 | 0→96 | 0→320 |
| `view_tree.canonical_prop_arena` committed | 0 | 188,416 | +188,416 | n/a | 0/0/46 | 0→4,096 | 0→4,096 |
| `view_tree.scratch_arena` active | 0 | 0 | 0 | 0% | 0/46/0 | 0→0 | 0→0 |
| `view_tree.scratch_arena` committed | 974,944 | 1,245,280 | +270,336 | +27.728% | 0/32/14 | 4,096→8,192 | 159,840→159,840 |
| `style.canonical.epoch.pool` live/reserved | 0 | 3,922,784 | +3,922,784 | n/a | 0/0/46 | 0→41,872 | 0→272,624 |

The logical composites are attribution views and are not added to physical
totals. Physical live is almost flat because the large owned DOM/style
reduction is intentionally exchanged for independently owned canonical style
pools and lifecycle metadata. Physical reserved increases because each page
retains minimum canonical/scratch arena chunks; active canonical view-prop data
is only 5,184 B while its 46 minimum chunks commit 188,416 B. Scratch finishes
with zero active bytes on every page, but retained chunks add 270,336 B.

Cumulative allocation traffic is not a live-memory total:

| Counter | Before sum | After sum | Delta |
|---|---:|---:|---:|
| `dom.document.pool` cumulative | 21,567,608 | 21,882,775 | +315,167 (+1.461%) |
| `view_tree.prop_pool` cumulative | 9,202,380 | 9,715,866 | +513,486 (+5.580%) |

### 9.4 R5/R6 adoption evidence

Across the 46 final pages, canonical Inline reuse produced 81 values, 803 index
lookups, 722 hits, 5,396 parent promotions, 218 COWs, 19,488 B of index storage,
zero collisions, and zero cap fallbacks. Exact equality remained authoritative.

Canonical styles produced 1,194 exact recipes/trees and 12,935 bound element
references. There were 14,082 lookups, 12,888 hits, 1,194 misses, 12,888 exact
comparisons, zero collisions, and 1,147 owned COWs. Static page capture creates
one current epoch per document, so it has no retired epoch; coexistence and
last-binding wholesale release are covered by focused tests.

The R6 stop condition was evaluated with two additional 46-page release runs;
all three variants retained 46/46 identical normalized layouts:

| Style policy | Physical live sum | Delta vs baseline | Document direct live | Style-epoch live | Decision |
|---|---:|---:|---:|---:|---|
| R6 disabled | 37,028,896 | +3,394,464 (+10.092%) | 21,777,072 | 0 | reject; per-element cascade records dominate |
| Canonicalize duplicate recipes seen in the current cascade only | 33,837,424 | +202,992 (+0.604%) | 17,160,496 | 1,425,104 | reject; misses reuse in later cascade batches |
| Exact epoch index for every recipe (adopted) | 33,592,256 | −42,176 (−0.125%) | 14,417,648 | 3,922,784 | adopt; saves 3,436,640 B vs disabled and 245,168 B vs batch-local duplicate-only |

### 9.5 Validation record

| Gate | Result |
|---|---|
| Release build | `make release` passed; final `lambda.exe` is the release build. |
| Focused campaign suite | `test_view_reuse_gtest.exe`: 19/19 passed, covering exact prop/style collisions, cap/COW, weak/stale liveness, arena retirement, overflow, variable sizes, UI owner arena, epoch coexistence, invalidation, and value lifetime. |
| Allocator/GC | Arena 91/91; GC 47/47; all focused memory-factory/pool/scratch suites passed. |
| CSS/DOM/layout components | DOM CRUD 63/63; WPT CSS syntax 38 passed + 6 intentional skips; layout custom 11/11; CSS animation 18/18; StateStore 4/4. |
| ASAN churn | 10k/overflow retained mutation churn passed; debug ASAN rich-text editor passed 27/27. |
| UI automation | 238 passed, 2 GUI skips; the sole release `test_rich_text_editor` crash reproduces at baseline commit. All campaign-sensitive RTE toolbar/dynamic-class scenarios pass after the runtime-attribute selector-cache fix. |
| Baseline layout | 4,365/4,379 passed, 6 skipped, 14 existing browser-reference failures. |
| `page` browser-reference run | 28/46 passed versus 27/46 at the instrumented baseline run. The batch comparator reports five stored-score regressions, but the stronger direct baseline-vs-after fresh-process comparison has 46/46 identical normalized layout JSON hashes. |
| Radiant cast lint | `make lint ARGS='--rule ^no-int-cast-radiant$'`: no violations. |
| Header-structure lint | No new Radiant header violates DD4 and the count is 19/24. The target still exits nonzero for one pre-existing non-allow-listed per-file mirror. Campaign declarations were placed in coherent headers. |
| Diff hygiene | `git diff --check`: clean. |

The full Radiant aggregate still inherits pre-existing repository debt: the 14
browser-reference baseline cases, the Puppertino expectation, and the
release-optimized rich-text-editor crash. None changes under the exact baseline
executable comparison, and the campaign does not hard-code around them.

### 9.6 LOC change

Measured from baseline commit `a267387dbf373a1a142dd30fd5bd9bf193f0c739`,
excluding these two design/plan documents and unrelated user files:

| Scope | Added | Deleted | Net |
|---|---:|---:|---:|
| Production/build source | 4,231 | 616 | +3,615 |
| Tests and profiling tool | 1,030 | 13 | +1,017 |
| Total implementation | 5,261 | 629 | +4,632 |

The positive LOC is deliberate correctness infrastructure rather than repeated
per-type paths: external generation/pin ownership, GC weak slots, exact deep
style snapshots, allocator telemetry, profile serialization, and churn/equality
tests. DOM node retirement still uses the one generic arena recycler, prop reset
uses one typed table, and canonical lookup uses one exact hash/equality path per
representation.

---

## 10. Sequencing and Stop Conditions

**Critical path:** R0 → R1 → R2 → R3 → R4 → R7. R5 is optional after R2. R6 depends on R0 recipe evidence and may follow R4 to isolate liveness risk; when adopted, it completes before the final R7 snapshot. The R7 baseline snapshot is captured after R0 instrumentation is available but against baseline commit `a267387db`, before any reuse behavior change.

- Stop R1 if any retained consumer still points into scratch at the proposed clear boundary.
- Stop Font sharing if finalization cannot make canonical values immutable at acceptable cost; keep Inline results.
- Stop R4 if any raw-node holder or node-arena auxiliary allocation lacks reliable lifetime/size metadata.
- Do not add per-type DOM free lists merely because the existing arena needs counters or allocation-class metadata; extend the shared allocator once.
- Stop any R5 group below measured metadata/hash cost.
- Stop R6 if epoch/recipe/COW cost exceeds measured clone savings or an owned clone cannot be independent of epoch destruction.
- Stop R7 comparison if the page manifests, sampling point, build mode, or metric definitions differ; correct the harness and rerun rather than normalize incomparable totals.
- Record a phase as not adopted rather than hard-coding around a failed invariant.
