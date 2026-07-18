# Radiant DOM/View Struct Refactoring — Implementation Plan

**Date:** 2026-07-18
**Baseline commit:** `019f47214`
**Parent design:** `vibe/radiant/Radiant_Design_Dom_View_Struct.md` — decisions DV1–DV15, findings F1–F8, target sketch (§4). This doc turns those into execution-grade tasks.
**Related:** `vibe/radiant/Radiant_Imp_Code_Dedup.md` (header consolidation), `vibe/radiant/Radiant_Design_Robustness.md` (T7 stale-View), `vibe/Lambda_Jube_DOM3.md` (property-table dispatch), `doc/dev/C_Plus_Convention.md`.

---

## 0. Ground rules (apply to every task)

1. **Gates per PR:** `make build` · `make test-radiant-baseline` 100% · `make layout suite=baseline` clean for layout-touching tasks · `make lint` green. From P3 on, additionally a **release-build** (`make release`) layout timing spot-check on the benchmark set (rule 10: never perf-test debug builds).
2. **P1 and P2.1 are semantically null** — no behavior change permitted. Any baseline diff in those phases is a bug in the port, not an acceptable delta.
3. **Size is measured, never guessed.** `temp/size_probe.cpp` (kept for the duration of this campaign) is run after every P3 task; results recorded in §7. Ratchet `static_assert`s are tightened as wins land, never loosened without amending the design doc.
4. **Root-cause comment at every non-mechanical change** (CLAUDE.md rule 12) — especially where a lifetime tier or invalidation story is the reason the code is shaped that way.
5. **One concern per PR.** Mechanical ports (P1.2) must not be mixed with semantic changes (P2/P3) in the same commit — reviewability is the point of the accessor-first strategy.
6. Phases are dependency-ordered; tasks *within* a phase are independent PRs unless noted. P6 can run any time after P0.
7. **Total LOC should end lower than it started — as a consequence of writing well, not as a quota.** The old code is inconsistent and unstructured; there is no reason refactored code should need more lines than what it replaces. The discipline:
   - **Write terse.** Restructure, refactor, refine, reuse — prefer collapsing a null-check ternary into an accessor call, a repeated guard into one method, a 4× copied block into a loop. New code earns its lines.
   - **Retire legacy completely.** When new structure lands, the code it supersedes is deleted *in the same PR* — no shadowed old paths, no "kept just in case", no legacy littered through the tree. The P4 property table is the case to watch: each batch of table rows retires the ad hoc per-property code it replaces.
   - **Check, don't police.** `./utils/verify_loc_reduction.sh --ref 019f47214 <touched + new files>` (it counts new helper files, so moves don't masquerade as savings) is run at phase ends as a consciousness check; tests and goldens excluded. If a phase comes out positive, that's a prompt to look for what wasn't retired or wasn't tersely written — not a blocker.

### Call-site scale (grep counts at baseline; include some false positives — scoping data, not targets)

| Field(s) | Sites | | Field(s) | Sites |
|---|---|---|---|---|
| `->bound` | ~2011 | | `->fi` / `->gi` | ~296 |
| `->blk` | ~1246 | | `->tb` / `->td` | ~370 |
| `->font` | ~1134 | | `->form` | ~434 |
| `->position` | ~613 | | `item_prop_type` | 145 |
| `->embed` | ~361 | | pseudo `*_styles` (5) | ~62 |
| `->scroller` | ~271 | | fragment-union fields | ~64 |
| `->in_line` | ~246 | | `->multicol` | 124 |
| `specified_style` | ~367 | | rare ptrs (vpath/backdrop/frags/shadow) | ~45 |

Consequence: the hot-field port (P1.2) is ~6–7k sites → **scripted per-field rewrites** reviewed in bulk, ordered smallest-first to shake out the pattern (`in_line` → `scroller` → … → `bound` last).

### LOC accounting (ground rule 7): where the reduction naturally comes from

