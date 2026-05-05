// Transactions, multi-step composition, and history (Phase R3)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_transaction
import lambda.package.editor.mod_history

let d0 = node('doc', [
  node('paragraph', [text("Hello, world.")]),
  node('paragraph', [text("Second.")])
])

let sel0 = text_selection(pos([0, 0], 0), pos([0, 0], 5))

// ---------------------------------------------------------------------------
// tx_begin / tx_step  — single step
// ---------------------------------------------------------------------------
let tx0 = tx_begin(d0, sel0)
"empty tx steps:";   len(tx0.steps)
"empty tx doc match:"; doc_text(tx0.doc_after) == doc_text(d0)

let s1 = step_replace_text([0, 0], 7, 12, "Lambda")
let tx1 = tx_step(tx0, s1)
"tx1 steps:";        len(tx1.steps)
"tx1 doc text:";     doc_text(tx1.doc_after) == "Hello, Lambda.Second."
"tx1 doc_before unchanged:"; doc_text(tx1.doc_before) == doc_text(d0)
// Selection mapped: [0,0]:0..5 sits before the edit -> unchanged.
"tx1 sel anchor:";   tx1.sel_after.anchor.offset
"tx1 sel head:";     tx1.sel_after.head.offset

// ---------------------------------------------------------------------------
// Two-step transaction with selection migration
// ---------------------------------------------------------------------------
let s2 = step_replace_text([0, 0], 0, 0, "[!] ")  // insert prefix
let tx2 = tx_step(tx1, s2)
"tx2 steps:";        len(tx2.steps)
"tx2 doc text:";     doc_text(tx2.doc_after) == "[!] Hello, Lambda.Second."
// First step left selection alone (offsets 0..5). The second insertion of
// 4 chars at offset 0 shifts both endpoints to 4..9.
"tx2 sel anchor:";   tx2.sel_after.anchor.offset
"tx2 sel head:";     tx2.sel_after.head.offset

// ---------------------------------------------------------------------------
// tx_invert reverses both doc and selection
// ---------------------------------------------------------------------------
let tx2_inv = tx_invert(tx2)
"inv steps:";        len(tx2_inv.steps)
let d2_back = step_apply(tx2_inv.steps[0], tx2.doc_after)
let d2_back2 = step_apply(tx2_inv.steps[1], d2_back)
"inv full doc:";     doc_text(d2_back2) == doc_text(d0)
"inv sel_before swapped:"; tx2_inv.sel_before.anchor.offset == 4
"inv sel_after swapped:";  tx2_inv.sel_after.anchor.offset == 0

// ---------------------------------------------------------------------------
// tx_set_meta / tx_get_meta
// ---------------------------------------------------------------------------
let tx3 = tx_set_meta(tx_set_meta(tx1, 'addToHistory', true), 'scrollIntoView', true)
"meta addToHistory:";   tx_get_meta(tx3, 'addToHistory')
"meta scrollIntoView:"; tx_get_meta(tx3, 'scrollIntoView')
"meta missing:";        tx_get_meta(tx3, 'unknown') == null
let tx3b = tx_set_meta(tx3, 'addToHistory', false)
"meta override value:"; tx_get_meta(tx3b, 'addToHistory') == false
"meta override count:"; len(tx3b.meta) == 2
let tx3c = tx_set_meta({doc_before: d0, doc_after: d0, steps: [], sel_before: sel0, sel_after: sel0,
  meta: [{name: 'mode', value: 'a'}, {name: 'mode', value: 'b'}]}, 'mode', 'c')
"meta dedupe value:"; tx_get_meta(tx3c, 'mode') == 'c'
"meta dedupe count:"; len(tx3c.meta) == 1

// ---------------------------------------------------------------------------
// History — push / undo / redo
// ---------------------------------------------------------------------------
let h0 = history_new()
"h0 can_undo:";  can_undo(h0)
"h0 can_redo:";  can_redo(h0)

// Apply step #1 and record
let r1 = history_apply_step(h0, d0, s1, sel0, null)
"r1 doc:";       doc_text(r1.doc) == "Hello, Lambda.Second."
"r1 can_undo:";  can_undo(r1.hist)
"r1 can_redo:";  can_redo(r1.hist)

// Apply step #2 on the new doc
let r2 = history_apply_step(r1.hist, r1.doc, s2, r1.sel, null)
"r2 doc:";       doc_text(r2.doc) == "[!] Hello, Lambda.Second."
"r2 undo depth:"; len(r2.hist.undo)

