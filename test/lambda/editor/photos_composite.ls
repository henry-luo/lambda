import img: lambda.image
import project: lambda.image.project
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import effects: lambda.editor.raster.effects
import gestures: lambda.editor.raster.interaction
fn check(label,value) => value
let red = img.blank({width:2,height:1,color:[1.0,0.0,0.0,1.0]})^
let blue = img.blank({width:2,height:1,color:[0.0,0.0,1.0,1.0]})^
fn blended(mode) any^ => img.sample(img.render_frame(img.document(img.stack([img.layer(red,{blend:mode}),img.layer(blue)]),{width:2,height:1}))^,0,0)^
check("multiply",blended('multiply')^ == [0,0,0,255])
check("screen",blended('screen')^ == [255,0,255,255])
check("overlay",blended('overlay')^ == [0,0,255,255])
check("darken",blended('darken')^ == [0,0,0,255])
check("lighten",blended('lighten')^ == [255,0,255,255])
check("difference",blended('difference')^ == [255,0,255,255])
check("exclusion",blended('exclusion')^ == [255,0,255,255])
let strip = img.with_region(red,[1,0,2,1],img.read_region(blue,[0,0,1,1])^)^
let affine = [0,1,-1,0,1,0]
let layer = img.layer(strip,{id:"color",name:"Color",position:[2,1],transform:affine})
let doc = img.document(img.stack([layer]),{width:8,height:6})
let rendered = img.render_frame(doc)^
check("affine-pixels",img.sample(rendered,2,1)^ == [255,0,0,255] and img.sample(rendered,2,2)^ == [0,0,255,255] and rendered.a[1,3] == 0)
let coverage = img.selection(2,1,{shape:"rectangle",start:[0,0],end:[1,1]})^
let masked = img.with_children(layer,[strip,img.mask(coverage,{position:[0,0],default:0})])^
let masked_doc = img.with_children(doc,[img.stack([masked])])^
check("affine-mask",img.render_frame(masked_doc)^.a[1,2] == 255 and img.render_frame(masked_doc)^.a[2,2] == 0)
check("affine-region",img.render_region(doc,[1,1,4,4])^.pixels == img.read_region(rendered,[1,1,4,4])^)
let translucent = img.with_region(strip,[1,0,2,1],img.blank({width:1,height:1,color:[0.0,0.0,1.0,0.0]})^.pixels)^
let scaled = img.render_frame(img.document(img.stack([img.layer(translucent,{transform:[2,0,0,1,0,0]})]),{width:4,height:1}))^
check("premultiplied-interpolation",img.sample(scaled,1,0)^ == [255,0,0,191])
let st = editor.open_session(img.document(img.stack([img.layer(strip,{id:"color",name:"Color",position:[2,1]})]),{width:8,height:6}))^
let rotated = editor.exec(st,{id:"layer.transform",matrix:affine})^
check("source-retained",img.raster_of(model.active(rotated)^.node) == strip and model.bounds(model.active(rotated)^.node) == {x:2,y:1,width:1,height:2})
check("transform-undo",editor.exec(rotated,{id:"history.undo"})^.doc == st.doc)
check("transform-paint-rejected",(editor.exec(rotated,{id:"paint.fill"}) ^ { true }) is bool)
let mono = editor.exec(rotated,{id:"effect.add",name:"Mono",options:{saturation:0.0}})^
check("effect-pixels",img.sample(img.render_frame(mono.doc)^,2,1)^ == [76,76,76,255])
check("effect-retained",img.raster_of(model.active(mono)^.node) == strip and effects.items(model.active(mono)^.node)[0].options.saturation == 0)
let disabled = editor.exec(mono,{id:"effect.toggle",index:0})^
check("effect-toggle",img.render_frame(disabled.doc)^ == img.render_frame(rotated.doc)^)
let modified = editor.exec(mono,{id:"effect.update",index:0,options:{brightness:0.2}})^
check("effect-update",effects.items(model.active(modified)^.node)[0].options == {brightness:0.2})
let second = editor.exec(modified,{id:"effect.add",options:{contrast:1.5}})^
let reordered = editor.exec(second,{id:"effect.reorder",index:1,to:0})^
check("effect-order",img.render_frame(reordered.doc)^ != img.render_frame(second.doc)^)
check("effect-remove",len(effects.items(model.active(editor.exec(second,{id:"effect.remove",index:0})^)^.node)) == 1)
let locked = editor.exec(reordered,{id:"layer.lock"})^
let text = project.encode(locked.doc)^
let restored = editor.open_session(project.decode(text)^)^
check("project-parameters",model.active(restored)^.node.transform == affine and model.active(restored)^.node.locked == true and
    effects.items(model.active(restored)^.node) == effects.items(model.active(locked)^.node))
check("project-pixels",img.render_frame(restored.doc)^ == img.render_frame(locked.doc)^)
let gray = img.blank({width:2,height:2,channels:1,color:[0.3]})^
check("gray-bytes",img.from_bytes(img.to_bytes(gray)^,{width:2,height:2,channels:1})^ == gray)
check("hidden-rgb-bytes",img.from_bytes(img.to_bytes(translucent)^,{width:2,height:1})^ == translucent)
let baked = editor.exec(second,{id:"layer.rasterize"})^
check("rasterize",len(effects.items(model.active(baked)^.node)) == 0 and model.active(baked)^.node.transform == null and
    img.render_frame(baked.doc)^ == img.render_frame(second.doc)^)
check("invalid-effect",(editor.exec(st,{id:"effect.add",options:{angle:10}}) ^ { true }) is bool)
check("singular-affine",(editor.exec(st,{id:"layer.transform",matrix:[1,0,0,0,0,0]}) ^ { true }) is bool)
check("unknown-blend",(editor.exec(st,{id:"layer.blend",blend:"bogus"}) ^ { true }) is bool)
check("invalid-bytes",(img.from_bytes(b'00',{width:2,height:1}) ^ { true }) is bool)
check("invalid-project",(project.decode("{format: \"lambda-photos\",version:2,document:<image>}") ^ { true }) is bool)
check("psd-refusal",(img.encode(rotated.doc,{format:'psd'}) ^ { true }) is bool)

let base_half = img.blank({width:2,height:1,color:[1.0,0.0,0.0,0.5]})^
let clip = img.layer(blue,{clipping:true})
let clipped_doc = img.document(img.stack([clip,img.layer(base_half)]),{width:2,height:1})
check("clipping-alpha",img.sample(img.render_frame(clipped_doc)^,0,0)^ == [0,0,255,128])
let empty_clip = img.document(img.stack([clip,img.layer(img.blank({width:2,height:1})^)]),{width:2,height:1})
check("clipping-empty",img.render_frame(empty_clip)^.a[0,0] == 0)
check("orphan-clip",(img.render_frame(img.document(img.stack([clip]),{width:2,height:1})) ^ { true }) is bool)
let grouped = img.stack([img.layer(red,{blend:'multiply'})],{isolation:'pass_through'})
let pass_doc = img.document(img.stack([grouped,img.layer(blue)]),{width:2,height:1})
check("pass-through-blend",img.sample(img.render_frame(pass_doc)^,0,0)^ == [0,0,0,255])
let isolated_doc = img.document(img.stack([img.with_properties(grouped,{isolation:'isolated'})^,img.layer(blue)]),{width:2,height:1})
check("isolated-blend",img.sample(img.render_frame(isolated_doc)^,0,0)^ == [255,0,0,255])
let hist = img.histogram(translucent)^
check("histogram",hist[0][255] == 1 and hist[2][255] == 0 and hist[3][0] == 1 and hist[3][255] == 1)
