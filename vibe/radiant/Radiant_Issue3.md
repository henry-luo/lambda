# Radiant Issue — native rich-transaction crash on script-handled structural edits

**Filed:** 2026-06-30 · **Updated:** 2026-06-30 · **Area:** Radiant native editing engine (`editing_dispatch` / `state_machine`) · **Severity:** high (process abort) · **Status:** fixed and verified
**Found while:** extending the Stage-4B plain-DOM JS editor fixtures under Radiant to cover structural edits (Enter/Backspace) — see `vibe/editing/Radiant_Editor_Stage4B.md`.

---

## TL;DR

Pressing **Enter** (or any structural edit) in the Stage-4B JS editor under Radiant aborted the process with a debug assertion:

```
state_machine: assertion failed after focus_transition:
  SM_INV_EDITING_TARGET_RANGES: active transaction has no surface
```

The native engine still wraps every `beforeinput` on a contenteditable surface in a **rich-transaction state machine** (`editing_run_transaction`), even though, under Stage 4B, the JS editor `preventDefault`s and applies the edit itself. When the JS handler's reconcile **splits/removes the focused block** (e.g. Enter), it synchronously detaches the surface view *inside* the native `beforeinput` dispatch. That fires a re-entrant `focus_clear` → `focus_transition`, whose end-of-transition invariant check finds the rich transaction still "active" but its surface gone, and aborts.

Plain typing did **not** trigger it: typing reconciles a text leaf in place (no focused-node removal, no `focus_transition`).

The fix keeps the native engine present (Phase 5 has not retired it yet) but makes it **tolerate a script that owns the apply path and reconciles the surface re-entrantly**, which is exactly the Stage-4B contract.

---

## Symptom

Headless event simulation against `test/html/editor-dom.html`:

- `type "ZZ"` → fine (in-place text-leaf reconcile).
- `key_press Enter` → process abort at `state_machine.cpp` `radiant_state_assert_valid`, message `SM_INV_EDITING_TARGET_RANGES: active transaction has no surface`.

Log trace immediately before the abort:

```
js_dom_dispatch_event: dispatched 'beforeinput' on … (prevented=1)
caret_clear
selection_clear
focus_clear
state_machine: assertion failed after focus_transition: SM_INV_EDITING_TARGET_RANGES: active transaction has no surface
```

---

## Root Cause

`editing_run_transaction` (`radiant/editing_dispatch.cpp`) drives a rich-transaction phase machine:

```
OPEN → (dispatch beforeinput to script) → BEFOREINPUT → [MUTATE] → [INPUT] → COMMIT(IDLE)
```

The `beforeinput` dispatch (`editing_dispatch_beforeinput_ex`, the `hooks->dispatch_input_event` call) runs the **JS handler synchronously**. For a structural edit the handler:

1. `preventDefault()`s (so the native engine performs **no** mutation — gated by `!prevented && !lambda_handled`),
2. edits its own model,
3. **reconciles the DOM** — removing the focused block's subtree, which calls `dom_pre_remove` → clears caret/selection and ultimately `focus_clear` → `focus_transition`,
4. restores the selection asynchronously.

Two distinct failures resulted:

1. **Re-entrant assert during dispatch.** The `focus_transition` triggered by step 3 runs `radiant_state_validate_interaction` while `rich_transaction_phase != IDLE` and the surface is momentarily gone → `SM_INV_EDITING_TARGET_RANGES`.
2. **Post-dispatch re-activation against a detached surface.** After the dispatch returns, `editing_run_transaction` re-activates `EDITING_RICH_TX_BEFOREINPUT` with the now-detached target view and asserts again (`editing_transaction_beforeinput`). The existing selection-handoff re-sync (`editing_dispatch_sync_rich_transaction_to_selection`) couldn't recover because the script restores the selection *asynchronously*, so at that instant focus/selection are cleared.

The native transaction is **inert** for a script-handled edit (it never mutates), so both asserts are false positives against a surface the script legitimately owns.

---

## Fix

All changes are gated so **pure native editing is byte-for-byte unaffected** (they only engage when a script `preventDefault`s, or while the substrate is mid script-dispatch).

| What | Where |
|---|---|
| New flag `rich_transaction_in_script_dispatch` on `EditingInteractionState` | `radiant/state_store.hpp` |
| Set the flag around the synchronous script `beforeinput` dispatch | `radiant/editing_dispatch.cpp` (`editing_dispatch_beforeinput_ex`) |
| Suspend the target-range invariant while the flag is set (re-entrant script reconcile window) | `radiant/state_machine.cpp` (`validate_editing_target_ranges_invariant`) |
| After dispatch, when the script `preventDefault`'d, re-anchor the transaction to the editing **host** (which survives reconcile) instead of the detached leaf view | `radiant/editing_dispatch.cpp` (`editing_run_transaction`) |

The host re-anchor is the load-bearing piece: `current_surface.owner` (the contenteditable host) is never removed by a child-subtree reconcile, so resolving a fresh surface from it gives the transaction a live target for its remaining (no-op) phases through `COMMIT`.

---

## Verification

Verified on 2026-06-30 (`make build`):

- `test/ui/editor4b/enter-split.json` (type → Enter → type) — **pass** (was a hard process abort).
- `test/ui/editor4b/backspace-delete.json` — **pass**.
- Full `test/editor-js/tools/run-radiant-fixtures.sh` — **9/9 pass** (6 prior + `enter-split` + `backspace-delete` + `select-all-replace`); no regression.
- `make test-radiant-baseline` — unchanged from baseline.

---

## Follow-up — whole-document-replace focus loss (also fixed, 2026-06-30)

Structural edits that **replace the whole document** (e.g. native Cmd+A then type) initially still produced wrong results: only the first character landed, the rest were dropped.

**Root cause (traced via `focus_clear` backtrace):** a small in-block edit (Enter split) takes Radiant's **incremental** rebuild path (`post_html_handler_incremental_rebuild`), which preserves the view tree and focus. A whole-doc replace fails the incremental check (`fallback=structural-css-risk`) and falls to the **full** rebuild (`post_html_handler_rebuild`), which destroys the entire view tree and **clears focus** (the focused `View*` is about to be freed) — but, unlike the Lambda-template path (`rebuild_lambda_doc`) and the form-control path (`restore_form_text_focus_after_input`), it **never restored focus**. The contenteditable host was left blurred, so the event simulator's subsequent `sim_text_input` keystrokes were not delivered.

**Fix:** in `post_html_handler_rebuild` (`radiant/event.cpp`), before clearing focus, capture the focused element if it resolves to a rich editing host (read from the *live focus*, since the reconcile's caret/focus churn may already have cleared `active_surface`); after relayout, if that host is still connected to the document, re-focus it. The DOM tree survives the view-tree teardown, so the freshly-laid-out element node is itself the focus target. Gated to rich editing hosts — form controls and other focus targets are unaffected.

**Verified:** `test/ui/editor4b/select-all-replace.json` (Cmd+A → type `Hello` → Enter → type `World`, asserting blocks `Hello`/`World`) passes; full fixture suite **9/9**; radiant baseline unchanged.

In-block structural edits (Enter inside a block) already preserved focus via the incremental path; the fix closes the full-rebuild case.

---

## Notes

- The native rich-transaction wrapper is vestigial for script-routed contenteditable; Stage 4B Phase 3 will reduce `contenteditable` to a routing flag and stop wrapping it at all, and Phase 5 will delete the engine. This fix is the minimal "tolerate the script" step that unblocks structural-edit fixtures in the meantime.
