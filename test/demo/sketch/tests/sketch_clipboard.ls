import m: ~~.mod_model
import scene: ~~.mod_scene

let a = m.make_object("o2","rect",{x:100.0,y:100.0,width:80.0,height:60.0})
let b = m.make_object("o4","ellipse",{x:240.0,y:150.0,width:70.0,height:50.0})
let doc = {*:m.blank(),seq:5,layers:[
    {id:"l1",name:"Shapes",visible:true,locked:false,opacity:1.0,objects:[a]},
    {id:"l3",name:"Ink",visible:true,locked:false,opacity:1.0,objects:[b]}
]}
let st = m.select_ids(m.initial(doc),["o2","o4"])
let copied = m.copy_selection(st)
len(copied.clipboard) == 2 and copied.doc == st.doc and copied.hist == st.hist and copied.saved == st.saved
m.context_select(st,{x:120.0,y:120.0}).selection == st.selection
m.context_select(st,{x:500.0,y:500.0}).selection == []
let only = m.context_select(m.select(st,"o4"),{x:120.0,y:120.0})
only.selection == ["o2"] and only.active == "l1" and only.hist == st.hist
let pasted = m.paste(copied)
len(m.objects(pasted.doc)) == 4 and len(pasted.hist.undo) == 1
m.lookup(pasted.doc,"o5").object.x == 120 and m.lookup(pasted.doc,"o6").object.x == 260
m.lookup(pasted.doc,"o5").layer.id == "l3" and m.lookup(pasted.doc,"o6").layer.id == "l3"
m.selected_ids(pasted) == ["o5","o6"] and pasted.doc.seq == 7
m.step_history(pasted).doc == st.doc and len(m.step_history(pasted).clipboard) == 2
m.step_history(m.step_history(pasted),true).doc == pasted.doc
let again = m.paste(pasted)
m.lookup(again.doc,"o7").object.x == 140 and m.lookup(again.doc,"o8").object.y == 190
len(again.hist.undo) == 2 and again.paste_count == 2
let at_pointer = m.paste(copied,{x:400.0,y:300.0})
m.lookup(at_pointer.doc,"o5").object.x == 400 and m.lookup(at_pointer.doc,"o5").object.y == 300
m.lookup(at_pointer.doc,"o6").object.x == 540 and m.lookup(at_pointer.doc,"o6").object.y == 350
let cut = m.copy_selection(st,true)
len(m.objects(cut.doc)) == 0 and len(cut.hist.undo) == 1 and len(cut.clipboard) == 2
m.step_history(cut).doc == st.doc
let restored = m.paste(cut,{x:100.0,y:100.0})
len(m.objects(restored.doc)) == 2 and len(restored.hist.undo) == 2
m.decode(m.encode(restored.doc))^ == restored.doc
let cleared = m.select_ids(copied,[])
m.copy_selection(cleared).clipboard == copied.clipboard and m.copy_selection(cleared,true).doc == st.doc
m.paste(m.initial(m.blank()),{x:1.0,y:2.0}).doc == m.blank()
let locked = m.layer_action(copied,"locked",copied.active)
not m.can_paste(locked) and m.paste(locked).doc == locked.doc
let hidden = m.layer_action(copied,"visible",copied.active)
not m.can_paste(hidden) and m.paste(hidden,{x:2.0,y:3.0}).doc == hidden.doc
let locked_target = m.context_select(locked,{x:270.0,y:170.0})
len(m.selected_ids(locked_target)) == 0
not scene.context_enabled(st,"undo") and scene.context_enabled(pasted,"undo")
not scene.context_enabled(st,"paste") and scene.context_enabled(copied,"paste")
not scene.context_enabled(cleared,"delete") and scene.context_enabled(st,"delete")
let duplicates = m.duplicate(st)
m.lookup(duplicates.doc,"o5").layer.id == "l1" and m.lookup(duplicates.doc,"o6").layer.id == "l3"
