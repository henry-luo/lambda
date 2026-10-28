import m: ~~.mod_model
let st = m.initial(m.blank())
let rect = m.make_object("o2","rect",{x:10.0,y:10.0,width:50.0,height:40.0})
let first = m.insert(st,rect)
let added = m.layer_action(first,"add")
len(added.doc.layers) == 2 and added.active == "l3"
let top = m.insert(added,{*:rect,id:"o4",fill:"#ffffff"})
m.hit(top.doc,{x:30.0,y:30.0}) == "o4"
let hidden = m.layer_action(top,"visible",top.active)
m.hit(hidden.doc,{x:30.0,y:30.0}) == "o2"
let locked = m.layer_action(top,"locked",top.active)
m.hit(locked.doc,{x:30.0,y:30.0}) == "o2"
m.begin({*:locked,tool:"brush"},{x:25.0,y:25.0}).drag == null
m.insert(locked,{*:rect,id:"o5"}).doc == locked.doc
m.remove({*:locked,selected:"o4"}).doc == locked.doc
let lowered = m.layer_action(top,"down")
lowered.doc.layers[0].id == added.active and m.hit(lowered.doc,{x:30.0,y:30.0}) == "o2"
m.step_history(lowered).doc == top.doc
let removed = m.layer_action(top,"delete")
len(removed.doc.layers) == 1 and removed.active == "l1"
m.step_history(removed).doc == top.doc
m.layer_action(first,"delete").doc == first.doc
let renamed = m.finish_edit(m.property(top,"layer_name","My ink")^)
m.layer_of(renamed.doc,renamed.active).name == "My ink"
let faded = m.finish_edit(m.property(top,"layer_opacity",0.0)^)
m.hit(faded.doc,{x:30.0,y:30.0}) == "o2"
m.decode({format:"lambda-sketch",version:99,document:top.doc}) ^ { true } ~ { false }
m.decode(m.encode({*:top.doc,layers:[]})) ^ { true } ~ { false }
m.decode(m.encode({*:top.doc,width:nan})) ^ { true } ~ { false }
m.decode(m.encode({*:top.doc,seq:4})) ^ { true } ~ { false }
m.decode(m.encode({*:first.doc,layers:[{*:first.doc.layers[0],objects:[rect,rect]}]})) ^ { true } ~ { false }
m.decode(m.encode({*:first.doc,layers:[{*:first.doc.layers[0],objects:[{*:rect,width:-1}]}]})) ^ { true } ~ { false }
m.decode(m.encode({*:first.doc,layers:[{*:first.doc.layers[0],objects:[{*:rect,fill:"red"}]}]})) ^ { true } ~ { false }
let roundtrip = m.decode(parse(format(m.encode(top.doc),'json'),'json')^ )^
let opened = m.open_document(st,roundtrip)
opened.doc == top.doc and opened.saved == opened.doc
let next = m.insert(opened,{*:rect,id:"o" ++ string(opened.doc.seq)})
len(unique([for (o in m.objects(next.doc)) o.id])) == 3
