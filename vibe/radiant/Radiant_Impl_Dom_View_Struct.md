# Radiant DOM/View Struct Refactoring — Implementation Plan

**Date:** 2026-07-18
**Baseline commit:** `019f47214`
**Parent design:** `vibe/radiant/Radiant_Design_Dom_View_Struct.md` — decisions DV1–DV16, findings F1–F8, target sketch (§4). This doc turns those into execution-grade tasks.
**Related:** `vibe/radiant/Radiant_Imp_Code_Dedup.md` (header consolidation), `vibe/radiant/Radiant_Design_Robustness.md` (T7 stale-View), `vibe/Lambda_Jube_DOM3.md` (property-table dispatch), `doc/dev/C_Plus_Convention.md`.

**Campaign status:** P0–P6 complete. All implementation phases and refactor-specific acceptance gates are complete; repository-wide baseline defects encountered by the aggregate gate are isolated and recorded in §16. **P7 (DV16 constructor retirement, added 2026-07-18) is a follow-up phase — not started.**

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

## P7 — Constructor retirement: static `create()` construction  **[DV16 — follow-up, COMPLETE]**

*(Added 2026-07-18 after campaign completion. The P0–P6 campaign left the C++ constructors untouched; they remain a fourth init path that production never runs and that has already drifted from the real ones. This phase establishes the C+ struct-based construction convention.)*

**LOC expectation (ground rule 7): P7 must come out net-negative.** It is pure consolidation — four constructor init lists plus the duplicated field stores at every allocation site (`dom_element_create`/`dom_element_init`, `mark_builder.cpp`, `lambda-data-runtime.cpp`) collapse into one `create()` per type; nothing new is added except those functions. Verify with `verify_loc_reduction.sh --ref <pre-P7 commit>` over the touched set at phase end; a positive reading means an old init path wasn't fully retired.

### P7.1 — Remove the constructors
- Delete the constructor init lists on `DomNode` (dom_node.hpp:186), `DomText` (:236), `DomComment` (:376), `DomElement` (dom_element.hpp:702). The structs become trivially constructible; add `static_assert(std::is_trivially_copyable_v<T>)` for each node type next to the existing overlay/size asserts.
- **Pre-audit:** find every site that currently relies on a constructor running — stack/local declarations (`DomElement el;`, `DomText t;`), any placement `new`, any `{}`-value-init that assumed ctor defaults (grep + compile-probe: temporarily `= delete` the ctors and let the compiler enumerate). Expected: tests + the `dom_element_init` "stack-allocated" path only.
- `DomNode`'s protected ctor was also what made the types non-standard-layout — after removal, revisit the `-Winvalid-offsetof` pragma block in `dom_element.hpp` (it may become unnecessary; delete it if so).

### P7.2 — Static `create()` member functions
- One per type, **combining `arena_calloc` + sparse non-zero stores in a single function** — the zeroed-storage contract stated in the function comment; null/0 fields are never written again:
  - `DomElement::create(DomDocument*, const char* tag_name, Element* backing)` — absorbs `dom_element_create` + the field-init half of `dom_element_init` (the non-zero stores: `node_type`, node id, `doc`, `display = {CSS_VALUE__UNDEF, …}`, tag retain/id, `specified_style` tree, `ELMT_FLAG_SYNTHETIC` handling).
  - `DomText::create(String*, DomElement* parent)` / `DomText::create_detached(String*, DomDocument*)` — absorbs `dom_text_create*`.
  - `DomComment::create(Element*, DomElement* parent)` / `create_detached` — absorbs `dom_comment_create*`.
  - `mark_builder.cpp` UI-mode and `lambda-data-runtime.cpp` allocation sites call the same `create()` (or a `create_in(Arena*)` variant where no `DomDocument` exists yet) — **no site keeps its own field list**.
- Existing `dom_*_create` free functions: keep only those the JS bridge/Jube links as one-line shims; delete the rest (grep `js_dom.cpp` + `sys_func_registry.c` for the live set).
- `dom_element_init` survives only if a genuine re-init-in-place consumer remains after the audit; otherwise it folds into `create()` and is deleted.

