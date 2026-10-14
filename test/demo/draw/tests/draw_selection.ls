import model: ~~.mod_model
import geom: lambda.editor.mod_geom
import scene: ~~.mod_scene
let st = model.initial()
let ids = ["n1","n2","n3","e1","e2"]
let grouped = model.select_ids(st,ids)
model.selected_ids(grouped) == ids
model.select_ids(st,["missing","n2","n2"]).selection == ["n2"]
model.enclosed(st.doc,{x:90,y:290,width:690,height:100}) == ids
model.enclosed(st.doc,{x:350,y:300,width:159,height:80}) == []
let moved_doc = model.move_selection(st.doc,ids,40,-20,false)
model.box(model.lookup(moved_doc,"n1").shape).x == 140
model.box(model.lookup(moved_doc,"n2").shape).x == 390
model.box(model.lookup(moved_doc,"n3").shape).y == 280
let moved = model.commit_edit(grouped,st.doc,moved_doc)
len(moved.hist.undo) == 1 and model.step_history(moved).doc == st.doc
let outside = model.move_selection(st.doc,["n1","n2"],-10000,10000,false)
model.box(model.lookup(outside,"n2").shape).x-model.box(model.lookup(outside,"n1").shape).x == 250
model.box(model.lookup(outside,"n1").shape).x == 0
model.move_selection(st.doc,[],20,20,false) == st.doc
let removed = model.remove(grouped)
len(model.objects(removed.doc)) == 2
model.step_history(removed).doc == st.doc
let align_st = model.select_ids(st,["n2","n4","n5"])
let aligned = model.align(align_st,"left")^;
all([for (target in ["n2","n4","n5"]) model.box(model.lookup(aligned.doc,target).shape).x == 350])
len(aligned.hist.undo) == 1 and model.step_history(aligned).doc == st.doc
let bottom = model.align(align_st,"bottom")^;
all([for (target in ["n2","n4","n5"]) model.box(model.lookup(bottom.doc,target).shape).y == 430])
let distributed = model.align(align_st,"distribute-x")^;
[for (target in ["n2","n4","n5"]) model.box(model.lookup(distributed.doc,target).shape).x] == [350,610,870]
model.align(model.select_ids(st,["n2"]),"left")^.doc == st.doc
let preview = model.preview_properties(model.select_ids(st,["n2"]),{label:"Live"})^;
len(preview.hist.undo) == 0 and model.attr(model.lookup(preview.doc,"n2").shape,"label") == "Live"
let changed = model.edit_properties(grouped,{stroke:"#123456"})^;
all([for (target in ids) model.attr(model.lookup(changed.doc,target).shape,"stroke") == "#123456"])
len(changed.hist.undo) == 1
model.edit_properties({*:st,selected:"n2"},{label:"Sketch the flow"})^.hist == st.hist
let detached = model.edit_edge_document(st.doc,"e1","to",{x:550,y:160})
model.endpoints(detached,model.lookup(detached,"e1").shape).to == {x:550,y:160}
model.attr(model.lookup(detached,"e1").shape,"to-shape") == ""
model.valid_node(model.lookup(detached,"e1").shape,["n1","n2","n3","n4","n5"])
let reversed = model.reverse_edge({*:st,doc:detached,selected:"e1"})^;
model.endpoints(reversed.doc,model.lookup(reversed.doc,"e1").shape).from == {x:550,y:160}
let free = model.edit_edge_document(detached,"e1","from",{x:200,y:100})
model.endpoints(free,model.lookup(free,"e1").shape).from == {x:200,y:100}
let free_moved = model.move_selection(free,["e1"],20,30,false)
model.endpoints(free_moved,model.lookup(free_moved,"e1").shape).to == {x:570,y:190}
let reattached = model.edit_edge_document(free,"e1","to",{x:950,y:220},"n4")
model.attr(model.lookup(reattached,"e1").shape,"to-shape") == "n4"
model.restore(parse(format(model.archive(free),'json'),'json')^)^ == free
let rotated = model.edit_properties({*:st,selected:"n2"},{rotation:90.0})^;
let b = model.world_box(model.lookup(rotated.doc,"n2").shape);
(abs(b.width-80)<0.001 and abs(b.height-160)<0.001);
model.hit_shape(rotated.doc,{x:430,y:270}) == "n2"
model.hit_shape(rotated.doc,{x:360,y:340}) == ""
let ends = model.endpoints(rotated.doc,model.lookup(rotated.doc,"e1").shape);
(abs(ends.to.x-390)<0.001 and abs(ends.to.y-340)<0.001);
let north = model.edit_properties({*:rotated,selected:"e1"},{'to-port':"north"})^;
(abs(model.endpoints(north.doc,model.lookup(north.doc,"e1").shape).to.x-470)<0.001);
let flipped = model.orient({*:rotated,selected:"n2"},"flip-x")
model.attr(model.lookup(flipped.doc,"n2").shape,"flip-x")
model.orient(flipped,"flip-x").doc == rotated.doc
model.restore(parse(format(model.archive(flipped.doc),'json'),'json')^)^ == flipped.doc
model.attr(model.lookup(model.rotate_document(st.doc,"n2",{x:530,y:340},true),"n2").shape,"rotation") == 90
let resized = model.resize_document(rotated.doc,"n2",-20,40,false)
let resized_node = model.lookup(resized,"n2").shape;
(abs(model.box(resized_node).width-200)<0.001 and abs(model.box(resized_node).height-100)<0.001);
(geom.v2_dist(model.turn_point(resized_node,{x:model.box(resized_node).x,y:model.box(resized_node).y},false),model.turn_point(model.lookup(rotated.doc,"n2").shape,{x:350,y:300},false))<0.001);
let aligned_rotated = model.align(model.select_ids(rotated,["n2","n4"]),"left")^;
(abs(model.world_box(model.lookup(aligned_rotated.doc,"n2").shape).x-model.world_box(model.lookup(aligned_rotated.doc,"n4").shape).x)<0.001);
let svg = format(scene.svg(flipped.doc,"","",false,true),'xml');
contains(svg,"rotate(90") and contains(svg,"scale(-1 1)") and not contains(svg,"selection-overlay")
let copied = model.duplicate(model.select_ids(st,["n1","n2"]))
len(model.objects(copied.doc)) == 12 and len(model.selected_ids(copied)) == 3
let copied_edge = [for (node in model.objects(copied.doc) where node.tag == 'connector' and contains(model.selected_ids(copied),model.id(node))) node][0];
contains(model.selected_ids(copied),model.attr(copied_edge,"from-shape")) and contains(model.selected_ids(copied),model.attr(copied_edge,"to-shape"))
len(copied.hist.undo) == 1 and model.step_history(copied).doc == st.doc
let inspector_free = model.edit_properties({*:st,selected:"e1"},{'to-shape':""})^;
model.endpoints(inspector_free.doc,model.lookup(inspector_free.doc,"e1").shape).to == {x:350,y:340}
