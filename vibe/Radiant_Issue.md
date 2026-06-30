# Radiant Issue — JS-created chrome layout view orphaned after an editing interaction

**Filed:** 2026-06-30 · **Area:** Radiant layout / view-tree ↔ DOM-tree linkage · **Severity:** medium (blocks coordinate hit-testing of JS-built UI after edits; has a workaround)
**Found while:** running the Stage-4B plain-DOM JS editor under Radiant (see `vibe/editing/Radiant_Editor_Stage4B.md` §3.2).

---

## TL;DR

When a page builds UI in JavaScript at load (e.g. `document.createElement` + `appendChild`) and that UI sits **next to an editable surface** (contenteditable), then **after the first surface interaction** (a click that places the caret, or Cmd+A), the JS-built UI's **layout view is left orphaned** — its position is the "pending-layout" sentinel (`x`/`y` ≈ `INT_MAX`) and is **not recomputed even by a forced full `reflow_html_doc`**. Coordinate-based hit-testing (the event simulator's `click`, and presumably real pointer hit-tests) therefore **misses** that UI: a click on a toolbar button resolves to `y = 2147483647` and never reaches the element.

At a *fresh* mount the same UI lays out correctly and is clickable. Only a prior editing-surface interaction triggers the regression.

---

## Symptom (observed)

Driving the Stage-4B editor headlessly (`./lambda.exe view … --event-file … --headless`):

- **Fresh mount, click a toolbar button** → resolves to a valid point, e.g. `event_sim: resolved selector '…Bold…' to (144, 23)`, the button's `onclick` fires. ✅
- **Click into the surface, then Cmd+A, then click the same toolbar button** → `event_sim: resolved selector '…Bold…' to (144, 2147483647)`, the click lands at `y=INT_MAX`, the `onclick` never fires, and the intended action (apply bold to the selection) does nothing. ❌

The `INT_MAX` y comes from `get_element_center_abs` (`radiant/event_sim.cpp:479`) summing `view->y` up the parent chain — one ancestor view of the toolbar button is at the pending-layout sentinel, i.e. it was never (re)laid-out.

---

## Root cause (as far as traced)

1. The page's chrome (a `.rdt-shell` containing a toolbar + the contenteditable surface) is **created entirely in JS** and appended under a parsed `<div id="root">`. At mount it lays out fine.
2. A surface interaction goes through the **editing/caret path**, which runs its own reflow (`state_store` editing → `reflow_process_pending`) and **clears `state->needs_reflow`**. After this, the JS-built chrome's layout view is at the pending sentinel (`INT_MAX`) — it was dropped from / not reattached to the laid-out view tree.
3. Because `needs_reflow` is already clear, the simulator's `sim_flush_pending_reflow` (`radiant/event_sim.cpp:385`) is a no-op before resolving the next click, so the stale geometry is used.
4. **Crucially, this is not just a reflow-scheduling gap:** forcing a full `reflow_html_doc` (`radiant/window.cpp:396`) unconditionally before resolving the click **still** yields `y=INT_MAX` (tested). So a full reflow does **not** recompute the JS-built chrome's views after the editing interaction — they appear to be **orphaned from the view tree** (or no longer linked to the DOM-element subtree the layout walks).

### Ruled out
- **`needs_reflow` scheduling for JS mutations** — already fixed: `js_dom_mutation_notify` now calls `doc_state_request_reflow()` for layout-affecting mutations (`lambda/js/js_dom.cpp` ~`js_dom_mutation_notify`, `radiant/state_store.cpp:3827`). This makes the chrome lay out at **mount**, but does not survive the editing interaction.
- **`view_state_prune_orphans`** (`radiant/state_store.cpp:3015`) — this prunes reactive *ViewState* (template state), **not** layout geometry; not the cause.
- **Forced full reflow** — does not recompute the orphaned views (see point 4).

### Where the fix likely belongs
The layout/view-tree build in `reflow_html_doc` (`radiant/window.cpp:396`) and the editing-path reflow: why a JS-created DOM subtree (appended via `appendChild` to a parsed node) loses its layout view — or fails to get it rebuilt — after an editing reflow, while a parse-time subtree does not. Compare how parse-time vs JS-`appendChild`-time `DomElement`s are linked to the view tree, and whether the editing reflow rebuilds views only for the editing host's subtree.

