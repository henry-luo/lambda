import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import tone: lambda.editor.raster.tone
import project: lambda.image.project
fn check(value) => value
let ramp = img.from_array(reshape([for (x in 0 to 255) for (c in 0 to 3) u8(if (c == 3) 128 else x)],[1,256,4]))^
let points = [[0,0],[0.5,1],[1,0]]
let smooth = img.adjust(ramp,{curves:{rgb:points},curve_mode:"smooth"})^
let linear = img.adjust(ramp,{curves:{rgb:points}})^
check(all([for (x in 0 to 255) abs(smooth.r[0,x]-round(4*(x/255)*(1-x/255)*255))<=1]))
check(all([for (x in 0 to 255) smooth.a[0,x] == 128]))
check(smooth.r[0,64]>linear.r[0,64])
check(img.to_bytes(img.adjust(ramp,{curves:{rgb:tone.identity},curve_mode:"smooth"})^)^ == img.to_bytes(ramp)^)
check(img.adjust(ramp,{curve_mode:"spline"}) ^ { true } ~ { false })
check(img.adjust(ramp,{curve_mode:1}) ^ { true } ~ { false })
let graph = tone.graph_points({r:points},"r","smooth")^
check(len(graph) == 256 and graph[64][1] == smooth.r[0,64]/255)
check(tone.graph_points({rgb:points},"rgb")^ == points)
let st = editor.open_session(img.of_raster(ramp))^
let edited = editor.exec(st,{id:"effect.add",name:"Smooth",options:{curves:{rgb:points},curve_mode:"smooth"}})^
check(model.primary(model.active(edited)^.node) == ramp and len(edited.hist.undo) == 1)
check(img.to_bytes(img.render_frame(edited.doc)^)^ == img.to_bytes(smooth)^)
check(img.to_bytes(img.render_frame(project.decode(project.encode(edited.doc)^)^)^)^ == img.to_bytes(smooth)^)
check(editor.exec(edited,{id:"history.undo"})^.doc == st.doc)
let layer = editor.exec(st,{id:"adjustment.insert",options:{curves:{rgb:points},curve_mode:"smooth"}})^
check(img.to_bytes(img.render_frame(layer.doc)^)^ == img.to_bytes(smooth)^)
