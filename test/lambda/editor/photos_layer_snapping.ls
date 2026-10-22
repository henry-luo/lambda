import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import guides: lambda.editor.raster.guides
import interaction: lambda.editor.raster.interaction
fn check(value) => value
let red = img.blank({width:10,height:8,color:[1,0,0,1]})^
let blue = img.blank({width:12,height:10,color:[0,0,1,1]})^
let moving = img.layer(red,{id:"moving",position:[10,10]})
let target = img.layer(blue,{id:"target",position:[70,40]})
let doc = img.document(img.stack([moving,target]),{width:200,height:120})
let st = editor.open_session(doc)^
let enabled = editor.exec(st,{id:"guide.configure",snap:true})^
let box = model.selection_bounds(enabled)
let boxes = model.snap_boxes(enabled)
check(boxes == [{x:70,y:40,width:12,height:10,id:"target"}])
check(guides.snap_move(enabled,box,47,0,1,false,boxes) == {dx:50,dy:0,x:70,y:null})
check(guides.snap_move(enabled,box,47,0,2,false,boxes).dx == 50)
check(guides.snap_move(enabled,box,47,0,4,false,boxes).dx == 47)
check(guides.snap_move(enabled,box,47,0,1,true,boxes).dx == 47)
let off = editor.exec(enabled,{id:"guide.configure",layers:false})^
check(guides.snap_move(off,box,47,0,1,false,boxes).x == null and off.hist == enabled.hist and not editor.dirty(off))
check(editor.exec(st,{id:"guide.configure",layers:1}) ^ { true } ~ { false })
fn pointer(x,y,alt=false) => {point:{x:x,y:y},screen:{x:x,y:y},pointer:1,alt:alt}
let draft = interaction.preview(interaction.begin(enabled,pointer(12,12))^,pointer(59,12))^
check(model.selection_bounds(draft).x == 60 and draft.gesture.snapped.x == 70)
check(draft.gesture.snap_boxes == boxes and draft.hist == enabled.hist)
check(model.lookup(draft.doc,"target").node == model.lookup(enabled.doc,"target").node)
check(interaction.cancel(draft).doc == enabled.doc)
check(model.selection_bounds(interaction.preview(draft,pointer(60,12))^).x == 60)
check(len(interaction.finish(draft)^.hist.undo) == len(enabled.hist.undo)+1)
check(editor.exec(interaction.finish(draft)^,{id:"history.undo"})^.doc == enabled.doc)
check(model.selection_bounds(interaction.preview(interaction.begin(enabled,pointer(12,12))^,pointer(59,12,true))^).x == 57)
let hidden = editor.exec(editor.exec(editor.exec(enabled,{id:"layer.select",ids:["target"]})^,{id:"layer.visibility",visible:false})^,{id:"layer.select",ids:["moving"]})^
check(model.snap_boxes(hidden) == [])
let transparent = img.replace_at(doc,[0,1],img.with_properties(target,{opacity:0})^)^
check(model.snap_boxes(editor.open_session(transparent)^) == [])
let locked = img.replace_at(doc,[0,1],img.with_properties(target,{locked:true})^)^
check(model.snap_boxes(editor.open_session(locked)^) == boxes)
let grouped_doc = img.document(img.stack([img.stack([moving,target],{id:"group"})]),{width:200,height:120})
let grouped = editor.open_session(grouped_doc)^
check(model.snap_boxes(grouped) == [])
check(model.snap_boxes(editor.exec(grouped,{id:"layer.select",ids:["group","moving"]})^) == [])
check(model.snap_boxes(editor.exec(grouped,{id:"layer.select",ids:["moving"]})^) == boxes)
let hidden_group = img.replace_at(grouped_doc,[0,0],img.with_properties(grouped_doc[0][0],{visible:false})^)^
check(model.snap_boxes(editor.exec(editor.open_session(hidden_group)^,{id:"layer.select",ids:["moving"]})^) == [])
let far = img.layer(blue,{id:"hidden",position:[190,90],visible:false})
let other_group = img.stack([target,far],{id:"other"})
let scoped = editor.open_session(img.document(img.stack([moving,other_group]),{width:200,height:120}))^
check([for (b in model.snap_boxes(scoped) where b.id == "other") b] == [{x:70,y:40,width:12,height:10,id:"other"}])
let adjustment = img.adjustment_layer({},{id:"adjustment"})
let graded = editor.open_session(img.document(img.stack([moving,adjustment,target]),{width:200,height:120}))^
check(model.snap_boxes(graded) == boxes)
let transformed = img.replace_at(doc,[0,1],img.with_properties(target,{transform:[1,0,0,2,5,0]})^)^
check(model.snap_boxes(editor.open_session(transformed)^) == [{x:75,y:40,width:12,height:20,id:"target"}])
let with_guide = editor.exec(enabled,{id:"guide.add",axis:"x",position:64})^
check(guides.snap_move(with_guide,box,45,0,1,false,[{x:66,y:40,width:20,height:10}]).x == 64)
check(guides.snap_move(enabled,box,47,0,1,false,[{x:70.4,y:40,width:12,height:10}]).dx == 50)
check(interaction.finish(interaction.begin(enabled,pointer(12,12))^)^.hist == enabled.hist)
let both = editor.exec(enabled,{id:"layer.select",ids:["moving","target"]})^
check(model.snap_boxes(both) == [])
