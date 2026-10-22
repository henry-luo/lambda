import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import effects: lambda.editor.raster.effects
import project: lambda.image.project
import interaction: lambda.editor.raster.interaction
fn check(value) => value
fn pixels(st) => img.to_bytes(img.render_frame(st.doc)^)^
fn refused(st,command) => editor.exec(st,command) ^ { true } ~ { false }
let source = img.from_bytes(b'204080ff80a0c080',{width:2,height:1})^
let original = editor.open_session(img.of_raster(source))^
let base_id = original.selected[0]
let st = editor.exec(original,{id:"adjustment.insert",name:"Grade",options:{curves:{rgb:[[0,1],[1,0]]}}})^
let id = st.selected[0]
check(model.adjustment(model.active(st)^.node) and model.primary(model.active(st)^.node) == null)
check(model.primary(model.lookup(st.doc,base_id).node) == source and len(st.hist.undo) == 1)
check(pixels(st) == img.to_bytes(img.adjust(source,{curves:{rgb:[[0,1],[1,0]]}})^)^)
check(editor.exec(st,{id:"history.undo"})^.doc == original.doc)
check(editor.exec(editor.exec(st,{id:"history.undo"})^,{id:"history.redo"})^.doc == st.doc)
check(pixels(editor.exec(st,{id:"layer.visibility",visible:false})^) == pixels(original))
check(pixels(editor.exec(st,{id:"effect.toggle",index:0})^) == pixels(original))
let half = editor.exec(st,{id:"layer.opacity",opacity:0.5})^
check(img.sample(img.render_frame(half.doc)^,1,0)^[3] == 128)
check(img.sample(img.render_frame(half.doc)^,0,0)^[0]>32 and img.sample(img.render_frame(half.doc)^,0,0)^[0]<223)
let mask = editor.exec(st,{id:"layer.select",ids:[id],target:"mask"})^
let hidden = editor.exec(mask,{id:"pixels.fill",color:[0.0]})^
check(pixels(hidden) == pixels(original) and model.primary(model.lookup(hidden.doc,base_id).node) == source)
let shown = editor.exec(hidden,{id:"mask.invert"})^
check(pixels(shown) == pixels(st))
check(pixels(editor.exec(hidden,{id:"mask.toggle"})^) == pixels(st))
check(editor.exec(hidden,{id:"selection.from_mask"})^.selection.width == 2)
check(refused(mask,{id:"mask.apply"}) and refused(st,{id:"pixels.fill",color:[1,0,0,1]}))
let removed = editor.exec(st,{id:"mask.remove"})^
check(model.mask(model.active(removed)^.node) == null and pixels(removed) == pixels(st))
check(pixels(editor.exec(removed,{id:"mask.add",coverage:0.0})^) == pixels(original))
let selected = editor.exec(original,{id:"selection.shape",options:{shape:"rectangle",start:[0,0],end:[1,1]}})^
let limited = editor.exec(selected,{id:"adjustment.insert",options:{curves:{rgb:[[0,1],[1,0]]}}})^
let limited_pixels = img.render_frame(limited.doc)^
check(img.sample(limited_pixels,0,0)^[0] == 223 and img.sample(limited_pixels,1,0)^ == img.sample(source,1,0)^)
let selected_mask = editor.exec(editor.exec(st,{id:"selection.shape",options:{shape:"rectangle",start:[1,0],end:[2,1]}})^,{id:"mask.from_selection"})^
check(img.sample(img.render_frame(selected_mask.doc)^,0,0)^ == img.sample(source,0,0)^ and selected_mask.target == "mask")
let drag = interaction.begin(selected_mask,{pointer:1,point:{x:1,y:0},screen:{x:1,y:0}})^
let moved_mask = interaction.finish(interaction.preview(drag,{pointer:1,point:{x:0,y:0},screen:{x:0,y:0}})^)^
check(moved_mask.selected == [id] and moved_mask.target == "mask" and model.position(model.active(moved_mask)^.node) == [-1,0])
check(moved_mask.doc == editor.exec(selected_mask,{id:"layer.move",dx:-1,dy:0})^.doc)
let upper = editor.exec(st,{id:"layer.insert",raster:img.blank({width:1,height:1,color:[1,0,0,1]})^})^
check(img.sample(img.render_frame(upper.doc)^,0,0)^ == [255,0,0,255])
let lower = editor.exec(st,{id:"layer.reorder",index:1})^
check(pixels(lower) == pixels(original))
let reopened = project.decode(project.encode(st.doc)^)^
check(model.adjustment(model.lookup(reopened,id).node) and img.to_bytes(img.render_frame(reopened)^)^ == pixels(st))
check(img.encode(st.doc,{format:'psd'}) ^ { true } ~ { false })
check(refused(st,{id:"layer.transform",angle:15}) and refused(st,{id:"layer.rasterize"}))
check(refused(st,{id:"layer.blend",blend:"multiply"}) and refused(st,{id:"style.add",options:{kind:"color_overlay",color:[1,0,0,1]}}))
check(refused(st,{id:"effect.add",options:{softness:1}}) and refused(st,{id:"effect.add",options:{vignette:0.5}}))
check(refused(editor.exec(st,{id:"layer.visibility",visible:false})^,{id:"effect.add",options:{sharpen:1}}))
check(refused(st,{id:"layer.clipping",clipping:true}))
check(refused(editor.exec(st,{id:"layer.insert",raster:source})^,{id:"layer.clipping",clipping:true}))
check(refused(editor.exec(st,{id:"layer.lock",locked:true})^,{id:"effect.update",index:0,options:{exposure:1}}))
let cropped = editor.exec(st,{id:"document.crop",rect:[1,0,2,1]})^
check(img.sample(img.render_frame(cropped.doc)^,0,0)^ == img.sample(img.render_frame(st.doc)^,1,0)^)
let resized = editor.exec(st,{id:"document.resize_image",width:4,height:2})^
check(model.mask(model.active(resized)^.node)[0].width == 4 and img.render_frame(resized.doc)^.width == 4)
let both = editor.exec(st,{id:"layer.select",ids:[id,base_id]})^
check(refused(both,{id:"layer.merge"}))
let group = editor.exec(both,{id:"layer.group"})^
check(pixels(group) == pixels(st) and refused(group,{id:"layer.ungroup"}) and refused(group,{id:"layer.transform",angle:15}))
let flat = editor.exec(st,{id:"document.flatten"})^
check(len(editor.layers(flat.doc)) == 1 and pixels(flat) == pixels(st) and img.encode(flat.doc,{format:'psd'})^ is binary)
let action = editor.exec(editor.exec(editor.exec(st,{id:"action.start",name:"Grade"})^,{id:"effect.update",index:0,options:{gamma:2}})^,{id:"action.stop"})^.actions[0]
let replayed = editor.exec(st,{id:"action.replay",action:action})^
check(effects.items(model.active(replayed)^.node)[0].options.gamma == 2 and len(replayed.hist.undo) == len(st.hist.undo)+1)

// an isolated adjustment has an empty backdrop; pass-through sees its parent's lower siblings.
let adjustment = img.adjustment_layer({curves:{rgb:[[0,1],[1,0]]}})
let scoped = img.document(img.stack([img.stack([adjustment]),img.layer(source)]),{width:2,height:1})
let passed = img.replace_at(scoped,[0,0],img.with_properties(scoped[0][0],{isolation:'pass_through'})^)^
check(img.to_bytes(img.render_frame(scoped)^)^ == img.to_bytes(source)^)
check(img.to_bytes(img.render_frame(passed)^)^ == pixels(st))
check(img.render_region(st.doc,[1,0,2,1])^.pixels == img.read_region(img.render_frame(st.doc)^,[1,0,2,1])^)
