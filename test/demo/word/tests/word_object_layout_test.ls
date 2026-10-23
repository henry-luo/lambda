import model: ~~.mod_model
import layout: ~~.mod_object_layout
import shapes: ~~.mod_shapes
import transform: ~~.mod_transform
import files: ~~.mod_io
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos
import image: lambda.editor.mod_image
import css: lambda.editor.mod_inline_format
import groups: ~~.mod_group_data
import controller: lambda.edit.controller
import outcome: lambda.dom.edit_result

let base = model.open_editor(node('doc',[node('p',[text("Keep the paragraph and all its text.")])]))
let shape = model.execute(base,"insert.shape",shapes.defaults)^
let path = image.selected(shape).path
let square = model.execute(shape,"object.layout",{values:{wrap:"square",side:"right",distance:12}})^
layout.settings(image.selected(square).node).wrap=="square"
css.css_get(attrs_get(image.selected(square).node.attrs,'style'),"float")=="right"
model.execute(square,"edit.undo")^.doc==shape.doc
model.execute(model.execute(square,"edit.undo")^,"edit.redo")^.doc==square.doc
files.restore(files.archive(square.doc,"Layout"))^.doc==square.doc
let frame = {x:20,y:40,left:0,top:-100,width:650,height:980,object_width:160,object_height:100}
let placed = model.execute(square,"object.position",{preset:"bottom-right",frame:frame})^
layout.settings(image.selected(placed).node).x==490 and layout.settings(image.selected(placed).node).y==780
layout.floating(image.selected(placed).node)
css.css_get(attrs_get(node_at(placed.doc,[0]).attrs,'style'),"position")=="relative"
contains(model.plain_text(placed.doc),"Keep the paragraph and all its text.")
let flipped = model.execute(placed,"object.flip",{axis:"x"})^
contains(attrs_get(image.selected(flipped).node.attrs,'style'),"scale(-1,1)")
let rotated = model.execute(flipped,"object.transform",{width:160,height:100,rotation:30})^
contains(attrs_get(image.selected(rotated).node.attrs,'style'),"rotate(30deg) scale(-1,1)")
let inline = model.execute(rotated,"object.layout",{values:{wrap:"inline"}})^
css.css_get(attrs_get(image.selected(inline).node.attrs,'style'),"left")==null
model.execute(shape,"object.layout",{values:{distance:-1}}) ^ {true} ~ {false}
model.execute(base,"object.position",{preset:"top-left",frame:frame}) ^ {true} ~ {false}
let source = image.selected(shape).node
let pair = model.open_editor(node('doc',[node('p',[source,text("First anchor stays")]),node('p',[source,text("Second anchor stays")])]))
let boxes = [{path:[0,0],x:20,y:30,width:160,height:100,object_width:160,object_height:100},
    {path:[1,0],x:200,y:130,width:160,height:100,object_width:160,object_height:100}]
let grouped = model.execute(pair,"object.group",{paths:[[1,0],[0,0]],boxes:boxes})^
len(image.images(grouped.doc))==1 and groups.settings(image.selected(grouped).node).width==340
groups.data(image.selected(grouped).node).items[1].x==260
contains(model.plain_text(grouped.doc),"First anchor stays") and contains(model.plain_text(grouped.doc),"Second anchor stays")
model.execute(grouped,"edit.undo")^.doc==pair.doc
model.execute(model.execute(grouped,"edit.undo")^,"edit.redo")^.doc==grouped.doc
files.restore(files.archive(grouped.doc,"Group"))^.doc==grouped.doc
let resized = model.execute(grouped,"object.transform",{width:510,height:300,rotation:90})^
let ungrouped = model.execute(resized,"object.ungroup")^
len(image.images(ungrouped.doc))==2
all([for (e in image.images(ungrouped.doc)) shapes.settings(e.node).width==240 and shapes.settings(e.node).height==150])
all([for (e in image.images(ungrouped.doc)) transform.rotation(e.node)==90])
model.execute(ungrouped,"edit.undo")^.doc==resized.doc
model.execute(grouped,"object.transform",{width:510,height:200,rotation:0}) ^ {true} ~ {false}
model.execute(pair,"object.group",{paths:[[0,0]],boxes:boxes}) ^ {true} ~ {false}
model.execute(shape,"object.ungroup") ^ {true} ~ {false}
let flipped_group = model.execute(grouped,"object.flip",{axis:"x"})^
let unflipped = model.execute(flipped_group,"object.ungroup")^
all([for (e in image.images(unflipped.doc)) layout.settings(e.node).flip_x])
let raised = model.execute(edit_set_selection(pair,node_selection([0,0])),"object.layer",{action:"front"})^;
(layout.settings(node_at(raised.doc,[0,0])).z>layout.settings(node_at(raised.doc,[1,0])).z)
let lowered = model.execute(raised,"object.layer",{action:"back"})^;
(layout.settings(node_at(lowered.doc,[0,0])).z<layout.settings(node_at(lowered.doc,[1,0])).z)
model.execute(lowered,"edit.undo")^.doc==raised.doc
let hidden = model.execute(placed,"object.layout",{values:{hidden:true}})^
css.css_get(attrs_get(image.selected(hidden).node.attrs,'style'),"visibility")=="hidden"
let behind = model.execute(placed,"object.layout",{values:{wrap:"behind"}})^
css.css_get(attrs_get(image.selected(behind).node.attrs,'style'),"z-index")=="-1"
all([for (mode in ["left","center","right","top","middle","bottom"])
    layout.floating(image.selected(model.execute(placed,"object.align",{mode:mode,frame:frame})^).node)])
