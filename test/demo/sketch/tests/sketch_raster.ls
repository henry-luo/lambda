import m: ~~.mod_model
import raster: ~~.mod_raster
import scene: ~~.mod_scene
import image: lambda.image
import media: lambda.doc.media

let pixels = image.blank({width:160,height:100,channels:4,color:[1.0,0.0,0.0,0.5]})^
let source = raster.from_bytes(image.encode(pixels,{format:'png'})^,"Transparent.png")^
source.image_width==160 and source.image_height==100 and starts_with(source.src,"data:image/png;base64,")
let decoded = image.render_frame(image.decode(media.data_uri_bytes(source.src)^)^)^
let sample = image.sample(decoded,0,0)^
sample[0]==255 and sample[1]==0 and abs(sample[3]-128)<=1
let st = m.initial(m.blank())
let imported = m.add_image(st,source)
len(imported.doc.layers)==2 and imported.active=="l2" and imported.selected=="o3" and imported.doc.seq==4
len(imported.hist.undo)==1 and imported.tool=="select"
let o = m.chosen(imported)
o.kind=="image" and o.width==160 and o.height==100 and o.x==400 and o.y==350
m.hit(imported.doc,{x:410.0,y:360.0})==o.id
m.step_history(imported).doc==st.doc
m.step_history(m.step_history(imported),true).doc==imported.doc
let moved = m.nudge(imported,10.0,20.0)^
m.chosen(moved).x==410 and m.chosen(moved).y==370 and m.chosen(moved).src==source.src
let gesture = m.begin(imported,{x:560.0,y:450.0})
gesture.drag.mode=="resize"
let resized = m.finish(m.preview(gesture,{x:640.0,y:460.0}))
m.chosen(resized).width==240 and m.chosen(resized).height==150
let stretched = m.finish(m.preview(gesture,{x:640.0,y:460.0},true))
m.chosen(stretched).width==240 and m.chosen(stretched).height==110
m.cancel(m.preview(gesture,{x:640.0,y:460.0})).doc==imported.doc
m.hit(m.layer_action(imported,"visible",imported.active).doc,{x:410.0,y:360.0})==""
m.hit(m.layer_action(imported,"locked",imported.active).doc,{x:410.0,y:360.0})==""
let hidden = m.layer_action(imported,"visible",imported.active)
len(m.add_image(hidden,source).doc.layers)==3
let pasted = m.paste(m.copy_selection(imported))
len(pasted.doc.layers)==3 and m.chosen(pasted).x==420 and m.chosen(pasted).src==source.src
len(pasted.hist.undo)==2 and pasted.active=="l4" and pasted.selected=="o5"
m.step_history(pasted).doc==imported.doc
let here = m.paste(m.copy_selection(imported),{x:20.0,y:40.0})
m.chosen(here).x==20 and m.chosen(here).y==40
m.chosen(m.add_image(st,source,{x:25.0,y:35.0})).x==25
let large = m.add_image(st,{*:source,image_width:1600,image_height:1000})
m.chosen(large).width==768 and m.chosen(large).height==480
let faded = m.finish_edit(m.property(imported,"opacity",.4)^)
m.chosen(faded).opacity==.4 and m.chosen(faded).src==source.src
m.property(imported,"fill","#ffffff")^.doc==imported.doc
m.encode(imported.doc).version==2 and m.encode(st.doc).version==1
m.decode(parse(format(m.encode(imported.doc),'json'),'json')^)^==imported.doc
m.decode(m.encode(st.doc))^==st.doc
let wrong = {*:imported.doc,layers:[imported.doc.layers[0],{*:imported.doc.layers[1],objects:[{*:o,image_width:161}]}]}
m.decode(m.encode(wrong)) ^ { true } ~ { false }
let inconsistent = {*:pasted.doc,layers:[*imported.doc.layers,{*:pasted.doc.layers[last],objects:[{*:m.chosen(pasted),image_height:101}]}]}
m.decode(m.encode(inconsistent)) ^ { true } ~ { false }
let corrupt = {*:imported.doc,layers:[imported.doc.layers[0],{*:imported.doc.layers[1],objects:[{*:o,src:"data:image/png;base64,AAAA"}]}]}
m.decode(m.encode(corrupt)) ^ { true } ~ { false }
m.decode({format:"lambda-sketch",version:1,document:imported.doc}) ^ { true } ~ { false }
raster.from_bytes(b'\x00010203') ^ { true } ~ { false }
let animation = <image width:1,height:1,
    <frame duration:{ticks:1,rate:10},<stack <layer kind:'raster',image.blank({width:1,height:1,color:[1.0,0.0,0.0,1.0]})^>>>
    <frame duration:{ticks:1,rate:10},<stack <layer kind:'raster',image.blank({width:1,height:1,color:[0.0,0.0,1.0,1.0]})^>>>>
raster.from_bytes(image.encode(animation,{format:'gif'})^) ^ { true } ~ { false }
raster.from_uri("https://example.com/image.png") ^ { true } ~ { false }
let snapshot = {html:"<p>Image<img src=\"" ++ source.src ++ "\"></p>",text:"caption"}
raster.clipboard_source(snapshot)==source.src
raster.clipboard_source({html:"<img src='https://example.com/image.png'>",text:"caption",image:source.src})==source.src
raster.clipboard_source({html:null,text:source.src})==source.src
raster.clipboard_source({html:"<img src='https://example.com/remote.png'>",text:"caption"})==null
raster.from_uri(raster.clipboard_source(snapshot))^.src==source.src
let svg = format(scene.svg(imported.doc),'xml')
contains(svg,"<image ") and contains(svg,source.src) and not contains(svg,"selection-box")
raster.storage(m.objects(imported.doc))==len(source.src)
let full = {*:st,doc:{*:st.doc,layers:[for (i in 1 to 64) {*:st.doc.layers[0],id:"l" ++ string(i)}],seq:65}}
not m.can_add_image(full) and m.add_image(full,source).doc==full.doc
