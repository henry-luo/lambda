import img: lambda.image
import project: lambda.image.project
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import channels: lambda.editor.raster.channels
fn check(label,value) => value
let st = editor.open_session(img.document(img.stack([img.layer(img.blank({width:8,height:6,color:[1,0,0,0.5]})^,{id:"red"})]),{width:8,height:6}))^
let viewed = editor.exec(st,{id:"channel.view",channel:"a"})^
check("preview",img.sample(channels.preview(viewed,img.render_frame(st.doc)^)^,1,1)^ == [128,128,128,255])
check("view-is-transient",viewed.doc == st.doc and len(viewed.hist.undo) == 0 and not editor.dirty(viewed))
let selected = editor.exec(st,{id:"selection.shape",options:{shape:"rectangle",start:[1,1],end:[4,3]}})^
let saved = editor.exec(selected,{id:"channel.save",name:"Cutout"})^
check("save",len(channels.items(saved.doc)) == 1 and channels.items(saved.doc)[0][0] == selected.selection and editor.dirty(saved))
let captured = editor.exec(saved,{id:"channel.capture",channel:"a",name:"Alpha"})^
check("component",img.sample(channels.items(captured.doc)[1][0],0,0)^ == [128])
let load = editor.exec(editor.exec(saved,{id:"selection.clear"})^,{id:"channel.load",channel:"channel-1"})^
check("load",load.selection == selected.selection)
let renamed = editor.exec(saved,{id:"channel.rename",channel:"channel-1",name:"Mask"})^
check("rename",channels.items(renamed.doc)[0].name == "Mask")
let replaced = editor.exec(saved,{id:"channel.replace",channel:"channel-1",component:"a"})^
check("replace",img.raster_of(model.active(replaced)^.node).a[0,0] == 0 and img.raster_of(model.active(replaced)^.node).a[1,1] == 255)
check("old-channel",img.raster_of(model.active(st)^.node).a[0,0] == 128)
let resized = editor.exec(saved,{id:"document.resize_image",width:16,height:12})^
check("resize",channels.items(resized.doc)[0][0].width == 16 and channels.items(resized.doc)[0][0].height == 12)
let expanded = editor.exec(saved,{id:"document.resize_canvas",width:10,height:8})^
check("canvas",img.sample(channels.items(expanded.doc)[0][0],9,7)^ == [0] and img.sample(channels.items(expanded.doc)[0][0],1,1)^ == [255])
let cropped = editor.exec(saved,{id:"document.crop",rect:[1,1,5,4]})^
check("crop",channels.items(cropped.doc)[0][0].width == 4 and img.sample(channels.items(cropped.doc)[0][0],0,0)^ == [255])
let restored = project.decode(project.encode(saved.doc)^)^
check("project",channels.items(restored) == channels.items(saved.doc))
let deleted = editor.exec(editor.exec(saved,{id:"channel.view",channel:"channel-1"})^,{id:"channel.delete",channel:"channel-1"})^
check("delete-view",len(channels.items(deleted.doc)) == 0 and deleted.channel_view == "composite")
let undo = editor.exec(editor.exec(saved,{id:"channel.view",channel:"channel-1"})^,{id:"history.undo"})^
check("undo-view",len(channels.items(undo.doc)) == 0 and undo.channel_view == "composite")
check("invalid-component",(editor.exec(saved,{id:"channel.replace",channel:"channel-1",component:"bad"}) ^ { true }) is bool)
check("no-selection",(editor.exec(st,{id:"channel.save"}) ^ { true }) is bool)
check("unknown",(editor.exec(st,{id:"channel.view",channel:"missing"}) ^ { true }) is bool)
check("psd-refusal",(img.encode(saved.doc,{format:'psd'}) ^ { true }) is bool)
