import model: ~~.mod_model
import graphics: ~~.mod_graphics
import icons: ~~.mod_icon
import smartart: ~~.mod_smartart
import charts: ~~.mod_chart
import transform: ~~.mod_transform
import focused: ~~.mod_context
import files: ~~.mod_io
import controller: lambda.edit.controller
import outcome: lambda.dom.edit_result
import image: lambda.editor.mod_image
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos
import lambda.editor.mod_step

let empty = model.open_editor(node('doc',[node('p',[text("Keep this text")])]))
let entries = [for (entry in graphics.catalog where contains(graphics.illustration_kinds,entry.kind)) entry]
let inserted = [for (entry in entries) model.execute(empty,entry.insert,entry.defaults)^]
all([for (i,e in inserted) entries[i].settings(image.selected(e).node)==entries[i].defaults])
all([for (e in inserted) starts_with(attrs_get(image.selected(e).node.attrs,'src'),"data:image/svg+xml;base64,")])
all([for (e in inserted) contains(model.plain_text(e.doc),"Keep this text")])
all([for (e in inserted) model.execute(e,"edit.undo")^.doc==empty.doc])
all([for (e in inserted) model.execute(model.execute(e,"edit.undo")^,"edit.redo")^.doc==e.doc])
all([for (i,e in inserted) focused.describe(e).kind==entries[i].kind])
all([for (i,e in inserted) focused.describe(e).actions[0].command==entries[i].edit])
let changed = [for (e in inserted) model.execute(e,"object.transform",{width:240,height:150,rotation:30})^]
all([for (i,e in changed) entries[i].settings(image.selected(e).node).width==240 and transform.rotation(image.selected(e).node)==30])
all([for (i,e in changed) model.execute(e,"edit.undo")^.doc==inserted[i].doc])
all([for (e in changed) files.restore(files.archive(e.doc,"Illustrations"))^.doc==e.doc])
all([for (i,e in changed) entries[i].settings(image.images(files.import_html_document(files.html_document(e.doc,"Illustrations")^)^)[0].node)==entries[i].settings(image.selected(e).node)])
fn copied(e) => controller.clipboard_result(e,"copy",outcome.model_applied(false,false,false,"",e.selection,e.model_revision))
let pasted = [for (e in changed) edit_exec(empty,edit_cmd_paste_html(copied(e).clipboard_html,""))]
all([for (i,e in pasted) entries[i].settings(image.images(e.doc)[0].node)==entries[i].settings(image.selected(changed[i]).node)])
all([for (i,e in changed) copied(e).clipboard_image==attrs_get(image.selected(e).node.attrs,'src')])
let replaced = [for (i,e in inserted) model.execute(e,entries[i].update,{*:entries[i].defaults,color:"#b43d47"})^]
all([for (i,e in replaced) len(image.images(e.doc))==1 and entries[i].settings(image.selected(e).node).color=="#b43d47"])
all([for (i,e in replaced) model.execute(e,"edit.undo")^.doc==inserted[i].doc])
all([for (entry in entries) (model.execute(empty,entry.update,entry.defaults) ^ { true } ~ { false })])
all([for (c in icons.catalog) name(icons.graphic({*:icons.defaults,name:c.id}))=='svg'])
all([for (layout in smartart.layouts) name(smartart.graphic({*:smartart.defaults,layout:layout.id}))=='svg'])
all([for (kind in charts.types) name(charts.graphic({*:charts.defaults,type:kind.id})^ )=='svg'])
let pie = format(charts.graphic({*:charts.defaults,type:"pie"})^,'xml')^
contains(pie,"marks arcs") and not contains(pie,"axis x-axis") and not contains(pie,"axis y-axis")
let line = format(charts.graphic({*:charts.defaults,type:"line",color:"#b43d47"})^,'xml')^
contains(line,"marks lines") and contains(line,"#b43d47")
charts.rows("A, -2\nB, 3.5\n")^==[{label:"A",value:-2.0},{label:"B",value:3.5}]
let emoji = model.execute(empty,"insert.emoji",{char:"👩‍🎨"})^
contains(model.plain_text(emoji.doc),"👩‍🎨Keep this text") and len(image.images(emoji.doc))==0
model.execute(emoji,"edit.undo")^.doc==empty.doc
model.execute(empty,"insert.icon",{*:icons.defaults,name:"missing"}) ^ { true } ~ { false }
model.execute(empty,"insert.icon",{*:icons.defaults,color:"red"}) ^ { true } ~ { false }
model.execute(empty,"insert.smartArt",{*:smartart.defaults,text:"Only one"}) ^ { true } ~ { false }
model.execute(empty,"insert.smartArt",{*:smartart.defaults,text:join([for (i in 1 to 9) string(i)],"\n")}) ^ { true } ~ { false }
model.execute(empty,"insert.chart",{*:charts.defaults,data:"A, nope"}) ^ { true } ~ { false }
model.execute(empty,"insert.chart",{*:charts.defaults,data:"A, 2\nA, 3"}) ^ { true } ~ { false }
model.execute(empty,"insert.chart",{*:charts.defaults,data:"A, 2, 3"}) ^ { true } ~ { false }
model.execute(empty,"insert.chart",{*:charts.defaults,type:"pie",data:"A, -2\nB, 4"}) ^ { true } ~ { false }
model.execute(empty,"insert.chart",{*:charts.defaults,type:"pie",data:"A, 0"}) ^ { true } ~ { false }
all([for (entry in entries) entry.settings(node_attrs('img',[{name:entry.key,value:"[]"}],[]))==null])
