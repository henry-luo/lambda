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

let s_all = {doc: d0, selection: all_selection()}
let tx_all_insert = cmd_insert_text(s_all, "All new")
"insert all doc:"; doc_text(tx_all_insert.doc_after) == "All new"
"insert all count:"; len(tx_all_insert.doc_after.content) == 1
"insert all tag:"; tx_all_insert.doc_after.content[0].tag == 'paragraph'
"insert all caret path:"; path_equal(tx_all_insert.sel_after.anchor.path, [0, 0])
"insert all caret off:"; tx_all_insert.sel_after.anchor.offset == 7

let tx_all_insert_marked = cmd_insert_text({doc: d0, selection: all_selection(), stored_marks: ['strong']}, "Bold")
"insert all mark:"; has_mark(node_at(tx_all_insert_marked.doc_after, [0, 0]).marks, 'strong')

let atom_doc = node('doc', [
  node('paragraph', [text("A")]),
  node('image', []),
  node('paragraph', [text("C")])
])
let atom_state = {doc: atom_doc, selection: node_selection([1])}
let tx_node_insert = cmd_insert_text(atom_state, "Inserted")
"insert node doc:"; [for (n in tx_node_insert.doc_after.content) doc_text(n)] == ["A", "Inserted", "C"]
"insert node tag:"; node_at(tx_node_insert.doc_after, [1]).tag == 'paragraph'
"insert node caret:"; path_equal(tx_node_insert.sel_after.anchor.path, [1, 0]) and tx_node_insert.sel_after.anchor.offset == 8

let inline_atom_doc = node('doc', [node('paragraph', [text("A"), node('image', []), text("C")])])
let tx_inline_node_insert = cmd_insert_text({doc: inline_atom_doc, selection: node_selection([0, 1])}, "B")
"insert inline node doc:"; doc_text(tx_inline_node_insert.doc_after) == "ABC"
"insert inline node text:"; node_at(tx_inline_node_insert.doc_after, [0, 1]).text == "B"
"insert inline node caret:"; path_equal(tx_inline_node_insert.sel_after.anchor.path, [0, 1]) and tx_inline_node_insert.sel_after.anchor.offset == 1

let mark_span_doc = node('doc', [node('paragraph', [
  text_marked("Hello", ['strong']),
  text(" "),
  text_marked("world", ['em'])
])])
let mark_span_sel = text_selection(pos([0, 0], 2), pos([0, 2], 3))
let mark_span_state = {doc: mark_span_doc, selection: mark_span_sel}
let tx_span_insert = cmd_insert_text(mark_span_state, "X")
"insert span doc:"; doc_text(tx_span_insert.doc_after) == "HeXld"
"insert span children:"; len(node_at(tx_span_insert.doc_after, [0]).content) == 3
"insert span mark:"; has_mark(node_at(tx_span_insert.doc_after, [0, 1]).marks, 'strong')
"insert span tail mark:"; has_mark(node_at(tx_span_insert.doc_after, [0, 2]).marks, 'em')
"insert span caret:"; path_equal(tx_span_insert.sel_after.anchor.path, [0, 1]) and tx_span_insert.sel_after.anchor.offset == 1

// ---------------------------------------------------------------------------
// cmd_delete_backward
// ---------------------------------------------------------------------------
//   collapsed at offset 5 -> delete the comma at index 4 (well, char before 5 = ',')
let tx_db = cmd_delete_backward(s0)
"del-back doc:";  doc_text(tx_db.doc_after) == "Hell, world.Second."
"del-back caret:"; tx_db.sel_after.anchor.offset == 4

// at start of first block: returns null
let s_start = {doc: d0, selection: text_selection(pos([0, 0], 0), pos([0, 0], 0))}
"del-back at start null:"; cmd_delete_backward(s_start) == null

