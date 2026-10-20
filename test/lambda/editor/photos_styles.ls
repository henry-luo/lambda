import img: lambda.image
import project: lambda.image.project
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import styles: lambda.editor.raster.styles
fn check(label,value) => value
let st = editor.open_session(img.document(img.stack([img.layer(img.blank({width:2,height:2,color:[1,0,0,1]})^,
    {id:"red",position:[5,5]})]),{width:16,height:16}))^
let shadow = editor.exec(st,{id:"style.add",options:{kind:"shadow",color:[0,0,1,1],radius:0,dx:3,dy:2,opacity:1}})^
let pixels = img.render_frame(shadow.doc)^
check("shadow-pixels",img.sample(pixels,5,5)^ == [255,0,0,255] and img.sample(pixels,8,7)^ == [0,0,255,255])
check("bounds",model.bounds(model.active(shadow)^.node) == {x:2,y:2,width:8,height:8})
check("source-retained",img.raster_of(model.active(shadow)^.node) == img.raster_of(model.active(st)^.node))
let disabled = editor.exec(shadow,{id:"style.toggle",index:0})^
check("disabled",img.render_frame(disabled.doc)^ == img.render_frame(st.doc)^ and model.bounds(model.active(disabled)^.node).width == 2)
let glow = editor.exec(st,{id:"style.add",options:{kind:"glow",radius:1,color:[0,1,0,1]}})^
let glowing = img.render_frame(glow.doc)^
check("glow",glowing.g[4,5] == 255 and glowing.a[4,5]>0 and img.sample(glowing,5,5)^ == [255,0,0,255])
let overlay = editor.exec(st,{id:"style.add",options:{kind:"overlay",color:[0,0,1,1]}})^
check("overlay",img.sample(img.render_frame(overlay.doc)^,5,5)^ == [0,0,255,255])
let changed = editor.exec(shadow,{id:"style.update",index:0,options:{kind:"shadow",color:[0,1,0,1],dx:3,dy:2}})^
check("update",img.sample(img.render_frame(changed.doc)^,8,7)^ == [0,255,0,255])
let mask = editor.exec(shadow,{id:"mask.add",coverage:0})^
check("masked-before-style",sum(img.render_frame(mask.doc)^.a) == 0)
let partial = editor.exec(st,{id:"mask.add",coverage:0.4})^
let disabled_mask = editor.exec(editor.exec(partial,{id:"style.add",options:{kind:"overlay",color:[0,0,1,1]}})^,{id:"style.toggle",index:0})^
check("disabled-mask-exact",img.render_frame(disabled_mask.doc)^ == img.render_frame(partial.doc)^)
let applied_mask = editor.exec(editor.exec(partial,{id:"style.add",options:{kind:"shadow",color:[0,1,0,1],dx:3,dy:2}})^,{id:"mask.apply"})^
check("apply-mask-once",img.render_frame(applied_mask.doc)^ == img.render_frame(editor.exec(partial,{id:"style.add",options:{kind:"shadow",color:[0,1,0,1],dx:3,dy:2}})^.doc)^)
let moved = editor.exec(shadow,{id:"layer.transform",matrix:[0,1,-1,0,4,0]})^
check("affine-region",img.render_region(moved.doc,[0,0,12,12])^.pixels == img.read_region(img.render_frame(moved.doc)^,[0,0,12,12])^)
let baked = editor.exec(shadow,{id:"layer.rasterize"})^
check("rasterize-extent",img.render_frame(baked.doc)^ == pixels and len(styles.items(model.active(baked)^.node)) == 0)
let restored = project.decode(project.encode(disabled.doc)^)^
check("project",styles.items(editor.layers(restored)[0].node) == styles.items(model.active(disabled)^.node))
check("undo",editor.exec(shadow,{id:"history.undo"})^.doc == st.doc)
check("remove",editor.exec(shadow,{id:"style.remove",index:0})^.doc == st.doc)
check("psd-refused",(img.encode(disabled.doc,{format:'psd'}) ^ { true }) is bool)
check("invalid-style",(editor.exec(st,{id:"style.add",options:{kind:"bad",color:[1,0,0,1]}}) ^ { true }) is bool)
check("invalid-color",(editor.exec(st,{id:"style.add",options:{kind:"overlay",color:[1,0,0]}}) ^ { true }) is bool)
check("invalid-geometry",(editor.exec(st,{id:"style.add",options:{kind:"shadow",radius:200,color:[1,0,0,1]}}) ^ { true }) is bool)
check("unknown-option",(editor.exec(st,{id:"style.add",options:{kind:"shadow",color:[1,0,0,1],bogus:2}}) ^ { true }) is bool)
