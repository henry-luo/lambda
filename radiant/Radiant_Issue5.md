# Radiant Issue 5 — `cmdSetColumnWidth` empties the table under `lambda.exe view`

Status: **OPEN** (blocks the table column-resize gesture end-to-end)

This document captures a bug discovered while fixing the Stage-4C editor's table
column-resize gesture under `lambda.exe view`. The *reported* problem
(getBoundingClientRect returning zero geometry for table cells) is **FIXED** — see
[§1](#1-fixed-getboundingclientrect-returned-zero-geometry-in-view-mode). While
verifying that fix, a **separate, downstream** bug surfaced: once the drag is
actually delivered, `cmdSetColumnWidth`'s result empties the table's rows/cells
from the doc model under `view` mode. That second bug is the subject of this
issue.

---

## 1. FIXED: getBoundingClientRect returned zero geometry in `view` mode

**Root cause.** `getBoundingClientRect()`/`getClientRects()` route through
`js_dom_ensure_layout_for_geometry()` (lambda/js/js_dom.cpp), which needs a
`UiContext` (`_js_current_ui_context`) to flush layout. That thread-local was
only ever set by the headless `lambda.exe js` document session
(`lambda/main.cpp`); the `lambda.exe view` path **never set it**, so the function
bailed and geometry queries returned stale/zero rects. Block-level elements
merely *appeared* to work because their committed geometry was already on the
node from the window's own layout pass; a freshly re-rendered subtree (an editor
table rebuilt on each render) read zeros.

Two secondary constraints made a naive fix crash:
- Forcing a full `layout_html_doc()` from a geometry query **during document
  load** re-enters the loader (fonts not yet processed, view pool not set up) and
  crashes in `collect_inline_styles_from_dom()`.
- Rebuilding `doc->view_tree` from a geometry query **in view mode** frees view
  state out from under the live renderer / event dispatch (observed as a
  use-after-free in a timer callback touching a freed `DomNode`).

**Fix** (all gated on an interactive/host-driven session so static headless
renders are unaffected):
- `radiant/window.cpp` — set `_js_current_ui_context` before load, and set a
  `host_driven` flag only when a loop will actually pump the JS event loop after
  the first commit (`!headless || sim_ctx`).
- `lambda/js/js_dom.cpp` — `js_dom_ensure_layout_for_geometry()`: in host-driven
  mode return the committed view tree's geometry as-is (no destructive rebuild);
  in the transient `lambda.exe js` session keep the self-contained
  rebuild-on-demand, but only once the initial layout has committed.
- `lambda/js/js_event_loop.cpp`, `lambda/js/js_mir_entrypoints_require.cpp`,
  `radiant/script_runner.cpp` — browser-faithful timers for host-driven sessions:
  a page's scripts share one event loop, and load-time `setTimeout(0)` callbacks
  are deferred to the host's post-commit pump instead of firing mid-load. This is
  what lets the editor's post-mount overlay re-sync read *committed* geometry.

**Verified.** `getBoundingClientRect` now returns real boxes for table/tr/td
(e.g. table `w=834`, first-row cells at `abs=(33.5,159.6) / (303.0,159.6) /
(539.4,159.6)`), and the column-resize drag now **delivers**: the synthesized
mousedown hit-tests to `.rdt-col-resizer`, the mouseup dispatches and bubbles to
the `window`-level `mouseup` listener, and `cmdSetColumnWidth` runs. 21+ existing
editor Phase-B fixtures still pass (no regression).

---

## 2. OPEN: `cmdSetColumnWidth` empties the table's rows/cells (doc model) under `view`

### Symptom