// at start of a sibling block: joins with previous sibling block
let s_join_back = {doc: d0, selection: text_selection(pos([1, 0], 0), pos([1, 0], 0))}
let tx_join_back = cmd_delete_backward(s_join_back)
"del-back join count:"; len(tx_join_back.doc_after.content) == 1
"del-back join doc:"; doc_text(tx_join_back.doc_after) == "Hello, world.Second."
"del-back join caret path:"; path_equal(tx_join_back.sel_after.anchor.path, [0, 0])
"del-back join caret off:"; tx_join_back.sel_after.anchor.offset == 13

// non-collapsed: deletes the range
let tx_db2 = cmd_delete_backward(s_sel)
"del-back range doc:"; doc_text(tx_db2.doc_after) == "Hello, .Second."
"del-back range caret:"; tx_db2.sel_after.anchor.offset == 7
let tx_db_span = cmd_delete_backward(mark_span_state)
"del-back span doc:"; doc_text(tx_db_span.doc_after) == "Held"
"del-back span caret:"; path_equal(tx_db_span.sel_after.anchor.path, [0, 0]) and tx_db_span.sel_after.anchor.offset == 2
let full_span_state = {doc: mark_span_doc, selection: text_selection(pos([0, 0], 0), pos([0, 2], 5))}
let tx_db_full_span = cmd_delete_backward(full_span_state)
"del-back full span doc:"; doc_text(tx_db_full_span.doc_after) == ""
"del-back full span leaf:"; is_text(node_at(tx_db_full_span.doc_after, [0, 0]))
"del-back full span caret:"; path_equal(tx_db_full_span.sel_after.anchor.path, [0, 0]) and tx_db_full_span.sel_after.anchor.offset == 0

let tx_db_all = cmd_delete_backward(s_all)
"del-back all doc:"; doc_text(tx_db_all.doc_after) == ""
"del-back all block:"; tx_db_all.doc_after.content[0].tag == 'paragraph'
"del-back all caret:"; path_equal(tx_db_all.sel_after.anchor.path, [0, 0]) and tx_db_all.sel_after.anchor.offset == 0

let tx_db_node = cmd_delete_backward(atom_state)
"del-back node order:"; [for (n in tx_db_node.doc_after.content) doc_text(n)] == ["A", "C"]
"del-back node caret:"; path_equal(tx_db_node.sel_after.anchor.path, []) and tx_db_node.sel_after.anchor.offset == 1
let tx_db_only_node = cmd_delete_backward({doc: node('doc', [node('image', [])]), selection: node_selection([0])})
"del-back only node doc:"; doc_text(tx_db_only_node.doc_after) == ""
"del-back only node caret:"; path_equal(tx_db_only_node.sel_after.anchor.path, [0, 0]) and tx_db_only_node.sel_after.anchor.offset == 0

// ---------------------------------------------------------------------------
// cmd_delete_forward
// ---------------------------------------------------------------------------
let tx_df = cmd_delete_forward(s0)
"del-fwd doc:";   doc_text(tx_df.doc_after) == "Hello world.Second."
"del-fwd caret:"; tx_df.sel_after.anchor.offset == 5

// at end of last leaf: null
let s_end = {doc: d0, selection: text_selection(pos([0, 0], 13), pos([0, 0], 13))}
let tx_join_fwd = cmd_delete_forward(s_end)
"del-fwd join count:"; len(tx_join_fwd.doc_after.content) == 1
"del-fwd join doc:"; doc_text(tx_join_fwd.doc_after) == "Hello, world.Second."
"del-fwd join caret path:"; path_equal(tx_join_fwd.sel_after.anchor.path, [0, 0])
"del-fwd join caret off:"; tx_join_fwd.sel_after.anchor.offset == 13

