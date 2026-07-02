# Radiant Issue — DOM mutation forces a full view-tree teardown and drops in-flight interaction state (DragDropState) mid-gesture

**Filed:** 2026-07-02 · **Area:** Radiant event/reconcile path (`post_html_handler_rebuild` in `radiant/event.cpp`) · **Severity:** medium (breaks any gesture whose handler mutates the DOM; forces per-feature workarounds) · **Status:** open — worked around for HTML5 drag-reorder in Stage 4C; follow-up design recorded in [`Radiant_DOM_Mutation.md`](Radiant_DOM_Mutation.md)
**Found while:** implementing HTML5 drag-reorder for the Stage-4C plain-DOM JS editor — see `vibe/editing/Radiant_Editor_Stage4C.md` ("drag-reorder" section).

**Follow-up design record:** [`Radiant_DOM_Mutation.md`](Radiant_DOM_Mutation.md) is the source of truth for the structural fix: current DOM mutation flow, fallback reduction, StateStore/view-state retention across fallback, `DragDropState` re-anchoring, and the event_sim/unit-test coverage plan.

---

## TL;DR

When a script event handler mutates the DOM, Radiant reconciles by **destroying the entire view (layout) tree and rebuilding it from scratch** (`post_html_handler_rebuild`, `radiant/event.cpp:4393`). Because the freed `View*` objects are referenced by transient interaction state, that same path **defensively clears `DragDropState`** (`doc_state_clear_drag_drop`, `event.cpp:4403`), the caret's view pointer, the per-view `state_map`, and animation targets.

The consequence: **any drag whose handler mutates the DOM loses its own drag state mid-gesture.** The editor's `dragover` handler inserts a drop-line indicator element → full relayout → `DragDropState.active` flips back to `0` → the native machinery treats the drag as over. Concretely traced: `active=1` after the first `dragover`, `active=0` on the very next `mousemove`.

Two things are wrong here and both deserve correction:

1. **A localized DOM mutation forces a *full* view-tree teardown** rather than an incremental relayout of the affected subtree. This is expensive and is the direct cause of #2.
2. **In-flight interaction state is thrown away instead of being re-anchored.** `DragDropState` (and the caret) hold `View*` pointers; the rebuild frees those views, so the code clears the state wholesale. It should instead **survive the rebuild and re-resolve its view pointers** from the DOM anchors it already knows.

---

## Symptom

Headless drag simulation against `test/html/editor-dom.html` (fixture `test/ui/editor4c/drag-reorder.json`), driven by `event_sim`'s `drag_and_drop` (`mousedown → 5× mousemove → mouseup`):

```
JSDND-DBG: mousedown draggable_elem=… tag=img
JSDND-DBG: mousemove (220,428) drag_drop=0x… pending=1 active=0   ← armed
JSDND-DBG: activating drag …                                       ← threshold crossed
JSDND: dispatched 'dragstart' … prevented=0
JSDND: dispatched 'dragover'  … prevented=1                        ← handler inserts drop-line
JSDND-DBG: mousemove (277,344) drag_drop=0x… pending=0 active=0    ← STATE WIPED
JSDND-DBG: mousemove (335,260) drag_drop=0x… pending=0 active=0
JSDND-DBG: mousemove (392,176) drag_drop=0x… pending=0 active=0
JSDND-DBG: mousemove (450, 91) drag_drop=0x… pending=0 active=0
```

`drag_drop` is the *same* pointer throughout (the struct isn't freed), but both `pending` and `active` are reset to `0` after the first `dragover`. With `active=0`, subsequent `mousemove`s skip the native drag block and `mouseup` skips the drop path, so no drop/dragend ever fires.

---

## Root cause

Two independent pieces of state track a drag:

1. **`state->drag_drop` (`DragDropState`)** — a C struct owned by the layout engine. Created on mousedown over a draggable element; carries `pending`/`active` flags and `View*` fields `source_view` and `drop_target`.
2. **The script's own handler state** (e.g. the editor's `this.dragSrcIdx`), updated by its `dragstart`/`dragover`/`drop` listeners.

The failure chain, per `mousemove`:

**Move 1** (threshold crossed):
- Native code sets `dd->active = true`, dispatches `dragstart`.
- A JS `dragover` is dispatched to the element under the cursor.
- The editor's `onDragOver` runs and calls `showDropLine(...)`, which **inserts a `<div class="rdt-drop-line">` into the DOM** to render the "drop here" indicator.

That DOM insertion invalidates layout. The reconcile path handles it with a **full teardown** (`radiant/event.cpp:4393`, inside `post_html_handler_rebuild`):

