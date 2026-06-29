// cmd_delete_multi_node — the canonical multi-node-selection operation.
// Deletes every node in a MultiNodeSelection (descending order), exposed both
// as a command and through the editor API.
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_commands
import lambda.package.editor.mod_editor

let d = node('doc', [
  node('paragraph', [text("A")]),
  node('paragraph', [text("B")]),
  node('paragraph', [text("C")]),
  node('paragraph', [text("D")])
])

// 1. command level: delete B ([1]) and D ([3]) -> A, C remain
let st = {doc: d, selection: multi_node_selection([[1], [3]])}
let tx = cmd_delete_multi_node(st)
"steps:"; len(tx.steps)
"child_count:"; len(tx.doc_after.content)
"text:"; doc_text(tx.doc_after) == "AC"
"first_kept:"; doc_text(tx.doc_after.content[0]) == "A"
"second_kept:"; doc_text(tx.doc_after.content[1]) == "C"

// 2. resulting selection collapses to an empty multi-node selection
"sel_after_kind:"; tx.sel_after.kind
"sel_after_len:"; len(tx.sel_after.paths)

// 3. unordered paths are handled (descending sort internally)
let tx2 = cmd_delete_multi_node({doc: d, selection: multi_node_selection([[3], [1]])})
"unordered_text:"; doc_text(tx2.doc_after) == "AC"

// 4. delete all four -> empty doc body
let tx3 = cmd_delete_multi_node({doc: d, selection: multi_node_selection([[0], [1], [2], [3]])})
"delete_all_count:"; len(tx3.doc_after.content)

// 5. non-applicable selections return null
"empty_sel_null:"; cmd_delete_multi_node({doc: d, selection: multi_node_selection([])}) == null
"text_sel_null:"; cmd_delete_multi_node({doc: d, selection: text_selection(pos([0,0],0), pos([0,0],0))}) == null
"node_sel_null:"; cmd_delete_multi_node({doc: d, selection: node_selection([1])}) == null

// 6. through the editor API
let editor = edit_open(d, null, multi_node_selection([[1], [3]]))
"can_exec:"; edit_can_exec(editor, edit_cmd_delete_multi_node())
let e2 = edit_exec(editor, edit_cmd_delete_multi_node())
"api_text:"; doc_text(e2.doc) == "AC"
"api_count:"; len(e2.doc.content)