let s_doc_end = {doc: d0, selection: text_selection(pos([1, 0], 7), pos([1, 0], 7))}
"del-fwd at doc end null:"; cmd_delete_forward(s_doc_end) == null
let tx_df_all = cmd_delete_forward(s_all)
"del-fwd all doc:"; doc_text(tx_df_all.doc_after) == ""
"del-fwd all caret:"; path_equal(tx_df_all.sel_after.anchor.path, [0, 0]) and tx_df_all.sel_after.anchor.offset == 0
let tx_df_node = cmd_delete_forward(atom_state)
"del-fwd node order:"; [for (n in tx_df_node.doc_after.content) doc_text(n)] == ["A", "C"]
"del-fwd node caret:"; path_equal(tx_df_node.sel_after.anchor.path, []) and tx_df_node.sel_after.anchor.offset == 1
let tx_df_span = cmd_delete_forward(mark_span_state)
"del-fwd span doc:"; doc_text(tx_df_span.doc_after) == "Held"
"del-fwd span caret:"; path_equal(tx_df_span.sel_after.anchor.path, [0, 0]) and tx_df_span.sel_after.anchor.offset == 2

// ---------------------------------------------------------------------------
// cmd_delete_word_backward / cmd_insert_line_break
// ---------------------------------------------------------------------------
let word_doc = node('doc', [node('paragraph', [text("one two   ")])])
let word_state = {doc: word_doc, selection: text_selection(pos([0, 0], 10), pos([0, 0], 10))}
let tx_dw = cmd_delete_word_backward(word_state)
"del-word doc:"; doc_text(tx_dw.doc_after) == "one "
"del-word caret:"; tx_dw.sel_after.anchor.offset == 4
"del-word start null:"; cmd_delete_word_backward({doc: word_doc, selection: text_selection(pos([0, 0], 0), pos([0, 0], 0))}) == null
let tx_dw_all = cmd_delete_word_backward(s_all)
"del-word all doc:"; doc_text(tx_dw_all.doc_after) == ""
"del-word all caret:"; path_equal(tx_dw_all.sel_after.anchor.path, [0, 0]) and tx_dw_all.sel_after.anchor.offset == 0
let tx_dw_node = cmd_delete_word_backward(atom_state)
"del-word node order:"; [for (n in tx_dw_node.doc_after.content) doc_text(n)] == ["A", "C"]
"del-word node caret:"; path_equal(tx_dw_node.sel_after.anchor.path, []) and tx_dw_node.sel_after.anchor.offset == 1
let tx_dw_span = cmd_delete_word_backward(mark_span_state)
"del-word span doc:"; doc_text(tx_dw_span.doc_after) == "Held"
"del-word span caret:"; path_equal(tx_dw_span.sel_after.anchor.path, [0, 0]) and tx_dw_span.sel_after.anchor.offset == 2

let line_state = {doc: d0, selection: text_selection(pos([0, 0], 5), pos([0, 0], 5))}
let tx_lb = cmd_insert_line_break(line_state)
"line-break children:"; len(node_at(tx_lb.doc_after, [0]).content) == 3
"line-break tag:"; node_at(tx_lb.doc_after, [0, 1]).tag == 'hard_break'
"line-break left:"; node_at(tx_lb.doc_after, [0, 0]).text == "Hello"
"line-break right:"; node_at(tx_lb.doc_after, [0, 2]).text == ", world."
"line-break caret path:"; path_equal(tx_lb.sel_after.anchor.path, [0, 2])
"line-break caret offset:"; tx_lb.sel_after.anchor.offset == 0
let tx_lb_span = cmd_insert_line_break(mark_span_state)
"line-break span doc:"; doc_text(tx_lb_span.doc_after) == "Held"
"line-break span children:"; len(node_at(tx_lb_span.doc_after, [0]).content) == 3
"line-break span tag:"; node_at(tx_lb_span.doc_after, [0, 1]).tag == 'hard_break'
"line-break span caret:"; path_equal(tx_lb_span.sel_after.anchor.path, [0, 2]) and tx_lb_span.sel_after.anchor.offset == 0

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
"toggle stored mark:"; has_mark(tx_get_meta(tx_bold, "storedMarks"), 'strong')
"toggle stored no doc change:"; doc_text(tx_bold.doc_after) == doc_text(d0)
let tx_bold_off = cmd_toggle_mark({doc: d0, selection: caret0, stored_marks: ['strong']}, 'strong')
"toggle stored off:"; len(tx_get_meta(tx_bold_off, "storedMarks")) == 0
let tx_stored_insert = cmd_insert_text({doc: d0, selection: caret0, stored_marks: tx_get_meta(tx_bold, "storedMarks")}, "! ")
"stored insert children:"; len(node_at(tx_stored_insert.doc_after, [0]).content) == 3
"stored insert mark:"; has_mark(node_at(tx_stored_insert.doc_after, [0, 1]).marks, 'strong')
"stored insert text:"; node_at(tx_stored_insert.doc_after, [0, 1]).text == "! "

