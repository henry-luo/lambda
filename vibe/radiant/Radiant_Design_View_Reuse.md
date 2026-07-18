# Radiant Design: View Prop Sharing & Storage Reuse

Status: **DRAFT for review** — decisions VR1–VR13 proposed 2026-07-18; OQ-A/C/D resolved by user same day (no DOM-level edit history; `specified_style` reuse in scope; free-list trimming deferred).
Scope: `ViewTree` pool/prop lifecycle (`radiant/view_pool.cpp`), DOM node storage (`lambda/input/css/`), reconcile/JS mutation paths.
Extends: `vibe/radiant/Radiant_Design_Dom_View_Struct.md` (DV1–DV16, campaign complete) — this round activates the **DV14 style-sharing follow-up** and closes the storage-reuse gap DV15 anticipated ("uniform size keeps pool recycling trivial").
Related: `Radiant_Design_Memory.md` (R1–R7), `Radiant_Design_Robustness.md` (T7 stale pointers, F1 RADIANT_CHECK), `vibe/Lambda_Design_Memory_Model.md` (type-stable pools, gen-handles), impl plan §11 alias ledger + §17 memory profile.

The two scopes are one principle applied on two axes:
- **Sharing** = reuse of identical prop values *across elements* (Part A).
- **Recycling** = reuse of delinked storage *across time* — reflow and DOM updates (Part B).

---

## 1. Goals & Non-Goals

**Goals**
1. Cache and share view `*Prop` groups within a view tree: elements with identical resolved values point at one shared instance instead of owning copies.
2. Manage the gaps/garbage created when reflow or DOM mutation delinks props and DOM nodes: bounded memory under sustained DOM churn, reuse instead of abandonment.
3. Reduce per-relayout allocator churn (the §17 profile shows ~28.3 MB cumulative pool allocations across the 46-page corpus against ~3.2 MB of live arena — most allocation work is repeated, not retained).
4. Extend the same reuse discipline to `specified_style` (the per-element AVL `StyleTree`): rule-identical elements share one tree (VR13).

