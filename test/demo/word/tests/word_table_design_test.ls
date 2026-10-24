import model: ~~.mod_model
import design: ~~.mod_table_design
import tables: ~~.mod_table
import files: ~~.mod_io
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos
import lambda.editor.mod_step

fn row(values,header=false) => node('tr',[for (value in values) node(if (header) 'th' else 'td',[text(value)])])
let table = node('table',[row(["A","B","C"],true),row(["D","E","F"]),row(["G","H","I"])])
let doc = node('doc',[node('p',[text("Before")]),table,table,node('p',[text("After")])])
let base = edit_set_selection(model.open_editor(doc),text_selection(pos([1,1,1,0],0),pos([1,1,1,0],1)))
let blue = model.execute(base,"table.style",{style:"blue"})^
design.settings(blue.doc.content[1]).style=="blue"
design.settings(blue.doc.content[1]).header and not design.settings(blue.doc.content[1]).total
blue.doc.content[2]==doc.content[2] and blue.selection==base.selection
len(blue.history.undo)==1 and model.execute(blue,"edit.undo")^.doc==doc
model.execute(model.execute(blue,"edit.undo")^,"edit.redo")^.doc==blue.doc
let banded = model.execute(blue,"table.designOption",{option:"bandedRows",value:true})^
design.settings(banded.doc.content[1]).bandedRows
model.execute(banded,"edit.undo")^.doc==blue.doc
let purple = model.execute(banded,"table.style",{style:"purple"})^
design.settings(purple.doc.content[1]).bandedRows and design.settings(purple.doc.content[1]).style=="purple"
all([for (s in design.styles) design.settings(model.execute(base,"table.style",{style:s.id})^.doc.content[1]).style==s.id])
all([for (entry in design.options) design.settings(model.execute(blue,"table.designOption",{option:entry.id,value:true})^.doc.content[1])[entry.id]==true])
let soft = model.execute(base,"table.style",{style:"soft"})^
design.settings(soft.doc.content[1]).bandedRows
let plain = model.execute(model.execute(soft,"table.designOption",{option:"bandedRows",value:false})^,"table.style",{style:"soft"})^
not design.settings(plain.doc.content[1]).bandedRows
let region = edit_set_selection(purple,tables.selection(purple,"row"))
let shaded = model.execute(region,"table.shading",{color:"#fef9c3"})^
all([for (cell in tables.selected(shaded)) design.shading_value(cell.node)=="#fef9c3"])
design.shading_value(node_at(shaded.doc,[1,0,0]))=="none" and shaded.selection==region.selection
model.execute(shaded,"edit.undo")^.doc==region.doc
let bordered = model.execute(shaded,"table.borders",{preset:"outside",style:"double",width:2,color:"#334155"})^
let cleared = model.execute(bordered,"table.shading",{color:"none"})^
all([for (cell in tables.selected(cleared)) design.shading_value(cell.node)=="none"])
contains(attrs_get(node_at(cleared.doc,[1,1,0]).attrs,'style'),"double #334155")
let styled = model.execute(bordered,"table.style",{style:"rose"})^
node_at(styled.doc,[1,1,1]).attrs==node_at(bordered.doc,[1,1,1]).attrs
files.restore(files.archive(styled.doc,"Design"))^.doc==styled.doc
let exported = files.html_document(styled.doc,"Design")^
contains(exported,"data-word-banded-rows=\"true\"") and contains(exported,"data-word-table-style=\"rose\"")
let imported = files.import_html_document(exported)^
design.settings(imported.content[1])==design.settings(styled.doc.content[1])
design.shading_value([for (cell in tables.cells(imported) where cell.path[0]==1 and doc_text(cell.node)=="E") cell.node][0])=="#fef9c3"
design.cell_paint({*:design.settings(blue.doc.content[1]),header:false},0,0,3,3).fill=="#ffffff"
design.cell_paint({*:design.settings(blue.doc.content[1]),total:true},2,1,3,3).weight==600
design.cell_paint({*:design.settings(blue.doc.content[1]),bandedColumns:true},1,1,3,3).fill==design.style("blue").band
all([for (s in design.styles) name(design.thumbnail({*:design.settings(blue.doc.content[1]),style:s.id}))=='svg'])
model.execute(base,"table.style",{style:"missing"}) ^ { true } ~ { false }
model.execute(base,"table.designOption",{option:"missing",value:true}) ^ { true } ~ { false }
model.execute(base,"table.designOption",{option:"header",value:"true"}) ^ { true } ~ { false }
model.execute(base,"table.shading",{color:"bad"}) ^ { true } ~ { false }
model.execute(model.open_editor(node('doc',[node('p',[text("Outside")])])),"table.style",{style:"blue"}) ^ { true } ~ { false }
