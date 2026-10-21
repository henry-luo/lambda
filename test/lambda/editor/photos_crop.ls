import editor: lambda.editor.raster
import img: lambda.image
import crop: lambda.editor.raster.crop
import interaction: lambda.editor.raster.interaction
import model: lambda.editor.raster.model

let raster = img.blank({width:8,height:6,color:[1,0,0,1]})^
let source = editor.open_session(img.of_raster(raster))^
let st = editor.exec(source,{id:"tool.configure",tool:"crop"})^
let input = {point:{x:1.2,y:1.5},screen:{x:12,y:15},pointer:1,shift:false}
let begin = interaction.begin(st,input)^
let draft = interaction.preview(begin,{point:{x:5.1,y:4.2},screen:{x:51,y:42}})^
draft.crop == {x:1,y:1,width:5,height:4}
draft.doc == source.doc and len(draft.hist.undo) == 0
not editor.dirty(draft)
interaction.cancel(draft).crop == null and interaction.cancel(draft).doc == source.doc
let ready = interaction.finish(draft)^
ready.gesture == null and ready.crop == draft.crop and len(ready.hist.undo) == 0
let applied = crop.apply_draft(ready)^
applied.doc.width == 5 and applied.doc.height == 4 and applied.crop == null
len(applied.hist.undo) == 1 and editor.dirty(applied)
model.position(model.active(applied)^.node) == [-1,-1]
img.sample(img.render_frame(applied.doc)^,4,3)^ == [255,0,0,255]
editor.exec(applied,{id:"history.undo"})^.doc == source.doc
editor.exec(editor.exec(applied,{id:"history.undo"})^,{id:"history.redo"})^.doc == applied.doc
crop.rectangle(st.doc,{x:20,y:10},{x:-3,y:-2})^ == {x:0,y:0,width:8,height:6}
crop.rectangle(st.doc,{x:2,y:2},{x:2,y:2})^ == null
crop.rectangle(st.doc,{x:-5,y:1},{x:-2,y:4})^ == null
crop.apply_draft(st) ^ { true } ~ { false }
let second = interaction.preview(interaction.begin(ready,input)^,{point:{x:3,y:3},screen:{x:30,y:30}})^
interaction.cancel(second).crop == ready.crop
editor.exec(ready,{id:"tool.configure",tool:"move"})^.crop == null
editor.exec(ready,{id:"document.resize_canvas",width:4,height:4})^.crop == null
editor.exec(ready,{id:"history.undo"})^.crop == null
crop.rectangle(st.doc,{x:null,y:0},{x:2,y:2}) ^ { true } ~ { false }
crop.rectangle(st.doc,{x:1.2,y:2.3},{x:1.2,y:2.3})^ == null
crop.rectangle(st.doc,{x:1.2,y:2.3},{x:1.2,y:4.8})^ == null
