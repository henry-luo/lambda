import draw: lambda.editor.draw
import model: lambda.editor.draw.model
import interaction: lambda.editor.draw.interaction
import viewport: lambda.editor.draw.viewport
let a = draw.vertex("a","rect",{x:80,y:100,width:120,height:80})^
let b = draw.vertex("b","ellipse",{x:350,y:100,width:120,height:80})^
let start = draw.exec(draw.open_session(draw.new_document({width:800,height:600}))^,{kind:'insert',objects:[a,b]})^
let selected = draw.update(start,{kind:'begin',x:140,y:140,point:{x:140,y:140},pointer:1})
let preview = draw.update(selected,{kind:'preview',x:180,y:160,point:{x:180,y:160}});
model.box(draw.lookup(preview.doc,"a").shape).x == 120
len(preview.hist.undo) == 1
let canceled = draw.update(preview,{kind:'cancel'});
canceled.doc == start.doc and len(canceled.hist.undo) == 1 and canceled.drag == null
let committed = draw.update(preview,{kind:'commit',x:180,y:160,point:{x:180,y:160}});
len(committed.hist.undo) == 2 and committed.drag == null;
(draw.exec(committed,{kind:'undo'})^).doc == start.doc
let noop = draw.update(selected,{kind:'commit',x:140,y:140,point:{x:140,y:140}});
len(noop.hist.undo) == 1
let additive = draw.update(model.select_ids(start,["a"]),{kind:'begin',x:410,y:140,point:{x:410,y:140},pointer:1,shift:true});
model.selected_ids(additive) == ["a","b"] and additive.drag == null
let rotate = draw.update(model.select_ids(start,["a"]),{kind:'begin',x:140,y:72,point:{x:140,y:72},pointer:1})
let rotated = draw.update(rotate,{kind:'commit',x:220,y:140,point:{x:220,y:140},shift:true});
model.attr(draw.lookup(rotated.doc,"a").shape,"rotate") == 90
len(draw.validate(rotated.doc)) == 0
viewport.point({x:100,y:200},2.0,180,260) == {x:40,y:30}
let shifted = draw.exec(start,{kind:'move',ids:["a"],dx:1,dy:0})^;
model.box(draw.lookup(shifted.doc,"a").shape).x == 81;
(draw.exec(shifted,{kind:'undo'})^).doc == start.doc
draw.exec(start,{kind:'set',id:"a",properties:{id:"new"}}) ^ { true } ~ { false }
draw.exec(start,{kind:'reconnect',id:"a",side:"to",endpoint:{kind:'point',x:0,y:0}}) ^ { true } ~ { false }
