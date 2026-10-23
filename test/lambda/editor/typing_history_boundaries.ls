import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let base = edit_open(node('doc',[node('paragraph',[text("")])]),editor_schemas.markdown,
    text_selection(pos([0,0],0),pos([0,0],0)))
let typed = edit_exec(edit_exec(base,edit_cmd_insert_text("Hello")),edit_cmd_insert_text(" world"))
len(typed.history.undo) == 1
doc_text(edit_exec(typed,edit_cmd_history_undo()).doc) == ""
let selected = edit_set_selection(typed,text_selection(pos([0,0],0),pos([0,0],5)))
let replaced = edit_exec(selected,edit_cmd_insert_text("Hi"))
doc_text(replaced.doc) == "Hi world"
len(replaced.history.undo) == 2
doc_text(edit_exec(replaced,edit_cmd_history_undo()).doc) == "Hello world"
let continued = edit_exec(replaced,edit_cmd_insert_text("!"))
doc_text(continued.doc) == "Hi! world"
len(continued.history.undo) == 2
doc_text(edit_exec(continued,edit_cmd_history_undo()).doc) == "Hello world"
let moved = edit_exec(edit_set_selection(typed,text_selection(pos([0,0],0),pos([0,0],0))),edit_cmd_insert_text("Oh "))
len(moved.history.undo) == 2
doc_text(edit_exec(moved,edit_cmd_history_undo()).doc) == "Hello world"
let all = edit_exec(edit_set_selection(typed,all_selection()),edit_cmd_insert_text("New"))
len(all.history.undo) == 2
doc_text(edit_exec(all,edit_cmd_history_undo()).doc) == "Hello world"
doc_text(edit_exec(edit_exec(all,edit_cmd_history_undo()),edit_cmd_history_redo()).doc) == "New"