// Adjacent transactions with the same historyGroup compress into one undo item.
let tga0 = tx_set_meta(tx_step(tx_begin(d0, text_selection(pos([0, 0], 0), pos([0, 0], 0))),
  step_replace_text([0, 0], 0, 0, "A")), "historyGroup", "typing")
let hg1 = history_push(h0, tx_set_selection(tga0, text_selection(pos([0, 0], 1), pos([0, 0], 1))))
let tgb0 = tx_set_meta(tx_step(tx_begin(tga0.doc_after, text_selection(pos([0, 0], 1), pos([0, 0], 1))),
  step_replace_text([0, 0], 1, 1, "B")), "historyGroup", "typing")
let hg2 = history_push(hg1, tx_set_selection(tgb0, text_selection(pos([0, 0], 2), pos([0, 0], 2))))
"group undo depth:"; len(hg2.undo)
let hgu = history_undo(hg2, tgb0.doc_after)
"group undo doc:"; doc_text(hgu.doc) == doc_text(d0)
"group redo depth:"; len(hgu.hist.redo)
let hgr = history_redo(hgu.hist, hgu.doc)
"group redo doc:"; doc_text(hgr.doc) == "ABHello, world.Second."

// Undo once
let u1 = history_undo(r2.hist, r2.doc)
"u1 ok:";        u1.ok
"u1 doc:";       doc_text(u1.doc) == "Hello, Lambda.Second."
"u1 can_redo:";  can_redo(u1.hist)
"u1 undo depth:"; len(u1.hist.undo)
"u1 redo depth:"; len(u1.hist.redo)

// Undo twice — should restore the original
let u2 = history_undo(u1.hist, u1.doc)
"u2 ok:";        u2.ok
"u2 doc:";       doc_text(u2.doc) == doc_text(d0)
"u2 can_undo:";  can_undo(u2.hist)
"u2 redo depth:"; len(u2.hist.redo)

// Trying to undo with empty stack returns ok=false and the same doc
let u3 = history_undo(u2.hist, u2.doc)
"u3 ok:";        u3.ok
"u3 doc unchanged:"; doc_text(u3.doc) == doc_text(d0)

// Redo once -> back to "Hello, Lambda.Second."
let rr1 = history_redo(u2.hist, u2.doc)
"rr1 ok:";       rr1.ok
"rr1 doc:";      doc_text(rr1.doc) == "Hello, Lambda.Second."

// Redo twice -> back to fully edited
let rr2 = history_redo(rr1.hist, rr1.doc)
"rr2 ok:";       rr2.ok
"rr2 doc:";      doc_text(rr2.doc) == "[!] Hello, Lambda.Second."
"rr2 can_undo:"; can_undo(rr2.hist)
"rr2 redo depth:"; len(rr2.hist.redo)

// New edit after undo clears the redo stack
let u_after = history_undo(rr2.hist, rr2.doc)
"u_after redo:"; len(u_after.hist.redo)
let s_new = step_set_node_type([0], 'heading')
let r_new = history_apply_step(u_after.hist, u_after.doc, s_new, null, null)
"r_new redo cleared:"; len(r_new.hist.redo) == 0
"r_new tag:";    r_new.doc.content[0].tag == 'heading'

// ---------------------------------------------------------------------------
// tx_map_pos — translate a SourcePos through every step in a transaction
// ---------------------------------------------------------------------------
//   tx2 has [replace_text "world"->"Lambda", insert "[!] " at offset 0]
//   pos at offset 12 (the '.') in the original leaf -> after step1 it's at 13,
//   after step2 it's at 17.
let p_orig = pos([0, 0], 12)
let p_final = tx_map_pos(tx2, p_orig)
"map pos final:"; p_final.offset

let node_sel = node_selection([1])
let delete_first_tx = tx_step(tx_begin(d0, node_sel), step_replace([], 0, 1, []))
"node selection shift kind:"; delete_first_tx.sel_after.kind == 'node'
"node selection shift path:"; path_equal(delete_first_tx.sel_after.path, [0])
let delete_selected_tx = tx_step(tx_begin(d0, node_sel), step_replace([], 1, 2, []))
"node selection deleted kind:"; delete_selected_tx.sel_after.kind == 'text'
"node selection deleted offset:"; delete_selected_tx.sel_after.anchor.offset == 1
