# Radiant Issue 5 — table column resize lost table rows under `lambda.exe view`

Status: **CLOSED** (fixed; regression fixture landed)

This document captures a bug discovered while fixing the Stage-4C editor's table
column-resize gesture under `lambda.exe view`. The *reported* problem
(getBoundingClientRect returning zero geometry for table cells) is **FIXED** — see
[§1](#1-fixed-getboundingclientrect-returned-zero-geometry-in-view-mode). While
verifying that fix, a **separate, downstream** bug surfaced: once the drag was
delivered, the reconciled DOM appeared to lose the table rows/cells under
`view` mode. That second bug is fixed in [§2](#2-fixed-anonymous-table-wrappers-were-hidden-from-js-child-traversal).

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

## 2. FIXED: anonymous table wrappers were hidden from JS child traversal

### Symptom

Running the column-resize fixture (see [§4](#4-regression-fixture)):

- **Before** the drag: `.rdt-surface table` = 1, `.rdt-surface tr td` = 6 (2×3),
  first-row `td` = 3, `.rdt-col-resizer` = 3. All correct.
- **After** the drag: `.rdt-surface table` = 1, but `tr` = 0, `td` = 0,
  `.rdt-surface table *` = 0 (the `<table>` has **no descendants at all**), and
  global `td[width]` = 0. The final assertion (`td[width] ≥ 1`) fails.

The original diagnosis said the rows/cells were removed from the editor doc
model. That was incorrect. Temporary breadcrumbs showed the doc model stayed
intact through `cmdSetColumnWidth`, `editorReducer`, and `renderDoc`
(`table/tr/td = 1/2/6`), and the command correctly set a `width` attr on the
resized column's cells.

### The gesture *is* delivered (this is not a getBoundingClientRect problem)

Instrumented `radiant/event.cpp`:
- `MOUSE_DOWN at (450,586) → target tag=div class=rdt-col-resizer` — the drag
  start hit-tests to the resizer, so `onmousedown`→`startColResize` runs.
- `MOUSE_UP at (510,586) → target #text` — the release dispatches through the JS
  EventTarget pipeline (`radiant_dispatch_mouse_event(..., "mouseup", ...)`), which
  bubbles to the `window`-level `mouseup` listener registered by
  `startColResize`, so `onUp`→`runCmd(cmdSetColumnWidth)` runs.

### Root cause

Radiant table layout generates anonymous table wrappers (`::anon-*`) for
layout. Those wrappers are stored in the real `DomNode` child chain; for example,
direct table rows can become children of an anonymous table section wrapper.

The JS DOM bridge already hid generated pseudo nodes from script-visible child
APIs, but it treated every generated node the same way: skip the generated node
and continue to its next sibling. For table layout wrappers this is wrong. They
are transparent wrappers, so script-visible traversal must flatten through them.

That mismatch made DOM APIs internally inconsistent:

- `querySelectorAll('td')` could still find the original descendants.
- `table.childNodes` / `firstChild` made the same table appear childless.

The keyed editor reconciler uses `parent.childNodes` to build its reuse map. When
it patched the `<table>`, it saw `existing.length = 0` even though the real rows
still existed behind `::anon-*` wrappers, so it created new rows/cells instead of
reusing the existing ones. The final view-tree/selector state then failed to
observe `td[width]`.

### Fix

`lambda/js/js_dom.cpp` now treats anonymous table wrappers as transparent in
script-visible child/sibling traversal:

- `js_dom_first_script_visible_child()` and `js_dom_last_script_visible_child()`
  descend through `::anon-*` wrappers.
- `js_dom_next_script_visible_sibling()` and
  `js_dom_prev_script_visible_sibling()` continue out of anonymous wrappers when
  needed.
- `insertBefore(node, node)` is guarded as a DOM no-op; detaching first can drop
  keyed reconciler children during ordering passes.

With the fix, the reconciler reuses the existing table rows/cells:

- table patch: `existing=2`, `vChildren=2`, `matched=2`, `created=0`.
- row patch: `existing=3`, `vChildren=3`, `matched=3`, `created=0`.
- after reconcile, `td[width] = 2` for the resized column.

### Verified

The repro fixture now passes:

```bash
./lambda.exe view test/html/editor-table.html \
  --event-file test/ui/editor4c/table-col-resize.json --headless
```

Result: `2 passed, 0 failed`.

`make build` also passes.

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
here only as a lead; it is **not** the cause of §2 (the failed assertion came from
script-visible table traversal, independent of overlay positioning).

---

## 4. Regression fixture

The repro harness landed with the §1 fix and the fixture is now checked in as a
regression:

- `test/ui/editor4c/table-col-resize.json`

Harness pieces already in the tree:
- `test/editor-js/demo/main-dom.ts` — adds a `TABLE_DOC` seed selected via
  `window.__RDT_SEED === 'table'`.
- `test/editor-js/tools/build-dom-page.mjs` — also emits
  `test/html/editor-table.html` with `<script>window.__RDT_SEED='table'</script>`
  before the bundle. Regenerate with `node tools/build-dom-page.mjs` from
  `test/editor-js/`.

Fixture:

```json
{
  "name": "editor4c: dragging a column resizer sets an explicit width on the column's cells",
  "_note": "Phase B table column-resize gesture. syncColResizers() places absolute .rdt-col-resizer handles at each first-row cell's right border using getBoundingClientRect() on the table + cells. mousedown on a handle starts startColResize(); the drag's mouseup runs cmdSetColumnWidth(), which writes a width attr on every cell in the dragged column.",
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

Current outcome: `.rdt-col-resizer` assertion passes and the final
`td[width] ≥ 1` assertion passes.
