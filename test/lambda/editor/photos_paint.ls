import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import gestures: lambda.editor.raster.interaction
import img: lambda.image
fn check(label,value) => value
fn point(x,y) => {pointer:1,point:{x:x,y:y},screen:{x:x,y:y},shift:false}
let base = editor.open_session(editor.new_document({width:520,height:32})^)^
let layer = editor.exec(base,{id:"layer.new"})^
let selected = editor.exec(layer,{id:"selection.shape",options:{shape:"rectangle",start:[250,4],end:[270,20]}})^
check("selection-content",selected.doc == layer.doc and len(selected.hist.undo) == 2)
check("selection-dirty",not editor.dirty(editor.mark_saved(selected,selected.doc)))
check("undo-selection",editor.exec(selected,{id:"history.undo"})^.selection == null)
let configured = editor.exec(selected,{id:"tool.configure",tool:"brush",brush:{size:8,hardness:1,spacing:0.25},foreground:"#ff0000"})^
check("settings",configured.tool == "brush" and configured.foreground == [1.0,0.0,0.0,1.0] and configured.hist == selected.hist)
let painted = editor.exec(configured,{id:"paint.stroke",points:[[245.0,10.0],[275.0,10.0]]})^
let pixels = model.pixel_target(painted)^.raster
check("paint-selection",pixels.a[10,255] == 255 and pixels.a[10,256] == 255 and pixels.a[10,245] == 0)
check("paint-undo",editor.exec(painted,{id:"history.undo"})^.doc == layer.doc)
check("target-restoration",editor.exec(painted,{id:"history.undo"})^.selection == selected.selection)
check("paint-redo",editor.exec(editor.exec(painted,{id:"history.undo"})^,{id:"history.redo"})^.doc == painted.doc)
let masked = editor.exec(painted,{id:"mask.from_selection"})^
check("mask-target",masked.target == "mask" and masked.selection == null and model.mask(model.active(masked)^.node) != null)
let cleared = editor.exec(masked,{id:"paint.stroke",points:[[255.0,10.0],[260.0,10.0]],options:{mode:"erase"}})^
check("mask-paint",img.render_frame(cleared.doc)^.a[10,256] == 0 and img.render_frame(masked.doc)^.a[10,256] == 255)
let mask_selection = editor.exec(masked,{id:"selection.from_mask"})^
check("mask-selection",img.coverage_bounds(mask_selection.selection)^ == [250,4,270,20])
let inverted = editor.exec(masked,{id:"mask.invert"})^
check("mask-invert",img.render_frame(inverted.doc)^.a[10,256] == 0)
let applied = editor.exec(masked,{id:"mask.apply"})^
check("mask-apply",model.mask(model.active(applied)^.node) == null and img.render_frame(applied.doc)^ == img.render_frame(masked.doc)^)
let copied = editor.exec(painted,{id:"clipboard.copy"})^
check("copy",copied.doc == painted.doc and copied.clipboard.position == [250,4] and copied.clipboard.raster.width == 20)
let pasted = editor.exec(copied,{id:"clipboard.paste"})^
check("paste",len(editor.layers(pasted.doc)) == 2 and model.position(model.active(pasted)^.node) == [250,4])
let cut = editor.exec(painted,{id:"clipboard.cut"})^
check("cut",img.render_frame(cut.doc)^.a[10,256] == 0 and cut.clipboard.raster.a[6,6] == 255)
check("cut-undo",editor.exec(cut,{id:"history.undo"})^.doc == painted.doc)
let crop = editor.exec(painted,{id:"document.crop"})^
check("crop",crop.doc.width == 20 and crop.doc.height == 16 and crop.selection == null and model.position(model.active(crop)^.node) == [-250,-4])
let draft = gestures.begin(configured,point(255,10))^
let moved = gestures.preview(draft,point(265,10))^
check("preview-history",moved.hist == configured.hist and moved.doc != configured.doc)
check("cancel-preview",gestures.cancel(moved).doc == configured.doc and gestures.cancel(moved).selection == configured.selection)
check("commit-preview",len(gestures.finish(moved)^.hist.undo) == len(configured.hist.undo)+1)
let small = editor.exec(base,{id:"layer.insert",raster:img.blank({width:2,height:2,color:[0.0,0.0,1.0,1.0]})^,position:[254,10]})^
let small_mask = editor.exec(small,{id:"mask.add"})^
let expanded = editor.exec(small_mask,{id:"paint.stroke",points:[[255.0,11.0],[260.0,11.0]],options:{size:4}})^
let expanded_node = model.active(expanded)^.node
check("expand-layer",img.raster_of(expanded_node).width>2 and model.position(expanded_node)[0]<254)
check("linked-mask-origin",model.position(expanded_node)[0]+model.position(model.mask(expanded_node))[0] == 254)
let saved = editor.open_session(img.decode(img.encode(masked.doc,{format:'psd'})^)^)^
check("painted-psd",img.render_frame(saved.doc)^ == img.render_frame(masked.doc)^)
check("invalid-setting",editor.exec(configured,{id:"tool.configure",brush:{hardnes:0.5}}) ^ { true } ~ { false })
check("locked-paint",editor.exec(editor.exec(painted,{id:"layer.lock"})^,{id:"paint.stroke",points:[[255,10]]}) ^ { true } ~ { false })