**Non-Goals**
- **Moving/compacting collection.** Node identity is load-bearing (DV15: the Lambda parent's items array points at `&child->elmt`; JS wrappers, DocState, `element_dom_map` all hold raw pointers). Reuse happens in place via free lists, never by relocation.
- **Cascade-level style sharing** (skipping selector *matching* for sibling elements, Blink "style sharing cache" style). This round shares *storage* — resolved prop groups (VR1–VR6) and specified-style trees (VR13) — but still runs matching per element; skipping the matching work itself is a later proposal that VR13's signature table enables.
- **Free-list trimming / arena compaction** (OQ-D resolved): returning free-list stock to the OS would require compacting the DOM/view tree, which node identity forecloses (see above). Retired-node stock is bounded by the document's own high-water mark and released wholesale at document teardown; a trim mechanism is deferred to a future phase if long-lived-session measurements demand it.
- **DOM-level edit history** (OQ-A resolved): radiant does not support contenteditable-style undo/redo; editing history lives in Lambda reactive templates, a level above the radiant DOM. Retirement liveness therefore has exactly two sweep gates (VR7) — no editor-history gate exists or is planned.
- Cross-document sharing; shrinking `DomDocument`; changing the DV4 accessor/ensure contract (sharing slots in *behind* it).

---

## 2. Current State (Findings)

**VF1 — Props are pool-allocated and individually freeable; the teardown machinery already exists.** `ViewTree::alloc_prop` = `pool_calloc` from `ViewTree::pool` (rpmalloc-backed), not the bump arena. The `VIEW_PROP_TEARDOWN` table (view_pool.cpp:396) frees every group + payload per element during retained relayout/teardown, with external-release hooks (font handles, embed docs). So prop "garbage" is not leaked storage — it is **churn**: every retained relayout frees each element's groups and re-allocates same-size blocks moments later.

**VF2 — No sharing exists.** Every element owns its prop copies. Inheritance-heavy groups (`FontProp` 96 B, `InlineProp` 52 B) are duplicated per element even when byte-identical down whole subtrees. The impl plan §11 alias ledger confirmed the invariant that makes sharing safe: **no prop group is aliased between elements today**, and DV9c (shared groups immutable post-cascade) is already normative.

**VF3 — DOM nodes are permanent garbage once delinked.** `DomElement::create` et al. allocate from the **doc bump arena** (`arena_calloc`); there is no per-node free. `dom_element_destroy` tears down side structures (style trees, retained strings) but abandons the 368 B of node storage. JS `removeChild`, reconcile subtree rebuilds, and editor operations therefore grow the doc arena monotonically for the document's lifetime — the "gaps/garbage" this proposal targets. (§17: DOM arena is the dominant tree cost, 3.19 MB vs 0.49 MB view arena across the corpus.)

**VF4 — Node sizes are uniform and known** (P0–P7 outcome): `DomElement` 368 B, `DomComment` fixed, `DomNode` 80 B — ideal free-list classes. Exception: `DomText` is co-allocated as one block `[DomText][String header][chars…]` (dom_node.hpp `dom_text_to_string` offset math), so its block size varies with text length.

**VF5 — `create()` is a single allocation choke point** (DV16/P7): every node type constructs through one static function with a zeroed-storage contract. A free-list pop slots in behind `create()` without touching any call site. Likewise `ensure_*` (DV4/P1) is the single prop-write gate — unshare-on-write slots in behind it.

**VF6 — Detached-node liveness is observable.** JS can hold references to removed nodes and re-insert them later, so "delinked" ≠ "dead". But the runtime already tracks what makes a detached node live: the JS wrapper registry (`element_dom_map` + js_dom wrapper invalidation used by `free_document`), tree linkage (`parent == nullptr`), and `view_state_ref`/DocState registration. Retirement can therefore be gated on provable unreachability rather than guessed.

---

## 3. Decisions

### Part A — Prop caching & sharing

### VR1 — Shareability is a per-group property, audited not assumed
Groups classify by their write pattern after cascade (DV10 ledger is the input):
- **Cascade-final** (candidate for sharing): all fields written during style resolution, never during layout/render. Expected: `InlineProp`, `PositionProp`, `TransformProp`, `FilterProp`, `MulticolProp`; `FontProp` (fields final post-resolve; see VR4 for its handle).
- **Layout-mutated** (never shared): groups receiving used-value writes or per-pass state — `BlockProp`, `FlexItemProp`, `GridItemProp`, `TableProp`/`TableCellProp`, `ScrollProp`, `EmbedProp`.
The audit's verdict is recorded per group in the header tier comment (`// tier-2: view-pool, cascade-final → shareable`). A layout write to a cascade-final group after this lands is a bug; debug builds assert it (VR8 write-barrier).

### VR2 — Two sharing tiers: parent-chain aliasing first, tree-wide interning second
- **Tier 1 — inherit-alias (cheap, ships first):** at resolve time, if no declaration touched group G on this element and the parent has G, the child stores the parent's pointer. Highest-value for `FontProp`/`InlineProp` (inherited properties; most elements change neither font nor color). Detection is already available: the resolver knows whether any declaration hit the group.
- **Tier 2 — tree-wide intern (hash-cons):** a `ViewTree` intern table keyed by group content. At cascade commit, the group is resolved into a stack scratch, hashed, and either matched to an existing instance or copied into a table-owned allocation. Catches non-inherited duplication (e.g. hundreds of cells with identical `PositionProp`/`TransformProp`).
Tier 1 handles the dominant case at near-zero cost; Tier 2's win is measured before adoption (VR11 probe) and adopted only for groups where the duplicate rate justifies the hash cost.

### VR3 — Ownership is explicit: three states per group slot
A group pointer is in exactly one of three states, distinguishable without new per-element storage where possible:
1. **absent** (`nullptr`) — canonical defaults (DV4 unchanged);
2. **shared** — points at parent's group (Tier 1) or an intern-table instance (Tier 2); the element does NOT own it;
3. **owned** — element-specific allocation (the only mutable state).
An `owned_groups` bitmask (one bit per shareable group, in the existing `elmt_flags` reserve) records state 3. Teardown (`VIEW_PROP_TEARDOWN`) frees a group **only when its owned bit is set** — shared instances are owned by their parent element (Tier 1) or the intern table (Tier 2), never by sharers.

### VR4 — Unshare-on-write via `ensure_*` (COW where COW is safe)
`ensure_G(el)` becomes the write gate: if G is absent or shared, allocate an owned copy (memcpy from the shared instance or canonical default), set the owned bit, return it. This is the COW pattern DV4 rejected for *defaults* — safe here because P1 made `ensure_*` the single write path, so the check-and-clone lives in exactly one function per group, not at every write site.
`FontProp` care: sharing multiplies handle aliasing — `font_handle` refcounts move to the owning instance only (`owns_font_handle` stays false on sharers), and `release_element_font_prop` runs only for owned groups. The existing external-release hook structure already supports this split.

### VR5 — Interned instances are immutable, table-owned, epoch-lifetime
Tier-2 instances are frozen at insert (debug: write-protect via checksum, VR8). The intern table owns their storage: they are never individually freed; the table drops wholesale when the view pool is recreated (full rebuild) and survives retained relayout. No refcounting — delinked elements just stop pointing; the table's epoch lifetime bounds waste, and the VR11 metrics watch for pathological table growth (a page whose styles mutate every frame interns new variants; cap table size and fall back to owned allocation past the cap).

### VR6 — Pointer-bearing groups intern by value, or not at all (initially)
Content-hash equality is trivial only for pointer-free POD groups (`InlineProp`, `PositionProp`). Groups carrying payload pointers (`FontProp.family`/`text_shadow`, `TransformProp.functions`, `BoundaryProp.background/border/…`) need payload-aware equality (hash the pointee chain). Phase 1 interns POD groups only; pointer-bearing groups get Tier-1 inherit-aliasing (pointer identity, no equality question) and are revisited for Tier 2 with payload hashing only if the VR11 probe shows a duplicate rate worth the complexity. `BoundaryProp` (160 B, ~2k sites, 5 payload pointers) is explicitly Phase-2.

### Part B — Storage recycling (gaps/garbage management)

### VR7 — Per-type free lists for DOM nodes; retirement gated on provable unreachability
`DomDocument` gains free-list heads for node storage; `T::create()` pops before bumping the arena (memset on pop preserves the zeroed-storage contract; 368 B memset is cheaper than an arena page fault).
- **`DomElement`/`DomComment`**: fixed-size lists (DV15 uniformity pays off directly).
- **`DomText`**: size-bucketed list (16-byte classes over the `[DomText][String][chars]` block; pop the smallest fitting bucket, or bump-allocate when none fits). The co-allocation block size is recorded in the retired block's header slot (the `DomText` storage itself, unused once retired).
- **Retirement condition** (all must hold): detached (`parent == nullptr`, not the root), **no live JS wrapper** (wrapper registry lookup), not referenced by DocState cursors/selection/focus (`view_state_ref` cleanup runs at retire), not inside a pseudo-content subtree. Retirement runs at defined **sweep points** — end of reconcile, and after JS heap GC invalidates wrappers — never inline in `removeChild` (JS may re-insert; a detached-but-wrapped node simply stays unretired until its wrapper dies).
- Retired nodes also release their retained side structures once (`dom_element_destroy` semantics), so retirement unifies today's split teardown paths.

### VR8 — Stale-pointer safety: poison + epoch in debug, doctrine deferred
Retire/reuse creates the T7 hazard by construction, so it ships with its own defenses:
- Debug builds: `memset(0xDD)` on retire; a per-node `debug_epoch` bumped on reuse; `RADIANT_CHECK` hooks validate epoch on view-state and event-path dereferences.
- Cascade-final shared groups get a debug checksum at freeze; the checksum is re-verified at teardown to catch post-cascade writes (the VR1 assertion).
- Full generational-handle doctrine (memory-model survey) remains future work; this proposal's poison+epoch is the affordable subset.

### VR9 — Retained relayout resets owned groups in place instead of free+realloc
Today `VIEW_PROP_TEARDOWN` (FREE_POOL mode) frees every group, and the next pass re-allocates identical sizes. New RESET mode for **owned** groups: keep the allocation, release payloads/external refs as today, re-init the block by memcpy from the canonical default (DV4 template), leave the pointer and owned bit intact for the resolver to overwrite. Shared/absent groups just clear their pointers (cheap). This removes the dominant free/alloc pair per element per relayout — the single biggest churn cut in this proposal — and is safe precisely because the element, type, and size are unchanged.

### VR10 — TextRects recycle through the same discipline
`TextRect` chains (pool-allocated per text per relayout) get a `ViewTree` free list, retired in teardown's RESET mode and popped in `layout_text.cpp`. Same fixed-size pattern as VR7, view-pool-side.

### VR11 — Measure first, adopt by evidence
Before Tier-2 interning or `BoundaryProp` payload hashing lands, a **duplicate-rate probe** runs over the §17 corpus: hash every group instance per page, report per-group duplicate ratios and byte savings. The probe also establishes the recycling metrics: doc-arena garbage ratio (retired-reusable vs abandoned bytes), free-list hit rate, pool cumulative-allocation delta vs the §17 baseline (28,325,328 B), and a **DOM-churn stress fixture** (repeated `removeChild`/`appendChild`/text mutation ×N) whose acceptance criterion is bounded doc-arena growth (today it is linear). Adoption threshold per group: measured savings must exceed the intern table's own footprint + hash cost on the corpus.

### Part C — Specified-style tree reuse *(scope added on OQ-C resolution)*

### VR12 — `specified_style` sharing by cascade signature
The per-element AVL `StyleTree` (tier-1, doc pool; ~367 sites) gets the same three-state treatment as prop groups, keyed not by content hash but by **cascade signature** — the ordered sequence of `(CssRule*, specificity)` applications the resolver already performs per element:
- During rule application, accumulate a signature hash. Elements with equal signatures and **no element-specific style sources** produce byte-identical trees → share one table-owned `StyleTree` (doc-level intern table, tier-1 lifetime, dropped on stylesheet change / document rebuild).
- **Element-specific sources force an owned tree** (never shared): inline `style=""` attributes, presentational HTML attributes that inject declarations (e.g. `<td width=>`), JS CSSOM mutations (`dom_element_apply_declaration`, `apply_inline_style`). The signature must cover *every* input that shapes the tree — rule set, application order, specificity, quirks-mode adjustments — a missed input silently shares wrong styles (see Risks).
- **Unshare-on-write** mirrors VR4: the first inline/JS style mutation on a sharing element deep-copies the shared tree into an owned one (`style_version`/`needs_style_recompute` invalidation unchanged). Read API (`dom_element_get_specified_value` etc.) is untouched.
- Pseudo-state variants need no special handling: `:hover`-dependent rule matches change the signature naturally.
- Expected win: rule-uniform documents (tables, lists, feeds) collapse hundreds of identical AVL trees into one; the R0 probe is extended to measure signature duplicate rates alongside prop duplicate rates, and Tier adoption follows the same evidence threshold (VR11).
- Shared trees reference the same `CssDeclaration*` objects from stylesheets as owned trees do today — no declaration copying is introduced; sharing is at the tree-node level.

### VR13 — LOC and convention discipline (carried over)
Ground rule 7 applies: sharing and recycling slot in behind existing gates (`create()`, `ensure_*`, teardown table) — no parallel paths, no shadowed legacy. The teardown table gains modes, not siblings; retirement unifies (not duplicates) `dom_element_destroy`. Net LOC target: ~flat (this round adds mechanism; the offset is the unified teardown/destroy path and any dup-probe-driven deletions), with the same consciousness check at phase ends.

---

## 4. Implementation Outline

**Detailed execution plan: `vibe/radiant/Radiant_Impl_View_Reuse.md`** (task-level breakdown R0.1–R5.5, test plan, metrics tracking, risk register). Summary below.

| Phase | Content | Gate |
|---|---|---|
| R0 | VR11 probe (prop dup rates **+ VR12 signature dup rates**) + metrics harness + churn stress fixture (fails today by design) | probe report lands in this doc |
| R1 | VR9 reset-in-place + VR10 TextRect recycling (pure churn cut, no sharing, no liveness) | baselines 100%; pool cumulative counters drop vs §17 |
| R2 | VR3 ownership bitmask + VR2 Tier-1 inherit-alias for `FontProp`/`InlineProp` + VR4 unshare-on-write + VR8 debug checks | baselines; §17 re-profile (expect DOM-side flat, pool live-bytes drop) |
| R3 | VR7 node free lists + retirement sweep + wrapper-liveness gate | churn fixture: bounded growth; UI automation + editor suites 100% |
| R4 | VR2 Tier-2 intern for probe-justified POD groups (VR5/VR6) | probe-predicted savings realized within 20%; table-growth cap tested |
| R5 | VR12 `specified_style` signature sharing + unshare-on-write | probe-justified; baselines + CSS/DOM-CRUD gtests 100%; doc-arena re-profile |

Ordering rationale: R1 is the highest win/risk ratio (no new aliasing, no liveness reasoning); R2 introduces sharing where detection is free; R3 is the only phase touching liveness and carries the heaviest test gate; R4 is optional-by-evidence. R5 comes last because signature correctness spans the whole cascade surface (rules, presentational attributes, quirks) — it reuses the ownership/unshare machinery R2 proves out, and its probe numbers (R0) decide how aggressively it lands.

## 5. Risks

- **R3 liveness under-approximation** → premature retire → use-after-reuse. Mitigations: conservative gate (any doubt = stay), sweep points only, VR8 poison+epoch, churn fixture + full event/editor suites. This is the T7 class deliberately taken on; it is why R3 is late and isolated.
- **Sharing a group that layout mutates** (VR1 misclassification) → cross-element corruption. Mitigation: audit against DV10 write-site ledger + debug checksum assertion (VR8); Tier 1 ships only for groups whose immutability the §11 alias work already scrutinized.
- **Intern table pathology** on style-thrashing pages (animation mutating inline styles) → table growth + hash cost with zero reuse. Mitigation: VR5 cap + fallback-to-owned; transition/animation paths write through `ensure_*` → owned copies, never intern (transitions already keep per-element state).
- **`DomText` bucket fragmentation** if text lengths are diverse. Mitigation: bucket sizes from probe data; worst case the list caps and falls through to bump allocation — never worse than today.
- **VR12 signature under-coverage** — any style input missed by the signature (a presentational attribute, a quirks adjustment, UA-sheet conditionality) silently shares wrong specified styles between elements. Mitigation: debug-build verification mode that builds the owned tree anyway and byte-compares it against the shared one on a sampled basis; the CSS/DOM-CRUD gtest suites gate R5; any mismatch demotes the element to owned (fail-safe direction).

## 6. Open Questions

- OQ-B: should Tier-1 aliasing apply transitively (grandchild → grandparent's group directly) or re-alias per level? (Transitive = flatter chains, but unshare must then copy from the transitive root; propose transitive, decide in R2.)

**Resolved 2026-07-18 (user):**
- ~~OQ-A~~ — no DOM-level undo/redo exists or is planned (editing lives in Lambda reactive templates above radiant); the two VR7 sweep gates suffice. Recorded as a Non-Goal.
- ~~OQ-C~~ — `specified_style` reuse is **in scope this round** → VR12 / phase R5.
- ~~OQ-D~~ — bounded free-list stock accepted; trimming would require tree compaction and is deferred to a future phase. Recorded as a Non-Goal.
