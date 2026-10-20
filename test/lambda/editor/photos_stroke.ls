import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import gestures: lambda.editor.raster.interaction
import img: lambda.image
import rendering: lambda.editor.raster.render
fn check(label,value) => value
fn pointer(x,y,p=1.0) => {pointer:1,point:{x:x,y:y},screen:{x:x,y:y},shift:false,pressure:p}
let initial = editor.exec(editor.open_session(editor.new_document({width:520,height:40})^)^,{id:"layer.new"})^
let ready = editor.exec(initial,{id:"tool.configure",tool:"brush",brush:{size:9,spacing:0.31,flow:0.4,opacity:0.5}})^
let first = gestures.begin(ready,pointer(250.3,10.2,0.3))^
let second = gestures.preview(first,pointer(260.1,13.2,0.8))^
let final = gestures.preview(second,pointer(270.4,15.3,0.6))^
let expected = editor.exec(ready,{id:"paint.stroke",points:final.gesture.points})^
check("incremental-result",final.doc == expected.doc)
check("incremental-coverage",final.gesture.stroke.count == 3 and final.gesture.stroke.step.work>second.gesture.stroke.step.work)
check("preview-does-not-record",final.hist == ready.hist)
check("cancel-restores",gestures.cancel(final).doc == ready.doc)
let committed = gestures.finish(final)^
check("one-transaction",len(committed.hist.undo) == len(ready.hist.undo)+1 and committed.gesture == null)
check("undo-original",editor.exec(committed,{id:"history.undo"})^.doc == ready.doc)
check("redo-result",editor.exec(editor.exec(committed,{id:"history.undo"})^,{id:"history.redo"})^.doc == expected.doc)
let reconfigured = {*:second,brush:{*:second.brush,opacity:0.8}}
let changed = gestures.preview(reconfigured,pointer(270.4,15.3,0.6))^
check("setting-change-restarts",changed.doc == editor.exec({*:ready,brush:reconfigured.brush},{id:"paint.stroke",points:changed.gesture.points})^.doc)
let small = editor.exec(editor.open_session(editor.new_document({width:64,height:32})^)^,
    {id:"layer.insert",position:[25,10],raster:img.blank({width:2,height:2,color:[0.0,0.0,1.0,1.0]})^})^
let small_ready = editor.exec(small,{id:"tool.configure",tool:"brush",brush:{size:4,opacity:0.5}})^
let grown = gestures.preview(gestures.preview(gestures.begin(small_ready,pointer(26,11))^,pointer(35,14))^,pointer(45,16))^
check("expanding-source",grown.doc == editor.exec(small_ready,{id:"paint.stroke",points:grown.gesture.points})^.doc)
let masked = editor.exec(small_ready,{id:"mask.add"})^
let erase = editor.exec(masked,{id:"tool.configure",tool:"erase"})^
let erased = gestures.preview(gestures.begin(erase,pointer(26,11))^,pointer(35,14))^
check("expanding-mask",erased.doc == editor.exec(erase,{id:"paint.stroke",points:erased.gesture.points,options:{mode:"erase"}})^.doc)

fn cached(st) any^ => {*:st,painted_doc:st.doc,painted_raster:img.render_frame(st.doc)^}
let before_frame = cached(first)^
let region_draft = gestures.preview(before_frame,pointer(260.1,13.2,0.8))^
let region_frame = rendering.frame(region_draft)^
check("region-matches-full",region_frame.changed and region_frame.raster == img.render_frame(region_draft.doc)^)
check("region-cache-frozen",before_frame.painted_raster == img.render_frame(first.doc)^)
let next_frame = gestures.preview({*:region_draft,painted_doc:region_draft.doc,painted_raster:region_frame.raster},pointer(270.4,15.3,0.6))^
check("successive-regions",rendering.frame(next_frame)^.raster == img.render_frame(next_frame.doc)^)
let stale = {*:next_frame,painted_doc:ready.doc,painted_raster:img.render_frame(ready.doc)^}
check("stale-cache-fallback",rendering.frame(stale)^.raster == img.render_frame(next_frame.doc)^)
let same = gestures.preview({*:region_draft,painted_doc:region_draft.doc,painted_raster:region_frame.raster},pointer(260.1,13.2,0.8))^
check("no-new-samples",not rendering.frame(same)^.changed)
let transparent_start = gestures.begin(cached({*:ready,brush:{*:ready.brush,opacity:0}})^,pointer(250,12))^
check("no-changed-pixels",not rendering.frame(transparent_start)^.changed and transparent_start.doc == ready.doc)
let layered_doc = img.document(img.stack([
    img.layer(img.blank({width:64,height:32,color:[0.2,0.8,0.1,0.5]})^,{id:"clip",name:"Clip",clipping:true,blend:'screen'}),
    img.layer(img.blank({width:64,height:32,color:[0.7,0.1,0.3,0.5]})^,{id:"paint",name:"Paint",blend:'multiply'}),
    img.layer(img.blank({width:64,height:32,color:[0.1,0.3,0.9,1.0]})^,{id:"back",name:"Back"})]),{width:64,height:32})
let layered = editor.exec(model.select(editor.open_session(layered_doc)^,["paint"])^,{id:"tool.configure",tool:"brush"})^
let layered_start = cached(gestures.begin(layered,pointer(10,12))^)^
let layered_draft = gestures.preview(layered_start,pointer(30,14))^
check("clipping-blend-region",rendering.frame(layered_draft)^.raster == img.render_frame(layered_draft.doc)^)
let blurred = editor.exec(layered,{id:"effect.add",options:{softness:2}})^
let blurred_start = cached(gestures.begin(blurred,pointer(10,12))^)^
let blurred_draft = gestures.preview(blurred_start,pointer(30,14))^
check("neighborhood-fallback",rendering.frame(blurred_draft)^.raster == img.render_frame(blurred_draft.doc)^)
let shadowed = editor.exec(ready,{id:"style.add",options:{kind:"shadow",color:[0,0,0,1],radius:3,dx:8,dy:2}})^
let shadow_start = cached(gestures.begin(shadowed,pointer(250,12))^)^
let shadow_draft = gestures.preview(shadow_start,pointer(265,14))^
check("style-fallback",rendering.frame(shadow_draft)^.raster == img.render_frame(shadow_draft.doc)^)
