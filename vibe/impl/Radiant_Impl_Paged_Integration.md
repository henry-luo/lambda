# Paged layout integration: one layout engine, breaks as the only paged step

**Status:** implementation in progress, started 2026-10-14. P1–P5 have focused flow gates; native/FO integration (P6) and removal (P7) have also started. The current tree has removed the item engine and its `RADIANT_PAGED_ENGINE=legacy` selector, the region trial engine and the external page policy. Earlier §9 entries describe their historical state. The obsolete secondary selector/container replay, item budgets and frame/table paint fallbacks are now removed too. P2 retains shared boundary/inline paint and per-sheet fixed geometry; P3 separates caption wrappers from painted grids. Remaining paint combinations, the native-provider adapter, test migration and aggregate/release gates stay open; the latest §9 entry records current evidence. This plan supersedes the engine-level direction of the P4 "resumable main flow" milestone recorded in [Radiant_Impl_Paged_Media](Radiant_Impl_Paged_Media.md); the page-box level, the page model and the output contracts recorded there stay.
**Design authority:** [Radiant_Layout_Paged_Media §1.3](../radiant/Radiant_Layout_Paged_Media.md#13-design-principle-2026-10-14-paged-layout-is-normal-layout-with-breaks) (the principle this plan implements), §3.2, §5 and §11 for the page model.
**Formal linkage:** D4.5.1v4 and D4.1.4v5 (ownership of published editions and retained pages), D7.1.4v2 (headless profile). No formal ruling defines Radiant page layout; the vibe design doc is the authority for this plan.

---

## 1. Goal

`lambda render x.html --paged` and `view --paged` must produce pages whose content is laid out by the **same code** that lays out the browsing view: `layout_block`, `layout_inline`/`layout_text`, `layout_table`, `layout_list`, `layout_flex`, `layout_grid`, floats, positioning, counters, generated content, fonts and shaping. The only page-specific layout work happens at fragmentation points: where a block is placed, where a line is committed and where a block ends. Everything above the flow (page geometry, page sequences, margin boxes, counters, running content, footnotes, page floats, references) is **page composition** and never a second layout engine.

Two measurable outcomes:

1. **Consistency.** With a page tall enough to hold the document, paged geometry equals continuous geometry at the same content width, box for box. A document that breaks across pages differs from the continuous layout only by the struts inserted at the breaks and by the margins truncated there.
2. **Size.** The paged subsystem shrinks by roughly half (§6), and the CSS subset admitted into pages becomes the full browsing subset.

## 2. Findings (2026-10-14 audit)

Three independent reads of the source established the following. Line numbers are from the current tree.

### 2.1 The paged engine is a second layout engine

`radiant/layout_paged.cpp` (8,195 lines) never calls `layout_block`, `layout_flow_node`, `layout_inline`, `layout_text`, `layout_table` or `process_list_item`. It builds its own box tree (`PagedFlowNode`, line 58), its own paragraph items (`paged_text_items` 1117: one glyph per code point through `font_get_glyph`, no fallback fonts, no `text-transform`, no hyphenation, no shaping), its own block frames (`paged_frame_measure` 2553), its own table producer (`paged_table_build` 2331, `paged_table_measure` 4815, `paged_table_split` 5465), its own list markers (`paged_list_marker` 1571), counters (`paged_counter_property` 884), generated content, images, and three separate walkers over that tree (`paged_fixed_flow` 7257 for replay, `paged_flow_height_inner` 4584 for measurement, `paged_page_next` 6727 for breaking). It computes styles through a second cascade (`view_tree_css.cpp`, 1,706 lines) producing `ViewCssStyle`, not the `FontProp`/`BlockProp`/`BoundaryProp` props the browsing engine uses.

| Kind of code in `layout_paged.cpp` | Lines (±10%) |
|---|---|
| Re-implements browsing layout (box tree, whitespace and glyphs, block widths and margins, intrinsic sizes, tables, lines, images, markers, counters, generated content, replay walkers, fragment painting) | ~2,600 |
| Page-specific (page sequences and styles, break selection and trials, regions for notes/floats, margin boxes, marks, targets and convergence, native providers, fixed pages) | ~5,000 (about 1,000 of it native-provider plumbing) |
| Infrastructure (types, checkpoints, options, diagnostics) | ~500 |

Consequences the user sees today:

- `paged_context_admit` (1428) rejects flex, grid, inline-block, positioned boxes, side floats, element multicol, replaced content other than `<img>`, and MathML. Documents that the browsing view renders fail to export.
- Text differs from the browsing view: no fallback fonts, no shaping, no hyphenation, no `text-transform`, no font-feature handling.
- Every HTML element default had to be re-added to the second cascade (fixed on 2026-10-14 by sharing one table, but the structural cause remains).
- Each page is laid out at least twice: `typeset_page_plan` lays out and publishes while enumerating candidates, restores, and `typeset_page_commit` replays up to the chosen candidate. Relaxation retries (up to three), terminal-master re-measurement and sheet-policy enumeration multiply this.

### 2.2 The browsing engine has the hook points but no paged mode

- `LayoutContext` (`layout.hpp:3635`) has no print, paged or fragmentation flag. The scoped-flag precedents are `run_mode` (3671) and `table_cell_first_row_layout` (3694).
- Block placement: `layout_block_content` positions a child at `block->y = pa_block->advance_y` (`layout_block.cpp:7539`) and the parent advances at 10693 after the child finishes. The pre-layout hook is at 7539 / before `layout_block_inner_content` (9225); the post-layout hook is 10693 and `finalize_block_flow` (4039).
- Line commit: `line_break` (`layout_text.cpp:2505`) knows the used line height by 2770 and advances `advance_y` at 2777; the views of the line run from `line.start_view` to `lycon->view`. Text rect `y` values are fixed when allocated (4287–4309).
- Margin collapsing is applied after the fact in two places that move already-placed content **up**: parent–child top collapse (`parent->y += y_delta`, 10547) and sibling collapse (`block->y -= collapse`, 10598). `BoundaryProp::flow_margin` (`view.hpp:1868`) keeps the uncollapsed margins for fragmentation.
- `break-*`, `orphans` and `widows` are resolved into `BlockProp` (`view.hpp:2320–2324`) but only `layout_multicol.cpp` reads them.
- Multicol fragments **after** layout: it lays the subtree out once at column width and repositions boxes and text rects into columns (`layout_multicol_content` 10393; `multicol_fragmented_child_union` 6539; line partition `multicol_balance_line_to_column` 3364; orphans/widows `multicol_should_break_line_for_widows_orphans` 13). It writes per-column `LayoutFragmentBox` records (`dom_element.hpp:657`) that only simple-background painting consumes.
- Flat `@page` export already runs normal layout at the page's content width: `render_export_session_begin_internal` (`render_output.cpp:484–519`) resolves the page style, sets the viewport to the page content box and calls `layout_html_doc`.
- `LayoutMeasureScope` / `LayoutViewSnapshot` (`layout_pass.cpp:10–80`) snapshot and restore a subtree's geometry; `CounterCheckpoint` rolls counters back.

### 2.3 The output model is engine-neutral

`view_tree_model.cpp` publishes through four calls (`view_tree_page_append`, `view_tree_fragment_append`, `view_tree_native_fragment_append`, `view_tree_model_commit`). Tests hand-build pages and fragments and paint or export them with no engine (`test_view_tree_model_gtest.cpp:14477`). Consumers (`paged_paint_node`, `pdf_secondary_page`, preview arrangement, retained page generations, the snapshot cache) read `LayoutViewNode` fields only: `rect`, `role`, `paint_box`, `computed_boundary`, `glyph_run`, `image_box`, `clip_*`, `border_outset`, `first/last_fragment`, `source`, `text_start/length`, `table_range`. The couplings a new producer must keep are: `page->style` is a `ViewPageStyle` from `view_css_page_style`; `page->sequence`/`fixed`/`label` are `page_document` types; `ViewTreeModel::composition` is engine-owned and destroyed by `paged_composition_destroy`; `layout_secondary_target`/`_mark` read the composition's stores; committed nodes may borrow only memory owned through the `ViewTree` shell (retained editions struct-copy the shell).

No production code supplies `PagedLayoutOptions::native_flows` or `page_policy`; the external `TypesetFlowProvider`/`TypesetPagePolicy` contracts are exercised by tests only. `view_tree_page_instances_create` has no production caller.

### 2.4 Test base

`test_view_tree_model_gtest.cpp` holds 542 tests: 488 `SecondaryViewTest` (402 call `layout_secondary_view`), 30 `TypesetTest`, 7 `PagedCssTest`, the rest cascade/model/sizing fixtures. By content: plain CSS-HTML page flow ~167 (34%), tables 90, images 37, GCPM notes/floats/marks ~60 (12%), preview/instances/snapshots 38, model-only 33, FO/native/page programs ~190 (39%). `test_pdf_render_visual_gtest.cpp` has 35 paged CLI tests; 117 paged HTML fixtures and 110 FO/`.rpd` fixtures live under `test/html`.

## 3. Target architecture

```text
                       ┌──────────────── page composition (page-specific) ────────────────┐
                       │ page table: sequence, named/sided/blank pages, page geometry      │
                       │ margin boxes · page counters/labels · running marks · references  │
                       │ footnote/page-float reservations · native stream adapter          │
                       └───────────────▲──────────────────────────────────▲────────────────┘
                                       │ hooks at breaks                  │ publication
 layout_html_doc ──► normal block/inline/table/list/flex/grid layout ──► one tall flow in the
                     with lycon->paged != nullptr                        DOM-embedded views
                     (struts at page ends, truncated margins,             │
                      orphans/widows/keeps, forced breaks)                ▼
                                                                  slice per fragmentainer ──► ViewTree edition
                                                                  (LayoutViewNode fragments,   (pages, preview, PDF,
                                                                   boundaries, glyph runs)      retained generations)
```

### 3.1 Pages are fragmentainers of one continuous flow

The page flow is laid out once, by `layout_html_doc`, as a tall continuous flow at the page body's content width. A **page table** lists the fragmentainers in flow order: for each one, its absolute flow extent `[top, bottom)`, its page, column index and body rectangle. The table grows lazily: when the flow reaches the end of the current fragmentainer, the composition asks the page sequence for the next page (name, side, blank insertion, terminal master) and appends its fragmentainer(s). Body columns (`column-count` on the page body region, FO `region-body`) are simply consecutive fragmentainers of column width on the same page; no column machinery is needed in layout.

A break is a **strut**: when a line or a monolithic box would cross a fragmentainer end, the layout inserts the distance to the next fragmentainer's top before it, so the box starts at that top. The strut belongs to the box it precedes (`pagination_strut` on the view), and unforced breaks truncate the adjoining margins (CSS Fragmentation 3 §5.2). Nothing is laid out twice and nothing is rolled back; a box that must move is **shifted**.

### 3.2 The paged mode on `LayoutContext`

```c
struct PagedFlowState;            // owned by the composition, one per layout pass
LayoutContext::paged              // nullptr in browsing/flat layout
```

`PagedFlowState` holds: the page table and the current fragmentainer; the absolute flow origin of the block being laid out (accumulated in `layout_block` where the parent context is saved, so `abs_y = origin_y + block.advance_y`); the pending forced break (`page`, `left`, `right`, named-page or sequence change); the last legal break candidate (view, absolute y, keep strength); the current paragraph's line count on this fragmentainer (for orphans) and the previous fragmentainer's trailing line count (for widows); reservations that shrink the current fragmentainer (footnote area, page floats); the mark and target journals; budgets and the diagnostic.

Four hooks, all guarded by `if (lycon->paged)` and implemented in `layout_paged.cpp`, are called from the browsing engine:

| Hook | Call site | Paged behaviour |
|---|---|---|
| `paged_block_before(lycon, block)` | `layout_block_content` before the child is positioned (7539) | Consume a forced `break-before` (page/left/right/named page/sequence): advance the page table and insert a strut. Record a candidate boundary with the sibling keep strength (`break-after: avoid` on the previous sibling, `break-before: avoid` on this one, enclosing `break-inside: avoid`). Collapse the parent–child top margin eagerly when it is statically determined, so the post-hoc adjustment at 10547 does not move struts. |
| `paged_line_commit(lycon, height)` | `line_break` after the used line height is final (2770) and before `advance_y` advances (2777) | If the line crosses the fragmentainer end: insert a strut before the line. If fewer than `orphans` lines of this paragraph remain above, shift those lines down with it (`paged_shift_flow_after`). If the line is the first content after an avoided boundary, move the strut to the last legal candidate instead (bounded by the fragmentainer top; CSS Break §4.3 fallback order when nothing is legal). Reserve footnote/page-float space for calls on this line, and re-check the fit after the reservation. |
| `paged_block_after(lycon, block)` | `layout_block` after the parent advances (10693) and in `finalize_block_flow` | Consume a forced `break-after`. Widows: if the paragraph ended with fewer than `widows` lines on the new fragmentainer, move the break earlier by shifting more lines across. Record `break-after: avoid` for the next sibling's candidate. Close marks/targets whose scope ends here. |
| `paged_monolithic(lycon, block)` | Before an atomic box is placed: replaced boxes, flex/grid containers, inline-blocks, table rows (from `layout_table`), multicol containers, boxes with `break-inside: avoid` that fit a fresh fragmentainer | If it does not fit the remaining extent but fits a fresh fragmentainer, strut before it (and apply the keep rules). If it does not fit any fragmentainer, place it and let publication slice it (browsers slice; the old engine failed). |

The shift primitive `paged_shift_flow_after(view, delta)` translates a view and everything after it in flow order: the view and its following siblings move by `delta`, each ancestor's height grows by `delta` and the ancestor's following siblings move. It is the only geometry mutation the paged mode adds beyond struts; keeps, orphans and widows all use it. Multicol's existing descendant projection (`multicol_project_fragmented_descendants`) shows the pattern for translating lines and text rects.

The two post-hoc margin adjustments (10547, 10598) and sibling collapse only move content **up**, so they never create overflow; in paged mode they call `paged_shift_flow_after` on the first strut they cross so the content after the strut stays on its page. `paged_settle_struts` runs once after layout and re-derives every strut from its box's absolute position against the page table, correcting any residual drift in one forward pass.

Normal layout keeps all its other behaviour: floats, positioned boxes, counters, generated content, line clamp, baselines, intrinsic sizing, caches. A layout cache entry computed under `lycon->paged` is keyed by the page-table generation, so a browsing relayout never reuses paged geometry (design §5.5).

### 3.3 Page composition above the flow

- **Page table and sequences** (today's groups 4, 15, 16 in the audit): `@page` cascade and typed page styles stay in `view_tree_css.cpp` (`view_css_page_style`) and `page_document.cpp` (masters, sequences, regions, folios, labels). The page table is the new owner of "which page comes next".
- **Width changes.** The flow's content width is constant within a **run**. A page transition whose body content width differs (a named page with another size, a terminal master with another body region) starts a new run only at a block-level forced break of a top-level body child: `layout_html_root` resets the content width between children. A width change at an unforced break, or at a nested forced break, is diagnosed as unsupported in this plan (the FO/`.rpd` fixtures with alternating left/right page widths move to equal widths or to the §7 extension).
- **Margin boxes and static regions:** laid out per page at the margin-box geometry (CSS Paged Media 3 §5.3.2 sizing stays in `paged_margin_group`/`paged_margin_pair`). Page counters and labels are evaluated from the page table. *Refined in P4 (§9):* a running element, which is document content with a browsing style, lays out with the browsing engine at its margin box's width; generated text in a margin box keeps the page-context composer, because page contexts keep `ViewCssStyle` (§3.4, §4.6). Native and FO static regions follow with the native streams (P6).
- **Running marks and named strings:** recorded by the hooks at the committed flow position (`typeset_marks` stays as the store, 51 lines).
- **References and convergence:** `target-counter`/`target-text`/`r:folio-ref` read the previous pass's page assignments; the pass loop in `layout_secondary_view` (seeded targets, pass, capture, `typeset_convergence_observe`, bounded retries) stays, now over whole-document layouts. Terminal-master selection and `counter(page)` inside body text use the same loop.
- **Footnotes and page floats:** a call reserves space on its fragmentainer when its line commits (the note is measured once by laying it out at the note-area width inside a `LayoutMeasureScope`); if the reservation makes the line overflow, the line and reservation move to the next fragmentainer. Note areas and page floats are then laid out as ordinary blocks at the page bottom/top during publication. Notes are atomic in this plan (a note taller than the available area moves whole to the next page's area); note splitting and deferred floats are the §7 extension. This replaces `typeset_regions` (179 lines) and the 900-line region trial machinery.
- **Native streams** (`r:flow`, `r:static-content`, FO leaders/line-stacking/label-body): a native block in the flow is laid out by an adapter that pulls the provider's contributions and commits them through `paged_line_commit`, so struts, orphans and widows apply to native lines as to CSS lines. The producer contract (`TypesetFlowProvider` contributions, `typeset_next_line` with producer-owned alternatives) stays; the page-plan/commit/transition policy contract (`typeset_page_plan`, `TypesetPagePolicy`, hold/reinsert assembly) is retired with the trial model. Extensibility for a future TeX producer is preserved at the line level (material, metrics, break legality, alternatives) and at the page level through the break-candidate callback that replaces `choose`.
- **Fixed PDF pages** (`paged_layout_fixed_pages`) stay as they are; they never enter the flow.

### 3.4 Publication: slicing the flow into an edition

After layout settles, `paged_publish(tree, state)` walks the DOM views once, in flow order, and for each fragmentainer appends to the edition:

- a block fragment for every box whose absolute extent intersects the fragmentainer, with the page-local rect clipped to the slice, `first_fragment`/`last_fragment` from the slice position, and a `computed_boundary` built from the element's `bound` with the fragment-edge rules (`radiant_decoration_retain`, slice/clone, collapsed-table `border_outset`) that `paged_boundary_publish` already implements;
- a text fragment per `TextRect` on the page, with `text_start/length` and a `PaintGlyphRun` shaped from the rect's run (the same shaping the raster painter uses; glyph buffers are copied into the model arena as today);
- image fragments with `PaintImageBox`;
- table row-group, row, cell and column fragments with `table_range`, and **repeated header/footer occurrences** on continuation pages (`VIEW_FRAGMENT_REPEATED_TABLE`) placed by shifting the fragmentainer's body start;
- fixed-position boxes whose containing block is the page, repeated per page;
- note areas, page floats, margin boxes and static regions with their roles.

`LayoutViewNode::computed_style` stops pointing at a per-element `ViewCssStyle`; body fragments carry the few paint-relevant values (background, display inner, visibility, clip) in `computed_boundary` and a small `ViewFragmentPaint` record. Page contexts keep `ViewCssStyle`. The painter, PDF writer, preview arrangement, retained generations, snapshot cache and page instances are unchanged. Continuous secondary editions (`VIEW_PRESENTATION_CONTINUOUS`) use the same publisher with one unbounded fragmentainer.

Because publication copies everything an edition needs into the edition's own arenas, the DOM-embedded view is only a **workspace**: a second edition, or the browsing layout, can run afterwards. `view --paged` composes the paged edition and does not keep a browsing layout; the browsing window re-lays out when it is shown. This revises the ownership text in design §3.2 (see the principle there).

### 3.5 What goes, what stays

| Area of `layout_paged.cpp` (audit group) | Lines now | After | Replacement |
|---|---|---|---|
| Flow tree, items, whitespace/glyphs, anchors, admission, images, pseudo, markers, builder (1d, 1e, 1h–1l) | ~700 | 0 | normal layout |
| Counters (1c) | 112 | 0 | `layout_counters.cpp`, `CounterCheckpoint` |
| Table structure, measure, rows, emit, split, continuation (2a, 9a, 9b) | ~1,530 | ~150 | `layout_table.cpp` + row hook + repeated-group publication |
| Block frames, boundary publish, pure measurement, block steps, fixed flow (3, 8, 10, 12) | ~790 | ~250 | normal layout + publication edges |
| Line layer (6) | 378 | ~120 | `line_break` hook; leaders and FO line stacking through the native adapter |
| Regions, notes, floats (1g, 7) | ~910 | ~400 | reservations at the hooks + ordinary layout of note areas |
| Page provider, break selection, trials, sheet policy (0e, 11 minus native) | ~1,150 | ~500 | the four hooks, shift, candidates, settle |
| Native providers (0b part, 1a, 11-native) | ~1,000 | ~450 | native block adapter |
| Page sequences/styles, margin boxes, static regions, counters/labels, fixed pages (4, 13–16) | ~590 | ~550 | kept |
| Generated content, marks, targets, convergence, entry (0c, 1f, 5, 17–19) | ~870 | ~600 | kept, re-based on flow positions |
| Paint (20) | 146 | ~150 | kept |
| Infrastructure | ~500 | ~250 | `PagedFlowState`, options, diagnostics |
| **Total `layout_paged.cpp`** | **8,195** | **≈3,400** | |
| `view_tree_css.cpp` per-element cascade | ~1,000 of 1,706 | 0 | normal cascade with `print_media`; page contexts stay (~700) |
| `typeset_regions.cpp`, page-plan/policy half of `typeset.cpp` | ~400 | 0 | hooks; line breaking half of `typeset.cpp` stays for native paragraphs |
| New code in the browsing engine (hooks, strut field, shift helper, cache key) | 0 | ≈350 | |

Expected net: about **−5,500 lines** across the paged subsystem, while pages gain flex, grid, inline-block, positioned boxes, side floats, element multicol, every replaced element, MathML, fallback fonts, shaping, hyphenation and `text-transform`, and lose the double layout per page.

## 4. Decisions and scope boundaries

1. **Slice instead of fail.** A monolithic box taller than a fragmentainer is placed and sliced at publication, as browsers print it. The old engine's "requires a paged fragment producer" family of diagnostics is removed. Diagnostics remain for page-level errors (invalid page geometry, unresolvable references, budgets, unsupported width changes).
2. **Constant width per run** (§3.3) — *ruled by the user 2026-10-14 for the current phase.* Changed-width paragraph regeneration and per-page table track remeasurement are dropped. A future phase must support a specific page size at a chosen page, for example rotating one page from portrait to landscape; that is the §7 continuation-relayout extension. The common form of that case, a named page on a top-level block (`page: landscape` with its forced break), is already covered by the run restart.
3. **Notes are atomic; page floats keep top/bottom placement; no deferred-float queue** — *ruled by the user 2026-10-14; note splitting is a future extension (§7).* The hold/reinsert assembly and the region trial engine are retired.
4. **The external page policy is retired** — *ruled by the user 2026-10-14.* `PagedLayoutOptions::page_policy` and `TypesetPagePolicy` have no production users. The native producer/line contract for future TeX material stays; a `break_candidate` callback on the native binding replaces `choose` for producers that need a say in page breaks.
5. **Fragment granularity changes** from one fragment per typeset item (glue included) to one per text rect. Source-coverage tests move from byte-per-item to byte-per-rect accounting.
6. **One cascade.** Pages use the browsing cascade with `print_media`. The per-element secondary cascade is removed; page/margin/footnote-area contexts keep `ViewCssStyle`.
7. **The DOM-embedded view is the workspace.** Editions coexist only as published trees. `view --paged` does not maintain a browsing layout while paged.
8. **Page-box level stays.** `ViewTree` editions, `ViewPageBox`, preview arrangement, book spreads, page instances, the snapshot cache, the PDF page loop, `page_document` and FO translation are unchanged.

## 5. Phases and gates

Each phase ends with the listed evidence; the existing continuous-layout baselines (`make test-radiant-baseline`, layout and render baselines) must not change in any phase, since the hooks are inert without `lycon->paged`.

### P0: parity harness and test triage

- Add `RenderOutputParity.PagedFlowMatchesContinuousLayoutWithoutBreaks`: lay out the paged corpus (`test/html/paged_media_*.html`, the Markdown cheatsheet, three WPT fragmentation fixtures) with one unbounded page and compare every box rect with the flat `@page` export at the same content width. This test is the consistency gate for every later phase.
- Classify the 488 `SecondaryViewTest` cases: **keep** (model-only, preview/instances/snapshots, `@page` cascade, FO translation, page programs, page labels, margin boxes, references: ≈200); **rewrite** to text-rect granularity and relaxed admission (CSS flow, tables, images, notes, marks: ≈250); **retire** (tests that pin rejected contexts, trial internals, changed-width regeneration, hold/reinsert policies: ≈40). Record the list in this document before P1 starts.
- Freeze the current paged PDF rasters of the 117 HTML fixtures under `temp/paged-integration-baseline/` for before/after comparison.

### P1: paged mode in the browsing engine

- `PagedFlowState`, `LayoutContext::paged`, the strut field on the view, `paged_shift_flow_after`, the page table with lazy growth, the four hooks, eager parent–child margin collapse in paged mode, strut re-anchoring at the two post-hoc sites, `paged_settle_struts`, unforced-break margin truncation, the layout-cache key.
- Supported in this phase: blocks, paragraphs, lists, images, forced breaks (`page`/`left`/`right`/named), `break-inside: avoid`, `break-before/after: avoid` keep chains, `orphans`/`widows`, monolithic boxes, blank page insertion for sides.
- Gate: the parity test; the rewritten CSS-flow tests; forced-break and keep fixtures produce the same page assignments as before (compare page indexes per element against the frozen baseline); `layout_html_doc` runs exactly once per pass (assert through the profiler).

### P2: publication

- `paged_publish` for blocks, text rects, images, decoration edges, first/last flags, fixed-position repetition, continuous editions.
- Retire `paged_frame_*`, `paged_commit_line`, `paged_boundary_publish`'s frame plumbing, the `PagedFlowNode` tree and its three walkers.
- Gate: rasters of the P1-supported fixtures match the frozen baseline within the strut and margin-truncation differences (documented per fixture); PDF link annotations and named destinations unchanged; retained page generations, page instances and the snapshot cache tests pass unchanged.

### P3: tables

- Row boundary hook in `layout_table` row placement (rows are the atomic unit; a row taller than a fragmentainer is sliced); repeated `thead`/`tfoot` occurrences and the body-start shift they need; collapsed-border half edges on continuation pages (already in `layout_table`); captions as ordinary blocks.
- Retire the paged table producer, split and continuation code.
- Gate: the rewritten table tests; the 53 FO/native preview/PDF pairs that contain tables; `PagedTablesRepeatGroupsAndSplitCellsInPdfAndPreview` rewritten for row-atomic splitting.

### P4: page composition re-based on the flow

- Margin boxes and static regions through ordinary layout in a measurement scope; page counters/labels from the page table; marks and running elements from the hooks; references and convergence over whole-document passes; terminal masters through the same loop; body columns as fragmentainers.
- Gate: GCPM marks/strings/targets tests; margin-box tests; the FO page-program fixtures; convergence tests (9→10 and 99→100 page relabeling).

### P5: footnotes and page floats

- Reservations at `paged_line_commit`; note areas and page floats laid out at publication; `::footnote-call`/`::footnote-marker` through the normal cascade (UA rule stays at UA origin).
- Retire `typeset_regions` and the region trial code.
- Gate: rewritten notes/floats tests (atomic notes); `PagedPdfBacktracksLateBlockClosuresWithNotesAndFloats` rewritten to the reservation semantics.

### P6: native streams and FO

- Native block adapter over `TypesetFlowProvider`; FO leaders, line stacking, label/body grids through the adapter and ordinary tables; retire the page-plan/policy half of `typeset.cpp` and the external page policy.
- Gate: the 36 native-binding tests rewritten to the adapter; the FO/native pairs; `PagedViewerAdmitsNativeAndFoDocumentsThroughCommonLoaders`.

### P7: removal and measurement

- Delete the remaining dead paths, `PagedFlowNode`, `PagedFrame`, `PagedComposer`, `PagedCheckpoint`; shrink `view_tree_css.cpp` to page contexts; update `doc/HTML_CSS_SVG_Support.md` §8.1 (admitted contexts become the browsing set), `doc/dev/radiant/RAD_11` and a new `RAD_23_Paged_Composition.md`.
- Gate: `make test-radiant-baseline`, the full `test_pdf_render_visual_gtest.exe`, release timing of the paged corpus against the frozen baseline (expected ≥2× faster per page), and the line counts of §3.5 reported against the actual tree.

## 6. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Post-hoc margin collapsing moves struts | Eager collapse where static; re-anchoring at the two sites; `paged_settle_struts` as the backstop, with a debug assertion that the settled pass makes no change on the corpus |
| `line_break` has 25 callers; a hook that shifts lines mid-line could invalidate caller state | The hook runs only after the line is complete (post-2770) and before any later line starts; shifting touches committed geometry only |
| Shift cost on long documents (each shift is O(depth + following siblings)) | Shifts happen only at keeps/orphans/widows, bounded by a few lines or blocks; measure on the 21-page cheatsheet and a 300-page corpus |
| Tests pin item-level fragment granularity | P0 triage; rewrite helpers (`append_fragment_text`, `expect_source_range_coverage`) once |
| Width changes between pages lose support | Explicit diagnostic; §7 extension if a user corpus needs it |
| Native/FO extensibility narrows | The producer and line contracts stay; the retired page-policy contract had no production users; the design doc §3.4 note records the change |
| Browsing window after `view --paged` | The window re-lays out from the DOM when shown; no edition borrows DOM slots |

## 7. Extensions outside this plan

- **Continuation relayout** for a specific page size at a chosen page (required by the user for a future phase, e.g. one landscape page in a portrait document): when the page table's next fragmentainer has another content width, resume the open block chain from a break token (child index, text offset) at the new width. This is the bounded form of the original P4 design and can be added without touching the strut model; forced breaks on top-level blocks already get it from the run restart.
- **Note splitting and deferred page floats** over the reservation model.
- **Column balancing** for `column-fill: balance` on the last page of a body-column region.
- **Flex/grid row-level breaks** following CSS Flexbox/Grid pagination (currently monolithic, sliced).

## 8. Rulings (2026-10-14)

The user ruled on the three open questions the same day; §4 records each ruling in place.

1. Page widths are constant per run in the current phase. A future phase must support a specific page size at a chosen page (portrait to landscape rotation of one page); see §7.
2. Footnotes are atomic in this plan; splitting is a future extension.
3. The external page-policy contract is retired; the producer/line contract for future TeX material stays.
4. (2026-10-15) A split table cell keeps its `vertical-align` and applies it to each of its slices on its own, as the item engine did and the [design doc](../radiant/Radiant_Layout_Paged_Media.md) describes ("split cells align each consumed slice with its visible decoration edges"). CSS Fragmentation 3 and CSS Tables 3 leave the placement of a fragmented cell's content undefined. §9 (P3) records the implementation.

## 9. Progress record

### 2026-10-14: P1 hooks, P2 publication for the CSS flow, P0 gates

**What runs.** `paged_layout_pass` dispatches to `paged_flow_pass` (`layout_paged.cpp`, section "Page flow through the browsing engine") unless `RADIANT_PAGED_ENGINE=legacy` selects the item engine, which stays in the file for comparison until P7. The pass resolves the first page's body rectangle (`paged_page_spec` + `paged_page_style` + `view_css_page_column`, with the opening `page` name read from the first flow content), sets the host viewport and print media on the document's engine, recascades, calls `layout_html_doc(ui, doc, false, state)` once, adopts the document's mutation epoch, publishes, then runs the unchanged composition tail (sequence end, terminal masters, page counters, margin boxes, static regions).

- **Browsing-engine hooks** (`layout.hpp` declarations; all inert while `lycon->paged` is null): `paged_block_before` at the top of `layout_block_content` after `block->y = pa_block->advance_y`; `paged_block_after` in the `layout_block` epilogue after the parent cursor advanced; `paged_line_commit` in `line_break` after the used line height is final and before `advance_y` advances; `paged_block_moved` after the two post-hoc margin collapses; `paged_flow_skips_element` in `layout_flow_node` for page-control and static-content elements; `paged_flow_pass_begin`/`_settle` around each `layout_html_root` pass (a repeated container-query pass rewinds its pages through a model checkpoint). `layout_html_doc` takes the state as a fourth parameter.
- **Flow state** (`PagedFlowState`): the fragmentainer table grown lazily through `paged_page_create`/`paged_column_select` (sided breaks insert blank pages through `paged_requested_side`; body columns are consecutive fragmentainers), the strut journal (`PagedStrut`: box or paragraph line, amount, forced flag), per-depth strut counts for the open blocks, and the flow-ordered paragraph records (`PagedFlowParagraph`: one block container's inline run with its line tops and heights).
- **Breaks.** `paged_fit_line` moves a line that would cross its fragmentainer end to the next one with `paged_shift_flow`; a line alone at a fragmentainer top overflows instead. A line or box that already starts a later fragmentainer by position (the previous content filled the page exactly) is a break too: it is recorded as a zero strut so orphans, keeps and widows apply to it as to a strut. Orphans move the preceding lines of the run unless they start the fragmentainer (`relaxed_line_minima`); widows run at `paged_paragraph_close` by undoing the last strut and refitting from the earlier line. Forced `break-before/after` (page, left, right, recto, verso, column), named pages (`paged_flow_page_name` through `css_select_element_declaration` and the inline declaration block) and page sequences break before the box in `paged_block_before`; an unforced break before a box truncates its margin (CSS Fragmentation 3 §5.2) and keeps `flow_margin`. Monolithic boxes (replaced, flex, grid, tables until P3, multicol, `break-inside: avoid`) move whole in `paged_block_after` when they fit a fresh fragmentainer, undoing any struts their content made; taller ones keep their breaks and publication slices them. `paged_keep_chain` honours `break-before: avoid` and `break-after: avoid` by moving the previous in-flow siblings onto the box's fragmentainer, refusing (`relaxed_avoidance`) when a sibling has its own breaks or already starts its fragmentainer.
- **Coordinate facts the hooks rely on** (verified against `render_walk.cpp`): inline-level views and text rects are positioned in the enclosing block's frame, spans add no offset; a box's flow top is the sum of its block ancestors' `y` (`view_geometry_ancestor_offset(view, nullptr, true)`); a text rect may start above its line box (negative half-leading), so publication assigns rects and atomic inlines by their vertical midpoint.
- **Publication** (`paged_flow_publish`): one walk of the DOM views; a block fragment per fragmentainer it intersects (root box on every page), text fragments per rect with glyph runs built by the shared `render_text_rect_glyph_run` (promoted from the SVG and PDF text renderers in `render_text.cpp`), images through `render_geometry_block_content_rect` + `render_media_image_rect`, list markers as glyph runs (counters) or bullet boxes (disc, circle, square), boundaries copied from `BoundaryProp` with `box-decoration-break` edge rules. Synthetic boxes (markers, generated content, table fixups) publish as generated occurrences of their nearest authored ancestor. `paged_paint_node` falls back to square corners, then to the background colour, when the simple boundary painter declines a box (rounded visible borders, layered backgrounds).
- **Host:** `ViewEnvironment::ui_context` carries the `UiContext` (set by `render_output.cpp`, `render_paged_window_compose` and the test fixture's shared headless context).

**Three engine defects found on the way** (fixed, independent of pagination): the Markdown and other non-HTML loaders never stored their CSS engine on the document (`create_layout_css_document`), so no recascade (print media, viewport, container queries) could ever reach those documents in the browsing path; the style-epoch environment key omitted `print_media` (`style_epoch.cpp`), unlike the media-condition cache key; and the raster lowering of a text glyph run (`render_glyph_run_raster_lower.hpp`) emitted an empty bitmap for an inkless glyph such as a preserved line feed, which the display list rejects (the item engine never hit this path because it always supplied glyph ids).

**Measurements** (debug build, `doc/Lambda_Cheatsheet.md --paged`): legacy engine 21 pages in 15.0 s; page flow 22 pages in 0.54–0.73 s (the raster preview of the same document renders in about the same time). Page-1 ink extents agree within a pixel (left 57, top 86 vs 57, 85); inline `code` padding now renders as spacing (the item engine ignored inline padding), and `break-after: avoid` headings move with their paragraph where the item engine relaxed the keep.

**Gates.** `SecondaryViewTest.PagedFlowMatchesContinuousLayoutWithoutBreaks` (the §5 P0 parity gate: paged flow equals the continuous layout at the body width box for box, and every box publishes once at the body origin), `PagedFlowBreaksAreStrutsOverTheContinuousLayout` (boxes only move forward by whole struts, every text fragment lies inside its page body) and `PagedFlowCascadesPrintMediaBeforeLayout` pass. Browsing layout baseline (`run_radiant_checks.js layout`): unchanged (the same three pre-existing failures). The 488 legacy `SecondaryViewTest` cases, run one per process (`temp/paged_probe/secondary_status*.tsv`), split as below; the failures are the fragment-granularity and admission changes §5 P0 anticipated, plus the phases not started.

| Bucket (by test name) | pass | fail | crash (test-side null dereference) | Phase |
|---|---|---|---|---|
| CSS flow | 62 | 13 | 0 | P1 (see below) |
| images | 16 | 9 | 0 | P2 |
| tables, captions, spans | 11 | 68 | 6 | P3 |
| marks, strings, targets, counters | 4 | 24 | 1 | P4 |
| notes, page floats | 0 | 22 | 0 | P5 |
| native, FO, sequences, regions | 30 | 184 | 9 | P6 |
| model, preview, snapshots | 13 | 6 | 0 | keep (fixture granularity) |
| page model, margins, sides | 9 | 16 | 0 | keep / P4 |

Totals after the P1 fixes of the same day: 145 pass, 342 fail, 16 crash of 503 (the three gates and the orphans/widows, keep-chain, avoid-inside, forced-boundary, overfull, sliced/cloned border, multicolor border, content-visibility and UA-defaults CSS-flow cases pass). The 16 crashes dereference fragment fields the flow no longer publishes (`computed_style`, nested item nodes); they are rewritten with their bucket, and the suite is run one test per process until then (correction recorded 2026-10-15: `make test-radiant-baseline` does run this binary, whole, so the crashes end its run there; 503 also counted 12 cases of other suites, the `SecondaryViewTest` total is 491). Of the 13 CSS-flow failures left: two expect inline-level fragments for atomic wraps (P2 inline decorations), two belong to repeated table groups (P3), four to FO or native style refinement (P6), one to generated-content pass budgets (P4), one to content functions (P4), one to replaced percentage heights (P2), `UnimplementedContextsCannotPublishFlattenedPages` is retired by §4.1, and `SourceFontShorthandAndBlockBordersBelongToTheSelectedView` pinned the secondary cascade's independence from the browsing view, which §4.6 retires. `test_pdf_render_visual_gtest` paged subset: 16 pass, 12 fail, all twelve table continuations (P3); the former grid-rejection case now expects a sliced export.

**Behaviour changes to note.** The page area is the flow's initial containing block *and* its viewport: media queries and viewport units in the browsing cascade see the page body width and height, where the item engine evaluated them against the edition environment's page size (tests that keyed `box-decoration-break: clone` on `(max-width: 500px)` now key it on the page width they mean). `box-decoration-break: clone` reserves the cloned bottom edges before a break and the cloned top edges after it, as the item engine did. The DOM-embedded view holds the paged flow after `layout_secondary_view` (ruling 7); fixtures that asserted the root box's prior geometry survived unchanged are retired.

**Known gaps in this state.** Tables break only as monolithic boxes (P3); string-set, running elements and `target-counter` through the flow are not registered (P4); footnotes and page floats are laid out as ordinary content (P5); native and FO streams are hidden from the flow (`paged_flow_skips_element`) but their static regions still expect prepared flows (P6); a page body width change inside a run is diagnosed as unsupported (§4.2); inline box decorations (span backgrounds and borders) are not published; published glyph runs own pinned copies of their fonts (released with the composition) and boundaries copy borders and background colours, but background images, gradients and layers stay with the browsing painter; fixed-position repetition per page is not implemented.

### 2026-10-15: P3 tables

**Row pass.** `layout_table` still places a table whole: its cells stay outside the line and block hooks (`paged_flow_closed`), and `paged_block_after` hands the finished table to `paged_table_fragment` (section "tables: row breaks, cell continuations and repeated groups" of `layout_paged.cpp`). The pass walks the captions, header group, body rows and footer group in visual order and:

- moves a row that crosses its fragmentainer's body end to the next fragmentainer when a fresh one holds it; rows joined by a row span move together, a row group under `break-inside: avoid` moves whole, and `break-before/after: avoid` between rows moves the earlier rows along (each relaxed when the move would empty a fragmentainer or no fragmentainer holds it);
- honours forced `break-before/after` between body rows (row or row-group values combine at the boundary); a forced break never separates the header or footer group from the body, so a page is never furniture alone;
- keeps the leading part with the table: when the captions, header and first body row cannot stay on the current fragmentainer, the whole table moves (keep chains then apply to the boundary before it, as for any moved box);
- continues a row that cannot move inside its cells: `paged_fit_finished` fits the cells' finished content against the fragmentainers in flow order, rebuilding each inline run's lines from its text rects and atomic inlines by line number (`paged_geometry_collect`), so `paged_fit_line` and `paged_paragraph_close` apply struts, orphans and widows to cell lines exactly as to flow lines; block-level boxes inside cells move whole when monolithic or are entered. The row then grows by its largest cell continuation;
- captions that cannot move continue through the same finished-content fitter.

Space is opened inside the table by position, not source order (`paged_table_open`): boxes starting at or after the break move, boxes spanning it grow, so a row-spanning cell crossing a break grows with it. Inside a table the sibling moves of `paged_flow_grow` are suppressed (cells sit side by side, groups are placed visually).

**Repeated groups.** A header and a footer group no taller than a quarter of the fragmentainer repeat (CSS 2.2 §17.2 leaves repetition to the UA; this is the limit browsers apply); body rows keep the header's extent free at the top of each continuation and the footer's extent at the bottom of every fragmentainer (`paged_reserve`, which also carries the cloned box edges). Publication puts a header copy at the top of each continuation and a footer copy one border spacing below the last body content of each fragment before the last, both `VIEW_FRAGMENT_REPEATED_TABLE` and generated; the header's own row stays the first occurrence and the footer's own row the last, so occurrences follow the pages. Body boxes keep to the table's body window on each fragmentainer (below the header copy, above the footer copy) and the table box ends after the footer copy.

**Undo and re-runs.** A table's breaks are kept on its record (`PagedFlowTable`) and stand in the strut journal as one entry whose amount is the table's growth. Before its first break the pass snapshots the geometry `layout_table` produced; when the content around a broken table moves (a monolithic ancestor moving whole, a keep chain, a post-hoc margin collapse of an ancestor), the table is restored from the snapshot and broken again at its new place (`paged_refit` runs table units in flow order with the paragraphs).

**Publication of table paint.** Collapsed cells publish the borders the grid resolved, straddling their grid lines through `border_outset`; cells carry `table_range`. Row, row-group, column and column-group backgrounds paint only under the cells originating in them (CSS 2.2 §17.5.1: the table background shows in the spacing and where cells are missing), as anonymous generated layers before each cell fragment; their own boxes publish without a background.

**Browsing-engine defect fixed.** `layout_table`'s flow placement numbered rows in visual order while the table metadata and the cells' `row_index` number them in source order, so a `<tfoot>` authored before `<tbody>` (the HTML 4 order) shifted every body row's height by one row (`table_row_source_index`). Layout baseline unchanged (3141 / 150 / 3).

**Gates.** `SecondaryViewTest.PagedFlowBreaksTablesBetweenRowsAndRepeatsTheirGroups`, `PagedFlowContinuesTallTableRowsInsideTheirCells`, `PagedFlowTableRowsHonorForcedBreaksAndGroupAvoidance`, `PagedFlowRestoresATableBeforeBreakingItAgain` pass with the three earlier gates. `test_pdf_render_visual_gtest` paged subset: 24 of 27 pass (12 failing before). Three fixtures that alternated left/right page widths moved to one width (§4.2). The Markdown cheatsheet exports 21 pages in about 0.5 s user time, its tables breaking between rows with the header repeated.

**Legacy `SecondaryViewTest`** (one per process): 147 pass, 332 fail, 16 crash of 495 with the four new gates (142 / 333 / 16 of 491 before them); ten legacy table cases moved from fail to pass and none regressed. The whole `test_pdf_render_visual_gtest` passes 260 of 277: besides the three paged cases below, the 14 failures (body column rules, native columns, FO page controls, page labels, size containers, gradients, group opacity, skip-ink, emphasis, spacing, RTL overflow, the Poppler comparison) fail identically on a build without the P3 changes. The remaining table-bucket failures are: the legacy model's separate grid wrapper box and per-item column fragments (structure the browsing table does not have), alternating page widths (§4.2, §7), FO omission and numbered-column traits (P6), track-budget and rejection diagnostics (retired by §4.1, slice instead of fail), collapsed outer edges kept on every fragment, and the split-cell alignment question below. `FixedTablesBreakBetweenRowsAndRepeatSourceGroupsWithoutChangingTheDom` was rewritten for the footer's occurrence order.

**Split-cell alignment (ruling 4).** The cells of a row that continues inside its cells align each slice on their own. Before the cells are fitted (`paged_table_split_cells`), middle and bottom cells move their content to the top of the cell; once the table's breaks are final, `paged_table_align_slices` places the content of each slice in that slice's alignment area. On the cell's first and last fragmentainers that area lies inside the cell's own edges (half a collapsed border). On the others it starts below the cloned edges and the repeated header and ends at the body end the publication windows use, less the cloned bottom edges. The slice moves by opening space above it and closing it below (`paged_table_open` in its content mode, which also moves text rects and inline views). Baseline cells keep the row's alignment on the row's first fragmentainer. On each later one their first lines align again through an extra top reserve per cell: the largest of the cells' reserve minus first-line top, less the cell's own. So the line fitting consumes the alignment space before it places the continuation's lines. The first line's baseline offset stands for the first line of each continuation, which is exact when a cell's lines share their metrics. The extent of a slice is measured the way `layout_table` measures cell alignment (line boxes, not glyph rectangles): `table_cell_vertical_bounds` takes an optional window, exported as `layout_table_cell_content_extent`, beside `layout_table_cell_alignment_insets`. `PagedSplitCellAlignmentMatchesExplicitRowsWithSliceAndCloneEdges` and `PagedSplitCellBaselinesMatchIndependentExplicitRowSlices` pass. The paged subset now passes 26 of 27 (only the caption wrapper case below fails), the whole binary 262 of 277, and the legacy `SplitTableBaselinesReserveSpaceBeforeConsumingIndependentCellLines` and `SplitTableCellsAlignEachSliceAndRetainIndependentSourceCursors` cases pass too (sweep 149 / 330 / 16 of 495, no case regressed). Layout checks are unchanged (3141 / 150 / 3).

**Open.**
- *Caption wrapper.* `PagedCaptionsMatchBlockWrappersAcrossTableAndCaptionContinuations` fails because the browsing table paints its border and background around the whole wrapper, captions included, where CSS gives them to the grid box; the paged edition reproduces the browsing geometry.
- `make test-radiant-baseline` also fails outside this work: `CssCascadeMemory` (jQuery UI page 0.3% over its CSS budget) and `doc_editor_tsv_table` fail identically without the P3 changes; the DocumentLanguage, photo-editor, iframe and email-preview cases do not involve tables or pages; the render suite aborted under load in the gate run and passes alone (207 / 4 XFAIL / 0).
- The legacy table producer, split and continuation code stays in the file behind `RADIANT_PAGED_ENGINE=legacy` with the rest of the item engine until P7.

### 2026-10-15: P4 page composition re-based on the flow

**Marks and named strings.** The layout records a mark source where it meets it, because only there do the counters stand as they do at the element (section "marks, named strings and targets through the flow" of `layout_paged.cpp`). `paged_element_enter`, called from `LayoutCounterScope::enter` once the element's own counter operations ran, evaluates its `string-set` (CSS GCPM 3 §1.1). The value is selected by name from the paged cascade's sources (`paged_flow_declared`, which also serves `page`; a style attribute is parsed once per composition, as the item engine read it). `content(before)` and `content(after)` evaluate the pseudo-element's content inside a counter checkpoint, so its own counter operations roll back. A `display:none` subtree records its assignments on its root without applying its counter operations (`paged_element_hidden`, from `layout_flow_node` and for root children). Running elements (`position: running(name)`, read from the browsing style) leave the flow in `paged_flow_skips_element`; root children now pass through that hook too. Publication walks the views in flow order and binds each recorded source to the page of the first body content after it (a text rect, image, list marker, atomic inline or leaf box). A source with no content after it binds to the last page, and a mark is at its page's start when no body content precedes it there. That is how the item engine bound a mark to the page its first item reached.

**Running elements through the browsing engine.** The composition tail now runs inside `layout_html_doc`, after the pass loop and while the layout context is live (`paged_flow_compose`): publication, regions, sequence ends, page counters, margin boxes and static regions. A margin box that takes `element()` lays the running element out with `layout_block` in a block formatting context of the box's width (`paged_running_layout`). Its min- and max-content widths (`layout_measure_intrinsic_widths`) feed the §5.3.2 band distribution, and in a side band its height at the band width does. The element is centred vertically and published into the box by the ordinary publisher over a one-fragmentainer table, with role `VIEW_FRAGMENT_RUNNING`. The element left the flow, so its views simply keep the last margin box's layout (the DOM view is a workspace, §3.4); a measurement scope would restore the view types but not the text rects, and a second layout then appended to stale rects. Generated text in margin boxes keeps the page-context composer (§3.3 refinement).

**References and convergence.** The DOM content evaluator takes a function hook (`DomContentFunctionHook` in `dom_element.hpp`). Generated content of the page flow passes `paged_content_function`, which resolves `target-counter()`, `target-counters()` and `target-text()`; `paged_content_target_function` now takes the referencing element and is shared with the item engine. The hook also resolves `content()` inside `string-set`, and diagnoses `counter(page)`, `counter(pages)` and `string()` in body content ("unresolved body generated-content binding"). A target's counters are recorded where the layout meets it, its page is the page of its first content, and its `before`/`after` text is the text its pseudo-elements generated in the layout. The reference pass loop of `layout_secondary_view` is unchanged and now repeats whole-document layouts; the 9→10, 99→100 and logical-label relabelling fixtures converge in four passes. Generated text publishes as an occurrence of its originating element (a P2 omission: it published as an occurrence of its own text node).

**Page model.** The run restart that §4.2 promised is implemented. A body child (or a root child, where the root holds the flow) at a forced break whose new page has another body width starts a run of that width. It and its following siblings lay out at the new width through the parent's context, which `layout_block` writes back after the child (`paged_flow_run_restart`). The root and body publish each run's width on its pages, and a width change at a nested box stays diagnosed. A sided forced break on an empty page of the wrong side turns that page blank and starts the next one (CSS Break 3 §3.1), which gives documents their leading blank page.

**Browsing-engine defects fixed** (independent of pagination; layout checks unchanged at 3141 / 150 / 3):
- `layout_html_root` opened no counter scope, so the root element's own `counter-reset`, `counter-increment` and `counter-set` never applied.
- `text_has_line_filled` ran its measuring loop once for an empty text node. It measured the terminator as a glyph (the `.notdef` advance) and stepped past the string, so an empty text node after inline content wrapped the line early. The relabelling fixtures hit this through the empty text of their entry.

**Gates.** `SecondaryViewTest.PagedFlowRecordsMarksWhereTheLayoutMeetsTheirSources`, `PagedFlowResolvesTargetReferencesOverWholeDocumentPasses` and `PagedFlowRestartsTheRunWidthWhereAPageChangesItsBodyWidth` pass with the earlier gates. These legacy GCPM, margin-box, counter and page-model cases now pass: hidden and inline named strings, physical-page string snapshots across blank pages, initial string reset, running elements, running strings with named pages, target page counters and logical labels, target URL decoding, target errors, self-referencing pass budgets, the three relabelling fixtures, logical page counters with blank sheets, bounded page counters, margin-box page totals, publishing-function keyword case, block pseudos, source counters across fragments, style containment, a named first page, a left start. Rewritten for this state: text rects keep their spaces (§4.5), the DOM view is the workspace and holds the browsing layout's synthetic pseudo children (§4.7, `authored_child`), and generated text is read from the edition (`append_generated_text`, §3.4). Legacy `SecondaryViewTest` sweep (one per process): 177 pass, 306 fail, 15 crash of 498 with the three new gates (149 / 330 / 16 of 495 before); 25 legacy cases moved from fail to pass, one from crash to fail, none regressed. `test_pdf_render_visual_gtest`: 264 of 278 pass, the paged subset 27 of 28 (the caption wrapper case still fails), and the logical page labels case now passes; the other 13 failures are the pre-existing ones listed under P3. The Markdown cheatsheet still exports 21 pages in about 0.3 s.

**Open.**
- *Leaders.* `leader()` in body content needs an inline fill that takes its line's free space and tiles its pattern; until then the page flow ignores it, as the browsing engine does.
- *Native and FO page programs* (`r:sequence-master` master runs and terminal masters, `r:static-content` and `fo:static-content` regions) belong with the native streams in P6. CSS has no terminal masters, and the flow's sequences come only from native and FO controls.
- `counter(page)` inside `string-set` (the item engine evaluated it on the page it reached) is diagnosed: the flow evaluates named strings before pages are numbered.
- `CounterResetsReachFollowingSiblingsWithoutEscapingTheirParent`: ancestor-first counter inheritance differs from the current CSS Lists editor's draft. The 2026-10-15 compatibility audit below records the conflicting browser baselines; this remains open.

### 2026-10-15: P5 footnotes and page floats

**Out of the flow.** `paged_flow_skips_element` takes a `float: footnote` element, and a `float: top` or `bottom` element whose `float-reference` is `page` or `column` (an inline reference keeps it in the flow), out of the flow in a paged edition (section "footnotes and page floats through the flow" of `layout_paged.cpp`). The element is laid out once by the browsing engine as a block formatting context root at the width it takes: the footnote area's content width, or the fragmentainer's width with the page area's height as the basis of percentage heights (`paged_detached_layout`, shared with running elements). A note met in the middle of a line lays out with the line's context set aside (`LayoutContextScope`), and the flow's hooks stand aside meanwhile (`PagedFlowState::outside`). The anchor waits for the next line commit (a call or inline float) or the next box start (a block-level float) and is placed on that line's or box's fragmentainer.

**Footnote call, marker and counter.** The browsing cascade gained the `::footnote-call` and `::footnote-marker` slots (`PSEUDO_STYLE_FOOTNOTE_CALL`/`MARKER`; the cascade's pseudo slot mapping is one table, `dom_pseudo_style_kind`). The call is a synthetic `::footnote-call` element in the note's place among its parent's children, the note staying its parent, so it inherits from the note, counts on its counter and publishes as the note's generated occurrence; the marker is the note's first child (`layout_footnote_pseudo`). Their user-agent defaults are UA-origin declarations (`content: counter(footnote)`, the marker followed by a space; the call `font-size: 65%; vertical-align: super; line-height: 0`, the zero line height keeping the item engine's calls from growing their lines). `content: none` generates no box, and `normal`, `initial` or `unset` shows the counter (CSS GCPM 3 §2.3). The `footnote` counter is reset at the root and incremented by each note unless the note increments it itself (§2.6).

**Reservations.** A placed anchor adds its extent to its fragmentainer: top floats, bottom floats and the footnote area (the page's `@footnote` context with percentages against the fragmentainer, `paged_fragmentainer_area`; its margins, edges, `min-height`, `height` and `max-height` as the item engine resolved them). `paged_reserve` keeps that extent free at the fragmentainer's end, so lines, monolithic boxes and table rows fit above it, and publication ends body slices there (`PagedFragmentainer::end`) and moves the body below the top floats. Policies (§2.7, ruling 2, notes atomic): with `footnote-policy: line` a note that does not fit beside its call moves the call's line to the next fragmentainer, with `block` the run's lines on the fragmentainer move with it, and a note that fits no fragmentainer beside its call is diagnosed (`TYPESET_UNPLACEABLE`). With `auto` the note waits for a later fragmentainer and later automatic notes wait behind it, keeping their order; new fragmentainers take the waiting notes first, keeping the area's limit, and after the body the rest take pages of their own, where the limit no longer applies. Page floats (ruling 3, no deferred-float queue) move with their anchor: a float that does not fit beside its line or box moves that line or box to the next fragmentainer, a float with `clear` avoids a fragmentainer holding a float of the cleared side, and a float taller than its page is diagnosed. `float-defer` is not applied (§7). Refits re-place the anchors of the lines they fit again and of the boxes they move; widows leave a line with anchors in place; a monolithic box's move counts the anchors inside it; a box whose closing edges reach into the reserved space continues after it on the next fragmentainer. The flow's end is tracked from the lines and boxes (the root box keeps the page height).

**Publication.** Each fragmentainer's top floats stack from its top, its bottom floats above the footnote area, and the area (role `VIEW_FRAGMENT_NOTE`, generated, its boundary and page-context style from `@footnote`) holds the notes in order at its content box; each element publishes through the ordinary publisher over a one-fragmentainer table (`paged_detached_publish`) with role `VIEW_FRAGMENT_NOTE` or `VIEW_FRAGMENT_FLOAT`, and its box occurrence carries `paint_box`. Material laid out at another width than its slot's lays out again there.

**Browsing-engine defects fixed** (layout checks unchanged at 3141 / 150 / 3): `font-size: unset` resolved to the initial size because `layout_resolve_font_size_value` handled `inherit` only, and `all: unset` and `font: unset` reset the font to its initial value; font longhands are inherited, so `unset` inherits them (CSS Cascade 4 §7.3.3).

**Gates.** `SecondaryViewTest.PagedFlowReservesFootnotesWhereTheirCallsCommit` and `PagedFlowPlacesPageFloatsOnTheirAnchorsPage` pass with the earlier ten gates. Legacy sweep (one per process): 206 pass, 277 fail, 15 crash of 498 (177 / 306 / 15 before); 26 legacy note and float cases moved from fail to pass, none regressed. Rewritten for this state: DOM geometry is the workspace (§4.7), text rects follow the font's content area and keep their spaces (§4.5), body fragments carry no page-context style (§4.6), notes are atomic (the split-note cases now use one note that fits and one that waits, with the same area geometry), anchors publish nothing, and closing decorations slice instead of failing (§4.1). `AFloatOnAContinuationPageKeepsTheFootnoteAreaLimit` became `WaitingNotesKeepTheAreaLimitBesideBodyContentAndTheirOrder`; `ExplicitPageFloatDeferralRetainsThePhysicalAnchorPage` and `ForcedBodyBreakDoesNotFlushAPageFloatDeferral` retire with float deferral (§7). The Markdown cheatsheet still exports 21 pages in about 0.3 s.

**Open.**
- Notes and floats inside tables, flex and grid items anchor after the closed box (their lines take no hook).
- Three image cases (`ImagesMove…`, `ImagesUseTheSameProducer…`, `PublishingCorpus…`) fail on the test fixture's images, which the flow publishes without an image box in the test host (the P2 image gap); the CLI renders the same images in notes and body.
- Native page programs (`ColumnNotesAndTopFloats…`, `BodyAlignment…`, `BodyColumnComputedBindings…`) belong to P6; `PercentageIndents…` alternates page widths (§4.2) and uses native refinements.
- `counter-reset: footnote` on a heading does not reach the following paragraphs under the browsing engine's ancestor-first inheritance; the compatibility audit below records the spec/baseline mismatch. `counter-set` updates the existing footnote counter.
- A box with anchors that a later refit moves (a monolithic ancestor) keeps its closing-edge growth; a footnote area of another width than the one its note was measured at re-lays the note out at publication without re-checking the reservation.

### 2026-10-15: publication ownership, fixed content and counter compatibility

**Images (P2).** `load_document_image` had checked the selected document's resource policy but decoded through `UiContext::document`. A headless secondary host with no current document therefore returned no image; a host displaying another document used that document's resource base. The loader now scopes decoding and animation registration to the selected document and restores the host's document afterwards. The DOM image adapter uses the same loader without a second context swap. `paged_publish_image` resolves the selected source into the edition's existing `image_resources` cache: the published image survives retirement of the browsing cache and DOM view properties, as required by **D4.5.1v4 / D4.1.4v5**. The three P5 image failures now pass; the repeated-running-image fixture also retires the host cache before raster/PDF painting.

**Fixed content (P2).** Publication collects fixed boxes whose containing block is the initial page viewport and emits one occurrence per physical page after composition, including inserted blank pages and trailing sequence sheets. Their source parent's flow-page offset is removed; their local positioned geometry is preserved. They publish as static furniture, independently of body clipping and body mark assignment. Fixed boxes whose containing block is a transformed or containing ancestor keep their ordinary source occurrence. The fixed-content regression checks source occurrences, text, a containing ancestor, raster pixels on every page, and PDF export. Re-layout of percentage-sized fixed furniture for different page widths remains unqualified.

**Counter compatibility (P4/P5, open).** The [published CSS Lists 3 §4.4.1](https://www.w3.org/TR/css-lists-3/#inheriting-counters) starts with the parent's counters and takes new names from the preceding sibling; the [current editor's draft §4.4.1](https://drafts.csswg.org/css-lists-3/#inheriting-counters) instead takes its counter set from the preceding sibling. Retaining every authored reset scope made the legacy sibling-reset fixture pass, but regressed `counters-004`, `foo-counter-reversed-007b` and `li-value-reversed-007b` against their cached browser references. That implementation change was withdrawn to preserve this plan's §5 browsing-baseline requirement; the original runtime and baseline expectations remain. `PagedFlowFootnoteNumbersFollowHeadingCounterSets` checks both calls and markers using `counter-set`, whose behavior agrees under both rules. Updating the inheritance contract and recapturing independent references remains separate outstanding work.

**Margin lifetime and font identity (P4).** `LogicalPageLabelsMatchAuthoredTextInPhysicalPdfAndFilteredPreview` crashed on both HEAD and the initial candidate in `setup_font`, while measuring a reused synthetic margin box. Detached margin views kept font-family pointers borrowed from a previous composition's declarations. `PagedMarginBorrows` now resets those view properties before returning the declaration borrow. It also removes their source pointers from the published-font lookup: retaining a source-pointer cache entry across that reset incorrectly reused page 1's 10px font for page 2's 20px font. Published font copies remain pinned and immutable (**D4.5.1v4**). The page-label raster/PDF comparison and `PagedFlowMarginFontsKeepEachPagesComputedSize` pass after these fixes.

**Validation.** Current native focus: **37/37 pass** (`TypesetTest.*`, `ResourceAdmissionTest.*`, `SecondaryViewTest.PagedFlow*` and the three image/notes/running-content fixtures). The current paged/native-FO PDF subset passes **27/29**; the caption-wrapper and FO/native pair gates remain failing. Dimension-cast lint and `git diff --check` pass. Paired layout runs with pinned HEAD implementations and the candidate have identical counts and failure inventories: **3,126 pass / 149 accepted partial / 19 fail** of 3,294. Those 19 failures predate this increment; no baseline scores were changed.

The aggregate Radiant run before the final margin-lifetime fix completed with **5,272 pass / 153 partial / 28 fail / 1 skip**. It includes the legacy caption test's null-style crash, CSS memory and page-snapshot failures, the layout residuals above and application/network/JIT failures. The UI automation gate passes **601/601**, and native scene3d passes **28/28** with normal macOS graphics access. An earlier sandbox run was stopped after graphics/XPC timeouts; its partial totals are not qualification evidence. The repaired margin path was replayed through the current focused and paged gates. The full PDF visual binary completed **279 pass / 15 fail** of 294 before the final margin fix; replaying those 15 failures on the installed final executable passes the repaired logical-label case and reproduces the other **14 failures**. The complete remaining failure inventory is in `temp/paged-integration-current/pdf-failures-replay.log`. Aggregate P7 qualification remains open.

**Remaining integration scope.** P2 inline decorations and richer boundary paint; P3 caption/grid separation; P4 leaders and page-dependent body/named-string bindings; the remaining P5 anchor/refit/width-reservation cases; the P6 provider adapter and `break_candidate` contract, remaining native/FO fidelity and pair gates; P7 secondary-cascade reduction, test migration, complete raster/aggregate evidence and release performance/size measurements. §7 extensions remain outside this increment.


### 2026-10-15: supporting retirement, retained paint and caption/grid separation

**Supporting retirement (P7).** The old element selector matcher, cloned secondary stylesheets/UA sheet, independent container measurement/replay maps, remembered-size commits, anonymous/generated item styles, item-list counter adapter and root marks, independent table/column walkers, image-measure records, frame-edge arguments and square-border/color-only paint fallback are removed. `PagedLayoutOptions::max_items` and `TYPESET_CONTRIBUTION_FLUSH_DEFERRED` no longer exist. The live `max_nodes` bound covers publication as well as sources; `max_container_passes` bounds the browsing engine's actual condition passes. `PagedPageCursor` retains the active page/sequence state required by composition. `TypesetFlowProvider` and producer-owned line alternatives remain intentionally for P6; their adapter is still outstanding.

**One cascade.** `view_tree_css.cpp` reads winning declarations from the document's shared `StyleTree`. `style_tree_get_property_declaration` includes aliases, shorthand components and `all`, with the shared rollback-aware cascade selection. Page/margin/footnote-area computation stays in `ViewCssStyle`; a source metadata bridge remains for native computed-property queries and page-context inheritance, and never lays out the body or rematches selectors. Winning declarations are snapshotted into the edition owner (**D4.5.1v4**). `page`, `string-set` and `footnote-policy` now have distinct registered property codes: the old matcher selected them by spelling, while an unregistered shared code could alias another publishing declaration. Their predefined names are appended without moving existing IDs (**D4.6.1v4**).

**P2 publication.** Edition-owned immutable `PaintList` records capture shared backgrounds (gradient/image/layers), border images, rounded/styled borders, outlines, supported outer shadows, inline span decorations, text decorations/shadows/emphasis and image markers. Temporary lowering lists borrow payloads without releasing the edition's paths, stops, text or image resources; scoped owner destruction releases them once (**D4.5.1v4 / D4.1.4v5**). PDF keeps an alternate record when the common background producer requires image-layer precomposition. No backend re-reads mutable DOM props. Slice image backgrounds retain their original positioning area and phase; clone decorations use each fragment's box. Background-only clipping keeps border and solid-fill coverage in the fragment rectangle, avoiding doubled antialias coverage on fractional table edges. Hidden own paint is suppressed while explicitly visible descendants remain paintable. Explicit-height boxes grow the fragmentainer table even when no text line commits. Viewport-fixed subtrees re-layout when the physical sheet's containing-block size changes, including percentages and source font selections.

**P3 captions.** Table fragments publish an unpainted wrapper, a separate painted grid and ordinary caption block occurrences. `layout_table_grid_rect` supplies the same grid rectangle to publication and continuous raster/SVG/PDF boundaries, including the simple fast path. Bottom captions begin after the closing grid border. Caption counters/pseudos are materialized in source order before visual reordering, and nested caption views reset before their second layout. Fixed table percentage sizes reuse the containing-block-resolved value; fixed border-box tracks subtract padding once; caption minimum width constrains fixed grids too. Chromium independently confirms the automatic/fixed caption minimum fixture (134px wrapper, 126px caption). Caption continuation controls use equal-width sheets as required by §4.2; changed-width continuation re-layout remains the §7 extension.

**Test migration inventory.** The following old contracts were removed, with their remaining public behavior checked through shared layout and retained publication. This is retirement evidence, not a claim that historical totals remain comparable.

| Retired contract/test | Current coverage |
|---|---|
| `ColumnSpanTrackBudgetsFailBeforePublishing` | Common `max_nodes` rollback tests and the browsing table track/span suites; no independent paged track budget |
| `RepeatedLeaderGlyphsRespectTheSelectedViewContributionBudget` | Item contribution budgeting removed; leaders remain an explicit P4/P6 gap |
| `PagedColumnBudgetsAndUnadmittedGridPoliciesRejectIncompleteEditions` | Common node/page budgets and live page-column validation; no item-grid admission policy |
| `UnequalTableRowsGainAnonymousCellsWithoutChangingDom` | Browsing table fixups and retained table raster/coverage controls; no separate anonymous secondary style |
| Item-budget branches in span/coverage and late rollback fixtures | Retain other assertions; late failures now use live node/page bounds |
| Cyclic caption percentage / nested-table rejection fixtures | Admit through browsing sizing/formatting contexts and check finite geometry/source text |
| Secondary container maps and per-body `computed_style` assertions | Check the unfragmented browsing extent, independent edition geometry/fonts, convergence budget and retained generations |

**Focused evidence.** The expanded native focus passes **50/50**, including rich paint after DOM relayout, image-cache retirement, source-edition release and repeated raster/PDF painting; two-page gradient phase; per-sheet fixed percentages; visibility; caption inheritance/counters, continuations and retained output; and shared container/native query replays. Paint list/parity checks pass **58/58**, including repeated lowering and exactly-once owner release. The isolated secondary sweep is **251 pass / 203 fail / 0 crashes** of 454 (before: **234 / 211 / 9**); none of the 234 previously passing tests regress. Invalid-native tests now use fatal precondition assertions before dereferencing missing styles/diagnostics, converting harness crashes into their existing failures. They are not counted as runtime fixes. The complete native binary finishes **408 pass / 202 fail** of 610, with no crash; a shared-process run and isolated cases can have different failure counts.

**Rendered evidence.** The final paged/native-FO PDF subset is **29/30 pass**; rich boundary/inline retention and caption/grid continuations pass, and the remaining failure is `FoAndNativePageControlsSharePreviewAndPhysicalPdfPages` (flow traits/sequence controls, P6). Broader boundary/background/border/table/shadow replay passes **48/49**, retaining the existing ordered-background-layer failure. Paired frozen/candidate layout runs have identical results and failure inventories: **3,126 pass / 149 accepted partial / 19 fail** of 3,294. No baseline scores were changed.

The full PDF visual binary completed **278 pass / 17 fail** of 295 before the final slice-coverage correction and physical-page SVG test comparison. Replaying all 17 failures with the corrected painter passes the five repaired rich-paint/table cases and reproduces the other **12 failures**. The final residual inventory equals the prior 14-case inventory without the now-passing caption-continuation and size-container export cases. This is a full run plus focused final replay, not a second full final run. Residuals cover three fixed-PDF import/labels cases; body-column rules/patterns and explicit native columns; the FO/native pair gate; ordered background layers, out-of-range gradient stops, styled skip-ink fringes and group opacity; and the PDF-package comparison against Poppler. Exact names and artifacts are in `temp/paged-retirement/final-validation/temp/pdf-failures-done.log`.

**Aggregate evidence.** The final `make test-radiant-baseline` completes with **5,562 pass / 153 partial / 225 fail / 1 skip** of 5,941. Its filtered native suite now reports **285/487 pass**, with 202 failures instead of aborting; the separate complete native run above includes 123 additional passing cases. UI automation passes **601/601**, native scene3d **28/28**, and the view UI suite **15/15**. Residuals are those native/FO and unmigrated-flow assertions, the same 19 layout residuals, CSS memory, page snapshots and two iframe viewer scenarios. Direct pinned-executable replay reproduces the LaTeX navigation failure on both frozen and current implementations; the second iframe evaluator scenario passes on both in isolation. The frozen executable also fails the CSS memory gate. Exact aggregate results are in `temp/paged-retirement/radiant-done.log`. No failed score was reclassified or baseline updated. Scratch artifacts, frozen/current executables and source/binary SHA-256 provenance are under `temp/paged-retirement/`. Current subsystem sizes are **5,139 lines** in `layout_paged.cpp`, **1,326** in `view_tree_css.cpp` and **253** in `typeset.cpp`; the §3.5 size target and release timing gate are not claimed complete.

**Remaining.** Full paint fidelity is not claimed: inset shadows, masks, transforms/effect ordering, mixed atomic inline decoration and other fragment combinations still need qualification. P2 link/destination, page-instance/snapshot, corpus-raster and P3 complete rewritten-table/FO pair gates stay open wherever their tests fail. P4 leaders, page-dependent body/named-string bindings and counter compatibility; P5 anchor/refit/width reservations; P6 native-provider/break-candidate integration and FO refinements; P7 remaining test migration, aggregate qualification and release timing/size gates remain outstanding. No release performance claim is made from debug runs.

### 2026-10-15 CSS qualification repairs

The [CSS failure register](Radiant_CSS_Qualification.md#active-repair-request--2026-10-15)
records this bounded repair request. All 53 FO/native export pairs now pass
exact preview pixels and their physical PDF page comparisons
(`temp/css_debt_repair/fo_computed_edges.json`). The caption, column-rule,
explicit-column, fixed-PDF import and definite-height tail export groups also
have focused passing receipts in that register.

Flow blocks replay discovered containing and line widths over the existing
bounded convergence passes; they are laid out again at those widths. Half-open
page lookup applies its closing-edge bias once. Sequence terminal extents are
updated once per sequence, retaining a real last-sheet body tail to settle
narrower terminal masters. Explicitly nested named-page width transitions are
still diagnosed as unsupported.

Tall note bodies use source block/text break edges and reserve independent
same-width continuation pieces before a following sequence. Changed-width note
continuations remain unsupported. Published pieces retain their own fragment
windows and first/last identity under **D4.5.1v4 / D4.1.4v5**; this does not
complete the separate legacy paged migration or qualify every multi-note case.

Native computed-property bindings now reach the live CSS workspace, including
its final background recomputation. Decoration conditionality drives individual
padding/border break reservations and published edges. FO root whitespace
defaults no longer precede duplicate authored attributes or override them with
an inline CSS reset. Invalid native traits are admitted through the common
source-preserving diagnostic path before layout. The focused flow, FO color,
whitespace and original caption controls pass 22/22
(`temp/css_debt_repair/flow_fo_final.json`); aggregate qualification remains open.

### Merge reconciliation — 2026-10-15

The CSS repairs and retained-paint retirement now share `layout_table_grid_rect`.
Independent native border/padding policies feed both fragment metadata and
edition-owned paint (**D4.5.1v4 / D4.1.4v5**); sliced image backgrounds keep their
complete positioning area. Caption counter initialization runs once, and the
definite-height tail repair retains the bounded fragmentainer lookup.

The merged CLI and three affected native targets build; layout integer-cast lint
and whitespace checks pass. Focused paint-list checks pass **58/58**, and the
selected PNG/SVG/PDF exporter groups pass **9/9**, including the original Hebrew
and gradient-edge comparisons. Native checks pass **28/30**: caption minimums,
source-order counters, retained paint, unequal columns and note continuations
pass. `TableCaptionsUseATransparentWrapperOutsideGridDecorations` and
`IndependentDecorationConditionalityReservesAndPaintsBodyAndCellContinuations`
still fail geometry assertions; their checks remain unchanged. This is focused
merge verification, not an all-green aggregate or attribution of both failures
to a particular parent. Receipts: `temp/merge_css_paged/{native,paint,export}.json`.

### 2026-10-16: reported view-reuse and WPT failures

**Bounded repair.** Reproduced the reported 186/490 native failures and the
six image/thirteen position layout regressions before editing. Native trait
admission previously ran only for fragmentable blocks, allowing invalid inline
image traits to publish successfully. Publication now checks visible authored
sources after the layout workspace has settled its mutation epoch. Anonymous
table wrappers are skipped when finding an authored style parent: their
unregistered identities cannot serve as source metadata contexts. This keeps
edition metadata on registered sources under **D4.5.1v4 / D4.1.4v5** and uses
the existing shared trait validators.

The unchanged native suite improves to **310/490** in the baseline selection
and **433/613** in the complete binary, with no previously passing case failing.
The six repaired cases cover invalid image scales, FO inheritance, percentage
indent diagnostics, intrinsic fixed-table sizing, split-table baselines and
independent split-cell cursors. The remaining **180** cases retain their
assertions; this repair does not complete P6/P7 or the changed-width and native
formatting extensions. Exact remaining failures and first assertions are in
`temp/failed_tests_remaining.json`.

**WPT inputs and DOM geometry.** Independent Chromium 152 captures show that
the six object-view-box references omitted the fixture support script, and
the thirteen sticky references omitted its generated indicator elements.
The companion corpus extractor now canonicalizes symlinked fixture/category
paths, resolves its checkout from the caller's root, and reuses the existing
reference-name helper. Nineteen independently captured Darwin references
retain the scripts' complete DOM; baseline scores and comparison thresholds
are unchanged. Source/reference hashes and browser identity are recorded in
`temp/wpt-reference-provenance.json`.

Eight sticky-table cases also exposed an engine defect: `offsetParent`
incorrectly selected a static table ancestor for a positioned descendant.
Table ancestry now applies only to static descendants, as specified by
[CSSOM View §7](https://drafts.csswg.org/cssom-view/#dom-htmlelement-offsetparent).
`PositionedTableDescendantsSkipStaticTableOffsetParents` checks sticky,
relative, absolute and static controls. The WPT suites pass **56/56 images**
and **139/139 position**; the text/layout native suite passes **128/128**.
Receipts are under `temp/failed_tests_*`, `temp/wpt_repaired.log` and
`temp/view_reuse_final_all.json`.

**Radiant gate.** `make test-radiant-baseline` completes with **5,683 pass,
155 accepted partial, 180 fail and one skip** of 6,019. Every failure belongs
to the remaining native paged selection above. UI automation passes 609/609,
DOM UI 157/157, page-load 111/111, page snapshots 51/51, and the full layout
baseline has zero regressions. The new offset-parent regression also passes
with forced AST and MIR execution. Aggregate report:
`temp/failed_tests_radiant_summary.json`.

**Lambda gate.** `make test-lambda-baseline` completes with **7,447/7,450**
combined runtime/input checks passing, including 313/313 forced-GC checks.
All three failures exercise `test/lambda/pdf/output_pages.ls`: its golden,
the tier-parity case and the binary-schema PDF case. The PDF callback visitor
has no `paint` consumer for retained page paint commands, so traversal fails.
An isolated CLI relinked with the HEAD versions of the three changed production
files reproduces the same failure on interpreter and JIT; the working CLI and
sources are not replaced. This existing callback-integration gap remains open.
Evidence: `temp/failed_tests_lambda_gate.log` and
`temp/head-pdf-comparison/result.json` (revision and binary checksum).