### P7.3 — Resolve the init divergences (canonical values)
- `display`: ctor's `CSS_VALUE_NONE` vs `dom_element_init`'s `CSS_VALUE__UNDEF` (=0, dom_element.cpp:307 — "critical for table elements"). Canonical: **`CSS_VALUE__UNDEF`** (the arena paths' de facto behavior). Root-cause comment at the store.
- `inline_line_number`: `DomNode` ctor's −1 vs arena paths' 0. Audit consumers of the −1 sentinel (`inline_line_number >= 0` style checks); pick the arena paths' 0 unless a consumer proves the sentinel is load-bearing, in which case `create()` sets −1 explicitly (it is then a documented non-zero default, not a divergence).
- Gate: baseline 100% — any diff traces directly to one of these two values.

### P7.4 — Record the convention
- Add a "Struct construction" section to `doc/dev/C_Plus_Convention.md`: *nodes and pool-allocated structs use static `create()` (zeroed storage + sparse non-zero stores, no C++ constructors); prop groups use `ensure_*()` (lazy alloc + memcpy from canonical default, DV4). Implementation chosen by default density.*
- LOC: net negative (four init lists + duplicated allocation-path field stores → one `create()` per type). Gates: `make build`, `make test-radiant-baseline` 100%, `make layout suite=baseline` clean, `verify_loc_reduction.sh` over the touched set.

---

## 7. Size + LOC tracking (updated per phase; LOC = `verify_loc_reduction.sh --ref 019f47214` cumulative net)

| Milestone | `DomNode` | `DomText` | `DomElement` | net LOC | Notes |
|---|---|---|---|---|---|
| Baseline `019f47214` | 80 | 136 | 584 | 0 | measured 2026-07-18 |
| after P1 | 80 | 136 | 584 | not snapshotted | canonical defaults, read accessors, and ensure writers landed |
| after P2.1 | 80 | 136 | 552 | not snapshotted | flag consolidation absorbed the second union slot |
| after P3.1 `ext` | 80 | 136 | 384 | not snapshotted | cold state moved behind the extension |
| after P3.2+P3.3 | 80 | 136 | 368 | not snapshotted | intrinsic widths folded into `LayoutCache`; synthetic flag replaced pointer |
| after P3.4 | 80 | 120 | 368 | not snapshotted | OQ2 rejected: symbols and first-letter slices intentionally use non-mirroring text/length |
| after P4 | 80 | 120 | 368 | not snapshotted | 78-row table replaced the JS computed-style switch |
| campaign end | 80 | 120 | 368 | +329 | final exact ratchets; LOC is the non-blocking production-source check from rule 7 |
| after P7 | 80 | 120 | 368 | +197 | P7 itself removed 132 production LOC; node sizes are unchanged |

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
| `DomElementExt` (t1) | `layout_fragments` | t2 (view pool) | sanctioned: extension slot is reset and rebuilt on every relayout |
| `DomElement` (t1) | `layout_cache` | t2 (view pool) | sanctioned: reset with the view pool; never durable style authority |
| `DomElementExt` (t1) | `pseudo` / `vpath` / `multicol` / `backdrop_filter` | t2 (view pool) | sanctioned: cold view data is grouped behind `ext` and rebuilt |
| `DomElement` (t1) | `shadow_host` / `shadow_root` | t1 (doc pool) | correct; DOM ownership links survive relayout |

## 10. Campaign completion matrix

| Phase | Status | Result |
|---|---|---|
| P0 | complete | Lifetime-tier tags and ledger, consolidated flags/tags, and exact size ratchets are in place. |
| P1 | complete | Canonical CSS-initial defaults, null-tolerant read accessors, and centralized ensure writers cover the tree-side property groups. Direct allocation residue was reduced to the documented pre-ViewTree async image seam. |
| P2 | complete | Parent-item and element-role unions are independent. Table/form collision workarounds and the shadow flex/grid fields in `FormControlProp` were removed. Four layout fixtures cover table flex/grid, form flex/grid, and nested table-in-flex-in-grid cases. |
| P3 | complete | Cold extension, intrinsic-cache move, synthetic flag, and `DomText` slimming landed. Exact final sizes are 80/120/368 B. |
| P4 | complete | The immutable 78-row computed-style table, flush entry, JS routing, unsupported-property logging, gtests, and live mutation UI fixture landed. |
| P5 | complete | Tree/attribute operations are methods and obsolete wrappers have zero callers. Scratch fields were moved where a stable owner exists; the explicitly allowed tagged deferrals are recorded in §14. |
| P6 | complete | JS runtime, viewport, reconcile, and opaque service state are grouped under named `DomDocument` sub-structures. |
| P7 | complete | Node constructors and the free `dom_element_init` path are retired. Static factories own arena/pool allocation and canonical initialization; only bridge-linked free creation shims remain. The C+ construction convention and verification evidence are recorded below. |

## 11. P1 shared-group alias ledger

The final pointer-assignment sweep found no live `DomElement`-to-`DomElement` alias of `font`, `in_line`, `bound`, `blk`, `position`, `embed`, or `scroller`.

| Site/category | Result | Decision |
|---|---|---|
| anonymous table boxes (`layout_table.cpp`) | inheritable font/inline fields are copied through shared helpers; only the pool-owned family string is retained | element-owned and mutable after cascade |
| generated pseudo-elements (`layout_block.cpp`) | font/boundary/inline pointers are deliberately left null and independently ensured | element-owned; comments protect the no-sharing invariant |
| `DomText::font = lycon->font.style` | pass-context association for text layout, not an element group alias | valid tier-2 association |
| replacement text node font copy | preserves the same text-run font during node replacement | valid text-node lifecycle copy |
| canonical defaults | returned only as `const*`; writers must call an ensure method | immutable process-wide defaults |

## 12. P3 measurements and decisions

- Final size probe: `DomNode=80`, `DomText=120`, `DomElement=368`; supporting sizes were `FontProp=96`, `BoundaryProp=160`, `InlineProp=52`, `BlockProp=256`, `FlexItemProp=96`, `GridItemProp=144`, `TableCellProp=64`, and `LayoutCache=412` bytes.
- OQ1 representative corpus: 2,187 elements, 328 extension allocations (15.0%), and 124 layout-cache allocations (5.7%). The overall extension rate is half the 30% reconsideration threshold, so the unified document-pool `DomElementExt` remains the selected split.
- OQ2 was rejected: symbol text and first-letter slices intentionally permit `text`/`length` views that do not mirror the backing `String`, so those fields remain.
- The synthetic backing regression exposed by full baselines was fixed at allocation: pool-created anonymous DOM elements start with `ELMT_FLAG_SYNTHETIC`. Backed element access therefore never infers backing identity from zero-filled flags.

## 13. P4 query census and coverage

The pre-table census found 42 `getComputedStyle` call sites across the UI/WPT harnesses and bundled framework/library fixtures. They cluster around geometry, box edges, typography/color, overflow, flex/grid alignment, animation/transition, transform/filter, direction, and generated content.

The table contains 78 unique property rows. The UI fixture asserts 11 initial reads (`display`, `position`, width/height, margin/padding, color/font size, opacity, overflow, and box sizing) and three fresh reads after mutation. Unit tests verify unique IDs, direct offsets/default groups, serialization, and mutation-triggered freshness. `content-visibility` remains an explicit special case because the CSS property enum has no row ID; unsupported enumerated properties return an empty string and log once.

## 14. P5 scratch and method audit

- Flex/grid multipass fields that cannot yet move without inventing a second synchronization structure carry `// tier-3 scratch` tags. Their algorithms enter through separate measurement/finalization APIs and do not expose a stable pass-keyed owner across those entry points.
- `TableCellProp::col_index`, `row_index`, and intrinsic width remain tagged scratch because collapsed-border/render consumers outlive the readily reachable `TableMetadata` lookup window. Moving them now would replace a direct lifetime contract with repeated table-tree searches.
- The methodization sweep covers parent/child traversal, attributes, classes, and child mutation. `js_dom`, the Radiant bridge, Jube registry, and system registry have no live references to the deleted `dom_element_*`/`dom_node_*` wrapper symbols; remaining old spellings are log labels only.

## 15. LOC consciousness check

The final scoped check against `019f47214` is **+329 production LOC**: tracked production files are −485 lines and the three new production files add 814 lines. Tests, goldens, and this plan are excluded per rule 7. This missed the optimistic reduction estimate, but the rule explicitly makes the check non-blocking. The positive balance is the new 78-row table/serializers and canonical default/ensure infrastructure; the audit nevertheless found and removed real residue: duplicated anonymous-table inheritance blocks, the form-control flex/grid shadow storage, the table-vs-flex serializer workaround, and direct property-group allocation sites. No superseded computed-style switch, collision path, or C-ABI wrapper path remains.

## 16. Final verification ledger

### Refactor-specific gates

- `make build`, `make build-test`, and `make release` pass.
- The targeted DOM/CSS/layout suites pass: 11 custom role-collision fixtures, 17 CSS animation/property-table tests, 15 CSS style-application tests, 63 CSS DOM CRUD tests, 102 CSS DOM integration tests, 49 HTML/CSS tests, and 39 Lambda `DomNode` tests. Declared skips remain skips.
- `make layout suite=baseline` passes **4,379/4,379** comparisons with six declared skips. The dedicated table/form flex/grid fixtures also match their goldens exactly.
- The release benchmark used 11 alternating rounds, each containing 30 layout invocations over the ten-page corpus. Median time changed from **1.44 s** at `019f47214` to **1.42 s** after the refactor (about 1.4% faster), satisfying the no-more-than-1% regression budget.
- The Radiant integer-cast lint rule passes. The implementation briefly introduced a per-file property-table header; the declarations were consolidated into `view.hpp`, and no implementation-added header violates the header-consolidation rule.

### Repository-wide aggregate baseline defects

`make run-radiant-baseline RADIANT_RENDER_JOBS=1` exercised all aggregate suites but does not return green for three defects outside this refactor. They were isolated instead of being hidden or worked around:

- The release UI rich-text-editor test exits with signal 11 at the same assertion point with both the final binary and a binary built from `019f47214`; the final debug binary passes all 27 assertions. Symbolized inspection places the failure in the pre-existing release JIT event-handler path, where `js_args_push(1)` supplies an invalid address, rather than in DOM/View storage.
- The WPT CSS-syntax aggregate reports 18 passes against a hard-coded threshold of 25. The failures concern existing syntax coverage and do not touch the DOM/View refactor.
- The render Node process reports its completed test summary (all 211 baseline render tests pass, with the expected failures/skips) and then hangs in Node/V8 worker shutdown. This is post-test process teardown rather than a render-content failure.

Full `make lint` likewise remains red only because the untouched, baseline-present `radiant/resource_resolver.hpp` is not in the existing per-file-header allowlist. The lint output confirms that this campaign adds no new violation. These defects are recorded as baseline conditions rather than addressed with unrelated scope expansion.

## 17. DOM/View arena memory profile

The final implementation and baseline `019f47214` were profiled with release builds across all 46 HTML files in `test/layout/data/page/`. An identical temporary diagnostic hook captured the memory-context allocator graph immediately after `layout_html_doc()` and before rendering, excluding paint surfaces, renderer allocations, JIT memory, and framework RSS. Each page ran in a fresh process. The hook was removed after capture; raw per-page results are retained in `temp/dom_view_memory_comparison.csv` and the aggregate report in `temp/dom_view_memory_summary.json`.

| Post-layout metric, summed across 46 independent page runs | Baseline | Final | Delta | Page outcomes |
|---|---:|---:|---:|---:|
| DOM arena used | 3,639,424 B | 3,193,344 B | **−446,080 B (−12.26%)** | 46 improved / 0 unchanged / 0 regressed |
| View arena used | 490,544 B | 490,544 B | 0 B (0%) | 0 / 46 / 0 |
| Combined arena used | 4,129,968 B | 3,683,888 B | **−446,080 B (−10.80%)** | 46 / 0 / 0 |
| Combined arena reserved | 5,431,392 B | 4,956,256 B | **−475,136 B (−8.75%)** | 9 / 37 / 0 |

Median combined arena use per page fell from 31,240 B to 28,192 B (−9.76%). Median reserved capacity fell from 65,536 B to 40,960 B; only nine pages crossed an arena chunk boundary, which is why most pages have lower used bytes but unchanged reserved capacity.

Largest absolute DOM-arena reductions were `underscorejs` (−139,104 B), `cnn_lite` (−29,936 B), `linuxmint` (−26,128 B), `flex` (−26,000 B), and `html5-kitchen-sink` (−24,288 B). `cnn_lite` had the largest reduction among those pages by percentage at 20.4%.

The arena result does not describe every allocation owned by the trees. `ViewTree::alloc_prop()` and cold document extensions allocate directly from their backing pools. Including the complete `dom.document` and `view_tree.pool` cumulative allocation counters gives 28,277,476 B at baseline and 28,325,328 B after the refactor: **+47,852 B (+0.17%)** in aggregate, while the median page falls from 292,688 B to 283,350 B (−3.19%). The near-flat aggregate includes the intentional cost of independently coexisting item/role props, lazy layout caches, and cold extensions; unlike arena used bytes, rpmalloc-backed pool counters are cumulative allocation upper bounds rather than exact live-byte measurements.

## 18. P7 completion evidence

- `DomNode`, `DomText`, `DomComment`, and `DomElement` have no C++ constructors. Compiler-trait assertions (`__is_trivially_copyable`) protect all four types without introducing a forbidden C++ standard-library dependency. The `-Winvalid-offsetof` suppression remains necessary: `DomElement` is still non-standard-layout because both its base and derived class contain storage, independently of constructors.
- `DomElement`, `DomText`, and `DomComment` now expose static `create()`/`create_in()` factories. All document-arena, view-pool, MarkBuilder UI, runtime, editor, layout, and test call sites use those factories or explicit zero-initialized test fixtures. Raw `arena_calloc`/`pool_calloc` allocation of these node types exists only inside the factories.
- The UI-mode fat `DomElement` is a genuine reused-storage consumer. Its `create_in(storage, ...)` overload replaces `dom_element_init` and resets old tree links, attribute caches, style-resolution state, and synthetic identity before rebinding; this prevents a previous DOM epoch from being spliced into a rebuilt tree.
- The canonical display default is `CSS_VALUE__UNDEF`, with the table-first-resolution invariant documented at the store. `inline_line_number` is zero until assigned to an atomic-inline view; the consumer audit found no negative-sentinel test, only local `-1` return fallbacks, so the zeroed arena behavior is canonical.
- The only retained `dom_*_create` free functions are one-return bridge shims used by `lambda/js/js_dom.cpp` and `lambda/module/radiant/radiant_dom_bridge.cpp`: element, attached/detached text, and detached comment creation. `dom_element_init` and all unlinked creation wrappers have zero declarations or callers.
- `doc/dev/C_Plus_Convention.md` now specifies static factories for zeroed node/pool storage and density-based selection between sparse factory stores and canonical-default `memcpy` for property groups.

Verification on the final implementation:

- `make build` and `make build-test` pass. Eight focused DOM/CSS/layout executables pass 185/185 tests.
- The UI suite passes 239 tests with two declared headless skips; this includes the reused-storage selection fixture that originally exposed stale epoch links.
- `make layout suite=baseline` passes 4,379/4,379 comparisons with six declared skips. The broader Radiant target also passes its layout, page (46), UI, view (21), page-load (104), fuzzy-crash (24), and render baselines; its final aggregate exit remains the separately recorded CSS-syntax threshold defect (18 passes versus 25 required), whose failures are tokenizer/encoding cases outside DOM construction.
- Constructor, placement-new, raw-allocation, legacy-initializer, wrapper-caller, and `inline_line_number` audits are clean against the contracts above; `git diff --check` passes.
- P7 production code is 214 additions and 346 deletions, **net −132 LOC**. The required full touched-set check (production, tests, and documentation) reports 64,159 lines before and 64,074 after: **net −85 LOC**, passing `verify_loc_reduction.sh --ref 1183c28ef2af60337988cc0afb7c965687da72dd`.
