// commands_basic.ls — exercise the editing commands (Phase R4)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_transaction
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_commands

let d0 = node('doc', [
  node('paragraph', [text("Hello, world.")]),
  node('paragraph', [text("Second.")])
])
let caret0 = text_selection(pos([0, 0], 5), pos([0, 0], 5))
let s0 = {doc: d0, selection: caret0}

// ---------------------------------------------------------------------------
// cmd_insert_text — collapsed caret inserts; non-collapsed selection replaces
// ---------------------------------------------------------------------------
let tx_ins = cmd_insert_text(s0, " there")
"ins doc:";    doc_text(tx_ins.doc_after) == "Hello there, world.Second."
"ins caret:";  tx_ins.sel_after.anchor.offset == 11
"ins is collapsed:"; tx_ins.sel_after.anchor.offset == tx_ins.sel_after.head.offset

// non-collapsed selection: select "world" (chars 7..12) and replace
let sel_sel = text_selection(pos([0, 0], 7), pos([0, 0], 12))
let s_sel = {doc: d0, selection: sel_sel}
let tx_rep = cmd_insert_text(s_sel, "Lambda")
"replace doc:";  doc_text(tx_rep.doc_after) == "Hello, Lambda.Second."
"replace caret:"; tx_rep.sel_after.anchor.offset == 13

// ---------------------------------------------------------------------------
// cmd_delete_backward
// ---------------------------------------------------------------------------
//   collapsed at offset 5 -> delete the comma at index 4 (well, char before 5 = ',')
let tx_db = cmd_delete_backward(s0)
"del-back doc:";  doc_text(tx_db.doc_after) == "Hell, world.Second."
"del-back caret:"; tx_db.sel_after.anchor.offset == 4

// at start of leaf: returns null
let s_start = {doc: d0, selection: text_selection(pos([0, 0], 0), pos([0, 0], 0))}
"del-back at start null:"; cmd_delete_backward(s_start) == null

// non-collapsed: deletes the range
let tx_db2 = cmd_delete_backward(s_sel)
"del-back range doc:"; doc_text(tx_db2.doc_after) == "Hello, .Second."
"del-back range caret:"; tx_db2.sel_after.anchor.offset == 7

// ---------------------------------------------------------------------------
// cmd_delete_forward
// ---------------------------------------------------------------------------
let tx_df = cmd_delete_forward(s0)
"del-fwd doc:";   doc_text(tx_df.doc_after) == "Hello world.Second."
"del-fwd caret:"; tx_df.sel_after.anchor.offset == 5

// at end of leaf: null
let s_end = {doc: d0, selection: text_selection(pos([0, 0], 13), pos([0, 0], 13))}
"del-fwd at end null:"; cmd_delete_forward(s_end) == null

// ---------------------------------------------------------------------------
// cmd_delete_word_backward / cmd_insert_line_break
// ---------------------------------------------------------------------------
let word_doc = node('doc', [node('paragraph', [text("one two   ")])])
let word_state = {doc: word_doc, selection: text_selection(pos([0, 0], 10), pos([0, 0], 10))}
let tx_dw = cmd_delete_word_backward(word_state)
"del-word doc:"; doc_text(tx_dw.doc_after) == "one "
"del-word caret:"; tx_dw.sel_after.anchor.offset == 4
"del-word start null:"; cmd_delete_word_backward({doc: word_doc, selection: text_selection(pos([0, 0], 0), pos([0, 0], 0))}) == null

let line_state = {doc: d0, selection: text_selection(pos([0, 0], 5), pos([0, 0], 5))}
let tx_lb = cmd_insert_line_break(line_state)
"line-break children:"; len(node_at(tx_lb.doc_after, [0]).content) == 3
"line-break tag:"; node_at(tx_lb.doc_after, [0, 1]).tag == 'hard_break'
"line-break left:"; node_at(tx_lb.doc_after, [0, 0]).text == "Hello"
"line-break right:"; node_at(tx_lb.doc_after, [0, 2]).text == ", world."
"line-break caret path:"; path_equal(tx_lb.sel_after.anchor.path, [0, 2])
"line-break caret offset:"; tx_lb.sel_after.anchor.offset == 0

let list_doc = node('doc', [node_attrs('list', [{name: 'ordered', value: false}], [
  node('list_item', [node('paragraph', [text("A")])]),
  node('list_item', [node('paragraph', [text("B")])]),
  node('list_item', [node('paragraph', [text("C")])])
])])
let list_state = {doc: list_doc, selection: text_selection(pos([0, 1, 0, 0], 1), pos([0, 1, 0, 0], 1))}
let tx_indent = cmd_indent_list_item(list_state)
"indent top count:"; len(node_at(tx_indent.doc_after, [0]).content) == 2
"indent nested text:"; doc_text(node_at(tx_indent.doc_after, [0, 0, 1, 0])) == "B"
"indent selected parent:"; path_equal(tx_indent.sel_after.path, [0, 0])

let outdent_state = {doc: tx_indent.doc_after, selection: text_selection(pos([0, 0, 1, 0, 0, 0], 1), pos([0, 0, 1, 0, 0, 0], 1))}
let tx_outdent = cmd_outdent_list_item(outdent_state)
"outdent top count:"; len(node_at(tx_outdent.doc_after, [0]).content) == 3
"outdent middle text:"; doc_text(node_at(tx_outdent.doc_after, [0, 1])) == "B"
"outdent selected:"; path_equal(tx_outdent.sel_after.path, [0, 1])
"indent first null:"; cmd_indent_list_item({doc: list_doc, selection: node_selection([0, 0])}) == null

