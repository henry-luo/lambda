import ruler: ~~.mod_ruler
import model: ~~.mod_model
import page: ~~.mod_page
import stories: ~~.mod_stories
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let base = model.open_editor(node('doc',[node('p',[text("First paragraph")]),node('p',[text("Second paragraph")])]))
let p = {indent:20,first:10,right:30}
ruler.indent_value(p,600,"indent",40)=={indent:50}
ruler.indent_value(p,600,"first",40)=={first:40}
ruler.indent_value(p,600,"right",40)=={right:0}
ruler.indent_value(p,600,"hanging",40)=={indent:50,first: -20}
ruler.indent_value(p,600,"indent",10000)=={indent:200}
ruler.indent_value(p,600,"first",-10000)=={first: -72}
ruler.indent_value(p,600,"right",-10000)=={right:200}
ruler.indent_value(p,600,"hanging",10000)=={indent:102,first: -72}
ruler.indent_value(p,100,"indent",10000)=={indent:20}
ruler.indent_value(p,600,"hanging",0)=={indent:20,first:10}
ruler.margin_value(base.doc,"left",96)=={left:44.4}
ruler.margin_value(base.doc,"right",96)=={right:0}
ruler.margin_value(base.doc,"top",96)=={top:42.4}
ruler.margin_value(base.doc,"bottom",-96)=={bottom:44.4}
ruler.margin_value(base.doc,"left",10000)=={left:80}
let narrow = model.execute(base,"layout.pageSetup",{paper:"A5",right:80})^
ruler.margin_value(narrow.doc,"left",10000)=={left:47.9}
let header = model.execute(base,"layout.headerFooter",{*:stories.defaults,header:"Studio"})^
ruler.margin_value(header.doc,"top",-10000)=={top:13.4}
model.points("24px")==18 and model.points("25.4mm")==72 and model.points("1in")==72
model.points("",11)==11 and model.points("normal")==0
let selected_range = text_selection(pos([0,0],2),pos([1,0],4))
let selected = edit_set_selection(base,selected_range)
let changed = model.execute(selected,"para.indents",{indent:24,first: -12,right:18})^
all([for (n in changed.doc.content) model.css_get(attrs_get(n.attrs,'style'),"margin-left")=="24pt"])
changed.selection==selected_range and len(changed.history.undo)==1
model.paragraph_indents(changed)=={indent:24,first: -12,right:18}
let undo = model.execute(changed,"edit.undo")^
undo.doc==base.doc and undo.selection==selected_range
model.execute(undo,"edit.redo")^.doc==changed.doc
let only_first = model.execute(changed,"para.indents",{first:20})^
model.paragraph_indents(only_first)=={indent:24,first:20,right:18};
(model.execute(base,"para.indents",{right: -1}) ^ {null})==null;
(model.execute(base,"para.indents",{first: -73}) ^ {null})==null
let cell_doc = node('doc',[node('table',[node('tr',[node('td',[text("Cell text")]),node('td',[node('p',[text("Wrapped")])])])])])
let cell = edit_set_selection(model.open_editor(cell_doc),text_selection(pos([0,0,0,0],2),pos([0,0,0,0],5)))
len(model.blocks(cell_doc))==2 and model.blocks(cell_doc)[1].path==[0,0,1,0]
let indented_cell = model.execute(cell,"para.indents",{indent:24,right:12})^
node_at(indented_cell.doc,[0,0,0,0]).tag=='p' and doc_text(indented_cell.doc)==doc_text(cell_doc)
indented_cell.selection==text_selection(pos([0,0,0,0,0],2),pos([0,0,0,0,0],5))
model.execute(indented_cell,"edit.undo")^.doc==cell_doc

stories.validate(model.execute(header,"layout.pageSetup",ruler.margin_value(header.doc,"top",-10000))^.doc,stories.settings(header.doc))^
let twice = model.execute(indented_cell,"para.indents",{indent:36})^
node_at(twice.doc,[0,0,0,0]).tag=='p' and node_at(twice.doc,[0,0,0,0,0]).kind=='text'

model.paragraph_indents(edit_set_selection(indented_cell,node_selection([0,0,0])))=={indent:24,first:0,right:12}

let cell_settings = {align:"left",lines:1.5,before:0,after:11,indent:24,first:0,right:12}
let dialog_cell = model.execute(cell,"para.set",cell_settings)^
node_at(dialog_cell.doc,[0,0,0,0]).tag=='p' and model.paragraph_settings(dialog_cell)==cell_settings
model.execute(dialog_cell,"edit.undo")^.doc==cell_doc