let tx_range_bold = cmd_toggle_mark(s_sel, 'strong')
let leaf_after = node_at(tx_range_bold.doc_after, [0, 0])
"toggle add:";    has_mark(leaf_after.marks, 'strong')
"toggle preserves text:"; leaf_after.text == "Hello, world."
// toggling again removes
let s_marked = {doc: tx_range_bold.doc_after, selection: sel_sel}
let tx_unbold = cmd_toggle_mark(s_marked, 'strong')
let leaf_after2 = node_at(tx_unbold.doc_after, [0, 0])
"toggle remove:"; not has_mark(leaf_after2.marks, 'strong')

let tx_all_bold = cmd_toggle_mark({doc: d0, selection: all_selection()}, 'strong')
"toggle all add steps:"; len(tx_all_bold.steps) == 2
"toggle all add first:"; has_mark(node_at(tx_all_bold.doc_after, [0, 0]).marks, 'strong')
"toggle all add second:"; has_mark(node_at(tx_all_bold.doc_after, [1, 0]).marks, 'strong')
let tx_all_unbold = cmd_toggle_mark({doc: tx_all_bold.doc_after, selection: all_selection()}, 'strong')
"toggle all remove steps:"; len(tx_all_unbold.steps) == 2
"toggle all remove first:"; not has_mark(node_at(tx_all_unbold.doc_after, [0, 0]).marks, 'strong')
"toggle all remove second:"; not has_mark(node_at(tx_all_unbold.doc_after, [1, 0]).marks, 'strong')
let partial_mark_doc = node('doc', [
  node('paragraph', [text_marked("Marked", ['strong'])]),
  node('paragraph', [text("Plain")])
])
let tx_all_partial = cmd_toggle_mark({doc: partial_mark_doc, selection: all_selection()}, 'strong')
"toggle all partial steps:"; len(tx_all_partial.steps) == 1
"toggle all partial first kept:"; has_mark(node_at(tx_all_partial.doc_after, [0, 0]).marks, 'strong')
"toggle all partial second added:"; has_mark(node_at(tx_all_partial.doc_after, [1, 0]).marks, 'strong')
let tx_node_bold = cmd_toggle_mark({doc: d0, selection: node_selection([0])}, 'strong')
"toggle node steps:"; len(tx_node_bold.steps) == 1
"toggle node first:"; has_mark(node_at(tx_node_bold.doc_after, [0, 0]).marks, 'strong')
"toggle node second untouched:"; not has_mark(node_at(tx_node_bold.doc_after, [1, 0]).marks, 'strong')
let tx_node_unbold = cmd_toggle_mark({doc: tx_node_bold.doc_after, selection: node_selection([0])}, 'strong')
"toggle node remove:"; not has_mark(node_at(tx_node_unbold.doc_after, [0, 0]).marks, 'strong')
let tx_span_bold = cmd_toggle_mark(mark_span_state, 'strong')
"toggle span add steps:"; len(tx_span_bold.steps) == 2
"toggle span first kept:"; has_mark(node_at(tx_span_bold.doc_after, [0, 0]).marks, 'strong')
"toggle span middle added:"; has_mark(node_at(tx_span_bold.doc_after, [0, 1]).marks, 'strong')
"toggle span last added:"; has_mark(node_at(tx_span_bold.doc_after, [0, 2]).marks, 'strong')
let tx_span_unbold = cmd_toggle_mark({doc: tx_span_bold.doc_after, selection: mark_span_sel}, 'strong')
"toggle span remove steps:"; len(tx_span_unbold.steps) == 3
"toggle span remove first:"; not has_mark(node_at(tx_span_unbold.doc_after, [0, 0]).marks, 'strong')
"toggle span remove middle:"; not has_mark(node_at(tx_span_unbold.doc_after, [0, 1]).marks, 'strong')
"toggle span remove last:"; not has_mark(node_at(tx_span_unbold.doc_after, [0, 2]).marks, 'strong')

