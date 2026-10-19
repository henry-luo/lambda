import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import gestures: lambda.editor.raster.interaction
fn check(label,value) => value
let gray = img.blank({width:12,height:8,color:[0.5,0.5,0.5,0.5]})^
let blue = img.blank({width:12,height:8,color:[0.0,0.0,1.0,1.0]})^
let stroke = {points:[[4.5,3.5]],size:3,hardness:1,flow:1,opacity:1,spacing:0.25}
let cloned = img.retouch(gray,{*:stroke,kind:"clone",sample:blue,sample_offset:[0,0]})^
check("clone",img.sample(cloned,4,3)^ == [0,0,255,255])
check("immutable",img.sample(gray,4,3)^ == [128,128,128,128] and img.sample(blue,4,3)^ == [0,0,255,255])
check("untouched",img.sample(cloned,10,7)^ == [128,128,128,128])
let healed = img.retouch(gray,{*:stroke,kind:"heal",sample:blue,sample_offset:[0,0],strength:1,radius:1})^
check("healing-color-match",img.sample(healed,4,3)^ == [128,128,128,255])
let dodged = img.retouch(gray,{*:stroke,kind:"dodge",strength:1})^
let burned = img.retouch(gray,{*:stroke,kind:"burn",strength:1})^
check("dodge",img.sample(dodged,4,3)^ == [176,176,176,128])
check("burn",img.sample(burned,4,3)^ == [92,92,92,128])
let sponge = img.retouch(blue,{*:stroke,kind:"sponge",strength:1})^
check("sponge",sponge.r[3,4] == sponge.g[3,4] and sponge.g[3,4] == sponge.b[3,4] and sponge.a[3,4] == 255)
let selected = img.selection(12,8,{shape:"rectangle",start:[4,3],end:[5,4]})^
let clipped = img.retouch(gray,{*:stroke,kind:"clone",sample:blue,sample_offset:[0,0],selection:selected})^
check("selection",clipped.b[3,4] == 255 and clipped.b[3,3] == 128)
let blurred = img.retouch(blue,{*:stroke,kind:"blur",strength:1,radius:2})^
check("neutral-blur",blurred == blue)
let sharp = img.retouch(blue,{*:stroke,kind:"sharpen",strength:1,radius:2})^
check("neutral-sharpen",sharp == blue)
check("invalid-kind",(img.retouch(gray,{*:stroke,kind:"unknown"}) ^ { true }) is bool)
check("missing-source",(img.retouch(gray,{*:stroke,kind:"clone"}) ^ { true }) is bool)
let image = img.document(img.stack([img.layer(blue,{id:"base",name:"Base",position:[0,0]})]),{width:12,height:8})
let st = editor.open_session(image)^
let source = editor.exec(st,{id:"retouch.source",point:[2,2]})^
check("source-session-only",source.doc == st.doc and source.hist == st.hist and not editor.dirty(source))
let layer = editor.exec(source,{id:"layer.new"})^
let configured = editor.exec(layer,{id:"tool.configure",tool:"clone",brush:{size:3,hardness:1,spacing:0.25}})^
let painted = editor.exec(configured,{id:"retouch.stroke",kind:"clone",points:[[6.5,4.5]]})^
check("new-layer-clone",img.raster_of(model.active(painted)^.node).b[4,6] == 255 and img.raster_of(model.active(painted)^.node).a[4,6] == 255)
check("aligned-offset",painted.retouch.anchor == [-4,-2])
let undone = editor.exec(painted,{id:"history.undo"})^
check("undo-target-source",undone.doc == configured.doc and undone.retouch.anchor == null and undone.retouch.source == source.retouch.source)
check("redo",editor.exec(undone,{id:"history.redo"})^.doc == painted.doc)
let unaligned = editor.exec(painted,{id:"retouch.configure",aligned:false})^
check("settings-session-only",unaligned.doc == painted.doc and unaligned.hist == painted.hist)
fn pointer(x,y,alt=false) => {pointer:4,point:{x:x,y:y},screen:{x:x,y:y},shift:false,alt:alt}
let draft = gestures.preview(gestures.begin(configured,pointer(6.5,4.5))^,pointer(9.5,4.5))^
check("cancel",gestures.cancel(draft).doc == configured.doc and gestures.cancel(draft).retouch.anchor == null)
check("commit",len(gestures.finish(draft)^.hist.undo) == len(configured.hist.undo)+1)
let sampled = gestures.begin(configured,pointer(3,3,true))^
check("alt-source",sampled.gesture == null and sampled.retouch.source.point == [3,3] and sampled.doc == configured.doc)
