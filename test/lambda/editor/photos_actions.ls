import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import gestures: lambda.editor.raster.interaction
import actions: lambda.editor.raster.actions
fn check(label,value) => value
let base = editor.open_session(editor.new_document({width:16,height:12})^)^
let first = editor.exec(base,{id:"layer.new",name:"First"})^
let painted = editor.exec(first,{id:"pixels.fill",color:[1.0,0.0,0.0,1.0]})^
let recording = editor.exec(painted,{id:"action.start",name:"Mono and fade"})^
let effect = editor.exec(recording,{id:"effect.add",options:{saturation:0.0},name:"Mono"})^
let faded = editor.exec(effect,{id:"layer.opacity",opacity:0.5})^
let stopped = editor.exec(faded,{id:"action.stop"})^
check("recording",stopped.recording == null and len(stopped.actions)==1 and len(stopped.actions[0].steps)==2)
check("record-not-image-change",recording.doc == painted.doc and recording.hist == painted.hist)
let target = editor.exec(stopped,{id:"layer.new",name:"Second"})^
let blue = editor.exec(target,{id:"pixels.fill",color:[0.0,0.0,1.0,1.0]})^
let replay = editor.exec(blue,{id:"action.replay",action:stopped.actions[0]})^
check("declared-target",model.active(replay)^.node.opacity == 0.5 and model.active(replay)^.node[1].options.saturation == 0)
check("replay-one-history",len(replay.hist.undo) == len(blue.hist.undo)+1)
check("undo-atomic",editor.exec(replay,{id:"history.undo"})^.doc == blue.doc)
check("other-layer",editor.lookup(replay.doc,painted.selected[0]).node == editor.lookup(stopped.doc,painted.selected[0]).node)
check("failure-atomic",(editor.exec(blue,{id:"action.replay",action:{version:1,target:"active-layer",name:"Invalid",steps:[
    {id:"layer.rename",name:"Changed"},{id:"layer.opacity",opacity:2}]}}) ^ { true }) is bool and model.active(blue)^.node.name == "Second")
check("unsupported-step",(actions.validate({version:1,target:"active-layer",name:"Invalid",steps:[{id:"layer.delete"}]}) ^ { true }) is bool)
check("data-only",(actions.validate({version:1,target:"active-layer",name:"Invalid",steps:[{id:"pixels.fill",color:<raster>}]}) ^ { true }) is bool)
check("record-target-guard",(editor.exec(recording,{id:"layer.select",ids:[]}) ^ { true }) is bool)
check("record-undo-guard",(editor.exec(recording,{id:"history.undo"}) ^ { true }) is bool)
let settings = editor.exec(painted,{id:"tool.configure",tool:"brush",foreground:"#0000ff",brush:{size:4,hardness:1,spacing:0.25}})^
let start = editor.exec(settings,{id:"action.start",name:"Brush blue"})^
fn pointer(x,y) => {pointer:1,point:{x:x,y:y},screen:{x:x,y:y},shift:false}
let stroke = gestures.finish(gestures.preview(gestures.begin(start,pointer(4.5,4.5))^,pointer(8.5,4.5))^ )^
check("gesture-record",len(stroke.recording.steps) == 1 and stroke.recording.steps[0].id == "paint.stroke")
let brush_action = editor.exec(stroke,{id:"action.stop"})^.actions[last]
check("resolved-brush",brush_action.steps[0].options.color == [0.0,0.0,1.0,1.0] and brush_action.steps[0].options.size == 4)
let differently_configured = editor.exec(blue,{id:"tool.configure",foreground:"#00ff00",brush:{size:1}})^
let brush_replay = editor.exec(differently_configured,{id:"action.replay",action:brush_action})^
check("brush-replay",img.sample(img.raster_of(model.active(brush_replay)^.node),4,4)^ == [0,0,255,255])
check("cancel",editor.exec(start,{id:"action.cancel"})^.recording == null)
check("action-data-roundtrip",actions.validate(parse(format(brush_action,'mark'),'mark')^)^ == brush_action)
