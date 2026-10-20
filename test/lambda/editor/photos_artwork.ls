import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import project: lambda.image.project
fn check(label,value) => value
let st = editor.open_session(editor.new_document({width:32,height:24})^)^
let rect = editor.exec(st,{id:"vector.insert",name:"Card",position:[3,4],options:{width:12,height:8,shape:"rectangle",fill:[1,0,0,1]}})^
let before = img.render_frame(rect.doc)^
check("vector-pixels",img.sample(before,4,5)^ == [255,0,0,255] and before.a[3,4] == 0)
let ellipse = editor.exec(rect,{id:"vector.update",options:{shape:"ellipse",fill:[0,0,1,1]}})^
check("editable-geometry",model.primary(model.active(ellipse)^.node).shape == "ellipse" and img.sample(img.render_frame(ellipse.doc)^,9,8)^ == [0,0,255,255])
check("undo",editor.exec(ellipse,{id:"history.undo"})^.doc == rect.doc)
check("snapshot",img.render_frame(rect.doc)^ == before)
let ramp = editor.exec(rect,{id:"vector.update",options:{gradient:{kind:"linear",start:[0,0],end:[12,0],color:[1,0,0,1],end_color:[0,0,1,1]}}})^
let ramp_pixels = img.render_frame(ramp.doc)^
check("gradient",ramp_pixels.r[6,4]>ramp_pixels.r[6,13] and ramp_pixels.b[6,13]>ramp_pixels.b[6,4])
let path = editor.exec(st,{id:"vector.insert",options:{width:16,height:16,shape:"path",fill:[0,1,0,1],path:[["M",0,0],["L",16,0],["L",0,16],["Z"]]}})^
let triangle = img.render_frame(path.doc)^
check("path",img.sample(triangle,2,2)^ == [0,255,0,255] and triangle.a[14,14] == 0)
let curve = editor.exec(path,{id:"vector.update",options:{path:[["M",1,12],["Q",8,0,15,12],["C",10,15,5,15,1,12],["Z"]]}})^
check("bezier",sum(img.render_frame(curve.doc)^.a)>0)
let transformed = editor.exec(path,{id:"layer.transform",angle:90})^
check("affine",model.primary(model.active(transformed)^.node) == model.primary(model.active(path)^.node) and img.render_frame(transformed.doc)^ != triangle)
let baked = editor.exec(transformed,{id:"layer.rasterize"})^
check("rasterize",name(model.primary(model.active(baked)^.node)) == 'raster' and img.render_frame(baked.doc)^ == img.render_frame(transformed.doc)^)
let mask = editor.exec(rect,{id:"mask.add",coverage:0.0})^
check("mask",sum(img.render_frame(mask.doc)^.a) == 0)
let selected = editor.exec(rect,{id:"selection.shape",options:{shape:"rectangle",start:[3,4],end:[8,10]}})^
let from_selection = editor.exec(selected,{id:"mask.from_selection"})^
check("artwork-selection-mask",img.render_frame(from_selection.doc)^.a[5,4] == 255 and img.render_frame(from_selection.doc)^.a[5,10] == 0)
check("artwork-mask-selection",editor.exec(from_selection,{id:"selection.from_mask"})^.selection == selected.selection)
let resized = editor.exec(rect,{id:"document.resize_image",width:64,height:48})^
check("resize",model.primary(model.active(resized)^.node).width == 12 and model.bounds(model.active(resized)^.node).width == 24)
let saved = project.decode(project.encode(ramp.doc)^)^
check("project",model.primary(editor.layers(saved)[0].node).gradient == model.primary(model.active(ramp)^.node).gradient and img.render_frame(saved)^ == ramp_pixels)
check("pixel-edit-refused",(editor.exec(rect,{id:"paint.fill"}) ^ { true }) is bool)
check("psd-refused",(img.encode(rect.doc,{format:'psd'}) ^ { true }) is bool)
check("invalid-shape",(editor.exec(rect,{id:"vector.update",options:{shape:"bad"}}) ^ { true }) is bool)
check("invalid-path",(editor.exec(path,{id:"vector.update",options:{path:[["L",1,2]]}}) ^ { true }) is bool)
check("unknown-option",(editor.exec(rect,{id:"vector.update",options:{bogus:2}}) ^ { true }) is bool)