| Sink (adds) | est. | Source (deletes) | est. |
|---|---|---|---|
| `view_prop_defaults.cpp` + accessors (P1.1) | +300–400 | null-check collapse at ~6–7k sites: `el->blk ? el->blk->x : d` / `if (el->blk && …)` → `el->block()->x` (P1.2) | −1500–2500 |
| `ext` struct + helpers (P3.1) | +100–150 | 145 `item_prop_type` guard sites → accessors; flex-table workaround deleted (P2) | −150–250 |
| property table + serializers (P4) | +400–600 | 4× fragment-union field blocks + their per-block handling → `frags[4]` loop (P3.1) | −150–250 |
| overlay accessors (P2.2) | +60 | ad hoc `js_dom` computed-style/property code replaced by table (P4.3) | −400–700 |
| | | `dom_element.hpp` wrapper decls + doc-comment blocks for methodized fns; dead C-ABI wrappers (P5.3) | −400–600 |
| | | dead code: `DomText.color` sites, `render_text` dead branch, `native_element` plumbing, `content_type` (P3.3/P3.4) | −100–150 |
| **total adds** | **~900–1200** | **total deletes** | **~2700–4400** |

Expected net: **−1500 to −3200 LOC**, arriving naturally if the discipline holds — the deletions are inherent to the refactor (collapsed guards, retired workarounds, replaced ad hoc code), not extra work. Run the script at each phase end and record in §7; a positive reading means something wasn't retired or wasn't written tersely — find it.

---

## P0 — Conventions, probes, flags (small, mechanical)

### P0.1 — Tier tags on every struct  **[DV1/DV2]**
- Add the one-line tier comment (`// tier-1: doc-pool, survives relayout` / `// tier-2: view-pool, rebuilt each relayout` / `// tier-3: layout-transient, valid within pass`) at the head of every struct in `dom_node.hpp`, `dom_element.hpp`, `view.hpp`, `layout.hpp`.
- While tagging, build the **tier-violation ledger**: every field whose pointee tier is *lower-lifetime* than its holder (the T7 class). Known entries to seed: `transition_state` (tier-1 holder ← the comment exists precisely for this), `DomElement.font/bound/...` (tier-1 struct holding tier-2 pointers — the sanctioned direction for the unified tree, document once at `DomElement` head), `view_state_ref`. The ledger goes in a `## Tier audit` appendix of this doc as tagging proceeds.
- No code changes; comments only. Gate: build + lint.

### P0.2 — Size ratchet, initial ceiling  **[DV11]**
- Add to `view.hpp` next to the existing overlay asserts: `static_assert(sizeof(DomElement) <= 584)`, `static_assert(sizeof(DomText) <= 136)`, `static_assert(sizeof(DomNode) <= 80)`. These assert *current* sizes as ceilings so no parallel work regresses them while this campaign runs; P3 tightens them.
- Keep `temp/size_probe.cpp` as the measurement tool; record baseline row in §7.

### P0.3 — `elmt_flags` consolidation  **[DV8]**
- Add `uint32_t elmt_flags` to `DomElement`; migrate the simple bools with named constants: `needs_style_recompute` (16 sites), `styles_resolved` (58), `float_prelaid` (9), `has_cached_intrinsic_widths` (9), `measuring_intrinsic_width` (8), `has_pending_element_scroll_x/y` (in the 28 `pending_element_scroll` sites), `has_inline_fragment_union` + 3 siblings (presence bits stay even after P3.1 moves the floats).
- Provide inline bit accessors (`bool styles_resolved() const` / `set_styles_resolved(bool)`) so call sites stay readable; port the ~130 sites mechanically.
- Reserve bit ranges now for the P2 union tags (3 bits `parent_item_kind`, 3 bits `role_kind`) and a `text_is_symbol` equivalent on `DomNode` (P3.4 uses a `node_flags` byte in DomNode padding instead — reserve nothing there).
- Gate: baseline 100% (flag semantics identical; watch `styles_resolved` reset paths in `skip_style_reset`/incremental layout).

---

## P1 — Accessor layer (semantically null; the seam for everything after)