```cpp
// Force full relayout by clearing view tree
if (doc->view_tree) {
    view_pool_destroy(doc->view_tree);   // frees ALL layout View objects
    mem_free(doc->view_tree);
    doc->view_tree = nullptr;
}
// Clear stale view pointers in DocState — old views are now freed
if (state) {
    if (state->cursor) state->cursor->view = nullptr;
    doc_state_clear_drag_drop(state);    // <-- wipes drag state mid-gesture
    if (state->state_map) hashmap_clear(state->state_map, false);
    animation_scheduler_remove_views(state->animation_scheduler);
}
```

The clear is *locally* defensible: `DragDropState.source_view` / `.drop_target` point into the view tree that was just freed; leaving them would be dangling `View*`s → use-after-free. So the code drops the whole struct's live flags rather than carry dangling pointers.

**Move 2:** the mousemove handler's guard `if (state->drag_drop && (dd->pending || dd->active))` is now false → native drag block skipped → no further `dragover`.

**mouseup:** the drop/dragend logic sits under `if (dd->active)` → false → **no drop, no dragend, no reorder**.

This is a genuine architectural conflict, not a bug in either component alone:

- The **native drag machinery** assumes the DOM/view tree is *stable* for the duration of a drag (it was designed for `dropzone`-attributed drop targets with no handler-driven DOM mutation).
- The **HTML5-DnD model** mutates the DOM on essentially every `dragover` (that is how a drop-line / insertion caret is drawn). So the first dispatched `dragover` destroys the state the next dispatch depends on.

---

## Current workaround (Stage 4C) — and why it's only a workaround

For drag-reorder I **decoupled the JS-DnD lifecycle from `DragDropState`** (`radiant/event.cpp`):

- `g_jsdnd_active` — a file-static `bool` that nothing in the reconcile path touches, so it survives the teardown. All JS `dragover` dispatch is gated on this flag instead of `dd->active`.
- `g_jsdnd_source` — the drag source stored as a **DOM element pointer** (not a `View*`). The full teardown frees the *view* tree but **not** the *DOM* tree (DOM nodes live in the document arena), so this pointer stays valid to `mouseup`, when `dragend` is dispatched to it.
- The per-move drop target uses `evcon.target`, which is freshly hit-tested from the rebuilt view tree on every event, so it is always live.

This makes drag-reorder work, but it is a **parallel state machine that sidesteps the real problem.** It does not help:

- other consumers of `DragDropState` (native `dropzone` drops that also mutate the DOM),
- the caret `view` pointer, which is likewise nulled on every mutation and re-projected from scratch,
- performance: every keystroke/`dragover`/DOM edit still tears down and rebuilds the *entire* view tree.

---

## Proposed fix (follow-up)

The structural fix is now specified in [`Radiant_DOM_Mutation.md`](Radiant_DOM_Mutation.md). In short:

- reduce fallback by treating broad cases as retained recascade/reflow whenever possible;
- make fallback retain StateStore/view state for connected DOM nodes and prune only removed nodes;
- re-anchor `DragDropState` by durable DOM node ids/boundaries instead of raw `View*` payloads;
- add focused C++ StateStore tests and `test/ui` event_sim coverage for reconcile mode, state rebinding, and drag/drop survival.

That proposal supersedes the earlier two-part sketch in this issue note.

---

## Repro / verification

- Fixture: `test/ui/editor4c/drag-reorder.json` (passes today only because of the `g_jsdnd_*` workaround).
- To observe the underlying bug, gate JS `dragover`/`drop` dispatch on the native `dd->active` again (revert the decoupling) and re-run: the trace shows `active` flipping to `0` after the first `dragover`, and the drop never fires.
- Debug traces referenced above were `log_error("JSDND-DBG: …")` probes at the mousemove drag block and the drag-init walk in `radiant/event.cpp` (removed after diagnosis; the surviving `log_debug("JSDND: …")` in `radiant_dispatch_drag_event` still shows the dispatch sequence).

## Related

- `vibe/radiant/Radiant_DOM_Mutation.md` — follow-up design record for DOM mutation handling, fallback reduction, StateStore retention, `DragDropState` anchoring, and tests.
- `vibe/editing/Radiant_Editor_Stage4C.md` — "drag-reorder" section (the workaround that motivated this issue).
- `vibe/radiant/Radiant_Issue3.md` — a sibling case where a script that reconciles the surface re-entrantly collided with native editing-transaction invariants; same underlying theme (native engine assuming a stability the script breaks).
