import model: ~~.mod_model
import files: ~~.mod_io
import surface: ~~.mod_surface
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let doc = node('doc',[node('ul',[node('li',[node('p',[text("First")])]),node('li',[node('p',[text("Second")])])])])
let selected = edit_set_selection(model.open_editor(doc),text_selection(pos([0,1,0,0],3),pos([0,1,0,0],3)))
let indented = model.execute(selected,"para.indent")^
attrs_get(node_at(indented.doc,[0,1]).attrs,'indent')==1
indented.selection==selected.selection
model.execute(indented,"edit.undo")^.doc==doc
model.execute(model.execute(indented,"edit.undo")^,"edit.redo")^.doc==indented.doc
let twice = model.execute(indented,"para.indent")^
attrs_get(node_at(twice.doc,[0,1]).attrs,'indent')==2
model.execute(twice,"para.outdent")^.doc==indented.doc
model.execute(indented,"para.outdent")^.doc==doc
let first = edit_set_selection(selected,text_selection(pos([0,0,0,0],0),pos([0,0,0,0],0)))
attrs_get(node_at(model.execute(first,"para.indent")^.doc,[0,0]).attrs,'indent')==1
files.restore(files.archive(twice.doc,"Lists"))^.doc==twice.doc
// HTML attributes must return to integer model levels so outdent can edit imported lists.
let html = files.html_document(twice.doc,"Lists")^
contains(html,"indent=\"2\"") and contains(surface.document_css,"margin-inline-start:3.5em")
let imported = files.import_html_document(html)^
attrs_get(node_at(imported,[0,1]).attrs,'indent')==2
let reopened = edit_set_selection(model.open_editor(imported),selected.selection)
attrs_get(node_at(model.execute(reopened,"para.outdent")^.doc,[0,1]).attrs,'indent')==1
let numbered = model.execute(model.open_editor(node('doc',[node('p',[text("Numbered")])])),"para.numbering")^
numbered.doc.content[0].tag=='ol'
let numbered_caret = edit_set_selection(numbered,text_selection(pos([0,0,0,0],3),pos([0,0,0,0],3)))
let numbered_indent = model.execute(model.execute(numbered_caret,"para.indent")^,"para.indent")^
attrs_get(node_at(numbered_indent.doc,[0,0]).attrs,'indent')==2
let end = edit_set_selection(twice,text_selection(pos([0,1,0,0],6),pos([0,1,0,0],6)))
let entered = edit_exec(end,edit_cmd_split_block())
attrs_get(node_at(entered.doc,[0,2]).attrs,'indent')==2