// ---------------------------------------------------------------------------
// cmd_toggle_mark
// ---------------------------------------------------------------------------
let tx_bold = cmd_toggle_mark(s0, 'strong')
let leaf_after = node_at(tx_bold.doc_after, [0, 0])
"toggle add:";    has_mark(leaf_after.marks, 'strong')
"toggle preserves text:"; leaf_after.text == "Hello, world."
// toggling again removes
let s_marked = {doc: tx_bold.doc_after, selection: caret0}
let tx_unbold = cmd_toggle_mark(s_marked, 'strong')
let leaf_after2 = node_at(tx_unbold.doc_after, [0, 0])
"toggle remove:"; not has_mark(leaf_after2.marks, 'strong')

// ---------------------------------------------------------------------------
// cmd_set_block_type
// ---------------------------------------------------------------------------
let tx_h = cmd_set_block_type(s0, 'heading')
let block_after = node_at(tx_h.doc_after, [0])
"set-type tag:";   block_after.tag == 'heading'
"set-type text preserved:"; doc_text(block_after) == "Hello, world."

// ---------------------------------------------------------------------------
// cmd_split_block — split paragraph[0] at offset 5
// ---------------------------------------------------------------------------
let tx_sp = cmd_split_block(s0)
"split block count:"; len(tx_sp.doc_after.content)
let split_left  = node_at(tx_sp.doc_after, [0])
let split_right = node_at(tx_sp.doc_after, [1])
"split left text:";  doc_text(split_left)  == "Hello"
"split right text:"; doc_text(split_right) == ", world."
"split right tag preserved:"; split_right.tag == 'paragraph'
"split caret path[0]:"; tx_sp.sel_after.anchor.path[0]
"split caret path[1]:"; tx_sp.sel_after.anchor.path[1]
"split caret offset:"; tx_sp.sel_after.anchor.offset == 0
// untouched second paragraph still present
"split tail:"; doc_text(node_at(tx_sp.doc_after, [2])) == "Second."

// split with caret at offset 0 -> empty left
let s_at0 = {doc: d0, selection: text_selection(pos([0, 0], 0), pos([0, 0], 0))}
let tx_sp0 = cmd_split_block(s_at0)
"split-at-0 left:";  len(doc_text(node_at(tx_sp0.doc_after, [0]))) == 0
"split-at-0 right:"; doc_text(node_at(tx_sp0.doc_after, [1])) == "Hello, world."

// split at end of a non-default block creates the schema default block
let heading_doc = node('doc', [node_attrs('heading', [{name: 'level', value: 2}], [text("Title")])])
let heading_state = {doc: heading_doc, selection: text_selection(pos([0, 0], 5), pos([0, 0], 5))}
let tx_heading_split = cmd_split_block(heading_state)
"split heading right tag:"; node_at(tx_heading_split.doc_after, [1]).tag == 'paragraph'
"split heading right attrs:"; len(node_at(tx_heading_split.doc_after, [1]).attrs) == 0
"split heading left tag:"; node_at(tx_heading_split.doc_after, [0]).tag == 'heading'

// ---------------------------------------------------------------------------
// chain — first non-null wins
// ---------------------------------------------------------------------------
fn never(_)  => null
fn always(state) => cmd_insert_text(state, "X")
let tx_chain = chain(s0, [never, always, never])
"chain doc:"; doc_text(tx_chain.doc_after) == "HelloX, world.Second."

// chain when nothing applies
"chain empty:"; chain(s0, [never, never]) == null

// ---------------------------------------------------------------------------
// Round-trip via tx_invert: undo an insert
// ---------------------------------------------------------------------------
let inv = tx_invert(tx_ins)
let restored = step_apply(inv.steps[0], tx_ins.doc_after)
"undo insert:"; doc_text(restored) == doc_text(d0)

// ---------------------------------------------------------------------------
// Structural drag/drop helpers — insert and move source-tree subtrees
// ---------------------------------------------------------------------------
let drop_doc = node('doc', [
  node('paragraph', [text("A")]),
  node('paragraph', [text("B")]),
  node('paragraph', [text("C")])
])
let drop_state = {doc: drop_doc, selection: node_selection([0])}

let tx_insert_at = cmd_insert_at(drop_state, [], 1, [node('paragraph', [text("X")])])
"insert-at order:"; [for (n in tx_insert_at.doc_after.content) doc_text(n)] == ["A", "X", "B", "C"]
"insert-at selected:"; path_equal(tx_insert_at.sel_after.path, [1])

let tx_move_later = cmd_move_node(drop_state, [0], [], 3)
"move later order:"; [for (n in tx_move_later.doc_after.content) doc_text(n)] == ["B", "C", "A"]
"move later selected:"; path_equal(tx_move_later.sel_after.path, [2])

let tx_move_earlier = cmd_move_node(drop_state, [2], [], 0)
"move earlier order:"; [for (n in tx_move_earlier.doc_after.content) doc_text(n)] == ["C", "A", "B"]
"move earlier selected:"; path_equal(tx_move_earlier.sel_after.path, [0])

"move same-position null:"; cmd_move_node(drop_state, [1], [], 1) == null
"move root null:"; cmd_move_node(drop_state, [], [], 0) == null

let nested_doc = node('doc', [node('list', [
  node('list_item', [node('paragraph', [text("child")])])
])])
let nested_state = {doc: nested_doc, selection: node_selection([0])}
"move into descendant null:"; cmd_move_node(nested_state, [0], [0, 0], 0) == null