### P1.1 — Canonical defaults + accessors  **[DV4]**
- New `radiant/view_prop_defaults.cpp`: one `extern const` canonical instance per prop group (`BLOCK_PROP_DEFAULT`, `BOUNDARY_PROP_DEFAULT`, `FONT_PROP_DEFAULT`, `INLINE_PROP_DEFAULT`, `SCROLL_PROP_DEFAULT`, `POSITION_PROP_DEFAULT`, `EMBED_PROP_DEFAULT`, `TRANSFORM_PROP_DEFAULT`, `FILTER_PROP_DEFAULT`, `MULTICOL_PROP_DEFAULT`, item/role group defaults), statically initialized to CSS initial values. **Care:** several current groups are calloc'd and then unconditionally written by the resolver — the canonical instance must encode the *CSS initial value*, not today's calloc-zero; cross-check each field against `resolve_css_style.cpp` defaults while writing them, and note divergences (this is where zero≠initial bugs have been hiding).
- Read accessors on `DomElement` (in `dom_element.hpp`, returning `const*`): `block()`, `boundary()`, `inl()`, `fontp()`, `scroll()`, `positionp()`, `embedp()`, `transformp()`, `filterp()` — name them to avoid colliding with existing field names during coexistence.
- Writers: `ensure_block(ViewTree*)` etc. — allocate via the existing `alloc_prop` path, **initialize by `memcpy` from the canonical default** (replacing calloc-zero init), return mutable pointer. Tier-1 groups (`css_variables`-adjacent) get doc-pool variants.
- **Care:** switching group init from zero-fill to default-fill is the one semantic change in P1 — it lands *field by field* only where the resolver provably overwrites, or with the default set equal to today's zero where the audit is not yet done. When in doubt, ship the default instance as all-zeros first and tighten to true CSS initials per group in a follow-up commit with baseline runs between.
- Gate: baseline 100% after each group's ensure-switch.

### P1.2 — Port call sites, per field, smallest first  **[mechanical]**
- Order: `in_line` (246) → `scroller` (271) → `embed` (361) → `position` (613) → `font` (1134, excluding `FontBox`/`DomText.font` false hits) → `blk` (1246) → `bound` (2011). One field = one PR.
- Pattern: `el->blk && el->blk->x` → `el->block()->x` (reads); `alloc_prop`+assign sites → `ensure_block(tree)`. Scripted rewrite + manual sweep of the non-matching residue; both styles compile side by side, so a PR can stop at any clean point.
- **Do not port** `layout.hpp`/tier-3 internal uses that copy a prop pointer into a `*Box` for a pass — those are correct as-is; the accessor is for tree-side access.
- Gate per PR: baseline 100%, zero layout diffs.

### P1.3 — Shared-group aliasing audit  **[DV9c prerequisite]**
- Find every site assigning one element's prop pointer from another element's (`child->font = parent->font` pattern; seed: `layout_table.cpp:4337/9368/9414` inherit-color copies are value copies — verify; hunt pointer aliasing specifically for `font`, `in_line`).
- Deliverable: ledger in this doc + decision per site — (a) genuinely shared → mark group immutable-post-cascade, (b) should be a value copy → fix. Blocking for P4 (derived computed style must know whether a group is element-owned).

---

## P2 — Union split by role  **[DV5/DV5a — correctness fix]**

### P2.1 — Introduce the two unions + tags
- Replace the 5-way union with: `union { FlexItemProp* fi; GridItemProp* gi; }` (tag `parent_item_kind`: none/flex/grid) and `union { TableProp* tb; TableCellProp* td; FormControlProp* form; }` (tag `role_kind`: none/table/cell/form). Member names unchanged → the ~1100 member-access sites compile untouched; only the 145 `item_prop_type` sites are edited (map old enum checks to the correct new tag).
- `DomElement` grows 8 B (allowed transiently; P3 pays it back). Ratchet stays at 584 — verify it holds (padding may absorb it; if not, bump the assert with a comment referencing this task, and P3.1 restores).
- Gate: baseline 100%; this PR is behavior-preserving (every old tag state maps 1:1 onto a (parent_item_kind, role_kind) pair).

