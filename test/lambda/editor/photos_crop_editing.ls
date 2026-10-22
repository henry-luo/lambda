import editor: lambda.editor.raster
import crop: lambda.editor.raster.crop
import canvas: lambda.editor.raster.canvas
import interaction: lambda.editor.raster.interaction
import render: lambda.editor.raster.render
fn check(value) => value
let st = editor.open_session(editor.new_document({width:200,height:160})^)^
let before = {x:40,y:30,width:80,height:60}
let start = {x:0,y:0}
let end = {x:10,y:15}
let expected = [{x:50,y:45,width:70,height:45},{x:40,y:45,width:80,height:45},{x:40,y:45,width:90,height:45},
    {x:50,y:30,width:70,height:60},{x:40,y:30,width:90,height:60},
    {x:50,y:30,width:70,height:75},{x:40,y:30,width:80,height:75},{x:40,y:30,width:90,height:75}]
check(all([for (i,h in canvas.handles) crop.edit_rectangle(st.doc,before,start,end,h.id)^ == expected[i]]))
check(crop.edit_rectangle(st.doc,before,{x:38,y:28},{x:48,y:43},"nw")^ == expected[0])
check(crop.edit_rectangle(st.doc,before,start,{x:1000,y:1000},"nw")^ == {x:119,y:89,width:1,height:1})
check(crop.edit_rectangle(st.doc,before,start,{x:1000,y:1000},"se")^ == {x:40,y:30,width:160,height:130})
check(crop.edit_rectangle(st.doc,before,start,{x:-1000,y:-1000},"move")^ == {x:0,y:0,width:80,height:60})
check(crop.edit_rectangle(st.doc,before,start,{x:1000,y:1000},"move")^ == {x:120,y:100,width:80,height:60})
check(crop.edit_rectangle(st.doc,before,start,start,"move")^ == before)
let square = {x:40,y:30,width:60,height:60}
check(crop.edit_rectangle(st.doc,square,start,{x:20,y:-20},"ne","1:1")^ == {x:40,y:10,width:80,height:80})
check(crop.edit_rectangle(st.doc,square,start,{x:-20,y:-20},"nw","1:1")^ == {x:20,y:10,width:80,height:80})
check(crop.edit_rectangle(st.doc,square,start,{x:20,y:0},"e","1:1")^ == {x:40,y:20,width:80,height:80})
check(crop.edit_rectangle(st.doc,square,start,{x:0,y:-20},"n","1:1")^ == {x:30,y:10,width:80,height:80})
check(crop.edit_rectangle(st.doc,square,start,{x:1000,y:0},"e","1:1")^ == {x:40,y:0,width:120,height:120})
check(crop.edit_rectangle(st.doc,square,start,{x:-1000,y:0},"e","1:1")^.width == 1)
check(all([for (h in canvas.handles) for (dx in [-1000,-17,0,19,1000]) for (dy in [-1000,0,1000]) (
    let r = crop.edit_rectangle(st.doc,square,start,{x:dx,y:dy},h.id,"1:1")^,
    r.x>=0 and r.y>=0 and r.width>=1 and r.width == r.height and r.x+r.width<=200 and r.y+r.height<=160)]))
check(all([for (ratio in [[5,2],[2,5],[12,7],[1,100]]) for (h in canvas.handles) (
    let initial = crop.rectangle(st.doc,{x:40,y:30},{x:190,y:150},"custom",ratio)^,
    let r = crop.edit_rectangle(st.doc,initial,start,{x:30,y:20},h.id,"custom",ratio)^,
    r.x>=0 and r.y>=0 and r.x+r.width<=200 and r.y+r.height<=160 and abs(r.height-r.width*ratio[1]/ratio[0])<=0.5)]))
check(crop.rectangle(st.doc,start,{x:100,y:100},"custom",[5,2])^ == {x:0,y:0,width:100,height:40})
check(crop.rectangle(st.doc,{x:100,y:100},start,"custom",[2,5])^ == {x:60,y:0,width:40,height:100})
check(crop.rectangle(st.doc,start,{x:100,y:100},"custom",[0.5,0.2])^ == {x:0,y:0,width:100,height:40})
check(crop.edit_rectangle(st.doc,{x:40,y:30,width:80,height:32},start,{x:20,y:20},"se","custom",[5,2])^ == {x:40,y:30,width:100,height:40})
check(all([for (bad in [[],[1],[1,0],[-1,1],[true,1],[nan,1],[inf,1],[1e308,1e-308],"5:2"])
    editor.exec(st,{id:"tool.configure",crop_ratio:bad}) ^ { true } ~ { false }]))
check(crop.edit_rectangle(st.doc,before,start,end,"unknown") ^ { true } ~ { false })
check(crop.edit_rectangle(st.doc,{*:before,x:-1},start,end,"se") ^ { true } ~ { false })
check(crop.edit_rectangle(st.doc,before,start,{x:inf,y:1},"move") ^ { true } ~ { false })
let configured = editor.exec(st,{id:"tool.configure",tool:"crop",crop_aspect:"custom",crop_ratio:[5,2]})^
check(configured.crop_ratio == [5,2] and configured.hist == st.hist and not editor.dirty(configured))
fn pointer(x,y,handle=null,shift=false) => {point:{x:x,y:y},screen:{x:x,y:y},pointer:1,crop_handle:handle,shift:shift}
let drawn = interaction.finish(interaction.preview(interaction.begin(configured,pointer(40,30))^,pointer(120,100))^)^
check(drawn.crop == {x:40,y:30,width:80,height:32})
let resizing = interaction.preview(interaction.begin(drawn,pointer(120,62,"se"))^,pointer(140,82))^
check(resizing.crop == {x:40,y:30,width:100,height:40})
check(resizing.doc == st.doc and resizing.hist == st.hist and interaction.cancel(resizing).crop == drawn.crop)
check(interaction.preview(resizing,pointer(130,72))^.crop == {x:40,y:30,width:90,height:36})
let ready = interaction.finish(resizing)^
check(ready.crop == resizing.crop and ready.gesture == null and not editor.dirty(ready))
check(editor.exec(ready,{id:"tool.configure",crop_ratio:[3,2]})^.crop == null)
check(editor.exec(ready,{id:"tool.configure",brush:{size:20}})^.crop == ready.crop)
let moving = interaction.preview(interaction.begin(ready,pointer(70,50,"move"))^,pointer(85,65))^
check(moving.crop == {x:55,y:45,width:100,height:40} and moving.hist == st.hist)
check(interaction.cancel(moving).crop == ready.crop)
let applied = crop.apply_draft(interaction.finish(moving)^)^
check(applied.doc.width == 100 and applied.doc.height == 40 and len(applied.hist.undo) == 1)
check(editor.exec(applied,{id:"history.undo"})^.doc == st.doc)
let free = {*:editor.exec(st,{id:"tool.configure",tool:"crop"})^,crop:before}
let shifted = interaction.preview(interaction.begin(free,pointer(120,60,"e"))^,pointer(140,60,null,true))^
check(shifted.crop == {x:40,y:10,width:100,height:100})
check(interaction.cancel(shifted).crop == before)
let cached = {*:resizing,painted_doc:st.doc,painted_raster:render.frame({*:st,painted_doc:null})^.raster}
check(not render.frame(cached)^.changed)
