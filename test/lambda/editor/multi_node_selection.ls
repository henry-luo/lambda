// MultiNodeSelection model addition (Stage 4 §2.2). Constructor, selection_paths,
// selection_to_string over several nodes, and mapping through a step
// (shift survivors, drop deleted). Port of the JS reference behaviour.
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_transaction

let d = node('doc', [
  node('paragraph', [text("A")]),
  node('paragraph', [text("B")]),
  node('paragraph', [text("C")]),
  node('paragraph', [text("D")])
])

// 1. constructor + shape
let sel = multi_node_selection([[1], [3]])
"kind:"; sel.kind
"paths_len:"; len(sel.paths)
"first_path0:"; sel.paths[0][0]
"second_path0:"; sel.paths[1][0]

// 2. selection_paths helper
"sel_paths_len:"; len(selection_paths(sel))
"node_sel_paths:"; len(selection_paths(node_selection([2])))
"text_sel_paths:"; len(selection_paths(text_selection(pos([0,0],0), pos([0,0],1))))

// 3. selection_to_string concatenates the targeted nodes in order (B then D)
"multinode_text:"; selection_to_string(d, sel) == "BD"
"single_node_text:"; selection_to_string(d, node_selection([2])) == "C"

// 4. map through an insert at the front: every path index shifts +1
let ins = step_replace([], 0, 0, [node('hr', [])])
let sel_ins = sel_map(ins, sel)
"ins_kind:"; sel_ins.kind
"ins_len:"; len(sel_ins.paths)
"ins_p0:"; sel_ins.paths[0][0]
"ins_p1:"; sel_ins.paths[1][0]

// 5. map through a delete of index 1 (B): that path drops, [3] shifts to [2]
let del = step_replace([], 1, 2, [])
let sel_del = sel_map(del, sel)
"del_len:"; len(sel_del.paths)
"del_survivor:"; sel_del.paths[0][0]

// 6. map through a delete of both selected nodes -> empty survivors
let del_all = step_replace([], 0, 4, [node('paragraph', [text("X")])])
let sel_none = sel_map(del_all, sel)
"none_kind:"; sel_none.kind
"none_len:"; len(sel_none.paths)

// 7. transaction carries multi-node selection through tx_step
let tx = tx_begin(d, sel)
let tx2 = tx_step(tx, ins)
"tx_sel_kind:"; tx2.sel_after.kind
"tx_sel_p0:"; tx2.sel_after.paths[0][0]