// ---------------------------------------------------------------------------
// cmd_set_block_type
// ---------------------------------------------------------------------------
let tx_h = cmd_set_block_type(s0, 'heading')
let block_after = node_at(tx_h.doc_after, [0])
"set-type tag:";   block_after.tag == 'heading'
"set-type text preserved:"; doc_text(block_after) == "Hello, world."
let tx_h_node = cmd_set_block_type({doc: d0, selection: node_selection([1])}, 'heading')
"set-type node tag:"; node_at(tx_h_node.doc_after, [1]).tag == 'heading'
"set-type node first untouched:"; node_at(tx_h_node.doc_after, [0]).tag == 'paragraph'
let tx_h_span = cmd_set_block_type(mark_span_state, 'heading')
"set-type span tag:"; node_at(tx_h_span.doc_after, [0]).tag == 'heading'
"set-type span text:"; doc_text(node_at(tx_h_span.doc_after, [0])) == "Hello world"
let tx_h_inline_node = cmd_set_block_type({doc: inline_atom_doc, selection: node_selection([0, 1])}, 'heading')
"set-type inline node parent:"; node_at(tx_h_inline_node.doc_after, [0]).tag == 'heading'
let tx_h_all = cmd_set_block_type({doc: d0, selection: all_selection()}, 'heading')
"set-type all steps:"; len(tx_h_all.steps) == 2
"set-type all first:"; node_at(tx_h_all.doc_after, [0]).tag == 'heading'
"set-type all second:"; node_at(tx_h_all.doc_after, [1]).tag == 'heading'
"set-type all selection:"; tx_h_all.sel_after.kind == 'all'

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

let tx_sp_span = cmd_split_block(mark_span_state)
"split span count:"; len(tx_sp_span.doc_after.content) == 2
"split span left:"; doc_text(node_at(tx_sp_span.doc_after, [0])) == "He"
"split span right:"; doc_text(node_at(tx_sp_span.doc_after, [1])) == "ld"
"split span left mark:"; has_mark(node_at(tx_sp_span.doc_after, [0, 0]).marks, 'strong')
"split span right mark:"; has_mark(node_at(tx_sp_span.doc_after, [1, 0]).marks, 'em')
"split span caret:"; path_equal(tx_sp_span.sel_after.anchor.path, [1, 0]) and tx_sp_span.sel_after.anchor.offset == 0

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

// ---------------------------------------------------------------------------
// Find/replace command — source-tree transaction over all text leaves
// ---------------------------------------------------------------------------
let replace_doc = node('doc', [
  node('paragraph', [text("foo foo")]),
  node('paragraph', [text("bar foo")])
])
let replace_state = {doc: replace_doc, selection: text_selection(pos([0, 0], 4), pos([0, 0], 7))}
let tx_replace_all = cmd_replace_all(replace_state, "foo", "qux")
"replace-all text:"; doc_text(tx_replace_all.doc_after) == "qux quxbar qux"
"replace-all steps:"; len(tx_replace_all.steps) == 2
"replace-all leaf1:"; node_at(tx_replace_all.doc_after, [0, 0]).text == "qux qux"
"replace-all leaf2:"; node_at(tx_replace_all.doc_after, [1, 0]).text == "bar qux"
"replace-all selection mapped:"; tx_replace_all.sel_after.anchor.offset == 7 and tx_replace_all.sel_after.head.offset == 7
let tx_replace_none = cmd_replace_all(replace_state, "zzz", "qux")
"replace-all none:"; tx_replace_none == null
"replace-all empty needle:"; cmd_replace_all(replace_state, "", "x") == null
