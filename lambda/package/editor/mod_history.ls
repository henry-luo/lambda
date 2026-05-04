// mod_history.ls — undo/redo stack of inverted transactions
// (Radiant Rich Text Editing, Phase R3 — §5.4).
//
// History = { undo: [Transaction...],   // each is the *inverse* of an applied tx
//             redo: [Transaction...] }  // tx values to re-apply on redo
//
// All operations are pure: every push / undo / redo returns a new history
// value alongside the new doc state.

import .mod_doc
import .mod_step
import .mod_transaction

pub fn history_new() => {undo: [], redo: []}

// Record `tx` after it has been applied. `tx.doc_after` is the new doc.
// Pushes the inverse onto the undo stack and clears the redo stack.
pub fn history_push(hist, tx) =>
  {undo: [*hist.undo, tx_invert(tx)], redo: []}

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
    // Apply the inverse — this is the tx that undoes the original.
    // The original tx is reconstructed by inverting again.
    let orig = tx_invert(inv)
    let new_undo = list_take(hist.undo, n - 1)
    {doc: inv.doc_after,
     hist: {undo: new_undo, redo: [*hist.redo, orig]},
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
    let new_redo = list_take(hist.redo, n - 1)
    {doc: tx.doc_after,
     hist: {undo: [*hist.undo, tx_invert(tx)], redo: new_redo},
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
