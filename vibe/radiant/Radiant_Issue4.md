# Radiant Issue — DOM mutation forces a full view-tree teardown and drops in-flight interaction state (DragDropState) mid-gesture

**Filed:** 2026-07-02 · **Closed:** 2026-07-08 · **Area:** Radiant event/reconcile path (`post_html_handler_rebuild` in `radiant/event.cpp`) · **Severity:** medium (broke gestures whose handlers mutate the DOM; forced per-feature workarounds) · **Status:** closed — fixed by retained DOM-mutation fallback, StateStore pruning/reprojection, and native `DragDropState` retention; implementation tracked in [`Radiant_DOM_Mutation.md`](Radiant_DOM_Mutation.md)
**Found while:** implementing HTML5 drag-reorder for the Stage-4C plain-DOM JS editor — see `vibe/editing/Radiant_Editor_Stage4C.md` ("drag-reorder" section).

**Fix record:** [`Radiant_DOM_Mutation.md`](Radiant_DOM_Mutation.md) is the source of truth for the structural fix: current DOM mutation flow, fallback reduction, StateStore/view-state retention across fallback, `DragDropState` retention/pruning, and the event_sim/unit-test coverage.

---

## TL;DR

**Fixed.** DOM mutation during HTML/JS event handling no longer forces the old
view-tree teardown path for ordinary fallback. `post_html_handler_rebuild(...)`
now uses a retained full-layout fallback when incremental reconcile is not
eligible: it resets layout-pool resources, keeps retained DOM/view identity,
relayouts from the mutated DOM, then prunes/reprojects StateStore owners for
nodes that actually disappeared.

Native drag now rides directly on `DragDropState` again. The old Stage-4C
file-static JS-DnD workaround (`g_jsdnd_*`) has been removed from
`radiant/event.cpp`; `dragover` handlers may mutate the DOM and the active drag
survives as long as its source node remains connected. If the source node is
removed, the drag state is deliberately cleared rather than left dangling.

Validated by the focused regression `dom_mutation_dragover_retains_dragdrop`,
the broader `dom_mutation_*` fixture pack, StateStore drag-pruning unit tests,
and the original Stage-4C `editor4c/drag-reorder.json` fixture.

The historical failure trace below is kept as the original root-cause record.

---

## Historical Symptom

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

## Historical Root Cause

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

## Historical Workaround (Retired)

The first drag-reorder workaround decoupled the JS-DnD lifecycle from
`DragDropState` (`radiant/event.cpp`):

- `g_jsdnd_active` — a file-static `bool` that nothing in the reconcile path touches, so it survives the teardown. All JS `dragover` dispatch is gated on this flag instead of `dd->active`.
- `g_jsdnd_source` — the drag source stored as a **DOM element pointer** (not a `View*`). The full teardown frees the *view* tree but **not** the *DOM* tree (DOM nodes live in the document arena), so this pointer stays valid to `mouseup`, when `dragend` is dispatched to it.
- The per-move drop target uses `evcon.target`, which is freshly hit-tested from the rebuilt view tree on every event, so it is always live.

That made drag-reorder work, but it was a **parallel state machine that
sidestepped the real problem.** It did not help:

- other consumers of `DragDropState` (native `dropzone` drops that also mutate the DOM),
- the caret `view` pointer, which is likewise nulled on every mutation and re-projected from scratch,
- performance: every keystroke/`dragover`/DOM edit still tore down and rebuilt the *entire* view tree.

This workaround is now retired. JS drag/drop dispatch is gated on retained
native `dd->active` / `dd->source_view`, and the Stage-4C `g_jsdnd_*` state no
longer exists in live code.

---

## Implemented Fix

The structural fix is specified and tracked in
[`Radiant_DOM_Mutation.md`](Radiant_DOM_Mutation.md). In short:

- reduce fallback by treating broad cases as retained recascade/reflow whenever possible;
- fallback now retains StateStore/view state for connected DOM nodes and prunes
  only removed nodes;
- `DragDropState` survives relayout when its source remains connected and clears
  when the source is removed;
- focused C++ StateStore tests and `test/ui` event_sim fixtures cover reconcile
  mode, state retention/pruning, and drag/drop survival.

That implementation supersedes the earlier two-part sketch in this issue note.

---

## Repro / verification

- `./test/test_ui_automation_gtest.exe --gtest_filter='UIAutomation/UIAutomationTest.RunTest/dom_mutation_dragover_retains_dragdrop' --gtest_color=no`
  — passed, 3/3 assertions.
- `./test/test_ui_automation_gtest.exe --gtest_filter='UIAutomation/UIAutomationTest.RunTest/dom_mutation_*' --gtest_color=no`
  — passed, 11/11 fixtures.
- `./test/test_state_store_gtest.exe --gtest_filter='*Drag*:*drag*:*StateStore*' --gtest_color=no`
  — passed, 4/4 StateStore drag-pruning tests.
- `./lambda.exe view test/html/editor-dom.html --event-file test/ui/editor4c/drag-reorder.json --headless --no-log --font-dir test/layout/data/font`
  — passed, 6/6 assertions.

## Related

- `vibe/radiant/Radiant_DOM_Mutation.md` — follow-up design record for DOM mutation handling, fallback reduction, StateStore retention, `DragDropState` anchoring, and tests.
- `vibe/editing/Radiant_Editor_Stage4C.md` — "drag-reorder" section (the workaround that motivated this issue, now retired).
- `vibe/radiant/Radiant_Issue3.md` — a sibling case where a script that reconciles the surface re-entrantly collided with native editing-transaction invariants; same underlying theme (native engine assuming a stability the script breaks).
