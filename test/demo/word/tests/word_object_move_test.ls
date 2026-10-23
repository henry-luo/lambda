import model: ~~.mod_model
import files: ~~.mod_io
import shapes: ~~.mod_shapes
import art: ~~.mod_wordart
import transform: ~~.mod_transform
import image: lambda.editor.mod_image
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let empty = model.open_editor(node('doc',[node('p',[text("")])]))
let picture = model.execute(empty,"insert.picture",{src:"data:image/svg+xml;base64,PHN2Zy8+",alt:"Picture",width:160,height:100,rotation:30})^
let shape = model.execute(empty,"insert.shape",{*:shapes.defaults,kind:"diamond",rotation:45,text:"Move me"})^
let lettering = model.execute(empty,"insert.wordArt",{*:art.defaults,text:"Move the words",rotation: -30})^
let objects = [for (editor in [picture,shape,lettering]) image.selected(editor).node]
fn original(n) => image.select_image(model.open_editor(node('doc',[
    node('p',[text("Before"),n,text("After")]),node('p',[text_marked("Target words",[mark('strong',true)])]),
    node('p',[]),node('ul',[node('li',[node('p',[text("List")])])]),node('table',[node('tr',[node('td',[text("Cell")])])])])),[0,1])
let selected = original(objects[0])
let moved = model.execute(selected,"object.move",{path:[0,1],target:pos([1,0],3)})^
node_at(moved.doc,[1,1])==objects[0]
doc_text(node_at(moved.doc,[0]))=="BeforeAfter" and doc_text(node_at(moved.doc,[1]))=="Target words"
node_at(moved.doc,[1,0]).marks==node_at(moved.doc,[1,2]).marks
moved.selection==node_selection([1,1]) and len(moved.history.undo)==1
model.execute(moved,"edit.undo")^.doc==selected.doc
model.execute(model.execute(moved,"edit.undo")^,"edit.redo")^.doc==moved.doc
let backward = model.execute(moved,"object.move",{path:[1,1],target:pos([0,0],2)})^
node_at(backward.doc,[0,1])==objects[0]
files.restore(files.archive(backward.doc,"Moved"))^.doc==backward.doc
all([for (n in objects) node_at(model.execute(original(n),"object.move",{path:[0,1],target:pos([2],0)})^.doc,[2,0])==n])
all([for (n in objects) node_at(model.execute(original(n),"object.move",{path:[0,1],target:pos([3,0,0,0],2)})^.doc,[3,0,0,1])==n])
all([for (n in objects) node_at(model.execute(original(n),"object.move",{path:[0,1],target:pos([4,0,0,0],2)})^.doc,[4,0,0,1])==n])
model.execute(selected,"object.move",{path:[0,1],target:pos([0,1],0)})^==selected
model.execute(selected,"object.move",{path:[0,1],target:pos([],0)})^==selected
transform.resize_cursor("nw",0)=="nwse-resize" and transform.resize_cursor("ne",0)=="nesw-resize"
transform.resize_cursor("se",90)=="nesw-resize" and transform.resize_cursor("sw",90)=="nwse-resize"
transform.resize_cursor("nw",45)=="ns-resize" and transform.resize_cursor("nw", -45)=="ew-resize"
