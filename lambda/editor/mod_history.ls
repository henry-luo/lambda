// mod_history.ls — undo/redo stack of inverted transactions
// (Radiant Rich Text Editing, Phase R3 — §5.4).
//
// History = { undo: [Transaction...],        // each is the *inverse* of an applied tx
//             redo: [Transaction...],        // tx values to re-apply on redo
//             undo_groups: [symbol|null...], // optional compression keys
//             redo_groups: [symbol|null...] }
//
// All operations are pure: every push / undo / redo returns a new history
// value alongside the new doc state.

import .mod_doc
import .mod_step
import .mod_transaction

pub fn history_new() => {undo: [], redo: [], undo_groups: [], redo_groups: []}

fn undo_groups(hist) => if (hist.undo_groups == null) [] else hist.undo_groups
fn redo_groups(hist) => if (hist.redo_groups == null) [] else hist.redo_groups

fn tx_history_group(tx) => tx_get_meta(tx, "historyGroup")

fn tx_merge(a, b) =>
  {doc_before: a.doc_before, doc_after: b.doc_after,
   steps: [*a.steps, *b.steps],
   sel_before: a.sel_before, sel_after: b.sel_after,
   meta: b.meta}

fn history_push_new(hist, tx, group) =>
  {undo: [*hist.undo, tx_invert(tx)], redo: [],
   undo_groups: [*undo_groups(hist), group], redo_groups: []}

// Record `tx` after it has been applied. `tx.doc_after` is the new doc.
// Pushes the inverse onto the undo stack and clears the redo stack. When two
// adjacent transactions share a non-null `historyGroup`, they are compressed
// into one undo item (used by typing runs; callers own timing/session policy).
pub fn history_push(hist, tx) {
  let group = tx_history_group(tx)
  let groups = undo_groups(hist)
  let n = len(hist.undo)
  if (group != null and n > 0 and len(groups) == n and groups[n - 1] == group) {
    let previous = tx_invert(hist.undo[n - 1])
    let merged = tx_merge(previous, tx)
    {undo: [*list_take(hist.undo, n - 1), tx_invert(merged)], redo: [],
     undo_groups: [*list_take(groups, n - 1), group], redo_groups: []}
  } else {
    history_push_new(hist, tx, group)
  }
}

pub fn can_undo(hist) => len(hist.undo) > 0
pub fn can_redo(hist) => len(hist.redo) > 0

// Pop the most recent inverse, apply it to `doc`, and move the original tx
// onto the redo stack.
//
// Returns: { doc, hist, sel } — sel is the selection to restore (sel_after of
// the inverse tx, which is sel_before of the original tx).
pub fn history_undo(hist, doc) {
  if (not can_undo(hist)) {
    {doc: doc, hist: hist, sel: null, ok: false}
  } else {
    let n = len(hist.undo)
    let inv = hist.undo[n - 1]
    let groups = undo_groups(hist)
    let group = if (len(groups) == n) groups[n - 1] else null
    // Apply the inverse — this is the tx that undoes the original.
    // The original tx is reconstructed by inverting again.
    let orig = tx_invert(inv)
    let new_undo = list_take(hist.undo, n - 1)
    let new_undo_groups = if (len(groups) == n) list_take(groups, n - 1) else groups
    {doc: inv.doc_after,
     hist: {undo: new_undo, redo: [*hist.redo, orig],
            undo_groups: new_undo_groups, redo_groups: [*redo_groups(hist), group]},
     sel: inv.sel_after,
     ok: true}
  }
}

// Pop the most recent redo entry, re-apply it, push its inverse to undo.
pub fn history_redo(hist, doc) {
  if (not can_redo(hist)) {
    {doc: doc, hist: hist, sel: null, ok: false}
  } else {
    let n = len(hist.redo)
    let tx = hist.redo[n - 1]
    let groups = redo_groups(hist)
    let group = if (len(groups) == n) groups[n - 1] else null
    let new_redo = list_take(hist.redo, n - 1)
    let new_redo_groups = if (len(groups) == n) list_take(groups, n - 1) else groups
    {doc: tx.doc_after,
     hist: {undo: [*hist.undo, tx_invert(tx)], redo: new_redo,
            undo_groups: [*undo_groups(hist), group], redo_groups: new_redo_groups},
     sel: tx.sel_after,
     ok: true}
  }
}

// Convenience: apply a single step as a one-step transaction and record it.
//   returns { doc, hist, sel, tx }
pub fn history_apply_step(hist, doc, step, sel_before, sel_after) {
  let tx0 = tx_begin(doc, sel_before)
  let tx1 = tx_step(tx0, step)
  let tx2 = if (sel_after == null) { tx1 } else { tx_set_selection(tx1, sel_after) }
  {doc: tx2.doc_after, hist: history_push(hist, tx2), sel: tx2.sel_after, tx: tx2}
}
