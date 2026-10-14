import model: ~~.mod_model
let st = model.initial()
len(model.objects(st.doc)) == 9
model.hit(st.doc,{x:180,y:340}) == "n1"
model.hit(st.doc,{x:100,y:300}) == ""
model.hit(st.doc,{x:690,y:340}) == "n3"
model.hit(st.doc,{x:612,y:302}) == ""
model.hit(st.doc,{x:300,y:340}) == "e1"
let added = model.add(st,"rect",23,27)
model.box(model.lookup(added.doc,added.selected).shape) == {x:20,y:30,width:160,height:80}
len(added.hist.undo) == 1
model.step_history(added).doc == st.doc
model.step_history(model.step_history(added),true).doc == added.doc
let moved_doc = model.transform(st.doc,"n2","move",37,-24,true)
model.box(model.lookup(moved_doc,"n2").shape).x == 390
model.box(model.lookup(moved_doc,"n2").shape).y == 280
let moved = model.commit_edit({*:st,selected:"n2"},st.doc,moved_doc)
len(moved.hist.undo) == 1
model.step_history(moved).doc == st.doc
model.route(moved.doc,model.lookup(moved.doc,"e1").shape)[last].x == 390
let shrunk = model.transform(st.doc,"n2","resize",-1000,-1000,false)
model.box(model.lookup(shrunk,"n2").shape).width == 60
model.box(model.lookup(shrunk,"n2").shape).height == 40
let outside = model.transform(st.doc,"n2","move",9000,9000,false)
let b = model.box(model.lookup(outside,"n2").shape);
b.x+b.width == model.width and b.y+b.height == model.height
let linked = model.connect(model.connect({*:st,tool:"connect"},"n1"),"n4")
len(model.objects(linked.doc)) == 10
model.attr(model.lookup(linked.doc,linked.selected).shape,"from-shape") == "n1"
model.connect(model.connect(st,"n1"),"n1").doc == st.doc
let removed = model.remove({*:st,selected:"n3"})
len(model.objects(removed.doc)) == 5
model.lookup(removed.doc,"e2") == null and model.lookup(removed.doc,"e3") == null and model.lookup(removed.doc,"e4") == null
model.step_history(removed).doc == st.doc
let copied = model.duplicate({*:st,selected:"n2"})
copied.selected != "n2" and model.attr(model.lookup(copied.doc,copied.selected).shape,"label") == "Sketch the flow"
let edited = model.change({*:st,selected:"n2"},"label","Plan & build <together>")
model.attr(model.lookup(edited.doc,"n2").shape,"label") == "Plan & build <together>"
let undone = model.step_history(edited)
let branched = model.change({*:undone,selected:"n2"},"label","Branch");
len(branched.hist.redo) == 0
let restored = model.restore(parse(format(model.archive(edited.doc),'json'),'json')^)^;
restored == edited.doc
model.restore({version:99}) ^ { true } ~ { false }
model.restore({version:1,drawing:{tag:"drawing",content:[{tag:"layer",content:[]} ]}})^ == model.drawing([])
let invalid = model.drawing([model.edge("e1","missing","also-missing")]);
model.restore(parse(format(model.archive(invalid),'json'),'json')^) ^ { true } ~ { false }
let loaded = model.open_document(st,restored)
let first = model.add(loaded,"rect",0,0)
let second = model.add(first,"rect",0,100);
first.selected != second.selected and len(unique([for (node in model.objects(second.doc)) model.id(node)])) == 11