let exported = files.import_html_document(files.html_document(grouped.doc,"Group")^)^
groups.data(image.images(exported)[0].node)==groups.data(image.selected(grouped).node)
let copied = controller.clipboard_result(grouped,"copy",outcome.model_applied(false,false,false,"",grouped.selection,grouped.model_revision))
let pasted = edit_exec(base,edit_cmd_paste_html(copied.clipboard_html,""))
groups.data(image.images(pasted.doc)[0].node)==groups.data(image.selected(grouped).node)
let edited = model.execute(flipped,"shape.set",{*:shapes.settings(image.selected(flipped).node),fill:"#3478cc"})^
layout.settings(image.selected(edited).node)==layout.settings(image.selected(flipped).node)
css.css_get(attrs_get(image.selected(edited).node.attrs,'style'),"position")=="absolute"
contains(attrs_get(image.selected(edited).node.attrs,'style'),"scale(-1,1)")
all([for (w in layout.wraps) layout.settings(image.selected(model.execute(placed,"object.layout",{values:{wrap:w.id}})^).node).wrap==w.id])
all([for (p in layout.positions) model.execute(shape,"object.position",{preset:p.id,frame:frame})^ is map])
let inline_group = model.execute(grouped,"object.layout",{values:{wrap:"inline"}})^
len(image.images(model.execute(inline_group,"object.ungroup",{frame:frame})^.doc))==2
let back_pair = model.execute(model.execute(edit_set_selection(pair,node_selection([0,0])),"object.layout",{values:{wrap:"behind",z:1}})^,
    "object.layout",{values:{x:20}})^
let both_back = model.execute(edit_set_selection(back_pair,node_selection([1,0])),"object.layout",{values:{wrap:"behind",z:2}})^
let back_front = model.execute(both_back,"object.layer",{action:"front"})^;
(layout.layer(layout.settings(node_at(back_front.doc,[1,0])))>layout.layer(layout.settings(node_at(back_front.doc,[0,0]))))
let copy_layout = controller.clipboard_result(placed,"copy",outcome.model_applied(false,false,false,"",placed.selection,placed.model_revision))
layout.settings(image.images(edit_exec(base,edit_cmd_paste_html(copy_layout.clipboard_html,"" )).doc)[0].node)==layout.settings(image.selected(placed).node)
model.execute(placed,"object.layer",{action:"bogus"}) ^ {true} ~ {false}
model.execute(placed,"object.flip",{axis:"bogus"}) ^ {true} ~ {false}
model.execute(shape,"object.position",{preset:"bogus",frame:frame}) ^ {true} ~ {false}
layout.settings(node_attrs('img',[{name:layout.key,value:"{\"wrap\":\"bogus\"}"}],[]))==layout.defaults
groups.settings(node_attrs('img',[{name:groups.key,value:"{\"items\":[{},{}]}"}],[]))==null
model.execute(grouped,"object.transform",{width:34,height:20,rotation:0}) ^ {true} ~ {false}
let smallest = transform.gesture_value({*:transform.settings(image.selected(grouped).node),handle:"se",x:0,y:0,zoom:1},-10000,-10000)
len(image.images(model.execute(model.execute(grouped,"object.transform",smallest)^,"object.ungroup")^.doc))==2
