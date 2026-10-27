import m: ~~.mod_model
import scene: ~~.mod_scene

let a = m.make_object("o2","rect",{x:100.0,y:100.0,width:80.0,height:60.0})
let b = m.make_object("o3","ellipse",{x:240.0,y:120.0,width:70.0,height:50.0})
let partial = m.make_object("o4","rect",{x:330.0,y:100.0,width:100.0,height:60.0})
let doc = {*:m.blank(),seq:20,layers:[
    {id:"l1",name:"Shapes",visible:true,locked:false,opacity:1.0,objects:[a,b,partial]},
    {id:"l5",name:"Hidden",visible:false,locked:false,opacity:1.0,objects:[{*:a,id:"o6"}]},
    {id:"l7",name:"Locked",visible:true,locked:true,opacity:1.0,objects:[{*:a,id:"o8"}]},
    {id:"l9",name:"Invisible",visible:true,locked:false,opacity:0.0,objects:[{*:a,id:"o10"}]},
    {id:"l11",name:"Ink",visible:true,locked:false,opacity:1.0,objects:[{*:a,id:"o12",x:150.0,y:200.0,width:60.0,height:40.0}]}
]}
let st = {*:m.initial(doc),tool:"select"}
let start = {x:80.0,y:80.0}
let end = {x:325.0,y:260.0}
let begin = m.begin(st,start)
begin.drag.mode == "marquee" and len(m.selected_ids(begin)) == 0
let preview = m.preview(begin,end)
m.selected_ids(preview) == ["o2","o3","o12"]
preview.doc == st.doc and preview.saved == st.saved and len(preview.hist.undo) == 0
contains(format(scene.overlay(preview),'xml'),"id=\"marquee\"")
let selected = m.finish(preview)
len(selected.hist.undo) == 0 and selected.drag == null
m.selection_box(selected) == {x:100.0,y:100.0,width:210.0,height:140.0}
m.chosen(selected) == null
not contains(format(scene.overlay(selected),'xml'),"resize-handle")
not contains(format(scene.svg(selected.doc),'xml'),"marquee")
m.selected_ids(m.finish(m.preview(m.begin(st,end),start))) == m.selected_ids(selected)
m.selected_ids(m.preview(m.begin(st,start),{x:260.0,y:160.0})) == ["o2"]
len(m.selected_ids(m.finish(m.preview(m.begin(st,start),{x:80.5,y:80.5})))) == 0
let previous = m.select(st,"o4")
let cancelled = m.cancel(m.preview(m.begin(previous,start),end))
m.selected_ids(cancelled) == ["o4"] and cancelled.active == previous.active and cancelled.doc == st.doc
let added = m.finish(m.preview(m.begin(selected,{x:320.0,y:80.0},1,true),{x:440.0,y:180.0}))
m.selected_ids(added) == ["o2","o3","o4","o12"] and len(added.hist.undo) == 0
let toggled = m.begin(selected,{x:120.0,y:120.0},1,true)
m.selected_ids(toggled) == ["o3","o12"] and toggled.drag == null
m.selected_ids(m.begin(toggled,{x:120.0,y:120.0},1,true)) == m.selected_ids(selected)
let forced = m.begin({*:selected,tool:"marquee"},{x:120.0,y:120.0})
forced.drag.mode == "marquee"
let move = m.preview(m.begin(selected,{x:120.0,y:120.0}),{x:140.0,y:150.0})
m.lookup(move.doc,"o2").object.x == 120 and m.lookup(move.doc,"o3").object.y == 150 and m.lookup(move.doc,"o12").object.x == 170
m.lookup(move.doc,"o4").object == partial and m.lookup(move.doc,"o6").object == m.lookup(st.doc,"o6").object
len(move.hist.undo) == 0 and m.selected_ids(move) == m.selected_ids(selected)
let moved = m.finish(move)
len(moved.hist.undo) == 1 and m.step_history(moved).doc == selected.doc
m.step_history(m.step_history(moved),true).doc == moved.doc
m.cancel(move).doc == selected.doc
let nudged = m.nudge(selected,5.0,-3.0)^
m.lookup(nudged.doc,"o2").object.x == 105 and m.lookup(nudged.doc,"o3").object.y == 117 and m.lookup(nudged.doc,"o12").object.x == 155
len(nudged.hist.undo) == 1
let copies = m.duplicate(selected)
len(m.objects(copies.doc)) == 10 and m.selected_ids(copies) == ["o20","o21","o22"]
m.lookup(copies.doc,"o20").layer.id == "l1" and m.lookup(copies.doc,"o22").layer.id == "l11"
m.lookup(copies.doc,"o20").object.x == 120 and m.lookup(copies.doc,"o22").object.y == 220
len(copies.hist.undo) == 1 and m.step_history(copies).doc == selected.doc
m.decode(m.encode(copies.doc))^ == copies.doc
let deleted = m.remove(selected)
len(m.objects(deleted.doc)) == 4 and len(m.selected_ids(deleted)) == 0
len(deleted.hist.undo) == 1 and m.step_history(deleted).doc == selected.doc
let styled = m.finish_edit(m.property(selected,"fill","#7665d9")^)
all([for (id in m.selected_ids(styled)) m.lookup(styled.doc,id).object.fill == "#7665d9"])
m.lookup(styled.doc,"o4").object.fill == partial.fill and len(styled.hist.undo) == 1
m.step_history(styled).doc == selected.doc
m.finish_edit(m.property(selected,"x",999)^).doc == selected.doc
m.selected_ids(m.select_ids(st,["o6","o8","o2"])) == ["o2"]
len(m.selected_ids(m.cancel(selected))) == 0
m.selected_ids(m.finish(m.preview(m.begin({*:st,grid:true},start),end))) == ["o2","o3","o12"]
