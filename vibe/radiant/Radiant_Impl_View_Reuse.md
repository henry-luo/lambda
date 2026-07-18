# Radiant View Prop Sharing & Storage Reuse — Implementation Plan

**Date:** 2026-07-18
**Baseline commit:** `a267387db`
**Parent design:** `vibe/radiant/Radiant_Design_View_Reuse.md` — decisions VR1–VR13, findings VF1–VF6, phases R0–R5. This doc turns those into execution-grade tasks.
**Related:** `Radiant_Design_Dom_View_Struct.md` + `Radiant_Impl_Dom_View_Struct.md` (DV1–DV16 complete; §11 alias ledger, §17 memory profile = this campaign's baseline numbers), `Radiant_Design_Robustness.md` (T7, RADIANT_CHECK), `doc/dev/C_Plus_Convention.md`.

**Campaign status: not started.**

---

## 0. Ground rules (apply to every task)

1. **Gates per PR:** `make build` · `make test-radiant-baseline` 100% · `make layout suite=baseline` clean · `make lint` green. R3 additionally: full UI automation + editor suites. R5 additionally: CSS + DOM-CRUD gtest suites (`test/css/`). Perf-relevant phases (R1+) get a **release-build** timing spot-check.
2. **Measure first, adopt by evidence (VR11).** R0's probe report is a merge prerequisite for R2/R4/R5 scope decisions — no interning lands for a group the probe doesn't justify. All memory claims re-run the §17 harness (46-page corpus, release build) and record deltas in §8.
3. **Behind existing gates only (VR13).** Sharing and recycling slot in behind `create()` (P7), `ensure_*` (`radiant/view_prop_ensure.cpp`), and the `VIEW_PROP_TEARDOWN` table (view_pool.cpp:396). No parallel allocation or teardown paths; the table gains *modes*, not siblings. A reviewer finding a second way to allocate/free the same object class is finding a defect.
4. **Fail toward owned.** Every sharing mechanism (alias, intern, signature) must have a one-line demotion path to the owned state, taken on any doubt (cap overflow, checksum mismatch, signature verification failure). Owned is always correct; shared is an optimization.
5. **Root-cause comment at every liveness/ownership decision point** (CLAUDE.md rule 12) — retirement gates, unshare triggers, and teardown-mode selections are exactly the code a future reader will misjudge without the invariant stated.
6. **LOC (ground rule 7 discipline, carried over):** target ~flat for the campaign — this round adds mechanism (free lists, intern tables, registry) whose offsets are the unified retire/destroy path (R3.2), teardown-mode consolidation (R1.1), and probe-driven deletions. Run `verify_loc_reduction.sh --ref a267387db` at phase ends as the consciousness check.
7. **Never mix a sharing change and a recycling change in one PR** — when a T7-class bug appears, the bisect must implicate one mechanism, not both.

### Baseline numbers (from impl plan §17, re-verified at `a267387db` before R0 merges)

| Metric (46-page corpus, release, post-layout) | Baseline |
|---|---|
| DOM arena used (sum) | 3,193,344 B |
| View arena used (sum) | 490,544 B |
| Pool cumulative allocations (dom.document + view_tree.pool) | 28,325,328 B |
| Median combined arena / page | 28,192 B |
| DOM-churn fixture (R0.3) arena growth | linear (unbounded) |

---

## R0 — Probe, metrics, fixtures (no production behavior change)

### R0.1 — Prop duplicate-rate probe  **[VR11]**
- Temporary diagnostic pass (same pattern as the §17 hook: capture after `layout_html_doc()`, removed after the campaign; raw CSV in `temp/`): for every `DomElement`, hash each present prop group's content (`XXH64` over the struct bytes for POD groups; for pointer-bearing groups hash the pointee chain — family string, shadow list, transform functions — so the numbers reflect *value* duplication).
- Report per group, per page and corpus-wide: instance count, distinct-value count, duplicate ratio, reclaimable bytes (`(instances − distinct) × sizeof`), and **inherit-chain ratio** (duplicates whose value equals their parent's — the Tier-1-reachable subset) vs non-ancestral duplicates (Tier-2-only).
- Output lands as a table in §8 of this doc. This single report decides: which groups get Tier-1 in R2, whether R4 happens at all, and for which groups.

### R0.2 — Cascade-signature duplicate probe  **[VR12 input]**
- In the same pass: accumulate the per-element signature hash (ordered `(CssRule*, specificity)` applications — instrument `dom_element_apply_rule`) plus an `element_specific` flag (inline style present / presentational-attribute declarations / post-parse CSSOM mutation).
- Report: signature-distinct count vs element count, shareable ratio (equal signature ∧ ¬element_specific), estimated `StyleTree` bytes reclaimable (measure actual per-tree footprint: AVL node count × node size from `css_style_node.cpp`).
- **Verification sub-probe:** for a sample of same-signature pairs, byte-compare their built trees — this validates signature *sufficiency* before R5 stakes correctness on it. Any mismatch in the probe = a missed signature input; find it now, not in R5.

### R0.3 — DOM-churn stress fixture  **[acceptance instrument for R3]**
- New layout/JS fixture: `removeChild`/`appendChild`/`createElement`/text mutation loops ×N (N≈10k) with periodic reconcile + relayout; assert via mem-context snapshot that doc-arena used bytes plateau after warmup. **Committed failing-by-design** (marked expected-fail like the known-failure convention) — it is R3's fails-before/passes-after test.
- Variant: same loop while JS holds references to a rotating subset of removed nodes (wrapper-liveness stress — nodes must NOT be retired while referenced, must be retired after release + GC).

### R0.4 — Metrics hooks
- Permanent (cheap, debug-only where costly): doc-arena garbage ratio (bytes in retired-node free lists + abandoned vs used), free-list hit rate per class, intern-table size/hit-rate counters, exposed through the existing mem-context dump (`--mem-dump`).
- Gate: R0 changes nothing in production paths; baselines trivially 100%. Probe report reviewed before R1 merges.

---

## R1 — Churn cut: reset-in-place + TextRect recycling (no sharing, no liveness)

### R1.1 — `VIEW_PROP_TEARDOWN` RESET mode  **[VR9]**
- Add `VIEW_TEARDOWN_RESET_IN_PLACE` to the flags consumed by `view_teardown_apply_table` (view_pool.cpp:433): for each present group — run `release_external` and `free_payload` exactly as today (font handles, embed docs, boundary payloads must still release), then instead of `view_pool_free_ptr` + clear, **memcpy the group from its canonical default** (`view_prop_defaults.cpp` instances) and keep the pointer.
- Retained-relayout call sites switch from FREE_POOL to RESET_IN_PLACE; full teardown (`ViewTree::destroy`, pool recreate) keeps FREE_POOL semantics (moot under pool destruction, but the mode stays correct for partial teardowns).
- **Care:** (a) groups whose *size class varies* don't exist — all groups are fixed-size structs; assert it (`static_assert` per table entry via a size field). (b) The union groups (`fi/gi`, `tb/td/form`): RESET must re-default to the *same member currently tagged* or clear to none — clearing to none is correct (next resolve re-establishes role), matches today's `clear_item_prop`. (c) `layout_cache` entry: reset = invalidate, not default-fill (it has its own generation logic).
- Test: gtest asserting pointer stability across a retained relayout (`el->blk` address unchanged, contents re-defaulted) + full layout baseline.

### R1.2 — TextRect free list  **[VR10]**
- `ViewTree` gains a `TextRect* rect_free_list`; teardown's text handling pushes rect chains instead of `pool_free`; the two allocation sites (layout_text.cpp:3176, :3194) pop-or-`pool_calloc` (memset on pop).
- Care: rects are chained (`next`) — push the whole chain in one splice; pop singly.

### R1.3 — Measure + record
- Re-run §17 harness + R0.4 counters. Expected: pool cumulative-allocation delta drops substantially on relayout-heavy pages (this phase's whole point); live bytes ~flat. Record in §8. Release timing spot-check (reset-in-place should be neutral-to-faster; memcpy default vs free+calloc).

---

## R2 — Tier-1 sharing: ownership + inherit-alias + unshare-on-write

### R2.1 — Ownership bitmask  **[VR3]**
- `owned_groups` bits in the `elmt_flags` reserve, one per shareable group (probe-confirmed set; plan for `font`, `in_line` + headroom). `ensure_*` sets the bit on allocation; `VIEW_PROP_TEARDOWN` (both FREE_POOL and RESET modes) acts **only when the bit is set**, else just clears the pointer.
- Land this *before* any aliasing exists (semantically null: every present group is owned today) — the mechanism proves itself against the full baseline before sharing turns on. Same accessor-first logic as the P1 seam.

### R2.2 — Inherit-alias: `FontProp`  **[VR2 Tier 1, VR4]**
- Resolver change (`resolve_css_style.cpp` font section): if **no declaration in this element's cascade touched any font-group property** and the parent element has a font group, set `el->font = parent->font`, owned bit clear. The resolver already knows per-property application; add a per-group "touched" accumulator during rule application.
- **`FontHandle` discipline:** sharers never own the handle (`owns_font_handle` false path); `release_element_font_prop` (view_pool.cpp:213) runs only for owned groups — R2.1 already gates it. The owning ancestor's teardown runs while descendants still point at the group **only** during whole-subtree teardown (top-down) — verify teardown order is leaf-first or make the RESET/FREE pass clear descendant aliases first; this is the one ordering hazard in R2, gets a dedicated gtest (teardown of a shared-font subtree, ASAN-clean).
- **OQ-B resolved here: transitive aliasing.** Child aliases the *resolution root's* group (grandchild → grandparent directly): flatter chains, one-hop unshare copy. Record the decision + rationale in the design doc when it lands.
- Derived-metrics care: `FontProp` carries computed metrics (`space_width`, ascender…) filled by `setup_font` — confirm these are deterministic per (family,size,style) so sharing them is value-correct (they are — they derive from the handle); root-cause comment at the alias site.

### R2.3 — Inherit-alias: `InlineProp`
- Same pattern; pure-POD group, no handle discipline. Color/visibility/cursor inherit — probe (R0.1) expected to show this as the highest-ratio group.

### R2.4 — Unshare-on-write  **[VR4]**
- `ensure_G(el, tree)` in `view_prop_ensure.cpp`: if pointer present but owned bit clear → allocate, memcpy from the shared instance, set bit, return. One function per group; no call-site changes (P1's seam pays off).
- Debug assert: writing detection — in debug builds, `ensure_*` on a shared group logs the unshare with element source_loc (visibility into unshare churn; a hot unshare path means the alias detection is misfiring).

### R2.5 — Debug write-barrier checksum  **[VR8, VR1 assertion]**
- Debug builds: when a group becomes shared (alias established), record `XXH64` of its bytes in a side table; at teardown/next-resolve, re-verify. Mismatch = something wrote through a shared pointer → `RADIANT_CHECK` failure naming the group + element. This is the guard that keeps VR1's "cascade-final" classification honest.

### R2.6 — Re-profile + record
- §17 re-run: expect pool live-bytes drop ≈ probe's inherit-chain reclaimable estimate for font+inline; DOM arena flat. Baselines + UI automation 100% (hover/focus restyle paths exercise unshare). Record §8.

---

## R3 — Node recycling: free lists + retirement (the liveness phase)

### R3.1 — Free-list infrastructure  **[VR7]**
- `DomDocument` gains: `DomElement* elem_free_list`, `DomComment* comment_free_list`, `DomText* text_free_buckets[N_BUCKETS]` (16 B classes over the `[DomText][String][chars]` block; block size stored in the retired node's own storage; bucket geometry from R0 text-length data), plus counters (R0.4).
- `T::create()` (all three) pops before `arena_calloc`; memset the popped block (preserves DV16's zeroed-storage contract); fresh node id from `next_node_id` (ids are never reused — event/state logs stay unambiguous).
- Care: `DomText::create` co-allocation sizing already computes the block size — pop the smallest fitting bucket; oversized remainder is NOT split (kept simple; measured waste in R0.4 counters).

### R3.2 — `retire_node()` — unified teardown  **[VR7, absorbs `dom_element_destroy`]**
- One function per type (`DomElement::retire(doc)` etc.): asserts the retirement conditions (below), releases retained side structures exactly once — `specified_style` (via `style_tree_destroy`; **ownership-aware after R5** — owned only), retained tag/id/class strings (`PersistentFieldRef` clears), `view_state_ref`/DocState deregistration, view props via `VIEW_PROP_TEARDOWN` FREE_POOL (ownership-aware post-R2), `ext` — then poisons (debug) and pushes to the free list.
- `dom_element_destroy`'s body becomes `retire` minus the free-list push where external callers need destroy-without-recycle semantics; audit its callers — expected outcome: destroy folds into retire entirely and is deleted (LOC offset).
- **Retirement conditions (all must hold; assert in debug):** `parent == nullptr` ∧ not document root ∧ no live JS wrapper (R3.3) ∧ no DocState reference (cursor/selection/focus check via `view_state_ref` registry) ∧ not a pseudo-content node (owned via `PseudoContentProp`, torn down by its owner). Per OQ-A resolution: **no editor-history gate exists** — radiant has no DOM-level undo/redo (Lambda reactive templates own editing history above this layer); root-cause comment states this so nobody adds a "safety" gate that hides real leaks.

### R3.3 — Wrapper-liveness registry  **[VR7 gate]**
- JS wrappers are branded native VMaps in the retained JS GC heap holding `DomNode*` (js_dom.h wrapping strategy). Add a bridge-side registry: `HashMap* wrapped_nodes` on `DomDocument` (DomNode* → wrap count / epoch), inserted at wrapper creation in `js_dom.cpp`, swept after each collection on the retained heap (`gc_collect` completion callback): entries whose branded VMap did not survive are dropped.
- **Conservative fallback (acceptable first landing):** if post-GC wrapper-death detection proves intrusive to the GC layer, mode B = "ever-wrapped ⇒ never retired (until document teardown)". Still bounded (wrapper creation is user-driven), still fixes the dominant no-JS/reconcile churn case, and the registry API is identical — detection precision upgrades later without touching retirement logic. R0.3's wrapper-stress variant marks its second half expected-fail under mode B.

### R3.4 — Sweep points  **[VR7]**
- Two, only: (1) **end of reconcile** — after the incremental/full reconcile paths (the `dom_js_record_reconcile` sites, event.cpp:4686 vicinity) walk the delink records (`DomJsMutationRecord` CHILD_REMOVE entries + reconcile-replaced subtrees) and retire nodes meeting R3.2's conditions; (2) **post-JS-GC** — after the registry sweep (R3.3), retire nodes newly wrapper-free that are already detached. Never inline in `removeChild` (JS re-insertion is legal and common).
- Detached-but-alive nodes (wrapped, or awaiting re-insert) simply wait — the fixture's wrapper-stress variant proves both directions.

### R3.5 — Poison + epoch  **[VR8]**
- Debug: `memset(0xDD)` before free-list push; `uint16_t debug_epoch` in DomNode debug-only padding, bumped on reuse; `RADIANT_CHECK` epoch validation in view-state deref and event target resolution paths.

### R3.6 — Acceptance
- R0.3 fixture flips to passing (bounded growth; plateau asserted). Full gates: radiant baseline, layout suite, UI automation, editor suites, ASAN run of the churn fixture. §17 + R0.4 re-profile recorded in §8. This phase's PRs are the campaign's most scrutinized; each lands separately (infrastructure → retire → registry → sweeps).

---

## R4 — Tier-2 interning (probe-gated; POD groups only)

### R4.1 — Intern table  **[VR2 Tier 2, VR5, VR6]**
- Only for groups where R0.1 shows non-ancestral duplicate ratio worth it (design threshold: savings > table footprint + hash cost). Expected candidates: `PositionProp`, `TransformProp`-sans-functions, `InlineProp` residual (non-inherited duplicates). **Skip entirely if the probe says Tier-1 already captured the win — R4 is optional by design.**
- `ViewTree`-owned open-addressing table keyed by group hash; resolve-commit path: build in stack scratch → hash → hit ? share (owned bit clear) : insert copy (table-owned). Interned instances immutable + checksummed (R2.5 machinery reused).
- Cap (elements-interned per group; from probe headroom) → past cap, fall back to owned (ground rule 4). Table drops wholesale on pool recreate.

### R4.2 — Validation
- Realized savings within 20% of probe prediction (else investigate before expanding); style-thrash micro-fixture (JS mutating inline styles per frame) shows no table growth (mutations go through `ensure_*` → owned, never intern).

---

## R5 — `specified_style` signature sharing  **[VR12; last by design]**

### R5.1 — Signature accumulation
- Productionize R0.2's instrumentation: during rule application (`dom_element_apply_rule` / pseudo-element variants), fold `(CssRule*, specificity)` into a per-element running hash; set `element_specific` on inline-style application (`apply_inline_style`), presentational-attribute declaration injection, and any post-parse `apply_declaration` (JS CSSOM).
- The R0.2 verification sub-probe must have come back clean (or its gaps fixed) — that report is this task's merge prerequisite.

### R5.2 — Doc-level tree intern + ownership
- `DomDocument`-owned signature table: signature → shared `StyleTree*` (tier-1, doc pool). Cascade completion per element: `element_specific` ? owned tree (today's path) : table lookup — hit shares (ownership bit on the element, mirroring VR3; reuse an `elmt_flags` bit), miss inserts this element's tree as the shared instance.
- `retire_node` (R3.2) and style-reset paths call `style_tree_destroy` **only for owned trees**. Table invalidation: stylesheet set change / document rebuild drops the table wholesale (trees freed with it); element `style_version` bumps work unchanged.

### R5.3 — Unshare-on-write
- First element-specific mutation on a sharing element: deep-copy shared → owned (`style_tree_clone` exists, css_style_node.cpp:865), set ownership, proceed. Gate is the same three entry points that set `element_specific`.

### R5.4 — Debug verification sampling
- Debug builds: for 1-in-N sharing elements, build the owned tree anyway, byte-compare against the shared instance; mismatch → `RADIANT_CHECK` naming the signature inputs + **demote to owned** (fail-safe). This is the standing guard against signature under-coverage (the phase's defining risk).

### R5.5 — Acceptance
- CSS + DOM-CRUD gtest suites, layout baseline, UI automation (pseudo-state restyle exercises signature variance), JS style-mutation fixtures. §17 re-profile: DOM-arena (doc pool) drop ≈ R0.2 estimate. Record §8.

---

## 7. Test plan (new tests by phase)

| Test | Phase | Proves |
|---|---|---|
| `view_reset_pointer_stability` gtest | R1 | RESET keeps prop addresses, re-defaults contents |
| shared-font subtree teardown (ASAN) | R2 | alias teardown ordering, handle release-once |
| unshare-on-write gtest (mutate shared → owned copy, sharer unaffected) | R2 | VR4 |
| DOM-churn fixture (R0.3) | R0 fail → R3 pass | bounded arena growth |
| wrapper-liveness stress (hold/release/GC cycles) | R3 | no premature retire; retire after release |
| epoch/poison RADIANT_CHECK negative test | R3 | stale-deref detection fires |
| style-thrash intern-cap fixture | R4 | no table pathology |
| signature share/unshare gtest + sampled verification | R5 | VR12 correctness |

## 8. Metrics tracking (filled per phase; §17 harness + R0.4 counters)

| Milestone | Pool cumulative (B) | Pool live Δ | DOM arena used (B) | Churn fixture | Notes |
|---|---|---|---|---|---|
| Baseline `a267387db` | 28,325,328 | — | 3,193,344 | linear | re-verify at R0 |
| after R1 | expect ↓↓ | ~flat | flat | linear | churn cut |
| after R2 | ↓ | ↓ (font+inline dup) | flat | linear | |
| after R3 | ↓ | ~flat | **plateau under churn** | **bounded** | |
| after R4 | ↓ | ↓ (probe-gated) | flat | bounded | optional |
| after R5 | ↓ | — | ↓ (tree dedup) | bounded | |

## 9. Sequencing & risk register

- **Critical path:** R0 → R1 → R2 → R3; R4 and R5 depend on R2's ownership machinery + their probe verdicts, and can land in either order after R3 (R5 also wants R3.2's ownership-aware retire).
- **Highest-risk task: R3.3/R3.4** (premature retire = use-after-reuse). Contained by: conservative fallback mode B, sweep-points-only, poison+epoch, the wrapper-stress fixture, ASAN gate, and ground rule 7 (recycling PRs never mixed with sharing PRs).
- **R2 teardown ordering** (owner freed before sharers cleared) — dedicated gtest + leaf-first audit before aliasing enables.
- **R5 signature under-coverage** — R0.2 verification sub-probe before build; sampled byte-compare + fail-safe demotion after. R5 last so the cheaper phases' wins aren't hostage to cascade subtleties.
- **Scope discipline:** if R0's probe shows a group/signature win below threshold, the corresponding task is *dropped*, recorded in §8 — not built "since we're here".
