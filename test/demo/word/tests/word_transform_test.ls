import model: ~~.mod_model
import transform: ~~.mod_transform
import shapes: ~~.mod_shapes
import art: ~~.mod_wordart
import files: ~~.mod_io
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import lambda.editor.mod_editor

let empty = model.open_editor(node('doc',[node('p',[text("")])]))
let picture = model.execute(empty,"insert.picture",{src:"data:image/svg+xml;base64,PHN2Zy8+",alt:"A picture",width:160,height:100})^
let changed = model.execute(picture,"object.transform",{width:240,height:150,rotation:90})^
attrs_get(model.current_node(changed,'img').node.attrs,'width')==240
attrs_get(model.current_node(changed,'img').node.attrs,'height')==150
transform.rotation(model.current_node(changed,'img').node)==90
model.css_get(attrs_get(model.current_node(changed,'img').node.attrs,'style'),"transform")=="rotate(90deg)"
attrs_get(model.current_node(changed,'img').node.attrs,'src')==attrs_get(model.current_node(picture,'img').node.attrs,'src')
model.execute(changed,"edit.undo")^.doc==picture.doc
model.execute(model.execute(changed,"edit.undo")^,"edit.redo")^.doc==changed.doc
let shape = model.execute(empty,"insert.shape",{*:shapes.defaults,text:"Decision",kind:"diamond"})^
let shape_changed = model.execute(shape,"object.transform",{width:200,height:125,rotation:-45})^
shapes.settings(model.current_node(shape_changed,'img').node)=={*:shapes.defaults,text:"Decision",kind:"diamond",width:200,height:125,rotation:-45}
contains(attrs_get(model.current_node(shape_changed,'img').node.attrs,'style'),"rotate(-45deg)")
attrs_get(model.current_node(shape_changed,'img').node.attrs,'src')!=attrs_get(model.current_node(shape,'img').node.attrs,'src')
let lettering = model.execute(empty,"insert.wordArt",{*:art.defaults,width:180,height:80,size:36})^
let lettering_changed = model.execute(lettering,"object.transform",{width:225,height:100,rotation:30,scale_text:true})^
art.settings(model.current_node(lettering_changed,'img').node)=={*:art.defaults,width:225,height:100,size:45,rotation:30}
model.execute(lettering_changed,"edit.undo")^.doc==lettering.doc
files.restore(files.archive(lettering_changed.doc,"Art"))^.doc==lettering_changed.doc
let imported = files.import_html_document(files.html_document(lettering_changed.doc,"Art")^)^
art.settings(imported.content[0].content[0]).rotation==30
transform.rotation(imported.content[0].content[0])==30
model.execute(picture,"object.transform",{width:15,height:100,rotation:0}) ^ { true } ~ { false }
model.execute(picture,"object.transform",{width:160,height:100,rotation:181}) ^ { true } ~ { false }
model.execute(empty,"object.transform",{width:160,height:100,rotation:0}) ^ { true } ~ { false }
model.execute(picture,"object.transform",{width:160,height:100,rotation:0,path:[99]}) ^ { true } ~ { false }
let drag = {kind:"picture",handle:"se",x:100,y:100,cx:100,cy:100,width:160,height:100,zoom:2,rotation:90}
transform.gesture_value(drag,50,180)=={width:200,height:125,rotation:90}
transform.gesture_value({*:drag,handle:"rotate",x:100,y:0,rotation:0},200,100).rotation==90
transform.gesture_value({*:drag,handle:"rotate",x:100,y:0,rotation:0},150,0,true).rotation==30
transform.normalized(390)==30 and transform.normalized(-390)==-30
