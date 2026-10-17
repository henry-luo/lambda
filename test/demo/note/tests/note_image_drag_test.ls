import model: ~~.mod_model
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_step
import lambda.editor.mod_source_pos

let picture = node_attrs('img',[{name:'src',value:"picture.png"},{name:'alt',value:"Picture"},
    {name:'width',value:128},{name:'height',value:80}],[])
let bold = [{name:'bold',value:true}]
let doc = node('doc',[
    node('p',[text("Before"),picture,text("After")]),
    node('p',[text_marked("Target",bold)]),node('p',[]),
    node('ul',[node('li',[node('p',[text("List")])])])])
let original = model.select_image(model.open_editor(doc),[0,1])
let moved = model.move_image(original,[0,1],pos([1,0],3));
node_at(moved.doc,[1,1])==picture
doc_text(node_at(moved.doc,[0]))=="BeforeAfter"
doc_text(node_at(moved.doc,[1]))=="Target"
node_at(moved.doc,[1,0]).marks==bold and node_at(moved.doc,[1,2]).marks==bold
moved.selection==node_selection([1,1])
len(model.images(moved.doc))==1
len(moved.history.undo)==1
let undone = edit_exec(moved,edit_cmd_history_undo());
undone.doc==original.doc and undone.selection==original.selection
edit_exec(undone,edit_cmd_history_redo()).doc==moved.doc
let backward = model.move_image(moved,[1,1],pos([0,0],2));
node_at(backward.doc,[0,1])==picture and node_at(backward.doc,[0,0]).text=="Be"
doc_text(node_at(backward.doc,[1]))=="Target"
let within = model.move_image(original,[0,1],pos([0,2],2));
node_at(within.doc,[0,2])==picture and node_at(within.doc,[0,1]).text=="Af"
edit_exec(within,edit_cmd_history_undo()).doc==doc
let empty = model.move_image(original,[0,1],pos([2],0));
node_at(empty.doc,[2,0])==picture
empty.selection==node_selection([2,0])
let listed = model.move_image(original,[0,1],pos([3,0,0,0],2));
node_at(listed.doc,[3,0,0,1])==picture
doc_text(node_at(listed.doc,[3,0]))=="List"
// self-drops and invalid destinations neither mutate the document nor create history.
model.move_image(original,[0,1],pos([0],1))==original
model.move_image(original,[0,1],pos([0],2))==original
model.move_image(original,[0,1],pos([0,1],0))==original
model.move_image(original,[0,1],pos([],0))==original
model.move_image(original,[0,1],pos([1,0],-1))==original
model.move_image(original,[0,1],pos([1,0],100))==original
model.move_image(original,[0,0],pos([1,0],2))==original
model.move_image(original,[0,1],pos([0,0],6))==original
model.move_image(original,[0,1],pos([0,2],0))==original
