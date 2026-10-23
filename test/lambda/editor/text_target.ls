import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let doc = node('doc',[node('p',[text("Keep this paragraph")]),node('p',[])])
let editor = edit_open(doc,editor_schemas.html5_subset,text_selection(pos([],0),pos([],0)))
// container coordinates must never turn the document or an atom into a text leaf.
not edit_can_exec(editor,edit_cmd_insert_text("a"))
edit_exec(editor,edit_cmd_insert_text("a")).doc==doc
len(edit_exec(editor,edit_cmd_insert_text("a")).history.undo)==0
let missing = edit_set_selection(editor,text_selection(pos([9],0),pos([9],0)))
not edit_can_exec(missing,edit_cmd_insert_text("a"))
edit_exec(missing,edit_cmd_insert_text("a")).doc==doc
// an empty paragraph remains an editable inline container.
let empty = edit_set_selection(editor,text_selection(pos([1],0),pos([1],0)))
edit_can_exec(empty,edit_cmd_insert_text("a"))
let typed = edit_exec(empty,edit_cmd_insert_text("a"))
typed.doc.content[0]==doc.content[0]
doc_text(typed.doc.content[1])=="a"
edit_exec(typed,edit_cmd_history_undo()).doc==doc
// DOM Select All can project a child range on the document, including formatted leaves.
let selected = edit_set_selection(editor,text_selection(pos([],0),pos([],2)))
let removed = edit_exec(selected,edit_cmd_delete_forward())
removed.doc.kind=='node' and removed.doc.tag=='doc'
len(removed.doc.content)==1 and removed.doc.content[0].tag=='p'
doc_text(removed.doc)==""
edit_exec(removed,edit_cmd_history_undo()).doc==doc
// partial structural ranges remove children without replacing the document itself.
let partial = edit_exec(edit_set_selection(editor,text_selection(pos([],0),pos([],1))),edit_cmd_delete_forward())
partial.doc.kind=='node' and partial.doc.content==[doc.content[1]]
edit_exec(partial,edit_cmd_history_undo()).doc==doc
