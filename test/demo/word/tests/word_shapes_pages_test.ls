import model: ~~.mod_model
import shapes: ~~.mod_shapes
import page: ~~.mod_page
import files: ~~.mod_io
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let doc = node('doc',[node('p',[text("BeforeAfter")])])
let editor = edit_set_selection(model.open_editor(doc),text_selection(pos([0,0],6),pos([0,0],6)))
let broken = model.execute(editor,"insert.pageBreak")^
len(page.sheets(broken.doc))==2
page.sheets(broken.doc)[0]==[broken.doc.content[0]]
page.sheets(broken.doc)[1]==[broken.doc.content[1]]
let typed = model.execute(broken,"text.insert",{data:"Second "})^
let entered = model.execute(typed,"text.enter")^
len(page.sheets(entered.doc))==2
len(page.sheets(entered.doc)[1])==2
model.plain_text(entered.doc)=="Before\nSecond \nAfter"
len(page.sheets(model.execute(broken,"edit.undo")^.doc))==1
len(page.sheets(model.execute(editor,"insert.blankPage")^.doc))==3
let table = model.execute(editor,"insert.table",{rows:1,cols:1,header:false})^
model.execute(table,"insert.pageBreak") ^ { true } ~ { false }

let inserted = model.execute(editor,"insert.shape",shapes.defaults)^
let img = model.current_node(inserted,'img')
img.node.tag=='img'
shapes.settings(img.node)==shapes.defaults
attrs_get(img.node.attrs,'width')==160
starts_with(attrs_get(img.node.attrs,'src'),"data:image/svg+xml;base64,")
model.plain_text(inserted.doc)=="BeforeAfter"
let changed_params = {*:shapes.defaults,kind:"diamond",width:240,height:120,fill:"#aabbcc",stroke:"#123456"}
let changed = model.execute(inserted,"shape.set",changed_params)^
let changed_img = model.current_node(changed,'img')
shapes.settings(changed_img.node)==changed_params
attrs_get(changed_img.node.attrs,'width')==240
attrs_get(changed_img.node.attrs,'src')!=attrs_get(img.node.attrs,'src')
model.execute(changed,"edit.undo")^.doc==inserted.doc
model.execute(model.execute(changed,"edit.undo")^,"edit.redo")^.doc==changed.doc
model.execute(inserted,"edit.undo")^.doc==doc
files.restore(files.archive(changed.doc,"Shapes"))^.doc==changed.doc
let imported = files.import_html_document(files.html_document(changed.doc,"Shapes")^)^
shapes.settings(imported.content[0].content[1])==changed_params
attrs_get(imported.content[0].content[1].attrs,'src')==attrs_get(changed_img.node.attrs,'src')
all([for (entry in shapes.catalog) model.current_node(model.execute(editor,"insert.shape",{*:shapes.defaults,kind:entry.id})^,'img')!=null])
model.execute(editor,"insert.shape",{*:shapes.defaults,kind:"missing"}) ^ { true } ~ { false }
model.execute(editor,"insert.shape",{*:shapes.defaults,width:0}) ^ { true } ~ { false }
model.execute(editor,"insert.shape",{*:shapes.defaults,height:2001}) ^ { true } ~ { false }
model.execute(editor,"insert.shape",{*:shapes.defaults,fill:"url(file.svg)"}) ^ { true } ~ { false }
model.execute(editor,"shape.set",shapes.defaults) ^ { true } ~ { false }
