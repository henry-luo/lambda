import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import transform: lambda.editor.raster.transform
import interaction: lambda.editor.raster.interaction
fn check(value) => value

let pixels = img.blank({width:8,height:4,color:[1,0,0,1]})^
let base = editor.exec(editor.open_session(editor.new_document({width:32,height:24})^)^,
    {id:"layer.insert",raster:pixels,position:[6,5]})^
let st = editor.exec(base,{id:"tool.configure",tool:"transform"})^
fn pointer(x,y,handle=null,shift=false,alt=false) => {point:{x:x,y:y},screen:{x:x,y:y},pointer:1,handle:handle,shift:shift,alt:alt}
let begin = interaction.begin(st,pointer(14,9,"se"))^
let draft = interaction.preview(begin,pointer(22,13))^
check(model.bounds(model.active(draft)^.node) == {x:6,y:5,width:16,height:8})
check(model.primary(model.active(draft)^.node) == pixels)
check(draft.hist == st.hist)
check(interaction.cancel(draft).doc == st.doc)
let done = interaction.finish(draft)^
check(len(done.hist.undo) == len(st.hist.undo)+1)
check(editor.exec(done,{id:"history.undo"})^.doc == st.doc)
check(editor.exec(editor.exec(done,{id:"history.undo"})^,{id:"history.redo"})^.doc == done.doc)
let edge = interaction.preview(interaction.begin(st,pointer(6,7,"w"))^,pointer(2,7))^
check(model.bounds(model.active(edge)^.node) == {x:2,y:5,width:12,height:4})
let constrained = interaction.preview(interaction.begin(st,pointer(14,9,"se"))^,pointer(22,10,null,true))^
check(model.bounds(model.active(constrained)^.node) == {x:6,y:5,width:16,height:8})
let centered = interaction.preview(interaction.begin(st,pointer(14,9,"se"))^,pointer(18,11,null,false,true))^
check(model.bounds(model.active(centered)^.node) == {x:2,y:3,width:16,height:8})
let rotated = editor.exec(st,{id:"layer.transform",angle:90})^
let bounds = model.bounds(model.active(rotated)^.node)
let resized = editor.exec(rotated,transform.handle_gesture(rotated,{x:bounds.x+bounds.width,y:bounds.y+bounds.height},
    {x:bounds.x+bounds.width*2,y:bounds.y+bounds.height*2},"se")^)^
let after = model.bounds(model.active(resized)^.node)
check(abs(after.x-bounds.x)<0.000001 and abs(after.y-bounds.y)<0.000001)
check(abs(after.width-bounds.width*2)<0.000001 and abs(after.height-bounds.height*2)<0.000001)
let rotate = editor.exec(st,transform.handle_gesture(st,{x:10,y:3},{x:14,y:7},"rotate",true)^)^
check(abs(model.active(rotate)^.node.transform[1]-1)<0.000001)
let mirrored = editor.exec(st,transform.handle_gesture(st,{x:14,y:9},{x:2,y:9},"e")^)^
check(model.active(mirrored)^.node.transform[0] == -0.5)
check(interaction.finish(begin)^.hist == st.hist)
check(transform.handle_gesture(st,{x:0,y:0},{x:1,y:1},"bogus") ^ { true } ~ { false })
let locked = editor.exec(st,{id:"layer.lock",locked:true})^
check(not transform.available(locked))
check(interaction.begin(locked,pointer(14,9,"se")) ^ { true } ~ { false })
check(not transform.available(editor.exec(st,{id:"layer.select",ids:[]})^))
