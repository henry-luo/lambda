import model: ~~.mod_model
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let doc = node('doc',[node('p',[text("First"),text_marked(" paragraph",['strong'])]),
    node('p',[text("Second paragraph")])])
let base = model.open_editor(doc)
let selected = model.select_paragraph(edit_set_selection(base,text_selection(pos([0,1],4),pos([0,0],2))))
selected.selection == text_selection(pos([0,0],0),pos([0,1],10))
selected.doc == doc
not edit_can_undo(selected)
let typed = model.execute(selected,"text.insert",{data:"Changed"})^
doc_text(typed.doc.content[0]) == "Changed"
doc_text(typed.doc.content[1]) == "Second paragraph"
model.execute(typed,"edit.undo")^.doc == doc
let second = model.select_paragraph(edit_set_selection(base,text_selection(pos([1,0],2),pos([1,0],2))))
second.selection == text_selection(pos([1,0],0),pos([1,0],16))
let empty = model.open_editor(node('doc',[node('p',[])]))
model.select_paragraph(empty).selection == text_selection(pos([0],0),pos([0],0))
let unicode = model.open_editor(node('doc',[node('ul',[node('li',[node('p',[text("Café 東京")])])])]))
let list_item = model.select_paragraph(unicode)
list_item.selection == text_selection(pos([0,0,0,0],0),pos([0,0,0,0],7))
let typing = model.execute(model.open_editor(node('doc',[node('p',[text("")])])),"text.insert",{data:"cat cat"})^
let replacement = model.execute(edit_set_selection(typing,text_selection(pos([0,0],0),pos([0,0],3))),
    "edit.replace",{replace:"dog"})^
doc_text(replacement.doc) == "dog cat"
len(replacement.history.undo) == 2
model.execute(replacement,"edit.undo")^.doc == typing.doc
