import model: lambda.editor.draw.model
import routing: lambda.editor.draw.routing
import interaction: lambda.editor.draw.interaction
import codec: lambda.editor.draw.codec
import scene: lambda.editor.draw.scene
let points = [{x:100,y:100},{x:200,y:100},{x:200,y:300},{x:400,y:300}]
let moved = routing.move_segment(points,1,{x:50,y:15});
moved == [{x:100,y:100},{x:250,y:100},{x:250,y:300},{x:400,y:300}]
routing.move_segment(points,0,{x:15,y:40}) == [{x:100,y:100},{x:100,y:140},{x:200,y:140},{x:200,y:300},{x:400,y:300}]
routing.move_segment(points,2,{x:15,y:40}) == [{x:100,y:100},{x:200,y:100},{x:200,y:340},{x:400,y:340},{x:400,y:300}]
let edge = model.with_attrs(model.edge("e","",""),{'from-x':100,'from-y':100,'to-x':400,'to-y':300,waypoints:slice(points,1,3)})
let drawing = model.drawing([edge])
let st = {*:model.select_ids(model.initial(drawing),["e"]),zoom:1.0,grid:false}
let begin = interaction.begin(st,{x:200,y:180,point:{x:200,y:180},pointer:4});
begin.drag.mode == "segment" and begin.drag.segment == 1
let preview = interaction.update(begin,{kind:'preview',x:250,y:195,point:{x:250,y:195}});
model.route(preview.doc,model.lookup(preview.doc,"e").shape) == moved
len(preview.hist.undo) == 0
interaction.cancel(preview).doc == drawing
let committed = interaction.update(preview,{kind:'commit',x:250,y:195,point:{x:250,y:195}});
len(committed.hist.undo) == 1 and committed.drag == null
model.step_history(committed).doc == drawing
codec.decode(parse(format(codec.encode(committed.doc)^,'json'),'json')^)^ == committed.doc
codec.encode(committed.doc,{version:3}) ^ { true } ~ { false }
model.valid_node(model.lookup(committed.doc,"e").shape,[])
let reversed = model.reverse_edge(committed)^;
model.route(reversed.doc,model.lookup(reversed.doc,"e").shape) == reverse(moved)
let reset = model.edit_properties(committed,{waypoints:[],'bend-x':-1.0,'bend-y':-1.0})^;
len(model.route(reset.doc,model.lookup(reset.doc,"e").shape)) <= 3
let along = interaction.update(begin,{kind:'commit',x:200,y:210,point:{x:200,y:210}});
along.doc == drawing and len(along.hist.undo) == 0
let first = routing.move_segment([{x:100,y:100},{x:400,y:100}],0,{x:0,y:50});
first == [{x:100,y:100},{x:100,y:150},{x:400,y:150},{x:400,y:100}]
routing.move_segment([{x:100,y:100},{x:400,y:300}],0,{x:20,y:10},false) ==
    [{x:100,y:100},{x:120,y:110},{x:420,y:310},{x:400,y:300}]
let shape = model.make_shape("target","rect",600,100,"Target")
let attach_doc = model.drawing([edge,shape]);
model.hit_shape(attach_doc,{x:598,y:140},6) == "target"
model.hit_shape(attach_doc,{x:590,y:140},6) == ""
let attached = model.edit_edge_document(attach_doc,"e","to",{x:600,y:140},"target")
let markup = format(scene.selection_view(attached,["e"],1.0),'xml');
contains(markup,"edge-to-connection") and contains(markup,"connection-mark")
contains(markup,"edge-segment-0")
not contains(scene.inner_xml(scene.edge_view(drawing,edge,["e"])),"<root>")
not contains(format(scene.svg(attached,"","",false,true),'xml'),"connection-mark")
not model.valid_waypoints([{x:0,y:"bad"}])
let zoomed = interaction.begin({*:st,zoom:2.0},{x:400,y:360,point:{x:200,y:180},pointer:4})
let scaled = interaction.update(zoomed,{kind:'commit',x:500,y:390,point:{x:250,y:195}});
model.route(scaled.doc,model.lookup(scaled.doc,"e").shape) == moved
let scrolled = interaction.update(begin,{kind:'preview',x:200,y:180,point:{x:250,y:180}});
model.route(scrolled.doc,model.lookup(scrolled.doc,"e").shape) == moved
