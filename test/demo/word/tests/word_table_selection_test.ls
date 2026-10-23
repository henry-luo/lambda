import model: ~~.mod_model
import tables: ~~.mod_table
import files: ~~.mod_io
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

fn cell(s,header=false) => node(if (header) 'th' else 'td',[text(s)])
let doc = node('doc',[node('p',[text("Before")]),node('table',[
    node('tr',[cell("A",true),cell("B",true),cell("C",true)]),
    node('tr',[cell("D"),cell("E"),cell("F")]),node('tr',[cell("G"),cell("H"),cell("I")])]),node('p',[text("After")])])
let base = edit_set_selection(model.open_editor(doc),text_selection(pos([1,1,1,0],0),pos([1,1,1,0],0)))
let single = model.execute(base,"table.selectCell")^
let row = model.execute(base,"table.selectRow")^
let column = model.execute(base,"table.selectColumn")^
let table = model.execute(base,"table.selectTable")^
let region = edit_set_selection(base,tables.selection(base,"cell",[1,2,2],[1,0,1]));
single.selection==node_selection([1,1,1])
row.selection==node_selection([1,1]) and len(tables.selected(row))==3
column.selection==multi_node_selection([[1,0,1],[1,1,1],[1,2,1]])
table.selection==node_selection([1]) and len(tables.selected(table))==9
len(tables.selected(region))==6 and tables.selected(region)[0].path==[1,0,1]
single.doc==doc and len(single.history.undo)==0
tables.native_editor(column).selection==text_selection(pos([1,0,1],0),pos([1,0,1],0))
model.table_context(row).row_index==1 and model.table_context(column).col_index==1
let bordered = model.execute(single,"table.borders",{preset:"all",style:"dashed",width:2,color:"#2864ad"})^
fn css(editor,path,key) => model.css_get(attrs_get(node_at(editor.doc,path).attrs,'style'),key);
all([for (side in ["top","right","bottom","left"]) css(bordered,[1,1,1],"border-" ++ side)=="2pt dashed #2864ad"])
css(bordered,[1,1,0],"border-right")=="2pt dashed #2864ad" and css(bordered,[1,0,1],"border-bottom")=="2pt dashed #2864ad"
css(bordered,[1,1,0],"border-left")==null
doc_text(bordered.doc)==doc_text(doc)
bordered.selection==single.selection and len(bordered.history.undo)==1
model.execute(bordered,"edit.undo")^.doc==doc
model.execute(model.execute(bordered,"edit.undo")^,"edit.redo")^.doc==bordered.doc
let outside = model.execute(row,"table.borders",{preset:"outside",style:"double",width:3,color:"#b43d47"})^;
css(outside,[1,1,0],"border-left")=="3pt double #b43d47" and css(outside,[1,1,2],"border-right")=="3pt double #b43d47"
css(outside,[1,1,0],"border-right")==null and css(outside,[1,1,1],"border-left")==null
let inside = model.execute(row,"table.borders",{preset:"inside",style:"dotted",width:1,color:"#24334b"})^;
css(inside,[1,1,0],"border-right")=="1pt dotted #24334b" and css(inside,[1,1,0],"border-left")==null
let removed = model.execute(bordered,"table.borders",{preset:"none"})^;
starts_with(css(removed,[1,1,1],"border-top"),"0pt none") and starts_with(css(removed,[1,0,1],"border-bottom"),"0pt none")
css(model.execute(removed,"table.borders",{preset:"all"})^,[1,1,1],"border-top")=="0.75pt solid #2864ad"
files.restore(files.archive(outside.doc,"Borders"))^.doc==outside.doc
contains(files.html_document(outside.doc,"Borders")^,"border-left: 3pt double #b43d47")
let aligned = model.execute(column,"table.cellAlign",{value:"bottomRight"})^;
all([for (r in 0 to 2) css(aligned,[1,r,1],"text-align")=="right"])
css(aligned,[1,0,0],"text-align")==null
let bold = model.execute(column,"format.bold")^;
all([for (r in 0 to 2) mark_value(node_at(bold.doc,[1,r,1,0]).marks,'strong')==true])
model.execute(bold,"format.bold")^.doc==doc
let cleared = model.execute(region,"table.clear")^;
len(tables.cells(cleared.doc))==9 and len(node_at(cleared.doc,[1,0,1]).content)==0
doc_text(node_at(cleared.doc,[1,0,0]))=="A" and doc_text(node_at(cleared.doc,[2]))=="After"
model.execute(cleared,"edit.undo")^.doc==doc;
(model.execute(single,"table.borders",{width:0}) ^ { null })==null;
(model.execute(single,"table.borders",{style:"bad"}) ^ { null })==null;
(model.execute(single,"table.borders",{color:"bad"}) ^ { null })==null

let typed = edit_apply(row,tables.replace_transaction(row,"insertText","Typed"));
len(tables.cells(typed.doc))==9 and doc_text(node_at(typed.doc,[1,1,0]))=="Typed"
doc_text(node_at(typed.doc,[1,1,1]))=="" and doc_text(node_at(typed.doc,[1,0,0]))=="A"
model.execute(typed,"edit.undo")^.doc==doc
let plain = edit_apply(single,tables.replace_transaction(single,"insertFromPaste","Plain"));
doc_text(node_at(plain.doc,[1,1,1]))=="Plain"
let pasted = edit_apply(column,tables.replace_transaction(column,"insertFromPaste","","<table><tr><td>X</td></tr><tr><td>Y</td></tr><tr><td>Z</td></tr></table>"));
len(tables.cells(pasted.doc))==9 and doc_text(node_at(pasted.doc,[1,0,1]))=="X"
doc_text(node_at(pasted.doc,[1,1,1]))=="Y" and doc_text(node_at(pasted.doc,[1,2,1]))=="Z"
doc_text(node_at(pasted.doc,[1,1,0]))=="D" and pasted.selection==column.selection
let pretty = edit_apply(column,tables.replace_transaction(column,"insertFromPaste","",
    "<table>\n <tr>\n <th>X</th>\n </tr>\n <tr>\n <td>Y</td>\n </tr>\n <tr>\n <td>Z</td>\n </tr>\n</table>"));
doc_text(node_at(pretty.doc,[1,0,1]))=="X" and doc_text(node_at(pretty.doc,[1,1,1]))=="Y" and doc_text(node_at(pretty.doc,[1,2,1]))=="Z"