---

## Minimal repro

**Prereqs**
```bash
make build                                  # builds lambda.exe (debug)
npm --prefix test/editor-js run build:page-dom   # builds test/html/editor-dom.html (classic IIFE, no React)
```

**A. Fails — toolbar click after a surface interaction** (`temp/issue-fail.json`):
```json
{ "name":"issue-fail","html":"test/html/editor-dom.html","viewport":{"width":900,"height":700},"events":[
  {"type":"wait","ms":300},
  {"type":"click","target":{"selector":".rdt-surface h1"}},
  {"type":"wait","ms":80},
  {"type":"key_combo","key":"a","mods_str":"super"},
  {"type":"wait","ms":80},
  {"type":"click","target":{"selector":".rdt-toolbar button[title=\"Bold (Cmd+B)\"]"}},
  {"type":"wait","ms":150},
  {"type":"assert_attribute","target":{"selector":".rdt-surface [data-source-path=\"0,0\"]"},"attribute":"style","contains":"font-weight:bold"}
]}
```
```bash
./lambda.exe view test/html/editor-dom.html --event-file temp/issue-fail.json --headless 2>&1 | grep -E "resolved selector|Assertions:|FAIL"
# → resolves the Bold button to (144, 2147483647); assert FAILS (no bold applied)
```

**B. Works — same button at fresh mount** (`temp/issue-pass.json`):
```json
{ "name":"issue-pass","html":"test/html/editor-dom.html","viewport":{"width":900,"height":700},"events":[
  {"type":"wait","ms":300},
  {"type":"click","target":{"selector":".rdt-toolbar button[title=\"Bold (Cmd+B)\"]"}},
  {"type":"wait","ms":100}
]}
```
```bash
./lambda.exe view test/html/editor-dom.html --event-file temp/issue-pass.json --headless 2>&1 | grep "resolved selector"
# → resolves the Bold button to (144, 23) — valid; onclick fires
```

The only difference between A and B is the prior `click .rdt-surface h1` + `Cmd+A`.

---

## Acceptance criteria

1. In repro **A**, `resolve_target` for `.rdt-toolbar button[title="Bold (Cmd+B)"]` returns **valid coordinates** (not `INT_MAX`) and the button's `onclick` fires.
2. The Stage-4B fixture `test/ui/editor4b/toolbar-bold-range.json` passes with the **`type "Z"` step removed** (currently kept only as the workaround for this issue).
3. `make test-radiant-baseline` remains green at its current level (no new regressions).

---

## Pointers

| What | Where |
|---|---|
| Coord resolution that surfaces `INT_MAX` | `radiant/event_sim.cpp:479` (`get_element_center_abs`), `:937` (`resolve_target`) |
| Sim reflow-before-resolve (no-op when `needs_reflow` clear) | `radiant/event_sim.cpp:385` (`sim_flush_pending_reflow`) |
| Full reflow entry (does **not** fix the orphan) | `radiant/window.cpp:396` (`reflow_html_doc`) |
| JS-mutation → reflow request (fixes mount only) | `lambda/js/js_dom.cpp` (`js_dom_mutation_notify`), `radiant/state_store.cpp:3827` (`doc_state_request_reflow`) |
| Reactive-state prune (NOT the cause) | `radiant/state_store.cpp:3015` (`view_state_prune_orphans`) |
| Editor page under test (JS-built chrome) | `test/html/editor-dom.html` (from `test/editor-js/demo/full-editor-dom.ts`) |

---

## Notes / scope

- A real interactive window does full per-frame layout, so it **may** mask the symptom for live use; whether the orphaning itself is observable interactively (e.g. a toolbar that stops responding to clicks after the first edit) should be confirmed on `./lambda.exe view test/html/editor-dom.html` (non-headless).
- Workaround in place: the editor's toolbar commands sync the selection from `getSelection()` just-in-time, and the range-format fixture types one character first so the editor's reconcile (→ reflow-on-mutation) re-establishes the chrome's layout before the toolbar click. This is harmless but should be removed once this issue is fixed.