Running the column-resize fixture (see [§4](#4-repro)):

- **Before** the drag: `.rdt-surface table` = 1, `.rdt-surface tr td` = 6 (2×3),
  first-row `td` = 3, `.rdt-col-resizer` = 3. All correct.
- **After** the drag: `.rdt-surface table` = 1, but `tr` = 0, `td` = 0,
  `.rdt-surface table *` = 0 (the `<table>` has **no descendants at all**), and
  global `td[width]` = 0. The final assertion (`td[width] ≥ 1`) fails.

The rows/cells are **removed**, not merely mis-styled or moved (`td` anywhere in
the document = 0).

### The gesture *is* delivered (this is not a getBoundingClientRect problem)

Instrumented `radiant/event.cpp`:
- `MOUSE_DOWN at (450,586) → target tag=div class=rdt-col-resizer` — the drag
  start hit-tests to the resizer, so `onmousedown`→`startColResize` runs.
- `MOUSE_UP at (510,586) → target #text` — the release dispatches through the JS
  EventTarget pipeline (`radiant_dispatch_mouse_event(..., "mouseup", ...)`), which
  bubbles to the `window`-level `mouseup` listener registered by
  `startColResize`, so `onUp`→`runCmd(cmdSetColumnWidth)` runs.

### It is the doc model that ends up empty, not a transient render glitch

Performing the drag and then a **second, unrelated re-render** (clicking a
paragraph) still shows `td` = 0. If only the DOM had been mis-reconciled, the
next `reconcileDoc(surface, renderDoc(doc))` would repaint the cells from an
intact doc model. It does not — so `state.doc`'s table content is genuinely empty
after the command is applied.

### What it is NOT (ruled out)

- **Not the command's transaction logic.** `test/commands/table-cells.test.ts`
  (which exercises `cmdSetColumnWidth`) is vitest-green, and Stage-4C Phase A runs
  that suite under `lambda.exe js` (green) — so the transaction is correct even
  under the MIR JIT *in isolation*.
- **Not the transaction/reducer primitives.** `applySetAttr` preserves
  `content` (`test/editor-js/src/model/step.ts`), and `replaceNodeAt` /
  `replaceAtStep` / `listSet` / `txStep` (`.../model/doc.ts`,
  `.../model/transaction.ts`) are plain immutable array/tree updates, shared with
  the **passing** image-resize gesture (which also sets `width`/`height` attrs via
  `stepSetAttr`+`applySetAttr`).
- **Not Radiant `setAttribute`.** A static page whose inline script calls
  `td.setAttribute('width','200')` keeps all 6 cells (before=6, after=6). So the
  DOM-API attribute write on a table cell is harmless.
- **Not general reconcile.** A normal click re-render (which reconciles the whole
  `<doc>`, table included) keeps the cells. Reconcile handles the table fine on an
  ordinary edit.

### Where it must live

The bug is specific to the **full view-mode command→`editorReducer`→render flow**
for `cmdSetColumnWidth` — i.e. something that runs in that path but *not* in the
isolated vitest command test:

- `cmdSetColumnWidth` returns `txSetSelection(tx, state.selection ??
  nodeSelection(anchorCellPath))` — a **node selection on the resized cell**. The
  isolated command test doesn't run `editorReducer` + selection projection +
  `renderDoc` + `reconcileDoc`; the view flow does.
- Candidates to bisect next:
  1. `selMap(step, sel)` (`.../model/transaction.ts` → mapping) for a `set_attr`
     step against a **node** selection — does it corrupt the selection or, via a
     shared mutation, the doc?
  2. `editorReducer` `apply` handling (`.../view/editor-state.ts`) under the MIR
     JIT for this specific transaction shape (multiple `set_attr` steps on
     sibling paths `[t,0,c]` and `[t,1,c]`).
  3. `renderDoc(doc_after)` for a table whose cells carry a `width` attr and whose
     selection is a node selection on a cell (does selection projection /
     `syncCellHighlight` mutate the rendered tree?).
- A JS→MIR codegen difference in the full-flow path (vs the isolated command) has
  not been excluded. `temp/mir_dump.txt` on a debug build is the place to inspect
  the transpiled transaction/reducer code if the JS-level bisect above comes up
  clean.

### Suggested bisection recipe

1. Reproduce with the fixture in [§4](#4-repro).
2. Add a temporary `log_error` (or a `data-*` attribute breadcrumb from JS) that
   dumps `state.doc`'s table row/cell counts (a) immediately after
   `cmdSetColumnWidth` returns its transaction, (b) after `editorReducer` applies
   it, and (c) after `renderDoc`. Whichever stage first shows 0 rows localizes the
   bug to command-result vs reducer vs render.
3. If all three show intact rows, the loss is in `reconcileDoc`/Radiant DOM for
   this specific attr change; if (a) or (b) shows 0, it's the reducer/selMap/JS-MIR
   path.

---

## 3. Secondary observation — dynamically-created overlays land in normal flow

The `.rdt-col-resizer` handles created by the editor's post-mount re-sync are
absolutely-positioned overlays (CSS `.rdt-col-resizer { position: absolute }`),
yet under `view` they resolve to `~(450,586)` (bottom of the shell, in normal
flow) rather than their intended `left/top` (`~536,159`, the column border). The
gesture still works because event_sim targets the element's *actual* laid-out
box, but it suggests the CSS cascade may not apply `position:absolute` to elements
created via JS `appendChild` **after** the initial load (the post-commit pump),
unlike load-time-created elements. This does not block the gesture and is noted
here only as a lead; it is **not** the cause of §2 (the cells vanish from the doc
model, independent of overlay positioning).

---

## 4. Repro

The repro harness landed with the §1 fix (kept, since it is benign and needed to
reproduce §2). The **fixture itself is intentionally kept OUT of
`test/ui/editor4c/`** so `make editor-4c-view` stays green; it is embedded below.

Harness pieces already in the tree:
- `test/editor-js/demo/main-dom.ts` — adds a `TABLE_DOC` seed selected via
  `window.__RDT_SEED === 'table'`.
- `test/editor-js/tools/build-dom-page.mjs` — also emits
  `test/html/editor-table.html` with `<script>window.__RDT_SEED='table'</script>`
  before the bundle. Regenerate with `node tools/build-dom-page.mjs` from
  `test/editor-js/`.

Save the following as `test/ui/editor4c/table-col-resize.json` to run the repro
(and delete it again to keep the baseline green):

```json
{
  "name": "editor4c: dragging a column resizer sets an explicit width on the column's cells",
  "_note": "Phase B — table column-resize gesture. syncColResizers() places absolute .rdt-col-resizer handles at each first-row cell's right border using getBoundingClientRect() on the table + cells. mousedown on a handle starts startColResize(); the drag's mouseup runs cmdSetColumnWidth(), which writes a width attr on every cell in the dragged column. Exercises getBoundingClientRect for table-internal (table/td) elements in the JS->Radiant path. Seed page editor-table.html sets window.__RDT_SEED='table'. BLOCKED by Radiant_Issue5 §2: the drag now delivers but cmdSetColumnWidth empties the table's rows/cells from the doc model under view.",
  "html": "test/html/editor-table.html",
  "viewport": {"width": 900, "height": 700},
  "events": [
    {"type": "wait", "ms": 300},
    {"type": "assert_count", "target": {"selector": ".rdt-col-resizer"}, "min": 1},
    {"type": "mouse_drag", "target": {"selector": ".rdt-col-resizer", "index": 1}, "dx": 60, "dy": 0},
    {"type": "wait", "ms": 150},
    {"type": "assert_count", "target": {"selector": ".rdt-surface td[width]"}, "min": 1}
  ]
}
```

Run:

```bash
./lambda.exe view test/html/editor-table.html \
  --event-file test/ui/editor4c/table-col-resize.json --headless
```

Current outcome: `.rdt-col-resizer` assertion passes; the final `td[width] ≥ 1`
assertion fails because the drag delivers `cmdSetColumnWidth`, which empties the
table (§2).
