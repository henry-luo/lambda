// mod_transaction.ls — transactions over the rich-text doc
// (Radiant Rich Text Editing, Phase R3 — §5.2).
//
// A Transaction bundles a sequence of Steps with the doc snapshots before/after,
// plus a mapped selection. Transactions are pure values; nothing mutates.
//
//   Transaction = {
//     doc_before:  Doc            // doc state when the tx was started
//     doc_after:   Doc            // doc state after applying every step
//     steps:       [Step...]      // applied in order
//     sel_before:  Selection|null
//     sel_after:   Selection|null
//     meta:        [{name, value}...]
//   }

import .mod_doc
import .mod_step
import .mod_source_pos

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

// Begin a transaction from a document and an optional starting selection.
pub fn tx_begin(doc, sel) =>
  {doc_before: doc, doc_after: doc, steps: [],
   sel_before: sel, sel_after: sel, meta: []}

// Add a step to the transaction. Returns a new transaction value.
pub fn tx_step(tx, step) {
  let new_doc = step_apply(step, tx.doc_after)
  let new_sel = if (tx.sel_after == null) { null } else { sel_map(step, tx.sel_after) }
  {doc_before: tx.doc_before,
   doc_after:  new_doc,
   steps:      [*tx.steps, step],
   sel_before: tx.sel_before,
   sel_after:  new_sel,
   meta:       tx.meta}
}

// Override the post-tx selection (e.g. an explicit caret placement after a
// command runs).
pub fn tx_set_selection(tx, sel) =>
  {doc_before: tx.doc_before, doc_after: tx.doc_after, steps: tx.steps,
   sel_before: tx.sel_before, sel_after: sel, meta: tx.meta}

// Attach a metadata entry (e.g. "scrollIntoView", "addToHistory").
pub fn tx_set_meta(tx, name, value) =>
  {doc_before: tx.doc_before, doc_after: tx.doc_after, steps: tx.steps,
   sel_before: tx.sel_before, sel_after: tx.sel_after,
   meta: [*tx.meta, {name: name, value: value}]}

fn find_meta_at(meta, name, i, n) {
  if (i >= n) { null }
  else if (meta[i].name == name) { meta[i].value }
  else { find_meta_at(meta, name, i + 1, n) }
}
pub fn tx_get_meta(tx, name) => find_meta_at(tx.meta, name, 0, len(tx.meta))

// ---------------------------------------------------------------------------
// Selection mapping (translate a Selection through one Step)
// ---------------------------------------------------------------------------

pub fn sel_map(step, sel) {
  if (sel.kind == 'all') { sel }
  else if (sel.kind == 'node') { sel }   // node selections survive identically; refinement deferred
  else if (sel.kind == 'text') {
    text_selection(step_map(step, sel.anchor), step_map(step, sel.head))
  }
  else { sel }
}

// ---------------------------------------------------------------------------
// Inversion — produce the transaction that undoes `tx`
// ---------------------------------------------------------------------------

// Build the list of inverted steps in reverse order, walking the original
// steps forward so each invert sees the doc state it was applied against.
fn invert_steps_at(steps, doc, i, n, acc) {
  if (i >= n) { acc }
  else {
    let inv = step_invert(steps[i], doc)
    let next = step_apply(steps[i], doc)
    invert_steps_at(steps, next, i + 1, n, [inv, *acc])
  }
}

pub fn tx_invert(tx) {
  let inv_steps = invert_steps_at(tx.steps, tx.doc_before, 0, len(tx.steps), [])
  {doc_before: tx.doc_after,
   doc_after:  tx.doc_before,
   steps:      inv_steps,
   sel_before: tx.sel_after,
   sel_after:  tx.sel_before,
   meta:       []}
}

// ---------------------------------------------------------------------------
// Step composition / mapping
// ---------------------------------------------------------------------------

// Map a SourcePos through every step in this transaction in order.
fn map_through_at(steps, p, i, n) {
  if (i >= n) { p }
  else { map_through_at(steps, step_map(steps[i], p), i + 1, n) }
}
pub fn tx_map_pos(tx, p) => map_through_at(tx.steps, p, 0, len(tx.steps))