### P2.2 — Overlay accessors, tag checks centralized
- Add `DomTable::table()`, `ViewTableCell::cell()`, `DomElement::flex_item()`, `grid_item()`, `form_control()` — each debug-asserts its tag and returns the member (nullable). Port the `item_prop_type == X && el->y` guard sites (the bulk of the 145) to accessors.
- Convention note added to `dom_element.hpp`: direct union-member access outside accessors is a violation (DV5a).

### P2.3 — Delete the collision workarounds + regression tests
- Remove the `layout_flex_multipass.cpp:1121` mid-layout `TableProp` re-allocation; the flex measurement path now writes `fi` without touching `tb`.
- **New layout tests (test/layout/)**: `display:table` as flex item (the 1121 scenario — border-spacing must survive flex measurement), `<input>`/`<select>` as flex item and as grid item (form + fi coexist), `display:table` as grid item, nested table-in-flex-in-grid. Each with expected `.txt` golden (rule 8).
- Sweep for sibling workarounds: grep flex/grid/table multipass for re-allocation of role props after measurement ("re-allocate", `alloc_prop.*TableProp|FormControlProp` outside resolve) — the 1121 comment says "they share the same union"; any cousin comment goes in this PR.

---

## P3 — Field regrouping (the size phase)

Order chosen so each task's measurement is attributable. Run `size_probe` + record in §7 after each.

