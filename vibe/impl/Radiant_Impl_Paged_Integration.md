# Paged layout integration: one layout engine, breaks as the only paged step

**Status:** implementation plan, written 2026-10-14 at the user's direction; implementation started the same day (§9 records progress: P1 hooks and P2 publication for the CSS flow are in, the page flow is the default engine, the item engine stays reachable with `RADIANT_PAGED_ENGINE=legacy` until P7). It supersedes the engine-level direction of the P4 "resumable main flow" milestone recorded in [Radiant_Impl_Paged_Media](Radiant_Impl_Paged_Media.md); the page-box level, the page model and the output contracts recorded there stay.
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
- **Margin boxes and static regions:** laid out per page as ordinary blocks of generated content inside a `LayoutMeasureScope` at the margin-box geometry (CSS Paged Media 3 §5.3.2 sizing stays in `paged_margin_group`/`paged_margin_pair`). Page counters and labels are evaluated from the page table.
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

Totals after the P1 fixes of the same day: 145 pass, 342 fail, 16 crash of 503 (the three gates and the orphans/widows, keep-chain, avoid-inside, forced-boundary, overfull, sliced/cloned border, multicolor border, content-visibility and UA-defaults CSS-flow cases pass). The 16 crashes dereference fragment fields the flow no longer publishes (`computed_style`, nested item nodes); they are rewritten with their bucket, and the suite is run one test per process until then (`make test-radiant-baseline` does not include this binary). Of the 13 CSS-flow failures left: two expect inline-level fragments for atomic wraps (P2 inline decorations), two belong to repeated table groups (P3), four to FO or native style refinement (P6), one to generated-content pass budgets (P4), one to content functions (P4), one to replaced percentage heights (P2), `UnimplementedContextsCannotPublishFlattenedPages` is retired by §4.1, and `SourceFontShorthandAndBlockBordersBelongToTheSelectedView` pinned the secondary cascade's independence from the browsing view, which §4.6 retires. `test_pdf_render_visual_gtest` paged subset: 16 pass, 12 fail, all twelve table continuations (P3); the former grid-rejection case now expects a sliced export.

**Behaviour changes to note.** The page area is the flow's initial containing block *and* its viewport: media queries and viewport units in the browsing cascade see the page body width and height, where the item engine evaluated them against the edition environment's page size (tests that keyed `box-decoration-break: clone` on `(max-width: 500px)` now key it on the page width they mean). `box-decoration-break: clone` reserves the cloned bottom edges before a break and the cloned top edges after it, as the item engine did. The DOM-embedded view holds the paged flow after `layout_secondary_view` (ruling 7); fixtures that asserted the root box's prior geometry survived unchanged are retired.

**Known gaps in this state.** Tables break only as monolithic boxes (P3); string-set, running elements and `target-counter` through the flow are not registered (P4); footnotes and page floats are laid out as ordinary content (P5); native and FO streams are hidden from the flow (`paged_flow_skips_element`) but their static regions still expect prepared flows (P6); a page body width change inside a run is diagnosed as unsupported (§4.2); inline box decorations (span backgrounds and borders) are not published; published glyph runs own pinned copies of their fonts (released with the composition) and boundaries copy borders and background colours, but background images, gradients and layers stay with the browsing painter; fixed-position repetition per page is not implemented.
