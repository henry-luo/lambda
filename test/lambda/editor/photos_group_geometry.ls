import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import transform: lambda.editor.raster.transform
import interaction: lambda.editor.raster.interaction
import project: lambda.image.project
fn check(value) => value
fn close(a,b) => abs(a-b)<0.000001
let red = img.blank({width:2,height:2,color:[1,0,0,1]})^
let blue = img.blank({width:2,height:2,color:[0,0,1,1]})^
let mask = img.mask(img.blank({width:2,height:2,channels:1,color:[0.5]})^,{position:[0,0],default:1})
let a = img.with_children(img.layer(red,{id:"a",position:[2,4]}),[red,mask])^
let b = img.layer(blue,{id:"b",position:[10,4]})
let image = img.document(img.stack([img.stack([a,img.stack([b],{id:"inner"})],{id:"group"})]),{width:32,height:24})
let st = editor.exec(editor.open_session(image)^,{id:"layer.select",ids:["group"]})^
let scaled = editor.exec(st,{id:"layer.transform",scale_x:2,scale_y:2})^
check(model.bounds(model.lookup(scaled.doc,"a").node) == {x:-3,y:3,width:4,height:4})
check(model.bounds(model.lookup(scaled.doc,"b").node) == {x:13,y:3,width:4,height:4})
check(model.primary(model.lookup(scaled.doc,"a").node) == red and model.mask(model.lookup(scaled.doc,"a").node) == mask)
check(model.lookup(scaled.doc,"group").node.transform == null and model.lookup(scaled.doc,"inner").node.transform == null)
check(len(scaled.hist.undo) == 1 and editor.exec(scaled,{id:"history.undo"})^.doc == st.doc)
check(editor.exec(editor.exec(scaled,{id:"history.undo"})^,{id:"history.redo"})^.doc == scaled.doc)
let parent_child = editor.exec(st,{id:"layer.select",ids:["group","a","inner","b"]})^
check(editor.exec(parent_child,{id:"layer.transform",scale_x:2,scale_y:2})^.doc == scaled.doc)
let leaves = editor.exec(st,{id:"layer.select",ids:["a","b"]})^
check(editor.exec(leaves,{id:"layer.transform",scale_x:2,scale_y:2})^.doc == scaled.doc)
check(model.selection_bounds(leaves) == {x:2,y:4,width:10,height:2})
let rotated = editor.exec(st,{id:"layer.transform",angle:90})^
let rb = model.bounds(model.lookup(rotated.doc,"group").node)
check(close(rb.x,6) and close(rb.y,0) and close(rb.width,2) and close(rb.height,10))
check(img.to_bytes(img.render_frame(editor.exec(scaled,{id:"layer.ungroup"})^.doc)^)^ == img.to_bytes(img.render_frame(scaled.doc)^)^)
check(img.to_bytes(img.render_frame(project.decode(project.encode(scaled.doc)^)^)^)^ == img.to_bytes(img.render_frame(scaled.doc)^)^)
check(editor.exec(st,{id:"layer.transform",matrix:transform.identity}) ^ { true } ~ { false })
check(editor.exec(st,{id:"layer.transform",delta:[1,0,0,1,3,2],angle:5}) ^ { true } ~ { false })
check(editor.exec(st,{id:"layer.transform",delta:[0,0,0,1,0,0]}) ^ { true } ~ { false })
let locked = editor.exec(editor.exec(st,{id:"layer.select",ids:["b"]})^,{id:"layer.lock",locked:true})^
let locked_group = editor.exec(locked,{id:"layer.select",ids:["group"]})^
check(not transform.available(locked_group))
check(editor.exec(locked_group,{id:"layer.transform",scale_x:2}) ^ { true } ~ { false })
let masked_group = img.with_children(model.lookup(st.doc,"group").node,[*content(model.lookup(st.doc,"group").node),mask])^
let masked = editor.open_session(model.replace(st.doc,"group",masked_group)^)^
check(not transform.available(masked))
check(editor.exec(masked,{id:"layer.transform",angle:10}) ^ { true } ~ { false })
let filtered = editor.exec(st,{id:"effect.add",options:{gamma:1.1}})^
check(not transform.available(filtered))
let tool = editor.exec(st,{id:"tool.configure",tool:"transform"})^
fn pointer(x,y,handle=null) => {point:{x:x,y:y},screen:{x:x,y:y},pointer:1,handle:handle}
let draft = interaction.preview(interaction.begin(tool,pointer(12,6,"se"))^,pointer(22,8))^
check(model.bounds(model.lookup(draft.doc,"group").node) == {x:2,y:4,width:20,height:4})
check(interaction.cancel(draft).doc == st.doc and len(interaction.finish(draft)^.hist.undo) == 1)
let move = interaction.preview(interaction.begin(st,pointer(2.5,4.5))^,pointer(5.5,6.5))^
check(move.selected == ["group"] and model.bounds(model.lookup(move.doc,"group").node).x == 5)
let straight = editor.exec(st,{id:"layer.straighten",start:{x:0,y:0},end:{x:10,y:10}})^
let m = model.lookup(straight.doc,"a").node.transform
check(close(m[0],math.sqrt(0.5)) and close(m[1],-math.sqrt(0.5)) and close(m[1]+m[3],0))
let vertical = editor.exec(st,{id:"layer.straighten",start:{x:0,y:0},end:{x:10,y:10},axis:"vertical"})^
let vm = model.lookup(vertical.doc,"a").node.transform
check(close(vm[0]+vm[2],0) and vm[1]+vm[3]>1)
check(editor.exec(st,{id:"layer.straighten",start:{x:10,y:10},end:{x:0,y:0}})^.doc == straight.doc)
check(editor.exec(st,{id:"layer.straighten",start:{x:2,y:2},end:{x:2,y:2}})^.hist == st.hist)
check(editor.exec(st,{id:"layer.straighten",start:{x:nan,y:0},end:{x:10,y:10}}) ^ { true } ~ { false })
let straight_tool = editor.exec(st,{id:"tool.configure",tool:"straighten"})^
let preview = interaction.preview(interaction.begin(straight_tool,pointer(0,0))^,pointer(10,10))^
check(preview.doc == straight.doc and preview.hist == st.hist)
check(interaction.cancel(preview).doc == st.doc and len(interaction.finish(preview)^.hist.undo) == 1)
let action = editor.exec(editor.exec(editor.exec(st,{id:"action.start",name:"Level horizon"})^,
    {id:"layer.straighten",start:{x:0,y:0},end:{x:10,y:10}})^,{id:"action.stop"})^.actions[0]
check(editor.exec(st,{id:"action.replay",action:action})^.doc == straight.doc)