### P3.1 — `DomElementExt` (`ext`)  **[DV6]**
- New struct in `dom_element.hpp` (tier-1, doc-pool per OQ1 default): fragment unions as `FragmentUnion frags[4]` + presence mask (~64 sites), five pseudo `StyleTree*` (~62 sites), `multicol` (124), `vpath` (3), `backdrop_filter` (19), `custom_layout_paint` (22), `layout_fragments`+count (11), `shadow_host`/`shadow_root` (12), pending element scroll floats (28).
- `ensure_ext(doc)` + null-tolerant read helpers (`el->pseudo_styles(PSEUDO_BEFORE)` returns null when `ext` absent). The ~350 total sites port with the move (they're cold paths; hand port, no scripting needed).
- **Care:** fragment-union fields are written during inline layout (tier-2-ish usage inside a tier-1 ext) — confirm they are reset per relayout today (they are re-computed each pass); add the reset to the relayout entry that currently resets `styles_resolved` so stale unions can't leak across passes. Root-cause comment at the reset.
- **OQ1 measurement:** count `ext` allocation rate on the layout corpus (log at doc teardown). If >30% of elements allocate `ext` mainly for pending-scroll or fragment unions, revisit the split (`ext`/`ext_view`).
- Expected: −(84 + 40 + ~56) ≈ −180 B → `DomElement` ≈ 412 B. Tighten ratchet.

### P3.2 — Intrinsic cache folds into `LayoutCache`  **[design §4 candidate]**
- Move `cached_min_content_width`/`cached_max_content_width` (+2 flag bits already in `elmt_flags`) into `radiant::LayoutCache` (43 sites for `layout_cache`; the intrinsic fields are ~17 sites). `measuring_intrinsic_width` re-entrancy guard stays a flag bit.
- **Care:** `LayoutCache` is lazily allocated — sites that cached intrinsics on elements *without* a layout_cache now allocate one; verify allocation-rate delta is noise on the corpus.
- Expected: −8 B.

### P3.3 — `native_element` removal  **[existing TODO, 148 sites]**
- Replace with `dom_element_to_element(de)` (offset math, already exists); the field's null-means-anonymous semantics move to a flag bit (`ELMT_FLAG_SYNTHETIC` for anonymous/pseudo elements whose embedded `elmt` is not Lambda-tree-linked).
- **Care:** sites using `native_element == nullptr` as the anonymous test (~30 of the 148) must use the new flag; the rest are mechanical `&de->elmt` substitutions. MarkEditor entry points assert the flag rather than the pointer.
- Expected: −8 B → ≈ 396 B.

### P3.4 — DomText slimming  **[DV7]**
- Delete `color` (dead; two sites — `dom_element.cpp:2298` clone copy, `render_text.cpp:180` guarded read — both verified dead 2026-07-18).
- `content_type` → `node_flags` bit on `DomNode` (fits existing padding next to `layout_dirty`; 41 `content_type` sites, most are `is_symbol()` which keeps its signature).
- **OQ2 spike:** verify `text`/`length` always mirror `native_string->chars`/`len` (audit the symbol path and `dom_text_set_content`); if yes, replace fields with inline accessors (−16 B). Ship as its own commit so it can revert independently.
- Expected: 136 → 120 B (→ 104 B with OQ2). Tighten ratchet; update the `sizeof(DomText) % alignof(uint32_t)` inline-String assert interaction (the `[DomText][String]` block layout in `dom_text_to_string` — **any DomText size change must keep that offset math and re-verify the UI-mode MarkBuilder co-allocation**).

### P3.5 — Ratchet finalization + perf check
- Final `size_probe` run; set the campaign ratchets (`DomElement` expected ≤ ~400 B here; ≤ 384 stretch if P3.2/P3.3 both land clean). Release-build layout timing on the benchmark set vs baseline commit; budget: no regression >1% median. Record both in §7.

---

## P4 — Derived computed style  **[DV9]**

### P4.1 — Property table
- New `radiant/css_prop_table.cpp` + header entry: `CssPropAccessor { id, group_kind, offset, value_kind, serialize, derive }` per design §DV9. Initial supported set (OQ3): drive from what the test corpus + `js_dom` currently query — enumerate `getComputedStyle`/style-read usage in `test/dom/`, `test/js/`, WPT harness before writing the table; the enumeration is this task's first deliverable.
- Table layout kept compatible with Jube DOM3 dispatch constraints (D0a–D0d, `vibe/Lambda_Jube_DOM3.md`); coordinate the row shape before landing.

### P4.2 — `dom_ensure_computed(el)` flush entry  **[DV9a]**
- Forces style resolution for the element's dirty ancestor chain; for used-value properties (width/height/inset family — flagged per row), forces layout if `layout_dirty`. Single entry point; both JS `getComputedStyle` and internal queries route through it.
- **Care:** re-entrancy (a flush triggered from within layout must be rejected — assert via the existing pass-scope state); incremental-layout interaction (`skip_style_reset`/`incremental_layout` flags gate what "force" means).

### P4.3 — Wire consumers
- `js_dom` `getComputedStyle` path reads via the table (replacing per-property ad hoc code); properties not in the table return empty string + one-time `log_debug` (explicit unsupported, DV9d — no silent wrong answers).
- Absent prop group ⇒ serialize from the canonical default instance (DV4 pays off here: table code never null-checks).

### P4.4 — Tests
- gtest: table completeness (every row's group/offset/kind round-trips against a synthetic element); flush semantics (mutate style → query → assert fresh value without explicit relayout call).
- Layout-level: `getComputedStyle` assertions for the supported set on a fixture page; goldens per rule 8.

---

## P5 — Scratch migration + method grouping  **[DV10/DV12 — incremental, can trail]**

### P5.1 — Flex/grid scratch out of `*Prop`
- `FlexItemProp`: `hypothetical_*`, `resolved_*`, `intrinsic_*` caches → a `FlexItemBox` in `layout.hpp` keyed by the pass (the multipass algorithms already carry per-item arrays; attach there). Persistent style residue (`flex_basis`, `flex_grow/shrink`, `align_self`, `order`, `aspect_ratio`) stays — target ≤ 32 B.
- `GridItemProp` likewise (`measured_*`, `track_*` → `GridItemBox`; placement style stays).
- **Care:** the design allows deferral where multipass genuinely needs cross-pass persistence — the deliverable is *either* the move *or* a `// tier-3 scratch` tag with the blocking reason; no silent leftovers.
- Expected side effect: smaller item props make flex/grid-heavy pages cheaper per element; measure.

### P5.2 — `TableCellProp` per-layout indices
- `col_index`/`row_index`/`intrinsic_width` → table layout's existing metadata arrays (`TableMetadata` in `layout.hpp`) if reachable; else tag as scratch. Collapsed-border resolved pointers stay (render-phase consumers).

### P5.3 — Methodization + C-ABI trim  **[DV12]**
- Move tree/attr/class free functions to methods (`dom_element_get_parent` → `DomElement::parent_element()`, etc.); keep C-ABI one-line wrappers only for symbols the JS bridge/Jube actually links (audit `js_dom.cpp` + `sys_func_registry.c` for the live set; delete the rest).
- One subsystem per PR; grep-verify zero remaining callers before each deletion.

---

## P6 — DomDocument grouping  **[DV13 — independent; any time after P0]**

- Group into named sub-structs (member access rewrites only, no behavior): `DomJsRuntime` (`js_mir_ctx`, `js_preamble_state`, `js_runtime_*`, `js_mutation_*` records + counters, `js_ready_state`, `js_doc_node`), `ViewportMeta` (`viewport_*`, `given_scale`, `scale`, `body_transform_scale`), `ReconcileLog` (`last_dom_reconcile_*`).
- The `void*` typed-as-comment fields (`keyframe_registry`, `cached_css_engine`, `mem_ctx`) keep their opacity but move into the relevant group.
- Gate: build + baseline; ~200 mechanical site edits.

---

## 7. Size + LOC tracking (updated per phase; LOC = `verify_loc_reduction.sh --ref 019f47214` cumulative net)

| Milestone | `DomNode` | `DomText` | `DomElement` | net LOC | Notes |
|---|---|---|---|---|---|
| Baseline `019f47214` | 80 | 136 | 584 | 0 | measured 2026-07-18 |
| after P1 | 80 | 136 | 584 | expect ≤ 0 | null-check collapse should already pay for the defaults file |
| after P2.1 | 80 | 136 | ≤592 | ↓ | +8 B second union slot (transient) |
| after P3.1 `ext` | 80 | 136 | ~412 | ↓ | target |
| after P3.2+P3.3 | 80 | 136 | ~396 | ↓ | target |
| after P3.4 | 80 | 120 (104 w/ OQ2) | ~396 | ↓ | target |
| after P4 | 80 | — | — | expect flat-to-↓ | table rows retire ad hoc code batch-by-batch |
| campaign end | 80 | ≤120 | ≤400 (stretch 384) | expect −1500 or better | ratchets set in P3.5 from measurements |

## 8. Sequencing & risk register

- **Critical path:** P0 → P1.1 → P1.2 (hot fields) → P2 → P3. P1.3 must complete before P4.3. P4 needs P1.1 (defaults) + P1.3 (aliasing ledger) but not P3. P5/P6 float.
- **Biggest schedule risk:** P1.2 volume (~6–7k sites). Mitigation: scripted rewrites, per-field PRs, both styles legal during transition — there is no flag-day.
- **Biggest correctness risks:** (a) P1.1 default-instance values diverging from resolver-written values on paths the resolver skips — mitigated by ship-zeros-first-then-tighten; (b) P3.1 fragment-union reset across relayout — explicit reset + comment; (c) P3.4 DomText size change vs the `[DomText][String]` co-allocation offset math — dedicated assert + MarkBuilder audit; (d) P4.2 flush re-entrancy — pass-scope assert.
- **Rollback:** every P3 move is a single-struct commit; reverting one restores the prior layout without touching ports (accessors absorb the difference — that is why P1 comes first).
- **LOC risk:** the P1.2 rewrite script should produce the *collapsed* form (`el->block()->x`), not a 1:1 rename (`el->blk` → `el->ensure_blk()`), or the largest natural deletion source evaporates — review the script's output pattern on the first (smallest) field before scaling. And P4 lands table rows together with the retirement of the code they replace (ground rule 7: no shadowed legacy paths).

## 9. Tier audit ledger (filled during P0.1)

*(seeded; extend while tagging)*

| Holder | Field | Pointee tier | Verdict |
|---|---|---|---|
| `DomElement` (t1) | `font/bound/blk/...` (t2) | t2 | sanctioned: unified tree; props rebuilt per relayout (DV9b) |
| `DomElement` (t1) | `transition_state` | t1 (doc pool) | correct; the header comment moves to the tier tag |
| `DomNode` (t1) | `view_state_ref` | t1 (DocState) | weak ref; document invalidation on epoch bump |
| … | | | |
