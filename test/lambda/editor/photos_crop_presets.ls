import img: lambda.image
import editor: lambda.editor.raster
import crop: lambda.editor.raster.crop
import canvas: lambda.editor.raster.canvas
import interaction: lambda.editor.raster.interaction
import workspaces: lambda.editor.raster.workspace
fn check(value) => value
let st = editor.open_session(editor.new_document({width:101,height:79})^)^
check(crop.rectangle(st.doc,{x:10,y:10},{x:50,y:40},"1:1")^ == {x:10,y:10,width:30,height:30})
check(crop.rectangle(st.doc,{x:50,y:40},{x:10,y:10},"1:1")^ == {x:20,y:10,width:30,height:30})
check(crop.rectangle(st.doc,{x:50,y:10},{x:10,y:40},"1:1")^ == {x:20,y:10,width:30,height:30})
check(crop.rectangle(st.doc,{x:10,y:40},{x:50,y:10},"1:1")^ == {x:10,y:10,width:30,height:30})
check(crop.rectangle(st.doc,{x:10,y:10},{x:70,y:60},"3:2")^ == {x:10,y:10,width:60,height:40})
check(crop.rectangle(st.doc,{x:10,y:10},{x:70,y:60},"2:3")^ == {x:10,y:10,width:33,height:50})
check(crop.rectangle(st.doc,{x:95,y:75},{x:150,y:150},"1:1")^ == {x:95,y:75,width:4,height:4})
check(crop.rectangle(st.doc,{x:5,y:4},{x:-20,y:-30},"1:1")^ == {x:1,y:0,width:4,height:4})
check(crop.rectangle(st.doc,{x:-10,y:-10},{x:40,y:40},"1:1")^ == {x:0,y:0,width:40,height:40})
check(crop.rectangle(st.doc,{x:10,y:10},{x:10,y:30},"1:1")^ == null)
check(crop.rectangle(st.doc,{x:10.2,y:10.2},{x:10.3,y:10.3},"16:9")^ == null)
check(crop.rectangle(st.doc,{x:0,y:0},{x:20,y:20},"unknown") ^ { true } ~ { false })
check(crop.rectangle(st.doc,{x:0,y:nan},{x:20,y:20},"original") ^ { true } ~ { false })
check(all([for (preset in canvas.crop_presets where preset.id != "free") for (sx in [-1,1]) for (sy in [-1,1]) (
    let rect = crop.rectangle(st.doc,{x:50.2,y:40.4},{x:50+sx*100,y:40+sy*100},preset.id)^,
    let ratio = canvas.crop_ratio(st.doc,preset.id)^,
    rect != null and rect.x>=0 and rect.y>=0 and rect.x+rect.width<=101 and rect.y+rect.height<=79 and
        abs(rect.height-rect.width/ratio)<=0.5)]))
let configured = editor.exec(st,{id:"tool.configure",tool:"crop",crop_aspect:"3:2"})^
check(configured.crop_aspect == "3:2" and configured.hist == st.hist and not editor.dirty(configured))
check(editor.exec(st,{id:"tool.configure",crop_aspect:"bad"}) ^ { true } ~ { false })
fn pointer(x,y,shift=false) => {point:{x:x,y:y},screen:{x:x,y:y},pointer:1,shift:shift}
let draft = interaction.preview(interaction.begin(configured,pointer(10,10))^,pointer(70,60))^
check(draft.crop == {x:10,y:10,width:60,height:40} and draft.doc == st.doc and draft.hist == st.hist)
check(interaction.cancel(draft).crop == null and interaction.cancel(draft).crop_aspect == "3:2")
let ready = interaction.finish(draft)^
check(editor.exec(ready,{id:"tool.configure",crop_aspect:"1:1"})^.crop == null)
let applied = crop.apply_draft(ready)^
check(applied.doc.width == 60 and applied.doc.height == 40 and len(applied.hist.undo) == 1)
check(editor.exec(applied,{id:"history.undo"})^.doc == st.doc)
let square = editor.exec(st,{id:"tool.configure",tool:"crop"})^
check(interaction.preview(interaction.begin(square,pointer(10,10))^,pointer(70,60,true))^.crop == {x:10,y:10,width:50,height:50})
let workspace = workspaces.add(workspaces.add(workspaces.create(),configured)^,square)^
check(workspaces.activate(workspace,"document-1",square)^.session.crop_aspect == "3:2")
